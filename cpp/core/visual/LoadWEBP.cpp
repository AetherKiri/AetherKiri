#include <webp/decode.h>
#include "tjsCommHead.h"
#include "GraphicsLoaderIntf.h"
#include "MsgIntf.h"
#include "tjsDictionary.h"
#include <cstring>
#include <memory>

void TVPLoadWEBP(void *formatdata, void *callbackdata,
                 tTVPGraphicSizeCallback sizecallback,
                 tTVPGraphicScanLineCallback scanlinecallback,
                 tTVPMetaInfoPushCallback metainfopushcallback,
                 tTJSBinaryStream *src, tjs_int keyidx,
                 tTVPGraphicLoadMode mode) {
    WebPDecoderConfig config;
    if(WebPInitDecoderConfig(&config) == 0) {
        TVPThrowExceptionMessage(TJS_W("Invalid WebP image"));
    }

    int datasize = src->GetSize();
    std::unique_ptr<uint8_t[]> data(new uint8_t[datasize]);
    src->ReadBuffer(data.get(), datasize);
    if(WebPGetFeatures(data.get(), datasize, &config.input) != VP8_STATUS_OK) {
        TVPThrowExceptionMessage(TJS_W("Invalid WebP image"));
    }

    unsigned int stride =
        sizecallback(callbackdata, config.input.width, config.input.height,
                     config.input.has_alpha ? gpfRGBA : gpfRGB);
#if 0
	WebPData webp_data = { data, datasize };
	WebPDemuxer* demux = WebPDemux(&webp_data);
	WebPChunkIterator chunk_iter;
	if (WebPDemuxGetChunk(demux, "USER", 1, &chunk_iter)) {
		chunk_iter.chunk.bytes;
		WebPDemuxReleaseChunkIterator(&chunk_iter);
	}

	WebPDemuxDelete(demux);
#endif
    if(glmNormal == mode) {
        const size_t image_size =
            static_cast<size_t>(config.input.height) * stride;
        std::unique_ptr<uint8_t[]> image(new uint8_t[image_size]);

        config.output.colorspace = MODE_RGBA;
        config.output.u.RGBA.rgba = image.get();
        config.output.u.RGBA.stride = stride;
        config.output.u.RGBA.size = image_size;
        config.output.is_external_memory = 1;
        if(WebPDecode(data.get(), datasize, &config) != VP8_STATUS_OK) {
            TVPThrowExceptionMessage(TJS_W("Invalid WebP image(RGBA mode)"));
        }

        for(int y = 0; y < config.input.height; y++) {
            void *scanline = scanlinecallback(callbackdata, y);
            if(!scanline)
                break;
            memcpy(scanline, image.get() + static_cast<size_t>(y) * stride,
                   stride);
            scanlinecallback(callbackdata, -1);
        }
    } else if(glmGrayscale == mode) {
        const size_t image_size =
            static_cast<size_t>(config.input.height) * stride;
        std::unique_ptr<uint8_t[]> image(new uint8_t[image_size]);

        config.output.colorspace = MODE_YUV;
        unsigned int uvSize = config.input.width * config.input.height / 4 +
            config.input.width + config.input.width;
        std::unique_ptr<uint8_t[]> dummy(new uint8_t[uvSize]);
        config.output.u.YUVA.y = image.get();
        config.output.u.YUVA.u = dummy.get();
        config.output.u.YUVA.v = dummy.get();
        config.output.u.YUVA.a = nullptr;
        config.output.u.YUVA.y_stride = stride;
        config.output.u.YUVA.u_stride = config.input.width / 2;
        config.output.u.YUVA.v_stride = config.input.width / 2;
        config.output.u.YUVA.a_stride = 0;
        config.output.u.YUVA.y_size = image_size;
        config.output.u.YUVA.u_size = uvSize;
        config.output.u.YUVA.v_size = uvSize;
        config.output.u.YUVA.a_size = 0;
        config.output.is_external_memory = 1;

        if(WebPDecode(data.get(), datasize, &config) != VP8_STATUS_OK) {
            TVPThrowExceptionMessage(
                TJS_W("Invalid WebP image(Grayscale Mode)"));
        }

        for(int y = 0; y < config.input.height; y++) {
            void *scanline = scanlinecallback(callbackdata, y);
            if(!scanline)
                break;
            memcpy(scanline, image.get() + static_cast<size_t>(y) * stride,
                   stride);
            scanlinecallback(callbackdata, -1);
        }
    } else {
        TVPThrowExceptionMessage(
            TJS_W("WebP does not support palettized image"));
    }
}

void TVPLoadHeaderWEBP(void *formatdata, tTJSBinaryStream *src,
                       iTJSDispatch2 **dic) {
    WebPDecoderConfig config;
    if(WebPInitDecoderConfig(&config) == 0) {
        TVPThrowExceptionMessage(TJS_W("Invalid WebP image"));
    }

    int datasize = src->GetSize();
    std::unique_ptr<uint8_t[]> data(new uint8_t[datasize]);
    src->ReadBuffer(data.get(), datasize);
    if(WebPGetFeatures(data.get(), datasize, &config.input) != VP8_STATUS_OK) {
        TVPThrowExceptionMessage(TJS_W("Invalid WebP image"));
    }

    *dic = TJSCreateDictionaryObject();
    tTJSVariant val((tjs_int32)config.input.width);
    (*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("width"), nullptr, &val, (*dic));
    val = tTJSVariant((tjs_int32)config.input.height);
    (*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("height"), nullptr, &val, (*dic));
    val = tTJSVariant(config.input.has_alpha ? 32 : 24);
    (*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("bpp"), nullptr, &val, (*dic));
}
