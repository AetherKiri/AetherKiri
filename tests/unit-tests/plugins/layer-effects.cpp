#include <catch2/catch_test_macros.hpp>

#include "portableLayerEffects.h"

#include <array>
#include <cstdint>
#include <vector>

using AetherKiri::LayerEffects::ImageView;

TEST_CASE("legacy layer effects keep alpha and respect the selected region") {
    constexpr int width = 3;
    constexpr int height = 2;
    std::array<std::uint8_t, width * height * 4> pixels{
        10, 20, 30, 40,  50, 60, 70, 80,  90, 100, 110, 120,
        130, 140, 150, 160, 170, 180, 190, 200, 210, 220, 230, 240};
    const auto original = pixels;
    ImageView view{pixels.data(), width, height, width * 4};

    REQUIRE(AetherKiri::LayerEffects::applyInvert(view, 1, 0, 1, 2));
    CHECK(pixels[0] == original[0]);
    CHECK(pixels[1] == original[1]);
    CHECK(pixels[2] == original[2]);
    CHECK(pixels[3] == original[3]);
    CHECK(pixels[4] == static_cast<std::uint8_t>(255 - original[4]));
    CHECK(pixels[5] == static_cast<std::uint8_t>(255 - original[5]));
    CHECK(pixels[6] == static_cast<std::uint8_t>(255 - original[6]));
    CHECK(pixels[7] == original[7]);
    CHECK(pixels[8] == original[8]);
    CHECK(pixels[9] == original[9]);
    CHECK(pixels[10] == original[10]);
    CHECK(pixels[11] == original[11]);
    CHECK(pixels[16] == static_cast<std::uint8_t>(255 - original[16]));
    CHECK(pixels[19] == original[19]);
}

TEST_CASE("legacy layer light and colorize are deterministic") {
    std::array<std::uint8_t, 8> pixels{{10, 30, 90, 123, 220, 180, 40, 231}};
    ImageView view{pixels.data(), 2, 1, 8};
    REQUIRE(AetherKiri::LayerEffects::applyLight(view, 0, 0, 2, 1, 0, 0));
    CHECK(pixels[0] == 10);
    CHECK(pixels[1] == 30);
    CHECK(pixels[2] == 90);
    CHECK(pixels[3] == 123);
    REQUIRE(AetherKiri::LayerEffects::applyColorize(view, 0, 0, 2, 1, 0, 255,
                                                    1.0));
    CHECK(pixels[3] == 123);
    CHECK(pixels[7] == 231);
    CHECK((pixels[0] != 10 || pixels[1] != 30 || pixels[2] != 90));
}

TEST_CASE("legacy layer mosaic averages each block from an immutable snapshot") {
    std::array<std::uint8_t, 16> pixels{
        0, 0, 0, 10,  100, 0, 0, 20,
        0, 100, 0, 30, 100, 100, 0, 40};
    ImageView view{pixels.data(), 2, 2, 8};
    REQUIRE(AetherKiri::LayerEffects::applyMosaic(view, 0, 0, 2, 2, 2));
    CHECK(pixels[0] == 50);
    CHECK(pixels[1] == 50);
    CHECK(pixels[2] == 0);
    CHECK(pixels[3] == 25);
    CHECK(pixels[4] == 50);
    CHECK(pixels[5] == 50);
    CHECK(pixels[6] == 0);
    CHECK(pixels[7] == 25);
    CHECK(pixels[8] == 50);
    CHECK(pixels[9] == 50);
    CHECK(pixels[10] == 0);
    CHECK(pixels[11] == 25);
}
