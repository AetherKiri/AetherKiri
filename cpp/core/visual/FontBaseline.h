#pragma once

#include <algorithm>
#include <cstdint>

namespace krkr::font {

// Keep every glyph in a face on one baseline.  The line box may be shorter
// than a font's design ascender, so reserve the face's (not the individual
// glyph's) descent before clamping the baseline into the box.
inline int ComputeLineBaseline(int lineHeight, int ascenderUnits,
                               int descenderUnits, int pixelsPerEm,
                               int unitsPerEm) {
    if(lineHeight <= 0 || pixelsPerEm <= 0 || unitsPerEm <= 0)
        return 0;

    const auto scale = [pixelsPerEm, unitsPerEm](int units) {
        return static_cast<int>(static_cast<std::int64_t>(units) *
                                pixelsPerEm / unitsPerEm);
    };
    const int ascent = scale(ascenderUnits);
    const int descent = std::max(0, -scale(descenderUnits));
    return std::clamp(ascent, 0, std::max(0, lineHeight - descent));
}

inline int ComputeFallbackBaselineAdjustment(int requestedBaseline,
                                             int fallbackBaseline) {
    return requestedBaseline - fallbackBaseline;
}

inline int ComputeGlyphOriginY(int lineBaseline, int glyphBearingY) {
    return lineBaseline - glyphBearingY;
}

// Some script helpers render text at the very top of a small, transparent
// layer.  A face whose design ascender is taller than its logical line box can
// legitimately produce a negative glyph top.  Keep the shared line baseline
// unchanged, but move that one draw far enough into its clip for both the ink
// and its outline to remain visible.
inline int ClampTextOriginToClipTop(int originY, int glyphTop,
                                   int outlineWidth, int clipTop) {
    const int inkTop = originY + glyphTop - std::max(0, outlineWidth);
    return inkTop < clipTop ? originY + (clipTop - inkTop) : originY;
}

} // namespace krkr::font
