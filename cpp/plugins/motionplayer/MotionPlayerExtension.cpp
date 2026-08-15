#include "MotionPlayerExtension.h"

#include <atomic>

namespace motion {
    namespace {
        std::atomic<const MotionPlayerExtensionV2 *> g_extension{nullptr};
        thread_local std::uint32_t g_suppressionDepth = 0;
    }

    ScopedMotionPlayerExtensionSuppression::
    ScopedMotionPlayerExtensionSuppression() {
        ++g_suppressionDepth;
    }

    ScopedMotionPlayerExtensionSuppression::
    ~ScopedMotionPlayerExtensionSuppression() {
        if(g_suppressionDepth != 0) {
            --g_suppressionDepth;
        }
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
        if(g_suppressionDepth != 0) {
            return nullptr;
        }
        return g_extension.load();
    }
}
