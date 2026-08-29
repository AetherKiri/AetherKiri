#pragma once

// Establish Aether's string/variant/debugger ABI before the upstream
// declaration.  The krkrz header has no private include guard beyond pragma
// once and would otherwise pull in its own tjsString.h when this header is
// included from SysInitIntf.cpp.
#include "tjsString.h"
#include "tjsVariant.h"
#include "../utils/Debugger.h"

// The DAP state machine declaration remains in the pinned krkrz checkout;
// its implementation is consumed through the matching source bridge.
#include "../../../third_party/krkrz_dev/src/core/common/tjs2/tjsDebuggerCore.h"
