//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// System Initialization and Uninitialization
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include <vector>
#include <algorithm>
#include <functional>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#include "tjsUtils.h"
#include "SysInitIntf.h"
#include "ScriptMgnIntf.h"
#include "tvpgl.h"
#include "../utils/ReplFileChannel.h"
#ifdef KRKRZ_ENABLE_DAP
#include "tjsDebuggerCore.h"
#endif

//---------------------------------------------------------------------------
// global data
//---------------------------------------------------------------------------
ttstr TVPProjectDir; // project directory (in unified storage name)
ttstr TVPDataPath; // data directory (in unified storage name)
//---------------------------------------------------------------------------

extern void TVPGL_C_Init();

//---------------------------------------------------------------------------
// TVPSystemInit : Entire System Initialization
//---------------------------------------------------------------------------
void TVPSystemInit() {
#ifdef _WIN32
#ifdef USING_PROTECT
    while(!TVPProtectInit()) {
        TVPUpdateLicense();
    }
#endif
#endif

    TVPBeforeSystemInit();

    TVPInitScriptEngine();

    TVPInitTVPGL();
    //	TVPGL_C_Init();

    TVPAfterSystemInit();
    // The file REPL is an opt-in development channel selected by
    // -replfile=<directory>.  Its implementation is Aether-owned and runs
    // commands on the existing VM thread; no second console/thread ABI is
    // linked into the product.
    TVPCreateREPL();
#ifdef KRKRZ_ENABLE_DAP
    // The server is inert unless -dap is present.  Start it only after the
    // script/graphics systems are ready so an early attach can inspect the
    // startup script safely.
    TVPCreateDAP();
#endif
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPSystemUninit : System shutdown, cleanup, etc...
//---------------------------------------------------------------------------
static void TVPCauseAtExit();

bool TVPSystemUninitCalled = false;

void TVPSystemUninit() {
    if(TVPSystemUninitCalled)
        return;
    TVPSystemUninitCalled = true;

    TVPDestroyREPL();
#ifdef KRKRZ_ENABLE_DAP
    // Stop the socket worker before tearing down the VM and variant storage;
    // a debugger hook can otherwise race script-engine destruction.
    TVPDestroyDAP();
#endif

    TVPBeforeSystemUninit();

    TVPUninitTVPGL();

    try {
        TVPUninitScriptEngine();
    } catch(...) {
        // ignore errors
    }

    TVPAfterSystemUninit();

    TVPCauseAtExit();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPAddAtExitHandler related
//---------------------------------------------------------------------------
struct tTVPAtExitInfo {
    tTVPAtExitInfo(tjs_int pri, void (*handler)()) {
        Priority = pri, Handler = handler;
    }

    tjs_int Priority;

    void (*Handler)();

    bool operator<(const tTVPAtExitInfo &r) const {
        return this->Priority < r.Priority;
    }

    bool operator>(const tTVPAtExitInfo &r) const {
        return this->Priority > r.Priority;
    }

    bool operator==(const tTVPAtExitInfo &r) const {
        return this->Priority == r.Priority;
    }
};

static std::vector<tTVPAtExitInfo> *TVPAtExitInfos = nullptr;
static bool TVPAtExitShutdown = false;

//---------------------------------------------------------------------------
void TVPAddAtExitHandler(tjs_int pri, void (*handler)()) {
    if(TVPAtExitShutdown)
        return;

    if(!TVPAtExitInfos)
        TVPAtExitInfos = new std::vector<tTVPAtExitInfo>();
    TVPAtExitInfos->emplace_back(pri, handler);
}

//---------------------------------------------------------------------------
static void TVPCauseAtExit() {
    if(TVPAtExitShutdown)
        return;
    TVPAtExitShutdown = true;

    std::sort(TVPAtExitInfos->begin(),
              TVPAtExitInfos->end()); // descending sort

    for(auto i = TVPAtExitInfos->begin(); i != TVPAtExitInfos->end(); ++i) {
        i->Handler();
    }

    delete TVPAtExitInfos;
}
//---------------------------------------------------------------------------
