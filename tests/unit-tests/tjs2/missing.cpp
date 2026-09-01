#include <catch2/catch_test_macros.hpp>

#include "tjs.h"
#include "tjsError.h"
#include "tjsInterCodeGen.h"

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

TEST_CASE(
    "exceptions from cached expression functions survive orphaned source blocks") {
    std::unique_ptr<tTJS, TJSReleaser> engine(new tTJS());
    tTJSVariant function;

    REQUIRE_NOTHROW(engine->EvalExpression(
        TJS_W("(function() { return 1; })"),
        &function));
    REQUIRE(function.Type() == tvtObject);

    tTJSVariantClosure closure = function.AsObjectClosureNoAddRef();
    REQUIRE(closure.Object != nullptr);
    auto *context = dynamic_cast<TJS::tTJSInterCodeContext *>(closure.Object);
    REQUIRE(context != nullptr);
    engine->CompactScriptCache(3);
    REQUIRE(context->GetBlock() == nullptr);
    try {
        TJS::TJS_eTJSScriptError(TJS_W("orphaned source"), context, 0);
        FAIL("the script error helper did not throw");
    } catch(const TJS::eTJSScriptError &error) {
        CHECK(error.GetBlockNoAddRef() == nullptr);
        CHECK(TJS_strlen(error.GetBlockName()) == 0);
        CHECK(error.GetSourceLine() == 0);
    }
}

TEST_CASE("TJS strings support prefix and suffix checks") {
    std::unique_ptr<tTJS, TJSReleaser> engine(new tTJS());
    tTJSVariant result;

    REQUIRE_NOTHROW(engine->EvalExpression(
        TJS_W("'scenario/start.ks'.startsWith('scenario/')"), &result));
    CHECK(result.AsInteger() == 1);
    REQUIRE_NOTHROW(engine->EvalExpression(
        TJS_W("'scenario/start.ks'.startsWith('start')"), &result));
    CHECK(result.AsInteger() == 0);
    REQUIRE_NOTHROW(engine->EvalExpression(
        TJS_W("'scenario/start.ks'.endsWith('.ks')"), &result));
    CHECK(result.AsInteger() == 1);
    REQUIRE_NOTHROW(engine->EvalExpression(
        TJS_W("'scenario/start.ks'.endsWith('.tjs')"), &result));
    CHECK(result.AsInteger() == 0);
    REQUIRE_NOTHROW(
        engine->EvalExpression(TJS_W("''.endsWith('')"), &result));
    CHECK(result.AsInteger() == 1);
}

TEST_CASE("TJS arrays support membership checks") {
    std::unique_ptr<tTJS, TJSReleaser> engine(new tTJS());
    tTJSVariant result;

    REQUIRE_NOTHROW(engine->EvalExpression(
        TJS_W("(const)[1, 2, 3].includes(2)"), &result));
    CHECK(result.AsInteger() == 1);
    REQUIRE_NOTHROW(engine->EvalExpression(
        TJS_W("(const)[1, 2, 3].includes(4)"), &result));
    CHECK(result.AsInteger() == 0);
    REQUIRE_NOTHROW(engine->EvalExpression(
        TJS_W("(const)[1, 2, 3].includes(1, 1)"), &result));
    CHECK(result.AsInteger() == 0);
    REQUIRE_NOTHROW(engine->EvalExpression(
        TJS_W("(const)[1, 2, 3].includes(3, -1)"), &result));
    CHECK(result.AsInteger() == 1);
}

TEST_CASE("TJS dictionary static helpers expose sorted keys and count") {
    std::unique_ptr<tTJS, TJSReleaser> engine(new tTJS());
    tTJSVariant result;

    REQUIRE_NOTHROW(engine->EvalExpression(
        TJS_W("(function() {"
              "  var dictionary = %[ 'z' => 3, 'a' => 1, 'm' => 2 ];"
              "  var keys = Dictionary.keys(dictionary);"
              "  return keys.join(',') + ':' + "
              "Dictionary.getCount(dictionary);"
              "})()"),
        &result));
    CHECK(ttstr(result) == TJS_W("a,m,z:3"));
}
