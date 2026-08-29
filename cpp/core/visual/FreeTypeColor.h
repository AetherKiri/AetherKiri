#pragma once

#include "tjsTypes.h"

#include <algorithm>

// FreeType's FT_PIXEL_MODE_BGRA uses premultiplied BGRA bytes.  Aether's
// software bitmap and texture contracts use straight-alpha RGBA bytes
// (little-endian pixels therefore read as 0xAABBGGRR).  Keep this conversion
// in a small, host-independent helper so the rasterizer and its contract test
// cannot silently drift apart.
namespace krkr::font {

inline tjs_uint8 UnpremultiplyChannel(tjs_uint8 value, tjs_uint8 alpha) {
    if(alpha == 0)
        return 0;
    if(alpha == 255)
        return value;
    const tjs_uint value8 =
        (static_cast<tjs_uint>(value) * 255u + alpha / 2u) / alpha;
    return static_cast<tjs_uint8>(std::min<tjs_uint>(255u, value8));
}

inline void ConvertFreeTypeBGRAPixel(const tjs_uint8 *src, tjs_uint8 *dst) {
    if(!src || !dst)
        return;
    const tjs_uint8 alpha = src[3];
    dst[0] = UnpremultiplyChannel(src[2], alpha);
    dst[1] = UnpremultiplyChannel(src[1], alpha);
    dst[2] = UnpremultiplyChannel(src[0], alpha);
    dst[3] = alpha;
}

} // namespace krkr::font
