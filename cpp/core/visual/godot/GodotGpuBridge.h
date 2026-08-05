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
    // Linearly interpolate all four source channels. Used by additive-alpha
    // page fades, where the alpha byte is part of the transition result.
    TVP_GODOT_GPU_BLEND_CONST_ALPHA_SD_A = 23,
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

extern "C" void TVPGodotGpuBridgeRegister(
    const TVPGodotGpuBridgeCallbacks *callbacks);
const TVPGodotGpuBridgeCallbacks *TVPGodotGpuBridgeGet();
