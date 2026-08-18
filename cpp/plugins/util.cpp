#include "ncbind.hpp"

#include "FontImpl.h"
#include "Platform.h"
#include "StorageIntf.h"

#include <memory>
#include <vector>

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

#define NCB_MODULE_NAME TJS_W("util.dll")

namespace {

ttstr LocalName(const tTJSVariant &value) {
    ttstr path(value);
    TVPGetLocalName(path);
    return path;
}

tjs_error TJS_INTF_METHOD EnumFont(tTJSVariant *, tjs_int numparams,
                                   tTJSVariant **param, iTJSDispatch2 *) {
    if(numparams < 2)
        return TJS_E_BADPARAMCOUNT;

    iTJSDispatch2 *array = param[1]->AsObjectNoAddRef();
    if(array == nullptr)
        return TJS_E_INVALIDOBJECT;

    tjs_int count = 0;
    if(TJS_FAILED(array->GetCount(&count, nullptr, nullptr, array)))
        return TJS_E_INVALIDOBJECT;

    std::vector<ttstr> fonts;
    TVPGetAllFontList(fonts);
    for(const auto &font : fonts) {
        tTJSVariant value(font);
        const tjs_error result = array->PropSetByNum(
            TJS_MEMBERENSURE, count++, &value, array);
        if(TJS_FAILED(result))
            return result;
    }
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD DeleteFileCompat(tTJSVariant *, tjs_int numparams,
                                           tTJSVariant **param,
                                           iTJSDispatch2 *) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;
    try {
        const ttstr path = LocalName(*param[0]);
        TVPDeleteFile(path.AsNarrowStdString());
    } catch(...) {
        // The original util.dll ignores the Win32 DeleteFile return value.
    }
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD MoveFileCompat(tTJSVariant *, tjs_int numparams,
                                         tTJSVariant **param,
                                         iTJSDispatch2 *) {
    if(numparams < 2)
        return TJS_E_BADPARAMCOUNT;
    try {
        const ttstr source = LocalName(*param[0]);
        const ttstr destination = LocalName(*param[1]);
        TVPRenameFile(source.AsNarrowStdString(),
                      destination.AsNarrowStdString());
    } catch(...) {
        // The original util.dll ignores the Win32 MoveFile return value.
    }
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD GetFileSizeCompat(tTJSVariant *result,
                                            tjs_int numparams,
                                            tTJSVariant **param,
                                            iTJSDispatch2 *) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    tjs_int64 size = -1;
    try {
        const ttstr path = LocalName(*param[0]);
        std::unique_ptr<tTJSBinaryStream> stream(
            TVPCreateStream(path, TJS_BS_READ));
        if(stream)
            size = static_cast<tjs_int64>(stream->GetSize());
    } catch(...) {
        size = -1;
    }
    if(result)
        *result = size;
    return TJS_S_OK;
}

tjs_error TJS_INTF_METHOD EnabledIme(tTJSVariant *, tjs_int numparams,
                                     tTJSVariant **, iTJSDispatch2 *) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;
    // IMM32 state is Windows-window specific. Other hosts route text input
    // through their native shell and require no per-window toggle here.
    return TJS_S_OK;
}

} // namespace

NCB_REGISTER_FUNCTION(enumFont, EnumFont);
NCB_REGISTER_FUNCTION(DeleteFile, DeleteFileCompat);
NCB_REGISTER_FUNCTION(MoveFile, MoveFileCompat);
NCB_REGISTER_FUNCTION(GetFileSize, GetFileSizeCompat);
NCB_REGISTER_FUNCTION(enabledIME, EnabledIme);
