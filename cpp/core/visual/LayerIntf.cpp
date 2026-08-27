//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Layer Management
//---------------------------------------------------------------------------
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif

#include "tjsCommHead.h"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <spdlog/spdlog.h>
#ifdef __ANDROID__
#include <android/log.h>
#endif

#include "tjsArray.h"
#include "tjsError.h"
#include "LayerIntf.h"
#include "MsgIntf.h"
#include "LayerBitmapIntf.h"
#include "LayerTreeOwner.h"
#include "GraphicsLoaderIntf.h"
#include "StorageIntf.h"
#include "ScriptMgnIntf.h"
#include "tvpgl.h"
#include "EventIntf.h"
#include "SysInitIntf.h"
#include "TickCount.h"
#include "DebugIntf.h"
#include "LayerManager.h"
#include "BitmapIntf.h"

#include "TVPColor.h"
#include "FontBaseline.h"
// #include "TVPSysFont.h"
#include "FontRasterizer.h"
#include "RectItf.h"
#include "FontSystem.h"
#include "tjsDictionary.h"
#include "ConfigManager/IndividualConfigManager.h"
#include "vkdefine.h"
#include "RenderManager.h"
#include "godot/GodotRenderManager.h"
#include "FontImpl.h"
#include "LayerCompletionCoordinates.h"
#include "PimgCompositeBounds.h"
#include "../../plugins/psbfile/PSBMedia.h"

extern void TVPSetFontRasterizer(tjs_int index);

extern tjs_int TVPGetFontRasterizer();

extern FontRasterizer *GetCurrentRasterizer();

extern void TVPMapPrerenderedFont(const tTVPFont &font, const ttstr &storage);

extern void TVPUnmapPrerenderedFont(const tTVPFont &font);

extern tjs_int TVPGetCursor(const ttstr &name);

namespace {
    struct LayerCompletionProfileState {
        bool active = false;
        std::size_t beforeCalls = 0;
        std::size_t onPaintCalls = 0;
        double onPaintMs = 0.0;
        double maxOnPaintMs = 0.0;
        std::string maxOnPaintLayer;
        std::size_t transitionCalls = 0;
        std::size_t maxDepth = 0;
        std::size_t depth = 0;
        std::size_t completionCalls = 0;
        std::chrono::steady_clock::time_point started{};
    };

    thread_local LayerCompletionProfileState g_layerCompletionProfile;

    inline bool layerCompletionProfileEnabled() {
        const char *enabled = std::getenv("AETHERKIRI_MOTION_RENDER_PROFILE");
        return enabled && *enabled && std::strcmp(enabled, "0") != 0;
    }

    inline double layerCompletionProfileSlowMs() {
        const char *value = std::getenv("AETHERKIRI_MOTION_RENDER_SLOW_MS");
        if (!value || !*value) return 100.0;
        char *end = nullptr;
        const double parsed = std::strtod(value, &end);
        return end != value && std::isfinite(parsed) && parsed > 0.0
            ? parsed
            : 100.0;
    }

    inline double motionAssignmentProfileSlowMs() {
        const char *value =
            std::getenv("AETHERKIRI_MOTION_ASSIGN_SLOW_MS");
        if(!value || !*value) return 5.0;
        char *end = nullptr;
        const double parsed = std::strtod(value, &end);
        return end != value && std::isfinite(parsed) && parsed > 0.0
            ? parsed
            : 5.0;
    }
}

//---------------------------------------------------------------------------
// global flags
//---------------------------------------------------------------------------
bool TVPFreeUnusedLayerCache = false;

static std::atomic<tjs_int> TVPLayerInstanceCount{0};
static std::atomic<int64_t> TVPLayerBitmapTotalBytes{0};
static std::atomic<bool> TVPFullGpuCompletionRequested{false};
static std::mutex TVPExchangedKagPageMutex;
static std::unordered_set<const tTJSNI_BaseLayer *>
    TVPExchangedHiddenKagPages;
static std::unordered_map<const tTJSNI_BaseLayer *, bool>
    TVPKagPageLastObservedVisibility;
struct TVPHiddenKagAssignmentStreak {
    std::chrono::steady_clock::time_point last{};
    std::size_t count = 0;
};
static std::unordered_map<const tTJSNI_BaseLayer *,
                          TVPHiddenKagAssignmentStreak>
    TVPHiddenKagAssignmentStreaks;
static std::unordered_set<const tTJSNI_BaseLayer *>
    TVPMotionSwapAssignmentTargets;

static bool TVPLayerTransitionTraceEnabled();

static bool TVPIsKagBackgroundPair(const tTJSNI_BaseLayer *first,
                                   const tTJSNI_BaseLayer *second) {
    if(!first || !second) {
        return false;
    }
    return (first->GetName() == TJS_W("表-背景") &&
            second->GetName() == TJS_W("裏-背景")) ||
           (first->GetName() == TJS_W("裏-背景") &&
            second->GetName() == TJS_W("表-背景"));
}

static bool TVPIsKagTransitionMotionAssignment(
    const tTJSNI_BaseLayer *target,
    const tTJSNI_BaseLayer *source) {
    if(!target || !source || !target->GetVisible() ||
       !source->GetName().IsEmpty() || source->GetVisible() ||
       target->GetWidth() != source->GetWidth() ||
       target->GetHeight() != source->GetHeight()) {
        return false;
    }

    const std::string target_name = target->GetName().AsStdString();
    constexpr const char *transition_prefix = "trans_";
    if(target_name.rfind(transition_prefix, 0) != 0) {
        return false;
    }

    auto *page = target->GetParent();
    auto *page_root = page ? page->GetParent() : nullptr;
    if(!page || !page_root || source->GetParent() != page_root) {
        return false;
    }
    for(tjs_uint index = 0; index < page_root->GetCount(); ++index) {
        auto *candidate =
            page_root->GetChildren(static_cast<tjs_int>(index));
        if(candidate != page &&
           TVPIsKagBackgroundPair(page, candidate)) {
            return true;
        }
    }
    return false;
}

// D3DEmote keeps the result of each character render in one invisible,
// full-stage scratch layer.  The legacy assignImages() path aliases the
// source texture into the destination; the next character then clears and
// rewrites that same texture, making the preceding character disappear.  A
// scratch layer is safe to hand off with AssignMotionImages(), which swaps
// the completed bitmap out of the destination and gives the old bitmap back
// to the scratch layer for the next render.
static bool TVPIsAffineSourceMotionScratch(
    const tTJSNI_BaseLayer *target,
    const tTJSNI_BaseLayer *source) {
    if(!target || !target->GetVisible() || target->GetName().IsEmpty() ||
       !source || source->GetVisible() || !source->GetName().IsEmpty()) {
        return false;
    }
    auto *source_parent = source->GetParent();
    auto *target_parent = target->GetParent();
    if(!source_parent || !target_parent ||
       source_parent->GetName().AsStdString() != "AffineSource情報プール用") {
        return false;
    }
    // Title-page and UI composition also use the same hidden pool.  Their
    // internal work layers are not visible page targets and must retain the
    // ordinary assignImages() semantics.
    return target_parent->GetName().AsStdString() !=
        "AffineSource情報プール用";
}

static bool TVPShouldDeferKagTransitionMotionAssignment(
    const tTJSNI_BaseLayer *target,
    const tTJSNI_BaseLayer *source) {
    if(!target || !source || source->GetName().IsEmpty() == false ||
       source->GetVisible()) {
        return false;
    }
    const auto target_name = target->GetName().AsStdString();
    if(target_name.rfind("trans_", 0) != 0) {
        return false;
    }
    auto *target_page = target->GetParent();
    if(!target_page || !target_page->DebugIsInTransition()) {
        return false;
    }
    // The scratch layer used by D3DEmote lives under トップレイヤ, not under
    // 裏-背景.  The relevant relationship is therefore the destination's
    // active transition, not source->Parent().
    const auto page_name = target_page->GetName().AsStdString();
    return page_name == "表-背景" || page_name == "裏-背景";
}

static void TVPClearExchangedKagPageRouting(
    const tTJSNI_BaseLayer *first,
    const tTJSNI_BaseLayer *second) {
    std::lock_guard<std::mutex> lock(TVPExchangedKagPageMutex);
    TVPExchangedHiddenKagPages.erase(first);
    TVPExchangedHiddenKagPages.erase(second);
    TVPHiddenKagAssignmentStreaks.clear();
}

static tTJSNI_BaseLayer *TVPResolveExchangedKagAssignmentTarget(
    tTJSNI_BaseLayer *target,
    tTJSNI_BaseLayer *source) {
    if(!target || !source || target->GetName().IsEmpty() ||
       !target->GetVisible() ||
       !source->GetName().IsEmpty() || source->GetVisible()) {
        return nullptr;
    }

    auto *hidden_page = target->GetParent();
    auto *page_root = hidden_page ? hidden_page->GetParent() : nullptr;
    if(!hidden_page || !page_root || source->GetParent() != page_root) {
        return nullptr;
    }

    const bool page_visible =
        hidden_page->GetVisible() &&
        hidden_page->GetParentVisible();
    bool known_stale = false;
    {
        std::lock_guard<std::mutex> lock(TVPExchangedKagPageMutex);
        auto &last_visible =
            TVPKagPageLastObservedVisibility[hidden_page];
        if(page_visible) {
            last_visible = true;
            TVPExchangedHiddenKagPages.erase(hidden_page);
            TVPHiddenKagAssignmentStreaks.erase(target);
            return nullptr;
        }
        if(last_visible) {
            TVPExchangedHiddenKagPages.insert(hidden_page);
        }
        last_visible = false;
        known_stale =
            TVPExchangedHiddenKagPages.find(hidden_page) !=
            TVPExchangedHiddenKagPages.end();
    }

    tTJSNI_BaseLayer *visible_page = nullptr;
    for(tjs_uint page_index = 0;
        page_index < page_root->GetCount(); ++page_index) {
        auto *candidate_page =
            page_root->GetChildren(static_cast<tjs_int>(page_index));
        if(!candidate_page || candidate_page == hidden_page ||
           !candidate_page->GetVisible() ||
           !candidate_page->GetParentVisible() ||
           !TVPIsKagBackgroundPair(hidden_page, candidate_page)) {
            continue;
        }
        visible_page = candidate_page;
        break;
    }
    if(!visible_page) {
        return nullptr;
    }

    const auto normalized_layer_name = [](const ttstr &name) {
        std::string value = name.AsStdString();
        constexpr const char *transition_prefix = "trans_";
        if(value.rfind(transition_prefix, 0) == 0) {
            value.erase(0, std::strlen(transition_prefix));
        }
        return value;
    };
    const auto target_name =
        normalized_layer_name(target->GetName());
    for(tjs_uint child_index = 0;
        child_index < visible_page->GetCount(); ++child_index) {
        auto *candidate =
            visible_page->GetChildren(
                static_cast<tjs_int>(child_index));
        if(!candidate || candidate == target ||
           !candidate->GetVisible() ||
           !candidate->GetParentVisible() ||
           normalized_layer_name(candidate->GetName()) != target_name ||
           candidate->GetWidth() != target->GetWidth() ||
           candidate->GetHeight() != target->GetHeight()) {
            continue;
        }
        // The visible page's `trans_*` child is the destination of the
        // upcoming crossfade.  Before StartTransition() sets InTransition,
        // redirecting the hidden-page motion frame into that child presents
        // the new frame for one tick (new -> fade -> new).  Keep the frame on
        // the hidden page so the transition snapshots old/new pages in the
        // intended order.  Once a transition is active, the assignment path
        // below is already guarded by TVPShouldDefer...().
        const auto candidate_name = candidate->GetName().AsStdString();
        const bool candidate_is_transition_layer =
            candidate_name.rfind("trans_", 0) == 0;
        if(known_stale && candidate_is_transition_layer &&
           !hidden_page->DebugIsInTransition() &&
           !visible_page->DebugIsInTransition()) {
            if(TVPLayerTransitionTraceEnabled()) {
                spdlog::info(
                    "LayerAssign route=transition-source target={} source={} "
                    "resolved={} reason=pre-transition-visible-trans",
                    static_cast<const void *>(target),
                    static_cast<const void *>(source),
                    static_cast<const void *>(candidate));
            }
            return nullptr;
        }
        return known_stale ? candidate : nullptr;
    }

    bool visible_page_has_content_layer = false;
    for(tjs_uint child_index = 0;
        child_index < visible_page->GetCount(); ++child_index) {
        auto *candidate = visible_page->GetChildren(
            static_cast<tjs_int>(child_index));
        if(candidate && candidate->GetVisible() &&
           candidate->GetParentVisible() &&
           candidate->GetWidth() >= target->GetWidth() / 2 &&
           candidate->GetHeight() >= target->GetHeight() / 2) {
            visible_page_has_content_layer = true;
            break;
        }
    }
    if(visible_page_has_content_layer ||
       hidden_page->DebugIsInTransition() ||
       visible_page->DebugIsInTransition()) {
        std::lock_guard<std::mutex> lock(
            TVPExchangedKagPageMutex);
        TVPHiddenKagAssignmentStreaks.erase(target);
        return nullptr;
    }

    // A page can already be marked stale when the script creates a brand-new
    // layer on it for the next transition.  Moving that layer on its first
    // AssignImages() leaks the incoming frame onto the visible page for one
    // present.  Require persistence from the target itself before treating it
    // as an orphan left behind by a completed page exchange.
    const auto now = std::chrono::steady_clock::now();
    bool persistent_hidden_target = false;
    {
        std::lock_guard<std::mutex> lock(
            TVPExchangedKagPageMutex);
        auto &streak =
            TVPHiddenKagAssignmentStreaks[target];
        if(streak.count == 0 ||
           now - streak.last >
               std::chrono::milliseconds(250)) {
            streak.count = 0;
        }
        streak.last = now;
        persistent_hidden_target = ++streak.count >= 12;
    }
    if(!persistent_hidden_target) {
        return nullptr;
    }
    // The page exchange can leave a continuously animated KAG layer attached
    // to the now-hidden page while the visible page contains only message and
    // click-wait layers. There is no destination to copy into in that shape;
    // move the authored layer itself to the active page. Preserve its order
    // so it remains below the page's message/UI children. Subsequent KAG page
    // exchanges carry the same live layer normally.
    const auto target_order = target->GetOrderIndex();
    {
        std::lock_guard<std::mutex> lock(TVPExchangedKagPageMutex);
        TVPHiddenKagAssignmentStreaks.erase(target);
        TVPMotionSwapAssignmentTargets.insert(target);
    }
    target->SetParent(visible_page);
    if(visible_page->GetCount() > 0) {
        target->SetOrderIndex(
            std::min<tjs_int>(
                static_cast<tjs_int>(target_order),
                static_cast<tjs_int>(visible_page->GetCount() - 1)));
    }
    if(auto logger = spdlog::get("plugin")) {
        const char *debug = std::getenv("AETHERKIRI_MOTION_DEBUG");
        if(debug && *debug && *debug != '0') {
            logger->info(
                "motion reparent stale KAG page target: target={} "
                "visiblePage={} order={}",
                target->GetName().AsStdString(),
                visible_page->GetName().AsStdString(),
                target->GetOrderIndex());
        }
    }
    return target;
}

tjs_int TVPGetLayerCount() { return TVPLayerInstanceCount.load(std::memory_order_relaxed); }
tjs_uint64 TVPGetLayerTotalBitmapBytes() {
    auto v = TVPLayerBitmapTotalBytes.load(std::memory_order_relaxed);
    return v > 0 ? static_cast<tjs_uint64>(v) : 0;
}

void TVPRequestFullGpuCompletion() {
    TVPFullGpuCompletionRequested.store(true, std::memory_order_release);
}

static bool TVPConsumeFullGpuCompletionRequest() {
    return TVPFullGpuCompletionRequested.exchange(
        false, std::memory_order_acq_rel);
}

static int64_t TVPCalcMainImageBytes(tTVPBaseTexture *img) {
    if(!img) return 0;
    return static_cast<int64_t>(img->GetWidth()) * img->GetHeight() * (img->GetBPP() / 8);
}

static bool TVPLayerDebugEnabled() {
    static const bool enabled = [] {
        const char *value = std::getenv("AETHERKIRI_LAYER_DEBUG");
        return value && *value && *value != '0';
    }();
    return enabled;
}

static bool TVPLayerLoadTraceEnabled() {
    static const bool enabled = [] {
        const char *value = std::getenv("AETHERKIRI_IMAGE_LOAD_TRACE");
        return value && *value && *value != '0';
    }();
    return enabled;
}

static bool TVPAffineTraceEnabled() {
    static const bool enabled = [] {
        const char *value = std::getenv("AETHERKIRI_AFFINE_TRACE");
        return value && *value && *value != '0';
    }();
    return enabled;
}

// Narrow, opt-in tracing for the dark image-error placeholders emitted by
// game-side PSD/PBD loaders.  Keep this separate from the broad layer trace so
// a render diagnosis can identify the owning layer without flooding normal
// runs with every fill/draw operation.
static bool TVPLayerErrorTraceEnabled() {
    static const bool enabled = [] {
        const char *value = std::getenv("AETHERKIRI_LAYER_ERROR_TRACE");
        return value && *value && *value != '0';
    }();
    return enabled;
}

static bool TVPLayerErrorTraceTake() {
    if(!TVPLayerErrorTraceEnabled())
        return false;
    static std::atomic<int> count{0};
    return count.fetch_add(1, std::memory_order_relaxed) < 256;
}

static bool TVPLayerDebugTake() {
    if(!TVPLayerDebugEnabled()) {
        return false;
    }
    static std::atomic<int> count{0};
    return count.fetch_add(1, std::memory_order_relaxed) < 50000;
}

static bool TVPLayerInputTraceEnabled() {
    const char *value = std::getenv("AETHERKIRI_INPUT_TRACE");
    return value && *value && *value != '0';
}

static bool TVPLayerTransitionTraceEnabled() {
    const char *value = std::getenv("AETHERKIRI_TRANS_TRACE");
    return value && *value && *value != '0';
}

static void TVPTraceLayerTransition(const char *event,
                                    const tTJSNI_BaseLayer *layer,
                                    const tTJSNI_BaseLayer *src = nullptr,
                                    tjs_error status = TJS_S_OK) {
    if(!TVPLayerTransitionTraceEnabled())
        return;
    spdlog::info(
        "LayerTrans {} layer={} native={} owner={} shutdown={} visible={} "
        "transition={} src={} srcNative={} srcOwner={} srcShutdown={} "
        "eventDisabled={} systemEventDisabled={} status={}",
        event ? event : "", layer ? layer->GetName().AsStdString() : "<null>",
        static_cast<const void *>(layer),
        layer ? static_cast<const void *>(layer->GetOwnerNoAddRef()) : nullptr,
        "<private>",
        layer && layer->GetVisible() ? "yes" : "no",
        layer && layer->DebugIsInTransition() ? "yes" : "no",
        src ? src->GetName().AsStdString() : "<null>",
        static_cast<const void *>(src),
        src ? static_cast<const void *>(src->GetOwnerNoAddRef()) : nullptr,
        "<private>",
        TVPEventDisabled ? "yes" : "no",
        TVPGetSystemEventDisabledState() ? "yes" : "no", status);
}

#ifdef __ANDROID__
#define AETHER_LAYER_INPUT_TRACE_LOG(...)                                       \
    do {                                                                       \
        if(TVPLayerInputTraceEnabled()) {                                       \
            __android_log_print(ANDROID_LOG_INFO, "aether-input", __VA_ARGS__); \
        }                                                                      \
    } while(0)
#else
#define AETHER_LAYER_INPUT_TRACE_LOG(...)                                       \
    do {                                                                       \
    } while(0)
#endif

static bool TVPLayerDrawTraceEnabled() {
    static const bool enabled = [] {
        const char *value = std::getenv("AETHERKIRI_LAYER_DRAW_TRACE");
        return value && *value && *value != '0';
    }();
    return enabled;
}

static bool TVPLayerDrawTraceTake() {
    if(!TVPLayerDrawTraceEnabled())
        return false;
    static std::atomic<int> count{0};
    return count.fetch_add(1, std::memory_order_relaxed) < 60000;
}

static bool TVPLayerDrawTraceName(const ttstr &name) {
    if(!TVPLayerDrawTraceEnabled())
        return false;
    if(const char *all = std::getenv("AETHERKIRI_LAYER_DRAW_TRACE_ALL");
       all && *all && *all != '0')
        return true;
    const std::string s = name.AsStdString();
    return s.find("表-背景") != std::string::npos ||
           s.find("メッセージレイヤ") != std::string::npos ||
           s == "colorframe" || s == "colorbase" || s == "colorover" ||
           s == "title_bg" || s == "title_logo" ||
           s.find("表メッセージレイヤ1") != std::string::npos ||
           s.find("表メッセージレイヤ2") != std::string::npos ||
           s.find("CG View Layer") != std::string::npos ||
           s == "face" || s == "trans_face" || s == "hide_face" ||
           s == "秀明" || s == "和奏" ||
           s == "item3" || s == "item6" ||
           s == "chflash" || s == "chview" || s == "chframe" ||
           s == "ev" || s == "trans_ev" ||
           s == "frame" || s == "truss" ||
           s == "stage2" ||
           s.find("mono_") != std::string::npos ||
           s.find("/02") != std::string::npos ||
           s.find("<01") != std::string::npos ||
           s.find("<02") != std::string::npos;
}

// Diagnostic escape hatch for comparing the renderer with the historical
// explicit ConvertLayerType() contract.  It is intentionally environment
// gated so the compatibility build can be tested without carrying a second
// source variant.
static bool TVPDisableAutomaticAlphaNormalization() {
    const char *value = std::getenv("AETHERKIRI_DISABLE_AUTO_ALPHA_CONVERT");
    return value && *value && *value != '0';
}

static std::atomic<bool> TVPLayerDrawTraceArmedFlag{false};

static bool TVPLayerDrawTraceIsPreviewLayer(tTJSNI_BaseLayer *layer) {
    if(!layer)
        return false;
    const std::string s = layer->GetName().AsStdString();
    return s.find("CG View Layer") != std::string::npos ||
           (s.find("表メッセージレイヤ2") != std::string::npos &&
            layer->GetVisible() && layer->DebugGetVisibleChildrenCount() > 0);
}

static bool TVPLayerDrawTraceScriptPreviewActive() {
    try {
        tTJSVariant result;
        TVPExecuteExpression(
            TJS_W("typeof kag == \"Object\" && kag && "
                  "kag.currentStorage == \"cgmode.ks\" && "
                  "(kag.currentLabel == \"*viewtrans\" || "
                  "kag.currentLabel == \"*view_rclick\")"),
            &result);
        return result.operator bool();
    } catch(...) {
        return false;
    }
}

static bool TVPLayerDrawTraceArmed() {
    if(!TVPLayerDrawTraceEnabled())
        return false;
    if(TVPLayerDrawTraceArmedFlag.load(std::memory_order_relaxed))
        return true;
    if(TVPLayerDrawTraceScriptPreviewActive()) {
        TVPLayerDrawTraceArmedFlag.store(true, std::memory_order_relaxed);
        spdlog::info("LayerDrawGPU trace armed by cgmode preview script");
        return true;
    }
    return false;
}

static bool TVPLayerDrawTraceArmIfNeeded(tTJSNI_BaseLayer *layer) {
    if(TVPLayerDrawTraceArmed())
        return true;
    if(const char *arm_name = std::getenv("AETHERKIRI_LAYER_DRAW_TRACE_ARM_NAME");
       arm_name && *arm_name && layer &&
       layer->GetName().AsStdString() != arm_name) {
        return false;
    }
    if(layer && TVPLayerDrawTraceName(layer->GetName())) {
        TVPLayerDrawTraceArmedFlag.store(true, std::memory_order_relaxed);
        spdlog::info("LayerDrawGPU trace armed by focus layer={}",
                     layer->GetName().AsStdString());
        return true;
    }
    if(TVPLayerDrawTraceIsPreviewLayer(layer)) {
        TVPLayerDrawTraceArmedFlag.store(true, std::memory_order_relaxed);
        spdlog::info("LayerDrawGPU trace armed by layer={}",
                     layer->GetName().AsStdString());
        return true;
    }
    return false;
}

static void TVPTraceLayerDrawGpu(const char *event,
                                 tTJSNI_BaseLayer *layer,
                                 const tTVPRect &dest,
                                 const tTVPRect &clip,
                                 tTVPDrawable *target) {
    const char *standDump = std::getenv("AETHERKIRI_DUMP_STAND_LAYERS");
    const bool standDumpEnabled =
        standDump && *standDump && *standDump != '0';
    if(!layer || (!standDumpEnabled &&
                  (!TVPLayerDrawTraceName(layer->GetName()) ||
                   !TVPLayerDrawTraceArmIfNeeded(layer) ||
                   !TVPLayerDrawTraceTake())))
        return;
    spdlog::info(
        "LayerDrawGPU {} layer={} target={} dest=({},{} {}x{}) clip=({},{} {}x{}) "
        "pos=({}, {}) size={}x{} image_pos=({}, {}) visible={} opacity={} image={} children={} "
        "visible_children={} transition={} trans_children={}",
        event, layer->GetName().AsStdString(), static_cast<const void *>(target),
        dest.left, dest.top, dest.get_width(), dest.get_height(), clip.left,
        clip.top, clip.get_width(), clip.get_height(), layer->GetLeft(),
        layer->GetTop(), layer->GetWidth(), layer->GetHeight(),
        layer->GetImageLeft(), layer->GetImageTop(),
        layer->GetVisible() ? "yes" : "no", layer->GetOpacity(),
        layer->GetMainImage() ? "yes" : "no", layer->GetCount(),
        layer->DebugGetVisibleChildrenCount(),
        layer->DebugIsInTransition() ? "yes" : "no",
        layer->DebugIsTransWithChildren() ? "yes" : "no");

    // Focused one-shot source inspection for compatibility debugging.  The
    // virtual motion slices are drawn through several temporary layers, so a
    // load-time sample alone cannot tell us whether a later AssignImages or
    // cache update restored straight-alpha pixels before composition.
    if(const char *pixels = std::getenv("AETHERKIRI_LAYER_SOURCE_PIXELS");
       pixels && *pixels && *pixels != '0' && event &&
       std::strcmp(event, "begin") == 0 && layer->GetMainImage()) {
        static std::unordered_set<std::string> sampled_names;
        const std::string name = layer->GetName().AsStdString();
        if(sampled_names.insert(name).second) {
            auto *image = layer->GetMainImage()->GetTexture();
            if(image) {
                const tjs_uint width = image->GetWidth();
                const tjs_uint height = image->GetHeight();
                const tjs_uint step_x = std::max<tjs_uint>(1, width / 48);
                const tjs_uint step_y = std::max<tjs_uint>(1, height / 48);
                tjs_uint min_alpha = 255;
                tjs_uint max_alpha = 0;
                tjs_uint zero_alpha_nonzero_rgb = 0;
                tjs_uint sampled_count = 0;
                for(tjs_uint sy = 0; sy < height; sy += step_y) {
                    const auto *row = static_cast<const tjs_uint32 *>(
                        image->GetScanLineForRead(sy));
                    if(!row)
                        continue;
                    for(tjs_uint sx = 0; sx < width; sx += step_x) {
                        const tjs_uint32 pixel = row[sx];
                        const tjs_uint alpha = (pixel >> 24) & 0xff;
                        min_alpha = std::min(min_alpha, alpha);
                        max_alpha = std::max(max_alpha, alpha);
                        if(alpha == 0 && (pixel & 0x00ffffffu) != 0)
                            ++zero_alpha_nonzero_rgb;
                        ++sampled_count;
                    }
                }
                spdlog::info(
                    "LayerDrawGPU source layer={} face={} type={} size={}x{} "
                    "sampled={} minA={} maxA={} zeroA_nonzeroRGB={} "
                    "points=[0x{:08x},0x{:08x},0x{:08x}]",
                    name, static_cast<int>(layer->GetFace()),
                    ttstr(layer->GetTypeNameString()).AsStdString(), width,
                    height, sampled_count, min_alpha, max_alpha,
                    zero_alpha_nonzero_rgb, image->GetPoint(0, 0),
                    image->GetPoint(static_cast<tjs_int>(width / 2),
                                    static_cast<tjs_int>(height / 2)),
                    image->GetPoint(static_cast<tjs_int>(width - 1),
                                    static_cast<tjs_int>(height - 1)));

                if(const char *dump = std::getenv("AETHERKIRI_LAYER_SOURCE_PPM");
                   dump && *dump && *dump != '0' && name == dump) {
                    static std::atomic<bool> dumped{false};
                    if(!dumped.exchange(true, std::memory_order_relaxed)) {
                        std::string safeName = name;
                        for(char &c : safeName) {
                            if(c == '/' || c == '\\' || c == ':' || c == ' ')
                                c = '_';
                        }
                        std::ofstream out("/tmp/aetherkiri-layer-source-" +
                                              safeName + ".ppm",
                                           std::ios::binary);
                        if(out) {
                            out << "P6\n" << width << ' ' << height << "\n255\n";
                            for(tjs_uint y = 0; y < height; ++y) {
                                const auto *row = static_cast<const tjs_uint32 *>(
                                    image->GetScanLineForRead(y));
                                for(tjs_uint x = 0; x < width; ++x) {
                                    const tjs_uint32 pixel = row ? row[x] : 0;
                                    const float a = ((pixel >> 24) & 0xff) / 255.0f;
                                    const float bg = ((x / 64 + y / 64) & 1) ? 0.92f : 0.68f;
                                    const auto blend = [&](tjs_uint shift) {
                                        const float c = ((pixel >> shift) & 0xff) / 255.0f;
                                        return static_cast<unsigned char>(std::clamp(
                                            (c * a + bg * (1.0f - a)) * 255.0f,
                                            0.0f, 255.0f));
                                    };
                                    const unsigned char rgb[3] = {
                                        blend(0), blend(8), blend(16)};
                                    out.write(reinterpret_cast<const char *>(rgb), 3);
                                }
                            }
                            spdlog::info("dumped layer source PPM {}", name);
                        }
                    }
                }
            }
        }
    }

    if(const char *dump = std::getenv("AETHERKIRI_DUMP_STAND_LAYERS");
       dump && *dump && *dump != '0' && event &&
       std::strcmp(event, "begin") == 0 && layer->GetMainImage()) {
        const std::string name = layer->GetName().AsStdString();
        if(name.find("直太") != std::string::npos) {
            static std::unordered_set<std::string> dumped_names;
            if(dumped_names.insert(name).second) {
                std::string label = name;
                std::replace(label.begin(), label.end(), '/', '_');
                std::replace(label.begin(), label.end(), '\\', '_');
                try {
                    TVPSaveImage(ttstr{"/tmp/aetherkiri-layer-" + label + ".png"},
                                 TJS_W("png"), layer->GetMainImage(), nullptr);
                } catch(...) {
                    spdlog::warn("failed dumping stand layer {}", name);
                }
            }
        }
    }

    // Temporary diagnostic: sample the source texture at the point where the
    // GPU draw is entered.  DrawSelf is not always reached through the same
    // path (e.g. a child can be composed by a temporary target), so sampling
    // here gives us the actual texture handed to the compositor.
    if(const char *pixels = std::getenv("AETHERKIRI_MESSAGE_FRAME_PIXELS");
       pixels && *pixels && *pixels != '0' && event &&
       std::strcmp(event, "begin") == 0 &&
       layer->GetName() == TJS_W("colorframe") && layer->GetMainImage()) {
        static std::atomic<bool> sampled{false};
        if(!sampled.exchange(true, std::memory_order_relaxed)) {
            auto *image = layer->GetMainImage()->GetTexture();
            if(!image)
                return;
            const tjs_uint width = image->GetWidth();
            const tjs_uint height = image->GetHeight();
            const tjs_uint step_x = std::max<tjs_uint>(1, width / 64);
            const tjs_uint step_y = std::max<tjs_uint>(1, height / 64);
            tjs_uint min_alpha = 255;
            tjs_uint max_alpha = 0;
            tjs_uint nonzero = 0;
            for(tjs_uint sy = 0; sy < height; sy += step_y) {
                const auto *row = static_cast<const tjs_uint32 *>(
                    image->GetScanLineForRead(sy));
                if(!row)
                    continue;
                for(tjs_uint sx = 0; sx < width; sx += step_x) {
                    const tjs_uint alpha = (row[sx] >> 24) & 0xff;
                    min_alpha = std::min(min_alpha, alpha);
                    max_alpha = std::max(max_alpha, alpha);
                    if(alpha)
                        ++nonzero;
                }
            }
            spdlog::info("message-frame draw-source size={}x{} minA={} maxA={} "
                         "nonzero={} points=[0x{:08x},0x{:08x},0x{:08x}]",
                         width, height, min_alpha, max_alpha, nonzero,
                         image->GetPoint(0, 0),
                         image->GetPoint(static_cast<tjs_int>(width / 2),
                                         static_cast<tjs_int>(height / 2)),
                         image->GetPoint(static_cast<tjs_int>(width - 1),
                                         static_cast<tjs_int>(height - 1)));
        }
    }
}

static void TVPTraceLayerDrawGpuChild(tTJSNI_BaseLayer *parent,
                                      tTJSNI_BaseLayer *child,
                                      const tTVPRect &intersect,
                                      bool will_draw) {
    const bool parent_focus =
        parent && TVPLayerDrawTraceName(parent->GetName()) &&
        parent->GetName() != TJS_W("表-背景");
    const bool child_focus = child && TVPLayerDrawTraceName(child->GetName());
    if(!parent_focus && !child_focus)
        return;
    if(!TVPLayerDrawTraceArmIfNeeded(parent) &&
       !TVPLayerDrawTraceArmIfNeeded(child))
        return;
    if(!TVPLayerDrawTraceTake())
        return;
    spdlog::info(
        "LayerDrawGPU child parent={} child={} will_draw={} child_visible={} "
        "intersect=({},{} {}x{}) child_pos=({}, {}) child_size={}x{} "
        "child_image={} child_children={} child_visible_children={} child_order={}",
        parent ? parent->GetName().AsStdString() : "<null>",
        child ? child->GetName().AsStdString() : "<null>",
        will_draw ? "yes" : "no",
        child && child->GetVisible() ? "yes" : "no", intersect.left,
        intersect.top, intersect.get_width(), intersect.get_height(),
        child ? child->GetLeft() : 0, child ? child->GetTop() : 0,
        child ? child->GetWidth() : 0, child ? child->GetHeight() : 0,
        child && child->GetMainImage() ? "yes" : "no",
        child ? child->GetCount() : 0,
                 child ? child->DebugGetVisibleChildrenCount() : 0,
                 child ? child->GetOrderIndex() : 0);
}

// Stage/clip layers are assembled by KAGEnvImage through a short-lived,
// unnamed child layer.  Keep the lifecycle trace opt-in: it is useful for
// distinguishing a missing virtual image from a correctly generated image
// that is later hidden by the layer-update path, but is far too noisy for the
// normal renderer log.
static bool TVPStage2TraceEnabled() {
    static const bool enabled = [] {
        const char *value = std::getenv("AETHERKIRI_STAGE2_TRACE");
        return value && *value && *value != '0';
    }();
    return enabled;
}

static bool TVPStage2Related(tTJSNI_BaseLayer *layer) {
    if(!layer)
        return false;
    tTJSNI_BaseLayer *current = layer;
    for(int depth = 0; current && depth < 4; ++depth) {
        if(current->GetName() == TJS_W("stage2"))
            return true;
        current = current->GetParent();
    }
    return false;
}

static tjs_uint32 TVPStage2Sample(tTJSNI_BaseLayer *layer,
                                  tjs_int x, tjs_int y) {
    if(!layer || !layer->GetMainImage())
        return 0;
    auto *image = layer->GetMainImage()->GetTexture();
    if(!image || x < 0 || y < 0 || static_cast<tjs_uint>(x) >= image->GetWidth() ||
       static_cast<tjs_uint>(y) >= image->GetHeight())
        return 0;
    return image->GetPoint(x, y);
}

static void TVPTraceStage2Lifecycle(const char *event,
                                    tTJSNI_BaseLayer *layer,
                                    const std::string &detail = {}) {
    if(!TVPStage2TraceEnabled() || !layer || !TVPStage2Related(layer))
        return;
    const auto *parent = layer->GetParent();
    const auto *image = layer->GetMainImage();
    std::string owner_class;
    std::string owner_onpaint;
    if(auto *owner = layer->GetOwnerNoAddRef()) {
        tTJSVariant class_name;
        if(TJS_SUCCEEDED(owner->ClassInstanceInfo(TJS_CII_GET, 0,
                                                   &class_name))) {
            owner_class = ttstr(class_name).AsStdString();
        }
        tTJSVariant onpaint;
        const tjs_error onpaint_er = owner->PropGet(
            TJS_IGNOREPROP, TJS_W("onPaint"), nullptr, &onpaint, owner);
        owner_onpaint = fmt::format("er={} type={}", onpaint_er,
                                    static_cast<int>(onpaint.Type()));
    }
    spdlog::info(
        "Stage2.lifecycle event={} layer={} ptr={} parent={} visible={} "
        "nodeVisible={} callPaint={} opacity={} pos=({}, {}) size={}x{} "
        "image={} tex={} p00=0x{:08x} "
        "pcenter=0x{:08x} children={} ownerClass={} ownerOnPaint={} detail={}",
        event ? event : "<unknown>", layer->GetName().AsStdString(),
        static_cast<const void *>(layer),
        parent ? parent->GetName().AsStdString() : "<none>",
        layer->GetVisible() ? "yes" : "no",
        layer->GetNodeVisible() ? "yes" : "no",
        layer->GetCallOnPaint() ? "yes" : "no", layer->GetOpacity(),
        layer->GetLeft(), layer->GetTop(), layer->GetWidth(), layer->GetHeight(),
        image ? "yes" : "no",
        static_cast<const void *>(image ? image->GetTexture() : nullptr),
        TVPStage2Sample(layer, 0, 0),
        TVPStage2Sample(layer, image ? static_cast<tjs_int>(image->GetWidth() / 2) : 0,
                        image ? static_cast<tjs_int>(image->GetHeight() / 2) : 0),
        layer->GetCount(), owner_class, owner_onpaint,
        detail);
}

// Focused diagnostic for the settings-page preview/flash layers.  The game
// creates these layers through the generic UI pack loader, so a stale parent
// composite can look identical to a missing image.  Keep this opt-in and
// narrow; it is removed once the cache invalidation path is verified.
static bool TVPFlashTraceRelated(tTJSNI_BaseLayer *layer) {
    if(!layer)
        return false;
    const char *dialog_trace = std::getenv("AETHERKIRI_DIALOG_TRACE");
    const bool trace_dialog_layers =
        dialog_trace && *dialog_trace && *dialog_trace != '0';
    for(int depth = 0; layer && depth < 3; ++depth) {
        const std::string name = layer->GetName().AsStdString();
        if(name == "chflash" || name == "chframe" || name == "chview" ||
           name.find("メッセージレイヤ1") != std::string::npos ||
           (trace_dialog_layers &&
            (name == "PulldownPanelLayer" ||
             name.find("LanguageSelect") != std::string::npos)))
            return true;
        layer = layer->GetParent();
    }
    return false;
}

static bool TVPFlashTraceEnabled() {
    const char *value = std::getenv("AETHERKIRI_FLASH_TRACE");
    return value && *value && *value != '0';
}

static void TVPTraceFlashLifecycle(const char *event,
                                   tTJSNI_BaseLayer *layer,
                                   const std::string &detail = {}) {
    if(!TVPFlashTraceEnabled() || !TVPFlashTraceRelated(layer))
        return;
    const auto *parent = layer->GetParent();
    const auto *image = layer->GetMainImage();
    const tjs_uint32 p00 = TVPStage2Sample(layer, 0, 0);
    const tjs_uint32 pc = TVPStage2Sample(
        layer, image ? static_cast<tjs_int>(image->GetWidth() / 2) : 0,
        image ? static_cast<tjs_int>(image->GetHeight() / 2) : 0);
    // The language selector is authored near the lower-left of its 2560x1920
    // panel (the PBD puts `base` at y=1241).  The old pcenter sample therefore
    // read an intentionally transparent cell and made a correctly composed
    // panel look empty.  Keep these points diagnostic-only and opt-in.
    const tjs_uint32 p_panel_top = TVPStage2Sample(layer, 14, 1175);
    const tjs_uint32 p_panel_mid = TVPStage2Sample(layer, 200, 1200);
    const tjs_uint32 p_panel_base = TVPStage2Sample(layer, 14, 1241);
    const tjs_uint32 p_panel_item = TVPStage2Sample(layer, 200, 1270);
    const tjs_uint32 p_panel_right = TVPStage2Sample(layer, 400, 1300);
    spdlog::info(
        "Flash.lifecycle event={} layer={} ptr={} parent={} visible={} "
        "owner={} "
        "nodeVisible={} opacity={} pos=({}, {}) size={}x{} image={} "
        "p00=0x{:08x} pcenter=0x{:08x} panel=[0x{:08x},0x{:08x},0x{:08x},0x{:08x},0x{:08x}] "
        "children={} visible_children={} detail={}",
        event ? event : "<unknown>", layer->GetName().AsStdString(),
        static_cast<const void *>(layer),
        parent ? parent->GetName().AsStdString() : "<none>",
        layer->GetVisible() ? "yes" : "no",
        static_cast<const void *>(layer->GetOwnerNoAddRef()),
        layer->GetNodeVisible() ? "yes" : "no",
        layer->GetOpacity(), layer->GetLeft(), layer->GetTop(), layer->GetWidth(),
        layer->GetHeight(), image ? "yes" : "no", p00, pc, p_panel_top,
        p_panel_mid, p_panel_base, p_panel_item, p_panel_right,
        layer->GetCount(), layer->DebugGetVisibleChildrenCount(), detail);
}

static bool TVPDialogLayerTraceEnabled(tTJSNI_BaseLayer *layer) {
    if(!layer)
        return false;
    const char *trace = std::getenv("AETHERKIRI_DIALOG_TRACE");
    if(!trace || !*trace || *trace == '0')
        return false;
    for(int depth = 0; layer && depth < 4; ++depth) {
        const std::string name = layer->GetName().AsStdString();
        if(name == "PulldownPanelLayer" ||
           name.find("LanguageSelect") != std::string::npos)
            return true;
        layer = layer->GetParent();
    }
    return false;
}

static bool TVPDialogLayerTraceTake() {
    static std::atomic<int> count{0};
    return count.fetch_add(1, std::memory_order_relaxed) < 512;
}

static void TVPTraceLayerInputEvent(const char *event,
                                    tTJSNI_BaseLayer *layer,
                                    const tTJSVariantClosure &action_owner) {
    if(!TVPLayerInputTraceEnabled() || !layer)
        return;
    AETHER_LAYER_INPUT_TRACE_LOG("LayerIntf %s layer=%s action=%d", event,
                                 layer->GetName().AsStdString().c_str(),
                                 action_owner.Object ? 1 : 0);
    spdlog::info("LayerIntf {} layer={} action={}", event,
                 layer->GetName().AsStdString(),
                 action_owner.Object ? "yes" : "no");
}

static std::string TVPVariantDebugString(const tTJSVariant &value) {
    ttstr text(value);
    return text.AsStdString();
}

static void TVPTraceKagExpressionValue(const char *label,
                                       const tjs_char *expression) {
    if(!TVPLayerInputTraceEnabled())
        return;
    try {
        tTJSVariant value;
        TVPExecuteExpression(ttstr(expression), &value);
        spdlog::info("LayerIntf kag {}={}", label,
                     TVPVariantDebugString(value));
    } catch(const eTJSScriptError &e) {
        spdlog::info("LayerIntf kag {} script-error message={} block={} line={}",
                     label, e.GetMessage().AsStdString(),
                     e.GetBlockName() ? ttstr(e.GetBlockName()).AsStdString()
                                      : "",
                     e.GetSourceLine());
    } catch(const eTJS &e) {
        spdlog::info("LayerIntf kag {} tjs-error message={}", label,
                     e.GetMessage().AsStdString());
    } catch(...) {
        spdlog::info("LayerIntf kag {} failed", label);
    }
}

static void TVPTraceKagState(const char *prefix) {
    if(!TVPLayerInputTraceEnabled())
        return;
    spdlog::info("LayerIntf kag-state {}", prefix ? prefix : "");
    TVPTraceKagExpressionValue(
        "typeof_kag", TJS_W("typeof kag"));
    TVPTraceKagExpressionValue(
        "currentStorage",
        TJS_W("(typeof kag == \"Object\" && kag) ? kag.currentStorage : \"\""));
    TVPTraceKagExpressionValue(
        "currentLabel",
        TJS_W("(typeof kag == \"Object\" && kag) ? kag.currentLabel : \"\""));
    TVPTraceKagExpressionValue(
        "inStable",
        TJS_W("(typeof kag == \"Object\" && kag) ? kag.inStable : \"\""));
    TVPTraceKagExpressionValue(
        "enabled",
        TJS_W("(typeof kag == \"Object\" && kag) ? kag.enabled : \"\""));
    TVPTraceKagExpressionValue(
        "currentState",
        TJS_W("(typeof SystemHook == \"Object\" && SystemHook) ? "
              "SystemHook.currentState : \"\""));
    TVPTraceKagExpressionValue(
        "playerWorking",
        TJS_W("(typeof SystemAction == \"Object\" && SystemAction) ? "
              "SystemAction.playerWorking : \"\""));
    TVPTraceKagExpressionValue(
        "current",
        TJS_W("(typeof kag == \"Object\" && kag) ? kag.current : \"\""));
    TVPTraceKagExpressionValue(
        "usingExtraConductor",
        TJS_W("(typeof kag == \"Object\" && kag) ? "
              "kag.usingExtraConductor : \"\""));
    TVPTraceKagExpressionValue(
        "conductor",
        TJS_W("(typeof kag == \"Object\" && kag) ? kag.conductor : \"\""));
    TVPTraceKagExpressionValue(
        "conductor.status",
        TJS_W("(typeof kag == \"Object\" && kag) ? "
              "kag.conductor.status : \"\""));
    TVPTraceKagExpressionValue(
        "conductor.mRun",
        TJS_W("(typeof kag == \"Object\" && kag) ? "
              "kag.conductor.mRun : \"\""));
    TVPTraceKagExpressionValue(
        "conductor.mWait",
        TJS_W("(typeof kag == \"Object\" && kag) ? "
              "kag.conductor.mWait : \"\""));
    TVPTraceKagExpressionValue(
        "conductor.mStop",
        TJS_W("(typeof kag == \"Object\" && kag) ? "
              "kag.conductor.mStop : \"\""));
    TVPTraceKagExpressionValue(
        "conductor.lastTagName",
        TJS_W("(typeof kag == \"Object\" && kag) ? "
              "kag.conductor.lastTagName : \"\""));
    TVPTraceKagExpressionValue(
        "conductor.curStorage",
        TJS_W("(typeof kag == \"Object\" && kag) ? "
              "kag.conductor.curStorage : \"\""));
    TVPTraceKagExpressionValue(
        "conductor.curLabel",
        TJS_W("(typeof kag == \"Object\" && kag) ? "
              "kag.conductor.curLabel : \"\""));
    TVPTraceKagExpressionValue(
        "conductor.runLabel",
        TJS_W("(typeof kag == \"Object\" && kag) ? "
              "kag.conductor.runLabel : \"\""));
    TVPTraceKagExpressionValue(
        "conductor.waitUntil",
        TJS_W("(typeof kag == \"Object\" && kag) ? "
              "kag.conductor.waitUntil : \"\""));
    TVPTraceKagExpressionValue(
        "conductor.waitKeys",
        TJS_W("(typeof kag == \"Object\" && kag && "
              "typeof Scripts == \"Object\") ? "
              "Scripts.getObjectKeys(kag.conductor.waitUntil).join(\",\") : \"\""));
    TVPTraceKagExpressionValue(
        "conductor.waitAllKeys",
        TJS_W("(typeof kag == \"Object\" && kag && "
              "typeof Scripts == \"Object\") ? "
              "Scripts.getObjectKeys(kag.conductor.waitAll).join(\",\") : \"\""));
    TVPTraceKagExpressionValue(
        "conductor.pendingCount",
        TJS_W("(typeof kag == \"Object\" && kag) ? "
              "kag.conductor.pendings.count : \"\""));
    TVPTraceKagExpressionValue(
        "conductor.pending0",
        TJS_W("(typeof kag == \"Object\" && kag && "
              "kag.conductor.pendings.count > 0) ? "
              "(kag.conductor.pendings[0].tagname + \":\" + "
              "((void !== kag.conductor.pendings[0].name) ? "
              "kag.conductor.pendings[0].name : \"\")) : \"\""));
    TVPTraceKagExpressionValue(
        "conductor.pending0.taglist",
        TJS_W("(typeof kag == \"Object\" && kag && "
              "kag.conductor.pendings.count > 0 && "
              "void !== kag.conductor.pendings[0].taglist) ? "
              "kag.conductor.pendings[0].taglist.join(\",\") : \"\""));
    TVPTraceKagExpressionValue(
        "conductor.pending0.keys",
        TJS_W("(typeof kag == \"Object\" && kag && "
              "typeof Scripts == \"Object\" && "
              "kag.conductor.pendings.count > 0) ? "
              "Scripts.getObjectKeys(kag.conductor.pendings[0]).join(\",\") : \"\""));
    TVPTraceKagExpressionValue(
        "conductor.pending0.hasTrans",
        TJS_W("(typeof kag == \"Object\" && kag && "
              "kag.conductor.pendings.count > 0) ? "
              "(void !== kag.conductor.pendings[0].trans) : \"\""));
    TVPTraceKagExpressionValue(
        "conductor.pending0.transKeys",
        TJS_W("(typeof kag == \"Object\" && kag && "
              "typeof Scripts == \"Object\" && "
              "kag.conductor.pendings.count > 0 && "
              "void !== kag.conductor.pendings[0].trans) ? "
              "Scripts.getObjectKeys(kag.conductor.pendings[0].trans).join(\",\") : \"\""));
    TVPTraceKagExpressionValue(
        "conductor.pending0.hasUpdate",
        TJS_W("(typeof kag == \"Object\" && kag && "
              "kag.conductor.pendings.count > 0) ? "
              "(void !== kag.conductor.pendings[0].update) : \"\""));
    TVPTraceKagExpressionValue(
        "conductor.pending0.updateKeys",
        TJS_W("(typeof kag == \"Object\" && kag && "
              "typeof Scripts == \"Object\" && "
              "kag.conductor.pendings.count > 0 && "
              "void !== kag.conductor.pendings[0].update) ? "
              "Scripts.getObjectKeys(kag.conductor.pendings[0].update).join(\",\") : \"\""));
    TVPTraceKagExpressionValue(
        "conductor.pending0.updateCount",
        TJS_W("(typeof kag == \"Object\" && kag && "
              "kag.conductor.pendings.count > 0 && "
              "void !== kag.conductor.pendings[0].update && "
              "void !== kag.conductor.pendings[0].update.count) ? "
              "kag.conductor.pendings[0].update.count : \"\""));
    TVPTraceKagExpressionValue(
        "conductor.pending1",
        TJS_W("(typeof kag == \"Object\" && kag && "
              "kag.conductor.pendings.count > 1) ? "
              "(kag.conductor.pendings[1].tagname + \":\" + "
              "((void !== kag.conductor.pendings[1].name) ? "
              "kag.conductor.pendings[1].name : \"\")) : \"\""));
    TVPTraceKagExpressionValue(
        "conductor.pending1.taglist",
        TJS_W("(typeof kag == \"Object\" && kag && "
              "kag.conductor.pendings.count > 1 && "
              "void !== kag.conductor.pendings[1].taglist) ? "
              "kag.conductor.pendings[1].taglist.join(\",\") : \"\""));
    TVPTraceKagExpressionValue(
        "conductor.fastCount",
        TJS_W("(typeof kag == \"Object\" && kag) ? "
              "kag.conductor.fasttags.count : \"\""));
    TVPTraceKagExpressionValue(
        "conductor.nextCount",
        TJS_W("(typeof kag == \"Object\" && kag) ? "
              "kag.conductor.nexttags.count : \"\""));
    TVPTraceKagExpressionValue(
        "conductor.next0",
        TJS_W("(typeof kag == \"Object\" && kag && "
              "kag.conductor.nexttags.count > 0) ? "
              "kag.conductor.nexttags[0].tagname : \"\""));
    TVPTraceKagExpressionValue(
        "main.status",
        TJS_W("(typeof kag == \"Object\" && kag) ? "
              "kag.mainConductor.status : \"\""));
    TVPTraceKagExpressionValue(
        "main.nextCount",
        TJS_W("(typeof kag == \"Object\" && kag) ? "
              "kag.mainConductor.nexttags.count : \"\""));
    TVPTraceKagExpressionValue(
        "main.waitKeys",
        TJS_W("(typeof kag == \"Object\" && kag && "
              "typeof Scripts == \"Object\") ? "
              "Scripts.getObjectKeys(kag.mainConductor.waitUntil).join(\",\") : \"\""));
    TVPTraceKagExpressionValue(
        "extra.status",
        TJS_W("(typeof kag == \"Object\" && kag) ? "
              "kag.extraConductor.status : \"\""));
    TVPTraceKagExpressionValue(
        "extra.nextCount",
        TJS_W("(typeof kag == \"Object\" && kag) ? "
              "kag.extraConductor.nexttags.count : \"\""));
    TVPTraceKagExpressionValue(
        "extra.waitKeys",
        TJS_W("(typeof kag == \"Object\" && kag && "
              "typeof Scripts == \"Object\") ? "
              "Scripts.getObjectKeys(kag.extraConductor.waitUntil).join(\",\") : \"\""));
    TVPTraceKagExpressionValue(
        "entryTags.count",
        TJS_W("(typeof kag == \"Object\" && kag && void !== kag.entryTags) ? "
              "kag.entryTags.count : \"\""));
    TVPTraceKagExpressionValue(
        "transMode",
        TJS_W("(typeof kag == \"Object\" && kag && void !== kag.transMode) ? "
              "kag.transMode : \"\""));
    TVPTraceKagExpressionValue(
        "transTarget",
        TJS_W("(typeof kag == \"Object\" && kag && void !== kag.transTarget) ? "
              "kag.transTarget : \"\""));
    TVPTraceKagExpressionValue(
        "waitInfo",
        TJS_W("(typeof kag == \"Object\" && kag && void !== kag.waitInfo) ? "
              "kag.waitInfo : \"\""));
}

class TVPTraceObjectEnumCaller : public tTJSDispatch {
public:
    TVPTraceObjectEnumCaller(std::string label, int limit)
        : Label(std::move(label)), Limit(limit) {}

    tjs_error FuncCall(tjs_uint32, const tjs_char *, tjs_uint32 *,
                       tTJSVariant *result, tjs_int numparams,
                       tTJSVariant **param, iTJSDispatch2 *) override {
        if(Count >= Limit) {
            if(result) *result = false;
            return TJS_S_OK;
        }
        if(numparams >= 2) {
            const tTVInteger member_flags = param[1]->AsInteger();
            if(!(member_flags & TJS_HIDDENMEMBER)) {
                const std::string name = TVPVariantDebugString(*param[0]);
                const std::string value =
                    numparams >= 3 ? TVPVariantDebugString(*param[2])
                                   : std::string("<no-value>");
                spdlog::info("LayerIntf trace {}.{}={}", Label, name, value);
                Count++;
            }
        }
        if(result) *result = true;
        return TJS_S_OK;
    }

private:
    std::string Label;
    int Limit;
    int Count = 0;
};

static void TVPTraceObjectMembers(const char *label,
                                  const tTJSVariantClosure &object,
                                  int limit) {
    if(!TVPLayerInputTraceEnabled() || !object.Object)
        return;
    TVPTraceObjectEnumCaller *caller =
        new TVPTraceObjectEnumCaller(label, limit);
    tTJSVariantClosure closure(caller);
    object.EnumMembers(TJS_IGNOREPROP, &closure, nullptr);
    caller->Release();
}

static void TVPTraceObjectProperty(const char *label,
                                   const tTJSVariantClosure &object,
                                   const tjs_char *property) {
    if(!TVPLayerInputTraceEnabled() || !object.Object)
        return;
    tTJSVariant value;
    ttstr prop_name(property);
    const tjs_error hr =
        object.PropGet(0, prop_name.c_str(), prop_name.GetHint(), &value,
                       nullptr);
    if(TJS_FAILED(hr) || value.Type() == tvtVoid) {
        spdlog::info("LayerIntf trace {}.{}=<missing> hr={}", label,
                     prop_name.AsStdString(), hr);
        return;
    }
    spdlog::info("LayerIntf trace {}.{}={}", label, prop_name.AsStdString(),
                 TVPVariantDebugString(value));
    if(value.Type() == tvtObject &&
        (prop_name == TJS_W("action") || prop_name == TJS_W("onClick") ||
        prop_name == TJS_W("current") ||
        prop_name == TJS_W("names") ||
        prop_name == TJS_W("controlOwner") ||
        prop_name == TJS_W("button") ||
        prop_name == TJS_W("_button") ||
        prop_name == TJS_W("buttonLayer"))) {
        tTJSVariantClosure nested = value.AsObjectClosureNoAddRef();
        if(nested.Object) {
            const std::string nested_label =
                std::string(label) + "." + prop_name.AsStdString();
            TVPTraceObjectMembers(nested_label.c_str(), nested, 80);
        }
    }
}

static void TVPTraceObjectForButtonClick(const char *label,
                                         const tTJSVariantClosure &object) {
    if(!TVPLayerInputTraceEnabled() || !object.Object)
        return;
    TVPTraceObjectProperty(label, object, TJS_W("action"));
    TVPTraceObjectProperty(label, object, TJS_W("name"));
    TVPTraceObjectProperty(label, object, TJS_W("linkNum"));
    TVPTraceObjectProperty(label, object, TJS_W("controlOwner"));
    TVPTraceObjectProperty(label, object, TJS_W("owner"));
    TVPTraceObjectProperty(label, object, TJS_W("parent"));
    TVPTraceObjectProperty(label, object, TJS_W("button"));
    TVPTraceObjectProperty(label, object, TJS_W("_button"));
    TVPTraceObjectProperty(label, object, TJS_W("buttonLayer"));
    TVPTraceObjectProperty(label, object, TJS_W("target"));
    TVPTraceObjectProperty(label, object, TJS_W("eventTarget"));
    TVPTraceObjectProperty(label, object, TJS_W("onclick"));
    TVPTraceObjectProperty(label, object, TJS_W("onenter"));
    TVPTraceObjectProperty(label, object, TJS_W("onleave"));
    TVPTraceObjectProperty(label, object, TJS_W("stor"));
    TVPTraceObjectProperty(label, object, TJS_W("_stor"));
    TVPTraceObjectProperty(label, object, TJS_W("store"));
    TVPTraceObjectProperty(label, object, TJS_W("_store"));
    TVPTraceObjectProperty(label, object, TJS_W("saveA"));
    TVPTraceObjectProperty(label, object, TJS_W("gameSaveM"));
    TVPTraceObjectProperty(label, object, TJS_W("current"));
    TVPTraceObjectProperty(label, object, TJS_W("Current"));
    TVPTraceObjectProperty(label, object, TJS_W("_current"));
    TVPTraceObjectProperty(label, object, TJS_W("onClick"));
    TVPTraceObjectProperty(label, object, TJS_W("onExecute"));
    TVPTraceObjectProperty(label, object, TJS_W("onButton"));
    TVPTraceObjectProperty(label, object, TJS_W("onButtonClick"));
    TVPTraceObjectProperty(label, object, TJS_W("click"));
    TVPTraceObjectProperty(label, object, TJS_W("execute"));
    TVPTraceObjectProperty(label, object, TJS_W("exp"));
    TVPTraceObjectProperty(label, object, TJS_W("expression"));
    TVPTraceObjectProperty(label, object, TJS_W("cmd"));
    TVPTraceObjectProperty(label, object, TJS_W("command"));
    TVPTraceObjectProperty(label, object, TJS_W("func"));
    TVPTraceObjectProperty(label, object, TJS_W("sename"));
    TVPTraceObjectProperty(label, object, TJS_W("names"));
    TVPTraceObjectProperty(label, object, TJS_W("array"));
    TVPTraceObjectProperty(label, object, TJS_W("buf"));
    TVPTraceObjectProperty(label, object, TJS_W("storage"));
    TVPTraceObjectProperty(label, object, TJS_W("voice"));
    TVPTraceObjectProperty(label, object, TJS_W("profile"));
    TVPTraceObjectProperty(label, object, TJS_W("dress"));
    TVPTraceObjectProperty(label, object, TJS_W("chara"));
    TVPTraceObjectProperty(label, object, TJS_W("scene"));
    TVPTraceObjectProperty(label, object, TJS_W("call"));
    TVPTraceObjectProperty(label, object, TJS_W("global"));
    TVPTraceObjectProperty(label, object, TJS_W("_up"));
    TVPTraceObjectProperty(label, object, TJS_W("_down"));
    TVPTraceObjectProperty(label, object, TJS_W("_click"));
    TVPTraceObjectProperty(label, object, TJS_W("params"));
    TVPTraceObjectProperty(label, object, TJS_W("_lastSelect"));
    TVPTraceObjectProperty(label, object, TJS_W("lastSelect"));
    TVPTraceObjectProperty(label, object, TJS_W("_issave"));
    TVPTraceObjectProperty(label, object, TJS_W("_useOldSelect"));
    TVPTraceObjectProperty(label, object, TJS_W("_loadNumber"));
    TVPTraceObjectProperty(label, object, TJS_W("_sysbtnTags"));
    TVPTraceObjectProperty(label, object, TJS_W("_sysbtnInfo"));

}

static void TVPTraceLayerActionOwner(const char *event,
                                     tTJSNI_BaseLayer *layer,
                                     const tTJSVariantClosure &object) {
    if(!TVPLayerInputTraceEnabled() || !layer || !object.Object)
        return;
    std::string label = std::string("layer.") + event + ".actionOwner";
    TVPTraceObjectForButtonClick(label.c_str(), object);

}

static void TVPTraceLayerActionResult(const char *event,
                                      tTJSNI_BaseLayer *layer, tjs_error hr,
                                      tTJSVariant *result) {
    if(!TVPLayerInputTraceEnabled() || !layer)
        return;
    std::string result_text =
        TJS_SUCCEEDED(hr) && result ? TVPVariantDebugString(*result)
                                    : std::string("<failed>");
    spdlog::info("LayerIntf {} action returned layer={} hr={} result={}",
                 event, layer->GetName().AsStdString(), hr, result_text);
}

static thread_local tTJSNI_BaseLayer *TVPLayerEventSource = nullptr;
static thread_local tTJSNI_BaseLayer *TVPLayerRecentEventSource = nullptr;
static thread_local bool TVPCafeStellaSyntheticClickActive = false;
static thread_local tjs_int TVPLayerLastSaveLoadItemIndex = 0;

struct tTVPLayerMouseUpContext {
    bool Active = false;
    tTJSNI_BaseLayer *Layer = nullptr;
    tjs_int X = 0;
    tjs_int Y = 0;
    tjs_int Button = 0;
    tjs_int64 Shift = 0;
};

static thread_local tTVPLayerMouseUpContext TVPLayerCurrentMouseUp;

void TVPResetLayerStateForHostSession() {
    {
        std::lock_guard<std::mutex> lock(TVPExchangedKagPageMutex);
        TVPExchangedHiddenKagPages.clear();
        TVPKagPageLastObservedVisibility.clear();
        TVPHiddenKagAssignmentStreaks.clear();
        TVPMotionSwapAssignmentTargets.clear();
    }
    TVPFullGpuCompletionRequested.store(false, std::memory_order_release);
    TVPLayerDrawTraceArmedFlag.store(false, std::memory_order_release);
    TVPLayerEventSource = nullptr;
    TVPLayerRecentEventSource = nullptr;
    TVPCafeStellaSyntheticClickActive = false;
    TVPLayerLastSaveLoadItemIndex = 0;
    TVPLayerCurrentMouseUp = {};
}

class TVPLayerEventSourceScope {
public:
    explicit TVPLayerEventSourceScope(tTJSNI_BaseLayer *layer)
        : Previous(TVPLayerEventSource) {
        TVPLayerEventSource = layer;
    }

    ~TVPLayerEventSourceScope() { TVPLayerEventSource = Previous; }

private:
    tTJSNI_BaseLayer *Previous;
};

class TVPLayerMouseUpContextScope {
public:
    TVPLayerMouseUpContextScope(tTJSNI_BaseLayer *layer, tjs_int numparams,
                                tTJSVariant **param)
        : Previous(TVPLayerCurrentMouseUp) {
        TVPLayerCurrentMouseUp = {};
        if(layer && numparams >= 4) {
            TVPLayerCurrentMouseUp.Active = true;
            TVPLayerCurrentMouseUp.Layer = layer;
            TVPLayerCurrentMouseUp.X = (tjs_int)*param[0];
            TVPLayerCurrentMouseUp.Y = (tjs_int)*param[1];
            TVPLayerCurrentMouseUp.Button = (tjs_int)*param[2];
            TVPLayerCurrentMouseUp.Shift = (tjs_int64)*param[3];
        }
    }

    ~TVPLayerMouseUpContextScope() { TVPLayerCurrentMouseUp = Previous; }

private:
    tTVPLayerMouseUpContext Previous;
};

static bool TVPLayerNameEquals(tTJSNI_BaseLayer *layer, const char *name) {
    return layer && layer->GetName().AsStdString() == name;
}

static bool TVPParseCafeStellaItemName(const ttstr &name, tjs_int &column,
                                       tjs_int &row, bool &has_row) {
    const std::string text = name.AsStdString();
    if(text.size() <= 4 || text.compare(0, 4, "item") != 0)
        return false;

    tjs_int first = 0;
    size_t i = 4;
    // Save/load grid builders use both item_${x}_${y} and item${x}_${y}.
    // This parser is limited to the separate save/load compatibility path;
    // normal button dispatch must use the callback data bound by the script.
    if(i < text.size() && text[i] == '_')
        ++i;
    if(i >= text.size() || text[i] < '0' || text[i] > '9')
        return false;
    for(; i < text.size() && text[i] >= '0' && text[i] <= '9'; ++i)
        first = first * 10 + (text[i] - '0');

    column = first;
    row = 0;
    has_row = false;
    if(i == text.size())
        return true;

    if(text[i] != '_')
        return false;
    ++i;
    if(i >= text.size() || text[i] < '0' || text[i] > '9')
        return false;

    tjs_int second = 0;
    for(; i < text.size(); ++i) {
        if(text[i] < '0' || text[i] > '9')
            return false;
        second = second * 10 + (text[i] - '0');
    }
    row = second;
    has_row = true;
    return true;
}

static bool TVPLayerNameLooksNumberedItem(const ttstr &name) {
    tjs_int column = 0;
    tjs_int row = 0;
    bool has_row = false;
    return TVPParseCafeStellaItemName(name, column, row, has_row);
}

static bool TVPGetNumberedItemIndex(tTJSNI_BaseLayer *layer, tjs_int &index) {
    if(!layer)
        return false;
    tjs_int column = 0;
    tjs_int row = 0;
    bool has_row = false;
    if(!TVPParseCafeStellaItemName(layer->GetName(), column, row, has_row))
        return false;
    if(!has_row) {
        index = column;
        return true;
    }

    tjs_int columns = 1;
    if(tTJSNI_BaseLayer *parent = layer->GetParent()) {
        const tjs_int item_width = static_cast<tjs_int>(layer->GetWidth());
        const tjs_int parent_width = static_cast<tjs_int>(parent->GetWidth());
        if(item_width > 0 && parent_width > 0) {
            columns = (parent_width + (item_width / 2)) / item_width;
            if(columns < 1)
                columns = 1;
        }
    }
    index = row * columns + column;
    return true;
}

static bool TVPLayerLooksCafeStellaSaveLoadGridItem(tTJSNI_BaseLayer *layer) {
    if(!layer || !TVPLayerNameLooksNumberedItem(layer->GetName()))
        return false;
    if(!TVPLayerNameEquals(layer->GetParent(), "ParentHackLayer"))
        return false;

    const tjs_uint width = layer->GetWidth();
    const tjs_uint height = layer->GetHeight();
    return width >= 220 && height >= 120;
}

static bool TVPGetCafeStellaSaveLoadGridItemIndex(tTJSNI_BaseLayer *layer,
                                                  tjs_int &item_index) {
    if(!TVPLayerNameEquals(layer, "ParentHackLayer") ||
       !TVPLayerLooksCafeStellaSaveLoadGridItem(TVPLayerEventSource)) {
        return false;
    }
    return TVPGetNumberedItemIndex(TVPLayerEventSource, item_index);
}

static tjs_error TVPCallCafeStellaGridItemMethod(
    const char *label, const tTJSVariantClosure &target,
    tTJSNI_BaseLayer *parent_layer, tTJSNI_BaseLayer *source_layer,
    tjs_int item_index, const tjs_char *method, tTJSVariant *result,
    tjs_int numparams, tTJSVariant **param) {
    if(!target.Object)
        return TJS_E_INVALIDOBJECT;

    ttstr method_name(method);
    const tjs_error hr = target.FuncCall(
        0, method_name.c_str(), method_name.GetHint(), result, numparams,
        param, nullptr);
    if(TVPLayerInputTraceEnabled()) {
        spdlog::info("LayerIntf onButtonClick item direct label={} parent={} source={} index={} method={} argc={} target={} this={} hr={}",
                     label ? label : "",
                     parent_layer ? parent_layer->GetName().AsStdString() : "",
                     source_layer ? source_layer->GetName().AsStdString() : "",
                     item_index, method_name.AsStdString(), numparams,
                     static_cast<const void *>(target.Object),
                     static_cast<const void *>(target.ObjThis), hr);
    }
    return hr;
}

static bool TVPInvokeCafeStellaGridItemDefault(
    const char *label, const tTJSVariantClosure &target,
    tTJSNI_BaseLayer *parent_layer, tTJSNI_BaseLayer *source_layer,
    tjs_int item_index, tTJSVariant *result) {
    tTJSVariant item_arg(item_index);
    tTJSVariant *item_args[1] = { &item_arg };

    const tjs_error set_item_hr = TVPCallCafeStellaGridItemMethod(
        label, target, parent_layer, source_layer, item_index,
        TJS_W("setItem"), nullptr, 1, item_args);
    if(TJS_FAILED(set_item_hr))
        return false;

    if(TVPLayerInputTraceEnabled()) {
        tTJSVariant current;
        TVPCallCafeStellaGridItemMethod(label, target, parent_layer,
                                        source_layer, item_index,
                                        TJS_W("getCurrent"), &current, 0,
                                        nullptr);
        spdlog::info("LayerIntf onButtonClick item direct label={} current={}",
                     label ? label : "", TVPVariantDebugString(current));
    }

    tjs_error default_hr = TVPCallCafeStellaGridItemMethod(
        label, target, parent_layer, source_layer, item_index,
        TJS_W("onDefault"), result, 0, nullptr);
    if(TJS_SUCCEEDED(default_hr))
        return true;

    default_hr = TVPCallCafeStellaGridItemMethod(
        label, target, parent_layer, source_layer, item_index,
        TJS_W("onDefault"), result, 1, item_args);
    return TJS_SUCCEEDED(default_hr);
}

static bool TVPInvokeCafeStellaGridItemCandidate(
    const char *label, const tTJSVariantClosure &target,
    tTJSNI_BaseLayer *parent_layer, tTJSNI_BaseLayer *source_layer,
    tjs_int item_index, tTJSVariant *result) {
    if(!target.Object)
        return false;

    if(TVPInvokeCafeStellaGridItemDefault(label, target, parent_layer,
                                          source_layer, item_index, result))
        return true;

    tTJSVariant item_arg(item_index);
    tTJSVariant *item_args[1] = { &item_arg };
    static const tjs_char *candidate_methods[] = {
        TJS_W("onItem"),
        TJS_W("select"),
        TJS_W("selec"),
        TJS_W("dsa"),
        TJS_W("_dsa"),
        TJS_W("onExecute"),
    };
    for(const tjs_char *candidate_method : candidate_methods) {
        const tjs_error hr = TVPCallCafeStellaGridItemMethod(
            label, target, parent_layer, source_layer, item_index,
            candidate_method, result, 1, item_args);
        if(TJS_SUCCEEDED(hr))
            return true;
    }
    return false;
}

static bool TVPInvokeCafeStellaGridItemCallbackOwner(
    const char *property_label, const tTJSVariantClosure &source_object,
    tTJSNI_BaseLayer *parent_layer, tTJSNI_BaseLayer *source_layer,
    tjs_int item_index, tTJSVariant *result) {
    if(!source_object.Object)
        return false;

    ttstr property_name(property_label);
    tTJSVariant callback_value;
    const tjs_error hr = source_object.PropGet(
        0, property_name.c_str(), property_name.GetHint(), &callback_value,
        nullptr);
    if(TJS_FAILED(hr) || callback_value.Type() != tvtObject) {
        if(TVPLayerInputTraceEnabled()) {
            spdlog::info("LayerIntf onButtonClick item direct callback property={} parent={} source={} index={} hr={} type={}",
                         property_label ? property_label : "",
                         parent_layer ? parent_layer->GetName().AsStdString() : "",
                         source_layer ? source_layer->GetName().AsStdString() : "",
                         item_index, hr, static_cast<int>(callback_value.Type()));
        }
        return false;
    }

    tTJSVariantClosure callback =
        callback_value.AsObjectClosureNoAddRef();
    if(TVPLayerInputTraceEnabled()) {
        spdlog::info("LayerIntf onButtonClick item direct callback property={} object={} this={}",
                     property_label ? property_label : "",
                     static_cast<const void *>(callback.Object),
                     static_cast<const void *>(callback.ObjThis));
    }
    if(!callback.ObjThis)
        return false;

    tTJSVariantClosure callback_owner(callback.ObjThis, callback.ObjThis);
    return TVPInvokeCafeStellaGridItemCandidate(
        property_label, callback_owner, parent_layer, source_layer, item_index,
        result);
}

static bool TVPGetCafeStellaCurrentClosure(tTJSNI_BaseLayer *source_layer,
                                           tTJSVariant &current_value);

static bool TVPExecuteCafeStellaCurrentCommand(
    const ttstr &command, tTJSNI_BaseLayer *parent_layer,
    tTJSNI_BaseLayer *source_layer, tjs_int item_index, tTJSVariant *result) {
    iTJSDispatch2 *source_owner =
        source_layer ? source_layer->GetOwnerNoAddRef() : nullptr;
    if(!source_owner)
        return false;

    const ttstr expression =
        ttstr(TJS_W("Current.cmd(\"")) + command + TJS_W("\")");
    auto trace_success = [&](const char *path, const tjs_error hr,
                             const tTJSVariant *value) {
        if(!TVPLayerInputTraceEnabled())
            return;
        spdlog::info("LayerIntf onButtonClick item direct command parent={} source={} index={} path={} expr={} result={} hr={}",
                     parent_layer ? parent_layer->GetName().AsStdString() : "",
                     source_layer ? source_layer->GetName().AsStdString() : "",
                     item_index, path ? path : "", expression.AsStdString(),
                     value ? TVPVariantDebugString(*value) : "", hr);
    };
    auto trace_exception = [&](const char *kind, const ttstr &message,
                               const tjs_char *block, tjs_int line,
                               const ttstr &trace) {
        if(!TVPLayerInputTraceEnabled())
            return;
        spdlog::info("LayerIntf onButtonClick item direct command parent={} source={} index={} expr={} threw kind={} message={} block={} line={} trace={}",
                     parent_layer ? parent_layer->GetName().AsStdString() : "",
                     source_layer ? source_layer->GetName().AsStdString() : "",
                     item_index, expression.AsStdString(), kind ? kind : "",
                     message.AsStdString(),
                     block ? ttstr(block).AsStdString() : "", line,
                     trace.AsStdString());
    };

    tTJSVariant current_value;
    if(TVPGetCafeStellaCurrentClosure(source_layer, current_value)) {
        tTJSVariantClosure current = current_value.AsObjectClosureNoAddRef();
        static ttstr cmd_name(TJS_W("cmd"));
        tTJSVariant command_arg(command);
        tTJSVariant *args[1] = { &command_arg };
        try {
            const tjs_error hr =
                current.FuncCall(0, cmd_name.c_str(), cmd_name.GetHint(),
                                 result, 1, args, nullptr);
            trace_success("current.cmd", hr, result);
            if(TJS_SUCCEEDED(hr))
                return true;
        } catch(eTJSScriptError &e) {
            trace_exception("script", e.GetMessage(), e.GetBlockName(),
                            e.GetSourceLine(), e.GetTrace());
        } catch(eTJS &e) {
            trace_exception("tjs", e.GetMessage(), TJS_W(""), -1, ttstr());
        } catch(...) {
            if(TVPLayerInputTraceEnabled()) {
                spdlog::info("LayerIntf onButtonClick item direct command parent={} source={} index={} path=current.cmd expr={} threw",
                             parent_layer ? parent_layer->GetName().AsStdString() : "",
                             source_layer ? source_layer->GetName().AsStdString() : "",
                             item_index, expression.AsStdString());
            }
        }
    }

    try {
        TVPExecuteExpression(expression, source_owner, result);
        trace_success("expression", TJS_S_OK, result);
        return true;
    } catch(eTJSScriptError &e) {
        trace_exception("script", e.GetMessage(), e.GetBlockName(),
                        e.GetSourceLine(), e.GetTrace());
        return false;
    } catch(eTJS &e) {
        trace_exception("tjs", e.GetMessage(), TJS_W(""), -1, ttstr());
        return false;
    } catch(...) {
        if(TVPLayerInputTraceEnabled()) {
            spdlog::info("LayerIntf onButtonClick item direct command parent={} source={} index={} expr={} threw",
                         parent_layer ? parent_layer->GetName().AsStdString() : "",
                         source_layer ? source_layer->GetName().AsStdString() : "",
                         item_index, expression.AsStdString());
        }
        return false;
    }
}

static bool TVPExecuteCafeStellaCurrentExpression(
    const ttstr &expression, tTJSNI_BaseLayer *parent_layer,
    tTJSNI_BaseLayer *source_layer, tjs_int item_index, tTJSVariant *result) {
    iTJSDispatch2 *source_owner =
        source_layer ? source_layer->GetOwnerNoAddRef() : nullptr;
    if(!source_owner)
        return false;

    auto trace_exception = [&](const char *kind, const ttstr &message,
                               const tjs_char *block, tjs_int line,
                               const ttstr &trace) {
        if(!TVPLayerInputTraceEnabled())
            return;
        spdlog::info("LayerIntf onButtonClick item direct expression parent={} source={} index={} expr={} threw kind={} message={} block={} line={} trace={}",
                     parent_layer ? parent_layer->GetName().AsStdString() : "",
                     source_layer ? source_layer->GetName().AsStdString() : "",
                     item_index, expression.AsStdString(), kind ? kind : "",
                     message.AsStdString(),
                     block ? ttstr(block).AsStdString() : "", line,
                     trace.AsStdString());
    };

    // Gallery page links are routed through the script-level Current proxy.
    // Keep a compact, opt-in state snapshot around changeGroup so we can tell
    // whether the command failed to dispatch or dispatched but selected an
    // empty/unbound page group.  This is deliberately generic and has no game
    // or filename knowledge; it is only enabled with the existing input trace.
    auto trace_gallery_state = [&](const char *phase) {
        if(!TVPLayerInputTraceEnabled() ||
           expression.AsStdString().find("changeGroup") == std::string::npos)
            return;
        const tjs_char *queries[] = {
            TJS_W("Current._object._currentPageGroup"),
            TJS_W("Current._object.pageGroupNames.join(',')"),
            TJS_W("Current._object.pageGroupBinds.join(',')"),
            TJS_W("Current._object.pageGroups.miu.start"),
            TJS_W("Current._object.pageGroups.miu.end"),
            TJS_W("Current._object.pageGroups.azu.start"),
            TJS_W("Current._object.pageGroups.azu.end"),
            TJS_W("Current._object.pageGroups.rio.start"),
            TJS_W("Current._object.pageGroups.rio.end"),
            TJS_W("Current._object.pageGroups.eri.start"),
            TJS_W("Current._object.pageGroups.eri.end"),
            TJS_W("Current._object.pageGroups.nic.start"),
            TJS_W("Current._object.pageGroups.nic.end"),
            TJS_W("Current._object.pageGroups.etc.start"),
            TJS_W("Current._object.pageGroups.etc.end"),
            TJS_W("Current._object.count"),
            TJS_W("Current._object.items.count"),
            TJS_W("Current._object.scutil.page"),
            TJS_W("Current._object.scutil.scroll"),
            TJS_W("Current._object.scutil._scrollMax"),
            TJS_W("Current._object.scutil.blockStep"),
            TJS_W("Current._object._rowcol"),
            TJS_W("Current._object.items[0].thumb"),
            TJS_W("Current._object.items[0].orig"),
            TJS_W("Current._object.items[0].scene.storage"),
            TJS_W("Current._object.items[0].scene.target"),
            TJS_W("Current._object.items[1].thumb"),
            TJS_W("Current._object.items[1].orig"),
            TJS_W("Current._object.items[1].scene.storage"),
            TJS_W("Current._object.items[2].thumb"),
            TJS_W("Current._object.items[2].orig"),
            TJS_W("Current._object.items[2].scene.storage"),
            TJS_W("Current._object.items[3].thumb"),
            TJS_W("Current._object.items[3].orig"),
            TJS_W("Current._object.items[3].scene.storage"),
            TJS_W("Current._object.items[4].thumb"),
            TJS_W("Current._object.items[4].orig"),
            TJS_W("Current._object.items[4].scene.storage"),
            TJS_W("Current._object.items[5].thumb"),
            TJS_W("Current._object.items[5].orig"),
            TJS_W("Current._object.items[5].scene.storage"),
            TJS_W("Current._object.items[6].thumb"),
            TJS_W("Current._object.items[6].orig"),
            TJS_W("Current._object.items[6].scene.storage"),
            TJS_W("Current._object.items[7].thumb"),
            TJS_W("Current._object.items[7].orig"),
            TJS_W("Current._object.items[7].scene.storage"),
            TJS_W("Current._object.items[8].thumb"),
            TJS_W("Current._object.items[8].orig"),
            TJS_W("Current._object.items[8].scene.storage"),
            TJS_W("(-1 \\ 9)"),
            TJS_W("(0 > 0)"),
            TJS_W("(0 < 0)"),
        };
        std::string values;
        for(const tjs_char *query : queries) {
            tTJSVariant value;
            try {
                TVPExecuteExpression(ttstr(query), source_owner, &value);
                if(!values.empty())
                    values += " | ";
                values += ttstr(query).AsStdString();
                values += "=";
                values += TVPVariantDebugString(value);
            } catch(...) {
                if(!values.empty())
                    values += " | ";
                values += ttstr(query).AsStdString();
                values += "=<error>";
            }
        }
        spdlog::info("LayerIntf gallery state phase={} parent={} source={} {}",
                     phase ? phase : "",
                     parent_layer ? parent_layer->GetName().AsStdString() : "",
                     source_layer ? source_layer->GetName().AsStdString() : "",
                     values);
    };

    try {
        trace_gallery_state("before");
        TVPExecuteExpression(expression, source_owner, result);
        trace_gallery_state("after");
        if(TVPLayerInputTraceEnabled()) {
            spdlog::info("LayerIntf onButtonClick item direct expression parent={} source={} index={} expr={} result={} hr=0",
                         parent_layer ? parent_layer->GetName().AsStdString() : "",
                         source_layer ? source_layer->GetName().AsStdString() : "",
                         item_index, expression.AsStdString(),
                         result ? TVPVariantDebugString(*result) : "");
        }
        return true;
    } catch(eTJSScriptError &e) {
        trace_exception("script", e.GetMessage(), e.GetBlockName(),
                        e.GetSourceLine(), e.GetTrace());
        return false;
    } catch(eTJS &e) {
        trace_exception("tjs", e.GetMessage(), TJS_W(""), -1, ttstr());
        return false;
    } catch(...) {
        if(TVPLayerInputTraceEnabled()) {
            spdlog::info("LayerIntf onButtonClick item direct expression parent={} source={} index={} expr={} threw",
                         parent_layer ? parent_layer->GetName().AsStdString() : "",
                         source_layer ? source_layer->GetName().AsStdString() : "",
                         item_index, expression.AsStdString());
        }
        return false;
    }
}

static bool TVPGetCafeStellaCurrentMode(tTJSNI_BaseLayer *parent_layer,
                                        tTJSNI_BaseLayer *source_layer,
                                        tjs_int item_index,
                                        std::string &mode_text) {
    tTJSVariant mode;
    if(!TVPExecuteCafeStellaCurrentCommand(
           TJS_W("getCurrentMode"), parent_layer, source_layer, item_index,
           &mode)) {
        return false;
    }
    mode_text = TVPVariantDebugString(mode);
    return true;
}

static bool TVPInvokeCafeStellaClosureMethod(
    const char *label, const tTJSVariantClosure &target,
    tTJSNI_BaseLayer *parent_layer, tTJSNI_BaseLayer *source_layer,
    tjs_int item_index, const tjs_char *method, tTJSVariant *result) {
    if(!target.Object)
        return false;

    ttstr method_name(method);
    tTJSVariant item_arg(item_index);
    tTJSVariant *item_args[1] = { &item_arg };
    tjs_error hr = TJS_E_FAIL;
    try {
        hr = target.FuncCall(0, method_name.c_str(), method_name.GetHint(),
                             result, 1, item_args, nullptr);
    } catch(eTJSScriptError &e) {
        if(TVPLayerInputTraceEnabled()) {
            spdlog::info("LayerIntf onButtonClick item numeric method label={} parent={} source={} index={} method={} threw kind=script message={} block={} line={} trace={}",
                         label ? label : "",
                         parent_layer ? parent_layer->GetName().AsStdString() : "",
                         source_layer ? source_layer->GetName().AsStdString() : "",
                         item_index, method_name.AsStdString(),
                         e.GetMessage().AsStdString(),
                         e.GetBlockName()
                             ? ttstr(e.GetBlockName()).AsStdString()
                             : "",
                         e.GetSourceLine(), e.GetTrace().AsStdString());
        }
        return false;
    } catch(eTJS &e) {
        if(TVPLayerInputTraceEnabled()) {
            spdlog::info("LayerIntf onButtonClick item numeric method label={} parent={} source={} index={} method={} threw kind=tjs message={}",
                         label ? label : "",
                         parent_layer ? parent_layer->GetName().AsStdString() : "",
                         source_layer ? source_layer->GetName().AsStdString() : "",
                         item_index, method_name.AsStdString(),
                         e.GetMessage().AsStdString());
        }
        return false;
    } catch(...) {
        if(TVPLayerInputTraceEnabled()) {
            spdlog::info("LayerIntf onButtonClick item numeric method label={} parent={} source={} index={} method={} threw",
                         label ? label : "",
                         parent_layer ? parent_layer->GetName().AsStdString() : "",
                         source_layer ? source_layer->GetName().AsStdString() : "",
                         item_index, method_name.AsStdString());
        }
        return false;
    }

    if(TVPLayerInputTraceEnabled()) {
        spdlog::info("LayerIntf onButtonClick item numeric method label={} parent={} source={} index={} method={} target={} this={} result={} hr={}",
                     label ? label : "",
                     parent_layer ? parent_layer->GetName().AsStdString() : "",
                     source_layer ? source_layer->GetName().AsStdString() : "",
                     item_index, method_name.AsStdString(),
                     static_cast<const void *>(target.Object),
                     static_cast<const void *>(target.ObjThis),
                     result ? TVPVariantDebugString(*result) : "", hr);
    }
    return TJS_SUCCEEDED(hr);
}

static bool TVPGetCafeStellaCurrentProperty(
    const char *label, const tTJSVariantClosure &object,
    tTJSVariant &current_value) {
    if(!object.Object)
        return false;

    static const tjs_char *property_names[] = { TJS_W("current"),
                                                TJS_W("Current"),
                                                TJS_W("_current") };
    for(const tjs_char *property : property_names) {
        ttstr property_name(property);
        tTJSVariant value;
        const tjs_error hr =
            object.PropGet(0, property_name.c_str(), property_name.GetHint(),
                           &value, nullptr);
        if(TVPLayerInputTraceEnabled()) {
            spdlog::info("LayerIntf getCurrent property label={} property={} hr={} value={}",
                         label ? label : "", property_name.AsStdString(), hr,
                         TJS_SUCCEEDED(hr) ? TVPVariantDebugString(value)
                                           : "<failed>");
        }
        if(TJS_SUCCEEDED(hr) && value.Type() == tvtObject) {
            current_value = value;
            return true;
        }
    }
    return false;
}

static bool TVPGetCafeStellaCurrentClosure(tTJSNI_BaseLayer *source_layer,
                                           tTJSVariant &current_value) {
    iTJSDispatch2 *source_owner =
        source_layer ? source_layer->GetOwnerNoAddRef() : nullptr;
    if(!source_owner)
        return false;
    try {
        TVPExecuteExpression(TJS_W("Current"), source_owner, &current_value);
        if(current_value.Type() == tvtObject)
            return true;
    } catch(...) {
    }

    tTJSVariantClosure source_object(source_owner, source_owner);
    if(TVPGetCafeStellaCurrentProperty("source", source_object,
                                       current_value))
        return true;

    tTJSVariantClosure source_action =
        source_layer ? source_layer->GetActionOwnerNoAddRef()
                     : tTJSVariantClosure();
    if(TVPGetCafeStellaCurrentProperty("sourceActionOwner", source_action,
                                       current_value))
        return true;

    return false;
}

static bool TVPInvokeCafeStellaCurrentMethod(
    tTJSNI_BaseLayer *parent_layer, tTJSNI_BaseLayer *source_layer,
    tjs_int item_index, const tjs_char *method, tTJSVariant *result) {
    tTJSVariant current_value;
    if(!TVPGetCafeStellaCurrentClosure(source_layer, current_value))
        return false;
    tTJSVariantClosure current =
        current_value.AsObjectClosureNoAddRef();
    return TVPInvokeCafeStellaClosureMethod(
        "Current", current, parent_layer, source_layer, item_index, method,
        result);
}

static bool TVPInvokeCafeStellaCurrentCommandFunction(
    tTJSNI_BaseLayer *parent_layer, tTJSNI_BaseLayer *source_layer,
    tjs_int item_index, const tjs_char *command, tTJSVariant *result) {
    tTJSVariant current_value;
    if(!TVPGetCafeStellaCurrentClosure(source_layer, current_value))
        return false;
    tTJSVariantClosure current =
        current_value.AsObjectClosureNoAddRef();
    if(!current.Object)
        return false;

    ttstr command_name(command);
    static ttstr cmd_func_name(TJS_W("cmd_func"));
    tTJSVariant command_arg(command_name);
    tTJSVariant item_arg(item_index);
    tTJSVariant *args[2] = { &command_arg, &item_arg };
    tjs_error hr = TJS_E_FAIL;
    try {
        hr = current.FuncCall(0, cmd_func_name.c_str(),
                              cmd_func_name.GetHint(), result, 2, args,
                              nullptr);
    } catch(eTJSScriptError &e) {
        if(TVPLayerInputTraceEnabled()) {
            spdlog::info("LayerIntf onButtonClick item numeric command-func parent={} source={} index={} command={} threw kind=script message={} block={} line={} trace={}",
                         parent_layer ? parent_layer->GetName().AsStdString() : "",
                         source_layer ? source_layer->GetName().AsStdString() : "",
                         item_index, command_name.AsStdString(),
                         e.GetMessage().AsStdString(),
                         e.GetBlockName()
                             ? ttstr(e.GetBlockName()).AsStdString()
                             : "",
                         e.GetSourceLine(), e.GetTrace().AsStdString());
        }
        return false;
    } catch(eTJS &e) {
        if(TVPLayerInputTraceEnabled()) {
            spdlog::info("LayerIntf onButtonClick item numeric command-func parent={} source={} index={} command={} threw kind=tjs message={}",
                         parent_layer ? parent_layer->GetName().AsStdString() : "",
                         source_layer ? source_layer->GetName().AsStdString() : "",
                         item_index, command_name.AsStdString(),
                         e.GetMessage().AsStdString());
        }
        return false;
    } catch(...) {
        if(TVPLayerInputTraceEnabled()) {
            spdlog::info("LayerIntf onButtonClick item numeric command-func parent={} source={} index={} command={} threw",
                         parent_layer ? parent_layer->GetName().AsStdString() : "",
                         source_layer ? source_layer->GetName().AsStdString() : "",
                         item_index, command_name.AsStdString());
        }
        return false;
    }

    if(TVPLayerInputTraceEnabled()) {
        spdlog::info("LayerIntf onButtonClick item numeric command-func parent={} source={} index={} command={} result={} hr={}",
                     parent_layer ? parent_layer->GetName().AsStdString() : "",
                     source_layer ? source_layer->GetName().AsStdString() : "",
                     item_index, command_name.AsStdString(),
                     result ? TVPVariantDebugString(*result) : "", hr);
    }
    return TJS_SUCCEEDED(hr);
}

static tjs_int TVPGetCafeStellaSaveLoadDataIndex(
    tTJSNI_BaseLayer *parent_layer, tTJSNI_BaseLayer *source_layer,
    tjs_int item_index) {
    tTJSVariant offset_value;
    try {
        if(TVPExecuteCafeStellaCurrentExpression(
               TJS_W("Current.propget(\"offset\")"), parent_layer,
               source_layer, item_index, &offset_value) &&
           offset_value.Type() != tvtVoid) {
            const tjs_int offset =
                static_cast<tjs_int>(offset_value.AsInteger());
            const tjs_int data_index = item_index + offset;
            if(TVPLayerInputTraceEnabled()) {
                spdlog::info("LayerIntf onButtonClick save-load index raw={} offset={} data={}",
                             item_index, offset, data_index);
            }
            return data_index;
        }
    } catch(...) {
    }
    if(TVPLayerInputTraceEnabled()) {
        spdlog::info("LayerIntf onButtonClick save-load index raw={} offset=<unavailable>",
                     item_index);
    }
    return item_index;
}

static bool TVPInvokeCafeStellaObjectPropertyMethod(
    const char *label, const tTJSVariantClosure &owner,
    tTJSNI_BaseLayer *parent_layer, tTJSNI_BaseLayer *source_layer,
    tjs_int item_index, const tjs_char *property, const tjs_char *method,
    tTJSVariant *result) {
    if(!owner.Object)
        return false;

    ttstr property_name(property);
    tTJSVariant property_value;
    tjs_error hr = owner.PropGet(0, property_name.c_str(),
                                 property_name.GetHint(), &property_value,
                                 nullptr);
    if(TVPLayerInputTraceEnabled()) {
        spdlog::info("LayerIntf onButtonClick item numeric property label={} parent={} source={} index={} property={} value={} hr={}",
                     label ? label : "",
                     parent_layer ? parent_layer->GetName().AsStdString() : "",
                     source_layer ? source_layer->GetName().AsStdString() : "",
                     item_index, property_name.AsStdString(),
                     TJS_SUCCEEDED(hr) ? TVPVariantDebugString(property_value)
                                       : "<failed>",
                     hr);
    }
    if(TJS_FAILED(hr) || property_value.Type() != tvtObject)
        return false;

    tTJSVariantClosure property_object =
        property_value.AsObjectClosureNoAddRef();
    return TVPInvokeCafeStellaClosureMethod(
        label, property_object, parent_layer, source_layer, item_index, method,
        result);
}

static bool TVPInvokeCafeStellaCurrentPropertyMethod(
    tTJSNI_BaseLayer *parent_layer, tTJSNI_BaseLayer *source_layer,
    tjs_int item_index, const tjs_char *property, const tjs_char *method,
    tTJSVariant *result) {
    tTJSVariant current_value;
    if(!TVPGetCafeStellaCurrentClosure(source_layer, current_value))
        return false;
    tTJSVariantClosure current =
        current_value.AsObjectClosureNoAddRef();
    return TVPInvokeCafeStellaObjectPropertyMethod(
        "Current.property", current, parent_layer, source_layer, item_index,
        property, method, result);
}

static bool TVPInvokeCafeStellaLayerPropertyMethod(
    tTJSNI_BaseLayer *owner_layer, tTJSNI_BaseLayer *parent_layer,
    tTJSNI_BaseLayer *source_layer, tjs_int item_index,
    const tjs_char *property, const tjs_char *method, tTJSVariant *result) {
    iTJSDispatch2 *owner_dispatch =
        owner_layer ? owner_layer->GetOwnerNoAddRef() : nullptr;
    if(!owner_dispatch)
        return false;

    tTJSVariantClosure owner(owner_dispatch, owner_dispatch);
    return TVPInvokeCafeStellaObjectPropertyMethod(
        "layer.property", owner, parent_layer, source_layer, item_index,
        property, method, result);
}

static bool TVPInvokeCafeStellaSaveLoadCurrentCommand(
    tTJSNI_BaseLayer *parent_layer, tTJSNI_BaseLayer *source_layer,
    tjs_int item_index, const std::string &mode_text, tTJSVariant *result) {
    tTJSVariant ignored;
    const tjs_int data_index =
        TVPGetCafeStellaSaveLoadDataIndex(parent_layer, source_layer,
                                          item_index);
    auto exec_numeric = [&](const tjs_char *command,
                            tjs_int command_index,
                            tTJSVariant *command_result) {
        ttstr command_name(command);
        ttstr index_text(command_index);
        if(TVPInvokeCafeStellaCurrentMethod(
               parent_layer, source_layer, command_index, command,
               command_result)) {
            return true;
        }
        if(TVPInvokeCafeStellaCurrentPropertyMethod(
               parent_layer, source_layer, command_index, TJS_W("_obj"),
               command, command_result)) {
            return true;
        }
        if(TVPExecuteCafeStellaCurrentExpression(
               ttstr(TJS_W("Current.func(\"")) + command_name +
                   TJS_W("\")(") + index_text + TJS_W(")"),
               parent_layer, source_layer, command_index, command_result)) {
            return true;
        }
        return TVPInvokeCafeStellaCurrentCommandFunction(
            parent_layer, source_layer, command_index, command,
            command_result);
    };

    exec_numeric(TJS_W("onItemEnter"), item_index, &ignored);
    exec_numeric(TJS_W("setIt"), item_index, &ignored);

    const tjs_char *candidate_commands[] = {
        TJS_W("onSelect"),
        TJS_W("onSel"),
        TJS_W("invoke"),
        TJS_W("onDefaultSelect"),
        TJS_W("onDefault"),
        mode_text == "save" ? TJS_W("onSave") : TJS_W("onLoad"),
        TJS_W("onDefaul"),
        TJS_W("onDefaultSe"),
        TJS_W("internalDef"),
    };
    for(const tjs_char *candidate_command : candidate_commands) {
        if(exec_numeric(candidate_command, data_index, result))
            return true;
    }
    return false;
}

static bool TVPInvokeCafeStellaCurrentDefault(
    tTJSNI_BaseLayer *parent_layer, tTJSNI_BaseLayer *source_layer,
    tjs_int item_index, tTJSVariant *result) {
    const ttstr index_text(item_index);
    tTJSVariant ignored;
    std::string mode_text;
    if(TVPGetCafeStellaCurrentMode(parent_layer, source_layer, item_index,
                                   mode_text)) {
        if(mode_text == "save" || mode_text == "load") {
            if(TVPLayerInputTraceEnabled()) {
                spdlog::info("LayerIntf onButtonClick item direct current-default state parent={} source={} index={} mode={}",
                             parent_layer ? parent_layer->GetName().AsStdString() : "",
                             source_layer ? source_layer->GetName().AsStdString() : "",
                             item_index, mode_text);
            }
            if(TVPInvokeCafeStellaSaveLoadCurrentCommand(
                   parent_layer, source_layer, item_index, mode_text,
                   result)) {
                return true;
            }
            return false;
        }
    }

    if(TVPExecuteCafeStellaCurrentCommand(
           ttstr(TJS_W("setItem/")) + index_text, parent_layer, source_layer,
           item_index, &ignored) &&
       TVPExecuteCafeStellaCurrentCommand(
           ttstr(TJS_W("onDefault/")) + index_text, parent_layer,
           source_layer, item_index, result)) {
        return true;
    }

    if(TVPExecuteCafeStellaCurrentCommand(
           ttstr(TJS_W("onItem/")) + index_text, parent_layer, source_layer,
           item_index, &ignored) &&
       TVPExecuteCafeStellaCurrentCommand(
           ttstr(TJS_W("onDefault/")) + index_text, parent_layer,
           source_layer, item_index, result)) {
        return true;
    }

    return TVPExecuteCafeStellaCurrentCommand(
        ttstr(TJS_W("onDefault/")) + index_text, parent_layer, source_layer,
        item_index, result);
}

static bool TVPInvokeCafeStellaSourceButtonClick(
    const tTJSVariantClosure &source_object, tTJSNI_BaseLayer *parent_layer,
    tTJSNI_BaseLayer *source_layer, tjs_int item_index, tTJSVariant *result) {
    if(!source_object.Object || !source_layer ||
       TVPCafeStellaSyntheticClickActive)
        return false;

    tTJSVariantClosure source_action =
        source_layer->GetActionOwnerNoAddRef();
    if(!source_action.Object)
        return false;

    const ttstr index_text(item_index);
    const ttstr onclick_expression =
        ttstr(TJS_W("(Current.cmd(\"setItem/")) + index_text +
        TJS_W("\")),(Current.cmd(\"onDefault/") + index_text + TJS_W("\"))");
    static ttstr onclick_name(TJS_W("onclick"));
    tTJSVariant previous_onclick;
    const tjs_error get_hr =
        source_object.PropGet(0, onclick_name.c_str(), onclick_name.GetHint(),
                              &previous_onclick, nullptr);
    tTJSVariant onclick_value(onclick_expression);
    tjs_error hr = source_object.PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP,
                                         onclick_name.c_str(),
                                         onclick_name.GetHint(),
                                         &onclick_value, nullptr);
    if(TVPLayerInputTraceEnabled()) {
        spdlog::info("LayerIntf onButtonClick item direct inject onclick parent={} source={} index={} get_hr={} set_hr={} expr={}",
                     parent_layer ? parent_layer->GetName().AsStdString() : "",
                     source_layer ? source_layer->GetName().AsStdString() : "",
                     item_index, get_hr, hr, onclick_expression.AsStdString());
    }
    if(TJS_FAILED(hr))
        return false;

    auto restore_onclick = [&]() {
        if(TJS_SUCCEEDED(get_hr)) {
            source_object.PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP,
                                  onclick_name.c_str(),
                                  onclick_name.GetHint(), &previous_onclick,
                                  nullptr);
        }
    };

    auto trace_exception = [&](const char *kind, const ttstr &message,
                               const tjs_char *block, tjs_int line,
                               const ttstr &trace) {
        if(!TVPLayerInputTraceEnabled())
            return;
        spdlog::info("LayerIntf onButtonClick item direct inject call parent={} source={} index={} threw kind={} message={} block={} line={} trace={}",
                     parent_layer ? parent_layer->GetName().AsStdString() : "",
                     source_layer ? source_layer->GetName().AsStdString() : "",
                     item_index, kind ? kind : "", message.AsStdString(),
                     block ? ttstr(block).AsStdString() : "", line,
                     trace.AsStdString());
    };

    tjs_int x = source_layer->GetWidth() > 0
                    ? static_cast<tjs_int>(source_layer->GetWidth() / 2)
                    : 0;
    tjs_int y = source_layer->GetHeight() > 0
                    ? static_cast<tjs_int>(source_layer->GetHeight() / 2)
                    : 0;
    tjs_int button = 0;
    tjs_int64 shift = 0;
    if(TVPLayerCurrentMouseUp.Active &&
       TVPLayerCurrentMouseUp.Layer == source_layer) {
        x = TVPLayerCurrentMouseUp.X;
        y = TVPLayerCurrentMouseUp.Y;
        button = TVPLayerCurrentMouseUp.Button;
        shift = TVPLayerCurrentMouseUp.Shift;
    }

    try {
        iTJSDispatch2 *source_dispatch = source_layer->GetOwnerNoAddRef();
        iTJSDispatch2 *evobj =
            TVPCreateEventObject(TJS_W("onMouseUp"), source_dispatch,
                                 source_dispatch);
        tTJSVariant evval(evobj, evobj);
        evobj->Release();

        auto set_event_member = [&](const tjs_char *name,
                                    tTJSVariant &value) {
            ttstr member_name(name);
            evobj->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP,
                           member_name.c_str(), member_name.GetHint(), &value,
                           evobj);
        };
        tTJSVariant x_arg(x);
        tTJSVariant y_arg(y);
        tTJSVariant button_arg(button);
        tTJSVariant shift_arg(shift);
        set_event_member(TJS_W("x"), x_arg);
        set_event_member(TJS_W("y"), y_arg);
        set_event_member(TJS_W("button"), button_arg);
        set_event_member(TJS_W("shift"), shift_arg);

        tTJSVariant *event_arg = &evval;
        TVPCafeStellaSyntheticClickActive = true;
        hr = source_action.FuncCall(0, TVPActionName.c_str(),
                                    TVPActionName.GetHint(), result, 1,
                                    &event_arg, nullptr);
        TVPCafeStellaSyntheticClickActive = false;
        restore_onclick();
    } catch(eTJSScriptError &e) {
        TVPCafeStellaSyntheticClickActive = false;
        restore_onclick();
        trace_exception("script", e.GetMessage(), e.GetBlockName(),
                        e.GetSourceLine(), e.GetTrace());
        return false;
    } catch(eTJS &e) {
        TVPCafeStellaSyntheticClickActive = false;
        restore_onclick();
        trace_exception("tjs", e.GetMessage(), TJS_W(""), -1, ttstr());
        return false;
    } catch(...) {
        TVPCafeStellaSyntheticClickActive = false;
        restore_onclick();
        if(TVPLayerInputTraceEnabled()) {
            spdlog::info("LayerIntf onButtonClick item direct inject call parent={} source={} index={} threw",
                         parent_layer ? parent_layer->GetName().AsStdString() : "",
                         source_layer ? source_layer->GetName().AsStdString() : "",
                         item_index);
        }
        return false;
    }

    if(TVPLayerInputTraceEnabled()) {
        spdlog::info("LayerIntf onButtonClick item direct inject call parent={} source={} index={} x={} y={} button={} shift={} hr={}",
                     parent_layer ? parent_layer->GetName().AsStdString() : "",
                     source_layer ? source_layer->GetName().AsStdString() : "",
                     item_index, x, y, button, shift, hr);
    }
    return TJS_SUCCEEDED(hr);
}

static bool TVPInvokeCafeStellaSaveLoadGridItem(tTJSNI_BaseLayer *layer,
                                                tTJSVariant *result) {
    tjs_int item_index = 0;
    if(!TVPGetCafeStellaSaveLoadGridItemIndex(layer, item_index) ||
       !TVPLayerEventSource) {
        return false;
    }
    TVPLayerLastSaveLoadItemIndex = item_index;

    iTJSDispatch2 *source_owner = TVPLayerEventSource->GetOwnerNoAddRef();
    if(!source_owner)
        return false;

    tTJSVariantClosure source_object(source_owner, source_owner);
    std::string mode_text;
    const bool has_cafestella_mode =
        TVPGetCafeStellaCurrentMode(layer, TVPLayerEventSource, item_index,
                                    mode_text);
    if(!has_cafestella_mode) {
        if(TVPLayerInputTraceEnabled()) {
            spdlog::info("LayerIntf onButtonClick item direct skip CafeStella fallback parent={} source={} index={} reason=no-current-mode",
                         layer->GetName().AsStdString(),
                         TVPLayerEventSource->GetName().AsStdString(),
                         item_index);
        }
        return false;
    }

    const bool save_load_mode =
        mode_text == "save" || mode_text == "load";
    if(TVPInvokeCafeStellaCurrentDefault(layer, TVPLayerEventSource,
                                         item_index, result))
        return true;
    if(!save_load_mode && TVPInvokeCafeStellaSourceButtonClick(
           source_object, layer, TVPLayerEventSource, item_index, result))
        return true;

    if(TVPInvokeCafeStellaGridItemCallbackOwner(
           "onenter", source_object, layer, TVPLayerEventSource, item_index,
           result))
        return true;
    if(TVPInvokeCafeStellaGridItemCallbackOwner(
           "onleave", source_object, layer, TVPLayerEventSource, item_index,
           result))
        return true;
    if(TVPInvokeCafeStellaGridItemCallbackOwner(
           "onclick", source_object, layer, TVPLayerEventSource, item_index,
           result))
        return true;

    static ttstr control_owner_name(TJS_W("controlOwner"));
    tTJSVariant control_owner_value;
    tjs_error hr =
        source_object.PropGet(0, control_owner_name.c_str(),
                              control_owner_name.GetHint(),
                              &control_owner_value, nullptr);
    if(TJS_FAILED(hr) || control_owner_value.Type() != tvtObject) {
        if(TVPLayerInputTraceEnabled()) {
            spdlog::info("LayerIntf onButtonClick item direct controlOwner missing parent={} source={} index={} hr={}",
                         layer->GetName().AsStdString(),
                         TVPLayerEventSource->GetName().AsStdString(),
                         item_index, hr);
        }
        return false;
    }

    tTJSVariantClosure control_owner =
        control_owner_value.AsObjectClosureNoAddRef();
    if(!control_owner.Object)
        return false;

    return TVPInvokeCafeStellaGridItemCandidate(
        "controlOwner", control_owner, layer, TVPLayerEventSource, item_index,
        result);
}

static int TVPAbsInt(int value) { return value < 0 ? -value : value; }

static constexpr tjs_uint TVPCafeStellaSaveThumbnailWidth = 408;
static constexpr tjs_uint TVPCafeStellaSaveThumbnailHeight = 230;

static std::string TVPLayerAsciiLower(std::string text) {
    for(char &ch : text) {
        if(ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return text;
}

static bool TVPLayerStorageNameLooksExtraThumbnail(const ttstr &name) {
    const std::string text = TVPLayerAsciiLower(name.AsStdString());
    return text.find("thum_") != std::string::npos;
}

static bool TVPLayerStorageNameLooksCafeStellaSaveThumbnail(const ttstr &name) {
    const std::string text = TVPLayerAsciiLower(name.AsStdString());
    if(text.find("savedata") == std::string::npos) {
        return false;
    }

    const size_t slash = text.find_last_of("/\\");
    const std::string file =
        slash == std::string::npos ? text : text.substr(slash + 1);

    return file.rfind("data_continue", 0) == 0 ||
           file.rfind("data_quick_", 0) == 0;
}

static bool TVPLayerStorageNameLooksThumbnail(const ttstr &name) {
    return TVPLayerStorageNameLooksExtraThumbnail(name) ||
           TVPLayerStorageNameLooksCafeStellaSaveThumbnail(name);
}

// UI scripts may first probe a language-specific image variant (for example
// `thum_ev102.png_cn`) and then pass that probe result directly to
// Layer.loadImages.  A large number of shipped games only provide the base
// image, so a missing variant must fall back to the unqualified storage name
// before the load is reported as an exception.  Keep this deliberately narrow
// to known language tags; a normal filename containing an arbitrary suffix
// must retain its existing behavior.
static bool TVPLayerStripLanguageVariant(const ttstr &name, ttstr &base) {
    const std::string text = name.AsStdString();
    static constexpr const char *suffixes[] = { "_cn",  "_tw", "_en",
                                                "_ja",  "_ko", "_chs",
                                                "_cht", "_jp", "_kr" };
    for(const char *suffix : suffixes) {
        const std::size_t length = std::strlen(suffix);
        if(text.size() <= length ||
           text.compare(text.size() - length, length, suffix) != 0)
            continue;

        const std::size_t stem_end = text.size() - length;
        // Require an extension before the language tag.  This avoids turning
        // a legitimate extensionless storage called `chapter_cn` into
        // `chapter`.
        const std::size_t dot = text.find_last_of("./\\");
        if(dot == std::string::npos || dot >= stem_end || text[dot] != '.')
            continue;
        base = ttstr{ text.substr(0, stem_end) };
        return true;
    }
    return false;
}

static bool TVPLayerTargetSizeLooksExtraThumbnail(tjs_uint width,
                                                  tjs_uint height) {
    return width >= 120 && width <= 512 && height >= 70 && height <= 320;
}

static bool TVPLayerSourceSizeLooksDownscaleThumbnail(tjs_uint source_width,
                                                      tjs_uint source_height,
                                                      tjs_uint target_width,
                                                      tjs_uint target_height) {
    if(!source_width || !source_height || !target_width || !target_height) {
        return false;
    }
    if(source_width <= target_width || source_height <= target_height) {
        return false;
    }
    const double source_aspect =
        static_cast<double>(source_width) / source_height;
    const double target_aspect =
        static_cast<double>(target_width) / target_height;
    return std::fabs(source_aspect - target_aspect) <= 0.05;
}

static bool TVPCafeStellaSourceSizeLooksSaveThumbnail(tjs_uint width,
                                                      tjs_uint height) {
    if(width < 480 || height < 260) {
        return false;
    }
    const double aspect = static_cast<double>(width) / height;
    return aspect >= 1.65 && aspect <= 1.9;
}

// PackinOne/ProxyStorage can expose a logical atlas name for which the
// original game deliberately does not ship a standalone TLG file.  Do not
// hide a real missing image behind a placeholder, but keep a missing virtual
// atlas from aborting the whole UI data pack: callers can still create their
// controls and use their authored geometry while the absent pixels remain
// transparent.  The size is intentionally bounded and only used when no
// authored image/fallback exists; concrete storage always wins above.
static bool TVPLayerNameLooksVirtualPack(const ttstr &name) {
    std::string storage = TVPExtractStorageName(name).AsStdString();
    if(storage.empty())
        storage = name.AsStdString();
    std::transform(storage.begin(), storage.end(), storage.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    for(const char *extension : {".tlg", ".tlg5", ".tlg6", ".png"}) {
        const std::size_t length = std::strlen(extension);
        if(storage.size() > length &&
           storage.compare(storage.size() - length, length, extension) == 0) {
            storage.resize(storage.size() - length);
            break;
        }
    }
    constexpr const char suffix[] = "__pack";
    // The native loader calls Layer.loadImages with the extensionless logical
    // name, while the graphic loader probes .tlg/.tlg5/.tlg6 internally.
    // Accept both spellings, but not an arbitrary filename ending in `pack`.
    return storage.size() > sizeof(suffix) - 1 &&
           storage.compare(storage.size() - (sizeof(suffix) - 1),
                           sizeof(suffix) - 1, suffix) == 0;
}

static bool TVPLayerUseVirtualPackPlaceholder(const ttstr &name) {
    const char *value = std::getenv("AETHERKIRI_VIRTUAL_PACK_PLACEHOLDER");
    if(value && *value == '0')
        return false;
    return TVPLayerNameLooksVirtualPack(name);
}

static bool TVPLayerThumbnailFitEnabled() {
    static const bool enabled = [] {
        const char *value = std::getenv("AETHERKIRI_THUMBNAIL_FIT");
        return !value || !*value || *value != '0';
    }();
    return enabled;
}

static bool TVPLayerLoadThumbnailFitted(tTVPBaseTexture *dest,
                                        const ttstr &name,
                                        tjs_uint32 colorkey,
                                        ttstr *provincename,
                                        iTJSDispatch2 **metainfo) {
    const bool extra_thumbnail = TVPLayerStorageNameLooksExtraThumbnail(name);
    const bool cafe_stella_save_thumbnail =
        TVPLayerStorageNameLooksCafeStellaSaveThumbnail(name);
    if(!TVPLayerThumbnailFitEnabled() || !dest ||
       (!extra_thumbnail && !cafe_stella_save_thumbnail)) {
        return false;
    }
    tjs_uint target_width = dest->GetWidth();
    tjs_uint target_height = dest->GetHeight();
    if(extra_thumbnail &&
       !TVPLayerTargetSizeLooksExtraThumbnail(target_width, target_height)) {
        return false;
    }

    tTVPBaseTexture source(1, 1);
    ttstr source_provincename;
    iTJSDispatch2 *source_metainfo = nullptr;
    TVPLoadGraphic(&source, name, colorkey, 0, 0, glmNormal,
                   &source_provincename, &source_metainfo);
    if(source_metainfo) {
        source_metainfo->Release();
    }

    if(cafe_stella_save_thumbnail) {
        if(!TVPCafeStellaSourceSizeLooksSaveThumbnail(source.GetWidth(),
                                                     source.GetHeight())) {
            return false;
        }
        // CafeStella's continue/quick-save hover copies cached save images
        // through a 408x230 source rect into a 204x115 UI slot. Pre-fitting
        // the cached bitmap keeps the engine's later source rect from cutting
        // diagonally through the original 587x330 save screenshot.
        target_width = TVPCafeStellaSaveThumbnailWidth;
        target_height = TVPCafeStellaSaveThumbnailHeight;
    } else if(extra_thumbnail) {
        if(!TVPLayerSourceSizeLooksDownscaleThumbnail(
               source.GetWidth(), source.GetHeight(), target_width,
               target_height)) {
            return false;
        }
        // Gallery scripts commonly load a full-size thumbnail into an already
        // sized thumbnail layer, then copy that layer's target rect. Keep the
        // script's rect behavior while avoiding a top-left crop of larger
        // same-aspect source images.
    }

    if(TVPLayerDebugEnabled() && TVPLayerDebugTake()) {
        spdlog::info(
            "Layer.loadImages thumbnail-fit name={} source={}x{} target={}x{}",
            name.AsStdString(), static_cast<int>(source.GetWidth()),
            static_cast<int>(source.GetHeight()), static_cast<int>(target_width),
            static_cast<int>(target_height));
    }

    dest->SetSizeWithFill(target_width, target_height, 0);
    dest->StretchBlt(tTVPRect(0, 0, target_width, target_height),
                     tTVPRect(0, 0, target_width, target_height), &source,
                     tTVPRect(0, 0, source.GetWidth(), source.GetHeight()),
                     bmCopy, 255, false, stFastLinear, 0.0);
    return true;
}

static bool TVPLayerGetObjectString(tTJSVariantClosure object,
                                    const tjs_char *name,
                                    ttstr &out) {
    tTJSVariant value;
    if(TJS_FAILED(object.PropGet(TJS_IGNOREPROP, name, nullptr, &value, nullptr)) ||
       value.Type() == tvtVoid) {
        return false;
    }
    out = ttstr(value);
    return !out.IsEmpty();
}

static std::vector<std::string> TVPLayerSplitPimgSeton(const std::string &seton) {
    std::vector<std::string> result;
    size_t start = 0;
    while(start <= seton.size()) {
        const size_t sep = seton.find(':', start);
        std::string part =
            sep == std::string::npos ? seton.substr(start)
                                      : seton.substr(start, sep - start);
        if(!part.empty()) {
            result.push_back(std::move(part));
        }
        if(sep == std::string::npos) {
            break;
        }
        start = sep + 1;
    }
    return result;
}

static std::string TVPLayerPimgArchiveFromStorage(std::string storage) {
    constexpr const char *scheme = "psb://";
    if(storage.rfind(scheme, 0) == 0) {
        storage.erase(0, std::strlen(scheme));
    }
    const size_t query = storage.find_first_of("?#");
    if(query != std::string::npos) {
        storage.erase(query);
    }
    const std::string lower = TVPLayerAsciiLower(storage);
    if(lower.size() < 5 || lower.substr(lower.size() - 5) != ".pimg") {
        storage += ".pimg";
    }
    return storage;
}

static bool TVPLayerLoadPimgComposite(tTJSNI_BaseLayer *layer,
                                      const ttstr &storage,
                                      const ttstr &seton,
                                      tjs_uint32 colorkey,
                                      iTJSDispatch2 **metainfo) {
    if(!layer) {
        return false;
    }

    PSB::PSBMedia *media = PSB::GetGlobalPSBMedia();
    if(!media) {
        return false;
    }

    const std::string archive =
        TVPLayerPimgArchiveFromStorage(storage.AsStdString());
    const std::string setonText = seton.AsStdString();
    const std::vector<std::string> requestedLayers =
        TVPLayerSplitPimgSeton(setonText);
    if(archive.empty() || requestedLayers.empty()) {
        return false;
    }

    media->ensureArchiveLoaded(archive, false);
    std::vector<PSB::PSBMedia::ImageInfoEntry> entries =
        media->getImagesByPrefix(archive);
    if(entries.empty() && media->ensureArchiveLoaded(archive, true)) {
        entries = media->getImagesByPrefix(archive);
    }
    if(entries.empty()) {
        return false;
    }

    std::unordered_map<std::string, const PSB::PSBMedia::ImageInfoEntry *>
        entriesByLabel;
    for(const auto &entry : entries) {
        if(entry.info.label.empty()) {
            continue;
        }
        entriesByLabel.emplace(TVPLayerAsciiLower(entry.info.label), &entry);
    }

    std::vector<const PSB::PSBMedia::ImageInfoEntry *> selected;
    selected.reserve(requestedLayers.size());
    for(auto it = requestedLayers.rbegin(); it != requestedLayers.rend(); ++it) {
        const auto found = entriesByLabel.find(TVPLayerAsciiLower(*it));
        if(found == entriesByLabel.end()) {
            spdlog::warn(
                "Layer.loadImages PIMG composite missing layer: archive={} seton={} layer={}",
                archive, setonText, *it);
            return false;
        }
        selected.push_back(found->second);
    }

    std::vector<TVPLayerInternal::PimgLayerRect> selectedRects;
    selectedRects.reserve(selected.size());
    for(const auto *entry : selected) {
        selectedRects.push_back({entry->info.left, entry->info.top,
                                 entry->info.width, entry->info.height});
    }

    TVPLayerInternal::PimgCompositeBounds bounds{};
    if(!TVPLayerInternal::ComputePimgCompositeBounds(selectedRects, bounds)) {
        std::vector<TVPLayerInternal::PimgLayerRect> fallbackRects;
        fallbackRects.reserve(entries.size());
        for(const auto &entry : entries) {
            fallbackRects.push_back({entry.info.left, entry.info.top,
                                     entry.info.width, entry.info.height});
        }
        if(!TVPLayerInternal::ComputePimgCompositeBounds(fallbackRects,
                                                         bounds)) {
            return false;
        }
    }

    tTVPBaseBitmap canvas(static_cast<tjs_uint>(bounds.width),
                          static_cast<tjs_uint>(bounds.height), 32);
    canvas.Fill(tTVPRect(0, 0, bounds.width, bounds.height), 0);

    bool wroteAnyLayer = false;
    iTJSDispatch2 *compositeMeta = nullptr;
    try {
        for(const auto *entry : selected) {
            const auto &info = entry->info;
            if(!info.visible || info.opacity <= 0) {
                continue;
            }

            tTVPBaseTexture source(1, 1);
            iTJSDispatch2 *layerMeta = nullptr;
            const ttstr layerStorage =
                ttstr(TJS_W("psb://")) + ttstr(entry->key.c_str());
            TVPLoadGraphic(&source, layerStorage, colorkey, 0, 0, glmNormal,
                           nullptr, compositeMeta ? nullptr : &layerMeta);
            if(layerMeta) {
                if(!compositeMeta) {
                    compositeMeta = layerMeta;
                } else {
                    layerMeta->Release();
                }
            }

            const tTVPRect sourceRect(
                0, 0, static_cast<tjs_int>(source.GetWidth()),
                static_cast<tjs_int>(source.GetHeight()));
            const tjs_int opacity =
                std::max(0, std::min(255, info.opacity));
            const tTVPBBBltMethod method =
                !wroteAnyLayer && opacity == 255 ? bmCopy : bmAlphaOnAlpha;
            canvas.Blt(info.left - bounds.left, info.top - bounds.top, &source,
                       sourceRect, method, opacity, false);
            wroteAnyLayer = true;
        }

        if(!wroteAnyLayer) {
            if(compositeMeta) {
                compositeMeta->Release();
            }
            return false;
        }

        layer->AssignMainImageWithUpdate(&canvas);
        spdlog::debug(
            "Layer.loadImages PIMG composite storage={} archive={} seton={} origin={},{} size={}x{} layers={}",
            storage.AsStdString(), archive, setonText, bounds.left, bounds.top,
            bounds.width, bounds.height, selected.size());
    } catch(...) {
        if(compositeMeta) {
            compositeMeta->Release();
        }
        spdlog::warn(
            "Layer.loadImages PIMG composite failed: storage={} archive={} seton={}",
            storage.AsStdString(), archive, setonText);
        return false;
    }

    if(metainfo) {
        *metainfo = compositeMeta;
    } else if(compositeMeta) {
        compositeMeta->Release();
    }
    return true;
}

static bool TVPLayerDebugNameLooksThumbnail(const ttstr &name) {
    return TVPLayerDebugEnabled() && TVPLayerStorageNameLooksThumbnail(name);
}

static bool TVPLayerDebugRectLooksThumbnail(int width, int height) {
    width = TVPAbsInt(width);
    height = TVPAbsInt(height);
    return (width >= 560 && width <= 610 && height >= 300 && height <= 350) ||
        (width >= 300 && width <= 350 && height >= 560 && height <= 610);
}

static bool TVPLayerDebugBitmapLooksThumbnail(const iTVPBaseBitmap *src,
                                              const tTVPRect &srcrect) {
    if(!src) {
        return false;
    }
    return TVPLayerDebugRectLooksThumbnail(static_cast<int>(src->GetWidth()),
                                           static_cast<int>(src->GetHeight())) ||
        TVPLayerDebugRectLooksThumbnail(srcrect.get_width(),
                                        srcrect.get_height());
}

static bool TVPLayerDebugShouldLogBitmap(const iTVPBaseBitmap *src,
                                         const tTVPRect &srcrect) {
    if(!TVPLayerDebugEnabled()) {
        return false;
    }
    return TVPLayerDebugTake() ||
        TVPLayerDebugBitmapLooksThumbnail(src, srcrect);
}

// Temporary, narrowly scoped inspection for gallery thumbnails.  The normal
// layer debug output intentionally omits object names because it is used for
// broad render profiling; this switch lets us distinguish the source atlas,
// scratch layer, and final tile while chasing a black thumbnail without
// changing the default log volume.
static bool TVPCopyTraceEnabled() {
    const char *value = std::getenv("AETHERKIRI_COPY_TRACE");
    return value && *value && *value != '0';
}

static bool TVPCopyTraceTake() {
    if(!TVPCopyTraceEnabled())
        return false;
    const char *all = std::getenv("AETHERKIRI_COPY_TRACE_ALL");
    if(all && *all && *all != '0')
        return true;
    static std::atomic<int> count{0};
    return count.fetch_add(1, std::memory_order_relaxed) < 5000;
}

static bool TVPCopyTraceThumbnail(const iTVPBaseBitmap *src,
                                  const tTVPRect &rect) {
    const char *thumbs = std::getenv("AETHERKIRI_COPY_TRACE_THUMBS");
    if(!thumbs || !*thumbs || *thumbs == '0')
        return true;
    if(!src)
        return false;
    const int width = static_cast<int>(src->GetWidth());
    const int height = static_cast<int>(src->GetHeight());
    const int rw = rect.get_width();
    const int rh = rect.get_height();
    return (width == 432 && height == 243) ||
           (width == 460 && height == 271) ||
           (width == 1380 && height == 271) ||
           (rw == 432 && rh == 243) || (rw == 460 && rh == 271) ||
           (rw == 1380 && rh == 271);
}

static const char *TVPLayerDebugDrawFaceName(tTVPDrawFace face) {
    switch(face) {
        case dfAlpha:
            return "alpha";
        case dfAddAlpha:
            return "addAlpha";
        case dfOpaque:
            return "opaque";
        case dfMask:
            return "mask";
        case dfProvince:
            return "province";
        case dfAuto:
            return "auto";
        default:
            return "unknown";
    }
}
//---------------------------------------------------------------------------

static bool IsGPU() {
    const bool isGPU = !TVPIsSoftwareRenderManager() &&
        !IndividualConfigManager::GetInstance()->GetValue<bool>(
            "ogl_accurate_render", false);
    return isGPU;
}

//---------------------------------------------------------------------------
// temporary bitmap management
//---------------------------------------------------------------------------
class tTVPTempBitmapHolder;

static tTVPTempBitmapHolder *TVPTempBitmapHolder = nullptr;

class tTVPTempBitmapHolder : public tTVPCompactEventCallbackIntf {
    /*
                Initial layer bitmap and temporary bitmaps ( for
       window updating ) holder

                TVP(kirikiri) will be sometimes executed as a child
       process, simply returns a argument information, or simply
       manages the restarting of itself. object that is not necessary
       at first should not be created at beginning of the process :-)
        */

    tTVPBaseTexture *Bitmap;

    std::vector<tTVPBaseTexture *> Temporaries;
    tjs_uint TempLevel;
    bool TempCompactInit;

private:
    tjs_int RefCount;

    tTVPTempBitmapHolder() : TempLevel(0), TempCompactInit(false) {
        // the default image must be a transparent, white colored
        // rectangle
        RefCount = 1;
        Bitmap = new tTVPBaseTexture(32, 32);
        Bitmap->Fill(tTVPRect(0, 0, 32, 32), TVP_RGBA2COLOR(255, 255, 255, 0));
    }

    ~tTVPTempBitmapHolder() {
        std::vector<tTVPBaseTexture *>::iterator i;
        for(i = Temporaries.begin(); i != Temporaries.end(); i++) {
            delete(*i);
        }
        if(TempCompactInit)
            TVPRemoveCompactEventHook(this);
        if(Bitmap)
            delete Bitmap;
    }

    tTVPBaseTexture *InternalGetTemp(tjs_uint w, tjs_uint h, bool fit) {
        // compact initialization
        if(!TempCompactInit) {
            TVPAddCompactEventHook(this);
            TempCompactInit = true;
        }

        // align width to even
        if(!fit)
            w += (w & 1);

        // get temporary bitmap (nested)
        TempLevel++;
        if(TempLevel > Temporaries.size()) {
            // increase buffer size
            tTVPBaseTexture *bmp = new tTVPBaseTexture(w, h);
            Temporaries.push_back(bmp);
            return bmp;
        } else {
            tTVPBaseTexture *bmp = Temporaries[TempLevel - 1];
            if(!fit) {
                tjs_uint bw = bmp->GetWidth();
                tjs_uint bh = bmp->GetHeight();
                if(bw < w || bh < h) {
                    // increase image size
                    bmp->SetSize(bw > w ? bw : w, bh > h ? bh : h, false);
                }
            } else {
                // the size must be fitted
                tjs_uint bw = bmp->GetWidth();
                tjs_uint bh = bmp->GetHeight();
                if(bw != w || bh != h)
                    bmp->SetSize(w, h, false);
            }
            return bmp;
        }
    }

    void InternalFreeTemp() {
        if(TempLevel == 0)
            return; // this must be a logical failure
        TempLevel--;
    }

    void CompactTempBitmap() {
        // compact tmporary bitmap cache
        std::vector<tTVPBaseTexture *>::iterator i;
        for(i = Temporaries.begin() + TempLevel; i != Temporaries.end(); i++) {
            delete(*i);
        }

        Temporaries.resize(TempLevel);
    }

    void OnCompact(tjs_int level) override {
        // OnCompact method from tTVPCompactEventCallbackIntf
        // called when the application is idle, deactivated,
        // minimized, or etc...
        if(level >= TVP_COMPACT_LEVEL_IDLE)
            CompactTempBitmap();
    }

public:
    static void AddRef() {
        if(!TVPTempBitmapHolder)
            TVPTempBitmapHolder = new tTVPTempBitmapHolder();
        else
            TVPTempBitmapHolder->RefCount++;
    }

    static void Release() {
        if(!TVPTempBitmapHolder)
            return;
        if(TVPTempBitmapHolder->RefCount == 1) {
            delete TVPTempBitmapHolder;
            TVPTempBitmapHolder = nullptr;
        } else {
            TVPTempBitmapHolder->RefCount--;
        }
    }

    static const tTVPBaseTexture *Get() { return TVPTempBitmapHolder->Bitmap; }

    static tTVPBaseTexture *GetTemp(tjs_uint w, tjs_uint h, bool fit = false) {
        return TVPTempBitmapHolder->InternalGetTemp(w, h, fit);
    }

    static void FreeTemp() { TVPTempBitmapHolder->InternalFreeTemp(); }
};

//---------------------------------------------------------------------------
const tTVPBaseTexture &TVPGetInitialBitmap() {
    tTVPTempBitmapHolder::AddRef(); // ensure default bitmap
    const tTVPBaseTexture *bmp = TVPTempBitmapHolder->Get();
    tTVPTempBitmapHolder::Release();

    return *bmp;
}

//---------------------------------------------------------------------------
void TVPTempBitmapHolderAddRef() {
    tTVPTempBitmapHolder::AddRef(); // ensure default bitmap
}

//---------------------------------------------------------------------------
void TVPTempBitmapHolderRelease() { tTVPTempBitmapHolder::Release(); }
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// FOR EACH CHILD
//---------------------------------------------------------------------------
#define TVP_LAYER_FOR_EACH_CHILD_BEGIN(varname)                                \
    {                                                                          \
        tObjectListSafeLockHolder<tTJSNI_BaseLayer> __holder(Children);        \
        tjs_int __count = Children.GetSafeLockedObjectCount();                 \
        tjs_int __i;                                                           \
        for(__i = 0; __i < __count; __i++) {                                   \
            tTJSNI_BaseLayer *varname = Children.GetSafeLockedObjectAt(__i);   \
            if(!varname)                                                       \
                continue;

#define TVP_LAYER_FOR_EACH_CHILD_END                                           \
    }                                                                          \
    ChildrenArrayValid = false;                                                \
    ChildrenOrderIndexValid = false;                                           \
    if(Manager)                                                                \
        Manager->InvalidateOverallIndex();                                     \
    }
//---------------------------------------------------------------------------
// for each child ( for static only access )
#define TVP_LAYER_FOR_EACH_CHILD_NOLOCK_BEGIN(varname)                         \
    {                                                                          \
        tjs_int __count = Children.GetCount();                                 \
        tjs_int __i;                                                           \
        for(__i = 0; __i < __count; __i++) {                                   \
            tTJSNI_BaseLayer *varname = Children[__i];                         \
            if(!varname)                                                       \
                continue;

#define TVP_LAYER_FOR_EACH_CHILD_NOLOCK_END                                    \
    }                                                                          \
    }

#define TVP_LAYER_FOR_EACH_CHILD_NOLOCK_BACKWARD_BEGIN(varname)                \
    {                                                                          \
        tjs_int __count = Children.GetCount();                                 \
        tjs_int __i;                                                           \
        for(__i = __count - 1; __i >= 0; __i--) {                              \
            tTJSNI_BaseLayer *varname = Children[__i];                         \
            if(!varname)                                                       \
                continue;

#define TVP_LAYER_FOR_EACH_CHILD_NOLOCK_BACKWARD_END                           \
    }                                                                          \
    }
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// Recursive Call
//---------------------------------------------------------------------------
#define TVP_LAYER_REC_CALL(funccall, action)                                   \
    action;                                                                    \
    TVP_LAYER_FOR_EACH_CHILD_BEGIN(child)                                      \
    child->funccall;                                                           \
    TVP_LAYER_FOR_EACH_CHILD_END
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// global options
//---------------------------------------------------------------------------
tTVPGraphicSplitOperationType TVPGraphicSplitOperationType = gsotNone;
bool TVPDefaultHoldAlpha = false;
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// below is the tTJSNI_BaseLayer implementation ( pretty large )
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// object lifetime stuff
//---------------------------------------------------------------------------
tTJSNI_BaseLayer::tTJSNI_BaseLayer() {
    TVPLayerInstanceCount.fetch_add(1, std::memory_order_relaxed);
    // creates bitmap holder
    tTVPTempBitmapHolder::AddRef();

    // object lifetime stuff
    Owner = nullptr;
    ActionOwner.ObjThis = ActionOwner.Object = nullptr;
    Shutdown = false;
    CompactEventHookInit = false;

    // interface to layer manager
    Manager = nullptr;

    // tree management
    Parent = nullptr;
    Visible = false;
    Opacity = 255;
    VisibleChildrenCount = -1;
    ChildrenArray = nullptr;
    ChildrenArrayValid = false;
    ArrayClearMethod = nullptr;
    OrderIndex = 0;
    OverallOrderIndex = 0;
    ChildrenOrderIndexValid = false;
    AbsoluteOrderMode = false; // initially relative mode
    AbsoluteOrderIndex = 0;

    // layer type management
    DisplayType = Type = ltAlpha;
    // later reset this if the layer becomes a primary layer
    NeutralColor = TransparentColor = TVP_RGBA2COLOR(255, 255, 255, 0);

    // geographical management
    ExposedRegionValid = false;
    Rect.left = 0;
    Rect.top = 0;
    Rect.right = 32;
    Rect.bottom = 32;

    // input event / hit test management
    HitType = htMask;
    HitThreshold = 16;
    Cursor = 0; // 0 = crDefault
    CursorX_Work = 0;
    ShowParentHint = true;
    IgnoreHintSensing = false;
    UseAttention = false;
    ImeMode = ::imDisable;
    AttentionLeft = AttentionTop = 0;

    Enabled = true;
    Focusable = false;
    JoinFocusChain = true;

    // image buffer management
    MainImage = nullptr;
    CanHaveImage = true;
    ProvinceImage = nullptr;
    ImageLeft = 0;
    ImageTop = 0;

    // cache management
    CacheEnabledCount = 0;
    CacheBitmap = nullptr;
    Cached = false;

    // drawing function stuff
    Face = dfAuto;
    UpdateDrawFace();
    ImageModified = false;
    HoldAlpha = TVPDefaultHoldAlpha;
    ClipRect.left = 0;
    ClipRect.right = 0;
    ClipRect.top = 0;
    ClipRect.bottom = 0;

    // Updating management
    CallOnPaint = false;
    InCompletion = false;

    // transition management
    DivisibleTransHandler = nullptr;
    GiveUpdateTransHandler = nullptr;
    TransDest = nullptr;
    TransDestObj = nullptr;
    TransSrc = nullptr;
    TransSrcObj = nullptr;
    InTransition = false;
    TransWithChildren = false;
    TransDrawable.Src1Bmp = nullptr;
    TransDrawable.Src2Bmp = nullptr;
    TransDrawable.SnapshotWarmupFrames = 0;
    TransDrawable.SkipSnapshotFrame = false;
    DestSLP = nullptr;
    SrcSLP = nullptr;
    TransCompEventPrevented = false;
    UseTransTickCallback = false;
    TransTickCallback = tTJSVariantClosure(nullptr, nullptr);

    // allocate the default image
    AllocateDefaultImage();

    // interface to font object
    Font = MainImage->GetFont(); // retrieve default font
    FontObject = nullptr;

#if 0
                                                                                                                            // province management
	ClearProvinceInformation();
#endif
}

//---------------------------------------------------------------------------
tTJSNI_BaseLayer::~tTJSNI_BaseLayer() {
    TVPLayerInstanceCount.fetch_sub(1, std::memory_order_relaxed);
    tTVPTempBitmapHolder::Release();
}

//---------------------------------------------------------------------------
tjs_error tTJSNI_BaseLayer::Construct(tjs_int numparams, tTJSVariant **param,
                                      iTJSDispatch2 *tjs_obj) {
    if(numparams < 2)
        return TJS_E_BADPARAMCOUNT;

    Owner = tjs_obj; // no addref
#if defined(__ANDROID__)
    spdlog::info("Layer construct begin native={} owner={} argc={}",
                 static_cast<void *>(this), static_cast<void *>(Owner),
                 static_cast<int>(numparams));
#endif

    // get the window native instance
    tTJSVariantClosure clo = param[0]->AsObjectClosureNoAddRef();
    // if(clo.Object == nullptr)
    // TVPThrowExceptionMessage(TVPSpecifyWindow);
    if(clo.Object == nullptr)
        TVPThrowExceptionMessage(
            TJS_W("Please specify layerTreeOwnerInterface object"));

    class iTVPLayerTreeOwner *lto = nullptr;
    tTJSVariant iface_v;
    if(TJS_FAILED(clo.PropGet(0, TJS_W("layerTreeOwnerInterface"), nullptr,
                              &iface_v, nullptr))) {
#if defined(__ANDROID__)
        spdlog::warn("Layer construct failed: no layerTreeOwnerInterface native={} owner={} windowObj={} windowThis={}",
                     static_cast<void *>(this), static_cast<void *>(Owner),
                     static_cast<void *>(clo.Object),
                     static_cast<void *>(clo.ObjThis));
#endif
        TVPThrowExceptionMessage(
            TJS_W("Cannot Retrive Layer Tree Owner Interface."));
    }
    lto = reinterpret_cast<iTVPLayerTreeOwner *>(
        (tjs_intptr_t)(tjs_int64)iface_v);

    // get the layer native instance
    clo = param[1]->AsObjectClosureNoAddRef();
    tTJSNI_Layer *lay = nullptr;
    if(clo.Object) {
        tjs_error lay_hr = clo.Object->NativeInstanceSupport(
            TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
            (iTJSNativeInstance **)&lay);
        if(TJS_FAILED(lay_hr)) {
#if defined(__ANDROID__)
            spdlog::warn("Layer construct failed: invalid parent native={} owner={} parentObj={} parentThis={} hr={}",
                         static_cast<void *>(this), static_cast<void *>(Owner),
                         static_cast<void *>(clo.Object),
                         static_cast<void *>(clo.ObjThis), lay_hr);
#endif
            TVPThrowExceptionMessage(TVPSpecifyLayer);
        }
    }

    // retrieve manager
    // layer manager is the same as the parent, if the parent is given
    if(lay) {
        Manager = lay->GetManager();
        if(Manager)
            Manager->AddRef(); // lock manager
    }

    // register to parent layer
    if(lay)
        Join(lay);

    // is primarylayer ?
    // ask window to create layer manager
    if(!lay) {
        Manager = new tTVPLayerManager(lto);
        Manager->AttachPrimary(this);
        Manager->RegisterSelfToWindow();

        Type = DisplayType = ltOpaque; // initially ltOpaque
        NeutralColor = TransparentColor = TVP_RGBA2COLOR(255, 255, 255, 255);
        UpdateDrawFace();
        HitThreshold = 0;
    }
    //	IncCacheEnabledCount(); ///// -------------------- test

    ActionOwner = param[0]->AsObjectClosure();
#if defined(__ANDROID__)
    spdlog::info("Layer construct end native={} owner={} parent={} manager={} primary={} actionObj={} actionThis={}",
                 static_cast<void *>(this), static_cast<void *>(Owner),
                 static_cast<void *>(Parent), static_cast<void *>(Manager),
                 IsPrimary() ? "yes" : "no",
                 static_cast<void *>(ActionOwner.Object),
                 static_cast<void *>(ActionOwner.ObjThis));
#endif

    return TJS_S_OK;
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::Invalidate() {
#if defined(__ANDROID__)
    spdlog::info("Layer invalidate native={} owner={} name='{}' parent={} manager={} primary={}",
                 static_cast<void *>(this), static_cast<void *>(Owner),
                 Name.AsStdString(), static_cast<void *>(Parent),
                 static_cast<void *>(Manager), IsPrimary() ? "yes" : "no");
#endif
    Shutdown = true;

    // stop transition
    StopTransition();
    if(TransDest)
        TransDest->StopTransition();
    if(DestSLP)
        DestSLP->Release(), DestSLP = nullptr;
    if(SrcSLP)
        SrcSLP->Release(), SrcSLP = nullptr;

    // cancel events
    TVPCancelSourceEvents(Owner);

    // release all objects
    if(IsPrimary()) {
        if(Manager)
            Manager->DetachPrimary();
        // also detach from draw device
        Manager->UnregisterSelfFromWindow();
    }

    if(Manager) {
        Manager->Release(); // no longer used in this context
        Manager = nullptr;
    }

    // part from the parent
    Part();

    // sever all children
    TVP_LAYER_FOR_EACH_CHILD_BEGIN(child)
    child->Part();
    TVP_LAYER_FOR_EACH_CHILD_END

    // invalidate font object
    if(FontObject) {
        FontObject->Invalidate(0, nullptr, nullptr, FontObject);
        FontObject->Release();
    }

    // deallocate image
    DeallocateImage();

    // free cache image
    DeallocateCache();

    // release the owner
    ActionOwner.Release();
    ActionOwner.ObjThis = ActionOwner.Object = nullptr;

    // release Children array
    if(ChildrenArray)
        ChildrenArray->Release(), ChildrenArray = nullptr;
    if(ArrayClearMethod)
        ArrayClearMethod->Release(), ArrayClearMethod = nullptr;

    // unregister from compact event hook
    if(CompactEventHookInit)
        TVPRemoveCompactEventHook(this);

    // cancel events once more
    TVPCancelSourceEvents(Owner);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::RegisterCompactEventHook() {
    if(!CompactEventHookInit) {
        TVPAddCompactEventHook(this);
        CompactEventHookInit = true;
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::OnCompact(tjs_int level) {
    if(level >= TVP_COMPACT_LEVEL_IDLE)
        CompactCache();
    if(MainImage) {
        if(level >= TVP_COMPACT_LEVEL_MINIMIZE) {
            MainImage->CompactGPUCache();
        }
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// Interface to Manager
//---------------------------------------------------------------------------
iTVPLayerTreeOwner *tTJSNI_BaseLayer::GetLayerTreeOwner() const {
    if(!Manager)
        return nullptr;
    return Manager->GetLayerTreeOwner();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tree management
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::Join(tTJSNI_BaseLayer *parent) {
    TVPTraceStage2Lifecycle(
        "join-before", this,
        fmt::format("new_parent={}", parent ? parent->GetName().AsStdString()
                                             : "<none>"));
    if(parent == this)
        TVPThrowExceptionMessage(TVPCannotSetParentSelf);
    if(parent && parent->Manager != Manager)
        TVPThrowExceptionMessage(TVPCannotMoveToUnderOtherPrimaryLayer);
    if(Parent)
        Part();
    Parent = parent;
    if(Parent)
        parent->AddChild(this);
    TVPTraceStage2Lifecycle("join-after", this);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::Part() {
    Update();

    if(Manager)
        Manager->NotifyPart(this);

    if(Parent != nullptr)
        Parent->SeverChild(this), Parent = nullptr;
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::AddChild(tTJSNI_BaseLayer *child) {
    NotifyChildrenVisualStateChanged();
    Children.Add(child);
    if(AbsoluteOrderMode) {
        // first insertion
        Children.Compact();
        tjs_int count = Children.GetCount();
        if(count >= 2) {
            tTJSNI_BaseLayer *last = Children[count - 2];
            child->AbsoluteOrderIndex = last->GetAbsoluteOrderIndex() + 1;
        }
    }
    ChildrenArrayValid = false;
    ChildrenOrderIndexValid = false;
    if(Manager)
        Manager->CheckTreeFocusableState(
            child); // check focusable state of child
    if(Manager)
        Manager->InvalidateOverallIndex();
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SeverChild(tTJSNI_BaseLayer *child) {
    if(Manager)
        Manager->BlurTree(child); // remove focus from "child"
    NotifyChildrenVisualStateChanged();
    Children.Remove(child);
    ChildrenArrayValid = false;
    ChildrenOrderIndexValid = false;
    if(Manager)
        Manager->InvalidateOverallIndex();
}

//---------------------------------------------------------------------------
iTJSDispatch2 *tTJSNI_BaseLayer::GetChildrenArrayObjectNoAddRef() {
    if(!ChildrenArray) {
        // create an Array object
        iTJSDispatch2 *classobj;
        ChildrenArray = TJSCreateArrayObject(&classobj);
        try {
            tTJSVariant val;
            tjs_error er;
            er = classobj->PropGet(0, TJS_W("clear"), nullptr, &val, classobj);
            // retrieve clear method
            if(TJS_FAILED(er))
                TVPThrowInternalError;
            ArrayClearMethod = val.AsObject();
        } catch(...) {
            ChildrenArray->Release();
            ChildrenArray = nullptr;
            classobj->Release();
            throw;
        }
        classobj->Release();
    }

    if(!ChildrenArrayValid) {
        // re-create children list
        ArrayClearMethod->FuncCall(0, nullptr, nullptr, nullptr, 0, nullptr,
                                   ChildrenArray);
        // clear array

        tjs_int count = 0;
        TVP_LAYER_FOR_EACH_CHILD_NOLOCK_BEGIN(child)
        iTJSDispatch2 *dsp = child->Owner;
        tTJSVariant val(dsp, dsp);
        ChildrenArray->PropSetByNum(TJS_MEMBERENSURE, count, &val,
                                    ChildrenArray);
        count++;
        TVP_LAYER_FOR_EACH_CHILD_NOLOCK_END

        ChildrenArrayValid = true;
    }

    return ChildrenArray;
}

//---------------------------------------------------------------------------
tTJSNI_BaseLayer *
tTJSNI_BaseLayer::GetAncestorChild(tTJSNI_BaseLayer *ancestor) {
    // retrieve "ancestor"'s child that is ancestor of this ( can be
    // thisself )
    tTJSNI_BaseLayer *c = this;
    tTJSNI_BaseLayer *p = Parent;
    while(p) {
        if(p == ancestor)
            return c;
        c = p;
        p = p->Parent;
    }
    return nullptr;
}

//---------------------------------------------------------------------------
bool tTJSNI_BaseLayer::IsAncestor(tTJSNI_BaseLayer *ancestor) {
    // is "ancestor" is ancestor of this layer ? (cannot be itself)
    tTJSNI_BaseLayer *p = Parent;
    while(p) {
        if(p == ancestor)
            return true;
        p = p->Parent;
    }
    return false;
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::RecreateOverallOrderIndex(
    tjs_uint &index, std::vector<tTJSNI_BaseLayer *> &nodes) {
    OverallOrderIndex = index;
    index++;

    nodes.push_back(this);

    TVP_LAYER_FOR_EACH_CHILD_NOLOCK_BEGIN(child)
    child->RecreateOverallOrderIndex(index, nodes);
    TVP_LAYER_FOR_EACH_CHILD_NOLOCK_END
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::Exchange(tTJSNI_BaseLayer *target, bool keepchildren) {
    // exchange this for the other layer
    if(this == target)
        return;

    tTJSNI_BaseLayer *this_ancestor_child = this->GetAncestorChild(target);
    tTJSNI_BaseLayer *target_ancestor_child = target->GetAncestorChild(this);

    tTJSNI_BaseLayer *this_parent = this->Parent;
    tTJSNI_BaseLayer *target_parent = target->Parent;

    bool this_primary = this->IsPrimary();
    bool target_primary = target->IsPrimary();

    bool this_parent_absolute = true;
    if(this->Parent)
        this_parent_absolute = this->Parent->AbsoluteOrderMode;
    tjs_int this_index = this->GetAbsoluteOrderIndex();

    bool target_parent_absolute = true;
    if(target->Parent)
        target_parent_absolute = target->Parent->AbsoluteOrderMode;
    tjs_int target_index = target->GetAbsoluteOrderIndex();

    // remove primary
    if(Manager) {
        if(this_primary)
            Manager->DetachPrimary();
        if(target_primary)
            Manager->DetachPrimary();
    }

    // part from each parent
    this->Part();
    target->Part();

    tTJSNI_BaseLayer *this_joined_parent;
    tTJSNI_BaseLayer *target_joined_parent;
    if(this_ancestor_child) {
        // "this" is a descendant of the "target"
        if(this_ancestor_child != this)
            this_ancestor_child->Part();

        if(!keepchildren) {
            // join to each target's parent
            this->Join(this_joined_parent = target_parent);
            if(target == this_parent)
                target->Join(target_joined_parent = this);
            else
                target->Join(target_joined_parent = this_parent);
        } else {
            // sever children
            std::vector<tjs_int> this_orders;
            std::vector<tjs_int> target_orders;
            tObjectList<tTJSNI_BaseLayer> this_children(this->Children);
            tObjectList<tTJSNI_BaseLayer> target_children(target->Children);
            tjs_int this_children_count = this_children.GetActualCount();
            tjs_int target_children_count = target_children.GetActualCount();

            for(int i = 0; i < this_children_count; i++) {
                this_orders.push_back(
                    this_children[i]->GetAbsoluteOrderIndex());
                this_children[i]->Part();
            }

            for(int i = 0; i < target_children_count; i++) {
                target_orders.push_back(
                    target_children[i]->GetAbsoluteOrderIndex());
                target_children[i]->Part();
            }

            // join to each target's parent
            this->Join(this_joined_parent = target_parent);
            if(target == this_parent)
                target->Join(target_joined_parent = this);
            else
                target->Join(target_joined_parent = this_parent);

            // let children join
            for(int i = 0; i < this_children_count; i++)
                this_children[i]->Join(target);
            for(int i = 0; i < target_children_count; i++)
                target_children[i]->Join(this);

            if(this->AbsoluteOrderMode && target->AbsoluteOrderMode) {
                // reset order index
                for(int i = 0; i < this_children_count; i++)
                    this_children[i]->SetAbsoluteOrderIndex(this_orders[i]);
                for(int i = 0; i < target_children_count; i++)
                    target_children[i]->SetAbsoluteOrderIndex(target_orders[i]);
            }
        }

        if(this_ancestor_child != this)
            this_ancestor_child->Join(this);
    } else if(target_ancestor_child) {
        // "target" is a descendant of "this"
        if(target_ancestor_child != target)
            target_ancestor_child->Part();

        if(!keepchildren) {
            // join to each target's parent
            if(this == target_parent)
                this->Join(this_joined_parent = target);
            else
                this->Join(this_joined_parent = target_parent);
            target->Join(target_joined_parent = this_parent);
        } else {
            // sever children
            std::vector<tjs_int> this_orders;
            std::vector<tjs_int> target_orders;
            tObjectList<tTJSNI_BaseLayer> this_children(this->Children);
            tObjectList<tTJSNI_BaseLayer> target_children(target->Children);
            tjs_int this_children_count = this_children.GetActualCount();
            tjs_int target_children_count = target_children.GetActualCount();

            for(int i = 0; i < this_children_count; i++) {
                this_orders.push_back(
                    this_children[i]->GetAbsoluteOrderIndex());
                this_children[i]->Part();
            }
            for(int i = 0; i < target_children_count; i++) {
                target_orders.push_back(
                    target_children[i]->GetAbsoluteOrderIndex());
                target_children[i]->Part();
            }

            // join to each target's parent
            if(this == target_parent)
                this->Join(this_joined_parent = target);
            else
                this->Join(this_joined_parent = target_parent);
            target->Join(target_joined_parent = this_parent);

            // let children join
            for(int i = 0; i < this_children_count; i++)
                this_children[i]->Join(target);
            for(int i = 0; i < target_children_count; i++)
                target_children[i]->Join(this);

            if(this->AbsoluteOrderMode && target->AbsoluteOrderMode) {
                // reset order index
                for(int i = 0; i < this_children_count; i++)
                    this_children[i]->SetAbsoluteOrderIndex(this_orders[i]);
                for(int i = 0; i < target_children_count; i++)
                    target_children[i]->SetAbsoluteOrderIndex(target_orders[i]);
            }
        }

        if(target_ancestor_child != target)
            target_ancestor_child->Join(target);
    } else {
        // two layers have no parent-child relationship
        if(!keepchildren) {
            // join to each target's parent
            this->Join(this_joined_parent = target_parent);
            target->Join(target_joined_parent = this_parent);
        } else {
            // sever children
            std::vector<tjs_int> this_orders;
            std::vector<tjs_int> target_orders;
            tObjectList<tTJSNI_BaseLayer> this_children(this->Children);
            tObjectList<tTJSNI_BaseLayer> target_children(target->Children);
            tjs_int this_children_count = this_children.GetActualCount();
            tjs_int target_children_count = target_children.GetActualCount();

            for(int i = 0; i < this_children_count; i++) {
                this_orders.push_back(
                    this_children[i]->GetAbsoluteOrderIndex());
                this_children[i]->Part();
            }
            for(int i = 0; i < target_children_count; i++) {
                target_orders.push_back(
                    target_children[i]->GetAbsoluteOrderIndex());
                target_children[i]->Part();
            }

            // join to each target's parent
            this->Join(this_joined_parent = target_parent);
            target->Join(target_joined_parent = this_parent);

            // let children join
            for(int i = 0; i < this_children_count; i++)
                this_children[i]->Join(target);
            for(int i = 0; i < target_children_count; i++)
                target_children[i]->Join(this);

            if(this->AbsoluteOrderMode && target->AbsoluteOrderMode) {
                // reset order index
                for(int i = 0; i < this_children_count; i++)
                    this_children[i]->SetAbsoluteOrderIndex(this_orders[i]);
                for(int i = 0; i < target_children_count; i++)
                    target_children[i]->SetAbsoluteOrderIndex(target_orders[i]);
            }
        }
    }

    // attach primary
    if(Manager) {
        if(this_primary)
            Manager->AttachPrimary(target);
        if(target_primary)
            Manager->AttachPrimary(this);
    }

    // reset order index
    if(target_joined_parent == this_joined_parent && target_joined_parent &&
       target_joined_parent->AbsoluteOrderMode == target_parent_absolute &&
       this_joined_parent->AbsoluteOrderMode == this_parent_absolute &&
       target_parent_absolute == this_parent_absolute &&
       target_parent_absolute == false) {
        // two layers have the same parent and the same order mode
        if(this_index < target_index) {
            target->SetOrderIndex(this_index);
            this->SetOrderIndex(target_index);
        } else {
            this->SetOrderIndex(target_index);
            target->SetOrderIndex(this_index);
        }
    } else {
        if(target_joined_parent &&
           target_joined_parent->AbsoluteOrderMode == target_parent_absolute) {
            if(target_parent_absolute)
                target->SetAbsoluteOrderIndex(target_index);
            else
                target->SetOrderIndex(target_index);
        }

        if(this_joined_parent &&
           this_joined_parent->AbsoluteOrderMode == this_parent_absolute) {
            if(this_parent_absolute)
                this->SetAbsoluteOrderIndex(this_index);
            else
                this->SetOrderIndex(this_index);
        }
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::CheckChildrenVisibleState() {
    if(!GetCount()) {
        VisibleChildrenCount = 0;
        return;
    }
    VisibleChildrenCount = 0;

    TVP_LAYER_FOR_EACH_CHILD_NOLOCK_BEGIN(child)
    if(child->GetVisible() && child->GetOpacity() != 0)
        VisibleChildrenCount++;
    TVP_LAYER_FOR_EACH_CHILD_NOLOCK_END
}

void tTJSNI_BaseLayer::ApplyLightContrast(tjs_int brightness_offset,
                                          tjs_int contrast_value) {
    if(!CanHaveImage || !MainImage || !GetHasImage()) {
        TVPAddLog(
            TJS_W("BaseLayer::ApplyLightContrast: Layer cannot have, does not "
                  "have, or GetHasImage is false for the main image."));
        return;
    }


    tjs_uint image_width = GetImageWidth();
    tjs_uint image_height = GetImageHeight();

    if(image_width == 0 || image_height == 0) {
        TVPAddLog(
            TJS_W("BaseLayer::ApplyLightContrast: Image dimensions are zero."));
        return;
    }

    void *buffer_raw = GetMainImagePixelBufferForWrite();
    if(!buffer_raw) {
        TVPAddLog(TJS_W("BaseLayer::ApplyLightContrast: Failed to get main "
                        "image pixel buffer for writing."));
        return;
    }

    tjs_uint8 *pixel_buffer_base = static_cast<tjs_uint8 *>(buffer_raw);
    tjs_int pitch = GetMainImagePixelBufferPitch();
    const int bytes_per_pixel = 4; // 假设 32-bit ARGB

    // 对比度调整因子计算
    // contrast_value: 假设范围是 -100 到 100 (一个常见的范围)
    // 映射到更适合公式的值，例如 -255 到 255 (如果 contrast_value 定义如此)
    // 如果 contrast_value 就是 -255 到 255，则不需要映射
    // 这里我们假设 contrast_value 是一个可以直接用于公式的标度
    // 一个常用的对比度公式因子:
    // contrast_value 一般范围在 -255 到 255. 0 是无变化.
    // 我们需要将其转换为一个乘法因子。
    // 如果 contrast_value 为 C (范围 -255 到 255):
    // factor = (259.0 * (C + 255.0)) / (255.0 * (259.0 - C));
    // 如果 C 为 0, factor = (259*255)/(255*259) = 1 (无变化)
    // 如果 C 为 255, factor = (259*510)/(255*4) = 129.5 (最大对比度) -
    // 这个公式可能需要调整或选择其他
    //
    // 另一种简单的对比度方法：
    // factor = (100.0 + contrast_value) / 100.0; // 如果 contrast_value
    // 是百分比 (-100 to infinity) newColor = factor * (oldColor - 128) + 128;
    //
    // 为了简单，我们使用一个可调的 contrast_level (例如 -127 到 127)
    // float fContrast = 1.0f + (contrast_value / 127.0f); // 假设
    // contrast_value 在 -127 到 127 如果 contrast_value 在 TJS 中是 0-255
    // 或类似，你需要先规范化

    double actual_contrast_factor;
    if(contrast_value == 0) {
        actual_contrast_factor = 1.0;
    } else {
        // 使用一个更标准化的对比度公式
        // 让 TJS 传入的 contrast_value 在 -100 到 100 之间
        // 如果 contrast_value = 0, factor should be 1.0
        // 如果 contrast_value = 100, factor might be 2.0 (增强对比度)
        // 如果 contrast_value = -100, factor might be 0.0 (降低对比度到灰色)
        // 我们需要定义 contrast_value 的含义。假设它是一个百分比调整。
        // 例如, 0 = no change, 100 = double contrast range, -100 = flatten to
        // grey 一个简单的线性映射： if contrast_value in [-100, 100]
        // mapped_contrast = (contrast_value + 100.0) / 100.0; // range [0, 2]
        // This is not standard.
        // Let's use the formula: Factor = (259 * (Contrast + 255)) / (255 *
        // (259 - Contrast)) Where Contrast is in [-255, 255]. If your TJS
        // FaceContrast is, say, 0-100, you need to map it. Example mapping: if
        // FaceContrast is 0..100 (0=normal, 100=max) then C_formula =
        // FaceContrast * 2.55; (maps 100 to 255) If FaceContrast is -100..100
        // (0=normal) then C_formula = FaceContrast * 2.55;

        // 我们假设 TJS 传入的 contrast_value 就是公式中的 C (范围 -255 到 255)
        if(contrast_value > 255)
            contrast_value = 255;
        if(contrast_value < -255)
            contrast_value = -255;

        if(contrast_value == 255) { // 避免除以零或接近零
            actual_contrast_factor =
                259.0 * (255.0 + 255.0) / (255.0 * (259.0 - 254.9)); // 近似
        } else if(contrast_value == -255) {
            actual_contrast_factor = 0; // 变成灰色
        } else {
            actual_contrast_factor = (259.0 * (contrast_value + 255.0)) /
                (255.0 * (259.0 - contrast_value));
        }
    }


    for(tjs_uint y = 0; y < image_height; ++y) {
        tjs_uint8 *current_scanline_ptr = pixel_buffer_base + (y * pitch);
        for(tjs_uint x = 0; x < image_width; ++x) {
            tjs_uint32 *current_pixel_val_ptr = reinterpret_cast<tjs_uint32 *>(
                current_scanline_ptr + x * bytes_per_pixel);
            tjs_uint32 original_color = *current_pixel_val_ptr;

            tjs_uint8 a_comp = (original_color >> 24) & 0xFF;
            tjs_uint8 r_comp = (original_color >> 16) & 0xFF;
            tjs_uint8 g_comp = (original_color >> 8) & 0xFF;
            tjs_uint8 b_comp = original_color & 0xFF;

            // 1. 应用亮度调整
            tjs_int r_bright = static_cast<tjs_int>(r_comp) + brightness_offset;
            tjs_int g_bright = static_cast<tjs_int>(g_comp) + brightness_offset;
            tjs_int b_bright = static_cast<tjs_int>(b_comp) + brightness_offset;

            // Clamp brightness adjusted values
            r_bright = (r_bright < 0) ? 0 : ((r_bright > 255) ? 255 : r_bright);
            g_bright = (g_bright < 0) ? 0 : ((g_bright > 255) ? 255 : g_bright);
            b_bright = (b_bright < 0) ? 0 : ((b_bright > 255) ? 255 : b_bright);

            // 2. 应用对比度调整 (在亮度调整后的值上)
            double r_contrast =
                actual_contrast_factor * (r_bright - 128.0) + 128.0;
            double g_contrast =
                actual_contrast_factor * (g_bright - 128.0) + 128.0;
            double b_contrast =
                actual_contrast_factor * (b_bright - 128.0) + 128.0;

            // Clamp contrast adjusted values
            r_comp = static_cast<tjs_uint8>(
                (r_contrast < 0) ? 0 : ((r_contrast > 255) ? 255 : r_contrast));
            g_comp = static_cast<tjs_uint8>(
                (g_contrast < 0) ? 0 : ((g_contrast > 255) ? 255 : g_contrast));
            b_comp = static_cast<tjs_uint8>(
                (b_contrast < 0) ? 0 : ((b_contrast > 255) ? 255 : b_contrast));

            *current_pixel_val_ptr = (static_cast<tjs_uint32>(a_comp) << 24) |
                (static_cast<tjs_uint32>(r_comp) << 16) |
                (static_cast<tjs_uint32>(g_comp) << 8) |
                static_cast<tjs_uint32>(b_comp);
        }
    }

    SetImageModified(true);
    Update();

    TVPAddLog(TJS_W("BaseLayer::ApplyLightContrast: Brightness adjusted by ") +
              ttstr(brightness_offset) + TJS_W(", Contrast factor applied: ") +
              ttstr(std::to_string(actual_contrast_factor)) +
              TJS_W(" (from input contrast_value: ") + ttstr(contrast_value) +
              TJS_W(")"));
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::NotifyChildrenVisualStateChanged() {
    VisibleChildrenCount = -1;
    SetToCreateExposedRegion(); // in geographical management
    if(Manager)
        Manager->NotifyVisualStateChanged();
}

//---------------------------------------------------------------------------
tjs_uint tTJSNI_BaseLayer::GetOverallOrderIndex() {
    if(!Manager)
        return 0;
    Manager->RecreateOverallOrderIndex();
    return OverallOrderIndex;
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::RecreateOrderIndex() {
    // recreate order index information
    tjs_uint index = 0;

    TVP_LAYER_FOR_EACH_CHILD_NOLOCK_BEGIN(child)
    child->OrderIndex = index;
    index++;
    TVP_LAYER_FOR_EACH_CHILD_NOLOCK_END

    ChildrenOrderIndexValid = true;
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetVisible(bool st) {
    const bool changed = Visible != st;
    if(changed)
        TVPTraceFlashLifecycle(st ? "visible-on-before" : "visible-off-before",
                               this);
    if(changed)
        TVPTraceStage2Lifecycle(st ? "visible-on-before" : "visible-off-before",
                                this);
    if(changed) {
        if(IsPrimary() && !st)
            TVPThrowExceptionMessage(TVPCannotSetPrimaryInvisible);
        if(st && _bitmapEvicted)
            EnsureBitmap();
        if(!st)
            Update();
        Visible = st;
        if(st)
            Update();
        if(Parent)
            Parent->NotifyChildrenVisualStateChanged();
        if(Visible) {
            if(Manager)
                Manager->CheckTreeFocusableState(this);
        } else {
            if(Manager)
                Manager->BlurTree(this); // in input/keyboard focus management
        }
    }
    if(changed)
        TVPTraceStage2Lifecycle(st ? "visible-on-after" : "visible-off-after",
                                this);
    if(changed)
        TVPTraceFlashLifecycle(st ? "visible-on-after" : "visible-off-after",
                               this);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetOpacity(tjs_int opa) {
    const bool changed = Opacity != opa;
    if(changed)
        TVPTraceFlashLifecycle("opacity-before", this,
                               fmt::format("new={}", opa));
    if(changed)
        TVPTraceStage2Lifecycle("opacity-before", this,
                                fmt::format("new={}", opa));
    if(const char *trace = std::getenv("AETHERKIRI_MESSAGE_FRAME_TRACE");
       trace && *trace && *trace != '0' &&
       (GetName() == TJS_W("colorframe") ||
        GetName().AsStdString().find("メッセージレイヤ") != std::string::npos)) {
        spdlog::info("message-frame opacity layer={} old={} new={}",
                     GetName().AsNarrowStdString(), Opacity, opa);
    }
    if(Opacity != opa) {
        if(IsPrimary() && opa != 255)
            TVPThrowExceptionMessage(TVPCannotSetPrimaryInvisible);
        if(opa != 0 && Visible && _bitmapEvicted)
            EnsureBitmap();
        Opacity = opa;
        if(Parent)
            Parent->NotifyChildrenVisualStateChanged();
        Update();
    }
    if(changed)
        TVPTraceStage2Lifecycle("opacity-after", this);
    if(changed)
        TVPTraceFlashLifecycle("opacity-after", this);
}

//---------------------------------------------------------------------------
bool tTJSNI_BaseLayer::IsPrimary() const {
    if(!Manager)
        return false;
    return Manager->GetPrimaryLayer() == this;
}

//---------------------------------------------------------------------------
bool tTJSNI_BaseLayer::GetParentVisible() const {
    // is parent visible? this does not check opacity
    tTJSNI_BaseLayer *par = Parent;
    while(par) {
        if(!par->Visible) {
            return false;
        }
        par = par->Parent;
    }

    return true;
}

//---------------------------------------------------------------------------
tTJSNI_BaseLayer *tTJSNI_BaseLayer::GetNeighborAbove(bool loop) {
    if(!Manager)
        return nullptr;

    tjs_uint index = GetOverallOrderIndex();
    std::vector<tTJSNI_BaseLayer *> &allnodes = Manager->GetAllNodes();

    if(allnodes.size() == 0)
        return nullptr; // must be an error !!

    if(index == 0) {
        // first ( primary )
        if(loop)
            return *(allnodes.end() - 1);
        else
            return nullptr;
    }

    return *(allnodes.begin() + index - 1);
}

//---------------------------------------------------------------------------
tTJSNI_BaseLayer *tTJSNI_BaseLayer::GetNeighborBelow(bool loop) {
    if(!Manager)
        return nullptr;

    tjs_uint index = GetOverallOrderIndex();
    std::vector<tTJSNI_BaseLayer *> &allnodes = Manager->GetAllNodes();

    if(allnodes.size() == 0)
        return nullptr; // must be an error !!

    if(index == allnodes.size() - 1) {
        // last
        if(loop)
            return *(allnodes.begin());
        else
            return nullptr;
    }

    return *(allnodes.begin() + index + 1);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::CheckZOrderMoveRule(tTJSNI_BaseLayer *lay) {
    if(!Parent)
        TVPThrowExceptionMessage(TVPCannotMovePrimaryOrSiblingless);
    if(Parent->Children.GetActualCount() <= 1)
        TVPThrowExceptionMessage(TVPCannotMovePrimaryOrSiblingless);
    if(lay == this)
        TVPThrowExceptionMessage(TVPCannotMoveNextToSelfOrNotSiblings);
    if(lay->Parent != Parent)
        TVPThrowExceptionMessage(TVPCannotMoveNextToSelfOrNotSiblings);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::ChildChangeOrder(tjs_int from, tjs_int to) {
    // called from children; change child's order from "from" to "to"
    // given orders are treated as orders before re-ordering.
    if(from == to)
        return; // no action

    tTJSNI_BaseLayer *fromlay;
    tTVPComplexRect rects;
    Children.Compact();
    if(from < to) {
        // forward

        // rotate
        fromlay = Children[from];
        for(tjs_int i = from; i < to; i++) {
            Children[i] = Children[i + 1];
            tTVPRect r = fromlay->Rect;
            if(TVPIntersectRect(&r, r, Children[i]->Rect))
                rects.Or(r); // add rectangle to update
        }
        Children[to] = fromlay;
    } else {
        // backward

        // rotate
        fromlay = Children[from];
        for(tjs_int i = from; i > to; i--) {
            Children[i] = Children[i - 1];
            tTVPRect r = fromlay->Rect;
            if(TVPIntersectRect(&r, r, Children[i]->Rect))
                rects.Or(r);
        }
        Children[to] = fromlay;
    }

    // update
    Update(rects);

    // clear caches
    ChildrenArrayValid = false;
    ChildrenOrderIndexValid = false;
    if(Manager)
        Manager->InvalidateOverallIndex();
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::ChildChangeAbsoluteOrder(tjs_int from, tjs_int abs_to) {
    // find index order
    tjs_int to = 0;
    TVP_LAYER_FOR_EACH_CHILD_NOLOCK_BEGIN(child)
    if(child->AbsoluteOrderIndex >= abs_to)
        break;
    to++;
    TVP_LAYER_FOR_EACH_CHILD_NOLOCK_END

    if(from < to)
        to--;

    ChildChangeOrder(from, to);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::MoveBefore(tTJSNI_BaseLayer *lay) {
    // move before sibling : lay
    // lay must not be a nullptr
    if(Parent)
        Parent->SetAbsoluteOrderMode(false);

    CheckZOrderMoveRule(lay);

    tjs_int this_order = GetOrderIndex();
    tjs_int lay_order = lay->GetOrderIndex();

    if(this_order < lay_order)
        Parent->ChildChangeOrder(this_order,
                                 lay_order); // move forward
    else
        Parent->ChildChangeOrder(this_order,
                                 lay_order + 1); // move backward
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::MoveBehind(tTJSNI_BaseLayer *lay) {
    // move behind sibling : lay
    // lay must not be a nullptr
    if(Parent)
        Parent->SetAbsoluteOrderMode(false);

    CheckZOrderMoveRule(lay);

    tjs_int this_order = GetOrderIndex();
    tjs_int lay_order = lay->GetOrderIndex();

    if(this_order < lay_order)
        Parent->ChildChangeOrder(this_order,
                                 lay_order - 1); // move forward
    else
        Parent->ChildChangeOrder(this_order,
                                 lay_order); // move backward
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetOrderIndex(tjs_int index) {
    // change order index
    if(!Parent)
        TVPThrowExceptionMessage(TVPCannotMovePrimaryOrSiblingless);

    Parent->SetAbsoluteOrderMode(false);

    if(index < 0)
        index = 0;
    if(index >= (tjs_int)Parent->Children.GetActualCount())
        index = Parent->Children.GetActualCount() - 1;

    Parent->ChildChangeOrder(GetOrderIndex(), index);
    Parent->NotifyChildrenVisualStateChanged();
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::BringToBack() {
    // to most back position
    if(!Parent)
        TVPThrowExceptionMessage(TVPCannotMovePrimaryOrSiblingless);

    Parent->SetAbsoluteOrderMode(false);

    SetOrderIndex(0);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::BringToFront() {
    // to most front position
    if(!Parent)
        TVPThrowExceptionMessage(TVPCannotMovePrimaryOrSiblingless);

    Parent->SetAbsoluteOrderMode(false);

    SetOrderIndex(Parent->Children.GetActualCount() - 1);
}

//---------------------------------------------------------------------------
tjs_int tTJSNI_BaseLayer::GetAbsoluteOrderIndex() {
    // retrieve order index in absolute position
    if(!Parent)
        return 0;
    if(Parent->AbsoluteOrderMode)
        return AbsoluteOrderIndex;
    return GetOrderIndex();
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetAbsoluteOrderIndex(tjs_int index) {
    if(!Parent)
        TVPThrowExceptionMessage(TVPCannotMovePrimaryOrSiblingless);

    Parent->SetAbsoluteOrderMode(true);

    Parent->ChildChangeAbsoluteOrder(GetOrderIndex(), index);

    AbsoluteOrderIndex = index;
    Parent->NotifyChildrenVisualStateChanged();
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetAbsoluteOrderMode(bool b) {
    // set absolute order index mode
    if(AbsoluteOrderMode != b) {
        AbsoluteOrderMode = b;
        if(b) {
            // to absolute order mode
            TVP_LAYER_FOR_EACH_CHILD_NOLOCK_BEGIN(child)
            child->AbsoluteOrderIndex = child->GetOrderIndex();
            TVP_LAYER_FOR_EACH_CHILD_NOLOCK_END
        } else {
            // to relative order mode

            // nothing to do
        }
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::DumpStructure(int level) {
    tjs_char *indent = new tjs_char[level * 2 + 1];
    try {
        for(tjs_int i = 0; i < level * 2; i++)
            indent[i] = TJS_W(' ');
        indent[level * 2] = 0;

        ttstr name = Name;
        if(name.IsEmpty())
            name = TJS_W("<noname>");
        ttstr ptr{ fmt::format(" (object {})", static_cast<void *>(Owner)) };
        ttstr ptr2{ fmt::format(" (native {})", static_cast<void *>(this)) };

        TVPAddLog(ttstr(indent) + name + ttstr(ptr) + ttstr(ptr2) +
                  ttstr(TJS_W(" (")) + ttstr(Rect.left) + ttstr(TJS_W(",")) +
                  ttstr(Rect.top) + ttstr(TJS_W(")-(")) + ttstr(Rect.right) +
                  ttstr(TJS_W(",")) + ttstr(Rect.bottom) + ttstr(TJS_W(") (")) +
                  ttstr(Rect.get_width()) + ttstr(TJS_W("x")) +
                  ttstr(Rect.get_height()) + ttstr(TJS_W(")")) +
                  ttstr(TJS_W(" ")) +
                  ttstr(GetVisible() ? TJS_W("visible") : TJS_W("invisible")) +
                  TJS_W(" index=") + ttstr(GetAbsoluteOrderIndex()) +
                  ttstr(ProvinceImage ? TJS_W(" p") : TJS_W("")) + TJS_W(" ") +
                  ttstr(GetTypeNameString()));
    } catch(...) {
        delete[] indent;
        throw;
    }

    delete[] indent;

    level++;
    TVP_LAYER_FOR_EACH_CHILD_NOLOCK_BEGIN(child)
    child->DumpStructure(level);
    TVP_LAYER_FOR_EACH_CHILD_NOLOCK_END
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// layer type management
//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::NotifyLayerTypeChange() {
    UpdateDrawFace();

    if(Parent)
        Parent->NotifyLayerTypeChange();
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::UpdateDrawFace() {
    // set DrawFace from Face and Type
    if(Face == dfAuto) {
        // DrawFace is chosen automatically from the layer type
        switch(DisplayType) {
            //	case ltBinder:
            case ltOpaque:
                DrawFace = dfOpaque;
                break;
            case ltAlpha:
                DrawFace = dfAlpha;
                break;
            case ltAdditive:
                DrawFace = dfOpaque;
                break;
            case ltSubtractive:
                DrawFace = dfOpaque;
                break;
            case ltMultiplicative:
                DrawFace = dfOpaque;
                break;
                //	case ltEffect:
                //	case ltFilter:
            case ltDodge:
                DrawFace = dfOpaque;
                break;
            case ltDarken:
                DrawFace = dfOpaque;
                break;
            case ltLighten:
                DrawFace = dfOpaque;
                break;
            case ltScreen:
                DrawFace = dfOpaque;
                break;
            case ltAddAlpha:
                DrawFace = dfAddAlpha;
                break;
            case ltPsNormal:
                DrawFace = dfAlpha;
                break;
            case ltPsAdditive:
                DrawFace = dfAlpha;
                break;
            case ltPsSubtractive:
                DrawFace = dfAlpha;
                break;
            case ltPsMultiplicative:
                DrawFace = dfAlpha;
                break;
            case ltPsScreen:
                DrawFace = dfAlpha;
                break;
            case ltPsOverlay:
                DrawFace = dfAlpha;
                break;
            case ltPsHardLight:
                DrawFace = dfAlpha;
                break;
            case ltPsSoftLight:
                DrawFace = dfAlpha;
                break;
            case ltPsColorDodge:
                DrawFace = dfAlpha;
                break;
            case ltPsColorDodge5:
                DrawFace = dfAlpha;
                break;
            case ltPsColorBurn:
                DrawFace = dfAlpha;
                break;
            case ltPsLighten:
                DrawFace = dfAlpha;
                break;
            case ltPsDarken:
                DrawFace = dfAlpha;
                break;
            case ltPsDifference:
                DrawFace = dfAlpha;
                break;
            case ltPsDifference5:
                DrawFace = dfAlpha;
                break;
            case ltPsExclusion:
                DrawFace = dfAlpha;
                break;
            default:
                DrawFace = dfOpaque;
                break;
        }
    } else {
        DrawFace = Face;
    }
}

//---------------------------------------------------------------------------
tTVPBlendOperationMode tTJSNI_BaseLayer::GetOperationModeFromType() const {
    // returns corresponding blend operation mode from layer type

    switch(DisplayType) {
            //	case ltBinder:
        case ltOpaque:
            return omOpaque;
        case ltAlpha:
            return omAlpha;
        case ltAdditive:
            return omAdditive;
        case ltSubtractive:
            return omSubtractive;
        case ltMultiplicative:
            return omMultiplicative;
            //	case ltEffect:
            //	case ltFilter:
        case ltDodge:
            return omDodge;
        case ltDarken:
            return omDarken;
        case ltLighten:
            return omLighten;
        case ltScreen:
            return omScreen;
        case ltAddAlpha:
            return omAddAlpha;
        case ltPsNormal:
            return omPsNormal;
        case ltPsAdditive:
            return omPsAdditive;
        case ltPsSubtractive:
            return omPsSubtractive;
        case ltPsMultiplicative:
            return omPsMultiplicative;
        case ltPsScreen:
            return omPsScreen;
        case ltPsOverlay:
            return omPsOverlay;
        case ltPsHardLight:
            return omPsHardLight;
        case ltPsSoftLight:
            return omPsSoftLight;
        case ltPsColorDodge:
            return omPsColorDodge;
        case ltPsColorDodge5:
            return omPsColorDodge5;
        case ltPsColorBurn:
            return omPsColorBurn;
        case ltPsLighten:
            return omPsLighten;
        case ltPsDarken:
            return omPsDarken;
        case ltPsDifference:
            return omPsDifference;
        case ltPsDifference5:
            return omPsDifference5;
        case ltPsExclusion:
            return omPsExclusion;

        default:
            return omOpaque;
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetType(tTVPLayerType type) {
    // set layer type to "type"
    if(Type != type) {
        // Keep the bitmap representation in sync with the layer face.  A
        // number of KiriKiri plug-ins build a layer as an ordinary alpha
        // layer, copy decoded pixels into it, and only then change its type
        // to ltAddAlpha.  In that sequence the old implementation changed
        // the blend mode but left straight-alpha RGB data in the texture;
        // transparent white pixels consequently became visible as opaque
        // white blocks.  Remember the old face before UpdateDrawFace() and
        // normalize only when the type transition really changes the pixel
        // convention.  This is deliberately layer-generic (not tied to a
        // particular motion or title resource).
        const tTVPDrawFace oldDrawFace = DrawFace;
        Type = type;
        switch(Type) {
            case ltBinder:
                NeutralColor = TransparentColor =
                    TVP_RGBA2COLOR(255, 255, 255, 0);
                DisplayType = Type;
                CanHaveImage = false;
                DeallocateImage();
                break;

            case ltOpaque: // formerly ltCoverRect
                NeutralColor = TransparentColor =
                    TVP_RGBA2COLOR(255, 255, 255, 0);
                DisplayType = Type;
                CanHaveImage = true;
                AllocateImage();
                break;

            case ltAlpha: // formerly ltTransparent
                NeutralColor = TransparentColor =
                    TVP_RGBA2COLOR(255, 255, 255, 0);
                DisplayType = Type;
                CanHaveImage = true;
                AllocateImage();
                break;

            case ltAdditive:
                NeutralColor = TransparentColor = TVP_RGBA2COLOR(0, 0, 0, 0);
                DisplayType = Type;
                CanHaveImage = true;
                AllocateImage();
                break;

            case ltSubtractive:
                NeutralColor = TransparentColor =
                    TVP_RGBA2COLOR(255, 255, 255, 0);
                DisplayType = Type;
                CanHaveImage = true;
                AllocateImage();
                break;

            case ltMultiplicative:
                NeutralColor = TransparentColor =
                    TVP_RGBA2COLOR(255, 255, 255, 0);
                DisplayType = Type;
                CanHaveImage = true;
                AllocateImage();
                break;

            case ltEffect:
                NeutralColor = TransparentColor =
                    TVP_RGBA2COLOR(255, 255, 255, 0);
                DisplayType = ltBinder; // TODO: retrieve actual DrawType
                CanHaveImage = false;
                DeallocateImage();
                break;

            case ltFilter:
                NeutralColor = TransparentColor =
                    TVP_RGBA2COLOR(255, 255, 255, 0);
                DisplayType = ltBinder; // TODO: retrieve actual DisplayType
                CanHaveImage = false;
                DeallocateImage();
                break;

            case ltDodge:
                NeutralColor = TransparentColor = TVP_RGBA2COLOR(0, 0, 0, 0);
                DisplayType = Type;
                CanHaveImage = true;
                AllocateImage();
                break;

            case ltDarken:
                NeutralColor = TransparentColor =
                    TVP_RGBA2COLOR(255, 255, 255, 0);
                DisplayType = Type;
                CanHaveImage = true;
                AllocateImage();
                break;

            case ltLighten:
                NeutralColor = TransparentColor = TVP_RGBA2COLOR(0, 0, 0, 0);
                DisplayType = Type;
                CanHaveImage = true;
                AllocateImage();
                break;

            case ltScreen:
                NeutralColor = TransparentColor = TVP_RGBA2COLOR(0, 0, 0, 0);
                DisplayType = Type;
                CanHaveImage = true;
                AllocateImage();
                break;

            case ltAddAlpha:
                NeutralColor = TransparentColor = TVP_RGBA2COLOR(0, 0, 0, 0);
                DisplayType = Type;
                CanHaveImage = true;
                AllocateImage();
                break;

            case ltPsNormal:
                NeutralColor = TransparentColor = TVP_RGBA2COLOR(0, 0, 0, 0);
                DisplayType = Type;
                CanHaveImage = true;
                AllocateImage();
                break;

            case ltPsAdditive:
                NeutralColor = TransparentColor = TVP_RGBA2COLOR(0, 0, 0, 0);
                DisplayType = Type;
                CanHaveImage = true;
                AllocateImage();
                break;

            case ltPsSubtractive:
                NeutralColor = TransparentColor =
                    TVP_RGBA2COLOR(255, 255, 255, 0);
                DisplayType = Type;
                CanHaveImage = true;
                AllocateImage();
                break;

            case ltPsMultiplicative:
                NeutralColor = TransparentColor =
                    TVP_RGBA2COLOR(255, 255, 255, 0);
                DisplayType = Type;
                CanHaveImage = true;
                AllocateImage();
                break;

            case ltPsScreen:
                NeutralColor = TransparentColor = TVP_RGBA2COLOR(0, 0, 0, 0);
                DisplayType = Type;
                CanHaveImage = true;
                AllocateImage();
                break;

            case ltPsOverlay:
                NeutralColor = TransparentColor =
                    TVP_RGBA2COLOR(128, 128, 128, 0);
                DisplayType = Type;
                CanHaveImage = true;
                AllocateImage();
                break;

            case ltPsHardLight:
                NeutralColor = TransparentColor =
                    TVP_RGBA2COLOR(128, 128, 128, 0);
                DisplayType = Type;
                CanHaveImage = true;
                AllocateImage();
                break;

            case ltPsSoftLight:
                NeutralColor = TransparentColor =
                    TVP_RGBA2COLOR(128, 128, 128, 0);
                DisplayType = Type;
                CanHaveImage = true;
                AllocateImage();
                break;

            case ltPsColorDodge:
                NeutralColor = TransparentColor = TVP_RGBA2COLOR(0, 0, 0, 0);
                DisplayType = Type;
                CanHaveImage = true;
                AllocateImage();
                break;

            case ltPsColorDodge5:
                NeutralColor = TransparentColor = TVP_RGBA2COLOR(0, 0, 0, 0);
                DisplayType = Type;
                CanHaveImage = true;
                AllocateImage();
                break;

            case ltPsColorBurn:
                NeutralColor = TransparentColor =
                    TVP_RGBA2COLOR(255, 255, 255, 0);
                DisplayType = Type;
                CanHaveImage = true;
                AllocateImage();
                break;

            case ltPsLighten:
                NeutralColor = TransparentColor = TVP_RGBA2COLOR(0, 0, 0, 0);
                DisplayType = Type;
                CanHaveImage = true;
                AllocateImage();
                break;

            case ltPsDarken:
                NeutralColor = TransparentColor =
                    TVP_RGBA2COLOR(255, 255, 255, 0);
                DisplayType = Type;
                CanHaveImage = true;
                AllocateImage();
                break;

            case ltPsDifference:
                NeutralColor = TransparentColor = TVP_RGBA2COLOR(0, 0, 0, 0);
                DisplayType = Type;
                CanHaveImage = true;
                AllocateImage();
                break;

            case ltPsDifference5:
                NeutralColor = TransparentColor = TVP_RGBA2COLOR(0, 0, 0, 0);
                DisplayType = Type;
                CanHaveImage = true;
                AllocateImage();
                break;

            case ltPsExclusion:
                NeutralColor = TransparentColor = TVP_RGBA2COLOR(0, 0, 0, 0);
                DisplayType = Type;
                CanHaveImage = true;
                AllocateImage();
                break;
        }
        NotifyLayerTypeChange();

        if(!TVPDisableAutomaticAlphaNormalization() && MainImage &&
           oldDrawFace != DrawFace) {
            if(DrawFace == dfAddAlpha && oldDrawFace == dfAlpha) {
                if(const char *trace = std::getenv("AETHERKIRI_ALPHA_TRACE");
                   trace && *trace && *trace != '0') {
                    spdlog::info("alpha type-convert name={} old={} new={} type={} image={}x{}",
                                 GetName().AsStdString(),
                                 TVPLayerDebugDrawFaceName(oldDrawFace),
                                 TVPLayerDebugDrawFaceName(DrawFace),
                                 ttstr(GetTypeNameString()).AsStdString(),
                                 GetImageWidth(),
                                 GetImageHeight());
                }
                MainImage->ConvertAlphaToAddAlpha();
                ImageModified = true;
                if(const char *trace = std::getenv("AETHERKIRI_ALPHA_TRACE");
                   trace && *trace && *trace != '0' && MainImage) {
                    spdlog::info("alpha type-convert sample name={} p00=0x{:08x} center=0x{:08x} pbr=0x{:08x}",
                                 GetName().AsStdString(),
                                 MainImage->GetPoint(0, 0),
                                 MainImage->GetPoint(
                                     static_cast<tjs_int>(GetImageWidth() / 2),
                                     static_cast<tjs_int>(GetImageHeight() / 2)),
                                 MainImage->GetPoint(
                                     static_cast<tjs_int>(GetImageWidth() - 1),
                                     static_cast<tjs_int>(GetImageHeight() - 1)));
                }
            } else if(DrawFace == dfAlpha && oldDrawFace == dfAddAlpha) {
                if(const char *trace = std::getenv("AETHERKIRI_ALPHA_TRACE");
                   trace && *trace && *trace != '0') {
                    spdlog::info("alpha type-convert name={} old={} new={} type={} image={}x{}",
                                 GetName().AsStdString(),
                                 TVPLayerDebugDrawFaceName(oldDrawFace),
                                 TVPLayerDebugDrawFaceName(DrawFace),
                                 ttstr(GetTypeNameString()).AsStdString(),
                                 GetImageWidth(),
                                 GetImageHeight());
                }
                MainImage->ConvertAddAlphaToAlpha();
                ImageModified = true;
            }
        }
        SetToCreateExposedRegion();
        Update();
    }
}

//---------------------------------------------------------------------------
const tjs_char *tTJSNI_BaseLayer::GetTypeNameString() {
    switch(Type) {
        case ltBinder:
            return TJS_W("ltBinder");
        case ltOpaque:
            return TJS_W("ltOpaque");
        case ltAlpha:
            return TJS_W("ltAlpha");
        case ltAdditive:
            return TJS_W("ltAdditive");
        case ltSubtractive:
            return TJS_W("ltSubtractive");
        case ltMultiplicative:
            return TJS_W("ltMultiplicative");
        case ltEffect:
            return TJS_W("ltEffect");
        case ltFilter:
            return TJS_W("ltFilter");
        case ltDodge:
            return TJS_W("ltDodge");
        case ltDarken:
            return TJS_W("ltDarken");
        case ltLighten:
            return TJS_W("ltLighten");
        case ltScreen:
            return TJS_W("ltScreen");
        case ltAddAlpha:
            return TJS_W("ltAddAlpha");
        case ltPsNormal:
            return TJS_W("PsNormal");
        case ltPsAdditive:
            return TJS_W("PsAdditive");
        case ltPsSubtractive:
            return TJS_W("PsSubtractive");
        case ltPsMultiplicative:
            return TJS_W("PsMultiplicative");
        case ltPsScreen:
            return TJS_W("PsScreen");
        case ltPsOverlay:
            return TJS_W("PsOverlay");
        case ltPsHardLight:
            return TJS_W("PsHardLight");
        case ltPsSoftLight:
            return TJS_W("PsSoftLight");
        case ltPsColorDodge:
            return TJS_W("PsColorDodge");
        case ltPsColorDodge5:
            return TJS_W("PsColorDodge5");
        case ltPsColorBurn:
            return TJS_W("PsColorBurn");
        case ltPsLighten:
            return TJS_W("PsLighten");
        case ltPsDarken:
            return TJS_W("PsDarken");
        case ltPsDifference:
            return TJS_W("PsDifference");
        case ltPsDifference5:
            return TJS_W("PsDifference5");
        case ltPsExclusion:
            return TJS_W("PsExclusion");

        default:
            return TJS_W("unknown");
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::ConvertLayerType(tTVPDrawFace fromtype) {
    // convert layer pixel representation method

    if(DrawFace == dfAddAlpha && fromtype == dfAlpha) {
        // alpha -> additive alpha
        if(MainImage)
            MainImage->ConvertAlphaToAddAlpha();
    } else if(DrawFace == dfAlpha && fromtype == dfAddAlpha) {
        // additive alpha -> alpha
        // this may loose additive stuff
        if(MainImage)
            MainImage->ConvertAddAlphaToAlpha();
    } else {
        // throw an error
        TVPThrowExceptionMessage(TVPCannotConvertLayerTypeUsingGivenDirection);
    }

    ImageModified = true;

    Update();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// geographical management
//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::CreateExposedRegion() {
    // create exposed/overlapped region information

    // find region which is not piled by any children
    ExposedRegion.Clear();
    OverlappedRegion.Clear();

    tTVPRect rect;
    rect.left = rect.top = 0;
    rect.right = Rect.get_width();
    rect.bottom = Rect.get_height();

    if(MainImage != nullptr) {
        // the layer has image

        if(GetVisibleChildrenCount() > TVP_EXPOSED_UNITE_LIMIT) {
            ExposedRegion.Or(rect);

            bool first = true;

            tTVPRect r2;

            TVP_LAYER_FOR_EACH_CHILD_NOLOCK_BEGIN(child)

            if(child->IsSeen()) {
                tTVPRect r(child->GetRect());
                if(TVPIntersectRect(&r, r, rect)) {
                    if(first) {
                        r2 = child->GetRect();
                        first = false;
                    } else {
                        TVPUnionRect(&r2, r2, r);
                    }
                }
            }

            TVP_LAYER_FOR_EACH_CHILD_NOLOCK_END

            OverlappedRegion.Or(r2);

            ExposedRegion.Sub(OverlappedRegion);
        } else {
            tTVPRect rect;
            rect.left = rect.top = 0;
            rect.right = Rect.get_width();
            rect.bottom = Rect.get_height();
            ExposedRegion.Or(rect);

            TVP_LAYER_FOR_EACH_CHILD_NOLOCK_BEGIN(child)

            if(child->IsSeen()) {
                tTVPRect r(child->GetRect());
                if(TVPIntersectRect(&r, r, rect))
                    OverlappedRegion.Or(r);
            }

            TVP_LAYER_FOR_EACH_CHILD_NOLOCK_END

            ExposedRegion.Sub(OverlappedRegion);
        }
    } else {
        // the layer has no image
        // ExposedRegion : child layer can directly transfer the image
        // to the parent's target OverlappedRegion : Inverse of
        // ExposedRegion

        ExposedRegion.Clear();
        OverlappedRegion.Clear();
        OverlappedRegion.Or(rect);

        // ExposedRegion is a region with is only one child layer
        // piled under the parent layer. Recalculating this is pretty
        // high-cost operation,
        if(GetVisibleChildrenCount() < TVP_DIRECT_UNITE_LIMIT) {
            tTVPComplexRect &one = ExposedRegion; // alias of ExposedRegion
            tTVPComplexRect two; // region which is more than two layers piled

            TVP_LAYER_FOR_EACH_CHILD_NOLOCK_BEGIN(child)

            if(child->IsSeen()) {
                tTVPRect r(child->GetRect());
                if(child->DisplayType == this->DisplayType &&
                   child->Opacity == 255) {
                    tTVPComplexRect one_and_r(one);
                    one_and_r.And(r);
                    tTVPComplexRect two_and_r(two);
                    two_and_r.And(r);
                    one.Sub(one_and_r);
                    two.Or(one_and_r);
                    two.Or(two_and_r);
                    tTVPComplexRect tmp;
                    tmp.Or(r);
                    tmp.Sub(one_and_r);
                    tmp.Sub(two_and_r);
                    one.Or(tmp);
                } else {
                    two.Or(r);
                    one.Sub(r);
                }
            }

            TVP_LAYER_FOR_EACH_CHILD_NOLOCK_END
        }

        OverlappedRegion.Sub(ExposedRegion);
    }

    ExposedRegionValid = true;
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::InternalSetSize(tjs_uint width, tjs_uint height) {
    if(Rect.get_width() != (tjs_int)width ||
       Rect.get_height() != (tjs_int)height) {
        Update(false);
        Rect.set_width(width);
        Rect.set_height(height);
        if(Parent)
            Parent->NotifyChildrenVisualStateChanged();
        SetToCreateExposedRegion();
        ImageLayerSizeChanged();
        Update(false);
        if(IsPrimary() && Manager)
            Manager->NotifyLayerResize();
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::InternalSetBounds(const tTVPRect &rect) {
    tjs_int width = rect.right - rect.left;
    tjs_int height = rect.bottom - rect.top;
    if(width < 0 || height < 0)
        TVPThrowExceptionMessage(TVPInvalidParam);

    if(Rect.left != rect.left || Rect.top != rect.top) {
        bool visible = GetVisible() || GetNodeVisible();
        if(IsPrimary() && (rect.left != 0 || rect.top != 0))
            TVPThrowExceptionMessage(TVPCannotMovePrimary);

        if(visible)
            ParentUpdate();
        Rect.set_offsets(rect.left, rect.top);
        if(Parent)
            Parent->NotifyChildrenVisualStateChanged();
        SetToCreateExposedRegion();
        if(visible)
            ParentUpdate();
    }

    InternalSetSize(width, height);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetLeft(tjs_int left) {
    if(Rect.left != left) {
        bool visible = GetVisible() || GetNodeVisible();
        if(IsPrimary() && left != 0)
            TVPThrowExceptionMessage(TVPCannotMovePrimary);
        if(visible)
            ParentUpdate();
        tjs_int w;
        w = Rect.get_width();
        Rect.left = left;
        Rect.right = w + Rect.left;
        if(Parent)
            Parent->NotifyChildrenVisualStateChanged();
        // TODO: SetLeft
        if(visible)
            ParentUpdate();
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetTop(tjs_int top) {
    if(Rect.top != top) {
        bool visible = GetVisible() || GetNodeVisible();
        if(IsPrimary() && top != 0)
            TVPThrowExceptionMessage(TVPCannotMovePrimary);
        if(visible)
            ParentUpdate();
        tjs_int h;
        h = Rect.get_height();
        Rect.top = top;
        Rect.bottom = h + Rect.top;
        if(Parent)
            Parent->NotifyChildrenVisualStateChanged();
        // TODO: SetTop;
        if(visible)
            ParentUpdate();
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetPosition(tjs_int left, tjs_int top) {
    if(Rect.left != left || Rect.top != top) {
        bool visible = GetVisible() || GetNodeVisible();
        if(IsPrimary() && (left != 0 || top != 0))
            TVPThrowExceptionMessage(TVPCannotMovePrimary);
        if(visible)
            ParentUpdate();
        Rect.set_offsets(left, top);
        if(Parent)
            Parent->NotifyChildrenVisualStateChanged();
        // TODO: SetPosition
        if(visible)
            ParentUpdate();
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetWidth(tjs_uint width) {
    if(Rect.get_width() != (tjs_int)width) {
        Update(false);
        Rect.set_width(width);
        if(Parent)
            Parent->NotifyChildrenVisualStateChanged();
        SetToCreateExposedRegion();
        ImageLayerSizeChanged();
        Update(false);
        if(IsPrimary() && Manager)
            Manager->NotifyLayerResize();
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetHeight(tjs_uint height) {
    if(Rect.get_height() != (tjs_int)height) {
        Update(false);
        Rect.set_height(height);
        if(Parent)
            Parent->NotifyChildrenVisualStateChanged();
        SetToCreateExposedRegion();
        ImageLayerSizeChanged();
        Update(false);
        if(IsPrimary() && Manager)
            Manager->NotifyLayerResize();
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetSize(tjs_uint width, tjs_uint height) {
    InternalSetSize(width, height);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetBounds(const tTVPRect &rect) {
    // TODO: SetBounds
    InternalSetBounds(rect);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::ParentRectToChildRect(tTVPRect &rect) {
    // note that this function does not convert transformed layer
    // coordinates.
    rect.left -= Rect.left;
    rect.right -= Rect.left;
    rect.top -= Rect.top;
    rect.bottom -= Rect.top;
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::ToPrimaryCoordinates(tjs_int &x, tjs_int &y) const {
    const tTJSNI_BaseLayer *l = this;

    while(l && !l->IsPrimary()) {
        x += l->Rect.left;
        y += l->Rect.top;
        l = l->Parent;
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::FromPrimaryCoordinates(tjs_int &x, tjs_int &y) const {
    const tTJSNI_BaseLayer *l = this;

    while(l && !l->IsPrimary()) {
        x -= l->Rect.left;
        y -= l->Rect.top;
        l = l->Parent;
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::FromPrimaryCoordinates(tjs_real &x, tjs_real &y) const {
    const tTJSNI_BaseLayer *l = this;

    while(l && !l->IsPrimary()) {
        x -= (tjs_real)l->Rect.left;
        y -= (tjs_real)l->Rect.top;
        l = l->Parent;
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// image buffer management
//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::ChangeImageSize(tjs_uint width, tjs_uint height) {
    // be called from geographical management
    if(!width || !height) {
        auto logger = spdlog::get("core");
        if(logger) logger->warn("ChangeImageSize: ignoring zero dimension {}x{}", width, height);
        return;
    }

    int64_t oldBytes = TVPCalcMainImageBytes(MainImage);
    if(MainImage)
        MainImage->SetSizeWithFill(width, height, NeutralColor);
    TVPLayerBitmapTotalBytes.fetch_add(TVPCalcMainImageBytes(MainImage) - oldBytes,
                                       std::memory_order_relaxed);
    if(ProvinceImage)
        ProvinceImage->SetSizeWithFill(width, height, 0);

    if(MainImage)
        ResetClip(); // cliprect is reset

    ImageModified = true;

    ResizeCache(); // in cache management
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::AllocateImage() {
    if(!MainImage) {
        ImageLeft = 0;
        ImageTop = 0;
        MainImage = new tTVPBaseTexture(Rect.get_width(), Rect.get_height());
        MainImage->Fill(tTVPRect(0, 0, Rect.get_width(), Rect.get_height()),
                        NeutralColor);
        MainImage->SetFont(Font); // set font
        TVPLayerBitmapTotalBytes.fetch_add(TVPCalcMainImageBytes(MainImage),
                                           std::memory_order_relaxed);
    }

    if(MainImage)
        ResetClip(); // cliprect is reset

    if(ProvinceImage) {
        ProvinceImage->SetSizeWithFill(MainImage->GetWidth(),
                                       MainImage->GetHeight(), 0);
    }

    FontChanged = true; // invalidate font assignment cache
    ImageModified = true;
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::DeallocateImage() {
    if(MainImage) {
        TVPLayerBitmapTotalBytes.fetch_sub(TVPCalcMainImageBytes(MainImage),
                                           std::memory_order_relaxed);
        delete MainImage;
        MainImage = nullptr;
    }
    if(ProvinceImage)
        delete ProvinceImage, ProvinceImage = nullptr;

    ImageModified = true;
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::EnsureBitmap() {
    if(!_bitmapEvicted) return;
    AllocateImage();
    if(!_evictedImageName.IsEmpty() && MainImage) {
        ttstr provincename;
        iTJSDispatch2 *metainfo = nullptr;
        if(!TVPLayerLoadThumbnailFitted(MainImage, _evictedImageName,
                                        _evictedColorKey, &provincename,
                                        &metainfo)) {
            TVPLoadGraphic(MainImage, _evictedImageName, _evictedColorKey,
                           0, 0, glmNormal, nullptr, nullptr);
        }
        if(metainfo) {
            metainfo->Release();
        }
    }
    _bitmapEvicted = false;
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::AllocateProvinceImage() {
    tjs_uint neww = MainImage ? MainImage->GetWidth() : Rect.get_width();
    tjs_uint newh = MainImage ? MainImage->GetHeight() : Rect.get_height();

    if(!ProvinceImage) {
        ProvinceImage = new tTVPBaseBitmap(neww, newh, 8);
        ProvinceImage->Fill(tTVPRect(0, 0, neww, newh), 0);
    } else {
        ProvinceImage->SetSizeWithFill(neww, newh, 0);
    }

    ImageModified = true;
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::DeallocateProvinceImage() {
    if(ProvinceImage)
        delete ProvinceImage, ProvinceImage = nullptr;
    ImageModified = true;
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::AllocateDefaultImage() {
    int64_t oldBytes = TVPCalcMainImageBytes(MainImage);
    if(!MainImage)
        MainImage = new tTVPBaseTexture(*TVPTempBitmapHolder->Get());
    else
        MainImage->Assign(*TVPTempBitmapHolder->Get());
    TVPLayerBitmapTotalBytes.fetch_add(TVPCalcMainImageBytes(MainImage) - oldBytes,
                                       std::memory_order_relaxed);

    FontChanged = true; // invalidate font assignment cache
    ResetClip(); // cliprect is reset

    ImageModified = true;
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::AssignImages(tTJSNI_BaseLayer *src) {
    const bool assignmentTrace =
        TVPLayerTransitionTraceEnabled() && src &&
        (GetWidth() >= 512 || GetHeight() >= 512 ||
         src->GetWidth() >= 512 || src->GetHeight() >= 512);
    if(TVPShouldDeferKagTransitionMotionAssignment(this, src)) {
        if(assignmentTrace) {
            spdlog::info(
                "LayerAssign route=defer-transition kind=images target={} source={}",
                static_cast<const void *>(this), static_cast<const void *>(src));
        }
        return;
    }

    // KAG's EnvGraphicLayer.copyImage contract flushes a pending onPaint on
    // the source before copying its image.  Hidden fore/back pages do not
    // otherwise participate in window completion, so copying immediately can
    // snapshot the still-transparent allocation instead of the queued affine
    // image.  Completing just the pending source here mirrors the script
    // contract and applies to vector, generated-colour and other affine
    // sources without depending on scene or layer names.
    if(src && src != this && src->GetCallOnPaint()) {
        TVPTraceStage2Lifecycle("assign-images-flush-paint-before", src);
        src->BeforeCompletion();
        TVPTraceStage2Lifecycle("assign-images-flush-paint-after", src);
    }
    if(TVPStage2TraceEnabled() &&
       (TVPStage2Related(this) || TVPStage2Related(src))) {
        TVPTraceStage2Lifecycle(
            "assign-images-before", this,
            fmt::format("source={} source_visible={} source_image={} source_p00=0x{:08x}",
                        src ? src->GetName().AsStdString() : "<null>",
                        src && src->GetVisible() ? "yes" : "no",
                        src && src->GetMainImage() ? "yes" : "no",
                        TVPStage2Sample(src, 0, 0)));
        if(src && src != this)
            TVPTraceStage2Lifecycle("assign-images-source", src);
    }

    if(assignmentTrace) {
        auto *targetParent = GetParent();
        auto *sourceParent = src->GetParent();
        spdlog::info(
            "LayerAssign enter kind=images target={} targetName={} targetParent={} targetParentName={} targetVisible={} targetParentVisible={} targetOpacity={} targetOrder={} targetOverallOrder={} targetPos=({}, {}) targetSize={}x{} targetImagePos=({}, {}) targetMainImage={} targetTexture={} source={} sourceName={} sourceParent={} sourceParentName={} sourceVisible={} sourceParentVisible={} sourceOpacity={} sourceOrder={} sourceOverallOrder={} sourcePos=({}, {}) sourceSize={}x{} sourceImagePos=({}, {}) sourceMainImage={} sourceTexture={}",
            static_cast<const void *>(this), GetName().AsStdString(),
            static_cast<const void *>(targetParent),
            targetParent ? targetParent->GetName().AsStdString()
                         : std::string("<none>"),
            GetVisible() ? 1 : 0, GetParentVisible() ? 1 : 0,
            GetOpacity(), GetOrderIndex(), GetOverallOrderIndex(), GetLeft(),
            GetTop(), GetWidth(), GetHeight(), GetImageLeft(), GetImageTop(),
            static_cast<const void *>(MainImage),
            MainImage ? static_cast<const void *>(MainImage->GetTexture())
                      : nullptr,
            static_cast<const void *>(src),
            src->GetName().AsStdString(), static_cast<const void *>(sourceParent),
            sourceParent ? sourceParent->GetName().AsStdString()
                         : std::string("<none>"),
            src->GetVisible() ? 1 : 0, src->GetParentVisible() ? 1 : 0,
            src->GetOpacity(), src->GetOrderIndex(),
            src->GetOverallOrderIndex(), src->GetLeft(), src->GetTop(),
            src->GetWidth(), src->GetHeight(), src->GetImageLeft(),
            src->GetImageTop(), static_cast<const void *>(src->MainImage),
            src->MainImage
                ? static_cast<const void *>(src->MainImage->GetTexture())
                : nullptr);
    }
    if(src && src != this && src->GetName().IsEmpty() &&
       !src->GetVisible()) {
        bool use_motion_swap =
            TVPIsKagTransitionMotionAssignment(this, src);
        {
            std::lock_guard<std::mutex> lock(
                TVPExchangedKagPageMutex);
            use_motion_swap = use_motion_swap ||
                TVPMotionSwapAssignmentTargets.find(this) !=
                TVPMotionSwapAssignmentTargets.end();
        }
        if(use_motion_swap) {
            if(assignmentTrace) {
                spdlog::info("LayerAssign route=motion-swap target={} source={}",
                             static_cast<const void *>(this),
                             static_cast<const void *>(src));
            }
            AssignMotionImages(src);
            return;
        }
    }
    if(auto *visible_target =
           TVPResolveExchangedKagAssignmentTarget(this, src)) {
        if(assignmentTrace) {
            spdlog::info(
                "LayerAssign route=exchanged-target target={} source={} resolved={}",
                static_cast<const void *>(this), static_cast<const void *>(src),
                static_cast<const void *>(visible_target));
        }
        if(visible_target != this) {
            visible_target->AssignImages(src);
            return;
        }
        AssignMotionImages(src);
        return;
    }
    if(TVPIsAffineSourceMotionScratch(this, src)) {
        if(assignmentTrace) {
            spdlog::info(
                "LayerAssign route=motion-scratch-swap target={} source={}",
                static_cast<const void *>(this),
                static_cast<const void *>(src));
        }
        AssignMotionImages(src);
        return;
    }
    // assign images
    bool main_changed = false;
    bool province_changed = false;
    bool shared_gpu_frame_updated = false;

    // KAG's temporaryLayer is normally named "syslay".  AffineSourceImage
    // uses it as a scratch surface for generated images (solid colours,
    // captures and text), then clears/reuses the surface for the next source.
    // AssignImages historically shared the texture object, which meant that a
    // target such as a transition/darken layer was silently cleared when the
    // scratch layer was reused.  Keep the normal sharing behaviour for
    // persistent layers and detach only this documented scratch surface.
    const auto is_temporary_source = [](const tTJSNI_BaseLayer *layer) {
        if(!layer) {
            return false;
        }
        const std::string name = layer->GetName().AsStdString();
        return name == "syslay" || name.rfind("syslay:", 0) == 0;
    };
    const bool copy_temporary_source = src != this && is_temporary_source(src);
    const bool assignTrace = [] {
        const char *value = std::getenv("AETHERKIRI_ASSIGN_TRACE");
        return value && *value && *value != '0';
    }();
    if(assignTrace) {
        static std::atomic<int> assignTraceCount{0};
        if(assignTraceCount.fetch_add(1, std::memory_order_relaxed) < 4000) {
            const auto parent_name = [](const tTJSNI_BaseLayer *layer) {
                return layer && layer->GetParent()
                    ? layer->GetParent()->GetName().AsStdString()
                    : std::string("<none>");
            };
            const auto sample = [](const tTJSNI_BaseLayer *layer,
                                   tjs_int x, tjs_int y) -> tjs_uint32 {
                if(!layer || !layer->MainImage || x < 0 || y < 0 ||
                   static_cast<tjs_uint>(x) >= layer->MainImage->GetWidth() ||
                   static_cast<tjs_uint>(y) >= layer->MainImage->GetHeight()) {
                    return 0;
                }
                return layer->MainImage->GetPoint(x, y);
            };
            const bool detail = [] {
                const char *value = std::getenv("AETHERKIRI_ASSIGN_TRACE_DETAIL");
                return value && *value && *value != '0';
            }();
            spdlog::info(
                "Layer.assign-images target={} source={} same={} targetImage={} sourceImage={} sourceSize={}x{}{}",
                GetName().AsStdString(), src ? src->GetName().AsStdString() : "<null>",
                src == this ? "yes" : "no", MainImage ? "yes" : "no",
                src && src->MainImage ? "yes" : "no",
                src && src->MainImage ? src->GetImageWidth() : 0,
                src && src->MainImage ? src->GetImageHeight() : 0,
                detail && src ? fmt::format(
                    " targetVisible={} targetParent={} targetOpacity={} "
                    "sourceVisible={} sourceParent={} sourceOpacity={} "
                    "targetTex={} sourceTex={} target00=0x{:08x} "
                    "source00=0x{:08x} sourceCenter=0x{:08x}",
                    GetVisible() ? "yes" : "no", parent_name(this),
                    GetOpacity(), src->GetVisible() ? "yes" : "no",
                    parent_name(src), src->GetOpacity(),
                    static_cast<const void *>(MainImage ? MainImage->GetTexture() : nullptr),
                    static_cast<const void *>(src->MainImage ? src->MainImage->GetTexture() : nullptr),
                    sample(this, 0, 0), sample(src, 0, 0),
                    sample(src, static_cast<tjs_int>(src->MainImage->GetWidth() / 2),
                          static_cast<tjs_int>(src->MainImage->GetHeight() / 2)))
                    : std::string());
        }
    }

    const bool alphaTrace = [] {
        const char *value = std::getenv("AETHERKIRI_ALPHA_TRACE");
        return value && *value && *value != '0';
    }();

    if(src->MainImage) {
        const bool convertToAddAlpha = DrawFace == dfAddAlpha &&
            src->DrawFace != dfAddAlpha;
        const bool convertToAlpha = DrawFace == dfAlpha &&
            src->DrawFace == dfAddAlpha;
        if(alphaTrace && (GetName() == TJS_W("truss") ||
                          GetName() == TJS_W("frame") ||
                          src->GetName() == TJS_W("truss") ||
                          src->GetName() == TJS_W("frame") ||
                          src->GetWidth() > 1000)) {
            spdlog::info("alpha assign target={} source={} targetFace={} sourceFace={} targetSize={}x{} sourceSize={}x{} convertAdd={} convertAlpha={}",
                         GetName().AsStdString(), src->GetName().AsStdString(),
                         TVPLayerDebugDrawFaceName(DrawFace),
                         TVPLayerDebugDrawFaceName(src->DrawFace),
                         GetImageWidth(), GetImageHeight(), src->GetImageWidth(),
                         src->GetImageHeight(), convertToAddAlpha,
                         convertToAlpha);
        }
        int64_t oldBytes = TVPCalcMainImageBytes(MainImage);
        if(MainImage)
            main_changed = MainImage->Assign(*src->MainImage);
        else {
            MainImage = new tTVPBaseTexture(*src->MainImage);
            main_changed = true;
        }

        if(copy_temporary_source && MainImage &&
           MainImage->GetTexture() == src->MainImage->GetTexture()) {
            // Independ() performs a GPU-side copy when the texture is shared;
            // IndependNoCopy() would lose the generated pixels and is not
            // appropriate here.
            MainImage->Independ();
            main_changed = true;
            if(assignTrace) {
                spdlog::info(
                    "Layer.assign-images detached scratch source={} target={} size={}x{}",
                    src->GetName().AsStdString(), GetName().AsStdString(),
                    MainImage->GetWidth(), MainImage->GetHeight());
            }
        }
        TVPLayerBitmapTotalBytes.fetch_add(TVPCalcMainImageBytes(MainImage) - oldBytes,
                                           std::memory_order_relaxed);
        if(main_changed)
            FontChanged = true; // invalidate font assignment cache

        // Assign() deliberately shares the source texture. A GPU-rendered
        // scratch layer can therefore keep the same texture object while its
        // pixels change every frame. Treat that as a new image frame even
        // though Assign() correctly reports that the object did not change.
        if(MainImage && MainImage->GetTexture() == src->MainImage->GetTexture()) {
            if(auto *texture =
                   dynamic_cast<GodotTexture2D *>(MainImage->GetTexture())) {
                shared_gpu_frame_updated = texture->HasPendingGpuWrites();
            }
        }

        // KiriKiri's ltAddAlpha layers store premultiplied (additive-alpha)
        // pixels.  A plain Layer/ltBinder source still contains ordinary
        // straight-alpha pixels, even when the destination is an
        // ltAddAlpha layer.  Sharing that texture without converting it makes
        // transparent white pixels contribute as opaque white in the
        // additive blend path (notably in motion/foreground compositions).
        // Convert after Assign(); GetTextureForRender() detaches a shared
        // texture before modifying it, so the source layer remains intact.
        if(!TVPDisableAutomaticAlphaNormalization() && MainImage &&
           convertToAddAlpha) {
            MainImage->ConvertAlphaToAddAlpha();
        } else if(MainImage && convertToAlpha) {
            MainImage->ConvertAddAlphaToAlpha();
        }
    } else if(MainImage) {
        DeallocateImage();
        main_changed = true;
    }

    if(src->ProvinceImage) {
        if(ProvinceImage)
            province_changed = ProvinceImage->Assign(*src->ProvinceImage);
        else {
            ProvinceImage = new tTVPBaseBitmap(*src->ProvinceImage);
            province_changed = true;
        }
    } else if(ProvinceImage) {
        DeallocateProvinceImage();
        province_changed = true;
    }

    if(!main_changed && !province_changed && !shared_gpu_frame_updated)
        return;

    if(main_changed && MainImage) {
        InternalSetImageSize(MainImage->GetWidth(), MainImage->GetHeight());
        // adjust position
    }

    ImageModified = true;

    if(main_changed && MainImage)
        ResetClip(); // cliprect is reset

    if(main_changed || shared_gpu_frame_updated)
        Update(false); // update
    TVPTraceStage2Lifecycle("assign-images-after", this);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::AssignMotionImages(tTJSNI_BaseLayer *src) {
    if(!src || src == this) {
        return;
    }
    const bool assignmentProfile = layerCompletionProfileEnabled();
    const auto assignmentStarted =
        assignmentProfile ? std::chrono::steady_clock::now()
                          : std::chrono::steady_clock::time_point{};
    double prepareMs = 0.0;
    double detachMs = 0.0;
    double swapMs = 0.0;
    double provinceMs = 0.0;
    double geometryMs = 0.0;
    double updateMs = 0.0;
    bool detachedSharedTexture = false;
    const bool assignmentTrace =
        TVPLayerTransitionTraceEnabled() &&
        (GetWidth() >= 512 || GetHeight() >= 512 ||
         src->GetWidth() >= 512 || src->GetHeight() >= 512);
    if(assignmentTrace) {
        auto *targetParent = GetParent();
        auto *sourceParent = src->GetParent();
        spdlog::info(
            "LayerAssign enter kind=motion target={} targetName={} targetParent={} targetParentName={} targetVisible={} targetParentVisible={} targetOrder={} source={} sourceName={} sourceParent={} sourceParentName={} sourceVisible={} sourceParentVisible={} sourceOrder={} targetImage={}x{} sourceImage={}x{}",
            static_cast<const void *>(this), GetName().AsStdString(),
            static_cast<const void *>(targetParent),
            targetParent ? targetParent->GetName().AsStdString()
                         : std::string("<none>"),
            GetVisible() ? 1 : 0, GetParentVisible() ? 1 : 0,
            GetOrderIndex(), static_cast<const void *>(src),
            src->GetName().AsStdString(), static_cast<const void *>(sourceParent),
            sourceParent ? sourceParent->GetName().AsStdString()
                         : std::string("<none>"),
            src->GetVisible() ? 1 : 0, src->GetParentVisible() ? 1 : 0,
            src->GetOrderIndex(), GetImageWidth(), GetImageHeight(),
            src->GetImageWidth(), src->GetImageHeight());
    }
    if(TVPShouldDeferKagTransitionMotionAssignment(this, src)) {
        if(assignmentTrace) {
            spdlog::info(
                "LayerAssign route=defer-transition kind=motion target={} source={}",
                static_cast<const void *>(this), static_cast<const void *>(src));
        }
        return;
    }
    if(auto *visible_target =
           TVPResolveExchangedKagAssignmentTarget(this, src)) {
        if(assignmentTrace) {
            spdlog::info(
                "LayerAssign route=exchanged-target kind=motion target={} source={} resolved={}",
                static_cast<const void *>(this), static_cast<const void *>(src),
                static_cast<const void *>(visible_target));
        }
        if(visible_target != this) {
            visible_target->AssignMotionImages(src);
            return;
        }
    }
    if(!src->MainImage) {
        AssignImages(src);
        return;
    }

    // D3DEmote renders each character into one full-window scratch layer and
    // immediately hands that frame to the character layer. AssignImages()
    // shares the texture, so clearing the scratch for the next character can
    // overwrite the preceding one. Swap the completed texture into the
    // destination instead: the destination's old texture becomes an
    // independent, already-sized buffer for the next scratch render.
    if(!MainImage) {
        const auto prepareStarted = std::chrono::steady_clock::now();
        AllocateDefaultImage();
        prepareMs += std::chrono::duration<double, std::milli>(
                         std::chrono::steady_clock::now() - prepareStarted)
                         .count();
    }

    // A character layer may have gone through the legacy AssignImages path
    // before it was recognized as an Emote scratch hand-off.  That path keeps
    // separate bitmap wrappers but aliases their underlying GPU texture.  A
    // pointer swap alone cannot break that alias: the next render into the
    // scratch layer would still overwrite the character that just received
    // the frame.  Detach the destination first, then swap the rendered
    // scratch texture into it.
    if(MainImage && src->MainImage &&
       MainImage->GetTexture() == src->MainImage->GetTexture()) {
        const auto detachStarted = std::chrono::steady_clock::now();
        detachedSharedTexture = true;
        auto *oldImage = MainImage;
        const auto width = std::max<tjs_uint>(1, oldImage->GetWidth());
        const auto height = std::max<tjs_uint>(1, oldImage->GetHeight());
        MainImage = new tTVPBaseTexture(width, height);
        MainImage->Fill(tTVPRect(0, 0, width, height), NeutralColor);
        MainImage->SetFont(Font);
        TVPLayerBitmapTotalBytes.fetch_add(
            TVPCalcMainImageBytes(MainImage) -
                TVPCalcMainImageBytes(oldImage),
            std::memory_order_relaxed);
        // In the normal case oldImage is owned only by this layer.  If a
        // malformed/legacy path made both layers point at the same wrapper,
        // leave it alive for src and only replace this layer's reference.
        if(oldImage != src->MainImage) {
            delete oldImage;
        }
        if(assignmentTrace) {
            spdlog::info(
                "LayerAssign route=motion-scratch-detach-shared-texture target={} source={} texture={}",
                static_cast<const void *>(this),
                static_cast<const void *>(src),
                static_cast<const void *>(src->MainImage->GetTexture()));
        }
        detachMs = std::chrono::duration<double, std::milli>(
                       std::chrono::steady_clock::now() - detachStarted)
                       .count();
    }
    const auto swapStarted = std::chrono::steady_clock::now();
    std::swap(MainImage, src->MainImage);
    FontChanged = true;
    src->FontChanged = true;
    swapMs = std::chrono::duration<double, std::milli>(
                 std::chrono::steady_clock::now() - swapStarted)
                 .count();

    // Some games create the visible E-mote destination as an otherwise
    // untouched Layer, whose engine default geometry is 32x32.  Swapping in
    // the full-stage bitmap changes the image size but InternalSetImageSize()
    // intentionally never enlarges the layer rectangle, so the character is
    // clipped to that tiny placeholder.  Grow only this recognizable E-mote
    // scratch hand-off; authored layers that intentionally crop their image
    // keep their existing geometry.
    const auto completedWidth = MainImage->GetWidth();
    const auto completedHeight = MainImage->GetHeight();
    if(TVPIsAffineSourceMotionScratch(this, src) && GetWidth() == 32 &&
       GetHeight() == 32 &&
       (completedWidth != GetWidth() || completedHeight != GetHeight())) {
        if(assignmentTrace) {
            spdlog::info(
                "LayerAssign route=motion-scratch-resize-target target={} source={} from=32x32 to={}x{}",
                static_cast<const void *>(this),
                static_cast<const void *>(src), completedWidth,
                completedHeight);
        }
        SetSize(completedWidth, completedHeight);
    }

    const auto provinceStarted = std::chrono::steady_clock::now();
    if(src->ProvinceImage) {
        if(ProvinceImage)
            ProvinceImage->Assign(*src->ProvinceImage);
        else {
            ProvinceImage = new tTVPBaseBitmap(*src->ProvinceImage);
        }
    } else if(ProvinceImage) {
        DeallocateProvinceImage();
    }
    provinceMs = std::chrono::duration<double, std::milli>(
                     std::chrono::steady_clock::now() - provinceStarted)
                     .count();

    const auto geometryStarted = std::chrono::steady_clock::now();
    InternalSetImageSize(completedWidth, completedHeight);
    ResetClip();
    if(src->MainImage) {
        src->InternalSetImageSize(src->MainImage->GetWidth(),
                                  src->MainImage->GetHeight());
        src->ResetClip();
    }

    ImageModified = true;
    src->ImageModified = true;
    geometryMs = std::chrono::duration<double, std::milli>(
                     std::chrono::steady_clock::now() - geometryStarted)
                     .count();
    const auto updateStarted = std::chrono::steady_clock::now();
    Update(false);
    updateMs = std::chrono::duration<double, std::milli>(
                   std::chrono::steady_clock::now() - updateStarted)
                   .count();

    if(assignmentProfile) {
        const double totalMs = std::chrono::duration<double, std::milli>(
                                   std::chrono::steady_clock::now() -
                                   assignmentStarted)
                                   .count();
        const double threshold = motionAssignmentProfileSlowMs();
        if(totalMs >= threshold || prepareMs >= threshold ||
           detachMs >= threshold || provinceMs >= threshold ||
           geometryMs >= threshold || updateMs >= threshold) {
            if(auto logger = spdlog::get("core")) {
                logger->info(
                    "motion assign profile: total_ms={:.3f} "
                    "prepare_ms={:.3f} detach_ms={:.3f} swap_ms={:.3f} "
                    "province_ms={:.3f} geometry_ms={:.3f} "
                    "update_ms={:.3f} detached={} target={} "
                    "target_name={} source={} source_name={} size={}x{}",
                    totalMs, prepareMs, detachMs, swapMs, provinceMs,
                    geometryMs, updateMs, detachedSharedTexture ? 1 : 0,
                    static_cast<const void *>(this), GetName().AsStdString(),
                    static_cast<const void *>(src),
                    src->GetName().AsStdString(), completedWidth,
                    completedHeight);
            }
        }
    }
}

//---------------------------------------------------------------------------
bool tTJSNI_BaseLayer::ExchangeMainImage(tTVPBaseTexture *&bitmap) {
    if(!bitmap || MainImage == bitmap)
        return false;
    if(!MainImage && _bitmapEvicted)
        EnsureBitmap();
    if(!MainImage || MainImage->GetWidth() != bitmap->GetWidth() ||
       MainImage->GetHeight() != bitmap->GetHeight() ||
       MainImage->GetBPP() != bitmap->GetBPP()) {
        return false;
    }

    // Swap bitmap objects instead of assigning the producer texture. The
    // producer can then reuse the returned display image without triggering
    // a full copy-on-write clone of the frame that is currently visible.
    std::swap(MainImage, bitmap);
    FontChanged = true;
    InternalSetImageSize(MainImage->GetWidth(), MainImage->GetHeight());
    ResetClip();
    ImageModified = true;
    Update(false);
    return true;
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::AssignMainImageWithUpdate(iTVPBaseBitmap *bmp) {
    TVPTraceStage2Lifecycle(
        "assign-main-before", this,
        fmt::format("source={}x{} p00=0x{:08x}",
                    bmp ? bmp->GetWidth() : 0, bmp ? bmp->GetHeight() : 0,
                    bmp ? bmp->GetPoint(0, 0) : 0));
    // assign images
    bool main_changed = true;

    if(bmp) {
        int64_t oldBytes = TVPCalcMainImageBytes(MainImage);
        if(MainImage)
            main_changed = MainImage->Assign(*bmp);
        else
            MainImage = new tTVPBaseTexture(*bmp);
        TVPLayerBitmapTotalBytes.fetch_add(TVPCalcMainImageBytes(MainImage) - oldBytes,
                                           std::memory_order_relaxed);
        FontChanged = true; // invalidate font assignment cache
    } else {
        DeallocateImage();
    }

    if(main_changed && MainImage) {
        InternalSetImageSize(MainImage->GetWidth(), MainImage->GetHeight());
        // adjust position
    }

    ImageModified = true;

    if(MainImage)
        ResetClip(); // cliprect is reset

    if(main_changed)
        Update(false); // update
    TVPTraceStage2Lifecycle("assign-main-after", this);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::AssignMainImage(iTVPBaseBitmap *bmp) {
    // assign single main bitmap image. the image size assigned must
    // be identical to the destination layer bitmap. destination
    // bitmap must have a layer bitmap

    if(!MainImage || bmp->GetWidth() != MainImage->GetWidth() ||
       bmp->GetHeight() != MainImage->GetHeight()) {
        // destination layer does not have a main image or
        // the size is not identical to the source layer bitmap
        TVPThrowInternalError;
    }

    bool main_changed = MainImage->Assign(*bmp);

    if(main_changed) {
        ImageModified = true;
        Update(false); // update
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::CopyFromMainImage(tTJSNI_Bitmap *bmp) {
    if(!MainImage && _bitmapEvicted) EnsureBitmap();
    if(!MainImage)
        TVPThrowExceptionMessage(TVPNotDrawableLayerType);
    bmp->CopyFrom(MainImage);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetHasImage(bool b) {
    if(!CanHaveImage && b)
        TVPThrowExceptionMessage(TVPLayerCannotHaveImage);
    if(b)
        AllocateImage();
    else
        DeallocateImage();
    NotifyChildrenVisualStateChanged();
    Update();
}

//---------------------------------------------------------------------------
bool tTJSNI_BaseLayer::GetHasImage() const { return MainImage != nullptr; }

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetImageLeft(tjs_int left) {
    if(!MainImage && _bitmapEvicted) EnsureBitmap();
    if(!MainImage)
        TVPThrowExceptionMessage(TVPNotDrawableLayerType);
    if(ImageLeft != left) {
        if(left > 0)
            TVPThrowExceptionMessage(TVPInvalidImagePosition);
        if((tjs_int)(MainImage->GetWidth()) + left < Rect.get_width())
            TVPThrowExceptionMessage(TVPInvalidImagePosition);
        ImageLeft = left;
        Update();
    }
}

//---------------------------------------------------------------------------
tjs_int tTJSNI_BaseLayer::GetImageLeft() const {
    if(!MainImage && !_bitmapEvicted)
        TVPThrowExceptionMessage(TVPNotDrawableLayerType);
    return ImageLeft;
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetImageTop(tjs_int top) {
    if(!MainImage && _bitmapEvicted) EnsureBitmap();
    if(!MainImage)
        TVPThrowExceptionMessage(TVPNotDrawableLayerType);
    if(ImageTop != top) {
        if(top > 0)
            TVPThrowExceptionMessage(TVPInvalidImagePosition);
        if((tjs_int)(MainImage->GetHeight()) + top < Rect.get_height())
            TVPThrowExceptionMessage(TVPInvalidImagePosition);
        ImageTop = top;
        Update();
    }
}

//---------------------------------------------------------------------------
tjs_int tTJSNI_BaseLayer::GetImageTop() const {
    if(!MainImage && !_bitmapEvicted)
        TVPThrowExceptionMessage(TVPNotDrawableLayerType);
    return ImageTop;
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetImagePosition(tjs_int left, tjs_int top) {
    if(!MainImage && _bitmapEvicted) EnsureBitmap();
    if(!MainImage)
        TVPThrowExceptionMessage(TVPNotDrawableLayerType);
    if(ImageLeft != left || ImageTop != top) {
        if(left > 0)
            TVPThrowExceptionMessage(TVPInvalidImagePosition);
        if(top > 0)
            TVPThrowExceptionMessage(TVPInvalidImagePosition);
        if((tjs_int)(MainImage->GetWidth()) + left < Rect.get_width())
            TVPThrowExceptionMessage(TVPInvalidImagePosition);
        if((tjs_int)(MainImage->GetHeight()) + top < Rect.get_height())
            TVPThrowExceptionMessage(TVPInvalidImagePosition);
        ImageLeft = left;
        ImageTop = top;
        Update();
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetImageWidth(tjs_uint width) {
    if(!MainImage && _bitmapEvicted) EnsureBitmap();
    if(!MainImage)
        TVPThrowExceptionMessage(TVPNotDrawableLayerType);

    if(width == MainImage->GetWidth())
        return;

    // adjust position
    if((tjs_int)width < Rect.get_width()) {
        ImageLeft = 0;
        SetWidth(width); // change layer size
    }

    if((tjs_int)(width) + ImageLeft < Rect.get_width()) {
        ImageLeft = Rect.get_width() - width;
    }

    // change image size...
    ChangeImageSize(width, MainImage->GetHeight());
}

//---------------------------------------------------------------------------
tjs_uint tTJSNI_BaseLayer::GetImageWidth() const {
    if(!MainImage) {
        if(_bitmapEvicted) return Rect.get_width();
        TVPThrowExceptionMessage(TVPNotDrawableLayerType);
    }
    return MainImage->GetWidth();
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetImageHeight(tjs_uint height) {
    if(!MainImage && _bitmapEvicted) EnsureBitmap();
    if(!MainImage)
        TVPThrowExceptionMessage(TVPNotDrawableLayerType);

    if(height == MainImage->GetHeight())
        return;

    // adjust position
    if((tjs_int)height < Rect.get_height()) {
        ImageTop = 0;
        SetHeight(height); // change layer size
    }

    if((tjs_int)(height + ImageTop) < Rect.get_height()) {
        ImageTop = Rect.get_height() - height;
    }

    // change image size...
    ChangeImageSize(MainImage->GetWidth(), height);
}

//---------------------------------------------------------------------------
tjs_uint tTJSNI_BaseLayer::GetImageHeight() const {
    if(!MainImage) {
        if(_bitmapEvicted) return Rect.get_height();
        TVPThrowExceptionMessage(TVPNotDrawableLayerType);
    }
    return MainImage->GetHeight();
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::InternalSetImageSize(tjs_uint width, tjs_uint height) {
    // adjust position
    if((tjs_int)width < Rect.get_width()) {
        ImageLeft = 0;
        SetWidth(width); // change layer size
    }
    if((tjs_int)(width + ImageLeft) < Rect.get_width()) {
        ImageLeft = Rect.get_width() - width;
    }

    if((tjs_int)height < Rect.get_height()) {
        ImageTop = 0;
        SetHeight(height); // change layer size
    }
    if((tjs_int)(height + ImageTop) < Rect.get_height()) {
        ImageTop = Rect.get_height() - height;
    }

    ChangeImageSize(width, height);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetImageSize(tjs_uint width, tjs_uint height) {
    if(!MainImage && _bitmapEvicted) EnsureBitmap();
    if(!MainImage)
        TVPThrowExceptionMessage(TVPNotDrawableLayerType);

    if(width == MainImage->GetWidth() && height == MainImage->GetHeight())
        return;

    InternalSetImageSize(width, height);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::ImageLayerSizeChanged() {
    // called from geographical management
    if(!MainImage)
        return;

    if((tjs_int)MainImage->GetWidth() < Rect.get_width()) {
        ChangeImageSize(Rect.get_width(), MainImage->GetHeight());
    }
    if((tjs_int)(MainImage->GetWidth() + ImageLeft) < Rect.get_width()) {
        ImageLeft = Rect.get_width() - MainImage->GetWidth();
        Update();
    }

    if((tjs_int)MainImage->GetHeight() < Rect.get_height()) {
        ChangeImageSize(MainImage->GetWidth(), Rect.get_height());
    }
    if((tjs_int)(MainImage->GetHeight() + ImageTop) < Rect.get_height()) {
        ImageTop = Rect.get_height() - MainImage->GetHeight();
        Update();
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::IndependMainImage(bool copy) {
    if(MainImage) {
        if(copy)
            MainImage->Independ();
        else
            MainImage->IndependNoCopy();
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::IndependProvinceImage(bool copy) {
    if(ProvinceImage) {
        if(copy)
            ProvinceImage->Independ();
        else
            ProvinceImage->IndependNoCopy();
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SaveLayerImage(const ttstr &name, const ttstr &type) {
    if(!MainImage && _bitmapEvicted) EnsureBitmap();
    if(!MainImage)
        TVPThrowExceptionMessage(TVPNotDrawableLayerType);

    iTJSDispatch2 *dic = TJSCreateDictionaryObject();
    try {
        tTJSVariant val;
        switch(Type) {
            case ltOpaque:
                val = tTJSVariant(TJS_W("opaque"));
                break;
            case ltAlpha:
                val = tTJSVariant(TJS_W("alpha"));
                break;
            case ltAdditive:
                val = tTJSVariant(TJS_W("add"));
                break;
            case ltSubtractive:
                val = tTJSVariant(TJS_W("sub"));
                break;
            case ltMultiplicative:
                val = tTJSVariant(TJS_W("mul"));
                break;
            case ltDodge:
                val = tTJSVariant(TJS_W("dodge"));
                break;
            case ltDarken:
                val = tTJSVariant(TJS_W("darken"));
                break;
            case ltLighten:
                val = tTJSVariant(TJS_W("lighten"));
                break;
            case ltScreen:
                val = tTJSVariant(TJS_W("screen"));
                break;
            case ltAddAlpha:
                val = tTJSVariant(TJS_W("addalpha"));
                break;
            case ltPsNormal:
                val = tTJSVariant(TJS_W("psnormal"));
                break;
            case ltPsAdditive:
                val = tTJSVariant(TJS_W("psadd"));
                break;
            case ltPsSubtractive:
                val = tTJSVariant(TJS_W("pssub"));
                break;
            case ltPsMultiplicative:
                val = tTJSVariant(TJS_W("psmul"));
                break;
            case ltPsScreen:
                val = tTJSVariant(TJS_W("psscreen"));
                break;
            case ltPsOverlay:
                val = tTJSVariant(TJS_W("psoverlay"));
                break;
            case ltPsHardLight:
                val = tTJSVariant(TJS_W("pshlight"));
                break;
            case ltPsSoftLight:
                val = tTJSVariant(TJS_W("psslight"));
                break;
            case ltPsColorDodge:
                val = tTJSVariant(TJS_W("psdodge"));
                break;
            case ltPsColorDodge5:
                val = tTJSVariant(TJS_W("psdodge5"));
                break;
            case ltPsColorBurn:
                val = tTJSVariant(TJS_W("psburn"));
                break;
            case ltPsLighten:
                val = tTJSVariant(TJS_W("pslighten"));
                break;
            case ltPsDarken:
                val = tTJSVariant(TJS_W("psdarken"));
                break;
            case ltPsDifference:
                val = tTJSVariant(TJS_W("psdiff"));
                break;
            case ltPsDifference5:
                val = tTJSVariant(TJS_W("psdiff5"));
                break;
            case ltPsExclusion:
                val = tTJSVariant(TJS_W("psexcl"));
                break;
            default:
                val = tTJSVariant(TJS_W("opaque"));
                break;
        }
        dic->PropSet(TJS_MEMBERENSURE, TJS_W("mode"), 0, &val, dic);

        if(ImageLeft > 0) {
            val = tTJSVariant(ImageLeft);
            dic->PropSet(TJS_MEMBERENSURE, TJS_W("offs_x"), 0, &val, dic);
        }
        if(ImageTop > 0) {
            val = tTJSVariant(ImageTop);
            dic->PropSet(TJS_MEMBERENSURE, TJS_W("offs_y"), 0, &val, dic);
        }
        if(ImageLeft > 0 || ImageTop > 0) {
            val = tTJSVariant(TJS_W("pixel"));
            dic->PropSet(TJS_MEMBERENSURE, TJS_W("offs_unit"), 0, &val, dic);
        }
        TVPSaveImage(name, type, MainImage, dic);
    } catch(...) {
        dic->Release();
        throw;
    }
    dic->Release();
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::AssignTexture(iTVPTexture2D *tex) {
    if(!MainImage && _bitmapEvicted) EnsureBitmap();
    if(!MainImage)
        TVPThrowExceptionMessage(TVPNotDrawableLayerType);
    int64_t oldBytes = TVPCalcMainImageBytes(MainImage);
    MainImage->AssignTexture(tex);
    TVPLayerBitmapTotalBytes.fetch_add(TVPCalcMainImageBytes(MainImage) - oldBytes,
                                       std::memory_order_relaxed);
    InternalSetImageSize(MainImage->GetWidth(), MainImage->GetHeight());
    ImageModified = true;
    ResetClip(); // cliprect is reset
    Update(false);
}

//---------------------------------------------------------------------------
iTJSDispatch2 *tTJSNI_BaseLayer::LoadImages(const ttstr &name,
                                            tjs_uint32 colorkey) {
    TVPTraceStage2Lifecycle("load-images-before", this,
                            fmt::format("name={} colorkey=0x{:08x}",
                                        name.AsStdString(), colorkey));
    // loads image(s) from specified storage.
    // colorkey must be a color that should be transparent, or:
    // 0x 01 ff ff ff (clAdapt) : the color key will be automatically
    // chosen from
    //                            target image, by selecting most used
    //                            color from the top line of the
    //                            image.
    // 0x 1f ff ff ff (clNone)  : does not apply the colorkey, or uses
    // image alpha
    //                            channel.
    // 0x30000000 (clPalIdx) + nn ( nn = palette index )
    //                          : select the color key by specified
    //                          palette index.
    // 0x40000000 (TVP_clAlphaMat) + 0xRRGGBB ( 0xRRGGBB = matting
    // color )
    //                          : do matting with the color using
    //                          alpha blending.
    // returns graphic image metainfo.

    if(_bitmapEvicted) {
        AllocateImage();
        _bitmapEvicted = false;
    }

    if(!MainImage)
        TVPThrowExceptionMessage(TVPNotDrawableLayerType);

    ttstr provincename;
    iTJSDispatch2 *metainfo = nullptr;

    int64_t oldBytes = TVPCalcMainImageBytes(MainImage);
    ttstr load_name = name;
    ttstr load_ext = TVPExtractStorageExt(load_name);
    load_ext.ToLowerCase();
    if(load_ext == TJS_W(".dref")) {
        ttstr fallback = TVPChopStorageExt(load_name) + TJS_W(".png");
        if(TVPIsExistentStorage(fallback)) {
            spdlog::warn("Layer.loadImages dref fallback: {} -> {}",
                         load_name.AsStdString(), fallback.AsStdString());
            load_name = fallback;
        }
    }

    auto load_graphic = [&](const ttstr &storage_name) {
        if(!TVPLayerLoadThumbnailFitted(MainImage, storage_name, colorkey,
                                        &provincename, &metainfo)) {
            TVPLoadGraphic(MainImage, storage_name, colorkey, 0, 0, glmNormal,
                           &provincename, &metainfo);
        }
    };

    try {
        load_graphic(load_name);
    } catch(...) {
        ttstr ext = TVPExtractStorageExt(name);
        ext.ToLowerCase();

        // `getExistImageName` in several UI script revisions returns the
        // language-qualified probe even when only the base image is shipped.
        // Retry that base name before applying the older extension/virtual
        // pack fallbacks below.  This is especially important for gallery
        // thumbnails, where an exception aborts the whole tile draw.
        ttstr language_fallback;
        bool language_recovered = false;
        if(TVPLayerStripLanguageVariant(name, language_fallback) &&
           TVPIsExistentStorage(language_fallback)) {
            try {
                load_name = language_fallback;
                load_graphic(language_fallback);
                language_recovered = true;
                if(TVPLayerDebugEnabled() && TVPLayerDebugTake())
                    spdlog::info("Layer.loadImages language fallback: {} -> {}",
                                 name.AsStdString(),
                                 language_fallback.AsStdString());
            } catch(...) {
                // Preserve the established fallback path if the base image
                // exists in the index but cannot be decoded.
                load_name = name;
            }
        }

        if(!language_recovered) {
            ttstr fallback = ext.IsEmpty() || ext == TJS_W(".dref")
                ? (ext.IsEmpty() ? name : TVPChopStorageExt(name)) +
                    TJS_W(".png")
                : name;
            if(!TVPIsExistentStorage(fallback)) {
                if(!TVPLayerUseVirtualPackPlaceholder(name))
                    throw;

                // Some native UI compilers publish logical atlases through a
                // plug-in-owned virtual resource map instead of a physical
                // file. Give the registered provider the first chance to
                // materialize that image. If no provider recognizes it,
                // retain the safe transparent placeholder used for
                // unsupported virtual packs.
                if(TVPProvideVirtualGraphic(name, MainImage)) {
                    spdlog::info("Layer.loadImages materialized virtual "
                                 "graphic: {} size={}x{}",
                                 name.AsStdString(),
                                 static_cast<unsigned>(MainImage->GetWidth()),
                                 static_cast<unsigned>(MainImage->GetHeight()));
                    load_name = name;
                    if(metainfo) {
                        metainfo->Release();
                        metainfo = nullptr;
                    }
                    provincename.Clear();
                } else {

                    // A virtual atlas has no concrete fallback to inspect.
                    // Preserve any dimensions already assigned by the UI
                    // loader; otherwise allocate a bounded transparent canvas
                    // large enough for authored slice coordinates.
                    const tjs_uint existing_width = MainImage->GetWidth();
                    const tjs_uint existing_height = MainImage->GetHeight();
                    const tjs_uint placeholder_width =
                        existing_width <= 32 ? 2048 : existing_width;
                    const tjs_uint placeholder_height =
                        existing_height <= 32 ? 2048 : existing_height;
                    spdlog::warn(
                        "Layer.loadImages transparent placeholder for missing "
                        "virtual pack: {} existing={}x{} size={}x{}",
                        name.AsStdString(),
                        static_cast<unsigned>(existing_width),
                        static_cast<unsigned>(existing_height),
                        static_cast<unsigned>(placeholder_width),
                        static_cast<unsigned>(placeholder_height));
                    MainImage->SetSize(placeholder_width, placeholder_height,
                                       false);
                    MainImage->Fill(
                        tTVPRect(0, 0, placeholder_width, placeholder_height),
                        0x00000000);
                    load_name = name;
                    if(metainfo) {
                        metainfo->Release();
                        metainfo = nullptr;
                    }
                    provincename.Clear();
                }
            } else {
                try {
                    load_name = fallback;
                    load_graphic(fallback);
                } catch(...) {
                    spdlog::warn(
                        "Layer.loadImages placeholder for unreadable image: {}",
                        fallback.AsStdString());
                    MainImage->SetSize(1, 1, false);
                    MainImage->Fill(tTVPRect(0, 0, 1, 1), 0x00000000);
                }
            }
            if(fallback != name && TVPIsExistentStorage(fallback)) {
                spdlog::warn("Layer.loadImages fallback: {} -> {}",
                             name.AsStdString(), fallback.AsStdString());
            }
        }
    }
    TVPLayerBitmapTotalBytes.fetch_add(TVPCalcMainImageBytes(MainImage) - oldBytes,
                                       std::memory_order_relaxed);

    // Images decoded by the storage/PSB loaders are straight-alpha.  When a
    // caller explicitly loads into an ltAddAlpha layer, normalize the pixel
    // representation before the first draw.  This is the same conversion
    // used by ConvertLayerType() and keeps transparent RGB data from leaking
    // into additive blending.
    if(!TVPDisableAutomaticAlphaNormalization() && MainImage &&
       DrawFace == dfAddAlpha) {
        MainImage->ConvertAlphaToAddAlpha();
    }

    // One-shot asset inspection for the generic PSB slice path.  This is
    // intentionally opt-in and is removed after the renderer diagnosis; it
    // lets us distinguish an opaque source asset from a lost-alpha blit.
    if(const char *dump = std::getenv("AETHERKIRI_DUMP_MONO_SLICES");
       dump && *dump && *dump != '0' &&
       name.AsStdString().find("psb://motion/mono_loop.mtn/") == 0) {
        static std::atomic<int> dump_index{0};
        const int index = dump_index.fetch_add(1, std::memory_order_relaxed);
        if(index < 4) {
            try {
                const std::string path =
                    "/tmp/aetherkiri-mono-slice-" + std::to_string(index) +
                    ".png";
                TVPSaveImage(ttstr{path}, TJS_W("png"), MainImage, nullptr);
                spdlog::info("dumped mono slice {} to {}", name.AsStdString(),
                             path);
            } catch(...) {
                spdlog::warn("failed dumping mono slice {}",
                             name.AsStdString());
            }
        }
    }

    if(const char *dump = std::getenv("AETHERKIRI_DUMP_IMAGE_LOADS");
       dump && *dump && *dump != '0' && MainImage) {
        const std::string wanted = dump;
        const std::string source = name.AsStdString();
        if(source.find(wanted) != std::string::npos) {
            static std::unordered_set<std::string> dumped_names;
            if(dumped_names.insert(source).second) {
                std::string label = source;
                std::replace(label.begin(), label.end(), '/', '_');
                std::replace(label.begin(), label.end(), '\\', '_');
                try {
                    TVPSaveImage(ttstr{"/tmp/aetherkiri-load-" + label + ".png"},
                                 TJS_W("png"), MainImage, nullptr);
                    spdlog::info("dumped loaded image {}", source);
                } catch(...) {
                    spdlog::warn("failed dumping loaded image {}", source);
                }
            }
        }
    }

    _evictedImageName = name;
    _evictedColorKey = colorkey;
    try {

        InternalSetImageSize(MainImage->GetWidth(), MainImage->GetHeight());
        if(TVPLayerLoadTraceEnabled() || TVPLayerDebugTake() ||
           TVPLayerDebugNameLooksThumbnail(name) ||
           TVPLayerDebugNameLooksThumbnail(load_name)) {
            spdlog::info(
                "Layer.loadImages name={} loaded={} layer={} size={}x{} colorkey=0x{:08x}",
                name.AsStdString(), load_name.AsStdString(),
                GetName().AsStdString(),
                static_cast<int>(MainImage->GetWidth()),
                static_cast<int>(MainImage->GetHeight()),
                static_cast<unsigned int>(colorkey));
        }
        if(!provincename.IsEmpty()) {
            // province image exists
            AllocateProvinceImage();

            try {
                TVPLoadGraphicProvince(ProvinceImage, provincename, 0,
                                       MainImage->GetWidth(),
                                       MainImage->GetHeight());

                if(ProvinceImage->GetWidth() != MainImage->GetWidth() ||
                   ProvinceImage->GetHeight() != MainImage->GetHeight())
                    TVPThrowExceptionMessage(TVPProvinceSizeMismatch,
                                             provincename);
            } catch(...) {
                DeallocateProvinceImage();
                throw;
            }
        } else {
            // province image does not exist
            DeallocateProvinceImage();
        }

        ImageModified = true;

        ResetClip(); // cliprect is reset

        Update(false);
    } catch(...) {
        if(metainfo)
            metainfo->Release();
        throw;
    }

    TVPTraceStage2Lifecycle(
        "load-images-after", this,
        fmt::format("name={} loaded={} size={}x{} p00=0x{:08x} center=0x{:08x}",
                    name.AsStdString(), load_name.AsStdString(),
                    MainImage ? MainImage->GetWidth() : 0,
                    MainImage ? MainImage->GetHeight() : 0,
                    TVPStage2Sample(this, 0, 0),
                    TVPStage2Sample(
                        this,
                        MainImage ? static_cast<tjs_int>(MainImage->GetWidth() / 2)
                                  : 0,
                        MainImage ? static_cast<tjs_int>(MainImage->GetHeight() / 2)
                                  : 0)));
    return metainfo;
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::LoadProvinceImage(const ttstr &name) {
    // load an image as a province image

    if(!MainImage)
        TVPThrowExceptionMessage(TVPNotDrawableLayerType);

    AllocateProvinceImage();

    try {
        TVPLoadGraphicProvince(ProvinceImage, name, 0, MainImage->GetWidth(),
                               MainImage->GetHeight());

        if(ProvinceImage->GetWidth() != MainImage->GetWidth() ||
           ProvinceImage->GetHeight() != MainImage->GetHeight())
            TVPThrowExceptionMessage(TVPProvinceSizeMismatch, name);
    } catch(...) {
        DeallocateProvinceImage();
        throw;
    }

    ImageModified = true;
}

//---------------------------------------------------------------------------
tjs_uint32 tTJSNI_BaseLayer::GetMainPixel(tjs_int x, tjs_int y) {
    if(!MainImage && _bitmapEvicted) EnsureBitmap();
    if(!MainImage)
        TVPThrowExceptionMessage(TVPNotDrawableLayerType);

    return TVPFromActualColor(MainImage->GetPoint(x, y) & 0xffffff);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetMainPixel(tjs_int x, tjs_int y, tjs_uint32 color) {
    if(!MainImage && _bitmapEvicted) EnsureBitmap();
    if(!MainImage)
        TVPThrowExceptionMessage(TVPNotDrawableLayerType);

    if(x < ClipRect.left || y < ClipRect.top || x >= ClipRect.right ||
       y >= ClipRect.bottom)
        return; // out of clipping rectangle

    MainImage->SetPointMain(x, y, TVPToActualColor(color));

    ImageModified = true;
    tTVPRect r;
    r.left = ImageLeft + x;
    r.top = ImageTop + y;
    r.right = r.left + 1;
    r.bottom = r.top + 1;
    Update(r);
}

//---------------------------------------------------------------------------
tjs_int tTJSNI_BaseLayer::GetMaskPixel(tjs_int x, tjs_int y) {
    if(!MainImage && _bitmapEvicted) EnsureBitmap();
    if(!MainImage)
        TVPThrowExceptionMessage(TVPNotDrawableLayerType);

    return (MainImage->GetPoint(x, y) & 0xff000000) >> 24;
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetMaskPixel(tjs_int x, tjs_int y, tjs_int mask) {
    if(!MainImage && _bitmapEvicted) EnsureBitmap();
    if(!MainImage)
        TVPThrowExceptionMessage(TVPNotDrawableLayerType);

    if(x < ClipRect.left || y < ClipRect.top || x >= ClipRect.right ||
       y >= ClipRect.bottom)
        return; // out of clipping rectangle

    MainImage->SetPointMask(x, y, mask);

    ImageModified = true;
    tTVPRect r;
    r.left = ImageLeft + x;
    r.top = ImageTop + y;
    r.right = r.left + 1;
    r.bottom = r.top + 1;
    Update(r);
}

//---------------------------------------------------------------------------
tjs_int tTJSNI_BaseLayer::GetProvincePixel(tjs_int x, tjs_int y) const {
    if(!ProvinceImage)
        return 0;

    if(x < 0 || y < 0 || x >= (tjs_int)ProvinceImage->GetWidth() ||
       y >= (tjs_int)ProvinceImage->GetHeight())
        return 0;

    return ProvinceImage->GetPoint(x, y);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetProvincePixel(tjs_int x, tjs_int y, tjs_int n) {
    if(!ProvinceImage)
        AllocateProvinceImage();

    if(x < ClipRect.left || y < ClipRect.top || x >= ClipRect.right ||
       y >= ClipRect.bottom)
        return; // out of clipping rectangle

    ProvinceImage->SetPoint(x, y, n);

    ImageModified = true;
    tTVPRect r;
    r.left = ImageLeft + x;
    r.top = ImageTop + y;
    r.right = r.left + 1;
    r.bottom = r.top + 1;
    Update(r);
}

//---------------------------------------------------------------------------
const void *tTJSNI_BaseLayer::GetMainImagePixelBuffer() const {
    if(!MainImage)
        return nullptr;
    return MainImage->GetScanLine(0);
}

//---------------------------------------------------------------------------
void *tTJSNI_BaseLayer::GetMainImagePixelBufferForWrite() {
    if(!MainImage)
        return nullptr;
    ImageModified = true;
    return MainImage->GetScanLineForWrite(0);
}

//---------------------------------------------------------------------------
tjs_int tTJSNI_BaseLayer::GetMainImagePixelBufferPitch() const {
    if(!MainImage)
        return 0;
    return MainImage->GetPitchBytes();
}

//---------------------------------------------------------------------------
const void *tTJSNI_BaseLayer::GetProvinceImagePixelBuffer() const {
    if(!ProvinceImage)
        return nullptr;
    return ProvinceImage->GetScanLine(0);
}

//---------------------------------------------------------------------------
void *tTJSNI_BaseLayer::GetProvinceImagePixelBufferForWrite() {
    if(!ProvinceImage)
        AllocateProvinceImage();
    ImageModified = true;
    return ProvinceImage->GetScanLineForWrite(0);
}

//---------------------------------------------------------------------------
tjs_int tTJSNI_BaseLayer::GetProvinceImagePixelBufferPitch() const {
    if(!ProvinceImage)
        return 0;
    return ProvinceImage->GetPitchBytes();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// input event / hit test management
//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetCursorByStorage(const ttstr &storage) {
    Cursor = TVPGetCursor(storage);
    if(Manager)
        Manager->NotifyMouseCursorChange(this, GetLayerActiveCursor());
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetCursorByNumber(tjs_int num) {
    Cursor = num;
    if(Manager)
        Manager->NotifyMouseCursorChange(this, GetLayerActiveCursor());
}

//---------------------------------------------------------------------------
tjs_int tTJSNI_BaseLayer::GetLayerActiveCursor() {
    // return layer's actual (active) mouse cursor
    tjs_int cursor = Cursor;
    tTJSNI_BaseLayer *p = this;
    while(cursor ==
          0) // while cursor is 0 ( crDefault ) .. look up parent layer
    {
        p = p->Parent;
        if(!p)
            break;
        cursor = p->Cursor;
    }
    return cursor;
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetCurrentCursorToWindow() {
    // set current layer cusor to the window
    if(Manager) {
        Manager->SetMouseCursor(GetLayerActiveCursor());
    }
}

//---------------------------------------------------------------------------
tjs_int tTJSNI_BaseLayer::GetCursorX() {
    tjs_int x, y;
    if(!Manager)
        return 0;
    Manager->GetCursorPos(x, y);

    tTJSNI_BaseLayer *p = this;
    while(p) {
        if(!p->Parent)
            break;
        x -= p->Rect.left;
        p = p->Parent;
    }

    return x;
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetCursorX(tjs_int x) {
    CursorX_Work = x; // once store to this variable;
                      // cursor moves on call to SetCursorY
}

//---------------------------------------------------------------------------
tjs_int tTJSNI_BaseLayer::GetCursorY() {
    tjs_int x, y;
    if(!Manager)
        return 0;
    Manager->GetCursorPos(x, y);

    tTJSNI_BaseLayer *p = this;
    while(p) {
        if(!p->Parent)
            break;
        y -= p->Rect.top;
        p = p->Parent;
    }

    return y;
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetCursorY(tjs_int y) {
    if(!Manager)
        return;

    tjs_int x = CursorX_Work;
    tTJSNI_BaseLayer *p = this;
    while(p) {
        if(!p->Parent)
            break;
        x += p->Rect.left;
        y += p->Rect.top;
        p = p->Parent;
    }

    Manager->SetCursorPos(x, y);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetCursorPos(tjs_int x, tjs_int y) {
    if(!Manager)
        return;

    tTJSNI_BaseLayer *p = this;
    while(p) {
        if(!p->Parent)
            break;
        x += p->Rect.left;
        y += p->Rect.top;
        p = p->Parent;
    }

    Manager->SetCursorPos(x, y);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetCurrentHintToWindow() {
    // set current hint to the window
    if(IgnoreHintSensing)
        return;
    if(Manager) {
        tTJSNI_BaseLayer *p = this;
        while(p->ShowParentHint) {
            if(!p->Parent)
                break;
            p = p->Parent;
        }

        Manager->SetHint(GetOwnerNoAddRef(), p->Hint);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetHint(const ttstr &hint) {
    ShowParentHint = false;
    IgnoreHintSensing = false;
    Hint = hint;
    if(Manager)
        Manager->NotifyHintChange(this, hint);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetAttentionLeft(tjs_int l) {
    AttentionLeft = l;
    if(Manager)
        Manager->NotifyAttentionStateChanged(this);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetAttentionTop(tjs_int t) {
    AttentionTop = t;
    if(Manager)
        Manager->NotifyAttentionStateChanged(this);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetAttentionPoint(tjs_int l, tjs_int t) {
    AttentionLeft = l;
    AttentionTop = t;
    if(Manager)
        Manager->NotifyAttentionStateChanged(this);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetUseAttention(bool b) {
    UseAttention = b;
    if(Manager)
        Manager->NotifyAttentionStateChanged(this);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetImeMode(tTVPImeMode mode) {
    ImeMode = mode;
    if(Manager)
        Manager->NotifyImeModeChanged(this);
}

//---------------------------------------------------------------------------
bool tTJSNI_BaseLayer::_HitTestNoVisibleCheck(tjs_int x, tjs_int y) {
    // do hit test.
    // this function does not check layer's visiblity

    if(HitType == htMask) {
        // use mask
        if(MainImage) {
            tjs_int px = x - ImageLeft, py = y - ImageTop;
            if(px >= 0 && py >= 0 && px < (tjs_int)MainImage->GetWidth() &&
               py < (tjs_int)MainImage->GetHeight()) {
                if(HitThreshold <= 0)
                    return true;

                // Alpha is an 8-bit value, so thresholds above 255 can never
                // hit.  Motion layers use 256 to deliberately pass pointer
                // events through; avoid synchronously reading a GPU texture
                // just to reach the same result.
                if(HitThreshold > 255)
                    return false;

                // A texture whose alpha metadata is known to be fully opaque
                // satisfies every remaining 8-bit mask threshold.  In the
                // native GPU backend this avoids downloading a full static
                // background merely to answer one pointer hit test.
                if(MainImage->IsOpaque())
                    return true;

                tjs_uint32 cl = MainImage->GetPoint(px, py);
                if((tjs_int)(cl >> 24) < HitThreshold)
                    return false;
                else
                    return true;
            } else {
                return false;
            }
        } else {
            // layer has no image
            // all pixels are treated as 0 alpha value
            if(x >= 0 && y >= 0 && x < Rect.get_width() &&
               y < Rect.get_height()) {
                if(0 < HitThreshold)
                    return false;
                else
                    return true;
            }
            return false;
        }
    } else if(HitType == htProvince) {
        // use province

        if(ProvinceImage) {
            tjs_int px = x - ImageLeft, py = y - ImageTop;
            if(px >= 0 && py >= 0 && px < (tjs_int)ProvinceImage->GetWidth() &&
               py < (tjs_int)ProvinceImage->GetHeight()) {
                tjs_uint32 cl = ProvinceImage->GetPoint(px, py);
                if(cl == 0)
                    return false;
                else
                    return true;
            } else {
                return false;
            }
        } else {
            return false;
        }
    }

    return true;
}

//---------------------------------------------------------------------------
bool tTJSNI_BaseLayer::HitTestNoVisibleCheck(tjs_int x, tjs_int y) {
    bool res = _HitTestNoVisibleCheck(x, y);

    if(res) {
        // call onHitTest to perform additional hittest
        if(Owner && !Shutdown) {
            OnHitTest_Work = true;

            tTJSVariant param[3];
            param[0] = x;
            param[1] = y;
            param[2] = true;
            static ttstr eventname(TJS_W("onHitTest"));
            TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 3,
                         param);

            res = OnHitTest_Work;
        } else {
            return res;
        }
    }

    return res;
}

//---------------------------------------------------------------------------
bool tTJSNI_BaseLayer::GetMostFrontChildAt(tjs_int x, tjs_int y,
                                           tTJSNI_BaseLayer **lay,
                                           const tTJSNI_BaseLayer *except,
                                           bool get_disabled) {
    // internal function

    // visible check
    if(!Visible)
        return false; // cannot hit invisible layer

    // convert coordinates ( the point is given by parent's
    // coordinates )
    x -= Rect.left;
    y -= Rect.top;

    // rectangle test
    if(x < 0 || y < 0 || x >= Rect.get_width() || y >= Rect.get_height())
        return false; // out of the rectangle

    tjs_int ox = x, oy = y;

    { // locked
        tObjectListSafeLockHolder<tTJSNI_BaseLayer> holder(Children);
        tjs_int i = Children.GetSafeLockedObjectCount() - 1;
        for(; i >= 0; i--) {
            tTJSNI_BaseLayer *child = Children.GetSafeLockedObjectAt(i);
            if(!child)
                continue;
            bool b =
                child->GetMostFrontChildAt(x, y, lay, except, get_disabled);
            if(b)
                return true;
        }
    } // end locked

    if(except == this)
        return false; // exclusion

    if(HitTestNoVisibleCheck(ox, oy)) {
        if(!get_disabled && (!GetNodeEnabled() || IsDisabledByMode())) {
            *lay = nullptr;
            return true; // cannot hit disabled or under modal layer
        }
        *lay = this;
        return true;
    }

    return false;
}

//---------------------------------------------------------------------------
tTJSNI_BaseLayer *tTJSNI_BaseLayer::GetMostFrontChildAt(tjs_int x, tjs_int y,
                                                        bool exclude_self,
                                                        bool get_disabled) {
    // get most front layer at (x, y),
    // excluding self layer if "exclude_self" is true.

    if(!Manager)
        return nullptr;

    // convert to primary layer's coods
    tTJSNI_BaseLayer *p = this;
    while(p) {
        if(!p->Parent)
            break;
        x += p->Rect.left;
        y += p->Rect.top;
        p = p->Parent;
    }

    // call Manager->GetMostFrontChildAt
    return Manager->GetMostFrontChildAt(x, y, exclude_self ? this : nullptr,
                                        get_disabled);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::FireClick(tjs_int x, tjs_int y) {
    if(Owner && !Shutdown) {
        TVPLayerRecentEventSource = this;
        tTJSVariant param[2];
        param[0] = x;
        param[1] = y;
        static ttstr eventname(TJS_W("onClick"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 2, param);
    }
}

//---------------------------------------------------------------------------
static bool TVPHasLinkButtonProperties(const tTJSVariantClosure &object) {
    if(!object.Object)
        return false;

    static ttstr control_owner_name(TJS_W("controlOwner"));
    static ttstr link_num_name(TJS_W("linkNum"));
    tTJSVariant control_owner;
    tTJSVariant link_num;
    return TJS_SUCCEEDED(object.PropGet(
               0, control_owner_name.c_str(), control_owner_name.GetHint(),
               &control_owner, object.ObjThis)) &&
           control_owner.Type() == tvtObject &&
           TJS_SUCCEEDED(object.PropGet(0, link_num_name.c_str(),
                                        link_num_name.GetHint(), &link_num,
                                        object.ObjThis)) &&
           link_num.Type() != tvtVoid;
}

// UI page sheets keep the command for a copied button in the logical entry
// itself (usually current.names[<layer>].exp), rather than on the native
// layer wrapper.  Keep the lookup deliberately small and side-effect free;
// this is used by both hit-test routing and the generic onClick fallback.
static bool TVPReadExplicitLayerExpression(const tTJSVariantClosure &object,
                                           ttstr *expression = nullptr) {
    if(!object.Object)
        return false;

    static ttstr exp_name(TJS_W("exp"));
    tTJSVariant value;
    const tjs_error hr = object.PropGet(0, exp_name.c_str(), exp_name.GetHint(),
                                        &value, object.ObjThis);
    if(TJS_FAILED(hr) || value.Type() == tvtVoid)
        return false;
    try {
        const ttstr text(value);
        if(text.IsEmpty())
            return false;
        if(expression)
            *expression = text;
        return true;
    } catch(...) {
        return false;
    }
}

static bool TVPHasExplicitLayerExpression(const tTJSVariantClosure &object) {
    return TVPReadExplicitLayerExpression(object, nullptr);
}

// UI page sheets keep their logical button objects in `current.names`, while
// the hit-test layer is often owned by a small native button wrapper stored in
// the logical object's `button`/`layer` member.  The old resolver only
// accepted the logical object when it itself exposed controlOwner/linkNum,
// which made gallery and page-sheet entries receive the generic window
// onClick event instead of their link callback.  Follow the explicit wrapper
// properties used by the KAG UI contract, but keep the traversal bounded and
// side-effect free so arbitrary game objects are never walked.
static tTJSVariantClosure
TVPUnwrapLayerButtonObject(const tTJSVariantClosure &candidate,
                           int depth = 0) {
    if(!candidate.Object || depth > 2)
        return tTJSVariantClosure(nullptr, nullptr);
    if(TVPHasLinkButtonProperties(candidate) ||
       TVPHasExplicitLayerExpression(candidate))
        return candidate;

    static const tjs_char *const wrapper_names[] = {
        TJS_W("button"),       TJS_W("_button"),
        TJS_W("buttonLayer"),  TJS_W("layer"),
        TJS_W("nativeButton"), TJS_W("nativeLayer"),
    };
    for(const tjs_char *name : wrapper_names) {
        ttstr property_name(name);
        tTJSVariant value;
        if(TJS_FAILED(candidate.PropGet(0, property_name.c_str(),
                                        property_name.GetHint(), &value,
                                        candidate.ObjThis)) ||
           value.Type() != tvtObject)
            continue;
        tTJSVariantClosure nested = value.AsObjectClosureNoAddRef();
        tTJSVariantClosure resolved =
            TVPUnwrapLayerButtonObject(nested, depth + 1);
        if(resolved.Object)
            return resolved;
    }
    return tTJSVariantClosure(nullptr, nullptr);
}

static tTJSVariantClosure
TVPResolveLayerButtonObject(tTJSNI_BaseLayer *layer) {
    if(!layer)
        return tTJSVariantClosure(nullptr, nullptr);

    tTJSVariantClosure owner(layer->GetOwnerNoAddRef(),
                             layer->GetOwnerNoAddRef());
    tTJSVariantClosure owner_button = TVPUnwrapLayerButtonObject(owner);
    if(owner_button.Object)
        return owner_button;

    tTJSVariantClosure action = layer->GetActionOwnerNoAddRef();
    if(!action.Object)
        return owner;

    static ttstr current_name(TJS_W("current"));
    static ttstr names_name(TJS_W("names"));
    tTJSVariant current_value;
    const tjs_error current_hr =
        action.PropGet(0, current_name.c_str(), current_name.GetHint(),
                       &current_value, action.ObjThis);
    if(TJS_FAILED(current_hr) || current_value.Type() != tvtObject) {
        if(TVPLayerInputTraceEnabled())
            spdlog::info("LayerIntf button resolve layer={} stage=current hr={} type={}",
                         layer->GetName().AsStdString(), current_hr,
                         static_cast<int>(current_value.Type()));
        return owner;
    }

    tTJSVariantClosure current = current_value.AsObjectClosureNoAddRef();
    tTJSVariant names_value;
    const tjs_error names_hr = current.Object
                                   ? current.PropGet(
                                         0, names_name.c_str(),
                                         names_name.GetHint(), &names_value,
                                         current.ObjThis)
                                   : TJS_E_INVALIDOBJECT;
    if(TJS_FAILED(names_hr) || names_value.Type() != tvtObject) {
        if(TVPLayerInputTraceEnabled())
            spdlog::info("LayerIntf button resolve layer={} stage=names hr={} type={}",
                         layer->GetName().AsStdString(), names_hr,
                         static_cast<int>(names_value.Type()));
        return owner;
    }

    tTJSVariantClosure names = names_value.AsObjectClosureNoAddRef();
    ttstr layer_name = layer->GetName();
    tTJSVariant button_value;
    const tjs_error button_hr = names.Object
                                    ? names.PropGet(
                                          0, layer_name.c_str(),
                                          layer_name.GetHint(), &button_value,
                                          names.ObjThis)
                                    : TJS_E_INVALIDOBJECT;
    if(TJS_FAILED(button_hr) || button_value.Type() != tvtObject) {
        if(TVPLayerInputTraceEnabled())
            spdlog::info("LayerIntf button resolve layer={} stage=button hr={} type={}",
                         layer_name.AsStdString(), button_hr,
                         static_cast<int>(button_value.Type()));
        return owner;
    }

    tTJSVariantClosure button = button_value.AsObjectClosureNoAddRef();
    tTJSVariantClosure resolved = TVPUnwrapLayerButtonObject(button);
    const bool valid = resolved.Object != nullptr;
    if(TVPLayerInputTraceEnabled())
        spdlog::info("LayerIntf button resolve layer={} stage=properties valid={}",
                     layer_name.AsStdString(), valid ? "yes" : "no");
    return valid ? resolved : owner;
}

bool tTJSNI_BaseLayer::HasButtonClickTarget() {
    const tTJSVariantClosure button = TVPResolveLayerButtonObject(this);
    return TVPHasLinkButtonProperties(button) ||
           TVPHasExplicitLayerExpression(button);
}

// Resolve the explicit command attached to a page-sheet entry.  The native
// action owner receives the mouse event, while the entry metadata lives under
// action.current.names[<layer name>].  Returning the actual metadata object
// lets FireButtonClick use its normal eval path and avoids game-specific
// coordinate or command special cases.
static bool TVPResolveLayerExplicitExpression(tTJSNI_BaseLayer *layer,
                                               tTJSVariantClosure &object,
                                               ttstr &expression) {
    object = tTJSVariantClosure(nullptr, nullptr);
    expression = ttstr();
    if(!layer)
        return false;

    auto try_object = [&](const tTJSVariantClosure &candidate) -> bool {
        if(!candidate.Object)
            return false;
        ttstr candidate_expression;
        if(!TVPReadExplicitLayerExpression(candidate, &candidate_expression))
            return false;
        object = candidate;
        expression = candidate_expression;
        return true;
    };

    const tTJSVariantClosure owner(layer->GetOwnerNoAddRef(),
                                   layer->GetOwnerNoAddRef());
    if(try_object(owner))
        return true;

    const tTJSVariantClosure action = layer->GetActionOwnerNoAddRef();
    if(try_object(action))
        return true;

    ttstr layer_name = layer->GetName();
    auto try_names_entry = [&](const tTJSVariantClosure &names) -> bool {
        if(!names.Object)
            return false;
        tTJSVariant entry_value;
        const tjs_error entry_hr = names.PropGet(
            0, layer_name.c_str(), layer_name.GetHint(), &entry_value,
            names.ObjThis);
        if(TJS_FAILED(entry_hr) || entry_value.Type() != tvtObject) {
            if(TVPLayerInputTraceEnabled()) {
                spdlog::info(
                    "LayerIntf explicit resolver entry layer={} hr={} type={}",
                    layer_name.AsStdString(), entry_hr,
                    static_cast<int>(entry_value.Type()));
            }
            return false;
        }
        if(TVPLayerInputTraceEnabled()) {
            spdlog::info("LayerIntf explicit resolver entry layer={} value={}",
                         layer_name.AsStdString(),
                         TVPVariantDebugString(entry_value));
            TVPTraceObjectForButtonClick(
                "explicit.resolver.entry",
                entry_value.AsObjectClosureNoAddRef());
        }
        return try_object(entry_value.AsObjectClosureNoAddRef());
    };

    // Depending on which UI wrapper created the layer, the logical `names`
    // table can be attached directly to the control owner, to `current`, or
    // one level below `controlOwner`/`store`.  Probe those bounded paths
    // rather than assuming only action.current.names exists.
    auto find_in_current_names = [&](const tTJSVariantClosure &root) -> bool {
        if(!root.Object)
            return false;
        if(try_object(root) || try_names_entry(root))
            return true;

        static const tjs_char *const nested_names[] = {
            TJS_W("current"), TJS_W("names"), TJS_W("controlOwner"),
            TJS_W("store"), TJS_W("action"), TJS_W("button"),
            TJS_W("_button"), TJS_W("layer"),
        };
        for(const tjs_char *property : nested_names) {
            ttstr property_name(property);
            tTJSVariant nested_value;
            if(TJS_FAILED(root.PropGet(0, property_name.c_str(),
                                       property_name.GetHint(), &nested_value,
                                       root.ObjThis)) ||
               nested_value.Type() != tvtObject)
                continue;
            const tTJSVariantClosure nested =
                nested_value.AsObjectClosureNoAddRef();
            if(try_object(nested) || try_names_entry(nested))
                return true;

            // A control owner often exposes `names` below `current`; probe
            // that second hop while keeping traversal deterministic.
            tTJSVariant names_value;
            static ttstr names_name(TJS_W("names"));
            if(TJS_SUCCEEDED(nested.PropGet(0, names_name.c_str(),
                                            names_name.GetHint(),
                                            &names_value, nested.ObjThis)) &&
               names_value.Type() == tvtObject &&
               try_names_entry(names_value.AsObjectClosureNoAddRef()))
                return true;
        }
        return false;
    };

    if(find_in_current_names(action))
        return true;

    const tTJSVariantClosure button = TVPResolveLayerButtonObject(layer);
    if(find_in_current_names(button))
        return true;

    // A few UI wrappers expose the action owner through an `action` member
    // instead of returning it directly.  Probe that one level only.
    if(action.Object) {
        static ttstr action_name(TJS_W("action"));
        tTJSVariant action_value;
        if(TJS_SUCCEEDED(action.PropGet(0, action_name.c_str(),
                                        action_name.GetHint(), &action_value,
                                        action.ObjThis)) &&
           action_value.Type() == tvtObject &&
           find_in_current_names(action_value.AsObjectClosureNoAddRef()))
            return true;
    }

    return false;
}

static bool TVPEvaluateLayerExplicitExpression(
    tTJSNI_BaseLayer *layer, const tTJSVariantClosure &object,
    const ttstr &expression) {
    if(!layer || !object.Object || expression.IsEmpty())
        return false;

    // Page-sheet link records are plain data objects.  Evaluating through
    // their `eval` member makes the VM use the record as the expression
    // context; commands which dispatch through the global `Current` proxy
    // (for example gallery group changes) can then resolve a stale/invalid
    // owner and report success without changing the live page.  The native
    // button path already has the current layer's script owner, so use that
    // context first.  Keep the record-eval path as a fallback for wrappers
    // that intentionally provide a private evaluator.
    tTJSVariant result;
    if(TVPExecuteCafeStellaCurrentExpression(expression, layer, layer, 0,
                                              &result)) {
        return true;
    }

    static ttstr eval_name(TJS_W("eval"));
    tTJSVariant expression_arg(expression);
    tTJSVariant *args[1] = {&expression_arg};
    result.Clear();
    try {
        const tjs_error eval_hr = object.FuncCall(
            0, eval_name.c_str(), eval_name.GetHint(), &result, 1, args,
            object.ObjThis);
        if(TJS_SUCCEEDED(eval_hr))
            return true;
    } catch(eTJSScriptError &) {
    } catch(eTJS &) {
    } catch(...) {
    }

    // Plain data objects do not implement eval.  Execute through the current
    // script owner, which is the same context used by KAG's expression path.
    result.Clear();
    return TVPExecuteCafeStellaCurrentExpression(expression, layer, layer, 0,
                                                  &result);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::FireButtonClick() {
    if(Owner && !Shutdown) {
        TVPLayerRecentEventSource = this;
        TVPLayerEventSourceScope event_source_scope(this);
        const tTJSVariantClosure button_object =
            TVPResolveLayerButtonObject(this);

        if(TVPLayerInputTraceEnabled()) {
            TVPTraceObjectForButtonClick("button.fire", button_object);
            auto trace_object_property = [&](const tjs_char *property,
                                             const char *label) {
                ttstr property_name(property);
                tTJSVariant value;
                const tjs_error hr = button_object.PropGet(
                    0, property_name.c_str(), property_name.GetHint(), &value,
                    button_object.ObjThis);
                if(TJS_FAILED(hr) || value.Type() != tvtObject)
                    return;
                TVPTraceObjectForButtonClick(
                    label, value.AsObjectClosureNoAddRef());
            };
            trace_object_property(TJS_W("controlOwner"),
                                  "button.fire.controlOwner");
            trace_object_property(TJS_W("store"), "button.fire.store");
        }

        auto eval_expression_property = [&](const tjs_char *property,
                                            const char *label) -> bool {
            ttstr property_name(property);
            tTJSVariant expression_value;
            const tjs_error prop_hr =
                button_object.PropGet(0, property_name.c_str(),
                               property_name.GetHint(), &expression_value,
                               button_object.ObjThis);
            if(TJS_FAILED(prop_hr) || expression_value.Type() == tvtVoid) {
                if(TVPLayerInputTraceEnabled()) {
                    spdlog::info("LayerIntf FireButtonClick {} layer={} hr={} value=<missing>",
                                 label, GetName().AsStdString(), prop_hr);
                }
                return false;
            }

            const ttstr expression(expression_value);
            if(expression.IsEmpty()) {
                if(TVPLayerInputTraceEnabled()) {
                    spdlog::info("LayerIntf FireButtonClick {} layer={} expr=<empty>",
                                 label, GetName().AsStdString());
                }
                return false;
            }

            static ttstr eval_name(TJS_W("eval"));
            tTJSVariant expression_arg(expression);
            tTJSVariant *args[1] = { &expression_arg };
            tTJSVariant eval_result;
            try {
                const tjs_error eval_hr =
                    button_object.FuncCall(
                        0, eval_name.c_str(), eval_name.GetHint(),
                        &eval_result, 1, args, button_object.ObjThis);
                if(TVPLayerInputTraceEnabled()) {
                    spdlog::info("LayerIntf FireButtonClick {} layer={} expr={} hr={} result={}",
                                 label, GetName().AsStdString(),
                                 expression.AsStdString(), eval_hr,
                                 TJS_SUCCEEDED(eval_hr)
                                     ? TVPVariantDebugString(eval_result)
                                     : "<failed>");
                }
                return TJS_SUCCEEDED(eval_hr);
            } catch(eTJSScriptError &e) {
                if(TVPLayerInputTraceEnabled()) {
                    spdlog::info("LayerIntf FireButtonClick {} layer={} expr={} threw script message={} block={} line={} trace={}",
                                 label, GetName().AsStdString(),
                                 expression.AsStdString(),
                                 e.GetMessage().AsStdString(),
                                 e.GetBlockName()
                                     ? ttstr(e.GetBlockName()).AsStdString()
                                     : "",
                                 e.GetSourceLine(), e.GetTrace().AsStdString());
                }
            } catch(eTJS &e) {
                if(TVPLayerInputTraceEnabled()) {
                    spdlog::info("LayerIntf FireButtonClick {} layer={} expr={} threw tjs message={}",
                                 label, GetName().AsStdString(),
                                 expression.AsStdString(),
                                 e.GetMessage().AsStdString());
                }
            } catch(...) {
                if(TVPLayerInputTraceEnabled()) {
                    spdlog::info("LayerIntf FireButtonClick {} layer={} expr={} threw",
                                 label, GetName().AsStdString(),
                                 expression.AsStdString());
                }
            }
            return false;
        };

        // Page-sheet copy buttons keep their command on the logical entry
        // (`current.names[<layer>].exp`) instead of on the native button
        // wrapper.  `_evalOnClick` is still present on that wrapper, but for
        // these entries it is only a no-op bookkeeping hook.  Evaluate the
        // resolved entry expression before invoking that hook so commands
        // such as `Current.cmd("changeGroup/0")` are not swallowed.
        {
            tTJSVariantClosure expression_object;
            ttstr expression;
            if(TVPResolveLayerExplicitExpression(this, expression_object,
                                                 expression)) {
                const bool evaluated = TVPEvaluateLayerExplicitExpression(
                    this, expression_object, expression);
                if(TVPLayerInputTraceEnabled()) {
                    spdlog::info(
                        "LayerIntf FireButtonClick explicit exp layer={} expr={} evaluated={}",
                        GetName().AsStdString(), expression.AsStdString(),
                        evaluated ? "yes" : "no");
                }
                if(evaluated)
                    return;
            }
        }

        if(eval_expression_property(TJS_W("exp"), "exp"))
            return;

        auto get_current_selected_index = [&]() -> tjs_int {
            tTJSVariant value;
            if(TVPExecuteCafeStellaCurrentExpression(
                   TJS_W("Current.propget(\"_lastSelect\")"), this, this, 0,
                   &value) &&
               value.Type() != tvtVoid) {
                try {
                    const tjs_int selected =
                        static_cast<tjs_int>(value.AsInteger());
                    if(TVPLayerInputTraceEnabled()) {
                        spdlog::info("LayerIntf FireButtonClick current selected layer={} index={}",
                                     GetName().AsStdString(), selected);
                    }
                    return selected;
                } catch(...) {
                }
            }
            const tjs_int visible_index = TVPLayerLastSaveLoadItemIndex;
            const tjs_int data_index =
                TVPGetCafeStellaSaveLoadDataIndex(this, this, visible_index);
            if(TVPLayerInputTraceEnabled()) {
                spdlog::info("LayerIntf FireButtonClick current selected fallback layer={} visible={} data={}",
                             GetName().AsStdString(), visible_index,
                             data_index);
            }
            return data_index;
        };

        auto invoke_current_default = [&]() -> bool {
            const ttstr layer_name = GetName();
            if(layer_name != TJS_W("to_load") && layer_name != TJS_W("to_save"))
                return false;

            const tjs_int selected_index = get_current_selected_index();
            tTJSVariant result;
            if(TVPInvokeCafeStellaCurrentMethod(
                   this, this, selected_index, TJS_W("onDefaultSelect"),
                   &result)) {
                if(TVPLayerInputTraceEnabled()) {
                    spdlog::info("LayerIntf FireButtonClick current onDefaultSelect layer={} index={} result={}",
                                 GetName().AsStdString(), selected_index,
                                 TVPVariantDebugString(result));
                }
                return true;
            }

            const ttstr selected_text(selected_index);
            if(TVPExecuteCafeStellaCurrentExpression(
                   ttstr(TJS_W("Current.func(\"onDefaultSelect\")(")) +
                       selected_text + TJS_W(")"),
                   this, this, selected_index, &result)) {
                if(TVPLayerInputTraceEnabled()) {
                    spdlog::info("LayerIntf FireButtonClick current func onDefaultSelect layer={} index={} result={}",
                                 GetName().AsStdString(), selected_index,
                                 TVPVariantDebugString(result));
                }
                return true;
            }

            return false;
        };

        if(invoke_current_default())
            return;

        // Native KAG button layers dispatch an inline `onclick` expression
        // through the button object.  Page-sheet copies are different: their
        // wrapper still exposes `_evalOnClick`, but `onclick` is void and the
        // actual command lives in the control owner's `links[linkNum]`
        // record.  Do not treat that no-op method as a successful dispatch;
        // let the standard owner callback below process the link instead.
        static ttstr eval_click_name(TJS_W("_evalOnClick"));
        tTJSVariant onclick_value;
        static ttstr onclick_name(TJS_W("onclick"));
        const tjs_error onclick_hr = button_object.PropGet(
            0, onclick_name.c_str(), onclick_name.GetHint(), &onclick_value,
            button_object.ObjThis);
        bool has_inline_click = TJS_SUCCEEDED(onclick_hr) &&
                                onclick_value.Type() != tvtVoid;
        if(has_inline_click) {
            try {
                has_inline_click = !ttstr(onclick_value).IsEmpty();
            } catch(...) {
                has_inline_click = false;
            }
        }
        if(has_inline_click) {
            tTJSVariant eval_result;
            const tjs_error eval_hr =
                button_object.FuncCall(0, eval_click_name.c_str(),
                                       eval_click_name.GetHint(), &eval_result,
                                       0, nullptr, button_object.ObjThis);
            if(TVPLayerInputTraceEnabled()) {
                spdlog::info(
                    "LayerIntf FireButtonClick _evalOnClick layer={} hr={} result={}",
                    GetName().AsStdString(), eval_hr,
                    TJS_SUCCEEDED(eval_hr)
                        ? TVPVariantDebugString(eval_result)
                        : "<failed>");
            }
            if(TJS_SUCCEEDED(eval_hr))
                return;
        } else if(TVPLayerInputTraceEnabled()) {
            spdlog::info(
                "LayerIntf FireButtonClick _evalOnClick skipped layer={} onclick_hr={} onclick_type={}",
                GetName().AsStdString(), onclick_hr,
                static_cast<int>(onclick_value.Type()));
        }

        auto call_control_owner_button = [&]() -> bool {
            static ttstr control_owner_name(TJS_W("controlOwner"));
            static ttstr link_num_name(TJS_W("linkNum"));
            static ttstr on_button_click_name(TJS_W("onButtonClick"));

            tTJSVariant control_owner_value;
            const tjs_error control_hr =
                button_object.PropGet(0, control_owner_name.c_str(),
                               control_owner_name.GetHint(),
                               &control_owner_value, button_object.ObjThis);
            if(TJS_FAILED(control_hr) ||
               control_owner_value.Type() != tvtObject) {
                if(TVPLayerInputTraceEnabled()) {
                    spdlog::info("LayerIntf FireButtonClick controlOwner layer={} hr={} type={}",
                                 GetName().AsStdString(), control_hr,
                                 static_cast<int>(control_owner_value.Type()));
                }
                return false;
            }

            tTJSVariant link_num((tjs_int)0);
            tTJSVariant link_num_value;
            const tjs_error link_hr =
                button_object.PropGet(0, link_num_name.c_str(),
                                      link_num_name.GetHint(), &link_num_value,
                                      button_object.ObjThis);
            if(TJS_SUCCEEDED(link_hr) && link_num_value.Type() != tvtVoid)
                link_num = link_num_value;

            tTJSVariantClosure control_owner =
                control_owner_value.AsObjectClosureNoAddRef();
            if(!control_owner.Object)
                return false;

            // LinkButtonLayerBase stores the actual command in
            // controlOwner.links[linkNum].exp.  Most engines execute it from
            // the owner callback, but page-sheet copies in this game expose
            // a lightweight owner callback that only updates selection state.
            // Resolve and execute the link record here when the wrapper has
            // no inline onclick.  This is the generic KAG link path; it is
            // deliberately independent of game names and link numbers.
            static ttstr links_name(TJS_W("links"));
            tTJSVariant links_value;
            const tjs_error links_hr = control_owner.PropGet(
                0, links_name.c_str(), links_name.GetHint(), &links_value,
                control_owner.ObjThis);
            tTJSVariantClosure link_object;
            ttstr link_expression;
            bool have_link_expression = false;
            bool have_link_target = false;
            if(TJS_SUCCEEDED(links_hr) && links_value.Type() == tvtObject) {
                tTJSVariantClosure links =
                    links_value.AsObjectClosureNoAddRef();
                tTJSVariant link_value;
                const tjs_int link_index =
                    static_cast<tjs_int>(link_num.AsInteger());
                const tjs_error item_hr = links.PropGetByNum(
                    0, link_index, &link_value, links.ObjThis);
                if(TJS_SUCCEEDED(item_hr) &&
                   link_value.Type() == tvtObject) {
                    link_object = link_value.AsObjectClosureNoAddRef();
                    have_link_expression = TVPReadExplicitLayerExpression(
                        link_object, &link_expression);
                    static ttstr target_name(TJS_W("target"));
                    tTJSVariant target_value;
                    if(TJS_SUCCEEDED(link_object.PropGet(
                           0, target_name.c_str(), target_name.GetHint(),
                           &target_value, link_object.ObjThis)) &&
                       target_value.Type() != tvtVoid) {
                        try {
                            have_link_target = !ttstr(target_value).IsEmpty();
                        } catch(...) {
                            have_link_target = false;
                        }
                    }
                    if(TVPLayerInputTraceEnabled()) {
                        TVPTraceObjectForButtonClick(
                            "button.fire.controlOwner.link", link_object);
                        spdlog::info(
                            "LayerIntf FireButtonClick link inspect layer={} link={} exp={}"
                            " has_exp={}",
                            GetName().AsStdString(), link_index,
                            have_link_expression
                                ? link_expression.AsStdString()
                                : "<missing>",
                            have_link_expression ? "yes" : "no");
                    }
                } else if(TVPLayerInputTraceEnabled()) {
                    spdlog::info(
                        "LayerIntf FireButtonClick link inspect layer={} link={} hr={} type={}",
                        GetName().AsStdString(), link_index, item_hr,
                        static_cast<int>(link_value.Type()));
                }
            } else if(TVPLayerInputTraceEnabled()) {
                spdlog::info(
                    "LayerIntf FireButtonClick links inspect layer={} hr={} type={}",
                    GetName().AsStdString(), links_hr,
                    static_cast<int>(links_value.Type()));
            }

            // A link target is part of the same KAG transaction as its
            // expression.  Let the owner callback evaluate the expression
            // and perform that navigation together.  Only execute the
            // expression directly for target-less page-sheet controls whose
            // lightweight owner callback does not dispatch the command.
            if(!has_inline_click && have_link_expression &&
               !have_link_target) {
                const bool evaluated = TVPEvaluateLayerExplicitExpression(
                    this, link_object, link_expression);
                if(TVPLayerInputTraceEnabled()) {
                    spdlog::info(
                        "LayerIntf FireButtonClick link exp layer={} link={} expr={} evaluated={}",
                        GetName().AsStdString(),
                        TVPVariantDebugString(link_num),
                        link_expression.AsStdString(),
                        evaluated ? "yes" : "no");
                }
                if(evaluated)
                    return true;
            }

            tTJSVariant *args[1] = { &link_num };
            tTJSVariant result;
            try {
                const tjs_error hr = control_owner.FuncCall(
                    0, on_button_click_name.c_str(),
                    on_button_click_name.GetHint(), &result, 1, args,
                    control_owner.ObjThis);
                if(TVPLayerInputTraceEnabled()) {
                    spdlog::info("LayerIntf FireButtonClick controlOwner.onButtonClick layer={} link={} hr={} result={}",
                                 GetName().AsStdString(),
                                 TVPVariantDebugString(link_num), hr,
                                 TJS_SUCCEEDED(hr)
                                     ? TVPVariantDebugString(result)
                                     : "<failed>");
                }
                return TJS_SUCCEEDED(hr);
            } catch(eTJSScriptError &e) {
                if(TVPLayerInputTraceEnabled()) {
                    spdlog::info("LayerIntf FireButtonClick controlOwner.onButtonClick layer={} link={} threw script message={} block={} line={} trace={}",
                                 GetName().AsStdString(),
                                 TVPVariantDebugString(link_num),
                                 e.GetMessage().AsStdString(),
                                 e.GetBlockName()
                                     ? ttstr(e.GetBlockName()).AsStdString()
                                     : "",
                                 e.GetSourceLine(), e.GetTrace().AsStdString());
                }
            } catch(eTJS &e) {
                if(TVPLayerInputTraceEnabled()) {
                    spdlog::info("LayerIntf FireButtonClick controlOwner.onButtonClick layer={} link={} threw tjs message={}",
                                 GetName().AsStdString(),
                                 TVPVariantDebugString(link_num),
                                 e.GetMessage().AsStdString());
                }
            } catch(...) {
                if(TVPLayerInputTraceEnabled()) {
                    spdlog::info("LayerIntf FireButtonClick controlOwner.onButtonClick layer={} link={} threw",
                                 GetName().AsStdString(),
                                 TVPVariantDebugString(link_num));
                }
            }
            return false;
        };

        if(call_control_owner_button())
            return;

        static ttstr eventname(TJS_W("onButtonClick"));
        static ttstr link_num_name(TJS_W("linkNum"));
        tTJSVariant link_num((tjs_int)0);
        tTJSVariant value;
        if(TJS_SUCCEEDED(button_object.PropGet(
               0, link_num_name.c_str(), link_num_name.GetHint(), &value,
               button_object.ObjThis))) {
            link_num = value;
        }
        tTJSVariant *args[1] = { &link_num };
        button_object.FuncCall(0, eventname.c_str(), eventname.GetHint(),
                               nullptr, 1, args, button_object.ObjThis);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::FireDoubleClick(tjs_int x, tjs_int y) {
    if(Owner && !Shutdown) {
        tTJSVariant param[2];
        param[0] = x;
        param[1] = y;
        static ttstr eventname(TJS_W("onDoubleClick"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 2, param);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::FireMouseDown(tjs_int x, tjs_int y, tTVPMouseButton mb,
                                     tjs_uint32 flags) {
    if(Owner && !Shutdown) {
        tTJSVariant param[4];
        param[0] = x;
        param[1] = y;
        param[2] = (tjs_int)mb;
        param[3] = (tjs_int64)flags;
        static ttstr eventname(TJS_W("onMouseDown"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 4, param);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::FireMouseUp(tjs_int x, tjs_int y, tTVPMouseButton mb,
                                   tjs_uint32 flags) {
    if(Owner && !Shutdown) {
        TVPLayerRecentEventSource = this;
        TVPLayerEventSourceScope event_source_scope(this);
        tTJSVariant param[4];
        param[0] = x;
        param[1] = y;
        param[2] = (tjs_int)mb;
        param[3] = (tjs_int64)flags;
        static ttstr eventname(TJS_W("onMouseUp"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 4, param);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::FireMouseMove(tjs_int x, tjs_int y, tjs_uint32 flags) {
    if(Owner && !Shutdown) {
        tTJSVariant param[3];
        param[0] = x;
        param[1] = y;
        param[2] = (tjs_int64)flags;
        static ttstr eventname(TJS_W("onMouseMove"));
        TVPPostEvent(Owner, Owner, eventname, 0,
                     TVP_EPT_IMMEDIATE | TVP_EPT_DISCARDABLE, 3, param);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::FireMouseEnter() {
    if(Owner && !Shutdown) {
        static ttstr eventname(TJS_W("onMouseEnter"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 0, nullptr);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::FireMouseLeave() {
    if(Owner && !Shutdown) {
        static ttstr eventname(TJS_W("onMouseLeave"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 0, nullptr);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::ReleaseCapture() {
    if(Manager)
        Manager->ReleaseCapture();
    // this releases mouse capture from all layers, ignoring which
    // layer captures.
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::ReleaseTouchCapture(tjs_uint32 id, bool all) {
    if(Manager) {
        if(all) {
            Manager->ReleaseTouchCaptureAll();
        } else {
            Manager->ReleaseTouchCapture(id);
        }
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::FireTouchDown(tjs_real x, tjs_real y, tjs_real cx,
                                     tjs_real cy, tjs_uint32 id) {
    if(Owner && !Shutdown) {
        tTJSVariant arg[5] = { x, y, cx, cy, (tjs_int64)id };
        static ttstr eventname(TJS_W("onTouchDown"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 5, arg);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::FireTouchUp(tjs_real x, tjs_real y, tjs_real cx,
                                   tjs_real cy, tjs_uint32 id) {
    if(Owner && !Shutdown) {
        tTJSVariant arg[5] = { x, y, cx, cy, (tjs_int64)id };
        static ttstr eventname(TJS_W("onTouchUp"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 5, arg);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::FireTouchMove(tjs_real x, tjs_real y, tjs_real cx,
                                     tjs_real cy, tjs_uint32 id) {
    if(Owner && !Shutdown) {
        tTJSVariant arg[5] = { x, y, cx, cy, (tjs_int64)id };
        static ttstr eventname(TJS_W("onTouchMove"));
        TVPPostEvent(Owner, Owner, eventname, 0,
                     TVP_EPT_IMMEDIATE | TVP_EPT_DISCARDABLE, 5, arg);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::FireTouchScaling(tjs_real startdist, tjs_real curdist,
                                        tjs_real cx, tjs_real cy,
                                        tjs_int flag) {
    if(Owner && !Shutdown) {
        tTJSVariant arg[5] = { startdist, curdist, cx, cy, flag };
        static ttstr eventname(TJS_W("onTouchScaling"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 5, arg);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::FireTouchRotate(tjs_real startangle, tjs_real curangle,
                                       tjs_real dist, tjs_real cx, tjs_real cy,
                                       tjs_int flag) {
    if(Owner && !Shutdown) {
        tTJSVariant arg[6] = { startangle, curangle, dist, cx, cy, flag };
        static ttstr eventname(TJS_W("onTouchRotate"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 6, arg);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::FireMultiTouch() {
    if(Owner && !Shutdown) {
        static ttstr eventname(TJS_W("onMultiTouch"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 0, nullptr);
    }
}

//---------------------------------------------------------------------------
bool tTJSNI_BaseLayer::ParentFocusable() const {
    tTJSNI_BaseLayer *par = Parent;
    while(par) {
        if(!par->Visible || !par->Enabled) {
            return false;
        }
        // note that here we do not check parent's focusable state.
        par = par->Parent;
    }

    return true;
}

//---------------------------------------------------------------------------
bool tTJSNI_BaseLayer::SetFocus(bool direction) {
    if(Manager)
        return Manager->SetFocusTo(this, direction);
    return false;
}

//---------------------------------------------------------------------------
bool tTJSNI_BaseLayer::GetFocused() {
    if(!Manager)
        return false;
    return Manager->GetFocusedLayer() == this;
}

//---------------------------------------------------------------------------
tTJSNI_BaseLayer *
tTJSNI_BaseLayer::SearchFirstFocusable(bool ignore_chain_focusable) {
    if(ignore_chain_focusable) {
        if(GetNodeFocusable())
            return this;
    } else {
        if(GetNodeFocusable() && JoinFocusChain)
            return this;
    }

    TVP_LAYER_FOR_EACH_CHILD_NOLOCK_BEGIN(child)
    tTJSNI_BaseLayer *lay = child->SearchFirstFocusable(ignore_chain_focusable);
    if(lay)
        return lay;
    TVP_LAYER_FOR_EACH_CHILD_NOLOCK_END

    return nullptr;
}

//---------------------------------------------------------------------------
tTJSNI_BaseLayer *tTJSNI_BaseLayer::_GetPrevFocusable() {
    // search next focusable layer backward
    tTJSNI_BaseLayer *p = this;
    tTJSNI_BaseLayer *current = this;

    p = p->GetNeighborAbove(true);
    if(current == p)
        return nullptr;
    if(!p)
        return nullptr;
    current = p;
    do {
        if(p->GetNodeFocusable() && p->JoinFocusChain)
            return p; // next focusable layer found
    } while(p = p->GetNeighborAbove(true), p && p != current);

    return nullptr; // no layer found
}

//---------------------------------------------------------------------------
tTJSNI_BaseLayer *tTJSNI_BaseLayer::GetPrevFocusable() {
    // search next focusable layer backward
    FocusWork = _GetPrevFocusable();
    if(Owner && !Shutdown && (!FocusWork || FocusWork->Owner)) {
        static ttstr eventname(TJS_W("onSearchPrevFocusable"));
        tTJSVariant param[1];
        if(FocusWork)
            param[0] = tTJSVariant(FocusWork->Owner, FocusWork->Owner);
        else
            param[0] = tTJSVariant((iTJSDispatch2 *)nullptr);
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 1, param);
    }
    return FocusWork;
}

//---------------------------------------------------------------------------
tTJSNI_BaseLayer *tTJSNI_BaseLayer::_GetNextFocusable() {
    // search next focusable layer forward
    tTJSNI_BaseLayer *p = this;
    tTJSNI_BaseLayer *current = this;

    p = p->GetNeighborBelow(true);
    if(current == p)
        return nullptr;
    if(!p)
        return nullptr;
    current = p;
    do {
        if(p->GetNodeFocusable() && p->JoinFocusChain)
            return p; // next focusable layer found
    } while(p = p->GetNeighborBelow(true), p && p != current);

    return nullptr; // no layer found
}

//---------------------------------------------------------------------------
tTJSNI_BaseLayer *tTJSNI_BaseLayer::GetNextFocusable() {
    // search next focusable layer forward
    FocusWork = _GetNextFocusable();
    if(Owner && !Shutdown && (!FocusWork || FocusWork->Owner)) {
        static ttstr eventname(TJS_W("onSearchNextFocusable"));
        tTJSVariant param[1];
        if(FocusWork)
            param[0] = tTJSVariant(FocusWork->Owner, FocusWork->Owner);
        else
            param[0] = tTJSVariant((iTJSDispatch2 *)nullptr);
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 1, param);
    }
    return FocusWork;
}

//---------------------------------------------------------------------------
tTJSNI_BaseLayer *tTJSNI_BaseLayer::FocusPrev() {
    if(Manager)
        return Manager->FocusPrev();
    else
        return nullptr;
}

//---------------------------------------------------------------------------
tTJSNI_BaseLayer *tTJSNI_BaseLayer::FocusNext() {
    if(Manager)
        return Manager->FocusNext();
    else
        return nullptr;
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetFocusable(bool b) {
    if(Focusable != b) {
        bool bstate = GetNodeFocusable();
        Focusable = b;
        bool astate = GetNodeFocusable();
        if(bstate != astate) {
            if(!astate) {
                // remove focus from this layer
                if(Manager) {
                    if(Manager->GetFocusedLayer() == this)
                        Manager->SetFocusTo(GetNextFocusable(),
                                            true); // blur
                }
            }
        }
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::FireBlur(tTJSNI_BaseLayer *prevfocused) {
    if(Owner && !Shutdown) {
        static ttstr eventname(TJS_W("onBlur"));
        tTJSVariant param[1];
        if(prevfocused)
            param[0] = tTJSVariant(prevfocused->Owner, prevfocused->Owner);
        else
            param[0] = tTJSVariant((iTJSDispatch2 *)nullptr);
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 1, param);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::FireFocus(tTJSNI_BaseLayer *prevfocused,
                                 bool direction) {
    if(Owner && !Shutdown) {
        static ttstr eventname(TJS_W("onFocus"));
        tTJSVariant param[2];
        if(prevfocused)
            param[0] = tTJSVariant(prevfocused->Owner, prevfocused->Owner);
        else
            param[0] = tTJSVariant((iTJSDispatch2 *)nullptr);
        param[1] = direction;
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 2, param);
    }
}

//---------------------------------------------------------------------------
tTJSNI_BaseLayer *
tTJSNI_BaseLayer::FireBeforeFocus(tTJSNI_BaseLayer *prevfocused,
                                  bool direction) {
    FocusWork = this;
    if(Owner && !Shutdown) {
        static ttstr eventname(TJS_W("onBeforeFocus"));
        tTJSVariant param[3];
        param[0] = tTJSVariant(Owner, Owner);
        if(prevfocused)
            param[1] = tTJSVariant(prevfocused->Owner, prevfocused->Owner);
        else
            param[1] = tTJSVariant((iTJSDispatch2 *)nullptr);
        param[2] = direction;
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 3, param);
    }
    return FocusWork;
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetMode() {
    if(Manager)
        Manager->SetModeTo(this);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::RemoveMode() {
    if(Manager)
        Manager->RemoveModeFrom(this);
}

//---------------------------------------------------------------------------
bool tTJSNI_BaseLayer::IsDisabledByMode() {
    // is "this" layer disable by modal layer?
    if(!Manager)
        return false;
    tTJSNI_BaseLayer *current = Manager->GetCurrentModalLayer();
    if(!current)
        return false;
    return !IsAncestorOrSelf(current);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::NotifyNodeEnabledState() {
    bool en = GetNodeEnabled();
    if(Owner && !Shutdown && EnabledWork != en) {
        if(en) {
            static ttstr eventname(TJS_W("onNodeEnabled"));
            TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 0,
                         nullptr);
        } else {
            static ttstr eventname(TJS_W("onNodeDisabled"));
            TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 0,
                         nullptr);
        }
    }

    TVP_LAYER_FOR_EACH_CHILD_BEGIN(child)
    child->NotifyNodeEnabledState();
    TVP_LAYER_FOR_EACH_CHILD_END
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SaveEnabledWork() {
    EnabledWork = GetNodeEnabled();

    TVP_LAYER_FOR_EACH_CHILD_NOLOCK_BEGIN(child)
    child->SaveEnabledWork();
    TVP_LAYER_FOR_EACH_CHILD_NOLOCK_END
}
//---------------------------------------------------------------------------

void tTJSNI_BaseLayer::SetEnabled(bool b) {
    // set enabled
    if(Enabled != b) {
        if(Manager)
            Manager->SaveEnabledWork();

        try {
            Enabled = b;

            if(Enabled) {
                // become enabled
                if(Manager)
                    Manager->CheckTreeFocusableState(this);
            } else {
                // become disabled
                if(Manager)
                    Manager->BlurTree(this);
            }
        } catch(...) {
            if(Manager)
                Manager->NotifyNodeEnabledState();
            throw;
        }

        if(Manager)
            Manager->NotifyNodeEnabledState();
    }
}

//---------------------------------------------------------------------------
bool tTJSNI_BaseLayer::ParentEnabled() {
    tTJSNI_BaseLayer *par = Parent;
    while(par) {
        if(!par->Enabled) {
            return false;
        }
        par = par->Parent;
    }

    return true;
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::FireKeyDown(tjs_uint key, tjs_uint32 shift) {
    if(Owner && !Shutdown) {
        tTJSVariant param[3];
        param[0] = (tjs_int)key;
        param[1] = (tjs_int)shift;
        param[2] = true;
        static ttstr eventname(TJS_W("onKeyDown"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 3, param);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::FireKeyUp(tjs_uint key, tjs_uint32 shift) {
    if(Owner && !Shutdown) {
        tTJSVariant param[3];
        param[0] = (tjs_int)key;
        param[1] = (tjs_int)shift;
        param[2] = true;
        static ttstr eventname(TJS_W("onKeyUp"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 3, param);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::FireKeyPress(tjs_char key) {
    if(Owner && !Shutdown) {
        tjs_char buf[2];
        buf[0] = (tjs_char)key;
        buf[1] = 0;
        tTJSVariant param[2];
        param[0] = buf;
        param[1] = true;
        static ttstr eventname(TJS_W("onKeyPress"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 2, param);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::FireMouseWheel(tjs_uint32 shift, tjs_int delta,
                                      tjs_int x, tjs_int y) {
    if(Owner && !Shutdown) {
        tTJSVariant val[4] = { (tjs_int)shift, delta, x, y };
        static ttstr eventname(TJS_W("onMouseWheel"));
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 4, val);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::DefaultKeyDown(tjs_uint key, tjs_uint32 shift) {
    // default keyboard behavior
    // this method is to be called by default onKeyDown event handler
    if(!Manager)
        return;

    bool no_shift_downed = !(shift & TVP_SS_SHIFT) && !(shift & TVP_SS_ALT) &&
        !(shift & TVP_SS_CTRL);

    if((key == VK_TAB || key == VK_RIGHT || key == VK_DOWN) &&
       no_shift_downed) {
        // [TAB] [<RIGHT>] [<DOWN>] : to next focusable
        Manager->FocusNext();
    } else if((key == VK_TAB && (shift & TVP_SS_SHIFT) &&
               !(shift & TVP_SS_ALT) && !(shift & TVP_SS_CTRL)) ||
              key == VK_LEFT || key == VK_UP) {
        // [SHIFT]+[TAB] [<LEFT>] [<UP>] : to previous focusable
        Manager->FocusPrev();
    } else if((key == VK_RETURN || key == VK_ESCAPE) && no_shift_downed) {
        // [ENTER] or [ESC] : pass to parent layer
        if(Parent) {
            if(Parent->GetNodeEnabled())
                Parent->FireKeyDown(key, shift);
        }
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::DefaultKeyUp(tjs_uint key, tjs_uint32 shift) {
    // default keyboard behavior
    if(!Manager)
        return;

    bool no_shift_downed = !(shift & TVP_SS_SHIFT) && !(shift & TVP_SS_ALT) &&
        !(shift & TVP_SS_CTRL);

    if((key == VK_RETURN || key == VK_ESCAPE) && no_shift_downed) {
        // [ENTER] or [ESC] : pass to parent layer
        if(Parent) {
            if(Parent->GetNodeEnabled())
                Parent->FireKeyUp(key, shift);
        }
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::DefaultKeyPress(tjs_char key) {
    // default keyboard behavior
    if(!Manager)
        return;

    if(key == 13 /* enter */ || key == 0x1b /* esc */) {
        if(Parent) {
            if(Parent->GetNodeEnabled())
                Parent->FireKeyPress(key);
        }
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// cache management
//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::AllocateCache() {
    if(!CacheBitmap) {
        CacheBitmap =
            new tTVPBaseTexture(Rect.get_width(), Rect.get_height(), 32);
    } else {
        CacheBitmap->SetSize(Rect.get_width(), Rect.get_height());
    }
    CacheRecalcRegion.Or(tTVPRect(0, 0, Rect.get_width(), Rect.get_height()));
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::ResizeCache() {
    // resize to Rect's size
    if(CacheBitmap && MainImage) {
        CacheBitmap->SetSize(MainImage->GetWidth(), MainImage->GetHeight());
    }
    CacheRecalcRegion.Or(tTVPRect(0, 0, Rect.get_width(), Rect.get_height()));
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::DeallocateCache() {
    if(CacheBitmap) {
        delete CacheBitmap;
        CacheBitmap = nullptr;
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::DispSizeChanged() {
    // is called from geographical management
    ResizeCache();
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::CompactCache() {
    // free cache image if the cache is not needed
    if(!CacheEnabledCount)
        DeallocateCache();
}

//---------------------------------------------------------------------------
tjs_uint tTJSNI_BaseLayer::IncCacheEnabledCount() {
    CacheEnabledCount++;
    if(CacheEnabledCount) {
        RegisterCompactEventHook();
        // register to compact event hook to call CompactCache when
        // idle
        AllocateCache();
    }
    return CacheEnabledCount;
}

//---------------------------------------------------------------------------
tjs_uint tTJSNI_BaseLayer::DecCacheEnabledCount() {
    CacheEnabledCount--;
    if(TVPFreeUnusedLayerCache && !CacheEnabledCount)
        DeallocateCache();
    // object is not freed until compact event, unless
    // TVPFreeUnusedLayerCache flags
    return CacheEnabledCount;
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetCached(bool b) {
    if(b != Cached) {
        Cached = b;
        if(b)
            IncCacheEnabledCount();
        else
            DecCacheEnabledCount();
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// drawing function stuff
//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::ResetClip() {
    if(!MainImage)
        TVPThrowExceptionMessage(TVPNotDrawableLayerType);

    ClipRect.left = ClipRect.top = 0;
    ClipRect.right = MainImage->GetWidth();
    ClipRect.bottom = MainImage->GetHeight();
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetClip(tjs_int left, tjs_int top, tjs_int width,
                               tjs_int height) {
    if(!MainImage)
        TVPThrowExceptionMessage(TVPNotDrawableLayerType);

    ClipRect.left = left < 0 ? 0 : left;
    ClipRect.top = top < 0 ? 0 : top;
    tjs_int right = width + left;
    tjs_int bottom = height + top;
    tjs_int w = MainImage->GetWidth();
    tjs_int h = MainImage->GetHeight();
    ClipRect.right = w < right ? w : right;
    ClipRect.bottom = h < bottom ? h : bottom;
    if(ClipRect.right < ClipRect.left)
        ClipRect.right = ClipRect.left;
    if(ClipRect.bottom < ClipRect.top)
        ClipRect.bottom = ClipRect.top;
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetClipLeft(tjs_int left) {
    SetClip(left, GetClipTop(), GetClipWidth(), GetClipHeight());
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetClipTop(tjs_int top) {
    SetClip(GetClipLeft(), top, GetClipWidth(), GetClipHeight());
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetClipWidth(tjs_int width) {
    SetClip(GetClipLeft(), GetClipTop(), width, GetClipHeight());
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetClipHeight(tjs_int height) {
    SetClip(GetClipLeft(), GetClipTop(), GetClipWidth(), height);
}

//---------------------------------------------------------------------------
bool tTJSNI_BaseLayer::ClipDestPointAndSrcRect(tjs_int &dx, tjs_int &dy,
                                               tTVPRect &srcrectout,
                                               const tTVPRect &srcrect) const {
    // clip (dx, dy) <- srcrect	with current clipping rectangle
    srcrectout = srcrect;
    tjs_int dr = dx + srcrect.right - srcrect.left;
    tjs_int db = dy + srcrect.bottom - srcrect.top;

    if(dx < ClipRect.left) {
        srcrectout.left += (ClipRect.left - dx);
        dx = ClipRect.left;
    }

    if(dr > ClipRect.right) {
        srcrectout.right -= (dr - ClipRect.right);
    }

    if(srcrectout.right <= srcrectout.left)
        return false; // out of the clipping rect

    if(dy < ClipRect.top) {
        srcrectout.top += (ClipRect.top - dy);
        dy = ClipRect.top;
    }

    if(db > ClipRect.bottom) {
        srcrectout.bottom -= (db - ClipRect.bottom);
    }

    if(srcrectout.bottom <= srcrectout.top)
        return false; // out of the clipping rect

    return true;
}

//---------------------------------------------------------------------------
bool tTJSNI_BaseLayer::GetBltMethodFromOperationModeAndDrawFace(
    tTVPBBBltMethod &result, tTVPBlendOperationMode mode) {
    // resulting corresponding  tTVPBBBltMethod value of mode and
    // current DrawFace. returns whether the method is known.
    tTVPBBBltMethod met;
    bool met_set = false;
    switch(mode) {
        case omPsNormal:
            met_set = true;
            met = bmPsNormal;
            break;
        case omPsAdditive:
            met_set = true;
            met = bmPsAdditive;
            break;
        case omPsSubtractive:
            met_set = true;
            met = bmPsSubtractive;
            break;
        case omPsMultiplicative:
            met_set = true;
            met = bmPsMultiplicative;
            break;
        case omPsScreen:
            met_set = true;
            met = bmPsScreen;
            break;
        case omPsOverlay:
            met_set = true;
            met = bmPsOverlay;
            break;
        case omPsHardLight:
            met_set = true;
            met = bmPsHardLight;
            break;
        case omPsSoftLight:
            met_set = true;
            met = bmPsSoftLight;
            break;
        case omPsColorDodge:
            met_set = true;
            met = bmPsColorDodge;
            break;
        case omPsColorDodge5:
            met_set = true;
            met = bmPsColorDodge5;
            break;
        case omPsColorBurn:
            met_set = true;
            met = bmPsColorBurn;
            break;
        case omPsLighten:
            met_set = true;
            met = bmPsLighten;
            break;
        case omPsDarken:
            met_set = true;
            met = bmPsDarken;
            break;
        case omPsDifference:
            met_set = true;
            met = bmPsDifference;
            break;
        case omPsDifference5:
            met_set = true;
            met = bmPsDifference5;
            break;
        case omPsExclusion:
            met_set = true;
            met = bmPsExclusion;
            break;
        case omAdditive:
            met_set = true;
            met = bmAdd;
            break;
        case omSubtractive:
            met_set = true;
            met = bmSub;
            break;
        case omMultiplicative:
            met_set = true;
            met = bmMul;
            break;
        case omDodge:
            met_set = true;
            met = bmDodge;
            break;
        case omDarken:
            met_set = true;
            met = bmDarken;
            break;
        case omLighten:
            met_set = true;
            met = bmLighten;
            break;
        case omScreen:
            met_set = true;
            met = bmScreen;
            break;
        case omAlpha:
            if(DrawFace == dfAlpha) {
                met_set = true;
                met = bmAlphaOnAlpha;
                break;
            } else if(DrawFace == dfAddAlpha) {
                met_set = true;
                met = bmAlphaOnAddAlpha;
                break;
            } else if(DrawFace == dfOpaque) {
                met_set = true;
                met = bmAlpha;
                break;
            }
            break;
        case omAddAlpha:
            if(DrawFace == dfAlpha) {
                met_set = true;
                met = bmAddAlphaOnAlpha;
                break;
            } else if(DrawFace == dfAddAlpha) {
                met_set = true;
                met = bmAddAlphaOnAddAlpha;
                break;
            } else if(DrawFace == dfOpaque) {
                met_set = true;
                met = bmAddAlpha;
                break;
            }
            break;
        case omOpaque:
            if(DrawFace == dfAlpha) {
                met_set = true;
                met = bmCopyOnAlpha;
                break;
            } else if(DrawFace == dfAddAlpha) {
                met_set = true;
                met = bmCopyOnAddAlpha;
                break;
            } else if(DrawFace == dfOpaque) {
                met_set = true;
                met = bmCopy;
                break;
            }
            break;
        case (tTVPBlendOperationMode)dfAuto:
            break;
    }

    result = met;

    return met_set;
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::FillRect(const tTVPRect &rect, tjs_uint32 color) {
    if(TVPLayerErrorTraceTake() &&
       (color == static_cast<tjs_uint32>(0x88000000u) ||
        GetName().AsStdString().find("直太") != std::string::npos)) {
        const auto *parent = GetParent();
        spdlog::warn(
            "Layer.error-placeholder fill layer={} parent={} face={} visible={} rect=({},{} {}x{}) color=0x{:08x} size={}x{}",
            GetName().AsStdString(),
            parent ? parent->GetName().AsStdString() : "<none>",
            static_cast<int>(DrawFace), GetVisible(), rect.left, rect.top,
            rect.get_width(), rect.get_height(),
            static_cast<unsigned int>(color), GetImageWidth(),
            GetImageHeight());
    }
    if(const char *trace = std::getenv("AETHERKIRI_MESSAGE_FRAME_TRACE");
       trace && *trace && *trace != '0' && GetName() == TJS_W("colorframe")) {
        spdlog::info(
            "message-frame fill layer={} face={} rect=({},{} {}x{}) color=0x{:08x}",
            GetName().AsNarrowStdString(), static_cast<int>(DrawFace), rect.left,
            rect.top, rect.get_width(), rect.get_height(), color);
    }
    if(const char *trace = std::getenv("AETHERKIRI_LAYER_FILL_TRACE");
       trace && *trace && *trace != '0') {
        const std::string name = GetName().AsStdString();
        const char *trace_all_value =
            std::getenv("AETHERKIRI_LAYER_FILL_TRACE_ALL");
        const bool trace_all = trace_all_value && *trace_all_value &&
            *trace_all_value != '0';
        static std::atomic<int> trace_count{0};
        if(trace_count.load(std::memory_order_relaxed) < 4000 &&
           (trace_all || name == "stage2" || name == "colorframe" ||
           name.find("メッセージレイヤ") != std::string::npos ||
           name == "表-背景")) {
            trace_count.fetch_add(1, std::memory_order_relaxed);
            spdlog::info(
                "Layer.fill-trace name={} face={} type={} rect=({},{} {}x{}) "
                "color=0x{:08x} neutral=0x{:08x} opacity={} visible={} "
                "size={}x{} image={} holdAlpha={}",
                name, static_cast<int>(DrawFace),
                ttstr(GetTypeNameString()).AsStdString(), rect.left, rect.top,
                rect.get_width(), rect.get_height(),
                static_cast<unsigned int>(color),
                static_cast<unsigned int>(GetNeutralColor()), GetOpacity(),
                GetVisible() ? "yes" : "no", GetImageWidth(),
                GetImageHeight(), MainImage ? "yes" : "no",
                GetHoldAlpha() ? "yes" : "no");
        }
    }
    // fill given rectangle with given "color"
    // this method does not do transparent coloring.

    tTVPRect destrect;
    if(!TVPIntersectRect(&destrect, rect, ClipRect))
        return; // out of the clipping rectangle

    if(DrawFace == dfAlpha || DrawFace == dfAddAlpha ||
       (DrawFace == dfOpaque && !HoldAlpha)) {
        // main and mask
        if(!MainImage)
            TVPThrowExceptionMessage(TVPNotDrawableLayerType);
        color = (color & 0xff000000) +
            (TVPToActualColor(color & 0xffffff) & 0xffffff);
        ImageModified = MainImage->Fill(destrect, color) || ImageModified;
    } else if(DrawFace == dfOpaque) {
        // main only
        if(!MainImage)
            TVPThrowExceptionMessage(TVPNotDrawableLayerType);
        color = TVPToActualColor(color);
        ImageModified =
            MainImage->FillColor(destrect, color, 255) || ImageModified;
    } else if(DrawFace == dfMask) {
        // mask only
        if(!MainImage)
            TVPThrowExceptionMessage(TVPNotDrawableLayerType);
        ImageModified =
            MainImage->FillMask(destrect, color & 0xff) || ImageModified;
    } else if(DrawFace == dfProvince) {
        // province
        color = color & 0xff;
        if(color) {
            if(!ProvinceImage)
                AllocateProvinceImage();
            if(ProvinceImage)
                ImageModified = ProvinceImage->Fill(destrect, color & 0xff) ||
                    ImageModified;
        } else {
            if(ProvinceImage) {
                if(destrect.left == 0 && destrect.top == 0 &&
                   destrect.right == (tjs_int)ProvinceImage->GetWidth() &&
                   destrect.bottom == (tjs_int)ProvinceImage->GetHeight()) {
                    // entire area of the province image will be
                    // filled with 0
                    DeallocateProvinceImage();
                    ImageModified = true;
                } else {
                    ImageModified =
                        ProvinceImage->Fill(destrect, color & 0xff) ||
                        ImageModified;
                }
            }
        }
    }

    if(ImageLeft != 0 || ImageTop != 0) {
        tTVPRect ur = destrect;
        ur.add_offsets(ImageLeft, ImageTop);
        Update(ur);
    } else {
        Update(destrect);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::ColorRect(const tTVPRect &rect, tjs_uint32 color,
                                 tjs_int opa) {
    // color given rectangle with given "color"

    tTVPRect destrect;
    if(!TVPIntersectRect(&destrect, rect, ClipRect))
        return; // out of the clipping rectangle

    switch(DrawFace) {
        case dfAlpha: // main and mask
            if(!MainImage)
                TVPThrowExceptionMessage(TVPNotDrawableLayerType);
            if(opa > 0) {
                color = TVPToActualColor(color);
                ImageModified =
                    MainImage->FillColorOnAlpha(destrect, color, opa) ||
                    ImageModified;
            } else {
                ImageModified = MainImage->RemoveConstOpacity(destrect, -opa) ||
                    ImageModified;
            }
            break;

        case dfAddAlpha: // additive alpha; main and mask
            if(!MainImage)
                TVPThrowExceptionMessage(TVPNotDrawableLayerType);
            if(opa >= 0) {
                color = TVPToActualColor(color);
                ImageModified =
                    MainImage->FillColorOnAddAlpha(destrect, color, opa) ||
                    ImageModified;
            } else {
                TVPThrowExceptionMessage(
                    TVPNegativeOpacityNotSupportedOnThisFace);
            }
            break;

        case dfOpaque: // main only
            if(!MainImage)
                TVPThrowExceptionMessage(TVPNotDrawableLayerType);
            color = TVPToActualColor(color);
            ImageModified =
                MainImage->FillColor(destrect, color, opa) || ImageModified;
            // note that tTVPBaseBitmap::FillColor always holds
            // destination alpha
            break;

        case dfMask: // mask ( opacity will be ignored )
            if(!MainImage)
                TVPThrowExceptionMessage(TVPNotDrawableLayerType);
            ImageModified =
                MainImage->FillMask(destrect, color & 0xff) || ImageModified;
            break;

        case dfProvince: // province ( opacity will be ignored )
            color = color & 0xff;
            if(color) {
                if(!ProvinceImage)
                    AllocateProvinceImage();
                if(ProvinceImage)
                    ImageModified =
                        ProvinceImage->Fill(destrect, color & 0xff) ||
                        ImageModified;
            } else {
                if(ProvinceImage) {
                    if(destrect.left == 0 && destrect.top == 0 &&
                       destrect.right == (tjs_int)ProvinceImage->GetWidth() &&
                       destrect.bottom == (tjs_int)ProvinceImage->GetHeight()) {
                        // entire area of the province image will be
                        // filled with 0
                        DeallocateProvinceImage();
                        ImageModified = true;
                    } else {
                        ImageModified =
                            ProvinceImage->Fill(destrect, color & 0xff) ||
                            ImageModified;
                    }
                }
            }
            break;

        case dfAuto:
            break;
    }

    if(ImageLeft != 0 || ImageTop != 0) {
        tTVPRect ur = destrect;
        ur.add_offsets(ImageLeft, ImageTop);
        Update(ur);
    } else {
        Update(destrect);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::DrawText(tjs_int x, tjs_int y, const ttstr &text,
                                tjs_uint32 color, tjs_int opa, bool aa,
                                tjs_int shadowlevel, tjs_uint32 shadowcolor,
                                tjs_int shadowwidth, tjs_int shadowofsx,
                                tjs_int shadowofsy) {
    if(TVPLayerErrorTraceTake() &&
       (text.AsStdString().find("直太") != std::string::npos ||
        GetName().AsStdString().find("直太") != std::string::npos)) {
        const auto *parent = GetParent();
        spdlog::warn(
            "Layer.error-placeholder text layer={} parent={} face={} visible={} pos=({},{}), text={} color=0x{:08x} opa={} size={}x{}",
            GetName().AsStdString(),
            parent ? parent->GetName().AsStdString() : "<none>",
            static_cast<int>(DrawFace), GetVisible(), x, y,
            text.AsStdString(), static_cast<unsigned int>(color), opa,
            GetImageWidth(), GetImageHeight());
    }
    // draw text
    if(!MainImage)
        TVPThrowExceptionMessage(TVPNotDrawableLayerType);

    tTVPBBBltMethod met;

    switch(DrawFace) {
        case dfAlpha:
            met = bmAlphaOnAlpha;
            break;
        case dfAddAlpha:
            if(opa < 0)
                TVPThrowExceptionMessage(
                    TVPNegativeOpacityNotSupportedOnThisFace);
            met = bmAlphaOnAddAlpha;
            break;
        case dfOpaque:
            met = bmAlpha;
            break;
        default:
            TVPThrowExceptionMessage(TVPNotDrawableFaceType, TJS_W("drawText"));
    }

    ApplyFont();

    tTVPComplexRect r;

    color = TVPToActualColor(color);

    MainImage->DrawText(ClipRect, x, y, text, TVP_REVRGB(color), met, opa,
                        HoldAlpha, aa, shadowlevel, TVP_REVRGB(shadowcolor),
                        shadowwidth, shadowofsx, shadowofsy, &r);

    if(r.GetCount())
        ImageModified = true;

    if(ImageLeft != 0 || ImageTop != 0) {
        r.AddOffsets(ImageLeft, ImageTop);
    }
    Update(r);
}

void tTJSNI_BaseLayer::DrawTextVerticalGradient(
    tjs_int x, tjs_int y, const ttstr &text, tjs_uint32 topcolor,
    tjs_uint32 bottomcolor, tjs_int opa, bool aa, tjs_int gradientHeight) {
    if(!MainImage)
        TVPThrowExceptionMessage(TVPNotDrawableLayerType);

    tTVPBBBltMethod met;
    switch(DrawFace) {
        case dfAlpha:
            met = bmAlphaOnAlpha;
            break;
        case dfAddAlpha:
            if(opa < 0)
                TVPThrowExceptionMessage(
                    TVPNegativeOpacityNotSupportedOnThisFace);
            met = bmAlphaOnAddAlpha;
            break;
        case dfOpaque:
            met = bmAlpha;
            break;
        default:
            TVPThrowExceptionMessage(TVPNotDrawableFaceType, TJS_W("drawText"));
    }

    ApplyFont();

    tTVPComplexRect r;
    topcolor = TVPToActualColor(topcolor);
    bottomcolor = TVPToActualColor(bottomcolor);

    MainImage->DrawTextVerticalGradient(ClipRect, x, y, text,
                                        TVP_REVRGB(topcolor),
                                        TVP_REVRGB(bottomcolor), met, opa,
                                        HoldAlpha, aa, gradientHeight, &r);

    if(r.GetCount())
        ImageModified = true;

    if(ImageLeft != 0 || ImageTop != 0)
        r.AddOffsets(ImageLeft, ImageTop);
    Update(r);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::DrawGlyph(tjs_int x, tjs_int y, iTJSDispatch2 *glyph,
                                 tjs_uint32 color, tjs_int opa, bool aa,
                                 tjs_int shadowlevel, tjs_uint32 shadowcolor,
                                 tjs_int shadowwidth, tjs_int shadowofsx,
                                 tjs_int shadowofsy) {
    // draw text
    if(!MainImage)
        TVPThrowExceptionMessage(TVPNotDrawableLayerType);
    tTVPBBBltMethod met = bmCopy;

    switch(DrawFace) {
        case dfAlpha:
            met = bmAlphaOnAlpha;
            break;
        case dfAddAlpha:
            if(opa < 0)
                TVPThrowExceptionMessage(
                    TVPNegativeOpacityNotSupportedOnThisFace);
            met = bmAlphaOnAddAlpha;
            break;
        case dfOpaque:
            met = bmAlpha;
            break;
        default:
            TVPThrowExceptionMessage(TVPNotDrawableFaceType,
                                     TJS_W("drawGlyph"));
    }

    ApplyFont();

    tTVPComplexRect r;

    color = TVPToActualColor(color);

    MainImage->DrawGlyph(glyph, ClipRect, x, y, color, met, opa, HoldAlpha, aa,
                         shadowlevel, shadowcolor, shadowwidth, shadowofsx,
                         shadowofsy, &r);

    if(r.GetCount())
        ImageModified = true;

    if(ImageLeft != 0 || ImageTop != 0) {
        r.AddOffsets(ImageLeft, ImageTop);
    }
    Update(r);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::PiledCopy(tjs_int dx, tjs_int dy, tTJSNI_BaseLayer *src,
                                 const tTVPRect &srcrect) {
    // rectangle copy of piled layer image

    // this can transfer the piled image of the source layer
    // this ignores Drawface of this, or DrawFace of the source layer.
    // this is affected by source layer type.

    if(!MainImage)
        TVPThrowExceptionMessage(TVPNotDrawableLayerType);
    if(!src->MainImage)
        TVPThrowExceptionMessage(TVPSourceLayerHasNoImage);

    tTVPRect rect;
    if(!ClipDestPointAndSrcRect(dx, dy, rect, srcrect))
        return; // out of the clipping rect
    src->IncCacheEnabledCount(); // enable cache
    try {
        iTVPBaseBitmap *bmp = src->Complete(rect);
        tTVPRect rc(rect);
        if(IsGPU()) {
            rc.set_offsets(0, 0);
        }
        ImageModified =
            MainImage->CopyRect(dx, dy, bmp, rc,
                                TVP_BB_COPY_MAIN | TVP_BB_COPY_MASK) ||
            ImageModified;
    } catch(...) {
        src->DecCacheEnabledCount();
        throw;
    }
    src->DecCacheEnabledCount(); // disable cache

    tTVPRect ur = rect;
    ur.set_offsets(dx, dy);
    if(ImageLeft != 0 || ImageTop != 0) {
        ur.add_offsets(ImageLeft, ImageTop);
        Update(ur);
    } else {
        Update(ur);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::CopyRect(tjs_int dx, tjs_int dy, iTVPBaseBitmap *src,
                                iTVPBaseBitmap *provincesrc,
                                const tTVPRect &srcrect) {
    // copy rectangle

    // this method switches automatically backward or forward copy,
    // when the distination and the source each other are overlapped.

    tTVPRect rect;
    if(!ClipDestPointAndSrcRect(dx, dy, rect, srcrect))
        return; // out of the clipping rect
    const bool copy_trace = TVPCopyTraceThumbnail(src, rect) &&
                            TVPCopyTraceTake();
    if(copy_trace) {
        const auto sample = [](const iTVPBaseBitmap *bitmap, int x,
                               int y) -> tjs_uint32 {
            if(!bitmap || x < 0 || y < 0 ||
               x >= static_cast<int>(bitmap->GetWidth()) ||
               y >= static_cast<int>(bitmap->GetHeight()))
                return 0;
            return bitmap->GetPoint(x, y);
        };
        spdlog::info(
            "Layer.copyTrace.before target={} parent={} face={} dest=({},{} {}x{}) srcSize={}x{} src00=0x{:08x} srcCenter=0x{:08x} dst00=0x{:08x}",
            GetName().AsStdString(),
            Parent ? Parent->GetName().AsStdString() : "<none>",
            TVPLayerDebugDrawFaceName(DrawFace), dx, dy, rect.get_width(),
            rect.get_height(), src ? static_cast<int>(src->GetWidth()) : -1,
            src ? static_cast<int>(src->GetHeight()) : -1,
            sample(src, rect.left, rect.top),
            sample(src, rect.left + rect.get_width() / 2,
                   rect.top + rect.get_height() / 2),
            sample(MainImage, dx, dy));
    }
    const bool dialog_trace = TVPDialogLayerTraceEnabled(this) &&
                              TVPDialogLayerTraceTake();
    if(dialog_trace) {
        spdlog::info(
            "Dialog.copyRect target={} face={} dest=({},{} {}x{}) srcSize={}x{} srcRect=({},{} {}x{}) targetSize={}x{} before=[0x{:08x},0x{:08x},0x{:08x},0x{:08x}]",
            GetName().AsStdString(), TVPLayerDebugDrawFaceName(DrawFace), dx,
            dy, rect.get_width(), rect.get_height(),
            src ? static_cast<int>(src->GetWidth()) : -1,
            src ? static_cast<int>(src->GetHeight()) : -1, rect.left, rect.top,
            rect.get_width(), rect.get_height(), GetImageWidth(),
            GetImageHeight(), TVPStage2Sample(this, dx, dy),
            TVPStage2Sample(this, dx + rect.get_width() / 2,
                            dy + rect.get_height() / 2),
            TVPStage2Sample(this, 14, 1241),
            TVPStage2Sample(this, 200, 1270));
    }
    if(const char *trace = std::getenv("AETHERKIRI_ALPHA_TRACE");
       trace && *trace && *trace != '0' && src && src->GetWidth() > 1000) {
        spdlog::info(
            "alpha copy target={} face={} targetSize={}x{} srcSize={}x{} srcRect={}x{}",
            GetName().AsStdString(), TVPLayerDebugDrawFaceName(DrawFace),
            GetImageWidth(), GetImageHeight(), src->GetWidth(),
            src->GetHeight(), rect.get_width(), rect.get_height());
    }
    if(TVPLayerDebugShouldLogBitmap(src, rect)) {
        spdlog::info(
            "Layer.copyRect face={} dest=({},{} {}x{}) srcSize={}x{} srcRect=({},{} {}x{})",
            TVPLayerDebugDrawFaceName(DrawFace), dx, dy, rect.get_width(),
            rect.get_height(), src ? static_cast<int>(src->GetWidth()) : -1,
            src ? static_cast<int>(src->GetHeight()) : -1, rect.left,
            rect.top, rect.get_width(), rect.get_height());
    }

    switch(DrawFace) {
        case dfAlpha:
        case dfAddAlpha:
            if(!MainImage)
                TVPThrowExceptionMessage(TVPNotDrawableLayerType);
            if(!src)
                TVPThrowExceptionMessage(TVPSourceLayerHasNoImage);
            ImageModified =
                MainImage->CopyRect(dx, dy, src, rect,
                                    TVP_BB_COPY_MAIN | TVP_BB_COPY_MASK) ||
                ImageModified;
            break;

        case dfOpaque:
            if(!MainImage)
                TVPThrowExceptionMessage(TVPNotDrawableLayerType);
            if(!src)
                TVPThrowExceptionMessage(TVPSourceLayerHasNoImage);
            ImageModified =
                MainImage->CopyRect(
                    dx, dy, src, rect,
                    HoldAlpha ? TVP_BB_COPY_MAIN
                              : (TVP_BB_COPY_MAIN | TVP_BB_COPY_MASK)) ||
                ImageModified;
            break;

        case dfMask:
            if(!MainImage)
                TVPThrowExceptionMessage(TVPNotDrawableLayerType);
            if(!src)
                TVPThrowExceptionMessage(TVPSourceLayerHasNoImage);
            ImageModified =
                MainImage->CopyRect(dx, dy, src, rect, TVP_BB_COPY_MASK) ||
                ImageModified;
            break;

        case dfProvince:
            if(!provincesrc) {
                // source province image is nullptr;
                // fill destination with zero
                if(ProvinceImage)
                    ProvinceImage->Fill(rect, 0);
                ImageModified = true;
            } else {
                // province image is not created if the image is not
                // needed allocate province image
                if(!ProvinceImage)
                    AllocateProvinceImage();
                // then copy
                ImageModified =
                    ProvinceImage->CopyRect(dx, dy, provincesrc, rect) ||
                    ImageModified;
            }
            break;
        case dfAuto:
            break;
    }

    if(dialog_trace) {
        spdlog::info(
            "Dialog.copyRect.after target={} dest=({},{} {}x{}) after=[0x{:08x},0x{:08x},0x{:08x},0x{:08x}]",
            GetName().AsStdString(), dx, dy, rect.get_width(),
            rect.get_height(), TVPStage2Sample(this, dx, dy),
            TVPStage2Sample(this, dx + rect.get_width() / 2,
                            dy + rect.get_height() / 2),
            TVPStage2Sample(this, 14, 1241),
            TVPStage2Sample(this, 200, 1270));
    }

    if(copy_trace) {
        const auto sample = [](const iTVPBaseBitmap *bitmap, int x,
                               int y) -> tjs_uint32 {
            if(!bitmap || x < 0 || y < 0 ||
               x >= static_cast<int>(bitmap->GetWidth()) ||
               y >= static_cast<int>(bitmap->GetHeight()))
                return 0;
            return bitmap->GetPoint(x, y);
        };
        spdlog::info("Layer.copyTrace.after target={} dst00=0x{:08x} dstCenter=0x{:08x}",
                     GetName().AsStdString(), sample(MainImage, dx, dy),
                     sample(MainImage, dx + rect.get_width() / 2,
                            dy + rect.get_height() / 2));
    }

    tTVPRect ur = rect;
    ur.set_offsets(dx, dy);
    if(ImageLeft != 0 || ImageTop != 0) {
        ur.add_offsets(ImageLeft, ImageTop);
        Update(ur);
    } else {
        Update(ur);
    }
}

//---------------------------------------------------------------------------
bool tTJSNI_BaseLayer::Copy9Patch(const iTVPBaseBitmap *src, tTVPRect &margin) {
    if(!MainImage)
        TVPThrowExceptionMessage(TVPNotDrawableLayerType);
    ImageModified = MainImage->Copy9Patch(src, margin);
    if(ImageModified) {
        tTVPRect ur(0, 0, GetImageWidth(), GetImageHeight());
        if(ImageLeft != 0 || ImageTop != 0) {
            ur.add_offsets(ImageLeft, ImageTop);
            Update(ur);
        } else {
            Update(ur);
        }
    }
    return ImageModified;
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::StretchCopy(const tTVPRect &destrect,
                                   iTVPBaseBitmap *src, const tTVPRect &srcrect,
                                   tTVPBBStretchType type, tjs_real typeopt) {
    // stretching copy
    if(TVPLayerDebugShouldLogBitmap(src, srcrect)) {
        spdlog::info(
            "Layer.stretchCopy face={} dest=({},{} {}x{}) srcSize={}x{} srcRect=({},{} {}x{}) type={} opt={}",
            TVPLayerDebugDrawFaceName(DrawFace), destrect.left, destrect.top,
            destrect.get_width(),
            destrect.get_height(), src ? static_cast<int>(src->GetWidth()) : -1,
            src ? static_cast<int>(src->GetHeight()) : -1, srcrect.left,
            srcrect.top, srcrect.get_width(), srcrect.get_height(),
            static_cast<int>(type), typeopt);
    }

    tTVPRect ur = destrect;
    if(ur.right < ur.left)
        std::swap(ur.right, ur.left);
    if(ur.bottom < ur.top)
        std::swap(ur.bottom, ur.top);
    if(!TVPIntersectRect(&ur, ur, ClipRect))
        return; // out of the clipping rectangle

    switch(DrawFace) {
        case dfAlpha:
        case dfAddAlpha:
            if(!MainImage)
                TVPThrowExceptionMessage(TVPNotDrawableLayerType);
            if(!src)
                TVPThrowExceptionMessage(TVPSourceLayerHasNoImage);
            ImageModified =
                MainImage->StretchBlt(ClipRect, destrect, src, srcrect, bmCopy,
                                      255, false, type, typeopt) ||
                ImageModified;
            break;

        case dfOpaque:
            if(!MainImage)
                TVPThrowExceptionMessage(TVPNotDrawableLayerType);
            if(!src)
                TVPThrowExceptionMessage(TVPSourceLayerHasNoImage);
            ImageModified =
                MainImage->StretchBlt(ClipRect, destrect, src, srcrect, bmCopy,
                                      255, HoldAlpha, type, typeopt) ||
                ImageModified;
            break;

        default:
            TVPThrowExceptionMessage(TVPNotDrawableFaceType,
                                     TJS_W("stretchCopy"));
    }

    if(ImageLeft != 0 || ImageTop != 0) {
        ur.add_offsets(ImageLeft, ImageTop);
        Update(ur);
    } else {
        Update(ur);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::AffineCopy(const t2DAffineMatrix &matrix,
                                  iTVPBaseBitmap *src, const tTVPRect &srcrect,
                                  tTVPBBStretchType type, bool clear) {
    // affine copy
    if(TVPLayerDebugShouldLogBitmap(src, srcrect)) {
        spdlog::info(
            "Layer.affineCopy.matrix face={} srcSize={}x{} srcRect=({},{} {}x{}) mat=[{},{},{},{},{},{}] type={} clear={}",
            TVPLayerDebugDrawFaceName(DrawFace),
            src ? static_cast<int>(src->GetWidth()) : -1,
            src ? static_cast<int>(src->GetHeight()) : -1, srcrect.left,
            srcrect.top, srcrect.get_width(), srcrect.get_height(), matrix.a,
            matrix.b, matrix.c, matrix.d, matrix.tx, matrix.ty,
            static_cast<int>(type), clear ? 1 : 0);
    }

    tTVPRect updaterect;
    bool updated;

    switch(DrawFace) {
        case dfAlpha:
        case dfAddAlpha: {
            if(!MainImage)
                TVPThrowExceptionMessage(TVPNotDrawableLayerType);
            if(!src)
                TVPThrowExceptionMessage(TVPSourceLayerHasNoImage);
            updated = MainImage->AffineBlt(ClipRect, src, srcrect, matrix,
                                           bmCopy, 255, &updaterect, false,
                                           type, clear, NeutralColor);
            break;
        }

        case dfOpaque: {
            if(!MainImage)
                TVPThrowExceptionMessage(TVPNotDrawableLayerType);
            if(!src)
                TVPThrowExceptionMessage(TVPSourceLayerHasNoImage);
            updated = MainImage->AffineBlt(ClipRect, src, srcrect, matrix,
                                           bmCopy, 255, &updaterect, HoldAlpha,
                                           type, clear, NeutralColor);
            break;
        }

        default:
            TVPThrowExceptionMessage(TVPNotDrawableFaceType,
                                     TJS_W("affineCopy"));
    }

    ImageModified = updated || ImageModified;

    if(TVPLayerDebugShouldLogBitmap(src, srcrect)) {
        spdlog::info(
            "Layer.affineCopy.matrix result updated={} updateRect=({},{} {}x{}) imageOfs=({}, {}) clip=({},{} {}x{}) layer={} pos=({}, {}) size={}x{}",
            updated ? 1 : 0, updaterect.left, updaterect.top,
            updaterect.get_width(), updaterect.get_height(), ImageLeft,
            ImageTop, ClipRect.left, ClipRect.top, ClipRect.get_width(),
            ClipRect.get_height(), GetName().AsStdString(), GetLeft(),
            GetTop(), GetWidth(), GetHeight());
    }

    if(updated) {
        updaterect.add_offsets(ImageLeft, ImageTop);
        Update(updaterect);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::AffineCopy(const tTVPPointD *points, iTVPBaseBitmap *src,
                                  const tTVPRect &srcrect,
                                  tTVPBBStretchType type, bool clear) {
    // affine copy
    if(TVPLayerDebugShouldLogBitmap(src, srcrect)) {
        spdlog::info(
            "Layer.affineCopy.points face={} srcSize={}x{} srcRect=({},{} {}x{}) p0=({},{}) p1=({},{}) p2=({},{}) type={} clear={}",
            TVPLayerDebugDrawFaceName(DrawFace),
            src ? static_cast<int>(src->GetWidth()) : -1,
            src ? static_cast<int>(src->GetHeight()) : -1, srcrect.left,
            srcrect.top, srcrect.get_width(), srcrect.get_height(),
            points ? points[0].x : 0.0, points ? points[0].y : 0.0,
            points ? points[1].x : 0.0, points ? points[1].y : 0.0,
            points ? points[2].x : 0.0, points ? points[2].y : 0.0,
            static_cast<int>(type), clear ? 1 : 0);
    }

    tTVPRect updaterect;
    bool updated;

    switch(DrawFace) {
        case dfAlpha:
        case dfAddAlpha: {
            if(!MainImage)
                TVPThrowExceptionMessage(TVPNotDrawableLayerType);
            if(!src)
                TVPThrowExceptionMessage(TVPSourceLayerHasNoImage);
            updated = MainImage->AffineBlt(ClipRect, src, srcrect, points,
                                           bmCopy, 255, &updaterect, false,
                                           type, clear, NeutralColor);
            break;
        }

        case dfOpaque: {
            if(!MainImage)
                TVPThrowExceptionMessage(TVPNotDrawableLayerType);
            if(!src)
                TVPThrowExceptionMessage(TVPSourceLayerHasNoImage);
            updated = MainImage->AffineBlt(ClipRect, src, srcrect, points,
                                           bmCopy, 255, &updaterect, HoldAlpha,
                                           type, clear, NeutralColor);
            break;
        }

        default:
            TVPThrowExceptionMessage(TVPNotDrawableFaceType,
                                     TJS_W("affineCopy"));
    }

    ImageModified = updated || ImageModified;

    if(TVPLayerDebugShouldLogBitmap(src, srcrect)) {
        spdlog::info(
            "Layer.affineCopy.points result updated={} updateRect=({},{} {}x{}) imageOfs=({}, {}) clip=({},{} {}x{}) layer={} pos=({}, {}) size={}x{}",
            updated ? 1 : 0, updaterect.left, updaterect.top,
            updaterect.get_width(), updaterect.get_height(), ImageLeft,
            ImageTop, ClipRect.left, ClipRect.top, ClipRect.get_width(),
            ClipRect.get_height(), GetName().AsStdString(), GetLeft(),
            GetTop(), GetWidth(), GetHeight());
    }

    if(updated) {
        updaterect.add_offsets(ImageLeft, ImageTop);
        Update(updaterect);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::BezierPatchCopy(const tTVPPointD *points, tjs_int divx,
                                       tjs_int divy, iTVPBaseBitmap *src,
                                       const tTVPRect &srcrect,
                                       tTVPBBStretchType type, bool clear) {
    if(!points || !src || divx < 2 || divy < 2)
        return;

    const auto cubicBlend = [](double p0, double p1, double p2, double p3,
                               double t) {
        const double mt = 1.0 - t;
        return mt * mt * mt * p0 + 3.0 * mt * mt * t * p1 +
            3.0 * mt * t * t * p2 + t * t * t * p3;
    };
    const auto samplePatch = [&](double u, double v) -> tTVPPointD {
        tTVPPointD curve[4];
        for(int row = 0; row < 4; ++row) {
            const auto *cp = &points[row * 4];
            curve[row].x = cubicBlend(cp[0].x, cp[1].x, cp[2].x, cp[3].x, u);
            curve[row].y = cubicBlend(cp[0].y, cp[1].y, cp[2].y, cp[3].y, u);
        }
        return {
            cubicBlend(curve[0].x, curve[1].x, curve[2].x, curve[3].x, v),
            cubicBlend(curve[0].y, curve[1].y, curve[2].y, curve[3].y, v),
        };
    };

    std::vector<tTVPPointD> tessellated;
    tessellated.reserve(static_cast<size_t>(divx) * static_cast<size_t>(divy));
    for(tjs_int y = 0; y < divy; ++y) {
        const double v = divy > 1
            ? static_cast<double>(y) / static_cast<double>(divy - 1)
            : 0.0;
        for(tjs_int x = 0; x < divx; ++x) {
            const double u = divx > 1
                ? static_cast<double>(x) / static_cast<double>(divx - 1)
                : 0.0;
            tessellated.push_back(samplePatch(u, v));
        }
    }

    MeshCopy(tessellated.data(), divx, divy, src, srcrect, type, clear);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::MeshCopy(const tTVPPointD *points, tjs_int divx,
                                tjs_int divy, iTVPBaseBitmap *src,
                                const tTVPRect &srcrect,
                                tTVPBBStretchType type, bool clear) {
    if(!points || !src || divx < 2 || divy < 2)
        return;

    if(DrawFace != dfAlpha && DrawFace != dfAddAlpha && DrawFace != dfOpaque)
        TVPThrowExceptionMessage(TVPNotDrawableFaceType, TJS_W("meshCopy"));
    if(!MainImage)
        TVPThrowExceptionMessage(TVPNotDrawableLayerType);

    if(clear)
        FillRect(ClipRect, NeutralColor);

    const double srcLeft = static_cast<double>(srcrect.left);
    const double srcTop = static_cast<double>(srcrect.top);
    const double srcWidth = static_cast<double>(srcrect.right - srcrect.left);
    const double srcHeight = static_cast<double>(srcrect.bottom - srcrect.top);

    std::vector<tTVPPointD> destinationPoints;
    std::vector<tTVPPointD> sourcePoints;
    destinationPoints.reserve(
        static_cast<size_t>(divx - 1) * static_cast<size_t>(divy - 1) * 6u);
    sourcePoints.reserve(destinationPoints.capacity());
    for(tjs_int y = 0; y < divy - 1; ++y) {
        const double v0 = static_cast<double>(y) / static_cast<double>(divy - 1);
        const double v1 = static_cast<double>(y + 1) /
            static_cast<double>(divy - 1);
        for(tjs_int x = 0; x < divx - 1; ++x) {
            const double u0 = static_cast<double>(x) /
                static_cast<double>(divx - 1);
            const double u1 = static_cast<double>(x + 1) /
                static_cast<double>(divx - 1);

            const double sourceLeft = srcLeft + srcWidth * u0;
            const double sourceTop = srcTop + srcHeight * v0;
            const double sourceRight = srcLeft + srcWidth * u1;
            const double sourceBottom = srcTop + srcHeight * v1;
            if(sourceRight <= sourceLeft || sourceBottom <= sourceTop)
                continue;

            const auto &p0 = points[y * divx + x];
            const auto &p1 = points[y * divx + x + 1];
            const auto &p2 = points[(y + 1) * divx + x];
            const auto &p3 = points[(y + 1) * divx + x + 1];
            destinationPoints.insert(destinationPoints.end(),
                                     {p0, p1, p2, p1, p2, p3});
            sourcePoints.insert(sourcePoints.end(), {
                {sourceLeft, sourceTop},
                {sourceRight, sourceTop},
                {sourceLeft, sourceBottom},
                {sourceRight, sourceTop},
                {sourceLeft, sourceBottom},
                {sourceRight, sourceBottom}
            });
        }
    }

    if(destinationPoints.empty()) return;

    iTVPRenderManager *manager = MainImage->GetRenderManager();
    const auto stretchType = static_cast<tTVPBBStretchType>(type & stTypeMask);
    manager->SetParameterInt(manager->EnumParameterID("StretchType"),
                             static_cast<int>(stretchType));
    const bool holdDestinationAlpha =
        DrawFace == dfOpaque ? HoldAlpha : false;
    iTVPRenderMethod *renderMethod = manager->GetRenderMethod(
        255, holdDestinationAlpha, bmCopy);
    if(!renderMethod) return;

    iTVPTexture2D *sourceTexture = src->GetTexture();
    iTVPTexture2D *convertedSource = nullptr;
    if(manager != src->GetRenderManager()) {
        const void *pixels = sourceTexture->GetScanLineForRead(0);
        if(pixels) {
            convertedSource = manager->CreateTexture2D(
                pixels, sourceTexture->GetPitch(), src->GetWidth(),
                src->GetHeight(), TVPTextureFormat::RGBA);
            sourceTexture = convertedSource;
        }
    }
    if(!sourceTexture) {
        if(convertedSource) convertedSource->Release();
        return;
    }

    // Keep both triangles of every cell in one render operation.  Calling
    // AffineBlt separately for each half treats each three-point input as a
    // full parallelogram, so the second half overwrites the first and exposes
    // alternating triangles when the result is later used as an alpha mask.
    constexpr size_t kTrianglesPerBatch = 64;
    const size_t triangleCount = destinationPoints.size() / 3u;
    bool anyUpdated = false;
    tTVPRect totalUpdateRect;
    for(size_t firstTriangle = 0; firstTriangle < triangleCount;
        firstTriangle += kTrianglesPerBatch) {
        const size_t batchTriangles = std::min(
            kTrianglesPerBatch, triangleCount - firstTriangle);
        const size_t firstPoint = firstTriangle * 3u;
        const size_t batchPoints = batchTriangles * 3u;
        double minX = destinationPoints[firstPoint].x;
        double minY = destinationPoints[firstPoint].y;
        double maxX = minX;
        double maxY = minY;
        for(size_t point = 1; point < batchPoints; ++point) {
            const auto &value = destinationPoints[firstPoint + point];
            minX = std::min(minX, value.x);
            minY = std::min(minY, value.y);
            maxX = std::max(maxX, value.x);
            maxY = std::max(maxY, value.y);
        }
        tTVPRect batchClip(
            std::max(ClipRect.left,
                     static_cast<tjs_int>(std::floor(minX)) - 1),
            std::max(ClipRect.top,
                     static_cast<tjs_int>(std::floor(minY)) - 1),
            std::min(ClipRect.right,
                     static_cast<tjs_int>(std::ceil(maxX)) + 1),
            std::min(ClipRect.bottom,
                     static_cast<tjs_int>(std::ceil(maxY)) + 1));
        if(batchClip.right <= batchClip.left ||
           batchClip.bottom <= batchClip.top) {
            continue;
        }
        tRenderTexQuadArray::Element sourceElement(
            sourceTexture, sourcePoints.data() + firstPoint);
        iTVPTexture2D *referenceTexture = MainImage->GetTexture();
        iTVPTexture2D *targetTexture = MainImage->GetTextureForRender(
            renderMethod->IsBlendTarget(), &batchClip);
        manager->OperateTriangles(
            renderMethod, static_cast<int>(batchTriangles), targetTexture,
            referenceTexture, batchClip,
            destinationPoints.data() + firstPoint,
            tRenderTexQuadArray(&sourceElement, 1));
        if(!anyUpdated) {
            totalUpdateRect = batchClip;
            anyUpdated = true;
        } else {
            totalUpdateRect.do_union(batchClip);
        }
    }
    if(convertedSource) convertedSource->Release();
    if(anyUpdated) {
        ImageModified = true;
        totalUpdateRect.add_offsets(ImageLeft, ImageTop);
        Update(totalUpdateRect);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::PileRect(tjs_int dx, tjs_int dy, tTJSNI_BaseLayer *src,
                                const tTVPRect &srcrect, tjs_int opacity) {
    // obsoleted (use OperateRect)

    // pile rectangle ( pixel alpha blend )

    // piled destination is determined by Drawface (not LayerType).
    // dfAlpha: destination alpha is considered
    // dfOpaque: destination alpha is ignored ( treated as full opaque
    // ) dfMask or dfProvince : causes an error this method ignores
    // soruce layer's LayerType or DrawFace. the destination alpha is
    // held on dfAlpha if 'HoldAlpha' is true, otherwide the alpha
    // information is destroyed.

    if(DrawFace != dfAlpha && DrawFace != dfOpaque) {
        TVPThrowExceptionMessage(TVPNotDrawableFaceType, TJS_W("pileRect"));
    }

    tTVPRect rect;
    if(!ClipDestPointAndSrcRect(dx, dy, rect, srcrect))
        return; // out of the clipping rect

    switch(DrawFace) {
        case dfAlpha:
            if(!MainImage)
                TVPThrowExceptionMessage(TVPNotDrawableLayerType);
            if(!src->MainImage)
                TVPThrowExceptionMessage(TVPSourceLayerHasNoImage);
            ImageModified =
                MainImage->Blt(dx, dy, src->MainImage, rect, bmAlphaOnAlpha,
                               opacity, HoldAlpha) ||
                ImageModified;
            break;

        case dfOpaque:
            if(!MainImage)
                TVPThrowExceptionMessage(TVPNotDrawableLayerType);
            if(!src->MainImage)
                TVPThrowExceptionMessage(TVPSourceLayerHasNoImage);
            ImageModified = MainImage->Blt(dx, dy, src->MainImage, rect,
                                           bmAlpha, opacity, HoldAlpha) ||
                ImageModified;
            break;

        default:
            break;
    }

    tTVPRect ur = rect;
    ur.set_offsets(dx, dy);
    if(ImageLeft != 0 || ImageTop != 0) {
        ur.add_offsets(ImageLeft, ImageTop);
        Update(ur);
    } else {
        Update(ur);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::BlendRect(tjs_int dx, tjs_int dy, tTJSNI_BaseLayer *src,
                                 const tTVPRect &srcrect, tjs_int opacity) {
    // obsoleted (use OperateRect)

    // blend rectangle ( constant alpha blend )

    // mostly the same as 'PileRect', but this does treat src as
    // completely opaque image.

    if(DrawFace != dfAlpha && DrawFace != dfOpaque) {
        TVPThrowExceptionMessage(TVPNotDrawableFaceType, TJS_W("blendRect"));
    }

    tTVPRect rect;
    if(!ClipDestPointAndSrcRect(dx, dy, rect, srcrect))
        return; // out of the clipping rect

    switch(DrawFace) {
        case dfAlpha:
            if(!MainImage)
                TVPThrowExceptionMessage(TVPNotDrawableLayerType);
            if(!src->MainImage)
                TVPThrowExceptionMessage(TVPSourceLayerHasNoImage);
            ImageModified = MainImage->Blt(dx, dy, src->MainImage, rect,
                                           bmCopyOnAlpha, opacity, HoldAlpha) ||
                ImageModified;
            break;

        case dfOpaque:
            if(!MainImage)
                TVPThrowExceptionMessage(TVPNotDrawableLayerType);
            if(!src->MainImage)
                TVPThrowExceptionMessage(TVPSourceLayerHasNoImage);
            ImageModified = MainImage->Blt(dx, dy, src->MainImage, rect, bmCopy,
                                           opacity, HoldAlpha) ||
                ImageModified;
            break;

        default:
            break;
    }

    tTVPRect ur = rect;
    ur.set_offsets(dx, dy);
    if(ImageLeft != 0 || ImageTop != 0) {
        ur.add_offsets(ImageLeft, ImageTop);
        Update(ur);
    } else {
        Update(ur);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::OperateRect(tjs_int dx, tjs_int dy, iTVPBaseBitmap *src,
                                   const tTVPRect &srcrect,
                                   tTVPBlendOperationMode mode,
                                   tjs_int opacity) {
    // operate on rectangle ( add/sub/mul/div and others )
    tTVPRect rect;
    if(!ClipDestPointAndSrcRect(dx, dy, rect, srcrect))
        return; // out of the clipping rect
    const bool dialog_trace = TVPDialogLayerTraceEnabled(this) &&
                              TVPDialogLayerTraceTake();
    if(dialog_trace) {
        spdlog::info(
            "Dialog.operateRect target={} face={} dest=({},{} {}x{}) srcSize={}x{} srcRect=({},{} {}x{}) targetSize={}x{} before=[0x{:08x},0x{:08x},0x{:08x},0x{:08x}]",
            GetName().AsStdString(), TVPLayerDebugDrawFaceName(DrawFace), dx,
            dy, rect.get_width(), rect.get_height(),
            src ? static_cast<int>(src->GetWidth()) : -1,
            src ? static_cast<int>(src->GetHeight()) : -1, rect.left, rect.top,
            rect.get_width(), rect.get_height(), GetImageWidth(),
            GetImageHeight(), TVPStage2Sample(this, dx, dy),
            TVPStage2Sample(this, dx + rect.get_width() / 2,
                            dy + rect.get_height() / 2),
            TVPStage2Sample(this, 14, 1241),
            TVPStage2Sample(this, 200, 1270));
    }

    // It does not throw an exception in this case perhaps
    if(mode == omAuto)
        TVPThrowExceptionMessage(TVPCannotAcceptModeAuto);

    // convert tTVPBlendOperationMode to tTVPBBBltMethod
    tTVPBBBltMethod met;
    if(!GetBltMethodFromOperationModeAndDrawFace(met, mode)) {
        // unknown blt mode
        TVPThrowExceptionMessage(TVPNotDrawableFaceType, TJS_W("operateRect"));
    }

    if(!MainImage)
        TVPThrowExceptionMessage(TVPNotDrawableLayerType);
    if(!src)
        TVPThrowExceptionMessage(TVPSourceLayerHasNoImage);
    if(TVPLayerDebugShouldLogBitmap(src, rect)) {
        spdlog::info(
            "Layer.operateRect face={} dest=({},{} {}x{}) srcSize={}x{} srcRect=({},{} {}x{}) mode={} opacity={}",
            TVPLayerDebugDrawFaceName(DrawFace), dx, dy, rect.get_width(),
            rect.get_height(), src ? static_cast<int>(src->GetWidth()) : -1,
            src ? static_cast<int>(src->GetHeight()) : -1, rect.left,
            rect.top, rect.get_width(), rect.get_height(),
            static_cast<int>(mode), opacity);
    }

    ImageModified =
        MainImage->Blt(dx, dy, src, rect, met, opacity, HoldAlpha) ||
        ImageModified;

    if(dialog_trace) {
        spdlog::info(
            "Dialog.operateRect.after target={} dest=({},{} {}x{}) after=[0x{:08x},0x{:08x},0x{:08x},0x{:08x}]",
            GetName().AsStdString(), dx, dy, rect.get_width(),
            rect.get_height(), TVPStage2Sample(this, dx, dy),
            TVPStage2Sample(this, dx + rect.get_width() / 2,
                            dy + rect.get_height() / 2),
            TVPStage2Sample(this, 14, 1241),
            TVPStage2Sample(this, 200, 1270));
    }

    tTVPRect ur = rect;
    ur.set_offsets(dx, dy);
    if(ImageLeft != 0 || ImageTop != 0) {
        ur.add_offsets(ImageLeft, ImageTop);
        Update(ur);
    } else {
        Update(ur);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::StretchPile(const tTVPRect &destrect,
                                   tTJSNI_BaseLayer *src,
                                   const tTVPRect &srcrect, tjs_int opacity,
                                   tTVPBBStretchType type) {
    // obsoleted (use OperateStretch)

    // stretching pile
    if(DrawFace != dfAlpha && DrawFace != dfOpaque) {
        TVPThrowExceptionMessage(TVPNotDrawableFaceType, TJS_W("stretchPile"));
    }

    tTVPRect ur = destrect;
    if(ur.right < ur.left)
        std::swap(ur.right, ur.left);
    if(ur.bottom < ur.top)
        std::swap(ur.bottom, ur.top);
    if(!TVPIntersectRect(&ur, ur, ClipRect))
        return; // out of the clipping rectangle

    switch(DrawFace) {
        case dfAlpha:
            if(!MainImage)
                TVPThrowExceptionMessage(TVPNotDrawableLayerType);
            if(!src->MainImage)
                TVPThrowExceptionMessage(TVPSourceLayerHasNoImage);
            ImageModified = MainImage->StretchBlt(
                                ClipRect, destrect, src->MainImage, srcrect,
                                bmAlphaOnAlpha, opacity, HoldAlpha, type) ||
                ImageModified;
            break;

        case dfOpaque:
            if(!MainImage)
                TVPThrowExceptionMessage(TVPNotDrawableLayerType);
            if(!src->MainImage)
                TVPThrowExceptionMessage(TVPSourceLayerHasNoImage);
            ImageModified = MainImage->StretchBlt(
                                ClipRect, destrect, src->MainImage, srcrect,
                                bmAlpha, opacity, HoldAlpha, type) ||
                ImageModified;
            break;

        default:
            break;
    }

    if(ImageLeft != 0 || ImageTop != 0) {
        ur.add_offsets(ImageLeft, ImageTop);
        Update(ur);
    } else {
        Update(ur);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::StretchBlend(const tTVPRect &destrect,
                                    tTJSNI_BaseLayer *src,
                                    const tTVPRect &srcrect, tjs_int opacity,
                                    tTVPBBStretchType type) {
    // obsoleted (use OperateStretch)

    // stretching blend
    if(DrawFace != dfAlpha && DrawFace != dfOpaque) {
        TVPThrowExceptionMessage(TVPNotDrawableFaceType, TJS_W("stretchBlend"));
    }

    tTVPRect ur = destrect;
    if(ur.right < ur.left)
        std::swap(ur.right, ur.left);
    if(ur.bottom < ur.top)
        std::swap(ur.bottom, ur.top);
    if(!TVPIntersectRect(&ur, ur, ClipRect))
        return; // out of the clipping rectangle

    switch(DrawFace) {
        case dfAlpha:
            if(!MainImage)
                TVPThrowExceptionMessage(TVPNotDrawableLayerType);
            if(!src->MainImage)
                TVPThrowExceptionMessage(TVPSourceLayerHasNoImage);
            ImageModified = MainImage->StretchBlt(
                                ClipRect, destrect, src->MainImage, srcrect,
                                bmCopyOnAlpha, opacity, HoldAlpha, type) ||
                ImageModified;
            break;

        case dfOpaque:
            if(!MainImage)
                TVPThrowExceptionMessage(TVPNotDrawableLayerType);
            if(!src->MainImage)
                TVPThrowExceptionMessage(TVPSourceLayerHasNoImage);
            ImageModified = MainImage->StretchBlt(
                                ClipRect, destrect, src->MainImage, srcrect,
                                bmCopy, opacity, HoldAlpha, type) ||
                ImageModified;
            break;

        default:
            break;
    }

    if(ImageLeft != 0 || ImageTop != 0) {
        ur.add_offsets(ImageLeft, ImageTop);
        Update(ur);
    } else {
        Update(ur);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::OperateStretch(const tTVPRect &destrect,
                                      iTVPBaseBitmap *src,
                                      const tTVPRect &srcrect,
                                      tTVPBlendOperationMode mode,
                                      tjs_int opacity, tTVPBBStretchType type,
                                      tjs_real typeopt) {
    // stretching operation (add/mul/sub etc.)

    tTVPRect ur = destrect;
    if(ur.right < ur.left)
        std::swap(ur.right, ur.left);
    if(ur.bottom < ur.top)
        std::swap(ur.bottom, ur.top);
    if(!TVPIntersectRect(&ur, ur, ClipRect))
        return; // out of the clipping rectangle
    if(TVPLayerDebugShouldLogBitmap(src, srcrect)) {
        spdlog::info(
            "Layer.operateStretch face={} dest=({},{} {}x{}) clipped=({},{} {}x{}) srcSize={}x{} srcRect=({},{} {}x{}) mode={} opacity={} type={} opt={}",
            TVPLayerDebugDrawFaceName(DrawFace), destrect.left, destrect.top,
            destrect.get_width(), destrect.get_height(), ur.left, ur.top,
            ur.get_width(), ur.get_height(),
            src ? static_cast<int>(src->GetWidth()) : -1,
            src ? static_cast<int>(src->GetHeight()) : -1, srcrect.left,
            srcrect.top, srcrect.get_width(), srcrect.get_height(),
            static_cast<int>(mode), opacity, static_cast<int>(type), typeopt);
    }

    // It does not throw an exception in this case perhaps
    if(mode == omAuto)
        TVPThrowExceptionMessage(TVPCannotAcceptModeAuto);

    // convert tTVPBlendOperationMode to tTVPBBBltMethod
    tTVPBBBltMethod met;
    if(!GetBltMethodFromOperationModeAndDrawFace(met, mode)) {
        // unknown blt mode
        TVPThrowExceptionMessage(TVPNotDrawableFaceType,
                                 TJS_W("operateStretch"));
    }

    if(!MainImage)
        TVPThrowExceptionMessage(TVPNotDrawableLayerType);
    if(!src)
        TVPThrowExceptionMessage(TVPSourceLayerHasNoImage);
    ImageModified = MainImage->StretchBlt(ClipRect, destrect, src, srcrect, met,
                                          opacity, HoldAlpha, type, typeopt) ||
        ImageModified;

    if(ImageLeft != 0 || ImageTop != 0) {
        ur.add_offsets(ImageLeft, ImageTop);
        Update(ur);
    } else {
        Update(ur);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::AffinePile(const t2DAffineMatrix &matrix,
                                  tTJSNI_BaseLayer *src,
                                  const tTVPRect &srcrect, tjs_int opacity,
                                  tTVPBBStretchType type) {
    // obsoleted (use OperateAffine)

    // affine pile
    tTVPRect updaterect;
    bool updated;

    if(DrawFace != dfAlpha && DrawFace != dfOpaque) {
        TVPThrowExceptionMessage(TVPNotDrawableFaceType, TJS_W("affinePile"));
    }

    switch(DrawFace) {
        case dfAlpha: {
            if(!MainImage)
                TVPThrowExceptionMessage(TVPNotDrawableLayerType);
            if(!src->MainImage)
                TVPThrowExceptionMessage(TVPSourceLayerHasNoImage);
            updated = MainImage->AffineBlt(ClipRect, src->MainImage, srcrect,
                                           matrix, bmAlphaOnAlpha, opacity,
                                           &updaterect, HoldAlpha, type);
            break;
        }

        case dfOpaque: {
            if(!MainImage)
                TVPThrowExceptionMessage(TVPNotDrawableLayerType);
            if(!src->MainImage)
                TVPThrowExceptionMessage(TVPSourceLayerHasNoImage);
            updated = MainImage->AffineBlt(ClipRect, src->MainImage, srcrect,
                                           matrix, bmAlpha, opacity,
                                           &updaterect, HoldAlpha, type);
            break;
        }
        default:
            break;
    }

    ImageModified = updated || ImageModified;

    if(updated) {
        updaterect.add_offsets(ImageLeft, ImageTop);
        Update(updaterect);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::AffinePile(const tTVPPointD *points,
                                  tTJSNI_BaseLayer *src,
                                  const tTVPRect &srcrect, tjs_int opacity,
                                  tTVPBBStretchType type) {
    // obsoleted (use OperateAffine)

    // affine pile
    tTVPRect updaterect;
    bool updated;

    if(DrawFace != dfAlpha && DrawFace != dfOpaque) {
        TVPThrowExceptionMessage(TVPNotDrawableFaceType, TJS_W("affinePile"));
    }

    switch(DrawFace) {
        case dfAlpha: {
            if(!MainImage)
                TVPThrowExceptionMessage(TVPNotDrawableLayerType);
            if(!src->MainImage)
                TVPThrowExceptionMessage(TVPSourceLayerHasNoImage);
            updated = MainImage->AffineBlt(ClipRect, src->MainImage, srcrect,
                                           points, bmAlphaOnAlpha, opacity,
                                           &updaterect, HoldAlpha, type);
            break;
        }

        case dfOpaque: {
            if(!MainImage)
                TVPThrowExceptionMessage(TVPNotDrawableLayerType);
            if(!src->MainImage)
                TVPThrowExceptionMessage(TVPSourceLayerHasNoImage);
            updated = MainImage->AffineBlt(ClipRect, src->MainImage, srcrect,
                                           points, bmAlpha, opacity,
                                           &updaterect, HoldAlpha, type);
            break;
        }
        default:
            break;
    }

    ImageModified = updated || ImageModified;

    if(updated) {
        updaterect.add_offsets(ImageLeft, ImageTop);
        Update(updaterect);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::AffineBlend(const t2DAffineMatrix &matrix,
                                   tTJSNI_BaseLayer *src,
                                   const tTVPRect &srcrect, tjs_int opacity,
                                   tTVPBBStretchType type) {
    // obsoleted (use OperateAffine)

    // affine blend
    tTVPRect updaterect;
    bool updated;

    if(DrawFace != dfAlpha && DrawFace != dfOpaque) {
        TVPThrowExceptionMessage(TVPNotDrawableFaceType, TJS_W("affineBlend"));
    }

    switch(DrawFace) {
        case dfAlpha: {
            if(!MainImage)
                TVPThrowExceptionMessage(TVPNotDrawableLayerType);
            if(!src->MainImage)
                TVPThrowExceptionMessage(TVPSourceLayerHasNoImage);
            updated = MainImage->AffineBlt(ClipRect, src->MainImage, srcrect,
                                           matrix, bmCopyOnAlpha, opacity,
                                           &updaterect, HoldAlpha, type);
            break;
        }

        case dfOpaque: {
            if(!MainImage)
                TVPThrowExceptionMessage(TVPNotDrawableLayerType);
            if(!src->MainImage)
                TVPThrowExceptionMessage(TVPSourceLayerHasNoImage);
            updated = MainImage->AffineBlt(ClipRect, src->MainImage, srcrect,
                                           matrix, bmCopy, opacity, &updaterect,
                                           HoldAlpha, type);
            break;
        }

        default:
            break;
    }

    ImageModified = updated || ImageModified;

    if(updated) {
        updaterect.add_offsets(ImageLeft, ImageTop);
        Update(updaterect);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::AffineBlend(const tTVPPointD *points,
                                   tTJSNI_BaseLayer *src,
                                   const tTVPRect &srcrect, tjs_int opacity,
                                   tTVPBBStretchType type) {
    // obsoleted (use OperateAffine)

    // affine blend
    tTVPRect updaterect;
    bool updated;

    if(DrawFace != dfAlpha && DrawFace != dfOpaque) {
        TVPThrowExceptionMessage(TVPNotDrawableFaceType, TJS_W("affineBlend"));
    }

    switch(DrawFace) {
        case dfAlpha: {
            if(!MainImage)
                TVPThrowExceptionMessage(TVPNotDrawableLayerType);
            if(!src->MainImage)
                TVPThrowExceptionMessage(TVPSourceLayerHasNoImage);
            updated = MainImage->AffineBlt(ClipRect, src->MainImage, srcrect,
                                           points, bmCopyOnAlpha, opacity,
                                           &updaterect, HoldAlpha, type);
            break;
        }

        case dfOpaque: {
            if(!MainImage)
                TVPThrowExceptionMessage(TVPNotDrawableLayerType);
            if(!src->MainImage)
                TVPThrowExceptionMessage(TVPSourceLayerHasNoImage);
            updated = MainImage->AffineBlt(ClipRect, src->MainImage, srcrect,
                                           points, bmCopy, opacity, &updaterect,
                                           HoldAlpha, type);
            break;
        }

        default:
            break;
    }

    ImageModified = updated || ImageModified;

    if(updated) {
        updaterect.add_offsets(ImageLeft, ImageTop);
        Update(updaterect);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::OperateAffine(const t2DAffineMatrix &matrix,
                                     iTVPBaseBitmap *src,
                                     const tTVPRect &srcrect,
                                     tTVPBlendOperationMode mode,
                                     tjs_int opacity, tTVPBBStretchType type) {
    // affine operation
    tTVPRect updaterect;
    bool updated;

    // It does not throw an exception in this case perhaps
    if(mode == omAuto)
        TVPThrowExceptionMessage(TVPCannotAcceptModeAuto);

    // convert tTVPBlendOperationMode to tTVPBBBltMethod
    tTVPBBBltMethod met;
    if(!GetBltMethodFromOperationModeAndDrawFace(met, mode)) {
        // unknown blt mode
        TVPThrowExceptionMessage(TVPNotDrawableFaceType,
                                 TJS_W("operateAffine"));
    }

    if(!MainImage)
        TVPThrowExceptionMessage(TVPNotDrawableLayerType);
    if(!src)
        TVPThrowExceptionMessage(TVPSourceLayerHasNoImage);
    updated = MainImage->AffineBlt(ClipRect, src, srcrect, matrix, met, opacity,
                                   &updaterect, HoldAlpha, type);

    ImageModified = updated || ImageModified;

    if(updated) {
        updaterect.add_offsets(ImageLeft, ImageTop);
        Update(updaterect);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::OperateAffine(const tTVPPointD *points,
                                     iTVPBaseBitmap *src,
                                     const tTVPRect &srcrect,
                                     tTVPBlendOperationMode mode,
                                     tjs_int opacity, tTVPBBStretchType type) {
    // affine operation
    tTVPRect updaterect;
    bool updated;

    // It does not throw an exception in this case perhaps
    if(mode == omAuto)
        TVPThrowExceptionMessage(TVPCannotAcceptModeAuto);

    // convert tTVPBlendOperationMode to tTVPBBBltMethod
    tTVPBBBltMethod met;
    if(!GetBltMethodFromOperationModeAndDrawFace(met, mode)) {
        // unknown blt mode
        TVPThrowExceptionMessage(TVPNotDrawableFaceType,
                                 TJS_W("operateAffine"));
    }

    if(!MainImage)
        TVPThrowExceptionMessage(TVPNotDrawableLayerType);
    if(!src)
        TVPThrowExceptionMessage(TVPSourceLayerHasNoImage);
    updated = MainImage->AffineBlt(ClipRect, src, srcrect, points, met, opacity,
                                   &updaterect, HoldAlpha, type);

    ImageModified = updated || ImageModified;

    if(updated) {
        updaterect.add_offsets(ImageLeft, ImageTop);
        Update(updaterect);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::OperateBezierPatch(const tTVPPointD *points,
                                          tjs_int divx, tjs_int divy,
                                          iTVPBaseBitmap *src,
                                          const tTVPRect &srcrect,
                                          tTVPBlendOperationMode mode,
                                          tjs_int opacity,
                                          tTVPBBStretchType type, bool clear) {
    if(!points || !src || divx < 2 || divy < 2)
        return;

    const auto cubicBlend = [](double p0, double p1, double p2, double p3,
                               double t) {
        const double mt = 1.0 - t;
        return mt * mt * mt * p0 + 3.0 * mt * mt * t * p1 +
            3.0 * mt * t * t * p2 + t * t * t * p3;
    };
    const auto samplePatch = [&](double u, double v) -> tTVPPointD {
        tTVPPointD curve[4];
        for(int row = 0; row < 4; ++row) {
            const auto *cp = &points[row * 4];
            curve[row].x = cubicBlend(cp[0].x, cp[1].x, cp[2].x, cp[3].x, u);
            curve[row].y = cubicBlend(cp[0].y, cp[1].y, cp[2].y, cp[3].y, u);
        }
        return {
            cubicBlend(curve[0].x, curve[1].x, curve[2].x, curve[3].x, v),
            cubicBlend(curve[0].y, curve[1].y, curve[2].y, curve[3].y, v),
        };
    };

    std::vector<tTVPPointD> tessellated;
    tessellated.reserve(static_cast<size_t>(divx) * static_cast<size_t>(divy));
    for(tjs_int y = 0; y < divy; ++y) {
        const double v = divy > 1
            ? static_cast<double>(y) / static_cast<double>(divy - 1)
            : 0.0;
        for(tjs_int x = 0; x < divx; ++x) {
            const double u = divx > 1
                ? static_cast<double>(x) / static_cast<double>(divx - 1)
                : 0.0;
            tessellated.push_back(samplePatch(u, v));
        }
    }

    OperateMesh(tessellated.data(), divx, divy, src, srcrect, mode, opacity,
                type, clear);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::OperateMesh(const tTVPPointD *points, tjs_int divx,
                                   tjs_int divy, iTVPBaseBitmap *src,
                                   const tTVPRect &srcrect,
                                   tTVPBlendOperationMode mode,
                                   tjs_int opacity,
                                   tTVPBBStretchType type, bool clear) {
    if(!points || !src || divx < 2 || divy < 2)
        return;

    if(mode == omAuto)
        TVPThrowExceptionMessage(TVPCannotAcceptModeAuto);

    tTVPBBBltMethod met;
    if(!GetBltMethodFromOperationModeAndDrawFace(met, mode)) {
        TVPThrowExceptionMessage(TVPNotDrawableFaceType,
                                 TJS_W("operateMesh"));
    }

    if(DrawFace != dfAlpha && DrawFace != dfAddAlpha && DrawFace != dfOpaque) {
        TVPThrowExceptionMessage(TVPNotDrawableFaceType,
                                 TJS_W("operateMesh"));
    }
    if(!MainImage)
        TVPThrowExceptionMessage(TVPNotDrawableLayerType);

    if(clear)
        FillRect(ClipRect, NeutralColor);

    const double srcLeft = static_cast<double>(srcrect.left);
    const double srcTop = static_cast<double>(srcrect.top);
    const double srcWidth = static_cast<double>(srcrect.right - srcrect.left);
    const double srcHeight = static_cast<double>(srcrect.bottom - srcrect.top);

    std::vector<tTVPPointD> destinationPoints;
    std::vector<tTVPPointD> sourcePoints;
    destinationPoints.reserve(
        static_cast<size_t>(divx - 1) * static_cast<size_t>(divy - 1) * 6u);
    sourcePoints.reserve(destinationPoints.capacity());
    for(tjs_int y = 0; y < divy - 1; ++y) {
        const double v0 = static_cast<double>(y) / static_cast<double>(divy - 1);
        const double v1 = static_cast<double>(y + 1) /
            static_cast<double>(divy - 1);
        for(tjs_int x = 0; x < divx - 1; ++x) {
            const double u0 = static_cast<double>(x) /
                static_cast<double>(divx - 1);
            const double u1 = static_cast<double>(x + 1) /
                static_cast<double>(divx - 1);

            // Native MMotionRenderManager feeds one continuous floating-point
            // UV grid to RenderMesh.  Rounding every cell independently makes
            // adjacent cells overlap by a source pixel whenever the texture
            // size is not divisible by the mesh count.  At the common 0.5x
            // E-mote presentation scale that discontinuity becomes a visible
            // half-pixel stair-step across hair, facial lines and silhouettes.
            const double sourceLeft = srcLeft + srcWidth * u0;
            const double sourceTop = srcTop + srcHeight * v0;
            const double sourceRight = srcLeft + srcWidth * u1;
            const double sourceBottom = srcTop + srcHeight * v1;
            if(sourceRight <= sourceLeft || sourceBottom <= sourceTop)
                continue;

            const auto &p0 = points[y * divx + x];
            const auto &p1 = points[y * divx + x + 1];
            const auto &p2 = points[(y + 1) * divx + x];
            const auto &p3 = points[(y + 1) * divx + x + 1];
            destinationPoints.insert(destinationPoints.end(),
                                     {p0, p1, p2, p1, p2, p3});
            sourcePoints.insert(sourcePoints.end(), {
                {sourceLeft, sourceTop},
                {sourceRight, sourceTop},
                {sourceLeft, sourceBottom},
                {sourceRight, sourceTop},
                {sourceLeft, sourceBottom},
                {sourceRight, sourceBottom}
            });
        }
    }

    if(destinationPoints.empty()) return;

    iTVPRenderManager *manager = MainImage->GetRenderManager();
    const auto stretchType = static_cast<tTVPBBStretchType>(type & stTypeMask);
    manager->SetParameterInt(manager->EnumParameterID("StretchType"),
                             static_cast<int>(stretchType));
    const bool holdDestinationAlpha =
        DrawFace == dfOpaque ? HoldAlpha : false;
    iTVPRenderMethod *renderMethod = manager->GetRenderMethod(
        opacity, holdDestinationAlpha, met);
    if(!renderMethod) return;

    iTVPTexture2D *sourceTexture = src->GetTexture();
    iTVPTexture2D *convertedSource = nullptr;
    if(manager != src->GetRenderManager()) {
        const void *pixels = sourceTexture->GetScanLineForRead(0);
        if(pixels) {
            convertedSource = manager->CreateTexture2D(
                pixels, sourceTexture->GetPitch(), src->GetWidth(),
                src->GetHeight(), TVPTextureFormat::RGBA);
            sourceTexture = convertedSource;
        }
    }
    if(!sourceTexture) {
        if(convertedSource) convertedSource->Release();
        return;
    }

    // The Metal bridge accepts at most 64 triangles per operation.  Batching
    // here turns thousands of per-cell full-image affine operations into a
    // small number of clipped GPU draws while preserving the exact two
    // triangles of every tessellated cell.
    constexpr size_t kTrianglesPerBatch = 64;
    const size_t triangleCount = destinationPoints.size() / 3u;
    bool anyUpdated = false;
    tTVPRect totalUpdateRect;
    for(size_t firstTriangle = 0; firstTriangle < triangleCount;
        firstTriangle += kTrianglesPerBatch) {
        const size_t batchTriangles = std::min(
            kTrianglesPerBatch, triangleCount - firstTriangle);
        const size_t firstPoint = firstTriangle * 3u;
        const size_t batchPoints = batchTriangles * 3u;
        double minX = destinationPoints[firstPoint].x;
        double minY = destinationPoints[firstPoint].y;
        double maxX = minX;
        double maxY = minY;
        for(size_t point = 1; point < batchPoints; ++point) {
            const auto &value = destinationPoints[firstPoint + point];
            minX = std::min(minX, value.x);
            minY = std::min(minY, value.y);
            maxX = std::max(maxX, value.x);
            maxY = std::max(maxY, value.y);
        }
        tTVPRect batchClip(
            std::max(ClipRect.left,
                     static_cast<tjs_int>(std::floor(minX)) - 1),
            std::max(ClipRect.top,
                     static_cast<tjs_int>(std::floor(minY)) - 1),
            std::min(ClipRect.right,
                     static_cast<tjs_int>(std::ceil(maxX)) + 1),
            std::min(ClipRect.bottom,
                     static_cast<tjs_int>(std::ceil(maxY)) + 1));
        if(batchClip.right <= batchClip.left ||
           batchClip.bottom <= batchClip.top) {
            continue;
        }
        tRenderTexQuadArray::Element sourceElement(
            sourceTexture, sourcePoints.data() + firstPoint);
        iTVPTexture2D *referenceTexture = MainImage->GetTexture();
        iTVPTexture2D *targetTexture = MainImage->GetTextureForRender(
            renderMethod->IsBlendTarget(), &batchClip);
        manager->OperateTriangles(
            renderMethod, static_cast<int>(batchTriangles), targetTexture,
            referenceTexture, batchClip,
            destinationPoints.data() + firstPoint,
            tRenderTexQuadArray(&sourceElement, 1));
        if(!anyUpdated) {
            totalUpdateRect = batchClip;
            anyUpdated = true;
        } else {
            totalUpdateRect.do_union(batchClip);
        }
    }
    if(convertedSource) convertedSource->Release();
    if(anyUpdated) {
        ImageModified = true;
        totalUpdateRect.add_offsets(ImageLeft, ImageTop);
        Update(totalUpdateRect);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::DoBoxBlur(tjs_int xblur, tjs_int yblur) {
    // blur with box blur method
    if(!MainImage)
        TVPThrowExceptionMessage(TVPNotDrawableLayerType);

    bool updated;

    if(DrawFace != dfAlpha)
        updated = MainImage->DoBoxBlur(ClipRect,
                                       tTVPRect(-xblur, -yblur, xblur, yblur));
    else
        updated = MainImage->DoBoxBlurForAlpha(
            ClipRect, tTVPRect(-xblur, -yblur, xblur, yblur));

    ImageModified = updated || ImageModified;

    if(updated) {
        tTVPRect updaterect(ClipRect);
        updaterect.add_offsets(ImageLeft, ImageTop);
        Update(updaterect);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::AdjustGamma(const tTVPGLGammaAdjustData &data) {
    // this is not affected by DrawFace
    if(!MainImage)
        TVPThrowExceptionMessage(TVPNotDrawableLayerType);

    if(DrawFace == dfAddAlpha)
        MainImage->AdjustGammaForAdditiveAlpha(ClipRect, data);
    else
        MainImage->AdjustGamma(ClipRect, data);

    ImageModified = true;
    Update();
}

// Function to generate a 1D Gaussian kernel
std::vector<float> generate_1d_gaussian_kernel(int radius, float sigma) {
    int size = 2 * radius + 1;
    std::vector<float> kernel(size);
    float sum = 0.0f;
    float r_squared = 2.0f * sigma * sigma;

    for(int i = 0; i < size; ++i) {
        int x = i - radius;
        kernel[i] = exp(-(static_cast<float>(x * x)) / r_squared);
        sum += kernel[i];
    }

    // Normalize the kernel
    for(int i = 0; i < size; ++i) {
        kernel[i] /= sum;
    }
    return kernel;
}

void tTJSNI_BaseLayer::ApplyGaussianBlur(tjs_int radius, float sigma) {
    if(!CanHaveImage || !MainImage || !GetHasImage()) {
        TVPAddLog(TJS_W("BaseLayer::ApplyGaussianBlur: Layer cannot have or "
                        "does not have a main image."));
        return;
    }

    tjs_uint image_width = GetImageWidth();
    tjs_uint image_height = GetImageHeight();

    if(image_width == 0 || image_height == 0 || radius <= 0 || sigma <= 0.0f) {
        TVPAddLog(TJS_W("BaseLayer::ApplyGaussianBlur: Invalid parameters "
                        "(dimensions, radius, or sigma)."));
        return;
    }

    // 1. Get original image data (we'll need a copy to read from while writing
    // to the main buffer) It's safer to work on a copy or a temporary buffer
    // for convolution
    std::vector<tjs_uint32> original_pixels(image_width * image_height);

    const void *main_buffer_ptr = GetMainImagePixelBuffer(); // For reading
    if(!main_buffer_ptr) {
        TVPAddLog(TJS_W("BaseLayer::ApplyGaussianBlur: Failed to get main "
                        "image pixel buffer for reading."));
        return;
    }
    const tjs_uint8 *main_image_data_byte_ptr =
        reinterpret_cast<const tjs_uint8 *>(main_buffer_ptr);
    tjs_int main_image_pitch = GetMainImagePixelBufferPitch();

    for(tjs_uint y_idx = 0; y_idx < image_height; ++y_idx) {
        const tjs_uint32 *scanline_in = reinterpret_cast<const tjs_uint32 *>(
            main_image_data_byte_ptr + y_idx * main_image_pitch);
        for(tjs_uint x_idx = 0; x_idx < image_width; ++x_idx) {
            original_pixels[y_idx * image_width + x_idx] = scanline_in[x_idx];
        }
    }
    // Release read buffer if GetMainImagePixelBuffer() implies a lock
    // (Depends on specific implementation of GetMainImagePixelBuffer)


    // 2. Generate 1D Gaussian kernel (for separable convolution)
    std::vector<float> kernel_1d = generate_1d_gaussian_kernel(radius, sigma);
    int kernel_radius = radius; // kernel_1d.size() = 2 * kernel_radius + 1

    // 3. Prepare a temporary buffer for the first pass (horizontal blur)
    std::vector<tjs_uint32> temp_pixels(image_width * image_height);

    // --- First Pass: Horizontal Blur ---
    for(tjs_int y = 0; y < static_cast<tjs_int>(image_height); ++y) {
        for(tjs_int x = 0; x < static_cast<tjs_int>(image_width); ++x) {
            float sum_r = 0.0f, sum_g = 0.0f, sum_b = 0.0f, sum_a = 0.0f;
            // float total_weight = 0.0f; // Kernel is already normalized

            for(int k = -kernel_radius; k <= kernel_radius; ++k) {
                int sample_x = x + k;

                // Edge handling: Clamp to edge (simple method)
                sample_x = std::max(
                    0,
                    std::min(sample_x, static_cast<tjs_int>(image_width) - 1));

                tjs_uint32 pixel_color =
                    original_pixels[y * image_width + sample_x];
                float weight = kernel_1d[k + kernel_radius];

                sum_a += ((pixel_color >> 24) & 0xFF) * weight;
                sum_r += ((pixel_color >> 16) & 0xFF) * weight;
                sum_g += ((pixel_color >> 8) & 0xFF) * weight;
                sum_b += (pixel_color & 0xFF) * weight;
            }

            tjs_uint8 final_a =
                static_cast<tjs_uint8>(std::min(255.0f, std::max(0.0f, sum_a)));
            tjs_uint8 final_r =
                static_cast<tjs_uint8>(std::min(255.0f, std::max(0.0f, sum_r)));
            tjs_uint8 final_g =
                static_cast<tjs_uint8>(std::min(255.0f, std::max(0.0f, sum_g)));
            tjs_uint8 final_b =
                static_cast<tjs_uint8>(std::min(255.0f, std::max(0.0f, sum_b)));

            temp_pixels[y * image_width + x] =
                (final_a << 24) | (final_r << 16) | (final_g << 8) | final_b;
        }
    }

    // --- Second Pass: Vertical Blur (from temp_pixels to final output buffer)
    // ---
    void *output_buffer_raw = GetMainImagePixelBufferForWrite();
    if(!output_buffer_raw) {
        TVPAddLog(TJS_W("BaseLayer::ApplyGaussianBlur: Failed to get main "
                        "image pixel buffer for writing."));
        return; // Or restore original_pixels to the buffer if appropriate
    }
    tjs_uint8 *output_pixel_buffer_base =
        static_cast<tjs_uint8 *>(output_buffer_raw);
    tjs_int output_pitch = GetMainImagePixelBufferPitch();

    for(tjs_int y = 0; y < static_cast<tjs_int>(image_height); ++y) {
        for(tjs_int x = 0; x < static_cast<tjs_int>(image_width); ++x) {
            float sum_r = 0.0f, sum_g = 0.0f, sum_b = 0.0f, sum_a = 0.0f;

            for(int k = -kernel_radius; k <= kernel_radius; ++k) {
                int sample_y = y + k;

                // Edge handling: Clamp to edge
                sample_y = std::max(
                    0,
                    std::min(sample_y, static_cast<tjs_int>(image_height) - 1));

                tjs_uint32 pixel_color =
                    temp_pixels[sample_y * image_width +
                                x]; // Read from horizontally blurred temp
                                    // buffer
                float weight = kernel_1d[k + kernel_radius];

                sum_a += ((pixel_color >> 24) & 0xFF) * weight;
                sum_r += ((pixel_color >> 16) & 0xFF) * weight;
                sum_g += ((pixel_color >> 8) & 0xFF) * weight;
                sum_b += (pixel_color & 0xFF) * weight;
            }

            tjs_uint32 *output_pixel_val_ptr = reinterpret_cast<tjs_uint32 *>(
                output_pixel_buffer_base + y * output_pitch +
                x * 4 /*bytes_per_pixel*/);

            output_pixel_val_ptr[0] =
                (static_cast<tjs_uint8>(std::min(255.0f, std::max(0.0f, sum_a)))
                 << 24) |
                (static_cast<tjs_uint8>(std::min(255.0f, std::max(0.0f, sum_r)))
                 << 16) |
                (static_cast<tjs_uint8>(std::min(255.0f, std::max(0.0f, sum_g)))
                 << 8) |
                (static_cast<tjs_uint8>(
                    std::min(255.0f, std::max(0.0f, sum_b))));
        }
    }

    SetImageModified(true);
    Update();

    TVPAddLog(TJS_W("BaseLayer::ApplyGaussianBlur: Gaussian blur applied with "
                    "radius ") +
              ttstr(radius) + TJS_W(" and sigma ") +
              ttstr(std::to_string(sigma)));
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::DoGrayScale() {
    // this is not affected by DrawFace
    if(!MainImage)
        TVPThrowExceptionMessage(TVPNotDrawableLayerType);

    MainImage->DoGrayScale(ClipRect);

    ImageModified = true;
    Update();
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::LRFlip() {
    // this is not affected by DrawFace
    if(!MainImage)
        TVPThrowExceptionMessage(TVPNotDrawableLayerType);

    tTVPRect r(0, 0, MainImage->GetWidth(), MainImage->GetHeight());
    MainImage->LRFlip(r);
    if(ProvinceImage)
        ProvinceImage->LRFlip(r);

    ImageModified = true;
    Update();
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::UDFlip() {
    // this is not affected by DrawFace
    if(!MainImage)
        TVPThrowExceptionMessage(TVPNotDrawableLayerType);

    tTVPRect r(0, 0, MainImage->GetWidth(), MainImage->GetHeight());
    MainImage->UDFlip(r);
    if(ProvinceImage)
        ProvinceImage->UDFlip(r);

    ImageModified = true;
    Update();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// interface to font object
//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::ApplyFont() {
    if(FontChanged && MainImage) {
        FontChanged = false;
        MainImage->SetFont(Font);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetFontFace(const ttstr &face) {
    if(Font.Face != face) {
        Font.Face = face;
        FontChanged = true;
    }
}

//---------------------------------------------------------------------------
ttstr tTJSNI_BaseLayer::GetFontFace() const { return Font.Face; }

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetFontHeight(tjs_int height) {
    if(height < 0)
        height = -height; // TVP2 does not support negative value of height

    if(Font.Height != height) {
        Font.Height = height;
        FontChanged = true;
    }
}

//---------------------------------------------------------------------------
tjs_int tTJSNI_BaseLayer::GetFontHeight() const { return Font.Height; }

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetFontAngle(tjs_int angle) {
    if(Font.Angle != angle) {
        angle = angle % 3600;
        if(angle < 0)
            angle += 3600;
        Font.Angle = angle;
        FontChanged = true;
    }
}

//---------------------------------------------------------------------------
tjs_int tTJSNI_BaseLayer::GetFontAngle() const { return Font.Angle; }

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetFontBold(bool b) {
    if((0 != (Font.Flags & TVP_TF_BOLD)) != b) {
        Font.Flags &= ~TVP_TF_BOLD;
        if(b)
            Font.Flags |= TVP_TF_BOLD;
        FontChanged = true;
    }
}

//---------------------------------------------------------------------------
bool tTJSNI_BaseLayer::GetFontBold() const {
    return 0 != (Font.Flags & TVP_TF_BOLD);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetFontItalic(bool b) {
    if((0 != (Font.Flags & TVP_TF_ITALIC)) != b) {
        Font.Flags &= ~TVP_TF_ITALIC;
        if(b)
            Font.Flags |= TVP_TF_ITALIC;
        FontChanged = true;
    }
}

//---------------------------------------------------------------------------
bool tTJSNI_BaseLayer::GetFontItalic() const {
    return 0 != (Font.Flags & TVP_TF_ITALIC);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetFontStrikeout(bool b) {
    if((0 != (Font.Flags & TVP_TF_STRIKEOUT)) != b) {
        Font.Flags &= ~TVP_TF_STRIKEOUT;
        if(b)
            Font.Flags |= TVP_TF_STRIKEOUT;
        FontChanged = true;
    }
}

//---------------------------------------------------------------------------
bool tTJSNI_BaseLayer::GetFontStrikeout() const {
    return 0 != (Font.Flags & TVP_TF_STRIKEOUT);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetFontUnderline(bool b) {
    if((0 != (Font.Flags & TVP_TF_UNDERLINE)) != b) {
        Font.Flags &= ~TVP_TF_UNDERLINE;
        if(b)
            Font.Flags |= TVP_TF_UNDERLINE;
        FontChanged = true;
    }
}

//---------------------------------------------------------------------------
bool tTJSNI_BaseLayer::GetFontUnderline() const {
    return 0 != (Font.Flags & TVP_TF_UNDERLINE);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::SetFontFaceIsFileName(bool b) {
    if((0 != (Font.Flags & TVP_TF_FONTFILE)) != b) {
        Font.Flags &= ~TVP_TF_FONTFILE;
        if(b)
            Font.Flags |= TVP_TF_FONTFILE;
        FontChanged = true;
    }
}

//---------------------------------------------------------------------------
bool tTJSNI_BaseLayer::GetFontFaceIsFileName() const {
    return 0 != (Font.Flags & TVP_TF_FONTFILE);
}

//---------------------------------------------------------------------------
tjs_int tTJSNI_BaseLayer::GetTextWidth(const ttstr &text) {
    if(!MainImage)
        TVPThrowExceptionMessage(TVPUnsupportedLayerType,
                                 TJS_W("getTextWidth"));

    ApplyFont();

    return MainImage->GetTextWidth(text);
}

//---------------------------------------------------------------------------
tjs_int tTJSNI_BaseLayer::GetTextHeight(const ttstr &text) {
    if(!MainImage)
        TVPThrowExceptionMessage(TVPUnsupportedLayerType,
                                 TJS_W("getTextHeight"));

    ApplyFont();

    return MainImage->GetTextHeight(text);
}

//---------------------------------------------------------------------------
double tTJSNI_BaseLayer::GetEscWidthX(const ttstr &text) {
    if(!MainImage)
        TVPThrowExceptionMessage(TVPUnsupportedLayerType,
                                 TJS_W("getEscWidthX"));

    ApplyFont();

    return MainImage->GetEscWidthX(text);
}

//---------------------------------------------------------------------------
double tTJSNI_BaseLayer::GetEscWidthY(const ttstr &text) {
    if(!MainImage)
        TVPThrowExceptionMessage(TVPUnsupportedLayerType,
                                 TJS_W("getEscWidthY"));

    ApplyFont();

    return MainImage->GetEscWidthY(text);
}

//---------------------------------------------------------------------------
double tTJSNI_BaseLayer::GetEscHeightX(const ttstr &text) {
    if(!MainImage)
        TVPThrowExceptionMessage(TVPUnsupportedLayerType,
                                 TJS_W("getEscHeightX"));

    ApplyFont();

    return MainImage->GetEscHeightX(text);
}

//---------------------------------------------------------------------------
double tTJSNI_BaseLayer::GetEscHeightY(const ttstr &text) {
    if(!MainImage)
        TVPThrowExceptionMessage(TVPUnsupportedLayerType,
                                 TJS_W("getEscHeightY"));

    ApplyFont();

    return MainImage->GetEscHeightY(text);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::GetFontGlyphDrawRect(const ttstr &text, tTVPRect &area) {
    if(!MainImage)
        TVPThrowExceptionMessage(TVPUnsupportedLayerType,
                                 TJS_W("getGlyphDrawRect"));

    ApplyFont();

    MainImage->GetFontGlyphDrawRect(text, area);
}
//---------------------------------------------------------------------------
#if 0
                                                                                                                        bool tTJSNI_BaseLayer::DoUserFontSelect(tjs_uint32 flags, const ttstr &caption,
		const ttstr &prompt, const ttstr &samplestring)
{
	ApplyFont();

	bool b = MainImage->SelectFont(flags, caption, prompt, samplestring,
		Font.Face);
	if(b) FontChanged = true;
	return b;
}
#endif

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::GetFontList(tjs_uint32 flags, std::vector<ttstr> &list) {
    ApplyFont();

    MainImage->GetFontList(flags, list);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::MapPrerenderedFont(const ttstr &storage) {
    ApplyFont();

    MainImage->MapPrerenderedFont(storage);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::UnmapPrerenderedFont() {
    ApplyFont();

    MainImage->UnmapPrerenderedFont();
}

//---------------------------------------------------------------------------
const tTVPFont &tTJSNI_BaseLayer::GetFont() const { return Font; }

//---------------------------------------------------------------------------
iTJSDispatch2 *tTJSNI_BaseLayer::GetFontObjectNoAddRef() {
    if(FontObject)
        return FontObject;

    // create font object if the object is not yet created.
    if(!Owner)
        TVPThrowExceptionMessage(TVPLayerObjectIsNotProperlyConstructed);
    FontObject = TVPCreateFontObject(Owner);

    return FontObject;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// updating management
//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::UpdateTransDestinationOnSelfUpdate(
    const tTVPComplexRect &region) {
    if(TransDest && TransDest->InTransition && TransDest->TransSelfUpdate) {
        // transition, its update is performed by user code, is
        // processing on transition destination. update the transition
        // destination as transition source does.
        switch(TransDest->TransUpdateType) {
            case tutDivisibleFade: {
                tTVPComplexRect cp(region);
                TransDest->Update(cp, true);
                break;
            }
            default:
                TransDest->Update(true);
                // update entire area of the transition destination
                // because we cannot determine where the update
                // affects.
                break;
        }
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::UpdateTransDestinationOnSelfUpdate(
    const tTVPRect &rect) {
    // essentially the same as
    // UpdateTransDestinationOnSelfUpdate(const tTVPComplexRect
    // &region)
    if(TransDest && TransDest->InTransition && TransDest->TransSelfUpdate) {
        switch(TransDest->TransUpdateType) {
            case tutDivisibleFade:
                TransDest->Update(rect, true);
                break;
            default:
                TransDest->Update(true);
                break;
        }
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::UpdateChildRegion(tTJSNI_BaseLayer *child,
                                         const tTVPComplexRect &region,
                                         bool tempupdate, bool targvisible,
                                         bool addtoprimary) {
    if(TVPFlashTraceEnabled() && TVPFlashTraceRelated(child)) {
        spdlog::info(
            "Flash.update-child parent={} child={} temp={} target_visible={} "
            "add_primary={} region_count={} child_visible={} child_opacity={} "
            "parent_visible_children={}",
            GetName().AsStdString(), child ? child->GetName().AsStdString() : "<null>",
            tempupdate ? 1 : 0, targvisible ? 1 : 0, addtoprimary ? 1 : 0,
            region.GetCount(), child && child->GetVisible() ? 1 : 0,
            child ? child->GetOpacity() : -1, GetVisibleChildrenCount());
    }
    // called by child.  add update rect subscribed in "rect"

    tTVPRect cr;
    cr.left = cr.top = 0;
    cr.right = Rect.get_width();
    cr.bottom = Rect.get_height();

    tTVPComplexRect converted;
    converted.CopyWithOffsets(region, cr, child->Rect.left, child->Rect.top);

    if(!tempupdate) {
        if(GetCacheEnabled()) {
            // caching is enabled
            if(targvisible) {
                CacheRecalcRegion.Or(converted);
                if(CacheRecalcRegion.GetCount() > TVP_CACHE_UNITE_LIMIT)
                    CacheRecalcRegion.Unite();
            }
        }
    }

    if(Parent) {
        Parent->UpdateChildRegion(this, converted, tempupdate, targvisible,
                                  addtoprimary);
    } else {
        if(addtoprimary)
            if(Manager)
                Manager->AddUpdateRegion(converted);
    }

    UpdateTransDestinationOnSelfUpdate(converted);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::InternalUpdate(const tTVPRect &rect, bool tempupdate) {
    tTVPRect cr;
    cr.left = cr.top = 0;
    cr.right = Rect.get_width();
    cr.bottom = Rect.get_height();
    if(!TVPIntersectRect(&cr, cr, rect))
        return;

    if(!tempupdate) {
        if(GetCacheEnabled()) {
            // caching is enabled
            CacheRecalcRegion.Or(cr);
            if(CacheRecalcRegion.GetCount() > TVP_CACHE_UNITE_LIMIT)
                CacheRecalcRegion.Unite();
        }
    }

    if(Parent) {
        tTVPComplexRect c;
        c.Or(cr);
        Parent->UpdateChildRegion(this, c, tempupdate, GetVisible(),
                                  GetNodeVisible());
    } else {
        if(Manager)
            Manager->AddUpdateRegion(cr);
    }

    UpdateTransDestinationOnSelfUpdate(cr);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::Update(tTVPComplexRect &rects, bool tempupdate) {
    tTVPRect cr;
    cr.left = cr.top = 0;
    cr.right = Rect.get_width();
    cr.bottom = Rect.get_height();
    rects.And(cr);

    if(!rects.GetCount())
        return;

    if(!tempupdate) {
        // in case of tempupdate == false
        /*
                        tempupdate == true indicates that the layer
           content is not changed, but the layer need to be updated to
           the window. Mainly used by transition update.

                        There is no need to update CacheRecalcRegion
           because the layer content is not changed when tempupdate ==
           true.
                */

        if(GetCacheEnabled()) {
            // caching is enabled
            CacheRecalcRegion.Or(rects);
            if(CacheRecalcRegion.GetCount() > TVP_CACHE_UNITE_LIMIT)
                CacheRecalcRegion.Unite();
        }
    }

    if(Parent) {
        Parent->UpdateChildRegion(this, rects, tempupdate, GetVisible(),
                                  GetNodeVisible());
    } else {
        if(Manager)
            Manager->AddUpdateRegion(rects);
    }

    UpdateTransDestinationOnSelfUpdate(rects);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::Update(const tTVPRect &rect, bool tempupdate) {
    // update part of the layer
    tTVPRect cr;
    cr.left = cr.top = 0;
    cr.right = Rect.get_width();
    cr.bottom = Rect.get_height();
    if(!TVPIntersectRect(&cr, cr, rect))
        return;

    InternalUpdate(cr, tempupdate);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::Update(bool tempupdate) {
    // update entire of the layer
    tTVPRect rect;
    rect.left = rect.top = 0;
    rect.right = Rect.get_width();
    rect.bottom = Rect.get_height();
    InternalUpdate(rect, tempupdate);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::ParentUpdate() {
    // called when layer moves
    if(Parent) {
        tTVPRect rect;
        rect.left = rect.top = 0;
        rect.right = Rect.get_width();
        rect.bottom = Rect.get_height();
        tTVPComplexRect c;
        c.Or(rect);
        Parent->UpdateChildRegion(this, c, false, GetVisible(),
                                  GetNodeVisible());
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::UpdateAllChildren(bool tempupdate) {
    TVP_LAYER_FOR_EACH_CHILD_BEGIN(child)

    child->Update(tempupdate);

    TVP_LAYER_FOR_EACH_CHILD_END
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::BeforeCompletion() {
    // called before the drawing is processed
    if(InCompletion)
        return;
    // calling during completion more than once is not allowed

    const bool profileCompletion = g_layerCompletionProfile.active;
    if(profileCompletion) {
        ++g_layerCompletionProfile.beforeCalls;
        ++g_layerCompletionProfile.depth;
        g_layerCompletionProfile.maxDepth = std::max(
            g_layerCompletionProfile.maxDepth,
            g_layerCompletionProfile.depth);
        if(InTransition) {
            ++g_layerCompletionProfile.transitionCalls;
        }
    }

    // fire onPaint
    if(CallOnPaint) {
        TVPTraceStage2Lifecycle("paint-dispatch-before", this);
        if(profileCompletion) {
            ++g_layerCompletionProfile.onPaintCalls;
        }
        CallOnPaint = false;
        static ttstr eventname(TJS_W("onPaint"));
        const auto onPaintStarted = profileCompletion
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 0, nullptr);
        TVPTraceStage2Lifecycle("paint-dispatch-after", this);
        if(profileCompletion) {
            const double onPaintMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - onPaintStarted).count();
            g_layerCompletionProfile.onPaintMs += onPaintMs;
            if(onPaintMs >= g_layerCompletionProfile.maxOnPaintMs) {
                g_layerCompletionProfile.maxOnPaintMs = onPaintMs;
                g_layerCompletionProfile.maxOnPaintLayer = fmt::format(
                    "{}@{}", GetName().AsStdString(),
                    static_cast<const void *>(this));
            }
        }
    }

    // for transition
    if(InTransition) {
        // transition is processing

        if(DivisibleTransHandler) {
            // notify start of processing unit
            tjs_error er;
            if(TransSelfUpdate) {
                // set TransTick here if the transition is performed
                // by user code; otherwise the TransTick is to be set
                // at tTJSNI_BaseLayer::InvokeTransition
                TransTick = GetTransTick();
            }
            er = DivisibleTransHandler->StartProcess(TransTick);
            if(er != TJS_S_TRUE) {
                TVPTraceLayerTransition("before-completion-start-finished",
                                        this, TransSrc, er);
                StopTransitionByHandler();
            }
        } else if(GiveUpdateTransHandler) {
            // not yet implemented
        }
    }

    const bool use_cached_transition_frames =
        TransUpdateType == tutDivisible ||
        TransUpdateType == tutDivisibleFade;
    TransDrawable.SkipSnapshotFrame = false;
    if(InTransition && TransWithChildren && use_cached_transition_frames &&
       TransDrawable.SnapshotWarmupFrames > 0) {
        --TransDrawable.SnapshotWarmupFrames;
        TransDrawable.SkipSnapshotFrame = true;
    }
    if(InTransition && TransWithChildren && use_cached_transition_frames &&
       !TransDrawable.SkipSnapshotFrame) {
        // Complete each stable page once. Transition Update() invalidates the
        // destination every frame, so rebuilding these snapshots on every
        // BeforeCompletion defeats the cache and repeats the entire child-tree
        // composition during the animation.
        if(!TransDrawable.Src1Bmp) {
            InTransition = false; // cheat!!!
            tTVPBaseTexture *completed = nullptr;
            try {
                completed = Complete();
                InTransition = true;
            } catch(...) {
                InTransition = true;
                throw;
            }
            if(completed) {
                auto *snapshot = new tTVPBaseTexture(*completed);
                snapshot->Independ();
                TransDrawable.Src1Bmp = snapshot;
            }
        }

        if(TransSrc && !TransDrawable.Src2Bmp) {
            tTVPBaseTexture *completed = TransSrc->Complete();
            if(completed) {
                auto *snapshot = new tTVPBaseTexture(*completed);
                snapshot->Independ();
                TransDrawable.Src2Bmp = snapshot;
            }
        }
    }

    TVP_LAYER_FOR_EACH_CHILD_BEGIN(child)

    child->BeforeCompletion();

    TVP_LAYER_FOR_EACH_CHILD_END

    if(profileCompletion) {
        --g_layerCompletionProfile.depth;
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::AfterCompletion() {
    // called after the drawing is processed
    if(InCompletion)
        return;
    // calling during completion more than once is not allowed

    if(InTransition) {
        // transition is processing
        if(DivisibleTransHandler) {
            // notify start of processing unit
            tjs_error er;
            er = DivisibleTransHandler->EndProcess();
            if(er != TJS_S_TRUE) {
                TVPTraceLayerTransition("after-completion-end-finished",
                                        this, TransSrc, er);
                StopTransitionByHandler();
            }
        } else if(GiveUpdateTransHandler) {
            // not yet implemented
        }
    }

    TVP_LAYER_FOR_EACH_CHILD_BEGIN(child)

    child->AfterCompletion();

    TVP_LAYER_FOR_EACH_CHILD_END
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::QueryUpdateExcludeRect(tTVPRect &rect,
                                              bool parentvisible) {
    // query completely opaque area

    // convert to local coordinates
    rect.left -= Rect.left;
    rect.right -= Rect.left;
    rect.top -= Rect.top;
    rect.bottom -= Rect.top;

    // recur to children
    parentvisible = parentvisible && Visible &&
        (DisplayType == ltOpaque || DisplayType == ltAlpha ||
         DisplayType == ltAddAlpha || DisplayType == ltPsNormal) &&
        Opacity == 255; // fixed 2004/01/09 W.Dee
    TVP_LAYER_FOR_EACH_CHILD_NOLOCK_BACKWARD_BEGIN(child)

    child->QueryUpdateExcludeRect(rect, parentvisible);

    TVP_LAYER_FOR_EACH_CHILD_NOLOCK_BACKWARD_END

    // copy current update exclude rect
    if(parentvisible)
        UpdateExcludeRect = rect;
    else
        UpdateExcludeRect.clear();

    // convert to parent's coordinates
    rect.left += Rect.left;
    rect.right += Rect.left;
    rect.top += Rect.top;
    rect.bottom += Rect.top;

    // check visibility & opacity
    if(parentvisible &&
       (DisplayType == ltOpaque || (MainImage && MainImage->IsOpaque())) &&
       Opacity == 255) {
        if(rect.is_empty()) {
            rect = Rect;
        } else {
            if(!TVPIntersectRect(&rect, rect, Rect))
                rect.clear();
        }
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::BltImage(iTVPBaseBitmap *dest,
                                tTVPLayerType destlayertype, tjs_int destx,
                                tjs_int desty, iTVPBaseBitmap *src,
                                const tTVPRect &srcrect, tTVPLayerType drawtype,
                                tjs_int opacity, bool hda) {
    // draw src to dest according with layer type
    /*
            // do the effect
            tTVPRect destrect;
            destrect.left = destx;
            destrect.top = desty;
            destrect.right = destx + srcrect.get_width();
            destrect.bottom = desty + srcrect.get_height();
            EffectImage(dest, destrect);
    */

    // blt to the target
    tTVPBBBltMethod met;
    switch(drawtype) {
        case ltBinder:
            // no action
            return;

        case ltOpaque: // formerly ltCoverRect
            // copy
            if(TVPIsTypeUsingAlpha(destlayertype))
                met = bmCopyOnAlpha;
            else if(TVPIsTypeUsingAddAlpha(destlayertype))
                met = bmCopyOnAddAlpha;
            else
                met = bmCopy;
            break;

        case ltAlpha: // formerly ltTransparent
            // alpha blend
            if(TVPIsTypeUsingAlpha(destlayertype))
                met = bmAlphaOnAlpha;
            else if(TVPIsTypeUsingAddAlpha(destlayertype))
                met = bmAlphaOnAddAlpha;
            else
                met = bmAlpha;
            break;

        case ltAdditive:
            // additive blend
            hda = hda || TVPIsTypeUsingAlphaChannel(destlayertype);
            // hda = true if destination has alpha
            // ( preserving mask )
            met = bmAdd;
            break;

        case ltSubtractive:
            // subtractive blend
            hda = hda || TVPIsTypeUsingAlphaChannel(destlayertype);
            met = bmSub;
            break;

        case ltMultiplicative:
            // multiplicative blend
            hda = hda || TVPIsTypeUsingAlphaChannel(destlayertype);
            met = bmMul;
            break;

        case ltDodge:
            // color dodge ( "Ooi yaki" in Japanese )
            hda = hda || TVPIsTypeUsingAlphaChannel(destlayertype);
            met = bmDodge;
            break;

        case ltDarken:
            // darken blend (select lower luminosity)
            hda = hda || TVPIsTypeUsingAlphaChannel(destlayertype);
            met = bmDarken;
            break;

        case ltLighten:
            // lighten blend (select higher luminosity)
            hda = hda || TVPIsTypeUsingAlphaChannel(destlayertype);
            met = bmLighten;
            break;

        case ltScreen:
            // screen multiplicative blend
            hda = hda || TVPIsTypeUsingAlphaChannel(destlayertype);
            met = bmScreen;
            break;

        case ltAddAlpha:
            // alpha blend
            if(TVPIsTypeUsingAlpha(destlayertype))
                met = bmAddAlphaOnAlpha;
            else if(TVPIsTypeUsingAddAlpha(destlayertype))
                met = bmAddAlphaOnAddAlpha;
            else
                met = bmAddAlpha;
            break;

        case ltPsNormal:
            // Photoshop compatible normal blend
            hda = hda || TVPIsTypeUsingAlphaChannel(destlayertype);
            met = bmPsNormal;
            break;

        case ltPsAdditive:
            // Photoshop compatible additive blend
            hda = hda || TVPIsTypeUsingAlphaChannel(destlayertype);
            met = bmPsAdditive;
            break;

        case ltPsSubtractive:
            // Photoshop compatible subtractive blend
            hda = hda || TVPIsTypeUsingAlphaChannel(destlayertype);
            met = bmPsSubtractive;
            break;

        case ltPsMultiplicative:
            // Photoshop compatible multiplicative blend
            hda = hda || TVPIsTypeUsingAlphaChannel(destlayertype);
            met = bmPsMultiplicative;
            break;

        case ltPsScreen:
            // Photoshop compatible screen blend
            hda = hda || TVPIsTypeUsingAlphaChannel(destlayertype);
            met = bmPsScreen;
            break;

        case ltPsOverlay:
            // Photoshop compatible overlay blend
            hda = hda || TVPIsTypeUsingAlphaChannel(destlayertype);
            met = bmPsOverlay;
            break;

        case ltPsHardLight:
            // Photoshop compatible hard light blend
            hda = hda || TVPIsTypeUsingAlphaChannel(destlayertype);
            met = bmPsHardLight;
            break;

        case ltPsSoftLight:
            // Photoshop compatible soft light blend
            hda = hda || TVPIsTypeUsingAlphaChannel(destlayertype);
            met = bmPsSoftLight;
            break;

        case ltPsColorDodge:
            // Photoshop compatible color dodge blend
            hda = hda || TVPIsTypeUsingAlphaChannel(destlayertype);
            met = bmPsColorDodge;
            break;

        case ltPsColorDodge5:
            // Photoshop 5.x compatible color dodge blend
            hda = hda || TVPIsTypeUsingAlphaChannel(destlayertype);
            met = bmPsColorDodge5;
            break;

        case ltPsColorBurn:
            // Photoshop compatible color burn blend
            hda = hda || TVPIsTypeUsingAlphaChannel(destlayertype);
            met = bmPsColorBurn;
            break;

        case ltPsLighten:
            // Photoshop compatible compare (lighten) blend
            hda = hda || TVPIsTypeUsingAlphaChannel(destlayertype);
            met = bmPsLighten;
            break;

        case ltPsDarken:
            // Photoshop compatible compare (darken) blend
            hda = hda || TVPIsTypeUsingAlphaChannel(destlayertype);
            met = bmPsDarken;
            break;

        case ltPsDifference:
            // Photoshop compatible difference blend
            hda = hda || TVPIsTypeUsingAlphaChannel(destlayertype);
            met = bmPsDifference;
            break;

        case ltPsDifference5:
            // Photoshop 5.x compatible difference blend
            hda = hda || TVPIsTypeUsingAlphaChannel(destlayertype);
            met = bmPsDifference5;
            break;

        case ltPsExclusion:
            // Photoshop compatible exclusion blend
            hda = hda || TVPIsTypeUsingAlphaChannel(destlayertype);
            met = bmPsExclusion;
            break;

        default:
            return;
    }

    dest->Blt(destx, desty, src, srcrect, met, opacity, hda);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::DrawSelf(tTVPDrawable *target, tTVPRect &pr,
                                tTVPRect &cr) {
    if(const char *trace = std::getenv("AETHERKIRI_MESSAGE_FRAME_PIXELS");
       trace && *trace && *trace != '0' && GetName() == TJS_W("colorframe") &&
       MainImage) {
        static std::atomic<bool> sampled{false};
        if(!sampled.exchange(true, std::memory_order_relaxed)) {
            auto *image = MainImage->GetTexture();
            if(!image)
                return;
            const tjs_uint width = image->GetWidth();
            const tjs_uint height = image->GetHeight();
            tjs_uint min_alpha = 255;
            tjs_uint max_alpha = 0;
            tjs_uint nonzero = 0;
            const tjs_uint step_x = std::max<tjs_uint>(1, width / 64);
            const tjs_uint step_y = std::max<tjs_uint>(1, height / 64);
            for(tjs_uint y = 0; y < height; y += step_y) {
                const auto *row = static_cast<const tjs_uint32 *>(
                    image->GetScanLineForRead(y));
                if(!row)
                    continue;
                for(tjs_uint x = 0; x < width; x += step_x) {
                    const tjs_uint32 pixel = row[x];
                    const tjs_uint alpha = (pixel >> 24) & 0xff;
                    min_alpha = std::min(min_alpha, alpha);
                    max_alpha = std::max(max_alpha, alpha);
                    if(alpha)
                        ++nonzero;
                }
            }
            auto sample = [&](tjs_uint x, tjs_uint y) -> tjs_uint32 {
                if(x >= width || y >= height)
                    return 0;
                const auto *row = static_cast<const tjs_uint32 *>(
                    image->GetScanLineForRead(y));
                return row ? row[x] : 0;
            };
            spdlog::info(
                "message-frame pixels size={}x{} minA={} maxA={} nonzero={} "
                "samples=[0x{:08x},0x{:08x},0x{:08x},0x{:08x}]",
                width, height, min_alpha, max_alpha, nonzero,
                sample(width / 2, 0), sample(width / 2, height / 4),
                sample(width / 2, height / 2), sample(width / 2, height - 1));
        }
    }
    if(!MainImage) {
        if(DisplayType == ltOpaque) {
            // fill destination with specified color
            tTVPBaseTexture *temp =
                tTVPTempBitmapHolder::GetTemp(cr.get_width(), cr.get_height());
            try {
                // do transition
                tTVPRect bitmaprect = cr;
                bitmaprect.set_offsets(0, 0);
                CopySelf(temp, 0, 0,
                         bitmaprect); // this fills temp with neutral color

                // send completion message
                TVPTraceLayerDrawGpu("drawself-complete", this, pr,
                                     bitmaprect, target);
                target->DrawCompleted(pr, temp, bitmaprect, DisplayType,
                                      Opacity);
            } catch(...) {
                tTVPTempBitmapHolder::FreeTemp();
                throw;
            }
            tTVPTempBitmapHolder::FreeTemp();
        }
        return;
    }

    // draw self MainImage(only) to target
    cr.add_offsets(-ImageLeft, -ImageTop);

    if(InTransition && !TransWithChildren && DivisibleTransHandler) {
        // transition without children

        // allocate temporary bitmap
        tTVPBaseTexture *temp =
            tTVPTempBitmapHolder::GetTemp(cr.get_width(), cr.get_height());
        try {
            // do transition
            tTVPRect bitmaprect = cr;
            bitmaprect.set_offsets(0, 0);
            DoDivisibleTransition(temp, 0, 0, cr);

            // send completion message
            TVPTraceLayerDrawGpu("drawself-complete", this, pr, bitmaprect,
                                 target);
            target->DrawCompleted(pr, temp, bitmaprect, DisplayType, Opacity);
        } catch(...) {
            tTVPTempBitmapHolder::FreeTemp();
            throw;
        }
        tTVPTempBitmapHolder::FreeTemp();
    } else {
        TVPTraceLayerDrawGpu("drawself-complete", this, pr, cr, target);
        target->DrawCompleted(pr, MainImage, cr, DisplayType, Opacity);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::CopySelfForRect(iTVPBaseBitmap *dest, tjs_int destx,
                                       tjs_int desty, const tTVPRect &srcrect) {
    // copy self image to the target
    tTVPRect cr = srcrect;
    cr.add_offsets(-ImageLeft, -ImageTop);

    if(InTransition && !TransWithChildren && DivisibleTransHandler) {
        // transition without children
        DoDivisibleTransition(dest, destx, desty, cr);
    } else {
        if(MainImage) {
            dest->CopyRect(destx, desty, MainImage, cr);
        } else {
            // main image does not exist
            // fill destination with TransparentColor
            // (this need to be transparent, so we do not use
            // NeutralColor which can be
            //  set by the user unless the DisplayType is ltOpaque)
            dest->Fill(tTVPRect(destx, desty, destx + cr.get_width(),
                                desty + cr.get_height()),
                       DisplayType == ltOpaque ? NeutralColor
                                               : TransparentColor);
        }
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::CopySelf(iTVPBaseBitmap *dest, tjs_int destx,
                                tjs_int desty, const tTVPRect &r) {
    const tTVPRect &uer = UpdateExcludeRect;
    if(uer.is_empty()) {
        CopySelfForRect(dest, destx, desty, r);
    } else {
        if(uer.top <= r.top && uer.bottom >= r.bottom) {
            if(uer.left > r.left && uer.right < r.right) {
                // split into two
                tTVPRect r2 = r;
                r2.right = uer.left;
                CopySelfForRect(dest, destx, desty, r2);
                r2.right = r.right;
                r2.left = uer.right;
                CopySelfForRect(dest, destx + (r2.left - r.left), desty, r2);
            } else if(r.left >= uer.left && r.right <= uer.right) {
                ; // nothing to do
            } else if(r.right <= uer.left || r.left >= uer.right) {
                CopySelfForRect(dest, destx, desty, r);
            } else if(r.right > uer.left && r.right <= uer.right) {
                tTVPRect r2 = r;
                r2.right = uer.left;
                CopySelfForRect(dest, destx, desty, r2);
            } else if(r.left >= uer.left && r.left < uer.left) {
                tTVPRect r2 = r;
                r2.left = uer.right;
                CopySelfForRect(dest, destx + (r2.left - r.left), desty, r2);
            } else {
                CopySelfForRect(dest, destx, desty, r);
            }

        } else {
            CopySelfForRect(dest, destx, desty, r);
        }
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::EffectImage(iTVPBaseBitmap *_dest,
                                   const tTVPRect &_destrect) {
    // if(Type == ltFilter) {
    //     // TODO: do filtering
    // } else if(Type == ltEffect) {
    //     // TODO: do effect
    // }
}

void tTJSNI_BaseLayer::Draw_GPU(tTVPDrawable *target, int x, int y,
                                const tTVPRect &r, bool visiblecheck) {
    if(visiblecheck && !IsSeen())
        return;

    tTVPRect rect;
    if(!TVPIntersectRect(&rect, r, Rect))
        return; // no intersection
    x += rect.left - r.left;
    y += rect.top - r.top;

    tTVPRect rctar(rect);
    rctar.set_offsets(x, y);

    CurrentDrawTarget = target;

    ParentRectToChildRect(rect); // to this layer based axis
    TVPTraceLayerDrawGpu("begin", this, rctar, rect, target);

    // process drawing
    DirectTransferToParent = false;

    // caching is not enabled

    if(Opacity < 255 || (InTransition && TransWithChildren)) {
        // rearrange pipe line for transition
        if(InTransition && TransWithChildren) {
            TransDrawable.Init(this, target);
            // UI scripts can finish hiding/reparenting outgoing controls one
            // event tick after StartTransition. Keep the last presented frame
            // during that tick instead of exposing and freezing the transient
            // half-torn-down layer tree.
            if(TransDrawable.SkipSnapshotFrame) {
                CurrentDrawTarget = nullptr;
                return;
            }
            // BeforeCompletion has already produced stable, fully-composited
            // source pages for divisible-fade transitions. Rewalking and
            // recompositing the complete child tree here is redundant: the
            // transition handler below reads Src1Bmp/Src2Bmp, not that freshly
            // composed intermediate. Complex UI screens can otherwise spend
            // 50-126 ms per frame doing work whose result is discarded.
            if(TransUpdateType == tutDivisibleFade &&
               TransDrawable.Src1Bmp &&
               (!TransSrc || TransDrawable.Src2Bmp)) {
                TransDrawable.DrawCompleted(
                    rctar, TransDrawable.Src1Bmp, rect, DisplayType, Opacity);
                CurrentDrawTarget = nullptr;
                return;
            }
            target = &TransDrawable;
        }
        if(GetVisibleChildrenCount() == 0) {
            DrawSelf(target, rctar, rect);
        } else {
            // rearrange pipe line for transition
            bool useTemp = false;
            if(GetCacheEnabled()) {
                UpdateBitmapForChild = CacheBitmap;
            } else {
                useTemp = true;
                UpdateBitmapForChild = tTVPTempBitmapHolder::GetTemp(
                    Rect.get_width(), Rect.get_height());
            }
            tTVPRect rectForChild(0, 0, Rect.get_width(), Rect.get_height());

            // Initialize the child composition target with this layer's own
            // pixels, or transparent/neutral pixels when the layer has no main
            // image. Transparent parent layers still need a known base before
            // children are drawn into the temporary texture.
            CopySelfForRect(UpdateBitmapForChild, 0, 0,
                            rectForChild); // transfer self image

            TVP_LAYER_FOR_EACH_CHILD_BEGIN(child) {
                // for each child...

                // visible check
                if(!child->Visible) {
                    tTVPRect empty;
                    TVPTraceLayerDrawGpuChild(this, child, empty, false);
                    continue;
                }

                // intersection check
                if(!TVPIntersectRect(&UpdateRectForChild, rectForChild,
                                     child->Rect)) {
                    tTVPRect empty;
                    TVPTraceLayerDrawGpuChild(this, child, empty, false);
                    continue;
                }
                TVPTraceLayerDrawGpuChild(this, child, UpdateRectForChild,
                                          true);

                // setup UpdateOfsX/Y UpdateRectForChildOfsX/Y
                UpdateOfsX = 0;
                UpdateOfsY = 0;
                UpdateRectForChildOfsX =
                    UpdateRectForChild.left - child->Rect.left;
                UpdateRectForChildOfsY =
                    UpdateRectForChild.top - child->Rect.top;

                // call children's "Draw" method
                child->Draw_GPU((tTVPDrawable *)this, UpdateRectForChild.left,
                                UpdateRectForChild.top, UpdateRectForChild);
            }
            TVP_LAYER_FOR_EACH_CHILD_END
            target->DrawCompleted(rctar, UpdateBitmapForChild, rect,
                                  DisplayType, Opacity);
            if(useTemp)
                tTVPTempBitmapHolder::FreeTemp();
        }
    } else {
        if(GetVisibleChildrenCount() == 0) {
            DrawSelf(target, rctar, rect);
        } else {
            DrawnRegion.Clear();
            // send completion message to the target

            // 			if (UpdateExcludeRect.top <= rect.top &&
            // UpdateExcludeRect.bottom >= rect.bottom &&
            // rect.left
            // >= UpdateExcludeRect.left && rect.right <=
            // UpdateExcludeRect.right) { 			} else
            {
                tTVPRect rc(rect);
                DrawSelf(target, rctar, rc);
            }

            TVP_LAYER_FOR_EACH_CHILD_BEGIN(child) {
                // for each child...

                // visible check
                if(!child->Visible) {
                    tTVPRect empty;
                    TVPTraceLayerDrawGpuChild(this, child, empty, false);
                    continue;
                }

                // intersection check
                tTVPRect chrect;
                if(!TVPIntersectRect(&chrect, rect, child->Rect)) {
                    tTVPRect empty;
                    TVPTraceLayerDrawGpuChild(this, child, empty, false);
                    continue;
                }
                TVPTraceLayerDrawGpuChild(this, child, chrect, true);

                // call children's "Draw" method
                child->Draw_GPU(target, x, y, rect);
            }
            TVP_LAYER_FOR_EACH_CHILD_END
        }
    }

    CurrentDrawTarget = nullptr;
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::InternalDrawNoCache_CPU(tTVPDrawable *target,
                                               const tTVPRect &rect) {
    bool totalopaque = (DisplayType == ltOpaque && Opacity == 255);
    if(GetVisibleChildrenCount() == 0) {
        // no visible children; no action needed
        tTVPRect pr = rect;
        pr.add_offsets(Rect.left, Rect.top);
        tTVPRect cr = rect;
        DrawSelf(target, pr, cr);
    } else {
        // has at least one visible child
        const tTVPComplexRect &overlapped = GetOverlappedRegion();
        const tTVPComplexRect &exposed = GetExposedRegion();

        // process overlapped region
        // clear DrawnRegion
        tTVPComplexRect::tIterator it;

        DrawnRegion.Clear();

        it = overlapped.GetIterator();
        while(it.Step()) {
            tTVPRect cr(*it);

            // intersection check
            if(!TVPIntersectRect(&cr, cr, rect))
                continue;

            tTVPRect updaterectforchild;
            bool tempalloc = false;

            // setup UpdateBitmapForChild and "updaterectforchild"
            if(totalopaque) {
                // this layer is totally opaque
                UpdateBitmapForChild =
                    target->GetDrawTargetBitmap(cr, updaterectforchild);
            } else {
                // this layer is transparent

                // retrieve temporary bitmap
                UpdateBitmapForChild = tTVPTempBitmapHolder::GetTemp(
                    cr.get_width(), cr.get_height());
                tempalloc = true;
                updaterectforchild.left = 0;
                updaterectforchild.top = 0;
                updaterectforchild.right = cr.get_width();
                updaterectforchild.bottom = cr.get_height();
            }

            try {
                // copy self image to the target
                CopySelf(UpdateBitmapForChild, updaterectforchild.left,
                         updaterectforchild.top, cr);

                TVP_LAYER_FOR_EACH_CHILD_BEGIN(child) {
                    // for each child...

                    // visible check
                    if(!child->Visible)
                        continue;

                    // intersection check
                    tTVPRect chrect;
                    if(!TVPIntersectRect(&chrect, cr, child->Rect))
                        continue;

                    // setup UpdateRectForChild
                    tjs_int ox = chrect.left - cr.left;
                    tjs_int oy = chrect.top - cr.top;

                    UpdateRectForChild = updaterectforchild;
                    UpdateRectForChild.add_offsets(ox, oy);
                    UpdateRectForChildOfsX = chrect.left - child->Rect.left;
                    UpdateRectForChildOfsY = chrect.top - child->Rect.top;

                    // setup UpdateOfsX, UpdateOfsY
                    UpdateOfsX = cr.left - updaterectforchild.left;
                    UpdateOfsY = cr.top - updaterectforchild.top;

                    // call children's "Draw" method
                    child->Draw((tTVPDrawable *)this, chrect, true);
                }
                TVP_LAYER_FOR_EACH_CHILD_END

            } catch(...) {
                if(tempalloc)
                    tTVPTempBitmapHolder::FreeTemp();
                throw;
            }

            // send completion message to the target
            if(DisplayType != ltBinder) {
                tTVPRect pr = cr;
                pr.add_offsets(Rect.left, Rect.top);
                target->DrawCompleted(pr, UpdateBitmapForChild,
                                      updaterectforchild, DisplayType, Opacity);
            }

            // release temporary bitmap
            if(tempalloc)
                tTVPTempBitmapHolder::FreeTemp();

        } // overlapped region

        // process exposed region
        DirectTransferToParent =
            true; // this flag is used only when MainImage == nullptr

        it = exposed.GetIterator();
        while(it.Step()) {
            tTVPRect cr(*it);

            // intersection check
            if(!TVPIntersectRect(&cr, cr, rect))
                continue;

            if(MainImage != nullptr) {
                // send completion message to the target
                tTVPRect pr = cr;
                pr.add_offsets(Rect.left, Rect.top);
                DrawSelf(target, pr, cr);
            } else {
                // call children's "Draw" method

                tTVPRect cr(*it);

                // intersection check
                if(!TVPIntersectRect(&cr, cr, rect))
                    continue;

                tTVPRect updaterectforchild;

                TVP_LAYER_FOR_EACH_CHILD_BEGIN(child) {
                    // for each child...

                    // visible check
                    if(!child->Visible)
                        continue;

                    // intersection check
                    tTVPRect chrect;
                    if(!TVPIntersectRect(&chrect, cr, child->Rect))
                        continue;

                    // call children's "Draw" method
                    child->Draw((tTVPDrawable *)this, chrect, true);
                }
                TVP_LAYER_FOR_EACH_CHILD_END
            }
        }
        DirectTransferToParent = false;
    } // has visible children/no visible children
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::Draw(tTVPDrawable *target, const tTVPRect &r,
                            bool visiblecheck) {
    // process updating pipe line.
    // draw the layer content to "target".
    // "r" is a rectangle to be drawn in the parent's coordinates.
    // parent has responsibility for piling the image returned from
    // children.

    if(visiblecheck && !IsSeen())
        return;

    tTVPRect rect = r;
    if(!TVPIntersectRect(&rect, rect, Rect))
        return; // no intersection

    CurrentDrawTarget = target;

    ParentRectToChildRect(rect);

    if(InTransition && TransWithChildren) {
        // rearrange pipe line for transition
        TransDrawable.Init(this, target);
        target = &TransDrawable;
    }

    // process drawing
    DirectTransferToParent = false;
    bool totalopaque = (DisplayType == ltOpaque && Opacity == 255);

    if(GetCacheEnabled() &&
       !(InTransition && !TransWithChildren && DivisibleTransHandler)) {
        // process must-recalc region

        tTVPComplexRect::tIterator it = CacheRecalcRegion.GetIterator();
        while(it.Step()) {
            tTVPRect cr(*it);

            // intersection check
            if(!TVPIntersectRect(&cr, cr, rect))
                continue;

            // clear DrawnRegion
            DrawnRegion.Clear();

            // setup UpdateBitmapForChild
            UpdateBitmapForChild = CacheBitmap;

            // copy self image to UpdateBitmapForChild
            if(MainImage != nullptr) {
                CopySelf(UpdateBitmapForChild, cr.left, cr.top,
                         cr); // transfer self image
            }

            TVP_LAYER_FOR_EACH_CHILD_BEGIN(child) {
                // for each child...

                // intersection check
                if(!TVPIntersectRect(&UpdateRectForChild, cr, child->Rect))
                    continue;

                // setup UpdateOfsX/Y UpdateRectForChildOfsX/Y
                UpdateOfsX = 0;
                UpdateOfsY = 0;
                UpdateRectForChildOfsX =
                    UpdateRectForChild.left - child->Rect.left;
                UpdateRectForChildOfsY =
                    UpdateRectForChild.top - child->Rect.top;

                // call children's "Draw" method
                child->Draw((tTVPDrawable *)this, UpdateRectForChild, true);
            }
            TVP_LAYER_FOR_EACH_CHILD_END

            // special optimazation for MainImage == nullptr

            if(MainImage == nullptr) {
                tTVPComplexRect nr;
                nr.Or(cr);
                nr.Sub(DrawnRegion);
                tTVPComplexRect::tIterator it = nr.GetIterator();
                while(it.Step()) {
                    tTVPRect r(*it);
                    CopySelf(UpdateBitmapForChild, r.left, r.top, r);
                    // CopySelf of MainImage == nullptr actually
                    // fills target rectangle with full transparency
                }
            }
        }

        CacheRecalcRegion.Sub(rect);

        if(CacheRecalcRegion.GetCount() > TVP_CACHE_UNITE_LIMIT)
            CacheRecalcRegion.Unite();

        // at this point, the cache bitmap should be completed

        // send completion message to the target
        tTVPRect pr = rect;
        pr.add_offsets(Rect.left, Rect.top);
        target->DrawCompleted(pr, CacheBitmap, rect, DisplayType, Opacity);
    } else {
        // caching is not enabled

        if(GetVisibleChildrenCount() == 0) {
            // no visible children; no action needed
            tTVPRect pr = rect;
            pr.add_offsets(Rect.left, Rect.top);
            tTVPRect cr = rect;
            DrawSelf(target, pr, cr);
        } else {
            // has at least one visible child
            const tTVPComplexRect &overlapped = GetOverlappedRegion();
            const tTVPComplexRect &exposed = GetExposedRegion();

            // process overlapped region
            // clear DrawnRegion
            tTVPComplexRect::tIterator it;

            DrawnRegion.Clear();

            it = overlapped.GetIterator();
            while(it.Step()) {
                tTVPRect cr(*it);

                // intersection check
                if(!TVPIntersectRect(&cr, cr, rect))
                    continue;

                tTVPRect updaterectforchild;
                bool tempalloc = false;

                // setup UpdateBitmapForChild and "updaterectforchild"
                if(totalopaque) {
                    // this layer is totally opaque
                    UpdateBitmapForChild =
                        target->GetDrawTargetBitmap(cr, updaterectforchild);
                } else {
                    // this layer is transparent

                    // retrieve temporary bitmap
                    UpdateBitmapForChild = tTVPTempBitmapHolder::GetTemp(
                        cr.get_width(), cr.get_height());
                    tempalloc = true;
                    updaterectforchild.left = 0;
                    updaterectforchild.top = 0;
                    updaterectforchild.right = cr.get_width();
                    updaterectforchild.bottom = cr.get_height();
                }

                try {
                    // copy self image to the target
                    CopySelf(UpdateBitmapForChild, updaterectforchild.left,
                             updaterectforchild.top, cr);

                    TVP_LAYER_FOR_EACH_CHILD_BEGIN(child) {
                        // for each child...

                        // visible check
                        if(!child->Visible)
                            continue;

                        // intersection check
                        tTVPRect chrect;
                        if(!TVPIntersectRect(&chrect, cr, child->Rect))
                            continue;

                        // setup UpdateRectForChild
                        tjs_int ox = chrect.left - cr.left;
                        tjs_int oy = chrect.top - cr.top;

                        UpdateRectForChild = updaterectforchild;
                        UpdateRectForChild.add_offsets(ox, oy);
                        UpdateRectForChildOfsX = chrect.left - child->Rect.left;
                        UpdateRectForChildOfsY = chrect.top - child->Rect.top;

                        // setup UpdateOfsX, UpdateOfsY
                        UpdateOfsX = cr.left - updaterectforchild.left;
                        UpdateOfsY = cr.top - updaterectforchild.top;

                        // call children's "Draw" method
                        child->Draw((tTVPDrawable *)this, chrect, true);
                    }
                    TVP_LAYER_FOR_EACH_CHILD_END

                } catch(...) {
                    if(tempalloc)
                        tTVPTempBitmapHolder::FreeTemp();
                    throw;
                }

                // send completion message to the target
                if(DisplayType != ltBinder) {
                    tTVPRect pr = cr;
                    pr.add_offsets(Rect.left, Rect.top);
                    target->DrawCompleted(pr, UpdateBitmapForChild,
                                          updaterectforchild, DisplayType,
                                          Opacity);
                }

                // release temporary bitmap
                if(tempalloc)
                    tTVPTempBitmapHolder::FreeTemp();

            } // overlapped region

            // process exposed region
            DirectTransferToParent = true; // this flag is used only when
                                           // MainImage == nullptr

            it = exposed.GetIterator();
            while(it.Step()) {
                tTVPRect cr(*it);

                // intersection check
                if(!TVPIntersectRect(&cr, cr, rect))
                    continue;

                if(MainImage != nullptr) {
                    // send completion message to the target
                    tTVPRect pr = cr;
                    pr.add_offsets(Rect.left, Rect.top);
                    DrawSelf(target, pr, cr);
                } else {
                    // call children's "Draw" method

                    tTVPRect cr(*it);

                    // intersection check
                    if(!TVPIntersectRect(&cr, cr, rect))
                        continue;

                    tTVPRect updaterectforchild;

                    TVP_LAYER_FOR_EACH_CHILD_BEGIN(child) {
                        // for each child...

                        // visible check
                        if(!child->Visible)
                            continue;

                        // intersection check
                        tTVPRect chrect;
                        if(!TVPIntersectRect(&chrect, cr, child->Rect))
                            continue;

                        // call children's "Draw" method
                        child->Draw((tTVPDrawable *)this, chrect, true);
                    }
                    TVP_LAYER_FOR_EACH_CHILD_END
                }
            }
            DirectTransferToParent = false;
        } // has visible children/no visible children
    } // cache enabled/disabled

    CurrentDrawTarget = nullptr;
}

//---------------------------------------------------------------------------
tTVPBaseTexture *tTJSNI_BaseLayer::GetDrawTargetBitmap(const tTVPRect &rect,
                                                       tTVPRect &cliprect) {
    // called from children to get the image buffer drawn to.
    if(DisplayType == ltBinder ||
       (MainImage == nullptr && DirectTransferToParent)) {
        tTVPRect _rect(rect);
        _rect.add_offsets(Rect.left, Rect.top);
        tTVPBaseTexture *bmp =
            CurrentDrawTarget->GetDrawTargetBitmap(_rect, cliprect);
        return bmp;
    }
    tjs_int w = rect.get_width();
    tjs_int h = rect.get_height();
    if(UpdateRectForChild.get_width() < w ||
       UpdateRectForChild.get_height() < h)
        TVPThrowExceptionMessage(TVPInternalError);
    cliprect = UpdateRectForChild;
    cliprect.add_offsets(rect.left - UpdateRectForChildOfsX,
                         rect.top - UpdateRectForChildOfsY);
    return UpdateBitmapForChild;
}

//---------------------------------------------------------------------------
tTVPLayerType tTJSNI_BaseLayer::GetTargetLayerType() {
    if(DisplayType == ltBinder) // return parent's display layer type
                                // when DisplayType == ltBinder
        return Parent ? Parent->DisplayType : ltOpaque;
    return DisplayType;
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::DrawCompleted(const tTVPRect &destrect,
                                     tTVPBaseTexture *bmp,
                                     const tTVPRect &cliprect,
                                     tTVPLayerType type, tjs_int opacity) {
    TVPTraceLayerDrawGpu("child-complete-in", this, destrect, cliprect,
                         CurrentDrawTarget);
    if(const char *trace = std::getenv("AETHERKIRI_COMPOSE_LAYER_TRACE");
       trace && *trace && *trace != '0' && bmp) {
        const std::string name = GetName().AsStdString();
        const bool focused = name.find("メッセージレイヤ") != std::string::npos ||
            name == "colorframe" || name == "colorbase" ||
            name == "colorover" || name == "frame" || name == "truss" ||
            name.find("/02") != std::string::npos ||
            name.find("<01") != std::string::npos ||
            name.find("<02") != std::string::npos;
        if(focused) {
            static std::atomic<int> count{0};
            if(count.fetch_add(1, std::memory_order_relaxed) < 3000) {
                const tjs_int sx = std::clamp(
                    cliprect.left + cliprect.get_width() / 2, 0,
                    static_cast<tjs_int>(bmp->GetWidth()) - 1);
                const tjs_int sy = std::clamp(
                    cliprect.top + cliprect.get_height() / 2, 0,
                    static_cast<tjs_int>(bmp->GetHeight()) - 1);
                spdlog::info(
                    "LayerCompose layer={} display={} type={} opacity={} "
                    "dest=({},{} {}x{}) clip=({},{} {}x{}) bmp={}x{} "
                    "src=0x{:08x}",
                    name, ttstr(GetTypeNameString()).AsStdString(),
                    static_cast<int>(type), opacity, destrect.left,
                    destrect.top, destrect.get_width(), destrect.get_height(),
                    cliprect.left, cliprect.top, cliprect.get_width(),
                    cliprect.get_height(), bmp->GetWidth(), bmp->GetHeight(),
                    bmp->GetPoint(sx, sy));
            }
        }
    }
    // called from children to notify that the image drawing is
    // completed. blend the image to the target unless bmp is the same
    // as UpdateBitmapForChild.
    if(DisplayType == ltBinder ||
       (MainImage == nullptr && DirectTransferToParent)) {
        tTVPRect _destrect(destrect);
        tTVPRect _cliprect(cliprect);
        _destrect.add_offsets(Rect.left, Rect.top);
        CurrentDrawTarget->DrawCompleted(_destrect, bmp, _cliprect, type,
                                         opacity);
        return;
    }

    if(bmp != UpdateBitmapForChild) {
        if(MainImage == nullptr) {
            // special optimization for MainImage == nullptr
            // (all the layer face is treated as transparent)
            tTVPComplexRect nr; // new region
            nr.Or(destrect);
            nr.Sub(DrawnRegion);
            tTVPComplexRect opr; // operation region
            // now nr is a client region which is not overlapped by
            // children at this time
            if(DisplayType == type && opacity == 255) {
                // DisplayType == type and full opacity
                // just copy the target bitmap
                tTVPComplexRect::tIterator it = nr.GetIterator();
                while(it.Step()) {
                    tTVPRect r(*it);
                    tTVPRect sr;
                    sr.left = cliprect.left + (r.left - destrect.left);
                    sr.top = cliprect.top + (r.top - destrect.top);
                    sr.right = sr.left + r.get_width();
                    sr.bottom = sr.top + r.get_height();

                    UpdateBitmapForChild->CopyRect(r.left - UpdateOfsX,
                                                   r.top - UpdateOfsY, bmp, sr);
                }
                // calculate operation region
                opr.Or(destrect);
                opr.Sub(nr);
            } else {
                // set operation region
                tTVPComplexRect::tIterator it = nr.GetIterator();
                while(it.Step()) {
                    tTVPRect r(*it);
                    r.add_offsets(-UpdateOfsX, -UpdateOfsY);
                    // fill r with transparent color
                    CopySelf(UpdateBitmapForChild, r.left, r.top, r);
                    // CopySelf of MainImage == nullptr actually
                    // fills target rectangle with full transparency
                }
                opr.Or(destrect);
            }

            // operate r
            tTVPComplexRect::tIterator it = opr.GetIterator();
            while(it.Step()) {
                tTVPRect r(*it);
                tTVPRect sr;
                sr.left = cliprect.left + (r.left - destrect.left);
                sr.top = cliprect.top + (r.top - destrect.top);
                sr.right = sr.left + r.get_width();
                sr.bottom = sr.top + r.get_height();

                BltImage(UpdateBitmapForChild, DisplayType, r.left - UpdateOfsX,
                         r.top - UpdateOfsY, bmp, sr, type, opacity);
            }

            // update DrawnRegion
            DrawnRegion.Or(destrect);
        } else {
            BltImage(UpdateBitmapForChild, DisplayType,
                     destrect.left - UpdateOfsX, destrect.top - UpdateOfsY, bmp,
                     cliprect, type, opacity);
        }
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::InternalComplete2(tTVPComplexRect &updateregion,
                                         tTVPDrawable *drawable) {
    //--- querying phase

    // search ltOpaque, not to draw region behind them.
    if(Manager)
        Manager->QueryUpdateExcludeRect();

    //--- drawing phase

    // split the region to some stripes to utilize the CPU's memory
    // caching.

    // tjs_int i;
    tTVPComplexRect::tIterator it = updateregion.GetIterator();
    while(it.Step()) {
        tTVPRect r(*it);

        // Add layer offset because Draw() accepts the position in
        // *parent* 's coordinates.
        r.add_offsets(Rect.left, Rect.top);

        if(TVPGraphicSplitOperationType != gsotNone) {
            // compute optimum height of the stripe
            tjs_int oh;

            if(GetVisibleChildrenCount() || InTransition) {
                // split
                tjs_int rw = r.get_width();
                if(rw < 40)
                    oh = 256;
                else if(rw < 80)
                    oh = 128;
                else if(rw < 160)
                    oh = 64;
                else if(rw < 320)
                    oh = 32;
                else
                    oh = 16; // 2 lines per core in modern 8 cores cpu
            } else {
                // no need to split
                oh = r.get_height();
            }

            // split to some stripes
            tjs_int y;
            tTVPRect opr;
            opr.left = r.left;
            opr.right = r.right;
            if(TVPGraphicSplitOperationType == gsotInterlace) {
                // interlaced split
                for(y = r.top; y < r.bottom; y += oh * 2) {
                    opr.top = y;
                    opr.bottom = (y + oh < r.bottom) ? y + oh : r.bottom;

                    // call "Draw" to draw to the window
                    Draw(drawable, opr, false);
                }
                for(y = r.top + oh; y < r.bottom; y += oh * 2) {
                    opr.top = y;
                    opr.bottom = (y + oh < r.bottom) ? y + oh : r.bottom;

                    // call "Draw" to draw to the window
                    Draw(drawable, opr, false);
                }
            } else if(TVPGraphicSplitOperationType == gsotSimple) {
                // non-interlaced
                for(y = r.top; y < r.bottom; y += oh) {
                    opr.top = y;
                    opr.bottom = (y + oh < r.bottom) ? y + oh : r.bottom;

                    // call "Draw" to draw to the window
                    Draw(drawable, opr, false);
                }
            } else if(TVPGraphicSplitOperationType == gsotBiDirection) {
                // bidirection
                static int direction = 0;
                if(direction & 1) {
                    for(y = r.top; y < r.bottom; y += oh) {
                        opr.top = y;
                        opr.bottom = (y + oh < r.bottom) ? y + oh : r.bottom;

                        // call "Draw" to draw to the window
                        Draw(drawable, opr, false);
                    }
                } else {
                    y = r.bottom - oh;
                    if(y < r.top)
                        y = r.top;
                    while(1) {
                        opr.top = (y < r.top ? r.top : y);
                        opr.bottom = (y + oh < r.bottom) ? y + oh : r.bottom;

                        if(opr.bottom <= r.top)
                            break;

                        // call "Draw" to draw to the window
                        Draw(drawable, opr, false);

                        y -= oh;
                    }
                }
                direction++;
            }
        } else {
            // don't split scanlines
            Draw(drawable, r, false);
        }
    }

    updateregion.Clear();
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::InternalComplete2_GPU(tTVPRect updateregion,
                                             tTVPDrawable *drawable,
                                             bool localDestination) {
    if(Manager)
        Manager->QueryUpdateExcludeRect();
    const auto coordinates =
        TVPLayerInternal::ResolveGpuCompletionCoordinates(
            updateregion.left, updateregion.top, updateregion.right,
            updateregion.bottom, Rect.left, Rect.top, localDestination);
    updateregion.left = coordinates.parentLeft;
    updateregion.top = coordinates.parentTop;
    updateregion.right = coordinates.parentRight;
    updateregion.bottom = coordinates.parentBottom;
    // Draw_GPU receives its rectangle in the layer's parent coordinates.
    // A window destination uses those same coordinates. Complete(), however,
    // renders into a layer-local cache bitmap, so its destination must retain
    // the unshifted local position even though the source rectangle is shifted
    // for intersection with this layer's Rect.
    Draw_GPU(drawable, coordinates.destinationX, coordinates.destinationY,
             updateregion, false);
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::InternalComplete(tTVPComplexRect &updateregion,
                                        tTVPDrawable *drawable) {
    BeforeCompletion();

    // at this point, final update region (in this completion) is
    // determined
    InCompletion = true;

    if(IsGPU()) {
        InternalComplete2_GPU(updateregion.GetBound(), drawable, true);
    } else {
        InternalComplete2(updateregion, drawable);
    }

    InCompletion = false;
    AfterCompletion();
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::CompleteForWindow(tTVPDrawable *drawable) {
    const bool profileCompletion = layerCompletionProfileEnabled() &&
        !g_layerCompletionProfile.active;
    if(profileCompletion) {
        g_layerCompletionProfile = {};
        g_layerCompletionProfile.active = true;
        static thread_local std::size_t completionCallCounter = 0;
        g_layerCompletionProfile.completionCalls = ++completionCallCounter;
        g_layerCompletionProfile.started = std::chrono::steady_clock::now();
    }
    BeforeCompletion();
    if(profileCompletion) {
        const double beforeMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() -
            g_layerCompletionProfile.started).count();
        if(beforeMs >= layerCompletionProfileSlowMs() ||
           g_layerCompletionProfile.completionCalls % 120u == 0u) {
            if(auto logger = spdlog::get("core")) {
                logger->info(
                    "layer completion profile: root={} before_ms={:.3f} "
                    "before_calls={} onPaint_calls={} onPaint_ms={:.3f} "
                    "max_onPaint_ms={:.3f} max_onPaint_layer={} "
                    "transition_calls={} "
                    "max_depth={} completion_call={}",
                    static_cast<const void *>(this), beforeMs,
                    g_layerCompletionProfile.beforeCalls,
                    g_layerCompletionProfile.onPaintCalls,
                    g_layerCompletionProfile.onPaintMs,
                    g_layerCompletionProfile.maxOnPaintMs,
                    g_layerCompletionProfile.maxOnPaintLayer,
                    g_layerCompletionProfile.transitionCalls,
                    g_layerCompletionProfile.maxDepth,
                    g_layerCompletionProfile.completionCalls);
            }
        }
        g_layerCompletionProfile.active = false;
    }

    if(Manager)
        Manager->NotifyUpdateRegionFixed();

    InCompletion = true;

    if(Manager)
        Manager->GetLayerTreeOwner()->StartBitmapCompletion(Manager);
    try {
        if(IsGPU()) {
            if(Manager) {
                tTVPComplexRect &updateRegion =
                    Manager->GetUpdateRegionForCompletion();
                if(updateRegion.GetCount() > 0) {
                    const tTVPRect dirty = updateRegion.GetBound();
                    // Small text/cursor updates can stay local, but a freshly
                    // rendered E-mote texture must be composited as one
                    // complete frame. Otherwise the presented double buffer
                    // can alternate between old and new character contents.
                    // Keep an opt-in full-frame diagnostic for hosts where a
                    // partial completion may expose an invalid cached region.
                    // This is intentionally environment-gated so normal
                    // rendering retains the dirty-rectangle fast path.
                    const char *force_full_env =
                        std::getenv("AETHERKIRI_FORCE_FULL_FRAME_REDRAW");
                    const bool force_full =
                        force_full_env && *force_full_env && *force_full_env != '0';
                    InternalComplete2_GPU(
                        (force_full || TVPConsumeFullGpuCompletionRequest())
                            ? Rect
                            : dirty,
                        drawable, false);
                    updateRegion.Clear();
                }
            } else {
                InternalComplete2_GPU(Rect, drawable, false);
            }
        } else {
            InternalComplete2(Manager->GetUpdateRegionForCompletion(),
                              drawable);
        }
    } catch(...) {
        if(Manager)
            Manager->GetLayerTreeOwner()->EndBitmapCompletion(Manager);
        throw;
    }
    if(Manager)
        Manager->GetLayerTreeOwner()->EndBitmapCompletion(Manager);

    InCompletion = false;
    AfterCompletion();
}

//---------------------------------------------------------------------------
tTVPBaseTexture *tTJSNI_BaseLayer::Complete(const tTVPRect &rect) {
    class tCompleteDrawable : public tTVPDrawable {
    protected:
        tTVPBaseTexture *Bitmap;
        tTVPRect BitmapRect;
        tTVPLayerType LayerType;

    public:
        tCompleteDrawable(tTVPBaseTexture *bmp, tTVPLayerType layertype) :
            Bitmap(bmp), LayerType(layertype) {};

        tTVPBaseTexture *GetDrawTargetBitmap(const tTVPRect &rect,
                                             tTVPRect &cliprect) override {
            cliprect = rect;
            return Bitmap;
        }

        tTVPLayerType GetTargetLayerType() override { return LayerType; }

        void DrawCompleted(const tTVPRect &destrect, tTVPBaseTexture *bmp,
                           const tTVPRect &cliprect, tTVPLayerType type,
                           tjs_int opacity) override {
            if(bmp != Bitmap) {
                Bitmap->CopyRect(destrect.left, destrect.top, bmp, cliprect);
            }
        }
    };

    class tCompleteDrawable_GPU : public tCompleteDrawable {
    public:
        tCompleteDrawable_GPU(tTVPBaseTexture *bmp, tTVPLayerType layertype) :
            tCompleteDrawable(bmp, layertype) {
            bmp->Fill(tTVPRect(0, 0, bmp->GetWidth(), bmp->GetHeight()),
                      layertype == ltOpaque ? 0xFF000000 : 0);
        };

        void DrawCompleted(const tTVPRect &destrect, tTVPBaseTexture *bmp,
                           const tTVPRect &cliprect, tTVPLayerType type,
                           tjs_int opacity) override {
            if(bmp != Bitmap) {
                BltImage(Bitmap, LayerType, destrect.left, destrect.top, bmp,
                         cliprect, type, opacity, LayerType == ltOpaque);
            }
        }
    };

    // complete given rectangle of cache.

    if(!MainImage && _bitmapEvicted)
        EnsureBitmap();

    if(!GetCacheEnabled())
        return nullptr;
    // caller must ensure that the caching is enabled

    if(MainImage && GetVisibleChildrenCount() == 0 && ImageLeft == 0 &&
       ImageTop == 0 && MainImage->GetWidth() == GetWidth() &&
       MainImage->GetHeight() == GetHeight()) {
        // the layer has no visible children
        // and entire of the bitmap is the visible area.
        // simply returns main image
        return MainImage;
    }

    if(CacheRecalcRegion.GetCount() == 0) {
        // the layer has not region to reconstruct
        return CacheBitmap;
    }

    tTVPComplexRect ur;
    ur.Or(rect);
    if(IsGPU()) {
        tCompleteDrawable_GPU drawable(CacheBitmap, DisplayType);
        InternalComplete(ur, &drawable); // complete cache
    } else {
        // create drawable object
        tCompleteDrawable drawable(CacheBitmap, DisplayType);

        // complete
        InternalComplete(ur, &drawable); // complete cache
    }
    return CacheBitmap;
}

//---------------------------------------------------------------------------
tTVPBaseTexture *tTJSNI_BaseLayer::Complete() {
    // complete entire area of the layer
    tTVPRect r;
    r.left = 0;
    r.top = 0;
    r.right = Rect.get_width();
    r.bottom = Rect.get_height();

    return Complete(r);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// transition management
//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::StartTransition(const ttstr &name, bool withchildren,
                                       tTJSNI_BaseLayer *transsource,
                                       tTJSVariantClosure options) {
    // start transition

    // is current transition processing?
    if(InTransition) {
        TVPThrowExceptionMessage(TVPCurrentTransitionMustBeStopping);
    }

    if(transsource && transsource->TransSrc == this) {
        TVPThrowExceptionMessage(TVPTransitionMutualSource);
    }
    if(TVPIsKagBackgroundPair(this, transsource)) {
        TVPClearExchangedKagPageRouting(this, transsource);
    }

    // pointers which must be released at last...
    iTVPTransHandlerProvider *pro = nullptr;
    tTVPSimpleOptionProvider *sop = nullptr;
    iTVPBaseTransHandler *handler = nullptr;

    try {
        // find transition handler
        pro = TVPFindTransHandlerProvider(name);
        // this may raise an exception

        // check selfupdate member of 'options'
        tTJSVariant var;
        TransSelfUpdate = false;
        static ttstr selfupdate_name(TJS_W("selfupdate"));
        if(TJS_SUCCEEDED(options.PropGet(0, selfupdate_name.c_str(),
                                         selfupdate_name.GetHint(), &var,
                                         nullptr))) {
            if(var.Type() != tvtVoid)
                TransSelfUpdate = 0 != (tjs_int)var;
            // selfupdate member found
        }

        // check callback member of 'options'
        TransTickCallback = tTJSVariantClosure(nullptr, nullptr);
        static ttstr callback_name(TJS_W("callback"));
        UseTransTickCallback = false;
        if(TJS_SUCCEEDED(options.PropGet(0, callback_name.c_str(),
                                         callback_name.GetHint(), &var,
                                         nullptr))) {
            // selfupdate member found
            if(var.Type() != tvtVoid) {
                TransTickCallback = var.AsObjectClosure(); // AddRef() is
                                                           // performed here
                UseTransTickCallback = true;
            }
        }

        // create option provider
        sop = new tTVPSimpleOptionProvider(options);

        tjs_int destLayerWidth = GetWidth();
        tjs_int destLayerHeight = GetHeight();
        const tjs_int destImageWidth = MainImage ? MainImage->GetWidth() : -1;
        const tjs_int destImageHeight = MainImage ? MainImage->GetHeight() : -1;
        tjs_int srcLayerWidth = transsource ? transsource->GetWidth() : 0;
        tjs_int srcLayerHeight = transsource ? transsource->GetHeight() : 0;
        const tjs_int srcImageWidth =
            (transsource && transsource->MainImage)
                ? transsource->MainImage->GetWidth()
                : -1;
        const tjs_int srcImageHeight =
            (transsource && transsource->MainImage)
                ? transsource->MainImage->GetHeight()
                : -1;

        const bool kag_background_pair =
            TVPIsKagBackgroundPair(this, transsource);
        if(withchildren && kag_background_pair && MainImage &&
           transsource && transsource->MainImage &&
           destLayerWidth > 0 && destLayerHeight > 0 &&
           srcLayerWidth > 0 && srcLayerHeight > 0 &&
           (destLayerWidth != srcLayerWidth ||
            destLayerHeight != srcLayerHeight)) {
            tjs_int commonWidth = std::max(destLayerWidth, srcLayerWidth);
            tjs_int commonHeight = std::max(destLayerHeight, srcLayerHeight);
            const tTJSNI_BaseLayer *destParent = Parent;
            const tTJSNI_BaseLayer *srcParent = transsource->Parent;
            const bool sameSizedParents =
                destParent && srcParent && destParent->GetWidth() > 0 &&
                destParent->GetHeight() > 0 &&
                destParent->GetWidth() == srcParent->GetWidth() &&
                destParent->GetHeight() == srcParent->GetHeight();
            if(sameSizedParents) {
                commonWidth = std::min<tjs_int>(
                    commonWidth, static_cast<tjs_int>(destParent->GetWidth()));
                commonHeight = std::min<tjs_int>(
                    commonHeight, static_cast<tjs_int>(destParent->GetHeight()));
            }
            const tjs_int maxImageWidth =
                std::max(destImageWidth, srcImageWidth);
            const tjs_int maxImageHeight =
                std::max(destImageHeight, srcImageHeight);
            const bool dimensionsPlausible =
                maxImageWidth > 0 && maxImageHeight > 0 &&
                commonWidth <= maxImageWidth &&
                commonHeight <= maxImageHeight;

            if(dimensionsPlausible) {
                if(TVPLayerTransitionTraceEnabled()) {
                    spdlog::info(
                        "LayerTrans align-kag-background layer={} {}x{} "
                        "src={} {}x{} destImage={}x{} srcImage={}x{} "
                        "parent={}x{} common={}x{}",
                        GetName().AsStdString(), destLayerWidth,
                        destLayerHeight, transsource->GetName().AsStdString(),
                        srcLayerWidth, srcLayerHeight, destImageWidth,
                        destImageHeight, srcImageWidth, srcImageHeight,
                        sameSizedParents ? destParent->GetWidth() : 0,
                        sameSizedParents ? destParent->GetHeight() : 0,
                        commonWidth, commonHeight);
                }
                if(destLayerWidth != commonWidth ||
                   destLayerHeight != commonHeight) {
                    InternalSetSize(static_cast<tjs_uint>(commonWidth),
                                    static_cast<tjs_uint>(commonHeight));
                    destLayerWidth = GetWidth();
                    destLayerHeight = GetHeight();
                }
                if(srcLayerWidth != commonWidth ||
                   srcLayerHeight != commonHeight) {
                    transsource->InternalSetSize(
                        static_cast<tjs_uint>(commonWidth),
                        static_cast<tjs_uint>(commonHeight));
                    srcLayerWidth = transsource->GetWidth();
                    srcLayerHeight = transsource->GetHeight();
                }
            } else if(TVPLayerTransitionTraceEnabled()) {
                spdlog::warn(
                    "LayerTrans skip-kag-background-align layer={} {}x{} "
                    "src={} {}x{} destImage={}x{} srcImage={}x{}",
                    GetName().AsStdString(), destLayerWidth, destLayerHeight,
                    transsource->GetName().AsStdString(), srcLayerWidth,
                    srcLayerHeight, destImageWidth, destImageHeight,
                    srcImageWidth, srcImageHeight);
            }
        }

        const tjs_int providerDestWidth =
            withchildren ? destLayerWidth : destImageWidth;
        const tjs_int providerDestHeight =
            withchildren ? destLayerHeight : destImageHeight;
        const tjs_int providerSrcWidth =
            transsource ? (withchildren ? srcLayerWidth : srcImageWidth) : 0;
        const tjs_int providerSrcHeight =
            transsource ? (withchildren ? srcLayerHeight : srcImageHeight) : 0;

        if(TVPLayerTransitionTraceEnabled()) {
            spdlog::info(
                "LayerTrans request name={} withChildren={} layer={} native={} "
                "layerSize={}x{} imageSize={}x{} src={} srcNative={} "
                "srcLayerSize={}x{} srcImageSize={}x{} providerDest={}x{} "
                "providerSrc={}x{}",
                name.AsStdString(), withchildren ? "yes" : "no",
                GetName().AsStdString(), static_cast<void *>(this),
                destLayerWidth, destLayerHeight, destImageWidth,
                destImageHeight,
                transsource ? transsource->GetName().AsStdString() : "<null>",
                static_cast<void *>(transsource), srcLayerWidth,
                srcLayerHeight, srcImageWidth, srcImageHeight,
                providerDestWidth, providerDestHeight, providerSrcWidth,
                providerSrcHeight);
        }

        // notify starting of the transition to the provider
        tjs_error er = pro->StartTransition(
            sop, &TVPSimpleImageProvider, DisplayType,
            providerDestWidth, providerDestHeight, providerSrcWidth,
            providerSrcHeight,
            &TransType, &TransUpdateType, &handler);

        if(TJS_FAILED(er))
            TVPThrowExceptionMessage(
                TVPTransHandlerError,
                TJS_W("iTVPTransHandlerProvider::StartTransition "
                      "failed"));

        if(TransUpdateType != tutDivisibleFade &&
           TransUpdateType != tutDivisible && TransUpdateType != tutGiveUpdate)
            TVPThrowExceptionMessage(TVPTransHandlerError,
                                     (const tjs_char *)TVPUnknownUpdateType);

        if(TransType != ttSimple && TransType != ttExchange)
            TVPThrowExceptionMessage(
                TVPTransHandlerError,
                (const tjs_char *)TVPUnknownTransitionType);

        // check update type
        if(TransUpdateType == tutGiveUpdate)
            TVPThrowExceptionMessage(
                TVPTransHandlerError,
                (const tjs_char *)TVPUnsupportedUpdateTypeTutGiveUpdate);
        // sorry for inconvinience
        if(TransType == ttExchange && !transsource)
            TVPThrowExceptionMessage(TVPSpecifyTransitionSource);

        // check wether the source and destination both have image
        if(!withchildren) {
            if(!MainImage)
                TVPThrowExceptionMessage(
                    TVPTransitionSourceAndDestinationMustHaveImage);
            if(transsource && !transsource->MainImage)
                TVPThrowExceptionMessage(
                    TVPTransitionSourceAndDestinationMustHaveImage);
        }

        // set to cache
        TransWithChildren = withchildren;
        delete TransDrawable.Src1Bmp;
        delete TransDrawable.Src2Bmp;
        TransDrawable.Src1Bmp = nullptr;
        TransDrawable.Src2Bmp = nullptr;
        TransDrawable.SnapshotWarmupFrames = 0;
        TransDrawable.SkipSnapshotFrame = false;
        if(TransWithChildren) {
            IncCacheEnabledCount();
            if(transsource)
                transsource->IncCacheEnabledCount();
        }

        // set to interrupt into updating/completion pipe line
        TransSrc = transsource;
        if(transsource)
            transsource->TransDest = this;

        // set transition handler
        if(TransUpdateType == tutDivisibleFade ||
           TransUpdateType == tutDivisible)
            DivisibleTransHandler =
                static_cast<iTVPDivisibleTransHandler *>(handler);
        if(TransUpdateType == tutGiveUpdate)
            GiveUpdateTransHandler =
                static_cast<iTVPGiveUpdateTransHandler *>(handler);

        // hold destination and source objects
        TransDestObj = Owner;
        if(TransDestObj)
            TransDestObj->AddRef();
        if(transsource)
            TransSrcObj = transsource->Owner;
        else
            TransSrcObj = nullptr;
        if(TransSrcObj)
            TransSrcObj->AddRef();

        // register to idle event handler
        TransIdleCallback.Owner = this;
        if(!TransSelfUpdate)
            TVPAddContinuousEventHook(&TransIdleCallback);

        // initial tick count
        TVPStartTickCount();
        if(UseTransTickCallback) {
            TransTick = 0;
            // initially 0
            // dummy calling StartProcess/EndProcess to notify initial
            // tick count; for first call with TransTick = 0, these
            // method should not return any error status here.
            if(DivisibleTransHandler) {
                DivisibleTransHandler->StartProcess(TransTick);
                DivisibleTransHandler->EndProcess();
            } else if(GiveUpdateTransHandler) {
                ; // not yet implemented
            }
        } else {
            TransTick = GetTransTick();
        }

        // set flag
        InTransition = true;
        TransCompEventPrevented = false;

        TVPTraceLayerTransition("start", this, TransSrc);
        spdlog::trace("[TransTrace] StartTransition name={} type={} updateType={} withChildren={} hasSrc={}",
            name.AsNarrowStdString(), (int)TransType, (int)TransUpdateType,
            withchildren, transsource != nullptr);

        // update
        Update(true);
    } catch(...) {
        if(pro)
            pro->Release();
        if(sop)
            sop->Release();
        if(handler)
            handler->Release();
        throw;
    }
    if(pro)
        pro->Release();
    if(sop)
        sop->Release();
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::InternalStopTransition() {
    // stop transition
    if(InTransition) {
        TVPTraceLayerTransition("internal-stop-enter", this, TransSrc);
        spdlog::trace("[TransTrace] StopTransition type={}", (int)TransType);
        InTransition = false;
        TransCompEventPrevented = false;
        delete TransDrawable.Src1Bmp;
        delete TransDrawable.Src2Bmp;
        TransDrawable.Src1Bmp = nullptr;
        TransDrawable.Src2Bmp = nullptr;
        TransDrawable.SnapshotWarmupFrames = 0;
        TransDrawable.SkipSnapshotFrame = false;

        // unregister idle event handler
        if(!TransSelfUpdate)
            TVPRemoveContinuousEventHook(&TransIdleCallback);

        // disable cache
        if(TransWithChildren) {
            DecCacheEnabledCount();
            if(TransSrc)
                TransSrc->DecCacheEnabledCount();
        }

        //
        // exchange the layer
        if(TransType == ttExchange) {
            tjs_int tl = this->Rect.left;
            tjs_int tt = this->Rect.top;
            tjs_int sl = TransSrc->Rect.left;
            tjs_int st = TransSrc->Rect.top;
            bool tv = this->GetVisible();
            bool sv = TransSrc->GetVisible();
            if(TransWithChildren) {
                // exchange with tree structure
                Exchange(TransSrc);
            } else {
                // exchange the layer and the target only
                Swap(TransSrc);
            }
            this->SetPosition(sl, st);
            TransSrc->SetPosition(tl, tt);
            this->SetVisible(sv);
            TransSrc->SetVisible(tv);

            // Exchange swaps the layer tree, not the compositor cache.  A
            // divisible transition has just populated CacheBitmap with the
            // pre-exchange tree; leaving CacheRecalcRegion empty therefore
            // presents that stale (often transparent) snapshot as the new
            // page.  Mark both exchanged roots dirty before releasing the
            // transition source so the next completion rebuilds their full
            // composition with the new children and visibility.
            auto invalidate_exchanged_root = [](tTJSNI_BaseLayer *root) {
                if(!root || root->Shutdown)
                    return;
                tTVPRect full(0, 0, root->Rect.get_width(),
                              root->Rect.get_height());
                root->CacheRecalcRegion.Or(full);
                root->Update(false);
            };
            tTJSNI_BaseLayer *exchanged_source = TransSrc;
            if(exchanged_source)
                exchanged_source->TransDest = nullptr;
            invalidate_exchanged_root(this);
            invalidate_exchanged_root(exchanged_source);
        }

        bool transsrcalive = false;
        if(TransSrc && !TransSrc->Shutdown)
            transsrcalive = true;
        if(TVPLayerTransitionTraceEnabled()) {
            spdlog::info(
                "LayerTrans internal-stop-alive layer={} src={} srcNative={} "
                "srcShutdown={} srcAlive={} owner={} transDestObj={} transSrcObj={}",
                GetName().AsStdString(),
                TransSrc ? TransSrc->GetName().AsStdString() : "<null>",
                static_cast<void *>(TransSrc),
                TransSrc && TransSrc->Shutdown ? "yes" : "no",
                transsrcalive ? "yes" : "no",
                static_cast<void *>(Owner),
                static_cast<void *>(TransDestObj),
                static_cast<void *>(TransSrcObj));
        }

        if(TransSrc)
            TransSrc->TransDest = nullptr;
        TransSrc = nullptr;

        // release transition handler object
        if(DivisibleTransHandler)
            DivisibleTransHandler->Release(), DivisibleTransHandler = nullptr;
        if(GiveUpdateTransHandler)
            GiveUpdateTransHandler->Release(), GiveUpdateTransHandler = nullptr;

        // fire event
        if(Owner && !Shutdown && transsrcalive) {
            TVPTraceLayerTransition("post-transition-completed", this, nullptr);
            static ttstr eventname(TJS_W("onTransitionCompleted"));

            // fire SYNCHRONOUS event of "onTransitionCompleted"
            tTJSVariant param[2];
            param[0] = tTJSVariant(TransDestObj, TransDestObj);
            if(TransDestObj)
                TransDestObj->Release(), TransDestObj = nullptr;
            param[1] = tTJSVariant(TransSrcObj, TransSrcObj);
            if(TransSrcObj)
                TransSrcObj->Release(), TransSrcObj = nullptr;

            TVPPostEvent(Owner, Owner, eventname, 0, TVP_EPT_IMMEDIATE, 2,
                         param);
        }

        // release destination and source objects
        if(TransDestObj)
            TransDestObj->Release(), TransDestObj = nullptr;
        if(TransSrcObj)
            TransSrcObj->Release(), TransSrcObj = nullptr;

        // release TransTickCallback
        TransTickCallback.Release(),
            TransTickCallback = tTJSVariantClosure(nullptr, nullptr);
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::StopTransition() {
    // stop the transition by manual
    InternalStopTransition();
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::StopTransitionByHandler() {
    // stopping of the transition caused by the handler
    TVPTraceLayerTransition("stop-by-handler", this, TransSrc);
    if(!TVPEventDisabled) {
        // event dispatching is enabled
        InternalStopTransition();
    } else {
        // event dispatching is not enabled
        TransCompEventPrevented = true;
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::InvokeTransition(tjs_uint64 tick) {
    if(!TransCompEventPrevented) {
        if(UseTransTickCallback)
            TransTick = GetTransTick();
        else
            TransTick = tick;
        if(!GetNodeVisible()) {
            StopTransitionByHandler();
        } else {
            if(!MainImage && TransWithChildren &&
               TransUpdateType == tutDivisibleFade && DisplayType != ltOpaque) {
                // update only for child region
                UpdateAllChildren(true);
                if(TransSrc)
                    TransSrc->UpdateAllChildren(true);
            } else {
                Update(true); // update without re-computing piled images
            }
        }
    } else {
        // transition complete event is prevented
        if(!TVPEventDisabled) // if event dispatching is enabled
            InternalStopTransition(); // stop the transition
    }
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::DoDivisibleTransition(iTVPBaseBitmap *dest, tjs_int dx,
                                             tjs_int dy,
                                             const tTVPRect &srcrect) {
    // apply transition ( with no children ) over given target bitmap
    if(!InTransition || !DivisibleTransHandler)
        return;

    tTVPDivisibleData data;
    data.Left = srcrect.left;
    data.Top = srcrect.top;
    data.Width = srcrect.get_width();
    data.Height = srcrect.get_height();

    // src1
    if(!SrcSLP)
        SrcSLP = new tTVPScanLineProviderForBaseBitmap(MainImage);
    else
        SrcSLP->Attach(MainImage);
    data.Src1 = SrcSLP;
    data.Src1Left = srcrect.left;
    data.Src1Top = srcrect.top;
    ImageModified = true;

    // src2
    if(TransSrc) {
        // source available
        if(!TransSrc->SrcSLP)
            TransSrc->SrcSLP =
                new tTVPScanLineProviderForBaseBitmap(TransSrc->MainImage);
        else
            TransSrc->SrcSLP->Attach(TransSrc->MainImage);

        data.Src2 = TransSrc->SrcSLP;
        data.Src2Left = srcrect.left;
        data.Src2Top = srcrect.top;
    }

    // dest
    if(!DestSLP)
        DestSLP = new tTVPScanLineProviderForBaseBitmap(dest);
    else
        DestSLP->Attach(dest);
    data.Dest = DestSLP;
    data.DestLeft = dx;
    data.DestTop = dy;

    // process
    DivisibleTransHandler->Process(&data);

    if(data.Dest == data.Src1) {
        // returned destination differs from given destination
        // (returned destination is data.Src1)
        dest->CopyRect(dx, dy, MainImage, srcrect);
    } else if(data.Dest == data.Src2) {
        // (returned destination is data.Src2)
        dest->CopyRect(dx, dy, TransSrc->MainImage, srcrect);
    }
}

//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
tTVPBaseTexture *
tTJSNI_BaseLayer::tTransDrawable::GetDrawTargetBitmap(const tTVPRect &rect,
                                                      tTVPRect &cliprect) {
    // save target bitmap pointer
    Target = OrgDrawable->GetDrawTargetBitmap(rect, cliprect);
    TargetRect = cliprect;
    return Target;
}

//---------------------------------------------------------------------------
tTVPLayerType tTJSNI_BaseLayer::tTransDrawable::GetTargetLayerType() {
    return OrgDrawable->GetTargetLayerType();
}

//---------------------------------------------------------------------------
void tTJSNI_BaseLayer::tTransDrawable::DrawCompleted(const tTVPRect &destrect,
                                                     tTVPBaseTexture *bmp,
                                                     const tTVPRect &cliprect,
                                                     tTVPLayerType type,
                                                     tjs_int opacity) {
    // do divisible transition
    if(!Owner->InTransition || !Owner->DivisibleTransHandler)
        return;
    tTVPDivisibleData data;
    data.Left = destrect.left - Owner->Rect.left;
    data.Top = destrect.top - Owner->Rect.top;
    data.Width = cliprect.get_width();
    data.Height = cliprect.get_height();

    tTVPBaseTexture *src1bmp;
    const bool use_cached_transition_frames =
        Owner->TransUpdateType == tutDivisible ||
        Owner->TransUpdateType == tutDivisibleFade;
    if(use_cached_transition_frames)
        src1bmp = Src1Bmp;
    else
        src1bmp = bmp;
    Owner->ImageModified = true;

    if(!Owner->SrcSLP)
        Owner->SrcSLP = new tTVPScanLineProviderForBaseBitmap(src1bmp);
    else
        Owner->SrcSLP->Attach(src1bmp);

    data.Src1 = Owner->SrcSLP;
    data.Src1Left = cliprect.left;
    data.Src1Top = cliprect.top;

    tTVPBaseTexture *src = nullptr;
    if(Owner->TransSrc) {
        // source available
        // prepare source 2 from CacheBitmap
        if(use_cached_transition_frames)
            src = Src2Bmp;
        else
            src = Owner->TransSrc->Complete(destrect);
        if(!Owner->TransSrc->SrcSLP)
            Owner->TransSrc->SrcSLP =
                new tTVPScanLineProviderForBaseBitmap(src);
        else
            Owner->TransSrc->SrcSLP->Attach(src);

        data.Src2 = Owner->TransSrc->SrcSLP;
        data.Src2Left = data.Left; // destrect.left;
        data.Src2Top = data.Top; // destrect.top;
    } else {
        data.Src2 = nullptr;
    }
    tTVPBaseTexture *dest;
    bool tempalloc = false;
    if(bmp == Target || Target == nullptr) {
        // source bitmap is the same as the Original Target;
        // allocatte temporary bitmap
        dest = tTVPTempBitmapHolder::GetTemp(cliprect.get_width(),
                                             cliprect.get_height(),
                                             true); // fit = true

        // TODO: check whether "fit" can affect the performance

        tempalloc = true;
        if(!Owner->DestSLP)
            Owner->DestSLP = new tTVPScanLineProviderForBaseBitmap(dest);
        else
            Owner->DestSLP->Attach(dest);
        data.Dest = Owner->DestSLP;
        data.DestLeft = 0;
        data.DestTop = 0;
    } else {
        if(!Owner->DestSLP)
            Owner->DestSLP = new tTVPScanLineProviderForBaseBitmap(Target);
        else
            Owner->DestSLP->Attach(Target);
        dest = Target;
        data.Dest = Owner->DestSLP;
        data.DestLeft = TargetRect.left;
        data.DestTop = TargetRect.top;
    }

    try {
        Owner->DivisibleTransHandler->Process(&data);
        tTVPRect cr = cliprect;

        if(data.Dest == Owner->DestSLP) {
            cr.set_offsets(data.DestLeft, data.DestTop);
            // Legacy opaque transition methods only define their RGB output;
            // classic presenters ignored the alpha byte. Alpha-aware child
            // composition makes that byte observable, so restore the
            // ltOpaque contract before handing the frame upstream.
            if(Owner->DisplayType == ltOpaque)
                dest->FillMask(cr, 255);
            OrgDrawable->DrawCompleted(destrect, dest, cr, type, opacity);
        } else if(data.Dest == data.Src1) {
            cr.set_offsets(data.DestLeft, data.DestTop);
            if(Owner->DisplayType == ltOpaque)
                src1bmp->FillMask(cr, 255);
            OrgDrawable->DrawCompleted(destrect, src1bmp, cr, type, opacity);
        } else if(data.Dest == data.Src2 && Owner->TransSrc) {
            cr.set_offsets(data.DestLeft, data.DestTop);
            if(Owner->DisplayType == ltOpaque)
                src->FillMask(cr, 255);
            OrgDrawable->DrawCompleted(destrect, src, cr, type, opacity);
        }
    } catch(...) {
        if(tempalloc)
            tTVPTempBitmapHolder::FreeTemp();
        throw;
    }

    if(tempalloc)
        tTVPTempBitmapHolder::FreeTemp();
}

//---------------------------------------------------------------------------
tjs_uint64 tTJSNI_BaseLayer::GetTransTick() {
    if(!UseTransTickCallback) {
        // just use TVPGetTickCount() as a source
        return TVPGetTickCount();
    } else {
        // call TransTickCallback to receive result
        tTJSVariant res;
        TransTickCallback.FuncCall(0, nullptr, nullptr, &res, 0, nullptr,
                                   nullptr);
        return (tjs_uint64)(tjs_int64)res;
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNC_Layer : TJS Layer class
//---------------------------------------------------------------------------
tjs_uint32 tTJSNC_Layer::ClassID = -1;

tTJSNC_Layer::tTJSNC_Layer() : tTJSNativeClass(TJS_W("Layer")) {
    // registration of native members

    TJS_BEGIN_NATIVE_MEMBERS(Layer) // constructor
    TJS_DECL_EMPTY_FINALIZE_METHOD
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL(/*var.name*/ _this,
                                      /*var.type*/ tTJSNI_Layer,
                                      /*TJS class name*/ Layer) {
        return TJS_S_OK;
    }
    TJS_END_NATIVE_CONSTRUCTOR_DECL(/*TJS class name*/ Layer)
    //----------------------------------------------------------------------

    //-- methods

    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ fetchImageSize) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        tjs_int width = 0;
        tjs_int height = 0;
        TVPGetImageSize(ttstr(*param[0]), width, height);
        if(const char *trace = std::getenv("AETHERKIRI_VIRTUAL_SIZE_TRACE");
           trace && *trace && *trace != '0') {
            spdlog::info("VirtualSizeTrace Layer.fetchImageSize name={} -> {}x{}",
                         ttstr(*param[0]).AsStdString(), width, height);
        }

        if(result) {
            iTJSDispatch2 *array = TJSCreateArrayObject();
            if(!array)
                return TJS_E_FAIL;
            try {
                tTJSVariant value(width);
                array->PropSetByNum(TJS_MEMBERENSURE, 0, &value, array);
                value = height;
                array->PropSetByNum(TJS_MEMBERENSURE, 1, &value, array);
                *result = tTJSVariant(array, array);
            } catch(...) {
                array->Release();
                throw;
            }
            array->Release();
        }
        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ fetchImageSize)
    //----------------------------------------------------------------------

    //----------------------------------------------------------------------
    // 在合适的位置，例如其他 Layer 方法绑定之后
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(
        /*func. name*/ light) // TJS中调用的方法名是 "light"
    {
        // 获取调用该方法的 Layer 对象的 C++ 实例
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        // 检查参数数量
        // light 方法期望两个参数：brightness 和 contrast
        if(numparams < 2)
            return TJS_E_BADPARAMCOUNT; // 参数数量不足

        // 从 TJSVariant 类型的参数中获取值
        // param[0] 是第一个参数 (brightness)
        // param[1] 是第二个参数 (contrast)
        // 你需要将它们转换为 C++ 中合适的类型，例如 tjs_int 或 float

        tjs_int brightness = 0; // 默认值
        tjs_int contrast = 0; // 默认值

        // 从 tTJSVariant 转换为 tjs_int (或其他适当类型)
        // 需要进行类型检查和转换
        if(param[0]->Type() != tvtVoid) { // 确保参数不是 void (即提供了参数)
            brightness =
                param[0]->AsInteger(); // 或者 param[0]->AsReal() 如果是浮点数
        }
        if(param[1]->Type() != tvtVoid) {
            contrast = param[1]->AsInteger(); // 或者 param[1]->AsReal()
        }
        _this->ApplyLightContrast(brightness, contrast);
        // 需要实现内部逻辑
        if(result)
            result->Clear(); // 清除返回值，表示void

        return TJS_S_OK; // 表示成功
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ light)
    //----------------------------------------------------------------------


    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ moveBefore) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        tTJSNI_BaseLayer *src = nullptr;
        tTJSVariantClosure clo = param[0]->AsObjectClosureNoAddRef();
        if(clo.Object) {
            if(TJS_FAILED(clo.Object->NativeInstanceSupport(
                   TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                   (iTJSNativeInstance **)&src)))
                TVPThrowExceptionMessage(TVPSpecifyLayer);
        }
        if(!src)
            TVPThrowExceptionMessage(TVPSpecifyLayer);

        _this->MoveBefore(src);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ moveBefore)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ moveBehind) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        tTJSNI_BaseLayer *src = nullptr;
        tTJSVariantClosure clo = param[0]->AsObjectClosureNoAddRef();
        if(clo.Object) {
            if(TJS_FAILED(clo.Object->NativeInstanceSupport(
                   TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                   (iTJSNativeInstance **)&src)))
                TVPThrowExceptionMessage(TVPSpecifyLayer);
        }
        if(!src)
            TVPThrowExceptionMessage(TVPSpecifyLayer);

        _this->MoveBehind(src);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ moveBehind)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ bringToBack) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        _this->BringToBack();

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ bringToBack)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ bringToFront) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        _this->BringToFront();

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ bringToFront)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ saveLayerImage) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        ttstr name(*param[0]);
        ttstr type(TJS_W("bmp"));
        if(numparams >= 2 && param[1]->Type() != tvtVoid)
            type = *param[1];
        _this->SaveLayerImage(name, type);
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ saveLayerImage)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ loadImages) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        ttstr name(*param[0]);
        tjs_uint32 key = clNone; // TODO Intfなのに固有値が
        if(numparams >= 2 && param[1]->Type() != tvtVoid)
            key = (tjs_uint32)param[1]->AsInteger();
        if(TVPLayerInputTraceEnabled() && numparams >= 3 &&
           param[2]->Type() == tvtObject) {
            spdlog::info("Layer.loadImages options name={} layer={}",
                         name.AsStdString(), _this->GetName().AsStdString());
            TVPTraceObjectMembers(
                "loadImages.options",
                param[2]->AsObjectClosureNoAddRef(), 120);
        }
        iTJSDispatch2 *metainfo = nullptr;
        if(numparams >= 3 && param[2]->Type() == tvtObject) {
            tTJSVariantClosure options = param[2]->AsObjectClosureNoAddRef();
            ttstr pimgStorage;
            ttstr pimgSeton;
            if(TVPLayerGetObjectString(options, TJS_W("storage"), pimgStorage) &&
               TVPLayerGetObjectString(options, TJS_W("seton"), pimgSeton) &&
               TVPLayerLoadPimgComposite(_this, pimgStorage, pimgSeton, key,
                                         &metainfo)) {
                try {
                    if(result)
                        *result = metainfo;
                } catch(...) {
                    if(metainfo)
                        metainfo->Release();
                    throw;
                }
                if(metainfo)
                    metainfo->Release();
                return TJS_S_OK;
            }
        }
        metainfo = _this->LoadImages(name, key);
        try {
            if(result)
                *result = metainfo;
        } catch(...) {
            if(metainfo)
                metainfo->Release();
            throw;
        }
        if(metainfo)
            metainfo->Release();
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ loadImages)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ loadSubImage) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        if(numparams < 3)
            return TJS_E_BADPARAMCOUNT;

        ttstr name(*param[0]);
        tjs_int dx = (tjs_int)*param[1];
        tjs_int dy = (tjs_int)*param[2];
        tjs_uint32 key = clNone;
        if(numparams >= 4 && param[3]->Type() != tvtVoid)
            key = (tjs_uint32)param[3]->AsInteger();

        tTVPBaseTexture source(1, 1);
        ttstr provincename;
        iTJSDispatch2 *metainfo = nullptr;
        TVPLoadGraphic(&source, name, key, 0, 0, glmNormal, &provincename,
                       &metainfo);

        std::unique_ptr<tTVPBaseBitmap> province;
        if(!provincename.IsEmpty()) {
            province = std::make_unique<tTVPBaseBitmap>(source.GetWidth(),
                                                        source.GetHeight(), 8);
            TVPLoadGraphicProvince(province.get(), provincename, 0,
                                   source.GetWidth(), source.GetHeight());
        }

        const tjs_int needed_right = dx + static_cast<tjs_int>(source.GetWidth());
        const tjs_int needed_bottom = dy + static_cast<tjs_int>(source.GetHeight());

        bool allocated_target = false;
        if(!_this->GetMainImage()) {
            if(_this->GetWidth() == 0 || _this->GetHeight() == 0) {
                const tjs_uint target_width = static_cast<tjs_uint>(
                    std::max<tjs_int>(1, needed_right));
                const tjs_uint target_height = static_cast<tjs_uint>(
                    std::max<tjs_int>(1, needed_bottom));
                _this->SetSize(target_width, target_height);
            }
            _this->SetHasImage(true);
            allocated_target = true;
        }

        if(needed_right > static_cast<tjs_int>(_this->GetImageWidth()) ||
           needed_bottom > static_cast<tjs_int>(_this->GetImageHeight())) {
            const tjs_uint target_width = static_cast<tjs_uint>(
                std::max<tjs_int>(needed_right,
                                  static_cast<tjs_int>(_this->GetImageWidth())));
            const tjs_uint target_height = static_cast<tjs_uint>(
                std::max<tjs_int>(needed_bottom,
                                  static_cast<tjs_int>(_this->GetImageHeight())));
            if(target_width == 0 || target_height == 0)
                TVPThrowExceptionMessage(TVPInvalidParam);
            _this->SetImageSize(target_width, target_height);
        }

        if(allocated_target) {
            _this->FillRect(tTVPRect(0, 0, _this->GetImageWidth(),
                                     _this->GetImageHeight()),
                            _this->GetNeutralColor());
        }

        tTVPRect source_rect(0, 0, source.GetWidth(), source.GetHeight());
        _this->CopyRect(dx, dy, &source, province.get(), source_rect);

        if(TVPLayerLoadTraceEnabled() || TVPLayerDebugTake()) {
            spdlog::info(
                "Layer.loadSubImage name={} dest=({}, {}) size={}x{} target={}x{} colorkey=0x{:08x}",
                name.AsStdString(), dx, dy, static_cast<int>(source.GetWidth()),
                static_cast<int>(source.GetHeight()),
                static_cast<int>(_this->GetWidth()),
                static_cast<int>(_this->GetHeight()),
                static_cast<unsigned int>(key));
        }

        try {
            if(result)
                *result = metainfo;
        } catch(...) {
            if(metainfo)
                metainfo->Release();
            throw;
        }
        if(metainfo)
            metainfo->Release();

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ loadSubImage)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ loadProvinceImage) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        ttstr name(*param[0]);
        _this->LoadProvinceImage(name);
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ loadProvinceImage)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ getMainPixel) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        if(numparams < 2)
            return TJS_E_BADPARAMCOUNT;
        if(result)
            *result = (tjs_int64)_this->GetMainPixel(*param[0], *param[1]);
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ getMainPixel)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setMainPixel) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        if(numparams < 3)
            return TJS_E_BADPARAMCOUNT;
        _this->SetMainPixel(*param[0], *param[1], (tjs_int)*param[2]);
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setMainPixel)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ getMaskPixel) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        if(numparams < 2)
            return TJS_E_BADPARAMCOUNT;
        if(result)
            *result = _this->GetMaskPixel(*param[0], *param[1]);
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ getMaskPixel)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setMaskPixel) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        if(numparams < 3)
            return TJS_E_BADPARAMCOUNT;
        _this->SetMaskPixel(*param[0], *param[1], *param[2]);
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setMaskPixel)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ getProvincePixel) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        if(numparams < 2)
            return TJS_E_BADPARAMCOUNT;
        if(result)
            *result = _this->GetProvincePixel(*param[0], *param[1]);
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ getProvincePixel)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setProvincePixel) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        if(numparams < 3)
            return TJS_E_BADPARAMCOUNT;
        _this->SetProvincePixel(*param[0], *param[1], *param[2]);
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setProvincePixel)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(
        /*func. name*/ getLayerAt) // not GetMostFrontChildAt
    {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        if(numparams < 2)
            return TJS_E_BADPARAMCOUNT;

        bool exclude_self = false;
        bool get_disabled = false;
        if(numparams >= 3 && param[2]->Type() != tvtVoid)
            exclude_self = param[2]->operator bool();

        if(numparams >= 4 && param[3]->Type() != tvtVoid)
            get_disabled = param[3]->operator bool();

        tTJSNI_BaseLayer *lay = _this->GetMostFrontChildAt(
            *param[0], *param[1], exclude_self, get_disabled);

        if(result) {
            if(lay && lay->GetOwnerNoAddRef())
                *result = tTJSVariant(lay->GetOwnerNoAddRef(),
                                      lay->GetOwnerNoAddRef());
            else
                *result = tTJSVariant((iTJSDispatch2 *)nullptr);
        }
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ getLayerAt)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(
        /*func. name*/ setPos) // not setPosition
    {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        if(numparams < 2)
            return TJS_E_BADPARAMCOUNT;
        if(numparams == 4 && param[2]->Type() != tvtVoid &&
           param[3]->Type() != tvtVoid) {
            // set bounds
            tTVPRect r;
            r.left = *param[0];
            r.top = *param[1];
            r.right = (tjs_int)*param[2] + r.left;
            r.bottom = (tjs_int)*param[3] + r.top;
            _this->SetBounds(r);
        } else {
            // set position only
            _this->SetPosition(*param[0], *param[1]);
        }
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setPos)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setSize) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        if(numparams < 2)
            return TJS_E_BADPARAMCOUNT;
        _this->SetSize((tjs_int)*param[0], (tjs_int)*param[1]);
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setSize)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setSizeToImageSize) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        _this->SetSize(_this->GetImageWidth(), _this->GetImageHeight());
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setSizeToImageSize)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setImagePos) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        if(numparams < 2)
            return TJS_E_BADPARAMCOUNT;
        _this->SetImagePosition((tjs_int)*param[0], (tjs_int)*param[1]);
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setImagePos)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setImageSize) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        if(numparams < 2)
            return TJS_E_BADPARAMCOUNT;
        _this->SetImageSize((tjs_int)*param[0], (tjs_int)*param[1]);
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setImageSize)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ independMainImage) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        bool copy = true;
        if(numparams >= 1 && param[0]->Type() != tvtVoid)
            copy = param[0]->operator bool();
        _this->IndependMainImage(copy);
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ independMainImage)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(
        /*func. name*/ independProvinceImage) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        bool copy = true;
        if(numparams >= 1 && param[0]->Type() != tvtVoid)
            copy = param[0]->operator bool();
        _this->IndependProvinceImage(copy);
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ independProvinceImage)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(
        /*func. name*/ setClip) // not setClipRect
    {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        if(numparams == 0) {
            // reset clip rectangle
            _this->ResetClip();

        } else {

            if(numparams < 4)
                return TJS_E_BADPARAMCOUNT;

            _this->SetClip((tjs_int)*param[0], (tjs_int)*param[1],
                           (tjs_int)*param[2], (tjs_int)*param[3]);
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setClip)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ fillRect) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        if(numparams < 5)
            return TJS_E_BADPARAMCOUNT;
        tjs_int x = *param[0];
        tjs_int y = *param[1];
        const auto fillColor = static_cast<tjs_uint32>(
            static_cast<tjs_int64>(*param[4]));
        _this->FillRect(
            tTVPRect(x, y, x + (tjs_int)*param[2], y + (tjs_int)*param[3]),
            fillColor);
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ fillRect)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ colorRect) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        if(numparams < 5)
            return TJS_E_BADPARAMCOUNT;
        tjs_int x, y;
        x = *param[0];
        y = *param[1];
        _this->ColorRect(
            tTVPRect(x, y, x + (tjs_int)*param[2], y + (tjs_int)*param[3]),
            static_cast<tjs_uint32>((tjs_int64)*param[4]),
            (numparams >= 6 && param[5]->Type() != tvtVoid) ? (tjs_int)*param[5]
                                                            : 255);
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ colorRect)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ drawText) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        if(numparams < 4)
            return TJS_E_BADPARAMCOUNT;

        const tjs_int x = *param[0];
        tjs_int y = *param[1];
        const tjs_uint32 color =
            static_cast<tjs_uint32>((tjs_int64)*param[3]);
        const tjs_int opacity =
            (numparams >= 5 && param[4]->Type() != tvtVoid) ? (tjs_int)*param[4]
                                                            : (tjs_int)255;
        const bool antialiased =
            (numparams >= 6 && param[5]->Type() != tvtVoid)
                ? param[5]->operator bool()
                : true;
        const tjs_int shadowLevel =
            (numparams >= 7 && param[6]->Type() != tvtVoid) ? (tjs_int)*param[6]
                                                            : 0;
        const tjs_uint32 shadowColor =
            (numparams >= 8 && param[7]->Type() != tvtVoid)
                ? static_cast<tjs_uint32>((tjs_int64)*param[7])
                : 0;
        const tjs_int shadowWidth =
            (numparams >= 9 && param[8]->Type() != tvtVoid) ? (tjs_int)*param[8]
                                                            : 0;
        const tjs_int shadowOffsetX =
            (numparams >= 10 && param[9]->Type() != tvtVoid)
                ? (tjs_int)*param[9]
                : 0;
        const tjs_int shadowOffsetY =
            (numparams >= 11 && param[10]->Type() != tvtVoid)
                ? (tjs_int)*param[10]
                : 0;

        // Some games draw a top-aligned name into a small transparent layer.
        // Replacement fonts can have ink above the logical line box, so keep
        // the glyph and its shadow inside the active clip just like the
        // vertical-gradient text path below does.
        try {
            tTVPRect glyphBounds;
            _this->GetFontGlyphDrawRect(*param[2], glyphBounds);
            y = krkr::font::ClampTextOriginToClipTop(
                y, glyphBounds.top,
                krkr::font::ComputeTextShadowTopPadding(
                    shadowLevel, shadowWidth, shadowOffsetY),
                _this->GetClipTop());
        } catch(...) {
            // Bounds measurement is best-effort. Preserve the original draw
            // behavior when the active font cannot report its glyph bounds.
        }

        _this->DrawText(
            x, y, *param[2], color, opacity, antialiased, shadowLevel,
            shadowColor, shadowWidth, shadowOffsetX, shadowOffsetY);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ drawText)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ drawTextVerticalGradient) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        if(numparams < 5)
            return TJS_E_BADPARAMCOUNT;

        tjs_int y = *param[1];
        tTVPRect glyphBounds;
        _this->GetFontGlyphDrawRect(*param[2], glyphBounds);
        y = krkr::font::ClampTextOriginToClipTop(
            y, glyphBounds.top, 0, _this->GetClipTop());

        _this->DrawTextVerticalGradient(
            *param[0], y, *param[2],
            static_cast<tjs_uint32>((tjs_int64)*param[3]),
            static_cast<tjs_uint32>((tjs_int64)*param[4]),
            (numparams >= 6 && param[5]->Type() != tvtVoid)
                ? (tjs_int)*param[5]
                : (tjs_int)255,
            (numparams >= 7 && param[6]->Type() != tvtVoid)
                ? param[6]->operator bool()
                : true,
            (numparams >= 8 && param[7]->Type() != tvtVoid)
                ? (tjs_int)*param[7]
                : _this->GetTextHeight(*param[2]));

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ drawTextVerticalGradient)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ drawGlyph) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        if(numparams < 4)
            return TJS_E_BADPARAMCOUNT;
        iTJSDispatch2 *glyph = param[2]->AsObjectNoAddRef();
        _this->DrawGlyph(
            *param[0], *param[1], glyph,
            static_cast<tjs_uint32>((tjs_int64)*param[3]),
            (numparams >= 5 && param[4]->Type() != tvtVoid) ? (tjs_int)*param[4]
                                                            : (tjs_int)255,
            (numparams >= 6 && param[5]->Type() != tvtVoid)
                ? param[5]->operator bool()
                : true,
            (numparams >= 7 && param[6]->Type() != tvtVoid) ? (tjs_int)*param[6]
                                                            : 0,
            (numparams >= 8 && param[7]->Type() != tvtVoid)
                ? static_cast<tjs_uint32>((tjs_int64)*param[7])
                : 0,
            (numparams >= 9 && param[8]->Type() != tvtVoid) ? (tjs_int)*param[8]
                                                            : 0,
            (numparams >= 10 && param[9]->Type() != tvtVoid)
                ? (tjs_int)*param[9]
                : 0,
            (numparams >= 11 && param[10]->Type() != tvtVoid)
                ? (tjs_int)*param[10]
                : 0);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ drawGlyph)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ piledCopy) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        if(numparams < 7)
            return TJS_E_BADPARAMCOUNT;

        tTJSNI_BaseLayer *src = nullptr;
        tTJSVariantClosure clo = param[2]->AsObjectClosureNoAddRef();
        if(clo.Object) {
            if(TJS_FAILED(clo.Object->NativeInstanceSupport(
                   TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                   (iTJSNativeInstance **)&src)))
                TVPThrowExceptionMessage(TVPSpecifyLayer);
        }
        if(!src)
            TVPThrowExceptionMessage(TVPSpecifyLayer);

        tTVPRect rect(*param[3], *param[4], *param[5], *param[6]);
        rect.right += rect.left;
        rect.bottom += rect.top;

        _this->PiledCopy(*param[0], *param[1], src, rect);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ piledCopy)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ copyRect) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        if(numparams < 7)
            return TJS_E_BADPARAMCOUNT;

        iTVPBaseBitmap *src = nullptr;
        iTVPBaseBitmap *provinceSrc = nullptr;
        tTJSNI_BaseLayer *sourceLayer = nullptr;
        tTJSVariantClosure clo = param[2]->AsObjectClosureNoAddRef();
        if(clo.Object) {
            if(TJS_FAILED(clo.Object->NativeInstanceSupport(
                   TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                   (iTJSNativeInstance **)&sourceLayer)))
                src = provinceSrc = nullptr;
            else {
                src = sourceLayer->GetMainImage();
                provinceSrc = sourceLayer->GetProvinceImage();
            }

            if(src == nullptr) { // try to get bitmap interface
                tTJSNI_Bitmap *srcbmp = nullptr;
                if(TJS_FAILED(clo.Object->NativeInstanceSupport(
                       TJS_NIS_GETINSTANCE, tTJSNC_Bitmap::ClassID,
                       (iTJSNativeInstance **)&srcbmp)))
                    src = provinceSrc = nullptr;
                else
                    src = provinceSrc = srcbmp->GetBitmap();
            }
        }
        if(!src && !provinceSrc)
            TVPThrowExceptionMessage(TVPSpecifyLayerOrBitmap);

        // Temporary source-name tracing is opt-in and limited to the gallery
        // thumbnail path.  The bitmap-level trace above cannot distinguish a
        // valid thumbnail atlas from the scratch layer that may have been
        // cleared by a failed load, so retain the actual script layer name.
        if(sourceLayer && TVPCopyTraceEnabled()) {
            const char *source_trace = std::getenv("AETHERKIRI_COPY_SOURCE_TRACE");
            const std::string targetName = _this->GetName().AsStdString();
            if(source_trace && *source_trace && *source_trace != '0' &&
               (targetName.find("DecorationButton") != std::string::npos ||
                targetName.find("一時") != std::string::npos)) {
                spdlog::info(
                    "Layer.copyRect.source target={} source={} sourceSize={}x{}",
                    targetName, sourceLayer->GetName().AsStdString(),
                    src ? static_cast<int>(src->GetWidth()) : -1,
                    src ? static_cast<int>(src->GetHeight()) : -1);
            }
        }

        tTVPRect rect(*param[3], *param[4], *param[5], *param[6]);
        rect.right += rect.left;
        rect.bottom += rect.top;

        _this->CopyRect(*param[0], *param[1], src, provinceSrc, rect);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ copyRect)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ pileRect) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        if(numparams < 7)
            return TJS_E_BADPARAMCOUNT;

        tTJSNI_BaseLayer *src = nullptr;
        tTJSVariantClosure clo = param[2]->AsObjectClosureNoAddRef();
        if(clo.Object) {
            if(TJS_FAILED(clo.Object->NativeInstanceSupport(
                   TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                   (iTJSNativeInstance **)&src)))
                TVPThrowExceptionMessage(TVPSpecifyLayer);
        }
        if(!src)
            TVPThrowExceptionMessage(TVPSpecifyLayer);

        tTVPRect rect(*param[3], *param[4], *param[5], *param[6]);
        rect.right += rect.left;
        rect.bottom += rect.top;

        if(numparams >= 9 && param[8]->Type() != tvtVoid) {
            TVPAddLog(TVPFormatMessage(
                TVPHoldDestinationAlphaParameterIsNowDeprecated,
                TJS_W("Layer.pileRect"), TJS_W("9")));
        }

        _this->PileRect(*param[0], *param[1], src, rect,
                        (numparams >= 8 && param[7]->Type() != tvtVoid)
                            ? (tjs_int)*param[7]
                            : 255);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ pileRect)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ blendRect) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        if(numparams < 7)
            return TJS_E_BADPARAMCOUNT;

        tTJSNI_BaseLayer *src = nullptr;
        tTJSVariantClosure clo = param[2]->AsObjectClosureNoAddRef();
        if(clo.Object) {
            if(TJS_FAILED(clo.Object->NativeInstanceSupport(
                   TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                   (iTJSNativeInstance **)&src)))
                TVPThrowExceptionMessage(TVPSpecifyLayer);
        }
        if(!src)
            TVPThrowExceptionMessage(TVPSpecifyLayer);

        tTVPRect rect(*param[3], *param[4], *param[5], *param[6]);
        rect.right += rect.left;
        rect.bottom += rect.top;

        if(numparams >= 9 && param[8]->Type() != tvtVoid) {
            TVPAddLog(TVPFormatMessage(
                TVPHoldDestinationAlphaParameterIsNowDeprecated,
                TJS_W("Layer.blendRect"), TJS_W("9")));
        }

        _this->BlendRect(*param[0], *param[1], src, rect,
                         (numparams >= 8 && param[7]->Type() != tvtVoid)
                             ? (tjs_int)*param[7]
                             : 255);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ blendRect)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ copy9Patch) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        iTVPBaseBitmap *src = nullptr;
        tTJSVariantClosure clo = param[0]->AsObjectClosureNoAddRef();
        if(clo.Object) {
            tTJSNI_BaseLayer *srclayer = nullptr;
            if(TJS_FAILED(clo.Object->NativeInstanceSupport(
                   TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                   (iTJSNativeInstance **)&srclayer)))
                src = nullptr;
            else {
                src = srclayer->GetMainImage();
            }

            if(src == nullptr) { // try to get bitmap interface
                tTJSNI_Bitmap *srcbmp = nullptr;
                if(TJS_FAILED(clo.Object->NativeInstanceSupport(
                       TJS_NIS_GETINSTANCE, tTJSNC_Bitmap::ClassID,
                       (iTJSNativeInstance **)&srcbmp)))
                    src = nullptr;
                else
                    src = srcbmp->GetBitmap();
            }
        }
        if(!src)
            TVPThrowExceptionMessage(TVPSpecifyLayerOrBitmap);

        tTVPRect margin;
        bool updated = _this->Copy9Patch(src, margin);
        if(result) {
            if(updated) {
                iTJSDispatch2 *ret = TVPCreateRectObject(
                    margin.left, margin.top, margin.right, margin.bottom);
                *result = tTJSVariant(ret, ret);
                ret->Release();
            } else {
                result->Clear();
            }
        }
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ copy9Patch)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ operateRect) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        if(numparams < 7)
            return TJS_E_BADPARAMCOUNT;

        iTVPBaseBitmap *src = nullptr;
        tTJSVariantClosure clo = param[2]->AsObjectClosureNoAddRef();
        tTVPBlendOperationMode automode = omAlpha;
        if(clo.Object) {
            tTJSNI_BaseLayer *srclayer = nullptr;
            if(TJS_FAILED(clo.Object->NativeInstanceSupport(
                   TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                   (iTJSNativeInstance **)&srclayer)))
                src = nullptr;
            else
                src = srclayer->GetMainImage(),
                automode = srclayer->GetOperationModeFromType();

            if(src == nullptr) { // try to get bitmap interface
                tTJSNI_Bitmap *srcbmp = nullptr;
                if(TJS_FAILED(clo.Object->NativeInstanceSupport(
                       TJS_NIS_GETINSTANCE, tTJSNC_Bitmap::ClassID,
                       (iTJSNativeInstance **)&srcbmp)))
                    src = nullptr;
                else
                    src = srcbmp->GetBitmap();
            }
        }
        if(!src)
            TVPThrowExceptionMessage(TVPSpecifyLayerOrBitmap);

        tTVPRect rect(*param[3], *param[4], *param[5], *param[6]);
        rect.right += rect.left;
        rect.bottom += rect.top;

        tTVPBlendOperationMode mode;
        if(numparams >= 8 && param[7]->Type() != tvtVoid)
            mode = (tTVPBlendOperationMode)(tjs_int)(*param[7]);
        else
            mode = omAuto;

        if(numparams >= 10 && param[9]->Type() != tvtVoid) {
            TVPAddLog(TVPFormatMessage(
                TVPHoldDestinationAlphaParameterIsNowDeprecated,
                TJS_W("Layer.operateRect"), TJS_W("10")));
        }

        // get correct blend mode if the mode is omAuto
        if(mode == omAuto)
            mode = automode;

        _this->OperateRect(*param[0], *param[1], src, rect, mode,
                           (numparams >= 9 && param[8]->Type() != tvtVoid)
                               ? (tjs_int)*param[8]
                               : 255);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ operateRect)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ stretchCopy) {
        // dx, dy, dw, dh, src, sx, sy, sw, sh, type=0
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        if(numparams < 9)
            return TJS_E_BADPARAMCOUNT;

        iTVPBaseBitmap *src = nullptr;
        tTJSVariantClosure clo = param[4]->AsObjectClosureNoAddRef();
        if(clo.Object) {
            tTJSNI_BaseLayer *srclayer = nullptr;
            if(TJS_FAILED(clo.Object->NativeInstanceSupport(
                   TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                   (iTJSNativeInstance **)&srclayer)))
                src = nullptr;
            else
                src = srclayer->GetMainImage();

            if(src == nullptr) { // try to get bitmap interface
                tTJSNI_Bitmap *srcbmp = nullptr;
                if(TJS_FAILED(clo.Object->NativeInstanceSupport(
                       TJS_NIS_GETINSTANCE, tTJSNC_Bitmap::ClassID,
                       (iTJSNativeInstance **)&srcbmp)))
                    src = nullptr;
                else
                    src = srcbmp->GetBitmap();
            }
        }
        if(!src)
            TVPThrowExceptionMessage(TVPSpecifyLayerOrBitmap);

        tTVPRect destrect(*param[0], *param[1], *param[2], *param[3]);
        destrect.right += destrect.left;
        destrect.bottom += destrect.top;

        tTVPRect srcrect(*param[5], *param[6], *param[7], *param[8]);
        srcrect.right += srcrect.left;
        srcrect.bottom += srcrect.top;

        tTVPBBStretchType type = stNearest;
        if(numparams >= 10)
            type = (tTVPBBStretchType)(tjs_int)*param[9];

        tjs_real typeopt = 0.0;
        if(numparams >= 11)
            typeopt = (tjs_real)*param[10];
        else if(type == stFastCubic || type == stCubic)
            typeopt = -1.0;

        _this->StretchCopy(destrect, src, srcrect, type, typeopt);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ stretchCopy)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ stretchPile) {
        // dx, dy, dw, dh, src, sx, sy, sw, sh, opa=255, type=0
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        if(numparams < 9)
            return TJS_E_BADPARAMCOUNT;

        tTJSNI_BaseLayer *src = nullptr;
        tTJSVariantClosure clo = param[4]->AsObjectClosureNoAddRef();
        if(clo.Object) {
            if(TJS_FAILED(clo.Object->NativeInstanceSupport(
                   TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                   (iTJSNativeInstance **)&src)))
                TVPThrowExceptionMessage(TVPSpecifyLayer);
        }
        if(!src)
            TVPThrowExceptionMessage(TVPSpecifyLayer);

        tTVPRect destrect(*param[0], *param[1], *param[2], *param[3]);
        destrect.right += destrect.left;
        destrect.bottom += destrect.top;

        tTVPRect srcrect(*param[5], *param[6], *param[7], *param[8]);
        srcrect.right += srcrect.left;
        srcrect.bottom += srcrect.top;

        tjs_int opa = 255;

        if(numparams >= 10 && param[9]->Type() != tvtVoid)
            opa = *param[9];

        tTVPBBStretchType type = stNearest;
        if(numparams >= 11 && param[10]->Type() != tvtVoid)
            type = (tTVPBBStretchType)(tjs_int)*param[10];

        if(numparams >= 12 && param[11]->Type() != tvtVoid) {
            TVPAddLog(TVPFormatMessage(
                TVPHoldDestinationAlphaParameterIsNowDeprecated,
                TJS_W("Layer.stretchPile"), TJS_W("12")));
        }

        _this->StretchPile(destrect, src, srcrect, opa, type);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ stretchPile)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ stretchBlend) {
        // dx, dy, dw, dh, src, sx, sy, sw, sh, opa=255, type=0
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        if(numparams < 9)
            return TJS_E_BADPARAMCOUNT;

        tTJSNI_BaseLayer *src = nullptr;
        tTJSVariantClosure clo = param[4]->AsObjectClosureNoAddRef();
        if(clo.Object) {
            if(TJS_FAILED(clo.Object->NativeInstanceSupport(
                   TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                   (iTJSNativeInstance **)&src)))
                TVPThrowExceptionMessage(TVPSpecifyLayer);
        }
        if(!src)
            TVPThrowExceptionMessage(TVPSpecifyLayer);

        tTVPRect destrect(*param[0], *param[1], *param[2], *param[3]);
        destrect.right += destrect.left;
        destrect.bottom += destrect.top;

        tTVPRect srcrect(*param[5], *param[6], *param[7], *param[8]);
        srcrect.right += srcrect.left;
        srcrect.bottom += srcrect.top;

        tjs_int opa = 255;

        if(numparams >= 10 && param[9]->Type() != tvtVoid)
            opa = *param[9];

        tTVPBBStretchType type = stNearest;
        if(numparams >= 11 && param[10]->Type() != tvtVoid)
            type = (tTVPBBStretchType)(tjs_int)*param[10];
        if(numparams >= 12 && param[11]->Type() != tvtVoid) {
            static bool IsWarned = false;
            if(!IsWarned) {
                IsWarned = true;
                TVPAddLog(TVPFormatMessage(
                    TVPHoldDestinationAlphaParameterIsNowDeprecated,
                    TJS_W("Layer.stretchBlend"), TJS_W("12")));
            }
        }

        _this->StretchBlend(destrect, src, srcrect, opa, type);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ stretchBlend)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ operateStretch) {
        // dx, dy, dw, dh, src, sx, sy, sw, sh, mode=omAuto, opa=255,
        // type=0
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        if(numparams < 9)
            return TJS_E_BADPARAMCOUNT;

        iTVPBaseBitmap *src = nullptr;
        tTJSVariantClosure clo = param[4]->AsObjectClosureNoAddRef();
        tTVPBlendOperationMode automode = omAlpha;
        if(clo.Object) {
            tTJSNI_BaseLayer *srclayer = nullptr;
            if(TJS_FAILED(clo.Object->NativeInstanceSupport(
                   TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                   (iTJSNativeInstance **)&srclayer)))
                src = nullptr;
            else
                src = srclayer->GetMainImage(),
                automode = srclayer->GetOperationModeFromType();

            if(src == nullptr) { // try to get bitmap interface
                tTJSNI_Bitmap *srcbmp = nullptr;
                if(TJS_FAILED(clo.Object->NativeInstanceSupport(
                       TJS_NIS_GETINSTANCE, tTJSNC_Bitmap::ClassID,
                       (iTJSNativeInstance **)&srcbmp)))
                    src = nullptr;
                else
                    src = srcbmp->GetBitmap();
            }
        }
        if(!src)
            TVPThrowExceptionMessage(TVPSpecifyLayerOrBitmap);

        tTVPRect destrect(*param[0], *param[1], *param[2], *param[3]);
        destrect.right += destrect.left;
        destrect.bottom += destrect.top;

        tTVPRect srcrect(*param[5], *param[6], *param[7], *param[8]);
        srcrect.right += srcrect.left;
        srcrect.bottom += srcrect.top;

        tTVPBlendOperationMode mode;
        if(numparams >= 10 && param[9]->Type() != tvtVoid)
            mode = (tTVPBlendOperationMode)(tjs_int)(*param[9]);
        else
            mode = omAuto;

        tjs_int opa = 255;

        if(numparams >= 11 && param[10]->Type() != tvtVoid)
            opa = *param[10];

        tTVPBBStretchType type = stNearest;
        if(numparams >= 12 && param[11]->Type() != tvtVoid)
            type = (tTVPBBStretchType)(tjs_int)*param[11];

        tjs_real typeopt = 0.0;
        if(numparams >= 13)
            typeopt = (tjs_real)*param[12];
        else if(type == stFastCubic || type == stCubic)
            typeopt = -1.0;

        // get correct blend mode if the mode is omAuto
        if(mode == omAuto)
            mode = automode;

        _this->OperateStretch(destrect, src, srcrect, mode, opa, type, typeopt);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ operateStretch)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ affineCopy) {
        // src, sx, sy, sw, sh, affine, x0/a, y0/b, x1/c, y1/d, x2/tx,
        // y2/ty, type=0
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        if(numparams < 12)
            return TJS_E_BADPARAMCOUNT;

        iTVPBaseBitmap *src = nullptr;
        tTJSVariantClosure clo = param[0]->AsObjectClosureNoAddRef();
        if(clo.Object) {
            tTJSNI_BaseLayer *srclayer = nullptr;
            if(TJS_FAILED(clo.Object->NativeInstanceSupport(
                   TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                   (iTJSNativeInstance **)&srclayer)))
                src = nullptr;
            else
                src = srclayer->GetMainImage();

            if(src == nullptr) { // try to get bitmap interface
                tTJSNI_Bitmap *srcbmp = nullptr;
                if(TJS_FAILED(clo.Object->NativeInstanceSupport(
                       TJS_NIS_GETINSTANCE, tTJSNC_Bitmap::ClassID,
                       (iTJSNativeInstance **)&srcbmp)))
                    src = nullptr;
                else
                    src = srcbmp->GetBitmap();
            }
        }
        if(!src)
            TVPThrowExceptionMessage(TVPSpecifyLayerOrBitmap);

        tTVPRect srcrect(*param[1], *param[2], *param[3], *param[4]);
        srcrect.right += srcrect.left;
        srcrect.bottom += srcrect.top;

        tTVPBBStretchType type = stNearest;

        if(numparams >= 13 && param[12]->Type() != tvtVoid)
            type = (tTVPBBStretchType)(tjs_int)*param[12];

        bool clear = false;

        if(numparams >= 14 && param[13]->Type() != tvtVoid)
            clear = 0 != (tjs_int)*param[13];

        if(param[5]->operator bool()) {
            // affine matrix mode
            t2DAffineMatrix mat;
            mat.a = *param[6];
            mat.b = *param[7];
            mat.c = *param[8];
            mat.d = *param[9];
            mat.tx = *param[10];
            mat.ty = *param[11];
            if(TVPAffineTraceEnabled()) {
                const std::string targetName = _this->GetName().AsStdString();
                spdlog::info(
                    "Layer.affineCopy call target={} targetPtr={} srcPtr={} "
                    "srcSize={}x{} rect=({},{} {}x{}) mat=[{},{},{},{},{},{}] "
                    "type={} clear={}",
                    targetName, static_cast<const void *>(_this),
                    static_cast<const void *>(src), src->GetWidth(),
                    src->GetHeight(), srcrect.left, srcrect.top,
                    srcrect.get_width(), srcrect.get_height(), mat.a, mat.b,
                    mat.c, mat.d, mat.tx, mat.ty, static_cast<int>(type),
                    clear ? 1 : 0);
            }
            _this->AffineCopy(mat, src, srcrect, type, clear);
        } else {
            // points mode
            tTVPPointD points[3];
            points[0].x = *param[6];
            points[0].y = *param[7];
            points[1].x = *param[8];
            points[1].y = *param[9];
            points[2].x = *param[10];
            points[2].y = *param[11];
            if(TVPAffineTraceEnabled()) {
                const std::string targetName = _this->GetName().AsStdString();
                spdlog::info(
                    "Layer.affineCopy call target={} targetPtr={} srcPtr={} "
                    "srcSize={}x{} rect=({},{} {}x{}) points=[{},{},{},{},{},{}] "
                    "type={} clear={}",
                    targetName, static_cast<const void *>(_this),
                    static_cast<const void *>(src), src->GetWidth(),
                    src->GetHeight(), srcrect.left, srcrect.top,
                    srcrect.get_width(), srcrect.get_height(), points[0].x,
                    points[0].y, points[1].x, points[1].y, points[2].x,
                    points[2].y, static_cast<int>(type), clear ? 1 : 0);
            }
            _this->AffineCopy(points, src, srcrect, type, clear);
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ affineCopy)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ affinePile) {
        // src, sx, sy, sw, sh, affine, x0/a, y0/b, x1/c, y1/d, x2/tx,
        // y2/ty, opa=255, type=0
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        if(numparams < 12)
            return TJS_E_BADPARAMCOUNT;

        tTJSNI_BaseLayer *src = nullptr;
        tTJSVariantClosure clo = param[0]->AsObjectClosureNoAddRef();
        if(clo.Object) {
            if(TJS_FAILED(clo.Object->NativeInstanceSupport(
                   TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                   (iTJSNativeInstance **)&src)))
                TVPThrowExceptionMessage(TVPSpecifyLayer);
        }
        if(!src)
            TVPThrowExceptionMessage(TVPSpecifyLayer);

        tTVPRect srcrect(*param[1], *param[2], *param[3], *param[4]);
        srcrect.right += srcrect.left;
        srcrect.bottom += srcrect.top;

        tjs_int opa = 255;
        tTVPBBStretchType type = stNearest;

        if(numparams >= 13 && param[12]->Type() != tvtVoid)
            opa = (tjs_int)*param[12];
        if(numparams >= 14 && param[13]->Type() != tvtVoid)
            type = (tTVPBBStretchType)(tjs_int)*param[13];
        if(numparams >= 15 && param[14]->Type() != tvtVoid) {
            TVPAddLog(TVPFormatMessage(
                TVPHoldDestinationAlphaParameterIsNowDeprecated,
                TJS_W("Layer.affinePile"), TJS_W("15")));
        }

        if(param[5]->operator bool()) {
            // affine matrix mode
            t2DAffineMatrix mat;
            mat.a = *param[6];
            mat.b = *param[7];
            mat.c = *param[8];
            mat.d = *param[9];
            mat.tx = *param[10];
            mat.ty = *param[11];
            _this->AffinePile(mat, src, srcrect, opa, type);
        } else {
            // points mode
            tTVPPointD points[3];
            points[0].x = *param[6];
            points[0].y = *param[7];
            points[1].x = *param[8];
            points[1].y = *param[9];
            points[2].x = *param[10];
            points[2].y = *param[11];
            _this->AffinePile(points, src, srcrect, opa, type);
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ affinePile)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ affineBlend) {
        // src, sx, sy, sw, sh, affine, x0/a, y0/b, x1/c, y1/d, x2/tx,
        // y2/ty, opa=255, type=0
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        if(numparams < 12)
            return TJS_E_BADPARAMCOUNT;

        tTJSNI_BaseLayer *src = nullptr;
        tTJSVariantClosure clo = param[0]->AsObjectClosureNoAddRef();
        if(clo.Object) {
            if(TJS_FAILED(clo.Object->NativeInstanceSupport(
                   TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                   (iTJSNativeInstance **)&src)))
                TVPThrowExceptionMessage(TVPSpecifyLayer);
        }
        if(!src)
            TVPThrowExceptionMessage(TVPSpecifyLayer);

        tTVPRect srcrect(*param[1], *param[2], *param[3], *param[4]);
        srcrect.right += srcrect.left;
        srcrect.bottom += srcrect.top;

        tjs_int opa = 255;
        tTVPBBStretchType type = stNearest;

        if(numparams >= 13 && param[12]->Type() != tvtVoid)
            opa = (tjs_int)*param[12];
        if(numparams >= 14 && param[13]->Type() != tvtVoid)
            type = (tTVPBBStretchType)(tjs_int)*param[13];
        if(numparams >= 15 && param[14]->Type() != tvtVoid) {
            TVPAddLog(TVPFormatMessage(
                TVPHoldDestinationAlphaParameterIsNowDeprecated,
                TJS_W("Layer.affineBlend"), TJS_W("15")));
        }

        if(param[5]->operator bool()) {
            // affine matrix mode
            t2DAffineMatrix mat;
            mat.a = *param[6];
            mat.b = *param[7];
            mat.c = *param[8];
            mat.d = *param[9];
            mat.tx = *param[10];
            mat.ty = *param[11];
            _this->AffineBlend(mat, src, srcrect, opa, type);
        } else {
            // points mode
            tTVPPointD points[3];
            points[0].x = *param[6];
            points[0].y = *param[7];
            points[1].x = *param[8];
            points[1].y = *param[9];
            points[2].x = *param[10];
            points[2].y = *param[11];
            _this->AffineBlend(points, src, srcrect, opa, type);
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ affineBlend)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ operateAffine) {
        // src, sx, sy, sw, sh, affine, x0/a, y0/b, x1/c, y1/d, x2/tx,
        // y2/ty, mode=omAuto, opa=255, type=0
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        if(numparams < 12)
            return TJS_E_BADPARAMCOUNT;

        iTVPBaseBitmap *src = nullptr;
        tTJSVariantClosure clo = param[0]->AsObjectClosureNoAddRef();
        tTVPBlendOperationMode automode = omAlpha;
        if(clo.Object) {
            tTJSNI_BaseLayer *srclayer = nullptr;
            if(TJS_FAILED(clo.Object->NativeInstanceSupport(
                   TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                   (iTJSNativeInstance **)&srclayer)))
                src = nullptr;
            else
                src = srclayer->GetMainImage(),
                automode = srclayer->GetOperationModeFromType();

            if(src == nullptr) { // try to get bitmap interface
                tTJSNI_Bitmap *srcbmp = nullptr;
                if(TJS_FAILED(clo.Object->NativeInstanceSupport(
                       TJS_NIS_GETINSTANCE, tTJSNC_Bitmap::ClassID,
                       (iTJSNativeInstance **)&srcbmp)))
                    src = nullptr;
                else
                    src = srcbmp->GetBitmap();
            }
        }
        if(!src)
            TVPThrowExceptionMessage(TVPSpecifyLayerOrBitmap);

        tTVPRect srcrect(*param[1], *param[2], *param[3], *param[4]);
        srcrect.right += srcrect.left;
        srcrect.bottom += srcrect.top;

        tjs_int opa = 255;
        tTVPBBStretchType type = stNearest;

        if(numparams >= 14 && param[13]->Type() != tvtVoid)
            opa = (tjs_int)*param[13];
        if(numparams >= 15 && param[14]->Type() != tvtVoid)
            type = (tTVPBBStretchType)(tjs_int)*param[14];
        if(numparams >= 16 && param[15]->Type() != tvtVoid) {
            TVPAddLog(TVPFormatMessage(
                TVPHoldDestinationAlphaParameterIsNowDeprecated,
                TJS_W("Layer.operateAffine"), TJS_W("16")));
        }

        tTVPBlendOperationMode mode;
        if(numparams >= 13 && param[12]->Type() != tvtVoid)
            mode = (tTVPBlendOperationMode)(tjs_int)(*param[12]);
        else
            mode = omAuto;

        // get correct blend mode if the mode is omAuto
        if(mode == omAuto)
            mode = automode;

        if(param[5]->operator bool()) {
            // affine matrix mode
            t2DAffineMatrix mat;
            mat.a = *param[6];
            mat.b = *param[7];
            mat.c = *param[8];
            mat.d = *param[9];
            mat.tx = *param[10];
            mat.ty = *param[11];
            _this->OperateAffine(mat, src, srcrect, mode, opa, type);
        } else {
            // points mode
            tTVPPointD points[3];
            points[0].x = *param[6];
            points[0].y = *param[7];
            points[1].x = *param[8];
            points[1].y = *param[9];
            points[2].x = *param[10];
            points[2].y = *param[11];
            _this->OperateAffine(points, src, srcrect, mode, opa, type);
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ operateAffine)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ doBoxBlur) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        tjs_int xblur = 1;
        tjs_int yblur = 1;

        if(numparams >= 1 && param[0]->Type() != tvtVoid)
            xblur = (tjs_int)*param[0];

        if(numparams >= 2 && param[1]->Type() != tvtVoid)
            yblur = (tjs_int)*param[1];

        _this->DoBoxBlur(xblur, yblur);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ doBoxBlur)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ adjustGamma) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        if(numparams == 0)
            return TJS_S_OK;

        tTVPGLGammaAdjustData data;
        memcpy(&data, &TVPIntactGammaAdjustData, sizeof(data));

        if(numparams >= 1 && param[0]->Type() != tvtVoid)
            data.RGamma = param[0]->AsReal();
        if(numparams >= 2 && param[1]->Type() != tvtVoid)
            data.RFloor = *param[1];
        if(numparams >= 3 && param[2]->Type() != tvtVoid)
            data.RCeil = *param[2];
        if(numparams >= 4 && param[3]->Type() != tvtVoid)
            data.GGamma = param[3]->AsReal();
        if(numparams >= 5 && param[4]->Type() != tvtVoid)
            data.GFloor = *param[4];
        if(numparams >= 6 && param[5]->Type() != tvtVoid)
            data.GCeil = *param[5];
        if(numparams >= 7 && param[6]->Type() != tvtVoid)
            data.BGamma = param[6]->AsReal();
        if(numparams >= 8 && param[7]->Type() != tvtVoid)
            data.BFloor = *param[7];
        if(numparams >= 9 && param[8]->Type() != tvtVoid)
            data.BCeil = *param[8];

        _this->AdjustGamma(data);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ adjustGamma)


    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ gaussianBlur) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ layer_instance,
                                /*var. type*/ tTJSNI_Layer);

        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT; // At least radius is needed

        tjs_int radius = 1; // Default radius
        float sigma = 1.0f; // Default sigma

        if(param[0]->Type() != tvtVoid) {
            radius = param[0]->AsInteger();
        }

        if(numparams >= 2 && param[1]->Type() != tvtVoid) {
            sigma = (float)param[1]->AsReal();
        } else {
            // If sigma is not provided, a common practice is to derive it from
            // radius sigma = 0.3 * ((radius - 1) * 0.5 - 1) + 0.8; // Example
            // from some libraries Or simply: sigma = static_cast<float>(radius)
            // / 2.0f; For simplicity, if only radius is given, let's use a
            // sigma that "looks good" for that radius. A common rule of thumb:
            // sigma approx radius/2 or radius/3 Or, use a fixed sensible
            // default if not provided, or require it. Here, we use a default if
            // not given, but you might want to require both.
            if(radius > 0 && sigma == 1.0f &&
               numparams < 2) { // Sigma was not explicitly set
                sigma = std::max(1.0f, static_cast<float>(radius) / 2.5f);
            }
        }

        if(radius <= 0) {
            TVPAddLog(TJS_W("Layer.gaussianBlur: Radius must be positive."));
            return TJS_E_INVALIDPARAM;
        }
        if(sigma <= 0.0f) {
            TVPAddLog(TJS_W("Layer.gaussianBlur: Sigma must be positive."));
            return TJS_E_INVALIDPARAM;
        }


        layer_instance->ApplyGaussianBlur(radius, sigma);

        if(result)
            result->Clear();
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ gaussianBlur)


    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ doGrayScale) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        _this->DoGrayScale();

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ doGrayScale)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ flipLR) // not LRFlip
    {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        _this->LRFlip();

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ flipLR)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ flipUD) // not UDFlip
    {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        _this->UDFlip();

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ flipUD)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ convertType) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        tTVPDrawFace fromtype = (tTVPDrawFace)(tjs_int)*param[0];

        _this->ConvertLayerType(fromtype);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ convertType)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ update) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        // this event sets CallOnPaint flag of tTJSNI_Layer, to
        // invoke onPaint event.

        if(numparams < 1) {
            _this->UpdateByScript();
            // update entire area of the layer
        } else {
            if(numparams < 4)
                return TJS_E_BADPARAMCOUNT;
            tjs_int l, t, w, h;
            l = (tjs_int)*param[0];
            t = (tjs_int)*param[1];
            w = (tjs_int)*param[2];
            h = (tjs_int)*param[3];
            _this->UpdateByScript(tTVPRect(l, t, l + w, t + h));
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ update)
    //---------------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setCursorPos) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        if(numparams < 2)
            return TJS_E_BADPARAMCOUNT;

        _this->SetCursorPos(*param[0], *param[1]);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setCursorPos)
    //---------------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ releaseCapture) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        _this->ReleaseCapture();

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ releaseCapture)
    //---------------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ releaseTouchCapture) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        if(numparams >= 1) {
            tjs_uint32 id = static_cast<tjs_uint32>(param[0]->AsInteger());
            _this->ReleaseTouchCapture(id);
        } else {
            _this->ReleaseTouchCapture(0, true);
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ releaseTouchCapture)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ focus) // not setFocus
    {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        bool direction = true;
        if(numparams >= 1)
            direction = param[0]->operator bool();

        bool succeeded = _this->SetFocus(direction);

        if(result)
            *result = (tjs_int)succeeded;

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ focus)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ focusPrev) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        tTJSNI_BaseLayer *lay = _this->FocusPrev();

        if(result) {
            if(lay && lay->GetOwnerNoAddRef())
                *result = tTJSVariant(lay->GetOwnerNoAddRef(),
                                      lay->GetOwnerNoAddRef());
            else
                *result = tTJSVariant((iTJSDispatch2 *)nullptr);
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ focusPrev)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ focusNext) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        tTJSNI_BaseLayer *lay = _this->FocusNext();

        if(result) {
            if(lay && lay->GetOwnerNoAddRef())
                *result = tTJSVariant(lay->GetOwnerNoAddRef(),
                                      lay->GetOwnerNoAddRef());
            else
                *result = tTJSVariant((iTJSDispatch2 *)nullptr);
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ focusNext)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setMode) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        _this->SetMode();

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setMode)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ removeMode) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        _this->RemoveMode();

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ removeMode)
    //---------------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(
        /*func. name*/ setAttentionPos) // not setAttentionPoint
    {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        if(numparams < 2)
            return TJS_E_BADPARAMCOUNT;

        _this->SetAttentionPoint(*param[0], *param[1]);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setAttentionPos)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ beginTransition) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        // parameters are : <name>, [<withchildren>=true],
        // [<transwith>=nullptr],
        //                  [<options>]
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        ttstr name = *param[0];
        bool withchildren = true;
        if(numparams >= 2 && param[1]->Type() != tvtVoid)
            withchildren = param[1]->operator bool();
        tTJSNI_BaseLayer *transsrc = nullptr;
        if(numparams >= 3 && param[2]->Type() != tvtVoid) {
            tTJSVariantClosure clo = param[2]->AsObjectClosureNoAddRef();
            if(clo.Object) {
                if(TJS_FAILED(clo.Object->NativeInstanceSupport(
                       TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                       (iTJSNativeInstance **)&transsrc)))
                    TVPThrowExceptionMessage(TVPSpecifyLayer);
            }
        }
        if(!transsrc)
            TVPThrowExceptionMessage(TVPSpecifyLayer);

        tTJSVariantClosure options(nullptr, nullptr);
        if(numparams >= 4 && param[3]->Type() != tvtVoid)
            options = param[3]->AsObjectClosureNoAddRef();

        _this->StartTransition(name, withchildren, transsrc, options);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ beginTransition)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ stopTransition) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        _this->StopTransition();

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ stopTransition)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ assignImages) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        tTJSNI_BaseLayer *src = nullptr;
        if(param[0]->Type() != tvtObject)
            TVPThrowExceptionMessage(TVPSpecifyLayer);

        tTJSVariantClosure clo = param[0]->AsObjectClosureNoAddRef();
        if(clo.Object) {
            if(TJS_FAILED(clo.Object->NativeInstanceSupport(
                   TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                   (iTJSNativeInstance **)&src)))
                TVPThrowExceptionMessage(TVPSpecifyLayer);
        }
        if(!src)
            TVPThrowExceptionMessage(TVPSpecifyLayer);

        _this->AssignImages(src);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ assignImages)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ assignMotionImages) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        tTJSNI_BaseLayer *src = nullptr;
        if(param[0]->Type() != tvtObject)
            TVPThrowExceptionMessage(TVPSpecifyLayer);

        tTJSVariantClosure clo = param[0]->AsObjectClosureNoAddRef();
        if(clo.Object) {
            if(TJS_FAILED(clo.Object->NativeInstanceSupport(
                   TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                   (iTJSNativeInstance **)&src)))
                TVPThrowExceptionMessage(TVPSpecifyLayer);
        }
        if(!src)
            TVPThrowExceptionMessage(TVPSpecifyLayer);

        _this->AssignMotionImages(src);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ assignMotionImages)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ dump) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
        _this->DumpStructure();
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ dump)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(
        /*func. name*/ copyToBitmapFromMainImage) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        tTJSVariantClosure clo = param[0]->AsObjectClosureNoAddRef();
        tTJSNI_Bitmap *dstbmp = nullptr;
        if(clo.Object) {
            if(TJS_FAILED(clo.Object->NativeInstanceSupport(
                   TJS_NIS_GETINSTANCE, tTJSNC_Bitmap::ClassID,
                   (iTJSNativeInstance **)&dstbmp)))
                return TJS_E_INVALIDPARAM;
            if(dstbmp)
                _this->CopyFromMainImage(dstbmp);
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(
        /*func. name*/ copyToBitmapFromMainImage)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(
        /*func. name*/ copyFromBitmapToMainImage) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        tTJSVariantClosure clo = param[0]->AsObjectClosureNoAddRef();
        tTJSNI_Bitmap *srcbmp = nullptr;
        if(clo.Object) {
            if(TJS_FAILED(clo.Object->NativeInstanceSupport(
                   TJS_NIS_GETINSTANCE, tTJSNC_Bitmap::ClassID,
                   (iTJSNativeInstance **)&srcbmp)))
                return TJS_E_INVALIDPARAM;
            if(srcbmp)
                _this->AssignMainImageWithUpdate(srcbmp->GetBitmap());
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(
        /*func. name*/ copyFromBitmapToMainImage)
    //----------------------------------------------------------------------

    //-- events

    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onHitTest) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        tTJSVariantClosure obj = _this->GetActionOwnerNoAddRef();
        /*
    this event does not call "action" method
    if(obj.Object)
    {
    TVP_ACTION_INVOKE_BEGIN(3, "onHitTest", objthis);
    TVP_ACTION_INVOKE_MEMBER("x");
    TVP_ACTION_INVOKE_MEMBER("y");
    TVP_ACTION_INVOKE_MEMBER("hit");
    TVP_ACTION_INVOKE_END(obj);
    }
    */
        if(numparams < 3)
            return TJS_E_BADPARAMCOUNT;
        bool b = param[2]->operator bool();

        _this->SetHitTestWork(b);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onHitTest)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onClick) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        TVPLayerEventSourceScope event_source_scope(_this);
        tTJSVariantClosure obj = _this->GetActionOwnerNoAddRef();
        TVPTraceLayerInputEvent("onClick", _this, obj);
        if(obj.Object) {
            TVPTraceLayerActionOwner("onClick", _this, obj);
            if(numparams < 2)
                return TJS_E_BADPARAMCOUNT;
            tjs_int arg_count = 0;
            iTJSDispatch2 *evobj =
                TVPCreateEventObject(TJS_W("onClick"), objthis, objthis);
            tTJSVariant evval(evobj, evobj);
            evobj->Release();
            {
                static ttstr member_name(TJS_W("x"));
                evobj->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP,
                               member_name.c_str(), member_name.GetHint(),
                               param[arg_count++], evobj);
            }
            {
                static ttstr member_name(TJS_W("y"));
                evobj->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP,
                               member_name.c_str(), member_name.GetHint(),
                               param[arg_count++], evobj);
            }
            tTJSVariant *pevval = &evval;
            TVPTraceKagState("before onClick action");
            const tjs_error hr =
                obj.FuncCall(0, TVPActionName.c_str(),
                             TVPActionName.GetHint(), result, 1, &pevval,
                             nullptr);
            TVPTraceLayerActionResult("onClick", _this, hr, result);
            TVPTraceKagState("after onClick action");

            // A copied UI button receives the generic layer action, but its
            // command is stored in current.names[<layer>].exp.  The native
            // action path does not expose that metadata, so follow the
            // original UI contract after dispatching the event.  Restrict
            // this to a distinct metadata object; wrappers that own their
            // expression are handled by the regular action implementation.
            tTJSVariantClosure expression_object;
            ttstr expression;
            if(TVPResolveLayerExplicitExpression(_this, expression_object,
                                                 expression) &&
               expression_object.Object != obj.Object) {
                const bool evaluated = TVPEvaluateLayerExplicitExpression(
                    _this, expression_object, expression);
                if(TVPLayerInputTraceEnabled()) {
                    spdlog::info(
                        "LayerIntf onClick explicit exp fallback layer={} expr={} evaluated={}",
                        _this->GetName().AsStdString(), expression.AsStdString(),
                        evaluated ? "yes" : "no");
                }
            }
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onClick)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onDoubleClick) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        tTJSVariantClosure obj = _this->GetActionOwnerNoAddRef();
        TVPTraceLayerInputEvent("onDoubleClick", _this, obj);
        if(obj.Object) {
            TVP_ACTION_INVOKE_BEGIN(2, "onDoubleClick", objthis);
            TVP_ACTION_INVOKE_MEMBER("x");
            TVP_ACTION_INVOKE_MEMBER("y");
            TVP_ACTION_INVOKE_END(obj);
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onDoubleClick)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onMouseDown) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        tTJSVariantClosure obj = _this->GetActionOwnerNoAddRef();
        TVPTraceLayerInputEvent("onMouseDown", _this, obj);
        if(obj.Object) {
            TVPTraceLayerActionOwner("onMouseDown", _this, obj);
            if(numparams < 4)
                return TJS_E_BADPARAMCOUNT;
            tjs_int arg_count = 0;
            iTJSDispatch2 *evobj =
                TVPCreateEventObject(TJS_W("onMouseDown"), objthis, objthis);
            tTJSVariant evval(evobj, evobj);
            evobj->Release();
            {
                static ttstr member_name(TJS_W("x"));
                evobj->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP,
                               member_name.c_str(), member_name.GetHint(),
                               param[arg_count++], evobj);
            }
            {
                static ttstr member_name(TJS_W("y"));
                evobj->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP,
                               member_name.c_str(), member_name.GetHint(),
                               param[arg_count++], evobj);
            }
            {
                static ttstr member_name(TJS_W("button"));
                evobj->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP,
                               member_name.c_str(), member_name.GetHint(),
                               param[arg_count++], evobj);
            }
            {
                static ttstr member_name(TJS_W("shift"));
                evobj->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP,
                               member_name.c_str(), member_name.GetHint(),
                               param[arg_count++], evobj);
            }
            tTJSVariant *pevval = &evval;
            TVPTraceKagState("before onMouseDown action");
            const tjs_error hr =
                obj.FuncCall(0, TVPActionName.c_str(),
                             TVPActionName.GetHint(), result, 1, &pevval,
                             nullptr);
            TVPTraceLayerActionResult("onMouseDown", _this, hr, result);
            TVPTraceKagState("after onMouseDown action");
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onMouseDown)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onMouseUp) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        TVPLayerEventSourceScope event_source(_this);
        TVPLayerMouseUpContextScope mouse_up_context(_this, numparams, param);
        tTJSVariantClosure obj = _this->GetActionOwnerNoAddRef();
        TVPTraceLayerInputEvent("onMouseUp", _this, obj);
        if(obj.Object) {
            TVPTraceLayerActionOwner("onMouseUp", _this, obj);
            if(numparams < 4)
                return TJS_E_BADPARAMCOUNT;
            tjs_int arg_count = 0;
            iTJSDispatch2 *evobj =
                TVPCreateEventObject(TJS_W("onMouseUp"), objthis, objthis);
            tTJSVariant evval(evobj, evobj);
            evobj->Release();
            {
                static ttstr member_name(TJS_W("x"));
                evobj->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP,
                               member_name.c_str(), member_name.GetHint(),
                               param[arg_count++], evobj);
            }
            {
                static ttstr member_name(TJS_W("y"));
                evobj->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP,
                               member_name.c_str(), member_name.GetHint(),
                               param[arg_count++], evobj);
            }
            {
                static ttstr member_name(TJS_W("button"));
                evobj->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP,
                               member_name.c_str(), member_name.GetHint(),
                               param[arg_count++], evobj);
            }
            {
                static ttstr member_name(TJS_W("shift"));
                evobj->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP,
                               member_name.c_str(), member_name.GetHint(),
                               param[arg_count++], evobj);
            }
            tTJSVariant *pevval = &evval;
            TVPTraceKagState("before onMouseUp action");
            const tjs_error hr =
                obj.FuncCall(0, TVPActionName.c_str(),
                             TVPActionName.GetHint(), result, 1, &pevval,
                             nullptr);
            TVPTraceLayerActionResult("onMouseUp", _this, hr, result);
            TVPTraceKagState("after onMouseUp action");
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onMouseUp)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onMouseMove) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        tTJSVariantClosure obj = _this->GetActionOwnerNoAddRef();
        if(obj.Object) {
            TVP_ACTION_INVOKE_BEGIN(3, "onMouseMove", objthis);
            TVP_ACTION_INVOKE_MEMBER("x");
            TVP_ACTION_INVOKE_MEMBER("y");
            TVP_ACTION_INVOKE_MEMBER("shift");
            TVP_ACTION_INVOKE_END(obj);
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onMouseMove)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onMouseEnter) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        tTJSVariantClosure obj = _this->GetActionOwnerNoAddRef();
        if(obj.Object) {
            TVP_ACTION_INVOKE_BEGIN(0, "onMouseEnter", objthis);
            TVP_ACTION_INVOKE_END(obj);
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onMouseEnter)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onMouseLeave) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        tTJSVariantClosure obj = _this->GetActionOwnerNoAddRef();
        if(obj.Object) {
            TVP_ACTION_INVOKE_BEGIN(0, "onMouseLeave", objthis);
            TVP_ACTION_INVOKE_END(obj);
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onMouseLeave)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onTouchDown) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        tTJSVariantClosure obj = _this->GetActionOwnerNoAddRef();
        if(obj.Object) {
            TVP_ACTION_INVOKE_BEGIN(5, "onTouchDown", objthis);
            TVP_ACTION_INVOKE_MEMBER("x");
            TVP_ACTION_INVOKE_MEMBER("y");
            TVP_ACTION_INVOKE_MEMBER("cx");
            TVP_ACTION_INVOKE_MEMBER("cy");
            TVP_ACTION_INVOKE_MEMBER("id");
            TVP_ACTION_INVOKE_END(obj);
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onTouchDown)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onTouchUp) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        tTJSVariantClosure obj = _this->GetActionOwnerNoAddRef();
        if(obj.Object) {
            TVP_ACTION_INVOKE_BEGIN(5, "onTouchUp", objthis);
            TVP_ACTION_INVOKE_MEMBER("x");
            TVP_ACTION_INVOKE_MEMBER("y");
            TVP_ACTION_INVOKE_MEMBER("cx");
            TVP_ACTION_INVOKE_MEMBER("cy");
            TVP_ACTION_INVOKE_MEMBER("id");
            TVP_ACTION_INVOKE_END(obj);
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onTouchUp)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onTouchMove) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        tTJSVariantClosure obj = _this->GetActionOwnerNoAddRef();
        if(obj.Object) {
            TVP_ACTION_INVOKE_BEGIN(5, "onTouchMove", objthis);
            TVP_ACTION_INVOKE_MEMBER("x");
            TVP_ACTION_INVOKE_MEMBER("y");
            TVP_ACTION_INVOKE_MEMBER("cx");
            TVP_ACTION_INVOKE_MEMBER("cy");
            TVP_ACTION_INVOKE_MEMBER("id");
            TVP_ACTION_INVOKE_END(obj);
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onTouchMove)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onTouchScaling) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        tTJSVariantClosure obj = _this->GetActionOwnerNoAddRef();
        if(obj.Object) {
            TVP_ACTION_INVOKE_BEGIN(5, "onTouchScaling", objthis);
            TVP_ACTION_INVOKE_MEMBER("startdistance");
            TVP_ACTION_INVOKE_MEMBER("currentdistance");
            TVP_ACTION_INVOKE_MEMBER("cx");
            TVP_ACTION_INVOKE_MEMBER("cy");
            TVP_ACTION_INVOKE_MEMBER("flag");
            TVP_ACTION_INVOKE_END(obj);
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onTouchScaling)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onTouchRotate) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        tTJSVariantClosure obj = _this->GetActionOwnerNoAddRef();
        if(obj.Object) {
            TVP_ACTION_INVOKE_BEGIN(6, "onTouchRotate", objthis);
            TVP_ACTION_INVOKE_MEMBER("startangle");
            TVP_ACTION_INVOKE_MEMBER("currentangle");
            TVP_ACTION_INVOKE_MEMBER("distance");
            TVP_ACTION_INVOKE_MEMBER("cx");
            TVP_ACTION_INVOKE_MEMBER("cy");
            TVP_ACTION_INVOKE_MEMBER("flag");
            TVP_ACTION_INVOKE_END(obj);
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onTouchRotate)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onMultiTouch) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        tTJSVariantClosure obj = _this->GetActionOwnerNoAddRef();
        if(obj.Object) {
            TVP_ACTION_INVOKE_BEGIN(0, "onMultiTouch", objthis);
            TVP_ACTION_INVOKE_END(obj);
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onMultiTouch)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onBlur) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        tTJSVariantClosure obj = _this->GetActionOwnerNoAddRef();
        if(obj.Object) {
            TVP_ACTION_INVOKE_BEGIN(1, "onBlur", objthis);
            TVP_ACTION_INVOKE_MEMBER("focused");
            TVP_ACTION_INVOKE_END(obj);
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onBlur)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onFocus) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        tTJSVariantClosure obj = _this->GetActionOwnerNoAddRef();
        if(obj.Object) {
            TVP_ACTION_INVOKE_BEGIN(1, "onFocus", objthis);
            TVP_ACTION_INVOKE_MEMBER("blurred");
            TVP_ACTION_INVOKE_MEMBER("direction");
            TVP_ACTION_INVOKE_END(obj);
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onFocus)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onNodeEnabled) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        tTJSVariantClosure obj = _this->GetActionOwnerNoAddRef();
        if(obj.Object) {
            TVP_ACTION_INVOKE_BEGIN(0, "onNodeEnabled", objthis);
            TVP_ACTION_INVOKE_END(obj);
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onNodeEnabled)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onNodeDisabled) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        tTJSVariantClosure obj = _this->GetActionOwnerNoAddRef();
        if(obj.Object) {
            TVP_ACTION_INVOKE_BEGIN(0, "onNodeDisabled", objthis);
            TVP_ACTION_INVOKE_END(obj);
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onNodeDisabled)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onKeyDown) {
        if(!objthis)
            return TJS_E_NATIVECLASSCRASH;

        tTJSNI_Layer *_this = nullptr;
        tjs_error native_hr = objthis->NativeInstanceSupport(
            TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
            (iTJSNativeInstance **)&_this);
        if(TJS_FAILED(native_hr) || !_this)
            return TJS_S_OK;

        tTJSVariantClosure obj = _this->GetActionOwnerNoAddRef();
        if(obj.Object) {
            TVP_ACTION_INVOKE_BEGIN(3, "onKeyDown", objthis);
            TVP_ACTION_INVOKE_MEMBER("key");
            TVP_ACTION_INVOKE_MEMBER("shift");
            TVP_ACTION_INVOKE_MEMBER("process");
            TVP_ACTION_INVOKE_END(obj);
        }

        // call default key down behavior handler
        if(numparams < 3 || param[2]->operator bool())
            _this->DefaultKeyDown((tjs_int)*param[0], (tjs_int)*param[1]);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onKeyDown)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onKeyUp) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        tTJSVariantClosure obj = _this->GetActionOwnerNoAddRef();
        if(obj.Object) {
            TVP_ACTION_INVOKE_BEGIN(3, "onKeyUp", objthis);
            TVP_ACTION_INVOKE_MEMBER("key");
            TVP_ACTION_INVOKE_MEMBER("shift");
            TVP_ACTION_INVOKE_MEMBER("process");
            TVP_ACTION_INVOKE_END(obj);
        }

        // call default key up behavior handler
        if(numparams < 3 || param[2]->operator bool())
            _this->DefaultKeyUp((tjs_int)*param[0], (tjs_int)*param[1]);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onKeyUp)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onKeyPress) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        tTJSVariantClosure obj = _this->GetActionOwnerNoAddRef();
        if(obj.Object) {
            TVP_ACTION_INVOKE_BEGIN(2, "onKeyPress", objthis);
            TVP_ACTION_INVOKE_MEMBER("key");
            TVP_ACTION_INVOKE_MEMBER("process");
            TVP_ACTION_INVOKE_END(obj);
        }

        // call default key down behavior handler
        if(numparams < 2 || param[1]->operator bool()) {
            ttstr p = *param[0];
            tjs_char code;
            if(p.IsEmpty())
                code = 0;
            else
                code = (tjs_char)*p.c_str();
            _this->DefaultKeyPress(code);
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onKeyPress)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onMouseWheel) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        tTJSVariantClosure obj = _this->GetActionOwnerNoAddRef();
        if(obj.Object) {
            TVP_ACTION_INVOKE_BEGIN(4, "onMouseWheel", objthis);
            TVP_ACTION_INVOKE_MEMBER("shift");
            TVP_ACTION_INVOKE_MEMBER("delta");
            TVP_ACTION_INVOKE_MEMBER("x");
            TVP_ACTION_INVOKE_MEMBER("y");
            TVP_ACTION_INVOKE_END(obj);
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onMouseWheel)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(
        /*func. name*/ onSearchPrevFocusable) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        tTJSVariantClosure obj = _this->GetActionOwnerNoAddRef();
        if(obj.Object) {
            TVP_ACTION_INVOKE_BEGIN(1, "onSearchPrevFocusable", objthis);
            TVP_ACTION_INVOKE_MEMBER("layer");
            TVP_ACTION_INVOKE_END(obj);
        }

        // set focusable layer back
        if(param[0]->Type() != tvtVoid) {
            tTJSVariantClosure clo = param[0]->AsObjectClosureNoAddRef();
            if(clo.Object) {
                tTJSNI_BaseLayer *src;
                if(clo.Object) {
                    if(TJS_FAILED(clo.Object->NativeInstanceSupport(
                           TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                           (iTJSNativeInstance **)&src)))
                        TVPThrowExceptionMessage(TVPSpecifyLayer);
                }
                if(!src)
                    TVPThrowExceptionMessage(TVPSpecifyLayer);
                _this->SetFocusWork(src);
            } else {
                _this->SetFocusWork(nullptr);
            }
        } else {
            _this->SetFocusWork(nullptr);
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onSearchPrevFocusable)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(
        /*func. name*/ onSearchNextFocusable) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        tTJSVariantClosure obj = _this->GetActionOwnerNoAddRef();
        if(obj.Object) {
            TVP_ACTION_INVOKE_BEGIN(1, "onSearchNextFocusable", objthis);
            TVP_ACTION_INVOKE_MEMBER("layer");
            TVP_ACTION_INVOKE_END(obj);
        }

        // set focusable layer back
        if(param[0]->Type() != tvtVoid) {
            tTJSVariantClosure clo = param[0]->AsObjectClosureNoAddRef();
            if(clo.Object) {
                tTJSNI_BaseLayer *src;
                if(clo.Object) {
                    if(TJS_FAILED(clo.Object->NativeInstanceSupport(
                           TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                           (iTJSNativeInstance **)&src)))
                        TVPThrowExceptionMessage(TVPSpecifyLayer);
                }
                if(!src)
                    TVPThrowExceptionMessage(TVPSpecifyLayer);
                _this->SetFocusWork(src);
            } else {
                _this->SetFocusWork(nullptr);
            }
        } else {
            _this->SetFocusWork(nullptr);
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onSearchNextFocusable)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onBeforeFocus) {
        if(!objthis)
            return TJS_E_NATIVECLASSCRASH;

        tTJSNI_Layer *_this = nullptr;
        tjs_error native_hr = objthis->NativeInstanceSupport(
            TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
            (iTJSNativeInstance **)&_this);
        if(TJS_FAILED(native_hr) || !_this)
            return TJS_S_OK;

        tTJSVariantClosure obj = _this->GetActionOwnerNoAddRef();
        if(obj.Object) {
            TVP_ACTION_INVOKE_BEGIN(3, "onBeforeFocus", objthis);
            TVP_ACTION_INVOKE_MEMBER("layer");
            TVP_ACTION_INVOKE_MEMBER("blurred");
            TVP_ACTION_INVOKE_MEMBER("direction");
            TVP_ACTION_INVOKE_END(obj);
        }

        // set focusable layer back
        if(param[0]->Type() != tvtVoid) {
            tTJSVariantClosure clo = param[0]->AsObjectClosureNoAddRef();
            if(clo.Object) {
                tTJSNI_BaseLayer *src;
                if(clo.Object) {
                    if(TJS_FAILED(clo.Object->NativeInstanceSupport(
                           TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                           (iTJSNativeInstance **)&src)))
                        TVPThrowExceptionMessage(TVPSpecifyLayer);
                }
                if(!src)
                    TVPThrowExceptionMessage(TVPSpecifyLayer);
                _this->SetFocusWork(src);
            } else {
                _this->SetFocusWork(nullptr);
            }
        } else {
            _this->SetFocusWork(nullptr);
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onBeforeFocus)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ onPaint) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        tTJSVariantClosure obj = _this->GetActionOwnerNoAddRef();
        if(obj.Object) {
            TVP_ACTION_INVOKE_BEGIN(0, "onPaint", objthis);
            TVP_ACTION_INVOKE_END(obj);
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onPaint)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(
        /*func. name*/ onTransitionCompleted) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);

        tTJSVariantClosure obj = _this->GetActionOwnerNoAddRef();
        if(obj.Object) {
            TVP_ACTION_INVOKE_BEGIN(2, "onTransitionCompleted", objthis);
            TVP_ACTION_INVOKE_MEMBER("dest"); // destination (before exchanging)
            TVP_ACTION_INVOKE_MEMBER("src"); // source (also before
                                             // exchanging;can be a nullptr)
            TVP_ACTION_INVOKE_END(obj);
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ onTransitionCompleted)
    //----------------------------------------------------------------------

    //-- properties

    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_PROP_DECL(parent){
        TJS_BEGIN_NATIVE_PROP_GETTER{ TJS_GET_NATIVE_INSTANCE(
            /*var. name*/ _this, /*var. type*/ tTJSNI_Layer);
    tTJSNI_BaseLayer *parent = _this->GetParent();
    if(parent) {
        iTJSDispatch2 *dsp = parent->GetOwnerNoAddRef();
        *result = tTJSVariant(dsp, dsp);
    } else {
        *result = tTJSVariant((iTJSDispatch2 *)nullptr);
    }
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);

    tTJSNI_BaseLayer *parent;
    tTJSVariantClosure clo = param->AsObjectClosureNoAddRef();
    if(clo.Object) {
        if(TJS_FAILED(clo.Object->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               (iTJSNativeInstance **)&parent)))
            TVPThrowExceptionMessage(TVPSpecifyLayer);
    }

    _this->SetParent(parent);

    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(parent)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(children){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
iTJSDispatch2 *dsp = _this->GetChildrenArrayObjectNoAddRef();
*result = tTJSVariant(dsp, dsp);
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(children)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(order) // not orderIndex
{ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = (tjs_int)_this->GetOrderIndex();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    _this->SetOrderIndex(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(order)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(absolute) // not absoluteOrderIndex
{ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = (tjs_int)_this->GetAbsoluteOrderIndex();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    _this->SetAbsoluteOrderIndex(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(absolute)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(absoluteOrderMode) // not absoluteOrderIndexMode
{ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = (tjs_int)_this->GetAbsoluteOrderMode();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    _this->SetAbsoluteOrderMode(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(absoluteOrderMode)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(visible){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = _this->GetVisible();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    _this->SetVisible(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(visible)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(cached){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = _this->GetCached();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    _this->SetCached(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(cached)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(nodeVisible){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = _this->GetNodeVisible();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(nodeVisible)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(opacity){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = _this->GetOpacity();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    _this->SetOpacity(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(opacity)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(window){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
iTVPLayerTreeOwner *lto = _this->GetLayerTreeOwner();
if(!lto) {
    *result = tTJSVariant((iTJSDispatch2 *)nullptr);
} else {
    iTJSDispatch2 *dsp = lto->GetOwnerNoAddRef();
    *result = tTJSVariant(dsp, dsp);
}
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(window)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(isPrimary){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = (tjs_int)_this->IsPrimary();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(isPrimary)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(left){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = _this->GetLeft();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    _this->SetLeft(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(left)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(top){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = _this->GetTop();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    _this->SetTop(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(top)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(width){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = (tjs_int64)_this->GetWidth();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    _this->SetWidth(static_cast<tjs_uint>((tjs_int64)*param));
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(width)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(height){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = (tjs_int64)_this->GetHeight();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    _this->SetHeight(static_cast<tjs_uint>((tjs_int64)*param));
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(height)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(imageLeft){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = (tjs_int64)_this->GetImageLeft();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    _this->SetImageLeft(static_cast<tjs_int>((tjs_int64)*param));
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(imageLeft)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(imageTop){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = (tjs_int64)_this->GetImageTop();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    _this->SetImageTop(static_cast<tjs_int>((tjs_int64)*param));
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(imageTop)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(imageWidth){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = (tjs_int64)_this->GetImageWidth();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    _this->SetImageWidth(static_cast<tjs_uint>((tjs_int64)*param));
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(imageWidth)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(imageHeight){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = (tjs_int64)_this->GetImageHeight();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    _this->SetImageHeight(static_cast<tjs_uint>((tjs_int64)*param));
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(imageHeight)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(type){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = (tjs_int)_this->GetType();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    _this->SetType((tTVPLayerType)(tjs_int)*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(type)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(face){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = (tjs_int)_this->GetFace();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    _this->SetFace((tTVPDrawFace)(tjs_int)*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(face)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(holdAlpha){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = (tjs_int)_this->GetHoldAlpha();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    _this->SetHoldAlpha(0 != (tjs_int)*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(holdAlpha)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(clipLeft){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = (tjs_int64)_this->GetClipLeft();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    _this->SetClipLeft(static_cast<tjs_int>((tjs_int64)*param));
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(clipLeft)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(clipTop){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = (tjs_int64)_this->GetClipTop();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    _this->SetClipTop(static_cast<tjs_int>((tjs_int64)*param));
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(clipTop)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(clipWidth){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = (tjs_int64)_this->GetClipWidth();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    _this->SetClipWidth(static_cast<tjs_int>((tjs_int64)*param));
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(clipWidth)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(clipHeight){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = (tjs_int64)_this->GetClipHeight();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    _this->SetClipHeight(static_cast<tjs_int>((tjs_int64)*param));
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(clipHeight)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(imageModified){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = (tjs_int)_this->GetImageModified();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    _this->SetImageModified(param->operator bool());
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(imageModified)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(hitType){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = (tjs_int)_this->GetHitType();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    _this->SetHitType((tTVPHitType)(tjs_int)*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(hitType)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(hitThreshold){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = _this->GetHitThreshold();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    _this->SetHitThreshold((tjs_int)*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(hitThreshold)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(cursor){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = _this->GetCursor();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    if(param->Type() == tvtString)
        _this->SetCursorByStorage(*param);
    else
        _this->SetCursorByNumber(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(cursor)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(cursorX){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = _this->GetCursorX();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    _this->SetCursorX(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(cursorX)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(cursorY){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = _this->GetCursorY();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    _this->SetCursorY(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(cursorY)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(hint){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = _this->GetHint();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    _this->SetHint(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(hint)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(showParentHint){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = _this->GetShowParentHint();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    _this->SetShowParentHint(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(showParentHint)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(ignoreHintSensing){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = _this->GetIgnoreHintSensing();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    _this->SetIgnoreHintSensing(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(ignoreHintSensing)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(focusable){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = _this->GetFocusable();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    _this->SetFocusable(param->operator bool());
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(focusable)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(prevFocusable){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
tTJSNI_BaseLayer *lay = _this->GetPrevFocusable();
if(lay) {
    iTJSDispatch2 *dsp = lay->GetOwnerNoAddRef();
    *result = tTJSVariant(dsp, dsp);
} else {
    *result = tTJSVariant((iTJSDispatch2 *)nullptr);
}
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(prevFocusable)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(nextFocusable){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
tTJSNI_BaseLayer *lay = _this->GetNextFocusable();
if(lay) {
    iTJSDispatch2 *dsp = lay->GetOwnerNoAddRef();
    *result = tTJSVariant(dsp, dsp);
} else {
    *result = tTJSVariant((iTJSDispatch2 *)nullptr);
}
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(nextFocusable)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(joinFocusChain){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = _this->GetJoinFocusChain();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    _this->SetJoinFocusChain(param->operator bool());
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(joinFocusChain)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(nodeFocusable){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = _this->GetNodeFocusable();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(nodeFocusable)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(focused){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = (tjs_int)_this->GetFocused();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(focused)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(enabled){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = _this->GetEnabled();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    _this->SetEnabled(param->operator bool());
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(enabled)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(nodeEnabled){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = _this->GetNodeEnabled();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(nodeEnabled)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(attentionLeft){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = _this->GetAttentionLeft();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    _this->SetAttentionLeft((tjs_int)*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(attentionLeft)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(attentionTop){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = _this->GetAttentionTop();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    _this->SetAttentionTop((tjs_int)*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(attentionTop)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(useAttention){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = _this->GetUseAttention();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    _this->SetUseAttention(param->operator bool());
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(useAttention)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(imeMode){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = (tjs_int)_this->GetImeMode();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    _this->SetImeMode((tTVPImeMode)(tjs_int)*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(imeMode)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(callOnPaint){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = _this->GetCallOnPaint();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    _this->SetCallOnPaint(param->operator bool());
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(callOnPaint)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(font){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
iTJSDispatch2 *dsp = _this->GetFontObjectNoAddRef();
*result = tTJSVariant(dsp, dsp);
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(font)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(name){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = _this->GetName();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    const ttstr old_name = _this->GetName();
    _this->SetName(*param);
    const bool new_is_chflash =
        ttstr(*param) == TJS_W("chflash");
    if(TVPFlashTraceEnabled() &&
       (new_is_chflash || old_name.IsEmpty() ||
        old_name == TJS_W("chflash"))) {
        spdlog::info("Flash.name layer={} ptr={} old={} new={} visible={} "
                     "opacity={} image={} pcenter=0x{:08x}",
                     _this->GetName().AsStdString(), static_cast<void *>(_this),
                     old_name.AsStdString(), _this->GetName().AsStdString(),
                     _this->GetVisible() ? "yes" : "no", _this->GetOpacity(),
                     _this->GetMainImage() ? "yes" : "no",
                     TVPStage2Sample(_this, _this->GetMainImage()
                                             ? static_cast<tjs_int>(_this->GetMainImage()->GetWidth() / 2)
                                             : 0,
                                     _this->GetMainImage()
                                         ? static_cast<tjs_int>(_this->GetMainImage()->GetHeight() / 2)
                                         : 0));
    }
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(name)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(neutralColor){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = (tjs_int64)_this->GetNeutralColor();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    _this->SetNeutralColor(static_cast<tjs_uint32>((tjs_int64)*param));
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(neutralColor)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(hasImage){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
*result = (tjs_int)(bool)_this->GetHasImage();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
    _this->SetHasImage((bool)*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(hasImage)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(mainImageBuffer){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
;
*result = (tTVInteger) reinterpret_cast<tjs_intptr_t>(
    _this->GetMainImagePixelBuffer());
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(mainImageBuffer)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(mainImageBufferForWrite){
    TJS_BEGIN_NATIVE_PROP_GETTER{
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
;
*result = (tTVInteger) reinterpret_cast<tjs_intptr_t>(
    _this->GetMainImagePixelBufferForWrite());
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(mainImageBufferForWrite)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(mainImageBufferPitch){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
;
*result = _this->GetMainImagePixelBufferPitch();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(mainImageBufferPitch)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(provinceImageBuffer){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Layer);
;
*result = (tTVInteger) reinterpret_cast<tjs_intptr_t>(
    _this->GetProvinceImagePixelBuffer());
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(provinceImageBuffer)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(provinceImageBufferForWrite){
    TJS_BEGIN_NATIVE_PROP_GETTER{
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
;
*result = (tTVInteger) reinterpret_cast<tjs_intptr_t>(
    _this->GetProvinceImagePixelBufferForWrite());
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(provinceImageBufferForWrite)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(provinceImageBufferPitch){
    TJS_BEGIN_NATIVE_PROP_GETTER{
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Layer);
;
*result = _this->GetProvinceImagePixelBufferPitch();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(provinceImageBufferPitch)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(mainImageGLTexture) {
    TJS_BEGIN_NATIVE_PROP_GETTER {
        TJS_GET_NATIVE_INSTANCE(_this, tTJSNI_Layer);
        tTVPBaseTexture *img = _this->GetMainImage();
        if (img && img->GetTexture()) {
            *result = (tTVInteger)img->GetTexture()->GetNativeGLTextureId();
        } else {
            *result = (tTVInteger)0;
        }
        return TJS_S_OK;
    }
    TJS_END_NATIVE_PROP_GETTER
    TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(mainImageGLTexture)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(mainImageGLTextureInternalWidth) {
    TJS_BEGIN_NATIVE_PROP_GETTER {
        TJS_GET_NATIVE_INSTANCE(_this, tTJSNI_Layer);
        tTVPBaseTexture *img = _this->GetMainImage();
        if (img && img->GetTexture()) {
            *result = (tTVInteger)img->GetTexture()->GetInternalWidth();
        } else {
            *result = (tTVInteger)0;
        }
        return TJS_S_OK;
    }
    TJS_END_NATIVE_PROP_GETTER
    TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(mainImageGLTextureInternalWidth)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(mainImageGLTextureInternalHeight) {
    TJS_BEGIN_NATIVE_PROP_GETTER {
        TJS_GET_NATIVE_INSTANCE(_this, tTJSNI_Layer);
        tTVPBaseTexture *img = _this->GetMainImage();
        if (img && img->GetTexture()) {
            *result = (tTVInteger)img->GetTexture()->GetInternalHeight();
        } else {
            *result = (tTVInteger)0;
        }
        return TJS_S_OK;
    }
    TJS_END_NATIVE_PROP_GETTER
    TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(mainImageGLTextureInternalHeight)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(invalidateGLTextureCache) {
    TJS_GET_NATIVE_INSTANCE(_this, tTJSNI_Layer);
    tTVPBaseTexture *img = _this->GetMainImage();
    if (img && img->GetTexture()) {
        img->GetTexture()->InvalidatePixelCache();
    }
    _this->SetImageModified(true);
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(invalidateGLTextureCache)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(resetStyle) {
    TJS_GET_NATIVE_INSTANCE(_this, tTJSNI_Layer);
    if (result) result->Clear();
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(resetStyle)
//----------------------------------------------------------------------
TJS_END_NATIVE_MEMBERS
}
//---------------------------------------------------------------------------

extern FontSystem *TVPFontSystem;

//---------------------------------------------------------------------------
// tTJSNI_Font : Font Native Object
//---------------------------------------------------------------------------
tTJSNI_Font::tTJSNI_Font() { Layer = nullptr; }

//---------------------------------------------------------------------------
tTJSNI_Font::~tTJSNI_Font() {}

//---------------------------------------------------------------------------
tjs_error tTJSNI_Font::Construct(tjs_int numparams, tTJSVariant **param,
                                 iTJSDispatch2 *tjs_obj) {
    if(numparams >= 1) {
        iTJSDispatch2 *dsp = param[0]->AsObjectNoAddRef();

        tTJSNI_Layer *lay = nullptr;
        if(TJS_FAILED(dsp->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
                                                 tTJSNC_Layer::ClassID,
                                                 (iTJSNativeInstance **)&lay)))
            TVPThrowExceptionMessage(TVPSpecifyLayer);

        Layer = lay;
    } else {
        Layer = nullptr;
        Font = TVPFontSystem->GetDefaultFont();
    }

    return TJS_S_OK;
}

//---------------------------------------------------------------------------
void tTJSNI_Font::Invalidate() {
    Layer = nullptr;

    inherited::Invalidate();
}

//---------------------------------------------------------------------------
void tTJSNI_Font::SetFontFace(const ttstr &face) {
    if(Layer)
        Layer->SetFontFace(face);
    else {
        if(Font.Face != face) {
            Font.Face = face;
        }
    }
}

//---------------------------------------------------------------------------
ttstr tTJSNI_Font::GetFontFace() const {
    if(Layer)
        return Layer->GetFontFace();
    else
        return Font.Face;
}

//---------------------------------------------------------------------------
void tTJSNI_Font::SetFontHeight(tjs_int height) {
    if(Layer)
        Layer->SetFontHeight(height);
    else {
        if(height < 0)
            height = -height; // TVP2 does not support negative value
                              // of height
        if(Font.Height != height) {
            Font.Height = height;
        }
    }
}

//---------------------------------------------------------------------------
tjs_int tTJSNI_Font::GetFontHeight() const {
    if(Layer)
        return Layer->GetFontHeight();
    else
        return Font.Height;
}

//---------------------------------------------------------------------------
void tTJSNI_Font::SetFontAngle(tjs_int angle) {
    if(Layer)
        Layer->SetFontAngle(angle);
    else {
        if(Font.Angle != angle) {
            angle = angle % 3600;
            if(angle < 0)
                angle += 3600;
            Font.Angle = angle;
        }
    }
}

//---------------------------------------------------------------------------
tjs_int tTJSNI_Font::GetFontAngle() const {
    if(Layer)
        return Layer->GetFontAngle();
    else
        return Font.Angle;
}

//---------------------------------------------------------------------------
void tTJSNI_Font::SetFontBold(bool b) {
    if(Layer)
        Layer->SetFontBold(b);
    else {
        if((0 != (Font.Flags & TVP_TF_BOLD)) != b) {
            Font.Flags &= ~TVP_TF_BOLD;
            if(b)
                Font.Flags |= TVP_TF_BOLD;
        }
    }
}

//---------------------------------------------------------------------------
bool tTJSNI_Font::GetFontBold() const {
    if(Layer)
        return Layer->GetFontBold();
    else
        return 0 != (Font.Flags & TVP_TF_BOLD);
}

//---------------------------------------------------------------------------
void tTJSNI_Font::SetFontItalic(bool b) {
    if(Layer)
        Layer->SetFontItalic(b);
    else {
        if((0 != (Font.Flags & TVP_TF_ITALIC)) != b) {
            Font.Flags &= ~TVP_TF_ITALIC;
            if(b)
                Font.Flags |= TVP_TF_ITALIC;
        }
    }
}

//---------------------------------------------------------------------------
bool tTJSNI_Font::GetFontItalic() const {
    if(Layer)
        return Layer->GetFontItalic();
    else
        return 0 != (Font.Flags & TVP_TF_ITALIC);
}

//---------------------------------------------------------------------------
void tTJSNI_Font::SetFontStrikeout(bool b) {
    if(Layer)
        Layer->SetFontStrikeout(b);
    else {
        if((0 != (Font.Flags & TVP_TF_STRIKEOUT)) != b) {
            Font.Flags &= ~TVP_TF_STRIKEOUT;
            if(b)
                Font.Flags |= TVP_TF_STRIKEOUT;
        }
    }
}

//---------------------------------------------------------------------------
bool tTJSNI_Font::GetFontStrikeout() const {
    if(Layer)
        return Layer->GetFontStrikeout();
    else
        return 0 != (Font.Flags & TVP_TF_STRIKEOUT);
}

//---------------------------------------------------------------------------
void tTJSNI_Font::SetFontUnderline(bool b) {
    if(Layer)
        Layer->SetFontUnderline(b);
    else {
        if((0 != (Font.Flags & TVP_TF_UNDERLINE)) != b) {
            Font.Flags &= ~TVP_TF_UNDERLINE;
            if(b)
                Font.Flags |= TVP_TF_UNDERLINE;
        }
    }
}

//---------------------------------------------------------------------------
bool tTJSNI_Font::GetFontUnderline() const {
    if(Layer)
        return Layer->GetFontUnderline();
    else
        return 0 != (Font.Flags & TVP_TF_UNDERLINE);
}

//---------------------------------------------------------------------------
void tTJSNI_Font::SetFontFaceIsFileName(bool b) {
    if(Layer)
        Layer->SetFontFaceIsFileName(b);
    else {
        if((0 != (Font.Flags & TVP_TF_FONTFILE)) != b) {
            Font.Flags &= ~TVP_TF_FONTFILE;
            if(b)
                Font.Flags |= TVP_TF_FONTFILE;
        }
    }
}

//---------------------------------------------------------------------------
bool tTJSNI_Font::GetFontFaceIsFileName() const {
    if(Layer)
        return Layer->GetFontFaceIsFileName();
    else
        return 0 != (Font.Flags & TVP_TF_FONTFILE);
}

//---------------------------------------------------------------------------
tjs_int tTJSNI_Font::GetTextWidthDirect(const ttstr &text) {
    GetCurrentRasterizer()->ApplyFont(Font);
    tjs_uint width = 0;
    const tjs_char *buf = text.c_str();
    while(*buf) {
        tjs_int w, h;
        GetCurrentRasterizer()->GetTextExtent(*buf, w, h);
        width += w;
        buf++;
    }
    return width;
}

//---------------------------------------------------------------------------
tjs_int tTJSNI_Font::GetTextWidth(const ttstr &text) {
    if(Layer)
        return Layer->GetTextWidth(text);
    else
        return GetTextWidthDirect(text);
}

//---------------------------------------------------------------------------
tjs_int tTJSNI_Font::GetTextHeight(const ttstr &text) {
    if(Layer)
        return Layer->GetTextHeight(text);
    else
        return std::abs(Font.Height);
}

//---------------------------------------------------------------------------
double tTJSNI_Font::GetEscWidthX(const ttstr &text) {
    if(Layer)
        return Layer->GetEscWidthX(text);
    else
        return std::cos(Font.Angle * (M_PI / 1800)) * GetTextWidthDirect(text);
}

//---------------------------------------------------------------------------
double tTJSNI_Font::GetEscWidthY(const ttstr &text) {
    if(Layer)
        return Layer->GetEscWidthY(text);
    else
        return std::sin(Font.Angle * (M_PI / 1800)) *
            (-GetTextWidthDirect(text));
}

//---------------------------------------------------------------------------
double tTJSNI_Font::GetEscHeightX(const ttstr &text) {
    if(Layer)
        return Layer->GetEscHeightX(text);
    else
        return std::sin(Font.Angle * (M_PI / 1800)) * std::abs(Font.Height);
}

//---------------------------------------------------------------------------
double tTJSNI_Font::GetEscHeightY(const ttstr &text) {
    if(Layer)
        return Layer->GetEscHeightY(text);
    else
        return std::cos(Font.Angle * (M_PI / 1800)) * std::abs(Font.Height);
}

//---------------------------------------------------------------------------
void tTJSNI_Font::GetFontGlyphDrawRect(const ttstr &text, tTVPRect &area) {
    if(Layer) {
        Layer->GetFontGlyphDrawRect(text, area);
    } else {
        GetCurrentRasterizer()->ApplyFont(Font);
        GetCurrentRasterizer()->GetGlyphDrawRect(text, area);
    }
}

//---------------------------------------------------------------------------
extern void TVPGetAllFontList(std::vector<ttstr> &list);

void tTJSNI_Font::GetFontList(tjs_uint32 flags, std::vector<ttstr> &list) {
    if(Layer)
        Layer->GetFontList(flags, list);
    else {
        std::vector<ttstr> ansilist;
        TVPGetAllFontList(ansilist);
        for(std::vector<ttstr>::iterator i = ansilist.begin();
            i != ansilist.end(); i++)
            list.push_back(i->c_str());
    }
}

//---------------------------------------------------------------------------
void tTJSNI_Font::MapPrerenderedFont(const ttstr &storage) {
    if(Layer)
        Layer->MapPrerenderedFont(storage);
    else
        TVPMapPrerenderedFont(Font, storage);
}

//---------------------------------------------------------------------------
void tTJSNI_Font::UnmapPrerenderedFont() {
    if(Layer)
        Layer->UnmapPrerenderedFont();
    else
        TVPUnmapPrerenderedFont(Font);
}

//---------------------------------------------------------------------------
const tTVPFont &tTJSNI_Font::GetFont() const {
    if(Layer)
        return Layer->GetFont();
    else
        return Font;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNC_Font : TJS Font class
//---------------------------------------------------------------------------
tjs_uint32 tTJSNC_Font::ClassID = -1;

static tjs_error TVPAddFontForScript(tTJSVariant *result, tjs_int numparams,
                                     tTJSVariant **param) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    std::vector<ttstr> fontNames;
    const ttstr filename = TVPGetPlacedPath(*param[0]);
    if(filename.length())
        TVPEnumFontsProc(filename, &fontNames);

    if(result) {
        iTJSDispatch2 *array = TJSCreateArrayObject();
        tTJSVariant arrayValue(array, array);
        *result = arrayValue;
        array->Release();

        for(tjs_uint i = 0; i < fontNames.size(); ++i) {
            tTJSVariant name(fontNames[i]);
            array->PropSetByNum(TJS_MEMBERENSURE, i, &name, array);
        }
    }

    return TJS_S_OK;
}

tTJSNC_Font::tTJSNC_Font() : tTJSNativeClass(TJS_W("Font")) {
    TJS_BEGIN_NATIVE_MEMBERS(Font) // constructor
    TJS_DECL_EMPTY_FINALIZE_METHOD
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL(/*var.name*/ _this,
                                      /*var.type*/ tTJSNI_Font,
                                      /*TJS class name*/ Font) {
        return TJS_S_OK;
    }
    TJS_END_NATIVE_CONSTRUCTOR_DECL(/*TJS class name*/ Font)
    //----------------------------------------------------------------------

    //-- methods

    // KAG3's original PolyfillInitialize.tjs loads its bundled font through
    // Font.addFont() and uses the first returned family name as -deffont.
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ addFont) {
        return TVPAddFontForScript(result, numparams, param);
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ addFont)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ AddFont) {
        return TVPAddFontForScript(result, numparams, param);
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ AddFont)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ AddTrueTypeFont) {
        return TVPAddFontForScript(result, numparams, param);
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL(/*func. name*/ AddTrueTypeFont)

    //---------------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ getTextWidth) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Font);
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        if(result)
            *result = _this->GetTextWidth(*param[0]);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ getTextWidth)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ getTextHeight) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Font);
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        if(result)
            *result = _this->GetTextHeight(*param[0]);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ getTextHeight)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ getEscWidthX) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Font);
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        if(result)
            *result = _this->GetEscWidthX(*param[0]);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ getEscWidthX)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ getEscWidthY) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Font);
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        if(result)
            *result = _this->GetEscWidthY(*param[0]);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ getEscWidthY)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ getEscHeightX) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Font);
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        if(result)
            *result = _this->GetEscHeightX(*param[0]);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ getEscHeightX)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ getEscHeightY) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Font);
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        if(result)
            *result = _this->GetEscHeightY(*param[0]);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ getEscHeightY)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ getGlyphDrawRect) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Font);
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        if(result) {
            tTVPRect rt;
            _this->GetFontGlyphDrawRect(*param[0], rt);
            iTJSDispatch2 *disp =
                TVPCreateRectObject(rt.left, rt.top, rt.right, rt.bottom);
            *result = tTJSVariant(disp, disp);
            disp->Release();
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ getGlyphDrawRect)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ doUserSelect) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Font);

        if(numparams < 4)
            return TJS_E_BADPARAMCOUNT;

        tjs_uint32 flags = (tjs_int64)*param[0];
        ttstr caption = *param[1];
        ttstr prompt = *param[2];
        ttstr samplestring = *param[3];

        tjs_int ret = // TODO: implement it ?
#if 0
                                                                                                                                                    (tjs_int)_this->GetLayer()->DoUserFontSelect(flags, caption,
		prompt, samplestring);
#else
            0;
#endif

        if(result)
            *result = ret;

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ doUserSelect)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ getList) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Font);

        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        tjs_uint32 flags = static_cast<tjs_uint32>((tjs_int64)*param[0]);

        std::vector<ttstr> list;
        _this->GetFontList(flags, list);

        if(result) {
            iTJSDispatch2 *dsp;
            dsp = TJSCreateArrayObject();
            tTJSVariant tmp(dsp, dsp);
            *result = tmp;
            dsp->Release();

            for(tjs_uint i = 0; i < list.size(); i++) {
                tmp = list[i];
                dsp->PropSetByNum(TJS_MEMBERENSURE, i, &tmp, dsp);
            }
        }

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ getList)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ mapPrerenderedFont) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Font);
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        _this->MapPrerenderedFont(*param[0]);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ mapPrerenderedFont)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(
        /*func. name*/ unmapPrerenderedFont) {
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Font);
        if(numparams < 0)
            return TJS_E_BADPARAMCOUNT;

        _this->UnmapPrerenderedFont();

        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(/*func. name*/ unmapPrerenderedFont)
    //----------------------------------------------------------------------

    //-- properties

    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_PROP_DECL(face){
        TJS_BEGIN_NATIVE_PROP_GETTER{ TJS_GET_NATIVE_INSTANCE(
            /*var. name*/ _this, /*var. type*/ tTJSNI_Font);
    *result = _this->GetFontFace();
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Font);
    _this->SetFontFace(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(face)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(height){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Font);
*result = _this->GetFontHeight();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Font);
    _this->SetFontHeight(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(height)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(bold){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Font);
*result = _this->GetFontBold();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Font);
    _this->SetFontBold(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(bold)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(italic){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Font);
*result = _this->GetFontItalic();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Font);
    _this->SetFontItalic(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(italic)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(strikeout){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Font);
*result = _this->GetFontStrikeout();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Font);
    _this->SetFontStrikeout(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(strikeout)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(underline){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Font);
*result = _this->GetFontUnderline();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Font);
    _this->SetFontUnderline(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(underline)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(angle){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Font);
*result = _this->GetFontAngle();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Font);
    _this->SetFontAngle(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(angle)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(faceIsFileName){
    // Face名をファイル名として開く、FreeTypeでのみ有効。ただし、そのレイヤーでIMEを有効した場合動作は不定
    TJS_BEGIN_NATIVE_PROP_GETTER{
        TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                /*var. type*/ tTJSNI_Font);
*result = _this->GetFontFaceIsFileName();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_Font);
    _this->SetFontFaceIsFileName(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(faceIsFileName)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(rasterizer){
    TJS_BEGIN_NATIVE_PROP_GETTER{ *result = TVPGetFontRasterizer();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TVPSetFontRasterizer(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL(rasterizer)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(defaultFaceName){
    TJS_BEGIN_NATIVE_PROP_GETTER{ *result = TVPGetDefaultFontName();
// *result = ttstr(TVPFontSystem->GetDefaultFontName());
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    ttstr name(*param);
    TVPSetDefaultFontName(name);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_STATIC_PROP_DECL(defaultFaceName)
//----------------------------------------------------------------------

TJS_END_NATIVE_MEMBERS
}

//---------------------------------------------------------------------------
// TVPCreateNativeClass_Font
//---------------------------------------------------------------------------
struct tFontClassHolder {
    tTJSNativeClass *Obj;

    tFontClassHolder() : Obj(nullptr) {}

    void Set(tTJSNativeClass *obj) {
        if(Obj) {
            Obj->Release();
            Obj = nullptr;
        }
        Obj = obj;
        Obj->AddRef();
    }

    ~tFontClassHolder() {
        if(Obj)
            Obj->Release(), Obj = nullptr;
    }
} static fontclassholder;

//---------------------------------------------------------------------------
tTJSNativeClass *TVPCreateNativeClass_Font() {
    if(fontclassholder.Obj) {
        tTJSNativeClass *fontclass = fontclassholder.Obj;
        fontclass->AddRef();
        return fontclass;
    }
    tTJSNativeClass *fontclass = new tTJSNC_Font();
    fontclassholder.Set(fontclass);
    return fontclass;
}

//---------------------------------------------------------------------------
iTJSDispatch2 *TVPCreateFontObject(iTJSDispatch2 *layer) {
    if(fontclassholder.Obj == nullptr) {
        TVPThrowInternalError;
    }
    iTJSDispatch2 *out;
    tTJSVariant param(layer);
    tTJSVariant *pparam = &param;
    if(TJS_FAILED(fontclassholder.Obj->CreateNew(0, nullptr, nullptr, &out, 1,
                                                 &pparam, fontclassholder.Obj)))
        TVPThrowInternalError;

    return out;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
iTJSDispatch2 *TVPGetObjectFrom_NI_BaseLayer(tTJSNI_BaseLayer *layer) {
    iTJSDispatch2 *disp = layer->Owner;
    if(disp)
        disp->AddRef();
    return disp;
}
//---------------------------------------------------------------------------
