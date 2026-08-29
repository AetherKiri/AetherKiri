// The SSE leaf uses the small constant table from krkrz's xmmlib.  It is a
// data-only source with no host lifecycle, so consume it directly from the
// pinned submodule instead of maintaining a second table in Aether.
#include "../../tjs2/tjsCommHead.h"
#include "../xmmlib.h"

#include "../../../../third_party/krkrz_dev/src/core/common/sound/xmmlib.cpp"
