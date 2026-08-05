#include <catch2/catch_test_macros.hpp>

#include "ScriptMgnIntf.h"
#include "tjs.h"

#include <string>

namespace {

using ScriptString = std::basic_string<tjs_char>;

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

std::size_t countOccurrences(const ttstr &script, const tjs_char *marker) {
    const ScriptString source(script.c_str(), script.GetLen());
    const ScriptString needle(marker);
    std::size_t count = 0;
    std::size_t position = 0;
    while((position = source.find(needle, position)) != ScriptString::npos) {
        ++count;
        position += needle.size();
    }
    return count;
}

bool onlyExpectedMovieExists(const ttstr &name) {
    return name == TJS_W("ev_mv023_02_06.mpg");
}

bool noMovieExists(const ttstr &) { return false; }

} // namespace

TEST_CASE("Shifted final movie mapping follows a complete numbered sequence") {
    ttstr script(TJS_W(
        "\t\"ev_mv022_02_01\" => %[ \"storage\",\"ev_mv022_02_01.mpg\" ],\r\n"
        "\t\"ev_mv022_02_05\" => %[ \"storage\",\"ev_mv022_02_05.mpg\" ],\r\n"
        "\t\"ev_mv023_02_06\" => %[ \"storage\",\"ev_mv022_02_06.mpg\" ],\r\n"
        "\t\"ev_mv023_02_01\" => %[ \"storage\",\"ev_mv023_02_01.mpg\" ],\r\n"
        "\t\"ev_mv023_02_05\" => %[ \"storage\",\"ev_mv023_02_05.mpg\" ],\r\n"));

    CHECK(TVPRepairShiftedNumberedMovieMappings(
              script, onlyExpectedMovieExists) == 1);
    CHECK(script.IndexOf(TJS_W(
              "\"ev_mv023_02_06\" => %[ \"storage\",\"ev_mv023_02_06.mpg\"")) >= 0);
    CHECK(script.IndexOf(TJS_W("\"storage\",\"ev_mv022_02_06.mpg\"")) < 0);
}

TEST_CASE("Shifted final movie mapping requires the corrected asset") {
    ttstr script(TJS_W(
        "\"ev_mv023_02_01\" => %[ \"storage\",\"ev_mv023_02_01.mpg\" ],\n"
        "\"ev_mv023_02_05\" => %[ \"storage\",\"ev_mv023_02_05.mpg\" ],\n"
        "\"ev_mv023_02_06\" => %[ \"storage\",\"ev_mv022_02_06.mpg\" ],\n"));
    const ttstr original(script);

    CHECK(TVPRepairShiftedNumberedMovieMappings(script, noMovieExists) == 0);
    CHECK(script == original);
}

TEST_CASE("Shifted final movie mapping requires surrounding identity entries") {
    ttstr script(TJS_W(
        "\"ev_mv023_02_06\" => %[ \"storage\",\"ev_mv022_02_06.mpg\" ],\n"));
    const ttstr original(script);

    CHECK(TVPRepairShiftedNumberedMovieMappings(
              script, onlyExpectedMovieExists) == 0);
    CHECK(script == original);
}

TEST_CASE("World face restore patch supports two-argument updateAll") {
    ttstr script(TJS_W(
        "\tfunction _updateAll(allData, snap=false) {\r\n"
        "\t\tif (allData !== void) {\r\n"
        "\t\t\tvar base = envTransMode ? 1 : 0;\r\n"
        "\t\t\tvar data = allData.data;\r\n"
        "\t\t\tvar leave = %[];\r\n"
        "\t\t\tvar create = [];\r\n"
        "\t\t\tfor (var i = 0; i < data.count; i++) {\r\n"
        "\t\t\t\tvar info = data[i];\r\n"
        "\t\t\t\tif (info !== void) {\r\n"
        "\t\t\t\t\t\tcreate.add(info);\r\n"
        "\t\t\t\t\t\tvar name = info[0];\r\n"
        "\t\t\t\t}\r\n"
        "\t\t\t}\r\n"
        "\t\t\t// 生成するものと同種のもの以外は破棄\r\n"
        "\t\t\tenvClear(leave);\r\n"
        "\t\t\t// オブジェクト生成\r\n"
        "\t\t}\r\n"
        "\t}\r\n"));

    REQUIRE(TVPPatchWorldRestoreFaceVisibility(script));
    CHECK(countOccurrences(script, TJS_W("__akRestoreFaceVisible")) == 3);
    CHECK(script.IndexOf(TJS_W("var base = envTransMode ? 1 : 0;")) >= 0);

    const auto declaration =
        script.IndexOf(TJS_W("var __akRestoreFaceVisible = false;"));
    const auto capture =
        script.IndexOf(TJS_W("__akRestoreFaceVisible = true;"));
    const auto restore =
        script.IndexOf(TJS_W("if (__akRestoreFaceVisible &&"));
    REQUIRE(declaration >= 0);
    REQUIRE(capture >= 0);
    REQUIRE(restore >= 0);
    CHECK(declaration < capture);
    CHECK(capture < restore);
}

TEST_CASE("World face restore patch supports three-argument LF scripts") {
    ttstr script(TJS_W(
        "\tfunction _updateAll(allData, snap=false, restore=true) {\n"
        "\t\tif (allData !== void) {\n"
        "\t\t\tvar data = allData.data;\n"
        "\t\t\tvar leave = %[];\n"
        "\t\t\tvar create = [];\n"
        "\t\t\t\t\t\tcreate.add(info);\n"
        "\t\t\t\t\t\tvar name = info[0];\n"
        "\t\t\tenvClear(leave);\n"
        "\t\t}\n"
        "\t}\n"));

    REQUIRE(TVPPatchWorldRestoreFaceVisibility(script));
    CHECK(countOccurrences(script, TJS_W("__akRestoreFaceVisible")) == 3);
    CHECK(script.IndexOf(TJS_W("\r\n")) < 0);
}

TEST_CASE("World face restore patch is atomic when an anchor is missing") {
    ttstr script(TJS_W(
        "\tfunction _updateAll(allData, snap=false) {\r\n"
        "\t\tif (allData !== void) {\r\n"
        "\t\t\tvar data = allData.data;\r\n"
        "\t\t\t\t\t\tcreate.add(info);\r\n"
        "\t\t}\r\n"
        "\t}\r\n"));
    const ttstr original(script);

    CHECK_FALSE(TVPPatchWorldRestoreFaceVisibility(script));
    CHECK(script == original);
    CHECK(script.IndexOf(TJS_W("__akRestoreFaceVisible")) < 0);
}

TEST_CASE("AffineSourceMotion D3D storage fallback does not require a missing member") {
    ttstr script(TJS_W(
        "\t\t\t\t\tvar s = remove[i];\r\n"
        "\t\t\t\t\tif (s != \"\") {\r\n"
        "\t\t\t\t\t\t_motion_manager.unload(s);\r\n"
        "\t\t\t\t\t}\r\n"
        "\t\t\t\tvar s = create[i];\r\n"
        "\t\t\t\tif (s != \"\") {\r\n"
        "\t\t\t\t\tif (!Storages.isExistentStorage(s)) {\r\n"
        "\t\t\t\t\t\terror(@\"警告:モーション用画像が見つからない:${s}\");\r\n"
        "\t\t\t\t\t} else {\r\n"
        "\t\t\t\t\t\ttry {\r\n"
        "\t\t\t\t\t\t\tvar obj = _motion_manager.load(s);\r\n"));

    REQUIRE(TVPPatchAffineSourceMotionStorageFallback(script));
    CHECK(script.IndexOf(TJS_W("_useD3D")) < 0);
    CHECK(script.IndexOf(TJS_W(
              "if (!Storages.isExistentStorage(loadStorage))")) >= 0);
    CHECK(script.IndexOf(TJS_W(
              "Storages.isExistentStorage(\"dx_\" + loadStorage)")) >= 0);
    CHECK(script.IndexOf(TJS_W(
              "Storages.isExistentStorage(\"dxlow_\" + unloadStorage)")) >= 0);
    CHECK(script.IndexOf(TJS_W("_motion_manager.load(loadStorage)")) >= 0);
    CHECK(script.IndexOf(TJS_W("_motion_manager.unload(unloadStorage)")) >= 0);
    CHECK_FALSE(TVPPatchAffineSourceMotionStorageFallback(script));
}

TEST_CASE("D3D stand source patch uses the layer affine contract") {
    ScriptEngineOwner engine;
    engine->ExecScript(TJS_W(
        "var clNone = 0;\n"
        "class D3DAffineSourcePicture {\n"
        "  var filename = \"old\";\n"
        "}\n"
        "class D3DAffineSourceImage {\n"
        "  var filename = \"\";\n"
        "  var loaded = 0;\n"
        "  var optionsSet = 0;\n"
        "  function D3DAffineSourceImage(owner, sourceClass) {}\n"
        "  function loadImages(storage, colorKey, options) {\n"
        "    if(storage == \"hero.pbd\") loaded = 1;\n"
        "  }\n"
        "  function setOptions(options) { optionsSet = 1; }\n"
        "}\n"
        "class D3DAffineLayer {\n"
        "  var _image;\n"
        "  var originalCalls = 0;\n"
        "  var affineCalls = 0;\n"
        "  function D3DAffineLayer() {\n"
        "    _image = new D3DAffineSourcePicture();\n"
        "  }\n"
        "  function loadImages(filename, colorKey=clNone, options=void, redraw=false) {\n"
        "    originalCalls++;\n"
        "  }\n"
        "  function calcAffine() { affineCalls++; }\n"
        "}\n"
        "function findAffineSource(filename, options) {\n"
        "  return %[sourceClass: D3DAffineSourceImage,\n"
        "           storage: \"hero.pbd\", ext: \".STAND\"];\n"
        "}\n"));

    REQUIRE_NOTHROW(engine->ExecScript(TVPGetD3DStandSourcePatchScript()));
    REQUIRE_NOTHROW(engine->ExecScript(TJS_W(
        "var standLayer = new D3DAffineLayer();\n"
        "standLayer.loadImages(\"hero.stand\", clNone, %[dress: \"default\"]);\n")));

    CHECK(evaluateInteger(engine.operator->(),
                          TJS_W("standLayer.originalCalls")) == 0);
    CHECK(evaluateInteger(engine.operator->(),
                          TJS_W("standLayer.affineCalls")) == 1);
    CHECK(evaluateInteger(engine.operator->(),
                          TJS_W("standLayer._image.loaded")) == 1);
    CHECK(evaluateInteger(engine.operator->(),
                          TJS_W("standLayer._image.optionsSet")) == 1);
}

TEST_CASE("D3DEmote GPU transaction hook pairs batches around drawAffine") {
    ScriptEngineOwner engine;
    engine->ExecScript(TJS_W(
        "class TestD3DBatchAdaptor {\n"
        "  var beginCount = 0;\n"
        "  var endCount = 0;\n"
        "  var throwOnBegin = false;\n"
        "  var throwOnEnd = false;\n"
        "  function beginGpuBatch() {\n"
        "    beginCount++;\n"
        "    if (throwOnBegin) throw new Exception(\"begin failed\");\n"
        "    return true;\n"
        "  }\n"
        "  function endGpuBatch() {\n"
        "    endCount++;\n"
        "    if (throwOnEnd) throw new Exception(\"end failed\");\n"
        "    return true;\n"
        "  }\n"
        "}\n"
        "class AffineSourceMotion {\n"
        "  var _useD3D = true;\n"
        "  var _window;\n"
        "  var shouldThrow = false;\n"
        "  var calls = 0;\n"
        "  function AffineSourceMotion() {\n"
        "    _window = %[motionD3DAdaptor: new TestD3DBatchAdaptor()];\n"
        "  }\n"
        "  function drawAffine(target, mtx, src) {\n"
        "    calls++;\n"
        "    if (shouldThrow) throw new Exception(\"draw failed\");\n"
        "    return 42;\n"
        "  }\n"
        "}\n"));

    CHECK(evaluateInteger(
              engine.operator->(),
              TJS_W("typeof AffineSourceMotion.drawAffine != \"undefined\" ? 1 : 0")) == 1);
    CHECK(evaluateInteger(
              engine.operator->(),
              TJS_W("typeof AffineSourceMotion.__aetherKiriOrigDrawAffine == \"undefined\" ? 1 : 0")) == 1);
    REQUIRE_NOTHROW(
        engine->ExecScript(TVPGetD3DEmoteGpuBatchPatchScript()));
    CHECK(evaluateInteger(
              engine.operator->(),
              TJS_W("typeof AffineSourceMotion.__aetherKiriOrigDrawAffine != \"undefined\" ? 1 : 0")) == 1);
    REQUIRE_NOTHROW(engine->ExecScript(
        TJS_W("var batchSource = new AffineSourceMotion();\n")));
    CHECK(evaluateInteger(engine.operator->(),
                          TJS_W("batchSource._useD3D ? 1 : 0")) == 1);
    CHECK(evaluateInteger(
              engine.operator->(),
              TJS_W("typeof batchSource._window.motionD3DAdaptor.beginGpuBatch != \"undefined\" ? 1 : 0")) == 1);
    REQUIRE_NOTHROW(engine->ExecScript(TJS_W(
        "var batchResult = batchSource.drawAffine(void, void, void);\n")));
    CHECK(evaluateInteger(engine.operator->(), TJS_W("batchResult")) == 42);
    CHECK(evaluateInteger(
              engine.operator->(),
              TJS_W("batchSource._window.motionD3DAdaptor.beginCount")) == 1);
    CHECK(evaluateInteger(
              engine.operator->(),
              TJS_W("batchSource._window.motionD3DAdaptor.endCount")) == 1);

    // Reapplying the hook must not nest a second wrapper.
    REQUIRE_NOTHROW(
        engine->ExecScript(TVPGetD3DEmoteGpuBatchPatchScript()));
    REQUIRE_NOTHROW(engine->ExecScript(TJS_W(
        "batchResult = batchSource.drawAffine(void, void, void);\n")));
    CHECK(evaluateInteger(
              engine.operator->(),
              TJS_W("batchSource._window.motionD3DAdaptor.beginCount")) == 2);
    CHECK(evaluateInteger(
              engine.operator->(),
              TJS_W("batchSource._window.motionD3DAdaptor.endCount")) == 2);

    REQUIRE_NOTHROW(engine->ExecScript(TJS_W(
        "var batchCaught = 0;\n"
        "batchSource.shouldThrow = true;\n"
        "try { batchSource.drawAffine(void, void, void); }\n"
        "catch(e) { batchCaught = 1; }\n")));
    CHECK(evaluateInteger(engine.operator->(), TJS_W("batchCaught")) == 1);
    CHECK(evaluateInteger(
              engine.operator->(),
              TJS_W("batchSource._window.motionD3DAdaptor.beginCount")) == 3);
    CHECK(evaluateInteger(
              engine.operator->(),
              TJS_W("batchSource._window.motionD3DAdaptor.endCount")) == 3);

    // A failed begin is optional instrumentation and must not suppress the
    // game's draw call. Since begin did not return, there is no matching end.
    REQUIRE_NOTHROW(engine->ExecScript(TJS_W(
        "batchSource.shouldThrow = false;\n"
        "batchSource._window.motionD3DAdaptor.throwOnBegin = true;\n"
        "batchResult = batchSource.drawAffine(void, void, void);\n")));
    CHECK(evaluateInteger(engine.operator->(), TJS_W("batchResult")) == 42);
    CHECK(evaluateInteger(engine.operator->(), TJS_W("batchSource.calls")) ==
          4);
    CHECK(evaluateInteger(
              engine.operator->(),
              TJS_W("batchSource._window.motionD3DAdaptor.beginCount")) == 4);
    CHECK(evaluateInteger(
              engine.operator->(),
              TJS_W("batchSource._window.motionD3DAdaptor.endCount")) == 3);

    // A failed end is also optional instrumentation and must not mask a
    // successful result from the original draw implementation.
    REQUIRE_NOTHROW(engine->ExecScript(TJS_W(
        "batchSource._window.motionD3DAdaptor.throwOnBegin = false;\n"
        "batchSource._window.motionD3DAdaptor.throwOnEnd = true;\n"
        "batchResult = batchSource.drawAffine(void, void, void);\n")));
    CHECK(evaluateInteger(engine.operator->(), TJS_W("batchResult")) == 42);
    CHECK(evaluateInteger(engine.operator->(), TJS_W("batchSource.calls")) ==
          5);
    CHECK(evaluateInteger(
              engine.operator->(),
              TJS_W("batchSource._window.motionD3DAdaptor.beginCount")) == 5);
    CHECK(evaluateInteger(
              engine.operator->(),
              TJS_W("batchSource._window.motionD3DAdaptor.endCount")) == 4);

    // If both the original draw and end fail, callers must still observe the
    // original draw failure instead of the cleanup failure.
    REQUIRE_NOTHROW(engine->ExecScript(TJS_W(
        "var batchCaughtMessage = \"\";\n"
        "batchSource.shouldThrow = true;\n"
        "try { batchSource.drawAffine(void, void, void); }\n"
        "catch(e) { batchCaughtMessage = e.message; }\n")));
    CHECK(evaluateInteger(
              engine.operator->(),
              TJS_W("batchCaughtMessage == \"draw failed\" ? 1 : 0")) == 1);
    CHECK(evaluateInteger(engine.operator->(), TJS_W("batchSource.calls")) ==
          6);
    CHECK(evaluateInteger(
              engine.operator->(),
              TJS_W("batchSource._window.motionD3DAdaptor.beginCount")) == 6);
    CHECK(evaluateInteger(
              engine.operator->(),
              TJS_W("batchSource._window.motionD3DAdaptor.endCount")) == 5);

    REQUIRE_NOTHROW(engine->ExecScript(TJS_W(
        "batchSource.shouldThrow = false;\n"
        "batchSource._window.motionD3DAdaptor.throwOnEnd = false;\n"
        "batchSource._useD3D = false;\n"
        "batchSource.drawAffine(void, void, void);\n")));
    CHECK(evaluateInteger(
              engine.operator->(),
              TJS_W("batchSource._window.motionD3DAdaptor.beginCount")) == 6);
    CHECK(evaluateInteger(
              engine.operator->(),
              TJS_W("batchSource._window.motionD3DAdaptor.endCount")) == 5);
}

TEST_CASE("D3DEmote GPU transaction hook follows AffineSourceMotion redefinition") {
    ScriptEngineOwner engine;
    engine->ExecScript(TJS_W(
        "class RedefinitionBatchAdaptor {\n"
        "  var beginCount = 0;\n"
        "  var endCount = 0;\n"
        "  function beginGpuBatch() { beginCount++; }\n"
        "  function endGpuBatch() { endCount++; }\n"
        "}\n"
        "class AffineSourceMotion {\n"
        "  var _useD3D = true;\n"
        "  var _window;\n"
        "  function AffineSourceMotion() {\n"
        "    _window = %[motionD3DAdaptor: new RedefinitionBatchAdaptor()];\n"
        "  }\n"
        "  function drawAffine(target, mtx, src) { return 42; }\n"
        "}\n"));

    REQUIRE_NOTHROW(engine->ExecScript(TVPGetD3DEmoteGpuBatchPatchScript()));
    REQUIRE_NOTHROW(engine->ExecScript(TJS_W(
        "var originalSource = new AffineSourceMotion();\n"
        "var originalResult = originalSource.drawAffine(void, void, void);\n")));
    CHECK(evaluateInteger(engine.operator->(), TJS_W("originalResult")) == 42);

    // AffineSourceMotion.tjs can replace the global class after motion.tjs was
    // loaded. Reapplying the post-load hook must wrap the replacement class,
    // while the first wrapper keeps calling its own saved implementation.
    REQUIRE_NOTHROW(engine->ExecScript(TJS_W(
        "class ReplacementAffineSourceMotion {\n"
        "  var _useD3D = true;\n"
        "  var _window;\n"
        "  function ReplacementAffineSourceMotion() {\n"
        "    _window = %[motionD3DAdaptor: new RedefinitionBatchAdaptor()];\n"
        "  }\n"
        "  function drawAffine(target, mtx, src) { return 84; }\n"
        "}\n"
        "global.AffineSourceMotion = ReplacementAffineSourceMotion;\n")));
    REQUIRE_NOTHROW(engine->ExecScript(TVPGetD3DEmoteGpuBatchPatchScript()));
    REQUIRE_NOTHROW(engine->ExecScript(TJS_W(
        "var replacementSource = new AffineSourceMotion();\n"
        "var replacementResult = replacementSource.drawAffine(void, void, void);\n"
        "originalResult = originalSource.drawAffine(void, void, void);\n")));

    CHECK(evaluateInteger(engine.operator->(), TJS_W("replacementResult")) ==
          84);
    CHECK(evaluateInteger(engine.operator->(), TJS_W("originalResult")) == 42);
    CHECK(evaluateInteger(
              engine.operator->(),
              TJS_W("replacementSource._window.motionD3DAdaptor.beginCount")) ==
          1);
    CHECK(evaluateInteger(
              engine.operator->(),
              TJS_W("replacementSource._window.motionD3DAdaptor.endCount")) ==
          1);
    CHECK(evaluateInteger(
              engine.operator->(),
              TJS_W("originalSource._window.motionD3DAdaptor.beginCount")) == 2);
    CHECK(evaluateInteger(
              engine.operator->(),
              TJS_W("originalSource._window.motionD3DAdaptor.endCount")) == 2);

    // A second affinesourcemotion.tjs-style trigger remains idempotent for the
    // replacement class rather than nesting another wrapper.
    REQUIRE_NOTHROW(engine->ExecScript(TVPGetD3DEmoteGpuBatchPatchScript()));
    REQUIRE_NOTHROW(engine->ExecScript(TJS_W(
        "replacementResult = replacementSource.drawAffine(void, void, void);\n")));
    CHECK(evaluateInteger(engine.operator->(), TJS_W("replacementResult")) ==
          84);
    CHECK(evaluateInteger(
              engine.operator->(),
              TJS_W("replacementSource._window.motionD3DAdaptor.beginCount")) ==
          2);
    CHECK(evaluateInteger(
              engine.operator->(),
              TJS_W("replacementSource._window.motionD3DAdaptor.endCount")) ==
          2);
}
