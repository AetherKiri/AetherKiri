#include "ncbind.hpp"

// Stub modules — register empty entries so Plugins.link() succeeds.
// The engine already has built-in support for the functionality these
// plugins originally provided (vorbis/opus decoding, transitions),
// but some games explicitly link them by name.

#define NCB_MODULE_NAME TJS_W("extrans.dll")
static void extrans_stub() {}
NCB_PRE_REGIST_CALLBACK(extrans_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("wuvorbis.dll")
static void wuvorbis_stub() {}
NCB_PRE_REGIST_CALLBACK(wuvorbis_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("wuopus.dll")
static void wuopus_stub() {}
NCB_PRE_REGIST_CALLBACK(wuopus_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("wuflac.dll")
static void wuflac_stub() {}
NCB_PRE_REGIST_CALLBACK(wuflac_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("layerExColor.dll")
static void layerExColor_stub() {}
NCB_PRE_REGIST_CALLBACK(layerExColor_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("layerExMosaic.dll")
static void layerExMosaic_stub() {}
NCB_PRE_REGIST_CALLBACK(layerExMosaic_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("layerExAVI.dll")
static void layerExAVI_stub() {}
NCB_PRE_REGIST_CALLBACK(layerExAVI_stub);
