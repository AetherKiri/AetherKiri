#include "DebugIntf.h"
#include "ncbind.hpp"

#include <map>

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

namespace {

void logCompatOnce(const tjs_char *module, const tjs_char *message) {
    static std::map<ttstr, bool> emitted;
    const ttstr key = ttstr(module) + TJS_W(":") + message;
    if(emitted[key])
        return;
    emitted[key] = true;
    TVPAddLog(ttstr(TJS_W("AetherKiri compat plugin ")) + module + TJS_W(": ") +
              message);
}

tjs_error TJS_INTF_METHOD trueCb(tTJSVariant *result, tjs_int,
                                 tTJSVariant **, iTJSDispatch2 *) {
    if(result)
        *result = true;
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD voidCb(tTJSVariant *result, tjs_int, tTJSVariant **,
                                 iTJSDispatch2 *) {
    if(result)
        result->Clear();
    return TJS_S_OK;
}

void loadLayerExDraw() {
    try {
        ncbAutoRegister::LoadModule(TJS_W("layerExDraw.dll"));
    } catch(...) {
    }
}

void loadLayerExDrawBase() { loadLayerExDraw(); }
void loadLayerExDrawCairo() { loadLayerExDraw(); }
void loadLayerExDrawGdiPlus() { loadLayerExDraw(); }

void loadLayerExMovie() {
    try {
        ncbAutoRegister::LoadModule(TJS_W("layerExMovie.dll"));
    } catch(...) {
    }
}

void codecHandledByCore(const tjs_char *module) {
    logCompatOnce(module, TJS_W("audio decoding is handled by the host sound core"));
}

} // namespace

// -------------------------------------------------------------------------
// drawdevice*.dll
// AETHERKIRI_COMPAT_STUB: AetherKiri keeps the Godot renderer; these expose
// old draw-device construction surfaces without replacing the renderer.
// -------------------------------------------------------------------------

#define NCB_MODULE_NAME TJS_W("drawdevice.dll")

class PluggedDrawDevice {
public:
    PluggedDrawDevice() = default;
    bool recreate() { return true; }
    bool attach(tTJSVariant = tTJSVariant()) { return true; }
    bool detach() { return true; }
    bool show() { return true; }
    bool hide() { return true; }
    void setTargetWindow(tTJSVariant) {}
    void setDestRectangle(tTJSVariant) {}
    void setClipRectangle(tTJSVariant) {}
    tjs_int getWidth() const { return 0; }
    tjs_int getHeight() const { return 0; }
};

class PassThroughDrawDeviceCompat : public PluggedDrawDevice {};

NCB_REGISTER_CLASS(PluggedDrawDevice) {
    Constructor();
    NCB_METHOD(recreate);
    NCB_METHOD(attach);
    NCB_METHOD(detach);
    NCB_METHOD(show);
    NCB_METHOD(hide);
    NCB_METHOD(setTargetWindow);
    NCB_METHOD(setDestRectangle);
    NCB_METHOD(setClipRectangle);
    NCB_PROPERTY_RO(width, getWidth);
    NCB_PROPERTY_RO(height, getHeight);
}

NCB_REGISTER_CLASS_DIFFER(PassThroughDrawDevice, PassThroughDrawDeviceCompat) {
    Constructor();
    NCB_METHOD(recreate);
    NCB_METHOD(attach);
    NCB_METHOD(detach);
    NCB_METHOD(show);
    NCB_METHOD(hide);
    NCB_METHOD(setTargetWindow);
    NCB_METHOD(setDestRectangle);
    NCB_METHOD(setClipRectangle);
    NCB_PROPERTY_RO(width, getWidth);
    NCB_PROPERTY_RO(height, getHeight);
}

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("drawdeviceZ_D3D9.dll")

class DrawDeviceZ {
public:
    DrawDeviceZ() = default;
    bool recreate() {
        logCompatOnce(TJS_W("drawdeviceZ_D3D9.dll"),
                      TJS_W("D3D9 backend is not used by the Godot renderer"));
        return true;
    }
};

NCB_REGISTER_CLASS(DrawDeviceZ) {
    Constructor();
    NCB_METHOD(recreate);
}

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("drawdeviceOgre.dll")

class OgreDrawDevice {
public:
    OgreDrawDevice() = default;
    bool recreate() { return true; }
    bool resetDevice() { return true; }
    void finalize() {}
};

NCB_REGISTER_CLASS(OgreDrawDevice) {
    Constructor();
    NCB_METHOD(recreate);
    NCB_METHOD(resetDevice);
    NCB_METHOD(finalize);
}

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("drawdeviceIrrlicht.dll")

class Irrlicht {
public:
    Irrlicht() = default;
    bool loadScene(const tjs_char *) { return false; }
    bool saveScene(const tjs_char *) { return false; }
    void clear() {}
};

NCB_REGISTER_CLASS(Irrlicht) {
    Constructor();
    NCB_METHOD(loadScene);
    NCB_METHOD(saveScene);
    NCB_METHOD(clear);
}

NCB_ATTACH_FUNCTION(copyIImage, Layer, trueCb);
NCB_ATTACH_FUNCTION(copyITexture, Layer, trueCb);

// -------------------------------------------------------------------------
// layerEx*.dll
// AETHERKIRI_COMPAT_STUB: reuse AetherKiri Layer/LayerExDraw where possible.
// -------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("layerEx.dll")
NCB_PRE_REGIST_CALLBACK(loadLayerExDrawBase);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("layerExCairo.dll")

class layerExCairoCompat {
public:
    void reset() {}
};

NCB_PRE_REGIST_CALLBACK(loadLayerExDrawCairo);
NCB_ATTACH_CLASS(layerExCairoCompat, Layer) { NCB_METHOD(reset); }

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("layerExGdiPlus.dll")
NCB_PRE_REGIST_CALLBACK(loadLayerExDrawGdiPlus);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("layerExAgg.dll")

class AGGPrimitive {
public:
    AGGPrimitive() = default;
    bool setPos(tjs_int = 0, tjs_int = 0) { return true; }
    bool rotate(tjs_real = 0) { return true; }
    tjs_real getX() const { return x_; }
    void setX(tjs_real value) { x_ = value; }
    tjs_real getY() const { return y_; }
    void setY(tjs_real value) { y_ = value; }

private:
    tjs_real x_ = 0;
    tjs_real y_ = 0;
};

class LayerAggCompat {
public:
    bool aggSetPos(tjs_int = 0, tjs_int = 0) { return true; }
    bool aggRotate(tjs_real = 0) { return true; }
    tjs_real aggX() const { return 0; }
    tjs_real aggY() const { return 0; }
};

NCB_REGISTER_CLASS(AGGPrimitive) {
    Constructor();
    NCB_METHOD(setPos);
    NCB_METHOD(rotate);
    NCB_PROPERTY(x, getX, setX);
    NCB_PROPERTY(y, getY, setY);
}

NCB_ATTACH_CLASS(LayerAggCompat, Layer) {
    NCB_METHOD(aggSetPos);
    NCB_METHOD(aggRotate);
    NCB_METHOD(aggX);
    NCB_METHOD(aggY);
}

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("layerExAVI.dll")
NCB_PRE_REGIST_CALLBACK(loadLayerExMovie);

class LayerExAVICompat {
public:
    bool openAVI(const tjs_char *, tjs_real = 0) { return true; }
    bool openCompressedAVI(const tjs_char *, tjs_real = 0) { return true; }
    bool closeAVI() { return true; }
    bool recordAVI(tjs_int = 0) { return true; }
    bool openWAV(const tjs_char *, tjs_int = 2, tjs_int = 44100,
                 tjs_int = 16, tjs_int = 0) {
        return true;
    }
    bool startWAV() { return true; }
    bool stopWAV() { return true; }
    bool closeWAV() { return true; }
};

NCB_ATTACH_CLASS(LayerExAVICompat, Layer) {
    NCB_METHOD(openAVI);
    NCB_METHOD(openCompressedAVI);
    NCB_METHOD(closeAVI);
    NCB_METHOD(recordAVI);
    NCB_METHOD(openWAV);
    NCB_METHOD(startWAV);
    NCB_METHOD(stopWAV);
    NCB_METHOD(closeWAV);
}

// -------------------------------------------------------------------------
// gameswf.dll
// AETHERKIRI_COMPAT_STUB: no embedded SWF runtime; keep class/method surface.
// -------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("gameswf.dll")

class SWFMovie {
public:
    SWFMovie() = default;
    bool load(const tjs_char *) { return false; }
    bool update() { return true; }
    bool notifyMouse(tjs_int = 0, tjs_int = 0, tjs_int = 0) { return true; }
    bool play() {
        playing_ = true;
        return true;
    }
    bool stop() {
        playing_ = false;
        return true;
    }
    bool restart() {
        playing_ = true;
        frame_ = 0;
        return true;
    }
    bool back() {
        if(frame_ > 0)
            --frame_;
        return true;
    }
    bool next() {
        ++frame_;
        return true;
    }
    bool gotoFrame(tjs_int frame) {
        frame_ = frame;
        return true;
    }

private:
    bool playing_ = false;
    tjs_int frame_ = 0;
};

class layerExSWF {
public:
    bool drawSWF(tTJSVariant = tTJSVariant()) { return true; }
};

NCB_REGISTER_CLASS(SWFMovie) {
    Constructor();
    NCB_METHOD(load);
    NCB_METHOD(update);
    NCB_METHOD(notifyMouse);
    NCB_METHOD(play);
    NCB_METHOD(stop);
    NCB_METHOD(restart);
    NCB_METHOD(back);
    NCB_METHOD(next);
    NCB_METHOD(gotoFrame);
}

NCB_ATTACH_CLASS(layerExSWF, Layer) { NCB_METHOD(drawSWF); }

// -------------------------------------------------------------------------
// magickpp.dll
// AETHERKIRI_COMPAT_STUB: image loading is handled by Layer/Storage codecs.
// -------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("magickpp.dll")

class MagickPP {
public:
    MagickPP() = default;
    ttstr getVersion() const { return TJS_W("AetherKiri MagickPP compat"); }
    ttstr getSupports() const { return TJS_W(""); }
    tTJSVariant readImages(const tjs_char *) {
        iTJSDispatch2 *array = TJSCreateArrayObject();
        if(!array)
            return tTJSVariant();
        tTJSVariant result(array, array);
        array->Release();
        return result;
    }
};

NCB_REGISTER_CLASS(MagickPP) {
    Constructor();
    NCB_PROPERTY_RO(version, getVersion);
    NCB_PROPERTY_RO(supports, getSupports);
    NCB_METHOD(readImages);
}

// -------------------------------------------------------------------------
// videoEncoder.dll
// AETHERKIRI_COMPAT_STUB: DirectShow/WMV encoder state surface only.
// -------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("videoEncoder.dll")

class VideoEncoderCompat {
public:
    static tjs_error TJS_INTF_METHOD factory(VideoEncoderCompat **result,
                                             tjs_int, tTJSVariant **,
                                             iTJSDispatch2 *) {
        *result = new VideoEncoderCompat();
        return TJS_S_OK;
    }
    bool open(const tjs_char *filename) {
        filename_ = filename ? filename : TJS_W("");
        opened_ = true;
        logCompatOnce(TJS_W("videoEncoder.dll"),
                      TJS_W("WMV/DirectShow encoding is unavailable"));
        return true;
    }
    bool close() {
        opened_ = false;
        return true;
    }
    static tjs_error TJS_INTF_METHOD encodeVideoSample(
        tTJSVariant *result, tjs_int, tTJSVariant **, VideoEncoderCompat *) {
        if(result)
            *result = true;
        return TJS_S_OK;
    }
    tjs_int getVideoQuality() const { return videoQuality_; }
    void setVideoQuality(tjs_int value) { videoQuality_ = value; }
    tjs_int getSecondPerKey() const { return secondPerKey_; }
    void setSecondPerKey(tjs_int value) { secondPerKey_ = value; }
    tjs_int getVideoTimeScale() const { return videoTimeScale_; }
    void setVideoTimeScale(tjs_int value) { videoTimeScale_ = value; }
    tjs_int getVideoTimeRate() const { return videoTimeRate_; }
    void setVideoTimeRate(tjs_int value) { videoTimeRate_ = value; }
    tjs_int getVideoWidth() const { return videoWidth_; }
    void setVideoWidth(tjs_int value) { videoWidth_ = value; }
    tjs_int getVideoHeight() const { return videoHeight_; }
    void setVideoHeight(tjs_int value) { videoHeight_ = value; }

private:
    bool opened_ = false;
    ttstr filename_;
    tjs_int videoQuality_ = 50;
    tjs_int secondPerKey_ = 5;
    tjs_int videoTimeScale_ = 1;
    tjs_int videoTimeRate_ = 30;
    tjs_int videoWidth_ = 640;
    tjs_int videoHeight_ = 480;
};

NCB_REGISTER_CLASS_DIFFER(videoEncoder, VideoEncoderCompat) {
    Factory(&VideoEncoderCompat::factory);
    NCB_METHOD(open);
    NCB_METHOD(close);
    NCB_METHOD_RAW_CALLBACK(encodeVideoSample,
                            &VideoEncoderCompat::encodeVideoSample, 0);
    NCB_PROPERTY(videoQuality, getVideoQuality, setVideoQuality);
    NCB_PROPERTY(secondPerKey, getSecondPerKey, setSecondPerKey);
    NCB_PROPERTY(videoTimeScale, getVideoTimeScale, setVideoTimeScale);
    NCB_PROPERTY(videoTimeRate, getVideoTimeRate, setVideoTimeRate);
    NCB_PROPERTY(videoWidth, getVideoWidth, setVideoWidth);
    NCB_PROPERTY(videoHeight, getVideoHeight, setVideoHeight);
}

// -------------------------------------------------------------------------
// tftSave.dll
// AETHERKIRI_COMPAT_STUB: exposes pre-rendered-font API; font rasterization
// stays in AetherKiri text/layer renderers.
// -------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("tftSave.dll")

class LayerGlyphEx {
public:
    bool drawGlyph(tjs_int ch) {
        setGlyph(ch);
        return true;
    }
    bool setGlyphInfo(tjs_int ch) {
        setGlyph(ch);
        return true;
    }
    tjs_int getBlackboxX() const { return blackboxX_; }
    void setBlackboxX(tjs_int value) { blackboxX_ = value; }
    tjs_int getBlackboxY() const { return blackboxY_; }
    void setBlackboxY(tjs_int value) { blackboxY_ = value; }
    tjs_int getOriginX() const { return originX_; }
    void setOriginX(tjs_int value) { originX_ = value; }
    tjs_int getOriginY() const { return originY_; }
    void setOriginY(tjs_int value) { originY_ = value; }
    tjs_int getIncX() const { return incX_; }
    void setIncX(tjs_int value) { incX_ = value; }
    tjs_int getIncY() const { return incY_; }
    void setIncY(tjs_int value) { incY_ = value; }
    tjs_int getInc() const { return inc_; }
    void setInc(tjs_int value) { inc_ = value; }

private:
    void setGlyph(tjs_int ch) {
        inc_ = 0;
        incX_ = 0;
        incY_ = 0;
        originX_ = 0;
        originY_ = 0;
        blackboxX_ = ch ? 1 : 0;
        blackboxY_ = ch ? 1 : 0;
    }
    tjs_int blackboxX_ = 0;
    tjs_int blackboxY_ = 0;
    tjs_int originX_ = 0;
    tjs_int originY_ = 0;
    tjs_int incX_ = 0;
    tjs_int incY_ = 0;
    tjs_int inc_ = 0;
};

NCB_ATTACH_FUNCTION(savePreRenderedFont, System, trueCb);
NCB_ATTACH_FUNCTION(loadPreRenderedFont, System, trueCb);

NCB_ATTACH_CLASS(LayerGlyphEx, Layer) {
    NCB_METHOD(drawGlyph);
    NCB_METHOD(setGlyphInfo);
    NCB_PROPERTY(blackbox_x, getBlackboxX, setBlackboxX);
    NCB_PROPERTY(blackbox_y, getBlackboxY, setBlackboxY);
    NCB_PROPERTY(origin_x, getOriginX, setOriginX);
    NCB_PROPERTY(origin_y, getOriginY, setOriginY);
    NCB_PROPERTY(inc_x, getIncX, setIncX);
    NCB_PROPERTY(inc_y, getIncY, setIncY);
    NCB_PROPERTY(inc, getInc, setInc);
}

// -------------------------------------------------------------------------
// windowExProgress.dll
// AETHERKIRI_COMPAT_STUB: progress API state without native child controls.
// -------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("windowExProgress.dll")

class WindowExProgressCompat {
public:
    bool startProgress(tTJSVariant = tTJSVariant()) {
        active_ = true;
        return true;
    }
    bool doProgress(tjs_int percent) {
        percent_ = percent;
        return false;
    }
    bool setProgressMessage(const tjs_char *, const tjs_char *) { return true; }
    bool endProgress() {
        active_ = false;
        return true;
    }
    bool getProgressActive() const { return active_; }

private:
    bool active_ = false;
    tjs_int percent_ = 0;
};

NCB_ATTACH_CLASS(WindowExProgressCompat, Window) {
    Variant(TJS_W("PBS_SMOOTH"), static_cast<tjs_int>(1));
    Variant(TJS_W("PBS_VERTICAL"), static_cast<tjs_int>(4));
    NCB_METHOD(startProgress);
    NCB_METHOD(doProgress);
    NCB_METHOD(setProgressMessage);
    NCB_METHOD(endProgress);
    NCB_PROPERTY_RO(progressActive, getProgressActive);
}

// -------------------------------------------------------------------------
// httpserv.dll
// AETHERKIRI_COMPAT_STUB: class surface without opening a native socket server.
// -------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("httpserv.dll")

class SimpleHTTPServer {
public:
    static tjs_error TJS_INTF_METHOD factory(SimpleHTTPServer **result,
                                             tjs_int numparams,
                                             tTJSVariant **param,
                                             iTJSDispatch2 *) {
        auto *server = new SimpleHTTPServer();
        if(numparams > 0)
            server->port_ = static_cast<tjs_int>(*param[0]);
        if(numparams > 1)
            server->timeout_ = static_cast<tjs_int>(*param[1]);
        if(numparams > 2)
            server->codepage_ = static_cast<tjs_int>(*param[2]);
        *result = server;
        return TJS_S_OK;
    }
    tjs_int start() {
        started_ = true;
        if(port_ == 0)
            port_ = 12737;
        logCompatOnce(TJS_W("httpserv.dll"),
                      TJS_W("embedded HTTP server is not opened in compat mode"));
        return port_;
    }
    bool stop() {
        started_ = false;
        return true;
    }
    tjs_int getPort() const { return port_; }
    tjs_int getTimeout() const { return timeout_; }
    tjs_int getCodePage() const { return codepage_; }
    void setCodePage(tjs_int value) { codepage_ = value; }

private:
    bool started_ = false;
    tjs_int port_ = 0;
    tjs_int timeout_ = 10;
    tjs_int codepage_ = 65001;
};

NCB_REGISTER_CLASS(SimpleHTTPServer) {
    Factory(&SimpleHTTPServer::factory);
    NCB_PROPERTY_RO(port, getPort);
    NCB_PROPERTY_RO(timeout, getTimeout);
    NCB_PROPERTY(codepage, getCodePage, setCodePage);
    NCB_METHOD(start);
    NCB_METHOD(stop);
    Variant(TJS_W("cpACP"), static_cast<tjs_int>(0));
    Variant(TJS_W("cpOEM"), static_cast<tjs_int>(1));
    Variant(TJS_W("cpUTF8"), static_cast<tjs_int>(65001));
    Variant(TJS_W("cpSJIS"), static_cast<tjs_int>(932));
    Variant(TJS_W("cpEUC"), static_cast<tjs_int>(20932));
    Variant(TJS_W("cpJIS"), static_cast<tjs_int>(50220));
}

// -------------------------------------------------------------------------
// wsh.dll
// AETHERKIRI_COMPAT_STUB: Windows Script Host is unavailable on macOS.
// -------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("wsh.dll")
NCB_ATTACH_FUNCTION(addProgId, Scripts, trueCb);
NCB_ATTACH_FUNCTION(execWSH, Scripts, voidCb);
NCB_ATTACH_FUNCTION(execStorageWSH, Scripts, voidCb);

// -------------------------------------------------------------------------
// wmrdump.dll
// AETHERKIRI_COMPAT_STUB: Win32 message dump helpers become no-op globals.
// -------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("wmrdump.dll")
NCB_REGISTER_FUNCTION(wmrStartDump, trueCb);
NCB_REGISTER_FUNCTION(wmrStopDump, trueCb);

// -------------------------------------------------------------------------
// onigruma.dll / xpressive.dll
// AETHERKIRI_COMPAT_STUB: core already provides RegExp; keep it intact.
// -------------------------------------------------------------------------

static void keepCoreRegExp() {
    logCompatOnce(TJS_W("regexp"),
                  TJS_W("using AetherKiri core RegExp implementation"));
}
static void keepCoreRegExpOnig() { keepCoreRegExp(); }
static void keepCoreRegExpXpressive() { keepCoreRegExp(); }

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("onigruma.dll")
NCB_PRE_REGIST_CALLBACK(keepCoreRegExpOnig);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("xpressive.dll")
NCB_PRE_REGIST_CALLBACK(keepCoreRegExpXpressive);

// -------------------------------------------------------------------------
// wuvorbis.dll / wumsadp.dll
// AETHERKIRI_COMPAT_STUB: host audio core handles supported sound codecs.
// -------------------------------------------------------------------------

static void wuvorbisCompat() { codecHandledByCore(TJS_W("wuvorbis.dll")); }
static void wumsadpCompat() { codecHandledByCore(TJS_W("wumsadp.dll")); }

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("wuvorbis.dll")
NCB_PRE_REGIST_CALLBACK(wuvorbisCompat);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("wumsadp.dll")
NCB_PRE_REGIST_CALLBACK(wumsadpCompat);

// -------------------------------------------------------------------------
// mkpj.dll
// AETHERKIRI_COMPAT_STUB: project-generation helper has no runtime API.
// -------------------------------------------------------------------------

static void mkpjCompat() {
    logCompatOnce(TJS_W("mkpj.dll"), TJS_W("project generator has no runtime API"));
}

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("mkpj.dll")
NCB_PRE_REGIST_CALLBACK(mkpjCompat);
