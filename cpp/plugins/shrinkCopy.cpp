#include "ncbind.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <spdlog/spdlog.h>

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

#define NCB_MODULE_NAME TJS_W("shrinkCopy.dll")

namespace {

struct LayerPixels {
    iTJSDispatch2 *object = nullptr;
    tjs_int width = 0;
    tjs_int height = 0;
    tjs_int pitch = 0;
    std::uint8_t *write = nullptr;
    const std::uint8_t *read = nullptr;
};

// `mainImageBufferForWrite` marks the bitmap dirty, but GPU-backed layers
// also need an explicit update notification after a plug-in writes pixels
// through the raw buffer.  The original desktop renderer happened to observe
// the CPU buffer directly; Godot's texture bridge otherwise keeps presenting
// the old (usually black) texture.
bool notifyLayerUpdate(iTJSDispatch2 *layer, tjs_int left, tjs_int top,
                       tjs_int width, tjs_int height) {
    if(!layer || width <= 0 || height <= 0)
        return false;
    tTJSVariant values[4] = {tTJSVariant(left), tTJSVariant(top),
                             tTJSVariant(width), tTJSVariant(height)};
    tTJSVariant *params[4] = {&values[0], &values[1], &values[2], &values[3]};
    static tjs_uint32 updateHint = 0;
    return TJS_SUCCEEDED(layer->FuncCall(0, TJS_W("update"), &updateHint,
                                         nullptr, 4, params, layer));
}

bool shrinkCopyTraceEnabled() {
    const char *value = std::getenv("AETHERKIRI_SHRINK_COPY_TRACE");
    return value && *value && *value != '0';
}

bool getBoolProp(iTJSDispatch2 *object, const tjs_char *name) {
    tTJSVariant value;
    if(!object || TJS_FAILED(object->PropGet(0, name, nullptr, &value, object)))
        return false;
    return value.AsInteger() != 0;
}

bool getIntProp(iTJSDispatch2 *object, const tjs_char *name, tjs_int &out) {
    tTJSVariant value;
    if(!object || TJS_FAILED(object->PropGet(0, name, nullptr, &value, object)))
        return false;
    out = static_cast<tjs_int>(value.AsInteger());
    return true;
}

bool getPtrProp(iTJSDispatch2 *object, const tjs_char *name,
                const std::uint8_t *&out) {
    tTJSVariant value;
    if(!object || TJS_FAILED(object->PropGet(0, name, nullptr, &value, object)))
        return false;
    out = reinterpret_cast<const std::uint8_t *>(
        static_cast<intptr_t>(value.AsInteger()));
    return out != nullptr;
}

bool getPtrProp(iTJSDispatch2 *object, const tjs_char *name,
                std::uint8_t *&out) {
    tTJSVariant value;
    if(!object || TJS_FAILED(object->PropGet(0, name, nullptr, &value, object)))
        return false;
    out = reinterpret_cast<std::uint8_t *>(
        static_cast<intptr_t>(value.AsInteger()));
    return out != nullptr;
}

bool readLayer(iTJSDispatch2 *object, LayerPixels &layer) {
    layer.object = object;
    return object && getBoolProp(object, TJS_W("hasImage")) &&
           getIntProp(object, TJS_W("imageWidth"), layer.width) &&
           getIntProp(object, TJS_W("imageHeight"), layer.height) &&
           getIntProp(object, TJS_W("mainImageBufferPitch"), layer.pitch) &&
           getPtrProp(object, TJS_W("mainImageBuffer"), layer.read) &&
           layer.width > 0 && layer.height > 0 && layer.pitch != 0;
}

bool writeLayer(iTJSDispatch2 *object, LayerPixels &layer) {
    layer.object = object;
    return object && getBoolProp(object, TJS_W("hasImage")) &&
           getIntProp(object, TJS_W("imageWidth"), layer.width) &&
           getIntProp(object, TJS_W("imageHeight"), layer.height) &&
           getIntProp(object, TJS_W("mainImageBufferPitch"), layer.pitch) &&
           getPtrProp(object, TJS_W("mainImageBufferForWrite"), layer.write) &&
           layer.width > 0 && layer.height > 0 && layer.pitch != 0;
}

bool setLayerSize(iTJSDispatch2 *object, tjs_int width, tjs_int height) {
    tTJSVariant w(width);
    tTJSVariant h(height);
    tTJSVariant *params[] = {&w, &h};
    return object &&
           TJS_SUCCEEDED(object->FuncCall(0, TJS_W("setImageSize"), nullptr,
                                          nullptr, 2, params, object));
}

const std::uint8_t *pixelAt(const LayerPixels &layer, tjs_int x, tjs_int y) {
    return layer.read + static_cast<ptrdiff_t>(y) * layer.pitch +
           static_cast<ptrdiff_t>(x) * 4;
}

std::uint8_t *pixelAtWrite(const LayerPixels &layer, tjs_int x, tjs_int y) {
    return layer.write + static_cast<ptrdiff_t>(y) * layer.pitch +
           static_cast<ptrdiff_t>(x) * 4;
}

void averageRect(const LayerPixels &src, tjs_int left, tjs_int top,
                 tjs_int right, tjs_int bottom, std::uint8_t *dst) {
    left = std::clamp(left, 0, src.width);
    right = std::clamp(right, 0, src.width);
    top = std::clamp(top, 0, src.height);
    bottom = std::clamp(bottom, 0, src.height);
    if(left >= right || top >= bottom) {
        dst[0] = dst[1] = dst[2] = 0;
        dst[3] = 0;
        return;
    }

    std::uint64_t sum[4] = {0, 0, 0, 0};
    std::uint64_t count = 0;
    for(tjs_int y = top; y < bottom; ++y) {
        const std::uint8_t *row = pixelAt(src, left, y);
        for(tjs_int x = left; x < right; ++x, row += 4) {
            sum[0] += row[0];
            sum[1] += row[1];
            sum[2] += row[2];
            sum[3] += row[3];
            ++count;
        }
    }

    dst[0] = static_cast<std::uint8_t>(sum[0] / count);
    dst[1] = static_cast<std::uint8_t>(sum[1] / count);
    dst[2] = static_cast<std::uint8_t>(sum[2] / count);
    dst[3] = static_cast<std::uint8_t>(sum[3] / count);
}

struct ShrinkCopy {
    static tjs_error TJS_INTF_METHOD layerShrinkCopy(tTJSVariant *,
                                                     tjs_int numparams,
                                                     tTJSVariant **param,
                                                     iTJSDispatch2 *dstObj) {
        if(numparams < 9)
            return TJS_E_BADPARAMCOUNT;

        const double dx = param[0]->AsReal();
        const double dy = param[1]->AsReal();
        const double dw = param[2]->AsReal();
        const double dh = param[3]->AsReal();
        iTJSDispatch2 *srcObj = param[4]->AsObjectNoAddRef();
        const tjs_int sx = static_cast<tjs_int>(param[5]->AsInteger());
        const tjs_int sy = static_cast<tjs_int>(param[6]->AsInteger());
        const tjs_int sw = static_cast<tjs_int>(param[7]->AsInteger());
        const tjs_int sh = static_cast<tjs_int>(param[8]->AsInteger());

        if(dw <= 0.0 || dh <= 0.0 || sw <= 0 || sh <= 0)
            return TJS_E_INVALIDPARAM;

        LayerPixels src;
        LayerPixels dst;
        const bool srcOk = readLayer(srcObj, src);
        const bool dstOk = writeLayer(dstObj, dst);
        if(shrinkCopyTraceEnabled()) {
            const auto sample = [](const LayerPixels &layer, tjs_int x,
                                   tjs_int y) -> tjs_uint32 {
                if(!layer.read || x < 0 || y < 0 || x >= layer.width ||
                   y >= layer.height)
                    return 0;
                const auto *p = pixelAt(layer, x, y);
                return static_cast<tjs_uint32>(p[0]) |
                       (static_cast<tjs_uint32>(p[1]) << 8) |
                       (static_cast<tjs_uint32>(p[2]) << 16) |
                       (static_cast<tjs_uint32>(p[3]) << 24);
            };
            spdlog::info(
                "shrinkCopy trace src={} {}x{} dst={} {}x{} params={} {} {}x{} srcRect={} {} {}x{} samples=0x{:08x}/0x{:08x}",
                srcOk ? 1 : 0, src.width, src.height, dstOk ? 1 : 0,
                dst.width, dst.height, dx, dy, dw, dh, sx, sy, sw, sh,
                sample(src, sx, sy),
                sample(src, sx + std::max<tjs_int>(0, sw / 2),
                       sy + std::max<tjs_int>(0, sh / 2)));
        }
        if(!srcOk || !dstOk)
            return TJS_E_INVALIDPARAM;

        const tjs_int dleft = std::max<tjs_int>(0, static_cast<tjs_int>(std::floor(dx)));
        const tjs_int dtop = std::max<tjs_int>(0, static_cast<tjs_int>(std::floor(dy)));
        const tjs_int dright = std::min<tjs_int>(
            dst.width, static_cast<tjs_int>(std::ceil(dx + dw)));
        const tjs_int dbottom = std::min<tjs_int>(
            dst.height, static_cast<tjs_int>(std::ceil(dy + dh)));

        if(dleft >= dright || dtop >= dbottom)
            return TJS_S_OK;

        for(tjs_int y = dtop; y < dbottom; ++y) {
            for(tjs_int x = dleft; x < dright; ++x) {
                const double relX0 = (static_cast<double>(x) - dx) / dw;
                const double relX1 = (static_cast<double>(x + 1) - dx) / dw;
                const double relY0 = (static_cast<double>(y) - dy) / dh;
                const double relY1 = (static_cast<double>(y + 1) - dy) / dh;
                const tjs_int sleft =
                    static_cast<tjs_int>(std::floor(sx + relX0 * sw));
                const tjs_int stop =
                    static_cast<tjs_int>(std::floor(sy + relY0 * sh));
                const tjs_int sright =
                    static_cast<tjs_int>(std::ceil(sx + relX1 * sw));
                const tjs_int sbottom =
                    static_cast<tjs_int>(std::ceil(sy + relY1 * sh));
                averageRect(src, sleft, stop, sright, sbottom,
                            pixelAtWrite(dst, x, y));
            }
        }

        notifyLayerUpdate(dstObj, dleft, dtop, dright - dleft,
                           dbottom - dtop);

        if(shrinkCopyTraceEnabled()) {
            const auto *pixel = pixelAt(dst, dleft, dtop);
            spdlog::info("shrinkCopy result rgba={},{},{},{} update={}",
                         pixel[0], pixel[1], pixel[2], pixel[3],
                         notifyLayerUpdate(dstObj, dleft, dtop,
                                           dright - dleft, dbottom - dtop)
                             ? 1
                             : 0);
        }

        return TJS_S_OK;
    }
};

struct ShrinkCopyFast {
    static tjs_error TJS_INTF_METHOD layerShrinkCopyFast(tTJSVariant *,
                                                         tjs_int numparams,
                                                         tTJSVariant **param,
                                                         iTJSDispatch2 *dstObj) {
        if(numparams < 2)
            return TJS_E_BADPARAMCOUNT;

        iTJSDispatch2 *srcObj = param[0]->AsObjectNoAddRef();
        const tjs_int stepX = static_cast<tjs_int>(param[1]->AsInteger());
        const tjs_int stepY =
            numparams >= 3 ? static_cast<tjs_int>(param[2]->AsInteger())
                           : stepX;
        if(stepX <= 0 || stepY <= 0)
            return TJS_E_INVALIDPARAM;

        LayerPixels src;
        if(!readLayer(srcObj, src))
            return TJS_E_INVALIDPARAM;

        const tjs_int width = (src.width + stepX - 1) / stepX;
        const tjs_int height = (src.height + stepY - 1) / stepY;
        if(!setLayerSize(dstObj, width, height))
            return TJS_E_FAIL;

        LayerPixels dst;
        if(!writeLayer(dstObj, dst))
            return TJS_E_FAIL;

        for(tjs_int y = 0; y < height; ++y) {
            const tjs_int top = y * stepY;
            const tjs_int bottom = std::min(src.height, top + stepY);
            for(tjs_int x = 0; x < width; ++x) {
                const tjs_int left = x * stepX;
                const tjs_int right = std::min(src.width, left + stepX);
                averageRect(src, left, top, right, bottom,
                            pixelAtWrite(dst, x, y));
            }
        }

        notifyLayerUpdate(dstObj, 0, 0, width, height);

        return TJS_S_OK;
    }
};

} // namespace

NCB_ATTACH_FUNCTION(shrinkCopy, Layer, ShrinkCopy::layerShrinkCopy);
NCB_ATTACH_FUNCTION(shrinkCopyFast, Layer, ShrinkCopyFast::layerShrinkCopyFast);
