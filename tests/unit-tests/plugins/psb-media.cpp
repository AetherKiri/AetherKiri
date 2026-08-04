#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <vector>

#include "LayerCompletionCoordinates.h"
#include "PimgCompositeBounds.h"
#include "psbfile/PSBMedia.h"

namespace {
    std::vector<uint8_t> makeWebPHeader(const size_t encodedSize) {
        std::vector<uint8_t> encoded(encodedSize, 0xa5);
        REQUIRE(encoded.size() >= 15);
        memcpy(encoded.data(), "RIFF", 4);
        memcpy(encoded.data() + 8, "WEBPVP8", 7);
        return encoded;
    }
}

TEST_CASE("PSB media preserves small WebP images before raw-pixel fallback") {
    SECTION("quick-menu previous-choice normal state") {
        const auto encoded = makeWebPHeader(24 * 25);
        CHECK(PSB::detail::IsSupportedImageHeader(encoded));
    }

    SECTION("chapter-number separator") {
        const auto encoded = makeWebPHeader(112);
        CHECK(encoded.size() > 9 * 3 * 4);
        CHECK(PSB::detail::IsSupportedImageHeader(encoded));
    }
}

TEST_CASE("PIMG composites normalize selected layers to their own bounds") {
    using TVPLayerInternal::ComputePimgCompositeBounds;
    using TVPLayerInternal::PimgCompositeBounds;

    SECTION("chapter ribbon") {
        PimgCompositeBounds bounds{};
        REQUIRE(ComputePimgCompositeBounds({{0, 55, 415, 88}}, bounds));
        CHECK(bounds.left == 0);
        CHECK(bounds.top == 55);
        CHECK(bounds.width == 415);
        CHECK(bounds.height == 88);
        CHECK(55 - bounds.top == 0);
    }

    SECTION("chapter label") {
        PimgCompositeBounds bounds{};
        REQUIRE(ComputePimgCompositeBounds({{60, 80, 197, 31}}, bounds));
        CHECK(bounds.left == 60);
        CHECK(bounds.top == 80);
        CHECK(bounds.width == 197);
        CHECK(bounds.height == 31);
        CHECK(60 - bounds.left == 0);
        CHECK(80 - bounds.top == 0);
    }

    SECTION("multiple selected layers retain relative placement") {
        PimgCompositeBounds bounds{};
        REQUIRE(ComputePimgCompositeBounds(
            {{60, 80, 197, 31}, {269, 80, 19, 31}}, bounds));
        CHECK(bounds.left == 60);
        CHECK(bounds.top == 80);
        CHECK(bounds.width == 228);
        CHECK(bounds.height == 31);
        CHECK(269 - bounds.left == 209);
    }
}

TEST_CASE("GPU offscreen completion keeps a layer-local destination") {
    using TVPLayerInternal::ResolveGpuCompletionCoordinates;

    SECTION("chapter layer cache") {
        const auto coordinates = ResolveGpuCompletionCoordinates(
            0, 0, 420, 88, 0, 55, true);
        CHECK(coordinates.parentLeft == 0);
        CHECK(coordinates.parentTop == 55);
        CHECK(coordinates.parentRight == 420);
        CHECK(coordinates.parentBottom == 143);
        CHECK(coordinates.destinationX == 0);
        CHECK(coordinates.destinationY == 0);
    }

    SECTION("window completion retains parent coordinates") {
        const auto coordinates = ResolveGpuCompletionCoordinates(
            0, 0, 420, 88, 0, 55, false);
        CHECK(coordinates.parentTop == 55);
        CHECK(coordinates.parentBottom == 143);
        CHECK(coordinates.destinationX == 0);
        CHECK(coordinates.destinationY == 55);
    }

    SECTION("dirty offscreen region retains its local offset") {
        const auto coordinates = ResolveGpuCompletionCoordinates(
            12, 7, 100, 40, 30, 55, true);
        CHECK(coordinates.parentLeft == 42);
        CHECK(coordinates.parentTop == 62);
        CHECK(coordinates.destinationX == 12);
        CHECK(coordinates.destinationY == 7);
    }
}
