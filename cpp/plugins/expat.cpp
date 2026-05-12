#include "PluginStub.h"
#include "ncbind.hpp"

#include <expat.h>

#include <memory>
#include <string>
#include <vector>

#define NCB_MODULE_NAME TJS_W("expat.dll")

namespace {

ttstr utf8ToTtstr(const char *bytes, tjs_uint length) {
    tjs_int wideLength =
        TVPUtf8ToWideCharString(bytes, length, static_cast<tjs_char *>(nullptr));
    if(wideLength < 0)
        TVPThrowExceptionMessage(TJS_W("invalid UTF-8 from XML parser"));

    std::vector<tjs_char> wide(static_cast<size_t>(wideLength) + 1, 0);
    if(wideLength > 0)
        TVPUtf8ToWideCharString(bytes, length, wide.data());
    return ttstr(wide.data());
}

ttstr utf8ToTtstr(const char *text) {
    return text ? utf8ToTtstr(text, static_cast<tjs_uint>(strlen(text)))
                : ttstr(TJS_W(""));
}

std::string ttstrToUtf8(const ttstr &text) {
    const tjs_int length = TVPWideCharToUtf8String(text.c_str(), nullptr);
    std::string out(static_cast<size_t>(length), '\0');
    if(length > 0)
        TVPWideCharToUtf8String(text.c_str(), out.data());
    return out;
}

std::string readStorageBytes(const ttstr &filename) {
    std::unique_ptr<tTJSBinaryStream> stream(
        TVPCreateStream(filename, TJS_BS_READ));
    if(!stream)
        TVPThrowExceptionMessage((ttstr(TJS_W("cannot open : ")) + filename)
                                     .c_str());

    const tjs_uint64 size64 = stream->GetSize();
    if(size64 > static_cast<tjs_uint64>(static_cast<size_t>(-1)))
        TVPThrowExceptionMessage(TJS_W("storage too large"));

    std::string bytes(static_cast<size_t>(size64), '\0');
    if(!bytes.empty())
        stream->ReadBuffer(bytes.data(), static_cast<tjs_uint>(bytes.size()));
    return bytes;
}

void addMember(iTJSDispatch2 *dispatch, const tjs_char *name,
               iTJSDispatch2 *member) {
    tTJSVariant value(member);
    member->Release();
    dispatch->PropSet(TJS_MEMBERENSURE, name, nullptr, &value, dispatch);
}

void delMember(iTJSDispatch2 *dispatch, const tjs_char *name) {
    dispatch->DeleteMember(0, name, nullptr, dispatch);
}

bool isValidMember(iTJSDispatch2 *dispatch, const tjs_char *name) {
    return dispatch &&
           dispatch->IsValid(TJS_IGNOREPROP, name, nullptr, dispatch) ==
               TJS_S_TRUE;
}

iTJSDispatch2 *getMember(iTJSDispatch2 *dispatch, const tjs_char *name) {
    tTJSVariant value;
    if(TJS_FAILED(
           dispatch->PropGet(TJS_IGNOREPROP, name, nullptr, &value, dispatch))) {
        TVPThrowExceptionMessage((ttstr(TJS_W("can't get member:")) + name)
                                     .c_str());
    }
    return value.AsObject();
}

void callMethod(iTJSDispatch2 *target, const tjs_char *name, tjs_int numparams,
                tTJSVariant **params) {
    if(!isValidMember(target, name))
        return;

    iTJSDispatch2 *method = getMember(target, name);
    method->FuncCall(0, nullptr, nullptr, nullptr, numparams, params, target);
    method->Release();
}

struct ParserContext {
    iTJSDispatch2 *target = nullptr;
};

void XMLCALL startElement(void *userData, const XML_Char *name,
                          const XML_Char **atts) {
    auto *context = static_cast<ParserContext *>(userData);
    if(!isValidMember(context->target, TJS_W("startElement")))
        return;

    tTJSVariant elementName(utf8ToTtstr(name));
    iTJSDispatch2 *dict = TJSCreateDictionaryObject();
    for(const XML_Char **p = atts; p && *p; p += 2) {
        tTJSVariant value(utf8ToTtstr(p[1]));
        dict->PropSet(TJS_MEMBERENSURE, utf8ToTtstr(p[0]).c_str(), nullptr,
                      &value, dict);
    }

    tTJSVariant attributes(dict);
    dict->Release();
    tTJSVariant *params[] = { &elementName, &attributes };
    callMethod(context->target, TJS_W("startElement"), 2, params);
}

void XMLCALL endElement(void *userData, const XML_Char *name) {
    auto *context = static_cast<ParserContext *>(userData);
    tTJSVariant elementName(utf8ToTtstr(name));
    tTJSVariant *params[] = { &elementName };
    callMethod(context->target, TJS_W("endElement"), 1, params);
}

void XMLCALL characterData(void *userData, const XML_Char *data, int len) {
    auto *context = static_cast<ParserContext *>(userData);
    tTJSVariant text(utf8ToTtstr(data, static_cast<tjs_uint>(len)));
    tTJSVariant *params[] = { &text };
    callMethod(context->target, TJS_W("characterData"), 1, params);
}

void XMLCALL processingInstruction(void *userData, const XML_Char *target,
                                   const XML_Char *data) {
    auto *context = static_cast<ParserContext *>(userData);
    tTJSVariant targetValue(utf8ToTtstr(target));
    tTJSVariant dataValue(utf8ToTtstr(data));
    tTJSVariant *params[] = { &targetValue, &dataValue };
    callMethod(context->target, TJS_W("processingInstruction"), 2, params);
}

void XMLCALL comment(void *userData, const XML_Char *data) {
    auto *context = static_cast<ParserContext *>(userData);
    tTJSVariant text(utf8ToTtstr(data));
    tTJSVariant *params[] = { &text };
    callMethod(context->target, TJS_W("comment"), 1, params);
}

void XMLCALL startCdataSection(void *userData) {
    auto *context = static_cast<ParserContext *>(userData);
    callMethod(context->target, TJS_W("startCdataSection"), 0, nullptr);
}

void XMLCALL endCdataSection(void *userData) {
    auto *context = static_cast<ParserContext *>(userData);
    callMethod(context->target, TJS_W("endCdataSection"), 0, nullptr);
}

void XMLCALL defaultHandler(void *userData, const XML_Char *data, int len) {
    auto *context = static_cast<ParserContext *>(userData);
    tTJSVariant text(utf8ToTtstr(data, static_cast<tjs_uint>(len)));
    tTJSVariant *params[] = { &text };
    callMethod(context->target, TJS_W("defaultHandler"), 1, params);
}

void XMLCALL defaultHandlerExpand(void *userData, const XML_Char *data,
                                  int len) {
    auto *context = static_cast<ParserContext *>(userData);
    tTJSVariant text(utf8ToTtstr(data, static_cast<tjs_uint>(len)));
    tTJSVariant *params[] = { &text };
    callMethod(context->target, TJS_W("defaultHandlerExpand"), 1, params);
}

} // namespace

#ifdef TJS_NATIVE_CLASSID_NAME
#undef TJS_NATIVE_CLASSID_NAME
#undef TJS_NCM_REG_THIS
#undef TJS_NATIVE_SET_ClassID
#endif
#define TJS_NCM_REG_THIS classobj
#define TJS_NATIVE_SET_ClassID TJS_NATIVE_CLASSID_NAME = TJS_NCM_CLASSID;
#define TJS_NATIVE_CLASSID_NAME ClassID_XMLParser
static tjs_int32 TJS_NATIVE_CLASSID_NAME = -1;

class NI_XMLParser : public tTJSNativeInstance {
public:
    NI_XMLParser() { createParser(); }

    ~NI_XMLParser() override { Invalidate(); }

    tjs_error Construct(tjs_int numparams, tTJSVariant **param,
                        iTJSDispatch2 *) override {
        if(numparams > 0)
            target_ = param[0]->AsObject();
        return TJS_S_OK;
    }

    void Invalidate() override {
        if(parser_) {
            XML_ParserFree(parser_);
            parser_ = nullptr;
        }
        if(target_) {
            target_->Release();
            target_ = nullptr;
        }
    }

    bool parse(const ttstr &text, iTJSDispatch2 *objthis) {
        const std::string bytes = ttstrToUtf8(text);
        prepare(objthis);
        const bool ok = XML_Parse(parser_, bytes.data(),
                                  static_cast<int>(bytes.size()), XML_TRUE) ==
                        XML_STATUS_OK;
        captureState();
        return ok;
    }

    bool parseStorage(const ttstr &filename, iTJSDispatch2 *objthis) {
        const std::string bytes = readStorageBytes(filename);
        prepare(objthis);
        const bool ok = XML_Parse(parser_, bytes.data(),
                                  static_cast<int>(bytes.size()), XML_TRUE) ==
                        XML_STATUS_OK;
        captureState();
        return ok;
    }

    XML_Error errorCode() const { return lastError_; }

    ttstr errorString() const {
        const XML_LChar *message = XML_ErrorString(lastError_);
        return message ? ttstr(message) : ttstr(TJS_W(""));
    }

    tTVInteger currentByteIndex() const { return currentByteIndex_; }
    tTVInteger currentLineNumber() const { return currentLineNumber_; }
    tTVInteger currentColumnNumber() const { return currentColumnNumber_; }
    tTVInteger currentByteCount() const { return currentByteCount_; }

private:
    void createParser() {
        parser_ = XML_ParserCreate("UTF-8");
        if(!parser_)
            TVPThrowExceptionMessage(TJS_W("cannot create XML parser"));
    }

    void prepare(iTJSDispatch2 *objthis) {
        if(!parser_)
            createParser();
        if(XML_ParserReset(parser_, "UTF-8") == XML_FALSE)
            TVPThrowExceptionMessage(TJS_W("cannot reset XML parser"));

        context_.target = target_ ? target_ : objthis;
        XML_SetUserData(parser_, &context_);

        if(isValidMember(context_.target, TJS_W("startElement")))
            XML_SetStartElementHandler(parser_, startElement);
        if(isValidMember(context_.target, TJS_W("endElement")))
            XML_SetEndElementHandler(parser_, endElement);
        if(isValidMember(context_.target, TJS_W("characterData")))
            XML_SetCharacterDataHandler(parser_, characterData);
        if(isValidMember(context_.target, TJS_W("processingInstruction")))
            XML_SetProcessingInstructionHandler(parser_,
                                                processingInstruction);
        if(isValidMember(context_.target, TJS_W("comment")))
            XML_SetCommentHandler(parser_, comment);
        if(isValidMember(context_.target, TJS_W("startCdataSection")))
            XML_SetStartCdataSectionHandler(parser_, startCdataSection);
        if(isValidMember(context_.target, TJS_W("endCdataSection")))
            XML_SetEndCdataSectionHandler(parser_, endCdataSection);
        if(isValidMember(context_.target, TJS_W("defaultHandler")))
            XML_SetDefaultHandler(parser_, defaultHandler);
        if(isValidMember(context_.target, TJS_W("defaultHandlerExpand")))
            XML_SetDefaultHandlerExpand(parser_, defaultHandlerExpand);

        captureState();
    }

    void captureState() {
        if(!parser_)
            return;
        lastError_ = XML_GetErrorCode(parser_);
        currentByteIndex_ = XML_GetCurrentByteIndex(parser_);
        currentLineNumber_ = XML_GetCurrentLineNumber(parser_);
        currentColumnNumber_ = XML_GetCurrentColumnNumber(parser_);
        currentByteCount_ = XML_GetCurrentByteCount(parser_);
    }

    XML_Parser parser_ = nullptr;
    iTJSDispatch2 *target_ = nullptr;
    ParserContext context_;
    XML_Error lastError_ = XML_ERROR_NONE;
    tTVInteger currentByteIndex_ = 0;
    tTVInteger currentLineNumber_ = 0;
    tTVInteger currentColumnNumber_ = 0;
    tTVInteger currentByteCount_ = 0;
};

static iTJSNativeInstance *Create_NI_XMLParser() { return new NI_XMLParser(); }

static iTJSDispatch2 *Create_NC_XMLParser() {
    tTJSNativeClassForPlugin *classobj =
        TJSCreateNativeClassForPlugin(TJS_W("XMLParser"), Create_NI_XMLParser);

    TJS_BEGIN_NATIVE_MEMBERS(XMLParser)

    TJS_DECL_EMPTY_FINALIZE_METHOD

    TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL(_this, NI_XMLParser, XMLParser) {
        return TJS_S_OK;
    }
    TJS_END_NATIVE_CONSTRUCTOR_DECL(XMLParser)

    TJS_BEGIN_NATIVE_METHOD_DECL(parse) {
        TJS_GET_NATIVE_INSTANCE(_this, NI_XMLParser);
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        iTJSDispatch2 *target = numparams > 1 ? param[1]->AsObjectNoAddRef()
                                              : objthis;
        if(result)
            *result = _this->parse(param[0]->AsStringNoAddRef(), target);
        else
            _this->parse(param[0]->AsStringNoAddRef(), target);
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(parse)

    TJS_BEGIN_NATIVE_METHOD_DECL(parseStorage) {
        TJS_GET_NATIVE_INSTANCE(_this, NI_XMLParser);
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        iTJSDispatch2 *target = numparams > 1 ? param[1]->AsObjectNoAddRef()
                                              : objthis;
        if(result)
            *result = _this->parseStorage(param[0]->AsStringNoAddRef(), target);
        else
            _this->parseStorage(param[0]->AsStringNoAddRef(), target);
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(parseStorage)

    TJS_BEGIN_NATIVE_PROP_DECL(errorCode) {
        TJS_BEGIN_NATIVE_PROP_GETTER {
            TJS_GET_NATIVE_INSTANCE(_this, NI_XMLParser);
            *result = static_cast<tTVInteger>(_this->errorCode());
            return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER
        TJS_DENY_NATIVE_PROP_SETTER
    }
    TJS_END_NATIVE_PROP_DECL(errorCode)

    TJS_BEGIN_NATIVE_PROP_DECL(errorString) {
        TJS_BEGIN_NATIVE_PROP_GETTER {
            TJS_GET_NATIVE_INSTANCE(_this, NI_XMLParser);
            *result = _this->errorString();
            return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER
        TJS_DENY_NATIVE_PROP_SETTER
    }
    TJS_END_NATIVE_PROP_DECL(errorString)

    TJS_BEGIN_NATIVE_PROP_DECL(currentByteIndex) {
        TJS_BEGIN_NATIVE_PROP_GETTER {
            TJS_GET_NATIVE_INSTANCE(_this, NI_XMLParser);
            *result = _this->currentByteIndex();
            return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER
        TJS_DENY_NATIVE_PROP_SETTER
    }
    TJS_END_NATIVE_PROP_DECL(currentByteIndex)

    TJS_BEGIN_NATIVE_PROP_DECL(currentLineNumber) {
        TJS_BEGIN_NATIVE_PROP_GETTER {
            TJS_GET_NATIVE_INSTANCE(_this, NI_XMLParser);
            *result = _this->currentLineNumber();
            return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER
        TJS_DENY_NATIVE_PROP_SETTER
    }
    TJS_END_NATIVE_PROP_DECL(currentLineNumber)

    TJS_BEGIN_NATIVE_PROP_DECL(currentColumnNumber) {
        TJS_BEGIN_NATIVE_PROP_GETTER {
            TJS_GET_NATIVE_INSTANCE(_this, NI_XMLParser);
            *result = _this->currentColumnNumber();
            return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER
        TJS_DENY_NATIVE_PROP_SETTER
    }
    TJS_END_NATIVE_PROP_DECL(currentColumnNumber)

    TJS_BEGIN_NATIVE_PROP_DECL(currentByteCount) {
        TJS_BEGIN_NATIVE_PROP_GETTER {
            TJS_GET_NATIVE_INSTANCE(_this, NI_XMLParser);
            *result = _this->currentByteCount();
            return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER
        TJS_DENY_NATIVE_PROP_SETTER
    }
    TJS_END_NATIVE_PROP_DECL(currentByteCount)

    TJS_BEGIN_NATIVE_PROP_DECL(currentButeCount) {
        TJS_BEGIN_NATIVE_PROP_GETTER {
            TJS_GET_NATIVE_INSTANCE(_this, NI_XMLParser);
            *result = _this->currentByteCount();
            return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER
        TJS_DENY_NATIVE_PROP_SETTER
    }
    TJS_END_NATIVE_PROP_DECL(currentButeCount)

    TJS_END_NATIVE_MEMBERS

    return classobj;
}

static void InitPlugin_XMLParser() {
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(global) {
        addMember(global, TJS_W("XMLParser"), Create_NC_XMLParser());
        global->Release();
    }
}

static void UninitPlugin_XMLParser() {
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(global) {
        delMember(global, TJS_W("XMLParser"));
        global->Release();
    }
}

NCB_PRE_REGIST_CALLBACK(InitPlugin_XMLParser);
NCB_POST_UNREGIST_CALLBACK(UninitPlugin_XMLParser);
