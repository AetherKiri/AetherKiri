// Direct source bridge for krkrz's NEON window kernels.  Aether selects these
// symbols from its existing CPU-feature dispatch and keeps the scalar path as
// the compatibility fallback.
#include "../../tjs2/tjsCommHead.h"
#include "../MathAlgorithms.h"

#include "../../../../third_party/krkrz_dev/src/core/common/sound/MathAlgorithms_NEON.cpp"
