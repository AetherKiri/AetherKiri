#ifndef AETHERKIRI_ENGINE_LEGACY_SERVICES_H_
#define AETHERKIRI_ENGINE_LEGACY_SERVICES_H_

#include "engine_api.h"

#include <stddef.h>
#include <stdint.h>

/* Private ABI between the engine_api dispatch layer and the KiriKiri runtime
 * glue (bridge/krkr2_runtime). The glue fills one table and the host installs
 * it through engine_install_legacy_services() before the first engine_create
 * call; engine_api itself never links the KiriKiri runtime. The table mirrors
 * the engine_legacy_* symbol surface that the glue used to export directly,
 * plus the host services and the private provider registration entry points
 * that the dispatch layer calls unconditionally.
 *
 * The built-in "kirikiri" registry entry stays enumerable and probeable even
 * without an installed table; creating a KiriKiri engine then returns
 * ENGINE_RESULT_NOT_SUPPORTED. */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct engine_legacy_services_v1_t {
  uint32_t struct_size;

  engine_result_t (*get_runtime_api_version)(uint32_t* out_api_version);
  engine_result_t (*create)(const engine_create_desc_t* desc,
                            engine_handle_t* out_handle);
  engine_result_t (*destroy)(engine_handle_t handle);
  engine_result_t (*open_game)(engine_handle_t handle,
                               const char* game_root_path_utf8,
                               const char* startup_script_utf8);
  engine_result_t (*open_game_async)(engine_handle_t handle,
                                     const char* game_root_path_utf8,
                                     const char* startup_script_utf8);
  engine_result_t (*get_startup_state)(engine_handle_t handle,
                                       uint32_t* out_state);
  engine_result_t (*drain_startup_logs)(engine_handle_t handle,
                                        char* out_buffer,
                                        uint32_t buffer_size,
                                        uint32_t* out_bytes_written);
  engine_result_t (*tick)(engine_handle_t handle, uint32_t delta_ms);
  engine_result_t (*pause)(engine_handle_t handle);
  engine_result_t (*resume)(engine_handle_t handle);
  engine_result_t (*set_option)(engine_handle_t handle,
                                const engine_option_t* option);
  engine_result_t (*set_surface_size)(engine_handle_t handle, uint32_t width,
                                      uint32_t height);
  engine_result_t (*get_frame_desc)(engine_handle_t handle,
                                    engine_frame_desc_t* out_frame_desc);
  engine_result_t (*read_frame_rgba)(engine_handle_t handle, void* out_pixels,
                                     size_t out_pixels_size);
  engine_result_t (*media_open)(engine_handle_t engine, const char* path_utf8,
                                engine_media_handle_t* out_media);
  engine_result_t (*media_destroy)(engine_media_handle_t media);
  engine_result_t (*media_play)(engine_media_handle_t media);
  engine_result_t (*media_pause)(engine_media_handle_t media);
  engine_result_t (*media_seek)(engine_media_handle_t media,
                                int64_t position_ms);
  engine_result_t (*media_set_rate)(engine_media_handle_t media,
                                    double playback_rate);
  engine_result_t (*media_get_state)(engine_media_handle_t media,
                                     engine_media_state_t* out_state);
  engine_result_t (*media_get_subtitle_tracks_json)(
      engine_media_handle_t media, char* out_buffer, uint32_t buffer_size,
      uint32_t* out_bytes_written);
  engine_result_t (*media_extract_subtitle)(engine_media_handle_t media,
                                            int32_t stream_index,
                                            const char* output_path_utf8);
  engine_result_t (*media_read_frame_rgba)(engine_media_handle_t media,
                                           void* out_pixels,
                                           size_t out_pixels_size,
                                           engine_frame_desc_t* out_frame_desc);
  engine_result_t (*get_godot_native_frame_texture)(
      engine_handle_t handle, uint64_t* out_texture_id, uint32_t* out_width,
      uint32_t* out_height, uint64_t* out_frame_serial);
  engine_result_t (*get_host_native_window)(engine_handle_t handle,
                                            void** out_window_handle);
  engine_result_t (*get_host_native_view)(engine_handle_t handle,
                                          void** out_view_handle);
  engine_result_t (*send_input)(engine_handle_t handle,
                                const engine_input_event_t* event);
  engine_result_t (*get_text_input_state)(engine_handle_t handle,
                                          engine_text_input_state_t* out_state);
  engine_result_t (*copy_text_input_text)(engine_handle_t handle,
                                          char* out_buffer,
                                          uint32_t buffer_size,
                                          uint32_t* out_bytes_written);
  engine_result_t (*get_main_menu_json)(engine_handle_t handle,
                                        char* out_buffer,
                                        uint32_t buffer_size,
                                        uint32_t* out_bytes_written);
  engine_result_t (*activate_menu_item)(engine_handle_t handle,
                                        const char* item_path_utf8);
  engine_result_t (*set_render_target_iosurface)(engine_handle_t handle,
                                                 uint32_t iosurface_id,
                                                 uint32_t width,
                                                 uint32_t height);
  engine_result_t (*set_render_target_surface)(engine_handle_t handle,
                                               void* native_window,
                                               uint32_t width, uint32_t height);
  engine_result_t (*get_frame_rendered_flag)(engine_handle_t handle,
                                             uint32_t* out_rendered);
  engine_result_t (*get_renderer_info)(engine_handle_t handle,
                                       char* out_buffer,
                                       uint32_t buffer_size);
  engine_result_t (*get_memory_stats)(engine_handle_t handle,
                                      engine_memory_stats_t* out_stats);
  engine_result_t (*get_plugin_debug_info)(engine_handle_t handle,
                                           char* out_buffer,
                                           uint32_t buffer_size,
                                           uint32_t* out_bytes_written);
  engine_result_t (*set_diagnostic_config)(
      engine_handle_t handle, const engine_diagnostic_config_t* config);
  engine_result_t (*mark_diagnostic_event)(engine_handle_t handle,
                                           const char* label_utf8,
                                           uint64_t* out_sequence);
  engine_result_t (*drain_diagnostic_events)(engine_handle_t handle,
                                             char* out_buffer,
                                             uint32_t buffer_size,
                                             uint32_t* out_bytes_written);
  const char* (*get_last_error)(engine_handle_t handle);

  /* Host services shared by every backend, provider runtimes included. They
   * are optional in the table only in the sense that hosts without the
   * KiriKiri glue never install one: when a table is present these members
   * must be filled. */
  bool (*activate_audio_session_for_host)(void);
  void (*drain_texture_recycle)(void);

  /* Registers the private runtime providers compiled into the glue
   * (CatSystem2 / Artemis / WA2). May be null when the glue carries none.
   * Idempotent, called on every engine_create exactly like the direct
   * AetherInternalRegister*Runtime() calls it replaces. */
  void (*register_private_runtimes)(void);
} engine_legacy_services_v1_t;

#define ENGINE_LEGACY_SERVICES_V1_FULL_SIZE \
  ((uint32_t)sizeof(engine_legacy_services_v1_t))
/* Everything through register_private_runtimes must be present in a v1
 * table; later members may grow with a larger struct_size. */
#define ENGINE_LEGACY_SERVICES_V1_REQUIRED_SIZE                         \
  ((uint32_t)(offsetof(engine_legacy_services_v1_t,                     \
                       register_private_runtimes) +                     \
              sizeof(((engine_legacy_services_v1_t*)0)                  \
                         ->register_private_runtimes)))

/* Installs the KiriKiri legacy services table. Must run before the first
 * engine_create call. Installing the same table again is idempotent; a
 * different table is rejected with ENGINE_RESULT_INVALID_STATE so two runtimes
 * cannot silently fight over the built-in "kirikiri" entry. */
engine_result_t engine_install_legacy_services(
    const engine_legacy_services_v1_t* services);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* AETHERKIRI_ENGINE_LEGACY_SERVICES_H_ */
