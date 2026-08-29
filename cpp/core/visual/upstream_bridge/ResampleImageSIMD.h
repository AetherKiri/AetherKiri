// Aether dispatch boundary for the optional krkrz resampling leaves.
//
// The upstream SSE2/AVX2 implementations are intentionally kept in the
// pinned submodule.  This header exposes only an engine-owned call boundary so
// Load/Layer code never has to know about the upstream thread API.
#pragma once

#include "../LayerBitmapIntf.h"
#include "ResampleImageInternal.h"

using tTVPResampleImageSIMDFunc = void (*)(
    const tTVPResampleClipping &clip,
    const tTVPImageCopyFuncBase *blendfunc,
    iTVPBaseBitmap *dest,
    const tTVPRect &destrect,
    const iTVPBaseBitmap *src,
    const tTVPRect &srcrect,
    tTVPBBStretchType type,
    tjs_real typeopt);

// Called after TVPGL's scalar/Highway tables have been initialized.  It is
// idempotent and leaves the scalar path selected when the ISA is unavailable
// or AETHERKIRI_TVPGL_SIMD disables acceleration.
void TVPInitResampleSIMD();

// Returns true when an upstream SIMD implementation handled the operation.
// A false return means the caller must execute its existing scalar path.
bool TVPResampleImageSIMD(const tTVPResampleClipping &clip,
                          const tTVPImageCopyFuncBase *blendfunc,
                          iTVPBaseBitmap *dest,
                          const tTVPRect &destrect,
                          const iTVPBaseBitmap *src,
                          const tTVPRect &srcrect,
                          tTVPBBStretchType type,
                          tjs_real typeopt);
