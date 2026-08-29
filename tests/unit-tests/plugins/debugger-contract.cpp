#include <catch2/catch_test_macros.hpp>

#include "CharacterSet.h"
#include "DebugIntf.h"
#include "Debugger.h"

TEST_CASE("krkrz debugger breakpoint metadata keeps the Aether ABI") {
    Breakpoints breakpoints;
    BPMeta meta;
    meta.condition = TJS_W("score > 10");
    meta.logMessage = TJS_W("score={score}");

    breakpoints.SetBreakPoint(TJS_W("scene/main.tjs"), 42, meta);
    REQUIRE(breakpoints.IsBreakPoint(TJS_W("scene/main.tjs"), 42));
    const BPMeta *readback =
        breakpoints.GetMeta(TJS_W("scene/main.tjs"), 42);
    REQUIRE(readback != nullptr);
    CHECK(readback->condition == TJS_W("score > 10"));
    CHECK(readback->logMessage == TJS_W("score={score}"));

    breakpoints.ClearBreakPoint(TJS_W("scene/main.tjs"), 42);
    CHECK_FALSE(breakpoints.IsBreakPoint(TJS_W("scene/main.tjs"), 42));
}

TEST_CASE("debugger text adapters preserve non-ASCII source names") {
    const std::string source = u8"回想/美羽.tjs";
    tjs_string wide;
    REQUIRE(TVPUtf8ToUtf16(wide, source));
    std::string roundtrip;
    REQUIRE(TVPUtf16ToUtf8(roundtrip, wide));
    CHECK(roundtrip == source);
}

TEST_CASE("Debug.prettyPrint exposes the krkrz primitive contract") {
    CHECK(TVPPrettyPrint(tTJSVariant(static_cast<tjs_int>(42)), 2, true) ==
          TJS_W("42"));
    CHECK(TVPPrettyPrint(tTJSVariant(TJS_W("美羽")), 2, true) ==
          TJS_W("\"美羽\""));
}
