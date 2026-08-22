#ifndef AETHERKIRI_SIGLUS_FFI_H_
#define AETHERKIRI_SIGLUS_FFI_H_

#include <stddef.h>
#include <stdint.h>

/*
 * C ABI contract between bridge/siglus_runtime (C++) and the siglus_rs
 * "aether host" (Rust, applied to packages/siglus_rs sources at build time
 * from bridge/siglus_runtime/overlay/files/aether_host.rs).
 *
 * The Rust side is headless: frames render offscreen via wgpu and are read
 * back over the CPU; input arrives through explicit calls. Error codes mirror
 * engine_result_t from engine_api.h so they can be passed through directly.
 */

#define SIGLUS_AK_FFI_API_VERSION 0x00010000u

/* Result codes (mirror engine_result_t). */
#define SIGLUS_AK_OK 0
#define SIGLUS_AK_EXIT_REQUESTED 1
#define SIGLUS_AK_INVALID_ARGUMENT (-1)
#define SIGLUS_AK_INVALID_STATE (-2)
#define SIGLUS_AK_NOT_SUPPORTED (-3)
#define SIGLUS_AK_IO_ERROR (-4)
#define SIGLUS_AK_INTERNAL_ERROR (-5)

/* Mouse button codes accepted by siglus_ak_mouse_button. */
#define SIGLUS_AK_BUTTON_LEFT 0
#define SIGLUS_AK_BUTTON_RIGHT 1
#define SIGLUS_AK_BUTTON_MIDDLE 2

#ifdef __cplusplus
extern "C" {
#endif

/* ABI handshake; call once and compare against SIGLUS_AK_FFI_API_VERSION. */
uint32_t siglus_ak_ffi_api_version(void);

/* Creates an empty host shell (no GPU work yet). Owned; destroy exactly once
 * with siglus_ak_destroy. scale_factor > 0 selects the logical pixel density;
 * non-finite or non-positive values fall back to 1.0. */
void *siglus_ak_create(float scale_factor);

/* Builds the offscreen renderer and boots the scene VM synchronously.
 * Returns SIGLUS_AK_OK, or a negative code with details from
 * siglus_ak_last_error. Only one game can be open per shell. */
int32_t siglus_ak_open(void *handle, const char *game_root_utf8,
                       uint32_t width, uint32_t height);

/* Tears down the open game but keeps the shell usable for another open. */
void siglus_ak_close(void *handle);

/* Destroys the shell; the handle is invalid afterwards. */
void siglus_ak_destroy(void *handle);

int32_t siglus_ak_resize(void *handle, uint32_t width, uint32_t height);

/* Advances simulation + renders one offscreen frame. Returns SIGLUS_AK_OK,
 * SIGLUS_AK_EXIT_REQUESTED when the engine asked to quit, or negative. */
int32_t siglus_ak_step(void *handle, uint32_t dt_ms);

/* Frame geometry of the latest rendered frame (tightly packed RGBA rows).
 * Out pointers may be null individually. */
int32_t siglus_ak_get_frame_desc(void *handle, uint32_t *out_width,
                                 uint32_t *out_height,
                                 uint32_t *out_stride_bytes);

/* Blocks until the staged frame lands in CPU memory and copies exactly
 * width * height * 4 bytes into out_pixels. Call after every successful
 * siglus_ak_step. */
int32_t siglus_ak_read_frame_rgba(void *handle, uint8_t *out_pixels,
                                  size_t out_size);

int32_t siglus_ak_mouse_move(void *handle, double x, double y);
int32_t siglus_ak_mouse_button(void *handle, int32_t button, int32_t pressed);
/* phase: 0 = begin, 1 = move, 2 = end (upstream mobile-host convention). */
int32_t siglus_ak_touch(void *handle, int32_t phase, double x, double y);
int32_t siglus_ak_mouse_wheel(void *handle, int32_t delta_y);
/* Platform/VK-style key codes (0x1B escape, ASCII letters/digits, ...). */
int32_t siglus_ak_key(void *handle, int32_t key_code, int32_t pressed);
/* NUL-terminated UTF-8; null clears the IME composition. */
int32_t siglus_ak_text_input(void *handle, const char *text_utf8);

/* Native message box bridge; pass null callback for engine-internal UI.
 * String pointers are valid only for the duration of the callback; answers
 * are delivered later via siglus_ak_submit_messagebox_result. */
typedef void (*siglus_ak_messagebox_fn)(void *user_data, uint64_t request_id,
                                        int32_t kind, const char *title_utf8,
                                        const char *message_utf8);
int32_t siglus_ak_set_messagebox_callback(void *handle,
                                          siglus_ak_messagebox_fn callback,
                                          void *user_data);
int32_t siglus_ak_submit_messagebox_result(void *handle, uint64_t request_id,
                                           int64_t value);

/* Last error for this shell; valid until the next FFI call on it. */
const char *siglus_ak_last_error(void *handle);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* AETHERKIRI_SIGLUS_FFI_H_ */
