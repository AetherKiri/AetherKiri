/**
 * @file EngineBootstrap.h
 * @brief Engine bootstrapper — replaces original AppDelegate for host-mode
 *        startup (e.g. host FFI).
 *
 * Uses a legacy EGL pbuffer surface for bridge rendering when enabled.
 */
#pragma once

#include <cstdint>
#include <string>
#include "angle_backend.h"  // for krkr::AngleBackend

class TVPEngineBootstrap {
public:
    /**
     * Initialize the engine runtime for host mode.
     *
     * This replaces TVPAppDelegate::bootstrapForHostRuntime() and performs:
     *   1. SDL initialization
     *   2. Bridge graphics context creation when enabled
     *   3. Search path configuration
     *   4. UI extension initialization
     *   5. Locale configuration
     *
     * @param width   Initial surface width in pixels
     * @param height  Initial surface height in pixels
     * @return true on success
     */
    static bool Initialize(uint32_t width, uint32_t height,
                           krkr::AngleBackend backend = krkr::AngleBackend::OpenGLES);

    /**
     * Shut down the engine runtime and destroy the EGL context.
     */
    static void Shutdown();

    /**
     * Resize the rendering surface.
     *
     * @param width   New width in pixels
     * @param height  New height in pixels
     * @return true on success
     */
    static bool Resize(uint32_t width, uint32_t height);

    /**
     * Check if the engine bootstrap has been initialized.
     */
    static bool IsInitialized();

private:
    static void InitializeGraphics(uint32_t width, uint32_t height,
                                   krkr::AngleBackend backend = krkr::AngleBackend::OpenGLES);
    static void InitializeLocale();

    static bool s_initialized;
};
