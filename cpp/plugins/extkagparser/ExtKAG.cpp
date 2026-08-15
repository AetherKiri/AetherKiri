#include "tjsCommHead.h"
#include "ncbind.hpp"

#define NCB_MODULE_NAME TJS_W("ExtKAGParser.dll")
#define TVP_KAGPARSER_EX_CLASSNAME TJS_W("KAGParser")

extern tTJSNativeClass* TVPCreateNativeClass_ExtKAGParser();
extern void ExtTVPResetScenarioCache();
static iTJSDispatch2* origKAGParser = nullptr;

void extkagparser_init()
{
    iTJSDispatch2* global = TVPGetScriptDispatch();
    if (global)
    {
        if (origKAGParser)
        {
            origKAGParser->Release();
            origKAGParser = nullptr;
        }
        tTJSVariant val;
        if (TJS_SUCCEEDED(global->PropGet(0, TVP_KAGPARSER_EX_CLASSNAME, nullptr, &val, global)) &&
            val.Type() == tvtObject && val.AsObjectNoAddRef())
        {
            origKAGParser = val.AsObject();
            val.Clear();
        }
        iTJSDispatch2* tjsclass = TVPCreateNativeClass_ExtKAGParser();
        val = tTJSVariant(tjsclass);
        tjsclass->Release();
        global->PropSet(TJS_MEMBERENSURE, TVP_KAGPARSER_EX_CLASSNAME, nullptr, &val, global);
        global->Release();
    }
}

void extkagparser_done()
{
    ExtTVPResetScenarioCache();

    iTJSDispatch2* global = TVPGetScriptDispatch();
    if (global)
    {
        global->DeleteMember(0, TVP_KAGPARSER_EX_CLASSNAME, nullptr, global);
        if (origKAGParser)
        {
            tTJSVariant val(origKAGParser);
            origKAGParser->Release();
            origKAGParser = nullptr;
            global->PropSet(TJS_MEMBERENSURE, TVP_KAGPARSER_EX_CLASSNAME, nullptr, &val, global);
        }
        global->Release();
    }
    else if (origKAGParser)
    {
        origKAGParser->Release();
        origKAGParser = nullptr;
    }
}

NCB_PRE_REGIST_CALLBACK(extkagparser_init);
NCB_POST_UNREGIST_CALLBACK(extkagparser_done);

extern "C" void TVPRegisterExtKAGParserPluginAnchor() {}
