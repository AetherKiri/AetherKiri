#ifndef AETHER_FONT_VARIATIONS_H
#define AETHER_FONT_VARIATIONS_H

#include "tjsCommHead.h"

#include <utility>
#include <vector>

struct tTVPFont;
using tTVPFontAxisCoord = std::pair<tjs_uint32, float>;

ttstr TVPNormalizeFontVariations(const ttstr &spec);
void TVPParseFontVariations(const ttstr &spec,
                            std::vector<tTVPFontAxisCoord> &out);
void TVPFontGetEffectiveVarCoords(const tTVPFont &font,
                                  std::vector<tTVPFontAxisCoord> &out);
void TVPFontGetEffectiveVarCoords(tjs_int weight, const ttstr &variations,
                                  std::vector<tTVPFontAxisCoord> &out);
tjs_uint32 TVPFontVarPackTag(const char *tag, size_t len);

extern bool TVPFontDefaultUseVarStyle;
void TVPInvalidateFontOptions();

#endif
