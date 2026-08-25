// PlayerInternal.h — Shared internal helpers extracted from Player.cpp
// These were originally in an anonymous namespace. Now in motion::internal
// with inline linkage for use across multiple translation units.
//
#pragma once

#include "Player.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include "WindowIntf.h"
#include <cstring>
#include <optional>
#include <random>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "LayerIntf.h"
#include "LayerBitmapIntf.h"
#include "LayerManager.h"
#include "GraphicsLoaderIntf.h"
#include "tvpgl.h"
#include "RuntimeSupport.h"
#include "ResourceManager.h"
#include "SeparateLayerAdaptor.h"
#include "D3DAdaptor.h"
#include "StorageIntf.h"
#include "ncbind.hpp"
#include "tjsArray.h"
#include "EventIntf.h"
#include "ScriptMgnIntf.h"
#include "NodeTree.h"
#include "MotionNode.h"
#include "MotionPlayerExtension.h"

#define LOGGER spdlog::get("plugin")
#define STUB_WARN(name) LOGGER->warn("Player::" #name "() stub called")

namespace motion {
namespace internal {

        inline bool sameMotionOwnershipIdentity(
            const std::string &childPath,
            const ttstr &childChara,
            const ttstr &childMotion,
            const std::string &ancestorPath,
            const ttstr &ancestorChara,
            const ttstr &ancestorMotion) {
            // One PSB commonly owns several independent objects whose clips
            // share generic names such as `show`, `normal`, or `off`.  Those
            // are valid ownership edges (for example TITLE2/show ->
            // char/show), not recursion.  A cycle requires the complete
            // resource/object/clip identity to repeat.
            return childPath == ancestorPath &&
                childChara == ancestorChara &&
                childMotion == ancestorMotion;
        }

        // Binder layers are structural containers. Kirikiri allows them to
        // participate in the layer tree, but drawable-only APIs such as
        // GetImageWidth/SetHasImage must never be used on them.
        inline bool presentationLayerTypeCanReceivePixels(
            tTVPLayerType type) {
            return type != ltBinder;
        }

        inline bool d3dEmoteFrameReuseRouteEligible(
            bool isEmoteMode,
            bool retainD3DPresentation,
            std::size_t commandCount) {
            return isEmoteMode && !retainD3DPresentation &&
                commandCount != 0;
        }

        inline bool d3dEmoteFrameCacheMatches(
            const detail::PlayerRuntime::EmoteRenderFrameCacheEntry &entry,
            const std::string &motion,
            double frame,
            int canvasWidth,
            int canvasHeight,
            std::size_t commandSignature) {
            return entry.bitmap && entry.motion == motion &&
                entry.canvasWidth == canvasWidth &&
                entry.canvasHeight == canvasHeight &&
                std::fabs(entry.frame - frame) < 0.0001 &&
                entry.bitmap->GetWidth() == canvasWidth &&
                entry.bitmap->GetHeight() == canvasHeight &&
                entry.commandSignature == commandSignature;
        }

        inline const MotionRenderPolicyV1 *motionRenderPolicy() {
            const auto *extension = motionPlayerExtension();
            return extension != nullptr ? extension->renderPolicy : nullptr;
        }

        inline bool isDifferenceAlphaPassThroughLeaf(
            bool hasOwnSource,
            bool groupOnly,
            int blendMode) {
            const auto *policy = motionRenderPolicy();
            return policy != nullptr &&
                policy->isDifferenceAlphaPassThroughLeaf != nullptr &&
                policy->isDifferenceAlphaPassThroughLeaf(
                    hasOwnSource, groupOnly, blendMode);
        }

        inline bool isIndependentDifferenceAlphaMaskGroup(
            bool groupOnly,
            bool hasExplicitMasks,
            int itemFlags,
            bool hasConcreteRenderParent) {
            const auto *policy = motionRenderPolicy();
            return policy != nullptr &&
                policy->isIndependentDifferenceAlphaMaskGroup != nullptr &&
                policy->isIndependentDifferenceAlphaMaskGroup(
                    groupOnly, hasExplicitMasks, itemFlags,
                    hasConcreteRenderParent);
        }

        inline bool canReceiveIndependentDifferenceAlphaMask(
            bool hasOwnSource,
            bool groupOnly,
            int blendMode) {
            const auto *policy = motionRenderPolicy();
            return policy != nullptr &&
                policy->canReceiveIndependentDifferenceAlphaMask != nullptr &&
                policy->canReceiveIndependentDifferenceAlphaMask(
                    hasOwnSource, groupOnly, blendMode);
        }

        inline bool isSyntheticMotionBlankSource(
            const std::string &sourceKey) {
            const auto *policy = motionRenderPolicy();
            return policy != nullptr &&
                policy->isSyntheticMotionBlankSource != nullptr &&
                policy->isSyntheticMotionBlankSource(sourceKey);
        }

        inline bool renderClipCoversCanvas(
            const std::array<int, 4> &clipRect,
            int canvasWidth,
            int canvasHeight) {
            return canvasWidth > 0 && canvasHeight > 0 &&
                clipRect[0] <= 0 && clipRect[1] <= 0 &&
                clipRect[2] >= canvasWidth &&
                clipRect[3] >= canvasHeight;
        }

        inline bool
        startupLogoUsesCenteredOrigin(const std::array<float, 4> &bounds,
                                      int canvasWidth, int canvasHeight) {
            if(canvasWidth <= 0 || canvasHeight <= 0 ||
               !std::all_of(bounds.begin(), bounds.end(),
                            [](float value) { return std::isfinite(value); })) {
                return false;
            }

            const float width = bounds[2] - bounds[0];
            const float height = bounds[3] - bounds[1];
            if(width <= 0.0f || height <= 0.0f) {
                return false;
            }

            const bool crossesOrigin = bounds[0] < 0.0f && bounds[1] < 0.0f &&
                bounds[2] > 0.0f && bounds[3] > 0.0f;
            if(!crossesOrigin) {
                return false;
            }

            const float canvasWidthF = static_cast<float>(canvasWidth);
            const float canvasHeightF = static_cast<float>(canvasHeight);
            const float centerX = (bounds[0] + bounds[2]) * 0.5f;
            const float centerY = (bounds[1] + bounds[3]) * 0.5f;
            const float centeredToleranceX =
                std::min(canvasWidthF * 0.1f, width * 0.2f + 1e-3f);
            const float centeredToleranceY =
                std::min(canvasHeightF * 0.1f, height * 0.2f + 1e-3f);
            return std::fabs(centerX) <= centeredToleranceX &&
                std::fabs(centerY) <= centeredToleranceY;
        }

        inline bool startupLogoMotionUsesCenteredOrigin(
            std::string motionPath) {
            std::transform(motionPath.begin(), motionPath.end(),
                           motionPath.begin(),
                           [](unsigned char ch) {
                               return static_cast<char>(std::tolower(ch));
                           });
            return motionPath.find("yuzulogo.mtn") != std::string::npos;
        }

        inline bool startupLogoMotionScalesAroundCanvasCenter(
            std::string motionPath) {
            std::transform(motionPath.begin(), motionPath.end(),
                           motionPath.begin(),
                           [](unsigned char ch) {
                               return static_cast<char>(std::tolower(ch));
                           });
            return motionPath.find("yuzulogo.mtn") != std::string::npos ||
                motionPath.find("m2logo.mtn") != std::string::npos;
        }

        inline bool startupLogoMotionUsesStableBackdropReference(
            std::string motionPath) {
            std::transform(motionPath.begin(), motionPath.end(),
                           motionPath.begin(),
                           [](unsigned char ch) {
                               return static_cast<char>(std::tolower(ch));
                           });
            return motionPath.find("m2logo.mtn") != std::string::npos;
        }

        inline bool startupLogoStableBackdropSource(
            const std::string &motionPath,
            std::string sourceKey) {
            if(!startupLogoMotionUsesStableBackdropReference(motionPath)) {
                return false;
            }
            std::transform(sourceKey.begin(), sourceKey.end(),
                           sourceKey.begin(),
                           [](unsigned char ch) {
                               return static_cast<char>(std::tolower(ch));
                           });
            return sourceKey == "src/logo/icon50" ||
                sourceKey.find("/logo/icon/icon50") != std::string::npos;
        }

        inline bool startupLogoPresentationScaleAppliesToSource(
            const std::string &motionPath,
            const std::string &sourceKey) {
            return !startupLogoMotionUsesStableBackdropReference(motionPath) ||
                startupLogoStableBackdropSource(motionPath, sourceKey);
        }

        inline std::array<float, 2> startupLogoPresentationScale(
            std::string motionPath,
            float canvasWidth,
            float canvasHeight,
            float referenceWidth,
            float referenceHeight) {
            if(!std::isfinite(canvasWidth) || !std::isfinite(canvasHeight) ||
               !std::isfinite(referenceWidth) ||
               !std::isfinite(referenceHeight) ||
               canvasWidth <= 0.0f || canvasHeight <= 0.0f ||
               referenceWidth <= 0.0f || referenceHeight <= 0.0f) {
                return { 1.0f, 1.0f };
            }

            const float scaleX = canvasWidth / referenceWidth;
            const float scaleY = canvasHeight / referenceHeight;
            if(startupLogoMotionUsesStableBackdropReference(motionPath)) {
                const float coveredScale = std::max(scaleX, scaleY);
                return { coveredScale, coveredScale };
            }
            return { scaleX, scaleY };
        }

        inline bool shouldCaptureYuzuTitlePresentationHoldFrame(
            bool hadHeldFrame,
            bool finalFrameRendered,
            bool hasOpaqueCanvasBaseFrame,
            bool hasStableFrame,
            bool hasOpaqueFinalOverlayFrame) {
            if(!hadHeldFrame) {
                return hasOpaqueCanvasBaseFrame || hasStableFrame ||
                    hasOpaqueFinalOverlayFrame;
            }
            return !finalFrameRendered && hasOpaqueFinalOverlayFrame;
        }

        inline bool yuzuTitlePresentationFrameIsStable(
            bool hasStableComposition,
            bool hasActiveTransientLogo) {
            return hasStableComposition && !hasActiveTransientLogo;
        }

        inline bool yuzuTitlePresentationHoldFrameIsResident(
            bool exactLayer,
            bool layerVisible,
            bool parentVisible,
            bool hasImage,
            bool hasMainImage,
            int opacity) {
            return exactLayer && layerVisible && parentVisible && hasImage &&
                hasMainImage && opacity > 0;
        }

        inline bool isFullCanvasCompositeRenderRoot(
            bool groupOnly,
            bool hasRenderParent,
            bool alphaMaskOnly,
            int opacity,
            const std::array<int, 4> &clipRect,
            int canvasWidth,
            int canvasHeight) {
            return groupOnly && !hasRenderParent && !alphaMaskOnly &&
                opacity > 0 &&
                renderClipCoversCanvas(
                    clipRect, canvasWidth, canvasHeight);
        }

        inline bool isFullCanvasDirectRenderPlane(
            bool hasOwnSource,
            bool groupOnly,
            bool hasRenderParent,
            bool alphaMaskOnly,
            int blendMode,
            int opacity,
            const std::array<int, 4> &clipRect,
            int canvasWidth,
            int canvasHeight) {
            return hasOwnSource && !groupOnly && !hasRenderParent &&
                !alphaMaskOnly && blendMode == 0 && opacity > 0 &&
                renderClipCoversCanvas(
                    clipRect, canvasWidth, canvasHeight);
        }

        inline bool isAuthoredDifferenceAlphaPair(
            const std::string &colourLabel,
            const std::string &alphaLabel) {
            const auto *policy = motionRenderPolicy();
            return policy != nullptr &&
                policy->isAuthoredDifferenceAlphaPair != nullptr &&
                policy->isAuthoredDifferenceAlphaPair(
                    colourLabel, alphaLabel);
        }

        inline bool isNestedDifferenceAlphaPair(
            std::size_t colourCommandIndex,
            const std::vector<std::size_t> &alphaAncestry) {
            const auto *policy = motionRenderPolicy();
            return policy != nullptr &&
                policy->isNestedDifferenceAlphaPair != nullptr &&
                policy->isNestedDifferenceAlphaPair(
                    colourCommandIndex, alphaAncestry);
        }

        inline bool isGenericDifferenceAlphaLabel(
            const std::string &label) {
            const auto *policy = motionRenderPolicy();
            return policy != nullptr &&
                policy->isGenericDifferenceAlphaLabel != nullptr &&
                policy->isGenericDifferenceAlphaLabel(label);
        }

        inline bool isUnambiguousNestedDifferenceAlphaPair(
            std::size_t nestedPairCount) {
            const auto *policy = motionRenderPolicy();
            return policy != nullptr &&
                policy->isUnambiguousNestedDifferenceAlphaPair != nullptr &&
                policy->isUnambiguousNestedDifferenceAlphaPair(
                    nestedPairCount);
        }

        inline bool shouldUseCombinedDifferenceAlphaMask(
            bool hasSelectedPair,
            std::size_t nestedSourceCount) {
            const auto *policy = motionRenderPolicy();
            return policy != nullptr &&
                policy->shouldUseCombinedDifferenceAlphaMask != nullptr &&
                policy->shouldUseCombinedDifferenceAlphaMask(
                    hasSelectedPair, nestedSourceCount);
        }

        inline bool shouldRecoverDifferenceAlphaFromRgb(
            std::size_t alphaPixelCount,
            std::size_t rgbPixelCount) {
            const auto *policy = motionRenderPolicy();
            return policy != nullptr &&
                policy->shouldRecoverDifferenceAlphaFromRgb != nullptr &&
                policy->shouldRecoverDifferenceAlphaFromRgb(
                    alphaPixelCount, rgbPixelCount);
        }

        inline std::uint8_t differenceAlphaFromRgb(
            std::uint8_t blue,
            std::uint8_t green,
            std::uint8_t red) {
            const auto *policy = motionRenderPolicy();
            return policy != nullptr &&
                    policy->differenceAlphaFromRgb != nullptr
                ? policy->differenceAlphaFromRgb(blue, green, red)
                : static_cast<std::uint8_t>(0);
        }

        inline int independentDifferenceAlphaMaskOperation(
            bool hasAuthoredPair, int groupItemFlags) {
            const auto *policy = motionRenderPolicy();
            return policy != nullptr &&
                    policy->independentDifferenceAlphaMaskOperation != nullptr
                ? policy->independentDifferenceAlphaMaskOperation(
                      hasAuthoredPair, groupItemFlags)
                : 1;
        }

        inline std::uint8_t applyMotionAlphaMaskValueLike_0x6AC4E4(
            std::uint8_t destinationAlpha,
            std::uint8_t maskAlpha,
            bool thresholdMaskMode,
            int operation,
            std::uint8_t threshold = 64) {
            const auto *policy = motionRenderPolicy();
            if(policy != nullptr &&
               policy->applyMotionAlphaMaskValue != nullptr) {
                return policy->applyMotionAlphaMaskValue(
                    destinationAlpha, maskAlpha, thresholdMaskMode,
                    operation, threshold);
            }
            if(operation != 1) {
                return destinationAlpha;
            }
            if(thresholdMaskMode) {
                return maskAlpha < threshold
                    ? static_cast<std::uint8_t>(0)
                    : destinationAlpha;
            }
            return static_cast<std::uint8_t>(
                (static_cast<int>(destinationAlpha) *
                 static_cast<int>(maskAlpha)) /
                255);
        }


        // Return true if a source path is a motion cross-reference
        // (e.g. "motion/title_bg/char_move"), not an image source.
        inline bool isMotionCrossReference(const std::string &src) {
            return src.rfind("motion/", 0) == 0;
        }

        inline bool shouldSearchCachedMotionComposition(
            const std::string &motionRef, const std::string &motionIcon) {
            const auto *policy = motionRenderPolicy();
            return policy != nullptr &&
                    policy->shouldSearchCachedMotionComposition != nullptr
                ? policy->shouldSearchCachedMotionComposition(
                      motionRef, motionIcon)
                : !motionIcon.empty();
        }

        inline bool isPsbRLCompressName(const std::optional<std::string> &name) {
            if(!name) {
                return false;
            }
            std::string lowered = *name;
            std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                           [](unsigned char ch) {
                               return static_cast<char>(std::tolower(ch));
                           });
            return lowered == "rl";
        }

        inline std::string psbDebugLowercase(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(),
                           [](unsigned char ch) {
                               return static_cast<char>(std::tolower(ch));
                           });
            return value;
        }

        inline bool shouldDebugPsbSource(
            const detail::MotionSnapshot &snapshot,
            const std::string &source) {
            const char *enabled = std::getenv("AETHERKIRI_MOTION_DEBUG");
            const bool debugEnabled =
                enabled && *enabled && std::strcmp(enabled, "0") != 0;
            if(!debugEnabled) {
                return false;
            }
            const auto path = psbDebugLowercase(snapshot.path);
            const auto src = psbDebugLowercase(source);
            return path.find("title.pimg") != std::string::npos ||
                path.find("title.psb") != std::string::npos ||
                src.find("title") != std::string::npos;
        }

        inline std::string samplePsbPixelStats(
            const std::vector<std::uint8_t> &data) {
            const size_t pixels = data.size() / 4u;
            if(pixels == 0) {
                return "pixels=0 sampled=0 alpha=0 visible=0 color=0 any=0 maxA=0 maxC=0";
            }
            const size_t stride = std::max<size_t>(1u, pixels / 4096u);
            size_t sampled = 0;
            size_t alpha = 0;
            size_t visible = 0;
            size_t color = 0;
            size_t any = 0;
            int maxAlpha = 0;
            int maxColor = 0;
            for(size_t i = 0; i < pixels; i += stride) {
                const size_t offset = i * 4u;
                if(offset + 3u >= data.size()) {
                    break;
                }
                const int c0 = data[offset + 0u];
                const int c1 = data[offset + 1u];
                const int c2 = data[offset + 2u];
                const int a = data[offset + 3u];
                const int maxRgb = std::max(c0, std::max(c1, c2));
                ++sampled;
                if(a != 0) {
                    ++alpha;
                }
                if(maxRgb != 0) {
                    ++color;
                }
                if((c0 | c1 | c2 | a) != 0) {
                    ++any;
                }
                if(a != 0 && maxRgb != 0) {
                    ++visible;
                }
                maxAlpha = std::max(maxAlpha, a);
                maxColor = std::max(maxColor, maxRgb);
            }
            std::ostringstream out;
            out << "pixels=" << pixels << " sampled=" << sampled
                << " alpha=" << alpha << " visible=" << visible
                << " color=" << color << " any=" << any
                << " maxA=" << maxAlpha << " maxC=" << maxColor;
            return out.str();
        }

        inline std::uint32_t psbDataHeader(
            const std::vector<std::uint8_t> &data) {
            if(data.size() < 4u) {
                return 0;
            }
            return static_cast<std::uint32_t>(data[0]) |
                (static_cast<std::uint32_t>(data[1]) << 8u) |
                (static_cast<std::uint32_t>(data[2]) << 16u) |
                (static_cast<std::uint32_t>(data[3]) << 24u);
        }

        inline bool markPsbDebugLogged(const std::string &key) {
            static std::unordered_set<std::string> loggedKeys;
            return loggedKeys.insert(key).second;
        }

        // PSB RL decompression: each RGBA channel is separately RL-compressed.
        // Format per channel: stream of [marker] entries where
        //   marker & 0x80 → repeat (marker & 0x7F + 1) copies of next byte
        //   otherwise      → (marker + 1) literal bytes follow
        // Aligned to libkrkr2.so via FreeMote PSB RL spec.
        // PSB RL decompression — two variants based on libkrkr2.so sub_695DE8:
        //
        // align=1 (with palette): single-byte RLE, used with 8-bit indexed data
        //   RLE run:  count = (marker & 0x7F) + 3, repeat 1 byte
        //   Literal:  count = marker + 1, copy count bytes
        //
        // align=4 (no palette, RGBA8): 4-byte RLE, used with 32-bit pixel data
        //   RLE run:  count = (marker & 0x7F) + 3, repeat 4 bytes
        //   Literal:  count = marker + 1, copy count*4 bytes
        //   (0x696D00-0x696D98 in libkrkr2.so)
        inline std::vector<std::uint8_t> decompressPsbRL(
            const std::vector<std::uint8_t> &compressed,
            size_t elementCount, int align = 4) {
            const size_t outputSize = elementCount * static_cast<size_t>(align);
            std::vector<std::uint8_t> output(outputSize, 0);

            const auto *src = compressed.data();
            const auto *srcEnd = src + compressed.size();
            auto *dst = output.data();
            const auto *dstEnd = dst + outputSize;

            while(src < srcEnd && dst < dstEnd) {
                const auto marker = *src++;
                if(marker & 0x80) {
                    // RLE run: repeat `align` bytes (count) times
                    const size_t count = (marker & 0x7F) + 3;
                    if(src + align > srcEnd) break;
                    for(size_t i = 0; i < count && dst + align <= dstEnd; i++) {
                        std::memcpy(dst, src, align);
                        dst += align;
                    }
                    src += align;
                } else {
                    // Literal: copy (marker+1)*align bytes verbatim
                    const size_t count = (marker + 1) * static_cast<size_t>(align);
                    if(src + count > srcEnd) break;
                    const size_t n = std::min(count,
                        static_cast<size_t>(dstEnd - dst));
                    std::memcpy(dst, src, n);
                    src += count;
                    dst += n;
                }
            }
            return output;
        }

        inline bool decodePsbPixelResource(
            const detail::MotionSnapshot &snapshot,
            const std::string &iconPath,
            const PSB::PSBResource &pixelResource,
            int width,
            int height,
            bool isRLCompressed,
            std::vector<std::uint8_t> &decodedOut,
            bool *outDecodedIsBgra = nullptr) {
            decodedOut.clear();
            if(outDecodedIsBgra) {
                *outDecodedIsBgra = false;
            }

            if(width <= 0 || height <= 0 || pixelResource.data.empty()) {
                return false;
            }

            const size_t pixelCount =
                static_cast<size_t>(width) * static_cast<size_t>(height);
            const auto palPath = iconPath + "/pal";
            const auto palIt = snapshot.resourcesByPath.find(palPath);
            const bool hasPalette = palIt != snapshot.resourcesByPath.end() &&
                palIt->second && !palIt->second->data.empty();

            if(hasPalette) {
                std::vector<std::uint8_t> indexBuffer;
                if(isRLCompressed) {
                    indexBuffer = decompressPsbRL(pixelResource.data,
                                                  pixelCount, 1);
                } else {
                    indexBuffer.resize(pixelCount, 0);
                    const size_t copyCount = std::min(pixelCount,
                                                      pixelResource.data.size());
                    std::memcpy(indexBuffer.data(), pixelResource.data.data(),
                                copyCount);
                }

                const size_t paletteEntryCount =
                    palIt->second->data.size() / sizeof(tjs_uint32);
                if(paletteEntryCount == 0) {
                    return false;
                }

                std::vector<tjs_uint32> rawPalette(paletteEntryCount, 0);
                std::memcpy(rawPalette.data(), palIt->second->data.data(),
                            paletteEntryCount * sizeof(tjs_uint32));
                std::vector<tjs_uint32> bgraPalette(paletteEntryCount, 0);
                TVPReverseRGB(bgraPalette.data(), rawPalette.data(),
                              static_cast<tjs_int>(paletteEntryCount));

                std::vector<tjs_uint32> expandedPixels(pixelCount, 0);
                TVPBLExpand8BitTo32BitPal(
                    expandedPixels.data(), indexBuffer.data(),
                    static_cast<tjs_int>(pixelCount), bgraPalette.data());

                decodedOut.resize(pixelCount * sizeof(tjs_uint32));
                std::memcpy(decodedOut.data(), expandedPixels.data(),
                            decodedOut.size());
                if(outDecodedIsBgra) {
                    *outDecodedIsBgra = true;
                }
                return true;
            }

            if(isRLCompressed) {
                decodedOut = decompressPsbRL(pixelResource.data, pixelCount, 4);
                return !decodedOut.empty();
            }

            if(pixelResource.data.size() >= pixelCount * 4u) {
                decodedOut.assign(pixelResource.data.begin(),
                                  pixelResource.data.begin() +
                                      static_cast<std::ptrdiff_t>(pixelCount * 4u));
                return true;
            }

            return false;
        }

        inline std::uint8_t expandPsb5To8(const std::uint16_t value) {
            return static_cast<std::uint8_t>((value << 3u) | (value >> 2u));
        }

        inline std::uint8_t expandPsb6To8(const std::uint16_t value) {
            return static_cast<std::uint8_t>((value << 2u) | (value >> 4u));
        }

        inline void decodePsbBcColorPalette(
            const std::uint8_t *block,
            bool allowDxt1Transparency,
            std::array<std::array<std::uint8_t, 4>, 4> &palette) {
            const auto color0 = static_cast<std::uint16_t>(block[0]) |
                (static_cast<std::uint16_t>(block[1]) << 8u);
            const auto color1 = static_cast<std::uint16_t>(block[2]) |
                (static_cast<std::uint16_t>(block[3]) << 8u);
            const auto decode565 = [](const std::uint16_t value) {
                return std::array<std::uint8_t, 4>{
                    expandPsb5To8(static_cast<std::uint16_t>((value >> 11u) & 0x1fu)),
                    expandPsb6To8(static_cast<std::uint16_t>((value >> 5u) & 0x3fu)),
                    expandPsb5To8(static_cast<std::uint16_t>(value & 0x1fu)),
                    0xffu,
                };
            };
            palette[0] = decode565(color0);
            palette[1] = decode565(color1);
            if(!allowDxt1Transparency || color0 > color1) {
                for(size_t channel = 0; channel < 3; ++channel) {
                    palette[2][channel] = static_cast<std::uint8_t>(
                        (2u * palette[0][channel] + palette[1][channel]) / 3u);
                    palette[3][channel] = static_cast<std::uint8_t>(
                        (palette[0][channel] + 2u * palette[1][channel]) / 3u);
                }
                palette[2][3] = 0xffu;
                palette[3][3] = 0xffu;
            } else {
                for(size_t channel = 0; channel < 3; ++channel) {
                    palette[2][channel] = static_cast<std::uint8_t>(
                        (palette[0][channel] + palette[1][channel]) / 2u);
                    palette[3][channel] = 0u;
                }
                palette[2][3] = 0xffu;
                palette[3][3] = 0u;
            }
        }

        // Decode only the selected icon rectangle from a BC1/DXT1 or
        // BC3/DXT5 atlas. E-mote character PSBs commonly keep a 4096x4096
        // DXT5 texture in source/<group>/texture/pixel and reference small
        // rectangles from source/<group>/icon/*.
        inline bool decodePsbBlockCompressedAtlasRegion(
            const std::vector<std::uint8_t> &compressed,
            const std::string &formatName,
            int atlasWidth,
            int atlasHeight,
            int left,
            int top,
            int width,
            int height,
            std::vector<std::uint8_t> &rgbaOut) {
            const auto format = psbDebugLowercase(formatName);
            const bool isDxt1 = format == "dxt1" || format == "bc1";
            const bool isDxt5 = format == "dxt5" || format == "bc3";
            if((!isDxt1 && !isDxt5) || atlasWidth <= 0 || atlasHeight <= 0 ||
               left < 0 || top < 0 || width <= 0 || height <= 0 ||
               left + width > atlasWidth || top + height > atlasHeight) {
                return false;
            }

            const size_t blockBytes = isDxt5 ? 16u : 8u;
            const size_t blocksWide =
                (static_cast<size_t>(atlasWidth) + 3u) / 4u;
            const size_t blocksHigh =
                (static_cast<size_t>(atlasHeight) + 3u) / 4u;
            if(compressed.size() < blocksWide * blocksHigh * blockBytes) {
                return false;
            }

            rgbaOut.assign(static_cast<size_t>(width) *
                               static_cast<size_t>(height) * 4u,
                           0u);
            const int firstBlockX = left / 4;
            const int firstBlockY = top / 4;
            const int lastBlockX = (left + width - 1) / 4;
            const int lastBlockY = (top + height - 1) / 4;
            for(int blockY = firstBlockY; blockY <= lastBlockY; ++blockY) {
                for(int blockX = firstBlockX; blockX <= lastBlockX; ++blockX) {
                    const size_t blockOffset =
                        (static_cast<size_t>(blockY) * blocksWide +
                         static_cast<size_t>(blockX)) * blockBytes;
                    const auto *block = compressed.data() + blockOffset;
                    const auto *colorBlock = block + (isDxt5 ? 8u : 0u);
                    std::array<std::array<std::uint8_t, 4>, 4> colors{};
                    decodePsbBcColorPalette(colorBlock, isDxt1, colors);
                    const std::uint32_t colorIndices =
                        static_cast<std::uint32_t>(colorBlock[4]) |
                        (static_cast<std::uint32_t>(colorBlock[5]) << 8u) |
                        (static_cast<std::uint32_t>(colorBlock[6]) << 16u) |
                        (static_cast<std::uint32_t>(colorBlock[7]) << 24u);

                    std::array<std::uint8_t, 8> alphas{};
                    std::uint64_t alphaIndices = 0;
                    if(isDxt5) {
                        alphas[0] = block[0];
                        alphas[1] = block[1];
                        if(alphas[0] > alphas[1]) {
                            for(size_t index = 2; index < 8; ++index) {
                                alphas[index] = static_cast<std::uint8_t>(
                                    ((8u - index) * alphas[0] +
                                     (index - 1u) * alphas[1]) / 7u);
                            }
                        } else {
                            for(size_t index = 2; index < 6; ++index) {
                                alphas[index] = static_cast<std::uint8_t>(
                                    ((6u - index) * alphas[0] +
                                     (index - 1u) * alphas[1]) / 5u);
                            }
                            alphas[6] = 0u;
                            alphas[7] = 0xffu;
                        }
                        for(size_t byte = 0; byte < 6; ++byte) {
                            alphaIndices |= static_cast<std::uint64_t>(
                                block[2u + byte]) << (8u * byte);
                        }
                    }

                    for(int localY = 0; localY < 4; ++localY) {
                        const int atlasY = blockY * 4 + localY;
                        if(atlasY < top || atlasY >= top + height) continue;
                        for(int localX = 0; localX < 4; ++localX) {
                            const int atlasX = blockX * 4 + localX;
                            if(atlasX < left || atlasX >= left + width) continue;
                            const int blockPixel = localY * 4 + localX;
                            const auto colorIndex = static_cast<size_t>(
                                (colorIndices >> (2u * blockPixel)) & 0x3u);
                            const size_t outOffset =
                                (static_cast<size_t>(atlasY - top) *
                                     static_cast<size_t>(width) +
                                 static_cast<size_t>(atlasX - left)) * 4u;
                            rgbaOut[outOffset + 0u] = colors[colorIndex][0];
                            rgbaOut[outOffset + 1u] = colors[colorIndex][1];
                            rgbaOut[outOffset + 2u] = colors[colorIndex][2];
                            rgbaOut[outOffset + 3u] = isDxt5
                                ? alphas[static_cast<size_t>(
                                      (alphaIndices >> (3u * blockPixel)) & 0x7u)]
                                : colors[colorIndex][3];
                        }
                    }
                }
            }
            return true;
        }

        constexpr double kMotionFramesPerMillisecond = 60.0 / 1000.0;

        inline std::string basenameWithoutExtension(const std::string &value) {
            const auto slash = value.find_last_of("/\\");
            const auto fileName =
                slash == std::string::npos ? value : value.substr(slash + 1);
            const auto dot = fileName.find_last_of('.');
            return dot == std::string::npos ? fileName : fileName.substr(0, dot);
        }

        inline std::shared_ptr<detail::MotionSnapshot>
        cacheMotion(detail::PlayerRuntime &runtime, const std::string &requestKey,
                    const std::string &resolvedKey,
                    const std::shared_ptr<detail::MotionSnapshot> &snapshot) {
            if(!snapshot) {
                return nullptr;
            }
            if(!requestKey.empty()) {
                runtime.motionsByKey.emplace(requestKey, snapshot);
            }
            if(!resolvedKey.empty()) {
                runtime.motionsByKey.emplace(resolvedKey, snapshot);
            }
            if(!snapshot->path.empty()) {
                runtime.motionsByKey.emplace(snapshot->path, snapshot);
            }
            return snapshot;
        }

        inline bool splitEmoteCandidateBase(const ttstr &candidate,
                                            std::string &base) {
            std::string storage = detail::narrow(
                TVPExtractStorageName(candidate));
            if(storage.empty()) {
                storage = detail::narrow(candidate);
                if(const auto slash = storage.find_last_of("/\\");
                   slash != std::string::npos) {
                    storage = storage.substr(slash + 1);
                }
            }
            storage = psbDebugLowercase(std::move(storage));
            const auto stripSuffix = [](std::string &value,
                                        const std::string &suffix) {
                if(value.size() < suffix.size() ||
                   value.compare(value.size() - suffix.size(), suffix.size(),
                                 suffix) != 0) {
                    return false;
                }
                value.resize(value.size() - suffix.size());
                return true;
            };
            if(!stripSuffix(storage, ".mtn") &&
               !stripSuffix(storage, ".psb")) {
                stripSuffix(storage, ".mt");
            }
            if(storage.size() <= 3 ||
               storage.compare(storage.size() - 3, 3, "emo") != 0) {
                return false;
            }
            base = storage.substr(0, storage.size() - 3);
            return !base.empty();
        }

        inline bool motionSnapshotHasTimelineSuffix(
            const detail::MotionSnapshot &snapshot,
            const std::string &loweredSuffix) {
            const auto matches = [&loweredSuffix](const std::string &label) {
                const auto lowered = psbDebugLowercase(label);
                const auto emoteSuffix = loweredSuffix + "emo";
                return lowered == loweredSuffix || lowered == emoteSuffix ||
                    (lowered.size() > loweredSuffix.size() &&
                     lowered.compare(lowered.size() - loweredSuffix.size(),
                                     loweredSuffix.size(), loweredSuffix) == 0) ||
                    (lowered.size() > emoteSuffix.size() &&
                     lowered.compare(lowered.size() - emoteSuffix.size(),
                                     emoteSuffix.size(), emoteSuffix) == 0);
            };
            return std::any_of(snapshot.mainTimelineLabels.begin(),
                               snapshot.mainTimelineLabels.end(), matches) ||
                std::any_of(snapshot.diffTimelineLabels.begin(),
                            snapshot.diffTimelineLabels.end(), matches) ||
                std::any_of(snapshot.clipsByLabel.begin(),
                            snapshot.clipsByLabel.end(),
                            [&matches](const auto &entry) {
                                return matches(entry.first);
                            });
        }

        inline std::shared_ptr<detail::MotionSnapshot>
        fallbackSplitEmoteMotion(const ResourceManager &resourceManager,
                                 const ttstr &candidate) {
            std::string base;
            if(!splitEmoteCandidateBase(candidate, base)) {
                return nullptr;
            }
            const auto snapshot = detail::lookupModuleSnapshot(
                resourceManager.getLastLoadedModule());
            if(!snapshot ||
               !motionSnapshotHasTimelineSuffix(*snapshot, base)) {
                return nullptr;
            }
            if(LOGGER) {
                LOGGER->info(
                    "motion resolve split emote candidate from cached module: request={} source={}",
                    candidate.AsStdString(), snapshot->path);
            }
            return snapshot;
        }

        inline std::shared_ptr<detail::MotionSnapshot>
        activateMotion(detail::PlayerRuntime &runtime,
                       const std::shared_ptr<detail::MotionSnapshot> &snapshot) {
            runtime.clearMotionBitmapCaches();
            runtime.activeMotion = snapshot;
            runtime.timelines.clear();
            runtime.lastExplicitTimelineLabel.clear();
            // Reset persistent node tree so it gets rebuilt for new motion
            runtime.nodes.clear();
            runtime.nodesBuilt = false;
            runtime.nodeLabelMap.clear();
            runtime.yuzuPresentationCenteredOriginConfirmed = false;
            runtime.yuzuPresentationTranslateX = 0.0f;
            runtime.yuzuPresentationTranslateY = 0.0f;
            // The public backend supports the numeric module contract it has
            // always exposed. Optional packages may recognize additional
            // vendor-specific module layouts through the extension seam.
            runtime.isEmoteMode = false;
            if(snapshot && snapshot->root) {
                auto typeVal = (*snapshot->root)["type"];
                if(auto num = std::dynamic_pointer_cast<PSB::PSBNumber>(typeVal)) {
                    runtime.isEmoteMode = (num->getValue<int>() == 1);
                }
                if(!runtime.isEmoteMode) {
                    if(const auto *extension = motionPlayerExtension();
                       extension && extension->detectExtendedEmoteMode) {
                        runtime.isEmoteMode =
                            extension->detectExtendedEmoteMode(*snapshot);
                    }
                }
            }
            if(snapshot) {
                detail::primeTimelineStates(runtime.timelines, *snapshot);
            }
            return snapshot;
        }

        inline std::shared_ptr<detail::MotionSnapshot>
        resolveMotion(detail::PlayerRuntime &runtime, const ttstr &name,
                      const ResourceManager *resourceManager) {
            const auto requestKey = detail::narrow(name);
            if(requestKey.empty()) {
                return nullptr;
            }

            if(const auto it = runtime.motionsByKey.find(requestKey);
               it != runtime.motionsByKey.end()) {
                return it->second;
            }

            const auto candidates = detail::buildMotionLookupCandidates(name);
            ttstr resolved;
            if(detail::resolveExistingPath(candidates, resolved)) {
                const auto resolvedKey = detail::narrow(resolved);
                if(const auto it = runtime.motionsByKey.find(resolvedKey);
                   it != runtime.motionsByKey.end()) {
                    runtime.motionsByKey.emplace(requestKey, it->second);
                    return it->second;
                }

                // KAG commonly preloads a module through ResourceManager and
                // then asks a newly-created Player to bind the same storage.
                // Prefer that shared immutable snapshot.  Parsing here first
                // made rapid dialogue advancement rescan large E-mote PSBs on
                // the main/render thread even though the module was cached.
                if(resourceManager != nullptr) {
                    const auto loaded =
                        resourceManager->findLoadedModule(resolved);
                    if(const auto snapshot =
                           detail::lookupModuleSnapshot(loaded)) {
                        resourceManager->rememberLoadedModule(
                            name, snapshot->moduleValue);
                        return cacheMotion(runtime, requestKey, resolvedKey,
                                           snapshot);
                    }
                }

                const auto snapshot = detail::loadMotionSnapshot(
                    resolved, ResourceManager::getEmotePSBDecryptSeed());
                if(snapshot) {
                    if(resourceManager != nullptr) {
                        resourceManager->rememberLoadedModule(
                            resolved, snapshot->moduleValue);
                        resourceManager->rememberLoadedModule(
                            name, snapshot->moduleValue);
                    }
                    return cacheMotion(runtime, requestKey, resolvedKey, snapshot);
                }
            }

            if(resourceManager != nullptr) {
                for(const auto &candidate : candidates) {
                    if(const auto loaded =
                           resourceManager->findLoadedModule(candidate);
                       loaded.Type() == tvtObject) {
                        if(const auto snapshot =
                               detail::lookupModuleSnapshot(loaded)) {
                            return cacheMotion(runtime, requestKey,
                                               detail::narrow(candidate),
                                               snapshot);
                        }
                    }
                    if(const auto snapshot = fallbackSplitEmoteMotion(
                           *resourceManager, candidate)) {
                        resourceManager->rememberLoadedModule(
                            candidate, snapshot->moduleValue);
                        return cacheMotion(runtime, requestKey,
                                           detail::narrow(candidate),
                                           snapshot);
                    }
                    if(TVPGetPlacedPath(candidate).IsEmpty()) {
                        continue;
                    }
                    const auto loaded = resourceManager->load(candidate);
                    if(const auto snapshot = detail::lookupModuleSnapshot(loaded)) {
                        return cacheMotion(runtime, requestKey,
                                           detail::narrow(candidate), snapshot);
                    }
                }
            }

            return nullptr;
        }

        inline std::vector<ttstr> buildSourceCandidates(
            const detail::PlayerRuntime &runtime, const ttstr &name) {
            std::vector<ttstr> candidates;
            if(name.IsEmpty()) {
                return candidates;
            }

            candidates.push_back(name);
            const auto requestKey = detail::narrow(name);
            if(!runtime.activeMotion) {
                return candidates;
            }

            const auto baseDir = TVPExtractStoragePath(
                detail::widen(runtime.activeMotion->path));
            for(const auto &candidate : runtime.activeMotion->sourceCandidates) {
                if(candidate == requestKey ||
                   basenameWithoutExtension(candidate) == requestKey) {
                    candidates.emplace_back(detail::widen(candidate));
                    detail::appendEmbeddedSourceCandidates(
                        *runtime.activeMotion, candidate, candidates);
                    if(!baseDir.IsEmpty() &&
                       candidate.find('/') == std::string::npos &&
                       candidate.find('\\') == std::string::npos) {
                        candidates.emplace_back(baseDir + detail::widen(candidate));
                    }
                }
            }

            return candidates;
        }

        inline std::vector<tTJSVariant>
        timelineInfoVariants(const detail::PlayerRuntime &runtime) {
            std::vector<tTJSVariant> items;
            for(const auto &label : runtime.playingTimelineLabels) {
                const auto it = runtime.timelines.find(label);
                if(it == runtime.timelines.end() || !it->second.playing) {
                    continue;
                }
                const auto &state = it->second;

                items.push_back(detail::makeDictionary({
                    { "label", detail::widen(label) },
                    { "flags", static_cast<tjs_int>(state.flags) },
                    { "loop", state.loop },
                    { "playing", state.playing },
                    { "currentTime", state.currentTime },
                    { "totalFrames", state.totalFrames },
                    { "blendRatio", state.blendRatio },
                }));
            }
            return items;
        }

        inline const detail::TimelineState *
        nthPlayingTimeline(const detail::PlayerRuntime &runtime, tjs_int idx) {
            if(idx < 0) {
                return nullptr;
            }
            if(static_cast<size_t>(idx) >= runtime.playingTimelineLabels.size()) {
                return nullptr;
            }
            const auto it =
                runtime.timelines.find(runtime.playingTimelineLabels[idx]);
            return it != runtime.timelines.end() ? &it->second : nullptr;
        }

        inline bool getObjectProperty(const tTJSVariant &object, const tjs_char *name,
                               tTJSVariant &result) {
            result.Clear();
            if(object.Type() != tvtObject || object.AsObjectNoAddRef() == nullptr) {
                return false;
            }
            return TJS_SUCCEEDED(object.AsObjectNoAddRef()->PropGet(
                TJS_IGNOREPROP, name, nullptr, &result,
                object.AsObjectNoAddRef()));
        }

        inline tjs_int getObjectCount(const tTJSVariant &object) {
            if(object.Type() != tvtObject || object.AsObjectNoAddRef() == nullptr) {
                return 0;
            }

            tjs_int count = 0;
            return TJS_SUCCEEDED(object.AsObjectClosureNoAddRef().GetCount(
                       &count, nullptr, nullptr, nullptr))
                ? count
                : 0;
        }

        inline bool tryGetLayerObject(const tTJSVariant &value,
                               tTJSNI_BaseLayer *&layer) {
            layer = nullptr;
            if(value.Type() != tvtObject || value.AsObjectNoAddRef() == nullptr) {
                return false;
            }

            auto tryDispatch = [&](iTJSDispatch2 *obj) {
                if(!obj) {
                    return false;
                }
                layer = nullptr;
                return TJS_SUCCEEDED(obj->NativeInstanceSupport(
                           TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                           reinterpret_cast<iTJSNativeInstance **>(&layer))) &&
                    layer != nullptr;
            };

            iTJSDispatch2 *obj = value.AsObjectNoAddRef();
            if(tryDispatch(obj)) {
                return true;
            }

            // Fallback: try both closure sides. Some TJS calls pass a closure
            // whose Object is a method/class dispatch while ObjThis is the
            // actual Layer instance.
            const auto closure = value.AsObjectClosureNoAddRef();
            if(closure.Object && closure.Object != obj) {
                if(tryDispatch(closure.Object)) {
                    return true;
                }
            }
            if(closure.ObjThis && closure.ObjThis != obj &&
               closure.ObjThis != closure.Object) {
                if(tryDispatch(closure.ObjThis)) {
                    return true;
                }
            }

            return false;
        }

        // Resolve a real Layer dispatch from a TJS value that might be
        // a SeparateLayerAdaptor, an AffineLayer wrapper, or a raw Layer.
        inline iTJSDispatch2 *tryResolveLayerDispatch(const tTJSVariant &value) {
            if(value.Type() != tvtObject || value.AsObjectNoAddRef() == nullptr) {
                return nullptr;
            }

            iTJSDispatch2 *obj = value.AsObjectNoAddRef();
            const auto closure = value.AsObjectClosureNoAddRef();
            iTJSDispatch2 *candidates[] = {
                obj,
                closure.ObjThis,
                closure.Object,
                nullptr,
            };

            // Direct Layer check
            for(auto *candidate : candidates) {
                if(!candidate) {
                    continue;
                }
                tTJSNI_BaseLayer *layer = nullptr;
                if(TJS_SUCCEEDED(candidate->NativeInstanceSupport(
                       TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                       reinterpret_cast<iTJSNativeInstance **>(&layer))) &&
                   layer) {
                    return candidate;
                }
            }

            // ncb SeparateLayerAdaptor → owner
            for(auto *candidate : candidates) {
                if(!candidate) {
                    continue;
                }
                if(auto *adaptor =
                       ncbInstanceAdaptor<SeparateLayerAdaptor>::GetNativeInstance(
                           candidate, false)) {
                    auto *ownerObj = adaptor->getOwner();
                    if(ownerObj) {
                        auto ownerResolved = tryResolveLayerDispatch(
                            tTJSVariant(ownerObj, ownerObj));
                        if(ownerResolved) return ownerResolved;
                    }
                }
            }

            static const tjs_char *explicitLayerProps[] = {
                TJS_W("targetLayer"), TJS_W("_targetLayer"),
                TJS_W("renderTarget"), TJS_W("_renderTarget"),
                TJS_W("layer"), TJS_W("_layer"), TJS_W("baseLayer"),
                TJS_W("_base"), TJS_W("base"), TJS_W("fore"),
                TJS_W("back"), TJS_W("primaryLayer"), nullptr };
            static const tjs_char *ownerLayerProps[] = {
                TJS_W("owner"), TJS_W("_owner"), TJS_W("parent"), nullptr };

            auto tryProps = [&](const tjs_char *const *propNames) -> iTJSDispatch2 * {
                for(int i = 0; propNames[i]; ++i) {
                    tTJSVariant propVal;
                    if(getObjectProperty(value, propNames[i], propVal) &&
                       propVal.Type() == tvtObject &&
                       propVal.AsObjectNoAddRef() != nullptr &&
                       propVal.AsObjectNoAddRef() != obj) {
                        auto *resolved = tryResolveLayerDispatch(propVal);
                        if(resolved) return resolved;
                    }
                }
                return nullptr;
            };

            if(auto *resolved = tryProps(explicitLayerProps)) return resolved;
            if(auto *resolved = tryProps(ownerLayerProps)) return resolved;

            return nullptr;
        }

        inline bool isYuzuTitlePresentationMotionPath(
            const std::string &motionPath) {
            const auto motion = psbDebugLowercase(motionPath);
            return motion.find("title_bg") != std::string::npos ||
                motion.find("titlebg") != std::string::npos;
        }

        inline iTJSDispatch2 *resolveYuzuTitlePresentationTargetFromLayerTree(
            const std::string &motionPath) {
            if(!isYuzuTitlePresentationMotionPath(motionPath) ||
               !TVPMainWindow || !TVPMainWindow->GetDrawDevice()) {
                return nullptr;
            }

            auto *primary = TVPMainWindow->GetDrawDevice()->GetPrimaryLayer();
            if(!primary || !primary->GetManager()) {
                return nullptr;
            }

            auto &nodes = primary->GetManager()->GetAllNodes();
            tTJSNI_BaseLayer *titleBg = nullptr;
            tTJSNI_BaseLayer *sysCover = nullptr;
            tTJSNI_BaseLayer *looseTitleBg = nullptr;
            for(auto *node : nodes) {
                if(!node || !node->GetOwnerNoAddRef()) {
                    continue;
                }
                const auto rawName = node->GetName().AsStdString();
                const auto lowerName = psbDebugLowercase(rawName);
                if(rawName == "title_bg") {
                    titleBg = node;
                    break;
                }
                if(!sysCover && rawName == "SysCoverLayer") {
                    sysCover = node;
                }
                if(!looseTitleBg &&
                   lowerName.find("title") != std::string::npos &&
                   lowerName.find("bg") != std::string::npos) {
                    looseTitleBg = node;
                }
            }

            auto *best = titleBg ? titleBg : (sysCover ? sysCover : looseTitleBg);
            if(!best) {
                return nullptr;
            }
            const char *debug = std::getenv("AETHERKIRI_MOTION_DEBUG");
            if(LOGGER && debug && *debug && std::strcmp(debug, "0") != 0) {
                LOGGER->info(
                    "motion yuzu title target fallback: motion={} layer={} visible={} parentVisible={} opacity={}",
                    motionPath, best->GetName().AsStdString(),
                    best->GetVisible() ? 1 : 0,
                    best->GetParentVisible() ? 1 : 0,
                    static_cast<int>(best->GetOpacity()));
            }
            return best->GetOwnerNoAddRef();
        }

        inline iTJSDispatch2 *tryResolveSeparateAdaptorOwner(const tTJSVariant &value) {
            return tryResolveLayerDispatch(value);
        }

        inline void pushGraphicCandidates(std::vector<ttstr> &candidates,
                                   const ttstr &base) {
            if(base.IsEmpty()) {
                return;
            }

            candidates.push_back(base);
            const auto raw = detail::narrow(base);
            if(raw.find('.') != std::string::npos) {
                return;
            }

            static const char *exts[] = { ".png",  ".webp", ".jpg", ".jpeg",
                                          ".bmp",  ".tlg",  ".pimg", ".psb" };
            for(const auto *ext : exts) {
                candidates.emplace_back(base + ttstr{ ext });
            }
        }

        inline bool getArrayItem(const tTJSVariant &object, tjs_int index,
                          tTJSVariant &result) {
            result.Clear();
            if(object.Type() != tvtObject || object.AsObjectNoAddRef() == nullptr) {
                return false;
            }
            return TJS_SUCCEEDED(object.AsObjectNoAddRef()->PropGetByNum(
                TJS_IGNOREPROP, index, &result, object.AsObjectNoAddRef()));
        }

        struct DictionaryEnumerator : public tTJSDispatch {
            std::vector<std::pair<ttstr, tTJSVariant>> entries;

            tjs_error FuncCall(tjs_uint32, const tjs_char *, tjs_uint32 *,
                               tTJSVariant *result, tjs_int numparams,
                               tTJSVariant **param,
                               iTJSDispatch2 *) override {
                if(numparams < 3) {
                    return TJS_E_BADPARAMCOUNT;
                }

                const tjs_uint32 flags = static_cast<tjs_uint32>(
                    param[1]->AsInteger());
                if(flags & TJS_HIDDENMEMBER) {
                    if(result) {
                        *result = static_cast<tjs_int>(1);
                    }
                    return TJS_S_OK;
                }

                entries.emplace_back(ttstr(*param[0]), *param[2]);
                if(result) {
                    *result = static_cast<tjs_int>(1);
                }
                return TJS_S_OK;
            }
        };

        // Bezier curve control points for easing.
        // Aligned to libkrkr2.so sub_69A754: PSB stores "x" and "y" arrays
        // in the curve data dict. Each array has 3*N+1 entries (N cubic segments).
        struct BezierCurve {
            std::vector<double> x;  // time control points
            std::vector<double> y;  // value control points
            bool empty() const { return x.empty(); }
        };

        // Control point curve for spline rotation (sub_698454).
        // PSB "cp" key stores nested structure: x, y, t arrays + s[] segments.
        // Each segment has x, y, p sub-arrays for cubic spline interpolation.
        struct SplineSegment {
            std::vector<double> x;  // breakpoints
            std::vector<double> y;  // values
            std::vector<double> p;  // spline parameters
        };
        struct ControlPointCurve {
            std::vector<double> x;  // main bezier X control points (3N+1)
            std::vector<double> y;  // main bezier Y control points (3N+1)
            std::vector<double> t;  // time knot points
            std::vector<SplineSegment> s;  // per-segment spline data
            bool empty() const { return t.empty(); }
        };

        struct FrameContentState {
            bool visible = false;
            int frameType = 0;        // frame["type"] from sub_6926B4: 0/2/3
            std::string src;
            std::string motionIcon;   // E-mote object-group motion ("icon")
            std::vector<std::string> srcList;  // For particle nodes: array of "chara/motion" paths
            double x = 0.0;
            double y = 0.0;
            double z = 0.0;
            double ox = 0.0;          // mask 0x1: position offset X
            double oy = 0.0;          // mask 0x1: position offset Y
            double width = 0.0;       // "zx" from PSB: display width in pixels
            double height = 0.0;      // "zy" from PSB: display height in pixels
            double opacity = 1.0;     // mask 0x400: 0.0-1.0 (from "opa" uint8 0-255)
            double angle = 0.0;       // mask 0x10: rotation degrees
            double scaleX = 1.0;      // mask 0x20: zoom X ("z")
            double scaleY = 1.0;      // mask 0x40: zoom Y ("zy" in clip context)
            double slantX = 0.0;      // mask 0x80: slant X ("s")
            double slantY = 0.0;      // mask 0x100: slant Y ("sy")
            bool flipX = false;       // mask 0x4: "fx"
            bool flipY = false;       // mask 0x8: "fy"
            int blendMode = 16;       // mask 0x20000: "bm"/"b" (default 16)
            // Aligned to sub_692AB0 (0x692F4C..0x693428):
            // clip+72..84 stores four packed RGBA DWORDs. Default is
            // vdupq_n_s32(0xFF808080), not four scalar channels.
            std::array<std::uint32_t, 4> packedColors{
                0xFF808080u, 0xFF808080u, 0xFF808080u, 0xFF808080u
            };
            BezierCurve ccc;          // mask 0x800: color curve control
            BezierCurve acc;          // mask 0x1000: angle curve control
            BezierCurve zcc;          // mask 0x2000: zoom curve control
            BezierCurve scc;          // mask 0x4000: slant curve control
            BezierCurve occ;          // mask 0x8000: opacity curve control
            BezierCurve cc;           // position curve (slot+296, "cc" PSB key)
            ControlPointCurve cp;     // rotation spline (slot+268, "cp" PSB key)
            // mask 0x02000000: E-mote BezierPatch payload. Native slots keep
            // the mesh easing curve at +296 and 32 normalized XY floats at
            // +320. Keep it separate from the legacy top-level cc field so
            // ordinary motion position easing is unaffected.
            bool hasMeshPayload = false;
            BezierCurve meshCc;
            std::vector<float> meshControlPoints;
            // === Subsystem data (mask 0x80000+) ===
            // mask 0x80000: motion sub-object (sub_692AB0 at 0x6938CC)
            int motionMask = 0;
            int motionFlags = 0;
            int motionDt = 0;
            bool motionDocmpl = false;
            double motionDofst = 0.0;
            std::string motionDtgt;    // mask 0x80000, sub-mask 0x10: target node name
            double motionTimeOffset = 0.0;
            // mask 0x100000: particle sub-object (sub_692AB0 at 0x693C64)
            int prtTrigger = 0;
            double prtFmin = 10.0;
            double prtF = 10.0;
            double prtVmin = 0.0;
            double prtV = 0.0;
            double prtAmin = 0.0;
            double prtA = 0.0;
            double prtZmin = 1.0;
            double prtZ = 1.0;
            double prtRange = 0.0;
            // mask 0x200000: camera sub-object (sub_692AB0 at 0x693EF0)
            double cameraFactor = 0.0;
            // mask 0x800000: anchor sub-object (sub_692AB0 at 0x694020)
            // target is a string ref to another node
            // mask 0x1000000: model sub-object (sub_692AB0 at 0x693AE8)
            double modelTimeOffset = 0.0;
            bool modelLoop = false;
            int modelDt = 0;
            // mask 0x8000000: feedback sub-object (sub_692AB0 at 0x694130)
            double feedbackTimespan = 0.0;
            // Transform order (default [0,1,2,3] = Flip,Angle,Zoom,Slant)
            int transformOrder[4] = {0, 1, 2, 3};
            bool hasTransformOrder = false;
            std::string action;       // "content.action" from PSB frameList
            bool hasSync = false;     // "content.sync" from PSB frameList
            // Clip slot timing — slot+328 in libkrkr2.so (frame start time)
            double clipStartTime = 0.0;
            // Gated diagnostics for frame selection vs dual-slot interpolation.
            bool debugEvaluated = false;
            int debugActiveIndex = -1;
            int debugNextIndex = -1;
            int debugFrameAType = 0;
            int debugFrameBType = 0;
            bool debugFrameAInvisible = false;
            bool debugFrameBInvisible = false;
            bool debugInterpolated = false;
            double debugFrameATime = 0.0;
            double debugFrameBTime = 0.0;
            double debugInterpT = 0.0;
            double debugFrameAOpacity = 1.0;
            double debugFrameBOpacity = 1.0;
            double debugFrameAScaleX = 1.0;
            double debugFrameAScaleY = 1.0;
            double debugFrameBScaleX = 1.0;
            double debugFrameBScaleY = 1.0;
            std::string debugFrameASrc;
            std::string debugFrameBSrc;
        };

        inline std::optional<double>
        psbNumberValue(const std::shared_ptr<PSB::IPSBValue> &value) {
            if(auto number = std::dynamic_pointer_cast<PSB::PSBNumber>(value)) {
                switch(number->numberType) {
                    case PSB::PSBNumberType::Float:
                        return number->getValue<float>();
                    case PSB::PSBNumberType::Double:
                        return number->getValue<double>();
                    case PSB::PSBNumberType::Int:
                        return static_cast<double>(number->getValue<int>());
                    case PSB::PSBNumberType::Long:
                    default:
                        return static_cast<double>(number->getValue<tjs_int64>());
                }
            }
            if(auto boolean = std::dynamic_pointer_cast<PSB::PSBBool>(value)) {
                return boolean->value ? 1.0 : 0.0;
            }
            return std::nullopt;
        }

        inline const std::array<float, 32> &identityMeshControlPoints() {
            static const std::array<float, 32> points = [] {
                std::array<float, 32> value{};
                for(int y = 0; y < 4; ++y) {
                    for(int x = 0; x < 4; ++x) {
                        const int index = (y * 4 + x) * 2;
                        value[index] = static_cast<float>(x) / 3.0f;
                        value[index + 1] = static_cast<float>(y) / 3.0f;
                    }
                }
                return value;
            }();
            return points;
        }

        template<typename ResolveVariable>
        inline bool evaluateMeshCombinators(
            std::vector<detail::MotionNode::MeshCombinatorEntry> &entries,
            ResolveVariable &&resolveVariable,
            std::vector<float> &controlPoints) {
            constexpr std::size_t kPatchFloatCount = 32;
            bool havePatch = false;
            bool modified = false;
            for(auto &entry : entries) {
                if(entry.meshCount <= 0 ||
                   entry.rawMeshes.size() <
                       static_cast<std::size_t>(entry.meshCount) *
                           kPatchFloatCount) {
                    continue;
                }

                double neutralValue = entry.rangeBegin;
                if(entry.meshCount > 1) {
                    neutralValue +=
                        (entry.rangeEnd - entry.rangeBegin) *
                        std::clamp(entry.neutralIndex, 0,
                                   entry.meshCount - 1) /
                        static_cast<double>(entry.meshCount - 1);
                }
                const double rawValue = resolveVariable(entry.variable,
                                                        neutralValue);
                const double range = entry.rangeEnd - entry.rangeBegin;
                const double normalized = std::abs(range) > 0.0000001
                    ? std::clamp((rawValue - entry.rangeBegin) / range,
                                 0.0, 1.0)
                    : 0.0;
                const double meshPosition =
                    normalized * static_cast<double>(entry.meshCount - 1);
                const float nativeMeshPosition =
                    static_cast<float>(meshPosition);
                havePatch = true;
                // libartemis.so 0x6D9A84 and compatible-v2 0x6DB8A8 use an
                // exact float comparison here.  The sampled Bezier patch and
                // its layer sum survive until a controller actually moves.
                if(entry.sampledPatchValid &&
                   entry.sampledPosition == nativeMeshPosition) {
                    continue;
                }
                const int meshA = std::clamp(
                    static_cast<int>(nativeMeshPosition), 0,
                    entry.meshCount - 1);
                const int meshB = std::min(meshA + 1,
                                           entry.meshCount - 1);
                const float ratio = nativeMeshPosition - meshA;
                const std::size_t offsetA =
                    static_cast<std::size_t>(meshA) * kPatchFloatCount;
                const std::size_t offsetB =
                    static_cast<std::size_t>(meshB) * kPatchFloatCount;
                for(std::size_t point = 0; point < kPatchFloatCount;
                    ++point) {
                    const double valueA = entry.rawMeshes[offsetA + point];
                    const double valueB = entry.rawMeshes[offsetB + point];
                    entry.sampledPatch[point] = static_cast<float>(
                        valueA * (1.0 - ratio) + valueB * ratio);
                }
                entry.sampledPosition = nativeMeshPosition;
                entry.sampledPatchValid = true;
                modified = true;
            }
            if(!havePatch) {
                controlPoints.clear();
                return false;
            }
            if(!modified && controlPoints.size() == kPatchFloatCount) {
                return true;
            }
            // The binary chooses SumAllMesh when at least half of a layer's
            // operators changed and DiffModifiedMesh otherwise.  A portable
            // node rarely has more than a handful of 32-float contributors;
            // summing their retained samples gives the same result while
            // avoiding a second mutable patch and all raw-resource reads.
            std::array<float, kPatchFloatCount> combined{};
            for(const auto &entry : entries) {
                if(!entry.sampledPatchValid) continue;
                for(std::size_t point = 0; point < kPatchFloatCount; ++point) {
                    combined[point] += entry.sampledPatch[point];
                }
            }
            controlPoints.assign(combined.begin(), combined.end());
            return true;
        }

        inline std::optional<double>
        psbDictionaryNumber(const std::shared_ptr<const PSB::PSBDictionary> &dic,
                            const char *key) {
            if(!dic) {
                return std::nullopt;
            }
            return psbNumberValue((*dic)[key]);
        }

        inline std::string
        psbDictionaryString(const std::shared_ptr<const PSB::PSBDictionary> &dic,
                            const char *key) {
            if(!dic) {
                return {};
            }
            if(auto text =
                   std::dynamic_pointer_cast<PSB::PSBString>((*dic)[key])) {
                return text->value;
            }
            return {};
        }

        inline std::shared_ptr<PSB::PSBList>
        psbDictionaryList(const std::shared_ptr<const PSB::PSBDictionary> &dic,
                          const char *key) {
            if(!dic) {
                return nullptr;
            }
            return std::dynamic_pointer_cast<PSB::PSBList>((*dic)[key]);
        }

        // Parse a BezierCurve from a PSB dict that has "x" and "y" list children.
        // Aligned to libkrkr2.so sub_69A754 (0x69A754): reads curve_data["x"]
        // and curve_data["y"] as arrays of doubles.
        inline BezierCurve parseBezierCurve(
            const std::shared_ptr<const PSB::PSBDictionary> &dic) {
            BezierCurve curve;
            if(!dic) return curve;
            auto xList = std::dynamic_pointer_cast<PSB::PSBList>((*dic)["x"]);
            auto yList = std::dynamic_pointer_cast<PSB::PSBList>((*dic)["y"]);
            if(!xList || !yList) return curve;
            for(int i = 0; i < static_cast<int>(xList->size()); i++) {
                if(auto v = psbNumberValue((*xList)[i])) curve.x.push_back(*v);
            }
            for(int i = 0; i < static_cast<int>(yList->size()); i++) {
                if(auto v = psbNumberValue((*yList)[i])) curve.y.push_back(*v);
            }
            return curve;
        }

        // Evaluate cubic bezier curve at parameter t.
        // Aligned to libkrkr2.so sub_69A754 (0x69A754):
        //   - x[] = time control points, y[] = value control points
        //   - Segments of 4 control points each (step 3, shared endpoints)
        //   - If t <= x[0]: return y[0]
        //   - If t >= x[last]: return y[last]
        //   - Find segment where x[i] >= t (step 3)
        //   - B(t) = (1-t)³P0 + 3(1-t)²tP1 + 3(1-t)t²P2 + t³P3
        template<typename CurveT>
        inline double evaluateBezierCurve(const CurveT &curve, double t) {
            if(curve.x.size() < 2 || curve.y.size() < 2) return t;
            if(curve.x.size() != curve.y.size()) return t;
            const size_t n = curve.x.size();
            if(curve.x[0] >= t) return curve.y[0];
            if(curve.x[n-1] <= t) return curve.y[n-1];
            // Find segment (step 3, aligned to sub_69A754 at 0x69A960)
            size_t i = 0;
            while(i < n && curve.x[i] < t) i += 3;
            if(i < 3 || i >= n) return t;
            // Cubic bezier: P0=y[i-3], P1=y[i-2], P2=y[i-1], P3=y[i]
            const double p0 = curve.y[i-3];
            const double p1 = curve.y[i-2];
            const double p2 = curve.y[i-1];
            const double p3 = curve.y[i];
            const double u = 1.0 - t;
            return u*u*u*p0 + 3.0*u*u*t*p1 + 3.0*u*t*t*p2 + t*t*t*p3;
        }

        // sub_698454 equivalent: evaluate control point curve for rotation.
        // Returns 2D point (cos/sin pair) for rotation interpolation.
        inline void evaluateControlPointCurve(
            double outXY[2], const ControlPointCurve &cp, double inputT) {
            if (cp.t.size() < 2 || cp.x.size() < 4 || cp.y.size() < 4) return;
            // Step 1: find segment in t[] where t[i+1] >= inputT (0x698720..0x698744)
            int segIdx = 0;
            int mainIdx = 0;
            for (size_t i = 1; i < cp.t.size(); ++i) {
                mainIdx += 3;
                if (cp.t[i] >= inputT) { segIdx = static_cast<int>(i) - 1; break; }
                segIdx = static_cast<int>(i) - 1;
            }
            if (segIdx < 0 || segIdx >= static_cast<int>(cp.s.size())) return;
            // Knot values (0x69875C..0x69878C)
            double tStart = cp.t[segIdx];
            double tEnd = (segIdx + 1 < static_cast<int>(cp.t.size())) ? cp.t[segIdx + 1] : tStart;
            double localT = (tEnd != tStart) ? (inputT - tStart) / (tEnd - tStart) : 0.0;
            // Step 2: evaluate segment spline to get bezier parameter (0x6989D8..0x698B38)
            double param = localT;
            const auto &seg = cp.s[segIdx];
            if (!seg.x.empty() && seg.x.size() == seg.y.size()) {
                double sx0 = seg.x[0];
                if (sx0 >= localT) {
                    param = seg.y[0];
                } else if (seg.x.back() <= localT) {
                    param = seg.y.back();
                } else {
                    // Find sub-segment (step 1, 0x698A18..0x698A38)
                    int subIdx = 0;
                    for (size_t i = 1; i < seg.x.size(); ++i) {
                        if (seg.x[i] >= localT) { subIdx = static_cast<int>(i) - 1; break; }
                        subIdx = static_cast<int>(i) - 1;
                    }
                    if (subIdx >= 0 && subIdx + 1 < static_cast<int>(seg.x.size()) &&
                        subIdx + 1 < static_cast<int>(seg.y.size())) {
                        double x0 = seg.x[subIdx], x1 = seg.x[subIdx + 1];
                        double y0 = seg.y[subIdx], y1 = seg.y[subIdx + 1];
                        double dx = x1 - x0;
                        if (dx != 0.0) {
                            double u = (localT - x0) / dx;
                            double p0 = (subIdx < static_cast<int>(seg.p.size())) ? seg.p[subIdx] : 0.0;
                            double p1 = (subIdx + 1 < static_cast<int>(seg.p.size())) ? seg.p[subIdx + 1] : 0.0;
                            // Cubic spline formula (0x698AF0..0x698B38)
                            param = dx * dx * ((u*u*u - u) * p1 + ((1-u)*(1-u)*(1-u) - (1-u)) * p0) / 6.0
                                  + u * y1 + (1 - u) * y0;
                        }
                    }
                }
            }
            // Step 3: evaluate main cubic bezier with 'param' (0x698BF0..0x698D0C)
            if (mainIdx >= 3 && mainIdx < static_cast<int>(cp.x.size()) &&
                mainIdx < static_cast<int>(cp.y.size())) {
                double px0 = cp.x[mainIdx-3], py0 = cp.y[mainIdx-3];
                double px1 = cp.x[mainIdx-2], py1 = cp.y[mainIdx-2];
                double px2 = cp.x[mainIdx-1], py2 = cp.y[mainIdx-1];
                double px3 = cp.x[mainIdx],   py3 = cp.y[mainIdx];
                double u = 1.0 - param;
                outXY[0] = u*u*u*px0 + 3*u*u*param*px1 + 3*u*param*param*px2 + param*param*param*px3;
                outXY[1] = u*u*u*py0 + 3*u*u*param*py1 + 3*u*param*param*py2 + param*param*param*py3;
            }
        }

        // sub_69A4D4 equivalent: position interpolation with optional easing + rotation.
        // Uses ccc for easing (slot+168) and cp for rotation (slot+268).
        inline void interpolatePosition69A4D4(
            const BezierCurve &easingCurve,         // ccc (slot+168)
            const double dstPos[3],                 // other slot [x,y,z]
            const double srcPos[3],                 // current slot [x,y,z]
            double outPos[3],                       // output
            int coordinateMode,
            const ControlPointCurve &rotationCurve, // cp (slot+268)
            double t) {
            // Skip if positions identical (0x69A52C..0x69A558)
            if (srcPos[0]==dstPos[0] && srcPos[1]==dstPos[1] && srcPos[2]==dstPos[2]) {
                outPos[0]=srcPos[0]; outPos[1]=srcPos[1]; outPos[2]=srcPos[2];
                return;
            }
            // Apply easing (0x69A55C..0x69A56C)
            double et = !easingCurve.empty() ? evaluateBezierCurve(easingCurve, t) : t;
            if (rotationCurve.empty()) {
                // Linear path (0x69A600..0x69A6F8)
                for (int i = 0; i < 3; ++i)
                    outPos[i] = (srcPos[i]!=dstPos[i]) ? srcPos[i]*(1-et)+dstPos[i]*et : srcPos[i];
                return;
            }
            // Rotation path via sub_698454 (0x69A588..0x69A5CC / 0x69A680..0x69A6F0)
            double rot[2] = {1.0, 0.0};
            evaluateControlPointCurve(rot, rotationCurve, et);
            double cosA = rot[0], sinA = rot[1];
            if (coordinateMode == 0) {
                double dx = dstPos[0]-srcPos[0], dy = dstPos[1]-srcPos[1];
                outPos[0] = srcPos[0] + dx*cosA - dy*sinA;
                outPos[1] = srcPos[1] + dx*sinA + dy*cosA;
                outPos[2] = (srcPos[2]!=dstPos[2]) ? srcPos[2]*(1-et)+dstPos[2]*et : srcPos[2];
            } else if (coordinateMode == 1) {
                double dx = dstPos[0]-srcPos[0], dz = dstPos[2]-srcPos[2];
                outPos[0] = srcPos[0] + dx*cosA - dz*sinA;
                outPos[1] = (srcPos[1]!=dstPos[1]) ? srcPos[1]*(1-et)+dstPos[1]*et : srcPos[1];
                outPos[2] = srcPos[2] + dz*cosA + dx*sinA;
            } else {
                outPos[0]=srcPos[0]; outPos[1]=srcPos[1]; outPos[2]=srcPos[2];
            }
        }

        inline std::shared_ptr<PSB::PSBDictionary>
        psbDictionaryValue(const std::shared_ptr<const PSB::PSBDictionary> &dic,
                           const char *key) {
            if(!dic) {
                return nullptr;
            }
            return std::dynamic_pointer_cast<PSB::PSBDictionary>((*dic)[key]);
        }

        inline double activeClipTime(const detail::PlayerRuntime &runtime,
                              const detail::MotionClip *clip) {
            if(clip) {
                if(const auto it = runtime.timelines.find(clip->label);
                   it != runtime.timelines.end()) {
                    return it->second.currentTime;
                }
            }

            for(const auto &label : runtime.playingTimelineLabels) {
                if(const auto it = runtime.timelines.find(label);
                   it != runtime.timelines.end()) {
                    return it->second.currentTime;
                }
            }
            return 0.0;
        }

        inline double motionClipEndTimeLikeKrkrsdl3(
            const detail::MotionClip *clip) {
            if(!clip) {
                return 0.0;
            }
            if(clip->syncTime > 0.0) {
                return clip->syncTime;
            }
            if(clip->selfSyncTime > 0.0) {
                return clip->selfSyncTime;
            }
            return clip->totalFrames;
        }

        inline void mergeFrameContent(const std::shared_ptr<PSB::PSBDictionary> &content,
                               FrameContentState &state,
                               int nodeType) {
            if(!content) {
                return;
            }

            // Aligned to libkrkr2.so sub_692AB0 (0x692AB0):
            // the clip slot is already reset to defaults by sub_69260C/ParsedFrame,
            // then this routine applies only the fields selected by mask bits.
            const int mask = static_cast<int>(
                psbDictionaryNumber(content, "mask").value_or(0));

            // sub_692AB0 gates the "src"/"icon" lookup on ((1 << nodeType) & 0x1849).
            // This covers the initial source-handle block only; ox/oy/coord are handled
            // below by their own mask bits.
            const bool sourceGateEnabled =
                nodeType >= 0 && nodeType < 63 &&
                ((((std::uint64_t)1) << static_cast<unsigned>(nodeType)) &
                 0x1849u) != 0;
            if(sourceGateEnabled) {
                if(const auto src = psbDictionaryString(content, "src"); !src.empty()) {
                    state.src = src;
                } else if(auto srcList = psbDictionaryList(content, "src")) {
                    for(size_t si = 0; si < srcList->size(); ++si) {
                        if(auto s = std::dynamic_pointer_cast<PSB::PSBString>((*srcList)[si])) {
                            state.srcList.push_back(s->value);
                        }
                    }
                    if(!state.srcList.empty()) state.src = state.srcList[0];
                }
                const auto icon = psbDictionaryString(content, "icon");
                if(!icon.empty() && !state.src.empty()) {
                    if(nodeType == 3) {
                        // Motion nodes address a named motion inside the
                        // object group held in src.
                        state.motionIcon = icon;
                    } else if(state.src == "blank") {
                        // E-mote transparent transform sources use
                        // blank/<width>:<height>:<originX>:<originY>.
                        state.src += "/" + icon;
                    } else if(state.src.rfind("src/", 0) == 0) {
                        state.src += "/" + icon;
                    } else {
                        // Image nodes use src as the PSB source group and
                        // icon as the concrete image within that group.
                        state.src = "src/" + state.src + "/" + icon;
                    }
                }
            }

            // mask & 0x1: ox/oy (sub_692AB0 at 0x692DC4)
            if(mask & 0x1) {
                if(const auto ox = psbDictionaryNumber(content, "ox"))
                    state.ox = *ox;
                if(const auto oy = psbDictionaryNumber(content, "oy"))
                    state.oy = *oy;
            }

            // mask & 0x2: coord[x,y,z] (sub_692AB0 at 0x692E14).
            // Binary fetches content["coord"] then reads indices 0/1/2 via sub_6695BC.
            if(mask & 0x2) {
                if(const auto coord = psbDictionaryList(content, "coord")) {
                    if(coord->size() > 0) {
                        if(const auto value = psbNumberValue((*coord)[0]))
                            state.x = *value;
                    }
                    if(coord->size() > 1) {
                        if(const auto value = psbNumberValue((*coord)[1]))
                            state.y = *value;
                    }
                    if(coord->size() > 2) {
                        if(const auto value = psbNumberValue((*coord)[2]))
                            state.z = *value;
                    }
                } else if(auto coordDict = psbDictionaryValue(content, "coord")) {
                    if(auto value = psbDictionaryNumber(coordDict, "0")) state.x = *value;
                    if(auto value = psbDictionaryNumber(coordDict, "1")) state.y = *value;
                    if(auto value = psbDictionaryNumber(coordDict, "2")) state.z = *value;
                }
            }

            // mask & 0x400: opa (sub_692AB0 at 0x693440)
            // CRITICAL: only read "opa" when mask bit 0x400 is set.
            // Default opacity is 255 (1.0) — set in FrameContentState init.
            if(mask & 0x400) {
                if(const auto opa = psbDictionaryNumber(content, "opa"))
                    state.opacity = std::clamp(*opa / 255.0, 0.0, 1.0);
            }

            // mask & 0x10: angle (sub_692AB0 at 0x692FC4)
            if(mask & 0x10) {
                if(const auto angle = psbDictionaryNumber(content, "angle"))
                    state.angle = *angle;
            }

            // mask & 0x4: fx, mask & 0x8: fy (sub_692AB0 at 0x692F6C)
            if(mask & 0xC) {
                if(mask & 0x4) {
                    if(const auto fx = psbDictionaryNumber(content, "fx"))
                        state.flipX = *fx != 0.0;
                }
                if(mask & 0x8) {
                    if(const auto fy = psbDictionaryNumber(content, "fy"))
                        state.flipY = *fy != 0.0;
                }
            }

            // mask & 0x60: scaleX ("zx") / scaleY ("zy")
            // Aligned to libkrkr2.so sub_692AB0 at 0x693018:
            // both keys are read directly from the PSB content dict and stored
            // in the current clip slot. This is required by logo motions whose
            // opening backdrop uses zx/zy magnification on a tiny source image.
            if(mask & 0x60) {
                if(const auto zx = psbDictionaryNumber(content, "zx"))
                    state.scaleX = *zx;
                if(const auto zy = psbDictionaryNumber(content, "zy"))
                    state.scaleY = *zy;
            }

            // mask & 0x80: slantX ("sx") / mask & 0x100: slantY ("sy")
            // Aligned to sub_692AB0 at 0x69306C.
            // 0x14D869A: bytes 73 00 78 00 00 00 = UTF-16LE "sx" (IDA showed "s")
            if(mask & 0x180) {
                if(mask & 0x80) {
                    if(const auto s = psbDictionaryNumber(content, "sx"))
                        state.slantX = *s;
                }
                if(mask & 0x100) {
                    if(const auto sy = psbDictionaryNumber(content, "sy"))
                        state.slantY = *sy;
                }
            }

            // mask & 0x20000: bm/blend mode (sub_692AB0 at 0x692F20)
            if(mask & 0x20000) {
                if(const auto bm = psbDictionaryNumber(content, "bm"))
                    state.blendMode = static_cast<int>(*bm);
            }

            // mask & 0x800: ccc/color curve control (sub_692AB0 at 0x6930DC)
            if(mask & 0x800) {
                if(auto cccDict = psbDictionaryValue(content, "ccc"))
                    state.ccc = parseBezierCurve(cccDict);
            }

            // mask & 0x1000: acc/angle curve control (sub_692AB0 at 0x69319C)
            if(mask & 0x1000) {
                if(auto accDict = psbDictionaryValue(content, "acc"))
                    state.acc = parseBezierCurve(accDict);
            }

            // mask & 0x2000: zcc/zoom curve control (sub_692AB0 at 0x6931FC)
            if(mask & 0x2000) {
                if(auto zccDict = psbDictionaryValue(content, "zcc"))
                    state.zcc = parseBezierCurve(zccDict);
            }

            // mask & 0x4000: scc/slant curve control (sub_692AB0 at 0x69325C)
            if(mask & 0x4000) {
                if(auto sccDict = psbDictionaryValue(content, "scc"))
                    state.scc = parseBezierCurve(sccDict);
            }

            // mask & 0x8000: occ/opacity curve control (sub_692AB0 at 0x69313C)
            if(mask & 0x8000) {
                if(auto occDict = psbDictionaryValue(content, "occ"))
                    state.occ = parseBezierCurve(occDict);
            }

            // "cc" position curve (sub_692AB0 at 0x693580, slot+296)
            // Used by sub_69A4D4 to ease position interpolation t value.
            // Check done AFTER per-property curves, gated by slot+25 (crossfading)
            // and slot+22 (flipX) at 0x6932B8..0x6932C4.
            if(auto ccDict = psbDictionaryValue(content, "cc"))
                state.cc = parseBezierCurve(ccDict);

            // "cp" rotation control points (sub_692AB0 at 0x6932D8, slot+268)
            // Used by sub_698454 for spline rotation interpolation in sub_69A4D4.
            if(auto cpDict = psbDictionaryValue(content, "cp")) {
                auto cpxList = psbDictionaryList(cpDict, "x");
                auto cpyList = psbDictionaryList(cpDict, "y");
                auto cptList = psbDictionaryList(cpDict, "t");
                auto cpsList = psbDictionaryList(cpDict, "s");
                if (cpxList && cpyList && cptList) {
                    for (size_t ci = 0; ci < cpxList->size(); ++ci)
                        if (auto v = psbNumberValue((*cpxList)[ci])) state.cp.x.push_back(*v);
                    for (size_t ci = 0; ci < cpyList->size(); ++ci)
                        if (auto v = psbNumberValue((*cpyList)[ci])) state.cp.y.push_back(*v);
                    for (size_t ci = 0; ci < cptList->size(); ++ci)
                        if (auto v = psbNumberValue((*cptList)[ci])) state.cp.t.push_back(*v);
                    if (cpsList) {
                        for (size_t ci = 0; ci < cpsList->size(); ++ci) {
                            SplineSegment seg;
                            if (auto segDict = std::dynamic_pointer_cast<PSB::PSBDictionary>((*cpsList)[ci])) {
                                if (auto sx = psbDictionaryList(segDict, "x"))
                                    for (size_t si = 0; si < sx->size(); ++si)
                                        if (auto v = psbNumberValue((*sx)[si])) seg.x.push_back(*v);
                                if (auto sy = psbDictionaryList(segDict, "y"))
                                    for (size_t si = 0; si < sy->size(); ++si)
                                        if (auto v = psbNumberValue((*sy)[si])) seg.y.push_back(*v);
                                if (auto sp = psbDictionaryList(segDict, "p"))
                                    for (size_t si = 0; si < sp->size(); ++si)
                                        if (auto v = psbNumberValue((*sp)[si])) seg.p.push_back(*v);
                            }
                            state.cp.s.push_back(std::move(seg));
                        }
                    }
                }
            }

            // E-mote BezierPatch data. libgame.so sub_68FE90 checks mask
            // 0x02000000, reads mesh.cc (fallback mesh.m), then requires
            // mesh.bp (fallback mesh.b) to contain exactly 32 floats.
            if(mask & 0x02000000) {
                state.hasMeshPayload = true;
                if(auto mesh = psbDictionaryValue(content, "mesh")) {
                    auto curve = psbDictionaryValue(mesh, "cc");
                    if(!curve) curve = psbDictionaryValue(mesh, "m");
                    if(curve) state.meshCc = parseBezierCurve(curve);

                    auto points = psbDictionaryList(mesh, "bp");
                    if(!points) points = psbDictionaryList(mesh, "b");
                    if(points && points->size() == 32) {
                        state.meshControlPoints.reserve(32);
                        for(size_t index = 0; index < 32; ++index) {
                            state.meshControlPoints.push_back(static_cast<float>(
                                psbNumberValue((*points)[static_cast<int>(index)])
                                    .value_or(0.0)));
                        }
                    }
                }
            }

            // mask & 0x200: packed color payload (sub_692AB0 at
            // 0x692F4C..0x693428). Binary stores four packed RGBA DWORDs.
            if(mask & 0x200) {
                if(auto colorDict = psbDictionaryValue(content, "color")) {
                    for(int ci = 0; ci < 4; ++ci) {
                        const auto key = std::to_string(ci);
                        if(auto value = psbDictionaryNumber(colorDict, key.c_str())) {
                            state.packedColors[ci] =
                                static_cast<std::uint32_t>(static_cast<std::int64_t>(*value));
                        }
                    }
                } else if(auto colorVal = psbDictionaryNumber(content, "color")) {
                    // Scalar color is broadcast to all four packed slots.
                    const auto packed = static_cast<std::uint32_t>(
                        static_cast<std::int64_t>(*colorVal));
                    state.packedColors = { packed, packed, packed, packed };
                }
            }

            // mask & 0x80000: motion sub-object (sub_692AB0 at 0x6938CC)
            // Full read: mask → flags/dt/docmpl/dofst/dtgt + timeOffset
            if(mask & 0x80000) {
                if(auto md = psbDictionaryValue(content, "motion")) {
                    int mm = static_cast<int>(
                        psbDictionaryNumber(md, "mask").value_or(0));
                    state.motionMask = mm;
                    if(mm & 0x1) {
                        if(auto v = psbDictionaryNumber(md, "flags"))
                            state.motionFlags = static_cast<int>(*v);
                    }
                    if(mm & 0x2) {
                        if(auto v = psbDictionaryNumber(md, "dt"))
                            state.motionDt = static_cast<int>(*v);
                    }
                    if(mm & 0x4) {
                        if(auto v = psbDictionaryNumber(md, "docmpl"))
                            state.motionDocmpl = *v != 0.0;
                    }
                    if(mm & 0x8) {
                        if(auto v = psbDictionaryNumber(md, "dofst"))
                            state.motionDofst = *v;
                    }
                    // dtgt (mm & 0x10): target node name string (sub_692AB0 at 0x693A48)
                    if(mm & 0x10) {
                        auto v = psbDictionaryString(md, "dtgt");
                        if(!v.empty()) state.motionDtgt = v;
                    }
                    if(auto v = psbDictionaryNumber(md, "timeOffset"))
                        state.motionTimeOffset = *v;
                }
            }

            // mask & 0x100000: particle sub-object (sub_692AB0 at 0x693C64)
            if(mask & 0x100000) {
                if(auto pd = psbDictionaryValue(content, "prt")) {
                    int pm = static_cast<int>(
                        psbDictionaryNumber(pd, "mask").value_or(0));
                    if(pm & 0x1) {
                        if(auto v = psbDictionaryNumber(pd, "trigger"))
                            state.prtTrigger = static_cast<int>(*v);
                    }
                    if(pm & 0x2) {
                        if(auto v = psbDictionaryNumber(pd, "fmin"))
                            state.prtFmin = *v;
                        if(auto v = psbDictionaryNumber(pd, "f"))
                            state.prtF = *v;
                    }
                    if(pm & 0x4) {
                        if(auto v = psbDictionaryNumber(pd, "vmin"))
                            state.prtVmin = *v;
                        if(auto v = psbDictionaryNumber(pd, "v"))
                            state.prtV = *v;
                    }
                    if(pm & 0x8) {
                        if(auto v = psbDictionaryNumber(pd, "amin"))
                            state.prtAmin = *v;
                        if(auto v = psbDictionaryNumber(pd, "a"))
                            state.prtA = *v;
                    }
                    if(pm & 0x10) {
                        if(auto v = psbDictionaryNumber(pd, "zmin"))
                            state.prtZmin = *v;
                        if(auto v = psbDictionaryNumber(pd, "z"))
                            state.prtZ = *v;
                    }
                    if(pm & 0x20) {
                        if(auto v = psbDictionaryNumber(pd, "range"))
                            state.prtRange = *v;
                    }
                }
            }

            // mask & 0x200000: camera (sub_692AB0 at 0x693EF0)
            if(mask & 0x200000) {
                if(auto cd = psbDictionaryValue(content, "camera")) {
                    if(auto v = psbDictionaryNumber(cd, "f"))
                        state.cameraFactor = *v;
                    // camera.target is a string ref (sub_529524)
                }
            }

            // mask & 0x800000: anchor (sub_692AB0 at 0x694020)
            if(mask & 0x800000) {
                // anchor.target is a string ref — read via sub_529524
                // No numeric properties to store; the target ref links
                // to another node for position constraint.
            }

            // mask & 0x1000000: model (sub_692AB0 at 0x693AE8)
            if(mask & 0x1000000) {
                if(auto md = psbDictionaryValue(content, "model")) {
                    if(auto v = psbDictionaryNumber(md, "timeOffset"))
                        state.modelTimeOffset = *v;
                    if(auto v = psbDictionaryNumber(md, "loop"))
                        state.modelLoop = *v != 0.0;
                    if(auto v = psbDictionaryNumber(md, "dt"))
                        state.modelDt = static_cast<int>(*v);
                    // model.dtgt is a string ref
                }
            }

            // mask & 0x8000000: feedback (sub_692AB0 at 0x694130)
            if(mask & 0x8000000) {
                if(auto fd = psbDictionaryValue(content, "feedback")) {
                    if(auto v = psbDictionaryNumber(fd, "timespan"))
                        state.feedbackTimespan = *v;
                }
            }

            // action/sync: not mask-gated (separate mechanism via mask & 0x40000
            // in sub_6926B4 at 0x6928EC)
            if(const auto act = psbDictionaryString(content, "action"); !act.empty()) {
                state.action = act;
            }
            if(const auto sync = psbDictionaryNumber(content, "sync")) {
                state.hasSync = *sync != 0.0;
            }
        }

        // Parse a single PSB frame entry: read time, type, content.
        // Aligned to libkrkr2.so sub_6926B4 (0x6926B4):
        //   - Reads frame["time"] (double), frame["type"] (int: 0=invisible, 2=static, 3=interpolate)
        //   - Reads frame["content"]["mask"] and calls sub_692AB0 to populate slot properties
        //   - Reads frame["content"]["act"] (action string, mask & 0x40000)
        struct ParsedFrame {
            double time = 0.0;
            int type = 0;        // 0=invisible, 2=static, 3=interpolate
            bool invisible = true;  // type==0
            bool interpolate = false; // type==3
            FrameContentState slot;  // populated by sub_692AB0 (mergeFrameContent)
        };

        inline ParsedFrame
        parseFrame(const std::shared_ptr<PSB::PSBDictionary> &frame,
                   int nodeType) {
            ParsedFrame result;
            if(!frame) return result;
            result.time = psbDictionaryNumber(frame, "time").value_or(0.0);
            result.type = static_cast<int>(
                psbDictionaryNumber(frame, "type").value_or(0.0));
            result.invisible = (result.type == 0);
            result.interpolate = (result.type == 3);
            result.slot.frameType = result.type;
            if(!result.invisible) {
                // sub_6926B4 at 0x692838: read content dict, then call sub_692AB0
                if(const auto content = psbDictionaryValue(frame, "content")) {
                    mergeFrameContent(content, result.slot, nodeType);
                }
            }
            return result;
        }

        // Initialize a FrameContentState from a single PSB frame's content.
        // Convenience wrapper: calls parseFrame (sub_6926B4) and returns
        // the populated slot. Used by flattenLayerNodes (PSB-tree path).
        inline FrameContentState
        initSlotFromFrame(const std::shared_ptr<PSB::PSBDictionary> &frame,
                          int nodeType) {
            return parseFrame(frame, nodeType).slot;
        }

        // Dual-slot interpolation between two FrameContentStates.
        // Aligned to libkrkr2.so sub_699AE4 (0x699AE4):
        //   - Takes slotA (active frame) and slotB (next frame)
        //   - Interpolation ratio t ∈ [0,1]
        //   - Applies bezier curve easing per property (ccc, acc, zcc, scc, occ)
        //   - Returns interpolated result
        inline FrameContentState
        interpolateSlots(const FrameContentState &slotA,
                         const FrameContentState &slotB,
                         double t) {
            FrameContentState state = slotA;

            auto lerp = [](double a, double b, double r) {
                return a * (1.0 - r) + b * r;
            };

            // Compute eased t for properties with curve control.
            // Aligned to sub_699AE4: if curve data exists, t is transformed
            // through sub_69A754 bezier evaluation before interpolation.

            // ccc: eases opacity and color (sub_69A4D4 at 0x69A55C)
            const double t_ccc = !state.ccc.empty()
                ? evaluateBezierCurve(state.ccc, t) : t;

            // acc: eases angle (sub_699AE4 at 0x699DE8)
            const double t_acc = !state.acc.empty()
                ? evaluateBezierCurve(state.acc, t) : t;

            // Position uses PLAIN t (sub_699AE4 at 0x699BB0~BC0).
            // Note: "cc" position curve is NOT applied in interpolateSlots.
            // It's only used by sub_69A4D4 (called from sub_6C1540/case 3 for
            // position derivative computation), separate from normal frame interpolation.
            state.x = lerp(state.x, slotB.x, t);
            state.y = lerp(state.y, slotB.y, t);
            state.z = lerp(state.z, slotB.z, t);
            state.ox = lerp(state.ox, slotB.ox, t);
            state.oy = lerp(state.oy, slotB.oy, t);

            // Opacity — uses ccc-eased t (sub_69A4D4 at 0x69A624)
            // Also supports occ (opacity-specific curve, mask 0x8000)
            // sub_699AE4 at 0x69A004: lerp as int, then round via
            // floor(v+0.5) or ceil(v-0.5)
            if(state.opacity != slotB.opacity) {
                const double t_opa = !state.occ.empty()
                    ? evaluateBezierCurve(state.occ, t)
                    : t_ccc;  // fall back to ccc if no occ
                const double opaA = state.opacity * 255.0;
                const double opaB = slotB.opacity * 255.0;
                double opaInterp = lerp(opaA, opaB, t_opa);
                // Integer rounding aligned to sub_699AE4 at 0x69A040:
                // if (v < 0) ceil(v - 0.5) else floor(v + 0.5)
                int opaInt = opaInterp < 0.0
                    ? static_cast<int>(std::ceil(opaInterp - 0.5))
                    : static_cast<int>(std::floor(opaInterp + 0.5));
                state.opacity = std::clamp(opaInt / 255.0, 0.0, 1.0);
            }

            // ccc controls both opacity and the four corner colors. Keep the
            // colors packed for the renderer, but interpolate each byte here
            // so animated tints do not remain pinned to the first keyframe.
            auto interpolatePackedColor = [&](uint32_t colorA,
                                               uint32_t colorB) {
                uint32_t result = 0;
                for(int shift = 0; shift < 32; shift += 8) {
                    const double channel = lerp(
                        static_cast<double>((colorA >> shift) & 0xffu),
                        static_cast<double>((colorB >> shift) & 0xffu),
                        t_ccc);
                    const int rounded = channel < 0.0
                        ? static_cast<int>(std::ceil(channel - 0.5))
                        : static_cast<int>(std::floor(channel + 0.5));
                    result |= static_cast<uint32_t>(
                        std::clamp(rounded, 0, 255)) << shift;
                }
                return result;
            };
            for(size_t colorIndex = 0;
                colorIndex < state.packedColors.size();
                ++colorIndex) {
                state.packedColors[colorIndex] = interpolatePackedColor(
                    slotA.packedColors[colorIndex],
                    slotB.packedColors[colorIndex]);
            }

            // Angle with 360° wrap — uses acc-eased t (sub_699AE4 at 0x699DEC)
            double curAngle = state.angle;
            double nxtAngle = slotB.angle;
            if(curAngle != nxtAngle) {
                if(curAngle >= nxtAngle) {
                    if(curAngle - nxtAngle > 180.0) nxtAngle += 360.0;
                } else {
                    if(nxtAngle - curAngle > 180.0) nxtAngle -= 360.0;
                }
                double interpAngle = lerp(curAngle, nxtAngle, t_acc);
                if(interpAngle < 0.0) interpAngle += 360.0;
                else if(interpAngle >= 360.0) interpAngle -= 360.0;
                state.angle = interpAngle;
            }

            // ScaleX/scaleY — uses zcc-eased t (sub_699AE4 at 0x699E4C)
            const double t_zcc = !state.zcc.empty()
                ? evaluateBezierCurve(state.zcc, t) : t;
            if(state.scaleX != slotB.scaleX)
                state.scaleX = lerp(state.scaleX, slotB.scaleX, t_zcc);
            if(state.scaleY != slotB.scaleY)
                state.scaleY = lerp(state.scaleY, slotB.scaleY, t_zcc);

            // SlantX/slantY — uses scc-eased t (sub_699AE4 at 0x699EFC)
            const double t_scc = !state.scc.empty()
                ? evaluateBezierCurve(state.scc, t) : t;
            if(state.slantX != slotB.slantX)
                state.slantX = lerp(state.slantX, slotB.slantX, t_scc);
            if(state.slantY != slotB.slantY)
                state.slantY = lerp(state.slantY, slotB.slantY, t_scc);

            // Width/height (linear)
            if(state.width != slotB.width)
                state.width = lerp(state.width, slotB.width, t);
            if(state.height != slotB.height)
                state.height = lerp(state.height, slotB.height, t);

            // E-mote mesh interpolation (libgame.so sub_696EC4 ->
            // sub_69802C). A missing bp list is the global identity 4x4
            // patch, not an absent deformation node.
            if(slotA.hasMeshPayload || slotB.hasMeshPayload ||
               !slotA.meshControlPoints.empty() ||
               !slotB.meshControlPoints.empty()) {
                state.hasMeshPayload = true;
                const auto &identity = identityMeshControlPoints();
                const float *pointsA = slotA.meshControlPoints.size() == 32
                    ? slotA.meshControlPoints.data() : identity.data();
                const float *pointsB = slotB.meshControlPoints.size() == 32
                    ? slotB.meshControlPoints.data() : identity.data();
                const double meshT = !slotA.meshCc.empty()
                    ? evaluateBezierCurve(slotA.meshCc, t) : t;
                state.meshControlPoints.resize(32);
                for(size_t index = 0; index < 32; ++index) {
                    state.meshControlPoints[index] = static_cast<float>(
                        lerp(pointsA[index], pointsB[index], meshT));
                }
                state.meshCc = slotA.meshCc;
            }

            // FlipX/FlipY: not interpolated, use slot A value
            // (sub_699AE4 copies directly from clip slot, no lerp)

            // Use src from slot A (or B if A is empty)
            if(state.src.empty() && !slotB.src.empty()) {
                state.src = slotB.src;
                state.motionIcon = slotB.motionIcon;
            }

            return state;
        }

        // Evaluate layer content at a given time.
        // Orchestrator that calls:
        //   1. parseFrame (sub_6926B4) — parse each frame in frameList
        //   2. mergeFrameContent (sub_692AB0) — read mask-gated properties (called by parseFrame)
        //   3. interpolateSlots (sub_699AE4) — dual-slot interpolation
        inline FrameContentState
        evaluateLayerContent(const std::shared_ptr<const PSB::PSBDictionary> &layer,
                             double time,
                             int nodeType,
                             bool collectDebug = false) {
            FrameContentState state;
            if(!layer) {
                return state;
            }

            // Motion PSB dictionaries are immutable after loading. Parsing
            // their mask-gated frame payloads for every node on every draw
            // duplicated thousands of dictionary lookups and temporary
            // vectors in E-mote scenes. Keep a per-update-thread parsed view;
            // OpenMP workers then read independent caches without a shared
            // lock. The weak owner prevents a reused raw address from binding
            // to a previous game's PSB object.
            struct ParsedLayerKey {
                const PSB::PSBDictionary *layer = nullptr;
                int nodeType = 0;
                bool operator==(const ParsedLayerKey &other) const {
                    return layer == other.layer && nodeType == other.nodeType;
                }
            };
            struct ParsedLayerKeyHash {
                size_t operator()(const ParsedLayerKey &key) const {
                    const auto address =
                        reinterpret_cast<std::uintptr_t>(key.layer);
                    return std::hash<std::uintptr_t>{}(address) ^
                        (std::hash<int>{}(key.nodeType) +
                         0x9e3779b9u + (address << 6u) + (address >> 2u));
                }
            };
            struct ParsedLayerFrames {
                std::weak_ptr<const PSB::PSBDictionary> owner;
                bool hasTransformOrder = false;
                int transformOrder[4] = {0, 1, 2, 3};
                std::vector<std::optional<ParsedFrame>> frames;
            };
            static thread_local std::unordered_map<
                ParsedLayerKey, ParsedLayerFrames, ParsedLayerKeyHash>
                parsedLayerCache;

            const ParsedLayerKey cacheKey{layer.get(), nodeType};
            auto cacheIt = parsedLayerCache.find(cacheKey);
            const std::weak_ptr<const PSB::PSBDictionary> requestedOwner =
                layer;
            const bool sameCachedOwner =
                cacheIt != parsedLayerCache.end() &&
                !cacheIt->second.owner.owner_before(requestedOwner) &&
                !requestedOwner.owner_before(cacheIt->second.owner);
            const bool cacheHit =
                cacheIt != parsedLayerCache.end() && sameCachedOwner;
            if(!cacheHit) {
                if(parsedLayerCache.size() > 4096) {
                    for(auto it = parsedLayerCache.begin();
                        it != parsedLayerCache.end(); ) {
                        if(it->second.owner.expired()) {
                            it = parsedLayerCache.erase(it);
                        } else {
                            ++it;
                        }
                    }
                }

                ParsedLayerFrames parsed;
                parsed.owner = layer;
                if(auto toList = psbDictionaryList(
                       std::const_pointer_cast<PSB::PSBDictionary>(layer),
                       "transformOrder")) {
                    for(int i = 0;
                        i < 4 && i < static_cast<int>(toList->size()); ++i) {
                        if(auto value = psbNumberValue((*toList)[i])) {
                            parsed.transformOrder[i] =
                                static_cast<int>(*value);
                        }
                    }
                    parsed.hasTransformOrder = true;
                }

                if(const auto frames =
                       psbDictionaryList(layer, "frameList")) {
                    parsed.frames.reserve(frames->size());
                    for(size_t index = 0; index < frames->size(); ++index) {
                        const auto frame =
                            std::dynamic_pointer_cast<PSB::PSBDictionary>(
                                (*frames)[static_cast<int>(index)]);
                        if(frame) {
                            parsed.frames.emplace_back(
                                parseFrame(frame, nodeType));
                        } else {
                            parsed.frames.emplace_back(std::nullopt);
                        }
                    }
                }
                cacheIt = parsedLayerCache.insert_or_assign(
                    cacheKey, std::move(parsed)).first;
            }
            const auto &parsedLayer = cacheIt->second;
            if(parsedLayer.frames.empty()) {
                return state;
            }

            // Read transformOrder from layer dict (stored at node+84..96 in libkrkr2.so).
            // sub_699940 uses this to determine the order of Flip/Angle/Zoom/Slant.
            if(parsedLayer.hasTransformOrder) {
                std::copy(parsedLayer.transformOrder,
                          parsedLayer.transformOrder + 4,
                          state.transformOrder);
                state.hasTransformOrder = true;
            }

            // Step 1: Find active frame (last frame with time <= time)
            int activeIndex = -1;
            for(size_t index = 0;
                index < parsedLayer.frames.size(); ++index) {
                if(!parsedLayer.frames[index]) {
                    continue;
                }
                if(parsedLayer.frames[index]->time > time) {
                    break;
                }
                activeIndex = static_cast<int>(index);
            }

            if(activeIndex < 0) return state;
            if(collectDebug) {
                state.debugEvaluated = true;
                state.debugActiveIndex = activeIndex;
            }

            // Step 2: Parse active frame via sub_6926B4
            const auto &frameAOptional =
                parsedLayer.frames[static_cast<size_t>(activeIndex)];
            if(!frameAOptional) return state;
            const ParsedFrame &frameA = *frameAOptional;
            if(collectDebug) {
                state.debugFrameATime = frameA.time;
                state.debugFrameAType = frameA.type;
                state.debugFrameAInvisible = frameA.invisible;
                state.debugFrameAOpacity = frameA.slot.opacity;
                state.debugFrameAScaleX = frameA.slot.scaleX;
                state.debugFrameAScaleY = frameA.slot.scaleY;
                state.debugFrameASrc = frameA.slot.src;
            }

            // type=0: node is invisible at this time
            if(frameA.invisible) {
                state.visible = false;
                return state;
            }

            // Preserve transformOrder from layer dict
            int savedTO[4]; bool savedHasTO = state.hasTransformOrder;
            std::copy(std::begin(state.transformOrder),
                      std::end(state.transformOrder), savedTO);
            state = frameA.slot;
            state.visible = true;
            state.clipStartTime = frameA.time;  // slot+328: frame start time
            if(collectDebug) {
                state.debugEvaluated = true;
                state.debugActiveIndex = activeIndex;
                state.debugFrameATime = frameA.time;
                state.debugFrameAType = frameA.type;
                state.debugFrameAInvisible = frameA.invisible;
                state.debugFrameAOpacity = frameA.slot.opacity;
                state.debugFrameAScaleX = frameA.slot.scaleX;
                state.debugFrameAScaleY = frameA.slot.scaleY;
                state.debugFrameASrc = frameA.slot.src;
            }
            if(savedHasTO) {
                std::copy(savedTO, savedTO + 4, state.transformOrder);
                state.hasTransformOrder = true;
            }

            // Native ForwardFrame (libartemis.so 0x6B30F0,
            // compatible-v2 0x6B43E8) toggles LayerInfo::activeSlot before
            // loading the following frame into the other slot. BuildFrameParam
            // (0x6B4988 / 0x6B5F7C) then tests activeSlot+241, so type=3 is a
            // property of frameA and controls the span leaving that frame.
            // This distinction is visible in E-mote mouth tracks authored as
            // 0:type2, 12:type3, 60:type3: values below the first mouth
            // threshold stay closed, then the 12..60 span opens monotonically.
            if(!frameA.interpolate) {
                return state;
            }

            const int nextIndex = activeIndex + 1;
            if(nextIndex >= static_cast<int>(parsedLayer.frames.size())) {
                return state;  // no next frame, just use slot A
            }
            if(collectDebug) {
                state.debugNextIndex = nextIndex;
            }

            // Step 3: Parse next frame via sub_6926B4
            const auto &frameBOptional =
                parsedLayer.frames[static_cast<size_t>(nextIndex)];
            if(!frameBOptional) return state;
            const ParsedFrame &frameB = *frameBOptional;
            if(collectDebug) {
                state.debugFrameBTime = frameB.time;
                state.debugFrameBType = frameB.type;
                state.debugFrameBInvisible = frameB.invisible;
                state.debugFrameBOpacity = frameB.slot.opacity;
                state.debugFrameBScaleX = frameB.slot.scaleX;
                state.debugFrameBScaleY = frameB.slot.scaleY;
                state.debugFrameBSrc = frameB.slot.src;
            }
            // Compute interpolation ratio
            const double duration = frameB.time - frameA.time;
            if(duration <= 0.0) return state;

            const double t = std::clamp(
                (time - frameA.time) / duration, 0.0, 1.0);
            if(collectDebug) {
                state.debugInterpT = t;
            }

            if(t <= 0.0 || frameB.invisible) {
                return state;  // at exact start or next is invisible
            }

            // Step 4: Interpolate via sub_699AE4
            if(frameB.slot.src.empty()) {
                FrameContentState inheritedSlotB = frameB.slot;
                inheritedSlotB.src = state.src;
                state = interpolateSlots(state, inheritedSlotB, t);
            } else {
                state = interpolateSlots(state, frameB.slot, t);
            }
            state.visible = true;
            if(collectDebug) {
                state.debugEvaluated = true;
                state.debugActiveIndex = activeIndex;
                state.debugNextIndex = nextIndex;
                state.debugFrameATime = frameA.time;
                state.debugFrameAType = frameA.type;
                state.debugFrameAInvisible = frameA.invisible;
                state.debugFrameAOpacity = frameA.slot.opacity;
                state.debugFrameAScaleX = frameA.slot.scaleX;
                state.debugFrameAScaleY = frameA.slot.scaleY;
                state.debugFrameASrc = frameA.slot.src;
                state.debugFrameBTime = frameB.time;
                state.debugFrameBType = frameB.type;
                state.debugFrameBInvisible = frameB.invisible;
                state.debugFrameBOpacity = frameB.slot.opacity;
                state.debugFrameBScaleX = frameB.slot.scaleX;
                state.debugFrameBScaleY = frameB.slot.scaleY;
                state.debugFrameBSrc = frameB.slot.src;
                state.debugInterpT = t;
                state.debugInterpolated = true;
            }

            if(savedHasTO) {
                std::copy(savedTO, savedTO + 4, state.transformOrder);
                state.hasTransformOrder = true;
            }

            return state;
        }


        // -----------------------------------------------------------------
        // Layer-API based rendering (no OpenCV dependency, used in web build)
        // -----------------------------------------------------------------

        // Navigate a PSB dictionary tree by a slash-separated path.
        inline std::shared_ptr<const PSB::PSBDictionary> navigatePSBPath(
            const std::shared_ptr<const PSB::PSBDictionary> &root,
            const std::string &path) {
            if(!root || path.empty()) return nullptr;
            auto node = root;
            std::istringstream pathStream(path);
            std::string segment;
            while(std::getline(pathStream, segment, '/')) {
                if(segment.empty() || !node) continue;
                auto child = std::dynamic_pointer_cast<const PSB::PSBDictionary>(
                    (*node)[segment]);
                if(!child) return nullptr;
                node = child;
            }
            return node;
        }

        // Find a PSB resource node by source name. The motion layer `src`
        // field uses paths like "src/title/bg" and the PSB tree stores
        // resources under "source/title/icon/bg/pixel".
        // Aligned to libkrkr2.so sub_6948E8: navigates source/<group>/icon/<name>.
        // Also reads originX/originY from the icon node (image anchor point,
        // used in sub_6BC4F0: origin = pos - matrix × (originX, originY)).
        // If the resource is RL-compressed or palettized, decodes into
        // decompressedOut. Palettized output matches libkrkr2.so's BGRA path.
        inline const PSB::PSBResource *findPSBResourceBySourceName(
            const detail::MotionSnapshot &snapshot,
            const std::string &source,
            int &outWidth, int &outHeight,
            std::vector<std::uint8_t> &decompressedOut,
            double &outOriginX, double &outOriginY,
            bool *outDecodedIsBgra = nullptr,
            bool decodePixelData = true,
            std::string *outResourcePath = nullptr,
            std::string *outCompressName = nullptr,
            int *outDecodedWidth = nullptr,
            int *outDecodedHeight = nullptr,
            std::array<int, 4> *outDecodedSourceRect = nullptr) {
            outWidth = 0;
            outHeight = 0;
            outOriginX = 0.0;
            outOriginY = 0.0;
            decompressedOut.clear();
            if(outDecodedIsBgra) {
                *outDecodedIsBgra = false;
            }
            if(outResourcePath) {
                outResourcePath->clear();
            }
            if(outCompressName) {
                outCompressName->clear();
            }
            if(outDecodedWidth) {
                *outDecodedWidth = 0;
            }
            if(outDecodedHeight) {
                *outDecodedHeight = 0;
            }
            if(outDecodedSourceRect) {
                *outDecodedSourceRect = {0, 0, 0, 0};
            }
            if(source.empty() || isMotionCrossReference(source)) {
                return nullptr;
            }

            // E-mote uses synthetic transparent sources as transform/mesh
            // containers.  They are encoded directly in the source name as
            //
            //     blank/<width>:<height>:<originX>:<originY>
            //
            // and therefore have no PSB pixel resource to look up.  Native
            // libgame still fills the node's clip rectangle and anchor from
            // these four values; descendants are then evaluated through the
            // blank node's Bezier patch.  Leaving the dimensions at zero
            // drops that patch and detaches face/hair/body child layers.
            if(source.rfind("blank/", 0) == 0) {
                const char *cursor = source.c_str() + 6;
                double values[4]{};
                bool valid = true;
                for(int i = 0; i < 4; ++i) {
                    char *end = nullptr;
                    values[i] = std::strtod(cursor, &end);
                    if(end == cursor || !std::isfinite(values[i])) {
                        valid = false;
                        break;
                    }
                    if(i < 3) {
                        if(*end != ':') {
                            valid = false;
                            break;
                        }
                        cursor = end + 1;
                    } else if(*end != '\0') {
                        valid = false;
                    }
                }
                if(valid && values[0] > 0.0 && values[1] > 0.0) {
                    outWidth = static_cast<int>(std::lround(values[0]));
                    outHeight = static_cast<int>(std::lround(values[1]));
                    outOriginX = values[2];
                    outOriginY = values[3];
                }
                return nullptr;
            }

            // Strategy 1: Parse src/<group>/<name> and navigate directly
            // to source/<group>/icon/<name> in the PSB tree.
            // This is the primary path aligned to libkrkr2.so sub_6948E8.
            if(source.rfind("src/", 0) == 0 && snapshot.root) {
                // Parse "src/<group>/<name>" → group, name
                const auto afterSrc = source.substr(4); // skip "src/"
                const auto slash = afterSrc.find('/');
                if(slash != std::string::npos) {
                    const auto group = afterSrc.substr(0, slash);
                    const auto name = afterSrc.substr(slash + 1);
                    // Navigate: source/<group>/icon/<name>
                    const auto iconPath =
                        "source/" + group + "/icon/" + name;
                    auto iconNode = navigatePSBPath(snapshot.root, iconPath);
                    if(iconNode) {
                        // Read width/height from the icon node
                        if(auto w = psbDictionaryNumber(iconNode, "width"))
                            outWidth = static_cast<int>(*w);
                        if(auto h = psbDictionaryNumber(iconNode, "height"))
                            outHeight = static_cast<int>(*h);
                        if(outWidth <= 0) {
                            if(auto tw = psbDictionaryNumber(iconNode,
                                             "truncated_width"))
                                outWidth = static_cast<int>(*tw);
                        }
                        if(outHeight <= 0) {
                            if(auto th = psbDictionaryNumber(iconNode,
                                             "truncated_height"))
                                outHeight = static_cast<int>(*th);
                        }
                        // Read origin (anchor point) from icon node
                        // Aligned to libkrkr2.so sub_6BC4F0: used as
                        // origin = pos - matrix × (originX, originY)
                        if(auto ox = psbDictionaryNumber(iconNode, "originX"))
                            outOriginX = *ox;
                        if(auto oy = psbDictionaryNumber(iconNode, "originY"))
                            outOriginY = *oy;
                        // Get the pixel resource
                        const auto pixelPath = iconPath + "/pixel";
                        auto resIt = snapshot.resourcesByPath.find(pixelPath);
                        if(resIt != snapshot.resourcesByPath.end() &&
                           !resIt->second->data.empty() &&
                           outWidth > 0 && outHeight > 0) {
                            auto compressStr =
                                psbDictionaryString(iconNode, "compress");
                            if(outResourcePath) {
                                *outResourcePath = pixelPath;
                            }
                            if(outCompressName) {
                                *outCompressName = compressStr;
                            }
                            if(!decodePixelData) {
                                return resIt->second.get();
                            }
                            const bool isRL =
                                isPsbRLCompressName(compressStr);
                            const bool decoded = decodePsbPixelResource(
                                snapshot, iconPath, *resIt->second,
                                outWidth, outHeight,
                                isRL,
                                decompressedOut, outDecodedIsBgra);
                            if(LOGGER && shouldDebugPsbSource(snapshot, source) &&
                               markPsbDebugLogged(snapshot.path + "|" +
                                                  source + "|" + pixelPath)) {
                                const auto palPath = iconPath + "/pal";
                                const auto palIt =
                                    snapshot.resourcesByPath.find(palPath);
                                const size_t palBytes =
                                    (palIt != snapshot.resourcesByPath.end() &&
                                     palIt->second)
                                    ? palIt->second->data.size()
                                    : 0u;
                                LOGGER->info(
                                    "motion psb source: path={} source={} pixel={} size={}x{} raw={} expected={} header=0x{:08x} compress={} isRL={} pal={} decoded={} decodedBytes={} decodedBgra={} rawStats=[{}] decodedStats=[{}]",
                                    snapshot.path, source, pixelPath,
                                    outWidth, outHeight,
                                    resIt->second->data.size(),
                                    static_cast<size_t>(outWidth) *
                                        static_cast<size_t>(outHeight) * 4u,
                                    psbDataHeader(resIt->second->data),
                                    compressStr.empty() ? "<none>" : compressStr,
                                    isRL ? 1 : 0,
                                    palBytes,
                                    decoded ? 1 : 0,
                                    decompressedOut.size(),
                                    outDecodedIsBgra && *outDecodedIsBgra ? 1 : 0,
                                    samplePsbPixelStats(resIt->second->data),
                                    samplePsbPixelStats(decompressedOut));
                            }
                            if(decoded) {
                                return resIt->second.get();
                            }
                        }

                        // Some E-mote exports (including Maitetsu) pack every
                        // icon in a group into one RGBA atlas:
                        //
                        //   source/<group>/texture/pixel
                        //
                        // The icon node then contains only left/top/width/
                        // height/origin.  Treat the selected atlas rectangle
                        // as this source's pixel resource.
                        const auto texturePath =
                            "source/" + group + "/texture";
                        const auto atlasPixelPath = texturePath + "/pixel";
                        const auto atlasIt =
                            snapshot.resourcesByPath.find(atlasPixelPath);
                        const auto atlasNode =
                            navigatePSBPath(snapshot.root, texturePath);
                        if(atlasIt != snapshot.resourcesByPath.end() &&
                           atlasIt->second && !atlasIt->second->data.empty() &&
                           outWidth > 0 && outHeight > 0) {
                            const auto left = psbDictionaryNumber(
                                iconNode, "left").value_or(0.0);
                            const auto top = psbDictionaryNumber(
                                iconNode, "top").value_or(0.0);
                            int atlasWidth = atlasNode
                                ? static_cast<int>(psbDictionaryNumber(
                                      atlasNode, "width").value_or(0.0))
                                : 0;
                            int atlasHeight = atlasNode
                                ? static_cast<int>(psbDictionaryNumber(
                                      atlasNode, "height").value_or(0.0))
                                : 0;
                            const auto atlasPixels =
                                atlasIt->second->data.size() / 4u;
                            if(atlasWidth <= 0 || atlasHeight <= 0) {
                                const auto squareSide = static_cast<int>(
                                    std::lround(std::sqrt(
                                        static_cast<double>(atlasPixels))));
                                if(squareSide > 0 &&
                                   static_cast<size_t>(squareSide) *
                                       static_cast<size_t>(squareSide) ==
                                       atlasPixels) {
                                    atlasWidth = squareSide;
                                    atlasHeight = squareSide;
                                }
                            }
                            const int atlasLeft =
                                static_cast<int>(std::lround(left));
                            const int atlasTop =
                                static_cast<int>(std::lround(top));
                            if(atlasWidth > 0 && atlasHeight > 0 &&
                               atlasLeft >= 0 && atlasTop >= 0 &&
                               atlasLeft + outWidth <= atlasWidth &&
                               atlasTop + outHeight <= atlasHeight) {
                                const auto compressStr = atlasNode
                                    ? psbDictionaryString(
                                          atlasNode, "compress")
                                    : std::string{};
                                const auto textureType = atlasNode
                                    ? psbDictionaryString(atlasNode, "type")
                                    : std::string{};
                                const auto resourceEncoding = textureType.empty()
                                    ? compressStr : textureType;
                                if(outResourcePath) {
                                    *outResourcePath = iconPath +
                                        fmt::format(
                                            "/atlas@{},{},{},{}",
                                            atlasLeft, atlasTop,
                                            outWidth, outHeight);
                                }
                                if(outCompressName) {
                                    *outCompressName = resourceEncoding;
                                }

                                // Native MPSBTex keeps the complete atlas
                                // bound while RenderMesh addresses the icon
                                // rectangle inside it.  A tightly cropped
                                // replacement texture clamps its outermost
                                // texel instead, turning the common 0.5x
                                // E-mote presentation into a dark, serrated
                                // silhouette.  Callers which consume the
                                // decoded layout request a small piece of the
                                // original atlas gutter and sample only the
                                // logical icon rectangle within it.
                                constexpr int kFilterGutter = 2;
                                const bool preserveFilterGutter =
                                    outDecodedWidth || outDecodedHeight ||
                                    outDecodedSourceRect;
                                const int decodedLeft = preserveFilterGutter
                                    ? std::max(0, atlasLeft - kFilterGutter)
                                    : atlasLeft;
                                const int decodedTop = preserveFilterGutter
                                    ? std::max(0, atlasTop - kFilterGutter)
                                    : atlasTop;
                                const int decodedRight = preserveFilterGutter
                                    ? std::min(atlasWidth,
                                               atlasLeft + outWidth +
                                                   kFilterGutter)
                                    : atlasLeft + outWidth;
                                const int decodedBottom = preserveFilterGutter
                                    ? std::min(atlasHeight,
                                               atlasTop + outHeight +
                                                   kFilterGutter)
                                    : atlasTop + outHeight;
                                const int decodedWidth =
                                    decodedRight - decodedLeft;
                                const int decodedHeight =
                                    decodedBottom - decodedTop;
                                const int insetLeft = atlasLeft - decodedLeft;
                                const int insetTop = atlasTop - decodedTop;
                                if(outDecodedWidth) {
                                    *outDecodedWidth = decodedWidth;
                                }
                                if(outDecodedHeight) {
                                    *outDecodedHeight = decodedHeight;
                                }
                                if(outDecodedSourceRect) {
                                    *outDecodedSourceRect = {
                                        insetLeft, insetTop,
                                        insetLeft + outWidth,
                                        insetTop + outHeight};
                                }
                                if(!decodePixelData) {
                                    return atlasIt->second.get();
                                }

                                if(decodePsbBlockCompressedAtlasRegion(
                                       atlasIt->second->data,
                                       resourceEncoding,
                                       atlasWidth, atlasHeight,
                                       decodedLeft, decodedTop,
                                       decodedWidth, decodedHeight,
                                       decompressedOut)) {
                                    if(outDecodedIsBgra) {
                                        *outDecodedIsBgra = false;
                                    }
                                    return atlasIt->second.get();
                                }

                                std::vector<std::uint8_t> atlasPixelsDecoded;
                                bool atlasIsBgra = false;
                                const auto paletteIt =
                                    snapshot.resourcesByPath.find(
                                        texturePath + "/pal");
                                const bool hasPalette =
                                    paletteIt !=
                                        snapshot.resourcesByPath.end() &&
                                    paletteIt->second &&
                                    !paletteIt->second->data.empty();
                                const size_t atlasByteCount =
                                    static_cast<size_t>(atlasWidth) *
                                    static_cast<size_t>(atlasHeight) * 4u;
                                const std::vector<std::uint8_t> *atlasData =
                                    nullptr;
                                if(!hasPalette &&
                                   !isPsbRLCompressName(compressStr) &&
                                   atlasIt->second->data.size() >=
                                       atlasByteCount) {
                                    // Raw atlases are common and can be
                                    // cropped directly. Avoid copying a
                                    // 4-16 MiB atlas once for every icon.
                                    atlasData = &atlasIt->second->data;
                                } else if(decodePsbPixelResource(
                                              snapshot, texturePath,
                                              *atlasIt->second,
                                              atlasWidth, atlasHeight,
                                              isPsbRLCompressName(
                                                  compressStr),
                                              atlasPixelsDecoded,
                                              &atlasIsBgra)) {
                                    atlasData = &atlasPixelsDecoded;
                                }
                                if(atlasData &&
                                   atlasData->size() >= atlasByteCount) {
                                    const size_t rowBytes =
                                        static_cast<size_t>(decodedWidth) * 4u;
                                    decompressedOut.resize(
                                        rowBytes *
                                        static_cast<size_t>(decodedHeight));
                                    for(int row = 0; row < decodedHeight; ++row) {
                                        const auto sourceOffset =
                                            (static_cast<size_t>(
                                                 decodedTop + row) *
                                                 static_cast<size_t>(
                                                     atlasWidth) +
                                             static_cast<size_t>(decodedLeft)) *
                                            4u;
                                        std::memcpy(
                                            decompressedOut.data() +
                                                static_cast<size_t>(row) *
                                                    rowBytes,
                                            atlasData->data() +
                                                sourceOffset,
                                            rowBytes);
                                    }
                                    if(outDecodedIsBgra) {
                                        *outDecodedIsBgra = atlasIsBgra;
                                    }
                                    return atlasIt->second.get();
                                }
                            }
                        }
                    }
                }
            }

            // Strategy 2 (fallback): Search resourcesByPath for a key
            // ending with /<baseName>/pixel.
            const auto lastSlash = source.rfind('/');
            const auto baseName = (lastSlash != std::string::npos)
                ? source.substr(lastSlash + 1) : source;

            for(const auto &[resPath, resource] : snapshot.resourcesByPath) {
                const auto targetSuffix = "/" + baseName + "/pixel";
                if(resPath.size() >= targetSuffix.size() &&
                   resPath.compare(resPath.size() - targetSuffix.size(),
                                   targetSuffix.size(), targetSuffix) == 0) {
                    // Found the pixel resource — read dims from parent node
                    const auto parentPath =
                        resPath.substr(0, resPath.size() - 6); // strip "/pixel"
                    if(snapshot.root) {
                        auto node = navigatePSBPath(snapshot.root, parentPath);
                        if(node) {
                            if(auto w = psbDictionaryNumber(node, "width"))
                                outWidth = static_cast<int>(*w);
                            if(auto h = psbDictionaryNumber(node, "height"))
                                outHeight = static_cast<int>(*h);
                            if(outWidth <= 0) {
                                if(auto tw = psbDictionaryNumber(node,
                                                 "truncated_width"))
                                    outWidth = static_cast<int>(*tw);
                            }
                            if(outHeight <= 0) {
                                if(auto th = psbDictionaryNumber(node,
                                                 "truncated_height"))
                                    outHeight = static_cast<int>(*th);
                            }
                            auto compressStr =
                                psbDictionaryString(node, "compress");
                            if(outWidth > 0 && outHeight > 0) {
                                if(outResourcePath) {
                                    *outResourcePath = resPath;
                                }
                                if(outCompressName) {
                                    *outCompressName = compressStr;
                                }
                                if(!decodePixelData) {
                                    return resource.get();
                                }
                                const bool isRL =
                                    isPsbRLCompressName(compressStr);
                                const bool decoded = decodePsbPixelResource(
                                    snapshot, parentPath, *resource,
                                    outWidth, outHeight,
                                    isRL,
                                    decompressedOut, outDecodedIsBgra);
                                if(LOGGER &&
                                   shouldDebugPsbSource(snapshot, source) &&
                                   markPsbDebugLogged(snapshot.path + "|" +
                                                      source + "|" + resPath)) {
                                    const auto palPath = parentPath + "/pal";
                                    const auto palIt =
                                        snapshot.resourcesByPath.find(palPath);
                                    const size_t palBytes =
                                        (palIt != snapshot.resourcesByPath.end() &&
                                         palIt->second)
                                        ? palIt->second->data.size()
                                        : 0u;
                                    LOGGER->info(
                                        "motion psb source fallback: path={} source={} pixel={} size={}x{} raw={} expected={} header=0x{:08x} compress={} isRL={} pal={} decoded={} decodedBytes={} decodedBgra={} rawStats=[{}] decodedStats=[{}]",
                                        snapshot.path, source, resPath,
                                        outWidth, outHeight,
                                        resource->data.size(),
                                        static_cast<size_t>(outWidth) *
                                            static_cast<size_t>(outHeight) * 4u,
                                        psbDataHeader(resource->data),
                                        compressStr.empty() ? "<none>" : compressStr,
                                        isRL ? 1 : 0,
                                        palBytes,
                                        decoded ? 1 : 0,
                                        decompressedOut.size(),
                                        outDecodedIsBgra && *outDecodedIsBgra ? 1 : 0,
                                        samplePsbPixelStats(resource->data),
                                        samplePsbPixelStats(decompressedOut));
                                }
                                if(!decoded) {
                                    continue;
                                }
                            }
                        }
                    }
                    if(outWidth > 0 && outHeight > 0 &&
                       !resource->data.empty()) {
                        return resource.get();
                    }
                }
            }
            return nullptr;
        }

        // Write raw BGRA pixel data from a PSB resource to a Layer
        // Load PSB resource pixel data into a layer.
        // Based on libkrkr2.so sub_6948E8: PSB texture data is raw RGBA8
        // pixels (not RL-compressed). The data size may be smaller than
        // width*height*4 — only data.size()/4 pixels are valid, the rest
        // should be zero (transparent). RGB order is swapped to BGRA for
        // TJS layers (TVPReverseRGB in libkrkr2.so).
        inline bool loadPSBResourceToLayer(
            tTJSNI_BaseLayer *layer,
            const PSB::PSBResource &resource,
            int width, int height) {
            if(!layer || width <= 0 || height <= 0 || resource.data.empty()) {
                return false;
            }

            if(!layer->GetHasImage()) {
                layer->SetHasImage(true);
            }
            layer->SetImageSize(static_cast<tjs_uint>(width),
                                static_cast<tjs_uint>(height));

            auto *dstPixels = reinterpret_cast<std::uint8_t *>(
                layer->GetMainImagePixelBufferForWrite());
            const auto pitch = layer->GetMainImagePixelBufferPitch();
            if(!dstPixels || pitch <= 0) {
                return false;
            }

            // Zero-fill the entire layer buffer first
            const auto totalRows = static_cast<size_t>(height);
            for(size_t row = 0; row < totalRows; ++row) {
                std::memset(dstPixels + pitch * row, 0,
                            static_cast<size_t>(width) * 4u);
            }

            // Copy raw RGBA8 data, swapping R↔B → BGRA (TJS format)
            const size_t pixelCount = resource.data.size() / 4u;
            const auto *src = resource.data.data();
            for(size_t i = 0; i < pixelCount; ++i) {
                const size_t px = i % static_cast<size_t>(width);
                const size_t py = i / static_cast<size_t>(width);
                if(py >= totalRows) break;
                auto *dst = dstPixels + pitch * py + px * 4;
                dst[0] = src[i * 4 + 2]; // B ← src R
                dst[1] = src[i * 4 + 1]; // G ← src G
                dst[2] = src[i * 4 + 0]; // R ← src B
                dst[3] = src[i * 4 + 3]; // A ← src A
            }
            return true;
        }

        // Try to resolve a source image path for the given source name in the
        // motion snapshot. Uses the same candidate generation logic as
        // loadMotionSourceImage but without OpenCV.
        inline ttstr resolveMotionSourcePath(
            const detail::MotionSnapshot &snapshot,
            const std::string &source) {
            if(source.empty() || isMotionCrossReference(source)) {
                return {};
            }

            std::vector<ttstr> candidates;
            const auto sourcePath = detail::widen(source);
            candidates.push_back(sourcePath);
            pushGraphicCandidates(candidates, sourcePath);
            detail::appendEmbeddedSourceCandidates(snapshot, source, candidates);
            for(const auto &alias : snapshot.resourceAliases) {
                const auto embeddedBase = ttstr{ TJS_W("psb://") } +
                    detail::widen(alias) + TJS_W("/") + sourcePath;
                pushGraphicCandidates(candidates, embeddedBase);
            }

            // PSB motion resources are stored in a tree like:
            //   source/<group>/<subgroup>/<name>/pixel
            // but motion layers reference them as:
            //   src/<group>/<name>
            // Scan resourcesByPath for matching resource paths.
            {
                const auto lastSlash = source.rfind('/');
                const auto baseName = (lastSlash != std::string::npos)
                    ? source.substr(lastSlash + 1) : source;

                for(const auto &[resPath, _] : snapshot.resourcesByPath) {
                    const auto targetSuffix = "/" + baseName + "/pixel";
                    if(resPath.size() >= targetSuffix.size() &&
                       resPath.compare(resPath.size() - targetSuffix.size(),
                                       targetSuffix.size(), targetSuffix) == 0) {
                        for(const auto &alias : snapshot.resourceAliases) {
                            const auto psbPath = ttstr{ TJS_W("psb://") } +
                                detail::widen(alias) + TJS_W("/") +
                                detail::widen(resPath);
                            pushGraphicCandidates(candidates, psbPath);
                        }
                    }
                }
            }

            std::unordered_set<std::string> seen;
            for(const auto &candidate : candidates) {
                const auto candidateKey = detail::narrow(candidate);
                if(!seen.insert(candidateKey).second || candidate.IsEmpty()) {
                    continue;
                }
                if(candidateKey.rfind("psb://", 0) == 0) {
                    if(TVPIsExistentStorage(candidate)) {
                        return candidate;
                    }
                    continue;
                }
                if(const auto placed = TVPGetPlacedPath(candidate);
                   !placed.IsEmpty()) {
                    return placed;
                }
            }
            return {};
        }

        // Flatten a PSB layer node tree into a list of render nodes.
        // Aligned to libkrkr2.so sub_6C4E28: converts tree into flat list
        // with pre-computed positions for the sub_6C7440 render loop.
        // Aligned to libkrkr2.so: full 2x3 affine [m11,m21,m12,m22,tx,ty]
        using Affine2x3 = std::array<double, 6>;

        inline Affine2x3 startupLogoGeometryParentTransform(
            const std::string &motionPath,
            const Affine2x3 &parent,
            int inheritFlags) {
            if(!startupLogoMotionUsesStableBackdropReference(motionPath) ||
               (inheritFlags & 0x060) == 0x060) {
                return parent;
            }

            const double basisX =
                std::hypot(parent[0], parent[1]);
            const double basisY =
                std::hypot(parent[2], parent[3]);
            if(!std::isfinite(basisX) || !std::isfinite(basisY) ||
               basisX <= 1.0e-9 || basisY <= 1.0e-9) {
                return parent;
            }

            // A skewed basis cannot be separated into independent authored
            // X/Y scales without changing its slant. M2's text parent is an
            // orthogonal uniform-scale transform, so keep the compatibility
            // correction deliberately narrow.
            const double basisDot =
                parent[0] * parent[2] + parent[1] * parent[3];
            if(std::fabs(basisDot) > basisX * basisY * 1.0e-6) {
                return parent;
            }

            Affine2x3 result = parent;
            if((inheritFlags & 0x020) == 0) {
                result[0] /= basisX;
                result[1] /= basisX;
            }
            if((inheritFlags & 0x040) == 0) {
                result[2] /= basisY;
                result[3] /= basisY;
            }
            return result;
        }

        // Compose: result = parent * Translate(lx, ly)
        inline Affine2x3 affineTranslate(const Affine2x3 &p, double lx, double ly) {
            return {p[0], p[1], p[2], p[3],
                    p[0]*lx + p[2]*ly + p[4],
                    p[1]*lx + p[3]*ly + p[5]};
        }

        // Compose: result = a * Scale(sx, sy)
        inline Affine2x3 affineScale(const Affine2x3 &a, double sx, double sy) {
            return {a[0]*sx, a[1]*sx, a[2]*sy, a[3]*sy, a[4], a[5]};
        }

        // Compose: result = a * Rotate(angleDeg)
        // Aligned to libkrkr2.so Player_updateLayers 2x2 matrix multiply
        inline Affine2x3 affineRotate(const Affine2x3 &a, double angleDeg) {
            if(angleDeg == 0.0) return a;
            const double rad = angleDeg * 3.14159265358979323846 / 180.0;
            const double c = std::cos(rad);
            const double s = std::sin(rad);
            // Rotation matrix R = [c -s; s c]
            // A * R: new_m11 = a.m11*c + a.m12*s, new_m12 = -a.m11*s + a.m12*c
            return {a[0]*c + a[2]*s, a[1]*c + a[3]*s,
                    -a[0]*s + a[2]*c, -a[1]*s + a[3]*c,
                    a[4], a[5]};
        }

        // Build local 2x2 matrix and right-multiply into affine.
        // Exactly replicates libkrkr2.so sub_699940 (0x699940):
        //   Starts from identity, LEFT-multiplies transforms in order
        //   [0=Flip, 1=Angle, 2=Zoom, 3=Slant] (default transformOrder).
        //   Then composes: affine = affine × local_2x2
        //
        // sub_699940 variable mapping (verified from decompilation):
        //   v5→m11(+120), v6→m12(+128), v4→m21(+136), v7→m22(+144)
        //   case 0 flipX: negate v5,v6 (row1) = left-multiply [-1,0;0,1]
        //   case 0 flipY: negate v4,v7 (row2) = left-multiply [1,0;0,-1]
        //   case 1 angle: left-multiply [cos,-sin;sin,cos]
        //   case 2 zoom:  left-multiply [zoomX,0;0,zoomY]
        //   case 3 slant: left-multiply [1,slantX;slantY,1]
        inline void applyLocalTransform(
            Affine2x3 &a,
            bool flipX,
            bool flipY,
            double angle,
            double scaleX,
            double scaleY,
            double slantX,
            double slantY,
            const int (&transformOrder)[4]) {
            // Build local 2x2 from identity via left-multiplication.
            // Exactly replicates sub_699940 (0x699940): iterates
            // transformOrder[0..3] and applies each transform case.
            // Default order [0,1,2,3] = [Flip, Angle, Zoom, Slant].
            double l11 = 1.0, l12 = 0.0, l21 = 0.0, l22 = 1.0;

            for(int step = 0; step < 4; step++) {
                const int op = transformOrder[step];
                switch(op) {
                    case 0: // Flip (left-multiply [-1,0;0,1] / [1,0;0,-1])
                        if(flipX) { l11 = -l11; l12 = -l12; }
                        if(flipY) { l21 = -l21; l22 = -l22; }
                        break;
                    case 1: // Angle (left-multiply [c,-s;s,c])
                        if(angle != 0.0) {
                            const double rad = angle * 2.0 * 3.14159265358979323846 / 360.0;
                            const double c = std::cos(rad);
                            const double s = std::sin(rad);
                            const double t11 = c*l11 - s*l21;
                            const double t12 = c*l12 - s*l22;
                            const double t21 = s*l11 + c*l21;
                            const double t22 = s*l12 + c*l22;
                            l11 = t11; l12 = t12; l21 = t21; l22 = t22;
                        }
                        break;
                    case 2: // Zoom (left-multiply [zx,0;0,zy]) — 0x699A50
                        if(scaleX != 1.0 || scaleY != 1.0) {
                            l11 *= scaleX; l12 *= scaleX;
                            l21 *= scaleY; l22 *= scaleY;
                        }
                        break;
                    case 3: // Slant (left-multiply [1,sx;sy,1]) — 0x699A7C
                        if(slantX != 0.0 || slantY != 0.0) {
                            const double t12 = l22*slantX + l12;
                            const double t21 = l11*slantY + l21;
                            const double t22 = l22 + l12*slantY;
                            const double t11 = l11 + slantX*l21;
                            l11 = t11; l12 = t12; l21 = t21; l22 = t22;
                        }
                        break;
                }
            }

            // Right-multiply local 2x2 into affine: A_new = A × L
            // (tx,ty unchanged; only 2x2 part is affected)
            const double m11 = a[0]*l11 + a[2]*l21;
            const double m21 = a[1]*l11 + a[3]*l21;
            const double m12 = a[0]*l12 + a[2]*l22;
            const double m22 = a[1]*l12 + a[3]*l22;
            a[0] = m11; a[1] = m21; a[2] = m12; a[3] = m22;
        }

        inline void applyLocalTransform(Affine2x3 &a,
                                 const FrameContentState &state) {
            applyLocalTransform(a,
                                state.flipX, state.flipY,
                                state.angle,
                                state.scaleX, state.scaleY,
                                state.slantX, state.slantY,
                                state.transformOrder);
        }

        // sub_699940 (0x699940) rebuilds the local 2x2 from node fields
        // after Player_updateLayers has already applied inheritFlags.
        inline void applyLocalTransform(Affine2x3 &a,
                                 const detail::MotionNode &node) {
            applyLocalTransform(a,
                                node.accumulated.flipX,
                                node.accumulated.flipY,
                                node.accumulated.angle,
                                node.accumulated.scaleX,
                                node.accumulated.scaleY,
                                node.accumulated.slantX,
                                node.accumulated.slantY,
                                node.transformOrder);
        }

} // namespace internal
} // namespace motion
