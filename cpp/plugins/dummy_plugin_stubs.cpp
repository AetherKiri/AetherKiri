#include "ncbind.hpp"
#include "DebugIntf.h"
#include "EventIntf.h"
#include "LayerImpl.h"
#include "ScriptMgnIntf.h"
#include "motionplayer/Player.h"

#include <algorithm>
#include <mutex>
#include <vector>

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

static iTJSDispatch2 *GlesCompatResolveLayerDispatch(const tTJSVariant &value,
                                                     int depth = 0) {
    if(depth > 4 || value.Type() != tvtObject || !value.AsObjectNoAddRef())
        return nullptr;

    iTJSDispatch2 *object = value.AsObjectNoAddRef();
    if(GlesCompatIsLayerDispatch(object))
        return object;

    static const tjs_char *kLayerProps[] = {
        TJS_W("owner"), TJS_W("_owner"), TJS_W("targetLayer"),
        TJS_W("layer"), TJS_W("_layer"), TJS_W("baseLayer"),
        TJS_W("_base"), TJS_W("base"), TJS_W("fore"),
        TJS_W("back"), TJS_W("primaryLayer"), TJS_W("parent"),
        nullptr
    };

    for(int i = 0; kLayerProps[i]; ++i) {
        tTJSVariant prop;
        if(!GlesCompatGetObjectProperty(value, kLayerProps[i], prop) ||
           prop.Type() != tvtObject || !prop.AsObjectNoAddRef() ||
           prop.AsObjectNoAddRef() == object) {
            continue;
        }
        if(auto *resolved = GlesCompatResolveLayerDispatch(prop, depth + 1))
            return resolved;
    }
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
    iTJSDispatch2 *defaultLayer = GlesCompatDefaultLayer();
    for(const auto &item : snapshot) {
        iTJSDispatch2 *player = item.player.Type() == tvtObject
            ? item.player.AsObjectNoAddRef()
            : nullptr;
        iTJSDispatch2 *layer = item.layer.Type() == tvtObject
            ? item.layer.AsObjectNoAddRef()
            : defaultLayer;
        if(GlesCompatInvokeMotionDraw(player, layer, tag))
            ++rendered;
    }

    if(snapshot.empty() && defaultLayer) {
        for(const auto &playerVar :
            motion::SnapshotAutoProgressPlayerDispatchesForCompat()) {
            iTJSDispatch2 *player = playerVar.Type() == tvtObject
                ? playerVar.AsObjectNoAddRef()
                : nullptr;
            if(GlesCompatInvokeMotionDraw(player, defaultLayer, tag))
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
}

static tjs_error GlesCompatEntryUpdateObjectCb(tTJSVariant *result,
                                               tjs_int numparams,
                                               tTJSVariant **param,
                                               iTJSDispatch2 *objthis) {
    LogGlesCompatArgsOnce(TJS_W("entryUpdateObject"), numparams, param);
    GlesCompatRegisterRenderable(
        numparams, param, reinterpret_cast<uintptr_t>(objthis),
        TJS_W("entryUpdateObject"));
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
    GlesCompatRenderMotionPlayers(TJS_W("copyLayer"));
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
    GlesCompatRenderMotionPlayers(TJS_W("drawAffine"));
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
    GlesCompatRenderMotionPlayers(TJS_W("drawLayer"));
    if(result)
        *result = true;
    return TJS_S_OK;
}

static tjs_error GlesCompatRenderCb(tTJSVariant *result, tjs_int,
                                    tTJSVariant **, iTJSDispatch2 *) {
    GlesCompatRenderMotionPlayers(TJS_W("render"));
    if(result)
        *result = true;
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
    dict->PropSet(TJS_MEMBERENSURE, TJS_W("screenWidth"), nullptr, &wv, dict);
    dict->PropSet(TJS_MEMBERENSURE, TJS_W("screenHeight"), nullptr, &hv, dict);

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
    SetGlesCompatMethod(dict, TJS_W("capture"), GlesCompatReturnFirstArgOrTrueCb);
    SetGlesCompatMethod(dict, TJS_W("captureScreen"),
                        GlesCompatReturnFirstArgOrTrueCb);
    SetGlesCompatMethod(dict, TJS_W("glesCapture"),
                        GlesCompatReturnFirstArgOrTrueCb);
    SetGlesCompatMethod(dict, TJS_W("glesCaptureScreen"),
                        GlesCompatReturnFirstArgOrTrueCb);
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

class GLESAdaptor {
public:
    GLESAdaptor() = default;

    tjs_int getScreenWidth() const { return screenWidth_; }
    void setScreenWidth(tjs_int value) { screenWidth_ = value; }
    tjs_int getScreenHeight() const { return screenHeight_; }
    void setScreenHeight(tjs_int value) { screenHeight_ = value; }

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
        GlesCompatRenderMotionPlayers(TJS_W("adaptor.copyLayer"));
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
        GlesCompatRenderMotionPlayers(TJS_W("adaptor.drawLayer"));
        if(result)
            *result = true;
        return TJS_S_OK;
    }

    static tjs_error renderCb(tTJSVariant *result, tjs_int, tTJSVariant **,
                              GLESAdaptor *) {
        GlesCompatRenderMotionPlayers(TJS_W("adaptor.render"));
        if(result)
            *result = true;
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
};

class OGLDrawDevice {
public:
    OGLDrawDevice() = default;

    tjs_int getScreenWidth() const { return adaptor_.getScreenWidth(); }
    void setScreenWidth(tjs_int value) { adaptor_.setScreenWidth(value); }
    tjs_int getScreenHeight() const { return adaptor_.getScreenHeight(); }
    void setScreenHeight(tjs_int value) { adaptor_.setScreenHeight(value); }

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

    static tjs_error renderCb(tTJSVariant *result, tjs_int numparams,
                              tTJSVariant **param, OGLDrawDevice *self) {
        return GLESAdaptor::renderCb(
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
    NCB_METHOD_RAW_CALLBACK(getModule, &GLESAdaptor::getModuleCb, 0);
    NCB_METHOD_RAW_CALLBACK(setScreenSize, &GLESAdaptor::setScreenSizeCb, 0);
    NCB_METHOD_RAW_CALLBACK(makeCurrent, &GLESAdaptor::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(beginScene, &GLESAdaptor::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(endScene, &GLESAdaptor::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(entryUpdateObject, &GLESAdaptor::entryUpdateObjectCb, 0);
    NCB_METHOD_RAW_CALLBACK(capture, &GLESAdaptor::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCapture, &GLESAdaptor::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(captureScreen, &GLESAdaptor::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCaptureScreen, &GLESAdaptor::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(copyLayer, &GLESAdaptor::copyLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCopyLayer, &GLESAdaptor::copyLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawLayer, &GLESAdaptor::drawLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesDrawLayer, &GLESAdaptor::drawLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawAffine, &GLESAdaptor::drawLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawAffineGLES, &GLESAdaptor::drawLayerCb, 0);
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
    NCB_METHOD_RAW_CALLBACK(getModule, &OGLDrawDevice::getModuleCb, 0);
    NCB_METHOD_RAW_CALLBACK(setScreenSize, &OGLDrawDevice::setScreenSizeCb, 0);
    NCB_METHOD_RAW_CALLBACK(makeCurrent, &OGLDrawDevice::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(beginScene, &OGLDrawDevice::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(endScene, &OGLDrawDevice::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(entryUpdateObject, &OGLDrawDevice::entryUpdateObjectCb, 0);
    NCB_METHOD_RAW_CALLBACK(capture, &OGLDrawDevice::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCapture, &OGLDrawDevice::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(captureScreen, &OGLDrawDevice::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCaptureScreen, &OGLDrawDevice::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(copyLayer, &OGLDrawDevice::copyLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCopyLayer, &OGLDrawDevice::copyLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawLayer, &OGLDrawDevice::drawLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesDrawLayer, &OGLDrawDevice::drawLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawAffine, &OGLDrawDevice::drawLayerCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawAffineGLES, &OGLDrawDevice::drawLayerCb, 0);
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

NCB_POST_REGIST_CALLBACK(GlesCompatPostRegist);

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
