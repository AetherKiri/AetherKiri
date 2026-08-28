// Aether ABI bridge for the upstream UTF-8/UTF-16 conversion helper.
//
// krkrz names the string typedef `tjs_string`; Aether exposes the equivalent
// TJS string as `ttstr`.  The narrow macro adaptation is confined to this
// bridge, while CharacterSet and all public symbols remain Aether-owned.
#include "tjsCommHead.h"

#include "../CharacterSet.h"

#define tjs_string ttstr
#include "../../../../third_party/krkrz_dev/src/core/common/utils/MiscUtility.cpp"
#undef tjs_string
