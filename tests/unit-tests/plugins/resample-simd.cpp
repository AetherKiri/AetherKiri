#include <catch2/catch_test_macros.hpp>

#include "visual/upstream_bridge/ResampleImageSIMD.h"

TEST_CASE("krkrz resampling dispatch preserves scalar-only modes") {
    tTVPResampleClipping clip{};
    clip.offsetx_ = 0;
    clip.offsety_ = 0;
    clip.width_ = 1;
    clip.height_ = 1;
    clip.dst_left_ = 0;
    clip.dst_top_ = 0;

    // Nearest-neighbour is intentionally outside the krkrz SIMD leaf.  The
    // dispatch must return false before touching bitmap pointers, allowing
    // the existing Aether scalar switch to handle every bitmap owner.
    TVPInitResampleSIMD();
    CHECK_FALSE(TVPResampleImageSIMD(
        clip, nullptr, nullptr, tTVPRect(0, 0, 1, 1), nullptr,
        tTVPRect(0, 0, 1, 1), stNearest, 0.0));

    // The upstream switch has no stFastLinear case.  It must remain on the
    // scalar Aether implementation instead of throwing from the SIMD leaf.
    CHECK_FALSE(TVPResampleImageSIMD(
        clip, nullptr, nullptr, tTVPRect(0, 0, 1, 1), nullptr,
        tTVPRect(0, 0, 1, 1), stFastLinear, 0.0));

    // Flags are accepted by the public enum, but are stripped only after a
    // valid bitmap pair is available.  A null pair still returns safely.
    CHECK_FALSE(TVPResampleImageSIMD(
        clip, nullptr, nullptr, tTVPRect(0, 0, 1, 1), nullptr,
        tTVPRect(0, 0, 1, 1),
        static_cast<tTVPBBStretchType>(stLinear | stRefNoClip), 0.0));
}
