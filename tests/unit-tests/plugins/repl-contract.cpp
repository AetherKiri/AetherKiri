#include <catch2/catch_test_macros.hpp>

#include "ReplFileChannel.h"

TEST_CASE("REPL JSON escaping preserves protocol delimiters") {
    CHECK(TVPRepl::JsonEscape("quote\" slash\\ line\n tab\t") ==
          "quote\\\" slash\\\\ line\\n tab\\t");
    CHECK(TVPRepl::JsonEscape(std::string(1, '\x01')) == "\\u0001");
    CHECK(TVPRepl::JsonEscape(u8"美羽") == u8"美羽");
}

TEST_CASE("REPL disabled command-line values are recognized") {
    CHECK(TVPRepl::IsDisabledOption(TJS_W("no")));
    CHECK(TVPRepl::IsDisabledOption(TJS_W("OFF")));
    CHECK(TVPRepl::IsDisabledOption(TJS_W("False")));
    CHECK(TVPRepl::IsDisabledOption(TJS_W("0")));
    CHECK(TVPRepl::IsDisabledOption(TJS_W("")));
    CHECK_FALSE(TVPRepl::IsDisabledOption(TJS_W("/tmp/aether-repl")));
}
