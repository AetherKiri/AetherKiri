#include "ScreenCapture.h"

#include "DebugIntf.h"
#include "GraphicsLoaderIntf.h"
#include "LayerBitmapIntf.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <vector>

#if defined(__GNUC__)
extern "C" void TVPHostPrepareScreenCaptureFrame() __attribute__((weak));
extern "C" void TVPHostRequestScreenCaptureUpdate() __attribute__((weak));
extern "C" bool TVPHostGetLatestFrameDesc(
    uint32_t *width, uint32_t *height, uint32_t *stride_bytes,
    uint64_t *serial) __attribute__((weak));
extern "C" bool TVPHostCopyLatestFrameRGBA(
    void *out_pixels, size_t out_pixels_size, uint32_t *width,
    uint32_t *height, uint32_t *stride_bytes,
    uint64_t *serial) __attribute__((weak));
#else
extern "C" void TVPHostPrepareScreenCaptureFrame();
extern "C" void TVPHostRequestScreenCaptureUpdate();
extern "C" bool TVPHostGetLatestFrameDesc(uint32_t *, uint32_t *, uint32_t *,
                                            uint64_t *);
extern "C" bool TVPHostCopyLatestFrameRGBA(void *, size_t, uint32_t *,
                                             uint32_t *, uint32_t *,
                                             uint64_t *);
#endif

namespace {

    std::mutex TVPScreenCaptureMutex;
    bool TVPScreenCapturePending = false;
    tTVPScreenCaptureRequest TVPPendingScreenCapture;
    bool TVPLastScreenCaptureSuccess = false;
    ttstr TVPLastScreenCapturePath;
    int TVPLastScreenCaptureWidth = 0;
    int TVPLastScreenCaptureHeight = 0;

    bool TVPTryCopyHostFrame(std::vector<tjs_uint8> &rgba, uint32_t &width,
                             uint32_t &height, uint32_t &stride) {
#if defined(__GNUC__)
        if(!TVPHostGetLatestFrameDesc || !TVPHostCopyLatestFrameRGBA)
            return false;
#endif
        for(int attempt = 0; attempt < 2; ++attempt) {
            uint64_t serial = 0;
            if(!TVPHostGetLatestFrameDesc(&width, &height, &stride, &serial) ||
               width == 0 || height == 0 || stride < width * 4u)
                return false;
            rgba.resize(static_cast<size_t>(stride) * height);
            if(TVPHostCopyLatestFrameRGBA(rgba.data(), rgba.size(), &width,
                                          &height, &stride, &serial))
                return true;
        }
        rgba.clear();
        return false;
    }

    bool TVPSaveHostScreenCapture(const tTVPScreenCaptureRequest &request) {
        std::vector<tjs_uint8> rgba;
        uint32_t full_width = 0;
        uint32_t full_height = 0;
        uint32_t stride = 0;
        if(!TVPTryCopyHostFrame(rgba, full_width, full_height, stride))
            return false;

        int left = request.x;
        int top = request.y;
        int width = request.width;
        int height = request.height;
        if(width <= 0 || height <= 0) {
            left = 0;
            top = 0;
            width = static_cast<int>(full_width);
            height = static_cast<int>(full_height);
        }
        left = std::max(0, left);
        top = std::max(0, top);
        width = std::min(width, static_cast<int>(full_width) - left);
        height = std::min(height, static_cast<int>(full_height) - top);
        if(width <= 0 || height <= 0)
            return false;

        tTVPBaseBitmap captured(static_cast<tjs_uint>(width),
                                static_cast<tjs_uint>(height), 32);
        for(int y = 0; y < height; ++y) {
            const auto *source = rgba.data() +
                static_cast<size_t>(top + y) * stride +
                static_cast<size_t>(left) * 4u;
            auto *destination = static_cast<tjs_uint8 *>(
                captured.GetScanLineForWrite(static_cast<tjs_uint>(y)));
            if(!destination)
                return false;
            // The host contract is RGBA8888 while Aether bitmaps are BGRA.
            for(int x = 0; x < width; ++x) {
                destination[x * 4 + 0] = source[x * 4 + 2];
                destination[x * 4 + 1] = source[x * 4 + 1];
                destination[x * 4 + 2] = source[x * 4 + 0];
                destination[x * 4 + 3] = source[x * 4 + 3];
            }
        }
        TVPSaveImage(request.path, TJS_W("png"), &captured, nullptr);
        TVPSetScreenCaptureResult(request.path, width, height, true);
        return true;
    }

} // namespace

void TVPRequestScreenCapture(const ttstr &path, int x, int y, int width,
                             int height) {
    {
        std::lock_guard<std::mutex> lock(TVPScreenCaptureMutex);
        TVPPendingScreenCapture = { path, x, y, width, height };
        TVPScreenCapturePending = true;
    }
#if defined(__GNUC__)
    if(TVPHostRequestScreenCaptureUpdate)
#endif
        TVPHostRequestScreenCaptureUpdate();
}

bool TVPHasPendingScreenCapture() {
    std::lock_guard<std::mutex> lock(TVPScreenCaptureMutex);
    return TVPScreenCapturePending;
}

void TVPPrepareScreenCaptureFrame() {
#if defined(__GNUC__)
    if(TVPHostPrepareScreenCaptureFrame)
#endif
        TVPHostPrepareScreenCaptureFrame();
}

bool TVPTakeScreenCaptureRequest(tTVPScreenCaptureRequest &request) {
    std::lock_guard<std::mutex> lock(TVPScreenCaptureMutex);
    if(!TVPScreenCapturePending)
        return false;
    request = TVPPendingScreenCapture;
    TVPScreenCapturePending = false;
    return true;
}

bool TVPSaveScreenCapture(const tTVPScreenCaptureRequest &request,
                          const iTVPBaseBitmap *source) {
    try {
        if(TVPSaveHostScreenCapture(request))
            return true;
    } catch(...) {
        // Fall through to the layer-buffer path for non-host builds or a
        // transient host readback failure.
    }
    if(!source || source->GetBPP() != 32) {
        TVPSetScreenCaptureResult(request.path, 0, 0, false);
        return false;
    }
    const int full_width = static_cast<int>(source->GetWidth());
    const int full_height = static_cast<int>(source->GetHeight());
    int left = request.x;
    int top = request.y;
    int width = request.width;
    int height = request.height;
    if(width <= 0 || height <= 0) {
        left = 0;
        top = 0;
        width = full_width;
        height = full_height;
    }
    left = std::max(0, left);
    top = std::max(0, top);
    width = std::min(width, full_width - left);
    height = std::min(height, full_height - top);
    if(width <= 0 || height <= 0) {
        TVPSetScreenCaptureResult(request.path, 0, 0, false);
        return false;
    }

    try {
        tTVPBaseBitmap captured(static_cast<tjs_uint>(width),
                                static_cast<tjs_uint>(height), 32);
        const size_t row_bytes = static_cast<size_t>(width) * 4u;
        for(int y = 0; y < height; ++y) {
            const auto *source_row = static_cast<const tjs_uint8 *>(
                source->GetScanLine(static_cast<tjs_uint>(top + y)));
            auto *destination_row =
                captured.GetScanLineForWrite(static_cast<tjs_uint>(y));
            if(!source_row || !destination_row)
                throw std::runtime_error("screen capture readback failed");
            std::memcpy(destination_row,
                        source_row + static_cast<size_t>(left) * 4u, row_bytes);
        }
        TVPSaveImage(request.path, TJS_W("png"), &captured, nullptr);
        TVPSetScreenCaptureResult(request.path, width, height, true);
        return true;
    } catch(...) {
        TVPAddImportantLog(TJS_W("Screen capture failed: ") + request.path);
        TVPSetScreenCaptureResult(request.path, width, height, false);
        return false;
    }
}

void TVPSetScreenCaptureResult(const ttstr &path, int width, int height,
                               bool success) {
    std::lock_guard<std::mutex> lock(TVPScreenCaptureMutex);
    TVPLastScreenCapturePath = path;
    TVPLastScreenCaptureWidth = width;
    TVPLastScreenCaptureHeight = height;
    TVPLastScreenCaptureSuccess = success;
}

bool TVPGetLastScreenCapture(ttstr &path, int &width, int &height,
                             bool &success) {
    std::lock_guard<std::mutex> lock(TVPScreenCaptureMutex);
    if(TVPLastScreenCapturePath.IsEmpty())
        return false;
    path = TVPLastScreenCapturePath;
    width = TVPLastScreenCaptureWidth;
    height = TVPLastScreenCaptureHeight;
    success = TVPLastScreenCaptureSuccess;
    return true;
}

void TVPResetScreenCaptureForHostSession() {
    std::lock_guard<std::mutex> lock(TVPScreenCaptureMutex);
    TVPScreenCapturePending = false;
    TVPPendingScreenCapture = {};
    TVPLastScreenCaptureSuccess = false;
    TVPLastScreenCapturePath = TJS_W("");
    TVPLastScreenCaptureWidth = 0;
    TVPLastScreenCaptureHeight = 0;
}
