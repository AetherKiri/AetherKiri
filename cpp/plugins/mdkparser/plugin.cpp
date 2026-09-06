#include "PluginStub.h"
#include "ncbind.hpp"

#include "LexicalAnalyzer.h"
#include "MDKParser.h"
#include "ReservedWord.h"

#define NCB_MODULE_NAME TJS_W("MDKParser.dll")

namespace AetherKiri::MDKParser {

namespace {

void SetGlobalMember(iTJSDispatch2 *global, const tjs_char *name,
                     iTJSDispatch2 *value) {
    if(!global || !name || !value)
        return;

    tTJSVariant variant(value, value);
    global->PropSet(TJS_MEMBERENSURE, name, nullptr, &variant, global);
}

void DeleteGlobalMember(const tjs_char *name) {
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(!global)
        return;
    global->DeleteMember(0, name, nullptr, global);
    global->Release();
}

} // namespace

#ifdef TJS_NATIVE_CLASSID_NAME
#undef TJS_NATIVE_CLASSID_NAME
#undef TJS_NCM_REG_THIS
#undef TJS_NATIVE_SET_ClassID
#endif
#define TJS_NCM_REG_THIS classobj
#define TJS_NATIVE_SET_ClassID TJS_NATIVE_CLASSID_NAME = TJS_NCM_CLASSID;
#define TJS_NATIVE_CLASSID_NAME ClassID_MDKParser
static tjs_int32 TJS_NATIVE_CLASSID_NAME = -1;

static iTJSNativeInstance *TJS_INTF_METHOD Create_NI_MDKParser() {
    return new tTJSNI_MDKParser();
}

static iTJSDispatch2 *Create_NC_MDKParser() {
    tTJSNativeClassForPlugin *classobj = TJSCreateNativeClassForPlugin(
        TJS_W("MDKParser"), Create_NI_MDKParser);

    TJS_BEGIN_NATIVE_MEMBERS(MDKParser)

    TJS_DECL_EMPTY_FINALIZE_METHOD

    TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL(_this, tTJSNI_MDKParser, MDKParser) {
        return TJS_S_OK;
    }
    TJS_END_NATIVE_CONSTRUCTOR_DECL(MDKParser)

    TJS_BEGIN_NATIVE_METHOD_DECL(loadScenario) {
        TJS_GET_NATIVE_INSTANCE(_this, tTJSNI_MDKParser);
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        if(result) {
            iTJSDispatch2 *scenario =
                _this->ParseMDKScenario(*param[0]);
            *result = tTJSVariant(scenario, scenario);
            if(scenario)
                scenario->Release();
        }
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(loadScenario)

    TJS_END_NATIVE_MEMBERS

    return classobj;
}

static bool g_initialized = false;

static void InitPlugin_MDKParser() {
    if(g_initialized)
        return;

    InitializeReservedWord();
    TJSReservedWordsHashAddRef();

    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(!global) {
        TJSReservedWordsHashRelease();
        FinalizeReservedWord();
        return;
    }

    iTJSDispatch2 *tjsclass = Create_NC_MDKParser();
    if(tjsclass) {
        SetGlobalMember(global, TJS_W("MDKParser"), tjsclass);
        tjsclass->Release();
        g_initialized = true;
    }
    global->Release();

    if(!g_initialized) {
        TJSReservedWordsHashRelease();
        FinalizeReservedWord();
    }
}

static void UninitPlugin_MDKParser() {
    if(!g_initialized)
        return;

    DeleteGlobalMember(TJS_W("MDKParser"));
    TJSReservedWordsHashRelease();
    FinalizeReservedWord();
    g_initialized = false;
}

NCB_PRE_REGIST_CALLBACK(InitPlugin_MDKParser);
NCB_POST_UNREGIST_CALLBACK(UninitPlugin_MDKParser);

} // namespace AetherKiri::MDKParser
