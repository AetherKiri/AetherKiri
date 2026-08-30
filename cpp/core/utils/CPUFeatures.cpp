#include "CPUFeatures.h"

#include <mutex>

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include <intrin.h>
#endif

namespace {

struct CPUFeatures {
    bool sse2 = false;
    bool ssse3 = false;
    bool avx2 = false;
    bool neon = false;
};

CPUFeatures DetectCPUFeatures() {
    CPUFeatures features;

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
    int regs[4] = {};
    __cpuid(regs, 1);
    features.sse2 = (regs[3] & (1 << 26)) != 0;
    features.ssse3 = (regs[2] & (1 << 9)) != 0;

    const bool avx = (regs[2] & (1 << 28)) != 0;
    const bool osxsave = (regs[2] & (1 << 27)) != 0;
    bool ymmState = false;
    if(avx && osxsave)
        ymmState = (_xgetbv(0) & 0x6) == 0x6;

    __cpuidex(regs, 7, 0);
    features.avx2 = ymmState && (regs[1] & (1 << 5)) != 0;
#elif (defined(__GNUC__) || defined(__clang__)) && \
    (defined(__i386__) || defined(__x86_64__))
    __builtin_cpu_init();
    features.sse2 = __builtin_cpu_supports("sse2") != 0;
    features.ssse3 = __builtin_cpu_supports("ssse3") != 0;
    features.avx2 = __builtin_cpu_supports("avx2") != 0;
#elif defined(__aarch64__) || defined(_M_ARM64)
    // ARMv8-A mandates Advanced SIMD/NEON.  The compiler emits the NEON
    // instructions only in the NEON-specific translation units.
    features.neon = true;
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
    features.neon = true;
#endif

    return features;
}

const CPUFeatures &GetCPUFeatures() {
    static std::once_flag once;
    static CPUFeatures features;
    std::call_once(once, [] { features = DetectCPUFeatures(); });
    return features;
}

} // namespace

bool TVPHasCPUFeature(TVPCPUFeature feature) {
    const CPUFeatures &features = GetCPUFeatures();
    switch(feature) {
        case TVPCPUFeature::SSE2:
            return features.sse2;
        case TVPCPUFeature::SSSE3:
            return features.ssse3;
        case TVPCPUFeature::AVX2:
            return features.avx2;
        case TVPCPUFeature::NEON:
            return features.neon;
    }
    return false;
}
