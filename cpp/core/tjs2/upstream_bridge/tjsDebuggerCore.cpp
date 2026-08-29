// Aether ABI bridge for the krkrz DAP state machine.  It is only compiled for
// desktop Debug builds with AETHERKIRI_ENABLE_DAP enabled; normal game builds
// retain the existing VM and incur no socket/JSON dependency.
#include "../tjsCommHead.h"
#include "../tjsDebuggerCore.h"
#include "../tjsDebuggerHook.h"
#include "../tjsInterCodeGen.h"
#include "../tjsScriptBlock.h"
#include "../tjsObject.h"
#include "../tjsDictionary.h"
#include "../tjs.h"
#include "../../base/CharacterSet.h"
#include "../../base/SysInitIntf.h"
#include "../../base/ScriptMgnIntf.h"
#include "../../base/StorageIntf.h"
#include "../../utils/DebugIntf.h"
#include "../../utils/DAPServer.h"
#include "../../utils/ReplFileChannel.h"

// Let the upstream pause loop drain Aether's file REPL while the VM is
// stopped at a breakpoint.  Only the small TVPDrainREPL symbol is shared;
// upstream's console/socket REPL implementation and worker thread are not
// linked, so Aether remains the sole VM/thread owner.
#define KRKRZ_USE_REPL 1

// krkrz's tTJSString::AsStdString() denotes UTF-16; Aether retains the
// historical UTF-8 meaning for compatibility.  Rename only the included
// upstream source's calls to the explicit adapter method.
#define AsStdString AsUtf16String

static inline int AetherTjsSnprintf(tjs_char *out, size_t capacity,
                                    const tjs_char *format, int value) {
    // The upstream debugger uses this helper only for "[%d]" register names.
    if(!out || capacity == 0)
        return 0;
    tjs_char number[40];
    TJS_int_to_str(value, number);
    const tjs_string rendered = tjs_string(TJS_W("[")) +
                                tjs_string(number) + tjs_string(TJS_W("]"));
    const size_t count = rendered.size() < capacity - 1
        ? rendered.size() : capacity - 1;
    if(count)
        std::char_traits<tjs_char>::copy(out, rendered.data(), count);
    out[count] = TJS_W('\0');
    (void)format;
    return static_cast<int>(count);
}
#define TJS_snprintf AetherTjsSnprintf

#include "../../../../third_party/krkrz_dev/src/core/common/tjs2/tjsDebuggerCore.cpp"

#undef TJS_snprintf
#undef AsStdString
#undef KRKRZ_USE_REPL
