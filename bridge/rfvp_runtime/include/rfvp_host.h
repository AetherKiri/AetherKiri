#pragma once
#include <stddef.h>
#include <stdint.h>

// Private C ABI between the C++ provider and the pinned Rust adapter.
#ifdef __cplusplus
extern "C" {
#endif
void* aether_rfvp_new(void);
void aether_rfvp_free(void* runtime);
int32_t aether_rfvp_probe(const char* path);
int32_t aether_rfvp_open(void* runtime, const char* path, const char* script,
    const char* writable, const char* font, const char* encoding);
int32_t aether_rfvp_tick(void* runtime, uint32_t delta_ms);
int32_t aether_rfvp_pause(void* runtime, uint32_t paused);
int32_t aether_rfvp_input(void* runtime, uint32_t kind, double x, double y,
    int32_t button, int32_t key, uint32_t modifiers, double wheel);
int32_t aether_rfvp_frame(void* runtime, uint32_t* width, uint32_t* height, uint64_t* serial);
int32_t aether_rfvp_read(void* runtime, void* pixels, size_t size);
const char* aether_rfvp_error(void* runtime);
uint32_t aether_rfvp_log(char* output, size_t size);
#ifdef __cplusplus
}
#endif
