// Aether ABI bridge for the scalar sound-window helpers.  The implementations
// remain in the pinned krkrz_dev core submodule; only the const-qualified
// Aether signatures are adapted here.  The upstream code never mutates its
// window argument, so the narrow const_cast is safe and keeps the existing
// public declaration stable for Aether callers.
#include "tjsCommHead.h"
#include "MathAlgorithms_Default.h"

#define DeinterleaveApplyingWindow AetherKrkrzDeinterleaveApplyingWindow
#define InterleaveOverlappingWindow AetherKrkrzInterleaveOverlappingWindow
#include "../../../third_party/krkrz_dev/src/core/common/sound/MathAlgorithms.cpp"
#undef InterleaveOverlappingWindow
#undef DeinterleaveApplyingWindow

void DeinterleaveApplyingWindow(float *__restrict dest[],
                                const float *__restrict src,
                                const float *__restrict win, int numch,
                                size_t destofs, size_t len) {
    AetherKrkrzDeinterleaveApplyingWindow(
        dest, src, const_cast<float *>(win), numch, destofs, len);
}

void InterleaveOverlappingWindow(
    float *__restrict dest, const float *__restrict const *__restrict src,
    const float *__restrict win, int numch, size_t srcofs, size_t len) {
    AetherKrkrzInterleaveOverlappingWindow(
        dest, src, const_cast<float *>(win), numch, srcofs, len);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
