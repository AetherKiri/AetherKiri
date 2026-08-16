#include <catch2/catch_test_macros.hpp>

#include "PluginImpl.h"
#include "ScriptMgnIntf.h"
#include "TransIntf.h"
#include "ncbind.hpp"
#include "tjsDictionary.h"

#include <cstring>
#include <utility>

extern tTJS *TVPScriptEngine;
extern "C" void TVPRegisterLayerExDrawPluginAnchor();

namespace {

void ensurePluginRegistryRuntime() {
    TVPRegisterLayerExDrawPluginAnchor();
    if(TVPGetScriptEngine() == nullptr)
        TVPScriptEngine = new tTJS();
    ncbAutoRegister::AllRegist();
}

tTJSVariant getGlobalProp(const tjs_char *name) {
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    REQUIRE(global != nullptr);

    tTJSVariant value;
    REQUIRE(TJS_SUCCEEDED(global->PropGet(0, name, nullptr, &value, global)));
    global->Release();
    return value;
}

tTJSVariant getProp(const tTJSVariant &object, const tjs_char *name) {
    REQUIRE(object.Type() == tvtObject);
    iTJSDispatch2 *dispatch = object.AsObjectNoAddRef();
    REQUIRE(dispatch != nullptr);

    tTJSVariant value;
    REQUIRE(
        TJS_SUCCEEDED(dispatch->PropGet(0, name, nullptr, &value, dispatch)));
    return value;
}

void setProp(iTJSDispatch2 *dispatch, const tjs_char *name,
             const tTJSVariant &value) {
    REQUIRE(dispatch != nullptr);
    REQUIRE(TJS_SUCCEEDED(
        dispatch->PropSet(TJS_MEMBERENSURE, name, nullptr, &value, dispatch)));
}

class ScenarioLoadCallback : public tTJSDispatch {
public:
    explicit ScenarioLoadCallback(ttstr scenario) : Scenario(std::move(scenario)) {}

    tjs_error FuncCall(tjs_uint32, const tjs_char *, tjs_uint32 *,
                       tTJSVariant *result, tjs_int, tTJSVariant **,
                       iTJSDispatch2 *) override {
        if(result)
            *result = Scenario;
        return TJS_S_OK;
    }

private:
    ttstr Scenario;
};

tTJSVariant getIndex(const tTJSVariant &object, tjs_int index) {
    REQUIRE(object.Type() == tvtObject);
    iTJSDispatch2 *dispatch = object.AsObjectNoAddRef();
    REQUIRE(dispatch != nullptr);

    tTJSVariant value;
    REQUIRE(TJS_SUCCEEDED(dispatch->PropGetByNum(0, index, &value, dispatch)));
    return value;
}

} // namespace

TEST_CASE("first-pass compatibility stubs are registered") {
    ensurePluginRegistryRuntime();

    const tjs_char *modules[] = {
        TJS_W("KAGParserEx.dll"),
        TJS_W("ExtKAGParser.dll"),
        TJS_W("extrans.dll"),
        TJS_W("k2compat.dll"),
        TJS_W("kagexopt.dll"),
        TJS_W("krmovie.dll"),
        TJS_W("kztouch.dll"),
        TJS_W("lzfs.dll"),
        TJS_W("dmmcloud.dll"),
        TJS_W("libegl.dll"),
        TJS_W("libglesv2.dll"),
        TJS_W("m2vdec.dll"),
        TJS_W("version.dll"),
        TJS_W("flashPlayer.dll"),
        TJS_W("layerExSubImage.dll"),
        TJS_W("layerExColor.dll"),
        TJS_W("layerExMosaic.dll"),
        TJS_W("layerExSave.dll"),
        TJS_W("gfxEffect.dll"),
        TJS_W("clipboardEx.dll"),
        TJS_W("shellExecute.dll"),
        TJS_W("process.dll"),
        TJS_W("tasktray.dll"),
        TJS_W("adjustMonitor.dll"),
        TJS_W("fpslimit.dll"),
        TJS_W("systemEx.dll"),
        TJS_W("htmlhelp.dll"),
        TJS_W("httprequest.dll"),
        TJS_W("drawdevice.dll"),
        TJS_W("DrawDeviceD2D.dll"),
        TJS_W("DrawDeviceD2Dm.dll"),
        TJS_W("drawdeviceD3D.dll"),
        TJS_W("drawdeviceIrrlicht.dll"),
        TJS_W("drawdeviceOgre.dll"),
        TJS_W("drawdeviceZ_D3D9.dll"),
        TJS_W("gameswf.dll"),
        TJS_W("httpserv.dll"),
        TJS_W("javascript.dll"),
        TJS_W("layerEx.dll"),
        TJS_W("xmlhttprequest.dll"),
        TJS_W("msgreceiver.dll"),
        TJS_W("messenger.dll"),
        TJS_W("oleclass.dll"),
        TJS_W("registory.dll"),
        TJS_W("resourceRW.dll"),
        TJS_W("shrinkCopy.dll"),
        TJS_W("sigcheck.dll"),
        TJS_W("sqlite3_xp3_vfs.dll"),
        TJS_W("stdio.dll"),
        TJS_W("tftSave.dll"),
        TJS_W("videoEncoder.dll"),
        TJS_W("windowExProgress.dll"),
        TJS_W("wmrdump.dll"),
        TJS_W("wsh.dll"),
        TJS_W("wumsadp.dll"),
        TJS_W("wuffmpeg.dll"),
        TJS_W("wuflac.dll"),
        TJS_W("wuopus.dll"),
        TJS_W("layerExAgg.dll"),
        TJS_W("layerExCairo.dll"),
        TJS_W("layerExGdiPlus.dll"),
        TJS_W("magickpp.dll"),
        TJS_W("mkpj.dll"),
        TJS_W("onigruma.dll"),
        TJS_W("squirrel.dll"),
        TJS_W("xpressive.dll"),
        TJS_W("zlib.dll"),
        TJS_W("binaryStream.dll"),
        TJS_W("base64.dll"),
        TJS_W("encode.dll"),
        TJS_W("expat.dll"),
        TJS_W("imagesaver.dll"),
        TJS_W("json.dll"),
        TJS_W("lineParser.dll"),
        TJS_W("memfile.dll"),
        TJS_W("minizip.dll"),
        TJS_W("msbtnhook.dll"),
        TJS_W("qrcode.dll"),
        TJS_W("sqlite3.dll"),
        TJS_W("kirikiroid2.dll"),
        TJS_W("sqlite3_xp3_vfs.dll"),
    };

    for(const auto *module : modules) {
        INFO(ttstr(module).AsStdString());
        CHECK(ncbAutoRegister::HasModule(module));
    }
}

TEST_CASE("krkrsdl3 plugin inventory is available") {
    ensurePluginRegistryRuntime();

    const tjs_char *modules[] = {
        TJS_W("addfont.dll"),
        TJS_W("alphamovie.dll"),
        TJS_W("csvparser.dll"),
        TJS_W("dirlist.dll"),
        TJS_W("drawdeviced3d.dll"),
        TJS_W("emoteplayer.dll"),
        TJS_W("expat.dll"),
        TJS_W("extkagparser.dll"),
        TJS_W("extrans.dll"),
        TJS_W("fftgraph.dll"),
        TJS_W("fstat.dll"),
        TJS_W("getabout.dll"),
        TJS_W("getsample.dll"),
        TJS_W("gfxeffect.dll"),
        TJS_W("json.dll"),
        TJS_W("kagparserex.dll"),
        TJS_W("kirikiroid2.dll"),
        TJS_W("layerexareaaverage.dll"),
        TJS_W("layerexbtoa.dll"),
        TJS_W("layerexdraw.dll"),
        TJS_W("layereximage.dll"),
        TJS_W("layerexmovie.dll"),
        TJS_W("layerexraster.dll"),
        TJS_W("motionplayer.dll"),
        TJS_W("packinone.dll"),
        TJS_W("perspective.dll"),
        TJS_W("psbfile.dll"),
        TJS_W("savestruct.dll"),
        TJS_W("scriptsex.dll"),
        TJS_W("shrinkcopy.dll"),
        TJS_W("sqlite3.dll"),
        TJS_W("textrender.dll"),
        TJS_W("varfile.dll"),
        TJS_W("win32dialog.dll"),
        TJS_W("windowex.dll"),
        TJS_W("wuffmpeg.dll"),
        TJS_W("wuopus.dll"),
        TJS_W("wutcwf.dll"),
        TJS_W("wuvorbis.dll"),
        TJS_W("xp3filter.dll"),
    };

    for(const auto *module : modules) {
        INFO(ttstr(module).AsStdString());
        CHECK(ncbAutoRegister::HasModule(module));
    }
}

TEST_CASE("extNagano transition providers survive a module reload") {
    ensurePluginRegistryRuntime();

    REQUIRE(ncbAutoRegister::LoadModule(TJS_W("extnagano.dll")));
    REQUIRE(ncbAutoRegister::UnloadModule(TJS_W("extnagano.dll")));
    REQUIRE_NOTHROW(
        ncbAutoRegister::LoadModule(TJS_W("extnagano.dll")));
}

TEST_CASE("win32dialog exposes portable dialog template styles") {
    ensurePluginRegistryRuntime();

    REQUIRE(ncbAutoRegister::LoadModule(TJS_W("win32dialog.dll")));
    tTJSVariant result;
    TVPGetScriptEngine()->EvalExpression(
        TJS_W("WIN32Dialog.DS_MODALFRAME | WIN32Dialog.WS_POPUP | "
              "WIN32Dialog.WS_CAPTION | WIN32Dialog.WS_SYSMENU | "
              "WIN32Dialog.DS_SETFONT"),
        &result);
    CHECK(result.AsInteger() == static_cast<tTVInteger>(0x80c800c0u));

    TVPGetScriptEngine()->EvalExpression(
        TJS_W("WIN32Dialog.FW_NORMAL == 400 && "
              "WIN32Dialog.FW_BOLD == 700 && "
              "WIN32Dialog.ICC_BAR_CLASSES == 4 && "
              "WIN32Dialog.initCommonControlsEx(WIN32Dialog.ICC_BAR_CLASSES)"),
        &result);
    CHECK(result.AsInteger() == 1);
}

TEST_CASE("msbtnhook exposes portable mouse-hook initialization") {
    ensurePluginRegistryRuntime();
    TVPExecuteScript(TJS_W("class Window {}"));

    REQUIRE(ncbAutoRegister::LoadModule(TJS_W("msbtnhook.dll")));
    tTJSVariant result;
    TVPGetScriptEngine()->EvalExpression(
        TJS_W("mbXButton1 == 3 && mbXButton2 == 4 && "
              "Window.startMouseHook()"),
        &result);
    CHECK(result.AsInteger() == 1);
}

TEST_CASE("TextRenderBase exposes renderOver as a boolean property") {
    ensurePluginRegistryRuntime();
    REQUIRE(ncbAutoRegister::LoadModule(TJS_W("textrender.dll")));

    tTJSVariant renderClass = getGlobalProp(TJS_W("TextRenderBase"));
    REQUIRE(renderClass.Type() == tvtObject);

    iTJSDispatch2 *renderer = nullptr;
    const tTJSVariantClosure renderClosure =
        renderClass.AsObjectClosureNoAddRef();
    REQUIRE(TJS_SUCCEEDED(renderClosure.CreateNew(
        0, nullptr, nullptr, &renderer, 0, nullptr, nullptr)));
    REQUIRE(renderer != nullptr);

    tTJSVariant renderOver;
    REQUIRE(TJS_SUCCEEDED(renderer->PropGet(
        0, TJS_W("renderOver"), nullptr, &renderOver, renderer)));
    CHECK(renderOver.Type() == tvtInteger);
    CHECK(static_cast<tjs_int>(renderOver) == 0);

    renderer->Release();
}

TEST_CASE("void member access stays safe for transient script layers") {
    ensurePluginRegistryRuntime();

    tTJSVariant result;
    REQUIRE_NOTHROW(
        TVPExecuteExpression(TJS_W("([])[void]"), &result));
    REQUIRE(result.Type() == tvtVoid);

    REQUIRE_NOTHROW(TVPExecuteExpression(
        TJS_W("typeof (([])[void]).offset"), &result));
    REQUIRE(result.Type() == tvtString);
    REQUIRE(ttstr(result) == TJS_W("undefined"));

    REQUIRE_NOTHROW(TVPExecuteExpression(
        TJS_W("typeof (([])[void])[\"offset\"]"), &result));
    REQUIRE(result.Type() == tvtString);
    REQUIRE(ttstr(result) == TJS_W("undefined"));
}

TEST_CASE("legacy compatibility plugins expose observable behavior") {
    ensurePluginRegistryRuntime();

    REQUIRE(ncbAutoRegister::LoadModule(TJS_W("zlib.dll")));
    tTJSVariant version;
    REQUIRE_NOTHROW(TVPExecuteExpression(TJS_W("zlibVersion()"), &version));
    CHECK(ttstr(version).GetLen() > 0);

    tTJSVariant compressFn = getGlobalProp(TJS_W("zlibCompress"));
    tTJSVariant uncompressFn = getGlobalProp(TJS_W("zlibUncompress"));

    tTJSVariant input(TJS_W("AetherKiri"));
    tTJSVariant *compressArgs[] = { &input };
    tTJSVariant compressed;
    REQUIRE(TJS_SUCCEEDED(compressFn.AsObjectClosureNoAddRef().FuncCall(
        0, nullptr, nullptr, &compressed, 1, compressArgs, nullptr)));
    REQUIRE(compressed.Type() == tvtOctet);

    tTJSVariant expectedSize(static_cast<tTVInteger>(64));
    tTJSVariant *uncompressArgs[] = { &compressed, &expectedSize };
    tTJSVariant roundTrip;
    REQUIRE(TJS_SUCCEEDED(uncompressFn.AsObjectClosureNoAddRef().FuncCall(
        0, nullptr, nullptr, &roundTrip, 2, uncompressArgs, nullptr)));
    REQUIRE(roundTrip.Type() == tvtOctet);

    tTJSVariantOctet *octet = roundTrip.AsOctetNoAddRef();
    REQUIRE(octet->GetLength() == 10);
    CHECK(std::memcmp(octet->GetData(), "AetherKiri", 10) == 0);

    REQUIRE(ncbAutoRegister::LoadModule(TJS_W("version.dll")));
    tTJSVariant versionClass = getGlobalProp(TJS_W("Version"));
    CHECK(versionClass.Type() == tvtObject);

    REQUIRE(ncbAutoRegister::LoadModule(TJS_W("kztouch.dll")));
    tTJSVariant touchClass = getGlobalProp(TJS_W("KZTouch"));
    CHECK(touchClass.Type() == tvtObject);
}

TEST_CASE("plugin load mode defaults to krkrsdl3 and can select all modules") {
    TVPSetPluginLoadMode(TJS_W("invalid"));
    CHECK(TVPIsKrkrsdl3PluginLoadMode());
    CHECK_FALSE(TVPIsAetherAllPluginLoadMode());

    TVPSetPluginLoadMode(TJS_W("aether_all"));
    CHECK(TVPIsAetherAllPluginLoadMode());
    CHECK_FALSE(TVPIsKrkrsdl3PluginLoadMode());

    TVPSetPluginLoadMode(TJS_W("krkrsdl3"));
    CHECK(TVPGetPluginLoadMode() == TJS_W("krkrsdl3"));
}

TEST_CASE("KAGParserEx getNextTag returns ordered taglist") {
    ensurePluginRegistryRuntime();
    ncbAutoRegister::UnloadModule(TJS_W("KAGParserEx.dll"));

    iTJSDispatch2 *global = TVPGetScriptDispatch();
    REQUIRE(global != nullptr);
    global->DeleteMember(0, TJS_W("KAGParser"), nullptr, global);

    REQUIRE(ncbAutoRegister::LoadModule(TJS_W("KAGParserEx.dll")));
    tTJSVariant parserClass = getGlobalProp(TJS_W("KAGParser"));
    REQUIRE(parserClass.Type() == tvtObject);

    iTJSDispatch2 *parser = nullptr;
    tTJSVariantClosure parserClosure = parserClass.AsObjectClosureNoAddRef();
    REQUIRE(TJS_SUCCEEDED(parserClosure.CreateNew(0, nullptr, nullptr, &parser,
                                                  0, nullptr, nullptr)));
    REQUIRE(parser != nullptr);

    ScenarioLoadCallback *callback =
        new ScenarioLoadCallback(TJS_W("[endtrans fade=1000 sync]\n"));
    tTJSVariant callbackValue(callback, callback);
    setProp(parser, TJS_W("onScenarioLoad"), callbackValue);
    callback->Release();

    tTJSVariant storage(TJS_W("memory.ks"));
    tTJSVariant *loadArgs[] = { &storage };
    REQUIRE(TJS_SUCCEEDED(parser->FuncCall(0, TJS_W("loadScenario"), nullptr,
                                           nullptr, 1, loadArgs, parser)));

    tTJSVariant tag;
    REQUIRE(TJS_SUCCEEDED(parser->FuncCall(0, TJS_W("getNextTag"), nullptr,
                                           &tag, 0, nullptr, parser)));
    REQUIRE(tag.Type() == tvtObject);
    CHECK(ttstr(getProp(tag, TJS_W("tagname"))) == TJS_W("endtrans"));
    CHECK((tjs_int)getProp(tag, TJS_W("fade")) == 1000);
    CHECK(ttstr(getProp(tag, TJS_W("sync"))) == TJS_W("true"));

    const tTJSVariant taglist = getProp(tag, TJS_W("taglist"));
    REQUIRE(taglist.Type() == tvtObject);
    CHECK(ttstr(getIndex(taglist, 0)) == TJS_W("tagname"));
    CHECK(ttstr(getIndex(taglist, 1)) == TJS_W("fade"));
    CHECK(ttstr(getIndex(taglist, 2)) == TJS_W("sync"));

    // taglist is native parser metadata: it must remain directly accessible
    // without leaking into the dictionary enumeration used by KAG scripts.
    iTJSDispatch2 *enumerated = TJSCreateArrayObject();
    REQUIRE(enumerated != nullptr);
    tTJSVariant *assignArgs[] = { &tag };
    REQUIRE(TJS_SUCCEEDED(enumerated->FuncCall(
        0, TJS_W("assign"), nullptr, nullptr, 1, assignArgs, enumerated)));
    tTJSVariant enumeratedValue(enumerated, enumerated);
    const tjs_int enumeratedCount =
        static_cast<tjs_int>(getProp(enumeratedValue, TJS_W("count")));
    CHECK(enumeratedCount == 6);
    for(tjs_int i = 0; i < enumeratedCount; i += 2)
        CHECK(ttstr(getIndex(enumeratedValue, i)) != TJS_W("taglist"));
    enumerated->Release();

    parser->Release();
    REQUIRE(ncbAutoRegister::UnloadModule(TJS_W("KAGParserEx.dll")));
    global->DeleteMember(0, TJS_W("KAGParser"), nullptr, global);
    global->DeleteMember(0, TJS_W("AetherKiriKAGParserEx"), nullptr, global);
    global->Release();
}

TEST_CASE("KAGParserEx preserves wildcard macro arguments in returned tags") {
    ensurePluginRegistryRuntime();
    ncbAutoRegister::UnloadModule(TJS_W("KAGParserEx.dll"));

    iTJSDispatch2 *global = TVPGetScriptDispatch();
    REQUIRE(global != nullptr);
    global->DeleteMember(0, TJS_W("KAGParser"), nullptr, global);

    REQUIRE(ncbAutoRegister::LoadModule(TJS_W("KAGParserEx.dll")));
    tTJSVariant parserClass = getGlobalProp(TJS_W("KAGParser"));
    REQUIRE(parserClass.Type() == tvtObject);

    iTJSDispatch2 *parser = nullptr;
    tTJSVariantClosure parserClosure = parserClass.AsObjectClosureNoAddRef();
    REQUIRE(TJS_SUCCEEDED(parserClosure.CreateNew(0, nullptr, nullptr, &parser,
                                                  0, nullptr, nullptr)));
    REQUIRE(parser != nullptr);

    ScenarioLoadCallback *callback = new ScenarioLoadCallback(
        TJS_W("[macro name=forward]\n"
              "[ev fixed=before * tail=after]\n"
              "[endmacro]\n"
              "[forward file=ev_cg002_02s.l2d "
              "motion=ev_mv002_02_00 loop]\n"));
    tTJSVariant callbackValue(callback, callback);
    setProp(parser, TJS_W("onScenarioLoad"), callbackValue);
    setProp(parser, TJS_W("ignoreCR"),
            tTJSVariant(static_cast<tTVInteger>(1)));
    callback->Release();

    tTJSVariant storage(TJS_W("memory.ks"));
    tTJSVariant *loadArgs[] = { &storage };
    REQUIRE(TJS_SUCCEEDED(parser->FuncCall(0, TJS_W("loadScenario"), nullptr,
                                           nullptr, 1, loadArgs, parser)));

    tTJSVariant tag;
    REQUIRE(TJS_SUCCEEDED(parser->FuncCall(0, TJS_W("getNextTag"), nullptr,
                                           &tag, 0, nullptr, parser)));
    REQUIRE(tag.Type() == tvtObject);
    CHECK(ttstr(getProp(tag, TJS_W("tagname"))) == TJS_W("ev"));
    CHECK(ttstr(getProp(tag, TJS_W("fixed"))) == TJS_W("before"));
    CHECK(ttstr(getProp(tag, TJS_W("file"))) ==
          TJS_W("ev_cg002_02s.l2d"));
    CHECK(ttstr(getProp(tag, TJS_W("motion"))) ==
          TJS_W("ev_mv002_02_00"));
    CHECK(ttstr(getProp(tag, TJS_W("loop"))) == TJS_W("true"));
    CHECK(ttstr(getProp(tag, TJS_W("tail"))) == TJS_W("after"));

    const tTJSVariant taglist = getProp(tag, TJS_W("taglist"));
    REQUIRE(taglist.Type() == tvtObject);
    CHECK(static_cast<tjs_int>(getProp(taglist, TJS_W("count"))) == 6);
    CHECK(ttstr(getIndex(taglist, 0)) == TJS_W("tagname"));
    CHECK(ttstr(getIndex(taglist, 1)) == TJS_W("fixed"));
    CHECK(ttstr(getIndex(taglist, 2)) == TJS_W("file"));
    CHECK(ttstr(getIndex(taglist, 3)) == TJS_W("motion"));
    CHECK(ttstr(getIndex(taglist, 4)) == TJS_W("loop"));
    CHECK(ttstr(getIndex(taglist, 5)) == TJS_W("tail"));

    parser->Release();
    REQUIRE(ncbAutoRegister::UnloadModule(TJS_W("KAGParserEx.dll")));
    global->DeleteMember(0, TJS_W("KAGParser"), nullptr, global);
    global->DeleteMember(0, TJS_W("AetherKiriKAGParserEx"), nullptr, global);
    global->Release();
}

TEST_CASE("KAGParserEx copyTag clones tags for conductor queues") {
    ensurePluginRegistryRuntime();
    ncbAutoRegister::UnloadModule(TJS_W("KAGParserEx.dll"));

    iTJSDispatch2 *global = TVPGetScriptDispatch();
    REQUIRE(global != nullptr);
    global->DeleteMember(0, TJS_W("KAGParser"), nullptr, global);

    REQUIRE(ncbAutoRegister::LoadModule(TJS_W("KAGParserEx.dll")));
    tTJSVariant parserClass = getGlobalProp(TJS_W("KAGParser"));
    REQUIRE(parserClass.Type() == tvtObject);

    iTJSDispatch2 *parser = nullptr;
    tTJSVariantClosure parserClosure = parserClass.AsObjectClosureNoAddRef();
    REQUIRE(TJS_SUCCEEDED(parserClosure.CreateNew(0, nullptr, nullptr, &parser,
                                                  0, nullptr, nullptr)));
    REQUIRE(parser != nullptr);

    iTJSDispatch2 *source = TJSCreateDictionaryObject();
    REQUIRE(source != nullptr);
    setProp(source, TJS_W("fade"), tTJSVariant(static_cast<tTVInteger>(1000)));
    setProp(source, TJS_W("sync"), tTJSVariant(static_cast<tTVInteger>(1)));

    tTJSVariant tagName(TJS_W("endtrans"));
    tTJSVariant sourceValue(source, source);
    tTJSVariant *copyArgs[] = { &tagName, &sourceValue };
    tTJSVariant copied;
    REQUIRE(TJS_SUCCEEDED(parser->FuncCall(0, TJS_W("copyTag"), nullptr,
                                           &copied, 2, copyArgs, parser)));

    REQUIRE(copied.Type() == tvtObject);
    CHECK(ttstr(getProp(copied, TJS_W("tagname"))) == TJS_W("endtrans"));
    CHECK((tjs_int)getProp(copied, TJS_W("fade")) == 1000);
    CHECK((tjs_int)getProp(copied, TJS_W("sync")) == 1);
    CHECK(copied.AsObjectNoAddRef() != source);

    const tTJSVariant taglist = getProp(copied, TJS_W("taglist"));
    REQUIRE(taglist.Type() == tvtObject);
    CHECK(ttstr(getIndex(taglist, 0)) == TJS_W("tagname"));
    CHECK(ttstr(getIndex(taglist, 1)) == TJS_W("fade"));
    CHECK(ttstr(getIndex(taglist, 2)) == TJS_W("sync"));

    parser->Release();
    source->Release();
    REQUIRE(ncbAutoRegister::UnloadModule(TJS_W("KAGParserEx.dll")));
    global->DeleteMember(0, TJS_W("KAGParser"), nullptr, global);
    global->DeleteMember(0, TJS_W("AetherKiriKAGParserEx"), nullptr, global);
    global->Release();
}

TEST_CASE("KAGParserEx preserves existing script KAGParser class") {
    ensurePluginRegistryRuntime();
    ncbAutoRegister::UnloadModule(TJS_W("KAGParserEx.dll"));

    iTJSDispatch2 *global = TVPGetScriptDispatch();
    REQUIRE(global != nullptr);

    iTJSDispatch2 *original = TJSCreateDictionaryObject();
    REQUIRE(original != nullptr);
    const tTJSVariant originalValue(original, original);
    setProp(global, TJS_W("KAGParser"), originalValue);
    original->Release();

    REQUIRE(ncbAutoRegister::LoadModule(TJS_W("KAGParserEx.dll")));

    const tTJSVariant preserved = getGlobalProp(TJS_W("KAGParser"));
    REQUIRE(preserved.Type() == tvtObject);
    CHECK(preserved.AsObjectNoAddRef() == original);

    const tTJSVariant marker = getGlobalProp(TJS_W("AetherKiriKAGParserEx"));
    CHECK(static_cast<bool>(getProp(marker, TJS_W("loaded"))));
    CHECK(ttstr(getProp(marker, TJS_W("mode"))) == TJS_W("precise"));

    REQUIRE(ncbAutoRegister::UnloadModule(TJS_W("KAGParserEx.dll")));

    const tTJSVariant restored = getGlobalProp(TJS_W("KAGParser"));
    REQUIRE(restored.Type() == tvtObject);
    CHECK(restored.AsObjectNoAddRef() == original);

    global->DeleteMember(0, TJS_W("KAGParser"), nullptr, global);
    global->DeleteMember(0, TJS_W("AetherKiriKAGParserEx"), nullptr, global);
    global->Release();
}

TEST_CASE("ExtKAGParser preserves native local and parameter macro semantics") {
    ensurePluginRegistryRuntime();
    ncbAutoRegister::UnloadModule(TJS_W("ExtKAGParser.dll"));

    iTJSDispatch2 *global = TVPGetScriptDispatch();
    REQUIRE(global != nullptr);

    iTJSDispatch2 *original = TJSCreateDictionaryObject();
    REQUIRE(original != nullptr);
    const tTJSVariant originalValue(original, original);
    setProp(global, TJS_W("KAGParser"), originalValue);
    original->Release();

    REQUIRE(ncbAutoRegister::LoadModule(TJS_W("ExtKAGParser.dll")));
    tTJSVariant parserClass = getGlobalProp(TJS_W("KAGParser"));
    REQUIRE(parserClass.Type() == tvtObject);
    CHECK(parserClass.AsObjectNoAddRef() != original);

    iTJSDispatch2 *parser = nullptr;
    tTJSVariantClosure parserClosure = parserClass.AsObjectClosureNoAddRef();
    REQUIRE(TJS_SUCCEEDED(parserClosure.CreateNew(0, nullptr, nullptr, &parser,
                                                  0, nullptr, nullptr)));
    REQUIRE(parser != nullptr);

    const tTJSVariant parserValue(parser, parser);
    CHECK(static_cast<tjs_int>(getProp(parserValue, TJS_W("localVariablesDepth"))) == 1);
    CHECK(getProp(parserValue, TJS_W("lf")).Type() == tvtObject);
    CHECK(getProp(parserValue, TJS_W("localVariables")).Type() == tvtObject);

    ScenarioLoadCallback *callback = new ScenarioLoadCallback(
        TJS_W("[pushlocalvar answer=42]\n"
              "[probe value=&lf.answer]\n"
              "[poplocalvar]\n"
              "[pmacro name=dr id=right]\n"
              "[pmacro name=dr1 dr=1]\n"
              "[stc ch=10 dr1]\n"));
    tTJSVariant callbackValue(callback, callback);
    setProp(parser, TJS_W("onScenarioLoad"), callbackValue);
    setProp(parser, TJS_W("ignoreCR"),
            tTJSVariant(static_cast<tTVInteger>(1)));
    callback->Release();

    tTJSVariant storage(TJS_W("memory.ks"));
    tTJSVariant *loadArgs[] = { &storage };
    REQUIRE(TJS_SUCCEEDED(parser->FuncCall(0, TJS_W("loadScenario"), nullptr,
                                           nullptr, 1, loadArgs, parser)));

    tTJSVariant tag;
    REQUIRE(TJS_SUCCEEDED(parser->FuncCall(0, TJS_W("getNextTag"), nullptr,
                                           &tag, 0, nullptr, parser)));
    REQUIRE(tag.Type() == tvtObject);
    CHECK(ttstr(getProp(tag, TJS_W("tagname"))) == TJS_W("probe"));
    CHECK(static_cast<tjs_int>(getProp(tag, TJS_W("value"))) == 42);
    CHECK(static_cast<tjs_int>(getProp(parserValue, TJS_W("localVariablesDepth"))) == 2);

    tag.Clear();
    REQUIRE(TJS_SUCCEEDED(parser->FuncCall(0, TJS_W("getNextTag"), nullptr,
                                           &tag, 0, nullptr, parser)));
    REQUIRE(tag.Type() == tvtObject);
    CHECK(ttstr(getProp(tag, TJS_W("tagname"))) == TJS_W("stc"));
    CHECK(ttstr(getProp(tag, TJS_W("ch"))) == TJS_W("10"));
    CHECK(ttstr(getProp(tag, TJS_W("dr"))) == TJS_W("1"));
    CHECK(getProp(tag, TJS_W("id")).Type() == tvtVoid);
    CHECK(static_cast<tjs_int>(
              getProp(parserValue, TJS_W("localVariablesDepth"))) == 1);

    parser->Release();
    parserClass.Clear();
    REQUIRE(ncbAutoRegister::UnloadModule(TJS_W("ExtKAGParser.dll")));

    const tTJSVariant restored = getGlobalProp(TJS_W("KAGParser"));
    REQUIRE(restored.Type() == tvtObject);
    CHECK(restored.AsObjectNoAddRef() == original);

    global->DeleteMember(0, TJS_W("KAGParser"), nullptr, global);
    global->Release();
}

TEST_CASE("ExtKAGParser restores legacy core parser call frames") {
    ensurePluginRegistryRuntime();
    ncbAutoRegister::UnloadModule(TJS_W("ExtKAGParser.dll"));

    iTJSDispatch2 *global = TVPGetScriptDispatch();
    REQUIRE(global != nullptr);
    global->DeleteMember(0, TJS_W("KAGParser"), nullptr, global);

    REQUIRE(ncbAutoRegister::LoadModule(TJS_W("ExtKAGParser.dll")));
    tTJSVariant parserClass = getGlobalProp(TJS_W("KAGParser"));
    REQUIRE(parserClass.Type() == tvtObject);

    const ttstr scenario =
        TJS_W("[call target=*sub]\n"
              "[after]\n"
              "[s]\n"
              "*sub\n"
              "[inside]\n"
              "[return]\n");

    auto createParser = [&]() {
        iTJSDispatch2 *parser = nullptr;
        tTJSVariantClosure parserClosure = parserClass.AsObjectClosureNoAddRef();
        REQUIRE(TJS_SUCCEEDED(parserClosure.CreateNew(
            0, nullptr, nullptr, &parser, 0, nullptr, nullptr)));
        REQUIRE(parser != nullptr);

        ScenarioLoadCallback *callback = new ScenarioLoadCallback(scenario);
        tTJSVariant callbackValue(callback, callback);
        setProp(parser, TJS_W("onScenarioLoad"), callbackValue);
        setProp(parser, TJS_W("ignoreCR"),
                tTJSVariant(static_cast<tTVInteger>(1)));
        callback->Release();
        return parser;
    };

    iTJSDispatch2 *sourceParser = createParser();
    tTJSVariant storage(TJS_W("memory.ks"));
    tTJSVariant *loadArgs[] = { &storage };
    REQUIRE(TJS_SUCCEEDED(sourceParser->FuncCall(
        0, TJS_W("loadScenario"), nullptr, nullptr, 1, loadArgs, sourceParser)));

    tTJSVariant tag;
    REQUIRE(TJS_SUCCEEDED(sourceParser->FuncCall(
        0, TJS_W("getNextTag"), nullptr, &tag, 0, nullptr, sourceParser)));
    REQUIRE(ttstr(getProp(tag, TJS_W("tagname"))) == TJS_W("inside"));

    tTJSVariant legacyState;
    REQUIRE(TJS_SUCCEEDED(sourceParser->FuncCall(
        0, TJS_W("store"), nullptr, &legacyState, 0, nullptr, sourceParser)));
    REQUIRE(legacyState.Type() == tvtObject);
    iTJSDispatch2 *state = legacyState.AsObjectNoAddRef();
    REQUIRE(state != nullptr);

    // Match the state shape emitted by Aether's former core KAGParser.
    state->DeleteMember(0, TJS_W("LocalVariables"), nullptr, state);
    state->DeleteMember(0, TJS_W("whileStack"), nullptr, state);
    state->DeleteMember(0, TJS_W("WhileLevelExp"), nullptr, state);
    state->DeleteMember(0, TJS_W("WhileLevelEach"), nullptr, state);
    const tTJSVariant callStack = getProp(legacyState, TJS_W("callStack"));
    const tTJSVariant callFrame = getIndex(callStack, 0);
    iTJSDispatch2 *frame = callFrame.AsObjectNoAddRef();
    REQUIRE(frame != nullptr);
    frame->DeleteMember(0, TJS_W("WhileStackDepth"), nullptr, frame);
    frame->DeleteMember(0, TJS_W("LocalVariablesCount"), nullptr, frame);

    iTJSDispatch2 *restoredParser = createParser();
    tTJSVariant *restoreArgs[] = { &legacyState };
    REQUIRE(TJS_SUCCEEDED(restoredParser->FuncCall(
        0, TJS_W("restore"), nullptr, nullptr, 1, restoreArgs,
        restoredParser)));

    tag.Clear();
    REQUIRE(TJS_SUCCEEDED(restoredParser->FuncCall(
        0, TJS_W("getNextTag"), nullptr, &tag, 0, nullptr,
        restoredParser)));
    REQUIRE(ttstr(getProp(tag, TJS_W("tagname"))) == TJS_W("inside"));

    tag.Clear();
    REQUIRE(TJS_SUCCEEDED(restoredParser->FuncCall(
        0, TJS_W("getNextTag"), nullptr, &tag, 0, nullptr,
        restoredParser)));
    REQUIRE(ttstr(getProp(tag, TJS_W("tagname"))) == TJS_W("after"));
    const tTJSVariant restoredValue(restoredParser, restoredParser);
    CHECK(static_cast<tjs_int>(getProp(
              restoredValue, TJS_W("localVariablesDepth"))) == 1);

    restoredParser->Release();
    sourceParser->Release();
    parserClass.Clear();
    REQUIRE(ncbAutoRegister::UnloadModule(TJS_W("ExtKAGParser.dll")));
    global->DeleteMember(0, TJS_W("KAGParser"), nullptr, global);
    global->Release();
}

TEST_CASE("extrans registers precise wave transition provider") {
    ensurePluginRegistryRuntime();
    ncbAutoRegister::UnloadModule(TJS_W("extrans.dll"));

    REQUIRE(ncbAutoRegister::LoadModule(TJS_W("extrans.dll")));

    iTVPTransHandlerProvider *provider =
        TVPFindTransHandlerProvider(TJS_W("wave"));
    REQUIRE(provider != nullptr);

    const tjs_char *providerName = nullptr;
    REQUIRE(TJS_SUCCEEDED(provider->GetName(&providerName)));
    CHECK(ttstr(providerName) == TJS_W("wave"));

    iTJSDispatch2 *optionsObject = TJSCreateDictionaryObject();
    REQUIRE(optionsObject != nullptr);
    const tTJSVariant timeValue(static_cast<tTVInteger>(100));
    setProp(optionsObject, TJS_W("time"), timeValue);

    tTVPSimpleOptionProvider options(
        tTJSVariantClosure(optionsObject, optionsObject));
    optionsObject->Release();

    tTVPTransType type = ttSimple;
    tTVPTransUpdateType updateType = tutDivisibleFade;
    iTVPBaseTransHandler *handler = nullptr;
    REQUIRE(provider->StartTransition(&options, &TVPSimpleImageProvider,
                                      ltOpaque, 16, 16, 16, 16, &type,
                                      &updateType, &handler) == TJS_S_OK);
    REQUIRE(handler != nullptr);
    CHECK(type == ttExchange);
    CHECK(updateType == tutDivisible);

    handler->Release();
    provider->Release();
    REQUIRE(ncbAutoRegister::UnloadModule(TJS_W("extrans.dll")));
}
