#include "luca_runtime.h"

#include "engine_runtime_provider.h"
#include "luca_ffi.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#endif

namespace aetherkiri::luca {
    namespace {
        constexpr const char *kRuntimeId = "luca";
        constexpr const char *kDisplayName = "LucaSystem (AetherLuca)";
        // Matches the OnscripterYuri/siglus providers so automatic probing
        // resolves ties deterministically; per-directory scores still decide
        // between disjoint marker sets.
        constexpr int32_t kProviderPriority = 90;
        constexpr uint32_t kDefaultSurfaceWidth = 1280;
        constexpr uint32_t kDefaultSurfaceHeight = 720;

        uint32_t ReadU32(const uint8_t *bytes) {
            return static_cast<uint32_t>(bytes[0]) |
                   (static_cast<uint32_t>(bytes[1]) << 8) |
                   (static_cast<uint32_t>(bytes[2]) << 16) |
                   (static_cast<uint32_t>(bytes[3]) << 24);
        }

        // Cheap PAK header plausibility check (notes/001): block_size is 4 or
        // 2048, the index region fits in the file and contains the entry
        // table. Keeps random files named SCRIPT.PAK from probing as Luca.
        bool LooksLikePak(const std::filesystem::path &path) {
            std::ifstream file(path, std::ios::binary);
            uint8_t header[0x24]{};
            if(!file.read(reinterpret_cast<char *>(header), sizeof(header))) {
                return false;
            }
            const uint32_t data_start = ReadU32(header);
            const uint32_t count = ReadU32(header + 4);
            const uint32_t block = ReadU32(header + 12);
            if(block != 4 && block != 2048) {
                return false;
            }
            std::error_code ec;
            const auto size = std::filesystem::file_size(path, ec);
            if(ec || size < 0x24 || data_start > size || data_start < 0x30) {
                return false;
            }
            return count <= (data_start - 0x30u) / 8u;
        }

        bool HasPakArchive(const std::filesystem::path &dir) {
            std::error_code ec;
            if(!std::filesystem::is_directory(dir, ec)) {
                return false;
            }
            for(const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
                std::error_code entry_ec;
                if(!entry.is_regular_file(entry_ec)) {
                    continue;
                }
                auto name = entry.path().filename().string();
                if(name.size() < 4) {
                    continue;
                }
                // Archive names are uppercase in real games; compare the
                // extension case-insensitively like the engine's folded
                // lookups (notes/004 §1).
                std::string tail = name.substr(name.size() - 4);
                for(char &c : tail) {
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                if(tail == ".pak") {
                    return true;
                }
            }
            return false;
        }

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
                // LucaSystem games keep their data under files/: the script
                // archive plus an image PAK family (notes/001). The marker
                // set is disjoint from siglus (Gameexe.ini/.dat, Scene.pck)
                // and OnscripterYuri (0.txt etc.).
                const std::filesystem::path files = root / "files";
                if(!std::filesystem::is_directory(files, ec)) {
                    return 0;
                }
                if(!LooksLikePak(files / "SCRIPT.PAK")) {
                    return 0;
                }
                if(!HasPakArchive(files / "image")) {
                    return 0;
                }
                return 90;
            } catch(...) {
                return 0;
            }
        }

        std::string SafeLast(void *ak) {
            return ak != nullptr ? std::string(luca_ak_last_error(ak)) : std::string();
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
                case LUCA_AK_INVALID_ARGUMENT:
                    return ENGINE_RESULT_INVALID_ARGUMENT;
                case LUCA_AK_INVALID_STATE:
                    return ENGINE_RESULT_INVALID_STATE;
                case LUCA_AK_NOT_SUPPORTED:
                    return ENGINE_RESULT_NOT_SUPPORTED;
                case LUCA_AK_IO_ERROR:
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
            uint32_t pending_width = kDefaultSurfaceWidth;
            uint32_t pending_height = kDefaultSurfaceHeight;
            uint32_t native_width = 0;
            uint32_t native_height = 0;

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

        // Steps one simulation tick and refreshes the CPU frame cache.
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
            const int32_t step = luca_ak_step(runtime->ak, delta_ms);
            if(step < 0) {
                const std::string detail = SafeLast(runtime->ak);
                LogHost(runtime, ENGINE_RUNTIME_LOG_ERROR,
                        ("luca_ak_step failed: " +
                         (detail.empty() ? std::string("(no detail)") : detail))
                            .c_str());
                return Fail(runtime->error, step, runtime->ak, "luca_ak_step");
            }
            if(step == LUCA_AK_EXIT_REQUESTED) {
                LogHost(runtime, ENGINE_RUNTIME_LOG_INFO,
                        "engine requested exit");
                runtime->terminated = true;
                runtime->error = "runtime requested termination";
                return ENGINE_RESULT_INVALID_STATE;
            }

            uint32_t width = 0;
            uint32_t height = 0;
            uint32_t stride = 0;
            const int32_t desc = luca_ak_get_frame_desc(
                runtime->ak, &width, &height, &stride);
            if(desc < 0) {
                return Fail(runtime->error, desc, runtime->ak,
                            "luca_ak_get_frame_desc");
            }
            runtime->frame_width = width;
            runtime->frame_height = height;
            runtime->frame_stride = stride;
            runtime->frame_serial += 1;
            const size_t needed =
                static_cast<size_t>(width) * height * 4u;
            runtime->frame_rgba.resize(needed);
            const int32_t read = luca_ak_read_frame_rgba(
                runtime->ak, runtime->frame_rgba.data(), needed);
            if(read < 0) {
                runtime->frame_rgba.clear();
                return Fail(runtime->error, read, runtime->ak,
                            "luca_ak_read_frame_rgba");
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
            if(luca_ak_ffi_api_version() != LUCA_AK_FFI_API_VERSION) {
                return ENGINE_RESULT_NOT_SUPPORTED;
            }
            auto runtime = std::unique_ptr<ProviderRuntime>(
                new(std::nothrow) ProviderRuntime(host));
            if(!runtime) {
                return ENGINE_RESULT_INTERNAL_ERROR;
            }
            runtime->ak = luca_ak_create();
            if(runtime->ak == nullptr) {
                runtime->error = "luca_ak_create failed";
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
            if(instance->ak != nullptr) {
                luca_ak_destroy(instance->ak);
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
                luca_ak_close(instance->ak);
                instance->opened = false;
            }
            instance->native_width = 0;
            instance->native_height = 0;
            const int32_t result = luca_ak_open(
                instance->ak, game_root_path_utf8);
            if(result < 0) {
                const std::string detail = SafeLast(instance->ak);
                LogHost(instance, ENGINE_RUNTIME_LOG_ERROR,
                        (std::string("luca_ak_open failed for ") +
                         game_root_path_utf8 + ": " +
                         (detail.empty() ? std::string("(no detail)") : detail))
                            .c_str());
                return Fail(instance->error, result, instance->ak,
                            "luca_ak_open");
            }

            // The vertical slice renders the first image PAK's first CZ
            // entry, so the native logical surface is the CZ geometry. Like
            // the siglus bridge, keep the runtime at native size and let the
            // Aether presentation layer own scaling.
            uint32_t native_width = 0;
            uint32_t native_height = 0;
            const int32_t screen_size_result = luca_ak_game_screen_size(
                instance->ak, &native_width, &native_height);
            if(screen_size_result >= 0 && native_width > 0 && native_height > 0) {
                const int32_t resize_result = luca_ak_resize(
                    instance->ak, native_width, native_height);
                if(resize_result < 0) {
                    const engine_result_t failure = Fail(
                        instance->error, resize_result, instance->ak,
                        "luca native surface resize");
                    luca_ak_close(instance->ak);
                    return failure;
                }
                instance->native_width = native_width;
                instance->native_height = native_height;
                LogHost(instance, ENGINE_RUNTIME_LOG_INFO,
                        (std::string("luca native surface ") +
                         std::to_string(native_width) + "x" +
                         std::to_string(native_height) + " (requested " +
                         std::to_string(instance->pending_width) + "x" +
                         std::to_string(instance->pending_height) + ")")
                            .c_str());
            }
            instance->opened = true;
            instance->paused = false;
            instance->terminated = false;
            instance->frame_rgba.clear();
            instance->frame_width = 0;
            instance->frame_height = 0;
            instance->frame_stride = 0;
            instance->frame_serial = 0;
            instance->delivered_frame_serial = 0;
            instance->error.clear();
            LogHost(instance, ENGINE_RUNTIME_LOG_INFO, "luca game opened");
            return ENGINE_RESULT_OK;
        }

        engine_result_t Pause(void *runtime) {
            auto *instance = Cast(runtime);
            if(instance == nullptr || !instance->opened) return ENGINE_RESULT_INVALID_STATE;
            const auto result = luca_ak_set_paused(instance->ak, 1);
            if(result >= 0) instance->paused = true;
            return OkOr(instance->error, result, instance->ak, "luca pause");
        }

        engine_result_t Resume(void *runtime) {
            auto *instance = Cast(runtime);
            if(instance == nullptr || !instance->opened) return ENGINE_RESULT_INVALID_STATE;
            const auto result = luca_ak_set_paused(instance->ak, 0);
            if(result >= 0) instance->paused = false;
            return OkOr(instance->error, result, instance->ak, "luca resume");
        }

        engine_result_t SetOption(void *runtime, const engine_option_t *option) {
            auto *instance = Cast(runtime);
            if(instance == nullptr || option == nullptr || option->key_utf8 == nullptr ||
               option->value_utf8 == nullptr) return ENGINE_RESULT_INVALID_ARGUMENT;
            // Runtime options (translation hooks, IME, skipping) arrive with
            // the script VM phases.
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
            // Once known, native geometry remains the runtime's logical
            // render space. The Aether presentation layer owns resizing.
            const uint32_t render_width =
                instance->native_width > 0 ? instance->native_width : width;
            const uint32_t render_height =
                instance->native_height > 0 ? instance->native_height : height;
            return OkOr(instance->error, luca_ak_resize(
                            instance->ak, render_width, render_height),
                        instance->ak, "luca_ak_resize");
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
            // Input routing lands with the script VM (Phase 3). Events are
            // validated and accepted so hosts keep their event pipelines.
            return ENGINE_RESULT_OK;
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
            if(Cast(runtime) == nullptr || output == nullptr || output_size == 0) {
                return ENGINE_RESULT_INVALID_ARGUMENT;
            }
            return CopyString(
                "runtime=luca provider=engine_runtime_provider_v1 "
                "render=cpu-rgba phase=vertical-slice",
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
            if(Cast(runtime) == nullptr) {
                return ENGINE_RESULT_INVALID_ARGUMENT;
            }
            return CopyString(
                "runtime=luca provider=engine_runtime_provider_v1 "
                "phase=vertical-slice frame=cpu-rgba",
                output, output_size, bytes_written);
        }

        const char *GetLastError(void *runtime) {
            ProviderRuntime *instance = Cast(runtime);
            if(instance == nullptr) {
                return "Luca runtime handle is null";
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
            // The text/IME layer arrives with the presentation phase.
            const auto size = output->struct_size;
            *output = {};
            output->struct_size = size;
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
                [](void *, uint64_t *, uint32_t *, uint32_t *, uint64_t *) {
                    return ENGINE_RESULT_NOT_SUPPORTED;
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
                [](void *, const char *, const char *) {
                    return ENGINE_RESULT_NOT_SUPPORTED;
                };
            provider.get_text_input_state =
                [](void *, uint32_t *out_state_flags) {
                    if(out_state_flags == nullptr) return ENGINE_RESULT_INVALID_ARGUMENT;
                    *out_state_flags = 0;
                    return ENGINE_RESULT_OK;
                };
            provider.get_text_input_details = GetTextInputDetails;
            provider.copy_text_input_text = [](void *runtime, char *output, uint32_t size, uint32_t *written) {
                if(Cast(runtime) == nullptr) return ENGINE_RESULT_INVALID_ARGUMENT;
                // No focused editor exists yet; report an empty string.
                return CopyString(std::string(), output, size, written);
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
            static engine_runtime_provider_v1_t provider = MakeProvider();
            return engine_register_runtime_provider(&provider);
        }
    }  // namespace

    void RegisterRuntimeProvider() {
        // Registration failures must not crash the host; the runtime simply
        // stays unavailable and probe() reports no match.
        (void)RegisterOnce();
    }

}  // namespace aetherkiri::luca
