#include "renpy_runtime.h"
#include "engine_runtime_provider.h"
#include "renpy_mobile_loader.h"
#if defined(__ANDROID__)
#include "renpy_mobile_adapter.h"
#endif
#if defined(AETHERKIRI_RENPY_IOS)
#include "renpy_runtime_ios_adapter.h"
#endif

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#if !defined(__ANDROID__) && !defined(__APPLE__)
#error "Ren'Py mobile provider must only be compiled for Android or Apple"
#endif

namespace aetherkiri::renpy {
namespace {

using mobile::Launcher;

struct MobileRuntime final {
    engine_runtime_host_v1_t host{};
    Launcher launcher;
#if defined(__ANDROID__)
    mobile::BootstrapAdapter bootstrap;
#endif
    std::mutex frame_mutex;
    std::vector<uint8_t> frame_pixels;
    uint32_t frame_width = 0;
    uint32_t frame_height = 0;
    uint32_t frame_stride = 0;
    uint64_t frame_serial = 0;
    uint64_t delivered_serial = 0;
    bool frame_rendered = false;
    bool opened = false;
    uint32_t surface_width = 0;
    uint32_t surface_height = 0;
    std::string error =
        "Ren'Py mobile runtime is not supported: official RAPT/Renios "
        "inputs do not export the host lifecycle ABI";
};

MobileRuntime* Cast(void* value) {
    return static_cast<MobileRuntime*>(value);
}

engine_result_t MapLauncherStatus(int status) {
    switch (status) {
        case RENPY_MOBILE_OK:
            return ENGINE_RESULT_OK;
        case RENPY_MOBILE_INVALID_ARGUMENT:
            return ENGINE_RESULT_INVALID_ARGUMENT;
        case RENPY_MOBILE_INVALID_STATE:
            return ENGINE_RESULT_INVALID_STATE;
        case RENPY_MOBILE_NOT_IMPLEMENTED:
            return ENGINE_RESULT_NOT_SUPPORTED;
        default:
            return ENGINE_RESULT_INTERNAL_ERROR;
    }
}

int PresentRgba(void* user_data, const uint8_t* rgba, uint32_t width,
                uint32_t height, uint32_t stride) {
    auto* runtime = static_cast<MobileRuntime*>(user_data);
    if (!runtime || !rgba || width == 0 || height == 0 ||
        width > std::numeric_limits<uint32_t>::max() / 4u ||
        stride < width * 4u) {
        return -1;
    }
    const size_t size = static_cast<size_t>(stride) * height;
    if (height != 0 && size / height != stride) return -1;
    std::lock_guard<std::mutex> lock(runtime->frame_mutex);
    runtime->frame_pixels.assign(rgba, rgba + size);
    runtime->frame_width = width;
    runtime->frame_height = height;
    runtime->frame_stride = stride;
    ++runtime->frame_serial;
    runtime->frame_rendered = true;
    return 0;
}

void LogUtf8(void* user_data, int level, const char* message) {
    (void)level;
    auto* runtime = static_cast<MobileRuntime*>(user_data);
    if (runtime && message) runtime->error = message;
}

engine_result_t CaptureFrame(MobileRuntime* runtime) {
    renpy_mobile_frame_t frame{};
    frame.struct_size = sizeof(frame);
    const int status = runtime->launcher.Frame(&frame);
    if (status != RENPY_MOBILE_OK) {
        // A launcher may deliver frames exclusively through present_rgba. A
        // missing frame is therefore not a tick failure.
        return status == RENPY_MOBILE_INVALID_STATE ? ENGINE_RESULT_OK
                                                    : MapLauncherStatus(status);
    }
    if (!frame.rgba || frame.width == 0 || frame.height == 0 ||
        frame.width > std::numeric_limits<uint32_t>::max() / 4u ||
        frame.stride < frame.width * 4u) {
        return ENGINE_RESULT_OK;
    }
    const size_t size = static_cast<size_t>(frame.stride) * frame.height;
    if (frame.height != 0 && size / frame.height != frame.stride) {
        runtime->error = "Ren'Py mobile frame stride overflows host size";
        return ENGINE_RESULT_INTERNAL_ERROR;
    }
    std::lock_guard<std::mutex> lock(runtime->frame_mutex);
    runtime->frame_pixels.assign(frame.rgba, frame.rgba + size);
    runtime->frame_width = frame.width;
    runtime->frame_height = frame.height;
    runtime->frame_stride = frame.stride;
    runtime->frame_serial = frame.serial != 0 ? frame.serial : runtime->frame_serial + 1;
    runtime->frame_rendered = true;
    return ENGINE_RESULT_OK;
}

int32_t Probe(void*, const char*) {
    // The provider is discoverable only when a rebuilt lifecycle payload is
    // linked. Official blocking RAPT/Renios archives remain invisible here.
    static Launcher launcher;
    return launcher.available() ? 100 : 0;
}

engine_result_t Create(void*, const engine_runtime_host_v1_t* host,
                       const engine_create_desc_t* desc, void** output) {
    if (!host || !desc || !output || host->struct_size < sizeof(*host) ||
        desc->struct_size < sizeof(*desc)) {
        return ENGINE_RESULT_INVALID_ARGUMENT;
    }
    *output = nullptr;
    try {
        auto runtime = std::make_unique<MobileRuntime>();
        runtime->host = *host;
        *output = runtime.release();
        return ENGINE_RESULT_OK;
    } catch (...) {
        return ENGINE_RESULT_INTERNAL_ERROR;
    }
}

void Destroy(void* value) {
    if (!value) return;
    auto* runtime = Cast(value);
    runtime->launcher.Shutdown();
#if defined(__ANDROID__)
    runtime->bootstrap.Stop();
#endif
    delete runtime;
}

engine_result_t Open(void* value, const char* game_root_path,
                     const char* startup_script) {
    if (!value || !game_root_path || game_root_path[0] == '\0') {
        return ENGINE_RESULT_INVALID_ARGUMENT;
    }
    auto* runtime = Cast(value);
    if (runtime->opened) return ENGINE_RESULT_INVALID_STATE;

    if (!runtime->launcher.available()) {
#if defined(__ANDROID__)
        mobile::BootstrapRequest request{};
        request.game_root_path_utf8 = game_root_path;
        request.startup_script_utf8 = startup_script;
        request.existing_host_activity = runtime->host.reserved_ptr[1];
        const auto result = runtime->bootstrap.Start(request);
        runtime->error = runtime->bootstrap.last_error();
        return result;
#elif defined(AETHERKIRI_RENPY_IOS)
        const auto* contract = renpy_get_ios_launcher_contract();
        runtime->error =
            "Ren'Py iOS mobile runtime is not supported: the official Renios "
            "libraries are staged, but no host-owned lifecycle payload is "
            "linked (a second UIKit/SDL entrypoint is forbidden)";
        if (contract && contract->limitation_utf8) {
            runtime->error += " ";
            runtime->error += contract->limitation_utf8;
        }
        return ENGINE_RESULT_NOT_SUPPORTED;
#else
        runtime->error = runtime->launcher.last_error();
        return ENGINE_RESULT_NOT_SUPPORTED;
#endif
    }

    renpy_mobile_config_t config{};
    config.struct_size = sizeof(config);
    config.abi_version = RENPY_MOBILE_LAUNCHER_ABI_VERSION;
    config.private_root_utf8 = game_root_path;
    config.public_root_utf8 = game_root_path;
    config.argv0_utf8 = startup_script ? startup_script : "AetherKiri";
    config.argc = 0;

    renpy_mobile_host_t host{};
    host.struct_size = sizeof(host);
    host.abi_version = RENPY_MOBILE_LAUNCHER_ABI_VERSION;
    host.user_data = runtime;
    host.present_rgba = PresentRgba;
    host.log_utf8 = LogUtf8;

    const auto result = MapLauncherStatus(runtime->launcher.Init(config, host));
    if (result != ENGINE_RESULT_OK) {
        runtime->error = runtime->launcher.last_error();
        return result;
    }
    runtime->opened = true;
    runtime->error.clear();
    return ENGINE_RESULT_OK;
}

engine_result_t Tick(void* value, uint32_t delta_ms) {
    if (!value) return ENGINE_RESULT_INVALID_ARGUMENT;
    auto* runtime = Cast(value);
    if (!runtime->opened) return ENGINE_RESULT_INVALID_STATE;
    const auto result = MapLauncherStatus(runtime->launcher.Tick(delta_ms));
    if (result != ENGINE_RESULT_OK) {
        runtime->error = runtime->launcher.last_error();
        return result;
    }
    runtime->frame_rendered = false;
    const auto frame_result = CaptureFrame(runtime);
    if (frame_result != ENGINE_RESULT_OK) return frame_result;
    return ENGINE_RESULT_OK;
}

engine_result_t Pause(void* value) {
    if (!value) return ENGINE_RESULT_INVALID_ARGUMENT;
    auto* runtime = Cast(value);
    if (!runtime->opened) return ENGINE_RESULT_INVALID_STATE;
    return MapLauncherStatus(runtime->launcher.Pause());
}

engine_result_t Resume(void* value) {
    if (!value) return ENGINE_RESULT_INVALID_ARGUMENT;
    auto* runtime = Cast(value);
    if (!runtime->opened) return ENGINE_RESULT_INVALID_STATE;
    return MapLauncherStatus(runtime->launcher.Resume());
}

engine_result_t SetSurfaceSize(void* value, uint32_t width, uint32_t height) {
    if (!value || width == 0 || height == 0) return ENGINE_RESULT_INVALID_ARGUMENT;
    auto* runtime = Cast(value);
    runtime->surface_width = width;
    runtime->surface_height = height;
    return ENGINE_RESULT_OK;
}

engine_result_t FrameDesc(void* value, engine_frame_desc_t* output) {
    if (!value || !output || output->struct_size < sizeof(*output)) {
        return ENGINE_RESULT_INVALID_ARGUMENT;
    }
    auto* runtime = Cast(value);
    std::lock_guard<std::mutex> lock(runtime->frame_mutex);
    output->width = runtime->frame_width;
    output->height = runtime->frame_height;
    output->stride_bytes = runtime->frame_stride;
    output->pixel_format = ENGINE_PIXEL_FORMAT_RGBA8888;
    output->frame_serial = runtime->frame_serial;
    return ENGINE_RESULT_OK;
}

engine_result_t ReadFrame(void* value, void* output, size_t output_size) {
    if (!value || !output) return ENGINE_RESULT_INVALID_ARGUMENT;
    auto* runtime = Cast(value);
    std::lock_guard<std::mutex> lock(runtime->frame_mutex);
    if (runtime->frame_pixels.empty()) return ENGINE_RESULT_INVALID_STATE;
    if (output_size < runtime->frame_pixels.size()) return ENGINE_RESULT_INVALID_ARGUMENT;
    std::memcpy(output, runtime->frame_pixels.data(), runtime->frame_pixels.size());
    runtime->delivered_serial = runtime->frame_serial;
    return ENGINE_RESULT_OK;
}

engine_result_t NativeFrame(void* value, uint64_t*, uint32_t*, uint32_t*,
                            uint64_t*) {
    // The lifecycle ABI owns an RGBA CPU buffer. It intentionally does not
    // expose an SDL/GLES texture that could steal Godot's render target.
    return value ? ENGINE_RESULT_NOT_SUPPORTED : ENGINE_RESULT_INVALID_ARGUMENT;
}

engine_result_t Input(void* value, const engine_input_event_t* event) {
    if (!value || !event || event->struct_size < sizeof(*event)) {
        return ENGINE_RESULT_INVALID_ARGUMENT;
    }
    auto* runtime = Cast(value);
    if (!runtime->opened) return ENGINE_RESULT_INVALID_STATE;
    renpy_mobile_input_t input{};
    input.struct_size = sizeof(input);
    input.timestamp_ns = event->timestamp_micros * 1000ull;
    input.device_id = event->pointer_id;
    input.code = event->key_code != 0 ? event->key_code : event->button;
    input.value = static_cast<int32_t>(event->type);
    input.x = static_cast<float>(event->x);
    input.y = static_cast<float>(event->y);
    input.pressure = (event->type == ENGINE_INPUT_EVENT_POINTER_UP) ? 0.0f : 1.0f;
    std::string text;
    switch (event->type) {
        case ENGINE_INPUT_EVENT_POINTER_DOWN:
        case ENGINE_INPUT_EVENT_POINTER_MOVE:
        case ENGINE_INPUT_EVENT_POINTER_UP:
        case ENGINE_INPUT_EVENT_POINTER_SCROLL:
            input.type = RENPY_MOBILE_INPUT_POINTER;
            break;
        case ENGINE_INPUT_EVENT_KEY_DOWN:
        case ENGINE_INPUT_EVENT_KEY_UP:
            input.type = RENPY_MOBILE_INPUT_KEY;
            break;
        case ENGINE_INPUT_EVENT_TEXT_INPUT: {
            input.type = RENPY_MOBILE_INPUT_TEXT;
            uint32_t cp = event->unicode_codepoint;
            if (cp <= 0x7fu) text.push_back(static_cast<char>(cp));
            else if (cp <= 0x7ffu) {
                text.push_back(static_cast<char>(0xc0u | (cp >> 6)));
                text.push_back(static_cast<char>(0x80u | (cp & 0x3fu)));
            } else if (cp <= 0xffffu) {
                text.push_back(static_cast<char>(0xe0u | (cp >> 12)));
                text.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3fu)));
                text.push_back(static_cast<char>(0x80u | (cp & 0x3fu)));
            } else if (cp <= 0x10ffffu) {
                text.push_back(static_cast<char>(0xf0u | (cp >> 18)));
                text.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3fu)));
                text.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3fu)));
                text.push_back(static_cast<char>(0x80u | (cp & 0x3fu)));
            }
            input.text_utf8 = text.c_str();
            break;
        }
        case ENGINE_INPUT_EVENT_BACK:
            input.type = RENPY_MOBILE_INPUT_KEY;
            input.code = 0x08;
            break;
        default:
            return ENGINE_RESULT_INVALID_ARGUMENT;
    }
    return MapLauncherStatus(runtime->launcher.Input(input));
}

engine_result_t Rendered(void* value, uint32_t* output) {
    if (!value || !output) return ENGINE_RESULT_INVALID_ARGUMENT;
    auto* runtime = Cast(value);
    *output = runtime->frame_rendered ? 1u : 0u;
    return ENGINE_RESULT_OK;
}

engine_result_t Renderer(void* value, char* buffer, uint32_t size) {
    if (!value || !buffer || size == 0) return ENGINE_RESULT_INVALID_ARGUMENT;
    constexpr char kRenderer[] =
        "Ren'Py mobile host lifecycle (host-owned RGBA frame)";
    if (size < sizeof(kRenderer)) {
        Cast(value)->error = "Ren'Py mobile renderer description buffer is too small";
        return ENGINE_RESULT_INVALID_ARGUMENT;
    }
    std::memcpy(buffer, kRenderer, sizeof(kRenderer));
    return ENGINE_RESULT_OK;
}

engine_result_t Unsupported(void* value) {
    if (!value) return ENGINE_RESULT_INVALID_ARGUMENT;
    Cast(value)->error =
        "Ren'Py mobile runtime operation requires the rebuilt host lifecycle ABI";
    return ENGINE_RESULT_NOT_SUPPORTED;
}

engine_result_t UnsupportedOption(void* value, const engine_option_t* option) {
    if (!value || !option || !option->key_utf8 || !option->value_utf8) {
        return ENGINE_RESULT_INVALID_ARGUMENT;
    }
    return Unsupported(value);
}

engine_result_t UnsupportedTextState(void* value, uint32_t*) {
    return Unsupported(value);
}

const char* Error(void* value) {
    return value ? Cast(value)->error.c_str() : "Invalid Ren'Py mobile runtime";
}

const engine_runtime_provider_v1_t& Provider() {
    static const engine_runtime_provider_v1_t provider = [] {
        engine_runtime_provider_v1_t p{};
        p.struct_size = sizeof(p);
        p.api_version = ENGINE_RUNTIME_PROVIDER_API_VERSION;
        p.runtime_id_utf8 = "renpy";
        p.display_name_utf8 = "Ren'Py (mobile host lifecycle)";
        p.priority = 60;
        p.probe = Probe;
        p.create = Create;
        p.destroy = Destroy;
        p.open_game = Open;
        p.tick = Tick;
        p.pause = Pause;
        p.resume = Resume;
        p.set_option = UnsupportedOption;
        p.set_surface_size = SetSurfaceSize;
        p.get_frame_desc = FrameDesc;
        p.read_frame_rgba = ReadFrame;
        p.get_godot_native_frame_texture = NativeFrame;
        p.send_input = Input;
        p.get_frame_rendered_flag = Rendered;
        p.get_renderer_info = Renderer;
        p.get_text_input_state = UnsupportedTextState;
        return p;
    }();
    return provider;
}

}  // namespace

void RegisterRuntimeProvider() {
    static std::once_flag once;
    std::call_once(once, [] { engine_register_runtime_provider(&Provider()); });
}

}  // namespace aetherkiri::renpy
