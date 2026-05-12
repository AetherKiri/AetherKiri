#include "ncbind.hpp"
#include "tp_stub.h"
#include "TextStream.h"

#include <string>

#define NCB_MODULE_NAME TJS_W("json.dll")

namespace {

bool isSpace(tjs_char ch) {
    return ch == TJS_W(' ') || ch == TJS_W('\t') || ch == TJS_W('\r') ||
           ch == TJS_W('\n');
}

class JsonParser {
public:
    explicit JsonParser(const ttstr &source) : text(source.c_str()) {}

    tTJSVariant parse() {
        skipSpace();
        tTJSVariant value = parseValue();
        skipSpace();
        if(*text)
            TVPThrowExceptionMessage(TJS_W("JSON parse error: trailing input"));
        return value;
    }

private:
    void skipSpace() {
        while(isSpace(*text))
            ++text;
    }

    bool consume(tjs_char ch) {
        skipSpace();
        if(*text != ch)
            return false;
        ++text;
        return true;
    }

    void expect(tjs_char ch) {
        if(!consume(ch))
            TVPThrowExceptionMessage(TJS_W("JSON parse error: unexpected token"));
    }

    bool consumeWord(const tjs_char *word) {
        skipSpace();
        const tjs_char *p = text;
        while(*word) {
            if(*p++ != *word++)
                return false;
        }
        text = p;
        return true;
    }

    tTJSVariant parseValue() {
        skipSpace();
        if(*text == TJS_W('"'))
            return tTJSVariant(parseString());
        if(*text == TJS_W('{'))
            return parseObject();
        if(*text == TJS_W('['))
            return parseArray();
        if(*text == TJS_W('-') || (*text >= TJS_W('0') && *text <= TJS_W('9')))
            return parseNumber();
        if(consumeWord(TJS_W("true")))
            return tTJSVariant(true);
        if(consumeWord(TJS_W("false")))
            return tTJSVariant(false);
        if(consumeWord(TJS_W("null")))
            return tTJSVariant();
        TVPThrowExceptionMessage(TJS_W("JSON parse error: invalid value"));
        return tTJSVariant();
    }

    static int hexValue(tjs_char ch) {
        if(ch >= TJS_W('0') && ch <= TJS_W('9'))
            return ch - TJS_W('0');
        if(ch >= TJS_W('a') && ch <= TJS_W('f'))
            return ch - TJS_W('a') + 10;
        if(ch >= TJS_W('A') && ch <= TJS_W('F'))
            return ch - TJS_W('A') + 10;
        return -1;
    }

    ttstr parseString() {
        expect(TJS_W('"'));
        ttstr result;
        while(*text && *text != TJS_W('"')) {
            tjs_char ch = *text++;
            if(ch == TJS_W('\\')) {
                ch = *text++;
                switch(ch) {
                    case TJS_W('"'):
                    case TJS_W('\\'):
                    case TJS_W('/'):
                        result += ch;
                        break;
                    case TJS_W('b'):
                        result += static_cast<tjs_char>(0x08);
                        break;
                    case TJS_W('f'):
                        result += static_cast<tjs_char>(0x0c);
                        break;
                    case TJS_W('n'):
                        result += TJS_W('\n');
                        break;
                    case TJS_W('r'):
                        result += TJS_W('\r');
                        break;
                    case TJS_W('t'):
                        result += TJS_W('\t');
                        break;
                    case TJS_W('u'): {
                        int value = 0;
                        for(int i = 0; i < 4; ++i) {
                            int digit = hexValue(*text++);
                            if(digit < 0)
                                TVPThrowExceptionMessage(
                                    TJS_W("JSON parse error: invalid unicode escape"));
                            value = (value << 4) | digit;
                        }
                        result += static_cast<tjs_char>(value);
                        break;
                    }
                    default:
                        TVPThrowExceptionMessage(
                            TJS_W("JSON parse error: invalid string escape"));
                }
            } else {
                result += ch;
            }
        }
        expect(TJS_W('"'));
        return result;
    }

    tTJSVariant parseNumber() {
        const tjs_char *begin = text;
        if(*text == TJS_W('-'))
            ++text;
        while(*text >= TJS_W('0') && *text <= TJS_W('9'))
            ++text;
        bool real = false;
        if(*text == TJS_W('.')) {
            real = true;
            ++text;
            while(*text >= TJS_W('0') && *text <= TJS_W('9'))
                ++text;
        }
        if(*text == TJS_W('e') || *text == TJS_W('E')) {
            real = true;
            ++text;
            if(*text == TJS_W('+') || *text == TJS_W('-'))
                ++text;
            while(*text >= TJS_W('0') && *text <= TJS_W('9'))
                ++text;
        }

        ttstr number(begin, text - begin);
        if(real)
            return tTJSVariant(static_cast<tTVReal>(TJSStringToReal(number.c_str())));
        return tTJSVariant(static_cast<tTVInteger>(number.AsInteger()));
    }

    tTJSVariant parseArray() {
        expect(TJS_W('['));
        iTJSDispatch2 *array = TJSCreateArrayObject();
        tjs_int index = 0;
        if(!consume(TJS_W(']'))) {
            do {
                tTJSVariant value = parseValue();
                array->PropSetByNum(TJS_MEMBERENSURE, index++, &value, array);
            } while(consume(TJS_W(',')));
            expect(TJS_W(']'));
        }
        tTJSVariant result(array, array);
        array->Release();
        return result;
    }

    tTJSVariant parseObject() {
        expect(TJS_W('{'));
        iTJSDispatch2 *dict = TJSCreateDictionaryObject();
        if(!consume(TJS_W('}'))) {
            do {
                skipSpace();
                if(*text != TJS_W('"'))
                    TVPThrowExceptionMessage(
                        TJS_W("JSON parse error: object key must be string"));
                ttstr key = parseString();
                expect(TJS_W(':'));
                tTJSVariant value = parseValue();
                dict->PropSet(TJS_MEMBERENSURE, key.c_str(), nullptr, &value,
                              dict);
            } while(consume(TJS_W(',')));
            expect(TJS_W('}'));
        }
        tTJSVariant result(dict, dict);
        dict->Release();
        return result;
    }

    const tjs_char *text;
};

class JsonWriter {
public:
    explicit JsonWriter(tjs_int newline) : pretty(newline != 0) {}

    ttstr stringify(tTJSVariant value) {
        writeValue(value);
        return output;
    }

private:
    void newline() {
        if(!pretty)
            return;
        output += TJS_W('\n');
        for(int i = 0; i < indent; ++i)
            output += TJS_W('\t');
    }

    void writeString(const tjs_char *str) {
        output += TJS_W('"');
        for(const tjs_char *p = str ? str : TJS_W(""); *p; ++p) {
            switch(*p) {
                case TJS_W('"'):
                    output += TJS_W("\\\"");
                    break;
                case TJS_W('\\'):
                    output += TJS_W("\\\\");
                    break;
                case TJS_W('\b'):
                    output += TJS_W("\\b");
                    break;
                case TJS_W('\f'):
                    output += TJS_W("\\f");
                    break;
                case TJS_W('\n'):
                    output += TJS_W("\\n");
                    break;
                case TJS_W('\r'):
                    output += TJS_W("\\r");
                    break;
                case TJS_W('\t'):
                    output += TJS_W("\\t");
                    break;
                default:
                    if(*p < 0x20) {
                        tjs_char buffer[8];
                        static const tjs_char hex[] = TJS_W("0123456789abcdef");
                        buffer[0] = TJS_W('\\');
                        buffer[1] = TJS_W('u');
                        buffer[2] = hex[(*p >> 12) & 0x0f];
                        buffer[3] = hex[(*p >> 8) & 0x0f];
                        buffer[4] = hex[(*p >> 4) & 0x0f];
                        buffer[5] = hex[*p & 0x0f];
                        buffer[6] = 0;
                        output += buffer;
                    } else {
                        output += *p;
                    }
            }
        }
        output += TJS_W('"');
    }

    void writeValue(tTJSVariant &value) {
        switch(value.Type()) {
            case tvtVoid:
                output += TJS_W("null");
                break;
            case tvtString:
                writeString(value.GetString());
                break;
            case tvtInteger:
                output += ttstr(value.AsInteger());
                break;
            case tvtReal:
                output += ttstr(value);
                break;
            case tvtObject:
                writeObject(value.AsObjectNoAddRef());
                break;
            default:
                output += TJS_W("null");
                break;
        }
    }

    bool isArray(iTJSDispatch2 *object) {
        return object && object->IsInstanceOf(TJS_IGNOREPROP, nullptr, nullptr,
                                              TJS_W("Array"), object) ==
                             TJS_S_TRUE;
    }

    tjs_int arrayCount(iTJSDispatch2 *array) {
        tTJSVariant count;
        if(TJS_SUCCEEDED(array->PropGet(TJS_IGNOREPROP, TJS_W("count"), nullptr,
                                        &count, array)))
            return static_cast<tjs_int>(count.AsInteger());
        return 0;
    }

    void writeObject(iTJSDispatch2 *object) {
        if(!object) {
            output += TJS_W("null");
            return;
        }
        if(isArray(object))
            writeArray(object);
        else
            writeDictionary(object);
    }

    void writeArray(iTJSDispatch2 *array) {
        output += TJS_W('[');
        ++indent;
        const tjs_int count = arrayCount(array);
        for(tjs_int i = 0; i < count; ++i) {
            if(i != 0)
                output += TJS_W(',');
            newline();
            tTJSVariant value;
            array->PropGetByNum(TJS_IGNOREPROP, i, &value, array);
            writeValue(value);
        }
        --indent;
        if(count > 0)
            newline();
        output += TJS_W(']');
    }

    class EnumWriter : public tTJSDispatch {
    public:
        explicit EnumWriter(JsonWriter *owner) : owner(owner) {}

        tjs_error FuncCall(tjs_uint32, const tjs_char *, tjs_uint32 *,
                           tTJSVariant *result, tjs_int numparams,
                           tTJSVariant **param, iTJSDispatch2 *) override {
            if(numparams > 2) {
                const tTVInteger flags = param[1]->AsInteger();
                if((flags & TJS_HIDDENMEMBER) == 0) {
                    if(!owner->firstMember)
                        owner->output += TJS_W(',');
                    owner->firstMember = false;
                    owner->newline();
                    owner->writeString(param[0]->GetString());
                    owner->output += owner->pretty ? TJS_W(": ") : TJS_W(":");
                    owner->writeValue(*param[2]);
                }
            }
            if(result)
                *result = true;
            return TJS_S_OK;
        }

    private:
        JsonWriter *owner;
    };

    void writeDictionary(iTJSDispatch2 *dict) {
        output += TJS_W('{');
        ++indent;
        bool parentFirst = firstMember;
        firstMember = true;
        auto *caller = new EnumWriter(this);
        tTJSVariantClosure closure(caller);
        dict->EnumMembers(TJS_IGNOREPROP, &closure, dict);
        caller->Release();
        const bool empty = firstMember;
        firstMember = parentFirst;
        --indent;
        if(!empty)
            newline();
        output += TJS_W('}');
    }

    ttstr output;
    bool pretty;
    int indent = 0;
    bool firstMember = true;
};

ttstr loadJsonText(const ttstr &filename, bool utf8) {
    std::unique_ptr<iTJSTextReadStream> stream(
        TVPCreateTextStreamForRead(TVPGetPlacedPath(filename),
                                   utf8 ? TJS_W("utf-8") : TJS_W("")));
    ttstr text;
    stream->Read(text, 0);
    return text;
}

void saveJsonText(const ttstr &filename, const ttstr &text, bool utf8) {
    std::unique_ptr<iTJSTextWriteStream> stream(
        TVPCreateTextStreamForWrite(TVPGetPlacedPath(filename),
                                    utf8 ? TJS_W("utf-8") : TJS_W("")));
    stream->Write(text);
}

class ScriptsJSON {
public:
    static tjs_error evalJSON(tTJSVariant *result, tjs_int numparams,
                              tTJSVariant **param, iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        JsonParser parser(param[0]->GetString());
        if(result)
            *result = parser.parse();
        return TJS_S_OK;
    }

    static tjs_error evalJSONStorage(tTJSVariant *result, tjs_int numparams,
                                     tTJSVariant **param, iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        bool utf8 = numparams > 1 && static_cast<tjs_int>(*param[1]) != 0;
        JsonParser parser(loadJsonText(param[0]->GetString(), utf8));
        if(result)
            *result = parser.parse();
        return TJS_S_OK;
    }

    static tjs_error saveJSON(tTJSVariant *result, tjs_int numparams,
                              tTJSVariant **param, iTJSDispatch2 *) {
        if(numparams < 2)
            return TJS_E_BADPARAMCOUNT;
        bool utf8 = numparams > 2 && static_cast<tjs_int>(*param[2]) != 0;
        tjs_int newline = numparams > 3 ? static_cast<tjs_int>(*param[3]) : 0;
        JsonWriter writer(newline);
        saveJsonText(param[0]->GetString(), writer.stringify(*param[1]), utf8);
        if(result)
            *result = true;
        return TJS_S_OK;
    }

    static tjs_error toJSONString(tTJSVariant *result, tjs_int numparams,
                                  tTJSVariant **param, iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        tjs_int newline = numparams > 1 ? static_cast<tjs_int>(*param[1]) : 0;
        JsonWriter writer(newline);
        if(result)
            *result = writer.stringify(*param[0]);
        return TJS_S_OK;
    }
};

} // namespace

NCB_ATTACH_CLASS(ScriptsJSON, Scripts) {
    RawCallback(TJS_W("evalJSON"), &ScriptsJSON::evalJSON, TJS_STATICMEMBER);
    RawCallback(TJS_W("evalJSONStorage"), &ScriptsJSON::evalJSONStorage,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("saveJSON"), &ScriptsJSON::saveJSON, TJS_STATICMEMBER);
    RawCallback(TJS_W("toJSONString"), &ScriptsJSON::toJSONString,
                TJS_STATICMEMBER);
}
