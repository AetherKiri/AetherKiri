// layerExBTOA — Layer alpha/province manipulation plugin
// Ported from https://github.com/wamsoft/layerExBTOA

#include "ncbind.hpp"
#include <vector>

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

#define NCB_MODULE_NAME TJS_W("layerExBTOA.dll")

#ifndef WIN32
typedef unsigned char BYTE;
typedef tjs_uint16 WORD;
typedef tjs_uint32 DWORD;
#endif
#define ltAddAlpha 12

typedef unsigned char       *WrtRefT;
typedef unsigned char const *ReadRefT;
typedef tjs_uint32           PixelT;

static tjs_uint32 hasImageHint, imageWidthHint, imageHeightHint;
static tjs_uint32 mainImageBufferHint, mainImageBufferPitchHint, mainImageBufferForWriteHint;
static tjs_uint32 provinceImageBufferHint, provinceImageBufferPitchHint, provinceImageBufferForWriteHint;
static tjs_uint32 clipLeftHint, clipTopHint, clipWidthHint, clipHeightHint;
static tjs_uint32 updateHint;
static tjs_uint32 typeHint;

static iTJSDispatch2 *getLayerClass(void) {
    tTJSVariant var;
    TVPExecuteExpression(TJS_W("Layer"), &var);
    return var.AsObjectNoAddRef();
}

static bool
GetLayerSize(iTJSDispatch2 *lay, tjs_int32 &w, tjs_int32 &h, tjs_int32 &pitch) {
    iTJSDispatch2 *layerClass = getLayerClass();
    if (!lay || TJS_FAILED(lay->IsInstanceOf(0, 0, 0, TJS_W("Layer"), lay)))
        return false;

    tTJSVariant val;
    if (TJS_FAILED(layerClass->PropGet(0, TJS_W("hasImage"), &hasImageHint, &val, lay)) ||
        (val.AsInteger() == 0))
        return false;

    if (TJS_FAILED(layerClass->PropGet(0, TJS_W("imageWidth"), &imageWidthHint, &val, lay)))
        return false;
    w = (tjs_int32)val.AsInteger();

    if (TJS_FAILED(layerClass->PropGet(0, TJS_W("imageHeight"), &imageHeightHint, &val, lay)))
        return false;
    h = (tjs_int32)val.AsInteger();

    if (TJS_FAILED(layerClass->PropGet(0, TJS_W("mainImageBufferPitch"), &mainImageBufferPitchHint, &val, lay)))
        return false;
    pitch = (tjs_int32)val.AsInteger();

    return (w > 0 && h > 0 && pitch != 0);
}

static bool
GetLayerBufferAndSize(iTJSDispatch2 *lay, tjs_int32 &w, tjs_int32 &h, WrtRefT &ptr, tjs_int32 &pitch) {
    iTJSDispatch2 *layerClass = getLayerClass();
    if (!lay || TJS_FAILED(lay->IsInstanceOf(0, 0, 0, TJS_W("Layer"), lay)))
        return false;

    tTJSVariant val;
    if (TJS_FAILED(layerClass->PropGet(0, TJS_W("hasImage"), &hasImageHint, &val, lay)) ||
        (val.AsInteger() == 0))
        return false;
    if (TJS_FAILED(layerClass->PropGet(0, TJS_W("imageWidth"), &imageWidthHint, &val, lay)))
        return false;
    w = (tjs_int32)val.AsInteger();
    if (TJS_FAILED(layerClass->PropGet(0, TJS_W("imageHeight"), &imageHeightHint, &val, lay)))
        return false;
    h = (tjs_int32)val.AsInteger();

    if (TJS_FAILED(layerClass->PropGet(0, TJS_W("mainImageBufferForWrite"), &mainImageBufferForWriteHint, &val, lay)))
        return false;
    ptr = reinterpret_cast<WrtRefT>(val.AsInteger());
    if (ptr == 0) return false;

    if (TJS_FAILED(layerClass->PropGet(0, TJS_W("mainImageBufferPitch"), &mainImageBufferPitchHint, &val, lay)))
        return false;
    pitch = (tjs_int32)val.AsInteger();

    return (w > 0 && h > 0 && pitch != 0);
}

static bool
GetClipSize(iTJSDispatch2 *lay, tjs_int32 &l, tjs_int32 &t, tjs_int32 &w, tjs_int32 &h, tjs_int32 &pitch) {
    iTJSDispatch2 *layerClass = getLayerClass();
    if (!lay || TJS_FAILED(lay->IsInstanceOf(0, 0, 0, TJS_W("Layer"), lay)))
        return false;

    tTJSVariant val;
    if (TJS_FAILED(layerClass->PropGet(0, TJS_W("hasImage"), &hasImageHint, &val, lay)) ||
        (val.AsInteger() == 0))
        return false;

    if (TJS_FAILED(layerClass->PropGet(0, TJS_W("clipLeft"), &clipLeftHint, &val, lay)))
        return false;
    l = (tjs_int32)val.AsInteger();
    if (TJS_FAILED(layerClass->PropGet(0, TJS_W("clipTop"), &clipTopHint, &val, lay)))
        return false;
    t = (tjs_int32)val.AsInteger();
    if (TJS_FAILED(layerClass->PropGet(0, TJS_W("clipWidth"), &clipWidthHint, &val, lay)))
        return false;
    w = (tjs_int32)val.AsInteger();
    if (TJS_FAILED(layerClass->PropGet(0, TJS_W("clipHeight"), &clipHeightHint, &val, lay)))
        return false;
    h = (tjs_int32)val.AsInteger();

    if (TJS_FAILED(layerClass->PropGet(0, TJS_W("mainImageBufferPitch"), &mainImageBufferPitchHint, &val, lay)))
        return false;
    pitch = (tjs_int32)val.AsInteger();

    return (w > 0 && h > 0 && pitch != 0);
}

static bool
GetClipBufferAndSize(iTJSDispatch2 *lay, tjs_int32 &l, tjs_int32 &t, tjs_int32 &w, tjs_int32 &h,
                     WrtRefT &ptr, tjs_int32 &pitch) {
    iTJSDispatch2 *layerClass = getLayerClass();
    if (!lay || TJS_FAILED(lay->IsInstanceOf(0, 0, 0, TJS_W("Layer"), lay)))
        return false;

    tTJSVariant val;
    if (TJS_FAILED(layerClass->PropGet(0, TJS_W("hasImage"), &hasImageHint, &val, lay)) ||
        (val.AsInteger() == 0))
        return false;

    if (TJS_FAILED(layerClass->PropGet(0, TJS_W("clipLeft"), &clipLeftHint, &val, lay)))
        return false;
    l = (tjs_int32)val.AsInteger();
    if (TJS_FAILED(layerClass->PropGet(0, TJS_W("clipTop"), &clipTopHint, &val, lay)))
        return false;
    t = (tjs_int32)val.AsInteger();
    if (TJS_FAILED(layerClass->PropGet(0, TJS_W("clipWidth"), &clipWidthHint, &val, lay)))
        return false;
    w = (tjs_int32)val.AsInteger();
    if (TJS_FAILED(layerClass->PropGet(0, TJS_W("clipHeight"), &clipHeightHint, &val, lay)))
        return false;
    h = (tjs_int32)val.AsInteger();

    if (TJS_FAILED(layerClass->PropGet(0, TJS_W("mainImageBufferForWrite"), &mainImageBufferForWriteHint, &val, lay)))
        return false;
    ptr = reinterpret_cast<WrtRefT>(val.AsInteger());
    if (ptr == 0) return false;

    if (TJS_FAILED(layerClass->PropGet(0, TJS_W("mainImageBufferPitch"), &mainImageBufferPitchHint, &val, lay)))
        return false;
    pitch = (tjs_int32)val.AsInteger();
    if (w <= 0 || h <= 0 || pitch == 0) return false;

    ptr += pitch * t + l * 4;
    return true;
}

// ---------- copyRightBlueToLeftAlpha ----------

static tjs_error TJS_INTF_METHOD
copyRightBlueToLeftAlpha(tTJSVariant *result, tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *lay) {
    WrtRefT dbuf = 0;
    tjs_int32 dw, dh, dpitch;
    if (!GetLayerBufferAndSize(lay, dw, dh, dbuf, dpitch))
        TVPThrowExceptionMessage(TJS_W("dest must be Layer."));

    dw /= 2;
    WrtRefT sbuf = dbuf + dw * 4;
    dbuf += 3;
    for (int i = 0; i < dh; i++) {
        WrtRefT p = sbuf;
        WrtRefT q = dbuf;
        for (int j = 0; j < dw; j++) {
            *q = *p;
            p += 4;
            q += 4;
        }
        sbuf += dpitch;
        dbuf += dpitch;
    }
    ncbPropAccessor layObj(lay);
    layObj.FuncCall(0, TJS_W("update"), &updateHint, NULL, 0, 0, dw, dh);
    return TJS_S_OK;
}

// ---------- copyBottomBlueToTopAlpha ----------

static tjs_error TJS_INTF_METHOD
copyBottomBlueToTopAlpha(tTJSVariant *result, tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *lay) {
    WrtRefT dbuf = 0;
    tjs_int32 dw, dh, dpitch;
    if (!GetLayerBufferAndSize(lay, dw, dh, dbuf, dpitch))
        TVPThrowExceptionMessage(TJS_W("dest must be Layer."));

    dh /= 2;
    WrtRefT sbuf = dbuf + dh * dpitch;
    dbuf += 3;
    for (int i = 0; i < dh; i++) {
        WrtRefT p = sbuf;
        WrtRefT q = dbuf;
        for (int j = 0; j < dw; j++) {
            *q = *p;
            p += 4;
            q += 4;
        }
        sbuf += dpitch;
        dbuf += dpitch;
    }
    ncbPropAccessor layObj(lay);
    layObj.FuncCall(0, TJS_W("update"), &updateHint, NULL, 0, 0, dw, dh);
    return TJS_S_OK;
}

// ---------- fillAlpha ----------

static tjs_error TJS_INTF_METHOD
fillAlpha(tTJSVariant *result, tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *lay) {
    WrtRefT dbuf = 0;
    tjs_int32 l, t, dw, dh, dpitch;
    if (!GetClipBufferAndSize(lay, l, t, dw, dh, dbuf, dpitch))
        TVPThrowExceptionMessage(TJS_W("dest must be Layer."));

    dbuf += 3;
    for (int i = 0; i < dh; i++) {
        WrtRefT q = dbuf;
        for (int j = 0; j < dw; j++) {
            *q = 0xff;
            q += 4;
        }
        dbuf += dpitch;
    }
    ncbPropAccessor layObj(lay);
    layObj.FuncCall(0, TJS_W("update"), &updateHint, NULL, l, t, dw, dh);
    return TJS_S_OK;
}

// ---------- copyAlphaToProvince ----------

static tjs_error TJS_INTF_METHOD
copyAlphaToProvince(tTJSVariant *result, tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *lay) {
    iTJSDispatch2 *layerClass = getLayerClass();

    ReadRefT sbuf = 0;
    WrtRefT  dbuf = 0;
    tjs_int32 l, t, w, h, spitch, dpitch, threshold = -1, matched = 1, otherwise = 0;
    if (TJS_PARAM_EXIST(0))
        threshold = (tjs_int32)(param[0]->AsInteger());
    if (TJS_PARAM_EXIST(1))
        matched = (tjs_int32)(param[1]->AsInteger());
    if (TJS_PARAM_EXIST(2))
        otherwise = (tjs_int32)(param[2]->AsInteger());

    if (!GetClipSize(lay, l, t, w, h, spitch))
        TVPThrowExceptionMessage(TJS_W("src must be Layer."));

    tTJSVariant val;
    if (TJS_FAILED(layerClass->PropGet(0, TJS_W("mainImageBuffer"), &mainImageBufferHint, &val, lay)) ||
        (sbuf = reinterpret_cast<ReadRefT>(val.AsInteger())) == NULL)
        TVPThrowExceptionMessage(TJS_W("src has no image."));
    sbuf += spitch * t + l * 4;

    val.Clear();
    if (TJS_FAILED(layerClass->PropGet(0, TJS_W("provinceImageBufferForWrite"), &provinceImageBufferForWriteHint, &val, lay)) ||
        (dbuf = reinterpret_cast<WrtRefT>(val.AsInteger())) == NULL)
        TVPThrowExceptionMessage(TJS_W("dst has no province image."));
    val.Clear();
    if (TJS_FAILED(layerClass->PropGet(0, TJS_W("provinceImageBufferPitch"), &provinceImageBufferPitchHint, &val, lay)) ||
        (dpitch = (tjs_int32)val.AsInteger()) == 0)
        TVPThrowExceptionMessage(TJS_W("dst has no province pitch."));
    dbuf += dpitch * t + l;

    sbuf += 3;
    unsigned char th = (unsigned char)threshold;
    unsigned char on  = (unsigned char)matched;
    unsigned char off = (unsigned char)otherwise;
    int mode = 0;
    if (threshold >= 0 && threshold < 256) {
        bool enmatch = (matched   >= 0 && matched   < 256);
        bool enother = (otherwise >= 0 && otherwise < 256);
        if (!enmatch && !enother) return TJS_S_OK;
        mode = (enmatch && enother) ? 1 : enmatch ? 3 : 4;
    } else if (threshold >= 256) {
        if (otherwise >= 0 && otherwise < 256) mode = 2;
        else return TJS_S_OK;
    }

    for (int y = 0; y < h; y++) {
        WrtRefT  p = dbuf;
        ReadRefT q = sbuf;
        switch (mode) {
        case 0: for (int x = 0; x < w; x++, q += 4) *p++ = *q; break;
        case 1: for (int x = 0; x < w; x++, q += 4) *p++ = (*q >= th) ? on : off; break;
        case 2: for (int x = 0; x < w; x++, q += 4) *p++ = off; break;
        case 3: for (int x = 0; x < w; x++, q += 4, p++) if (*q >= th) *p = on; break;
        case 4: for (int x = 0; x < w; x++, q += 4, p++) if (*q < th) *p = off; break;
        }
        sbuf += spitch;
        dbuf += dpitch;
    }
    ncbPropAccessor layObj(lay);
    layObj.FuncCall(0, TJS_W("update"), &updateHint, NULL, l, t, w, h);
    return TJS_S_OK;
}

// ---------- clipAlphaRect ----------

static inline PixelT ClipAddAlpha(PixelT const pixel, PixelT alpha) {
    const PixelT mask = 0xFF;
    if (!((~alpha) & mask)) return pixel;
    if (!(  alpha  & mask)) return 0;

    const PixelT col_a = ((pixel >> 24) & mask) * alpha;
    const PixelT col_r = ((pixel >> 16) & mask) * alpha;
    const PixelT col_g = ((pixel >>  8) & mask) * alpha;
    const PixelT col_b = ((pixel      ) & mask) * alpha;
    return ( (((col_a + (col_a >> 7)) >> 8) << 24) |
             (((col_r + (col_r >> 7)) >> 8) << 16) |
             (((col_g + (col_g >> 7)) >> 8) <<  8) |
             (((col_b + (col_b >> 7)) >> 8)      ) );
}

static inline bool CalcClipArea(
    tjs_int32 &dx, tjs_int32 &dy, const tjs_int32 diw, const tjs_int32 dih,
    tjs_int32 &sx, tjs_int32 &sy, const tjs_int32 siw, const tjs_int32 sih,
    tjs_int32 &w, tjs_int32 &h)
{
    if (sx + w <= 0   || sy + h <= 0   || sx >= siw || sy >= sih) return true;
    if (sx < 0) { w += sx; dx -= sx; sx = 0; }
    if (sy < 0) { h += sy; dy -= sy; sy = 0; }
    tjs_int32 cut;
    if ((cut = sx + w - siw) > 0) w -= cut;
    if ((cut = sy + h - sih) > 0) h -= cut;
    if (dx + w <= 0   || dy + h <= 0   || dx >= diw || dy >= dih) return true;
    if (dx < 0) { w += dx; sx -= dx; dx = 0; }
    if (dy < 0) { h += dy; sy -= dy; dy = 0; }
    if ((cut = dx + w - diw) > 0) w -= cut;
    if ((cut = dy + h - dih) > 0) h -= cut;
    return (w <= 0 || h <= 0);
}

static inline iTJSDispatch2 *GetSrcDstLayerInfo(
    iTJSDispatch2 *src, tjs_int32 &siw, tjs_int32 &sih, tjs_int32 &spitch, ReadRefT &sbuf,
    iTJSDispatch2 *dst, tjs_int32 &dl, tjs_int32 &dt, tjs_int32 &diw, tjs_int32 &dih, tjs_int32 &dpitch, WrtRefT &dbuf)
{
    iTJSDispatch2 *layerClass = getLayerClass();
    tTJSVariant val;

    if (!GetClipSize(dst, dl, dt, diw, dih, dpitch))
        TVPThrowExceptionMessage(TJS_W("dest must be Layer."));
    if (!GetLayerSize(src, siw, sih, spitch))
        TVPThrowExceptionMessage(TJS_W("src must be Layer."));

    if (TJS_FAILED(layerClass->PropGet(0, TJS_W("mainImageBuffer"), &mainImageBufferHint, &val, src))) sbuf = 0;
    else sbuf = reinterpret_cast<ReadRefT>(val.AsInteger());

    if (TJS_FAILED(layerClass->PropGet(0, TJS_W("mainImageBufferForWrite"), &mainImageBufferForWriteHint, &val, dst))) dbuf = 0;
    else dbuf = reinterpret_cast<WrtRefT>(val.AsInteger());

    if (!sbuf || !dbuf) TVPThrowExceptionMessage(TJS_W("Layer has no images."));

    if (TJS_FAILED(layerClass->PropGet(0, TJS_W("mainImageBufferPitch"), &mainImageBufferPitchHint, &val, dst)))
        TVPThrowExceptionMessage(TJS_W("dest has no pitch."));
    dpitch = (tjs_int32)val.AsInteger();

    return layerClass;
}

static tjs_error TJS_INTF_METHOD
clipAlphaRect(tTJSVariant *result, tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *dst) {
    ncbPropAccessor layObj(dst);

    ReadRefT sbuf = 0;
    WrtRefT  dbuf = 0;
    iTJSDispatch2 *src = 0;
    tjs_int32 w, h;
    tjs_int32 dx, dy, dl, dt, diw, dih, dpitch;
    tjs_int32 sx, sy, siw, sih, spitch;
    unsigned char clrval = 0;
    bool clr = false, addalpha = false;
    if (numparams < 7) return TJS_E_BADPARAMCOUNT;

    dx  = (tjs_int32)param[0]->AsInteger();
    dy  = (tjs_int32)param[1]->AsInteger();
    src =       param[2]->AsObjectNoAddRef();
    sx  = (tjs_int32)param[3]->AsInteger();
    sy  = (tjs_int32)param[4]->AsInteger();
    w   = (tjs_int32)param[5]->AsInteger();
    h   = (tjs_int32)param[6]->AsInteger();
    if (numparams >= 8 && param[7]->Type() != tvtVoid) {
        tjs_int32 n = (tjs_int32)param[7]->AsInteger();
        clr = (n >= 0 && n < 256);
        clrval = (unsigned char)(n & 255);
    }
    if (w <= 0 || h <= 0) return TJS_E_INVALIDPARAM;

    iTJSDispatch2 *layerClass = GetSrcDstLayerInfo(src, siw, sih, spitch, sbuf,
                                                   dst, dl, dt, diw, dih, dpitch, dbuf);
    {
        tTJSVariant val;
        if (TJS_FAILED(layerClass->PropGet(0, TJS_W("type"), &typeHint, &val, dst))) return TJS_E_FAIL;
        addalpha = val.AsInteger() == ltAddAlpha;
    }

    dbuf += dpitch * dt + dl * 4;
    dx -= dl;
    dy -= dt;

    if (CalcClipArea(dx, dy, diw, dih, sx, sy, siw, sih, w, h)) goto none;

    {
        tjs_int32 x, y;
        WrtRefT  p;
        ReadRefT q;

        if (!addalpha) {
            if (clr) {
                for (y = 0;    y < dy;  y++) for ((x = 0, p = dbuf + y * dpitch + 3); x < diw; x++, p += 4) *p = clrval;
                for (y = dy+h; y < dih; y++) for ((x = 0, p = dbuf + y * dpitch + 3); x < diw; x++, p += 4) *p = clrval;
            }
            for (y = 0; y < h; y++) {
                if (clr) for ((x = 0, p = dbuf + (y + dy) * dpitch + 3); x < dx; x++, p += 4) *p = clrval;

                p = dbuf + (y + dy) * dpitch + 3 + (dx * 4);
                q = sbuf + (y + sy) * spitch + 3 + (sx * 4);
                for (x = 0; x < w; x++, p += 4, q += 4) {
                    tjs_uint32 n = (tjs_uint32)(*p) * (tjs_uint32)(*q);
                    *p = (unsigned char)((n + (n >> 7)) >> 8);
                }
                if (clr) for (x = dx + w; x < diw; x++, p += 4) *p = clrval;
            }
        } else {
            PixelT *pp;
            if (clr && clrval < 255) {
                if (!clrval) {
                    for (y = 0;    y < dy;  y++) for ((x = 0, pp = (PixelT*)(dbuf + y * dpitch)); x < diw; x++) *pp++ = 0;
                    for (y = dy+h; y < dih; y++) for ((x = 0, pp = (PixelT*)(dbuf + y * dpitch)); x < diw; x++) *pp++ = 0;
                    for (y = 0; y < h; y++) {
                        for ((x = 0, pp = (PixelT*)(dbuf + (y + dy) * dpitch)); x < dx; x++) *pp++ = 0;
                        pp = (PixelT*)(dbuf + (y + dy) * dpitch + (dx * 4));
                        q  =           sbuf + (y + sy) * spitch + 3 + (sx * 4);
                        for (x = 0;    x < w;   x++, q += 4) { *pp = ClipAddAlpha(*pp, (PixelT)*q); pp++; }
                        for (x = dx+w; x < diw; x++) *pp++ = 0;
                    }
                } else {
                    PixelT cval = (PixelT)clrval;
                    for (y = 0;    y < dy;  y++) for ((x = 0, pp = (PixelT*)(dbuf + y * dpitch)); x < diw; x++) { *pp = ClipAddAlpha(*pp, cval); pp++; }
                    for (y = dy+h; y < dih; y++) for ((x = 0, pp = (PixelT*)(dbuf + y * dpitch)); x < diw; x++) { *pp = ClipAddAlpha(*pp, cval); pp++; }
                    for (y = 0; y < h; y++) {
                        for ((x = 0, pp = (PixelT*)(dbuf + (y + dy) * dpitch)); x < dx; x++) { *pp = ClipAddAlpha(*pp, cval); pp++; }
                        pp = (PixelT*)(dbuf + (y + dy) * dpitch + (dx * 4));
                        q  =           sbuf + (y + sy) * spitch + 3 + (sx * 4);
                        for (x = 0;    x < w;   x++, q += 4) { *pp = ClipAddAlpha(*pp, (PixelT)*q); pp++; }
                        for (x = dx+w; x < diw; x++) { *pp = ClipAddAlpha(*pp, cval); pp++; }
                    }
                }
            } else {
                for (y = 0; y < h; y++) {
                    pp = (PixelT*)(dbuf + (y + dy) * dpitch + (dx * 4));
                    q  =           sbuf + (y + sy) * spitch + 3 + (sx * 4);
                    for (x = 0; x < w; x++, q += 4) { *pp = ClipAddAlpha(*pp, (PixelT)*q); pp++; }
                }
            }
        }
    }

    if (clr) {
        layObj.FuncCall(0, TJS_W("update"), &updateHint, NULL, dl, dt, diw, dih);
    } else {
        layObj.FuncCall(0, TJS_W("update"), &updateHint, NULL, dl + dx, dt + dy, w, h);
    }
    return TJS_S_OK;
none:
    if (clr) {
        for (tjs_int32 y = 0; y < dih; y++) {
            WrtRefT p = dbuf + y * dpitch + 3;
            for (tjs_int32 x = 0; x < diw; x++, p += 4) *p = clrval;
        }
        layObj.FuncCall(0, TJS_W("update"), &updateHint, NULL, dl, dt, diw, dih);
    }
    return TJS_S_OK;
}

// ---------- overwrapRect ----------

static tjs_error TJS_INTF_METHOD
overwrapRect(tTJSVariant *result, tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *dst) {
    ncbPropAccessor layObj(dst);

    ReadRefT sbuf = 0;
    WrtRefT  dbuf = 0;
    iTJSDispatch2 *src = 0;
    tjs_int32 w, h;
    tjs_int32 dx, dy, dl, dt, diw, dih, dpitch;
    tjs_int32 sx, sy, siw, sih, spitch;
    unsigned char threshold = 1;
    if (numparams < 7) return TJS_E_BADPARAMCOUNT;

    dx  = (tjs_int32)param[0]->AsInteger();
    dy  = (tjs_int32)param[1]->AsInteger();
    src =       param[2]->AsObjectNoAddRef();
    sx  = (tjs_int32)param[3]->AsInteger();
    sy  = (tjs_int32)param[4]->AsInteger();
    w   = (tjs_int32)param[5]->AsInteger();
    h   = (tjs_int32)param[6]->AsInteger();
    if (numparams >= 8 && param[7]->Type() != tvtVoid) {
        tjs_int32 n = (tjs_int32)param[7]->AsInteger();
        if (n >= 0 && n < 256) threshold = (unsigned char)(n);
    }
    if (w <= 0 || h <= 0) return TJS_E_INVALIDPARAM;

    GetSrcDstLayerInfo(src, siw, sih, spitch, sbuf,
                       dst, dl, dt, diw, dih, dpitch, dbuf);

    dbuf += dpitch * dt + dl * 4;
    dx -= dl;
    dy -= dt;

    if (CalcClipArea(dx, dy, diw, dih, sx, sy, siw, sih, w, h)) return TJS_S_OK;

    for (tjs_int32 y = 0; y < h; y++) {
        WrtRefT  p = dbuf + (y + dy) * dpitch + (dx * 4);
        ReadRefT q = sbuf + (y + sy) * spitch + (sx * 4);
        for (tjs_int32 x = 0; x < w; x++, p += 4, q += 4) {
            if (q[3] >= threshold) *(reinterpret_cast<PixelT*>(p)) = *(reinterpret_cast<const PixelT*>(q));
        }
    }
    return TJS_S_OK;
}

// ---------- fillByProvince ----------

static tjs_error TJS_INTF_METHOD
fillByProvince(tTJSVariant *result, tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *lay) {
    iTJSDispatch2 *layerClass = getLayerClass();

    if (numparams < 2) return TJS_E_BADPARAMCOUNT;
    unsigned char index = (int)*param[0];
    PixelT color = (int)*param[1];

    WrtRefT dbuf = 0;
    tjs_int32 l, t, dw, dh, dpitch;
    if (!GetClipBufferAndSize(lay, l, t, dw, dh, dbuf, dpitch))
        TVPThrowExceptionMessage(TJS_W("must be Layer."));

    ReadRefT sbuf = 0;
    tjs_int32 spitch;
    {
        tTJSVariant val;
        if (TJS_FAILED(layerClass->PropGet(0, TJS_W("provinceImageBuffer"), &provinceImageBufferHint, &val, lay)) ||
            (sbuf = reinterpret_cast<ReadRefT>(val.AsInteger())) == NULL)
            TVPThrowExceptionMessage(TJS_W("no province image."));
        if (TJS_FAILED(layerClass->PropGet(0, TJS_W("provinceImageBufferPitch"), &provinceImageBufferPitchHint, &val, lay)) ||
            (spitch = (tjs_int32)val.AsInteger()) == 0)
            TVPThrowExceptionMessage(TJS_W("no province pitch."));
    }
    sbuf += t * spitch + l;

    for (int y = 0; y < dh; y++) {
        ReadRefT q = sbuf;
        PixelT *p = (PixelT*)dbuf;
        for (int x = 0; x < dw; x++) {
            if (*q == index) *(PixelT*)p = color;
            q++;
            p++;
        }
        sbuf += spitch;
        dbuf += dpitch;
    }
    ncbPropAccessor layObj(lay);
    layObj.FuncCall(0, TJS_W("update"), &updateHint, NULL, l, t, dw, dh);
    return TJS_S_OK;
}

// ---------- fillToProvince ----------

static tjs_error TJS_INTF_METHOD
fillToProvince(tTJSVariant *result, tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *lay) {
    iTJSDispatch2 *layerClass = getLayerClass();

    if (numparams < 2) return TJS_E_BADPARAMCOUNT;
    tjs_uint32 rcolor = ((tjs_uint32)(param[0])->AsInteger());
    PixelT color = rcolor & 0xFFFFFF;
    bool allmatch = rcolor < 0;
    unsigned char index = (int)*param[1];
    tjs_int32 threshold_val = 64;
    if (TJS_PARAM_EXIST(2))
        threshold_val = (tjs_int32)(param[2]->AsInteger());

    ReadRefT sbuf = 0;
    tjs_int32 l, t, dw, dh, spitch;
    if (!GetClipSize(lay, l, t, dw, dh, spitch))
        TVPThrowExceptionMessage(TJS_W("src must be Layer."));

    tTJSVariant val;
    if (TJS_FAILED(layerClass->PropGet(0, TJS_W("mainImageBuffer"), &mainImageBufferHint, &val, lay)) ||
        (sbuf = reinterpret_cast<ReadRefT>(val.AsInteger())) == NULL)
        TVPThrowExceptionMessage(TJS_W("src has no image."));
    sbuf += spitch * t + l * 4;

    WrtRefT dbuf = 0;
    tjs_int32 dpitch;
    val.Clear();
    if (TJS_FAILED(layerClass->PropGet(0, TJS_W("provinceImageBufferForWrite"), &provinceImageBufferForWriteHint, &val, lay)) ||
        (dbuf = reinterpret_cast<WrtRefT>(val.AsInteger())) == NULL)
        TVPThrowExceptionMessage(TJS_W("dst has no province image."));
    val.Clear();
    if (TJS_FAILED(layerClass->PropGet(0, TJS_W("provinceImageBufferPitch"), &provinceImageBufferPitchHint, &val, lay)) ||
        (dpitch = (tjs_int32)val.AsInteger()) == 0)
        TVPThrowExceptionMessage(TJS_W("dst has no province pitch."));
    dbuf += dpitch * t + l;

    int minx = -1, miny = -1, maxx = -1, maxy = -1;
    for (int y = 0; y < dh; y++) {
        const PixelT *q = (const PixelT*)sbuf;
        WrtRefT p = dbuf;
        for (int x = 0; x < dw; x++) {
            if ((allmatch || ((*q & 0xFFFFFF) == color)) && ((tjs_int32)(*q >> 24)) >= threshold_val) {
                *p = index;
                if (minx < 0 || minx > x) minx = x;
                if (miny < 0 || miny > y) miny = y;
                if (maxx < 0 || maxx < x) maxx = x;
                if (maxy < 0 || maxy < y) maxy = y;
            }
            q++;
            p++;
        }
        sbuf += spitch;
        dbuf += dpitch;
    }
    if (result) {
        result->Clear();
        if (minx >= 0) {
            iTJSDispatch2 *arr = TJSCreateArrayObject();
            if (arr) {
                tTJSVariant num;
                num = (minx + l);            arr->PropSetByNum(TJS_MEMBERENSURE, 0, &num, arr);
                num = (miny + t);            arr->PropSetByNum(TJS_MEMBERENSURE, 1, &num, arr);
                num = (maxx + l - minx + 1); arr->PropSetByNum(TJS_MEMBERENSURE, 2, &num, arr);
                num = (maxy + t - miny + 1); arr->PropSetByNum(TJS_MEMBERENSURE, 3, &num, arr);
                tTJSVariant v(arr, arr);
                arr->Release();
                *result = v;
            }
        }
    }
    ncbPropAccessor layObj(lay);
    layObj.FuncCall(0, TJS_W("update"), &updateHint, NULL, l, t, dw, dh);
    return TJS_S_OK;
}

// ---------- Register all methods on the Layer class ----------

NCB_ATTACH_FUNCTION(copyRightBlueToLeftAlpha, Layer, copyRightBlueToLeftAlpha);
NCB_ATTACH_FUNCTION(copyBottomBlueToTopAlpha, Layer, copyBottomBlueToTopAlpha);
NCB_ATTACH_FUNCTION(fillAlpha, Layer, fillAlpha);
NCB_ATTACH_FUNCTION(copyAlphaToProvince, Layer, copyAlphaToProvince);
NCB_ATTACH_FUNCTION(clipAlphaRect, Layer, clipAlphaRect);
NCB_ATTACH_FUNCTION(overwrapRect, Layer, overwrapRect);
NCB_ATTACH_FUNCTION(fillByProvince, Layer, fillByProvince);
NCB_ATTACH_FUNCTION(fillToProvince, Layer, fillToProvince);
