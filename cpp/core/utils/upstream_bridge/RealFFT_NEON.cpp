// Direct source bridge for krkrz's NEON real-FFT leaf.  It is compiled only
// for ARM targets and leaves the scalar rdft path available as a fallback.
#include "../../tjs2/tjsCommHead.h"
#include "../RealFFT.h"

#include "../../../../third_party/krkrz_dev/src/core/common/sound/RealFFT_NEON.cpp"
