#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

// Private C ABI between the C++ provider and the pinned Rust adapter.
#ifdef __cplusplus
extern "C" {
#endif
#define AETHER_RFVP_GPU_CALLBACKS_API_VERSION 0x01000000u

typedef struct aether_rfvp_gpu_rect_t {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
} aether_rfvp_gpu_rect_t;

typedef struct aether_rfvp_gpu_point_t {
    double x;
    double y;
} aether_rfvp_gpu_point_t;

typedef struct aether_rfvp_gpu_callbacks_t {
    uint32_t struct_size;
    uint32_t api_version;
    uint64_t (*create_rgba)(uint32_t width, uint32_t height,
                            const void* pixels, uint32_t stride_bytes);
    void (*release_texture)(uint64_t texture);
    bool (*update_rgba)(uint64_t texture, const void* pixels,
                        uint32_t stride_bytes,
                        const aether_rfvp_gpu_rect_t* rect);
    bool (*clear_rgba)(uint64_t texture, uint32_t rgba,
                       const aether_rfvp_gpu_rect_t* rect);
    bool (*draw_triangles)(uint64_t dst, uint64_t src,
                           uint32_t triangle_count,
                           const aether_rfvp_gpu_rect_t* clip_rect,
                           const aether_rfvp_gpu_point_t* dst_points,
                           const aether_rfvp_gpu_point_t* src_points,
                           float opacity, uint32_t blend_mode);
    bool (*read_rgba)(uint64_t texture, void* out_pixels,
                      size_t out_pixels_size, uint32_t stride_bytes);
    uint64_t (*begin_batch)(void);
    bool (*end_batch)(uint64_t batch_token);
    bool (*flush)(void);
} aether_rfvp_gpu_callbacks_t;

void* aether_rfvp_new(const aether_rfvp_gpu_callbacks_t* gpu_callbacks);
void aether_rfvp_free(void* runtime);
int32_t aether_rfvp_probe(const char* path);
int32_t aether_rfvp_open(void* runtime, const char* path, const char* script,
    const char* writable, const char* font, const char* encoding,
    const char* renderer);
int32_t aether_rfvp_tick(void* runtime, uint32_t delta_ms);
int32_t aether_rfvp_pause(void* runtime, uint32_t paused);
int32_t aether_rfvp_input(void* runtime, uint32_t kind, double x, double y,
    int32_t button, int32_t key, uint32_t modifiers, double wheel);
int32_t aether_rfvp_frame(void* runtime, uint32_t* width, uint32_t* height, uint64_t* serial);
int32_t aether_rfvp_gpu_frame(void* runtime, uint64_t* texture,
    uint32_t* width, uint32_t* height, uint64_t* serial);
int32_t aether_rfvp_read(void* runtime, void* pixels, size_t size);
const char* aether_rfvp_error(void* runtime);
uint32_t aether_rfvp_log(char* output, size_t size);
#ifdef __cplusplus
}
#endif
