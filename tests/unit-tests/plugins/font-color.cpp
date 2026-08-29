#include <catch2/catch_test_macros.hpp>

#include "CharacterData.h"
#include "FreeTypeColor.h"

#include <array>

TEST_CASE("FreeType premultiplied BGRA is converted to straight RGBA") {
    const std::array<tjs_uint8, 4> halfRed{{0, 0, 128, 128}};
    std::array<tjs_uint8, 4> converted{};
    krkr::font::ConvertFreeTypeBGRAPixel(halfRed.data(), converted.data());
    CHECK(converted == std::array<tjs_uint8, 4>{{255, 0, 0, 128}});

    const std::array<tjs_uint8, 4> opaque{{7, 23, 41, 255}};
    krkr::font::ConvertFreeTypeBGRAPixel(opaque.data(), converted.data());
    CHECK(converted == std::array<tjs_uint8, 4>{{41, 23, 7, 255}});

    const std::array<tjs_uint8, 4> transparent{{255, 127, 3, 0}};
    krkr::font::ConvertFreeTypeBGRAPixel(transparent.data(), converted.data());
    CHECK(converted == std::array<tjs_uint8, 4>{{0, 0, 0, 0}});
}

TEST_CASE("color glyph shadows are reduced to an alpha mask") {
    const std::array<tjs_uint8, 8> rgba{{12, 34, 56, 78, 90, 123, 210, 200}};
    const tGlyphMetrics metrics{7, 0};
    auto *color = new tTVPCharacterData(rgba.data(), 2, -1, 3, 2, 1,
                                         metrics, true);
    auto *mask = color->CreateAlphaMask();
    REQUIRE(mask != nullptr);
    CHECK_FALSE(mask->FullColored);
    CHECK(mask->Gray == 256);
    CHECK(mask->OriginX == -1);
    CHECK(mask->OriginY == 3);
    CHECK(mask->Metrics.CellIncX == 7);
    CHECK(mask->GetData()[0] == 78);
    CHECK(mask->GetData()[1] == 200);
    mask->Release();
    color->Release();
}
