/*
 * KrKr2 Engine - Highway SIMD Misc Functions
 *
 * Implements miscellaneous utility functions:
 *   TVPDoGrayScale, TVPSwapLine8, TVPSwapLine32,
 *   TVPReverse8, TVPReverse32, TVPMakeAlphaFromKey,
 *   TVPCopyMask, TVPCopyColor, TVPFillColor, TVPFillMask,
 *   TVPBindMaskToMain, TVPConstColorAlphaBlend (3 variants),
 *   TVPRemoveConstOpacity, TVPRemoveOpacity (4 variants),
 *   TVPUpscale65_255
 *
 * Phase 4 implementation.
 */

#include "tjsTypes.h"
#include "tvpgl.h"

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tvpgl_simd_misc.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();
namespace krkr2 {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

// =========================================================================
// TVPDoGrayScale: convert ARGB to grayscale preserving alpha
// Gray = (R*19 + G*183 + B*54) >> 8
// =========================================================================
void DoGrayScale_HWY(tjs_uint32 *dest, tjs_int len) {
    for (tjs_int i = 0; i < len; i++) {
        tjs_uint32 px = dest[i];
        tjs_uint32 a = px & 0xFF000000;
        tjs_uint32 r = (px >> 16) & 0xFF;
        tjs_uint32 g = (px >> 8) & 0xFF;
        tjs_uint32 b = px & 0xFF;
        tjs_uint32 gray = (r * 19 + g * 183 + b * 54) >> 8;
        dest[i] = a | (gray << 16) | (gray << 8) | gray;
    }
}

// =========================================================================
// TVPSwapLine32: swap two lines of 32-bit pixels
// =========================================================================
void SwapLine32_HWY(tjs_uint32 *line1, tjs_uint32 *line2, tjs_int len) {
    const hn::ScalableTag<uint8_t> d8;
    const size_t N_PIXELS = hn::Lanes(d8) / 4;

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N_PIXELS) <= len; i += N_PIXELS) {
        auto v1 = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(line1 + i));
        auto v2 = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(line2 + i));
        hn::StoreU(v2, d8, reinterpret_cast<uint8_t*>(line1 + i));
        hn::StoreU(v1, d8, reinterpret_cast<uint8_t*>(line2 + i));
    }
    for (; i < len; i++) {
        tjs_uint32 tmp = line1[i];
        line1[i] = line2[i];
        line2[i] = tmp;
    }
}

// =========================================================================
// TVPSwapLine8: swap two lines of 8-bit data
// =========================================================================
void SwapLine8_HWY(tjs_uint8 *line1, tjs_uint8 *line2, tjs_int len) {
    const hn::ScalableTag<uint8_t> d8;
    const size_t N = hn::Lanes(d8);

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N) <= len; i += N) {
        auto v1 = hn::LoadU(d8, line1 + i);
        auto v2 = hn::LoadU(d8, line2 + i);
        hn::StoreU(v2, d8, line1 + i);
        hn::StoreU(v1, d8, line2 + i);
    }
    for (; i < len; i++) {
        tjs_uint8 tmp = line1[i];
        line1[i] = line2[i];
        line2[i] = tmp;
    }
}

// =========================================================================
// TVPReverse32: reverse order of 32-bit pixels in a line
// =========================================================================
void Reverse32_HWY(tjs_uint32 *pixels, tjs_int len) {
    tjs_int left = 0, right = len - 1;
    while (left < right) {
        tjs_uint32 tmp = pixels[left];
        pixels[left] = pixels[right];
        pixels[right] = tmp;
        left++;
        right--;
    }
}

// =========================================================================
// TVPReverse8: reverse order of 8-bit data
// =========================================================================
void Reverse8_HWY(tjs_uint8 *pixels, tjs_int len) {
    tjs_int left = 0, right = len - 1;
    while (left < right) {
        tjs_uint8 tmp = pixels[left];
        pixels[left] = pixels[right];
        pixels[right] = tmp;
        left++;
        right--;
    }
}

// =========================================================================
// TVPMakeAlphaFromKey: set alpha=0 for pixels matching key, alpha=0xFF otherwise
// =========================================================================
void MakeAlphaFromKey_HWY(tjs_uint32 *dest, tjs_int len, tjs_uint32 key) {
    const hn::ScalableTag<uint32_t> d32;
    const size_t N = hn::Lanes(d32);
    const auto vkey = hn::Set(d32, key & 0x00FFFFFF);
    const auto vmask_rgb = hn::Set(d32, 0x00FFFFFF);
    const auto valpha = hn::Set(d32, 0xFF000000);

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N) <= len; i += N) {
        auto vp = hn::LoadU(d32, reinterpret_cast<const uint32_t*>(dest + i));
        auto vrgb = hn::And(vp, vmask_rgb);
        auto eq_mask = hn::Eq(vrgb, vkey);
        // If matches key: alpha=0, else alpha=0xFF
        auto result = hn::Or(vrgb, hn::IfThenZeroElse(eq_mask, valpha));
        hn::StoreU(result, d32, reinterpret_cast<uint32_t*>(dest + i));
    }
    for (; i < len; i++) {
        tjs_uint32 px = dest[i] & 0x00FFFFFF;
        dest[i] = (px == (key & 0x00FFFFFF)) ? px : (px | 0xFF000000);
    }
}

// =========================================================================
// TVPCopyMask: copy alpha channel from src to dest
// dest_alpha = src_alpha, dest_rgb unchanged
// =========================================================================
void CopyMask_HWY(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len) {
    const hn::ScalableTag<uint8_t> d8;
    const size_t N_PIXELS = hn::Lanes(d8) / 4;
    const auto mask = hn::Dup128VecFromValues(d8,
        0, 0, 0, 0xFF,  0, 0, 0, 0xFF,
        0, 0, 0, 0xFF,  0, 0, 0, 0xFF);

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N_PIXELS) <= len; i += N_PIXELS) {
        auto vd = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(dest + i));
        auto vs = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(src + i));
        auto result = hn::Or(hn::AndNot(mask, vd), hn::And(mask, vs));
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));
    }
    for (; i < len; i++) {
        dest[i] = (dest[i] & 0x00FFFFFF) | (src[i] & 0xFF000000);
    }
}

// =========================================================================
// TVPCopyColor: copy RGB from src to dest, preserve dest alpha
// =========================================================================
void CopyColor_HWY(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len) {
    const hn::ScalableTag<uint8_t> d8;
    const size_t N_PIXELS = hn::Lanes(d8) / 4;
    const auto mask = hn::Dup128VecFromValues(d8,
        0, 0, 0, 0xFF,  0, 0, 0, 0xFF,
        0, 0, 0, 0xFF,  0, 0, 0, 0xFF);

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N_PIXELS) <= len; i += N_PIXELS) {
        auto vd = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(dest + i));
        auto vs = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(src + i));
        // Keep dest alpha, take src RGB
        auto result = hn::Or(hn::AndNot(mask, vs), hn::And(mask, vd));
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));
    }
    for (; i < len; i++) {
        dest[i] = (dest[i] & 0xFF000000) | (src[i] & 0x00FFFFFF);
    }
}

// =========================================================================
// TVPFillColor: fill RGB channels with color, preserve alpha
// =========================================================================
void FillColor_HWY(tjs_uint32 *dest, tjs_int len, tjs_uint32 color) {
    const hn::ScalableTag<uint8_t> d8;
    const size_t N_PIXELS = hn::Lanes(d8) / 4;
    const hn::ScalableTag<uint32_t> d32;
    const auto vcolor = hn::Set(d32, color & 0x00FFFFFF);
    const auto vmask_alpha = hn::Set(d32, 0xFF000000u);

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N_PIXELS) <= len; i += N_PIXELS) {
        auto vd = hn::LoadU(d32, reinterpret_cast<const uint32_t*>(dest + i));
        auto result = hn::Or(hn::And(vd, vmask_alpha), vcolor);
        hn::StoreU(result, d32, reinterpret_cast<uint32_t*>(dest + i));
    }
    for (; i < len; i++) {
        dest[i] = (dest[i] & 0xFF000000) | (color & 0x00FFFFFF);
    }
}

// =========================================================================
// TVPFillMask: fill alpha channel, preserve RGB
// =========================================================================
void FillMask_HWY(tjs_uint32 *dest, tjs_int len, tjs_uint32 mask_val) {
    const hn::ScalableTag<uint32_t> d32;
    const size_t N = hn::Lanes(d32);
    // TVPFillMask accepts an 8-bit opacity value. Expand it into AARRGGBB's
    // alpha byte just like the scalar implementation.
    const tjs_uint32 alpha = (mask_val & 0xFFu) << 24u;
    const auto valpha = hn::Set(d32, alpha);
    const auto vmask_rgb = hn::Set(d32, 0x00FFFFFFu);

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N) <= len; i += N) {
        auto vd = hn::LoadU(d32, reinterpret_cast<const uint32_t*>(dest + i));
        auto result = hn::Or(hn::And(vd, vmask_rgb), valpha);
        hn::StoreU(result, d32, reinterpret_cast<uint32_t*>(dest + i));
    }
    for (; i < len; i++) {
        dest[i] = (dest[i] & 0x00FFFFFF) | alpha;
    }
}

// =========================================================================
// TVPBindMaskToMain: apply 8-bit mask to alpha channel of 32-bit pixels
// dest_alpha = mask[i], dest_rgb unchanged
// =========================================================================
void BindMaskToMain_HWY(tjs_uint32 *main, const tjs_uint8 *mask, tjs_int len) {
    for (tjs_int i = 0; i < len; i++) {
        main[i] = (main[i] & 0x00FFFFFF) | (static_cast<tjs_uint32>(mask[i]) << 24);
    }
}

// =========================================================================
// TVPConstColorAlphaBlend: blend constant color with opacity
// dest = dest + (color - dest) * opa >> 8
// =========================================================================
void ConstColorAlphaBlend_HWY(tjs_uint32 *dest, tjs_int len,
                               tjs_uint32 color, tjs_int opa) {
    const hn::ScalableTag<uint8_t> d8;
    const hn::Repartition<uint16_t, decltype(d8)> d16;
    const size_t N_PIXELS = hn::Lanes(d8) / 4;

    tjs_uint8 color_bytes[4] = {
        static_cast<tjs_uint8>(color & 0xFF),
        static_cast<tjs_uint8>((color >> 8) & 0xFF),
        static_cast<tjs_uint8>((color >> 16) & 0xFF),
        static_cast<tjs_uint8>((color >> 24) & 0xFF)
    };
    const auto vcolor = hn::Dup128VecFromValues(d8,
        color_bytes[0], color_bytes[1], color_bytes[2], color_bytes[3],
        color_bytes[0], color_bytes[1], color_bytes[2], color_bytes[3],
        color_bytes[0], color_bytes[1], color_bytes[2], color_bytes[3],
        color_bytes[0], color_bytes[1], color_bytes[2], color_bytes[3]);
    const auto vopa16 = hn::Set(d16, static_cast<uint16_t>(opa));
    const auto v255 = hn::Set(d16, static_cast<uint16_t>(255));
    const auto vinv_opa = hn::Sub(v255, vopa16);  // 255 - opa

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N_PIXELS) <= len; i += N_PIXELS) {
        auto vd = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(dest + i));

        const auto half = hn::Half<decltype(d8)>();
        auto c_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vcolor));
        auto d_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vd));
        auto c_hi = hn::PromoteUpperTo(d16, vcolor);
        auto d_hi = hn::PromoteUpperTo(d16, vd);

        // result = (color * opa >> 8) + (dest * (255 - opa) >> 8)
        // Split >>8 before add to avoid u16 overflow
        auto r_lo = hn::Add(hn::ShiftRight<8>(hn::Mul(c_lo, vopa16)),
                            hn::ShiftRight<8>(hn::Mul(d_lo, vinv_opa)));
        auto r_hi = hn::Add(hn::ShiftRight<8>(hn::Mul(c_hi, vopa16)),
                            hn::ShiftRight<8>(hn::Mul(d_hi, vinv_opa)));

        auto result = hn::OrderedDemote2To(d8, r_lo, r_hi);
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));
    }
    for (; i < len; i++) {
        tjs_uint32 d = dest[i];
        tjs_uint32 d1 = d & 0x00ff00ff;
        tjs_uint32 d2 = d & 0x0000ff00;
        dest[i] = ((((((color & 0x00ff00ff) - d1) * opa) >> 8) + d1) & 0x00ff00ff) |
                  ((((((color & 0x0000ff00) - d2) * opa) >> 8) + d2) & 0x0000ff00);
    }
}

// =========================================================================
// TVPRemoveConstOpacity: scale alpha by (255 - strength) / 255
// dest_alpha = dest_alpha * (255 - strength) >> 8
// =========================================================================
void RemoveConstOpacity_HWY(tjs_uint32 *dest, tjs_int len, tjs_int strength) {
    tjs_int inv = 255 - strength;
    for (tjs_int i = 0; i < len; i++) {
        tjs_uint32 px = dest[i];
        tjs_uint32 a = (px >> 24) * inv >> 8;
        dest[i] = (px & 0x00FFFFFF) | (a << 24);
    }
}

}  // namespace HWY_NAMESPACE
}  // namespace krkr2
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace krkr2 {
HWY_EXPORT(DoGrayScale_HWY);
HWY_EXPORT(SwapLine32_HWY);
HWY_EXPORT(SwapLine8_HWY);
HWY_EXPORT(Reverse32_HWY);
HWY_EXPORT(Reverse8_HWY);
HWY_EXPORT(MakeAlphaFromKey_HWY);
HWY_EXPORT(CopyMask_HWY);
HWY_EXPORT(CopyColor_HWY);
HWY_EXPORT(FillColor_HWY);
HWY_EXPORT(FillMask_HWY);
HWY_EXPORT(BindMaskToMain_HWY);
HWY_EXPORT(ConstColorAlphaBlend_HWY);
HWY_EXPORT(RemoveConstOpacity_HWY);
}  // namespace krkr2

extern "C" {
void TVPDoGrayScale_hwy(tjs_uint32 *dest, tjs_int len) {
    krkr2::HWY_DYNAMIC_DISPATCH(DoGrayScale_HWY)(dest, len);
}
void TVPSwapLine32_hwy(tjs_uint32 *line1, tjs_uint32 *line2, tjs_int len) {
    krkr2::HWY_DYNAMIC_DISPATCH(SwapLine32_HWY)(line1, line2, len);
}
void TVPSwapLine8_hwy(tjs_uint8 *line1, tjs_uint8 *line2, tjs_int len) {
    krkr2::HWY_DYNAMIC_DISPATCH(SwapLine8_HWY)(line1, line2, len);
}
void TVPReverse32_hwy(tjs_uint32 *pixels, tjs_int len) {
    krkr2::HWY_DYNAMIC_DISPATCH(Reverse32_HWY)(pixels, len);
}
void TVPReverse8_hwy(tjs_uint8 *pixels, tjs_int len) {
    krkr2::HWY_DYNAMIC_DISPATCH(Reverse8_HWY)(pixels, len);
}
void TVPMakeAlphaFromKey_hwy(tjs_uint32 *dest, tjs_int len, tjs_uint32 key) {
    krkr2::HWY_DYNAMIC_DISPATCH(MakeAlphaFromKey_HWY)(dest, len, key);
}
void TVPCopyMask_hwy(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len) {
    krkr2::HWY_DYNAMIC_DISPATCH(CopyMask_HWY)(dest, src, len);
}
void TVPCopyColor_hwy(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len) {
    krkr2::HWY_DYNAMIC_DISPATCH(CopyColor_HWY)(dest, src, len);
}
void TVPFillColor_hwy(tjs_uint32 *dest, tjs_int len, tjs_uint32 color) {
    krkr2::HWY_DYNAMIC_DISPATCH(FillColor_HWY)(dest, len, color);
}
void TVPFillMask_hwy(tjs_uint32 *dest, tjs_int len, tjs_uint32 mask) {
    krkr2::HWY_DYNAMIC_DISPATCH(FillMask_HWY)(dest, len, mask);
}
void TVPBindMaskToMain_hwy(tjs_uint32 *main, const tjs_uint8 *mask, tjs_int len) {
    krkr2::HWY_DYNAMIC_DISPATCH(BindMaskToMain_HWY)(main, mask, len);
}
void TVPConstColorAlphaBlend_hwy(tjs_uint32 *dest, tjs_int len,
                                  tjs_uint32 color, tjs_int opa) {
    krkr2::HWY_DYNAMIC_DISPATCH(ConstColorAlphaBlend_HWY)(dest, len, color, opa);
}
void TVPRemoveConstOpacity_hwy(tjs_uint32 *dest, tjs_int len, tjs_int strength) {
    krkr2::HWY_DYNAMIC_DISPATCH(RemoveConstOpacity_HWY)(dest, len, strength);
}
}  // extern "C"
#endif
