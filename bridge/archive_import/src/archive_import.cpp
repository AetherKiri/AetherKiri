#include "aether/archive_import.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <system_error>
#include <vector>

#include <lz4frame.h>

#include <Common/MyCom.h>
#include <Common/MyString.h>
#include <Common/UTFConvert.h>
#include <Windows/PropVariant.h>
#include <7zip/Archive/IArchive.h>
#include <7zip/Common/FileStreams.h>
#include <7zip/IPassword.h>

extern "C" HRESULT WINAPI CreateObject(const GUID *, const GUID *, void **);
extern "C" HRESULT WINAPI GetHandlerProperty2(UInt32, PROPID, PROPVARIANT *);
extern "C" HRESULT WINAPI GetIsArc(UInt32, Func_IsArc *);
extern "C" HRESULT WINAPI GetNumberOfFormats(UInt32 *);

namespace aether::archive_import {
namespace {

namespace fs = std::filesystem;
using NWindows::NCOM::CPropVariant;

constexpr UInt64 kMaxSignatureScan = UINT64_C(8) * 1024 * 1024 * 1024;
constexpr std::uint32_t kLz4FrameMagic = UINT32_C(0x184D2204);

const std::set<std::string> kTransportFormats = {
    "7z", "apfs", "apm", "ar", "arj", "bzip2", "cab", "chm", "compound",
    "cpio", "cramfs", "dmg", "ext", "fat", "gpt", "gzip", "hfs", "hxs",
    "iso", "lp", "lzh", "lzma", "lzma86", "mbr", "mslz", "mub", "ntfs",
    "nsis", "ppmd", "qcow", "rar", "rar5", "rpm", "sparse", "split",
    "squashfs", "tar", "udf", "uefic", "uefif", "vdi", "vhd", "vhdx",
    "vmdk", "wim", "xar", "xz", "z", "zip", "zstd"
};

std::string WideToUtf8(const wchar_t *value) {
    if (value == nullptr) return {};
    AString utf8;
    ConvertUnicodeToUTF8(UString(value), utf8);
    return std::string(utf8.Ptr(), utf8.Len());
}

UString Utf8ToWide(const std::string &value) {
    UString result;
    ConvertUTF8ToUnicode(AString(value.c_str()), result);
    return result;
}

FString Utf8ToFileString(const std::string &value) {
    return us2fs(Utf8ToWide(value));
}

std::string VariantString(const PROPVARIANT &value) {
    return value.vt == VT_BSTR ? WideToUtf8(value.bstrVal) : std::string();
}

bool VariantBool(const PROPVARIANT &value) {
    return value.vt == VT_BOOL && value.boolVal != VARIANT_FALSE;
}

std::uint32_t VariantUInt32(const PROPVARIANT &value) {
    if (value.vt == VT_UI4) return value.ulVal;
    if (value.vt == VT_UI8) return static_cast<std::uint32_t>(value.uhVal.QuadPart);
    return 0;
}

bool IsSafeRelativePath(const fs::path &path) {
    if (path.empty() || path.is_absolute() || path.has_root_name()) return false;
    for (const auto &part : path) {
        if (part == "..") return false;
    }
    return true;
}

std::string FormatName(UInt32 index) {
    CPropVariant value;
    if (GetHandlerProperty2(index, NArchive::NHandlerPropID::kName, &value) != S_OK)
        return {};
    std::string result = VariantString(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

bool FormatClassId(UInt32 index, GUID *class_id) {
    CPropVariant value;
    if (GetHandlerProperty2(index, NArchive::NHandlerPropID::kClassID, &value) != S_OK ||
        value.vt != VT_BSTR || SysStringByteLen(value.bstrVal) != sizeof(GUID))
        return false;
    std::memcpy(class_id, value.bstrVal, sizeof(GUID));
    return true;
}

std::vector<std::string> FormatExtensions(UInt32 index) {
    CPropVariant value;
    if (GetHandlerProperty2(index, NArchive::NHandlerPropID::kExtension, &value) != S_OK)
        return {};
    std::vector<std::string> result;
    std::string extensions = VariantString(value);
    std::size_t begin = 0;
    while (begin < extensions.size()) {
        const std::size_t end = extensions.find(' ', begin);
        std::string extension = extensions.substr(begin, end - begin);
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!extension.empty()) result.push_back(std::move(extension));
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return result;
}

struct EmbeddedMatch {
    std::uint64_t offset = std::numeric_limits<std::uint64_t>::max();
    std::string format;
};

bool IsValidLz4FrameAt(const fs::path &path, std::uint64_t offset);

EmbeddedMatch FindEmbeddedTransport(const fs::path &path,
                                    const ProgressCallback &progress = {}) {
    const std::vector<std::pair<std::string, std::vector<unsigned char>>> signatures = {
        {"7z", {0x37, 0x7a, 0xbc, 0xaf, 0x27, 0x1c}},
        {"zip", {'P', 'K', 0x03, 0x04}},
        {"zip", {'P', 'K', 0x05, 0x06}},
        {"zip", {'P', 'K', 0x06, 0x06}},
        {"rar", {'R', 'a', 'r', '!', 0x1a, 0x07}},
        {"rar5", {'R', 'a', 'r', '!', 0x1a, 0x07, 0x01, 0x00}},
        {"lz4", {0x04, 0x22, 0x4d, 0x18}},
    };
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::error_code size_error;
    const auto total_size = fs::file_size(path, size_error);
    constexpr std::size_t kChunk = 1024 * 1024;
    constexpr std::size_t kOverlap = 16;
    std::vector<unsigned char> buffer(kChunk + kOverlap);
    std::size_t carried = 0;
    std::uint64_t absolute = 0;
    while (input && absolute < kMaxSignatureScan) {
        const std::uint64_t remaining = kMaxSignatureScan - absolute;
        const std::size_t request = static_cast<std::size_t>(
            std::min<std::uint64_t>(kChunk, remaining));
        input.read(reinterpret_cast<char *>(buffer.data() + carried),
                   static_cast<std::streamsize>(request));
        const std::size_t read = static_cast<std::size_t>(input.gcount());
        const std::size_t available = carried + read;
        for (const auto &entry : signatures) {
            const auto found = std::search(buffer.begin(), buffer.begin() + available,
                                           entry.second.begin(), entry.second.end());
            if (found != buffer.begin() + available) {
                const auto offset = absolute - carried +
                    static_cast<std::uint64_t>(found - buffer.begin());
                if (entry.first != "lz4" || IsValidLz4FrameAt(path, offset))
                    return {offset, entry.first};
            }
        }
        if (read == 0) break;
        carried = std::min(kOverlap, available);
        std::copy(buffer.begin() + available - carried, buffer.begin() + available, buffer.begin());
        absolute += read;
        if (progress) progress(absolute, size_error ? 0 : total_size);
    }
    if (progress) progress(std::min(absolute, kMaxSignatureScan), size_error ? 0 : total_size);
    return {};
}

std::uint64_t FindEmbeddedBytes(const fs::path &path,
                                const std::vector<Byte> &signature) {
    if (signature.empty()) return std::numeric_limits<std::uint64_t>::max();
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::numeric_limits<std::uint64_t>::max();
    constexpr std::size_t kChunk = 1024 * 1024;
    const std::size_t overlap = signature.size() > 1 ? signature.size() - 1 : 0;
    std::vector<unsigned char> buffer(kChunk + overlap);
    std::size_t carried = 0;
    std::uint64_t absolute = 0;
    while (input && absolute < kMaxSignatureScan) {
        const std::uint64_t remaining = kMaxSignatureScan - absolute;
        const std::size_t request = static_cast<std::size_t>(std::min<std::uint64_t>(kChunk, remaining));
        input.read(reinterpret_cast<char *>(buffer.data() + carried),
                   static_cast<std::streamsize>(request));
        const std::size_t read = static_cast<std::size_t>(input.gcount());
        const std::size_t available = carried + read;
        const auto found = std::search(buffer.begin(), buffer.begin() + available,
                                       signature.begin(), signature.end());
        if (found != buffer.begin() + available)
            return absolute - carried + static_cast<std::uint64_t>(found - buffer.begin());
        if (read == 0) break;
        carried = std::min(overlap, available);
        if (carried != 0)
            std::copy(buffer.begin() + available - carried, buffer.begin() + available,
                      buffer.begin());
        absolute += read;
    }
    return std::numeric_limits<std::uint64_t>::max();
}

bool IsValidLz4FrameAt(const fs::path &path, std::uint64_t offset) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    input.seekg(static_cast<std::streamoff>(offset));
    std::vector<char> header(64 * 1024);
    input.read(header.data(), static_cast<std::streamsize>(header.size()));
    const std::size_t size = static_cast<std::size_t>(input.gcount());
    if (size < sizeof(kLz4FrameMagic)) return false;
    LZ4F_decompressionContext_t context = nullptr;
    const size_t created = LZ4F_createDecompressionContext(&context, LZ4F_VERSION);
    if (LZ4F_isError(created)) return false;
    LZ4F_frameInfo_t info{};
    size_t available = size;
    const size_t decoded = LZ4F_getFrameInfo(context, &info, header.data(), &available);
    LZ4F_freeDecompressionContext(context);
    return !LZ4F_isError(decoded);
}

std::uint64_t FindLz4Frame(const fs::path &path, const ProgressCallback &progress = {}) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::numeric_limits<std::uint64_t>::max();
    constexpr std::size_t kChunk = 1024 * 1024;
    std::vector<unsigned char> buffer(kChunk + 3);
    std::size_t carried = 0;
    std::uint64_t absolute = 0;
    while (input && absolute < kMaxSignatureScan) {
        const std::uint64_t remaining = kMaxSignatureScan - absolute;
        const std::size_t request = static_cast<std::size_t>(
            std::min<std::uint64_t>(kChunk, remaining));
        input.read(reinterpret_cast<char *>(buffer.data() + carried),
                   static_cast<std::streamsize>(request));
        const std::size_t read = static_cast<std::size_t>(input.gcount());
        const std::size_t available = carried + read;
        for (std::size_t i = 0; i + sizeof(kLz4FrameMagic) <= available; ++i) {
            const std::uint32_t magic = static_cast<std::uint32_t>(buffer[i]) |
                (static_cast<std::uint32_t>(buffer[i + 1]) << 8) |
                (static_cast<std::uint32_t>(buffer[i + 2]) << 16) |
                (static_cast<std::uint32_t>(buffer[i + 3]) << 24);
            if (magic == kLz4FrameMagic) {
                const auto offset = absolute - carried + i;
                if (IsValidLz4FrameAt(path, offset)) return offset;
            }
        }
        if (read == 0) break;
        carried = std::min<std::size_t>(3, available);
        std::copy(buffer.begin() + available - carried, buffer.begin() + available,
                  buffer.begin());
        absolute += read;
        if (progress) {
            std::error_code size_error;
            const auto total = fs::file_size(path, size_error);
            progress(absolute, size_error ? 0 : total);
        }
    }
    return std::numeric_limits<std::uint64_t>::max();
}

bool IsLz4Frame(const fs::path &path, std::uint64_t *offset = nullptr,
                const ProgressCallback &progress = {}) {
    const std::uint64_t found = FindLz4Frame(path, progress);
    if (offset != nullptr) *offset = found;
    return found != std::numeric_limits<std::uint64_t>::max();
}

bool HandlerSignatures(UInt32 index, std::vector<std::vector<Byte>> *signatures,
                       UInt32 *signature_offset) {
    signatures->clear();
    *signature_offset = 0;
    CPropVariant offset;
    if (GetHandlerProperty2(index, NArchive::NHandlerPropID::kSignatureOffset, &offset) == S_OK)
        *signature_offset = VariantUInt32(offset);
    CPropVariant value;
    if (GetHandlerProperty2(index, NArchive::NHandlerPropID::kSignature, &value) == S_OK &&
        value.vt == VT_BSTR && SysStringByteLen(value.bstrVal) != 0) {
        const auto *bytes = reinterpret_cast<const Byte *>(value.bstrVal);
        signatures->emplace_back(bytes, bytes + SysStringByteLen(value.bstrVal));
        return true;
    }
    if (GetHandlerProperty2(index, NArchive::NHandlerPropID::kMultiSignature, &value) != S_OK ||
        value.vt != VT_BSTR)
        return false;
    const auto *bytes = reinterpret_cast<const Byte *>(value.bstrVal);
    std::size_t remaining = SysStringByteLen(value.bstrVal);
    while (remaining != 0) {
        const std::size_t size = *bytes++;
        --remaining;
        if (size > remaining) return false;
        signatures->emplace_back(bytes, bytes + size);
        bytes += size;
        remaining -= size;
    }
    return !signatures->empty();
}

std::string StrongFormat(const fs::path &path) {
    return FindEmbeddedTransport(path).format;
}

bool ExtractLz4(const fs::path &source, const fs::path &destination,
               const ExtractOptions &options, ExtractResult *result,
               std::uint64_t *total_bytes, std::uint32_t *total_files,
               const ProgressCallback &progress = {}) {
    std::uint64_t offset = 0;
    if (!IsLz4Frame(source, &offset, progress)) return false;
    std::ifstream input(source, std::ios::binary);
    if (!input) { result->error = "Cannot read the selected file"; return false; }
    input.seekg(static_cast<std::streamoff>(offset));
    std::error_code ec;
    fs::create_directories(destination, ec);
    if (ec) { result->error = "Cannot create the extraction destination"; return false; }
    const fs::path target = destination / fs::u8path(source.stem().u8string());
    auto *out = new std::ofstream(target, std::ios::binary | std::ios::trunc);
    if (!*out) { delete out; result->error = "Cannot create an extracted file"; return false; }
    LZ4F_decompressionContext_t context = nullptr;
    const size_t created = LZ4F_createDecompressionContext(&context, LZ4F_VERSION);
    if (LZ4F_isError(created)) { delete out; result->error = "LZ4 decoder unavailable"; return false; }
    std::vector<char> input_buffer(1024 * 1024), output_buffer(1024 * 1024);
    size_t hint = 1;
    std::uint64_t written = 0;
    std::uint64_t input_position = offset;
    bool ok = true;
    while (hint != 0 && input) {
        input.read(input_buffer.data(), static_cast<std::streamsize>(input_buffer.size()));
        size_t input_size = static_cast<size_t>(input.gcount());
        if (progress) {
            std::error_code size_error;
            const auto total = fs::file_size(source, size_error);
            input_position += input_size;
            progress(input_position, size_error ? 0 : total);
        }
        const char *cursor = input_buffer.data();
        while (input_size != 0) {
            size_t output_size = output_buffer.size();
            size_t consumed = input_size;
            hint = LZ4F_decompress(context, output_buffer.data(), &output_size,
                                   cursor, &consumed, nullptr);
            if (LZ4F_isError(hint)) { ok = false; break; }
            cursor += consumed;
            input_size -= consumed;
            written += output_size;
            if (written > options.max_total_bytes || *total_bytes > options.max_total_bytes - written) {
                result->error = "Archive expands beyond the configured size limit";
                ok = false;
                break;
            }
            out->write(output_buffer.data(), static_cast<std::streamsize>(output_size));
            if (!*out) { result->error = "Cannot write an extracted file"; ok = false; break; }
            if (consumed == 0 && output_size == 0) { ok = false; break; }
        }
        if (!ok) break;
    }
    LZ4F_freeDecompressionContext(context);
    out->close();
    delete out;
    if (!ok || hint != 0) {
        if (result->error.empty()) result->error = "Invalid LZ4 frame";
        fs::remove_all(destination, ec);
        return false;
    }
    if (++(*total_files) > options.max_files) {
        result->error = "Archive contains too many files";
        fs::remove_all(destination, ec);
        return false;
    }
    *total_bytes += written;
    result->format = "lz4";
    return true;
}

class OpenCallback final : public IArchiveOpenCallback,
                           public IArchiveOpenVolumeCallback,
                           public ICryptoGetTextPassword,
                           public CMyUnknownImp {
public:
    OpenCallback(fs::path source, std::string password)
        : source_(std::move(source)), password_(std::move(password)) {}

    bool password_requested = false;

    Z7_COM_UNKNOWN_IMP_3(IArchiveOpenCallback, IArchiveOpenVolumeCallback,
                         ICryptoGetTextPassword)
    Z7_IFACE_COM7_IMP(IArchiveOpenCallback)
    Z7_IFACE_COM7_IMP(IArchiveOpenVolumeCallback)
    Z7_IFACE_COM7_IMP(ICryptoGetTextPassword)

private:
    fs::path source_;
    std::string password_;
};

HRESULT OpenCallback::SetTotal(const UInt64 *, const UInt64 *) throw() { return S_OK; }
HRESULT OpenCallback::SetCompleted(const UInt64 *, const UInt64 *) throw() { return S_OK; }

HRESULT OpenCallback::GetProperty(PROPID prop_id, PROPVARIANT *value) throw() {
    CPropVariant property;
    if (prop_id == kpidName) property = Utf8ToWide(source_.filename().u8string());
    property.Detach(value);
    return S_OK;
}

HRESULT OpenCallback::GetStream(const wchar_t *name, IInStream **stream) throw() {
    *stream = nullptr;
    const fs::path relative = fs::u8path(WideToUtf8(name));
    if (!IsSafeRelativePath(relative) || relative.has_parent_path()) return S_FALSE;
    const fs::path candidate = source_.parent_path() / relative;
    auto *file = new CInFileStream;
    CMyComPtr<IInStream> holder = file;
    if (!file->Open(Utf8ToFileString(candidate.u8string()))) return S_FALSE;
    *stream = holder.Detach();
    return S_OK;
}

HRESULT OpenCallback::CryptoGetTextPassword(BSTR *password) throw() {
    password_requested = true;
    if (password_.empty()) return E_ABORT;
    return StringToBstr(Utf8ToWide(password_).Ptr(), password);
}

struct OpenArchive {
    CMyComPtr<IInArchive> archive;
    CMyComPtr<IInStream> stream;
    std::string format;
    bool encrypted = false;
};

bool OpenRecognized(const fs::path &path, const std::string &password,
                    OpenArchive *result, std::string *error,
                    const ProgressCallback &progress = {}) {
    std::error_code ec;
    const auto file_size = fs::file_size(path, ec);
    if (ec) {
        *error = "Cannot read the selected file";
        return false;
    }

    UInt32 format_count = 0;
    if (GetNumberOfFormats(&format_count) != S_OK) {
        *error = "7-Zip format registry is unavailable";
        return false;
    }
    std::string extension = path.extension().u8string();
    if (!extension.empty() && extension.front() == '.') extension.erase(extension.begin());
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    struct Candidate { UInt32 index; UInt64 offset; };
    std::vector<Candidate> candidates;
    std::vector<Byte> header(static_cast<std::size_t>(std::min<std::uint64_t>(file_size, 1024 * 1024)));
    {
        std::ifstream input(path, std::ios::binary);
        input.read(reinterpret_cast<char *>(header.data()), static_cast<std::streamsize>(header.size()));
        header.resize(static_cast<std::size_t>(input.gcount()));
    }
    for (UInt32 index = 0; index < format_count; ++index) {
        const std::string format = FormatName(index);
        if (kTransportFormats.count(format) == 0) continue;
        UInt64 offset = std::numeric_limits<UInt64>::max();
        const auto extensions = FormatExtensions(index);
        if (std::find(extensions.begin(), extensions.end(), extension) != extensions.end())
            offset = 0;
        Func_IsArc is_arc = nullptr;
        if (offset == std::numeric_limits<UInt64>::max() && !header.empty() &&
            GetIsArc(index, &is_arc) == S_OK && is_arc != nullptr &&
            is_arc(header.data(), header.size()) == k_IsArc_Res_YES)
            offset = 0;
        if (offset != std::numeric_limits<UInt64>::max())
            candidates.push_back({index, offset});
    }
    bool embedded_scanned = false;
    for (std::size_t candidate_index = 0;;) {
        if (candidate_index >= candidates.size()) {
            if (embedded_scanned) break;
            embedded_scanned = true;
            const EmbeddedMatch embedded = FindEmbeddedTransport(path, progress);
            if (embedded.offset == std::numeric_limits<std::uint64_t>::max()) break;
            for (UInt32 index = 0; index < format_count; ++index) {
                if (FormatName(index) != embedded.format) continue;
                const Candidate candidate{index, embedded.offset};
                const bool duplicate = std::any_of(candidates.begin(), candidates.end(),
                    [&](const Candidate &existing) {
                        return existing.index == candidate.index &&
                               existing.offset == candidate.offset;
                    });
                if (!duplicate) candidates.push_back(candidate);
                break;
            }
            continue;
        }
        const Candidate candidate = candidates[candidate_index++];
        const UInt32 index = candidate.index;
        const std::string format = FormatName(index);
        GUID class_id{};
        if (!FormatClassId(index, &class_id)) continue;

        CMyComPtr<IInArchive> archive;
        if (CreateObject(&class_id, &IID_IInArchive,
                         reinterpret_cast<void **>(&archive)) != S_OK || !archive)
            continue;
        auto *file = new CInFileStream;
        CMyComPtr<IInStream> stream = file;
        if (!file->Open(Utf8ToFileString(path.u8string()))) continue;
        UInt64 new_position = 0;
        if (candidate.offset != 0 &&
            stream->Seek(static_cast<Int64>(candidate.offset), STREAM_SEEK_SET,
                         &new_position) != S_OK)
            continue;
        auto *callback_impl = new OpenCallback(path, password);
        CMyComPtr<IArchiveOpenCallback> callback = callback_impl;
        const UInt64 scan = std::min<UInt64>(file_size - candidate.offset, kMaxSignatureScan);
        const HRESULT opened = archive->Open(stream, &scan, callback);
        if (opened != S_OK) {
            if (callback_impl->password_requested)
                *error = "The archive password is missing or incorrect";
            continue;
        }

        UInt32 count = 0;
        if (archive->GetNumberOfItems(&count) != S_OK || count == 0) {
            archive->Close();
            continue;
        }
        bool encrypted = false;
        for (UInt32 item = 0; item < count; ++item) {
            CPropVariant property;
            if (archive->GetProperty(item, kpidEncrypted, &property) == S_OK &&
                VariantBool(property)) {
                encrypted = true;
                break;
            }
        }
        result->archive = archive;
        result->stream = stream;
        result->format = format;
        result->encrypted = encrypted;
        return true;
    }
    return false;
}

class ExtractCallback final : public IArchiveExtractCallback,
                              public ICryptoGetTextPassword,
                              public CMyUnknownImp {
public:
    ExtractCallback(IInArchive *archive, fs::path destination,
                    fs::path default_entry_name,
                    const ExtractOptions &options, std::uint64_t *total_bytes,
                    std::uint32_t *total_files, ProgressCallback progress)
        : archive_(archive), destination_(std::move(destination)),
          default_entry_name_(std::move(default_entry_name)), options_(options),
          total_bytes_(total_bytes), total_files_(total_files), progress_(std::move(progress)) {}

    Z7_COM_UNKNOWN_IMP_2(IArchiveExtractCallback, ICryptoGetTextPassword)
    Z7_IFACE_COM7_IMP(IProgress)
    Z7_IFACE_COM7_IMP(IArchiveExtractCallback)
    Z7_IFACE_COM7_IMP(ICryptoGetTextPassword)

public:
    std::string error;
    bool password_required = false;

private:
    IInArchive *archive_;
    fs::path destination_;
    fs::path default_entry_name_;
    const ExtractOptions &options_;
    std::uint64_t *total_bytes_;
    std::uint32_t *total_files_;
    CMyComPtr<ISequentialOutStream> current_stream_;
    ProgressCallback progress_;
    UInt64 progress_total_ = 0;
};

HRESULT ExtractCallback::SetTotal(UInt64 total) throw() {
    if (total > options_.max_total_bytes ||
        *total_bytes_ > options_.max_total_bytes - total) {
        error = "Archive expands beyond the configured size limit";
        return E_ABORT;
    }
    progress_total_ = total;
    if (progress_) progress_(0, progress_total_);
    return S_OK;
}

HRESULT ExtractCallback::SetCompleted(const UInt64 *completed) throw() {
    if (progress_ && completed != nullptr) progress_(*completed, progress_total_);
    return S_OK;
}

HRESULT ExtractCallback::GetStream(UInt32 index, ISequentialOutStream **out_stream,
                                   Int32 ask_mode) throw() {
    *out_stream = nullptr;
    if (ask_mode != NArchive::NExtract::NAskMode::kExtract) return S_OK;
    CPropVariant path_value;
    const bool has_path = archive_->GetProperty(index, kpidPath, &path_value) == S_OK &&
                          path_value.vt == VT_BSTR &&
                          !VariantString(path_value).empty();
    fs::path relative = has_path
        ? fs::u8path(VariantString(path_value)).lexically_normal()
        : default_entry_name_.lexically_normal();
    if (!IsSafeRelativePath(relative)) {
        error = "Archive entry attempts to escape the destination";
        return E_ABORT;
    }
    CPropVariant dir_value;
    const bool is_dir = archive_->GetProperty(index, kpidIsDir, &dir_value) == S_OK &&
                        VariantBool(dir_value);
    std::error_code ec;
    const fs::path target = destination_ / relative;
    if (is_dir) {
        fs::create_directories(target, ec);
        return ec ? E_FAIL : S_OK;
    }
    if (++(*total_files_) > options_.max_files) {
        error = "Archive contains too many files";
        return E_ABORT;
    }
    CPropVariant size_value;
    if (archive_->GetProperty(index, kpidSize, &size_value) == S_OK &&
        size_value.vt == VT_UI8) {
        const UInt64 size = size_value.uhVal.QuadPart;
        if (size > options_.max_total_bytes ||
            *total_bytes_ > options_.max_total_bytes - size) {
            error = "Archive expands beyond the configured size limit";
            return E_ABORT;
        }
        *total_bytes_ += size;
    }
    fs::create_directories(target.parent_path(), ec);
    if (ec) return E_FAIL;
    auto *file = new COutFileStream;
    current_stream_ = file;
    if (!file->Create_ALWAYS(Utf8ToFileString(target.u8string()))) {
        error = "Cannot create an extracted file";
        current_stream_.Release();
        return E_FAIL;
    }
    *out_stream = current_stream_;
    (*out_stream)->AddRef();
    return S_OK;
}

HRESULT ExtractCallback::PrepareOperation(Int32) throw() { return S_OK; }

HRESULT ExtractCallback::SetOperationResult(Int32 result) throw() {
    current_stream_.Release();
    if (result == NArchive::NExtract::NOperationResult::kOK) return S_OK;
    password_required = result == NArchive::NExtract::NOperationResult::kWrongPassword ||
                        result == NArchive::NExtract::NOperationResult::kDataError;
    error = password_required ? "The archive password is missing or incorrect"
                              : "7-Zip could not extract an archive entry";
    return E_ABORT;
}

HRESULT ExtractCallback::CryptoGetTextPassword(BSTR *password) throw() {
    if (options_.password.empty()) {
        password_required = true;
        return E_ABORT;
    }
    return StringToBstr(Utf8ToWide(options_.password).Ptr(), password);
}

std::string ArchiveStem(const fs::path &path) {
    std::string name = path.filename().u8string();
    const std::string lower = [&] {
        std::string value = name;
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }();
    const auto volume = lower.rfind(".7z.001");
    if (volume != std::string::npos && volume + 7 == lower.size()) return name.substr(0, volume);
    const auto part = lower.rfind(".part1.rar");
    if (part != std::string::npos && part + 10 == lower.size()) return name.substr(0, part);
    return path.stem().u8string();
}

fs::path UniqueDestination(const fs::path &parent, const std::string &stem) {
    fs::path candidate = parent / fs::u8path(stem.empty() ? "Imported Game" : stem);
    for (unsigned suffix = 2; fs::exists(candidate); ++suffix)
        candidate = parent / fs::u8path((stem.empty() ? "Imported Game" : stem) +
                                        " (" + std::to_string(suffix) + ")");
    return candidate;
}

bool ExtractOne(const fs::path &source, const fs::path &destination,
                const ExtractOptions &options, ExtractResult *result,
                std::uint64_t *total_bytes, std::uint32_t *total_files,
                const ProgressCallback &progress = {}) {
    if (IsLz4Frame(source, nullptr, progress))
        return ExtractLz4(source, destination, options, result, total_bytes, total_files, progress);
    OpenArchive opened;
    std::string open_error;
    ExtractOptions resolved_options = options;
    std::vector<std::string> passwords = options.passwords;
    if (passwords.empty()) passwords.push_back(options.password);
    bool opened_archive = false;
    for (const std::string &password : passwords) {
        resolved_options.password = password;
        if (OpenRecognized(source, password, &opened, &open_error)) {
            opened_archive = true;
            break;
        }
    }
    if (!opened_archive) {
        const std::string strong_format = StrongFormat(source);
        if (!strong_format.empty() && strong_format != "lz4") {
            result->password_required =
                open_error == "The archive password is missing or incorrect";
            result->format = strong_format;
            result->error = result->password_required
                ? open_error : "The archive is damaged or unsupported";
            return false;
        }
        result->error = open_error.empty() ? "The file is not a supported archive" : open_error;
        return false;
    }
    std::error_code ec;
    fs::create_directories(destination, ec);
    if (ec) {
        result->error = "Cannot create the extraction destination";
        return false;
    }
    auto *callback_impl = new ExtractCallback(opened.archive, destination, source.stem(), resolved_options,
                                              total_bytes, total_files, progress);
    CMyComPtr<IArchiveExtractCallback> callback = callback_impl;
    const HRESULT extracted = opened.archive->Extract(nullptr, UINT32_MAX, 0, callback);
    opened.archive->Close();
    if (extracted != S_OK || !callback_impl->error.empty()) {
        result->password_required = callback_impl->password_required ||
                                    (opened.encrypted && options.password.empty());
        result->error = callback_impl->error.empty()
            ? "7-Zip failed to extract the archive" : callback_impl->error;
        return false;
    }
    result->format = opened.format;
    return true;
}

}  // namespace

ProbeResult Probe(const std::string &path, const ProgressCallback &progress) {
    ProbeResult result;
    const fs::path source = fs::u8path(path);
    if (IsLz4Frame(source)) {
        result.recognized = true;
        result.format = "lz4";
        return result;
    }
    OpenArchive opened;
    if (OpenRecognized(source, {}, &opened, &result.error, progress)) {
        result.recognized = true;
        result.encrypted = opened.encrypted;
        result.format = opened.format;
        opened.archive->Close();
    } else {
        const std::string strong_format = FindEmbeddedTransport(source, progress).format;
        if (!strong_format.empty()) {
            result.recognized = true;
            result.encrypted =
                result.error == "The archive password is missing or incorrect";
            result.format = strong_format;
            if (result.encrypted)
                result.error.clear();
        }
    }
    return result;
}

ExtractResult ExtractRecursive(const std::string &path,
                               const std::string &destination,
                               const ExtractOptions &options,
                               const ProgressCallback &progress) {
    ExtractResult result;
    const fs::path source = fs::u8path(path);
    const fs::path root = UniqueDestination(fs::u8path(destination), ArchiveStem(source));
    std::uint64_t total_bytes = 0;
    std::uint32_t total_files = 0;
    if (!ExtractOne(source, root, options, &result, &total_bytes, &total_files, progress)) {
        std::error_code ignored;
        fs::remove_all(root, ignored);
        return result;
    }
    if (progress) progress(1, 1);
    result.extracted_archives.push_back(source.u8string());

    for (std::uint32_t depth = 0;; ++depth) {
        std::vector<fs::path> nested;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
             it != end && !ec; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            if (IsLz4Frame(it->path())) {
                nested.push_back(it->path());
                continue;
            }
            OpenArchive probe;
            std::string probe_error;
            if (OpenRecognized(it->path(), options.password, &probe, &probe_error)) {
                probe.archive->Close();
                nested.push_back(it->path());
            } else if (!StrongFormat(it->path()).empty()) {
                nested.push_back(it->path());
            }
        }
        if (nested.empty()) break;
        if (depth >= options.max_depth) {
            result.error = "Nested archive depth exceeds the configured limit";
            std::error_code ignored;
            fs::remove_all(root, ignored);
            return result;
        }
        bool extracted_any = false;
        for (const fs::path &nested_source : nested) {
            const fs::path nested_destination = UniqueDestination(
                nested_source.parent_path(), ArchiveStem(nested_source));
            if (!ExtractOne(nested_source, nested_destination, options, &result,
                            &total_bytes, &total_files, progress)) {
                std::error_code ignored;
                fs::remove_all(root, ignored);
                return result;
            }
            result.extracted_archives.push_back(nested_source.u8string());
            fs::remove(nested_source, ec);
            extracted_any = true;
        }
        if (!extracted_any) break;
    }
    result.ok = true;
    result.output_path = root.u8string();
    return result;
}

}  // namespace aether::archive_import
