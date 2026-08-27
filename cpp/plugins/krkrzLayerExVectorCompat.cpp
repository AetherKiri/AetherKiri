// Compatibility bridge for krkrz_dev's layerExVector module.
//
// The upstream plugin owns a ThorVG renderer and a second set of GDI+/Layer
// native classes.  Linking that implementation beside Aether's LayerExDraw
// would duplicate global TJS classes and renderer state.  This bridge keeps a
// single Aether renderer and exposes the small vector-facing surface that
// games commonly use (font loading/aliases and drawStringArea).

#include "FontImpl.h"
#include "ncbind.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

using Callback = tTJSNativeClassMethodCallback;

// The vector module is attached after layerExDraw has registered its native
// methods.  Keep strong references to those original dispatches before the
// compatibility callbacks are attached, so the callbacks can extend the
// behaviour without recursively calling themselves.
tTJSVariant canonicalDrawString;
tTJSVariant canonicalMeasureString;

bool isNumber(const tTJSVariant *value) {
    return value && (value->Type() == tvtInteger || value->Type() == tvtReal);
}

bool isVoid(const tTJSVariant *value) {
    return !value || value->Type() == tvtVoid;
}

bool getMember(iTJSDispatch2 *object, const tjs_char *name,
               tTJSVariant &value) {
    if(!object)
        return false;
    return TJS_SUCCEEDED(
        object->PropGet(0, name, nullptr, &value, object));
}

bool setMember(iTJSDispatch2 *object, const tjs_char *name,
               const tTJSVariant &value) {
    if(!object)
        return false;
    return TJS_SUCCEEDED(object->PropSet(
        TJS_MEMBERENSURE, name, nullptr, &value, object));
}

bool callMember(iTJSDispatch2 *object, const tjs_char *name,
                tTJSVariant &result, tjs_int numparams,
                tTJSVariant **params) {
    tTJSVariant method;
    if(!getMember(object, name, method) || method.Type() != tvtObject)
        return false;
    return TJS_SUCCEEDED(method.AsObjectClosureNoAddRef().FuncCall(
                      0, nullptr, nullptr, &result, numparams, params, object));
}

bool callDispatch(const tTJSVariant &dispatch, iTJSDispatch2 *object,
                  tTJSVariant &result, tjs_int numparams,
                  tTJSVariant **params) {
    if(dispatch.Type() != tvtObject || !object)
        return false;
    return TJS_SUCCEEDED(dispatch.AsObjectClosureNoAddRef().FuncCall(
        0, nullptr, nullptr, &result, numparams, params, object));
}

bool getGlobalObject(const tjs_char *name, tTJSVariant &value) {
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(!global)
        return false;
    const bool ok = getMember(global, name, value);
    global->Release();
    return ok;
}

void captureCanonicalSurface() {
    tTJSVariant layerClass;
    if(getGlobalObject(TJS_W("Layer"), layerClass) &&
       layerClass.Type() == tvtObject) {
        tTJSVariant method;
        if(getMember(layerClass.AsObjectNoAddRef(), TJS_W("drawString"),
                     method) &&
           method.Type() == tvtObject) {
            canonicalDrawString = method;
        }
        if(getMember(layerClass.AsObjectNoAddRef(), TJS_W("measureString"),
                     method) &&
           method.Type() == tvtObject) {
            canonicalMeasureString = method;
        }
    }

}

void restoreCanonicalSurface() {
    tTJSVariant layerClass;
    if(getGlobalObject(TJS_W("Layer"), layerClass) &&
       layerClass.Type() == tvtObject) {
        iTJSDispatch2 *layer = layerClass.AsObjectNoAddRef();
        if(canonicalDrawString.Type() == tvtObject)
            setMember(layer, TJS_W("drawString"), canonicalDrawString);
        if(canonicalMeasureString.Type() == tvtObject)
            setMember(layer, TJS_W("measureString"), canonicalMeasureString);
    }

}

bool getRealMember(iTJSDispatch2 *object, const tjs_char *name,
                   tjs_real &value) {
    tTJSVariant member;
    if(!getMember(object, name, member))
        return false;
    if(member.Type() != tvtInteger && member.Type() != tvtReal)
        return false;
    value = static_cast<tjs_real>(member);
    return true;
}

bool getIntegerMember(iTJSDispatch2 *object, const tjs_char *name,
                      tjs_int &value) {
    tTJSVariant member;
    if(!getMember(object, name, member))
        return false;
    if(member.Type() != tvtInteger && member.Type() != tvtReal)
        return false;
    value = static_cast<tjs_int>(member);
    return true;
}

struct RectInfo {
    tjs_real left = 0;
    tjs_real top = 0;
    tjs_real width = 0;
    tjs_real height = 0;
    bool valid = false;
};

bool readRect(const tTJSVariant &value, RectInfo &rect) {
    if(value.Type() != tvtObject)
        return false;
    iTJSDispatch2 *object = value.AsObjectNoAddRef();
    if(!object)
        return false;

    tjs_real x = 0;
    tjs_real y = 0;
    tjs_real width = 0;
    tjs_real height = 0;
    if(!getRealMember(object, TJS_W("x"), x))
        getRealMember(object, TJS_W("left"), x);
    if(!getRealMember(object, TJS_W("y"), y))
        getRealMember(object, TJS_W("top"), y);
    if(!getRealMember(object, TJS_W("width"), width)) {
        tjs_real right = 0;
        if(getRealMember(object, TJS_W("right"), right))
            width = right - x;
    }
    if(!getRealMember(object, TJS_W("height"), height)) {
        tjs_real bottom = 0;
        if(getRealMember(object, TJS_W("bottom"), bottom))
            height = bottom - y;
    }
    rect.left = x;
    rect.top = y;
    rect.width = std::max<tjs_real>(0, width);
    rect.height = std::max<tjs_real>(0, height);
    rect.valid = true;
    return true;
}

void unionRect(RectInfo &target, const RectInfo &source) {
    if(!source.valid)
        return;
    if(!target.valid) {
        target = source;
        return;
    }
    const tjs_real right = std::max(target.left + target.width,
                                   source.left + source.width);
    const tjs_real bottom = std::max(target.top + target.height,
                                     source.top + source.height);
    target.left = std::min(target.left, source.left);
    target.top = std::min(target.top, source.top);
    target.width = std::max<tjs_real>(0, right - target.left);
    target.height = std::max<tjs_real>(0, bottom - target.top);
}

tTJSVariant makeRect(const RectInfo &rect) {
    // Prefer the canonical LayerExDraw RectF class so callers retain the
    // upstream object contract.  A dictionary fallback keeps this bridge
    // harmless on builds where no native vector renderer is available.
    tTJSVariant rectClass;
    if(getGlobalObject(TJS_W("GdiPlus"), rectClass) &&
       rectClass.Type() == tvtObject) {
        iTJSDispatch2 *gdiPlus = rectClass.AsObjectNoAddRef();
        tTJSVariant rectCtor;
        if(getMember(gdiPlus, TJS_W("RectF"), rectCtor) &&
           rectCtor.Type() == tvtObject) {
            tTJSVariant x(rect.left), y(rect.top), width(rect.width),
                height(rect.height);
            tTJSVariant *params[] = { &x, &y, &width, &height };
            iTJSDispatch2 *created = nullptr;
            if(TJS_SUCCEEDED(rectCtor.AsObjectClosureNoAddRef().CreateNew(
                   0, nullptr, nullptr, &created, 4, params, nullptr)) &&
               created) {
                tTJSVariant result(created, created);
                created->Release();
                return result;
            }
        }
    }

    iTJSDispatch2 *dictionary = TJSCreateDictionaryObject();
    if(!dictionary)
        return tTJSVariant();
    const tTJSVariant x(rect.left), y(rect.top), width(rect.width),
        height(rect.height);
    setMember(dictionary, TJS_W("x"), x);
    setMember(dictionary, TJS_W("y"), y);
    setMember(dictionary, TJS_W("width"), width);
    setMember(dictionary, TJS_W("height"), height);
    const tTJSVariant result(dictionary, dictionary);
    dictionary->Release();
    return result;
}

bool measureText(iTJSDispatch2 *layer, const tTJSVariant &font,
                 const ttstr &text, RectInfo &rect) {
    tTJSVariant textValue(text);
    tTJSVariant *params[] = { const_cast<tTJSVariant *>(&font), &textValue };
    tTJSVariant measured;
    if(!callMember(layer, TJS_W("measureString"), measured, 2, params))
        return false;
    return readRect(measured, rect);
}

bool measureCanonicalText(iTJSDispatch2 *layer, const tTJSVariant &font,
                          const ttstr &text, RectInfo &rect) {
    if(canonicalMeasureString.Type() != tvtObject || !layer)
        return false;
    tTJSVariant textValue(text);
    tTJSVariant *params[] = { const_cast<tTJSVariant *>(&font), &textValue };
    tTJSVariant measured;
    if(!callDispatch(canonicalMeasureString, layer, measured, 2, params))
        return false;
    return readRect(measured, rect);
}

bool drawText(iTJSDispatch2 *layer, const tTJSVariant &font,
              const tTJSVariant &appearance, tjs_real x, tjs_real y,
              const ttstr &text, RectInfo &rect) {
    tTJSVariant xValue(x), yValue(y), textValue(text);
    tTJSVariant *params[] = { const_cast<tTJSVariant *>(&font),
                              const_cast<tTJSVariant *>(&appearance),
                              &xValue, &yValue, &textValue };
    tTJSVariant drawn;
    if(!callMember(layer, TJS_W("drawString"), drawn, 5, params))
        return false;
    return readRect(drawn, rect);
}

bool drawCanonicalText(iTJSDispatch2 *layer, const tTJSVariant &font,
                       const tTJSVariant &appearance, tjs_real x, tjs_real y,
                       const ttstr &text, RectInfo &rect) {
    if(canonicalDrawString.Type() != tvtObject || !layer)
        return false;
    tTJSVariant xValue(x), yValue(y), textValue(text);
    tTJSVariant *params[] = { const_cast<tTJSVariant *>(&font),
                              const_cast<tTJSVariant *>(&appearance),
                              &xValue, &yValue, &textValue };
    tTJSVariant drawn;
    if(!callDispatch(canonicalDrawString, layer, drawn, 5, params))
        return false;
    return readRect(drawn, rect);
}

std::vector<ttstr> splitLines(const ttstr &text) {
    std::vector<ttstr> lines;
    ttstr current;
    for(tjs_int i = 0; i < text.length(); ++i) {
        const tjs_char ch = text[i];
        if(ch == TJS_W('\r'))
            continue;
        if(ch == TJS_W('\n')) {
            lines.emplace_back(current);
            current = ttstr();
        } else {
            current += ch;
        }
    }
    lines.emplace_back(current);
    return lines;
}

ttstr sliceString(const ttstr &text, tjs_int start, tjs_int count) {
    ttstr result;
    if(start < 0)
        start = 0;
    const tjs_int end = std::min<tjs_int>(text.length(), start +
                                                           std::max<tjs_int>(0, count));
    for(tjs_int index = start; index < end; ++index)
        result += text[index];
    return result;
}

// When the caller omits the optional alias, krkrz derives the registration
// name from the file stem (rather than from the font's internal family name).
// Keep that convention so scripts can consistently use
// `new GdiPlus.Font("my_font", size)` after `loadFont("my_font.ttf")`.
ttstr defaultFontAlias(const ttstr &path) {
    tjs_int start = 0;
    for(tjs_int index = 0; index < path.length(); ++index) {
        if(path[index] == TJS_W('/') || path[index] == TJS_W('\\'))
            start = index + 1;
    }
    tjs_int end = path.length();
    for(tjs_int index = end; index > start; --index) {
        if(path[index - 1] == TJS_W('.')) {
            end = index - 1;
            break;
        }
    }
    return sliceString(path, start, end - start);
}

// TJS strings are UTF-16 on every supported target.  Keep surrogate pairs in
// one drawing unit when applying the per-character letter-spacing fallback.
std::vector<ttstr> splitTextUnits(const ttstr &text) {
    std::vector<ttstr> units;
    for(tjs_int index = 0; index < text.length(); ++index) {
        tjs_int count = 1;
        const tjs_char high = text[index];
        if(high >= static_cast<tjs_char>(0xD800) &&
           high <= static_cast<tjs_char>(0xDBFF) && index + 1 < text.length()) {
            const tjs_char low = text[index + 1];
            if(low >= static_cast<tjs_char>(0xDC00) &&
               low <= static_cast<tjs_char>(0xDFFF)) {
                count = 2;
            }
        }
        units.emplace_back(sliceString(text, index, count));
        index += count - 1;
    }
    return units;
}

tjs_real getLetterSpacing(const tTJSVariant &font) {
    tjs_real spacing = 1.0;
    if(font.Type() != tvtObject)
        return spacing;
    if(!getRealMember(font.AsObjectNoAddRef(), TJS_W("letterSpacing"),
                      spacing) ||
       !std::isfinite(static_cast<double>(spacing)) || spacing <= 0) {
        return 1.0;
    }
    return spacing;
}

tjs_real getCanonicalLineHeight(const tTJSVariant &font) {
    if(font.Type() != tvtObject)
        return 0;
    tjs_real lineHeight = 0;
    // The canonical Aether FontInfo exposes a read-only pixel metric.  The
    // vector API uses the same property name for a writable scale (1.0 is
    // normal), so the native metric is published through a private sibling
    // property by each renderer backend.
    if(!getRealMember(font.AsObjectNoAddRef(),
                      TJS_W("aetherNativeLineSpacing"), lineHeight) &&
       !getRealMember(font.AsObjectNoAddRef(), TJS_W("lineSpacing"),
                      lineHeight)) {
        return 0;
    }
    return std::isfinite(static_cast<double>(lineHeight)) && lineHeight > 0
               ? lineHeight
               : 0;
}

tjs_real getLineSpacingScale(const tTJSVariant &font) {
    if(font.Type() != tvtObject)
        return 1.0;
    tjs_real scale = 1.0;
    if(!getRealMember(font.AsObjectNoAddRef(),
                      TJS_W("aetherKrkrzLineSpacing"), scale) ||
       !std::isfinite(static_cast<double>(scale)) || scale < 0) {
        return 1.0;
    }
    return scale;
}

std::vector<ttstr> wrapLine(iTJSDispatch2 *layer, const tTJSVariant &font,
                            const ttstr &line, tjs_real maxWidth,
                            tjs_int wrap) {
    if(wrap == 0 || maxWidth <= 0 || line.length() <= 1)
        return { line };

    if(wrap == 4) {
        // ThorVG's Ellipsis mode is single-line: retain the longest prefix
        // that fits together with "...".  If even the marker does not fit,
        // return an empty prefix rather than overflowing the requested box.
        const ttstr marker(TJS_W("..."));
        RectInfo full;
        if(measureText(layer, font, line, full) && full.width <= maxWidth)
            return { line };
        RectInfo markerBounds;
        if(!measureText(layer, font, marker, markerBounds) ||
           markerBounds.width > maxWidth)
            return { ttstr() };
        tjs_int best = 0;
        for(tjs_int end = 1; end <= line.length(); ++end) {
            const ttstr candidate = sliceString(line, 0, end) + marker;
            RectInfo measured;
            if(!measureText(layer, font, candidate, measured) ||
               measured.width > maxWidth)
                break;
            best = end;
        }
        return { sliceString(line, 0, best) + marker };
    }

    std::vector<ttstr> result;
    tjs_int start = 0;
    const tjs_int length = line.length();
    while(start < length) {
        tjs_int best = start + 1;
        for(tjs_int end = start + 1; end <= length; ++end) {
            const ttstr candidate = sliceString(line, start, end - start);
            RectInfo measured;
            if(!measureText(layer, font, candidate, measured))
                break;
            if(measured.width <= maxWidth || end == start + 1) {
                best = end;
            } else {
                break;
            }
        }

        // Word wrapping prefers the last whitespace that still fits.  Mixed
        // and character modes intentionally fall back to the same safe
        // character boundary when no word boundary is available.
        if(wrap == 2 || wrap == 3) {
            for(tjs_int i = best; i > start; --i) {
                const tjs_char ch = line[i - 1];
                if(ch == TJS_W(' ') || ch == TJS_W('\t')) {
                    best = i;
                    break;
                }
            }
        }
        if(best <= start)
            best = start + 1;
        ttstr piece = sliceString(line, start, best - start);
        while(!piece.IsEmpty() &&
              (piece[0] == TJS_W(' ') || piece[0] == TJS_W('\t'))) {
            piece = sliceString(piece, 1, piece.length() - 1);
        }
        while(!piece.IsEmpty() &&
              (piece[piece.length() - 1] == TJS_W(' ') ||
               piece[piece.length() - 1] == TJS_W('\t'))) {
            piece = sliceString(piece, 0, piece.length() - 1);
        }
        result.emplace_back(piece);
        start = best;
        while(start < length &&
              (line[start] == TJS_W(' ') || line[start] == TJS_W('\t'))) {
            ++start;
        }
    }
    if(result.empty())
        result.emplace_back(ttstr());
    return result;
}

bool measureTextWithSpacing(iTJSDispatch2 *layer, const tTJSVariant &font,
                            const ttstr &text, tjs_real letterSpacing,
                            tjs_real lineSpacingScale, RectInfo &rect) {
    rect = RectInfo();
    const auto lines = splitLines(text);
    tjs_real lineHeight = 0;
    for(const auto &line : lines) {
        tjs_real lineWidth = 0;
        tjs_real lineTop = 0;
        tjs_real lineBottom = 0;
        for(const auto &unit : splitTextUnits(line)) {
            RectInfo measured;
            if(!measureCanonicalText(layer, font, unit, measured))
                return false;
            lineWidth += std::max<tjs_real>(0, measured.width) * letterSpacing;
            if(!rect.valid && lineWidth == measured.width * letterSpacing)
                rect.left = measured.left;
            lineTop = std::min(lineTop, measured.top);
            lineBottom = std::max(lineBottom, measured.top + measured.height);
        }
        if(line.IsEmpty()) {
            RectInfo measured;
            if(measureCanonicalText(layer, font, ttstr(), measured)) {
                lineTop = measured.top;
                lineBottom = measured.top + measured.height;
            }
        }
        lineHeight = std::max(lineHeight, lineBottom - lineTop);
        rect.width = std::max(rect.width, lineWidth);
    }
    if(lineHeight <= 0)
        lineHeight = getCanonicalLineHeight(font);
    if(lineHeight <= 0) {
        tjs_real fontSize = 0;
        if(!getRealMember(font.AsObjectNoAddRef(), TJS_W("fontSize"),
                          fontSize))
            getRealMember(font.AsObjectNoAddRef(), TJS_W("emSize"), fontSize);
        lineHeight = fontSize > 0 ? fontSize : 1;
    }
    // Aether supplies the native pixel metric; krkrz expresses lineSpacing as
    // a scale.  Apply the scale only in the compatibility path so ordinary
    // LayerExDraw users retain their established metrics.
    rect.height = lineHeight * lineSpacingScale * lines.size();
    rect.valid = true;
    return true;
}

bool drawTextWithSpacing(iTJSDispatch2 *layer, const tTJSVariant &font,
                         const tTJSVariant &appearance, tjs_real x, tjs_real y,
                         const ttstr &text, tjs_real letterSpacing,
                         tjs_real lineSpacingScale, RectInfo &rect) {
    rect = RectInfo();
    const auto lines = splitLines(text);
    tjs_real lineHeight = getCanonicalLineHeight(font);
    if(lineHeight <= 0) {
        RectInfo measured;
        if(measureCanonicalText(layer, font, ttstr(TJS_W("M")), measured))
            lineHeight = measured.height;
    }
    if(lineHeight <= 0) {
        tjs_real fontSize = 0;
        if(!getRealMember(font.AsObjectNoAddRef(), TJS_W("fontSize"),
                          fontSize))
            getRealMember(font.AsObjectNoAddRef(), TJS_W("emSize"), fontSize);
        lineHeight = fontSize > 0 ? fontSize : 1;
    }
    lineHeight *= lineSpacingScale;
    for(size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        tjs_real cursorX = x;
        for(const auto &unit : splitTextUnits(lines[lineIndex])) {
            RectInfo measured;
            if(!measureCanonicalText(layer, font, unit, measured))
                return false;
            RectInfo drawn;
            if(!drawCanonicalText(layer, font, appearance, cursorX,
                                  y + static_cast<tjs_real>(lineIndex) * lineHeight,
                                  unit, drawn))
                return false;
            unionRect(rect, drawn);
            cursorX += std::max<tjs_real>(0, measured.width) * letterSpacing;
        }
    }
    if(!rect.valid) {
        rect.left = x;
        rect.top = y;
        rect.valid = true;
    }
    return true;
}

void loadLayerExDrawForVector() {
    try {
        if(!ncbAutoRegister::LoadModule(TJS_W("layerExDraw.dll"))) {
            TVPAddLog(TJS_W("layerExVector compatibility: layerExDraw is not "
                           "available on this target"));
        } else {
            captureCanonicalSurface();
            TVPAddLog(TJS_W("layerExVector compatibility: using the AetherKiri "
                           "LayerExDraw renderer"));
        }
    } catch(...) {
        TVPAddLog(TJS_W("layerExVector compatibility: failed to load "
                       "layerExDraw"));
    }
}

tjs_error loadFontCompat(tTJSVariant *result, tjs_int numparams,
                         tTJSVariant **param, iTJSDispatch2 *) {
    if(result)
        *result = false;
    if(numparams < 1 || numparams > 2 || !param)
        return TJS_E_BADPARAMCOUNT;
    if(isVoid(param[0]) || param[0]->Type() != tvtString)
        return TJS_E_INVALIDPARAM;
    if(numparams == 2 && !isVoid(param[1]) &&
       param[1]->Type() != tvtString)
        return TJS_E_INVALIDPARAM;

    const ttstr requestedPath = param[0]->GetString();
    ttstr resolvedPath;
    try {
        resolvedPath = TVPGetPlacedPath(requestedPath);
    } catch(...) {
        resolvedPath.Clear();
    }
    if(resolvedPath.IsEmpty())
        resolvedPath = requestedPath;

    // Font.addFont is a script-facing wrapper around the same core
    // enumerator, but it is not guaranteed to be registered when an optional
    // plugin is loaded early during startup. Use the core path directly so
    // layerExVector remains order-independent; this also enables absolute
    // native paths on desktop targets.
    std::vector<ttstr> fontNames;
    bool loaded = false;
    try {
        loaded = TVPEnumFontsProc(resolvedPath, &fontNames) > 0;
    } catch(...) {
        // A malformed or missing font is a normal script-level failure.  The
        // upstream API reports false for it; do not let a TJS exception abort
        // the whole compatibility module.
        loaded = false;
    }

    // Renderer-specific font collections (notably Windows GDI+ and Blend2D)
    // are populated as well.  Core FreeType registration remains authoritative
    // if a backend does not implement addPrivateFont.
    tTJSVariant path(resolvedPath);
    tTJSVariant *fontParams[] = { &path };
    tTJSVariant gdiPlus;
    if(getGlobalObject(TJS_W("GdiPlus"), gdiPlus) &&
       gdiPlus.Type() == tvtObject) {
        tTJSVariant addPrivateFont;
        if(getMember(gdiPlus.AsObjectNoAddRef(), TJS_W("addPrivateFont"),
                     addPrivateFont) && addPrivateFont.Type() == tvtObject) {
            try {
                addPrivateFont.AsObjectClosureNoAddRef().FuncCall(
                    0, nullptr, nullptr, nullptr, 1, fontParams,
                    gdiPlus.AsObjectNoAddRef());
            } catch(...) {
                // Core registration is sufficient for the path-rendering
                // fallback; a backend-specific collection is an optimization.
            }
        }
    }

    ttstr actualName;
    if(loaded && !fontNames.empty())
        actualName = fontNames.front();
    else if(loaded)
        loaded = false;

    if(loaded) {
        ttstr alias;
        if(numparams >= 2 && !isVoid(param[1]) &&
           param[1]->Type() == tvtString) {
            alias = param[1]->GetString();
        }
        if(alias.IsEmpty())
            alias = defaultFontAlias(path);
        if(!alias.IsEmpty())
            loaded = TVPRegisterFontAlias(alias, actualName);
    }

    if(result)
        *result = loaded;
    return TJS_S_OK;
}

tjs_error unloadFontCompat(tTJSVariant *result, tjs_int numparams,
                           tTJSVariant **param, iTJSDispatch2 *) {
    if(result)
        *result = false;
    if(numparams != 1 || !param)
        return TJS_E_BADPARAMCOUNT;
    if(isVoid(param[0]) || param[0]->Type() != tvtString)
        return TJS_E_INVALIDPARAM;

    // Font streams are owned by the process-wide KiriKiri font table.  Keep
    // them alive for existing Font instances; this mirrors Aether's normal
    // addFont lifetime and avoids invalidating a scene that still references
    // the alias.
    if(result)
        *result = true;
    return TJS_S_OK;
}

tjs_error fontItalicGet(tTJSVariant *result, tjs_int, tTJSVariant **,
                        iTJSDispatch2 *object) {
    if(result)
        result->Clear();
    tjs_int style = 0;
    getIntegerMember(object, TJS_W("style"), style);
    if(result)
        *result = static_cast<tjs_real>((style & 2) != 0 ? 0.18 : 0.0);
    return TJS_S_OK;
}

tjs_error fontItalicSet(tTJSVariant *, tjs_int numparams,
                        tTJSVariant **param, iTJSDispatch2 *object) {
    if(numparams < 1 || !param || !param[0] || !isNumber(param[0]))
        return TJS_E_BADPARAMCOUNT;
    const tjs_real shear = static_cast<tjs_real>(*param[0]);
    if(!std::isfinite(static_cast<double>(shear)))
        return TJS_E_INVALIDPARAM;
    tjs_int style = 0;
    getIntegerMember(object, TJS_W("style"), style);
    style = (style & ~2) | (shear != 0 ? 2 : 0);
    const tTJSVariant value(style);
    return setMember(object, TJS_W("style"), value) ? TJS_S_OK : TJS_E_FAIL;
}

tjs_error fontSpacingGet(tTJSVariant *result, tjs_int, tTJSVariant **,
                         iTJSDispatch2 *object) {
    if(result)
        result->Clear();
    tTJSVariant value;
    if(getMember(object, TJS_W("__aether_krkrz_letterSpacing"), value)) {
        if(result)
            *result = value;
        return TJS_S_OK;
    }
    if(result)
        *result = static_cast<tjs_real>(1.0);
    return TJS_S_OK;
}

tjs_error fontSpacingSet(tTJSVariant *, tjs_int numparams,
                         tTJSVariant **param, iTJSDispatch2 *object) {
    if(numparams < 1 || !param || !param[0] || !isNumber(param[0]))
        return TJS_E_BADPARAMCOUNT;
    const tjs_real spacing = static_cast<tjs_real>(*param[0]);
    if(!std::isfinite(static_cast<double>(spacing)) || spacing <= 0)
        return TJS_E_INVALIDPARAM;
    return setMember(object, TJS_W("__aether_krkrz_letterSpacing"),
                      *param[0])
               ? TJS_S_OK
               : TJS_E_FAIL;
}

tjs_error fontLineSpacingGet(tTJSVariant *result, tjs_int, tTJSVariant **,
                              iTJSDispatch2 *object) {
    if(result)
        result->Clear();
    tTJSVariant value;
    if(getMember(object, TJS_W("aetherKrkrzLineSpacing"), value)) {
        if(result)
            *result = value;
        return TJS_S_OK;
    }
    if(result)
        *result = static_cast<tjs_real>(1.0);
    return TJS_S_OK;
}

tjs_error fontLineSpacingSet(tTJSVariant *, tjs_int numparams,
                             tTJSVariant **param, iTJSDispatch2 *object) {
    if(numparams < 1 || !param || !param[0] || !isNumber(param[0]))
        return TJS_E_BADPARAMCOUNT;
    const tjs_real spacing = static_cast<tjs_real>(*param[0]);
    // ThorVG treats lineSpacing as a non-negative scale.  Keep zero valid
    // (it intentionally overlaps lines) but reject NaN/negative values that
    // would otherwise make layout unbounded.
    if(!std::isfinite(static_cast<double>(spacing)) || spacing < 0)
        return TJS_E_INVALIDPARAM;
    return setMember(object, TJS_W("aetherKrkrzLineSpacing"), *param[0])
               ? TJS_S_OK
               : TJS_E_FAIL;
}

bool approximatelyDefaultLayout(const tTJSVariant &font) {
    return std::abs(getLetterSpacing(font) - static_cast<tjs_real>(1.0)) <
               static_cast<tjs_real>(1e-6) &&
           std::abs(getLineSpacingScale(font) - static_cast<tjs_real>(1.0)) <
               static_cast<tjs_real>(1e-6);
}

tjs_error measureStringCompat(tTJSVariant *result, tjs_int numparams,
                              tTJSVariant **param, iTJSDispatch2 *object) {
    if(result)
        result->Clear();
    if(numparams < 2 || !param || !object || !param[0] || !param[1])
        return TJS_E_BADPARAMCOUNT;
    if(param[0]->Type() != tvtObject || param[1]->Type() != tvtString)
        return TJS_E_INVALIDPARAM;

    const tTJSVariant font = *param[0];
    const ttstr text = param[1]->GetString();
    tTJSVariant forwardedResult;
    tTJSVariant &callResult = result ? *result : forwardedResult;
    if(approximatelyDefaultLayout(font) || text.IsEmpty()) {
        tTJSVariant *forwarded[] = { const_cast<tTJSVariant *>(&font),
                                     param[1] };
        return callDispatch(canonicalMeasureString, object, callResult, 2,
                            forwarded)
                   ? TJS_S_OK
                   : TJS_E_FAIL;
    }

    RectInfo measured;
    if(!measureTextWithSpacing(object, font, text, getLetterSpacing(font),
                               getLineSpacingScale(font), measured)) {
        // A backend may not expose the canonical measure method on a custom
        // Layer object.  Preserve the original error/fallback in that case.
        tTJSVariant *forwarded[] = { const_cast<tTJSVariant *>(&font),
                                     param[1] };
        return callDispatch(canonicalMeasureString, object, callResult, 2,
                            forwarded)
                   ? TJS_S_OK
                   : TJS_E_FAIL;
    }
    if(result)
        *result = makeRect(measured);
    return TJS_S_OK;
}

tjs_error drawStringCompat(tTJSVariant *result, tjs_int numparams,
                           tTJSVariant **param, iTJSDispatch2 *object) {
    if(result)
        result->Clear();
    if(numparams < 5 || !param || !object)
        return TJS_E_BADPARAMCOUNT;
    for(tjs_int i = 0; i < 5; ++i) {
        if(!param[i])
            return TJS_E_INVALIDPARAM;
    }
    if(param[0]->Type() != tvtObject || param[1]->Type() != tvtObject ||
       (param[2]->Type() != tvtInteger && param[2]->Type() != tvtReal) ||
       (param[3]->Type() != tvtInteger && param[3]->Type() != tvtReal) ||
       param[4]->Type() != tvtString)
        return TJS_E_INVALIDPARAM;

    const tTJSVariant font = *param[0];
    const ttstr text = param[4]->GetString();
    tTJSVariant forwardedResult;
    tTJSVariant &callResult = result ? *result : forwardedResult;
    if(approximatelyDefaultLayout(font) || text.IsEmpty()) {
        return callDispatch(canonicalDrawString, object, callResult, numparams,
                            param)
                   ? TJS_S_OK
                   : TJS_E_FAIL;
    }

    const tTJSVariant appearance = *param[1];
    const tjs_real x = static_cast<tjs_real>(*param[2]);
    const tjs_real y = static_cast<tjs_real>(*param[3]);
    RectInfo drawn;
    if(!drawTextWithSpacing(object, font, appearance, x, y, text,
                            getLetterSpacing(font),
                            getLineSpacingScale(font), drawn)) {
        return callDispatch(canonicalDrawString, object, callResult, numparams,
                            param)
                   ? TJS_S_OK
                   : TJS_E_FAIL;
    }
    if(result)
        *result = makeRect(drawn);
    return TJS_S_OK;
}

tjs_error drawStringAreaCompat(tTJSVariant *result, tjs_int numparams,
                               tTJSVariant **param, iTJSDispatch2 *object) {
    if(result)
        result->Clear();
    if(numparams < 10 || !param || !object)
        return TJS_E_BADPARAMCOUNT;
    for(tjs_int i = 0; i < 10; ++i) {
        if(!param[i])
            return TJS_E_INVALIDPARAM;
    }
    if(param[0]->Type() != tvtObject || param[1]->Type() != tvtObject ||
       (param[2]->Type() != tvtInteger && param[2]->Type() != tvtReal) ||
       (param[3]->Type() != tvtInteger && param[3]->Type() != tvtReal) ||
       (param[4]->Type() != tvtInteger && param[4]->Type() != tvtReal) ||
       (param[5]->Type() != tvtInteger && param[5]->Type() != tvtReal) ||
       (param[6]->Type() != tvtInteger && param[6]->Type() != tvtReal) ||
       (param[7]->Type() != tvtInteger && param[7]->Type() != tvtReal) ||
       (param[8]->Type() != tvtInteger && param[8]->Type() != tvtReal) ||
       param[9]->Type() != tvtString)
        return TJS_E_INVALIDPARAM;

    const tTJSVariant font = *param[0];
    const tTJSVariant appearance = *param[1];
    const tjs_real x = static_cast<tjs_real>(*param[2]);
    const tjs_real y = static_cast<tjs_real>(*param[3]);
    const tjs_real width = std::max<tjs_real>(0, static_cast<tjs_real>(*param[4]));
    const tjs_real height = std::max<tjs_real>(0, static_cast<tjs_real>(*param[5]));
    const tjs_real alignX = std::clamp(static_cast<tjs_real>(*param[6]),
                                       static_cast<tjs_real>(0),
                                       static_cast<tjs_real>(1));
    const tjs_real alignY = std::clamp(static_cast<tjs_real>(*param[7]),
                                       static_cast<tjs_real>(0),
                                       static_cast<tjs_real>(1));
    const tjs_int wrap = static_cast<tjs_int>(*param[8]);
    const ttstr text = param[9]->GetString();

    std::vector<ttstr> lines;
    for(const auto &line : splitLines(text)) {
        const auto wrapped = wrapLine(object, font, line, width, wrap);
        lines.insert(lines.end(), wrapped.begin(), wrapped.end());
    }
    if(lines.empty())
        lines.emplace_back(ttstr());

    tjs_real lineHeight = 0;
    for(const auto &line : lines) {
        RectInfo measured;
        if(measureText(object, font, line, measured) && measured.height > 0) {
            lineHeight = std::max(lineHeight, measured.height);
        }
    }
    if(lineHeight <= 0) {
        tjs_real fontSize = 0;
        if(!getRealMember(font.AsObjectNoAddRef(), TJS_W("fontSize"),
                          fontSize))
            getRealMember(font.AsObjectNoAddRef(), TJS_W("emSize"), fontSize);
        lineHeight = fontSize > 0 ? fontSize : 1;
    }

    lineHeight *= getLineSpacingScale(font);

    const tjs_real totalHeight = lineHeight * lines.size();
    const tjs_real originY = y + std::max<tjs_real>(0, height - totalHeight) * alignY;
    RectInfo total;
    for(size_t index = 0; index < lines.size(); ++index) {
        if(originY + static_cast<tjs_real>(index + 1) * lineHeight >
           y + height && wrap == 4)
            break;
        RectInfo measured;
        tjs_real lineWidth = 0;
        if(measureText(object, font, lines[index], measured))
            lineWidth = measured.width;
        const tjs_real originX = x + std::max<tjs_real>(0, width - lineWidth) * alignX;
        RectInfo drawn;
        if(drawText(object, font, appearance, originX,
                    originY + static_cast<tjs_real>(index) * lineHeight,
                    lines[index], drawn)) {
            unionRect(total, drawn);
        }
    }

    if(!total.valid) {
        total.left = x;
        total.top = y;
        total.width = 0;
        total.height = 0;
        total.valid = true;
    }
    if(result)
        *result = makeRect(total);
    return TJS_S_OK;
}

class LayerExVectorLayerCompat {};
class LayerExVectorFontCompat {};

} // namespace

#define NCB_MODULE_NAME TJS_W("layerExVector.dll")

NCB_PRE_REGIST_CALLBACK(loadLayerExDrawForVector);
NCB_POST_UNREGIST_CALLBACK(restoreCanonicalSurface);
NCB_ATTACH_FUNCTION(loadFont, GdiPlus, loadFontCompat);
NCB_ATTACH_FUNCTION(unloadFont, GdiPlus, unloadFontCompat);

NCB_ATTACH_CLASS(LayerExVectorLayerCompat, Layer) {
    RawCallback(TJS_W("drawString"), static_cast<Callback>(&drawStringCompat),
                0);
    RawCallback(TJS_W("measureString"),
                static_cast<Callback>(&measureStringCompat), 0);
    RawCallback(TJS_W("drawStringArea"),
                static_cast<Callback>(&drawStringAreaCompat), 0);
}

NCB_ATTACH_CLASS(LayerExVectorFontCompat, GdiPlus.Font) {
    RawCallback(TJS_W("italic"), static_cast<Callback>(&fontItalicGet),
                static_cast<Callback>(&fontItalicSet), 0);
    RawCallback(TJS_W("letterSpacing"),
                static_cast<Callback>(&fontSpacingGet),
                static_cast<Callback>(&fontSpacingSet), 0);
    RawCallback(TJS_W("lineSpacing"),
                static_cast<Callback>(&fontLineSpacingGet),
                static_cast<Callback>(&fontLineSpacingSet), 0);
}
