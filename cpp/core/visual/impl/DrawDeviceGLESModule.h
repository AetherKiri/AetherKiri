#pragma once
#include "tjs.h"
#include "tjsNative.h"
#include "DebugIntf.h"
#include "ScriptMgnIntf.h"
#include "DrawDevice.h"
#include <dlfcn.h>
#include <unordered_map>

namespace DrawDeviceGLES {

using CreateKrkrGLESModuleObjectFn = tjs_error (*)(tTJSVariant *, tjs_int, tjs_int);
using ModuleName = std::basic_string<tjs_char>;
using ModuleStore = std::unordered_map<uintptr_t, std::unordered_map<ModuleName, tTJSVariant>>;

inline tjs_error TryCreateModuleViaKrkrGLES(tTJSVariant *result, tjs_int width,
                                            tjs_int height) {
    void *sym = dlsym(RTLD_DEFAULT, "TVPKrkrGLESCreateModuleObject");
    if(!sym) return TJS_E_MEMBERNOTFOUND;
    auto fn = reinterpret_cast<CreateKrkrGLESModuleObjectFn>(sym);
    return fn(result, width, height);
}

inline tjs_error ReturnTrueCb(tTJSVariant *result, tjs_int, tTJSVariant **,
                              iTJSDispatch2 *) {
    if(result) *result = true;
    return TJS_S_OK;
}

inline tjs_error ReturnFirstArgOrTrueCb(tTJSVariant *result, tjs_int numparams,
                                        tTJSVariant **param, iTJSDispatch2 *) {
    if(!result) return TJS_S_OK;
    if(numparams > 0 && param) {
        *result = *param[0];
    } else {
        *result = true;
    }
    return TJS_S_OK;
}

inline const tjs_char *VariantTypeName(tTJSVariantType type) {
    switch(type) {
    case tvtVoid: return TJS_W("void");
    case tvtObject: return TJS_W("object");
    case tvtString: return TJS_W("string");
    case tvtOctet: return TJS_W("octet");
    case tvtInteger: return TJS_W("integer");
    case tvtReal: return TJS_W("real");
    default: return TJS_W("unknown");
    }
}

inline void LogCompatArgsOnce(const tjs_char *tag, tjs_int numparams,
                              tTJSVariant **param) {
    static tjs_int logCount = 0;
    if(logCount++ >= 12)
        return;
    ttstr msg = ttstr(TJS_W("DrawDeviceGLESModule.")) + tag +
                TJS_W(": argc=") + ttstr(numparams);
    for(tjs_int i = 0; i < numparams; ++i) {
        msg += TJS_W(" [");
        msg += ttstr(i);
        msg += TJS_W(":");
        msg += (param && param[i]) ? VariantTypeName(param[i]->Type())
                                   : TJS_W("null");
        msg += TJS_W("]");
    }
    TVPAddLog(msg);
}

inline tjs_error EntryUpdateObjectCb(tTJSVariant *result, tjs_int numparams,
                                     tTJSVariant **param, iTJSDispatch2 *) {
    LogCompatArgsOnce(TJS_W("entryUpdateObject"), numparams, param);
    if(result) *result = true;
    return TJS_S_OK;
}

inline tjs_error CopyLayerCb(tTJSVariant *result, tjs_int numparams,
                             tTJSVariant **param, iTJSDispatch2 *) {
    LogCompatArgsOnce(TJS_W("copyLayer"), numparams, param);
    if(result) *result = true;
    return TJS_S_OK;
}

inline tjs_error DrawAffineCb(tTJSVariant *result, tjs_int numparams,
                              tTJSVariant **param, iTJSDispatch2 *) {
    LogCompatArgsOnce(TJS_W("drawAffine"), numparams, param);
    if(result) *result = true;
    return TJS_S_OK;
}

inline tjs_error CreateObjectByExpression(tTJSVariant *result,
                                          const tjs_char *expression) {
    if(!result || !expression) return TJS_S_OK;
    try {
        TVPExecuteExpression(ttstr(expression), result);
    } catch(...) {
        result->Clear();
        return TJS_E_FAIL;
    }
    return TJS_S_OK;
}

inline void InvokeLoadIfPresent(tTJSVariant &object, tjs_int numparams,
                                tTJSVariant **param) {
    if(numparams <= 0 || !param || object.Type() != tvtObject) return;
    iTJSDispatch2 *dispatch = object.AsObjectNoAddRef();
    if(!dispatch) return;
    tjs_uint hint = 0;
    dispatch->FuncCall(0, TJS_W("load"), &hint, nullptr, numparams, param,
                       dispatch);
}

inline tjs_error CreateModelCb(tTJSVariant *result, tjs_int numparams,
                               tTJSVariant **param, iTJSDispatch2 *) {
    tTJSVariant model;
    tjs_error er = CreateObjectByExpression(&model, TJS_W("new Live2DModel()"));
    if(TJS_FAILED(er)) {
        if(result) result->Clear();
        return er;
    }
    InvokeLoadIfPresent(model, numparams, param);
    if(result) *result = model;
    return TJS_S_OK;
}

inline tjs_error CreateMatrixCb(tTJSVariant *result, tjs_int, tTJSVariant **,
                                iTJSDispatch2 *) {
    return CreateObjectByExpression(result, TJS_W("new Live2DMatrix()"));
}

inline tjs_error CreateDeviceCb(tTJSVariant *result, tjs_int, tTJSVariant **,
                                iTJSDispatch2 *) {
    return CreateObjectByExpression(result, TJS_W("new Live2DDevice()"));
}

inline void SetObjectMethod(iTJSDispatch2 *obj, const tjs_char *name,
                            tTJSNativeClassMethodCallback cb) {
    if(!obj || !name || !cb) return;
    iTJSDispatch2 *method = TJSCreateNativeClassMethod(cb);
    if(!method) return;
    tTJSVariant v(method, method);
    obj->PropSet(TJS_MEMBERENSURE, name, nullptr, &v, obj);
    method->Release();
}

inline tjs_error CreateFallbackModuleObject(tTJSVariant *result, tjs_int width,
                                            tjs_int height) {
    iTJSDispatch2 *dict = TJSCreateCustomObject();
    if(!dict) return TJS_E_FAIL;

    tTJSVariant wv(width), hv(height);
    dict->PropSet(TJS_MEMBERENSURE, TJS_W("screenWidth"), nullptr, &wv, dict);
    dict->PropSet(TJS_MEMBERENSURE, TJS_W("screenHeight"), nullptr, &hv, dict);

    SetObjectMethod(dict, TJS_W("entryUpdateObject"), EntryUpdateObjectCb);
    SetObjectMethod(dict, TJS_W("setScreenSize"), ReturnTrueCb);
    SetObjectMethod(dict, TJS_W("makeCurrent"), ReturnTrueCb);
    SetObjectMethod(dict, TJS_W("beginScene"), ReturnTrueCb);
    SetObjectMethod(dict, TJS_W("endScene"), ReturnTrueCb);
    SetObjectMethod(dict, TJS_W("finalize"), ReturnTrueCb);
    SetObjectMethod(dict, TJS_W("render"), ReturnTrueCb);
    SetObjectMethod(dict, TJS_W("glesEntry"), ReturnTrueCb);
    SetObjectMethod(dict, TJS_W("glesRemove"), ReturnTrueCb);
    SetObjectMethod(dict, TJS_W("capture"), ReturnFirstArgOrTrueCb);
    SetObjectMethod(dict, TJS_W("captureScreen"), ReturnFirstArgOrTrueCb);
    SetObjectMethod(dict, TJS_W("glesCapture"), ReturnFirstArgOrTrueCb);
    SetObjectMethod(dict, TJS_W("glesCaptureScreen"), ReturnFirstArgOrTrueCb);
    SetObjectMethod(dict, TJS_W("copyLayer"), CopyLayerCb);
    SetObjectMethod(dict, TJS_W("glesCopyLayer"), CopyLayerCb);
    SetObjectMethod(dict, TJS_W("drawLayer"), ReturnTrueCb);
    SetObjectMethod(dict, TJS_W("glesDrawLayer"), ReturnTrueCb);
    SetObjectMethod(dict, TJS_W("drawAffine"), DrawAffineCb);
    SetObjectMethod(dict, TJS_W("drawAffineGLES"), DrawAffineCb);
    SetObjectMethod(dict, TJS_W("setMatrix"), ReturnTrueCb);
    SetObjectMethod(dict, TJS_W("createModel"), CreateModelCb);
    SetObjectMethod(dict, TJS_W("createMatrix"), CreateMatrixCb);
    SetObjectMethod(dict, TJS_W("createDevice"), CreateDeviceCb);

    if(result) *result = tTJSVariant(dict, dict);
    dict->Release();
    return TJS_S_OK;
}

inline ModuleStore &GetCachedModules() {
    static ModuleStore modules;
    return modules;
}

inline ModuleName NormalizeModuleName(tjs_int numparams, tTJSVariant **param) {
    if(numparams <= 0 || !param || !param[0] || param[0]->Type() == tvtVoid) {
        return TJS_W("live2d");
    }
    ttstr raw(*param[0]);
    ModuleName out(raw.c_str());
    for(auto &ch : out) {
        if(ch >= static_cast<tjs_char>('A') && ch <= static_cast<tjs_char>('Z')) {
            ch = static_cast<tjs_char>(ch + 32);
        }
    }
    if(out.empty()) out = TJS_W("live2d");
    return out;
}

inline void ClearCachedModulesForDevice(void *device) {
    if(!device) return;
    GetCachedModules().erase(reinterpret_cast<uintptr_t>(device));
}

inline void EnsureWindowGLESAdaptor(tTVPDrawDevice *device) {
    if(!device) return;
    iTVPWindow *window = device->GetWindowInterface();
    if(!window) return;
    iTJSDispatch2 *windowObj = window->GetWindowDispatch();
    if(!windowObj) return;

    tTJSVariant adaptor;
    windowObj->PropGet(0, TJS_W("glesAdaptor"), nullptr, &adaptor, windowObj);
}

template<typename DeviceT>
tjs_error GetModuleForDevice(DeviceT *device, tTJSVariant *result,
                             tjs_int numparams, tTJSVariant **param) {
    const ModuleName moduleName = NormalizeModuleName(numparams, param);
    if(moduleName == ModuleName(TJS_W("live2d"))) EnsureWindowGLESAdaptor(device);
    const uintptr_t key = reinterpret_cast<uintptr_t>(device);
    auto &cache = GetCachedModules();
    auto dit = cache.find(key);
    if(dit != cache.end()) {
        auto mit = dit->second.find(moduleName);
        if(mit != dit->second.end() && mit->second.Type() == tvtObject &&
           mit->second.AsObjectNoAddRef() != nullptr) {
            if(result) *result = mit->second;
            return TJS_S_OK;
        }
    }

    tjs_int w = 0, h = 0;
    device->GetSrcSize(w, h);
    tTJSVariant created;
    if(TJS_SUCCEEDED(TryCreateModuleViaKrkrGLES(&created, w, h))) {
        if(created.Type() == tvtObject && created.AsObjectNoAddRef() != nullptr)
            cache[key][moduleName] = created;
        if(result) *result = created;
        return TJS_S_OK;
    }
    if(TJS_SUCCEEDED(CreateFallbackModuleObject(&created, w, h))) {
        if(created.Type() == tvtObject && created.AsObjectNoAddRef() != nullptr)
            cache[key][moduleName] = created;
        if(result) *result = created;
        return TJS_S_OK;
    }
    if(result) result->Clear();
    return TJS_E_FAIL;
}

} // namespace DrawDeviceGLES
