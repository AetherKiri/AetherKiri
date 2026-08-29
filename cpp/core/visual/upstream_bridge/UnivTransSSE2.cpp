// Source bridge for krkrz's SSE2 universal-transition leaves.  Aether keeps
// the transition table/storage owner and normalizes the two byte-level
// differences in the upstream kernels: the non-alpha path must clear the
// destination alpha byte, while the alpha path uses Aether's exact integer
// alpha interpolation (the upstream SIMD approximation is one LSB different
// for some inputs).
#include "../../tjs2/tjsCommHead.h"
#include "../tvpgl.h"

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || \
    defined(__x86_64__)

// Keep the upstream entry points private to this translation unit.  The
// public names below are Aether ABI adapters, so no second global dispatch
// table or duplicate symbol can be introduced by the submodule.
#define TVPInitUnivTransBlendTable_sse2_c \
    AetherKrkrzInitUnivTransBlendTableSSE2
#define TVPInitUnivTransBlendTable_d_sse2_c \
    AetherKrkrzInitUnivTransBlendTableDSSE2
#define TVPUnivTransBlend_sse2_c AetherKrkrzUnivTransBlendSSE2
#define TVPUnivTransBlend_switch_sse2_c AetherKrkrzUnivTransBlendSwitchSSE2
#define TVPUnivTransBlend_d_sse2_c AetherKrkrzUnivTransBlendDSSE2
#define TVPUnivTransBlend_switch_d_sse2_c \
    AetherKrkrzUnivTransBlendSwitchDSSE2
#include "../../../../third_party/krkrz_dev/src/core/common/visual/gl/univtrans_sse2.cpp"
#undef TVPUnivTransBlend_switch_d_sse2_c
#undef TVPUnivTransBlend_d_sse2_c
#undef TVPUnivTransBlend_switch_sse2_c
#undef TVPUnivTransBlend_sse2_c
#undef TVPInitUnivTransBlendTable_d_sse2_c
#undef TVPInitUnivTransBlendTable_sse2_c

namespace {

inline void normalize_non_alpha(tjs_uint32 *dest, tjs_int len) {
    for(tjs_int i = 0; i < len; ++i)
        dest[i] &= 0x00ffffffu;
}

inline void normalize_alpha(tjs_uint32 *dest, const tjs_uint32 *src1,
                            const tjs_uint32 *src2, const tjs_uint8 *rule,
                            const tjs_uint32 *table, tjs_int len) {
    for(tjs_int i = 0; i < len; ++i) {
        const tjs_int a1 = static_cast<tjs_int>(src1[i] >> 24);
        const tjs_int a2 = static_cast<tjs_int>(src2[i] >> 24);
        const tjs_int opa = static_cast<tjs_int>(table[rule[i]]);
        // Keep the interpolation signed.  With a2 < a1 an unsigned
        // subtraction wraps before the shift; that happens to preserve the
        // low byte on most inputs, but makes the contract dependent on
        // modulo arithmetic and can produce a wrong result if the table or
        // pixel format is widened in the future.
        const tjs_int alpha = a1 + ((a2 - a1) * opa >> 8);
        // The upstream kernel already computed the RGB channels.  Replace
        // only alpha, preserving its SIMD work and Aether's pixel semantics.
        dest[i] = (dest[i] & 0x00ffffffu) | ((alpha & 0xffu) << 24);
    }
}

inline void normalize_alpha_switch(tjs_uint32 *dest,
                                   const tjs_uint32 *src1,
                                   const tjs_uint32 *src2,
                                   const tjs_uint8 *rule,
                                   const tjs_uint32 *table, tjs_int len,
                                   tjs_int src1lv, tjs_int src2lv) {
    for(tjs_int i = 0; i < len; ++i) {
        const tjs_int raw = rule[i];
        if(raw >= src1lv || raw < src2lv)
            continue; // upstream copied src1/src2 verbatim on these paths
        const tjs_int a1 = static_cast<tjs_int>(src1[i] >> 24);
        const tjs_int a2 = static_cast<tjs_int>(src2[i] >> 24);
        const tjs_int opa = static_cast<tjs_int>(table[raw]);
        const tjs_int alpha = a1 + ((a2 - a1) * opa >> 8);
        dest[i] = (dest[i] & 0x00ffffffu) | ((alpha & 0xffu) << 24);
    }
}

} // namespace

void TVPInitUnivTransBlendTable_sse2_c(tjs_uint32 *table, tjs_int phase,
                                       tjs_int vague) {
    AetherKrkrzInitUnivTransBlendTableSSE2(table, phase, vague);
}

void TVPInitUnivTransBlendTable_d_sse2_c(tjs_uint32 *table, tjs_int phase,
                                         tjs_int vague) {
    AetherKrkrzInitUnivTransBlendTableDSSE2(table, phase, vague);
}

void TVPUnivTransBlend_sse2_c(tjs_uint32 *dest, const tjs_uint32 *src1,
                              const tjs_uint32 *src2, const tjs_uint8 *rule,
                              const tjs_uint32 *table, tjs_int len) {
    if(len <= 0)
        return;
    AetherKrkrzUnivTransBlendSSE2(dest, src1, src2, rule, table, len);
    normalize_non_alpha(dest, len);
}

void TVPUnivTransBlend_switch_sse2_c(
    tjs_uint32 *dest, const tjs_uint32 *src1, const tjs_uint32 *src2,
    const tjs_uint8 *rule, const tjs_uint32 *table, tjs_int len,
    tjs_int src1lv, tjs_int src2lv) {
    if(len <= 0)
        return;
    AetherKrkrzUnivTransBlendSwitchSSE2(dest, src1, src2, rule, table, len,
                                        src1lv, src2lv);
    for(tjs_int i = 0; i < len; ++i) {
        const tjs_int raw = rule[i];
        if(raw < src1lv && raw >= src2lv)
            dest[i] &= 0x00ffffffu;
    }
}

void TVPUnivTransBlend_d_sse2_c(tjs_uint32 *dest, const tjs_uint32 *src1,
                                const tjs_uint32 *src2, const tjs_uint8 *rule,
                                const tjs_uint32 *table, tjs_int len) {
    if(len <= 0)
        return;
    AetherKrkrzUnivTransBlendDSSE2(dest, src1, src2, rule, table, len);
    normalize_alpha(dest, src1, src2, rule, table, len);
}

void TVPUnivTransBlend_switch_d_sse2_c(
    tjs_uint32 *dest, const tjs_uint32 *src1, const tjs_uint32 *src2,
    const tjs_uint8 *rule, const tjs_uint32 *table, tjs_int len,
    tjs_int src1lv, tjs_int src2lv) {
    if(len <= 0)
        return;
    AetherKrkrzUnivTransBlendSwitchDSSE2(dest, src1, src2, rule, table, len,
                                         src1lv, src2lv);
    normalize_alpha_switch(dest, src1, src2, rule, table, len, src1lv,
                           src2lv);
}
#endif
