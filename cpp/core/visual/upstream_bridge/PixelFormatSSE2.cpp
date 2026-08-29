// Source bridge for krkrz's SSE2 24-bit-to-32-bit conversion leaf.
#include "../../tjs2/tjsCommHead.h"
#include "../tvpgl.h"

#include <cstddef>

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || \
    defined(__x86_64__)
#define TVPConvert24BitTo32Bit_sse2_c AetherKrkrzConvert24BitTo32BitSSE2
#include "../../../../third_party/krkrz_dev/src/core/common/visual/gl/pixelformat_sse2.cpp"
#undef TVPConvert24BitTo32Bit_sse2_c

void TVPConvert24BitTo32Bit_sse2_c(tjs_uint32 *dest, const tjs_uint8 *buf,
                                   tjs_int len) {
    if(len <= 0 || !dest || !buf)
        return;

    // The upstream SSE2 leaf is BGR-oriented; Aether's public helper is
    // RGB-oriented.  Reverse each bounded 16-pixel block before handing it
    // to the upstream SIMD kernel, then use the exact scalar contract for the
    // tail.
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
        AetherKrkrzConvert24BitTo32BitSSE2(dest + offset, bgr, 16);
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
