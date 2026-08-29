
#include "FreeTypeFontRasterizer.h"
#include "LayerBitmapIntf.h"
#include "FreeType.h"
#include "FontBaseline.h"
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
#include "CharacterSet.h"
#include <algorithm>
#include <complex>
#include <mutex>
#include <string>
#include <unordered_set>
#include <unordered_map>

extern FontSystem *TVPFontSystem;

namespace {
struct GlyphExtentCacheKey {
    std::string font;
    tjs_uint32 ch = 0;

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
    if(EffectiveEmojiMode == TVP_EMOJI_COLOR)
        face->SetOption(TVP_FACE_OPTIONS_COLOR);
    else
        face->ClearOption(TVP_FACE_OPTIONS_COLOR);
}

void FreeTypeFontRasterizer::ClearFallbackFaces() {
    FaceFallbacks.clear();
    FaceFallbackEmoji.clear();
}

class tFreeTypeFace *FreeTypeFontRasterizer::GetOrCreateFace(
    const ttstr &name, tjs_uint32 options, tjs_int weight,
    const ttstr &variations) {
    const std::string key = name.AsStdString() + "|" +
        std::to_string(options) + "|" + std::to_string(weight) + "|" +
        variations.AsStdString();
    auto found = FaceCache.find(key);
    if(found != FaceCache.end())
        return found->second.get();
    auto face = std::make_unique<tFreeTypeFace>(name, options);
    face->ApplyFontVariations(weight, variations);
    auto *result = face.get();
    FaceCache.emplace(key, std::move(face));
    return result;
}

void FreeTypeFontRasterizer::ApplyFallbackFaces() {
    if(!Face || !FaceFallbacks.empty())
        return;

    std::vector<ttstr> candidates;
    std::vector<bool> candidateEmoji;
    const ttstr &current = Face->GetFontName();
    const ttstr &default_font = TVPGetDefaultFontName();
    const ttstr monoEmoji(TVPGetEmojiFaceName(TVP_EMOJI_MONO));
    const ttstr colorEmoji(TVPGetEmojiFaceName(TVP_EMOJI_COLOR));
    const ttstr selectedEmoji = EffectiveEmojiMode == TVP_EMOJI_COLOR
        ? colorEmoji : monoEmoji;
    std::unordered_set<std::string> seenFaces;
    std::unordered_set<std::string> emojiFaces;
    auto faceIdentity = [](const ttstr &name) {
        if(auto *info = TVPFindFont(name)) {
            return info->Path.AsStdString() + "#" +
                std::to_string(std::max(0, info->Index));
        }
        return name.AsStdString();
    };
    const std::string currentIdentity = faceIdentity(current);
    if(!monoEmoji.IsEmpty())
        emojiFaces.insert(faceIdentity(monoEmoji));
    if(!colorEmoji.IsEmpty())
        emojiFaces.insert(faceIdentity(colorEmoji));
    auto append_unique = [&](const ttstr &name) {
        if(name.IsEmpty())
            return;
        const std::string identity = faceIdentity(name);
        if(identity == currentIdentity || !seenFaces.insert(identity).second)
            return;
        candidates.emplace_back(name);
        candidateEmoji.emplace_back(emojiFaces.find(identity) != emojiFaces.end());
    };

    append_unique(default_font);
    std::vector<ttstr> all_fonts;
    TVPGetAllFontList(all_fonts);
    // The hash-table iteration order is intentionally unspecified.  A stable
    // secondary order keeps fallback selection reproducible across runs while
    // preserving the configured default face as the first candidate.
    std::sort(all_fonts.begin(), all_fonts.end(),
              [](const ttstr &lhs, const ttstr &rhs) {
                  return lhs.AsStdString() < rhs.AsStdString();
              });
    for(const auto &name : all_fonts) {
        append_unique(name);
    }

    for(size_t i = 0; i < candidates.size(); ++i) {
        const auto &name = candidates[i];
        const tjs_uint32 options = EffectiveEmojiMode == TVP_EMOJI_COLOR
            ? TVP_FACE_OPTIONS_COLOR : 0;
        auto *fallback = GetOrCreateFace(name, options, CurrentFont.Weight,
                                         CurrentFont.Variations);
        ApplyFaceOptions(fallback);
        FaceFallbacks.emplace_back(fallback);
        FaceFallbackEmoji.emplace_back(candidateEmoji[i]);
    }

    // Keep the explicitly configured emoji face at the end of the chain.  It
    // is only added when the shared registry knows the alias; unknown names
    // must remain a no-op for legacy games.
    if((EffectiveEmojiMode == TVP_EMOJI_MONO ||
        EffectiveEmojiMode == TVP_EMOJI_COLOR) &&
       !selectedEmoji.IsEmpty() && TVPFindFont(selectedEmoji)) {
        bool already = false;
        for(const auto &existing : candidates)
            already = already || existing == selectedEmoji;
        if(!already) {
            const tjs_uint32 options = EffectiveEmojiMode == TVP_EMOJI_COLOR
                ? TVP_FACE_OPTIONS_COLOR : 0;
            auto *emojiFace = GetOrCreateFace(selectedEmoji, options,
                                               CurrentFont.Weight,
                                               CurrentFont.Variations);
            ApplyFaceOptions(emojiFace);
            FaceFallbacks.emplace_back(emojiFace);
            FaceFallbackEmoji.emplace_back(true);
        }
    }
}

FreeTypeFontRasterizer::FreeTypeFontRasterizer() :
    RefCount(0), Face(nullptr), LastBitmap(nullptr) {
    AddRef();
}
FreeTypeFontRasterizer::~FreeTypeFontRasterizer() {
    std::lock_guard<std::recursive_mutex> stateLock(Mutex);

    Face = nullptr;
    ClearFallbackFaces();
    FaceCache.clear();
}
void FreeTypeFontRasterizer::AddRef() { RefCount++; }
//---------------------------------------------------------------------------
void FreeTypeFontRasterizer::Release() {
    RefCount--;
    LastBitmap = nullptr;
    if(RefCount == 0) {

        Face = nullptr;
        ClearFallbackFaces();
        FaceCache.clear();
        delete this;
    }
}
//---------------------------------------------------------------------------
void FreeTypeFontRasterizer::ApplyFont(class tTVPNativeBaseBitmap *bmp,
                                       bool force) {
    std::lock_guard<std::recursive_mutex> stateLock(Mutex);
    if(bmp != LastBitmap || force) {
        ApplyFont(bmp->GetFont());
        LastBitmap = bmp;
    }
}
//---------------------------------------------------------------------------
void FreeTypeFontRasterizer::ApplyFont(const tTVPFont &font) {
    std::lock_guard<std::recursive_mutex> stateLock(Mutex);
    CurrentFont = font;
    EffectiveEmojiMode = TVPResolveEmojiMode(font.EmojiMode);
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
    opt |= (EffectiveEmojiMode == TVP_EMOJI_COLOR)
        ? TVP_FACE_OPTIONS_COLOR : 0;
    const std::string requestedFaceKey =
        stdname.AsStdString() + "|" + std::to_string(opt) + "|" +
        std::to_string(font.Weight) + "|" + font.Variations.AsStdString() +
        "|emoji=" + std::to_string(EffectiveEmojiMode);
    bool recreate = false;
    if(Face) {
        if(CurrentFaceCacheKey != requestedFaceKey) {
            ClearFallbackFaces();
            Face = GetOrCreateFace(stdname, opt, font.Weight,
                                   font.Variations);
            CurrentFaceCacheKey = requestedFaceKey;
            recreate = true;
        }
    } else {
        Face = GetOrCreateFace(stdname, opt, font.Weight, font.Variations);
        CurrentFaceCacheKey = requestedFaceKey;
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
        if(EffectiveEmojiMode == TVP_EMOJI_COLOR)
            Face->SetOption(TVP_FACE_OPTIONS_COLOR);
        else
            Face->ClearOption(TVP_FACE_OPTIONS_COLOR);
    }
    for(auto *fallback : FaceFallbacks) {
        ApplyFaceOptions(fallback);
    }
    const tjs_int height = font.Height < 0 ? -font.Height : font.Height;
    const ttstr &resolved_face = Face ? Face->GetFontName() : font.Face;
    CurrentExtentCacheFontKey = resolved_face.AsStdString() + "|" +
                                std::to_string(height) + "|" +
                                std::to_string(font.Flags) + "|" +
                                std::to_string(font.Weight) + "|" +
                                font.Variations.AsStdString() + "|" +
                                std::to_string(TVPFontNames.GetCount());
    LastBitmap = nullptr;
}
//---------------------------------------------------------------------------
static bool isUnicodeSpace(tjs_uint32 ch) { return TVPIsUnicodeSpace(ch); }
static bool isDefaultIgnorableUnicode(tjs_uint32 ch) {
    return TVPIsUnicodeDefaultIgnorable(ch);
}

void FreeTypeFontRasterizer::GetTextExtent(tjs_char ch, tjs_int &w,
                                           tjs_int &h) {
    GetTextExtent(static_cast<tjs_uint32>(static_cast<tjs_uint16>(ch)), w, h);
}

void FreeTypeFontRasterizer::GetTextExtent(tjs_uint32 ch, tjs_int &w,
                                           tjs_int &h) {
    std::lock_guard<std::recursive_mutex> stateLock(Mutex);
    w = 0;
    h = 0;
    if(!Face)
        return;
    if(isDefaultIgnorableUnicode(ch)) {
        w = 0;
        h = 0;
        return;
    }

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
    std::lock_guard<std::recursive_mutex> stateLock(Mutex);
    // LayerBitmap uses this value as the shared baseline anchor for mapped
    // TFT glyphs. It must match the baseline used by runtime FreeType glyphs,
    // otherwise a line mixing the two sources jumps vertically.
    if(Face)
        return Face->GetLineBaseline();
    return 0;
}

//---------------------------------------------------------------------------
tTVPCharacterData *
FreeTypeFontRasterizer::GetBitmap(const tTVPFontAndCharacterData &font,
                                  tjs_int aofsx, tjs_int aofsy) {
    std::lock_guard<std::recursive_mutex> stateLock(Mutex);
    if(!Face)
        return nullptr;
    if(isDefaultIgnorableUnicode(font.Character)) {
        auto *data = new tTVPCharacterData();
        data->Antialiased = font.Antialiased;
        data->FullColored = false;
        data->Blured = font.Blured;
        data->BlurWidth = font.BlurWidth;
        data->BlurLevel = font.BlurLevel;
        return data;
    }

    // A variation selector can override the global/font emoji policy for one
    // code point.  Rebuild the face chain only for the duration of this glyph;
    // all returned data is owned by the character cache, so restoring the
    // caller's chain afterwards is safe and keeps the public Font ABI stable.
    const bool preferEmoji =
        font.EmojiPresentation == TVP_EMOJI_PRESENTATION_EMOJI;
    const bool forceText =
        font.EmojiPresentation == TVP_EMOJI_PRESENTATION_TEXT;
    tjs_int requestedMode = EffectiveEmojiMode;
    if(preferEmoji && requestedMode == TVP_EMOJI_NONE)
        requestedMode = TVP_EMOJI_COLOR;
    if(forceText)
        requestedMode = TVP_EMOJI_NONE;
    bool reapplied = false;
    if(requestedMode != EffectiveEmojiMode) {
        tTVPFont adjusted = font.Font;
        adjusted.EmojiMode = requestedMode;
        ApplyFont(adjusted);
        reapplied = true;
    }

    // VS15/VS16 temporarily changes the mutable face chain.  Restore it on
    // every exit path, including a rasterizer exception, so one malformed
    // glyph cannot affect the following glyph.
    auto restoreFace = [&]() noexcept {
        if(!reapplied)
            return;
        try {
            ApplyFont(font.Font);
        } catch(...) {
            // Keep the original exception (if any); the next ApplyFont call
            // will retry the canonical face chain.
        }
        reapplied = false;
    };

    tTVPCharacterData *data = nullptr;
    try {
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

    ApplyFallbackFaces();
    auto tryFallback = [&](size_t index) -> bool {
        if(index >= FaceFallbacks.size())
            return false;
        auto *fallback = FaceFallbacks[index];
        data = fallback->GetGlyphFromCharcode(font.Character);
        if(!data)
            return false;
        // The caller supplies a single y coordinate for the whole line.
        // Missing glyphs from fallback faces must share the primary baseline.
        data->OriginY += krkr::font::ComputeFallbackBaselineAdjustment(
            Face->GetLineBaseline(), fallback->GetLineBaseline());
        return true;
    };

    if(preferEmoji) {
        // VS16 explicitly prefers the configured emoji face, even when the
        // primary text face also contains a monochrome star/heart glyph.
        for(size_t i = 0; i < FaceFallbacks.size() && !data; ++i) {
            if(i < FaceFallbackEmoji.size() && FaceFallbackEmoji[i])
                tryFallback(i);
        }
    }
    if(!data)
        data = Face->GetGlyphFromCharcode(font.Character);
    if(!data && !isUnicodeSpace(font.Character)) {
        for(size_t i = 0; i < FaceFallbacks.size() && !data; ++i) {
            if(preferEmoji && i < FaceFallbackEmoji.size() &&
               FaceFallbackEmoji[i])
                continue;
            tryFallback(i);
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

    // Shadows use the glyph's alpha as a coverage mask.  Converting here
    // keeps both the normal (unblurred) and blurred shadow paths compatible
    // with the legacy text-color/blend contract while the main glyph retains
    // its original RGB channels.
    if(font.Blured && data->FullColored) {
        tTVPCharacterData *mask = data->CreateAlphaMask();
        data->Release();
        data = mask;
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
    data->Blured = font.Blured;
    data->BlurWidth = font.BlurWidth;
    data->BlurLevel = font.BlurLevel;
    data->OriginX += aofsx; // for vertical text
                            //	data->OriginY += aofsy;

    // apply blur
    if(font.Blured && !data->FullColored)
        data->Blur(); // nasty ...

    restoreFace();
    return data;
    } catch(...) {
        if(data)
            data->Release();
        restoreFace();
        throw;
    }
}
//---------------------------------------------------------------------------
void FreeTypeFontRasterizer::GetGlyphDrawRect(const ttstr &text,
                                              tTVPRect &area) {
    std::lock_guard<std::recursive_mutex> stateLock(Mutex);
    if(!Face) {
        area.left = area.top = area.right = area.bottom = 0;
        return;
    }
    Face->ClearOption(TVP_FACE_OPTIONS_NO_ANTIALIASING);
    Face->ClearOption(TVP_FACE_OPTIONS_NO_HINTING);

    area.left = area.top = area.right = area.bottom = 0;
    tjs_int offsetx = 0;
    tjs_int offsety = 0;
    const tjs_size len = text.length();
    tjs_size index = 0;
    bool have_area = false;
    while(index < len) {
        tjs_uint32 ch = 0;
        tjs_size consumed = 0;
        if(!TVPReadUtf16CodePoint(text.c_str() + index, len - index, ch,
                                  consumed) || consumed == 0)
            break;
        index += consumed;
        // VS15/VS16 are presentation hints, not independent glyphs.
        if(index < len && (text[index] == TVP_EMOJI_VS15 ||
                           text[index] == TVP_EMOJI_VS16))
            ++index;
        if(isDefaultIgnorableUnicode(ch))
            continue;
        tjs_int ax, ay;
        tTVPRect rt(0, 0, 0, 0);
        bool result = Face->GetGlyphRectFromCharcode(rt, ch, ax, ay);
        if(result == false && !isUnicodeSpace(ch)) {
            ApplyFallbackFaces();
            for(auto *fallback : FaceFallbacks) {
                result = fallback->GetGlyphRectFromCharcode(rt, ch, ax, ay);
                if(result) {
                    rt.add_offsets(
                        0, krkr::font::ComputeFallbackBaselineAdjustment(
                               Face->GetLineBaseline(),
                               fallback->GetLineBaseline()));
                    break;
                }
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
            if(have_area) {
                area.do_union(rt);
            } else {
                area = rt;
                have_area = true;
            }
        }
        offsetx += ax;
        offsety = 0;
    }
}
