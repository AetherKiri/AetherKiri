#pragma once

// Keep the public FontService declaration in the pinned krkrz_dev checkout.
// Aether's binary-stream class predates krkrz's iTJSBinaryStream spelling, so
// provide the narrow source-compatibility aliases before including the
// upstream API.  No FontService implementation is copied into this tree.
#include "tjsCommHead.h"

using iTJSBinaryStream = TJS::tTJSBinaryStream;
using TJS::ttstr;

#include "../../../third_party/krkrz_dev/src/core/common/visual/FontServiceIntf.h"

// Aether-only extensions used by adapters that expose an explicit TTC/OTC
// face index.  The upstream service resolves a name/path to the registry's
// default face; richtext's registerFont(path, name, index) contract needs a
// way to request another face without introducing a second font registry.
tTVPFontFaceHandle TVPFontAcquireFaceAt(const ttstr &nameOrPath,
                                        tjs_int faceIndex);
tTVPFontFaceHandle TVPFontAcquireFaceInstanceAt(
    const ttstr &nameOrPath, tjs_int faceIndex,
    const tTVPFontVarCoord *coords, tjs_int count);
bool TVPFontGetFaceInfoAt(const ttstr &nameOrPath, tjs_int faceIndex,
                          tTVPFontFaceInfo *out);
