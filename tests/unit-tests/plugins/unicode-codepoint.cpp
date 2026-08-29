#include <catch2/catch_test_macros.hpp>

#include "CharacterSet.h"

TEST_CASE("UTF-16 code-point reader combines valid surrogate pairs") {
    const tjs_string text = TJS_W("A\U0001F600Z");
    tjs_uint32 codepoint = 0;
    tjs_size consumed = 0;

    REQUIRE(TVPReadUtf16CodePoint(text.data(), text.size(), codepoint,
                                  consumed));
    CHECK(codepoint == static_cast<tjs_uint32>('A'));
    CHECK(consumed == 1);

    REQUIRE(TVPReadUtf16CodePoint(text.data() + consumed,
                                  text.size() - consumed, codepoint,
                                  consumed));
    CHECK(codepoint == 0x1F600);
    CHECK(consumed == 2);

    REQUIRE(TVPReadUtf16CodePoint(text.data() + 3, 1, codepoint, consumed));
    CHECK(codepoint == static_cast<tjs_uint32>('Z'));
    CHECK(consumed == 1);
}
TEST_CASE("UTF-16 code-point reader preserves bounded lone surrogates") {
    const tjs_char high[] = {static_cast<tjs_char>(0xD83D),
                             static_cast<tjs_char>('x')};
    tjs_uint32 codepoint = 0;
    tjs_size consumed = 0;

    REQUIRE(TVPReadUtf16CodePoint(high, 1, codepoint, consumed));
    CHECK(codepoint == 0xD83D);
    CHECK(consumed == 1);

    REQUIRE(TVPReadUtf16CodePoint(high, 2, codepoint, consumed));
    CHECK(codepoint == 0xD83D);
    CHECK(consumed == 1);

    CHECK_FALSE(TVPReadUtf16CodePoint(nullptr, 1, codepoint, consumed));
    CHECK(consumed == 0);
}
