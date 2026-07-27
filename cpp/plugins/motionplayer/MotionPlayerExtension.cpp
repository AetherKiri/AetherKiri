#include "MotionPlayerExtension.h"

#include <atomic>

namespace motion {
    namespace {
        std::atomic<const MotionPlayerExtensionV2 *> g_extension{nullptr};
    }

    bool registerMotionPlayerExtension(
        const MotionPlayerExtensionV2 *extension) {
        if(!extension ||
           extension->abiVersion != kMotionPlayerExtensionAbiVersion) {
            return false;
        }

        const MotionPlayerExtensionV2 *expected = nullptr;
        if(g_extension.compare_exchange_strong(expected, extension)) {
            return true;
        }
        return expected == extension;
    }

    const MotionPlayerExtensionV2 *motionPlayerExtension() {
        return g_extension.load();
    }
}
