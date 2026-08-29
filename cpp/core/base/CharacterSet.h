//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Character code conversion
//---------------------------------------------------------------------------

#ifndef __CharacterSet_H__
#define __CharacterSet_H__

// various character conding conversion.
// currently only utf-8 related functions are implemented.
#include "tjsTypes.h"
#include <string>

TJS_EXP_FUNC_DEF(tjs_int, TVPWideCharToUtf8String,
                 (const tjs_char *in, char *out));

// Length-bounded variant used by binary/text stream adapters.  Keep this
// upstream ABI alongside the NUL-terminated overload; callers commonly use
// the first pass with a null output buffer to size an exact destination.
extern tjs_int TVPWideCharToUtf8String(const tjs_char *in, tjs_uint length,
                                       char *out);

TJS_EXP_FUNC_DEF(tjs_int, TVPUtf8ToWideCharString,
                 (const char *in, tjs_char *out));

extern tjs_int TVPUtf8ToWideCharString(const char *in, tjs_uint length,
                                       tjs_char *out);

// Modern krkrz leaves (DAP, REPL and other source-level adapters) use the
// owning string overloads.  Keep the legacy buffer API above untouched and
// expose these helpers on the same Aether ABI.
extern bool TVPUtf8ToUtf16(tjs_string &out, const char *in);
extern bool TVPUtf8ToUtf16(tjs_string &out, const std::string &in);
extern bool TVPUtf16ToUtf8(std::string &out, const tjs_char *in);
extern bool TVPUtf16ToUtf8(std::string &out, const tjs_string &in);

// Read one Unicode code point from a UTF-16 buffer.  A valid surrogate pair
// is combined and consumes two code units; an unpaired surrogate is returned
// as-is and consumes one unit so the caller can preserve the legacy tofu
// behaviour.  The helper is deliberately length-bounded: visual/layout code
// must not peek past a string terminator when processing the final code unit.
extern bool TVPReadUtf16CodePoint(const tjs_char *text, tjs_size remaining,
                                  tjs_uint32 &codepoint,
                                  tjs_size &consumed);

inline bool TVPIsUnicodeSpace(tjs_uint32 codepoint) {
    return (codepoint >= 0x0009 && codepoint <= 0x000D) ||
        codepoint == 0x0020 || codepoint == 0x0085 || codepoint == 0x00A0 ||
        codepoint == 0x1680 ||
        (codepoint >= 0x2000 && codepoint <= 0x200A) ||
        codepoint == 0x2028 || codepoint == 0x2029 ||
        codepoint == 0x202F || codepoint == 0x205F || codepoint == 0x3000;
}

inline bool TVPIsUnicodeDefaultIgnorable(tjs_uint32 codepoint) {
    return codepoint == 0x00AD ||
        (codepoint >= 0x200B && codepoint <= 0x200F) ||
        codepoint == 0x2060 ||
        (codepoint >= 0xFE00 && codepoint <= 0xFE0F) ||
        codepoint == 0xFEFF;
}

#endif
