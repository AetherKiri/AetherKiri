//---------------------------------------------------------------------------
// Optional recovery for a narrow class of malformed text scripts.
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include "tjsScriptRecovery.h"
#include "tjsScriptBlock.h"

namespace TJS {
    namespace {
        bool RecoveryEnabled() {
            const char *value = std::getenv("AETHERKIRI_TJS_TRAILING_RECOVERY");
            return value == nullptr || value[0] == '\0' || value[0] != '0';
        }

        bool IsTokenChar(tjs_char ch) {
            return (ch >= TJS_W('a') && ch <= TJS_W('z')) ||
                   (ch >= TJS_W('A') && ch <= TJS_W('Z')) ||
                   (ch >= TJS_W('0') && ch <= TJS_W('9')) ||
                   ch == TJS_W('_') || ch == TJS_W('$');
        }
    } // namespace

    bool TJSExperimentalRecoverTrailingToken(tTJS *engine,
                                              const ttstr &source,
                                              tjs_int error_position,
                                              ttstr &recovered) {
        if(!engine || !RecoveryEnabled())
            return false;

        const tjs_int length = source.GetLen();
        if(length <= 0)
            return false;

        tjs_int end = length;
        while(end > 0 && (source[end - 1] == TJS_W(' ') ||
                          source[end - 1] == TJS_W('\t') ||
                          source[end - 1] == TJS_W('\r') ||
                          source[end - 1] == TJS_W('\n'))) {
            --end;
        }
        if(end <= 0 || !IsTokenChar(source[end - 1]))
            return false;

        tjs_int start = end;
        while(start > 0 && IsTokenChar(source[start - 1]))
            --start;
        if(start == end || end - start > 32)
            return false;

        // The parser must have reported the error at the candidate token or
        // in the whitespace immediately following it. This prevents a
        // valid-looking suffix from being removed to hide an unrelated
        // syntax error earlier in a file.
        if(error_position < start || error_position > length)
            return false;

        ttstr candidate = source.SubString(0, static_cast<unsigned int>(start));
        if(candidate.GetLen() == 0)
            return false;

        auto *probe = new tTJSScriptBlock(engine);
        try {
            // Parse only. The candidate is executed later by the caller, so
            // no partially recovered context can leak into the real runtime.
            probe->Parse(candidate.c_str(), false, false);
            probe->Release();
            recovered = candidate;
            return true;
        } catch(...) {
            probe->Release();
            return false;
        }
    }
} // namespace TJS
