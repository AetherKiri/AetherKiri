#include <catch2/catch_test_macros.hpp>

#include "PluginImpl.h"
#include "ScriptMgnIntf.h"
#include "TransIntf.h"
#include "ncbind.hpp"
#include "tjsDictionary.h"

extern tTJS *TVPScriptEngine;

namespace {

void ensurePluginRegistryRuntime() {
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

} // namespace

TEST_CASE("first-pass compatibility stubs are registered") {
    ensurePluginRegistryRuntime();

    const tjs_char *modules[] = {
        TJS_W("KAGParserEx.dll"),
        TJS_W("ExtKAGParser.dll"),
        TJS_W("extrans.dll"),
        TJS_W("flashPlayer.dll"),
        TJS_W("layerExSubImage.dll"),
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
