// Direct source bridge for krkrz's SSE real-FFT leaf.  The Aether scalar
// implementation and public header stay the ABI owner; this file contributes
// only the suffixed rdft_sse symbol.
#include "../../tjs2/tjsCommHead.h"
#include "../RealFFT.h"

#include "../../../../third_party/krkrz_dev/src/core/common/sound/RealFFT_SSE.cpp"
