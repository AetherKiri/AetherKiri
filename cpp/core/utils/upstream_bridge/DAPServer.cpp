// Aether ABI bridge for the krkrz Content-Length framed DAP transport.
#include "../../tjs2/tjsCommHead.h"
#include "../DAPServer.h"
#include "../ThreadIntf.h"
#include "../DebugIntf.h"
#include "../../base/CharacterSet.h"

// krkrz's tTVPThread("name") + StartThread() pair maps to Aether's
// suspended bool constructor + Resume().  Keeping the translation local
// avoids changing the public thread ABI or linking a second ThreadImpl.
#define tTVPThread(name) tTVPThread(true)
#define StartThread() Resume()
#include "../../../../third_party/krkrz_dev/src/core/common/utils/DAPServer.cpp"
#undef StartThread
#undef tTVPThread
