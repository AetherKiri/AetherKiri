// The scalar real-FFT implementation is consumed directly from the pinned
// krkrz_dev core checkout.  This wrapper keeps the source-level dependency
// explicit without copying the implementation into Aether.
#include "tjsCommHead.h"

#include "../../../../third_party/krkrz_dev/src/core/common/sound/RealFFT.cpp"
