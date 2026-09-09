#include "rfvp_runtime_provider.h"
#include "rfvp_host.h"
#include "engine_runtime_provider.h"
#include "GodotGpuBridge.h"

#include <cstring>
#include <memory>
#include <mutex>
#include <string>

namespace aetherkiri::rfvp {
namespace {
TVPGodotGpuBridgeCallbacks g_gpu_bridge{};
TVPGodotGpuBatchCallbacks g_gpu_batch{};
bool g_gpu_bridge_registered = false;

uint64_t GpuCreate(uint32_t width, uint32_t height, const void* pixels,
                   uint32_t stride) {
    return g_gpu_bridge_registered && g_gpu_bridge.create_rgba
        ? g_gpu_bridge.create_rgba(width, height, pixels, stride)
        : 0;
}
void GpuRelease(uint64_t texture) {
    if (g_gpu_bridge_registered && g_gpu_bridge.release_texture)
        g_gpu_bridge.release_texture(texture);
}
bool GpuUpdate(uint64_t texture, const void* pixels, uint32_t stride,
               const aether_rfvp_gpu_rect_t* rect) {
    return g_gpu_bridge_registered && g_gpu_bridge.update_rgba &&
        g_gpu_bridge.update_rgba(texture, pixels, stride,
            reinterpret_cast<const tTVPRect*>(rect));
}
bool GpuClear(uint64_t texture, uint32_t rgba,
              const aether_rfvp_gpu_rect_t* rect) {
    return g_gpu_bridge_registered && g_gpu_bridge.clear_rgba &&
        g_gpu_bridge.clear_rgba(texture, rgba,
            reinterpret_cast<const tTVPRect*>(rect));
}
bool GpuDraw(uint64_t dst, uint64_t src, uint32_t triangle_count,
             const aether_rfvp_gpu_rect_t* clip,
             const aether_rfvp_gpu_point_t* dst_points,
             const aether_rfvp_gpu_point_t* src_points, float opacity,
             uint32_t blend_mode) {
    return g_gpu_bridge_registered && g_gpu_bridge.draw_triangles &&
        g_gpu_bridge.draw_triangles(
            dst, src, triangle_count,
            reinterpret_cast<const tTVPRect*>(clip),
            reinterpret_cast<const tTVPPointD*>(dst_points),
            reinterpret_cast<const tTVPPointD*>(src_points), opacity,
            blend_mode);
}
bool GpuRead(uint64_t texture, void* pixels, size_t size, uint32_t stride) {
    return g_gpu_bridge_registered && g_gpu_bridge.read_rgba &&
        g_gpu_bridge.read_rgba(texture, pixels, size, stride);
}
uint64_t GpuBegin() {
    return g_gpu_bridge_registered && g_gpu_batch.begin_batch
        ? g_gpu_batch.begin_batch()
        : 0;
}
bool GpuEnd(uint64_t token) {
    return g_gpu_bridge_registered && g_gpu_batch.end_batch &&
        g_gpu_batch.end_batch(token);
}
bool GpuFlush() {
    return g_gpu_bridge_registered && g_gpu_bridge.flush &&
        g_gpu_bridge.flush();
}
const aether_rfvp_gpu_callbacks_t* GpuCallbacks() {
    static const aether_rfvp_gpu_callbacks_t callbacks{
        sizeof(aether_rfvp_gpu_callbacks_t),
        AETHER_RFVP_GPU_CALLBACKS_API_VERSION,
        GpuCreate, GpuRelease, GpuUpdate, GpuClear, GpuDraw, GpuRead,
        GpuBegin, GpuEnd, GpuFlush};
    return g_gpu_bridge_registered ? &callbacks : nullptr;
}

struct Runtime {
    void* core = nullptr;
    engine_runtime_host_v1_t host{};
    std::string writable, font, encoding = "sjis", renderer = "auto", error;
    uint64_t read_serial = 0;
    bool opened = false;
    Runtime() : core(aether_rfvp_new(GpuCallbacks())) {}
    ~Runtime() { aether_rfvp_free(core); }
    engine_result_t result(int32_t code) {
        char message[4096];
        while (const auto level = aether_rfvp_log(message, sizeof(message))) {
            if (host.log) host.log(host.user_data, level - 1, "rfvp", message);
        }
        if (code == 1) {
            error = "runtime requested termination";
            return ENGINE_RESULT_INVALID_STATE;
        }
        if (code < 0) {
            const char* text = aether_rfvp_error(core);
            error = text && *text ? text : "rfvp operation is not supported";
        } else { error.clear(); }
        return static_cast<engine_result_t>(code);
    }
};
Runtime* cast(void* p) { return static_cast<Runtime*>(p); }
int32_t Probe(void*, const char* path) { return path ? aether_rfvp_probe(path) : 0; }
engine_result_t Create(void*, const engine_runtime_host_v1_t* host,
                       const engine_create_desc_t* desc, void** output) {
    if (!host || !desc || !output || host->struct_size < sizeof(*host) ||
        desc->struct_size < sizeof(*desc)) return ENGINE_RESULT_INVALID_ARGUMENT;
    *output = nullptr;
    try {
        auto runtime = std::make_unique<Runtime>();
        runtime->host = *host;
        runtime->writable = desc->writable_path_utf8 ? desc->writable_path_utf8 : "";
        *output = runtime.release();
        return ENGINE_RESULT_OK;
    } catch (...) { return ENGINE_RESULT_INTERNAL_ERROR; }
}
void Destroy(void* p) { delete cast(p); }
engine_result_t Open(void* p, const char* path, const char* startup) {
    if (!p || !path) return ENGINE_RESULT_INVALID_ARGUMENT;
    auto& r = *cast(p);
    const auto result = r.result(aether_rfvp_open(r.core, path, startup,
        r.writable.c_str(), r.font.c_str(), r.encoding.c_str(),
        r.renderer.c_str()));
    if (result == ENGINE_RESULT_OK) r.opened = true;
    return result;
}
engine_result_t Tick(void* p, uint32_t delta) {
    if (!p) return ENGINE_RESULT_INVALID_ARGUMENT;
    return cast(p)->result(aether_rfvp_tick(cast(p)->core, delta));
}
engine_result_t Pause(void* p) {
    if (!p) return ENGINE_RESULT_INVALID_ARGUMENT;
    return cast(p)->result(aether_rfvp_pause(cast(p)->core, 1));
}
engine_result_t Resume(void* p) {
    if (!p) return ENGINE_RESULT_INVALID_ARGUMENT;
    return cast(p)->result(aether_rfvp_pause(cast(p)->core, 0));
}
engine_result_t Option(void* p, const engine_option_t* option) {
    if (!p || !option || !option->key_utf8 || !option->value_utf8)
        return ENGINE_RESULT_INVALID_ARGUMENT;
    auto& r = *cast(p);
    const std::string key = option->key_utf8, value = option->value_utf8;
    if (key == "default_font" || key == "rfvp_encoding" ||
        key == "rfvp_renderer") {
        const std::string& current = key == "default_font" ? r.font
            : key == "rfvp_encoding" ? r.encoding : r.renderer;
        if (value == current) return ENGINE_RESULT_OK;
        if (r.opened) {
            r.error = "rfvp font, encoding and renderer must be set before opening a game";
            return ENGINE_RESULT_INVALID_STATE;
        }
        if (key == "default_font") r.font = value;
        else if (key == "rfvp_encoding") {
            if (value != "sjis" && value != "gbk" && value != "utf8") {
                r.error = "rfvp_encoding must be sjis, gbk or utf8";
                return ENGINE_RESULT_INVALID_ARGUMENT;
            }
            r.encoding = value;
        } else {
            if (value != "auto" && value != "gpu" && value != "cpu") {
                r.error = "rfvp_renderer must be auto, gpu or cpu";
                return ENGINE_RESULT_INVALID_ARGUMENT;
            }
            r.renderer = value;
        }
        return ENGINE_RESULT_OK;
    }
    // The shell broadcasts KiriKiri renderer/cache options to all providers.
    // They have no effect on rfvp; reject only unknown rfvp-specific options.
    return key.rfind("rfvp_", 0) == 0 ? ENGINE_RESULT_NOT_SUPPORTED : ENGINE_RESULT_OK;
}
engine_result_t Resize(void* p, uint32_t width, uint32_t height) {
    // Godot scales the native-resolution frame and supplies native coordinates.
    return p && width && height ? ENGINE_RESULT_OK : ENGINE_RESULT_INVALID_ARGUMENT;
}
engine_result_t Frame(void* p, engine_frame_desc_t* frame) {
    if (!p || !frame || frame->struct_size < sizeof(*frame)) return ENGINE_RESULT_INVALID_ARGUMENT;
    uint32_t width = 0, height = 0;
    uint64_t serial = 0;
    auto& r = *cast(p);
    auto result = r.result(aether_rfvp_frame(r.core, &width, &height, &serial));
    if (result != ENGINE_RESULT_OK) return result;
    *frame = {};
    frame->struct_size = sizeof(*frame);
    frame->width = width; frame->height = height; frame->stride_bytes = width * 4;
    frame->pixel_format = ENGINE_PIXEL_FORMAT_RGBA8888; frame->frame_serial = serial;
    return ENGINE_RESULT_OK;
}
engine_result_t Read(void* p, void* pixels, size_t size) {
    if (!p || !pixels) return ENGINE_RESULT_INVALID_ARGUMENT;
    auto& r = *cast(p);
    auto result = r.result(aether_rfvp_read(r.core, pixels, size));
    if (result == ENGINE_RESULT_OK) {
        uint32_t w, h;
        aether_rfvp_frame(r.core, &w, &h, &r.read_serial);
    }
    return result;
}
engine_result_t NativeFrame(void* p, uint64_t* texture, uint32_t* width,
                            uint32_t* height, uint64_t* serial) {
    if (!p || !texture || !width || !height || !serial)
        return ENGINE_RESULT_INVALID_ARGUMENT;
    const auto result = aether_rfvp_gpu_frame(
        cast(p)->core, texture, width, height, serial);
    // A CPU-selected runtime legitimately has no native texture. Avoid
    // replacing its last useful error with an expected capability miss.
    return result == -3 ? ENGINE_RESULT_NOT_SUPPORTED
                        : cast(p)->result(result);
}
engine_result_t Input(void* p, const engine_input_event_t* event) {
    if (!p || !event || event->struct_size < sizeof(*event)) return ENGINE_RESULT_INVALID_ARGUMENT;
    return cast(p)->result(aether_rfvp_input(cast(p)->core, event->type, event->x, event->y,
        event->button, event->key_code, static_cast<uint32_t>(event->modifiers), event->delta_y));
}
engine_result_t Rendered(void* p, uint32_t* rendered) {
    if (!p || !rendered) return ENGINE_RESULT_INVALID_ARGUMENT;
    uint32_t w, h; uint64_t serial;
    auto& r = *cast(p);
    auto result = r.result(aether_rfvp_frame(r.core, &w, &h, &serial));
    *rendered = result == ENGINE_RESULT_OK && serial != r.read_serial;
    return result;
}
engine_result_t Renderer(void* p, char* buffer, uint32_t size) {
    uint64_t texture = 0, serial = 0;
    uint32_t width = 0, height = 0;
    const bool gpu = p && aether_rfvp_gpu_frame(
        cast(p)->core, &texture, &width, &height, &serial) == 0 && texture != 0;
    const char* name = gpu ? "rfvp Godot GPU Bridge"
                           : "rfvp CPU RGBA8 (Godot presentation)";
    const size_t required = std::strlen(name) + 1;
    if (!buffer || size < required) return ENGINE_RESULT_INVALID_ARGUMENT;
    std::memcpy(buffer, name, required);
    return ENGINE_RESULT_OK;
}
engine_result_t TextInput(void*, uint32_t* flags) {
    if (!flags) return ENGINE_RESULT_INVALID_ARGUMENT;
    *flags = 0; return ENGINE_RESULT_OK;
}
const char* Error(void* p) { return p ? cast(p)->error.c_str() : "Invalid rfvp runtime"; }
const engine_runtime_provider_v1_t& Provider() {
    static const auto provider = [] {
        engine_runtime_provider_v1_t p{};
        p.struct_size = sizeof(p); p.api_version = ENGINE_RUNTIME_PROVIDER_API_VERSION;
        p.runtime_id_utf8 = "rfvp"; p.display_name_utf8 = "FVP (rfvp)"; p.priority = 95;
        p.probe = Probe; p.create = Create; p.destroy = Destroy; p.open_game = Open; p.tick = Tick;
        p.pause = Pause; p.resume = Resume; p.set_option = Option; p.set_surface_size = Resize;
        p.get_frame_desc = Frame; p.read_frame_rgba = Read; p.send_input = Input;
        p.get_godot_native_frame_texture = NativeFrame;
        p.get_frame_rendered_flag = Rendered; p.get_renderer_info = Renderer;
        p.get_text_input_state = TextInput; p.get_last_error = Error;
        return p;
    }();
    return provider;
}
} // namespace

void RegisterRuntimeProvider() {
    static std::once_flag once;
    std::call_once(once, [] { engine_register_runtime_provider(&Provider()); });
}
void RegisterGpuBridge(const TVPGodotGpuBridgeCallbacks* callbacks,
                       const TVPGodotGpuBatchCallbacks* batch_callbacks) {
    g_gpu_bridge = {};
    g_gpu_batch = {};
    g_gpu_bridge_registered = false;
    if (!callbacks || !batch_callbacks ||
        batch_callbacks->struct_size < sizeof(TVPGodotGpuBatchCallbacks) ||
        batch_callbacks->abi_version !=
            TVP_GODOT_GPU_BATCH_CALLBACKS_ABI_VERSION ||
        !callbacks->create_rgba || !callbacks->release_texture ||
        !callbacks->update_rgba || !callbacks->clear_rgba ||
        !callbacks->draw_triangles || !callbacks->read_rgba ||
        !callbacks->flush || !batch_callbacks->begin_batch ||
        !batch_callbacks->end_batch) {
        return;
    }
    g_gpu_bridge = *callbacks;
    g_gpu_batch = *batch_callbacks;
    g_gpu_bridge_registered = true;
}
} // namespace aetherkiri::rfvp
