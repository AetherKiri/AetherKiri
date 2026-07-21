#include <catch2/catch_test_macros.hpp>

#include "ScriptAliasUtils.h"
#include "ScriptMgnIntf.h"
#include "tjsDictionary.h"

extern tTJS *TVPScriptEngine;

namespace {

void ensureScriptRuntime() {
    if(TVPGetScriptEngine() == nullptr)
        TVPScriptEngine = new tTJS();
}

void setRawMember(iTJSDispatch2 *object, const tjs_char *name,
                  const tTJSVariant &value) {
    REQUIRE(object != nullptr);
    REQUIRE(TJS_SUCCEEDED(object->PropSet(
        TJS_MEMBERENSURE | TJS_IGNOREPROP, name, nullptr, &value, object)));
}

tTJSVariant getRawMember(iTJSDispatch2 *object, const tjs_char *name) {
    REQUIRE(object != nullptr);
    tTJSVariant value;
    REQUIRE(TJS_SUCCEEDED(object->PropGet(
        TJS_MEMBERMUSTEXIST | TJS_IGNOREPROP, name, nullptr, &value,
        object)));
    return value;
}

} // namespace

TEST_CASE("krkrgles aliases preserve script-owned draw-device members") {
    ensureScriptRuntime();

    iTJSDispatch2 *global = TVPGetScriptEngine()->GetGlobalNoAddRef();
    REQUIRE(global != nullptr);

    iTJSDispatch2 *targetObject = TJSCreateDictionaryObject();
    iTJSDispatch2 *prototypeObject = TJSCreateDictionaryObject();
    REQUIRE(targetObject != nullptr);
    REQUIRE(prototypeObject != nullptr);

    tTJSVariant target(targetObject, targetObject);
    tTJSVariant prototype(prototypeObject, prototypeObject);
    targetObject->Release();
    prototypeObject->Release();

    const tTJSVariant targetMarker(static_cast<tjs_int>(17));
    const tTJSVariant prototypeMarker(static_cast<tjs_int>(23));
    setRawMember(target.AsObjectNoAddRef(), TJS_W("drawDevice"), targetMarker);
    setRawMember(prototype.AsObjectNoAddRef(), TJS_W("drawDevice"),
                 prototypeMarker);
    setRawMember(target.AsObjectNoAddRef(), TJS_W("prototype"), prototype);
    setRawMember(global, TJS_W("KAGWindow"), target);

    const tTJSVariant replacement(static_cast<tjs_int>(29));
    REQUIRE(aetherkiri::plugins::EnsureGlobalObjectMember(
        TJS_W("KAGWindow"), TJS_W("drawDevice"), replacement));

    CHECK(getRawMember(target.AsObjectNoAddRef(), TJS_W("drawDevice"))
              .AsInteger() == 17);
    CHECK(getRawMember(prototype.AsObjectNoAddRef(), TJS_W("drawDevice"))
              .AsInteger() == 23);
    REQUIRE(aetherkiri::plugins::EnsureGlobalObjectMember(
        TJS_W("KAGWindow"), TJS_W("gpuDrawDevice"), replacement));
    CHECK(getRawMember(target.AsObjectNoAddRef(), TJS_W("gpuDrawDevice"))
              .AsInteger() == 29);
    CHECK(getRawMember(prototype.AsObjectNoAddRef(), TJS_W("gpuDrawDevice"))
              .AsInteger() == 29);

    const tTJSVariant secondReplacement(static_cast<tjs_int>(31));
    REQUIRE(aetherkiri::plugins::EnsureGlobalObjectMember(
        TJS_W("KAGWindow"), TJS_W("gpuDrawDevice"), secondReplacement));
    CHECK(getRawMember(target.AsObjectNoAddRef(), TJS_W("gpuDrawDevice"))
              .AsInteger() == 29);

    global->DeleteMember(0, TJS_W("KAGWindow"), nullptr, global);
}
