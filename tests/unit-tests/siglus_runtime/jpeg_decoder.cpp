#include <catch2/catch_test_macros.hpp>
#include "siglus_jpeg_decoder.h"
#include <turbojpeg.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <vector>

namespace {
    std::vector<uint8_t> MakeJpeg() {
        std::array<uint8_t, 8 * 8 * 3> rgb{};
        for(size_t y = 0; y < 8; ++y) for(size_t x = 0; x < 8; ++x) {
            const auto i = (y * 8 + x) * 3;
            rgb[i] = static_cast<uint8_t>(30 + y * 25);
            rgb[i + 1] = static_cast<uint8_t>(40 + x * 25);
            rgb[i + 2] = 100;
        }
        auto compressor = tjInitCompress();
        REQUIRE(compressor);
        unsigned char *encoded = nullptr;
        unsigned long size = 0;
        const int status = tjCompress2(compressor, rgb.data(), 8, 0, 8, TJPF_RGB,
            &encoded, &size, TJSAMP_444, 100, TJFLAG_ACCURATEDCT);
        tjDestroy(compressor);
        REQUIRE(status == 0);
        std::vector<uint8_t> jpeg(encoded, encoded + size);
        tjFree(encoded);
        return jpeg;
    }
}

TEST_CASE("Siglus host JPEG emits top-down RGBA with opaque alpha") {
    const auto jpeg = MakeJpeg();
    std::array<uint8_t, 8 * 8 * 4 + 16> pixels{};
    pixels.fill(0xa5);
    REQUIRE(aetherkiri::siglus::DecodeJpeg(jpeg.data(), jpeg.size(), 8, 8,
        pixels.data(), 8 * 8 * 4) == 0);
    for(size_t y = 0; y < 8; ++y) for(size_t x = 0; x < 8; ++x) {
        const auto i = (y * 8 + x) * 4;
        CHECK(std::abs(int(pixels[i]) - int(30 + y * 25)) <= 3);
        CHECK(std::abs(int(pixels[i + 1]) - int(40 + x * 25)) <= 3);
        CHECK(std::abs(int(pixels[i + 2]) - 100) <= 3);
        CHECK(pixels[i + 3] == 255);
    }
    CHECK(std::all_of(pixels.begin() + 256, pixels.end(), [](auto b) { return b == 0xa5; }));
}

TEST_CASE("Siglus host JPEG rejects unsafe dimensions and buffer lengths before writing") {
    const auto jpeg = MakeJpeg();
    std::array<uint8_t, 8 * 8 * 4> pixels{};
    pixels.fill(0xa5);
    using aetherkiri::siglus::DecodeJpeg;
    CHECK(DecodeJpeg(jpeg.data(), jpeg.size(), 8, 8, pixels.data(), pixels.size() - 1) != 0);
    CHECK(DecodeJpeg(jpeg.data(), jpeg.size(), 16, 4, pixels.data(), pixels.size()) != 0);
    CHECK(DecodeJpeg(jpeg.data(), jpeg.size(), UINT32_MAX, UINT32_MAX,
        pixels.data(), std::numeric_limits<size_t>::max()) != 0);
    CHECK(DecodeJpeg(jpeg.data(), jpeg.size(), 0, 8, pixels.data(), 0) != 0);
    CHECK(DecodeJpeg(nullptr, jpeg.size(), 8, 8, pixels.data(), pixels.size()) != 0);
    CHECK(DecodeJpeg(jpeg.data(), jpeg.size(), 8, 8, nullptr, pixels.size()) != 0);
    CHECK(DecodeJpeg(jpeg.data(), 10, 8, 8, pixels.data(), pixels.size()) != 0);
    CHECK(std::all_of(pixels.begin(), pixels.end(), [](auto b) { return b == 0xa5; }));
}
