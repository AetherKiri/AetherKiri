#include "tjsCommHead.h"

#include "TVPScreen.h"
#include "Application.h"
#if defined(KRKR_ENABLE_GPU_BRIDGE)
#include "krkr_egl_context.h"
#endif

int tTVPScreen::GetWidth() { return 2048; }
int tTVPScreen::GetHeight() {
#if defined(KRKR_ENABLE_GPU_BRIDGE)
    auto& egl = krkr::GetEngineEGLContext();
    if (egl.IsValid()) {
        uint32_t w = egl.GetWidth();
        uint32_t h = egl.GetHeight();
        if (w > 0 && h > 0) {
            int baseW = GetWidth();
            return baseW * static_cast<int>(h) / static_cast<int>(w);
        }
    }
#endif
    return GetWidth();
}

int tTVPScreen::GetDesktopLeft() { return 0; }
int tTVPScreen::GetDesktopTop() { return 0; }
int tTVPScreen::GetDesktopWidth() { return GetWidth(); }
int tTVPScreen::GetDesktopHeight() { return GetHeight(); }
