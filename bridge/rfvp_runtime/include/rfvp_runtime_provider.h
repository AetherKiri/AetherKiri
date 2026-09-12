#pragma once

struct TVPGodotGpuBridgeCallbacks;
struct TVPGodotGpuBatchCallbacks;

namespace aetherkiri::rfvp {
void RegisterRuntimeProvider();
void RegisterGpuBridge(const TVPGodotGpuBridgeCallbacks* callbacks,
                       const TVPGodotGpuBatchCallbacks* batch_callbacks);
}
