//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Layer Management
//---------------------------------------------------------------------------

#include "tjsCommHead.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#ifdef __ANDROID__
#include <android/log.h>
#endif

#include "tjsArray.h"
#include "LayerManager.h"
#include "MsgIntf.h"
#include "LayerBitmapIntf.h"
#include "StorageIntf.h"
#include "EventIntf.h"
#include "SysInitIntf.h"
#include "TickCount.h"
#include "ScriptMgnIntf.h"
#include "DebugIntf.h"
#include "BitmapIntf.h"
#include "BitmapLayerTreeOwner.h"
#include "LayerTreeOwner.h"
#include "WindowIntf.h"
#include "EngineLoop.h"
#include "RenderManager.h"
#include "spdlog/spdlog.h"

#include <cctype>
#include <string>

namespace {
bool TVPInputTraceEnabled() {
    const char *value = std::getenv("AETHERKIRI_INPUT_TRACE");
    return value && *value && *value != '0';
}

#ifdef __ANDROID__
#define AETHER_INPUT_TRACE_LOG(...)                                             \
    do {                                                                        \
        if(TVPInputTraceEnabled()) {                                            \
            __android_log_print(ANDROID_LOG_INFO, "aether-input", __VA_ARGS__); \
        }                                                                       \
    } while(0)
#else
#define AETHER_INPUT_TRACE_LOG(...)                                             \
    do {                                                                        \
    } while(0)
#endif

void TVPTraceExpressionValue(const char *name, const tjs_char *expression) {
    try {
        tTJSVariant value;
        TVPExecuteExpression(ttstr(expression), &value);
        spdlog::info("LayerManager title diag {}={}", name,
                     ttstr(value).AsStdString());
    } catch(const eTJS &e) {
        spdlog::info("LayerManager title diag {} failed: {}", name,
                     ttstr(e.GetMessage()).AsStdString());
    } catch(...) {
        spdlog::info("LayerManager title diag {} failed", name);
    }
}

bool TVPScriptIsCgModeViewTrans() {
    try {
        tTJSVariant result;
        TVPExecuteExpression(
            TJS_W("typeof kag == \"Object\" && kag && "
                  "kag.currentStorage == \"cgmode.ks\" && "
                  "kag.currentLabel == \"*viewtrans\""),
            &result);
        return result.operator bool();
    } catch(...) {
        return false;
    }
}

bool TVPRouteCgModePreviewRightClick() {
    if(!TVPScriptIsCgModeViewTrans())
        return false;

    try {
        tTJSVariant result;
        TVPExecuteExpression(
            TJS_W("kag.process(\"\", \"*view_rclick\")"), &result);
        if(TVPInputTraceEnabled()) {
            spdlog::info(
                "LayerManager routed cgmode preview right click to *view_rclick");
        }
        return true;
    } catch(const eTJS &e) {
        if(TVPInputTraceEnabled()) {
            spdlog::info(
                "LayerManager cgmode preview right click route failed: {}",
                ttstr(e.GetMessage()).AsStdString());
        }
    } catch(...) {
        if(TVPInputTraceEnabled()) {
            spdlog::info(
                "LayerManager cgmode preview right click route failed");
        }
    }
    return false;
}

bool TVPScriptIsCgPreviewLoop() {
    try {
        tTJSVariant result;
        TVPExecuteExpression(
            TJS_W("typeof kag == \"Object\" && kag && "
                  "((kag.currentStorage == \"extra.ks\" && "
                  "  kag.currentLabel == \"*viewloop\") || "
                  " (kag.currentStorage == \"cgmode.ks\" && "
                  "  (kag.currentLabel == \"*viewtrans\" || "
                  "   kag.currentLabel == \"*view_rclick\")))"),
            &result);
        return result.operator bool();
    } catch(...) {
        return false;
    }
}

void TVPTraceTitleStateDiagnostics() {
    if(!TVPInputTraceEnabled())
        return;

    static int logged_count = 0;
    if(logged_count >= 8)
        return;
    logged_count++;

    TVPTraceExpressionValue("typeof_kag", TJS_W("typeof kag"));
    TVPTraceExpressionValue("typeof_global_kag", TJS_W("typeof global.kag"));
    TVPTraceExpressionValue("typeof_inTitleMenu", TJS_W("typeof inTitleMenu"));
    TVPTraceExpressionValue(
        "currentStorage",
        TJS_W("(typeof kag == \"Object\" && kag) ? kag.currentStorage : \"\""));
    TVPTraceExpressionValue(
        "currentLabel",
        TJS_W("(typeof kag == \"Object\" && kag) ? kag.currentLabel : \"\""));
    TVPTraceExpressionValue(
        "currentScenario",
        TJS_W("(typeof kag == \"Object\" && kag) ? kag.currentScenario : \"\""));
    TVPTraceExpressionValue(
        "currentConductor",
        TJS_W("(typeof kag == \"Object\" && kag) ? kag.conductor : \"\""));
    TVPTraceExpressionValue("typeof_SystemActionBase",
                            TJS_W("typeof SystemActionBase"));
}

bool TVPScriptReportsTitleMenu() {
    constexpr tjs_uint32 kCacheMs = 50;
    static tjs_uint32 cached_tick = 0;
    static bool cached_value = false;
    static bool cached_once = false;
    static bool logged_failure = false;

    const tjs_uint32 now = TVPGetRoughTickCount32();
    if(cached_once && static_cast<tjs_uint32>(now - cached_tick) < kCacheMs)
        return cached_value;

    cached_once = true;
    cached_tick = now;
    cached_value = false;

    try {
        TVPTraceTitleStateDiagnostics();
        tTJSVariant result;
        TVPExecuteExpression(
            TJS_W("typeof inTitleMenu == \"Object\" && "
                  "typeof kag == \"Object\" && inTitleMenu(kag)"),
            &result);
        cached_value = result.operator bool();
        if(TVPInputTraceEnabled()) {
            spdlog::info("LayerManager title state query result={}",
                         cached_value ? "true" : "false");
        }
    } catch(const eTJS &e) {
        if(TVPInputTraceEnabled() && !logged_failure) {
            spdlog::info("LayerManager title state query failed: {}",
                         ttstr(e.GetMessage()).AsStdString());
        }
        logged_failure = true;
    } catch(...) {
        if(TVPInputTraceEnabled() && !logged_failure)
            spdlog::info("LayerManager title state query failed");
        logged_failure = true;
    }

    return cached_value;
}

bool TVPIsTitleMenuBackgroundLayer(tTJSNI_BaseLayer *layer) {
    if(!layer)
        return false;
    const std::string name = layer->GetName().AsStdString();
    return name == "SysCoverLayer" || name == "title_bg";
}

std::string TVPTraceLayerImageSampleInfo(tTJSNI_BaseLayer *layer,
                                         bool force = false) {
    if(!TVPInputTraceEnabled() || !layer)
        return "-";

    if(const char *all = std::getenv("AETHERKIRI_INPUT_TRACE_ALL_IMAGES");
       all && *all && *all != '0')
        force = true;

    const std::string name = layer->GetName().AsStdString();
    if(!force && name.find("CG View Layer") == std::string::npos &&
       name.find("表メッセージレイヤ2") == std::string::npos)
        return "-";

    tTVPBaseTexture *image = nullptr;
    try {
        image = layer->GetMainImage();
    } catch(...) {
        return "get-image-failed";
    }
    if(!image)
        return "none";

    const int iw = static_cast<int>(image->GetWidth());
    const int ih = static_cast<int>(image->GetHeight());
    if(iw <= 0 || ih <= 0)
        return "empty";

    auto sample = [&](int x, int y) -> tjs_uint32 {
        x = std::clamp(x, 0, iw - 1);
        y = std::clamp(y, 0, ih - 1);
        try {
            return image->GetPoint(x, y);
        } catch(...) {
            return 0;
        }
    };

    int nonzero_alpha = 0;
    int total = 0;
    for(int gy = 0; gy < 8; ++gy) {
        for(int gx = 0; gx < 8; ++gx) {
            const int x = iw == 1 ? 0 : (gx * (iw - 1)) / 7;
            const int y = ih == 1 ? 0 : (gy * (ih - 1)) / 7;
            if((sample(x, y) & 0xff000000u) != 0)
                nonzero_alpha++;
            total++;
        }
    }

    const tjs_uint32 p00 = sample(0, 0);
    const tjs_uint32 center = sample(iw / 2, ih / 2);
    const tjs_uint32 pbr = sample(iw - 1, ih - 1);
    tjs_uint32 extra = 0;
    int extra_x = -1;
    int extra_y = -1;
    if(const char *sx = std::getenv("AETHERKIRI_INPUT_TRACE_SAMPLE_X");
       sx && *sx) {
        char *end = nullptr;
        extra_x = static_cast<int>(std::strtol(sx, &end, 10));
        if(!end || *end != '\0') extra_x = -1;
    }
    if(const char *sy = std::getenv("AETHERKIRI_INPUT_TRACE_SAMPLE_Y");
       sy && *sy) {
        char *end = nullptr;
        extra_y = static_cast<int>(std::strtol(sy, &end, 10));
        if(!end || *end != '\0') extra_y = -1;
    }
    if(extra_x >= 0 && extra_y >= 0)
        extra = sample(extra_x, extra_y);
    char buf[240];
    std::snprintf(buf, sizeof(buf),
                  "img=%dx%d ofs=%d,%d alpha=%d/%d p00=%08x center=%08x pbr=%08x sample(%d,%d)=%08x",
                  iw, ih, layer->GetImageLeft(), layer->GetImageTop(),
                  nonzero_alpha, total, p00, center, pbr, extra_x, extra_y,
                  extra);
    return buf;
}

std::string TVPTraceLayerChildrenInfo(tTJSNI_BaseLayer *layer) {
    if(!TVPInputTraceEnabled() || !layer)
        return "-";

    const std::string name = layer->GetName().AsStdString();
    const char *requested = std::getenv("AETHERKIRI_INPUT_TRACE_CHILDREN");
    const bool trace_all = requested && *requested && *requested != '0';
    if(!trace_all && name.find("CG View Layer") == std::string::npos)
        return "-";

    std::string result;
    const tjs_uint count = layer->GetCount();
    for(tjs_uint i = 0; i < count; ++i) {
        tTJSNI_BaseLayer *child = layer->GetChildren(static_cast<tjs_int>(i));
        if(!child)
            continue;
        if(!result.empty())
            result += " | ";
        result += "#";
        result += std::to_string(i);
        result += " ";
        result += child->GetName().AsStdString();
        result += " order=";
        result += std::to_string(child->GetOrderIndex());
        result += " pos=";
        result += std::to_string(child->GetLeft());
        result += ",";
        result += std::to_string(child->GetTop());
        result += " size=";
        result += std::to_string(child->GetWidth());
        result += "x";
        result += std::to_string(child->GetHeight());
        result += " vis=";
        result += child->GetVisible() ? "1" : "0";
        result += "/";
        result += child->GetNodeVisible() ? "1" : "0";
        result += " en=";
        result += child->GetNodeEnabled() ? "1" : "0";
        result += " opa=";
        result += std::to_string(child->GetOpacity());
        result += " has=";
        result += child->GetHasImage() ? "1" : "0";
        result += " owner=";
        result += child->GetOwnerNoAddRef() ? "1" : "0";
        result += " action=";
        result += child->GetActionOwnerNoAddRef().Object ? "1" : "0";
        result += " ";
        result += TVPTraceLayerImageSampleInfo(child, true);
    }
    return result.empty() ? "none" : result;
}

void TVPTraceLayerHit(const char *event, tjs_int x, tjs_int y,
                      tTJSNI_BaseLayer *layer) {
    if(!TVPInputTraceEnabled()) return;
    if(layer) {
        const auto action_owner = layer->GetActionOwnerNoAddRef();
        AETHER_INPUT_TRACE_LOG(
            "LayerManager %s hit primary=(%d,%d) layer=%s overall=%d rect=%dx%d+%d+%d visible=%d/%d enabled=%d action=%d",
            event, x, y, layer->GetName().AsStdString().c_str(),
            layer->GetOverallOrderIndex(), layer->GetWidth(),
            layer->GetHeight(), layer->GetLeft(), layer->GetTop(),
            layer->GetVisible() ? 1 : 0, layer->GetNodeVisible() ? 1 : 0,
            layer->GetNodeEnabled() ? 1 : 0, action_owner.Object ? 1 : 0);
        spdlog::info("LayerManager {} hit primary=({}, {}) layer={} overall={} rect={}x{}+{}+{} self_visible={} visible={} enabled={} action={}",
                     event, x, y, layer->GetName().AsStdString(),
                     layer->GetOverallOrderIndex(),
                     layer->GetWidth(), layer->GetHeight(), layer->GetLeft(),
                     layer->GetTop(), layer->GetVisible() ? "yes" : "no",
                     layer->GetNodeVisible() ? "yes" : "no",
                     layer->GetNodeEnabled() ? "yes" : "no",
                     action_owner.Object ? "yes" : "no");
    } else {
        AETHER_INPUT_TRACE_LOG("LayerManager %s hit primary=(%d,%d) layer=<none>",
                               event, x, y);
        spdlog::info("LayerManager {} hit primary=({}, {}) layer=<none>",
                     event, x, y);
    }
}

void TVPTraceLayersAt(tTVPLayerManager *manager, const char *reason,
                      tjs_int x, tjs_int y) {
    if(!TVPInputTraceEnabled() || !manager)
        return;
    spdlog::info("LayerManager layer dump reason={} primary=({}, {})", reason,
                 x, y);
    auto &nodes = manager->GetAllNodes();
    for(auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
        tTJSNI_BaseLayer *candidate = *it;
        if(!candidate)
            continue;
        tjs_int local_x = x;
        tjs_int local_y = y;
        candidate->FromPrimaryCoordinates(local_x, local_y);
        const bool rect_hit =
            local_x >= 0 && local_y >= 0 &&
            local_x < static_cast<tjs_int>(candidate->GetWidth()) &&
            local_y < static_cast<tjs_int>(candidate->GetHeight());
        if(!rect_hit)
            continue;
        const auto action_owner = candidate->GetActionOwnerNoAddRef();
        const bool pixel_hit =
            candidate->HitTestNoVisibleCheck(local_x, local_y);
        tTJSNI_BaseLayer *parent = candidate->GetParent();
        const std::string parent_name =
            parent ? parent->GetName().AsStdString() : std::string("<none>");
        const std::string image_samples =
            TVPTraceLayerImageSampleInfo(candidate);
        const std::string child_samples =
            TVPTraceLayerChildrenInfo(candidate);
        AETHER_INPUT_TRACE_LOG(
            "LayerManager stack %s layer=%s parent=%s order=%d/%d local=(%d,%d) size=%dx%d pos=(%d,%d) vis=%d/%d en=%d opacity=%d pixel=%d action=%d",
            reason, candidate->GetName().AsStdString().c_str(),
            parent_name.c_str(), candidate->GetOrderIndex(),
            candidate->GetOverallOrderIndex(), local_x, local_y,
            candidate->GetWidth(), candidate->GetHeight(),
            candidate->GetLeft(), candidate->GetTop(),
            candidate->GetVisible() ? 1 : 0,
            candidate->GetNodeVisible() ? 1 : 0,
            candidate->GetNodeEnabled() ? 1 : 0, candidate->GetOpacity(),
            pixel_hit ? 1 : 0, action_owner.Object ? 1 : 0);
        spdlog::info("  layer={} parent={} order={} overall={} local=({}, {}) size={}x{} pos=({}, {}) type={} display_type={} has_image={} image_samples={} child_samples={} children={} visible_children={} in_transition={} trans_children={} self_visible={} visible={} enabled={} opacity={} pixel={} action={}",
                     candidate->GetName().AsStdString(), parent_name,
                     candidate->GetOrderIndex(),
                     candidate->GetOverallOrderIndex(), local_x, local_y,
                     candidate->GetWidth(), candidate->GetHeight(),
                     candidate->GetLeft(), candidate->GetTop(),
                     ttstr(candidate->GetTypeNameString()).AsStdString(),
                     static_cast<int>(candidate->DebugGetDisplayType()),
                     candidate->GetHasImage() ? "yes" : "no",
                     image_samples,
                     child_samples,
                     candidate->GetCount(),
                     candidate->DebugGetVisibleChildrenCount(),
                     candidate->DebugIsInTransition() ? "yes" : "no",
                     candidate->DebugIsTransWithChildren() ? "yes" : "no",
                     candidate->GetVisible() ? "yes" : "no",
                     candidate->GetNodeVisible() ? "yes" : "no",
                     candidate->GetNodeEnabled() ? "yes" : "no",
                     candidate->GetOpacity(), pixel_hit ? "yes" : "no",
                     action_owner.Object ? "yes" : "no");
    }
}

void TVPTraceCgModeViewTransIdle(tTVPLayerManager *manager, tjs_int x,
                                 tjs_int y) {
    if(!TVPInputTraceEnabled() || !manager)
        return;
    if(!TVPScriptIsCgModeViewTrans())
        return;

    static int logged_count = 0;
    static tjs_uint32 last_tick = 0;
    const tjs_uint32 now = TVPGetRoughTickCount32();
    if(logged_count >= 16)
        return;
    if(logged_count > 0 && static_cast<tjs_uint32>(now - last_tick) < 250)
        return;

    logged_count++;
    last_tick = now;
    TVPTraceLayersAt(manager, "cgmode-viewtrans-idle", x, y);
}

bool TVPIsSaveLoadButtonLayer(tTJSNI_BaseLayer *layer) {
    if(!layer) return false;
    const std::string name = layer->GetName().AsStdString();
    return name == "save" || name == "load" || name == "qload" ||
           name == "back" || name == "return" || name == "yes" ||
           name == "no" || name == "to_save" || name == "to_load" ||
           name == "to_qsave" || name == "to_qload" ||
           name == "to_back" || name == "to_return";
}

bool TVPIsSaveLoadOverlayCommandLayer(tTJSNI_BaseLayer *layer) {
    if(!layer)
        return false;
    const std::string name = layer->GetName().AsStdString();
    return name == "to_save" || name == "to_load" ||
           name == "to_qsave" || name == "to_qload" ||
           name == "to_back" || name == "to_return";
}

bool TVPLayerRectContainsPrimaryPoint(tTJSNI_BaseLayer *layer, tjs_int x,
                                      tjs_int y, tjs_int &local_x,
                                      tjs_int &local_y) {
    if(!layer)
        return false;
    local_x = x;
    local_y = y;
    layer->FromPrimaryCoordinates(local_x, local_y);
    return local_x >= 0 && local_y >= 0 &&
           local_x < static_cast<tjs_int>(layer->GetWidth()) &&
           local_y < static_cast<tjs_int>(layer->GetHeight());
}

bool TVPIsActiveMotionButtonLayer(tTJSNI_BaseLayer *layer) {
    if(!layer || !layer->GetNodeVisible() || !layer->GetNodeEnabled())
        return false;

    iTJSDispatch2 *owner = layer->GetOwnerNoAddRef();
    if(!owner)
        return false;

    static ttstr motion_working_name(TJS_W("motionWorking"));
    tTJSVariant motion_working;
    if(TJS_FAILED(owner->PropGet(0, motion_working_name.c_str(),
                                 motion_working_name.GetHint(),
                                 &motion_working, owner)) ||
       !motion_working.operator bool()) {
        return false;
    }

    static ttstr motion_buttons_name(TJS_W("_motionButtons"));
    tTJSVariant motion_buttons_value;
    if(TJS_FAILED(owner->PropGet(0, motion_buttons_name.c_str(),
                                 motion_buttons_name.GetHint(),
                                 &motion_buttons_value, owner)) ||
       motion_buttons_value.Type() != tvtObject) {
        return false;
    }

    const tTJSVariantClosure motion_buttons =
        motion_buttons_value.AsObjectClosureNoAddRef();
    if(!motion_buttons.Object)
        return false;

    static ttstr count_name(TJS_W("count"));
    tTJSVariant count;
    return TJS_SUCCEEDED(motion_buttons.PropGet(
               0, count_name.c_str(), count_name.GetHint(), &count,
               nullptr)) &&
           count.AsInteger() > 0;
}

tTJSNI_BaseLayer *TVPFindMotionButtonOwnerForDisplayProxy(
    tTJSNI_BaseLayer *layer, tjs_int x, tjs_int y) {
    tTJSNI_BaseLayer *proxy = layer;
    while(proxy && proxy->GetName().IsEmpty()) {
        tTJSNI_BaseLayer *parent = proxy->GetParent();
        if(!parent || proxy->GetLeft() != 0 || proxy->GetTop() != 0 ||
           proxy->GetWidth() != parent->GetWidth() ||
           proxy->GetHeight() != parent->GetHeight()) {
            break;
        }

        if(TVPIsActiveMotionButtonLayer(parent)) {
            tjs_int local_x = 0;
            tjs_int local_y = 0;
            if(TVPLayerRectContainsPrimaryPoint(parent, x, y, local_x,
                                                 local_y) &&
               parent->HitTestNoVisibleCheck(local_x, local_y)) {
                if(TVPInputTraceEnabled()) {
                    spdlog::info(
                        "LayerManager route display proxy to motion owner primary=({}, {}) proxy={} owner={}",
                        x, y, proxy->GetName().AsStdString(),
                        parent->GetName().AsStdString());
                }
                return parent;
            }
        }
        proxy = parent;
    }
    return layer;
}

tTJSNI_BaseLayer *TVPRoutePassiveKagPresentationProxyToPage(
    tTJSNI_BaseLayer *hit, tjs_int x, tjs_int y) {
    if(!hit || !hit->GetName().IsEmpty())
        return hit;

    tTJSNI_BaseLayer *presentation = hit->GetParent();
    if(!presentation || presentation->GetName().IsEmpty() ||
       presentation->GetCount() != 1 ||
       TVPIsActiveMotionButtonLayer(presentation) ||
       hit->GetLeft() != 0 || hit->GetTop() != 0 ||
       hit->GetWidth() != presentation->GetWidth() ||
       hit->GetHeight() != presentation->GetHeight()) {
        return hit;
    }

    tTJSNI_BaseLayer *page = presentation->GetParent();
    if(!page || !page->GetNodeVisible() || !page->GetNodeEnabled() ||
       presentation->GetLeft() != 0 || presentation->GetTop() != 0 ||
       presentation->GetWidth() != page->GetWidth() ||
       presentation->GetHeight() != page->GetHeight()) {
        return hit;
    }

    // KAG EX mounts stand/E-mote characters as a named layer with one
    // unnamed, full-page display proxy below the foreground page (for
    // example `<unnamed> -> ショコラ -> 表-背景`, child to parent).  The
    // proxy is only a presentation surface; allowing its opaque pixels to own
    // pointer input prevents the foreground page's click-to-advance handler
    // from running.
    // The wrapper normally starts as ltBinder, but affine transitions can
    // replace it with an ltAlpha presentation layer while retaining the same
    // passive proxy structure. Restrict the structural fallback to KAG
    // page-background names so title/gallery controls remain interactive.
    const std::string page_name = page->GetName().AsStdString();
    if(page_name.find("背景") == std::string::npos)
        return hit;

    tjs_int local_x = 0;
    tjs_int local_y = 0;
    if(!TVPLayerRectContainsPrimaryPoint(page, x, y, local_x, local_y) ||
       !page->HitTestNoVisibleCheck(local_x, local_y)) {
        return hit;
    }

    if(TVPInputTraceEnabled()) {
        spdlog::info(
            "LayerManager pass passive KAG presentation proxy to page primary=({}, {}) presentation={} page={}",
            x, y, presentation->GetName().AsStdString(), page_name);
    }
    return page;
}

bool TVPIsCgPreviewPresentationLayer(tTJSNI_BaseLayer *layer) {
    if(!layer)
        return false;
    const std::string name = layer->GetName().AsStdString();
    return name.find("CG View Layer :") != std::string::npos;
}

bool TVPLayerImageAlphaHit(tTJSNI_BaseLayer *layer, tjs_int local_x,
                           tjs_int local_y) {
    if(!layer || !layer->GetHasImage())
        return false;
    tTVPBaseTexture *image = nullptr;
    try {
        image = layer->GetMainImage();
    } catch(...) {
        return false;
    }
    if(!image)
        return false;

    const tjs_int px = local_x - layer->GetImageLeft();
    const tjs_int py = local_y - layer->GetImageTop();
    if(px < 0 || py < 0 || px >= static_cast<tjs_int>(image->GetWidth()) ||
       py >= static_cast<tjs_int>(image->GetHeight())) {
        return false;
    }

    try {
        return (image->GetPoint(px, py) & 0xff000000u) != 0;
    } catch(...) {
        return false;
    }
}

tTJSNI_BaseLayer *TVPFindCgPreviewLayerAt(tTVPLayerManager *manager, tjs_int x,
                                          tjs_int y) {
    if(!manager || !TVPScriptIsCgPreviewLoop())
        return nullptr;

    auto &nodes = manager->GetAllNodes();
    for(auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
        tTJSNI_BaseLayer *candidate = *it;
        if(!TVPIsCgPreviewPresentationLayer(candidate) ||
           !candidate->GetVisible() || !candidate->GetNodeVisible() ||
           !candidate->GetNodeEnabled()) {
            continue;
        }

        tjs_int local_x = 0;
        tjs_int local_y = 0;
        if(!TVPLayerRectContainsPrimaryPoint(candidate, x, y, local_x,
                                             local_y)) {
            continue;
        }
        if(!TVPLayerImageAlphaHit(candidate, local_x, local_y)) {
            continue;
        }

        if(TVPInputTraceEnabled()) {
            const auto action_owner = candidate->GetActionOwnerNoAddRef();
            spdlog::info(
                "LayerManager cg preview presentation hit primary=({}, {}) layer={} local=({}, {}) visible={} parentVisible={} enabled={} action={}",
                x, y, candidate->GetName().AsStdString(), local_x, local_y,
                candidate->GetVisible() ? "yes" : "no",
                candidate->GetNodeVisible() ? "yes" : "no",
                candidate->GetNodeEnabled() ? "yes" : "no",
                action_owner.Object ? "yes" : "no");
        }
        return candidate;
    }
    return nullptr;
}

bool TVPIsSaveLoadItemLayer(tTJSNI_BaseLayer *layer) {
    if(!layer) return false;
    const std::string name = layer->GetName().AsStdString();
    if(name.rfind("item", 0) != 0)
        return false;
    const tjs_int width = layer->GetWidth();
    const tjs_int height = layer->GetHeight();
    if(width >= 300 && height >= 80)
        return true;
    // CafeStella's save slots are compact cards such as item00 at 259x250.
    return width >= 220 && width <= 300 && height >= 180 && height <= 300;
}

bool TVPIsGalleryItemLayer(tTJSNI_BaseLayer *layer) {
    if(!layer) return false;
    const std::string name = layer->GetName().AsStdString();
    if(name.size() < 5 || name.rfind("item", 0) != 0)
        return false;
    for(size_t i = 4; i < name.size(); ++i) {
        if(!std::isdigit(static_cast<unsigned char>(name[i])))
            return false;
    }
    // Scene-gallery sheets are authored at more than one scale.  The
    // compact layouts used by most titles are around 240x140, while
    // drciot's replay sheet uses 460x271 cells.  Keep the predicate bounded
    // so arbitrary full-screen `itemNN` layers are not treated as gallery
    // controls, but accept both authored ranges.
    return layer->GetWidth() >= 120 && layer->GetWidth() <= 640 &&
        layer->GetHeight() >= 80 && layer->GetHeight() <= 360;
}

bool TVPIsConfirmableSelectionLayer(tTJSNI_BaseLayer *layer) {
    return TVPIsSaveLoadItemLayer(layer) || TVPIsGalleryItemLayer(layer);
}

bool TVPIsMessageLayer(tTJSNI_BaseLayer *layer) {
    if(!layer) return false;
    return layer->GetName().AsStdString().find("メッセージ") !=
           std::string::npos;
}

bool TVPIsScriptInstanceOf(tTJSNI_BaseLayer *layer,
                           const tjs_char *class_name) {
    if(!layer || !class_name)
        return false;
    iTJSDispatch2 *owner = layer->GetOwnerNoAddRef();
    return owner &&
           owner->IsInstanceOf(0, nullptr, nullptr, class_name, owner) ==
               TJS_S_TRUE;
}

tTJSNI_BaseLayer *TVPFindAffinePresentationAncestor(
    tTJSNI_BaseLayer *layer) {
    // KAG's AffineLayer (and EnvGraphicLayer subclasses) is a presentation
    // surface.  It deliberately has no pointer handlers of its own, but its
    // opaque character pixels still win the native Layer hit test.
    for(tTJSNI_BaseLayer *candidate = layer; candidate;
        candidate = candidate->GetParent()) {
        if(TVPIsScriptInstanceOf(candidate, TJS_W("AffineLayer")))
            return candidate;
    }
    return nullptr;
}

bool TVPIsInLayerSubtree(tTJSNI_BaseLayer *layer,
                         tTJSNI_BaseLayer *root) {
    for(tTJSNI_BaseLayer *candidate = layer; candidate;
        candidate = candidate->GetParent()) {
        if(candidate == root)
            return true;
    }
    return false;
}

tTJSNI_BaseLayer *TVPRouteAffinePresentationToMessageLayer(
    tTVPLayerManager *manager, tTJSNI_BaseLayer *hit, tjs_int x, tjs_int y) {
    if(!manager || !hit)
        return hit;

    tTJSNI_BaseLayer *presentation =
        TVPFindAffinePresentationAncestor(hit);
    if(!presentation)
        return hit;

    // Motion-backed GUI layers are real controls.  They are normally routed
    // before this helper, but keep the guard here for nested display proxies.
    if(TVPIsActiveMotionButtonLayer(presentation))
        return hit;

    // Find the front-most live message layer behind the character subtree.
    // MessageLayer handles click-to-advance in onMouseDown, so routing only
    // the synthetic onClick event would still leave the game stuck.
    auto &nodes = manager->GetAllNodes();
    for(auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
        tTJSNI_BaseLayer *candidate = *it;
        if(!candidate || TVPIsInLayerSubtree(candidate, presentation) ||
           !TVPIsMessageLayer(candidate) || !candidate->GetNodeVisible() ||
           !candidate->GetNodeEnabled()) {
            continue;
        }

        tjs_int local_x = 0;
        tjs_int local_y = 0;
        if(!TVPLayerRectContainsPrimaryPoint(candidate, x, y, local_x,
                                             local_y) ||
           !candidate->HitTestNoVisibleCheck(local_x, local_y)) {
            continue;
        }

        if(TVPInputTraceEnabled()) {
            spdlog::info(
                "LayerManager pass affine presentation through to message primary=({}, {}) presentation={} hit={} message={}",
                x, y, presentation->GetName().AsStdString(),
                hit->GetName().AsStdString(),
                candidate->GetName().AsStdString());
        }
        return candidate;
    }

    return hit;
}
}

//---------------------------------------------------------------------------
// tTVPLayerManager
//---------------------------------------------------------------------------
tTVPDestTexture::tTVPDestTexture(tjs_uint w, tjs_uint h)
    : tTVPBaseTexture(w, h) {
    // This texture is the layer-manager's readback-visible composition
    // surface. GPU backends can keep aliased KAG blits on the software path
    // while leaving ordinary layer textures fast.
    if(Bitmap != nullptr) {
        Bitmap->SetCpuCompositeTarget(true);
    }
}

tTVPLayerManager::tTVPLayerManager(iTVPLayerTreeOwner *owner) {
    RefCount = 1;
    LayerTreeOwner = owner;
    DrawDeviceData = nullptr;
    DrawBuffer = nullptr;
    DesiredLayerType = ltOpaque;

    CaptureOwner = nullptr;
    LastMouseMoveSent = nullptr;
    Primary = nullptr;
    FocusedLayer = nullptr;
    OverallOrderIndexValid = false;
    EnabledWorkRefCount = 0;
    FocusChangeLock = false;
    VisualStateChanged = true;
    LastMouseMoveX = -1;
    LastMouseMoveY = -1;
    InNotifyingHintOrCursorChange = false;
}
//---------------------------------------------------------------------------
tTVPLayerManager::~tTVPLayerManager() {
    if(DrawBuffer)
        delete DrawBuffer;
}
//---------------------------------------------------------------------------
void tTVPLayerManager::AddRef() { RefCount++; }
//---------------------------------------------------------------------------
void tTVPLayerManager::Release() {
    if(RefCount == 1)
        delete this;
    else
        RefCount--;
}
//---------------------------------------------------------------------------
void tTVPLayerManager::RegisterSelfToWindow() {
    LayerTreeOwner->RegisterLayerManager(this);
}
//---------------------------------------------------------------------------
void tTVPLayerManager::UnregisterSelfFromWindow() {
    LayerTreeOwner->UnregisterLayerManager(this);
}

void tTVPLayerManager::SetHoldAlpha(bool b) {
    HoldAlpha = b;
    if(!DrawBuffer)
        return;
    static_cast<tTVPDestTexture *>(DrawBuffer)->SetHoldAlpha(b);
}

void tTVPLayerManager::ClearDrawBufferForAlpha() {
    if(!DrawBuffer || HoldAlpha)
        return;
    tjs_int width = 0;
    tjs_int height = 0;
    if(!GetPrimaryLayerSize(width, height) || width <= 0 || height <= 0)
        return;
    DrawBuffer->Fill(tTVPRect(0, 0, width, height), 0x00000000u);
}

tTVPBaseTexture *tTVPLayerManager::EnsureDrawBufferSize(
    tjs_int w, tjs_int h, bool clear_on_resize) {
    if(w <= 0 || h <= 0)
        return DrawBuffer;

    // The compositor can expose untouched pixels while a transparent child
    // only updates part of the primary surface. KiriKiri defines those pixels
    // from the opaque primary layer's neutral color; a hard-coded black clear
    // leaks a black edge through otherwise valid transparent title artwork.
    const tjs_uint32 neutral_color =
        Primary ? (Primary->GetNeutralColor() & 0x00ffffffu) : 0;
    const tjs_uint32 clear_color = HoldAlpha ? neutral_color | 0xff000000u
                                             : neutral_color;

    const tjs_uint target_w = static_cast<tjs_uint>(w);
    const tjs_uint target_h = static_cast<tjs_uint>(h);
    if(!DrawBuffer) {
        DrawBuffer = new tTVPDestTexture(target_w, target_h);
        DrawBuffer->Fill(tTVPRect(0, 0, w, h), clear_color);
        static_cast<tTVPDestTexture *>(DrawBuffer)->SetHoldAlpha(HoldAlpha);
        return DrawBuffer;
    }

    if(DrawBuffer->GetWidth() != target_w ||
       DrawBuffer->GetHeight() != target_h) {
        DrawBuffer->SetSize(target_w, target_h, !clear_on_resize);
        if(clear_on_resize)
            DrawBuffer->Fill(tTVPRect(0, 0, w, h), clear_color);
        static_cast<tTVPDestTexture *>(DrawBuffer)->SetHoldAlpha(HoldAlpha);
    }
    return DrawBuffer;
}

tTVPBaseTexture *tTVPLayerManager::EnsureDrawBufferMatchesPrimary(
    bool clear_on_resize) {
    tjs_int w = 0, h = 0;
    if(!GetPrimaryLayerSize(w, h))
        return DrawBuffer;
    return EnsureDrawBufferSize(w, h, clear_on_resize);
}

//---------------------------------------------------------------------------
tTVPBaseTexture *tTVPLayerManager::GetDrawTargetBitmap(const tTVPRect &rect,
                                                       tTVPRect &cliprect) {
    // retrieve draw target bitmap
    tjs_int w = rect.get_width();
    tjs_int h = rect.get_height();

    if(Primary) {
        const tTVPRect &rc = Primary->GetRect();
        w = rc.get_width();
        h = rc.get_height();
    }
    EnsureDrawBufferSize(w, h, false);

    cliprect = rect;
    return DrawBuffer;
}
//---------------------------------------------------------------------------
tTVPLayerType tTVPLayerManager::GetTargetLayerType() {
    return DesiredLayerType;
}
//---------------------------------------------------------------------------
void tTVPLayerManager::DrawCompleted(const tTVPRect &destrect,
                                     tTVPBaseTexture *bmp,
                                     const tTVPRect &cliprect,
                                     tTVPLayerType type, tjs_int opacity) {
#if 0
	if (!LayerTreeOwner) return;
	LayerTreeOwner->NotifyBitmapCompleted(this, destrect.left, destrect.top, bmp, cliprect, type, opacity);
#else
    tjs_int w, h;
    if(!/*LayerTreeOwner->*/ GetPrimaryLayerSize(w, h))
        return;
    // Window->GetDrawDevice()->GetSrcSize(w, h);
    EnsureDrawBufferSize(w, h, false);

    // The Godot path composes each completion into DrawBuffer before it is
    // presented.  BitmapLayerTreeOwner is also used as a drawable owner by
    // LayerOwnerTexture (for message glyphs), so it still needs the legacy
    // completion notification; otherwise its bitmap remains unchanged and
    // the script-side Texture never receives the newly rendered text.
    auto notify_bitmap_owner = [&]() {
        if(auto *bitmap_owner =
               dynamic_cast<tTJSNI_BitmapLayerTreeOwner *>(LayerTreeOwner)) {
            bitmap_owner->NotifyBitmapCompleted(
                this, destrect.left, destrect.top, bmp, cliprect, type,
                opacity);
        }
    };

    if(const char *trace = std::getenv("AETHERKIRI_MESSAGE_FRAME_COMPOSE");
       trace && *trace && *trace != '0' && type == ltAlpha && bmp &&
       destrect.get_width() >= 1000 && destrect.get_height() >= 400) {
        const tjs_int sx = std::clamp(
            cliprect.left + cliprect.get_width() / 2, 0,
            static_cast<tjs_int>(bmp->GetWidth()) - 1);
        const tjs_int sy = std::clamp(
            cliprect.top + cliprect.get_height() / 2, 0,
            static_cast<tjs_int>(bmp->GetHeight()) - 1);
        const tjs_int dx = std::clamp(
            destrect.left + destrect.get_width() / 2, 0,
            static_cast<tjs_int>(DrawBuffer->GetWidth()) - 1);
        const tjs_int dy = std::clamp(
            destrect.top + destrect.get_height() / 2, 0,
            static_cast<tjs_int>(DrawBuffer->GetHeight()) - 1);
        spdlog::info(
            "message-frame compose type={} opacity={} dest=({},{} {}x{}) "
            "clip=({},{} {}x{}) src=0x{:08x} dst_before=0x{:08x}",
            static_cast<int>(type), opacity, destrect.left, destrect.top,
            destrect.get_width(), destrect.get_height(), cliprect.left,
            cliprect.top, cliprect.get_width(), cliprect.get_height(),
            bmp->GetPoint(sx, sy), DrawBuffer->GetPoint(dx, dy));
        DrawBuffer->Blt(destrect.left, destrect.top, bmp, cliprect, type,
                        opacity, HoldAlpha);
        spdlog::info("message-frame compose dst_after=0x{:08x}",
                     DrawBuffer->GetPoint(dx, dy));
        notify_bitmap_owner();
        return;
    }

    // A D2D manager is an alpha-preserving composition surface even when
    // one of its source layers advertises ltOpaque.  The generic layer-type
    // overload maps that combination to CopyOpaqueImage, which deliberately
    // forces alpha to 255; an untouched/transparent opaque layer then becomes
    // opaque black and covers a lower manager (the RPG map in particular).
    // Copy the source pixels verbatim for this manager so zero-alpha regions
    // stay transparent while genuinely opaque pixels remain opaque.
    if(!HoldAlpha && DesiredLayerType == ltAlpha && type == ltOpaque) {
        DrawBuffer->Blt(destrect.left, destrect.top, bmp, cliprect, bmCopy,
                         opacity, false);
    } else {
        DrawBuffer->Blt(destrect.left, destrect.top, bmp, cliprect, type,
                         opacity, HoldAlpha);
    }
    notify_bitmap_owner();
#endif
}

tTVPBaseTexture *tTVPLayerManager::GetOrCreateDrawBuffer() {
    return EnsureDrawBufferMatchesPrimary(false);
}

//---------------------------------------------------------------------------
void tTVPLayerManager::AttachPrimary(tTJSNI_BaseLayer *pri) {
    // attach primary layer to the manager
    DetachPrimary();

    if(!Primary) {
        Primary = pri;
        EnabledWorkRefCount = 0;
        OverallOrderIndexValid = false;
        UpdateRegion.Clear();
        pri->SetVisible(true);
        pri->SetOpacity(255);
    }
}
//---------------------------------------------------------------------------
void tTVPLayerManager::DetachPrimary() {
    // detach primary layer from the manager
    if(Primary) {
        SetFocusTo(nullptr);
        ReleaseCapture();
        ReleaseTouchCaptureAll();
        ForceMouseLeave();
        NotifyPart(Primary);
        Primary = nullptr;
    }
}
//---------------------------------------------------------------------------
bool tTVPLayerManager::GetPrimaryLayerSize(tjs_int &w, tjs_int &h) const {
    if(IsPrimaryLayerAttached()) {
        w = Primary->GetWidth();
        h = Primary->GetHeight();
        return true;
    } else {
        return false;
    }
}
//---------------------------------------------------------------------------
void tTVPLayerManager::NotifyPart(tTJSNI_BaseLayer *lay) {
    // notifies layer parting from its parent
    InvalidateOverallIndex();
    BlurTree(lay);
    ReleaseCaptureFromTree(lay);
}
//---------------------------------------------------------------------------
void tTVPLayerManager::InvalidateOverallIndex() {
    OverallOrderIndexValid = false;
}
//---------------------------------------------------------------------------
void tTVPLayerManager::RecreateOverallOrderIndex() {
    // recreate overall order index
    if(!OverallOrderIndexValid) {
        tjs_uint index = 0;
        AllNodes.clear();
        if(Primary)
            Primary->RecreateOverallOrderIndex(index, AllNodes);

        OverallOrderIndexValid = true;
    }
}
//---------------------------------------------------------------------------
std::vector<tTJSNI_BaseLayer *> &tTVPLayerManager::GetAllNodes() {
    if(!OverallOrderIndexValid)
        RecreateOverallOrderIndex();
    return AllNodes;
}
//---------------------------------------------------------------------------
void tTVPLayerManager::QueryUpdateExcludeRect() {
    if(!VisualStateChanged)
        return;
    tTVPRect r;
    r.clear();
    if(Primary)
        Primary->QueryUpdateExcludeRect(r, true);
    VisualStateChanged = false;
}
//---------------------------------------------------------------------------
void tTVPLayerManager::NotifyMouseCursorChange(tTJSNI_BaseLayer *layer,
                                               tjs_int cursor) {
    if(InNotifyingHintOrCursorChange)
        return;

    InNotifyingHintOrCursorChange = true;
    try {

        tTJSNI_BaseLayer *l;

        if(CaptureOwner)
            l = CaptureOwner;
        else
            l = GetMostFrontChildAt(LastMouseMoveX, LastMouseMoveY);

        if(l == layer)
            SetMouseCursor(cursor);
    } catch(...) {
        InNotifyingHintOrCursorChange = false;
        throw;
    }

    InNotifyingHintOrCursorChange = false;
}
//---------------------------------------------------------------------------
void tTVPLayerManager::SetMouseCursor(tjs_int cursor) {
    if(!LayerTreeOwner)
        return;

    LayerTreeOwner->SetMouseCursor(this, cursor);
}
//---------------------------------------------------------------------------
void tTVPLayerManager::GetCursorPos(tjs_int &x, tjs_int &y) {
    if(!LayerTreeOwner)
        return;
    LayerTreeOwner->GetCursorPos(this, x, y);
}
//---------------------------------------------------------------------------
void tTVPLayerManager::SetCursorPos(tjs_int x, tjs_int y) {
    if(!LayerTreeOwner)
        return;
    LayerTreeOwner->SetCursorPos(this, x, y);
}
//---------------------------------------------------------------------------
void tTVPLayerManager::NotifyHintChange(tTJSNI_BaseLayer *layer,
                                        const ttstr &hint) {
    if(InNotifyingHintOrCursorChange)
        return;

    InNotifyingHintOrCursorChange = true;

    try {
        tTJSNI_BaseLayer *l;

        if(CaptureOwner)
            l = CaptureOwner;
        else
            l = GetMostFrontChildAt(LastMouseMoveX, LastMouseMoveY);

        if(l == layer)
            SetHint(l->GetOwnerNoAddRef(), hint);
    } catch(...) {
        InNotifyingHintOrCursorChange = false;
        throw;
    }

    InNotifyingHintOrCursorChange = false;
}
//---------------------------------------------------------------------------
void tTVPLayerManager::SetHint(iTJSDispatch2 *sender, const ttstr &hint) {
    if(!LayerTreeOwner)
        return;
    LayerTreeOwner->SetHint(this, sender, hint);
}
//---------------------------------------------------------------------------
void tTVPLayerManager::NotifyLayerResize() {
    // notifies layer resizing to the LayerTreeOwner
    if(!LayerTreeOwner)
        return;

    tjs_int w = 0, h = 0;
    if(GetPrimaryLayerSize(w, h) && w > 0 && h > 0) {
        EnsureDrawBufferSize(w, h, true);
        UpdateRegion.Clear();
        UpdateRegion.Or(tTVPRect(0, 0, w, h));
    }

    LayerTreeOwner->NotifyLayerResize(this);
    NotifyWindowInvalidation();
}
//---------------------------------------------------------------------------
void tTVPLayerManager::NotifyWindowInvalidation() {
    // notifies layer surface is invalidated and should be transfered
    // to LayerTreeOwner.
    if(!LayerTreeOwner)
        return;

    LayerTreeOwner->NotifyLayerImageChange(this);
    // TODO atlernative of LayerTreeOwner->RequestUpdate();
}
//---------------------------------------------------------------------------
void tTVPLayerManager::SetLayerTreeOwner(class iTVPLayerTreeOwner *owner) {
    // sets LayerTreeOwner
    LayerTreeOwner = owner;
}
//---------------------------------------------------------------------------
void tTVPLayerManager::NotifyResizeFromWindow(tjs_uint w, tjs_uint h) {
    // is called by the owner window, notifies windows's client area
    // size has been changed. does not be called if owner window's
    // "autoResize" property is false.

    // currently this function is not used.

    if(Primary)
        Primary->InternalSetSize(w, h);
}
//---------------------------------------------------------------------------
tTJSNI_BaseLayer *tTVPLayerManager::GetMostFrontChildAt(
    tjs_int x, tjs_int y, tTJSNI_BaseLayer *except, bool get_disabled) {
    // return most front layer at given point.
    // this does checking of layer's visibility.
    // x and y are given in primary layer's coordinates.
    if(!Primary)
        return nullptr;

    tTJSNI_BaseLayer *lay = nullptr;
    Primary->GetMostFrontChildAt(x, y, &lay, except, get_disabled);
    return lay;
}
//---------------------------------------------------------------------------
tTJSNI_BaseLayer *tTVPLayerManager::GetClickableLayerAt(tjs_int x, tjs_int y) {
    tTJSNI_BaseLayer *layer = GetMostFrontChildAt(x, y);
    // Do not globally prioritize a full-size CG presentation layer here.
    // Gallery controls can be composited over that layer and must retain the
    // normal front-to-back hit result for press, hover, click, and drag.  The
    // preview fallback is intentionally applied only to right-click release
    // in PrimaryMouseUp, where it is needed to close a full-screen preview.
    layer = TVPFindMotionButtonOwnerForDisplayProxy(layer, x, y);
    layer = TVPRoutePassiveKagPresentationProxyToPage(layer, x, y);
    layer = TVPRouteAffinePresentationToMessageLayer(this, layer, x, y);

    if(TVPIsSaveLoadItemLayer(layer))
        TVPTraceLayersAt(this, "save-load-item", x, y);
    if(!layer || !TVPIsMessageLayer(layer) || !Primary)
        return layer;

    const tjs_int lower_control_band = (tjs_int)(Primary->GetHeight() * 3 / 4);
    if(y < lower_control_band)
        return layer;
    const bool message_command_band = IsSaveLoadMessageCommandBand(layer, x, y);
    if(message_command_band)
        TVPTraceLayersAt(this, "message-command-band", x, y);

    tTJSNI_BaseLayer *under = GetMostFrontChildAt(x, y, layer);
    if(TVPInputTraceEnabled() && under) {
        spdlog::info("LayerManager passthrough candidate top={} under={}",
                     layer->GetName().AsStdString(),
                     under->GetName().AsStdString());
    }
    if(TVPIsSaveLoadButtonLayer(under))
        return under;

    auto &nodes = GetAllNodes();
    if(message_command_band) {
        for(auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
            tTJSNI_BaseLayer *candidate = *it;
            if(!TVPIsSaveLoadOverlayCommandLayer(candidate) ||
               !candidate->GetNodeVisible()) {
                continue;
            }
            tjs_int local_x = 0;
            tjs_int local_y = 0;
            if(!TVPLayerRectContainsPrimaryPoint(candidate, x, y, local_x,
                                                 local_y)) {
                continue;
            }
            const bool pixel_hit =
                candidate->HitTestNoVisibleCheck(local_x, local_y);
            if(TVPInputTraceEnabled()) {
                spdlog::info(
                    "LayerManager save/load overlay command through message={} enabled={} pixel={}",
                    candidate->GetName().AsStdString(),
                    candidate->GetNodeEnabled() ? "yes" : "no",
                    pixel_hit ? "yes" : "no");
            }
            const auto action_owner = candidate->GetActionOwnerNoAddRef();
            if(!candidate->GetNodeEnabled() && !pixel_hit &&
               !action_owner.Object)
                continue;
            return candidate;
        }
    }

    for(auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
        tTJSNI_BaseLayer *candidate = *it;
        if(!TVPIsSaveLoadButtonLayer(candidate) || !candidate->GetNodeVisible() ||
           !candidate->GetNodeEnabled()) {
            continue;
        }
        tjs_int local_x = x;
        tjs_int local_y = y;
        candidate->FromPrimaryCoordinates(local_x, local_y);
        if(candidate->HitTestNoVisibleCheck(local_x, local_y)) {
            if(TVPInputTraceEnabled()) {
                spdlog::info("LayerManager save/load candidate through message={}",
                             candidate->GetName().AsStdString());
            }
            return candidate;
        }
    }
    return layer;
}

bool tTVPLayerManager::IsPendingConfirmStillOnSameSelection() {
    return GetPendingConfirmSelectionLayer() != nullptr;
}

tTJSNI_BaseLayer *tTVPLayerManager::GetPendingConfirmSelectionLayer() {
    if(!Primary || PendingConfirmLayerName.empty())
        return nullptr;

    tTJSNI_BaseLayer *layer =
        GetConfirmableSelectionLayerAt(PendingConfirmX, PendingConfirmY);
    if(!TVPIsConfirmableSelectionLayer(layer))
        return nullptr;
    if(layer->GetName().AsStdString() != PendingConfirmLayerName)
        return nullptr;
    return layer;
}

tTJSNI_BaseLayer *tTVPLayerManager::GetConfirmableSelectionLayerAt(
    tjs_int x, tjs_int y) {
    auto &nodes = GetAllNodes();
    int scanned = 0;
    auto inspect_candidate = [&](tTJSNI_BaseLayer *candidate,
                                 bool require_pixel_hit) -> tTJSNI_BaseLayer * {
        if(!TVPIsConfirmableSelectionLayer(candidate))
            return nullptr;
        if(!candidate->GetNodeVisible() || !candidate->GetNodeEnabled())
            return nullptr;
        scanned++;
        tjs_int local_x = x;
        tjs_int local_y = y;
        candidate->FromPrimaryCoordinates(local_x, local_y);
        const bool rect_hit =
            local_x >= 0 && local_y >= 0 && local_x < candidate->GetWidth() &&
            local_y < candidate->GetHeight();
        if(!rect_hit)
            return nullptr;
        if(TVPInputTraceEnabled()) {
            const auto action_owner = candidate->GetActionOwnerNoAddRef();
            spdlog::info("LayerManager selection rect candidate={} local=({}, {}) size={}x{} owner={} action={}",
                         candidate->GetName().AsStdString(), local_x, local_y,
                         candidate->GetWidth(), candidate->GetHeight(),
                         candidate->GetOwnerNoAddRef() ? "yes" : "no",
                         action_owner.Object ? "yes" : "no");
        }
        if(require_pixel_hit && !candidate->HitTestNoVisibleCheck(local_x, local_y))
            return nullptr;
        return candidate;
    };

    for(auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
        if(tTJSNI_BaseLayer *candidate = inspect_candidate(*it, true))
            return candidate;
    }
    scanned = 0;
    for(tTJSNI_BaseLayer *candidate : nodes) {
        if(!TVPIsConfirmableSelectionLayer(candidate))
            continue;
        if(!candidate->GetNodeVisible() || !candidate->GetNodeEnabled())
            continue;
        tjs_int local_x = x;
        tjs_int local_y = y;
        candidate->FromPrimaryCoordinates(local_x, local_y);
        const bool rect_hit =
            local_x >= 0 && local_y >= 0 && local_x < candidate->GetWidth() &&
            local_y < candidate->GetHeight();
        if(!rect_hit)
            continue;
        scanned++;
        if(TVPInputTraceEnabled()) {
            const auto action_owner = candidate->GetActionOwnerNoAddRef();
            spdlog::info("LayerManager selection rect candidate={} local=({}, {}) size={}x{} owner={} action={}",
                         candidate->GetName().AsStdString(), local_x, local_y,
                         candidate->GetWidth(), candidate->GetHeight(),
                         candidate->GetOwnerNoAddRef() ? "yes" : "no",
                         action_owner.Object ? "yes" : "no");
        }
        return candidate;
    }
    if(TVPInputTraceEnabled())
        spdlog::info("LayerManager selection scan none at ({}, {}) candidates={}",
                     x, y, scanned);
    return nullptr;
}

bool tTVPLayerManager::ShouldSynthesizeEnterForSaveLoadButton(
    tTJSNI_BaseLayer *layer, tjs_int x, tjs_int y) {
    if(!IsSaveLoadMessageCommandBand(layer, x, y))
        return false;

    const tjs_int w = (tjs_int)Primary->GetWidth();
    // Some KAG save/load screens draw bottom command buttons into the message
    // layer instead of separate button layers. These commands are also
    // bound to Enter; use that path when the pointer lands in their band.
    return x >= w * 35 / 100 && x <= w * 70 / 100;
}

bool tTVPLayerManager::IsSaveLoadMessageCommandBand(tTJSNI_BaseLayer *layer,
                                                    tjs_int x, tjs_int y) {
    if(!Primary || !TVPIsMessageLayer(layer))
        return false;
    const tjs_int w = (tjs_int)Primary->GetWidth();
    const tjs_int h = (tjs_int)Primary->GetHeight();
    if(w <= 0 || h <= 0)
        return false;

    if(y < h * 90 / 100)
        return false;

    // CafeStella's bottom Save/Load commands are drawn in the message layer;
    // keep those clicks out of the save-slot grid behind them.
    const bool save_command = x >= w * 35 / 100 && x <= w * 49 / 100;
    const bool load_command = x >= w * 52 / 100 && x <= w * 70 / 100;
    return save_command || load_command;
}

bool tTVPLayerManager::IsTitleMenuInputState(tTJSNI_BaseLayer *layer) {
    (void)layer;
    return TVPScriptReportsTitleMenu();
}

bool tTVPLayerManager::IsTitleMenuControlPoint(tjs_int x, tjs_int y) {
    if(!Primary)
        return true;

    const tjs_int w = (tjs_int)Primary->GetWidth();
    const tjs_int h = (tjs_int)Primary->GetHeight();
    if(w <= 0 || h <= 0)
        return true;

    // Some titles draw menu controls into the full-screen title layer and
    // dispatch them by coordinate. Keep the common left and right menu bands
    // interactive, but keep background/title-art taps from re-entering title
    // scripts.
    const bool left_title_controls =
        x <= w * 25 / 100 && y >= h * 30 / 100 && y <= h * 94 / 100;
    const bool right_title_controls =
        x >= w * 56 / 100 && y >= h * 25 / 100 && y <= h * 94 / 100;
    const bool far_right_switcher = x >= w * 92 / 100 && y >= h * 45 / 100;
    return left_title_controls || right_title_controls || far_right_switcher;
}

void tTVPLayerManager::PrimaryClick(tjs_int x, tjs_int y) {
    if(SuppressCurrentTitleMenuPointerGesture) {
        if(TVPInputTraceEnabled()) {
            spdlog::info(
                "LayerManager suppress title repeat click primary=({}, {})", x,
                y);
        }
        SuppressCurrentTitleMenuPointerGesture = false;
        return;
    }
    tTJSNI_BaseLayer *l = GetClickableLayerAt(x, y);
    TVPTraceLayerHit("click", x, y, l);
    TVPTraceLayersAt(this, "click-stack", x, y);
    if(l /*&& CaptureOwner == l*/) {
        if(TVPIsCgPreviewPresentationLayer(l)) {
            if(TVPInputTraceEnabled()) {
                spdlog::info(
                    "LayerManager cg preview suppress synthetic click layer={} primary=({}, {})",
                    l->GetName().AsStdString(), x, y);
            }
            return;
        }
        if(TVPIsSaveLoadOverlayCommandLayer(l)) {
            if(TVPInputTraceEnabled()) {
                spdlog::info(
                    "LayerManager save/load overlay command click -> onButtonClick layer={} primary=({}, {})",
                    l->GetName().AsStdString(), x, y);
            }
            l->FireButtonClick();
            return;
        }
        // LinkButtonLayerBase and copied UI-sheet buttons dispatch through
        // _evalOnClick/onButtonClick, regardless of whether the surrounding
        // page is the title screen.  Limiting this path to title/save-load
        // pages made gallery group selectors, settings controls, and the
        // language switch receive only the generic Layer.onClick event.
        if(l->HasButtonClickTarget()) {
            // A normal captured button gesture is already completed by the
            // captured layer's onMouseUp handler.  PrimaryClick is delivered
            // before PrimaryMouseUp on the Godot host, so evaluating the
            // bound expression here as well toggles state twice (open, then
            // immediately closed).  Only synthesize the click when the
            // button appeared after mouse-down and therefore does not own the
            // capture; that case has no matching button mouse-up to dispatch
            // its expression.
            if(CaptureOwner == l) {
                if(TVPInputTraceEnabled()) {
                    spdlog::info(
                        "LayerManager title link click deferred to captured mouseup layer={} primary=({}, {})",
                    l->GetName().AsStdString(), x, y);
                }
                return;
            }
            if(TVPInputTraceEnabled()) {
                spdlog::info(
                    "LayerManager link click -> onButtonClick layer={} primary=({}, {})",
                    l->GetName().AsStdString(), x, y);
            }
            l->FireButtonClick();
            return;
        }
        if(ShouldSynthesizeEnterForSaveLoadButton(l, x, y) && TVPMainWindow) {
            if(TVPInputTraceEnabled()) {
                spdlog::info("LayerManager save/load command click -> Enter primary=({}, {})",
                             x, y);
            }
            PendingConfirmRequiresSameSelection = false;
            PendingConfirmLayerName.clear();
            PendingSaveLoadEnterTick = TVPGetRoughTickCount32() + 100;
            return;
        }
        const bool message_command_band =
            IsSaveLoadMessageCommandBand(l, x, y);
        l->FromPrimaryCoordinates(x, y);
        l->FireClick(x, y);
    }
}
//---------------------------------------------------------------------------
void tTVPLayerManager::PrimaryDoubleClick(tjs_int x, tjs_int y) {
    tTJSNI_BaseLayer *l = GetClickableLayerAt(x, y);
    TVPTraceLayersAt(this, "double-click-stack", x, y);
    if(l /*&& CaptureOwner == l*/) {
        l->FromPrimaryCoordinates(x, y);
        l->FireDoubleClick(x, y);
    }
}
//---------------------------------------------------------------------------
void tTVPLayerManager::PrimaryMouseDown(tjs_int x, tjs_int y,
                                        tTVPMouseButton mb, tjs_uint32 flags) {
    if(mb == mbLeft && !IsTitleMenuControlPoint(x, y)) {
        tTJSNI_BaseLayer *title_hit = GetClickableLayerAt(x, y);
        if(IsTitleMenuInputState(title_hit) &&
           TVPIsTitleMenuBackgroundLayer(title_hit)) {
            if(TVPInputTraceEnabled()) {
                spdlog::info(
                    "LayerManager suppress title background pointer primary=({}, {})",
                    x, y);
            }
            SuppressCurrentTitleMenuPointerGesture = true;
            return;
        }
    }

    // Refresh script-side hover state before the press. Motion-backed buttons
    // can become interactive while the cursor is stationary (for example
    // after a title animation unlocks), so coordinate changes alone are not
    // sufficient to establish their focus.
    PrimaryMouseMove(x, y, flags, true);
    tTJSNI_BaseLayer *l =
        CaptureOwner ? CaptureOwner : GetClickableLayerAt(x, y);
    TVPTraceLayerHit("down", x, y, l);
    TVPTraceLayersAt(this, "down-stack", x, y);
    SuppressCurrentTitleMenuPointerGesture = false;
    if(l) {
        l->FromPrimaryCoordinates(x, y);
        ReleaseCaptureCalled = false;
        l->FireMouseDown(x, y, mb, flags);
        bool no_capture = ReleaseCaptureCalled;

        if(CaptureOwner != l) {
            ReleaseCapture();

            if(!no_capture) {
                CaptureOwner = l;
                if(CaptureOwner->Owner)
                    CaptureOwner->Owner->AddRef(); // addref TJS object
            }
        }

        SetHint(nullptr, ttstr());
    } else {
        ReleaseCapture();
    }
}
//---------------------------------------------------------------------------
void tTVPLayerManager::PrimaryMouseUp(tjs_int x, tjs_int y, tTVPMouseButton mb,
                                      tjs_uint32 flags) {
    if(mb == mbLeft && SuppressCurrentTitleMenuPointerGesture) {
        if(TVPInputTraceEnabled()) {
            spdlog::info(
                "LayerManager suppress title repeat mouseup primary=({}, {})",
                x, y);
        }
        return;
    }
    tTJSNI_BaseLayer *l;

    if(CaptureOwner)
        l = CaptureOwner;
    else
        l = GetClickableLayerAt(x, y);
    TVPTraceLayerHit("up", x, y, l);
    TVPTraceLayersAt(this, "up-stack", x, y);

    const int orig_x = x;
    const int orig_y = y;
    tTJSNI_BaseLayer *right_click_preview =
        mb == mbRight ? TVPFindCgPreviewLayerAt(this, orig_x, orig_y)
                      : nullptr;
    if(right_click_preview) {
        const std::string preview_name =
            right_click_preview->GetName().AsStdString();
        right_click_preview->FromPrimaryCoordinates(x, y);
        right_click_preview->FireMouseUp(x, y, mb, flags);
        TVPRouteCgModePreviewRightClick();
        if(TVPInputTraceEnabled()) {
            spdlog::info(
                "LayerManager route right click to cg preview mouseup {}",
                preview_name);
        }
        if(!TVPIsAnyMouseButtonPressedInShiftStateFlags(flags)) {
            ReleaseCapture();
            PrimaryMouseMove(orig_x, orig_y, flags);
        }
        return;
    }

    if(l) {
        l->FromPrimaryCoordinates(x, y);
        l->FireMouseUp(x, y, mb, flags);

        if(!TVPIsAnyMouseButtonPressedInShiftStateFlags(flags)) {
            ReleaseCapture();
            PrimaryMouseMove(orig_x, orig_y,
                             flags); // force recheck current under-cursor layer
        }
    }
}
//---------------------------------------------------------------------------
void tTVPLayerManager::PrimaryMouseMove(tjs_int x, tjs_int y,
                                        tjs_uint32 flags, bool force) {
    bool poschanged = force || LastMouseMoveX != x || LastMouseMoveY != y;
    LastMouseMoveX = x;
    LastMouseMoveY = y;

    tTJSNI_BaseLayer *l;

    if(CaptureOwner)
        l = CaptureOwner;
    else
        l = GetClickableLayerAt(x, y);

    // enter/leave event
    if(LastMouseMoveSent != l) {
        if(LastMouseMoveSent)
            LastMouseMoveSent->FireMouseLeave();

        // recheck l because the layer may become invalid during
        // FireMouseLeave call.
        if(CaptureOwner)
            l = CaptureOwner;
        else
            l = GetClickableLayerAt(x, y);

        if(l) {
            InNotifyingHintOrCursorChange = true;
            try {
                tTJSNI_BaseLayer *ll;

                l->FireMouseEnter();

                // recheck l because the layer may become invalid
                // during FireMouseEnter call.
                if(CaptureOwner)
                    ll = CaptureOwner;
                else
                    ll = GetClickableLayerAt(x, y);

                if(l != ll) {
                    l->FireMouseLeave();
                    l = ll;
                    if(l)
                        l->FireMouseEnter();
                }

                // note: rechecking is done only once to avoid
                // infinite loop

                if(l)
                    l->SetCurrentCursorToWindow();
                if(l)
                    l->SetCurrentHintToWindow();
            } catch(...) {
                InNotifyingHintOrCursorChange = false;
                throw;
            }
            InNotifyingHintOrCursorChange = false;
        }

        if(!l) {
            SetMouseCursor(0);
            SetHint(nullptr, ttstr());
        }
    }

    if(LastMouseMoveSent != l) {
        if(LastMouseMoveSent) {
            tTJSNI_BaseLayer *lay = LastMouseMoveSent;
            LastMouseMoveSent = nullptr;
            if(lay->Owner)
                lay->Owner->Release();
        }

        LastMouseMoveSent = l;

        if(LastMouseMoveSent) {
            if(LastMouseMoveSent->Owner)
                LastMouseMoveSent->Owner->AddRef();
        }
    }

    if(l) {
        if(poschanged) {
            l->FromPrimaryCoordinates(x, y);
            l->FireMouseMove(x, y, flags);
        }
    } else {
        // no layer to send the event
    }
}
//---------------------------------------------------------------------------
void tTVPLayerManager::PrimaryTouchDown(tjs_real x, tjs_real y, tjs_real cx,
                                        tjs_real cy, tjs_uint32 id) {
    tjs_int ix = (tjs_int)x, iy = (tjs_int)y;
    ReleaseTouchCapture(id);
    tTJSNI_BaseLayer *l = GetMostFrontChildAt(ix, iy);
    if(l) {
        l->FromPrimaryCoordinates(x, y);
        ReleaseTouchCaptureIDMark = (tjs_int64)id;
        l->FireTouchDown(x, y, cx, cy, id);
        if(ReleaseTouchCaptureIDMark == (tjs_int64)id) {
            SetTouchCapture(id, l);
        }
    }
}
//---------------------------------------------------------------------------
void tTVPLayerManager::PrimaryTouchUp(tjs_real x, tjs_real y, tjs_real cx,
                                      tjs_real cy, tjs_uint32 id) {
    tjs_int ix = (tjs_int)x, iy = (tjs_int)y;
    tTJSNI_BaseLayer *l =
        GetTouchCapture(id) ? GetTouchCapture(id) : GetMostFrontChildAt(ix, iy);
    if(l) {
        l->FromPrimaryCoordinates(x, y);
        l->FireTouchUp(x, y, cx, cy, id);
        ReleaseTouchCapture(id);
    }
}
//---------------------------------------------------------------------------
void tTVPLayerManager::PrimaryTouchMove(tjs_real x, tjs_real y, tjs_real cx,
                                        tjs_real cy, tjs_uint32 id) {
    tjs_int ix = (tjs_int)x, iy = (tjs_int)y;
    tTJSNI_BaseLayer *l =
        GetTouchCapture(id) ? GetTouchCapture(id) : GetMostFrontChildAt(ix, iy);
    if(l) {
        l->FromPrimaryCoordinates(x, y);
        l->FireTouchMove(x, y, cx, cy, id);
    }
}
//---------------------------------------------------------------------------
void tTVPLayerManager::PrimaryTouchScaling(tjs_real startdist, tjs_real curdist,
                                           tjs_real cx, tjs_real cy,
                                           tjs_int flag) {
    if(FocusedLayer)
        FocusedLayer->FireTouchScaling(startdist, curdist, cx, cy, flag);
}
//---------------------------------------------------------------------------
void tTVPLayerManager::PrimaryTouchRotate(tjs_real startangle,
                                          tjs_real curangle, tjs_real dist,
                                          tjs_real cx, tjs_real cy,
                                          tjs_int flag) {
    if(FocusedLayer)
        FocusedLayer->FireTouchRotate(startangle, curangle, dist, cx, cy, flag);
}
//---------------------------------------------------------------------------
void tTVPLayerManager::PrimaryMultiTouch() {
    if(FocusedLayer)
        FocusedLayer->FireMultiTouch();
}
//---------------------------------------------------------------------------
void tTVPLayerManager::ForceMouseLeave() {
    if(LastMouseMoveSent) {
        tTJSNI_BaseLayer *lay = LastMouseMoveSent;
        LastMouseMoveSent = nullptr;
        lay->FireMouseLeave();
        if(lay->Owner)
            lay->Owner->Release();
    }
}
//---------------------------------------------------------------------------
void tTVPLayerManager::ForceMouseRecheck() {
    PrimaryMouseMove(LastMouseMoveX, LastMouseMoveY, 0, true);
}
//---------------------------------------------------------------------------
void tTVPLayerManager::MouseOutOfWindow() {
    // notifys that the mouse cursor goes outside of the window.
    if(!CaptureOwner)
        PrimaryMouseMove(-1, -1,
                         0); // force mouse cursor out of the all
}
//---------------------------------------------------------------------------
void tTVPLayerManager::LeaveMouseFromTree(tTJSNI_BaseLayer *root) {
    // force to leave mouse
    if(LastMouseMoveSent) {
        if(LastMouseMoveSent->IsAncestorOrSelf(root)) {
            tTJSNI_BaseLayer *lay = LastMouseMoveSent;
            LastMouseMoveSent = nullptr;
            lay->FireMouseLeave();
            if(lay->Owner)
                lay->Owner->Release();
        }
    }
}
//---------------------------------------------------------------------------
void tTVPLayerManager::ReleaseCapture() {
    // release capture state
    ReleaseCaptureCalled = true;
    if(CaptureOwner) {
        tTJSNI_BaseLayer *lay = CaptureOwner;
        CaptureOwner = nullptr;
        if(lay->Owner)
            lay->Owner->Release();
        // release TJS object

        LayerTreeOwner->ReleaseMouseCapture(this);
    }
}
//---------------------------------------------------------------------------
void tTVPLayerManager::ReleaseCaptureFromTree(tTJSNI_BaseLayer *layer) {
    // Release capture state, if the capture object is descendant of
    // 'layer' or 'layer' itself.
    if(CaptureOwner) {
        if(CaptureOwner->IsAncestorOrSelf(layer)) {
            ReleaseCapture();
        }
    }
    std::vector<tTVPTouchCaptureLayer>::iterator itr = TouchCapture.begin();
    while(itr != TouchCapture.end()) {
        tTJSNI_BaseLayer *l = itr->Owner;
        if(l && l->IsAncestorOrSelf(layer)) {
            if(l->Owner)
                l->Owner->Release();
            if(ReleaseTouchCaptureIDMark == (tjs_int64)(itr->TouchID))
                ReleaseTouchCaptureIDMark = -1;
            itr = TouchCapture.erase(itr);
        } else {
            itr++;
        }
    }
}
//---------------------------------------------------------------------------
void tTVPLayerManager::ReleaseTouchCapture(tjs_uint32 id) {
    FindTouchID pred(id);
    std::vector<tTVPTouchCaptureLayer>::iterator itr =
        std::find_if(TouchCapture.begin(), TouchCapture.end(), pred);
    if(itr != TouchCapture.end()) {
        tTJSNI_BaseLayer *old = itr->Owner;
        if(old && old->Owner)
            old->Owner->Release();
        TouchCapture.erase(itr);
    }
    if(ReleaseTouchCaptureIDMark == (tjs_int64)id)
        ReleaseTouchCaptureIDMark = -1;
}
//---------------------------------------------------------------------------
void tTVPLayerManager::ReleaseTouchCaptureAll() {
    for(std::vector<tTVPTouchCaptureLayer>::iterator itr = TouchCapture.begin();
        itr != TouchCapture.end(); itr++) {
        tTJSNI_BaseLayer *l = itr->Owner;
        if(l->Owner)
            l->Owner->Release();
    }
    TouchCapture.clear();
    ReleaseTouchCaptureIDMark = -1;
}
//---------------------------------------------------------------------------
void tTVPLayerManager::SetTouchCapture(tjs_uint32 id, tTJSNI_BaseLayer *layer) {
    FindTouchID pred(id);
    std::vector<tTVPTouchCaptureLayer>::iterator itr =
        std::find_if(TouchCapture.begin(), TouchCapture.end(), pred);
    if(itr != TouchCapture.end()) {
        // 既に同一IDのものがある場合は、同じ場所で置き換える
        tTJSNI_BaseLayer *old = itr->Owner;
        if(old && old->Owner)
            old->Owner->Release();
        itr->Owner = layer;
        if(layer->Owner)
            layer->Owner->AddRef();
    } else {
        // ない場合は、末尾に追加。
        TouchCapture.emplace_back(id, layer);
        if(layer->Owner)
            layer->Owner->AddRef();
    }
}
//---------------------------------------------------------------------------
bool tTVPLayerManager::BlurTree(tTJSNI_BaseLayer *root) {
    // (primary only) remove focus from "root"
    RemoveTreeModalState(root);
    LeaveMouseFromTree(root);

    if(!FocusedLayer)
        return false;

    if(!FocusedLayer->IsAncestorOrSelf(root))
        return false;
    // root is not ancestor of current focused layer

    tTJSNI_BaseLayer *next = root->GetNextFocusable();

    if(next != FocusedLayer)
        SetFocusTo(next,
                   true); // focus to root's next focusable layer
    else
        SetFocusTo(nullptr, true);

    return true;
}
//---------------------------------------------------------------------------
void tTVPLayerManager::CheckTreeFocusableState(tTJSNI_BaseLayer *root) {
    // (primary only) check newly added tree's focusable state
    /*	// uncomment here to auto-focus
            if(FocusedLayer) return;

            tTJSNI_BaseLayer *lay = root->SearchFirstFocusable(true);
            if(lay) SetFocusTo(lay, true);
    */
}
//---------------------------------------------------------------------------
tTJSNI_BaseLayer *tTVPLayerManager::FocusPrev() {
    // focus to previous layer
    tTJSNI_BaseLayer *l;
    if(!FocusedLayer)
        l = SearchFirstFocusable(false); // search first focusable layer
    else
        l = FocusedLayer->GetPrevFocusable();

    if(l)
        SetFocusTo(l, false);
    return l;
}
//---------------------------------------------------------------------------
tTJSNI_BaseLayer *tTVPLayerManager::FocusNext() {
    // focus to next layer
    tTJSNI_BaseLayer *l;
    if(!FocusedLayer)
        l = SearchFirstFocusable(false); // search first focusable layer
    else
        l = FocusedLayer->GetNextFocusable();

    if(l)
        SetFocusTo(l, true);
    return l;
}
//---------------------------------------------------------------------------
tTJSNI_BaseLayer *
tTVPLayerManager::SearchFirstFocusable(bool ignore_chain_focusable) {
    // (primary only) search first focusable layer
    if(!Primary)
        return nullptr;
    tTJSNI_BaseLayer *lay =
        Primary->SearchFirstFocusable(ignore_chain_focusable);

    return lay;
}
//---------------------------------------------------------------------------
bool tTVPLayerManager::SetFocusTo(tTJSNI_BaseLayer *layer, bool direction) {
    // set focus to layer

    // direction = true : forward focus
    // direction = false: backward focus

    if(layer && !layer->GetNodeFocusable())
        return false;

    if(layer && !layer->Shutdown)
        layer = layer->FireBeforeFocus(FocusedLayer, direction);

    if(layer && !layer->GetNodeFocusable())
        return false;

    if(FocusedLayer == layer)
        return false;

    if(FocusChangeLock)
        TVPThrowExceptionMessage(TVPCannotChangeFocusInProcessingFocus);
    FocusChangeLock = true;

    tTJSNI_BaseLayer *org = FocusedLayer;
    FocusedLayer = layer;

    try {
        if(org && !org->Shutdown)
            org->FireBlur(layer);

        if(FocusedLayer && !FocusedLayer->Shutdown)
            FocusedLayer->FireFocus(org, direction);
    } catch(...) {
        if(FocusedLayer)
            if(FocusedLayer->Owner)
                FocusedLayer->Owner->AddRef();
        if(org)
            if(org->Owner)
                org->Owner->Release();
        FocusChangeLock = false;
        throw;
    }

    if(FocusedLayer)
        if(FocusedLayer->Owner)
            FocusedLayer->Owner->AddRef();
    if(org)
        if(org->Owner)
            org->Owner->Release();

    if(FocusedLayer)
        SetImeModeOf(FocusedLayer);
    else
        ResetImeMode();
    if(FocusedLayer)
        SetAttentionPointOf(FocusedLayer);
    else
        DisableAttentionPoint();

    FocusChangeLock = false;
    return true;
}
//---------------------------------------------------------------------------
void tTVPLayerManager::ReleaseAllModalLayer() {
    // (primary only) release all modal layer on invalidation
    std::vector<tTJSNI_BaseLayer *> copy(ModalLayerVector);
    ModalLayerVector.clear();

    std::vector<tTJSNI_BaseLayer *>::iterator i;
    for(i = copy.begin(); i < copy.end(); i++) {
        if((*i)->Owner)
            (*i)->Owner->Release();
    }
}
//---------------------------------------------------------------------------
void tTVPLayerManager::SetModeTo(tTJSNI_BaseLayer *layer) {
    // (primary only) set mode to layer
    if(!layer)
        return;

    SaveEnabledWork();

    try {
        tTJSNI_BaseLayer *current = GetCurrentModalLayer();
        if(current && layer->IsAncestorOrSelf(current))
            TVPThrowExceptionMessage(TVPCannotSetModeToDisabledOrModal);
        // cannot set mode to parent layer
        if(!layer->Visible)
            layer->Visible = true;
        if(!layer->GetParentVisible() || !layer->Enabled)
            TVPThrowExceptionMessage(TVPCannotSetModeToDisabledOrModal);
        // cannot set mode to parent layer
        if(layer == current)
            TVPThrowExceptionMessage(TVPCannotSetModeToDisabledOrModal);
        // cannot set mode to already modal layer

        SetFocusTo(layer->SearchFirstFocusable(), true);

        if(layer->Owner)
            layer->Owner->AddRef();
        ModalLayerVector.push_back(layer);

    } catch(...) {
        NotifyNodeEnabledState();
        throw;
    }

    NotifyNodeEnabledState();
}
//---------------------------------------------------------------------------
void tTVPLayerManager::RemoveModeFrom(tTJSNI_BaseLayer *layer) {
    // remove modal state from given layer
    bool do_notify = false;

    try {
        std::vector<tTJSNI_BaseLayer *>::iterator i;
        for(i = ModalLayerVector.begin(); i < ModalLayerVector.end();) {
            if(layer == *i) {
                if(!do_notify) {
                    do_notify = true;
                    SaveEnabledWork();
                }
                if(layer->Owner)
                    layer->Owner->Release();
                SetFocusTo(layer->GetNextFocusable(), true);
                i = ModalLayerVector.erase(i);
            } else {
                i++;
            }
        }
    } catch(...) {
        if(do_notify)
            NotifyNodeEnabledState();
        throw;
    }

    if(do_notify)
        NotifyNodeEnabledState();
}
//---------------------------------------------------------------------------
void tTVPLayerManager::RemoveTreeModalState(tTJSNI_BaseLayer *root) {
    // remove modal state from given tree
    bool do_notify = false;

    try {
        std::vector<tTJSNI_BaseLayer *>::iterator i;
        for(i = ModalLayerVector.begin(); i < ModalLayerVector.end();) {
            if((*i)->IsAncestorOrSelf(root)) {
                if(!do_notify) {
                    do_notify = true;
                    SaveEnabledWork();
                }
                if((*i)->Owner)
                    (*i)->Owner->Release();
                SetFocusTo(root->GetNextFocusable(), true);
                i = ModalLayerVector.erase(i);
            } else {
                i++;
            }
        }
    } catch(...) {
        if(do_notify)
            NotifyNodeEnabledState();
        throw;
    }

    if(do_notify)
        NotifyNodeEnabledState();
}
//---------------------------------------------------------------------------
tTJSNI_BaseLayer *tTVPLayerManager::GetCurrentModalLayer() const {
    // (primary only) get current modal layer
    tjs_uint size = (tjs_uint)ModalLayerVector.size();
    if(size == 0)
        return nullptr;
    return *(ModalLayerVector.begin() + size - 1);
}
//---------------------------------------------------------------------------
bool tTVPLayerManager::SearchAttentionPoint(tTJSNI_BaseLayer *target,
                                            tjs_int &x, tjs_int &y) {
    // search specified layer 's attention point
    while(target) {
        if(target->UseAttention) {
            x = target->AttentionLeft, y = target->AttentionTop;
            target->ToPrimaryCoordinates(x, y);
            return true;
        }
        target = target->Parent;
    }
    return false;
}
//---------------------------------------------------------------------------
void tTVPLayerManager::SetAttentionPointOf(tTJSNI_BaseLayer *layer) {
    if(!LayerTreeOwner)
        return;
    tjs_int x, y;
    if(SearchAttentionPoint(layer, x, y))
        LayerTreeOwner->SetAttentionPoint(this, layer, x, y);
    else
        LayerTreeOwner->DisableAttentionPoint(this);
}
//---------------------------------------------------------------------------
void tTVPLayerManager::DisableAttentionPoint() {
    if(LayerTreeOwner)
        LayerTreeOwner->DisableAttentionPoint(this);
}
//---------------------------------------------------------------------------
void tTVPLayerManager::NotifyAttentionStateChanged(tTJSNI_BaseLayer *from) {
    if(FocusedLayer == from) {
        SetAttentionPointOf(from);
    }
}
//---------------------------------------------------------------------------
void tTVPLayerManager::SetImeModeOf(tTJSNI_BaseLayer *layer) {
    if(!LayerTreeOwner)
        return;
    LayerTreeOwner->SetImeMode(this, layer->ImeMode);
}
//---------------------------------------------------------------------------
void tTVPLayerManager::ResetImeMode() {
    if(!LayerTreeOwner)
        return;
    LayerTreeOwner->ResetImeMode(this);
}
//---------------------------------------------------------------------------
void tTVPLayerManager::NotifyImeModeChanged(tTJSNI_BaseLayer *from) {
    if(FocusedLayer == from) {
        SetImeModeOf(from);
    }
}
//---------------------------------------------------------------------------
void tTVPLayerManager::SaveEnabledWork() {
    // save current node enabled state to EnabledWork
    // this does recursive call
    if(EnabledWorkRefCount == 0)
        if(Primary)
            Primary->SaveEnabledWork();

    EnabledWorkRefCount++;
}
//---------------------------------------------------------------------------
void tTVPLayerManager::NotifyNodeEnabledState() {
    // notify node enabled state change to self and its children
    // this refers EnabledWork which is created by SaveEnabledWork
    EnabledWorkRefCount--;

    if(EnabledWorkRefCount == 0)
        if(Primary)
            Primary->NotifyNodeEnabledState();
}
//---------------------------------------------------------------------------
void tTVPLayerManager::PrimaryKeyDown(tjs_uint key, tjs_uint32 shift) {
    if(TVPInputTraceEnabled()) {
        spdlog::info("LayerManager keydown key={} focused={} primary={}",
                     key,
                     FocusedLayer ? FocusedLayer->GetName().AsStdString() : "<none>",
                     Primary ? Primary->GetName().AsStdString() : "<none>");
    }
    if(FocusedLayer)
        FocusedLayer->FireKeyDown(key, shift);
    else if(Primary)
        Primary->DefaultKeyDown(key, shift);
}
//---------------------------------------------------------------------------
void tTVPLayerManager::PrimaryKeyUp(tjs_uint key, tjs_uint32 shift) {
    if(FocusedLayer)
        FocusedLayer->FireKeyUp(key, shift);
    else if(Primary)
        Primary->DefaultKeyUp(key, shift);
}
//---------------------------------------------------------------------------
void tTVPLayerManager::PrimaryKeyPress(tjs_char key) {
    if(FocusedLayer)
        FocusedLayer->FireKeyPress(key);
    else if(Primary)
        Primary->DefaultKeyPress(key);
}
//---------------------------------------------------------------------------
void tTVPLayerManager::PrimaryMouseWheel(tjs_uint32 shift, tjs_int delta,
                                         tjs_int x, tjs_int y) {
    if(TVPInputTraceEnabled()) {
        tTJSNI_BaseLayer *hit = GetClickableLayerAt(x, y);
        spdlog::info(
            "LayerManager wheel primary=({}, {}) delta={} focused={} hit={}", x,
            y, delta,
            FocusedLayer ? FocusedLayer->GetName().AsStdString() : "<none>",
            hit ? hit->GetName().AsStdString() : "<none>");
    }
    if(FocusedLayer)
        FocusedLayer->FireMouseWheel(shift, delta, x, y);
}
//---------------------------------------------------------------------------
void tTVPLayerManager::AddUpdateRegion(const tTVPComplexRect &rects) {
    UpdateRegion.Or(rects);
    if(UpdateRegion.GetCount() > TVP_UPDATE_UNITE_LIMIT)
        UpdateRegion.Unite();
    NotifyWindowInvalidation();
}
//---------------------------------------------------------------------------
void tTVPLayerManager::AddUpdateRegion(const tTVPRect &rect) {
    // the window is invalidated;
    UpdateRegion.Or(rect);
    NotifyWindowInvalidation();
}
//---------------------------------------------------------------------------
void tTVPLayerManager::UpdateToDrawDevice() {
    // drawdevice -> layer
    if(!Primary)
        return;
    auto process_pending_enter = [&](bool selection_confirm) {
        if(PendingSaveLoadEnterTick <= 0 || !TVPMainWindow)
            return;
        if(PendingConfirmRequiresSameSelection != selection_confirm)
            return;
        if(TVPGetRoughTickCount32() < PendingSaveLoadEnterTick)
            return;
        PendingSaveLoadEnterTick = 0;
        if(PendingConfirmRequiresSameSelection &&
           !IsPendingConfirmStillOnSameSelection()) {
            if(TVPInputTraceEnabled())
                spdlog::info("LayerManager selectable item confirm fallback canceled");
            PendingConfirmRequiresSameSelection = false;
            PendingConfirmLayerName.clear();
        } else {
            tTJSNI_BaseLayer *confirm_layer =
                PendingConfirmRequiresSameSelection
                    ? GetPendingConfirmSelectionLayer()
                    : nullptr;
            PendingConfirmRequiresSameSelection = false;
            PendingConfirmLayerName.clear();
            if(TVPInputTraceEnabled())
                spdlog::info("LayerManager selectable item confirm fallback dispatch Enter");
            if(confirm_layer) {
                confirm_layer->FireKeyDown(13, 0);
                confirm_layer->FireKeyUp(13, 0);
            } else {
                EngineInputEvent event;
                event.type = kEngineInputKeyDown;
                event.key_code = 13;
                if(auto *loop = EngineLoop::GetInstance()) {
                    loop->HandleInputEvent(event);
                    PendingSaveLoadEnterReleaseTick =
                        TVPGetRoughTickCount32() + 100;
                } else {
                    TVPPostInputEvent(
                        new tTVPOnKeyDownInputEvent(TVPMainWindow, 13, 0));
                }
            }
        }
    };
    process_pending_enter(false);
    if(PendingSaveLoadEnterReleaseTick > 0 &&
       TVPGetRoughTickCount32() >= PendingSaveLoadEnterReleaseTick) {
        PendingSaveLoadEnterReleaseTick = 0;
        EngineInputEvent event;
        event.type = kEngineInputKeyUp;
        event.key_code = 13;
        if(auto *loop = EngineLoop::GetInstance())
            loop->HandleInputEvent(event);
    }
    Primary->CompleteForWindow(this);
    process_pending_enter(true);
    TVPTraceCgModeViewTransIdle(this, (tjs_int)Primary->GetWidth() / 2,
                                (tjs_int)Primary->GetHeight() / 2);
}
//---------------------------------------------------------------------------
void tTVPLayerManager::NotifyUpdateRegionFixed() {
    // called by primary layer, notifying final update region is fixed
    //	Window->NotifyUpdateRegionFixed(UpdateRegion);
}
//---------------------------------------------------------------------------
void tTVPLayerManager::RequestInvalidation(const tTVPRect &r) {
    // called by the owner window to notify window surface is
    // invalidated by the system or user.
    if(!Primary)
        return;

    tTVPRect ur;
    tTVPRect cr(0, 0, Primary->Rect.get_width(), Primary->Rect.get_height());

    if(TVPIntersectRect(&ur, r, cr)) {
        AddUpdateRegion(ur);
    }
}
//---------------------------------------------------------------------------
void tTVPLayerManager::RecheckInputState() {
    // To re-check current layer under current mouse position
    // and update hint, cursor type and process layer enter/leave.
    // This can be reasonably slow, about 1 sec interval.
    ForceMouseRecheck();
}
//---------------------------------------------------------------------------
void tTVPLayerManager::DumpLayerStructure() {
    if(Primary)
        Primary->DumpStructure();
}
//---------------------------------------------------------------------------

bool tTVPDestTexture::CopyRect(
    tjs_int x, tjs_int y, const iTVPBaseBitmap *ref, tTVPRect refrect,
    tjs_int plane /*= (TVP_BB_COPY_MAIN | TVP_BB_COPY_MASK)*/) {
    if(HoldAlpha) {
        return tTVPBaseTexture::CopyRect(x, y, ref, refrect, TVP_BB_COPY_MAIN);
    } else {
        return tTVPBaseTexture::CopyRect(x, y, ref, refrect, plane);
    }
}
