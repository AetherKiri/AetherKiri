#ifndef AETHERKIRI_LEGACY_ENGINE_API_H_
#define AETHERKIRI_LEGACY_ENGINE_API_H_

#include "engine_api.h"

extern "C" {

engine_result_t engine_legacy_get_runtime_api_version(uint32_t* out_api_version);
engine_result_t engine_legacy_create(const engine_create_desc_t* desc,
                                     engine_handle_t* out_handle);
engine_result_t engine_legacy_destroy(engine_handle_t handle);
engine_result_t engine_legacy_open_game(engine_handle_t handle,
                                        const char* game_root_path_utf8,
                                        const char* startup_script_utf8);
engine_result_t engine_legacy_open_game_async(engine_handle_t handle,
                                              const char* game_root_path_utf8,
                                              const char* startup_script_utf8);
engine_result_t engine_legacy_get_startup_state(engine_handle_t handle,
                                                uint32_t* out_state);
engine_result_t engine_legacy_drain_startup_logs(
    engine_handle_t handle, char* out_buffer, uint32_t buffer_size,
    uint32_t* out_bytes_written);
engine_result_t engine_legacy_tick(engine_handle_t handle, uint32_t delta_ms);
engine_result_t engine_legacy_pause(engine_handle_t handle);
engine_result_t engine_legacy_resume(engine_handle_t handle);
engine_result_t engine_legacy_set_option(engine_handle_t handle,
                                         const engine_option_t* option);
engine_result_t engine_legacy_set_surface_size(engine_handle_t handle,
                                               uint32_t width,
                                               uint32_t height);
engine_result_t engine_legacy_get_frame_desc(
    engine_handle_t handle, engine_frame_desc_t* out_frame_desc);
engine_result_t engine_legacy_read_frame_rgba(engine_handle_t handle,
                                              void* out_pixels,
                                              size_t out_pixels_size);
engine_result_t engine_legacy_media_open(engine_handle_t engine,
                                         const char* path_utf8,
                                         engine_media_handle_t* out_media);
engine_result_t engine_legacy_media_destroy(engine_media_handle_t media);
engine_result_t engine_legacy_media_play(engine_media_handle_t media);
engine_result_t engine_legacy_media_pause(engine_media_handle_t media);
engine_result_t engine_legacy_media_seek(engine_media_handle_t media,
                                         int64_t position_ms);
engine_result_t engine_legacy_media_set_rate(engine_media_handle_t media,
                                             double playback_rate);
engine_result_t engine_legacy_media_get_state(
    engine_media_handle_t media, engine_media_state_t* out_state);
engine_result_t engine_legacy_media_get_subtitle_tracks_json(
    engine_media_handle_t media, char* out_buffer, uint32_t buffer_size,
    uint32_t* out_bytes_written);
engine_result_t engine_legacy_media_extract_subtitle(
    engine_media_handle_t media, int32_t stream_index,
    const char* output_path_utf8);
engine_result_t engine_legacy_media_read_frame_rgba(
    engine_media_handle_t media, void* out_pixels, size_t out_pixels_size,
    engine_frame_desc_t* out_frame_desc);
#if defined(ENGINE_API_USE_KRKR2_RUNTIME)
engine_result_t engine_legacy_get_godot_native_frame_texture(
    engine_handle_t handle, uint64_t* out_texture_id, uint32_t* out_width,
    uint32_t* out_height, uint64_t* out_frame_serial);
#endif
engine_result_t engine_legacy_get_host_native_window(engine_handle_t handle,
                                                     void** out_window_handle);
engine_result_t engine_legacy_get_host_native_view(engine_handle_t handle,
                                                   void** out_view_handle);
engine_result_t engine_legacy_send_input(engine_handle_t handle,
                                         const engine_input_event_t* event);
engine_result_t engine_legacy_get_text_input_state(
    engine_handle_t handle, engine_text_input_state_t* out_state);
engine_result_t engine_legacy_copy_text_input_text(
    engine_handle_t handle, char* out_buffer, uint32_t buffer_size,
    uint32_t* out_bytes_written);
engine_result_t engine_legacy_get_main_menu_json(
    engine_handle_t handle, char* out_buffer, uint32_t buffer_size,
    uint32_t* out_bytes_written);
engine_result_t engine_legacy_activate_menu_item(engine_handle_t handle,
                                                 const char* item_path_utf8);
engine_result_t engine_legacy_set_render_target_iosurface(
    engine_handle_t handle, uint32_t iosurface_id, uint32_t width,
    uint32_t height);
engine_result_t engine_legacy_set_render_target_surface(
    engine_handle_t handle, void* native_window, uint32_t width,
    uint32_t height);
engine_result_t engine_legacy_get_frame_rendered_flag(engine_handle_t handle,
                                                      uint32_t* out_rendered);
engine_result_t engine_legacy_get_renderer_info(engine_handle_t handle,
                                                char* out_buffer,
                                                uint32_t buffer_size);
engine_result_t engine_legacy_get_memory_stats(
    engine_handle_t handle, engine_memory_stats_t* out_stats);
engine_result_t engine_legacy_get_plugin_debug_info(
    engine_handle_t handle, char* out_buffer, uint32_t buffer_size,
    uint32_t* out_bytes_written);
engine_result_t engine_legacy_set_diagnostic_config(
    engine_handle_t handle, const engine_diagnostic_config_t* config);
engine_result_t engine_legacy_mark_diagnostic_event(engine_handle_t handle,
                                                    const char* label_utf8,
                                                    uint64_t* out_sequence);
engine_result_t engine_legacy_drain_diagnostic_events(
    engine_handle_t handle, char* out_buffer, uint32_t buffer_size,
    uint32_t* out_bytes_written);
const char* engine_legacy_get_last_error(engine_handle_t handle);

}  // extern "C"

#endif  /* AETHERKIRI_LEGACY_ENGINE_API_H_ */
