// Source bridge for krkrz's SSE2 color-map leaves.  The upstream file also
// contains optional character-blur entry points whose C++ callback names do
// not match Aether's C-linkage owner; the local aliases below keep those
// helpers linkable without importing a second blur implementation.
#include "../../tjs2/tjsCommHead.h"
#include "../tvpgl.h"

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || \
    defined(__x86_64__)

extern "C" {
void TVP_ch_blur_copy(tjs_uint8 *, tjs_int, tjs_int, tjs_int,
                      const tjs_uint8 *, tjs_int, tjs_int, tjs_int,
                      tjs_int, tjs_int);
void TVP_ch_blur_copy65(tjs_uint8 *, tjs_int, tjs_int, tjs_int,
                        const tjs_uint8 *, tjs_int, tjs_int, tjs_int,
                        tjs_int, tjs_int);
}

static void AetherKrkrzTVPChBlurCopy(
    tjs_uint8 *dest, tjs_int destpitch, tjs_int destwidth, tjs_int destheight,
    const tjs_uint8 *src, tjs_int srcpitch, tjs_int srcwidth,
    tjs_int srcheight, tjs_int blurwidth, tjs_int blurlevel) {
    TVP_ch_blur_copy(dest, destpitch, destwidth, destheight, src, srcpitch,
                     srcwidth, srcheight, blurwidth, blurlevel);
}

static void AetherKrkrzTVPChBlurCopy65(
    tjs_uint8 *dest, tjs_int destpitch, tjs_int destwidth, tjs_int destheight,
    const tjs_uint8 *src, tjs_int srcpitch, tjs_int srcwidth,
    tjs_int srcheight, tjs_int blurwidth, tjs_int blurlevel) {
    TVP_ch_blur_copy65(dest, destpitch, destwidth, destheight, src, srcpitch,
                       srcwidth, srcheight, blurwidth, blurlevel);
}

#define TVP_ch_blur_copy AetherKrkrzTVPChBlurCopy
#define TVP_ch_blur_copy65 AetherKrkrzTVPChBlurCopy65
#include "../../../../third_party/krkrz_dev/src/core/common/visual/gl/colormap_sse2.cpp"
#undef TVP_ch_blur_copy65
#undef TVP_ch_blur_copy

#endif
