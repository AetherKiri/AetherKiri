#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

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

// A blurred shadow grows by shadowWidth in every direction and is then moved
// by the requested shadow offset.  Return only the extra space needed above
// the unshadowed glyph; shadows that are moved far enough down need no extra
// top padding.
inline int ComputeTextShadowTopPadding(int shadowLevel, int shadowWidth,
                                       int shadowOffsetY) {
    if(shadowLevel == 0)
        return 0;

    const std::int64_t width = std::max<std::int64_t>(
        -static_cast<std::int64_t>(shadowWidth),
        static_cast<std::int64_t>(shadowWidth));
    const std::int64_t padding =
        std::max<std::int64_t>(0, width - shadowOffsetY);
    return static_cast<int>(std::min<std::int64_t>(
        padding, std::numeric_limits<int>::max()));
}

} // namespace krkr::font
