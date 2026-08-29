
#ifndef __FONT_RASTERIZER_H__
#define __FONT_RASTERIZER_H__

#include "tjsTypes.h"
#include "tjsString.h"

class FontRasterizer {

public:
    virtual ~FontRasterizer() = default;
    virtual void AddRef() = 0;
    virtual void Release() = 0;
    virtual void ApplyFont(class tTVPNativeBaseBitmap *bmp, bool force) = 0;
    virtual void ApplyFont(const struct tTVPFont &font) = 0;
    virtual void GetTextExtent(tjs_char ch, tjs_int &w, tjs_int &h) = 0;
    // Code-point overload used by Unicode-aware layout.  Keeping the legacy
    // UTF-16-unit virtual intact preserves third-party/GDI rasterizer ABI;
    // implementations that do not support supplementary planes naturally
    // fall back to the old one-unit query.
    virtual void GetTextExtent(tjs_uint32 codepoint, tjs_int &w,
                               tjs_int &h) {
        GetTextExtent(static_cast<tjs_char>(codepoint), w, h);
    }
    virtual tjs_int GetAscentHeight() = 0;
    virtual class tTVPCharacterData *
    GetBitmap(const struct tTVPFontAndCharacterData &font, tjs_int aofsx,
              tjs_int aofsy) = 0;
    virtual void GetGlyphDrawRect(const TJS::ttstr &text,
                                  struct tTVPRect &area) = 0;
};

#endif // __FREE_TYPE_FONT_RASTERIZER_H__
