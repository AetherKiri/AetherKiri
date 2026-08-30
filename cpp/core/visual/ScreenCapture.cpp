#include "ScreenCapture.h"

#include "DebugIntf.h"
#include "GraphicsLoaderIntf.h"
#include "LayerBitmapIntf.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <stdexcept>

namespace {

    std::mutex TVPScreenCaptureMutex;
    bool TVPScreenCapturePending = false;
    tTVPScreenCaptureRequest TVPPendingScreenCapture;
    bool TVPLastScreenCaptureSuccess = false;
    ttstr TVPLastScreenCapturePath;
    int TVPLastScreenCaptureWidth = 0;
    int TVPLastScreenCaptureHeight = 0;

} // namespace

void TVPRequestScreenCapture(const ttstr &path, int x, int y, int width,
                             int height) {
    std::lock_guard<std::mutex> lock(TVPScreenCaptureMutex);
    TVPPendingScreenCapture = { path, x, y, width, height };
    TVPScreenCapturePending = true;
}

bool TVPHasPendingScreenCapture() {
    std::lock_guard<std::mutex> lock(TVPScreenCaptureMutex);
    return TVPScreenCapturePending;
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
