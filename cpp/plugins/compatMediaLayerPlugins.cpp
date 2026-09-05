#include "DebugIntf.h"
#include "LayerBitmapIntf.h"
#include "LayerIntf.h"
#include "ncbind.hpp"

#include <algorithm>
#include <map>
#include <vector>

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

tjs_int compatVariantInt(tTJSVariant **param, tjs_int numparams,
                         tjs_int index, tjs_int fallback) {
    if(index >= numparams || !param || !param[index] ||
       param[index]->Type() == tvtVoid)
        return fallback;
    return static_cast<tjs_int>(*param[index]);
}

tTJSNI_BaseLayer *compatLayerFromThis(iTJSDispatch2 *objthis) {
    if(!objthis)
        return nullptr;
    tTJSNI_BaseLayer *layer = nullptr;
    if(TJS_FAILED(objthis->NativeInstanceSupport(
           TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
           reinterpret_cast<iTJSNativeInstance **>(&layer)))) {
        return nullptr;
    }
    return layer;
}

tjs_error setGlyphOctetResult(tTJSVariant *result, tjs_int width,
                              tjs_int height,
                              const std::vector<tjs_uint8> &glyph) {
    if(!result)
        return TJS_S_OK;

    tTJSVariantOctet *octet = TJSAllocVariantOctet(
        glyph.empty() ? nullptr : glyph.data(),
        static_cast<tjs_uint>(glyph.size()));
    if(!octet)
        return TJS_S_OK;

    iTJSDispatch2 *array = TJSCreateArrayObject();
    if(!array) {
        octet->Release();
        return TJS_E_FAIL;
    }

    tTJSVariant value;
    value = width;
    array->PropSetByNum(TJS_MEMBERENSURE, 0, &value, array);
    value = height;
    array->PropSetByNum(TJS_MEMBERENSURE, 1, &value, array);
    value = octet;
    array->PropSetByNum(TJS_MEMBERENSURE, 2, &value, array);
    octet->Release();

    *result = tTJSVariant(array, array);
    array->Release();
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD makeGlyphBitmapCompat(tTJSVariant *result,
                                                tjs_int numparams,
                                                tTJSVariant **param,
                                                iTJSDispatch2 *objthis) {
    if(result)
        result->Clear();

    tTJSNI_BaseLayer *layer = compatLayerFromThis(objthis);
    if(!layer)
        return TJS_E_NATIVECLASSCRASH;

    tTVPBaseTexture *image = layer->GetMainImage();
    if(!image)
        return TJS_S_OK;

    const tjs_int image_width = static_cast<tjs_int>(image->GetWidth());
    const tjs_int image_height = static_cast<tjs_int>(image->GetHeight());
    if(image_width <= 0 || image_height <= 0)
        return TJS_S_OK;

    tjs_int left = compatVariantInt(param, numparams, 1, 0);
    tjs_int top = compatVariantInt(param, numparams, 2, 0);
    tjs_int width = compatVariantInt(param, numparams, 3, image_width - left);
    tjs_int height = compatVariantInt(param, numparams, 4, image_height - top);

    left = std::clamp(left, 0, image_width);
    top = std::clamp(top, 0, image_height);
    width = std::clamp(width, 0, image_width - left);
    height = std::clamp(height, 0, image_height - top);
    if(width <= 0 || height <= 0)
        return TJS_S_OK;

    const tjs_int pitch = image->GetPitchBytes();
    if(pitch <= 0)
        return TJS_S_OK;

    std::vector<tjs_uint8> glyph(static_cast<size_t>(width) *
                                 static_cast<size_t>(height));
    for(tjs_int y = 0; y < height; ++y) {
        const auto *row = static_cast<const tjs_uint32 *>(
            image->GetScanLine(static_cast<tjs_uint>(top + y)));
        if(!row)
            continue;
        row += left;
        for(tjs_int x = 0; x < width; ++x) {
            tjs_uint8 alpha =
                static_cast<tjs_uint8>((row[x] >> 24) & 0xff);
            if(alpha == 0) {
                const tjs_uint32 pixel = row[x];
                alpha = static_cast<tjs_uint8>(
                    std::max({pixel & 0xff, (pixel >> 8) & 0xff,
                              (pixel >> 16) & 0xff}));
            }
            glyph[static_cast<size_t>(y) * static_cast<size_t>(width) +
                  static_cast<size_t>(x)] = alpha;
        }
    }

    return setGlyphOctetResult(result, width, height, glyph);
}

tjs_error TJS_INTF_METHOD operateGlyphToProvinceCompat(
    tTJSVariant *result, tjs_int numparams, tTJSVariant **param,
    iTJSDispatch2 *objthis) {
    if(result)
        result->Clear();
    if(numparams < 5 || !param || !param[4] ||
       param[4]->Type() != tvtOctet)
        return TJS_E_BADPARAMCOUNT;

    tTJSNI_BaseLayer *layer = compatLayerFromThis(objthis);
    if(!layer)
        return TJS_E_NATIVECLASSCRASH;

    tTVPBaseTexture *image = layer->GetMainImage();
    if(!image)
        return TJS_S_OK;

    tjs_int dst_x = compatVariantInt(param, numparams, 0, 0);
    tjs_int dst_y = compatVariantInt(param, numparams, 1, 0);
    tjs_int width = compatVariantInt(param, numparams, 2, 0);
    tjs_int height = compatVariantInt(param, numparams, 3, 0);
    if(width <= 0 || height <= 0)
        return TJS_S_OK;

    auto *octet = param[4]->AsOctetNoAddRef();
    if(!octet || !octet->GetData())
        return TJS_S_OK;
    const tjs_uint8 *src = octet->GetData();
    const size_t src_len = octet->GetLength();

    const tjs_int image_width = static_cast<tjs_int>(image->GetWidth());
    const tjs_int image_height = static_cast<tjs_int>(image->GetHeight());
    tjs_int src_x = 0;
    tjs_int src_y = 0;
    tjs_int copy_w = width;
    tjs_int copy_h = height;
    if(dst_x < 0) {
        src_x = -dst_x;
        copy_w -= src_x;
        dst_x = 0;
    }
    if(dst_y < 0) {
        src_y = -dst_y;
        copy_h -= src_y;
        dst_y = 0;
    }
    copy_w = std::min(copy_w, image_width - dst_x);
    copy_h = std::min(copy_h, image_height - dst_y);
    if(copy_w <= 0 || copy_h <= 0)
        return TJS_S_OK;

    auto *dst = static_cast<tjs_uint8 *>(
        layer->GetProvinceImagePixelBufferForWrite());
    const tjs_int pitch = layer->GetProvinceImagePixelBufferPitch();
    if(!dst || pitch <= 0)
        return TJS_S_OK;

    for(tjs_int y = 0; y < copy_h; ++y) {
        const size_t src_row =
            static_cast<size_t>(src_y + y) * static_cast<size_t>(width) +
            static_cast<size_t>(src_x);
        if(src_row >= src_len)
            break;
        const size_t row_available = src_len - src_row;
        const tjs_int row_w =
            static_cast<tjs_int>(std::min<size_t>(copy_w, row_available));
        tjs_uint8 *dst_row = dst + static_cast<size_t>(dst_y + y) *
                                       static_cast<size_t>(pitch) +
                             static_cast<size_t>(dst_x);
        const tjs_uint8 *src_row_ptr = src + src_row;
        for(tjs_int x = 0; x < row_w; ++x)
            dst_row[x] = std::max(dst_row[x], src_row_ptr[x]);
    }

    tTVPRect rect;
    rect.left = dst_x;
    rect.top = dst_y;
    rect.right = dst_x + copy_w;
    rect.bottom = dst_y + copy_h;
    layer->Update(rect);
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD makeBitmapFromProvinceCompat(
    tTJSVariant *result, tjs_int numparams, tTJSVariant **param,
    iTJSDispatch2 *objthis) {
    if(result)
        result->Clear();

    tTJSNI_BaseLayer *layer = compatLayerFromThis(objthis);
    if(!layer)
        return TJS_E_NATIVECLASSCRASH;

    tjs_int left = compatVariantInt(param, numparams, 0, 0);
    tjs_int top = compatVariantInt(param, numparams, 1, 0);
    tjs_int width = compatVariantInt(param, numparams, 2, 0);
    tjs_int height = compatVariantInt(param, numparams, 3, 0);
    if(width <= 0 || height <= 0)
        return TJS_S_OK;

    auto *province = static_cast<const tjs_uint8 *>(
        layer->GetProvinceImagePixelBuffer());
    const tjs_int pitch = layer->GetProvinceImagePixelBufferPitch();
    tTVPBaseBitmap *province_image = layer->GetProvinceImage();
    const tjs_int province_width =
        province_image ? static_cast<tjs_int>(province_image->GetWidth()) : 0;
    const tjs_int province_height =
        province_image ? static_cast<tjs_int>(province_image->GetHeight()) : 0;
    std::vector<tjs_uint8> glyph(static_cast<size_t>(width) *
                                 static_cast<size_t>(height));
    if(province && pitch > 0 && province_width > 0 && province_height > 0) {
        for(tjs_int y = 0; y < height; ++y) {
            if(top + y < 0 || top + y >= province_height)
                continue;
            const tjs_uint8 *src_row =
                province + static_cast<size_t>(top + y) *
                               static_cast<size_t>(pitch);
            for(tjs_int x = 0; x < width; ++x) {
                if(left + x < 0 || left + x >= province_width)
                    continue;
                glyph[static_cast<size_t>(y) * static_cast<size_t>(width) +
                      static_cast<size_t>(x)] = src_row[left + x];
            }
        }
    }

    return setGlyphOctetResult(result, width, height, glyph);
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
// msdfrender.dll
// AETHERKIRI_COMPAT_STUB: enough glyph extraction for games that use
// PreRenderFontEx/MSDF atlases while final blending stays in Layer.drawGlyph.
// -------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("msdfrender.dll")

class MsdfrenderLayerCompat {};

NCB_ATTACH_CLASS(MsdfrenderLayerCompat, Layer) {
    RawCallback(TJS_W("makeGlyphSDF"), makeGlyphBitmapCompat, 0);
    RawCallback(TJS_W("makeGlyphMSDF"), makeGlyphBitmapCompat, 0);
    RawCallback(TJS_W("operateGlyphToProvince"),
                operateGlyphToProvinceCompat, 0);
    RawCallback(TJS_W("makeBitmapFromProvince"),
                makeBitmapFromProvinceCompat, 0);
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
// wuffmpeg.dll / wuvorbis.dll / wumsadp.dll
// AETHERKIRI_COMPAT_STUB: host audio core handles supported sound codecs.
// -------------------------------------------------------------------------

static void wuffmpegCompat() { codecHandledByCore(TJS_W("wuffmpeg.dll")); }
static void wuvorbisCompat() { codecHandledByCore(TJS_W("wuvorbis.dll")); }
static void wumsadpCompat() { codecHandledByCore(TJS_W("wumsadp.dll")); }

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("wuffmpeg.dll")
NCB_PRE_REGIST_CALLBACK(wuffmpegCompat);

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
