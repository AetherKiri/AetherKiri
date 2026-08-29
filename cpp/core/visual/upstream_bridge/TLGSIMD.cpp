// Source bridge for krkrz's standalone TLG SSE2 kernels.  The upstream file
// is included only for x86 translation units; Aether's LoadTLG.cpp remains the
// owner of format detection, virtual streams, metadata, and error handling.
#include "../../tjs2/tjsCommHead.h"
#include "../TLGSIMD.h"

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || \
    defined(__x86_64__)
#include "../../../../third_party/krkrz_dev/src/core/common/visual/gl/tlg_sse2.cpp"
#endif
