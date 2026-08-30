//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Thread base class
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include "ThreadIntf.h"
#include "ThreadImpl.h"

#include <array>
#include <atomic>
#include <chrono>

namespace {

    constexpr std::size_t kVisualPhaseCount = 6;

    struct tTVPVisualRenderStatsAtomic {
        std::array<std::atomic<std::uint64_t>, kVisualPhaseCount> phase_count{};
        std::array<std::atomic<std::uint64_t>, kVisualPhaseCount> phase_ns{};
        std::atomic<std::uint64_t> dirty_rect_count{ 0 };
        std::atomic<std::uint64_t> dirty_pixels{ 0 };
        std::atomic<std::uint64_t> dirty_unite_count{ 0 };
        std::atomic<std::uint64_t> upload_count{ 0 };
        std::atomic<std::uint64_t> upload_success_count{ 0 };
        std::atomic<std::uint64_t> upload_bytes{ 0 };
        std::atomic<std::uint64_t> upload_ns{ 0 };
        std::atomic<std::uint64_t> readback_count{ 0 };
        std::atomic<std::uint64_t> readback_success_count{ 0 };
        std::atomic<std::uint64_t> readback_bytes{ 0 };
        std::atomic<std::uint64_t> readback_ns{ 0 };
        std::atomic<std::uint64_t> video_queued_count{ 0 };
        std::atomic<std::uint64_t> video_presented_count{ 0 };
        std::atomic<std::uint64_t> video_dropped_count{ 0 };
        std::atomic<std::uint64_t> video_converted_pixels{ 0 };
        std::atomic<std::uint64_t> video_convert_ns{ 0 };
        std::atomic<std::uint64_t> video_fallback_convert_count{ 0 };
    };

    tTVPVisualRenderStatsAtomic TVPVisualStats;

    std::uint64_t TVPVisualNowNs() {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
    }

    std::size_t TVPVisualPhaseIndex(tTVPVisualRenderPhase phase) {
        return static_cast<std::size_t>(phase);
    }

    void TVPRecordVisualPhase(tTVPVisualRenderPhase phase,
                              std::uint64_t elapsed_ns) {
        const std::size_t index = TVPVisualPhaseIndex(phase);
        TVPVisualStats.phase_count[index].fetch_add(1,
                                                    std::memory_order_relaxed);
        TVPVisualStats.phase_ns[index].fetch_add(elapsed_ns,
                                                 std::memory_order_relaxed);
    }

} // namespace

tTVPVisualPhaseTimer::tTVPVisualPhaseTimer(tTVPVisualRenderPhase phase) :
    Phase(phase), StartedNs(TVPVisualNowNs()) {}

tTVPVisualPhaseTimer::~tTVPVisualPhaseTimer() {
    TVPRecordVisualPhase(Phase, TVPVisualNowNs() - StartedNs);
}

void TVPRecordVisualDirtyRegion(std::uint64_t rect_count,
                                std::uint64_t pixels) {
    TVPVisualStats.dirty_rect_count.fetch_add(rect_count,
                                              std::memory_order_relaxed);
    TVPVisualStats.dirty_pixels.fetch_add(pixels, std::memory_order_relaxed);
}

void TVPRecordVisualDirtyUnite() {
    TVPVisualStats.dirty_unite_count.fetch_add(1, std::memory_order_relaxed);
}

void TVPRecordVisualUpload(std::uint64_t elapsed_ns, std::uint64_t bytes,
                           bool success) {
    TVPVisualStats.upload_count.fetch_add(1, std::memory_order_relaxed);
    if(success)
        TVPVisualStats.upload_success_count.fetch_add(
            1, std::memory_order_relaxed);
    TVPVisualStats.upload_bytes.fetch_add(bytes, std::memory_order_relaxed);
    TVPVisualStats.upload_ns.fetch_add(elapsed_ns, std::memory_order_relaxed);
}

void TVPRecordVisualReadback(std::uint64_t elapsed_ns, std::uint64_t bytes,
                             bool success) {
    TVPVisualStats.readback_count.fetch_add(1, std::memory_order_relaxed);
    if(success)
        TVPVisualStats.readback_success_count.fetch_add(
            1, std::memory_order_relaxed);
    TVPVisualStats.readback_bytes.fetch_add(bytes, std::memory_order_relaxed);
    TVPVisualStats.readback_ns.fetch_add(elapsed_ns, std::memory_order_relaxed);
}

void TVPRecordVisualVideoQueued() {
    TVPVisualStats.video_queued_count.fetch_add(1, std::memory_order_relaxed);
}

void TVPRecordVisualVideoPresented() {
    TVPVisualStats.video_presented_count.fetch_add(1,
                                                   std::memory_order_relaxed);
}

void TVPRecordVisualVideoDropped(std::uint64_t count) {
    TVPVisualStats.video_dropped_count.fetch_add(count,
                                                 std::memory_order_relaxed);
}

void TVPRecordVisualVideoConvert(std::uint64_t elapsed_ns, std::uint64_t pixels,
                                 bool used_fallback) {
    TVPVisualStats.video_converted_pixels.fetch_add(pixels,
                                                    std::memory_order_relaxed);
    TVPVisualStats.video_convert_ns.fetch_add(elapsed_ns,
                                              std::memory_order_relaxed);
    if(used_fallback) {
        TVPVisualStats.video_fallback_convert_count.fetch_add(
            1, std::memory_order_relaxed);
    }
}

tTVPVisualRenderStatsSnapshot TVPGetVisualRenderStats() {
    tTVPVisualRenderStatsSnapshot snapshot;
    const auto load_phase = [&](tTVPVisualRenderPhase phase,
                                std::uint64_t &count, std::uint64_t &ns) {
        const std::size_t index = TVPVisualPhaseIndex(phase);
        count =
            TVPVisualStats.phase_count[index].load(std::memory_order_relaxed);
        ns = TVPVisualStats.phase_ns[index].load(std::memory_order_relaxed);
    };
    load_phase(tTVPVisualRenderPhase::DrawDeviceUpdate, snapshot.update_count,
               snapshot.update_ns);
    load_phase(tTVPVisualRenderPhase::DrawDeviceShow, snapshot.show_count,
               snapshot.show_ns);
    load_phase(tTVPVisualRenderPhase::LayerCompleteWindow,
               snapshot.complete_window_count, snapshot.complete_window_ns);
    load_phase(tTVPVisualRenderPhase::LayerDraw, snapshot.layer_draw_count,
               snapshot.layer_draw_ns);
    load_phase(tTVPVisualRenderPhase::LayerBeforeCompletion,
               snapshot.before_completion_count, snapshot.before_completion_ns);
    load_phase(tTVPVisualRenderPhase::LayerAfterCompletion,
               snapshot.after_completion_count, snapshot.after_completion_ns);
#define TVP_LOAD_VISUAL_STAT(name)                                             \
    snapshot.name = TVPVisualStats.name.load(std::memory_order_relaxed)
    TVP_LOAD_VISUAL_STAT(dirty_rect_count);
    TVP_LOAD_VISUAL_STAT(dirty_pixels);
    TVP_LOAD_VISUAL_STAT(dirty_unite_count);
    TVP_LOAD_VISUAL_STAT(upload_count);
    TVP_LOAD_VISUAL_STAT(upload_success_count);
    TVP_LOAD_VISUAL_STAT(upload_bytes);
    TVP_LOAD_VISUAL_STAT(upload_ns);
    TVP_LOAD_VISUAL_STAT(readback_count);
    TVP_LOAD_VISUAL_STAT(readback_success_count);
    TVP_LOAD_VISUAL_STAT(readback_bytes);
    TVP_LOAD_VISUAL_STAT(readback_ns);
    TVP_LOAD_VISUAL_STAT(video_queued_count);
    TVP_LOAD_VISUAL_STAT(video_presented_count);
    TVP_LOAD_VISUAL_STAT(video_dropped_count);
    TVP_LOAD_VISUAL_STAT(video_converted_pixels);
    TVP_LOAD_VISUAL_STAT(video_convert_ns);
    TVP_LOAD_VISUAL_STAT(video_fallback_convert_count);
#undef TVP_LOAD_VISUAL_STAT
    return snapshot;
}

void TVPResetVisualRenderStats() {
    for(auto &value : TVPVisualStats.phase_count)
        value.store(0, std::memory_order_relaxed);
    for(auto &value : TVPVisualStats.phase_ns)
        value.store(0, std::memory_order_relaxed);
#define TVP_RESET_VISUAL_STAT(name)                                            \
    TVPVisualStats.name.store(0, std::memory_order_relaxed)
    TVP_RESET_VISUAL_STAT(dirty_rect_count);
    TVP_RESET_VISUAL_STAT(dirty_pixels);
    TVP_RESET_VISUAL_STAT(dirty_unite_count);
    TVP_RESET_VISUAL_STAT(upload_count);
    TVP_RESET_VISUAL_STAT(upload_success_count);
    TVP_RESET_VISUAL_STAT(upload_bytes);
    TVP_RESET_VISUAL_STAT(upload_ns);
    TVP_RESET_VISUAL_STAT(readback_count);
    TVP_RESET_VISUAL_STAT(readback_success_count);
    TVP_RESET_VISUAL_STAT(readback_bytes);
    TVP_RESET_VISUAL_STAT(readback_ns);
    TVP_RESET_VISUAL_STAT(video_queued_count);
    TVP_RESET_VISUAL_STAT(video_presented_count);
    TVP_RESET_VISUAL_STAT(video_dropped_count);
    TVP_RESET_VISUAL_STAT(video_converted_pixels);
    TVP_RESET_VISUAL_STAT(video_convert_ns);
    TVP_RESET_VISUAL_STAT(video_fallback_convert_count);
#undef TVP_RESET_VISUAL_STAT
}

//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
