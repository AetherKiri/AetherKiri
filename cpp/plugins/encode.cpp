#include "PluginStub.h"
#include "ncbind.hpp"
#include "tp_stub.h"

#include "e2u.h"
#include "hojo2u.h"
#include "u2e.h"

#include <algorithm>
#include <vector>

#define NCB_MODULE_NAME TJS_W("encode.dll")

namespace {

ttstr invalidEncodingMessage(const ttstr &encoding) {
    return encoding + TJS_W(" is not a supported encoding");
}

tTJSVariantOctet *makeOctet(const std::vector<tjs_uint8> &data) {
    return TJSAllocVariantOctet(data.empty() ? nullptr : data.data(),
                                static_cast<tjs_uint>(data.size()));
}

void ensureBytes(tjs_int index, tjs_int needed, tjs_int length,
                 const tjs_char *encoding) {
    if(index + needed >= length)
        TVPThrowExceptionMessage((ttstr(TJS_W("invalid ")) + encoding +
                                  TJS_W(" sequence"))
                                     .c_str());
}

tjs_char eucJp0208ToUnicode(tjs_uint8 hiByte, tjs_uint8 lowByte) {
    const int hi = hiByte & 0x7f;
    const int low = lowByte & 0x7f;
    if(32 <= hi && hi <= 127 && 32 <= low && low <= 127) {
        const int key = (hi - 32) * 96 + (low - 32);
        const unsigned short value = e2u_tbl[key];
        return value != 0 ? static_cast<tjs_char>(value) : TJS_W('?');
    }
    return TJS_W('?');
}

ttstr decodeUtf8(tTJSVariantOctet *octet) {
    if(!octet)
        return TJS_W("");

    const tjs_uint8 *data = octet->GetData();
    const tjs_int length = octet->GetLength();
    std::vector<tjs_char> chars;

    for(tjs_int i = 0; i < length; ++i) {
        if(data[i] <= 0x7f) {
            chars.push_back(data[i]);
        } else if(data[i] <= 0xdf) {
            ensureBytes(i, 1, length, TJS_W("UTF-8"));
            chars.push_back(((data[i] & 0x1f) << 6) | (data[i + 1] & 0x3f));
            i += 1;
        } else if(data[i] <= 0xef) {
            ensureBytes(i, 2, length, TJS_W("UTF-8"));
            chars.push_back(((data[i] & 0x0f) << 12) |
                            ((data[i + 1] & 0x3f) << 6) |
                            (data[i + 2] & 0x3f));
            i += 2;
        } else {
            TVPThrowExceptionMessage(TJS_W("invalid UTF-8 sequence"));
        }
    }

    chars.push_back(0);
    return ttstr(chars.data());
}

ttstr decodeEucJp(tTJSVariantOctet *octet) {
    if(!octet)
        return TJS_W("");

    const tjs_uint8 *euc = octet->GetData();
    const tjs_int length = octet->GetLength();
    std::vector<tjs_char> chars;

    for(tjs_int i = 0; i < length; ++i) {
        if(euc[i] < 0x80) {
            chars.push_back(euc[i]);
        } else if(euc[i] == 0x8e) {
            ensureBytes(i, 1, length, TJS_W("EUC-JP"));
            tjs_uint8 value = 0;
            if(euc[i + 1] >= 0xa1 && euc[i + 1] <= 0xdf)
                value = euc[i + 1] - 0x40;
            chars.push_back(0xff00 | value);
            ++i;
        } else if(euc[i] == 0x8f) {
            ensureBytes(i, 2, length, TJS_W("EUC-JP"));
            const int hi = euc[i + 1] & 0x7f;
            const int low = euc[i + 2] & 0x7f;
            unsigned short value = 0;
            if(32 <= hi && hi <= 127 && 32 <= low && low <= 127)
                value = hojo2u_tbl[(hi - 32) * 96 + (low - 32)];
            chars.push_back(value != 0 ? static_cast<tjs_char>(value)
                                       : TJS_W('?'));
            i += 2;
        } else if(euc[i] < 0xa0) {
            // Ignore C1 controls, matching the original plugin.
        } else {
            ensureBytes(i, 1, length, TJS_W("EUC-JP"));
            chars.push_back(eucJp0208ToUnicode(euc[i], euc[i + 1]));
            ++i;
        }
    }

    chars.push_back(0);
    return ttstr(chars.data());
}

ttstr decodeShiftJis(tTJSVariantOctet *octet) {
    if(!octet)
        return TJS_W("");

    const tjs_uint8 *sjis = octet->GetData();
    const tjs_int length = octet->GetLength();
    std::vector<tjs_char> chars;

    for(tjs_int i = 0; i < length; ++i) {
        const tjs_uint8 first = sjis[i];
        if(first < 0x80) {
            chars.push_back(first);
        } else if(0xa1 <= first && first <= 0xdf) {
            chars.push_back(0xff00 | (first - 0x40));
        } else {
            ensureBytes(i, 1, length, TJS_W("Shift_JIS"));
            const tjs_uint8 second = sjis[i + 1];
            const int adjusted = first < 0xa0 ? first - 0x81 : first - 0xc1;
            int row = adjusted * 2 + 0x21;
            int cell = 0;
            if(second >= 0x9f) {
                ++row;
                cell = second - 0x7e;
            } else {
                cell = second - (second >= 0x80 ? 0x20 : 0x1f);
            }
            chars.push_back(eucJp0208ToUnicode(row | 0x80, cell | 0x80));
            ++i;
        }
    }

    chars.push_back(0);
    return ttstr(chars.data());
}

tTJSVariantOctet *encodeUtf8(const ttstr &str) {
    std::vector<tjs_uint8> bytes;
    const tjs_int length = str.GetLen();
    for(tjs_int i = 0; i < length; ++i) {
        const tjs_char c = str[i];
        if(c < (1 << 7)) {
            bytes.push_back(static_cast<tjs_uint8>(c));
        } else if(c < (1 << 11)) {
            bytes.push_back(0xc0 | ((c >> 6) & 0x1f));
            bytes.push_back(0x80 | (c & 0x3f));
        } else {
            bytes.push_back(0xe0 | ((c >> 12) & 0x0f));
            bytes.push_back(0x80 | ((c >> 6) & 0x3f));
            bytes.push_back(0x80 | (c & 0x3f));
        }
    }
    return makeOctet(bytes);
}

tTJSVariantOctet *encodeEucJp(const ttstr &str) {
    std::vector<tjs_uint8> bytes;
    const tjs_int length = str.GetLen();
    for(tjs_int i = 0; i < length; ++i) {
        const unsigned short value = u2e_tbl[str[i]];
        if(value == 0) {
            bytes.push_back('?');
        } else if(value < 0x80) {
            bytes.push_back(static_cast<tjs_uint8>(value));
        } else if(value > 0xa0 && value <= 0xdf) {
            bytes.push_back(0x8e);
            bytes.push_back(value & 0xff);
        } else if(value >= 0x2121 && value <= 0x6d63) {
            bytes.push_back(0x8f);
            bytes.push_back((value >> 8) | 0x80);
            bytes.push_back((value & 0xff) | 0x80);
        } else if(value != 0xffff) {
            bytes.push_back(value >> 8);
            bytes.push_back(value & 0xff);
        }
    }
    return makeOctet(bytes);
}

tTJSVariantOctet *encodeShiftJis(const ttstr &str) {
    std::vector<tjs_uint8> bytes;
    const tjs_int length = str.GetLen();
    for(tjs_int i = 0; i < length; ++i) {
        const unsigned short value = u2e_tbl[str[i]];
        if(value == 0) {
            bytes.push_back('?');
        } else if(value < 0x80) {
            bytes.push_back(static_cast<tjs_uint8>(value));
        } else if(value > 0xa0 && value <= 0xdf) {
            bytes.push_back(value & 0xff);
        } else if(value != 0xffff && value >= 0xa1a1 && value <= 0xfefe) {
            const int row = (value >> 8) & 0x7f;
            const int cell = value & 0x7f;
            bytes.push_back(((row - 1) >> 1) + (row <= 0x5e ? 0x81 : 0xc1));
            bytes.push_back(cell + ((row & 1) != 0
                                        ? (cell <= 0x5f ? 0x1f : 0x20)
                                        : 0x7e));
        } else {
            bytes.push_back('?');
        }
    }
    return makeOctet(bytes);
}

class NI_Encode : public tTJSNativeInstance {
public:
    tjs_error Construct(tjs_int, tTJSVariant **, iTJSDispatch2 *) override {
        TVPThrowExceptionMessage(
            TJSGetMessageMapMessage(TJS_W("TVPCannotCreateInstance")).c_str());
        return TJS_E_FAIL;
    }

    static ttstr decode(tTJSVariant *data, const ttstr &encoding) {
        if(encoding == TJS_W("UTF-8"))
            return decodeUtf8(data->AsOctetNoAddRef());
        if(encoding == TJS_W("EUC-JP"))
            return decodeEucJp(data->AsOctetNoAddRef());
        if(encoding == TJS_W("Shift_JIS"))
            return decodeShiftJis(data->AsOctetNoAddRef());
        TVPThrowExceptionMessage(invalidEncodingMessage(encoding).c_str());
        return TJS_W("");
    }

    static tTJSVariantOctet *encode(const ttstr &str, const ttstr &encoding) {
        if(encoding == TJS_W("UTF-8"))
            return encodeUtf8(str);
        if(encoding == TJS_W("EUC-JP"))
            return encodeEucJp(str);
        if(encoding == TJS_W("Shift_JIS"))
            return encodeShiftJis(str);
        TVPThrowExceptionMessage(invalidEncodingMessage(encoding).c_str());
        return makeOctet({});
    }
};

iTJSNativeInstance *Create_NI_Encode() { return new NI_Encode(); }

void addGlobalClass(const tjs_char *name, iTJSDispatch2 *klass) {
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(!global)
        return;

    tTJSVariant value(klass);
    klass->Release();
    global->PropSet(TJS_MEMBERENSURE, name, nullptr, &value, global);
    global->Release();
}

} // namespace

#ifdef TJS_NATIVE_CLASSID_NAME
#undef TJS_NATIVE_CLASSID_NAME
#undef TJS_NCM_REG_THIS
#undef TJS_NATIVE_SET_ClassID
#endif
#define TJS_NCM_REG_THIS classobj
#define TJS_NATIVE_SET_ClassID TJS_NATIVE_CLASSID_NAME = TJS_NCM_CLASSID;
#define TJS_NATIVE_CLASSID_NAME ClassID_Encode
static tjs_int32 TJS_NATIVE_CLASSID_NAME = -1;

static iTJSDispatch2 *Create_NC_Encode() {
    tTJSNativeClassForPlugin *classobj =
        TJSCreateNativeClassForPlugin(TJS_W("Encode"), Create_NI_Encode);

    TJS_BEGIN_NATIVE_MEMBERS(Encode)

    TJS_DECL_EMPTY_FINALIZE_METHOD

    TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL_NO_INSTANCE(Encode) {
        return TJS_S_OK;
    }
    TJS_END_NATIVE_CONSTRUCTOR_DECL(Encode)

    TJS_BEGIN_NATIVE_METHOD_DECL(decode) {
        if(numparams < 2)
            return TJS_E_BADPARAMCOUNT;
        if(param[0]->Type() != tvtOctet || param[1]->Type() != tvtString)
            return TJS_E_INVALIDPARAM;

        if(result)
            *result = NI_Encode::decode(param[0], ttstr(*param[1]));
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(decode)

    TJS_BEGIN_NATIVE_METHOD_DECL(encode) {
        if(numparams < 2)
            return TJS_E_BADPARAMCOUNT;
        if(param[0]->Type() != tvtString || param[1]->Type() != tvtString)
            return TJS_E_INVALIDPARAM;

        if(result)
            *result = NI_Encode::encode(ttstr(*param[0]), ttstr(*param[1]));
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(encode)

    TJS_END_NATIVE_MEMBERS

    return classobj;
}

static void InitPlugin_Encode() {
    addGlobalClass(TJS_W("Encode"), Create_NC_Encode());
}

NCB_PRE_REGIST_CALLBACK(InitPlugin_Encode);
