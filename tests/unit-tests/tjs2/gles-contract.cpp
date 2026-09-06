#include <catch2/catch_test_macros.hpp>

#include "tjs.h"
#include "GpuCompatScript.h"
#include "GlesCaptureUtils.h"

#include <memory>

namespace {
struct EngineReleaser {
    void operator()(tTJS *engine) const { engine->Release(); }
};

} // namespace

TEST_CASE("GPU companion script preserves adaptor constructor and lazy getter", "[gles]") {
    std::unique_ptr<tTJS, EngineReleaser> engine(new tTJS());
    REQUIRE_NOTHROW(engine->ExecScript(TJS_W(
        "class GLESAdaptor {\n"
        "  function GLESAdaptor(owner) {}\n"
        "  function setScreenSize(w, h) {}\n"
        "}\n"
        "class OGLDrawDevice { function setScreenSize(w, h) {} }\n"
        "class BasicDrawDevice {}\n"
        "class Window {}\n"
        "Window.BasicDrawDevice = BasicDrawDevice;\n"
        "class KAGWindow extends Window {\n"
        "  var width = 1920, height = 1080, getterCalls = 0;\n"
        "  var _glesAdaptor;\n"
        "  property glesAdaptor { getter() {\n"
        "    getterCalls++;\n"
        "    if(_glesAdaptor === void) _glesAdaptor = new GLESAdaptor(this);\n"
        "    return _glesAdaptor;\n"
        "  } }\n"
        "}\n")));
    REQUIRE_NOTHROW(engine->ExecScript(ttstr(TVP_GPU_COMPAT_SCRIPT)));
    tTJSVariant result;
    REQUIRE_NOTHROW(engine->ExecScript(TJS_W(
        "var owner = new KAGWindow();\n"
        "owner.KAGWindow_createDrawDevice();\n"
        "return owner.getterCalls;\n"), &result));
    CHECK(result.AsInteger() == 0);
    REQUIRE_NOTHROW(engine->EvalExpression(
        TJS_W("owner.GLESAdaptor instanceof 'Class'"), &result));
    CHECK(result.AsInteger() == 1);
    REQUIRE_NOTHROW(engine->EvalExpression(
        TJS_W("owner.glesAdaptor instanceof 'GLESAdaptor'"), &result));
    CHECK(result.AsInteger() == 1);
    REQUIRE_NOTHROW(engine->EvalExpression(TJS_W("owner.getterCalls"), &result));
    CHECK(result.AsInteger() == 1);
}

TEST_CASE("GLES capture separates destination from callback dimensions and context", "[gles]") {
    std::unique_ptr<tTJS, EngineReleaser> engine(new tTJS());
    tTJSVariant callback, target, context, result;
    REQUIRE_NOTHROW(engine->ExecScript(TJS_W(
        "var calls = 0, width, height, received;\n"
        "function onCapture(w, h, data) {\n"
        "  calls++; width = w; height = h; received = data;\n"
        "}\n")));
    engine->EvalExpression(TJS_W("onCapture"), &callback);
    engine->EvalExpression(TJS_W("%[destination: 1]"), &target);
    engine->EvalExpression(TJS_W("%[background: 73]"), &context);
    tTJSVariant flags(1);
    tTJSVariant *args[] = {&target, &callback, &context, &flags};
    REQUIRE(aetherkiri::plugins::gles::CaptureCallbackIndex(4, args) == 1);
    REQUIRE(aetherkiri::plugins::gles::InvokeCaptureCallback(1280, 720, 4, args) == TJS_S_OK);
    engine->EvalExpression(TJS_W("width == 1280 && height == 720 && received.background == 73 && calls == 1"), &result);
    CHECK(result.AsInteger() == 1);

    tTJSVariant *directArgs[] = {&callback, &context};
    REQUIRE(aetherkiri::plugins::gles::InvokeCaptureCallback(1920, 1080, 2, directArgs) == TJS_S_OK);
    engine->EvalExpression(TJS_W("width == 1920 && height == 1080 && received.background == 73 && calls == 2"), &result);
    CHECK(result.AsInteger() == 1);

    // A missing background must remain void, not become the destination.
    REQUIRE(aetherkiri::plugins::gles::InvokeCaptureCallback(1280, 720, 2, args) == TJS_S_OK);
    engine->EvalExpression(TJS_W("received === void && calls == 3"), &result);
    CHECK(result.AsInteger() == 1);

    // Layers and dictionaries are objects, but are not callbacks.
    tTJSVariant *noCallback[] = {&target, &context};
    CHECK(aetherkiri::plugins::gles::CaptureCallbackIndex(2, noCallback) == -1);
    CHECK(aetherkiri::plugins::gles::InvokeCaptureCallback(1280, 720, 2, noCallback) == TJS_S_OK);
    engine->EvalExpression(TJS_W("calls"), &result);
    CHECK(result.AsInteger() == 3);
}
