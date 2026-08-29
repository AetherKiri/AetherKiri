// Aether source bridge for krkrz's variable-font specification parser.
//
// The parser has no renderer, storage, or registry state.  The Aether-owned
// tTVPFont declaration is included first, then the implementation is compiled
// directly from the pinned submodule so the project does not retain a second
// copy of the algorithm.
#include "../../tjs2/tjsCommHead.h"
#include "../tvpfontstruc.h"
#include "../FontVariations.h"

#include "../../../../third_party/krkrz_dev/src/core/common/visual/FontVariations.cpp"
