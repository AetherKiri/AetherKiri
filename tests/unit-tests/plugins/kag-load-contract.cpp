#include <catch2/catch_test_macros.hpp>

#include "ScriptMgnIntf.h"
#include "tjs.h"

namespace {

class ScriptEngineOwner {
public:
    ScriptEngineOwner() : engine_(new tTJS()) {}
    ~ScriptEngineOwner() { engine_->Release(); }

    tTJS *operator->() const { return engine_; }

private:
    tTJS *engine_;
};

tjs_int evaluateInteger(tTJS *engine, const tjs_char *expression) {
    tTJSVariant result;
    engine->EvalExpression(expression, &result);
    return result.AsInteger();
}

ttstr evaluateString(tTJS *engine, const tjs_char *expression) {
    tTJSVariant result;
    engine->EvalExpression(expression, &result);
    return ttstr(result);
}

} // namespace

TEST_CASE("KAG load guard falls back only when a wrapper swallows the load") {
    SECTION("delegated loads execute exactly once") {
        ScriptEngineOwner engine;
        engine->ExecScript(TJS_W(
            "var nativeSerial = 0, nativeCalls = 0, wrappedCalls = 0;\n"
            "var Scripts = %[\n"
            "  getStorageExecutionSerial: function() { return global.nativeSerial; },\n"
            "  execStorageNative: function(storage) { global.nativeCalls++; global.nativeSerial++; }\n"
            "];\n"
            "function KAGLoadScript(storage) { wrappedCalls++; nativeSerial++; }\n"));
        engine->ExecScript(TVPGetKagLoadContractGuardScript());
        engine->ExecScript(TJS_W("KAGLoadScript(\"EditLayer.tjs\");"));

        CHECK(evaluateInteger(engine.operator->(), TJS_W("wrappedCalls")) == 1);
        CHECK(evaluateInteger(engine.operator->(), TJS_W("nativeCalls")) == 0);
    }

    SECTION("swallowed loads fall back once and preserve extra arguments") {
        ScriptEngineOwner engine;
        engine->ExecScript(TJS_W(
            "var nativeSerial = 0, nativeCalls = 0, wrappedCalls = 0;\n"
            "var wrappedMode = \"\", nativeMode = \"\";\n"
            "var Scripts = %[\n"
            "  getStorageExecutionSerial: function() { return global.nativeSerial; },\n"
            "  execStorageNative: function(storage, mode) { global.nativeCalls++; global.nativeMode = mode; global.nativeSerial++; }\n"
            "];\n"
            "function KAGLoadScript(storage, mode) { wrappedCalls++; wrappedMode = mode; }\n"));
        engine->ExecScript(TVPGetKagLoadContractGuardScript());
        CHECK(evaluateString(engine.operator->(),
                             TJS_W("typeof global.__aetherKiriOriginalKAGLoadScript")) ==
              TJS_W("Object"));
        engine->ExecScript(
            TJS_W("KAGLoadScript(\"EditLayer.tjs\", \"utf-8\");"));

        CHECK(evaluateInteger(engine.operator->(), TJS_W("wrappedCalls")) == 1);
        CHECK(evaluateInteger(engine.operator->(), TJS_W("nativeCalls")) == 1);

        tTJSVariant mode;
        engine->EvalExpression(TJS_W("nativeMode"), &mode);
        CHECK(ttstr(mode) == TJS_W("utf-8"));
    }

    SECTION("empty storage is not synthesized") {
        ScriptEngineOwner engine;
        engine->ExecScript(TJS_W(
            "var nativeSerial = 0, nativeCalls = 0;\n"
            "var Scripts = %[\n"
            "  getStorageExecutionSerial: function() { return global.nativeSerial; },\n"
            "  execStorageNative: function(storage) { global.nativeCalls++; global.nativeSerial++; }\n"
            "];\n"
            "function KAGLoadScript(storage) {}\n"));
        engine->ExecScript(TVPGetKagLoadContractGuardScript());
        engine->ExecScript(TJS_W("KAGLoadScript(\"\");"));

        CHECK(evaluateInteger(engine.operator->(), TJS_W("nativeCalls")) == 0);
    }
}

TEST_CASE("late patches preserve registered load hooks") {
    ScriptEngineOwner engine;
    engine->ExecScript(TJS_W(
        "var loadTrigger = %[instance: %[loadHooks: new Dictionary()]];\n"
        "loadTrigger.instance.loadHooks.continueHook =\n"
        "  \"registered-before-patch\";\n"
        "loadTrigger.instance.loadHooks.sharedHook =\n"
        "  \"runtime-registration\";\n"));

    tTJSVariant savedHooks;
    engine->EvalExpression(TVPGetPatchRuntimeRegistryExpression(), &savedHooks);
    engine->ExecScript(TJS_W(
        "loadTrigger = %[instance: %[loadHooks: new Dictionary()]];\n"
        "loadTrigger.instance.loadHooks.patchHook =\n"
        "  \"registered-by-patch\";\n"
        "loadTrigger.instance.loadHooks.sharedHook = \"patch-default\";\n"));
    tTJSVariant replacementHooks;
    engine->EvalExpression(TVPGetPatchRuntimeRegistryExpression(),
                           &replacementHooks);
    REQUIRE(TVPMergeObjectMembers(replacementHooks.AsObjectNoAddRef(),
                                  savedHooks.AsObjectNoAddRef()));

    CHECK(evaluateString(engine.operator->(),
                         TJS_W("loadTrigger.instance.loadHooks.continueHook")) ==
          TJS_W("registered-before-patch"));
    CHECK(evaluateString(engine.operator->(),
                         TJS_W("loadTrigger.instance.loadHooks.patchHook")) ==
          TJS_W("registered-by-patch"));
    CHECK(evaluateString(engine.operator->(),
                         TJS_W("loadTrigger.instance.loadHooks.sharedHook")) ==
          TJS_W("runtime-registration"));
}

TEST_CASE("late patches recover a skipped load trigger singleton") {
    ScriptEngineOwner engine;
    engine->ExecScript(TJS_W(
        "class loadTrigger {\n"
        "  var loadHooks = new Dictionary();\n"
        "  function marker() { return \"original\"; }\n"
        "}\n"
        "loadTrigger.instance = new loadTrigger();\n"
        "loadTrigger.instance.loadHooks.continueHook = \"registered\";\n"));

    tTJSVariant savedHooks;
    engine->EvalExpression(TVPGetPatchRuntimeRegistryExpression(),
                           &savedHooks);
    engine->ExecScript(TJS_W(
        "class loadTrigger {\n"
        "  var loadHooks = new Dictionary();\n"
        "  function marker() { return \"replacement\"; }\n"
        "}\n"));

    CHECK(evaluateString(engine.operator->(),
                         TJS_W("typeof loadTrigger.instance")) ==
          TJS_W("undefined"));
    engine->ExecScript(TVPGetPatchRuntimeInstanceRecoveryScript());

    tTJSVariant replacementHooks;
    engine->EvalExpression(TVPGetPatchRuntimeRegistryExpression(),
                           &replacementHooks);
    REQUIRE(TVPMergeObjectMembers(replacementHooks.AsObjectNoAddRef(),
                                  savedHooks.AsObjectNoAddRef()));
    CHECK(evaluateString(engine.operator->(),
                         TJS_W("loadTrigger.instance.marker()")) ==
          TJS_W("replacement"));
    CHECK(evaluateString(
              engine.operator->(),
              TJS_W("loadTrigger.instance.loadHooks.continueHook")) ==
          TJS_W("registered"));
}

TEST_CASE("patch prerequisites persist globally across script blocks") {
    ScriptEngineOwner engine;

    engine->ExecScript(TVPGetStartupPatchPrerequisitesScript());
    CHECK(evaluateString(engine.operator->(),
                         TJS_W("typeof global.kagHookEntries")) ==
          TJS_W("Object"));
    CHECK(evaluateString(engine.operator->(),
                         TJS_W("typeof global.afterInitCallback")) ==
          TJS_W("Object"));

    engine->ExecScript(TJS_W(
        "global.kagHookEntries.add(123);\n"
        "global.KAGWindow = %[];\n"));
    engine->ExecScript(TVPGetStartupPatchPrerequisitesScript());
    engine->ExecScript(TVPGetPatchWindowPrerequisitesScript());

    CHECK(evaluateInteger(engine.operator->(),
                          TJS_W("global.kagHookEntries.count")) == 1);
    CHECK(evaluateInteger(engine.operator->(),
                          TJS_W("global.KAGWindow.kagHookEntries[0]")) ==
          123);
    CHECK(evaluateInteger(engine.operator->(),
                          TJS_W("global.KAGWindow.COMMAND_WAIT")) == 2);
}
