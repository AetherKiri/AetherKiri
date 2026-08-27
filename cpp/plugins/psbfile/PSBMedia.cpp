//
// Created by LiDon on 2025/9/11.
//

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <unordered_set>
#include <vector>
#include <spdlog/spdlog.h>

#include "PSBMedia.h"

#include "PSBFile.h"
#include "PSBMediaRegistry.h"
#include "resources/ImageMetadata.h"
#include "MsgIntf.h"
#include "Platform.h"
#include "StorageIntf.h"
#include "StorageImpl.h"
#include "SysInitIntf.h"
#include "UtilStreams.h"
#include "GraphicsLoaderIntf.h"
#include "motionplayer/ResourceManager.h"

namespace PSB {
#define LOGGER spdlog::get("plugin")

    namespace {
        std::string LowerAscii(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(),
                           [](const unsigned char ch) {
                               return static_cast<char>(std::tolower(ch));
                           });
            return value;
        }

        std::string Basename(std::string value) {
            const auto slash = value.find_last_of("/\\");
            if(slash != std::string::npos) {
                value.erase(0, slash + 1);
            }
            return value;
        }

        std::string StripExtension(std::string value) {
            const auto dot = value.find_last_of('.');
            if(dot != std::string::npos) {
                value.erase(dot);
            }
            return value;
        }

        bool IsBareMotionSliceRequest(const std::string &key) {
            if(key.empty() || key.find('/') != std::string::npos ||
               key.size() <= 4 ||
               key.compare(key.size() - 4, 4, ".tlg") != 0) {
                return false;
            }
            const auto stem = key.substr(0, key.size() - 4);
            return stem.find('_') != std::string::npos;
        }

        bool SharesMotionStem(const std::string &request,
                              const std::string &archive) {
            const auto requestStem = StripExtension(Basename(request));
            const auto archiveStem = StripExtension(Basename(archive));
            if(requestStem.empty() || archiveStem.empty()) {
                return false;
            }

            // A sliced resource is conventionally named
            // <motion-family>_<role>.tlg while the PSB is
            // <motion-family>_<motion>.mtn.  Compare the family token only;
            // the role itself is intentionally opaque and is assigned in the
            // authored PSB layer order below.
            const auto requestSep = requestStem.find('_');
            const auto archiveSep = archiveStem.find('_');
            const auto requestFamily = requestStem.substr(
                0, requestSep == std::string::npos ? requestStem.size()
                                                     : requestSep);
            const auto archiveFamily = archiveStem.substr(
                0, archiveSep == std::string::npos ? archiveStem.size()
                                                     : archiveSep);
            if(requestFamily.empty() || archiveFamily.empty()) {
                return false;
            }
            return requestFamily == archiveFamily ||
                requestStem.rfind(archiveStem + '_', 0) == 0 ||
                archiveStem.rfind(requestFamily + '_', 0) == 0;
        }

        bool HasMotionArchiveExtension(const std::string &value) {
            const auto lower = LowerAscii(value);
            for(const auto *extension : {".mtn", ".psb"}) {
                if(lower.size() > std::strlen(extension) &&
                   lower.compare(lower.size() - std::strlen(extension),
                                 std::strlen(extension), extension) == 0) {
                    return true;
                }
            }
            return false;
        }

        std::string ArchiveBoundaryKey(const std::string &key) {
            // Resource keys normally look like `archive.mtn/path`.  When the
            // archive itself lives in a subdirectory (for example
            // `motion/mono_loop.mtn`), splitting at the first slash would
            // incorrectly treat `motion` as the archive.  Keep the generic
            // storage path intact by recognizing a PSB/motion extension first.
            const auto lower = LowerAscii(key);
            size_t boundary = std::string::npos;
            for(const auto *extension : {".mtn/", ".psb/", ".pimg/"}) {
                const auto position = lower.find(extension);
                if(position == std::string::npos) {
                    continue;
                }
                const auto candidate = position + std::strlen(extension) - 1;
                if(boundary == std::string::npos || candidate < boundary) {
                    boundary = candidate;
                }
            }
            if(boundary != std::string::npos) {
                return key.substr(0, boundary);
            }
            const auto slash = key.find('/');
            return slash == std::string::npos ? key : key.substr(0, slash);
        }

        // Discover motion containers only when a sliced layer actually asks
        // for a missing bare TLG.  This keeps startup cheap while still
        // supporting games that keep the motion PSB inside an XP3 archive and
        // never issue an ordinary `mono_loop.mtn` load before SliceLayer.
        std::vector<std::string> DiscoverMotionArchives(
            const std::string &request, std::mutex &discoveryMutex,
            std::unordered_set<std::string> &scannedRoots,
            std::vector<std::string> &knownArchives) {
            std::lock_guard<std::mutex> discoveryLock(discoveryMutex);
            std::vector<std::string> roots;
            auto appendRoot = [&roots](const ttstr &root) {
                const auto value = root.AsStdString();
                if(value.empty() ||
                   std::find(roots.begin(), roots.end(), value) != roots.end()) {
                    return;
                }
                roots.push_back(value);
            };
            appendRoot(TVPProjectDir);
            appendRoot(TVPDataPath);
            appendRoot(TVPGetAppPath());

            auto appendKnown = [&knownArchives](std::string candidate) {
                std::replace(candidate.begin(), candidate.end(), '\\', '/');
                while(candidate.rfind("./", 0) == 0) {
                    candidate.erase(0, 2);
                }
                if(candidate.empty() || !HasMotionArchiveExtension(candidate)) {
                    return;
                }
                candidate = LowerAscii(std::move(candidate));
                if(std::find(knownArchives.begin(), knownArchives.end(),
                             candidate) == knownArchives.end()) {
                    knownArchives.push_back(std::move(candidate));
                }
            };

            for(const auto &root : roots) {
                if(!scannedRoots.insert(root).second) {
                    continue;
                }

                ttstr localRoot(root.c_str());
                try {
                    TVPGetLocalName(localRoot);
                } catch(...) {
                    continue;
                }

                std::vector<std::string> entries;
                TVPGetLocalFileListAt(
                    localRoot,
                    [&entries](const ttstr &file, tTVPLocalFileInfo *info) {
                        if(info != nullptr && (info->Mode & S_IFREG)) {
                            entries.push_back(file.AsStdString());
                        }
                    });

                for(const auto &entry : entries) {
                    if(HasMotionArchiveExtension(entry)) {
                        appendKnown(entry);
                        continue;
                    }
                    const auto lower = LowerAscii(entry);
                    if(lower.size() <= 4 ||
                       lower.compare(lower.size() - 4, 4, ".xp3") != 0) {
                        continue;
                    }

                    ttstr archivePath =
                        TVPGetPlacedPath(ttstr(entry.c_str()));
                    if(archivePath.IsEmpty()) {
                        archivePath = localRoot + ttstr(entry.c_str());
                    }
                    tTVPArchive *archive = nullptr;
                    try {
                        archive = TVPOpenArchive(archivePath, true);
                    } catch(...) {
                        archive = nullptr;
                    }
                    if(!archive) {
                        continue;
                    }
                    try {
                        for(tjs_uint index = 0; index < archive->GetCount();
                            ++index) {
                            const auto name = archive->GetName(index).AsStdString();
                            if(HasMotionArchiveExtension(name)) {
                                appendKnown(name);
                            }
                        }
                    } catch(...) {
                        // A damaged optional archive must not prevent the
                        // current game from resolving other motion assets.
                    }
                    archive->Release();
                }
            }

            std::vector<std::string> matches;
            for(const auto &archive : knownArchives) {
                if(SharesMotionStem(request, archive)) {
                    matches.push_back(archive);
                }
            }
            const char *debug = std::getenv("AETHERKIRI_PSB_DEBUG");
            if(LOGGER && debug && *debug && *debug != '0') {
                LOGGER->info("PSB motion discovery: request={} candidates={}",
                             request, matches.empty() ? std::string("<none>")
                                                      : matches.front());
            }
            return matches;
        }
    } // namespace

    std::vector<std::string> PSBMedia::discoverMotionArchives(
        const std::string &request) {
        return DiscoverMotionArchives(request, _motionDiscoveryMutex,
                                      _motionScannedRoots,
                                      _motionKnownArchives);
    }

    bool detail::IsSupportedImageHeader(const std::vector<uint8_t> &data) {
        if(data.size() >= 8 && data[0] == 0x89 && data[1] == 0x50 &&
           data[2] == 0x4e && data[3] == 0x47) {
            return true;
        }
        if(data.size() >= 15 &&
           memcmp(data.data(), "RIFF", 4) == 0 &&
           memcmp(data.data() + 8, "WEBPVP8", 7) == 0) {
            return true;
        }
        if(data.size() >= 2 && data[0] == 'B' && data[1] == 'M') {
            return true;
        }
        if(data.size() >= 3 && data[0] == 0xff && data[1] == 0xd8 &&
           data[2] == 0xff) {
            return true;
        }
        if(data.size() >= 3 && data[0] == 'T' && data[1] == 'L' &&
           data[2] == 'G') {
            return true;
        }
        return false;
    }

    namespace {
        size_t CalcEntryFootprint(const PSBMedia::CacheEntry &entry) {
            size_t total = entry.resource ? entry.resource->data.size() : 0;
            if(entry.convertedImage) {
                total += entry.convertedImage->size();
            }
            return total;
        }

        bool IsVerbosePSBDebugEnabled() {
            static const bool enabled = [] {
                const char *value = std::getenv("AETHERKIRI_PSB_DEBUG");
                return value && *value && *value != '0';
            }();
            return enabled;
        }

        bool IsDebugPSBKey(const std::string &key) {
            (void)key;
            return IsVerbosePSBDebugEnabled();
        }

        bool IsDebugPSBArchive(const std::string &archiveKey) {
            (void)archiveKey;
            return IsVerbosePSBDebugEnabled();
        }

        std::string ArchiveKeyFromResourceKey(const std::string &key) {
            const auto slashPos = key.find('/');
            if(slashPos == std::string::npos) {
                return key;
            }
            return key.substr(0, slashPos);
        }

        bool IsPinnedPSBArchive(const std::string &archiveKey) {
            return archiveKey == "window.pimg" ||
                archiveKey == "title.pimg" ||
                archiveKey == "quickmenu.pimg" ||
                archiveKey == "voicebar.pimg" ||
                archiveKey == "chapter.pimg" ||
                archiveKey == "file.pimg" ||
                archiveKey == "autoskip.psb";
        }

        bool IsLogoMotionPSBKey(const std::string &key) {
            return key.rfind("yuzulogo.mtn/", 0) == 0 ||
                key.rfind("m2logo.mtn/", 0) == 0;
        }

        std::string JoinResourcePath(const std::vector<std::string> &path) {
            std::string result;
            for(size_t index = 0; index < path.size(); ++index) {
                if(index != 0) {
                    result += '/';
                }
                result += path[index];
            }
            return result;
        }

        void RegisterValueResourcesIntoMedia(
            PSBMedia &media, const std::string &archiveKey,
            const std::shared_ptr<IPSBValue> &value,
            std::vector<std::string> &path,
            const std::shared_ptr<spdlog::logger> &logger,
            size_t &logged) {
            if(!value) {
                return;
            }

            const auto resource = std::dynamic_pointer_cast<PSBResource>(value);
            if(resource) {
                if(path.empty()) {
                    return;
                }
                const std::string name = JoinResourcePath(path);
                media.add(archiveKey + "/" + name, resource);
                if(logger && logged < 120 && IsDebugPSBArchive(archiveKey)) {
                    logger->info("psb register: {}/{}", archiveKey, name);
                    ++logged;
                }
                return;
            }

            const auto dict = std::dynamic_pointer_cast<PSBDictionary>(value);
            if(dict) {
                for(const auto &[key, child] : *dict) {
                    path.push_back(key);
                    RegisterValueResourcesIntoMedia(
                        media, archiveKey, child, path, logger, logged);
                    path.pop_back();
                }
                return;
            }

            const auto list = std::dynamic_pointer_cast<PSBList>(value);
            if(list) {
                for(size_t index = 0; index < list->size(); ++index) {
                    path.push_back(std::to_string(index));
                    RegisterValueResourcesIntoMedia(
                        media, archiveKey,
                        (*list)[static_cast<int>(index)], path, logger, logged);
                    path.pop_back();
                }
            }
        }

        uint16_t ReadLE16(const uint8_t *src) {
            return static_cast<uint16_t>(src[0]) |
                static_cast<uint16_t>(src[1] << 8);
        }

        uint16_t ReadBE16(const uint8_t *src) {
            return static_cast<uint16_t>(src[1]) |
                static_cast<uint16_t>(src[0] << 8);
        }

        bool DecompressRLPixel(const std::vector<uint8_t> &input,
                               std::vector<uint8_t> &output,
                               size_t align) {
            if(align == 0 || input.empty() || output.empty()) {
                return false;
            }

            constexpr uint8_t kLookAhead = 1u << 7;
            size_t inPos = 0;
            size_t outPos = 0;

            while(inPos < input.size()) {
                const uint8_t cmdByte = input[inPos++];
                if(cmdByte & kLookAhead) {
                    const size_t count =
                        static_cast<size_t>((cmdByte ^ kLookAhead) + 3);
                    if(inPos + align > input.size()) {
                        return false;
                    }
                    if(outPos + count * align > output.size()) {
                        return false;
                    }
                    for(size_t i = 0; i < count; ++i) {
                        memcpy(output.data() + outPos, input.data() + inPos, align);
                        outPos += align;
                    }
                    inPos += align;
                } else {
                    const size_t count =
                        static_cast<size_t>(cmdByte + 1) * align;
                    if(inPos + count > input.size()) {
                        return false;
                    }
                    if(outPos + count > output.size()) {
                        return false;
                    }
                    memcpy(output.data() + outPos, input.data() + inPos, count);
                    inPos += count;
                    outPos += count;
                }
            }

            return outPos == output.size();
        }

        std::shared_ptr<std::vector<uint8_t>> BuildBmpFromRaw(
            const PSBMedia::CachedImageInfo &info,
            const std::shared_ptr<PSBResource> &resource) {
            if(!resource || info.width <= 0 || info.height <= 0) {
                return nullptr;
            }

            const auto &rawSrc = resource->data;
            const size_t pixelCount =
                static_cast<size_t>(info.width) * static_cast<size_t>(info.height);
            if(pixelCount == 0) {
                return nullptr;
            }

            std::vector<uint8_t> rlDecoded;
            size_t decodedAlign = 0;
            const std::vector<uint8_t> *src = &rawSrc;

            auto inferAlign = [&]() -> size_t {
                const auto typedFormat = Extension::toPSBPixelFormat(info.type, info.spec);
                switch(typedFormat) {
                    case PSBPixelFormat::A8:
                    case PSBPixelFormat::L8:
                    case PSBPixelFormat::A8_SW:
                    case PSBPixelFormat::L8_SW:
                    case PSBPixelFormat::TileA8_SW:
                    case PSBPixelFormat::TileL8_SW:
                    case PSBPixelFormat::CI8:
                    case PSBPixelFormat::CI8_SW:
                    case PSBPixelFormat::CI8_SW_PSP:
                    case PSBPixelFormat::TileCI8:
                        return 1;
                    case PSBPixelFormat::A8L8:
                    case PSBPixelFormat::A8L8_SW:
                    case PSBPixelFormat::TileA8L8_SW:
                    case PSBPixelFormat::LeRGBA4444:
                    case PSBPixelFormat::BeRGBA4444:
                    case PSBPixelFormat::LeRGBA4444_SW:
                    case PSBPixelFormat::TileLeRGBA4444_SW:
                    case PSBPixelFormat::RGBA5650:
                    case PSBPixelFormat::RGBA5650_SW:
                    case PSBPixelFormat::TileRGBA5650_SW:
                    case PSBPixelFormat::CI4:
                    case PSBPixelFormat::CI4_SW:
                    case PSBPixelFormat::CI4_SW_PSP:
                    case PSBPixelFormat::TileCI4:
                    case PSBPixelFormat::RGB5A3:
                        return 2;
                    case PSBPixelFormat::LeRGBA8:
                    case PSBPixelFormat::BeRGBA8:
                    case PSBPixelFormat::LeRGBA8_SW:
                    case PSBPixelFormat::BeRGBA8_SW:
                    case PSBPixelFormat::FlipLeRGBA8_SW:
                    case PSBPixelFormat::FlipBeRGBA8_SW:
                    case PSBPixelFormat::TileLeRGBA8_SW:
                    case PSBPixelFormat::TileBeRGBA8_SW:
                    case PSBPixelFormat::TileBeRGBA8_Rvl:
                        return 4;
                    default:
                        if(!info.palette.empty()) {
                            return 1;
                        }
                        return 0;
                }
            };

            if(info.compress == PSBCompressType::RL) {
                std::vector<size_t> candidateAligns;
                if(const size_t inferred = inferAlign(); inferred != 0) {
                    candidateAligns.push_back(inferred);
                }
                for(size_t align : { static_cast<size_t>(4), static_cast<size_t>(3),
                                     static_cast<size_t>(2), static_cast<size_t>(1) }) {
                    if(std::find(candidateAligns.begin(), candidateAligns.end(),
                                 align) == candidateAligns.end()) {
                        candidateAligns.push_back(align);
                    }
                }
                for(const size_t align : candidateAligns) {
                    std::vector<uint8_t> trial(pixelCount * align);
                    if(!DecompressRLPixel(rawSrc, trial, align)) {
                        continue;
                    }
                    rlDecoded = std::move(trial);
                    src = &rlDecoded;
                    decodedAlign = align;
                    break;
                }
                if(src == &rawSrc) {
                    const size_t directAlign = inferAlign();
                    if(directAlign != 0 &&
                       rawSrc.size() >= pixelCount * directAlign) {
                        src = &rawSrc;
                        decodedAlign = directAlign;
                    } else {
                        return nullptr;
                    }
                }
            }

            // Motion PSBs produced by the game sometimes omit the pixel
            // format metadata. Keep bounded raw channel evidence in the
            // existing debug log so the generic fallback can be validated
            // against the authored data rather than the final screenshot.
            if(LOGGER && IsDebugPSBKey(info.debugKey) && decodedAlign == 4 &&
               src->size() >= 4) {
                std::string first;
                const size_t sampleBytes = std::min<size_t>(src->size(), 32);
                for(size_t i = 0; i < sampleBytes; ++i) {
                    if(i != 0) first += ',';
                    first += std::to_string((*src)[i]);
                }
                size_t zero[4] = {0, 0, 0, 0};
                size_t full[4] = {0, 0, 0, 0};
                size_t partial[4] = {0, 0, 0, 0};
                const size_t samples = std::min(pixelCount, size_t{4096});
                for(size_t p = 0; p < samples; ++p) {
                    const size_t base = p * 4;
                    for(size_t channel = 0; channel < 4; ++channel) {
                        const uint8_t value = (*src)[base + channel];
                        if(value == 0) {
                            ++zero[channel];
                        } else if(value == 255) {
                            ++full[channel];
                        } else {
                            ++partial[channel];
                        }
                    }
                }
                LOGGER->info(
                    "psb raw4 key={} first=[{}] samples={} ch0(z/f/p)={}/{}/{} ch1={}/{}/{} ch2={}/{}/{} ch3={}/{}/{}",
                    info.debugKey, first, samples, zero[0], full[0], partial[0],
                    zero[1], full[1], partial[1], zero[2], full[2], partial[2],
                    zero[3], full[3], partial[3]);
                if(std::getenv("AETHERKIRI_PSB_DUMP_CHANNELS") &&
                   info.debugKey.find("motion/mono_loop.mtn/") == 0) {
                    std::string label = info.debugKey;
                    std::replace(label.begin(), label.end(), '/', '_');
                    std::ofstream rawFile("/tmp/aetherkiri-" + label + ".raw",
                                          std::ios::binary);
                    rawFile.write(reinterpret_cast<const char *>(src->data()),
                                  static_cast<std::streamsize>(src->size()));
                }
            }

            PSBPixelFormat format =
                Extension::toPSBPixelFormat(info.type.empty() ? "RGBA8" : info.type,
                                            info.spec);
            const bool assumeBGRA =
                info.type.empty() && decodedAlign == 4 && info.palette.empty();
            if(info.type.empty()) {
                if(!info.palette.empty()) {
                    if(src->size() == pixelCount) {
                        format = PSBPixelFormat::CI8;
                    } else if(src->size() * 2 == pixelCount) {
                        format = PSBPixelFormat::CI4;
                    }
                } else if(decodedAlign == 4 || src->size() == pixelCount * 4) {
                    format = PSBPixelFormat::LeRGBA8;
                } else if(decodedAlign == 3 || src->size() == pixelCount * 3) {
                    format = PSBPixelFormat::None;
                } else if(decodedAlign == 2 || src->size() == pixelCount * 2) {
                    format = PSBPixelFormat::RGBA5650;
                } else if(decodedAlign == 1 || src->size() == pixelCount) {
                    format = PSBPixelFormat::L8;
                }
            }

            const auto paletteFormat = Extension::toPSBPixelFormat(
                info.paletteType, info.spec);
            const bool logoPaletteIsBgra = IsLogoMotionPSBKey(info.debugKey);

            const auto build32Bmp = [&](auto &&pixelWriter) {
                const size_t pitch = static_cast<size_t>(info.width) * 4;
                const size_t headerSize = sizeof(TVP_WIN_BITMAPFILEHEADER) +
                    sizeof(TVP_WIN_BITMAPINFOHEADER);
                const size_t fileSize =
                    headerSize + pitch * static_cast<size_t>(info.height);

                auto out = std::make_shared<std::vector<uint8_t>>(fileSize);
                auto *dst = out->data();

                TVP_WIN_BITMAPFILEHEADER bfh{};
                bfh.bfType = 'B' + ('M' << 8);
                bfh.bfSize = static_cast<tjs_uint32>(fileSize);
                bfh.bfOffBits = static_cast<tjs_uint32>(headerSize);
                memcpy(dst, &bfh, sizeof bfh);

                TVP_WIN_BITMAPINFOHEADER bih{};
                bih.biSize = sizeof(bih);
                bih.biWidth = info.width;
                bih.biHeight = info.height;
                bih.biPlanes = 1;
                bih.biBitCount = 32;
                bih.biCompression = 0;
                memcpy(dst + sizeof(bfh), &bih, sizeof bih);

                auto *pixelDst = dst + headerSize;
                for(int y = 0; y < info.height; ++y) {
                    auto *rowDst = pixelDst +
                        static_cast<size_t>(info.height - 1 - y) * pitch;
                    for(int x = 0; x < info.width; ++x) {
                        pixelWriter(x, y, rowDst + static_cast<size_t>(x) * 4);
                    }
                }

                // One-shot diagnostic for validating the decoded alpha map
                // independently of the renderer.  This is intentionally
                // opt-in and is removed/disabled by default in production.
                if(const char *dump = std::getenv("AETHERKIRI_PSB_DUMP_RAW");
                   dump && *dump && *dump != '0') {
                    static std::mutex dumpMutex;
                    static std::unordered_set<std::string> dumped;
                    std::lock_guard<std::mutex> lock(dumpMutex);
                    std::string label = info.debugKey;
                    std::replace(label.begin(), label.end(), '/', '_');
                    std::replace(label.begin(), label.end(), '\\', '_');
                    if(dumped.insert(label).second) {
                        std::ofstream file("/tmp/aetherkiri-psb-" + label + ".bmp",
                                           std::ios::binary);
                        file.write(reinterpret_cast<const char *>(out->data()),
                                   static_cast<std::streamsize>(out->size()));
                    }
                }
                return out;
            };

            const auto writePalettePixel = [&](uint32_t paletteIndex,
                                               uint8_t *dstPx) {
                if(info.palette.empty()) {
                    dstPx[0] = 0;
                    dstPx[1] = 0;
                    dstPx[2] = 0;
                    dstPx[3] = 0xff;
                    return;
                }

                switch(paletteFormat == PSBPixelFormat::None
                           ? Extension::defaultPalettePixelFormat(info.spec)
                           : paletteFormat) {
                    case PSBPixelFormat::LeRGBA8:
                    case PSBPixelFormat::BeRGBA8:
                    default: {
                        const size_t index = static_cast<size_t>(paletteIndex) * 4;
                        if(index + 3 >= info.palette.size()) {
                            dstPx[0] = 0;
                            dstPx[1] = 0;
                            dstPx[2] = 0;
                            dstPx[3] = 0xff;
                            return;
                        }
                        dstPx[0] = info.palette[index + (logoPaletteIsBgra ? 0 : 2)];
                        dstPx[1] = info.palette[index + 1];
                        dstPx[2] = info.palette[index + (logoPaletteIsBgra ? 2 : 0)];
                        dstPx[3] = info.palette[index + 3];
                        return;
                    }
                    case PSBPixelFormat::RGB5A3: {
                        const size_t index = static_cast<size_t>(paletteIndex) * 2;
                        if(index + 1 >= info.palette.size()) {
                            dstPx[0] = 0;
                            dstPx[1] = 0;
                            dstPx[2] = 0;
                            dstPx[3] = 0xff;
                            return;
                        }
                        const uint16_t v = ReadBE16(&info.palette[index]);
                        if(v & 0x8000) {
                            const uint8_t r =
                                static_cast<uint8_t>(((v >> 10) & 0x1f) * 255 / 31);
                            const uint8_t g =
                                static_cast<uint8_t>(((v >> 5) & 0x1f) * 255 / 31);
                            const uint8_t b =
                                static_cast<uint8_t>((v & 0x1f) * 255 / 31);
                            dstPx[0] = b;
                            dstPx[1] = g;
                            dstPx[2] = r;
                            dstPx[3] = 0xff;
                        } else {
                            const uint8_t a =
                                static_cast<uint8_t>(((v >> 12) & 0x7) * 255 / 7);
                            const uint8_t r =
                                static_cast<uint8_t>(((v >> 8) & 0xf) * 17);
                            const uint8_t g =
                                static_cast<uint8_t>(((v >> 4) & 0xf) * 17);
                            const uint8_t b =
                                static_cast<uint8_t>((v & 0xf) * 17);
                            dstPx[0] = b;
                            dstPx[1] = g;
                            dstPx[2] = r;
                            dstPx[3] = a;
                        }
                        return;
                    }
                }
            };

            switch(format) {
                case PSBPixelFormat::LeRGBA8:
                case PSBPixelFormat::BeRGBA8:
                case PSBPixelFormat::LeRGBA8_SW:
                case PSBPixelFormat::BeRGBA8_SW:
                case PSBPixelFormat::FlipLeRGBA8_SW:
                case PSBPixelFormat::FlipBeRGBA8_SW:
                case PSBPixelFormat::TileLeRGBA8_SW:
                case PSBPixelFormat::TileBeRGBA8_SW:
                case PSBPixelFormat::TileBeRGBA8_Rvl:
                case PSBPixelFormat::None: {
                    if(src->size() >= pixelCount * 3 && src->size() < pixelCount * 4) {
                        return build32Bmp([&](int x, int y, uint8_t *dstPx) {
                            const size_t index =
                                (static_cast<size_t>(y) * info.width + x) * 3;
                            dstPx[0] = (*src)[index + 2];
                            dstPx[1] = (*src)[index + 1];
                            dstPx[2] = (*src)[index + 0];
                            dstPx[3] = 0xff;
                        });
                    }
                    if(src->size() < pixelCount * 4) {
                        return nullptr;
                    }
                    return build32Bmp([&](int x, int y, uint8_t *dstPx) {
                        const size_t index =
                            (static_cast<size_t>(y) * info.width + x) * 4;
                        if(assumeBGRA) {
                            dstPx[0] = (*src)[index + 0];
                            dstPx[1] = (*src)[index + 1];
                            dstPx[2] = (*src)[index + 2];
                        } else {
                            dstPx[0] = (*src)[index + 2];
                            dstPx[1] = (*src)[index + 1];
                            dstPx[2] = (*src)[index + 0];
                        }
                        dstPx[3] = (*src)[index + 3];
                    });
                }
                case PSBPixelFormat::A8: {
                    if(src->size() < pixelCount) {
                        return nullptr;
                    }
                    return build32Bmp([&](int x, int y, uint8_t *dstPx) {
                        const size_t index = static_cast<size_t>(y) * info.width + x;
                        dstPx[0] = 0xff;
                        dstPx[1] = 0xff;
                        dstPx[2] = 0xff;
                        dstPx[3] = (*src)[index];
                    });
                }
                case PSBPixelFormat::L8:
                case PSBPixelFormat::L8_SW:
                case PSBPixelFormat::TileL8_SW: {
                    if(src->size() < pixelCount) {
                        return nullptr;
                    }
                    return build32Bmp([&](int x, int y, uint8_t *dstPx) {
                        const size_t index = static_cast<size_t>(y) * info.width + x;
                        dstPx[0] = (*src)[index];
                        dstPx[1] = (*src)[index];
                        dstPx[2] = (*src)[index];
                        dstPx[3] = 0xff;
                    });
                }
                case PSBPixelFormat::A8L8:
                case PSBPixelFormat::A8L8_SW:
                case PSBPixelFormat::TileA8L8_SW: {
                    if(src->size() < pixelCount * 2) {
                        return nullptr;
                    }
                    return build32Bmp([&](int x, int y, uint8_t *dstPx) {
                        const size_t index =
                            (static_cast<size_t>(y) * info.width + x) * 2;
                        const uint8_t l = (*src)[index + 0];
                        const uint8_t a = (*src)[index + 1];
                        dstPx[0] = l;
                        dstPx[1] = l;
                        dstPx[2] = l;
                        dstPx[3] = a;
                    });
                }
                case PSBPixelFormat::LeRGBA4444:
                case PSBPixelFormat::BeRGBA4444:
                case PSBPixelFormat::LeRGBA4444_SW:
                case PSBPixelFormat::TileLeRGBA4444_SW: {
                    if(src->size() < pixelCount * 2) {
                        return nullptr;
                    }
                    return build32Bmp([&](int x, int y, uint8_t *dstPx) {
                        const size_t index =
                            (static_cast<size_t>(y) * info.width + x) * 2;
                        const uint16_t v = ReadLE16(&(*src)[index]);
                        const uint8_t r =
                            static_cast<uint8_t>(((v >> 12) & 0x0f) * 17);
                        const uint8_t g =
                            static_cast<uint8_t>(((v >> 8) & 0x0f) * 17);
                        const uint8_t b =
                            static_cast<uint8_t>(((v >> 4) & 0x0f) * 17);
                        const uint8_t a = static_cast<uint8_t>((v & 0x0f) * 17);
                        dstPx[0] = b;
                        dstPx[1] = g;
                        dstPx[2] = r;
                        dstPx[3] = a;
                    });
                }
                case PSBPixelFormat::RGBA5650:
                case PSBPixelFormat::RGBA5650_SW:
                case PSBPixelFormat::TileRGBA5650_SW: {
                    if(src->size() < pixelCount * 2) {
                        return nullptr;
                    }
                    return build32Bmp([&](int x, int y, uint8_t *dstPx) {
                        const size_t index =
                            (static_cast<size_t>(y) * info.width + x) * 2;
                        const uint16_t v = ReadLE16(&(*src)[index]);
                        const uint8_t r =
                            static_cast<uint8_t>(((v >> 11) & 0x1f) * 255 / 31);
                        const uint8_t g =
                            static_cast<uint8_t>(((v >> 5) & 0x3f) * 255 / 63);
                        const uint8_t b =
                            static_cast<uint8_t>((v & 0x1f) * 255 / 31);
                        dstPx[0] = b;
                        dstPx[1] = g;
                        dstPx[2] = r;
                        dstPx[3] = 0xff;
                    });
                }
                case PSBPixelFormat::CI8:
                case PSBPixelFormat::CI8_SW:
                case PSBPixelFormat::CI8_SW_PSP:
                case PSBPixelFormat::TileCI8: {
                    if(src->size() < pixelCount || info.palette.empty()) {
                        return nullptr;
                    }
                    return build32Bmp([&](int x, int y, uint8_t *dstPx) {
                        const size_t index = static_cast<size_t>(y) * info.width + x;
                        writePalettePixel((*src)[index], dstPx);
                    });
                }
                case PSBPixelFormat::CI4:
                case PSBPixelFormat::CI4_SW:
                case PSBPixelFormat::CI4_SW_PSP:
                case PSBPixelFormat::TileCI4: {
                    if(src->size() * 2 < pixelCount || info.palette.empty()) {
                        return nullptr;
                    }
                    return build32Bmp([&](int x, int y, uint8_t *dstPx) {
                        const size_t pixelIndex =
                            static_cast<size_t>(y) * info.width + x;
                        const uint8_t packed = (*src)[pixelIndex / 2];
                        const uint8_t palIndex =
                            (pixelIndex & 1) == 0 ? static_cast<uint8_t>((packed >> 4) & 0x0f)
                                                  : static_cast<uint8_t>(packed & 0x0f);
                        writePalettePixel(palIndex, dstPx);
                    });
                }
                default:
                    return nullptr;
            }
        }

        size_t ClampSizeT(size_t value, size_t min_value, size_t max_value) {
            return std::max(min_value, std::min(value, max_value));
        }

        float GetPSBFloat(const std::shared_ptr<IPSBValue> &value, float fallback = 0.0f) {
            if(!value) return fallback;
            if(const auto num = std::dynamic_pointer_cast<PSBNumber>(value)) {
                if(num->numberType == PSBNumberType::Float)
                    return num->getFloatValue();
                if(num->numberType == PSBNumberType::Double)
                    return static_cast<float>(BitConverter::fromByteArray<double>(num->data));
                return static_cast<float>(num->getLongValue());
            }
            return fallback;
        }

        int GetPSBInt(const std::shared_ptr<IPSBValue> &value, int fallback = 0) {
            if(!value) return fallback;
            if(const auto num = std::dynamic_pointer_cast<PSBNumber>(value)) {
                if(num->numberType == PSBNumberType::Float)
                    return static_cast<int>(num->getFloatValue());
                if(num->numberType == PSBNumberType::Double)
                    return static_cast<int>(
                        BitConverter::fromByteArray<double>(num->data));
                return static_cast<int>(num->getLongValue());
            }
            return fallback;
        }

        void DumpPSBValue(const std::shared_ptr<spdlog::logger> &logger,
                          const std::string &name,
                          const std::shared_ptr<IPSBValue> &value,
                          int depth, int maxDepth) {
            if(!logger || !value || depth > maxDepth) return;
            std::string indent(depth * 2, ' ');
            if(const auto subDict = std::dynamic_pointer_cast<PSBDictionary>(value)) {
                logger->info("{}{}/ (dict, {} keys)", indent, name, subDict->size());
                if(depth < maxDepth) {
                    for(const auto &[key, child] : *subDict) {
                        DumpPSBValue(logger, key, child, depth + 1, maxDepth);
                    }
                }
            } else if(const auto num = std::dynamic_pointer_cast<PSBNumber>(value)) {
                if(num->numberType == PSBNumberType::Float)
                    logger->info("{}{}= {} (float)", indent, name, num->getFloatValue());
                else
                    logger->info("{}{}= {} (int/long)", indent, name, num->getLongValue());
            } else if(const auto str = std::dynamic_pointer_cast<PSBString>(value)) {
                logger->info("{}{}= \"{}\" (string)", indent, name, str->value);
            } else if(const auto list = std::dynamic_pointer_cast<PSBList>(value)) {
                logger->info("{}{}= [list, {} items]", indent, name, list->size());
                if(depth < maxDepth) {
                    for(int i = 0; i < static_cast<int>(list->size()) && i < 3; i++) {
                        DumpPSBValue(logger, "[" + std::to_string(i) + "]",
                                     (*list)[i], depth + 1, maxDepth);
                    }
                }
            } else {
                logger->info("{}{}= <other>", indent, name);
            }
        }

        std::string MapSrcToResourcePath(const std::string &src) {
            // "src/title/bg" → "source/title/icon/bg"
            if(src.size() > 4 && src.substr(0, 4) == "src/") {
                std::string rest = src.substr(4); // "title/bg"
                auto slashPos = rest.find('/');
                if(slashPos != std::string::npos) {
                    std::string group = rest.substr(0, slashPos);  // "title"
                    std::string name = rest.substr(slashPos + 1);  // "bg"
                    return "source/" + group + "/icon/" + name;
                }
            }
            return src;
        }

        struct LayerFrameInfo {
            std::string src;
            float ox = 0, oy = 0, cx = 0, cy = 0;
        };

        bool ExtractFrameInfo(const std::shared_ptr<PSBDictionary> &layerDict,
                              LayerFrameInfo &out,
                              const std::shared_ptr<spdlog::logger> &logger = nullptr,
                              const std::string &debugLabel = "") {
            auto frameList = std::dynamic_pointer_cast<PSBList>((*layerDict)["frameList"]);
            if(!frameList || frameList->size() == 0) {
                if(logger) logger->info("  ExtractFrameInfo[{}]: no frameList", debugLabel);
                return false;
            }
            auto frame0 = std::dynamic_pointer_cast<PSBDictionary>((*frameList)[0]);
            if(!frame0) {
                if(logger) logger->info("  ExtractFrameInfo[{}]: frame0 not dict", debugLabel);
                return false;
            }
            auto content = std::dynamic_pointer_cast<PSBDictionary>((*frame0)["content"]);
            if(!content) {
                if(logger) {
                    std::string keys;
                    for(const auto &[k, v] : *frame0) {
                        if(!keys.empty()) keys += ", ";
                        keys += k;
                    }
                    logger->info("  ExtractFrameInfo[{}]: no content in frame0 (keys: {})",
                        debugLabel, keys);
                }
                return false;
            }
            auto srcStr = std::dynamic_pointer_cast<PSBString>((*content)["src"]);
            if(!srcStr || srcStr->value.empty()) {
                if(logger) logger->info("  ExtractFrameInfo[{}]: no src string in content", debugLabel);
                return false;
            }

            out.src = srcStr->value;
            out.ox = GetPSBFloat((*content)["ox"], 0);
            out.oy = GetPSBFloat((*content)["oy"], 0);
            auto coordList = std::dynamic_pointer_cast<PSBList>((*content)["coord"]);
            if(coordList && coordList->size() >= 2) {
                out.cx = GetPSBFloat((*coordList)[0], 0);
                out.cy = GetPSBFloat((*coordList)[1], 0);
            }
            return true;
        }

        void CollectLayersFromMotion(
            const std::shared_ptr<PSBDictionary> &motionDict,
            const std::string &motionName,
            const std::string &sceneName,
            float parentX, float parentY,
            const std::shared_ptr<PSBDictionary> &objectTree,
            std::vector<PSBMedia::LayerPosition> &positions,
            std::vector<PSBMedia::ButtonBoundInfo> *buttons,
            const std::shared_ptr<spdlog::logger> &logger,
            int depth = 0) {
            if(depth > 8) return;

            auto targetMotion = std::dynamic_pointer_cast<PSBDictionary>((*motionDict)[motionName]);
            if(!targetMotion) {
                if(logger && depth > 0) logger->info("CollectLayers: {}/{} not found (depth={})",
                    sceneName, motionName, depth);
                return;
            }

            auto layerList = std::dynamic_pointer_cast<PSBList>((*targetMotion)["layer"]);
            if(!layerList) {
                if(logger && depth > 0) logger->info("CollectLayers: {}/{} has no layer list (depth={})",
                    sceneName, motionName, depth);
                return;
            }

            if(logger) {
                logger->info("CollectLayers: {}/{} has {} layers (depth={})",
                    sceneName, motionName, layerList->size(), depth);
            }

            for(int i = 0; i < static_cast<int>(layerList->size()); i++) {
                auto layerDict = std::dynamic_pointer_cast<PSBDictionary>((*layerList)[i]);
                if(!layerDict) continue;

                auto labelVal = std::dynamic_pointer_cast<PSBString>((*layerDict)["label"]);
                std::string label = labelVal ? labelVal->value : ("layer_" + std::to_string(i));

                LayerFrameInfo fi;
                if(!ExtractFrameInfo(layerDict, fi, logger, sceneName + "/" + motionName + "/" + label)) continue;

                float finalX = parentX + fi.ox + fi.cx;
                float finalY = parentY + fi.oy + fi.cy;

                if(fi.src.substr(0, 7) == "motion/") {
                    std::string ref = fi.src.substr(7);
                    auto slash = ref.find('/');
                    if(slash != std::string::npos && objectTree) {
                        std::string objName = ref.substr(0, slash);
                        std::string subMotion = ref.substr(slash + 1);
                        auto objDict = std::dynamic_pointer_cast<PSBDictionary>((*objectTree)[objName]);
                        if(objDict) {
                            auto objMotionDict = std::dynamic_pointer_cast<PSBDictionary>((*objDict)["motion"]);
                            if(objMotionDict) {
                                if(logger) {
                                    logger->info("follow motion ref: {} → {}/{} offset=({},{})",
                                        fi.src, objName, subMotion, finalX, finalY);
                                }
                                size_t posBefore = positions.size();
                                CollectLayersFromMotion(objMotionDict, subMotion, sceneName,
                                    finalX, finalY, objectTree, positions, buttons, logger, depth + 1);

                                if(buttons) {
                                    std::string newImageKey;
                                    if(positions.size() > posBefore) {
                                        newImageKey = positions[posBefore].srcPath;
                                    }
                                    bool found = false;
                                    for(auto &existing : *buttons) {
                                        if(existing.buttonName == objName && existing.sceneName == sceneName) {
                                            if(existing.imageKey.empty() && !newImageKey.empty()) {
                                                existing.imageKey = newImageKey;
                                            }
                                            found = true;
                                            break;
                                        }
                                    }
                                    if(!found) {
                                        PSBMedia::ButtonBoundInfo btn;
                                        btn.sceneName = sceneName;
                                        btn.buttonName = objName;
                                        btn.imageKey = newImageKey;
                                        btn.left = finalX;
                                        btn.top = finalY;
                                        buttons->push_back(std::move(btn));
                                    }
                                }
                            }
                        }
                    }
                } else if(fi.src.substr(0, 4) == "src/") {
                    std::string resourcePath = MapSrcToResourcePath(fi.src);
                    PSBMedia::LayerPosition pos;
                    pos.sceneName = sceneName;
                    pos.layerName = sceneName + "/" + label;
                    pos.srcPath = resourcePath;
                    pos.left = finalX;
                    pos.top = finalY;
                    pos.opacity = 255;
                    pos.visible = true;

                    if(logger) {
                        logger->info("layer: [{}] src={} → res={} final=({},{}) depth={}",
                            pos.layerName, fi.src, resourcePath, finalX, finalY, depth);
                    }

                    positions.push_back(std::move(pos));
                } else {
                    if(logger) {
                        logger->info("  unhandled src type: [{}] src={} pos=({},{}) depth={}",
                            label, fi.src, finalX, finalY, depth);
                    }
                }
            }
        }

        void CollectLayerPositionsFromMotion(
            std::vector<PSBMedia::LayerPosition> &positions,
            std::vector<PSBMedia::ButtonBoundInfo> &buttons,
            const std::shared_ptr<PSBDictionary> &objectTree,
            const std::shared_ptr<spdlog::logger> &logger) {
            if(!objectTree) return;

            for(const auto &[sceneName, sceneVal] : *objectTree) {
                auto sceneDict = std::dynamic_pointer_cast<PSBDictionary>(sceneVal);
                if(!sceneDict) continue;

                auto motionDict = std::dynamic_pointer_cast<PSBDictionary>((*sceneDict)["motion"]);
                if(!motionDict) continue;

                std::string motionName = "normal";
                if(!std::dynamic_pointer_cast<PSBDictionary>((*motionDict)[motionName])) {
                    motionName = "show";
                }
                if(!std::dynamic_pointer_cast<PSBDictionary>((*motionDict)[motionName])) {
                    motionName = "bt";
                }

                CollectLayersFromMotion(motionDict, motionName, sceneName,
                    0, 0, objectTree, positions, &buttons, logger);
            }
        }

        void RegisterPSBResourcesIntoMedia(
            PSBMedia &media, PSBFile &psb, const std::string &archiveKey) {
            auto logger = IsDebugPSBArchive(archiveKey) ? LOGGER : nullptr;
            size_t logged = 0;
            const auto objs = psb.getObjects();
            if(objs) {
                std::vector<std::string> path;
                RegisterValueResourcesIntoMedia(
                    media, archiveKey,
                    std::const_pointer_cast<PSBDictionary>(objs),
                    path, logger, logged);
            }

            auto *handler = psb.getTypeHandler();
            if(!handler)
                return;
            auto resources = handler->collectResources(psb, false);
            for(auto &metadata : resources) {
                auto *image =
                    dynamic_cast<ImageMetadata *>(metadata.get());
                if(!image)
                    continue;
                auto resource = image->getResource();
                if(!resource)
                    continue;
                const std::string name = image->getName();
                if(name.empty())
                    continue;
                media.add(archiveKey + "/" + name, resource, image);
                if(logger && logged < 120 && IsDebugPSBArchive(archiveKey)) {
                    logger->info("psb register: {}/{}", archiveKey, name);
                    ++logged;
                }

                const std::uint32_t resourceIndex = image->getIndex();
                if(resourceIndex != UINT32_MAX) {
                    const std::string indexedName =
                        std::to_string(resourceIndex) + ".tlg";
                    if(indexedName != name) {
                        media.add(archiveKey + "/" + indexedName, resource,
                                  image);
                        if(logger && logged < 120 &&
                           IsDebugPSBArchive(archiveKey)) {
                            logger->info("psb register alias: {}/{} -> {}",
                                         archiveKey, indexedName, name);
                            ++logged;
                        }
                    }
                }
            }

            if(objs) {
                auto objectTree = std::dynamic_pointer_cast<PSBDictionary>((*objs)["object"]);
                if(objectTree && logger) {
                    std::string objNames;
                    for(const auto &[k, v] : *objectTree) {
                        if(!objNames.empty()) objNames += ", ";
                        objNames += k;
                        auto d = std::dynamic_pointer_cast<PSBDictionary>(v);
                        if(d) {
                            auto m = std::dynamic_pointer_cast<PSBDictionary>((*d)["motion"]);
                            if(m) {
                                objNames += "[";
                                bool first = true;
                                for(const auto &[mk, mv] : *m) {
                                    if(!first) objNames += ",";
                                    objNames += mk;
                                    first = false;
                                }
                                objNames += "]";
                            }
                        }
                    }
                    logger->info("PSB objectTree for {}: {}", archiveKey, objNames);
                }

                if(objectTree) {
                    std::vector<PSBMedia::LayerPosition> positions;
                    std::vector<PSBMedia::ButtonBoundInfo> buttons;
                    CollectLayerPositionsFromMotion(positions, buttons, objectTree, logger);
                    if(!positions.empty()) {
                        size_t count = positions.size();
                        media.addLayerPositions(archiveKey, std::move(positions));
                        if(logger) logger->info("Stored {} layer positions for {}",
                            count, archiveKey);
                    }
                    if(!buttons.empty()) {
                        size_t count = buttons.size();
                        media.addButtonBounds(archiveKey, std::move(buttons));
                        if(logger) logger->info("Stored {} button bounds for {}",
                            count, archiveKey);
                    }
                }
            }
        }
    } // namespace

    PSBMedia::PSBMedia() {
        _ref = 1;

        tTJSVariant val;
        if(TVPGetCommandLineNoInit(TJS_W("memory_profile"), &val)) {
            ttstr profile = ttstr(val).AsLowerCase();
            if(profile == TJS_W("aggressive") || profile == TJS_W("lowmem")) {
                _configuredMaxEntryCount = 1024;
                _configuredMaxByteSize = 128ULL * 1024ULL * 1024ULL;
            }
        }

        if(TVPGetCommandLineNoInit(TJS_W("psb_cache_entries"), &val)) {
            const tjs_int configured = static_cast<tjs_int>(val.AsInteger());
            if(configured > 0) {
                _configuredMaxEntryCount =
                    static_cast<size_t>(configured);
            }
        }
        if(TVPGetCommandLineNoInit(TJS_W("psb_cache_mb"), &val)) {
            const tjs_int configured = static_cast<tjs_int>(val.AsInteger());
            if(configured > 0) {
                _configuredMaxByteSize = static_cast<size_t>(configured) *
                    1024ULL * 1024ULL;
            }
        }

        _configuredMaxEntryCount =
            ClampSizeT(_configuredMaxEntryCount, 128, 8192);
        _configuredMaxByteSize = ClampSizeT(
            _configuredMaxByteSize, 16ULL * 1024ULL * 1024ULL,
            512ULL * 1024ULL * 1024ULL);
        _maxEntryCount = _configuredMaxEntryCount;
        _maxByteSize = _configuredMaxByteSize;
    }

    void PSBMedia::NormalizeDomainName(ttstr &name) {
        name = name.AsLowerCase();
    }

    void PSBMedia::NormalizePathName(ttstr &name) {
        auto *p = name.Independ();
        while(*p) {
            if(*p == TJS_W('\\')) {
                *p = TJS_W('/');
            }
            ++p;
        }
        name = name.AsLowerCase();
    }

    std::string PSBMedia::canonicalizeKey(const std::string &key) const {
        std::string out;
        out.reserve(key.size());
        for(const char ch : key) {
            if(ch == '\\') {
                out.push_back('/');
            } else if(ch >= 'A' && ch <= 'Z') {
                out.push_back(static_cast<char>(ch - 'A' + 'a'));
            } else {
                out.push_back(ch);
            }
        }

        constexpr const char *kFileScheme = "file://";
        if(out.rfind(kFileScheme, 0) == 0) {
            out.erase(0, 7);
            if(out.rfind("./", 0) == 0) {
                out.erase(0, 1);
            }
        }
        if(out.rfind("./", 0) == 0) {
            out.erase(0, 1);
        }

        std::string compact;
        compact.reserve(out.size());
        bool prevSlash = false;
        for(const char ch : out) {
            if(ch == '/') {
                if(prevSlash) {
                    continue;
                }
                prevSlash = true;
            } else {
                prevSlash = false;
            }
            compact.push_back(ch);
        }
        return compact;
    }

    void PSBMedia::touchLocked(CacheEntry &entry) {
        if(entry.lruIt != _lru.begin()) {
            _lru.splice(_lru.begin(), _lru, entry.lruIt);
            entry.lruIt = _lru.begin();
        }
    }

    void PSBMedia::adaptBudgetByMemoryPressureLocked() {
        const tjs_int self_used_mb = TVPGetSelfUsedMemory();
        const tjs_int free_mb = TVPGetSystemFreeMemory();

        size_t max_entry_count = _configuredMaxEntryCount;
        size_t max_byte_size = _configuredMaxByteSize;

        if((self_used_mb >= 1500) || (free_mb >= 0 && free_mb < 512)) {
            max_entry_count = std::min(max_entry_count, static_cast<size_t>(512));
            max_byte_size = std::min(
                max_byte_size, static_cast<size_t>(96ULL * 1024ULL * 1024ULL));
        } else if((self_used_mb >= 1100) || (free_mb >= 0 && free_mb < 800)) {
            max_entry_count = std::min(max_entry_count, static_cast<size_t>(768));
            max_byte_size = std::min(
                max_byte_size, static_cast<size_t>(144ULL * 1024ULL * 1024ULL));
        } else if((self_used_mb >= 850) || (free_mb >= 0 && free_mb < 1200)) {
            max_entry_count = std::min(max_entry_count, static_cast<size_t>(1024));
            max_byte_size = std::min(
                max_byte_size, static_cast<size_t>(192ULL * 1024ULL * 1024ULL));
        }

        _maxEntryCount = max_entry_count;
        _maxByteSize = max_byte_size;
    }

    PSBMedia::ResourceMap::iterator
    PSBMedia::findBySuffixLocked(const std::string &key) {
        auto matched = _resources.end();
        size_t matchCount = 0;
        std::string firstMatch;
        std::string secondMatch;

        for(auto it = _resources.begin(); it != _resources.end(); ++it) {
            const auto &stored = it->first;
            if(stored.size() < key.size()) {
                continue;
            }
            if(stored.compare(stored.size() - key.size(), key.size(), key) != 0) {
                continue;
            }
            if(stored.size() == key.size()) {
                return it;
            }

            const char boundary = stored[stored.size() - key.size() - 1];
            if(boundary == '/' || boundary == '>') {
                if(matchCount == 0) {
                    matched = it;
                    firstMatch = stored;
                } else if(matchCount == 1) {
                    secondMatch = stored;
                }
                ++matchCount;
            }
        }

        if(matchCount == 1) {
            return matched;
        }
        if(matchCount > 1) {
            LOGGER->debug(
                "PSB media cache ambiguous suffix-hit rejected: {} -> {}, {}{}",
                key, firstMatch, secondMatch, matchCount > 2 ? ", ..." : "");
        }
        return _resources.end();
    }

    void PSBMedia::evictIfNeededLocked() {
        adaptBudgetByMemoryPressureLocked();

        size_t evictedCount = 0;
        size_t evictedBytes = 0;
        size_t pinnedScans = 0;
        while((_resources.size() > _maxEntryCount || _bytesInUse > _maxByteSize) &&
              !_lru.empty() && pinnedScans < _lru.size()) {
            const std::string victimKey = _lru.back();
            _lru.pop_back();

            auto it = _resources.find(victimKey);
            if(it == _resources.end()) {
                continue;
            }
            if(IsPinnedPSBArchive(ArchiveKeyFromResourceKey(victimKey))) {
                _lru.push_front(victimKey);
                it->second.lruIt = _lru.begin();
                ++pinnedScans;
                continue;
            }

            pinnedScans = 0;
            evictedCount++;
            evictedBytes += it->second.sizeBytes;
            _bytesInUse -= it->second.sizeBytes;
            _resources.erase(it);
        }

        if(evictedCount > 0) {
            LOGGER->debug(
                "PSB media cache evicted: count={} bytes={} remain_count={} "
                "remain_bytes={}",
                evictedCount, evictedBytes, _resources.size(), _bytesInUse);
        }
    }

    bool PSBMedia::CheckExistentStorage(const ttstr &name) {
        const auto key = canonicalizeKey(name.AsStdString());
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if(_resources.find(key) != _resources.end()) {
                _hitCount++;
                return true;
            }
            const bool found = findBySuffixLocked(key) != _resources.end();
            if(found)
                _hitCount++;
            else
                _missCount++;
            if(found)
                return true;
        }
        if(!tryLazyLoadArchive(key, true))
            return false;
        std::lock_guard<std::mutex> lock(_mutex);
        if(_resources.find(key) != _resources.end()) {
            _hitCount++;
            return true;
        }
        const bool found = findBySuffixLocked(key) != _resources.end();
        if(found)
            _hitCount++;
        else
            _missCount++;
        return found;
    }

    tTJSBinaryStream *PSBMedia::Open(const ttstr &name, tjs_uint32 flags) {
        (void)flags;
        const auto key = canonicalizeKey(name.AsStdString());

        std::shared_ptr<PSBResource> res;
        std::shared_ptr<std::vector<uint8_t>> convertedImage;
        CachedImageInfo imageInfo;
        bool hasImageInfo = false;
        std::string resolvedKey;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            auto it = _resources.find(key);
            if(it == _resources.end()) {
                it = findBySuffixLocked(key);
                if(it != _resources.end()) {
                    LOGGER->debug("PSB media cache suffix-hit: {} -> {}", key,
                                  it->first);
                }
            }
            if(it != _resources.end() && it->second.resource != nullptr) {
                _hitCount++;
                touchLocked(it->second);
                res = it->second.resource;
                convertedImage = it->second.convertedImage;
                imageInfo = it->second.imageInfo;
                hasImageInfo = it->second.hasImageInfo;
                resolvedKey = it->first;
            }
        }

        if(!res && tryLazyLoadArchive(key, true)) {
            std::lock_guard<std::mutex> lock(_mutex);
            auto it = _resources.find(key);
            if(it == _resources.end()) {
                it = findBySuffixLocked(key);
                if(it != _resources.end()) {
                    LOGGER->debug("PSB media cache suffix-hit(after-load): {} -> {}",
                                  key, it->first);
                }
            }
            if(it != _resources.end() && it->second.resource != nullptr) {
                _hitCount++;
                touchLocked(it->second);
                res = it->second.resource;
                convertedImage = it->second.convertedImage;
                imageInfo = it->second.imageInfo;
                hasImageInfo = it->second.hasImageInfo;
                resolvedKey = it->first;
            }
        }

        if(!res) {
            std::lock_guard<std::mutex> lock(_mutex);
            _missCount++;
            LOGGER->warn("PSB media cache miss: {}", key);
            TVPThrowExceptionMessage(TJS_W("%1:cannot open psb resource"),
                                     name);
            return nullptr;
        }
        if(LOGGER && IsDebugPSBKey(resolvedKey)) {
            const uint32_t header =
                res->data.size() >= 4
                ? static_cast<uint32_t>(res->data[0]) |
                    (static_cast<uint32_t>(res->data[1]) << 8) |
                    (static_cast<uint32_t>(res->data[2]) << 16) |
                    (static_cast<uint32_t>(res->data[3]) << 24)
                : 0;
            LOGGER->info(
                "psb open: key={} hasMeta={} w={} h={} type={} palType={} pal={} compress={} raw={} header=0x{:08x}",
                resolvedKey, hasImageInfo ? 1 : 0, imageInfo.width,
                imageInfo.height, imageInfo.type, imageInfo.paletteType,
                imageInfo.palette.size(), static_cast<int>(imageInfo.compress),
                res->data.size(), header);
        }
        if(!convertedImage && hasImageInfo &&
           !detail::IsSupportedImageHeader(res->data)) {
            convertedImage = BuildBmpFromRaw(imageInfo, res);
            if(LOGGER && IsDebugPSBKey(resolvedKey)) {
                LOGGER->info(
                    "psb open: convert key={} ok={} converted={} type={}",
                    resolvedKey, convertedImage ? 1 : 0,
                    convertedImage ? convertedImage->size() : 0,
                    imageInfo.type);
            }
            if(convertedImage) {
                std::lock_guard<std::mutex> lock(_mutex);
                auto it = _resources.find(resolvedKey);
                if(it != _resources.end()) {
                    _bytesInUse -= it->second.sizeBytes;
                    it->second.convertedImage = convertedImage;
                    it->second.sizeBytes = CalcEntryFootprint(it->second);
                    _bytesInUse += it->second.sizeBytes;
                    touchLocked(it->second);
                    evictIfNeededLocked();
                }
            }
        }

        const auto &streamBytes =
            convertedImage ? *convertedImage : res->data;
        if(LOGGER && IsDebugPSBKey(resolvedKey)) {
            const uint32_t outHeader =
                streamBytes.size() >= 4
                ? static_cast<uint32_t>(streamBytes[0]) |
                    (static_cast<uint32_t>(streamBytes[1]) << 8) |
                    (static_cast<uint32_t>(streamBytes[2]) << 16) |
                    (static_cast<uint32_t>(streamBytes[3]) << 24)
                : 0;
            LOGGER->info("psb open: return key={} size={} header=0x{:08x}",
                         resolvedKey, streamBytes.size(), outHeader);
        }
        auto *memoryStream = new tTVPMemoryStream();
        memoryStream->WriteBuffer(streamBytes.data(), streamBytes.size());
        memoryStream->Seek(0, TJS_BS_SEEK_SET);
        return memoryStream;
    }

    bool PSBMedia::tryLazyLoadArchive(const std::string &key,
                                      bool reloadIfLoaded) {
        const auto archiveKey = ArchiveBoundaryKey(key);
        if(archiveKey.empty() || archiveKey == key)
            return false;
        bool shouldAttemptLoad = false;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if(_missingResourceKeys.find(key) != _missingResourceKeys.end()) {
                return false;
            }
            // A parse/decryption failure applies to the complete archive, not
            // just the requested child resource.  E-mote archives can expose
            // resources through their dedicated runtime while remaining
            // unreadable by the generic PSB loader; retrying that same parse
            // for every missing child caused hundreds of exceptions and
            // synchronous I/O stalls during dialogue playback.
            if(_failedArchives.find(archiveKey) != _failedArchives.end()) {
                _missingResourceKeys.insert(key);
                return false;
            }
            const std::string archivePrefix = archiveKey + "/";
            bool archiveHasLiveResources = false;
            for(const auto &entry : _resources) {
                if(entry.first.rfind(archivePrefix, 0) == 0) {
                    archiveHasLiveResources = true;
                    break;
                }
            }
            const bool knownResource =
                _knownResourceKeys.find(key) != _knownResourceKeys.end();
            const bool firstLoad = _loadedArchives.insert(archiveKey).second;
            shouldAttemptLoad =
                firstLoad ||
                (reloadIfLoaded && (knownResource || !archiveHasLiveResources));
            if(!shouldAttemptLoad && !knownResource) {
                _missingResourceKeys.insert(key);
            }
        }
        if(!shouldAttemptLoad)
            return false;

        if(IsVerbosePSBDebugEnabled()) {
            LOGGER->info("PSB lazy-load request: key={} archive={}", key,
                         archiveKey);
        }

        try {
            ttstr archivePath(archiveKey.c_str());
            const auto loadArchive = [&](const tjs_int seed) {
                auto candidate = std::make_unique<PSBFile>();
                candidate->setSeed(seed);
                candidate->setPreParseCallback(
                    [](std::uint8_t *data, const size_t size) {
                        return motion::ResourceManager::
                            applyEmotePSBDecryptFunc(data, size);
                    });
                return candidate->loadPSBFile(archivePath)
                    ? std::move(candidate)
                    : std::unique_ptr<PSBFile>{};
            };

            std::unique_ptr<PSBFile> psb;
            try {
                // Ordinary scenario/image PSBs must remain seedless. Encrypted
                // E-mote archives use the Motion seed only as a fallback.
                psb = loadArchive(0);
            } catch(...) {
                if(motion::ResourceManager::getEmotePSBDecryptSeed() == 0) {
                    throw;
                }
            }
            if(!psb && motion::ResourceManager::getEmotePSBDecryptSeed() != 0) {
                psb = loadArchive(
                    motion::ResourceManager::getEmotePSBDecryptSeed());
            }
            if(!psb) {
                std::lock_guard<std::mutex> lock(_mutex);
                _loadedArchives.erase(archiveKey);
                _failedArchives.insert(archiveKey);
                LOGGER->debug("PSB lazy-load failed: {}", archiveKey);
                return false;
            }
            if(IsDebugPSBArchive(archiveKey)) {
                LOGGER->info("PSB lazy-load archive: {}", archiveKey);
            } else {
                LOGGER->debug("PSB lazy-load archive: {}", archiveKey);
            }
            RegisterPSBResourcesIntoMedia(*this, *psb, archiveKey);
            // The generic registration above owns ordinary PSB resources.
            // Add only the authored motion slice mapping here; repeating the
            // full object/resource walk would replace cache entries and
            // duplicate the archive's image metadata.  The registry remains
            // data-driven and never knows a title's `truss`/`frame` names.
            registerMotionSliceResources(ttstr(archiveKey.c_str()), *psb);
            {
                std::lock_guard<std::mutex> lock(_mutex);
                _failedArchives.erase(archiveKey);
                const bool found = _resources.find(key) != _resources.end() ||
                    findBySuffixLocked(key) != _resources.end();
                if(found) {
                    _missingResourceKeys.erase(key);
                } else {
                    _missingResourceKeys.insert(key);
                }
            }
            return true;
        } catch(const std::exception &e) {
            std::lock_guard<std::mutex> lock(_mutex);
            _loadedArchives.erase(archiveKey);
            _failedArchives.insert(archiveKey);
            LOGGER->warn("PSB lazy-load error: {} ({})", e.what(), archiveKey);
            return false;
        } catch(...) {
            std::lock_guard<std::mutex> lock(_mutex);
            _loadedArchives.erase(archiveKey);
            _failedArchives.insert(archiveKey);
            LOGGER->warn("PSB lazy-load unknown error: {}", archiveKey);
            return false;
        }
    }

    void PSBMedia::GetListAt(const ttstr &name, iTVPStorageLister *lister) {
        LOGGER->error("TODO: PSBMedia GetListAt");
    }

    void PSBMedia::GetLocallyAccessibleName(ttstr &name) {
        LOGGER->error("can't get GetLocallyAccessibleName from {}!",
                      name.AsStdString());
    }

    void PSBMedia::add(const std::string &name,
                       const std::shared_ptr<PSBResource> &resource,
                       const ImageMetadata *imageMeta) {
        if(resource == nullptr) {
            return;
        }

        const auto key = canonicalizeKey(name);
        const size_t incomingSize = resource->data.size();

        std::lock_guard<std::mutex> lock(_mutex);
        _knownResourceKeys.insert(key);
        _missingResourceKeys.erase(key);
        auto it = _resources.find(key);
        if(it != _resources.end()) {
            const bool sameResource =
                it->second.resource == resource ||
                (it->second.resource != nullptr &&
                 it->second.resource->data == resource->data);
            bool sameImageInfo = imageMeta == nullptr;
            if(imageMeta != nullptr && it->second.hasImageInfo) {
                const auto &old = it->second.imageInfo;
                sameImageInfo = old.width == imageMeta->getWidth() &&
                    old.height == imageMeta->getHeight() &&
                    old.left == imageMeta->getLeft() &&
                    old.top == imageMeta->getTop() &&
                    old.opacity == imageMeta->getOpacity() &&
                    old.visible == imageMeta->getVisible() &&
                    old.layerType == imageMeta->getLayerType() &&
                    old.type == imageMeta->getType() &&
                    old.paletteType == imageMeta->getPalType() &&
                    old.spec == imageMeta->getSpec() &&
                    old.compress == imageMeta->getCompress() &&
                    old.palette == imageMeta->getPalette().data;
            }
            const bool preserveConverted =
                sameResource && sameImageInfo &&
                it->second.convertedImage != nullptr;
            auto convertedImage = preserveConverted
                ? it->second.convertedImage
                : std::shared_ptr<std::vector<uint8_t>>{};

            _bytesInUse -= it->second.sizeBytes;
            it->second.resource = resource;
            it->second.convertedImage = std::move(convertedImage);
            // The object-tree registration pass has no ImageMetadata.  Do not
            // erase metadata learned by an earlier handler pass for the same
            // resource, or its cached raw-to-BMP conversion becomes unusable.
            if(imageMeta != nullptr)
                it->second.hasImageInfo = true;
            if(imageMeta) {
                it->second.imageInfo.debugKey = key;
                it->second.imageInfo.label = imageMeta->getLabel();
                it->second.imageInfo.width = imageMeta->getWidth();
                it->second.imageInfo.height = imageMeta->getHeight();
                it->second.imageInfo.left = imageMeta->getLeft();
                it->second.imageInfo.top = imageMeta->getTop();
                it->second.imageInfo.opacity = imageMeta->getOpacity();
                it->second.imageInfo.visible = imageMeta->getVisible();
                it->second.imageInfo.layerType = imageMeta->getLayerType();
                it->second.imageInfo.type = imageMeta->getType();
                it->second.imageInfo.paletteType = imageMeta->getPalType();
                it->second.imageInfo.spec = imageMeta->getSpec();
                it->second.imageInfo.compress = imageMeta->getCompress();
                it->second.imageInfo.palette = imageMeta->getPalette().data;
            }
            it->second.sizeBytes = CalcEntryFootprint(it->second);
            _bytesInUse += it->second.sizeBytes;
            touchLocked(it->second);
        } else {
            _lru.push_front(key);
            CacheEntry entry{};
            entry.resource = resource;
            entry.hasImageInfo = imageMeta != nullptr;
            if(imageMeta) {
                entry.imageInfo.debugKey = key;
                entry.imageInfo.label = imageMeta->getLabel();
                entry.imageInfo.width = imageMeta->getWidth();
                entry.imageInfo.height = imageMeta->getHeight();
                entry.imageInfo.left = imageMeta->getLeft();
                entry.imageInfo.top = imageMeta->getTop();
                entry.imageInfo.opacity = imageMeta->getOpacity();
                entry.imageInfo.visible = imageMeta->getVisible();
                entry.imageInfo.layerType = imageMeta->getLayerType();
                entry.imageInfo.type = imageMeta->getType();
                entry.imageInfo.paletteType = imageMeta->getPalType();
                entry.imageInfo.spec = imageMeta->getSpec();
                entry.imageInfo.compress = imageMeta->getCompress();
                entry.imageInfo.palette = imageMeta->getPalette().data;
            }
            entry.sizeBytes = CalcEntryFootprint(entry);
            entry.lruIt = _lru.begin();
            size_t entrySize = entry.sizeBytes;
            _resources.emplace(key, std::move(entry));
            _bytesInUse += entrySize;
        }

        evictIfNeededLocked();
    }

    void PSBMedia::setCacheBudget(size_t maxEntries, size_t maxBytes) {
        std::lock_guard<std::mutex> lock(_mutex);
        _configuredMaxEntryCount = ClampSizeT(maxEntries, 128, 8192);
        _configuredMaxByteSize = ClampSizeT(
            maxBytes, 16ULL * 1024ULL * 1024ULL, 512ULL * 1024ULL * 1024ULL);
        _maxEntryCount = _configuredMaxEntryCount;
        _maxByteSize = _configuredMaxByteSize;
        evictIfNeededLocked();
    }

    PSBMediaCacheStats PSBMedia::getCacheStats() const {
        std::lock_guard<std::mutex> lock(_mutex);
        PSBMediaCacheStats stats;
        stats.entryCount = _resources.size();
        stats.entryLimit = _maxEntryCount;
        stats.bytesInUse = _bytesInUse;
        stats.byteLimit = _maxByteSize;
        stats.hitCount = _hitCount;
        stats.missCount = _missCount;
        return stats;
    }

    void PSBMedia::removeByPrefix(const std::string &prefix) {
        std::string normalizedPrefix = canonicalizeKey(prefix);
        if(!normalizedPrefix.empty() && normalizedPrefix.back() != '/') {
            normalizedPrefix.push_back('/');
        }

        std::lock_guard<std::mutex> lock(_mutex);
        for(auto it = _resources.begin(); it != _resources.end();) {
            if(it->first.rfind(normalizedPrefix, 0) == 0) {
                _bytesInUse -= it->second.sizeBytes;
                _lru.erase(it->second.lruIt);
                _knownResourceKeys.erase(it->first);
                _missingResourceKeys.erase(it->first);
                it = _resources.erase(it);
                continue;
            }
            ++it;
        }
        if(!normalizedPrefix.empty()) {
            std::string archiveKey = normalizedPrefix;
            archiveKey.pop_back();
            _loadedArchives.erase(archiveKey);
            _failedArchives.erase(archiveKey);

            for(auto it = _motionSliceSets.begin();
                it != _motionSliceSets.end();) {
                if(it->archiveKey == archiveKey ||
                   it->archiveKey.rfind(archiveKey + "/", 0) == 0) {
                    for(auto alias = _motionSliceAliases.begin();
                        alias != _motionSliceAliases.end();) {
                        if(alias->second.rfind(archiveKey + "/", 0) == 0) {
                            alias = _motionSliceAliases.erase(alias);
                        } else {
                            ++alias;
                        }
                    }
                    it = _motionSliceSets.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }

    void PSBMedia::clear() {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _resources.clear();
            _lru.clear();
            _bytesInUse = 0;
            _hitCount = 0;
            _missCount = 0;
            _loadedArchives.clear();
            _failedArchives.clear();
            _knownResourceKeys.clear();
            _missingResourceKeys.clear();
            _motionSliceSets.clear();
            _motionSliceAliases.clear();
            _motionSliceGeneration = 0;
        }
        // Motion archive discovery is session-scoped.  AetherKiri can launch
        // several games in one process, so never retain paths from the prior
        // project after the PSB media registry is reset.
        {
            std::lock_guard<std::mutex> lock(_motionDiscoveryMutex);
            _motionScannedRoots.clear();
            _motionKnownArchives.clear();
        }
    }

    bool PSBMedia::ensureArchiveLoaded(const std::string &archiveKey,
                                        bool reloadIfLoaded) {
        std::string archive = canonicalizeKey(archiveKey);
        while(!archive.empty() && archive.back() == '/') {
            archive.pop_back();
        }
        if(archive.empty()) {
            return false;
        }

        const std::string archivePrefix = archive + "/";
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if(!reloadIfLoaded) {
                for(const auto &[key, entry] : _resources) {
                    (void)entry;
                    if(key.rfind(archivePrefix, 0) == 0) {
                        return true;
                    }
                }
            }
        }

        return tryLazyLoadArchive(archivePrefix + "__aetherkiri_probe__",
                                  reloadIfLoaded);
    }

    std::vector<PSBMedia::ImageInfoEntry> PSBMedia::getImagesByPrefix(
        const std::string &prefix) const {
        std::string norm = canonicalizeKey(prefix);
        if(!norm.empty() && norm.back() != '/')
            norm.push_back('/');
        std::vector<ImageInfoEntry> result;
        std::lock_guard<std::mutex> lock(_mutex);
        for(const auto &[key, entry] : _resources) {
            if(key.rfind(norm, 0) != 0)
                continue;
            if(!entry.hasImageInfo)
                continue;
            result.push_back({key, entry.imageInfo});
        }
        return result;
    }

    void PSBMedia::addMotionSliceSet(std::string archiveKey,
                                     std::vector<std::string> imageKeys,
                                     int authoredWidth,
                                     int authoredHeight) {
        archiveKey = canonicalizeKey(archiveKey);
        if(archiveKey.empty() || imageKeys.empty()) {
            return;
        }

        std::vector<std::string> normalizedImages;
        normalizedImages.reserve(imageKeys.size());
        for(auto &imageKey : imageKeys) {
            imageKey = canonicalizeKey(imageKey);
            if(imageKey.empty() ||
               std::find(normalizedImages.begin(), normalizedImages.end(),
                         imageKey) != normalizedImages.end()) {
                continue;
            }
            normalizedImages.push_back(std::move(imageKey));
        }
        if(normalizedImages.empty()) {
            return;
        }

        std::lock_guard<std::mutex> lock(_mutex);
        for(auto it = _motionSliceSets.begin();
            it != _motionSliceSets.end();) {
            if(it->archiveKey != archiveKey) {
                ++it;
                continue;
            }
            for(auto alias = _motionSliceAliases.begin();
                alias != _motionSliceAliases.end();) {
                if(alias->second.rfind(archiveKey + "/", 0) == 0) {
                    alias = _motionSliceAliases.erase(alias);
                } else {
                    ++alias;
                }
            }
            it = _motionSliceSets.erase(it);
        }

        MotionSliceSet set;
        set.archiveKey = std::move(archiveKey);
        set.imageKeys = std::move(normalizedImages);
        set.authoredWidth = std::max(0, authoredWidth);
        set.authoredHeight = std::max(0, authoredHeight);
        set.generation = ++_motionSliceGeneration;
        _motionSliceSets.push_back(std::move(set));
    }

    bool PSBMedia::resolveMotionSliceStorage(const std::string &request,
                                             std::string &resolved) {
        const auto key = canonicalizeKey(request);
        if(!IsBareMotionSliceRequest(key)) {
            return false;
        }

        auto resolveRegistered = [&]() -> bool {
            std::lock_guard<std::mutex> lock(_mutex);
            if(const auto known = _motionSliceAliases.find(key);
               known != _motionSliceAliases.end()) {
                resolved = known->second;
                return true;
            }

            MotionSliceSet *selected = nullptr;
            for(auto &set : _motionSliceSets) {
                if(set.imageKeys.empty() ||
                   !SharesMotionStem(key, set.archiveKey)) {
                    continue;
                }
                if(selected == nullptr ||
                   set.generation > selected->generation) {
                    selected = &set;
                }
            }
            if(selected == nullptr ||
               selected->nextImage >= selected->imageKeys.size()) {
                return false;
            }

            const auto target = selected->imageKeys[selected->nextImage++];
            selected->assignments.emplace(key, target);
            _motionSliceAliases.emplace(key, target);
            resolved = target;
            if(LOGGER) {
                LOGGER->debug("PSB motion slice alias: {} -> {}", key,
                              resolved);
            }
            return true;
        };

        if(resolveRegistered()) {
            return true;
        }

        // A sliced layer may be the first consumer of a motion archive.  In
        // that case no PSB load has populated _motionSliceSets yet.  Discover
        // matching .mtn/.psb entries from the mounted game roots and let the
        // regular lazy loader parse/register them before trying the alias
        // assignment again.
        for(const auto &archive : discoverMotionArchives(key)) {
            if(!ensureArchiveLoaded(archive, true)) {
                continue;
            }
            if(resolveRegistered()) {
                return true;
            }
        }
        return false;
    }

    bool PSBMedia::getMotionSliceCanvasSize(const std::string &storage,
                                            int &width,
                                            int &height) const {
        std::string key = storage;
        if(key.size() >= 6) {
            std::string scheme = key.substr(0, 6);
            std::transform(scheme.begin(), scheme.end(), scheme.begin(),
                           [](const unsigned char ch) {
                               return static_cast<char>(std::tolower(ch));
                           });
            if(scheme == "psb://") {
                key.erase(0, 6);
            }
        }
        key = canonicalizeKey(key);

        std::lock_guard<std::mutex> lock(_mutex);
        for(auto it = _motionSliceSets.rbegin();
            it != _motionSliceSets.rend(); ++it) {
            if(it->authoredWidth <= 0 || it->authoredHeight <= 0 ||
               std::find(it->imageKeys.begin(), it->imageKeys.end(), key) ==
                   it->imageKeys.end()) {
                continue;
            }
            width = it->authoredWidth;
            height = it->authoredHeight;
            return true;
        }
        return false;
    }

    bool PSBMedia::getImageInfo(const std::string &key,
                                CachedImageInfo &outInfo) const {
        std::string norm = canonicalizeKey(key);
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _resources.find(norm);
        if(it == _resources.end() || !it->second.hasImageInfo)
            return false;
        outInfo = it->second.imageInfo;
        return true;
    }

    void PSBMedia::addLayerPositions(const std::string &archiveKey,
                                     std::vector<LayerPosition> positions) {
        std::lock_guard<std::mutex> lock(_mutex);
        _layerPositions[archiveKey] = std::move(positions);
    }

    std::vector<PSBMedia::LayerPosition>
    PSBMedia::getLayerPositions(const std::string &prefix) const {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _layerPositions.find(prefix);
        if(it != _layerPositions.end())
            return it->second;
        return {};
    }

    void PSBMedia::addButtonBounds(const std::string &archiveKey,
                                   std::vector<ButtonBoundInfo> bounds) {
        std::lock_guard<std::mutex> lock(_mutex);
        _buttonBoundsMap[archiveKey] = std::move(bounds);
    }

    std::vector<PSBMedia::ButtonBoundInfo>
    PSBMedia::getButtonBounds(const std::string &prefix) const {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _buttonBoundsMap.find(prefix);
        if(it != _buttonBoundsMap.end())
            return it->second;
        return {};
    }
} // namespace PSB
