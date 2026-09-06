#include <catch2/catch_test_macros.hpp>

#include "BasicDrawDevice.h"
#include "ScriptMgnIntf.h"
#include "WindowIntf.h"
#include "ncbind.hpp"

extern tTJS *TVPScriptEngine;

namespace {
class GlesRuntime {
public:
    GlesRuntime() {
        TVPScriptEngine = new tTJS();
        auto *window = TVPCreateNativeClass_Window();
        auto *device = new tTJSNC_BasicDrawDevice();
        tTJSVariant windowValue(window);
        deviceClass = tTJSVariant(device);
        window->Release();
        device->Release();
        window->PropSet(TJS_MEMBERENSURE | TJS_STATICMEMBER, TJS_W("BasicDrawDevice"), nullptr,
                         &deviceClass, window);
        window->PropSet(TJS_MEMBERENSURE | TJS_STATICMEMBER, TJS_W("PassThroughDrawDevice"), nullptr,
                         &deviceClass, window);
        auto *global = TVPScriptEngine->GetGlobalNoAddRef();
        global->PropSet(TJS_MEMBERENSURE, TJS_W("Window"), nullptr,
                         &windowValue, global);
        ncbAutoRegister::AllRegist();
    }
    ~GlesRuntime() {
        ncbAutoRegister::UnloadModule(TJS_W("krkrgles.dll"));
        deviceClass.Clear();
        TVPScriptEngine->Release();
        TVPScriptEngine = nullptr;
    }
    tTJSVariant deviceClass;
};

tTJSVariant moduleMethod(const tTJSVariant &object) {
    tTJSVariant method;
    auto *dispatch = object.AsObjectNoAddRef();
    REQUIRE(TJS_SUCCEEDED(dispatch->PropGet(TJS_IGNOREPROP,
                                            TJS_W("getModule"), nullptr,
                                            &method, dispatch)));
    REQUIRE(method.Type() == tvtObject);
    return method;
}

TEST_CASE("draw-device modules initialize the owning window's lazy adaptor",
          "[gles][owner]") {
    GlesRuntime runtime;
    REQUIRE(ncbAutoRegister::LoadModule(TJS_W("krkrgles.dll")));
    tTJSVariant initialized;
    REQUIRE_NOTHROW(TVPScriptEngine->ExecScript(TJS_W(
        "class CaptureBaseWindow extends Window {\n"
        "  function CaptureBaseWindow() { super.Window(); }\n"
        "}\n"
        "class CaptureWindow extends CaptureBaseWindow {\n"
        "  function CaptureWindow() { super.CaptureBaseWindow(); }\n"
        "  var _adaptor, initializeCount = 0;\n"
        "  property glesAdaptor { getter() {\n"
        "    if(_adaptor === void) {\n"
        "      initializeCount++;\n"
        "      _adaptor = new global.GLESAdaptor(this);\n"
        "    }\n"
        "    return _adaptor;\n"
        "  } }\n"
        "  property drawDevice {\n"
        "    getter() { return global.CaptureBaseWindow.drawDevice; }\n"
        "    setter(value) { global.CaptureBaseWindow.drawDevice = value; }\n"
        "  }\n"
        "}\n"
        "var owner = new CaptureWindow();\n"
        "owner.drawDevice = new Window.BasicDrawDevice();\n"
        "owner.drawDevice.getModule('live2d');\n"
        "var initialized = owner.initializeCount;\n"
        "invalidate owner;\n"
        "return initialized;\n"), &initialized));
    CHECK(initialized.AsInteger() == 1);
}

tTJSVariant createDrawDevice(const tTJSVariant &deviceClass) {
    iTJSDispatch2 *created = nullptr;
    auto *klass = deviceClass.AsObjectNoAddRef();
    REQUIRE(TJS_SUCCEEDED(klass->CreateNew(0, nullptr, nullptr, &created,
                                          0, nullptr, klass)));
    REQUIRE(created != nullptr);
    tTJSVariant result(created, created);
    created->Release();
    return result;
}
} // namespace

TEST_CASE("GLES loading preserves the built-in draw-device module lifecycle",
          "[gles][load-order]") {
    GlesRuntime runtime;
    const tTJSVariant nativeMethod = moduleMethod(runtime.deviceClass);
    tTJSVariant earlyDevice;
    SECTION("graphics module loads before the first window") {
        REQUIRE(ncbAutoRegister::LoadModule(TJS_W("krkrgles.dll")));
    }
    SECTION("graphics module loads after a window's device exists") {
        earlyDevice = createDrawDevice(runtime.deviceClass);
        REQUIRE(ncbAutoRegister::LoadModule(TJS_W("krkrgles.dll")));
        CHECK(moduleMethod(earlyDevice).AsObjectNoAddRef() ==
              nativeMethod.AsObjectNoAddRef());
    }
    CHECK(moduleMethod(runtime.deviceClass).AsObjectNoAddRef() ==
          nativeMethod.AsObjectNoAddRef());
    const tTJSVariant lateDevice = createDrawDevice(runtime.deviceClass);
    CHECK(moduleMethod(lateDevice).AsObjectNoAddRef() ==
          nativeMethod.AsObjectNoAddRef());
}
