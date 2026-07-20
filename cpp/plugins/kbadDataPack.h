#pragma once

#include "tjs.h"

#include <cstddef>

// Decodes the structured data format identified by the "KBAD100\0"
// signature. Returns false when the input uses a different format.
bool TVPDecodeKbadDataPack(const void *data, std::size_t size,
                           tTJSVariant *result);

// Stream convenience wrapper used by Scripts.loadDataPack.
bool TVPLoadKbadDataPack(tTJSBinaryStream *stream, tTJSVariant *result);
