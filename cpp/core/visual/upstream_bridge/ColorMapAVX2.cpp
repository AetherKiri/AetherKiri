// Source bridge for krkrz's AVX2 color-map leaves.  AVX2 is enabled only for
// a single x86 target; universal/mobile builds keep the scalar/other-ISA path.
#include "../../tjs2/tjsCommHead.h"
#include "../tvpgl.h"

#if (defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || \
     defined(__x86_64__)) && defined(AETHER_KRKRZ_VISUAL_AVX2_COMPILED)
#include "../../../../third_party/krkrz_dev/src/core/common/visual/gl/colormap_avx2.cpp"
#endif
