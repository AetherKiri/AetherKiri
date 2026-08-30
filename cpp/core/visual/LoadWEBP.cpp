#include <webp/decode.h>
#include "tjsCommHead.h"
#include "GraphicsLoaderIntf.h"
#include "MsgIntf.h"
#include "tjsDictionary.h"
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <spdlog/spdlog.h>

namespace {
bool TVPWebPTraceEnabled() {
    static const bool enabled = [] {
        const char *profile = std::getenv("AETHERKIRI_IMAGE_LOAD_TRACE");
        const char *motion = std::getenv("AETHERKIRI_MOTION_RENDER_PROFILE");
        return (profile && *profile && *profile != '0') ||
               (motion && *motion && *motion != '0');
    }();
    return enabled;
}

bool TVPWebPDirectOutputEnabled() {
    static const bool enabled = [] {
        const char *value = std::getenv("AETHERKIRI_WEBP_DIRECT_OUTPUT");
        // Direct output is safe for the engine's bitmap callbacks and avoids
        // an additional full-image copy. Set the variable to 0 to A/B or
        // disable it when integrating a custom callback implementation.
        return !(value && *value == '0');
    }();
    return enabled;
}

bool TVPWebPDecodeThreadsEnabled() {
    static const bool enabled = [] {
        const char *value = std::getenv("AETHERKIRI_WEBP_DECODE_THREADS");
        // Large PSD layers benefit from libwebp's row-parallel decoder. Set
        // this to 0 to A/B or disable it on a constrained device.
        return !(value && *value == '0');
    }();
    return enabled;
}

} // namespace

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

    const auto load_start = std::chrono::steady_clock::now();
    int datasize = src->GetSize();
    std::unique_ptr<uint8_t[]> data(new uint8_t[datasize]);
    src->ReadBuffer(data.get(), datasize);
    const auto read_done = std::chrono::steady_clock::now();
    if(WebPGetFeatures(data.get(), datasize, &config.input) != VP8_STATUS_OK) {
        TVPThrowExceptionMessage(TJS_W("Invalid WebP image"));
    }
    const auto features_done = std::chrono::steady_clock::now();
    const bool decode_threads =
        TVPWebPDecodeThreadsEnabled() &&
        static_cast<uint64_t>(config.input.width) *
                static_cast<uint64_t>(config.input.height) >=
            1024ULL * 1024ULL;
    if(decode_threads)
        config.options.use_threads = 1;

    unsigned int stride =
        sizecallback(callbackdata, config.input.width, config.input.height,
                     config.input.has_alpha ? gpfRGBA : gpfRGB);
    void *direct_scanline = nullptr;
    const bool direct_output =
        glmNormal == mode && TVPWebPDirectOutputEnabled() &&
        stride >= config.input.width * 4U &&
        (direct_scanline = scanlinecallback(callbackdata, 0)) != nullptr;
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
        std::unique_ptr<uint8_t[]> image;
        if(!direct_output)
            image.reset(new uint8_t[image_size]);

        config.output.colorspace = MODE_RGBA;
        config.output.u.RGBA.rgba = direct_output
                                         ? static_cast<uint8_t *>(direct_scanline)
                                         : image.get();
        config.output.u.RGBA.stride = stride;
        config.output.u.RGBA.size = image_size;
        config.output.is_external_memory = 1;
        const auto decode_start = std::chrono::steady_clock::now();
        if(WebPDecode(data.get(), datasize, &config) != VP8_STATUS_OK) {
            TVPThrowExceptionMessage(TJS_W("Invalid WebP image(RGBA mode)"));
        }
        const auto decode_done = std::chrono::steady_clock::now();

        for(int y = 0; y < config.input.height; y++) {
            void *scanline = direct_output && y == 0
                                 ? direct_scanline
                                 : scanlinecallback(callbackdata, y);
            if(!scanline)
                break;
            if(!direct_output) {
                memcpy(scanline, image.get() + static_cast<size_t>(y) * stride,
                       stride);
            }
            scanlinecallback(callbackdata, -1);
        }
        if(TVPWebPTraceEnabled()) {
            const auto end = std::chrono::steady_clock::now();
            const auto ms = [](auto begin, auto end) {
                return std::chrono::duration<double, std::milli>(end - begin)
                    .count();
            };
            if(ms(load_start, end) >= 5.0) {
                spdlog::info(
                    "webp decode profile: size={}x{} bytes={} direct={} threads={} read_ms={:.3f} feature_ms={:.3f} decode_ms={:.3f} copy_ms={:.3f} total_ms={:.3f}",
                    config.input.width, config.input.height, datasize,
                    direct_output ? 1 : 0, decode_threads ? 1 : 0,
                    ms(load_start, read_done),
                    ms(read_done, features_done), ms(decode_start, decode_done),
                    ms(decode_done, end), ms(load_start, end));
            }
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
