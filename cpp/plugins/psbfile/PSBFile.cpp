#include "PSBFile.h"
#include "PSBFileExtension.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#if defined(__APPLE__) || (defined(__linux__) && !defined(__ANDROID__))
#define AETHERKIRI_HAS_EXECINFO 1
#include <execinfo.h>
#endif
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
#include <zlib.h>

#include "EMoteCTX.h"
#include "ncbind.hpp"

#include "TVPMmapAlloc.h"
#ifdef TVP_USE_MMAP_TEMP
static constexpr size_t kPSBMmapThreshold = 256 * 1024;
#endif

#define LOGGER spdlog::get("plugin")

#if defined(AETHERKIRI_INTERNAL_PSBFILE)
extern "C" void AetherInternalRegisterPSBFileRuntime();
#endif

namespace PSB {
    namespace {
        bool IsPSBLoadDebugEnabled() {
            static const bool enabled = [] {
                const char *value = std::getenv("AETHERKIRI_PSB_DEBUG");
                return value && *value && std::strcmp(value, "0") != 0;
            }();
            return enabled;
        }

        std::uint32_t ReadU32LE(const std::uint8_t *data) {
            return static_cast<std::uint32_t>(data[0]) |
                (static_cast<std::uint32_t>(data[1]) << 8) |
                (static_cast<std::uint32_t>(data[2]) << 16) |
                (static_cast<std::uint32_t>(data[3]) << 24);
        }

        void WriteU16LE(std::uint8_t *data, const std::uint16_t value) {
            data[0] = static_cast<std::uint8_t>(value);
            data[1] = static_cast<std::uint8_t>(value >> 8);
        }

        void WriteU32LE(std::uint8_t *data, const std::uint32_t value) {
            data[0] = static_cast<std::uint8_t>(value);
            data[1] = static_cast<std::uint8_t>(value >> 8);
            data[2] = static_cast<std::uint8_t>(value >> 16);
            data[3] = static_cast<std::uint8_t>(value >> 24);
        }

        // Scenario PSBs are compiled data rather than text.  Keep an
        // opt-in, bounded tree dump beside the parser so compatibility work
        // can inspect authored layer commands without changing normal game
        // behaviour.  This is intentionally disabled unless the caller sets
        // AETHERKIRI_PSB_DUMP_PATH.
        void DumpPSBValue(const std::shared_ptr<IPSBValue> &value,
                          std::ostream &out, const std::string &path,
                          std::size_t &nodes, const std::size_t maxNodes,
                          const int depth = 0) {
            if(!value || nodes++ >= maxNodes) {
                return;
            }
            const auto indent = std::string(static_cast<std::size_t>(depth) * 2,
                                            ' ');
            if(auto dict = std::dynamic_pointer_cast<PSBDictionary>(value)) {
                out << indent << path << " {\n";
                for(const auto &[key, child] : *dict) {
                    DumpPSBValue(child, out, path + "." + key, nodes,
                                 maxNodes, depth + 1);
                    if(nodes >= maxNodes)
                        break;
                }
                out << indent << "}\n";
                return;
            }
            if(auto list = std::dynamic_pointer_cast<PSBList>(value)) {
                out << indent << path << " [\n";
                std::size_t index = 0;
                for(const auto &child : *list) {
                    DumpPSBValue(child, out,
                                 path + "[" + std::to_string(index++) + "]",
                                 nodes, maxNodes, depth + 1);
                    if(nodes >= maxNodes)
                        break;
                }
                out << indent << "]\n";
                return;
            }
            std::string repr;
            try {
                repr = value->toString();
            } catch(...) {
                repr = "<toString threw>";
            }
            constexpr std::size_t maxString = 320;
            if(repr.size() > maxString)
                repr.resize(maxString), repr += "...";
            out << indent << path << " = " << repr << "\n";
        }

        void MaybeDumpPSBTree(const ttstr &sourceName,
                              const std::shared_ptr<IPSBValue> &root) {
            const char *dumpPath = std::getenv("AETHERKIRI_PSB_DUMP_PATH");
            if(!dumpPath || !*dumpPath || !root)
                return;
            const char *match = std::getenv("AETHERKIRI_PSB_DUMP_MATCH");
            const std::string source = sourceName.AsStdString();
            if(match && *match && source.find(match) == std::string::npos)
                return;
            std::size_t maxNodes = 200000;
            if(const char *limit = std::getenv("AETHERKIRI_PSB_DUMP_MAX_NODES")) {
                try {
                    maxNodes = std::max<std::size_t>(1, std::stoull(limit));
                } catch(...) {
                }
            }
            std::ofstream out(dumpPath, std::ios::app);
            if(!out)
                return;
            out << "\n=== PSB " << source << " type="
                << static_cast<int>(root->getType()) << " ===\n";
            std::size_t nodes = 0;
            DumpPSBValue(root, out, "$", nodes, maxNodes);
            out << "=== END PSB nodes=" << nodes << " ===\n";
        }

        void NormalizeObjectHeader(std::uint8_t *data, const size_t size,
                                   const PSBHeader &header) {
            if(!data || size < header.GetHeaderLength()) {
                return;
            }
            std::memcpy(data, header.signature, sizeof(header.signature));
            WriteU16LE(data + 4, header.version);
            // The parser already applied the title's E-mote transform and
            // seed cipher. Mark the retained object as plain so a native backend
            // does not try to interpret the normalized bytes as encrypted.
            WriteU16LE(data + 6, 0);
            WriteU32LE(data + 8, header.offsetEncrypt);
            WriteU32LE(data + 12, header.offsetNames);
            WriteU32LE(data + 16, header.offsetStrings);
            WriteU32LE(data + 20, header.offsetStringsData);
            WriteU32LE(data + 24, header.offsetChunkOffsets);
            WriteU32LE(data + 28, header.offsetChunkLengths);
            WriteU32LE(data + 32, header.offsetChunkData);
            WriteU32LE(data + 36, header.offsetEntries);
            if(header.version > 2) {
                WriteU32LE(data + 40, header.checksum);
            }
            if(header.version > 3) {
                WriteU32LE(data + 44, header.offsetExtraChunkOffsets);
                WriteU32LE(data + 48, header.offsetExtraChunkLengths);
                WriteU32LE(data + 52, header.offsetExtraChunkData);
            }
        }

        void LogPSBStage(const ttstr &filePath, const char *stage) {
            if(IsPSBLoadDebugEnabled()) {
                LOGGER->info("PSBFile stage: {} ({})", stage,
                             filePath.AsStdString());
            }
        }

        bool ContainsUtf16LeAscii(const std::uint8_t *data,
                                  const size_t size,
                                  const char *needle) {
            if(!data || !needle)
                return false;
            const size_t needleLength = std::strlen(needle);
            if(needleLength == 0 || size < needleLength * 2)
                return false;
            for(size_t offset = 0; offset + needleLength * 2 <= size;
                offset += 2) {
                bool matched = true;
                for(size_t index = 0; index < needleLength; ++index) {
                    if(data[offset + index * 2] !=
                           static_cast<std::uint8_t>(needle[index]) ||
                       data[offset + index * 2 + 1] != 0) {
                        matched = false;
                        break;
                    }
                }
                if(matched)
                    return true;
            }
            return false;
        }

        bool IsExternalPimgIndex(const std::uint8_t *data, const size_t size,
                                 const ttstr &sourceName) {
            return size >= 4 && data[0] == 0xff && data[1] == 0xfe &&
                TVPExtractStorageExt(sourceName).AsLowerCase() ==
                    TJS_W(".pimg") &&
                ContainsUtf16LeAscii(data + 2, size - 2,
                                     "_extra_binary_file_");
        }

        bool IsStreamRangeValid(const std::uint64_t base,
                                const std::uint64_t relative,
                                const std::uint64_t size) {
            return base <= size && relative <= size - base;
        }
    } // namespace

    void PSBFile::resetState() {
        charset = PSBArray();
        namesData = PSBArray();
        nameIndexes = PSBArray();
        names.clear();

        stringOffsets = PSBArray();
        strings.clear();

        chunkOffsets = PSBArray();
        chunkLengths = PSBArray();
        resources.clear();

        extraChunkOffsets = PSBArray();
        extraChunkLengths = PSBArray();
        extraResources.clear();

        _root.reset();
        _compatRoot.Clear();
        _objectImage.reset();
        _header = PSBHeader{};
        _type = PSBType::PSB;
    }

    void PSBFile::loadKeys(TJS::tTJSBinaryStream *stream) {
        if(stream == nullptr || _header.offsetNames > stream->GetSize()) {
            throw std::runtime_error("PSB name table is outside the stream");
        }
        const size_t len = nameIndexes.value.size();
        names.clear();
        names.reserve(len);
        const auto streamSize = stream->GetSize();
        for(size_t i = 0; i < len; i++) {
            const auto relative = static_cast<std::uint64_t>(nameIndexes[i]);
            if(!IsStreamRangeValid(_header.offsetNames, relative,
                                   streamSize)) {
                throw std::runtime_error("PSB name index is outside the stream");
            }
            stream->SetPosition(static_cast<std::uint64_t>(_header.offsetNames) +
                                relative);
            names.push_back(PSB::Extension::readStringZeroTrim(stream));
        }
    }

    void PSBFile::loadNames() {
        const size_t len = nameIndexes.value.size();
        names.clear();
        names.reserve(len);
        for(size_t i = 0; i < len; i++) {
            std::string codepoints;
            const auto index = nameIndexes[i];
            if(index >= namesData.value.size()) {
                throw std::runtime_error("PSB name index is outside namesData");
            }
            auto chr = namesData[static_cast<int>(index)];
            size_t steps = 0;
            while(chr != u'\0') {
                if(++steps > namesData.value.size()) {
                    throw std::runtime_error("PSB name chain is cyclic");
                }
                if(chr >= namesData.value.size()) {
                    throw std::runtime_error("PSB name chain index is invalid");
                }
                const auto code = namesData[static_cast<int>(chr)];
                if(code >= charset.value.size()) {
                    throw std::runtime_error("PSB name charset index is invalid");
                }
                const auto d = charset[static_cast<int>(code)];
                if(d > chr || chr - d > 0xff) {
                    throw std::runtime_error("PSB name character value is invalid");
                }
                codepoints.push_back(static_cast<char>(chr - d));

                chr = code;
            }

            std::reverse(codepoints.begin(), codepoints.end()); // little endian
            names.push_back(std::move(codepoints));
        }
    }

    void PSBFile::loadString(std::unique_ptr<PSB::PSBString> &str,
                             TJS::tTJSBinaryStream *stream) {
        if(!str || !str->index.has_value() || stream == nullptr) {
            throw std::runtime_error("PSB string index is invalid");
        }
        auto idx = str->index;
        if(idx.value() >= stringOffsets.value.size()) {
            throw std::runtime_error("PSB string index is outside offsets");
        }
        if(_header.offsetStringsData > stream->GetSize()) {
            throw std::runtime_error("PSB string data table is outside stream");
        }
        const auto relative = static_cast<std::uint64_t>(stringOffsets[
            static_cast<int>(idx.value())]);
        if(!IsStreamRangeValid(_header.offsetStringsData, relative,
                               stream->GetSize())) {
            throw std::runtime_error("PSB string offset is outside stream");
        }
        const auto refStr = std::find_if(
            strings.begin(), strings.end(),
            [idx](const PSB::PSBString &s) { return s.index == idx; });

        stream->SetPosition(static_cast<std::uint64_t>(_header.offsetStringsData) +
                            relative);
        auto strValue = PSB::Extension::readStringZeroTrim(stream);

        // Strict value equal check
        if(refStr != strings.end() && strValue == refStr->value) {
            str = std::make_unique<PSBString>(*refStr);
            return;
        }

        if(refStr != strings.end()) {
            LOGGER->info("{} does not match {}", refStr->value, strValue);
        }

        str->value = strValue;
        strings.emplace_back(*str);
    }

    std::shared_ptr<PSB::PSBList>
    PSBFile::loadList(TJS::tTJSBinaryStream *stream, bool lazyLoad) {
        auto offsets = PSB::PSBList::loadIntoList(
            stream->ReadI8LE() -
                static_cast<std::uint8_t>(PSB::PSBObjType::ArrayN1) + 1,
            stream);
        const size_t pos = stream->GetPosition();
        auto list = std::make_shared<PSB::PSBList>(offsets.size());
        std::optional<std::uint32_t> maxOffset{};
        size_t endPos = pos;
        if(lazyLoad && !offsets.empty()) {
            maxOffset = *std::max_element(offsets.cbegin(), offsets.cend());
        }
        for(const auto offset : offsets) {
            if(!IsStreamRangeValid(pos, offset, stream->GetSize())) {
                throw std::runtime_error("PSB list offset is outside stream");
            }
            stream->SetPosition(pos + offset);
            auto obj = unpack(stream);
            if(obj != nullptr) {
                if(auto *child = dynamic_cast<PSB::IPSBChild *>(obj.get()))
                    child->parent = list;
                if(auto *singleton = dynamic_cast<PSB::IPSBSingleton *>(obj.get()))
                    singleton->parents.push_back(list);

                list->push_back(obj);
            }

            if(lazyLoad && offset == maxOffset) {
                endPos = stream->GetPosition();
            }
        }

        if(lazyLoad) {
            stream->SetPosition(endPos);
        }
        return std::move(list);
    }

    std::shared_ptr<PSB::PSBDictionary>
    PSBFile::loadObjects(TJS::tTJSBinaryStream *stream, bool lazyLoad) {
        const auto names = PSB::PSBList::loadIntoList(
            stream->ReadI8LE() -
                static_cast<std::uint8_t>(PSB::PSBObjType::ArrayN1) + 1,
            stream);
        const auto offsets = PSB::PSBList::loadIntoList(
            stream->ReadI8LE() -
                static_cast<std::uint8_t>(PSB::PSBObjType::ArrayN1) + 1,
            stream);
        auto pos = stream->GetPosition();
        auto dictionary = std::make_shared<PSB::PSBDictionary>(names.size());
        std::optional<std::uint32_t> maxOffset{};
        auto endPos = pos;
        if(lazyLoad && !offsets.empty()) {
            maxOffset = *std::max_element(offsets.cbegin(), offsets.cend());
        }

        for(size_t i = 0; i < names.size(); i++) {
            const auto nameIdx = names[i];
            if(nameIdx >= PSBFile::names.size()) {
                LOGGER->warn("Bad PSB format: at position:{}, name index {} >= "
                             "Names count ({}), skipping.",
                             pos, nameIdx, PSBFile::names.size());
                continue;
            }
            auto name = PSBFile::names[nameIdx];
            std::shared_ptr<PSB::IPSBValue> obj = nullptr;
            std::uint32_t offset = 0;
            if(i < offsets.size()) {
                offset = offsets[i];
                if(!IsStreamRangeValid(pos, offset, stream->GetSize())) {
                    throw std::runtime_error("PSB object offset is outside stream");
                }
                stream->SetPosition(pos + offset);
                obj = unpack(stream, lazyLoad);
            } else {
                LOGGER->warn("Bad PSB format: at position:{}, offset index {} "
                             ">= offsets count ({}), skipping.",
                             pos, i, offsets.size());
            }

            if(obj != nullptr) {
                if(auto *c = dynamic_cast<IPSBChild *>(obj.get())) {
                    c->parent = dictionary;
                }

                if(auto *s = dynamic_cast<IPSBSingleton *>(obj.get())) {
                    s->parents.push_back(dictionary);
                }

                dictionary->emplace(name, obj);
            }

            if(lazyLoad && offset == maxOffset) {
                endPos = stream->GetPosition();
            }
        }

        if(lazyLoad) {
            stream->SetPosition(endPos);
        }
        return std::move(dictionary);
    }

    std::shared_ptr<PSBDictionary>
    PSBFile::loadObjectsV1(TJS::tTJSBinaryStream *stream, bool lazyLoad) {
        auto offsets = PSB::PSBList::loadIntoList(
            stream->ReadI8LE() -
                static_cast<std::uint8_t>(PSBObjType::ArrayN1) + 1,
            stream);
        std::optional<std::uint32_t> maxOffset{};
        if(lazyLoad && !offsets.empty()) {
            maxOffset = *std::max_element(offsets.cbegin(), offsets.cend());
        }
        const auto pos = stream->GetPosition();
        auto endPos = pos;
        auto dictionary = std::make_shared<PSBDictionary>(offsets.size());
        for(const auto offset : offsets) {
            if(!IsStreamRangeValid(pos, offset, stream->GetSize())) {
                throw std::runtime_error("PSB v1 object offset is outside stream");
            }
            stream->SetPosition(pos + offset);
            PSBNumber nameIdx(static_cast<PSBObjType>(stream->ReadI8LE()),
                              stream);
            const auto nameValue = nameIdx.getLongValue();
            if(nameValue < 0 ||
               static_cast<std::uint64_t>(nameValue) >= PSBFile::names.size()) {
                throw std::runtime_error("PSB v1 name index is invalid");
            }
            auto name = PSBFile::names[static_cast<size_t>(nameValue)];
            auto obj = unpack(stream, lazyLoad);
            if(obj != nullptr) {

                if(auto *c = dynamic_cast<IPSBChild *>(obj.get())) {
                    c->parent = dictionary;
                }

                if(auto *s = dynamic_cast<IPSBSingleton *>(obj.get())) {
                    s->parents.push_back(dictionary);
                }

                dictionary->emplace(name, obj);
            }

            if(lazyLoad && offset == maxOffset) {
                endPos = stream->GetPosition();
            }
        }

        if(lazyLoad) {
            stream->SetPosition(endPos);
        }

        return std::move(dictionary);
    }

    std::shared_ptr<PSB::IPSBValue>
    PSBFile::unpack(TJS::tTJSBinaryStream *stream, bool lazyLoad) {

        auto typeByte = stream->ReadI8LE();

        switch(auto type = static_cast<PSB::PSBObjType>(typeByte)) {
            case PSB::PSBObjType::None:
                return nullptr;
            case PSB::PSBObjType::Null:
                return std::make_shared<PSB::PSBNull>();
            case PSB::PSBObjType::False:
            case PSB::PSBObjType::True:
                return std::make_shared<PSB::PSBBool>(type ==
                                                      PSB::PSBObjType::True);
            case PSB::PSBObjType::NumberN0:
            case PSB::PSBObjType::NumberN1:
            case PSB::PSBObjType::NumberN2:
            case PSB::PSBObjType::NumberN3:
            case PSB::PSBObjType::NumberN4:
            case PSB::PSBObjType::NumberN5:
            case PSB::PSBObjType::NumberN6:
            case PSB::PSBObjType::NumberN7:
            case PSB::PSBObjType::NumberN8:
            case PSB::PSBObjType::Float0:
            case PSB::PSBObjType::Float:
            case PSB::PSBObjType::Double:
                return std::make_shared<PSB::PSBNumber>(type, stream);
            case PSB::PSBObjType::ArrayN1:
            case PSB::PSBObjType::ArrayN2:
            case PSB::PSBObjType::ArrayN3:
            case PSB::PSBObjType::ArrayN4:
            case PSB::PSBObjType::ArrayN5:
            case PSB::PSBObjType::ArrayN6:
            case PSB::PSBObjType::ArrayN7:
            case PSB::PSBObjType::ArrayN8:
                return std::make_shared<PSB::PSBArray>(
                    typeByte -
                        static_cast<std::int8_t>(PSB::PSBObjType::ArrayN1) + 1,
                    stream);
            case PSB::PSBObjType::StringN1:
            case PSB::PSBObjType::StringN2:
            case PSB::PSBObjType::StringN3:
            case PSB::PSBObjType::StringN4: {
                auto str = std::make_unique<PSB::PSBString>(
                    typeByte -
                        static_cast<std::int8_t>(PSB::PSBObjType::StringN1) + 1,
                    stream);
                if(lazyLoad) {
                    const auto foundStr = std::find_if(
                        strings.begin(), strings.end(),
                        [&str](const PSB::PSBString &s) {
                            return s.index.has_value() && s.index == str->index;
                        });
                    if(foundStr == strings.end()) {
                        strings.emplace_back(*str);
                    } else {
                        str = std::make_unique<PSB::PSBString>(*foundStr);
                    }
                } else {
                    loadString(str, stream);
                }

                return std::move(str);
            }
            case PSB::PSBObjType::ResourceN1:
            case PSB::PSBObjType::ResourceN2:
            case PSB::PSBObjType::ResourceN3:
            case PSB::PSBObjType::ResourceN4:
            case PSB::PSBObjType::ExtraChunkN1:
            case PSB::PSBObjType::ExtraChunkN2:
            case PSB::PSBObjType::ExtraChunkN3:
            case PSB::PSBObjType::ExtraChunkN4: {
                const bool isExtra = type >= PSB::PSBObjType::ExtraChunkN1;
                auto &resList = isExtra ? extraResources : resources;
                auto res = std::make_shared<PSBResource>(
                    typeByte -
                        static_cast<std::uint8_t>(
                            isExtra ? PSB::PSBObjType::ExtraChunkN1
                                    : PSB::PSBObjType::ResourceN1) +
                        1,
                    stream);
                res->isExtra = isExtra;
                const auto foundRes =
                    std::find_if(resList.begin(), resList.end(),
                                 [&res](const std::shared_ptr<PSBResource> &r) {
                                     return r->index == res->index;
                                 });

                if(foundRes == resList.end()) {
                    resList.push_back(res);
                } else {
                    res = *foundRes;
                }

                return res;
            }
            case PSB::PSBObjType::List:
                return loadList(stream, lazyLoad);
            case PSB::PSBObjType::Objects:
                return _header.version != 1 ? loadObjects(stream, lazyLoad)
                                            : loadObjectsV1(stream, lazyLoad);
            default:
                LOGGER->error("unknown psbObjType: 0x{:02X} at stream pos {}",
                              static_cast<unsigned>(typeByte),
                              stream->GetPosition());
                return nullptr;
        }
    }

    bool PSBFile::loadPSBData(const void *data, size_t readSize,
                              const ttstr &sourceName, bool loadResources) try {
        const bool traceLoad = IsPSBLoadDebugEnabled();
        if(traceLoad) {
            LOGGER->info("PSBFile load begin: path={} seed={}",
                         sourceName.AsStdString(), _seed);
        }
        resetState();
        if(!data || readSize < 9) {
            if(traceLoad) {
                LOGGER->warn("PSBFile too small: path={} size={}",
                             sourceName.AsStdString(), readSize);
            }
            return false;
        }

        const auto *fileData = static_cast<const std::uint8_t *>(data);
        if(traceLoad) {
            LOGGER->info("PSBFile raw: path={} size={} first4=0x{:08x}",
                         sourceName.AsStdString(), readSize,
                         ReadU32LE(fileData));
        }

#if defined(AETHERKIRI_INTERNAL_PSBFILE)
        AetherInternalRegisterPSBFileRuntime();
#endif
        std::vector<std::uint8_t> extensionData;
        const auto *extension = psbFileExtension();
        if(extension != nullptr &&
           extension->isCompressedFrame != nullptr &&
           extension->decompressFrame != nullptr &&
           extension->isCompressedFrame(fileData, readSize)) {
            std::string error;
            if(!extension->decompressFrame(
                   fileData, readSize, extensionData, error)) {
                LOGGER->warn("PSB extension decompression failed: {} ({})",
                             error,
                             sourceName.AsStdString());
                return false;
            }
            LOGGER->debug("PSB extension decompressed: {} -> {} bytes ({})",
                          readSize, extensionData.size(),
                          sourceName.AsStdString());
            fileData = extensionData.data();
            readSize = extensionData.size();
            if(readSize < 9) {
                LOGGER->warn(
                    "PSB extension decompressed to invalid size: {} ({})",
                    readSize, sourceName.AsStdString());
                return false;
            }
        }

        if(IsExternalPimgIndex(fileData, readSize, sourceName)) {
            if(extension == nullptr || extension->loadExternalPimg == nullptr) {
                LOGGER->warn("External-resource PIMG is unsupported: {}",
                             sourceName.AsStdString());
                return false;
            }
            std::string error;
            tTJSVariant root;
            if(!extension->loadExternalPimg(fileData, readSize, sourceName,
                                            root, error) ||
               root.Type() != tvtObject || root.AsObjectNoAddRef() == nullptr) {
                LOGGER->warn("External-resource PIMG load failed: {} ({})",
                             error.empty() ? "invalid root" : error,
                             sourceName.AsStdString());
                return false;
            }
            _compatRoot = root;
            _type = PSBType::Pimg;
            LOGGER->debug("External-resource PIMG loaded: {}",
                          sourceName.AsStdString());
            return true;
        }

        char outerSign[4];
        memcpy(outerSign, fileData, 4);

        const bool isMdf = ((outerSign[0] & ~0x20) == 'M') &&
                           ((outerSign[1] & ~0x20) == 'D') &&
                           ((outerSign[2] & ~0x20) == 'F') &&
                           outerSign[3] == '\0';

        size_t psbSize;
        if(isMdf) {
            uint32_t uncompressedSize;
            memcpy(&uncompressedSize, fileData + 4, 4);
            psbSize = uncompressedSize;
        } else {
            psbSize = readSize;
        }

        if(psbSize > std::numeric_limits<tjs_uint>::max()) {
            LOGGER->warn("PSB stream is too large: {} bytes ({})", psbSize,
                         sourceName.AsStdString());
            return false;
        }
        tTVPMemoryStream stream{ nullptr, static_cast<tjs_uint>(psbSize) };

        if(isMdf) {
            uLongf destLen = static_cast<uLongf>(psbSize);
            int zResult = uncompress(
                static_cast<Bytef *>(stream.GetInternalBuffer()), &destLen,
                fileData + 8, static_cast<uLong>(readSize - 8));

            if(zResult != Z_OK) {
                LOGGER->warn("MDF decompression failed: zlib error {} ({})",
                             zResult, sourceName.AsStdString());
                return false;
            }
            LOGGER->debug("MDF decompressed: {} -> {} bytes ({})",
                          readSize, destLen, sourceName.AsStdString());
        } else {
            memcpy(stream.GetInternalBuffer(), fileData, readSize);
        }

        if(_preParseCallback &&
           !_preParseCallback(
               static_cast<std::uint8_t *>(stream.GetInternalBuffer()),
               psbSize)) {
            LOGGER->warn("PSB pre-parse callback failed: {}",
                         sourceName.AsStdString());
            return false;
        }

        // Some E-mote titles encrypt the PSB signature along with the rest of
        // the payload.  The title-provided pre-parse callback must therefore
        // run before validating the inner PSB/MFL header.  MDF is still
        // identified from its unencrypted outer wrapper and decompressed
        // before the callback, matching the buffer the PSB parser consumes.
        char sign[4];
        memcpy(sign, stream.GetInternalBuffer(), 4);
        if(std::memcmp(sign, PsbSignature, sizeof(sign)) != 0 &&
           std::memcmp(sign, MflSignature, sizeof(sign)) != 0) {
            LOGGER->warn("Not a PSB/MDF/MFL file: {}",
                         sourceName.AsStdString());
            return false;
        }

        stream.SetPosition(0);
        LogPSBStage(sourceName, "parse header");
        _header = PSB::parsePSBHeader(&stream);
        if(traceLoad) {
            LOGGER->info(
                "PSBFile header: path={} size={} psbSize={} seed={} version={} "
                "encrypt={} encrypted={} headerLen={} offsets encrypt={} names={} "
                "strings={} stringsData={} chunkOffsets={} chunkLengths={} "
                "chunkData={} entries={} extraOffsets={} extraLengths={} "
                "extraData={}",
                sourceName.AsStdString(), readSize, psbSize, _seed,
                _header.version, _header.encrypt, _header.isEncrypted(),
                _header.GetHeaderLength(), _header.offsetEncrypt,
                _header.offsetNames, _header.offsetStrings,
                _header.offsetStringsData, _header.offsetChunkOffsets,
                _header.offsetChunkLengths, _header.offsetChunkData,
                _header.offsetEntries, _header.offsetExtraChunkOffsets,
                _header.offsetExtraChunkLengths, _header.offsetExtraChunkData);
        }

        if(_seed > 0) {
            // decrypt

            uint32_t key[4];

            key[0] = 0x075BCD15;
            key[1] = 0x159A55E5;
            key[2] = 0x1F123BB5;
            key[3] = _seed;
            EMoteCTX emoteCtx{};
            init_emote_ctx(&emoteCtx, key);

            if(_header.isEncrypted() &&
               _header.GetHeaderLength() > stream.GetSize()) {

                emote_decrypt(
                    &emoteCtx,
                    reinterpret_cast<std::uint8_t *>(&_header.offsetEncrypt),
                    4);
                emote_decrypt(
                    &emoteCtx,
                    reinterpret_cast<std::uint8_t *>(&_header.offsetNames), 4);
                emote_decrypt(
                    &emoteCtx,
                    reinterpret_cast<std::uint8_t *>(&_header.offsetStrings),
                    4);
                emote_decrypt(&emoteCtx,
                              reinterpret_cast<std::uint8_t *>(
                                  &_header.offsetStringsData),
                              4);
                emote_decrypt(&emoteCtx,
                              reinterpret_cast<std::uint8_t *>(
                                  &_header.offsetChunkOffsets),
                              4);
                emote_decrypt(&emoteCtx,
                              reinterpret_cast<std::uint8_t *>(
                                  &_header.offsetChunkLengths),
                              4);
                emote_decrypt(
                    &emoteCtx,
                    reinterpret_cast<std::uint8_t *>(&_header.offsetChunkData),
                    4);
                emote_decrypt(
                    &emoteCtx,
                    reinterpret_cast<std::uint8_t *>(&_header.offsetEntries),
                    4);

                if(_header.version > 2) {
                    emote_decrypt(
                        &emoteCtx,
                        reinterpret_cast<std::uint8_t *>(&_header.checksum), 4);
                }

                if(_header.version > 3) {
                    emote_decrypt(&emoteCtx,
                                  reinterpret_cast<std::uint8_t *>(
                                      &_header.offsetExtraChunkOffsets),
                                  4);
                    emote_decrypt(&emoteCtx,
                                  reinterpret_cast<std::uint8_t *>(
                                      &_header.offsetExtraChunkLengths),
                                  4);
                    emote_decrypt(&emoteCtx,
                                  reinterpret_cast<std::uint8_t *>(
                                      &_header.offsetExtraChunkData),
                                  4);
                }
            }

            if(_header.version == 2) {
                emote_decrypt(
                    &emoteCtx,
                    &static_cast<std::uint8_t *>(
                        stream.GetInternalBuffer())[_header.offsetEncrypt],
                    _header.offsetChunkOffsets - _header.offsetEncrypt);
            }
        }

        if(std::strcmp(_header.signature, PSB::PsbSignature) != 0) {
            LOGGER->warn("Not a valid PSB file ({}): signature='{}'",
                         sourceName.AsStdString(), _header.signature);
            return false;
        }

        if(_header.isEncrypted() &&
           _header.GetHeaderLength() > stream.GetSize() && _seed == 0) {
            LOGGER->critical("psb file is encrypted");
            return false;
        }

        if(_header.version > 4) {
            LOGGER->critical("not support psb file format version > 4");
            return false;
        }

        const auto streamSize = static_cast<std::uint64_t>(stream.GetSize());
        if(_header.GetHeaderLength() > streamSize ||
           !IsStreamRangeValid(0, _header.offsetNames, streamSize) ||
           !IsStreamRangeValid(0, _header.offsetStrings, streamSize) ||
           !IsStreamRangeValid(0, _header.offsetStringsData, streamSize) ||
           !IsStreamRangeValid(0, _header.offsetChunkOffsets, streamSize) ||
           !IsStreamRangeValid(0, _header.offsetChunkLengths, streamSize) ||
           !IsStreamRangeValid(0, _header.offsetChunkData, streamSize) ||
           !IsStreamRangeValid(0, _header.offsetEntries, streamSize) ||
           (_header.version >= 4 &&
            (!IsStreamRangeValid(0, _header.offsetExtraChunkOffsets,
                                 streamSize) ||
             !IsStreamRangeValid(0, _header.offsetExtraChunkLengths,
                                 streamSize) ||
             !IsStreamRangeValid(0, _header.offsetExtraChunkData, streamSize)))) {
            LOGGER->warn("PSB header contains an out-of-range offset ({})",
                         sourceName.AsStdString());
            return false;
        }

        auto objectImage = std::make_shared<std::vector<std::uint8_t>>(
            static_cast<const std::uint8_t *>(stream.GetInternalBuffer()),
            static_cast<const std::uint8_t *>(stream.GetInternalBuffer()) +
                stream.GetSize());
        NormalizeObjectHeader(objectImage->data(), objectImage->size(),
                              _header);
        _objectImage = std::move(objectImage);

        // Pre Load Strings
        LogPSBStage(sourceName, "load string offsets");
        stream.SetPosition(_header.offsetStrings);
        stringOffsets = PSB::PSBArray(
            stream.ReadI8LE() -
                static_cast<std::uint8_t>(PSB::PSBObjType::ArrayN1) + 1,
            &stream);
        if(traceLoad) {
            LOGGER->info("PSBFile strings: path={} offsets={}",
                         sourceName.AsStdString(), stringOffsets.value.size());
        }

        // Load Names
        LogPSBStage(sourceName, "load names");
        if(_header.version == 1) {
            // don't believe HeaderLength
            if(_header.offsetEncrypt >= stream.GetSize()) {
                _header.offsetEncrypt = _header.GetHeaderLength();
            }
            stream.SetPosition(_header.offsetEncrypt);
            nameIndexes = PSB::PSBArray(
                stream.ReadI8LE() -
                    static_cast<std::uint8_t>(PSB::PSBObjType::ArrayN1) + 1,
                &stream);
            loadKeys(&stream);
        } else {
            stream.SetPosition(_header.offsetNames);
            charset = PSB::PSBArray(
                stream.ReadI8LE() -
                    static_cast<std::uint8_t>(PSB::PSBObjType::ArrayN1) + 1,
                &stream);
            namesData = PSB::PSBArray(
                stream.ReadI8LE() -
                    static_cast<std::uint8_t>(PSB::PSBObjType::ArrayN1) + 1,
                &stream);
            nameIndexes = PSB::PSBArray(
                stream.ReadI8LE() -
                    static_cast<std::uint8_t>(PSB::PSBObjType::ArrayN1) + 1,
                &stream);
            loadNames();
        }
        if(traceLoad) {
            LOGGER->info(
                "PSBFile names: path={} charset={} namesData={} indexes={} "
                "names={}",
                sourceName.AsStdString(), charset.value.size(),
                namesData.value.size(), nameIndexes.value.size(),
                names.size());
        }

        // Pre Load Resources (Chunks)
        LogPSBStage(sourceName, "load chunk offsets");
        stream.SetPosition(_header.offsetChunkOffsets);
        chunkOffsets = PSB::PSBArray(
            stream.ReadI8LE() -
                static_cast<std::uint8_t>(PSB::PSBObjType::ArrayN1) + 1,
            &stream);
        stream.SetPosition(_header.offsetChunkLengths);
        chunkLengths = PSB::PSBArray(
            stream.ReadI8LE() -
                static_cast<std::uint8_t>(PSB::PSBObjType::ArrayN1) + 1,
            &stream);
        if(traceLoad) {
            LOGGER->info("PSBFile chunks: path={} offsets={} lengths={}",
                         sourceName.AsStdString(), chunkOffsets.value.size(),
                         chunkLengths.value.size());
        }

        resources.reserve(chunkLengths.value.size());

        if(_header.version >= 4) {
            // Pre Load Extra Resources (Chunks)
            LogPSBStage(sourceName, "load extra chunk offsets");
            stream.SetPosition(_header.offsetExtraChunkOffsets);
            extraChunkOffsets = PSB::PSBArray(
                stream.ReadI8LE() -
                    static_cast<std::uint8_t>(PSB::PSBObjType::ArrayN1) + 1,
                &stream);
            stream.SetPosition(_header.offsetExtraChunkLengths);
            extraChunkLengths = PSB::PSBArray(
                stream.ReadI8LE() -
                    static_cast<std::uint8_t>(PSB::PSBObjType::ArrayN1) + 1,
                &stream);
            extraResources.reserve(extraChunkLengths.value.size());
        }
        // Load Entries
        LogPSBStage(sourceName, "load root entries");
        stream.SetPosition(_header.offsetEntries);
        auto obj = unpack(&stream);
        if(!obj) {
            LOGGER->error("Can not parse objects");
        }

        _root = std::move(obj);
        // Load resource payloads only when the caller will consume them.  The
        // object graph keeps resource indices intact, so metadata-only users
        // can still inspect labels and file names without paying the cost of
        // copying every embedded image/audio chunk.
        if(loadResources) {
            LogPSBStage(sourceName, "load resources");
            for(auto &res : resources) {
                loadResource(*res, &stream);
            }

            if(_header.version >= 4) {
                LogPSBStage(sourceName, "load extra resources");
                for(auto &res : extraResources) {
                    loadExtraResource(*res, &stream);
                }
            }
        }

        afterLoad();
        MaybeDumpPSBTree(sourceName, _root);
        if(traceLoad) {
            LOGGER->info(
                "PSBFile load ok: path={} type={} strings={} resources={} "
                "extraResources={}",
                sourceName.AsStdString(), static_cast<int>(_type),
                strings.size(), resources.size(), extraResources.size());
        }
        return true;
    } catch(const std::exception &error) {
        resetState();
        LOGGER->warn("PSB parse failed: {} ({})", error.what(),
                     sourceName.AsStdString());
        return false;
    } catch(...) {
        resetState();
        LOGGER->warn("PSB parse failed with an unknown error ({})",
                     sourceName.AsStdString());
        return false;
    }

    bool PSBFile::loadPSBFile(const ttstr &filePath) {
        LOGGER->debug("load psb file: {}", filePath.AsStdString());
        const bool traceLoad = IsPSBLoadDebugEnabled();
#if defined(AETHERKIRI_HAS_EXECINFO)
        const std::string tracePath = filePath.AsStdString();
        if(traceLoad && tracePath.size() >= 4 &&
           tracePath.compare(tracePath.size() - 4, 4, ".pbd") == 0) {
            void *frames[20]{};
            const int frameCount = backtrace(frames, 20);
            char **symbols = backtrace_symbols(frames, frameCount);
            for(int index = 0; symbols && index < frameCount; ++index)
                LOGGER->info("PSBFile pbd caller[{}]: {}", index,
                             symbols[index]);
            std::free(symbols);
        }
#endif
        resetState();
        auto *s = TVPCreateStream(filePath);
        if(!s) {
            if(traceLoad) {
                LOGGER->warn("PSBFile open failed: {}", filePath.AsStdString());
            }
            return false;
        }

        const size_t readSize = s->GetSize();
        if(readSize < 9) {
            if(traceLoad) {
                LOGGER->warn("PSBFile too small: path={} size={}",
                             filePath.AsStdString(), readSize);
            }
            delete s;
            return false;
        }

        bool fileDataMmap = false;
        uint8_t *fileData = nullptr;
#if defined(__APPLE__) || defined(__linux__) || defined(__ANDROID__)
        if(readSize >= kPSBMmapThreshold) {
            fileData = (uint8_t *)TVPMmapAlloc(readSize);
            fileDataMmap = true;
        }
#endif
        if(!fileData)
            fileData = new uint8_t[readSize];
        s->Read(fileData, readSize);
        delete s;

        auto freeFileData = [&]() {
            if(fileDataMmap) {
#if defined(__APPLE__) || defined(__linux__) || defined(__ANDROID__)
                TVPMmapFree(fileData);
#endif
            } else {
                delete[] fileData;
            }
            fileData = nullptr;
        };

        bool result = loadPSBData(fileData, readSize, filePath);
        freeFileData();
        return result;
    }

    void PSBFile::loadResource(PSBResource &res,
                               TJS::tTJSBinaryStream *stream) const {
        if(!res.index.has_value() || stream == nullptr) {
            throw std::runtime_error("Resource Index invalid");
        }

        const auto index = static_cast<size_t>(res.index.value());
        if(index >= chunkOffsets.value.size() ||
           index >= chunkLengths.value.size()) {
            throw std::runtime_error("Resource Index out of range");
        }
        const auto offset = static_cast<std::uint64_t>(chunkOffsets[index]);
        const auto length = static_cast<std::uint64_t>(chunkLengths[index]);
        const auto dataStart = static_cast<std::uint64_t>(_header.offsetChunkData);
        const auto streamSize = static_cast<std::uint64_t>(stream->GetSize());
        if(dataStart > streamSize || offset > streamSize - dataStart ||
           length > streamSize - dataStart - offset) {
            throw std::runtime_error("Resource chunk exceeds PSB stream");
        }
        stream->SetPosition(_header.offsetChunkData + offset);
        std::vector<std::uint8_t> tmp(static_cast<size_t>(length));
        if(length > 0) {
            stream->ReadBuffer(tmp.data(), static_cast<size_t>(length));
        }
        res.data = std::move(tmp);
    }

    void PSBFile::loadExtraResource(PSBResource &res,
                                    TJS::tTJSBinaryStream *stream) const {
        if(!res.index.has_value() || stream == nullptr) {
            throw std::runtime_error("Extra Resource Index invalid");
        }

        const auto index = static_cast<size_t>(res.index.value());
        if(index >= extraChunkOffsets.value.size() ||
           index >= extraChunkLengths.value.size()) {
            throw std::runtime_error("Extra Resource Index out of range");
        }
        const auto offset = static_cast<std::uint64_t>(extraChunkOffsets[index]);
        const auto length = static_cast<std::uint64_t>(extraChunkLengths[index]);
        const auto dataStart = static_cast<std::uint64_t>(_header.offsetExtraChunkData);
        const auto streamSize = static_cast<std::uint64_t>(stream->GetSize());
        if(dataStart > streamSize || offset > streamSize - dataStart ||
           length > streamSize - dataStart - offset) {
            throw std::runtime_error("Extra resource chunk exceeds PSB stream");
        }
        stream->SetPosition(_header.offsetExtraChunkData + offset);
        std::vector<std::uint8_t> tmp(static_cast<size_t>(length));
        if(length > 0) {
            stream->ReadBuffer(tmp.data(), static_cast<size_t>(length));
        }
        res.data = std::move(tmp);
    }

    void PSBFile::afterLoad() {
        constexpr auto intMax = static_cast<std::uint32_t>(INT_MAX);
        std::sort(strings.begin(), strings.end(),
                  [intMax](const PSB::PSBString &r1, const PSB::PSBString &r2) {
                      return r1.index.value_or(intMax) <
                          r2.index.value_or(intMax);
                  });
        std::sort(resources.begin(), resources.end(),
                  [intMax](const std::shared_ptr<PSBResource> &r1,
                           const std::shared_ptr<PSBResource> &r2) {
                      return r1->index.value_or(intMax) <
                          r2->index.value_or(intMax);
                  });
        std::sort(extraResources.begin(), extraResources.end(),
                  [intMax](const std::shared_ptr<PSBResource> &r1,
                           const std::shared_ptr<PSBResource> &r2) {
                      return r1->index.value_or(intMax) <
                          r2->index.value_or(intMax);
                  });
        inferType();
    }
} // namespace PSB
