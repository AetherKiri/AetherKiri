#include "TextStream.h"
#include "ncbind.hpp"

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

#define NCB_MODULE_NAME TJS_W("kirikiroid2.dll")

namespace {

tjs_error TJS_INTF_METHOD krkrStrOrd(tTJSVariant *result, tjs_int numparams,
                                     tTJSVariant **param, iTJSDispatch2 *) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    if(param[0]->Type() != tvtString) {
        if(result)
            *result = *param[0];
        return TJS_S_OK;
    }

    const tjs_char *text = param[0]->GetString();
    if(result)
        *result = text && *text ? static_cast<tjs_int>(*text) : 0;
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD krkrSetTextEncoding(tTJSVariant *, tjs_int numparams,
                                              tTJSVariant **param,
                                              iTJSDispatch2 *) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;
    if(param[0]->Type() == tvtString)
        TVPSetDefaultReadEncoding(param[0]->AsStringNoAddRef());
    return TJS_S_OK;
}

} // namespace

NCB_REGISTER_FUNCTION(_str_ord, krkrStrOrd);
NCB_ATTACH_FUNCTION(setTextEncoding, Storages, krkrSetTextEncoding);
