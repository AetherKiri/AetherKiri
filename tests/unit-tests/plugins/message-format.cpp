#include <catch2/catch_test_macros.hpp>

#include "MsgIntf.h"

TEST_CASE("message formatting accepts positional string specifiers") {
    CHECK(TVPFormatMessage(TJS_W("font '%1$s'"), TJS_W("example.ttf")) ==
          TJS_W("font 'example.ttf'"));
    CHECK(TVPFormatMessage(TJS_W("%2$s then %1$s"), TJS_W("first"),
                           TJS_W("second")) == TJS_W("second then first"));
}

TEST_CASE("message formatting preserves legacy placeholders and percent") {
    CHECK(TVPFormatMessage(TJS_W("%1 %% done"), TJS_W("100")) ==
          TJS_W("100 % done"));
}
