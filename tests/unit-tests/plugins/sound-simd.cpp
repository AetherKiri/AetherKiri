#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sound/MathAlgorithms.h"
#include "utils/RealFFT.h"

#include <array>
#include <cmath>
#include <vector>

namespace {
constexpr size_t kSamples = 19;

void fillInput(std::array<float, kSamples * 2> &interleaved,
               std::array<float, kSamples> &window) {
    for(size_t i = 0; i < kSamples; ++i) {
        interleaved[i * 2] = static_cast<float>(i) * 0.25f - 1.0f;
        interleaved[i * 2 + 1] = 1.0f - static_cast<float>(i) * 0.125f;
        window[i] = 0.25f + static_cast<float>(i) * 0.03125f;
    }
}
} // namespace

TEST_CASE("krkrz sound SIMD windows preserve the scalar contract") {
    std::array<float, kSamples * 2> interleaved{};
    std::array<float, kSamples> window{};
    fillInput(interleaved, window);

    std::array<float, kSamples> scalarLeft{};
    std::array<float, kSamples> scalarRight{};
    std::array<float, kSamples> simdLeft{};
    std::array<float, kSamples> simdRight{};
    float *scalarChannels[] = {scalarLeft.data(), scalarRight.data()};
    float *simdChannels[] = {simdLeft.data(), simdRight.data()};

    DeinterleaveApplyingWindow(scalarChannels, interleaved.data(),
                               window.data(), 2, 0, kSamples);

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || \
    defined(_M_IX86)
    DeinterleaveApplyingWindow_sse(simdChannels, interleaved.data(),
                                   window.data(), 2, 0, kSamples);
#elif defined(__aarch64__) || defined(__ARM_NEON)
    DeinterleaveApplyingWindow_neon(simdChannels, interleaved.data(),
                                    window.data(), 2, 0, kSamples);
#else
    simdLeft = scalarLeft;
    simdRight = scalarRight;
#endif

    for(size_t i = 0; i < kSamples; ++i) {
        CHECK(simdLeft[i] == Catch::Approx(scalarLeft[i]).margin(1e-6f));
        CHECK(simdRight[i] == Catch::Approx(scalarRight[i]).margin(1e-6f));
    }

    std::array<float, kSamples * 2> scalarOutput{};
    std::array<float, kSamples * 2> simdOutput{};
    const float *scalarSources[] = {scalarLeft.data(), scalarRight.data()};
    const float *simdSources[] = {simdLeft.data(), simdRight.data()};
    InterleaveOverlappingWindow(scalarOutput.data(), scalarSources,
                                window.data(), 2, 0, kSamples);
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || \
    defined(_M_IX86)
    InterleaveOverlappingWindow_sse(simdOutput.data(), simdSources,
                                    window.data(), 2, 0, kSamples);
#elif defined(__aarch64__) || defined(__ARM_NEON)
    InterleaveOverlappingWindow_neon(simdOutput.data(), simdSources,
                                     window.data(), 2, 0, kSamples);
#else
    simdOutput = scalarOutput;
#endif
    for(size_t i = 0; i < kSamples * 2; ++i)
        CHECK(simdOutput[i] == Catch::Approx(scalarOutput[i]).margin(1e-6f));
}

TEST_CASE("krkrz sound SIMD real FFT preserves the scalar contract") {
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || \
    defined(_M_IX86)
    constexpr auto simd_fft = &rdft_sse;
#elif defined(__aarch64__) || defined(__ARM_NEON)
    constexpr auto simd_fft = &rdft_neon;
#else
    // Unsupported targets intentionally keep the scalar implementation only.
    SUCCEED("no SIMD real-FFT leaf for this target");
    return;
#endif

    for(const int n : {32, 64, 128}) {
        std::vector<float> scalar(static_cast<size_t>(n));
        std::vector<float> simd(static_cast<size_t>(n));
        for(int i = 0; i < n; ++i) {
            const float x = static_cast<float>(i);
            scalar[static_cast<size_t>(i)] =
                std::sin(0.17f * x) + 0.03125f * std::cos(0.07f * x * x);
        }
        simd = scalar;

        // Keep work arrays independent: both implementations lazily build
        // their twiddle tables and mutate the bookkeeping vectors.
        std::vector<int> scalar_ip(64, 0);
        std::vector<int> simd_ip(64, 0);
        std::vector<float> scalar_w(static_cast<size_t>(n), 0.0f);
        std::vector<float> simd_w(static_cast<size_t>(n), 0.0f);

        rdft(n, 1, scalar.data(), scalar_ip.data(), scalar_w.data());
        simd_fft(n, 1, simd.data(), simd_ip.data(), simd_w.data());
        for(int i = 0; i < n; ++i)
            CHECK(simd[static_cast<size_t>(i)] ==
                  Catch::Approx(scalar[static_cast<size_t>(i)]).margin(2e-4f));

        rdft(n, -1, scalar.data(), scalar_ip.data(), scalar_w.data());
        simd_fft(n, -1, simd.data(), simd_ip.data(), simd_w.data());
        for(int i = 0; i < n; ++i)
            CHECK(simd[static_cast<size_t>(i)] ==
                  Catch::Approx(scalar[static_cast<size_t>(i)]).margin(3e-4f));
    }
}
