
#include "FreeTypeFontRasterizer.h"
#include "LayerBitmapIntf.h"
#include "FreeType.h"
#if _WIN32
#include <corecrt_math_defines.h>
#else
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <cmath>
#endif
#include "MsgIntf.h"
#include "FontSystem.h"
#include "FontImpl.h"
#include <complex>
#include <mutex>
#include <string>
#include <unordered_map>

extern void TVPUninitializeFreeFont();
extern FontSystem *TVPFontSystem;

namespace {
struct GlyphExtentCacheKey {
    std::string font;
    tjs_char ch = 0;

    bool operator==(const GlyphExtentCacheKey &other) const {
        return ch == other.ch && font == other.font;
    }
};

struct GlyphExtentCacheKeyHash {
    std::size_t operator()(const GlyphExtentCacheKey &key) const {
        const auto font_hash = std::hash<std::string>{}(key.font);
        const auto char_hash = std::hash<tjs_uint32>{}(
            static_cast<tjs_uint32>(key.ch));
        return font_hash ^ (char_hash + 0x9e3779b9u + (font_hash << 6) +
                            (font_hash >> 2));
    }
};

struct GlyphExtentCacheValue {
    tjs_int w = 0;
    tjs_int h = 0;
};

std::mutex TVPGlyphExtentCacheMutex;
std::unordered_map<GlyphExtentCacheKey, GlyphExtentCacheValue,
                   GlyphExtentCacheKeyHash>
    TVPGlyphExtentCache;
constexpr std::size_t TVPGlyphExtentCacheLimit = 32768;
} // namespace

void FreeTypeFontRasterizer::ApplyFaceOptions(tFreeTypeFace *face) {
    if(!face)
        return;
    face->SetHeight(CurrentFont.Height < 0 ? -CurrentFont.Height
                                           : CurrentFont.Height);
    if(CurrentFont.Flags & TVP_TF_ITALIC) {
        face->SetOption(TVP_TF_ITALIC);
    } else {
        face->ClearOption(TVP_TF_ITALIC);
    }
    if(CurrentFont.Flags & TVP_TF_BOLD) {
        face->SetOption(TVP_TF_BOLD);
    } else {
        face->ClearOption(TVP_TF_BOLD);
    }
    if(CurrentFont.Flags & TVP_TF_UNDERLINE) {
        face->SetOption(TVP_TF_UNDERLINE);
    } else {
        face->ClearOption(TVP_TF_UNDERLINE);
    }
    if(CurrentFont.Flags & TVP_TF_STRIKEOUT) {
        face->SetOption(TVP_TF_STRIKEOUT);
    } else {
        face->ClearOption(TVP_TF_STRIKEOUT);
    }
}

void FreeTypeFontRasterizer::ClearFallbackFaces() {
    for(auto *face : FaceFallbacks) {
        delete face;
    }
    FaceFallbacks.clear();
}

void FreeTypeFontRasterizer::ApplyFallbackFaces() {
    if(!Face || !FaceFallbacks.empty())
        return;

    std::vector<ttstr> candidates;
    const ttstr &current = Face->GetFontName();
    const ttstr &default_font = TVPGetDefaultFontName();
    auto append_unique = [&](const ttstr &name) {
        if(name.IsEmpty() || name == current)
            return;
        for(const auto &existing : candidates) {
            if(existing == name)
                return;
        }
        candidates.emplace_back(name);
    };

    append_unique(default_font);
    std::vector<ttstr> all_fonts;
    TVPGetAllFontList(all_fonts);
    for(const auto &name : all_fonts) {
        append_unique(name);
    }

    for(const auto &name : candidates) {
        auto *fallback = new tFreeTypeFace(name, 0);
        ApplyFaceOptions(fallback);
        FaceFallbacks.emplace_back(fallback);
    }
}

FreeTypeFontRasterizer::FreeTypeFontRasterizer() :
    RefCount(0), Face(nullptr), LastBitmap(nullptr) {
    AddRef();
}
FreeTypeFontRasterizer::~FreeTypeFontRasterizer() {

    delete Face;
    Face = nullptr;
    ClearFallbackFaces();
    TVPUninitializeFreeFont();
}
void FreeTypeFontRasterizer::AddRef() { RefCount++; }
//---------------------------------------------------------------------------
void FreeTypeFontRasterizer::Release() {
    RefCount--;
    LastBitmap = nullptr;
    if(RefCount == 0) {

        delete Face;
        Face = nullptr;
        ClearFallbackFaces();
        delete this;
    }
}
//---------------------------------------------------------------------------
void FreeTypeFontRasterizer::ApplyFont(class tTVPNativeBaseBitmap *bmp,
                                       bool force) {
    if(bmp != LastBitmap || force) {
        ApplyFont(bmp->GetFont());
        LastBitmap = bmp;
    }
}
//---------------------------------------------------------------------------
void FreeTypeFontRasterizer::ApplyFont(const tTVPFont &font) {
    CurrentFont = font;
    ttstr stdname = TVPFontSystem->GetBeingFont(font.Face);
    // TVP_FACE_OPTIONS_NO_ANTIALIASING
    // TVP_FACE_OPTIONS_NO_HINTING
    // TVP_FACE_OPTIONS_FORCE_AUTO_HINTING
    tjs_uint32 opt = 0;
    opt |= (font.Flags & TVP_TF_ITALIC) ? TVP_TF_ITALIC : 0;
    opt |= (font.Flags & TVP_TF_BOLD) ? TVP_TF_BOLD : 0;
    opt |= (font.Flags & TVP_TF_UNDERLINE) ? TVP_TF_UNDERLINE : 0;
    opt |= (font.Flags & TVP_TF_STRIKEOUT) ? TVP_TF_STRIKEOUT : 0;
    opt |= (font.Flags & TVP_TF_FONTFILE) ? TVP_FACE_OPTIONS_FILE : 0;
    bool recreate = false;
    if(Face) {
        if(Face->GetFontName() != stdname) {
            delete Face;
            Face = nullptr;
            ClearFallbackFaces();
            Face = new tFreeTypeFace(stdname, opt);
            recreate = true;
        }
    } else {
        Face = new tFreeTypeFace(stdname, opt);
        ClearFallbackFaces();
        recreate = true;
    }
    Face->SetHeight(font.Height < 0 ? -font.Height : font.Height);
    if(recreate == false) {
        if(font.Flags & TVP_TF_ITALIC) {
            Face->SetOption(TVP_TF_ITALIC);
        } else {
            Face->ClearOption(TVP_TF_ITALIC);
        }
        if(font.Flags & TVP_TF_BOLD) {
            Face->SetOption(TVP_TF_BOLD);
        } else {
            Face->ClearOption(TVP_TF_BOLD);
        }
        if(font.Flags & TVP_TF_UNDERLINE) {
            Face->SetOption(TVP_TF_UNDERLINE);
        } else {
            Face->ClearOption(TVP_TF_UNDERLINE);
        }
        if(font.Flags & TVP_TF_STRIKEOUT) {
            Face->SetOption(TVP_TF_STRIKEOUT);
        } else {
            Face->ClearOption(TVP_TF_STRIKEOUT);
        }
    }
    for(auto *fallback : FaceFallbacks) {
        ApplyFaceOptions(fallback);
    }
    const tjs_int height = font.Height < 0 ? -font.Height : font.Height;
    const ttstr &resolved_face = Face ? Face->GetFontName() : font.Face;
    CurrentExtentCacheFontKey = resolved_face.AsStdString() + "|" +
                                std::to_string(height) + "|" +
                                std::to_string(font.Flags) + "|" +
                                std::to_string(TVPFontNames.GetCount());
    LastBitmap = nullptr;
}
//---------------------------------------------------------------------------
static bool isUnicodeSpace(char16_t ch);
void FreeTypeFontRasterizer::GetTextExtent(tjs_char ch, tjs_int &w,
                                           tjs_int &h) {
    if(!Face)
        return;

    GlyphExtentCacheKey key{CurrentExtentCacheFontKey, ch};
    {
        std::lock_guard<std::mutex> lock(TVPGlyphExtentCacheMutex);
        auto it = TVPGlyphExtentCache.find(key);
        if(it != TVPGlyphExtentCache.end()) {
            w = it->second.w;
            h = it->second.h;
            return;
        }
    }

    tjs_int resolved_w = 0;
    tjs_int resolved_h = 0;
    if(Face) {
        tGlyphMetrics metrics{};
        if(Face->GetGlyphSizeFromCharcode(ch, metrics)) {
            resolved_w = metrics.CellIncX;
            resolved_h = metrics.CellIncY;
        } else if(!isUnicodeSpace(ch)) {
            ApplyFallbackFaces();
            for(auto *fallback : FaceFallbacks) {
                if(fallback->GetGlyphSizeFromCharcode(ch, metrics)) {
                    resolved_w = metrics.CellIncX;
                    resolved_h = metrics.CellIncY;
                    break;
                }
            }
            if(resolved_w == 0 && resolved_h == 0) {
                resolved_w = Face->GetHeight();
                resolved_h = resolved_w;
            }
        } else {
            resolved_w = Face->GetHeight();
            resolved_h = resolved_w;
        }
    }
    w = resolved_w;
    h = resolved_h;

    std::lock_guard<std::mutex> lock(TVPGlyphExtentCacheMutex);
    if(TVPGlyphExtentCache.size() >= TVPGlyphExtentCacheLimit) {
        TVPGlyphExtentCache.clear();
    }
    TVPGlyphExtentCache.emplace(std::move(key),
                                GlyphExtentCacheValue{resolved_w, resolved_h});
}
//---------------------------------------------------------------------------
tjs_int FreeTypeFontRasterizer::GetAscentHeight() {
    if(Face)
        return Face->GetAscent();
    return 0;
}
static bool isUnicodeSpace(char16_t ch) {
    return (ch >= 0x0009 && ch <= 0x000D) || ch == 0x0020 || ch == 0x0085 ||
        ch == 0x00A0 || ch == 0x1680 || (ch >= 0x2000 && ch <= 0x200A) ||
        ch == 0x2028 || ch == 0x2029 || ch == 0x202F || ch == 0x205F ||
        ch == 0x3000;
}
//---------------------------------------------------------------------------
tTVPCharacterData *
FreeTypeFontRasterizer::GetBitmap(const tTVPFontAndCharacterData &font,
                                  tjs_int aofsx, tjs_int aofsy) {
    if(!Face)
        return nullptr;
    if(font.Antialiased) {
        Face->ClearOption(TVP_FACE_OPTIONS_NO_ANTIALIASING);
    } else {
        Face->SetOption(TVP_FACE_OPTIONS_NO_ANTIALIASING);
    }
    if(font.Hinting) {
        Face->ClearOption(TVP_FACE_OPTIONS_NO_HINTING);
        // Face->SetOption( TVP_FACE_OPTIONS_FORCE_AUTO_HINTING );
    } else {
        Face->SetOption(TVP_FACE_OPTIONS_NO_HINTING);
        // Face->ClearOption( TVP_FACE_OPTIONS_FORCE_AUTO_HINTING );
    }
    tTVPCharacterData *data = Face->GetGlyphFromCharcode(font.Character);
    if(!data && !isUnicodeSpace(font.Character)) {
        ApplyFallbackFaces();
        for(auto *fallback : FaceFallbacks) {
            data = fallback->GetGlyphFromCharcode(font.Character);
            if(data)
                break;
        }
    }
    if(data == nullptr) {
        data = Face->GetGlyphFromCharcode(Face->GetDefaultChar());
    }
    if(data == nullptr) {
        data = Face->GetGlyphFromCharcode(Face->GetFirstChar());
    }
    if(data == nullptr) {
        TVPThrowExceptionMessage(TVPFontRasterizeError);
    }

    int cx = data->Metrics.CellIncX;
    int cy = data->Metrics.CellIncY;
    if(font.Font.Angle == 0) {
        data->Metrics.CellIncX = cx;
        data->Metrics.CellIncY = 0;
    } else if(font.Font.Angle == 2700) {
        data->Metrics.CellIncX = 0;
        data->Metrics.CellIncY = cx;
    } else {
        double angle = font.Font.Angle * (M_PI / 1800);
        data->Metrics.CellIncX = static_cast<tjs_int>(std::cos(angle) * cx);
        data->Metrics.CellIncY = static_cast<tjs_int>(-std::sin(angle) * cx);
    }

    data->Antialiased = font.Antialiased;
    data->FullColored = false;
    data->Blured = font.Blured;
    data->BlurWidth = font.BlurWidth;
    data->BlurLevel = font.BlurLevel;
    data->OriginX += aofsx; // for vertical text
                            //	data->OriginY += aofsy;

    // apply blur
    if(font.Blured)
        data->Blur(); // nasty ...
    return data;
}
//---------------------------------------------------------------------------
void FreeTypeFontRasterizer::GetGlyphDrawRect(const ttstr &text,
                                              tTVPRect &area) {
    if(!Face) {
        area.left = area.top = area.right = area.bottom = 0;
        return;
    }
    Face->ClearOption(TVP_FACE_OPTIONS_NO_ANTIALIASING);
    Face->ClearOption(TVP_FACE_OPTIONS_NO_HINTING);

    area.left = area.top = area.right = area.bottom = 0;
    tjs_int offsetx = 0;
    tjs_int offsety = 0;
    tjs_uint len = text.length();
    for(tjs_uint i = 0; i < len; i++) {
        tjs_char ch = text[i];
        tjs_int ax, ay;
        tTVPRect rt(0, 0, 0, 0);
        bool result = Face->GetGlyphRectFromCharcode(rt, ch, ax, ay);
        if(result == false && !isUnicodeSpace(ch)) {
            ApplyFallbackFaces();
            for(auto *fallback : FaceFallbacks) {
                result = fallback->GetGlyphRectFromCharcode(rt, ch, ax, ay);
                if(result)
                    break;
            }
        }
        if(result == false)
            result = Face->GetGlyphRectFromCharcode(rt, Face->GetDefaultChar(),
                                                    ax, ay);
        if(result == false)
            result = Face->GetGlyphRectFromCharcode(rt, Face->GetFirstChar(),
                                                    ax, ay);
        if(result) {
            rt.add_offsets(offsetx, offsety);
            if(i != 0) {
                area.do_union(rt);
            } else {
                area = rt;
            }
        }
        offsetx += ax;
        offsety = 0;
    }
}
