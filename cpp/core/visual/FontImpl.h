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
tTJSBinaryStream *TVPCreateFontStream(const ttstr &fontname,
                                      tjs_int *faceIndex);
// FontServiceIntf follows krkrz's one-argument stream API.  Keep an explicit
// overload instead of a default argument so the two public contracts cannot
// become ambiguous when both headers are included by a plugin.
tTJSBinaryStream *TVPCreateFontStream(const ttstr &fontname);
struct TVPFontNamePathInfo {
    ttstr Path;
    std::function<tTJSBinaryStream *(TVPFontNamePathInfo *)> Getter;
    int Index{};
    // Canonical family name reported by the selected face.  The hash-table
    // key may be a localized name or a script alias; keeping this value lets
    // native collections (for example Windows GDI+) resolve the same face
    // without maintaining a second alias table.
    ttstr FamilyName;
};
TVPFontNamePathInfo *TVPFindFont(const ttstr &name);

// Resolve a requested family to the shared Aether font table.  If the exact
// name is not registered, the configured default face is returned and
// resolvedName receives the face that will actually be used.  Keeping this
// fallback in the core prevents each compatibility adapter from maintaining a
// separate font lookup policy.
TVPFontNamePathInfo *TVPResolveFont(const ttstr &name,
                                    ttstr *resolvedName = nullptr);

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
