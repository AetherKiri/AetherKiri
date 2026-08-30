// Runtime CPU feature detection used by optional SIMD dispatchers.
#ifndef AETHER_CPU_FEATURES_H
#define AETHER_CPU_FEATURES_H

enum class TVPCPUFeature {
    SSE2,
    SSSE3,
    AVX2,
    NEON,
};

[[nodiscard]] bool TVPHasCPUFeature(TVPCPUFeature feature);

#endif // AETHER_CPU_FEATURES_H
