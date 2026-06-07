#include "ncbind.hpp"
#include "DebugIntf.h"
#include "ScriptMgnIntf.h"

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

static tjs_error GlesCompatEntryUpdateObjectCb(tTJSVariant *result,
                                               tjs_int numparams,
                                               tTJSVariant **param,
                                               iTJSDispatch2 *) {
    LogGlesCompatArgsOnce(TJS_W("entryUpdateObject"), numparams, param);
    if(result)
        *result = true;
    return TJS_S_OK;
}

static tjs_error GlesCompatCopyLayerCb(tTJSVariant *result, tjs_int numparams,
                                       tTJSVariant **param, iTJSDispatch2 *) {
    LogGlesCompatArgsOnce(TJS_W("copyLayer"), numparams, param);
    if(result)
        *result = true;
    return TJS_S_OK;
}

static tjs_error GlesCompatDrawAffineCb(tTJSVariant *result, tjs_int numparams,
                                        tTJSVariant **param, iTJSDispatch2 *) {
    LogGlesCompatArgsOnce(TJS_W("drawAffine"), numparams, param);
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
    SetGlesCompatMethod(dict, TJS_W("finalize"), GlesCompatReturnTrueCb);
    SetGlesCompatMethod(dict, TJS_W("render"), GlesCompatReturnTrueCb);
    SetGlesCompatMethod(dict, TJS_W("glesEntry"), GlesCompatReturnTrueCb);
    SetGlesCompatMethod(dict, TJS_W("glesRemove"), GlesCompatReturnTrueCb);
    SetGlesCompatMethod(dict, TJS_W("capture"), GlesCompatReturnFirstArgOrTrueCb);
    SetGlesCompatMethod(dict, TJS_W("captureScreen"),
                        GlesCompatReturnFirstArgOrTrueCb);
    SetGlesCompatMethod(dict, TJS_W("glesCapture"),
                        GlesCompatReturnFirstArgOrTrueCb);
    SetGlesCompatMethod(dict, TJS_W("glesCaptureScreen"),
                        GlesCompatReturnFirstArgOrTrueCb);
    SetGlesCompatMethod(dict, TJS_W("copyLayer"), GlesCompatCopyLayerCb);
    SetGlesCompatMethod(dict, TJS_W("glesCopyLayer"), GlesCompatCopyLayerCb);
    SetGlesCompatMethod(dict, TJS_W("drawLayer"), GlesCompatReturnTrueCb);
    SetGlesCompatMethod(dict, TJS_W("glesDrawLayer"), GlesCompatReturnTrueCb);
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
    NCB_METHOD_RAW_CALLBACK(entryUpdateObject, &GLESAdaptor::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(capture, &GLESAdaptor::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCapture, &GLESAdaptor::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(captureScreen, &GLESAdaptor::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCaptureScreen, &GLESAdaptor::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(copyLayer, &GLESAdaptor::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCopyLayer, &GLESAdaptor::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawLayer, &GLESAdaptor::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesDrawLayer, &GLESAdaptor::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawAffine, &GLESAdaptor::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawAffineGLES, &GLESAdaptor::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(render, &GLESAdaptor::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(setMatrix, &GLESAdaptor::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(createModel, &GLESAdaptor::createModelCb, 0);
    NCB_METHOD_RAW_CALLBACK(createMatrix, &GLESAdaptor::createMatrixCb, 0);
    NCB_METHOD_RAW_CALLBACK(createDevice, &GLESAdaptor::createDeviceCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesEntry, &GLESAdaptor::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesRemove, &GLESAdaptor::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(finalize, &GLESAdaptor::noOpCb, 0);
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
    NCB_METHOD_RAW_CALLBACK(entryUpdateObject, &OGLDrawDevice::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(capture, &OGLDrawDevice::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCapture, &OGLDrawDevice::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(captureScreen, &OGLDrawDevice::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCaptureScreen, &OGLDrawDevice::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(copyLayer, &OGLDrawDevice::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesCopyLayer, &OGLDrawDevice::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawLayer, &OGLDrawDevice::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesDrawLayer, &OGLDrawDevice::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawAffine, &OGLDrawDevice::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(drawAffineGLES, &OGLDrawDevice::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(render, &OGLDrawDevice::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(setMatrix, &OGLDrawDevice::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(createModel, &OGLDrawDevice::createModelCb, 0);
    NCB_METHOD_RAW_CALLBACK(createMatrix, &OGLDrawDevice::createMatrixCb, 0);
    NCB_METHOD_RAW_CALLBACK(createDevice, &OGLDrawDevice::createDeviceCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesEntry, &OGLDrawDevice::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(glesRemove, &OGLDrawDevice::noOpCb, 0);
    NCB_METHOD_RAW_CALLBACK(finalize, &OGLDrawDevice::noOpCb, 0);
}
#endif

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("gfxEffect.dll")
class gfxFire {
public:
    gfxFire() = default;
};
NCB_REGISTER_CLASS(gfxFire) {
    Constructor();
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
REGISTER_EMPTY_PLUGIN(drawdeviceD3D, drawdeviceD3D);

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
#define NCB_MODULE_NAME TJS_W("shrinkCopy.dll")
REGISTER_EMPTY_PLUGIN(shrinkCopy, shrinkCopy);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("sigcheck.dll")
REGISTER_EMPTY_PLUGIN(sigcheck, sigcheck);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("sqlite3_xp3_vfs.dll")
REGISTER_EMPTY_PLUGIN(sqlite3_xp3_vfs, sqlite3_xp3_vfs);

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
