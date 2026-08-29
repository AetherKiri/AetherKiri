#define NCB_MODULE_NAME TJS_W("clipfile.dll")
#include "clipfile_compat.hpp"

// psdfile's Aether-owned storage adapter has the same historical helper
// names.  Rename only the upstream callback symbols inside this translation
// unit so both storage media can coexist in one static plugin archive.
#define initStorage clipfile_initStorage
#define doneStorage clipfile_doneStorage
#include <clipfile/main.cpp>
#undef doneStorage
#undef initStorage

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
