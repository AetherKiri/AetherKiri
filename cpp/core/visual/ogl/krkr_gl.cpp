/**
 * @file krkr_gl.cpp
 * @brief Lightweight OpenGL state cache.
 */

#include "krkr_gl.h"
#include <algorithm>
#include <vector>

// Maximum number of texture units we track
static constexpr int kMaxTextureUnits = 16;

// Maximum number of vertex attribute slots we track
static constexpr int kMaxVertexAttribs = 16;

namespace krkr {
namespace gl {

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------
namespace {

// Currently active texture unit (GL_TEXTURE0 .. GL_TEXTUREN)
GLenum s_activeTextureUnit = GL_TEXTURE0;

// Bound texture per unit (index = unit - GL_TEXTURE0)
GLuint s_boundTextures[kMaxTextureUnits] = {};

// Currently used shader program
GLuint s_currentProgram = 0;

// Bitmask of currently enabled vertex attrib arrays
unsigned int s_enabledVertexAttribs = 0;

} // anonymous namespace

// ---------------------------------------------------------------------------
// Texture binding
// ---------------------------------------------------------------------------

void BindTexture2D(GLuint textureId) {
    BindTexture2DN(0, textureId);
}

void BindTexture2DN(unsigned int slot, GLuint textureId) {
    GLenum unit = GL_TEXTURE0 + slot;
    // Always call GL directly: external code (e.g. Application host) may
    // reset GL state behind our back, making cached values stale.
    s_activeTextureUnit = unit;
    glActiveTexture(unit);
    if (slot < kMaxTextureUnits) {
        s_boundTextures[slot] = textureId;
    }
    glBindTexture(GL_TEXTURE_2D, textureId);
}

void ActiveTexture(GLenum textureUnit) {
    s_activeTextureUnit = textureUnit;
    glActiveTexture(textureUnit);
}

void DeleteTexture(GLuint textureId) {
    // Invalidate from cache
    for (int i = 0; i < kMaxTextureUnits; ++i) {
        if (s_boundTextures[i] == textureId) {
            s_boundTextures[i] = 0;
        }
    }
    glDeleteTextures(1, &textureId);
}

// ---------------------------------------------------------------------------
// Shader program
// ---------------------------------------------------------------------------

void UseProgram(GLuint program) {
    s_currentProgram = program;
    glUseProgram(program);
}

// ---------------------------------------------------------------------------
// Vertex attributes
// ---------------------------------------------------------------------------

void EnableVertexAttribs(unsigned int flags) {
    // Always call GL directly to avoid stale cache issues.
    for (int i = 0; i < kMaxVertexAttribs; ++i) {
        unsigned int bit = 1u << i;
        if (flags & bit) {
            glEnableVertexAttribArray(i);
        } else if (s_enabledVertexAttribs & bit) {
            glDisableVertexAttribArray(i);
        }
    }
    s_enabledVertexAttribs = flags;
}

// ---------------------------------------------------------------------------
// Blend state
// ---------------------------------------------------------------------------

void BlendResetToCache() {
    // Force GL to re-apply blend state on next draw.
    // The original engine tracks blend src/dst factors; since the engine
    // always sets blend via raw glBlendFunc/glBlendFuncSeparate
    // before each draw, we just need to ensure the cache won't
    // suppress the next call. With raw GL calls this is a no-op
    // because there is no blend cache layer above us.
    //
    // Kept as an empty function for API compatibility; if we later
    // add blend caching, implement the reset here.
}

// ---------------------------------------------------------------------------
// Cache invalidation
// ---------------------------------------------------------------------------

void InvalidateStateCache() {
    s_activeTextureUnit = GL_TEXTURE0;
    for (int i = 0; i < kMaxTextureUnits; ++i) {
        s_boundTextures[i] = 0;
    }
    s_currentProgram = 0;
    s_enabledVertexAttribs = 0;
}

// ---------------------------------------------------------------------------
// Renderer recreated callbacks
// ---------------------------------------------------------------------------

namespace {
    std::vector<std::function<void()>> s_rendererRecreatedCallbacks;
} // anonymous namespace

void OnRendererRecreated(std::function<void()> callback) {
    s_rendererRecreatedCallbacks.push_back(std::move(callback));
}

void FireRendererRecreated() {
    // Invalidate all caches first
    InvalidateStateCache();
    // Then notify all registered listeners
    for (auto& cb : s_rendererRecreatedCallbacks) {
        cb();
    }
}

} // namespace gl
} // namespace krkr
