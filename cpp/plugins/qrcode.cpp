#include "PluginStub.h"
#include "ncbind.hpp"
#include "qrcode/QR_Encode.h"

#include <string>

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

#define NCB_MODULE_NAME TJS_W("qrcode.dll")

namespace {

std::string toUtf8(const ttstr &text) {
    const tjs_int length = TVPWideCharToUtf8String(text.c_str(), nullptr);
    std::string out(static_cast<size_t>(length), '\0');
    if(length > 0)
        TVPWideCharToUtf8String(text.c_str(), out.data());
    return out;
}

void callSetImageSize(iTJSDispatch2 *layer, tjs_int width, tjs_int height) {
    tTJSVariant widthValue(width);
    tTJSVariant heightValue(height);
    tTJSVariant *params[] = { &widthValue, &heightValue };
    layer->FuncCall(0, TJS_W("setImageSize"), nullptr, nullptr, 2, params,
                    layer);
}

tjs_intptr_t getLayerPointer(iTJSDispatch2 *layer, const tjs_char *name) {
    tTJSVariant value;
    if(TJS_FAILED(layer->PropGet(0, name, nullptr, &value, layer)))
        TVPThrowExceptionMessage((ttstr(TJS_W("cannot get Layer.")) + name)
                                     .c_str());
    return static_cast<tjs_intptr_t>(value.AsInteger());
}

tjs_int getLayerInteger(iTJSDispatch2 *layer, const tjs_char *name) {
    return static_cast<tjs_int>(getLayerPointer(layer, name));
}

class LayerQRCode {
public:
    static tjs_error TJS_INTF_METHOD drawQRCode(tTJSVariant *result,
                                                tjs_int numparams,
                                                tTJSVariant **param,
                                                iTJSDispatch2 *objthis) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        const std::string text = toUtf8(param[0]->AsStringNoAddRef());
        const tjs_int ecLevel =
            numparams > 1 && param[1]->Type() != tvtVoid
                ? static_cast<tjs_int>(param[1]->AsInteger())
                : QR_LEVEL_L;
        const tjs_int qrVersion =
            numparams > 2 && param[2]->Type() != tvtVoid
                ? static_cast<tjs_int>(param[2]->AsInteger())
                : QR_VRESION_S;
        const tjs_int autoExtent =
            numparams > 3 && param[3]->Type() != tvtVoid
                ? static_cast<tjs_int>(param[3]->AsInteger())
                : TRUE;
        const tjs_int maskPattern =
            numparams > 4 && param[4]->Type() != tvtVoid
                ? static_cast<tjs_int>(param[4]->AsInteger())
                : -1;

        CQR_Encode encoder;
        if(!encoder.EncodeData(ecLevel, qrVersion, autoExtent, maskPattern,
                               text.data(), static_cast<int>(text.size()))) {
            if(result)
                *result = TJS_W("QR encode failed: data is empty or too large");
            return TJS_S_OK;
        }

        const tjs_int symbolSize = encoder.m_nSymbleSize;
        const tjs_int imageSize = symbolSize + QR_MARGIN * 2;
        callSetImageSize(objthis, imageSize, imageSize);

        const tjs_int pitch =
            getLayerInteger(objthis, TJS_W("mainImageBufferPitch"));
        auto *buffer = reinterpret_cast<tjs_uint8 *>(
            getLayerPointer(objthis, TJS_W("mainImageBufferForWrite")));
        if(!buffer)
            TVPThrowExceptionMessage(TJS_W("Layer has no writable image buffer"));

        for(tjs_int y = 0; y < imageSize; ++y) {
            auto *row = reinterpret_cast<tjs_uint32 *>(buffer + y * pitch);
            for(tjs_int x = 0; x < imageSize; ++x) {
                const bool inSymbol = x >= QR_MARGIN && y >= QR_MARGIN &&
                                      x < QR_MARGIN + symbolSize &&
                                      y < QR_MARGIN + symbolSize;
                const bool dark =
                    inSymbol &&
                    encoder.m_byModuleData[x - QR_MARGIN][y - QR_MARGIN] != 0;
                row[x] = dark ? 0xff000000u : 0xffffffffu;
            }
        }

        return TJS_S_OK;
    }
};

} // namespace

NCB_ATTACH_CLASS(LayerQRCode, Layer) {
    RawCallback(TJS_W("drawQRCode"), &Class::drawQRCode, 0);
}
