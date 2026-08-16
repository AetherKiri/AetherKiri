#include "tp_stub.h"
#include "ncbind.hpp"
#include "KAGParser.h"
#include "ScriptMgnIntf.h"

namespace {

tjs_int g_linkCount = 0;
bool g_installedKAGParser = false;

void SetProp(iTJSDispatch2 *target, const tjs_char *name, tTJSVariant value) {
    if(!target || !name)
        return;
    target->PropSet(TJS_MEMBERENSURE, name, nullptr, &value, target);
}

void SetKAGParserMarker(bool loaded) {
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(!global)
        return;

    iTJSDispatch2 *dict = TJSCreateDictionaryObject();
    if(dict) {
        SetProp(dict, TJS_W("loaded"), tTJSVariant(loaded));
        SetProp(dict, TJS_W("mode"), tTJSVariant(TJS_W("precise")));
        tTJSVariant dictValue(dict, dict);
        global->PropSet(TJS_MEMBERENSURE, TJS_W("AetherKiriKAGParserEx"),
                        nullptr, &dictValue, global);
        dict->Release();
    }

    tTJSVariant kagParser;
    if(TJS_SUCCEEDED(global->PropGet(0, TJS_W("KAGParser"), nullptr,
                                     &kagParser, global)) &&
       kagParser.Type() == tvtObject && kagParser.AsObjectNoAddRef()) {
        iTJSDispatch2 *kag = kagParser.AsObjectNoAddRef();
        SetProp(kag, TJS_W("aetherKiriKAGParserEx"), tTJSVariant(loaded));
        SetProp(kag, TJS_W("kagParserExCompatible"), tTJSVariant(loaded));
    }

    global->Release();
}

void InstallKAGParserClass() {
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(!global)
        return;

    if(g_linkCount == 0) {
        tTJSVariant current;
        const bool hasKAGParser =
            TJS_SUCCEEDED(global->PropGet(0, TJS_W("KAGParser"), nullptr,
                                          &current, global)) &&
            current.Type() == tvtObject && current.AsObjectNoAddRef();

        if(!hasKAGParser) {
            iTJSDispatch2 *replacement = TVPCreateNativeClass_KAGParser();
            if(replacement) {
                tTJSVariant replacementValue(replacement, replacement);
                global->PropSet(TJS_MEMBERENSURE, TJS_W("KAGParser"), nullptr,
                                &replacementValue, global);
                replacement->Release();
                g_installedKAGParser = true;
            }
        }
    }

    ++g_linkCount;
    global->Release();
}

void RestoreKAGParserClass() {
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(!global)
        return;

    if(g_linkCount > 0)
        --g_linkCount;

    if(g_linkCount == 0 && g_installedKAGParser) {
        global->DeleteMember(0, TJS_W("KAGParser"), nullptr, global);
        g_installedKAGParser = false;
    }

    global->Release();
}

} // namespace

static void LinkKAGParserCompatibility() {
    InstallKAGParserClass();
    SetKAGParserMarker(true);
}

static void UnlinkKAGParserCompatibility() {
    SetKAGParserMarker(false);
    RestoreKAGParserClass();
}

// KAGParserEx.dll compatibility uses Aether's extended core parser.  The
// distinct ExtKAGParser.dll implementation is registered by extkagparser.
static ncbCallbackAutoRegister g_kagparserex_cb(
    TJS_W("KAGParserEx.dll"), ncbAutoRegister::PreRegist,
    &LinkKAGParserCompatibility, nullptr);
static ncbCallbackAutoRegister g_kagparserex_unload_cb(
    TJS_W("KAGParserEx.dll"), ncbAutoRegister::PostRegist, nullptr,
    &UnlinkKAGParserCompatibility);

extern "C" void TVPRegisterKAGParserExPluginAnchor() {}
