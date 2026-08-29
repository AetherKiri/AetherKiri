#include <catch2/catch_test_macros.hpp>

#include "PluginImpl.h"
#include "ScriptMgnIntf.h"
#include "SystemIntf.h"
#include "tjsArray.h"
#include "tjsDictionary.h"
#include "ncbind.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

extern tTJS *TVPScriptEngine;

namespace {

void ensureRuntime() {
    if(TVPGetScriptEngine() == nullptr)
        TVPScriptEngine = new tTJS();

    // The plugin test binary shares one script world across cases, and some
    // cases intentionally create a bare TJS engine first. Mirror the product
    // bootstrap so this test is independent of Catch2's ordering.
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    REQUIRE(global != nullptr);
    tTJSVariant existingSystem;
    if(TJS_FAILED(global->PropGet(0, TJS_W("System"), nullptr,
                                  &existingSystem, global))) {
        iTJSDispatch2 *systemClass = TVPCreateNativeClass_System();
        REQUIRE(systemClass != nullptr);
        tTJSVariant value(systemClass);
        systemClass->Release();
        REQUIRE(TJS_SUCCEEDED(global->PropSet(
            TJS_MEMBERENSURE | TJS_IGNOREPROP, TJS_W("System"), nullptr,
            &value, global)));
    }
    global->Release();
    ncbAutoRegister::AllRegist();
    REQUIRE(ncbAutoRegister::LoadModule(TJS_W("tftSave.dll")));
}

tTJSVariant getGlobal(const tjs_char *name) {
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    REQUIRE(global != nullptr);
    tTJSVariant value;
    REQUIRE(TJS_SUCCEEDED(global->PropGet(0, name, nullptr, &value, global)));
    global->Release();
    return value;
}

void setProp(iTJSDispatch2 *object, const tjs_char *name,
             const tTJSVariant &value) {
    REQUIRE(object != nullptr);
    REQUIRE(TJS_SUCCEEDED(object->PropSet(TJS_MEMBERENSURE, name, nullptr,
                                         &value, object)));
}

iTJSDispatch2 *makeCharacters() {
    iTJSDispatch2 *array = TJSCreateArrayObject();
    REQUIRE(array != nullptr);
    const tTJSVariant first(static_cast<tjs_int>(0x41));
    const tTJSVariant second(static_cast<tjs_int>(0x3042));
    REQUIRE(TJS_SUCCEEDED(
        array->PropSetByNum(TJS_MEMBERENSURE, 0, &first, array)));
    REQUIRE(TJS_SUCCEEDED(
        array->PropSetByNum(TJS_MEMBERENSURE, 1, &second, array)));
    return array;
}

class GlyphCallback final : public tTJSDispatch {
public:
    explicit GlyphCallback(bool modify = false) : modify_(modify) {}

    tjs_error FuncCall(tjs_uint32, const tjs_char *, tjs_uint32 *,
                       tTJSVariant *result, tjs_int numparams,
                       tTJSVariant **params, iTJSDispatch2 *) override {
        if(numparams < 1 || !params || !params[0])
            return TJS_E_BADPARAMCOUNT;
        const tjs_int code = static_cast<tjs_int>(*params[0]);
        if(modify_) {
            if(numparams < 2 || !params[1] || params[1]->Type() != tvtObject)
                return TJS_E_BADPARAMCOUNT;
            iTJSDispatch2 *info = params[1]->AsObjectNoAddRef();
            const tTJSVariant origin(static_cast<tjs_int>(code == 0x41 ? 7 : 9));
            setProp(info, TJS_W("origin_x"), origin);
            if(result)
                *result = true;
            return TJS_S_OK;
        }

        iTJSDispatch2 *info = TJSCreateDictionaryObject();
        if(!info)
            return TJS_E_FAIL;
        const tjs_int width = code == 0x41 ? 2 : 1;
        const tjs_int height = code == 0x41 ? 2 : 3;
        setProp(info, TJS_W("blackbox_x"), tTJSVariant(width));
        setProp(info, TJS_W("blackbox_y"), tTJSVariant(height));
        setProp(info, TJS_W("origin_x"), tTJSVariant(1));
        setProp(info, TJS_W("origin_y"), tTJSVariant(-2));
        setProp(info, TJS_W("inc_x"), tTJSVariant(width + 1));
        setProp(info, TJS_W("inc_y"), tTJSVariant(0));
        setProp(info, TJS_W("inc"), tTJSVariant(width + 1));

        std::vector<tjs_uint8> alpha(static_cast<size_t>(width * height));
        for(size_t i = 0; i < alpha.size(); ++i)
            alpha[i] = static_cast<tjs_uint8>((i * 17 + code) & 0xff);
        tTJSVariantOctet *octet = TJSAllocVariantOctet(
            alpha.data(), static_cast<tjs_uint>(alpha.size()));
        if(!octet) {
            info->Release();
            return TJS_E_FAIL;
        }
        tTJSVariant image;
        image = octet;
        setProp(info, TJS_W("image"), image);
        octet->Release();
        if(result)
            *result = tTJSVariant(info, info);
        info->Release();
        return TJS_S_OK;
    }

    std::vector<tjs_int> loadedCodes;
    std::vector<tjs_int> loadedWidths;

private:
    bool modify_ = false;
};

class LoadCallback final : public tTJSDispatch {
public:
    tjs_error FuncCall(tjs_uint32, const tjs_char *, tjs_uint32 *,
                       tTJSVariant *result, tjs_int numparams,
                       tTJSVariant **params, iTJSDispatch2 *) override {
        if(numparams != 2 || !params || !params[0] || !params[1] ||
           params[1]->Type() != tvtObject)
            return TJS_E_BADPARAMCOUNT;
        loadedCodes.push_back(static_cast<tjs_int>(*params[0]));
        tTJSVariant width;
        if(TJS_SUCCEEDED(params[1]->AsObjectNoAddRef()->PropGet(
               0, TJS_W("blackbox_x"), nullptr, &width,
               params[1]->AsObjectNoAddRef())))
            loadedWidths.push_back(static_cast<tjs_int>(width));
        if(result)
            *result = true;
        return TJS_S_OK;
    }

    std::vector<tjs_int> loadedCodes;
    std::vector<tjs_int> loadedWidths;
};

tTJSVariant callSystem(const tTJSVariant &system, const tjs_char *name,
                       tjs_int count, tTJSVariant **params) {
    tTJSVariant function;
    REQUIRE(TJS_SUCCEEDED(system.AsObjectNoAddRef()->PropGet(
        0, name, nullptr, &function, system.AsObjectNoAddRef())));
    REQUIRE(function.Type() == tvtObject);
    tTJSVariant result;
    const tjs_error error = function.AsObjectClosureNoAddRef().FuncCall(
        0, nullptr, nullptr, &result, count, params,
        system.AsObjectNoAddRef());
    REQUIRE(TJS_SUCCEEDED(error));
    return result;
}

} // namespace

TEST_CASE("tftSave round-trips upstream glyph cache and modifies metrics") {
    ensureRuntime();
    const tTJSVariant system = getGlobal(TJS_W("System"));
    REQUIRE(system.Type() == tvtObject);

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "aetherkiri-tft-save-test.tft";
    std::error_code ignored;
    std::filesystem::remove(path, ignored);

    iTJSDispatch2 *characters = makeCharacters();
    auto *saveCallbackObject = new GlyphCallback();
    const tTJSVariant pathValue(ttstr(path.string()));
    const tTJSVariant charactersValue(characters, characters);
    const tTJSVariant callbackValue(saveCallbackObject, saveCallbackObject);
    tTJSVariant *saveParams[] = {
        const_cast<tTJSVariant *>(&pathValue),
        const_cast<tTJSVariant *>(&charactersValue),
        const_cast<tTJSVariant *>(&callbackValue)};
    const tTJSVariant saveResult =
        callSystem(system, TJS_W("savePreRenderedFont"), 3, saveParams);
    CHECK(static_cast<bool>(saveResult));
    saveCallbackObject->Release();
    characters->Release();
    REQUIRE(std::filesystem::exists(path));
    CHECK(std::filesystem::file_size(path) > 36);

    iTJSDispatch2 *loadedCharacters = TJSCreateArrayObject();
    REQUIRE(loadedCharacters != nullptr);
    auto *loadCallbackObject = new LoadCallback();
    const tTJSVariant loadedValue(loadedCharacters, loadedCharacters);
    const tTJSVariant loadCallbackValue(loadCallbackObject, loadCallbackObject);
    tTJSVariant *loadParams[] = {
        const_cast<tTJSVariant *>(&pathValue),
        const_cast<tTJSVariant *>(&loadedValue),
        const_cast<tTJSVariant *>(&loadCallbackValue)};
    const tTJSVariant loadResult =
        callSystem(system, TJS_W("loadPreRenderedFont"), 3, loadParams);
    CHECK(static_cast<bool>(loadResult));
    CHECK(TJSGetArrayElementCount(loadedCharacters) == 2);
    CHECK(loadCallbackObject->loadedCodes == std::vector<tjs_int>{0x41, 0x3042});
    CHECK(loadCallbackObject->loadedWidths == std::vector<tjs_int>{2, 1});
    loadCallbackObject->Release();
    loadedCharacters->Release();

    auto *modifyCallbackObject = new GlyphCallback(true);
    const tTJSVariant modifyCallbackValue(modifyCallbackObject,
                                          modifyCallbackObject);
    tTJSVariant *modifyParams[] = {
        const_cast<tTJSVariant *>(&pathValue),
        const_cast<tTJSVariant *>(&modifyCallbackValue)};
    const tTJSVariant modifyResult = callSystem(
        system, TJS_W("modifyPreRenderedFont"), 2, modifyParams);
    CHECK(static_cast<bool>(modifyResult));
    modifyCallbackObject->Release();

    std::filesystem::remove(path, ignored);
}
