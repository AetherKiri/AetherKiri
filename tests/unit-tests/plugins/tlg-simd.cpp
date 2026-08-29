#include <catch2/catch_test_macros.hpp>

#include "TLGSIMD.h"
#include "tvpgl.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || \
    defined(_M_IX86)

namespace {
void fillBytes(std::vector<tjs_uint8> &bytes, tjs_uint32 seed) {
    for(size_t i = 0; i < bytes.size(); ++i) {
        seed = seed * 1664525u + 1013904223u;
        bytes[i] = static_cast<tjs_uint8>(seed >> 24);
    }
}
} // namespace

TEST_CASE("krkrz TLG5 SSE2 decompression matches Aether scalar") {
    // Two literal groups exercise both the fast-copy and tail paths.  A flag
    // bit of zero means the following byte is a literal in the TLG LZSS
    // stream.
    const std::array<tjs_uint8, 18> stream = {
        0x00, 'A', 'e', 't', 'h', 'e', 'r', '-', 'K',
        0x00, 'i', 'r', 'i', '-', 'T', 'L', 'G', '!'};
    std::vector<tjs_uint8> scalarOut(32, 0), simdOut(32, 0);
    std::vector<tjs_uint8> scalarText(4096 + 32, 0), simdText(4096 + 32, 0);

    const tjs_int scalarR = TVPTLG5DecompressSlide_c(
        scalarOut.data(), stream.data(), static_cast<tjs_int>(stream.size()),
        scalarText.data(), 0);
    const tjs_int simdR = TVPTLG5DecompressSlide_sse2_c(
        simdOut.data(), stream.data(), static_cast<tjs_int>(stream.size()),
        simdText.data(), 0);

    CHECK(simdR == scalarR);
    CHECK(std::equal(scalarOut.begin(), scalarOut.begin() + 16,
                    simdOut.begin()));
    CHECK(std::equal(scalarText.begin(), scalarText.end(), simdText.begin()));
}

TEST_CASE("krkrz TLG5 SSE2 color composition matches Aether scalar") {
    for(const tjs_int width : {1, 3, 4, 7, 8, 13}) {
        std::vector<tjs_uint8> upper(static_cast<size_t>(width) * 4 + 16);
        std::vector<tjs_uint8> upperCopy = upper;
        std::vector<tjs_uint8> outScalar(static_cast<size_t>(width) * 4 + 16);
        std::vector<tjs_uint8> outSimd = outScalar;
        std::array<std::vector<tjs_uint8>, 4> scalarBuf, simdBuf;
        std::array<tjs_uint8 *, 4> scalarPtrs{}, simdPtrs{};
        for(size_t c = 0; c < scalarBuf.size(); ++c) {
            scalarBuf[c].resize(static_cast<size_t>(width) + 16);
            fillBytes(scalarBuf[c], static_cast<tjs_uint32>(0x1234 + c * 77));
            simdBuf[c] = scalarBuf[c];
            scalarPtrs[c] = scalarBuf[c].data();
            simdPtrs[c] = simdBuf[c].data();
        }
        fillBytes(upper, 0x9876u + static_cast<tjs_uint32>(width));
        upperCopy = upper;

        TVPTLG5ComposeColors3To4_c(outScalar.data(), upper.data(),
                                    scalarPtrs.data(), width);
        TVPTLG5ComposeColors3To4_sse2_c(outSimd.data(), upperCopy.data(),
                                         simdPtrs.data(), width);
        CHECK(std::equal(outScalar.begin(), outScalar.begin() + width * 4,
                         outSimd.begin()));

        std::fill(outScalar.begin(), outScalar.end(), 0);
        std::fill(outSimd.begin(), outSimd.end(), 0);
        fillBytes(upper, 0x4321u + static_cast<tjs_uint32>(width));
        upperCopy = upper;
        TVPTLG5ComposeColors4To4_c(outScalar.data(), upper.data(),
                                    scalarPtrs.data(), width);
        TVPTLG5ComposeColors4To4_sse2_c(outSimd.data(), upperCopy.data(),
                                         simdPtrs.data(), width);
        CHECK(std::equal(outScalar.begin(), outScalar.begin() + width * 4,
                         outSimd.begin()));
    }
}

TEST_CASE("krkrz TLG6 SSE2 line decoding matches Aether scalar") {
    constexpr tjs_int width = 16;
    constexpr tjs_int blocks = 2;
    for(const tjs_int direction : {0, 1}) {
        std::vector<tjs_uint32> scalarPrev(width), simdPrev(width);
        std::vector<tjs_uint32> scalarCur(width), simdCur(width);
        std::vector<tjs_uint8> scalarFilters(blocks), simdFilters(blocks);
        std::vector<tjs_uint32> scalarInput(256), simdInput(256);
        for(tjs_int i = 0; i < width; ++i) {
            scalarPrev[i] = 0x10203040u + static_cast<tjs_uint32>(i * 37);
            scalarCur[i] = 0x55667788u ^ static_cast<tjs_uint32>(i * 91);
        }
        simdPrev = scalarPrev;
        simdCur = scalarCur;
        scalarFilters = {0, 17};
        simdFilters = scalarFilters;
        for(size_t i = 0; i < scalarInput.size(); ++i)
            scalarInput[i] = 0x01010101u * static_cast<tjs_uint32>(i + 3);
        simdInput = scalarInput;

        TVPTLG6DecodeLineGeneric_c(
            scalarPrev.data(), scalarCur.data(), width, 0, blocks,
            scalarFilters.data(), 16, scalarInput.data(), 0x7f7f7f7fu, 0,
            direction);
        TVPTLG6DecodeLineGeneric_sse2_c(
            simdPrev.data(), simdCur.data(), width, 0, blocks,
            simdFilters.data(), 16, simdInput.data(), 0x7f7f7f7fu, 0,
            direction);

        CHECK(scalarCur == simdCur);
        CHECK(scalarPrev == simdPrev);
    }
}

#else

TEST_CASE("TLG SIMD leaves are unavailable on this architecture") {
    SUCCEED("scalar TLG implementation remains the portable fallback");
}

#endif
