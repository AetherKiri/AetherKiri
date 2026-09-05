/**
 * @file textrender.cpp
 * @brief TextRenderBase plugin for KiriKiri2.
 *
 * Ported from the drop-in replacement by Hikaru Terazono (3c1u),
 * licensed under Apache-2.0 / MIT.
 * https://github.com/3c1u/TextRender
 */

#include "ncbind.hpp"
#include "FreeTypeFontRasterizer.h"
#include "FontBaseline.h"
#include "LayerIntf.h"
#include "RectItf.h"
#include "TextTransform.h"
#include "textrender_timing.h"
#include "tvpfontstruc.h"
#include "WindowIntf.h"
#if defined(KRKR_ENABLE_GPU_BRIDGE)
#include "krkr_egl_context.h"
#endif

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#define NCB_MODULE_NAME TJS_W("textrender.dll")
#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

using tjs_ustring = std::basic_string<tjs_char>;
using RgbColor = uint32_t;
using TextColor = uint64_t;

#define setprop_t(d, p, ty) \
  { tTJSVariant v(ty(p)); d->PropSet(TJS_MEMBERENSURE, TJS_W(#p), nullptr, &v, d); }

#define setprop_opt_t(d, p, ty) \
  if (p != std::nullopt) { \
    tTJSVariant v(ty(*p)); d->PropSet(TJS_MEMBERENSURE, TJS_W(#p), nullptr, &v, d); \
  } else { \
    tTJSVariant v; d->PropSet(TJS_MEMBERENSURE, TJS_W(#p), nullptr, &v, d); \
  }

#define setprop(d, p) setprop_t(d, p, )

#define setprop_opt(d, p) setprop_opt_t(d, p, )

#define getprop_t(d, p, ty) \
  { tTJSVariant v; \
    if (TJS_SUCCEEDED(d->PropGet(0, TJS_W(#p), nullptr, &v, d)) && v.Type() != tvtVoid) { \
      p = ty((tjs_int)(v)); \
    } }

#define getprop_opt_t(d, p, ty) \
  { tTJSVariant v; \
    if (TJS_SUCCEEDED(d->PropGet(0, TJS_W(#p), nullptr, &v, d)) && v.Type() != tvtVoid) { \
      p = ty(v); \
    } else { p = std::nullopt; } }

#define getprop(d, p) getprop_t(d, p, )

static tjs_ustring variant_to_ustring(const tTJSVariant &v) {
  const tjs_char *s = v.GetString();
  return s ? tjs_ustring(s) : tjs_ustring();
}

static bool ustring_contains(const tjs_ustring &s, tjs_char ch) {
  return s.find(ch) != tjs_ustring::npos;
}

static bool textRenderVariantIsString(const tTJSVariant &value) {
  return value.Type() == tvtString || value.Type() == tvtOctet;
}

static bool textRenderVariantIsNumeric(const tTJSVariant &value) {
  const tTJSVariantType type = value.Type();
  return type != tvtVoid && type != tvtString && type != tvtObject;
}

static bool textRenderReadObjectProp(iTJSDispatch2 *object,
                                     const tjs_char *name,
                                     tTJSVariant &value) {
  return object &&
         TJS_SUCCEEDED(object->PropGet(0, name, nullptr, &value, object)) &&
         value.Type() != tvtVoid;
}

static bool textRenderReadIntPropAliases(
    iTJSDispatch2 *object, std::initializer_list<const tjs_char *> names,
    int &value) {
  for (const auto *name : names) {
    tTJSVariant prop;
    if (!textRenderReadObjectProp(object, name, prop) ||
        !textRenderVariantIsNumeric(prop)) {
      continue;
    }
    value = static_cast<int>((tjs_int)prop);
    return true;
  }
  return false;
}

static bool textRenderReadRealPropAliases(
    iTJSDispatch2 *object, std::initializer_list<const tjs_char *> names,
    double &value) {
  for (const auto *name : names) {
    tTJSVariant prop;
    if (!textRenderReadObjectProp(object, name, prop) ||
        !textRenderVariantIsNumeric(prop)) {
      continue;
    }
    value = prop.AsReal();
    return true;
  }
  return false;
}

static bool textRenderReadStringPropAliases(
    iTJSDispatch2 *object, std::initializer_list<const tjs_char *> names,
    tjs_ustring &value) {
  for (const auto *name : names) {
    tTJSVariant prop;
    if (!textRenderReadObjectProp(object, name, prop) ||
        !textRenderVariantIsString(prop)) {
      continue;
    }
    const tjs_char *s = prop.GetString();
    value = s ? tjs_ustring(s) : tjs_ustring();
    return true;
  }
  return false;
}

struct TextRenderState {
  bool bold = false;
  bool italic = false;
  tjs_ustring face{TJS_W("user")};
  int fontSize = 24;
  double fontScale = 1.0;
  RgbColor chColor = 0xffffff;
  int rubySize = 10;
  int rubyOffset = -2;
  bool shadow = true;
  RgbColor shadowColor = 0x000000;
  bool edge = false;
  RgbColor edgeColor = 0x0080ff;
  int edgeExtent = 2;
  int edgeEmphasis = 2048;
  int lineSpacing = 6;
  int pitch = 0;
  int lineSize = 0;
  int align = -1;
  int valign = -1;

  tTJSVariant serialize() const {
    auto dict = TJSCreateDictionaryObject();
    setprop(dict, bold);
    setprop(dict, italic);
    setprop(dict, fontSize);
    setprop(dict, fontScale);
    { tTJSVariant v(ttstr(face.c_str())); dict->PropSet(TJS_MEMBERENSURE, TJS_W("face"), nullptr, &v, dict); }
    setprop_t(dict, chColor, static_cast<tjs_int>);
    setprop(dict, rubySize);
    setprop(dict, rubyOffset);
    setprop(dict, shadow);
    setprop_t(dict, shadowColor, static_cast<tjs_int>);
    setprop(dict, edge);
    setprop_t(dict, edgeColor, static_cast<tjs_int>);
    setprop(dict, edgeExtent);
    setprop(dict, edgeEmphasis);
    setprop(dict, lineSpacing);
    setprop(dict, pitch);
    setprop(dict, lineSize);
    setprop(dict, align);
    setprop(dict, valign);
    auto res = tTJSVariant(dict, dict);
    dict->Release();
    return res;
  }

  void deserialize(tTJSVariant t) {
    auto dict = t.AsObjectNoAddRef();
    if (!dict) return;
    getprop(dict, bold);
    getprop(dict, italic);
    getprop(dict, fontSize);
    textRenderReadIntPropAliases(dict,
                                 {TJS_W("fontSize"), TJS_W("fontsize"),
                                  TJS_W("fontheight"), TJS_W("fontHeight")},
                                 fontSize);
    textRenderReadRealPropAliases(dict,
                                  {TJS_W("fontScale"), TJS_W("fontscale")},
                                  fontScale);
    textRenderReadStringPropAliases(dict,
                                    {TJS_W("face"), TJS_W("fontface"),
                                     TJS_W("fontFace")},
                                    face);
    getprop_t(dict, chColor, static_cast<RgbColor>);
    {
      int color = static_cast<int>(chColor);
      if (textRenderReadIntPropAliases(dict,
                                       {TJS_W("chColor"), TJS_W("color")},
                                       color)) {
        chColor = static_cast<RgbColor>(color);
      }
    }
    getprop(dict, rubySize);
    getprop(dict, rubyOffset);
    getprop(dict, shadow);
    getprop_t(dict, shadowColor, static_cast<RgbColor>);
    getprop(dict, edge);
    getprop_t(dict, edgeColor, static_cast<RgbColor>);
    getprop(dict, edgeExtent);
    getprop(dict, edgeEmphasis);
    getprop(dict, lineSpacing);
    textRenderReadIntPropAliases(dict,
                                 {TJS_W("lineSpacing"), TJS_W("linespacing")},
                                 lineSpacing);
    getprop(dict, pitch);
    getprop(dict, lineSize);
    textRenderReadIntPropAliases(dict,
                                 {TJS_W("lineSize"), TJS_W("linesize"),
                                  TJS_W("linestep"), TJS_W("lineStep")},
                                 lineSize);
    textRenderReadIntPropAliases(dict, {TJS_W("align"), TJS_W("halign")},
                                 align);
    textRenderReadIntPropAliases(dict, {TJS_W("valign")}, valign);
  }
};

struct TextRenderOptions {
  tjs_ustring following{
      u"%),:;]}\uff61\uff63\uff9e\uff9f\u3002\uff0c\u3001\uff0e\uff1a\uff1b\u309b\u309c\u30fd\u30fe\u309d\u309e\u3005\u2019\u201d\uff09\u3015\uff3d\uff5d\u3009\u300b\u300d\u300f\u3011\u00b0\u2032\u2033\u2103\uffe0\uff05\u2030\u3000!.?"
      u"\uff64\uff65\uff67\uff68\uff69\uff6a\uff6b\uff6c\uff6d\uff6e\uff6f\uff70\u30fb\uff1f\uff01\u30fc\u3041\u3043\u3045\u3047\u3049\u3063\u3083\u3085\u3087\u308e\u30a1\u30a3\u30a5\u30a7\u30a9\u30c3\u30e3\u30e5\u30e7\u30ee\u30f5\u30f6"};
  tjs_ustring leading{u"\\$([{\uff62\u2018\u201c\uff08\u3014\uff3b\uff5b\u3008\u300a\u300c\u300e\u3010\uffe5\uff04\uffe1"};
  tjs_ustring begin{u"\u300c\u300e\uff08\u2018\u201c\u3014\uff3b\uff5b\u3008\u300a"};
  tjs_ustring end{u"\u300d\u300f\uff09\u2019\u201d\u3015\uff3d\uff5d\u3009\u300b"};
  bool ignoreDelay = false;
  bool widthTimeScale = false;

  void deserialize(tTJSVariant t) {
    auto dict = t.AsObjectNoAddRef();
    if (!dict) return;
    { tTJSVariant v;
      if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("following"), nullptr, &v, dict)) && v.Type() != tvtVoid) {
        const tjs_char *s = v.GetString(); if (s) following = s;
      }
    }
    { tTJSVariant v;
      if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("leading"), nullptr, &v, dict)) && v.Type() != tvtVoid) {
        const tjs_char *s = v.GetString(); if (s) leading = s;
      }
    }
    { tTJSVariant v;
      if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("begin"), nullptr, &v, dict)) && v.Type() != tvtVoid) {
        const tjs_char *s = v.GetString(); if (s) begin = s;
      }
    }
    { tTJSVariant v;
      if (TJS_SUCCEEDED(dict->PropGet(0, TJS_W("end"), nullptr, &v, dict)) && v.Type() != tvtVoid) {
        const tjs_char *s = v.GetString(); if (s) end = s;
      }
    }
    {
      int value = ignoreDelay ? 1 : 0;
      if (textRenderReadIntPropAliases(
              dict, {TJS_W("ignore_delay"), TJS_W("ignoreDelay")}, value)) {
        ignoreDelay = value != 0;
      }
    }
    {
      int value = widthTimeScale ? 1 : 0;
      if (textRenderReadIntPropAliases(
              dict,
              {TJS_W("width_time_scale"), TJS_W("widthTimeScale")},
              value)) {
        widthTimeScale = value != 0;
      }
    }
  }
};

struct CharacterInfo {
  bool bold = false;
  bool italic = false;
  bool graph = false;
  bool vertical = false;
  tjs_ustring face{TJS_W("user")};
  int x = 0;
  int y = 0;
  int cw = 0;
  int size = 0;
  RgbColor color = 0xffffff;
  std::optional<RgbColor> edge = std::nullopt;
  int edgeExtent = 0;
  int edgeEmphasis = 0;
  std::optional<RgbColor> shadow = std::nullopt;
  tjs_ustring text;
  double delay = 0.0;

  tTJSVariant serialize() const {
    auto dict = TJSCreateDictionaryObject();
    setprop(dict, bold);
    setprop(dict, italic);
    setprop(dict, graph);
    setprop(dict, vertical);
    setprop(dict, x);
    setprop(dict, y);
    setprop(dict, cw);
    setprop(dict, size);
    { tTJSVariant v(ttstr(face.c_str())); dict->PropSet(TJS_MEMBERENSURE, TJS_W("face"), nullptr, &v, dict); }
    setprop_t(dict, color, static_cast<tjs_int>);
    setprop_opt_t(dict, edge, static_cast<tjs_int>);
    setprop(dict, edgeExtent);
    setprop(dict, edgeEmphasis);
    setprop_opt_t(dict, shadow, static_cast<tjs_int>);
    { tTJSVariant v(ttstr(text.c_str())); dict->PropSet(TJS_MEMBERENSURE, TJS_W("text"), nullptr, &v, dict); }
    {
      tTJSVariant v(krkr::textrender::CharacterDelayForScript(delay));
      dict->PropSet(TJS_MEMBERENSURE, TJS_W("delay"), nullptr, &v, dict);
    }
    auto res = tTJSVariant(dict, dict);
    dict->Release();
    return res;
  }
};

struct TextRenderKeyWait {
  int pos = 0;
  double time = 0.0;
};

#define property_accessor(name, type, storage) \
  type get_##name() const { return storage; } \
  void set_##name(type v) { storage = v; }

#define property_accessor_cast(name, type, cast, storage) \
  cast get_##name() const { return cast(storage); } \
  void set_##name(cast v) { storage = type(v); }

#define property_accessor_string(name, storage) \
  tTJSVariant get_##name() const { return tTJSVariant(ttstr(storage.c_str())); } \
  void set_##name(tTJSVariant v) { \
    const tjs_char *s = v.GetString(); \
    storage = s ? tjs_ustring(s) : tjs_ustring(); \
  }

#define property_delegate(name) NCB_PROPERTY(name, get_##name, set_##name);

static TextColor textRenderColorRaw(const tTJSVariant &value,
                                    TextColor fallback) {
  if (!textRenderVariantIsNumeric(value)) return fallback;
  return static_cast<TextColor>((tjs_int64)value);
}

static tjs_uint32 textRenderColor24(const tTJSVariant &value,
                                    tjs_uint32 fallback) {
  return static_cast<tjs_uint32>(textRenderColorRaw(value, fallback)) &
         0x00ffffff;
}

static tjs_uint32 textRenderColorBottom24(TextColor color) {
  return static_cast<tjs_uint32>(color) & 0x00ffffff;
}

static tjs_uint32 textRenderColorTop24(TextColor color) {
  return static_cast<tjs_uint32>((color >> 24) & 0x00ffffff);
}

static bool textRenderIsPackedGradient(TextColor color) {
  return (color & 0x8000000000000000ULL) != 0;
}

static bool textRenderTryObjectProp(iTJSDispatch2 *object,
                                    const tjs_char *name,
                                    tTJSVariant &value) {
  return object &&
         TJS_SUCCEEDED(object->PropGet(0, name, nullptr, &value, object)) &&
         value.Type() != tvtVoid;
}

static bool textRenderTryNumericObjectProp(iTJSDispatch2 *object,
                                           const tjs_char *name,
                                           TextColor &value) {
  tTJSVariant prop;
  if (!textRenderTryObjectProp(object, name, prop) ||
      !textRenderVariantIsNumeric(prop)) {
    return false;
  }
  value = textRenderColorRaw(prop, value);
  return true;
}

static bool textRenderTryIntObjectProp(iTJSDispatch2 *object,
                                       const tjs_char *name,
                                       tjs_int &value) {
  tTJSVariant prop;
  if (!textRenderTryObjectProp(object, name, prop) ||
      !textRenderVariantIsNumeric(prop)) {
    return false;
  }
  value = static_cast<tjs_int>(prop);
  return true;
}

static bool textRenderTryIntArg(tjs_int numparams, tTJSVariant **param,
                                tjs_int index, tjs_int &value) {
  if (index < 0 || index >= numparams || !param || !param[index] ||
      !textRenderVariantIsNumeric(*param[index])) {
    return false;
  }
  value = static_cast<tjs_int>(*param[index]);
  return true;
}

static tjs_int textRenderFindStringArg(tjs_int numparams, tTJSVariant **param) {
  for (tjs_int i = 0; i < numparams; ++i) {
    if (param && param[i] && textRenderVariantIsString(*param[i])) return i;
  }
  return -1;
}

static tTJSNI_BaseLayer *textRenderGetNativeLayer(iTJSDispatch2 *obj) {
  if (!obj) return nullptr;
  tTJSNI_BaseLayer *layer = nullptr;
  if (TJS_SUCCEEDED(obj->NativeInstanceSupport(
          TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
          reinterpret_cast<iTJSNativeInstance **>(&layer)))) {
    return layer;
  }
  return nullptr;
}

static tTJSNI_BaseLayer *textRenderFindLayerArg(tjs_int numparams,
                                                tTJSVariant **param,
                                                iTJSDispatch2 *objthis) {
  if (auto *layer = textRenderGetNativeLayer(objthis)) return layer;
  for (tjs_int i = 0; i < numparams; ++i) {
    if (!param || !param[i] || param[i]->Type() != tvtObject) continue;
    iTJSDispatch2 *obj = param[i]->AsObjectNoAddRef();
    if (auto *layer = textRenderGetNativeLayer(obj)) return layer;
  }
  return nullptr;
}

static void textRenderLogEdgeShadowArgs(tjs_int numparams, tTJSVariant **param,
                                        tjs_int textIndex) {
  static const bool enabled = std::getenv("AETHERKIRI_TEXT_TRACE") != nullptr;
  if (!enabled) return;
  static int logged = 0;
  if (logged >= 40) return;
  ++logged;

  std::string message = "textrender: EdgeShadowDrawText args #" +
                        std::to_string(logged) + " count=" +
                        std::to_string(numparams) + " textIndex=" +
                        std::to_string(textIndex);
  for (tjs_int i = 0; i < numparams; ++i) {
    message += " [";
    message += std::to_string(i);
    if (!param || !param[i]) {
      message += ":null]";
      continue;
    }
    message += ":t";
    message += std::to_string(static_cast<int>(param[i]->Type()));
    if (textRenderVariantIsNumeric(*param[i])) {
      message += "=";
      message += std::to_string(static_cast<long long>((tjs_int64)*param[i]));
    } else if (textRenderVariantIsString(*param[i])) {
      std::string s = ttstr(*param[i]).AsStdString();
      if (s.size() > 48) s.resize(48);
      message += "=\"";
      message += s;
      message += "\"";
    } else if (param[i]->Type() == tvtObject) {
      message += "=object";
    }
    message += "]";
  }
  spdlog::info("{}", message);
}

static void textRenderDrawTextWithColor(tTJSNI_BaseLayer *layer, tjs_int x,
                                        tjs_int y, const tTJSVariant &text,
                                        TextColor color,
                                        tjs_int opacity, tjs_int textHeight) {
  if (opacity <= 0) return;
  opacity = std::clamp<tjs_int>(opacity, 0, 255);
  if (!textRenderIsPackedGradient(color)) {
    layer->DrawText(x, y, text, textRenderColorBottom24(color), opacity, true,
                    0, 0, 0, 0, 0);
    return;
  }

  const tjs_uint32 textColor = textRenderColorTop24(color);
  layer->DrawTextVerticalGradient(x, y, text, textColor, textColor, opacity,
                                  true, std::clamp<tjs_int>(textHeight, 8, 128));
}

static tjs_error TJS_INTF_METHOD
EdgeShadowDrawTextCompat(tTJSVariant *result, tjs_int numparams,
                         tTJSVariant **param, iTJSDispatch2 *objthis) {
  if (!objthis || numparams < 3) return TJS_E_BADPARAMCOUNT;

  const tjs_int textIndex = textRenderFindStringArg(numparams, param);
  if (textIndex < 0) return TJS_E_BADPARAMCOUNT;
  textRenderLogEdgeShadowArgs(numparams, param, textIndex);

  tTJSNI_BaseLayer *layer =
      textRenderFindLayerArg(numparams, param, objthis);
  if (!layer) {
    if (result) *result = true;
    return TJS_S_OK;
  }

  tjs_int x = 0;
  tjs_int y = 0;
  bool haveX = false;
  bool haveY = false;
  for (tjs_int i = 0; i < textIndex; ++i) {
    tjs_int value = 0;
    if (!textRenderTryIntArg(numparams, param, i, value)) continue;
    if (!haveX) {
      x = value;
      haveX = true;
    } else if (!haveY) {
      y = value;
      haveY = true;
      break;
    }
  }

  const tjs_int colorIndex = textIndex + 1;
  const bool isNameLayerTextCall = numparams == 5 && textIndex == 2;
  TextColor color = 0xffffff;
  if (colorIndex < numparams && param[colorIndex] &&
      textRenderVariantIsNumeric(*param[colorIndex])) {
    color = textRenderColorRaw(*param[colorIndex], color);
  }
  tjs_int textOpacity = 255;
  textRenderTryIntArg(numparams, param, colorIndex + 1, textOpacity);
  textOpacity = std::clamp<tjs_int>(textOpacity, 0, 255);

  TextColor edgeColor = 0xffffff;
  tjs_int edgeWidth = 0;
  tjs_int textHeight = 24;
  if (numparams >= 15 && param[14] &&
      textRenderVariantIsNumeric(*param[14])) {
    edgeColor = textRenderColorRaw(*param[14], 0xffffff);
    tjs_int edgeX = 0;
    tjs_int edgeY = 0;
    if (textRenderTryIntArg(numparams, param, 11, edgeX) &&
        textRenderTryIntArg(numparams, param, 12, edgeY)) {
      edgeWidth = std::max(edgeX, edgeY);
    }
  } else {
    for (tjs_int i = numparams - 1; i > colorIndex; --i) {
      if (!param[i] || !textRenderVariantIsNumeric(*param[i])) continue;
      const TextColor candidate = textRenderColorRaw(*param[i], 0);
      if (candidate > 0xff) {
        edgeColor = candidate;
        break;
      }
    }
  }
  for (tjs_int i = 0; i < numparams; ++i) {
    if (!param || !param[i] || param[i]->Type() != tvtObject) continue;
    iTJSDispatch2 *object = param[i]->AsObjectNoAddRef();
    if (i > textIndex) {
      if (isNameLayerTextCall) {
        textRenderTryNumericObjectProp(object, TJS_W("color"), edgeColor);
        textRenderTryNumericObjectProp(object, TJS_W("nameLayerDefaultColor"),
                                       edgeColor);
      } else {
        textRenderTryNumericObjectProp(object, TJS_W("color"), color);
        textRenderTryNumericObjectProp(object, TJS_W("chColor"), color);
        textRenderTryNumericObjectProp(object, TJS_W("fontColor"), color);
        textRenderTryNumericObjectProp(object, TJS_W("textColor"), color);
      }
    }
    TextColor objectEdgeColor = edgeColor;
    if (textRenderTryNumericObjectProp(object, TJS_W("edgeColor"),
                                       objectEdgeColor)) {
      edgeColor = objectEdgeColor;
    }
    textRenderTryIntObjectProp(object, TJS_W("edgeExtent"), edgeWidth);
    textRenderTryIntObjectProp(object, TJS_W("edgeWidth"), edgeWidth);
    textRenderTryIntObjectProp(object, TJS_W("fontheight"), textHeight);
    textRenderTryIntObjectProp(object, TJS_W("fontHeight"), textHeight);
    textRenderTryIntObjectProp(object, TJS_W("fontSize"), textHeight);
    textRenderTryIntObjectProp(object, TJS_W("size"), textHeight);
  }
  edgeWidth = std::clamp<tjs_int>(edgeWidth, 0, 12);
  textHeight = std::clamp<tjs_int>(textHeight, 8, 128);

  // YuzuSoft's five-argument name renderer draws at y=0 in a dedicated
  // transparent layer. Source Han-style faces can have negative ink bounds
  // even though the line baseline is correct, so the layer clip used to cut
  // off the top strokes. Adjust only this compatibility draw; changing the
  // global baseline would move message text away from its KAG hit boxes.
  tTVPRect glyphBounds;
  bool haveGlyphBounds = false;
  const tjs_int requestedY = y;
  if (isNameLayerTextCall) {
    try {
      layer->GetFontGlyphDrawRect(ttstr(*param[textIndex]), glyphBounds);
      y = krkr::font::ClampTextOriginToClipTop(
          y, glyphBounds.top, edgeWidth, layer->GetClipTop());
      haveGlyphBounds = true;
    } catch (...) {
      // Drawing below still has the same failure handling as before. Bounds
      // measurement is only used to prevent clipping when it is available.
    }
  }

  if (std::getenv("AETHERKIRI_TEXT_TRACE") != nullptr) {
    const std::string text = ttstr(*param[textIndex]).AsStdString();
    spdlog::info(
        "textrender EdgeShadowDrawText resolved text=\"{}\" x={} y={} color=0x{:016x} "
        "edgeColor=0x{:016x} edgeWidth={} textOpacity={} textHeight={} packed={} "
        "requestedY={} glyphTop={} clipTop={}",
        text.substr(0, 96), x, y, static_cast<unsigned long long>(color),
        static_cast<unsigned long long>(edgeColor), edgeWidth, textOpacity,
        textHeight, textRenderIsPackedGradient(color), requestedY,
        haveGlyphBounds ? glyphBounds.top : 0, layer->GetClipTop());
  }

  try {
    for (tjs_int dy = -edgeWidth; dy <= edgeWidth; ++dy) {
      for (tjs_int dx = -edgeWidth; dx <= edgeWidth; ++dx) {
        if (dx == 0 && dy == 0) continue;
        if (dx * dx + dy * dy > edgeWidth * edgeWidth + 1) continue;
        layer->DrawText(x + dx, y + dy, *param[textIndex],
                        textRenderColorBottom24(edgeColor), 255, true, 0, 0,
                        0, 0, 0);
      }
    }
    textRenderDrawTextWithColor(layer, x, y, *param[textIndex], color,
                                textOpacity, textHeight);
  } catch (...) {
    if (result) *result = false;
    return TJS_S_OK;
  }

  if (result) *result = true;
  return TJS_S_OK;
}

class TextRenderBase {
public:
  TextRenderBase() : m_rasterizer(new FreeTypeFontRasterizer()) {}
  virtual ~TextRenderBase() {
    if (m_rasterizer) {
      m_rasterizer->Release();
      m_rasterizer = nullptr;
    }
  }

  bool render(tTJSString text, int autoIndent, int diff, int all, bool same,
              bool plain = false, iTJSDispatch2 *callbackThis = nullptr);
  static tjs_error TJS_INTF_METHOD renderCallback(
      tTJSVariant *result, tjs_int numparams, tTJSVariant **param,
      iTJSDispatch2 *objthis);
  void setRenderSize(int width, int height);
  void setDefault(tTJSVariant defaultSettings);
  void setOption(tTJSVariant options);
  tTJSVariant getCharacters(int start, int end);
  void clear();
  void done();
  void newline();

  tTJSVariant getKeyWait();
  double get_renderDelay() const;
  int calcShowCount(int elapsed);
  int get_renderCount() const;
  void set_renderCount(int value);
  void redraw();
  tTJSVariant renderText(tTJSString text);
  void resetStyle();
  void resetFont();
  bool renderOver() const { return m_overflow; }
  void setFontScale(double v) { set_fontScale(v); }
  tTJSVariant getRect();

  property_accessor(vertical, bool, m_vertical);
  property_accessor(bold, bool, m_state.bold);
  property_accessor(italic, bool, m_state.italic);
  property_accessor_string(face, m_state.face);
  property_accessor(fontSize, int, m_state.fontSize);
  double get_fontScale() const { return m_state.fontScale; }
  void set_fontScale(double v) {
    m_state.fontScale = v;
    m_fontDirty = true;
    m_layoutDirty = true;
  }
  property_accessor_cast(chColor, RgbColor, tjs_int, m_state.chColor);
  property_accessor(rubySize, int, m_state.rubySize);
  property_accessor(rubyOffset, int, m_state.rubyOffset);
  property_accessor(shadow, bool, m_state.shadow);
  property_accessor_cast(shadowColor, RgbColor, tjs_int, m_state.shadowColor);
  property_accessor(edge, bool, m_state.edge);
  property_accessor_cast(edgeColor, RgbColor, tjs_int, m_state.edgeColor);
  property_accessor(edgeExtent, int, m_state.edgeExtent);
  property_accessor(edgeEmphasis, int, m_state.edgeEmphasis);
  property_accessor(lineSpacing, int, m_state.lineSpacing);
  property_accessor(pitch, int, m_state.pitch);
  property_accessor(lineSize, int, m_state.lineSize);
  property_accessor(align, int, m_state.align);
  property_accessor(valign, int, m_state.valign);
  property_accessor(timeScale, double, m_timeScale);

  property_accessor(defaultBold, bool, m_default.bold);
  property_accessor(defaultItalic, bool, m_default.italic);
  property_accessor_string(defaultFace, m_default.face);
  property_accessor(defaultFontSize, int, m_default.fontSize);
  property_accessor(defaultFontScale, double, m_default.fontScale);
  property_accessor_cast(defaultChColor, RgbColor, tjs_int, m_default.chColor);
  property_accessor(defaultRubySize, int, m_default.rubySize);
  property_accessor(defaultRubyOffset, int, m_default.rubyOffset);
  property_accessor(defaultShadow, bool, m_default.shadow);
  property_accessor_cast(defaultShadowColor, RgbColor, tjs_int, m_default.shadowColor);
  property_accessor(defaultEdge, bool, m_default.edge);
  property_accessor_cast(defaultEdgeColor, RgbColor, tjs_int, m_default.edgeColor);
  property_accessor(defaultEdgeExtent, int, m_default.edgeExtent);
  property_accessor(defaultEdgeEmphasis, int, m_default.edgeEmphasis);
  property_accessor(defaultLineSpacing, int, m_default.lineSpacing);
  property_accessor(defaultPitch, int, m_default.pitch);
  property_accessor(defaultLineSize, int, m_default.lineSize);
  property_accessor(defaultAlign, int, m_default.align);
  property_accessor(defaultValign, int, m_default.valign);

  int get_renderLeft() { return getRenderedBounds().left; }
  int get_renderTop() { return getRenderedBounds().top; }
  int get_renderWidth() {
    const auto bounds = getRenderedBounds();
    return bounds.right - bounds.left;
  }
  int get_renderHeight() {
    const auto bounds = getRenderedBounds();
    return bounds.bottom - bounds.top;
  }
  int get_renderRight() { return getRenderedBounds().right; }
  int get_renderBottom() { return getRenderedBounds().bottom; }

private:
  struct RenderBounds {
    int left;
    int top;
    int right;
    int bottom;
  };

  FontRasterizer *m_rasterizer;
  int m_cachedAscentHeight = 0;
  bool m_fontDirty = true;
  bool m_layoutDirty = false;

  int m_boxWidth = 0;
  int m_boxHeight = 0;
  int m_x = 0;
  int m_y = 0;
  int m_indent = 0;
  int m_autoIndent = 0;
  int m_renderCountOverride = -1;
  bool m_overflow = false;
  bool m_isBeginningOfLine = true;
  bool m_vertical = false;
  double m_timeScale = 1.0;
  double m_baseCharacterDelay = 0.0;
  double m_characterDelay = 0.0;
  double m_currentDelay = 0.0;
  double m_renderDelay = 0.0;
  size_t m_timingSegmentStart = 0;
  double m_timingSegmentStartDelay = 0.0;
  size_t m_sameKeyWaitIndex = 0;
  bool m_sameTiming = false;

  TextRenderOptions m_options{};
  TextRenderState m_default{};
  TextRenderState m_state{};

  std::vector<CharacterInfo> m_characters{};
  std::vector<CharacterInfo> m_buffer{};
  std::vector<TextRenderKeyWait> m_keyWaits{};
  uint32_t m_mode = 0;

  void pushCharacter(tjs_char ch);
  void pushGraphicalCharacter(const tjs_ustring &graph);
  void performLinebreak();
  void flush(bool force = false);
  void applyFont();
  void applyAlignment();
  double getRequestedFontScale() const;
  double getEffectiveFontScale() const;
  int getEffectiveLineSpacing() const;
  int getEffectiveFontHeight() const;
  int getAscentHeight();
  RenderBounds getRenderedBounds();
  size_t getCharacterCount() const;
  void addDelay(double delay);
  void synchronizeTiming(double targetTime);
  std::optional<double> resolveDelayLabel(
      iTJSDispatch2 *callbackThis, const tjs_ustring &label) const;
};

enum TextRenderMode {
  kTextRenderModeLeading = 0,
  kTextRenderModeNormal,
  kTextRenderModeFollowing,
};

static bool readchar(tTJSString const &str, size_t &i, tjs_char &c) {
  auto const len = (size_t)str.GetLen();
  if (++i >= len) return false;
  c = str[i];
  return true;
}

static void read_integer(tTJSString const &str, size_t &i, int &value) {
  tjs_char ch;
  bool is_negative = false;
  while (true) {
    if (!readchar(str, i, ch)) {
      TVPThrowExceptionMessage(TJS_W("TextRenderBase::render() parse error: expected integer or ';', found EOF"));
    }
    if ('0' <= ch && ch <= '9') { value = value * 10 + (ch - '0'); continue; }
    if (ch == '-') { is_negative = !is_negative; continue; }
    if (ch == ';') { if (is_negative) value = -value; return; }
    TVPThrowExceptionMessage(TJS_W("TextRenderBase::render() parse error: unexpected char"));
  }
}

static int read_optional_integer(tTJSString const &str, size_t &i,
                                 int fallback) {
  tjs_ustring token;
  tjs_char ch;
  while (true) {
    if (!readchar(str, i, ch)) {
      TVPThrowExceptionMessage(
          TJS_W("TextRenderBase::render() parse error: expected integer or ';', found EOF"));
    }
    if (ch == ';') break;
    token += ch;
  }

  const auto parsed = krkr::textrender::ParseOptionalIntegerToken(
      std::basic_string_view<tjs_char>(token));
  if (!parsed.valid) {
    TVPThrowExceptionMessage(
        TJS_W("TextRenderBase::render() parse error: unexpected char"));
  }
  return parsed.hasValue ? parsed.value : fallback;
}

static tjs_ustring read_delay_label(tTJSString const &str, size_t &i,
                                    const tjs_char *command) {
  tjs_char ch;
  if (!readchar(str, i, ch) || ch != '$') {
    TVPThrowExceptionMessage(
        TJS_W("TextRenderBase::render() parse error: expected '$' after %1"),
        command);
  }

  tjs_ustring label;
  while (true) {
    if (!readchar(str, i, ch)) {
      TVPThrowExceptionMessage(
          TJS_W("TextRenderBase::render() parse error: unterminated label after %1"),
          command);
    }
    if (ch == ';') return label;
    label += ch;
  }
}

static bool read_flag(tTJSString const &str, size_t &i, bool default_value,
                      const tjs_char *command) {
  tjs_char ch;
  if (!readchar(str, i, ch)) {
    TVPThrowExceptionMessage(
        TJS_W("TextRenderBase::render() parse error: expected flag after %1"),
        command);
  }
  if (ch == '0') return false;
  if (ch == '1') return true;
  return default_value;
}

tjs_error TJS_INTF_METHOD TextRenderBase::renderCallback(
    tTJSVariant *result, tjs_int numparams, tTJSVariant **param,
    iTJSDispatch2 *objthis) {
  if (!objthis) return TJS_E_NATIVECLASSCRASH;
  if (numparams < 1 || !param || !param[0]) return TJS_E_BADPARAMCOUNT;

  auto *self = ncbInstanceAdaptor<TextRenderBase>::GetNativeInstance(objthis);
  if (!self) return TJS_E_NATIVECLASSCRASH;

  const auto intParam = [&](tjs_int index, int fallback) {
    return index < numparams && param[index] &&
                   param[index]->Type() != tvtVoid
               ? static_cast<int>(static_cast<tjs_int>(*param[index]))
               : fallback;
  };
  const int autoIndent = intParam(1, 0);
  const int diff = intParam(2, 0);
  const int all = intParam(3, 0);
  const bool same = intParam(4, 0) != 0;
  const bool plain = intParam(5, 0) != 0;

  const bool rendered =
      self->render(ttstr(*param[0]), autoIndent, diff, all, same, plain,
                   objthis);
  if (result) *result = rendered;
  return TJS_S_OK;
}

void TextRenderBase::applyFont() {
  if (!m_fontDirty) return;
  m_fontDirty = false;

  tTVPFont font;
  font.Height = static_cast<tjs_int>(getEffectiveFontHeight());
  font.Flags = static_cast<tjs_uint32>(
      (m_state.bold ? TVP_TF_BOLD : 0) | (m_state.italic ? TVP_TF_ITALIC : 0));
  font.Angle = 0;
  font.Face = ttstr(m_state.face.c_str());
  m_rasterizer->ApplyFont(font);
  m_cachedAscentHeight = m_rasterizer->GetAscentHeight();
}

double TextRenderBase::getEffectiveFontScale() const {
  double scale = getRequestedFontScale();

  auto *window = TVPMainWindow;
  if (!window) return scale;

  tjs_int srcW = window->GetWidth();
  tjs_int srcH = window->GetHeight();
  if (srcW <= 0 || srcH <= 0) {
    auto *drawDevice = window->GetDrawDevice();
    if (!drawDevice) return scale;
    drawDevice->GetSrcSize(srcW, srcH);
    if (srcW <= 0 || srcH <= 0) return scale;
  }

  uint32_t fbW = 0;
  uint32_t fbH = 0;
#if defined(KRKR_ENABLE_GPU_BRIDGE)
  auto &egl = krkr::GetEngineEGLContext();
  if (egl.HasIOSurface()) {
    fbW = egl.GetIOSurfaceWidth();
    fbH = egl.GetIOSurfaceHeight();
  } else if (egl.HasNativeWindow()) {
    fbW = egl.GetNativeWindowWidth();
    fbH = egl.GetNativeWindowHeight();
  } else if (egl.IsValid()) {
    fbW = egl.GetWidth();
    fbH = egl.GetHeight();
  }
#endif

  if (fbW == 0 || fbH == 0) return scale;

  const double autoScale =
      std::clamp(std::max(static_cast<double>(srcW) / static_cast<double>(fbW),
                          static_cast<double>(srcH) / static_cast<double>(fbH)),
                 1.0, 2.0);
  return scale * autoScale;
}

double TextRenderBase::getRequestedFontScale() const {
  const double scale = std::max(m_state.fontScale, 0.001);
  if (scale >= 1.0) return scale;

  const bool looksLikeMainMessageBox =
      m_boxWidth >= 900 && m_boxHeight >= 120 && m_state.fontSize >= 20;
  if (looksLikeMainMessageBox) return 1.0;

  return scale;
}

int TextRenderBase::getEffectiveLineSpacing() const {
  return static_cast<int>(std::lround(
      static_cast<double>(m_state.lineSpacing) *
      std::max(getEffectiveFontScale() /
                   std::max(getRequestedFontScale(), 0.001),
               1.0)));
}

int TextRenderBase::getEffectiveFontHeight() const {
  return std::max(1, static_cast<int>(
                         std::lround(m_state.fontSize * getEffectiveFontScale())));
}

int TextRenderBase::getAscentHeight() {
  applyFont();
  return m_cachedAscentHeight;
}

size_t TextRenderBase::getCharacterCount() const {
  return m_characters.size() + m_buffer.size();
}

void TextRenderBase::addDelay(double delay) {
  if (m_options.ignoreDelay) return;
  m_currentDelay += std::max(delay, 0.0);
}

void TextRenderBase::synchronizeTiming(double targetTime) {
  targetTime = std::max(targetTime, m_timingSegmentStartDelay);
  const double sourceDuration =
      std::max(m_currentDelay - m_timingSegmentStartDelay, 0.0);
  const double targetDuration = targetTime - m_timingSegmentStartDelay;
  const double scale = sourceDuration > 0.0
                           ? targetDuration / sourceDuration
                           : 1.0;

  size_t index = 0;
  const auto scaleCharacter = [&](CharacterInfo &character) {
    if (index++ < m_timingSegmentStart) return;
    character.delay =
        m_timingSegmentStartDelay +
        (character.delay - m_timingSegmentStartDelay) * scale;
  };
  for (auto &character : m_characters) scaleCharacter(character);
  for (auto &character : m_buffer) scaleCharacter(character);
  for (auto &wait : m_keyWaits) {
    if (wait.pos < static_cast<int>(m_timingSegmentStart)) continue;
    wait.time = m_timingSegmentStartDelay +
                (wait.time - m_timingSegmentStartDelay) * scale;
  }

  m_currentDelay = targetTime;
  m_renderDelay = std::max(m_renderDelay, targetTime);
  m_timingSegmentStart = getCharacterCount();
  m_timingSegmentStartDelay = targetTime;
}

std::optional<double> TextRenderBase::resolveDelayLabel(
    iTJSDispatch2 *callbackThis, const tjs_ustring &label) const {
  if (!callbackThis) return std::nullopt;

  tTJSVariant labelValue(ttstr(label.c_str()));
  tTJSVariant *params[] = {&labelValue};
  tTJSVariant resolved;
  const tjs_error status = callbackThis->FuncCall(
      0, TJS_W("onLabel"), nullptr, &resolved, 1, params, callbackThis);
  if (TJS_FAILED(status) ||
      (resolved.Type() != tvtInteger && resolved.Type() != tvtReal)) {
    return std::nullopt;
  }
  return static_cast<double>(resolved.AsReal());
}

bool TextRenderBase::render(tTJSString text, int autoIndent, int diff, int all,
                            bool same, bool plain,
                            iTJSDispatch2 *callbackThis) {
  const std::string originalText = text.AsStdString();
  const std::string translatedText =
      TVPTransformText("kirikiri", originalText);
  if (translatedText != originalText) text = tTJSString(translatedText);
  // A new message keeps the already laid-out glyphs, but their reveal time is
  // reset to zero.  This is how MsgwinRender appends a new line while only
  // animating the newly supplied text.  `same` is used by synchronized
  // secondary-language rendering and therefore keeps the existing timeline.
  flush();
  if (!same) {
    for (auto &character : m_characters) character.delay = 0.0;
    m_keyWaits.clear();
    m_renderDelay = 0.0;
    m_currentDelay = 0.0;
  } else {
    // `same` is used for concurrently rendered language tracks.  They share
    // the first track's key-wait anchors but each track starts at time zero;
    // renderDelay remains the maximum duration of all tracks.
    m_currentDelay = 0.0;
  }

  m_autoIndent = autoIndent;
  m_sameTiming = same;
  m_sameKeyWaitIndex = 0;
  m_baseCharacterDelay =
      m_options.ignoreDelay ? 0.0 : std::max(static_cast<double>(diff), 0.0);
  if (all > 0 && m_baseCharacterDelay == 0.0 && !m_options.ignoreDelay) {
    // Keep distinct timestamps so the requested total time can be
    // distributed even when the caller did not provide a character speed.
    m_baseCharacterDelay = 0.001;
  }
  m_characterDelay = m_baseCharacterDelay;
  m_timingSegmentStart = getCharacterCount();
  m_timingSegmentStartDelay = m_currentDelay;

  size_t len = (size_t)text.GetLen();
  const auto finishTiming = [&]() {
    if (all > 0 && !m_options.ignoreDelay) {
      synchronizeTiming(static_cast<double>(all));
    } else {
      m_renderDelay = std::max(m_renderDelay, m_currentDelay);
    }
  };

  if (plain) {
    krkr::textrender::ParsePlainText<tjs_char>(
        std::basic_string_view<tjs_char>(text.c_str(), len),
        [this](tjs_char ch) { pushCharacter(ch); }, [this]() {
          flush();
          performLinebreak();
        });
    finishTiming();
    return !m_overflow;
  }

  for (size_t i = 0; i < len; ++i) {
    tjs_char ch = text[i];
    switch (ch) {
    case '%': {
      if (!readchar(text, i, ch))
        TVPThrowExceptionMessage(TJS_W("TextRenderBase: parse error after %%"));

      switch (ch) {
      case 'f': {
        tjs_ustring fontname;
        while (true) {
          if (!readchar(text, i, ch))
            TVPThrowExceptionMessage(TJS_W("TextRenderBase: parse error in %%f"));
          if (ch == ';') break;
          fontname += ch;
        }
        if (!fontname.empty()) m_state.face = fontname;
        m_fontDirty = true;
        break;
      }
      case 'b': {
        m_state.bold = read_flag(text, i, m_default.bold, TJS_W("%b"));
        m_fontDirty = true;
        break;
      }
      case 'i': {
        m_state.italic = read_flag(text, i, m_default.italic, TJS_W("%i"));
        m_fontDirty = true;
        break;
      }
      case 's': {
        m_state.shadow = read_flag(text, i, m_default.shadow, TJS_W("%s"));
        break;
      }
      case 'e': {
        m_state.edge = read_flag(text, i, m_default.edge, TJS_W("%e"));
        break;
      }
      case 'd': {
        // An empty %d; resets the speed to the caller supplied base delay.
        // It is distinct from %d0;, which intentionally disables delay.
        const int value = read_optional_integer(text, i, 100);
        if (!m_options.ignoreDelay) {
          m_characterDelay =
              m_baseCharacterDelay * static_cast<double>(value) / 100.0;
        }
        break;
      }
      case 'a': {
        const int value = read_optional_integer(
            text, i, static_cast<int>(m_baseCharacterDelay));
        if (!m_options.ignoreDelay) {
          m_characterDelay = std::max(static_cast<double>(value), 0.0);
        }
        break;
      }
      case 'w': {
        if (i + 1 < len && text[i + 1] == '$') {
          const auto label = read_delay_label(text, i, TJS_W("%w"));
          if (const auto value = resolveDelayLabel(callbackThis, label)) {
            addDelay(*value);
          }
        } else {
          int value = 0;
          read_integer(text, i, value);
          addDelay(m_baseCharacterDelay * static_cast<double>(value) / 100.0);
        }
        break;
      }
      case 't': {
        if (i + 1 < len && text[i + 1] == '$') {
          const auto label = read_delay_label(text, i, TJS_W("%t"));
          if (const auto value = resolveDelayLabel(callbackThis, label)) {
            addDelay(*value);
          }
        } else {
          int value = 0;
          read_integer(text, i, value);
          addDelay(static_cast<double>(value));
        }
        break;
      }
      case 'D': {
        if (i + 1 < len && text[i + 1] == '$') {
          const auto label = read_delay_label(text, i, TJS_W("%D"));
          if (!m_options.ignoreDelay) {
            if (const auto value = resolveDelayLabel(callbackThis, label)) {
              synchronizeTiming(*value);
            }
          }
          break;
        }
        int value = 0;
        read_integer(text, i, value);
        if (!m_options.ignoreDelay) synchronizeTiming(value);
        break;
      }
      case 'n': {
        // KAGEX/krkrz-compatible logical line break: %n[lines];.
        int lines = 0;
        read_integer(text, i, lines);
        if (lines == 0) lines = 1;
        flush();
        performLinebreak();
        for (int line = 0; line < lines; ++line) performLinebreak();
        break;
      }
      case 'r': m_state = m_default; m_fontDirty = true; break;
      case '0': case '1': case '2': case '3': case '4':
      case '5': case '6': case '7': case '8': case '9': {
        int value = static_cast<int>(ch - '0');
        read_integer(text, i, value);
        m_state.fontSize = m_default.fontSize * value / 100;
        m_fontDirty = true;
        break;
      }
      default: break;
      }
      break;
    }
    case '\\': {
      if (!readchar(text, i, ch))
        TVPThrowExceptionMessage(TJS_W("TextRenderBase: parse error after backslash"));

      switch (ch) {
      case 'n': flush(); performLinebreak(); break;
      case 't': pushCharacter('\t'); break;
      case 'i': m_indent = m_x; break;
      case 'r': m_indent = 0; break;
      case 'w': pushCharacter(' '); break;
      case 'k': {
        const int position = static_cast<int>(getCharacterCount());
        if (m_sameTiming && m_sameKeyWaitIndex < m_keyWaits.size()) {
          synchronizeTiming(m_keyWaits[m_sameKeyWaitIndex++].time);
        } else if (!m_sameTiming) {
          m_keyWaits.push_back({position, m_currentDelay});
        }
        break;
      }
      case 'x': break;
      default: pushCharacter(ch); break;
      }
      break;
    }
    case '[': {
      while (true) {
        if (!readchar(text, i, ch))
          TVPThrowExceptionMessage(TJS_W("TextRenderBase: parse error in ruby []"));
        if (ch == ']') break;
      }
      break;
    }
    case '#': {
      RgbColor colour = 0x00;
      while (true) {
        if (!readchar(text, i, ch))
          TVPThrowExceptionMessage(TJS_W("TextRenderBase: parse error in color #"));
        if (ch == ';') break;
        RgbColor c = 0;
        if ('0' <= ch && ch <= '9') c = static_cast<RgbColor>(ch - '0');
        else if ('A' <= ch && ch <= 'F') c = 0x0a + static_cast<RgbColor>(ch - 'A');
        else if ('a' <= ch && ch <= 'f') c = 0x0a + static_cast<RgbColor>(ch - 'a');
        colour = (colour << 4) | c;
      }
      m_state.chColor = colour;
      break;
    }
    case '&': {
      tjs_ustring graph;
      while (true) {
        if (!readchar(text, i, ch))
          TVPThrowExceptionMessage(TJS_W("TextRenderBase: parse error in graphic &"));
        if (ch == ';') break;
        graph += ch;
      }
      pushGraphicalCharacter(graph);
      break;
    }
    case '$': {
      while (true) {
        if (!readchar(text, i, ch))
          TVPThrowExceptionMessage(TJS_W("TextRenderBase: parse error in eval $"));
        if (ch == ';') break;
      }
      break;
    }
    case '\r':
      // Treat CRLF as one logical line break while still supporting files
      // that contain old-style CR-only line endings.
      if (i + 1 < len && text[i + 1] == '\n') ++i;
      flush();
      performLinebreak();
      break;
    case '\n':
      flush();
      performLinebreak();
      break;
    default:
      pushCharacter(ch);
      break;
    }
  }

  finishTiming();

  return !m_overflow;
}

void TextRenderBase::performLinebreak() {
  m_x = m_indent;
  m_isBeginningOfLine = true;
  m_y += getAscentHeight() + getEffectiveLineSpacing();
}

void TextRenderBase::pushGraphicalCharacter(const tjs_ustring &) {
  // graphical character embedding is not yet implemented
}

void TextRenderBase::pushCharacter(tjs_char ch) {
  auto isLeadingChar = ustring_contains(m_options.leading, ch);
  auto isFollowingChar = ustring_contains(m_options.following, ch);
  auto isIndent = ustring_contains(m_options.begin, ch);
  auto isIndentDecr = ustring_contains(m_options.end, ch);

  uint32_t current;
  if (isLeadingChar) current = kTextRenderModeLeading;
  else if (isFollowingChar) current = kTextRenderModeFollowing;
  else current = kTextRenderModeNormal;

  if (m_mode == kTextRenderModeFollowing || m_mode != kTextRenderModeLeading) {
    flush();
  }

  applyFont();

  int advance_width = 0, advance_height = 0;
  m_rasterizer->GetTextExtent(ch, advance_width, advance_height);

  CharacterInfo info;
  info.bold = m_state.bold;
  info.italic = m_state.italic;
  info.graph = false;
  info.vertical = false;
  info.face = m_state.face;
  info.x = 0;
  info.y = 0;
  info.cw = advance_width;
  info.size = getEffectiveFontHeight();
  info.color = m_state.chColor;
  info.edge = m_state.edge ? std::make_optional(m_state.edgeColor) : std::nullopt;
  info.edgeExtent = m_state.edge ? m_state.edgeExtent : 0;
  info.edgeEmphasis = m_state.edge ? m_state.edgeEmphasis : 0;
  info.shadow = m_state.shadow ? std::make_optional(m_state.shadowColor) : std::nullopt;
  tjs_char tmp[] = { ch, 0 };
  info.text = tmp;
  info.delay = m_currentDelay;

  m_buffer.push_back(std::move(info));

  double characterDelay = m_characterDelay;
  if (m_options.widthTimeScale) {
    characterDelay *=
        static_cast<double>(std::max(advance_width, 0)) /
        static_cast<double>(std::max(getEffectiveFontHeight(), 1));
  }
  addDelay(characterDelay);

  if (m_autoIndent) {
    if (m_isBeginningOfLine && m_autoIndent < 0) {
      m_x -= advance_width;
    }
    if (isIndent) {
      m_indent = m_x + advance_width;
    }
    if (isIndentDecr && m_indent > 0) {
      flush();
      m_indent = 0;
    }
  }

  m_mode = current;
  m_isBeginningOfLine = false;
}

void TextRenderBase::flush(bool force) {
  if (m_buffer.empty()) return;

  auto x = m_x;
  for (auto &ch : m_buffer) {
    auto advance_width = ch.cw;
    auto new_x = advance_width + x + m_state.pitch;

    if (m_boxWidth < new_x) {
      if (force) {
        performLinebreak();
        x = m_x;
        new_x = advance_width + x + m_state.pitch;
      } else {
        performLinebreak();
        flush(true);
        return;
      }
    }
    ch.x = x;
    ch.y = m_y;
    x = new_x;
  }

  m_x = x;
  m_characters.insert(m_characters.end(), m_buffer.begin(), m_buffer.end());
  m_buffer.clear();
  m_layoutDirty = true;
}

void TextRenderBase::applyAlignment() {
  if (!m_layoutDirty || m_characters.empty()) return;
  m_layoutDirty = false;

  struct LineBounds {
    size_t first = 0;
    size_t last = 0;
    int y = 0;
    int left = 0;
    int right = 0;
    int top = 0;
    int bottom = 0;
  };

  std::vector<LineBounds> lines;
  lines.reserve(4);
  for (size_t i = 0; i < m_characters.size(); ++i) {
    const auto &ch = m_characters[i];
    const int left = ch.x;
    const int right = ch.x + std::max(ch.cw, 0);
    const int top = ch.y;
    const int bottom = ch.y + std::max(ch.size, getAscentHeight());
    if (lines.empty() || lines.back().y != ch.y) {
      lines.push_back({i, i, ch.y, left, right, top, bottom});
      continue;
    }
    auto &line = lines.back();
    line.last = i;
    line.left = std::min(line.left, left);
    line.right = std::max(line.right, right);
    line.top = std::min(line.top, top);
    line.bottom = std::max(line.bottom, bottom);
  }

  int blockTop = std::numeric_limits<int>::max();
  int blockBottom = std::numeric_limits<int>::min();
  for (auto &line : lines) {
    if (m_boxWidth > 0) {
      const int lineWidth = line.right - line.left;
      int offsetX = 0;
      if (m_state.align == 0) {
        offsetX = (m_boxWidth - lineWidth) / 2 - line.left;
      } else if (m_state.align > 0) {
        offsetX = (m_boxWidth - lineWidth) - line.left;
      }
      if (offsetX != 0) {
        for (size_t i = line.first; i <= line.last; ++i) {
          m_characters[i].x += offsetX;
        }
        line.left += offsetX;
        line.right += offsetX;
      }
    }
    blockTop = std::min(blockTop, line.top);
    blockBottom = std::max(blockBottom, line.bottom);
  }

  if (m_boxHeight > 0 && blockTop <= blockBottom) {
    const int blockHeight = blockBottom - blockTop;
    int offsetY = 0;
    if (m_state.valign == 0) {
      offsetY = (m_boxHeight - blockHeight) / 2 - blockTop;
    } else if (m_state.valign > 0) {
      offsetY = (m_boxHeight - blockHeight) - blockTop;
    }
    if (offsetY != 0) {
      for (auto &ch : m_characters) ch.y += offsetY;
    }
  }

  if (std::getenv("AETHERKIRI_TEXT_TRACE") != nullptr) {
    static int logged = 0;
    if (logged < 40) {
      ++logged;
      spdlog::info(
          "textrender layout #{} chars={} box={}x{} fontSize={} fontScale={} "
          "align={} valign={}",
          logged, m_characters.size(), m_boxWidth, m_boxHeight,
          m_state.fontSize, m_state.fontScale, m_state.align, m_state.valign);
    }
  }
}

void TextRenderBase::setRenderSize(int width, int height) {
  m_boxWidth = width;
  m_boxHeight = height;
  clear();
}

void TextRenderBase::setDefault(tTJSVariant defaultSettings) {
  m_default.deserialize(defaultSettings);
}

void TextRenderBase::setOption(tTJSVariant options) {
  m_options.deserialize(options);
}

tTJSVariant TextRenderBase::getCharacters(int start, int end) {
  applyAlignment();
  auto array = TJSCreateArrayObject();

  if ((end < start) || (start == 0 && end == 0)) {
    for (size_t i = 0, cnt = m_characters.size(); i < cnt; ++i) {
      auto ch = m_characters[i].serialize();
      array->PropSetByNum(TJS_MEMBERENSURE, (tjs_int)i, &ch, array);
    }
  }

  auto res = tTJSVariant(array, array);
  array->Release();
  return res;
}

void TextRenderBase::clear() {
  m_characters.clear();
  m_buffer.clear();
  m_state = m_default;
  m_overflow = false;
  m_x = 0;
  m_y = 0;
  m_indent = 0;
  m_isBeginningOfLine = true;
  m_fontDirty = true;
  m_layoutDirty = false;
  m_mode = kTextRenderModeLeading;
  m_keyWaits.clear();
  m_baseCharacterDelay = 0.0;
  m_characterDelay = 0.0;
  m_currentDelay = 0.0;
  m_renderDelay = 0.0;
  m_timingSegmentStart = 0;
  m_timingSegmentStartDelay = 0.0;
  m_sameKeyWaitIndex = 0;
  m_sameTiming = false;
}

void TextRenderBase::done() {
  flush();
  applyAlignment();
}

void TextRenderBase::newline() {
  flush();
  performLinebreak();
}

void TextRenderBase::resetStyle() {
  m_state = m_default;
  m_fontDirty = true;
  m_layoutDirty = true;
}

void TextRenderBase::resetFont() {
  m_state.bold = m_default.bold;
  m_state.italic = m_default.italic;
  m_state.face = m_default.face;
  m_state.fontSize = m_default.fontSize;
  m_state.fontScale = m_default.fontScale;
  m_fontDirty = true;
  m_layoutDirty = true;
}

tTJSVariant TextRenderBase::getKeyWait() {
  auto array = TJSCreateArrayObject();
  for (size_t i = 0; i < m_keyWaits.size(); ++i) {
    auto dict = TJSCreateDictionaryObject();
    {
      tTJSVariant value(m_keyWaits[i].pos);
      dict->PropSet(TJS_MEMBERENSURE, TJS_W("pos"), nullptr, &value, dict);
    }
    {
      tTJSVariant value(
          krkr::textrender::ScaleDelay(m_keyWaits[i].time, m_timeScale));
      dict->PropSet(TJS_MEMBERENSURE, TJS_W("time"), nullptr, &value, dict);
    }
    tTJSVariant value(dict, dict);
    dict->Release();
    array->PropSetByNum(TJS_MEMBERENSURE, static_cast<tjs_int>(i), &value,
                        array);
  }
  auto res = tTJSVariant(array, array);
  array->Release();
  return res;
}

double TextRenderBase::get_renderDelay() const {
  return krkr::textrender::ScaleDelay(m_renderDelay, m_timeScale);
}

int TextRenderBase::calcShowCount(int elapsed) {
  return krkr::textrender::CalcShowCount(
      m_characters.begin(), m_characters.end(),
      static_cast<double>(elapsed), m_timeScale,
      [](const CharacterInfo &character) { return character.delay; });
}

int TextRenderBase::get_renderCount() const {
  if (m_renderCountOverride >= 0) return m_renderCountOverride;
  return static_cast<int>(m_characters.size() + m_buffer.size());
}

void TextRenderBase::set_renderCount(int value) {
  m_renderCountOverride = std::max(value, 0);
}

void TextRenderBase::redraw() {
  done();
  m_renderCountOverride = -1;
}

tTJSVariant TextRenderBase::renderText(tTJSString text) {
  clear();
  render(text, 0, 0, 0, false);
  done();
  return getCharacters(0, 0);
}

TextRenderBase::RenderBounds TextRenderBase::getRenderedBounds() {
  applyAlignment();
  RenderBounds bounds{0, 0, 0, 0};
  bool hasCharacter = false;

  auto accumulate = [&](const CharacterInfo &character) {
    const int left = character.x;
    const int top = character.y;
    const int right = character.x + std::max(character.cw, 0);
    const int bottom = character.y + std::max(character.size, 0);
    if (!hasCharacter) {
      bounds = {left, top, right, bottom};
      hasCharacter = true;
      return;
    }
    bounds.left = std::min(bounds.left, left);
    bounds.top = std::min(bounds.top, top);
    bounds.right = std::max(bounds.right, right);
    bounds.bottom = std::max(bounds.bottom, bottom);
  };

  for (const auto &character : m_characters) accumulate(character);
  for (const auto &character : m_buffer) accumulate(character);

  if (!hasCharacter) {
    bounds.right = std::max(m_boxWidth, 0);
    bounds.bottom = std::max(m_boxHeight, 0);
  }

  return bounds;
}

tTJSVariant TextRenderBase::getRect() {
  const auto bounds = getRenderedBounds();
  iTJSDispatch2 *rect = TVPCreateRectObject(bounds.left, bounds.top,
                                            bounds.right, bounds.bottom);
  tTJSVariant result(rect, rect);
  rect->Release();
  return result;
}

NCB_REGISTER_CLASS(TextRenderBase) {
  Constructor();

  NCB_METHOD_RAW_CALLBACK(render, &TextRenderBase::renderCallback, 0);
  NCB_METHOD(setRenderSize);
  NCB_METHOD(setDefault);
  NCB_METHOD(setOption);
  NCB_METHOD(getCharacters);
  NCB_METHOD(clear);
  NCB_METHOD(done);
  NCB_METHOD(newline);

  NCB_METHOD(getKeyWait);
  NCB_METHOD(calcShowCount);
  NCB_METHOD(redraw);
  NCB_METHOD(renderText);
  NCB_METHOD(resetStyle);
  NCB_METHOD(resetFont);
  NCB_METHOD(setFontScale);
  NCB_METHOD(getRect);

  property_delegate(vertical);
  property_delegate(bold);
  property_delegate(italic);
  property_delegate(face);
  property_delegate(fontSize);
  property_delegate(chColor);
  property_delegate(rubySize);
  property_delegate(rubyOffset);
  property_delegate(shadow);
  property_delegate(shadowColor);
  property_delegate(edge);
  property_delegate(edgeColor);
  property_delegate(edgeExtent);
  property_delegate(edgeEmphasis);
  property_delegate(lineSpacing);
  property_delegate(pitch);
  property_delegate(lineSize);
  property_delegate(align);
  property_delegate(valign);
  property_delegate(timeScale);

  property_delegate(defaultBold);
  property_delegate(defaultItalic);
  property_delegate(defaultFace);
  property_delegate(defaultFontSize);
  property_delegate(defaultFontScale);
  property_delegate(defaultChColor);
  property_delegate(defaultRubySize);
  property_delegate(defaultRubyOffset);
  property_delegate(defaultShadow);
  property_delegate(defaultShadowColor);
  property_delegate(defaultEdge);
  property_delegate(defaultEdgeColor);
  property_delegate(defaultEdgeExtent);
  property_delegate(defaultEdgeEmphasis);
  property_delegate(defaultLineSpacing);
  property_delegate(defaultPitch);
  property_delegate(defaultLineSize);
  property_delegate(defaultAlign);
  property_delegate(defaultValign);

  NCB_PROPERTY(fontScale, get_fontScale, set_fontScale);
  NCB_PROPERTY(renderCount, get_renderCount, set_renderCount);
  // The original textrender.dll exposes renderOver as a read-only property.
  // Registering it as a method makes a property read return a truthy function
  // object, so callers can incorrectly retry overflowing text forever.
  NCB_PROPERTY_RO(renderOver, renderOver);
  NCB_PROPERTY_RO(renderDelay, get_renderDelay);
  NCB_PROPERTY_RO(renderLeft, get_renderLeft);
  NCB_PROPERTY_RO(renderTop, get_renderTop);
  NCB_PROPERTY_RO(renderWidth, get_renderWidth);
  NCB_PROPERTY_RO(renderHeight, get_renderHeight);
  NCB_PROPERTY_RO(renderRight, get_renderRight);
  NCB_PROPERTY_RO(renderBottom, get_renderBottom);
};

NCB_ATTACH_FUNCTION(EdgeShadowDrawText, Layer, EdgeShadowDrawTextCompat);
NCB_ATTACH_FUNCTION(EdgeShadowDrawTextKinsokuRect, Layer, EdgeShadowDrawTextCompat);
