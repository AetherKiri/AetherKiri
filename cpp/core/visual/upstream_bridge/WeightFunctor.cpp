// Aether ABI bridge for the upstream resampling weight constants.
//
// The header remains Aether-owned so all visual targets see one declaration.
// The implementation contains no renderer state or host callbacks, so it is
// safe to compile directly from the pinned krkrz_dev submodule.
#include "../../tjs2/tjsCommHead.h"

#include <cfloat>

#include "../gl/WeightFunctor.h"

#include "../../../../third_party/krkrz_dev/src/core/common/visual/gl/WeightFunctor.cpp"
