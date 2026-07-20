//---------------------------------------------------------------------------
// Optional recovery for a narrow class of malformed text scripts.
//---------------------------------------------------------------------------
#ifndef tjsScriptRecoveryH
#define tjsScriptRecoveryH

#include "tjs.h"

namespace TJS {

// Returns true only when removing one short identifier-like token from the
// end produces a script that parses completely in a throwaway script block.
// The caller remains responsible for executing the returned source.
bool TJSExperimentalRecoverTrailingToken(tTJS *engine, const ttstr &source,
                                          tjs_int error_position,
                                          ttstr &recovered);

} // namespace TJS

#endif
