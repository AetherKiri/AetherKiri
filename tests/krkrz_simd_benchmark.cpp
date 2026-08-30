#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

#include "tjsCommHead.h"
#include "tvpgl.h"
#include "cpu_detect.h"
#include "tvpgl_ia32_intf.h"

extern "C" tjs_uint32 TVPCPUType = 0;
extern "C" void TVPInitTVPGL();
void TVPGL_C_Init();
void TVPInitializeResampleSSE2() {}
void TVPInitializeResampleAVX2() {}

#ifdef KRKRZ_TEST_HAS_X86
void TVPGL_SSE2_Init();
#endif
#ifdef KRKRZ_TEST_HAS_NEON
void TVPGL_NEON_Init();
#endif

namespace {

using Clock = std::chrono::steady_clock;
volatile std::uint64_t BenchmarkSink = 0;
int BenchmarkParityFailures = 0;

template <typename Work>
double Measure(int pixels, int iterations, Work work) {
    std::vector<double> samples;
    samples.reserve(7);
    for(int round = 0; round < 7; ++round) {
        const auto begin = Clock::now();
        for(int i = 0; i < iterations; ++i)
            work();
        const auto elapsed = std::chrono::duration<double, std::nano>(
            Clock::now() - begin).count();
        samples.push_back(elapsed / (static_cast<double>(pixels) * iterations));
    }
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

void Report(const char *name, double scalar, double simd) {
    std::printf("%-30s scalar=%7.3f ns/px  simd=%7.3f ns/px  speedup=%5.2fx\n",
                name, scalar, simd, scalar / simd);
}

void ReportObserved(const char *name, double scalar, double simd) {
    std::printf(
        "%-30s scalar=%7.3f ns/px  simd=%7.3f ns/px  observe=%5.2fx\n",
        name, scalar, simd, scalar / simd);
}

template <typename Function>
void BenchmarkBlend(const char *name, Function scalar, Function simd,
                    std::vector<tjs_uint32> &dest,
                    const std::vector<tjs_uint32> &src) {
    auto ref = dest;
    auto test = dest;
    scalar(ref.data(), src.data(), static_cast<tjs_int>(ref.size()));
    simd(test.data(), src.data(), static_cast<tjs_int>(test.size()));
    if(ref != test) {
        std::printf("%-30s parity=FAIL\n", name);
        ++BenchmarkParityFailures;
        return;
    }
    const int iterations = 3000;
    const double scalar_time =
        Measure(static_cast<int>(dest.size()), iterations, [&] {
            scalar(dest.data(), src.data(),
                   static_cast<tjs_int>(dest.size()));
        });
    const double simd_time =
        Measure(static_cast<int>(dest.size()), iterations, [&] {
            simd(dest.data(), src.data(),
                 static_cast<tjs_int>(dest.size()));
        });
    BenchmarkSink += dest[dest.size() / 2];
    Report(name, scalar_time, simd_time);
}

template <typename Function>
void BenchmarkFill(const char *name, Function scalar, Function simd,
                   std::vector<tjs_uint32> &dest) {
    auto ref = dest;
    auto test = dest;
    scalar(ref.data(), static_cast<tjs_int>(ref.size()), 0x7f2359a1u);
    simd(test.data(), static_cast<tjs_int>(test.size()), 0x7f2359a1u);
    if(ref != test) {
        std::printf("%-30s parity=FAIL\n", name);
        ++BenchmarkParityFailures;
        return;
    }
    const int iterations = 6000;
    const double scalar_time =
        Measure(static_cast<int>(dest.size()), iterations, [&] {
            scalar(dest.data(), static_cast<tjs_int>(dest.size()),
                   0x7f2359a1u);
        });
    const double simd_time =
        Measure(static_cast<int>(dest.size()), iterations, [&] {
            simd(dest.data(), static_cast<tjs_int>(dest.size()),
                 0x7f2359a1u);
        });
    BenchmarkSink += dest[dest.size() / 3];
    Report(name, scalar_time, simd_time);
}

template <typename Function>
void BenchmarkColorBlend(const char *name, Function scalar, Function simd,
                         std::vector<tjs_uint32> &dest,
                         bool dispatch_candidate = true) {
    auto ref = dest;
    auto test = dest;
    scalar(ref.data(), static_cast<tjs_int>(ref.size()), 0x004c91d2u, 173);
    simd(test.data(), static_cast<tjs_int>(test.size()), 0x004c91d2u, 173);
    if(ref != test) {
        std::printf("%-30s %s\n", name,
                    dispatch_candidate ? "parity=FAIL" : "rejected=parity");
        if(dispatch_candidate)
            ++BenchmarkParityFailures;
        return;
    }
    const int iterations = 3000;
    const double scalar_time =
        Measure(static_cast<int>(dest.size()), iterations, [&] {
            scalar(dest.data(), static_cast<tjs_int>(dest.size()),
                   0x004c91d2u, 173);
        });
    const double simd_time =
        Measure(static_cast<int>(dest.size()), iterations, [&] {
            simd(dest.data(), static_cast<tjs_int>(dest.size()), 0x004c91d2u,
                 173);
        });
    BenchmarkSink += dest[dest.size() / 5];
    if(dispatch_candidate)
        Report(name, scalar_time, simd_time);
    else
        ReportObserved(name, scalar_time, simd_time);
}

#ifdef KRKRZ_TEST_HAS_X86
void BenchmarkTLG() {
    constexpr int width = 4096;
    constexpr int iterations = 1600;
    using Compose = void (*)(tjs_uint8 *, const tjs_uint8 *,
                             tjs_uint8 *const *, tjs_int);
    using Slide = tjs_int (*)(tjs_uint8 *, const tjs_uint8 *, tjs_int,
                              tjs_uint8 *, tjs_int);
    using DecodeLine = void (*)(tjs_uint32 *, tjs_uint32 *, tjs_int, tjs_int,
                                tjs_uint8 *, tjs_int, tjs_uint32 *,
                                tjs_uint32, tjs_int, tjs_int);

    std::printf("TLG candidates (observation only; runtime stays scalar)\n");
    TVPGL_C_Init();
    const Compose compose3_scalar = TVPTLG5ComposeColors3To4;
    const Compose compose4_scalar = TVPTLG5ComposeColors4To4;
    const Slide slide_scalar = TVPTLG5DecompressSlide;
    const DecodeLine line_scalar = TVPTLG6DecodeLine;
    TVPGL_SSE2_Init();
    const Compose compose3_simd = TVPTLG5ComposeColors3To4;
    const Compose compose4_simd = TVPTLG5ComposeColors4To4;
    const Slide slide_simd = TVPTLG5DecompressSlide;
    const DecodeLine line_simd = TVPTLG6DecodeLine;

    std::mt19937 rng(0x544c4735u);
    std::vector<tjs_uint8> upper(width * 4);
    std::vector<tjs_uint8> channels[4];
    for(auto &channel : channels)
        channel.resize(width);
    for(auto &value : upper)
        value = static_cast<tjs_uint8>(rng());
    for(auto &channel : channels)
        for(auto &value : channel)
            value = static_cast<tjs_uint8>(rng());
    tjs_uint8 *channel_ptrs[4] = { channels[0].data(), channels[1].data(),
                                   channels[2].data(), channels[3].data() };
    std::vector<tjs_uint8> ref(width * 4), test(width * 4);

    const auto run_compose = [&](const char *name, Compose scalar,
                                 Compose simd) {
        scalar(ref.data(), upper.data(), channel_ptrs, width);
        simd(test.data(), upper.data(), channel_ptrs, width);
        if(ref != test) {
            std::printf("%-30s parity=FAIL\n", name);
            ++BenchmarkParityFailures;
            return;
        }
        const double scalar_time = Measure(width, iterations, [&] {
            scalar(ref.data(), upper.data(), channel_ptrs, width);
        });
        const double simd_time = Measure(width, iterations, [&] {
            simd(test.data(), upper.data(), channel_ptrs, width);
        });
        BenchmarkSink += test[width];
        Report(name, scalar_time, simd_time);
    };
    run_compose("TLG5 compose 3->4", compose3_scalar, compose3_simd);
    run_compose("TLG5 compose 4->4", compose4_scalar, compose4_simd);

    std::vector<tjs_uint8> encoded;
    encoded.reserve((width / 8) * 9);
    for(int block = 0; block < width / 8; ++block) {
        encoded.push_back(0);
        for(int j = 0; j < 8; ++j)
            encoded.push_back(static_cast<tjs_uint8>(rng()));
    }
    std::vector<tjs_uint8> text_scalar(4096 + 16, 0);
    std::vector<tjs_uint8> text_simd(4096 + 16, 0);
    std::vector<tjs_uint8> out_scalar(width + 16, 0);
    std::vector<tjs_uint8> out_simd(width + 16, 0);
    const int scalar_r = slide_scalar(out_scalar.data(), encoded.data(),
                                      static_cast<tjs_int>(encoded.size()),
                                      text_scalar.data(), 0);
    const int simd_r = slide_simd(out_simd.data(), encoded.data(),
                                  static_cast<tjs_int>(encoded.size()),
                                  text_simd.data(), 0);
    if(scalar_r != simd_r || out_scalar != out_simd) {
        std::printf("%-30s parity=FAIL\n", "TLG5 literal slide");
        ++BenchmarkParityFailures;
    } else {
        const double scalar_time = Measure(width, iterations, [&] {
            slide_scalar(out_scalar.data(), encoded.data(),
                         static_cast<tjs_int>(encoded.size()),
                         text_scalar.data(), 0);
        });
        const double simd_time = Measure(width, iterations, [&] {
            slide_simd(out_simd.data(), encoded.data(),
                       static_cast<tjs_int>(encoded.size()),
                       text_simd.data(), 0);
        });
        BenchmarkSink += out_simd[width / 2];
        Report("TLG5 literal slide", scalar_time, simd_time);
    }

    constexpr int blocks = width / 8;
    std::vector<tjs_uint32> previous(width), residual(width);
    std::vector<tjs_uint32> line_ref(width), line_test(width);
    std::vector<tjs_uint8> filters(blocks);
    for(auto &value : previous)
        value = rng();
    for(auto &value : residual)
        value = rng();
    for(auto &value : filters)
        value = static_cast<tjs_uint8>(rng() & 31u);
    line_scalar(previous.data(), line_ref.data(), width, blocks,
                filters.data(), 8, residual.data(), 0, 0, 1);
    line_simd(previous.data(), line_test.data(), width, blocks,
              filters.data(), 8, residual.data(), 0, 0, 1);
    if(line_ref != line_test) {
        std::printf("%-30s parity=FAIL\n", "TLG6 line predictor");
        ++BenchmarkParityFailures;
    } else {
        const double scalar_time = Measure(width, iterations, [&] {
            line_scalar(previous.data(), line_ref.data(), width, blocks,
                        filters.data(), 8, residual.data(), 0, 0, 1);
        });
        const double simd_time = Measure(width, iterations, [&] {
            line_simd(previous.data(), line_test.data(), width, blocks,
                      filters.data(), 8, residual.data(), 0, 0, 1);
        });
        BenchmarkSink += line_test[width / 2];
        Report("TLG6 line predictor", scalar_time, simd_time);
    }
}
#endif

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    TVPInitTVPGL();
    TVPGL_C_Init();
    const auto alpha_scalar = TVPAlphaBlend;
    const auto fill_scalar = TVPFillARGB;
    const auto color_scalar = TVPConstColorAlphaBlend;
    const auto color_d_scalar = TVPConstColorAlphaBlend_d;
    const auto color_a_scalar = TVPConstColorAlphaBlend_a;

    TVPInitCPUFeatures();
#ifdef KRKRZ_TEST_HAS_X86
    TVPGL_SSE2_Init();
#elif defined(KRKRZ_TEST_HAS_NEON)
    TVPGL_NEON_Init();
#endif

    const auto alpha_simd = TVPAlphaBlend;
    const auto fill_simd = TVPFillARGB;
    const auto color_simd = TVPConstColorAlphaBlend;
    const auto color_d_simd = TVPConstColorAlphaBlend_d;
    const auto color_a_simd = TVPConstColorAlphaBlend_a;

    constexpr int pixels = 4096;
    std::mt19937 rng(0x53494d44u);
    std::vector<tjs_uint32> dest(pixels), src(pixels);
    for(auto &value : dest)
        value = rng();
    for(auto &value : src)
        value = rng();

    std::printf("krkrz SIMD microbenchmark (median of 7, CPU=0x%08x)\n",
                static_cast<unsigned>(TVPCPUType));
    BenchmarkFill("FillARGB", fill_scalar, fill_simd, dest);
    BenchmarkBlend("AlphaBlend", alpha_scalar, alpha_simd, dest, src);
    BenchmarkColorBlend("ConstColorAlphaBlend", color_scalar, color_simd,
                        dest);
    BenchmarkColorBlend("ConstColorAlphaBlend_d", color_d_scalar,
                        color_d_simd, dest, false);
    BenchmarkColorBlend("ConstColorAlphaBlend_a", color_a_scalar,
                        color_a_simd, dest, false);
#ifdef KRKRZ_TEST_HAS_X86
    BenchmarkTLG();
#endif
    std::printf("sink=%llu\n",
                static_cast<unsigned long long>(BenchmarkSink));
    return BenchmarkParityFailures == 0 ? 0 : 1;
}
