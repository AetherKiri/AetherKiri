#pragma once

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace motion::detail {

struct MotionRenderBatchStats {
    std::uint64_t onPaintCalls = 0;
    std::uint64_t drawCompatCalls = 0;
    std::uint64_t nativeRenderCalls = 0;
    double onPaintMs = 0.0;
    double drawCompatMs = 0.0;
    double nativeRenderMs = 0.0;
};

inline bool motionRenderBatchProfileEnabled() {
    const char *value = std::getenv("AETHERKIRI_MOTION_RENDER_PROFILE");
    return value && *value && std::strcmp(value, "0") != 0;
}

inline thread_local MotionRenderBatchStats *g_motionRenderBatchStats = nullptr;

inline void motionRenderBatchRecordOnPaint(double elapsedMs) {
    if(!g_motionRenderBatchStats) return;
    ++g_motionRenderBatchStats->onPaintCalls;
    g_motionRenderBatchStats->onPaintMs += elapsedMs;
}

inline void motionRenderBatchRecordDrawCompat(double elapsedMs) {
    if(!g_motionRenderBatchStats) return;
    ++g_motionRenderBatchStats->drawCompatCalls;
    g_motionRenderBatchStats->drawCompatMs += elapsedMs;
}

inline void motionRenderBatchRecordNativeRender(double elapsedMs) {
    if(!g_motionRenderBatchStats) return;
    ++g_motionRenderBatchStats->nativeRenderCalls;
    g_motionRenderBatchStats->nativeRenderMs += elapsedMs;
}

class ScopedMotionRenderBatchProfile {
public:
    explicit ScopedMotionRenderBatchProfile(bool enabled)
        : previous_(g_motionRenderBatchStats), owns_(enabled && !previous_) {
        if(owns_) {
            g_motionRenderBatchStats = &stats_;
        }
    }

    ~ScopedMotionRenderBatchProfile() {
        if(owns_) {
            g_motionRenderBatchStats = previous_;
        }
    }

    bool owns() const { return owns_; }
    const MotionRenderBatchStats &stats() const { return stats_; }

private:
    MotionRenderBatchStats *previous_ = nullptr;
    MotionRenderBatchStats stats_;
    bool owns_ = false;
};

}  // namespace motion::detail
