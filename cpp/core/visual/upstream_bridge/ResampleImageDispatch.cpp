// Runtime dispatch for the krkrz image-resampling leaves.
#include "../../tjs2/tjsCommHead.h"
#include "ResampleImageSIMD.h"
#include "../../environ/DetectCPU.h"
#include "../../environ/cpu_types.h"

#include <cstdlib>
#include <cstring>
#include <mutex>

#if defined(AETHER_KRKRZ_RESAMPLE_SIMD_COMPILED) && \
    (defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || \
     defined(__x86_64__))
extern void TVPInitializeResampleSSE2();
extern void TVPResampleImageSSE2(
    const tTVPResampleClipping &, const tTVPImageCopyFuncBase *,
    iTVPBaseBitmap *, const tTVPRect &, const iTVPBaseBitmap *,
    const tTVPRect &, tTVPBBStretchType, tjs_real);
extern void TVPInitializeResampleAVX2();
extern void TVPResampleImageAVX2(
    const tTVPResampleClipping &, const tTVPImageCopyFuncBase *,
    iTVPBaseBitmap *, const tTVPRect &, const iTVPBaseBitmap *,
    const tTVPRect &, tTVPBBStretchType, tjs_real);
#endif

namespace {

tTVPResampleImageSIMDFunc g_resampler = nullptr;
std::once_flag g_init_once;

bool envAllowsSIMD() {
    const char *value = std::getenv("AETHERKIRI_TVPGL_SIMD");
    if (!value || !*value) return true;
    return std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0 ||
           std::strcmp(value, "on") == 0 || std::strcmp(value, "yes") == 0;
}

// The krkrz leaves intentionally implement the high-quality/fixed-point
// filters only. In particular, stFastLinear is an Aether scalar mode and
// must not be sent to the upstream switch (which would throw). Keep this
// table next to the dispatch boundary so a future upstream filter addition
// cannot accidentally change the scalar compatibility contract.
bool supportsUpstreamFilter(tTVPBBStretchType requested) {
    const auto type = static_cast<tTVPBBStretchType>(
        static_cast<int>(requested) & static_cast<int>(stTypeMask));
    switch (type) {
        case stLinear:
        case stCubic:
        case stSemiFastLinear:
        case stFastCubic:
        case stLanczos2:
        case stFastLanczos2:
        case stLanczos3:
        case stFastLanczos3:
        case stSpline16:
        case stFastSpline16:
        case stSpline36:
        case stFastSpline36:
        case stAreaAvg:
        case stFastAreaAvg:
        case stGaussian:
        case stFastGaussian:
        case stBlackmanSinc:
        case stFastBlackmanSinc:
            return true;
        default:
            return false;
    }
}

} // namespace

void TVPInitResampleSIMD() {
    std::call_once(g_init_once, [] {
        if (!envAllowsSIMD()) return;

#if defined(AETHER_KRKRZ_RESAMPLE_SIMD_COMPILED) && \
    (defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || \
     defined(__x86_64__))
        TVPDetectCPU();
        // AVX2 is selected first because its source is compiled separately
        // with -mavx2.  A CPU without AVX2 falls back to the SSE2 leaf, then
        // scalar.  TVP_CPU_HAS_AVX2 uses krkrz's 0x80000000 bit.
        if ((TVPCPUType & TVP_CPU_HAS_AVX2) != 0) {
            TVPInitializeResampleAVX2();
            g_resampler = TVPResampleImageAVX2;
        } else if ((TVPCPUType & TVP_CPU_HAS_SSE2) != 0) {
            TVPInitializeResampleSSE2();
            g_resampler = TVPResampleImageSSE2;
        }
#endif
    });
}

bool TVPResampleImageSIMD(const tTVPResampleClipping &clip,
                          const tTVPImageCopyFuncBase *blendfunc,
                          iTVPBaseBitmap *dest,
                          const tTVPRect &destrect,
                          const iTVPBaseBitmap *src,
                          const tTVPRect &srcrect,
    tTVPBBStretchType type,
    tjs_real typeopt) {
    TVPInitResampleSIMD();
    if (!g_resampler) return false;
    // Keep all scalar-only and malformed calls on the existing Aether path.
    // This also avoids dereferencing null bitmap owners in contract tests and
    // preserves overlap semantics for in-place resampling.
    if (!dest || !src || dest == src || !supportsUpstreamFilter(type))
        return false;
    if (dest->GetBPP() != 32 || src->GetBPP() != 32 ||
        clip.getDestWidth() <= 0 || clip.getDestHeight() <= 0)
        return false;

    // The SIMD leaf is a scanline algorithm.  Probe the same read/write
    // contract that it will use so compressed, GPU-only, or custom bitmap
    // owners can decline cleanly and let the existing scalar/render path
    // decide how to materialize pixels.  The probe also materializes a
    // compressed source once, avoiding a deferred assertion in worker tasks.
    if (!dest->GetScanLineForWrite(0) || !src->GetScanLine(0))
        return false;

    // Upstream's switch does not understand stRefNoClip (or future flags),
    // while the clipping object already carries the effective destination
    // region. Strip only the flag bits at the ABI boundary.
    const auto base_type = static_cast<tTVPBBStretchType>(
        static_cast<int>(type) & static_cast<int>(stTypeMask));
    g_resampler(clip, blendfunc, dest, destrect, src, srcrect, base_type,
                typeopt);
    return true;
}
