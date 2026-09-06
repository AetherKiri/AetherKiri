#include <catch2/catch_test_macros.hpp>
#include <memory>

#include "LayerImpl.h"
#include "LayerManager.h"
#include "ScriptMgnIntf.h"
#include "StorageIntf.h"
#include "SysInitIntf.h"
#include "SystemControl.h"
#include "WindowIntf.h"
#include "tvpgl.h"

extern tTJS *TVPScriptEngine;

namespace {
class PointerRuntime {
public:
    PointerRuntime() {
        TVPInitTVPGL();
        TVPSetCurrentDirectory(TJS_W("file://./"));
        TVPSetCommandLine(TJS_W("renderer"), TJS_W("software"));
        TVPProjectDir = TJS_W("file://./");
        systemControl = std::make_unique<tTVPSystemControl>();
        TVPSystemControl = systemControl.get();
        TVPScriptEngine = new tTJS();
        auto *global = TVPScriptEngine->GetGlobalNoAddRef();
        auto install = [global](const tjs_char *name, iTJSDispatch2 *klass) {
            tTJSVariant value(klass);
            global->PropSet(TJS_MEMBERENSURE, name, nullptr, &value, global);
            klass->Release();
        };
        install(TJS_W("Window"), TVPCreateNativeClass_Window());
        install(TJS_W("Layer"), TVPCreateNativeClass_Layer());
        run(TJS_W(
            "var clicks = 0, ups = 0, downs = 0, doubles = 0;\n"
            "var releasesCapture = false, revealOnDown = false;\n"
            "var window = new Window();\n"
            "class ReplaySurface extends Layer {\n"
            "  function ReplaySurface(window) { super.Layer(window, null); }\n"
            "  function onMouseDown(x, y, button, flags) {\n"
            "    if(global.revealOnDown) {\n"
            "      global.button.visible = true; releaseCapture();\n"
            "    }\n"
            "  }\n"
            "}\n"
            "var primary = new ReplaySurface(window);\n"
            "primary.setSize(640, 480);\n"
            "class Button extends Layer {\n"
            "  function Button(window, parent) {\n"
            "    super.Layer(window, parent);\n"
            "    setSize(640, 480); hitThreshold = 0; name = 'gallery';\n"
            "  }\n"
            "  var exp = 'clicks++', onclick = 'clicks++', pressed = false;\n"
            "  function _evalOnClick() { global.clicks++; }\n"
            "  function onDoubleClick(x, y) { global.doubles++; }\n"
            "  function onMouseDown(x, y, button, flags) {\n"
            "    global.downs++; pressed = true;\n"
            "    if(global.releasesCapture) releaseCapture();\n"
            "  }\n"
            "  function onMouseUp(x, y, button, flags) {\n"
            "    global.ups++;\n"
            "    if(pressed && !global.releasesCapture) global.clicks++;\n"
            "    pressed = false;\n"
            "  }\n"
            "}\n"
            "var button = new Button(window, primary);\n"
            "button.visible = false;\n"));
        tTJSVariant value;
        TVPScriptEngine->EvalExpression(TJS_W("primary"), &value);
        auto *layer = tTJSNI_Layer::FromVariant(value);
        REQUIRE(layer != nullptr);
        manager = layer->GetManager();
        REQUIRE(manager != nullptr);
    }

    ~PointerRuntime() {
        run(TJS_W("invalidate button; invalidate primary; invalidate window;"));
        TVPScriptEngine->Release();
        TVPScriptEngine = nullptr;
        TVPSystemControl = nullptr;
        TVPSystemControlAlive = false;
    }

    void run(const tjs_char *script) { TVPScriptEngine->ExecScript(script); }
    tjs_int value(const tjs_char *expression) {
        tTJSVariant result;
        TVPScriptEngine->EvalExpression(expression, &result);
        return result.AsInteger();
    }
    void down() { manager->PrimaryMouseDown(300, 200, mbLeft, 0); }
    void clickAndUp() {
        manager->PrimaryClick(300, 200);
        manager->PrimaryMouseUp(300, 200, mbLeft, 0);
    }

    tTVPLayerManager *manager = nullptr;

private:
    std::unique_ptr<tTVPSystemControl> systemControl;
};
} // namespace

TEST_CASE("a release cannot activate a button revealed after mouse-down",
          "[input][gallery][gesture]") {
    PointerRuntime runtime;
    SECTION("the replay ends in the mouse-down handler") {
        runtime.run(TJS_W("revealOnDown = true;"));
        runtime.down();
    }
    SECTION("the replay ends between down and release") {
        runtime.down();
        runtime.manager->ReleaseCapture();
        runtime.run(TJS_W("button.visible = true;"));
    }
    runtime.clickAndUp();
    CHECK(runtime.value(TJS_W("clicks")) == 0);
    CHECK(runtime.value(TJS_W("ups")) == 0);

    // The menu must remain interactive on the following complete gesture.
    runtime.down();
    runtime.clickAndUp();
    CHECK(runtime.value(TJS_W("downs")) == 1);
    CHECK(runtime.value(TJS_W("ups")) == 1);
    CHECK(runtime.value(TJS_W("clicks")) == 1);
}

TEST_CASE("a normal captured button dispatches once on mouse-up",
          "[input][gesture]") {
    PointerRuntime runtime;
    runtime.run(TJS_W("button.visible = true;"));
    runtime.down();
    runtime.clickAndUp();
    CHECK(runtime.value(TJS_W("clicks")) == 1);
    CHECK(runtime.value(TJS_W("ups")) == 1);
}

TEST_CASE("a button that releases capture can still finish its own click",
          "[input][gesture]") {
    PointerRuntime runtime;
    runtime.run(TJS_W("button.visible = true; releasesCapture = true;"));
    runtime.down();
    runtime.clickAndUp();
    CHECK(runtime.value(TJS_W("clicks")) == 1);
    CHECK(runtime.value(TJS_W("ups")) == 1);
}

TEST_CASE("explicit click-only dispatch remains available",
          "[input][gesture]") {
    PointerRuntime runtime;
    runtime.run(TJS_W("button.visible = true;"));
    runtime.manager->PrimaryClick(300, 200);
    CHECK(runtime.value(TJS_W("clicks")) == 1);
}

TEST_CASE("invalidating the pressed layer does not redirect its release",
          "[input][gesture][lifetime]") {
    PointerRuntime runtime;
    runtime.run(TJS_W(
        "button.visible = true;\n"
        "var overlay = new Layer(window, primary);\n"
        "overlay.setSize(640, 480); overlay.hitThreshold = 0;\n"
        "overlay.visible = true;\n"));
    runtime.down();
    runtime.manager->ReleaseCapture();
    runtime.run(TJS_W("invalidate overlay; overlay = void;"));
    runtime.clickAndUp();
    CHECK(runtime.value(TJS_W("clicks")) == 0);
    CHECK(runtime.value(TJS_W("ups")) == 0);
    runtime.down();
    runtime.clickAndUp();
    CHECK(runtime.value(TJS_W("clicks")) == 1);
}

TEST_CASE("a synthetic double-click cannot cross the active gesture target",
          "[input][gesture]") {
    PointerRuntime runtime;
    runtime.run(TJS_W("revealOnDown = true;"));
    runtime.down();
    runtime.manager->PrimaryDoubleClick(300, 200);
    runtime.manager->PrimaryMouseUp(300, 200, mbLeft, 0);
    CHECK(runtime.value(TJS_W("doubles")) == 0);
    runtime.manager->PrimaryDoubleClick(300, 200);
    CHECK(runtime.value(TJS_W("doubles")) == 1);
}
