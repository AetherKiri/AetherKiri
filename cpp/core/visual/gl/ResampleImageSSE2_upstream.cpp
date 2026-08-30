// Compile the upstream SSE2 kernel against AetherKiri's bitmap ABI.  The
// public engine uses iTVPBaseBitmap at this boundary; the upstream kernel
// only relies on the common bitmap methods, so the type name is adapted here
// without changing the vendored source tree.
#include "tjsCommHead.h"
#include "../LayerBitmapIntf.h"
#include "../impl/LayerBitmapImpl.h"
#include "ThreadIntf.h"
#include <float.h>
#include "ResampleImageInternal.h"
#include "KrkrzThreadCompat.h"

#define tTVPBaseBitmap iTVPBaseBitmap
#include "../../../../third_party/krkrz_dev/src/core/common/visual/gl/ResampleImageSSE2.cpp"
#undef tTVPBaseBitmap

#undef TVPExecThreadTask
#undef TVP_THREAD_PARAM
#undef TVPEndThreadTask
#undef TVPBeginThreadTask
