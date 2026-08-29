// Runtime dispatch for the compatible krkrz visual SIMD leaves.  Unlike the
// upstream TVPGL_*_Init entry points, this file installs only leaf pointers;
// it never replaces Aether's complete renderer dispatch table.
#include "../../tjs2/tjsCommHead.h"
#include "../tvpgl.h"
#include "../VisualSIMDLeaves.h"
#include "../../environ/DetectCPU.h"
#include "../../environ/cpu_types.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace {

using ColorMapFn = decltype(TVPApplyColorMap);
using ColorMapOFn = decltype(TVPApplyColorMap_o);
using ColorMap65Fn = decltype(TVPApplyColorMap65);
using ColorMap65OFn = decltype(TVPApplyColorMap65_o);
using PixelFormatFn = decltype(TVPConvert24BitTo32Bit);
using UnivTableFn = decltype(TVPInitUnivTransBlendTable);
using UnivBlendFn = decltype(TVPUnivTransBlend);
using UnivSwitchFn = decltype(TVPUnivTransBlend_switch);
using UnivBlendDFn = decltype(TVPUnivTransBlend_d);
using UnivSwitchDFn = decltype(TVPUnivTransBlend_switch_d);

struct ScalarDefaults {
    ColorMapFn color_map = nullptr;
    ColorMapOFn color_map_o = nullptr;
    ColorMap65Fn color_map65 = nullptr;
    ColorMap65OFn color_map65_o = nullptr;
    PixelFormatFn pixel_format = nullptr;
    UnivTableFn univ_table = nullptr;
    UnivTableFn univ_table_d = nullptr;
    UnivBlendFn univ_blend = nullptr;
    UnivSwitchFn univ_switch = nullptr;
    UnivBlendDFn univ_blend_d = nullptr;
    UnivSwitchDFn univ_switch_d = nullptr;
    bool captured = false;
};

ScalarDefaults defaults;
std::atomic<tjs_uint32> installed_mask{0};
std::mutex init_mutex;

bool enabled() {
    const char *value = std::getenv("AETHERKIRI_TVPGL_SIMD");
    if(value == nullptr || value[0] == '\0')
        return true;
    return std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0 ||
           std::strcmp(value, "on") == 0 || std::strcmp(value, "yes") == 0;
}

void capture_defaults() {
    if(defaults.captured)
        return;
    defaults.color_map = TVPApplyColorMap;
    defaults.color_map_o = TVPApplyColorMap_o;
    defaults.color_map65 = TVPApplyColorMap65;
    defaults.color_map65_o = TVPApplyColorMap65_o;
    defaults.pixel_format = TVPConvert24BitTo32Bit;
    defaults.univ_table = TVPInitUnivTransBlendTable;
    defaults.univ_table_d = TVPInitUnivTransBlendTable_d;
    defaults.univ_blend = TVPUnivTransBlend;
    defaults.univ_switch = TVPUnivTransBlend_switch;
    defaults.univ_blend_d = TVPUnivTransBlend_d;
    defaults.univ_switch_d = TVPUnivTransBlend_switch_d;
    defaults.captured = true;
}

void restore_defaults() {
    TVPApplyColorMap = defaults.color_map;
    TVPApplyColorMap_o = defaults.color_map_o;
    TVPApplyColorMap65 = defaults.color_map65;
    TVPApplyColorMap65_o = defaults.color_map65_o;
    TVPConvert24BitTo32Bit = defaults.pixel_format;
    TVPInitUnivTransBlendTable = defaults.univ_table;
    TVPInitUnivTransBlendTable_d = defaults.univ_table_d;
    TVPUnivTransBlend = defaults.univ_blend;
    TVPUnivTransBlend_switch = defaults.univ_switch;
    TVPUnivTransBlend_d = defaults.univ_blend_d;
    TVPUnivTransBlend_switch_d = defaults.univ_switch_d;
}

} // namespace

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || \
    defined(__x86_64__)
extern void TVPApplyColorMap_sse2_c(tjs_uint32 *, const tjs_uint8 *, tjs_int,
                                    tjs_uint32);
extern void TVPApplyColorMap_o_sse2_c(tjs_uint32 *, const tjs_uint8 *, tjs_int,
                                      tjs_uint32, tjs_int);
extern void TVPApplyColorMap65_sse2_c(tjs_uint32 *, const tjs_uint8 *, tjs_int,
                                      tjs_uint32);
extern void TVPApplyColorMap65_o_sse2_c(tjs_uint32 *, const tjs_uint8 *,
                                        tjs_int, tjs_uint32, tjs_int);
extern void TVPConvert24BitTo32Bit_sse2_c(tjs_uint32 *, const tjs_uint8 *,
                                          tjs_int);
extern void TVPInitUnivTransBlendTable_sse2_c(tjs_uint32 *, tjs_int, tjs_int);
extern void TVPInitUnivTransBlendTable_d_sse2_c(tjs_uint32 *, tjs_int, tjs_int);
extern void TVPUnivTransBlend_sse2_c(tjs_uint32 *, const tjs_uint32 *,
                                     const tjs_uint32 *, const tjs_uint8 *,
                                     const tjs_uint32 *, tjs_int);
extern void TVPUnivTransBlend_switch_sse2_c(
    tjs_uint32 *, const tjs_uint32 *, const tjs_uint32 *, const tjs_uint8 *,
    const tjs_uint32 *, tjs_int, tjs_int, tjs_int);
extern void TVPUnivTransBlend_d_sse2_c(tjs_uint32 *, const tjs_uint32 *,
                                       const tjs_uint32 *, const tjs_uint8 *,
                                       const tjs_uint32 *, tjs_int);
extern void TVPUnivTransBlend_switch_d_sse2_c(
    tjs_uint32 *, const tjs_uint32 *, const tjs_uint32 *, const tjs_uint8 *,
    const tjs_uint32 *, tjs_int, tjs_int, tjs_int);
#if defined(AETHER_KRKRZ_VISUAL_AVX2_COMPILED)
extern void TVPApplyColorMap_avx2_c(tjs_uint32 *, const tjs_uint8 *, tjs_int,
                                    tjs_uint32);
extern void TVPApplyColorMap_o_avx2_c(tjs_uint32 *, const tjs_uint8 *, tjs_int,
                                      tjs_uint32, tjs_int);
extern void TVPApplyColorMap65_avx2_c(tjs_uint32 *, const tjs_uint8 *, tjs_int,
                                      tjs_uint32);
extern void TVPApplyColorMap65_o_avx2_c(tjs_uint32 *, const tjs_uint8 *,
                                        tjs_int, tjs_uint32, tjs_int);
#endif
#endif

#if defined(__aarch64__) || defined(__arm64__) || defined(__ARM_NEON) || \
    defined(__ARM_NEON__)
extern void TVPApplyColorMap_neon_c(tjs_uint32 *, const tjs_uint8 *, tjs_int,
                                    tjs_uint32);
extern void TVPApplyColorMap_o_neon_c(tjs_uint32 *, const tjs_uint8 *, tjs_int,
                                      tjs_uint32, tjs_int);
extern void TVPApplyColorMap65_neon_c(tjs_uint32 *, const tjs_uint8 *, tjs_int,
                                      tjs_uint32);
extern void TVPApplyColorMap65_o_neon_c(tjs_uint32 *, const tjs_uint8 *,
                                        tjs_int, tjs_uint32, tjs_int);
extern void TVPConvert24BitTo32Bit_neon_c(tjs_uint32 *, const tjs_uint8 *,
                                          tjs_int);
#endif

namespace {

void install_x86_color_map(bool avx2) {
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || \
    defined(__x86_64__)
#if defined(AETHER_KRKRZ_VISUAL_AVX2_COMPILED)
    if(avx2) {
        TVPApplyColorMap = TVPApplyColorMap_avx2_c;
        TVPApplyColorMap_o = TVPApplyColorMap_o_avx2_c;
        TVPApplyColorMap65 = TVPApplyColorMap65_avx2_c;
        TVPApplyColorMap65_o = TVPApplyColorMap65_o_avx2_c;
        return;
    }
#else
    (void)avx2;
#endif
    TVPApplyColorMap = TVPApplyColorMap_sse2_c;
    TVPApplyColorMap_o = TVPApplyColorMap_o_sse2_c;
    TVPApplyColorMap65 = TVPApplyColorMap65_sse2_c;
    TVPApplyColorMap65_o = TVPApplyColorMap65_o_sse2_c;
#else
    (void)avx2;
#endif
}

void install_common_x86_leaves() {
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || \
    defined(__x86_64__)
    TVPConvert24BitTo32Bit = TVPConvert24BitTo32Bit_sse2_c;
    TVPInitUnivTransBlendTable = TVPInitUnivTransBlendTable_sse2_c;
    TVPInitUnivTransBlendTable_d = TVPInitUnivTransBlendTable_d_sse2_c;
    TVPUnivTransBlend = TVPUnivTransBlend_sse2_c;
    TVPUnivTransBlend_switch = TVPUnivTransBlend_switch_sse2_c;
    TVPUnivTransBlend_d = TVPUnivTransBlend_d_sse2_c;
    TVPUnivTransBlend_switch_d = TVPUnivTransBlend_switch_d_sse2_c;
#endif
}

} // namespace

extern "C" void TVPInitVisualSIMDLeaves() {
    const std::lock_guard<std::mutex> lock(init_mutex);
    capture_defaults();
    restore_defaults();
    installed_mask.store(0, std::memory_order_release);
    if(!enabled())
        return;

    TVPDetectCPU();
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || \
    defined(__x86_64__)
    if((TVPCPUType & TVP_CPU_HAS_SSE2) != 0) {
        const bool avx2 = (TVPCPUType & TVP_CPU_HAS_AVX2) != 0;
        install_x86_color_map(avx2);
        install_common_x86_leaves();
        installed_mask.store(TVP_VISUAL_SIMD_COLORMAP |
                                 TVP_VISUAL_SIMD_PIXELFORMAT |
                                 TVP_VISUAL_SIMD_UNIVTRANS,
                             std::memory_order_release);
    }
#elif defined(__aarch64__) || defined(__arm64__) || defined(__ARM_NEON) || \
    defined(__ARM_NEON__)
    if((TVPCPUType & TVP_CPU_HAS_NEON) != 0) {
        TVPApplyColorMap = TVPApplyColorMap_neon_c;
        TVPApplyColorMap_o = TVPApplyColorMap_o_neon_c;
        TVPApplyColorMap65 = TVPApplyColorMap65_neon_c;
        TVPApplyColorMap65_o = TVPApplyColorMap65_o_neon_c;
        TVPConvert24BitTo32Bit = TVPConvert24BitTo32Bit_neon_c;
        installed_mask.store(TVP_VISUAL_SIMD_COLORMAP |
                                 TVP_VISUAL_SIMD_PIXELFORMAT,
                             std::memory_order_release);
    }
#endif
}

extern "C" tjs_uint32 TVPVisualSIMDLeavesMask() {
    return installed_mask.load(std::memory_order_acquire);
}
