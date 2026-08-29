#ifndef tjsDebuggerHookH
#define tjsDebuggerHookH

#include <atomic>

#include "tjsDebug.h"
#include "../utils/Debugger.h"

namespace TJS {

// The VM checks this relaxed flag before entering the hot-path hook.  The
// krkrz implementation owns the flag and forwards events to DebuggerCore;
// Aether only exposes the stable declaration.
extern std::atomic<bool> TVPDebuggerAttachedFlag;

inline bool TVPDebuggerWantsHook() {
    return TVPDebuggerAttachedFlag.load(std::memory_order_relaxed);
}

} // namespace TJS

#endif // tjsDebuggerHookH
