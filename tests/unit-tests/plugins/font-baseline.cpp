#include <catch2/catch_test_macros.hpp>

#include "FontBaseline.h"

TEST_CASE("FreeType line baseline is clamped with face-wide metrics") {
    // Source Han-style metrics have an ascender larger than the em square.
    // The face descent reserves room in the 24 px KAG line box.
    CHECK(krkr::font::ComputeLineBaseline(24, 1160, -288, 24, 1000) == 18);

    // Metrics that already fit retain their natural baseline.
    CHECK(krkr::font::ComputeLineBaseline(24, 800, -200, 24, 1000) == 19);
}

TEST_CASE("FreeType line baseline handles invalid line and face metrics") {
    CHECK(krkr::font::ComputeLineBaseline(0, 1160, -288, 24, 1000) == 0);
    CHECK(krkr::font::ComputeLineBaseline(24, 1160, -288, 24, 0) == 0);
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

TEST_CASE("top-aligned text keeps negative ink and outlines inside its clip") {
    CHECK(krkr::font::ClampTextOriginToClipTop(0, -9, 1, 0) == 10);
    CHECK(krkr::font::ClampTextOriginToClipTop(4, -2, 1, 0) == 4);
    CHECK(krkr::font::ClampTextOriginToClipTop(0, 0, 1, 0) == 1);
    CHECK(krkr::font::ClampTextOriginToClipTop(8, -4, 0, 3) == 8);
}
