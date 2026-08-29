#include "portableRegistry.h"

#include "CharacterSet.h"
#include "Platform.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace AetherKiri {
namespace {

constexpr std::array<char, 8> kMagic = {'A', 'K', 'R', 'R', 'E', 'G', '0', '1'};
constexpr std::uint32_t kVersion = 1;
constexpr std::uint32_t kMaxEntries = 16384;
constexpr std::uint32_t kMaxKeyBytes = 16 * 1024;
constexpr std::uint32_t kMaxValueBytes = 64 * 1024 * 1024;

enum class ValueKind : std::uint8_t {
    String = 1,
    Integer = 2,
    Real = 3,
    Octet = 4,
};

struct EncodedValue {
    ValueKind kind;
    std::vector<std::uint8_t> payload;
};

struct StoreState {
    std::mutex mutex;
    std::map<std::string, tTJSVariant> values;
    bool loaded = false;
    bool dirty = false;
};

StoreState &state() {
    static StoreState value;
    return value;
}

std::string toUtf8(const ttstr &value) {
    const tjs_int length = TVPWideCharToUtf8String(value.c_str(), nullptr);
    if(length <= 0)
        return {};
    std::string result(static_cast<std::size_t>(length), '\0');
    if(TVPWideCharToUtf8String(value.c_str(), result.data()) < 0)
        return {};
    return result;
}

ttstr fromUtf8(const std::uint8_t *data, std::size_t length) {
    if(!data || length == 0)
        return ttstr();
    const tjs_int wideLength = TVPUtf8ToWideCharString(
        reinterpret_cast<const char *>(data),
        static_cast<tjs_uint>(std::min<std::size_t>(
            length, std::numeric_limits<tjs_uint>::max())), nullptr);
    if(wideLength <= 0)
        return ttstr();
    std::vector<tjs_char> wide(static_cast<std::size_t>(wideLength) + 1, 0);
    TVPUtf8ToWideCharString(
        reinterpret_cast<const char *>(data),
        static_cast<tjs_uint>(std::min<std::size_t>(
            length, std::numeric_limits<tjs_uint>::max())), wide.data());
    return ttstr(wide.data(), wideLength);
}

void appendU32(std::vector<std::uint8_t> &out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffu));
}

void appendU64(std::vector<std::uint8_t> &out, std::uint64_t value) {
    for(unsigned int shift = 0; shift < 64; shift += 8)
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
}

bool readU32(const std::vector<std::uint8_t> &data, std::size_t &offset,
            std::uint32_t &value) {
    if(offset > data.size() || data.size() - offset < 4)
        return false;
    value = static_cast<std::uint32_t>(data[offset]) |
        (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
        (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
        (static_cast<std::uint32_t>(data[offset + 3]) << 24);
    offset += 4;
    return true;
}

bool readU64(const std::vector<std::uint8_t> &data, std::size_t &offset,
            std::uint64_t &value) {
    if(offset > data.size() || data.size() - offset < 8)
        return false;
    value = 0;
    for(unsigned int shift = 0; shift < 64; shift += 8)
        value |= static_cast<std::uint64_t>(data[offset++]) << shift;
    return true;
}

bool encodeValue(const tTJSVariant &value, EncodedValue &encoded) {
    encoded.payload.clear();
    switch(value.Type()) {
    case tvtString: {
        const std::string text = toUtf8(value.AsStringNoAddRef());
        if(text.size() > kMaxValueBytes)
            return false;
        encoded.kind = ValueKind::String;
        encoded.payload.assign(text.begin(), text.end());
        return true;
    }
    case tvtInteger: {
        encoded.kind = ValueKind::Integer;
        appendU64(encoded.payload,
                  static_cast<std::uint64_t>(static_cast<tjs_int64>(value)));
        return true;
    }
    case tvtReal: {
        const double real = static_cast<double>(value.AsReal());
        if(!std::isfinite(real))
            return false;
        encoded.kind = ValueKind::Real;
        const auto *bytes = reinterpret_cast<const std::uint8_t *>(&real);
        encoded.payload.assign(bytes, bytes + sizeof(real));
        return true;
    }
    case tvtOctet: {
        const tTJSVariantOctet *octet = value.AsOctetNoAddRef();
        if(!octet || octet->GetLength() > kMaxValueBytes)
            return false;
        encoded.kind = ValueKind::Octet;
        if(octet->GetLength() > 0 && octet->GetData())
            encoded.payload.assign(octet->GetData(),
                                   octet->GetData() + octet->GetLength());
        return true;
    }
    default:
        return false;
    }
}

bool decodeValue(ValueKind kind, const std::vector<std::uint8_t> &payload,
                 tTJSVariant &value) {
    switch(kind) {
    case ValueKind::String:
        value = fromUtf8(payload.data(), payload.size());
        return true;
    case ValueKind::Integer: {
        if(payload.size() != 8)
            return false;
        std::size_t offset = 0;
        std::uint64_t raw = 0;
        if(!readU64(payload, offset, raw))
            return false;
        value = static_cast<tjs_int64>(raw);
        return true;
    }
    case ValueKind::Real: {
        if(payload.size() != sizeof(double))
            return false;
        double real = 0;
        std::memcpy(&real, payload.data(), sizeof(real));
        if(!std::isfinite(real))
            return false;
        value = real;
        return true;
    }
    case ValueKind::Octet:
        value = tTJSVariant(payload.data(),
                            static_cast<tjs_uint>(payload.size()));
        return true;
    default:
        return false;
    }
}

std::filesystem::path registryPath() {
    const std::string &base = TVPGetInternalPreferencePath();
    return std::filesystem::path(base) / "AetherRegistry.akrreg";
}

bool readFile(std::vector<std::uint8_t> &bytes) {
    bytes.clear();
    std::ifstream input(registryPath(), std::ios::binary | std::ios::ate);
    if(!input)
        return false;
    const std::streampos end = input.tellg();
    if(end < 0 || static_cast<std::uint64_t>(end) >
                       static_cast<std::uint64_t>(kMaxEntries) *
                           (kMaxKeyBytes + kMaxValueBytes + 32))
        return false;
    const std::size_t size = static_cast<std::size_t>(end);
    input.seekg(0, std::ios::beg);
    bytes.resize(size);
    if(size > 0)
        input.read(reinterpret_cast<char *>(bytes.data()),
                   static_cast<std::streamsize>(size));
    return input.good() || input.eof();
}

bool decodeFile(const std::vector<std::uint8_t> &bytes,
                std::map<std::string, tTJSVariant> &values) {
    values.clear();
    if(bytes.size() < kMagic.size() + 8)
        return false;
    if(!std::equal(kMagic.begin(), kMagic.end(), bytes.begin()))
        return false;
    std::size_t offset = kMagic.size();
    std::uint32_t version = 0;
    std::uint32_t count = 0;
    if(!readU32(bytes, offset, version) || !readU32(bytes, offset, count) ||
       version != kVersion || count > kMaxEntries)
        return false;

    for(std::uint32_t index = 0; index < count; ++index) {
        std::uint32_t keyLength = 0;
        std::uint32_t valueLength = 0;
        if(!readU32(bytes, offset, keyLength) || offset >= bytes.size())
            return false;
        const auto kind = static_cast<ValueKind>(bytes[offset++]);
        if(!readU32(bytes, offset, valueLength) ||
           keyLength == 0 || keyLength > kMaxKeyBytes ||
           valueLength > kMaxValueBytes || offset > bytes.size() ||
           bytes.size() - offset < static_cast<std::size_t>(keyLength) +
                                      static_cast<std::size_t>(valueLength))
            return false;
        std::string key(reinterpret_cast<const char *>(bytes.data() + offset),
                        keyLength);
        offset += keyLength;
        std::vector<std::uint8_t> payload(
            bytes.begin() + static_cast<std::ptrdiff_t>(offset),
            bytes.begin() + static_cast<std::ptrdiff_t>(offset + valueLength));
        offset += valueLength;
        tTJSVariant value;
        if(!decodeValue(kind, payload, value) || !values.emplace(std::move(key),
                                                                 std::move(value))
                                                    .second)
            return false;
    }
    return offset == bytes.size();
}

std::vector<std::uint8_t> encodeFile(
    const std::map<std::string, tTJSVariant> &values) {
    std::vector<std::uint8_t> bytes;
    bytes.insert(bytes.end(), kMagic.begin(), kMagic.end());
    appendU32(bytes, kVersion);
    appendU32(bytes, static_cast<std::uint32_t>(values.size()));
    for(const auto &pair : values) {
        EncodedValue encoded{};
        if(pair.first.empty() || pair.first.size() > kMaxKeyBytes ||
           !encodeValue(pair.second, encoded) ||
           encoded.payload.size() > kMaxValueBytes)
            return {};
        appendU32(bytes, static_cast<std::uint32_t>(pair.first.size()));
        bytes.push_back(static_cast<std::uint8_t>(encoded.kind));
        appendU32(bytes, static_cast<std::uint32_t>(encoded.payload.size()));
        bytes.insert(bytes.end(), pair.first.begin(), pair.first.end());
        bytes.insert(bytes.end(), encoded.payload.begin(), encoded.payload.end());
    }
    return bytes;
}

void ensureLoadedLocked(StoreState &store) {
    if(store.loaded)
        return;
    store.loaded = true;
    std::vector<std::uint8_t> bytes;
    if(!readFile(bytes))
        return;
    std::map<std::string, tTJSVariant> decoded;
    if(decodeFile(bytes, decoded))
        store.values = std::move(decoded);
}

bool writeFileAtomically(const std::vector<std::uint8_t> &bytes) {
    try {
        const std::filesystem::path path = registryPath();
        std::filesystem::create_directories(path.parent_path());
        const std::filesystem::path temp = path.string() + ".tmp";
        {
            std::ofstream output(temp, std::ios::binary | std::ios::trunc);
            if(!output)
                return false;
            if(!bytes.empty())
                output.write(reinterpret_cast<const char *>(bytes.data()),
                             static_cast<std::streamsize>(bytes.size()));
            output.flush();
            if(!output)
                return false;
        }
        std::error_code error;
        std::filesystem::rename(temp, path, error);
        if(error) {
            // Windows does not replace an existing file with rename().
            std::filesystem::remove(path, error);
            error.clear();
            std::filesystem::rename(temp, path, error);
        }
        if(error)
            std::filesystem::remove(temp, error);
        return !error;
    } catch(...) {
        return false;
    }
}

bool isSubkey(const std::string &key, const std::string &prefix) {
    if(key == prefix)
        return true;
    if(key.size() <= prefix.size() || key.compare(0, prefix.size(), prefix) != 0)
        return false;
    const char separator = key[prefix.size()];
    return separator == '\\' || separator == '/';
}

} // namespace

PortableRegistryStore &PortableRegistryStore::Instance() {
    static PortableRegistryStore store;
    return store;
}

bool PortableRegistryStore::Write(const ttstr &key, const tTJSVariant &value) {
    const std::string normalized = toUtf8(key);
    EncodedValue encoded{};
    if(normalized.empty() || normalized.size() > kMaxKeyBytes ||
       !encodeValue(value, encoded))
        return false;
    StoreState &store = state();
    {
        std::lock_guard<std::mutex> lock(store.mutex);
        ensureLoadedLocked(store);
        store.values[normalized] = value;
        store.dirty = true;
    }
    return Flush();
}

bool PortableRegistryStore::Read(const ttstr &key, tTJSVariant &value) {
    const std::string normalized = toUtf8(key);
    if(normalized.empty())
        return false;
    StoreState &store = state();
    std::lock_guard<std::mutex> lock(store.mutex);
    ensureLoadedLocked(store);
    const auto found = store.values.find(normalized);
    if(found == store.values.end())
        return false;
    value = found->second;
    return true;
}

bool PortableRegistryStore::DeleteValue(const ttstr &key) {
    const std::string normalized = toUtf8(key);
    if(normalized.empty())
        return false;
    StoreState &store = state();
    {
        std::lock_guard<std::mutex> lock(store.mutex);
        ensureLoadedLocked(store);
        const auto found = store.values.find(normalized);
        if(found == store.values.end())
            return false;
        store.values.erase(found);
        store.dirty = true;
    }
    return Flush();
}

bool PortableRegistryStore::DeleteKey(const ttstr &key) {
    const std::string normalized = toUtf8(key);
    if(normalized.empty())
        return false;
    StoreState &store = state();
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(store.mutex);
        ensureLoadedLocked(store);
        for(auto it = store.values.begin(); it != store.values.end();) {
            if(isSubkey(it->first, normalized)) {
                it = store.values.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
        if(changed)
            store.dirty = true;
    }
    return !changed || Flush();
}

bool PortableRegistryStore::Flush() {
    StoreState &store = state();
    std::map<std::string, tTJSVariant> snapshot;
    {
        std::lock_guard<std::mutex> lock(store.mutex);
        ensureLoadedLocked(store);
        if(!store.dirty)
            return true;
        snapshot = store.values;
    }
    const std::vector<std::uint8_t> bytes = encodeFile(snapshot);
    if(bytes.empty())
        return false;
    if(!writeFileAtomically(bytes))
        return false;
    std::lock_guard<std::mutex> lock(store.mutex);
    store.dirty = false;
    return true;
}

} // namespace AetherKiri
