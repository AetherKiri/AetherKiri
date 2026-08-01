#include <catch2/catch_test_macros.hpp>

#include "tjsTypes.h"

#include <array>

extern "C" void TVPFillMask_hwy(tjs_uint32 *dest, tjs_int len,
                                 tjs_uint32 mask);

TEST_CASE("SIMD FillMask expands 8-bit opacity into the alpha byte") {
    std::array<tjs_uint32, 19> pixels{};
    for(std::size_t i = 0; i < pixels.size(); ++i)
        pixels[i] = static_cast<tjs_uint32>(0x12010203u + i);

    TVPFillMask_hwy(pixels.data(), static_cast<tjs_int>(pixels.size()), 0x7f);

    for(std::size_t i = 0; i < pixels.size(); ++i)
        CHECK(pixels[i] ==
              (0x7f000000u | ((0x00010203u + i) & 0x00ffffffu)));
}
