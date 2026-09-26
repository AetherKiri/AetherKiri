/* Installs the KiriKiri legacy services table into the engine_api dispatch
 * layer. Since the Phase 2d link flip the runtime glue is linked by the Godot
 * extension host instead of engine_api itself, so the engine_legacy_* surface
 * travels through one installable table (see
 * bridge/engine_api/src/engine_legacy_services.h). */

#include "legacy_engine_api.h"
#include "engine_legacy_services.h"

#if defined(AETHERKIRI_INTERNAL_CATSYSTEM2)
extern "C" void AetherInternalRegisterCatSystem2Runtime(void);
#endif
#if defined(AETHERKIRI_INTERNAL_ARTEMIS) && \
    defined(AETHERKIRI_ENABLE_ARTEMIS_RUNTIME)
extern "C" void AetherInternalRegisterArtemisRuntime(void);
#endif
#if defined(AETHERKIRI_INTERNAL_WA2) && defined(AETHERKIRI_ENABLE_WA2_RUNTIME)
extern "C" void AetherInternalRegisterWa2Runtime(void);
#endif

namespace {

void RegisterPrivateRuntimes() {
#if defined(AETHERKIRI_INTERNAL_CATSYSTEM2)
  AetherInternalRegisterCatSystem2Runtime();
#endif
#if defined(AETHERKIRI_INTERNAL_ARTEMIS) && \
    defined(AETHERKIRI_ENABLE_ARTEMIS_RUNTIME)
  AetherInternalRegisterArtemisRuntime();
#endif
#if defined(AETHERKIRI_INTERNAL_WA2) && defined(AETHERKIRI_ENABLE_WA2_RUNTIME)
  AetherInternalRegisterWa2Runtime();
#endif
}

const engine_legacy_services_v1_t kKrkr2LegacyServices = {
    sizeof(engine_legacy_services_v1_t),
    &engine_legacy_get_runtime_api_version,
    &engine_legacy_create,
    &engine_legacy_destroy,
    &engine_legacy_open_game,
    &engine_legacy_open_game_async,
    &engine_legacy_get_startup_state,
    &engine_legacy_drain_startup_logs,
    &engine_legacy_tick,
    &engine_legacy_pause,
    &engine_legacy_resume,
    &engine_legacy_set_option,
    &engine_legacy_set_surface_size,
    &engine_legacy_get_frame_desc,
    &engine_legacy_read_frame_rgba,
    &engine_legacy_media_open,
    &engine_legacy_media_destroy,
    &engine_legacy_media_play,
    &engine_legacy_media_pause,
    &engine_legacy_media_seek,
    &engine_legacy_media_set_rate,
    &engine_legacy_media_get_state,
    &engine_legacy_media_get_subtitle_tracks_json,
    &engine_legacy_media_extract_subtitle,
    &engine_legacy_media_read_frame_rgba,
    &engine_legacy_get_godot_native_frame_texture,
    &engine_legacy_get_host_native_window,
    &engine_legacy_get_host_native_view,
    &engine_legacy_send_input,
    &engine_legacy_get_text_input_state,
    &engine_legacy_copy_text_input_text,
    &engine_legacy_get_main_menu_json,
    &engine_legacy_activate_menu_item,
    &engine_legacy_set_render_target_iosurface,
    &engine_legacy_set_render_target_surface,
    &engine_legacy_get_frame_rendered_flag,
    &engine_legacy_get_renderer_info,
    &engine_legacy_get_memory_stats,
    &engine_legacy_get_plugin_debug_info,
    &engine_legacy_set_diagnostic_config,
    &engine_legacy_mark_diagnostic_event,
    &engine_legacy_drain_diagnostic_events,
    &engine_legacy_get_last_error,
    &engine_legacy_activate_audio_session_for_host,
    &engine_legacy_drain_texture_recycle,
    &RegisterPrivateRuntimes,
};

}  // namespace

extern "C" engine_result_t aether_krkr2_install_legacy_services(void) {
  return engine_install_legacy_services(&kKrkr2LegacyServices);
}
