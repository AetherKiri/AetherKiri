#include "ncbind.hpp"
#include "ScriptMgnIntf.h"

struct Live2DRenderTarget {
    unsigned int fbo;
    int width;
    int height;
};

Live2DRenderTarget g_live2dRenderTarget = {0, 0, 0};

#define NCB_MODULE_NAME TJS_W("krkrlive2d.dll")

namespace {

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
    Live2DModel() = default;

    static tjs_error okCb(tTJSVariant *result, tjs_int, tTJSVariant **,
                          Live2DModel *) {
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
    NCB_METHOD_RAW_CALLBACK(render, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(show, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(hide, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(progress, &Live2DModel::okCb, 0);
    NCB_METHOD_RAW_CALLBACK(load, &Live2DModel::okCb, 0);
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
