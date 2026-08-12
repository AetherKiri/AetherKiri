#pragma once

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

    // Versioned, SDK-neutral player seam. Private packages may implement it;
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
    struct MotionPlayerExtensionV3 {
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

    inline constexpr std::uint32_t kMotionPlayerExtensionAbiVersion = 3;

    bool registerMotionPlayerExtension(
        const MotionPlayerExtensionV3 *extension);
    const MotionPlayerExtensionV3 *motionPlayerExtension();
}
