// Compile the upstream AVX2 kernel against AetherKiri's bitmap ABI.  See the
// SSE2 adapter for why this is kept as a thin translation-unit wrapper.
#include "tjsCommHead.h"
#include "../LayerBitmapIntf.h"
#include "../impl/LayerBitmapImpl.h"
#include "ThreadIntf.h"
#include <float.h>
#include "ResampleImageInternal.h"
#include "KrkrzThreadCompat.h"

#define tTVPBaseBitmap iTVPBaseBitmap
#include "../../../../third_party/krkrz_dev/src/core/common/visual/gl/ResampleImageAVX2.cpp"
#undef tTVPBaseBitmap

#undef TVPExecThreadTask
#undef TVP_THREAD_PARAM
#undef TVPEndThreadTask
#undef TVPBeginThreadTask
