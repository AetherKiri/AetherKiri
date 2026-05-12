#include "ncbind.hpp"
#include "tp_stub.h"

#include <array>
#include <string>
#include <vector>

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

#define NCB_MODULE_NAME TJS_W("base64.dll")

class Base64 {
    static constexpr char kTable[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    static void encodeBlock(const tjs_uint8 *input, tjs_int size,
                            std::string &output) {
        tjs_int i = 0;
        for(; i + 2 < size; i += 3) {
            output.push_back(kTable[(input[i] >> 2) & 0x3f]);
            output.push_back(
                kTable[((input[i] << 4) & 0x30) | ((input[i + 1] >> 4) & 0x0f)]);
            output.push_back(
                kTable[((input[i + 1] << 2) & 0x3c) | ((input[i + 2] >> 6) & 0x03)]);
            output.push_back(kTable[input[i + 2] & 0x3f]);
        }

        switch(size - i) {
            case 2:
                output.push_back(kTable[(input[i] >> 2) & 0x3f]);
                output.push_back(
                    kTable[((input[i] << 4) & 0x30) | ((input[i + 1] >> 4) & 0x0f)]);
                output.push_back(kTable[(input[i + 1] << 2) & 0x3c]);
                output.push_back('=');
                break;
            case 1:
                output.push_back(kTable[(input[i] >> 2) & 0x3f]);
                output.push_back(kTable[(input[i] << 4) & 0x30]);
                output.push_back('=');
                output.push_back('=');
                break;
            default:
                break;
        }
    }

    static int decodeValue(tjs_char ch) {
        if(ch >= TJS_W('A') && ch <= TJS_W('Z'))
            return ch - TJS_W('A');
        if(ch >= TJS_W('a') && ch <= TJS_W('z'))
            return ch - TJS_W('a') + 26;
        if(ch >= TJS_W('0') && ch <= TJS_W('9'))
            return ch - TJS_W('0') + 52;
        if(ch == TJS_W('+'))
            return 62;
        if(ch == TJS_W('/'))
            return 63;
        return -1;
    }

    static bool isIgnored(tjs_char ch) {
        return ch == TJS_W('\r') || ch == TJS_W('\n') || ch == TJS_W('\t') ||
               ch == TJS_W(' ');
    }

    static std::vector<tjs_uint8> decodeString(const tjs_char *data,
                                               tjs_int length) {
        std::vector<tjs_uint8> output;
        std::array<int, 4> quad{};
        int q = 0;

        for(tjs_int i = 0; i < length; ++i) {
            const tjs_char ch = data[i];
            if(isIgnored(ch))
                continue;

            if(ch == TJS_W('=')) {
                quad[q++] = -2;
            } else {
                const int value = decodeValue(ch);
                if(value < 0)
                    continue;
                quad[q++] = value;
            }

            if(q == 4) {
                output.push_back(static_cast<tjs_uint8>((quad[0] << 2) |
                                                        ((quad[1] & 0x30) >> 4)));
                if(quad[2] != -2) {
                    output.push_back(static_cast<tjs_uint8>(((quad[1] & 0x0f) << 4) |
                                                            ((quad[2] & 0x3c) >> 2)));
                }
                if(quad[3] != -2) {
                    output.push_back(static_cast<tjs_uint8>(((quad[2] & 0x03) << 6) |
                                                            quad[3]));
                }
                q = 0;
            }
        }

        return output;
    }

    static ttstr md5Hex(const std::vector<tjs_uint8> &data) {
        TVP_md5_state_t state;
        TVP_md5_init(&state);
        if(!data.empty())
            TVP_md5_append(&state, data.data(), static_cast<int>(data.size()));

        tjs_uint8 digest[16];
        TVP_md5_finish(&state, digest);

        static constexpr char hex[] = "0123456789abcdef";
        char buffer[33];
        for(int i = 0; i < 16; ++i) {
            buffer[i * 2] = hex[digest[i] >> 4];
            buffer[i * 2 + 1] = hex[digest[i] & 0x0f];
        }
        buffer[32] = '\0';
        return ttstr(buffer);
    }

public:
    static tjs_error TJS_INTF_METHOD encode(tTJSVariant *result,
                                            tjs_int numparams,
                                            tTJSVariant **param,
                                            iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        if(!result)
            return TJS_S_OK;

        ttstr filename = TVPGetPlacedPath(*param[0]);
        if(filename.IsEmpty()) {
            result->Clear();
            return TJS_S_OK;
        }

        tTJSBinaryStream *input = TVPCreateStream(filename, TJS_BS_READ);
        if(!input) {
            result->Clear();
            return TJS_S_OK;
        }

        std::string encoded;
        tjs_uint8 buffer[1024 * 12];
        tjs_uint read = 0;
        while((read = input->Read(buffer, sizeof(buffer))) > 0)
            encodeBlock(buffer, static_cast<tjs_int>(read), encoded);

        delete input;
        *result = encoded.c_str();
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD decode(tTJSVariant *result,
                                            tjs_int numparams,
                                            tTJSVariant **param,
                                            iTJSDispatch2 *) {
        if(numparams < 2)
            return TJS_E_BADPARAMCOUNT;

        ttstr source = param[0]->AsStringNoAddRef();
        const ttstr filename = param[1]->AsStringNoAddRef();
        if(filename.IsEmpty())
            TVPThrowExceptionMessage(TJS_W("no filename"));

        std::vector<tjs_uint8> decoded =
            decodeString(source.c_str(), static_cast<tjs_int>(source.length()));

        tTJSBinaryStream *output = TVPCreateStream(filename, TJS_BS_WRITE);
        if(!output)
            TVPThrowExceptionMessage((ttstr(TJS_W("can't open writefile: ")) +
                                      filename)
                                         .c_str());

        if(!decoded.empty())
            output->Write(decoded.data(), static_cast<tjs_uint>(decoded.size()));
        delete output;

        if(result)
            *result = md5Hex(decoded);
        return TJS_S_OK;
    }
};

NCB_REGISTER_CLASS(Base64) {
    RawCallback("encode", &Class::encode, 0);
    RawCallback("decode", &Class::decode, 0);
}
