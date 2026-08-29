// Source bridge for krkrz's ARM NEON 24-bit-to-32-bit conversion leaf.
#include "../../tjs2/tjsCommHead.h"
#include "../tvpgl.h"

#include <cstddef>

#if defined(__aarch64__) || defined(__arm64__) || defined(__ARM_NEON) || \
    defined(__ARM_NEON__)
// krkrz's leaf consumes BGR triplets, while Aether's long-standing public
// TVPConvert24BitTo32Bit contract consumes RGB triplets.  Keep the upstream
// SIMD implementation compiled under a private name and put the byte-order
// adaptation at this boundary instead of changing either public ABI.
#define TVPConvert24BitTo32Bit_neon_c AetherKrkrzConvert24BitTo32BitNEON
#include "../../../../third_party/krkrz_dev/src/core/common/visual/gl/pixelformat_neon.cpp"
#undef TVPConvert24BitTo32Bit_neon_c

void TVPConvert24BitTo32Bit_neon_c(tjs_uint32 *dest, const tjs_uint8 *buf,
                                   tjs_int len) {
    if(len <= 0 || !dest || !buf)
        return;

    // Adapt one upstream SIMD block at a time.  The temporary is bounded and
    // keeps the hot conversion itself in krkrz's NEON implementation.
    tjs_uint8 bgr[48];
    const tjs_int blocks = (len >> 4) << 4;
    for(tjs_int offset = 0; offset < blocks; offset += 16) {
        const tjs_uint8 *source = buf + static_cast<std::size_t>(offset) * 3u;
        for(tjs_int pixel = 0; pixel < 16; ++pixel) {
            const tjs_uint8 *rgb = source + static_cast<std::size_t>(pixel) * 3u;
            tjs_uint8 *bgrPixel = bgr + static_cast<std::size_t>(pixel) * 3u;
            bgrPixel[0] = rgb[2];
            bgrPixel[1] = rgb[1];
            bgrPixel[2] = rgb[0];
        }
        AetherKrkrzConvert24BitTo32BitNEON(
            dest + offset, bgr, 16);
    }

    dest += blocks;
    buf += static_cast<std::size_t>(blocks) * 3u;
    for(tjs_int tail = len - blocks; tail > 0; --tail) {
        *dest++ = 0xff000000u | (static_cast<tjs_uint32>(buf[0]) << 16) |
                  (static_cast<tjs_uint32>(buf[1]) << 8) |
                  static_cast<tjs_uint32>(buf[2]);
        buf += 3;
    }
}
#endif
