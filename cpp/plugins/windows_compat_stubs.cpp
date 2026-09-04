#include <cstddef>

#ifdef _WIN32
#include "tjs.h"
#include "tjs2/tjsVariant.h"
#include "tjs2/tjsTypes.h"
#include "MsgIntf.h"

extern "C" void TVPRegisterDataPackCompatPluginAnchor() {}
extern "C" void TVPRegisterSliceLayerCompat() {}
extern "C" void TVPPreparePackinOneVirtualResources(const ttstr &, const tTJSVariant &) {}
#endif
