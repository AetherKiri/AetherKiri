//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Thread base class
//---------------------------------------------------------------------------
#ifndef ThreadIntfH
#define ThreadIntfH

#include "tjsNative.h"
#include <cstdint>
#include <functional>

//---------------------------------------------------------------------------
// tTVPThreadPriority
//---------------------------------------------------------------------------
enum tTVPThreadPriority {
    ttpIdle,
    ttpLowest,
    ttpLower,
    ttpNormal,
    ttpHigher,
    ttpHighest,
    ttpTimeCritical
};
//---------------------------------------------------------------------------

#include "ThreadImpl.h"

/*[*/
const tjs_int TVPMaxThreadNum = 8;
typedef const std::function<void(int)> &TVP_THREAD_TASK_FUNC;
/*]*/

TJS_EXP_FUNC_DEF(tjs_int, TVPGetProcessorNum, ());

TJS_EXP_FUNC_DEF(tjs_int, TVPGetThreadNum, ());

TJS_EXP_FUNC_DEF(void, TVPExecThreadTask,
                 (int numThreads, TVP_THREAD_TASK_FUNC func));

enum class tTVPVisualRenderPhase : std::uint8_t {
    DrawDeviceUpdate,
    DrawDeviceShow,
    LayerCompleteWindow,
    LayerDraw,
    LayerBeforeCompletion,
    LayerAfterCompletion,
};

struct tTVPVisualRenderStatsSnapshot {
    std::uint64_t update_count = 0;
    std::uint64_t update_ns = 0;
    std::uint64_t show_count = 0;
    std::uint64_t show_ns = 0;
    std::uint64_t complete_window_count = 0;
    std::uint64_t complete_window_ns = 0;
    std::uint64_t layer_draw_count = 0;
    std::uint64_t layer_draw_ns = 0;
    std::uint64_t before_completion_count = 0;
    std::uint64_t before_completion_ns = 0;
    std::uint64_t after_completion_count = 0;
    std::uint64_t after_completion_ns = 0;
    std::uint64_t dirty_rect_count = 0;
    std::uint64_t dirty_pixels = 0;
    std::uint64_t dirty_unite_count = 0;
    std::uint64_t upload_count = 0;
    std::uint64_t upload_success_count = 0;
    std::uint64_t upload_bytes = 0;
    std::uint64_t upload_ns = 0;
    std::uint64_t readback_count = 0;
    std::uint64_t readback_success_count = 0;
    std::uint64_t readback_bytes = 0;
    std::uint64_t readback_ns = 0;
    std::uint64_t video_queued_count = 0;
    std::uint64_t video_presented_count = 0;
    std::uint64_t video_dropped_count = 0;
    std::uint64_t video_converted_pixels = 0;
    std::uint64_t video_convert_ns = 0;
    std::uint64_t video_fallback_convert_count = 0;
};

class tTVPVisualPhaseTimer {
public:
    explicit tTVPVisualPhaseTimer(tTVPVisualRenderPhase phase);
    ~tTVPVisualPhaseTimer();

    tTVPVisualPhaseTimer(const tTVPVisualPhaseTimer &) = delete;
    tTVPVisualPhaseTimer &operator=(const tTVPVisualPhaseTimer &) = delete;

private:
    tTVPVisualRenderPhase Phase;
    std::uint64_t StartedNs;
};

void TVPRecordVisualDirtyRegion(std::uint64_t rect_count, std::uint64_t pixels);
void TVPRecordVisualDirtyUnite();
void TVPRecordVisualUpload(std::uint64_t elapsed_ns, std::uint64_t bytes,
                           bool success);
void TVPRecordVisualReadback(std::uint64_t elapsed_ns, std::uint64_t bytes,
                             bool success);
void TVPRecordVisualVideoQueued();
void TVPRecordVisualVideoPresented();
void TVPRecordVisualVideoDropped(std::uint64_t count = 1);
void TVPRecordVisualVideoConvert(std::uint64_t elapsed_ns, std::uint64_t pixels,
                                 bool used_fallback);
tTVPVisualRenderStatsSnapshot TVPGetVisualRenderStats();
void TVPResetVisualRenderStats();

#endif
