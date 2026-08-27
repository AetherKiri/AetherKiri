#pragma once
#include "tjs.h"
#include "tjsHashSearch.h"
#include <functional>
#include <freetype/freetype.h>
#include <vector>

const FT_Library TVPGetFontLibrary();

void TVPInitFontNames();
// Enumerate a storage font and, when the storage table is not mounted yet,
// fall back to a readable native file path (used by krkrz loadFont).
int TVPEnumFontsProc(const ttstr &FontPath,
                     std::vector<ttstr> *fontNames = nullptr);
const ttstr &TVPGetDefaultFontName();
bool TVPSetDefaultFontName(const ttstr &fontName);
void TVPGetAllFontList(std::vector<ttstr> &list);
tTJSBinaryStream *TVPCreateFontStream(const ttstr &fontname);
struct TVPFontNamePathInfo {
    ttstr Path;
    std::function<tTJSBinaryStream *(TVPFontNamePathInfo *)> Getter;
    int Index{};
};
TVPFontNamePathInfo *TVPFindFont(const ttstr &name);

// Register a script-visible alias for an already enumerated font family.
//
// krkrz's layerExVector API lets a game choose a short name when loading a
// font file (GdiPlus.loadFont(path, name)).  Keeping the alias in the core
// font table means every renderer (FreeType, libgdiplus, Blend2D and the
// Windows backend) resolves the same stream without maintaining a second
// font registry in a plugin adapter.
bool TVPRegisterFontAlias(const ttstr &alias, const ttstr &fontName);

//---------------------------------------------------------------------------
// font enumeration and existence check
//---------------------------------------------------------------------------
class tTVPttstrHash {
public:
    static tjs_uint32 Make(const ttstr &val);
};
extern tTJSHashTable<ttstr, TVPFontNamePathInfo, tTVPttstrHash> TVPFontNames;
