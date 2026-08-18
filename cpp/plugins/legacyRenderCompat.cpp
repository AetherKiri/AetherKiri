#include "ncbind.hpp"

#include <algorithm>
#include <array>

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

tjs_error Invoke(iTJSDispatch2 *object, const tjs_char *name,
                 tjs_int count, tTJSVariant **params) {
    return object == nullptr
        ? TJS_E_INVALIDOBJECT
        : object->FuncCall(0, name, nullptr, nullptr, count, params, object);
}

tjs_error TJS_INTF_METHOD NoOp(tTJSVariant *, tjs_int,
                               tTJSVariant **, iTJSDispatch2 *) {
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD BlurCompat(tTJSVariant *, tjs_int numparams,
                                     tTJSVariant **param, iTJSDispatch2 *) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;
    iTJSDispatch2 *options = param[0]->AsObjectNoAddRef();
    tTJSVariant layer;
    if(!GetProperty(options, TJS_W("layer"), layer))
        return TJS_S_OK;
    iTJSDispatch2 *target = layer.AsObjectNoAddRef();
    const tjs_int level = std::max(1, GetInteger(options, TJS_W("level"), 1));
    tTJSVariant x(level), y(level);
    tTJSVariant *args[] = { &x, &y };
    return Invoke(target, TJS_W("doBoxBlur"), 2, args);
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

    std::array<tTJSVariant, 10> values = {
        tTJSVariant(GetInteger(options, TJS_W("dleft"), 0)),
        tTJSVariant(GetInteger(options, TJS_W("dtop"), 0)),
        tTJSVariant(GetInteger(options, TJS_W("dwidth"), 0)),
        tTJSVariant(GetInteger(options, TJS_W("dheight"), 0)),
        source,
        tTJSVariant(GetInteger(options, TJS_W("sleft"), 0)),
        tTJSVariant(GetInteger(options, TJS_W("stop"), 0)),
        tTJSVariant(GetInteger(options, TJS_W("swidth"), 0)),
        tTJSVariant(GetInteger(options, TJS_W("sheight"), 0)),
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

} // namespace

#define NCB_MODULE_NAME TJS_W("drawer.dll")
NCB_REGISTER_FUNCTION(drawLine, NoOp);
NCB_REGISTER_FUNCTION(drawAALine, NoOp);
NCB_REGISTER_FUNCTION(drawAATriangle, NoOp);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("filter.dll")
NCB_REGISTER_FUNCTION(Smudge, NoOp);
NCB_REGISTER_FUNCTION(Blur, BlurCompat);
NCB_REGISTER_FUNCTION(Lens, NoOp);
NCB_REGISTER_FUNCTION(InitLens, NoOp);
NCB_REGISTER_FUNCTION(ReleaseLens, NoOp);
NCB_REGISTER_FUNCTION(Noise, NoOp);
NCB_REGISTER_FUNCTION(Contrast, NoOp);
NCB_REGISTER_FUNCTION(initHaze, NoOp);
NCB_REGISTER_FUNCTION(doHaze, HazeCompat);
NCB_REGISTER_FUNCTION(endHaze, NoOp);
NCB_REGISTER_FUNCTION(Stretch, StretchCompat);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("slideopen.dll")
NCB_REGISTER_FUNCTION(initSlideOpen, NoOp);
NCB_REGISTER_FUNCTION(drawSlideOpen, NoOp);
NCB_REGISTER_FUNCTION(finishSlideOpen, NoOp);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("firespark.dll")
NCB_REGISTER_FUNCTION(initFireSpark, NoOp);
NCB_REGISTER_FUNCTION(finishFireSpark, NoOp);
NCB_REGISTER_FUNCTION(changeFireSpark, NoOp);
NCB_REGISTER_FUNCTION(drawFireSpark, NoOp);
