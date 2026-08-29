#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "FontVariations.h"
#include "LayerIntf.h"

TEST_CASE("krkrz variable-font specs are normalized through the shared parser") {
    CHECK(TVPNormalizeFontVariations(
              TJS_W(" Wght = 700.4, wdth=87.6, Wght=701.2 ")) ==
          ttstr(TJS_W("wdth=87.5,wght=701")));

    std::vector<tTVPFontAxisCoord> coords;
    TVPFontGetEffectiveVarCoords(650, TJS_W("wdth=88"), coords);
    REQUIRE(coords.size() == 2);
    const tjs_uint32 wdth = TVPFontVarPackTag("wdth", 4);
    const tjs_uint32 wght = TVPFontVarPackTag("wght", 4);
    CHECK(coords[0].first == wdth);
    CHECK(coords[0].second == Catch::Approx(88.0f));
    CHECK(coords[1].first == wght);
    CHECK(coords[1].second == Catch::Approx(650.0f));
}

TEST_CASE("invalid variable-font tokens fail at the script boundary") {
    REQUIRE_THROWS(TVPNormalizeFontVariations(TJS_W("wght")));
    REQUIRE_THROWS(TVPNormalizeFontVariations(TJS_W("wght=700,toolong=1")));
}

TEST_CASE("Font exposes weight and variations without replacing the Font ABI") {
    iTJSDispatch2 *fontClass = TVPCreateNativeClass_Font();
    REQUIRE(fontClass != nullptr);

    tTJSVariant defaultMode;
    REQUIRE(TJS_SUCCEEDED(fontClass->PropGet(
        0, TJS_W("defaultEmojiMode"), nullptr, &defaultMode, fontClass)));
    CHECK(static_cast<tjs_int>(defaultMode) >= TVP_EMOJI_NONE);
    CHECK(static_cast<tjs_int>(defaultMode) <= TVP_EMOJI_COLOR);

    tTJSVariant defaultUseVarStyle;
    REQUIRE(TJS_SUCCEEDED(fontClass->PropGet(
        0, TJS_W("defaultUseVarStyle"), nullptr, &defaultUseVarStyle,
        fontClass)));
    CHECK(defaultUseVarStyle.Type() == tvtInteger);

    // weight, variations and emojiMode are instance properties.  Probe them
    // on a real Font object so this test catches registration/ABI regressions
    // without confusing instance members with class-level policy.
    iTJSDispatch2 *fontObject = nullptr;
    REQUIRE(TJS_SUCCEEDED(fontClass->CreateNew(
        0, nullptr, nullptr, &fontObject, 0, nullptr, fontClass)));
    REQUIRE(fontObject != nullptr);

    for(const auto *name :
        {TJS_W("weight"), TJS_W("variations"), TJS_W("emojiMode")}) {
        tTJSVariant property;
        INFO(ttstr(name).AsStdString());
        REQUIRE(TJS_SUCCEEDED(
            fontObject->PropGet(0, name, nullptr, &property, fontObject)));
    }

    tTJSVariant weight(700);
    REQUIRE(TJS_SUCCEEDED(fontObject->PropSet(
        TJS_MEMBERENSURE, TJS_W("weight"), nullptr, &weight, fontObject)));
    tTJSVariant weightReadback;
    REQUIRE(TJS_SUCCEEDED(fontObject->PropGet(
        0, TJS_W("weight"), nullptr, &weightReadback, fontObject)));
    CHECK(static_cast<tjs_int>(weightReadback) == 700);

    tTJSVariant variations(TJS_W(" Wght=700.4, wdth=87.6, Wght=701.2 "));
    REQUIRE(TJS_SUCCEEDED(fontObject->PropSet(
        TJS_MEMBERENSURE, TJS_W("variations"), nullptr, &variations,
        fontObject)));
    tTJSVariant variationsReadback;
    REQUIRE(TJS_SUCCEEDED(fontObject->PropGet(
        0, TJS_W("variations"), nullptr, &variationsReadback, fontObject)));
    CHECK(ttstr(variationsReadback) == TJS_W("wdth=87.5,wght=701"));

    fontObject->Release();

    fontClass->Release();
}
