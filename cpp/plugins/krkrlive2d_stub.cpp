#include "ncbind.hpp"
#include "DebugIntf.h"
#include "ScriptMgnIntf.h"
#include "StorageIntf.h"

#include <limits>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

struct Live2DRenderTarget {
    unsigned int fbo;
    int width;
    int height;
};

Live2DRenderTarget g_live2dRenderTarget = {0, 0, 0};

#define NCB_MODULE_NAME TJS_W("krkrlive2d.dll")

namespace {

#if defined(__EMSCRIPTEN__)
EM_JS(void, AetherKiriLive2DLoadModelBytes,
      (int id, const char *storage, const unsigned char *data, int size), {
          try {
              var name = UTF8ToString(storage);
              var bytes = HEAPU8.slice(data, data + size);
              var bridge = globalThis.AetherKiriLive2D;
              if(bridge && typeof bridge.loadModelFromBytes === "function") {
                  bridge.loadModelFromBytes(id, name, bytes);
              } else {
                  globalThis.__aetherKiriPendingLive2D =
                      globalThis.__aetherKiriPendingLive2D || [];
                  globalThis.__aetherKiriPendingLive2D.push({
                      id: id,
                      storage: name,
                      bytes: bytes
                  });
              }
          } catch(e) {
              console.error("AetherKiri Live2D load bridge failed:", e);
          }
      });

EM_JS(void, AetherKiriLive2DSetVisible, (int id, int visible), {
    try {
        var bridge = globalThis.AetherKiriLive2D;
        if(bridge && typeof bridge.setVisible === "function")
            bridge.setVisible(id, !!visible);
    } catch(e) {
        console.error("AetherKiri Live2D visibility bridge failed:", e);
    }
});

EM_JS(void, AetherKiriLive2DProgress, (int id), {
    try {
        var bridge = globalThis.AetherKiriLive2D;
        if(bridge && typeof bridge.progress === "function")
            bridge.progress(id);
    } catch(e) {
        console.error("AetherKiri Live2D progress bridge failed:", e);
    }
});

EM_JS(void, AetherKiriLive2DRelease, (int id), {
    try {
        var bridge = globalThis.AetherKiriLive2D;
        if(bridge && typeof bridge.release === "function")
            bridge.release(id);
    } catch(e) {
        console.error("AetherKiri Live2D release bridge failed:", e);
    }
});
#else
static void AetherKiriLive2DLoadModelBytes(int, const char *,
                                           const unsigned char *, int) {}
static void AetherKiriLive2DSetVisible(int, int) {}
static void AetherKiriLive2DProgress(int) {}
static void AetherKiriLive2DRelease(int) {}
#endif

static int NextLive2DModelId() {
    static int nextId = 0;
    return ++nextId;
}

static bool LoadStorageBytes(const ttstr &storage,
                             std::vector<unsigned char> &out) {
    out.clear();
    try {
        tTJSBinaryStream *stream = TVPCreateStream(storage, TJS_BS_READ);
        if(!stream)
            return false;
        const tjs_uint64 size = stream->GetSize();
        if(size > static_cast<tjs_uint64>(std::numeric_limits<int>::max())) {
            delete stream;
            return false;
        }
        out.resize(static_cast<size_t>(size));
        if(size > 0)
            stream->ReadBuffer(out.data(), static_cast<tjs_uint>(size));
        delete stream;
        return true;
    } catch(...) {
        return false;
    }
}

static void SetIntResult(tTJSVariant *result, tjs_int value = 0) {
    if(result)
        *result = value;
}

static void SetBoolResult(tTJSVariant *result, bool value = false) {
    if(result)
        *result = static_cast<tjs_int>(value ? 1 : 0);
}

static void SetStringResult(tTJSVariant *result, const tjs_char *value = TJS_W("")) {
    if(result)
        *result = value;
}

static void SetArrayResult(tTJSVariant *result) {
    if(!result)
        return;
    iTJSDispatch2 *array = TJSCreateArrayObject();
    *result = tTJSVariant(array, array);
    array->Release();
}

static tjs_error CreateLive2DObject(tTJSVariant *result, const tjs_char *expr) {
    if(!result)
        return TJS_S_OK;
    try {
        TVPExecuteExpression(ttstr(expr), result);
    } catch(...) {
        result->Clear();
    }
    return TJS_S_OK;
}

} // namespace

class Live2DMatrix {
public:
    Live2DMatrix() = default;
    static tjs_error setMatrixCb(tTJSVariant *result, tjs_int, tTJSVariant **,
                                 Live2DMatrix *) {
        SetIntResult(result, 1);
        return TJS_S_OK;
    }
};

class Live2DDevice {
public:
    Live2DDevice() = default;
    void beginScene() {}
    void endScene() {}
    void onBeginScene() {}
    void onEndScene() {}

    static tjs_error renderCb(tTJSVariant *result, tjs_int, tTJSVariant **,
                              Live2DDevice *) {
        SetIntResult(result, 1);
        return TJS_S_OK;
    }
};

class Live2DModel {
public:
    Live2DModel() : modelId_(NextLive2DModelId()) {}
    ~Live2DModel() { AetherKiriLive2DRelease(modelId_); }

    static tjs_error okCb(tTJSVariant *result, tjs_int, tTJSVariant **,
                          Live2DModel *) {
        SetIntResult(result, 1);
        return TJS_S_OK;
    }

    static tjs_error renderCb(tTJSVariant *result, tjs_int, tTJSVariant **,
                              Live2DModel *self) {
        if(self)
            AetherKiriLive2DProgress(self->modelId_);
        SetIntResult(result, 1);
        return TJS_S_OK;
    }

    static tjs_error showCb(tTJSVariant *result, tjs_int, tTJSVariant **,
                            Live2DModel *self) {
        if(self)
            AetherKiriLive2DSetVisible(self->modelId_, 1);
        SetIntResult(result, 1);
        return TJS_S_OK;
    }

    static tjs_error hideCb(tTJSVariant *result, tjs_int, tTJSVariant **,
                            Live2DModel *self) {
        if(self)
            AetherKiriLive2DSetVisible(self->modelId_, 0);
        SetIntResult(result, 1);
        return TJS_S_OK;
    }

    static tjs_error progressCb(tTJSVariant *result, tjs_int, tTJSVariant **,
                                Live2DModel *self) {
        if(self)
            AetherKiriLive2DProgress(self->modelId_);
        SetIntResult(result, 1);
        return TJS_S_OK;
    }

    static tjs_error zeroCb(tTJSVariant *result, tjs_int, tTJSVariant **,
                            Live2DModel *) {
        SetIntResult(result, 0);
        return TJS_S_OK;
    }

    static tjs_error falseCb(tTJSVariant *result, tjs_int, tTJSVariant **,
                             Live2DModel *) {
        SetBoolResult(result, false);
        return TJS_S_OK;
    }

    static tjs_error loadCb(tTJSVariant *result, tjs_int numparams,
                            tTJSVariant **param, Live2DModel *self) {
        ttstr storage = TJS_W("(unknown)");
        if(numparams > 0 && param && param[0])
            storage = param[0]->AsStringNoAddRef();
        std::vector<unsigned char> bytes;
        if(self && LoadStorageBytes(storage, bytes)) {
            const std::string storageName = storage.AsStdString();
            AetherKiriLive2DLoadModelBytes(
                self->modelId_, storageName.c_str(), bytes.data(),
                static_cast<int>(bytes.size()));
            TVPAddLog(ttstr(TJS_W("krkrlive2d_web: queued Live2D model for WebGL renderer: ")) +
                      storage + TJS_W(" (") +
                      ttstr(static_cast<tjs_int>(bytes.size())) +
                      TJS_W(" bytes)"));
        } else {
            TVPAddLog(ttstr(TJS_W("krkrlive2d_web: failed to read Live2D model: ")) +
                      storage);
        }
        SetBoolResult(result, true);
        return TJS_S_OK;
    }

    static tjs_error emptyStringCb(tTJSVariant *result, tjs_int, tTJSVariant **,
                                   Live2DModel *) {
        SetStringResult(result);
        return TJS_S_OK;
    }

    static tjs_error emptyArrayCb(tTJSVariant *result, tjs_int, tTJSVariant **,
                                  Live2DModel *) {
        SetArrayResult(result);
        return TJS_S_OK;
    }

    static tjs_error cloneCb(tTJSVariant *result, tjs_int, tTJSVariant **,
                             Live2DModel *) {
        return CreateLive2DObject(result, TJS_W("new Live2DModel()"));
    }

    static tjs_error getDeviceCb(tTJSVariant *result, tjs_int, tTJSVariant **,
                                 Live2DModel *) {
        return CreateLive2DObject(result, TJS_W("new Live2DDevice()"));
    }

    static tjs_error setDeviceCb(tTJSVariant *, tjs_int, tTJSVariant **,
                                 Live2DModel *) {
        return TJS_S_OK;
    }

private:
    int modelId_ = 0;
};

NCB_REGISTER_CLASS(Live2DMatrix) {
    Constructor();
    NCB_METHOD_RAW_CALLBACK(setMatrix, &Live2DMatrix::setMatrixCb, 0);
}

NCB_REGISTER_CLASS(Live2DDevice) {
    Constructor();
    NCB_METHOD(beginScene);
    NCB_METHOD(endScene);
    NCB_METHOD(onBeginScene);
    NCB_METHOD(onEndScene);
    NCB_METHOD_RAW_CALLBACK(render, &Live2DDevice::renderCb, 0);
}

NCB_REGISTER_CLASS(Live2DModel) {
    Constructor();
    NCB_PROPERTY_RAW_CALLBACK(device, Live2DModel::getDeviceCb,
                              Live2DModel::setDeviceCb, 0);
    NCB_METHOD_RAW_CALLBACK(render, &Live2DModel::renderCb, 0);
    NCB_METHOD_RAW_CALLBACK(show, &Live2DModel::showCb, 0);
    NCB_METHOD_RAW_CALLBACK(hide, &Live2DModel::hideCb, 0);
    NCB_METHOD_RAW_CALLBACK(progress, &Live2DModel::progressCb, 0);
    NCB_METHOD_RAW_CALLBACK(load, &Live2DModel::loadCb, 0);
    NCB_METHOD_RAW_CALLBACK(clone, &Live2DModel::cloneCb, 0);
    NCB_METHOD_RAW_CALLBACK(setScale, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(getScale, &Live2DModel::zeroCb, 0);
    NCB_METHOD_RAW_CALLBACK(setMatrix, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(setVoiceValue, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(setVoiceWeight, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(setVoiceMode, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(setBlinkingInterval, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(setBlinkingSettings, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(setBlinkingMode, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(setMosaicParam, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(getExpressionCount, &Live2DModel::zeroCb, 0);
    NCB_METHOD_RAW_CALLBACK(getExpressionName, &Live2DModel::emptyStringCb, 0);
    NCB_METHOD_RAW_CALLBACK(setExpression, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(getExpression, &Live2DModel::emptyStringCb, 0);
    NCB_METHOD_RAW_CALLBACK(fixExpression, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(getMotionGroupCount, &Live2DModel::zeroCb, 0);
    NCB_METHOD_RAW_CALLBACK(getMotionGroupName, &Live2DModel::emptyStringCb, 0);
    NCB_METHOD_RAW_CALLBACK(getMotionCount, &Live2DModel::zeroCb, 0);
    NCB_METHOD_RAW_CALLBACK(getMotionName, &Live2DModel::emptyStringCb, 0);
    NCB_METHOD_RAW_CALLBACK(startMotion, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(stopMotion, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(getCurrentMotions, &Live2DModel::emptyArrayCb, 0);
    NCB_METHOD_RAW_CALLBACK(isPlaying, &Live2DModel::falseCb, 0);
    NCB_METHOD_RAW_CALLBACK(getParameterCount, &Live2DModel::zeroCb, 0);
    NCB_METHOD_RAW_CALLBACK(getParameterInfo, &Live2DModel::emptyStringCb, 0);
    NCB_METHOD_RAW_CALLBACK(getParameterValue, &Live2DModel::zeroCb, 0);
    NCB_METHOD_RAW_CALLBACK(setParameterValue, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(setParameterType, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(setDiffParameterValue, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(getDiffParameterValue, &Live2DModel::zeroCb, 0);
    NCB_METHOD_RAW_CALLBACK(addEyeBlinkId, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(addLipSyncId, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(canSync, &Live2DModel::falseCb, 0);
    NCB_METHOD_RAW_CALLBACK(sync, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(getPartCount, &Live2DModel::zeroCb, 0);
    NCB_METHOD_RAW_CALLBACK(getPartInfo, &Live2DModel::emptyStringCb, 0);
    NCB_METHOD_RAW_CALLBACK(setPart, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(getPartValue, &Live2DModel::zeroCb, 0);
    NCB_METHOD_RAW_CALLBACK(setPartValue, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(setPartFadeTime, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(getEventCount, &Live2DModel::zeroCb, 0);
    NCB_METHOD_RAW_CALLBACK(getEventName, &Live2DModel::emptyStringCb, 0);
    NCB_METHOD_RAW_CALLBACK(addVriableMotion, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(delVariableMotion, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(getVariableMotionCount, &Live2DModel::zeroCb, 0);
    NCB_METHOD_RAW_CALLBACK(getVariableMotionName, &Live2DModel::emptyStringCb, 0);
    NCB_METHOD_RAW_CALLBACK(getVariableMotionInfo, &Live2DModel::emptyStringCb, 0);
    NCB_METHOD_RAW_CALLBACK(getVariable, &Live2DModel::zeroCb, 0);
    NCB_METHOD_RAW_CALLBACK(setVariable, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(isMosaicModel, &Live2DModel::falseCb, 0);
    NCB_METHOD_RAW_CALLBACK(reload, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(resetParts, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(resetVariables, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(resetExpressionVariables, &Live2DModel::okCb, 0);
}

extern "C" void TVPRegisterKrkrLive2DPluginAnchor() {
    ncbAutoRegister::RegisterInternalPluginEntry(
        TJS_W("krkrlive2d.dll"),
        ncbAutoRegister::ClassRegist,
        &ncbNativeClassAutoRegister_Live2DMatrix);
    ncbAutoRegister::RegisterInternalPluginEntry(
        TJS_W("krkrlive2d.dll"),
        ncbAutoRegister::ClassRegist,
        &ncbNativeClassAutoRegister_Live2DDevice);
    ncbAutoRegister::RegisterInternalPluginEntry(
        TJS_W("krkrlive2d.dll"),
        ncbAutoRegister::ClassRegist,
        &ncbNativeClassAutoRegister_Live2DModel);
}
