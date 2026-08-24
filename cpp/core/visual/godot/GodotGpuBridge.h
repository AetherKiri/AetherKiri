#pragma once

#include <cstddef>
#include <cstdint>

struct tTVPRect;
struct tTVPPointD;

// Texture synchronization on the producer side and queue execution in the
// Godot bridge must agree on the unset environment-variable behavior.
constexpr bool TVP_GODOT_DEFER_GPU_DRAIN_DEFAULT = false;

struct TVPGodotGpuBridgeCallbacks {
    uint64_t (*create_rgba)(uint32_t width, uint32_t height,
                            const void *pixels, uint32_t stride_bytes);
    void (*release_texture)(uint64_t texture);
    bool (*update_rgba)(uint64_t texture, const void *pixels,
                        uint32_t stride_bytes, const tTVPRect *rect);
    bool (*clear_rgba)(uint64_t texture, uint32_t rgba,
                       const tTVPRect *rect);
    bool (*copy_rect)(uint64_t dst, uint64_t src, const tTVPRect *dst_rect,
                      const tTVPRect *src_rect);
    bool (*copy_triangles)(uint64_t dst, uint64_t src, uint32_t triangle_count,
                           const tTVPRect *clip_rect,
                           const tTVPPointD *dst_points,
                           const tTVPPointD *src_points);
    bool (*draw_triangles)(uint64_t dst, uint64_t src, uint32_t triangle_count,
                           const tTVPRect *clip_rect,
                           const tTVPPointD *dst_points,
                           const tTVPPointD *src_points, float opacity,
                           uint32_t blend_mode);
    bool (*draw_masked_triangles)(
        uint64_t dst, uint64_t src, uint64_t mask, uint32_t triangle_count,
        const tTVPRect *clip_rect, const tTVPPointD *dst_points,
        const tTVPPointD *src_points, const tTVPPointD *mask_points,
        float opacity, uint32_t blend_mode, bool inverted_mask);
    bool (*mosaic_rects)(uint64_t texture, const tTVPRect *rects,
                         uint32_t rect_count, uint32_t block_x,
                         uint32_t block_y);
    bool (*blend_rect)(uint64_t dst, uint64_t src, const tTVPRect *dst_rect,
                       const tTVPRect *src_rect, uint32_t mode,
                       int opacity, uint32_t color);
    bool (*blend_rect2)(uint64_t dst, uint64_t src1, uint64_t src2,
                        const tTVPRect *dst_rect, const tTVPRect *src1_rect,
                        const tTVPRect *src2_rect, uint32_t mode,
                        int opacity, uint32_t color);
    bool (*blend_rect3)(uint64_t dst, uint64_t src1, uint64_t src2,
                        uint64_t src3, const tTVPRect *dst_rect,
                        const tTVPRect *src1_rect, const tTVPRect *src2_rect,
                        const tTVPRect *src3_rect, uint32_t mode,
                        int opacity, uint32_t color);
    bool (*read_rgba)(uint64_t texture, void *out_pixels,
                      size_t out_pixels_size, uint32_t stride_bytes);
    uint64_t (*begin_read_rgba)(uint64_t texture);
    bool (*poll_read_rgba)(uint64_t request, void *out_pixels,
                           size_t out_pixels_size, uint32_t stride_bytes,
                           bool *ready);
    void (*discard_read_rgba)(uint64_t request);
    bool (*flush)();
};

// Keep batch controls in a separate table: TVPGodotGpuBridgeCallbacks predates
// size/version fields and is copied by value across the engine/GDExtension ABI.
// Appending fields to that table would make a new engine read past an older
// extension's allocation when cached components are mixed.
struct TVPGodotGpuBatchCallbacks {
    uint32_t struct_size;
    uint32_t abi_version;
    uint64_t (*begin_batch)();
    bool (*end_batch)(uint64_t batch_token);
};

constexpr uint32_t TVP_GODOT_GPU_BATCH_CALLBACKS_ABI_VERSION = 1;

// Optional cross-API texture import used by native runtimes which render in
// their own graphics context. Keep this separate from the original callback
// table: the latter predates size/version fields and cannot be extended
// without making mixed public/private builds read past an older allocation.
struct TVPGodotGpuExternalTextureCallbacks {
    uint32_t struct_size;
    uint32_t abi_version;
    // On Apple platforms native_image is a retained CVPixelBufferRef whose
    // IOSurface is shared by OpenGL ES and Metal. The callback does not take
    // ownership; the producer must keep it alive until release_texture.
    uint64_t (*import_apple_pixel_buffer)(void *native_image,
                                          uint32_t width,
                                          uint32_t height);
    // Optional. Wait until every previously queued Godot use of this texture
    // has completed before the producer writes the shared IOSurface again.
    // Producers must use a multi-buffered target when this callback is null.
    bool (*prepare_for_native_write)(uint64_t texture);
    // On Android native_image is an AHardwareBuffer whose storage is shared
    // by the producer's OpenGL ES context and Godot's Vulkan RenderingDevice. The
    // callback retains its own buffer reference until release_texture.
    uint64_t (*import_android_hardware_buffer)(void *native_image,
                                                uint32_t width,
                                                uint32_t height);
    // Optional publication hook for backends whose native producer and
    // Godot use different graphics APIs. Called after the producer fence has
    // completed and before the texture is exposed to Godot composition.
    bool (*publish_native_write)(uint64_t texture);
};

constexpr uint32_t TVP_GODOT_GPU_EXTERNAL_TEXTURE_CALLBACKS_ABI_VERSION = 1;

// Limits deferred GPU draining to a producer-defined command group.  The
// bridge callbacks are optional so non-Godot renderers retain their current
// immediate behavior.
class TVPGodotGpuBatchScope {
public:
    explicit TVPGodotGpuBatchScope(bool enabled = true);
    ~TVPGodotGpuBatchScope() noexcept;

    TVPGodotGpuBatchScope(const TVPGodotGpuBatchScope &) = delete;
    TVPGodotGpuBatchScope &operator=(const TVPGodotGpuBatchScope &) = delete;

    bool active() const { return batch_token_ != 0; }
    bool finish();

private:
    uint64_t batch_token_ = 0;
    bool (*end_batch_)(uint64_t batch_token) = nullptr;
};

bool TVPGodotGpuBridgeBatchActive();

enum TVPGodotGpuBlendMode : uint32_t {
    TVP_GODOT_GPU_BLEND_ALPHA = 1,
    TVP_GODOT_GPU_BLEND_ALPHA_D = 2,
    TVP_GODOT_GPU_BLEND_COPY_COLOR = 3,
    TVP_GODOT_GPU_BLEND_CONST_ALPHA_SD = 4,
    TVP_GODOT_GPU_BLEND_FILL_ARGB = 5,
    TVP_GODOT_GPU_BLEND_ALPHA_A = 6,
    TVP_GODOT_GPU_BLEND_ALPHA_BLEND_A = 7,
    TVP_GODOT_GPU_BLEND_REMOVE_CONST_OPACITY = 8,
    TVP_GODOT_GPU_BLEND_CONST_ALPHA_SD_D = 9,
    TVP_GODOT_GPU_BLEND_CONST_ALPHA_D = 10,
    TVP_GODOT_GPU_BLEND_PS_SCREEN = 11,
    TVP_GODOT_GPU_BLEND_UNIVERSAL = 12,
    TVP_GODOT_GPU_BLEND_UNIVERSAL_D = 13,
    TVP_GODOT_GPU_BLEND_UNIVERSAL_A = 14,
    TVP_GODOT_GPU_BLEND_PS_MULTIPLY = 15,
    TVP_GODOT_GPU_BLEND_PS_ADD = 16,
    TVP_GODOT_GPU_BLEND_PS_SUBTRACT = 17,
    // Preserve RGB and replace only the destination alpha channel.
    TVP_GODOT_GPU_BLEND_FILL_MASK = 18,
    // Apply a source alpha mask to destination alpha while preserving RGB.
    TVP_GODOT_GPU_BLEND_APPLY_ALPHA_MASK = 19,
    // Copy all four channels exactly through the queued compute batch.
    TVP_GODOT_GPU_BLEND_COPY_RGBA = 20,
    // Apply a positive alpha mask to src1, then AlphaBlend_d it into dst in
    // one dispatch. src2 supplies the mask alpha.
    TVP_GODOT_GPU_BLEND_ALPHA_D_MASK_MULTIPLY = 21,
    // Threshold-mask src1 at alpha 64, then AlphaBlend_d it into dst.
    TVP_GODOT_GPU_BLEND_ALPHA_D_MASK_THRESHOLD = 22,
    // Apply an 8-bit glyph mask using KiriKiri additive-alpha color mapping.
    TVP_GODOT_GPU_BLEND_APPLY_COLOR_MAP_A = 23,
    // Composite an additive-alpha source while preserving destination alpha.
    TVP_GODOT_GPU_BLEND_ADDITIVE_ALPHA = 24,
    TVP_GODOT_GPU_BLEND_ADDITIVE_ALPHA_A = 25,
    // Symmetric box blur. The x radius is passed in opacity and the y radius
    // in the low byte of color; this reuses the stable blend callback ABI.
    TVP_GODOT_GPU_BLEND_BOX_BLUR_ALPHA = 26,
    // draw_triangles is shared by Cubism (whose low bits describe Cubism
    // colour/alpha modes) and KiriKiri (whose low bits are the modes above).
    // Tag the latter so AlphaBlend/AlphaBlend_d are not mistaken for Cubism
    // add/multiply flags by the bridge shader.
    TVP_GODOT_GPU_BLEND_TVP_OPERATION = 0x00010000u,
    // Request destination-alpha mask accumulation for a triangle mesh.  This
    // is a generic GPU bridge operation; the optional Live2D package uses it
    // to avoid rebuilding and uploading full mask textures on the CPU.
    TVP_GODOT_GPU_BLEND_MASK_WRITE = 0x00020000u,
};

// draw_triangles is also used by the Live2D renderer, whose low 16 bits carry
// Cubism blend flags.  Tag Kirikiri render-method modes separately so the
// bridge shader can preserve both interpretations without an ABI expansion.
constexpr uint32_t TVP_GODOT_GPU_TRIANGLE_TVP_BLEND =
    TVP_GODOT_GPU_BLEND_TVP_OPERATION;
// The source stores premultiplied RGB (native E-mote/OpenGL output). The
// triangle sampler must not multiply RGB by alpha a second time.
constexpr uint32_t TVP_GODOT_GPU_TRIANGLE_SOURCE_PREMULTIPLIED = 0x20000000u;

extern "C" void TVPGodotGpuBridgeRegister(
    const TVPGodotGpuBridgeCallbacks *callbacks);
const TVPGodotGpuBridgeCallbacks *TVPGodotGpuBridgeGet();
extern "C" void TVPGodotGpuBatchRegister(
    const TVPGodotGpuBatchCallbacks *callbacks);
const TVPGodotGpuBatchCallbacks *TVPGodotGpuBatchGet();
extern "C" void TVPGodotGpuExternalTextureRegister(
    const TVPGodotGpuExternalTextureCallbacks *callbacks);
const TVPGodotGpuExternalTextureCallbacks *
TVPGodotGpuExternalTextureGet();
