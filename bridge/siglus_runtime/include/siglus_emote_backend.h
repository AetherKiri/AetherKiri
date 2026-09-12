#ifndef AETHERKIRI_SIGLUS_EMOTE_BACKEND_H_
#define AETHERKIRI_SIGLUS_EMOTE_BACKEND_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Optional host-owned player implementation. This header contains no SDK
 * types or implementation. The function table is copied at registration;
 * callbacks must remain loaded until all players are destroyed. All calls
 * for one player are serialized on the engine thread. No C++ exception may
 * cross this boundary. Error buffers are NUL-terminated UTF-8.
 */
typedef struct siglus_emote_source_t {
    const uint8_t *bytes;
    size_t size;
} siglus_emote_source_t;

enum {
    SIGLUS_EMOTE_PROGRESS_MS = 1,
    SIGLUS_EMOTE_SET_VARIABLE = 2,
    SIGLUS_EMOTE_PLAY_TIMELINE = 3,
    SIGLUS_EMOTE_STOP_TIMELINE = 4,
    SIGLUS_EMOTE_STOP_ALL = 5,
    SIGLUS_EMOTE_PASS = 6,
    SIGLUS_EMOTE_SKIP = 7,
    SIGLUS_EMOTE_IS_ANIMATING = 8
};

typedef struct siglus_emote_backend_v1_t {
    uint32_t version; /* 1 */
    uint32_t size; /* sizeof(siglus_emote_backend_v1_t) */
    /* All normalized PSB sources belong to ONE composed player. Sources are
     * borrowed only during create; the backend must retain/copy as needed. */
    void *(*create)(const siglus_emote_source_t *, size_t, char *, size_t);
    void (*destroy)(void *);
    void *(*clone_player)(void *, char *, size_t);
    int32_t (*control)(void *, uint32_t, const char *, double, uint32_t,
                       int32_t *, char *, size_t);
    /* Exactly width*height*4 top-down, straight-alpha RGBA8 pixels. rep_x/y
     * follow Siglus's object render origin, not a host window position.
     * Returns 0 on success, a negative value on failure. */
    int32_t (*render_rgba)(void *, uint32_t, uint32_t, float, float,
                           uint8_t *, size_t, char *, size_t);
} siglus_emote_backend_v1_t;

int32_t siglus_ak_register_emote_backend(const siglus_emote_backend_v1_t *);

#ifdef __cplusplus
}
#endif
#endif
