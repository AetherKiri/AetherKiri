#pragma once

#include "tjs.h"

#include <string>

// Aether-owned file REPL boundary.  The pinned krkrz checkout provides the
// protocol semantics as reference, but its console/socket implementation is
// tied to a different stream and thread ABI.  This adapter intentionally
// executes one command on the existing Aether main thread, so it cannot race
// the VM or create a second ThreadImpl owner.
void TVPCreateREPL();
void TVPDestroyREPL();
void TVPDrainREPL();
bool TVPReplFileChannelActive();

namespace TVPRepl {

// Small pure helpers are kept public for contract tests and host tooling.
std::string JsonEscape(const std::string &value);
bool IsDisabledOption(const ttstr &value);

} // namespace TVPRepl
