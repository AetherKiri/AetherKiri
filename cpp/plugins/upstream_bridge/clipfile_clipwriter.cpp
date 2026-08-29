#define NCB_MODULE_NAME TJS_W("clipfile.dll")
#include "clipfile_compat.hpp"

#include <clipfile/clipwriter.cpp>

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
