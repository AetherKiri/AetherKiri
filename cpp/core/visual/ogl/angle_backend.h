/**
 * @file angle_backend.h
 * @brief Legacy EGL backend type enumeration.
 *
 * Separated from krkr_egl_context.h so that higher-level modules
 * (EngineBootstrap, engine_api) can reference the backend type
 * without pulling in the full EGL context dependency tree.
 */
#pragma once

namespace krkr {

/**
 * Legacy EGL backend type for Android platform.
 * On macOS/iOS, Metal is always used regardless of this setting.
 */
enum class AngleBackend {
    OpenGLES,   ///< EGL_PLATFORM_ANGLE_TYPE_OPENGLES_ANGLE (default)
    Vulkan,     ///< EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE
};

} // namespace krkr
