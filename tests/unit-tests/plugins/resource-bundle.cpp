#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "portableResourceBundle.h"

using AetherKiri::ResourceBundle::Decode;
using AetherKiri::ResourceBundle::Encode;
using AetherKiri::ResourceBundle::Entry;

TEST_CASE("portable resource bundle round-trips typed resources") {
    const std::vector<Entry> input{
        {"@10", "=manifest", 0, {'a', 'b', 'c'}},
        {"=RCDATA", "@7", 1041, {0x00, 0xff, 0x42}},
    };
    std::vector<std::uint8_t> encoded;
    std::string error;
    REQUIRE(Encode(input, encoded, &error));
    CHECK(error.empty());

    std::vector<Entry> output;
    REQUIRE(Decode(encoded, output, &error));
    REQUIRE(output.size() == input.size());
    for(std::size_t i = 0; i < input.size(); ++i) {
        CHECK(output[i].type == input[i].type);
        CHECK(output[i].name == input[i].name);
        CHECK(output[i].language == input[i].language);
        CHECK(output[i].bytes == input[i].bytes);
    }
}

TEST_CASE("portable resource bundle rejects ambiguity and corruption") {
    Entry first{"=A", "=BC", 0, {1}};
    Entry second{"=AB", "=C", 0, {2}};
    std::vector<std::uint8_t> encoded;
    std::string error;
    REQUIRE(Encode({first, second}, encoded, &error));

    SECTION("trailing bytes") {
        encoded.push_back(0);
        std::vector<Entry> decoded;
        CHECK_FALSE(Decode(encoded, decoded, &error));
        CHECK(decoded.empty());
    }

    SECTION("duplicate identity") {
        CHECK_FALSE(Encode({first, first}, encoded, &error));
    }

    SECTION("invalid key") {
        Entry invalid{"plain", "=name", 0, {1}};
        CHECK_FALSE(Encode({invalid}, encoded, &error));
    }
}
