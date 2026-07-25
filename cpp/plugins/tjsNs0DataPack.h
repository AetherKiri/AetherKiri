#pragma once

#include "tjs.h"

// Decodes the structured data format identified by the TJS/ns0 or TJS/4s0
// signature. Returns false when the stream uses a different format.
bool TVPLoadTjsNs0DataPack(tTJSBinaryStream *stream, tTJSVariant *result,
                           const ttstr &outerIv = ttstr());

extern "C" void TVPRegisterTjsNs0DataPackLoader();
