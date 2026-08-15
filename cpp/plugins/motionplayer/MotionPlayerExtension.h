#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "tjs.h"

namespace PSB {
    class PSBDictionary;
}

namespace motion {
    class Player;
    class ResourceManager;

    namespace detail {
        struct MotionNode;
        struct MotionSnapshot;
    }

    struct MotionBackendValue {
        enum class Type { Void, Number, Boolean, String };

        Type type = Type::Void;
        double number = 0.0;
        bool boolean = false;
        std::string string;

        static MotionBackendValue Number(double value) {
            MotionBackendValue result;
            result.type = Type::Number;
            result.number = value;
            return result;
        }
        static MotionBackendValue Boolean(bool value) {
            MotionBackendValue result;
            result.type = Type::Boolean;
            result.boolean = value;
            return result;
        }
        static MotionBackendValue String(std::string value) {
            MotionBackendValue result;
            result.type = Type::String;
            result.string = std::move(value);
            return result;
        }
    };

    struct MotionBackendFrame {
        std::vector<std::uint8_t> rgba;
        std::shared_ptr<const std::vector<std::uint8_t>> sharedRgba;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        bool alphaPremultiplied = true;
    };

    // Backend-neutral description of a frame that remains resident in the host
    // GPU. The producer-owned lifetime keeps the imported shared image alive
    // until both the backend and KiriKiri composition have moved past it.
    struct MotionBackendGpuFrame {
        std::uint64_t texture = 0;
        std::shared_ptr<void> lifetime;
        std::uint32_t canvasWidth = 0;
        std::uint32_t canvasHeight = 0;
        std::uint32_t frameLeft = 0;
        std::uint32_t frameTop = 0;
        std::uint32_t frameWidth = 0;
        std::uint32_t frameHeight = 0;
        std::uint32_t textureWidth = 0;
        std::uint32_t textureHeight = 0;
        bool alphaPremultiplied = true;
        bool flippedY = false;

        bool valid() const {
            return texture != 0 && lifetime != nullptr &&
                frameWidth != 0 && frameHeight != 0;
        }
    };

    // Kiri retained-root clips and native public timelines occupy different
    // namespaces. Return native flags only for an exact public label so callers
    // never substitute an authored demo timeline for an internal root.
    inline int exactMotionBackendTimelineFlags(
        const std::string &requested,
        const std::vector<std::string> &mainLabels,
        const std::vector<std::string> &diffLabels) {
        if(std::find(diffLabels.begin(), diffLabels.end(), requested) !=
           diffLabels.end()) {
            return 3;
        }
        if(std::find(mainLabels.begin(), mainLabels.end(), requested) !=
           mainLabels.end()) {
            return 1;
        }
        return 0;
    }

    // A retained Kiri model entry point is not a public native timeline, but the
    // backend can still expose one dedicated difference timeline for its
    // ambient body/hair/bust motion. Prefer the unnumbered authored waiting
    // loop and use a numbered variant only when it is the sole available
    // choice. This deliberately excludes main/sample timelines, whose demo
    // sequences cycle expressions and gestures.
    inline std::string preferredMotionBackendIdleTimeline(
        const std::vector<std::string> &diffLabels) {
        std::string numberedFallback;
        for(const auto &label : diffLabels) {
            const auto marker = label.find("waiting_loop");
            if(marker == std::string::npos) {
                continue;
            }
            const auto suffix = marker + std::string("waiting_loop").size();
            if(suffix == label.size()) {
                return label;
            }
            if(numberedFallback.empty() &&
               std::all_of(label.begin() + static_cast<std::ptrdiff_t>(suffix),
                           label.end(), [](unsigned char value) {
                               return value >= '0' && value <= '9';
                           })) {
                numberedFallback = label;
            }
        }
        return numberedFallback;
    }

    // Versioned, backend-neutral player seam. Private packages may implement it;
    // public-only builds retain the compatible software renderer.
    class MotionNativePlayerBackend {
    public:
        virtual ~MotionNativePlayerBackend() = default;
        virtual std::unique_ptr<MotionNativePlayerBackend> clone(
            std::string *error) const = 0;
        virtual bool assignState(const MotionNativePlayerBackend &source,
                                 std::string *error) = 0;
        virtual bool invoke(const std::string &method,
                            const std::vector<MotionBackendValue> &arguments,
                            std::vector<MotionBackendValue> *results,
                            std::string *error) = 0;
        virtual bool render(std::uint32_t width, std::uint32_t height,
                            MotionBackendFrame *frame,
                            std::string *error) = 0;
        virtual bool supportsGpuOutput() const { return false; }
        virtual bool renderGpu(std::uint32_t width, std::uint32_t height,
                               MotionBackendGpuFrame *frame,
                               std::string *error) {
            (void)width;
            (void)height;
            (void)frame;
            if(error) *error = "native GPU motion output is unavailable";
            return false;
        }
    };

    struct MotionRenderPolicyV1 {
        bool (*isDifferenceAlphaPassThroughLeaf)(
            bool hasOwnSource, bool groupOnly, int blendMode) = nullptr;
        bool (*isIndependentDifferenceAlphaMaskGroup)(
            bool groupOnly, bool hasExplicitMasks, int itemFlags,
            bool hasConcreteRenderParent) = nullptr;
        bool (*canReceiveIndependentDifferenceAlphaMask)(
            bool hasOwnSource, bool groupOnly, int blendMode) = nullptr;
        bool (*isSyntheticMotionBlankSource)(
            const std::string &sourceKey) = nullptr;
        bool (*isAuthoredDifferenceAlphaPair)(
            const std::string &colourLabel,
            const std::string &alphaLabel) = nullptr;
        bool (*isNestedDifferenceAlphaPair)(
            std::size_t colourCommandIndex,
            const std::vector<std::size_t> &alphaAncestry) = nullptr;
        bool (*isGenericDifferenceAlphaLabel)(
            const std::string &label) = nullptr;
        bool (*isUnambiguousNestedDifferenceAlphaPair)(
            std::size_t nestedPairCount) = nullptr;
        bool (*shouldUseCombinedDifferenceAlphaMask)(
            bool hasSelectedPair, std::size_t nestedSourceCount) = nullptr;
        bool (*shouldRecoverDifferenceAlphaFromRgb)(
            std::size_t alphaPixelCount,
            std::size_t rgbPixelCount) = nullptr;
        std::uint8_t (*differenceAlphaFromRgb)(
            std::uint8_t blue, std::uint8_t green,
            std::uint8_t red) = nullptr;
        int (*independentDifferenceAlphaMaskOperation)(
            bool hasAuthoredPair, int groupItemFlags) = nullptr;
        std::uint8_t (*applyMotionAlphaMaskValue)(
            std::uint8_t destinationAlpha,
            std::uint8_t maskAlpha,
            bool thresholdMaskMode,
            int operation,
            std::uint8_t threshold) = nullptr;
        bool (*shouldSearchCachedMotionComposition)(
            const std::string &motionRef,
            const std::string &motionIcon) = nullptr;
    };

    // Small, versioned seam for optional motionplayer features.  The public
    // backend remains the only backend; private packages may register focused
    // controller implementations without copying or replacing it.
    struct MotionPlayerExtensionV4 {
        std::uint32_t abiVersion = 0;
        bool (*detectExtendedEmoteMode)(
            const detail::MotionSnapshot &snapshot) = nullptr;
        void (*collectControlMetadata)(
            const std::shared_ptr<const PSB::PSBDictionary> &base,
            detail::MotionSnapshot &snapshot) = nullptr;
        void (*configureNodeTree)(
            std::vector<detail::MotionNode> &nodes) = nullptr;
        void (*ensureControlState)(Player &player) = nullptr;
        bool (*hasActivePhysics)(const Player &player) = nullptr;
        void (*serializeControlState)(
            const Player &player,
            tTJSVariant &eye,
            tTJSVariant &bust,
            tTJSVariant &hair,
            tTJSVariant &parts) = nullptr;
        void (*unserializeControlState)(
            Player &player,
            const tTJSVariant &eye,
            const tTJSVariant &bust,
            const tTJSVariant &hair,
            const tTJSVariant &parts) = nullptr;
        void (*stepAutoBlink)(Player &player, double dt) = nullptr;
        void (*stepPhysics)(Player &player, double dt) = nullptr;
        const MotionRenderPolicyV1 *renderPolicy = nullptr;
        std::unique_ptr<MotionNativePlayerBackend> (*createNativePlayer)(
            const detail::MotionSnapshot &snapshot,
            std::string *error) = nullptr;
    };

    inline constexpr std::uint32_t kMotionPlayerExtensionAbiVersion = 4;

    bool registerMotionPlayerExtension(
        const MotionPlayerExtensionV4 *extension);
    const MotionPlayerExtensionV4 *motionPlayerExtension();
}
