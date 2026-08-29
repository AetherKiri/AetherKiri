#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "psbfile/PSBValue.h"
#include "UtilStreams.h"

namespace {
    std::vector<std::uint8_t> makePackedArray(const std::uint8_t count,
                                              const std::uint8_t width,
                                              const std::vector<std::uint8_t> &data) {
        std::vector<std::uint8_t> bytes;
        bytes.reserve(2 + data.size());
        bytes.push_back(count);
        bytes.push_back(static_cast<std::uint8_t>(
            static_cast<std::uint8_t>(PSB::PSBObjType::NumberN8) + width));
        bytes.insert(bytes.end(), data.begin(), data.end());
        return bytes;
    }
}

TEST_CASE("PSB packed collections decode full-width counts and entries") {
    const auto bytes = makePackedArray(2, 2, {0x34, 0x12, 0x78, 0x56});
    tTVPMemoryStream stream(bytes.data(), static_cast<tjs_uint>(bytes.size()));
    PSB::PSBArray array(1, &stream);
    REQUIRE(array.value.size() == 2);
    CHECK(array.value[0] == 0x1234);
    CHECK(array.value[1] == 0x5678);
}

TEST_CASE("PSB packed collections reject malformed or truncated input") {
    SECTION("count cannot overflow or allocate without a bound") {
        const std::vector<std::uint8_t> bytes(9, 0xff);
        tTVPMemoryStream stream(bytes.data(), static_cast<tjs_uint>(bytes.size()));
        CHECK_THROWS(PSB::PSBArray(8, &stream));
    }

    SECTION("entry width is restricted to the PSB number-width range") {
        const auto bytes = makePackedArray(1, 5, {0, 0, 0, 0, 0});
        tTVPMemoryStream stream(bytes.data(), static_cast<tjs_uint>(bytes.size()));
        CHECK_THROWS(PSB::PSBArray(1, &stream));
    }

    SECTION("truncated entries are rejected before allocation") {
        const auto bytes = makePackedArray(2, 4, {0x01, 0x02, 0x03});
        tTVPMemoryStream stream(bytes.data(), static_cast<tjs_uint>(bytes.size()));
        CHECK_THROWS(PSB::PSBList::loadIntoList(1, &stream));
    }
}
