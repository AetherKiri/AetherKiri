#include "ncbind.hpp"
#include "StorageIntf.h"
#include "TextStream.h"
#include "tp_stub.h"

#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#define NCB_MODULE_NAME TJS_W("toml.dll")

namespace {

using TomlString = std::basic_string<tjs_char>;

bool isSpace(tjs_char ch) {
    return ch == TJS_W(' ') || ch == TJS_W('\t') || ch == TJS_W('\r') ||
           ch == TJS_W('\n') || ch == static_cast<tjs_char>(0xfeff);
}

bool isDigit(tjs_char ch) { return ch >= TJS_W('0') && ch <= TJS_W('9'); }

bool isHexDigit(tjs_char ch) {
    return isDigit(ch) || (ch >= TJS_W('a') && ch <= TJS_W('f')) ||
           (ch >= TJS_W('A') && ch <= TJS_W('F'));
}

tjs_int hexValue(tjs_char ch) {
    if(ch >= TJS_W('0') && ch <= TJS_W('9'))
        return ch - TJS_W('0');
    if(ch >= TJS_W('a') && ch <= TJS_W('f'))
        return ch - TJS_W('a') + 10;
    if(ch >= TJS_W('A') && ch <= TJS_W('F'))
        return ch - TJS_W('A') + 10;
    return -1;
}

TomlString trim(const TomlString &value) {
    size_t begin = 0;
    size_t end = value.size();
    while(begin < end && isSpace(value[begin]))
        ++begin;
    while(end > begin && isSpace(value[end - 1]))
        --end;
    return value.substr(begin, end - begin);
}

tTJSVariant makeDictionary() {
    iTJSDispatch2 *dict = TJSCreateDictionaryObject();
    tTJSVariant value(dict, dict);
    dict->Release();
    return value;
}

tTJSVariant makeArray() {
    iTJSDispatch2 *array = TJSCreateArrayObject();
    tTJSVariant value(array, array);
    array->Release();
    return value;
}

void setDictionaryValue(iTJSDispatch2 *dict, const TomlString &key,
                        tTJSVariant &value) {
    if(!dict)
        return;
    dict->PropSet(TJS_MEMBERENSURE, key.c_str(), nullptr, &value, dict);
}

bool isKbadTomlBytes(const std::vector<std::uint8_t> &bytes) {
    static constexpr char Signature[] = "KBAD100";
    return bytes.size() >= 8 &&
           std::equal(std::begin(Signature), std::end(Signature) - 1,
                      bytes.begin()) &&
           bytes[7] == 0;
}

std::vector<std::uint8_t> readStorageBytes(const ttstr &placed) {
    std::unique_ptr<tTJSBinaryStream> stream(TVPCreateStream(placed, TJS_BS_READ));
    if(!stream)
        TVPThrowExceptionMessage(
            (ttstr(TJS_W("cannot open : ")) + placed).c_str());

    const auto size64 = stream->GetSize();
    if(size64 > static_cast<tjs_uint64>(static_cast<tjs_uint>(-1)))
        TVPThrowExceptionMessage(TJS_W("storage too large"));

    const auto size = static_cast<tjs_uint>(size64);
    std::vector<std::uint8_t> bytes(size);
    if(size > 0)
        stream->ReadBuffer(bytes.data(), size);
    return bytes;
}

TomlString decodeUtf16LeBytes(const std::uint8_t *data, size_t size) {
    if(size >= 2 && data[0] == 0xff && data[1] == 0xfe) {
        data += 2;
        size -= 2;
    }
    if(size & 1)
        --size;

    TomlString out;
    out.reserve(size / 2);
    for(size_t i = 0; i + 1 < size; i += 2) {
        const tjs_char ch =
            static_cast<tjs_char>(data[i] |
                                  (static_cast<std::uint16_t>(data[i + 1]) << 8));
        if(ch != 0)
            out.push_back(ch);
    }
    return out;
}

class KbadDecoder {
public:
    explicit KbadDecoder(const std::vector<std::uint8_t> &bytes) :
        bytes(bytes), pos(8) {}

    tTJSVariant decode() {
        tTJSVariant value = decodeValue();
        if(pos != bytes.size())
            throw std::runtime_error("trailing KBAD data");
        return value;
    }

private:
    std::uint8_t readU8() {
        require(1);
        return bytes[pos++];
    }

    std::uint16_t readU16() {
        require(2);
        std::uint16_t value = bytes[pos] |
            (static_cast<std::uint16_t>(bytes[pos + 1]) << 8);
        pos += 2;
        return value;
    }

    std::uint32_t readU32() {
        require(4);
        std::uint32_t value = bytes[pos] |
            (static_cast<std::uint32_t>(bytes[pos + 1]) << 8) |
            (static_cast<std::uint32_t>(bytes[pos + 2]) << 16) |
            (static_cast<std::uint32_t>(bytes[pos + 3]) << 24);
        pos += 4;
        return value;
    }

    std::uint64_t readU64() {
        require(8);
        std::uint64_t value = 0;
        for(int shift = 0; shift < 64; shift += 8)
            value |= static_cast<std::uint64_t>(bytes[pos++]) << shift;
        return value;
    }

    float readFloat32() {
        const std::uint32_t bits = readU32();
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    double readFloat64() {
        const std::uint64_t bits = readU64();
        double value = 0.0;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    TomlString readString(size_t charCount) {
        if(charCount > (bytes.size() - pos) / 2)
            throw std::runtime_error("truncated KBAD string");
        TomlString value =
            decodeUtf16LeBytes(bytes.data() + pos, charCount * 2);
        pos += charCount * 2;
        return value;
    }

    tTJSVariant decodeValue() {
        const std::uint8_t type = readU8();
        if(type <= 0x7f)
            return tTJSVariant(static_cast<tTVInteger>(type));
        if(type >= 0xe0)
            return tTJSVariant(static_cast<tTVInteger>(
                static_cast<std::int8_t>(type)));

        if(type >= 0x80 && type <= 0x8f)
            return decodeMap(type & 0x0f);
        if(type >= 0x90 && type <= 0x9f)
            return decodeArray(type & 0x0f);
        if(type >= 0xa0 && type <= 0xbf)
            return tTJSVariant(ttstr(readString(type & 0x1f)));

        switch(type) {
        case 0xc0:
        case 0xc1:
            return tTJSVariant();
        case 0xc2:
            return tTJSVariant(false);
        case 0xc3:
            return tTJSVariant(true);
        case 0xc4:
            return tTJSVariant(ttstr(readString(readU8())));
        case 0xc5:
            return tTJSVariant(ttstr(readString(readU16())));
        case 0xc6:
            return tTJSVariant(ttstr(readString(readU32())));
        case 0xca:
            return tTJSVariant(static_cast<tTVReal>(readFloat32()));
        case 0xcb:
            return tTJSVariant(static_cast<tTVReal>(readFloat64()));
        case 0xcc:
            return tTJSVariant(static_cast<tTVInteger>(readU8()));
        case 0xcd:
            return tTJSVariant(static_cast<tTVInteger>(readU16()));
        case 0xce:
            return tTJSVariant(static_cast<tTVInteger>(readU32()));
        case 0xcf:
            return tTJSVariant(static_cast<tTVInteger>(readU64()));
        case 0xd0:
            return tTJSVariant(static_cast<tTVInteger>(
                static_cast<std::int8_t>(readU8())));
        case 0xd1:
            return tTJSVariant(static_cast<tTVInteger>(
                static_cast<std::int16_t>(readU16())));
        case 0xd2:
            return tTJSVariant(static_cast<tTVInteger>(
                static_cast<std::int32_t>(readU32())));
        case 0xd3:
            return tTJSVariant(static_cast<tTVInteger>(
                static_cast<std::int64_t>(readU64())));
        case 0xd9:
            return tTJSVariant(ttstr(readString(readU8())));
        case 0xda:
            return tTJSVariant(ttstr(readString(readU16())));
        case 0xdb:
            return tTJSVariant(ttstr(readString(readU32())));
        case 0xdc:
            return decodeArray(readU16());
        case 0xdd:
            return decodeArray(readU32());
        case 0xde:
            return decodeMap(readU16());
        case 0xdf:
            return decodeMap(readU32());
        default:
            throw std::runtime_error("unsupported KBAD type");
        }
    }

    tTJSVariant decodeArray(size_t count) {
        tTJSVariant result = makeArray();
        iTJSDispatch2 *array = result.AsObjectNoAddRef();
        for(size_t i = 0; i < count; ++i) {
            tTJSVariant value = decodeValue();
            array->PropSetByNum(TJS_MEMBERENSURE, static_cast<tjs_int>(i),
                                &value, array);
        }
        return result;
    }

    tTJSVariant decodeMap(size_t count) {
        tTJSVariant result = makeDictionary();
        iTJSDispatch2 *dict = result.AsObjectNoAddRef();
        for(size_t i = 0; i < count; ++i) {
            tTJSVariant keyValue = decodeValue();
            tTJSVariant value = decodeValue();
            const tjs_char *keyChars = keyValue.GetString();
            if(keyChars)
                setDictionaryValue(dict, TomlString(keyChars), value);
        }
        return result;
    }

    void require(size_t count) const {
        if(count > bytes.size() - pos)
            throw std::runtime_error("truncated KBAD data");
    }

    const std::vector<std::uint8_t> &bytes;
    size_t pos = 0;
};

tTJSVariant decodeKbadObject(const std::vector<std::uint8_t> &bytes) {
    KbadDecoder decoder(bytes);
    return decoder.decode();
}

struct LoadedTomlText {
    TomlString text;
    ttstr placed;
    bool kbad = false;
    tTJSVariant kbadValue;
};

LoadedTomlText loadTomlText(const ttstr &filename) {
    LoadedTomlText loaded;
    loaded.placed = TVPGetPlacedPath(filename);
    std::vector<std::uint8_t> bytes = readStorageBytes(loaded.placed);
    if(isKbadTomlBytes(bytes)) {
        loaded.kbad = true;
        loaded.kbadValue = decodeKbadObject(bytes);
        return loaded;
    }

    std::unique_ptr<iTJSTextReadStream> stream(TVPCreateTextStreamForRead(
        loaded.placed, TJS_W("")));
    ttstr text;
    stream->Read(text, 0);
    loaded.text = TomlString(text.c_str(), text.GetLen());
    return loaded;
}

bool tomlTraceEnabled() {
    const char *value = std::getenv("AETHERKIRI_TOML_TRACE");
    return value && *value && *value != '0';
}

size_t objectKeyCount(iTJSDispatch2 *object) {
    if(!object)
        return 0;
    struct Counter : public tTJSDispatch {
        tjs_uint RefCount = 1;
        size_t Count = 0;

        tjs_uint AddRef() override { return ++RefCount; }
        tjs_uint Release() override {
            if(--RefCount == 0) {
                delete this;
                return 0;
            }
            return RefCount;
        }
        tjs_error FuncCall(tjs_uint32, const tjs_char *, tjs_uint32 *,
                           tTJSVariant *result, tjs_int numparams,
                           tTJSVariant **, iTJSDispatch2 *) override {
            if(numparams > 0)
                ++Count;
            if(result)
                *result = static_cast<tjs_int>(1);
            return TJS_S_OK;
        }
    };

    Counter *counter = new Counter();
    tTJSVariantClosure closure(counter);
    object->EnumMembers(TJS_IGNOREPROP, &closure, object);
    const size_t result = counter->Count;
    closure.Release();
    return result;
}

std::string variantPreview(const tTJSVariant &value) {
    if(value.Type() == tvtVoid)
        return "<void>";
    if(value.Type() == tvtObject)
        return "<object>";
    return ttstr(value.AsStringNoAddRef()).AsStdString();
}

void traceTomlResult(const ttstr &filename, const ttstr &placed,
                     const TomlString &text, const tTJSVariant &parsed) {
    if(!tomlTraceEnabled())
        return;

    iTJSDispatch2 *root = parsed.AsObjectNoAddRef();
    tTJSVariant textsValue;
    tTJSVariant commonBackValue;
    iTJSDispatch2 *texts = nullptr;
    if(root &&
       TJS_SUCCEEDED(root->PropGet(TJS_IGNOREPROP, TJS_W("texts"), nullptr,
                                   &textsValue, root)) &&
       textsValue.Type() == tvtObject) {
        texts = textsValue.AsObjectNoAddRef();
        texts->PropGet(TJS_IGNOREPROP, TJS_W("common_back_title"), nullptr,
                       &commonBackValue, texts);
    }

    spdlog::info(
        "toml decode storage={} placed={} chars={} root_keys={} texts_keys={} common_back_title={}",
        filename.AsStdString(), placed.AsStdString(), text.size(),
        objectKeyCount(root), objectKeyCount(texts),
        variantPreview(commonBackValue));
}

class TomlParser {
public:
    explicit TomlParser(TomlString source) :
        source(std::move(source)), root(makeDictionary()),
        current(root.AsObjectNoAddRef()) {}

    tTJSVariant parse() {
        TomlString statement;
        int depth = 0;
        bool pending = false;

        size_t start = 0;
        while(start <= source.size()) {
            size_t end = source.find(TJS_W('\n'), start);
            if(end == TomlString::npos)
                end = source.size();
            TomlString line = source.substr(start, end - start);
            line = stripComment(line);

            if(!pending) {
                statement = trim(line);
            } else {
                statement += TJS_W('\n');
                statement += line;
            }

            depth = nestingDepth(statement);
            pending = depth > 0;
            if(!pending) {
                parseStatement(trim(statement));
                statement.clear();
            }

            if(end == source.size())
                break;
            start = end + 1;
        }

        if(!trim(statement).empty())
            parseStatement(trim(statement));
        return root;
    }

private:
    TomlString stripComment(const TomlString &line) const {
        bool inBasic = false;
        bool inLiteral = false;
        for(size_t i = 0; i < line.size(); ++i) {
            const tjs_char ch = line[i];
            if(inBasic) {
                if(ch == TJS_W('\\') && i + 1 < line.size()) {
                    ++i;
                } else if(ch == TJS_W('"')) {
                    inBasic = false;
                }
                continue;
            }
            if(inLiteral) {
                if(ch == TJS_W('\''))
                    inLiteral = false;
                continue;
            }
            if(ch == TJS_W('"')) {
                inBasic = true;
            } else if(ch == TJS_W('\'')) {
                inLiteral = true;
            } else if(ch == TJS_W('#')) {
                return line.substr(0, i);
            }
        }
        return line;
    }

    int nestingDepth(const TomlString &text) const {
        bool inBasic = false;
        bool inLiteral = false;
        int depth = 0;
        for(size_t i = 0; i < text.size(); ++i) {
            const tjs_char ch = text[i];
            if(inBasic) {
                if(ch == TJS_W('\\') && i + 1 < text.size()) {
                    ++i;
                } else if(ch == TJS_W('"')) {
                    inBasic = false;
                }
                continue;
            }
            if(inLiteral) {
                if(ch == TJS_W('\''))
                    inLiteral = false;
                continue;
            }
            if(ch == TJS_W('"')) {
                inBasic = true;
            } else if(ch == TJS_W('\'')) {
                inLiteral = true;
            } else if(ch == TJS_W('[') || ch == TJS_W('{')) {
                ++depth;
            } else if((ch == TJS_W(']') || ch == TJS_W('}')) && depth > 0) {
                --depth;
            }
        }
        return depth;
    }

    void parseStatement(const TomlString &statement) {
        if(statement.empty())
            return;

        if(statement.front() == TJS_W('[') && statement.back() == TJS_W(']')) {
            TomlString table = trim(statement.substr(1, statement.size() - 2));
            current = ensureTable(parseKeyPath(table)).AsObjectNoAddRef();
            return;
        }

        const size_t eq = findTopLevelEquals(statement);
        if(eq == TomlString::npos)
            return;

        auto keyPath = parseKeyPath(trim(statement.substr(0, eq)));
        if(keyPath.empty())
            return;

        iTJSDispatch2 *target = current;
        for(size_t i = 0; i + 1 < keyPath.size(); ++i) {
            tTJSVariant child = ensureChildDictionary(target, keyPath[i]);
            target = child.AsObjectNoAddRef();
        }

        TomlString valueText = trim(statement.substr(eq + 1));
        TomlParser valueParser(valueText);
        tTJSVariant value = valueParser.parseValueOnly();
        setDictionaryValue(target, keyPath.back(), value);
    }

    tTJSVariant parseValueOnly() {
        pos = 0;
        return parseValue();
    }

    size_t findTopLevelEquals(const TomlString &text) const {
        bool inBasic = false;
        bool inLiteral = false;
        int depth = 0;
        for(size_t i = 0; i < text.size(); ++i) {
            const tjs_char ch = text[i];
            if(inBasic) {
                if(ch == TJS_W('\\') && i + 1 < text.size()) {
                    ++i;
                } else if(ch == TJS_W('"')) {
                    inBasic = false;
                }
                continue;
            }
            if(inLiteral) {
                if(ch == TJS_W('\''))
                    inLiteral = false;
                continue;
            }
            if(ch == TJS_W('"')) {
                inBasic = true;
            } else if(ch == TJS_W('\'')) {
                inLiteral = true;
            } else if(ch == TJS_W('[') || ch == TJS_W('{')) {
                ++depth;
            } else if((ch == TJS_W(']') || ch == TJS_W('}')) && depth > 0) {
                --depth;
            } else if((ch == TJS_W('=') || ch == TJS_W(':')) && depth == 0) {
                return i;
            }
        }
        return TomlString::npos;
    }

    std::vector<TomlString> parseKeyPath(const TomlString &text) {
        std::vector<TomlString> keys;
        size_t index = 0;
        while(index < text.size()) {
            skipSpaces(text, index);
            if(index >= text.size())
                break;

            TomlString key;
            if(text[index] == TJS_W('"') || text[index] == TJS_W('\'')) {
                key = parseQuotedKey(text, index);
            } else {
                const size_t begin = index;
                while(index < text.size() && text[index] != TJS_W('.'))
                    ++index;
                key = trim(text.substr(begin, index - begin));
            }
            keys.push_back(key);
            skipSpaces(text, index);
            if(index < text.size() && text[index] == TJS_W('.')) {
                ++index;
            } else {
                break;
            }
        }
        return keys;
    }

    TomlString parseQuotedKey(const TomlString &text, size_t &index) {
        const tjs_char quote = text[index++];
        TomlString result;
        while(index < text.size()) {
            tjs_char ch = text[index++];
            if(ch == quote)
                break;
            if(quote == TJS_W('"') && ch == TJS_W('\\') &&
               index < text.size()) {
                result += parseEscape(text, index);
            } else {
                result += ch;
            }
        }
        return result;
    }

    tTJSVariant ensureTable(const std::vector<TomlString> &path) {
        iTJSDispatch2 *dict = root.AsObjectNoAddRef();
        tTJSVariant currentValue = root;
        for(const auto &key : path) {
            currentValue = ensureChildDictionary(dict, key);
            dict = currentValue.AsObjectNoAddRef();
        }
        return currentValue;
    }

    tTJSVariant ensureChildDictionary(iTJSDispatch2 *dict,
                                      const TomlString &key) {
        tTJSVariant child;
        if(dict && TJS_SUCCEEDED(dict->PropGet(TJS_IGNOREPROP, key.c_str(),
                                               nullptr, &child, dict)) &&
           child.Type() == tvtObject && child.AsObjectNoAddRef()) {
            return child;
        }
        child = makeDictionary();
        setDictionaryValue(dict, key, child);
        return child;
    }

    static void skipSpaces(const TomlString &text, size_t &index) {
        while(index < text.size() && isSpace(text[index]))
            ++index;
    }

    void skipSpaces() { skipSpaces(source, pos); }

    bool consume(tjs_char ch) {
        skipSpaces();
        if(pos >= source.size() || source[pos] != ch)
            return false;
        ++pos;
        return true;
    }

    tTJSVariant parseValue() {
        skipSpaces();
        if(pos >= source.size())
            return tTJSVariant();
        const tjs_char ch = source[pos];
        if(ch == TJS_W('"'))
            return tTJSVariant(parseBasicString());
        if(ch == TJS_W('\''))
            return tTJSVariant(parseLiteralString());
        if(ch == TJS_W('['))
            return parseArray();
        if(ch == TJS_W('{'))
            return parseInlineTable();
        if(ch == TJS_W('+') || ch == TJS_W('-') || isDigit(ch))
            return parseNumber();
        if(matchWord(TJS_W("true")))
            return tTJSVariant(true);
        if(matchWord(TJS_W("false")))
            return tTJSVariant(false);
        if(matchWord(TJS_W("void")) || matchWord(TJS_W("null")))
            return tTJSVariant();
        return tTJSVariant(parseBareString());
    }

    bool matchWord(const tjs_char *word) {
        skipSpaces();
        const size_t begin = pos;
        for(size_t i = 0; word[i]; ++i) {
            if(begin + i >= source.size() || source[begin + i] != word[i])
                return false;
        }
        const size_t end = begin + TJS_strlen(word);
        if(end < source.size()) {
            const tjs_char next = source[end];
            if((next >= TJS_W('a') && next <= TJS_W('z')) ||
               (next >= TJS_W('A') && next <= TJS_W('Z')) ||
               (next >= TJS_W('0') && next <= TJS_W('9')) ||
               next == TJS_W('_')) {
                return false;
            }
        }
        pos = end;
        return true;
    }

    tjs_char parseEscape(const TomlString &text, size_t &index) {
        const tjs_char esc = text[index++];
        switch(esc) {
        case TJS_W('b'): return static_cast<tjs_char>(0x08);
        case TJS_W('t'): return TJS_W('\t');
        case TJS_W('n'): return TJS_W('\n');
        case TJS_W('f'): return static_cast<tjs_char>(0x0c);
        case TJS_W('r'): return TJS_W('\r');
        case TJS_W('"'): return TJS_W('"');
        case TJS_W('\\'): return TJS_W('\\');
        case TJS_W('u'): {
            tjs_int value = 0;
            for(int i = 0; i < 4 && index < text.size(); ++i) {
                const tjs_int digit = hexValue(text[index++]);
                if(digit < 0)
                    break;
                value = (value << 4) | digit;
            }
            return static_cast<tjs_char>(value);
        }
        default: return esc;
        }
    }

    ttstr parseBasicString() {
        ++pos;
        TomlString result;
        while(pos < source.size()) {
            tjs_char ch = source[pos++];
            if(ch == TJS_W('"'))
                break;
            if(ch == TJS_W('\\') && pos < source.size()) {
                result += parseEscape(source, pos);
            } else {
                result += ch;
            }
        }
        return ttstr(result);
    }

    ttstr parseLiteralString() {
        ++pos;
        TomlString result;
        while(pos < source.size()) {
            tjs_char ch = source[pos++];
            if(ch == TJS_W('\''))
                break;
            result += ch;
        }
        return ttstr(result);
    }

    ttstr parseBareString() {
        const size_t begin = pos;
        while(pos < source.size() && source[pos] != TJS_W(',') &&
              source[pos] != TJS_W(']') && source[pos] != TJS_W('}')) {
            ++pos;
        }
        return ttstr(trim(source.substr(begin, pos - begin)));
    }

    tTJSVariant parseNumber() {
        const size_t begin = pos;
        if(pos < source.size() &&
           (source[pos] == TJS_W('+') || source[pos] == TJS_W('-'))) {
            ++pos;
        }

        bool negative = false;
        if(source[begin] == TJS_W('-'))
            negative = true;

        if(pos + 1 < source.size() && source[pos] == TJS_W('0') &&
           (source[pos + 1] == TJS_W('x') || source[pos + 1] == TJS_W('X'))) {
            pos += 2;
            tTVInteger value = 0;
            while(pos < source.size() && isHexDigit(source[pos])) {
                value = (value << 4) + hexValue(source[pos++]);
            }
            return tTJSVariant(negative ? -value : value);
        }

        bool real = false;
        while(pos < source.size() && isDigit(source[pos]))
            ++pos;
        if(pos < source.size() && source[pos] == TJS_W('.')) {
            real = true;
            ++pos;
            while(pos < source.size() && isDigit(source[pos]))
                ++pos;
        }
        if(pos < source.size() &&
           (source[pos] == TJS_W('e') || source[pos] == TJS_W('E'))) {
            real = true;
            ++pos;
            if(pos < source.size() &&
               (source[pos] == TJS_W('+') || source[pos] == TJS_W('-'))) {
                ++pos;
            }
            while(pos < source.size() && isDigit(source[pos]))
                ++pos;
        }

        TomlString number = source.substr(begin, pos - begin);
        if(real)
            return tTJSVariant(
                static_cast<tTVReal>(TJSStringToReal(number.c_str())));
        return tTJSVariant(static_cast<tTVInteger>(ttstr(number).AsInteger()));
    }

    tTJSVariant parseArray() {
        consume(TJS_W('['));
        tTJSVariant result = makeArray();
        iTJSDispatch2 *array = result.AsObjectNoAddRef();
        tjs_int index = 0;
        while(pos < source.size()) {
            skipSpaces();
            if(consume(TJS_W(']')))
                break;
            tTJSVariant value = parseValue();
            array->PropSetByNum(TJS_MEMBERENSURE, index++, &value, array);
            skipSpaces();
            consume(TJS_W(','));
        }
        return result;
    }

    tTJSVariant parseInlineTable() {
        consume(TJS_W('{'));
        tTJSVariant result = makeDictionary();
        iTJSDispatch2 *dict = result.AsObjectNoAddRef();
        while(pos < source.size()) {
            skipSpaces();
            if(consume(TJS_W('}')))
                break;
            TomlString key = parseInlineKey();
            skipSpaces();
            if(pos < source.size() &&
               (source[pos] == TJS_W('=') || source[pos] == TJS_W(':'))) {
                ++pos;
            }
            tTJSVariant value = parseValue();
            setDictionaryValue(dict, key, value);
            skipSpaces();
            consume(TJS_W(','));
        }
        return result;
    }

    TomlString parseInlineKey() {
        skipSpaces();
        if(pos < source.size() &&
           (source[pos] == TJS_W('"') || source[pos] == TJS_W('\''))) {
            return parseQuotedKey(source, pos);
        }
        const size_t begin = pos;
        while(pos < source.size() && source[pos] != TJS_W('=') &&
              source[pos] != TJS_W(':') && source[pos] != TJS_W(',') &&
              source[pos] != TJS_W('}')) {
            ++pos;
        }
        return trim(source.substr(begin, pos - begin));
    }

    TomlString source;
    size_t pos = 0;
    tTJSVariant root;
    iTJSDispatch2 *current = nullptr;
};

class ScriptsToml {
public:
    static tjs_error tomlDecode(tTJSVariant *result, tjs_int numparams,
                                tTJSVariant **param, iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        TomlString text(param[0]->GetString(),
                        TJS_strlen(param[0]->GetString()));
        TomlParser parser(std::move(text));
        tTJSVariant parsed = parser.parse();
        traceTomlResult(TJS_W("<inline>"), TJS_W("<inline>"),
                        TomlString(param[0]->GetString(),
                                   TJS_strlen(param[0]->GetString())),
                        parsed);
        if(result)
            *result = parsed;
        return TJS_S_OK;
    }

    static tjs_error tomlDecodeFromStorage(tTJSVariant *result,
                                           tjs_int numparams,
                                           tTJSVariant **param,
                                           iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        ttstr filename(param[0]->GetString());
        LoadedTomlText loaded = loadTomlText(filename);
        if(loaded.kbad) {
            if(result)
                *result = loaded.kbadValue;
            if(result)
                traceTomlResult(filename, loaded.placed, TJS_W("<KBAD>"),
                                *result);
            return TJS_S_OK;
        }
        TomlParser parser(loaded.text);
        if(result)
            *result = parser.parse();
        if(result)
            traceTomlResult(filename, loaded.placed, loaded.text, *result);
        return TJS_S_OK;
    }
};

} // namespace

NCB_ATTACH_CLASS(ScriptsToml, Scripts) {
    RawCallback(TJS_W("tomlDecode"), &ScriptsToml::tomlDecode,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("tomlDecodeFromStorage"),
                &ScriptsToml::tomlDecodeFromStorage, TJS_STATICMEMBER);
    RawCallback(TJS_W("evalTOML"), &ScriptsToml::tomlDecode,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("evalTOMLStorage"),
                &ScriptsToml::tomlDecodeFromStorage, TJS_STATICMEMBER);
}

extern "C" void TVPRegisterTomlPluginAnchor() {}
