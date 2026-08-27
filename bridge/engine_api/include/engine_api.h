#ifndef KRKR2_ENGINE_API_H_
#define KRKR2_ENGINE_API_H_

#include <stddef.h>
#include <stdint.h>

/* Export macro for shared-library builds. */
#if defined(_WIN32)
#if defined(ENGINE_API_BUILD_SHARED)
#define ENGINE_API_EXPORT __declspec(dllexport)
#elif defined(ENGINE_API_USE_SHARED)
#define ENGINE_API_EXPORT __declspec(dllimport)
#else
#define ENGINE_API_EXPORT
#endif
#else
#if defined(__GNUC__) && __GNUC__ >= 4 && \
    (defined(ENGINE_API_BUILD_SHARED) || defined(ENGINE_API_EXPORT_SYMBOLS))
#define ENGINE_API_EXPORT __attribute__((visibility("default")))
#else
#define ENGINE_API_EXPORT
#endif
#endif

#if defined(__cplusplus)
extern "C" {
#endif

/* ABI version: major(8bit), minor(8bit), patch(16bit). */
#define ENGINE_API_VERSION 0x01050000u
#define ENGINE_API_MAKE_VERSION(major, minor, patch) \
  ((((uint32_t)(major)&0xFFu) << 24u) | (((uint32_t)(minor)&0xFFu) << 16u) | \
   ((uint32_t)(patch)&0xFFFFu))

typedef struct engine_handle_s* engine_handle_t;
typedef struct engine_media_handle_s* engine_media_handle_t;

/* Registers host renderer callbacks. The callback table is renderer-specific. */
ENGINE_API_EXPORT void engine_register_godot_gpu_bridge(
    const void* callbacks);
/* Registers optional producer-batch callbacks separately so the legacy Godot
 * GPU callback table remains ABI-stable. */
ENGINE_API_EXPORT void engine_register_godot_gpu_batch_bridge(
    const void* callbacks);
/* Registers the optional native shared-texture import table. It is kept
 * separate from the legacy GPU bridge ABI so older private packages remain
 * binary-safe. */
ENGINE_API_EXPORT void engine_register_godot_gpu_external_texture_bridge(
    const void* callbacks);

typedef enum engine_result_t {
  ENGINE_RESULT_OK = 0,
  ENGINE_RESULT_INVALID_ARGUMENT = -1,
  ENGINE_RESULT_INVALID_STATE = -2,
  ENGINE_RESULT_NOT_SUPPORTED = -3,
  ENGINE_RESULT_IO_ERROR = -4,
  ENGINE_RESULT_INTERNAL_ERROR = -5
} engine_result_t;

typedef struct engine_create_desc_t {
  uint32_t struct_size;
  uint32_t api_version;
  const char* writable_path_utf8;
  const char* cache_path_utf8;
  void* user_data;
  uint64_t reserved_u64[4];
  void* reserved_ptr[4];
} engine_create_desc_t;

typedef struct engine_option_t {
  const char* key_utf8;
  const char* value_utf8;
  uint64_t reserved_u64[2];
  void* reserved_ptr[2];
} engine_option_t;

typedef enum engine_pixel_format_t {
  ENGINE_PIXEL_FORMAT_UNKNOWN = 0,
  ENGINE_PIXEL_FORMAT_RGBA8888 = 1
} engine_pixel_format_t;

typedef struct engine_frame_desc_t {
  uint32_t struct_size;
  uint32_t width;
  uint32_t height;
  uint32_t stride_bytes;
  uint32_t pixel_format;
  uint64_t frame_serial;
  uint64_t reserved_u64[4];
  void* reserved_ptr[4];
} engine_frame_desc_t;

typedef enum engine_media_status_t {
  ENGINE_MEDIA_STATUS_IDLE = 0,
  ENGINE_MEDIA_STATUS_PLAYING = 1,
  ENGINE_MEDIA_STATUS_PAUSED = 2,
  ENGINE_MEDIA_STATUS_ENDED = 3,
  ENGINE_MEDIA_STATUS_ERROR = 4
} engine_media_status_t;

typedef struct engine_media_state_t {
  uint32_t struct_size;
  uint32_t status;
  uint32_t width;
  uint32_t height;
  int64_t position_ms;
  int64_t duration_ms;
  double playback_rate;
  uint64_t frame_serial;
  uint32_t frame_ready;
  uint32_t seekable;
  uint32_t has_audio;
  uint32_t has_video;
  uint64_t reserved_u64[4];
  void* reserved_ptr[4];
} engine_media_state_t;

typedef struct engine_memory_stats_t {
  uint32_t struct_size;
  uint32_t self_used_mb;
  uint32_t system_free_mb;
  uint32_t system_total_mb;

  uint64_t graphic_cache_bytes;
  uint64_t graphic_cache_limit_bytes;
  uint64_t xp3_segment_cache_bytes;

  uint64_t psb_cache_bytes;
  uint32_t psb_cache_entries;
  uint32_t psb_cache_entry_limit;
  uint64_t psb_cache_hits;
  uint64_t psb_cache_misses;

  uint32_t archive_cache_entries;
  uint32_t archive_cache_limit;
  uint32_t autopath_cache_entries;
  uint32_t autopath_cache_limit;
  uint32_t autopath_table_entries;
  uint32_t reserved_u32;

  /* Process-memory details. On Apple platforms physical_footprint and
   * available bytes track the values used by the app memory limit. Other
   * platforms may report resident memory and leave unavailable fields at 0. */
  uint64_t process_resident_bytes;
  uint64_t process_physical_footprint_bytes;
  uint64_t process_peak_physical_footprint_bytes;
  uint64_t process_available_bytes;
  void* reserved_ptr[4];
} engine_memory_stats_t;

/* Structured diagnostic categories. Values are a stable ABI bit mask. */
typedef enum engine_diagnostic_category_t {
  ENGINE_DIAGNOSTIC_CATEGORY_LIFECYCLE = 1u << 0u,
  ENGINE_DIAGNOSTIC_CATEGORY_INPUT = 1u << 1u,
  ENGINE_DIAGNOSTIC_CATEGORY_RENDER = 1u << 2u,
  ENGINE_DIAGNOSTIC_CATEGORY_STORAGE = 1u << 3u,
  ENGINE_DIAGNOSTIC_CATEGORY_SCRIPT = 1u << 4u,
  ENGINE_DIAGNOSTIC_CATEGORY_AUDIO = 1u << 5u,
  ENGINE_DIAGNOSTIC_CATEGORY_VIDEO = 1u << 6u,
  ENGINE_DIAGNOSTIC_CATEGORY_PLUGIN = 1u << 7u,
  ENGINE_DIAGNOSTIC_CATEGORY_MEMORY = 1u << 8u,
  ENGINE_DIAGNOSTIC_CATEGORY_SYSTEM = 1u << 9u,
  ENGINE_DIAGNOSTIC_CATEGORY_ALL = 0x3ffu
} engine_diagnostic_category_t;

typedef struct engine_diagnostic_config_t {
  uint32_t struct_size;
  uint32_t enabled;
  uint64_t category_mask;
  uint32_t slow_frame_threshold_us;
  uint32_t max_events;
  /* Caller clock sampled immediately before applying this config. When set,
   * native event timestamps are translated into the caller's monotonic domain. */
  uint64_t host_monotonic_origin_us;
  const char* session_id_utf8;
  uint64_t reserved_u64[4];
  void* reserved_ptr[4];
} engine_diagnostic_config_t;

typedef enum engine_input_event_type_t {
  ENGINE_INPUT_EVENT_POINTER_DOWN = 1,
  ENGINE_INPUT_EVENT_POINTER_MOVE = 2,
  ENGINE_INPUT_EVENT_POINTER_UP = 3,
  ENGINE_INPUT_EVENT_POINTER_SCROLL = 4,
  ENGINE_INPUT_EVENT_KEY_DOWN = 5,
  ENGINE_INPUT_EVENT_KEY_UP = 6,
  ENGINE_INPUT_EVENT_TEXT_INPUT = 7,
  ENGINE_INPUT_EVENT_BACK = 8
} engine_input_event_type_t;

/* A gesture recognizer sets this on POINTER_UP when a forwarded press was
 * reclassified and must be cancelled without completing a click. */
typedef enum engine_input_modifier_t {
  ENGINE_INPUT_MODIFIER_POINTER_CANCEL = 1u << 30
} engine_input_modifier_t;

typedef enum engine_text_input_state_flag_t {
  ENGINE_TEXT_INPUT_STATE_NONE = 0,
  ENGINE_TEXT_INPUT_STATE_ACTIVE = 1u << 0
} engine_text_input_state_flag_t;

typedef enum engine_startup_state_t {
  ENGINE_STARTUP_STATE_IDLE = 0,
  ENGINE_STARTUP_STATE_RUNNING = 1,
  ENGINE_STARTUP_STATE_SUCCEEDED = 2,
  ENGINE_STARTUP_STATE_FAILED = 3
} engine_startup_state_t;

typedef struct engine_input_event_t {
  uint32_t struct_size;
  uint32_t type;
  uint64_t timestamp_micros;
  double x;
  double y;
  double delta_x;
  double delta_y;
  int32_t pointer_id;
  int32_t button;
  int32_t key_code;
  int32_t modifiers;
  uint32_t unicode_codepoint;
  uint32_t reserved_u32;
  uint64_t reserved_u64[2];
  void* reserved_ptr[2];
} engine_input_event_t;

/* Host-facing snapshot of the focused KiriKiri text input layer. */
typedef struct engine_text_input_state_t {
  uint32_t struct_size;
  /* Non-zero when the focused layer's IME mode accepts composed input. */
  uint32_t ime_active;
  int32_t ime_mode;
  /* Non-zero when the focused layer publishes an editable caret position. */
  uint32_t attention_point_valid;
  int32_t attention_x;
  int32_t attention_y;
  /* Backing text is copied separately with engine_copy_text_input_text(). */
  uint32_t text_available;
  uint32_t text_utf8_bytes;
  /* Unicode scalar offsets; equal values mean there is no selection. */
  int32_t selection_start;
  int32_t selection_end;
  uint64_t reserved_u64[2];
  void* reserved_ptr[4];
} engine_text_input_state_t;

/*
 * Returns runtime API version in out_api_version.
 * out_api_version must be non-null.
 */
ENGINE_API_EXPORT engine_result_t engine_get_runtime_api_version(
    uint32_t* out_api_version);

/*
 * Creates an engine handle.
 * desc and out_handle must be non-null.
 * out_handle is set only when ENGINE_RESULT_OK is returned.
 */
ENGINE_API_EXPORT engine_result_t engine_create(const engine_create_desc_t* desc,
                                                engine_handle_t* out_handle);

/*
 * Destroys engine handle and releases all resources.
 * Idempotent: passing a null handle returns ENGINE_RESULT_OK.
 */
ENGINE_API_EXPORT engine_result_t engine_destroy(engine_handle_t handle);

/*
 * Retrieves one platform operation emitted by a runtime provider. Requests
 * are queued across asynchronous startup and must be consumed on the host UI
 * thread. If no request is pending, out_available is set to zero and the
 * function returns OK. A too-small output buffer leaves the request queued.
 */
ENGINE_API_EXPORT engine_result_t engine_poll_platform_request(
    engine_handle_t handle, char* operation_buffer,
    uint32_t operation_buffer_size, char* argument_buffer,
    uint32_t argument_buffer_size, uint32_t* out_available);

/*
 * Returns the asynchronous result of a platform request to the active runtime
 * provider. operation_utf8 must match the request operation; argument_utf8 is
 * provider-defined UTF-8 data.
 */
ENGINE_API_EXPORT engine_result_t engine_submit_platform_response(
    engine_handle_t handle, const char* operation_utf8,
    const char* argument_utf8);

/*
 * Opens a game package/root directory.
 * handle and game_root_path_utf8 must be non-null.
 * startup_script_utf8 may be null to use default startup script.
 */
ENGINE_API_EXPORT engine_result_t engine_open_game(
    engine_handle_t handle, const char* game_root_path_utf8,
    const char* startup_script_utf8);

/*
 * Starts game opening asynchronously on a background worker.
 * Returns immediately when the startup task is scheduled.
 */
ENGINE_API_EXPORT engine_result_t engine_open_game_async(
    engine_handle_t handle, const char* game_root_path_utf8,
    const char* startup_script_utf8);

/*
 * Gets async startup state.
 * out_state must be non-null.
 */
ENGINE_API_EXPORT engine_result_t engine_get_startup_state(
    engine_handle_t handle, uint32_t* out_state);

/*
 * Drains startup logs into caller buffer as UTF-8 text.
 * Each log line is terminated by '\n'.
 * Returns bytes written in out_bytes_written.
 */
ENGINE_API_EXPORT engine_result_t engine_drain_startup_logs(
    engine_handle_t handle, char* out_buffer, uint32_t buffer_size,
    uint32_t* out_bytes_written);

/*
 * Ticks engine main loop once.
 * handle must be non-null.
 * delta_ms is caller-provided elapsed milliseconds.
 */
ENGINE_API_EXPORT engine_result_t engine_tick(engine_handle_t handle,
                                              uint32_t delta_ms);

/*
 * Pauses runtime execution.
 * Idempotent: calling pause on a paused engine returns ENGINE_RESULT_OK.
 */
ENGINE_API_EXPORT engine_result_t engine_pause(engine_handle_t handle);

/*
 * Resumes runtime execution.
 * Idempotent: calling resume on a running engine returns ENGINE_RESULT_OK.
 */
ENGINE_API_EXPORT engine_result_t engine_resume(engine_handle_t handle);

/*
 * Sets runtime option by UTF-8 key/value pair.
 * handle and option must be non-null.
 */
ENGINE_API_EXPORT engine_result_t engine_set_option(engine_handle_t handle,
                                                    const engine_option_t* option);

/*
 * Sets the host presentation surface size in pixels. Runtime-specific game
 * coordinates remain in the game's logical coordinate space.
 * width and height must be greater than zero.
 */
ENGINE_API_EXPORT engine_result_t engine_set_surface_size(engine_handle_t handle,
                                                          uint32_t width,
                                                          uint32_t height);

/*
 * Gets current frame descriptor.
 * out_frame_desc->struct_size must be initialized by caller.
 */
ENGINE_API_EXPORT engine_result_t engine_get_frame_desc(
    engine_handle_t handle, engine_frame_desc_t* out_frame_desc);

/*
 * Reads current frame into caller-provided RGBA8888 buffer.
 * out_pixels_size must be >= stride_bytes * height from engine_get_frame_desc.
 */
ENGINE_API_EXPORT engine_result_t engine_read_frame_rgba(
    engine_handle_t handle, void* out_pixels, size_t out_pixels_size);

/*
 * Opens a standalone local media file with the runtime FFmpeg pipeline.
 * Only one host-visible media player is expected to be active at a time.
 */
ENGINE_API_EXPORT engine_result_t engine_media_open(
    engine_handle_t engine, const char* path_utf8,
    engine_media_handle_t* out_media);

ENGINE_API_EXPORT engine_result_t engine_media_destroy(
    engine_media_handle_t media);

ENGINE_API_EXPORT engine_result_t engine_media_play(
    engine_media_handle_t media);

ENGINE_API_EXPORT engine_result_t engine_media_pause(
    engine_media_handle_t media);

ENGINE_API_EXPORT engine_result_t engine_media_seek(
    engine_media_handle_t media, int64_t position_ms);

ENGINE_API_EXPORT engine_result_t engine_media_set_rate(
    engine_media_handle_t media, double playback_rate);

ENGINE_API_EXPORT engine_result_t engine_media_get_state(
    engine_media_handle_t media, engine_media_state_t* out_state);

/*
 * Lists embedded text subtitle streams as UTF-8 JSON.
 * The result is an array of objects containing stream_index, codec, language,
 * title, and default. Writes bytes written in out_bytes_written.
 */
ENGINE_API_EXPORT engine_result_t engine_media_get_subtitle_tracks_json(
    engine_media_handle_t media, char* out_buffer, uint32_t buffer_size,
    uint32_t* out_bytes_written);

/*
 * Extracts one embedded text subtitle stream to an ASS sidecar file.
 * The stream index must come from engine_media_get_subtitle_tracks_json.
 */
ENGINE_API_EXPORT engine_result_t engine_media_extract_subtitle(
    engine_media_handle_t media, int32_t stream_index,
    const char* output_path_utf8);

/*
 * Copies the most recent due video frame as tightly packed RGBA8888.
 * Call engine_media_get_state first and allocate width * height * 4 bytes.
 */
ENGINE_API_EXPORT engine_result_t engine_media_read_frame_rgba(
    engine_media_handle_t media, void* out_pixels, size_t out_pixels_size,
    engine_frame_desc_t* out_frame_desc);

/*
 * Gets the current Godot-native GPU texture id for zero-copy display.
 * The id is owned by the Godot GDExtension render bridge and can be
 * resolved to a Texture2DRD there. Returns NOT_SUPPORTED when the current
 * frame is not backed by a Godot RenderingDevice texture.
 */
ENGINE_API_EXPORT engine_result_t engine_get_godot_native_frame_texture(
    engine_handle_t handle, uint64_t* out_texture_id, uint32_t* out_width,
    uint32_t* out_height, uint64_t* out_frame_serial);

/* Runtime-specific state for the most recent Godot-native frame. */
#define ENGINE_GODOT_PRESENTATION_STATE_NONE 0u
#define ENGINE_GODOT_PRESENTATION_STATE_RESET_HISTORY (1u << 0)

ENGINE_API_EXPORT engine_result_t engine_get_godot_presentation_state(
    engine_handle_t handle, uint32_t* out_state_flags);

/*
 * Gets host-native render window handle.
 * On macOS runtime build this is NSWindow*.
 * Returns ENGINE_RESULT_NOT_SUPPORTED on unsupported platforms/builds.
 */
ENGINE_API_EXPORT engine_result_t engine_get_host_native_window(
    engine_handle_t handle, void** out_window_handle);

/*
 * Gets host-native render view handle.
 * On macOS runtime build this is NSView* (typically the GLFW content view).
 * Returns ENGINE_RESULT_NOT_SUPPORTED on unsupported platforms/builds.
 */
ENGINE_API_EXPORT engine_result_t engine_get_host_native_view(
    engine_handle_t handle, void** out_view_handle);

/*
 * Sends one input event to the runtime.
 * event->struct_size must be initialized by caller.
 */
ENGINE_API_EXPORT engine_result_t engine_send_input(engine_handle_t handle,
                                                    const engine_input_event_t* event);

/*
 * Gets the current script-visible text input/IME focus state.
 * out_state->struct_size must be initialized by caller.
 */
ENGINE_API_EXPORT engine_result_t engine_get_text_input_state(
    engine_handle_t handle, engine_text_input_state_t* out_state);

/*
 * Copies the focused editor's backing text as null-terminated UTF-8.
 * text_utf8_bytes in engine_text_input_state_t reports the full byte count,
 * excluding the terminator. A smaller destination is safely truncated.
 */
ENGINE_API_EXPORT engine_result_t engine_copy_text_input_text(
    engine_handle_t handle, char* out_buffer, uint32_t buffer_size,
    uint32_t* out_bytes_written);

/*
 * Exports the current main window menu tree as UTF-8 JSON.
 * Writes bytes written in out_bytes_written.
 * Returns an empty JSON array ("[]") when no menu is available.
 */
ENGINE_API_EXPORT engine_result_t engine_get_main_menu_json(
    engine_handle_t handle, char* out_buffer, uint32_t buffer_size,
    uint32_t* out_bytes_written);

/*
 * Activates one exported menu item by slash-separated index path
 * (for example "0/2/1").
 */
ENGINE_API_EXPORT engine_result_t engine_activate_menu_item(
    engine_handle_t handle, const char* item_path_utf8);

/*
 * Sets an IOSurface as the render target for the engine.
 * When set, engine_tick renders directly to this IOSurface (zero-copy),
 * bypassing the glReadPixels path used by engine_read_frame_rgba.
 *
 * iosurface_id: The IOSurfaceID obtained from IOSurfaceGetID().
 *               Pass 0 to detach and revert to the default Pbuffer mode.
 * width/height: Dimensions of the IOSurface in pixels.
 *
 * Platform: macOS only. Returns ENGINE_RESULT_NOT_SUPPORTED on other platforms.
 */
ENGINE_API_EXPORT engine_result_t engine_set_render_target_iosurface(
    engine_handle_t handle, uint32_t iosurface_id,
    uint32_t width, uint32_t height);

/*
 * Sets an Android Surface (from SurfaceTexture) as the render target.
 * When set, engine_tick renders to an EGL WindowSurface created from the
 * ANativeWindow. The GPU bridge can deliver frames directly to the host
 * texture path.
 *
 * native_window: ANativeWindow* obtained from ANativeWindow_fromSurface().
 *                Pass NULL to detach and revert to the default Pbuffer mode.
 * width/height: Dimensions in pixels.
 *
 * Platform: Android only. Returns ENGINE_RESULT_NOT_SUPPORTED on other platforms.
 */
ENGINE_API_EXPORT engine_result_t engine_set_render_target_surface(
    engine_handle_t handle, void* native_window,
    uint32_t width, uint32_t height);

/*
 * Queries whether the last engine_tick produced a new rendered frame.
 * out_
 *
 * out_
 *   - 0: no new frame since last query
 *   - 1: a new frame was rendered
 *
 * This is useful in external texture modes to know when to notify the host
 * display path.
 */
ENGINE_API_EXPORT engine_result_t engine_get_frame_rendered_flag(
    engine_handle_t handle, uint32_t* out_
);

/*
 * Queries the graphics renderer information string.
 * Writes a null-terminated UTF-8 string into out_buffer describing
 * the active graphics backend (e.g. "Metal", "OpenGL ES", "D3D11").
 *
 * out_buffer and buffer_size must be non-null / > 0.
 * If the buffer is too small the string is truncated.
 * Returns ENGINE_RESULT_INVALID_STATE if the runtime is not active.
 */
ENGINE_API_EXPORT engine_result_t engine_get_renderer_info(
    engine_handle_t handle, char* out_buffer, uint32_t buffer_size);

/*
 * Gets runtime memory/cache statistics snapshot.
 * out_stats->struct_size must be initialized by caller.
 */
ENGINE_API_EXPORT engine_result_t engine_get_memory_stats(
    engine_handle_t handle, engine_memory_stats_t* out_stats);

/* Gets a bounded JSON object with plugin load, fallback, call, and missing-member stats. */
ENGINE_API_EXPORT engine_result_t engine_get_plugin_debug_info(
    engine_handle_t handle, char* out_buffer, uint32_t buffer_size,
    uint32_t* out_bytes_written);

/* Configures the bounded structured diagnostic queue for this handle. */
ENGINE_API_EXPORT engine_result_t engine_set_diagnostic_config(
    engine_handle_t handle, const engine_diagnostic_config_t* config);

/* Inserts a cross-layer marker and returns its event sequence. */
ENGINE_API_EXPORT engine_result_t engine_mark_diagnostic_event(
    engine_handle_t handle, const char* label_utf8, uint64_t* out_sequence);

/* Drains newline-delimited JSON diagnostic events into caller storage. */
ENGINE_API_EXPORT engine_result_t engine_drain_diagnostic_events(
    engine_handle_t handle, char* out_buffer, uint32_t buffer_size,
    uint32_t* out_bytes_written);

/*
 * Returns last error message as UTF-8 null-terminated string.
 * The returned pointer remains valid until next API call on the same handle.
 * Returns empty string when no error is recorded.
 */
ENGINE_API_EXPORT const char* engine_get_last_error(engine_handle_t handle);

#if defined(__cplusplus)
}  /* extern "C" */
#endif

#endif  /* KRKR2_ENGINE_API_H_ */
