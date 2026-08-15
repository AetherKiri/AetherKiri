#include "MotionPlayerExtension.h"

#include <atomic>

namespace motion {
    namespace {
        std::atomic<const MotionPlayerExtensionV4 *> g_extension{nullptr};
    }

    bool registerMotionPlayerExtension(
        const MotionPlayerExtensionV4 *extension) {
        if(!extension ||
           extension->abiVersion != kMotionPlayerExtensionAbiVersion) {
            return false;
        }

        const MotionPlayerExtensionV4 *expected = nullptr;
        if(g_extension.compare_exchange_strong(expected, extension)) {
            return true;
        }
        return expected == extension;
    }

    const MotionPlayerExtensionV4 *motionPlayerExtension() {
        return g_extension.load();
    }
}
