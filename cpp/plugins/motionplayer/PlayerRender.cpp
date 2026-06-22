// PlayerRender.cpp — Drawing/rendering: renderToLayer, draw, frameProgress
// Split from Player.cpp for maintainability.
//
#include "PlayerInternal.h"
#include "ConfigManager/IndividualConfigManager.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

using namespace motion::internal;

namespace {

    tTJSNI_BaseLayer *resolveNativeLayer(iTJSDispatch2 *layerObject);
    std::string sampleBitmapStats(const iTVPBaseBitmap *bitmap);

    bool motionRenderProfileEnabled() {
        static const bool enabled = [] {
            const char *value = std::getenv("AETHERKIRI_MOTION_RENDER_PROFILE");
            return value && *value && std::strcmp(value, "0") != 0;
        }();
        return enabled;
    }

    std::uint64_t motionRenderProfileNowUs() {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    void renderReuseHashCombine(std::size_t &seed, std::size_t value) {
        seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    }

    std::size_t renderReuseHashFloat(float value) {
        return std::hash<int>{}(
            static_cast<int>(std::lround(static_cast<double>(value) * 1024.0)));
    }

    std::size_t renderCommandReuseSignature(
        const std::vector<motion::detail::PlayerRuntime::RenderCommand> &commands) {
        std::size_t seed = commands.size();
        for(const auto &command : commands) {
            renderReuseHashCombine(seed, std::hash<int>{}(command.nodeIndex));
            renderReuseHashCombine(seed, std::hash<std::string>{}(command.sourceKey));
            renderReuseHashCombine(seed, std::hash<int>{}(command.blendMode));
            renderReuseHashCombine(seed, std::hash<int>{}(command.opacity));
            renderReuseHashCombine(seed, std::hash<int>{}(command.itemFlags));
            renderReuseHashCombine(seed, std::hash<int>{}(command.parentNodeIndex));
            renderReuseHashCombine(seed, std::hash<int>{}(command.visibleAncestorIndex));
            renderReuseHashCombine(seed, std::hash<int>{}(command.meshDivX));
            renderReuseHashCombine(seed, std::hash<int>{}(command.meshDivY));
            renderReuseHashCombine(seed, std::hash<int>{}(command.meshType));
            renderReuseHashCombine(seed, std::hash<bool>{}(command.groupOnly));
            renderReuseHashCombine(seed, std::hash<bool>{}(command.clearEnabled));
            for(const auto value : command.packedColors) {
                renderReuseHashCombine(seed, std::hash<std::uint32_t>{}(value));
            }
            for(const auto value : command.clipRect) {
                renderReuseHashCombine(seed, std::hash<int>{}(value));
            }
            for(const auto value : command.dirtyRect) {
                renderReuseHashCombine(seed, std::hash<int>{}(value));
            }
            for(const auto value : command.worldCorners) {
                renderReuseHashCombine(seed, renderReuseHashFloat(value));
            }
            for(const auto value : command.localCorners) {
                renderReuseHashCombine(seed, renderReuseHashFloat(value));
            }
            for(const auto value : command.worldMeshPoints) {
                renderReuseHashCombine(seed, renderReuseHashFloat(value));
            }
            for(const auto value : command.localMeshPoints) {
                renderReuseHashCombine(seed, renderReuseHashFloat(value));
            }
        }
        return seed;
    }

    using PresentationRenderCacheEntry =
        motion::detail::PlayerRuntime::PresentationRenderCacheEntry;

    struct GlobalPresentationRenderCacheEntry : PresentationRenderCacheEntry {
        tTJSVariant sourceLayer;
        std::uint64_t storedUs = 0;
    };

    using GlobalPresentationRenderCache =
        std::unordered_map<iTJSDispatch2 *, GlobalPresentationRenderCacheEntry>;

    GlobalPresentationRenderCache &globalPresentationRenderCache() {
        static GlobalPresentationRenderCache cache;
        return cache;
    }

    GlobalPresentationRenderCacheEntry makeGlobalPresentationRenderCacheEntry(
        iTJSDispatch2 *sourceLayerObject,
        const std::string &motionPath,
        double frame,
        tjs_int canvasWidth,
        tjs_int canvasHeight,
        std::size_t commandSignature,
        std::uint64_t storedUs) {
        GlobalPresentationRenderCacheEntry entry;
        entry.motion = motionPath;
        entry.frame = frame;
        entry.canvasWidth = canvasWidth;
        entry.canvasHeight = canvasHeight;
        entry.commandSignature = commandSignature;
        if(sourceLayerObject) {
            entry.sourceLayer =
                tTJSVariant(sourceLayerObject, sourceLayerObject);
        }
        entry.storedUs = storedUs;
        return entry;
    }

    void invalidateGlobalPresentationRenderTarget(iTJSDispatch2 *target) {
        auto &cache = globalPresentationRenderCache();
        if(target) {
            cache.erase(target);
        } else {
            cache.clear();
        }
    }

    bool presentationRenderEntryMatches(
        const PresentationRenderCacheEntry &entry,
        const std::string &motionPath,
        double frame,
        tjs_int canvasWidth,
        tjs_int canvasHeight,
        std::size_t commandSignature) {
        return entry.motion == motionPath &&
            entry.canvasWidth == canvasWidth &&
            entry.canvasHeight == canvasHeight &&
            std::fabs(entry.frame - frame) < 0.0001 &&
            entry.commandSignature == commandSignature;
    }

    constexpr std::uint64_t kGlobalPresentationReuseTtlUs = 100000;
    constexpr std::uint64_t kGlobalPresentationCopyReuseTtlUs = 100000;

    iTJSDispatch2 *globalPresentationSourceLayerObject(
        GlobalPresentationRenderCacheEntry &entry) {
        if(entry.sourceLayer.Type() != tvtObject) {
            return nullptr;
        }
        return entry.sourceLayer.AsObjectNoAddRef();
    }

    GlobalPresentationRenderCache::iterator findGlobalPresentationRenderSource(
        GlobalPresentationRenderCache &cache,
        iTJSDispatch2 *targetLayerObject,
        const std::string &motionPath,
        double frame,
        tjs_int canvasWidth,
        tjs_int canvasHeight,
        std::size_t commandSignature,
        std::uint64_t nowUs) {
        auto it = cache.begin();
        while(it != cache.end()) {
            auto &entry = it->second;
            const bool expired =
                nowUs < entry.storedUs ||
                nowUs - entry.storedUs > kGlobalPresentationCopyReuseTtlUs;
            if(expired || !globalPresentationSourceLayerObject(entry)) {
                it = cache.erase(it);
                continue;
            }
            if(it->first != targetLayerObject &&
               presentationRenderEntryMatches(entry, motionPath, frame,
                                               canvasWidth, canvasHeight,
                                               commandSignature)) {
                return it;
            }
            ++it;
        }
        return cache.end();
    }

    bool packedColorsAreDefault(std::uint32_t c0, std::uint32_t c1,
                                std::uint32_t c2, std::uint32_t c3) {
        return c0 == 0xFF808080u && c1 == 0xFF808080u && c2 == 0xFF808080u &&
            c3 == 0xFF808080u;
    }

    bool packedColorsAreOpaqueWhite(std::uint32_t c0, std::uint32_t c1,
                                    std::uint32_t c2, std::uint32_t c3) {
        return (c0 & c1 & c2 & c3) == 0xFFFFFFFFu;
    }

    std::array<int, 4> unpackPackedRgba(std::uint32_t packedColor) {
        return {
            static_cast<int>((packedColor >> 16) & 0xFFu),
            static_cast<int>((packedColor >> 8) & 0xFFu),
            static_cast<int>(packedColor & 0xFFu),
            static_cast<int>((packedColor >> 24) & 0xFFu),
        };
    }

    std::shared_ptr<tTVPBaseBitmap> cloneBitmap32(const tTVPBaseBitmap &src) {
        auto copy = std::make_shared<tTVPBaseBitmap>(
            static_cast<tjs_uint>(src.GetWidth()),
            static_cast<tjs_uint>(src.GetHeight()), 32);
        for(tjs_uint y = 0; y < src.GetHeight(); ++y) {
            const auto *srcRow = static_cast<const std::uint8_t *>(
                src.GetScanLine(y));
            auto *dstRow = static_cast<std::uint8_t *>(
                copy->GetScanLineForWrite(y));
            std::memcpy(dstRow, srcRow,
                        static_cast<size_t>(src.GetWidth()) * 4u);
        }
        return copy;
    }

    bool bitmapLooksAlphaOnlyMask(const tTVPBaseBitmap &bitmap) {
        const int width = static_cast<int>(bitmap.GetWidth());
        const int height = static_cast<int>(bitmap.GetHeight());
        if(width <= 0 || height <= 0) {
            return false;
        }

        const size_t total =
            static_cast<size_t>(width) * static_cast<size_t>(height);
        const size_t step = std::max<size_t>(1, total / 4096u);
        size_t alphaPixels = 0;
        size_t colorPixels = 0;
        for(size_t i = 0; i < total; i += step) {
            const int y = static_cast<int>(i / static_cast<size_t>(width));
            const int x = static_cast<int>(i % static_cast<size_t>(width));
            const auto *row = static_cast<const std::uint8_t *>(
                bitmap.GetScanLine(static_cast<tjs_uint>(y)));
            const auto *pixel = row + static_cast<size_t>(x) * 4u;
            if(pixel[3] == 0) {
                continue;
            }
            ++alphaPixels;
            if(pixel[0] != 0 || pixel[1] != 0 || pixel[2] != 0) {
                ++colorPixels;
            }
        }

        return alphaPixels >= 8 && colorPixels * 32u <= alphaPixels;
    }

    bool pixelLooksWhiteMask(const std::uint8_t *pixel) {
        if(!pixel || pixel[3] == 0) {
            return false;
        }
        const int minChannel = std::min({ pixel[0], pixel[1], pixel[2] });
        const int maxChannel = std::max({ pixel[0], pixel[1], pixel[2] });
        const int spread = maxChannel - minChannel;
        if(minChannel >= 210 && spread <= 48) {
            return true;
        }

        // Some PSB logo masks are already alpha-multiplied before the
        // motion color command is applied, so their antialiased text pixels
        // are neutral gray rather than pure white.
        const int threshold = pixel[3] >= 200 ? 176 : 72;
        return maxChannel >= threshold && minChannel >= threshold &&
            spread <= 80;
    }

    bool bitmapHasTransparentPixels(const tTVPBaseBitmap &bitmap) {
        const int width = static_cast<int>(bitmap.GetWidth());
        const int height = static_cast<int>(bitmap.GetHeight());
        if(width <= 0 || height <= 0) {
            return false;
        }

        const size_t total =
            static_cast<size_t>(width) * static_cast<size_t>(height);
        const size_t step = std::max<size_t>(1, total / 4096u);
        for(size_t i = 0; i < total; i += step) {
            const int y = static_cast<int>(i / static_cast<size_t>(width));
            const int x = static_cast<int>(i % static_cast<size_t>(width));
            const auto *row = static_cast<const std::uint8_t *>(
                bitmap.GetScanLine(static_cast<tjs_uint>(y)));
            const auto *pixel = row + static_cast<size_t>(x) * 4u;
            if(pixel[3] < 250) {
                return true;
            }
        }
        return false;
    }

    bool bitmapLooksWhiteMask(const tTVPBaseBitmap &bitmap) {
        const int width = static_cast<int>(bitmap.GetWidth());
        const int height = static_cast<int>(bitmap.GetHeight());
        if(width <= 0 || height <= 0) {
            return false;
        }

        const size_t total =
            static_cast<size_t>(width) * static_cast<size_t>(height);
        const size_t step = std::max<size_t>(1, total / 4096u);
        size_t visiblePixels = 0;
        size_t whitePixels = 0;
        for(size_t i = 0; i < total; i += step) {
            const int y = static_cast<int>(i / static_cast<size_t>(width));
            const int x = static_cast<int>(i % static_cast<size_t>(width));
            const auto *row = static_cast<const std::uint8_t *>(
                bitmap.GetScanLine(static_cast<tjs_uint>(y)));
            const auto *pixel = row + static_cast<size_t>(x) * 4u;
            if(pixel[3] == 0) {
                continue;
            }
            ++visiblePixels;
            if(pixelLooksWhiteMask(pixel)) {
                ++whitePixels;
            }
        }

        return visiblePixels >= 8 && whitePixels * 4u >= visiblePixels * 3u;
    }

    bool bitmapHasWhiteMaskPixels(const tTVPBaseBitmap &bitmap) {
        const int width = static_cast<int>(bitmap.GetWidth());
        const int height = static_cast<int>(bitmap.GetHeight());
        if(width <= 0 || height <= 0) {
            return false;
        }

        const size_t total =
            static_cast<size_t>(width) * static_cast<size_t>(height);
        const size_t step = std::max<size_t>(1, total / 4096u);
        size_t whitePixels = 0;
        for(size_t i = 0; i < total; i += step) {
            const int y = static_cast<int>(i / static_cast<size_t>(width));
            const int x = static_cast<int>(i % static_cast<size_t>(width));
            const auto *row = static_cast<const std::uint8_t *>(
                bitmap.GetScanLine(static_cast<tjs_uint>(y)));
            const auto *pixel = row + static_cast<size_t>(x) * 4u;
            if(pixelLooksWhiteMask(pixel) && ++whitePixels >= 8) {
                return true;
            }
        }
        return false;
    }


    std::string renderDebugLowercase(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char ch) {
                           return static_cast<char>(std::tolower(ch));
                       });
        return value;
    }

    constexpr const char *kYuzuStartupLogoMotion = "yuzulogo.mtn";
    constexpr const char *kM2StartupLogoMotion = "m2logo.mtn";

    bool loweredPathContainsYuzuStartupLogo(const std::string &loweredPath) {
        return loweredPath.find(kYuzuStartupLogoMotion) != std::string::npos;
    }

    bool loweredPathContainsM2StartupLogo(const std::string &loweredPath) {
        return loweredPath.find(kM2StartupLogoMotion) != std::string::npos;
    }

    bool loweredPathContainsStartupLogo(const std::string &loweredPath) {
        return loweredPathContainsYuzuStartupLogo(loweredPath) ||
            loweredPathContainsM2StartupLogo(loweredPath);
    }

    bool shouldDebugTitleRender(const std::string &motionPath,
                                const std::string &sourceKey = {},
                                const std::string &origin = {}) {
        const char *enabled = std::getenv("AETHERKIRI_MOTION_DEBUG");
        if(!enabled || !*enabled || std::strcmp(enabled, "0") == 0) {
            return false;
        }
        const char *debugAll = std::getenv("AETHERKIRI_MOTION_DEBUG_ALL");
        if(debugAll && *debugAll && std::strcmp(debugAll, "0") != 0) {
            return true;
        }
        const auto motion = renderDebugLowercase(motionPath);
        const auto source = renderDebugLowercase(sourceKey);
        const auto resolved = renderDebugLowercase(origin);
        return motion.find("title.pimg") != std::string::npos ||
            motion.find("title.psb") != std::string::npos ||
            motion.find("title") != std::string::npos ||
            loweredPathContainsStartupLogo(motion) ||
            source.find("title") != std::string::npos ||
            source.find("yuzu") != std::string::npos ||
            resolved.find("title.pimg") != std::string::npos ||
            resolved.find("title.psb") != std::string::npos ||
            loweredPathContainsStartupLogo(resolved);
    }

    bool markRenderDebugLogged(const std::string &key) {
        static std::unordered_set<std::string> loggedKeys;
        return loggedKeys.insert(key).second;
    }

    bool isYuzuTitlePresentationMotion(const std::string &motionPath) {
        const auto motion = renderDebugLowercase(motionPath);
        return motion.find("title_bg") != std::string::npos ||
            motion.find("titlebg") != std::string::npos;
    }

    bool isYuzuStartupLogoMotion(const std::string &motionPath) {
        const auto motion = renderDebugLowercase(motionPath);
        return loweredPathContainsYuzuStartupLogo(motion);
    }

    bool isM2StartupLogoMotion(const std::string &motionPath) {
        const auto motion = renderDebugLowercase(motionPath);
        return loweredPathContainsM2StartupLogo(motion);
    }

    bool isYuzuLogoPresentationMotion(const std::string &motionPath) {
        return isM2StartupLogoMotion(motionPath) ||
            isYuzuStartupLogoMotion(motionPath);
    }

    std::array<int, 4> neutralStartupLogoTint(
        const std::string &motionPath) {
        if(isYuzuStartupLogoMotion(motionPath)) {
            return { 54, 158, 248, 255 };
        }
        return { 255, 255, 255, 255 };
    }

    std::array<int, 4> yuzuStartupLogoDisplayTextTint(
        const std::string &motionPath) {
        if(isYuzuStartupLogoMotion(motionPath)) {
            return { 248, 158, 54, 255 };
        }
        return { 255, 255, 255, 255 };
    }

    bool isYuzuLogoTextMaskSource(const std::string &motionPath,
                                  const std::string &sourceKey) {
        if(!isYuzuStartupLogoMotion(motionPath)) {
            return false;
        }
        const auto source = renderDebugLowercase(sourceKey);
        return source.find("moji") != std::string::npos ||
            source.find("text") != std::string::npos ||
            source.find("letter") != std::string::npos;
    }

    bool isYuzuLogoCompositeSource(const std::string &motionPath,
                                   const std::string &sourceKey) {
        if(!isYuzuStartupLogoMotion(motionPath)) {
            return false;
        }
        const auto source = renderDebugLowercase(sourceKey);
        return source.find("yuzu_logo") != std::string::npos;
    }

    bool motionSourcePixelsAreBGRA(const std::string &motionPath,
                                   bool decodedPixelsAreBgra) {
        if(decodedPixelsAreBgra) {
            return true;
        }

        const auto motion = renderDebugLowercase(motionPath);
        if(loweredPathContainsStartupLogo(motion)) {
            return false;
        }

        return true;
    }

    bool isYuzuPresentationMotion(const std::string &motionPath) {
        return isYuzuTitlePresentationMotion(motionPath) ||
            isYuzuLogoPresentationMotion(motionPath);
    }

    bool isYuzuTitleWhiteUtilityLayer(const std::string &motionPath,
                                      const std::string &nodeLabel,
                                      const std::string &sourceKey) {
        if(!isYuzuTitlePresentationMotion(motionPath)) {
            return false;
        }
        const auto source = renderDebugLowercase(sourceKey);
        if(source != "src/title/white" &&
           source.find("/title/white") == std::string::npos &&
           source.find("/white") == std::string::npos) {
            return false;
        }
        return true;
    }

    bool isYuzuTitlePositionLayer(const std::string &nodeLabel,
                                  const std::string &sourceKey) {
        const auto source = renderDebugLowercase(sourceKey);
        if(source == "src/title/pos" ||
           source == "src/title/pos2" ||
           source == "src/title/pos3" ||
           source == "src/title/pos4" ||
           source.find("/title/pos") != std::string::npos) {
            return true;
        }
        const auto label = renderDebugLowercase(nodeLabel);
        return label == "title_charall" &&
            source.find("title") != std::string::npos &&
            source.find("pos") != std::string::npos;
    }

    bool isYuzuTitleBackgroundLayer(const std::string &nodeLabel,
                                    const std::string &sourceKey) {
        const auto label = renderDebugLowercase(nodeLabel);
        if(label == "bg") {
            return true;
        }
        const auto source = renderDebugLowercase(sourceKey);
        return source.find("bg_title") != std::string::npos ||
            source == "src/title/bg" ||
            source == "src/title/bg1" ||
            source == "src/title/bg2" ||
            source.find("/title/bg") != std::string::npos;
    }

    bool isYuzuTitleCharacterOrLogoLayer(const std::string &nodeLabel,
                                         const std::string &sourceKey) {
        const auto source = renderDebugLowercase(sourceKey);
        if(source.find("src/title/logo") != std::string::npos ||
           source.find("/title/logo") != std::string::npos ||
           source.find("src/title/ch") != std::string::npos ||
           source.find("/title/ch") != std::string::npos ||
           source.find("title2_ch") != std::string::npos ||
           source.find("title_char") != std::string::npos) {
            return true;
        }
        const auto label = renderDebugLowercase(nodeLabel);
        return label.find("title_char") != std::string::npos;
    }

    bool isYuzuTitleStandaloneCharacterLayer(const std::string &nodeLabel,
                                             const std::string &sourceKey) {
        if(isYuzuTitlePositionLayer(nodeLabel, sourceKey)) {
            return false;
        }
        const auto source = renderDebugLowercase(sourceKey);
        if(source.find("src/title/ch") != std::string::npos ||
           source.find("/title/ch") != std::string::npos ||
           source.find("title2_ch") != std::string::npos) {
            return true;
        }
        const auto label = renderDebugLowercase(nodeLabel);
        if(label.rfind("ch", 0) != 0 || label.size() <= 2) {
            return false;
        }
        return std::all_of(
            label.begin() + 2, label.end(),
            [](char ch) {
                return std::isdigit(static_cast<unsigned char>(ch));
            });
    }

    bool isYuzuTitleSyntheticIntroLayer(
        const motion::detail::PlayerRuntime::PreparedRenderItem &entry) {
        const auto label = renderDebugLowercase(entry.nodeLabel);
        constexpr const char *kSuffix = "_intro";
        constexpr size_t kSuffixLength = 6;
        return label.size() > kSuffixLength &&
            label.compare(label.size() - kSuffixLength, kSuffixLength,
                          kSuffix) == 0 &&
            isYuzuTitleStandaloneCharacterLayer(entry.nodeLabel,
                                                entry.sourceKey);
    }

    bool isYuzuTitleClipAnimatedCharacterLayer(
        const motion::detail::PlayerRuntime::PreparedRenderItem &entry) {
        return entry.visibleAncestorIndex >= 0 &&
            isYuzuTitleStandaloneCharacterLayer(entry.nodeLabel,
                                                entry.sourceKey);
    }

    bool isYuzuTitleRenderablePresentationLayer(
        const motion::detail::PlayerRuntime::PreparedRenderItem &entry) {
        if(!entry.drawFlag || entry.skipFlag0 || entry.skipFlag1 ||
           entry.opacity <= 0) {
            return false;
        }
        if(isYuzuTitleWhiteUtilityLayer("title_bg", entry.nodeLabel,
                                        entry.sourceKey)) {
            return false;
        }
        if(isYuzuTitleBackgroundLayer(entry.nodeLabel, entry.sourceKey) ||
           isYuzuTitlePositionLayer(entry.nodeLabel, entry.sourceKey)) {
            return true;
        }
        return isYuzuTitleCharacterOrLogoLayer(entry.nodeLabel,
                                               entry.sourceKey);
    }

    bool hasYuzuTitleRenderablePresentationFrame(
        const motion::detail::PlayerRuntime &runtime) {
        return std::any_of(
            runtime.preparedRenderItems.begin(),
            runtime.preparedRenderItems.end(),
            [](const auto &entry) {
                return isYuzuTitleRenderablePresentationLayer(entry);
            });
    }

    bool hasYuzuTitleFinalPresentationFrame(
        const motion::detail::PlayerRuntime &runtime) {
        return std::any_of(
            runtime.preparedRenderItems.begin(),
            runtime.preparedRenderItems.end(),
            [](const auto &entry) {
                return entry.drawFlag && !entry.skipFlag0 && !entry.skipFlag1 &&
                    entry.opacity > 0 &&
                    (isYuzuTitlePositionLayer(entry.nodeLabel,
                                              entry.sourceKey) ||
                     isYuzuTitleCharacterOrLogoLayer(entry.nodeLabel,
                                                     entry.sourceKey));
            });
    }

    void logYuzuTitlePreparedSummary(
        const motion::detail::PlayerRuntime &runtime,
        const std::string &motionPath,
        double frameTick) {
        if(!LOGGER || !shouldDebugTitleRender(motionPath)) {
            return;
        }
        const int frameBucket =
            static_cast<int>(std::floor(frameTick / 30.0));
        if(!markRenderDebugLogged(fmt::format(
               "title-prepared-summary|{}|{}", motionPath, frameBucket))) {
            return;
        }

        std::ostringstream summary;
        int logged = 0;
        for(const auto &entry : runtime.preparedRenderItems) {
            const auto source = renderDebugLowercase(entry.sourceKey);
            if(source.find("src/title/") == std::string::npos &&
               source.find("/title/") == std::string::npos &&
               source.find("title") == std::string::npos &&
               source.find("src/yuzu/") == std::string::npos &&
               source.find("/yuzu/") == std::string::npos &&
               source.find("logo") == std::string::npos) {
                continue;
            }
            if(logged++ > 0) {
                summary << ";";
            }
            summary << entry.nodeIndex << ":"
                    << (entry.nodeLabel.empty() ? std::string("<none>")
                                                : entry.nodeLabel)
                    << ":src=" << (entry.sourceKey.empty() ? std::string("<none>")
                                                           : entry.sourceKey)
                    << ":draw=" << (entry.drawFlag ? 1 : 0)
                    << ":skip=" << (entry.skipFlag0 ? 1 : 0)
                    << (entry.skipFlag1 ? 1 : 0)
                    << ":group=" << (entry.groupOnly ? 1 : 0)
                    << ":opa=" << entry.opacity
                    << ":blend=" << entry.blendMode
                    << ":mesh=" << entry.meshType << "/" << entry.meshDivX
                    << "x" << entry.meshDivY
                    << ":color=[" << fmt::format(
                           "0x{:08x},0x{:08x},0x{:08x},0x{:08x}",
                           entry.packedColors[0], entry.packedColors[1],
                           entry.packedColors[2], entry.packedColors[3])
                    << "]"
                    << ":paint=[" << entry.paintBox[0] << ","
                    << entry.paintBox[1] << "," << entry.paintBox[2]
                    << "," << entry.paintBox[3] << "]";
            if(logged >= 24) {
                break;
            }
        }
        LOGGER->info(
            "motion title prepared summary: motion={} frameTick={:.2f} items={} renderable={} entries=[{}]",
            motionPath, frameTick, runtime.preparedRenderItems.size(),
            hasYuzuTitleRenderablePresentationFrame(runtime) ? 1 : 0,
            summary.str());
    }

    bool validRenderPaintBox(const std::array<float, 4> &box) {
        return std::isfinite(box[0]) && std::isfinite(box[1]) &&
            std::isfinite(box[2]) && std::isfinite(box[3]) &&
            box[2] > box[0] && box[3] > box[1];
    }

    std::array<float, 4> boundsFromRenderCorners(
        const std::array<float, 8> &corners) {
        std::array<float, 4> bounds{
            corners[0], corners[1], corners[0], corners[1]
        };
        for(size_t i = 0; i + 1 < corners.size(); i += 2) {
            if(!std::isfinite(corners[i]) || !std::isfinite(corners[i + 1])) {
                return {0.0f, 0.0f, 0.0f, 0.0f};
            }
            bounds[0] = std::min(bounds[0], corners[i]);
            bounds[1] = std::min(bounds[1], corners[i + 1]);
            bounds[2] = std::max(bounds[2], corners[i]);
            bounds[3] = std::max(bounds[3], corners[i + 1]);
        }
        return bounds;
    }

    bool isCenteredGameMotion(const std::string &motionPath) {
        const auto motion = renderDebugLowercase(motionPath);
        const auto slash = motion.find_last_of("/\\");
        const std::string name =
            slash == std::string::npos ? motion : motion.substr(slash + 1);
        if(name.rfind("sd", 0) != 0 || name.size() < 4) {
            return false;
        }
        return std::isdigit(static_cast<unsigned char>(name[2]));
    }

    double readMotionResolutionValue(const tTJSVariant &value) {
        if(value.Type() == tvtInteger || value.Type() == tvtReal) {
            return value.AsReal();
        }
        if(value.Type() != tvtObject || value.AsObjectNoAddRef() == nullptr) {
            return 0.0;
        }
        tTJSVariant resolution;
        if(getObjectProperty(value, TJS_W("resolution"), resolution) &&
           resolution.Type() != tvtVoid) {
            return resolution.AsReal();
        }
        return 0.0;
    }

    double resolveCenteredGameMotionResolution(
        double explicitResolution,
        const tTJSVariant &tags,
        const tTJSVariant &metadata,
        const std::string &motionPath) {
        if(std::isfinite(explicitResolution) && explicitResolution > 0.0) {
            return explicitResolution;
        }
        const double tagResolution = readMotionResolutionValue(tags);
        if(std::isfinite(tagResolution) && tagResolution > 0.0) {
            return tagResolution;
        }
        const double metadataResolution = readMotionResolutionValue(metadata);
        if(std::isfinite(metadataResolution) && metadataResolution > 0.0) {
            return metadataResolution;
        }
        return isCenteredGameMotion(motionPath) ? 133.33 : 100.0;
    }

    void translatePreparedRenderItem(
        motion::detail::PlayerRuntime::PreparedRenderItem &entry,
        float dx,
        float dy) {
        auto translatePointArray = [dx, dy](auto &points) {
            for(size_t i = 0; i + 1 < points.size(); i += 2) {
                points[i] += dx;
                points[i + 1] += dy;
            }
        };
        translatePointArray(entry.corners);
        translatePointArray(entry.meshPoints);
        if(validRenderPaintBox(entry.paintBox)) {
            entry.paintBox[0] += dx;
            entry.paintBox[1] += dy;
            entry.paintBox[2] += dx;
            entry.paintBox[3] += dy;
        }
        if(entry.hasViewport && validRenderPaintBox(entry.viewport)) {
            entry.viewport[0] += dx;
            entry.viewport[1] += dy;
            entry.viewport[2] += dx;
            entry.viewport[3] += dy;
        }
    }

    void scalePreparedRenderItem(
        motion::detail::PlayerRuntime::PreparedRenderItem &entry,
        float originX,
        float originY,
        float scale) {
        auto scalePointArray = [originX, originY, scale](auto &points) {
            for(size_t i = 0; i + 1 < points.size(); i += 2) {
                points[i] = originX + (points[i] - originX) * scale;
                points[i + 1] = originY + (points[i + 1] - originY) * scale;
            }
        };
        scalePointArray(entry.corners);
        scalePointArray(entry.meshPoints);
        if(validRenderPaintBox(entry.paintBox)) {
            entry.paintBox[0] = originX + (entry.paintBox[0] - originX) * scale;
            entry.paintBox[1] = originY + (entry.paintBox[1] - originY) * scale;
            entry.paintBox[2] = originX + (entry.paintBox[2] - originX) * scale;
            entry.paintBox[3] = originY + (entry.paintBox[3] - originY) * scale;
        }
        if(entry.hasViewport && validRenderPaintBox(entry.viewport)) {
            entry.viewport[0] = originX + (entry.viewport[0] - originX) * scale;
            entry.viewport[1] = originY + (entry.viewport[1] - originY) * scale;
            entry.viewport[2] = originX + (entry.viewport[2] - originX) * scale;
            entry.viewport[3] = originY + (entry.viewport[3] - originY) * scale;
        }
    }

    void adjustPreparedRenderItemsForCenteredGameMotion(
        motion::detail::PlayerRuntime &runtime,
        const std::string &motionPath,
        tjs_int canvasWidth,
        tjs_int canvasHeight,
        double configuredResolution) {
        if(!isCenteredGameMotion(motionPath) ||
           canvasWidth <= 0 || canvasHeight <= 0 ||
           runtime.preparedRenderItems.empty()) {
            return;
        }

        std::array<float, 4> bounds{0.0f, 0.0f, 0.0f, 0.0f};
        bool haveBounds = false;
        for(const auto &entry : runtime.preparedRenderItems) {
            if(!entry.drawFlag || entry.skipFlag0 || entry.skipFlag1 ||
               entry.opacity <= 0 || !entry.hasOwnSource) {
                continue;
            }
            auto entryBounds = boundsFromRenderCorners(entry.corners);
            if(!validRenderPaintBox(entryBounds)) {
                entryBounds = entry.paintBox;
            }
            if(!validRenderPaintBox(entryBounds)) {
                continue;
            }
            if(!haveBounds) {
                bounds = entryBounds;
                haveBounds = true;
            } else {
                bounds[0] = std::min(bounds[0], entryBounds[0]);
                bounds[1] = std::min(bounds[1], entryBounds[1]);
                bounds[2] = std::max(bounds[2], entryBounds[2]);
                bounds[3] = std::max(bounds[3], entryBounds[3]);
            }
        }
        if(!haveBounds || !validRenderPaintBox(bounds)) {
            if(LOGGER && shouldDebugTitleRender(motionPath) &&
               markRenderDebugLogged("centered-game-motion-no-bounds:" + motionPath)) {
                LOGGER->info(
                    "motion centered game presentation skipped: motion={} reason=no_bounds canvas={}x{} preparedItems={}",
                    motionPath, canvasWidth, canvasHeight,
                    runtime.preparedRenderItems.size());
            }
            return;
        }

        const float width = bounds[2] - bounds[0];
        const float height = bounds[3] - bounds[1];
        const float canvasW = static_cast<float>(canvasWidth);
        const float canvasH = static_cast<float>(canvasHeight);
        if(LOGGER && shouldDebugTitleRender(motionPath) &&
           markRenderDebugLogged("centered-game-motion-candidate:" + motionPath)) {
            LOGGER->info(
                "motion centered game presentation candidate: motion={} bounds=[{:.1f},{:.1f},{:.1f},{:.1f}] size={:.1f}x{:.1f} canvas={}x{} preparedItems={}",
                motionPath, bounds[0], bounds[1], bounds[2], bounds[3],
                width, height, canvasWidth, canvasHeight,
                runtime.preparedRenderItems.size());
        }
        if(width < canvasW * 0.08f || height < canvasH * 0.08f ||
           width > canvasW * 0.90f || height > canvasH * 0.90f) {
            return;
        }

        const float currentCenterX = (bounds[0] + bounds[2]) * 0.5f;
        const float currentCenterY = (bounds[1] + bounds[3]) * 0.5f;
        const float originToleranceX = std::max(16.0f, canvasW * 0.02f);
        const float originToleranceY = std::max(16.0f, canvasH * 0.02f);
        const bool topLeftOrigin =
            std::fabs(bounds[0]) <= originToleranceX &&
            std::fabs(bounds[1]) <= originToleranceY;
        const bool centeredOrigin =
            bounds[0] < 0.0f && bounds[1] < 0.0f &&
            bounds[2] > 0.0f && bounds[3] > 0.0f &&
            std::fabs(currentCenterX) <= originToleranceX &&
            std::fabs(currentCenterY) <= originToleranceY;
        if(!topLeftOrigin && !centeredOrigin) {
            return;
        }

        const float motionScale =
            (std::isfinite(configuredResolution) && configuredResolution > 0.0)
                ? static_cast<float>(100.0 / configuredResolution)
                : 1.0f;
        float scaledCenterX = currentCenterX;
        float scaledCenterY = currentCenterY;
        std::array<float, 4> scaledBounds = bounds;
        if(std::isfinite(motionScale) && motionScale > 0.0f &&
           std::fabs(motionScale - 1.0f) >= 0.0001f) {
            for(auto &entry : runtime.preparedRenderItems) {
                scalePreparedRenderItem(entry, currentCenterX, currentCenterY,
                                        motionScale);
            }
            scaledBounds[0] = currentCenterX +
                (bounds[0] - currentCenterX) * motionScale;
            scaledBounds[1] = currentCenterY +
                (bounds[1] - currentCenterY) * motionScale;
            scaledBounds[2] = currentCenterX +
                (bounds[2] - currentCenterX) * motionScale;
            scaledBounds[3] = currentCenterY +
                (bounds[3] - currentCenterY) * motionScale;
            scaledCenterX = (scaledBounds[0] + scaledBounds[2]) * 0.5f;
            scaledCenterY = (scaledBounds[1] + scaledBounds[3]) * 0.5f;
        }

        const float targetCenterX = canvasW * 0.5f;
        const float targetCenterY = canvasH * 0.5f;
        const float translateX = std::round(targetCenterX - scaledCenterX);
        const float translateY = std::round(targetCenterY - scaledCenterY);
        if(std::fabs(translateX) < 0.5f && std::fabs(translateY) < 0.5f) {
            return;
        }

        for(auto &entry : runtime.preparedRenderItems) {
            translatePreparedRenderItem(entry, translateX, translateY);
        }
        if(LOGGER && shouldDebugTitleRender(motionPath) &&
           markRenderDebugLogged("centered-game-motion:" + motionPath)) {
            LOGGER->info(
                "motion centered game presentation: motion={} bounds=[{:.1f},{:.1f},{:.1f},{:.1f}] scaledBounds=[{:.1f},{:.1f},{:.1f},{:.1f}] canvas={}x{} resolution={:.2f} scale={:.4f} translate={:.1f},{:.1f}",
                motionPath, bounds[0], bounds[1], bounds[2], bounds[3],
                scaledBounds[0], scaledBounds[1], scaledBounds[2],
                scaledBounds[3], canvasWidth, canvasHeight,
                configuredResolution, motionScale, translateX, translateY);
        }
    }

    void adjustPreparedRenderItemsForYuzuPresentation(
        motion::detail::PlayerRuntime &runtime,
        const std::string &motionPath,
        tjs_int canvasWidth,
        tjs_int canvasHeight) {
        if(!isYuzuPresentationMotion(motionPath) ||
           canvasWidth <= 0 || canvasHeight <= 0 ||
           runtime.preparedRenderItems.empty()) {
            return;
        }
        const bool isTitleMotion = isYuzuTitlePresentationMotion(motionPath);
        const bool isLogoMotion = isYuzuLogoPresentationMotion(motionPath);

        if(isTitleMotion) {
            const bool hasSyntheticIntroLayer = std::any_of(
                runtime.preparedRenderItems.begin(),
                runtime.preparedRenderItems.end(),
                [](const auto &entry) {
                    return entry.drawFlag && !entry.skipFlag0 &&
                        !entry.skipFlag1 && entry.opacity > 0 &&
                        isYuzuTitleSyntheticIntroLayer(entry);
                });
            if(hasSyntheticIntroLayer) {
                int suppressedClipCharacters = 0;
                for(auto &entry : runtime.preparedRenderItems) {
                    if(entry.drawFlag && !entry.skipFlag0 && !entry.skipFlag1 &&
                       entry.opacity > 0 &&
                       isYuzuTitleClipAnimatedCharacterLayer(entry)) {
                        entry.skipFlag0 = true;
                        ++suppressedClipCharacters;
                    }
                }
                if(suppressedClipCharacters > 0 && LOGGER &&
                   shouldDebugTitleRender(motionPath) &&
                   markRenderDebugLogged(fmt::format(
                       "yuzu-title-clip-character-suppressed:{}:{}",
                       motionPath, suppressedClipCharacters))) {
                    LOGGER->info(
                        "motion title clip character layer suppressed: motion={} suppressedClipCharacters={}",
                        motionPath, suppressedClipCharacters);
                }
            }

            const bool hasActiveCompositeCharacterLayer = std::any_of(
                runtime.preparedRenderItems.begin(),
                runtime.preparedRenderItems.end(),
                [](const auto &entry) {
                    return entry.drawFlag && !entry.skipFlag0 &&
                        !entry.skipFlag1 && entry.opacity >= 250 &&
                        isYuzuTitlePositionLayer(entry.nodeLabel,
                                                 entry.sourceKey);
                });
            if(hasActiveCompositeCharacterLayer) {
                int suppressed = 0;
                for(auto &entry : runtime.preparedRenderItems) {
                    if(entry.drawFlag && !entry.skipFlag0 &&
                       !entry.skipFlag1 && entry.opacity > 0 &&
                       isYuzuTitleSyntheticIntroLayer(entry)) {
                        entry.skipFlag0 = true;
                        ++suppressed;
                    }
                }
                if(suppressed > 0 && LOGGER &&
                   shouldDebugTitleRender(motionPath) &&
                   markRenderDebugLogged(fmt::format(
                       "yuzu-title-composite-suppressed:{}:{}",
                       motionPath, suppressed))) {
                    LOGGER->info(
                        "motion title composite character layer active: motion={} suppressedSyntheticIntroCharacters={}",
                        motionPath, suppressed);
                }
            }
        }

        bool usesCenteredPresentationOrigin = isLogoMotion || isTitleMotion;
        float centeredPresentationTranslateX =
            static_cast<float>(canvasWidth) * 0.5f;
        float centeredPresentationTranslateY =
            static_cast<float>(canvasHeight) * 0.5f;
        bool centeredOriginFromReference = false;
        if(isTitleMotion &&
           runtime.yuzuPresentationCenteredOriginConfirmed) {
            usesCenteredPresentationOrigin = true;
            centeredOriginFromReference = true;
            centeredPresentationTranslateX =
                runtime.yuzuPresentationTranslateX;
            centeredPresentationTranslateY =
                runtime.yuzuPresentationTranslateY;
        }
        auto considerCenteredOrigin =
            [&](const motion::detail::PlayerRuntime::PreparedRenderItem &entry,
                int referenceKind) {
                if(!entry.drawFlag || entry.opacity <= 0 ||
                   isYuzuTitleWhiteUtilityLayer(motionPath, entry.nodeLabel,
                                                entry.sourceKey)) {
                    return;
                }
                const auto source = renderDebugLowercase(entry.sourceKey);
                const bool isTitlePositionReference =
                    isTitleMotion &&
                    isYuzuTitlePositionLayer(entry.nodeLabel, entry.sourceKey);
                if(usesCenteredPresentationOrigin &&
                   (!isTitlePositionReference || centeredOriginFromReference)) {
                    return;
                }
                const bool isLogoBackdrop =
                    isLogoMotion &&
                    (source == "src/logo/icon50" ||
                     source.find("/logo/icon/icon50") != std::string::npos);
                switch(referenceKind) {
                    case 0:
                        if(isTitleMotion &&
                           !isTitlePositionReference &&
                           (!isYuzuTitleBackgroundLayer(entry.nodeLabel,
                                                        entry.sourceKey) ||
                            source.find("_bottom") != std::string::npos ||
                            source.find("_top") != std::string::npos)) {
                            return;
                        }
                        break;
                    case 1:
                        if(!isTitleMotion ||
                           (!isTitlePositionReference &&
                            source != "src/title/pos2")) {
                            return;
                        }
                        break;
                    default:
                        break;
                }

                auto referenceBox = boundsFromRenderCorners(entry.corners);
                if(!validRenderPaintBox(referenceBox)) {
                    referenceBox = entry.paintBox;
                }
                if(!validRenderPaintBox(referenceBox)) {
                    return;
                }
                const float width = referenceBox[2] - referenceBox[0];
                const float height = referenceBox[3] - referenceBox[1];
                const float centerX = (referenceBox[0] + referenceBox[2]) * 0.5f;
                const float centerY = (referenceBox[1] + referenceBox[3]) * 0.5f;
                const float widthRatio =
                    width / static_cast<float>(canvasWidth);
                const float heightRatio =
                    height / static_cast<float>(canvasHeight);
                const float maxCenteredWidthRatio = isLogoMotion ? 1.55f : 1.25f;
                const float maxCenteredHeightRatio = isLogoMotion ? 1.55f : 1.25f;
                const float minCenteredHeightRatio = isLogoMotion ? 0.70f : 0.75f;
                const bool crossesOrigin =
                    referenceBox[0] < 0.0f && referenceBox[1] < 0.0f &&
                    referenceBox[2] > 0.0f && referenceBox[3] > 0.0f;
                if(isLogoBackdrop && crossesOrigin) {
                    usesCenteredPresentationOrigin = true;
                    return;
                }
                if(isTitlePositionReference && crossesOrigin &&
                   widthRatio >= 0.75f &&
                   heightRatio >= 0.55f &&
                   widthRatio <= maxCenteredWidthRatio &&
                   heightRatio <= maxCenteredHeightRatio &&
                   std::fabs(centerX) <=
                       static_cast<float>(canvasWidth) * 0.1f &&
                   std::fabs(centerY) <=
                       static_cast<float>(canvasHeight) * 0.1f) {
                    usesCenteredPresentationOrigin = true;
                    centeredOriginFromReference = true;
                    centeredPresentationTranslateX =
                        std::round(-referenceBox[0]);
                    centeredPresentationTranslateY =
                        std::round(-referenceBox[1]);
                    return;
                }
                usesCenteredPresentationOrigin =
                    crossesOrigin &&
                    referenceBox[2] > 0.0f && referenceBox[3] > 0.0f &&
                    widthRatio >= 0.75f &&
                    (heightRatio >= minCenteredHeightRatio ||
                     (isLogoBackdrop && heightRatio >= 0.55f)) &&
                    widthRatio <= maxCenteredWidthRatio &&
                    heightRatio <= maxCenteredHeightRatio &&
                    std::fabs(centerX) <= static_cast<float>(canvasWidth) * 0.1f &&
                    std::fabs(centerY) <= static_cast<float>(canvasHeight) * 0.1f;
            };

        for(const auto &entry : runtime.preparedRenderItems) {
            considerCenteredOrigin(entry, 0);
        }
        if(!usesCenteredPresentationOrigin) {
            for(const auto &entry : runtime.preparedRenderItems) {
                considerCenteredOrigin(entry, 1);
            }
        }
        if(usesCenteredPresentationOrigin) {
            const float translateX = centeredPresentationTranslateX;
            const float translateY = centeredPresentationTranslateY;
            if(isTitleMotion && centeredOriginFromReference) {
                runtime.yuzuPresentationCenteredOriginConfirmed = true;
                runtime.yuzuPresentationTranslateX = translateX;
                runtime.yuzuPresentationTranslateY = translateY;
            }
            for(auto &entry : runtime.preparedRenderItems) {
                translatePreparedRenderItem(entry, translateX, translateY);
            }
            if(LOGGER && shouldDebugTitleRender(motionPath) &&
               markRenderDebugLogged("yuzu-presentation-origin:" + motionPath)) {
                LOGGER->info(
                    "motion presentation origin: motion={} centered=1 translate={:.1f},{:.1f} canvas={}x{} reference={}",
                    motionPath, translateX, translateY, canvasWidth,
                    canvasHeight, centeredOriginFromReference ? 1 : 0);
            }
        }

        float refWidth = 0.0f;
        float refHeight = 0.0f;
        float refArea = 0.0f;
        auto considerReference =
            [&](const motion::detail::PlayerRuntime::PreparedRenderItem &entry,
                int referenceKind) {
                if(!entry.drawFlag || entry.opacity <= 0 ||
                   isYuzuTitleWhiteUtilityLayer(motionPath, entry.nodeLabel,
                                                entry.sourceKey)) {
                    return;
                }
                const auto source = renderDebugLowercase(entry.sourceKey);
                switch(referenceKind) {
                    case 0:
                        if(!isTitleMotion || source != "src/title/pos2") {
                            return;
                        }
                        break;
                    case 1:
                        if(!isTitleMotion ||
                           !isYuzuTitleBackgroundLayer(entry.nodeLabel,
                                                       entry.sourceKey) ||
                           source.find("_bottom") != std::string::npos ||
                           source.find("_top") != std::string::npos) {
                            return;
                        }
                        break;
                    default:
                        break;
                }

                auto referenceBox = boundsFromRenderCorners(entry.corners);
                if(!validRenderPaintBox(referenceBox) &&
                   entry.hasViewport && validRenderPaintBox(entry.viewport)) {
                    referenceBox = entry.viewport;
                }
                if(!validRenderPaintBox(referenceBox) &&
                   validRenderPaintBox(entry.paintBox)) {
                    referenceBox = entry.paintBox;
                }
                if(!validRenderPaintBox(referenceBox)) {
                    return;
                }
                const float width = referenceBox[2] - referenceBox[0];
                const float height = referenceBox[3] - referenceBox[1];
                const float area = width * height;
                if(area > refArea) {
                    refArea = area;
                    refWidth = width;
                    refHeight = height;
                }
            };

        for(const auto &entry : runtime.preparedRenderItems) {
            considerReference(entry, 0);
        }
        if(refWidth <= 0.0f || refHeight <= 0.0f) {
            for(const auto &entry : runtime.preparedRenderItems) {
                considerReference(entry, 1);
            }
        }
        if(refWidth <= 0.0f || refHeight <= 0.0f) {
            for(const auto &entry : runtime.preparedRenderItems) {
                considerReference(entry, 2);
            }
        }
        if(refWidth <= 0.0f || refHeight <= 0.0f) {
            return;
        }

        const float scaleX = static_cast<float>(canvasWidth) / refWidth;
        const float scaleY = static_cast<float>(canvasHeight) / refHeight;
        if(LOGGER && shouldDebugTitleRender(motionPath) &&
           markRenderDebugLogged("yuzu-presentation-scale-candidate:" +
                                 motionPath)) {
            LOGGER->info(
                "motion presentation scale candidate: motion={} ref={:.1f}x{:.1f} canvas={}x{} scale={:.3f},{:.3f}",
                motionPath, refWidth, refHeight, canvasWidth, canvasHeight,
                scaleX, scaleY);
        }
        if(scaleX < 1.25f || scaleY < 1.25f ||
           scaleX > 3.25f || scaleY > 3.25f) {
            return;
        }

        const bool scaleAroundCanvasCenter =
            isLogoMotion && usesCenteredPresentationOrigin;
        const float scaleOriginX =
            scaleAroundCanvasCenter ? static_cast<float>(canvasWidth) * 0.5f
                                    : 0.0f;
        const float scaleOriginY =
            scaleAroundCanvasCenter ? static_cast<float>(canvasHeight) * 0.5f
                                    : 0.0f;
        auto scalePointArray = [&](auto &points, float sx, float sy) {
            for(size_t i = 0; i + 1 < points.size(); i += 2) {
                points[i] = scaleOriginX + (points[i] - scaleOriginX) * sx;
                points[i + 1] =
                    scaleOriginY + (points[i + 1] - scaleOriginY) * sy;
            }
        };
        auto shouldScaleBox = [&](const std::array<float, 4> &box) {
            if(!validRenderPaintBox(box)) {
                return false;
            }
            const float width = box[2] - box[0];
            const float height = box[3] - box[1];
            return width <= refWidth * 1.25f &&
                height <= refHeight * 1.25f;
        };

        for(auto &entry : runtime.preparedRenderItems) {
            const bool scaleGeometry =
                shouldScaleBox(boundsFromRenderCorners(entry.corners));
            if(scaleGeometry) {
                scalePointArray(entry.corners, scaleX, scaleY);
                scalePointArray(entry.meshPoints, scaleX, scaleY);
            }
            if(shouldScaleBox(entry.paintBox)) {
                entry.paintBox[0] =
                    scaleOriginX + (entry.paintBox[0] - scaleOriginX) * scaleX;
                entry.paintBox[1] =
                    scaleOriginY + (entry.paintBox[1] - scaleOriginY) * scaleY;
                entry.paintBox[2] =
                    scaleOriginX + (entry.paintBox[2] - scaleOriginX) * scaleX;
                entry.paintBox[3] =
                    scaleOriginY + (entry.paintBox[3] - scaleOriginY) * scaleY;
            }
            if(entry.hasViewport && shouldScaleBox(entry.viewport)) {
                entry.viewport[0] =
                    scaleOriginX + (entry.viewport[0] - scaleOriginX) * scaleX;
                entry.viewport[1] =
                    scaleOriginY + (entry.viewport[1] - scaleOriginY) * scaleY;
                entry.viewport[2] =
                    scaleOriginX + (entry.viewport[2] - scaleOriginX) * scaleX;
                entry.viewport[3] =
                    scaleOriginY + (entry.viewport[3] - scaleOriginY) * scaleY;
            }
        }

        if(LOGGER && shouldDebugTitleRender(motionPath) &&
           markRenderDebugLogged("yuzu-presentation-scale:" + motionPath)) {
            LOGGER->info(
                "motion presentation scale: motion={} ref={:.1f}x{:.1f} canvas={}x{} scale={:.3f},{:.3f}",
                motionPath, refWidth, refHeight, canvasWidth, canvasHeight,
                scaleX, scaleY);
        }
    }

    std::string describeLayerForDebug(tTJSNI_BaseLayer *layer) {
        if(!layer) {
            return "<null>";
        }
        auto safeInt = [](auto &&fn, const char *fallback = "?") {
            try {
                return std::to_string(fn());
            } catch(...) {
                return std::string(fallback);
            }
        };
        auto safeBool = [](auto &&fn, const char *fallback = "?") {
            try {
                return fn() ? std::string("1") : std::string("0");
            } catch(...) {
                return std::string(fallback);
            }
        };
        auto safeName = [&]() {
            try {
                return motion::detail::narrow(layer->GetName());
            } catch(...) {
                return std::string("?");
            }
        };
        std::ostringstream out;
        out << "ptr=" << static_cast<const void *>(layer)
            << ",name=" << safeName()
            << ",primary=" << safeBool([&]() { return layer->IsPrimary(); })
            << ",visible=" << safeBool([&]() { return layer->GetVisible(); })
            << ",parentVisible=" << safeBool([&]() { return layer->GetParentVisible(); })
            << ",opacity=" << safeInt([&]() { return layer->GetOpacity(); })
            << ",order=" << safeInt([&]() { return layer->GetOrderIndex(); })
            << ",overall=" << safeInt([&]() { return layer->GetOverallOrderIndex(); })
            << ",rect=[" << safeInt([&]() { return layer->GetLeft(); })
            << "," << safeInt([&]() { return layer->GetTop(); })
            << "," << safeInt([&]() { return layer->GetWidth(); })
            << "x" << safeInt([&]() { return layer->GetHeight(); }) << "]"
            << ",image=" << safeInt([&]() { return layer->GetImageWidth(); })
            << "x" << safeInt([&]() { return layer->GetImageHeight(); })
            << ",hasImage=" << safeBool([&]() { return layer->GetHasImage(); })
            << ",type=" << safeInt([&]() { return static_cast<int>(layer->GetType()); })
            << ",children=" << safeInt([&]() { return layer->GetCount(); });
        return out.str();
    }

    std::string describeLayerAncestryForDebug(tTJSNI_BaseLayer *layer) {
        std::ostringstream out;
        int depth = 0;
        while(layer && depth < 12) {
            if(depth != 0) {
                out << " <- ";
            }
            out << "[" << depth << ":" << describeLayerForDebug(layer) << "]";
            layer = layer->GetParent();
            ++depth;
        }
        if(layer) {
            out << " <- ...";
        }
        return out.str();
    }

    void describeLayerTreeForDebug(std::ostringstream &out,
                                   tTJSNI_BaseLayer *layer,
                                   int depth = 0) {
        if(!layer || depth > 3) {
            return;
        }
        out << "\n";
        for(int i = 0; i < depth; ++i) {
            out << "  ";
        }
        out << "- " << describeLayerForDebug(layer);
        const auto childCount = std::min<tjs_uint>(layer->GetCount(), 12);
        for(tjs_uint i = 0; i < childCount; ++i) {
            describeLayerTreeForDebug(out, layer->GetChildren(static_cast<tjs_int>(i)),
                                      depth + 1);
        }
        if(layer->GetCount() > childCount) {
            out << "\n";
            for(int i = 0; i <= depth; ++i) {
                out << "  ";
            }
            out << "... " << (layer->GetCount() - childCount) << " more children";
        }
    }

    std::string joinRenderStrings(const std::vector<std::string> &values,
                                  const char *separator = ",") {
        std::ostringstream out;
        for(size_t i = 0; i < values.size(); ++i) {
            if(i != 0) {
                out << separator;
            }
            out << values[i];
        }
        return out.str();
    }

    std::string sampleBitmapStats(const iTVPBaseBitmap *bitmap) {
        if(!bitmap || bitmap->GetWidth() <= 0 || bitmap->GetHeight() <= 0) {
            return "bitmap=0x0 sampled=0 alpha=0 visible=0 color=0 any=0 maxA=0 maxC=0";
        }
        const size_t width = static_cast<size_t>(bitmap->GetWidth());
        const size_t height = static_cast<size_t>(bitmap->GetHeight());
        const size_t pixels = width * height;
        const size_t stride = std::max<size_t>(1u, pixels / 4096u);
        size_t sampled = 0;
        size_t alpha = 0;
        size_t visible = 0;
        size_t color = 0;
        size_t any = 0;
        int maxAlpha = 0;
        int maxColor = 0;
        std::uint64_t sumR = 0;
        std::uint64_t sumG = 0;
        std::uint64_t sumB = 0;
        size_t white = 0;
        size_t yellow = 0;
        for(size_t index = 0; index < pixels; index += stride) {
            const size_t y = index / width;
            const size_t x = index - y * width;
            const auto *row = static_cast<const std::uint8_t *>(
                bitmap->GetScanLine(static_cast<tjs_uint>(y)));
            if(!row) {
                continue;
            }
            const auto *pixel = row + x * 4u;
            const int b = pixel[0];
            const int g = pixel[1];
            const int r = pixel[2];
            const int a = pixel[3];
            const int maxRgb = std::max(r, std::max(g, b));
            ++sampled;
            if(a != 0) {
                ++alpha;
            }
            if(maxRgb != 0) {
                ++color;
            }
            if((r | g | b | a) != 0) {
                ++any;
            }
            if(a != 0 && maxRgb != 0) {
                ++visible;
                sumR += static_cast<std::uint64_t>(r);
                sumG += static_cast<std::uint64_t>(g);
                sumB += static_cast<std::uint64_t>(b);
                if(pixelLooksWhiteMask(pixel)) {
                    ++white;
                }
                if(r >= 220 && g >= 170 && b <= 160) {
                    ++yellow;
                }
            }
            maxAlpha = std::max(maxAlpha, a);
            maxColor = std::max(maxColor, maxRgb);
        }
        std::ostringstream out;
        out << "bitmap=" << width << "x" << height
            << " sampled=" << sampled
            << " alpha=" << alpha
            << " visible=" << visible
            << " color=" << color
            << " any=" << any
            << " maxA=" << maxAlpha
            << " maxC=" << maxColor
            << " avgRGB=("
            << (visible ? static_cast<int>(sumR / visible) : 0) << ","
            << (visible ? static_cast<int>(sumG / visible) : 0) << ","
            << (visible ? static_cast<int>(sumB / visible) : 0) << ")"
            << " white=" << white
            << " yellow=" << yellow;
        return out.str();
    }

    template <typename AnimatorState>
    bool stepQueuedAnimatorLike_0x67D01C(AnimatorState &state, double dt,
                                         double &outValue) {
        double remaining = std::max(dt, 0.0);

        while(remaining > 0.0) {
            if(!state.active) {
                if(state.queue.empty()) {
                    outValue = state.currentValue;
                    return false;
                }
                const auto frame = state.queue.front();
                state.queue.pop_front();
                state.startValue = state.currentValue;
                state.targetValue = frame.value;
                state.duration = std::max(frame.duration, 0.000001f);
                state.weight = frame.weight;
                state.progress = 0.0f;
                state.active = true;
            }

            const double remainingDuration =
                static_cast<double>(state.duration) *
                std::max(0.0f, 1.0f - state.progress);
            const double consume = std::min(remaining, remainingDuration);
            if(state.duration > 0.0f) {
                state.progress = static_cast<float>(std::min(
                    1.0, static_cast<double>(state.progress) +
                             consume / static_cast<double>(state.duration)));
            } else {
                state.progress = 1.0f;
            }

            const double ratio =
                std::pow(std::clamp(static_cast<double>(state.progress), 0.0,
                                    1.0),
                         static_cast<double>(state.weight));
            state.currentValue = static_cast<float>(
                state.startValue +
                (state.targetValue - state.startValue) * ratio);
            remaining -= consume;

            if(state.progress >= 1.0f) {
                state.currentValue = state.targetValue;
                state.active = false;
            }

            if(consume <= 0.0) {
                break;
            }
        }

        outValue = state.currentValue;
        return state.active || !state.queue.empty();
    }

    double timelineBlendEaseWeightLike_0x6735AC(double ease) {
        if(ease == 0.0) {
            return 1.0;
        }
        if(ease > 0.0) {
            return ease + 1.0;
        }
        return 1.0 / (1.0 - ease);
    }

    void applyPackedCornerTintLike_0x6A7518(
        tTVPBaseBitmap &bitmap,
        const std::array<std::uint32_t, 4> &packedColors,
        bool halfAlphaBlend,
        bool tintDefaultNeutralMask,
        bool tintOnlyWhitePixels,
        const std::array<int, 4> &neutralTint) {
        const auto c0 = packedColors[0];
        const auto c1 = packedColors[1];
        const auto c2 = packedColors[2];
        const auto c3 = packedColors[3];
        const bool alphaOnlyMask = bitmapLooksAlphaOnlyMask(bitmap);
        const bool colorsAreNeutral = packedColorsAreDefault(c0, c1, c2, c3) ||
            packedColorsAreOpaqueWhite(c0, c1, c2, c3);
        if(colorsAreNeutral && !tintDefaultNeutralMask) {
            return;
        }

        const auto topLeft =
            colorsAreNeutral ? neutralTint : unpackPackedRgba(c0);
        const auto topRight =
            colorsAreNeutral ? neutralTint : unpackPackedRgba(c1);
        const auto bottomRight =
            colorsAreNeutral ? neutralTint : unpackPackedRgba(c2);
        const auto bottomLeft =
            colorsAreNeutral ? neutralTint : unpackPackedRgba(c3);
        const int width = static_cast<int>(bitmap.GetWidth());
        const int height = static_cast<int>(bitmap.GetHeight());
        if(width <= 0 || height <= 0) {
            return;
        }

        const int colorDivisor =
            (halfAlphaBlend && !colorsAreNeutral) ? 128 : 255;
        const int spanX = std::max(width - 1, 1);
        const int spanY = std::max(height - 1, 1);
        const auto lerpChannel = [](int a, int b, int pos, int span) -> int {
            if(span <= 0) {
                return a;
            }
            return a + (pos * (b - a)) / span;
        };

        for(int y = 0; y < height; ++y) {
            auto *row = static_cast<std::uint8_t *>(
                bitmap.GetScanLineForWrite(static_cast<tjs_uint>(y)));
            const int rowLeftR =
                lerpChannel(topLeft[0], bottomLeft[0], y, spanY);
            const int rowLeftG =
                lerpChannel(topLeft[1], bottomLeft[1], y, spanY);
            const int rowLeftB =
                lerpChannel(topLeft[2], bottomLeft[2], y, spanY);
            const int rowLeftA =
                lerpChannel(topLeft[3], bottomLeft[3], y, spanY);
            const int rowRightR =
                lerpChannel(topRight[0], bottomRight[0], y, spanY);
            const int rowRightG =
                lerpChannel(topRight[1], bottomRight[1], y, spanY);
            const int rowRightB =
                lerpChannel(topRight[2], bottomRight[2], y, spanY);
            const int rowRightA =
                lerpChannel(topRight[3], bottomRight[3], y, spanY);

            for(int x = 0; x < width; ++x) {
                auto *dst = row + static_cast<size_t>(x) * 4u;
                const bool whiteMaskPixel = pixelLooksWhiteMask(dst);
                if(tintOnlyWhitePixels && !whiteMaskPixel) {
                    continue;
                }
                const int tintR =
                    lerpChannel(rowLeftR, rowRightR, x, spanX);
                const int tintG =
                    lerpChannel(rowLeftG, rowRightG, x, spanX);
                const int tintB =
                    lerpChannel(rowLeftB, rowRightB, x, spanX);
                const int tintA =
                    lerpChannel(rowLeftA, rowRightA, x, spanX);
                if(colorsAreNeutral && tintOnlyWhitePixels && whiteMaskPixel) {
                    const int srcA = static_cast<int>(dst[3]);
                    dst[2] = static_cast<std::uint8_t>(tintR);
                    dst[1] = static_cast<std::uint8_t>(tintG);
                    dst[0] = static_cast<std::uint8_t>(tintB);
                    dst[3] = static_cast<std::uint8_t>(
                        std::min(255, tintA * srcA / 255));
                    continue;
                }
                const int srcR = alphaOnlyMask ? 255 : static_cast<int>(dst[2]);
                const int srcG = alphaOnlyMask ? 255 : static_cast<int>(dst[1]);
                const int srcB = alphaOnlyMask ? 255 : static_cast<int>(dst[0]);
                dst[2] = static_cast<std::uint8_t>(std::min(
                    255, tintR * srcR / colorDivisor));
                dst[1] = static_cast<std::uint8_t>(std::min(
                    255, tintG * srcG / colorDivisor));
                dst[0] = static_cast<std::uint8_t>(std::min(
                    255, tintB * srcB / colorDivisor));
                dst[3] = static_cast<std::uint8_t>(std::min(
                    255, tintA * static_cast<int>(dst[3]) / 255));
            }
        }
    }

    void recolorYuzuCompositeLogoText(tTVPBaseBitmap &bitmap,
                                      const std::array<int, 4> &textTint) {
        const int width = static_cast<int>(bitmap.GetWidth());
        const int height = static_cast<int>(bitmap.GetHeight());
        if(width <= 0 || height <= 0) {
            return;
        }

        for(int y = 0; y < height; ++y) {
            auto *row = static_cast<std::uint8_t *>(
                bitmap.GetScanLineForWrite(static_cast<tjs_uint>(y)));
            for(int x = 0; x < width; ++x) {
                auto *dst = row + static_cast<size_t>(x) * 4u;
                const int a = static_cast<int>(dst[3]);
                if(a == 0) {
                    continue;
                }

                const int r = static_cast<int>(dst[0]);
                const int g = static_cast<int>(dst[1]);
                const int b = static_cast<int>(dst[2]);
                const int maxRgb = std::max(r, std::max(g, b));
                const bool blueLogoText =
                    b >= 170 && g >= 80 && r <= 180 && b > r + 35 &&
                    b >= g + 8;
                if(!blueLogoText) {
                    continue;
                }

                const int scale = std::max(96, maxRgb);
                dst[0] = static_cast<std::uint8_t>(
                    std::min(255, textTint[0] * scale / 255));
                dst[1] = static_cast<std::uint8_t>(
                    std::min(255, textTint[1] * scale / 255));
                dst[2] = static_cast<std::uint8_t>(
                    std::min(255, textTint[2] * scale / 255));
            }
        }
    }

    iTJSDispatch2 *resolveLayerTreeOwnerObject(iTJSDispatch2 *object) {
        if(!object) {
            return nullptr;
        }

        tTJSVariant objectVar(object, object);
        tTJSVariant value;
        if(getObjectProperty(objectVar, TJS_W("layerTreeOwnerInterface"), value) &&
           value.Type() != tvtVoid) {
            return object;
        }

        if(getObjectProperty(objectVar, TJS_W("window"), value) &&
           value.Type() == tvtObject && value.AsObjectNoAddRef()) {
            return value.AsObjectNoAddRef();
        }

        if(auto *resolvedLayer = tryResolveLayerDispatch(objectVar);
           resolvedLayer && resolvedLayer != object) {
            return resolveLayerTreeOwnerObject(resolvedLayer);
        }

        return nullptr;
    }

    iTJSDispatch2 *resolvePrimaryLayerObject(iTJSDispatch2 *layerTreeOwnerObject) {
        if(!layerTreeOwnerObject) {
            return nullptr;
        }

        tTJSVariant ownerVar(layerTreeOwnerObject, layerTreeOwnerObject);
        tTJSVariant primaryVar;
        if(!getObjectProperty(ownerVar, TJS_W("primaryLayer"), primaryVar) ||
           primaryVar.Type() != tvtObject || !primaryVar.AsObjectNoAddRef()) {
            return nullptr;
        }

        if(auto *resolved = tryResolveLayerDispatch(primaryVar)) {
            return resolved;
        }
        return primaryVar.AsObjectNoAddRef();
    }

    iTJSDispatch2 *resolveMainWindowOwnerObject() {
        if(!TVPMainWindow) {
            return nullptr;
        }
        auto *owner = TVPMainWindow->GetOwnerNoAddRef();
        if(owner) {
            return owner;
        }

        iTJSDispatch2 *global = TVPGetScriptDispatch();
        if(!global) {
            return nullptr;
        }

        tTJSVariant windowClassVar;
        tTJSVariant mainWindowVar;
        iTJSDispatch2 *resolved = nullptr;
        if(TJS_SUCCEEDED(global->PropGet(0, TJS_W("Window"), nullptr,
                                         &windowClassVar, global)) &&
           windowClassVar.Type() == tvtObject &&
           windowClassVar.AsObjectNoAddRef() &&
           TJS_SUCCEEDED(windowClassVar.AsObjectNoAddRef()->PropGet(
               0, TJS_W("mainWindow"), nullptr, &mainWindowVar,
               windowClassVar.AsObjectNoAddRef())) &&
           mainWindowVar.Type() == tvtObject &&
           mainWindowVar.AsObjectNoAddRef()) {
            resolved = mainWindowVar.AsObjectNoAddRef();
        }

        global->Release();
        return resolved;
    }

    iTJSDispatch2 *resolveMainWindowPrimaryLayerObject() {
        return resolvePrimaryLayerObject(resolveMainWindowOwnerObject());
    }

    iTJSDispatch2 *createLayerObject(iTJSDispatch2 *layerTreeOwnerObject,
                                     iTJSDispatch2 *parentLayerObject) {
        if(!layerTreeOwnerObject) {
            return nullptr;
        }

        iTJSDispatch2 *global = TVPGetScriptDispatch();
        if(!global) {
            return nullptr;
        }

        tTJSVariant layerClassVar;
        iTJSDispatch2 *created = nullptr;
        const bool haveLayerClass =
            TJS_SUCCEEDED(global->PropGet(
                0, TJS_W("Layer"), nullptr, &layerClassVar, global))
            && layerClassVar.Type() == tvtObject
            && layerClassVar.AsObjectNoAddRef();
        if(haveLayerClass) {
            tTJSVariant ownerVar(layerTreeOwnerObject, layerTreeOwnerObject);
            tTJSVariant parentVar =
                parentLayerObject ? tTJSVariant(parentLayerObject, parentLayerObject)
                                  : tTJSVariant();
            tTJSVariant *args[] = { &ownerVar, &parentVar };
            if(TJS_FAILED(layerClassVar.AsObjectNoAddRef()->CreateNew(
                   0, nullptr, nullptr, &created, 2, args,
                   layerClassVar.AsObjectNoAddRef()))) {
                created = nullptr;
            }
        }

        global->Release();
        return created;
    }

    bool configureReusableLayerObject(iTJSDispatch2 *layerObject,
                                      iTJSDispatch2 *parentLayerObject,
                                      tTVPLayerType layerType,
                                      bool visible,
                                      bool absoluteOrderMode) {
        auto *layer = resolveNativeLayer(layerObject);
        if(!layer) {
            return false;
        }

        if(parentLayerObject) {
            if(auto *parentLayer = resolveNativeLayer(parentLayerObject);
               parentLayer && layer->GetParent() != parentLayer) {
                layer->SetParent(parentLayer);
            }
        }

        layer->SetType(layerType);
        layer->SetAbsoluteOrderMode(absoluteOrderMode);
        layer->SetVisible(visible);
        return true;
    }

    iTJSDispatch2 *ensureReusableLayerObject(tTJSVariant &slot,
                                             iTJSDispatch2 *layerTreeOwnerObject,
                                             iTJSDispatch2 *parentLayerObject,
                                             tTVPLayerType layerType,
                                             bool visible,
                                             bool absoluteOrderMode = false) {
        if(!layerTreeOwnerObject && parentLayerObject) {
            layerTreeOwnerObject = resolveLayerTreeOwnerObject(parentLayerObject);
        }
        if(!parentLayerObject && layerTreeOwnerObject) {
            parentLayerObject = resolvePrimaryLayerObject(layerTreeOwnerObject);
        }

        iTJSDispatch2 *layerObject =
            slot.Type() == tvtObject ? slot.AsObjectNoAddRef() : nullptr;
        if(!layerObject) {
            layerObject = createLayerObject(layerTreeOwnerObject, parentLayerObject);
            if(!layerObject) {
                return nullptr;
            }
            slot = tTJSVariant(layerObject, layerObject);
            layerObject->Release();
            layerObject = slot.AsObjectNoAddRef();
        }

        if(!configureReusableLayerObject(layerObject, parentLayerObject,
                                         layerType, visible,
                                         absoluteOrderMode)) {
            return nullptr;
        }
        return layerObject;
    }

    tTJSNI_BaseLayer *resolveNativeLayer(iTJSDispatch2 *layerObject) {
        if(!layerObject) {
            return nullptr;
        }
        tTJSNI_BaseLayer *layer = nullptr;
        if(TJS_FAILED(layerObject->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&layer))) || !layer) {
            return nullptr;
        }
        return layer;
    }

    bool queryLayerCanvasSize(iTJSDispatch2 *layerObject, int &width, int &height) {
        width = 0;
        height = 0;
        if(auto *layer = resolveNativeLayer(layerObject)) {
            width = static_cast<int>(layer->GetWidth());
            height = static_cast<int>(layer->GetHeight());
            if(width <= 0 || height <= 0) {
                width = static_cast<int>(layer->GetImageWidth());
                height = static_cast<int>(layer->GetImageHeight());
            }
        }
        return width > 0 && height > 0;
    }

    bool motionLayerNameSuggestsPresentationTarget(tTJSNI_BaseLayer *layer,
                                                   bool allowTransientLayer) {
        if(!layer) {
            return false;
        }
        const auto name =
            renderDebugLowercase(motion::detail::narrow(layer->GetName()));
        if(!allowTransientLayer &&
           name.find("trans_syslay") != std::string::npos) {
            return false;
        }
        return name.find("background") != std::string::npos ||
            name.find("back") != std::string::npos ||
            name.find("bg") != std::string::npos ||
            name.find("haikei") != std::string::npos ||
            name.find("trans_syslay") != std::string::npos ||
            name.find("背景") != std::string::npos;
    }

    bool motionLayerCoversCanvas(tTJSNI_BaseLayer *layer,
                                 int canvasWidth,
                                 int canvasHeight) {
        if(!layer || canvasWidth <= 0 || canvasHeight <= 0) {
            return false;
        }
        const int width = std::max(static_cast<int>(layer->GetWidth()),
                                   static_cast<int>(layer->GetImageWidth()));
        const int height = std::max(static_cast<int>(layer->GetHeight()),
                                    static_cast<int>(layer->GetImageHeight()));
        if(width <= 0 || height <= 0) {
            return false;
        }
        const int left = static_cast<int>(layer->GetLeft());
        const int top = static_cast<int>(layer->GetTop());
        return left <= 0 && top <= 0 &&
            left + width >= canvasWidth &&
            top + height >= canvasHeight;
    }

    bool motionLayerIsVisiblePresentationSurface(tTJSNI_BaseLayer *layer) {
        return layer && layer->GetOwnerNoAddRef() && layer->GetVisible() &&
            layer->GetParentVisible() && layer->GetOpacity() > 0;
    }

    int scoreMotionPresentationChild(tTJSNI_BaseLayer *parent,
                                     tTJSNI_BaseLayer *child,
                                     int canvasWidth,
                                     int canvasHeight,
                                     tjs_uint index,
                                     bool allowTransientLayer) {
        if(!parent || !child || child == parent || !child->GetOwnerNoAddRef()) {
            return -1;
        }
        if(!motionLayerIsVisiblePresentationSurface(child) ||
           !child->GetHasImage() ||
           !motionLayerCoversCanvas(child, canvasWidth, canvasHeight)) {
            return -1;
        }
        if(!motionLayerNameSuggestsPresentationTarget(child,
                                                      allowTransientLayer)) {
            return -1;
        }

        int score = 100;
        const auto name =
            renderDebugLowercase(motion::detail::narrow(child->GetName()));
        if(allowTransientLayer &&
           name.find("trans_syslay") != std::string::npos) {
            score += 200;
        }
        if(child->GetOrderIndex() == 0) {
            score += 20;
        }
        if(index == 0) {
            score += 10;
        }
        if(child->GetCount() > 0) {
            score += 5;
        }
        if(!child->IsPrimary() && parent->GetParent()) {
            score += 5;
        }
        return score;
    }

    bool genericMotionLayerNameLooksLikeUiChrome(const std::string &name) {
        return name.find("bar") != std::string::npos ||
            name.find("frame") != std::string::npos ||
            name.find("grad") != std::string::npos ||
            name.find("eff") != std::string::npos ||
            name.find("mask") != std::string::npos ||
            name.find("sq_") != std::string::npos ||
            name.find("square") != std::string::npos ||
            name.find("logo") != std::string::npos ||
            name.find("date") != std::string::npos ||
            name.find("passive") != std::string::npos ||
            name.find("message") != std::string::npos ||
            name.find("text") != std::string::npos ||
            name.find("trans") != std::string::npos ||
            name.find("クリック") != std::string::npos ||
            name.find("待ち") != std::string::npos ||
            name.find("メッセージ") != std::string::npos ||
            name.find("テキスト") != std::string::npos ||
            name.find("名前") != std::string::npos;
    }

    int scoreGenericMotionPresentationChild(tTJSNI_BaseLayer *parent,
                                            tTJSNI_BaseLayer *child,
                                            int canvasWidth,
                                            int canvasHeight,
                                            int depth) {
        if(!parent || !child || child == parent || !child->GetOwnerNoAddRef()) {
            return -1;
        }
        if(!motionLayerIsVisiblePresentationSurface(child) ||
           !child->GetHasImage() ||
           !motionLayerCoversCanvas(child, canvasWidth, canvasHeight)) {
            return -1;
        }
        if(child->GetCount() > 0) {
            return -1;
        }

        const auto name =
            renderDebugLowercase(motion::detail::narrow(child->GetName()));
        if(genericMotionLayerNameLooksLikeUiChrome(name)) {
            return -1;
        }

        int score = 100;
        if(name == "ev") {
            score += 700;
        } else if(name.find("ev") != std::string::npos ||
                  name.find("event") != std::string::npos ||
                  name.find("cg") != std::string::npos) {
            score += 500;
        } else if(name == "stage" || name.find("stage") != std::string::npos) {
            score += 350;
        } else if(name.find("背景") != std::string::npos ||
                  name.find("background") != std::string::npos ||
                  name.find("bg") != std::string::npos) {
            score += 80;
        }
        score += static_cast<int>(child->GetOverallOrderIndex());
        score += static_cast<int>(child->GetOrderIndex()) * 2;
        score -= depth * 4;
        return score;
    }

    bool yuzuTitleLayerNameMatches(tTJSNI_BaseLayer *layer) {
        if(!layer) {
            return false;
        }
        const auto name =
            renderDebugLowercase(motion::detail::narrow(layer->GetName()));
        return name == "title_bg" ||
            name.find("title_bg") != std::string::npos ||
            (name.find("title") != std::string::npos &&
             name.find("bg") != std::string::npos);
    }

    tTJSNI_BaseLayer *findYuzuTitlePresentationLayer(tTJSNI_BaseLayer *parent,
                                                     int canvasWidth,
                                                     int canvasHeight) {
        if(!parent || parent->GetCount() <= 0) {
            return nullptr;
        }

        struct Candidate {
            tTJSNI_BaseLayer *layer = nullptr;
            int score = -1;
        };
        Candidate best;
        auto visit = [&](auto &&self,
                         tTJSNI_BaseLayer *current,
                         int depth) -> void {
            if(!current || current->GetCount() <= 0) {
                return;
            }
            const auto childCount = current->GetCount();
            for(tjs_uint i = 0; i < childCount; ++i) {
                auto *child = current->GetChildren(static_cast<tjs_int>(i));
                if(motionLayerIsVisiblePresentationSurface(child) &&
                   child->GetHasImage() &&
                   motionLayerCoversCanvas(child, canvasWidth, canvasHeight) &&
                   yuzuTitleLayerNameMatches(child)) {
                    int score = 500;
                    if(child->GetOrderIndex() == 0) {
                        score += 20;
                    }
                    score -= depth * 4;
                    if(score > best.score) {
                        best.layer = child;
                        best.score = score;
                    }
                }
                self(self, child, depth + 1);
            }
        };
        visit(visit, parent, 0);
        return best.layer;
    }

    tTJSNI_BaseLayer *findMotionPresentationChild(tTJSNI_BaseLayer *parent,
                                                  int canvasWidth,
                                                  int canvasHeight,
                                                  bool allowTransientLayer) {
        if(!parent || parent->GetCount() <= 0) {
            return nullptr;
        }

        struct Candidate {
            tTJSNI_BaseLayer *layer = nullptr;
            int score = -1;
        };
        Candidate best;
        auto visit = [&](auto &&self,
                         tTJSNI_BaseLayer *current,
                         int depth) -> void {
            if(!current || current->GetCount() <= 0) {
                return;
            }
            const auto childCount = current->GetCount();
            for(tjs_uint i = 0; i < childCount; ++i) {
                auto *child = current->GetChildren(static_cast<tjs_int>(i));
                int score = scoreMotionPresentationChild(
                    current, child, canvasWidth, canvasHeight, i,
                    allowTransientLayer);
                if(score >= 0) {
                    score -= depth * 3;
                    if(score > best.score) {
                        best.layer = child;
                        best.score = score;
                    }
                }
                self(self, child, depth + 1);
            }
        };
        visit(visit, parent, 0);
        return best.layer;
    }

    tTJSNI_BaseLayer *findGenericMotionPresentationChild(
        tTJSNI_BaseLayer *parent,
        int canvasWidth,
        int canvasHeight) {
        if(!parent || parent->GetCount() <= 0) {
            return nullptr;
        }

        struct Candidate {
            tTJSNI_BaseLayer *layer = nullptr;
            int score = -1;
        };
        Candidate best;
        auto visit = [&](auto &&self,
                         tTJSNI_BaseLayer *current,
                         int depth) -> void {
            if(!current || current->GetCount() <= 0) {
                return;
            }
            const auto childCount = current->GetCount();
            for(tjs_uint i = 0; i < childCount; ++i) {
                auto *child = current->GetChildren(static_cast<tjs_int>(i));
                const int score = scoreGenericMotionPresentationChild(
                    current, child, canvasWidth, canvasHeight, depth);
                if(score > best.score) {
                    best.layer = child;
                    best.score = score;
                }
                self(self, child, depth + 1);
            }
        };
        visit(visit, parent, 0);
        return best.layer;
    }

    bool prepareMotionPresentationLayerForRender(tTJSNI_BaseLayer *layer,
                                                 int canvasWidth,
                                                 int canvasHeight) {
        if(!layer || canvasWidth <= 0 || canvasHeight <= 0) {
            return false;
        }
        if(!layer->GetHasImage()) {
            layer->SetHasImage(true);
        }
        const tjs_uint imageWidth = std::max(
            static_cast<tjs_uint>(canvasWidth),
            static_cast<tjs_uint>(std::max<tjs_int>(layer->GetImageWidth(), 0)));
        const tjs_uint imageHeight = std::max(
            static_cast<tjs_uint>(canvasHeight),
            static_cast<tjs_uint>(std::max<tjs_int>(layer->GetImageHeight(), 0)));
        if(layer->GetImageWidth() < imageWidth ||
           layer->GetImageHeight() < imageHeight) {
            layer->SetImageSize(imageWidth, imageHeight);
        }
        layer->SetClip(0, 0,
                       std::max<tjs_int>(canvasWidth, layer->GetWidth()),
                       std::max<tjs_int>(canvasHeight, layer->GetHeight()));
        layer->SetVisible(true);
        // Yuzu/KAG motion presentation layers are visual canvases. Keep them
        // out of mouse hit testing so title buttons and message layers behind
        // the full-screen background can still receive input.
        layer->SetHitThreshold(256);
        return true;
    }

    iTJSDispatch2 *ensureYuzuTitlePresentationRenderLayer(
        motion::detail::PlayerRuntime &runtime,
        iTJSDispatch2 *layerTreeOwnerObject,
        iTJSDispatch2 *parentLayerObject) {
        if(!parentLayerObject) {
            return nullptr;
        }
        if(!layerTreeOwnerObject) {
            layerTreeOwnerObject =
                resolveLayerTreeOwnerObject(parentLayerObject);
        }
        auto *layerObject = ensureReusableLayerObject(
            runtime.presentationRenderLayer,
            layerTreeOwnerObject,
            parentLayerObject,
            static_cast<tTVPLayerType>(ltAlpha),
            true);
        auto *layer = resolveNativeLayer(layerObject);
        if(!layer) {
            return nullptr;
        }
        layer->SetName(TJS_W("AetherKiriYuzuTitlePresentation"));
        layer->SetHitThreshold(256);
        // title_bg is a background/event surface. A fallback presentation layer
        // must stay behind title UI and character layers instead of becoming a
        // full-screen foreground overlay.
        try {
            layer->BringToBack();
        } catch(...) {
        }
        return layerObject;
    }

    bool prepareLayerForRender(iTJSDispatch2 *layerObject,
                               int width, int height,
                               tjs_uint32 clearColor) {
        auto *layer = resolveNativeLayer(layerObject);
        if(!layer || width <= 0 || height <= 0) {
            return false;
        }

        if(!layer->GetHasImage()) {
            layer->SetHasImage(true);
        }
        layer->SetImageSize(static_cast<tjs_uint>(width),
                            static_cast<tjs_uint>(height));
        layer->SetSize(width, height);
        layer->SetClip(0, 0, width, height);
        tTVPRect rect(0, 0, width, height);
        layer->FillRect(rect, clearColor);
        return true;
    }

    bool prepareLayerForPresentationCopy(iTJSDispatch2 *layerObject,
                                         bool usingPresentationTarget,
                                         int width,
                                         int height) {
        auto *layer = resolveNativeLayer(layerObject);
        if(!layer || width <= 0 || height <= 0) {
            return false;
        }
        if(usingPresentationTarget) {
            return prepareMotionPresentationLayerForRender(layer, width, height);
        }
        if(!layer->GetHasImage()) {
            layer->SetHasImage(true);
        }
        if(layer->GetImageWidth() < width || layer->GetImageHeight() < height) {
            layer->SetImageSize(static_cast<tjs_uint>(width),
                                static_cast<tjs_uint>(height));
        }
        if(layer->GetWidth() != width || layer->GetHeight() != height) {
            layer->SetSize(width, height);
        }
        layer->SetClip(0, 0, width, height);
        layer->SetVisible(true);
        return true;
    }

    bool copyGlobalPresentationRender(
        iTJSDispatch2 *targetLayerObject,
        bool usingPresentationTarget,
        int canvasWidth,
        int canvasHeight,
        GlobalPresentationRenderCacheEntry &entry) {
        auto *sourceLayerObject = globalPresentationSourceLayerObject(entry);
        if(!sourceLayerObject || sourceLayerObject == targetLayerObject) {
            return false;
        }
        auto *sourceLayer = resolveNativeLayer(sourceLayerObject);
        auto *targetLayer = resolveNativeLayer(targetLayerObject);
        if(!sourceLayer || !targetLayer || !sourceLayer->GetMainImage()) {
            return false;
        }
        if(sourceLayer->GetImageWidth() < canvasWidth ||
           sourceLayer->GetImageHeight() < canvasHeight) {
            return false;
        }
        if(!prepareLayerForPresentationCopy(targetLayerObject,
                                            usingPresentationTarget,
                                            canvasWidth, canvasHeight)) {
            return false;
        }
        try {
            const tTVPRect sourceRect(0, 0, canvasWidth, canvasHeight);
            targetLayer->CopyRect(0, 0, sourceLayer->GetMainImage(), nullptr,
                                  sourceRect);
        } catch(...) {
            return false;
        }
        return true;
    }

    tTVPBlendOperationMode resolveBlendOperationModeLike_0x6C7440(
        int rawBlendMode) {
        // libkrkr2.so 0x6C7440 does not pass the raw item blend flag through to
        // operateRect. It first maps the low 4 bits to the final TVP blend
        // operation mode: 1->0xE, 2/5->0xF, 3->0x10, 4->0x11, and the raw 0 /
        // default path ultimately composites with mode 2 in the common case.
        switch(rawBlendMode & 0x0F) {
            case 1:
                return omPsAdditive;       // 0xE
            case 2:
            case 5:
                return omPsSubtractive;    // 0xF
            case 3:
                return omPsMultiplicative; // 0x10
            case 4:
                return omPsScreen;         // 0x11
            case 0:
            default:
                return omAlpha;            // 0x2
        }
    }

    bool shouldUseDirectRenderPathLike_0x6C7440(
        const motion::detail::PlayerRuntime::RenderCommand &command) {
        const unsigned lowNibble =
            static_cast<unsigned>(command.blendMode) & 0x0Fu;
        return !command.clearEnabled &&
            command.visibleAncestorIndex < 0 &&
            (lowNibble == 0u || lowNibble > 5u);
    }

    std::array<tTVPPointD, 3> buildAffineTrianglePoints(
        const std::array<float, 8> &corners,
        float xOffset,
        float yOffset) {
        return {{
            { static_cast<double>(corners[0] + xOffset),
              static_cast<double>(corners[1] + yOffset) },
            { static_cast<double>(corners[2] + xOffset),
              static_cast<double>(corners[3] + yOffset) },
            { static_cast<double>(corners[6] + xOffset),
              static_cast<double>(corners[7] + yOffset) },
        }};
    }

    bool axisAlignedIntegerRectFromCorners(
        const std::array<float, 8> &corners,
        float xOffset,
        float yOffset,
        tTVPRect &outRect) {
        constexpr float kEpsilon = 0.02f;
        for(float value : corners) {
            if(!std::isfinite(value)) {
                return false;
            }
        }

        const float left = corners[0] + xOffset;
        const float top = corners[1] + yOffset;
        const float right = corners[2] + xOffset;
        const float bottom = corners[5] + yOffset;
        if(!(right > left && bottom > top)) {
            return false;
        }
        if(std::fabs((corners[3] + yOffset) - top) > kEpsilon ||
           std::fabs((corners[4] + xOffset) - right) > kEpsilon ||
           std::fabs((corners[6] + xOffset) - left) > kEpsilon ||
           std::fabs((corners[7] + yOffset) - bottom) > kEpsilon) {
            return false;
        }

        auto roundToInt = [&](float value, int &out) {
            const float rounded = std::round(value);
            if(std::fabs(value - rounded) > kEpsilon) {
                return false;
            }
            out = static_cast<int>(rounded);
            return true;
        };

        int il = 0;
        int it = 0;
        int ir = 0;
        int ib = 0;
        if(!roundToInt(left, il) || !roundToInt(top, it) ||
           !roundToInt(right, ir) || !roundToInt(bottom, ib) ||
           ir <= il || ib <= it) {
            return false;
        }
        outRect = tTVPRect(il, it, ir, ib);
        return true;
    }

    std::vector<tTVPPointD> buildMeshPoints(
        const std::vector<float> &points,
        float xOffset,
        float yOffset) {
        std::vector<tTVPPointD> result;
        result.reserve(points.size() / 2u);
        for(size_t i = 0; i + 1 < points.size(); i += 2) {
            result.push_back({
                static_cast<double>(points[i] + xOffset),
                static_cast<double>(points[i + 1] + yOffset),
            });
        }
        return result;
    }

    motion::D3DAdaptor *ensureSharedD3DAdaptor(iTJSDispatch2 *targetLayerObject) {
        static std::unique_ptr<motion::D3DAdaptor> s_sharedAdaptor;
        if(!s_sharedAdaptor) {
            s_sharedAdaptor = std::make_unique<motion::D3DAdaptor>();
        }

        int width = 0;
        int height = 0;
        if(auto *layer = resolveNativeLayer(targetLayerObject)) {
            width = static_cast<int>(layer->GetWidth());
            height = static_cast<int>(layer->GetHeight());
            if(width <= 0 || height <= 0) {
                width = static_cast<int>(layer->GetImageWidth());
                height = static_cast<int>(layer->GetImageHeight());
            }
        }

        if(width > 0 && height > 0) {
            if(s_sharedAdaptor->getWidth() != width ||
               s_sharedAdaptor->getHeight() != height) {
                s_sharedAdaptor->setSize(width, height);
            }
        }
        s_sharedAdaptor->setVisible(true);
        return s_sharedAdaptor.get();
    }

    struct RenderClipRect {
        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;
    };

    bool computeRenderClipRect(
        const motion::detail::PlayerRuntime::PreparedRenderItem &entry,
                               int canvasWidth, int canvasHeight,
                               RenderClipRect &out,
                               std::string *failureReason = nullptr) {
        float clipLeft = std::max(0.0f, entry.paintBox[0]);
        float clipTop = std::max(0.0f, entry.paintBox[1]);
        float clipRight = std::min(static_cast<float>(canvasWidth), entry.paintBox[2]);
        float clipBottom = std::min(static_cast<float>(canvasHeight), entry.paintBox[3]);

        if(entry.hasViewport && entry.viewport[2] >= entry.viewport[0]
           && entry.viewport[3] >= entry.viewport[1]) {
            clipLeft = std::max(clipLeft, floorf(entry.viewport[0]));
            clipTop = std::max(clipTop, floorf(entry.viewport[1]));
            clipRight = std::min(clipRight, ceilf(entry.viewport[2]));
            clipBottom = std::min(clipBottom, ceilf(entry.viewport[3]));
        }

        if(!(clipLeft < clipRight && clipTop < clipBottom)) {
            if(failureReason) {
                *failureReason = fmt::format(
                    "invalid_intersection paintBox=[{:.3f},{:.3f},{:.3f},{:.3f}] viewport={} canvas=[0,0,{},{}]",
                    entry.paintBox[0], entry.paintBox[1], entry.paintBox[2],
                    entry.paintBox[3],
                    entry.hasViewport
                        ? fmt::format("[{:.3f},{:.3f},{:.3f},{:.3f}]",
                                      entry.viewport[0], entry.viewport[1],
                                      entry.viewport[2], entry.viewport[3])
                        : std::string("<invalid default>"),
                    canvasWidth, canvasHeight);
            }
            return false;
        }

        out.left = static_cast<int>(clipLeft);
        out.top = static_cast<int>(clipTop);
        out.right = static_cast<int>(clipRight);
        out.bottom = static_cast<int>(clipBottom);
        if(failureReason) {
            failureReason->clear();
        }
        return out.left < out.right && out.top < out.bottom;
    }

    bool isAccurateSlaRenderEnabled() {
        auto *config = IndividualConfigManager::GetInstance();
        if(!config) {
            return false;
        }
        return config->GetValue<bool>("ogl_accurate_render", false);
    }

    tTVPRect localRectFromCommand(
        const motion::detail::PlayerRuntime::RenderCommand &command) {
        return tTVPRect(0, 0,
                        command.clipRect[2] - command.clipRect[0],
                        command.clipRect[3] - command.clipRect[1]);
    }

    bool clearLayerAlphaOutsideRect(tTJSNI_BaseLayer *layer,
                                    const tTVPRect &outerRect,
                                    const tTVPRect &innerRect) {
        if(!layer || !layer->GetMainImage()) {
            return false;
        }
        auto *bmp = layer->GetMainImage();
        if(outerRect.left >= outerRect.right || outerRect.top >= outerRect.bottom) {
            return true;
        }

        auto clearMask = [&](const tTVPRect &rect) {
            if(rect.left < rect.right && rect.top < rect.bottom) {
                bmp->FillMask(rect, 0);
            }
        };

        clearMask(tTVPRect(outerRect.left, outerRect.top,
                           innerRect.left, outerRect.bottom));
        clearMask(tTVPRect(innerRect.right, outerRect.top,
                           outerRect.right, outerRect.bottom));
        clearMask(tTVPRect(std::max(outerRect.left, innerRect.left),
                           outerRect.top,
                           std::min(outerRect.right, innerRect.right),
                           innerRect.top));
        clearMask(tTVPRect(std::max(outerRect.left, innerRect.left),
                           innerRect.bottom,
                           std::min(outerRect.right, innerRect.right),
                           outerRect.bottom));
        return true;
    }

    bool applyMotionAlphaMaskLike_0x6AF104(
        iTJSDispatch2 *dstLayerObject,
        int dstX,
        int dstY,
        iTJSDispatch2 *srcLayerObject,
        int srcX,
        int srcY,
        int width,
        int height,
        int threshold,
        int playerStencilType,
        int itemFlags,
        const std::string &motionPath,
        double frameTime,
        int dstNodeIndex,
        int srcNodeIndex) {
        auto *dstLayer = resolveNativeLayer(dstLayerObject);
        auto *srcLayer = resolveNativeLayer(srcLayerObject);
        if(!dstLayer || !srcLayer || !dstLayer->GetHasImage() ||
           !srcLayer->GetHasImage() || !dstLayer->GetMainImage() ||
           !srcLayer->GetMainImage()) {
            return false;
        }

        auto *dstBmp = dstLayer->GetMainImage();
        auto *srcBmp = srcLayer->GetMainImage();
        const auto &dstClip = dstLayer->GetClip();
        const int dstImageWidth = static_cast<int>(dstLayer->GetImageWidth());
        const int dstImageHeight = static_cast<int>(dstLayer->GetImageHeight());
        const int srcImageWidth = static_cast<int>(srcLayer->GetImageWidth());
        const int srcImageHeight = static_cast<int>(srcLayer->GetImageHeight());

        const int requestedLeft = dstX;
        const int requestedTop = dstY;
        const int requestedRight = dstX + width;
        const int requestedBottom = dstY + height;

        if(dstClip.left > dstX) {
            srcX += dstClip.left - dstX;
            width -= dstClip.left - dstX;
            dstX = dstClip.left;
        }
        if(dstClip.top > dstY) {
            srcY += dstClip.top - dstY;
            height -= dstClip.top - dstY;
            dstY = dstClip.top;
        }
        if(srcX < 0) {
            dstX -= srcX;
            width += srcX;
            srcX = 0;
        }
        if(srcY < 0) {
            dstY -= srcY;
            height += srcY;
            srcY = 0;
        }

        const int dstLimitRight =
            std::min(dstClip.right, dstImageWidth);
        const int dstLimitBottom =
            std::min(dstClip.bottom, dstImageHeight);
        if(dstX + width > dstLimitRight) {
            width = dstLimitRight - dstX;
        }
        if(dstY + height > dstLimitBottom) {
            height = dstLimitBottom - dstY;
        }
        if(srcX + width > srcImageWidth) {
            width = srcImageWidth - srcX;
        }
        if(srcY + height > srcImageHeight) {
            height = srcImageHeight - srcY;
        }

        const tTVPRect requestedRect(
            std::max(requestedLeft, dstClip.left),
            std::max(requestedTop, dstClip.top),
            std::min(requestedRight, dstLimitRight),
            std::min(requestedBottom, dstLimitBottom));
        const tTVPRect overlapRect(dstX, dstY, dstX + width, dstY + height);

        if((itemFlags & 3) == 1) {
            clearLayerAlphaOutsideRect(dstLayer, requestedRect, overlapRect);
        }

        if(width <= 0 || height <= 0) {
            return true;
        }

        const bool thresholdMaskMode = playerStencilType == 0;
        for(int y = 0; y < height; ++y) {
            auto *dstRow =
                static_cast<std::uint8_t *>(dstBmp->GetScanLineForWrite(dstY + y));
            const auto *srcRow =
                static_cast<const std::uint8_t *>(srcBmp->GetScanLine(srcY + y));
            for(int x = 0; x < width; ++x) {
                auto *dstPixel = dstRow + (dstX + x) * 4;
                const auto *srcPixel = srcRow + (srcX + x) * 4;
                const auto srcAlpha = static_cast<int>(srcPixel[3]);
                auto &dstAlpha = dstPixel[3];
                switch(itemFlags) {
                    case 1:
                        if(thresholdMaskMode) {
                            if(srcAlpha < threshold) {
                                dstAlpha = 0;
                            }
                        } else {
                            dstAlpha = static_cast<std::uint8_t>(
                                (static_cast<int>(dstAlpha) * srcAlpha) / 255);
                        }
                        break;
                    case 2:
                        if(thresholdMaskMode) {
                            if(srcAlpha >= threshold) {
                                dstAlpha = 0;
                            }
                        } else {
                            dstAlpha = static_cast<std::uint8_t>(
                                ((255 - srcAlpha) * static_cast<int>(dstAlpha)) / 255);
                        }
                        break;
                    case 5:
                    case 6:
                        if(thresholdMaskMode) {
                            if(srcAlpha >= threshold) {
                                dstAlpha = 255;
                            }
                        } else {
                            dstAlpha = static_cast<std::uint8_t>(
                                srcAlpha +
                                ((255 - srcAlpha) * static_cast<int>(dstAlpha)) / 255);
                        }
                        break;
                    default:
                        break;
                }
            }
        }

        motion::detail::logoChainTraceLogf(
            motionPath, "execute.mask", "0x6AF104", frameTime,
            "dstNode={} srcNode={} itemFlags={} playerStencilType={} threshold={} requested=[{},{},{},{}] overlap=[{},{},{},{}]",
            dstNodeIndex, srcNodeIndex, itemFlags, playerStencilType, threshold,
            requestedRect.left, requestedRect.top,
            requestedRect.right, requestedRect.bottom,
            overlapRect.left, overlapRect.top,
            overlapRect.right, overlapRect.bottom);
        return true;
    }

} // namespace

namespace motion {

    // --- Drawing/rendering ---
    void Player::setClearColor(tjs_int color) { _runtime->clearColor = color; }

    void Player::setResizable(bool v) { _runtime->resizable = v; }

    void Player::removeAllTextures() {
        _runtime->sourcesByKey.clear();
        _runtime->clearMotionBitmapCaches();
    }

    void Player::removeAllBg() { _runtime->backgrounds.clear(); }

    void Player::removeAllCaption() { _runtime->captions.clear(); }

    void Player::registerBg(tTJSVariant bg) { _runtime->backgrounds.push_back(bg); }

    void Player::registerCaption(tTJSVariant caption) {
        _runtime->captions.push_back(caption);
    }

    void Player::unloadUnusedTextures() {}

    tjs_int Player::alphaOpAdd() { return ++_runtime->alphaOpCounter; }

    tTJSVariant Player::captureCanvas() {
        if(_runtime->lastCanvas.Type() == tvtVoid) {
            draw();
        }
        return _runtime->lastCanvas;
    }

    tTJSVariant makeCanvasSummary(const detail::PlayerRuntime &runtime) {
        std::vector<iTJSDispatch2 *> uniqueSources;
        for(const auto &[_, source] : runtime.sourcesByKey) {
            if(source.Type() != tvtObject)
                continue;
            auto *object = source.AsObjectNoAddRef();
            if(!object)
                continue;
            if(std::find(uniqueSources.begin(), uniqueSources.end(), object) ==
               uniqueSources.end()) {
                uniqueSources.push_back(object);
            }
        }

        const tjs_int width =
            runtime.width > 0
                ? runtime.width
                : (runtime.activeMotion
                       ? static_cast<tjs_int>(runtime.activeMotion->width)
                       : 0);
        const tjs_int height =
            runtime.height > 0
                ? runtime.height
                : (runtime.activeMotion
                       ? static_cast<tjs_int>(runtime.activeMotion->height)
                       : 0);

        return detail::makeDictionary({
            { "width", width },
            { "height", height },
            { "sourceCount", static_cast<tjs_int>(uniqueSources.size()) },
            { "backgroundCount", static_cast<tjs_int>(runtime.backgrounds.size()) },
            { "captionCount", static_cast<tjs_int>(runtime.captions.size()) },
            { "flip", runtime.flip },
            { "opacity", runtime.opacity },
        });
    }

    void Player::ensureNodeTreeBuilt() {
        ensureMotionLoaded();
        if(!_runtime->activeMotion || _runtime->nodesBuilt) {
            return;
        }

        std::string clipLabel;
        if(const auto *clip = selectActiveClip()) {
            clipLabel = clip->label;
        }

        if(LOGGER && shouldDebugTitleRender(_runtime->activeMotion->path)) {
            LOGGER->info("motion build node tree: motion={} clip={} playing={}",
                         _runtime->activeMotion->path,
                         clipLabel.empty() ? std::string("<root>") : clipLabel,
                         joinRenderStrings(_runtime->playingTimelineLabels));
            if(const auto *clip = selectActiveClip()) {
                LOGGER->info(
                    "motion build node tree layers: motion={} clip={} clipLayers=[{}] rootLayers=[{}]",
                    _runtime->activeMotion->path, clip->label,
                    joinRenderStrings(clip->layerNames),
                    joinRenderStrings(_runtime->activeMotion->layerNames));
            }
        }

        _runtime->nodes = detail::buildNodeTree(*_runtime->activeMotion, clipLabel);
        _runtime->nodesBuilt = true;

        if(!_runtime->nodes.empty()) {
            auto &root = _runtime->nodes[0];
            root.localState.flipX = _rootFlipX;
            if(_hasPendingRootPos) {
                root.localState.posX = _pendingRootX;
                root.localState.posY = _pendingRootY;
            }
            root.localState.dirty = true;
        }

        _runtime->nodeLabelMap.clear();
        for(size_t ni = 0; ni < _runtime->nodes.size(); ++ni) {
            const auto &label = _runtime->nodes[ni].layerName;
            if(!label.empty()) {
                _runtime->nodeLabelMap.emplace(label, static_cast<int>(ni));
            }
        }

        if(LOGGER && shouldDebugTitleRender(_runtime->activeMotion->path)) {
            std::ostringstream nodeSummary;
            const size_t limit = std::min<size_t>(_runtime->nodes.size(), 24);
            for(size_t ni = 0; ni < limit; ++ni) {
                const auto &node = _runtime->nodes[ni];
                if(ni != 0) {
                    nodeSummary << ";";
                }
                nodeSummary << ni << ":"
                            << (node.layerName.empty() ? std::string("<root>")
                                                       : node.layerName)
                            << ":t" << node.nodeType
                            << ":src" << (node.hasSource ? 1 : 0)
                            << ":p" << node.parentIndex;
            }
            LOGGER->info("motion build nodes: motion={} count={} nodes=[{}]",
                         _runtime->activeMotion->path, _runtime->nodes.size(),
                         nodeSummary.str());
        }

        if(detail::logoChainTraceEnabled(_runtime->activeMotion)) {
            const auto &motionPath = _runtime->activeMotion->path;
            detail::logoChainTraceLogf(
                motionPath, "buildNodeTree", "0x6B51F0", _clampedEvalTime,
                "clipLabel={} rootLayers={} nodeCount={}",
                clipLabel.empty() ? std::string("<root>") : clipLabel,
                activeLayerNames().size(), _runtime->nodes.size());
            for(const auto &node : _runtime->nodes) {
                const bool hasStencilTypeKey =
                    node.psbNode && static_cast<bool>((*node.psbNode)["stencilType"]);
                detail::logoChainTraceLogf(
                    motionPath, "buildNodeTree.node", "0x6B51F0",
                    _clampedEvalTime,
                    "nodeIndex={} label={} type={} parent={} hasSource={} meshType={} inheritFlags=0x{:x} parentClipIndex={} stencilType={} hasStencilTypeKey={}",
                    node.index,
                    node.layerName.empty() ? std::string("<root>")
                                           : node.layerName,
                    node.nodeType, node.parentIndex, node.hasSource ? 1 : 0,
                    node.meshType, node.inheritFlags, node.parentClipIndex,
                    node.stencilType, hasStencilTypeKey ? 1 : 0);
            }
        }
    }

    bool Player::renderViaSharedD3DAdaptor(iTJSDispatch2 *targetLayerObject) {
        if(!targetLayerObject) {
            return false;
        }

        auto *resolvedTarget = targetLayerObject;
        tTJSVariant wrapper(targetLayerObject, targetLayerObject);
        if(auto *resolved = tryResolveLayerDispatch(wrapper)) {
            resolvedTarget = resolved;
        }

        auto *targetLayer = resolveNativeLayer(resolvedTarget);
        if(!targetLayer) {
            return false;
        }

        auto *sharedAdaptor = ensureSharedD3DAdaptor(resolvedTarget);
        if(!sharedAdaptor) {
            return false;
        }

        if(!renderToD3DAdaptor(sharedAdaptor)) {
            return false;
        }

        if(sharedAdaptor->getWidth() > 0 && sharedAdaptor->getHeight() > 0) {
            targetLayer->SetSize(sharedAdaptor->getWidth(),
                                 sharedAdaptor->getHeight());
        }
        targetLayer->SetVisible(true);

        tTJSVariant targetVar(resolvedTarget, resolvedTarget);
        tTJSVariant *args[] = { &targetVar };
        if(TJS_FAILED(sharedAdaptor->captureCanvas(nullptr, 1, args, nullptr))) {
            return false;
        }

        targetLayer->Update(false);
        _runtime->lastCanvas = tTJSVariant(resolvedTarget, resolvedTarget);
        return true;
    }

    bool Player::buildRenderCommands(tjs_int canvasWidth, tjs_int canvasHeight) {
        if(!_runtime) {
            return false;
        }

        _runtime->renderCommands.clear();
        const auto motionPath =
            _runtime->activeMotion ? _runtime->activeMotion->path : std::string{};
        for(const auto &entry : _runtime->preparedRenderItems) {
            if(!entry.drawFlag || entry.skipFlag0 || entry.skipFlag1 ||
               entry.opacity <= 0) {
                continue;
            }
            if(isYuzuTitleWhiteUtilityLayer(motionPath, entry.nodeLabel,
                                            entry.sourceKey)) {
                if(LOGGER && shouldDebugTitleRender(motionPath, entry.sourceKey) &&
                   markRenderDebugLogged(
                       "title-white-utility:" + motionPath + ":" +
                       entry.nodeLabel + ":" + entry.sourceKey)) {
                    LOGGER->info(
                        "motion skip title white utility layer: motion={} node={} label={} source={}",
                        motionPath, entry.nodeIndex, entry.nodeLabel,
                        entry.sourceKey);
                }
                continue;
            }

            RenderClipRect clipRect;
            std::string clipFailureReason;
            if(!computeRenderClipRect(entry, canvasWidth, canvasHeight, clipRect,
                                      &clipFailureReason)) {
                detail::logoChainTraceCheck(
                    motionPath, "renderCommand.clip", "0x6C4E28",
                    _clampedEvalTime,
                    fmt::format(
                        "paintBox∩viewport∩canvas exp paintBox=[{:.3f},{:.3f},{:.3f},{:.3f}] viewport={} canvas=[0,0,{},{}]",
                        entry.paintBox[0], entry.paintBox[1], entry.paintBox[2],
                        entry.paintBox[3],
                        entry.hasViewport
                            ? fmt::format("[{:.3f},{:.3f},{:.3f},{:.3f}]",
                                          entry.viewport[0], entry.viewport[1],
                                          entry.viewport[2], entry.viewport[3])
                            : std::string("<invalid default>"),
                        canvasWidth, canvasHeight),
                    fmt::format("nodeIndex={} act=<invalid:{}>",
                                entry.nodeIndex, clipFailureReason),
                    false,
                    "sub_6C4E28 produced an invalid local clip rect");
                continue;
            }

            detail::PlayerRuntime::RenderCommand command;
            command.nodeIndex = entry.nodeIndex;
            command.nodeLabel = entry.nodeLabel;
            command.srcRef = entry.srcRef;
            command.sourceKey = entry.sourceKey;
            command.hasOwnSource = entry.hasOwnSource;
            command.groupOnly = entry.groupOnly;
            command.blendMode = entry.blendMode;
            command.opacity = entry.opacity;
            command.itemFlags = entry.updateCount;
            command.parentNodeIndex = entry.visibleAncestorIndex;
            command.packedColors = entry.packedColors;
            command.visibleAncestorIndex = entry.visibleAncestorIndex;
            command.clearEnabled = _clearEnabled;
            command.meshType = entry.meshType;
            command.meshDivX = entry.meshDivX;
            command.meshDivY = entry.meshDivY;
            command.layerId = entry.layerId;
            command.worldCorners = entry.corners;
            command.clipRect = {
                clipRect.left, clipRect.top, clipRect.right, clipRect.bottom
            };
            command.dirtyRect = command.clipRect;

            for(size_t ci = 0; ci < entry.corners.size(); ci += 2) {
                command.localCorners[ci] =
                    entry.corners[ci] - 0.5f - static_cast<float>(clipRect.left);
                command.localCorners[ci + 1] =
                    entry.corners[ci + 1] - 0.5f - static_cast<float>(clipRect.top);
            }

            command.worldMeshPoints = entry.meshPoints;
            command.localMeshPoints.reserve(entry.meshPoints.size());
            for(size_t pi = 0; pi + 1 < entry.meshPoints.size(); pi += 2) {
                command.localMeshPoints.push_back(
                    entry.meshPoints[pi] - 0.5f - static_cast<float>(clipRect.left));
                command.localMeshPoints.push_back(
                    entry.meshPoints[pi + 1] - 0.5f - static_cast<float>(clipRect.top));
            }

            if(LOGGER && shouldDebugTitleRender(motionPath, entry.sourceKey)) {
                const auto source = renderDebugLowercase(entry.sourceKey);
                const bool isTrackedTitleLayer =
                    source.find("src/title/") != std::string::npos ||
                    source == "src/title/title2_ch" ||
                    source == "src/title/logo" ||
                    source == "src/title/pos2" ||
                    source == "src/title/pos" ||
                    source == "src/title/pos4" ||
                    isYuzuLogoPresentationMotion(motionPath);
                if(isTrackedTitleLayer) {
                    const int frameBucket =
                        static_cast<int>(std::floor(_frameTickCount / 30.0));
                    const int opacityBucket =
                        std::clamp(command.opacity, 0, 255) / 16;
                    if(markRenderDebugLogged(fmt::format(
                           "title-prepared|{}|{}|{}|{}", motionPath,
                           source, frameBucket, opacityBucket))) {
                        LOGGER->info(
                            "motion title prepared item: motion={} source={} node={} label={} frameTick={:.2f} opacity={} blend={} meshType={} mesh={}x{} parent={} flags={} clip=[{},{},{},{}] paint=[{:.1f},{:.1f},{:.1f},{:.1f}] world=[{:.1f},{:.1f},{:.1f},{:.1f},{:.1f},{:.1f},{:.1f},{:.1f}]",
                            motionPath, entry.sourceKey, entry.nodeIndex,
                            entry.nodeLabel, _frameTickCount, command.opacity,
                            command.blendMode, command.meshType,
                            command.meshDivX, command.meshDivY,
                            command.parentNodeIndex, command.itemFlags,
                            command.clipRect[0], command.clipRect[1],
                            command.clipRect[2], command.clipRect[3],
                            entry.paintBox[0], entry.paintBox[1],
                            entry.paintBox[2], entry.paintBox[3],
                            command.worldCorners[0], command.worldCorners[1],
                            command.worldCorners[2], command.worldCorners[3],
                            command.worldCorners[4], command.worldCorners[5],
                            command.worldCorners[6], command.worldCorners[7]);
                    }
                }
            }

            {
                std::array<float, 8> expectedLocalCorners{};
                bool cornersOk = true;
                for(size_t ci = 0; ci < entry.corners.size(); ci += 2) {
                    expectedLocalCorners[ci] =
                        entry.corners[ci] - 0.5f - static_cast<float>(clipRect.left);
                    expectedLocalCorners[ci + 1] =
                        entry.corners[ci + 1] - 0.5f - static_cast<float>(clipRect.top);
                    if(std::fabs(expectedLocalCorners[ci] -
                                 command.localCorners[ci]) > 0.01f ||
                       std::fabs(expectedLocalCorners[ci + 1] -
                                 command.localCorners[ci + 1]) > 0.01f) {
                        cornersOk = false;
                    }
                }
                detail::logoChainTraceCheck(
                    motionPath, "renderCommand.clip", "0x6C4E28",
                    _clampedEvalTime,
                    fmt::format(
                        "paintBox∩viewport∩canvas exp=[{},{},{},{}]",
                        clipRect.left, clipRect.top, clipRect.right,
                        clipRect.bottom),
                    fmt::format(
                        "nodeIndex={} act=[{},{},{},{}]",
                        entry.nodeIndex, command.clipRect[0],
                        command.clipRect[1], command.clipRect[2],
                        command.clipRect[3]),
                    true,
                    "sub_6C4E28 clip rect diverged from expected intersection");
                detail::logoChainTraceCheck(
                    motionPath, "renderCommand.localCorners", "0x6C4E28",
                    _clampedEvalTime,
                    fmt::format(
                        "corners-0.5-clipOrigin exp=[{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}]",
                        expectedLocalCorners[0], expectedLocalCorners[1],
                        expectedLocalCorners[2], expectedLocalCorners[3],
                        expectedLocalCorners[4], expectedLocalCorners[5],
                        expectedLocalCorners[6], expectedLocalCorners[7]),
                    fmt::format(
                        "nodeIndex={} act=[{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}]",
                        entry.nodeIndex,
                        command.localCorners[0], command.localCorners[1],
                        command.localCorners[2], command.localCorners[3],
                        command.localCorners[4], command.localCorners[5],
                        command.localCorners[6], command.localCorners[7]),
                    cornersOk,
                    "sub_6C4E28 local corner translation diverged from clip-local expectation");
            }

            _runtime->renderCommands.push_back(std::move(command));
        }

        std::unordered_map<int, size_t> commandIndexByNode;
        commandIndexByNode.reserve(_runtime->renderCommands.size());
        for(size_t i = 0; i < _runtime->renderCommands.size(); ++i) {
            _runtime->renderCommands[i].childCommandIndices.clear();
            _runtime->renderCommands[i].leafBuilt = false;
            _runtime->renderCommands[i].composedBuilt = false;
            _runtime->renderCommands[i].executedDirect = false;
            _runtime->renderCommands[i].builtRect = {0, 0, 0, 0};
            commandIndexByNode.emplace(_runtime->renderCommands[i].nodeIndex, i);
        }
        for(size_t i = 0; i < _runtime->renderCommands.size(); ++i) {
            const int parentNodeIndex = _runtime->renderCommands[i].parentNodeIndex;
            if(parentNodeIndex < 0) {
                continue;
            }
            if(const auto it = commandIndexByNode.find(parentNodeIndex);
               it != commandIndexByNode.end()) {
                auto &parentCommand = _runtime->renderCommands[it->second];
                // Synthetic/group-only parents keep local child composition.
                // The special alpha-mask path is selected later from the
                // parent's stencil flags instead of being inferred from every
                // visible ancestor relationship here.
                if(!parentCommand.groupOnly) {
                    continue;
                }
                _runtime->renderCommands[it->second].childCommandIndices.push_back(
                    static_cast<int>(i));
                _runtime->renderCommands[i].hasRenderParent = true;
            }
        }

        detail::logoChainTraceLogf(
            motionPath, "renderCommand.count", "0x6C4E28",
            _clampedEvalTime,
            "canvas={}x{} preparedItems={} renderCommands={}",
            canvasWidth, canvasHeight, _runtime->preparedRenderItems.size(),
            _runtime->renderCommands.size());
        if(LOGGER && shouldDebugTitleRender(motionPath)) {
            const int frameBucket =
                static_cast<int>(std::floor(_frameTickCount / 30.0));
            if(markRenderDebugLogged(fmt::format(
                   "title-command-count|{}|{}", motionPath, frameBucket))) {
                LOGGER->info(
                    "motion title command count: motion={} frameTick={:.2f} preparedItems={} renderCommands={}",
                    motionPath, _frameTickCount,
                    _runtime->preparedRenderItems.size(),
                    _runtime->renderCommands.size());
            }
        }
        return !_runtime->renderCommands.empty();
    }

    bool Player::executeLayerRenderCommands(iTJSDispatch2 *renderLayerObject,
                                            bool skipUpdate) {
        if(!renderLayerObject || !_runtime || !_runtime->activeMotion) {
            return false;
        }
        const auto motionPath = _runtime->activeMotion->path;

        auto *renderLayer = resolveNativeLayer(renderLayerObject);
        iTJSDispatch2 *scratchOwner = resolveMainWindowOwnerObject();
        iTJSDispatch2 *scratchParent = resolveMainWindowPrimaryLayerObject();
        if(!scratchOwner) {
            scratchOwner = resolveLayerTreeOwnerObject(renderLayerObject);
        }
        if(scratchParent && !resolveNativeLayer(scratchParent)) {
            if(auto *resolved =
                   tryResolveLayerDispatch(tTJSVariant(scratchParent, scratchParent))) {
                scratchParent = resolved;
            }
        }
        if(!scratchParent) {
            scratchParent = renderLayerObject;
        }
        if(scratchParent && !resolveNativeLayer(scratchParent)) {
            scratchParent = renderLayerObject;
        }
        detail::logoChainTraceLogf(
            motionPath, "execute.setup.pre", "0x6C7440", _clampedEvalTime,
            "renderLayer={} scratchOwner={} scratchParent={} renderLayerNative={} scratchParentNative={}",
            static_cast<const void *>(renderLayerObject),
            static_cast<const void *>(scratchOwner),
            static_cast<const void *>(scratchParent),
            static_cast<const void *>(renderLayer),
            static_cast<const void *>(resolveNativeLayer(scratchParent)));
        detail::logoChainTraceLogf(
            motionPath, "execute.begin", "0x6C7440", _clampedEvalTime,
            "renderCommands={} renderLayer={} scratchOwner={} scratchParent={} skipUpdate={}",
            _runtime->renderCommands.size(),
            static_cast<const void *>(renderLayer),
            static_cast<const void *>(scratchOwner),
            static_cast<const void *>(scratchParent), skipUpdate ? 1 : 0);
        int snapshotCopyOrder = 0;
        if(!renderLayer) {
            detail::logoChainTraceCheck(
                motionPath, "execute.setup", "0x6C7440", _clampedEvalTime,
                "renderLayer should resolve before executeLayerRenderCommands",
                fmt::format("renderLayer={}",
                            static_cast<const void *>(renderLayer)),
                false,
                "SLA/Layer backend could not resolve native layers before copy");
            return false;
        }

        struct RenderProfileStats {
            int baseHits = 0;
            int baseMisses = 0;
            int preparedHits = 0;
            int preparedMisses = 0;
            int storageLoads = 0;
            int psbLoads = 0;
            int tintBuilds = 0;
            std::uint64_t baseResolveUs = 0;
            std::uint64_t preparedResolveUs = 0;
            std::uint64_t tintBuildUs = 0;
        };
        RenderProfileStats profileStats;
        const bool profileEnabled = motionRenderProfileEnabled();
        const auto profileStartUs =
            profileEnabled ? motionRenderProfileNowUs() : 0;
        auto &baseSourceCache = _runtime->motionSourceBitmapCache;
        auto &preparedSourceCache = _runtime->motionPreparedBitmapCache;
        auto resolveBaseSourceBitmap =
            [&](const detail::PlayerRuntime::RenderCommand &command)
                -> std::shared_ptr<tTVPBaseBitmap> {
            if(command.sourceKey.empty()) {
                return nullptr;
            }
            if(auto it = baseSourceCache.find(command.sourceKey);
               it != baseSourceCache.end()) {
                if(profileEnabled) {
                    ++profileStats.baseHits;
                }
                return it->second;
            }
            if(profileEnabled) {
                ++profileStats.baseMisses;
            }
            const auto resolveStartUs =
                profileEnabled ? motionRenderProfileNowUs() : 0;

            std::shared_ptr<tTVPBaseBitmap> srcBmp;
            std::string sourceOrigin("unresolved");
            const auto resolvedPath =
                resolveMotionSourcePath(*_runtime->activeMotion, command.sourceKey);
            if(!resolvedPath.IsEmpty()) {
                ttstr loadPath = resolvedPath;
                const auto pathString = detail::narrow(resolvedPath);
                if(pathString.rfind('.') == std::string::npos ||
                   pathString.rfind('.') < pathString.rfind('/')) {
                    loadPath = resolvedPath + TJS_W(".png");
                }
                try {
                    auto bmp = std::make_shared<tTVPBaseBitmap>(1, 1, 32);
                    TVPLoadGraphic(bmp.get(), loadPath, TVP_clNone, 0, 0,
                                   glmNormal, nullptr, nullptr);
                    if(bmp->GetWidth() > 0 && bmp->GetHeight() > 0) {
                        srcBmp = bmp;
                        sourceOrigin = detail::narrow(loadPath);
                        if(profileEnabled) {
                            ++profileStats.storageLoads;
                        }
                        if(LOGGER &&
                           shouldDebugTitleRender(motionPath, command.sourceKey,
                                                  sourceOrigin) &&
                           markRenderDebugLogged("load|" + motionPath + "|" +
                                                 command.sourceKey + "|" +
                                                 sourceOrigin)) {
                            LOGGER->info(
                                "motion bitmap load: motion={} source={} resolved={} stats=[{}]",
                                motionPath, command.sourceKey, sourceOrigin,
                                sampleBitmapStats(srcBmp.get()));
                        }
                    }
                } catch(...) {
                }
            }

            if(!srcBmp) {
                int width = 0;
                int height = 0;
                double originX = 0.0;
                double originY = 0.0;
                std::vector<std::uint8_t> decodedPixels;
                bool decodedPixelsAreBgra = false;
                const auto *resource = findPSBResourceBySourceName(
                    *_runtime->activeMotion, command.sourceKey, width, height,
                    decodedPixels, originX, originY, &decodedPixelsAreBgra);
                if(resource && width > 0 && height > 0 && !resource->data.empty()) {
                    const bool sourcePixelsAreBgra =
                        motionSourcePixelsAreBGRA(motionPath,
                                                  decodedPixelsAreBgra);
                    const auto &pixelData =
                        decodedPixels.empty() ? resource->data : decodedPixels;
                    auto bmp = std::make_shared<tTVPBaseBitmap>(
                        static_cast<tjs_uint>(width),
                        static_cast<tjs_uint>(height), 32);
                    tTVPRect fillRect(0, 0, width, height);
                    bmp->Fill(fillRect, 0x00000000);
                    const auto *src = pixelData.data();
                    for(int y = 0; y < height; ++y) {
                        auto *row = static_cast<std::uint8_t *>(
                            bmp->GetScanLineForWrite(static_cast<tjs_uint>(y)));
                        for(int x = 0; x < width; ++x) {
                            const size_t sourceIndex =
                                (static_cast<size_t>(y) * width + x) * 4u;
                            if(sourceIndex + 3 >= pixelData.size()) {
                                break;
                            }
                            auto *dst = row + x * 4;
                            if(sourcePixelsAreBgra) {
                                dst[0] = src[sourceIndex + 0];
                                dst[1] = src[sourceIndex + 1];
                                dst[2] = src[sourceIndex + 2];
                            } else {
                                dst[0] = src[sourceIndex + 2];
                                dst[1] = src[sourceIndex + 1];
                                dst[2] = src[sourceIndex + 0];
                            }
                            dst[3] = src[sourceIndex + 3];
                        }
                    }
                    srcBmp = bmp;
                    if(profileEnabled) {
                        ++profileStats.psbLoads;
                    }
                    sourceOrigin = fmt::format(
                        "psb:{}:{}x{}:origin=({:.3f},{:.3f}):bgra={}",
                        command.sourceKey, width, height, originX, originY,
                        sourcePixelsAreBgra ? 1 : 0);
                    if(LOGGER &&
                       shouldDebugTitleRender(motionPath, command.sourceKey,
                                              sourceOrigin) &&
                       markRenderDebugLogged("load-psb|" + motionPath + "|" +
                                             command.sourceKey + "|" +
                                             sourceOrigin)) {
                        LOGGER->info(
                            "motion bitmap load-psb: motion={} source={} resolved={} stats=[{}]",
                            motionPath, command.sourceKey, sourceOrigin,
                            sampleBitmapStats(srcBmp.get()));
                    }
                }
            }

            detail::logoChainTraceLogf(
                motionPath, "execute.source", "0x6C7440", _clampedEvalTime,
                "source={} resolve={} bitmap={}x{}",
                command.sourceKey.empty() ? std::string("<none>")
                                          : command.sourceKey,
                sourceOrigin,
                srcBmp ? srcBmp->GetWidth() : 0,
                srcBmp ? srcBmp->GetHeight() : 0);

            baseSourceCache.emplace(command.sourceKey, srcBmp);
            if(profileEnabled) {
                profileStats.baseResolveUs +=
                    motionRenderProfileNowUs() - resolveStartUs;
            }
            return srcBmp;
        };
        auto resolveSourceBitmap =
            [&](const detail::PlayerRuntime::RenderCommand &command)
                -> std::shared_ptr<tTVPBaseBitmap> {
            if(command.sourceKey.empty()) {
                return nullptr;
            }

            const bool useHalfAlphaTint =
                (command.blendMode & 0xF0) == 0x10;
            const auto tintKey = fmt::format(
                "{}|{:08x}|{:08x}|{:08x}|{:08x}|{}",
                command.sourceKey, command.packedColors[0],
                command.packedColors[1], command.packedColors[2],
                command.packedColors[3], useHalfAlphaTint ? 1 : 0);
            if(auto it = preparedSourceCache.find(tintKey);
               it != preparedSourceCache.end()) {
                if(profileEnabled) {
                    ++profileStats.preparedHits;
                }
                return it->second;
            }
            if(profileEnabled) {
                ++profileStats.preparedMisses;
            }
            const auto preparedStartUs =
                profileEnabled ? motionRenderProfileNowUs() : 0;

            auto srcBmp = resolveBaseSourceBitmap(command);
            if(!srcBmp) {
                preparedSourceCache.emplace(tintKey, nullptr);
                if(profileEnabled) {
                    profileStats.preparedResolveUs +=
                        motionRenderProfileNowUs() - preparedStartUs;
                }
                return nullptr;
            }

            const bool sourceIsAlphaOnlyMask = bitmapLooksAlphaOnlyMask(*srcBmp);
            const bool colorsCarryTint =
                !packedColorsAreDefault(command.packedColors[0],
                                        command.packedColors[1],
                                        command.packedColors[2],
                                        command.packedColors[3]) &&
                !packedColorsAreOpaqueWhite(command.packedColors[0],
                                            command.packedColors[1],
                                            command.packedColors[2],
                                            command.packedColors[3]);
            const bool tintDefaultAlphaMask =
                sourceIsAlphaOnlyMask &&
                isYuzuStartupLogoMotion(motionPath);
            const bool yuzuLogoTextSource =
                isYuzuLogoTextMaskSource(motionPath, command.sourceKey);
            const bool yuzuLogoCompositeSource =
                isYuzuLogoCompositeSource(motionPath, command.sourceKey);
            const bool tintDefaultLogoTextMask =
                yuzuLogoTextSource &&
                bitmapLooksWhiteMask(*srcBmp);
            const bool tintDefaultLogoWhitePixels =
                yuzuLogoTextSource &&
                bitmapHasWhiteMaskPixels(*srcBmp);
            const bool tintOnlyWhitePixels =
                tintDefaultLogoWhitePixels && !sourceIsAlphaOnlyMask;
            const bool tintDefaultNeutralMask =
                tintDefaultAlphaMask || tintDefaultLogoTextMask ||
                tintDefaultLogoWhitePixels;
            const bool needsTint = tintDefaultNeutralMask || colorsCarryTint;
            if(!needsTint && !yuzuLogoCompositeSource) {
                preparedSourceCache.emplace(tintKey, srcBmp);
                if(profileEnabled) {
                    profileStats.preparedResolveUs +=
                        motionRenderProfileNowUs() - preparedStartUs;
                }
                return srcBmp;
            }

            const auto tintStartUs =
                profileEnabled ? motionRenderProfileNowUs() : 0;
            auto tinted = cloneBitmap32(*srcBmp);
            if(profileEnabled) {
                ++profileStats.tintBuilds;
            }
            if(needsTint) {
                applyPackedCornerTintLike_0x6A7518(
                    *tinted, command.packedColors, useHalfAlphaTint,
                    tintDefaultNeutralMask, tintOnlyWhitePixels,
                    neutralStartupLogoTint(motionPath));
            }
            if(yuzuLogoCompositeSource) {
                recolorYuzuCompositeLogoText(
                    *tinted, yuzuStartupLogoDisplayTextTint(motionPath));
            }
            if(profileEnabled) {
                profileStats.tintBuildUs +=
                    motionRenderProfileNowUs() - tintStartUs;
            }
            if(LOGGER &&
               shouldDebugTitleRender(motionPath, command.sourceKey) &&
               markRenderDebugLogged("tint|" + motionPath + "|" + tintKey)) {
                LOGGER->info(
                    "motion bitmap tint: motion={} source={} halfAlpha={} packed=[0x{:08x},0x{:08x},0x{:08x},0x{:08x}] before=[{}] after=[{}]",
                    motionPath, command.sourceKey, useHalfAlphaTint ? 1 : 0,
                    command.packedColors[0], command.packedColors[1],
                    command.packedColors[2], command.packedColors[3],
                    sampleBitmapStats(srcBmp.get()),
                    sampleBitmapStats(tinted.get()));
            }
            detail::logoChainTraceLogf(
                motionPath, "execute.sourceTint", "0x6C1B70/0x6A7518",
                _clampedEvalTime,
                "source={} halfAlphaTint={} packedColor=[0x{:08x},0x{:08x},0x{:08x},0x{:08x}] bitmap={}x{}",
                command.sourceKey, useHalfAlphaTint ? 1 : 0,
                command.packedColors[0], command.packedColors[1],
                command.packedColors[2], command.packedColors[3],
                tinted->GetWidth(), tinted->GetHeight());
            preparedSourceCache.emplace(tintKey, tinted);
            if(profileEnabled) {
                profileStats.preparedResolveUs +=
                    motionRenderProfileNowUs() - preparedStartUs;
            }
            return tinted;
        };

        const int playerStencilType = _maskMode;
        auto ensureCommandLayer =
            [&](tTJSVariant &slot) -> iTJSDispatch2 * {
            return ensureReusableLayerObject(
                slot,
                scratchOwner,
                scratchParent,
                static_cast<tTVPLayerType>(ltAlpha),
                false);
        };
        auto renderCommandSourceToLayer =
            [&](detail::PlayerRuntime::RenderCommand &command,
                iTJSDispatch2 *targetLayerObject,
                tTJSNI_BaseLayer *targetLayer,
                const std::shared_ptr<tTVPBaseBitmap> &srcBmp,
                const tTVPRect &sourceRect,
                const char *branch) -> bool {
            if(!targetLayerObject || !targetLayer) {
                return false;
            }
            const int clipWidth = command.clipRect[2] - command.clipRect[0];
            const int clipHeight = command.clipRect[3] - command.clipRect[1];
            if(clipWidth <= 0 || clipHeight <= 0) {
                return false;
            }
            if(!prepareLayerForRender(targetLayerObject, clipWidth, clipHeight,
                                      0x00000000)) {
                return false;
            }
            if(!srcBmp || srcBmp->GetWidth() <= 0 || srcBmp->GetHeight() <= 0) {
                return true;
            }
            if(command.meshType == 0) {
                tTVPRect localRect;
                const bool canUseRectCopy =
                    axisAlignedIntegerRectFromCorners(command.localCorners,
                                                      0.5f, 0.5f,
                                                      localRect) &&
                    localRect.get_width() == sourceRect.get_width() &&
                    localRect.get_height() == sourceRect.get_height();
                if(canUseRectCopy) {
                    targetLayer->CopyRect(localRect.left, localRect.top,
                                          srcBmp.get(), nullptr, sourceRect);
                } else {
                    const auto localPts =
                        buildAffineTrianglePoints(command.localCorners, 0.0f,
                                                  0.0f);
                    targetLayer->AffineCopy(localPts.data(), srcBmp.get(),
                                            sourceRect, stLinear,
                                            command.clearEnabled);
                }
            } else {
                if(command.localMeshPoints.empty() || command.meshDivX < 2 ||
                   command.meshDivY < 2) {
                    return false;
                }
                auto localMeshPoints =
                    buildMeshPoints(command.localMeshPoints, 0.0f, 0.0f);
                if(command.meshType == 1) {
                    targetLayer->BezierPatchCopy(
                        localMeshPoints.data(), command.meshDivX,
                        command.meshDivY, srcBmp.get(), sourceRect, stLinear,
                        command.clearEnabled);
                } else if(command.meshType == 2) {
                    targetLayer->MeshCopy(localMeshPoints.data(),
                                          command.meshDivX, command.meshDivY,
                                          srcBmp.get(), sourceRect, stLinear,
                                          command.clearEnabled);
                } else {
                    return false;
                }
            }
            detail::logoChainTraceLogf(
                motionPath, "execute.layerSource", "0x6C7440", _clampedEvalTime,
                "branch={} nodeIndex={} clipRect=[{},{},{},{}] layer={}x{} clearEnabled={}",
                branch, command.nodeIndex,
                command.clipRect[0], command.clipRect[1],
                command.clipRect[2], command.clipRect[3],
                clipWidth, clipHeight, command.clearEnabled ? 1 : 0);
            return true;
        };
        auto chooseCommandOutputLayerObject =
            [&](detail::PlayerRuntime::RenderCommand &command) -> iTJSDispatch2 * {
            if(command.composedBuilt &&
               command.composedLayer.Type() == tvtObject) {
                return command.composedLayer.AsObjectNoAddRef();
            }
            if(command.leafBuilt && command.leafLayer.Type() == tvtObject) {
                return command.leafLayer.AsObjectNoAddRef();
            }
            return nullptr;
        };

        auto buildCommandOutput = [&](auto &&self, size_t commandIndex) -> bool {
            auto &command = _runtime->renderCommands[commandIndex];
            if(command.executedDirect || command.leafBuilt || command.composedBuilt) {
                return true;
            }

            const int clipWidth = command.clipRect[2] - command.clipRect[0];
            const int clipHeight = command.clipRect[3] - command.clipRect[1];
            if(clipWidth <= 0 || clipHeight <= 0) {
                return false;
            }

            auto srcBmp = resolveSourceBitmap(command);
            const bool hasSourceBitmap =
                srcBmp && srcBmp->GetWidth() > 0 && srcBmp->GetHeight() > 0;
            if(!hasSourceBitmap && command.childCommandIndices.empty()) {
                detail::logoChainTraceCheck(
                    motionPath, "execute.source", "0x6C7440",
                    _clampedEvalTime,
                    "resolved bitmap should exist with positive size",
                    fmt::format("nodeIndex={} source={} bitmap={}x{}",
                                command.nodeIndex, command.sourceKey,
                                srcBmp ? srcBmp->GetWidth() : 0,
                                srcBmp ? srcBmp->GetHeight() : 0),
                    false,
                    "sub_6C7440 could not resolve a drawable source bitmap");
                return false;
            }

            const tTVPRect sourceRect(
                0, 0,
                hasSourceBitmap ? static_cast<tjs_int>(srcBmp->GetWidth()) : 0,
                hasSourceBitmap ? static_cast<tjs_int>(srcBmp->GetHeight()) : 0);
            if(hasSourceBitmap) {
                detail::logoChainTraceCheck(
                    motionPath, "execute.srcRect", "0x6C7440",
                    _clampedEvalTime,
                    fmt::format("full texture rect exp=[0,0,{},{}]",
                                srcBmp->GetWidth(), srcBmp->GetHeight()),
                    fmt::format("nodeIndex={} act=[{},{},{},{}]",
                                command.nodeIndex, sourceRect.left,
                                sourceRect.top, sourceRect.right,
                                sourceRect.bottom),
                    true,
                    "sub_6C7440 source rect was not the full texture bounds");
            }

            const bool hasChildren = !command.childCommandIndices.empty();
            const bool useDirectRenderPath =
                shouldUseDirectRenderPathLike_0x6C7440(command) &&
                !hasChildren && command.parentNodeIndex < 0;
            if(useDirectRenderPath) {
                command.executedDirect = true;
                command.builtRect = command.clipRect;
                return true;
            }

            iTJSDispatch2 *leafLayerObject = ensureCommandLayer(command.leafLayer);
            auto *leafLayer = resolveNativeLayer(leafLayerObject);
            if(!leafLayerObject || !leafLayer) {
                detail::logoChainTraceCheck(
                    motionPath, "execute.workLayer", "0x6C7440",
                    _clampedEvalTime,
                    "leaf layer should resolve for buffered item path",
                    fmt::format("nodeIndex={} leafLayer={}",
                                command.nodeIndex,
                                static_cast<const void *>(leafLayer)),
                    false,
                    "sub_6C7440 could not allocate the per-item leaf layer");
                return false;
            }

            if(!renderCommandSourceToLayer(command, leafLayerObject, leafLayer,
                                           srcBmp, sourceRect,
                                           "item.leaf.affineCopy")) {
                return false;
            }
            if(LOGGER &&
               shouldDebugTitleRender(motionPath, command.sourceKey) &&
               markRenderDebugLogged(
                   fmt::format("leaf|{}|{}|{}", motionPath, command.nodeIndex,
                               command.sourceKey))) {
                LOGGER->info(
                    "motion render leaf: motion={} node={} source={} clip=[{},{},{},{}] opacity={} blend={} flags={} src=[{}] leaf=[{}]",
                    motionPath, command.nodeIndex, command.sourceKey,
                    command.clipRect[0], command.clipRect[1],
                    command.clipRect[2], command.clipRect[3],
                    command.opacity, command.blendMode, command.itemFlags,
                    sampleBitmapStats(srcBmp.get()),
                    sampleBitmapStats(leafLayer->GetMainImage()));
            }
            command.leafBuilt = true;
            command.builtRect = command.clipRect;

            bool hasBuiltChildren = false;
            for(const int childCommandIndex : command.childCommandIndices) {
                if(childCommandIndex < 0 ||
                   childCommandIndex >=
                       static_cast<int>(_runtime->renderCommands.size())) {
                    continue;
                }
                hasBuiltChildren =
                    self(self, static_cast<size_t>(childCommandIndex)) ||
                    hasBuiltChildren;
            }

            if(!hasBuiltChildren) {
                return true;
            }

            iTJSDispatch2 *composedLayerObject =
                ensureCommandLayer(command.composedLayer);
            auto *composedLayer = resolveNativeLayer(composedLayerObject);
            if(!composedLayerObject || !composedLayer) {
                detail::logoChainTraceCheck(
                    motionPath, "execute.workLayer", "0x6C7440",
                    _clampedEvalTime,
                    "composed layer should resolve for parent item path",
                    fmt::format("nodeIndex={} composedLayer={}",
                                command.nodeIndex,
                                static_cast<const void *>(composedLayer)),
                    false,
                    "sub_6C7440 could not allocate the composed output layer");
                return false;
            }

            if(!prepareLayerForRender(composedLayerObject, clipWidth, clipHeight,
                                      0x00000000)) {
                return false;
            }
            if(command.leafBuilt) {
                const auto localRect = localRectFromCommand(command);
                composedLayer->CopyRect(0, 0, leafLayer->GetMainImage(),
                                        nullptr, localRect);
            }

            for(const int childCommandIndex : command.childCommandIndices) {
                if(childCommandIndex < 0 ||
                   childCommandIndex >=
                       static_cast<int>(_runtime->renderCommands.size())) {
                    continue;
                }
                auto &child = _runtime->renderCommands[childCommandIndex];
                if((command.itemFlags & 4) != 0) {
                    auto *childMaskLayerObject =
                        child.leafLayer.Type() == tvtObject
                            ? child.leafLayer.AsObjectNoAddRef()
                            : nullptr;
                    if(!childMaskLayerObject) {
                        continue;
                    }
                    const int childWidth = child.builtRect[2] - child.builtRect[0];
                    const int childHeight = child.builtRect[3] - child.builtRect[1];
                    if(childWidth <= 0 || childHeight <= 0) {
                        continue;
                    }
                    applyMotionAlphaMaskLike_0x6AF104(
                        composedLayerObject,
                        child.builtRect[0] - command.clipRect[0],
                        child.builtRect[1] - command.clipRect[1],
                        childMaskLayerObject,
                        0,
                        0,
                        childWidth,
                        childHeight,
                        64,
                        playerStencilType,
                        command.itemFlags,
                        motionPath,
                        _clampedEvalTime,
                        command.nodeIndex,
                        child.nodeIndex);
                    continue;
                }

                auto *childOutputLayerObject = chooseCommandOutputLayerObject(child);
                auto *childOutputLayer = resolveNativeLayer(childOutputLayerObject);
                if(!childOutputLayerObject || !childOutputLayer) {
                    continue;
                }
                const auto childLocalRect = localRectFromCommand(child);
                const auto childBlendMode =
                    resolveBlendOperationModeLike_0x6C7440(child.blendMode);
                const auto childOpacity = static_cast<tjs_int>(
                    std::clamp(child.opacity, 0, 255));
                if(childOpacity <= 0) {
                    continue;
                }
                composedLayer->OperateRect(
                    child.builtRect[0] - command.clipRect[0],
                    child.builtRect[1] - command.clipRect[1],
                    childOutputLayer->GetMainImage(),
                    childLocalRect,
                    childBlendMode,
                    childOpacity);
            }

            command.composedBuilt = true;
            return true;
        };

        for(size_t i = 0; i < _runtime->renderCommands.size(); ++i) {
            auto &command = _runtime->renderCommands[i];
            const auto blendMode =
                resolveBlendOperationModeLike_0x6C7440(command.blendMode);
            const auto effectiveColor =
                unpackPackedRgba(command.packedColors[0]);
            const auto opa = static_cast<tjs_int>(
                std::clamp(command.opacity, 0, 255));
            if(opa <= 0) {
                continue;
            }

            if(!buildCommandOutput(buildCommandOutput, i)) {
                continue;
            }

            try {
            if(command.executedDirect) {
                auto srcBmp = resolveSourceBitmap(command);
                if(!srcBmp || srcBmp->GetWidth() <= 0 ||
                   srcBmp->GetHeight() <= 0) {
                    continue;
                }
                const tTVPRect sourceRect(
                    0, 0, static_cast<tjs_int>(srcBmp->GetWidth()),
                    static_cast<tjs_int>(srcBmp->GetHeight()));
                std::string branch("direct.operateAffine");
                const bool directCopyWithoutOpacity =
                    ((command.blendMode & 0x0F) == 0) && opa >= 255 &&
                    !bitmapHasTransparentPixels(*srcBmp);
                if(command.meshType == 0) {
                    tTVPRect destinationRect;
                    const bool canUseRectCopy =
                        axisAlignedIntegerRectFromCorners(command.worldCorners,
                                                          0.0f, 0.0f,
                                                          destinationRect) &&
                        destinationRect.get_width() == sourceRect.get_width() &&
                        destinationRect.get_height() == sourceRect.get_height();
                    if(canUseRectCopy && directCopyWithoutOpacity) {
                        branch = "direct.copyRect";
                        renderLayer->CopyRect(destinationRect.left,
                                              destinationRect.top,
                                              srcBmp.get(), nullptr,
                                              sourceRect);
                    } else if(canUseRectCopy) {
                        branch = "direct.operateRect";
                        renderLayer->OperateRect(destinationRect.left,
                                                 destinationRect.top,
                                                 srcBmp.get(), sourceRect,
                                                 blendMode, opa);
                    } else {
                        const auto worldPts =
                            buildAffineTrianglePoints(command.worldCorners,
                                                       -0.5f, -0.5f);
                        if(directCopyWithoutOpacity) {
                            branch = "direct.affineCopy";
                            renderLayer->AffineCopy(worldPts.data(), srcBmp.get(),
                                                    sourceRect, stLinear,
                                                    command.clearEnabled);
                        } else {
                            renderLayer->OperateAffine(worldPts.data(),
                                                       srcBmp.get(), sourceRect,
                                                       blendMode, opa,
                                                       stLinear);
                        }
                    }
                } else {
                    if(command.worldMeshPoints.empty() ||
                       command.meshDivX < 2 || command.meshDivY < 2) {
                        continue;
                    }
                        auto worldMeshPoints =
                            buildMeshPoints(command.worldMeshPoints,
                                            -0.5f, -0.5f);
                        if(command.meshType == 1) {
                            if(directCopyWithoutOpacity) {
                                branch = "direct.bezierPatchCopy";
                                renderLayer->BezierPatchCopy(
                                    worldMeshPoints.data(), command.meshDivX,
                                    command.meshDivY, srcBmp.get(), sourceRect,
                                    stLinear, command.clearEnabled);
                            } else {
                                branch = "direct.operateBezierPatch";
                                renderLayer->OperateBezierPatch(
                                    worldMeshPoints.data(), command.meshDivX,
                                    command.meshDivY, srcBmp.get(), sourceRect,
                                    blendMode, opa, stLinear,
                                    command.clearEnabled);
                            }
                        } else if(command.meshType == 2) {
                            if(directCopyWithoutOpacity) {
                                branch = "direct.meshCopy";
                                renderLayer->MeshCopy(
                                    worldMeshPoints.data(), command.meshDivX,
                                    command.meshDivY, srcBmp.get(), sourceRect,
                                    stLinear, command.clearEnabled);
                            } else {
                                branch = "direct.operateMesh";
                                renderLayer->OperateMesh(
                                    worldMeshPoints.data(), command.meshDivX,
                                    command.meshDivY, srcBmp.get(), sourceRect,
                                    blendMode, opa, stLinear,
                                    command.clearEnabled);
                            }
                        } else {
                            continue;
                        }
                    }
                    if(LOGGER &&
                       shouldDebugTitleRender(motionPath, command.sourceKey) &&
                       markRenderDebugLogged(
                           fmt::format("direct-copy|{}|{}|{}",
                                       motionPath, command.nodeIndex,
                                       command.sourceKey))) {
                        LOGGER->info(
                            "motion render direct-copy: motion={} node={} source={} clip=[{},{},{},{}] opacity={} blend={} src=[{}] render=[{}]",
                            motionPath, command.nodeIndex, command.sourceKey,
                            command.clipRect[0], command.clipRect[1],
                            command.clipRect[2], command.clipRect[3],
                            opa, command.blendMode,
                            sampleBitmapStats(srcBmp.get()),
                            sampleBitmapStats(renderLayer->GetMainImage()));
                    }
                    detail::logoChainTraceLogf(
                        motionPath, "execute.copy", "0x6C7440",
                        _clampedEvalTime,
                        "branch={} nodeIndex={} clipRect=[{},{},{},{}] dirtyRect=[{},{},{},{}] blendMode={} opacity={} packedColor=[0x{:08x},0x{:08x},0x{:08x},0x{:08x}] effectiveColor=[{},{},{},{}] visibleAncestorIndex={} clearEnabled={} renderPath=direct workLayer=0x0 renderLayer={}x{}",
                        branch, command.nodeIndex,
                        command.clipRect[0], command.clipRect[1],
                        command.clipRect[2], command.clipRect[3],
                        command.dirtyRect[0], command.dirtyRect[1],
                        command.dirtyRect[2], command.dirtyRect[3],
                        command.blendMode, opa,
                        command.packedColors[0], command.packedColors[1],
                        command.packedColors[2], command.packedColors[3],
                        effectiveColor[0], effectiveColor[1],
                        effectiveColor[2], effectiveColor[3],
                        command.visibleAncestorIndex,
                        command.clearEnabled ? 1 : 0,
                        renderLayer->GetWidth(), renderLayer->GetHeight());
                    if(detail::logoSnapshotMarkEnabledForPath(motionPath) &&
                       isM2StartupLogoMotion(motionPath) &&
                       _clampedEvalTime >= 30.0 && _clampedEvalTime <= 40.0) {
                        std::fprintf(stderr,
                                     "SNAPCOPY order=%d frame=%.3f nodeIndex=%d branch=%s clipRect=[%d,%d,%d,%d] opacity=%d blend=%d\n",
                                     snapshotCopyOrder++, _clampedEvalTime,
                                     command.nodeIndex, branch.c_str(),
                                     command.clipRect[0], command.clipRect[1],
                                     command.clipRect[2], command.clipRect[3],
                                     opa, command.blendMode);
                    }
                    continue;
                }

                auto *outputLayerObject = chooseCommandOutputLayerObject(command);
                auto *outputLayer = resolveNativeLayer(outputLayerObject);
                if(!outputLayerObject || !outputLayer) {
                    continue;
                }

                const auto localRect = localRectFromCommand(command);
                renderLayer->OperateRect(command.clipRect[0], command.clipRect[1],
                                         outputLayer->GetMainImage(), localRect,
                                         blendMode, opa);
                if(LOGGER &&
                   shouldDebugTitleRender(motionPath, command.sourceKey) &&
                   markRenderDebugLogged(
                       fmt::format("buffered-copy|{}|{}|{}",
                                   motionPath, command.nodeIndex,
                                   command.sourceKey))) {
                    LOGGER->info(
                        "motion render buffered-copy: motion={} node={} source={} clip=[{},{},{},{}] opacity={} blend={} output=[{}] render=[{}]",
                        motionPath, command.nodeIndex, command.sourceKey,
                        command.clipRect[0], command.clipRect[1],
                        command.clipRect[2], command.clipRect[3],
                        opa, command.blendMode,
                        sampleBitmapStats(outputLayer->GetMainImage()),
                        sampleBitmapStats(renderLayer->GetMainImage()));
                }
                detail::logoChainTraceLogf(
                    motionPath, "execute.copy", "0x6C7440", _clampedEvalTime,
                    "branch={} nodeIndex={} clipRect=[{},{},{},{}] dirtyRect=[{},{},{},{}] blendMode={} opacity={} packedColor=[0x{:08x},0x{:08x},0x{:08x},0x{:08x}] effectiveColor=[{},{},{},{}] visibleAncestorIndex={} clearEnabled={} renderPath=buffered outputLayer={}x{} renderLayer={}x{} childCount={}",
                    command.composedBuilt ? "buffered.operateRect.composed"
                                          : "buffered.operateRect.leaf",
                    command.nodeIndex,
                    command.clipRect[0], command.clipRect[1],
                    command.clipRect[2], command.clipRect[3],
                    command.dirtyRect[0], command.dirtyRect[1],
                    command.dirtyRect[2], command.dirtyRect[3],
                    command.blendMode, opa,
                    command.packedColors[0], command.packedColors[1],
                    command.packedColors[2], command.packedColors[3],
                    effectiveColor[0], effectiveColor[1],
                    effectiveColor[2], effectiveColor[3],
                    command.visibleAncestorIndex,
                    command.clearEnabled ? 1 : 0,
                    localRect.get_width(), localRect.get_height(),
                    renderLayer->GetWidth(), renderLayer->GetHeight(),
                    command.childCommandIndices.size());
                if(detail::logoSnapshotMarkEnabledForPath(motionPath) &&
                   isM2StartupLogoMotion(motionPath) &&
                   _clampedEvalTime >= 30.0 && _clampedEvalTime <= 40.0) {
                    const char *snapBranch = command.composedBuilt
                        ? "buffered.operateRect.composed"
                        : "buffered.operateRect.leaf";
                    std::fprintf(stderr,
                                 "SNAPCOPY order=%d frame=%.3f nodeIndex=%d branch=%s clipRect=[%d,%d,%d,%d] opacity=%d blend=%d childCount=%zu\n",
                                 snapshotCopyOrder++, _clampedEvalTime,
                                 command.nodeIndex, snapBranch,
                                 command.clipRect[0], command.clipRect[1],
                                 command.clipRect[2], command.clipRect[3],
                                 opa, command.blendMode,
                                 command.childCommandIndices.size());
                }
            } catch(const eTJS &) {
            } catch(...) {
            }
        }

        if(!skipUpdate) {
            renderLayer->Update(false);
            detail::logoChainTraceLogf(
                motionPath, "execute.update", "0x6C7440", _clampedEvalTime,
                "renderLayer.Update(false) size={}x{}",
                renderLayer->GetWidth(), renderLayer->GetHeight());
        }
        if(profileEnabled && LOGGER) {
            LOGGER->info(
                "motion render profile: motion={} frame={:.2f} target={} native={} commands={} cache=base:{}/{} prepared:{}/{} loads=storage:{} psb:{} tintBuilds={} us=total:{} base:{} prepared:{} tint:{}",
                motionPath, _clampedEvalTime,
                static_cast<const void *>(renderLayerObject),
                static_cast<const void *>(renderLayer),
                _runtime->renderCommands.size(),
                profileStats.baseHits, profileStats.baseMisses,
                profileStats.preparedHits, profileStats.preparedMisses,
                profileStats.storageLoads, profileStats.psbLoads,
                profileStats.tintBuilds,
                motionRenderProfileNowUs() - profileStartUs,
                profileStats.baseResolveUs, profileStats.preparedResolveUs,
                profileStats.tintBuildUs);
        }
        return true;
    }

    iTJSDispatch2 *Player::resolveSeparateLayerRenderTarget(
        SeparateLayerAdaptor *sla,
        iTJSDispatch2 *fallbackOwner,
        int &canvasWidth,
        int &canvasHeight) {
        canvasWidth = 0;
        canvasHeight = 0;
        if(!sla) {
            return nullptr;
        }

        iTJSDispatch2 *targetLayerObject = nullptr;
        if(auto *resolved = tryResolveLayerDispatch(sla->getTargetLayer())) {
            targetLayerObject = resolved;
        }
        if(!targetLayerObject) {
            targetLayerObject = fallbackOwner;
        }
        if(!targetLayerObject) {
            return nullptr;
        }

        sla->setTargetLayer(tTJSVariant(targetLayerObject, targetLayerObject));
        if(!queryLayerCanvasSize(targetLayerObject, canvasWidth, canvasHeight)) {
            return nullptr;
        }

        iTJSDispatch2 *renderTarget = ensureReusableLayerObject(
            sla->privateRenderTargetSlot(),
            resolveLayerTreeOwnerObject(targetLayerObject),
            targetLayerObject,
            static_cast<tTVPLayerType>(ltAlpha),
            true,
            sla->getAbsolute());
        if(!renderTarget) {
            return nullptr;
        }

        sla->setPrivateRenderTarget(tTJSVariant(renderTarget, renderTarget));
        if(auto *renderLayer = resolveNativeLayer(renderTarget)) {
            renderLayer->SetSize(canvasWidth, canvasHeight);
            renderLayer->SetVisible(true);
        }
        return renderTarget;
    }

    bool Player::renderMotionFrameToTarget(iTJSDispatch2 *renderTargetObject,
                                           tjs_int canvasWidth,
                                           tjs_int canvasHeight,
                                           const char *traceFunc) {
        if(!renderTargetObject || canvasWidth <= 0 || canvasHeight <= 0) {
            return false;
        }
        if(!prepareLayerForRender(renderTargetObject, canvasWidth, canvasHeight,
                                  0x00000000)) {
            return false;
        }

        const auto motionPath =
            _runtime && _runtime->activeMotion ? _runtime->activeMotion->path
                                               : std::string{};
        detail::logoChainTraceLogf(
            motionPath, "sla.renderMotionFrame", "0x6DE738",
            _clampedEvalTime,
            "target={} canvas={}x{} route={}",
            static_cast<const void *>(renderTargetObject),
            canvasWidth, canvasHeight,
            traceFunc ? traceFunc : "0x6DE738");

        buildRenderCommands(canvasWidth, canvasHeight);
        return executeLayerRenderCommands(renderTargetObject, true);
    }

    bool Player::renderToD3DAdaptor(D3DAdaptor *adaptor) {
        if(!adaptor || adaptor->getWidth() <= 0 || adaptor->getHeight() <= 0) {
            return false;
        }
        // Guard against recursion: D3D capture can re-enter drawCompat.
        static bool s_inRenderToD3D = false;
        if(s_inRenderToD3D) return false;
        s_inRenderToD3D = true;
        struct Guard { ~Guard() { s_inRenderToD3D = false; } } guard;

        ensureMotionLoaded();
        if(!_runtime->activeMotion) return false;
        const auto motionPath = _runtime->activeMotion->path;
        detail::logoChainTraceLogf(
            motionPath, "draw.d3d", "0x6D5B90", _clampedEvalTime,
            "adaptorSize={}x{} route=D3DAdaptor_renderFromPlayer",
            adaptor->getWidth(), adaptor->getHeight());

        ensureNodeTreeBuilt();
        prepareRenderItems();
        applyPreparedRenderItemTranslateOffsets();
        adjustPreparedRenderItemsForYuzuPresentation(
            *_runtime, motionPath, adaptor->getWidth(), adaptor->getHeight());
        adjustPreparedRenderItemsForCenteredGameMotion(
            *_runtime, motionPath, adaptor->getWidth(), adaptor->getHeight(),
            resolveCenteredGameMotionResolution(_resolution, _tags, _metadata,
                                                motionPath));

        iTJSDispatch2 *renderLayerObject =
            ensureReusableLayerObject(_runtime->internalRenderLayer,
                                      adaptor->getWindowObject(),
                                      nullptr,
                                      static_cast<tTVPLayerType>(ltAlpha),
                                      false);
        if(!renderLayerObject) {
            return false;
        }
        if(!prepareLayerForRender(renderLayerObject, adaptor->getWidth(),
                                  adaptor->getHeight(), 0x00000000)) {
            return false;
        }

        buildRenderCommands(adaptor->getWidth(), adaptor->getHeight());
        executeLayerRenderCommands(renderLayerObject, true);

        // D3D backend still ends with copying pixels into the adaptor buffer,
        // but it now consumes prepared items directly instead of recursing into
        // renderToLayer().
        tTJSNI_BaseLayer *layer = nullptr;
        if(TJS_FAILED(renderLayerObject->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&layer))) || !layer) {
            return false;
        }

        const int w = adaptor->getWidth();
        const int h = adaptor->getHeight();
        const int layerW = static_cast<int>(layer->GetImageWidth());
        const int layerH = static_cast<int>(layer->GetImageHeight());
        const auto *srcBuf = reinterpret_cast<const std::uint8_t *>(
            layer->GetMainImagePixelBuffer());
        auto srcPitch = layer->GetMainImagePixelBufferPitch();

        if(!srcBuf || srcPitch <= 0 || layerW <= 0 || layerH <= 0) return false;

        // Resize adaptor buffer if needed
        if(w != layerW || h != layerH) {
            adaptor->setSize(layerW, layerH);
        }
        adaptor->clearBuffer();

        auto *dstBuf = adaptor->getBuffer();
        const auto dstPitch = adaptor->getBufferPitch();
        const int copyH = std::min(layerH, adaptor->getHeight());
        const int copyRowBytes = std::min(
            static_cast<int>(layerW * 4), dstPitch);

        for(int y = 0; y < copyH; ++y) {
            std::memcpy(dstBuf + dstPitch * y,
                        srcBuf + srcPitch * y,
                        static_cast<size_t>(copyRowBytes));
        }

        return true;
    }

    bool Player::renderToLayer(iTJSDispatch2 *layerObject, bool skipUpdate) {
        if(!layerObject) {
            return false;
        }

        ensureMotionLoaded();
        if(!_runtime->activeMotion) {
            return false;
        }
        const auto motionPath = _runtime->activeMotion->path;

        iTJSDispatch2 *resolvedLayerObject = layerObject;
        tTJSVariant wrapper(layerObject, layerObject);
        if(auto *resolved = tryResolveLayerDispatch(wrapper)) {
            resolvedLayerObject = resolved;
        }

        int canvasWidth = 0;
        int canvasHeight = 0;
        const bool queriedCanvas =
            queryLayerCanvasSize(resolvedLayerObject, canvasWidth, canvasHeight);
        if(!queriedCanvas && _runtime->activeMotion) {
            canvasWidth = static_cast<int>(_runtime->activeMotion->width);
            canvasHeight = static_cast<int>(_runtime->activeMotion->height);
        }
        if(canvasWidth <= 0 || canvasHeight <= 0) {
            return false;
        }
        detail::logoChainTraceLogf(
            motionPath, "draw.layer", "0x6C7440/0x6CE7D8", _clampedEvalTime,
            "targetLayerCanvas={}x{} skipUpdate={} needsInternalAssignImages={}",
            canvasWidth, canvasHeight, skipUpdate ? 1 : 0,
            _needsInternalAssignImages ? 1 : 0);

        iTJSDispatch2 *finalLayerObject = resolvedLayerObject;
        tTJSNI_BaseLayer *finalNativeLayer = resolveNativeLayer(finalLayerObject);
        const bool yuzuTitlePresentation =
            isYuzuTitlePresentationMotion(motionPath);
        const bool yuzuLogoPresentation =
            isYuzuLogoPresentationMotion(motionPath);
        if(yuzuTitlePresentation && _runtime->activeMotion &&
           _runtime->activeMotion->width > 0 &&
           _runtime->activeMotion->height > 0) {
            canvasWidth = static_cast<int>(_runtime->activeMotion->width);
            canvasHeight = static_cast<int>(_runtime->activeMotion->height);
        }
        const bool allowTransientPresentationLayer =
            yuzuLogoPresentation && !yuzuTitlePresentation;
        tTJSNI_BaseLayer *presentationChild = nullptr;
        if(yuzuTitlePresentation) {
            presentationChild = findMotionPresentationChild(
                finalNativeLayer, canvasWidth, canvasHeight, true);
            if(!presentationChild) {
                presentationChild = findYuzuTitlePresentationLayer(
                    finalNativeLayer, canvasWidth, canvasHeight);
            }
        } else if(yuzuLogoPresentation) {
            presentationChild = findMotionPresentationChild(
                finalNativeLayer, canvasWidth, canvasHeight,
                allowTransientPresentationLayer);
        } else {
            presentationChild = findGenericMotionPresentationChild(
                finalNativeLayer, canvasWidth, canvasHeight);
        }
        if(presentationChild) {
            if(auto *presentationObject = presentationChild->GetOwnerNoAddRef()) {
                finalLayerObject = presentationObject;
                finalNativeLayer = presentationChild;
                detail::logoChainTraceLogf(
                    motionPath, "draw.layer.presentationTarget",
                    "KAGWorldPlugin/Yuzu layer adaptor", _clampedEvalTime,
                    "source=[{}] target=[{}] canvas={}x{} transientAllowed={}",
                    describeLayerForDebug(resolveNativeLayer(resolvedLayerObject)),
                    describeLayerForDebug(finalNativeLayer),
                    canvasWidth, canvasHeight,
                    allowTransientPresentationLayer ? 1 : 0);
                if(LOGGER && shouldDebugTitleRender(motionPath) &&
                   markRenderDebugLogged("presentation-target:" + motionPath)) {
                    LOGGER->info(
                        "motion presentation target: motion={} source=[{}] target=[{}] transientAllowed={}",
                        motionPath,
                        describeLayerForDebug(resolveNativeLayer(resolvedLayerObject)),
                        describeLayerForDebug(finalNativeLayer),
                        allowTransientPresentationLayer ? 1 : 0);
                }
            }
        }
        ensureNodeTreeBuilt();
        prepareRenderItems();
        applyPreparedRenderItemTranslateOffsets();
        adjustPreparedRenderItemsForYuzuPresentation(
            *_runtime, motionPath, canvasWidth, canvasHeight);
        adjustPreparedRenderItemsForCenteredGameMotion(
            *_runtime, motionPath, canvasWidth, canvasHeight,
            resolveCenteredGameMotionResolution(_resolution, _tags, _metadata,
                                                motionPath));
        if(yuzuTitlePresentation) {
            logYuzuTitlePreparedSummary(*_runtime, motionPath, _frameTickCount);
        }
        const bool yuzuTitleHasFinalFrame =
            yuzuTitlePresentation &&
            hasYuzuTitleFinalPresentationFrame(*_runtime);
        if(yuzuTitlePresentation &&
           !hasYuzuTitleRenderablePresentationFrame(*_runtime)) {
            if(LOGGER && shouldDebugTitleRender(motionPath) &&
               markRenderDebugLogged("presentation-skip-empty:" + motionPath)) {
                LOGGER->info(
                    "motion presentation skip empty title frame: motion={} target=[{}]",
                    motionPath, describeLayerForDebug(finalNativeLayer));
            }
            _runtime->clearPresentationRenderReuse();
            invalidateGlobalPresentationRenderTarget(finalLayerObject);
            _runtime->lastCanvas =
                tTJSVariant(resolvedLayerObject, resolvedLayerObject);
            return true;
        }
        if(yuzuTitlePresentation &&
           _runtime->yuzuTitleFinalFrameRendered &&
           !yuzuTitleHasFinalFrame) {
            if(LOGGER && shouldDebugTitleRender(motionPath) &&
               markRenderDebugLogged("presentation-skip-tail:" + motionPath)) {
                LOGGER->info(
                    "motion presentation skip title tail frame: motion={} target=[{}]",
                    motionPath, describeLayerForDebug(finalNativeLayer));
            }
            _runtime->clearPresentationRenderReuse();
            invalidateGlobalPresentationRenderTarget(finalLayerObject);
            _runtime->lastCanvas =
                tTJSVariant(resolvedLayerObject, resolvedLayerObject);
            return true;
        }

        const bool usingPresentationTarget =
            finalLayerObject && finalLayerObject != resolvedLayerObject;

        const bool bypassInternalAssignForYuzuTitle =
            yuzuTitlePresentation && _needsInternalAssignImages && !skipUpdate;
        iTJSDispatch2 *renderLayerObject = finalLayerObject;
        if(_needsInternalAssignImages && !skipUpdate &&
           !bypassInternalAssignForYuzuTitle) {
            auto *treeOwner = resolveLayerTreeOwnerObject(finalLayerObject);
            if(!treeOwner) {
                treeOwner = resolveLayerTreeOwnerObject(resolvedLayerObject);
            }
            renderLayerObject = ensureReusableLayerObject(
                _runtime->internalRenderLayer,
                treeOwner,
                finalLayerObject,
                static_cast<tTVPLayerType>(ltAlpha),
                false);
        }
        if(bypassInternalAssignForYuzuTitle && LOGGER &&
           shouldDebugTitleRender(motionPath) &&
           markRenderDebugLogged("presentation-direct-target:" + motionPath)) {
            LOGGER->info(
                "motion presentation direct target: motion={} target=[{}]",
                motionPath, describeLayerForDebug(finalNativeLayer));
        }

        buildRenderCommands(canvasWidth, canvasHeight);
        const auto presentationCommandSignature =
            renderCommandReuseSignature(_runtime->renderCommands);
        const bool canReuseYuzuPresentation =
            (yuzuTitlePresentation || yuzuLogoPresentation);
        const bool canReusePresentationRender =
            canReuseYuzuPresentation &&
            renderLayerObject == finalLayerObject && renderLayerObject != nullptr &&
            !_runtime->renderCommands.empty();
        const auto presentationReuseNowUs =
            canReusePresentationRender ? motionRenderProfileNowUs() : 0;
        if(canReusePresentationRender) {
            auto cacheIt =
                _runtime->presentationRenderCache.find(renderLayerObject);
            if(cacheIt != _runtime->presentationRenderCache.end()) {
                const auto &entry = cacheIt->second;
                if(presentationRenderEntryMatches(
                       entry, motionPath, _clampedEvalTime, canvasWidth,
                       canvasHeight, presentationCommandSignature)) {
                    ++_runtime->presentationRenderReuseSkips;
                    if(motionRenderProfileEnabled() && LOGGER) {
                        LOGGER->info(
                            "motion render reuse: motion={} frame={:.2f} target={} canvas={}x{} commands={} skips={} scope=runtime",
                            motionPath, _clampedEvalTime,
                            static_cast<const void *>(renderLayerObject),
                            canvasWidth, canvasHeight,
                            _runtime->renderCommands.size(),
                            _runtime->presentationRenderReuseSkips);
                    }
                    _runtime->lastCanvas =
                        tTJSVariant(resolvedLayerObject, resolvedLayerObject);
                    return true;
                }
            }
            auto &globalCache = globalPresentationRenderCache();
            auto globalIt = globalCache.find(renderLayerObject);
	            if(globalIt != globalCache.end() &&
	               presentationReuseNowUs >= globalIt->second.storedUs &&
	               presentationReuseNowUs - globalIt->second.storedUs <=
	                   kGlobalPresentationReuseTtlUs &&
	               presentationRenderEntryMatches(
	                   globalIt->second, motionPath, _clampedEvalTime, canvasWidth,
	                   canvasHeight, presentationCommandSignature)) {
                ++_runtime->presentationRenderReuseSkips;
                _runtime->presentationRenderCache[renderLayerObject] = {
                    motionPath,
                    _clampedEvalTime,
                    canvasWidth,
                    canvasHeight,
                    presentationCommandSignature,
                };
                if(motionRenderProfileEnabled() && LOGGER) {
                    LOGGER->info(
                        "motion render reuse: motion={} frame={:.2f} target={} canvas={}x{} commands={} skips={} scope=global age_us={}",
                        motionPath, _clampedEvalTime,
                        static_cast<const void *>(renderLayerObject), canvasWidth,
                        canvasHeight, _runtime->renderCommands.size(),
                        _runtime->presentationRenderReuseSkips,
                        presentationReuseNowUs - globalIt->second.storedUs);
                }
	                _runtime->lastCanvas =
	                    tTJSVariant(resolvedLayerObject, resolvedLayerObject);
	                return true;
	            }
	            auto sourceIt = findGlobalPresentationRenderSource(
	                globalCache, renderLayerObject, motionPath, _clampedEvalTime,
	                canvasWidth, canvasHeight, presentationCommandSignature,
	                presentationReuseNowUs);
	            if(sourceIt != globalCache.end() &&
	               copyGlobalPresentationRender(
	                   renderLayerObject, usingPresentationTarget, canvasWidth,
	                   canvasHeight, sourceIt->second)) {
	                ++_runtime->presentationRenderReuseSkips;
	                _runtime->presentationRenderCache[renderLayerObject] = {
	                    motionPath,
	                    _clampedEvalTime,
	                    canvasWidth,
	                    canvasHeight,
	                    presentationCommandSignature,
	                };
	                globalCache[renderLayerObject] =
	                    makeGlobalPresentationRenderCacheEntry(
	                        renderLayerObject, motionPath, _clampedEvalTime,
	                        canvasWidth, canvasHeight,
	                        presentationCommandSignature, presentationReuseNowUs);
	                if(motionRenderProfileEnabled() && LOGGER) {
	                    LOGGER->info(
	                        "motion render reuse: motion={} frame={:.2f} target={} source={} canvas={}x{} commands={} skips={} scope=global-copy age_us={}",
	                        motionPath, _clampedEvalTime,
	                        static_cast<const void *>(renderLayerObject),
	                        static_cast<const void *>(sourceIt->first),
	                        canvasWidth, canvasHeight,
	                        _runtime->renderCommands.size(),
	                        _runtime->presentationRenderReuseSkips,
	                        presentationReuseNowUs - sourceIt->second.storedUs);
	                }
	                _runtime->lastCanvas =
	                    tTJSVariant(resolvedLayerObject, resolvedLayerObject);
	                return true;
	            }
	        }

        if(renderLayerObject != finalLayerObject) {
            if(!prepareLayerForRender(renderLayerObject, canvasWidth, canvasHeight,
                                      0x00000000)) {
                _runtime->invalidatePresentationRenderTarget(renderLayerObject);
                invalidateGlobalPresentationRenderTarget(renderLayerObject);
                return false;
            }
        } else if(auto *targetLayer = resolveNativeLayer(finalLayerObject)) {
            if(usingPresentationTarget) {
                if(!prepareMotionPresentationLayerForRender(targetLayer, canvasWidth,
                                                            canvasHeight)) {
                    _runtime->invalidatePresentationRenderTarget(renderLayerObject);
                    invalidateGlobalPresentationRenderTarget(renderLayerObject);
                    return false;
                }
            } else {
                if(targetLayer->GetWidth() != canvasWidth ||
                   targetLayer->GetHeight() != canvasHeight) {
                    targetLayer->SetSize(canvasWidth, canvasHeight);
                }
            }
        } else {
            _runtime->invalidatePresentationRenderTarget(renderLayerObject);
            invalidateGlobalPresentationRenderTarget(renderLayerObject);
            return false;
        }

        if(!executeLayerRenderCommands(renderLayerObject, true)) {
            _runtime->invalidatePresentationRenderTarget(renderLayerObject);
            invalidateGlobalPresentationRenderTarget(renderLayerObject);
            return false;
        }
        if(canReusePresentationRender) {
            _runtime->presentationRenderCache[renderLayerObject] = {
                motionPath,
                _clampedEvalTime,
                canvasWidth,
                canvasHeight,
                presentationCommandSignature,
            };
	            globalPresentationRenderCache()[renderLayerObject] =
	                makeGlobalPresentationRenderCacheEntry(
	                    renderLayerObject, motionPath, _clampedEvalTime,
	                    canvasWidth, canvasHeight, presentationCommandSignature,
	                    presentationReuseNowUs ? presentationReuseNowUs
	                                           : motionRenderProfileNowUs());
	        } else {
            _runtime->invalidatePresentationRenderTarget(renderLayerObject);
            invalidateGlobalPresentationRenderTarget(renderLayerObject);
        }
        auto *renderLayer = resolveNativeLayer(renderLayerObject);
        if(LOGGER && shouldDebugTitleRender(motionPath) &&
           markRenderDebugLogged("render-target-check:" + motionPath)) {
            LOGGER->info(
                "motion render target check: motion={} object={} native={}",
                motionPath,
                static_cast<const void *>(renderLayerObject),
                static_cast<const void *>(renderLayer));
        }
        if(renderLayer && LOGGER && shouldDebugTitleRender(motionPath)) {
            static std::unordered_map<std::string, int> treeLogCountByMotion;
            auto &treeLogCount = treeLogCountByMotion[motionPath];
            if(treeLogCount < 2) {
                ++treeLogCount;
                try {
                    std::ostringstream out;
                    out << "motion render target tree: motion=" << motionPath
                        << " renderLayer="
                        << static_cast<const void *>(renderLayer)
                        << " layer=" << describeLayerForDebug(renderLayer)
                        << " ancestry="
                        << describeLayerAncestryForDebug(renderLayer);
                    describeLayerTreeForDebug(out, renderLayer);
                    LOGGER->info("{}", out.str());
                } catch(const std::exception &e) {
                    LOGGER->warn("motion render target tree failed: motion={} error={}",
                                 motionPath, e.what());
                } catch(...) {
                    LOGGER->warn("motion render target tree failed: motion={} error=<unknown>",
                                 motionPath);
                }
            }
        }

        if(!skipUpdate) {
            if(renderLayerObject != finalLayerObject) {
                updateLayerAfterDraw(finalLayerObject);
            } else if(auto *layer = resolveNativeLayer(finalLayerObject)) {
	                layer->Update(false);
	                detail::logoChainTraceLogf(
	                    motionPath, "post.layer", "0x6CE7D8", _clampedEvalTime,
	                    "targetLayer.Update(false) size={}x{}",
                    layer->GetWidth(), layer->GetHeight());
            }
        }
        if(bypassInternalAssignForYuzuTitle) {
            _needsInternalAssignImages = false;
        }
        if(yuzuTitleHasFinalFrame) {
            _runtime->yuzuTitleFinalFrameRendered = true;
        }

        if(yuzuTitlePresentation && !_presentationHoldRendering &&
           finalLayerObject) {
            // Keep a short tail for the last animation frame; a multi-second
            // hold can leave title_bg above later scene layers during startup.
            enablePresentationHold(finalLayerObject, 250);
        }

        _runtime->lastCanvas =
            tTJSVariant(resolvedLayerObject, resolvedLayerObject);
        detail::logoChainTraceSummary(
            motionPath, "renderToLayer", _clampedEvalTime,
            skipUpdate ? "skipUpdate=1" : "skipUpdate=0");
        return true;
    }

    bool Player::renderToSeparateLayerAdaptor(iTJSDispatch2 *slaObject) {
        if(!slaObject || !_runtime) {
            return false;
        }

        SeparateLayerAdaptor *sla =
            ncbInstanceAdaptor<SeparateLayerAdaptor>::GetNativeInstance(
                slaObject, false);
        iTJSDispatch2 *ownerLayer = sla ? sla->getOwner() : nullptr;
        if(!ownerLayer) {
            ownerLayer = tryResolveSeparateAdaptorOwner(tTJSVariant(slaObject, slaObject));
        }
        if(!ownerLayer) {
            return false;
        }

        ensureMotionLoaded();
        if(!_runtime->activeMotion) {
            return false;
        }
        const auto motionPath = _runtime->activeMotion->path;

        int canvasWidth = 0;
        int canvasHeight = 0;
        iTJSDispatch2 *renderTarget =
            resolveSeparateLayerRenderTarget(sla, ownerLayer, canvasWidth,
                                             canvasHeight);
        if(!renderTarget) {
            detail::logoChainTraceSummary(
                motionPath, "renderToSeparateLayerAdaptor", _clampedEvalTime,
                "fail=resolveSeparateLayerRenderTarget");
            return false;
        }
        detail::logoChainTraceLogf(
            motionPath, "draw.sla", "0x6D5658", _clampedEvalTime,
            "ownerLayer={} targetCanvas={}x{} accurate={} route={}",
            static_cast<const void *>(ownerLayer),
            canvasWidth, canvasHeight,
            isAccurateSlaRenderEnabled() ? 1 : 0,
            isAccurateSlaRenderEnabled()
                ? "0x6C9CA8 -> 0x6CE938"
                : "Player_RenderMotionFrame -> Layer_UpdateRect");
        detail::logoChainTraceLogf(
            motionPath, "sla.resolveTarget", "0x6D5948",
            _clampedEvalTime,
            "targetLayer={} privateTarget={} absolute={} canvas={}x{}",
            static_cast<const void *>(tryResolveLayerDispatch(sla->getTargetLayer())),
            static_cast<const void *>(renderTarget),
            sla->getAbsolute() ? 1 : 0,
            canvasWidth, canvasHeight);

        ensureNodeTreeBuilt();
        prepareRenderItems();
        applyPreparedRenderItemTranslateOffsets();
        adjustPreparedRenderItemsForYuzuPresentation(
            *_runtime, motionPath, canvasWidth, canvasHeight);
        adjustPreparedRenderItemsForCenteredGameMotion(
            *_runtime, motionPath, canvasWidth, canvasHeight,
            resolveCenteredGameMotionResolution(_resolution, _tags, _metadata,
                                                motionPath));

        if(!renderMotionFrameToTarget(renderTarget, canvasWidth, canvasHeight,
                                      isAccurateSlaRenderEnabled()
                                          ? "0x6C9CA8"
                                          : "0x6DE738")) {
            detail::logoChainTraceSummary(
                motionPath, "renderToSeparateLayerAdaptor", _clampedEvalTime,
                "fail=renderMotionFrameToTarget");
            return false;
        }

        if(isAccurateSlaRenderEnabled()) {
            detail::logoChainTraceLogf(
                motionPath, "sla.accurate.begin", "0x6C9CA8",
                _clampedEvalTime,
                "target={} canvas={}x{}",
                static_cast<const void *>(renderTarget),
                canvasWidth, canvasHeight);
            updateAccurateSLAAfterDraw(renderTarget);
            detail::logoChainTraceLogf(
                motionPath, "sla.accurate.end", "0x6CE938",
                _clampedEvalTime,
                "target={}", static_cast<const void *>(renderTarget));
        } else if(auto *renderLayer = resolveNativeLayer(renderTarget)) {
            renderLayer->Update(false);
            detail::logoChainTraceLogf(
                motionPath, "sla.updateRect", "0x800F4C", _clampedEvalTime,
                "renderTarget.Update(false) size={}x{} ownerLayer={}",
                renderLayer->GetWidth(), renderLayer->GetHeight(),
                static_cast<const void *>(ownerLayer));
        } else {
            detail::logoChainTraceCheck(
                motionPath, "sla.updateRect", "0x800F4C", _clampedEvalTime,
                "renderTarget should expose a native layer for Update(false)",
                "renderTarget native layer missing", false,
                "Player_RenderMotionFrame finished but SLA target lacked a native layer");
        }

        _runtime->lastCanvas = tTJSVariant(ownerLayer, ownerLayer);
        detail::logoChainTraceSummary(
            motionPath, "renderToSeparateLayerAdaptor", _clampedEvalTime,
            isAccurateSlaRenderEnabled() ? "accurate=1" : "accurate=0");
        return true;
    }

    bool Player::updateLayerAfterDraw(iTJSDispatch2 *targetLayerObject) {
        if(!_needsInternalAssignImages) {
            return true;
        }
        const auto motionPath =
            _runtime && _runtime->activeMotion ? _runtime->activeMotion->path
                                               : std::string{};

        _needsInternalAssignImages = false;
        if(!targetLayerObject) {
            return false;
        }

        iTJSDispatch2 *renderLayerObject =
            _runtime->internalRenderLayer.Type() == tvtObject
                ? _runtime->internalRenderLayer.AsObjectNoAddRef()
                : nullptr;
        if(!renderLayerObject) {
            return false;
        }

        try {
            tTJSVariant targetVar(targetLayerObject, targetLayerObject);
            tTJSVariant *args[] = { &targetVar };
            const bool ok = TJS_SUCCEEDED(renderLayerObject->FuncCall(
                0, TJS_W("assignImages"), nullptr, nullptr, 1, args,
                renderLayerObject));
            detail::logoChainTraceCheck(
                motionPath, "post.assignImages", "0x6CE7D8",
                _clampedEvalTime,
                "internal render layer assignImages(targetLayer)",
                ok ? "assignImages(targetLayer)" : "assignImages(failed)",
                ok,
                "sub_6CE7D8 failed to assign internal render layer to target");
            return ok;
        } catch(...) {
            detail::logoChainTraceCheck(
                motionPath, "post.assignImages", "0x6CE7D8",
                _clampedEvalTime,
                "internal render layer assignImages(targetLayer)",
                "assignImages(threw)", false,
                "sub_6CE7D8 threw while assigning internal render layer");
            return false;
        }
    }

    bool Player::updateAccurateSLAAfterDraw(iTJSDispatch2 *targetLayerObject) {
        if(!targetLayerObject) {
            return false;
        }
        const auto motionPath =
            _runtime && _runtime->activeMotion ? _runtime->activeMotion->path
                                               : std::string{};

        if(auto *layer = resolveNativeLayer(targetLayerObject)) {
            layer->Update(false);
            detail::logoChainTraceLogf(
                motionPath, "post.sla.accurate", "0x6C9CA8/0x6CE938",
                _clampedEvalTime,
                "route=renderTarget.Update(false) size={}x{}",
                layer->GetWidth(), layer->GetHeight());
            return true;
        }
        detail::logoChainTraceCheck(
            motionPath, "post.sla.accurate", "0x6C9CA8/0x6CE938",
            _clampedEvalTime,
            "accurate SLA should update the resolved render target",
            "no post-update target", false,
            "accurate SLA render finished without a target update");
        return false;
    }

    tTJSVariant Player::findSource(ttstr name) {
        loadSource(name);
        const auto key = detail::narrow(name);
        if(const auto it = _runtime->sourcesByKey.find(key);
           it != _runtime->sourcesByKey.end()) {
            return it->second;
        }
        return {};
    }

    void Player::loadSource(ttstr name) {
        const auto requestKey = detail::narrow(name);
        if(requestKey.empty() ||
           _runtime->sourcesByKey.find(requestKey) !=
               _runtime->sourcesByKey.end()) {
            return;
        }

        ttstr resolved;
        if(!detail::resolveExistingPath(buildSourceCandidates(*_runtime, name),
                                        resolved)) {
            return;
        }

        const auto resolvedKey = detail::narrow(resolved);
        if(const auto existing = _runtime->sourcesByKey.find(resolvedKey);
           existing != _runtime->sourcesByKey.end()) {
            _runtime->sourcesByKey.emplace(requestKey, existing->second);
            return;
        }

        const auto source = _resourceManagerNative.load(resolved);
        if(source.Type() == tvtVoid) {
            return;
        }

        _runtime->sourcesByKey.emplace(requestKey, source);
        _runtime->sourcesByKey.emplace(resolvedKey, source);
    }

    void Player::clearCache() {
        _runtime->sourcesByKey.clear();
        _runtime->clearMotionBitmapCaches();
        _runtime->lastCanvas.Clear();
    }

    void Player::setSize(tjs_int w, tjs_int h) {
        _runtime->width = w;
        _runtime->height = h;
    }

    void Player::copyRect(tTJSVariant) {}

    void Player::adjustGamma(tTJSVariant) {}

    void Player::draw() {
        // Keep the no-arg C++ method as a lightweight prepare pass. The real
        // libkrkr2.so draw dispatch happens in drawCompat based on argument type.
        if(!_runtime->visible) {
            _runtime->lastCanvas.Clear();
            return;
        }

        ensureMotionLoaded();
        ensureNodeTreeBuilt();
        calcViewParam();
        prepareRenderItems();
        if(_canvasCaptureEnabled) {
            _runtime->lastCanvas = makeCanvasSummary(*_runtime);
        }
    }

    void Player::scheduleTimelineControlAnimatorLike_0x671A50(
        detail::TimelineState &state, size_t trackIndex, float value,
        double transition, double easeWeight) {
        if(trackIndex >= state.controlTrackAnimators.size()) {
            state.controlTrackAnimators.resize(trackIndex + 1);
        }
        if(trackIndex >= state.controlTrackValues.size()) {
            state.controlTrackValues.resize(trackIndex + 1, 0.0f);
        }

        auto &animator = state.controlTrackAnimators[trackIndex];
        const float targetValue = value;
        if(transition <= 0.0) {
            animator.queue.clear();
            animator.active = false;
            animator.currentValue = targetValue;
            animator.startValue = targetValue;
            animator.targetValue = targetValue;
            animator.progress = 1.0f;
            animator.duration = 0.0f;
            animator.weight = static_cast<float>(easeWeight);
            state.controlTrackValues[trackIndex] = targetValue;
            return;
        }

        animator.queue.push_back(detail::TimelineControlKeyframe{
            targetValue,
            static_cast<float>(transition),
            static_cast<float>(easeWeight),
        });
        if(!animator.active && animator.queue.size() == 1 &&
           animator.progress >= 1.0f) {
            animator.startValue = animator.currentValue;
            animator.targetValue = animator.currentValue;
        }
    }

    void Player::setTimelineBlendLike_0x6735AC(const std::string &label,
                                               bool autoStop, double value,
                                               double transition,
                                               double ease) {
        if(!_runtime || label.empty()) {
            return;
        }

        auto timelineIt = _runtime->timelines.find(label);
        if(timelineIt == _runtime->timelines.end()) {
            return;
        }

        auto &state = timelineIt->second;
        state.label = label;
        state.blendAutoStop = autoStop;
        const float targetValue = static_cast<float>(value);
        const float easeWeight =
            static_cast<float>(timelineBlendEaseWeightLike_0x6735AC(ease));

        if(transition <= 0.0) {
            state.blendAnimator.queue.clear();
            state.blendAnimator.active = false;
            state.blendAnimator.currentValue = targetValue;
            state.blendAnimator.startValue = targetValue;
            state.blendAnimator.targetValue = targetValue;
            state.blendAnimator.progress = 1.0f;
            state.blendAnimator.duration = 0.0f;
            state.blendAnimator.weight = easeWeight;
            state.blendRatio = value;
            return;
        }

        state.blendAnimator.queue.push_back(detail::TimelineControlKeyframe{
            targetValue,
            static_cast<float>(transition),
            easeWeight,
        });
        if(!state.blendAnimator.active &&
           state.blendAnimator.queue.size() == 1 &&
           state.blendAnimator.progress >= 1.0f) {
            state.blendAnimator.startValue = state.blendAnimator.currentValue;
            state.blendAnimator.targetValue = state.blendAnimator.currentValue;
        }
        _emoteDirty = true;
    }

    void Player::stepTimelineControlAnimatorsLike_0x67D01C(double dt) {
        for(const auto &label : _runtime->playingTimelineLabels) {
            const auto timelineIt = _runtime->timelines.find(label);
            if(timelineIt == _runtime->timelines.end()) {
                continue;
            }

            auto &state = timelineIt->second;
            for(size_t trackIndex = 0;
                trackIndex < state.controlTrackAnimators.size(); ++trackIndex) {
                double steppedValue =
                    trackIndex < state.controlTrackValues.size()
                    ? static_cast<double>(state.controlTrackValues[trackIndex])
                    : 0.0;
                const bool stillAnimating = stepQueuedAnimatorLike_0x67D01C(
                    state.controlTrackAnimators[trackIndex], dt, steppedValue);
                if(trackIndex >= state.controlTrackValues.size()) {
                    state.controlTrackValues.resize(trackIndex + 1, 0.0f);
                }
                state.controlTrackValues[trackIndex] =
                    static_cast<float>(steppedValue);
                if(stillAnimating) {
                    _emoteDirty = true;
                }
            }
        }
    }

    void Player::stepTimelineBlendAnimatorsLike_0x67D01C(double dt) {
        for(const auto &label : _runtime->playingTimelineLabels) {
            const auto timelineIt = _runtime->timelines.find(label);
            if(timelineIt == _runtime->timelines.end()) {
                continue;
            }

            auto &state = timelineIt->second;
            double steppedBlend = state.blendRatio;
            const bool stillAnimating = stepQueuedAnimatorLike_0x67D01C(
                state.blendAnimator, dt, steppedBlend);
            state.blendRatio = steppedBlend;
            if(stillAnimating) {
                _emoteDirty = true;
            }
        }
    }

    void Player::refreshFixedControllerEvalOutputsLike_0x67D01C() {
        const auto *activeMotion = _runtime->activeMotion.get();
        if(!activeMotion) {
            return;
        }

        for(const auto &binding : activeMotion->fixedControllerOutputs) {
            if(binding.label.empty()) {
                continue;
            }

            double value = 0.0;
            const auto *bucket =
                controllerAnimatorBucketLike_0x671228(binding.type);
            if(bucket != nullptr) {
                if(const auto it = bucket->find(binding.label);
                   it != bucket->end()) {
                    value = static_cast<double>(it->second.currentValue);
                } else if(const auto *state =
                              findControllerAnimatorStateLike_0x671228(
                                  binding.label)) {
                    value = static_cast<double>(state->currentValue);
                } else if(const auto it = _variableValues.find(binding.label);
                          it != _variableValues.end()) {
                    value = it->second;
                } else {
                    value = getVariable(detail::widen(binding.label));
                }
            } else if(const auto it = _variableValues.find(binding.label);
                      it != _variableValues.end()) {
                value = it->second;
            } else {
                value = getVariable(detail::widen(binding.label));
            }

            ensureEvalResultSlotLike_0x686944(binding.label) = value;
            _evalResultValues[binding.label] = value;
            _variableValues[binding.label] = value;
        }
    }

    void Player::accumulateTimelineContributionLike_0x67C560(
        const std::string &label, double &value) {
        const auto *activeMotion = _runtime->activeMotion.get();
        if(!activeMotion || label.empty()) {
            return;
        }

        for(const auto &timelineLabel : _runtime->playingTimelineLabels) {
            const auto timelineIt = _runtime->timelines.find(timelineLabel);
            const auto controlIt =
                activeMotion->timelineControlByLabel.find(timelineLabel);
            if(timelineIt == _runtime->timelines.end() ||
               controlIt == activeMotion->timelineControlByLabel.end()) {
                continue;
            }

            const auto &state = timelineIt->second;
            if((state.flags & 2) == 0) {
                continue;
            }

            const auto &binding = controlIt->second;
            for(size_t trackIndex = 0; trackIndex < binding.tracks.size();
                ++trackIndex) {
                const auto &track = binding.tracks[trackIndex];
                if(track.instantVariable || track.frames.empty() ||
                   track.label != label ||
                   trackIndex >= state.controlTrackValues.size()) {
                    continue;
                }
                value += static_cast<double>(state.controlTrackValues[trackIndex]) *
                    state.blendRatio;
            }
        }
    }

    void Player::applyClampControlsLike_0x67C8A8() {
        const auto *activeMotion = _runtime->activeMotion.get();
        if(!activeMotion) {
            return;
        }

        for(const auto &binding : activeMotion->clampControls) {
            if(binding.varLr.empty() || binding.varUd.empty()) {
                continue;
            }

            const double range = binding.maxValue - binding.minValue;
            if(std::abs(range) <= 0.0000001) {
                continue;
            }

            double lrValue = 0.0;
            double udValue = 0.0;
            if(const auto it = _evalResultValues.find(binding.varLr);
               it != _evalResultValues.end()) {
                lrValue = it->second;
            } else if(const auto it = _variableValues.find(binding.varLr);
                      it != _variableValues.end()) {
                lrValue = it->second;
            } else {
                lrValue = getVariable(detail::widen(binding.varLr));
            }

            if(const auto it = _evalResultValues.find(binding.varUd);
               it != _evalResultValues.end()) {
                udValue = it->second;
            } else if(const auto it = _variableValues.find(binding.varUd);
                      it != _variableValues.end()) {
                udValue = it->second;
            } else {
                udValue = getVariable(detail::widen(binding.varUd));
            }

            double lrNorm =
                ((lrValue - binding.minValue) / range) * 2.0 - 1.0;
            double udNorm =
                ((udValue - binding.minValue) / range) * 2.0 - 1.0;

            if(lrNorm != 0.0 && udNorm != 0.0) {
                if(binding.type == 1) {
                    const double radius =
                        std::sqrt(lrNorm * lrNorm + udNorm * udNorm);
                    if(radius > 1.0) {
                        const double angle = std::atan2(udNorm, lrNorm);
                        lrNorm = std::cos(angle);
                        udNorm = std::sin(angle);
                    }
                } else {
                    double ratio = std::abs(lrNorm / udNorm);
                    if(ratio > 1.0) {
                        ratio = 1.0 / ratio;
                    }
                    const double invLen =
                        1.0 / std::sqrt(ratio * ratio + 1.0);
                    const double projX = lrNorm * invLen;
                    const double projY = udNorm * invLen;
                    const double projLen =
                        std::sqrt(projX * projX + projY * projY);
                    if(projLen > 0.0) {
                        const double scale =
                            (1.0 - std::cos(ratio * 1.57079633)) *
                                ((std::sin(projLen * 1.57079633) / projLen) -
                                 1.0) +
                            1.0;
                        lrNorm = projX * scale;
                        udNorm = projY * scale;
                    }
                }
            }

            double lrFinal = binding.minValue + range * (lrNorm + 1.0) * 0.5;
            const double udFinal =
                binding.minValue + range * (udNorm + 1.0) * 0.5;
            if(shouldMirrorEvalLabelLike_0x67C6B0(binding.varLr)) {
                lrFinal = -lrFinal;
            }
            writeEvalResultValueLike_0x6C4668(binding.varLr, lrFinal);
            writeEvalResultValueLike_0x6C4668(binding.varUd, udFinal);
        }
    }

    void Player::applyEvalResultPostProcessLike_0x67CC9C() {
        for(auto &entry : _evalResultList) {
            accumulateTimelineContributionLike_0x67C560(entry.label, entry.value);
            double outputValue = entry.value;
            if(shouldMirrorEvalLabelLike_0x67C6B0(entry.label)) {
                outputValue = -outputValue;
            }
            _evalResultValues[entry.label] = outputValue;
            _variableValues[entry.label] = outputValue;
        }

        applyClampControlsLike_0x67C8A8();
    }

    void Player::preProgressPlayingTimelinesLike_0x671764(
        double dt, std::unordered_map<std::string, double> *prevTimes) {
        if(dt <= 0.0) {
            return;
        }

        const auto *activeMotion = _runtime->activeMotion.get();
        size_t writeIndex = 0;
        for(size_t readIndex = 0;
            readIndex < _runtime->playingTimelineLabels.size(); ++readIndex) {
            const std::string label = _runtime->playingTimelineLabels[readIndex];
            const auto it = _runtime->timelines.find(label);
            if(it == _runtime->timelines.end()) {
                continue;
            }

            auto &state = it->second;
            if(prevTimes != nullptr) {
                (*prevTimes)[label] = state.currentTime;
            }

            if(!state.playing) {
                continue;
            }

            state.wasPlaying = true;
            bool keepPlaying = true;

            const detail::TimelineControlBinding *binding = nullptr;
            if(activeMotion) {
                if(const auto controlIt =
                       activeMotion->timelineControlByLabel.find(label);
                   controlIt != activeMotion->timelineControlByLabel.end()) {
                    binding = &controlIt->second;
                }
            }

            if(!binding) {
                state.currentTime += dt;
                if(state.totalFrames > 0.0 && state.currentTime >= state.totalFrames) {
                    if(state.loopTime >= 0.0) {
                        while(state.currentTime >= state.totalFrames) {
                            state.currentTime =
                                state.currentTime + state.loopTime -
                                state.totalFrames;
                        }
                    } else {
                        state.currentTime = state.totalFrames;
                        state.playing = false;
                        keepPlaying = false;
                    }
                }
            } else {
                const auto stepInternalRoute =
                    [this, &state, binding](double routeDt) {
                        if((state.flags & 2) == 0 || routeDt <= 0.0) {
                            return;
                        }

                        double steppedBlend = state.blendRatio;
                        const bool blendAnimating =
                            stepQueuedAnimatorLike_0x67D01C(
                                state.blendAnimator, routeDt, steppedBlend);
                        state.blendRatio = steppedBlend;
                        if(blendAnimating) {
                            _emoteDirty = true;
                        }

                        if(state.controlTrackValues.size() < binding->tracks.size()) {
                            state.controlTrackValues.resize(
                                binding->tracks.size(), 0.0f);
                        }
                        if(state.controlTrackAnimators.size() <
                           binding->tracks.size()) {
                            state.controlTrackAnimators.resize(
                                binding->tracks.size());
                        }

                        for(size_t trackIndex = 0;
                            trackIndex < binding->tracks.size(); ++trackIndex) {
                            const auto &track = binding->tracks[trackIndex];
                            if(track.instantVariable || track.frames.empty()) {
                                continue;
                            }

                            double steppedValue =
                                static_cast<double>(
                                    state.controlTrackValues[trackIndex]);
                            const bool trackAnimating =
                                stepQueuedAnimatorLike_0x67D01C(
                                    state.controlTrackAnimators[trackIndex],
                                    routeDt, steppedValue);
                            state.controlTrackValues[trackIndex] =
                                static_cast<float>(steppedValue);
                            if(trackAnimating) {
                                _emoteDirty = true;
                            }
                        }
                    };

                const double loopBegin = binding->loopBegin;
                const double loopEnd = binding->loopEnd;
                const double lastTime =
                    binding->lastTime >= 0.0 ? binding->lastTime : state.totalFrames;

                if(!state.controlInitialized ||
                   state.controlFrameCursor.size() != binding->tracks.size()) {
                    resetTimelineControlStateLike_0x671A50(
                        state, *binding, std::max(state.currentTime, 0.0));
                }

                if(loopBegin < 0.0) {
                    applyTimelineControlWindowLike_0x669E1C(
                        state, *binding, state.currentTime + dt, true);
                    stepInternalRoute(dt);

                    const bool blendAnimatorPending =
                        state.blendAnimator.active ||
                        !state.blendAnimator.queue.empty();
                    if(lastTime <= state.currentTime ||
                       (state.blendAutoStop && !blendAnimatorPending)) {
                        state.currentTime = lastTime;
                        state.playing = false;
                        keepPlaying = false;
                    }
                } else if(loopEnd > loopBegin) {
                    double remaining = dt;
                    while(remaining > 0.0 &&
                          state.currentTime + remaining >= loopEnd) {
                        const double currentTime = state.currentTime;
                        applyTimelineControlWindowLike_0x669E1C(
                            state, *binding, loopEnd, false);
                        remaining -= std::max(loopEnd - currentTime, 0.0);
                        resetTimelineControlStateLike_0x671A50(
                            state, *binding, loopBegin);
                    }
                    applyTimelineControlWindowLike_0x669E1C(
                        state, *binding, state.currentTime + remaining, true);
                    stepInternalRoute(remaining);

                    const bool blendAnimatorPending =
                        state.blendAnimator.active ||
                        !state.blendAnimator.queue.empty();
                    if(state.blendAutoStop && !blendAnimatorPending) {
                        state.playing = false;
                        keepPlaying = false;
                    }
                } else {
                    applyTimelineControlWindowLike_0x669E1C(
                        state, *binding, state.currentTime + dt, true);
                    stepInternalRoute(dt);

                    const bool blendAnimatorPending =
                        state.blendAnimator.active ||
                        !state.blendAnimator.queue.empty();
                    if(lastTime <= state.currentTime ||
                       (state.blendAutoStop && !blendAnimatorPending)) {
                        state.currentTime = lastTime;
                        state.playing = false;
                        keepPlaying = false;
                    }
                }
            }

            if(!keepPlaying && state.wasPlaying) {
                _runtime->pendingEvents.push_back({1, label, {}});
                state.wasPlaying = false;
            }

            if(state.playing && keepPlaying) {
                _runtime->playingTimelineLabels[writeIndex++] = label;
            }
        }
        _runtime->playingTimelineLabels.resize(writeIndex);
    }

    void Player::resetTimelineControlStateLike_0x671A50(
        detail::TimelineState &state,
        const detail::TimelineControlBinding &binding,
        double time) {
        state.controlFrameCursor.assign(binding.tracks.size(), -1);
        state.controlTrackValues.assign(binding.tracks.size(), 0.0f);
        state.controlTrackAnimators.assign(binding.tracks.size(), {});
        for(size_t trackIndex = 0; trackIndex < binding.tracks.size();
            ++trackIndex) {
            const auto &track = binding.tracks[trackIndex];
            int cursor = -1;
            int lastNonTypeZero = -1;
            for(size_t frameIndex = 0; frameIndex < track.frames.size();
                ++frameIndex) {
                const auto &frame = track.frames[frameIndex];
                if(!frame.isTypeZero) {
                    lastNonTypeZero = static_cast<int>(frameIndex);
                }
                if(frame.time <= time) {
                    cursor = static_cast<int>(frameIndex);
                    continue;
                }
                break;
            }
            state.controlFrameCursor[trackIndex] = cursor;

            if(lastNonTypeZero < 0) {
                continue;
            }

            const auto &frame =
                track.frames[static_cast<size_t>(lastNonTypeZero)];
            const size_t nextIndex = static_cast<size_t>(lastNonTypeZero + 1);
            const double transition =
                nextIndex < track.frames.size()
                ? std::max(track.frames[nextIndex].time - time - 1.0, 0.0)
                : 0.0;
            if((state.flags & 2) != 0 && !track.instantVariable) {
                scheduleTimelineControlAnimatorLike_0x671A50(
                    state, trackIndex, frame.value, transition,
                    frame.easingWeight);
            } else {
                setVariableResolvedWeightLike_0x671228(
                    track.label, static_cast<double>(frame.value), transition,
                    frame.easingWeight);
            }
        }
        state.controlInitialized = true;
        state.controlLastAppliedTime = time;
    }

    void Player::applyTimelineControlWindowLike_0x669E1C(
        detail::TimelineState &state,
        const detail::TimelineControlBinding &binding,
        double targetTime,
        bool inclusiveEnd) {
        if(state.controlFrameCursor.size() != binding.tracks.size()) {
            state.controlFrameCursor.assign(binding.tracks.size(), -1);
        }
        if(state.controlTrackValues.size() < binding.tracks.size()) {
            state.controlTrackValues.resize(binding.tracks.size(), 0.0f);
        }
        if(state.controlTrackAnimators.size() < binding.tracks.size()) {
            state.controlTrackAnimators.resize(binding.tracks.size());
        }

        for(size_t trackIndex = 0; trackIndex < binding.tracks.size();
            ++trackIndex) {
            const auto &track = binding.tracks[trackIndex];
            if(track.label.empty() || track.frames.empty()) {
                continue;
            }
            if((state.flags & 4) != 0 && track.instantVariable) {
                continue;
            }

            const bool internalRoute =
                (state.flags & 2) != 0 && !track.instantVariable;
            int cursor = state.controlFrameCursor[trackIndex];
            const int lastCursor =
                static_cast<int>(track.frames.size()) - 1;
            if(cursor >= lastCursor) {
                continue;
            }

            while(cursor + 1 < static_cast<int>(track.frames.size())) {
                const auto nextIndex = static_cast<size_t>(cursor + 1);
                const auto &nextFrame = track.frames[nextIndex];
                const bool crossed = inclusiveEnd
                    ? nextFrame.time <= targetTime
                    : nextFrame.time < targetTime;
                if(!crossed) {
                    break;
                }

                if(!nextFrame.isTypeZero &&
                   nextIndex + 1 < track.frames.size()) {
                    const auto &followingFrame = track.frames[nextIndex + 1];
                    const double transition = std::max(
                        followingFrame.time - targetTime - 1.0, 0.0);
                    if(internalRoute) {
                        scheduleTimelineControlAnimatorLike_0x671A50(
                            state, trackIndex, nextFrame.value, transition,
                            nextFrame.easingWeight);
                    } else {
                        setVariableResolvedWeightLike_0x671228(
                            track.label, static_cast<double>(nextFrame.value),
                            transition, nextFrame.easingWeight);
                    }
                }

                cursor = static_cast<int>(nextIndex);
            }

            state.controlFrameCursor[trackIndex] = cursor;
        }

        state.currentTime = targetTime;
        state.controlLastAppliedTime = targetTime;
    }

    void Player::applyTimelineControlFrameCrossingLike_0x67CD20(
        const std::unordered_map<std::string, double> &prevTimes) {
        const auto *activeMotion = _runtime->activeMotion.get();
        if(!activeMotion) {
            return;
        }

        for(const auto &label : _runtime->playingTimelineLabels) {
            const auto timelineIt = _runtime->timelines.find(label);
            const auto controlIt =
                activeMotion->timelineControlByLabel.find(label);
            if(timelineIt == _runtime->timelines.end() ||
               controlIt == activeMotion->timelineControlByLabel.end()) {
                continue;
            }

            auto &state = timelineIt->second;
            const auto &binding = controlIt->second;
            const auto prevIt = prevTimes.find(label);
            const double prevTime =
                prevIt != prevTimes.end() ? prevIt->second : state.currentTime;
            const bool rewound = !state.controlInitialized ||
                state.currentTime < prevTime ||
                state.controlFrameCursor.size() != binding.tracks.size();
            if(rewound) {
                // Aligned to sub_671A50: re-seek per-track cursors using the
                // timeline time before the current crossing scan.
                resetTimelineControlStateLike_0x671A50(
                    state, binding, std::max(prevTime, 0.0));
            }

            if((state.flags & 2) != 0 && (state.flags & 4) == 0) {
                // Aligned to sub_67CD20 + sub_6735AC:
                // crossed-frame entry into the internal route triggers a
                // timeline-level fade to 0 over 20 frames before the runtime
                // is marked as initialized.
                setTimelineBlendLike_0x6735AC(label, true, 0.0, 20.0, 0.0);
                state.flags |= 4;
            }

            for(size_t trackIndex = 0; trackIndex < binding.tracks.size();
                ++trackIndex) {
                const auto &track = binding.tracks[trackIndex];
                if(track.label.empty() || track.frames.empty()) {
                    continue;
                }
                if((state.flags & 2) != 0 && !track.instantVariable) {
                    continue;
                }

                int cursor = trackIndex < state.controlFrameCursor.size()
                    ? state.controlFrameCursor[trackIndex]
                    : -1;
                size_t nextIndex = cursor >= 0
                    ? static_cast<size_t>(cursor + 1)
                    : 0;
                while(nextIndex < track.frames.size() &&
                      track.frames[nextIndex].time <= state.currentTime) {
                    const auto &frame = track.frames[nextIndex];
                    if(!frame.isTypeZero) {
                        setVariableResolvedWeightLike_0x671228(
                            track.label, static_cast<double>(frame.value),
                            frame.time, frame.easingWeight);
                    }
                    cursor = static_cast<int>(nextIndex);
                    ++nextIndex;
                }

                if(trackIndex >= state.controlFrameCursor.size()) {
                    state.controlFrameCursor.resize(trackIndex + 1, -1);
                }
                state.controlFrameCursor[trackIndex] = cursor;
            }

            state.controlLastAppliedTime = state.currentTime;
        }
    }

    void Player::frameProgress(double dt) {
        // Aligned to libkrkr2.so Player_progress_inner (0x6C106C):
        // _speed is a bool flag (play/pause). When false, skip progress entirely.
        if(!_speed) {
            return;
        }
        const auto *activeClipBeforeProgress = selectActiveClip();
        const double actualDelta = dt;
        _frameLastTime = actualDelta;
        _frameLoopTime += actualDelta;
        _loopTime += actualDelta;
        _frameTickCount += actualDelta;

        _evalResultValues.clear();

        // Aligned to Player_preProgress (0x671764): timeline advancement
        // happens before controller stepping inside Player_progress.
        std::unordered_map<std::string, double> prevTimes;
        preProgressPlayingTimelinesLike_0x671764(actualDelta, &prevTimes);

        double remainingControllerStep = actualDelta;
        const auto stepControllerBucket =
            [this](auto &bucket, double controllerDt) {
                for(auto &[label, state] : bucket) {
                    double steppedValue = state.currentValue;
                    const bool stillAnimating = stepQueuedAnimatorLike_0x67D01C(
                        state, controllerDt, steppedValue);
                    _variableValues[label] = steppedValue;
                    ensureEvalResultSlotLike_0x686944(label) = steppedValue;
                    _evalResultValues[label] = steppedValue;
                    if(stillAnimating) {
                        _emoteDirty = true;
                    }
                }
            };
        while(remainingControllerStep > 0.0) {
            const double controllerDt = std::min(remainingControllerStep, 1.1);
            // Aligned to 0x67D01C container order: type4 -> type5 -> type6
            // -> type8 -> type7, then generic eval animators.
            stepControllerBucket(_type4ControllerAnimators, controllerDt);
            stepControllerBucket(_type5ControllerAnimators, controllerDt);
            stepControllerBucket(_type6ControllerAnimators, controllerDt);
            stepControllerBucket(_type8ControllerAnimators, controllerDt);
            stepControllerBucket(_type7ControllerAnimators, controllerDt);
            refreshFixedControllerEvalOutputsLike_0x67D01C();
            remainingControllerStep -= controllerDt;
        }

        applyEvalResultPostProcessLike_0x67CC9C();

        // Camera velocity/friction moved to updateLayers pre-loop (0x6BB360..0x6BB42C)

        // Inference from libkrkr2.so Player_progress_inner (0x6C106C):
        // player+456 is the selected clip/timeline eval time consumed by
        // Player_updateLayers (0x6BB33C), not an arbitrary primary-label entry.
        _clampedEvalTime = activeClipTime(*_runtime, selectActiveClip());

        // krkrsdl3's Player (isMotion=true) stops non-loop motion clips by
        // syncTime, then selfSyncTime, then lastTime. Yuzu logo clips rely on
        // this Player-level flag becoming false; timeline-only state is not
        // enough for scripts polling .playing or waiting for onSync().
        if(!_runtime->isEmoteMode && activeClipBeforeProgress &&
           !activeClipBeforeProgress->loop) {
            const double endTime =
                motionClipEndTimeLikeKrkrsdl3(activeClipBeforeProgress);
            double clipTime = activeClipTime(*_runtime, activeClipBeforeProgress);
            if(clipTime <= 0.0) {
                clipTime = _frameLoopTime;
            }
            if(endTime > 0.0 && clipTime >= endTime) {
                const bool wasPlaying =
                    _allplaying || !_runtime->playingTimelineLabels.empty();
                for(auto &[_, state] : _runtime->timelines) {
                    state.currentTime = std::min(state.currentTime, endTime);
                    state.playing = false;
                    state.wasPlaying = false;
                }
                _runtime->playingTimelineLabels.clear();
                if(wasPlaying) {
                    _runtime->pendingEvents.push_back(
                        {1, activeClipBeforeProgress->label, {}});
                }
            }
        }

        // Scan PSB layers for action/sync events crossed this frame
        // Aligned to libkrkr2.so: updateLayers queues events during evaluation
        if(_runtime->activeMotion && actualDelta > 0) {
            for(const auto &[name, prev] : prevTimes) {
                const auto stateIt = _runtime->timelines.find(name);
                if(stateIt == _runtime->timelines.end()) {
                    continue;
                }
                if(stateIt->second.currentTime > prev) {
                    detail::scanLayerActions(*_runtime->activeMotion,
                                             prev, stateIt->second.currentTime,
                                             _runtime->pendingEvents);
                }
            }
        }

        _allplaying = !_runtime->playingTimelineLabels.empty();
        _syncActive = _syncWaiting && _allplaying;
    }


} // namespace motion
