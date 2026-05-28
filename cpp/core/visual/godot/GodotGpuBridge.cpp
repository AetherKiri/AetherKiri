#include "GodotGpuBridge.h"

namespace {
TVPGodotGpuBridgeCallbacks g_callbacks{};
bool g_registered = false;
} // namespace

extern "C" void TVPGodotGpuBridgeRegister(
    const TVPGodotGpuBridgeCallbacks *callbacks) {
    if (callbacks == nullptr) {
        g_callbacks = {};
        g_registered = false;
        return;
    }
    g_callbacks = *callbacks;
    g_registered = true;
}

const TVPGodotGpuBridgeCallbacks *TVPGodotGpuBridgeGet() {
    return g_registered ? &g_callbacks : nullptr;
}

