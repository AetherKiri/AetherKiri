#include "ncbind.hpp"
#include "extrans_precise/mosaic.h"
#include "extrans_precise/ripple.h"
#include "extrans_precise/rotatetrans.h"
#include "extrans_precise/turn.h"
#include "extrans_precise/wave.h"

void initExtrans() {
    RegisterWaveTransHandlerProvider();
    RegisterMosaicTransHandlerProvider();
    RegisterTurnTransHandlerProvider();
    RegisterRotateTransHandlerProvider();
    RegisterRippleTransHandlerProvider();
}

void doneExtrans() {
    UnregisterWaveTransHandlerProvider();
    UnregisterMosaicTransHandlerProvider();
    UnregisterTurnTransHandlerProvider();
    UnregisterRotateTransHandlerProvider();
    UnregisterRippleTransHandlerProvider();
}

#define NCB_MODULE_NAME TJS_W("extrans.dll")
NCB_PRE_REGIST_CALLBACK(initExtrans);
NCB_POST_UNREGIST_CALLBACK(doneExtrans);

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
