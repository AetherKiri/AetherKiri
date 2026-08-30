#include "UpstreamSIMDDispatch.h"

#include "CPUFeatures.h"
#include "tvpgl.h"

#if defined(AETHER_ENABLE_VISUAL_X86_SIMD)
extern void TVPApplyColorMap65_sse2_c(tjs_uint32 *, const tjs_uint8 *, tjs_int,
                                      tjs_uint32);
extern void TVPApplyColorMap_sse2_c(tjs_uint32 *, const tjs_uint8 *, tjs_int,
                                    tjs_uint32);
extern void TVPApplyColorMap65_d_sse2_c(tjs_uint32 *, const tjs_uint8 *,
                                        tjs_int, tjs_uint32);
extern void TVPApplyColorMap65_a_sse2_c(tjs_uint32 *, const tjs_uint8 *,
                                        tjs_int, tjs_uint32);
extern void TVPApplyColorMap_a_sse2_c(tjs_uint32 *, const tjs_uint8 *, tjs_int,
                                      tjs_uint32);
extern void TVPApplyColorMap65_o_sse2_c(tjs_uint32 *, const tjs_uint8 *,
                                        tjs_int, tjs_uint32, tjs_int);
extern void TVPApplyColorMap_o_sse2_c(tjs_uint32 *, const tjs_uint8 *, tjs_int,
                                      tjs_uint32, tjs_int);
extern void TVPApplyColorMap65_ao_sse2_c(tjs_uint32 *, const tjs_uint8 *,
                                         tjs_int, tjs_uint32, tjs_int);
extern void TVPApplyColorMap_ao_sse2_c(tjs_uint32 *, const tjs_uint8 *, tjs_int,
                                       tjs_uint32, tjs_int);

extern void TVPApplyColorMap65_avx2_c(tjs_uint32 *, const tjs_uint8 *, tjs_int,
                                      tjs_uint32);
extern void TVPApplyColorMap_avx2_c(tjs_uint32 *, const tjs_uint8 *, tjs_int,
                                    tjs_uint32);
extern void TVPApplyColorMap65_d_avx2_c(tjs_uint32 *, const tjs_uint8 *,
                                        tjs_int, tjs_uint32);
extern void TVPApplyColorMap65_a_avx2_c(tjs_uint32 *, const tjs_uint8 *,
                                        tjs_int, tjs_uint32);
extern void TVPApplyColorMap_a_avx2_c(tjs_uint32 *, const tjs_uint8 *, tjs_int,
                                      tjs_uint32);
extern void TVPApplyColorMap65_o_avx2_c(tjs_uint32 *, const tjs_uint8 *,
                                        tjs_int, tjs_uint32, tjs_int);
extern void TVPApplyColorMap_o_avx2_c(tjs_uint32 *, const tjs_uint8 *, tjs_int,
                                      tjs_uint32, tjs_int);
extern void TVPApplyColorMap65_ao_avx2_c(tjs_uint32 *, const tjs_uint8 *,
                                         tjs_int, tjs_uint32, tjs_int);
extern void TVPApplyColorMap_ao_avx2_c(tjs_uint32 *, const tjs_uint8 *, tjs_int,
                                       tjs_uint32, tjs_int);

extern void TVPInitUnivTransBlendTable_sse2_c(tjs_uint32 *, tjs_int, tjs_int);
extern void TVPInitUnivTransBlendTable_d_sse2_c(tjs_uint32 *, tjs_int, tjs_int);
extern void TVPUnivTransBlend_sse2_c(tjs_uint32 *, const tjs_uint32 *,
                                     const tjs_uint32 *, const tjs_uint8 *,
                                     const tjs_uint32 *, tjs_int);
extern void TVPUnivTransBlend_switch_sse2_c(tjs_uint32 *, const tjs_uint32 *,
                                            const tjs_uint32 *,
                                            const tjs_uint8 *,
                                            const tjs_uint32 *, tjs_int,
                                            tjs_int, tjs_int);
extern void TVPUnivTransBlend_d_sse2_c(tjs_uint32 *, const tjs_uint32 *,
                                       const tjs_uint32 *, const tjs_uint8 *,
                                       const tjs_uint32 *, tjs_int);
extern void TVPUnivTransBlend_switch_d_sse2_c(tjs_uint32 *, const tjs_uint32 *,
                                              const tjs_uint32 *,
                                              const tjs_uint8 *,
                                              const tjs_uint32 *, tjs_int,
                                              tjs_int, tjs_int);
extern void TVPAdjustGamma_a_sse2_c(tjs_uint32 *, tjs_int,
                                    tTVPGLGammaAdjustTempData *);
extern void TVPConvert24BitTo32Bit_ssse3_c(tjs_uint32 *, const tjs_uint8 *,
                                           tjs_int);
extern void TVPDoBoxBlurAvg16_sse2_c(tjs_uint32 *, tjs_uint16 *,
                                     const tjs_uint16 *, const tjs_uint16 *,
                                     tjs_int, tjs_int);
extern void TVPDoBoxBlurAvg16_d_sse2_c(tjs_uint32 *, tjs_uint16 *,
                                       const tjs_uint16 *, const tjs_uint16 *,
                                       tjs_int, tjs_int);
extern void TVPDoBoxBlurAvg32_sse2_c(tjs_uint32 *, tjs_uint32 *,
                                     const tjs_uint32 *, const tjs_uint32 *,
                                     tjs_int, tjs_int);
extern void TVPDoBoxBlurAvg32_d_sse2_c(tjs_uint32 *, tjs_uint32 *,
                                       const tjs_uint32 *, const tjs_uint32 *,
                                       tjs_int, tjs_int);
extern tjs_int TVPTLG5DecompressSlide_sse2_c(tjs_uint8 *, const tjs_uint8 *,
                                             tjs_int, tjs_uint8 *, tjs_int);
extern void TVPTLG5ComposeColors3To4_sse2_c(tjs_uint8 *, const tjs_uint8 *,
                                            tjs_uint8 *const *, tjs_int);
extern void TVPTLG5ComposeColors4To4_sse2_c(tjs_uint8 *, const tjs_uint8 *,
                                            tjs_uint8 *const *, tjs_int);
extern void TVPTLG6DecodeLineGeneric_sse2_c(tjs_uint32 *, tjs_uint32 *, tjs_int,
                                            tjs_int, tjs_int, tjs_uint8 *,
                                            tjs_int, tjs_uint32 *, tjs_uint32,
                                            tjs_int, tjs_int);
extern void TVPTLG6DecodeLine_sse2_c(tjs_uint32 *, tjs_uint32 *, tjs_int,
                                     tjs_int, tjs_uint8 *, tjs_int,
                                     tjs_uint32 *, tjs_uint32, tjs_int,
                                     tjs_int);
#endif

#if defined(AETHER_ENABLE_VISUAL_ARM_SIMD)
extern void TVPApplyColorMap65_neon_c(tjs_uint32 *, const tjs_uint8 *, tjs_int,
                                      tjs_uint32);
extern void TVPApplyColorMap_neon_c(tjs_uint32 *, const tjs_uint8 *, tjs_int,
                                    tjs_uint32);
extern void TVPApplyColorMap65_d_neon_c(tjs_uint32 *, const tjs_uint8 *,
                                        tjs_int, tjs_uint32);
extern void TVPApplyColorMap65_a_neon_c(tjs_uint32 *, const tjs_uint8 *,
                                        tjs_int, tjs_uint32);
extern void TVPApplyColorMap_a_neon_c(tjs_uint32 *, const tjs_uint8 *, tjs_int,
                                      tjs_uint32);
extern void TVPApplyColorMap65_o_neon_c(tjs_uint32 *, const tjs_uint8 *,
                                        tjs_int, tjs_uint32, tjs_int);
extern void TVPApplyColorMap_o_neon_c(tjs_uint32 *, const tjs_uint8 *, tjs_int,
                                      tjs_uint32, tjs_int);
extern void TVPApplyColorMap65_ao_neon_c(tjs_uint32 *, const tjs_uint8 *,
                                         tjs_int, tjs_uint32, tjs_int);
extern void TVPApplyColorMap_ao_neon_c(tjs_uint32 *, const tjs_uint8 *, tjs_int,
                                       tjs_uint32, tjs_int);
extern void TVPAdjustGamma_a_neon_c(tjs_uint32 *, tjs_int,
                                    tTVPGLGammaAdjustTempData *);
extern void TVPConvert24BitTo32Bit_neon_c(tjs_uint32 *, const tjs_uint8 *,
                                          tjs_int);
#endif

namespace {

    template <typename F65, typename F, typename F65D, typename F65A,
              typename FA, typename F65O, typename FO, typename F65AO,
              typename FAO>
    void InstallColorMap(F65 f65, F f, F65D f65d, F65A f65a, FA fa, F65O f65o,
                         FO fo, F65AO f65ao, FAO fao) {
        TVPApplyColorMap65 = f65;
        TVPApplyColorMap = f;
        TVPApplyColorMap65_d = f65d;
        TVPApplyColorMap65_a = f65a;
        TVPApplyColorMap_a = fa;
        TVPApplyColorMap65_o = f65o;
        TVPApplyColorMap_o = fo;
        TVPApplyColorMap65_ao = f65ao;
        TVPApplyColorMap_ao = fao;
    }

} // namespace

void TVPInitUpstreamVisualSIMD() {
#if defined(AETHER_ENABLE_VISUAL_X86_SIMD)
    if(TVPHasCPUFeature(TVPCPUFeature::SSE2)) {
        InstallColorMap(TVPApplyColorMap65_sse2_c, TVPApplyColorMap_sse2_c,
                        TVPApplyColorMap65_d_sse2_c,
                        TVPApplyColorMap65_a_sse2_c, TVPApplyColorMap_a_sse2_c,
                        TVPApplyColorMap65_o_sse2_c, TVPApplyColorMap_o_sse2_c,
                        TVPApplyColorMap65_ao_sse2_c,
                        TVPApplyColorMap_ao_sse2_c);
        TVPInitUnivTransBlendTable = TVPInitUnivTransBlendTable_sse2_c;
        TVPInitUnivTransBlendTable_d = TVPInitUnivTransBlendTable_d_sse2_c;
        TVPInitUnivTransBlendTable_a = TVPInitUnivTransBlendTable_sse2_c;
        TVPUnivTransBlend = TVPUnivTransBlend_sse2_c;
        TVPUnivTransBlend_a = TVPUnivTransBlend_sse2_c;
        TVPUnivTransBlend_d = TVPUnivTransBlend_d_sse2_c;
        TVPUnivTransBlend_switch = TVPUnivTransBlend_switch_sse2_c;
        TVPUnivTransBlend_switch_a = TVPUnivTransBlend_switch_sse2_c;
        TVPUnivTransBlend_switch_d = TVPUnivTransBlend_switch_d_sse2_c;
        TVPAdjustGamma_a = TVPAdjustGamma_a_sse2_c;
        TVPDoBoxBlurAvg16 = TVPDoBoxBlurAvg16_sse2_c;
        TVPDoBoxBlurAvg16_d = TVPDoBoxBlurAvg16_d_sse2_c;
        TVPDoBoxBlurAvg32 = TVPDoBoxBlurAvg32_sse2_c;
        TVPDoBoxBlurAvg32_d = TVPDoBoxBlurAvg32_d_sse2_c;
        TVPTLG5DecompressSlide = TVPTLG5DecompressSlide_sse2_c;
        TVPTLG5ComposeColors3To4 = TVPTLG5ComposeColors3To4_sse2_c;
        TVPTLG5ComposeColors4To4 = TVPTLG5ComposeColors4To4_sse2_c;
#if defined(TJS_64BIT_OS)
        TVPTLG6DecodeLineGeneric = TVPTLG6DecodeLineGeneric_sse2_c;
        TVPTLG6DecodeLine = TVPTLG6DecodeLine_sse2_c;
#endif
    }
    if(TVPHasCPUFeature(TVPCPUFeature::SSSE3)) {
        TVPConvert24BitTo32Bit = TVPConvert24BitTo32Bit_ssse3_c;
        TVPBLConvert24BitTo32Bit = TVPConvert24BitTo32Bit_ssse3_c;
    }
    if(TVPHasCPUFeature(TVPCPUFeature::AVX2)) {
        InstallColorMap(TVPApplyColorMap65_avx2_c, TVPApplyColorMap_avx2_c,
                        TVPApplyColorMap65_d_avx2_c,
                        TVPApplyColorMap65_a_avx2_c, TVPApplyColorMap_a_avx2_c,
                        TVPApplyColorMap65_o_avx2_c, TVPApplyColorMap_o_avx2_c,
                        TVPApplyColorMap65_ao_avx2_c,
                        TVPApplyColorMap_ao_avx2_c);
    }
#elif defined(AETHER_ENABLE_VISUAL_ARM_SIMD)
    if(TVPHasCPUFeature(TVPCPUFeature::NEON)) {
        InstallColorMap(TVPApplyColorMap65_neon_c, TVPApplyColorMap_neon_c,
                        TVPApplyColorMap65_d_neon_c,
                        TVPApplyColorMap65_a_neon_c, TVPApplyColorMap_a_neon_c,
                        TVPApplyColorMap65_o_neon_c, TVPApplyColorMap_o_neon_c,
                        TVPApplyColorMap65_ao_neon_c,
                        TVPApplyColorMap_ao_neon_c);
        TVPAdjustGamma_a = TVPAdjustGamma_a_neon_c;
        TVPConvert24BitTo32Bit = TVPConvert24BitTo32Bit_neon_c;
        TVPBLConvert24BitTo32Bit = TVPConvert24BitTo32Bit_neon_c;
    }
#endif
}
