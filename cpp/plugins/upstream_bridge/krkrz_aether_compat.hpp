#pragma once

// This header is intentionally small.  It establishes the Aether ABI before
// an upstream translation unit is included and documents the ownership rule:
// upstream business code may be reused, while tp_stub/ncbind implementations
// and plugin registries remain Aether-owned.

#include "PluginStub.h"
#include "ncbind.hpp"
#include "tjs.h"

// krkrz leaf sources assume the Windows ABI macro is supplied by their
// original build system.  Aether builds the same callbacks on every host and
// intentionally uses an empty expansion (matching the existing Aether
// plugins) unless a platform-specific ABI requires otherwise.
#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

#define AETHER_KRKRZ_SOURCE_ADAPTED 1

namespace aether::krkrz {

// krkrz's iTJSBinaryStream exposes Destruct(); Aether's tTJSBinaryStream is
// virtual-destructor/RAII based.  Adapters that own an Aether stream should
// use this helper instead of calling the upstream lifetime method.
inline void destroy_binary_stream(tTJSBinaryStream *stream) noexcept {
    delete stream;
}

} // namespace aether::krkrz
