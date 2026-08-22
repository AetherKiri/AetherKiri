#ifndef AETHERKIRI_SIGLUS_FFI_H_
#define AETHERKIRI_SIGLUS_FFI_H_

#include <stdint.h>

/*
 * C ABI contract between bridge/siglus_runtime (C++) and the siglus_rs
 * workspace (Rust, packages/siglus_rs).
 *
 * Phase 2 skeleton: only the ABI version handshake is defined so the C++ side
 * can compile against a stable surface while the Rust "aether host" is still
 * being implemented inside siglus_scene_vm. Phase 3 extends this header with:
 *
 *   siglus_ak_create(game_root, writable_path, host_callbacks) -> handle
 *   siglus_ak_open_game(handle, game_root, startup_script)
 *   siglus_ak_tick(handle, delta_ms)
 *   siglus_ak_resize(handle, width, height)
 *   siglus_ak_read_frame_rgba(handle, out_pixels, out_size)
 *   siglus_ak_send_input(handle, event)
 *   siglus_ak_shutdown(handle)
 *
 * The rendering plan is offscreen wgpu -> CPU readback via read_frame_rgba,
 * matching how onscripter delivers frames; a shared-texture path may replace
 * it later.
 */

#define SIGLUS_AK_FFI_API_VERSION 0x00010000u

#ifdef __cplusplus
extern "C" {
#endif

/* Returns SIGLUS_AK_FFI_API_VERSION of the linked Rust implementation.
 * Declared here now so the C++ side links against the archive and any ABI
 * drift fails at link time instead of surfacing as a runtime mismatch. */
uint32_t siglus_ak_ffi_api_version(void);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* AETHERKIRI_SIGLUS_FFI_H_ */
