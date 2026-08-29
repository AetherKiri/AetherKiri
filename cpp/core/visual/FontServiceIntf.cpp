// Aether implementation of the krkrz FontServiceIntf contract.
//
// The public declaration and the variable-font parser remain in the pinned
// krkrz_dev submodule.  This file is deliberately an adapter over Aether's
// existing font registry and FreeType owner: it does not introduce a second
// font database or a second rasterizer.

#include "tjsCommHead.h"
#include "FontServiceIntf.h"

#include "CharacterSet.h"
#include "BinaryStream.h"
#include "FontImpl.h"
#include "FreeType.h"
#include "FontStream.h"
#include "MsgIntf.h"
#include "StorageIntf.h"
#include "UtilStreams.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <numeric>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <ft2build.h>
#include FT_COLOR_H
#include FT_GLYPH_H
#include FT_MULTIPLE_MASTERS_H
#include FT_OUTLINE_H
#include FT_STROKER_H
#include FT_SFNT_NAMES_H
#include FT_SYNTHESIS_H
#include FT_TRUETYPE_IDS_H
#include FT_TRUETYPE_TABLES_H

#if defined(AETHERKIRI_FONT_SHAPING_ENABLED)
#include <hb.h>
#include <hb-ot.h>
#include <fribidi/fribidi.h>
#endif

using namespace TJS;

// tFreeTypeFace uses this library instance for all of its faces.  It is kept
// as an extern here rather than opening a second FreeType library, which would
// invalidate FT_Done_MM_Var/rasterizer ownership rules.
extern FT_Library FreeTypeLibrary;

namespace {

constexpr std::uint64_t kMaxFontBufferBytes = 128u * 1024u * 1024u;

struct FontBuffer {
    std::shared_ptr<const std::vector<tjs_uint8>> Bytes;
};

struct FontFace {
    std::unique_ptr<tFreeTypeFace> Impl;
    std::shared_ptr<const std::vector<tjs_uint8>> Bytes;
    ttstr Key;
    tjs_int FaceIndex = 0;
    // Keep the normalized coordinates alongside the FreeType face.  HarfBuzz
    // is created from the shared SFNT bytes (rather than from a second FT
    // face), so it needs the same design-space state explicitly reapplied.
    std::vector<tTVPFontVarCoord> Variations;
    std::vector<tjs_uint8> Bitmap;
    std::vector<tjs_uint8> Mask;
};

struct FontFaceChain {
    std::vector<std::unique_ptr<FontFace>> Faces;
};

static FontFace *AsFace(tTVPFontFaceHandle handle) {
    return static_cast<FontFace *>(handle);
}

static FontFaceChain *AsChain(tTVPFontFaceChainHandle handle) {
    return static_cast<FontFaceChain *>(handle);
}

static FT_Face RawFace(FontFace *face) {
    if(!face || !face->Impl || !face->Impl->GetBaseFace())
        return nullptr;
    return face->Impl->GetBaseFace()->GetFTFace();
}

static ttstr FromUtf8(const char *value) {
    if(!value || !*value)
        return ttstr();
    tjs_string converted;
    if(!TVPUtf8ToUtf16(converted, std::string(value)))
        return ttstr();
    return ttstr(reinterpret_cast<const tjs_char *>(converted.c_str()));
}

// Resolve a path lazily.  addFont/loadFont may be called after the initial
// font-table scan, so a direct path is enumerated on first use.  We never use
// TVPResolveFont's default fallback here: an unknown face must fail instead of
// silently opening the application's default font.
static TVPFontNamePathInfo *FindOrRegister(const ttstr &token) {
    if(token.IsEmpty())
        return nullptr;
    if(auto *info = TVPFindFont(token))
        return info;
    try {
        TVPEnumFontsProc(token);
    } catch(...) {
        // The service API is best effort; callers receive nullptr below.
    }
    if(auto *info = TVPFindFont(token))
        return info;

    // Native-path enumeration stores the canonical local path as Path.  Match
    // it as a final step so a `file://` spelling can still acquire the face.
    const std::string requested = token.AsStdString();
    std::vector<ttstr> names;
    TVPGetAllFontList(names);
    for(const auto &name : names) {
        auto *info = TVPFindFont(name);
        if(info && (info->Path.AsStdString() == requested ||
                    info->FamilyName.AsStdString() == requested))
            return info;
    }
    return nullptr;
}

static std::shared_ptr<const std::vector<tjs_uint8>> ReadFontBytesFromStream(
    std::unique_ptr<tTJSBinaryStream> stream) {
    if(!stream)
        return {};
    const tjs_uint64 size = stream->GetSize();
    if(size == 0 || size > kMaxFontBufferBytes ||
       size > static_cast<tjs_uint64>(std::numeric_limits<std::size_t>::max()))
        return {};
    auto bytes = std::make_shared<std::vector<tjs_uint8>>(
        static_cast<std::size_t>(size));
    stream->SetPosition(0);
    std::size_t offset = 0;
    while(offset < bytes->size()) {
        const tjs_uint want = static_cast<tjs_uint>(std::min<std::size_t>(
            bytes->size() - offset,
            static_cast<std::size_t>(std::numeric_limits<tjs_uint>::max())));
        const tjs_uint got = stream->Read(bytes->data() + offset, want);
        if(got == 0)
            break;
        offset += got;
    }
    if(offset != bytes->size())
        return {};
    return std::const_pointer_cast<const std::vector<tjs_uint8>>(bytes);
}

static std::shared_ptr<const std::vector<tjs_uint8>> ReadFontBytes(
    const ttstr &name, tjs_int &faceIndex) {
    if(!FindOrRegister(name))
        return {};
    try {
        return ReadFontBytesFromStream(
            std::unique_ptr<tTJSBinaryStream>(
                TVPCreateFontStream(name, &faceIndex)));
    } catch(...) {
        return {};
    }
}

// Read the bytes for an explicitly selected TTC/OTC face.  The regular
// TVPCreateFontStream(name, &index) API intentionally follows the registry's
// alias (usually face zero), so adapters that accept an index need to retain
// the same storage/getter while overriding only the selected face.
static std::shared_ptr<const std::vector<tjs_uint8>> ReadFontBytesAt(
    const ttstr &name, tjs_int requestedFaceIndex) {
    auto *info = FindOrRegister(name);
    if(!info || requestedFaceIndex < 0)
        return {};

    std::unique_ptr<tTJSBinaryStream> stream;
    try {
        stream.reset(info->Getter
            ? info->Getter(info)
            : TVPCreateBinaryStreamForRead(info->Path, TJS_W("")));
        // FontFace retains the immutable bytes for the lifetime of the face,
        // so the explicit-index path does not need to populate the global
        // stream cache (and therefore cannot alias two TTC faces under one
        // registry key).  The ordinary path still uses FontStream's bounded
        // cache through TVPCreateFontStream.
        return ReadFontBytesFromStream(std::move(stream));
    } catch(...) {
        return {};
    }
}

static FontFace *AcquireFaceImpl(const ttstr &name,
                                 const tTVPFontVarCoord *coords,
                                 tjs_int coordCount,
                                 tjs_int requestedFaceIndex = -1) {
    TVPFontNamePathInfo *info = FindOrRegister(name);
    if(!info)
        return nullptr;

    if(requestedFaceIndex > 0xff)
        return nullptr; // the legacy FreeType option reserves one byte
    const bool explicitFaceIndex = requestedFaceIndex >= 0;
    tjs_int faceIndex = explicitFaceIndex
        ? requestedFaceIndex : std::max<tjs_int>(0, info->Index);
    auto bytes = explicitFaceIndex ? ReadFontBytesAt(name, faceIndex)
                                   : ReadFontBytes(name, faceIndex);
    if(!bytes)
        return nullptr;

    std::unique_ptr<tFreeTypeFace> impl;
    try {
        // tFreeTypeFace owns the stream/FT_Face used by the legacy renderer;
        // FontService owns only a shared immutable byte view for consumers
        // such as minikin.
        const tjs_uint32 faceOptions = explicitFaceIndex
            ? (TVP_FACE_OPTIONS_EXPLICIT_FACE_INDEX |
               TVP_FACE_OPTIONS_FACE_INDEX(faceIndex))
            : 0;
        impl = std::make_unique<tFreeTypeFace>(name, faceOptions);
        if(coords && coordCount > 0) {
            // Use the same clamping/parser path as drawText.  The public
            // service coordinates are already tag/value pairs; serialize the
            // values through the face's direct FreeType setter below instead.
            FT_Face raw = impl->GetBaseFace()->GetFTFace();
            if(raw) {
                FT_MM_Var *mm = nullptr;
                if(FT_Get_MM_Var(raw, &mm) == 0 && mm) {
                    std::vector<FT_Fixed> values(mm->num_axis);
                    for(FT_UInt axis = 0; axis < mm->num_axis; ++axis)
                        values[axis] = mm->axis[axis].def;
                    for(tjs_int coordIndex = 0; coordIndex < coordCount;
                        ++coordIndex) {
                        const auto &coord = coords[coordIndex];
                        for(FT_UInt axis = 0; axis < mm->num_axis; ++axis) {
                            if(mm->axis[axis].tag != coord.Tag)
                                continue;
                            const float minValue = static_cast<float>(
                                mm->axis[axis].minimum) / 65536.0f;
                            const float maxValue = static_cast<float>(
                                mm->axis[axis].maximum) / 65536.0f;
                            const float requested = std::isfinite(coord.Value)
                                ? coord.Value :
                                static_cast<float>(mm->axis[axis].def) /
                                    65536.0f;
                            const float value = std::max(
                                minValue, std::min(maxValue, requested));
                            values[axis] = static_cast<FT_Fixed>(
                                std::llround(static_cast<double>(value) * 65536.0));
                            break;
                        }
                    }
                    FT_Set_Var_Design_Coordinates(raw, mm->num_axis,
                                                  values.data());
                    FT_Done_MM_Var(FreeTypeLibrary, mm);
                }
            }
        }
    } catch(...) {
        return nullptr;
    }

    auto *result = new FontFace();
    result->Impl = std::move(impl);
    result->Bytes = std::move(bytes);
    result->Key = name;
    result->FaceIndex = faceIndex;
    if(coords && coordCount > 0) {
        FT_MM_Var *mm = nullptr;
        if(FT_Get_MM_Var(RawFace(result), &mm) == 0 && mm) {
            result->Variations.reserve(static_cast<std::size_t>(coordCount));
            for(tjs_int i = 0; i < coordCount; ++i) {
                for(FT_UInt axis = 0; axis < mm->num_axis; ++axis) {
                    if(mm->axis[axis].tag != coords[i].Tag)
                        continue;
                    const float minValue = static_cast<float>(
                        mm->axis[axis].minimum) / 65536.0f;
                    const float maxValue = static_cast<float>(
                        mm->axis[axis].maximum) / 65536.0f;
                    const float value = std::isfinite(coords[i].Value)
                        ? std::max(minValue, std::min(maxValue, coords[i].Value))
                        : static_cast<float>(mm->axis[axis].def) / 65536.0f;
                    bool replaced = false;
                    for(auto &existing : result->Variations) {
                        if(existing.Tag == coords[i].Tag) {
                            existing.Value = value;
                            replaced = true;
                            break;
                        }
                    }
                    if(!replaced)
                        result->Variations.push_back(
                            tTVPFontVarCoord{coords[i].Tag, value});
                    break;
                }
            }
            FT_Done_MM_Var(FreeTypeLibrary, mm);
        }
    }
    return result;
}

static bool SetPixelSize(FontFace *face, tjs_int pixelSize) {
    if(!face || !face->Impl || pixelSize <= 0)
        return false;
    face->Impl->SetHeight(pixelSize);
    return RawFace(face) != nullptr;
}

static bool LoadGlyph(FontFace *face, tjs_uint32 glyphId, tjs_int pixelSize,
                      bool color, bool bold, bool italic, tjs_int mode) {
    FT_Face raw = RawFace(face);
    if(!raw || glyphId >= raw->num_glyphs)
        return false;
    if(mode != TVP_FONT_METRICS_UNSCALED && !SetPixelSize(face, pixelSize))
        return false;

    FT_Int32 flags = FT_LOAD_DEFAULT;
    if(mode == TVP_FONT_METRICS_UNSCALED)
        flags = FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING;
    else if(mode == TVP_FONT_METRICS_UNHINTED)
        flags |= FT_LOAD_NO_HINTING | FT_LOAD_NO_AUTOHINT;
    if(color)
        flags |= FT_LOAD_COLOR;
    else if(FT_IS_SCALABLE(raw))
        flags |= FT_LOAD_NO_BITMAP;
    if(FT_Load_Glyph(raw, static_cast<FT_UInt>(glyphId), flags) != 0)
        return false;
    if(bold && raw->glyph->format == FT_GLYPH_FORMAT_OUTLINE)
        FT_GlyphSlot_Embolden(raw->glyph);
    if(italic && raw->glyph->format == FT_GLYPH_FORMAT_OUTLINE)
        FT_GlyphSlot_Oblique(raw->glyph);
    return true;
}

static float MetricValue(FT_Pos value, bool unscaled) {
    return unscaled ? static_cast<float>(value)
                    : static_cast<float>(value) / 64.0f;
}

static bool FillGlyphMetrics(FontFace *face, tjs_uint32 glyphId,
                             tjs_int pixelSize, bool bold, bool italic,
                             tjs_int mode, tTVPFontGlyphMetrics *out) {
    if(!out || (mode != TVP_FONT_METRICS_HINTED &&
                mode != TVP_FONT_METRICS_UNHINTED &&
                mode != TVP_FONT_METRICS_UNSCALED))
        return false;
    if(!LoadGlyph(face, glyphId, pixelSize, false, bold, italic, mode))
        return false;
    FT_Face raw = RawFace(face);
    const bool unscaled = mode == TVP_FONT_METRICS_UNSCALED;
    const FT_Glyph_Metrics &m = raw->glyph->metrics;
    out->AdvanceX = MetricValue(m.horiAdvance, unscaled);
    out->AdvanceY = MetricValue(m.vertAdvance, unscaled);
    out->BearingX = MetricValue(m.horiBearingX, unscaled);
    out->BearingY = MetricValue(m.horiBearingY, unscaled);
    out->Width = MetricValue(m.width, unscaled);
    out->Height = MetricValue(m.height, unscaled);
    return true;
}

static bool CopyBitmap(FontFace *face, const FT_Bitmap &source,
                       tTVPFontGlyphBitmap *out) {
    if(!out || source.width == 0 || source.rows == 0 || !source.buffer)
        return false;
    const std::size_t width = source.width;
    const std::size_t rows = source.rows;
    const bool color = source.pixel_mode == FT_PIXEL_MODE_BGRA;
    const std::size_t bytesPerPixel = color ? 4u : 1u;
    if(width > std::numeric_limits<std::size_t>::max() / bytesPerPixel ||
       rows > std::numeric_limits<std::size_t>::max() /
                   (width * bytesPerPixel))
        return false;
    const std::size_t rowBytes = width * bytesPerPixel;
    const std::size_t pitch = rowBytes;
    const long long sourcePitch = static_cast<long long>(source.pitch);
    const unsigned long long absPitch = sourcePitch < 0
        ? static_cast<unsigned long long>(-sourcePitch)
        : static_cast<unsigned long long>(sourcePitch);
    if(absPitch < rowBytes)
        return false;

    face->Bitmap.assign(rows * rowBytes, 0);
    if(color) {
        for(std::size_t y = 0; y < rows; ++y) {
            const auto *src = source.buffer +
                static_cast<std::ptrdiff_t>(y) * source.pitch;
            std::memcpy(face->Bitmap.data() + y * pitch, src, rowBytes);
        }
        out->Format = TVP_FONT_BITMAP_BGRA;
    } else if(source.pixel_mode == FT_PIXEL_MODE_GRAY && source.num_grays <= 256) {
        for(std::size_t y = 0; y < rows; ++y) {
            const auto *src = source.buffer +
                static_cast<std::ptrdiff_t>(y) * source.pitch;
            auto *dst = face->Bitmap.data() + y * pitch;
            for(std::size_t x = 0; x < width; ++x) {
                const unsigned int value = src[x];
                dst[x] = source.num_grays == 0 || source.num_grays == 256
                    ? static_cast<tjs_uint8>(value)
                    : source.num_grays <= 1
                    ? (value ? 255 : 0)
                    : static_cast<tjs_uint8>(std::min<unsigned int>(
                          255u, value * 255u /
                              (static_cast<unsigned int>(source.num_grays) - 1u)));
            }
        }
        out->Format = TVP_FONT_BITMAP_GRAY;
    } else if(source.pixel_mode == FT_PIXEL_MODE_MONO) {
        const std::size_t sourceRowBytes = (width + 7u) / 8u;
        if(static_cast<unsigned long long>(source.pitch < 0 ? -source.pitch
                                                             : source.pitch) <
           sourceRowBytes)
            return false;
        for(std::size_t y = 0; y < rows; ++y) {
            const auto *src = source.buffer +
                static_cast<std::ptrdiff_t>(y) * source.pitch;
            auto *dst = face->Bitmap.data() + y * pitch;
            for(std::size_t x = 0; x < width; ++x)
                dst[x] = (src[x >> 3] & (0x80u >> (x & 7u))) ? 255 : 0;
        }
        out->Format = TVP_FONT_BITMAP_GRAY;
    } else {
        face->Bitmap.clear();
        return false;
    }

    out->Left = RawFace(face)->glyph->bitmap_left;
    out->Top = RawFace(face)->glyph->bitmap_top;
    out->Width = static_cast<tjs_int>(width);
    out->Height = static_cast<tjs_int>(rows);
    out->Pitch = static_cast<tjs_int>(pitch);
    out->Buffer = face->Bitmap.data();
    return true;
}

struct OutlineContext {
    iTVPFontOutlineSink *Sink = nullptr;
    bool HaveContour = false;
    bool Failed = false;
};

static float OutlineCoord(FT_Pos value) {
    return static_cast<float>(value) / 64.0f;
}

static int OutlineMove(const FT_Vector *to, void *opaque) {
    auto *ctx = static_cast<OutlineContext *>(opaque);
    if(ctx->Failed)
        return 1;
    try {
        if(ctx->HaveContour)
            ctx->Sink->ClosePath();
        ctx->Sink->MoveTo(OutlineCoord(to->x), OutlineCoord(to->y));
        ctx->HaveContour = true;
    } catch(...) {
        ctx->Failed = true;
        return 1;
    }
    return 0;
}

static int OutlineLine(const FT_Vector *to, void *opaque) {
    auto *ctx = static_cast<OutlineContext *>(opaque);
    if(ctx->Failed)
        return 1;
    try {
        ctx->Sink->LineTo(OutlineCoord(to->x), OutlineCoord(to->y));
    } catch(...) {
        ctx->Failed = true;
        return 1;
    }
    return 0;
}

static int OutlineConic(const FT_Vector *control, const FT_Vector *to,
                        void *opaque) {
    auto *ctx = static_cast<OutlineContext *>(opaque);
    if(ctx->Failed)
        return 1;
    try {
        ctx->Sink->QuadTo(OutlineCoord(control->x), OutlineCoord(control->y),
                          OutlineCoord(to->x), OutlineCoord(to->y));
    } catch(...) {
        ctx->Failed = true;
        return 1;
    }
    return 0;
}

static int OutlineCubic(const FT_Vector *c1, const FT_Vector *c2,
                        const FT_Vector *to, void *opaque) {
    auto *ctx = static_cast<OutlineContext *>(opaque);
    if(ctx->Failed)
        return 1;
    try {
        ctx->Sink->CubicTo(OutlineCoord(c1->x), OutlineCoord(c1->y),
                           OutlineCoord(c2->x), OutlineCoord(c2->y),
                           OutlineCoord(to->x), OutlineCoord(to->y));
    } catch(...) {
        ctx->Failed = true;
        return 1;
    }
    return 0;
}

static bool IsValidScalar(tjs_uint32 codepoint) {
    return codepoint <= 0x10ffffu &&
           !(codepoint >= 0xd800u && codepoint <= 0xdfffu);
}

static bool ContainsAll(const ttstr &text, FontFace *face) {
    if(text.IsEmpty())
        return true;
    const tjs_size length = text.length();
    tjs_size index = 0;
    while(index < length) {
        tjs_uint32 codepoint = 0;
        tjs_size consumed = 0;
        if(!TVPReadUtf16CodePoint(text.c_str() + index, length - index,
                                   codepoint, consumed) || consumed == 0)
            return false;
        index += consumed;
        if(IsValidScalar(codepoint) && TVPFontGetGlyphIndex(face, codepoint) == 0)
            return false;
    }
    return true;
}

static ttstr FaceString(const char *value) {
    return FromUtf8(value);
}

static ttstr SfntNameString(FT_Face face, FT_UShort nameId) {
    if(!face || !FT_IS_SFNT(face))
        return ttstr();
    const FT_UInt count = FT_Get_Sfnt_Name_Count(face);
    ttstr fallback;
    for(FT_UInt i = 0; i < count; ++i) {
        FT_SfntName name{};
        if(FT_Get_Sfnt_Name(face, i, &name) != 0 || name.name_id != nameId ||
           !name.string || name.string_len == 0)
            continue;
        ttstr value;
        // Unicode-platform and Windows name records are UTF-16BE.  Decode
        // them explicitly instead of treating the bytes as locale text;
        // this preserves Japanese/Chinese full names used by font pickers.
        if((name.platform_id == TT_PLATFORM_MICROSOFT ||
            name.platform_id == TT_PLATFORM_APPLE_UNICODE) &&
           (name.string_len % 2u) == 0) {
            tjs_string wide;
            wide.reserve(name.string_len / 2u);
            for(FT_UInt offset = 0; offset < name.string_len; offset += 2u)
                wide.push_back(static_cast<tjs_char>(
                    (static_cast<tjs_uint16>(name.string[offset]) << 8) |
                    static_cast<tjs_uint16>(name.string[offset + 1])));
            value = ttstr(reinterpret_cast<const tjs_char *>(wide.c_str()));
        } else {
            std::string bytes(reinterpret_cast<const char *>(name.string),
                              name.string_len);
            tjs_string wide;
            if(TVPUtf8ToUtf16(wide, bytes))
                value = ttstr(reinterpret_cast<const tjs_char *>(wide.c_str()));
            else
                value = ttstr(bytes);
        }
        if(value.IsEmpty())
            continue;
        // Prefer English (0x0409) but retain the first localized record as a
        // deterministic fallback when a font has no English name.
        if(name.language_id == 0x0409 || name.language_id == 0) {
            return value;
        }
        if(fallback.IsEmpty())
            fallback = value;
    }
    return fallback;
}

static bool FaceSupportsScript(FontFace *face, const ttstr &script) {
    if(!face || script.IsEmpty())
        return true;
    std::string tag = script.AsStdString();
    if(tag.size() > 4)
        tag.resize(4);
    for(char &ch : tag)
        if(ch >= 'a' && ch <= 'z')
            ch = static_cast<char>(ch - 'a' + 'A');
    // Representative code points keep this query useful without maintaining
    // a second Unicode-range database.  A face that contains any sample is a
    // candidate; callers can combine this with ContainsText for strict
    // coverage checks.
    static const std::set<std::string> knownTags{
        "LATN", "CYRL", "GREK", "ARAB", "HEBR", "DEVA", "THAI",
        "HANI", "HANS", "HANT", "JPAN", "KORE", "BENG", "TAML",
        "TELU", "MTHK", "GEOR", "ARMN", "ETHI", "KHMR", "MYMR"};
    if(knownTags.find(tag) == knownTags.end())
        return true;
    const std::vector<tjs_uint32> samples =
        tag == "LATN" ? std::vector<tjs_uint32>{0x0041, 0x0061, 0x00E9} :
        tag == "CYRL" ? std::vector<tjs_uint32>{0x0410, 0x0430} :
        tag == "GREK" ? std::vector<tjs_uint32>{0x0391, 0x03B1} :
        tag == "ARAB" ? std::vector<tjs_uint32>{0x0627, 0x0645} :
        tag == "HEBR" ? std::vector<tjs_uint32>{0x05D0, 0x05D1} :
        tag == "DEVA" ? std::vector<tjs_uint32>{0x0915, 0x093E} :
        tag == "THAI" ? std::vector<tjs_uint32>{0x0E01, 0x0E32} :
        tag == "HANI" || tag == "HANS" || tag == "HANT" || tag == "JPAN"
            ? std::vector<tjs_uint32>{0x4E00, 0x65E5} :
        tag == "KORE" ? std::vector<tjs_uint32>{0xAC00, 0xB098} :
        tag == "BENG" ? std::vector<tjs_uint32>{0x0985, 0x0995} :
        tag == "TAML" ? std::vector<tjs_uint32>{0x0B85, 0x0B95} :
        tag == "TELU" ? std::vector<tjs_uint32>{0x0C05, 0x0C15} :
        tag == "GEOR" ? std::vector<tjs_uint32>{0x10D0, 0x10D1} :
        tag == "ARMN" ? std::vector<tjs_uint32>{0x0531, 0x0532} :
        tag == "ETHI" ? std::vector<tjs_uint32>{0x1200, 0x1201} :
        tag == "KHMR" ? std::vector<tjs_uint32>{0x1780, 0x1781} :
        tag == "MYMR" ? std::vector<tjs_uint32>{0x1000, 0x1001} :
        std::vector<tjs_uint32>{};
    for(const auto codepoint : samples)
        if(TVPFontGetGlyphIndex(face, codepoint) != 0)
            return true;
    return false;
}

static void FillFaceInfo(FontFace *face, const ttstr &key,
                         tTVPFontFaceInfo *out) {
    if(!out)
        return;
    *out = tTVPFontFaceInfo{};
    FT_Face raw = RawFace(face);
    out->Key = key;
    out->FaceIndex = face ? face->FaceIndex : 0;
    if(!raw)
        return;
    out->Family = FaceString(raw->family_name);
    out->Subfamily = FaceString(raw->style_name);
    out->FullName = SfntNameString(raw, TT_NAME_ID_FULL_NAME);
    if(out->FullName.IsEmpty())
        out->FullName = out->Family;
    out->PostScriptName = FaceString(FT_Get_Postscript_Name(raw));
    out->Weight = (raw->style_flags & FT_STYLE_FLAG_BOLD) ? 700 : 400;
    if(auto *os2 = static_cast<TT_OS2 *>(FT_Get_Sfnt_Table(raw, FT_SFNT_OS2)))
        out->Weight = std::max<tjs_int>(1, std::min<tjs_int>(1000,
                                                              os2->usWeightClass));
    out->Slant = (raw->style_flags & FT_STYLE_FLAG_ITALIC) ? 1 : 0;
    if(out->Slant == 0 && raw->style_name) {
        std::string style(raw->style_name);
        std::transform(style.begin(), style.end(), style.begin(),
                       [](unsigned char ch) {
                           return static_cast<char>(std::tolower(ch));
                       });
        if(style.find("oblique") != std::string::npos)
            out->Slant = 2;
    }
    out->Bold = (raw->style_flags & FT_STYLE_FLAG_BOLD) != 0;
    out->Color = FT_HAS_COLOR(raw) != 0;
    out->Monospace = (raw->face_flags & FT_FACE_FLAG_FIXED_WIDTH) != 0;
    out->Scalable = FT_IS_SCALABLE(raw) != 0;
}

static std::string FaceIdentity(FontFace *face) {
    if(!face)
        return {};
    if(auto *info = TVPFindFont(face->Key))
        return info->Path.AsStdString() + "#" +
               std::to_string(std::max(0, info->Index));
    return face->Key.AsStdString() + "#" + std::to_string(face->FaceIndex);
}

static bool Utf16CodepointAt(const ttstr &text, tjs_size &index,
                             tjs_uint32 &codepoint, tjs_size &consumed) {
    const tjs_size length = static_cast<tjs_size>(text.length());
    if(index >= length)
        return false;
    return TVPReadUtf16CodePoint(text.c_str() + index,
                                 length - index, codepoint, consumed);
}

static tjs_uint32 Utf8LengthForCodepoint(tjs_uint32 codepoint) {
    if(codepoint <= 0x7fu)
        return 1;
    if(codepoint <= 0x7ffu)
        return 2;
    if(codepoint <= 0xffffu)
        return 3;
    return 4;
}

struct ShapeCodepoint {
    tjs_uint32 Codepoint = 0;
    std::size_t Utf16Start = 0;
    std::size_t Utf16Length = 0;
    std::size_t Utf8Start = 0;
    std::size_t Utf8Length = 0;
};

static bool AppendUtf8Codepoint(tjs_uint32 codepoint, std::string &out) {
    if(!IsValidScalar(codepoint))
        codepoint = 0xfffdu;
    if(codepoint <= 0x7fu) {
        out.push_back(static_cast<char>(codepoint));
    } else if(codepoint <= 0x7ffu) {
        out.push_back(static_cast<char>(0xc0u | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
    } else if(codepoint <= 0xffffu) {
        out.push_back(static_cast<char>(0xe0u | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3fu)));
        out.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
    } else if(codepoint <= 0x10ffffu) {
        out.push_back(static_cast<char>(0xf0u | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80u | ((codepoint >> 12) & 0x3fu)));
        out.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3fu)));
        out.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
    } else {
        return false;
    }
    return true;
}

static bool BuildShapeCodepoints(const ttstr &text,
                                 std::vector<ShapeCodepoint> &records,
                                 std::string &utf8) {
    records.clear();
    utf8.clear();
    const std::size_t textLength = static_cast<std::size_t>(text.length());
    records.reserve(textLength);
    utf8.reserve(textLength);
    std::size_t index = 0;
    while(index < textLength) {
        tjs_uint32 codepoint = 0;
        tjs_size consumed = 0;
        if(!TVPReadUtf16CodePoint(text.c_str() + index,
                                  textLength - index, codepoint,
                                  consumed) || consumed == 0)
            return false;
        const std::size_t utf8Start = utf8.size();
        if(!AppendUtf8Codepoint(codepoint, utf8))
            return false;
        records.push_back(ShapeCodepoint{
            IsValidScalar(codepoint) ? codepoint : 0xfffdu,
            index,
            consumed,
            utf8Start,
            utf8.size() - utf8Start});
        index += consumed;
    }
    return true;
}

static bool IsVariationSelector(tjs_uint32 codepoint) {
    return (codepoint >= 0xfe00u && codepoint <= 0xfe0fu) ||
           (codepoint >= 0xe0100u && codepoint <= 0xe01efu);
}

static bool IsJoinControl(tjs_uint32 codepoint) {
    return codepoint == 0x200cu || codepoint == 0x200du;
}

static bool IsCombiningMark(tjs_uint32 codepoint) {
    return (codepoint >= 0x0300u && codepoint <= 0x036fu) ||
           (codepoint >= 0x1ab0u && codepoint <= 0x1affu) ||
           (codepoint >= 0x1dc0u && codepoint <= 0x1dffu) ||
           (codepoint >= 0x20d0u && codepoint <= 0x20ffu) ||
           (codepoint >= 0xfe20u && codepoint <= 0xfe2fu) ||
           (codepoint >= 0x0591u && codepoint <= 0x05bdu) ||
           (codepoint >= 0x0610u && codepoint <= 0x061au) ||
           (codepoint >= 0x064bu && codepoint <= 0x065fu);
}

#if defined(AETHERKIRI_FONT_SHAPING_ENABLED)

struct BidiRunRange {
    std::size_t Start = 0;
    std::size_t End = 0;
    FriBidiLevel Level = 0;
};

static bool ResolveBidiRuns(const std::vector<ShapeCodepoint> &records,
                            tjs_int baseDirection,
                            std::vector<BidiRunRange> &visualRuns) {
    visualRuns.clear();
    if(records.empty())
        return true;
    if(records.size() > static_cast<std::size_t>(
           std::numeric_limits<FriBidiStrIndex>::max()))
        return false;
    std::vector<FriBidiChar> chars;
    chars.reserve(records.size());
    for(const auto &record : records)
        chars.push_back(static_cast<FriBidiChar>(record.Codepoint));
    std::vector<FriBidiCharType> types(records.size());
    std::vector<FriBidiBracketType> brackets(records.size());
    std::vector<FriBidiLevel> levels(records.size());
    fribidi_get_bidi_types(chars.data(), static_cast<FriBidiStrIndex>(chars.size()),
                           types.data());
    fribidi_get_bracket_types(chars.data(),
                              static_cast<FriBidiStrIndex>(chars.size()),
                              types.data(), brackets.data());
    FriBidiParType paragraph = baseDirection == TVP_FONT_BASEDIR_RTL
        ? FRIBIDI_PAR_RTL
        : baseDirection == TVP_FONT_BASEDIR_LTR ? FRIBIDI_PAR_LTR
                                                 : FRIBIDI_PAR_ON;
    const FriBidiLevel maxLevel = fribidi_get_par_embedding_levels_ex(
        types.data(), brackets.data(), static_cast<FriBidiStrIndex>(chars.size()),
        &paragraph, levels.data());
    if(maxLevel == 0)
        return false;

    for(std::size_t i = 0; i < levels.size();) {
        const std::size_t start = i;
        const FriBidiLevel level = levels[i];
        while(i < levels.size() && levels[i] == level)
            ++i;
        visualRuns.push_back(BidiRunRange{start, i, level});
    }

    // UBA rule L2: reverse contiguous sequences at each descending level.
    // Runs are kept in logical order while shaping; the reordered index list
    // is the visual left-to-right order consumed by the Aether sink.
    std::vector<std::size_t> order(visualRuns.size());
    std::iota(order.begin(), order.end(), 0);
    const FriBidiLevel lowest = paragraph & FRIBIDI_MASK_RTL ? 1 : 0;
    for(int level = static_cast<int>(maxLevel) - 1; level > lowest; --level) {
        std::size_t i = 0;
        while(i < order.size()) {
            if(static_cast<int>(visualRuns[order[i]].Level) < level) {
                ++i;
                continue;
            }
            const std::size_t start = i;
            while(i < order.size() &&
                  static_cast<int>(visualRuns[order[i]].Level) >= level)
                ++i;
            std::reverse(order.begin() + static_cast<std::ptrdiff_t>(start),
                         order.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }
    std::vector<BidiRunRange> reordered;
    reordered.reserve(order.size());
    for(const auto index : order)
        reordered.push_back(visualRuns[index]);
    visualRuns.swap(reordered);
    return true;
}

static hb_font_t *CreateHarfBuzzFont(FontFace *face, tjs_int pixelSize) {
    if(!face || !face->Bytes || face->Bytes->empty() || pixelSize <= 0 ||
       face->Bytes->size() > std::numeric_limits<unsigned int>::max())
        return nullptr;
    auto *raw = RawFace(face);
    if(!raw)
        return nullptr;
    auto *blob = hb_blob_create(
        reinterpret_cast<const char *>(face->Bytes->data()),
        static_cast<unsigned int>(face->Bytes->size()), HB_MEMORY_MODE_READONLY,
        nullptr, nullptr);
    if(!blob)
        return nullptr;
    auto *hbFace = hb_face_create(blob,
                                  static_cast<unsigned int>(face->FaceIndex));
    hb_blob_destroy(blob);
    if(!hbFace)
        return nullptr;
    auto *font = hb_font_create(hbFace);
    hb_face_destroy(hbFace);
    if(!font)
        return nullptr;
    hb_ot_font_set_funcs(font);
    const unsigned int scale = static_cast<unsigned int>(std::min<tjs_int>(
        pixelSize, std::numeric_limits<int>::max() / 64)) * 64u;
    hb_font_set_scale(font, static_cast<int>(scale), static_cast<int>(scale));
    hb_font_set_ppem(font, static_cast<unsigned int>(pixelSize),
                     static_cast<unsigned int>(pixelSize));
    if(!face->Variations.empty()) {
        std::vector<hb_variation_t> variations;
        variations.reserve(face->Variations.size());
        for(const auto &coord : face->Variations)
            variations.push_back(hb_variation_t{
                static_cast<hb_tag_t>(coord.Tag), coord.Value});
        hb_font_set_variations(font, variations.data(),
                               static_cast<unsigned int>(variations.size()));
    }
    (void)raw;
    return font;
}

struct ShapedGlyphValue {
    tjs_uint32 GlyphId = 0;
    tjs_int FaceIndex = -1;
    tjs_uint32 Cluster = 0;
    float Advance = 0.0f;
    float XOffset = 0.0f;
    float YOffset = 0.0f;
    bool RTL = false;
};

static float FixedStrikeScale(FontFace *face, tjs_int pixelSize) {
    const FT_Face raw = RawFace(face);
    if(!raw || FT_IS_SCALABLE(raw) || !raw->size || pixelSize <= 0)
        return 1.0f;
    const float strike = raw->size->metrics.y_ppem / 64.0f;
    return strike > 0.0f ? static_cast<float>(pixelSize) / strike : 1.0f;
}

static bool ShapeSpanWithHarfBuzz(FontFace *face,
                                  const std::vector<ShapeCodepoint> &records,
                                  std::size_t start, std::size_t end,
                                  tjs_int faceIndex, tjs_int pixelSize,
                                  bool rtl,
                                  std::vector<ShapedGlyphValue> &out) {
    if(!face || start >= end)
        return false;
    if(!SetPixelSize(face, pixelSize))
        return false;
    hb_font_t *font = CreateHarfBuzzFont(face, pixelSize);
    if(!font)
        return false;
    hb_buffer_t *buffer = hb_buffer_create();
    if(!buffer) {
        hb_font_destroy(font);
        return false;
    }
    // hb_buffer_add() appends raw Unicode code points and, unlike the
    // add_utf8/add_utf16 helpers, does not infer the buffer content type.
    // Mark the buffer before appending so HarfBuzz's segment-property and
    // shaping assertions see a Unicode buffer.  Leaving the type INVALID
    // works in release builds but aborts debug/test hosts in
    // hb_buffer_guess_segment_properties().
    hb_buffer_set_content_type(buffer, HB_BUFFER_CONTENT_TYPE_UNICODE);
    for(std::size_t i = start; i < end; ++i)
        hb_buffer_add(buffer, static_cast<hb_codepoint_t>(records[i].Codepoint),
                      static_cast<unsigned int>(std::min<std::size_t>(
                          records[i].Utf8Start,
                          std::numeric_limits<unsigned int>::max())));
    hb_buffer_set_direction(buffer, rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
    hb_buffer_guess_segment_properties(buffer);
    // Guessing fills script/language while the direction above remains the
    // resolved UBA direction on current HarfBuzz releases.
    hb_buffer_set_direction(buffer, rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
    hb_shape(font, buffer, nullptr, 0);
    unsigned int glyphCount = 0;
    const hb_glyph_info_t *infos = hb_buffer_get_glyph_infos(buffer, &glyphCount);
    const hb_glyph_position_t *positions =
        hb_buffer_get_glyph_positions(buffer, &glyphCount);
    if(!infos || !positions) {
        hb_buffer_destroy(buffer);
        hb_font_destroy(font);
        return false;
    }
    const float strikeScale = FixedStrikeScale(face, pixelSize);
    out.reserve(out.size() + glyphCount);
    for(unsigned int i = 0; i < glyphCount; ++i) {
        const float advance = static_cast<float>(positions[i].x_advance) /
                              64.0f * strikeScale;
        const float xOffset = static_cast<float>(positions[i].x_offset) /
                              64.0f * strikeScale;
        const float yOffset = static_cast<float>(positions[i].y_offset) /
                              64.0f * strikeScale;
        ShapedGlyphValue value;
        value.GlyphId = infos[i].codepoint;
        value.FaceIndex = faceIndex;
        value.Cluster = infos[i].cluster;
        value.Advance = std::isfinite(advance) ? std::max(0.0f, advance) : 0.0f;
        value.XOffset = std::isfinite(xOffset) ? xOffset : 0.0f;
        value.YOffset = std::isfinite(yOffset) ? yOffset : 0.0f;
        value.RTL = rtl;
        out.emplace_back(value);
    }
    hb_buffer_destroy(buffer);
    hb_font_destroy(font);
    return true;
}

static bool ShapeLineHarfBuzz(FontFaceChain *chain,
                              const std::vector<ShapeCodepoint> &records,
                              tjs_int pixelSize, tjs_int baseDirection,
                              std::vector<ShapedGlyphValue> &out) {
    if(!chain || chain->Faces.empty() || records.empty())
        return false;
    std::vector<BidiRunRange> runs;
    if(!ResolveBidiRuns(records, baseDirection, runs))
        return false;
    out.clear();
    for(const auto &run : runs) {
        const bool rtl = (run.Level & 1u) != 0;
        struct Span {
            std::size_t Start;
            std::size_t End;
            tjs_int FaceIndex;
        };
        std::vector<Span> spans;
        tjs_int previousFace = -1;
        for(std::size_t i = run.Start; i < run.End; ++i) {
            const tjs_uint32 cp = records[i].Codepoint;
            tjs_int faceIndex = -1;
            if((IsVariationSelector(cp) || IsJoinControl(cp) ||
                IsCombiningMark(cp) || TVPIsUnicodeDefaultIgnorable(cp)) &&
               previousFace >= 0) {
                faceIndex = previousFace;
            } else {
                faceIndex = TVPFontChainFaceForChar(
                    static_cast<tTVPFontFaceChainHandle>(chain), cp, false);
                if(faceIndex < 0)
                    faceIndex = 0; // upstream glyphware's .notdef policy
            }
            if(!spans.empty() && spans.back().FaceIndex == faceIndex)
                spans.back().End = i + 1;
            else
                spans.push_back(Span{i, i + 1, faceIndex});
            previousFace = faceIndex;
        }
        if(rtl)
            std::reverse(spans.begin(), spans.end());
        for(const auto &span : spans) {
            if(span.FaceIndex < 0 ||
               span.FaceIndex >= static_cast<tjs_int>(chain->Faces.size()))
                continue;
            auto *face = chain->Faces[static_cast<std::size_t>(span.FaceIndex)].get();
            const std::size_t before = out.size();
            bool ok = false;
            if(RawFace(face) && FT_IS_SCALABLE(RawFace(face)))
                ok = ShapeSpanWithHarfBuzz(face, records, span.Start, span.End,
                                           span.FaceIndex, pixelSize, rtl, out);
            if(!ok) {
                out.resize(before);
                // Bitmap-only faces do not expose OpenType design units to
                // HarfBuzz.  Preserve them through the exact Aether metric /
                // glyph path instead of dropping an emoji span.
                for(std::size_t i = span.Start; i < span.End; ++i) {
                    const tjs_uint32 gid = TVPFontGetGlyphIndex(face,
                                                                  records[i].Codepoint);
                    tTVPFontGlyphMetrics metrics{};
                    if(gid == 0 || !TVPFontGetGlyphMetricsEx(
                            face, gid, pixelSize, false, false,
                            TVP_FONT_METRICS_UNHINTED, &metrics))
                        continue;
                    out.push_back(ShapedGlyphValue{
                        gid, span.FaceIndex,
                        static_cast<tjs_uint32>(std::min<std::size_t>(
                            records[i].Utf8Start,
                            std::numeric_limits<tjs_uint32>::max())),
                        std::max(0.0f, metrics.AdvanceX), 0.0f, 0.0f, rtl});
                }
                if(rtl && span.End - span.Start > 1)
                    std::reverse(out.begin() + static_cast<std::ptrdiff_t>(before),
                                 out.end());
            }
        }
    }
    return !out.empty() || records.empty();
}

#endif // AETHERKIRI_FONT_SHAPING_ENABLED

#if defined(FREETYPE_MAJOR) && (FREETYPE_MAJOR > 2 || \
                                 (FREETYPE_MAJOR == 2 && FREETYPE_MINOR >= 13))

struct ColorMatrix {
    float A = 1.0f;
    float B = 0.0f;
    float C = 0.0f;
    float D = 0.0f;
    float E = 1.0f;
    float F = 0.0f;
};

static ColorMatrix MultiplyColorMatrix(const ColorMatrix &lhs,
                                       const ColorMatrix &rhs) {
    return ColorMatrix{
        lhs.A * rhs.A + lhs.B * rhs.D,
        lhs.A * rhs.B + lhs.B * rhs.E,
        lhs.D * rhs.A + lhs.E * rhs.D,
        lhs.D * rhs.B + lhs.E * rhs.E,
        lhs.A * rhs.C + lhs.B * rhs.F + lhs.C,
        lhs.D * rhs.C + lhs.E * rhs.F + lhs.F};
}

static std::array<float, 2> ApplyColorMatrix(const ColorMatrix &matrix,
                                             float x, float y) {
    return {matrix.A * x + matrix.B * y + matrix.C,
            matrix.D * x + matrix.E * y + matrix.F};
}

static ColorMatrix ColorMatrixFromAffine(const FT_Affine23 &affine) {
    return ColorMatrix{
        static_cast<float>(affine.xx) / 65536.0f,
        static_cast<float>(affine.xy) / 65536.0f,
        static_cast<float>(affine.dx) / 65536.0f,
        static_cast<float>(affine.yx) / 65536.0f,
        static_cast<float>(affine.yy) / 65536.0f,
        static_cast<float>(affine.dy) / 65536.0f};
}

static ColorMatrix TranslationMatrix(float x, float y) {
    ColorMatrix result;
    result.C = x;
    result.F = y;
    return result;
}

static ColorMatrix ScaleAroundMatrix(float sx, float sy, float cx, float cy) {
    return MultiplyColorMatrix(
        TranslationMatrix(cx, cy),
        MultiplyColorMatrix(ColorMatrix{sx, 0.0f, 0.0f, 0.0f, sy, 0.0f},
                            TranslationMatrix(-cx, -cy)));
}

static ColorMatrix RotateAroundMatrix(float radians, float cx, float cy) {
    const float sine = std::sin(radians);
    const float cosine = std::cos(radians);
    return MultiplyColorMatrix(
        TranslationMatrix(cx, cy),
        MultiplyColorMatrix(ColorMatrix{cosine, -sine, 0.0f, sine, cosine,
                                        0.0f},
                            TranslationMatrix(-cx, -cy)));
}

static bool ColorFromIndex(const FT_Color *palette, FT_UInt paletteCount,
                           FT_ColorIndex index, FT_Color &out) {
    if(!palette || index.palette_index >= paletteCount)
        return false;
    out = palette[index.palette_index];
    const float alpha = std::max(0.0f, std::min(1.0f,
        static_cast<float>(index.alpha) / 16384.0f));
    out.alpha = static_cast<FT_Byte>(std::llround(
        static_cast<float>(out.alpha) * alpha));
    return true;
}

struct ColorWalkContext {
    FT_Face Face = nullptr;
    FT_Color *Palette = nullptr;
    FT_UInt PaletteCount = 0;
    iTVPFontColorLayerSink *Sink = nullptr;
    tjs_int Count = 0;
    int Depth = 0;
    bool SinkFailed = false;
    std::set<FT_UInt> GlyphStack;
};

// FreeType's color-stop iterator needs the owning face as its first argument;
// this wrapper keeps the call site readable and bounds callback memory.
static bool ReadColorStops(FT_Face face, FT_ColorLine colorLine,
                           const FT_Color *palette, FT_UInt paletteCount,
                           std::vector<tTVPFontColorStop> &stops) {
    stops.clear();
    constexpr FT_UInt kMaxStops = 256;
    FT_ColorStop stop{};
    while(stops.size() < kMaxStops &&
          FT_Get_Colorline_Stops(face, &stop,
                                 &colorLine.color_stop_iterator)) {
        FT_Color color{};
        if(!ColorFromIndex(palette, paletteCount, stop.color, color))
            continue;
        tTVPFontColorStop converted{};
        converted.Offset = std::max(0.0f, std::min(1.0f,
            static_cast<float>(stop.stop_offset) / 65536.0f));
        converted.R = color.red;
        converted.G = color.green;
        converted.B = color.blue;
        converted.A = color.alpha;
        stops.push_back(converted);
    }
    return !stops.empty();
}

static bool WalkColorPaint(ColorWalkContext &context, FT_OpaquePaint opaque,
                           const ColorMatrix &matrix, FT_UInt glyphId,
                           bool haveGlyph);

static bool EmitColorPaint(ColorWalkContext &context, tjs_int paintKind,
                           FT_Color color, const ColorMatrix &matrix,
                           FT_UInt glyphId, const std::vector<tTVPFontColorStop> &stops,
                           float x0, float y0, float x1, float y1,
                           float r0, float r1) {
    if(!context.Sink || !context.Face ||
       (!stops.empty() && glyphId == 0))
        return false;
    tTVPFontColorLayer layer{};
    layer.GlyphId = glyphId;
    layer.Transform[0] = matrix.A;
    layer.Transform[1] = matrix.B;
    layer.Transform[2] = matrix.C;
    layer.Transform[3] = matrix.D;
    layer.Transform[4] = matrix.E;
    layer.Transform[5] = matrix.F;
    layer.PaintKind = paintKind;
    layer.R = color.red;
    layer.G = color.green;
    layer.B = color.blue;
    layer.A = color.alpha;
    layer.X0 = x0;
    layer.Y0 = y0;
    layer.X1 = x1;
    layer.Y1 = y1;
    layer.R0 = r0;
    layer.R1 = r1;
    layer.StopCount = static_cast<tjs_int>(stops.size());
    layer.Stops = stops.empty() ? nullptr : stops.data();
    try {
        context.Sink->Layer(layer);
    } catch(...) {
        context.SinkFailed = true;
        return false;
    }
    ++context.Count;
    return true;
}

static bool WalkColorPaint(ColorWalkContext &context, FT_OpaquePaint opaque,
                           const ColorMatrix &matrix, FT_UInt glyphId,
                           bool haveGlyph) {
    if(context.Depth >= 64)
        return false;
    ++context.Depth;
    FT_COLR_Paint paint{};
    if(!FT_Get_Paint(context.Face, opaque, &paint)) {
        --context.Depth;
        return false;
    }
    bool ok = true;
    switch(paint.format) {
        case FT_COLR_PAINTFORMAT_COLR_LAYERS: {
            FT_LayerIterator iterator = paint.u.colr_layers.layer_iterator;
            FT_OpaquePaint child{};
            while(FT_Get_Paint_Layers(context.Face, &iterator, &child)) {
                if(!WalkColorPaint(context, child, matrix, glyphId, haveGlyph)) {
                    ok = false;
                    break;
                }
            }
            break;
        }
        case FT_COLR_PAINTFORMAT_GLYPH:
            ok = WalkColorPaint(context, paint.u.glyph.paint, matrix,
                                paint.u.glyph.glyphID, true);
            break;
        case FT_COLR_PAINTFORMAT_SOLID: {
            FT_Color color{};
            ok = haveGlyph && ColorFromIndex(context.Palette,
                                             context.PaletteCount,
                                             paint.u.solid.color, color) &&
                 EmitColorPaint(context, TVP_FONT_PAINT_SOLID, color, matrix,
                                glyphId, {}, 0, 0, 0, 0, 0, 0);
            break;
        }
        case FT_COLR_PAINTFORMAT_LINEAR_GRADIENT: {
            std::vector<tTVPFontColorStop> stops;
            FT_Color fallback{};
            const auto &gradient = paint.u.linear_gradient;
            ok = haveGlyph && ReadColorStops(context.Face, gradient.colorline,
                                             context.Palette, context.PaletteCount,
                                             stops);
            if(ok && !stops.empty()) {
                const auto p0 = ApplyColorMatrix(matrix,
                    static_cast<float>(gradient.p0.x) / 65536.0f,
                    static_cast<float>(gradient.p0.y) / 65536.0f);
                const auto p1 = ApplyColorMatrix(matrix,
                    static_cast<float>(gradient.p1.x) / 65536.0f,
                    static_cast<float>(gradient.p1.y) / 65536.0f);
                fallback = FT_Color{stops.front().R, stops.front().G,
                                    stops.front().B, stops.front().A};
                ok = EmitColorPaint(context, TVP_FONT_PAINT_LINEAR, fallback,
                                    matrix, glyphId, stops, p0[0], p0[1],
                                    p1[0], p1[1], 0, 0);
            }
            break;
        }
        case FT_COLR_PAINTFORMAT_RADIAL_GRADIENT: {
            std::vector<tTVPFontColorStop> stops;
            const auto &gradient = paint.u.radial_gradient;
            ok = haveGlyph && ReadColorStops(context.Face, gradient.colorline,
                                             context.Palette, context.PaletteCount,
                                             stops);
            if(ok && !stops.empty()) {
                const auto c0 = ApplyColorMatrix(matrix,
                    static_cast<float>(gradient.c0.x) / 65536.0f,
                    static_cast<float>(gradient.c0.y) / 65536.0f);
                const auto c1 = ApplyColorMatrix(matrix,
                    static_cast<float>(gradient.c1.x) / 65536.0f,
                    static_cast<float>(gradient.c1.y) / 65536.0f);
                const float scale = std::sqrt(std::fabs(matrix.A * matrix.E -
                                                         matrix.B * matrix.D));
                FT_Color fallback{stops.front().R, stops.front().G,
                                  stops.front().B, stops.front().A};
                ok = EmitColorPaint(context, TVP_FONT_PAINT_RADIAL, fallback,
                                    matrix, glyphId, stops, c0[0], c0[1],
                                    c1[0], c1[1],
                                    static_cast<float>(gradient.r0) / 64.0f * scale,
                                    static_cast<float>(gradient.r1) / 64.0f * scale);
            }
            break;
        }
        case FT_COLR_PAINTFORMAT_TRANSFORM:
            ok = WalkColorPaint(context, paint.u.transform.paint,
                                MultiplyColorMatrix(matrix,
                                    ColorMatrixFromAffine(paint.u.transform.affine)),
                                glyphId, haveGlyph);
            break;
        case FT_COLR_PAINTFORMAT_TRANSLATE:
            ok = WalkColorPaint(context, paint.u.translate.paint,
                                MultiplyColorMatrix(matrix, TranslationMatrix(
                                    static_cast<float>(paint.u.translate.dx) / 65536.0f,
                                    static_cast<float>(paint.u.translate.dy) / 65536.0f)),
                                glyphId, haveGlyph);
            break;
        case FT_COLR_PAINTFORMAT_SCALE:
            ok = WalkColorPaint(context, paint.u.scale.paint,
                                MultiplyColorMatrix(matrix, ScaleAroundMatrix(
                                    static_cast<float>(paint.u.scale.scale_x) / 65536.0f,
                                    static_cast<float>(paint.u.scale.scale_y) / 65536.0f,
                                    static_cast<float>(paint.u.scale.center_x) / 65536.0f,
                                    static_cast<float>(paint.u.scale.center_y) / 65536.0f)),
                                glyphId, haveGlyph);
            break;
        case FT_COLR_PAINTFORMAT_ROTATE:
            ok = WalkColorPaint(context, paint.u.rotate.paint,
                                MultiplyColorMatrix(matrix, RotateAroundMatrix(
                                    static_cast<float>(paint.u.rotate.angle) /
                                        65536.0f * 0.017453292519943295f,
                                    static_cast<float>(paint.u.rotate.center_x) / 65536.0f,
                                    static_cast<float>(paint.u.rotate.center_y) / 65536.0f)),
                                glyphId, haveGlyph);
            break;
        case FT_COLR_PAINTFORMAT_SKEW: {
            const float x = std::tan(static_cast<float>(paint.u.skew.x_skew_angle) /
                                     65536.0f * 0.017453292519943295f);
            const float y = std::tan(static_cast<float>(paint.u.skew.y_skew_angle) /
                                     65536.0f * 0.017453292519943295f);
            const float cx = static_cast<float>(paint.u.skew.center_x) / 65536.0f;
            const float cy = static_cast<float>(paint.u.skew.center_y) / 65536.0f;
            const ColorMatrix skew = MultiplyColorMatrix(
                TranslationMatrix(cx, cy),
                MultiplyColorMatrix(ColorMatrix{1.0f, x, 0.0f, y, 1.0f, 0.0f},
                                    TranslationMatrix(-cx, -cy)));
            ok = WalkColorPaint(context, paint.u.skew.paint,
                                MultiplyColorMatrix(matrix, skew), glyphId,
                                haveGlyph);
            break;
        }
        case FT_COLR_PAINTFORMAT_COLR_GLYPH: {
            const FT_UInt nested = paint.u.colr_glyph.glyphID;
            if(!context.GlyphStack.insert(nested).second) {
                ok = false;
                break;
            }
            FT_OpaquePaint root{};
            ok = FT_Get_Color_Glyph_Paint(context.Face, nested,
                                           FT_COLOR_NO_ROOT_TRANSFORM, &root) &&
                 WalkColorPaint(context, root, matrix, 0, false);
            context.GlyphStack.erase(nested);
            break;
        }
        case FT_COLR_PAINTFORMAT_COMPOSITE:
            // The public sink is an ordered layer list.  Preserve the
            // backdrop/source order and let the consumer apply the composite
            // mode; unsupported blend modes remain visually conservative.
            ok = WalkColorPaint(context, paint.u.composite.backdrop_paint,
                                matrix, glyphId, haveGlyph) &&
                 WalkColorPaint(context, paint.u.composite.source_paint,
                                matrix, glyphId, haveGlyph);
            break;
        case FT_COLR_PAINTFORMAT_SWEEP_GRADIENT:
            // A sweep gradient cannot be represented by the current public
            // API.  Use its first palette stop as a deterministic solid rather
            // than silently dropping the glyph.
            if(haveGlyph) {
                std::vector<tTVPFontColorStop> stops;
                const auto &gradient = paint.u.sweep_gradient;
                ok = ReadColorStops(context.Face, gradient.colorline,
                                    context.Palette, context.PaletteCount,
                                    stops);
                if(ok && !stops.empty()) {
                    FT_Color color{stops.front().R, stops.front().G,
                                   stops.front().B, stops.front().A};
                    ok = EmitColorPaint(context, TVP_FONT_PAINT_SOLID, color,
                                        matrix, glyphId, {}, 0, 0, 0, 0, 0, 0);
                }
            } else {
                ok = false;
            }
            break;
        default:
            ok = false;
            break;
    }
    --context.Depth;
    return ok;
}

#endif // FreeType COLR v1 API

} // namespace

//---------------------------------------------------------------------------
// Shared FontStream API
//---------------------------------------------------------------------------

tTVPFontBufferHandle TVPAcquireFontBuffer(const ttstr &storage,
                                          const tjs_uint8 **data,
                                          tjs_uint64 *size) {
    tjs_int faceIndex = 0;
    auto bytes = ReadFontBytes(storage, faceIndex);
    if(!bytes)
        return nullptr;
    auto *buffer = new FontBuffer{std::move(bytes)};
    if(data)
        *data = buffer->Bytes->data();
    if(size)
        *size = static_cast<tjs_uint64>(buffer->Bytes->size());
    return buffer;
}

void TVPReleaseFontBuffer(tTVPFontBufferHandle buffer) {
    delete static_cast<FontBuffer *>(buffer);
}

ttstr TVPFontResolveKey(const ttstr &nameOrPath) {
    if(auto *info = FindOrRegister(nameOrPath))
        return info->FamilyName.IsEmpty() ? nameOrPath : info->FamilyName;
    return ttstr();
}

bool TVPFontNameKnown(const ttstr &name) {
    return FindOrRegister(name) != nullptr;
}

//---------------------------------------------------------------------------
// Face and fallback chain
//---------------------------------------------------------------------------

tTVPFontFaceHandle TVPFontAcquireFace(const ttstr &nameOrPath) {
    return AcquireFaceImpl(nameOrPath, nullptr, 0);
}

tTVPFontFaceHandle TVPFontAcquireFaceAt(const ttstr &nameOrPath,
                                        tjs_int faceIndex) {
    return AcquireFaceImpl(nameOrPath, nullptr, 0, faceIndex);
}

tTVPFontFaceHandle TVPFontAcquireFaceInstance(const ttstr &nameOrPath,
                                              const tTVPFontVarCoord *coords,
                                              tjs_int count) {
    return AcquireFaceImpl(nameOrPath, coords, count);
}

tTVPFontFaceHandle TVPFontAcquireFaceInstanceAt(
    const ttstr &nameOrPath, tjs_int faceIndex,
    const tTVPFontVarCoord *coords, tjs_int count) {
    return AcquireFaceImpl(nameOrPath, coords, count, faceIndex);
}

void TVPFontReleaseFace(tTVPFontFaceHandle face) {
    delete AsFace(face);
}

bool TVPFontGetFaceData(tTVPFontFaceHandle face, const tjs_uint8 **data,
                        tjs_uint64 *size, tjs_int *faceIndex) {
    auto *f = AsFace(face);
    if(!f || !f->Bytes)
        return false;
    if(data)
        *data = f->Bytes->data();
    if(size)
        *size = static_cast<tjs_uint64>(f->Bytes->size());
    if(faceIndex)
        *faceIndex = f->FaceIndex;
    return true;
}

tTVPFontFaceChainHandle TVPFontAcquireFaceChain(
    const ttstr &commaSeparatedNames) {
    auto chain = std::make_unique<FontFaceChain>();
    std::vector<ttstr> names;
    if(commaSeparatedNames.IsEmpty()) {
        const ttstr &defaultName = TVPGetDefaultFontName();
        if(!defaultName.IsEmpty())
            names.emplace_back(defaultName);
    } else {
        const tjs_char *begin = commaSeparatedNames.c_str();
        tjs_size start = 0;
        const tjs_size length = commaSeparatedNames.length();
        for(tjs_size i = 0; i <= length; ++i) {
            if(i != length && begin[i] != TJS_W(','))
                continue;
            ttstr token(begin + start, static_cast<tjs_int>(i - start));
            token = token.Trim();
            if(!token.IsEmpty())
                names.emplace_back(std::move(token));
            start = i + 1;
        }
    }
    std::set<std::string> seen;
    for(const auto &name : names) {
        auto face = std::unique_ptr<FontFace>(
            static_cast<FontFace *>(TVPFontAcquireFace(name)));
        if(!face)
            continue;
        if(!seen.insert(FaceIdentity(face.get())).second)
            continue;
        chain->Faces.emplace_back(std::move(face));
    }
    return chain.release();
}

void TVPFontReleaseFaceChain(tTVPFontFaceChainHandle chain) {
    delete AsChain(chain);
}

tjs_int TVPFontChainCount(tTVPFontFaceChainHandle chain) {
    auto *c = AsChain(chain);
    return c ? static_cast<tjs_int>(c->Faces.size()) : 0;
}

tTVPFontFaceHandle TVPFontChainFaceAt(tTVPFontFaceChainHandle chain,
                                       tjs_int index) {
    auto *c = AsChain(chain);
    if(!c || index < 0 || index >= static_cast<tjs_int>(c->Faces.size()))
        return nullptr;
    return c->Faces[static_cast<std::size_t>(index)].get();
}

tjs_int TVPFontChainFaceForChar(tTVPFontFaceChainHandle chain,
                                tjs_uint32 codepoint, bool preferLast) {
    auto *c = AsChain(chain);
    if(!c || !IsValidScalar(codepoint))
        return -1;
    if(preferLast) {
        for(tjs_int i = static_cast<tjs_int>(c->Faces.size()) - 1; i >= 0; --i)
            if(TVPFontGetGlyphIndex(c->Faces[static_cast<std::size_t>(i)].get(),
                                    codepoint) != 0)
                return i;
    } else {
        for(tjs_int i = 0; i < static_cast<tjs_int>(c->Faces.size()); ++i)
            if(TVPFontGetGlyphIndex(c->Faces[static_cast<std::size_t>(i)].get(),
                                    codepoint) != 0)
                return i;
    }
    return -1;
}

//---------------------------------------------------------------------------
// Metrics and glyph supply
//---------------------------------------------------------------------------

bool TVPFontGetLineMetrics(tTVPFontFaceHandle face, tjs_int pixelSize,
                           tTVPFontLineMetrics *out) {
    auto *f = AsFace(face);
    FT_Face raw = RawFace(f);
    if(!raw || !out || pixelSize <= 0 || !SetPixelSize(f, pixelSize))
        return false;
    const float units = raw->units_per_EM > 0
        ? static_cast<float>(raw->units_per_EM) : 1.0f;
    const float scale = raw->units_per_EM > 0
        ? static_cast<float>(pixelSize) / units : 1.0f;
    if(raw->units_per_EM > 0) {
        out->Ascent = static_cast<float>(raw->ascender) * scale;
        out->Descent = static_cast<float>(-raw->descender) * scale;
        const float naturalHeight = static_cast<float>(raw->height) * scale;
        out->LineGap = std::max(0.0f, naturalHeight -
                                         out->Ascent - out->Descent);
        out->UnderlineOffset = static_cast<float>(-raw->underline_position) * scale;
        out->UnderlineThickness = std::max(1.0f,
                                            static_cast<float>(raw->underline_thickness) * scale);
        out->StrikeoutOffset = out->Ascent * 0.7f;
        out->StrikeoutThickness = out->UnderlineThickness;
        out->UnitsPerEm = units;
    } else {
        // Bitmap-strike fonts have no meaningful font-unit scale.  Expose a
        // pixel-space line box while keeping the API well-defined.
        out->Ascent = raw->size ? raw->size->metrics.ascender / 64.0f
                                : static_cast<float>(pixelSize);
        out->Descent = raw->size ? -raw->size->metrics.descender / 64.0f : 0.0f;
        out->LineGap = 0.0f;
        out->UnderlineOffset = 0.0f;
        out->UnderlineThickness = 1.0f;
        out->StrikeoutOffset = out->Ascent * 0.7f;
        out->StrikeoutThickness = 1.0f;
        out->UnitsPerEm = 1.0f;
    }
    return true;
}

tjs_uint32 TVPFontGetGlyphIndex(tTVPFontFaceHandle face,
                                tjs_uint32 codepoint) {
    auto *f = AsFace(face);
    FT_Face raw = RawFace(f);
    if(!raw || !IsValidScalar(codepoint))
        return 0;
    return static_cast<tjs_uint32>(FT_Get_Char_Index(raw, codepoint));
}

bool TVPFontGetGlyphMetrics(tTVPFontFaceHandle face, tjs_uint32 glyphId,
                            tjs_int pixelSize, bool bold, bool italic,
                            tTVPFontGlyphMetrics *out) {
    return FillGlyphMetrics(AsFace(face), glyphId, pixelSize, bold, italic,
                            TVP_FONT_METRICS_HINTED, out);
}

bool TVPFontGetGlyphMetricsEx(tTVPFontFaceHandle face, tjs_uint32 glyphId,
                              tjs_int pixelSize, bool bold, bool italic,
                              tjs_int mode, tTVPFontGlyphMetrics *out) {
    return FillGlyphMetrics(AsFace(face), glyphId, pixelSize, bold, italic,
                            mode, out);
}

bool TVPFontGetGlyphBitmap(tTVPFontFaceHandle face, tjs_uint32 glyphId,
                           tjs_int pixelSize, bool color, bool bold,
                           bool italic, tTVPFontGlyphBitmap *out) {
    auto *f = AsFace(face);
    if(!out || !LoadGlyph(f, glyphId, pixelSize, color, bold, italic,
                          TVP_FONT_METRICS_HINTED))
        return false;
    FT_Face raw = RawFace(f);
    if(raw->glyph->format != FT_GLYPH_FORMAT_BITMAP) {
        if(FT_Render_Glyph(raw->glyph, FT_RENDER_MODE_NORMAL) != 0)
            return false;
    }
    return CopyBitmap(f, raw->glyph->bitmap, out);
}

bool TVPFontGetGlyphOutline(tTVPFontFaceHandle face, tjs_uint32 glyphId,
                            bool bold, bool italic,
                            iTVPFontOutlineSink *sink) {
    auto *f = AsFace(face);
    FT_Face raw = RawFace(f);
    if(!raw || !sink || !FT_IS_SCALABLE(raw) ||
       !LoadGlyph(f, glyphId, 0, false, bold, italic,
                  TVP_FONT_METRICS_UNSCALED) ||
       raw->glyph->format != FT_GLYPH_FORMAT_OUTLINE)
        return false;

    OutlineContext context{sink, false, false};
    FT_Outline_Funcs funcs{};
    funcs.move_to = OutlineMove;
    funcs.line_to = OutlineLine;
    funcs.conic_to = OutlineConic;
    funcs.cubic_to = OutlineCubic;
    funcs.shift = 0;
    funcs.delta = 0;
    const FT_Error error = FT_Outline_Decompose(&raw->glyph->outline,
                                                &funcs, &context);
    if(context.HaveContour && !context.Failed) {
        try {
            sink->ClosePath();
        } catch(...) {
            context.Failed = true;
        }
    }
    return error == 0 && !context.Failed;
}

bool TVPFontRenderGlyphMask(tTVPFontFaceHandle face, tjs_uint32 glyphId,
                            const tTVPFontRenderParams *params,
                            tTVPFontGlyphMask *out) {
    auto *f = AsFace(face);
    FT_Face raw = RawFace(f);
    if(!f || !raw || !params || !out ||
       !std::isfinite(params->StrokeWidth) || params->StrokeWidth < 0.0f ||
       !FT_IS_SCALABLE(raw) ||
       !LoadGlyph(f, glyphId, 0, false, params->Bold, params->Italic,
                  TVP_FONT_METRICS_UNSCALED) ||
       raw->glyph->format != FT_GLYPH_FORMAT_OUTLINE)
        return false;

    FT_Glyph glyph = nullptr;
    if(FT_Get_Glyph(raw->glyph, &glyph) != 0 || !glyph)
        return false;
    auto releaseGlyph = [&]() {
        if(glyph)
            FT_Done_Glyph(glyph);
        glyph = nullptr;
    };

    // FT_Glyph_Transform uses 16.16 matrix coefficients and 26.6 deltas;
    // this is exactly the FontService transform contract (font units -> px).
    FT_Matrix matrix{};
    for(int i = 0; i < 4; ++i) {
        const float value = params->Transform[i == 0 ? 0 :
                                               i == 1 ? 1 :
                                               i == 2 ? 3 : 4];
        if(!std::isfinite(value) || std::fabs(value) > 32767.0f) {
            releaseGlyph();
            return false;
        }
    }
    matrix.xx = static_cast<FT_Fixed>(std::llround(
        static_cast<double>(params->Transform[0]) * 65536.0));
    matrix.xy = static_cast<FT_Fixed>(std::llround(
        static_cast<double>(params->Transform[1]) * 65536.0));
    matrix.yx = static_cast<FT_Fixed>(std::llround(
        static_cast<double>(params->Transform[3]) * 65536.0));
    matrix.yy = static_cast<FT_Fixed>(std::llround(
        static_cast<double>(params->Transform[4]) * 65536.0));
    FT_Vector delta{};
    if(!std::isfinite(params->Transform[2]) ||
       !std::isfinite(params->Transform[5]) ||
       std::fabs(params->Transform[2]) >
           static_cast<float>(std::numeric_limits<FT_Pos>::max() / 64) ||
       std::fabs(params->Transform[5]) >
           static_cast<float>(std::numeric_limits<FT_Pos>::max() / 64)) {
        releaseGlyph();
        return false;
    }
    delta.x = static_cast<FT_Pos>(std::llround(
        static_cast<double>(params->Transform[2]) * 64.0));
    delta.y = static_cast<FT_Pos>(std::llround(
        static_cast<double>(params->Transform[5]) * 64.0));
    FT_Glyph_Transform(glyph, &matrix, &delta);

    if(params->StrokeWidth > 0.0f) {
        const double radius = static_cast<double>(params->StrokeWidth) * 32.0;
        if(!std::isfinite(radius) || radius >
           static_cast<double>(std::numeric_limits<FT_Fixed>::max())) {
            releaseGlyph();
            return false;
        }
        FT_Stroker stroker = nullptr;
        if(FT_Stroker_New(FreeTypeLibrary, &stroker) != 0 || !stroker) {
            releaseGlyph();
            return false;
        }
        const FT_Stroker_LineCap cap = params->Cap == TVP_FONT_CAP_BUTT
            ? FT_STROKER_LINECAP_BUTT
            : params->Cap == TVP_FONT_CAP_SQUARE
                ? FT_STROKER_LINECAP_SQUARE : FT_STROKER_LINECAP_ROUND;
        const FT_Stroker_LineJoin join = params->Join == TVP_FONT_JOIN_BEVEL
            ? FT_STROKER_LINEJOIN_BEVEL
            : params->Join == TVP_FONT_JOIN_MITER
                ? FT_STROKER_LINEJOIN_MITER_VARIABLE
                : FT_STROKER_LINEJOIN_ROUND;
        const float miter = std::isfinite(params->MiterLimit) &&
                                    params->MiterLimit > 0.0f
            ? params->MiterLimit : 4.0f;
        FT_Stroker_Set(stroker, static_cast<FT_Fixed>(std::llround(radius)),
                       cap, join, static_cast<FT_Fixed>(std::llround(
                           static_cast<double>(miter) * 65536.0)));
        const FT_Error error = FT_Glyph_Stroke(&glyph, stroker, 1);
        FT_Stroker_Done(stroker);
        if(error != 0 || !glyph) {
            releaseGlyph();
            return false;
        }
    }

    if(glyph->format != FT_GLYPH_FORMAT_OUTLINE) {
        releaseGlyph();
        return false;
    }
    auto *outlineGlyph = reinterpret_cast<FT_OutlineGlyph>(glyph);
    FT_Outline *outline = &outlineGlyph->outline;
    FT_BBox box{};
    FT_Outline_Get_CBox(outline, &box);
    if(box.xMax <= box.xMin || box.yMax <= box.yMin)
    {
        releaseGlyph();
        return false;
    }
    const auto ceil26 = [](FT_Pos value) -> FT_Pos {
        return (value + 63) & ~static_cast<FT_Pos>(63);
    };
    const auto floor26 = [](FT_Pos value) -> FT_Pos {
        if(value >= 0)
            return value & ~static_cast<FT_Pos>(63);
        return -(((-value) + 63) & ~static_cast<FT_Pos>(63));
    };
    const FT_Pos minX = floor26(box.xMin);
    const FT_Pos minY = floor26(box.yMin);
    const FT_Pos maxX = ceil26(box.xMax);
    const FT_Pos maxY = ceil26(box.yMax);
    const FT_Pos width26 = maxX - minX;
    const FT_Pos height26 = maxY - minY;
    if(width26 <= 0 || height26 <= 0 ||
       width26 / 64 > std::numeric_limits<tjs_int>::max() ||
       height26 / 64 > std::numeric_limits<tjs_int>::max()) {
        releaseGlyph();
        return false;
    }
    const tjs_int width = static_cast<tjs_int>(width26 / 64);
    const tjs_int height = static_cast<tjs_int>(height26 / 64);
    if(static_cast<std::size_t>(width) >
           std::numeric_limits<std::size_t>::max() /
               static_cast<std::size_t>(height)) {
        releaseGlyph();
        return false;
    }
    f->Mask.assign(static_cast<std::size_t>(width) * height, 0);
    for(int i = 0; i < outline->n_points; ++i) {
        outline->points[i].x -= minX;
        outline->points[i].y -= minY;
    }
    FT_Bitmap bitmap{};
    bitmap.rows = static_cast<unsigned int>(height);
    bitmap.width = static_cast<unsigned int>(width);
    bitmap.pitch = width;
    bitmap.buffer = f->Mask.data();
    bitmap.num_grays = 256;
    bitmap.pixel_mode = FT_PIXEL_MODE_GRAY;
    FT_Raster_Params raster{};
    raster.target = &bitmap;
    raster.source = outline;
    raster.flags = FT_RASTER_FLAG_AA;
    if(FT_Outline_Render(FreeTypeLibrary, outline, &raster) != 0) {
        releaseGlyph();
        return false;
    }
    out->Left = static_cast<tjs_int>(minX / 64);
    out->Top = static_cast<tjs_int>(maxY / 64);
    out->Width = width;
    out->Height = height;
    out->Pitch = width;
    out->Buffer = f->Mask.data();
    releaseGlyph();
    return true;
}

tjs_int TVPFontGetColorLayers(tTVPFontFaceHandle face, tjs_uint32 glyphId,
                              tjs_int pixelSize,
                              iTVPFontColorLayerSink *sink, float *clipBox) {
    auto *f = AsFace(face);
    FT_Face raw = RawFace(f);
    if(clipBox)
        std::fill(clipBox, clipBox + 4, 0.0f);
    if(!raw || !sink || pixelSize <= 0 || !FT_HAS_COLOR(raw))
        return 0;
    if(!SetPixelSize(f, pixelSize))
        return 0;

    FT_Color *palette = nullptr;
    FT_Palette_Data paletteData{};
    if(FT_Palette_Data_Get(raw, &paletteData) != 0 ||
       FT_Palette_Select(raw, 0, &palette) != 0 || !palette)
        return 0;

#if defined(FREETYPE_MAJOR) && (FREETYPE_MAJOR > 2 || \
                                 (FREETYPE_MAJOR == 2 && FREETYPE_MINOR >= 13))
    // Prefer the COLR v1 paint graph.  The public sink can represent solid,
    // linear and radial paints; transforms and nested color glyphs are
    // flattened by WalkColorPaint.  If a malformed/unsupported graph cannot
    // be flattened, fall through to the long-standing COLR v0 layer API.
    FT_OpaquePaint rootPaint{};
    if(FT_Get_Color_Glyph_Paint(raw, static_cast<FT_UInt>(glyphId),
                                FT_COLOR_NO_ROOT_TRANSFORM, &rootPaint)) {
        const float scale = raw->units_per_EM > 0
            ? static_cast<float>(pixelSize) /
                  static_cast<float>(raw->units_per_EM)
            : 1.0f;
        ColorWalkContext context;
        context.Face = raw;
        context.Palette = palette;
        context.PaletteCount = paletteData.num_palette_entries;
        context.Sink = sink;
        ColorMatrix root{};
        root.A = scale;
        root.E = scale;
        context.GlyphStack.insert(static_cast<FT_UInt>(glyphId));
        const bool walked = WalkColorPaint(context, rootPaint, root, 0, false);
        context.GlyphStack.erase(static_cast<FT_UInt>(glyphId));
        if(context.SinkFailed)
            return 0;
        if(clipBox) {
            FT_ClipBox box{};
            if(FT_Get_Color_Glyph_ClipBox(raw, static_cast<FT_UInt>(glyphId),
                                          &box)) {
                const std::array<FT_Vector, 4> corners{
                    box.bottom_left, box.top_left, box.top_right,
                    box.bottom_right};
                float minX = std::numeric_limits<float>::max();
                float minY = std::numeric_limits<float>::max();
                float maxX = std::numeric_limits<float>::lowest();
                float maxY = std::numeric_limits<float>::lowest();
                for(const auto &corner : corners) {
                    // ClipBox is already expressed in 26.6 pixels after
                    // SetPixelSize; unlike paint coordinates it must not be
                    // scaled a second time by the root matrix.
                    const float x = static_cast<float>(corner.x) / 64.0f;
                    const float y = static_cast<float>(corner.y) / 64.0f;
                    minX = std::min(minX, x);
                    minY = std::min(minY, y);
                    maxX = std::max(maxX, x);
                    maxY = std::max(maxY, y);
                }
                clipBox[0] = minX;
                clipBox[1] = minY;
                clipBox[2] = maxX;
                clipBox[3] = maxY;
            }
        }
        if(walked && context.Count > 0)
            return context.Count;
    }
#endif

    FT_LayerIterator iterator{};
    FT_UInt layerGlyph = 0;
    FT_UInt colorIndex = 0;
    tjs_int count = 0;
    while(FT_Get_Color_Glyph_Layer(raw, static_cast<FT_UInt>(glyphId),
                                   &layerGlyph, &colorIndex, &iterator)) {
        FT_Color color{};
        if(colorIndex == 0xffffu) {
            color.red = color.green = color.blue = 0;
            color.alpha = 255;
        } else if(colorIndex < paletteData.num_palette_entries) {
            color = palette[colorIndex];
        } else {
            continue;
        }
        tTVPFontColorLayer layer{};
        layer.GlyphId = layerGlyph;
        layer.Transform[0] = 1.0f;
        layer.Transform[4] = 1.0f;
        layer.PaintKind = TVP_FONT_PAINT_SOLID;
        layer.R = color.red;
        layer.G = color.green;
        layer.B = color.blue;
        layer.A = color.alpha;
        try {
            sink->Layer(layer);
        } catch(...) {
            break;
        }
        ++count;
    }
    return count;
}

//---------------------------------------------------------------------------
// Variable fonts
//---------------------------------------------------------------------------

tjs_int TVPFontGetVarAxes(tTVPFontFaceHandle face, tTVPFontVarAxis *out,
                          tjs_int maxCount) {
    FT_Face raw = RawFace(AsFace(face));
    if(!raw)
        return 0;
    FT_MM_Var *mm = nullptr;
    if(FT_Get_MM_Var(raw, &mm) != 0 || !mm)
        return 0;
    const tjs_int total = static_cast<tjs_int>(mm->num_axis);
    if(out && maxCount > 0) {
        const tjs_int count = std::min(total, maxCount);
        for(tjs_int i = 0; i < count; ++i) {
            out[i].Tag = mm->axis[i].tag;
            out[i].MinValue = static_cast<float>(mm->axis[i].minimum) / 65536.0f;
            out[i].DefaultValue = static_cast<float>(mm->axis[i].def) / 65536.0f;
            out[i].MaxValue = static_cast<float>(mm->axis[i].maximum) / 65536.0f;
        }
    }
    FT_Done_MM_Var(FreeTypeLibrary, mm);
    return total;
}

bool TVPFontSetVariations(tTVPFontFaceHandle face,
                          const tTVPFontVarCoord *coords, tjs_int count) {
    auto *fontFace = AsFace(face);
    FT_Face raw = RawFace(fontFace);
    if(!raw || !coords || count <= 0)
        return false;
    FT_MM_Var *mm = nullptr;
    if(FT_Get_MM_Var(raw, &mm) != 0 || !mm)
        return false;
    std::vector<FT_Fixed> values(mm->num_axis);
    // Preserve coordinates that were set on this face previously.  The
    // upstream contract explicitly says omitted axes keep their current
    // value; resetting everything to defaults made successive slider updates
    // silently undo unrelated axes.
    if(FT_Get_Var_Design_Coordinates(raw, mm->num_axis, values.data()) != 0)
        for(FT_UInt i = 0; i < mm->num_axis; ++i)
            values[i] = mm->axis[i].def;
    bool applied = false;
    std::vector<tTVPFontVarCoord> appliedCoords;
    for(tjs_int i = 0; i < count; ++i) {
        for(FT_UInt axis = 0; axis < mm->num_axis; ++axis) {
            if(mm->axis[axis].tag != coords[i].Tag)
                continue;
            const float minValue = static_cast<float>(mm->axis[axis].minimum) /
                                   65536.0f;
            const float maxValue = static_cast<float>(mm->axis[axis].maximum) /
                                   65536.0f;
            const float requested = std::isfinite(coords[i].Value)
                ? coords[i].Value
                : static_cast<float>(mm->axis[axis].def) / 65536.0f;
            const float value = std::max(minValue,
                                         std::min(maxValue, requested));
            values[axis] = static_cast<FT_Fixed>(
                std::llround(static_cast<double>(value) * 65536.0));
            applied = true;
            appliedCoords.push_back(tTVPFontVarCoord{coords[i].Tag, value});
            break;
        }
    }
    const FT_Error error = applied
        ? FT_Set_Var_Design_Coordinates(raw, mm->num_axis, values.data())
        : 1;
    FT_Done_MM_Var(FreeTypeLibrary, mm);
    if(error == 0) {
        for(const auto &coord : appliedCoords) {
            bool replaced = false;
            for(auto &existing : fontFace->Variations) {
                if(existing.Tag == coord.Tag) {
                    existing.Value = coord.Value;
                    replaced = true;
                    break;
                }
            }
            if(!replaced)
                fontFace->Variations.push_back(coord);
        }
    }
    return applied && error == 0;
}

//---------------------------------------------------------------------------
// Basic, lossless fallback shaping
//---------------------------------------------------------------------------

static bool ShapeLineBasic(tTVPFontFaceChainHandle chain, const ttstr &text,
                           tjs_int pixelSize, tjs_int baseDirection,
                           iTVPFontShapeSink *sink) {
    auto *c = AsChain(chain);
    if(!c || c->Faces.empty() || !sink || pixelSize <= 0)
        return false;

    struct Item {
        tTVPFontShapedGlyph Glyph{};
    };
    std::vector<Item> items;
    tjs_size index = 0;
    tjs_uint32 utf8Cluster = 0;
    const tjs_size textLength = static_cast<tjs_size>(text.length());
    while(index < textLength) {
        tjs_uint32 codepoint = 0;
        tjs_size consumed = 0;
        const tjs_uint32 cluster = utf8Cluster;
        if(!Utf16CodepointAt(text, index, codepoint, consumed) || consumed == 0)
            break;
        index += consumed;
        utf8Cluster += Utf8LengthForCodepoint(codepoint);
        // Variation selectors are default-ignorables for line breaking but
        // are meaningful to emoji shaping (text vs emoji presentation).  Do
        // not discard them; only the truly invisible controls are omitted by
        // the scalar fallback.
        if(TVPIsUnicodeDefaultIgnorable(codepoint) &&
           !IsVariationSelector(codepoint) && !IsJoinControl(codepoint))
            continue;
        const tjs_int faceIndex = TVPFontChainFaceForChar(chain, codepoint, false);
        if(faceIndex < 0)
            continue;
        auto *face = c->Faces[static_cast<std::size_t>(faceIndex)].get();
        const tjs_uint32 glyphId = TVPFontGetGlyphIndex(face, codepoint);
        tTVPFontGlyphMetrics metrics{};
        if(glyphId == 0 || !TVPFontGetGlyphMetricsEx(
                                face, glyphId, pixelSize, false, false,
                                TVP_FONT_METRICS_UNHINTED, &metrics))
            continue;
        Item item;
        item.Glyph.GlyphId = glyphId;
        item.Glyph.FaceIndexInChain = faceIndex;
        item.Glyph.Cluster = cluster;
        item.Glyph.Advance = metrics.AdvanceX;
        item.Glyph.XOffset = 0.0f;
        item.Glyph.YOffset = 0.0f;
        items.emplace_back(item);
    }
    const bool rtl = baseDirection == TVP_FONT_BASEDIR_RTL;
    if(rtl)
        std::reverse(items.begin(), items.end());
    float width = 0.0f;
    for(auto &item : items) {
        item.Glyph.X = width;
        item.Glyph.Y = 0.0f;
        item.Glyph.RTL = rtl;
        width += item.Glyph.Advance;
    }
    tTVPFontLineMetrics line{};
    TVPFontGetLineMetrics(c->Faces.front().get(), pixelSize, &line);
    try {
        sink->Begin(static_cast<tjs_int>(items.size()), width, line.Ascent,
                    line.Descent);
        for(const auto &item : items)
            sink->Glyph(item.Glyph);
    } catch(...) {
        return false;
    }
    return true;
}

bool TVPFontShapeLine(tTVPFontFaceChainHandle chain, const ttstr &text,
                      tjs_int pixelSize, tjs_int baseDirection,
                      iTVPFontShapeSink *sink) {
    if(!chain || !sink || pixelSize <= 0)
        return false;
#if defined(AETHERKIRI_FONT_SHAPING_ENABLED)
    std::vector<ShapeCodepoint> records;
    std::string utf8;
    if(BuildShapeCodepoints(text, records, utf8) && !records.empty()) {
        std::vector<ShapedGlyphValue> shaped;
        if(ShapeLineHarfBuzz(AsChain(chain), records, pixelSize, baseDirection,
                             shaped) && !shaped.empty()) {
            float width = 0.0f;
            for(const auto &value : shaped)
                width += value.Advance;
            auto *c = AsChain(chain);
            tTVPFontLineMetrics line{};
            if(!c || c->Faces.empty() ||
               !TVPFontGetLineMetrics(c->Faces.front().get(), pixelSize, &line))
                return false;
            float pen = 0.0f;
            try {
                sink->Begin(static_cast<tjs_int>(shaped.size()), width,
                            line.Ascent, line.Descent);
                for(const auto &value : shaped) {
                    tTVPFontShapedGlyph glyph{};
                    glyph.GlyphId = value.GlyphId;
                    glyph.FaceIndexInChain = value.FaceIndex;
                    glyph.X = pen;
                    glyph.Y = 0.0f;
                    glyph.XOffset = value.XOffset;
                    glyph.YOffset = value.YOffset;
                    glyph.Advance = value.Advance;
                    glyph.Cluster = value.Cluster;
                    glyph.RTL = value.RTL;
                    sink->Glyph(glyph);
                    pen += value.Advance;
                }
            } catch(...) {
                // Begin/Glyph callbacks are a transaction from the
                // consumer's point of view.  Once Begin has been called we
                // cannot safely invoke the scalar fallback: doing so would
                // emit a second Begin and duplicate a partially delivered
                // glyph run.  Report the failure to the caller instead.
                return false;
            }
            return true;
        }
    }
#endif
    return ShapeLineBasic(chain, text, pixelSize, baseDirection, sink);
}

//---------------------------------------------------------------------------
// Metadata query
//---------------------------------------------------------------------------

tjs_int TVPFontQueryFaces(const tTVPFontQueryParams &params,
                          iTVPFontQuerySink *sink) {
    if(!sink)
        return 0;
    std::vector<ttstr> names;
    TVPGetAllFontList(names);
    std::sort(names.begin(), names.end(), [](const ttstr &lhs, const ttstr &rhs) {
        return lhs.AsStdString() < rhs.AsStdString();
    });
    std::set<std::string> seen;
    tjs_int count = 0;
    for(const auto &name : names) {
        auto *face = static_cast<FontFace *>(TVPFontAcquireFace(name));
        if(!face)
            continue;
        const std::string identity = FaceIdentity(face);
        if(!seen.insert(identity).second) {
            TVPFontReleaseFace(face);
            continue;
        }
        tTVPFontFaceInfo info{};
        FillFaceInfo(face, name, &info);
        bool match = true;
        if(!params.Name.IsEmpty()) {
            const std::string requested = params.Name.AsStdString();
            match = info.Family.AsStdString() == requested ||
                    info.FullName.AsStdString() == requested ||
                    info.PostScriptName.AsStdString() == requested ||
                    name.AsStdString() == requested;
        }
        if(match && params.Weight >= 0)
            match = info.Weight == params.Weight;
        if(match && params.Slant >= 0)
            match = info.Slant == params.Slant;
        if(match && params.Monospace >= 0)
            match = info.Monospace == (params.Monospace != 0);
        if(match && params.Color >= 0)
            match = info.Color == (params.Color != 0);
        if(match && !params.Script.IsEmpty())
            match = FaceSupportsScript(face, params.Script);
        if(match)
            match = ContainsAll(params.ContainsText, face);
        if(match) {
            try {
                sink->Found(info);
                ++count;
            } catch(...) {
                TVPFontReleaseFace(face);
                break;
            }
        }
        TVPFontReleaseFace(face);
    }
    return count;
}

bool TVPFontGetFaceInfo(const ttstr &nameOrPath, tTVPFontFaceInfo *out) {
    if(!out)
        return false;
    auto *face = static_cast<FontFace *>(TVPFontAcquireFace(nameOrPath));
    if(!face)
        return false;
    FillFaceInfo(face, nameOrPath, out);
    TVPFontReleaseFace(face);
    return true;
}

bool TVPFontGetFaceInfoAt(const ttstr &nameOrPath, tjs_int faceIndex,
                          tTVPFontFaceInfo *out) {
    if(!out)
        return false;
    auto *face = static_cast<FontFace *>(
        TVPFontAcquireFaceAt(nameOrPath, faceIndex));
    if(!face)
        return false;
    FillFaceInfo(face, nameOrPath, out);
    TVPFontReleaseFace(face);
    return true;
}

const void *TVPGetFontTvgBridge() {
    // The ThorVG glyphware bridge is an optional upstream component.  Aether
    // currently exposes FreeType geometry/bitmap APIs but does not link that
    // fork, so returning nullptr is the explicit capability boundary.
    return nullptr;
}
