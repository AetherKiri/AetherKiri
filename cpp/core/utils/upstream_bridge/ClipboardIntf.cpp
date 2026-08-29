// Aether ABI bridge for the upstream Clipboard native class.
//
// Clipboard storage/platform calls remain Aether-owned.  This translation
// unit only supplies the portable TJS class registration and therefore uses
// the Aether interface header and calling-convention definition.
#include "../../tjs2/tjsCommHead.h"

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

#include "../ClipboardIntf.h"

#include "../../../../third_party/krkrz_dev/src/core/common/utils/ClipboardIntf.cpp"
