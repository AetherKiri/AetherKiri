#include "ncbind.hpp"
#include "DebugIntf.h"
#include "EventIntf.h"
#include "LayerImpl.h"
#include "BitmapIntf.h"
#include "ScriptMgnIntf.h"
#include "motionplayer/Player.h"

#include <algorithm>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

#if defined(__APPLE__)
extern "C" bool TVPGodotLive2DRenderToLayer(iTJSDispatch2 *layerDispatch)
    __attribute__((weak_import));
#elif defined(__GNUC__)
extern "C" bool TVPGodotLive2DRenderToLayer(iTJSDispatch2 *layerDispatch)
    __attribute__((weak));
#endif

// Stub modules — register empty entries so Plugins.link() succeeds.
// The engine already has built-in support for the functionality these
// plugins originally provided, but some games explicitly link them by name.

#define NCB_MODULE_NAME TJS_W("k2compat.dll")
static void k2compat_stub() {}
NCB_PRE_REGIST_CALLBACK(k2compat_stub);

#if defined(__EMSCRIPTEN__)
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("layerExDraw.dll")
static void layerExDraw_stub() {}
NCB_PRE_REGIST_CALLBACK(layerExDraw_stub);

extern "C" void TVPRegisterLayerExDrawPluginAnchor() {}
#endif

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("kagexopt.dll")
static void kagexopt_stub() {}
NCB_PRE_REGIST_CALLBACK(kagexopt_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("krkrsteam.dll")
static void krkrsteam_stub() {}
NCB_PRE_REGIST_CALLBACK(krkrsteam_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("krmovie.dll")
static void krmovie_stub() {}
NCB_PRE_REGIST_CALLBACK(krmovie_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("kztouch.dll")
static void kztouch_stub() {}
NCB_PRE_REGIST_CALLBACK(kztouch_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("lzfs.dll")
static void lzfs_stub() {}
NCB_PRE_REGIST_CALLBACK(lzfs_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("win32ole.dll")
static void win32ole_stub() {}
NCB_PRE_REGIST_CALLBACK(win32ole_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("layerExSubImage.dll")
static void layerExSubImage_stub() {}
NCB_PRE_REGIST_CALLBACK(layerExSubImage_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("shellExecute.dll")
static void shellExecute_stub() {}
NCB_PRE_REGIST_CALLBACK(shellExecute_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("process.dll")
static void process_stub() {}
NCB_PRE_REGIST_CALLBACK(process_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("tasktray.dll")
static void tasktray_stub() {}
NCB_PRE_REGIST_CALLBACK(tasktray_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("adjustMonitor.dll")
static void adjustMonitor_stub() {}
NCB_PRE_REGIST_CALLBACK(adjustMonitor_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("fpslimit.dll")
static void fpslimit_stub() {}
NCB_PRE_REGIST_CALLBACK(fpslimit_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("systemEx.dll")
static void systemEx_stub() {}
NCB_PRE_REGIST_CALLBACK(systemEx_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("dmmcloud.dll")
static void dmmcloud_stub() {}
NCB_PRE_REGIST_CALLBACK(dmmcloud_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("libegl.dll")
static void libegl_stub() {}
NCB_PRE_REGIST_CALLBACK(libegl_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("libglesv2.dll")
static void libglesv2_stub() {}
NCB_PRE_REGIST_CALLBACK(libglesv2_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("m2vdec.dll")
static void m2vdec_stub() {}
NCB_PRE_REGIST_CALLBACK(m2vdec_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("version.dll")
static void version_stub() {}
NCB_PRE_REGIST_CALLBACK(version_stub);

#if !defined(KRKR_ENABLE_GPU_BRIDGE)
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("krkrgles.dll")
namespace {

static bool GlesCompatRenderGodotLive2D(iTJSDispatch2 *layerDispatch) {
#if defined(__GNUC__)
    if(TVPGodotLive2DRenderToLayer)
        return TVPGodotLive2DRenderToLayer(layerDispatch);
#endif
    (void)layerDispatch;
    return false;
}

static void SetGlesCompatInt(tTJSVariant *result, tjs_int value = 0) {
    if(result)
        *result = value;
}

static tjs_error CreateGlesCompatObject(tTJSVariant *result,
                                        const tjs_char *expression) {
    if(!result)
        return TJS_S_OK;
    try {
        TVPExecuteExpression(ttstr(expression), result);
    } catch(...) {
        result->Clear();
    }
    return TJS_S_OK;
}

static void SetGlesCompatMethod(iTJSDispatch2 *obj, const tjs_char *name,
                                tTJSNativeClassMethodCallback cb) {
    if(!obj || !name || !cb)
        return;
    iTJSDispatch2 *method = TJSCreateNativeClassMethod(cb);
    if(!method)
        return;
    tTJSVariant value(method, method);
    obj->PropSet(TJS_MEMBERENSURE, name, nullptr, &value, obj);
    method->Release();
}

static void SetGlesCompatProperty(iTJSDispatch2 *obj, const tjs_char *name,
                                  const tTJSVariant &value) {
    if(!obj || !name)
        return;
    auto copy = value;
    obj->PropSet(TJS_MEMBERENSURE, name, nullptr, &copy, obj);
}

static tjs_error GlesCompatReturnTrueCb(tTJSVariant *result, tjs_int,
                                        tTJSVariant **, iTJSDispatch2 *) {
    if(result)
        *result = true;
    return TJS_S_OK;
}

static tjs_error GlesCompatReturnFirstArgOrTrueCb(tTJSVariant *result,
                                                  tjs_int numparams,
                                                  tTJSVariant **param,
                                                  iTJSDispatch2 *) {
    if(!result)
        return TJS_S_OK;
    if(numparams > 0 && param && param[0])
        *result = *param[0];
    else
        *result = true;
    return TJS_S_OK;
}

static const tjs_char *GlesCompatVariantTypeName(tTJSVariantType type) {
    switch(type) {
    case tvtVoid: return TJS_W("void");
    case tvtObject: return TJS_W("object");
    case tvtString: return TJS_W("string");
    case tvtOctet: return TJS_W("octet");
    case tvtInteger: return TJS_W("integer");
    case tvtReal: return TJS_W("real");
    default: return TJS_W("unknown");
    }
}

static void LogGlesCompatArgsOnce(const tjs_char *tag, tjs_int numparams,
                                  tTJSVariant **param) {
    static tjs_int logCount = 0;
    if(logCount++ >= 12)
        return;
    ttstr msg = ttstr(TJS_W("GLESCompat.")) + tag + TJS_W(": argc=") +
                ttstr(numparams);
    for(tjs_int i = 0; i < numparams; ++i) {
        msg += TJS_W(" [");
        msg += ttstr(i);
        msg += TJS_W(":");
        msg += (param && param[i])
                   ? GlesCompatVariantTypeName(param[i]->Type())
                   : TJS_W("null");
        msg += TJS_W("]");
    }
    TVPAddLog(msg);
}

static iTJSDispatch2 *g_glesCompatRegisteredLayer = nullptr;

struct GlesCompatRenderable {
    tTJSVariant player;
    tTJSVariant layer;
    uintptr_t ownerKey = 0;
};

static std::mutex &GlesCompatRenderMutex() {
    static std::mutex mutex;
    return mutex;
}

static std::vector<GlesCompatRenderable> &GlesCompatRenderables() {
    static std::vector<GlesCompatRenderable> renderables;
    return renderables;
}

static bool GlesCompatGetObjectProperty(const tTJSVariant &object,
                                        const tjs_char *name,
                                        tTJSVariant &result) {
    result.Clear();
    if(object.Type() != tvtObject || !object.AsObjectNoAddRef() || !name)
        return false;
    iTJSDispatch2 *dispatch = object.AsObjectNoAddRef();
    return TJS_SUCCEEDED(dispatch->PropGet(
        TJS_IGNOREPROP, name, nullptr, &result, dispatch));
}

static void GlesCompatSetObjectProperty(iTJSDispatch2 *object,
                                        const tjs_char *name,
                                        const tTJSVariant &value) {
    if(!object || !name)
        return;
    auto copy = value;
    object->PropSet(TJS_MEMBERENSURE, name, nullptr, &copy, object);
}

static void GlesCompatIncrementRenderCount(iTJSDispatch2 *object) {
    if(!object)
        return;
    tTJSVariant current;
    tjs_int value = 0;
    if(TJS_SUCCEEDED(object->PropGet(TJS_IGNOREPROP, TJS_W("renderCount"),
                                     nullptr, &current, object)) &&
       current.Type() != tvtVoid) {
        try {
            value = static_cast<tjs_int>(current);
        } catch(...) {
            value = 0;
        }
    }
    GlesCompatSetObjectProperty(object, TJS_W("renderCount"),
                                tTJSVariant(value + 1));
}

static bool GlesCompatIsNumericVariant(const tTJSVariant &value) {
    const auto type = value.Type();
    return type != tvtVoid && type != tvtString && type != tvtObject &&
        type != tvtOctet;
}

static bool GlesCompatIsLayerDispatch(iTJSDispatch2 *object) {
    if(!object)
        return false;

    tTJSNI_BaseLayer *layer = nullptr;
    if(TJS_SUCCEEDED(object->NativeInstanceSupport(
           TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
           reinterpret_cast<iTJSNativeInstance **>(&layer))) &&
       layer) {
        return true;
    }

    tTJSVariant imageWidth;
    return TJS_SUCCEEDED(object->PropGet(
        TJS_IGNOREPROP, TJS_W("imageWidth"), nullptr, &imageWidth, object)) &&
        imageWidth.Type() != tvtVoid;
}

static tTJSNI_BaseLayer *GlesCompatNativeLayer(iTJSDispatch2 *object) {
    if(!object)
        return nullptr;
    tTJSNI_BaseLayer *layer = nullptr;
    if(TJS_SUCCEEDED(object->NativeInstanceSupport(
           TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
           reinterpret_cast<iTJSNativeInstance **>(&layer))) &&
       layer) {
        return layer;
    }
    return nullptr;
}

static bool GlesCompatGetBitmapFromObject(iTJSDispatch2 *object,
                                          iTVPBaseBitmap **bitmap,
                                          tTVPBlendOperationMode *mode) {
    if(!object || !bitmap)
        return false;
    if(auto *layer = GlesCompatNativeLayer(object)) {
        *bitmap = layer->GetMainImage();
        if(mode)
            *mode = layer->GetOperationModeFromType();
        return *bitmap != nullptr;
    }

    tTJSNI_Bitmap *srcBitmap = nullptr;
    if(TJS_SUCCEEDED(object->NativeInstanceSupport(
           TJS_NIS_GETINSTANCE, tTJSNC_Bitmap::ClassID,
           reinterpret_cast<iTJSNativeInstance **>(&srcBitmap))) &&
       srcBitmap) {
        *bitmap = srcBitmap->GetBitmap();
        if(mode)
            *mode = omAlpha;
        return *bitmap != nullptr;
    }
    return false;
}

static bool GlesCompatGetLayerAndSourceArgs(tjs_int numparams,
                                            tTJSVariant **param,
                                            tTJSNI_BaseLayer **dstLayer,
                                            iTJSDispatch2 **srcObject,
                                            tjs_int *dstIndex,
                                            tjs_int *srcIndex) {
    if(!param || !dstLayer || !srcObject)
        return false;
    *dstLayer = nullptr;
    *srcObject = nullptr;
    if(dstIndex)
        *dstIndex = -1;
    if(srcIndex)
        *srcIndex = -1;

    for(tjs_int i = 0; i < numparams; ++i) {
        if(!param[i] || param[i]->Type() != tvtObject ||
           !param[i]->AsObjectNoAddRef()) {
            continue;
        }
        iTJSDispatch2 *object = param[i]->AsObjectNoAddRef();
        if(!*dstLayer) {
            if(auto *layer = GlesCompatNativeLayer(object)) {
                *dstLayer = layer;
                if(dstIndex)
                    *dstIndex = i;
                continue;
            }
        }
        if(!*srcObject) {
            iTVPBaseBitmap *bitmap = nullptr;
            if(GlesCompatGetBitmapFromObject(object, &bitmap, nullptr)) {
                *srcObject = object;
                if(srcIndex)
                    *srcIndex = i;
            }
        }
    }

    return *dstLayer && *srcObject;
}

static bool GlesCompatGetNumericArgs(tjs_int numparams, tTJSVariant **param,
                                     tjs_int start,
                                     std::vector<tjs_real> &out) {
    out.clear();
    if(!param)
        return false;
    for(tjs_int i = std::max<tjs_int>(0, start); i < numparams; ++i) {
        if(!param[i] || !GlesCompatIsNumericVariant(*param[i]))
            continue;
        out.push_back(static_cast<tjs_real>(*param[i]));
    }
    return !out.empty();
}

static bool GlesCompatDrawLayerNative(tjs_int numparams, tTJSVariant **param) {
    tTJSNI_BaseLayer *dstLayer = nullptr;
    iTJSDispatch2 *srcObject = nullptr;
    tjs_int dstIndex = -1;
    tjs_int srcIndex = -1;
    if(!GlesCompatGetLayerAndSourceArgs(numparams, param, &dstLayer,
                                        &srcObject, &dstIndex, &srcIndex)) {
        return false;
    }

    iTVPBaseBitmap *src = nullptr;
    tTVPBlendOperationMode mode = omAlpha;
    if(!GlesCompatGetBitmapFromObject(srcObject, &src, &mode) || !src)
        return false;

    std::vector<tjs_real> nums;
    const tjs_int firstNumeric =
        srcIndex > dstIndex ? dstIndex + 1 : srcIndex + 1;
    GlesCompatGetNumericArgs(numparams, param, firstNumeric, nums);

    const tjs_int dx = nums.size() > 0 ? static_cast<tjs_int>(nums[0]) : 0;
    const tjs_int dy = nums.size() > 1 ? static_cast<tjs_int>(nums[1]) : 0;
    const tjs_int dw = nums.size() > 2 ? static_cast<tjs_int>(nums[2])
                                       : dstLayer->GetWidth();
    const tjs_int dh = nums.size() > 3 ? static_cast<tjs_int>(nums[3])
                                       : dstLayer->GetHeight();
    const tjs_int sx = nums.size() > 4 ? static_cast<tjs_int>(nums[4]) : 0;
    const tjs_int sy = nums.size() > 5 ? static_cast<tjs_int>(nums[5]) : 0;
    const tjs_int sw = nums.size() > 6 ? static_cast<tjs_int>(nums[6])
                                       : src->GetWidth();
    const tjs_int sh = nums.size() > 7 ? static_cast<tjs_int>(nums[7])
                                       : src->GetHeight();
    if(dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0)
        return false;

    try {
        dstLayer->StretchCopy(tTVPRect(dx, dy, dx + dw, dy + dh), src,
                              tTVPRect(sx, sy, sx + sw, sy + sh),
                              stNearest, 0.0);
        dstLayer->Update(false);
        return true;
    } catch(...) {
        return false;
    }
}

static bool GlesCompatDrawAffineNative(tjs_int numparams,
                                       tTJSVariant **param) {
    tTJSNI_BaseLayer *dstLayer = nullptr;
    iTJSDispatch2 *srcObject = nullptr;
    tjs_int dstIndex = -1;
    tjs_int srcIndex = -1;
    if(!GlesCompatGetLayerAndSourceArgs(numparams, param, &dstLayer,
                                        &srcObject, &dstIndex, &srcIndex)) {
        return false;
    }

    iTVPBaseBitmap *src = nullptr;
    tTVPBlendOperationMode mode = omAlpha;
    if(!GlesCompatGetBitmapFromObject(srcObject, &src, &mode) || !src)
        return false;

    std::vector<tjs_real> nums;
    const tjs_int firstNumeric =
        srcIndex > dstIndex ? dstIndex + 1 : srcIndex + 1;
    GlesCompatGetNumericArgs(numparams, param, firstNumeric, nums);
    if(nums.size() < 11)
        return false;

    const tTVPRect srcRect(
        static_cast<tjs_int>(nums[0]),
        static_cast<tjs_int>(nums[1]),
        static_cast<tjs_int>(nums[0] + nums[2]),
        static_cast<tjs_int>(nums[1] + nums[3]));
    if(srcRect.get_width() <= 0 || srcRect.get_height() <= 0)
        return false;

    const auto opMode = mode == omAuto ? omAlpha : mode;
    const tjs_int opacity =
        nums.size() > 13 ? static_cast<tjs_int>(nums[13]) : 255;
    const auto stretchType =
        nums.size() > 14
            ? static_cast<tTVPBBStretchType>(static_cast<tjs_int>(nums[14]))
            : stNearest;

    try {
        const bool matrixMode = static_cast<tjs_int>(nums[4]) != 0;
        if(matrixMode) {
            t2DAffineMatrix matrix;
            matrix.a = nums[5];
            matrix.b = nums[6];
            matrix.c = nums[7];
            matrix.d = nums[8];
            matrix.tx = nums[9];
            matrix.ty = nums[10];
            dstLayer->OperateAffine(matrix, src, srcRect, opMode, opacity,
                                    stretchType);
        } else {
            tTVPPointD points[3];
            points[0].x = nums[5];
            points[0].y = nums[6];
            points[1].x = nums[7];
            points[1].y = nums[8];
            points[2].x = nums[9];
            points[2].y = nums[10];
            dstLayer->OperateAffine(points, src, srcRect, opMode, opacity,
                                    stretchType);
        }
        dstLayer->Update(false);
        return true;
    } catch(...) {
        return false;
    }
}

static iTJSDispatch2 *GlesCompatResolveLayerDispatch(const tTJSVariant &value,
                                                     int depth = 0) {
    if(depth > 4 || value.Type() != tvtObject || !value.AsObjectNoAddRef())
        return nullptr;

    iTJSDispatch2 *object = value.AsObjectNoAddRef();
    if(GlesCompatIsLayerDispatch(object))
        return object;

    static const tjs_char *kExplicitLayerProps[] = {
        TJS_W("targetLayer"), TJS_W("_targetLayer"),
        TJS_W("renderTarget"), TJS_W("_renderTarget"),
        TJS_W("layer"), TJS_W("_layer"), TJS_W("baseLayer"),
        TJS_W("_base"), TJS_W("base"), TJS_W("fore"),
        TJS_W("back"), TJS_W("primaryLayer"), nullptr
    };
    static const tjs_char *kOwnerLayerProps[] = {
        TJS_W("owner"), TJS_W("_owner"), TJS_W("parent"), nullptr
    };

    auto tryProps = [&](const tjs_char *const *props) -> iTJSDispatch2 * {
        for(int i = 0; props[i]; ++i) {
            tTJSVariant prop;
            if(!GlesCompatGetObjectProperty(value, props[i], prop) ||
               prop.Type() != tvtObject || !prop.AsObjectNoAddRef() ||
               prop.AsObjectNoAddRef() == object) {
                continue;
            }
            if(auto *resolved = GlesCompatResolveLayerDispatch(prop, depth + 1))
                return resolved;
        }
        return nullptr;
    };

    if(auto *resolved = tryProps(kExplicitLayerProps))
        return resolved;
    if(auto *resolved = tryProps(kOwnerLayerProps))
        return resolved;
    return nullptr;
}

static iTJSDispatch2 *GlesCompatFindLayerInParams(tjs_int numparams,
                                                  tTJSVariant **param) {
    if(!param)
        return nullptr;
    for(tjs_int i = 0; i < numparams; ++i) {
        if(!param[i])
            continue;
        if(auto *layer = GlesCompatResolveLayerDispatch(*param[i]))
            return layer;
    }
    return nullptr;
}

static iTJSDispatch2 *GlesCompatResolveMainWindowPrimaryLayer() {
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(!global)
        return nullptr;

    iTJSDispatch2 *resolved = nullptr;
    tTJSVariant windowClass;
    tTJSVariant mainWindow;
    tTJSVariant primaryLayer;
    if(TJS_SUCCEEDED(global->PropGet(0, TJS_W("Window"), nullptr,
                                     &windowClass, global)) &&
       windowClass.Type() == tvtObject && windowClass.AsObjectNoAddRef() &&
       TJS_SUCCEEDED(windowClass.AsObjectNoAddRef()->PropGet(
           0, TJS_W("mainWindow"), nullptr, &mainWindow,
           windowClass.AsObjectNoAddRef())) &&
       mainWindow.Type() == tvtObject && mainWindow.AsObjectNoAddRef() &&
       TJS_SUCCEEDED(mainWindow.AsObjectNoAddRef()->PropGet(
           0, TJS_W("primaryLayer"), nullptr, &primaryLayer,
           mainWindow.AsObjectNoAddRef())) &&
       primaryLayer.Type() == tvtObject && primaryLayer.AsObjectNoAddRef()) {
        resolved = GlesCompatResolveLayerDispatch(primaryLayer);
        if(!resolved)
            resolved = primaryLayer.AsObjectNoAddRef();
    }

    global->Release();
    return resolved;
}

static iTJSDispatch2 *GlesCompatDefaultLayer() {
    if(g_glesCompatRegisteredLayer)
        return g_glesCompatRegisteredLayer;
    g_glesCompatRegisteredLayer = GlesCompatResolveMainWindowPrimaryLayer();
    return g_glesCompatRegisteredLayer;
}

static motion::Player *GlesCompatNativeMotionPlayer(iTJSDispatch2 *object) {
    return object ? ncbInstanceAdaptor<motion::Player>::GetNativeInstance(
                        object, false)
                  : nullptr;
}

static iTJSDispatch2 *GlesCompatResolveMotionPlayerDispatch(
    const tTJSVariant &value, int depth = 0) {
    if(depth > 3 || value.Type() != tvtObject || !value.AsObjectNoAddRef())
        return nullptr;

    iTJSDispatch2 *object = value.AsObjectNoAddRef();
    if(GlesCompatNativeMotionPlayer(object))
        return object;

    static const tjs_char *kPlayerProps[] = {
        TJS_W("player"), TJS_W("motionPlayer"), TJS_W("motion"),
        TJS_W("object"), TJS_W("target"), TJS_W("owner"),
        TJS_W("_owner"), nullptr
    };

    for(int i = 0; kPlayerProps[i]; ++i) {
        tTJSVariant prop;
        if(!GlesCompatGetObjectProperty(value, kPlayerProps[i], prop) ||
           prop.Type() != tvtObject || !prop.AsObjectNoAddRef() ||
           prop.AsObjectNoAddRef() == object) {
            continue;
        }
        if(auto *resolved =
               GlesCompatResolveMotionPlayerDispatch(prop, depth + 1)) {
            return resolved;
        }
    }
    return nullptr;
}

static iTJSDispatch2 *GlesCompatFindMotionPlayerInParams(tjs_int numparams,
                                                         tTJSVariant **param) {
    if(!param)
        return nullptr;
    for(tjs_int i = 0; i < numparams; ++i) {
        if(!param[i])
            continue;
        if(auto *player = GlesCompatResolveMotionPlayerDispatch(*param[i]))
            return player;
    }
    return nullptr;
}

static bool GlesCompatInvokeMotionDraw(iTJSDispatch2 *player,
                                       iTJSDispatch2 *targetLayer,
                                       const tjs_char *tag) {
    if(!player || !targetLayer || !GlesCompatNativeMotionPlayer(player))
        return false;

    static bool rendering = false;
    if(rendering)
        return false;
    rendering = true;
    struct Guard {
        ~Guard() { rendering = false; }
    } guard;

    try {
        tTJSVariant result;
        tTJSVariant layerArg(targetLayer, targetLayer);
        tTJSVariant *args[] = { &layerArg };
        tjs_uint hint = 0;
        const tjs_error er = player->FuncCall(
            0, TJS_W("draw"), &hint, &result, 1, args, player);
        if(TJS_SUCCEEDED(er))
            return true;
        TVPAddLog(ttstr(TJS_W("GLESCompat.")) +
                  (tag ? tag : TJS_W("render")) +
                  TJS_W(": Motion.Player.draw failed"));
    } catch(const eTJS &e) {
        TVPAddLog(ttstr(TJS_W("GLESCompat.")) +
                  (tag ? tag : TJS_W("render")) +
                  TJS_W(": Motion.Player.draw threw ") + e.GetMessage());
    } catch(...) {
        TVPAddLog(ttstr(TJS_W("GLESCompat.")) +
                  (tag ? tag : TJS_W("render")) +
                  TJS_W(": Motion.Player.draw threw unknown exception"));
    }
    return false;
}

class GlesCompatMotionRenderHook final :
    public tTVPContinuousEventCallbackIntf {
public:
    void Start() {
        if(registered_)
            return;
        TVPAddContinuousEventHook(this);
        registered_ = true;
    }
    void Stop() {
        if(!registered_)
            return;
        TVPRemoveContinuousEventHook(this);
        registered_ = false;
    }
    void OnContinuousCallback(tjs_uint64) override;

private:
    bool registered_ = false;
};

static GlesCompatMotionRenderHook &GlesCompatRenderHook() {
    static GlesCompatMotionRenderHook hook;
    return hook;
}

static void GlesCompatRegisterRenderable(tjs_int numparams,
                                         tTJSVariant **param,
                                         uintptr_t ownerKey,
                                         const tjs_char *tag) {
    iTJSDispatch2 *layer = GlesCompatFindLayerInParams(numparams, param);
    if(layer)
        g_glesCompatRegisteredLayer = layer;

    iTJSDispatch2 *player =
        GlesCompatFindMotionPlayerInParams(numparams, param);
    if(!player)
        return;
    if(!layer) {
        tTJSVariant playerVar(player, player);
        layer = GlesCompatResolveLayerDispatch(playerVar);
    }
    if(!layer) {
        TVPAddLog(ttstr(TJS_W("GLESCompat.")) +
                  (tag ? tag : TJS_W("entry")) +
                  TJS_W(": skipped Motion.Player render target without layer"));
        return;
    }

    std::lock_guard<std::mutex> lock(GlesCompatRenderMutex());
    auto &items = GlesCompatRenderables();
    auto it = std::find_if(items.begin(), items.end(),
        [player](const GlesCompatRenderable &item) {
            return item.player.Type() == tvtObject &&
                item.player.AsObjectNoAddRef() == player;
        });
    if(it == items.end()) {
        if(items.size() >= 64)
            items.erase(items.begin());
        GlesCompatRenderable item;
        item.player = tTJSVariant(player, player);
        if(layer)
            item.layer = tTJSVariant(layer, layer);
        item.ownerKey = ownerKey;
        items.push_back(item);
        TVPAddLog(ttstr(TJS_W("GLESCompat.")) +
                  (tag ? tag : TJS_W("entry")) +
                  TJS_W(": registered Motion.Player render target"));
    } else {
        if(layer)
            it->layer = tTJSVariant(layer, layer);
        it->ownerKey = ownerKey;
    }
    GlesCompatRenderHook().Start();
}

static void GlesCompatRemoveRenderable(tjs_int numparams, tTJSVariant **param,
                                       uintptr_t ownerKey) {
    iTJSDispatch2 *player =
        GlesCompatFindMotionPlayerInParams(numparams, param);
    std::lock_guard<std::mutex> lock(GlesCompatRenderMutex());
    auto &items = GlesCompatRenderables();
    items.erase(std::remove_if(items.begin(), items.end(),
        [player, ownerKey](const GlesCompatRenderable &item) {
            const bool playerMatches =
                player && item.player.Type() == tvtObject &&
                item.player.AsObjectNoAddRef() == player;
            const bool ownerMatches = ownerKey && item.ownerKey == ownerKey;
            return playerMatches || (!player && ownerMatches);
        }), items.end());
}

static tjs_int GlesCompatRenderMotionPlayers(const tjs_char *tag) {
    std::vector<GlesCompatRenderable> snapshot;
    {
        std::lock_guard<std::mutex> lock(GlesCompatRenderMutex());
        snapshot = GlesCompatRenderables();
    }

    tjs_int rendered = 0;
    for(const auto &item : snapshot) {
        iTJSDispatch2 *player = item.player.Type() == tvtObject
            ? item.player.AsObjectNoAddRef()
            : nullptr;
        iTJSDispatch2 *layer = item.layer.Type() == tvtObject
            ? item.layer.AsObjectNoAddRef()
            : nullptr;
        if(!layer && item.player.Type() == tvtObject)
            layer = GlesCompatResolveLayerDispatch(item.player);
        if(!layer)
            continue;
        if(GlesCompatInvokeMotionDraw(player, layer, tag))
            ++rendered;
    }

    if(snapshot.empty()) {
        for(const auto &playerVar :
            motion::SnapshotAutoProgressPlayerDispatchesForCompat()) {
            iTJSDispatch2 *player = playerVar.Type() == tvtObject
                ? playerVar.AsObjectNoAddRef()
                : nullptr;
            iTJSDispatch2 *layer = GlesCompatResolveLayerDispatch(playerVar);
            if(!layer)
                continue;
            if(GlesCompatInvokeMotionDraw(player, layer, tag))
                ++rendered;
        }
    }

    static tjs_int logCount = 0;
    if(rendered > 0 && logCount++ < 8) {
        TVPAddLog(ttstr(TJS_W("GLESCompat.")) +
                  (tag ? tag : TJS_W("render")) +
                  TJS_W(": rendered Motion.Player count=") + ttstr(rendered));
    }
    return rendered;
}

void GlesCompatMotionRenderHook::OnContinuousCallback(tjs_uint64) {
    GlesCompatRenderMotionPlayers(TJS_W("continuous"));
    static tjs_uint debugTick = 0;
    static bool debugEnabled = [] {
        const char *value = std::getenv("AETHERKIRI_MOTION_DEBUG");
        return value && *value && std::string(value) != "0";
    }();
    if(debugEnabled) {
        ++debugTick;
        if(debugTick == 3000 || debugTick == 4200 || debugTick == 5400) {
            if(auto *layer = GlesCompatNativeLayer(GlesCompatDefaultLayer())) {
                TVPAddLog(ttstr(TJS_W("GLESCompat.debug.layerTree tick=")) +
                          ttstr(static_cast<tjs_int>(debugTick)));
                layer->DumpStructure();
            }
        }
    }
}

static tjs_error GlesCompatEntryUpdateObjectCb(tTJSVariant *result,
                                               tjs_int numparams,
                                               tTJSVariant **param,
                                               iTJSDispatch2 *objthis) {
    LogGlesCompatArgsOnce(TJS_W("entryUpdateObject"), numparams, param);
    GlesCompatRegisterRenderable(
        numparams, param, reinterpret_cast<uintptr_t>(objthis),
        TJS_W("entryUpdateObject"));
    GlesCompatIncrementRenderCount(objthis);
    if(result)
        *result = true;
    return TJS_S_OK;
}

static tjs_error GlesCompatCopyLayerCb(tTJSVariant *result, tjs_int numparams,
                                       tTJSVariant **param,
                                       iTJSDispatch2 *objthis) {
    LogGlesCompatArgsOnce(TJS_W("copyLayer"), numparams, param);
    if(auto *layer = GlesCompatFindLayerInParams(numparams, param))
        g_glesCompatRegisteredLayer = layer;
    GlesCompatRegisterRenderable(
        numparams, param, reinterpret_cast<uintptr_t>(objthis),
        TJS_W("copyLayer"));
    GlesCompatDrawLayerNative(numparams, param);
    GlesCompatRenderMotionPlayers(TJS_W("copyLayer"));
    GlesCompatIncrementRenderCount(objthis);
    if(result)
        *result = true;
    return TJS_S_OK;
}

static tjs_error GlesCompatDrawAffineCb(tTJSVariant *result, tjs_int numparams,
                                        tTJSVariant **param,
                                        iTJSDispatch2 *objthis) {
    LogGlesCompatArgsOnce(TJS_W("drawAffine"), numparams, param);
    if(auto *layer = GlesCompatFindLayerInParams(numparams, param))
        g_glesCompatRegisteredLayer = layer;
    GlesCompatRegisterRenderable(
        numparams, param, reinterpret_cast<uintptr_t>(objthis),
        TJS_W("drawAffine"));
    GlesCompatDrawAffineNative(numparams, param);
    GlesCompatRenderMotionPlayers(TJS_W("drawAffine"));
    GlesCompatIncrementRenderCount(objthis);
    if(result)
        *result = true;
    return TJS_S_OK;
}

static tjs_error GlesCompatDrawLayerCb(tTJSVariant *result, tjs_int numparams,
                                       tTJSVariant **param,
                                       iTJSDispatch2 *objthis) {
    LogGlesCompatArgsOnce(TJS_W("drawLayer"), numparams, param);
    if(auto *layer = GlesCompatFindLayerInParams(numparams, param))
        g_glesCompatRegisteredLayer = layer;
    GlesCompatRegisterRenderable(
        numparams, param, reinterpret_cast<uintptr_t>(objthis),
        TJS_W("drawLayer"));
    GlesCompatDrawLayerNative(numparams, param);
    GlesCompatRenderMotionPlayers(TJS_W("drawLayer"));
    GlesCompatIncrementRenderCount(objthis);
    if(result)
        *result = true;
    return TJS_S_OK;
}

static tjs_error GlesCompatRenderCb(tTJSVariant *result, tjs_int,
                                    tTJSVariant **, iTJSDispatch2 *objthis) {
    GlesCompatRenderMotionPlayers(TJS_W("render"));
    GlesCompatIncrementRenderCount(objthis);
    if(result)
        *result = true;
    return TJS_S_OK;
}

static tjs_error GlesCompatCaptureCb(tTJSVariant *result, tjs_int numparams,
                                     tTJSVariant **param,
                                     iTJSDispatch2 *objthis) {
    LogGlesCompatArgsOnce(TJS_W("capture"), numparams, param);
    iTJSDispatch2 *layerDispatch = GlesCompatFindLayerInParams(numparams, param);
    if(layerDispatch)
        g_glesCompatRegisteredLayer = layerDispatch;
	    GlesCompatRegisterRenderable(
	        numparams, param, reinterpret_cast<uintptr_t>(objthis),
	        TJS_W("capture"));
	    GlesCompatRenderMotionPlayers(TJS_W("capture"));
	    if(layerDispatch)
	        GlesCompatRenderGodotLive2D(layerDispatch);
	    GlesCompatIncrementRenderCount(objthis);
    if(result) {
        if(numparams > 0 && param && param[0])
            *result = *param[0];
        else
            *result = true;
    }
    return TJS_S_OK;
}

static tjs_error GlesCompatGlesEntryCb(tTJSVariant *result, tjs_int numparams,
                                       tTJSVariant **param,
                                       iTJSDispatch2 *objthis) {
    LogGlesCompatArgsOnce(TJS_W("glesEntry"), numparams, param);
    GlesCompatRegisterRenderable(
        numparams, param, reinterpret_cast<uintptr_t>(objthis),
        TJS_W("glesEntry"));
    if(result)
        *result = true;
    return TJS_S_OK;
}

static tjs_error GlesCompatGlesRemoveCb(tTJSVariant *result, tjs_int numparams,
                                        tTJSVariant **param,
                                        iTJSDispatch2 *objthis) {
    GlesCompatRemoveRenderable(
        numparams, param, reinterpret_cast<uintptr_t>(objthis));
    if(result)
        *result = true;
    return TJS_S_OK;
}

static void GlesCompatInvokeLoadIfPresent(tTJSVariant &object,
                                          tjs_int numparams,
                                          tTJSVariant **param) {
    if(numparams <= 0 || !param || object.Type() != tvtObject)
        return;
    iTJSDispatch2 *dispatch = object.AsObjectNoAddRef();
    if(!dispatch)
        return;
    tjs_uint hint = 0;
    dispatch->FuncCall(0, TJS_W("load"), &hint, nullptr, numparams, param,
                       dispatch);
}

static tjs_error GlesCompatCreateModelCb(tTJSVariant *result,
                                         tjs_int numparams,
                                         tTJSVariant **param,
                                         iTJSDispatch2 *) {
    tTJSVariant model;
    tjs_error er = CreateGlesCompatObject(&model, TJS_W("new Live2DModel()"));
    if(TJS_FAILED(er) || model.Type() != tvtObject) {
        if(result)
            result->Clear();
        return TJS_FAILED(er) ? er : TJS_E_FAIL;
    }
    GlesCompatInvokeLoadIfPresent(model, numparams, param);
    if(result)
        *result = model;
    return TJS_S_OK;
}

static tjs_error GlesCompatCreateMatrixCb(tTJSVariant *result, tjs_int,
                                          tTJSVariant **, iTJSDispatch2 *) {
    return CreateGlesCompatObject(result, TJS_W("new Live2DMatrix()"));
}

static tjs_error GlesCompatCreateDeviceCb(tTJSVariant *result, tjs_int,
                                          tTJSVariant **, iTJSDispatch2 *) {
    return CreateGlesCompatObject(result, TJS_W("new Live2DDevice()"));
}

static tjs_error CreateGlesCompatModule(tTJSVariant *result, tjs_int width,
                                        tjs_int height) {
    iTJSDispatch2 *dict = TJSCreateDictionaryObject();
    if(!dict) {
        if(result)
            result->Clear();
        return TJS_E_FAIL;
    }

    tTJSVariant wv(width), hv(height);
    SetGlesCompatProperty(dict, TJS_W("screenWidth"), wv);
    SetGlesCompatProperty(dict, TJS_W("screenHeight"), hv);
    SetGlesCompatProperty(dict, TJS_W("renderCount"), tTJSVariant(0));

    SetGlesCompatMethod(dict, TJS_W("entryUpdateObject"),
                        GlesCompatEntryUpdateObjectCb);
    SetGlesCompatMethod(dict, TJS_W("setScreenSize"), GlesCompatReturnTrueCb);
    SetGlesCompatMethod(dict, TJS_W("makeCurrent"), GlesCompatReturnTrueCb);
    SetGlesCompatMethod(dict, TJS_W("beginScene"), GlesCompatReturnTrueCb);
    SetGlesCompatMethod(dict, TJS_W("endScene"), GlesCompatReturnTrueCb);
    SetGlesCompatMethod(dict, TJS_W("finalize"), GlesCompatGlesRemoveCb);
    SetGlesCompatMethod(dict, TJS_W("render"), GlesCompatRenderCb);
    SetGlesCompatMethod(dict, TJS_W("glesEntry"), GlesCompatGlesEntryCb);
    SetGlesCompatMethod(dict, TJS_W("glesRemove"), GlesCompatGlesRemoveCb);
    SetGlesCompatMethod(dict, TJS_W("capture"), GlesCompatCaptureCb);
    SetGlesCompatMethod(dict, TJS_W("captureScreen"), GlesCompatCaptureCb);
    SetGlesCompatMethod(dict, TJS_W("glesCapture"), GlesCompatCaptureCb);
    SetGlesCompatMethod(dict, TJS_W("glesCaptureScreen"), GlesCompatCaptureCb);
    SetGlesCompatMethod(dict, TJS_W("copyLayer"), GlesCompatCopyLayerCb);
    SetGlesCompatMethod(dict, TJS_W("glesCopyLayer"), GlesCompatCopyLayerCb);
    SetGlesCompatMethod(dict, TJS_W("drawLayer"), GlesCompatDrawLayerCb);
    SetGlesCompatMethod(dict, TJS_W("glesDrawLayer"), GlesCompatDrawLayerCb);
    SetGlesCompatMethod(dict, TJS_W("drawAffine"), GlesCompatDrawAffineCb);
    SetGlesCompatMethod(dict, TJS_W("drawAffineGLES"), GlesCompatDrawAffineCb);
    SetGlesCompatMethod(dict, TJS_W("setMatrix"), GlesCompatReturnTrueCb);
    SetGlesCompatMethod(dict, TJS_W("createModel"), GlesCompatCreateModelCb);
    SetGlesCompatMethod(dict, TJS_W("createMatrix"), GlesCompatCreateMatrixCb);
    SetGlesCompatMethod(dict, TJS_W("createDevice"), GlesCompatCreateDeviceCb);

    if(result)
        *result = tTJSVariant(dict, dict);
    dict->Release();
    return TJS_S_OK;
}

} // namespace

extern "C" tjs_error TVPKrkrGLESCreateModuleObject(tTJSVariant *result,
                                                   tjs_int width,
                                                   tjs_int height) {
    return CreateGlesCompatModule(result, width, height);
}

class GLESAdaptor {
public:
    GLESAdaptor() = default;

    tjs_int getScreenWidth() const { return screenWidth_; }
    void setScreenWidth(tjs_int value) { screenWidth_ = value; }
    tjs_int getScreenHeight() const { return screenHeight_; }
    void setScreenHeight(tjs_int value) { screenHeight_ = value; }
    tjs_int getRenderCount() const { return renderCount_; }
    void setRenderCount(tjs_int value) { renderCount_ = value; }

    static tjs_error noOpCb(tTJSVariant *result, tjs_int, tTJSVariant **,
                            GLESAdaptor *) {
        SetGlesCompatInt(result, 1);
        return TJS_S_OK;
    }

    static tjs_error entryUpdateObjectCb(tTJSVariant *result,
                                         tjs_int numparams,
                                         tTJSVariant **param,
                                         GLESAdaptor *self) {
        LogGlesCompatArgsOnce(TJS_W("adaptor.entryUpdateObject"),
                              numparams, param);
        GlesCompatRegisterRenderable(
            numparams, param, reinterpret_cast<uintptr_t>(self),
            TJS_W("adaptor.entryUpdateObject"));
        if(self)
            ++self->renderCount_;
        if(result)
            *result = true;
        return TJS_S_OK;
    }

    static tjs_error copyLayerCb(tTJSVariant *result, tjs_int numparams,
                                 tTJSVariant **param, GLESAdaptor *self) {
        LogGlesCompatArgsOnce(TJS_W("adaptor.copyLayer"), numparams, param);
        if(auto *layer = GlesCompatFindLayerInParams(numparams, param))
            g_glesCompatRegisteredLayer = layer;
        GlesCompatRegisterRenderable(
            numparams, param, reinterpret_cast<uintptr_t>(self),
            TJS_W("adaptor.copyLayer"));
        GlesCompatDrawLayerNative(numparams, param);
        GlesCompatRenderMotionPlayers(TJS_W("adaptor.copyLayer"));
        if(self)
            ++self->renderCount_;
        if(result)
            *result = true;
        return TJS_S_OK;
    }

    static tjs_error drawLayerCb(tTJSVariant *result, tjs_int numparams,
                                 tTJSVariant **param, GLESAdaptor *self) {
        LogGlesCompatArgsOnce(TJS_W("adaptor.drawLayer"), numparams, param);
        if(auto *layer = GlesCompatFindLayerInParams(numparams, param))
            g_glesCompatRegisteredLayer = layer;
        GlesCompatRegisterRenderable(
            numparams, param, reinterpret_cast<uintptr_t>(self),
            TJS_W("adaptor.drawLayer"));
        GlesCompatDrawLayerNative(numparams, param);
        GlesCompatRenderMotionPlayers(TJS_W("adaptor.drawLayer"));
        if(self)
            ++self->renderCount_;
        if(result)
            *result = true;
        return TJS_S_OK;
    }

    static tjs_error drawAffineCb(tTJSVariant *result, tjs_int numparams,
                                  tTJSVariant **param, GLESAdaptor *self) {
        LogGlesCompatArgsOnce(TJS_W("adaptor.drawAffine"), numparams, param);
        if(auto *layer = GlesCompatFindLayerInParams(numparams, param))
            g_glesCompatRegisteredLayer = layer;
        GlesCompatRegisterRenderable(
            numparams, param, reinterpret_cast<uintptr_t>(self),
            TJS_W("adaptor.drawAffine"));
        GlesCompatDrawAffineNative(numparams, param);
        GlesCompatRenderMotionPlayers(TJS_W("adaptor.drawAffine"));
        if(self)
            ++self->renderCount_;
        if(result)
            *result = true;
        return TJS_S_OK;
    }

    static tjs_error renderCb(tTJSVariant *result, tjs_int, tTJSVariant **,
                              GLESAdaptor *self) {
        GlesCompatRenderMotionPlayers(TJS_W("adaptor.render"));
        if(self)
            ++self->renderCount_;
        if(result)
            *result = true;
        return TJS_S_OK;
    }

    static tjs_error captureCb(tTJSVariant *result, tjs_int numparams,
                               tTJSVariant **param, GLESAdaptor *self) {
        LogGlesCompatArgsOnce(TJS_W("adaptor.capture"), numparams, param);
        iTJSDispatch2 *layerDispatch =
            GlesCompatFindLayerInParams(numparams, param);
        if(layerDispatch)
            g_glesCompatRegisteredLayer = layerDispatch;
	    GlesCompatRegisterRenderable(
	        numparams, param, reinterpret_cast<uintptr_t>(self),
	        TJS_W("adaptor.capture"));
	    GlesCompatRenderMotionPlayers(TJS_W("adaptor.capture"));
	    if(layerDispatch)
	        GlesCompatRenderGodotLive2D(layerDispatch);
	    if(self)
	        ++self->renderCount_;
        if(result) {
            if(numparams > 0 && param && param[0])
                *result = *param[0];
            else
                *result = true;
        }
        return TJS_S_OK;
    }

    static tjs_error finalizeCb(tTJSVariant *result, tjs_int,
                                tTJSVariant **, GLESAdaptor *self) {
        GlesCompatRemoveRenderable(0, nullptr,
                                   reinterpret_cast<uintptr_t>(self));
        if(result)
            *result = true;
        return TJS_S_OK;
    }

    static tjs_error glesEntryCb(tTJSVariant *result, tjs_int numparams,
                                 tTJSVariant **param, GLESAdaptor *self) {
        LogGlesCompatArgsOnce(TJS_W("adaptor.glesEntry"), numparams, param);
        GlesCompatRegisterRenderable(
            numparams, param, reinterpret_cast<uintptr_t>(self),
            TJS_W("adaptor.glesEntry"));
        if(result)
            *result = true;
        return TJS_S_OK;
    }

    static tjs_error glesRemoveCb(tTJSVariant *result, tjs_int numparams,
                                  tTJSVariant **param, GLESAdaptor *self) {
        GlesCompatRemoveRenderable(
            numparams, param, reinterpret_cast<uintptr_t>(self));
        if(result)
            *result = true;
        return TJS_S_OK;
    }

    static tjs_error getModuleCb(tTJSVariant *result, tjs_int, tTJSVariant **,
                                 GLESAdaptor *self) {
        const tjs_int width = self ? self->screenWidth_ : 0;
        const tjs_int height = self ? self->screenHeight_ : 0;
        return CreateGlesCompatModule(result, width, height);
    }

    static tjs_error setScreenSizeCb(tTJSVariant *result, tjs_int numparams,
                                     tTJSVariant **param, GLESAdaptor *self) {
        if(self && numparams >= 2) {
            self->screenWidth_ = static_cast<tjs_int>(*param[0]);
            self->screenHeight_ = static_cast<tjs_int>(*param[1]);
        }
        SetGlesCompatInt(result, 1);
        return TJS_S_OK;
    }

    static tjs_error createModelCb(tTJSVariant *result, tjs_int numparams,
                                   tTJSVariant **param, GLESAdaptor *) {
        tTJSVariant model;
        tjs_error er = CreateGlesCompatObject(&model, TJS_W("new Live2DModel()"));
        if(TJS_FAILED(er) || model.Type() != tvtObject) {
            if(result)
                result->Clear();
            return TJS_FAILED(er) ? er : TJS_E_FAIL;
        }
        GlesCompatInvokeLoadIfPresent(model, numparams, param);
        if(result)
            *result = model;
        return TJS_S_OK;
    }

    static tjs_error createMatrixCb(tTJSVariant *result, tjs_int,
                                    tTJSVariant **, GLESAdaptor *) {
        return CreateGlesCompatObject(result, TJS_W("new Live2DMatrix()"));
    }

    static tjs_error createDeviceCb(tTJSVariant *result, tjs_int,
                                    tTJSVariant **, GLESAdaptor *) {
        return CreateGlesCompatObject(result, TJS_W("new Live2DDevice()"));
    }

private:
    tjs_int screenWidth_ = 0;
    tjs_int screenHeight_ = 0;
    tjs_int renderCount_ = 0;
};

class OGLDrawDevice {
public:
    OGLDrawDevice() = default;

    tjs_int getScreenWidth() const { return adaptor_.getScreenWidth(); }
    void setScreenWidth(tjs_int value) { adaptor_.setScreenWidth(value); }
    tjs_int getScreenHeight() const { return adaptor_.getScreenHeight(); }
    void setScreenHeight(tjs_int value) { adaptor_.setScreenHeight(value); }
    tjs_int getRenderCount() const { return adaptor_.getRenderCount(); }
    void setRenderCount(tjs_int value) { adaptor_.setRenderCount(value); }

    static tjs_error noOpCb(tTJSVariant *result, tjs_int, tTJSVariant **,
                            OGLDrawDevice *) {
        SetGlesCompatInt(result, 1);
        return TJS_S_OK;
    }

    static tjs_error entryUpdateObjectCb(tTJSVariant *result,
                                         tjs_int numparams,
                                         tTJSVariant **param,
                                         OGLDrawDevice *self) {
        return GLESAdaptor::entryUpdateObjectCb(
            result, numparams, param, self ? &self->adaptor_ : nullptr);
    }

    static tjs_error copyLayerCb(tTJSVariant *result, tjs_int numparams,
                                 tTJSVariant **param, OGLDrawDevice *self) {
        return GLESAdaptor::copyLayerCb(
            result, numparams, param, self ? &self->adaptor_ : nullptr);
    }

    static tjs_error drawLayerCb(tTJSVariant *result, tjs_int numparams,
                                 tTJSVariant **param, OGLDrawDevice *self) {
        return GLESAdaptor::drawLayerCb(
            result, numparams, param, self ? &self->adaptor_ : nullptr);
    }

    static tjs_error drawAffineCb(tTJSVariant *result, tjs_int numparams,
                                  tTJSVariant **param, OGLDrawDevice *self) {
        return GLESAdaptor::drawAffineCb(
            result, numparams, param, self ? &self->adaptor_ : nullptr);
    }

    static tjs_error renderCb(tTJSVariant *result, tjs_int numparams,
                              tTJSVariant **param, OGLDrawDevice *self) {
        return GLESAdaptor::renderCb(
            result, numparams, param, self ? &self->adaptor_ : nullptr);
    }

    static tjs_error captureCb(tTJSVariant *result, tjs_int numparams,
                               tTJSVariant **param, OGLDrawDevice *self) {
        return GLESAdaptor::captureCb(
            result, numparams, param, self ? &self->adaptor_ : nullptr);
    }

    static tjs_error finalizeCb(tTJSVariant *result, tjs_int numparams,
                                tTJSVariant **param, OGLDrawDevice *self) {
        return GLESAdaptor::finalizeCb(
            result, numparams, param, self ? &self->adaptor_ : nullptr);
    }

    static tjs_error glesEntryCb(tTJSVariant *result, tjs_int numparams,
                                 tTJSVariant **param, OGLDrawDevice *self) {
        return GLESAdaptor::glesEntryCb(
            result, numparams, param, self ? &self->adaptor_ : nullptr);
    }

    static tjs_error glesRemoveCb(tTJSVariant *result, tjs_int numparams,
                                  tTJSVariant **param, OGLDrawDevice *self) {
        return GLESAdaptor::glesRemoveCb(
            result, numparams, param, self ? &self->adaptor_ : nullptr);
    }

    static tjs_error getModuleCb(tTJSVariant *result, tjs_int numparams,
                                 tTJSVariant **param, OGLDrawDevice *self) {
        return GLESAdaptor::getModuleCb(result, numparams, param,
                                        self ? &self->adaptor_ : nullptr);
    }

    static tjs_error setScreenSizeCb(tTJSVariant *result, tjs_int numparams,
                                     tTJSVariant **param, OGLDrawDevice *self) {
        if(self && numparams >= 2) {
            self->setScreenWidth(static_cast<tjs_int>(*param[0]));
            self->setScreenHeight(static_cast<tjs_int>(*param[1]));
        }
        SetGlesCompatInt(result, 1);
        return TJS_S_OK;
    }

    static tjs_error createModelCb(tTJSVariant *result, tjs_int numparams,
                                   tTJSVariant **param, OGLDrawDevice *) {
        return GLESAdaptor::createModelCb(result, numparams, param, nullptr);
    }

    static tjs_error createMatrixCb(tTJSVariant *result, tjs_int numparams,
                                    tTJSVariant **param, OGLDrawDevice *) {
        return GLESAdaptor::createMatrixCb(result, numparams, param, nullptr);
    }

    static tjs_error createDeviceCb(tTJSVariant *result, tjs_int numparams,
                                    tTJSVariant **param, OGLDrawDevice *) {
        return GLESAdaptor::createDeviceCb(result, numparams, param, nullptr);
    }

private:
    GLESAdaptor adaptor_;
};

NCB_REGISTER_CLASS(GLESAdaptor) {
    Constructor();
    NCB_PROPERTY(screenWidth, getScreenWidth, setScreenWidth);
    NCB_PROPERTY(screenHeight, getScreenHeight, setScreenHeight);
    NCB_PROPERTY(renderCount, getRenderCount, setRenderCount);
    NCB_METHOD_RAW_CALLBACK(getModule, &GLESAdaptor::getModuleCb, 0);
    NCB_METHOD_RAW_CALLBACK(setScreenSize, &GLESAdaptor::setScreenSizeCb, 0);
    NCB_METHOD_RAW_CALLBACK(makeCurrent, &GLESAdaptor::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(beginScene, &GLESAdaptor::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(endScene, &GLESAdaptor::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(entryUpdateObject, &GLESAdaptor::entryUpdateObjectCb, 0);
    NCB_METHOD_RAW_CALLBACK(capture, &GLESAdaptor::captureCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCapture, &GLESAdaptor::captureCb, 0);
    NCB_METHOD_RAW_CALLBACK(captureScreen, &GLESAdaptor::captureCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCaptureScreen, &GLESAdaptor::captureCb, 0);
    NCB_METHOD_RAW_CALLBACK(copyLayer, &GLESAdaptor::copyLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCopyLayer, &GLESAdaptor::copyLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawLayer, &GLESAdaptor::drawLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesDrawLayer, &GLESAdaptor::drawLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawAffine, &GLESAdaptor::drawAffineCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawAffineGLES, &GLESAdaptor::drawAffineCb, 0);
    NCB_METHOD_RAW_CALLBACK(render, &GLESAdaptor::renderCb, 0);
    NCB_METHOD_RAW_CALLBACK(setMatrix, &GLESAdaptor::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(createModel, &GLESAdaptor::createModelCb, 0);
    NCB_METHOD_RAW_CALLBACK(createMatrix, &GLESAdaptor::createMatrixCb, 0);
    NCB_METHOD_RAW_CALLBACK(createDevice, &GLESAdaptor::createDeviceCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesEntry, &GLESAdaptor::glesEntryCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesRemove, &GLESAdaptor::glesRemoveCb, 0);
    NCB_METHOD_RAW_CALLBACK(finalize, &GLESAdaptor::finalizeCb, 0);
}

NCB_REGISTER_CLASS(OGLDrawDevice) {
    Constructor();
    NCB_PROPERTY(screenWidth, getScreenWidth, setScreenWidth);
    NCB_PROPERTY(screenHeight, getScreenHeight, setScreenHeight);
    NCB_PROPERTY(renderCount, getRenderCount, setRenderCount);
    NCB_METHOD_RAW_CALLBACK(getModule, &OGLDrawDevice::getModuleCb, 0);
    NCB_METHOD_RAW_CALLBACK(setScreenSize, &OGLDrawDevice::setScreenSizeCb, 0);
    NCB_METHOD_RAW_CALLBACK(makeCurrent, &OGLDrawDevice::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(beginScene, &OGLDrawDevice::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(endScene, &OGLDrawDevice::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(entryUpdateObject, &OGLDrawDevice::entryUpdateObjectCb, 0);
    NCB_METHOD_RAW_CALLBACK(capture, &OGLDrawDevice::captureCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCapture, &OGLDrawDevice::captureCb, 0);
    NCB_METHOD_RAW_CALLBACK(captureScreen, &OGLDrawDevice::captureCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCaptureScreen, &OGLDrawDevice::captureCb, 0);
    NCB_METHOD_RAW_CALLBACK(copyLayer, &OGLDrawDevice::copyLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCopyLayer, &OGLDrawDevice::copyLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawLayer, &OGLDrawDevice::drawLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesDrawLayer, &OGLDrawDevice::drawLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawAffine, &OGLDrawDevice::drawAffineCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawAffineGLES, &OGLDrawDevice::drawAffineCb, 0);
    NCB_METHOD_RAW_CALLBACK(render, &OGLDrawDevice::renderCb, 0);
    NCB_METHOD_RAW_CALLBACK(setMatrix, &OGLDrawDevice::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(createModel, &OGLDrawDevice::createModelCb, 0);
    NCB_METHOD_RAW_CALLBACK(createMatrix, &OGLDrawDevice::createMatrixCb, 0);
    NCB_METHOD_RAW_CALLBACK(createDevice, &OGLDrawDevice::createDeviceCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesEntry, &OGLDrawDevice::glesEntryCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesRemove, &OGLDrawDevice::glesRemoveCb, 0);
    NCB_METHOD_RAW_CALLBACK(finalize, &OGLDrawDevice::finalizeCb, 0);
}

static tjs_error GlesCompatDrawDeviceGetModuleCb(tTJSVariant *result,
                                                 tjs_int, tTJSVariant **,
                                                 iTJSDispatch2 *) {
    return CreateGlesCompatModule(result, 0, 0);
}

static void GlesCompatPostRegist() {
    try {
        TVPExecuteExpression(
            TJS_W("try { Window.OGLDrawDevice = OGLDrawDevice; } catch(e) { }\n")
            TJS_W("try { Window.GLESAdaptor = GLESAdaptor; } catch(e) { }\n")
            TJS_W("try { KAGWindow.KAGWindow_createDrawDevice = KAGWindow_createDrawDevice; } catch(e) { }\n")
            TJS_W("try { KAGWindow.prototype.KAGWindow_createDrawDevice = KAGWindow_createDrawDevice; } catch(e) { }\n"),
            static_cast<tTJSVariant *>(nullptr));
    } catch(...) {
    }
    GlesCompatRenderHook().Start();
}

static void GlesCompatPreUnregist() {
    GlesCompatRenderHook().Stop();
    {
        std::lock_guard<std::mutex> lock(GlesCompatRenderMutex());
        GlesCompatRenderables().clear();
    }
    g_glesCompatRegisteredLayer = nullptr;
}

NCB_POST_REGIST_CALLBACK(GlesCompatPostRegist);
NCB_PRE_UNREGIST_CALLBACK(GlesCompatPreUnregist);

NCB_ATTACH_FUNCTION_WITHTAG(getModule, WindowPassThroughDrawDeviceGlesCompat,
                            Window.PassThroughDrawDevice,
                            GlesCompatDrawDeviceGetModuleCb);
NCB_ATTACH_FUNCTION_WITHTAG(getModule, WindowBasicDrawDeviceGlesCompat,
                            Window.BasicDrawDevice,
                            GlesCompatDrawDeviceGetModuleCb);
#endif

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("gfxEffect.dll")
class gfxFire {
public:
    gfxFire() { TVPAddLog(TJS_W("gfxFire construct")); }
    void finalize() { TVPAddLog(TJS_W("gfxFire finalize")); }
};
NCB_REGISTER_CLASS(gfxFire) {
    Constructor();
    NCB_METHOD(finalize);
}

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("flashPlayer.dll")
class FlashPlayer {
public:
    FlashPlayer() = default;
    FlashPlayer(tjs_int, tjs_int) {}

    void loadMovie(tjs_int, const tjs_char *) {}
    void tGotoFrame(tjs_int) {}
    void tGotoLabel(const tjs_char *) {}
    tjs_int tCurrentFrame() const { return 0; }
    ttstr tCurrentLabel() const { return ttstr(); }
    void tPlay() { playing_ = true; }
    void tStopPlay() { playing_ = false; }
    void setVariable(const tjs_char *, const tjs_char *) {}
    ttstr getVariable(const tjs_char *) const { return ttstr(); }
    void tSetProperty(const tjs_char *, tjs_int) {}
    ttstr tGetProperty(const tjs_char *) const { return ttstr(); }
    void tCallFrame(tjs_int) {}
    void tCallLabel(const tjs_char *) {}
    void tSetPropertyNum(const tjs_char *, tjs_int) {}
    tjs_int tGetPropertyNum(const tjs_char *) const { return 0; }
    void enforceLocalSecurity() {}
    void disableLocalSecurity() {}

    tjs_int getReadyState() const { return 0; }
    tjs_int getTotalFrames() const { return 0; }
    bool getPlaying() const { return playing_; }
    void setPlaying(bool value) { playing_ = value; }
    tjs_int getQuality() const { return quality_; }
    void setQuality(tjs_int value) { quality_ = value; }
    tjs_int getScaleMode() const { return scaleMode_; }
    void setScaleMode(tjs_int value) { scaleMode_ = value; }
    tjs_int getAlignMode() const { return alignMode_; }
    void setAlignMode(tjs_int value) { alignMode_ = value; }
    ttstr getMovie() const { return movie_; }
    void setMovie(const tjs_char *value) { movie_ = value ? value : TJS_W(""); }
    ttstr getWMode() const { return wmode_; }
    void setWMode(const tjs_char *value) { wmode_ = value ? value : TJS_W(""); }
    ttstr getFlashVars() const { return flashVars_; }
    void setFlashVars(const tjs_char *value) {
        flashVars_ = value ? value : TJS_W("");
    }

private:
    bool playing_ = false;
    tjs_int quality_ = 0;
    tjs_int scaleMode_ = 0;
    tjs_int alignMode_ = 0;
    ttstr movie_;
    ttstr wmode_;
    ttstr flashVars_;
};

NCB_REGISTER_CLASS(FlashPlayer) {
    Constructor();
    NCB_CONSTRUCTOR((tjs_int, tjs_int));

    NCB_PROPERTY_RO(readyState, getReadyState);
    NCB_PROPERTY_RO(totalFrames, getTotalFrames);
    NCB_PROPERTY(playing, getPlaying, setPlaying);
    NCB_PROPERTY(quality, getQuality, setQuality);
    NCB_PROPERTY(scaleMode, getScaleMode, setScaleMode);
    NCB_PROPERTY(alignMode, getAlignMode, setAlignMode);
    NCB_PROPERTY(movie, getMovie, setMovie);
    NCB_PROPERTY(wMode, getWMode, setWMode);
    NCB_PROPERTY(flashVars, getFlashVars, setFlashVars);

    NCB_METHOD(loadMovie);
    NCB_METHOD(tGotoFrame);
    NCB_METHOD(tGotoLabel);
    NCB_METHOD(tCurrentFrame);
    NCB_METHOD(tCurrentLabel);
    NCB_METHOD(tPlay);
    NCB_METHOD(tStopPlay);
    NCB_METHOD(setVariable);
    NCB_METHOD(getVariable);
    NCB_METHOD(tSetProperty);
    NCB_METHOD(tGetProperty);
    NCB_METHOD(tCallFrame);
    NCB_METHOD(tCallLabel);
    NCB_METHOD(tSetPropertyNum);
    NCB_METHOD(tGetPropertyNum);
    NCB_METHOD(enforceLocalSecurity);
    NCB_METHOD(disableLocalSecurity);
}

#define REGISTER_EMPTY_PLUGIN(id, module) \
    static void id##_stub() {} \
    NCB_PRE_REGIST_CALLBACK(id##_stub)

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("htmlhelp.dll")
REGISTER_EMPTY_PLUGIN(htmlhelp, htmlhelp);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("httprequest.dll")
REGISTER_EMPTY_PLUGIN(httprequest, httprequest);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("drawdevice.dll")
REGISTER_EMPTY_PLUGIN(drawdevice, drawdevice);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("drawdeviceD3D.dll")
static void drawdeviceD3D_init() {
    try {
        ncbAutoRegister::LoadModule(TJS_W("emoteplayer.dll"));
    } catch(...) {
    }
}
NCB_PRE_REGIST_CALLBACK(drawdeviceD3D_init);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("drawdeviceIrrlicht.dll")
REGISTER_EMPTY_PLUGIN(drawdeviceIrrlicht, drawdeviceIrrlicht);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("drawdeviceOgre.dll")
REGISTER_EMPTY_PLUGIN(drawdeviceOgre, drawdeviceOgre);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("drawdeviceZ_D3D9.dll")
REGISTER_EMPTY_PLUGIN(drawdeviceZ_D3D9, drawdeviceZ_D3D9);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("gameswf.dll")
REGISTER_EMPTY_PLUGIN(gameswf, gameswf);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("httpserv.dll")
REGISTER_EMPTY_PLUGIN(httpserv, httpserv);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("javascript.dll")
REGISTER_EMPTY_PLUGIN(javascript, javascript);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("layerEx.dll")
REGISTER_EMPTY_PLUGIN(layerEx, layerEx);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("xmlhttprequest.dll")
REGISTER_EMPTY_PLUGIN(xmlhttprequest, xmlhttprequest);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("msgreceiver.dll")
REGISTER_EMPTY_PLUGIN(msgreceiver, msgreceiver);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("messenger.dll")
REGISTER_EMPTY_PLUGIN(messenger, messenger);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("oleclass.dll")
REGISTER_EMPTY_PLUGIN(oleclass, oleclass);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("registory.dll")
REGISTER_EMPTY_PLUGIN(registory, registory);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("resourceRW.dll")
REGISTER_EMPTY_PLUGIN(resourceRW, resourceRW);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("sigcheck.dll")
REGISTER_EMPTY_PLUGIN(sigcheck, sigcheck);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("stdio.dll")
REGISTER_EMPTY_PLUGIN(stdio, stdio);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("tftSave.dll")
REGISTER_EMPTY_PLUGIN(tftSave, tftSave);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("videoEncoder.dll")
REGISTER_EMPTY_PLUGIN(videoEncoder, videoEncoder);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("windowExProgress.dll")
REGISTER_EMPTY_PLUGIN(windowExProgress, windowExProgress);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("wmrdump.dll")
REGISTER_EMPTY_PLUGIN(wmrdump, wmrdump);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("wsh.dll")
REGISTER_EMPTY_PLUGIN(wsh, wsh);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("wumsadp.dll")
REGISTER_EMPTY_PLUGIN(wumsadp, wumsadp);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("layerExAgg.dll")
REGISTER_EMPTY_PLUGIN(layerExAgg, layerExAgg);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("layerExCairo.dll")
REGISTER_EMPTY_PLUGIN(layerExCairo, layerExCairo);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("layerExGdiPlus.dll")
REGISTER_EMPTY_PLUGIN(layerExGdiPlus, layerExGdiPlus);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("magickpp.dll")
REGISTER_EMPTY_PLUGIN(magickpp, magickpp);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("mkpj.dll")
REGISTER_EMPTY_PLUGIN(mkpj, mkpj);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("onigruma.dll")
REGISTER_EMPTY_PLUGIN(onigruma, onigruma);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("squirrel.dll")
REGISTER_EMPTY_PLUGIN(squirrel, squirrel);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("xpressive.dll")
REGISTER_EMPTY_PLUGIN(xpressive, xpressive);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("zlib.dll")
REGISTER_EMPTY_PLUGIN(zlib, zlib);

#undef REGISTER_EMPTY_PLUGIN
