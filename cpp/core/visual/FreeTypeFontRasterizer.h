
#ifndef __FREE_TYPE_FONT_RASTERIZER_H__
#define __FREE_TYPE_FONT_RASTERIZER_H__

#include "tjsCommHead.h"
#include "CharacterData.h"
#include "FontRasterizer.h"
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class FreeTypeFontRasterizer : public FontRasterizer {
    tjs_int RefCount;
    class tFreeTypeFace *Face; //!< Non-owning current face
    std::vector<class tFreeTypeFace *> FaceFallbacks;
    std::unordered_map<std::string, std::unique_ptr<class tFreeTypeFace>>
        FaceCache;
    class tTVPNativeBaseBitmap *LastBitmap;
    tTVPFont CurrentFont;
    std::string CurrentExtentCacheFontKey;
    std::recursive_mutex Mutex;
    void ApplyFallbackFaces();
    void ClearFallbackFaces();
    class tFreeTypeFace *GetOrCreateFace(const ttstr &name,
                                         tjs_uint32 options);
    void ApplyFaceOptions(class tFreeTypeFace *face);

public:
    FreeTypeFontRasterizer();
    ~FreeTypeFontRasterizer() override;
    void AddRef() override;
    void Release() override;
    void ApplyFont(class tTVPNativeBaseBitmap *bmp, bool force) override;
    void ApplyFont(const struct tTVPFont &font) override;
    void GetTextExtent(tjs_char ch, tjs_int &w, tjs_int &h) override;
    tjs_int GetAscentHeight() override;
    tTVPCharacterData *GetBitmap(const tTVPFontAndCharacterData &font,
                                 tjs_int aofsx, tjs_int aofsy) override;
    void GetGlyphDrawRect(const ttstr &text, struct tTVPRect &area) override;
};

#endif // __FREE_TYPE_FONT_RASTERIZER_H__
