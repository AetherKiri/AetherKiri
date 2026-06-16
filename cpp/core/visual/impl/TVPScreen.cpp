#include "tjsCommHead.h"

#include "TVPScreen.h"
#include "Application.h"
#include <cstdint>
#if defined(KRKR_ENABLE_GPU_BRIDGE)
#include "krkr_egl_context.h"
#endif

namespace {
bool GetHostSurfaceSize(uint32_t &width, uint32_t &height) {
#if defined(KRKR_ENABLE_GPU_BRIDGE)
    auto& egl = krkr::GetEngineEGLContext();
    if (egl.IsValid()) {
        width = egl.GetWidth();
        height = egl.GetHeight();
        if (width > 0 && height > 0) {
            return true;
        }
    }
#endif
    return false;
}
} // namespace

int tTVPScreen::GetWidth() {
    uint32_t width = 0;
    uint32_t height = 0;
    if (GetHostSurfaceSize(width, height)) {
        return static_cast<int>(width);
    }
    return 1920;
}

int tTVPScreen::GetHeight() {
    uint32_t width = 0;
    uint32_t height = 0;
    if (GetHostSurfaceSize(width, height)) {
        return static_cast<int>(height);
    }
    return 1080;
}

int tTVPScreen::GetDesktopLeft() { return 0; }
int tTVPScreen::GetDesktopTop() { return 0; }
int tTVPScreen::GetDesktopWidth() { return GetWidth(); }
int tTVPScreen::GetDesktopHeight() { return GetHeight(); }
