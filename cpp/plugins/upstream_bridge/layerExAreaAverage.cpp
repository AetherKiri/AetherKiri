#define NCB_MODULE_NAME TJS_W("layerExAreaAverage.dll")
#include "krkrz_aether_compat.hpp"
#include "TickCount.h"
// The upstream tp_stub exposes a direct tjs_intptr_t conversion on
// tTJSVariant. Aether's variant intentionally uses its integer conversion;
// map the legacy spelling for the two pointer reads in this source file.
#define tjs_intptr_t tTVInteger
#include <layerExAreaAverage/main.cpp>
#undef tjs_intptr_t
#undef NCB_MODULE_NAME
