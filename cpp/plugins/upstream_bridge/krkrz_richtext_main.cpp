#define NCB_MODULE_NAME TJS_W("krkr_richtext.dll")
#include "krkrz_richtext_compat.hpp"

// Reuse the complete upstream registration and TJS surface. Aether keeps
// ownership of the VM, stream, font service, and global class registry; this
// file supplies only the ABI/module-name adaptation above.
#include <krkr_richtext/src/main.cpp>

#undef tjs_intptr_t
#undef TVPCreateStream
#undef iTJSBinaryStream
#ifdef AETHER_RICHTEXT_UNDEF_S_OK
#undef S_OK
#undef AETHER_RICHTEXT_UNDEF_S_OK
#endif
#undef NCB_MODULE_NAME
