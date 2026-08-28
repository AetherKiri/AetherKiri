// Aether ABI bridge for the upstream WaveSegmentQueue implementation.
//
// Include the Aether-owned header first so the upstream translation unit is
// compiled against the same public declarations used by the rest of the
// sound module.  The implementation itself remains in the pinned submodule;
// this file intentionally contains no copied business logic.
#include "tjsCommHead.h"
#include "WaveSegmentQueue.h"

#include "../../../../third_party/krkrz_dev/src/core/common/sound/WaveSegmentQueue.cpp"
