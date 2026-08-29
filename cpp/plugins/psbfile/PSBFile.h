#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "tjs.h"
#include "PSB.h"
#include "PSBHeader.h"
#include "PSBValue.h"

namespace PSB {

    class PSBFile {
    public:
        PSBArray charset{};
        PSBArray namesData{};
        PSBArray nameIndexes{};
        std::vector<std::string> names{};
        PSBArray stringOffsets{};
        std::vector<PSBString> strings{};

        PSBArray chunkOffsets;
        PSBArray chunkLengths;

        std::vector<std::shared_ptr<PSBResource>> resources;

        PSBArray extraChunkOffsets{};
        PSBArray extraChunkLengths{};
        std::vector<std::shared_ptr<PSBResource>> extraResources;

        explicit PSBFile() = default;
        void resetState();

        void loadKeys(TJS::tTJSBinaryStream *stream);
        void loadNames();

        void setSeed(int seed) { this->_seed = seed; }
        using PreParseCallback =
            std::function<bool(std::uint8_t *, size_t)>;
        void setPreParseCallback(PreParseCallback callback) {
            _preParseCallback = std::move(callback);
        }

        /**
         * file type: *.PIMG
         * @param filePath
         */
        bool loadPSBFile(const ttstr &filePath);
        // `loadResources=false` is useful for metadata-only consumers (for
        // example gallery indexes) that need the object tree but not the
        // potentially hundreds of megabytes of embedded image chunks.  Keep
        // the default unchanged for normal PSB decoding.
        bool loadPSBData(const void *data, size_t size, const ttstr &sourceName,
                         bool loadResources = true);
        /**
         * Load a string based on index, lift stream Position
         */
        void loadString(std::unique_ptr<PSBString> &str,
                        TJS::tTJSBinaryStream *stream);

        std::shared_ptr<PSBList> loadList(TJS::tTJSBinaryStream *stream,
                                          bool lazyLoad = false);
        std::shared_ptr<PSBDictionary>
        loadObjects(TJS::tTJSBinaryStream *stream, bool lazyLoad = false);

        std::shared_ptr<PSBDictionary>
        loadObjectsV1(TJS::tTJSBinaryStream *stream, bool lazyLoad = false);
        std::shared_ptr<IPSBValue> unpack(TJS::tTJSBinaryStream *stream,
                                          bool lazyLoad = false);
        void loadResource(PSBResource &res,
                          TJS::tTJSBinaryStream *stream) const;
        void loadExtraResource(PSBResource &res,
                               TJS::tTJSBinaryStream *stream) const;
        void afterLoad();

        [[nodiscard]] std::shared_ptr<const PSBDictionary> getObjects() const {
            return std::dynamic_pointer_cast<const PSBDictionary>(_root);
        }

        [[nodiscard]] const std::shared_ptr<IPSBValue> &getRootValue() const {
            return _root;
        }

        [[nodiscard]] const tTJSVariant &getCompatRoot() const {
            return _compatRoot;
        }

        [[nodiscard]] bool hasCompatRoot() const {
            return _compatRoot.Type() == tvtObject &&
                _compatRoot.AsObjectNoAddRef() != nullptr;
        }

        [[nodiscard]] PSBSpec getPlatform() const {
            const auto objects = getObjects();
            if(!objects) {
                return PSBSpec::None;
            }
            const auto spec = (*objects)["spec"];
            if(!spec) {
                return PSBSpec::None;
            }
            std::string value;
            try {
                value = spec->toString();
            } catch(...) {
                return PSBSpec::Other;
            }
            std::transform(value.begin(), value.end(), value.begin(),
                           [](const unsigned char ch) {
                               return static_cast<char>(std::tolower(ch));
                           });
            if(value.empty() || value == "none") return PSBSpec::None;
            if(value == "common") return PSBSpec::Common;
            if(value == "krkr" || value == "kirikiri") return PSBSpec::Krkr;
            if(value == "win" || value == "windows") return PSBSpec::Win;
            if(value == "ems" || value == "webgl") return PSBSpec::Ems;
            if(value == "psp") return PSBSpec::PSP;
            if(value == "vita" || value == "psvita") return PSBSpec::Vita;
            if(value == "ps3") return PSBSpec::PS3;
            if(value == "ps4") return PSBSpec::PS4;
            if(value == "nx" || value == "switch") return PSBSpec::NX;
            if(value == "citra" || value == "3ds") return PSBSpec::Citra;
            if(value == "and" || value == "android") return PSBSpec::And;
            if(value == "x360" || value == "xbox360") return PSBSpec::X360;
            if(value == "revo" || value == "wii") return PSBSpec::Revo;
            return PSBSpec::Other;
        }

        [[nodiscard]] IPSBType *getTypeHandler() const {
            auto handler = TypeHandlers.find(_type);
            if(handler != TypeHandlers.end()) {
                return handler->second.get();
            }

            return TypeHandlers.at(PSBType::Motion).get();
        }

        PSBHeader getPSBHeader() const { return this->_header; }

        PSBType getType() const { return _type; }

        // Exact object image handed to the parser after container
        // decompression and the title-provided pre-parse transform. Native
        // E-mote backends consume the same bytes without reopening archives.
        [[nodiscard]] const std::shared_ptr<const std::vector<std::uint8_t>> &
        getObjectImage() const { return _objectImage; }

    private:
        int _seed = 0;
        PreParseCallback _preParseCallback;
        PSBHeader _header{};
        std::shared_ptr<IPSBValue> _root{};
        tTJSVariant _compatRoot{};
        std::shared_ptr<const std::vector<std::uint8_t>> _objectImage;
        PSBType _type{ PSBType::PSB };

        PSBType inferType() {
            // Keep the same precedence as FreeMote's format discriminator.
            // A tachie/image PSB can contain fields that also look like a
            // generic motion tree; relying on map iteration made the result
            // depend on registration order and produced different aliases.
            constexpr PSBType kOrder[] = {
                PSBType::Pimg,
                PSBType::Scn,
                PSBType::Mmo,
                PSBType::Tachie,
                PSBType::ArchiveInfo,
                PSBType::SoundArchive,
                PSBType::Motion,
            };
            for(const auto type : kOrder) {
                const auto it = TypeHandlers.find(type);
                if(it != TypeHandlers.end() && it->second->isThisType(*this)) {
                    this->_type = type;
                    return this->_type;
                }
            }
            this->_type = PSBType::PSB;
            return this->_type;
        }
    };
} // namespace PSB
