#define NCB_MODULE_NAME TJS_W("getSample.dll")
#include "krkrz_aether_compat.hpp"
#include <getSample/main.cpp>

// Aether historically exposed this no-op capability probe.  Keep the public
// TJS surface while taking the implementation of the sampling methods from
// the pinned upstream source.
static tjs_error aetherEnableGetSampleGetter(
    tTJSVariant *result, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
    if(result)
        *result = static_cast<tjs_int>(1);
    return TJS_S_OK;
}
NCB_ATTACH_FUNCTION(enableGetSample, WaveSoundBuffer,
                    aetherEnableGetSampleGetter);

#undef NCB_MODULE_NAME
