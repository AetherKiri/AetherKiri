#include <catch2/catch_test_macros.hpp>

#include "LayerIntf.h"

TEST_CASE("Font exposes KAG3 bundled-font compatibility methods") {
    iTJSDispatch2 *fontClass = TVPCreateNativeClass_Font();
    REQUIRE(fontClass != nullptr);

    for(const auto *name : {TJS_W("addFont"), TJS_W("AddFont"),
                            TJS_W("AddTrueTypeFont")}) {
        tTJSVariant method;
        INFO(ttstr(name).AsStdString());
        REQUIRE(TJS_SUCCEEDED(
            fontClass->PropGet(0, name, nullptr, &method, fontClass)));
        CHECK(method.Type() == tvtObject);
    }

    fontClass->Release();
}
