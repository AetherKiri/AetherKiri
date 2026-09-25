#pragma once

// Thin wrapper over the engine_api GPU bridge ABI
// (bridge/engine_api/include/engine_gpu_bridge.h). The callback tables,
// blend modes, and the tTVPRect/tTVPPointD geometry moved there so the
// Godot extension and the siglus/rfvp runtime glue no longer depend on
// krkr2 header paths. This header keeps only the krkr2-side C++ batch
// helpers whose implementations live in GodotGpuBridge.cpp.

#include "engine_gpu_bridge.h"

// Limits deferred GPU draining to a producer-defined command group.  The
// bridge callbacks are optional so non-Godot renderers retain their current
// immediate behavior.
class TVPGodotGpuBatchScope {
public:
    explicit TVPGodotGpuBatchScope(bool enabled = true);
    ~TVPGodotGpuBatchScope() noexcept;

    TVPGodotGpuBatchScope(const TVPGodotGpuBatchScope &) = delete;
    TVPGodotGpuBatchScope &operator=(const TVPGodotGpuBatchScope &) = delete;

    bool active() const { return batch_token_ != 0; }
    bool finish();

private:
    uint64_t batch_token_ = 0;
    bool (*end_batch_)(uint64_t batch_token) = nullptr;
};

bool TVPGodotGpuBridgeBatchActive();
