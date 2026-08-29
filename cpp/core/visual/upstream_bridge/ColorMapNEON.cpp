// Source bridge for krkrz's ARM NEON color-map leaves.
#include "../../tjs2/tjsCommHead.h"
#include "../tvpgl.h"

#if defined(__aarch64__) || defined(__arm64__) || defined(__ARM_NEON) || \
    defined(__ARM_NEON__)
#include "../../../../third_party/krkrz_dev/src/core/common/visual/gl/colormap_neon.cpp"
#endif
