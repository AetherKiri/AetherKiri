#include <catch2/catch_test_macros.hpp>

#include "XP3ArchiveCxDecoder.h"

#include <array>
#include <cstdint>

TEST_CASE("built-in XP3 Cx selection rejects pre-decoded bytecode") {
    constexpr std::uint32_t fingerprint = 0xe425cd85u;
    const std::array<std::uint8_t, 8> bytecode = {
        'T', 'J', 'S', '2', '1', '0', '0', 0,
    };
    const std::array<std::uint8_t, 8> encrypted = {
        0x31, 0x2f, 0x36, 0x57, 0x54, 0x55, 0x55, 0x65,
    };

    CHECK(TVPIsBuiltinXP3CxScheme(fingerprint));
    CHECK_FALSE(TVPShouldUseBuiltinXP3CxDecoder(
        fingerprint, bytecode.data(), bytecode.size()));
    CHECK(TVPShouldUseBuiltinXP3CxDecoder(
        fingerprint, encrypted.data(), encrypted.size()));
    CHECK_FALSE(TVPShouldUseBuiltinXP3CxDecoder(
        0x12345678u, encrypted.data(), encrypted.size()));
}

TEST_CASE("built-in XP3 Cx decoder is shared by sibling archives") {
    constexpr std::uint32_t fingerprint = 0xe425cd85u;

    TVPResetBuiltinXP3CxDecoder();
    CHECK_FALSE(TVPIsBuiltinXP3CxDecoderActive());

    CHECK(TVPActivateBuiltinXP3CxDecoder(fingerprint));
    CHECK(TVPIsBuiltinXP3CxDecoderActive());

    TVPResetBuiltinXP3CxDecoder();
    CHECK_FALSE(TVPIsBuiltinXP3CxDecoderActive());
}
