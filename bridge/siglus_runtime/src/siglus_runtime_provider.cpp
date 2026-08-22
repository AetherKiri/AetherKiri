#include "siglus_runtime.h"

#include "engine_runtime_provider.h"
#include "siglus_ffi.h"

#include <cstring>
#include <filesystem>
#include <string>

namespace aetherkiri::siglus {
    namespace {
        constexpr const char *kRuntimeId = "siglus";
        constexpr const char *kDisplayName = "SiglusEngine (siglus_rs)";

        // Phase 2 stub: the Rust host is not wired yet. Every runtime
        // operation reports NOT_SUPPORTED so the dispatcher can surface a
        // clear error instead of misbehaving silently.
        const char *const kStubError =
            "siglus_rs backend is not wired yet (bridge phase 3 pending)";

        int32_t Probe(void *, const char *game_root_path) {
            if(game_root_path == nullptr || game_root_path[0] == '\0') {
                return 0;
            }
            try {
                const std::filesystem::path root(game_root_path);
                std::error_code ec;
                if(!std::filesystem::is_directory(root, ec)) {
                    return 0;
                }
                // SiglusEngine games are identified by Gameexe.ini at the
                // game root (both spellings cover case-sensitive hosts).
                for(const char *name : {"Gameexe.ini", "gameexe.ini"}) {
                    if(std::filesystem::exists(root / name, ec)) {
                        return 90;
                    }
                }
                return 0;
            } catch(...) {
                return 0;
            }
        }

        engine_runtime_provider_v1_t MakeProvider() {
            engine_runtime_provider_v1_t provider{};
            provider.struct_size = sizeof(provider);
            provider.api_version = ENGINE_RUNTIME_PROVIDER_API_VERSION;
            provider.runtime_id_utf8 = kRuntimeId;
            provider.display_name_utf8 = kDisplayName;
            provider.priority = 0;
            provider.provider_user_data = nullptr;

            provider.probe = Probe;

            // The stub never hands out a runtime handle, so every instance
            // method can only observe a null handle and fail cleanly.
            provider.create =
                [](void *, const engine_runtime_host_v1_t *,
                   const engine_create_desc_t *, void **out_runtime) {
                    if(out_runtime != nullptr) {
                        *out_runtime = nullptr;
                    }
                    return ENGINE_RESULT_NOT_SUPPORTED;
                };
            provider.destroy = [](void *) {};
            provider.open_game =
                [](void *, const char *, const char *) { return ENGINE_RESULT_NOT_SUPPORTED; };
            provider.tick =
                [](void *, uint32_t) { return ENGINE_RESULT_NOT_SUPPORTED; };
            provider.pause = [](void *) { return ENGINE_RESULT_NOT_SUPPORTED; };
            provider.resume = [](void *) { return ENGINE_RESULT_NOT_SUPPORTED; };
            provider.set_option =
                [](void *, const engine_option_t *) { return ENGINE_RESULT_NOT_SUPPORTED; };
            provider.set_surface_size =
                [](void *, uint32_t, uint32_t) { return ENGINE_RESULT_NOT_SUPPORTED; };
            provider.get_frame_desc =
                [](void *, engine_frame_desc_t *) { return ENGINE_RESULT_NOT_SUPPORTED; };
            provider.read_frame_rgba =
                [](void *, void *, size_t) { return ENGINE_RESULT_NOT_SUPPORTED; };
            provider.get_godot_native_frame_texture =
                [](void *, uint64_t *, uint32_t *, uint32_t *, uint64_t *) {
                    return ENGINE_RESULT_NOT_SUPPORTED;
                };
            provider.get_host_native_window =
                [](void *, void **) { return ENGINE_RESULT_NOT_SUPPORTED; };
            provider.get_host_native_view =
                [](void *, void **) { return ENGINE_RESULT_NOT_SUPPORTED; };
            provider.send_input =
                [](void *, const engine_input_event_t *) { return ENGINE_RESULT_NOT_SUPPORTED; };
            provider.get_main_menu_json =
                [](void *, char *, uint32_t, uint32_t *) { return ENGINE_RESULT_NOT_SUPPORTED; };
            provider.activate_menu_item =
                [](void *, const char *) { return ENGINE_RESULT_NOT_SUPPORTED; };
            provider.set_render_target_iosurface =
                [](void *, uint32_t, uint32_t, uint32_t) { return ENGINE_RESULT_NOT_SUPPORTED; };
            provider.set_render_target_surface =
                [](void *, void *, uint32_t, uint32_t) { return ENGINE_RESULT_NOT_SUPPORTED; };
            provider.get_frame_rendered_flag =
                [](void *, uint32_t *) { return ENGINE_RESULT_NOT_SUPPORTED; };
            provider.get_renderer_info =
                [](void *, char *, uint32_t) { return ENGINE_RESULT_NOT_SUPPORTED; };
            provider.get_memory_stats =
                [](void *, engine_memory_stats_t *) { return ENGINE_RESULT_NOT_SUPPORTED; };
            provider.get_plugin_debug_info =
                [](void *, char *, uint32_t, uint32_t *) { return ENGINE_RESULT_NOT_SUPPORTED; };
            provider.get_last_error = [](void *) { return kStubError; };
            provider.submit_platform_response =
                [](void *, const char *, const char *) { return ENGINE_RESULT_NOT_SUPPORTED; };
            provider.get_text_input_state =
                [](void *, uint32_t *) { return ENGINE_RESULT_NOT_SUPPORTED; };
            provider.get_godot_presentation_state =
                [](void *, uint32_t *) { return ENGINE_RESULT_NOT_SUPPORTED; };
            return provider;
        }

        engine_result_t RegisterOnce() {
            static engine_runtime_provider_v1_t provider = MakeProvider();
            return engine_register_runtime_provider(&provider);
        }
    }  // namespace

    void RegisterRuntimeProvider() {
        // Registration failures must not crash the host; the runtime simply
        // stays unavailable and probe() reports no match.
        (void)RegisterOnce();
    }

}  // namespace aetherkiri::siglus
