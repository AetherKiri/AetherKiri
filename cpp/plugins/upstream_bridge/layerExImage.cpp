#define NCB_MODULE_NAME TJS_W("layerExImage.dll")
#include "krkrz_aether_compat.hpp"
#ifndef _WIN32
using BYTE = tjs_uint8;
using WORD = tjs_uint16;
struct RGBQUAD {
    BYTE rgbBlue;
    BYTE rgbGreen;
    BYTE rgbRed;
    BYTE rgbReserved;
};
#endif
#include <layerExImage/LayerExImage.cpp>
#include <layerExImage/Main.cpp>
#undef NCB_MODULE_NAME
