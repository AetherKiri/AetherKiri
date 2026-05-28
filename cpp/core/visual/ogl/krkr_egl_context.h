/**
 * @file krkr_egl_context.h
 * @brief Headless EGL context manager for the legacy bridge backend.
 *
 * Replaces GLFW window + GLViewImpl with an offscreen
 * EGL Pbuffer surface, providing a pure headless OpenGL ES 2.0
 * context for compatibility backends:
 *   - macOS  → Metal backend
 *   - Windows → D3D11 backend
 *   - Linux  → Desktop GL / Vulkan backend
 *   - Android → native GLES / Vulkan backend
 */
#pragma once

#include <cstdint>

// Legacy bridge EGL path.
#define KRKR_HAS_EGL 1
#include <EGL/egl.h>

#include "angle_backend.h"  // krkr::AngleBackend

namespace krkr {

class EGLContextManager {
public:
    EGLContextManager() = default;
    ~EGLContextManager();

    // Non-copyable
    EGLContextManager(const EGLContextManager&) = delete;
    EGLContextManager& operator=(const EGLContextManager&) = delete;

    /**
     * Initialize the EGL display, create a Pbuffer surface and
     * an OpenGL ES 2.0 context.  Makes the context current.
     *
     * @param width   Initial surface width in pixels
     * @param height  Initial surface height in pixels
     * @return true on success
     */
    bool Initialize(uint32_t width, uint32_t height,
                    AngleBackend backend = AngleBackend::OpenGLES);

    /**
     * Destroy the EGL context, surface, and display.
     */
    void Destroy();

    /**
     * Make this context current on the calling thread.
     */
    bool MakeCurrent();

    /**
     * Release the context from the calling thread.
     */
    bool ReleaseCurrent();

    /**
     * Resize the Pbuffer surface. This destroys the old surface
     * and creates a new one with the requested dimensions.
     * The context is re-made current after resize.
     *
     * @param width   New width in pixels
     * @param height  New height in pixels
     * @return true on success
     */
    bool Resize(uint32_t width, uint32_t height);

    /**
     * Attach an IOSurface as the render target FBO.
     * When attached, all rendering goes to the IOSurface instead
     * of the Pbuffer. The FBO is bound as GL_FRAMEBUFFER.
     *
     * @param iosurface_id  IOSurfaceID from IOSurfaceGetID()
     * @param width         IOSurface width in pixels
     * @param height        IOSurface height in pixels
     * @return true on success
     */
    bool AttachIOSurface(uint32_t iosurface_id, uint32_t width, uint32_t height);

    /**
     * Detach the IOSurface FBO, reverting to the default Pbuffer.
     */
    void DetachIOSurface();

    /**
     * Bind the IOSurface FBO (if attached) or the default FBO.
     * Call this before rendering a frame.
     */
    void BindRenderTarget();

    /**
     * @return true if an IOSurface is currently attached as render target.
     */
    bool HasIOSurface() const { return iosurface_fbo_ != 0; }

    /**
     * @return the IOSurface FBO width (0 if not attached).
     */
    uint32_t GetIOSurfaceWidth() const { return iosurface_width_; }

    /**
     * @return the IOSurface FBO height (0 if not attached).
     */
    uint32_t GetIOSurfaceHeight() const { return iosurface_height_; }

    /**
     * Initialize EGL directly with an ANativeWindow (Android only).
     * Unlike Initialize() which creates a Pbuffer, this creates
     * a WindowSurface directly, avoiding Pbuffer-related failures
     * on some Android devices/drivers.
     *
     * After this call, IsValid()==true and HasNativeWindow()==true.
     *
     * @param window  ANativeWindow* from ANativeWindow_fromSurface()
     * @param width   Surface width in pixels
     * @param height  Surface height in pixels
     * @return true on success
     */
    bool InitializeWithWindow(void* window, uint32_t width, uint32_t height,
                              AngleBackend backend = AngleBackend::OpenGLES);

    /**
     * Attach an Android ANativeWindow as the render target.
     * Creates an EGL WindowSurface and binds it as the primary surface.
     * eglSwapBuffers() delivers frames directly to the SurfaceTexture.
     *
     * @param window  ANativeWindow* from ANativeWindow_fromSurface()
     * @param width   Surface width in pixels
     * @param height  Surface height in pixels
     * @return true on success
     */
    bool AttachNativeWindow(void* window, uint32_t width, uint32_t height);

    /**
     * Detach the ANativeWindow, reverting to Pbuffer mode.
     */
    void DetachNativeWindow();

    /**
     * @return true if rendering to an ANativeWindow (Android SurfaceTexture).
     */
    bool HasNativeWindow() const { return native_window_ != nullptr; }

    /**
     * @return the EGL WindowSurface for eglSwapBuffers (Android).
     */
    EGLSurface GetWindowSurface() const { return window_surface_; }

    /**
     * @return the ANativeWindow width (0 if not attached).
     */
    uint32_t GetNativeWindowWidth() const { return window_width_; }

    /**
     * @return the ANativeWindow height (0 if not attached).
     */
    uint32_t GetNativeWindowHeight() const { return window_height_; }

    /**
     * Update stored native window dimensions after SurfaceTexture resize.
     * setDefaultBufferSize() changes the buffer dimensions; the EGL surface
     * auto-adapts on next eglSwapBuffers. We track the new size here so
     * UpdateDrawBuffer() calculates the correct letterbox viewport.
     */
    void UpdateNativeWindowSize(uint32_t w, uint32_t h) {
        window_width_ = w;
        window_height_ = h;
    }

    /**
     * Mark the current frame as dirty (new content rendered).
     * Must be called after UpdateDrawBuffer() completes rendering.
     */
    void MarkFrameDirty() { frame_dirty_ = true; }

    /**
     * Check and consume the dirty flag.
     * @return true if the frame was dirty (and flag is now cleared).
     */
    bool ConsumeFrameDirty() {
        if (!frame_dirty_) return false;
        frame_dirty_ = false;
        return true;
    }

    /**
     * Peek at the dirty flag without consuming it.
     */
    bool IsFrameDirty() const { return frame_dirty_; }

    // Accessors
    uint32_t GetWidth()   const { return width_; }
    uint32_t GetHeight()  const { return height_; }
    bool     IsValid()    const { return context_ != EGL_NO_CONTEXT; }

    EGLDisplay GetDisplay() const { return display_; }
    EGLSurface GetSurface() const { return surface_; }
    EGLContext GetContext() const { return context_; }

private:
    bool CreateSurface(uint32_t width, uint32_t height);
    void DestroySurface();
    void DestroyIOSurfaceResources();
    void DestroyNativeWindowResources();

    /**
     * Acquire an EGL display with the specified backend.
     * On Android, uses eglGetPlatformDisplayEXT with Vulkan → OpenGLES fallback.
     * On other platforms, falls back to eglGetDisplay(EGL_DEFAULT_DISPLAY).
     * Updates `backend` in-place if a fallback was triggered.
     */
    EGLDisplay AcquireAngleDisplay(AngleBackend& backend);

    EGLDisplay display_  = EGL_NO_DISPLAY;
    EGLSurface surface_  = EGL_NO_SURFACE;
    EGLContext context_   = EGL_NO_CONTEXT;
    EGLConfig  config_    = nullptr;
    uint32_t   width_     = 0;
    uint32_t   height_    = 0;

    // Bridge backend type (Android only; macOS/iOS always use Metal)
    AngleBackend angle_backend_ = AngleBackend::OpenGLES;

    // IOSurface FBO resources (macOS zero-copy rendering)
    EGLSurface iosurface_pbuffer_   = EGL_NO_SURFACE;
    uint32_t   iosurface_fbo_       = 0;
    uint32_t   iosurface_texture_   = 0;
    uint32_t   iosurface_tex_target_= 0; // GL_TEXTURE_2D or GL_TEXTURE_RECTANGLE_ANGLE
    uint32_t   iosurface_rbo_depth_ = 0;
    uint32_t   iosurface_width_     = 0;
    uint32_t   iosurface_height_    = 0;
    uint32_t   iosurface_id_        = 0;

    // Android WindowSurface resources (SurfaceTexture zero-copy rendering)
    void*      native_window_       = nullptr; // ANativeWindow*
    EGLSurface window_surface_      = EGL_NO_SURFACE;
    uint32_t   window_width_        = 0;
    uint32_t   window_height_       = 0;

    // Frame dirty flag — prevents eglSwapBuffers on frames where
    // UpdateDrawBuffer() was not called, avoiding double-buffer
    // flicker (alternating between current and stale back-buffer).
    bool       frame_dirty_         = false;
};

/**
 * Get the global engine EGL context singleton.
 * Initialized once during engine bootstrap.
 */
EGLContextManager& GetEngineEGLContext();

} // namespace krkr
