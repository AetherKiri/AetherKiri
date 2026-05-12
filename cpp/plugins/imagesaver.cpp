#include "PluginStub.h"
#include "GraphicsLoaderIntf.h"
#include "ncbind.hpp"

#include <algorithm>
#include <memory>
#include <vector>

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

#define NCB_MODULE_NAME TJS_W("imagesaver.dll")

namespace {

void addMember(iTJSDispatch2 *dispatch, const tjs_char *name,
               iTJSDispatch2 *member) {
    tTJSVariant value(member);
    member->Release();
    dispatch->PropSet(TJS_MEMBERENSURE, name, nullptr, &value, dispatch);
}

void delMember(iTJSDispatch2 *dispatch, const tjs_char *name) {
    dispatch->DeleteMember(0, name, nullptr, dispatch);
}

tjs_int64 getLayerInteger(iTJSDispatch2 *layer, const tjs_char *name,
                          const tjs_char *message) {
    tTJSVariant value;
    if(TJS_FAILED(layer->PropGet(0, name, nullptr, &value, layer)))
        TVPThrowExceptionMessage(message);
    return value.AsInteger();
}

void writeBytes(tTJSBinaryStream *stream, const void *data, tjs_uint size,
                const ttstr &name) {
    try {
        stream->WriteBuffer(data, size);
    } catch(...) {
        TVPThrowExceptionMessage((ttstr(TJS_W("write failed : ")) + name)
                                     .c_str());
    }
}

void writeU16(tTJSBinaryStream *stream, tjs_uint16 value, const ttstr &name) {
    const tjs_uint8 bytes[] = {
        static_cast<tjs_uint8>(value & 0xff),
        static_cast<tjs_uint8>((value >> 8) & 0xff),
    };
    writeBytes(stream, bytes, sizeof(bytes), name);
}

void writeU32(tTJSBinaryStream *stream, tjs_uint32 value, const ttstr &name) {
    const tjs_uint8 bytes[] = {
        static_cast<tjs_uint8>(value & 0xff),
        static_cast<tjs_uint8>((value >> 8) & 0xff),
        static_cast<tjs_uint8>((value >> 16) & 0xff),
        static_cast<tjs_uint8>((value >> 24) & 0xff),
    };
    writeBytes(stream, bytes, sizeof(bytes), name);
}

void writeI32(tTJSBinaryStream *stream, tjs_int32 value, const ttstr &name) {
    writeU32(stream, static_cast<tjs_uint32>(value), name);
}

void saveAsBmp(const ttstr &name, tjs_int width, tjs_int height,
               const tjs_uint8 *buffer, tjs_int bufferPitch) {
    if(width <= 0 || height <= 0 || !buffer)
        TVPThrowExceptionMessage(TJS_W("invalid layer image"));

    TVPClearGraphicCache();

    const tjs_uint rowBytes = static_cast<tjs_uint>(width) * 4;
    const tjs_uint srcPitch =
        static_cast<tjs_uint>(bufferPitch < 0 ? -bufferPitch : bufferPitch);
    if(srcPitch < rowBytes)
        TVPThrowExceptionMessage(TJS_W("invalid layer image pitch"));

    const tjs_uint32 fileHeaderSize = 14;
    const tjs_uint32 infoHeaderSize = 40;
    const tjs_uint32 pixelBytes = rowBytes * static_cast<tjs_uint32>(height);
    const tjs_uint32 offBits = fileHeaderSize + infoHeaderSize;
    const tjs_uint32 fileSize = offBits + pixelBytes;

    std::unique_ptr<tTJSBinaryStream> output(
        TVPCreateStream(name, TJS_BS_WRITE));
    if(!output)
        TVPThrowExceptionMessage((ttstr(TJS_W("cannot open : ")) + name)
                                     .c_str());

    writeU16(output.get(), 0x4d42, name);
    writeU32(output.get(), fileSize, name);
    writeU16(output.get(), 0, name);
    writeU16(output.get(), 0, name);
    writeU32(output.get(), offBits, name);

    writeU32(output.get(), infoHeaderSize, name);
    writeI32(output.get(), width, name);
    writeI32(output.get(), height, name);
    writeU16(output.get(), 1, name);
    writeU16(output.get(), 32, name);
    writeU32(output.get(), 0, name);
    writeU32(output.get(), pixelBytes, name);
    writeI32(output.get(), 0, name);
    writeI32(output.get(), 0, name);
    writeU32(output.get(), 0, name);
    writeU32(output.get(), 0, name);

    const tjs_uint8 *row = buffer + bufferPitch * (height - 1);
    std::vector<tjs_uint8> copy(rowBytes);
    for(tjs_int y = 0; y < height; ++y) {
        std::copy(row, row + rowBytes, copy.begin());
        writeBytes(output.get(), copy.data(), rowBytes, name);
        row -= bufferPitch;
    }
}

class SaveLayerImageFunction : public tTJSDispatch {
public:
    tjs_error TJS_INTF_METHOD FuncCall(tjs_uint32, const tjs_char *membername,
                                       tjs_uint32 *, tTJSVariant *result,
                                       tjs_int numparams,
                                       tTJSVariant **param,
                                       iTJSDispatch2 *) override {
        if(membername)
            return TJS_E_MEMBERNOTFOUND;
        if(numparams < 3)
            return TJS_E_BADPARAMCOUNT;

        iTJSDispatch2 *layer = param[0]->AsObjectNoAddRef();
        const tjs_int width = static_cast<tjs_int>(getLayerInteger(
            layer, TJS_W("imageWidth"),
            TJS_W("invoking of Layer.imageWidth failed.")));
        const tjs_int height = static_cast<tjs_int>(getLayerInteger(
            layer, TJS_W("imageHeight"),
            TJS_W("invoking of Layer.imageHeight failed.")));
        const auto *buffer = reinterpret_cast<const tjs_uint8 *>(
            static_cast<tjs_intptr_t>(getLayerInteger(
                layer, TJS_W("mainImageBuffer"),
                TJS_W("invoking of Layer.mainImageBuffer failed."))));
        const tjs_int pitch = static_cast<tjs_int>(getLayerInteger(
            layer, TJS_W("mainImageBufferPitch"),
            TJS_W("invoking of Layer.mainImageBufferPitch failed.")));

        const ttstr format = param[2]->AsStringNoAddRef();
        if(format == TJS_W("bmp")) {
            saveAsBmp(param[1]->AsStringNoAddRef(), width, height, buffer,
                      pitch);
        } else {
            TVPThrowExceptionMessage(TJS_W("Not supported format."));
        }

        if(result)
            result->Clear();
        return TJS_S_OK;
    }
};

void InitImageSaverPlugin() {
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(global) {
        addMember(global, TJS_W("saveLayerImage"), new SaveLayerImageFunction());
        global->Release();
    }
}

void UninitImageSaverPlugin() {
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(global) {
        delMember(global, TJS_W("saveLayerImage"));
        global->Release();
    }
}

} // namespace

NCB_PRE_REGIST_CALLBACK(InitImageSaverPlugin);
NCB_POST_UNREGIST_CALLBACK(UninitImageSaverPlugin);
