#pragma once

#include <cstdint>
#include <memory>
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

    // Small, versioned seam for optional motionplayer features.  The public
    // backend remains the only backend; private packages may register focused
    // controller implementations without copying or replacing it.
    struct MotionPlayerExtensionV1 {
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
    };

    inline constexpr std::uint32_t kMotionPlayerExtensionAbiVersion = 1;

    bool registerMotionPlayerExtension(
        const MotionPlayerExtensionV1 *extension);
    const MotionPlayerExtensionV1 *motionPlayerExtension();
}
