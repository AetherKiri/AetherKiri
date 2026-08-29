#include "krkrz_richtext_compat.hpp"

// The upstream file expects its tp_stub to publish FontServiceIntf. Aether's
// local ncbind header intentionally does not include that optional service, so
// the compatibility header primes it before the source is included.
#include <krkr_richtext/src/HostFontBackend.cpp>

#undef tjs_intptr_t
#undef TVPCreateStream
#undef iTJSBinaryStream
#ifdef AETHER_RICHTEXT_UNDEF_S_OK
#undef S_OK
#undef AETHER_RICHTEXT_UNDEF_S_OK
#endif
