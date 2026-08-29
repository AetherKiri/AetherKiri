//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Thread base class
//---------------------------------------------------------------------------
#ifndef ThreadIntfH
#define ThreadIntfH

#include "tjsNative.h"
#include <functional>

//---------------------------------------------------------------------------
// tTVPThreadPriority
//---------------------------------------------------------------------------
enum tTVPThreadPriority {
    ttpIdle,
    ttpLowest,
    ttpLower,
    ttpNormal,
    ttpHigher,
    ttpHighest,
    ttpTimeCritical
};
//---------------------------------------------------------------------------

// Keep the platform implementation addressable when this header is included
// through a downstream target (for example tjs2's debugger bridge) that only
// exports the utils root include directory.  Translation units that add the
// win32 directory directly continue to accept the same concrete type.
#include "win32/ThreadImpl.h"

/*[*/
const tjs_int TVPMaxThreadNum = 8;
typedef const std::function<void(int)> &TVP_THREAD_TASK_FUNC;
/*]*/

TJS_EXP_FUNC_DEF(tjs_int, TVPGetProcessorNum, ());

TJS_EXP_FUNC_DEF(tjs_int, TVPGetThreadNum, ());

TJS_EXP_FUNC_DEF(void, TVPExecThreadTask,
                 (int numThreads, TVP_THREAD_TASK_FUNC func));

#endif
