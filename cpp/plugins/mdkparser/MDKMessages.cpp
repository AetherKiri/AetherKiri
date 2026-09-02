// The upstream project obtains these strings from a Windows .rc resource.
// Keep the same message indexes while providing a resource-free implementation
// for macOS, iOS, and other platforms supported by AetherKiri.

#include "tp_stub.h"
#include "MDKMessages.h"

namespace AetherKiri::MDKParser {

namespace {

constexpr const tjs_char *kMessages[NUM_MDK_MESSAGE_MAX] = {
    TJS_W("Comment is not terminated."),
    TJS_W("String, regexp, or octet literal is not terminated."),
    TJS_W("Insufficient memory."),
    TJS_W("Cannot be interpreted as a numeric value."),
    TJS_W("Invalid character '%1'"),
    TJS_W("A numeric value is at the beginning of a line, but it is not "
           "interpreted as a choice due to lack of '.'"),
    TJS_W("Internal error at %1 line %2"),
    TJS_W("'%c' is already registered."),
    TJS_W("'%c' cannot be registered."),
    TJS_W("Redundant parameters."),
    TJS_W("Redundant properties."),
    TJS_W("File description defined in property of '%1' is incorrect. "
           "\".\" must be followed by a string."),
    TJS_W("File description defined in property of '%1' is incorrect. "
           "\"::\" must be followed by a string."),
    TJS_W("Reference defined in property of '%1' is incorrect. "
           "\".\" must be followed by a string."),
    TJS_W("Reference defined in property of '%1' is incorrect. "
           "\"::\" must be followed by a string."),
    TJS_W("Property of '%1' is set to a value other than \"+\". "
           "Cannot be interpreted as a numberic value."),
    TJS_W("Property of '%1' is set to a value other than \"-\". "
           "Cannot be interpreted as a numberic value."),
    TJS_W("'<' is followed by a non-numeric value."),
    TJS_W("'<' is not closed by '>' in tag."),
    TJS_W("'{' is followed by a non-numeric value in tag."),
    TJS_W("'{' is not closed by '}' in tag."),
    TJS_W("'(' is followed by a non-numeric value."),
    TJS_W("'(' is not closed by ')' in tag."),
    TJS_W("$ is followed by values that cannot be interpreted as parameter "
           "names."),
    TJS_W("Text decoration is closed by ']'."),
    TJS_W("Uninterpretable symbol used in tag."),
    TJS_W("@ is followed by a string that cannot be interpreted as a "
           "character name."),
    TJS_W("# is followed by a string that cannot be interpreted as a label."),
    TJS_W("'|' is not found after '*' in a choice."),
    TJS_W("'>' is not followed by an if condition."),
    TJS_W("'>' is followed by an uninterpretable string."),
    TJS_W("'\u300A' does not have '|' in the front, cannot interprete as a "
           "ruby character"),
    TJS_W("'\u300B' is not found after '\u300A', cannot interprete as a ruby "
           "characters."),
    TJS_W("'{' does not have '|' in the front, cannot interprete as text "
           "decoration."),
    TJS_W("':(' is not followed by a image file name, or ')' is missing."),
    TJS_W("':' is not followed by an emoji, or ':' is missing."),
    TJS_W("Unknown syntax."),
    TJS_W("End of file reached with tag unclosed."),
};

} // namespace

ttstr TVPMdkGetText(int num) {
    if(num < 0 || num >= NUM_MDK_MESSAGE_MAX)
        return ttstr(TJS_W("Internal Error."));
    return ttstr(kMessages[num]);
}

} // namespace AetherKiri::MDKParser
