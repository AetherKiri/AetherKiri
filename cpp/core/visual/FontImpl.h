#pragma once
#include "tjs.h"
#include "tjsHashSearch.h"
#include <functional>
#include <freetype/freetype.h>
#include <vector>

const FT_Library TVPGetFontLibrary();

void TVPInitFontNames();
int TVPEnumFontsProc(const ttstr &FontPath);
const ttstr &TVPGetDefaultFontName();
void TVPGetAllFontList(std::vector<ttstr> &list);
tTJSBinaryStream *TVPCreateFontStream(const ttstr &fontname);
struct TVPFontNamePathInfo {
    ttstr Path;
    std::function<tTJSBinaryStream *(TVPFontNamePathInfo *)> Getter;
    int Index{};
};
TVPFontNamePathInfo *TVPFindFont(const ttstr &name);

//---------------------------------------------------------------------------
// font enumeration and existence check
//---------------------------------------------------------------------------
class tTVPttstrHash {
public:
    static tjs_uint32 Make(const ttstr &val);
};
extern tTJSHashTable<ttstr, TVPFontNamePathInfo, tTVPttstrHash> TVPFontNames;
