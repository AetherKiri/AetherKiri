// Adapt the vendored ARM real-FFT kernel to AetherKiri's XMM compatibility
// declarations and ARM feature macros.
#include "tjsCommHead.h"
#include "xmmlib.h"
#include "../../../../third_party/krkrz_dev/src/core/common/sound/RealFFT_NEON.cpp"
