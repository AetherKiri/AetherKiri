// Runtime installation of the krkrz TLG SSE2 leaves.  Aether keeps scalar
// defaults and only swaps the five TLG function pointers after CPU detection;
// this makes the optimization transparent to every existing TLG call site.
// The source file is compiled for all targets so the init symbol stays stable,
// but the upstream leaf objects exist only in a single-architecture x86
// build.  The CMake definition below keeps universal ARM/x86 slices linked
// against the scalar implementation without dangling references.
#include "../../tjs2/tjsCommHead.h"
#include "../TLGSIMD.h"
#include "tvpgl.h"
#include "DetectCPU.h"
#include "cpu_types.h"

#if defined(AETHER_KRKRZ_TLG_SIMD_COMPILED) && \
    (defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || \
     defined(__x86_64__))
// The pinned krkrz SSE2 composition leaves follow the upstream BGRA channel
// order.  Aether's scalar TLG registry has historically exposed the swapped
// order required by its RGBA texture boundary.  Keep both paths byte-for-byte
// equivalent by adapting the channel pointers at this narrow dispatch edge;
// the upstream leaf itself remains untouched in the pinned submodule.
static void TVPTLG5ComposeColors3To4_aether_sse2_c(
    tjs_uint8 *outp, const tjs_uint8 *upper, tjs_uint8 *const *buf,
    tjs_int width) {
    tjs_uint8 *swapped[3] = {buf[2], buf[1], buf[0]};
    TVPTLG5ComposeColors3To4_sse2_c(outp, upper, swapped, width);
}

static void TVPTLG5ComposeColors4To4_aether_sse2_c(
    tjs_uint8 *outp, const tjs_uint8 *upper, tjs_uint8 *const *buf,
    tjs_int width) {
    tjs_uint8 *swapped[4] = {buf[2], buf[1], buf[0], buf[3]};
    TVPTLG5ComposeColors4To4_sse2_c(outp, upper, swapped, width);
}

void TVPInitTLGSIMD() {
    TVPDetectCPU();
    if((TVPCPUType & TVP_CPU_HAS_SSE2) == 0)
        return;

    TVPTLG5DecompressSlide = TVPTLG5DecompressSlide_sse2_c;
    TVPTLG5ComposeColors3To4 = TVPTLG5ComposeColors3To4_aether_sse2_c;
    TVPTLG5ComposeColors4To4 = TVPTLG5ComposeColors4To4_aether_sse2_c;
    TVPTLG6DecodeLineGeneric = TVPTLG6DecodeLineGeneric_sse2_c;
    TVPTLG6DecodeLine = TVPTLG6DecodeLine_sse2_c;
}
#else
void TVPInitTLGSIMD() {}
#endif
