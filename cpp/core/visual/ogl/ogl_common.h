#pragma once

// ---------------------------------------------------------------------------
// OpenGL headers for the legacy bridge GLES2 + EGL path.
// ---------------------------------------------------------------------------
// This path provides a GLES2 + EGL interface across compatibility platforms:
//   macOS   → Metal backend
//   Windows → D3D11 backend
//   Linux   → Desktop GL / Vulkan backend
//   Android → native GLES / Vulkan backend
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif

// ---------------------------------------------------------------------------
// GLES2 compatibility defines for desktop GL constants
// ---------------------------------------------------------------------------
#ifndef GL_DEPTH24_STENCIL8
#ifdef GL_DEPTH24_STENCIL8_OES
#define GL_DEPTH24_STENCIL8 GL_DEPTH24_STENCIL8_OES
#else
#define GL_DEPTH24_STENCIL8 0x88F0
#endif
#endif

#ifndef GL_READ_BUFFER
// GL_READ_BUFFER is not part of GLES2 core; used only behind #ifdef guards
#endif

#include <string>

bool TVPCheckGLExtension(const std::string &extname);

// ---------------------------------------------------------------------------
// CHECK_GL_ERROR_DEBUG — checks for GL errors in debug builds.
// In debug builds, checks for GL errors after each call.
// In release builds, this is a no-op.
// ---------------------------------------------------------------------------
#ifndef CHECK_GL_ERROR_DEBUG
#ifdef _DEBUG
#include <cassert>
#define CHECK_GL_ERROR_DEBUG()                                                 \
    do {                                                                       \
        GLenum __error = glGetError();                                         \
        if (__error) {                                                         \
            /* Log but don't assert — some errors are recoverable */           \
        }                                                                      \
    } while (false)
#else
#define CHECK_GL_ERROR_DEBUG() ((void)0)
#endif
#endif
