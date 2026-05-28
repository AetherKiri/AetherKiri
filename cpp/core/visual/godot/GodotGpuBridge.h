#pragma once

#include <cstddef>
#include <cstdint>

struct tTVPRect;
struct tTVPPointD;

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
    bool (*blend_rect)(uint64_t dst, uint64_t src, const tTVPRect *dst_rect,
                       const tTVPRect *src_rect, uint32_t mode,
                       int opacity, uint32_t color);
    bool (*blend_rect2)(uint64_t dst, uint64_t src1, uint64_t src2,
                        const tTVPRect *dst_rect, const tTVPRect *src1_rect,
                        const tTVPRect *src2_rect, uint32_t mode,
                        int opacity, uint32_t color);
    bool (*read_rgba)(uint64_t texture, void *out_pixels,
                      size_t out_pixels_size, uint32_t stride_bytes);
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
};

extern "C" void TVPGodotGpuBridgeRegister(
    const TVPGodotGpuBridgeCallbacks *callbacks);
const TVPGodotGpuBridgeCallbacks *TVPGodotGpuBridgeGet();
