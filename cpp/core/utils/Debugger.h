#pragma once

// The debugger breakpoint model is a portable, host-independent leaf. Keep
// its implementation in the pinned krkrz checkout and expose it through an
// Aether include so DAP/REPL adapters cannot grow a second breakpoint ABI.
#include "tjsTypes.h"
#include "../../../third_party/krkrz_dev/src/core/common/utils/Debugger.h"
