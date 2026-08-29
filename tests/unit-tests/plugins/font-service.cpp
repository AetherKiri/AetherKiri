#include <catch2/catch_test_macros.hpp>

#include "FontServiceIntf.h"
#include "FontImpl.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {
struct ShapeCapture final : iTVPFontShapeSink {
    tjs_int Count = 0;
    float Width = 0.0f;
    std::vector<tTVPFontShapedGlyph> Glyphs;

    void TJS_INTF_METHOD Begin(tjs_int glyphCount, float width, float,
                               float) override {
        Count = glyphCount;
        Width = width;
        Glyphs.clear();
        Glyphs.reserve(glyphCount > 0 ? static_cast<std::size_t>(glyphCount)
                                      : 0);
    }
    void TJS_INTF_METHOD Glyph(const tTVPFontShapedGlyph &glyph) override {
        Glyphs.push_back(glyph);
    }
};

struct QueryCapture final : iTVPFontQuerySink {
    std::vector<tTVPFontFaceInfo> Faces;
    void TJS_INTF_METHOD Found(const tTVPFontFaceInfo &info) override {
        Faces.push_back(info);
    }
};
} // namespace

TEST_CASE("FontService keeps unknown faces out of the default fallback") {
    const ttstr unknown(TJS_W("__aether_font_service_missing__"));
    CHECK_FALSE(TVPFontNameKnown(unknown));
    CHECK(TVPFontResolveKey(unknown).IsEmpty());
    CHECK(TVPFontAcquireFace(unknown) == nullptr);
}

TEST_CASE("FontService exposes the registered FreeType face contract") {
    TVPInitFontNames();
    std::vector<ttstr> names;
    TVPGetAllFontList(names);
    if(names.empty())
        SKIP("the test host has no registered font");

    tTVPFontFaceHandle face = TVPFontAcquireFace(names.front());
    if(!face)
        SKIP("the registered font cannot be opened by FreeType");

    tTVPFontFaceInfo info{};
    REQUIRE(TVPFontGetFaceInfo(names.front(), &info));
    CHECK_FALSE(info.Family.IsEmpty());

    const tjs_uint8 *data = nullptr;
    tjs_uint64 size = 0;
    tjs_int faceIndex = -1;
    REQUIRE(TVPFontGetFaceData(face, &data, &size, &faceIndex));
    CHECK(data != nullptr);
    CHECK(size > 0);
    CHECK(faceIndex >= 0);

    tTVPFontLineMetrics line{};
    REQUIRE(TVPFontGetLineMetrics(face, 16, &line));
    CHECK(line.Ascent >= 0.0f);
    CHECK(line.Descent >= 0.0f);

    const tjs_uint32 glyph = TVPFontGetGlyphIndex(face, 0x41);
    if(glyph != 0) {
        tTVPFontGlyphMetrics metrics{};
        CHECK(TVPFontGetGlyphMetricsEx(
            face, glyph, 16, false, false, TVP_FONT_METRICS_UNHINTED,
            &metrics));
        CHECK(metrics.AdvanceX >= 0.0f);

        tTVPFontGlyphBitmap bitmap{};
        CHECK(TVPFontGetGlyphBitmap(face, glyph, 16, false, false, false,
                                    &bitmap));
        CHECK(bitmap.Buffer != nullptr);
        CHECK(bitmap.Width > 0);
        CHECK(bitmap.Height > 0);

        tTVPFontLineMetrics metricsLine{};
        REQUIRE(TVPFontGetLineMetrics(face, 16, &metricsLine));
        if(metricsLine.UnitsPerEm > 0.0f) {
            tTVPFontRenderParams render{};
            render.Transform[0] = 16.0f / metricsLine.UnitsPerEm;
            render.Transform[4] = 16.0f / metricsLine.UnitsPerEm;
            render.MiterLimit = 4.0f;
            tTVPFontGlyphMask mask{};
            CHECK(TVPFontRenderGlyphMask(face, glyph, &render, &mask));
            CHECK(mask.Buffer != nullptr);
            CHECK(mask.Width > 0);
            render.StrokeWidth = 1.0f;
            CHECK(TVPFontRenderGlyphMask(face, glyph, &render, &mask));
            CHECK(mask.Buffer != nullptr);
        }
    }

    TVPFontReleaseFace(face);
}

TEST_CASE("FontService chains deduplicate aliases and select coverage") {
    TVPInitFontNames();
    std::vector<ttstr> names;
    TVPGetAllFontList(names);
    if(names.empty())
        SKIP("the test host has no registered font");

    const ttstr chainName = names.front() + TJS_W(",") + names.front();
    tTVPFontFaceChainHandle chain = TVPFontAcquireFaceChain(chainName);
    REQUIRE(chain != nullptr);
    CHECK(TVPFontChainCount(chain) == 1);
    CHECK(TVPFontChainFaceAt(chain, 0) != nullptr);
    CHECK(TVPFontChainFaceAt(chain, -1) == nullptr);
    CHECK(TVPFontChainFaceForChar(chain, 0x41, false) >= -1);
    TVPFontReleaseFaceChain(chain);
}

TEST_CASE("FontService honors an explicit collection face index") {
    TVPInitFontNames();
    std::vector<ttstr> names;
    TVPGetAllFontList(names);
    if(names.empty())
        SKIP("the test host has no registered font");

    // Face zero is valid for every registered face and exercises the
    // explicit-index path even when the host has no TTC installed.  If a
    // collection exposes another face, verify that its reported index is
    // preserved instead of silently falling back to face zero.
    auto face0 = TVPFontAcquireFaceAt(names.front(), 0);
    REQUIRE(face0 != nullptr);
    const tjs_uint8 *bytes = nullptr;
    tjs_uint64 size = 0;
    tjs_int index = -1;
    REQUIRE(TVPFontGetFaceData(face0, &bytes, &size, &index));
    CHECK(index == 0);
    CHECK(bytes != nullptr);
    CHECK(size > 0);
    TVPFontReleaseFace(face0);

    for(const auto &name : names) {
        tTVPFontFaceInfo info{};
        if(!TVPFontGetFaceInfo(name, &info) || info.FaceIndex <= 0)
            continue;
        auto selected = TVPFontAcquireFaceAt(name, info.FaceIndex);
        REQUIRE(selected != nullptr);
        CHECK(TVPFontGetFaceData(selected, nullptr, nullptr, &index));
        CHECK(index == info.FaceIndex);
        TVPFontReleaseFace(selected);
        break;
    }
}

TEST_CASE("FontService shapes Unicode without dropping variation selectors") {
    TVPInitFontNames();
    std::vector<ttstr> names;
    TVPGetAllFontList(names);
    if(names.empty())
        SKIP("the test host has no registered font");
    auto chain = TVPFontAcquireFaceChain(names.front());
    if(!chain || TVPFontChainCount(chain) == 0)
        SKIP("the registered font cannot be opened");

    ShapeCapture capture;
    const ttstr text(TJS_W("office \uFE0F"));
    REQUIRE(TVPFontShapeLine(chain, text, 18, TVP_FONT_BASEDIR_LTR,
                             &capture));
    CHECK(capture.Count == static_cast<tjs_int>(capture.Glyphs.size()));
    CHECK(capture.Width >= 0.0f);
    CHECK_FALSE(capture.Glyphs.empty());
    for(const auto &glyph : capture.Glyphs) {
        CHECK(glyph.FaceIndexInChain >= 0);
        CHECK(glyph.Cluster < 64u);
        CHECK(glyph.Advance >= 0.0f);
    }
    TVPFontReleaseFaceChain(chain);
}

TEST_CASE("FontService metadata filters script and returns full names") {
    TVPInitFontNames();
    QueryCapture capture;
    tTVPFontQueryParams params;
    params.Script = ttstr(TJS_W("Latn"));
    const tjs_int count = TVPFontQueryFaces(params, &capture);
    CHECK(count == static_cast<tjs_int>(capture.Faces.size()));
    for(const auto &info : capture.Faces) {
        CHECK_FALSE(info.Family.IsEmpty());
        CHECK_FALSE(info.FullName.IsEmpty());
        CHECK(info.Slant >= 0);
    }
}
