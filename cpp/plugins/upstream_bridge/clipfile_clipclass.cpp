#define NCB_MODULE_NAME TJS_W("clipfile.dll")
#include "clipfile_compat.hpp"

// Compile the business implementation directly from the pinned submodule.
// No local copy is kept in AetherKiri.
#include <clipfile/clipclass.cpp>

#undef TVPCreateStream
#undef iTJSBinaryStream
#undef NCB_MODULE_NAME
#ifdef AETHER_CLIP_UNDEF_TJS_STRRCHR
#undef TJS_strrchr
#undef AETHER_CLIP_UNDEF_TJS_STRRCHR
#endif
#ifdef AETHER_CLIP_UNDEF_S_OK
#undef S_OK
#undef AETHER_CLIP_UNDEF_S_OK
#endif
