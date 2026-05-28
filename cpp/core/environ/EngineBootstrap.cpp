/**
 * @file EngineBootstrap.cpp
 * @brief Engine bootstrapper implementation.
 *
 * Replaces the original AppDelegate for host-mode startup.
 * Creates a legacy EGL pbuffer context for bridge rendering when enabled.
 */

#include "EngineBootstrap.h"

#include <spdlog/spdlog.h>
#include <thread>

#include "Application.h"
#include "ConfigManager/LocaleConfigManager.h"
#if defined(KRKR_ENABLE_GPU_BRIDGE)
#include "krkr_egl_context.h"
#include "krkr_gl.h"
#include "ogl_common.h"
#endif

// Forward declaration — defined in stubs/ui_stubs.cpp
void TVPInitUIExtension();

extern void TVPForceRegisterGodotRenderManager();
#if defined(KRKR_ENABLE_GPU_BRIDGE)
extern void TVPForceRegisterOpenGLRenderManager();
#endif

extern "C" void SDL_SetMainReady();
extern std::thread::id TVPMainThreadID;
std::string TVPGetCurrentLanguage();

// ---------------------------------------------------------------------------
// Static member
// ---------------------------------------------------------------------------

bool TVPEngineBootstrap::s_initialized = false;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool TVPEngineBootstrap::Initialize(uint32_t width, uint32_t height,
                                    krkr::AngleBackend backend) {
    if (s_initialized) {
        spdlog::warn("TVPEngineBootstrap::Initialize called but already initialized");
        return true;
    }

    // 1. SDL setup (required for audio / misc subsystems)
    SDL_SetMainReady();
    TVPMainThreadID = std::this_thread::get_id();
    spdlog::debug("EngineBootstrap: starting initialization");
    spdlog::default_logger()->flush();

    // 2. Initialize host graphics state.
    InitializeGraphics(width, height, backend);
    spdlog::default_logger()->flush();

    TVPForceRegisterGodotRenderManager();
#if defined(KRKR_ENABLE_GPU_BRIDGE)
    TVPForceRegisterOpenGLRenderManager();
#endif

    // 3. Initialize UI extensions
    TVPInitUIExtension();

    // 4. Initialize locale
    InitializeLocale();

    s_initialized = true;
    spdlog::info("EngineBootstrap: initialization complete ({}x{})", width, height);
    spdlog::default_logger()->flush();
    return true;
}

void TVPEngineBootstrap::Shutdown() {
    if (!s_initialized) {
        return;
    }

    spdlog::info("EngineBootstrap: shutting down");
#if defined(KRKR_ENABLE_GPU_BRIDGE)
    krkr::GetEngineEGLContext().Destroy();
#endif
    s_initialized = false;
}

bool TVPEngineBootstrap::Resize(uint32_t width, uint32_t height) {
    if (!s_initialized) {
        spdlog::error("EngineBootstrap::Resize called before Initialize");
        return false;
    }

#if defined(KRKR_ENABLE_GPU_BRIDGE)
    auto& egl = krkr::GetEngineEGLContext();
    if (!egl.Resize(width, height)) {
        spdlog::error("EngineBootstrap::Resize failed for {}x{}", width, height);
        return false;
    }

    // Update the viewport to match the new surface size
    glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
#endif
    spdlog::info("EngineBootstrap: resized to {}x{}", width, height);
    return true;
}

bool TVPEngineBootstrap::IsInitialized() {
    return s_initialized;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void TVPEngineBootstrap::InitializeGraphics(uint32_t width, uint32_t height,
                                             krkr::AngleBackend backend) {
#if defined(KRKR_ENABLE_GPU_BRIDGE)
    auto& egl = krkr::GetEngineEGLContext();
    if (!egl.Initialize(width, height, backend)) {
        spdlog::error("EngineBootstrap: EGL context initialization failed, "
                       "rendering may not work correctly");
        return;
    }

    // Set initial viewport
    glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));

    // Clear with black
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    spdlog::info("EngineBootstrap: legacy EGL context ready");
#else
    (void)backend;
    spdlog::info("EngineBootstrap: Godot host graphics selected ({}x{})",
                 width, height);
#endif
}

void TVPEngineBootstrap::InitializeLocale() {
    LocaleConfigManager::GetInstance()->Initialize(TVPGetCurrentLanguage());
}
