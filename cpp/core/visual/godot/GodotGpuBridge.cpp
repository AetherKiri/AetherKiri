#include "GodotGpuBridge.h"

namespace {
TVPGodotGpuBridgeCallbacks g_callbacks{};
bool g_registered = false;
TVPGodotGpuBatchCallbacks g_batch_callbacks{};
bool g_batch_registered = false;
thread_local uint32_t g_batch_scope_depth = 0;
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

extern "C" void TVPGodotGpuBatchRegister(
    const TVPGodotGpuBatchCallbacks *callbacks) {
    g_batch_callbacks = {};
    g_batch_registered = false;
    if (callbacks == nullptr ||
        callbacks->struct_size < sizeof(TVPGodotGpuBatchCallbacks) ||
        callbacks->abi_version !=
            TVP_GODOT_GPU_BATCH_CALLBACKS_ABI_VERSION) {
        return;
    }
    // Copy named ABI-v1 fields instead of the whole caller allocation so a
    // future, larger table remains safe for this implementation.
    g_batch_callbacks.struct_size = sizeof(TVPGodotGpuBatchCallbacks);
    g_batch_callbacks.abi_version =
        TVP_GODOT_GPU_BATCH_CALLBACKS_ABI_VERSION;
    g_batch_callbacks.begin_batch = callbacks->begin_batch;
    g_batch_callbacks.end_batch = callbacks->end_batch;
    g_batch_registered = callbacks->begin_batch != nullptr &&
        callbacks->end_batch != nullptr;
}

const TVPGodotGpuBatchCallbacks *TVPGodotGpuBatchGet() {
    return g_batch_registered ? &g_batch_callbacks : nullptr;
}

TVPGodotGpuBatchScope::TVPGodotGpuBatchScope(bool enabled) {
    if (!enabled) return;
    const auto *callbacks = TVPGodotGpuBatchGet();
    if (callbacks == nullptr || callbacks->begin_batch == nullptr ||
        callbacks->end_batch == nullptr) {
        return;
    }
    const uint64_t token = callbacks->begin_batch();
    if (token == 0) return;
    batch_token_ = token;
    end_batch_ = callbacks->end_batch;
    ++g_batch_scope_depth;
}

TVPGodotGpuBatchScope::~TVPGodotGpuBatchScope() noexcept {
    try {
        finish();
    } catch (...) {
        // Destructors are the fallback close path for early render returns.
        // A host callback must not turn that cleanup into std::terminate.
    }
}

bool TVPGodotGpuBatchScope::finish() {
    if (batch_token_ == 0) return true;
    const uint64_t token = batch_token_;
    auto *end_batch = end_batch_;
    batch_token_ = 0;
    end_batch_ = nullptr;
    if (g_batch_scope_depth != 0) --g_batch_scope_depth;
    return end_batch != nullptr && end_batch(token);
}

bool TVPGodotGpuBridgeBatchActive() {
    return g_batch_scope_depth != 0;
}
