//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000-2007 W.Dee <dee@kikyou.info> and
   contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// CPU idetification / features detection routine
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include "cpu_types.h"
#include "DebugIntf.h"
#include "SysInitIntf.h"

#include "ThreadIntf.h"
#include "Exception.h"

/*
        Note: CPU clock measuring routine is in EmergencyExit.cpp,
   reusing hot-key watching thread.
*/

//---------------------------------------------------------------------------
extern "C" {
tjs_uint32 TVPCPUType = 0; // CPU type
tjs_uint32 TVPCPUFeatures = 0;
}

static bool TVPCPUChecked = false;
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPGetCPUTypeForOne
//---------------------------------------------------------------------------
static void TVPGetCPUTypeForOne() {
    try {
        TVPCPUFeatures = 0;

        // TVPCheckCPU(); // in detect_cpu.nas
    } catch(... /*EXCEPTION_EXECUTE_HANDLER*/) {
        // exception had been ocured
        throw Exception("CPU check failure.");
    }

    // check OSFXSR
    // 	if(TVPCPUFeatures & TVP_CPU_HAS_SSE)
    // 	{
    // 		__try
    // 		{
    // 			__emit__(0x0f, 0x57, 0xc0); // xorps xmm0, xmm0 (SSE)
    // 		}
    // 		__except(EXCEPTION_EXECUTE_HANDLER)
    // 		{
    // 			// exception had been ocured
    // 			// execution of 'xorps' is failed (XMM registers not
    // available) 			TVPCPUFeatures &=~ TVP_CPU_HAS_SSE;
    // TVPCPUFeatures
    // &=~ TVP_CPU_HAS_SSE2;
    // 		}
    // 	}
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
static ttstr TVPDumpCPUFeatures(tjs_uint32 features) {
    ttstr ret;
    // #define TVP_DUMP_CPU(x, n) { ret += TJS_W("  ") TJS_W(n);  \
    // 	if(features & x) ret += TJS_W(":yes"); else ret += TJS_W(":no"); }
    //
    // 	TVP_DUMP_CPU(TVP_CPU_HAS_FPU, "FPU");
    // 	TVP_DUMP_CPU(TVP_CPU_HAS_MMX, "MMX");
    // 	TVP_DUMP_CPU(TVP_CPU_HAS_3DN, "3DN");
    // 	TVP_DUMP_CPU(TVP_CPU_HAS_SSE, "SSE");
    // 	TVP_DUMP_CPU(TVP_CPU_HAS_CMOV, "CMOVcc");
    // 	TVP_DUMP_CPU(TVP_CPU_HAS_E3DN, "E3DN");
    // 	TVP_DUMP_CPU(TVP_CPU_HAS_EMMX, "EMMX");
    // 	TVP_DUMP_CPU(TVP_CPU_HAS_SSE2, "SSE2");
    // 	TVP_DUMP_CPU(TVP_CPU_HAS_TSC, "TSC");

    return ret;
}
//---------------------------------------------------------------------------
static ttstr TVPDumpCPUInfo(tjs_int cpu_num) {
    // dump detected cpu type
    ttstr features(TJS_W("(info) CPU #") + ttstr(cpu_num) + TJS_W(" : "));

    features += TVPDumpCPUFeatures(TVPCPUFeatures);

    tjs_uint32 vendor = TVPCPUFeatures & TVP_CPU_VENDOR_MASK;

    // #undef TVP_DUMP_CPU
    // #define TVP_DUMP_CPU(x, n) { \
    // 	if(vendor == x) features += TJS_W("  ") TJS_W(n); }
    //
    // 	TVP_DUMP_CPU(TVP_CPU_IS_INTEL, "Intel");
    // 	TVP_DUMP_CPU(TVP_CPU_IS_AMD, "AMD");
    // 	TVP_DUMP_CPU(TVP_CPU_IS_IDT, "IDT");
    // 	TVP_DUMP_CPU(TVP_CPU_IS_CYRIX, "Cyrix");
    // 	TVP_DUMP_CPU(TVP_CPU_IS_NEXGEN, "NexGen");
    // 	TVP_DUMP_CPU(TVP_CPU_IS_RISE, "Rise");
    // 	TVP_DUMP_CPU(TVP_CPU_IS_UMC, "UMC");
    // 	TVP_DUMP_CPU(TVP_CPU_IS_TRANSMETA, "Transmeta");
    //
    // 	TVP_DUMP_CPU(TVP_CPU_IS_UNKNOWN, "Unknown");
    //
    // #undef TVP_DUMP_CPU

    //	features += TJS_W("(") + ttstr((const tjs_nchar
    //*)TVPCPUVendor) +
    // TJS_W(")");

    // 	if(TVPCPUName[0]!=0)
    // 		features += TJS_W(" [") + ttstr((const tjs_nchar
    // *)TVPCPUName) + TJS_W("]");

    // 	features += TJS_W("  CPUID(1)/EAX=") +
    // TJSInt32ToHex(TVPCPUID1_EAX); 	features += TJS_W("
    // CPUID(1)/EBX=") + TJSInt32ToHex(TVPCPUID1_EBX);

    TVPAddImportantLog(features);

    // 	if(((TVPCPUID1_EAX >> 8) & 0x0f) <= 4)
    // 		throw Exception("CPU check failure: CPU family 4 or lesser
    // is not supported\r\n"+ 		features.AsAnsiString());

    return features;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPDetectCPU
//---------------------------------------------------------------------------
static void TVPDisableCPU(tjs_uint32 featurebit, const tjs_char *name) {}

void TVPDetectCPU() {
    if(TVPCPUChecked)
        return;
    TVPCPUChecked = true;

    // The portable detector used by SDL/Godot targets historically left the
    // feature mask empty on desktop macOS/Linux.  That made every optional
    // krkrz SIMD leaf (including the sound/TLG adapters) silently fall back to
    // scalar code even when the ISA was guaranteed by the target.  Seed only
    // features that are safe for the current compile-time architecture; the
    // Windows detector keeps its more detailed CPUID path.
#if defined(__aarch64__) || defined(__arm64__) || defined(__ARM_NEON)
    TVPCPUFeatures |= TVP_CPU_FAMILY_ARM | TVP_CPU_HAS_NEON;
#elif defined(__x86_64__) || defined(_M_X64)
    // SSE2 and CMOV are architectural requirements of x86-64.  SSE/MMX are
    // also available on all supported x86-64 hosts and are used by the legacy
    // audio mixer and the krkrz window kernels.
    TVPCPUFeatures |= TVP_CPU_FAMILY_X64 | TVP_CPU_HAS_FPU |
                      TVP_CPU_HAS_MMX | TVP_CPU_HAS_SSE |
                      TVP_CPU_HAS_SSE2 | TVP_CPU_HAS_CMOV;
#if defined(__GNUC__) || defined(__clang__)
    // Unlike SSE2, AVX is not architectural on x86-64.  The compiler probe
    // includes the OSXSAVE/XCR0 checks, so selecting the AVX2 leaf here cannot
    // fault on hosts that merely expose the CPUID bit.
    if(__builtin_cpu_supports("avx"))
        TVPCPUFeatures |= TVP_CPU_HAS_AVX;
    if(__builtin_cpu_supports("avx2"))
        TVPCPUFeatures |= TVP_CPU_HAS_AVX | TVP_CPU_HAS_AVX2;
#endif
#elif defined(__i386__) || defined(_M_IX86)
    // 32-bit builds may run on older CPUs.  Ask the compiler's runtime probe
    // when available and leave the mask empty otherwise, preserving the
    // scalar path instead of emitting an illegal instruction.
    TVPCPUFeatures |= TVP_CPU_FAMILY_X86;
#if defined(__GNUC__) || defined(__clang__)
    if(__builtin_cpu_supports("sse"))
        TVPCPUFeatures |= TVP_CPU_HAS_SSE;
    if(__builtin_cpu_supports("sse2"))
        TVPCPUFeatures |= TVP_CPU_HAS_SSE2;
    if(__builtin_cpu_supports("cmov"))
        TVPCPUFeatures |= TVP_CPU_HAS_CMOV;
    if(__builtin_cpu_supports("avx"))
        TVPCPUFeatures |= TVP_CPU_HAS_AVX;
    if(__builtin_cpu_supports("avx2"))
        TVPCPUFeatures |= TVP_CPU_HAS_AVX | TVP_CPU_HAS_AVX2;
#endif
#endif

    tjs_uint32 features = 0;
    features = (TVPCPUFeatures & TVP_CPU_FEATURE_MASK);
    TVPCPUType &= ~(TVP_CPU_FEATURE_MASK | TVP_CPU_FAMILY_MASK);
    TVPCPUType |= features;
    TVPCPUType |= TVPCPUFeatures & TVP_CPU_FAMILY_MASK;

    TVPDisableCPU(TVP_CPU_HAS_NEON, TJS_W("-cpuneon"));
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// jpeg and png loader support functions
//---------------------------------------------------------------------------
unsigned long MMXReady = 0;
extern "C" {
void CheckMMX() {
    TVPDetectCPU();
    MMXReady = TVPCPUType & TVP_CPU_HAS_MMX;
}
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPGetCPUType
//---------------------------------------------------------------------------
tjs_uint32 TVPGetCPUType() {
    TVPDetectCPU();
    return TVPCPUType;
}
//---------------------------------------------------------------------------
