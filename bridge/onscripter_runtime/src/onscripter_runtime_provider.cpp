#include "onscripter_runtime.h"

#include "engine_runtime_provider.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>

namespace aetherkiri::onscripter {
    namespace {

        struct ProviderRuntime {
            ProviderRuntime(const engine_runtime_host_v1_t *host_value,
                            const engine_create_desc_t *desc) :
                host(host_value != nullptr ? *host_value
                                           : engine_runtime_host_v1_t{}) {
                const std::string writable =
                    desc != nullptr && desc->writable_path_utf8 != nullptr
                    ? desc->writable_path_utf8
                    : "";
                const std::string cache =
                    desc != nullptr && desc->cache_path_utf8 != nullptr
                    ? desc->cache_path_utf8
                    : "";
                initialized = runtime.initialize(writable, cache);
                if(!initialized) {
                    error = runtime.last_error();
                }
            }

            void flush_logs() {
                std::string pending = runtime.drain_logs();
                if(pending.empty() || host.log == nullptr) {
                    return;
                }
                size_t begin = 0;
                while(begin <= pending.size()) {
                    const size_t end = pending.find('\n', begin);
                    const std::string line = pending.substr(
                        begin,
                        end == std::string::npos ? std::string::npos
                                                 : end - begin);
                    if(!line.empty()) {
                        host.log(host.user_data, ENGINE_RUNTIME_LOG_INFO,
                                 "onscripter", line.c_str());
                    }
                    if(end == std::string::npos) {
                        break;
                    }
                    begin = end + 1;
                }
            }

            engine_runtime_host_v1_t host{};
            Runtime runtime;
            Frame frame;
            bool initialized = false;
            bool frame_ready = false;
            uint64_t delivered_frame_serial = 0;
            std::string error;
        };

        ProviderRuntime *Cast(void *runtime) {
            return static_cast<ProviderRuntime *>(runtime);
        }

        engine_result_t Fail(ProviderRuntime *runtime, engine_result_t result,
                             const std::string &message) {
            if(runtime != nullptr) {
                runtime->error = message;
            }
            return result;
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

        int32_t Probe(void *, const char *game_root_path) {
            if(game_root_path == nullptr || game_root_path[0] == '\0') {
                return 0;
            }
            try {
                return Runtime::looks_like_game(game_root_path) ? 90 : 0;
            } catch(...) {
                return 0;
            }
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
                new(std::nothrow) ProviderRuntime(host, desc));
            if(!runtime) {
                return ENGINE_RESULT_INTERNAL_ERROR;
            }
            if(!runtime->initialized) {
                return ENGINE_RESULT_INVALID_STATE;
            }
            *out_runtime = runtime.release();
            return ENGINE_RESULT_OK;
        }

        void Destroy(void *runtime) { delete Cast(runtime); }

        engine_result_t OpenGame(void *opaque, const char *game_root,
                                 const char *) {
            ProviderRuntime *runtime = Cast(opaque);
            if(runtime == nullptr || game_root == nullptr ||
               game_root[0] == '\0') {
                return ENGINE_RESULT_INVALID_ARGUMENT;
            }
            runtime->error.clear();
            runtime->frame = {};
            runtime->frame_ready = false;
            runtime->delivered_frame_serial = 0;
            if(!runtime->runtime.open_game(game_root)) {
                return Fail(runtime, ENGINE_RESULT_IO_ERROR,
                            runtime->runtime.last_error());
            }

            // Runtime::open_game starts the upstream interpreter on its own
            // thread. Provider open_game is synchronous by contract; waiting
            // here lets the engine dispatcher's existing async-open worker
            // remain the single owner of startup state exposed to Godot.
            while(true) {
                runtime->flush_logs();
                const StartupState state = runtime->runtime.startup_state();
                if(state == StartupState::Succeeded) {
                    return ENGINE_RESULT_OK;
                }
                if(state == StartupState::Failed) {
                    return Fail(runtime, ENGINE_RESULT_IO_ERROR,
                                runtime->runtime.last_error());
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }

        engine_result_t Tick(void *opaque, uint32_t) {
            ProviderRuntime *runtime = Cast(opaque);
            if(runtime == nullptr) {
                return ENGINE_RESULT_INVALID_ARGUMENT;
            }
            runtime->flush_logs();
            if(runtime->runtime.tick()) {
                runtime->error.clear();
                return ENGINE_RESULT_OK;
            }
            return Fail(runtime, ENGINE_RESULT_INVALID_STATE,
                        runtime->runtime.has_ended()
                            ? "runtime requested termination"
                            : runtime->runtime.last_error());
        }

        engine_result_t Pause(void *opaque) {
            ProviderRuntime *runtime = Cast(opaque);
            if(runtime == nullptr) {
                return ENGINE_RESULT_INVALID_ARGUMENT;
            }
            return runtime->runtime.pause()
                ? ENGINE_RESULT_OK
                : Fail(runtime, ENGINE_RESULT_INVALID_STATE,
                       runtime->runtime.last_error());
        }

        engine_result_t Resume(void *opaque) {
            ProviderRuntime *runtime = Cast(opaque);
            if(runtime == nullptr) {
                return ENGINE_RESULT_INVALID_ARGUMENT;
            }
            return runtime->runtime.resume()
                ? ENGINE_RESULT_OK
                : Fail(runtime, ENGINE_RESULT_INVALID_STATE,
                       runtime->runtime.last_error());
        }

        engine_result_t SetOption(void *opaque, const engine_option_t *option) {
            ProviderRuntime *runtime = Cast(opaque);
            if(runtime == nullptr || option == nullptr ||
               option->key_utf8 == nullptr) {
                return ENGINE_RESULT_INVALID_ARGUMENT;
            }
            const std::string value =
                option->value_utf8 != nullptr ? option->value_utf8 : "";
            if(!runtime->runtime.set_option(option->key_utf8, value)) {
                return Fail(runtime, ENGINE_RESULT_INVALID_ARGUMENT,
                            runtime->runtime.last_error());
            }
            runtime->error.clear();
            return ENGINE_RESULT_OK;
        }

        engine_result_t SetSurfaceSize(void *opaque, uint32_t width,
                                       uint32_t height) {
            if(Cast(opaque) == nullptr || width == 0 || height == 0) {
                return ENGINE_RESULT_INVALID_ARGUMENT;
            }
            // ONScripter composes at script-native resolution. The shared Godot
            // player owns target-size scaling and frame enhancement.
            return ENGINE_RESULT_OK;
        }

        engine_result_t RefreshFrame(ProviderRuntime *runtime) {
            if(runtime == nullptr) {
                return ENGINE_RESULT_INVALID_ARGUMENT;
            }
            Frame frame;
            if(!runtime->runtime.read_frame(frame)) {
                return Fail(runtime, ENGINE_RESULT_INVALID_STATE,
                            runtime->runtime.last_error().empty()
                                ? "ONScripter frame is not ready"
                                : runtime->runtime.last_error());
            }
            runtime->frame = std::move(frame);
            runtime->frame_ready = true;
            runtime->error.clear();
            return ENGINE_RESULT_OK;
        }

        engine_result_t GetFrameDesc(void *opaque,
                                     engine_frame_desc_t *output) {
            ProviderRuntime *runtime = Cast(opaque);
            if(runtime == nullptr || output == nullptr ||
               output->struct_size < sizeof(engine_frame_desc_t)) {
                return ENGINE_RESULT_INVALID_ARGUMENT;
            }
            const engine_result_t result = RefreshFrame(runtime);
            if(result != ENGINE_RESULT_OK) {
                return result;
            }
            output->width = runtime->frame.width;
            output->height = runtime->frame.height;
            output->stride_bytes = runtime->frame.stride_bytes;
            output->pixel_format = ENGINE_PIXEL_FORMAT_RGBA8888;
            output->frame_serial = runtime->frame.serial;
            return ENGINE_RESULT_OK;
        }

        engine_result_t ReadFrame(void *opaque, void *output,
                                  size_t output_size) {
            ProviderRuntime *runtime = Cast(opaque);
            if(runtime == nullptr || output == nullptr) {
                return ENGINE_RESULT_INVALID_ARGUMENT;
            }
            if(!runtime->frame_ready) {
                const engine_result_t result = RefreshFrame(runtime);
                if(result != ENGINE_RESULT_OK) {
                    return result;
                }
            }
            if(output_size < runtime->frame.rgba.size()) {
                return Fail(runtime, ENGINE_RESULT_INVALID_ARGUMENT,
                            "RGBA frame output buffer is too small");
            }
            std::memcpy(output, runtime->frame.rgba.data(),
                        runtime->frame.rgba.size());
            runtime->delivered_frame_serial = runtime->frame.serial;
            runtime->error.clear();
            return ENGINE_RESULT_OK;
        }

        engine_result_t SendInput(void *opaque,
                                  const engine_input_event_t *event) {
            ProviderRuntime *runtime = Cast(opaque);
            if(runtime == nullptr || event == nullptr ||
               event->struct_size < sizeof(engine_input_event_t)) {
                return ENGINE_RESULT_INVALID_ARGUMENT;
            }

            bool sent = false;
            switch(event->type) {
                case ENGINE_INPUT_EVENT_POINTER_DOWN:
                case ENGINE_INPUT_EVENT_POINTER_MOVE:
                case ENGINE_INPUT_EVENT_POINTER_UP:
                case ENGINE_INPUT_EVENT_POINTER_SCROLL:
                    sent = runtime->runtime.send_pointer_event(
                        static_cast<int>(event->type), event->pointer_id,
                        event->x, event->y, event->delta_x, event->delta_y,
                        event->button, event->modifiers);
                    break;
                case ENGINE_INPUT_EVENT_KEY_DOWN:
                case ENGINE_INPUT_EVENT_KEY_UP:
                    sent = runtime->runtime.send_key_event(
                        event->type == ENGINE_INPUT_EVENT_KEY_DOWN,
                        event->key_code, event->modifiers, 0);
                    break;
                case ENGINE_INPUT_EVENT_TEXT_INPUT:
                    sent = runtime->runtime.send_key_event(
                        true, 0, event->modifiers,
                        static_cast<int>(event->unicode_codepoint));
                    break;
                case ENGINE_INPUT_EVENT_BACK:
                    sent = runtime->runtime.send_key_event(true, 0x1b, 0, 0) &&
                        runtime->runtime.send_key_event(false, 0x1b, 0, 0);
                    break;
                default:
                    return ENGINE_RESULT_INVALID_ARGUMENT;
            }
            return sent ? ENGINE_RESULT_OK
                        : Fail(runtime, ENGINE_RESULT_INVALID_STATE,
                               runtime->runtime.last_error());
        }

        engine_result_t GetFrameRenderedFlag(void *opaque, uint32_t *output) {
            ProviderRuntime *runtime = Cast(opaque);
            if(runtime == nullptr || output == nullptr) {
                return ENGINE_RESULT_INVALID_ARGUMENT;
            }
            const engine_result_t result = RefreshFrame(runtime);
            if(result != ENGINE_RESULT_OK) {
                *output = 0;
                return result;
            }
            *output = runtime->frame.serial != runtime->delivered_frame_serial
                ? 1u
                : 0u;
            return ENGINE_RESULT_OK;
        }

        engine_result_t GetRendererInfo(void *opaque, char *output,
                                        uint32_t output_size) {
            ProviderRuntime *runtime = Cast(opaque);
            if(runtime == nullptr) {
                return ENGINE_RESULT_INVALID_ARGUMENT;
            }
            return CopyString(runtime->runtime.renderer_info(), output,
                              output_size);
        }

        engine_result_t GetMemoryStats(void *opaque,
                                       engine_memory_stats_t *output) {
            if(Cast(opaque) == nullptr || output == nullptr ||
               output->struct_size < sizeof(engine_memory_stats_t)) {
                return ENGINE_RESULT_INVALID_ARGUMENT;
            }
            const uint32_t struct_size = output->struct_size;
            std::memset(output, 0, sizeof(*output));
            output->struct_size = struct_size;
            return ENGINE_RESULT_OK;
        }

        engine_result_t GetDebugInfo(void *opaque, char *output,
                                     uint32_t output_size,
                                     uint32_t *bytes_written) {
            if(Cast(opaque) == nullptr) {
                return ENGINE_RESULT_INVALID_ARGUMENT;
            }
            return CopyString(
                "runtime=onscripter provider=engine_runtime_provider_v1 "
                "integration=AetherRuntimePlayer media=FFmpeg commands=full",
                output, output_size, bytes_written);
        }

        const char *GetLastError(void *opaque) {
            ProviderRuntime *runtime = Cast(opaque);
            if(runtime == nullptr) {
                return "ONScripter runtime handle is null";
            }
            if(!runtime->error.empty()) {
                return runtime->error.c_str();
            }
            runtime->error = runtime->runtime.last_error();
            return runtime->error.c_str();
        }

        engine_runtime_provider_v1_t g_provider = {
            sizeof(engine_runtime_provider_v1_t),
            ENGINE_RUNTIME_PROVIDER_API_VERSION,
            "onscripter",
            "ONScripterYuri",
            90,
            nullptr,
            Probe,
            Create,
            Destroy,
            OpenGame,
            Tick,
            Pause,
            Resume,
            SetOption,
            SetSurfaceSize,
            GetFrameDesc,
            ReadFrame,
            nullptr,
            nullptr,
            nullptr,
            SendInput,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            GetFrameRenderedFlag,
            GetRendererInfo,
            GetMemoryStats,
            GetDebugInfo,
            GetLastError,
            {},
            nullptr,
            nullptr,
            nullptr,
            {}
        };

    } // namespace

    void RegisterRuntimeProvider() {
        static std::once_flag once;
        std::call_once(
            once, [] { (void)engine_register_runtime_provider(&g_provider); });
    }

} // namespace aetherkiri::onscripter
