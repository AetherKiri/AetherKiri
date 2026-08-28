// Aether ABI bridge for the upstream MD5-backed entropy pool.
//
// The public declarations and MD5/TJS headers are pre-included from Aether;
// their include guards prevent the upstream tree from introducing a second
// ABI or string/type definition.  The upstream translation unit itself is
// otherwise unchanged.
#include "tjsCommHead.h"

#include "../Random.h"
#include "../md5.h"
#include "../../tjs2/tjsUtils.h"

#include "../../../../third_party/krkrz_dev/src/core/common/utils/Random.cpp"
