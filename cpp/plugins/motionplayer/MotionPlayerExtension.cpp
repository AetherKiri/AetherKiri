#include "MotionPlayerExtension.h"

#include <atomic>

namespace motion {
    namespace {
        std::atomic<const MotionPlayerExtensionV3 *> g_extension{nullptr};
    }

    bool registerMotionPlayerExtension(
        const MotionPlayerExtensionV3 *extension) {
        if(!extension ||
           extension->abiVersion != kMotionPlayerExtensionAbiVersion) {
            return false;
        }

        const MotionPlayerExtensionV3 *expected = nullptr;
        if(g_extension.compare_exchange_strong(expected, extension)) {
            return true;
        }
        return expected == extension;
    }

    const MotionPlayerExtensionV3 *motionPlayerExtension() {
        return g_extension.load();
    }
}
