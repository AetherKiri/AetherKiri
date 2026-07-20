#include "tjs.h"

#include <string>

tTJS *TVPGetScriptEngine() { return nullptr; }

void TVPExecuteExpression(const ttstr &, const ttstr &, tjs_int,
                          iTJSDispatch2 *, tTJSVariant *) {}

ttstr TVPExtractStorageName(const ttstr &name) { return name; }

ttstr TVPGetMessageByLocale(const std::string &key) {
    return ttstr(key.c_str());
}

iTJSTextReadStream *TVPCreateTextStreamForRead(const ttstr &, const ttstr &) {
    return nullptr;
}
