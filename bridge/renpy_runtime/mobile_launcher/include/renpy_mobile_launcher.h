#ifndef AETHERKIRI_RENPY_MOBILE_LAUNCHER_H
#define AETHERKIRI_RENPY_MOBILE_LAUNCHER_H

/*
 * Host-owned Ren'Py mobile lifecycle ABI.
 *
 * This header is a contract for a future fork of the official Ren'Py native
 * launcher. It deliberately contains no implementation and is not linked by
 * the current mobile provider. The fork must export these symbols from the
 * replacement librenpython.so (Android) and librenpython.a (Renios).
 *
 * The existing official libraries are process launchers: SDL_main and
 * launcher_main eventually call Py_RunMain and do not satisfy this ABI.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RENPY_MOBILE_LAUNCHER_ABI_VERSION 1u

typedef enum renpy_mobile_status {
    RENPY_MOBILE_OK = 0,
    RENPY_MOBILE_INVALID_ARGUMENT = -1,
    RENPY_MOBILE_INVALID_STATE = -2,
    RENPY_MOBILE_NOT_IMPLEMENTED = -3,
    RENPY_MOBILE_ERROR = -4,
} renpy_mobile_status_t;

typedef enum renpy_mobile_input_type {
    RENPY_MOBILE_INPUT_KEY = 1,
    RENPY_MOBILE_INPUT_POINTER = 2,
    RENPY_MOBILE_INPUT_TEXT = 3,
    RENPY_MOBILE_INPUT_GAMEPAD = 4,
} renpy_mobile_input_type_t;

typedef struct renpy_mobile_config {
    uint32_t struct_size;
    uint32_t abi_version;
    const char *private_root_utf8;
    const char *public_root_utf8;
    const char *apk_path_utf8;
    const char *argv0_utf8;
    const char *const *argv;
    int argc;
} renpy_mobile_config_t;

typedef struct renpy_mobile_host {
    uint32_t struct_size;
    uint32_t abi_version;
    void *user_data;

    /* Called on the host render thread. `rgba` remains owned by Ren'Py until
     * the callback returns. The host must copy it before returning. */
    int (*present_rgba)(void *user_data, const uint8_t *rgba,
                        uint32_t width, uint32_t height, uint32_t stride);

    /* Optional diagnostics hook. The string is UTF-8 and only borrowed for
     * the duration of the callback. */
    void (*log_utf8)(void *user_data, int level, const char *message);
} renpy_mobile_host_t;

typedef struct renpy_mobile_frame {
    uint32_t struct_size;
    const uint8_t *rgba;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint64_t serial;
} renpy_mobile_frame_t;

typedef struct renpy_mobile_input {
    uint32_t struct_size;
    uint32_t type;
    uint64_t timestamp_ns;
    int32_t device_id;
    int32_t code;
    int32_t value;
    float x;
    float y;
    float pressure;
    const char *text_utf8;
} renpy_mobile_input_t;

/* The only supported entrypoint sequence is:
 *
 *   init -> zero or more { tick, frame, input, pause/resume } -> shutdown
 *
 * init/tick/frame/input/pause/resume/shutdown must return promptly. In
 * particular, none of them may call Py_RunMain, SDL_main, UIApplicationMain,
 * SDL_RunApp, or create an Android Activity. */
int renpy_mobile_init(const renpy_mobile_config_t *config,
                      const renpy_mobile_host_t *host);
int renpy_mobile_tick(uint32_t budget_ms);
int renpy_mobile_frame(renpy_mobile_frame_t *out_frame);
int renpy_mobile_input(const renpy_mobile_input_t *event);
int renpy_mobile_pause(void);
int renpy_mobile_resume(void);
void renpy_mobile_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* AETHERKIRI_RENPY_MOBILE_LAUNCHER_H */
