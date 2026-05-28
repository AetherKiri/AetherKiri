/**
 * @file krkr_texture2d.h
 * @brief Lightweight Texture2D replacement for CCTexture2D.
 *
 * This class provides just enough of the Texture2D API
 * that is actually used by RenderManager.cpp and RenderManager_ogl.cpp.
 * It is NOT a general-purpose texture class — only the methods actually
 * called in KiriKiri2 are implemented.
 *
 * Used in RenderManager.cpp:
 *   - new Texture2D / autorelease()
 *   - initWithData(data, dataLen, pixelFormat, width, height, size)
 *   - updateWithData(data, offsetX, offsetY, width, height)
 *   - getPixelsWide() / getPixelsHigh()
 *
 * Used in RenderManager_ogl.cpp (AdapterTexture2D):
 *   - _name, _contentSize, _maxS, _maxT, _pixelsWide, _pixelsHigh
 *   - _pixelFormat, _hasPremultipliedAlpha, _hasMipmaps
 *   - setGLProgram() — NOT needed (we remove this dependency)
 */
#pragma once

#include <cstddef>   // size_t, ssize_t
#include <cstring>   // memcpy
#include <cstdint>
#include <vector>

#if defined(KRKR_ENABLE_GPU_BRIDGE)
#include "ogl_common.h"
#else
using GLenum = unsigned int;
using GLuint = unsigned int;
#define GL_RGBA 0x1908
#define GL_RGB 0x1907
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_SHORT_4_4_4_4 0x8033
#define GL_UNSIGNED_SHORT_5_6_5 0x8363
#define GL_LUMINANCE 0x1909
#define GL_LUMINANCE_ALPHA 0x190A
#endif

namespace krkr {

// ---------------------------------------------------------------------------
// PixelFormat — mirrors the original engine pixel format enum
// ---------------------------------------------------------------------------
enum class PixelFormat {
    RGBA8888,
    RGB888,
    RGBA4444,
    RGB565,
    A8,
    I8,
    AI88,
    BGRA8888,
};

// ---------------------------------------------------------------------------
// Size — minimal Size struct for texture dimensions
// ---------------------------------------------------------------------------
struct Size {
    float width  = 0;
    float height = 0;

    Size() = default;
    Size(float w, float h) : width(w), height(h) {}

    static const Size ZERO;
};

inline const Size Size::ZERO = {0, 0};

// ---------------------------------------------------------------------------
// Texture2D — lightweight Texture2D for the krkr rendering pipeline
//
// Implements reference counting with autorelease() support.
// autorelease() is a no-op that just returns this — the caller is
// expected to manage lifetime manually or via the existing KiriKiri2
// reference counting in iTVPTexture2D.
// ---------------------------------------------------------------------------
class Texture2D {
public:
    Texture2D() = default;
    virtual ~Texture2D() {
#if defined(KRKR_ENABLE_GPU_BRIDGE)
        if (_name && _ownsTexture) {
            glDeleteTextures(1, &_name);
        }
#endif
    }

    // --- Reference counting (simplified autorelease pool is not needed) ---
    void autorelease() {
        // Originally, autorelease adds to a pool. Here we simply mark
        // that external code manages the lifetime. The caller (iTVPTexture2D)
        // handles Release() properly.
        _autoreleased = true;
    }

    // --- Initialization ---
    /**
     * Initialize with pixel data.
     *
     * @param data       Pixel data pointer
     * @param dataLen    Byte length of data (unused, kept for API compat)
     * @param format     Pixel format
     * @param pixelsWide Width in pixels
     * @param pixelsHigh Height in pixels
     * @param contentSize Content size (unused, kept for API compat)
     */
    bool initWithData(const void *data, ssize_t dataLen, PixelFormat format,
                      int pixelsWide, int pixelsHigh, const Size &contentSize) {
        _pixelsWide  = pixelsWide;
        _pixelsHigh  = pixelsHigh;
        _pixelFormat = format;
        _contentSize = Size(static_cast<float>(pixelsWide),
                            static_cast<float>(pixelsHigh));

        GLenum glFormat = GL_RGBA;
        GLenum glType   = GL_UNSIGNED_BYTE;
        resolveGLFormat(format, glFormat, glType);

#if defined(KRKR_ENABLE_GPU_BRIDGE)
        if (_name == 0) {
            glGenTextures(1, &_name);
            _ownsTexture = true;
        }
        glBindTexture(GL_TEXTURE_2D, _name);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glTexImage2D(GL_TEXTURE_2D, 0, glFormat, pixelsWide, pixelsHigh, 0,
                     glFormat, glType, data);
#else
        _cpuPixels.assign(static_cast<size_t>(pixelsWide) * pixelsHigh * 4, 0);
        if (data != nullptr) {
            const size_t copyLen = dataLen > 0
                ? static_cast<size_t>(dataLen)
                : _cpuPixels.size();
            std::memcpy(_cpuPixels.data(), data, copyLen < _cpuPixels.size() ? copyLen : _cpuPixels.size());
        }
#endif
        return true;
    }

    /**
     * Update a sub-region of the texture.
     */
    bool updateWithData(const void *data, int offsetX, int offsetY,
                        int width, int height) {
        GLenum glFormat = GL_RGBA;
        GLenum glType   = GL_UNSIGNED_BYTE;
        resolveGLFormat(_pixelFormat, glFormat, glType);

#if defined(KRKR_ENABLE_GPU_BRIDGE)
        if (_name == 0) return false;
        glBindTexture(GL_TEXTURE_2D, _name);
        glTexSubImage2D(GL_TEXTURE_2D, 0, offsetX, offsetY, width, height,
                        glFormat, glType, data);
#else
        if (data == nullptr || _cpuPixels.empty()) return false;
        const auto *src = static_cast<const uint8_t *>(data);
        for (int y = 0; y < height; ++y) {
            const size_t dstOffset =
                (static_cast<size_t>(offsetY + y) * _pixelsWide + offsetX) * 4;
            const size_t srcOffset = static_cast<size_t>(y) * width * 4;
            std::memcpy(_cpuPixels.data() + dstOffset, src + srcOffset,
                        static_cast<size_t>(width) * 4);
        }
#endif
        return true;
    }

    // --- Accessors ---
    GLuint getName()       const { return _name; }
    int    getPixelsWide() const { return _pixelsWide; }
    int    getPixelsHigh() const { return _pixelsHigh; }
    PixelFormat getPixelFormat() const { return _pixelFormat; }
    const Size& getContentSize() const { return _contentSize; }

    // --- Fields exposed for AdapterTexture2D in RenderManager_ogl.cpp ---
    // These mirror the original Texture2D protected members that AdapterTexture2D
    // directly accesses.
    GLuint _name = 0;
    Size   _contentSize;
    float  _maxS = 1.0f;
    float  _maxT = 1.0f;
    int    _pixelsWide = 0;
    int    _pixelsHigh = 0;
    PixelFormat _pixelFormat = PixelFormat::RGBA8888;
    bool   _hasPremultipliedAlpha = false;
    bool   _hasMipmaps = false;

protected:
    bool _ownsTexture = false;
    bool _autoreleased = false;
    std::vector<uint8_t> _cpuPixels;

    static void resolveGLFormat(PixelFormat format, GLenum &glFormat, GLenum &glType) {
        switch (format) {
            case PixelFormat::RGBA8888:
                glFormat = GL_RGBA; glType = GL_UNSIGNED_BYTE; break;
            case PixelFormat::RGB888:
                glFormat = GL_RGB;  glType = GL_UNSIGNED_BYTE; break;
            case PixelFormat::RGBA4444:
                glFormat = GL_RGBA; glType = GL_UNSIGNED_SHORT_4_4_4_4; break;
            case PixelFormat::RGB565:
                glFormat = GL_RGB;  glType = GL_UNSIGNED_SHORT_5_6_5; break;
            case PixelFormat::A8:
            case PixelFormat::I8:
                glFormat = GL_LUMINANCE; glType = GL_UNSIGNED_BYTE; break;
            case PixelFormat::AI88:
                glFormat = GL_LUMINANCE_ALPHA; glType = GL_UNSIGNED_BYTE; break;
            case PixelFormat::BGRA8888:
#ifdef GL_BGRA
                glFormat = GL_BGRA; glType = GL_UNSIGNED_BYTE; break;
#else
                glFormat = GL_RGBA; glType = GL_UNSIGNED_BYTE; break;
#endif
        }
    }
};

} // namespace krkr
