#include "DebugIntf.h"
#include "BitmapIntf.h"
#include "FontImpl.h"
#include "FontServiceIntf.h"
#include "LayerBitmapIntf.h"
#include "LayerIntf.h"
#include "TVPScreen.h"
#include "WindowImpl.h"
#include "ncbind.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <cstring>
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

tjs_error TJS_INTF_METHOD unavailableCb(tTJSVariant *result, tjs_int,
                                         tTJSVariant **,
                                         iTJSDispatch2 *) {
    if(result)
        *result = false;
    logCompatOnce(TJS_W("compat"),
                  TJS_W("requested host-only operation is unavailable"));
    return TJS_S_OK;
}

std::atomic<bool> g_wmrDumping{false};

tjs_error TJS_INTF_METHOD wmrStartDumpCb(tTJSVariant *result, tjs_int,
                                         tTJSVariant **,
                                         iTJSDispatch2 *) {
    g_wmrDumping.store(true, std::memory_order_release);
    logCompatOnce(TJS_W("wmrdump.dll"),
                  TJS_W("portable message-dump state enabled; host UI owns the log sink"));
    if(result)
        *result = true;
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD wmrStopDumpCb(tTJSVariant *result, tjs_int,
                                        tTJSVariant **,
                                        iTJSDispatch2 *) {
    g_wmrDumping.store(false, std::memory_order_release);
    if(result)
        *result = true;
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD wmrIsDumpingCb(tTJSVariant *result, tjs_int,
                                         tTJSVariant **,
                                         iTJSDispatch2 *) {
    if(result)
        *result = g_wmrDumping.load(std::memory_order_acquire);
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

    bool recreate() {
        // Keep the current Aether renderer as the sole draw-device owner. The
        // compatibility object is useful for scripts that only configure a
        // device, but it must not replace TVPMainWindow->DrawDevice.
        recreated_ = TVPMainWindow != nullptr;
        return recreated_;
    }

    bool attach(tTJSVariant target = tTJSVariant()) {
        if(!recreated_ && !recreate())
            return false;
        target_ = target;
        attached_ = true;
        return true;
    }

    bool detach() {
        attached_ = false;
        target_.Clear();
        return true;
    }

    bool show() {
        if(!attached_)
            return false;
        visible_ = true;
        return true;
    }

    bool hide() {
        if(!attached_)
            return false;
        visible_ = false;
        return true;
    }

    void setTargetWindow(tTJSVariant target) { target_ = target; }
    void setDestRectangle(tTJSVariant value) { dest_ = value; }
    void setClipRectangle(tTJSVariant value) { clip_ = value; }

    tjs_int getWidth() const { return rectangleExtent(dest_, true); }
    tjs_int getHeight() const { return rectangleExtent(dest_, false); }

private:
    static tjs_int rectangleExtent(const tTJSVariant &value, bool width) {
        if(value.Type() == tvtObject && value.AsObjectNoAddRef()) {
            ncbPropAccessor prop(value);
            const tjs_char *extent = width ? TJS_W("w") : TJS_W("h");
            tjs_int result = prop.getIntValue(extent, 0);
            if(result > 0)
                return result;
            const tjs_int first = prop.getIntValue(width ? TJS_W("left")
                                                          : TJS_W("top"),
                                                   0);
            const tjs_int last = prop.getIntValue(width ? TJS_W("right")
                                                         : TJS_W("bottom"),
                                                  first);
            if(last >= first)
                return last - first;
        }
        if(TVPMainWindow && TVPMainWindow->GetForm()) {
            tjs_int w = 0;
            tjs_int h = 0;
            TVPMainWindow->GetForm()->GetSize(w, h);
            return width ? std::max(0, w) : std::max(0, h);
        }
        return width ? std::max(0, tTVPScreen::GetDesktopWidth())
                     : std::max(0, tTVPScreen::GetDesktopHeight());
    }

    bool recreated_ = false;
    bool attached_ = false;
    bool visible_ = false;
    tTJSVariant target_;
    tTJSVariant dest_;
    tTJSVariant clip_;
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
        return false;
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
    bool recreate() {
        logCompatOnce(TJS_W("drawdeviceOgre.dll"),
                      TJS_W("Ogre backend is not used by the Aether renderer"));
        return false;
    }
    bool resetDevice() { return false; }
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

// These entry points require an Irrlicht/GPU object owned by the original
// plug-in.  Aether layers expose CPU pixels, not an IImage/ITexture handle;
// claiming success here leaves the caller with an object that was never
// copied.  Keep the symbol for script compatibility and report the missing
// host capability explicitly.
NCB_ATTACH_FUNCTION(copyIImage, Layer, unavailableCb);
NCB_ATTACH_FUNCTION(copyITexture, Layer, unavailableCb);

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
    bool setPos(tjs_int x = 0, tjs_int y = 0) {
        x_ = static_cast<tjs_real>(x);
        y_ = static_cast<tjs_real>(y);
        return true;
    }
    bool rotate(tjs_real angle = 0) {
        if(!std::isfinite(angle))
            return false;
        angle_ = angle;
        return true;
    }
    tjs_real getX() const { return x_; }
    void setX(tjs_real value) { x_ = value; }
    tjs_real getY() const { return y_; }
    void setY(tjs_real value) { y_ = value; }
    tjs_real getAngle() const { return angle_; }
    void setAngle(tjs_real value) { angle_ = value; }

private:
    tjs_real x_ = 0;
    tjs_real y_ = 0;
    tjs_real angle_ = 0;
};

class LayerAggCompat {
public:
    bool aggSetPos(tjs_int x = 0, tjs_int y = 0) {
        x_ = static_cast<tjs_real>(x);
        y_ = static_cast<tjs_real>(y);
        return true;
    }
    bool aggRotate(tjs_real angle = 0) {
        if(!std::isfinite(angle))
            return false;
        angle_ = angle;
        return true;
    }
    tjs_real aggX() const { return x_; }
    tjs_real aggY() const { return y_; }

private:
    tjs_real x_ = 0;
    tjs_real y_ = 0;
    tjs_real angle_ = 0;
};

NCB_REGISTER_CLASS(AGGPrimitive) {
    Constructor();
    NCB_METHOD(setPos);
    NCB_METHOD(rotate);
    NCB_PROPERTY(x, getX, setX);
    NCB_PROPERTY(y, getY, setY);
    NCB_PROPERTY(angle, getAngle, setAngle);
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
    bool openAVI(const tjs_char *, tjs_real = 0) { return unavailable(); }
    bool openCompressedAVI(const tjs_char *, tjs_real = 0) {
        return unavailable();
    }
    bool closeAVI() { return unavailable(); }
    bool recordAVI(tjs_int = 0) { return unavailable(); }
    bool openWAV(const tjs_char *, tjs_int = 2, tjs_int = 44100,
                 tjs_int = 16, tjs_int = 0) {
        return unavailable();
    }
    bool startWAV() { return unavailable(); }
    bool stopWAV() { return unavailable(); }
    bool closeWAV() { return unavailable(); }

private:
    static bool unavailable() {
        logCompatOnce(TJS_W("layerExAVI.dll"),
                      TJS_W("AVI/WAV capture requires a host media backend"));
        return false;
    }
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
    bool load(const tjs_char *) {
        loaded_ = false;
        logCompatOnce(TJS_W("gameswf.dll"),
                      TJS_W("SWF playback requires an embedded SWF runtime"));
        return false;
    }
    bool update() { return loaded_; }
    bool notifyMouse(tjs_int = 0, tjs_int = 0, tjs_int = 0) {
        return loaded_;
    }
    bool play() {
        if(!loaded_)
            return false;
        playing_ = true;
        return true;
    }
    bool stop() {
        if(!loaded_)
            return false;
        playing_ = false;
        return true;
    }
    bool restart() {
        if(!loaded_)
            return false;
        playing_ = true;
        frame_ = 0;
        return true;
    }
    bool back() {
        if(!loaded_)
            return false;
        if(frame_ > 0)
            --frame_;
        return true;
    }
    bool next() {
        if(!loaded_)
            return false;
        ++frame_;
        return true;
    }
    bool gotoFrame(tjs_int frame) {
        if(!loaded_)
            return false;
        frame_ = frame;
        return true;
    }

private:
    bool loaded_ = false;
    bool playing_ = false;
    tjs_int frame_ = 0;
};

class layerExSWF {
public:
    bool drawSWF(tTJSVariant = tTJSVariant()) {
        logCompatOnce(TJS_W("gameswf.dll"),
                      TJS_W("SWF drawing requires an embedded SWF runtime"));
        return false;
    }
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
    // Expose the formats owned by Aether's GraphicsLoader.  This is kept as
    // a string for the historical Magick++ plug-in contract (callers split
    // on commas); it deliberately does not claim ImageMagick-only formats.
    ttstr getSupports() const {
        return TJS_W("bmp,dib,jpg,jpeg,jif,png,tlg,tlg5,tlg6,webp,amv");
    }

    tTJSVariant readImages(const tjs_char *filename) {
        iTJSDispatch2 *array = TJSCreateArrayObject();
        if(!array)
            return tTJSVariant();

        if(filename && *filename) {
            iTJSDispatch2 *bitmap = nullptr;
            try {
                bitmap = TVPCreateBitmapObject();
                if(bitmap) {
                    tTJSVariant path(filename);
                    tTJSVariant *args[] = {&path};
                    tTJSVariant metadata;
                    const tjs_error error = bitmap->FuncCall(
                        0, TJS_W("load"), nullptr, &metadata, 1, args, bitmap);
                    if(TJS_SUCCEEDED(error)) {
                        tTJSVariant value(bitmap, bitmap);
                        const tjs_int index = 0;
                        array->PropSetByNum(TJS_MEMBERENSURE, index, &value,
                                            array);
                    }
                }
            } catch(...) {
                logCompatOnce(TJS_W("magickpp.dll"),
                              TJS_W("image could not be decoded by the core loader"));
            }
            if(bitmap)
                bitmap->Release();
        }
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
        opened_ = false;
        logCompatOnce(TJS_W("videoEncoder.dll"),
                      TJS_W("WMV/DirectShow encoding is unavailable"));
        return false;
    }
    bool close() {
        opened_ = false;
        return true;
    }
    static tjs_error TJS_INTF_METHOD encodeVideoSample(
        tTJSVariant *result, tjs_int, tTJSVariant **, VideoEncoderCompat *) {
        if(result)
            *result = false;
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
// The file cache itself is intentionally owned by the core FontService.  The
// layer-facing half is implemented here on top of that same service so games
// using setGlyphInfo/drawGlyph see real FreeType metrics and pixels instead of
// a successful 1x1 placeholder.
// -------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("tftSave.dll")

class LayerGlyphEx {
public:
    explicit LayerGlyphEx(iTJSDispatch2 *object) : object_(object) {}

    bool drawGlyph(tjs_int ch) { return renderGlyphImpl(ch, true); }
    bool setGlyphInfo(tjs_int ch) { return renderGlyphImpl(ch, false); }
    // Newer tftSave builds expose renderGlyph for DirectWrite.  FreeType is
    // the shared portable rasterizer in Aether, so it follows the same path.
    bool renderGlyph(tjs_int ch) { return renderGlyphImpl(ch, true); }

    tjs_int getGlyphCharset() const { return glyphCharset_; }
    void setGlyphCharset(tjs_int value) { glyphCharset_ = value; }
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
    static tjs_int roundedMetric(float value) {
        if(!std::isfinite(value))
            return 0;
        return static_cast<tjs_int>(std::lround(value));
    }

    void setMetricProperties(const tTVPFontGlyphMetrics &metrics,
                             const tTVPFontGlyphBitmap *bitmap) {
        blackboxX_ = bitmap ? bitmap->Width : roundedMetric(metrics.Width);
        blackboxY_ = bitmap ? bitmap->Height : roundedMetric(metrics.Height);
        originX_ = bitmap ? bitmap->Left : roundedMetric(metrics.BearingX);
        originY_ = bitmap ? bitmap->Top : roundedMetric(metrics.BearingY);
        incX_ = roundedMetric(metrics.AdvanceX);
        incY_ = roundedMetric(metrics.AdvanceY);
        inc_ = incX_;
        if(object_) {
            auto set = [this](const tjs_char *name, tjs_int value) {
                tTJSVariant variant(value);
                object_->PropSet(TJS_MEMBERENSURE, name, nullptr, &variant,
                                 object_);
            };
            set(TJS_W("blackbox_x"), blackboxX_);
            set(TJS_W("blackbox_y"), blackboxY_);
            set(TJS_W("origin_x"), originX_);
            set(TJS_W("origin_y"), originY_);
            set(TJS_W("inc_x"), incX_);
            set(TJS_W("inc_y"), incY_);
            set(TJS_W("inc"), inc_);
        }
    }

    bool renderGlyphImpl(tjs_int ch, bool writeImage) {
        if(ch < 0 || !object_)
            return false;
        tTJSNI_BaseLayer *layer = compatLayerFromThis(object_);
        if(!layer)
            return false;

        const tTVPFont &font = layer->GetFont();
        const tjs_int pixelSize = std::max<tjs_int>(1, std::abs(font.Height));
        const bool bold = (font.Flags & TVP_TF_BOLD) != 0;
        const bool italic = (font.Flags & TVP_TF_ITALIC) != 0;
        ttstr faceName = font.Face;
        if(faceName.IsEmpty())
            faceName = TVPGetDefaultFontName();

        tTVPFontFaceHandle face = nullptr;
        try {
            face = TVPFontAcquireFace(faceName);
        } catch(...) {
            face = nullptr;
        }
        if(!face) {
            logCompatOnce(TJS_W("tftSave.dll"),
                          TJS_W("font face could not be resolved"));
            return false;
        }

        const tjs_uint32 glyph = TVPFontGetGlyphIndex(
            face, static_cast<tjs_uint32>(ch));
        tTVPFontGlyphMetrics metrics{};
        const bool haveMetrics = glyph != 0 && TVPFontGetGlyphMetrics(
            face, glyph, pixelSize, bold, italic, &metrics);
        tTVPFontGlyphBitmap bitmap{};
        const bool haveBitmap = writeImage && glyph != 0 &&
            TVPFontGetGlyphBitmap(face, glyph, pixelSize, false, bold, italic,
                                  &bitmap);

        if(haveMetrics)
            setMetricProperties(metrics, haveBitmap ? &bitmap : nullptr);
        else {
            tTVPFontGlyphMetrics empty{};
            setMetricProperties(empty, nullptr);
        }

        bool rendered = !writeImage;
        if(writeImage && haveBitmap && bitmap.Width > 0 && bitmap.Height > 0 &&
           bitmap.Buffer && bitmap.Pitch != 0) {
            try {
                if(!layer->GetMainImage())
                    layer->SetHasImage(true);
                layer->SetImageSize(static_cast<tjs_uint>(bitmap.Width),
                                    static_cast<tjs_uint>(bitmap.Height));
                auto *destination = static_cast<tjs_uint8 *>(
                    layer->GetMainImagePixelBufferForWrite());
                const tjs_int destinationPitch =
                    layer->GetMainImagePixelBufferPitch();
                if(destination && destinationPitch >= bitmap.Width * 4) {
                    std::memset(destination,
                                0,
                                static_cast<size_t>(destinationPitch) *
                                    static_cast<size_t>(bitmap.Height));
                    for(tjs_int y = 0; y < bitmap.Height; ++y) {
                        const tjs_int sourceY = bitmap.Pitch > 0
                            ? y : bitmap.Height - 1 - y;
                        const auto *source = bitmap.Buffer +
                            static_cast<ptrdiff_t>(sourceY) *
                                static_cast<ptrdiff_t>(std::abs(bitmap.Pitch));
                        auto *target = destination +
                            static_cast<size_t>(y) *
                                static_cast<size_t>(destinationPitch);
                        if(bitmap.Format == TVP_FONT_BITMAP_BGRA) {
                            std::memcpy(target, source,
                                        static_cast<size_t>(bitmap.Width) * 4);
                        } else {
                            for(tjs_int x = 0; x < bitmap.Width; ++x) {
                                const tjs_uint32 alpha = source[x];
                                const tjs_uint32 pixel = (alpha << 24) |
                                    0x00ffffffu;
                                std::memcpy(target + static_cast<size_t>(x) * 4,
                                            &pixel, sizeof(pixel));
                            }
                        }
                    }
                    layer->Update(tTVPRect(0, 0, bitmap.Width,
                                           bitmap.Height));
                    rendered = true;
                }
            } catch(...) {
                rendered = false;
            }
        }
        TVPFontReleaseFace(face);
        return glyph != 0 && haveMetrics && rendered;
    }

    iTJSDispatch2 *object_ = nullptr;
    tjs_int glyphCharset_ = 1; // DEFAULT_CHARSET-compatible Unicode mode
    tjs_int blackboxX_ = 0;
    tjs_int blackboxY_ = 0;
    tjs_int originX_ = 0;
    tjs_int originY_ = 0;
    tjs_int incX_ = 0;
    tjs_int incY_ = 0;
    tjs_int inc_ = 0;
};

NCB_GET_INSTANCE_HOOK(LayerGlyphEx) {
    /**/ NCB_GET_INSTANCE_HOOK_CLASS() {}
    /**/ ~NCB_GET_INSTANCE_HOOK_CLASS() {}

    NCB_INSTANCE_GETTER(objthis) {
        ClassT *object = GetNativeInstance(objthis);
        if(!object)
            SetNativeInstance(objthis, (object = new ClassT(objthis)));
        return object;
    }
};

NCB_ATTACH_CLASS_WITH_HOOK(LayerGlyphEx, Layer) {
    NCB_METHOD(drawGlyph);
    NCB_METHOD(setGlyphInfo);
    NCB_METHOD(renderGlyph);
    NCB_PROPERTY(glyphCharset, getGlyphCharset, setGlyphCharset);
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
    bool startProgress(tTJSVariant init = tTJSVariant()) {
        active_ = true;
        cancelled_ = false;
        percent_ = 0;
        messages_.clear();
        if(init.Type() == tvtObject && init.AsObjectNoAddRef()) {
            iTJSDispatch2 *dict = init.AsObjectNoAddRef();
            tTJSVariant value;
            if(TJS_SUCCEEDED(dict->PropGet(TJS_IGNOREPROP,
                                           TJS_W("cancelRequested"), nullptr,
                                           &value, dict)) &&
               value.Type() != tvtVoid)
                cancelled_ = static_cast<bool>(value);
            if(TJS_SUCCEEDED(dict->PropGet(TJS_IGNOREPROP,
                                           TJS_W("percent"), nullptr, &value,
                                           dict)) &&
               value.Type() != tvtVoid)
                percent_ = std::clamp(static_cast<tjs_int>(value), 0, 100);
        }
        return true;
    }
    bool doProgress(tjs_int percent) {
        if(!active_)
            return true;
        if(percent < 0) {
            cancelled_ = true;
            percent_ = 0;
        } else {
            percent_ = std::clamp(percent, 0, 100);
        }
        return cancelled_;
    }
    bool setProgressMessage(const tjs_char *name, const tjs_char *text) {
        if(!active_ || !name)
            return false;
        messages_[name] = text ? text : TJS_W("");
        return true;
    }
    bool endProgress() {
        active_ = false;
        return true;
    }
    bool getProgressActive() const { return active_; }
    tjs_int getProgressPercent() const { return percent_; }
    bool getProgressCancelled() const { return cancelled_; }
    void setProgressCancelled(bool value) { cancelled_ = value; }
    ttstr getProgressMessage(const tjs_char *name) const {
        if(!name)
            return ttstr();
        const auto found = messages_.find(name);
        return found == messages_.end() ? ttstr() : found->second;
    }

private:
    bool active_ = false;
    bool cancelled_ = false;
    tjs_int percent_ = 0;
    std::map<ttstr, ttstr> messages_;
};

NCB_ATTACH_CLASS(WindowExProgressCompat, Window) {
    Variant(TJS_W("PBS_SMOOTH"), static_cast<tjs_int>(1));
    Variant(TJS_W("PBS_VERTICAL"), static_cast<tjs_int>(4));
    NCB_METHOD(startProgress);
    NCB_METHOD(doProgress);
    NCB_METHOD(setProgressMessage);
    NCB_METHOD(endProgress);
    NCB_PROPERTY_RO(progressActive, getProgressActive);
    NCB_PROPERTY_RO(progressPercent, getProgressPercent);
    NCB_PROPERTY(progressCancelled, getProgressCancelled,
                 setProgressCancelled);
    NCB_METHOD(getProgressMessage);
}

// -------------------------------------------------------------------------
// wsh.dll
// AETHERKIRI_COMPAT_STUB: Windows Script Host is unavailable on portable
// hosts.  The functions remain registered so old scripts can probe them,
// but they never report a successful execution without a WSH engine.
// -------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("wsh.dll")
NCB_ATTACH_FUNCTION(addProgId, Scripts, unavailableCb);
NCB_ATTACH_FUNCTION(execWSH, Scripts, unavailableCb);
NCB_ATTACH_FUNCTION(execStorageWSH, Scripts, unavailableCb);

// -------------------------------------------------------------------------
// wmrdump.dll
// AETHERKIRI_COMPAT_STUB: message dumping is represented by a process-local
// state flag on portable hosts.  It is intentionally not exposed as a fake
// HWND/file dump; callers can observe whether the request was accepted.
// -------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("wmrdump.dll")
NCB_REGISTER_FUNCTION(wmrStartDump, wmrStartDumpCb);
NCB_REGISTER_FUNCTION(wmrStopDump, wmrStopDumpCb);
NCB_REGISTER_FUNCTION(wmrIsDumping, wmrIsDumpingCb);

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
