#pragma once

#include "tjs.h"

namespace aetherkiri::plugins::gles {

inline bool IsCallable(const tTJSVariant *value) {
    if(!value || value->Type() != tvtObject) return false;
    const auto closure = value->AsObjectClosureNoAddRef();
    return closure.Object &&
           closure.IsInstanceOf(0, nullptr, nullptr, TJS_W("Function"),
                                nullptr) == TJS_S_TRUE;
}

inline int CaptureCallbackIndex(tjs_int count, tTJSVariant **args) {
    if(!args) return -1;
    if(count > 0 && IsCallable(args[0])) return 0;
    if(count > 1 && IsCallable(args[1])) return 1;
    return -1;
}

// Native capture(targetLayer, callback, context, flags) invokes
// callback(width, height, context). The target is the render destination,
// not a callback argument. Also accept the callback-first convenience form.
inline tjs_error InvokeCaptureCallback(tjs_int width, tjs_int height,
                                       tjs_int count, tTJSVariant **args) {
    const int index = CaptureCallbackIndex(count, args);
    if(index < 0) return TJS_S_OK;
    const auto callback = args[index]->AsObjectClosureNoAddRef();
    tTJSVariant widthValue(width > 0 ? width : 1920);
    tTJSVariant heightValue(height > 0 ? height : 1080);
    tTJSVariant context;
    if(count > index + 1 && args[index + 1]) context = *args[index + 1];
    tTJSVariant *callbackArgs[] = {&widthValue, &heightValue, &context};
    return callback.FuncCall(0, nullptr, nullptr, nullptr, 3, callbackArgs,
                             nullptr);
}

} // namespace aetherkiri::plugins::gles
