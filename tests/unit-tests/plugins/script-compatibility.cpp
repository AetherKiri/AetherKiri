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
