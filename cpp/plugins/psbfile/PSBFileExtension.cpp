#include "PSBFileExtension.h"

#include <atomic>

namespace PSB {
    namespace {
        std::atomic<const PSBFileExtensionV1 *> g_extension{nullptr};
    }

    bool registerPSBFileExtension(const PSBFileExtensionV1 *extension) {
        if(extension == nullptr ||
           extension->abiVersion != kPSBFileExtensionAbiVersion) {
            return false;
        }
        const PSBFileExtensionV1 *expected = nullptr;
        if(g_extension.compare_exchange_strong(expected, extension)) {
            return true;
        }
        return expected == extension;
    }

    const PSBFileExtensionV1 *psbFileExtension() {
        return g_extension.load();
    }
}
