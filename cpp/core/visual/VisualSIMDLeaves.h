/*
 * Runtime-selected leaf functions from the pinned krkrz_dev visual SIMD
 * sources.  Aether owns the TVPGL registry and all scalar fallbacks; this
 * interface only exposes the one-time installation hook and a diagnostic mask.
 */
#pragma once

#include "tjsTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    TVP_VISUAL_SIMD_COLORMAP = 1u << 0,
    TVP_VISUAL_SIMD_PIXELFORMAT = 1u << 1,
    TVP_VISUAL_SIMD_UNIVTRANS = 1u << 2,
};

/// Restore the Aether-owned baseline and install compatible krkrz leaves.
/// Calling this repeatedly is safe and leaves unsupported paths untouched.
void TVPInitVisualSIMDLeaves();

/// Returns the leaf groups installed by the last initialization call.
tjs_uint32 TVPVisualSIMDLeavesMask();

#ifdef __cplusplus
}
#endif
