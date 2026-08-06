#include <catch2/catch_test_macros.hpp>

#include "tjs.h"

#include <memory>

namespace {

class MockEnabledGuard {
public:
    explicit MockEnabledGuard(bool enabled)
        : Previous(TJS::TVPIsMockEnabled()) {
        TJS::TVPSetMockEnabled(enabled);
    }

    ~MockEnabledGuard() { TJS::TVPSetMockEnabled(Previous); }

private:
    bool Previous;
};

struct TJSReleaser {
    void operator()(tTJS *engine) const { engine->Release(); }
};

} // namespace

TEST_CASE("TJS missing dispatch is independent of compatibility mocks") {
    std::unique_ptr<tTJS, TJSReleaser> engine(new tTJS());
    tTJSVariant proxy;
    REQUIRE_NOTHROW(engine->EvalExpression(
        TJS_W("(function() {"
              "  var proxy = %[];"
              "  proxy.forwardedWriteResult = 0;"
              "  proxy.missing = function(set, name, value) {"
              "    if(set && name == 'forwardedWrite') {"
              "      this.forwardedWriteResult = *value;"
              "      return true;"
              "    }"
              "    if(set) return false;"
              "    if(name == 'forwardedValue') {"
              "      *value = 73;"
              "      return true;"
              "    }"
              "    if(name == 'forwardedCall') {"
              "      *value = function(arg) { return arg + 1; };"
              "      return true;"
              "    }"
              "    return false;"
              "  };"
              "  return proxy;"
              "})()"),
        &proxy));
    REQUIRE(proxy.Type() == tvtObject);

    iTJSDispatch2 *dispatch = proxy.AsObjectNoAddRef();
    REQUIRE(dispatch != nullptr);
    tTJSVariant missing_name(TJS_W("missing"));
    REQUIRE(TJS_SUCCEEDED(dispatch->ClassInstanceInfo(
        TJS_CII_SET_MISSING, 0, &missing_name)));

    MockEnabledGuard mock_disabled(false);

    tTJSVariant value;
    REQUIRE(TJS_SUCCEEDED(dispatch->PropGet(
        0, TJS_W("forwardedValue"), nullptr, &value, dispatch)));
    CHECK(value.AsInteger() == 73);

    tTJSVariant write_value(19);
    REQUIRE(TJS_SUCCEEDED(dispatch->PropSet(
        0, TJS_W("forwardedWrite"), nullptr, &write_value, dispatch)));
    tTJSVariant write_result;
    REQUIRE(TJS_SUCCEEDED(dispatch->PropGet(
        0, TJS_W("forwardedWriteResult"), nullptr, &write_result,
        dispatch)));
    CHECK(write_result.AsInteger() == 19);

    tTJSVariant argument(41);
    tTJSVariant *arguments[] = { &argument };
    tTJSVariant call_result;
    REQUIRE(TJS_SUCCEEDED(dispatch->FuncCall(
        0, TJS_W("forwardedCall"), nullptr, &call_result, 1, arguments,
        dispatch)));
    CHECK(call_result.AsInteger() == 42);
}

TEST_CASE("KAG layer type conversion accepts integer enum values") {
    std::unique_ptr<tTJS, TJSReleaser> engine(new tTJS());
    tTJSVariant result;

    REQUIRE_NOTHROW(engine->ExecScript(
        TJS_W("function convLayerType(value) { return value.type; }\n"
              "return convLayerType(3);"),
        &result, nullptr, TJS_W("world.tjs")));
    CHECK(result.AsInteger() == 3);
}

TEST_CASE("integer pseudo-property remains scoped to KAG layer conversion") {
    std::unique_ptr<tTJS, TJSReleaser> engine(new tTJS());
    tTJSVariant result;

    REQUIRE_THROWS(engine->ExecScript(
        TJS_W("function readType(value) { return value.type; }\n"
              "return readType(3);"),
        &result, nullptr, TJS_W("world.tjs")));
    REQUIRE_THROWS(engine->ExecScript(
        TJS_W("function convLayerType(value) { return value.type; }\n"
              "return convLayerType(29);"),
        &result, nullptr, TJS_W("world.tjs")));
}
