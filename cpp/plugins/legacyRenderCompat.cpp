#include "LayerIntf.h"
#include "ncbind.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

namespace {

    bool GetProperty(iTJSDispatch2 *object, const tjs_char *name,
                     tTJSVariant &value) {
        return object != nullptr &&
            TJS_SUCCEEDED(object->PropGet(TJS_IGNOREPROP, name, nullptr, &value,
                                          object));
    }

    tjs_int GetInteger(iTJSDispatch2 *object, const tjs_char *name,
                       tjs_int fallback) {
        tTJSVariant value;
        return GetProperty(object, name, value) && value.Type() != tvtVoid
            ? static_cast<tjs_int>(value.AsInteger())
            : fallback;
    }

    tjs_int GetObjectInteger(const tTJSVariant &object, const tjs_char *name,
                             tjs_int fallback) {
        return object.Type() == tvtObject
            ? GetInteger(object.AsObjectNoAddRef(), name, fallback)
            : fallback;
    }

    tTJSNI_BaseLayer *GetLayer(const tTJSVariant &value) {
        if(value.Type() != tvtObject)
            return nullptr;
        tTJSVariantClosure closure = value.AsObjectClosureNoAddRef();
        if(closure.Object == nullptr)
            return nullptr;
        tTJSNI_BaseLayer *layer = nullptr;
        if(TJS_FAILED(closure.Object->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&layer))))
            return nullptr;
        return layer;
    }

    tjs_error Invoke(iTJSDispatch2 *object, const tjs_char *name, tjs_int count,
                     tTJSVariant **params) {
        return object == nullptr ? TJS_E_INVALIDOBJECT
                                 : object->FuncCall(0, name, nullptr, nullptr,
                                                    count, params, object);
    }

    tjs_error TJS_INTF_METHOD NoOp(tTJSVariant *, tjs_int, tTJSVariant **,
                                   iTJSDispatch2 *) {
        return TJS_S_OK;
    }

    tjs_error BlurLayer(iTJSDispatch2 *options) {
        tTJSVariant value;
        if(!GetProperty(options, TJS_W("layer"), value))
            return TJS_S_OK;
        tTJSNI_BaseLayer *layer = GetLayer(value);
        if(layer == nullptr)
            return TJS_E_INVALIDOBJECT;
        const tjs_int level =
            std::max(1, GetInteger(options, TJS_W("level"), 1));
        layer->DoBoxBlur(level, level);
        return TJS_S_OK;
    }

    tjs_error TJS_INTF_METHOD BlurCompat(tTJSVariant *, tjs_int numparams,
                                         tTJSVariant **param, iTJSDispatch2 *) {
        return numparams < 1 ? TJS_E_BADPARAMCOUNT
                             : BlurLayer(param[0]->AsObjectNoAddRef());
    }

    tjs_error TJS_INTF_METHOD NoiseCompat(tTJSVariant *, tjs_int numparams,
                                          tTJSVariant **param,
                                          iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        iTJSDispatch2 *options = param[0]->AsObjectNoAddRef();
        tTJSVariant value;
        if(!GetProperty(options, TJS_W("layer"), value))
            return TJS_S_OK;
        tTJSNI_BaseLayer *layer = GetLayer(value);
        if(layer == nullptr)
            return TJS_E_INVALIDOBJECT;

        const bool monochrome = GetInteger(options, TJS_W("monocro"), 1) != 0;
        int lower = std::clamp(GetInteger(options, TJS_W("under"), 0), 0, 255);
        int upper =
            std::clamp(GetInteger(options, TJS_W("upper"), 255), 0, 255);
        if(lower > upper)
            std::swap(lower, upper);
        std::uint32_t seed = static_cast<std::uint32_t>(
            GetInteger(options, TJS_W("seed"), std::rand()));
        auto next = [&seed, lower, upper]() {
            seed = seed * 1566083941u + 1u;
            return lower +
                static_cast<int>((seed >> 24) %
                                 static_cast<unsigned>(upper - lower + 1));
        };

        auto *row = static_cast<std::uint8_t *>(
            layer->GetMainImagePixelBufferForWrite());
        const tjs_int pitch = layer->GetMainImagePixelBufferPitch();
        if(row == nullptr)
            return TJS_E_INVALIDOBJECT;
        for(tjs_uint y = 0; y < layer->GetHeight(); ++y) {
            auto *pixels = reinterpret_cast<std::uint32_t *>(row);
            for(tjs_uint x = 0; x < layer->GetWidth(); ++x) {
                const std::uint32_t alpha = pixels[x] & 0xff000000u;
                if(monochrome) {
                    const std::uint32_t n = static_cast<std::uint32_t>(next());
                    pixels[x] = alpha | n | (n << 8) | (n << 16);
                } else {
                    pixels[x] = alpha | static_cast<std::uint32_t>(next()) |
                        (static_cast<std::uint32_t>(next()) << 8) |
                        (static_cast<std::uint32_t>(next()) << 16);
                }
            }
            row += pitch;
        }
        layer->Update();
        return TJS_S_OK;
    }

    tjs_error TJS_INTF_METHOD ContrastCompat(tTJSVariant *, tjs_int numparams,
                                             tTJSVariant **param,
                                             iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        iTJSDispatch2 *options = param[0]->AsObjectNoAddRef();
        tTJSVariant value;
        if(!GetProperty(options, TJS_W("layer"), value))
            return TJS_S_OK;
        tTJSNI_BaseLayer *layer = GetLayer(value);
        if(layer == nullptr)
            return TJS_E_INVALIDOBJECT;
        const int level =
            std::clamp(GetInteger(options, TJS_W("level"), 0), -127, 127);
        if(level == 0)
            return TJS_S_OK;

        std::array<std::uint8_t, 256> table{};
        for(int i = 0; i < 256; ++i) {
            const int mapped = level > 0
                ? (i <= level ? 0
                              : (i >= 255 - level
                                     ? 255
                                     : (i - level) * 255 / (255 - 2 * level)))
                : i * (255 + 2 * level) / 255 - level;
            table[static_cast<std::size_t>(i)] =
                static_cast<std::uint8_t>(std::clamp(mapped, 0, 255));
        }

        auto *row = static_cast<std::uint8_t *>(
            layer->GetMainImagePixelBufferForWrite());
        const tjs_int pitch = layer->GetMainImagePixelBufferPitch();
        if(row == nullptr)
            return TJS_E_INVALIDOBJECT;
        for(tjs_uint y = 0; y < layer->GetHeight(); ++y) {
            for(tjs_uint x = 0; x < layer->GetWidth(); ++x) {
                auto *pixel = row + x * 4;
                pixel[0] = table[pixel[0]];
                pixel[1] = table[pixel[1]];
                pixel[2] = table[pixel[2]];
            }
            row += pitch;
        }
        layer->Update();
        return TJS_S_OK;
    }

    tjs_error TJS_INTF_METHOD StretchCompat(tTJSVariant *, tjs_int numparams,
                                            tTJSVariant **param,
                                            iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        iTJSDispatch2 *options = param[0]->AsObjectNoAddRef();
        tTJSVariant source, destination;
        if(!GetProperty(options, TJS_W("src"), source) ||
           !GetProperty(options, TJS_W("dest"), destination))
            return TJS_S_OK;

        const tjs_int sx = GetInteger(options, TJS_W("sleft"), 0);
        const tjs_int sy = GetInteger(options, TJS_W("stop"), 0);
        const tjs_int sw = GetInteger(
            options, TJS_W("swidth"),
            std::max(0, GetObjectInteger(source, TJS_W("width"), 0) - sx));
        const tjs_int sh = GetInteger(
            options, TJS_W("sheight"),
            std::max(0, GetObjectInteger(source, TJS_W("height"), 0) - sy));
        std::array<tTJSVariant, 10> values = {
            tTJSVariant(GetInteger(options, TJS_W("dleft"), 0)),
            tTJSVariant(GetInteger(options, TJS_W("dtop"), 0)),
            tTJSVariant(GetInteger(options, TJS_W("dwidth"), sw)),
            tTJSVariant(GetInteger(options, TJS_W("dheight"), sh)),
            source,
            tTJSVariant(sx),
            tTJSVariant(sy),
            tTJSVariant(sw),
            tTJSVariant(sh),
            tTJSVariant(1),
        };
        std::array<tTJSVariant *, 10> args{};
        for(std::size_t i = 0; i < values.size(); ++i)
            args[i] = &values[i];
        return Invoke(destination.AsObjectNoAddRef(), TJS_W("stretchCopy"),
                      static_cast<tjs_int>(args.size()), args.data());
    }

    tjs_error TJS_INTF_METHOD HazeCompat(tTJSVariant *, tjs_int numparams,
                                         tTJSVariant **param, iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        iTJSDispatch2 *options = param[0]->AsObjectNoAddRef();
        tTJSVariant source, destination;
        if(!GetProperty(options, TJS_W("src"), source) ||
           !GetProperty(options, TJS_W("dest"), destination))
            return TJS_S_OK;
        tTJSVariant *args[] = { &source };
        return Invoke(destination.AsObjectNoAddRef(), TJS_W("assignImages"), 1,
                      args);
    }

    void DrawLine(tTJSNI_BaseLayer *layer, int x0, int y0, int x1, int y1,
                  tjs_uint32 color) {
        const int dx = std::abs(x1 - x0);
        const int sx = x0 < x1 ? 1 : -1;
        const int dy = -std::abs(y1 - y0);
        const int sy = y0 < y1 ? 1 : -1;
        int error = dx + dy;
        for(;;) {
            if(x0 >= 0 && y0 >= 0 && x0 < static_cast<int>(layer->GetWidth()) &&
               y0 < static_cast<int>(layer->GetHeight()))
                layer->SetMainPixel(x0, y0, color);
            if(x0 == x1 && y0 == y1)
                break;
            const int twice = error * 2;
            if(twice >= dy) {
                error += dy;
                x0 += sx;
            }
            if(twice <= dx) {
                error += dx;
                y0 += sy;
            }
        }
    }

    tjs_error TJS_INTF_METHOD DrawLineCompat(tTJSVariant *, tjs_int numparams,
                                             tTJSVariant **param,
                                             iTJSDispatch2 *) {
        if(numparams < 6)
            return TJS_E_BADPARAMCOUNT;
        tTJSNI_BaseLayer *layer = GetLayer(*param[0]);
        if(layer == nullptr)
            return TJS_E_INVALIDOBJECT;
        DrawLine(layer, param[1]->AsInteger(), param[2]->AsInteger(),
                 param[3]->AsInteger(), param[4]->AsInteger(),
                 static_cast<tjs_uint32>(param[5]->AsInteger()));
        layer->Update();
        return TJS_S_OK;
    }

    tjs_error TJS_INTF_METHOD DrawTriangleCompat(tTJSVariant *,
                                                 tjs_int numparams,
                                                 tTJSVariant **param,
                                                 iTJSDispatch2 *) {
        if(numparams < 8)
            return TJS_E_BADPARAMCOUNT;
        tTJSNI_BaseLayer *layer = GetLayer(*param[0]);
        if(layer == nullptr)
            return TJS_E_INVALIDOBJECT;
        const tjs_uint32 color = static_cast<tjs_uint32>(param[7]->AsInteger());
        DrawLine(layer, param[1]->AsInteger(), param[2]->AsInteger(),
                 param[3]->AsInteger(), param[4]->AsInteger(), color);
        DrawLine(layer, param[3]->AsInteger(), param[4]->AsInteger(),
                 param[5]->AsInteger(), param[6]->AsInteger(), color);
        DrawLine(layer, param[5]->AsInteger(), param[6]->AsInteger(),
                 param[1]->AsInteger(), param[2]->AsInteger(), color);
        layer->Update();
        return TJS_S_OK;
    }

    tTJSVariant SlideSource;
    tTJSVariant SlideDestination;

    void CommitSlideFrame() {
        if(SlideSource.Type() != tvtObject ||
           SlideDestination.Type() != tvtObject)
            return;
        tTJSVariant *args[] = { &SlideSource };
        Invoke(SlideDestination.AsObjectNoAddRef(), TJS_W("assignImages"), 1,
               args);
    }

    tjs_error TJS_INTF_METHOD InitSlideOpenCompat(tTJSVariant *,
                                                  tjs_int numparams,
                                                  tTJSVariant **param,
                                                  iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        iTJSDispatch2 *state = param[0]->AsObjectNoAddRef();
        if(!GetProperty(state, TJS_W("src"), SlideSource) ||
           !GetProperty(state, TJS_W("dest"), SlideDestination))
            return TJS_E_INVALIDOBJECT;
        CommitSlideFrame();
        return TJS_S_OK;
    }

    tjs_error TJS_INTF_METHOD DrawSlideOpenCompat(tTJSVariant *, tjs_int,
                                                  tTJSVariant **,
                                                  iTJSDispatch2 *) {
        CommitSlideFrame();
        return TJS_S_OK;
    }

    tjs_error TJS_INTF_METHOD FinishSlideOpenCompat(tTJSVariant *, tjs_int,
                                                    tTJSVariant **,
                                                    iTJSDispatch2 *) {
        CommitSlideFrame();
        SlideSource.Clear();
        SlideDestination.Clear();
        return TJS_S_OK;
    }

    tTJSVariant FireSparkLayer;
    int FireSparkMaxGeneration = 32;
    bool FireSparkPaused = false;
    bool FireSparkEffectEnabled = true;

    void UpdateFireSparkOptions(iTJSDispatch2 *options) {
        if(options == nullptr)
            return;
        tTJSVariant layer;
        if(GetProperty(options, TJS_W("layer"), layer))
            FireSparkLayer = layer;
        FireSparkMaxGeneration = std::max(
            0, GetInteger(options, TJS_W("maxgen"), FireSparkMaxGeneration));
        FireSparkPaused =
            GetInteger(options, TJS_W("pause"), FireSparkPaused ? 1 : 0) != 0;
        FireSparkEffectEnabled =
            GetInteger(options, TJS_W("effect"),
                       FireSparkEffectEnabled ? 1 : 0) != 0;
    }

    tjs_error TJS_INTF_METHOD InitFireSparkCompat(tTJSVariant *,
                                                  tjs_int numparams,
                                                  tTJSVariant **param,
                                                  iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        FireSparkLayer = *param[0];
        FireSparkMaxGeneration = 32;
        FireSparkPaused = false;
        FireSparkEffectEnabled = true;
        if(numparams >= 2)
            UpdateFireSparkOptions(param[1]->AsObjectNoAddRef());
        return TJS_S_OK;
    }

    tjs_error TJS_INTF_METHOD ChangeFireSparkCompat(tTJSVariant *,
                                                    tjs_int numparams,
                                                    tTJSVariant **param,
                                                    iTJSDispatch2 *) {
        if(numparams > 0)
            UpdateFireSparkOptions(param[0]->AsObjectNoAddRef());
        return TJS_S_OK;
    }

    tjs_error TJS_INTF_METHOD DrawFireSparkCompat(tTJSVariant *,
                                                  tjs_int numparams,
                                                  tTJSVariant **param,
                                                  iTJSDispatch2 *) {
        tTJSNI_BaseLayer *layer = GetLayer(FireSparkLayer);
        if(layer == nullptr)
            return TJS_S_OK;
        if(FireSparkPaused)
            return TJS_S_OK;

        const tjs_int width = static_cast<tjs_int>(layer->GetWidth());
        const tjs_int height = static_cast<tjs_int>(layer->GetHeight());
        if(width <= 0 || height <= 0)
            return TJS_S_OK;
        layer->FillRect(tTVPRect(0, 0, width, height), 0x00000000u);
        if(FireSparkEffectEnabled) {
            iTJSDispatch2 *options =
                numparams > 0 ? param[0]->AsObjectNoAddRef() : nullptr;
            std::uint32_t state = static_cast<std::uint32_t>(
                GetInteger(options, TJS_W("tick"), 0));
            const int count = std::min(FireSparkMaxGeneration, 512);
            for(int i = 0; i < count; ++i) {
                state = state * 1664525u + 1013904223u;
                const int x = static_cast<int>((state >> 8) % width);
                state = state * 1664525u + 1013904223u;
                const int y = height - 1 -
                    static_cast<int>(((state >> 8) + i * 13u) % height);
                const std::uint32_t intensity = 128u + ((state >> 24) & 0x7fu);
                const std::uint32_t color = (intensity << 24) | 0x00ff9a28u;
                layer->SetMainPixel(x, y, color);
                if(y + 1 < height)
                    layer->SetMainPixel(x, y + 1, color);
            }
        }
        layer->Update();
        return TJS_S_OK;
    }

    tjs_error TJS_INTF_METHOD FinishFireSparkCompat(tTJSVariant *, tjs_int,
                                                    tTJSVariant **,
                                                    iTJSDispatch2 *) {
        FireSparkLayer.Clear();
        FireSparkMaxGeneration = 32;
        FireSparkPaused = false;
        FireSparkEffectEnabled = true;
        return TJS_S_OK;
    }

} // namespace

#define NCB_MODULE_NAME TJS_W("drawer.dll")
NCB_REGISTER_FUNCTION(drawLine, DrawLineCompat);
NCB_REGISTER_FUNCTION(drawAALine, DrawLineCompat);
NCB_REGISTER_FUNCTION(drawAATriangle, DrawTriangleCompat);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("filter.dll")
NCB_REGISTER_FUNCTION(Smudge, BlurCompat);
NCB_REGISTER_FUNCTION(Blur, BlurCompat);
NCB_REGISTER_FUNCTION(Lens, NoOp);
NCB_REGISTER_FUNCTION(InitLens, NoOp);
NCB_REGISTER_FUNCTION(ReleaseLens, NoOp);
NCB_REGISTER_FUNCTION(Noise, NoiseCompat);
NCB_REGISTER_FUNCTION(Contrast, ContrastCompat);
NCB_REGISTER_FUNCTION(initHaze, NoOp);
NCB_REGISTER_FUNCTION(doHaze, HazeCompat);
NCB_REGISTER_FUNCTION(endHaze, NoOp);
NCB_REGISTER_FUNCTION(Stretch, StretchCompat);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("slideopen.dll")
NCB_REGISTER_FUNCTION(initSlideOpen, InitSlideOpenCompat);
NCB_REGISTER_FUNCTION(drawSlideOpen, DrawSlideOpenCompat);
NCB_REGISTER_FUNCTION(finishSlideOpen, FinishSlideOpenCompat);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("firespark.dll")
NCB_REGISTER_FUNCTION(initFireSpark, InitFireSparkCompat);
NCB_REGISTER_FUNCTION(finishFireSpark, FinishFireSparkCompat);
NCB_REGISTER_FUNCTION(changeFireSpark, ChangeFireSparkCompat);
NCB_REGISTER_FUNCTION(drawFireSpark, DrawFireSparkCompat);
