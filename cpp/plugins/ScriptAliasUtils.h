#ifndef AETHERKIRI_SCRIPT_ALIAS_UTILS_H
#define AETHERKIRI_SCRIPT_ALIAS_UTILS_H

#include "ScriptMgnIntf.h"

namespace aetherkiri::plugins {

inline bool EnsureScriptMember(const tTJSVariant &target,
                               const tjs_char *member,
                               const tTJSVariant &value) {
    if(target.Type() != tvtObject || !member)
        return false;

    tTJSVariantClosure closure = target.AsObjectClosureNoAddRef();
    if(!closure.Object)
        return false;

    iTJSDispatch2 *objThis = closure.ObjThis ? closure.ObjThis : closure.Object;
    tTJSVariant existing;
    if(TJS_SUCCEEDED(closure.Object->PropGet(
           TJS_MEMBERMUSTEXIST | TJS_IGNOREPROP, member, nullptr, &existing,
           objThis))) {
        return true;
    }

    return TJS_SUCCEEDED(closure.Object->PropSet(
        TJS_MEMBERENSURE | TJS_IGNOREPROP, member, nullptr, &value, objThis));
}

inline bool EnsureGlobalObjectMember(const tjs_char *globalName,
                                     const tjs_char *member,
                                     const tTJSVariant &value) {
    tTJS *engine = TVPGetScriptEngine();
    if(!engine || !globalName || !member)
        return false;

    iTJSDispatch2 *global = engine->GetGlobalNoAddRef();
    if(!global)
        return false;

    tTJSVariant target;
    if(TJS_FAILED(global->PropGet(0, globalName, nullptr, &target, global)))
        return false;

    bool installed = EnsureScriptMember(target, member, value);

    tTJSVariant prototype;
    tTJSVariantClosure closure = target.AsObjectClosureNoAddRef();
    if(closure.Object &&
       TJS_SUCCEEDED(closure.Object->PropGet(
           TJS_IGNOREPROP, TJS_W("prototype"), nullptr, &prototype,
           closure.ObjThis ? closure.ObjThis : closure.Object))) {
        installed = EnsureScriptMember(prototype, member, value) || installed;
    }

    return installed;
}

} // namespace aetherkiri::plugins

#endif
