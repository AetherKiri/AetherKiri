// Aether-owned dispatch boundary for the optional krkrz TLG SSE2 leaves.
// The complete TLG container/format loader stays in LoadTLG.cpp; these
// declarations expose only the four leaf kernels that have been parity
// checked against Aether's scalar tvpgl implementation.
#pragma once

#include "tjsTypes.h"

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || \
    defined(__x86_64__)

tjs_int TVPTLG5DecompressSlide_sse2_c(tjs_uint8 *out, const tjs_uint8 *in,
                                      tjs_int insize, tjs_uint8 *text,
                                      tjs_int initialr);
void TVPTLG5ComposeColors3To4_sse2_c(tjs_uint8 *outp, const tjs_uint8 *upper,
                                     tjs_uint8 *const *buf, tjs_int width);
void TVPTLG5ComposeColors4To4_sse2_c(tjs_uint8 *outp, const tjs_uint8 *upper,
                                     tjs_uint8 *const *buf, tjs_int width);
void TVPTLG6DecodeLineGeneric_sse2_c(
    tjs_uint32 *prevline, tjs_uint32 *curline, tjs_int width,
    tjs_int start_block, tjs_int block_limit, tjs_uint8 *filtertypes,
    tjs_int skipblockbytes, tjs_uint32 *in, tjs_uint32 initialp,
    tjs_int oddskip, tjs_int dir);
void TVPTLG6DecodeLine_sse2_c(
    tjs_uint32 *prevline, tjs_uint32 *curline, tjs_int width,
    tjs_int block_count, tjs_uint8 *filtertypes, tjs_int skipblockbytes,
    tjs_uint32 *in, tjs_uint32 initialp, tjs_int oddskip, tjs_int dir);

/// Install the SSE2 leaf pointers after TVPGL_C_Init has installed scalar
/// defaults.  On non-x86 targets this is a no-op, preserving one ABI owner.
void TVPInitTLGSIMD();

#else

void TVPInitTLGSIMD();

#endif
