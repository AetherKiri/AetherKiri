// layerExRaster — Layer raster scroll copy effect
// Ported from https://github.com/wamsoft/layerExRaster

#include "ncbind.hpp"
#include <cstdio>
#define _USE_MATH_DEFINES
#include <cmath>

#define NCB_MODULE_NAME TJS_W("layerExRaster.dll")

#include "layerExBase_wamsoft.hpp"

struct layerExRaster : public layerExBase
{
    layerExRaster(DispatchT obj) : layerExBase(obj) {}

    void copyRaster(tTJSVariant layer, int maxh, int lines, int cycle, tjs_int64 time) {
        tjs_int width, height, pitch;
        unsigned char* buffer;
        {
            iTJSDispatch2 *layerobj = layer.AsObjectNoAddRef();
            tTJSVariant var;
            layerobj->PropGet(0, TJS_W("imageWidth"), NULL, &var, layerobj);
            width = (tjs_int)var;
            layerobj->PropGet(0, TJS_W("imageHeight"), NULL, &var, layerobj);
            height = (tjs_int)var;
            layerobj->PropGet(0, TJS_W("mainImageBuffer"), NULL, &var, layerobj);
            buffer = (unsigned char*)(tjs_intptr_t)(tTVInteger)var;
            layerobj->PropGet(0, TJS_W("mainImageBufferPitch"), NULL, &var, layerobj);
            pitch = (tjs_int)var;
        }

        if (_width != width || _height != height) return;

        constexpr double kPi = 3.14159265358979323846;
        double omega = 2 * kPi / lines;
        tjs_int CurH = (tjs_int)maxh;
        double rad = -omega * time / cycle * (height / 2);

        rad += omega * _clipTop;
        _buffer += _pitch * _clipTop + _clipLeft * 4;
        buffer  +=  pitch * _clipTop + _clipLeft * 4;

        for (tjs_int n = 0; n < _clipHeight; n++, rad += omega) {
            tjs_int d = (tjs_int)(sin(rad) * CurH);
            if (d >= 0) {
                int w = _clipWidth - d;
                const tjs_uint32 *src = (const tjs_uint32*)(buffer + n * pitch);
                tjs_uint32 *dest = (tjs_uint32*)(_buffer + n * _pitch) + d;
                for (tjs_int i = 0; i < w; i++) *dest++ = *src++;
            } else {
                int w = _clipWidth + d;
                const tjs_uint32 *src = (const tjs_uint32*)(buffer + n * pitch) - d;
                tjs_uint32 *dest = (tjs_uint32*)(_buffer + n * _pitch);
                for (tjs_int i = 0; i < w; i++) *dest++ = *src++;
            }
        }
        redraw();
    }
};

NCB_GET_INSTANCE_HOOK(layerExRaster)
{
    NCB_INSTANCE_GETTER(objthis) {
        ClassT* obj = GetNativeInstance(objthis);
        if (!obj) {
            obj = new ClassT(objthis);
            SetNativeInstance(objthis, obj);
        }
        obj->reset();
        return obj;
    }
    ~NCB_GET_INSTANCE_HOOK_CLASS() {}
};

NCB_ATTACH_CLASS_WITH_HOOK(layerExRaster, Layer) {
    NCB_METHOD(copyRaster);
}
