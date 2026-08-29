//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Character code conversion
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include "CharacterSet.h"
#include "MsgIntf.h"

#include <cstring>
#include <string>

namespace {
bool TVPDecodeUtf8(const std::string &input, tjs_string &output);
bool TVPEncodeUtf16(const tjs_string &input, std::string &output);
} // namespace

//---------------------------------------------------------------------------
tjs_int TVPWideCharToUtf8String(const tjs_char *in, char *out) {
    if(!in)
        return -1;
    // Decode/encode through the same surrogate-aware implementation used by
    // DAP and REPL adapters.  The old byte-at-a-time routine emitted lone
    // UTF-16 surrogates and accepted obsolete 5/6-byte UTF-8 sequences, which
    // made source paths fail to round-trip on modern platforms.
    const tjs_string input(in);
    std::string encoded;
    if(!TVPEncodeUtf16(input, encoded))
        return -1;
    if(out && !encoded.empty())
        std::memcpy(out, encoded.data(), encoded.size());
    return static_cast<tjs_int>(encoded.size());
}

// Length-bounded counterpart retained for krkrz's stream and logging leaves.
// The historical contract stops at the first NUL even when a larger bound is
// supplied, while still rejecting a truncated/lone UTF-16 surrogate.
tjs_int TVPWideCharToUtf8String(const tjs_char *in, tjs_uint length,
                                char *out) {
    if(!in)
        return -1;
    size_t bounded = 0;
    while(bounded < static_cast<size_t>(length) && in[bounded] != '\0')
        ++bounded;
    const tjs_string input(in, bounded);
    std::string encoded;
    if(!TVPEncodeUtf16(input, encoded))
        return -1;
    if(out && !encoded.empty())
        std::memcpy(out, encoded.data(), encoded.size());
    return static_cast<tjs_int>(encoded.size());
}

//---------------------------------------------------------------------------
tjs_int TVPUtf8ToWideCharString(const char *in, tjs_char *out) {
    if(!in)
        return -1;
    tjs_string decoded;
    if(!TVPDecodeUtf8(std::string(in), decoded))
        return -1;
    if(out && !decoded.empty())
        std::memcpy(out, decoded.data(), decoded.size() * sizeof(tjs_char));
    return static_cast<tjs_int>(decoded.size());
}

//---------------------------------------------------------------------------
tjs_int TVPUtf8ToWideCharString(const char *in, tjs_uint length,
                                tjs_char *out) {
    if(!in)
        return -1;
    size_t bounded = 0;
    while(bounded < static_cast<size_t>(length) && in[bounded] != '\0')
        ++bounded;
    tjs_string decoded;
    if(!TVPDecodeUtf8(std::string(in, bounded), decoded))
        return -1;
    if(out && !decoded.empty())
        std::memcpy(out, decoded.data(), decoded.size() * sizeof(tjs_char));
    return static_cast<tjs_int>(decoded.size());
}

namespace {

bool TVPDecodeUtf8(const std::string &input, tjs_string &output) {
    output.clear();
    output.reserve(input.size());
    for(size_t i = 0; i < input.size();) {
        const auto byte = static_cast<unsigned char>(input[i]);
        tjs_uint32 codepoint = 0;
        size_t width = 0;
        if(byte <= 0x7f) {
            codepoint = byte;
            width = 1;
        } else if(byte >= 0xc2 && byte <= 0xdf) {
            codepoint = byte & 0x1f;
            width = 2;
        } else if(byte >= 0xe0 && byte <= 0xef) {
            codepoint = byte & 0x0f;
            width = 3;
        } else if(byte >= 0xf0 && byte <= 0xf4) {
            codepoint = byte & 0x07;
            width = 4;
        } else {
            return false;
        }
        if(i + width > input.size())
            return false;
        for(size_t j = 1; j < width; ++j) {
            const auto continuation = static_cast<unsigned char>(input[i + j]);
            if((continuation & 0xc0) != 0x80)
                return false;
            codepoint = (codepoint << 6) | (continuation & 0x3f);
        }
        if((width == 2 && codepoint < 0x80) ||
           (width == 3 && codepoint < 0x800) ||
           (width == 4 && codepoint < 0x10000) ||
           codepoint > 0x10ffff ||
           (codepoint >= 0xd800 && codepoint <= 0xdfff))
            return false;
        if(codepoint <= 0xffff) {
            output.push_back(static_cast<tjs_char>(codepoint));
        } else {
            codepoint -= 0x10000;
            output.push_back(static_cast<tjs_char>(0xd800 + (codepoint >> 10)));
            output.push_back(static_cast<tjs_char>(0xdc00 + (codepoint & 0x3ff)));
        }
        i += width;
    }
    return true;
}

bool TVPEncodeUtf16(const tjs_string &input, std::string &output) {
    output.clear();
    output.reserve(input.size());
    for(size_t i = 0; i < input.size(); ++i) {
        tjs_uint32 codepoint = input[i];
        if(codepoint >= 0xd800 && codepoint <= 0xdbff) {
            if(i + 1 >= input.size())
                return false;
            const tjs_uint32 low = input[++i];
            if(low < 0xdc00 || low > 0xdfff)
                return false;
            codepoint = 0x10000 + ((codepoint - 0xd800) << 10) +
                        (low - 0xdc00);
        } else if(codepoint >= 0xdc00 && codepoint <= 0xdfff) {
            return false;
        }
        if(codepoint <= 0x7f) {
            output.push_back(static_cast<char>(codepoint));
        } else if(codepoint <= 0x7ff) {
            output.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        } else if(codepoint <= 0xffff) {
            output.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        } else if(codepoint <= 0x10ffff) {
            output.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        } else {
            return false;
        }
    }
    return true;
}

} // namespace

bool TVPUtf8ToUtf16(tjs_string &out, const char *in) {
    if(!in) {
        out.clear();
        return false;
    }
    return TVPDecodeUtf8(std::string(in), out);
}

bool TVPUtf8ToUtf16(tjs_string &out, const std::string &in) {
    return TVPDecodeUtf8(in, out);
}

bool TVPUtf16ToUtf8(std::string &out, const tjs_char *in) {
    if(!in) {
        out.clear();
        return false;
    }
    return TVPEncodeUtf16(tjs_string(in), out);
}

bool TVPUtf16ToUtf8(std::string &out, const tjs_string &in) {
    return TVPEncodeUtf16(in, out);
}

bool TVPReadUtf16CodePoint(const tjs_char *text, tjs_size remaining,
                           tjs_uint32 &codepoint, tjs_size &consumed) {
    if(!text || remaining == 0) {
        codepoint = 0;
        consumed = 0;
        return false;
    }

    const tjs_uint32 first = static_cast<tjs_uint16>(text[0]);
    codepoint = first;
    consumed = 1;
    if(first < 0xD800 || first > 0xDBFF || remaining < 2)
        return true;

    const tjs_uint32 second = static_cast<tjs_uint16>(text[1]);
    if(second < 0xDC00 || second > 0xDFFF)
        return true;

    codepoint = 0x10000u + ((first - 0xD800u) << 10) +
        (second - 0xDC00u);
    consumed = 2;
    return true;
}
//---------------------------------------------------------------------------
