/*
 * KrKr2 Engine - Highway SIMD Alpha Blend Implementation
 *
 * Implements TVPAlphaBlend and its 4 variants using Google Highway SIMD.
 * Phase 2 implementation - core alpha blending (most frequently used).
 *
 * FIX: Use signed i16 arithmetic for (s - d) * alpha to avoid u16 underflow.
 */

#include "tjsTypes.h"
#include "tvpgl.h"

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tvpgl_simd_alpha_blend.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();
namespace krkr2 {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

/*
 * Core alpha blend helper: result = (s * a) >> 8 + (d * (255 - a)) >> 8
 * Uses UNSIGNED u16 arithmetic throughout. Each product s*a or d*inv_a
 * fits in u16 (max 255*255=65025 < 65535). After >>8, each term <=254.
 * Sum <=508 fits u16; OrderedDemote2To saturates to 255.
 *
 * IMPORTANT: We must >>8 each product BEFORE adding, to avoid u16 overflow
 * (s*a + d*inv_a can reach 130050 > 65535).
 */
static HWY_INLINE auto BlendChannel(
    hn::ScalableTag<uint16_t> d16,
    hn::Vec<hn::ScalableTag<uint16_t>> s,
    hn::Vec<hn::ScalableTag<uint16_t>> d,
    hn::Vec<hn::ScalableTag<uint16_t>> a)
    -> hn::Vec<hn::ScalableTag<uint16_t>> {
    const auto v255 = hn::Set(d16, static_cast<uint16_t>(255));
    auto inv_a = hn::Sub(v255, a);  // 255 - a
    // (s * a >> 8) + (d * (255 - a) >> 8) — split to avoid u16 overflow.
    // The integer approximation of d*255/256 is one level below d for any
    // non-zero channel.  That is correct for a partially covered source,
    // but not for a==0: a transparent texel is a strict no-op.  Character
    // fade helper layers use transparent white padding over the full message
    // frame, so letting that rounding through creates a visible brightness
    // pulse between lines.
    auto blended = hn::Add(hn::ShiftRight<8>(hn::Mul(s, a)),
                           hn::ShiftRight<8>(hn::Mul(d, inv_a)));
    return hn::IfThenElse(hn::Eq(a, hn::Zero(d16)), d, blended);
}

void AlphaBlend_HWY(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len) {
    const hn::ScalableTag<uint8_t> d8;
    const hn::Repartition<uint16_t, decltype(d8)> d16;
    const size_t N_PIXELS = hn::Lanes(d8) / 4;

    const auto alpha_shuffle = hn::Dup128VecFromValues(d8,
        3, 3, 3, 3,    7, 7, 7, 7,
        11, 11, 11, 11, 15, 15, 15, 15);

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N_PIXELS) <= len; i += N_PIXELS) {
        auto vs = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(src + i));
        auto vd = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(dest + i));
        auto va = hn::TableLookupBytes(vs, alpha_shuffle);

        const auto half = hn::Half<decltype(d8)>();
        auto s_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vs));
        auto d_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vd));
        auto a_lo = hn::PromoteTo(d16, hn::LowerHalf(half, va));
        auto s_hi = hn::PromoteUpperTo(d16, vs);
        auto d_hi = hn::PromoteUpperTo(d16, vd);
        auto a_hi = hn::PromoteUpperTo(d16, va);

        auto r_lo = BlendChannel(d16, s_lo, d_lo, a_lo);
        auto r_hi = BlendChannel(d16, s_hi, d_hi, a_hi);

        auto result = hn::OrderedDemote2To(d8, r_lo, r_hi);
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));
    }
    for (; i < len; i++) {
        tjs_uint32 s = src[i], d = dest[i];
        tjs_uint32 a = s >> 24;
        if(a == 0)
            continue;
        tjs_uint32 d1 = d & 0xff00ff;
        d1 = (d1 + (((s & 0xff00ff) - d1) * a >> 8)) & 0xff00ff;
        tjs_uint32 d2 = d & 0xff00;
        dest[i] = d1 + ((d2 + (((s & 0xff00) - d2) * a >> 8)) & 0xff00);
    }
}

void AlphaBlend_HDA_HWY(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len) {
    const hn::ScalableTag<uint8_t> d8;
    const hn::Repartition<uint16_t, decltype(d8)> d16;
    const size_t N_PIXELS = hn::Lanes(d8) / 4;

    const auto alpha_shuffle = hn::Dup128VecFromValues(d8,
        3, 3, 3, 3,    7, 7, 7, 7,
        11, 11, 11, 11, 15, 15, 15, 15);
    const auto alpha_mask = hn::Dup128VecFromValues(d8,
        0, 0, 0, 0xFF,  0, 0, 0, 0xFF,
        0, 0, 0, 0xFF,  0, 0, 0, 0xFF);

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N_PIXELS) <= len; i += N_PIXELS) {
        auto vs = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(src + i));
        auto vd = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(dest + i));
        auto va = hn::TableLookupBytes(vs, alpha_shuffle);

        const auto half = hn::Half<decltype(d8)>();
        auto r_lo = BlendChannel(d16, hn::PromoteTo(d16, hn::LowerHalf(half, vs)),
                                      hn::PromoteTo(d16, hn::LowerHalf(half, vd)),
                                      hn::PromoteTo(d16, hn::LowerHalf(half, va)));
        auto r_hi = BlendChannel(d16, hn::PromoteUpperTo(d16, vs),
                                      hn::PromoteUpperTo(d16, vd),
                                      hn::PromoteUpperTo(d16, va));

        auto result = hn::OrderedDemote2To(d8, r_lo, r_hi);
        result = hn::Or(hn::AndNot(alpha_mask, result), hn::And(alpha_mask, vd));
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));
    }
    for (; i < len; i++) {
        tjs_uint32 s = src[i], d = dest[i];
        tjs_uint32 a = s >> 24;
        if(a == 0)
            continue;
        tjs_uint32 d1 = d & 0xff00ff;
        d1 = (d1 + (((s & 0xff00ff) - d1) * a >> 8)) & 0xff00ff;
        tjs_uint32 d2 = d & 0xff00;
        dest[i] = d1 + ((d2 + (((s & 0xff00) - d2) * a >> 8)) & 0xff00);
        dest[i] = (dest[i] & 0x00ffffff) | (d & 0xff000000);
    }
}

void AlphaBlend_o_HWY(tjs_uint32 *dest, const tjs_uint32 *src,
                       tjs_int len, tjs_int opa) {
    const hn::ScalableTag<uint8_t> d8;
    const hn::Repartition<uint16_t, decltype(d8)> d16;
    const size_t N_PIXELS = hn::Lanes(d8) / 4;

    const auto alpha_shuffle = hn::Dup128VecFromValues(d8,
        3, 3, 3, 3,    7, 7, 7, 7,
        11, 11, 11, 11, 15, 15, 15, 15);
    const auto vopa16 = hn::Set(d16, static_cast<uint16_t>(opa));

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N_PIXELS) <= len; i += N_PIXELS) {
        auto vs = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(src + i));
        auto vd = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(dest + i));
        auto va_raw = hn::TableLookupBytes(vs, alpha_shuffle);

        const auto half = hn::Half<decltype(d8)>();
        // Scale alpha by opacity
        auto a_lo = hn::ShiftRight<8>(hn::Mul(hn::PromoteTo(d16, hn::LowerHalf(half, va_raw)), vopa16));
        auto a_hi = hn::ShiftRight<8>(hn::Mul(hn::PromoteUpperTo(d16, va_raw), vopa16));

        auto r_lo = BlendChannel(d16, hn::PromoteTo(d16, hn::LowerHalf(half, vs)),
                                      hn::PromoteTo(d16, hn::LowerHalf(half, vd)), a_lo);
        auto r_hi = BlendChannel(d16, hn::PromoteUpperTo(d16, vs),
                                      hn::PromoteUpperTo(d16, vd), a_hi);

        auto result = hn::OrderedDemote2To(d8, r_lo, r_hi);
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));
    }
    for (; i < len; i++) {
        tjs_uint32 s = src[i], d = dest[i];
        tjs_uint32 a = ((s >> 24) * opa) >> 8;
        if(a == 0)
            continue;
        tjs_uint32 d1 = d & 0xff00ff;
        d1 = (d1 + (((s & 0xff00ff) - d1) * a >> 8)) & 0xff00ff;
        tjs_uint32 d2 = d & 0xff00;
        dest[i] = d1 + ((d2 + (((s & 0xff00) - d2) * a >> 8)) & 0xff00);
    }
}

void AlphaBlend_HDA_o_HWY(tjs_uint32 *dest, const tjs_uint32 *src,
                           tjs_int len, tjs_int opa) {
    const hn::ScalableTag<uint8_t> d8;
    const hn::Repartition<uint16_t, decltype(d8)> d16;
    const size_t N_PIXELS = hn::Lanes(d8) / 4;

    const auto alpha_shuffle = hn::Dup128VecFromValues(d8,
        3, 3, 3, 3,    7, 7, 7, 7,
        11, 11, 11, 11, 15, 15, 15, 15);
    const auto alpha_mask = hn::Dup128VecFromValues(d8,
        0, 0, 0, 0xFF,  0, 0, 0, 0xFF,
        0, 0, 0, 0xFF,  0, 0, 0, 0xFF);
    const auto vopa16 = hn::Set(d16, static_cast<uint16_t>(opa));

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N_PIXELS) <= len; i += N_PIXELS) {
        auto vs = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(src + i));
        auto vd = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(dest + i));
        auto va_raw = hn::TableLookupBytes(vs, alpha_shuffle);

        const auto half = hn::Half<decltype(d8)>();
        auto a_lo = hn::ShiftRight<8>(hn::Mul(hn::PromoteTo(d16, hn::LowerHalf(half, va_raw)), vopa16));
        auto a_hi = hn::ShiftRight<8>(hn::Mul(hn::PromoteUpperTo(d16, va_raw), vopa16));

        auto r_lo = BlendChannel(d16, hn::PromoteTo(d16, hn::LowerHalf(half, vs)),
                                      hn::PromoteTo(d16, hn::LowerHalf(half, vd)), a_lo);
        auto r_hi = BlendChannel(d16, hn::PromoteUpperTo(d16, vs),
                                      hn::PromoteUpperTo(d16, vd), a_hi);

        auto result = hn::OrderedDemote2To(d8, r_lo, r_hi);
        result = hn::Or(hn::AndNot(alpha_mask, result), hn::And(alpha_mask, vd));
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));
    }
    for (; i < len; i++) {
        tjs_uint32 s = src[i], d = dest[i];
        tjs_uint32 a = ((s >> 24) * opa) >> 8;
        if(a == 0)
            continue;
        tjs_uint32 d1 = d & 0xff00ff;
        d1 = (d1 + (((s & 0xff00ff) - d1) * a >> 8)) & 0xff00ff;
        tjs_uint32 d2 = d & 0xff00;
        dest[i] = d1 + ((d2 + (((s & 0xff00) - d2) * a >> 8)) & 0xff00);
        dest[i] = (dest[i] & 0x00ffffff) | (d & 0xff000000);
    }
}

}  // namespace HWY_NAMESPACE
}  // namespace krkr2
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace krkr2 {
HWY_EXPORT(AlphaBlend_HWY);
HWY_EXPORT(AlphaBlend_HDA_HWY);
HWY_EXPORT(AlphaBlend_o_HWY);
HWY_EXPORT(AlphaBlend_HDA_o_HWY);
}  // namespace krkr2

extern "C" {
void TVPAlphaBlend_hwy(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len) {
    krkr2::HWY_DYNAMIC_DISPATCH(AlphaBlend_HWY)(dest, src, len);
}
void TVPAlphaBlend_HDA_hwy(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len) {
    krkr2::HWY_DYNAMIC_DISPATCH(AlphaBlend_HDA_HWY)(dest, src, len);
}
void TVPAlphaBlend_o_hwy(tjs_uint32 *dest, const tjs_uint32 *src,
                          tjs_int len, tjs_int opa) {
    krkr2::HWY_DYNAMIC_DISPATCH(AlphaBlend_o_HWY)(dest, src, len, opa);
}
void TVPAlphaBlend_HDA_o_hwy(tjs_uint32 *dest, const tjs_uint32 *src,
                              tjs_int len, tjs_int opa) {
    krkr2::HWY_DYNAMIC_DISPATCH(AlphaBlend_HDA_o_HWY)(dest, src, len, opa);
}
}  // extern "C"
#endif
