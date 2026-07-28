#include <catch2/catch_test_macros.hpp>

#include "FontBaseline.h"

TEST_CASE("FreeType bitmap baseline fits the rendered glyph") {
    // A Source Han-style Japanese name glyph has no descender. Reserving the
    // face descent would set the baseline to 18 and clip a 23 px bearing at
    // the top of a 24 px message layer. Its own baseline is 23 instead.
    CHECK(krkr::font::ComputeGlyphBaseline(24, 1160, 24, 1000, 1) == 23);

    // A glyph with a 5 px descender still fits its lower edge into the same
    // logical KAG line box.
    CHECK(krkr::font::ComputeGlyphBaseline(24, 1160, 24, 1000, 5) == 19);
}

TEST_CASE("FreeType line baseline handles invalid line and face metrics") {
    CHECK(krkr::font::ComputeGlyphBaseline(0, 1160, 24, 1000, 1) == 0);
    CHECK(krkr::font::ComputeGlyphBaseline(24, 1160, 24, 0, 1) == 0);
}

TEST_CASE("fallback glyphs are aligned to the requested face baseline") {
    const int requested = krkr::font::ComputeLineBaseline(
        48, 800, -200, 48, 1000);
    const int fallback = krkr::font::ComputeLineBaseline(
        48, 1160, -288, 48, 1000);

    REQUIRE(requested != fallback);
    CHECK(fallback + krkr::font::ComputeFallbackBaselineAdjustment(
                         requested, fallback) == requested);
}

TEST_CASE("prerendered and runtime glyphs use the same baseline contract") {
    const int baseline = krkr::font::ComputeLineBaseline(
        26, 1160, -288, 26, 1000);

    // TFT OriginY and FreeType bitmap_top are both upward bearings from the
    // baseline. Equal bearings therefore resolve to the same draw position.
    const int prerenderedOriginY =
        krkr::font::ComputeGlyphOriginY(baseline, 22);
    const int runtimeOriginY =
        krkr::font::ComputeGlyphOriginY(baseline, 22);
    CHECK(prerenderedOriginY == -3);
    CHECK(runtimeOriginY == prerenderedOriginY);

    // Using the raw design ascender for only the TFT path recreates the
    // visible jump between adjacent glyphs.
    CHECK(krkr::font::ComputeGlyphOriginY(30, 22) != runtimeOriginY);
}
