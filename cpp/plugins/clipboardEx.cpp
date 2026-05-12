#include "ncbind.hpp"
#include "tp_stub.h"

#include "ClipboardIntf.h"

#define NCB_MODULE_NAME TJS_W("clipboardEx.dll")

namespace {

constexpr tjs_int kCbfText = 1;
constexpr tjs_int kCbfBitmap = 2;
constexpr tjs_int kCbfTJS = 3;

tTJSVariant g_clipboardTJS;
bool g_hasClipboardTJS = false;

void setGlobalInteger(const tjs_char *name, tjs_int value) {
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(!global)
        return;

    tTJSVariant var(value);
    global->PropSet(TJS_MEMBERENSURE, name, nullptr, &var, global);
    global->Release();
}

void deleteGlobalMember(const tjs_char *name) {
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(!global)
        return;

    global->DeleteMember(0, name, nullptr, global);
    global->Release();
}

bool variantHasMember(const tTJSVariant &variant, const tjs_char *name) {
    iTJSDispatch2 *object = variant.AsObjectNoAddRef();
    if(!object)
        return false;

    tTJSVariant value;
    return TJS_SUCCEEDED(
        object->PropGet(TJS_MEMBERMUSTEXIST, name, nullptr, &value, object));
}

void validateTJSClipboardData(const tTJSVariant &data) {
    if(!variantHasMember(data, TJS_W("type"))) {
        TVPThrowExceptionMessage(
            TJS_W("TJS expression to copy clipboard must have a field named 'type'."));
    }
    if(!variantHasMember(data, TJS_W("body"))) {
        TVPThrowExceptionMessage(
            TJS_W("TJS expression to copy clipboard must have a field named 'body'."));
    }
}

void RegisterClipboardExConstants() {
    setGlobalInteger(TJS_W("cbfBitmap"), kCbfBitmap);
    setGlobalInteger(TJS_W("cbfTJS"), kCbfTJS);
}

void UnregisterClipboardExConstants() {
    deleteGlobalMember(TJS_W("cbfBitmap"));
    deleteGlobalMember(TJS_W("cbfTJS"));
    g_clipboardTJS.Clear();
    g_hasClipboardTJS = false;
}

class ClipboardEx {
public:
    static tjs_error hasFormat(tTJSVariant *result, tjs_int numparams,
                               tTJSVariant **param, iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        bool has = false;
        switch(static_cast<tjs_int>(*param[0])) {
            case kCbfText:
                has = TVPClipboardHasFormat(cbfText);
                break;
            case kCbfBitmap:
                has = false;
                break;
            case kCbfTJS:
                has = g_hasClipboardTJS;
                break;
            default:
                has = false;
                break;
        }

        if(result)
            *result = has;
        return TJS_S_OK;
    }

    static tjs_error getTJS(tTJSVariant *result, tjs_int, tTJSVariant **,
                            iTJSDispatch2 *) {
        if(result) {
            if(g_hasClipboardTJS)
                *result = g_clipboardTJS;
            else
                result->Clear();
        }
        return TJS_S_OK;
    }

    static tjs_error setTJS(tTJSVariant *, tjs_int numparams,
                            tTJSVariant **param, iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        validateTJSClipboardData(*param[0]);
        g_clipboardTJS = *param[0];
        g_hasClipboardTJS = true;
        return TJS_S_OK;
    }

    static tjs_error setAsBitmap(tTJSVariant *, tjs_int numparams,
                                 tTJSVariant **, iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        return TJS_S_OK;
    }

    static tjs_error getAsBitmap(tTJSVariant *result, tjs_int numparams,
                                 tTJSVariant **, iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        if(result)
            *result = false;
        return TJS_S_OK;
    }

    static tjs_error setMultipleData(tTJSVariant *, tjs_int numparams,
                                     tTJSVariant **param, iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        iTJSDispatch2 *object = param[0]->AsObjectNoAddRef();
        if(!object)
            TVPThrowExceptionMessage(
                TJS_W("multiple clipboard data has supported format."));

        bool hasSupportedData = false;
        tTJSVariant value;
        if(TJS_SUCCEEDED(object->PropGet(TJS_MEMBERMUSTEXIST, TJS_W("text"),
                                         nullptr, &value, object))) {
            TVPClipboardSetText(value.AsStringNoAddRef());
            hasSupportedData = true;
        }

        value.Clear();
        if(TJS_SUCCEEDED(object->PropGet(TJS_MEMBERMUSTEXIST, TJS_W("tjs"),
                                         nullptr, &value, object))) {
            validateTJSClipboardData(value);
            g_clipboardTJS = value;
            g_hasClipboardTJS = true;
            hasSupportedData = true;
        }

        if(!hasSupportedData)
            TVPThrowExceptionMessage(
                TJS_W("multiple clipboard data has supported format."));

        return TJS_S_OK;
    }
};

class WindowClipboardEx {
public:
    bool getClipboardWatchEnabled() const { return false; }
    void setClipboardWatchEnabled(bool) {}
};

} // namespace

NCB_PRE_REGIST_CALLBACK(RegisterClipboardExConstants);
NCB_POST_UNREGIST_CALLBACK(UnregisterClipboardExConstants);

NCB_ATTACH_CLASS(ClipboardEx, Clipboard) {
    RawCallback(TJS_W("hasFormat"), &ClipboardEx::hasFormat, 0);
    RawCallback(TJS_W("asTJS"), &ClipboardEx::getTJS, &ClipboardEx::setTJS, 0);
    RawCallback(TJS_W("setAsBitmap"), &ClipboardEx::setAsBitmap, 0);
    RawCallback(TJS_W("getAsBitmap"), &ClipboardEx::getAsBitmap, 0);
    RawCallback(TJS_W("setMultipleData"), &ClipboardEx::setMultipleData, 0);
}

NCB_ATTACH_CLASS(WindowClipboardEx, Window) {
    NCB_PROPERTY(clipboardWatchEnabled, getClipboardWatchEnabled,
                 setClipboardWatchEnabled);
}
