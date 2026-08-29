#include <catch2/catch_test_macros.hpp>

#include "VisualSIMDLeaves.h"
#include "tvpgl.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <mutex>
#include <random>
#include <vector>

extern "C" {
void TVPInitTVPGL();
void TVPApplyColorMap_c(tjs_uint32 *, const tjs_uint8 *, tjs_int, tjs_uint32);
void TVPApplyColorMap_o_c(tjs_uint32 *, const tjs_uint8 *, tjs_int, tjs_uint32,
                          tjs_int);
void TVPApplyColorMap65_c(tjs_uint32 *, const tjs_uint8 *, tjs_int, tjs_uint32);
void TVPApplyColorMap65_o_c(tjs_uint32 *, const tjs_uint8 *, tjs_int,
                            tjs_uint32, tjs_int);
void TVPConvert24BitTo32Bit_c(tjs_uint32 *, const tjs_uint8 *, tjs_int);
void TVPInitUnivTransBlendTable_c(tjs_uint32 *, tjs_int, tjs_int);
void TVPInitUnivTransBlendTable_d_c(tjs_uint32 *, tjs_int, tjs_int);
void TVPUnivTransBlend_c(tjs_uint32 *, const tjs_uint32 *, const tjs_uint32 *,
                         const tjs_uint8 *, const tjs_uint32 *, tjs_int);
void TVPUnivTransBlend_switch_c(tjs_uint32 *, const tjs_uint32 *,
                                const tjs_uint32 *, const tjs_uint8 *,
                                const tjs_uint32 *, tjs_int, tjs_int, tjs_int);
void TVPUnivTransBlend_d_c(tjs_uint32 *, const tjs_uint32 *, const tjs_uint32 *,
                           const tjs_uint8 *, const tjs_uint32 *, tjs_int);
void TVPUnivTransBlend_switch_d_c(
    tjs_uint32 *, const tjs_uint32 *, const tjs_uint32 *, const tjs_uint8 *,
    const tjs_uint32 *, tjs_int, tjs_int, tjs_int);
}

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || \
    defined(_M_IX86)
void TVPApplyColorMap_sse2_c(tjs_uint32 *, const tjs_uint8 *, tjs_int,
                             tjs_uint32);
void TVPApplyColorMap_o_sse2_c(tjs_uint32 *, const tjs_uint8 *, tjs_int,
                               tjs_uint32, tjs_int);
void TVPApplyColorMap65_sse2_c(tjs_uint32 *, const tjs_uint8 *, tjs_int,
                               tjs_uint32);
void TVPApplyColorMap65_o_sse2_c(tjs_uint32 *, const tjs_uint8 *, tjs_int,
                                 tjs_uint32, tjs_int);
void TVPConvert24BitTo32Bit_sse2_c(tjs_uint32 *, const tjs_uint8 *, tjs_int);
void TVPInitUnivTransBlendTable_sse2_c(tjs_uint32 *, tjs_int, tjs_int);
void TVPInitUnivTransBlendTable_d_sse2_c(tjs_uint32 *, tjs_int, tjs_int);
void TVPUnivTransBlend_sse2_c(tjs_uint32 *, const tjs_uint32 *,
                              const tjs_uint32 *, const tjs_uint8 *,
                              const tjs_uint32 *, tjs_int);
void TVPUnivTransBlend_switch_sse2_c(
    tjs_uint32 *, const tjs_uint32 *, const tjs_uint32 *, const tjs_uint8 *,
    const tjs_uint32 *, tjs_int, tjs_int, tjs_int);
void TVPUnivTransBlend_d_sse2_c(tjs_uint32 *, const tjs_uint32 *,
                                const tjs_uint32 *, const tjs_uint8 *,
                                const tjs_uint32 *, tjs_int);
void TVPUnivTransBlend_switch_d_sse2_c(
    tjs_uint32 *, const tjs_uint32 *, const tjs_uint32 *, const tjs_uint8 *,
    const tjs_uint32 *, tjs_int, tjs_int, tjs_int);
#define AETHER_VISUAL_LEAF_COLOR_MAP TVPApplyColorMap_sse2_c
#define AETHER_VISUAL_LEAF_COLOR_MAP_O TVPApplyColorMap_o_sse2_c
#define AETHER_VISUAL_LEAF_COLOR_MAP65 TVPApplyColorMap65_sse2_c
#define AETHER_VISUAL_LEAF_COLOR_MAP65_O TVPApplyColorMap65_o_sse2_c
#define AETHER_VISUAL_LEAF_PIXEL TVPConvert24BitTo32Bit_sse2_c
#define AETHER_VISUAL_LEAF_TABLE TVPInitUnivTransBlendTable_sse2_c
#define AETHER_VISUAL_LEAF_TABLE_D TVPInitUnivTransBlendTable_d_sse2_c
#define AETHER_VISUAL_LEAF_UNIV TVPUnivTransBlend_sse2_c
#define AETHER_VISUAL_LEAF_UNIV_SWITCH TVPUnivTransBlend_switch_sse2_c
#define AETHER_VISUAL_LEAF_UNIV_D TVPUnivTransBlend_d_sse2_c
#define AETHER_VISUAL_LEAF_UNIV_SWITCH_D TVPUnivTransBlend_switch_d_sse2_c
#define AETHER_VISUAL_LEAF_AVAILABLE 1
#elif defined(__aarch64__) || defined(__arm64__) || defined(__ARM_NEON) || \
    defined(__ARM_NEON__)
void TVPApplyColorMap_neon_c(tjs_uint32 *, const tjs_uint8 *, tjs_int,
                             tjs_uint32);
void TVPApplyColorMap_o_neon_c(tjs_uint32 *, const tjs_uint8 *, tjs_int,
                               tjs_uint32, tjs_int);
void TVPApplyColorMap65_neon_c(tjs_uint32 *, const tjs_uint8 *, tjs_int,
                               tjs_uint32);
void TVPApplyColorMap65_o_neon_c(tjs_uint32 *, const tjs_uint8 *, tjs_int,
                                 tjs_uint32, tjs_int);
void TVPConvert24BitTo32Bit_neon_c(tjs_uint32 *, const tjs_uint8 *, tjs_int);
#define AETHER_VISUAL_LEAF_COLOR_MAP TVPApplyColorMap_neon_c
#define AETHER_VISUAL_LEAF_COLOR_MAP_O TVPApplyColorMap_o_neon_c
#define AETHER_VISUAL_LEAF_COLOR_MAP65 TVPApplyColorMap65_neon_c
#define AETHER_VISUAL_LEAF_COLOR_MAP65_O TVPApplyColorMap65_o_neon_c
#define AETHER_VISUAL_LEAF_PIXEL TVPConvert24BitTo32Bit_neon_c
#define AETHER_VISUAL_LEAF_AVAILABLE 1
#endif

namespace {
void init_tables() {
    static std::once_flag once;
    std::call_once(once, [] { TVPInitTVPGL(); });
}

template <typename Leaf, typename Scalar>
void compare_color_no_opacity(Leaf leaf, Scalar scalar, bool level65) {
    std::mt19937 rng(0x5a17u + (level65 ? 65u : 256u));
    for(const tjs_int len : {0, 1, 2, 3, 4, 7, 16, 17, 31}) {
        std::vector<tjs_uint32> expected(static_cast<size_t>(len) + 4);
        std::vector<tjs_uint8> mask(static_cast<size_t>(len) + 4);
        for(tjs_int i = 0; i < len + 4; ++i) {
            expected[static_cast<size_t>(i)] = rng();
            mask[static_cast<size_t>(i)] = static_cast<tjs_uint8>(
                level65 ? rng() % 65u : rng() % 256u);
        }
        std::vector<tjs_uint32> actual = expected;
        const tjs_uint32 color = rng();
        scalar(expected.data() + 1, mask.data() + 1, len, color);
        leaf(actual.data() + 1, mask.data() + 1, len, color);
        CHECK(std::equal(expected.begin() + 1,
                         expected.begin() + 1 + static_cast<size_t>(len),
                         actual.begin() + 1));
    }
}

template <typename Leaf, typename Scalar>
void compare_color_with_opacity(Leaf leaf, Scalar scalar, bool level65) {
    std::mt19937 rng(0x5a17u + (level65 ? 65u : 256u) + 17u);
    for(const tjs_int len : {0, 1, 2, 3, 4, 7, 16, 17, 31}) {
        std::vector<tjs_uint32> expected(static_cast<size_t>(len) + 4);
        std::vector<tjs_uint8> mask(static_cast<size_t>(len) + 4);
        for(tjs_int i = 0; i < len + 4; ++i) {
            expected[static_cast<size_t>(i)] = rng();
            mask[static_cast<size_t>(i)] = static_cast<tjs_uint8>(
                level65 ? rng() % 65u : rng() % 256u);
        }
        std::vector<tjs_uint32> actual = expected;
        const tjs_uint32 color = rng();
        const tjs_int opa = static_cast<tjs_int>(rng() % 257u);
        scalar(expected.data() + 1, mask.data() + 1, len, color, opa);
        leaf(actual.data() + 1, mask.data() + 1, len, color, opa);
        CHECK(std::equal(expected.begin() + 1,
                         expected.begin() + 1 + static_cast<size_t>(len),
                         actual.begin() + 1));
    }
}
} // namespace

#if defined(AETHER_VISUAL_LEAF_AVAILABLE)
TEST_CASE("krkrz visual color-map leaves preserve scalar pixels") {
    init_tables();
    compare_color_no_opacity(AETHER_VISUAL_LEAF_COLOR_MAP, TVPApplyColorMap_c,
                             false);
    compare_color_with_opacity(AETHER_VISUAL_LEAF_COLOR_MAP_O,
                               TVPApplyColorMap_o_c, false);
    compare_color_no_opacity(AETHER_VISUAL_LEAF_COLOR_MAP65,
                             TVPApplyColorMap65_c, true);
    compare_color_with_opacity(AETHER_VISUAL_LEAF_COLOR_MAP65_O,
                               TVPApplyColorMap65_o_c, true);
}

TEST_CASE("krkrz visual pixel-format leaf preserves 24-bit conversion") {
    init_tables();
    std::mt19937 rng(0x24u);
    for(const tjs_int len : {0, 1, 2, 3, 4, 7, 16, 17, 31}) {
        std::vector<tjs_uint8> bytes(static_cast<size_t>(len) * 3 + 32);
        for(auto &value : bytes)
            value = static_cast<tjs_uint8>(rng());
        std::vector<tjs_uint32> expected(static_cast<size_t>(len) + 4);
        for(auto &value : expected)
            value = rng();
        std::vector<tjs_uint32> actual = expected;
        TVPConvert24BitTo32Bit_c(expected.data() + 1, bytes.data() + 1, len);
        AETHER_VISUAL_LEAF_PIXEL(actual.data() + 1, bytes.data() + 1, len);
        CHECK(std::equal(expected.begin() + 1,
                         expected.begin() + 1 + static_cast<size_t>(len),
                         actual.begin() + 1));
    }
}

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || \
    defined(_M_IX86)
TEST_CASE("krkrz SSE2 transition leaves preserve scalar output") {
    init_tables();
    std::mt19937 rng(0x71u);
    for(const auto params :
        std::array<std::pair<tjs_int, tjs_int>, 3>{{{32, 3}, {128, 17},
                                                    {256, 31}}}) {
        const tjs_int phase = params.first;
        const tjs_int vague = params.second;
        std::array<tjs_uint32, 256> tableScalar{}, tableSimd{};
        std::array<tjs_uint32, 256> tableScalarD{}, tableSimdD{};
        TVPInitUnivTransBlendTable_c(tableScalar.data(), phase, vague);
        AETHER_VISUAL_LEAF_TABLE(tableSimd.data(), phase, vague);
        TVPInitUnivTransBlendTable_d_c(tableScalarD.data(), phase, vague);
        AETHER_VISUAL_LEAF_TABLE_D(tableSimdD.data(), phase, vague);
        for(size_t i = 0; i < tableScalar.size(); ++i)
            CHECK((tableSimd[i] & 0xffffu) == tableScalar[i]);
        // The upstream non-alpha kernel stores two copies of the opacity in
        // one word; the adapter intentionally keeps that private table shape.
        CHECK(tableScalarD == tableSimdD);

        for(const tjs_int len : {1, 2, 3, 4, 7, 19}) {
            std::vector<tjs_uint32> src1(static_cast<size_t>(len) + 4);
            std::vector<tjs_uint32> src2 = src1;
            std::vector<tjs_uint32> expected = src1;
            std::vector<tjs_uint32> actual = src1;
            std::vector<tjs_uint8> rule(static_cast<size_t>(len) + 4);
            for(tjs_int i = 0; i < len + 4; ++i) {
                src1[static_cast<size_t>(i)] = rng();
                src2[static_cast<size_t>(i)] = rng();
                rule[static_cast<size_t>(i)] = static_cast<tjs_uint8>(rng());
            }
            TVPUnivTransBlend_c(expected.data() + 1, src1.data() + 1,
                                src2.data() + 1, rule.data() + 1,
                                tableScalar.data(), len);
            AETHER_VISUAL_LEAF_UNIV(actual.data() + 1, src1.data() + 1,
                                    src2.data() + 1, rule.data() + 1,
                                    tableSimd.data(), len);
            CHECK(std::equal(expected.begin() + 1,
                             expected.begin() + 1 + static_cast<size_t>(len),
                             actual.begin() + 1));

            TVPUnivTransBlend_switch_c(
                expected.data() + 1, src1.data() + 1, src2.data() + 1,
                rule.data() + 1, tableScalar.data(), len, 220, 35);
            AETHER_VISUAL_LEAF_UNIV_SWITCH(
                actual.data() + 1, src1.data() + 1, src2.data() + 1,
                rule.data() + 1, tableSimd.data(), len, 220, 35);
            CHECK(std::equal(expected.begin() + 1,
                             expected.begin() + 1 + static_cast<size_t>(len),
                             actual.begin() + 1));

            TVPUnivTransBlend_d_c(expected.data() + 1, src1.data() + 1,
                                  src2.data() + 1, rule.data() + 1,
                                  tableScalarD.data(), len);
            AETHER_VISUAL_LEAF_UNIV_D(actual.data() + 1, src1.data() + 1,
                                      src2.data() + 1, rule.data() + 1,
                                      tableSimdD.data(), len);
            CHECK(std::equal(expected.begin() + 1,
                             expected.begin() + 1 + static_cast<size_t>(len),
                             actual.begin() + 1));

            TVPUnivTransBlend_switch_d_c(
                expected.data() + 1, src1.data() + 1, src2.data() + 1,
                rule.data() + 1, tableScalarD.data(), len, 220, 35);
            AETHER_VISUAL_LEAF_UNIV_SWITCH_D(
                actual.data() + 1, src1.data() + 1, src2.data() + 1,
                rule.data() + 1, tableSimdD.data(), len, 220, 35);
            CHECK(std::equal(expected.begin() + 1,
                             expected.begin() + 1 + static_cast<size_t>(len),
                             actual.begin() + 1));
        }
    }
}

TEST_CASE("krkrz SSE2 alpha transition handles descending alpha") {
    init_tables();
    // Exercise the signed-difference branch explicitly.  Random coverage
    // above is useful for parity, but this case documents the contract that
    // previously depended on unsigned wraparound when src2 alpha is lower.
    constexpr tjs_int phase = 192;
    constexpr tjs_int vague = 64;
    std::array<tjs_uint32, 256> table{};
    TVPInitUnivTransBlendTable_d_c(table.data(), phase, vague);

    const std::array<tjs_uint32, 3> src1{{0xf0102030u, 0xe0405060u,
                                          0xd08090a0u}};
    const std::array<tjs_uint32, 3> src2{{0x10203040u, 0x20406080u,
                                          0x30a0b0c0u}};
    const std::array<tjs_uint8, 3> rule{{160u, 176u, 191u}};
    std::array<tjs_uint32, 3> expected{};
    std::array<tjs_uint32, 3> actual{};
    TVPUnivTransBlend_d_c(expected.data(), src1.data(), src2.data(),
                          rule.data(), table.data(),
                          static_cast<tjs_int>(expected.size()));
    AETHER_VISUAL_LEAF_UNIV_D(actual.data(), src1.data(), src2.data(),
                              rule.data(), table.data(),
                              static_cast<tjs_int>(actual.size()));
    CHECK(expected == actual);
    for(size_t i = 0; i < actual.size(); ++i) {
        const tjs_int a1 = static_cast<tjs_int>(src1[i] >> 24);
        const tjs_int a2 = static_cast<tjs_int>(src2[i] >> 24);
        const tjs_int opa = static_cast<tjs_int>(table[rule[i]]);
        const tjs_int expected_alpha = a1 + ((a2 - a1) * opa >> 8);
        CHECK(static_cast<tjs_int>(actual[i] >> 24) == expected_alpha);
    }
}

#endif
#endif
