#include "PluginStub.h"
#include "GraphicsLoaderIntf.h"
#include "ncbind.hpp"
#include "upstream_bridge/layerExSaveCodecs.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>
#include <spdlog/spdlog.h>

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

#define NCB_MODULE_NAME TJS_W("imagesaver.dll")

namespace {

bool saveTraceEnabled() {
    static const bool enabled = [] {
        const char *value = std::getenv("AETHERKIRI_SAVE_TRACE");
        return value && *value && *value != '0';
    }();
    return enabled;
}

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

bool validImageBuffer(tjs_int width, tjs_int height, tjs_int pitch,
                      const tjs_uint8 *buffer) {
    if(width <= 0 || height <= 0 || pitch == 0 || !buffer)
        return false;

    const auto width64 = static_cast<std::int64_t>(width);
    const auto height64 = static_cast<std::int64_t>(height);
    const auto pitch64 = static_cast<std::int64_t>(pitch);
    const auto rowBytes = width64 * 4;
    const auto absolutePitch = pitch64 < 0 ? -pitch64 : pitch64;
    if(rowBytes > absolutePitch)
        return false;

    const auto rowSpan = (height64 - 1) * absolutePitch;
    const auto minOffset = pitch64 < 0 ? -rowSpan : 0;
    const auto maxOffset = pitch64 < 0 ? rowBytes : rowSpan + rowBytes;
    return minOffset >= static_cast<std::int64_t>(
                           std::numeric_limits<std::ptrdiff_t>::min()) &&
           maxOffset <= static_cast<std::int64_t>(
                            std::numeric_limits<std::ptrdiff_t>::max());
}

const tjs_uint8 *imageRow(const tjs_uint8 *buffer, tjs_int pitch,
                          tjs_int y) {
    const auto offset = static_cast<std::int64_t>(y) *
                        static_cast<std::int64_t>(pitch);
    return buffer + static_cast<std::ptrdiff_t>(offset);
}

void saveAsBmp(const ttstr &name, tjs_int width, tjs_int height,
               const tjs_uint8 *buffer, tjs_int bufferPitch) {
    if(!validImageBuffer(width, height, bufferPitch, buffer))
        TVPThrowExceptionMessage(TJS_W("invalid layer image"));

    TVPClearGraphicCache();

    const auto rowBytes64 = static_cast<std::uint64_t>(width) * 4u;
    const auto pixelBytes64 = rowBytes64 * static_cast<std::uint64_t>(height);
    const auto offBits64 = std::uint64_t(14) + std::uint64_t(40);
    const auto fileSize64 = offBits64 + pixelBytes64;
    if(rowBytes64 > std::numeric_limits<tjs_uint>::max() ||
       pixelBytes64 > std::numeric_limits<tjs_uint32>::max() ||
       fileSize64 > std::numeric_limits<tjs_uint32>::max())
        TVPThrowExceptionMessage(TJS_W("layer image is too large for BMP"));

    const auto rowBytes = static_cast<tjs_uint>(rowBytes64);
    const auto pixelBytes = static_cast<tjs_uint32>(pixelBytes64);

    const tjs_uint32 fileHeaderSize = 14;
    const tjs_uint32 infoHeaderSize = 40;
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

    std::vector<tjs_uint8> copy(rowBytes);
    // BMP stores rows bottom-up.  `buffer` remains the first logical row,
    // even when the source pitch is negative.
    for(tjs_int y = height; y-- > 0;) {
        const tjs_uint8 *row = imageRow(buffer, bufferPitch, y);
        std::copy(row, row + rowBytes, copy.begin());
        writeBytes(output.get(), copy.data(), rowBytes, name);
    }
}

void saveAsEncoded(const ttstr &name, tjs_int width, tjs_int height,
                   const tjs_uint8 *buffer, tjs_int bufferPitch, bool tlg5) {
    if(width <= 0 || height <= 0 || !buffer)
        TVPThrowExceptionMessage(TJS_W("invalid layer image"));

    std::vector<std::uint8_t> encoded;
    const bool ok = tlg5
                        ? aether::krkrz::layer_save::encodeTlg5(
                              buffer, width, height, bufferPitch, encoded)
                        : aether::krkrz::layer_save::encodePng(
                              buffer, width, height, bufferPitch, encoded);
    if(!ok || encoded.empty())
        TVPThrowExceptionMessage(TJS_W("cannot encode layer image"));

    TVPClearGraphicCache();
    std::unique_ptr<tTJSBinaryStream> output(
        TVPCreateStream(name, TJS_BS_WRITE));
    if(!output)
        TVPThrowExceptionMessage((ttstr(TJS_W("cannot open : ")) + name)
                                     .c_str());
    if(encoded.size() > std::numeric_limits<tjs_uint>::max())
        TVPThrowExceptionMessage(TJS_W("encoded layer image is too large"));
    writeBytes(output.get(), encoded.data(),
               static_cast<tjs_uint>(encoded.size()), name);
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

        ttstr format = param[2]->AsStringNoAddRef();
        format.ToLowerCase();
        const ttstr filename = param[1]->AsStringNoAddRef();
        if(saveTraceEnabled()) {
            spdlog::info("SaveTrace saveLayerImage file={} format={} size={}x{} pitch={}",
                         filename.AsStdString(), format.AsStdString(), width,
                         height, pitch);
        }
        if(format == TJS_W("bmp")) {
            saveAsBmp(filename, width, height, buffer, pitch);
        } else if(format == TJS_W("png")) {
            saveAsEncoded(filename, width, height, buffer, pitch, false);
        } else if(format == TJS_W("tlg") || format == TJS_W("tlg5")) {
            saveAsEncoded(filename, width, height, buffer, pitch, true);
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
