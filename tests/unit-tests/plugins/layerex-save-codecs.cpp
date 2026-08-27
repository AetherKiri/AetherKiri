#include <catch2/catch_test_macros.hpp>

#include "upstream_bridge/layerExSaveCodecs.hpp"

#include <LodePNG/lodepng.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {

std::uint32_t readU32LE(const std::vector<std::uint8_t> &bytes,
                        std::size_t offset) {
    REQUIRE(offset + 4 <= bytes.size());
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

} // namespace

TEST_CASE("krkrz layerExSave PNG codec preserves Aether BGRA pixels") {
    // Two rows with deliberately different colors and an alpha edge.  The
    // extra pitch bytes verify that the adapter does not assume tight rows.
    const std::vector<std::uint8_t> bgra = {
        3, 2, 1, 255, 30, 20, 10, 128, 0, 0, 0, 0, // row 0 + padding
        60, 50, 40, 255, 90, 80, 70, 64, 0, 0, 0, 0, // row 1 + padding
    };
    std::vector<std::uint8_t> png;
    REQUIRE(aether::krkrz::layer_save::encodePng(bgra.data(), 2, 2, 12,
                                                 png));
    REQUIRE(png.size() > 24);
    const std::uint8_t signature[] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a,
                                      0x1a, 0x0a};
    CHECK(std::equal(std::begin(signature), std::end(signature), png.begin()));

    std::vector<unsigned char> decoded;
    unsigned width = 0;
    unsigned height = 0;
    REQUIRE(lexsave::lodepng::decode(decoded, width, height, png,
                                     lexsave::LCT_RGBA, 8) == 0);
    REQUIRE(width == 2);
    REQUIRE(height == 2);
    const std::vector<std::uint8_t> expected = {
        1, 2, 3, 255, 10, 20, 30, 128,
        40, 50, 60, 255, 70, 80, 90, 64,
    };
    CHECK(decoded == expected);
}

TEST_CASE("krkrz layerExSave codecs honor a negative row pitch") {
    // The pointer is the first logical row (the second physical row here),
    // matching the upstream BufRefT contract for a bottom-up image.
    const std::vector<std::uint8_t> physical = {
        60, 50, 40, 255, 90, 80, 70, 64, // logical row 1
        3,  2,  1,  255, 30, 20, 10, 128 // logical row 0
    };
    std::vector<std::uint8_t> png;
    REQUIRE(aether::krkrz::layer_save::encodePng(
        physical.data() + 8, 2, 2, -8, png));

    std::vector<unsigned char> decoded;
    unsigned width = 0;
    unsigned height = 0;
    REQUIRE(lexsave::lodepng::decode(decoded, width, height, png,
                                     lexsave::LCT_RGBA, 8) == 0);
    CHECK(width == 2);
    CHECK(height == 2);
    const std::vector<std::uint8_t> expected = {
        1, 2, 3, 255, 10, 20, 30, 128,
        40, 50, 60, 255, 70, 80, 90, 64,
    };
    CHECK(decoded == expected);
}

TEST_CASE("krkrz layerExSave TLG5 codec emits a valid block stream") {
    const std::vector<std::uint8_t> bgra = {
        3, 2, 1, 255, 30, 20, 10, 128,
        60, 50, 40, 255, 90, 80, 70, 64,
    };
    std::vector<std::uint8_t> tlg;
    REQUIRE(aether::krkrz::layer_save::encodeTlg5(bgra.data(), 2, 2, 8, tlg));
    REQUIRE(tlg.size() > 28);
    const char magic[] = "TLG5.0\x00raw\x1a\x00";
    CHECK(std::equal(magic, magic + 11, tlg.begin()));
    CHECK(tlg[11] == 4);
    CHECK(readU32LE(tlg, 12) == 2);
    CHECK(readU32LE(tlg, 16) == 2);
    CHECK(readU32LE(tlg, 20) == 4);

    constexpr std::size_t blockTableOffset = 24;
    const std::uint32_t blockSize = readU32LE(tlg, blockTableOffset);
    CHECK(blockSize > 0);
    CHECK(blockTableOffset + 4 + blockSize == tlg.size());
}

TEST_CASE("krkrz layerExSave codecs reject invalid layer geometry") {
    std::vector<std::uint8_t> output;
    const std::uint8_t pixel[4] = {0, 0, 0, 0};
    CHECK_FALSE(aether::krkrz::layer_save::encodePng(pixel, 0, 1, 4, output));
    CHECK_FALSE(aether::krkrz::layer_save::encodeTlg5(pixel, 1, 1, 2, output));
}
