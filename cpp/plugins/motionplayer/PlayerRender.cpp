// PlayerRender.cpp — Drawing/rendering: renderToLayer, draw, frameProgress
// Split from Player.cpp for maintainability.
//
#include "PlayerInternal.h"
#include "MotionPlayerExtension.h"
#include "ConfigManager/IndividualConfigManager.h"
#include "TickCount.h"
#include "ThreadIntf.h"
#include "godot/GodotRenderManager.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <set>
#include <unordered_map>
#include <unordered_set>

using namespace motion::internal;

namespace {

    tTJSNI_BaseLayer *resolveNativeLayer(iTJSDispatch2 *layerObject);
    std::string sampleBitmapStats(const iTVPBaseBitmap *bitmap);
    bool bitmapVisibleBounds(const iTVPBaseBitmap *bitmap,
                             tTVPRect &bounds);
    bool showCenteredPresentationMessageUiOverlay(
        tTJSNI_BaseLayer *presentationLayer,
        int canvasWidth,
        int canvasHeight,
        const char *reason);
    void hideCenteredPresentationMessageUiOverlay(
        tTJSNI_BaseLayer *targetLayer);
    bool findCenteredPresentationMessageUiTop(
        tTJSNI_BaseLayer *presentationLayer,
        int canvasHeight,
        tjs_int &messageTop);
    bool motionPresentationLayerHasVisibleSamples(tTJSNI_BaseLayer *layer);
    bool centeredPresentationFrameHasResolvedScale(
        tTJSNI_BaseLayer *layer,
        const std::string &motionPath,
        const char *reason);

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

    struct SharedMotionSourceBitmapEntry {
        std::shared_ptr<tTVPBaseBitmap> bitmap;
        std::size_t bytes = 0;
        std::uint64_t lastUse = 0;
    };

    struct SharedMotionSourceBitmapCache {
        std::mutex mutex;
        std::unordered_map<std::string, SharedMotionSourceBitmapEntry> entries;
        std::size_t bytes = 0;
        std::uint64_t useCounter = 0;
    };

    SharedMotionSourceBitmapCache &sharedMotionSourceBitmapCache() {
        static SharedMotionSourceBitmapCache cache;
        return cache;
    }

    void appendMotionSourceFingerprint(
        std::uint64_t &first,
        std::uint64_t &second,
        const std::vector<std::uint8_t> &data) {
        for(const auto value : data) {
            first ^= value;
            first *= 1099511628211ull;
            second += value + 0x9e3779b97f4a7c15ull + (second << 6) +
                (second >> 2);
        }
    }

    std::string makeSharedMotionSourceBitmapKey(
        const motion::detail::MotionSnapshot &snapshot,
        const std::string &resourcePath,
        const std::string &compressName,
        int width,
        int height,
        const PSB::PSBResource &resource,
        std::unordered_map<
            const PSB::PSBResource *,
            std::pair<std::uint64_t, std::uint64_t>> &fingerprintCache) {
        auto fingerprintIt = fingerprintCache.find(&resource);
        if(fingerprintIt == fingerprintCache.end()) {
            std::uint64_t first = 1469598103934665603ull;
            std::uint64_t second = 0x517cc1b727220a95ull;
            appendMotionSourceFingerprint(first, second, resource.data);
            fingerprintIt = fingerprintCache.emplace(
                &resource, std::make_pair(first, second)).first;
        }
        auto first = fingerprintIt->second.first;
        auto second = fingerprintIt->second.second;

        std::size_t paletteBytes = 0;
        if(resourcePath.size() > 6 &&
           resourcePath.compare(resourcePath.size() - 6, 6, "/pixel") == 0) {
            const auto palettePath =
                resourcePath.substr(0, resourcePath.size() - 6) + "/pal";
            if(const auto paletteIt = snapshot.resourcesByPath.find(palettePath);
               paletteIt != snapshot.resourcesByPath.end() && paletteIt->second) {
                paletteBytes = paletteIt->second->data.size();
                appendMotionSourceFingerprint(
                    first, second, paletteIt->second->data);
            }
        }
        return fmt::format(
            "{}x{}|{}|{}|{}:{:016x}:{:016x}|{}",
            width, height, resourcePath, compressName,
            resource.data.size(), first, second, paletteBytes);
    }

    std::shared_ptr<tTVPBaseBitmap> findSharedMotionSourceBitmap(
        const std::string &key) {
        auto &cache = sharedMotionSourceBitmapCache();
        std::lock_guard<std::mutex> lock(cache.mutex);
        const auto it = cache.entries.find(key);
        if(it == cache.entries.end()) {
            return nullptr;
        }
        it->second.lastUse = ++cache.useCounter;
        return it->second.bitmap;
    }

    void rememberSharedMotionSourceBitmap(
        const std::string &key,
        const std::shared_ptr<tTVPBaseBitmap> &bitmap) {
        if(key.empty() || !bitmap) {
            return;
        }
        auto &cache = sharedMotionSourceBitmapCache();
        std::lock_guard<std::mutex> lock(cache.mutex);
        const auto bitmapBytes =
            static_cast<std::size_t>(bitmap->GetWidth()) *
            static_cast<std::size_t>(bitmap->GetHeight()) * 4u;
        if(auto existing = cache.entries.find(key);
           existing != cache.entries.end()) {
            cache.bytes -= existing->second.bytes;
            existing->second = {bitmap, bitmapBytes, ++cache.useCounter};
            cache.bytes += bitmapBytes;
        } else {
            cache.entries.emplace(
                key,
                SharedMotionSourceBitmapEntry{
                    bitmap, bitmapBytes, ++cache.useCounter});
            cache.bytes += bitmapBytes;
        }

        constexpr std::size_t kSharedBitmapCacheLimit = 256u * 1024u * 1024u;
        while(cache.bytes > kSharedBitmapCacheLimit &&
              cache.entries.size() > 1) {
            auto oldest = cache.entries.begin();
            for(auto it = std::next(cache.entries.begin());
                it != cache.entries.end(); ++it) {
                if(it->second.lastUse < oldest->second.lastUse) {
                    oldest = it;
                }
            }
            cache.bytes -= oldest->second.bytes;
            cache.entries.erase(oldest);
        }
    }

    void renderReuseHashCombine(std::size_t &seed, std::size_t value) {
        seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    }

    std::size_t renderReuseHashFloat(float value) {
        return std::hash<int>{}(
            static_cast<int>(std::lround(static_cast<double>(value) * 1024.0)));
    }

    std::size_t renderCommandLeafReuseSignature(
        const motion::detail::PlayerRuntime::RenderCommand &command) {
        std::size_t seed = 0x6c7440u;
        // Scratch layers are retained by their stable command-list slot. Keep
        // the authored identity in the signature so a topology change can
        // reuse the allocation but can never reuse another node's pixels.
        renderReuseHashCombine(seed, std::hash<int>{}(command.nodeIndex));
        renderReuseHashCombine(
            seed, std::hash<std::uintptr_t>{}(command.renderScopeId));
        renderReuseHashCombine(
            seed, std::hash<int>{}(command.scopedNodeIndex));
        renderReuseHashCombine(seed,
                               std::hash<std::string>{}(command.nodeLabel));
        renderReuseHashCombine(seed,
                               std::hash<std::string>{}(command.sourceKey));
        if(command.sourceMotion) {
            renderReuseHashCombine(
                seed, std::hash<std::string>{}(command.sourceMotion->path));
        }
        renderReuseHashCombine(seed,
                               std::hash<bool>{}(command.hasOwnSource));
        renderReuseHashCombine(seed,
                               std::hash<bool>{}(command.groupOnly));
        // The low nibble is applied when this output is composited into its
        // parent. Only the high nibble changes source preparation here.
        renderReuseHashCombine(seed,
                               std::hash<int>{}(command.blendMode & 0xF0));
        renderReuseHashCombine(seed,
                               std::hash<bool>{}(command.clearEnabled));
        renderReuseHashCombine(seed, std::hash<int>{}(command.meshDivX));
        renderReuseHashCombine(seed, std::hash<int>{}(command.meshDivY));
        renderReuseHashCombine(seed, std::hash<int>{}(command.meshType));
        const int clipWidth = command.clipRect[2] - command.clipRect[0];
        const int clipHeight = command.clipRect[3] - command.clipRect[1];
        renderReuseHashCombine(seed, std::hash<int>{}(clipWidth));
        renderReuseHashCombine(seed, std::hash<int>{}(clipHeight));
        for(const auto value : command.packedColors) {
            renderReuseHashCombine(seed, std::hash<std::uint32_t>{}(value));
        }
        // Exact float hashes are intentional. Quantizing animation geometry
        // here could freeze sub-pixel eye or hair motion.
        for(const auto value : command.localCorners) {
            renderReuseHashCombine(seed, std::hash<float>{}(value));
        }
        for(const auto value : command.localMeshPoints) {
            renderReuseHashCombine(seed, std::hash<float>{}(value));
        }
        return seed;
    }

    std::string renderCommandOutputCacheKey(
        const motion::detail::PlayerRuntime::RenderCommand &command) {
        return fmt::format(
            "{:x}:{}:{}", command.renderScopeId,
            command.scopedNodeIndex, command.nodeIndex);
    }

    std::size_t renderCommandReuseSignature(
        const std::vector<motion::detail::PlayerRuntime::RenderCommand> &commands) {
        std::size_t seed = commands.size();
        for(const auto &command : commands) {
            renderReuseHashCombine(seed, std::hash<int>{}(command.nodeIndex));
            renderReuseHashCombine(
                seed, std::hash<std::uintptr_t>{}(command.renderScopeId));
            renderReuseHashCombine(
                seed, std::hash<int>{}(command.scopedNodeIndex));
            renderReuseHashCombine(
                seed,
                std::hash<std::uintptr_t>{}(command.parentRenderScopeId));
            renderReuseHashCombine(
                seed, std::hash<int>{}(command.scopedParentNodeIndex));
            for(const auto &ancestor :
                    command.outerRenderAncestorChain) {
                renderReuseHashCombine(
                    seed,
                    std::hash<std::uintptr_t>{}(
                        ancestor.renderScopeId));
                renderReuseHashCombine(
                    seed,
                    std::hash<int>{}(ancestor.scopedNodeIndex));
            }
            renderReuseHashCombine(seed,
                                   std::hash<std::string>{}(command.nodeLabel));
            renderReuseHashCombine(seed, std::hash<std::string>{}(command.sourceKey));
            if(command.sourceMotion) {
                renderReuseHashCombine(
                    seed,
                    std::hash<std::string>{}(command.sourceMotion->path));
            }
            renderReuseHashCombine(seed,
                                   std::hash<bool>{}(command.hasOwnSource));
            renderReuseHashCombine(seed, std::hash<int>{}(command.blendMode));
            renderReuseHashCombine(seed, std::hash<int>{}(command.opacity));
            renderReuseHashCombine(seed, std::hash<int>{}(command.itemFlags));
            renderReuseHashCombine(seed, std::hash<int>{}(command.parentNodeIndex));
            renderReuseHashCombine(seed,
                                   std::hash<bool>{}(command.hasRenderParent));
            renderReuseHashCombine(seed,
                                   std::hash<bool>{}(command.alphaMaskOnly));
            renderReuseHashCombine(
                seed,
                std::hash<int>{}(command.differenceAlphaMaskOperation));
            for(const auto value :
                    command.differenceAlphaMaskSourceCommandIndices) {
                renderReuseHashCombine(seed, std::hash<int>{}(value));
            }
            for(const auto value :
                    command.differenceAlphaMaskGroupCommandIndices) {
                renderReuseHashCombine(seed, std::hash<int>{}(value));
            }
            for(const auto value :
                    command.differenceAlphaMaskInputCommandIndices) {
                renderReuseHashCombine(seed, std::hash<int>{}(value));
            }
            renderReuseHashCombine(seed, std::hash<int>{}(command.visibleAncestorIndex));
            renderReuseHashCombine(seed, std::hash<bool>{}(command.requiresLocalClip));
            renderReuseHashCombine(seed, std::hash<int>{}(command.meshDivX));
            renderReuseHashCombine(seed, std::hash<int>{}(command.meshDivY));
            renderReuseHashCombine(seed, std::hash<int>{}(command.meshType));
            renderReuseHashCombine(seed, std::hash<int>{}(command.layerId));
            renderReuseHashCombine(seed, std::hash<bool>{}(command.groupOnly));
            renderReuseHashCombine(
                seed,
                std::hash<bool>{}(command.implicitVisibleStencilGroup));
            renderReuseHashCombine(
                seed,
                std::hash<bool>{}(command.implicitVisibleStencilBase));
            renderReuseHashCombine(
                seed,
                std::hash<int>{}(
                    command.implicitVisibleStencilGroupNodeIndex));
            renderReuseHashCombine(
                seed, std::hash<bool>{}(command.stencilMaskReferenced));
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
            for(const auto value : command.childCommandIndices) {
                renderReuseHashCombine(seed, std::hash<int>{}(value));
            }
            for(const auto value : command.stencilModifierCommandIndices) {
                renderReuseHashCombine(seed, std::hash<int>{}(value));
            }
            for(const auto value : command.stencilMaskNodeIndices) {
                renderReuseHashCombine(seed, std::hash<int>{}(value));
            }
            for(const auto value : command.stencilMaskCommandIndices) {
                renderReuseHashCombine(seed, std::hash<int>{}(value));
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

    bool packedColorsAreOpaqueBlack(std::uint32_t c0, std::uint32_t c1,
                                    std::uint32_t c2, std::uint32_t c3) {
        return c0 == 0xFF000000u && c1 == 0xFF000000u &&
            c2 == 0xFF000000u && c3 == 0xFF000000u;
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

    std::shared_ptr<tTVPBaseBitmap>
    recoverRgbEncodedDifferenceAlphaMask(
        const tTVPBaseBitmap &bitmap) {
        const int width = static_cast<int>(bitmap.GetWidth());
        const int height = static_cast<int>(bitmap.GetHeight());
        if(width <= 0 || height <= 0) {
            return nullptr;
        }

        size_t alphaPixels = 0;
        size_t rgbPixels = 0;
        for(int y = 0; y < height; ++y) {
            const auto *row = static_cast<const std::uint8_t *>(
                bitmap.GetScanLine(static_cast<tjs_uint>(y)));
            for(int x = 0; x < width; ++x) {
                const auto *pixel =
                    row + static_cast<size_t>(x) * 4u;
                if(pixel[3] != 0) {
                    ++alphaPixels;
                }
                if(pixel[0] != 0 || pixel[1] != 0 ||
                   pixel[2] != 0) {
                    ++rgbPixels;
                }
            }
        }
        if(!motion::internal::shouldRecoverDifferenceAlphaFromRgb(
               alphaPixels, rgbPixels)) {
            return nullptr;
        }

        auto recovered = cloneBitmap32(bitmap);
        for(int y = 0; y < height; ++y) {
            auto *row = static_cast<std::uint8_t *>(
                recovered->GetScanLineForWrite(
                    static_cast<tjs_uint>(y)));
            for(int x = 0; x < width; ++x) {
                auto *pixel =
                    row + static_cast<size_t>(x) * 4u;
                pixel[3] = motion::internal::differenceAlphaFromRgb(
                    pixel[0], pixel[1], pixel[2]);
            }
        }
        return recovered;
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
        const auto motion = renderDebugLowercase(motionPath);
        const auto source = renderDebugLowercase(sourceKey);
        const auto resolved = renderDebugLowercase(origin);
        const bool sdPreviewMotion =
            motion.find("/sd") != std::string::npos ||
            motion.find("\\sd") != std::string::npos ||
            motion.find("sd") == 0;
        return motion.find("title.pimg") != std::string::npos ||
            motion.find("title.psb") != std::string::npos ||
            motion.find("title") != std::string::npos ||
            sdPreviewMotion ||
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

    bool isYuzuNumberedTitleCharacterMotion(
        const std::string &motionPath,
        const std::string &sourceKey = {}) {
        const auto motion = renderDebugLowercase(motionPath);
        const auto titlePos = motion.find("title_bg");
        if(titlePos == std::string::npos ||
           titlePos + 8 >= motion.size() ||
           motion[titlePos + 8] < '0' || motion[titlePos + 8] > '9') {
            return false;
        }
        if(sourceKey.empty()) {
            return true;
        }
        const auto source = renderDebugLowercase(sourceKey);
        return source.rfind("src/ch", 0) == 0 ||
            source.find("/ch") != std::string::npos;
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

    bool isYuzuStartupLogoWhiteWashLayer(const std::string &motionPath,
                                         const std::string &nodeLabel,
                                         const std::string &sourceKey) {
        if(!isYuzuStartupLogoMotion(motionPath)) {
            return false;
        }
        const auto source = renderDebugLowercase(sourceKey);
        if(source.find("moji_white") != std::string::npos) {
            return true;
        }
        const auto label = renderDebugLowercase(nodeLabel);
        return label == "white" && source.find("/yuzu/") != std::string::npos;
    }

    bool motionSourcePixelsAreBGRA(bool decodedPixelsAreBgra) {
        // The decoder explicitly marks palettized resources after expanding
        // their palette into TVP's BGRA layout. Unpalettized raw/RL motion
        // resources remain in authored RGBA order regardless of the PSB
        // container version and still need the R/B conversion below.
        return decodedPixelsAreBgra;
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

    bool isYuzuTitleStencilUtilityLayer(const std::string &motionPath,
                                        const std::string &sourceKey) {
        if(!isYuzuTitlePresentationMotion(motionPath)) {
            return false;
        }
        const auto source = renderDebugLowercase(sourceKey);
        return source.rfind("src/#mask/", 0) == 0 ||
            source.find("/#mask/") != std::string::npos;
    }

    bool isYuzuSdStencilUtilityLayer(const std::string &motionPath,
                                     const std::string &nodeLabel,
                                     const std::string &sourceKey) {
        if(!isYuzuSdPresentationMotionPath(motionPath)) {
            return false;
        }
        const auto label = renderDebugLowercase(nodeLabel);
        const auto source = renderDebugLowercase(sourceKey);
        if(label != "mask") {
            return false;
        }
        const auto slash = source.find_last_of("/\\");
        return (slash == std::string::npos ? source : source.substr(slash + 1)) ==
            "mask";
    }

    bool isYuzuTitleNormalPresentationSource(const std::string &sourceKey) {
        const auto source = renderDebugLowercase(sourceKey);
        return source.find("src/normal/") != std::string::npos ||
            source.find("/normal/") != std::string::npos ||
            source.find("src/normal_re/") != std::string::npos ||
            source.find("/normal_re/") != std::string::npos;
    }

    bool isYuzuTitleNormalPresentationLayer(
        const motion::detail::PlayerRuntime::PreparedRenderItem &entry) {
        return isYuzuTitleNormalPresentationSource(entry.sourceKey);
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

    bool isYuzuNumberedTitleCompositeSource(const std::string &sourceKey) {
        const auto source = renderDebugLowercase(sourceKey);
        // `src/common/title_logo` contains the broad `/title_` token too, but
        // it is an overlay and must not be sorted with the background pass.
        if(source.find("title_logo") != std::string::npos) {
            return false;
        }
        return source.rfind("src/title_", 0) == 0 ||
            source.find("/title_") != std::string::npos;
    }

    bool isYuzuTitleBackgroundLayer(const std::string &nodeLabel,
                                    const std::string &sourceKey) {
        const auto label = renderDebugLowercase(nodeLabel);
        if(label == "bg" || label.rfind("bg_", 0) == 0 ||
           (label.size() > 2 &&
            label.compare(label.size() - 2, 2, "bg") == 0)) {
            return true;
        }
        const auto source = renderDebugLowercase(sourceKey);
        return source.find("bg_title") != std::string::npos ||
            source.rfind("src/bg", 0) == 0 ||
            source.find("/bg") != std::string::npos ||
            source == "src/title/bg" ||
            source == "src/title/bg1" ||
            source == "src/title/bg2" ||
            source.find("/title/bg") != std::string::npos ||
            isYuzuNumberedTitleCompositeSource(sourceKey);
    }

    bool isYuzuTitleLogoLayer(const std::string &nodeLabel,
                              const std::string &sourceKey) {
        const auto source = renderDebugLowercase(sourceKey);
        if(source.find("logo") != std::string::npos ||
           sourceKey.find("ロゴ") != std::string::npos) {
            return true;
        }
        const auto label = renderDebugLowercase(nodeLabel);
        return label == "logo" || label == "logoover" ||
            label == "title_logo" || label.find("logo") != std::string::npos ||
            nodeLabel.find("ロゴ") != std::string::npos;
    }

    bool isYuzuTitleNonCharacterLayer(const std::string &nodeLabel,
                                      const std::string &sourceKey) {
        if(isYuzuTitleBackgroundLayer(nodeLabel, sourceKey) ||
           isYuzuTitlePositionLayer(nodeLabel, sourceKey) ||
           isYuzuTitleLogoLayer(nodeLabel, sourceKey) ||
           isYuzuTitleWhiteUtilityLayer("title_bg", nodeLabel, sourceKey)) {
            return true;
        }

        const auto source = renderDebugLowercase(sourceKey);
        const auto label = renderDebugLowercase(nodeLabel);
        return source.find("title_main") != std::string::npos ||
            source.find("sosyoku") != std::string::npos ||
            source.find("/effect") != std::string::npos ||
            label == "main" || label == "title_main";
    }

    bool isYuzuTitleCharacterOrLogoLayer(const std::string &nodeLabel,
                                         const std::string &sourceKey) {
        const auto source = renderDebugLowercase(sourceKey);
        if(isYuzuTitleLogoLayer(nodeLabel, sourceKey) ||
           source.find("src/title/ch") != std::string::npos ||
           source.find("/title/ch") != std::string::npos ||
           source.find("title2_ch") != std::string::npos ||
           source.find("title_char") != std::string::npos) {
            return true;
        }
        if(!isYuzuTitleNonCharacterLayer(nodeLabel, sourceKey) &&
           (source.find("src/title/") != std::string::npos ||
            source.find("/title/") != std::string::npos)) {
            return true;
        }
        if(isYuzuTitleNormalPresentationSource(sourceKey)) {
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
        if(!isYuzuTitleNonCharacterLayer(nodeLabel, sourceKey) &&
           (source.find("src/title/") != std::string::npos ||
            source.find("/title/") != std::string::npos)) {
            return true;
        }
        if(isYuzuTitleNormalPresentationSource(sourceKey)) {
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

    int yuzuTitleIntroDrawPriority(
        const motion::detail::PlayerRuntime::PreparedRenderItem &entry) {
        // Check the overlay before the deliberately broad background matcher.
        // Some games store it as `src/common/title_logo`, which otherwise
        // shares the `/title_` naming convention used by composite cards.
        if(isYuzuTitleLogoLayer(entry.nodeLabel, entry.sourceKey)) {
            return 3;
        }
        if(isYuzuTitleBackgroundLayer(entry.nodeLabel, entry.sourceKey)) {
            return 0;
        }
        if(isYuzuTitlePositionLayer(entry.nodeLabel, entry.sourceKey)) {
            return 1;
        }
        if(isYuzuTitleStandaloneCharacterLayer(entry.nodeLabel,
                                               entry.sourceKey)) {
            return 2;
        }
        return 1;
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
        if(isYuzuTitleNormalPresentationLayer(entry)) {
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

    bool isYuzuTitleMainLayer(const std::string &nodeLabel,
                              const std::string &sourceKey) {
        const auto source = renderDebugLowercase(sourceKey);
        if(source.find("src/title/title_main") != std::string::npos ||
           source.find("/title/title_main") != std::string::npos) {
            return true;
        }
        const auto label = renderDebugLowercase(nodeLabel);
        return label == "main" || label == "title_main";
    }

    bool yuzuTitleEntryCoversCanvas(
        const motion::detail::PlayerRuntime::PreparedRenderItem &entry,
        int canvasWidth,
        int canvasHeight) {
        if(canvasWidth <= 0 || canvasHeight <= 0) {
            return false;
        }
        auto bounds = entry.paintBox;
        if(bounds[2] <= bounds[0] || bounds[3] <= bounds[1]) {
            bounds = entry.viewport;
        }
        if(bounds[2] <= bounds[0] || bounds[3] <= bounds[1]) {
            return false;
        }
        const float width = bounds[2] - bounds[0];
        const float height = bounds[3] - bounds[1];
        return width >= static_cast<float>(canvasWidth) * 0.9f &&
            height >= static_cast<float>(canvasHeight) * 0.9f &&
            bounds[0] <= static_cast<float>(canvasWidth) * 0.1f &&
            bounds[1] <= static_cast<float>(canvasHeight) * 0.1f &&
            bounds[2] >= static_cast<float>(canvasWidth) * 0.9f &&
            bounds[3] >= static_cast<float>(canvasHeight) * 0.9f;
    }

    bool hasYuzuTitleOpaqueCanvasBaseFrame(
        const motion::detail::PlayerRuntime &runtime,
        int canvasWidth,
        int canvasHeight) {
        return std::any_of(
            runtime.preparedRenderItems.begin(),
            runtime.preparedRenderItems.end(),
            [&](const auto &entry) {
                if(!entry.drawFlag || entry.skipFlag0 || entry.skipFlag1 ||
                   entry.opacity < 240 || entry.groupOnly ||
                   (entry.blendMode & 0x0f) != 0 ||
                   !isYuzuTitleBackgroundLayer(entry.nodeLabel,
                                               entry.sourceKey) ||
                   isYuzuTitleWhiteUtilityLayer(
                       "title_bg", entry.nodeLabel, entry.sourceKey)) {
                    return false;
                }
                const auto source = renderDebugLowercase(entry.sourceKey);
                const auto label = renderDebugLowercase(entry.nodeLabel);
                if(source.find("src/common/") != std::string::npos ||
                   source.find("/#mask/") != std::string::npos ||
                   label == "flash" || label == "通常bg") {
                    return false;
                }
                return yuzuTitleEntryCoversCanvas(
                    entry, canvasWidth, canvasHeight);
            });
    }

    bool hasYuzuTitleOpaqueFinalOverlayFrame(
        const motion::detail::PlayerRuntime &runtime) {
        bool hasOpaqueFinalOverlay = false;
        bool hasActiveTransientLogo = false;
        for(const auto &entry : runtime.preparedRenderItems) {
            if(!entry.drawFlag || entry.skipFlag0 || entry.skipFlag1) {
                continue;
            }
            const bool isLogo = isYuzuTitleLogoLayer(entry.nodeLabel,
                                                     entry.sourceKey);
            const bool isMain = isYuzuTitleMainLayer(entry.nodeLabel,
                                                     entry.sourceKey);
            if(!isLogo && !isMain) {
                continue;
            }
            if(entry.opacity >= 240) {
                hasOpaqueFinalOverlay = true;
            }
            // Numbered title cards draw a normal opaque logo plus a second
            // additive copy for the bounce/flash. The normal copy alone is
            // not proof that the animation has settled: retaining while the
            // additive copy is visible freezes an oversized white logo over
            // the completed card.
            if(isLogo && entry.opacity > 0 &&
               (entry.blendMode & 0x0f) != 0) {
                hasActiveTransientLogo = true;
            }
        }
        return hasOpaqueFinalOverlay && !hasActiveTransientLogo;
    }

    bool hasYuzuTitleStablePresentationFrame(
        const motion::detail::PlayerRuntime &runtime) {
        int opaqueTitleCharacters = 0;
        int opaqueNormalPresentationItems = 0;
        bool hasOpaqueBackground = false;
        bool hasOpaqueNumberedComposite = false;
        bool hasOpaqueMainOrLogo = false;
        for(const auto &entry : runtime.preparedRenderItems) {
            if(!entry.drawFlag || entry.skipFlag0 || entry.skipFlag1 ||
               entry.opacity < 240 ||
               isYuzuTitleWhiteUtilityLayer("title_bg", entry.nodeLabel,
                                            entry.sourceKey)) {
                continue;
            }
            if(isYuzuTitleBackgroundLayer(entry.nodeLabel, entry.sourceKey)) {
                hasOpaqueBackground = true;
            }
            if(isYuzuNumberedTitleCompositeSource(entry.sourceKey)) {
                hasOpaqueNumberedComposite = true;
            }
            if(isYuzuTitlePositionLayer(entry.nodeLabel, entry.sourceKey) ||
               isYuzuTitleStandaloneCharacterLayer(entry.nodeLabel,
                                                   entry.sourceKey)) {
                ++opaqueTitleCharacters;
            }
            if(isYuzuTitleLogoLayer(entry.nodeLabel, entry.sourceKey) ||
               isYuzuTitleMainLayer(entry.nodeLabel, entry.sourceKey)) {
                hasOpaqueMainOrLogo = true;
            }
            if(isYuzuTitleNormalPresentationLayer(entry)) {
                ++opaqueNormalPresentationItems;
            }
        }
        return (hasOpaqueBackground && opaqueTitleCharacters >= 3 &&
                hasOpaqueMainOrLogo) ||
            // A normal title card commonly starts with its opaque background
            // and character while a white transition is still fading out.
            // That is a useful delta-composition base, but not the terminal
            // presentation frame.  The title/main overlay is the lifecycle
            // signal that the card is ready to retain after the motion ends.
            (opaqueNormalPresentationItems >= 2 && hasOpaqueMainOrLogo) ||
            (hasOpaqueNumberedComposite && hasOpaqueMainOrLogo);
    }

    void logYuzuTitlePreparedSummary(
        const motion::detail::PlayerRuntime &runtime,
        const std::string &motionPath,
        double frameTick) {
        if(!LOGGER || !shouldDebugTitleRender(motionPath)) {
            return;
        }
        const int frameBucket =
            static_cast<int>(std::floor(frameTick / 5.0));
        if(!markRenderDebugLogged(fmt::format(
               "title-prepared-summary|{}|{}", motionPath, frameBucket))) {
            return;
        }

        std::ostringstream summary;
        int logged = 0;
        for(const auto &entry : runtime.preparedRenderItems) {
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

    bool resolvePsbSourceDimensions(
        const motion::detail::MotionSnapshot &snapshot,
        const std::string &sourceKey,
        float &width,
        float &height) {
        width = 0.0f;
        height = 0.0f;
        if(sourceKey.rfind("src/", 0) != 0 || !snapshot.root) {
            return false;
        }

        const auto sourcePath = sourceKey.substr(4);
        const auto slash = sourcePath.find('/');
        if(slash == std::string::npos || slash == 0 ||
           slash + 1 >= sourcePath.size()) {
            return false;
        }

        const auto iconNode = navigatePSBPath(
            snapshot.root,
            "source/" + sourcePath.substr(0, slash) + "/icon/" +
                sourcePath.substr(slash + 1));
        if(!iconNode) {
            return false;
        }

        auto sourceWidth =
            psbDictionaryNumber(iconNode, "width").value_or(0.0);
        auto sourceHeight =
            psbDictionaryNumber(iconNode, "height").value_or(0.0);
        if(sourceWidth <= 0.0) {
            sourceWidth = psbDictionaryNumber(iconNode, "truncated_width")
                              .value_or(0.0);
        }
        if(sourceHeight <= 0.0) {
            sourceHeight = psbDictionaryNumber(iconNode, "truncated_height")
                               .value_or(0.0);
        }
        if(!std::isfinite(sourceWidth) || !std::isfinite(sourceHeight) ||
           sourceWidth <= 0.0 || sourceHeight <= 0.0) {
            return false;
        }

        width = static_cast<float>(sourceWidth);
        height = static_cast<float>(sourceHeight);
        return true;
    }

    bool hasDuplicatedNumberedTitleScale(
        const motion::detail::MotionSnapshot &snapshot,
        const motion::detail::PlayerRuntime::PreparedRenderItem &entry) {
        float sourceWidth = 0.0f;
        float sourceHeight = 0.0f;
        if(!resolvePsbSourceDimensions(snapshot, entry.sourceKey,
                                       sourceWidth, sourceHeight)) {
            return false;
        }

        // The PSB quad is ordered top-left, top-right, bottom-right,
        // bottom-left. Measure its two transformed edge lengths instead of
        // its AABB so rotation does not look like an extra scale.
        const float renderedWidth = std::hypot(
            entry.corners[2] - entry.corners[0],
            entry.corners[3] - entry.corners[1]);
        const float renderedHeight = std::hypot(
            entry.corners[6] - entry.corners[0],
            entry.corners[7] - entry.corners[1]);
        const float scaleX = renderedWidth / sourceWidth;
        const float scaleY = renderedHeight / sourceHeight;
        constexpr float kNestedScaleMin = 2.15f;
        constexpr float kNestedScaleMax = 2.35f;
        constexpr float kMaxAnisotropy = 0.12f;
        return std::isfinite(scaleX) && std::isfinite(scaleY) &&
            scaleX >= kNestedScaleMin && scaleX <= kNestedScaleMax &&
            scaleY >= kNestedScaleMin && scaleY <= kNestedScaleMax &&
            std::fabs(scaleX - scaleY) <= kMaxAnisotropy;
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

    bool computeCenteredMotionMessageSafeScale(
        tTJSNI_BaseLayer *presentationLayer,
        double contentWidth,
        double contentHeight,
        int canvasWidth,
        int canvasHeight,
        double currentScale,
        double &safeScale,
        tjs_int *outPadding = nullptr,
        tjs_int *outSafeBottom = nullptr) {
        if(!presentationLayer || contentWidth <= 0.0 || contentHeight <= 0.0 ||
           canvasWidth <= 0 || canvasHeight <= 0 || currentScale <= 0.0) {
            return false;
        }

        tjs_int messageTop = 0;
        if(!findCenteredPresentationMessageUiTop(presentationLayer, canvasHeight,
                                                 messageTop)) {
            return false;
        }

        const tjs_int padding =
            std::max<tjs_int>(8, static_cast<tjs_int>(canvasHeight / 72));
        const tjs_int safeBottom = std::max<tjs_int>(1, messageTop - padding);
        if(safeBottom <= padding ||
           safeBottom < std::max<tjs_int>(1, canvasHeight / 3)) {
            return false;
        }
        const double centerY = canvasHeight / 2.0;
        const double centeredUpperRoom = centerY - padding;
        const double centeredLowerRoom = safeBottom - centerY;
        double adjustedScale = currentScale;
        if(centeredUpperRoom > 0.0 && centeredLowerRoom > 0.0) {
            adjustedScale = std::min(
                adjustedScale,
                (2.0 * std::min(centeredUpperRoom, centeredLowerRoom)) /
                    contentHeight);
        } else {
            adjustedScale = std::min(
                adjustedScale, safeBottom / contentHeight);
        }
        adjustedScale = std::min(adjustedScale, canvasWidth / contentWidth);
        if(adjustedScale <= 0.0) {
            return false;
        }

        safeScale = adjustedScale;
        if(outPadding) {
            *outPadding = padding;
        }
        if(outSafeBottom) {
            *outSafeBottom = safeBottom;
        }
        return true;
    }

    bool layerBelongsToCgViewPresentation(tTJSNI_BaseLayer *layer);
    std::string describeLayerForDebug(tTJSNI_BaseLayer *layer);
    bool queryMainWindowCanvasSize(int &width, int &height);

    void adjustPreparedRenderItemsForCenteredGameMotion(
        motion::detail::PlayerRuntime &runtime,
        const std::string &motionPath,
        tjs_int canvasWidth,
        tjs_int canvasHeight,
        double configuredResolution,
        tTJSNI_BaseLayer *presentationLayer = nullptr) {
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
        const bool yuzuSdMotion =
            isYuzuSdPresentationMotionPath(motionPath);
        int layoutCanvasWidth = canvasWidth;
        int layoutCanvasHeight = canvasHeight;
        int visibleCanvasWidth = 0;
        int visibleCanvasHeight = 0;
        if(yuzuSdMotion &&
           queryMainWindowCanvasSize(visibleCanvasWidth, visibleCanvasHeight) &&
           visibleCanvasWidth > 0 && visibleCanvasHeight > 0) {
            layoutCanvasWidth = std::min(canvasWidth, visibleCanvasWidth);
            layoutCanvasHeight = std::min(canvasHeight, visibleCanvasHeight);
        }
        const float canvasW = static_cast<float>(layoutCanvasWidth);
        const float canvasH = static_cast<float>(layoutCanvasHeight);
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
        float stageWidth = width;
        float stageHeight = height;
        if(runtime.activeMotion) {
            if(std::isfinite(runtime.activeMotion->width) &&
               runtime.activeMotion->width > 0.0) {
                stageWidth = static_cast<float>(runtime.activeMotion->width);
            }
            if(std::isfinite(runtime.activeMotion->height) &&
               runtime.activeMotion->height > 0.0) {
                stageHeight = static_cast<float>(runtime.activeMotion->height);
            }
        }
        if(LOGGER && shouldDebugTitleRender(motionPath) &&
           markRenderDebugLogged("centered-game-motion-candidate:" + motionPath)) {
            LOGGER->info(
                "motion centered game presentation candidate: motion={} bounds=[{:.1f},{:.1f},{:.1f},{:.1f}] size={:.1f}x{:.1f} canvas={}x{} stage={:.1f}x{:.1f} topLeft={} centered={} preparedItems={}",
                motionPath, bounds[0], bounds[1], bounds[2], bounds[3],
                width, height, canvasWidth, canvasHeight,
                stageWidth, stageHeight,
                topLeftOrigin ? 1 : 0, centeredOrigin ? 1 : 0,
                runtime.preparedRenderItems.size());
        }
        const bool plausibleTopLeftStage =
            topLeftOrigin &&
            std::isfinite(stageWidth) && std::isfinite(stageHeight) &&
            stageWidth > 0.0f && stageHeight > 0.0f &&
            width <= stageWidth * 1.05f && height <= stageHeight * 1.05f;
        const bool plausibleCenteredStage =
            centeredOrigin &&
            std::isfinite(stageWidth) && std::isfinite(stageHeight) &&
            stageWidth > 0.0f && stageHeight > 0.0f &&
            width <= stageWidth * 1.20f && height <= stageHeight * 1.20f;
        const bool plausibleAnimatedStage =
            !topLeftOrigin && !centeredOrigin &&
            std::isfinite(stageWidth) && std::isfinite(stageHeight) &&
            stageWidth > 0.0f && stageHeight > 0.0f &&
            width <= stageWidth * 2.20f &&
            height <= stageHeight * 2.20f &&
            currentCenterX >= -stageWidth &&
            currentCenterX <= stageWidth * 2.0f &&
            currentCenterY >= -stageHeight &&
            currentCenterY <= stageHeight * 2.0f;
        if(width < canvasW * 0.08f || height < canvasH * 0.08f ||
           ((!plausibleTopLeftStage && !plausibleCenteredStage &&
             !plausibleAnimatedStage) &&
            (width > canvasW * 0.90f || height > canvasH * 0.90f))) {
            if(LOGGER && shouldDebugTitleRender(motionPath) &&
               std::getenv("AETHERKIRI_MOTION_LAYER_DEBUG")) {
                LOGGER->info(
                    "motion centered game presentation skipped: motion={} reason=size bounds=[{:.1f},{:.1f},{:.1f},{:.1f}] size={:.1f}x{:.1f} canvas={}x{} stage={:.1f}x{:.1f} topLeft={} centered={} animatedStage={} preparedItems={}",
                    motionPath, bounds[0], bounds[1], bounds[2], bounds[3],
                    width, height, canvasWidth, canvasHeight,
                    stageWidth, stageHeight,
                    topLeftOrigin ? 1 : 0, centeredOrigin ? 1 : 0,
                    plausibleAnimatedStage ? 1 : 0,
                    runtime.preparedRenderItems.size());
            }
            return;
        }

        if(!topLeftOrigin && !centeredOrigin && !plausibleAnimatedStage) {
            return;
        }

        // `resolution` belongs to the motion's logical coordinate system. It
        // scales geometry only; source and target bitmaps retain their native
        // pixel dimensions (for example a 960x540 stage backed by a
        // 1920x1440 image). Low-resolution targets are grown separately in
        // prepareMotionPresentationLayerForRender().
        float motionScale =
            (std::isfinite(configuredResolution) &&
             configuredResolution > 0.0)
                ? static_cast<float>(100.0 / configuredResolution)
                : 1.0f;
        const float baseMotionScale = motionScale;
        double messageSafeScale = motionScale;
        tjs_int messagePadding = 0;
        tjs_int messageSafeBottom = 0;
        const bool storyYuzuSdLayer = yuzuSdMotion;
        const bool constrainedForMessageUi =
            !storyYuzuSdLayer && computeCenteredMotionMessageSafeScale(
                presentationLayer, width, height, canvasWidth, canvasHeight,
                motionScale, messageSafeScale, &messagePadding,
                &messageSafeBottom);
        if(constrainedForMessageUi &&
           std::isfinite(messageSafeScale) && messageSafeScale > 0.0) {
            motionScale = static_cast<float>(messageSafeScale);
        }
        const bool stageBasedOrigin = topLeftOrigin || plausibleAnimatedStage;
        const float stageCenterX = stageBasedOrigin ? stageWidth * 0.5f : 0.0f;
        const float stageCenterY = stageBasedOrigin ? stageHeight * 0.5f : 0.0f;
        if(!std::isfinite(stageCenterX) || !std::isfinite(stageCenterY)) {
            return;
        }
        std::array<float, 4> scaledBounds = bounds;
        if(std::isfinite(motionScale) && motionScale > 0.0f &&
           std::fabs(motionScale - 1.0f) >= 0.0001f) {
            for(auto &entry : runtime.preparedRenderItems) {
                scalePreparedRenderItem(entry, stageCenterX, stageCenterY,
                                        motionScale);
            }
            scaledBounds[0] = stageCenterX +
                (bounds[0] - stageCenterX) * motionScale;
            scaledBounds[1] = stageCenterY +
                (bounds[1] - stageCenterY) * motionScale;
            scaledBounds[2] = stageCenterX +
                (bounds[2] - stageCenterX) * motionScale;
            scaledBounds[3] = stageCenterY +
                (bounds[3] - stageCenterY) * motionScale;
        }

        const float targetCenterX = canvasW * 0.5f;
        const float targetCenterY = canvasH * 0.5f;
        const float scaledCenterX =
            (scaledBounds[0] + scaledBounds[2]) * 0.5f;
        const float scaledCenterY =
            (scaledBounds[1] + scaledBounds[3]) * 0.5f;
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
                    "motion centered game presentation: motion={} bounds=[{:.1f},{:.1f},{:.1f},{:.1f}] scaledBounds=[{:.1f},{:.1f},{:.1f},{:.1f}] canvas={}x{} resolution={:.2f} baseScale={:.4f} scale={:.4f} topLeft={} centered={} animatedStage={} constrained={} safeBottom={} padding={} contentCenter={:.1f},{:.1f} translate={:.1f},{:.1f}",
                motionPath, bounds[0], bounds[1], bounds[2], bounds[3],
                scaledBounds[0], scaledBounds[1], scaledBounds[2],
                scaledBounds[3], layoutCanvasWidth, layoutCanvasHeight,
                configuredResolution, baseMotionScale, motionScale,
                topLeftOrigin ? 1 : 0, centeredOrigin ? 1 : 0,
                plausibleAnimatedStage ? 1 : 0,
                constrainedForMessageUi ? 1 : 0, messageSafeBottom,
                messagePadding, scaledCenterX,
                scaledCenterY, translateX, translateY);
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
            const bool hasLayerNeedingStableOrdering = std::any_of(
                runtime.preparedRenderItems.begin(),
                runtime.preparedRenderItems.end(),
                [](const auto &entry) {
                    if(!entry.drawFlag || entry.skipFlag0 || entry.skipFlag1 ||
                       entry.opacity <= 0) {
                        return false;
                    }
                    return isYuzuTitlePositionLayer(entry.nodeLabel,
                                                    entry.sourceKey) ||
                        isYuzuTitleStandaloneCharacterLayer(entry.nodeLabel,
                                                            entry.sourceKey) ||
                        isYuzuTitleLogoLayer(entry.nodeLabel, entry.sourceKey);
                });
            if(hasLayerNeedingStableOrdering) {
                std::stable_sort(
                    runtime.preparedRenderItems.begin(),
                    runtime.preparedRenderItems.end(),
                    [](const auto &lhs, const auto &rhs) {
                        return yuzuTitleIntroDrawPriority(lhs) <
                            yuzuTitleIntroDrawPriority(rhs);
                    });
            }
            if(hasSyntheticIntroLayer) {
                std::vector<std::string> syntheticIntroSources;
                syntheticIntroSources.reserve(runtime.preparedRenderItems.size());
                for(const auto &entry : runtime.preparedRenderItems) {
                    if(entry.drawFlag && !entry.skipFlag0 && !entry.skipFlag1 &&
                       entry.opacity > 0 && isYuzuTitleSyntheticIntroLayer(entry)) {
                        syntheticIntroSources.push_back(
                            renderDebugLowercase(entry.sourceKey));
                    }
                }
                int suppressedClipCharacters = 0;
                int suppressedCompositeLayers = 0;
                for(auto &entry : runtime.preparedRenderItems) {
                    if(entry.drawFlag && !entry.skipFlag0 && !entry.skipFlag1 &&
                       entry.opacity > 0 &&
                       isYuzuTitleClipAnimatedCharacterLayer(entry) &&
                       std::find(syntheticIntroSources.begin(),
                                 syntheticIntroSources.end(),
                                 renderDebugLowercase(entry.sourceKey)) !=
                           syntheticIntroSources.end()) {
                        entry.skipFlag0 = true;
                        ++suppressedClipCharacters;
                    } else if(entry.drawFlag && !entry.skipFlag0 &&
                              !entry.skipFlag1 && entry.opacity > 0 &&
                              isYuzuTitlePositionLayer(entry.nodeLabel,
                                                       entry.sourceKey)) {
                        entry.skipFlag0 = true;
                        ++suppressedCompositeLayers;
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
                if(suppressedCompositeLayers > 0 && LOGGER &&
                   shouldDebugTitleRender(motionPath) &&
                   markRenderDebugLogged(fmt::format(
                       "yuzu-title-composite-delayed:{}:{}",
                       motionPath, suppressedCompositeLayers))) {
                    LOGGER->info(
                        "motion title composite layer delayed until intro completes: motion={} suppressedCompositeLayers={}",
                        motionPath, suppressedCompositeLayers);
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
                       isYuzuTitleStandaloneCharacterLayer(entry.nodeLabel,
                                                           entry.sourceKey)) {
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
                        "motion title composite character layer active: motion={} suppressedStandaloneCharacters={}",
                        motionPath, suppressed);
                }
            }
        }

        bool usesCenteredPresentationOrigin = isLogoMotion;
        float centeredPresentationTranslateX =
            static_cast<float>(canvasWidth) * 0.5f;
        float centeredPresentationTranslateY =
            static_cast<float>(canvasHeight) * 0.5f;
        bool centeredOriginFromReference = false;
        const motion::detail::PlayerRuntime::PreparedRenderItem
            *titleOriginReference = nullptr;
        int titleOriginReferencePriority = -1;
        if(isTitleMotion) {
            for(const auto &entry : runtime.preparedRenderItems) {
                if(!entry.drawFlag || entry.skipFlag0 || entry.skipFlag1 ||
                   entry.opacity <= 0 ||
                   isYuzuTitleWhiteUtilityLayer(motionPath, entry.nodeLabel,
                                                entry.sourceKey)) {
                    continue;
                }

                int priority = -1;
                if(isYuzuTitleMainLayer(entry.nodeLabel, entry.sourceKey)) {
                    priority = 3;
                } else if(isYuzuTitlePositionLayer(entry.nodeLabel,
                                                   entry.sourceKey)) {
                    priority = 2;
                } else if(isYuzuTitleBackgroundLayer(entry.nodeLabel,
                                                     entry.sourceKey) ||
                          isYuzuTitleNormalPresentationLayer(entry)) {
                    priority = 1;
                }
                if(priority <= titleOriginReferencePriority) {
                    continue;
                }

                auto referenceBox = boundsFromRenderCorners(entry.corners);
                if(!validRenderPaintBox(referenceBox)) {
                    referenceBox = entry.paintBox;
                }
                if(!validRenderPaintBox(referenceBox)) {
                    continue;
                }
                titleOriginReference = &entry;
                titleOriginReferencePriority = priority;
            }

            if(titleOriginReference) {
                auto referenceBox =
                    boundsFromRenderCorners(titleOriginReference->corners);
                if(!validRenderPaintBox(referenceBox)) {
                    referenceBox = titleOriginReference->paintBox;
                }
                const float centerX =
                    (referenceBox[0] + referenceBox[2]) * 0.5f;
                const float centerY =
                    (referenceBox[1] + referenceBox[3]) * 0.5f;
                const bool crossesOrigin =
                    referenceBox[0] < 0.0f && referenceBox[1] < 0.0f &&
                    referenceBox[2] > 0.0f && referenceBox[3] > 0.0f;
                const float centeredToleranceX =
                    std::max(32.0f, static_cast<float>(canvasWidth) * 0.15f);
                const float centeredToleranceY =
                    std::max(32.0f, static_cast<float>(canvasHeight) * 0.15f);
                usesCenteredPresentationOrigin =
                    crossesOrigin &&
                    std::fabs(centerX) <= centeredToleranceX &&
                    std::fabs(centerY) <= centeredToleranceY;
                centeredOriginFromReference =
                    usesCenteredPresentationOrigin;
            }
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
                const bool isTitleCanvasReference =
                    isTitleMotion &&
                    isYuzuTitleMainLayer(entry.nodeLabel, entry.sourceKey);
                if(isTitleMotion) {
                    return;
                }
                if(usesCenteredPresentationOrigin &&
                   ((!isTitlePositionReference && !isTitleCanvasReference) ||
                    centeredOriginFromReference)) {
                    return;
                }
                const bool isLogoBackdrop =
                    isLogoMotion &&
                    (source == "src/logo/icon50" ||
                     source.find("/logo/icon/icon50") != std::string::npos);
                const bool isNormalTitleReference =
                    isTitleMotion &&
                    isYuzuTitleNormalPresentationLayer(entry);
                switch(referenceKind) {
                    case 0:
                        if(isTitleMotion &&
                           !isTitlePositionReference &&
                           !isTitleCanvasReference &&
                           ((!isYuzuTitleBackgroundLayer(entry.nodeLabel,
                                                         entry.sourceKey) &&
                             !isNormalTitleReference) ||
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
                const bool nearCanvasSize =
                    std::fabs(width - static_cast<float>(canvasWidth)) <=
                        std::max(8.0f, static_cast<float>(canvasWidth) * 0.08f) &&
                    std::fabs(height - static_cast<float>(canvasHeight)) <=
                        std::max(8.0f, static_cast<float>(canvasHeight) * 0.08f);
                if(isTitleCanvasReference && crossesOrigin && nearCanvasSize &&
                   std::fabs(centerX) <=
                       static_cast<float>(canvasWidth) * 0.1f &&
                   std::fabs(centerY) <=
                       static_cast<float>(canvasHeight) * 0.1f) {
                    usesCenteredPresentationOrigin = true;
                    centeredOriginFromReference = true;
                    centeredPresentationTranslateX =
                        std::round(static_cast<float>(canvasWidth) * 0.5f);
                    centeredPresentationTranslateY =
                        std::round(static_cast<float>(canvasHeight) * 0.5f);
                    return;
                }
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

        // Some numbered Yuzu title cards author their character under two
        // nested 150% groups. Collapse that duplicated 225% transform only
        // when the prepared quad is actually 2.25x its PSB source. Matching
        // by title filename alone also catches 1:1 cards and shrinks them to
        // 4/9 of their intended size.
        if(runtime.activeMotion &&
           isYuzuNumberedTitleCharacterMotion(motionPath)) {
            const float canvasCenterX =
                static_cast<float>(canvasWidth) * 0.5f;
            const float canvasCenterY =
                static_cast<float>(canvasHeight) * 0.5f;
            for(auto &entry : runtime.preparedRenderItems) {
                if(!entry.drawFlag || entry.skipFlag0 || entry.skipFlag1 ||
                   entry.opacity <= 0 ||
                   !hasDuplicatedNumberedTitleScale(
                       *runtime.activeMotion, entry)) {
                    continue;
                }
                auto bounds = boundsFromRenderCorners(entry.corners);
                if(!validRenderPaintBox(bounds)) {
                    bounds = entry.paintBox;
                }
                if(!validRenderPaintBox(bounds)) {
                    continue;
                }
                const float currentCenterX =
                    (bounds[0] + bounds[2]) * 0.5f;
                const float currentCenterY =
                    (bounds[1] + bounds[3]) * 0.5f;
                const float designCenterX = canvasCenterX +
                    (currentCenterX - canvasCenterX) / 1.5f;
                const float designCenterY = canvasCenterY +
                    (currentCenterY - canvasCenterY) / 1.5f;
                scalePreparedRenderItem(entry, currentCenterX,
                                        currentCenterY, 4.0f / 9.0f);
                translatePreparedRenderItem(entry,
                                            designCenterX - currentCenterX,
                                            designCenterY - currentCenterY);
            }
        }

        // Some Yuzu titles mix high-resolution source images with a smaller
        // design coordinate system. Do not infer another scale when the
        // current frame already contains a canvas-sized stage; that would
        // stretch a local character node such as title/pos2 over the screen.
        if(isTitleMotion) {
            const bool hasCanvasSizedStage = std::any_of(
                runtime.preparedRenderItems.begin(),
                runtime.preparedRenderItems.end(),
                [&](const auto &entry) {
                    if(!entry.drawFlag || entry.skipFlag0 || entry.skipFlag1 ||
                       entry.opacity <= 0 ||
                       isYuzuTitleWhiteUtilityLayer(
                           motionPath, entry.nodeLabel, entry.sourceKey)) {
                        return false;
                    }
                    if(!isYuzuTitleBackgroundLayer(entry.nodeLabel,
                                                   entry.sourceKey) &&
                       !isYuzuTitleNormalPresentationLayer(entry) &&
                       !isYuzuTitleMainLayer(entry.nodeLabel,
                                             entry.sourceKey)) {
                        return false;
                    }
                    auto bounds = boundsFromRenderCorners(entry.corners);
                    if(!validRenderPaintBox(bounds) && entry.hasViewport &&
                       validRenderPaintBox(entry.viewport)) {
                        bounds = entry.viewport;
                    }
                    if(!validRenderPaintBox(bounds) &&
                       validRenderPaintBox(entry.paintBox)) {
                        bounds = entry.paintBox;
                    }
                    if(!validRenderPaintBox(bounds)) {
                        return false;
                    }
                    const float width = bounds[2] - bounds[0];
                    const float height = bounds[3] - bounds[1];
                    const float widthRatio =
                        width / static_cast<float>(canvasWidth);
                    const float heightRatio =
                        height / static_cast<float>(canvasHeight);
                    return widthRatio >= 0.88f && widthRatio <= 1.12f &&
                        heightRatio >= 0.88f && heightRatio <= 1.12f;
                });
            if(hasCanvasSizedStage) {
                if(LOGGER && shouldDebugTitleRender(motionPath) &&
                   markRenderDebugLogged(
                       "yuzu-presentation-native-stage:" + motionPath)) {
                    LOGGER->info(
                        "motion presentation scale skipped: motion={} reason=canvas-sized-stage canvas={}x{}",
                        motionPath, canvasWidth, canvasHeight);
                }
                return;
            }
        }

        float refWidth = 0.0f;
        float refHeight = 0.0f;
        float refArea = 0.0f;
        int refOpacity = -1;
        auto considerReference =
            [&](const motion::detail::PlayerRuntime::PreparedRenderItem &entry,
                int referenceKind) {
                if(!entry.drawFlag || entry.skipFlag0 || entry.skipFlag1 ||
                   entry.opacity <= 0 ||
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
                if(isTitleMotion &&
                   (width < static_cast<float>(canvasWidth) * 0.25f ||
                    height < static_cast<float>(canvasHeight) * 0.25f)) {
                    return;
                }
                const float area = width * height;
                if(entry.opacity > refOpacity ||
                   (entry.opacity == refOpacity && area > refArea)) {
                    refOpacity = entry.opacity;
                    refArea = area;
                    refWidth = width;
                    refHeight = height;
                }
            };

        // A title background represents the design stage. A named position
        // node is only local content and must never determine canvas scale.
        if(isTitleMotion) {
            for(const auto &entry : runtime.preparedRenderItems) {
                considerReference(entry, 1);
            }
        } else {
            for(const auto &entry : runtime.preparedRenderItems) {
                considerReference(entry, 0);
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
            const float maximumExtent = isTitleMotion ? 4.0f : 1.25f;
            return width <= refWidth * maximumExtent &&
                height <= refHeight * maximumExtent;
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
        const tTVPBaseBitmap &source,
        tTVPBaseBitmap &destination,
        const std::array<std::uint32_t, 4> &packedColors,
        bool halfAlphaBlend,
        bool tintDefaultNeutralMask,
        bool tintOnlyWhitePixels,
        const std::array<int, 4> &neutralTint) {
        const auto c0 = packedColors[0];
        const auto c1 = packedColors[1];
        const auto c2 = packedColors[2];
        const auto c3 = packedColors[3];
        const bool alphaOnlyMask = bitmapLooksAlphaOnlyMask(source);
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
        const int width = static_cast<int>(source.GetWidth());
        const int height = static_cast<int>(source.GetHeight());
        if(width <= 0 || height <= 0) {
            return;
        }
        if(destination.GetWidth() != source.GetWidth() ||
           destination.GetHeight() != source.GetHeight()) {
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

        // Corner colors are commonly animated as one uniform value. In that
        // case the general bilinear loop needlessly evaluates four lerps for
        // every pixel (several million pixels for a title-card source).
        // Keeping this as a separate branch is bit-for-bit equivalent because
        // lerp(a, a, ...) is exactly a for every coordinate.
        const bool hasUniformTint =
            topLeft == topRight && topLeft == bottomRight &&
            topLeft == bottomLeft;

        // Resolve the writable storage once before dispatching row workers.
        // GetScanLineForWrite() may perform copy-on-write and update texture
        // state, so it must not be called concurrently from worker threads.
        const auto *sourcePixels = static_cast<const std::uint8_t *>(
            source.GetScanLine(0));
        auto *destinationPixels = static_cast<std::uint8_t *>(
            destination.GetScanLineForWrite(0));
        const auto sourcePitch =
            static_cast<std::ptrdiff_t>(source.GetPitchBytes());
        const auto destinationPitch =
            static_cast<std::ptrdiff_t>(destination.GetPitchBytes());
        if(!sourcePixels || !destinationPixels || sourcePitch == 0 ||
           destinationPitch == 0) {
            return;
        }

        const size_t pixelCount =
            static_cast<size_t>(width) * static_cast<size_t>(height);
        constexpr size_t kParallelTintThreshold = 512u * 512u;
        const int workerCount = pixelCount >= kParallelTintThreshold
            ? std::max(1, std::min<int>(TVPGetThreadNum(), height))
            : 1;

        const auto tintRows = [&](int workerIndex) {
            const int yBegin = height * workerIndex / workerCount;
            const int yEnd = height * (workerIndex + 1) / workerCount;
            for(int y = yBegin; y < yEnd; ++y) {
                const auto *sourceRow = sourcePixels +
                    static_cast<std::ptrdiff_t>(y) * sourcePitch;
                auto *destinationRow = destinationPixels +
                    static_cast<std::ptrdiff_t>(y) * destinationPitch;
                const int rowLeftR = hasUniformTint
                    ? topLeft[0]
                    : lerpChannel(topLeft[0], bottomLeft[0], y, spanY);
                const int rowLeftG = hasUniformTint
                    ? topLeft[1]
                    : lerpChannel(topLeft[1], bottomLeft[1], y, spanY);
                const int rowLeftB = hasUniformTint
                    ? topLeft[2]
                    : lerpChannel(topLeft[2], bottomLeft[2], y, spanY);
                const int rowLeftA = hasUniformTint
                    ? topLeft[3]
                    : lerpChannel(topLeft[3], bottomLeft[3], y, spanY);
                const int rowRightR = hasUniformTint
                    ? topLeft[0]
                    : lerpChannel(topRight[0], bottomRight[0], y, spanY);
                const int rowRightG = hasUniformTint
                    ? topLeft[1]
                    : lerpChannel(topRight[1], bottomRight[1], y, spanY);
                const int rowRightB = hasUniformTint
                    ? topLeft[2]
                    : lerpChannel(topRight[2], bottomRight[2], y, spanY);
                const int rowRightA = hasUniformTint
                    ? topLeft[3]
                    : lerpChannel(topRight[3], bottomRight[3], y, spanY);

                // This is the dominant animation case: one uniform packed
                // color applied to an ordinary RGBA image. Keep it outside
                // the general per-pixel mask/gradient branches. Blend mode
                // 0x10 uses divisor 128, so a right shift is exactly the same
                // integer operation for these non-negative channel values.
                if(hasUniformTint && !tintOnlyWhitePixels &&
                   !alphaOnlyMask && colorDivisor == 128 &&
                   rowLeftA == 255) {
                    for(int x = 0; x < width; ++x) {
                        const auto *src =
                            sourceRow + static_cast<size_t>(x) * 4u;
                        auto *dst =
                            destinationRow + static_cast<size_t>(x) * 4u;
                        const int red = rowLeftR * static_cast<int>(src[2]);
                        const int green = rowLeftG * static_cast<int>(src[1]);
                        const int blue = rowLeftB * static_cast<int>(src[0]);
                        const int tintedRed = red >> 7;
                        const int tintedGreen = green >> 7;
                        const int tintedBlue = blue >> 7;
                        dst[2] = static_cast<std::uint8_t>(
                            tintedRed > 255 ? 255 : tintedRed);
                        dst[1] = static_cast<std::uint8_t>(
                            tintedGreen > 255 ? 255 : tintedGreen);
                        dst[0] = static_cast<std::uint8_t>(
                            tintedBlue > 255 ? 255 : tintedBlue);
                        dst[3] = src[3];
                    }
                    continue;
                }

                for(int x = 0; x < width; ++x) {
                    const auto *src =
                        sourceRow + static_cast<size_t>(x) * 4u;
                    auto *dst =
                        destinationRow + static_cast<size_t>(x) * 4u;
                    // White-mask classification is relatively expensive and
                    // is irrelevant for ordinary color tinting. Short-circuit
                    // it here instead of scanning RGB thresholds for every
                    // pixel of multi-megapixel title-card sources.
                    const bool whiteMaskPixel =
                        tintOnlyWhitePixels && pixelLooksWhiteMask(src);
                    if(tintOnlyWhitePixels && !whiteMaskPixel) {
                        std::memcpy(dst, src, 4u);
                        continue;
                    }
                    const int tintR = hasUniformTint
                        ? rowLeftR
                        : lerpChannel(rowLeftR, rowRightR, x, spanX);
                    const int tintG = hasUniformTint
                        ? rowLeftG
                        : lerpChannel(rowLeftG, rowRightG, x, spanX);
                    const int tintB = hasUniformTint
                        ? rowLeftB
                        : lerpChannel(rowLeftB, rowRightB, x, spanX);
                    const int tintA = hasUniformTint
                        ? rowLeftA
                        : lerpChannel(rowLeftA, rowRightA, x, spanX);
                    if(colorsAreNeutral && tintOnlyWhitePixels &&
                       whiteMaskPixel) {
                        const int srcA = static_cast<int>(src[3]);
                        dst[2] = static_cast<std::uint8_t>(tintR);
                        dst[1] = static_cast<std::uint8_t>(tintG);
                        dst[0] = static_cast<std::uint8_t>(tintB);
                        dst[3] = static_cast<std::uint8_t>(
                            std::min(255, tintA * srcA / 255));
                        continue;
                    }
                    const int srcR =
                        alphaOnlyMask ? 255 : static_cast<int>(src[2]);
                    const int srcG =
                        alphaOnlyMask ? 255 : static_cast<int>(src[1]);
                    const int srcB =
                        alphaOnlyMask ? 255 : static_cast<int>(src[0]);
                    dst[2] = static_cast<std::uint8_t>(std::min(
                        255, tintR * srcR / colorDivisor));
                    dst[1] = static_cast<std::uint8_t>(std::min(
                        255, tintG * srcG / colorDivisor));
                    dst[0] = static_cast<std::uint8_t>(std::min(
                        255, tintB * srcB / colorDivisor));
                    dst[3] = static_cast<std::uint8_t>(std::min(
                        255, tintA * static_cast<int>(src[3]) / 255));
                }
            }
        };

        if(workerCount > 1) {
            TVPExecThreadTask(workerCount, tintRows);
        } else {
            tintRows(0);
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
        const auto ownerClosure = ownerVar.AsObjectClosureNoAddRef();
        tTJSVariant primaryVar;
        // primaryLayer is a native TJS property.  getObjectProperty() uses
        // TJS_IGNOREPROP intentionally for metadata lookup, which returns the
        // property dispatch itself instead of invoking its getter.  Passing
        // that dispatch as Layer(parent) makes the constructor reject it with
        // "Specify Layer class object" on the first D3D affine frame.
        if(!ownerClosure.Object ||
           TJS_FAILED(ownerClosure.PropGet(0, TJS_W("primaryLayer"), nullptr,
                                           &primaryVar, nullptr)) ||
           primaryVar.Type() != tvtObject || !primaryVar.AsObjectNoAddRef()) {
            return nullptr;
        }

        if(auto *resolved = tryResolveLayerDispatch(primaryVar)) {
            return resolved;
        }
        // A parent passed to Layer's constructor must be a verified native
        // Layer.  Never fall back to an arbitrary script/property object.
        return nullptr;
    }

    tTJSNI_BaseLayer *resolvePrimaryAncestorLayer(
        tTJSNI_BaseLayer *layer) {
        while(layer && !layer->IsPrimary()) {
            layer = layer->GetParent();
        }
        return layer && layer->IsPrimary() ? layer : nullptr;
    }

    iTJSDispatch2 *resolvePrimaryAncestorLayerObject(
        tTJSNI_BaseLayer *layer) {
        auto *primary = resolvePrimaryAncestorLayer(layer);
        return primary ? primary->GetOwnerNoAddRef() : nullptr;
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

    bool closureHasLayerTreeOwnerInterface(const tTJSVariantClosure &closure) {
        if(!closure.Object) {
            return false;
        }
        tTJSVariant value;
        return TJS_SUCCEEDED(
            closure.PropGet(0, TJS_W("layerTreeOwnerInterface"), nullptr,
                            &value, nullptr));
    }

    tTJSVariant resolveLayerTreeOwnerVariantFromLayer(
        tTJSNI_BaseLayer *layer) {
        for(auto *current = layer; current; current = current->GetParent()) {
            const auto ownerClosure = current->GetActionOwnerNoAddRef();
            if(closureHasLayerTreeOwnerInterface(ownerClosure)) {
                return tTJSVariant(ownerClosure.Object, ownerClosure.ObjThis);
            }
        }

        if(auto *owner = resolveMainWindowOwnerObject()) {
            tTJSVariant ownerVar(owner, owner);
            if(ownerVar.Type() == tvtObject &&
               closureHasLayerTreeOwnerInterface(
                   ownerVar.AsObjectClosureNoAddRef())) {
                return ownerVar;
            }
        }
        return tTJSVariant();
    }

    iTJSDispatch2 *createLayerObjectWithOwnerVariant(
        const tTJSVariant &layerTreeOwnerVariant,
        iTJSDispatch2 *parentLayerObject) {
        if(layerTreeOwnerVariant.Type() != tvtObject ||
           !layerTreeOwnerVariant.AsObjectNoAddRef()) {
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
            tTJSVariant ownerVar(layerTreeOwnerVariant);
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

    iTJSDispatch2 *createLayerObject(iTJSDispatch2 *layerTreeOwnerObject,
                                     iTJSDispatch2 *parentLayerObject) {
        if(!layerTreeOwnerObject) {
            return nullptr;
        }
        tTJSVariant ownerVar(layerTreeOwnerObject, layerTreeOwnerObject);
        return createLayerObjectWithOwnerVariant(ownerVar, parentLayerObject);
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

    iTJSDispatch2 *ensureReusableLayerObjectWithOwnerVariant(
        tTJSVariant &slot,
        const tTJSVariant &layerTreeOwnerVariant,
        iTJSDispatch2 *parentLayerObject,
        tTVPLayerType layerType,
        bool visible,
        bool absoluteOrderMode = false) {
        if(!parentLayerObject ||
           layerTreeOwnerVariant.Type() != tvtObject ||
           !layerTreeOwnerVariant.AsObjectNoAddRef()) {
            return nullptr;
        }

        iTJSDispatch2 *layerObject =
            slot.Type() == tvtObject ? slot.AsObjectNoAddRef() : nullptr;
        if(!layerObject) {
            layerObject = createLayerObjectWithOwnerVariant(
                layerTreeOwnerVariant, parentLayerObject);
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

    bool queryMainWindowCanvasSize(int &width, int &height) {
        width = 0;
        height = 0;
        if(!TVPMainWindow || !TVPMainWindow->GetDrawDevice()) {
            return false;
        }
        TVPMainWindow->GetDrawDevice()->GetSrcSize(width, height);
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

    bool motionLayerIsDrawableCgViewSurface(tTJSNI_BaseLayer *layer) {
        return layer && layer->GetOwnerNoAddRef() && layer->GetVisible() &&
            layer->GetOpacity() > 0;
    }

    bool layerNameContains(tTJSNI_BaseLayer *layer, const std::string &needle) {
        if(!layer) {
            return false;
        }
        const auto name =
            renderDebugLowercase(motion::detail::narrow(layer->GetName()));
        return name.find(needle) != std::string::npos;
    }

    bool layerIsCgViewContainer(tTJSNI_BaseLayer *layer) {
        if(!layer) {
            return false;
        }
        const auto name =
            renderDebugLowercase(motion::detail::narrow(layer->GetName()));
        return name == "cg view layer" ||
            name.find("cg view layer :") != std::string::npos;
    }

    bool layerIsSpecificCgViewContainer(tTJSNI_BaseLayer *layer) {
        return layerNameContains(layer, "cg view layer :");
    }

    bool layerIsCgViewAffineSurface(tTJSNI_BaseLayer *layer) {
        if(!layer) {
            return false;
        }
        const auto name =
            renderDebugLowercase(motion::detail::narrow(layer->GetName()));
        return name.find("affinelayer") != std::string::npos &&
            name.find("cg view layer") != std::string::npos;
    }

    bool layerBelongsToCgViewPresentation(tTJSNI_BaseLayer *layer) {
        for(auto *current = layer; current; current = current->GetParent()) {
            if(layerIsCgViewContainer(current) ||
               layerIsCgViewAffineSurface(current)) {
                return true;
            }
        }
        return false;
    }

    iTJSDispatch2 *resolveCgViewAffineRenderParentObject(
        tTJSNI_BaseLayer *affineLayer) {
        if(!layerIsCgViewAffineSurface(affineLayer)) {
            return nullptr;
        }
        auto *parentLayer = affineLayer->GetParent();
        if(!layerIsSpecificCgViewContainer(parentLayer)) {
            return nullptr;
        }
        return parentLayer->GetOwnerNoAddRef();
    }

    void configureCgViewAffineRenderChildLayer(
        tTJSNI_BaseLayer *renderLayer,
        tTJSNI_BaseLayer *affineLayer,
        tTJSNI_BaseLayer *renderParentLayer) {
        if(!renderLayer || !affineLayer || !renderParentLayer) {
            return;
        }

        renderLayer->SetName(TJS_W("AetherKiriCgViewMotionSurface"));
        renderLayer->SetVisible(true);
        renderLayer->SetEnabled(false);
        renderLayer->SetHitType(htMask);
        renderLayer->SetHitThreshold(256);
        renderLayer->SetPosition(0, 0);

        const tjs_int baseOrder = renderParentLayer->GetAbsoluteOrderMode()
            ? affineLayer->GetAbsoluteOrderIndex()
            : affineLayer->GetOrderIndex();
        const tjs_int targetOrder = baseOrder + 1;
        if(renderParentLayer->GetAbsoluteOrderMode()) {
            renderLayer->SetAbsoluteOrderIndex(targetOrder);
        } else {
            renderLayer->SetOrderIndex(targetOrder);
        }
    }

    void configureYuzuSdEventRenderChildLayer(
        tTJSNI_BaseLayer *renderLayer,
        tTJSNI_BaseLayer *eventLayer,
        int canvasWidth,
        int canvasHeight) {
        if(!renderLayer || !eventLayer || canvasWidth <= 0 ||
           canvasHeight <= 0) {
            return;
        }

        renderLayer->SetName(TJS_W("AetherKiriYuzuSdMotionSurface"));
        renderLayer->SetVisible(true);
        renderLayer->SetEnabled(false);
        renderLayer->SetHitType(htMask);
        renderLayer->SetHitThreshold(256);
        renderLayer->SetPosition(0, 0);
        renderLayer->SetSize(canvasWidth, canvasHeight);
        renderLayer->SetClip(0, 0, canvasWidth, canvasHeight);
        renderLayer->SetOrderIndex(
            std::max<tjs_int>(0, static_cast<tjs_int>(eventLayer->GetCount()) - 1));
    }

    iTJSDispatch2 *findYuzuSdEventRenderChildLayerObject(
        tTJSNI_BaseLayer *eventLayer) {
        if(!eventLayer) {
            return nullptr;
        }
        const auto count = eventLayer->GetCount();
        for(tjs_uint i = 0; i < count; ++i) {
            auto *child = eventLayer->GetChildren(static_cast<tjs_int>(i));
            if(child && child->GetOwnerNoAddRef() &&
               child->GetName().AsStdString() ==
                   "AetherKiriYuzuSdMotionSurface") {
                return child->GetOwnerNoAddRef();
            }
        }
        return nullptr;
    }

    iTJSDispatch2 *findCgViewRenderChildLayerObject(
        tTJSNI_BaseLayer *renderParentLayer) {
        if(!renderParentLayer) {
            return nullptr;
        }
        const auto count = renderParentLayer->GetCount();
        for(tjs_uint i = 0; i < count; ++i) {
            auto *child =
                renderParentLayer->GetChildren(static_cast<tjs_int>(i));
            if(child && child->GetOwnerNoAddRef() &&
               child->GetName().AsStdString() ==
                   "AetherKiriCgViewMotionSurface") {
                return child->GetOwnerNoAddRef();
            }
        }
        return nullptr;
    }

    void detachYuzuSdEventRenderSurfaceChildren(
        tTJSNI_BaseLayer *renderLayer) {
        if(!renderLayer) {
            return;
        }
        std::vector<tTJSNI_BaseLayer *> children;
        const auto count = renderLayer->GetCount();
        children.reserve(count);
        for(tjs_uint i = 0; i < count; ++i) {
            if(auto *child =
                   renderLayer->GetChildren(static_cast<tjs_int>(i))) {
                children.push_back(child);
            }
        }
        for(auto *child : children) {
            child->SetVisible(false);
            child->SetParent(nullptr);
        }
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
            name.find("fade") != std::string::npos ||
            name.find("fader") != std::string::npos ||
            name.find("load trigger") != std::string::npos ||
            name.find("trigger") != std::string::npos ||
            name.find("systembaselayer") != std::string::npos ||
            name.find("system base") != std::string::npos ||
            name.find("system") != std::string::npos ||
            name.find("sq_") != std::string::npos ||
            name.find("square") != std::string::npos ||
            name.find("logo") != std::string::npos ||
            name.find("date") != std::string::npos ||
            name.find("passive") != std::string::npos ||
            name.find("message") != std::string::npos ||
            name.find("title") != std::string::npos ||
            name.find("text") != std::string::npos ||
            name.find("trans") != std::string::npos ||
            name == "face" ||
            name == "msgwin" ||
            name == "character" ||
            name == "character2" ||
            name == "chara" ||
            name.find("msgwin") != std::string::npos ||
            name.find("character") != std::string::npos ||
            name.find("portrait") != std::string::npos ||
            name.find("クリック") != std::string::npos ||
            name.find("待ち") != std::string::npos ||
            name.find("メッセージ") != std::string::npos ||
            name.find("テキスト") != std::string::npos ||
            name.find("名前") != std::string::npos;
    }

    bool genericMotionLayerNameLooksLikeEventSurface(const std::string &name) {
        return name == "ev" ||
            name == "event" ||
            name == "cg" ||
            name.find("event") != std::string::npos ||
            name.find("cg") != std::string::npos;
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
        const auto name =
            renderDebugLowercase(motion::detail::narrow(child->GetName()));
        if(genericMotionLayerNameLooksLikeUiChrome(name)) {
            return -1;
        }
        if(child->GetCount() > 0 &&
           !genericMotionLayerNameLooksLikeEventSurface(name)) {
            return -1;
        }
        if(name == "stage" || name.find("stage") != std::string::npos) {
            return -1;
        }

        int score = 100;
        if(name == "ev") {
            score += 700;
        } else if(name.find("ev") != std::string::npos ||
                  name.find("event") != std::string::npos ||
                  name.find("cg") != std::string::npos) {
            score += 500;
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

    int scoreStartupLogoPresentationChild(tTJSNI_BaseLayer *parent,
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
            score += 900;
        } else if(name.find("ev") != std::string::npos ||
                  name.find("event") != std::string::npos ||
                  name.find("cg") != std::string::npos) {
            score += 700;
        } else if(name == "stage" || name.find("stage") != std::string::npos) {
            score += 600;
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

    tTJSNI_BaseLayer *findStartupLogoPresentationLayer(
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
                const int score = scoreStartupLogoPresentationChild(
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

    tTJSNI_BaseLayer *findCgViewMotionPresentationChild(
        tTJSNI_BaseLayer *parent,
        int canvasWidth,
        int canvasHeight) {
        if(!parent) {
            return nullptr;
        }

        struct Candidate {
            tTJSNI_BaseLayer *layer = nullptr;
            int score = -1;
        };
        Candidate best;
        auto considerImageBackedCgViewTarget =
            [&](tTJSNI_BaseLayer *candidate, int depth, int baseScore) {
                if(!candidate || !candidate->GetOwnerNoAddRef() ||
                   !layerIsSpecificCgViewContainer(candidate) ||
                   !motionLayerIsDrawableCgViewSurface(candidate) ||
                   !candidate->GetHasImage() ||
                   !motionLayerCoversCanvas(candidate, canvasWidth,
                                            canvasHeight)) {
                    return;
                }
                int score = baseScore;
                if(candidate->GetVisible() && candidate->GetOpacity() > 0) {
                    score += 100;
                }
                if(candidate->GetParentVisible()) {
                    score += 50;
                }
                score += static_cast<int>(candidate->GetOverallOrderIndex());
                score += static_cast<int>(candidate->GetOrderIndex()) * 2;
                score -= depth * 4;
                if(score > best.score) {
                    best.layer = candidate;
                    best.score = score;
                }
            };
        auto considerAffineCgViewTarget =
            [&](tTJSNI_BaseLayer *candidate, int depth, int baseScore) {
                if(!candidate || !candidate->GetOwnerNoAddRef() ||
                   !layerIsCgViewAffineSurface(candidate) ||
                   !motionLayerIsDrawableCgViewSurface(candidate)) {
                    return;
                }
                int score = baseScore;
                if(candidate->GetVisible() && candidate->GetOpacity() > 0) {
                    score += 100;
                }
                if(candidate->GetParentVisible()) {
                    score += 50;
                }
                if(auto *parentLayer = candidate->GetParent();
                   layerIsSpecificCgViewContainer(parentLayer)) {
                    score += 500;
                }
                score += static_cast<int>(candidate->GetOverallOrderIndex());
                score += static_cast<int>(candidate->GetOrderIndex()) * 2;
                score -= depth * 4;
                if(score > best.score) {
                    best.layer = candidate;
                    best.score = score;
                }
            };

        // Yuzu/KAG CG galleries put the half-transparent preview mask on the
        // "CG View Layer" parent and the actual SD image/motion on its
        // AffineLayer child. Keep the motion on that child so the script-owned
        // zoom, scroll and ordering stay authoritative.
        if(layerIsCgViewAffineSurface(parent)) {
            considerAffineCgViewTarget(parent, 0, 2400);
            considerImageBackedCgViewTarget(parent->GetParent(), 0, 900);
        }
        considerImageBackedCgViewTarget(parent, 0, 900);

        auto visit = [&](auto &&self,
                         tTJSNI_BaseLayer *current,
                         int depth) -> void {
            if(!current || current->GetCount() <= 0) {
                return;
            }
            const auto childCount = current->GetCount();
            for(tjs_uint i = 0; i < childCount; ++i) {
                auto *child = current->GetChildren(static_cast<tjs_int>(i));
                if(child && layerIsCgViewAffineSurface(child)) {
                    considerAffineCgViewTarget(child, depth + 1, 2400);
                    considerImageBackedCgViewTarget(current, depth + 1, 900);
                }
                considerImageBackedCgViewTarget(child, depth + 1, 900);
                if(child && layerIsCgViewAffineSurface(child) &&
                   layerIsCgViewContainer(current) &&
                   motionLayerIsDrawableCgViewSurface(child)) {
                    int score = 2400;
                    if(layerIsSpecificCgViewContainer(current)) {
                        score += 500;
                    }
                    if(current->GetVisible() && current->GetOpacity() > 0) {
                        score += 100;
                    }
                    if(current->GetParentVisible()) {
                        score += 50;
                    }
                    score += static_cast<int>(current->GetOverallOrderIndex());
                    score += static_cast<int>(child->GetOrderIndex()) * 2;
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

    bool motionPresentationLayerHasVisibleSamples(tTJSNI_BaseLayer *layer) {
        if(!layer ||
           !motion::internal::presentationLayerTypeCanReceivePixels(
               layer->GetType()) ||
           !layer->GetHasImage()) {
            return false;
        }
        auto *image = layer->GetMainImage();
        if(!image || image->GetWidth() <= 0 || image->GetHeight() <= 0) {
            return false;
        }

        const tjs_int width = static_cast<tjs_int>(image->GetWidth());
        const tjs_int height = static_cast<tjs_int>(image->GetHeight());
        for(tjs_int yStep = 0; yStep < 8; ++yStep) {
            const tjs_int y = std::min<tjs_int>(
                height - 1, std::max<tjs_int>(0, yStep * height / 7));
            for(tjs_int xStep = 0; xStep < 8; ++xStep) {
                const tjs_int x = std::min<tjs_int>(
                    width - 1, std::max<tjs_int>(0, xStep * width / 7));
                if((image->GetPoint(x, y) & 0xff000000u) != 0) {
                    return true;
                }
            }
        }
        return false;
    }

    bool shouldPreservePreviousGenericPresentationFrame(
        const motion::detail::PlayerRuntime &runtime,
        tTJSNI_BaseLayer *targetLayer,
        int canvasWidth,
        int canvasHeight) {
        if(canvasWidth <= 0 || canvasHeight <= 0 ||
           runtime.renderCommands.empty() ||
           !motionPresentationLayerHasVisibleSamples(targetLayer)) {
            return false;
        }

        int maxRight = 0;
        int maxBottom = 0;
        bool hasOffscreenSource = false;
        for(const auto &command : runtime.renderCommands) {
            if(command.clipRect[0] != 0 || command.clipRect[1] != 0) {
                return false;
            }
            maxRight = std::max(maxRight, command.clipRect[2]);
            maxBottom = std::max(maxBottom, command.clipRect[3]);
            for(size_t i = 0; i + 1 < command.worldCorners.size(); i += 2) {
                if(command.worldCorners[i] < -1.0f ||
                   command.worldCorners[i + 1] < -1.0f) {
                    hasOffscreenSource = true;
                    break;
                }
            }
        }

        if(!hasOffscreenSource) {
            return false;
        }

        return maxRight > 0 && maxBottom > 0 &&
            maxRight <= std::max(1, canvasWidth * 2 / 5) &&
            maxBottom <= std::max(1, canvasHeight * 2 / 5);
    }

    bool centeredGameMotionFrameNeedsPreviousComposite(
        const motion::detail::PlayerRuntime &runtime,
        const std::string &motionPath,
        tTJSNI_BaseLayer *targetLayer) {
        if(!isCenteredGameMotion(motionPath) ||
           runtime.renderCommands.empty() ||
           !motionPresentationLayerHasVisibleSamples(targetLayer)) {
            return false;
        }

        for(const auto &command : runtime.renderCommands) {
            if(command.opacity <= 0) {
                continue;
            }
            const auto label = renderDebugLowercase(command.nodeLabel);
            const auto source = renderDebugLowercase(command.sourceKey);
            if(label == "bg" || label == "background" ||
               label == "haikei" ||
               source.find("/bg") != std::string::npos ||
               source.find("/background") != std::string::npos ||
               source.find("/haikei") != std::string::npos ||
               source.find("/背景") != std::string::npos) {
                return false;
            }
        }

        return true;
    }

    void clearMotionPresentationLayer(tTJSNI_BaseLayer *layer,
                                      int canvasWidth,
                                      int canvasHeight) {
        if(!layer || canvasWidth <= 0 || canvasHeight <= 0) {
            return;
        }
        const tTVPRect clearRect(
            0, 0,
            std::max<tjs_int>(canvasWidth, layer->GetImageWidth()),
            std::max<tjs_int>(canvasHeight, layer->GetImageHeight()));
        if(auto *image = layer->GetMainImage()) {
            image->Fill(clearRect, 0x00000000);
        }
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
        // The motion canvas and the backing image are independent. A title can
        // animate in 1920x1080 coordinates while its layer keeps a 1920x1440
        // high-resolution image. Grow undersized targets, but never shrink an
        // existing high-resolution backing image to the motion canvas.
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

    struct YuzuTitlePresentationHoldEntry {
        tTJSVariant layer;
        std::shared_ptr<tTVPBaseBitmap> bitmap;
        std::string motion;
        std::string layerName;
        tjs_int width = 0;
        tjs_int height = 0;
        tjs_int opacity = 255;
        tjs_uint64 capturedTick = 0;
    };

    using YuzuTitlePresentationHoldCache =
        std::unordered_map<tTJSNI_BaseLayer *, YuzuTitlePresentationHoldEntry>;

    YuzuTitlePresentationHoldCache &yuzuTitlePresentationHoldCache() {
        static YuzuTitlePresentationHoldCache cache;
        return cache;
    }

    std::string layerNameLower(tTJSNI_BaseLayer *layer) {
        return layer ? renderDebugLowercase(
                           motion::detail::narrow(layer->GetName()))
                     : std::string{};
    }

    bool yuzuTitlePresentationBitmapHasMeaningfulContent(
        const tTVPBaseTexture *image) {
        if(!image || image->GetWidth() <= 0 || image->GetHeight() <= 0) {
            return false;
        }

        int opaqueSamples = 0;
        int nonWhiteSamples = 0;
        for(int yStep = 0; yStep < 12; ++yStep) {
            const int y = std::min<int>(
                image->GetHeight() - 1,
                yStep * static_cast<int>(image->GetHeight()) / 11);
            for(int xStep = 0; xStep < 16; ++xStep) {
                const int x = std::min<int>(
                    image->GetWidth() - 1,
                    xStep * static_cast<int>(image->GetWidth()) / 15);
                const auto pixel = image->GetPoint(x, y);
                const int alpha = static_cast<int>((pixel >> 24) & 0xffu);
                if(alpha == 0) {
                    continue;
                }
                ++opaqueSamples;
                const int red = static_cast<int>((pixel >> 16) & 0xffu);
                const int green = static_cast<int>((pixel >> 8) & 0xffu);
                const int blue = static_cast<int>(pixel & 0xffu);
                if(red < 245 || green < 245 || blue < 245) {
                    ++nonWhiteSamples;
                }
            }
        }
        return opaqueSamples > 0 &&
            nonWhiteSamples >= std::max(6, opaqueSamples / 20);
    }

    bool captureYuzuTitlePresentationHoldFrame(
        iTJSDispatch2 *layerObject,
        tTJSNI_BaseLayer *layer,
        const std::string &motionPath) {
        if(!layerObject || !layer || !isYuzuTitlePresentationMotion(motionPath) ||
           !motionPresentationLayerHasVisibleSamples(layer)) {
            return false;
        }
        auto *image = layer->GetMainImage();
        if(!image || image->GetWidth() <= 0 || image->GetHeight() <= 0 ||
           !yuzuTitlePresentationBitmapHasMeaningfulContent(image)) {
            return false;
        }

        const auto width = static_cast<tjs_int>(image->GetWidth());
        const auto height = static_cast<tjs_int>(image->GetHeight());
        auto &entry = yuzuTitlePresentationHoldCache()[layer];
        if(!entry.bitmap || entry.width != width || entry.height != height) {
            entry.bitmap = std::make_shared<tTVPBaseBitmap>(
                static_cast<tjs_uint>(width),
                static_cast<tjs_uint>(height), 32);
            entry.width = width;
            entry.height = height;
        }
        entry.layer = tTJSVariant(layerObject, layerObject);
        entry.motion = motionPath;
        entry.layerName = layerNameLower(layer);
        entry.opacity = layer->GetOpacity() > 0 ? layer->GetOpacity() : 255;
        entry.capturedTick = TVPGetTickCount();
        entry.bitmap->Fill(tTVPRect(0, 0, width, height), 0x00000000);
        entry.bitmap->CopyRect(0, 0, image, tTVPRect(0, 0, width, height));
        if(LOGGER && shouldDebugTitleRender(motionPath) &&
           markRenderDebugLogged("presentation-capture-hold:" + motionPath)) {
            LOGGER->info(
                "motion presentation captured held title frame: motion={} target=[{}] size={}x{}",
                motionPath, describeLayerForDebug(layer), width, height);
        }
        return true;
    }

    YuzuTitlePresentationHoldEntry *findYuzuTitlePresentationHoldFrame(
        tTJSNI_BaseLayer *layer,
        const std::string &motionPath) {
        if(!layer) {
            return nullptr;
        }

        auto &cache = yuzuTitlePresentationHoldCache();
        if(auto exact = cache.find(layer);
           exact != cache.end() && exact->second.bitmap &&
           exact->second.motion == motionPath) {
            return &exact->second;
        }

        const auto targetName = layerNameLower(layer);
        YuzuTitlePresentationHoldEntry *best = nullptr;
        for(auto &item : cache) {
            auto &candidate = item.second;
            if(candidate.motion != motionPath || !candidate.bitmap ||
               candidate.width <= 0 || candidate.height <= 0) {
                continue;
            }
            const bool sameName =
                !targetName.empty() && candidate.layerName == targetName;
            const bool compatibleSize =
                candidate.width >= layer->GetWidth() &&
                candidate.height >= layer->GetHeight();
            if(!sameName && !compatibleSize) {
                continue;
            }
            if(!best || candidate.capturedTick > best->capturedTick) {
                best = &candidate;
            }
        }
        return best;
    }

    bool restoreYuzuTitlePresentationHoldFrame(
        tTJSNI_BaseLayer *layer,
        const std::string &motionPath,
        int canvasWidth,
        int canvasHeight) {
        if(!layer || !isYuzuTitlePresentationMotion(motionPath)) {
            return false;
        }
        auto *entry = findYuzuTitlePresentationHoldFrame(layer, motionPath);
        if(!entry || !entry->bitmap || entry->width <= 0 ||
           entry->height <= 0) {
            return false;
        }

        const int restoreWidth = canvasWidth > 0
            ? canvasWidth
            : static_cast<int>(entry->width);
        const int restoreHeight = canvasHeight > 0
            ? canvasHeight
            : static_cast<int>(entry->height);
        if(!prepareMotionPresentationLayerForRender(
               layer, restoreWidth, restoreHeight)) {
            return false;
        }
        auto *image = layer->GetMainImage();
        if(!image) {
            return false;
        }

        const tTVPRect clearRect(
            0, 0,
            std::max<tjs_int>(restoreWidth, layer->GetImageWidth()),
            std::max<tjs_int>(restoreHeight, layer->GetImageHeight()));
        image->Fill(clearRect, 0x00000000);
        image->CopyRect(
            0, 0, entry->bitmap.get(),
            tTVPRect(0, 0, std::min(entry->width, restoreWidth),
                     std::min(entry->height, restoreHeight)));
        if(layer->GetOpacity() != entry->opacity) {
            layer->SetOpacity(entry->opacity);
        }
        layer->SetVisible(true);
        layer->SetHitThreshold(256);
        layer->Update(false);
        return true;
    }

    bool centeredGameMotionPresentationLayerShouldPassHit(
        tTJSNI_BaseLayer *layer) {
        if(!layer) {
            return false;
        }
        const auto name =
            renderDebugLowercase(motion::detail::narrow(layer->GetName()));
        return name == "ev" || name == "trans_ev" ||
            name == "sd" || name == "trans_sd";
    }

    void configureCenteredGameMotionPresentationHitPassthrough(
        tTJSNI_BaseLayer *layer) {
        if(!centeredGameMotionPresentationLayerShouldPassHit(layer)) {
            return;
        }
        layer->SetHitType(htMask);
        layer->SetHitThreshold(256);
    }

    bool centeredGameMotionStablePresentationLayer(tTJSNI_BaseLayer *layer) {
        if(!layer) {
            return false;
        }
        const auto name =
            renderDebugLowercase(motion::detail::narrow(layer->GetName()));
        return name == "ev" || name == "sd";
    }

    bool isYuzuSdPreviewMotionPath(const std::string &motionPath) {
        auto motion = renderDebugLowercase(motionPath);
        const auto slash = motion.find_last_of("/\\");
        const std::string name =
            slash == std::string::npos ? motion : motion.substr(slash + 1);
        return name.rfind("sd", 0) == 0 && name.size() > 2 &&
            std::isdigit(static_cast<unsigned char>(name[2])) &&
            (name.find(".psb") != std::string::npos ||
             name.find(".mtn") != std::string::npos);
    }

    bool centeredGameMotionHoldCaptureLayer(
        tTJSNI_BaseLayer *layer,
        const std::string &motionPath) {
        (void)motionPath;
        // The gallery owns two CG View buffers and swaps them around each
        // transition. Holding or copying those frames outside that hierarchy
        // leaves stale previews above the mask and races the next buffer.
        return centeredGameMotionStablePresentationLayer(layer);
    }

    enum class YuzuSdPresentationLayerFamily {
        None,
        Sd,
        Event,
    };

    YuzuSdPresentationLayerFamily yuzuSdPresentationLayerFamilyName(
        const std::string &name) {
        if(name == "sd" || name == "trans_sd") {
            return YuzuSdPresentationLayerFamily::Sd;
        }
        if(name == "ev" || name == "trans_ev") {
            return YuzuSdPresentationLayerFamily::Event;
        }
        return YuzuSdPresentationLayerFamily::None;
    }

    YuzuSdPresentationLayerFamily yuzuSdPresentationLayerFamily(
        tTJSNI_BaseLayer *layer) {
        if(!layer || !layer->GetOwnerNoAddRef()) {
            return YuzuSdPresentationLayerFamily::None;
        }
        const auto name =
            renderDebugLowercase(motion::detail::narrow(layer->GetName()));
        return yuzuSdPresentationLayerFamilyName(name);
    }

    bool copyPresentationFramePixelsPreservingLayerState(
        tTJSNI_BaseLayer *sourceLayer,
        tTJSNI_BaseLayer *targetLayer,
        int canvasWidth,
        int canvasHeight) {
        if(!sourceLayer || !targetLayer || sourceLayer == targetLayer ||
           !motion::internal::presentationLayerTypeCanReceivePixels(
               sourceLayer->GetType()) ||
           !motion::internal::presentationLayerTypeCanReceivePixels(
               targetLayer->GetType())) {
            return false;
        }

        try {
            if(!sourceLayer->GetHasImage()) {
                return false;
            }
            auto *sourceImage = sourceLayer->GetMainImage();
            if(!sourceImage || sourceImage->GetWidth() <= 0 ||
               sourceImage->GetHeight() <= 0) {
                return false;
            }

            const auto sourceWidth =
                static_cast<tjs_int>(sourceImage->GetWidth());
            const auto sourceHeight =
                static_cast<tjs_int>(sourceImage->GetHeight());
            const auto targetWidth = std::max<tjs_int>(
                std::max<tjs_int>(sourceWidth, canvasWidth),
                std::max<tjs_int>(targetLayer->GetImageWidth(), 0));
            const auto targetHeight = std::max<tjs_int>(
                std::max<tjs_int>(sourceHeight, canvasHeight),
                std::max<tjs_int>(targetLayer->GetImageHeight(), 0));
            if(targetWidth <= 0 || targetHeight <= 0) {
                return false;
            }

            if(!targetLayer->GetHasImage()) {
                targetLayer->SetHasImage(true);
            }
            if(targetLayer->GetImageWidth() < targetWidth ||
               targetLayer->GetImageHeight() < targetHeight) {
                targetLayer->SetImageSize(
                    static_cast<tjs_uint>(targetWidth),
                    static_cast<tjs_uint>(targetHeight));
            }
            if(targetLayer->GetImageLeft() != sourceLayer->GetImageLeft() ||
               targetLayer->GetImageTop() != sourceLayer->GetImageTop()) {
                targetLayer->SetImagePosition(sourceLayer->GetImageLeft(),
                                              sourceLayer->GetImageTop());
            }
            if(targetLayer->GetClipWidth() < canvasWidth ||
               targetLayer->GetClipHeight() < canvasHeight) {
                targetLayer->SetClip(0, 0, canvasWidth, canvasHeight);
            }

            auto *targetImage = targetLayer->GetMainImage();
            if(!targetImage) {
                return false;
            }
            targetImage->Fill(tTVPRect(0, 0, targetWidth, targetHeight),
                              0x00000000);
            targetImage->CopyRect(0, 0, sourceImage,
                                  tTVPRect(0, 0,
                                           sourceWidth, sourceHeight));
            targetLayer->Update(false);
            return true;
        } catch(...) {
            return false;
        }
    }

    int syncYuzuSdPreviewPresentationLayers(
        tTJSNI_BaseLayer *sourceLayer,
        const std::string &motionPath,
        int canvasWidth,
        int canvasHeight,
        const char *reason) {
        if(!sourceLayer || !isYuzuSdPreviewMotionPath(motionPath) ||
           canvasWidth <= 0 || canvasHeight <= 0 ||
           !motionPresentationLayerHasVisibleSamples(sourceLayer)) {
            return 0;
        }
        if(layerBelongsToCgViewPresentation(sourceLayer)) {
            return 0;
        }
        const auto sourceFamily =
            yuzuSdPresentationLayerFamily(sourceLayer);
        if(sourceFamily == YuzuSdPresentationLayerFamily::None) {
            return 0;
        }
        if(!centeredPresentationFrameHasResolvedScale(
               sourceLayer, motionPath, reason)) {
            return 0;
        }

        auto *root = sourceLayer;
        while(root && root->GetParent()) {
            root = root->GetParent();
        }
        if(!root) {
            return 0;
        }

        std::vector<tTJSNI_BaseLayer *> targets;
        std::unordered_set<tTJSNI_BaseLayer *> seen;
        auto visit = [&](auto &&self, tTJSNI_BaseLayer *layer) -> void {
            if(!layer) {
                return;
            }
            if(layer != sourceLayer &&
               motion::internal::presentationLayerTypeCanReceivePixels(
                   layer->GetType()) &&
               yuzuSdPresentationLayerFamily(layer) == sourceFamily &&
               seen.insert(layer).second) {
                targets.push_back(layer);
            }
            const auto childCount = layer->GetCount();
            for(tjs_uint i = 0; i < childCount; ++i) {
                self(self, layer->GetChildren(static_cast<tjs_int>(i)));
            }
        };
        visit(visit, root);

        int copied = 0;
        for(auto *target : targets) {
            if(copyPresentationFramePixelsPreservingLayerState(
                   sourceLayer, target, canvasWidth, canvasHeight)) {
                ++copied;
            }
        }

        if(copied > 0 && LOGGER) {
            const char *debug = std::getenv("AETHERKIRI_MOTION_DEBUG");
            if(debug && *debug && std::strcmp(debug, "0") != 0) {
                LOGGER->info(
                    "motion sd preview sync layers: reason={} motion={} source=[{}] targets={} copied={} frame=[{}]",
                    reason ? reason : "<null>", motionPath,
                    describeLayerForDebug(sourceLayer), targets.size(), copied,
                    sampleBitmapStats(sourceLayer->GetMainImage()));
            }
        }
        return copied;
    }

    struct CenteredPresentationHoldEntry {
        tTJSVariant layer;
        tTJSVariant parentLayer;
        tTJSVariant overlayParentLayer;
        tTJSVariant overlayLayer;
        std::shared_ptr<tTVPBaseBitmap> bitmap;
        std::string motion;
        std::string layerName;
        tjs_int width = 0;
        tjs_int height = 0;
        tjs_int left = 0;
        tjs_int top = 0;
        tjs_int primaryLeft = 0;
        tjs_int primaryTop = 0;
        tjs_int layerWidth = 0;
        tjs_int layerHeight = 0;
        tjs_int imageLeft = 0;
        tjs_int imageTop = 0;
        tjs_int clipLeft = 0;
        tjs_int clipTop = 0;
        tjs_int clipWidth = 0;
        tjs_int clipHeight = 0;
        tjs_int opacity = 255;
        tjs_int orderIndex = 0;
        tjs_int absoluteOrderIndex = 0;
        tjs_int overlayLeft = 0;
        tjs_int overlayTop = 0;
        tjs_int overlayOrderIndex = 0;
        tjs_int overlayAbsoluteOrderIndex = 0;
        tjs_int primaryOrderIndex = 0;
        tjs_int primaryAbsoluteOrderIndex = 0;
        bool parentAbsoluteOrderMode = false;
        bool overlayParentAbsoluteOrderMode = false;
        bool primaryAbsoluteOrderMode = false;
        bool hasGeometry = false;
        bool hasOverlayGeometry = false;
        bool hasPrimaryGeometry = false;
        bool cgViewPresentation = false;
        tTVPRect contentBounds;
        tTVPRect pendingExpandedBounds;
        bool hasContentBounds = false;
        int pendingExpandedBoundsFrames = 0;
        int overlayFramesRemaining = 0;
        tjs_uint64 capturedTick = 0;
        tjs_uint64 holdUntilTick = 0;
    };

    constexpr tjs_uint64 kCenteredPresentationHoldDurationMs = 5000;
    constexpr tjs_uint64 kCenteredPresentationHoldVisibleRefreshMs = 250;
    constexpr int kCenteredPresentationHoldOverlayFrames = 120;
    using CenteredPresentationHoldCache =
        std::unordered_map<tTJSNI_BaseLayer *, CenteredPresentationHoldEntry>;
    using CenteredPresentationMessageUiOverlayCache =
        std::unordered_map<tTJSNI_BaseLayer *, tTJSVariant>;

    CenteredPresentationHoldCache &centeredPresentationHoldCache() {
        static CenteredPresentationHoldCache cache;
        return cache;
    }

    CenteredPresentationMessageUiOverlayCache &
    centeredPresentationMessageUiOverlayCache() {
        static CenteredPresentationMessageUiOverlayCache cache;
        return cache;
    }

    bool centeredPresentationFrameHasResolvedScale(
        tTJSNI_BaseLayer *layer,
        const std::string &motionPath,
        const char *reason) {
        if(!layer || motionPath.empty()) {
            return true;
        }
        const auto family = yuzuSdPresentationLayerFamily(layer);
        if(family == YuzuSdPresentationLayerFamily::None) {
            return true;
        }
        const auto layerName =
            renderDebugLowercase(motion::detail::narrow(layer->GetName()));
        CenteredPresentationHoldEntry *previousStable = nullptr;
        for(auto &cached : centeredPresentationHoldCache()) {
            if(cached.first == layer) {
                continue;
            }
            auto &candidate = cached.second;
            if(candidate.motion != motionPath || !candidate.bitmap ||
               !candidate.hasContentBounds ||
               yuzuSdPresentationLayerFamilyName(candidate.layerName) !=
                   family) {
                continue;
            }
            if(!previousStable ||
               candidate.capturedTick > previousStable->capturedTick) {
                previousStable = &candidate;
            }
        }
        if(!previousStable) {
            return true;
        }

        // A stable layer is normally the only cached member of its family.
        // Scan alpha bounds only during a front/back replacement overlap so
        // looping SD animation frames keep their steady-state render cost.
        auto *image = layer->GetMainImage();
        tTVPRect currentBounds;
        if(!image || !bitmapVisibleBounds(image, currentBounds)) {
            return true;
        }

        const auto previousWidth = previousStable->contentBounds.get_width();
        const auto previousHeight = previousStable->contentBounds.get_height();
        const auto currentWidth = currentBounds.get_width();
        const auto currentHeight = currentBounds.get_height();
        if(previousWidth <= 0 || previousHeight <= 0 ||
           currentWidth <= previousWidth * 6 / 5 ||
           currentHeight <= previousHeight * 6 / 5) {
            return true;
        }

        previousStable->holdUntilTick = std::max(
            previousStable->holdUntilTick,
            TVPGetTickCount() + kCenteredPresentationHoldDurationMs);
        if(LOGGER && std::getenv("AETHERKIRI_MOTION_LAYER_DEBUG")) {
            LOGGER->info(
                "motion centered frame rejected unresolved scale: reason={} motion={} layer={} previous=[{},{},{},{}] current=[{},{},{},{}]",
                reason ? reason : "<null>", motionPath, layerName,
                previousStable->contentBounds.left,
                previousStable->contentBounds.top,
                previousStable->contentBounds.right,
                previousStable->contentBounds.bottom,
                currentBounds.left, currentBounds.top,
                currentBounds.right, currentBounds.bottom);
        }
        return false;
    }

    bool refreshCenteredPresentationHoldEntryFromVisibleLayer(
        CenteredPresentationHoldEntry &entry,
        tjs_uint64 now) {
        if(entry.layer.Type() != tvtObject) {
            return false;
        }
        auto *layer = resolveNativeLayer(entry.layer.AsObjectNoAddRef());
        if(!layer || !layer->GetVisible() || !layer->GetParentVisible() ||
           !centeredGameMotionHoldCaptureLayer(layer, entry.motion) ||
           !motionPresentationLayerHasVisibleSamples(layer)) {
            return false;
        }

        entry.holdUntilTick = std::max(
            entry.holdUntilTick,
            now + kCenteredPresentationHoldDurationMs);
        if(entry.bitmap && entry.capturedTick != 0 &&
           now < entry.capturedTick + kCenteredPresentationHoldVisibleRefreshMs) {
            return true;
        }

        auto *image = layer->GetMainImage();
        if(!image || image->GetWidth() <= 0 || image->GetHeight() <= 0) {
            return true;
        }

        tTVPRect currentBounds;
        const bool hasCurrentBounds =
            bitmapVisibleBounds(image, currentBounds);
        if(hasCurrentBounds && entry.hasContentBounds) {
            const auto oldWidth = entry.contentBounds.get_width();
            const auto oldHeight = entry.contentBounds.get_height();
            const auto currentWidth = currentBounds.get_width();
            const auto currentHeight = currentBounds.get_height();
            const bool abruptlyExpanded =
                oldWidth > 0 && oldHeight > 0 &&
                currentWidth > oldWidth * 6 / 5 &&
                currentHeight > oldHeight * 6 / 5;
            if(abruptlyExpanded) {
                const auto pendingWidth =
                    entry.pendingExpandedBounds.get_width();
                const auto pendingHeight =
                    entry.pendingExpandedBounds.get_height();
                const bool samePendingExpansion =
                    entry.pendingExpandedBoundsFrames > 0 &&
                    std::abs(currentWidth - pendingWidth) <=
                        std::max<tjs_int>(2, currentWidth / 50) &&
                    std::abs(currentHeight - pendingHeight) <=
                        std::max<tjs_int>(2, currentHeight / 50);
                if(!samePendingExpansion) {
                    entry.pendingExpandedBounds = currentBounds;
                    entry.pendingExpandedBoundsFrames = 1;
                    if(LOGGER &&
                       std::getenv("AETHERKIRI_MOTION_LAYER_DEBUG")) {
                        LOGGER->info(
                            "motion centered hold rejected transient scale jump: motion={} layer={} old=[{},{},{},{}] current=[{},{},{},{}]",
                            entry.motion, entry.layerName,
                            entry.contentBounds.left, entry.contentBounds.top,
                            entry.contentBounds.right,
                            entry.contentBounds.bottom,
                            currentBounds.left, currentBounds.top,
                            currentBounds.right, currentBounds.bottom);
                    }
                    return true;
                }
                ++entry.pendingExpandedBoundsFrames;
            } else {
                entry.pendingExpandedBoundsFrames = 0;
            }
        } else {
            entry.pendingExpandedBoundsFrames = 0;
        }

        const auto width = static_cast<tjs_int>(image->GetWidth());
        const auto height = static_cast<tjs_int>(image->GetHeight());
        if(!entry.bitmap || entry.width != width || entry.height != height) {
            entry.bitmap = std::make_shared<tTVPBaseBitmap>(
                static_cast<tjs_uint>(width),
                static_cast<tjs_uint>(height), 32);
            entry.width = width;
            entry.height = height;
        }
        entry.bitmap->Fill(tTVPRect(0, 0, width, height), 0x00000000);
        entry.bitmap->CopyRect(0, 0, image, tTVPRect(0, 0, width, height));
        if(hasCurrentBounds) {
            entry.contentBounds = currentBounds;
            entry.hasContentBounds = true;
        }
        entry.pendingExpandedBoundsFrames = 0;
        entry.capturedTick = now;
        return true;
    }

    CenteredPresentationHoldEntry *findCenteredPresentationHoldEntry(
        tTJSNI_BaseLayer *layer,
        const std::string &motionPath) {
        if(!layer) {
            return nullptr;
        }

        auto &cache = centeredPresentationHoldCache();
        const auto layerName =
            renderDebugLowercase(motion::detail::narrow(layer->GetName()));
        CenteredPresentationHoldEntry *best = nullptr;
        for(auto &entry : cache) {
            auto &candidate = entry.second;
            if(candidate.motion == motionPath &&
               candidate.layerName == layerName &&
               candidate.bitmap) {
                if(!best || candidate.capturedTick > best->capturedTick ||
                   (candidate.capturedTick == best->capturedTick &&
                    candidate.holdUntilTick > best->holdUntilTick)) {
                    best = &candidate;
                }
            }
        }
        return best;
    }

    CenteredPresentationHoldEntry *findLiveCenteredPresentationHoldEntry(
        tTJSNI_BaseLayer *layer) {
        if(!layer) {
            return nullptr;
        }

        const auto now = TVPGetTickCount();
        auto usable = [&](CenteredPresentationHoldEntry &entry) {
            if(entry.holdUntilTick < now) {
                refreshCenteredPresentationHoldEntryFromVisibleLayer(entry, now);
            }
            return entry.bitmap && entry.width > 0 && entry.height > 0 &&
                entry.holdUntilTick >= now;
        };

        auto &cache = centeredPresentationHoldCache();
        const auto layerName =
            renderDebugLowercase(motion::detail::narrow(layer->GetName()));
        CenteredPresentationHoldEntry *best = nullptr;
        for(auto &entry : cache) {
            auto &candidate = entry.second;
            if(candidate.layerName == layerName && usable(candidate)) {
                if(!best || candidate.capturedTick > best->capturedTick ||
                   (candidate.capturedTick == best->capturedTick &&
                    candidate.holdUntilTick > best->holdUntilTick)) {
                    best = &candidate;
                }
            }
        }
        return best;
    }

    bool hasCenteredPresentationHoldHistoryForLayer(
        tTJSNI_BaseLayer *layer) {
        if(!layer) {
            return false;
        }

        auto &cache = centeredPresentationHoldCache();
        if(auto exact = cache.find(layer);
           exact != cache.end() && exact->second.bitmap) {
            return true;
        }

        const auto layerName =
            renderDebugLowercase(motion::detail::narrow(layer->GetName()));
        for(const auto &entry : cache) {
            if(entry.second.layerName == layerName && entry.second.bitmap) {
                return true;
            }
        }
        return false;
    }

    tTJSNI_BaseLayer *findCenteredPresentationOverlayAnchor(
        tTJSNI_BaseLayer *layer) {
        auto *anchor = layer;
        while(anchor && anchor->GetParent()) {
            auto *parent = anchor->GetParent();
            if(parent->GetVisible() && parent->GetParentVisible()) {
                return anchor;
            }
            anchor = parent;
        }
        return layer;
    }

    tTJSNI_BaseLayer *resolveCenteredPresentationOrderLayer(
        tTJSNI_BaseLayer *layer) {
        auto *orderLayer = findCenteredPresentationOverlayAnchor(layer);
        if(!orderLayer) {
            orderLayer = layer;
        }
        if(layerIsCgViewAffineSurface(orderLayer)) {
            if(auto *parent = orderLayer->GetParent();
               layerIsCgViewContainer(parent)) {
                orderLayer = parent;
            }
        }
        return orderLayer;
    }

    bool centeredPresentationLayerNameLooksLikeMessageUi(
        const std::string &name) {
        return name.find("message") != std::string::npos ||
            name.find("msgwin") != std::string::npos ||
            name.find("msg") != std::string::npos ||
            name.find("text") != std::string::npos ||
            name.find("name") != std::string::npos ||
            name.find("メッセージ") != std::string::npos ||
            name.find("テキスト") != std::string::npos ||
            name.find("名前") != std::string::npos;
    }

    bool centeredPresentationLayerSubtreeLooksLikeMessageUi(
        tTJSNI_BaseLayer *layer,
        int depth = 0) {
        if(!layer || depth > 4) {
            return false;
        }
        if(!layer->GetVisible() || layer->GetOpacity() <= 0 ||
           !layer->GetParentVisible()) {
            return false;
        }
        const auto name =
            renderDebugLowercase(motion::detail::narrow(layer->GetName()));
        if(centeredPresentationLayerNameLooksLikeMessageUi(name)) {
            return true;
        }
        const auto childCount = layer->GetCount();
        for(tjs_uint i = 0; i < childCount; ++i) {
            if(centeredPresentationLayerSubtreeLooksLikeMessageUi(
                   layer->GetChildren(static_cast<tjs_int>(i)), depth + 1)) {
                return true;
            }
        }
        return false;
    }

    tTJSNI_BaseLayer *findVisibleCenteredPresentationSceneSibling(
        tTJSNI_BaseLayer *hiddenSceneLayer) {
        if(!hiddenSceneLayer || !hiddenSceneLayer->GetParent() ||
           hiddenSceneLayer->GetCount() == 0) {
            return nullptr;
        }

        auto *parent = hiddenSceneLayer->GetParent();
        const auto sceneWidth = std::max<tjs_int>(
            hiddenSceneLayer->GetWidth(), hiddenSceneLayer->GetImageWidth());
        const auto sceneHeight = std::max<tjs_int>(
            hiddenSceneLayer->GetHeight(), hiddenSceneLayer->GetImageHeight());
        if(sceneWidth <= 0 || sceneHeight <= 0) {
            return nullptr;
        }

        tTJSNI_BaseLayer *best = nullptr;
        int bestScore = std::numeric_limits<int>::min();
        const auto hiddenOrder = hiddenSceneLayer->GetOrderIndex();
        const auto childCount = parent->GetCount();
        for(tjs_uint i = 0; i < childCount; ++i) {
            auto *candidate = parent->GetChildren(static_cast<tjs_int>(i));
            if(!candidate || candidate == hiddenSceneLayer ||
               !candidate->GetOwnerNoAddRef() || !candidate->GetVisible() ||
               !candidate->GetParentVisible() ||
               candidate->GetCount() == 0 ||
               candidate->GetType() != hiddenSceneLayer->GetType()) {
                continue;
            }
            if(hiddenSceneLayer->GetHasImage() && !candidate->GetHasImage()) {
                continue;
            }

            const auto candidateWidth = std::max<tjs_int>(
                candidate->GetWidth(), candidate->GetImageWidth());
            const auto candidateHeight = std::max<tjs_int>(
                candidate->GetHeight(), candidate->GetImageHeight());
            const auto widthTolerance = std::max<tjs_int>(64, sceneWidth / 10);
            const auto heightTolerance = std::max<tjs_int>(64, sceneHeight / 10);
            if(std::abs(candidateWidth - sceneWidth) > widthTolerance ||
               std::abs(candidateHeight - sceneHeight) > heightTolerance) {
                continue;
            }
            // Front/back scene pages are structural peers.  Custom Yuzu
            // message skins do not necessarily put "message" or
            // "メッセージ" in their layer names, so requiring a name match
            // here makes a hidden back page fall through to the top layer.
            // A full-screen hold parented there can then cover the real
            // message window whenever the pages exchange.  Type, geometry,
            // visibility, and the shared parent already identify the peer;
            // retain the message-name heuristic only as a preference.
            const bool hasMessageUi =
                centeredPresentationLayerSubtreeLooksLikeMessageUi(candidate);
            int score = (hasMessageUi ? 200 : 100) - std::min<int>(
                50, static_cast<int>(std::llabs(
                        static_cast<long long>(candidate->GetOrderIndex()) -
                        static_cast<long long>(hiddenOrder))));
            if(score > bestScore) {
                bestScore = score;
                best = candidate;
            }
        }
        return best;
    }

    bool centeredPresentationLayerShouldStayBelowMessageUi(
        tTJSNI_BaseLayer *layer) {
        if(!layer) {
            return false;
        }
        const auto name =
            renderDebugLowercase(motion::detail::narrow(layer->GetName()));
        return centeredGameMotionPresentationLayerShouldPassHit(layer) ||
            name == "aetherkiricenteredpresentationhold";
    }

    void placeCenteredPresentationBelowMessageUi(tTJSNI_BaseLayer *layer,
                                                 const char *reason) {
        if(!layer) {
            return;
        }

        if(!centeredPresentationLayerShouldStayBelowMessageUi(layer)) {
            return;
        }

        auto *orderLayer = resolveCenteredPresentationOrderLayer(layer);
        auto *parent = orderLayer->GetParent();
        if(!parent) {
            return;
        }

        const bool absoluteOrderMode = parent->GetAbsoluteOrderMode();
        tTJSNI_BaseLayer *messageLayer = nullptr;
        tjs_int messageOrder = std::numeric_limits<tjs_int>::max();
        const auto childCount = parent->GetCount();
        for(tjs_uint i = 0; i < childCount; ++i) {
            auto *child = parent->GetChildren(static_cast<tjs_int>(i));
            if(!child || child == orderLayer || !child->GetOwnerNoAddRef()) {
                continue;
            }
            if(!centeredPresentationLayerSubtreeLooksLikeMessageUi(child)) {
                continue;
            }
            const tjs_int childOrder = absoluteOrderMode
                ? child->GetAbsoluteOrderIndex()
                : child->GetOrderIndex();
            if(childOrder < messageOrder) {
                messageOrder = childOrder;
                messageLayer = child;
            }
        }
        if(!messageLayer || messageOrder <= 0) {
            return;
        }

        const tjs_int currentOrder = absoluteOrderMode
            ? orderLayer->GetAbsoluteOrderIndex()
            : orderLayer->GetOrderIndex();
        if(currentOrder < messageOrder) {
            return;
        }

        const tjs_int targetOrder = std::max<tjs_int>(0, messageOrder - 1);
        if(absoluteOrderMode) {
            orderLayer->SetAbsoluteOrderIndex(targetOrder);
        } else {
            orderLayer->SetOrderIndex(targetOrder);
        }

        if(LOGGER && std::getenv("AETHERKIRI_MOTION_LAYER_DEBUG")) {
            LOGGER->info(
                "motion centered order guard: reason={} layer=[{}] orderLayer=[{}] message=[{}] orderMode={} from={} to={}",
                reason ? reason : "<null>",
                describeLayerForDebug(layer),
                describeLayerForDebug(orderLayer),
                describeLayerForDebug(messageLayer),
                absoluteOrderMode ? "absolute" : "relative",
                currentOrder, targetOrder);
        }
    }

    CenteredPresentationHoldEntry *
    captureCenteredPresentationHoldLayerSamples(
        tTJSNI_BaseLayer *layer,
        const std::string &motionPath) {
        if(!centeredGameMotionHoldCaptureLayer(layer, motionPath) ||
           !motionPresentationLayerHasVisibleSamples(layer)) {
            return nullptr;
        }
        auto *image = layer->GetMainImage();
        if(!image || image->GetWidth() <= 0 || image->GetHeight() <= 0) {
            return nullptr;
        }

        const tjs_int width = static_cast<tjs_int>(image->GetWidth());
        const tjs_int height = static_cast<tjs_int>(image->GetHeight());
        auto &cache = centeredPresentationHoldCache();
        const auto layerName =
            renderDebugLowercase(motion::detail::narrow(layer->GetName()));
        tTVPRect currentBounds;
        const bool hasCurrentBounds =
            bitmapVisibleBounds(image, currentBounds);
        // During a Yuzu front/back page exchange, the replacement `ev` layer
        // is briefly populated at its native size before MotionPlayer applies
        // the motion's logical resolution.  Keep the prior stable transition
        // frame until the replacement has reached its resolved geometry.
        if(!centeredPresentationFrameHasResolvedScale(
               layer, motionPath, "capture")) {
            return nullptr;
        }

        auto &entry = cache[layer];
        if(!entry.bitmap || entry.width != width || entry.height != height) {
            entry.bitmap = std::make_shared<tTVPBaseBitmap>(
                static_cast<tjs_uint>(width),
                static_cast<tjs_uint>(height), 32);
            entry.width = width;
            entry.height = height;
        }

        entry.layerName = layerName;
        entry.cgViewPresentation = layerBelongsToCgViewPresentation(layer);
        entry.left = layer->GetLeft();
        entry.top = layer->GetTop();
        entry.primaryLeft = 0;
        entry.primaryTop = 0;
        layer->ToPrimaryCoordinates(entry.primaryLeft, entry.primaryTop);
        entry.layerWidth = layer->GetWidth();
        entry.layerHeight = layer->GetHeight();
        entry.imageLeft = layer->GetImageLeft();
        entry.imageTop = layer->GetImageTop();
        entry.clipLeft = layer->GetClipLeft();
        entry.clipTop = layer->GetClipTop();
        entry.clipWidth = layer->GetClipWidth();
        entry.clipHeight = layer->GetClipHeight();
        entry.opacity = layer->GetOpacity() > 0 ? layer->GetOpacity() : 255;
        entry.orderIndex = layer->GetOrderIndex();
        entry.absoluteOrderIndex = layer->GetAbsoluteOrderIndex();
        if(auto *parent = layer->GetParent()) {
            entry.parentAbsoluteOrderMode = parent->GetAbsoluteOrderMode();
            if(auto *parentObject = parent->GetOwnerNoAddRef()) {
                entry.parentLayer =
                    tTJSVariant(parentObject, parentObject);
            }
        }
        entry.overlayParentLayer.Clear();
        entry.overlayLeft = entry.left;
        entry.overlayTop = entry.top;
        entry.overlayOrderIndex = entry.orderIndex;
        entry.overlayAbsoluteOrderIndex = entry.absoluteOrderIndex;
        entry.overlayParentAbsoluteOrderMode = entry.parentAbsoluteOrderMode;
        entry.hasOverlayGeometry = false;
        if(auto *anchor = findCenteredPresentationOverlayAnchor(layer)) {
            if(auto *overlayParent = anchor->GetParent()) {
                if(auto *overlayParentObject = overlayParent->GetOwnerNoAddRef()) {
                    entry.overlayParentLayer =
                        tTJSVariant(overlayParentObject, overlayParentObject);
                    tjs_int parentPrimaryLeft = 0;
                    tjs_int parentPrimaryTop = 0;
                    overlayParent->ToPrimaryCoordinates(parentPrimaryLeft,
                                                        parentPrimaryTop);
                    entry.overlayLeft = entry.primaryLeft - parentPrimaryLeft;
                    entry.overlayTop = entry.primaryTop - parentPrimaryTop;
                    entry.overlayOrderIndex = anchor->GetOrderIndex();
                    entry.overlayAbsoluteOrderIndex =
                        anchor->GetAbsoluteOrderIndex();
                    entry.overlayParentAbsoluteOrderMode =
                        overlayParent->GetAbsoluteOrderMode();
                    entry.hasOverlayGeometry = true;
                }
            }
        }
        entry.hasGeometry = entry.layerWidth > 0 && entry.layerHeight > 0;
        entry.hasPrimaryGeometry = entry.hasGeometry;
        if(auto *primaryChild = layer) {
            while(primaryChild->GetParent() &&
                  primaryChild->GetParent()->GetParent() &&
                  !primaryChild->GetParent()->IsPrimary()) {
                primaryChild = primaryChild->GetParent();
            }
            if(primaryChild->GetParent() &&
               primaryChild->GetParent()->IsPrimary()) {
                entry.primaryAbsoluteOrderMode =
                    primaryChild->GetParent()->GetAbsoluteOrderMode();
                entry.primaryOrderIndex = primaryChild->GetOrderIndex();
                entry.primaryAbsoluteOrderIndex =
                    primaryChild->GetAbsoluteOrderIndex();
            } else {
                entry.primaryAbsoluteOrderMode = false;
                entry.primaryOrderIndex = layer->GetOrderIndex();
                entry.primaryAbsoluteOrderIndex = layer->GetAbsoluteOrderIndex();
            }
        }
        if(!motionPath.empty()) {
            entry.motion = motionPath;
        } else if(entry.motion.empty()) {
            for(const auto &cached : cache) {
                if(cached.second.layerName == entry.layerName &&
                   !cached.second.motion.empty()) {
                    entry.motion = cached.second.motion;
                    break;
                }
            }
        }
        const auto now = TVPGetTickCount();
        entry.capturedTick = now;
        entry.holdUntilTick = std::max(
            entry.holdUntilTick,
            now + kCenteredPresentationHoldDurationMs);
        entry.bitmap->Fill(tTVPRect(0, 0, width, height), 0x00000000);
        entry.bitmap->CopyRect(0, 0, image, tTVPRect(0, 0, width, height));
        entry.hasContentBounds = hasCurrentBounds;
        if(hasCurrentBounds) {
            entry.contentBounds = currentBounds;
        }
        entry.pendingExpandedBoundsFrames = 0;
        return &entry;
    }

    bool copyCenteredPresentationHoldEntryToLayer(
        tTJSNI_BaseLayer *layer,
        CenteredPresentationHoldEntry &entry,
        int canvasWidth,
        int canvasHeight,
        bool usePrimaryPlacement = false,
        bool useOverlayPlacement = false,
        bool preserveTargetGeometry = false) {
        const bool trace =
            std::getenv("AETHERKIRI_MOTION_LAYER_DEBUG") != nullptr;
        auto fail = [&](const char *reason) {
            if(trace && LOGGER) {
                LOGGER->info(
                    "motion centered copy hold layer failed layer=[{}] reason={} entry={}x{} layerRect=[{},{} {}x{}] overlayRect=[{},{}] imagePos=[{},{}] clip=[{},{},{}x{}] usePrimary={} useOverlay={} preserveTarget={}",
                    describeLayerForDebug(layer),
                    reason ? reason : "<null>",
                    entry.width,
                    entry.height,
                    entry.left,
                    entry.top,
                    entry.layerWidth,
                    entry.layerHeight,
                    entry.overlayLeft,
                    entry.overlayTop,
                    entry.imageLeft,
                    entry.imageTop,
                    entry.clipLeft,
                    entry.clipTop,
                    entry.clipWidth,
                    entry.clipHeight,
                    usePrimaryPlacement ? 1 : 0,
                    useOverlayPlacement ? 1 : 0,
                    preserveTargetGeometry ? 1 : 0);
            }
            return false;
        };
        if(!layer) {
            return fail("no-layer");
        }
        if(!entry.bitmap || entry.width <= 0 || entry.height <= 0) {
            return fail("no-bitmap");
        }

        try {
            const int restoreWidth =
                std::max<int>(canvasWidth, static_cast<int>(entry.width));
            const int restoreHeight =
                std::max<int>(canvasHeight, static_cast<int>(entry.height));
            if(!prepareMotionPresentationLayerForRender(layer, restoreWidth,
                                                        restoreHeight)) {
                return fail("prepare-failed");
            }

            const tjs_int targetClipLeft = layer->GetClipLeft();
            const tjs_int targetClipTop = layer->GetClipTop();
            const tjs_int targetClipWidth = layer->GetClipWidth();
            const tjs_int targetClipHeight = layer->GetClipHeight();

            const bool hasPlacement = !preserveTargetGeometry && (useOverlayPlacement
                ? entry.hasOverlayGeometry
                : (usePrimaryPlacement ? entry.hasPrimaryGeometry
                                       : entry.hasGeometry));
            const tjs_int targetLeft = useOverlayPlacement
                ? entry.overlayLeft
                : (usePrimaryPlacement ? entry.primaryLeft : entry.left);
            const tjs_int targetTop = useOverlayPlacement
                ? entry.overlayTop
                : (usePrimaryPlacement ? entry.primaryTop : entry.top);
            if(hasPlacement) {
                if(layer->GetLeft() != targetLeft ||
                   layer->GetTop() != targetTop) {
                    layer->SetPosition(targetLeft, targetTop);
                }
                if(layer->GetWidth() != entry.layerWidth ||
                   layer->GetHeight() != entry.layerHeight) {
                    layer->SetSize(static_cast<tjs_uint>(entry.layerWidth),
                                   static_cast<tjs_uint>(entry.layerHeight));
                }
            }

            if(!preserveTargetGeometry &&
               (layer->GetImageLeft() != entry.imageLeft ||
                layer->GetImageTop() != entry.imageTop)) {
                layer->SetImagePosition(entry.imageLeft, entry.imageTop);
            }

            auto *mainImage = layer->GetMainImage();
            if(!layer->GetHasImage() || !mainImage) {
                return fail("no-main-image");
            }

            const tTVPRect sourceRect(0, 0, entry.width, entry.height);
            const tTVPRect clearRect(0, 0, restoreWidth, restoreHeight);
            layer->SetClip(0, 0, restoreWidth, restoreHeight);
            mainImage->Fill(clearRect, 0x00000000);
            mainImage->CopyRect(0, 0, entry.bitmap.get(), sourceRect);
            if(preserveTargetGeometry) {
                layer->SetClip(targetClipLeft, targetClipTop,
                               targetClipWidth, targetClipHeight);
            } else if(entry.hasGeometry && entry.clipWidth > 0 &&
                      entry.clipHeight > 0) {
                layer->SetClip(entry.clipLeft, entry.clipTop,
                               entry.clipWidth, entry.clipHeight);
            }
            if(layer->GetOpacity() != entry.opacity) {
                layer->SetOpacity(entry.opacity);
            }
            layer->SetVisible(true);
            configureCenteredGameMotionPresentationHitPassthrough(layer);
            placeCenteredPresentationBelowMessageUi(layer, "hold-copy");
            showCenteredPresentationMessageUiOverlay(
                layer, restoreWidth, restoreHeight, "hold-copy");
            const auto now = TVPGetTickCount();
            entry.holdUntilTick = std::max(
                entry.holdUntilTick,
                now + kCenteredPresentationHoldDurationMs);
            layer->Update(false);
        } catch(const eTJS &e) {
            if(trace && LOGGER) {
                LOGGER->info(
                    "motion centered copy hold layer exception layer=[{}] message={}",
                    describeLayerForDebug(layer),
                    e.GetMessage().AsStdString());
            }
            return false;
        } catch(...) {
            if(trace && LOGGER) {
                LOGGER->info(
                    "motion centered copy hold layer exception layer=[{}] message=<unknown>",
                    describeLayerForDebug(layer));
            }
            return false;
        }
        return true;
    }

    void hideCenteredPresentationHoldOverlay(
        CenteredPresentationHoldEntry &entry) {
        entry.overlayFramesRemaining = 0;
        if(entry.overlayLayer.Type() != tvtObject) {
            return;
        }
        if(auto *overlay =
               resolveNativeLayer(entry.overlayLayer.AsObjectNoAddRef())) {
            if(overlay->GetVisible()) {
                overlay->SetVisible(false);
            }
        }
    }

    bool copyCenteredPresentationHoldEntryToOverlay(
        CenteredPresentationHoldEntry &entry,
        tTJSNI_BaseLayer *referenceLayer) {
        const bool trace =
            std::getenv("AETHERKIRI_MOTION_LAYER_DEBUG") != nullptr;
        auto fail = [&](const char *reason) {
            if(trace && LOGGER) {
                LOGGER->info(
                    "motion centered copy hold overlay failed reason={} ref=[{}] refAncestry={} entryLayer={} entry={}x{} overlay=[{},{} order={} absOrder={} absMode={}] primary=[{},{} order={} absOrder={} absMode={}]",
                    reason ? reason : "<null>",
                    describeLayerForDebug(referenceLayer),
                    describeLayerAncestryForDebug(referenceLayer),
                    entry.layerName,
                    entry.width,
                    entry.height,
                    entry.overlayLeft,
                    entry.overlayTop,
                    entry.overlayOrderIndex,
                    entry.overlayAbsoluteOrderIndex,
                    entry.overlayParentAbsoluteOrderMode ? 1 : 0,
                    entry.primaryLeft,
                    entry.primaryTop,
                    entry.primaryOrderIndex,
                    entry.primaryAbsoluteOrderIndex,
                    entry.primaryAbsoluteOrderMode ? 1 : 0);
            }
            return false;
        };
        try {
            if(!entry.bitmap || entry.width <= 0 || entry.height <= 0 ||
               (entry.overlayParentLayer.Type() != tvtObject &&
                entry.parentLayer.Type() != tvtObject)) {
                return fail("missing-entry");
            }

            auto *owner = resolveMainWindowOwnerObject();
            auto *parentObject = entry.overlayParentLayer.Type() == tvtObject
                ? entry.overlayParentLayer.AsObjectNoAddRef()
                : nullptr;
            bool useSavedOverlayParent = false;
            if(parentObject) {
                auto *savedParentLayer = resolveNativeLayer(parentObject);
                if(savedParentLayer && savedParentLayer->GetVisible() &&
                   savedParentLayer->GetParentVisible()) {
                    useSavedOverlayParent = true;

                    // Capturing an event CG while its front/back scene page is
                    // hidden records the still-visible top layer as the overlay
                    // parent.  Merely checking that saved parent for visibility
                    // is insufficient: restoring the opaque hold there places it
                    // above the visible page and its message window.  Refine that
                    // cached top-level parent to the currently visible structural
                    // peer before reusing it.
                    auto *hiddenSceneAnchor =
                        findCenteredPresentationOverlayAnchor(referenceLayer);
                    auto *visibleSceneSibling =
                        findVisibleCenteredPresentationSceneSibling(
                            hiddenSceneAnchor);
                    if(hiddenSceneAnchor && visibleSceneSibling &&
                       hiddenSceneAnchor->GetParent() == savedParentLayer &&
                       visibleSceneSibling->GetOwnerNoAddRef()) {
                        parentObject =
                            visibleSceneSibling->GetOwnerNoAddRef();
                        savedParentLayer = visibleSceneSibling;
                        entry.overlayParentLayer =
                            tTJSVariant(parentObject, parentObject);
                        tjs_int parentPrimaryLeft = 0;
                        tjs_int parentPrimaryTop = 0;
                        savedParentLayer->ToPrimaryCoordinates(
                            parentPrimaryLeft, parentPrimaryTop);
                        entry.overlayLeft =
                            entry.primaryLeft - parentPrimaryLeft;
                        entry.overlayTop =
                            entry.primaryTop - parentPrimaryTop;
                        entry.overlayParentAbsoluteOrderMode =
                            savedParentLayer->GetAbsoluteOrderMode();
                        entry.hasOverlayGeometry = true;
                    }
                } else {
                    parentObject = nullptr;
                }
            }
            if(!parentObject) {
                auto *sourceParentObject = entry.parentLayer.Type() == tvtObject
                    ? entry.parentLayer.AsObjectNoAddRef()
                    : nullptr;
                owner = resolveLayerTreeOwnerObject(sourceParentObject);
                if(!owner && referenceLayer) {
                    owner = resolveLayerTreeOwnerObject(
                        referenceLayer->GetOwnerNoAddRef());
                }
                if(!owner) {
                    owner = resolveMainWindowOwnerObject();
                }
                if(!owner) {
                    return fail("no-owner");
                }

                // A page transition can hide the saved background container
                // before its replacement SD layer becomes visible. Keep the
                // hold surface inside the nearest visible scene container;
                // parenting it to the primary layer would put it above the
                // message window for the duration of the transition.
                auto *fallbackAnchor =
                    findCenteredPresentationOverlayAnchor(referenceLayer);
                auto *visibleSceneSibling =
                    findVisibleCenteredPresentationSceneSibling(fallbackAnchor);
                auto *fallbackParent = visibleSceneSibling
                    ? visibleSceneSibling
                    : (fallbackAnchor ? fallbackAnchor->GetParent() : nullptr);
                if(fallbackAnchor && fallbackParent &&
                   fallbackParent->GetVisible() &&
                   fallbackParent->GetParentVisible() &&
                   fallbackParent->GetOwnerNoAddRef()) {
                    parentObject = fallbackParent->GetOwnerNoAddRef();
                    useSavedOverlayParent = true;
                    entry.overlayParentLayer =
                        tTJSVariant(parentObject, parentObject);
                    tjs_int parentPrimaryLeft = 0;
                    tjs_int parentPrimaryTop = 0;
                    fallbackParent->ToPrimaryCoordinates(parentPrimaryLeft,
                                                         parentPrimaryTop);
                    entry.overlayLeft = entry.primaryLeft - parentPrimaryLeft;
                    entry.overlayTop = entry.primaryTop - parentPrimaryTop;
                    if(!visibleSceneSibling) {
                        entry.overlayOrderIndex =
                            fallbackAnchor->GetOrderIndex();
                        entry.overlayAbsoluteOrderIndex =
                            fallbackAnchor->GetAbsoluteOrderIndex();
                    }
                    entry.overlayParentAbsoluteOrderMode =
                        fallbackParent->GetAbsoluteOrderMode();
                    entry.hasOverlayGeometry = true;
                }

                if(!parentObject) {
                    parentObject =
                        resolvePrimaryAncestorLayerObject(referenceLayer);
                }
                if(!parentObject) {
                    parentObject = resolvePrimaryLayerObject(owner);
                }
            } else if(!owner) {
                owner = resolveLayerTreeOwnerObject(parentObject);
                if(!owner) {
                    owner = resolveMainWindowOwnerObject();
                }
                if(!owner) {
                    return fail("no-owner");
                }
            }
            auto *parentLayer = resolveNativeLayer(parentObject);
            if(!parentObject || !parentLayer || !parentLayer->GetVisible() ||
               !parentLayer->GetParentVisible()) {
                return fail("no-visible-overlay-parent");
            }

            auto *overlayObject = ensureReusableLayerObject(
                entry.overlayLayer,
                owner,
                parentObject,
                static_cast<tTVPLayerType>(ltAlpha),
                true,
                useSavedOverlayParent ? entry.overlayParentAbsoluteOrderMode
                                      : entry.primaryAbsoluteOrderMode);
            auto *overlayLayer = resolveNativeLayer(overlayObject);
            if(!overlayObject || !overlayLayer) {
                return fail("ensure-overlay-failed");
            }

            overlayLayer->SetName(TJS_W("AetherKiriCenteredPresentationHold"));
            overlayLayer->SetHitType(htMask);
            overlayLayer->SetHitThreshold(256);
            const bool useForegroundOverlayOrder =
                entry.cgViewPresentation ||
                layerBelongsToCgViewPresentation(referenceLayer);
            if(useForegroundOverlayOrder && parentLayer->GetAbsoluteOrderMode()) {
                tjs_int maxAbsoluteOrder =
                    std::numeric_limits<tjs_int>::min();
                const auto childCount = parentLayer->GetCount();
                for(tjs_uint i = 0; i < childCount; ++i) {
                    auto *child = parentLayer->GetChildren(
                        static_cast<tjs_int>(i));
                    if(!child || !child->GetOwnerNoAddRef()) {
                        continue;
                    }
                    maxAbsoluteOrder = std::max(
                        maxAbsoluteOrder, child->GetAbsoluteOrderIndex());
                }
                overlayLayer->SetAbsoluteOrderIndex(
                    maxAbsoluteOrder == std::numeric_limits<tjs_int>::min()
                        ? 0
                        : maxAbsoluteOrder + 1);
            } else if(useForegroundOverlayOrder) {
                overlayLayer->SetOrderIndex(
                    std::max<tjs_int>(0, parentLayer->GetCount() - 1));
            } else if(useSavedOverlayParent &&
                      entry.overlayParentAbsoluteOrderMode) {
                overlayLayer->SetAbsoluteOrderIndex(
                    entry.overlayAbsoluteOrderIndex);
            } else if(!useSavedOverlayParent && entry.primaryAbsoluteOrderMode) {
                overlayLayer->SetAbsoluteOrderIndex(
                    entry.primaryAbsoluteOrderIndex);
            } else {
                const auto maxIndex =
                    std::max<tjs_int>(0, parentLayer->GetCount() - 1);
                const auto targetOrder = useSavedOverlayParent
                    ? entry.overlayOrderIndex
                    : entry.primaryOrderIndex;
                overlayLayer->SetOrderIndex(
                    std::min<tjs_int>(targetOrder, maxIndex));
            }

            if(!copyCenteredPresentationHoldEntryToLayer(
                   overlayLayer, entry, entry.width, entry.height,
                   !useSavedOverlayParent, useSavedOverlayParent)) {
                return fail("copy-to-overlay-layer-failed");
            }
            entry.overlayFramesRemaining =
                std::max(entry.overlayFramesRemaining,
                         kCenteredPresentationHoldOverlayFrames);
            overlayLayer->SetVisible(true);
            placeCenteredPresentationBelowMessageUi(overlayLayer,
                                                    "hold-overlay");
            overlayLayer->Update(false);
            if(trace && LOGGER) {
                LOGGER->info(
                    "motion centered copy hold overlay shown ref=[{}] parent=[{}] overlay=[{}] savedParent={}",
                    describeLayerForDebug(referenceLayer),
                    describeLayerForDebug(parentLayer),
                    describeLayerForDebug(overlayLayer),
                    useSavedOverlayParent ? 1 : 0);

            }
            return true;
        } catch(const eTJS &e) {
            if(trace && LOGGER) {
                LOGGER->info(
                    "motion centered copy hold overlay exception ref=[{}] message={}",
                    describeLayerForDebug(referenceLayer),
                    e.GetMessage().AsStdString());
            }
            return false;
        } catch(...) {
            if(trace && LOGGER) {
                LOGGER->info(
                    "motion centered copy hold overlay exception ref=[{}] message=<unknown>",
                    describeLayerForDebug(referenceLayer));
            }
            return false;
        }
    }

    bool centeredPresentationHoldOverlayIsActive(
        const CenteredPresentationHoldEntry &entry) {
        if(entry.overlayLayer.Type() != tvtObject) {
            return false;
        }
        auto *overlay =
            resolveNativeLayer(entry.overlayLayer.AsObjectNoAddRef());
        return overlay && overlay->GetVisible();
    }

    iTJSDispatch2 *findCenteredPresentationHoldRenderTarget(
        const std::string &motionPath,
        tTJSNI_BaseLayer *referenceLayer) {
        (void)referenceLayer;
        CenteredPresentationHoldEntry *best = nullptr;
        bool hasBestScore = false;
        long long bestScore = 0;

        auto usableCachedEntry = [&](CenteredPresentationHoldEntry &entry) {
            if(entry.motion != motionPath || !entry.bitmap ||
               entry.width <= 0 || entry.height <= 0) {
                return false;
            }
            return true;
        };
        auto visibleTargetScore = [](tTJSNI_BaseLayer *layer,
                                     bool usesOverlay) {
            long long score = 0;
            if(layer) {
                score += static_cast<long long>(layer->GetOverallOrderIndex()) *
                    10000LL;
                score += static_cast<long long>(layer->GetOrderIndex()) *
                    100LL;
                score += static_cast<long long>(layer->GetAbsoluteOrderIndex());
                const auto name = renderDebugLowercase(
                    motion::detail::narrow(layer->GetName()));
                if(name == "sd") {
                    score += 1000000LL;
                }
                if(name.find("trans") != std::string::npos) {
                    score -= 1000000LL;
                }
            }
            if(usesOverlay) {
                score += 5000LL;
            }
            return score;
        };

        auto consider = [&](CenteredPresentationHoldEntry &entry,
                            bool usesOverlay) {
            if(!usableCachedEntry(entry)) {
                return;
            }
            auto &slot = usesOverlay ? entry.overlayLayer : entry.layer;
            if(slot.Type() != tvtObject) {
                return;
            }
            auto *layer = resolveNativeLayer(slot.AsObjectNoAddRef());
            if(!layer || !layer->GetVisible() ||
               !layer->GetParentVisible() ||
               !motionPresentationLayerHasVisibleSamples(layer)) {
                return;
            }
            const auto name = renderDebugLowercase(
                motion::detail::narrow(layer->GetName()));
            if(name.find("trans") != std::string::npos) {
                return;
            }
            const auto score = visibleTargetScore(layer, usesOverlay);
            if(!best || !hasBestScore || score > bestScore ||
               (score == bestScore && entry.capturedTick > best->capturedTick)) {
                best = &entry;
                bestScore = score;
                hasBestScore = true;
            }
        };

        for(auto &item : centeredPresentationHoldCache()) {
            consider(item.second, false);
        }

        if(!best) {
            return nullptr;
        }
        auto &slot = best->layer;
        return slot.Type() == tvtObject ? slot.AsObjectNoAddRef() : nullptr;
    }

    bool layerLooksLikeReplacementPresentation(
        tTJSNI_BaseLayer *layer) {
        if(!layer || !layer->GetVisible() || layer->GetOpacity() <= 0 ||
           !layer->GetParentVisible() || !layer->GetHasImage() ||
           !motionPresentationLayerHasVisibleSamples(layer)) {
            return false;
        }

        const auto name =
            renderDebugLowercase(motion::detail::narrow(layer->GetName()));
        if(name == "trans_sd" || name == "trans_ev") {
            return false;
        }
        if(name == "sd" || name == "ev") {
            return true;
        }

        auto *image = layer->GetMainImage();
        if(!image || image->GetWidth() <= 0 || image->GetHeight() <= 0) {
            return false;
        }

        tjs_int primaryWidth = 0;
        tjs_int primaryHeight = 0;
        if(auto *parent = layer; parent) {
            while(parent->GetParent()) {
                parent = parent->GetParent();
            }
            primaryWidth = parent->GetWidth();
            primaryHeight = parent->GetHeight();
            if(primaryWidth <= 0 || primaryHeight <= 0) {
                primaryWidth = parent->GetImageWidth();
                primaryHeight = parent->GetImageHeight();
            }
        }

        const tjs_int layerWidth =
            std::max<tjs_int>(layer->GetWidth(), layer->GetImageWidth());
        const tjs_int layerHeight =
            std::max<tjs_int>(layer->GetHeight(), layer->GetImageHeight());
        if(primaryWidth > 0 && primaryHeight > 0) {
            return layerWidth >= primaryWidth * 3 / 4 &&
                layerHeight >= primaryHeight * 3 / 4;
        }

        return layerWidth >= 640 && layerHeight >= 360;
    }

    void captureCenteredPresentationHoldFrame(
        iTJSDispatch2 *layerObject,
        tTJSNI_BaseLayer *layer,
        const std::string &motionPath);

    bool bitmapVisibleBounds(const iTVPBaseBitmap *bitmap,
                             tTVPRect &bounds) {
        if(!bitmap || bitmap->GetWidth() <= 0 || bitmap->GetHeight() <= 0) {
            return false;
        }

        const auto width = static_cast<tjs_int>(bitmap->GetWidth());
        const auto height = static_cast<tjs_int>(bitmap->GetHeight());
        tjs_int left = width;
        tjs_int top = height;
        tjs_int right = 0;
        tjs_int bottom = 0;
        for(tjs_int y = 0; y < height; ++y) {
            const auto *row = static_cast<const std::uint8_t *>(
                bitmap->GetScanLine(static_cast<tjs_uint>(y)));
            if(!row) {
                continue;
            }
            for(tjs_int x = 0; x < width; ++x) {
                const auto *pixel = row + static_cast<size_t>(x) * 4u;
                if(pixel[3] == 0) {
                    continue;
                }
                left = std::min(left, x);
                top = std::min(top, y);
                right = std::max(right, x + 1);
                bottom = std::max(bottom, y + 1);
            }
        }
        if(left >= right || top >= bottom) {
            return false;
        }
        bounds = tTVPRect(left, top, right, bottom);
        return true;
    }

    bool centeredPresentationMessageLayerIsWorthCompositing(
        tTJSNI_BaseLayer *layer,
        int canvasHeight) {
        if(!layer || !layer->GetVisible() || layer->GetOpacity() <= 0 ||
           !layer->GetParentVisible() || !layer->GetHasImage() ||
           !layer->GetMainImage()) {
            return false;
        }

        const auto name =
            renderDebugLowercase(motion::detail::narrow(layer->GetName()));
        if(name == "aetherkiricenteredpresentationmessageui") {
            return false;
        }
        if(!centeredPresentationLayerNameLooksLikeMessageUi(name) ||
           centeredPresentationLayerShouldStayBelowMessageUi(layer)) {
            return false;
        }

        tTVPRect bounds;
        if(!bitmapVisibleBounds(layer->GetMainImage(), bounds)) {
            return false;
        }

        tjs_int primaryLeft = 0;
        tjs_int primaryTop = 0;
        layer->ToPrimaryCoordinates(primaryLeft, primaryTop);
        const tjs_int layerBottom =
            primaryTop + layer->GetImageTop() + bounds.bottom;
        const tjs_int messageBandTop =
            canvasHeight > 0 ? canvasHeight / 2 : 0;
        return layerBottom >= messageBandTop;
    }

    int compositeCenteredPresentationMessageUiLayer(
        tTJSNI_BaseLayer *targetLayer,
        tTJSNI_BaseLayer *sourceLayer,
        int canvasHeight) {
        if(!targetLayer || !sourceLayer || targetLayer == sourceLayer ||
           !centeredPresentationMessageLayerIsWorthCompositing(sourceLayer,
                                                               canvasHeight)) {
            return 0;
        }

        auto *targetImage = targetLayer->GetMainImage();
        auto *sourceImage = sourceLayer->GetMainImage();
        if(!targetImage || !sourceImage || targetImage->GetWidth() <= 0 ||
           targetImage->GetHeight() <= 0 || sourceImage->GetWidth() <= 0 ||
           sourceImage->GetHeight() <= 0) {
            return 0;
        }

        tTVPRect sourceRect;
        if(!bitmapVisibleBounds(sourceImage, sourceRect)) {
            return 0;
        }

        tjs_int targetPrimaryLeft = 0;
        tjs_int targetPrimaryTop = 0;
        tjs_int sourcePrimaryLeft = 0;
        tjs_int sourcePrimaryTop = 0;
        targetLayer->ToPrimaryCoordinates(targetPrimaryLeft, targetPrimaryTop);
        sourceLayer->ToPrimaryCoordinates(sourcePrimaryLeft, sourcePrimaryTop);

        tjs_int dstX =
            sourcePrimaryLeft + sourceLayer->GetImageLeft() + sourceRect.left -
            targetPrimaryLeft - targetLayer->GetImageLeft();
        tjs_int dstY =
            sourcePrimaryTop + sourceLayer->GetImageTop() + sourceRect.top -
            targetPrimaryTop - targetLayer->GetImageTop();

        const tjs_int targetWidth =
            static_cast<tjs_int>(targetImage->GetWidth());
        const tjs_int targetHeight =
            static_cast<tjs_int>(targetImage->GetHeight());
        if(dstX < 0) {
            sourceRect.left -= dstX;
            dstX = 0;
        }
        if(dstY < 0) {
            sourceRect.top -= dstY;
            dstY = 0;
        }
        if(dstX + sourceRect.get_width() > targetWidth) {
            sourceRect.right -= dstX + sourceRect.get_width() - targetWidth;
        }
        if(dstY + sourceRect.get_height() > targetHeight) {
            sourceRect.bottom -= dstY + sourceRect.get_height() - targetHeight;
        }
        if(sourceRect.left >= sourceRect.right ||
           sourceRect.top >= sourceRect.bottom) {
            return 0;
        }

        try {
            targetImage->Blt(dstX, dstY, sourceImage, sourceRect,
                             bmAlphaOnAlpha,
                             std::clamp<tjs_int>(sourceLayer->GetOpacity(),
                                                 0, 255),
                             true);
        } catch(...) {
            return 0;
        }

        return 1;
    }

    int compositeMessageUiOverCenteredPresentation(
        tTJSNI_BaseLayer *targetLayer,
        int canvasWidth,
        int canvasHeight,
        const char *reason) {
        (void)canvasWidth;
        if(!centeredPresentationLayerShouldStayBelowMessageUi(targetLayer) ||
           !targetLayer->GetMainImage()) {
            return 0;
        }

        auto *root = targetLayer;
        while(root && root->GetParent()) {
            root = root->GetParent();
        }
        if(!root) {
            return 0;
        }

        int composited = 0;
        auto visit = [&](auto &&self, tTJSNI_BaseLayer *layer) -> void {
            if(!layer) {
                return;
            }
            composited += compositeCenteredPresentationMessageUiLayer(
                targetLayer, layer, canvasHeight);
            const auto childCount = layer->GetCount();
            for(tjs_uint i = 0; i < childCount; ++i) {
                self(self, layer->GetChildren(static_cast<tjs_int>(i)));
            }
        };
        visit(visit, root);

        if(composited > 0 && LOGGER &&
           std::getenv("AETHERKIRI_MOTION_LAYER_DEBUG")) {
            LOGGER->info(
                "motion centered message ui composite: reason={} target=[{}] layers={}",
                reason ? reason : "<null>",
                describeLayerForDebug(targetLayer),
                composited);
        }
        return composited;
    }

    void hideCenteredPresentationMessageUiOverlay(tTJSNI_BaseLayer *targetLayer) {
        if(!targetLayer) {
            return;
        }
        auto &cache = centeredPresentationMessageUiOverlayCache();
        auto it = cache.find(targetLayer);
        if(it == cache.end() || it->second.Type() != tvtObject) {
            return;
        }
        if(auto *overlay =
               resolveNativeLayer(it->second.AsObjectNoAddRef())) {
            overlay->SetVisible(false);
            overlay->Update(false);
        }
    }

    tjs_int findSiblingMessageUiAbsoluteOrder(tTJSNI_BaseLayer *parent,
                                              tTJSNI_BaseLayer *skipLayer) {
        if(!parent) {
            return std::numeric_limits<tjs_int>::min();
        }

        tjs_int best = std::numeric_limits<tjs_int>::min();
        const auto childCount = parent->GetCount();
        for(tjs_uint i = 0; i < childCount; ++i) {
            auto *child = parent->GetChildren(static_cast<tjs_int>(i));
            if(!child || child == skipLayer || !child->GetOwnerNoAddRef()) {
                continue;
            }
            const auto name =
                renderDebugLowercase(motion::detail::narrow(child->GetName()));
            if(name == "aetherkiricenteredpresentationmessageui") {
                continue;
            }
            if(!centeredPresentationLayerSubtreeLooksLikeMessageUi(child)) {
                continue;
            }
            best = std::max<tjs_int>(best, child->GetAbsoluteOrderIndex());
        }
        return best;
    }

    bool showCenteredPresentationMessageUiOverlay(
        tTJSNI_BaseLayer *presentationLayer,
        int canvasWidth,
        int canvasHeight,
        const char *reason) {
        if(!presentationLayer) {
            return false;
        }
        if(layerBelongsToCgViewPresentation(presentationLayer)) {
            hideCenteredPresentationMessageUiOverlay(presentationLayer);
            return false;
        }
        const auto presentationName = renderDebugLowercase(
            motion::detail::narrow(presentationLayer->GetName()));
        if(presentationName == "sd") {
            // Story SD layers are ordered below the real message UI. Copying
            // that UI into another layer also captures in-game thumbnails.
            hideCenteredPresentationMessageUiOverlay(presentationLayer);
            return false;
        }
        if(!centeredPresentationLayerShouldStayBelowMessageUi(presentationLayer) ||
           !presentationLayer->GetMainImage()) {
            return false;
        }

        auto *anchor = findCenteredPresentationOverlayAnchor(presentationLayer);
        if(!anchor) {
            anchor = presentationLayer;
        }
        auto *parentLayer = anchor->GetParent();
        auto *parentObject = parentLayer ? parentLayer->GetOwnerNoAddRef() : nullptr;
        if(!parentLayer || !parentObject || !parentLayer->GetVisible() ||
           !parentLayer->GetParentVisible()) {
            hideCenteredPresentationMessageUiOverlay(presentationLayer);
            return false;
        }

        auto *owner = resolveMainWindowOwnerObject();
        if(!owner) {
            owner = resolveLayerTreeOwnerObject(parentObject);
        }
        if(!owner) {
            hideCenteredPresentationMessageUiOverlay(presentationLayer);
            return false;
        }

        auto &slot = centeredPresentationMessageUiOverlayCache()[presentationLayer];
        auto *overlayObject = ensureReusableLayerObject(
            slot,
            owner,
            parentObject,
            static_cast<tTVPLayerType>(ltAlpha),
            true,
            parentLayer->GetAbsoluteOrderMode());
        auto *overlayLayer = resolveNativeLayer(overlayObject);
        if(!overlayObject || !overlayLayer) {
            return false;
        }

        overlayLayer->SetName(TJS_W("AetherKiriCenteredPresentationMessageUi"));
        overlayLayer->SetHitType(htMask);
        overlayLayer->SetHitThreshold(256);

        tjs_int parentPrimaryLeft = 0;
        tjs_int parentPrimaryTop = 0;
        tjs_int anchorPrimaryLeft = 0;
        tjs_int anchorPrimaryTop = 0;
        parentLayer->ToPrimaryCoordinates(parentPrimaryLeft, parentPrimaryTop);
        anchor->ToPrimaryCoordinates(anchorPrimaryLeft, anchorPrimaryTop);
        overlayLayer->SetPosition(anchorPrimaryLeft - parentPrimaryLeft,
                                  anchorPrimaryTop - parentPrimaryTop);

        const int overlayWidth = std::max<int>(
            canvasWidth,
            std::max<tjs_int>(presentationLayer->GetImageWidth(),
                              presentationLayer->GetWidth()));
        const int overlayHeight = std::max<int>(
            canvasHeight,
            std::max<tjs_int>(presentationLayer->GetImageHeight(),
                              presentationLayer->GetHeight()));
        if(!prepareMotionPresentationLayerForRender(overlayLayer,
                                                    overlayWidth,
                                                    overlayHeight)) {
            return false;
        }
        auto *overlayImage = overlayLayer->GetMainImage();
        if(!overlayImage) {
            overlayLayer->SetVisible(false);
            return false;
        }
        overlayImage->Fill(tTVPRect(0, 0, overlayWidth, overlayHeight),
                           0x00000000);

        const tjs_int messageOrder =
            findSiblingMessageUiAbsoluteOrder(parentLayer, overlayLayer);
        const tjs_int targetOrder = std::max<tjs_int>(
            anchor->GetAbsoluteOrderIndex() + 1,
            messageOrder == std::numeric_limits<tjs_int>::min()
                ? anchor->GetAbsoluteOrderIndex() + 1
                : messageOrder + 1);
        if(parentLayer->GetAbsoluteOrderMode()) {
            overlayLayer->SetAbsoluteOrderIndex(targetOrder);
        } else {
            overlayLayer->SetOrderIndex(targetOrder);
        }

        const int composited = compositeMessageUiOverCenteredPresentation(
            overlayLayer, overlayWidth, overlayHeight, reason);
        if(composited <= 0) {
            overlayLayer->SetVisible(false);
            overlayLayer->Update(false);
            return false;
        }

        overlayLayer->SetVisible(true);
        overlayLayer->Update(false);
        return true;
    }

    void detachGeneratedCenteredPresentationLayer(
        tTJSVariant &slot,
        const char *expectedName) {
        if(slot.Type() != tvtObject) {
            slot.Clear();
            return;
        }

        try {
            if(auto *layer = resolveNativeLayer(slot.AsObjectNoAddRef());
               layer &&
               (!expectedName ||
                layer->GetName().AsStdString() == expectedName)) {
                layer->SetVisible(false);
                layer->Update(false);
                layer->SetParent(nullptr);
            }
        } catch(...) {
        }
        slot.Clear();
    }

    void releaseCenteredPresentationHoldEntry(
        tTJSNI_BaseLayer *cacheKey,
        CenteredPresentationHoldEntry &entry) {
        detachGeneratedCenteredPresentationLayer(
            entry.overlayLayer, "AetherKiriCenteredPresentationHold");

        auto &messageCache = centeredPresentationMessageUiOverlayCache();
        if(auto message = messageCache.find(cacheKey);
           message != messageCache.end()) {
            detachGeneratedCenteredPresentationLayer(
                message->second,
                "AetherKiriCenteredPresentationMessageUi");
            messageCache.erase(message);
        }

        entry.layer.Clear();
        entry.parentLayer.Clear();
        entry.overlayParentLayer.Clear();
        entry.bitmap.reset();
        entry.overlayFramesRemaining = 0;
        entry.holdUntilTick = 0;
    }

    tTVPRect centeredMotionFrameDestinationForSize(
        tjs_int frameWidth,
        tjs_int frameHeight,
        int canvasWidth,
        int canvasHeight) {
        if(frameWidth <= 0 || frameHeight <= 0) {
            return tTVPRect(0, 0, 0, 0);
        }

        const auto targetCanvasWidth =
            std::max<tjs_int>(1, canvasWidth > 0 ? canvasWidth : frameWidth);
        const auto targetCanvasHeight =
            std::max<tjs_int>(1, canvasHeight > 0 ? canvasHeight : frameHeight);
        double scale = 1.0;
        if(frameWidth > targetCanvasWidth || frameHeight > targetCanvasHeight) {
            scale = std::min(
                targetCanvasWidth / static_cast<double>(frameWidth),
                targetCanvasHeight / static_cast<double>(frameHeight));
        }
        const auto destWidth = std::max<tjs_int>(
            1, static_cast<tjs_int>(frameWidth * scale + 0.5));
        const auto destHeight = std::max<tjs_int>(
            1, static_cast<tjs_int>(frameHeight * scale + 0.5));
        const auto left = (targetCanvasWidth - destWidth) / 2;
        const auto top = (targetCanvasHeight - destHeight) / 2;
        return tTVPRect(left, top, left + destWidth, top + destHeight);
    }

    tTVPRect centeredMotionScaledFrameDestinationForSize(
        tjs_int frameWidth,
        tjs_int frameHeight,
        int canvasWidth,
        int canvasHeight,
        double contentScale) {
        if(frameWidth <= 0 || frameHeight <= 0) {
            return tTVPRect(0, 0, 0, 0);
        }

        const auto targetCanvasWidth =
            std::max<tjs_int>(1, canvasWidth > 0 ? canvasWidth : frameWidth);
        const auto targetCanvasHeight =
            std::max<tjs_int>(1, canvasHeight > 0 ? canvasHeight : frameHeight);
        double scale = std::isfinite(contentScale) && contentScale > 0.0
            ? contentScale
            : 1.0;
        if(frameWidth * scale > targetCanvasWidth ||
           frameHeight * scale > targetCanvasHeight) {
            scale = std::min(
                scale,
                std::min(targetCanvasWidth / static_cast<double>(frameWidth),
                         targetCanvasHeight / static_cast<double>(frameHeight)));
        }
        const auto destWidth = std::max<tjs_int>(
            1, static_cast<tjs_int>(frameWidth * scale + 0.5));
        const auto destHeight = std::max<tjs_int>(
            1, static_cast<tjs_int>(frameHeight * scale + 0.5));
        const auto left = (targetCanvasWidth - destWidth) / 2;
        const auto top = (targetCanvasHeight - destHeight) / 2;
        return tTVPRect(left, top, left + destWidth, top + destHeight);
    }

    tTVPRect centeredMotionFrameDestination(
        const iTVPBaseBitmap *bitmap,
        int canvasWidth,
        int canvasHeight) {
        if(!bitmap || bitmap->GetWidth() <= 0 || bitmap->GetHeight() <= 0) {
            return tTVPRect(0, 0, 0, 0);
        }
        return centeredMotionFrameDestinationForSize(
            static_cast<tjs_int>(bitmap->GetWidth()),
            static_cast<tjs_int>(bitmap->GetHeight()),
            canvasWidth, canvasHeight);
    }

    bool findCenteredPresentationMessageUiTop(
        tTJSNI_BaseLayer *presentationLayer,
        int canvasHeight,
        tjs_int &messageTop) {
        if(!presentationLayer || canvasHeight <= 0) {
            return false;
        }

        auto *root = presentationLayer;
        while(root && root->GetParent()) {
            root = root->GetParent();
        }
        if(!root) {
            return false;
        }

        bool found = false;
        messageTop = static_cast<tjs_int>(canvasHeight);
        auto visit = [&](auto &&self, tTJSNI_BaseLayer *layer) -> void {
            if(!layer) {
                return;
            }
            const auto name =
                renderDebugLowercase(motion::detail::narrow(layer->GetName()));
            if(layer != presentationLayer &&
               name != "aetherkiricenteredpresentationmessageui" &&
               !centeredPresentationLayerShouldStayBelowMessageUi(layer) &&
               centeredPresentationLayerNameLooksLikeMessageUi(name) &&
               layer->GetParentVisible() && layer->GetOpacity() > 0 &&
               layer->GetHasImage() && layer->GetMainImage()) {
                tTVPRect bounds;
                if(bitmapVisibleBounds(layer->GetMainImage(), bounds)) {
                    tjs_int primaryLeft = 0;
                    tjs_int primaryTop = 0;
                    layer->ToPrimaryCoordinates(primaryLeft, primaryTop);
                    const tjs_int layerTop =
                        primaryTop + layer->GetImageTop() + bounds.top;
                    const tjs_int layerBottom =
                        primaryTop + layer->GetImageTop() + bounds.bottom;
                    if(layerBottom >= canvasHeight / 2 &&
                       layerTop < canvasHeight) {
                        messageTop = std::min<tjs_int>(
                            messageTop, std::max<tjs_int>(0, layerTop));
                        found = true;
                    }
                }
            }

            const auto childCount = layer->GetCount();
            for(tjs_uint i = 0; i < childCount; ++i) {
                self(self, layer->GetChildren(static_cast<tjs_int>(i)));
            }
        };
        visit(visit, root);
        return found;
    }

    bool constrainCenteredMotionDestinationAboveMessageUi(
        tTVPRect &destination,
        tjs_int frameWidth,
        tjs_int frameHeight,
        tTJSNI_BaseLayer *presentationLayer,
        int canvasWidth,
        int canvasHeight) {
        if(frameWidth <= 0 || frameHeight <= 0 ||
           destination.get_width() <= 0 || destination.get_height() <= 0 ||
           canvasWidth <= 0 || canvasHeight <= 0) {
            return false;
        }

        const double currentScale = std::min(
            destination.get_width() / static_cast<double>(frameWidth),
            destination.get_height() / static_cast<double>(frameHeight));
        if(currentScale <= 0.0) {
            return false;
        }

        tjs_int padding = 0;
        tjs_int safeBottom = 0;
        double safeScale = currentScale;
        if(!computeCenteredMotionMessageSafeScale(
               presentationLayer, frameWidth, frameHeight,
               canvasWidth, canvasHeight, currentScale, safeScale,
               &padding, &safeBottom)) {
            return false;
        }

        const double centerY = canvasHeight / 2.0;
        tjs_int destWidth = std::max<tjs_int>(
            1, static_cast<tjs_int>(frameWidth * safeScale + 0.5));
        tjs_int destHeight = std::max<tjs_int>(
            1, static_cast<tjs_int>(frameHeight * safeScale + 0.5));
        tjs_int left = (static_cast<tjs_int>(canvasWidth) - destWidth) / 2;
        tjs_int top =
            static_cast<tjs_int>(centerY - destHeight / 2.0 + 0.5);

        if(top < padding || top + destHeight > safeBottom) {
            const double fallbackScale = std::min(
                currentScale,
                std::min(canvasWidth / static_cast<double>(frameWidth),
                         std::max<tjs_int>(1, safeBottom - padding) /
                             static_cast<double>(frameHeight)));
            if(fallbackScale <= 0.0) {
                return false;
            }
            destWidth = std::max<tjs_int>(
                1, static_cast<tjs_int>(frameWidth * fallbackScale + 0.5));
            destHeight = std::max<tjs_int>(
                1, static_cast<tjs_int>(frameHeight * fallbackScale + 0.5));
            left = (static_cast<tjs_int>(canvasWidth) - destWidth) / 2;
            top = std::max<tjs_int>(
                padding, (safeBottom - destHeight) / 2);
        }

        left = std::max<tjs_int>(0, left);
        if(left + destWidth > canvasWidth) {
            left = std::max<tjs_int>(0, canvasWidth - destWidth);
        }
        top = std::max<tjs_int>(padding, top);
        if(top + destHeight > safeBottom) {
            top = std::max<tjs_int>(padding, safeBottom - destHeight);
        }

        const tTVPRect adjusted(
            left, top, left + destWidth, top + destHeight);
        if(adjusted.left == destination.left &&
           adjusted.top == destination.top &&
           adjusted.right == destination.right &&
           adjusted.bottom == destination.bottom) {
            return false;
        }
        destination = adjusted;
        return true;
    }

    bool centeredMotionDestinationLooksSaneForSize(
        const tTVPRect &destination,
        tjs_int frameWidth,
        tjs_int frameHeight,
        int canvasWidth,
        int canvasHeight) {
        if(frameWidth <= 0 || frameHeight <= 0 ||
           canvasWidth <= 0 || canvasHeight <= 0 ||
           destination.get_width() <= 0 ||
           destination.get_height() <= 0 ||
           destination.right <= 0 || destination.bottom <= 0 ||
           destination.left >= canvasWidth ||
           destination.top >= canvasHeight) {
            return false;
        }

        const auto centered =
            centeredMotionFrameDestinationForSize(
                frameWidth, frameHeight, canvasWidth, canvasHeight);
        const auto expectedWidth = centered.get_width();
        const auto expectedHeight = centered.get_height();
        if(expectedWidth <= 0 || expectedHeight <= 0) {
            return false;
        }

        const auto width = destination.get_width();
        const auto height = destination.get_height();
        if(width < expectedWidth * 3 / 4 ||
           height < expectedHeight * 3 / 4) {
            return false;
        }

        const auto aspectDelta = std::abs(
            static_cast<long long>(width) * frameHeight -
            static_cast<long long>(height) * frameWidth);
        const auto aspectBase =
            static_cast<long long>(width) * frameHeight;
        if(aspectBase > 0 && aspectDelta * 4 > aspectBase) {
            return false;
        }

        const auto destinationCenterX = (destination.left + destination.right) / 2;
        const auto destinationCenterY = (destination.top + destination.bottom) / 2;
        const auto canvasCenterX = canvasWidth / 2;
        const auto canvasCenterY = canvasHeight / 2;
        if(std::abs(destinationCenterX - canvasCenterX) > canvasWidth / 5 ||
           std::abs(destinationCenterY - canvasCenterY) > canvasHeight / 6) {
            return false;
        }

        return true;
    }

    bool centeredMotionDestinationLooksSane(
        const tTVPRect &destination,
        const iTVPBaseBitmap *frameBitmap,
        int canvasWidth,
        int canvasHeight) {
        if(!frameBitmap) {
            return false;
        }
        return centeredMotionDestinationLooksSaneForSize(
            destination,
            static_cast<tjs_int>(frameBitmap->GetWidth()),
            static_cast<tjs_int>(frameBitmap->GetHeight()),
            canvasWidth, canvasHeight);
    }

    bool resolveCenteredMotionClipFrameImage(
        const std::string &motionPath,
        const std::string &clipLabel,
        ttstr &imagePath) {
        if(!isCenteredGameMotion(motionPath) || clipLabel.empty() ||
           clipLabel.find('/') != std::string::npos ||
           clipLabel.find('\\') != std::string::npos) {
            return false;
        }

        const auto clipBase = motion::detail::widen(clipLabel);
        const auto motionStorage = motion::detail::widen(motionPath);
        const auto baseDir = TVPExtractStoragePath(motionStorage);
        std::vector<ttstr> candidates;
        if(!baseDir.IsEmpty()) {
            candidates.emplace_back(baseDir + clipBase + TJS_W(".png"));
            candidates.emplace_back(baseDir + clipBase + TJS_W(".webp"));
        }
        candidates.emplace_back(clipBase + TJS_W(".png"));
        candidates.emplace_back(clipBase + TJS_W(".webp"));

        for(const auto &candidate : candidates) {
            if(TVPIsExistentStorage(candidate)) {
                imagePath = candidate;
                return true;
            }
        }
        return false;
    }

    std::shared_ptr<tTVPBaseBitmap> loadCenteredMotionClipFrameImage(
        const std::string &motionPath,
        const std::string &clipLabel,
        ttstr &imagePath) {
        if(!resolveCenteredMotionClipFrameImage(motionPath, clipLabel,
                                                imagePath)) {
            return nullptr;
        }

        try {
            auto bitmap = std::make_shared<tTVPBaseBitmap>(1, 1, 32);
            TVPLoadGraphic(bitmap.get(), imagePath, TVP_clNone, 0, 0,
                           glmNormal, nullptr, nullptr);
            if(bitmap->GetWidth() > 0 && bitmap->GetHeight() > 0) {
                return bitmap;
            }
        } catch(...) {
        }
        return nullptr;
    }

    bool centeredMotionPresentationContainerName(const std::string &name) {
        return name == "sd" || name == "trans_sd" ||
            name == "ev" || name == "trans_ev";
    }

    int hideCenteredMotionPresentationContainerChildren(
        tTJSNI_BaseLayer *layer) {
        if(!layer) {
            return 0;
        }

        int hidden = 0;
        const auto name =
            renderDebugLowercase(motion::detail::narrow(layer->GetName()));
        if(centeredMotionPresentationContainerName(name)) {
            const auto childCount = layer->GetCount();
            for(tjs_uint i = 0; i < childCount; ++i) {
                auto *child = layer->GetChildren(static_cast<tjs_int>(i));
                if(child && child->GetVisible()) {
                    child->SetVisible(false);
                    ++hidden;
                }
            }
            if(hidden > 0) {
                layer->Update(false);
            }
        }

        const auto childCount = layer->GetCount();
        for(tjs_uint i = 0; i < childCount; ++i) {
            hidden += hideCenteredMotionPresentationContainerChildren(
                layer->GetChildren(static_cast<tjs_int>(i)));
        }
        return hidden;
    }

    int hideOtherCenteredMotionPresentationContainers(
        tTJSNI_BaseLayer *layer,
        tTJSNI_BaseLayer *keepLayer) {
        if(!layer) {
            return 0;
        }

        int hidden = 0;
        bool keepAncestor = false;
        for(auto *ancestor = keepLayer; ancestor; ancestor = ancestor->GetParent()) {
            if(ancestor == layer) {
                keepAncestor = true;
                break;
            }
        }
        const auto name =
            renderDebugLowercase(motion::detail::narrow(layer->GetName()));
        if(!keepAncestor &&
           centeredMotionPresentationContainerName(name) &&
           layer->GetVisible()) {
            layer->SetVisible(false);
            layer->Update(false);
            ++hidden;
        }

        const auto childCount = layer->GetCount();
        for(tjs_uint i = 0; i < childCount; ++i) {
            hidden += hideOtherCenteredMotionPresentationContainers(
                layer->GetChildren(static_cast<tjs_int>(i)), keepLayer);
        }
        return hidden;
    }

    void syncCenteredPresentationHoldMotionFrame(
        tTJSNI_BaseLayer *sourceLayer,
        const std::string &motionPath,
        int canvasWidth,
        int canvasHeight) {
        if(!sourceLayer || !isCenteredGameMotion(motionPath) ||
           layerBelongsToCgViewPresentation(sourceLayer)) {
            return;
        }

        auto *sourceImage = sourceLayer->GetMainImage();
        if(!sourceImage || sourceImage->GetWidth() <= 0 ||
           sourceImage->GetHeight() <= 0) {
            return;
        }

        const auto width = static_cast<tjs_int>(sourceImage->GetWidth());
        const auto height = static_cast<tjs_int>(sourceImage->GetHeight());
        const auto now = TVPGetTickCount();
        int syncedLayers = 0;

        auto syncEntryBitmap = [&](CenteredPresentationHoldEntry &entry) {
            if(entry.motion != motionPath) {
                return false;
            }
            if(!entry.bitmap || entry.width != width ||
               entry.height != height) {
                entry.bitmap = std::make_shared<tTVPBaseBitmap>(
                    static_cast<tjs_uint>(width),
                    static_cast<tjs_uint>(height), 32);
                entry.width = width;
                entry.height = height;
            }
            entry.bitmap->Fill(tTVPRect(0, 0, width, height), 0x00000000);
            entry.bitmap->CopyRect(0, 0, sourceImage,
                                   tTVPRect(0, 0, width, height));
            entry.capturedTick = now;
            entry.holdUntilTick = std::max(
                entry.holdUntilTick,
                now + kCenteredPresentationHoldDurationMs);
            return true;
        };

        auto syncLiveLayer = [&](CenteredPresentationHoldEntry &entry,
                                 tTJSVariant &slot) {
            if(slot.Type() != tvtObject) {
                return;
            }
            auto *layerObject = slot.AsObjectNoAddRef();
            auto *layer = resolveNativeLayer(layerObject);
            if(!layer || layer == sourceLayer || !layer->GetVisible() ||
               !layer->GetParentVisible()) {
                return;
            }
            const auto layerName = renderDebugLowercase(
                motion::detail::narrow(layer->GetName()));
            if(layerName.find("trans") != std::string::npos) {
                return;
            }
            if(copyCenteredPresentationHoldEntryToLayer(
                   layer, entry, canvasWidth, canvasHeight,
                   false, false, true)) {
                ++syncedLayers;
            }
        };

        int syncedEntries = 0;
        for(auto &item : centeredPresentationHoldCache()) {
            auto &entry = item.second;
            if(!syncEntryBitmap(entry)) {
                continue;
            }
            ++syncedEntries;
            syncLiveLayer(entry, entry.layer);
            syncLiveLayer(entry, entry.overlayLayer);
        }

        if(LOGGER && shouldDebugTitleRender(motionPath) && syncedEntries > 0) {
            LOGGER->info(
                "motion centered hold sync: motion={} source=[{}] entries={} liveLayers={}",
                motionPath, describeLayerForDebug(sourceLayer),
                syncedEntries, syncedLayers);
        }
    }

    bool renderCenteredMotionClipFrameImage(
        iTJSDispatch2 *layerObject,
        tTJSNI_BaseLayer *layer,
        const std::string &motionPath,
        const std::string &clipLabel,
        double configuredResolution,
        int canvasWidth,
        int canvasHeight) {
        if(!layerObject || !layer || !isCenteredGameMotion(motionPath)) {
            return false;
        }
        if(layerBelongsToCgViewPresentation(layer)) {
            return false;
        }

        ttstr imagePath;
        auto frameBitmap = loadCenteredMotionClipFrameImage(
            motionPath, clipLabel, imagePath);
        if(!frameBitmap) {
            return false;
        }

        tTVPRect sourceRect(
            0, 0,
            static_cast<tjs_int>(frameBitmap->GetWidth()),
            static_cast<tjs_int>(frameBitmap->GetHeight()));
        tTVPRect visibleSourceRect;
        const bool croppedTransparentFrame =
            bitmapVisibleBounds(frameBitmap.get(), visibleSourceRect) &&
            visibleSourceRect.get_width() > 0 &&
            visibleSourceRect.get_height() > 0 &&
            (visibleSourceRect.left > 0 || visibleSourceRect.top > 0 ||
             visibleSourceRect.right <
                 static_cast<tjs_int>(frameBitmap->GetWidth()) ||
             visibleSourceRect.bottom <
                 static_cast<tjs_int>(frameBitmap->GetHeight()));
        if(croppedTransparentFrame) {
            sourceRect = visibleSourceRect;
        }

        const double fullFrameScale =
            (std::isfinite(configuredResolution) && configuredResolution > 0.0)
                ? 100.0 / configuredResolution
                : 1.0;
        const auto scaledCenteredDestination =
            centeredMotionScaledFrameDestinationForSize(
                sourceRect.get_width(), sourceRect.get_height(),
                canvasWidth, canvasHeight, fullFrameScale);

        tTVPRect destination;
        bool hasDestination = false;
        if(auto *entry = findCenteredPresentationHoldEntry(layer, motionPath)) {
            hasDestination =
                bitmapVisibleBounds(entry->bitmap.get(), destination);
        }
        if(!hasDestination) {
            hasDestination =
                bitmapVisibleBounds(layer->GetMainImage(), destination);
        }
        if(hasDestination &&
           !centeredMotionDestinationLooksSaneForSize(
               destination, sourceRect.get_width(), sourceRect.get_height(),
               canvasWidth, canvasHeight)) {
            hasDestination = false;
        }
        if(hasDestination && fullFrameScale < 0.999 &&
           scaledCenteredDestination.get_width() > 0 &&
           scaledCenteredDestination.get_height() > 0 &&
           (destination.get_width() >
                scaledCenteredDestination.get_width() * 6 / 5 ||
            destination.get_height() >
                scaledCenteredDestination.get_height() * 6 / 5)) {
            hasDestination = false;
        }
        if(hasDestination && croppedTransparentFrame) {
            const auto expected = centeredMotionFrameDestinationForSize(
                sourceRect.get_width(), sourceRect.get_height(),
                canvasWidth, canvasHeight);
            if(expected.get_width() > 0 && expected.get_height() > 0 &&
               destination.get_width() > expected.get_width() * 3 / 2 &&
               destination.get_height() > expected.get_height() * 3 / 2) {
                hasDestination = false;
            }
        }
        const bool usedCenteredDestination = !hasDestination;
        if(!hasDestination) {
            destination = scaledCenteredDestination;
        }
        const bool constrainedForMessageUi =
            constrainCenteredMotionDestinationAboveMessageUi(
                destination, sourceRect.get_width(), sourceRect.get_height(),
                layer, canvasWidth, canvasHeight);
        if(destination.get_width() <= 0 ||
           destination.get_height() <= 0) {
            return false;
        }

        try {
            int hiddenChildren = 0;
            if(!prepareMotionPresentationLayerForRender(
                   layer, std::max(canvasWidth, destination.right),
                   std::max(canvasHeight, destination.bottom))) {
                return false;
            }
            clearMotionPresentationLayer(layer, canvasWidth, canvasHeight);
            hiddenChildren +=
                hideCenteredMotionPresentationContainerChildren(layer);
            layer->StretchCopy(destination, frameBitmap.get(), sourceRect,
                               stLinear);
            layer->SetVisible(true);
            configureCenteredGameMotionPresentationHitPassthrough(layer);
            placeCenteredPresentationBelowMessageUi(layer, "full-frame");
            captureCenteredPresentationHoldFrame(layerObject, layer, motionPath);
            syncCenteredPresentationHoldMotionFrame(
                layer, motionPath, canvasWidth, canvasHeight);
            syncYuzuSdPreviewPresentationLayers(
                layer, motionPath, canvasWidth, canvasHeight, "full-frame");
            showCenteredPresentationMessageUiOverlay(
                layer, canvasWidth, canvasHeight, "full-frame");
            layer->Update(false);
            if(LOGGER && shouldDebugTitleRender(motionPath)) {
                LOGGER->info(
                    "motion centered full-frame image: motion={} clip={} image={} target=[{}] dest=[{},{},{},{}] centeredFallback={} resolution={:.2f} scale={:.4f} sourceRect=[{},{},{},{}] source={}x{} hiddenChildren={} frame=[{}] layerAfter=[{}]",
                    motionPath, clipLabel, motion::detail::narrow(imagePath),
                    describeLayerForDebug(layer), destination.left,
                    destination.top, destination.right, destination.bottom,
                    usedCenteredDestination ? 1 : 0,
                    configuredResolution, fullFrameScale,
                    sourceRect.left, sourceRect.top, sourceRect.right,
                    sourceRect.bottom,
                    frameBitmap->GetWidth(), frameBitmap->GetHeight(),
                    hiddenChildren,
                    sampleBitmapStats(frameBitmap.get()),
                    sampleBitmapStats(layer->GetMainImage()));
                if(constrainedForMessageUi) {
                    LOGGER->info(
                        "motion centered full-frame constrained above message ui: motion={} clip={} dest=[{},{},{},{}]",
                        motionPath, clipLabel, destination.left,
                        destination.top, destination.right,
                        destination.bottom);
                }
            }
            return true;
        } catch(...) {
        }
        return false;
    }

    void hideCenteredPresentationHoldOverlaysForReplacementLayer(
        tTJSNI_BaseLayer *layer) {
        auto &cache = centeredPresentationHoldCache();
        if(cache.empty()) {
            return;
        }

        std::string replacementLayerName;
        if(centeredGameMotionStablePresentationLayer(layer)) {
            replacementLayerName =
                renderDebugLowercase(motion::detail::narrow(layer->GetName()));
        }

        bool anyOverlayVisible = false;
        for(const auto &item : cache) {
            if(centeredPresentationHoldOverlayIsActive(item.second)) {
                anyOverlayVisible = true;
                break;
            }
        }
        if(!anyOverlayVisible ||
           !layerLooksLikeReplacementPresentation(layer)) {
            return;
        }

        for(auto &item : cache) {
            if(!replacementLayerName.empty() &&
               item.second.layerName != replacementLayerName) {
                continue;
            }
            // assignImages has already installed the replacement pixels. A
            // hold surface is only a temporary bridge while no replacement is
            // available; copying it back here would overwrite a newly loaded
            // still CG with the preceding animation's final frame.
            hideCenteredPresentationHoldOverlay(item.second);
            if(item.second.layer.Type() == tvtObject) {
                hideCenteredPresentationMessageUiOverlay(
                    resolveNativeLayer(item.second.layer.AsObjectNoAddRef()));
            }
        }
    }

    void captureCenteredPresentationHoldFrame(
        iTJSDispatch2 *layerObject,
        tTJSNI_BaseLayer *layer,
        const std::string &motionPath) {
        if(!layerObject) {
            return;
        }
        if(!isCenteredGameMotion(motionPath) ||
           layerBelongsToCgViewPresentation(layer)) {
            return;
        }
        auto *entry = captureCenteredPresentationHoldLayerSamples(
            layer, motionPath);
        if(!entry) {
            return;
        }

        entry->layer =
            tTJSVariant(layerObject, layerObject);
        hideCenteredPresentationHoldOverlay(*entry);

        const auto currentFamily =
            yuzuSdPresentationLayerFamily(layer);
        if(currentFamily != YuzuSdPresentationLayerFamily::None &&
           isYuzuSdPreviewMotionPath(motionPath)) {
            auto &cache = centeredPresentationHoldCache();
            std::vector<tTJSNI_BaseLayer *> staleKeys;
            staleKeys.reserve(cache.size());
            for(auto &cached : cache) {
                const auto &previous = cached.second;
                if(&previous == entry ||
                   previous.cgViewPresentation != entry->cgViewPresentation ||
                   yuzuSdPresentationLayerFamilyName(previous.layerName) !=
                       currentFamily) {
                    continue;
                }
                staleKeys.push_back(cached.first);
            }
            for(auto *staleKey : staleKeys) {
                auto stale = cache.find(staleKey);
                if(stale == cache.end()) {
                    continue;
                }
                if(LOGGER &&
                   std::getenv("AETHERKIRI_MOTION_LAYER_DEBUG")) {
                    LOGGER->info(
                        "motion centered hold released replaced SD layer: motion={} oldMotion={} family={} oldLayer={} newLayer={}",
                        motionPath,
                        stale->second.motion,
                        currentFamily == YuzuSdPresentationLayerFamily::Sd
                            ? "sd"
                            : "event",
                        stale->second.layerName,
                        entry->layerName);
                }
                releaseCenteredPresentationHoldEntry(
                    stale->first, stale->second);
                cache.erase(stale);
            }
        }
    }

    bool restoreCenteredPresentationHoldFrame(
        tTJSNI_BaseLayer *layer,
        const std::string &motionPath,
        int canvasWidth,
        int canvasHeight,
        bool allowHiddenParent = false) {
        if(!centeredGameMotionStablePresentationLayer(layer) ||
           !isCenteredGameMotion(motionPath) ||
           (!allowHiddenParent && !layer->GetParentVisible())) {
            return false;
        }

        auto *entry = findCenteredPresentationHoldEntry(layer, motionPath);
        if(!entry || !entry->bitmap || entry->motion != motionPath ||
           entry->width <= 0 || entry->height <= 0) {
            return false;
        }

        return copyCenteredPresentationHoldEntryToLayer(
            layer, *entry, canvasWidth, canvasHeight, false, false, true);
    }

    void postProcessCenteredGameMotionSeparateLayerPresentation(
        iTJSDispatch2 *presentationLayerObject,
        iTJSDispatch2 *renderTargetObject,
        const std::string &motionPath,
        int canvasWidth,
        int canvasHeight) {
        if(!isCenteredGameMotion(motionPath) || canvasWidth <= 0 ||
           canvasHeight <= 0) {
            return;
        }

        auto *renderLayer = resolveNativeLayer(renderTargetObject);
        auto *presentationLayer = resolveNativeLayer(presentationLayerObject);
        if(renderLayer) {
            renderLayer->SetHitType(htMask);
            renderLayer->SetHitThreshold(256);
            if(motionPresentationLayerHasVisibleSamples(renderLayer)) {
                const bool renderIsCgViewSdPreview =
                    layerBelongsToCgViewPresentation(renderLayer) &&
                    isYuzuSdPreviewMotionPath(motionPath);
                if(renderIsCgViewSdPreview) {
                    captureCenteredPresentationHoldFrame(
                        renderTargetObject, renderLayer, motionPath);
                    syncCenteredPresentationHoldMotionFrame(
                        renderLayer, motionPath, canvasWidth, canvasHeight);
                } else if(!layerBelongsToCgViewPresentation(renderLayer)) {
                    syncYuzuSdPreviewPresentationLayers(
                        renderLayer, motionPath, canvasWidth, canvasHeight,
                        "sla-private-post-render");
                }
            }
        }

        if(!presentationLayer) {
            return;
        }

        configureCenteredGameMotionPresentationHitPassthrough(presentationLayer);
        if(!layerBelongsToCgViewPresentation(presentationLayer)) {
            placeCenteredPresentationBelowMessageUi(presentationLayer,
                                                    "sla-post-render");
        }

        const bool presentationIsCgViewAffine =
            layerIsCgViewAffineSurface(presentationLayer);
        const bool presentationCanReceivePixels =
            motion::internal::presentationLayerTypeCanReceivePixels(
                presentationLayer->GetType());
        bool presentationVisible =
            presentationCanReceivePixels &&
            motionPresentationLayerHasVisibleSamples(presentationLayer);
        if(!presentationVisible && renderLayer &&
           motionPresentationLayerHasVisibleSamples(renderLayer) &&
           !presentationIsCgViewAffine && presentationCanReceivePixels) {
            copyPresentationFramePixelsPreservingLayerState(
                renderLayer, presentationLayer, canvasWidth, canvasHeight);
            presentationVisible =
                motionPresentationLayerHasVisibleSamples(presentationLayer);
        }

        if(presentationVisible && !presentationIsCgViewAffine) {
            captureCenteredPresentationHoldFrame(
                presentationLayerObject, presentationLayer, motionPath);
            syncCenteredPresentationHoldMotionFrame(
                presentationLayer, motionPath, canvasWidth, canvasHeight);
            syncYuzuSdPreviewPresentationLayers(
                presentationLayer, motionPath, canvasWidth, canvasHeight,
                "sla-presentation-post-render");
            showCenteredPresentationMessageUiOverlay(
                presentationLayer, canvasWidth, canvasHeight,
                "sla-post-render");
        } else {
            hideCenteredPresentationMessageUiOverlay(presentationLayer);
        }
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
        // Animated E-mote bounds often oscillate by one or two pixels. Exact
        // resizing destroys and recreates the Metal texture on every tick.
        // Keep the backing image at its largest observed size while retaining
        // the exact logical size, clip, and clear rectangle below.
        const int imageWidth = static_cast<int>(layer->GetImageWidth());
        const int imageHeight = static_cast<int>(layer->GetImageHeight());
        if(imageWidth < width || imageHeight < height) {
            layer->SetImageSize(
                static_cast<tjs_uint>(std::max(imageWidth, width)),
                static_cast<tjs_uint>(std::max(imageHeight, height)));
        }
        layer->SetSize(width, height);
        layer->SetClip(0, 0, width, height);
        tTVPRect rect(0, 0, width, height);
        auto *image = layer->GetMainImage();
        if(!image) {
            return false;
        }
        if(!TVPGodotClearMotionScratchInPlace(
               image, rect, clearColor)) {
            image->Fill(rect, clearColor);
        }
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
        // World corners already include the complete ancestor transform, so a
        // nested leaf that does not belong to a render group can be drawn
        // straight into the presentation target.  Routing those leaves through
        // local scratch layers doubles the pixel work (source -> scratch ->
        // target); large looping SD motions then monopolize the script tick and
        // make typewriter text visibly stutter.  Synthetic/stencil groups,
        // local viewport clips and authored Photoshop blend modes still use
        // buffered composition to preserve their ordering and mask semantics.
        const unsigned lowNibble =
            static_cast<unsigned>(command.blendMode) & 0x0Fu;
        return !command.clearEnabled &&
            !command.hasRenderParent &&
            !command.requiresLocalClip &&
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

    bool axisAlignedRectBoundsFromCorners(
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

        // Stretch/OperateStretch take integer rectangles, but a PSB motion can
        // place a perfectly axis-aligned quad on a fractional pixel (the SD
        // assets in LimeLight are commonly scaled by 0.75). Requiring integer
        // authored corners sends those quads through OperateTriangles, whose
        // compatibility implementation is a synchronous software rasterizer.
        // Snap only the bounds; rotated/skewed quads still use the affine path.
        const int il = static_cast<int>(std::lround(left));
        const int it = static_cast<int>(std::lround(top));
        const int ir = static_cast<int>(std::lround(right));
        const int ib = static_cast<int>(std::lround(bottom));
        if(ir <= il || ib <= it) {
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

    bool applyMotionAlphaMaskLike_0x6AC4E4(
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
        const tTVPRect dstMaskRect(dstX, dstY, dstX + width, dstY + height);
        const tTVPRect srcMaskRect(srcX, srcY, srcX + width, srcY + height);
        const bool gpuMaskApplied = TVPGodotApplyAlphaMask(
            dstBmp, srcBmp, dstMaskRect, srcMaskRect,
            threshold, thresholdMaskMode, itemFlags);
        if(!gpuMaskApplied) {
            for(int y = 0; y < height; ++y) {
                auto *dstRow = static_cast<std::uint8_t *>(
                    dstBmp->GetScanLineForWrite(dstY + y));
                const auto *srcRow = static_cast<const std::uint8_t *>(
                    srcBmp->GetScanLine(srcY + y));
                for(int x = 0; x < width; ++x) {
                    auto *dstPixel = dstRow + (dstX + x) * 4;
                    const auto *srcPixel = srcRow + (srcX + x) * 4;
                    const auto srcAlpha = static_cast<int>(srcPixel[3]);
                    auto &dstAlpha = dstPixel[3];
                    switch(itemFlags) {
                        case 1:
                            if(thresholdMaskMode) {
                                if(srcAlpha < threshold) dstAlpha = 0;
                            } else {
                                dstAlpha = static_cast<std::uint8_t>(
                                    (static_cast<int>(dstAlpha) * srcAlpha) / 255);
                            }
                            break;
                        case 2:
                            if(thresholdMaskMode) {
                                if(srcAlpha >= threshold) dstAlpha = 0;
                            } else {
                                dstAlpha = static_cast<std::uint8_t>(
                                    ((255 - srcAlpha) *
                                     static_cast<int>(dstAlpha)) / 255);
                            }
                            break;
                        case 5:
                        case 6:
                            if(thresholdMaskMode) {
                                if(srcAlpha >= threshold) dstAlpha = 255;
                            } else {
                                dstAlpha = static_cast<std::uint8_t>(
                                    srcAlpha + ((255 - srcAlpha) *
                                                static_cast<int>(dstAlpha)) / 255);
                            }
                            break;
                        default:
                            break;
                    }
                }
            }
        }

        motion::detail::logoChainTraceLogf(
            motionPath, "execute.mask", "0x6AC4E4", frameTime,
            "dstNode={} srcNode={} itemFlags={} playerStencilType={} threshold={} requested=[{},{},{},{}] overlap=[{},{},{},{}]",
            dstNodeIndex, srcNodeIndex, itemFlags, playerStencilType, threshold,
            requestedRect.left, requestedRect.top,
            requestedRect.right, requestedRect.bottom,
            overlapRect.left, overlapRect.top,
            overlapRect.right, overlapRect.bottom);
        return true;
    }

} // namespace

extern "C" bool AetherKiriMotionRestoreCenteredPresentationLayer(
    tTJSNI_BaseLayer *layer) {
    const bool trace = std::getenv("AETHERKIRI_MOTION_LAYER_DEBUG") != nullptr;
    if(centeredGameMotionPresentationLayerShouldPassHit(layer)) {
        configureCenteredGameMotionPresentationHitPassthrough(layer);
        placeCenteredPresentationBelowMessageUi(layer, "restore-hook");
    }
    auto logReturn = [&](const char *reason, bool result) {
        if(trace && LOGGER && centeredGameMotionPresentationLayerShouldPassHit(layer)) {
            LOGGER->info(
                "motion centered restore hook layer=[{}] reason={} result={}",
                describeLayerForDebug(layer), reason ? reason : "<null>",
                result ? 1 : 0);
        }
        return result;
    };
    if(!centeredGameMotionStablePresentationLayer(layer)) {
        return logReturn("not-centered-stable", false);
    }
    if(!layer->GetVisible() || layer->GetOpacity() == 0 ||
       !layer->GetParentVisible()) {
        auto *entry = findLiveCenteredPresentationHoldEntry(layer);
        if(!entry) {
            return logReturn("no-live-entry", false);
        }
        const bool shown = copyCenteredPresentationHoldEntryToOverlay(
            *entry, layer);
        return logReturn(shown ? "overlay-shown" : "overlay-copy-failed",
                         shown);
    }
    if(layer->GetVisible() && layer->GetOpacity() != 0 &&
       motionPresentationLayerHasVisibleSamples(layer)) {
        return logReturn("already-visible", false);
    }

    auto *entry = findLiveCenteredPresentationHoldEntry(layer);
    if(!entry) {
        return logReturn("no-live-entry", false);
    }

    const int canvasWidth = std::max<tjs_int>(
        std::max<tjs_int>(layer->GetImageWidth(), layer->GetWidth()),
        entry->width);
    const int canvasHeight = std::max<tjs_int>(
        std::max<tjs_int>(layer->GetImageHeight(), layer->GetHeight()),
        entry->height);
    const bool restored = copyCenteredPresentationHoldEntryToLayer(
        layer, *entry, canvasWidth, canvasHeight, false, false, true);
    return logReturn(restored ? "restored" : "copy-failed", restored);
}

extern "C" bool
AetherKiriMotionPreserveCenteredPresentationLayerBeforeTransparentClear(
    tTJSNI_BaseLayer *layer,
    tjs_int x,
    tjs_int y,
    tjs_int width,
    tjs_int height) {
    const bool trace = std::getenv("AETHERKIRI_MOTION_LAYER_DEBUG") != nullptr;
    if(centeredGameMotionPresentationLayerShouldPassHit(layer)) {
        configureCenteredGameMotionPresentationHitPassthrough(layer);
        placeCenteredPresentationBelowMessageUi(layer, "preserve-hook");
    }
    auto logReturn = [&](const char *reason, bool result) {
        if(trace && LOGGER &&
           centeredGameMotionPresentationLayerShouldPassHit(layer)) {
            LOGGER->info(
                "motion centered preserve hook layer=[{}] reason={} result={}",
                describeLayerForDebug(layer), reason ? reason : "<null>",
                result ? 1 : 0);
        }
        return result;
    };
    if(!centeredGameMotionStablePresentationLayer(layer)) {
        return logReturn("not-centered-stable", false);
    }
    if(!hasCenteredPresentationHoldHistoryForLayer(layer)) {
        return logReturn("no-history", false);
    }
    if(findLiveCenteredPresentationHoldEntry(layer)) {
        return logReturn("live-entry-preserved", true);
    }
    if(width <= 0 || height <= 0 || x > 0 || y > 0) {
        return logReturn("partial-clear", false);
    }

    const tjs_int coveredWidth = x + width;
    const tjs_int coveredHeight = y + height;
    const tjs_int requiredWidth = std::max<tjs_int>(
        layer->GetImageWidth(), layer->GetWidth());
    const tjs_int requiredHeight = std::max<tjs_int>(
        layer->GetImageHeight(), layer->GetHeight());
    if(coveredWidth < requiredWidth || coveredHeight < requiredHeight) {
        return logReturn("partial-clear", false);
    }

    auto *entry = captureCenteredPresentationHoldLayerSamples(layer, "");
    if(entry) {
        return logReturn("preserved", true);
    }
    entry = findLiveCenteredPresentationHoldEntry(layer);
    if(entry && (!layer->GetVisible() || layer->GetOpacity() == 0 ||
                 !layer->GetParentVisible())) {
        const bool shown = copyCenteredPresentationHoldEntryToOverlay(
            *entry, layer);
        return logReturn(shown ? "overlay-shown" : "overlay-copy-failed",
                         shown);
    }
    return logReturn("no-visible-samples", false);
}

extern "C" bool AetherKiriMotionShowCenteredPresentationHoldOverlay(
    tTJSNI_BaseLayer *layer) {
    const bool trace = std::getenv("AETHERKIRI_MOTION_LAYER_DEBUG") != nullptr;
    if(centeredGameMotionPresentationLayerShouldPassHit(layer)) {
        configureCenteredGameMotionPresentationHitPassthrough(layer);
        placeCenteredPresentationBelowMessageUi(layer, "show-overlay-hook");
    }
    auto logReturn = [&](const char *reason, bool result) {
        if(trace && LOGGER &&
           centeredGameMotionPresentationLayerShouldPassHit(layer)) {
            LOGGER->info(
                "motion centered hold overlay layer=[{}] reason={} result={}",
                describeLayerForDebug(layer), reason ? reason : "<null>",
                result ? 1 : 0);
        }
        return result;
    };
    if(!centeredGameMotionStablePresentationLayer(layer)) {
        return logReturn("not-centered-stable", false);
    }

    auto *entry = findLiveCenteredPresentationHoldEntry(layer);
    if(!entry && motionPresentationLayerHasVisibleSamples(layer)) {
        entry = captureCenteredPresentationHoldLayerSamples(layer, "");
    }
    if(!entry) {
        return logReturn("no-live-entry", false);
    }

    const bool shown = copyCenteredPresentationHoldEntryToOverlay(*entry, layer);
    return logReturn(shown ? "shown" : "copy-failed", shown);
}

extern "C" void AetherKiriMotionTickCenteredPresentationHoldOverlays() {
    auto &cache = centeredPresentationHoldCache();
    if(cache.empty()) {
        return;
    }

    const auto now = TVPGetTickCount();
    for(auto &item : cache) {
        auto &entry = item.second;
        refreshCenteredPresentationHoldEntryFromVisibleLayer(entry, now);
        if(entry.layer.Type() == tvtObject) {
            if(auto *layer =
                   resolveNativeLayer(entry.layer.AsObjectNoAddRef())) {
                const bool hasLayerImage =
                    layer->GetHasImage() && layer->GetMainImage();
                const tjs_int imageWidth =
                    hasLayerImage ? layer->GetImageWidth() : 0;
                const tjs_int imageHeight =
                    hasLayerImage ? layer->GetImageHeight() : 0;
                const int canvasWidth = std::max<tjs_int>(
                    std::max<tjs_int>(imageWidth, layer->GetWidth()),
                    entry.width);
                const int canvasHeight = std::max<tjs_int>(
                    std::max<tjs_int>(imageHeight, layer->GetHeight()),
                    entry.height);
                if(layer->GetVisible() && layer->GetParentVisible() &&
                   hasLayerImage &&
                   motionPresentationLayerHasVisibleSamples(layer)) {
                    showCenteredPresentationMessageUiOverlay(
                        layer, canvasWidth, canvasHeight,
                        "message-ui-tick");
                }
            }
        }

        if(entry.overlayLayer.Type() != tvtObject) {
            if(entry.holdUntilTick != 0 && entry.holdUntilTick < now) {
                entry.holdUntilTick = 0;
            }
            continue;
        }

        auto *overlay =
            resolveNativeLayer(entry.overlayLayer.AsObjectNoAddRef());
        if(!overlay || !overlay->GetVisible()) {
            entry.overlayFramesRemaining = 0;
            continue;
        }
        placeCenteredPresentationBelowMessageUi(overlay, "hold-overlay-tick");
        if(entry.overlayFramesRemaining > 0) {
            --entry.overlayFramesRemaining;
            if(entry.overlayFramesRemaining == 0) {
                hideCenteredPresentationHoldOverlay(entry);
                continue;
            }
        }
        if(entry.holdUntilTick != 0 && entry.holdUntilTick < now) {
            hideCenteredPresentationHoldOverlay(entry);
            entry.holdUntilTick = 0;
        }
    }
}

extern "C" void
AetherKiriMotionDismissCenteredPresentationHoldOverlaysForLayer(
    tTJSNI_BaseLayer *layer) {
    hideCenteredPresentationHoldOverlaysForReplacementLayer(layer);
}

namespace motion {

    // --- Drawing/rendering ---
    void Player::setClearColor(tjs_int color) { _runtime->clearColor = color; }

    void Player::setResizable(bool v) { _runtime->resizable = v; }

    void Player::removeAllTextures() {
        _runtime->sourcesByKey.clear();
        _runtime->sourceLookupMisses.clear();
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

        const auto *selectedClip = selectActiveClip();
        const std::string clipLabel =
            selectedClip ? selectedClip->label : std::string{};

        if(LOGGER && shouldDebugTitleRender(_runtime->activeMotion->path)) {
            LOGGER->info("motion build node tree: player={} parent={} parentNode={} motion={} clip={} playing={} loop={} loopTime={:.3f} totalFrames={:.3f} selfSync={:.3f} sync={:.3f}",
                         static_cast<const void *>(this),
                         static_cast<const void *>(_motionParentPlayer),
                         _motionParentNodeIndex,
                         _runtime->activeMotion->path,
                         clipLabel.empty() ? std::string("<root>") : clipLabel,
                         joinRenderStrings(_runtime->playingTimelineLabels),
                         selectedClip && selectedClip->loop ? 1 : 0,
                         selectedClip ? selectedClip->loopTime : -1.0,
                         selectedClip ? selectedClip->totalFrames : 0.0,
                         selectedClip ? selectedClip->selfSyncTime : 0.0,
                         selectedClip ? selectedClip->syncTime : 0.0);
            if(const auto *clip = selectedClip) {
                LOGGER->info(
                    "motion build node tree layers: motion={} clip={} clipLayers=[{}] rootLayers=[{}]",
                    _runtime->activeMotion->path, clip->label,
                    joinRenderStrings(clip->layerNames),
                    joinRenderStrings(_runtime->activeMotion->layerNames));
            }
        }

        _runtime->nodes =
            detail::buildNodeTree(*_runtime->activeMotion, selectedClip);
        _runtime->nodesBuilt = true;
        _layersDirty = true;

        if(!_runtime->nodes.empty()) {
            auto &root = _runtime->nodes[0];
            root.localState.flipX = _rootFlipX;
            if(_hasPendingRootPos) {
                root.localState.posX = _pendingRootX;
                root.localState.posY = _pendingRootY;
                root.localState.posZ = _pendingRootZ;
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
        for(size_t ni = 1; ni < _runtime->nodes.size(); ++ni) {
            auto &node = _runtime->nodes[ni];
            if(node.nodeType == 3) {
                if(auto *child = node.getChildPlayer()) {
                    child->_motionParentPlayer = this;
                    child->_motionParentNodeIndex = static_cast<int>(ni);
                    // Child motion nodes are advanced by updateLayers(). They
                    // must not retain a second global progress callback after
                    // the parent tree is replaced during a KAG page clone.
                    child->disableAutoProgress();
                }
            }
        }

        if(LOGGER && shouldDebugTitleRender(_runtime->activeMotion->path)) {
            std::ostringstream nodeSummary;
            const size_t limit =
                std::min<size_t>(_runtime->nodes.size(), 24);
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
                            << ":st" << node.stencilType
                            << ":b" << node.accumulated.blendMode
                            << ":pd" << node.priorDraw
                            << ":pz" << node.parameterizeIndex
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
        const bool logoTraceEnabled =
            detail::logoChainTraceEnabledForPath(motionPath);
        for(const auto &entry : _runtime->preparedRenderItems) {
            if(!entry.drawFlag || entry.skipFlag0 || entry.skipFlag1 ||
               (entry.opacity <= 0 && !entry.stencilMaskReferenced)) {
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
            if(isYuzuTitleStencilUtilityLayer(motionPath, entry.sourceKey)) {
                if(LOGGER && shouldDebugTitleRender(motionPath, entry.sourceKey) &&
                   markRenderDebugLogged(
                       "title-stencil-utility:" + motionPath + ":" +
                       entry.sourceKey)) {
                    LOGGER->info(
                        "motion skip title stencil utility layer: motion={} node={} label={} source={}",
                        motionPath, entry.nodeIndex, entry.nodeLabel,
                        entry.sourceKey);
                }
                continue;
            }
            // Yuzu SD motions carry a white bitmap named `mask` as stencil
            // input for a nested set. Drawing it as a normal colour layer
            // washes the authored table/background out to white.
            if(isYuzuSdStencilUtilityLayer(motionPath, entry.nodeLabel,
                                           entry.sourceKey)) {
                continue;
            }
            if(isYuzuStartupLogoWhiteWashLayer(motionPath, entry.nodeLabel,
                                               entry.sourceKey)) {
                if(LOGGER && shouldDebugTitleRender(motionPath, entry.sourceKey) &&
                   markRenderDebugLogged(
                       "yuzu-logo-white-wash:" + motionPath + ":" +
                       entry.nodeLabel + ":" + entry.sourceKey)) {
                    LOGGER->info(
                        "motion skip yuzu logo white wash layer: motion={} node={} label={} source={}",
                        motionPath, entry.nodeIndex, entry.nodeLabel,
                        entry.sourceKey);
                }
                continue;
            }

            RenderClipRect clipRect;
            std::string clipFailureReason;
            if(!computeRenderClipRect(entry, canvasWidth, canvasHeight, clipRect,
                                      &clipFailureReason)) {
                if(logoTraceEnabled) {
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
                }
                continue;
            }

            detail::PlayerRuntime::RenderCommand command;
            command.nodeIndex = entry.nodeIndex;
            command.renderScopeId = entry.renderScopeId;
            command.scopedNodeIndex = entry.scopedNodeIndex;
            command.parentRenderScopeId = entry.parentRenderScopeId;
            command.scopedParentNodeIndex =
                entry.scopedParentNodeIndex;
            command.outerRenderAncestorChain =
                entry.outerRenderAncestorChain;
            command.nodeLabel = entry.nodeLabel;
            command.srcRef = entry.srcRef;
            command.sourceKey = entry.sourceKey;
            command.sourceMotion = entry.sourceMotion;
            command.hasOwnSource = entry.hasOwnSource;
            command.groupOnly = entry.groupOnly;
            command.implicitVisibleStencilGroup =
                entry.implicitVisibleStencilGroup;
            command.implicitVisibleStencilBase =
                entry.implicitVisibleStencilBase;
            command.implicitVisibleStencilGroupNodeIndex =
                entry.implicitVisibleStencilGroupNodeIndex;
            command.stencilMaskReferenced = entry.stencilMaskReferenced;
            command.blendMode = entry.blendMode;
            command.opacity = entry.opacity;
            command.itemFlags = entry.updateCount;
            command.parentNodeIndex = entry.visibleAncestorIndex;
            command.stencilMaskNodeIndices = entry.stencilMaskNodeIndices;
            command.packedColors = entry.packedColors;

            // Numbered Yuzu title motions use bm=21 with opaque black as the
            // neutral presentation marker for the full-colour character
            // pass. Treating it as an ordinary corner multiplier erases every
            // RGB channel and leaves only an invisible black alpha shape.
            if(isYuzuNumberedTitleCharacterMotion(motionPath,
                                                  entry.sourceKey) &&
               command.blendMode == 21 &&
               packedColorsAreOpaqueBlack(
                   command.packedColors[0], command.packedColors[1],
                   command.packedColors[2], command.packedColors[3])) {
                command.blendMode = 16;
                command.packedColors = {
                    0xFF808080u, 0xFF808080u,
                    0xFF808080u, 0xFF808080u
                };
            }
            command.visibleAncestorIndex = entry.visibleAncestorIndex;
            command.requiresLocalClip = entry.hasViewport &&
                (entry.viewport[0] > entry.paintBox[0] + 0.01f ||
                 entry.viewport[1] > entry.paintBox[1] + 0.01f ||
                 entry.viewport[2] < entry.paintBox[2] - 0.01f ||
                 entry.viewport[3] < entry.paintBox[3] - 0.01f);
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
                            "motion title prepared item: motion={} source={} node={} label={} frameTick={:.2f} opacity={} blend={} sort={:.3f} meshType={} mesh={}x{} parent={} flags={} clip=[{},{},{},{}] paint=[{:.1f},{:.1f},{:.1f},{:.1f}] world=[{:.1f},{:.1f},{:.1f},{:.1f},{:.1f},{:.1f},{:.1f},{:.1f}]",
                            motionPath, entry.sourceKey, entry.nodeIndex,
                            entry.nodeLabel, _frameTickCount, command.opacity,
                            command.blendMode, entry.sortKey, command.meshType,
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

            if(logoTraceEnabled) {
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
        std::unordered_map<
            std::uintptr_t, std::unordered_map<int, size_t>>
            commandIndexByScopedNode;
        for(size_t i = 0; i < _runtime->renderCommands.size(); ++i) {
            _runtime->renderCommands[i].childCommandIndices.clear();
            _runtime->renderCommands[i].stencilModifierCommandIndices.clear();
            _runtime->renderCommands[i].stencilMaskCommandIndices.clear();
            _runtime->renderCommands[i]
                .differenceAlphaMaskSourceCommandIndices.clear();
            _runtime->renderCommands[i]
                .differenceAlphaMaskGroupCommandIndices.clear();
            _runtime->renderCommands[i]
                .differenceAlphaMaskInputCommandIndices.clear();
            _runtime->renderCommands[i]
                .differenceAlphaMaskOperation = 0;
            _runtime->renderCommands[i].alphaMaskOnly = false;
            _runtime->renderCommands[i].leafBuilt = false;
            _runtime->renderCommands[i].composedBuilt = false;
            _runtime->renderCommands[i].executedDirect = false;
            _runtime->renderCommands[i].builtRect = {0, 0, 0, 0};
            // Prepared-item mask membership keeps zero-opacity authored mask
            // sources alive through command creation. Mask inputs referenced
            // by more than one layer still need their dedicated off-screen
            // lifetime below.
            _runtime->renderCommands[i].stencilMaskReferenced = false;
            commandIndexByNode.emplace(_runtime->renderCommands[i].nodeIndex, i);
            const auto &command = _runtime->renderCommands[i];
            if(command.renderScopeId != 0 &&
               command.scopedNodeIndex >= 0) {
                commandIndexByScopedNode[command.renderScopeId].emplace(
                    command.scopedNodeIndex, i);
            }
        }
        auto findCommandIndex =
            [&](std::uintptr_t scopeId, int scopedNodeIndex,
                int flattenedNodeIndex) -> size_t {
                if(scopeId != 0 && scopedNodeIndex >= 0) {
                    const auto scopeIt =
                        commandIndexByScopedNode.find(scopeId);
                    if(scopeIt != commandIndexByScopedNode.end()) {
                        const auto nodeIt =
                            scopeIt->second.find(scopedNodeIndex);
                        if(nodeIt != scopeIt->second.end()) {
                            return nodeIt->second;
                        }
                    }
                }
                const auto flattenedIt =
                    commandIndexByNode.find(flattenedNodeIndex);
                return flattenedIt == commandIndexByNode.end()
                    ? _runtime->renderCommands.size()
                    : flattenedIt->second;
            };
        auto findNearestAncestorCommandIndex =
            [&](std::uintptr_t scopeId, int scopedNodeIndex,
                int flattenedNodeIndex,
                const std::vector<
                    detail::PlayerRuntime::RenderAncestorReference>
                    &outerAncestorChain) -> size_t {
                std::set<std::pair<std::uintptr_t, int>>
                    visitedScopedNodes;
                auto walkScopedAncestors =
                    [&](std::uintptr_t candidateScopeId,
                        int candidateScopedNodeIndex,
                        int candidateFlattenedNodeIndex) -> size_t {
                        while(true) {
                            const size_t commandIndex = findCommandIndex(
                                candidateScopeId,
                                candidateScopedNodeIndex,
                                candidateFlattenedNodeIndex);
                            if(commandIndex <
                               _runtime->renderCommands.size()) {
                                return commandIndex;
                            }
                            if(candidateScopeId == 0 ||
                               candidateScopedNodeIndex < 0 ||
                               !visitedScopedNodes.insert(
                                   {candidateScopeId,
                                    candidateScopedNodeIndex}).second) {
                                break;
                            }
                            // Flattened child motions can point through
                            // source-less transform nodes that correctly have
                            // no render command. Walk the complete local node
                            // tree before crossing the next recorded Player
                            // boundary.
                            const auto scopeIt =
                                commandIndexByScopedNode.find(
                                    candidateScopeId);
                            if(scopeIt ==
                               commandIndexByScopedNode.end()) {
                                break;
                            }
                            const auto *scopeRuntime =
                                reinterpret_cast<
                                    const detail::PlayerRuntime *>(
                                        candidateScopeId);
                            if(!scopeRuntime ||
                               candidateScopedNodeIndex >=
                                   static_cast<int>(
                                       scopeRuntime->nodes.size())) {
                                break;
                            }
                            const int nextScopedNodeIndex =
                                scopeRuntime->nodes[
                                    candidateScopedNodeIndex]
                                    .visibleAncestorIndex;
                            if(nextScopedNodeIndex ==
                               candidateScopedNodeIndex) {
                                break;
                            }
                            candidateScopedNodeIndex =
                                nextScopedNodeIndex;
                            candidateFlattenedNodeIndex = -1;
                        }
                        return _runtime->renderCommands.size();
                    };

                size_t commandIndex = walkScopedAncestors(
                    scopeId, scopedNodeIndex, flattenedNodeIndex);
                if(commandIndex < _runtime->renderCommands.size()) {
                    return commandIndex;
                }
                for(const auto &outerAncestor :
                        outerAncestorChain) {
                    commandIndex = walkScopedAncestors(
                        outerAncestor.renderScopeId,
                        outerAncestor.scopedNodeIndex, -1);
                    if(commandIndex <
                       _runtime->renderCommands.size()) {
                        return commandIndex;
                    }
                }
                return _runtime->renderCommands.size();
            };
        auto rebuildCommandIndexMaps = [&]() {
            commandIndexByNode.clear();
            commandIndexByScopedNode.clear();
            commandIndexByNode.reserve(
                _runtime->renderCommands.size());
            for(size_t i = 0;
                i < _runtime->renderCommands.size(); ++i) {
                const auto &command =
                    _runtime->renderCommands[i];
                commandIndexByNode.emplace(command.nodeIndex, i);
                if(command.renderScopeId != 0 &&
                   command.scopedNodeIndex >= 0) {
                    commandIndexByScopedNode[
                        command.renderScopeId].emplace(
                            command.scopedNodeIndex, i);
                }
            }
        };
        struct IndependentDifferenceMaskMember {
            int commandIndex = -1;
            // Command indices from the drawable itself up to and including
            // the independent flags-6 carrier.
            std::vector<size_t> ancestry;
        };
        std::vector<std::vector<IndependentDifferenceMaskMember>>
            independentDifferenceMaskSourceMembers(
                _runtime->renderCommands.size());
        std::vector<std::vector<IndependentDifferenceMaskMember>>
            independentDifferenceMaskTargetMembers(
                _runtime->renderCommands.size());
        for(size_t i = 0; i < _runtime->renderCommands.size(); ++i) {
            const auto &childCommand = _runtime->renderCommands[i];
            int ancestorNodeIndex = childCommand.parentNodeIndex;
            std::uintptr_t ancestorScopeId =
                childCommand.parentRenderScopeId;
            int ancestorScopedNodeIndex =
                childCommand.scopedParentNodeIndex;
            auto ancestorOuterChain =
                childCommand.outerRenderAncestorChain;
            std::unordered_set<size_t> visitedAncestorCommands;
            std::vector<size_t> commandAncestry{i};
            while(ancestorNodeIndex >= 0 ||
                  (ancestorScopeId != 0 &&
                   ancestorScopedNodeIndex >= 0)) {
                const size_t ancestorCommandIndex =
                    findNearestAncestorCommandIndex(
                    ancestorScopeId, ancestorScopedNodeIndex,
                    ancestorNodeIndex, ancestorOuterChain);
                if(ancestorCommandIndex >=
                       _runtime->renderCommands.size() ||
                   !visitedAncestorCommands.insert(
                       ancestorCommandIndex).second) {
                    break;
                }
                commandAncestry.push_back(ancestorCommandIndex);
                auto &ancestorCommand =
                    _runtime->renderCommands[ancestorCommandIndex];
                if(ancestorCommand.groupOnly) {
                    // Native type-12 render items own the complete drawable
                    // descendant subtree, including layers separated from the
                    // group by ordinary transform/blank nodes. Flatten every
                    // such drawable into the nearest composite command so an
                    // iris texture cannot escape to the final target merely
                    // because its immediate parent is `UD`/`LR`.
                    // Every authored composite mask owns the complete
                    // descendant subtree. This is also required for a single
                    // mask: flattened child-player artwork can sit below
                    // transform nodes, and leaving it on the direct path
                    // bypasses the mask entirely (for example a character's
                    // bangs escaping the head-outline stencil). Older E-mote
                    // data also authors op-5 groups without a mask list: their
                    // first drawable child is the visible stencil base and the
                    // remaining children are cropped to its alpha.
                    const bool isStandaloneAlphaModifier =
                        ancestorCommand.stencilMaskNodeIndices.empty() &&
                        (ancestorCommand.itemFlags & 7) == 6;
                    const size_t modifierParentCommandIndex =
                        findCommandIndex(
                            ancestorCommand.parentRenderScopeId,
                            ancestorCommand.scopedParentNodeIndex,
                            ancestorCommand.parentNodeIndex);
                    const bool hasConcreteRenderParent =
                        ancestorCommand.parentNodeIndex >= 0 &&
                        modifierParentCommandIndex <
                            _runtime->renderCommands.size() &&
                        modifierParentCommandIndex != ancestorCommandIndex;
                    const bool isIndependentDifferenceMask =
                        internal::isIndependentDifferenceAlphaMaskGroup(
                            ancestorCommand.groupOnly,
                            !ancestorCommand.stencilMaskNodeIndices.empty(),
                            ancestorCommand.itemFlags,
                            hasConcreteRenderParent);
                    if(isIndependentDifferenceMask) {
                        // sub_6C7088 applies item+264 as an alpha modifier to
                        // each ordinary descendant work surface. Retain
                        // mode-6 leaves only as hidden alpha inputs. Ordinary
                        // colour descendants remain standalone outputs: their
                        // individual authored Z positions place the broad
                        // mosaic below the small mosaic and liquid artwork.
                        ancestorCommand.alphaMaskOnly = true;
                        if(internal::isDifferenceAlphaPassThroughLeaf(
                               childCommand.hasOwnSource,
                               childCommand.groupOnly,
                               childCommand.blendMode)) {
                            ancestorCommand
                                .differenceAlphaMaskSourceCommandIndices
                                .push_back(static_cast<int>(i));
                            independentDifferenceMaskSourceMembers[
                                ancestorCommandIndex].push_back(
                                    IndependentDifferenceMaskMember{
                                        static_cast<int>(i),
                                        commandAncestry});
                        } else if(
                            internal::canReceiveIndependentDifferenceAlphaMask(
                                childCommand.hasOwnSource,
                                childCommand.groupOnly,
                                childCommand.blendMode) &&
                            !internal::isSyntheticMotionBlankSource(
                                childCommand.sourceKey)) {
                            independentDifferenceMaskTargetMembers[
                                ancestorCommandIndex].push_back(
                                    IndependentDifferenceMaskMember{
                                        static_cast<int>(i),
                                        commandAncestry});
                            // This carrier contributes alpha but is not the
                            // target's render parent. Keep walking so the
                            // colour leaf joins the next outer composite at
                            // its original sorted position. Stopping here
                            // would make it a standalone final output that
                            // incorrectly covers siblings such as the small
                            // mosaic already drawn by the outer group.
                            const int nextAncestorNodeIndex =
                                ancestorCommand.parentNodeIndex;
                            const std::uintptr_t nextAncestorScopeId =
                                ancestorCommand.parentRenderScopeId;
                            const int nextAncestorScopedNodeIndex =
                                ancestorCommand.scopedParentNodeIndex;
                            if(nextAncestorNodeIndex ==
                                   ancestorNodeIndex &&
                               nextAncestorScopeId ==
                                   ancestorScopeId &&
                               nextAncestorScopedNodeIndex ==
                                   ancestorScopedNodeIndex) {
                                break;
                            }
                            ancestorNodeIndex =
                                nextAncestorNodeIndex;
                            ancestorScopeId =
                                nextAncestorScopeId;
                            ancestorScopedNodeIndex =
                                nextAncestorScopedNodeIndex;
                            ancestorOuterChain =
                                ancestorCommand
                                    .outerRenderAncestorChain;
                            continue;
                        }
                    } else if(
                        !ancestorCommand.stencilMaskNodeIndices.empty() ||
                        ancestorCommand.implicitVisibleStencilGroup ||
                        isStandaloneAlphaModifier) {
                        ancestorCommand.childCommandIndices.push_back(
                            static_cast<int>(i));
                        _runtime->renderCommands[i].hasRenderParent = true;
                    }
                    break;
                }
                const int nextAncestorNodeIndex =
                    ancestorCommand.parentNodeIndex;
                if(nextAncestorNodeIndex == ancestorNodeIndex) {
                    break;
                }
                ancestorNodeIndex = nextAncestorNodeIndex;
                ancestorScopeId =
                    ancestorCommand.parentRenderScopeId;
                ancestorScopedNodeIndex =
                    ancestorCommand.scopedParentNodeIndex;
                ancestorOuterChain =
                    ancestorCommand.outerRenderAncestorChain;
            }
        }
        for(size_t groupCommandIndex = 0;
            groupCommandIndex < _runtime->renderCommands.size();
            ++groupCommandIndex) {
            auto &group =
                _runtime->renderCommands[groupCommandIndex];
            if(!group.alphaMaskOnly ||
               group.differenceAlphaMaskSourceCommandIndices.empty()) {
                continue;
            }
            const auto &sourceMembers =
                independentDifferenceMaskSourceMembers[
                    groupCommandIndex];
            const auto &targetMembers =
                independentDifferenceMaskTargetMembers[
                    groupCommandIndex];
            for(const auto &targetMember : targetMembers) {
                const int targetCommandIndex =
                    targetMember.commandIndex;
                if(targetCommandIndex < 0 ||
                   targetCommandIndex >= static_cast<int>(
                       _runtime->renderCommands.size())) {
                    continue;
                }
                auto &target =
                    _runtime->renderCommands[targetCommandIndex];
                bool hasSelectedPair = false;
                bool hasLabelPair = false;
                size_t nestedSourceCount = 0;
                size_t nestedPairCount = 0;
                for(const auto &sourceMember : sourceMembers) {
                    const int sourceCommandIndex =
                        sourceMember.commandIndex;
                    if(sourceCommandIndex < 0 ||
                       sourceCommandIndex >= static_cast<int>(
                           _runtime->renderCommands.size())) {
                        continue;
                    }
                    const auto &source =
                        _runtime->renderCommands[sourceCommandIndex];
                    const bool labelPair =
                        internal::isAuthoredDifferenceAlphaPair(
                            target.nodeLabel, source.nodeLabel);
                    const bool nestedRelationship =
                        internal::isNestedDifferenceAlphaPair(
                            static_cast<size_t>(targetCommandIndex),
                            sourceMember.ancestry);
                    if(nestedRelationship) {
                        ++nestedSourceCount;
                    }
                    const bool nestedPair =
                        nestedRelationship &&
                        internal::isGenericDifferenceAlphaLabel(
                            source.nodeLabel);
                    if(labelPair || nestedPair) {
                        target
                            .differenceAlphaMaskInputCommandIndices
                            .push_back(sourceCommandIndex);
                        hasSelectedPair = true;
                        hasLabelPair =
                            hasLabelPair || labelPair;
                        if(nestedPair) {
                            ++nestedPairCount;
                        }
                    }
                }
                if(internal::shouldUseCombinedDifferenceAlphaMask(
                       hasSelectedPair, nestedSourceCount)) {
                    // Native keeps both item+304 masks for authored
                    // additional parts, and also builds their union at the
                    // flags-6 carrier's group+324 slot. A base colour leaf
                    // such as general_obj_y owns named fade_* descendants but
                    // has no same-name alpha sibling, so it consumes that
                    // combined group mask. An unpaired foreground leaf has no
                    // descendant mask and remains visible without one. A
                    // single transient named branch is likewise incomplete
                    // and must not crop away the carrier's other half.
                    for(const auto &sourceMember : sourceMembers) {
                        if(sourceMember.commandIndex >= 0 &&
                           sourceMember.commandIndex < static_cast<int>(
                               _runtime->renderCommands.size())) {
                            target
                                .differenceAlphaMaskInputCommandIndices
                                .push_back(
                                    sourceMember.commandIndex);
                        }
                    }
                }
                const bool useDifferenceOperation =
                    hasLabelPair ||
                    internal::
                        isUnambiguousNestedDifferenceAlphaPair(
                            nestedPairCount);
                target.differenceAlphaMaskOperation =
                    internal::independentDifferenceAlphaMaskOperation(
                        useDifferenceOperation, group.itemFlags);
                if(!target
                        .differenceAlphaMaskInputCommandIndices
                        .empty()) {
                    target
                        .differenceAlphaMaskGroupCommandIndices
                        .push_back(
                            static_cast<int>(groupCommandIndex));
                }
            }
            // The carrier contributes alpha topology only. Visible
            // descendants retain their own Z positions and output
            // independently after their masks have been applied.
            group.alphaMaskOnly = true;
        }
        // A flags-6 container modifies its authored parent only when that
        // parent has a concrete render command. Containers below source-less
        // transforms were marked alphaMaskOnly above: native has no item+264
        // colour target for them, so neither the carrier nor its mask RGB is
        // copied to the final canvas.
        for(size_t i = 0; i < _runtime->renderCommands.size(); ++i) {
            auto &modifier = _runtime->renderCommands[i];
            if(!modifier.groupOnly ||
               !modifier.stencilMaskNodeIndices.empty() ||
               (modifier.itemFlags & 7) != 6 ||
               modifier.parentNodeIndex < 0) {
                continue;
            }
            const size_t parentCommandIndex = findCommandIndex(
                modifier.parentRenderScopeId,
                modifier.scopedParentNodeIndex,
                modifier.parentNodeIndex);
            if(parentCommandIndex >=
                   _runtime->renderCommands.size() ||
               parentCommandIndex == i) {
                continue;
            }
            auto &parent =
                _runtime->renderCommands[parentCommandIndex];
            parent.stencilModifierCommandIndices.push_back(
                static_cast<int>(i));
            modifier.hasRenderParent = true;
        }
        // Native sub_6C7440 walks a dedicated stencil-mask item list after it
        // has composed the group's ordinary children.  Preserve that
        // distinction instead of treating every colour child as a mask.
        for(auto &command : _runtime->renderCommands) {
            for(const int maskNodeIndex : command.stencilMaskNodeIndices) {
                if(const auto it = commandIndexByNode.find(maskNodeIndex);
                   it != commandIndexByNode.end() &&
                   it->second < _runtime->renderCommands.size() &&
                   _runtime->renderCommands[it->second].nodeIndex !=
                       command.nodeIndex) {
                    command.stencilMaskCommandIndices.push_back(
                        static_cast<int>(it->second));
                    if(command.stencilMaskNodeIndices.size() > 1) {
                        _runtime->renderCommands[it->second]
                            .stencilMaskReferenced = true;
                    }
                }
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
        const bool logoTraceEnabled =
            detail::logoChainTraceEnabledForPath(motionPath);

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
            if(logoTraceEnabled) {
                detail::logoChainTraceCheck(
                    motionPath, "execute.setup", "0x6C7440", _clampedEvalTime,
                    "renderLayer should resolve before executeLayerRenderCommands",
                    fmt::format("renderLayer={}",
                                static_cast<const void *>(renderLayer)),
                    false,
                    "SLA/Layer backend could not resolve native layers before copy");
            }
            return false;
        }

        struct RenderProfileStats {
            int baseHits = 0;
            int baseMisses = 0;
            int sharedBitmapHits = 0;
            int sharedBitmapMisses = 0;
            int preparedHits = 0;
            int preparedMisses = 0;
            int storageLoads = 0;
            int psbLoads = 0;
            int tintBuilds = 0;
            int tintEvictions = 0;
            int directOutputs = 0;
            int bufferedOutputs = 0;
            int commandOutputCacheHits = 0;
            int commandLeafCacheHits = 0;
            std::uint64_t baseResolveUs = 0;
            std::uint64_t preparedResolveUs = 0;
            std::uint64_t tintBuildUs = 0;
            std::uint64_t tintAllocateUs = 0;
            std::uint64_t tintApplyUs = 0;
            std::uint64_t psbMetadataUs = 0;
            std::uint64_t psbDecodeUs = 0;
            std::uint64_t psbConvertUs = 0;
        };
        RenderProfileStats profileStats;
        const bool profileEnabled = motionRenderProfileEnabled();
        const auto profileStartUs =
            profileEnabled ? motionRenderProfileNowUs() : 0;
        auto &baseSourceCache = _runtime->motionSourceBitmapCache;
        auto &preparedSourceCache = _runtime->motionPreparedBitmapCache;
        auto &materializedKeysBySource =
            _runtime->motionPreparedMaterializedKeysBySource;
        // A source can occur more than once in one command list, and direct
        // commands resolve it once while building and again while copying.
        // Never evict a variant already touched by this execution.
        std::unordered_set<std::string> preparedKeysTouchedThisExecution;
        const auto preparedVariantCapacity =
            [](const std::shared_ptr<tTVPBaseBitmap> &bitmap) -> size_t {
                const size_t pixelCount = bitmap
                    ? static_cast<size_t>(bitmap->GetWidth()) *
                          static_cast<size_t>(bitmap->GetHeight())
                    : 0u;
                constexpr size_t kLargePreparedBitmapPixels = 512u * 512u;
                return pixelCount >= kLargePreparedBitmapPixels ? 4u : 16u;
            };
        const auto trimMaterializedPreparedKeys =
            [&](std::deque<std::string> &keys, size_t capacity) {
                while(keys.size() > capacity) {
                    const auto evictIt = std::find_if(
                        keys.begin(), keys.end(), [&](const std::string &key) {
                            return preparedKeysTouchedThisExecution.find(key) ==
                                preparedKeysTouchedThisExecution.end();
                        });
                    if(evictIt == keys.end()) {
                        // More live variants than the normal capacity are
                        // valid in one frame. The next cache hit or insertion
                        // trims them after that frame's touch set expires.
                        break;
                    }
                    preparedSourceCache.erase(*evictIt);
                    keys.erase(evictIt);
                    if(profileEnabled) {
                        ++profileStats.tintEvictions;
                    }
                }
            };
        const auto touchMaterializedPreparedKey =
            [&](const std::string &sourceKey, const std::string &preparedKey) {
                preparedKeysTouchedThisExecution.insert(preparedKey);
                auto sourceIt = materializedKeysBySource.find(sourceKey);
                if(sourceIt == materializedKeysBySource.end()) {
                    return;
                }
                auto &keys = sourceIt->second;
                const auto keyIt =
                    std::find(keys.begin(), keys.end(), preparedKey);
                if(keyIt == keys.end()) {
                    return;
                }
                keys.erase(keyIt);
                keys.push_back(preparedKey);
                const auto bitmapIt = preparedSourceCache.find(preparedKey);
                trimMaterializedPreparedKeys(
                    keys, preparedVariantCapacity(
                              bitmapIt != preparedSourceCache.end()
                                  ? bitmapIt->second
                                  : nullptr));
            };
        const auto cacheMaterializedPreparedBitmap =
            [&](const std::string &sourceKey, const std::string &preparedKey,
                const std::shared_ptr<tTVPBaseBitmap> &bitmap) {
                preparedSourceCache.emplace(preparedKey, bitmap);
                auto &keys = materializedKeysBySource[sourceKey];
                const auto existing =
                    std::find(keys.begin(), keys.end(), preparedKey);
                if(existing != keys.end()) {
                    keys.erase(existing);
                }
                keys.push_back(preparedKey);
                preparedKeysTouchedThisExecution.insert(preparedKey);
                trimMaterializedPreparedKeys(
                    keys, preparedVariantCapacity(bitmap));
            };
        const auto commandSourceIdentity =
            [&](const detail::PlayerRuntime::RenderCommand &command) {
                const auto *sourceMotion = command.sourceMotion
                    ? command.sourceMotion.get()
                    : _runtime->activeMotion.get();
                return (sourceMotion ? sourceMotion->path : std::string{}) +
                    '\n' + command.sourceKey;
            };
        auto resolveBaseSourceBitmap =
            [&](const detail::PlayerRuntime::RenderCommand &command)
                -> std::shared_ptr<tTVPBaseBitmap> {
            if(command.sourceKey.empty()) {
                return nullptr;
            }
            const auto *sourceMotion = command.sourceMotion
                ? command.sourceMotion.get()
                : _runtime->activeMotion.get();
            if(!sourceMotion) {
                return nullptr;
            }
            const auto sourceIdentity = commandSourceIdentity(command);
            if(auto it = baseSourceCache.find(sourceIdentity);
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
            int width = 0;
            int height = 0;
            double originX = 0.0;
            double originY = 0.0;
            std::string resourcePath;
            std::string compressName;
            std::string sharedBitmapKey;
            std::vector<std::uint8_t> decodedPixels;
            bool decodedPixelsAreBgra = false;
            const auto metadataStartUs =
                profileEnabled ? motionRenderProfileNowUs() : 0;
            const auto *resourceMetadata = findPSBResourceBySourceName(
                *sourceMotion, command.sourceKey, width, height,
                decodedPixels, originX, originY, nullptr, false,
                &resourcePath, &compressName);
            if(resourceMetadata && width > 0 && height > 0 &&
               !resourceMetadata->data.empty()) {
                sharedBitmapKey = makeSharedMotionSourceBitmapKey(
                    *sourceMotion, resourcePath, compressName,
                    width, height,
                    *resourceMetadata,
                    _runtime->motionSourceResourceFingerprintCache);
                srcBmp = findSharedMotionSourceBitmap(sharedBitmapKey);
                if(profileEnabled) {
                    if(srcBmp) {
                        ++profileStats.sharedBitmapHits;
                    } else {
                        ++profileStats.sharedBitmapMisses;
                    }
                }
            }
            if(profileEnabled) {
                profileStats.psbMetadataUs +=
                    motionRenderProfileNowUs() - metadataStartUs;
            }

            // Embedded E-mote icons are authoritative for their `src/...`
            // keys. Resolving them through the generic storage layer first
            // scans every PSB resource and performs several archive lookups
            // per icon, even though the exact resource was already found.
            // Keep external lookup only for motions without an embedded icon.
            const auto resolvedPath = resourceMetadata
                ? ttstr{}
                : resolveMotionSourcePath(
                      *sourceMotion, command.sourceKey);
            const auto resolvedPathString = detail::narrow(resolvedPath);
            const bool resolvedPathIsEmbeddedPsb =
                resolvedPathString.rfind("psb://", 0) == 0;
            // A PSB icon's `pixel` resource contains raw/RL/palettized pixels,
            // even though the compatibility storage path is exposed with a
            // synthetic `.png` suffix. Sending those bytes through the normal
            // image loader produces a 1x1 transparent fallback and prevents
            // the PSB-aware decoder below from ever running (for example the
            // animated dressing-room curtains in eyecatch3.mtn).
            if(!resolvedPath.IsEmpty() && !resolvedPathIsEmbeddedPsb) {
                ttstr loadPath = resolvedPath;
                const auto &pathString = resolvedPathString;
                if(pathString.rfind('.') == std::string::npos ||
                   pathString.rfind('.') < pathString.rfind('/')) {
                    loadPath = resolvedPath + TJS_W(".png");
                }
                try {
                    auto bmp = std::make_shared<tTVPBaseBitmap>(1, 1, 32);
                    TVPLoadGraphic(bmp.get(), loadPath, TVP_clNone, 0, 0,
                                   glmNormal, nullptr, nullptr);
                    if(bmp->GetWidth() > 0 && bmp->GetHeight() > 0) {
                        // LimeLight's numbered title motions address a
                        // 677x288 local logo, while the archive resolves the
                        // same key to a transparent 1920x1080 design-canvas
                        // PNG. Feeding that whole canvas to the local quad
                        // shrinks the visible logo by roughly three times.
                        // Crop the authored local rectangle first so the
                        // motion's scale/bounce transforms remain valid.
                        if(isYuzuNumberedTitleCharacterMotion(motionPath) &&
                           isYuzuTitleLogoLayer(command.nodeLabel,
                                                command.sourceKey) &&
                           bmp->GetWidth() == 1920 &&
                           bmp->GetHeight() == 1080) {
                            constexpr int logoLeft = 37;
                            constexpr int logoTop = 704;
                            constexpr int logoWidth = 677;
                            constexpr int logoHeight = 288;
                            auto cropped = std::make_shared<tTVPBaseBitmap>(
                                logoWidth, logoHeight, 32);
                            cropped->Fill(
                                tTVPRect(0, 0, logoWidth, logoHeight),
                                0x00000000);
                            cropped->CopyRect(
                                0, 0, bmp.get(),
                                tTVPRect(logoLeft, logoTop,
                                         logoLeft + logoWidth,
                                         logoTop + logoHeight));
                            srcBmp = cropped;
                        } else {
                            srcBmp = bmp;
                        }
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
                const auto *resource = resourceMetadata;
                if(!srcBmp && resourceMetadata) {
                    const auto decodeStartUs =
                        profileEnabled ? motionRenderProfileNowUs() : 0;
                    resource = findPSBResourceBySourceName(
                        *sourceMotion, command.sourceKey,
                        width, height, decodedPixels, originX, originY,
                        &decodedPixelsAreBgra, true);
                    if(profileEnabled) {
                        profileStats.psbDecodeUs +=
                            motionRenderProfileNowUs() - decodeStartUs;
                    }
                }
                if(resource && width > 0 && height > 0 && !resource->data.empty()) {
                    const bool sourcePixelsAreBgra = motionSourcePixelsAreBGRA(
                        decodedPixelsAreBgra);
                    if(!srcBmp) {
                        const auto convertStartUs =
                            profileEnabled ? motionRenderProfileNowUs() : 0;
                        const auto &pixelData = decodedPixels.empty()
                            ? resource->data : decodedPixels;
                        auto bmp = std::make_shared<tTVPBaseBitmap>(
                            static_cast<tjs_uint>(width),
                            static_cast<tjs_uint>(height), 32);
                        const auto totalPixels =
                            static_cast<std::size_t>(width) *
                            static_cast<std::size_t>(height);
                        const auto validPixels = std::min(
                            pixelData.size() / 4u, totalPixels);
                        // A decoded PSB icon normally covers the complete
                        // bitmap. Do not clear every pixel only to overwrite
                        // it immediately; retain the clear for truncated
                        // resources where the untouched tail must be alpha 0.
                        if(validPixels < totalPixels) {
                            bmp->Fill(tTVPRect(0, 0, width, height),
                                      0x00000000);
                        }
                        const auto *src = pixelData.data();
                        for(int y = 0; y < height; ++y) {
                            const auto rowOffset =
                                static_cast<std::size_t>(y) *
                                static_cast<std::size_t>(width);
                            if(rowOffset >= validPixels) {
                                break;
                            }
                            const auto rowPixels = std::min(
                                static_cast<std::size_t>(width),
                                validPixels - rowOffset);
                            auto *row = static_cast<std::uint8_t *>(
                                bmp->GetScanLineForWrite(
                                    static_cast<tjs_uint>(y)));
                            const auto *sourceRow =
                                src + rowOffset * 4u;
                            if(sourcePixelsAreBgra) {
                                std::memcpy(row, sourceRow, rowPixels * 4u);
                            } else {
                                TVPReverseRGB(
                                    reinterpret_cast<tjs_uint32 *>(row),
                                    reinterpret_cast<const tjs_uint32 *>(
                                        sourceRow),
                                    static_cast<tjs_int>(rowPixels));
                            }
                        }
                        srcBmp = bmp;
                        rememberSharedMotionSourceBitmap(
                            sharedBitmapKey, srcBmp);
                        if(profileEnabled) {
                            ++profileStats.psbLoads;
                            profileStats.psbConvertUs +=
                                motionRenderProfileNowUs() - convertStartUs;
                        }
                    }
                    sourceOrigin = fmt::format(
                        "psb:{}:{}x{}:origin=({:.3f},{:.3f}):bgra={}:shared={}",
                        command.sourceKey, width, height, originX, originY,
                        sourcePixelsAreBgra ? 1 : 0,
                        decodedPixels.empty() ? 1 : 0);
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

            baseSourceCache.emplace(sourceIdentity, srcBmp);
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
            const auto sourceIdentity = commandSourceIdentity(command);
            const auto tintKey = fmt::format(
                "{}|{:08x}|{:08x}|{:08x}|{:08x}|{}",
                sourceIdentity, command.packedColors[0],
                command.packedColors[1], command.packedColors[2],
                command.packedColors[3], useHalfAlphaTint ? 1 : 0);
            if(auto it = preparedSourceCache.find(tintKey);
               it != preparedSourceCache.end()) {
                if(profileEnabled) {
                    ++profileStats.preparedHits;
                }
                touchMaterializedPreparedKey(sourceIdentity, tintKey);
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
            // Tinting writes every destination pixel, so construct the result
            // directly from the immutable source instead of cloning the whole
            // bitmap and immediately walking it again. Besides avoiding one
            // full-size copy, this prevents per-scanline copy-on-write checks
            // from dominating large animated title-card sources.
            auto tinted = needsTint
                ? std::make_shared<tTVPBaseBitmap>(
                      static_cast<tjs_uint>(srcBmp->GetWidth()),
                      static_cast<tjs_uint>(srcBmp->GetHeight()), 32)
                : cloneBitmap32(*srcBmp);
            if(profileEnabled) {
                ++profileStats.tintBuilds;
                profileStats.tintAllocateUs +=
                    motionRenderProfileNowUs() - tintStartUs;
            }
            if(needsTint) {
                const auto tintApplyStartUs =
                    profileEnabled ? motionRenderProfileNowUs() : 0;
                applyPackedCornerTintLike_0x6A7518(
                    *srcBmp, *tinted, command.packedColors, useHalfAlphaTint,
                    tintDefaultNeutralMask, tintOnlyWhitePixels,
                    neutralStartupLogoTint(motionPath));
                if(profileEnabled) {
                    profileStats.tintApplyUs +=
                        motionRenderProfileNowUs() - tintApplyStartUs;
                }
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
            cacheMaterializedPreparedBitmap(sourceIdentity, tintKey,
                                             tinted);
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
                const bool canUseAxisAlignedStretch =
                    axisAlignedRectBoundsFromCorners(command.localCorners,
                                                     0.5f, 0.5f,
                                                     localRect);
                if(canUseAxisAlignedStretch) {
                    if(localRect.get_width() == sourceRect.get_width() &&
                       localRect.get_height() == sourceRect.get_height()) {
                        targetLayer->CopyRect(localRect.left, localRect.top,
                                              srcBmp.get(), nullptr, sourceRect);
                    } else {
                        targetLayer->StretchCopy(localRect, srcBmp.get(),
                                                 sourceRect, stLinear, 0.0);
                    }
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
        auto materializeDifferenceAlphaMaskSource =
            [&](size_t commandIndex) -> iTJSDispatch2 * {
                if(commandIndex >= _runtime->renderCommands.size()) {
                    return nullptr;
                }
                auto &maskCommand =
                    _runtime->renderCommands[commandIndex];
                if(!internal::isDifferenceAlphaPassThroughLeaf(
                       maskCommand.hasOwnSource,
                       maskCommand.groupOnly,
                       maskCommand.blendMode)) {
                    return nullptr;
                }
                if(maskCommand.maskLayer.Type() == tvtObject) {
                    return maskCommand.maskLayer.AsObjectNoAddRef();
                }
                detail::PlayerRuntime::EmoteCommandOutputCacheEntry
                    *persistentMaskEntry = nullptr;
                const auto maskSignature =
                    renderCommandLeafReuseSignature(maskCommand);
                if(_runtime->isEmoteMode) {
                    const auto maskCacheKey =
                        fmt::format("mask:{}", commandIndex);
                    auto [it, inserted] =
                        _runtime->emoteCommandOutputCache.try_emplace(
                            maskCacheKey);
                    (void)inserted;
                    persistentMaskEntry = &it->second;
                    persistentMaskEntry->lastUseGeneration =
                        _runtime->emoteCommandOutputCacheGeneration;
                    if(persistentMaskEntry->maskValid &&
                       persistentMaskEntry->maskSignature ==
                           maskSignature &&
                       persistentMaskEntry->maskLayer.Type() ==
                           tvtObject &&
                       resolveNativeLayer(
                           persistentMaskEntry->maskLayer
                               .AsObjectNoAddRef())) {
                        maskCommand.maskLayer =
                            persistentMaskEntry->maskLayer;
                        ++_runtime->emoteCommandLeafCacheHits;
                        if(profileEnabled) {
                            ++profileStats.commandLeafCacheHits;
                        }
                        return maskCommand.maskLayer
                            .AsObjectNoAddRef();
                    }
                    // Repaint into the retained layer on a signature miss,
                    // avoiding per-frame Layer/texture allocation.
                    maskCommand.maskLayer =
                        persistentMaskEntry->maskLayer;
                }
                auto maskBitmap = resolveSourceBitmap(maskCommand);
                if(!maskBitmap || maskBitmap->GetWidth() <= 0 ||
                   maskBitmap->GetHeight() <= 0) {
                    return nullptr;
                }
                auto recoveredMask =
                    recoverRgbEncodedDifferenceAlphaMask(
                        *maskBitmap);
                const bool recoveredAlphaFromRgb =
                    static_cast<bool>(recoveredMask);
                if(recoveredMask) {
                    maskBitmap = std::move(recoveredMask);
                }
                auto *maskLayerObject =
                    ensureCommandLayer(maskCommand.maskLayer);
                auto *maskLayer =
                    resolveNativeLayer(maskLayerObject);
                if(!maskLayerObject || !maskLayer) {
                    return nullptr;
                }
                const tTVPRect sourceRect(
                    0, 0,
                    static_cast<tjs_int>(maskBitmap->GetWidth()),
                    static_cast<tjs_int>(maskBitmap->GetHeight()));
                if(!renderCommandSourceToLayer(
                       maskCommand, maskLayerObject, maskLayer,
                       maskBitmap, sourceRect,
                       "difference.alphaMaskSource")) {
                    maskCommand.maskLayer.Clear();
                    if(persistentMaskEntry) {
                        persistentMaskEntry->maskValid = false;
                    }
                    return nullptr;
                }
                if(persistentMaskEntry) {
                    persistentMaskEntry->maskLayer =
                        maskCommand.maskLayer;
                    persistentMaskEntry->maskSignature =
                        maskSignature;
                    persistentMaskEntry->maskValid = true;
                }
                if(recoveredAlphaFromRgb && LOGGER) {
                    LOGGER->info(
                        "motion difference RGB alpha recovery: motion={} node={} label={} source={}",
                        motionPath, maskCommand.nodeIndex,
                        maskCommand.nodeLabel, maskCommand.sourceKey);
                }
                return maskLayerObject;
            };
        auto applyIndependentDifferenceAlphaMasks =
            [&](detail::PlayerRuntime::RenderCommand &command,
                iTJSDispatch2 *destinationLayerObject,
                int destinationWorldLeft,
                int destinationWorldTop) {
                if(!destinationLayerObject) {
                    return;
                }
                auto *destinationLayer =
                    resolveNativeLayer(destinationLayerObject);
                if(!destinationLayer ||
                   !destinationLayer->GetMainImage()) {
                    return;
                }
                const int destinationWidth =
                    command.clipRect[2] - command.clipRect[0];
                const int destinationHeight =
                    command.clipRect[3] - command.clipRect[1];
                if(destinationWidth <= 0 ||
                   destinationHeight <= 0) {
                    return;
                }
                auto *destinationBitmap =
                    destinationLayer->GetMainImage();
                for(const int groupCommandIndex :
                        command.differenceAlphaMaskGroupCommandIndices) {
                    if(groupCommandIndex < 0 ||
                       groupCommandIndex >= static_cast<int>(
                           _runtime->renderCommands.size())) {
                        continue;
                    }
                    const auto &group =
                        _runtime->renderCommands[groupCommandIndex];
                    struct DifferenceMaskSurface {
                        const tTVPBaseTexture *bitmap = nullptr;
                        std::array<int, 4> clipRect{
                            0, 0, 0, 0};
                    };
                    std::vector<DifferenceMaskSurface> surfaces;
                    for(const int sourceCommandIndex :
                            command
                                .differenceAlphaMaskInputCommandIndices) {
                        if(std::find(
                               group
                                   .differenceAlphaMaskSourceCommandIndices
                                   .begin(),
                               group
                                   .differenceAlphaMaskSourceCommandIndices
                                   .end(),
                               sourceCommandIndex) ==
                           group
                               .differenceAlphaMaskSourceCommandIndices
                               .end()) {
                            continue;
                        }
                        if(sourceCommandIndex < 0 ||
                           sourceCommandIndex >= static_cast<int>(
                               _runtime->renderCommands.size())) {
                            continue;
                        }
                        auto *sourceLayerObject =
                            materializeDifferenceAlphaMaskSource(
                                static_cast<size_t>(
                                    sourceCommandIndex));
                        auto *sourceLayer =
                            resolveNativeLayer(sourceLayerObject);
                        if(!sourceLayerObject || !sourceLayer ||
                           !sourceLayer->GetMainImage()) {
                            continue;
                        }
                        const auto &source =
                            _runtime->renderCommands[
                                sourceCommandIndex];
                        surfaces.push_back(
                            DifferenceMaskSurface{
                                sourceLayer->GetMainImage(),
                                source.clipRect});
                    }
                    if(surfaces.empty()) {
                        continue;
                    }
                    const bool thresholdMaskMode =
                        playerStencilType == 0;
                    const int maskOperation =
                        command.differenceAlphaMaskOperation != 0
                            ? command.differenceAlphaMaskOperation
                            : (group.itemFlags & 3);
                    bool gpuMaskApplied = false;
                    auto *unionMaskLayerObject =
                        ensureCommandLayer(command.unionMaskLayer);
                    auto *unionMaskLayer =
                        resolveNativeLayer(unionMaskLayerObject);
                    if(unionMaskLayerObject && unionMaskLayer &&
                       prepareLayerForRender(
                           unionMaskLayerObject, destinationWidth,
                           destinationHeight, 0x00000000)) {
                        std::vector<iTVPBaseBitmap *> maskBitmaps;
                        std::vector<tTVPRect> maskDstRects;
                        std::vector<tTVPRect> maskSrcRects;
                        maskBitmaps.reserve(surfaces.size());
                        maskDstRects.reserve(surfaces.size());
                        maskSrcRects.reserve(surfaces.size());
                        for(const auto &surface : surfaces) {
                            const int worldLeft = std::max(
                                destinationWorldLeft, surface.clipRect[0]);
                            const int worldTop = std::max(
                                destinationWorldTop, surface.clipRect[1]);
                            const int worldRight = std::min(
                                destinationWorldLeft + destinationWidth,
                                surface.clipRect[2]);
                            const int worldBottom = std::min(
                                destinationWorldTop + destinationHeight,
                                surface.clipRect[3]);
                            if(worldLeft >= worldRight ||
                               worldTop >= worldBottom) {
                                continue;
                            }
                            maskBitmaps.push_back(
                                const_cast<iTVPBaseBitmap *>(
                                    static_cast<const iTVPBaseBitmap *>(
                                        surface.bitmap)));
                            maskDstRects.emplace_back(
                                worldLeft - destinationWorldLeft,
                                worldTop - destinationWorldTop,
                                worldRight - destinationWorldLeft,
                                worldBottom - destinationWorldTop);
                            maskSrcRects.emplace_back(
                                worldLeft - surface.clipRect[0],
                                worldTop - surface.clipRect[1],
                                worldRight - surface.clipRect[0],
                                worldBottom - surface.clipRect[1]);
                        }
                        if(!maskBitmaps.empty()) {
                            gpuMaskApplied = TVPGodotApplyAlphaUnionMask(
                                destinationBitmap,
                                unionMaskLayer->GetMainImage(),
                                maskBitmaps.data(), maskDstRects.data(),
                                maskSrcRects.data(), maskBitmaps.size(),
                                thresholdMaskMode, maskOperation,
                                destinationWidth, destinationHeight);
                        }
                    }
                    if(!gpuMaskApplied) {
                        for(int destinationY = 0;
                            destinationY < destinationHeight;
                            ++destinationY) {
                            auto *destinationRow =
                                static_cast<std::uint8_t *>(
                                    destinationBitmap
                                        ->GetScanLineForWrite(
                                            destinationY));
                            const int worldY =
                                destinationWorldTop +
                                destinationY;
                            for(int destinationX = 0;
                                destinationX < destinationWidth;
                                ++destinationX) {
                                const int worldX =
                                    destinationWorldLeft +
                                    destinationX;
                                int unionAlpha = 0;
                                for(const auto &surface :
                                        surfaces) {
                                    if(worldX <
                                           surface.clipRect[0] ||
                                       worldY <
                                           surface.clipRect[1] ||
                                       worldX >=
                                           surface.clipRect[2] ||
                                       worldY >=
                                           surface.clipRect[3]) {
                                        continue;
                                    }
                                    const auto *sourceRow =
                                        static_cast<
                                            const std::uint8_t *>(
                                            surface.bitmap
                                                ->GetScanLine(
                                                    worldY -
                                                    surface
                                                        .clipRect[1]));
                                    const int sourceAlpha =
                                        sourceRow[
                                            (worldX -
                                             surface.clipRect[0]) *
                                                4 +
                                            3];
                                    if(thresholdMaskMode) {
                                        if(sourceAlpha >= 64) {
                                            unionAlpha = 255;
                                            break;
                                        }
                                    } else {
                                        unionAlpha +=
                                            ((255 - unionAlpha) *
                                             sourceAlpha) /
                                            255;
                                    }
                                }
                                auto &destinationAlpha =
                                    destinationRow[
                                        destinationX * 4 + 3];
                                destinationAlpha =
                                    internal::
                                        applyMotionAlphaMaskValueLike_0x6AC4E4(
                                            destinationAlpha,
                                            static_cast<std::uint8_t>(
                                                unionAlpha),
                                            thresholdMaskMode,
                                            maskOperation,
                                            64);
                            }
                        }
                    }
                    detail::logoChainTraceLogf(
                        motionPath, "execute.mask",
                        "0x6AC4E4", _clampedEvalTime,
                        "dstNode={} srcNode={} itemFlags={} playerStencilType={} threshold=64 pairedDifferenceSources={}",
                        command.nodeIndex, group.nodeIndex,
                        maskOperation, playerStencilType,
                        surfaces.size());
                }
            };
        const bool commandOutputCacheEnabled =
            _runtime->isEmoteMode && !_runtime->renderCommands.empty();
        const std::uint64_t commandCacheGeneration =
            commandOutputCacheEnabled
                ? ++_runtime->emoteCommandOutputCacheGeneration
                : 0;
        std::vector<std::string> commandCacheKeys(
            _runtime->renderCommands.size());
        std::vector<std::size_t> commandLeafSignatures(
            _runtime->renderCommands.size(), 0);
        std::vector<std::size_t> commandOutputSignatures(
            _runtime->renderCommands.size(), 0);
        std::vector<std::uint8_t> commandOutputSignatureState(
            _runtime->renderCommands.size(), 0);
        std::vector<bool> commandCacheEligible(
            _runtime->renderCommands.size(), commandOutputCacheEnabled);

        if(commandOutputCacheEnabled) {
            for(size_t i = 0; i < _runtime->renderCommands.size(); ++i) {
                const auto &command = _runtime->renderCommands[i];
                // Command order is stable through normal animation. Retain a
                // scratch allocation per slot; exact signatures below decide
                // whether pixels may be reused when topology changes.
                commandCacheKeys[i] = fmt::format("command:{}", i);
                commandLeafSignatures[i] =
                    renderCommandLeafReuseSignature(command);
                // An implicit stencil parent masks its child layer in-place
                // before compositing it. Retaining that mutated child would
                // apply the mask a second time on the next frame.
                if(command.groupOnly &&
                   command.implicitVisibleStencilGroup) {
                    for(const int childIndex :
                            command.childCommandIndices) {
                        if(childIndex >= 0 &&
                           childIndex < static_cast<int>(
                               commandCacheEligible.size())) {
                            const auto &child =
                                _runtime->renderCommands[
                                    static_cast<size_t>(childIndex)];
                            // Positive stencils on ordinary alpha children
                            // are now sampled while composing into the parent;
                            // the child's cached pixels remain pristine.
                            const bool usesNonDestructiveFusedStencil =
                                (command.itemFlags & 3) == 1 &&
                                resolveBlendOperationModeLike_0x6C7440(
                                    child.blendMode) == omAlpha;
                            if(!usesNonDestructiveFusedStencil) {
                                commandCacheEligible[
                                    static_cast<size_t>(childIndex)] = false;
                            }
                        }
                    }
                }
            }

            auto buildOutputSignature =
                [&](auto &&self, size_t commandIndex) -> std::size_t {
                if(commandIndex >= _runtime->renderCommands.size()) {
                    return 0;
                }
                if(commandOutputSignatureState[commandIndex] == 2) {
                    return commandOutputSignatures[commandIndex];
                }
                if(commandOutputSignatureState[commandIndex] == 1) {
                    // Authored mask graphs can point back to an ancestor.
                    // Its local signature is enough to close that edge while
                    // preserving deterministic invalidation.
                    return commandLeafSignatures[commandIndex];
                }
                commandOutputSignatureState[commandIndex] = 1;
                const auto &command =
                    _runtime->renderCommands[commandIndex];
                std::size_t seed = commandLeafSignatures[commandIndex];
                renderReuseHashCombine(seed, std::hash<int>{}(_maskMode));
                renderReuseHashCombine(
                    seed, std::hash<int>{}(command.itemFlags));
                renderReuseHashCombine(
                    seed,
                    std::hash<int>{}(
                        command.differenceAlphaMaskOperation));
                renderReuseHashCombine(
                    seed, std::hash<bool>{}(command.groupOnly));
                renderReuseHashCombine(
                    seed,
                    std::hash<bool>{
                        }(command.implicitVisibleStencilGroup));
                renderReuseHashCombine(
                    seed,
                    std::hash<bool>{
                        }(command.implicitVisibleStencilBase));
                renderReuseHashCombine(
                    seed,
                    std::hash<int>{
                        }(command.implicitVisibleStencilGroupNodeIndex));

                auto combineDependency =
                    [&](int dependencyIndex, std::size_t edgeKind) {
                    renderReuseHashCombine(seed, edgeKind);
                    if(dependencyIndex < 0 ||
                       dependencyIndex >= static_cast<int>(
                           _runtime->renderCommands.size())) {
                        renderReuseHashCombine(
                            seed, std::hash<int>{}(dependencyIndex));
                        return;
                    }
                    const auto dependencyOffset =
                        static_cast<size_t>(dependencyIndex);
                    const auto &dependency =
                        _runtime->renderCommands[dependencyOffset];
                    renderReuseHashCombine(
                        seed, self(self, dependencyOffset));
                    renderReuseHashCombine(
                        seed,
                        std::hash<int>{}(
                            dependency.clipRect[0] -
                            command.clipRect[0]));
                    renderReuseHashCombine(
                        seed,
                        std::hash<int>{}(
                            dependency.clipRect[1] -
                            command.clipRect[1]));
                    renderReuseHashCombine(
                        seed,
                        std::hash<int>{}(
                            dependency.clipRect[2] -
                            dependency.clipRect[0]));
                    renderReuseHashCombine(
                        seed,
                        std::hash<int>{}(
                            dependency.clipRect[3] -
                            dependency.clipRect[1]));
                    // These properties are deliberately excluded from a
                    // child's local bitmap signature: they take effect only
                    // when the parent consumes that bitmap.
                    renderReuseHashCombine(
                        seed,
                        std::hash<int>{}(dependency.opacity));
                    renderReuseHashCombine(
                        seed,
                        std::hash<int>{}(dependency.blendMode));
                    renderReuseHashCombine(
                        seed,
                        std::hash<int>{}(dependency.itemFlags));
                };

                for(const int index : command.childCommandIndices) {
                    combineDependency(index, 0x1001u);
                }
                for(const int index :
                        command.stencilModifierCommandIndices) {
                    combineDependency(index, 0x1002u);
                }
                for(const int index :
                        command.stencilMaskCommandIndices) {
                    combineDependency(index, 0x1003u);
                }
                for(const int index :
                        command.differenceAlphaMaskGroupCommandIndices) {
                    combineDependency(index, 0x1004u);
                }
                for(const int index :
                        command.differenceAlphaMaskInputCommandIndices) {
                    combineDependency(index, 0x1005u);
                }
                commandOutputSignatures[commandIndex] = seed;
                commandOutputSignatureState[commandIndex] = 2;
                return seed;
            };
            for(size_t i = 0; i < _runtime->renderCommands.size(); ++i) {
                buildOutputSignature(buildOutputSignature, i);
            }
        }

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
            // Native sub_6C7088 case 6 is a pass-through branch: it reuses the
            // existing work surface and never paints the item's source RGB.
            // Apply that rule to every standalone leaf, not only leaves that
            // happened to retain a flags-6 render parent after child-player
            // flattening.
            if(internal::isDifferenceAlphaPassThroughLeaf(
                   command.hasOwnSource,
                   command.groupOnly,
                   command.blendMode)) {
                command.builtRect = command.clipRect;
                return false;
            }

            const int clipWidth = command.clipRect[2] - command.clipRect[0];
            const int clipHeight = command.clipRect[3] - command.clipRect[1];
            if(clipWidth <= 0 || clipHeight <= 0) {
                return false;
            }

            auto srcBmp = resolveSourceBitmap(command);
            const bool hasSourceBitmap =
                srcBmp && srcBmp->GetWidth() > 0 && srcBmp->GetHeight() > 0;
            if(!hasSourceBitmap && command.childCommandIndices.empty() &&
               command.stencilModifierCommandIndices.empty()) {
                if(logoTraceEnabled) {
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
                }
                return false;
            }

            const tTVPRect sourceRect(
                0, 0,
                hasSourceBitmap ? static_cast<tjs_int>(srcBmp->GetWidth()) : 0,
                hasSourceBitmap ? static_cast<tjs_int>(srcBmp->GetHeight()) : 0);
            if(hasSourceBitmap && logoTraceEnabled) {
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
                !hasChildren && !command.stencilMaskReferenced &&
                command.stencilModifierCommandIndices.empty() &&
                command.differenceAlphaMaskGroupCommandIndices.empty() &&
                !command.hasRenderParent;
            if(useDirectRenderPath) {
                command.executedDirect = true;
                command.builtRect = command.clipRect;
                if(profileEnabled) {
                    ++profileStats.directOutputs;
                }
                return true;
            }

            const bool canReusePristineLeaf =
                commandOutputCacheEnabled &&
                commandCacheEligible[commandIndex] &&
                command.stencilModifierCommandIndices.empty() &&
                command.stencilMaskCommandIndices.empty() &&
                command.differenceAlphaMaskGroupCommandIndices.empty() &&
                !command.implicitVisibleStencilGroup;
            detail::PlayerRuntime::EmoteCommandOutputCacheEntry
                *commandCacheEntry = nullptr;
            bool reusedPristineLeaf = false;
            if(commandOutputCacheEnabled) {
                auto [it, inserted] =
                    _runtime->emoteCommandOutputCache.try_emplace(
                        commandCacheKeys[commandIndex]);
                (void)inserted;
                commandCacheEntry = &it->second;
                commandCacheEntry->lastUseGeneration =
                    commandCacheGeneration;

                // Even on a signature miss the retained objects are useful
                // scratch buffers; prepareLayerForRender overwrites them
                // before the new output is exposed.
                command.leafLayer = commandCacheEntry->leafLayer;
                command.composedLayer =
                    commandCacheEntry->composedLayer;
                command.maskLayer = commandCacheEntry->maskLayer;
                command.unionMaskLayer =
                    commandCacheEntry->unionMaskLayer;

                if(commandCacheEligible[commandIndex] &&
                   commandCacheEntry->outputValid &&
                   commandCacheEntry->outputSignature ==
                       commandOutputSignatures[commandIndex]) {
                    command.leafBuilt =
                        commandCacheEntry->leafBuilt;
                    command.composedBuilt =
                        commandCacheEntry->composedBuilt;
                    command.builtRect = command.clipRect;
                    iTJSDispatch2 *cachedOutputObject = nullptr;
                    if(command.composedBuilt &&
                       command.composedLayer.Type() == tvtObject) {
                        cachedOutputObject =
                            command.composedLayer.AsObjectNoAddRef();
                    } else if(command.leafBuilt &&
                              command.leafLayer.Type() == tvtObject) {
                        cachedOutputObject =
                            command.leafLayer.AsObjectNoAddRef();
                    }
                    if(cachedOutputObject &&
                       resolveNativeLayer(cachedOutputObject)) {
                        ++_runtime->emoteCommandOutputCacheHits;
                        if(profileEnabled) {
                            ++profileStats.bufferedOutputs;
                            ++profileStats.commandOutputCacheHits;
                        }
                        return true;
                    }
                    command.leafBuilt = false;
                    command.composedBuilt = false;
                    commandCacheEntry->outputValid = false;
                }

                if(canReusePristineLeaf &&
                   commandCacheEntry->leafValid &&
                   commandCacheEntry->leafSignature ==
                       commandLeafSignatures[commandIndex] &&
                   command.leafLayer.Type() == tvtObject &&
                   resolveNativeLayer(
                       command.leafLayer.AsObjectNoAddRef())) {
                    command.leafBuilt = true;
                    command.builtRect = command.clipRect;
                    reusedPristineLeaf = true;
                    ++_runtime->emoteCommandLeafCacheHits;
                    if(profileEnabled) {
                        ++profileStats.commandLeafCacheHits;
                    }
                } else {
                    command.leafBuilt = false;
                }
                command.composedBuilt = false;
            }

            auto rememberCommandOutput = [&]() {
                if(!commandCacheEntry) {
                    return;
                }
                commandCacheEntry->leafLayer = command.leafLayer;
                commandCacheEntry->composedLayer =
                    command.composedLayer;
                commandCacheEntry->maskLayer = command.maskLayer;
                commandCacheEntry->unionMaskLayer =
                    command.unionMaskLayer;
                commandCacheEntry->leafSignature =
                    commandLeafSignatures[commandIndex];
                commandCacheEntry->outputSignature =
                    commandOutputSignatures[commandIndex];
                commandCacheEntry->leafBuilt = command.leafBuilt;
                commandCacheEntry->composedBuilt =
                    command.composedBuilt;
                commandCacheEntry->leafValid =
                    canReusePristineLeaf && command.leafBuilt;
                commandCacheEntry->outputValid =
                    commandCacheEligible[commandIndex] &&
                    (command.leafBuilt || command.composedBuilt);
                commandCacheEntry->lastUseGeneration =
                    commandCacheGeneration;
            };

            if(profileEnabled) {
                ++profileStats.bufferedOutputs;
            }

            iTJSDispatch2 *leafLayerObject = ensureCommandLayer(command.leafLayer);
            auto *leafLayer = resolveNativeLayer(leafLayerObject);
            if(!leafLayerObject || !leafLayer) {
                if(logoTraceEnabled) {
                    detail::logoChainTraceCheck(
                        motionPath, "execute.workLayer", "0x6C7440",
                        _clampedEvalTime,
                        "leaf layer should resolve for buffered item path",
                        fmt::format("nodeIndex={} leafLayer={}",
                                    command.nodeIndex,
                                    static_cast<const void *>(leafLayer)),
                        false,
                        "sub_6C7440 could not allocate the per-item leaf layer");
                }
                return false;
            }

            if(!command.leafBuilt) {
                if(!renderCommandSourceToLayer(
                       command, leafLayerObject, leafLayer,
                       srcBmp, sourceRect,
                       "item.leaf.affineCopy")) {
                    return false;
                }
                if(LOGGER &&
                   shouldDebugTitleRender(motionPath, command.sourceKey) &&
                   markRenderDebugLogged(
                       fmt::format("leaf|{}|{}|{}", motionPath,
                                   command.nodeIndex,
                                   command.sourceKey))) {
                    LOGGER->info(
                        "motion render leaf: motion={} node={} source={} clip=[{},{},{},{}] opacity={} blend={} flags={} src=[{}] leaf=[{}]",
                        motionPath, command.nodeIndex, command.sourceKey,
                        command.clipRect[0], command.clipRect[1],
                        command.clipRect[2], command.clipRect[3],
                        command.opacity, command.blendMode,
                        command.itemFlags,
                        sampleBitmapStats(srcBmp.get()),
                        sampleBitmapStats(leafLayer->GetMainImage()));
                }
                command.leafBuilt = true;
            }
            command.builtRect = command.clipRect;

            bool hasBuiltChildren = false;
            bool hasBuiltModifiers = false;
            const bool usesImplicitVisibleStencil =
                command.groupOnly &&
                command.implicitVisibleStencilGroup;
            iTJSDispatch2 *implicitStencilLayerObject = nullptr;
            detail::PlayerRuntime::RenderCommand *implicitStencilCommand =
                nullptr;
            iTJSDispatch2 *composedLayerObject = nullptr;
            tTJSNI_BaseLayer *composedLayer = nullptr;

            for(const int childCommandIndex : command.childCommandIndices) {
                if(childCommandIndex < 0 ||
                   childCommandIndex >=
                       static_cast<int>(_runtime->renderCommands.size())) {
                    continue;
                }
                auto &child =
                    _runtime->renderCommands[childCommandIndex];
                hasBuiltChildren =
                    self(self, static_cast<size_t>(childCommandIndex)) ||
                    hasBuiltChildren;
            }

            for(const int modifierCommandIndex :
                    command.stencilModifierCommandIndices) {
                if(modifierCommandIndex < 0 ||
                   modifierCommandIndex >=
                       static_cast<int>(_runtime->renderCommands.size())) {
                    continue;
                }
                hasBuiltModifiers =
                    self(self, static_cast<size_t>(modifierCommandIndex)) ||
                    hasBuiltModifiers;
            }

            auto applyStencilModifiers =
                [&](iTJSDispatch2 *destinationLayerObject,
                    int destinationWorldLeft,
                    int destinationWorldTop) {
                    if(!destinationLayerObject || !hasBuiltModifiers) {
                        return;
                    }
                    for(const int modifierCommandIndex :
                            command.stencilModifierCommandIndices) {
                        if(modifierCommandIndex < 0 ||
                           modifierCommandIndex >= static_cast<int>(
                               _runtime->renderCommands.size())) {
                            continue;
                        }
                        auto &modifier =
                            _runtime->renderCommands[modifierCommandIndex];
                        auto *modifierLayerObject =
                            chooseCommandOutputLayerObject(modifier);
                        if(!modifierLayerObject) {
                            continue;
                        }
                        const int modifierWidth =
                            modifier.builtRect[2] - modifier.builtRect[0];
                        const int modifierHeight =
                            modifier.builtRect[3] - modifier.builtRect[1];
                        if(modifierWidth <= 0 || modifierHeight <= 0) {
                            continue;
                        }
                        applyMotionAlphaMaskLike_0x6AC4E4(
                            destinationLayerObject,
                            modifier.builtRect[0] - destinationWorldLeft,
                            modifier.builtRect[1] - destinationWorldTop,
                            modifierLayerObject, 0, 0,
                            modifierWidth, modifierHeight, 64,
                            playerStencilType, modifier.itemFlags & 3,
                            motionPath, _clampedEvalTime,
                            command.nodeIndex, modifier.nodeIndex);
                    }
                };

            if(!hasBuiltChildren) {
                applyStencilModifiers(
                    leafLayerObject, command.clipRect[0],
                    command.clipRect[1]);
                applyIndependentDifferenceAlphaMasks(
                    command, leafLayerObject,
                    command.clipRect[0], command.clipRect[1]);
                rememberCommandOutput();
                return true;
            }

            if(usesImplicitVisibleStencil) {
                for(const int childCommandIndex :
                        command.childCommandIndices) {
                    if(childCommandIndex < 0 ||
                       childCommandIndex >=
                           static_cast<int>(
                               _runtime->renderCommands.size())) {
                        continue;
                    }
                    auto &child =
                        _runtime->renderCommands[childCommandIndex];
                    if(!child.implicitVisibleStencilBase ||
                       child.implicitVisibleStencilGroupNodeIndex !=
                           command.nodeIndex ||
                       child.opacity <= 0) {
                        continue;
                    }
                    auto *candidateLayerObject =
                        chooseCommandOutputLayerObject(child);
                    auto *candidateLayer =
                        resolveNativeLayer(candidateLayerObject);
                    if(!candidateLayerObject || !candidateLayer ||
                       !candidateLayer->GetMainImage()) {
                        continue;
                    }
                    implicitStencilLayerObject = candidateLayerObject;
                    implicitStencilCommand = &child;
                    break;
                }
            }

            const bool isStandaloneDifferenceGroup =
                command.groupOnly &&
                command.stencilMaskNodeIndices.empty() &&
                (command.itemFlags & 7) == 6;
            if(!composedLayerObject) {
                // A freshly rendered leaf already has the exact dimensions
                // and transparent pixels required by the composite. Transfer
                // that scratch layer into the composed slot and ping-pong the
                // previous composed allocation into the leaf slot for the
                // next frame. This removes a full clear plus leaf->composed
                // copy for every animated group without sharing or skipping
                // any animation state.
                const bool canTransferFreshLeaf =
                    command.leafBuilt && !reusedPristineLeaf &&
                    !isStandaloneDifferenceGroup &&
                    command.leafLayer.Type() == tvtObject;
                if(canTransferFreshLeaf) {
                    std::swap(command.leafLayer,
                              command.composedLayer);
                    composedLayerObject =
                        command.composedLayer.AsObjectNoAddRef();
                    composedLayer =
                        resolveNativeLayer(composedLayerObject);
                    command.leafBuilt = false;
                } else {
                    composedLayerObject =
                        ensureCommandLayer(command.composedLayer);
                    composedLayer = resolveNativeLayer(composedLayerObject);
                    if(!composedLayerObject || !composedLayer) {
                        if(logoTraceEnabled) {
                            detail::logoChainTraceCheck(
                                motionPath, "execute.workLayer", "0x6C7440",
                                _clampedEvalTime,
                                "composed layer should resolve for parent item path",
                                fmt::format(
                                    "nodeIndex={} composedLayer={}",
                                    command.nodeIndex,
                                    static_cast<const void *>(composedLayer)),
                                false,
                                "sub_6C7440 could not allocate the composed output layer");
                        }
                        return false;
                    }

                    if(!prepareLayerForRender(
                           composedLayerObject, clipWidth, clipHeight,
                           0x00000000)) {
                        return false;
                    }
                    // The type-12 root's blank/32 source is an allocation
                    // placeholder. Its descendants contain the actual
                    // difference artwork, so do not seed the composed layer
                    // with that root.
                    if(command.leafBuilt &&
                       !isStandaloneDifferenceGroup) {
                        const auto localRect =
                            localRectFromCommand(command);
                        composedLayer->CopyRect(
                            0, 0, leafLayer->GetMainImage(), nullptr,
                            localRect);
                    }
                }
            }

            for(const int childCommandIndex : command.childCommandIndices) {
                if(childCommandIndex < 0 ||
                   childCommandIndex >=
                       static_cast<int>(_runtime->renderCommands.size())) {
                    continue;
                }
                auto &child = _runtime->renderCommands[childCommandIndex];
                auto *childOutputLayerObject =
                    chooseCommandOutputLayerObject(child);
                auto *childOutputLayer =
                    resolveNativeLayer(childOutputLayerObject);
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
                bool fusedImplicitStencilBlend = false;
                if(usesImplicitVisibleStencil &&
                   implicitStencilCommand &&
                   !child.implicitVisibleStencilBase) {
                    const int childWidth =
                        child.builtRect[2] - child.builtRect[0];
                    const int childHeight =
                        child.builtRect[3] - child.builtRect[1];
                    const int maskOriginX =
                        child.builtRect[0] -
                        implicitStencilCommand->builtRect[0];
                    const int maskOriginY =
                        child.builtRect[1] -
                        implicitStencilCommand->builtRect[1];
                    if(childBlendMode == omAlpha &&
                       (command.itemFlags & 3) == 1 &&
                       composedLayer->GetMainImage() &&
                       childOutputLayer->GetMainImage()) {
                        auto *implicitStencilLayer =
                            resolveNativeLayer(implicitStencilLayerObject);
                        if(implicitStencilLayer &&
                           implicitStencilLayer->GetMainImage()) {
                            const int dstOriginX =
                                child.builtRect[0] - command.clipRect[0];
                            const int dstOriginY =
                                child.builtRect[1] - command.clipRect[1];
                            const int maskImageWidth = static_cast<int>(
                                implicitStencilLayer->GetImageWidth());
                            const int maskImageHeight = static_cast<int>(
                                implicitStencilLayer->GetImageHeight());
                            const int localLeft = std::max(
                                {0, -dstOriginX, -maskOriginX});
                            const int localTop = std::max(
                                {0, -dstOriginY, -maskOriginY});
                            const int localRight = std::min(
                                {childWidth,
                                 clipWidth - dstOriginX,
                                 maskImageWidth - maskOriginX});
                            const int localBottom = std::min(
                                {childHeight,
                                 clipHeight - dstOriginY,
                                 maskImageHeight - maskOriginY});
                            if(localLeft >= localRight ||
                               localTop >= localBottom) {
                                // A positive stencil makes the child fully
                                // transparent when there is no overlap.
                                fusedImplicitStencilBlend = true;
                            } else {
                                const tTVPRect dstRect(
                                    dstOriginX + localLeft,
                                    dstOriginY + localTop,
                                    dstOriginX + localRight,
                                    dstOriginY + localBottom);
                                const tTVPRect srcRect(
                                    localLeft, localTop,
                                    localRight, localBottom);
                                const tTVPRect maskRect(
                                    maskOriginX + localLeft,
                                    maskOriginY + localTop,
                                    maskOriginX + localRight,
                                    maskOriginY + localBottom);
                                fusedImplicitStencilBlend =
                                    TVPGodotBlendAlphaDWithMask(
                                        composedLayer->GetMainImage(),
                                        childOutputLayer->GetMainImage(),
                                        implicitStencilLayer->GetMainImage(),
                                        dstRect, srcRect, maskRect,
                                        childOpacity,
                                        playerStencilType == 0);
                            }
                        }
                    }
                    if(!fusedImplicitStencilBlend) {
                        if(commandOutputCacheEnabled &&
                           childCommandIndex >= 0 &&
                           childCommandIndex < static_cast<int>(
                               commandCacheKeys.size())) {
                            // The compatibility path below mutates the child
                            // in-place. Never expose those masked pixels as a
                            // pristine cache hit on a later frame.
                            const auto cached =
                                _runtime->emoteCommandOutputCache.find(
                                    commandCacheKeys[
                                        static_cast<size_t>(
                                            childCommandIndex)]);
                            if(cached !=
                               _runtime->emoteCommandOutputCache.end()) {
                                cached->second.leafValid = false;
                                cached->second.outputValid = false;
                            }
                        }
                        applyMotionAlphaMaskLike_0x6AC4E4(
                            childOutputLayerObject, 0, 0,
                            implicitStencilLayerObject,
                            maskOriginX, maskOriginY,
                            childWidth, childHeight, 64,
                            playerStencilType, command.itemFlags & 3,
                            motionPath, _clampedEvalTime,
                            child.nodeIndex,
                            implicitStencilCommand->nodeIndex);
                    }
                }
                if(!fusedImplicitStencilBlend) {
                    composedLayer->OperateRect(
                        child.builtRect[0] - command.clipRect[0],
                        child.builtRect[1] - command.clipRect[1],
                        childOutputLayer->GetMainImage(),
                        childLocalRect,
                        childBlendMode,
                        childOpacity);
                }
            }
            applyStencilModifiers(
                composedLayerObject, command.clipRect[0],
                command.clipRect[1]);

            struct CompositeMaskSurface {
                const tTVPBaseTexture *bitmap = nullptr;
                iTJSDispatch2 *layerObject = nullptr;
                int worldLeft = 0;
                int worldTop = 0;
                int width = 0;
                int height = 0;
                int opacity = 255;
                int itemFlags = 0;
                int nodeIndex = -1;
            };
            std::vector<CompositeMaskSurface> compositeMaskSurfaces;
            compositeMaskSurfaces.reserve(
                command.stencilMaskCommandIndices.size());

            for(const int maskCommandIndex :
                    command.stencilMaskCommandIndices) {
                if(maskCommandIndex < 0 ||
                   maskCommandIndex >=
                       static_cast<int>(_runtime->renderCommands.size())) {
                    continue;
                }
                const bool maskOutputBuilt =
                    self(self, static_cast<size_t>(maskCommandIndex));
                if(!maskOutputBuilt) {
                    continue;
                }
                auto &mask = _runtime->renderCommands[maskCommandIndex];
                iTJSDispatch2 *maskLayerObject = nullptr;
                // sub_6C7440 selects item+324 for a composed mask (flag 4),
                // otherwise item+304 for the leaf mask.
                if((mask.itemFlags & 4) != 0 && mask.composedBuilt &&
                   mask.composedLayer.Type() == tvtObject) {
                    maskLayerObject = mask.composedLayer.AsObjectNoAddRef();
                } else if(mask.leafBuilt &&
                          mask.leafLayer.Type() == tvtObject) {
                    maskLayerObject = mask.leafLayer.AsObjectNoAddRef();
                } else {
                    maskLayerObject = chooseCommandOutputLayerObject(mask);
                }
                if(!maskLayerObject) {
                    continue;
                }
                auto *maskLayer = resolveNativeLayer(maskLayerObject);
                if(!maskLayer || !maskLayer->GetMainImage()) {
                    continue;
                }
                const int maskWidth =
                    mask.builtRect[2] - mask.builtRect[0];
                const int maskHeight =
                    mask.builtRect[3] - mask.builtRect[1];
                if(maskWidth <= 0 || maskHeight <= 0) {
                    continue;
                }
                compositeMaskSurfaces.push_back(CompositeMaskSurface{
                    maskLayer->GetMainImage(),
                    maskLayerObject,
                    mask.builtRect[0], mask.builtRect[1],
                    maskWidth, maskHeight,
                    std::clamp(mask.opacity, 0, 255),
                    mask.itemFlags,
                    mask.nodeIndex});
            }

            // A type-12 stencilComposite node carries the operation on the
            // group itself (normally 0x5). Native sub_6C2208 first uses that
            // full value to add the referenced layer images into an off-screen
            // alpha mask, then sub_6C7088 applies the resulting group image to
            // its ordinary children with flags&3 (crop for 0x5).
            //
            // Referenced mask items deliberately ignore their display
            // opacity. `add_mask` is authored with opacity zero so it is not
            // painted as colour, but its source alpha still participates in
            // the native mask build. Dropping it leaves the entire circular
            // iris visible instead of following the moving eyelid aperture.
            const int compositeMaskOperation = command.itemFlags & 3;
            if((command.itemFlags & 4) != 0 &&
               !compositeMaskSurfaces.empty() &&
               (compositeMaskOperation == 1 ||
                compositeMaskOperation == 2)) {
                const int dstWidth = clipWidth;
                const int dstHeight = clipHeight;
                const bool thresholdMaskMode = playerStencilType == 0;
                bool gpuMaskApplied = false;
                if(command.groupOnly &&
                   compositeMaskSurfaces.size() > 1) {
                    iTJSDispatch2 *maskScratchLayerObject =
                        ensureCommandLayer(command.maskLayer);
                    auto *maskScratchLayer =
                        resolveNativeLayer(maskScratchLayerObject);
                    if(maskScratchLayerObject && maskScratchLayer &&
                       prepareLayerForRender(maskScratchLayerObject,
                                             dstWidth, dstHeight,
                                             0x00000000)) {
                        std::vector<iTVPBaseBitmap *> maskBitmaps;
                        std::vector<tTVPRect> maskDstRects;
                        std::vector<tTVPRect> maskSrcRects;
                        maskBitmaps.reserve(compositeMaskSurfaces.size());
                        maskDstRects.reserve(compositeMaskSurfaces.size());
                        maskSrcRects.reserve(compositeMaskSurfaces.size());
                        for(const auto &surface : compositeMaskSurfaces) {
                            int dstLeft =
                                surface.worldLeft - command.clipRect[0];
                            int dstTop =
                                surface.worldTop - command.clipRect[1];
                            int srcLeft = 0;
                            int srcTop = 0;
                            int width = surface.width;
                            int height = surface.height;
                            if(dstLeft < 0) {
                                srcLeft -= dstLeft;
                                width += dstLeft;
                                dstLeft = 0;
                            }
                            if(dstTop < 0) {
                                srcTop -= dstTop;
                                height += dstTop;
                                dstTop = 0;
                            }
                            width = std::min(width, dstWidth - dstLeft);
                            height = std::min(height, dstHeight - dstTop);
                            width = std::min(width,
                                             surface.width - srcLeft);
                            height = std::min(height,
                                              surface.height - srcTop);
                            if(width <= 0 || height <= 0) {
                                continue;
                            }
                            maskBitmaps.push_back(
                                const_cast<iTVPBaseBitmap *>(
                                    static_cast<const iTVPBaseBitmap *>(
                                        surface.bitmap)));
                            maskDstRects.emplace_back(
                                dstLeft, dstTop,
                                dstLeft + width, dstTop + height);
                            maskSrcRects.emplace_back(
                                srcLeft, srcTop,
                                srcLeft + width, srcTop + height);
                        }
                        if(!maskBitmaps.empty()) {
                            gpuMaskApplied =
                                TVPGodotApplyAlphaUnionMask(
                                    composedLayer->GetMainImage(),
                                    maskScratchLayer->GetMainImage(),
                                    maskBitmaps.data(), maskDstRects.data(),
                                    maskSrcRects.data(), maskBitmaps.size(),
                                    thresholdMaskMode,
                                    compositeMaskOperation,
                                    dstWidth, dstHeight);
                        }
                    }
                }

                auto *dstBitmap = composedLayer->GetMainImage();
                if(!gpuMaskApplied && compositeMaskSurfaces.size() == 1) {
                    // With one input, native's op-5 mask build followed by
                    // op-1/op-2 application is algebraically identical to a
                    // single crop. Request the whole group rectangle so the
                    // helper also clears alpha outside the mask's bounds.
                    const auto &surface = compositeMaskSurfaces.front();
                    applyMotionAlphaMaskLike_0x6AC4E4(
                        composedLayerObject, 0, 0, surface.layerObject,
                        command.clipRect[0] - surface.worldLeft,
                        command.clipRect[1] - surface.worldTop,
                        dstWidth, dstHeight, 64, playerStencilType,
                        compositeMaskOperation, motionPath, _clampedEvalTime,
                        command.nodeIndex, surface.nodeIndex);
                } else if(!gpuMaskApplied) {
                    for(int y = 0; y < dstHeight; ++y) {
                        auto *dstRow = static_cast<std::uint8_t *>(
                            dstBitmap->GetScanLineForWrite(y));
                        const int worldY = command.clipRect[1] + y;
                        for(int x = 0; x < dstWidth; ++x) {
                            const int worldX = command.clipRect[0] + x;
                            int unionAlpha = 0;
                            for(const auto &surface : compositeMaskSurfaces) {
                                if(worldX < surface.worldLeft ||
                                   worldY < surface.worldTop ||
                                   worldX >= surface.worldLeft + surface.width ||
                                   worldY >= surface.worldTop + surface.height) {
                                    continue;
                                }
                                const int sourceX = worldX - surface.worldLeft;
                                const int sourceY = worldY - surface.worldTop;
                                if(sourceX >= static_cast<int>(surface.bitmap->GetWidth()) ||
                                   sourceY >= static_cast<int>(surface.bitmap->GetHeight())) {
                                    continue;
                                }
                                const auto *sourceRow =
                                    static_cast<const std::uint8_t *>(
                                        surface.bitmap->GetScanLine(sourceY));
                                const int sourceAlpha = static_cast<int>(
                                    sourceRow[sourceX * 4 + 3]);
                                if(thresholdMaskMode) {
                                    if(sourceAlpha >= 64) {
                                        unionAlpha = 255;
                                        break;
                                    }
                                } else {
                                    unionAlpha +=
                                        ((255 - unionAlpha) * sourceAlpha) / 255;
                                }
                            }

                            auto &dstAlpha = dstRow[x * 4 + 3];
                            if(compositeMaskOperation == 1) {
                                if(thresholdMaskMode) {
                                    if(unionAlpha < 64) dstAlpha = 0;
                                } else {
                                    dstAlpha = static_cast<std::uint8_t>(
                                        (static_cast<int>(dstAlpha) * unionAlpha) /
                                        255);
                                }
                            } else if(thresholdMaskMode) {
                                if(unionAlpha >= 64) dstAlpha = 0;
                            } else {
                                dstAlpha = static_cast<std::uint8_t>(
                                    (static_cast<int>(dstAlpha) *
                                     (255 - unionAlpha)) / 255);
                            }
                        }
                    }
                }

            } else {
                // Retain the native per-item path for non-composite stencil
                // chains.  These nodes author their own operation codes.
                for(size_t maskIndex = 0;
                    maskIndex < compositeMaskSurfaces.size(); ++maskIndex) {
                    const auto &surface = compositeMaskSurfaces[maskIndex];
                    applyMotionAlphaMaskLike_0x6AC4E4(
                        composedLayerObject,
                        surface.worldLeft - command.clipRect[0],
                        surface.worldTop - command.clipRect[1],
                        surface.layerObject, 0, 0,
                        surface.width, surface.height, 64,
                        playerStencilType, surface.itemFlags & 3,
                        motionPath, _clampedEvalTime,
                        command.nodeIndex, surface.nodeIndex);
                }
            }

            applyIndependentDifferenceAlphaMasks(
                command, composedLayerObject,
                command.clipRect[0], command.clipRect[1]);
            command.composedBuilt = true;
            rememberCommandOutput();
            return true;
        };

        std::vector<size_t> executionOrder;
        executionOrder.reserve(_runtime->renderCommands.size());
        for(size_t i = 0; i < _runtime->renderCommands.size(); ++i) {
            executionOrder.push_back(i);
        }
        if(motionPath.find("日々姫15a.psb") != std::string::npos) {
            // This Last Run gallery motion authors its full-canvas custom
            // background after the character root in PSB traversal order.
            // D3D renders that custom plane first, then the transparent
            // character composition over it. Preserve every internal
            // character/mask dependency and only move this independent
            // top-level background ahead of the root.
            const auto background = std::find_if(
                executionOrder.begin(), executionOrder.end(),
                [&](size_t index) {
                    const auto &command =
                        _runtime->renderCommands[index];
                    return !command.hasRenderParent &&
                           !command.alphaMaskOnly &&
                           command.nodeLabel == "■追加パーツ" &&
                           command.sourceKey == "src/#custom/10";
                });
            if(background != executionOrder.end()) {
                std::rotate(executionOrder.begin(), background,
                            std::next(background));
            }
        }

        for(const size_t i : executionOrder) {
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
            if(command.hasRenderParent || command.alphaMaskOnly) {
                detail::logoChainTraceLogf(
                    motionPath, "execute.skipChild", "0x6C7440",
                    _clampedEvalTime,
                    "nodeIndex={} parentNodeIndex={} alphaMaskOnly={} clipRect=[{},{},{},{}]",
                    command.nodeIndex, command.parentNodeIndex,
                    command.alphaMaskOnly ? 1 : 0,
                    command.clipRect[0], command.clipRect[1],
                    command.clipRect[2], command.clipRect[3]);
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
                    // IsOpaque is maintained exactly by the texture backend.
                    // A sampled alpha scan can miss sparse transparency and
                    // would let CopyRect erase layers already drawn below it.
                    srcBmp->IsOpaque();
                if(command.meshType == 0) {
                    tTVPRect destinationRect;
                    const bool canUseAxisAlignedStretch =
                        axisAlignedRectBoundsFromCorners(command.worldCorners,
                                                         0.0f, 0.0f,
                                                         destinationRect);
                    if(canUseAxisAlignedStretch) {
                        const bool sameSize =
                            destinationRect.get_width() ==
                                sourceRect.get_width() &&
                            destinationRect.get_height() ==
                                sourceRect.get_height();
                        if(sameSize && directCopyWithoutOpacity) {
                            branch = "direct.copyRect";
                            renderLayer->CopyRect(destinationRect.left,
                                                  destinationRect.top,
                                                  srcBmp.get(), nullptr,
                                                  sourceRect);
                        } else if(sameSize) {
                            branch = "direct.operateRect";
                            renderLayer->OperateRect(destinationRect.left,
                                                     destinationRect.top,
                                                     srcBmp.get(), sourceRect,
                                                     blendMode, opa);
                        } else if(directCopyWithoutOpacity) {
                            branch = "direct.stretchCopy";
                            renderLayer->StretchCopy(destinationRect,
                                                     srcBmp.get(), sourceRect,
                                                     stLinear, 0.0);
                        } else {
                            branch = "direct.operateStretch";
                            renderLayer->OperateStretch(
                                destinationRect, srcBmp.get(), sourceRect,
                                blendMode, opa, stLinear, 0.0);
                        }
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
        if(commandOutputCacheEnabled &&
           (commandCacheGeneration % 120u) == 0u) {
            auto &cache = _runtime->emoteCommandOutputCache;
            for(auto it = cache.begin(); it != cache.end();) {
                if(it->second.lastUseGeneration + 240u <
                   commandCacheGeneration) {
                    it = cache.erase(it);
                } else {
                    ++it;
                }
            }
            while(cache.size() > 512u) {
                auto oldest = cache.begin();
                for(auto it = std::next(cache.begin());
                    it != cache.end(); ++it) {
                    if(it->second.lastUseGeneration <
                       oldest->second.lastUseGeneration) {
                        oldest = it;
                    }
                }
                cache.erase(oldest);
            }
        }
        if(profileEnabled && LOGGER) {
            size_t materializedPreparedEntries = 0;
            for(const auto &entry : materializedKeysBySource) {
                materializedPreparedEntries += entry.second.size();
            }
            LOGGER->info(
                "motion render profile: motion={} frame={:.2f} target={} native={} commands={} signature={:016x} outputs=direct:{} buffered:{} cache=command:{}/{} base:{}/{} shared:{}/{} prepared:{}/{} entries=command:{} prepared:{} materialized:{} evictions:{} loads=storage:{} psb:{} tintBuilds={} us=total:{} base:{} prepared:{} tint:{} alloc:{} apply:{} psbMeta:{} psbDecode:{} psbConvert:{}",
                motionPath, _clampedEvalTime,
                static_cast<const void *>(renderLayerObject),
                static_cast<const void *>(renderLayer),
                _runtime->renderCommands.size(),
                renderCommandReuseSignature(_runtime->renderCommands),
                profileStats.directOutputs, profileStats.bufferedOutputs,
                profileStats.commandOutputCacheHits,
                profileStats.commandLeafCacheHits,
                profileStats.baseHits, profileStats.baseMisses,
                profileStats.sharedBitmapHits,
                profileStats.sharedBitmapMisses,
                profileStats.preparedHits, profileStats.preparedMisses,
                _runtime->emoteCommandOutputCache.size(),
                preparedSourceCache.size(), materializedPreparedEntries,
                profileStats.tintEvictions,
                profileStats.storageLoads, profileStats.psbLoads,
                profileStats.tintBuilds,
                motionRenderProfileNowUs() - profileStartUs,
                profileStats.baseResolveUs, profileStats.preparedResolveUs,
                profileStats.tintBuildUs, profileStats.tintAllocateUs,
                profileStats.tintApplyUs, profileStats.psbMetadataUs,
                profileStats.psbDecodeUs, profileStats.psbConvertUs);
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

        iTJSDispatch2 *renderTarget = nullptr;
        tTJSNI_BaseLayer *targetNativeLayer = resolveNativeLayer(targetLayerObject);
        if(!targetNativeLayer && fallbackOwner) {
            targetNativeLayer = resolveNativeLayer(fallbackOwner);
        }
        tTJSVariant treeOwnerVariant =
            resolveLayerTreeOwnerVariantFromLayer(targetNativeLayer);
        if(treeOwnerVariant.Type() == tvtObject &&
           treeOwnerVariant.AsObjectNoAddRef()) {
            renderTarget = ensureReusableLayerObjectWithOwnerVariant(
                sla->privateRenderTargetSlot(),
                treeOwnerVariant,
                targetLayerObject,
                static_cast<tTVPLayerType>(ltAlpha),
                true,
                sla->getAbsolute());
        }
        if(!renderTarget) {
            renderTarget = ensureReusableLayerObject(
                sla->privateRenderTargetSlot(),
                resolveLayerTreeOwnerObject(targetLayerObject),
                targetLayerObject,
                static_cast<tTVPLayerType>(ltAlpha),
                true,
                sla->getAbsolute());
        }
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
        adaptor->notePlayerDraw();
        // Ordinary D3DEmote rendering captures each completed draw batch into
        // its authored target layer. D3DAffineSourceMotion instead keeps
        // drawing to the adaptor without ever calling captureCanvas. Detect
        // that behavior and retain its completed surface in the layer tree.
        const bool retainD3DPresentation =
            adaptor->shouldRetainUncapturedPresentation();
        if(!retainD3DPresentation &&
           _runtime->d3dPresentationLayer.Type() == tvtObject) {
            if(auto *presentation = resolveNativeLayer(
                   _runtime->d3dPresentationLayer.AsObjectNoAddRef())) {
                presentation->SetVisible(false);
            }
            adaptor->setRetainedPresentationLayer(nullptr);
        }
        detail::logoChainTraceLogf(
            motionPath, "draw.d3d", "0x6D5B90", _clampedEvalTime,
            "adaptorSize={}x{} route=D3DAdaptor_renderFromPlayer",
            adaptor->getWidth(), adaptor->getHeight());

        ensureNodeTreeBuilt();
        // A freshly selected E-mote model is commonly built from draw(), after
        // the progress callback for that tick has already run.  The tree is
        // therefore dirty but has never executed the native updateLayers
        // pipeline that instantiates its nested Motion players.  Honor the
        // same dirty gates as the ordinary presentation renderer; otherwise
        // all_parts/全体構造 reaches prepareRenderItems with empty body/head
        // children and only its transparent layout nodes are submitted.
        const bool parentStateChanged =
            applyMotionParentRootStateForRender();
        if((parentStateChanged || _layersDirty || _emoteDirty) &&
           !_runtime->nodes.empty()) {
            updateLayers();
        }
        prepareRenderItems();
        applyPreparedRenderItemTranslateOffsets();
        adjustPreparedRenderItemsForYuzuPresentation(
            *_runtime, motionPath, adaptor->getWidth(),
            adaptor->getHeight());
        adjustPreparedRenderItemsForCenteredGameMotion(
            *_runtime, motionPath, adaptor->getWidth(),
            adaptor->getHeight(),
            resolveCenteredGameMotionResolution(
                _resolution, _tags, _metadata, motionPath));

        iTJSDispatch2 *windowObject = adaptor->getWindowObject();
        iTJSDispatch2 *primaryLayerObject =
            resolvePrimaryLayerObject(windowObject);
        if(!primaryLayerObject) {
            return false;
        }

        const std::size_t renderLayerIndex =
            retainD3DPresentation ? 0u
                                  : _runtime->nextD3DRenderLayer;
        auto &renderLayerSlot =
            _runtime->d3dRenderLayers[renderLayerIndex];
        if(!retainD3DPresentation) {
            _runtime->nextD3DRenderLayer =
                (_runtime->nextD3DRenderLayer + 1u) %
                _runtime->d3dRenderLayers.size();
        }
        iTJSDispatch2 *presentationLayerObject = nullptr;
        if(retainD3DPresentation) {
            iTJSDispatch2 *presentationParentObject =
                primaryLayerObject;
            if(auto *primary =
                   resolveNativeLayer(primaryLayerObject)) {
                for(tjs_uint index = 0;
                    index < primary->GetCount(); ++index) {
                    auto *candidate =
                        primary->GetChildren(
                            static_cast<tjs_int>(index));
                    if(candidate && candidate->GetOwnerNoAddRef() &&
                       candidate->GetName() == TJS_W("表-背景")) {
                        presentationParentObject =
                            candidate->GetOwnerNoAddRef();
                        break;
                    }
                }
            }
            const bool presentationCreated =
                _runtime->d3dPresentationLayer.Type() != tvtObject;
            presentationLayerObject = ensureReusableLayerObject(
                _runtime->d3dPresentationLayer, windowObject,
                presentationParentObject,
                static_cast<tTVPLayerType>(ltAlpha),
                true);
            auto *presentationLayer =
                resolveNativeLayer(presentationLayerObject);
            if(!presentationLayerObject || !presentationLayer) {
                return false;
            }
            presentationLayer->SetName(
                TJS_W("AetherKiriD3DPlayerSurface"));
            presentationLayer->SetEnabled(false);
            presentationLayer->SetHitType(htMask);
            presentationLayer->SetHitThreshold(256);
            if(presentationCreated) {
                // The parent layer owns the static CG/viewer pixels. Keep the
                // animated overlay above those pixels but below all authored
                // click-wait/message/viewer child layers.
                presentationLayer->BringToBack();
            }
            if(presentationCreated &&
               !prepareLayerForRender(
                   presentationLayerObject, adaptor->getWidth(),
                   adaptor->getHeight(), 0x00000000)) {
                return false;
            }
            // D3DAffineSourceMotion renders the complete 1280x720 scene, but
            // the surrounding KAG layer owns the gallery's left save button
            // and right motion-control list. A layer clip only constrains
            // drawing into its image; it does not clip that image during
            // composition. Make the layer itself the authored center preview
            // width and offset the full source image instead.
            const int previewLeft =
                std::min(128, adaptor->getWidth());
            const int previewRight =
                std::min(168, adaptor->getWidth() - previewLeft);
            const int previewWidth =
                std::max(1, adaptor->getWidth() -
                                previewLeft - previewRight);
            presentationLayer->SetPosition(previewLeft, 0);
            presentationLayer->SetSize(previewWidth,
                                       adaptor->getHeight());
            presentationLayer->SetImagePosition(-previewLeft, 0);
        }
        iTJSDispatch2 *renderLayerObject =
            ensureReusableLayerObject(renderLayerSlot,
                                      windowObject,
                                      primaryLayerObject,
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
        if(!executeLayerRenderCommands(renderLayerObject, true)) {
            adaptor->setRenderedLayer(nullptr);
            return false;
        }
        const char *motionDebug =
            std::getenv("AETHERKIRI_MOTION_DEBUG");
        if(LOGGER && motionDebug && *motionDebug &&
           std::strcmp(motionDebug, "0") != 0 &&
           markRenderDebugLogged("d3d-completed-frame|" + motionPath)) {
            auto *debugLayer = resolveNativeLayer(renderLayerObject);
            tTVPRect visibleBounds;
            const bool hasVisibleBounds =
                debugLayer && bitmapVisibleBounds(
                                  debugLayer->GetMainImage(), visibleBounds);
            LOGGER->info(
                "motion d3d completed frame: motion={} retained={} prepared={} commands={} layer=[{}] pixels=[{}] visibleBounds={}",
                motionPath, retainD3DPresentation ? 1 : 0,
                _runtime->preparedRenderItems.size(),
                _runtime->renderCommands.size(),
                describeLayerForDebug(debugLayer),
                debugLayer
                    ? sampleBitmapStats(debugLayer->GetMainImage())
                    : std::string("none"),
                hasVisibleBounds
                    ? fmt::format("[{},{},{},{}]", visibleBounds.left,
                                  visibleBounds.top, visibleBounds.right,
                                  visibleBounds.bottom)
                    : std::string("none"));
        }

        // captureCanvas retains this rendered layer and copies it directly to
        // the caller's destination. On the Godot backend that remains an
        // ordered GPU copy, avoiding the old full-canvas GPU readback, CPU
        // memcpy, and upload on every E-mote frame.
        tTJSNI_BaseLayer *layer = nullptr;
        if(TJS_FAILED(renderLayerObject->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&layer))) || !layer) {
            adaptor->setRenderedLayer(nullptr);
            return false;
        }

        const int layerW = static_cast<int>(layer->GetImageWidth());
        const int layerH = static_cast<int>(layer->GetImageHeight());
        if(layerW <= 0 || layerH <= 0) {
            adaptor->setRenderedLayer(nullptr);
            return false;
        }
        // Resize adaptor buffer if needed
        if(adaptor->getWidth() != layerW ||
           adaptor->getHeight() != layerH) {
            adaptor->setSize(layerW, layerH);
        }
        if(retainD3DPresentation) {
            auto *presentationLayer =
                resolveNativeLayer(presentationLayerObject);
            if(!presentationLayer) {
                adaptor->setRenderedLayer(nullptr);
                return false;
            }
            presentationLayer->AssignMotionImages(layer);
            adaptor->setRetainedPresentationLayer(
                presentationLayerObject);
            adaptor->setRenderedLayer(presentationLayerObject);
        } else {
            adaptor->setRenderedLayer(renderLayerObject);
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
        if(_runtime->yuzuSdPresentationRetired &&
           isYuzuSdPresentationMotionPath(motionPath)) {
            return true;
        }

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
        // AffineSourceMotion has no intrinsic bitmap size: its TJS
        // setSizeToImageSize() implementation is deliberately a no-op.  The
        // temporary AffineLayer consequently remains at KiriKiri's 32x32
        // placeholder size even though E-mote supplies coordinates in the
        // main-window space.  Native motionplayer renders that dynamic source
        // against the draw-device canvas.  Do the same when the affine
        // translation clearly lies outside the placeholder; otherwise every
        // character item is clipped before its PSB bitmap is resolved.
        int mainCanvasWidth = 0;
        int mainCanvasHeight = 0;
        const bool hasMainCanvas =
            _runtime->isEmoteMode &&
            queryMainWindowCanvasSize(mainCanvasWidth, mainCanvasHeight);
        const auto &drawAffine = _runtime->drawAffineMatrix;
        const bool dynamicEmotePlaceholder =
            hasMainCanvas &&
            mainCanvasWidth > canvasWidth &&
            mainCanvasHeight > canvasHeight &&
            canvasWidth <= 64 && canvasHeight <= 64 &&
            (std::fabs(drawAffine[4]) >= canvasWidth ||
             std::fabs(drawAffine[5]) >= canvasHeight);
        if(dynamicEmotePlaceholder) {
            canvasWidth = mainCanvasWidth;
            canvasHeight = mainCanvasHeight;
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
        const bool centeredGamePresentation =
            !yuzuTitlePresentation && !yuzuLogoPresentation &&
            isCenteredGameMotion(motionPath);
        if(yuzuTitlePresentation) {
            int visibleCanvasWidth = 0;
            int visibleCanvasHeight = 0;
            if(_runtime->activeMotion &&
               _runtime->activeMotion->width > 0 &&
               _runtime->activeMotion->height > 0) {
                canvasWidth = static_cast<int>(_runtime->activeMotion->width);
                canvasHeight = static_cast<int>(_runtime->activeMotion->height);
            } else if(queryMainWindowCanvasSize(visibleCanvasWidth,
                                                visibleCanvasHeight)) {
                canvasWidth = visibleCanvasWidth;
                canvasHeight = visibleCanvasHeight;
            }
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
            presentationChild = findStartupLogoPresentationLayer(
                finalNativeLayer, canvasWidth, canvasHeight);
            if(!presentationChild) {
                presentationChild = findMotionPresentationChild(
                    finalNativeLayer, canvasWidth, canvasHeight,
                    allowTransientPresentationLayer);
            }
        } else {
            presentationChild = findCgViewMotionPresentationChild(
                finalNativeLayer, canvasWidth, canvasHeight);
            if(!presentationChild) {
                presentationChild = findGenericMotionPresentationChild(
                    finalNativeLayer, canvasWidth, canvasHeight);
            }
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
                   markRenderDebugLogged(
                       "presentation-target:" + motionPath + ":" +
                       _runtime->lastExplicitTimelineLabel)) {
                    LOGGER->info(
                        "motion presentation target: motion={} label={} source=[{}] target=[{}] canvas={}x{} transientAllowed={}",
                        motionPath, _runtime->lastExplicitTimelineLabel,
                        describeLayerForDebug(resolveNativeLayer(resolvedLayerObject)),
                        describeLayerForDebug(finalNativeLayer),
                        canvasWidth, canvasHeight,
                        allowTransientPresentationLayer ? 1 : 0);
                }
            }
        }
        if(!presentationChild && centeredGamePresentation) {
            if(auto *holdTargetObject = findCenteredPresentationHoldRenderTarget(
                   motionPath, finalNativeLayer)) {
                if(auto *holdTargetLayer = resolveNativeLayer(holdTargetObject)) {
                    finalLayerObject = holdTargetObject;
                    finalNativeLayer = holdTargetLayer;
                    detail::logoChainTraceLogf(
                        motionPath, "draw.layer.centeredHoldTarget",
                        "AetherKiri centered motion adaptor", _clampedEvalTime,
                        "source=[{}] target=[{}] canvas={}x{}",
                        describeLayerForDebug(resolveNativeLayer(resolvedLayerObject)),
                        describeLayerForDebug(finalNativeLayer),
                        canvasWidth, canvasHeight);
                    if(LOGGER && shouldDebugTitleRender(motionPath)) {
                        static std::unordered_map<std::string, int>
                            holdTargetLogCountByMotion;
                        auto &logCount = holdTargetLogCountByMotion[motionPath];
                        if(logCount < 8) {
                            ++logCount;
                            LOGGER->info(
                                "motion centered hold render target: motion={} source=[{}] target=[{}] count={}",
                                motionPath,
                                describeLayerForDebug(
                                    resolveNativeLayer(resolvedLayerObject)),
                                describeLayerForDebug(finalNativeLayer),
                                logCount);
                        }
                    }
                }
            }
        }
        if(centeredGamePresentation) {
            configureCenteredGameMotionPresentationHitPassthrough(
                finalNativeLayer);
            placeCenteredPresentationBelowMessageUi(
                finalNativeLayer, "target-select");
        }
        ensureNodeTreeBuilt();
        const bool parentStateChanged = applyMotionParentRootStateForRender();
        if((parentStateChanged || _layersDirty || _emoteDirty) &&
           !_runtime->nodes.empty()) {
            updateLayers();
        }
        prepareRenderItems();
        if(!skipUpdate && _runtime->preparedRenderItems.empty()) {
            const bool hasMotionChildNode = std::any_of(
                _runtime->nodes.begin(), _runtime->nodes.end(),
                [](const motion::detail::MotionNode &node) {
                    return node.nodeType == 3;
                });
            if(hasMotionChildNode) {
                updateLayers();
                prepareRenderItems();
                if(LOGGER && shouldDebugTitleRender(motionPath) &&
                   markRenderDebugLogged("presentation-prerender-update:" +
                                         motionPath)) {
                    LOGGER->info(
                        "motion presentation prerender update: motion={} preparedItems={}",
                        motionPath, _runtime->preparedRenderItems.size());
                }
            }
        }
        applyPreparedRenderItemTranslateOffsets();
        const bool cgViewScriptedPresentation =
            layerBelongsToCgViewPresentation(finalNativeLayer);
        if(cgViewScriptedPresentation) {
            if(LOGGER && shouldDebugTitleRender(motionPath) &&
               markRenderDebugLogged("yuzu-presentation-skip-cg-view:" +
                                     motionPath)) {
                LOGGER->info(
                    "motion yuzu presentation adjustment skipped: motion={} reason=cg_view_scripted_affine target=[{}]",
                    motionPath, describeLayerForDebug(finalNativeLayer));
            }
        } else {
            adjustPreparedRenderItemsForYuzuPresentation(
                *_runtime, motionPath, canvasWidth, canvasHeight);
        }
        adjustPreparedRenderItemsForCenteredGameMotion(
            *_runtime, motionPath, canvasWidth, canvasHeight,
            resolveCenteredGameMotionResolution(_resolution, _tags, _metadata,
                                                motionPath),
            finalNativeLayer);
        if(yuzuTitlePresentation &&
           !prepareMotionPresentationLayerForRender(
               finalNativeLayer, canvasWidth, canvasHeight)) {
            return false;
        }
        if(yuzuTitlePresentation) {
            logYuzuTitlePreparedSummary(*_runtime, motionPath, _frameTickCount);
        }
        const bool yuzuTitleHasRenderableFrame =
            yuzuTitlePresentation &&
            hasYuzuTitleRenderablePresentationFrame(*_runtime);
        const bool yuzuTitleHasStableFrame =
            yuzuTitlePresentation &&
            hasYuzuTitleStablePresentationFrame(*_runtime);
        const bool yuzuTitleHasOpaqueCanvasBaseFrame =
            yuzuTitlePresentation &&
            hasYuzuTitleOpaqueCanvasBaseFrame(
                *_runtime, canvasWidth, canvasHeight);
        const bool yuzuTitleHasOpaqueFinalOverlayFrame =
            yuzuTitlePresentation &&
            hasYuzuTitleOpaqueFinalOverlayFrame(*_runtime);
        if(yuzuTitlePresentation && !yuzuTitleHasRenderableFrame) {
            const bool restoredHeldFrame =
                restoreYuzuTitlePresentationHoldFrame(
                    finalNativeLayer, motionPath, canvasWidth, canvasHeight);
            if(LOGGER && shouldDebugTitleRender(motionPath) &&
               markRenderDebugLogged("presentation-skip-empty:" + motionPath)) {
                LOGGER->info(
                    "motion presentation skip empty title frame: motion={} restored={} target=[{}]",
                    motionPath, restoredHeldFrame ? 1 : 0,
                    describeLayerForDebug(finalNativeLayer));
            }
            _runtime->clearPresentationRenderReuse();
            invalidateGlobalPresentationRenderTarget(finalLayerObject);
            _runtime->lastCanvas =
                tTJSVariant(resolvedLayerObject, resolvedLayerObject);
            return true;
        }
        if(yuzuTitlePresentation &&
           _runtime->yuzuTitleFinalFrameRendered &&
           !yuzuTitleHasOpaqueCanvasBaseFrame) {
            const bool restoredHeldFrame =
                restoreYuzuTitlePresentationHoldFrame(
                    finalNativeLayer, motionPath, canvasWidth, canvasHeight);
            if(LOGGER && shouldDebugTitleRender(motionPath) &&
               markRenderDebugLogged("presentation-skip-tail:" + motionPath)) {
                LOGGER->info(
                    "motion presentation skip title tail frame: motion={} restored={} target=[{}]",
                    motionPath, restoredHeldFrame ? 1 : 0,
                    describeLayerForDebug(finalNativeLayer));
            }
            _runtime->clearPresentationRenderReuse();
            invalidateGlobalPresentationRenderTarget(finalLayerObject);
            _runtime->lastCanvas =
                tTJSVariant(resolvedLayerObject, resolvedLayerObject);
            return true;
        }

        iTJSDispatch2 *renderLayerObject = finalLayerObject;
        const bool useCgViewAffineRenderChild =
            layerIsCgViewAffineSurface(finalNativeLayer) &&
            !yuzuTitlePresentation && !yuzuLogoPresentation;
        bool useYuzuSdEventRenderChild =
            centeredGamePresentation &&
            isYuzuSdPreviewMotionPath(motionPath) &&
            centeredGameMotionPresentationLayerShouldPassHit(finalNativeLayer) &&
            !finalNativeLayer->GetHasImage();
        if(useYuzuSdEventRenderChild) {
            auto *treeOwner = resolveLayerTreeOwnerObject(finalLayerObject);
            if(!treeOwner) {
                treeOwner = resolveLayerTreeOwnerObject(resolvedLayerObject);
            }
            tTJSVariant treeOwnerVariant =
                resolveLayerTreeOwnerVariantFromLayer(finalNativeLayer);
            if(treeOwnerVariant.Type() != tvtObject && treeOwner) {
                treeOwnerVariant = tTJSVariant(treeOwner, treeOwner);
            }
            if(auto *existingSurface =
                   findYuzuSdEventRenderChildLayerObject(finalNativeLayer)) {
                auto *currentSurface =
                    _runtime->internalRenderLayer.Type() == tvtObject
                        ? _runtime->internalRenderLayer.AsObjectNoAddRef()
                        : nullptr;
                if(existingSurface != currentSurface) {
                    auto *existingNative = resolveNativeLayer(existingSurface);
                    detachYuzuSdEventRenderSurfaceChildren(existingNative);
                    _runtime->internalRenderLayer =
                        tTJSVariant(existingSurface, existingSurface);
                    _runtime->clearPresentationRenderReuse();
                    invalidateGlobalPresentationRenderTarget(existingSurface);
                }
            }
            renderLayerObject = ensureReusableLayerObjectWithOwnerVariant(
                _runtime->internalRenderLayer,
                treeOwnerVariant,
                finalLayerObject,
                static_cast<tTVPLayerType>(ltAlpha),
                true,
                false);
            if(!renderLayerObject) {
                renderLayerObject = ensureReusableLayerObject(
                    _runtime->internalRenderLayer,
                    treeOwner,
                    finalLayerObject,
                    static_cast<tTVPLayerType>(ltAlpha),
                    true,
                    false);
            }
            auto *renderLayer = resolveNativeLayer(renderLayerObject);
            if(renderLayer) {
                configureYuzuSdEventRenderChildLayer(renderLayer,
                                                      finalNativeLayer,
                                                      canvasWidth,
                                                      canvasHeight);
                finalLayerObject = renderLayerObject;
                finalNativeLayer = renderLayer;
                if(LOGGER && shouldDebugTitleRender(motionPath)) {
                    LOGGER->info(
                        "motion yuzu sd event render surface: motion={} label={} parent=[{}] render=[{}]",
                        motionPath, _runtime->lastExplicitTimelineLabel,
                        describeLayerForDebug(renderLayer->GetParent()),
                        describeLayerForDebug(renderLayer));
                }
            } else {
                useYuzuSdEventRenderChild = false;
                renderLayerObject = finalLayerObject;
            }
        } else if(useCgViewAffineRenderChild) {
            iTJSDispatch2 *renderParentObject =
                resolveCgViewAffineRenderParentObject(finalNativeLayer);
            if(!renderParentObject) {
                renderParentObject = finalLayerObject;
            }
            auto *treeOwner = resolveLayerTreeOwnerObject(finalLayerObject);
            if(!treeOwner) {
                treeOwner = resolveLayerTreeOwnerObject(resolvedLayerObject);
            }
            if(!treeOwner && renderParentObject) {
                treeOwner = resolveLayerTreeOwnerObject(renderParentObject);
            }
            tTJSVariant treeOwnerVariant =
                resolveLayerTreeOwnerVariantFromLayer(finalNativeLayer);
            if(treeOwnerVariant.Type() != tvtObject && treeOwner) {
                treeOwnerVariant = tTJSVariant(treeOwner, treeOwner);
            }
            if(auto *existingSurface = findCgViewRenderChildLayerObject(
                   resolveNativeLayer(renderParentObject))) {
                auto *currentSurface =
                    _runtime->internalRenderLayer.Type() == tvtObject
                        ? _runtime->internalRenderLayer.AsObjectNoAddRef()
                        : nullptr;
                if(existingSurface != currentSurface) {
                    auto *existingNative = resolveNativeLayer(existingSurface);
                    detachYuzuSdEventRenderSurfaceChildren(existingNative);
                    _runtime->internalRenderLayer =
                        tTJSVariant(existingSurface, existingSurface);
                    _runtime->clearPresentationRenderReuse();
                    invalidateGlobalPresentationRenderTarget(existingSurface);
                }
            }
            renderLayerObject = ensureReusableLayerObjectWithOwnerVariant(
                _runtime->internalRenderLayer,
                treeOwnerVariant,
                renderParentObject,
                static_cast<tTVPLayerType>(ltAlpha),
                true,
                false);
            if(auto *renderLayer = resolveNativeLayer(renderLayerObject)) {
                configureCgViewAffineRenderChildLayer(
                    renderLayer, finalNativeLayer,
                    resolveNativeLayer(renderParentObject));
                if(LOGGER && shouldDebugTitleRender(motionPath) &&
                   markRenderDebugLogged("cg-view-motion-surface:" + motionPath)) {
                    LOGGER->info(
                        "motion cg view render surface: motion={} affine=[{}] parent=[{}] render=[{}]",
                        motionPath, describeLayerForDebug(finalNativeLayer),
                        describeLayerForDebug(resolveNativeLayer(renderParentObject)),
                        describeLayerForDebug(renderLayer));
                }
            }
        }

        const bool usingPresentationTarget =
            finalLayerObject && finalLayerObject != resolvedLayerObject;
        const bool bypassInternalAssignImages =
            (yuzuTitlePresentation || useYuzuSdEventRenderChild) &&
            _needsInternalAssignImages && !skipUpdate;

        if(!useYuzuSdEventRenderChild && !useCgViewAffineRenderChild &&
           _needsInternalAssignImages && !skipUpdate &&
           !bypassInternalAssignImages) {
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
        if(bypassInternalAssignImages && LOGGER &&
           shouldDebugTitleRender(motionPath) &&
           markRenderDebugLogged("presentation-direct-target:" + motionPath +
                                 ":" +
                                 _runtime->lastExplicitTimelineLabel)) {
            LOGGER->info(
                "motion presentation direct target: motion={} target=[{}]",
                motionPath, describeLayerForDebug(finalNativeLayer));
        }
        buildRenderCommands(canvasWidth, canvasHeight);
        const bool clearGenericPresentationLayer =
            usingPresentationTarget && renderLayerObject == finalLayerObject &&
            !yuzuTitlePresentation && !yuzuLogoPresentation;
        const bool compositeOverPreviousCenteredFrame =
            clearGenericPresentationLayer &&
            centeredGameMotionFrameNeedsPreviousComposite(
                *_runtime, motionPath, finalNativeLayer);
        if(clearGenericPresentationLayer && _runtime->renderCommands.empty()) {
            if(centeredGamePresentation &&
               restoreCenteredPresentationHoldFrame(
                   finalNativeLayer, motionPath, canvasWidth, canvasHeight,
                   true)) {
                _runtime->lastCanvas =
                    tTJSVariant(resolvedLayerObject, resolvedLayerObject);
                return true;
            }
            if(LOGGER && shouldDebugTitleRender(motionPath) &&
               markRenderDebugLogged("presentation-skip-empty-generic:" +
                                     motionPath)) {
                std::ostringstream preparedSummary;
                const size_t itemLimit =
                    std::min<size_t>(_runtime->preparedRenderItems.size(), 16);
                for(size_t i = 0; i < itemLimit; ++i) {
                    const auto &item = _runtime->preparedRenderItems[i];
                    if(i != 0) {
                        preparedSummary << ";";
                    }
                    preparedSummary << i << ":node=" << item.nodeIndex
                                    << ":label=" << item.nodeLabel
                                    << ":src="
                                    << (item.sourceKey.empty()
                                            ? std::string("<none>")
                                            : item.sourceKey)
                                    << ":draw=" << (item.drawFlag ? 1 : 0)
                                    << ":skip=" << (item.skipFlag0 ? 1 : 0)
                                    << "/" << (item.skipFlag1 ? 1 : 0)
                                    << ":own=" << (item.hasOwnSource ? 1 : 0)
                                    << ":group=" << (item.groupOnly ? 1 : 0)
                                    << ":opa=" << item.opacity
                                    << ":parent="
                                    << item.visibleAncestorIndex
                                    << ":paint=[" << item.paintBox[0] << ","
                                    << item.paintBox[1] << ","
                                    << item.paintBox[2] << ","
                                    << item.paintBox[3] << "]";
                }
                LOGGER->info(
                    "motion presentation skip empty generic frame: motion={} target=[{}] preparedItems={} prepared=[{}]",
                    motionPath, describeLayerForDebug(finalNativeLayer),
                    _runtime->preparedRenderItems.size(),
                    preparedSummary.str());
            }
            _runtime->lastCanvas =
                tTJSVariant(resolvedLayerObject, resolvedLayerObject);
            return true;
        }
        if(clearGenericPresentationLayer &&
           !compositeOverPreviousCenteredFrame &&
           shouldPreservePreviousGenericPresentationFrame(
               *_runtime, finalNativeLayer, canvasWidth, canvasHeight)) {
            _runtime->lastCanvas =
                tTJSVariant(resolvedLayerObject, resolvedLayerObject);
            return true;
        }
        if(compositeOverPreviousCenteredFrame && LOGGER &&
           shouldDebugTitleRender(motionPath) &&
           markRenderDebugLogged("presentation-delta-composite:" +
                                 motionPath)) {
            LOGGER->info(
                "motion presentation delta composite: motion={} target=[{}] commands={}",
                motionPath, describeLayerForDebug(finalNativeLayer),
                _runtime->renderCommands.size());
        }
        const auto presentationCommandSignature =
            renderCommandReuseSignature(_runtime->renderCommands);
        const bool canReuseEmoteRender =
            _runtime->isEmoteMode &&
            renderLayerObject == finalLayerObject &&
            renderLayerObject != nullptr &&
            !usingPresentationTarget &&
            !centeredGamePresentation &&
            !yuzuTitlePresentation &&
            !yuzuLogoPresentation &&
            !_runtime->renderCommands.empty();
        if(canReuseEmoteRender) {
            const auto &entry = _runtime->emoteRenderFrameCache;
            const bool cacheIdentityMatches =
                entry.bitmap &&
                entry.motion == motionPath &&
                entry.canvasWidth == canvasWidth &&
                entry.canvasHeight == canvasHeight &&
                std::fabs(entry.frame - _clampedEvalTime) < 0.0001 &&
                entry.bitmap->GetWidth() == canvasWidth &&
                entry.bitmap->GetHeight() == canvasHeight;
            const bool exactCacheMatch =
                cacheIdentityMatches &&
                entry.commandSignature == presentationCommandSignature;
            auto *renderLayer = exactCacheMatch
                ? resolveNativeLayer(renderLayerObject)
                : nullptr;
            auto *renderImage =
                renderLayer ? renderLayer->GetMainImage() : nullptr;
            if(renderImage &&
               renderImage->GetWidth() == canvasWidth &&
               renderImage->GetHeight() == canvasHeight) {
                renderImage->CopyRect(
                    0, 0, entry.bitmap.get(),
                    tTVPRect(0, 0, canvasWidth, canvasHeight));
                ++_runtime->emoteRenderFrameReuseSkips;
                // A full CopyRect aliases the cached Godot texture.  The TJS
                // D3DEmote path immediately assignImages() it to the actual
                // character layer; forcing Update() on the invisible shared
                // work layer dirties the whole window even when that
                // character already references this exact cached texture.
                if(motionRenderProfileEnabled() && LOGGER) {
                    LOGGER->info(
                        "motion emote render reuse: motion={} frame={:.2f} target={} canvas={}x{} commands={} signature={:016x} cached_signature={:016x} skips={} reason={} age_us={}",
                        motionPath, _clampedEvalTime,
                        static_cast<const void *>(renderLayerObject),
                        canvasWidth, canvasHeight,
                        _runtime->renderCommands.size(),
                        presentationCommandSignature,
                        entry.commandSignature,
                        _runtime->emoteRenderFrameReuseSkips,
                        "exact",
                        motionRenderProfileNowUs() >= entry.storedUs
                            ? motionRenderProfileNowUs() - entry.storedUs
                            : 0);
                }
                _runtime->lastCanvas =
                    tTJSVariant(resolvedLayerObject, resolvedLayerObject);
                return true;
            }
        }
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
                // AffineSourceMotion's D3D branch clears each character target
                // but renders every character through one shared
                // _motionWorkLayer before assignImages(). Clear the actual
                // E-mote render target as well, otherwise a later affine
                // scale/position change leaves the previous character frame
                // in that shared work surface.
                if(_runtime->isEmoteMode) {
                    if(!prepareLayerForRender(
                           finalLayerObject, canvasWidth, canvasHeight,
                           targetLayer->GetNeutralColor())) {
                        _runtime->invalidatePresentationRenderTarget(
                            renderLayerObject);
                        invalidateGlobalPresentationRenderTarget(
                            renderLayerObject);
                        return false;
                    }
                } else if(usingPresentationTarget) {
                    if(!prepareMotionPresentationLayerForRender(
                           targetLayer, canvasWidth, canvasHeight)) {
                        _runtime->invalidatePresentationRenderTarget(renderLayerObject);
                        invalidateGlobalPresentationRenderTarget(renderLayerObject);
                        return false;
                    }
                    if(centeredGamePresentation) {
                        placeCenteredPresentationBelowMessageUi(
                            targetLayer, "prepare-target");
                    }
                    if(clearGenericPresentationLayer &&
                       !compositeOverPreviousCenteredFrame) {
                        clearMotionPresentationLayer(targetLayer, canvasWidth,
                                                     canvasHeight);
                    }
            } else {
                if(targetLayer->GetWidth() != canvasWidth ||
                   targetLayer->GetHeight() != canvasHeight) {
                    targetLayer->SetSize(canvasWidth, canvasHeight);
                }
                // Auto-progress can retain either the dedicated CG child or
                // KAG's ev/sd transition surface as its next target. Both
                // direct routes bypass the generic presentation clear above,
                // so moving SD layers otherwise accumulate every previous
                // position as ghost images.
                const auto directYuzuSdLayerName =
                    isYuzuSdPresentationMotionPath(motionPath)
                        ? yuzuSdPresentationTargetLayerName(finalLayerObject)
                        : std::string{};
                const bool directYuzuSdTransitionTarget =
                    _autoProgressRendering &&
                    (directYuzuSdLayerName == "ev" ||
                     directYuzuSdLayerName == "trans_ev" ||
                     directYuzuSdLayerName == "sd" ||
                     directYuzuSdLayerName == "trans_sd");
                if(targetLayer->GetName().AsStdString() ==
                       "AetherKiriCgViewMotionSurface" ||
                   directYuzuSdTransitionTarget) {
                    clearMotionPresentationLayer(targetLayer, canvasWidth,
                                                 canvasHeight);
                }
            }
        } else {
            _runtime->invalidatePresentationRenderTarget(renderLayerObject);
            invalidateGlobalPresentationRenderTarget(renderLayerObject);
            return false;
        }

        // Title motions frequently provide a full composite once, followed by
        // delta-only flash/logo frames. Rebuild each delta frame over the last
        // stable composite so transient full-screen effects cannot accumulate.
        if(yuzuTitlePresentation && !yuzuTitleHasOpaqueCanvasBaseFrame &&
           findYuzuTitlePresentationHoldFrame(finalNativeLayer, motionPath)) {
            restoreYuzuTitlePresentationHoldFrame(
                finalNativeLayer, motionPath, canvasWidth, canvasHeight);
        }

        if(!executeLayerRenderCommands(renderLayerObject, true)) {
            if(centeredGamePresentation &&
               restoreCenteredPresentationHoldFrame(
                   finalNativeLayer, motionPath, canvasWidth, canvasHeight,
                   true)) {
                _runtime->lastCanvas =
                    tTJSVariant(resolvedLayerObject, resolvedLayerObject);
                return true;
            }
            _runtime->invalidatePresentationRenderTarget(renderLayerObject);
            invalidateGlobalPresentationRenderTarget(renderLayerObject);
            return false;
        }
        bool capturedYuzuTitleFrame = false;
        if(yuzuTitlePresentation && renderLayerObject) {
            const bool hadHeldFrame =
                findYuzuTitlePresentationHoldFrame(
                    finalNativeLayer, motionPath) != nullptr;
            const bool shouldCaptureTitleFrame =
                (!hadHeldFrame &&
                 (yuzuTitleHasOpaqueCanvasBaseFrame ||
                  yuzuTitleHasStableFrame)) ||
                (hadHeldFrame && !_runtime->yuzuTitleFinalFrameRendered &&
                 yuzuTitleHasOpaqueFinalOverlayFrame);
            if(shouldCaptureTitleFrame) {
                capturedYuzuTitleFrame =
                    captureYuzuTitlePresentationHoldFrame(
                        renderLayerObject,
                        resolveNativeLayer(renderLayerObject),
                        motionPath);
            }
            if(capturedYuzuTitleFrame &&
               (yuzuTitleHasStableFrame ||
                (hadHeldFrame && yuzuTitleHasOpaqueFinalOverlayFrame))) {
                _runtime->yuzuTitleFinalFrameRendered = true;
            }
        }
        if(centeredGamePresentation) {
            auto *executedLayer = resolveNativeLayer(renderLayerObject);
            const bool executedCgViewSdPreview =
                executedLayer &&
                layerBelongsToCgViewPresentation(executedLayer) &&
                isYuzuSdPreviewMotionPath(motionPath);
            if(executedCgViewSdPreview &&
               motionPresentationLayerHasVisibleSamples(executedLayer)) {
                captureCenteredPresentationHoldFrame(
                    renderLayerObject, executedLayer, motionPath);
                syncCenteredPresentationHoldMotionFrame(
                    executedLayer, motionPath, canvasWidth, canvasHeight);
            } else {
                syncYuzuSdPreviewPresentationLayers(
                    executedLayer, motionPath, canvasWidth,
                    canvasHeight, "execute");
            }
        }
        if(canReuseEmoteRender) {
            // The E-mote work layer itself is not the visible character layer,
            // so assignImages() can replace the visible GPU texture after the
            // normal dirty region has already been calculated. Pair every real
            // cache refresh with one complete window composite. Cached frames
            // keep the local dirty-region path and remain inexpensive.
            TVPRequestFullGpuCompletion();
            auto *executedLayer = resolveNativeLayer(renderLayerObject);
            auto *executedImage =
                executedLayer ? executedLayer->GetMainImage() : nullptr;
            if(executedImage &&
               executedImage->GetWidth() == canvasWidth &&
               executedImage->GetHeight() == canvasHeight) {
                auto &entry = _runtime->emoteRenderFrameCache;
                if(!entry.bitmap ||
                   entry.bitmap->GetWidth() != canvasWidth ||
                   entry.bitmap->GetHeight() != canvasHeight) {
                    entry.bitmap = std::make_shared<tTVPBaseBitmap>(
                        static_cast<tjs_uint>(canvasWidth),
                        static_cast<tjs_uint>(canvasHeight), 32);
                }
                entry.bitmap->CopyRect(
                    0, 0, executedImage,
                    tTVPRect(0, 0, canvasWidth, canvasHeight));
                entry.motion = motionPath;
                entry.frame = _clampedEvalTime;
                entry.canvasWidth = canvasWidth;
                entry.canvasHeight = canvasHeight;
                entry.commandSignature = presentationCommandSignature;
                entry.storedUs = motionRenderProfileNowUs();
            }
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
            const auto treeLogKey =
                motionPath + "|" + _runtime->lastExplicitTimelineLabel;
            auto &treeLogCount = treeLogCountByMotion[treeLogKey];
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
                if(useCgViewAffineRenderChild) {
                    if(auto *layer = resolveNativeLayer(renderLayerObject)) {
                        layer->SetVisible(true);
                        layer->Update(false);
                    }
                } else {
                    updateLayerAfterDraw(finalLayerObject);
                }
            } else if(auto *layer = resolveNativeLayer(finalLayerObject)) {
	                layer->Update(false);
	                detail::logoChainTraceLogf(
	                    motionPath, "post.layer", "0x6CE7D8", _clampedEvalTime,
	                    "targetLayer.Update(false) size={}x{}",
                    layer->GetWidth(), layer->GetHeight());
            }
            if(centeredGamePresentation) {
                finalNativeLayer = resolveNativeLayer(finalLayerObject);
                configureCenteredGameMotionPresentationHitPassthrough(
                    finalNativeLayer);
                placeCenteredPresentationBelowMessageUi(
                    finalNativeLayer, "post-render");
                if(layerBelongsToCgViewPresentation(finalNativeLayer)) {
                    hideCenteredPresentationMessageUiOverlay(finalNativeLayer);
                } else {
                    bool centeredPresentationVisible = false;
                    if(motionPresentationLayerHasVisibleSamples(finalNativeLayer)) {
                        captureCenteredPresentationHoldFrame(
                            finalLayerObject, finalNativeLayer, motionPath);
                        centeredPresentationVisible = true;
                    } else {
                        centeredPresentationVisible =
                            restoreCenteredPresentationHoldFrame(
                                finalNativeLayer, motionPath, canvasWidth,
                                canvasHeight, true);
                    }
                    if(centeredPresentationVisible) {
                        syncYuzuSdPreviewPresentationLayers(
                            finalNativeLayer, motionPath, canvasWidth,
                            canvasHeight, "post-render");
                        showCenteredPresentationMessageUiOverlay(
                            finalNativeLayer, canvasWidth, canvasHeight,
                            "post-render");
                    } else {
                        hideCenteredPresentationMessageUiOverlay(finalNativeLayer);
                    }
                }
            }
        }
        if(bypassInternalAssignImages) {
            _needsInternalAssignImages = false;
        }
        if(yuzuTitlePresentation && !_presentationHoldRendering &&
           finalLayerObject) {
            // Keep a short tail for the last animation frame; a multi-second
            // hold can leave title_bg above later scene layers during startup.
            enablePresentationHold(finalLayerObject, 250);
        }

        iTJSDispatch2 *lastCanvasObject =
            (useCgViewAffineRenderChild || useYuzuSdEventRenderChild) &&
                    renderLayerObject
                ? renderLayerObject
                : resolvedLayerObject;
        _runtime->lastCanvas =
            tTJSVariant(lastCanvasObject, lastCanvasObject);
        if((useCgViewAffineRenderChild || useYuzuSdEventRenderChild) &&
           lastCanvasObject) {
            _targetLayer = tTJSVariant(lastCanvasObject, lastCanvasObject);
        }
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
        iTJSDispatch2 *presentationTarget =
            tryResolveLayerDispatch(sla->getTargetLayer());
        if(!presentationTarget) {
            presentationTarget = ownerLayer;
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
        const bool parentStateChanged = applyMotionParentRootStateForRender();
        if((parentStateChanged || _layersDirty || _emoteDirty) &&
           !_runtime->nodes.empty()) {
            updateLayers();
        }
        prepareRenderItems();
        applyPreparedRenderItemTranslateOffsets();
        auto *renderNativeLayer = resolveNativeLayer(renderTarget);
        auto *ownerNativeLayer = resolveNativeLayer(ownerLayer);
        auto *presentationNativeLayer = resolveNativeLayer(presentationTarget);
        const bool cgViewScriptedPresentation =
            layerBelongsToCgViewPresentation(renderNativeLayer) ||
            layerBelongsToCgViewPresentation(ownerNativeLayer) ||
            layerBelongsToCgViewPresentation(presentationNativeLayer);
        if(cgViewScriptedPresentation) {
            if(LOGGER && shouldDebugTitleRender(motionPath) &&
               markRenderDebugLogged("sla-yuzu-presentation-skip-cg-view:" +
                                     motionPath)) {
                LOGGER->info(
                    "motion yuzu presentation adjustment skipped: motion={} reason=sla_cg_view_scripted_affine render=[{}] owner=[{}] target=[{}]",
                    motionPath, describeLayerForDebug(renderNativeLayer),
                    describeLayerForDebug(ownerNativeLayer),
                    describeLayerForDebug(presentationNativeLayer));
            }
        } else {
            adjustPreparedRenderItemsForYuzuPresentation(
                *_runtime, motionPath, canvasWidth, canvasHeight);
        }
        if(cgViewScriptedPresentation) {
            if(LOGGER && shouldDebugTitleRender(motionPath) &&
               markRenderDebugLogged("sla-center-skip-cg-view:" + motionPath)) {
                LOGGER->info(
                    "motion centered game presentation skipped: motion={} reason=sla_cg_view_scripted_affine render=[{}] owner=[{}] target=[{}]",
                    motionPath, describeLayerForDebug(renderNativeLayer),
                    describeLayerForDebug(ownerNativeLayer),
                    describeLayerForDebug(presentationNativeLayer));
            }
        } else {
            adjustPreparedRenderItemsForCenteredGameMotion(
                *_runtime, motionPath, canvasWidth, canvasHeight,
                resolveCenteredGameMotionResolution(_resolution, _tags, _metadata,
                                                    motionPath),
                renderNativeLayer ? renderNativeLayer : ownerNativeLayer);
        }

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

        postProcessCenteredGameMotionSeparateLayerPresentation(
            presentationTarget, renderTarget, motionPath, canvasWidth,
            canvasHeight);
        if(auto *renderLayer = resolveNativeLayer(renderTarget);
           renderLayer && layerBelongsToCgViewPresentation(renderLayer) &&
           isYuzuSdPreviewMotionPath(motionPath)) {
            captureYuzuSdPreviewFrame(renderTarget, motionPath);
        }

        _runtime->lastCanvas = tTJSVariant(renderTarget, renderTarget);
        detail::logoChainTraceSummary(
            motionPath, "renderToSeparateLayerAdaptor", _clampedEvalTime,
            isAccurateSlaRenderEnabled() ? "accurate=1" : "accurate=0");
        return true;
    }

    void Player::captureYuzuSdPreviewFrame(
        iTJSDispatch2 *renderTargetObject,
        const std::string &motionPath) {
        if(!renderTargetObject || !_runtime || !_runtime->activeMotion ||
           motionPath.empty()) {
            return;
        }
        auto *layer = resolveNativeLayer(renderTargetObject);
        auto *image = layer ? layer->GetMainImage() : nullptr;
        if(!image || image->GetWidth() <= 0 || image->GetHeight() <= 0 ||
           !motionPresentationLayerHasVisibleSamples(layer)) {
            return;
        }

        const tjs_int width = static_cast<tjs_int>(image->GetWidth());
        const tjs_int height = static_cast<tjs_int>(image->GetHeight());
        if(!_yuzuSdPreviewFrame ||
           _yuzuSdPreviewFrame->GetWidth() != width ||
           _yuzuSdPreviewFrame->GetHeight() != height) {
            _yuzuSdPreviewFrame = std::make_shared<tTVPBaseBitmap>(
                static_cast<tjs_uint>(width),
                static_cast<tjs_uint>(height), 32);
        }
        _yuzuSdPreviewFrame->CopyRect(
            0, 0, image, tTVPRect(0, 0, width, height));
        _yuzuSdPreviewFrameMotion = motionPath;
        _yuzuSdPreviewFrameLabel = _runtime->lastExplicitTimelineLabel;
    }

    bool Player::restoreFrozenYuzuSdPreviewFrame(
        iTJSDispatch2 *targetObject) {
        if(!_runtime || !_runtime->activeMotion || !_yuzuSdPreviewFrame ||
           _yuzuSdPreviewFrameMotion != _runtime->activeMotion->path ||
           _yuzuSdPreviewFrameLabel != _runtime->lastExplicitTimelineLabel) {
            return false;
        }

        iTJSDispatch2 *renderTarget = targetObject;
        if(targetObject) {
            if(auto *sla =
                   ncbInstanceAdaptor<SeparateLayerAdaptor>::GetNativeInstance(
                       targetObject, false)) {
                auto *ownerLayer = sla->getOwner();
                if(!ownerLayer) {
                    ownerLayer = tryResolveSeparateAdaptorOwner(
                        tTJSVariant(targetObject, targetObject));
                }
                int canvasWidth = 0;
                int canvasHeight = 0;
                renderTarget = resolveSeparateLayerRenderTarget(
                    sla, ownerLayer, canvasWidth, canvasHeight);
            }
        }
        if(!renderTarget && _runtime->lastCanvas.Type() == tvtObject) {
            renderTarget = _runtime->lastCanvas.AsObjectNoAddRef();
        }

        auto *layer = resolveNativeLayer(renderTarget);
        if(!layer) {
            return false;
        }
        const int width = static_cast<int>(_yuzuSdPreviewFrame->GetWidth());
        const int height = static_cast<int>(_yuzuSdPreviewFrame->GetHeight());
        if(!prepareMotionPresentationLayerForRender(layer, width, height)) {
            return false;
        }
        auto *image = layer->GetMainImage();
        if(!image) {
            return false;
        }
        image->Fill(
            tTVPRect(0, 0, image->GetWidth(), image->GetHeight()),
            0x00000000);
        image->CopyRect(
            0, 0, _yuzuSdPreviewFrame.get(),
            tTVPRect(0, 0, width, height));
        layer->SetVisible(true);
        layer->Update(false);
        _runtime->lastCanvas = tTJSVariant(renderTarget, renderTarget);
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

        const std::string lookupKey =
            (_runtime->activeMotion ? _runtime->activeMotion->path
                                    : std::string{}) +
            '\n' + requestKey;
        if(_runtime->sourceLookupMisses.find(lookupKey) !=
           _runtime->sourceLookupMisses.end()) {
            return;
        }

        ttstr resolved;
        if(!detail::resolveExistingPath(buildSourceCandidates(*_runtime, name),
                                        resolved)) {
            _runtime->sourceLookupMisses.emplace(lookupKey);
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
            _runtime->sourceLookupMisses.emplace(lookupKey);
            return;
        }

        _runtime->sourcesByKey.emplace(requestKey, source);
        _runtime->sourcesByKey.emplace(resolvedKey, source);
    }

    void Player::clearCache() {
        _runtime->sourcesByKey.clear();
        _runtime->sourceLookupMisses.clear();
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
        const bool parentStateChanged = applyMotionParentRootStateForRender();
        if((parentStateChanged || _layersDirty || _emoteDirty) &&
           !_runtime->nodes.empty()) {
            updateLayers();
        }
        calcViewParam();
        prepareRenderItems();
        if(_canvasCaptureEnabled) {
            _runtime->lastCanvas = makeCanvasSummary(*_runtime);
        }
    }

    void Player::scheduleTimelineControlAnimatorLike_0x6646E0(
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

        // sub_6646E0 receives Player+1161 as its queuing flag.  When that
        // byte is clear it discards pending requests before adding the new
        // one, while preserving the animator's currently evaluated value.
        // Always appending here lets old loop requests run after the seek and
        // makes the tail/body alternate between stale poses on consecutive
        // frames at every loop boundary.
        if(!_emoteAnimatorFlag) {
            animator.queue.clear();
            animator.active = false;
            animator.startValue = animator.currentValue;
            animator.targetValue = animator.currentValue;
            animator.progress = 1.0f;
            animator.duration = 0.0f;
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
        _layersDirty = true;

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
            // The list stores the persistent controller/base value.  A
            // timeline is a per-frame additive layer over that value; it must
            // not be folded back into the persistent slot.  Mutating
            // entry.value here accumulated every waiting-loop sample across
            // frames until the parameter hit an extreme.  The visible result
            // was a fast one-shot sway followed by completely still physics
            // anchors (and, for slant controls, a permanently tilted model).
            double outputValue = entry.value;
            accumulateTimelineContributionLike_0x67C560(entry.label,
                                                          outputValue);
            if(shouldMirrorEvalLabelLike_0x67C6B0(entry.label)) {
                outputValue = -outputValue;
            }
            _evalResultValues[entry.label] = outputValue;
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
            const double timelineTimeBeforeAdvance = state.currentTime;
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
                    seekTimelineControlStateLike_0x66EE30(
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
                        seekTimelineControlStateLike_0x66EE30(
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

            if(LOGGER && std::getenv("AETHERKIRI_EMOTE_TIMELINE_TRACE")) {
                const auto beforeBucket = static_cast<long long>(
                    std::floor(std::max(timelineTimeBeforeAdvance, 0.0) /
                               60.0));
                const auto afterBucket = static_cast<long long>(
                    std::floor(std::max(state.currentTime, 0.0) / 60.0));
                const bool rewound =
                    state.currentTime + 0.0001 < timelineTimeBeforeAdvance;
                if(rewound || afterBucket != beforeBucket) {
                    std::ostringstream values;
                    if(binding) {
                        for(size_t trackIndex = 0;
                            trackIndex < binding->tracks.size(); ++trackIndex) {
                            if(trackIndex != 0) {
                                values << ';';
                            }
                            const auto &track = binding->tracks[trackIndex];
                            const double trackValue =
                                trackIndex < state.controlTrackValues.size()
                                ? state.controlTrackValues[trackIndex]
                                : 0.0;
                            const bool active =
                                trackIndex < state.controlTrackAnimators.size()
                                ? state.controlTrackAnimators[trackIndex].active
                                : false;
                            const size_t queued =
                                trackIndex < state.controlTrackAnimators.size()
                                ? state.controlTrackAnimators[trackIndex]
                                      .queue.size()
                                : 0;
                            values << track.label << '=' << trackValue
                                   << "(active=" << (active ? 1 : 0)
                                   << ",queue=" << queued << ')';
                        }
                    }
                    LOGGER->info(
                        "[EMOTE_TIMELINE] step motion={} label={} time={:.3f}->{:.3f} flags={} blend={:.4f} values=[{}]",
                        activeMotion ? activeMotion->path : std::string{},
                        label, timelineTimeBeforeAdvance, state.currentTime,
                        state.flags, state.blendRatio, values.str());
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

    void Player::seekTimelineControlStateLike_0x66EE30(
        detail::TimelineState &state,
        const detail::TimelineControlBinding &binding,
        double time) {
        // libgame.so sub_66EE30 relocates the timeline cursor and rebuilds
        // only the per-track frame cursors.  It deliberately keeps the
        // existing track animator/value alive, then queues the value at the
        // seek destination from the current pose.  Clearing those objects at
        // every loop boundary makes the model snap to zero for one frame --
        // visible as the periodic high-frequency tail/body twitch.
        state.currentTime = time;
        state.controlFrameCursor.assign(binding.tracks.size(), -1);
        state.controlTrackValues.resize(binding.tracks.size(), 0.0f);
        state.controlTrackAnimators.resize(binding.tracks.size());
        for(size_t trackIndex = 0; trackIndex < binding.tracks.size();
            ++trackIndex) {
            const auto &track = binding.tracks[trackIndex];
            int cursor = -1;
            int lastNonTypeZero = -1;
            for(size_t frameIndex = 0; frameIndex < track.frames.size();
                ++frameIndex) {
                const auto &frame = track.frames[frameIndex];
                if(frame.time > time) {
                    break;
                }
                cursor = static_cast<int>(frameIndex);
                if(!frame.isTypeZero) {
                    lastNonTypeZero = cursor;
                }
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
                scheduleTimelineControlAnimatorLike_0x6646E0(
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
                        scheduleTimelineControlAnimatorLike_0x6646E0(
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
                seekTimelineControlStateLike_0x66EE30(
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
                        const double transition =
                            nextIndex + 1 < track.frames.size()
                            ? std::max(track.frames[nextIndex + 1].time -
                                           state.currentTime - 1.0,
                                       0.0)
                            : 0.0;
                        setVariableResolvedWeightLike_0x671228(
                            track.label, static_cast<double>(frame.value),
                            transition, frame.easingWeight);
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

    void Player::passTimelinesLike_0x67A100() {
        if(!_runtime || !_runtime->activeMotion) {
            return;
        }

        _layersDirty = true;
        size_t writeIndex = 0;
        for(size_t readIndex = 0;
            readIndex < _runtime->playingTimelineLabels.size(); ++readIndex) {
            const std::string label =
                _runtime->playingTimelineLabels[readIndex];
            const auto stateIt = _runtime->timelines.find(label);
            const auto bindingIt =
                _runtime->activeMotion->timelineControlByLabel.find(label);
            if(stateIt == _runtime->timelines.end() ||
               bindingIt ==
                   _runtime->activeMotion->timelineControlByLabel.end()) {
                _runtime->playingTimelineLabels[writeIndex++] = label;
                continue;
            }

            auto &state = stateIt->second;
            const auto &binding = bindingIt->second;
            // Native pass() only consumes one-shot timelines. Looping control
            // timelines keep running and are left in the active list.
            if(binding.loopBegin >= 0.0 || !state.playing) {
                if(state.playing) {
                    _runtime->playingTimelineLabels[writeIndex++] = label;
                }
                continue;
            }

            if((state.flags & 2) != 0) {
                if((state.flags & 4) != 0) {
                    _runtime->playingTimelineLabels[writeIndex++] = label;
                    continue;
                }
                setTimelineBlendLike_0x6735AC(
                    label, true, 0.0, 20.0, 0.0);
                state.flags |= 4;
            }

            if(!state.controlInitialized ||
               state.controlFrameCursor.size() != binding.tracks.size()) {
                seekTimelineControlStateLike_0x66EE30(
                    state, binding, std::max(state.currentTime, 0.0));
            }
            if(state.controlTrackAnimators.size() < binding.tracks.size()) {
                state.controlTrackAnimators.resize(binding.tracks.size());
            }
            if(state.controlTrackValues.size() < binding.tracks.size()) {
                state.controlTrackValues.resize(binding.tracks.size(), 0.0f);
            }

            for(size_t trackIndex = 0; trackIndex < binding.tracks.size();
                ++trackIndex) {
                const auto &track = binding.tracks[trackIndex];
                int cursor = state.controlFrameCursor[trackIndex];
                for(size_t frameIndex = static_cast<size_t>(cursor + 1);
                    frameIndex < track.frames.size(); ++frameIndex) {
                    const auto &frame = track.frames[frameIndex];
                    if(!frame.isTypeZero) {
                        if((state.flags & 2) != 0 &&
                           !track.instantVariable) {
                            scheduleTimelineControlAnimatorLike_0x6646E0(
                                state, trackIndex, frame.value, frame.time,
                                frame.easingWeight);
                        } else {
                            setVariableResolvedWeightLike_0x671228(
                                track.label, static_cast<double>(frame.value),
                                frame.time, frame.easingWeight);
                        }
                    }
                    cursor = static_cast<int>(frameIndex);
                }
                state.controlFrameCursor[trackIndex] = cursor;
            }

            state.controlLastAppliedTime = binding.lastTime;
            if((state.flags & 4) != 0) {
                _runtime->playingTimelineLabels[writeIndex++] = label;
            } else {
                state.playing = false;
                state.wasPlaying = false;
            }
        }
        _runtime->playingTimelineLabels.resize(writeIndex);
        _allplaying = !_runtime->playingTimelineLabels.empty();
        if(!_allplaying) {
            disableAutoProgress();
        }
        _emoteDirty = true;
    }

    void Player::ensureEmoteControlStateInitialized() {
        if(const auto *extension = motionPlayerExtension();
           extension && extension->ensureControlState) {
            extension->ensureControlState(*this);
        }
    }

    void Player::stepAutoBlinkControllersLike_0x660FBC(double dt) {
        if(const auto *extension = motionPlayerExtension();
           extension && extension->stepAutoBlink) {
            extension->stepAutoBlink(*this, dt);
        }
    }

    void Player::stepEmotePhysicsLike_0x678B28(double dt) {
        if(const auto *extension = motionPlayerExtension();
           extension && extension->stepPhysics) {
            extension->stepPhysics(*this, dt);
        }
    }

    void Player::frameProgress(double dt) {
        // Aligned to libkrkr2.so Player_progress_inner (0x6C106C):
        // _speed is a bool flag (play/pause). When false, skip progress entirely.
        if(!_speed) {
            return;
        }
        _layersDirty = true;
        if(!_completedEndedTimelineRenderHoldLabel.empty()) {
            const auto stateIt = _runtime->timelines.find(
                _completedEndedTimelineRenderHoldLabel);
            if(stateIt != _runtime->timelines.end() &&
               stateIt->second.playing && stateIt->second.totalFrames > 0.0 &&
               stateIt->second.currentTime + 0.0001 <
                   stateIt->second.totalFrames) {
                _completedEndedTimelineRenderHoldLabel.clear();
                _yuzuSdChildContinuationFrames = 0.0;
            }
        }
        const auto *activeClipBeforeProgress = selectActiveClip();
        const double actualDelta = dt;
        if(!_completedEndedTimelineRenderHoldLabel.empty() &&
           _yuzuSdChildContinuationFrames > 0.0) {
            _yuzuSdChildContinuationFrames = std::max(
                0.0, _yuzuSdChildContinuationFrames - actualDelta);
        }
        _frameLastTime = actualDelta;
        _frameLoopTime += actualDelta;
        _loopTime += actualDelta;
        _frameTickCount += actualDelta;

        _evalResultValues.clear();

        ensureEmoteControlStateInitialized();
        const auto *extension = motionPlayerExtension();
        if(extension && extension->hasActivePhysics &&
           extension->hasActivePhysics(*this)) {
            ensureNodeTreeBuilt();
        }

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

        stepAutoBlinkControllersLike_0x660FBC(actualDelta);

        applyEvalResultPostProcessLike_0x67CC9C();
        stepEmotePhysicsLike_0x678B28(actualDelta);

        // Camera velocity/friction moved to updateLayers pre-loop (0x6BB360..0x6BB42C)

        // Inference from libkrkr2.so Player_progress_inner (0x6C106C):
        // player+456 is the selected clip/timeline eval time consumed by
        // Player_updateLayers (0x6BB33C), not an arbitrary primary-label entry.
        _clampedEvalTime = activeClipTime(*_runtime, selectActiveClip());

        // title_bg3..6 are the four character title cards. Their nested
        // `charmove` clip contains a transition-out tail (70..90), while the
        // parent title keeps rendering through the logo settle at frame 165.
        // Keep the nested player on its authored fully-visible pose so the
        // character remains part of the completed title card.
        if(_motionParentPlayer && activeClipBeforeProgress &&
           activeClipBeforeProgress->label == "charmove" &&
           _runtime->activeMotion &&
           isYuzuNumberedTitleCharacterMotion(
               _runtime->activeMotion->path)) {
            _clampedEvalTime = std::min(_clampedEvalTime, 70.0);
        }

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
                const double completedRenderTime = std::max(
                    0.0, std::nextafter(endTime, 0.0));
                for(auto &[label, state] : _runtime->timelines) {
                    state.currentTime =
                        label == activeClipBeforeProgress->label
                            ? completedRenderTime
                            : std::min(state.currentTime, endTime);
                    state.playing = false;
                    state.wasPlaying = false;
                }
                _runtime->playingTimelineLabels.clear();
                // The sync boundary itself is already outside the authored
                // one-shot slot.  Keep rendering its last visible sample
                // while reporting playback as complete; otherwise short UI
                // transitions (`over`/`out`) disappear as soon as they end.
                _clampedEvalTime = completedRenderTime;
                if(wasPlaying) {
                    _runtime->pendingEvents.push_back(
                        {1, activeClipBeforeProgress->label, {}});
                }

                // A motion sub-node may use a one-shot presentation clip and
                // provide a one-frame `normal` clip as its persistent pose.
                // The parent keeps the child Player alive after the one-shot
                // ends, so select that owner's idle pose without reporting a
                // new playing timeline. This is how classic title PSBs hand
                // off from an intro character to the complete static cast.
                const auto activeMotionPath =
                    _runtime->activeMotion
                        ? psbDebugLowercase(_runtime->activeMotion->path)
                        : std::string{};
                if(_motionParentPlayer &&
                   activeClipBeforeProgress->label != "normal" &&
                   activeMotionPath.find("title.psb") != std::string::npos &&
                   psbDebugLowercase(detail::narrow(_chara)) == "char") {
                    const auto *normalClip = detail::findMotionClip(
                        *_runtime->activeMotion, detail::narrow(_chara),
                        "normal", false);
                    if(normalClip) {
                        auto &normalState = _runtime->timelines["normal"];
                        normalState.label = "normal";
                        normalState.totalFrames = normalClip->totalFrames;
                        normalState.loop = normalClip->loop;
                        normalState.loopTime = normalClip->loopTime;
                        normalState.currentTime = 0.0;
                        normalState.playing = false;
                        normalState.wasPlaying = false;
                        _runtime->lastExplicitTimelineLabel = "normal";
                        _clampedEvalTime = 0.0;
                        _runtime->nodes.clear();
                        _runtime->nodeLabelMap.clear();
                        _runtime->nodesBuilt = false;
                        _emoteDirty = true;
                    }
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

        _allplaying = !_runtime->playingTimelineLabels.empty() ||
            shouldReportPlayingChildPlayers();
        _syncActive = _syncWaiting && _allplaying;
    }


} // namespace motion
