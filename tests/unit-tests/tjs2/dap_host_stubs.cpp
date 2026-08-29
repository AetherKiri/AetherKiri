// Minimal host boundary for the standalone TJS2 corpus executable.
//
// The product links the real Aether base/utils/environ modules.  This test
// intentionally links only the tjs2 archive so parser/VM regressions remain
// cheap and deterministic.  When the parent build enables the krkrz DAP
// bridge, the archive also contains the debugger state machine and therefore
// references a handful of host services.  These no-op implementations keep
// that isolated test honest without introducing a second runtime owner.

#include "tjsCommHead.h"
#include "../../../cpp/core/base/CharacterSet.h"
#include "../../../cpp/core/base/ScriptMgnIntf.h"
#include "../../../cpp/core/base/StorageIntf.h"
#include "../../../cpp/core/base/SysInitIntf.h"
#include "../../../cpp/core/utils/DAPServer.h"
#include "../../../cpp/core/utils/DebugIntf.h"
#include "../../../cpp/core/utils/ThreadIntf.h"

#include <utility>

// The standalone test never starts a DAP worker.  Keep the base object in a
// valid, non-running state so the debugger constructor/destructor ABI remains
// linkable while no socket or thread can leak into the test process.
tTVPThread::tTVPThread(bool suspended)
    : Terminated(false), Suspended(suspended) {}

tTVPThread::~tTVPThread() = default;

bool TVPGetCommandLine(const tjs_char *, tTJSVariant *) { return false; }

void TVPTerminateAsync(int) {}

void TVPDrainREPL() {}

ttstr TVPPrettyPrint(const tTJSVariant &, int, bool) {
    return ttstr();
}

iTJSDispatch2 *TVPGetScriptDispatch() { return nullptr; }

ttstr TVPNormalizeStorageName(const ttstr &name) { return name; }

ttstr TVPGetLocallyAccessibleName(const ttstr &name) { return name; }

// The three-argument overload is used by the krkrz debugger core; the
// five-argument overload lives in stubs.cpp for the legacy VM tests.
void TVPExecuteExpression(const ttstr &, iTJSDispatch2 *, tTJSVariant *) {}

// DAP transport methods are deliberately inert here.  The real socket
// implementation is compiled into core_utils_module and exercised by the
// product/plugin contract tests.
tTVPDAPServerThread::tTVPDAPServerThread(
    int port, NotifyCallback on_message)
    : tTVPThread(true), port_(port), on_message_(std::move(on_message)) {}

tTVPDAPServerThread::~tTVPDAPServerThread() = default;

bool tTVPDAPServerThread::TryPopMessage(picojson::value &) { return false; }

void tTVPDAPServerThread::PostMessage(const picojson::value &) {}

void tTVPDAPServerThread::Shutdown() {
    terminating_.store(true, std::memory_order_release);
}

void tTVPDAPServerThread::Execute() {}

void TVPCreateDAPServer(int, tTVPDAPServerThread::NotifyCallback) {}
void TVPDestroyDAPServer() {}
tTVPDAPServerThread *TVPGetDAPServer() { return nullptr; }
