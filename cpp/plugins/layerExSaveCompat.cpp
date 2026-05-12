#include "PluginStub.h"
#include "ncbind.hpp"

#include <algorithm>
#include <cstdint>

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

#define NCB_MODULE_NAME TJS_W("layerExSave.dll")

namespace {

using ReadPtr = const tjs_uint8 *;
using WritePtr = tjs_uint8 *;

struct LayerImage {
    tjs_int width = 0;
    tjs_int height = 0;
    tjs_int pitch = 0;
    ReadPtr read = nullptr;
    WritePtr write = nullptr;
};

bool getBoolProp(iTJSDispatch2 *layer, const tjs_char *name) {
    tTJSVariant value;
    return TJS_SUCCEEDED(layer->PropGet(0, name, nullptr, &value, layer)) &&
           value.AsInteger() != 0;
}

tjs_int getIntProp(iTJSDispatch2 *layer, const tjs_char *name) {
    tTJSVariant value;
    if(TJS_FAILED(layer->PropGet(0, name, nullptr, &value, layer)))
        TVPThrowExceptionMessage((ttstr(TJS_W("cannot get Layer.")) + name)
                                     .c_str());
    return static_cast<tjs_int>(value.AsInteger());
}

LayerImage getLayerImage(iTJSDispatch2 *layer, bool writable) {
    if(!layer ||
       TJS_FAILED(layer->IsInstanceOf(0, nullptr, nullptr, TJS_W("Layer"),
                                      layer)) ||
       !getBoolProp(layer, TJS_W("hasImage")))
        TVPThrowExceptionMessage(TJS_W("Invalid layer image."));

    LayerImage image;
    image.width = getIntProp(layer, TJS_W("imageWidth"));
    image.height = getIntProp(layer, TJS_W("imageHeight"));
    image.pitch = getIntProp(layer, TJS_W("mainImageBufferPitch"));
    if(image.width <= 0 || image.height <= 0 || image.pitch == 0)
        TVPThrowExceptionMessage(TJS_W("Invalid layer image."));

    const tjs_char *bufferName =
        writable ? TJS_W("mainImageBufferForWrite") : TJS_W("mainImageBuffer");
    auto *buffer = reinterpret_cast<tjs_uint8 *>(
        static_cast<tjs_intptr_t>(getIntProp(layer, bufferName)));
    if(!buffer)
        TVPThrowExceptionMessage(TJS_W("Invalid layer image."));
    image.read = buffer;
    image.write = buffer;
    return image;
}

ReadPtr pixelAt(const LayerImage &image, tjs_int x, tjs_int y) {
    return image.read + y * image.pitch + x * 4;
}

WritePtr writablePixelAt(const LayerImage &image, tjs_int x, tjs_int y) {
    return image.write + y * image.pitch + x * 4;
}

bool nonTransparent(ReadPtr p) { return p[3] != 0; }

bool nonZero(ReadPtr p) {
    return p[0] != 0 || p[1] != 0 || p[2] != 0 || p[3] != 0;
}

bool samePixel(ReadPtr a, ReadPtr b) {
    return a[3] == b[3] &&
           (a[3] == 0 || (a[0] == b[0] && a[1] == b[1] && a[2] == b[2]));
}

void makeRectResult(tTJSVariant *result, tjs_int x, tjs_int y, tjs_int w,
                    tjs_int h) {
    if(!result)
        return;
    ncbDictionaryAccessor dict;
    dict.SetValue(TJS_W("x"), x);
    dict.SetValue(TJS_W("y"), y);
    dict.SetValue(TJS_W("w"), w);
    dict.SetValue(TJS_W("h"), h);
    *result = dict;
}

template <typename Predicate>
tjs_error cropRect(tTJSVariant *result, iTJSDispatch2 *layer,
                   Predicate predicate) {
    const LayerImage image = getLayerImage(layer, false);
    tjs_int minX = image.width;
    tjs_int minY = image.height;
    tjs_int maxX = -1;
    tjs_int maxY = -1;

    for(tjs_int y = 0; y < image.height; ++y) {
        for(tjs_int x = 0; x < image.width; ++x) {
            if(predicate(pixelAt(image, x, y))) {
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        }
    }

    if(result)
        result->Clear();
    if(maxX < minX || maxY < minY)
        return TJS_S_OK;

    makeRectResult(result, minX, minY, maxX - minX + 1, maxY - minY + 1);
    return TJS_S_OK;
}

void clipRect(const LayerImage &image, tjs_int &left, tjs_int &top,
              tjs_int &width, tjs_int &height) {
    if(left < 0) {
        width += left;
        left = 0;
    }
    if(top < 0) {
        height += top;
        top = 0;
    }
    if(left + width > image.width)
        width = image.width - left;
    if(top + height > image.height)
        height = image.height - top;
}

void callLayerSave(iTJSDispatch2 *layer, const ttstr &filename,
                   const tjs_char *type) {
    tTJSVariant filenameValue(filename);
    tTJSVariant typeValue(type);
    tTJSVariant *args[] = { &filenameValue, &typeValue };
    layer->FuncCall(0, TJS_W("saveLayerImage"), nullptr, nullptr, 2, args,
                    layer);
}

} // namespace

static tjs_error TJS_INTF_METHOD saveLayerImagePng(tTJSVariant *, tjs_int num,
                                                   tTJSVariant **param,
                                                   iTJSDispatch2 *layer) {
    if(num < 1)
        return TJS_E_BADPARAMCOUNT;
    callLayerSave(layer, param[0]->AsStringNoAddRef(), TJS_W("png"));
    return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD saveLayerImageTlg5(tTJSVariant *, tjs_int num,
                                                    tTJSVariant **param,
                                                    iTJSDispatch2 *layer) {
    if(num < 1)
        return TJS_E_BADPARAMCOUNT;
    callLayerSave(layer, param[0]->AsStringNoAddRef(), TJS_W("tlg5"));
    return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD saveLayerImagePngOctet(tTJSVariant *,
                                                        tjs_int,
                                                        tTJSVariant **,
                                                        iTJSDispatch2 *) {
    TVPThrowExceptionMessage(
        TJS_W("saveLayerImagePngOctet is not supported by this compatibility layer"));
    return TJS_E_FAIL;
}

static tjs_error TJS_INTF_METHOD getCropRect(tTJSVariant *result, tjs_int,
                                             tTJSVariant **,
                                             iTJSDispatch2 *layer) {
    return cropRect(result, layer, nonTransparent);
}

static tjs_error TJS_INTF_METHOD getCropRectZero(tTJSVariant *result, tjs_int,
                                                 tTJSVariant **,
                                                 iTJSDispatch2 *layer) {
    return cropRect(result, layer, nonZero);
}

static tjs_error TJS_INTF_METHOD getDiffRect(tTJSVariant *result, tjs_int num,
                                             tTJSVariant **param,
                                             iTJSDispatch2 *layer) {
    if(num < 1)
        return TJS_E_BADPARAMCOUNT;

    const LayerImage image = getLayerImage(layer, false);
    const LayerImage base = getLayerImage(param[0]->AsObjectNoAddRef(), false);
    if(image.width != base.width || image.height != base.height)
        TVPThrowExceptionMessage(TJS_W("Different layer size."));

    tjs_int minX = image.width;
    tjs_int minY = image.height;
    tjs_int maxX = -1;
    tjs_int maxY = -1;
    for(tjs_int y = 0; y < image.height; ++y) {
        for(tjs_int x = 0; x < image.width; ++x) {
            if(!samePixel(pixelAt(image, x, y), pixelAt(base, x, y))) {
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        }
    }

    if(result)
        result->Clear();
    if(maxX < minX || maxY < minY)
        return TJS_S_OK;
    makeRectResult(result, minX, minY, maxX - minX + 1, maxY - minY + 1);
    return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD getDiffPixel(tTJSVariant *result, tjs_int num,
                                              tTJSVariant **param,
                                              iTJSDispatch2 *layer) {
    if(num < 1)
        return TJS_E_BADPARAMCOUNT;

    const bool fillSame = num >= 2 && param[1]->Type() != tvtVoid;
    const bool fillDiff = num >= 3 && param[2]->Type() != tvtVoid;
    const tjs_uint32 sameColor =
        fillSame ? static_cast<tjs_uint32>(param[1]->AsInteger()) : 0;
    const tjs_uint32 diffColor =
        fillDiff ? static_cast<tjs_uint32>(param[2]->AsInteger()) : 0;

    const LayerImage image = getLayerImage(layer, true);
    const LayerImage base = getLayerImage(param[0]->AsObjectNoAddRef(), false);
    if(image.width != base.width || image.height != base.height)
        TVPThrowExceptionMessage(TJS_W("Different layer size."));

    tTVInteger count = 0;
    for(tjs_int y = 0; y < image.height; ++y) {
        for(tjs_int x = 0; x < image.width; ++x) {
            const bool same = samePixel(pixelAt(image, x, y), pixelAt(base, x, y));
            auto *dst = reinterpret_cast<tjs_uint32 *>(writablePixelAt(image, x, y));
            if(same) {
                if(fillSame)
                    *dst = sameColor;
            } else {
                ++count;
                if(fillDiff)
                    *dst = diffColor;
            }
        }
    }
    if(result)
        *result = count;
    return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD copyBlueToAlpha(tTJSVariant *, tjs_int num,
                                                 tTJSVariant **param,
                                                 iTJSDispatch2 *layer) {
    if(num < 1)
        return TJS_E_BADPARAMCOUNT;
    const LayerImage src = getLayerImage(param[0]->AsObjectNoAddRef(), false);
    const LayerImage dst = getLayerImage(layer, true);
    const tjs_int width = std::min(src.width, dst.width);
    const tjs_int height = std::min(src.height, dst.height);
    for(tjs_int y = 0; y < height; ++y)
        for(tjs_int x = 0; x < width; ++x)
            writablePixelAt(dst, x, y)[3] = pixelAt(src, x, y)[0];
    return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD isBlank(tTJSVariant *result, tjs_int num,
                                         tTJSVariant **param,
                                         iTJSDispatch2 *layer) {
    if(num < 4)
        return TJS_E_BADPARAMCOUNT;

    const LayerImage image = getLayerImage(layer, false);
    tjs_int left = param[0]->AsInteger();
    tjs_int top = param[1]->AsInteger();
    tjs_int width = param[2]->AsInteger();
    tjs_int height = param[3]->AsInteger();
    clipRect(image, left, top, width, height);

    bool blank = true;
    if(width > 0 && height > 0) {
        for(tjs_int y = top; blank && y < top + height; ++y)
            for(tjs_int x = left; x < left + width; ++x)
                if(nonZero(pixelAt(image, x, y))) {
                    blank = false;
                    break;
                }
    }
    if(result)
        *result = blank;
    return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD clearAlpha(tTJSVariant *, tjs_int num,
                                            tTJSVariant **param,
                                            iTJSDispatch2 *layer) {
    const int threshold = num <= 0 ? 0 : static_cast<int>(param[0]->AsInteger());
    const tjs_uint32 fillColor =
        static_cast<tjs_uint32>((num > 1 ? param[1]->AsInteger() : 0) &
                                0x00ffffff);
    const LayerImage image = getLayerImage(layer, true);
    for(tjs_int y = 0; y < image.height; ++y) {
        for(tjs_int x = 0; x < image.width; ++x) {
            auto *pixel = writablePixelAt(image, x, y);
            if(pixel[3] <= threshold)
                *reinterpret_cast<tjs_uint32 *>(pixel) = fillColor;
        }
    }
    return TJS_S_OK;
}

static tjs_error TJS_INTF_METHOD getAverageColor(tTJSVariant *result,
                                                 tjs_int num,
                                                 tTJSVariant **param,
                                                 iTJSDispatch2 *layer) {
    if(num < 4)
        return TJS_E_BADPARAMCOUNT;

    const LayerImage image = getLayerImage(layer, false);
    tjs_int left = param[0]->AsInteger();
    tjs_int top = param[1]->AsInteger();
    tjs_int width = param[2]->AsInteger();
    tjs_int height = param[3]->AsInteger();
    clipRect(image, left, top, width, height);
    if(width <= 0 || height <= 0)
        TVPThrowExceptionMessage(TJS_W("invalid layer range"));

    tjs_uint64 a = 0, r = 0, g = 0, b = 0;
    for(tjs_int y = top; y < top + height; ++y) {
        for(tjs_int x = left; x < left + width; ++x) {
            ReadPtr p = pixelAt(image, x, y);
            b += p[0];
            g += p[1];
            r += p[2];
            a += p[3];
        }
    }
    const tjs_uint64 size = static_cast<tjs_uint64>(width) * height;
    const tjs_uint32 color =
        ((a / size) << 24) | ((r / size) << 16) | ((g / size) << 8) |
        (b / size);
    if(result)
        *result = static_cast<tTVInteger>(color);
    return TJS_S_OK;
}

NCB_ATTACH_FUNCTION(saveLayerImageTlg5, Layer, saveLayerImageTlg5);
NCB_ATTACH_FUNCTION(saveLayerImagePng, Layer, saveLayerImagePng);
NCB_ATTACH_FUNCTION(saveLayerImagePngOctet, Layer, saveLayerImagePngOctet);
NCB_ATTACH_FUNCTION(getCropRect, Layer, getCropRect);
NCB_ATTACH_FUNCTION(getCropRectZero, Layer, getCropRectZero);
NCB_ATTACH_FUNCTION(getDiffRect, Layer, getDiffRect);
NCB_ATTACH_FUNCTION(getDiffPixel, Layer, getDiffPixel);
NCB_ATTACH_FUNCTION(copyBlueToAlpha, Layer, copyBlueToAlpha);
NCB_ATTACH_FUNCTION(isBlank, Layer, isBlank);
NCB_ATTACH_FUNCTION(clearAlpha, Layer, clearAlpha);
NCB_ATTACH_FUNCTION(getAverageColor, Layer, getAverageColor);
