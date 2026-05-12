#include "PluginStub.h"
#include "ncbind.hpp"

#include <memory>
#include <string>
#include <vector>

#define NCB_MODULE_NAME TJS_W("lineParser.dll")

namespace {

class LineSource {
public:
    virtual ~LineSource() = default;
    virtual bool getNextLine(ttstr &line) = 0;
};

class StringLineSource final : public LineSource {
public:
    explicit StringLineSource(const ttstr &text) : text_(text) {}

    bool getNextLine(ttstr &line) override {
        line = TJS_W("");
        int ch;
        while((ch = getc()) != EOF && !endOfLine(static_cast<tjs_char>(ch)))
            line += static_cast<tjs_char>(ch);

        return line.length() > 0 || ch != EOF;
    }

private:
    int getc() {
        return pos_ < static_cast<tjs_uint>(text_.length()) ? text_[pos_++]
                                                             : EOF;
    }

    void ungetc() {
        if(pos_ > 0)
            --pos_;
    }

    bool eof() const { return pos_ >= static_cast<tjs_uint>(text_.length()); }

    bool endOfLine(tjs_char ch) {
        const bool eol = ch == TJS_W('\r') || ch == TJS_W('\n');
        if(ch == TJS_W('\r')) {
            ch = static_cast<tjs_char>(getc());
            if(!eof() && ch != TJS_W('\n'))
                ungetc();
        }
        return eol;
    }

    ttstr text_;
    tjs_uint pos_ = 0;
};

ttstr readUtf8Storage(const ttstr &filename) {
    std::unique_ptr<tTJSBinaryStream> stream(
        TVPCreateStream(filename, TJS_BS_READ));
    if(!stream)
        TVPThrowExceptionMessage((ttstr(TJS_W("cannot open : ")) + filename)
                                     .c_str());

    const auto size64 = stream->GetSize();
    if(size64 > static_cast<tjs_uint64>(static_cast<tjs_uint>(-1)))
        TVPThrowExceptionMessage(TJS_W("storage too large"));

    const auto size = static_cast<tjs_uint>(size64);
    std::string bytes(size, '\0');
    if(size > 0)
        stream->ReadBuffer(bytes.data(), size);

    tjs_int length = TVPUtf8ToWideCharString(
        bytes.data(), size, static_cast<tjs_char *>(nullptr));
    if(length < 0)
        TVPThrowExceptionMessage(TJS_W("invalid UTF-8 storage"));

    std::vector<tjs_char> wide(static_cast<size_t>(length) + 1, 0);
    if(length > 0)
        TVPUtf8ToWideCharString(bytes.data(), size, wide.data());
    return ttstr(wide.data());
}

ttstr readTextStorage(const ttstr &filename) {
    std::unique_ptr<iTJSTextReadStream> stream(
        TVPCreateTextStreamForRead(filename, TJS_W("")));
    if(!stream)
        TVPThrowExceptionMessage((ttstr(TJS_W("cannot open : ")) + filename)
                                     .c_str());

    ttstr text;
    stream->Read(text, 0);
    return text;
}

void addMember(iTJSDispatch2 *dispatch, const tjs_char *name,
               iTJSDispatch2 *member) {
    tTJSVariant value(member);
    member->Release();
    dispatch->PropSet(TJS_MEMBERENSURE, name, nullptr, &value, dispatch);
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

bool isValidMember(iTJSDispatch2 *dispatch, const tjs_char *name) {
    return dispatch->IsValid(TJS_IGNOREPROP, name, nullptr, dispatch) ==
           TJS_S_TRUE;
}

} // namespace

#ifdef TJS_NATIVE_CLASSID_NAME
#undef TJS_NATIVE_CLASSID_NAME
#undef TJS_NCM_REG_THIS
#undef TJS_NATIVE_SET_ClassID
#endif
#define TJS_NCM_REG_THIS classobj
#define TJS_NATIVE_SET_ClassID TJS_NATIVE_CLASSID_NAME = TJS_NCM_CLASSID;
#define TJS_NATIVE_CLASSID_NAME ClassID_LineParser
static tjs_int32 TJS_NATIVE_CLASSID_NAME = -1;

class NI_LineParser : public tTJSNativeInstance {
public:
    NI_LineParser() = default;
    ~NI_LineParser() override { clear(); }

    tjs_error Construct(tjs_int numparams, tTJSVariant **param,
                        iTJSDispatch2 *) override {
        if(numparams > 0)
            target_ = param[0]->AsObject();
        return TJS_S_OK;
    }

    void Invalidate() override {
        clear();
        if(target_) {
            target_->Release();
            target_ = nullptr;
        }
    }

    void init(const ttstr &text) {
        source_ = std::make_unique<StringLineSource>(text);
        lineNo_ = 0;
    }

    void initStorage(const ttstr &filename, bool utf8 = false) {
        init(utf8 ? readUtf8Storage(filename) : readTextStorage(filename));
    }

    bool getNextLine(ttstr &line) {
        if(!source_)
            return false;

        if(source_->getNextLine(line)) {
            ++lineNo_;
            return true;
        }

        clear();
        return false;
    }

    tjs_int32 currentLineNumber() const { return lineNo_; }

    void parse(iTJSDispatch2 *objthis) {
        iTJSDispatch2 *target = target_ ? target_ : objthis;
        if(!source_ || !target || !isValidMember(target, TJS_W("doLine")))
            return;

        iTJSDispatch2 *method = getMember(target, TJS_W("doLine"));
        ttstr line;
        while(getNextLine(line)) {
            tTJSVariant lineValue(line);
            tTJSVariant lineNoValue(lineNo_);
            tTJSVariant *params[] = { &lineValue, &lineNoValue };
            method->FuncCall(0, nullptr, nullptr, nullptr, 2, params, target);
        }
        method->Release();
        clear();
    }

private:
    void clear() { source_.reset(); }

    iTJSDispatch2 *target_ = nullptr;
    std::unique_ptr<LineSource> source_;
    tjs_int32 lineNo_ = 0;
};

static iTJSNativeInstance *Create_NI_LineParser() {
    return new NI_LineParser();
}

static iTJSDispatch2 *Create_NC_LineParser() {
    tTJSNativeClassForPlugin *classobj =
        TJSCreateNativeClassForPlugin(TJS_W("LineParser"), Create_NI_LineParser);

    TJS_BEGIN_NATIVE_MEMBERS(LineParser)

    TJS_DECL_EMPTY_FINALIZE_METHOD

    TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL(_this, NI_LineParser, LineParser) {
        return TJS_S_OK;
    }
    TJS_END_NATIVE_CONSTRUCTOR_DECL(LineParser)

    TJS_BEGIN_NATIVE_METHOD_DECL(init) {
        TJS_GET_NATIVE_INSTANCE(_this, NI_LineParser);
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        _this->init(param[0]->AsStringNoAddRef());
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(init)

    TJS_BEGIN_NATIVE_METHOD_DECL(initStorage) {
        TJS_GET_NATIVE_INSTANCE(_this, NI_LineParser);
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        _this->initStorage(param[0]->AsStringNoAddRef(),
                           numparams > 1 && (tjs_int)*param[1] != 0);
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(initStorage)

    TJS_BEGIN_NATIVE_METHOD_DECL(getNextLine) {
        TJS_GET_NATIVE_INSTANCE(_this, NI_LineParser);
        ttstr line;
        if(_this->getNextLine(line) && result)
            *result = line;
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(getNextLine)

    TJS_BEGIN_NATIVE_METHOD_DECL(parse) {
        TJS_GET_NATIVE_INSTANCE(_this, NI_LineParser);
        if(numparams > 0)
            _this->init(param[0]->AsStringNoAddRef());
        _this->parse(objthis);
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(parse)

    TJS_BEGIN_NATIVE_METHOD_DECL(parseStorage) {
        TJS_GET_NATIVE_INSTANCE(_this, NI_LineParser);
        if(numparams > 0) {
            _this->initStorage(param[0]->AsStringNoAddRef(),
                               numparams > 1 && (tjs_int)*param[1] != 0);
        }
        _this->parse(objthis);
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(parseStorage)

    TJS_BEGIN_NATIVE_PROP_DECL(currentLineNumber) {
        TJS_BEGIN_NATIVE_PROP_GETTER {
            TJS_GET_NATIVE_INSTANCE(_this, NI_LineParser);
            *result = _this->currentLineNumber();
            return TJS_S_OK;
        }
        TJS_END_NATIVE_PROP_GETTER
        TJS_DENY_NATIVE_PROP_SETTER
    }
    TJS_END_NATIVE_PROP_DECL(currentLineNumber)

    TJS_END_NATIVE_MEMBERS

    return classobj;
}

static void InitPlugin_LineParser() {
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(global) {
        addMember(global, TJS_W("LineParser"), Create_NC_LineParser());
        global->Release();
    }
}

NCB_PRE_REGIST_CALLBACK(InitPlugin_LineParser);
