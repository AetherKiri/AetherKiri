#include "BitmapIntf.h"
#include "LayerIntf.h"
#include "StorageIntf.h"
#include "tjsArray.h"
#include "ncbind.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

// tftSave is a small, useful cache format rather than a Windows-only API.
// The original plug-in used IStream and DirectWrite, which made the whole
// module unavailable on macOS/Linux.  Keep the byte format compatible with
// the upstream loader while replacing only the storage/callback boundary.

namespace {

constexpr std::array<std::uint8_t, 24> kHeader = {
    'T', 'V', 'P', ' ', 'p', 'r', 'e', '-', 'r', 'e', 'n', 'd', 'e', 'r',
    'e', 'd', ' ', 'f', 'o', 'n', 't', 0x1a, 0x01, 0x02};
constexpr std::size_t kHeaderSize = kHeader.size();
constexpr std::size_t kFixedPrefixSize = kHeaderSize + 12;
constexpr std::size_t kIndexRecordSize = 20;
constexpr std::size_t kMaxFileBytes = 256u * 1024u * 1024u;
constexpr std::size_t kMaxGlyphs = 65536;
constexpr std::size_t kMaxGlyphPixels = 64u * 1024u * 1024u;

struct GlyphRecord {
    std::uint16_t code = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::int16_t originX = 0;
    std::int16_t originY = 0;
    std::int16_t incX = 0;
    std::int16_t incY = 0;
    std::int16_t inc = 0;
    std::vector<std::uint8_t> alpha; // 65-level values (0..64)
    std::uint32_t dataOffset = 0;
};

struct FontFile {
    std::vector<std::uint8_t> bytes;
    std::vector<GlyphRecord> glyphs;
    std::uint32_t characterIndex = 0;
    std::uint32_t recordIndex = 0;
};

std::uint16_t readLe16(const std::uint8_t *data) {
    return static_cast<std::uint16_t>(data[0]) |
        static_cast<std::uint16_t>(data[1] << 8);
}

std::uint32_t readLe32(const std::uint8_t *data) {
    return static_cast<std::uint32_t>(data[0]) |
        (static_cast<std::uint32_t>(data[1]) << 8) |
        (static_cast<std::uint32_t>(data[2]) << 16) |
        (static_cast<std::uint32_t>(data[3]) << 24);
}

std::int16_t readLeS16(const std::uint8_t *data) {
    return static_cast<std::int16_t>(readLe16(data));
}

void appendLe16(std::vector<std::uint8_t> &out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
}

void appendLe32(std::vector<std::uint8_t> &out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 24));
}

void storeLe32(std::vector<std::uint8_t> &out, std::size_t offset,
               std::uint32_t value) {
    if(offset + 4 > out.size())
        return;
    out[offset] = static_cast<std::uint8_t>(value);
    out[offset + 1] = static_cast<std::uint8_t>(value >> 8);
    out[offset + 2] = static_cast<std::uint8_t>(value >> 16);
    out[offset + 3] = static_cast<std::uint8_t>(value >> 24);
}

std::size_t align4(std::size_t value) { return (value + 3u) & ~std::size_t(3u); }

bool readStorage(const ttstr &storage, std::vector<std::uint8_t> &bytes) {
    bytes.clear();
    if(storage.IsEmpty())
        return false;
    try {
        std::unique_ptr<tTJSBinaryStream> stream(
            TVPCreateStream(storage, TJS_BS_READ));
        if(!stream)
            return false;
        const tjs_uint64 size = stream->GetSize();
        if(size > kMaxFileBytes || size >
               static_cast<tjs_uint64>(std::numeric_limits<std::size_t>::max()))
            return false;
        bytes.resize(static_cast<std::size_t>(size));
        std::size_t offset = 0;
        constexpr std::size_t kChunk = 1024u * 1024u;
        while(offset < bytes.size()) {
            const tjs_uint count = static_cast<tjs_uint>(std::min(
                kChunk, bytes.size() - offset));
            if(stream->Read(bytes.data() + offset, count) != count)
                return false;
            offset += count;
        }
        return true;
    } catch(...) {
        return false;
    }
}

bool writeStorage(const ttstr &storage, const std::vector<std::uint8_t> &bytes) {
    if(storage.IsEmpty() || bytes.size() > kMaxFileBytes)
        return false;
    try {
        std::unique_ptr<tTJSBinaryStream> stream(
            TVPCreateStream(storage, TJS_BS_WRITE));
        if(!stream)
            return false;
        std::size_t offset = 0;
        constexpr std::size_t kChunk = 1024u * 1024u;
        while(offset < bytes.size()) {
            const tjs_uint count = static_cast<tjs_uint>(std::min(
                kChunk, bytes.size() - offset));
            if(stream->Write(bytes.data() + offset, count) != count)
                return false;
            offset += count;
        }
        return true;
    } catch(...) {
        return false;
    }
}

bool decodeAlpha(const std::vector<std::uint8_t> &bytes, std::size_t begin,
                 std::size_t end, std::size_t expected,
                 std::vector<std::uint8_t> &out) {
    if(begin > end || end > bytes.size() || expected > kMaxGlyphPixels)
        return false;
    out.clear();
    out.reserve(expected);
    std::uint8_t previous = 0;
    bool havePrevious = false;
    for(std::size_t cursor = begin; cursor < end && out.size() < expected;
        ++cursor) {
        const std::uint8_t value = bytes[cursor];
        if(value <= 0x40) {
            out.push_back(value);
            previous = value;
            havePrevious = true;
            continue;
        }
        const std::size_t run = static_cast<std::size_t>(value - 0x40);
        if(run == 0 || run > expected - out.size())
            return false;
        const std::uint8_t fill = havePrevious ? previous : 0;
        out.insert(out.end(), run, fill);
    }
    return out.size() == expected;
}

void encodeAlpha(const std::vector<std::uint8_t> &alpha,
                 std::vector<std::uint8_t> &out) {
    std::size_t index = 0;
    while(index < alpha.size()) {
        const std::uint8_t value = alpha[index];
        std::size_t end = index + 1;
        while(end < alpha.size() && alpha[end] == value)
            ++end;
        std::size_t run = end - index;
        if(run >= 2) {
            while(run > 0) {
                const std::size_t chunk = std::min<std::size_t>(run, 190);
                out.push_back(static_cast<std::uint8_t>(0x40 + chunk));
                run -= chunk;
            }
        } else {
            out.push_back(value);
        }
        index = end;
    }
}

bool parseFont(std::vector<std::uint8_t> bytes, FontFile &out,
               std::string *error = nullptr) {
    auto fail = [&](const char *message) {
        if(error)
            *error = message;
        return false;
    };
    out = FontFile{};
    if(bytes.size() < kFixedPrefixSize)
        return fail("tft file is shorter than its header");
    if(!std::equal(kHeader.begin(), kHeader.end(), bytes.begin()))
        return fail("invalid tft header");

    const std::uint32_t count = readLe32(bytes.data() + kHeaderSize);
    const std::uint32_t characterIndex =
        readLe32(bytes.data() + kHeaderSize + 4);
    const std::uint32_t recordIndex =
        readLe32(bytes.data() + kHeaderSize + 8);
    if(count == 0 || count > kMaxGlyphs)
        return fail("invalid tft glyph count");
    if(characterIndex < kFixedPrefixSize || recordIndex < characterIndex ||
       characterIndex > bytes.size() || recordIndex > bytes.size())
        return fail("invalid tft index offsets");
    const std::size_t charsEnd = static_cast<std::size_t>(characterIndex) +
        static_cast<std::size_t>(count) * 2u;
    const std::size_t recordsEnd = static_cast<std::size_t>(recordIndex) +
        static_cast<std::size_t>(count) * kIndexRecordSize;
    if(charsEnd > bytes.size() || recordsEnd > bytes.size())
        return fail("truncated tft index");

    out.bytes = std::move(bytes);
    out.characterIndex = characterIndex;
    out.recordIndex = recordIndex;
    out.glyphs.resize(count);
    std::unordered_set<std::uint16_t> seen;
    for(std::size_t i = 0; i < count; ++i) {
        const auto code = readLe16(out.bytes.data() + characterIndex + i * 2u);
        if(!seen.insert(code).second)
            return fail("duplicate tft character");
        out.glyphs[i].code = code;
    }

    std::size_t previousOffset = kFixedPrefixSize;
    for(std::size_t i = 0; i < count; ++i) {
        const std::size_t p = static_cast<std::size_t>(recordIndex) +
            i * kIndexRecordSize;
        auto &glyph = out.glyphs[i];
        glyph.dataOffset = readLe32(out.bytes.data() + p);
        glyph.width = readLe16(out.bytes.data() + p + 4);
        glyph.height = readLe16(out.bytes.data() + p + 6);
        glyph.originX = readLeS16(out.bytes.data() + p + 8);
        glyph.originY = readLeS16(out.bytes.data() + p + 10);
        glyph.incX = readLeS16(out.bytes.data() + p + 12);
        glyph.incY = readLeS16(out.bytes.data() + p + 14);
        glyph.inc = readLeS16(out.bytes.data() + p + 16);
        const std::size_t pixelCount = static_cast<std::size_t>(glyph.width) *
            static_cast<std::size_t>(glyph.height);
        if(pixelCount > kMaxGlyphPixels || glyph.dataOffset < previousOffset ||
           glyph.dataOffset > characterIndex)
            return fail("invalid tft glyph data offset");
        const std::size_t nextOffset = i + 1 < count
            ? readLe32(out.bytes.data() + p + kIndexRecordSize)
            : static_cast<std::size_t>(characterIndex);
        if(nextOffset < glyph.dataOffset || nextOffset > characterIndex)
            return fail("invalid tft glyph data range");
        if(!decodeAlpha(out.bytes, glyph.dataOffset, nextOffset, pixelCount,
                        glyph.alpha))
            return fail("invalid tft glyph compression");
        previousOffset = glyph.dataOffset;
    }
    return true;
}

std::uint8_t alphaTo65(std::uint8_t value) {
    return value <= 64 ? value : static_cast<std::uint8_t>(
        (static_cast<unsigned int>(value) * 64u + 127u) / 255u);
}

bool getProperty(iTJSDispatch2 *object, const tjs_char *name,
                 tTJSVariant &value) {
    return object && TJS_SUCCEEDED(object->PropGet(
        TJS_MEMBERMUSTEXIST, name, nullptr, &value, object));
}

tjs_int propertyInt(iTJSDispatch2 *object, const tjs_char *name,
                    tjs_int fallback, bool *present = nullptr) {
    tTJSVariant value;
    const bool found = getProperty(object, name, value) &&
        value.Type() != tvtVoid;
    if(present)
        *present = found;
    return found ? static_cast<tjs_int>(value) : fallback;
}

std::int16_t clampS16(tjs_int value) {
    return static_cast<std::int16_t>(std::clamp(
        value, static_cast<tjs_int>(std::numeric_limits<std::int16_t>::min()),
        static_cast<tjs_int>(std::numeric_limits<std::int16_t>::max())));
}

bool copyLayerAlpha(tTJSNI_BaseLayer *layer, std::uint16_t width,
                    std::uint16_t height, std::vector<std::uint8_t> &alpha) {
    if(!layer)
        return false;
    tTVPBaseTexture *image = layer->GetMainImage();
    if(!image || !image->Is32BPP() || image->GetWidth() == 0 ||
       image->GetHeight() == 0)
        return false;
    const std::size_t expected = static_cast<std::size_t>(width) * height;
    alpha.assign(expected, 0);
    const tjs_int imageWidth = static_cast<tjs_int>(image->GetWidth());
    const tjs_int imageHeight = static_cast<tjs_int>(image->GetHeight());
    const tjs_int copyWidth = std::min<tjs_int>(width, imageWidth);
    const tjs_int copyHeight = std::min<tjs_int>(height, imageHeight);
    for(tjs_int y = 0; y < copyHeight; ++y) {
        const auto *row = static_cast<const std::uint32_t *>(
            image->GetScanLine(static_cast<tjs_uint>(y)));
        if(!row)
            continue;
        for(tjs_int x = 0; x < copyWidth; ++x)
            alpha[static_cast<std::size_t>(y) * width + x] = alphaTo65(
                static_cast<std::uint8_t>(row[x] >> 24));
    }
    return true;
}

bool copyOctetAlpha(iTJSDispatch2 *object, std::uint16_t width,
                    std::uint16_t height, std::vector<std::uint8_t> &alpha) {
    tTJSVariant image;
    if(!getProperty(object, TJS_W("image"), image) ||
       image.Type() != tvtOctet)
        return false;
    const auto *octet = image.AsOctetNoAddRef();
    if(!octet || !octet->GetData())
        return false;
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    const std::size_t length = octet->GetLength();
    if(length != pixels && length != pixels * 4u)
        return false;
    alpha.resize(pixels);
    const auto *data = octet->GetData();
    if(length == pixels) {
        for(std::size_t i = 0; i < pixels; ++i)
            alpha[i] = alphaTo65(data[i]);
    } else {
        for(std::size_t i = 0; i < pixels; ++i)
            alpha[i] = alphaTo65(data[i * 4u + 3u]);
    }
    return true;
}

bool extractGlyph(iTJSDispatch2 *object, std::uint16_t code,
                  GlyphRecord &glyph) {
    if(!object)
        return false;
    bool widthPresent = false;
    bool heightPresent = false;
    const tjs_int width = propertyInt(object, TJS_W("blackbox_x"), 0,
                                      &widthPresent);
    const tjs_int height = propertyInt(object, TJS_W("blackbox_y"), 0,
                                       &heightPresent);
    if(width < 0 || height < 0 || width > 65535 || height > 65535)
        return false;

    tTJSNI_BaseLayer *layer = nullptr;
    if(TJS_SUCCEEDED(object->NativeInstanceSupport(
           TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
           reinterpret_cast<iTJSNativeInstance **>(&layer))) && layer) {
        tTVPBaseTexture *image = layer->GetMainImage();
        const std::uint16_t imageWidth = image
            ? static_cast<std::uint16_t>(std::min<tjs_uint>(image->GetWidth(), 65535u))
            : 0;
        const std::uint16_t imageHeight = image
            ? static_cast<std::uint16_t>(std::min<tjs_uint>(image->GetHeight(), 65535u))
            : 0;
        const std::uint16_t finalWidth = widthPresent
            ? static_cast<std::uint16_t>(width) : imageWidth;
        const std::uint16_t finalHeight = heightPresent
            ? static_cast<std::uint16_t>(height) : imageHeight;
        glyph.width = finalWidth;
        glyph.height = finalHeight;
        if(!copyLayerAlpha(layer, finalWidth, finalHeight, glyph.alpha))
            return false;
    } else {
        glyph.width = static_cast<std::uint16_t>(width);
        glyph.height = static_cast<std::uint16_t>(height);
        if(!copyOctetAlpha(object, glyph.width, glyph.height, glyph.alpha) &&
           !glyph.alpha.empty())
            return false;
        if(glyph.width != 0 && glyph.height != 0 && glyph.alpha.empty())
            return false;
    }

    glyph.code = code;
    glyph.originX = clampS16(propertyInt(object, TJS_W("origin_x"), 0));
    glyph.originY = clampS16(propertyInt(object, TJS_W("origin_y"), 0));
    glyph.incX = clampS16(propertyInt(object, TJS_W("inc_x"), glyph.width));
    glyph.incY = clampS16(propertyInt(object, TJS_W("inc_y"), 0));
    glyph.inc = clampS16(propertyInt(object, TJS_W("inc"), glyph.incX));
    return true;
}

bool getCharacters(const tTJSVariant &value, std::vector<std::uint16_t> &codes) {
    codes.clear();
    if(value.Type() != tvtObject || !value.AsObjectNoAddRef())
        return false;
    iTJSDispatch2 *array = value.AsObjectNoAddRef();
    const tjs_int count = TJSGetArrayElementCount(array);
    if(count <= 0 || static_cast<std::size_t>(count) > kMaxGlyphs)
        return false;
    std::unordered_set<std::uint16_t> seen;
    for(tjs_int i = 0; i < count; ++i) {
        tTJSVariant item;
        if(TJS_FAILED(array->PropGetByNum(TJS_IGNOREPROP, i, &item, array)))
            return false;
        const tjs_int64 code = static_cast<tjs_int64>(item);
        if(code < 0 || code > 0xffff)
            return false;
        if(seen.insert(static_cast<std::uint16_t>(code)).second)
            codes.push_back(static_cast<std::uint16_t>(code));
    }
    std::sort(codes.begin(), codes.end());
    return !codes.empty();
}

void setCharacters(iTJSDispatch2 *array, const std::vector<GlyphRecord> &glyphs) {
    if(!array)
        return;
    for(tjs_int i = 0; i < static_cast<tjs_int>(glyphs.size()); ++i) {
        tTJSVariant code(static_cast<tjs_int>(glyphs[static_cast<std::size_t>(i)].code));
        array->PropSetByNum(TJS_MEMBERENSURE, i, &code, array);
    }
}

void setDict(iTJSDispatch2 *dict, const tjs_char *name,
             const tTJSVariant &value) {
    if(dict)
        dict->PropSet(TJS_MEMBERENSURE, name, nullptr, &value, dict);
}

tTJSVariant glyphInfoVariant(const GlyphRecord &glyph) {
    iTJSDispatch2 *dict = TJSCreateDictionaryObject();
    if(!dict)
        return tTJSVariant();
    setDict(dict, TJS_W("blackbox_x"), tTJSVariant(glyph.width));
    setDict(dict, TJS_W("blackbox_y"), tTJSVariant(glyph.height));
    setDict(dict, TJS_W("origin_x"), tTJSVariant(glyph.originX));
    setDict(dict, TJS_W("origin_y"), tTJSVariant(glyph.originY));
    setDict(dict, TJS_W("inc_x"), tTJSVariant(glyph.incX));
    setDict(dict, TJS_W("inc_y"), tTJSVariant(glyph.incY));
    setDict(dict, TJS_W("inc"), tTJSVariant(glyph.inc));
    tTJSVariantOctet *octet = TJSAllocVariantOctet(
        glyph.alpha.empty() ? nullptr : glyph.alpha.data(),
        static_cast<tjs_uint>(glyph.alpha.size()));
    if(octet) {
        tTJSVariant image;
        image = octet;
        setDict(dict, TJS_W("image"), image);
        octet->Release();
    }
    tTJSVariant result(dict, dict);
    dict->Release();
    return result;
}

bool callCallback(const tTJSVariantClosure &closure, std::uint16_t code,
                  const tTJSVariant *info, tTJSVariant &result) {
    tTJSVariant codeValue(static_cast<tjs_int>(code));
    tTJSVariant *params[2] = {&codeValue, const_cast<tTJSVariant *>(info)};
    const tjs_int count = info ? 2 : 1;
    try {
        return TJS_SUCCEEDED(closure.FuncCall(0, nullptr, nullptr, &result,
                                              count, params, nullptr));
    } catch(...) {
        return false;
    }
}

bool buildFont(const std::vector<GlyphRecord> &glyphs,
               std::vector<std::uint8_t> &bytes) {
    if(glyphs.empty() || glyphs.size() > kMaxGlyphs)
        return false;
    bytes.assign(kFixedPrefixSize, 0);
    std::copy(kHeader.begin(), kHeader.end(), bytes.begin());
    std::vector<std::vector<std::uint8_t>> compressed(glyphs.size());
    for(std::size_t i = 0; i < glyphs.size(); ++i) {
        const auto expected = static_cast<std::size_t>(glyphs[i].width) *
            glyphs[i].height;
        if(glyphs[i].alpha.size() != expected || expected > kMaxGlyphPixels)
            return false;
        encodeAlpha(glyphs[i].alpha, compressed[i]);
        if(bytes.size() > kMaxFileBytes - compressed[i].size())
            return false;
        bytes.insert(bytes.end(), compressed[i].begin(), compressed[i].end());
    }
    bytes.resize(align4(bytes.size()), 0);
    const std::uint32_t characterIndex = static_cast<std::uint32_t>(bytes.size());
    for(const auto &glyph : glyphs)
        appendLe16(bytes, glyph.code);
    bytes.resize(align4(bytes.size()), 0);
    const std::uint32_t recordIndex = static_cast<std::uint32_t>(bytes.size());
    std::uint32_t dataOffset = static_cast<std::uint32_t>(kFixedPrefixSize);
    for(std::size_t i = 0; i < glyphs.size(); ++i) {
        appendLe32(bytes, dataOffset);
        appendLe16(bytes, glyphs[i].width);
        appendLe16(bytes, glyphs[i].height);
        appendLe16(bytes, static_cast<std::uint16_t>(glyphs[i].originX));
        appendLe16(bytes, static_cast<std::uint16_t>(glyphs[i].originY));
        appendLe16(bytes, static_cast<std::uint16_t>(glyphs[i].incX));
        appendLe16(bytes, static_cast<std::uint16_t>(glyphs[i].incY));
        appendLe16(bytes, static_cast<std::uint16_t>(glyphs[i].inc));
        appendLe16(bytes, 0);
        dataOffset += static_cast<std::uint32_t>(compressed[i].size());
    }
    storeLe32(bytes, kHeaderSize, static_cast<std::uint32_t>(glyphs.size()));
    storeLe32(bytes, kHeaderSize + 4, characterIndex);
    storeLe32(bytes, kHeaderSize + 8, recordIndex);
    return bytes.size() <= kMaxFileBytes;
}

void logError(const char *message) {
    TVPAddLog(ttstr(TJS_W("AetherKiri tftSave: ")) +
              ttstr(message ? message : "operation failed"));
}

tjs_error savePreRenderedFontCb(tTJSVariant *result, tjs_int numparams,
                                tTJSVariant **param, iTJSDispatch2 *) {
    if(numparams < 3 || !param || !param[0] || !param[1] || !param[2] ||
       param[2]->Type() != tvtObject)
        return TJS_E_BADPARAMCOUNT;
    std::vector<std::uint16_t> codes;
    if(!getCharacters(*param[1], codes)) {
        logError("characters must be a non-empty array of UTF-16 codes");
        return TJS_E_FAIL;
    }
    const tTJSVariantClosure callback = param[2]->AsObjectClosureNoAddRef();
    std::vector<GlyphRecord> glyphs;
    glyphs.reserve(codes.size());
    for(const auto code : codes) {
        tTJSVariant callbackResult;
        if(!callCallback(callback, code, nullptr, callbackResult) ||
           callbackResult.Type() != tvtObject ||
           !extractGlyph(callbackResult.AsObjectNoAddRef(), code,
                         glyphs.emplace_back())) {
            glyphs.clear();
            logError("glyph callback returned an invalid image or metric set");
            return TJS_E_FAIL;
        }
    }
    std::vector<std::uint8_t> encoded;
    if(!buildFont(glyphs, encoded) ||
       !writeStorage(param[0]->AsStringNoAddRef(), encoded)) {
        logError("could not write the pre-rendered font storage");
        return TJS_E_FAIL;
    }
    if(result)
        *result = true;
    return TJS_S_OK;
}

tjs_error loadPreRenderedFontCb(tTJSVariant *result, tjs_int numparams,
                                tTJSVariant **param, iTJSDispatch2 *) {
    if(numparams < 2 || !param || !param[0] || !param[1] ||
       param[1]->Type() != tvtObject)
        return TJS_E_BADPARAMCOUNT;
    std::vector<std::uint8_t> bytes;
    FontFile file;
    std::string error;
    if(!readStorage(param[0]->AsStringNoAddRef(), bytes) ||
       !parseFont(std::move(bytes), file, &error)) {
        logError(error.empty() ? "could not read the pre-rendered font storage"
                               : error.c_str());
        return TJS_E_FAIL;
    }
    setCharacters(param[1]->AsObjectNoAddRef(), file.glyphs);
    if(numparams > 2 && param[2] && param[2]->Type() == tvtObject) {
        const tTJSVariantClosure callback = param[2]->AsObjectClosureNoAddRef();
        for(const auto &glyph : file.glyphs) {
            const tTJSVariant info = glyphInfoVariant(glyph);
            tTJSVariant callbackResult;
            if(!callCallback(callback, glyph.code, &info, callbackResult)) {
                logError("pre-rendered font load callback failed");
                return TJS_E_FAIL;
            }
        }
    }
    if(result)
        *result = true;
    return TJS_S_OK;
}

tjs_error modifyPreRenderedFontCb(tTJSVariant *result, tjs_int numparams,
                                  tTJSVariant **param, iTJSDispatch2 *) {
    if(numparams < 2 || !param || !param[0] || !param[1] ||
       param[1]->Type() != tvtObject)
        return TJS_E_BADPARAMCOUNT;
    std::vector<std::uint8_t> bytes;
    FontFile file;
    std::string error;
    if(!readStorage(param[0]->AsStringNoAddRef(), bytes) ||
       !parseFont(std::move(bytes), file, &error)) {
        logError(error.empty() ? "could not read the pre-rendered font storage"
                               : error.c_str());
        return TJS_E_FAIL;
    }
    const tTJSVariantClosure callback = param[1]->AsObjectClosureNoAddRef();
    for(auto &glyph : file.glyphs) {
        const tTJSVariant info = glyphInfoVariant(glyph);
        tTJSVariant callbackResult;
        if(!callCallback(callback, glyph.code, &info, callbackResult)) {
            logError("pre-rendered font modify callback failed");
            return TJS_E_FAIL;
        }
        if(!static_cast<bool>(callbackResult))
            continue;
        const tjs_int width = propertyInt(info.AsObjectNoAddRef(),
                                          TJS_W("blackbox_x"), glyph.width);
        const tjs_int height = propertyInt(info.AsObjectNoAddRef(),
                                           TJS_W("blackbox_y"), glyph.height);
        if(width != glyph.width || height != glyph.height) {
            logError("blackbox dimensions cannot be changed");
            return TJS_E_FAIL;
        }
        glyph.originX = clampS16(propertyInt(info.AsObjectNoAddRef(),
                                             TJS_W("origin_x"), glyph.originX));
        glyph.originY = clampS16(propertyInt(info.AsObjectNoAddRef(),
                                             TJS_W("origin_y"), glyph.originY));
        glyph.incX = clampS16(propertyInt(info.AsObjectNoAddRef(),
                                          TJS_W("inc_x"), glyph.incX));
        glyph.incY = clampS16(propertyInt(info.AsObjectNoAddRef(),
                                          TJS_W("inc_y"), glyph.incY));
        glyph.inc = clampS16(propertyInt(info.AsObjectNoAddRef(),
                                         TJS_W("inc"), glyph.inc));
    }

    // Rebuild rather than patching host-endian records in place.  This also
    // canonicalizes files produced by old Windows builds and keeps offsets
    // bounded after a malformed/truncated input has been rejected.
    if(!buildFont(file.glyphs, file.bytes) ||
       !writeStorage(param[0]->AsStringNoAddRef(), file.bytes)) {
        logError("could not write modified pre-rendered font storage");
        return TJS_E_FAIL;
    }
    if(result)
        *result = true;
    return TJS_S_OK;
}

} // namespace

#define NCB_MODULE_NAME TJS_W("tftSave.dll")
NCB_ATTACH_FUNCTION(savePreRenderedFont, System, savePreRenderedFontCb);
NCB_ATTACH_FUNCTION(loadPreRenderedFont, System, loadPreRenderedFontCb);
NCB_ATTACH_FUNCTION(modifyPreRenderedFont, System, modifyPreRenderedFontCb);
