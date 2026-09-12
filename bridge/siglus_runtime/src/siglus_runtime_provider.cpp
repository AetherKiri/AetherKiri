#include "siglus_runtime.h"

#include "engine_runtime_provider.h"
#include "siglus_audio_output.h"
#include "siglus_ffi.h"
#include "siglus_shared_frame.h"
#include "siglus_emote_backend.h"
#include "siglus_jpeg_decoder.h"

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#endif

#if defined(AETHERKIRI_SIGLUS_HOST_EMOTE)
extern "C" const siglus_emote_backend_v1_t* AetherInternalGetSiglusEmoteBackendV1();
#endif

namespace aetherkiri::siglus {
    namespace {
        constexpr const char *kRuntimeId = "siglus";
        constexpr const char *kDisplayName = "SiglusEngine (siglus_rs)";
        // Matches the OnscripterYuri provider so automatic probing resolves
        // ties deterministically; scores still decide per-directory matches.
        constexpr int32_t kProviderPriority = 90;
        constexpr uint32_t kDefaultSurfaceWidth = 1280;
        constexpr uint32_t kDefaultSurfaceHeight = 720;

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
                // SiglusEngine games are identified by their data files at
                // the game root: Gameexe.ini/.dat (the latter is the
                // encrypted form shipped by many trials/retail builds) and
                // Scene.pck (unique to Siglus).
                for(const char *name : {"Gameexe.ini", "gameexe.ini",
                                         "Gameexe.dat", "gameexe.dat",
                                         "Scene.pck", "scene.pck"}) {
                    if(std::filesystem::exists(root / name, ec)) {
                        return 90;
                    }
                }
                return 0;
            } catch(...) {
                return 0;
            }
        }

        std::string SafeLast(void *ak) {
            return ak != nullptr ? std::string(siglus_ak_last_error(ak)) : std::string();
        }

        engine_result_t Fail(std::string &error_sink, int32_t ffi_result,
                             void *ak, const char *context) {
            error_sink = context;
            const std::string detail = SafeLast(ak);
            if(!detail.empty()) {
                error_sink += ": ";
                error_sink += detail;
            }
            switch(ffi_result) {
                case SIGLUS_AK_INVALID_ARGUMENT:
                    return ENGINE_RESULT_INVALID_ARGUMENT;
                case SIGLUS_AK_INVALID_STATE:
                    return ENGINE_RESULT_INVALID_STATE;
                case SIGLUS_AK_NOT_SUPPORTED:
                    return ENGINE_RESULT_NOT_SUPPORTED;
                case SIGLUS_AK_IO_ERROR:
                    return ENGINE_RESULT_IO_ERROR;
                default:
                    return ENGINE_RESULT_INTERNAL_ERROR;
            }
        }

        engine_result_t OkOr(std::string &error_sink, int32_t ffi_result,
                             void *ak, const char *context) {
            if(ffi_result >= 0) {
                return ENGINE_RESULT_OK;
            }
            return Fail(error_sink, ffi_result, ak, context);
        }

        struct ProviderRuntime {
            const engine_runtime_host_v1_t host{};
            void *ak = nullptr;
            bool opened = false;
            bool paused = false;
            bool terminated = false;
            SharedFrame shared_frame;
            bool shared_current = false;
            int32_t ime_start = -1;
            int32_t ime_length = 0;
            uint32_t pending_width = kDefaultSurfaceWidth;
            uint32_t pending_height = kDefaultSurfaceHeight;

            // Latest decoded frame served to the host.
            std::vector<uint8_t> frame_rgba;
            uint32_t frame_width = 0;
            uint32_t frame_height = 0;
            uint32_t frame_stride = 0;
            uint64_t frame_serial = 0;
            uint64_t delivered_frame_serial = 0;
            std::string error;
            std::string scratch_error;

            explicit ProviderRuntime(const engine_runtime_host_v1_t *host_value) :
                host(host_value != nullptr ? *host_value
                                           : engine_runtime_host_v1_t{}) {}
        };

        ProviderRuntime *Cast(void *runtime) {
            return static_cast<ProviderRuntime *>(runtime);
        }

        engine_result_t CopyString(const std::string &value, char *output,
                                   uint32_t output_size,
                                   uint32_t *bytes_written = nullptr) {
            if(output == nullptr || output_size == 0) {
                return ENGINE_RESULT_INVALID_ARGUMENT;
            }
            const size_t copy_size =
                std::min<size_t>(value.size(), output_size - 1u);
            std::memcpy(output, value.data(), copy_size);
            output[copy_size] = '\0';
            if(bytes_written != nullptr) {
                *bytes_written = static_cast<uint32_t>(copy_size);
            }
            return copy_size == value.size() ? ENGINE_RESULT_OK
                                             : ENGINE_RESULT_INVALID_ARGUMENT;
        }

        void LogHost(ProviderRuntime *runtime, uint32_t level,
                     const char *message) {
            if(runtime != nullptr && runtime->host.log != nullptr) {
                runtime->host.log(runtime->host.user_data, level,
                                  kRuntimeId, message);
            }
        }

        void PlatformRequest(void *opaque, const char *operation, const char *argument) {
            auto *runtime = Cast(opaque);
            if(runtime != nullptr && runtime->host.platform_request != nullptr) {
                runtime->host.platform_request(runtime->host.user_data, operation, argument);
            }
        }

        // Steps one simulation tick and refreshes the frame cache.
        engine_result_t Tick(void *opaque, uint32_t delta_ms) {
            ProviderRuntime *runtime = Cast(opaque);
            if(runtime == nullptr || runtime->ak == nullptr || !runtime->opened) {
                return ENGINE_RESULT_INVALID_STATE;
            }
            if(runtime->terminated) {
                runtime->error = "runtime has been terminated";
                return ENGINE_RESULT_INVALID_STATE;
            }
            if(runtime->paused) return ENGINE_RESULT_OK;
            uint32_t target_width = 0, target_height = 0, target_stride = 0;
            bool shared = siglus_ak_get_frame_desc(runtime->ak, &target_width,
                &target_height, &target_stride) >= 0 &&
                runtime->shared_frame.begin(runtime->ak, target_width, target_height);
            const int32_t step = siglus_ak_step(runtime->ak, delta_ms);
            if(step < 0) {
                const std::string detail = SafeLast(runtime->ak);
                LogHost(runtime, ENGINE_RUNTIME_LOG_ERROR,
                        ("siglus_ak_step failed: " +
                         (detail.empty() ? std::string("(no detail)") : detail))
                            .c_str());
                return Fail(runtime->error, step, runtime->ak, "siglus_ak_step");
            }
            if(step == SIGLUS_AK_EXIT_REQUESTED) {
                LogHost(runtime, ENGINE_RUNTIME_LOG_INFO,
                        "engine requested exit");
                runtime->terminated = true;
                runtime->error = "runtime requested termination";
                audio::StopOutput();
                return ENGINE_RESULT_INVALID_STATE;
            }

            uint32_t width = 0;
            uint32_t height = 0;
            uint32_t stride = 0;
            const int32_t desc = siglus_ak_get_frame_desc(
                runtime->ak, &width, &height, &stride);
            if(desc < 0) {
                return Fail(runtime->error, desc, runtime->ak,
                            "siglus_ak_get_frame_desc");
            }
            runtime->frame_width = width;
            runtime->frame_height = height;
            runtime->frame_stride = stride;
            runtime->frame_serial += 1;
            runtime->shared_current = shared && runtime->shared_frame.publish(runtime->frame_serial);
            if(runtime->shared_current) {
                runtime->frame_rgba.clear();
                runtime->error.clear();
                return ENGINE_RESULT_OK;
            }
            // Failed imports/publication preserve the existing CPU fallback.
            const size_t needed =
                static_cast<size_t>(width) * height * 4u;
            runtime->frame_rgba.resize(needed);
            const int32_t read = siglus_ak_read_frame_rgba(
                runtime->ak, runtime->frame_rgba.data(), needed);
            if(read < 0) {
                runtime->frame_rgba.clear();
                return Fail(runtime->error, read, runtime->ak,
                            "siglus_ak_read_frame_rgba");
            }
            runtime->error.clear();
            return ENGINE_RESULT_OK;
        }

        engine_result_t Create(void *, const engine_runtime_host_v1_t *host,
                               const engine_create_desc_t *desc,
                               void **out_runtime) {
            if(host == nullptr || desc == nullptr || out_runtime == nullptr) {
                return ENGINE_RESULT_INVALID_ARGUMENT;
            }
            *out_runtime = nullptr;
            if(host->api_version != ENGINE_RUNTIME_PROVIDER_API_VERSION) {
                return ENGINE_RESULT_NOT_SUPPORTED;
            }
            auto runtime = std::unique_ptr<ProviderRuntime>(
                new(std::nothrow) ProviderRuntime(host));
            if(!runtime) {
                return ENGINE_RESULT_INTERNAL_ERROR;
            }
            runtime->ak = siglus_ak_create(1.0f);
            if(runtime->ak == nullptr) {
                runtime->error = "siglus_ak_create failed";
                return ENGINE_RESULT_INTERNAL_ERROR;
            }
            *out_runtime = runtime.release();
            return ENGINE_RESULT_OK;
        }

        void Destroy(void *runtime) {
            ProviderRuntime *instance = Cast(runtime);
            if(instance == nullptr) {
                return;
            }
            audio::StopOutput();
            if(instance->ak != nullptr) {
                instance->shared_frame.reset(instance->ak);
                siglus_ak_destroy(instance->ak);
                instance->ak = nullptr;
            }
            delete instance;
        }

        engine_result_t OpenGame(void *runtime,
                                 const char *game_root_path_utf8,
                                 const char *) {
            ProviderRuntime *instance = Cast(runtime);
            if(instance == nullptr || instance->ak == nullptr ||
               game_root_path_utf8 == nullptr ||
               game_root_path_utf8[0] == '\0') {
                return ENGINE_RESULT_INVALID_ARGUMENT;
            }
            if(instance->opened) {
                instance->shared_frame.reset(instance->ak);
                siglus_ak_close(instance->ak);
                instance->opened = false;
            }
            // The create descriptor carries no surface geometry; start at the
            // engine's default and follow set_surface_size calls.
            const int32_t result = siglus_ak_open(
                instance->ak, game_root_path_utf8,
                instance->pending_width, instance->pending_height);
            if(result < 0) {
                const std::string detail = SafeLast(instance->ak);
                LogHost(instance, ENGINE_RUNTIME_LOG_ERROR,
                        (std::string("siglus_ak_open failed for ") +
                         game_root_path_utf8 + ": " +
                         (detail.empty() ? std::string("(no detail)") : detail))
                            .c_str());
                return Fail(instance->error, result, instance->ak,
                            "siglus_ak_open");
            }
            instance->opened = true;
            instance->paused = false;
            instance->terminated = false;
            instance->shared_current = false;
            instance->ime_start = -1;
            instance->ime_length = 0;
            siglus_ak_set_platform_request_callback(instance->ak, PlatformRequest, instance);
            audio::SetPaused(false);
            instance->error.clear();
            LogHost(instance, ENGINE_RUNTIME_LOG_INFO, "siglus game opened");
            // Wire the kira mixer FIFO to an OpenAL streaming output; failure
            // only downgrades to silence, the game itself stays usable.
            std::string audio_error;
            if(!audio::StartOutput(audio_error)) {
                LogHost(instance, ENGINE_RUNTIME_LOG_WARNING,
                        ("siglus audio disabled: " + audio_error).c_str());
            }
            return ENGINE_RESULT_OK;
        }

        engine_result_t Pause(void *runtime) {
            auto *instance = Cast(runtime);
            if(instance == nullptr || !instance->opened) return ENGINE_RESULT_INVALID_STATE;
            const auto result = siglus_ak_set_paused(instance->ak, 1);
            if(result >= 0) { instance->paused = true; audio::SetPaused(true); }
            return OkOr(instance->error, result, instance->ak, "siglus pause");
        }

        engine_result_t Resume(void *runtime) {
            auto *instance = Cast(runtime);
            if(instance == nullptr || !instance->opened) return ENGINE_RESULT_INVALID_STATE;
            const auto result = siglus_ak_set_paused(instance->ak, 0);
            if(result >= 0) { instance->paused = false; audio::SetPaused(false); }
            return OkOr(instance->error, result, instance->ak, "siglus resume");
        }

        engine_result_t SetOption(void *runtime, const engine_option_t *option) {
            ProviderRuntime *instance = Cast(runtime);
            if(instance == nullptr || option == nullptr || option->key_utf8 == nullptr ||
               option->value_utf8 == nullptr) return ENGINE_RESULT_INVALID_ARGUMENT;
            const std::string key(option->key_utf8);
            if(key == "ime_preedit") {
                // The separate cursor option is sent first by the host.
                return OkOr(instance->error, siglus_ak_ime_preedit(instance->ak,
                    option->value_utf8, instance->ime_start, instance->ime_length),
                    instance->ak, "siglus IME preedit");
            }
            if(key == "ime_preedit_cursor") {
                int start = -1, length = 0;
                if(std::sscanf(option->value_utf8, "%d,%d", &start, &length) != 2)
                    return ENGINE_RESULT_INVALID_ARGUMENT;
                instance->ime_start = start;
                instance->ime_length = length;
                return ENGINE_RESULT_OK;
            }
            if(key == "ime_disabled") {
                return OkOr(instance->error, siglus_ak_ime_preedit(instance->ak, nullptr, -1, 0),
                    instance->ak, "siglus IME disabled");
            }
            return ENGINE_RESULT_NOT_SUPPORTED;
        }

        engine_result_t SetSurfaceSize(void *runtime, uint32_t width,
                                       uint32_t height) {
            ProviderRuntime *instance = Cast(runtime);
            if(instance == nullptr || instance->ak == nullptr ||
               width == 0 || height == 0) {
                return ENGINE_RESULT_INVALID_ARGUMENT;
            }
            instance->pending_width = width;
            instance->pending_height = height;
            if(!instance->opened) {
                return ENGINE_RESULT_OK;
            }
            return OkOr(instance->error, siglus_ak_resize(instance->ak, width, height),
                        instance->ak, "siglus_ak_resize");
        }

        engine_result_t GetFrameDesc(void *runtime,
                                     engine_frame_desc_t *out_frame_desc) {
            ProviderRuntime *instance = Cast(runtime);
            if(instance == nullptr || out_frame_desc == nullptr ||
               out_frame_desc->struct_size < sizeof(engine_frame_desc_t)) {
                return ENGINE_RESULT_INVALID_ARGUMENT;
            }
            const uint32_t struct_size = out_frame_desc->struct_size;
            std::memset(out_frame_desc, 0, sizeof(*out_frame_desc));
            out_frame_desc->struct_size = struct_size;
            out_frame_desc->width = instance->frame_width;
            out_frame_desc->height = instance->frame_height;
            out_frame_desc->stride_bytes = instance->frame_stride;
            out_frame_desc->pixel_format = ENGINE_PIXEL_FORMAT_RGBA8888;
            out_frame_desc->frame_serial = instance->frame_serial;
            return ENGINE_RESULT_OK;
        }

        engine_result_t ReadFrame(void *runtime, void *out_pixels,
                                  size_t out_pixels_size) {
            ProviderRuntime *instance = Cast(runtime);
            if(instance == nullptr || out_pixels == nullptr) {
                return ENGINE_RESULT_INVALID_ARGUMENT;
            }
            if(instance->shared_current) {
                const auto result = OkOr(instance->error, siglus_ak_read_frame_rgba(
                    instance->ak, static_cast<uint8_t *>(out_pixels), out_pixels_size), instance->ak, "siglus capture shared frame");
                if(result == ENGINE_RESULT_OK) instance->delivered_frame_serial = instance->frame_serial;
                return result;
            }
            const size_t frame_bytes = instance->frame_rgba.size();
            if(frame_bytes == 0) {
                instance->error = "no frame rendered yet";
                return ENGINE_RESULT_INVALID_STATE;
            }
            if(out_pixels_size < frame_bytes) {
                instance->error = "RGBA frame output buffer is too small";
                return ENGINE_RESULT_INVALID_ARGUMENT;
            }
            std::memcpy(out_pixels, instance->frame_rgba.data(), frame_bytes);
            instance->delivered_frame_serial = instance->frame_serial;
            instance->error.clear();
            return ENGINE_RESULT_OK;
        }

        engine_result_t SendInput(void *runtime,
                                  const engine_input_event_t *event) {
            ProviderRuntime *instance = Cast(runtime);
            if(instance == nullptr || instance->ak == nullptr || !instance->opened ||
               event == nullptr || event->struct_size < sizeof(engine_input_event_t)) {
                return ENGINE_RESULT_INVALID_ARGUMENT;
            }
            int32_t ffi_result = SIGLUS_AK_OK;
            if(event->type == ENGINE_INPUT_EVENT_POINTER_UP &&
               (event->modifiers & ENGINE_INPUT_MODIFIER_POINTER_CANCEL) != 0) {
                return OkOr(instance->error, siglus_ak_pointer_cancel(instance->ak),
                            instance->ak, "siglus pointer cancellation");
            }
            switch(event->type) {
                case ENGINE_INPUT_EVENT_POINTER_DOWN:
                case ENGINE_INPUT_EVENT_POINTER_UP:
                    ffi_result = siglus_ak_mouse_move(instance->ak, event->x, event->y);
                    if(ffi_result >= 0) {
                        ffi_result = siglus_ak_mouse_button(instance->ak, event->button,
                            event->type == ENGINE_INPUT_EVENT_POINTER_DOWN ? 1 : 0);
                    }
                    break;
                case ENGINE_INPUT_EVENT_POINTER_MOVE:
                    ffi_result = siglus_ak_mouse_move(instance->ak, event->x, event->y);
                    break;
                case ENGINE_INPUT_EVENT_POINTER_SCROLL:
                    ffi_result = siglus_ak_mouse_wheel(
                        instance->ak, static_cast<int32_t>(event->delta_y));
                    break;
                case ENGINE_INPUT_EVENT_KEY_DOWN:
                case ENGINE_INPUT_EVENT_KEY_UP:
                    // 0x200..0x21f is the embedded host's raw joypad domain;
                    // it cannot collide with the original 256 Windows VKs.
                    if(event->key_code >= 0x200 && event->key_code < 0x220) {
                        ffi_result = siglus_ak_joypad_button(instance->ak, event->key_code - 0x200,
                            event->type == ENGINE_INPUT_EVENT_KEY_DOWN ? 1 : 0);
                    } else {
                        ffi_result = siglus_ak_key(instance->ak, event->key_code,
                            event->type == ENGINE_INPUT_EVENT_KEY_DOWN ? 1 : 0);
                    }
                    break;
                case ENGINE_INPUT_EVENT_TEXT_INPUT: {
                    // Encode the single Unicode scalar value as UTF-8.
                    char utf8[5] = {0, 0, 0, 0, 0};
                    uint32_t cp = event->unicode_codepoint;
                    if(cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
                        return ENGINE_RESULT_INVALID_ARGUMENT;
                    }
                    size_t length = 0;
                    if(cp <= 0x7F) {
                        utf8[length++] = static_cast<char>(cp);
                    } else if(cp <= 0x7FF) {
                        utf8[length++] = static_cast<char>(0xC0 | (cp >> 6));
                        utf8[length++] = static_cast<char>(0x80 | (cp & 0x3F));
                    } else if(cp <= 0xFFFF) {
                        utf8[length++] = static_cast<char>(0xE0 | (cp >> 12));
                        utf8[length++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        utf8[length++] = static_cast<char>(0x80 | (cp & 0x3F));
                    } else {
                        utf8[length++] = static_cast<char>(0xF0 | (cp >> 18));
                        utf8[length++] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                        utf8[length++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        utf8[length++] = static_cast<char>(0x80 | (cp & 0x3F));
                    }
                    ffi_result = siglus_ak_text_input(instance->ak, utf8);
                    break;
                }
                case ENGINE_INPUT_EVENT_BACK:
                    ffi_result = siglus_ak_key(instance->ak, 0x1B, 1);
                    if(ffi_result >= 0) {
                        ffi_result = siglus_ak_key(instance->ak, 0x1B, 0);
                    }
                    break;
                default:
                    return ENGINE_RESULT_INVALID_ARGUMENT;
            }
            return OkOr(instance->error, ffi_result, instance->ak,
                        "siglus input dispatch");
        }

        engine_result_t GetFrameRenderedFlag(void *runtime,
                                             uint32_t *out_rendered) {
            ProviderRuntime *instance = Cast(runtime);
            if(instance == nullptr || out_rendered == nullptr) {
                return ENGINE_RESULT_INVALID_ARGUMENT;
            }
            *out_rendered =
                instance->frame_serial != instance->delivered_frame_serial
                    ? 1u
                    : 0u;
            return ENGINE_RESULT_OK;
        }

        engine_result_t GetRendererInfo(void *runtime, char *output,
                                        uint32_t output_size) {
            ProviderRuntime *instance = Cast(runtime);
            if(instance == nullptr) {
                return ENGINE_RESULT_INVALID_ARGUMENT;
            }
            return CopyString(
                std::string("runtime=siglus provider=engine_runtime_provider_v1 ") +
                (instance->shared_current ? "render=wgpu-iosurface-shared" : "render=wgpu-offscreen-cpu"),
                output, output_size);
        }

        engine_result_t GetMemoryStats(void *runtime,
                                       engine_memory_stats_t *output) {
            if(Cast(runtime) == nullptr || output == nullptr ||
               output->struct_size < sizeof(engine_memory_stats_t)) {
                return ENGINE_RESULT_INVALID_ARGUMENT;
            }
            const uint32_t struct_size = output->struct_size;
            std::memset(output, 0, sizeof(*output));
            output->struct_size = struct_size;
            auto *instance = Cast(runtime);
            uint64_t cpu = 0, gpu = 0, cpu_limit = 0, gpu_limit = 0;
            const auto result = siglus_ak_cache_stats(instance->ak, &cpu, &gpu, &cpu_limit, &gpu_limit);
            if(result < 0) return Fail(instance->error, result, instance->ak, "siglus cache stats");
            output->graphic_cache_bytes = cpu + gpu;
            output->graphic_cache_limit_bytes = cpu_limit + gpu_limit;
#if defined(__APPLE__)
            task_vm_info_data_t vm{};
            mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
            if(task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&vm), &count) == KERN_SUCCESS) {
                output->process_resident_bytes = vm.resident_size;
                output->process_physical_footprint_bytes = vm.phys_footprint;
                output->process_peak_physical_footprint_bytes = std::max<uint64_t>(vm.phys_footprint, vm.ledger_phys_footprint_peak);
                output->self_used_mb = static_cast<uint32_t>(vm.phys_footprint / (1024 * 1024));
            }
            uint64_t total = 0;
            size_t total_size = sizeof(total);
            if(sysctlbyname("hw.memsize", &total, &total_size, nullptr, 0) == 0)
                output->system_total_mb = static_cast<uint32_t>(total / (1024 * 1024));
            vm_statistics64_data_t stats{};
            count = HOST_VM_INFO64_COUNT;
            vm_size_t page_size = 0;
            const auto host = mach_host_self();
            if(host_page_size(host, &page_size) == KERN_SUCCESS &&
               host_statistics64(host, HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&stats), &count) == KERN_SUCCESS)
                output->system_free_mb = static_cast<uint32_t>((stats.free_count + stats.inactive_count) * page_size / (1024 * 1024));
            mach_port_deallocate(mach_task_self(), host);
#endif
            return ENGINE_RESULT_OK;
        }

        engine_result_t GetDebugInfo(void *runtime, char *output,
                                     uint32_t output_size,
                                     uint32_t *bytes_written) {
            ProviderRuntime *instance = Cast(runtime);
            if(instance == nullptr) {
                return ENGINE_RESULT_INVALID_ARGUMENT;
            }
            char details[4096]{};
            const auto result = siglus_ak_debug_info(instance->ak, details, sizeof(details));
            if(result < 0) return Fail(instance->error, result, instance->ak, "siglus debug info");
            return CopyString(std::string("runtime=siglus ") + details, output, output_size, bytes_written);
        }

        const char *GetLastError(void *runtime) {
            ProviderRuntime *instance = Cast(runtime);
            if(instance == nullptr) {
                return "Siglus runtime handle is null";
            }
            if(!instance->error.empty()) {
                return instance->error.c_str();
            }
            if(instance->ak != nullptr) {
                instance->scratch_error = SafeLast(instance->ak);
                if(!instance->scratch_error.empty()) {
                    return instance->scratch_error.c_str();
                }
            }
            return "";
        }

        engine_result_t GetTextInputDetails(void *runtime, engine_text_input_state_t *output) {
            auto *instance = Cast(runtime);
            if(instance == nullptr || output == nullptr || output->struct_size < sizeof(*output))
                return ENGINE_RESULT_INVALID_ARGUMENT;
            siglus_ak_text_input_state_t state{};
            const auto result = siglus_ak_get_text_input_state(instance->ak, &state);
            if(result < 0) return Fail(instance->error, result, instance->ak, "siglus text input state");
            const auto size = output->struct_size;
            *output = {};
            output->struct_size = size;
            output->ime_active = state.active;
            output->attention_point_valid = state.active;
            output->attention_x = state.x;
            output->attention_y = state.y;
            output->text_available = state.active;
            output->text_utf8_bytes = state.text_bytes;
            output->selection_start = state.selection_start;
            output->selection_end = state.selection_end;
            return ENGINE_RESULT_OK;
        }

        engine_runtime_provider_v1_t MakeProvider() {
            engine_runtime_provider_v1_t provider{};
            provider.struct_size = sizeof(provider);
            provider.api_version = ENGINE_RUNTIME_PROVIDER_API_VERSION;
            provider.runtime_id_utf8 = kRuntimeId;
            provider.display_name_utf8 = kDisplayName;
            provider.priority = kProviderPriority;
            provider.provider_user_data = nullptr;

            provider.probe = Probe;
            provider.create = Create;
            provider.destroy = Destroy;
            provider.open_game = OpenGame;
            provider.tick = Tick;
            provider.pause = Pause;
            provider.resume = Resume;
            provider.set_option = SetOption;
            provider.set_surface_size = SetSurfaceSize;
            provider.get_frame_desc = GetFrameDesc;
            provider.read_frame_rgba = ReadFrame;
            provider.get_godot_native_frame_texture =
                [](void *runtime, uint64_t *texture, uint32_t *width, uint32_t *height,
                   uint64_t *serial) {
                    auto *instance = Cast(runtime);
                    if(instance == nullptr || texture == nullptr || width == nullptr ||
                       height == nullptr || serial == nullptr) return ENGINE_RESULT_INVALID_ARGUMENT;
                    if(!instance->shared_current || !instance->shared_frame.get(texture, width, height, serial))
                        return ENGINE_RESULT_NOT_SUPPORTED;
                    instance->delivered_frame_serial = instance->frame_serial;
                    return ENGINE_RESULT_OK;
                };
            provider.get_host_native_window =
                [](void *, void **) { return ENGINE_RESULT_NOT_SUPPORTED; };
            provider.get_host_native_view =
                [](void *, void **) { return ENGINE_RESULT_NOT_SUPPORTED; };
            provider.send_input = SendInput;
            provider.get_main_menu_json =
                [](void *, char *, uint32_t, uint32_t *) {
                    return ENGINE_RESULT_NOT_SUPPORTED;
                };
            provider.activate_menu_item =
                [](void *, const char *) { return ENGINE_RESULT_NOT_SUPPORTED; };
            provider.set_render_target_iosurface =
                [](void *, uint32_t, uint32_t, uint32_t) {
                    return ENGINE_RESULT_NOT_SUPPORTED;
                };
            provider.set_render_target_surface =
                [](void *, void *, uint32_t, uint32_t) {
                    return ENGINE_RESULT_NOT_SUPPORTED;
                };
            provider.get_frame_rendered_flag = GetFrameRenderedFlag;
            provider.get_renderer_info = GetRendererInfo;
            provider.get_memory_stats = GetMemoryStats;
            provider.get_plugin_debug_info = GetDebugInfo;
            provider.get_last_error = GetLastError;
            provider.submit_platform_response =
                [](void *runtime, const char *operation, const char *argument) {
                    auto *instance = Cast(runtime);
                    if(instance == nullptr) return ENGINE_RESULT_INVALID_ARGUMENT;
                    return OkOr(instance->error,
                        siglus_ak_submit_platform_response(instance->ak, operation, argument),
                        instance->ak, "siglus platform response");
                };
            provider.get_text_input_state =
                [](void *runtime, uint32_t *out_state_flags) {
                    if(out_state_flags == nullptr) return ENGINE_RESULT_INVALID_ARGUMENT;
                    engine_text_input_state_t state{};
                    state.struct_size = sizeof(state);
                    const auto result = GetTextInputDetails(runtime, &state);
                    *out_state_flags = state.ime_active ? ENGINE_TEXT_INPUT_STATE_ACTIVE : 0;
                    return result;
                };
            provider.get_text_input_details = GetTextInputDetails;
            provider.copy_text_input_text = [](void *runtime, char *output, uint32_t size, uint32_t *written) {
                auto *instance = Cast(runtime);
                if(instance == nullptr) return ENGINE_RESULT_INVALID_ARGUMENT;
                return OkOr(instance->error, siglus_ak_copy_text_input_text(instance->ak, output, size, written),
                    instance->ak, "siglus copy input text");
            };
            provider.get_godot_presentation_state =
                [](void *, uint32_t *out_state_flags) {
                    if(out_state_flags != nullptr) {
                        *out_state_flags = 0;
                    }
                    return ENGINE_RESULT_OK;
                };
            return provider;
        }

        engine_result_t RegisterOnce() {
            static const int32_t jpeg_status = siglus_register_jpeg_decoder(DecodeJpeg);
            if(jpeg_status != SIGLUS_AK_OK) {
                std::fprintf(stderr, "[siglus] host JPEG decoder registration failed: %d\n", jpeg_status);
            }
#if defined(AETHERKIRI_SIGLUS_HOST_EMOTE)
            static const int32_t emote_status = siglus_ak_register_emote_backend(
                AetherInternalGetSiglusEmoteBackendV1());
            if(emote_status != SIGLUS_AK_OK) {
                std::fprintf(stderr, "[siglus] host E-mote backend registration failed: %d\n", emote_status);
            }
#endif
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
