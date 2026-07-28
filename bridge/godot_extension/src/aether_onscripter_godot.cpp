#include "onscripter_runtime.h"

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace godot {
namespace {

constexpr int kResultOk = 0;
constexpr int kResultInvalidArgument = -1;
constexpr int kResultInvalidState = -2;
constexpr int kResultNotSupported = -3;

class AetherOnscripterPlayer final : public Node {
    GDCLASS(AetherOnscripterPlayer, Node)

public:
    AetherOnscripterPlayer() = default;
    ~AetherOnscripterPlayer() override { destroy_engine(); }

    bool initialize_engine(const String &writable_path,
                           const String &cache_path) {
        const CharString writable_utf8 = writable_path.utf8();
        const CharString cache_utf8 = cache_path.utf8();
        const bool initialized = runtime_.initialize(
            writable_utf8.get_data(), cache_utf8.get_data());
        set_result(initialized ? kResultOk : kResultInvalidState);
        if (!initialized) {
            last_error_ = String::utf8(runtime_.last_error().c_str());
        }
        return initialized;
    }

    void destroy_engine() {
        release_frame_texture();
        media_close();
        runtime_.shutdown();
        game_open_requested_ = false;
    }

    bool is_initialized() const { return runtime_.is_initialized(); }

    bool is_game_open() const {
        return game_open_requested_ &&
               runtime_.startup_state() !=
                   aetherkiri::onscripter::StartupState::Failed;
    }

    String get_last_result() const { return last_result_; }
    String get_last_error() const { return last_error_; }

    int set_render_backend(const String &backend) {
        backend_ = backend;
        set_result(kResultOk);
        return kResultOk;
    }

    String get_render_backend() const { return backend_; }

    int set_engine_option(const String &key, const String &value) {
        const CharString key_utf8 = key.utf8();
        const CharString value_utf8 = value.utf8();
        const bool accepted =
            runtime_.set_option(key_utf8.get_data(), value_utf8.get_data());
        set_result(accepted ? kResultOk : kResultInvalidArgument);
        return accepted ? kResultOk : kResultInvalidArgument;
    }

    int set_surface_size(int width, int height) {
        if (width <= 0 || height <= 0) {
            last_error_ = "surface dimensions must be positive";
            set_result(kResultInvalidArgument);
            return kResultInvalidArgument;
        }
        // ONScripter composes at the script's native resolution. Godot owns
        // presentation scaling through its TextureRect and viewport.
        set_result(kResultOk);
        return kResultOk;
    }

    int open_game(const String &game_root_path, bool async) {
        (void)async;
        const CharString path_utf8 = game_root_path.utf8();
        if (!runtime_.open_game(path_utf8.get_data())) {
            game_open_requested_ = false;
            last_error_ = String::utf8(runtime_.last_error().c_str());
            set_result(kResultInvalidArgument);
            return kResultInvalidArgument;
        }
        game_open_requested_ = true;
        last_error_ = "";
        set_result(kResultOk);
        return kResultOk;
    }

    int tick(double delta_seconds) {
        (void)delta_seconds;
        if (!runtime_.tick()) {
            if (runtime_.has_ended()) {
                last_error_ = "runtime requested termination";
            } else {
                last_error_ = String::utf8(runtime_.last_error().c_str());
            }
            set_result(kResultInvalidState);
            return kResultInvalidState;
        }
        set_result(kResultOk);
        return kResultOk;
    }

    int pause() {
        const bool paused = runtime_.pause();
        set_result(paused ? kResultOk : kResultInvalidState);
        return paused ? kResultOk : kResultInvalidState;
    }

    int resume() {
        const bool resumed = runtime_.resume();
        set_result(resumed ? kResultOk : kResultInvalidState);
        return resumed ? kResultOk : kResultInvalidState;
    }

    int send_pointer_event(int type, int pointer_id, double x, double y,
                           double delta_x, double delta_y, int button,
                           int modifiers = 0) {
        (void)pointer_id;
        const bool sent = runtime_.send_pointer_event(
            type, x, y, delta_x, delta_y, button, modifiers);
        set_result(sent ? kResultOk : kResultInvalidState);
        return sent ? kResultOk : kResultInvalidState;
    }

    int send_key_event(bool pressed, int key_code, int modifiers,
                       int unicode_codepoint) {
        const bool sent = runtime_.send_key_event(
            pressed, key_code, modifiers, unicode_codepoint);
        set_result(sent ? kResultOk : kResultInvalidState);
        return sent ? kResultOk : kResultInvalidState;
    }

    int get_startup_state() {
        if (runtime_.startup_state() ==
            aetherkiri::onscripter::StartupState::Failed) {
            last_error_ = String::utf8(runtime_.last_error().c_str());
        }
        return static_cast<int>(runtime_.startup_state());
    }

    String drain_startup_logs() {
        const std::string logs = runtime_.drain_logs();
        return logs.empty() ? String() : String::utf8(logs.c_str());
    }

    int set_diagnostic_config(bool enabled, const String &session_id,
                              int64_t category_mask,
                              int slow_frame_threshold_ms = 20,
                              int max_events = 2000) {
        (void)enabled;
        (void)session_id;
        (void)category_mask;
        (void)slow_frame_threshold_ms;
        (void)max_events;
        set_result(kResultOk);
        return kResultOk;
    }

    int64_t mark_diagnostic_event(const String &label) {
        (void)label;
        return 0;
    }

    String drain_diagnostic_events() { return String(); }

    String get_renderer_info() const {
        return String::utf8(runtime_.renderer_info().c_str());
    }

    Dictionary get_memory_stats() const { return Dictionary(); }

    String get_plugin_debug_info() const {
        return "runtime=OnscripterYuri integration=GodotTexture "
               "media=FFmpeg commands=full";
    }

    String get_frame_texture_backend() const {
        return frame_texture_.is_valid()
            ? String("onscripter_yuri_image_texture")
            : String("none");
    }

    Dictionary read_frame_rgba() {
        Dictionary output;
        aetherkiri::onscripter::Frame frame;
        if (!runtime_.read_frame(frame)) {
            return output;
        }
        PackedByteArray data;
        data.resize(static_cast<int64_t>(frame.rgba.size()));
        if (!frame.rgba.empty()) {
            std::memcpy(data.ptrw(), frame.rgba.data(), frame.rgba.size());
        }
        output["width"] = static_cast<int64_t>(frame.width);
        output["height"] = static_cast<int64_t>(frame.height);
        output["stride_bytes"] = static_cast<int64_t>(frame.stride_bytes);
        output["frame_serial"] = static_cast<int64_t>(frame.serial);
        output["rgba"] = data;
        return output;
    }

    Ref<Texture2D> update_frame_texture() {
        aetherkiri::onscripter::Frame frame;
        if (!runtime_.read_frame(frame)) {
            return frame_texture_;
        }

        PackedByteArray data;
        data.resize(static_cast<int64_t>(frame.rgba.size()));
        if (!frame.rgba.empty()) {
            std::memcpy(data.ptrw(), frame.rgba.data(), frame.rgba.size());
        }
        Ref<Image> image = Image::create_from_data(
            static_cast<int32_t>(frame.width),
            static_cast<int32_t>(frame.height), false,
            Image::FORMAT_RGBA8, data);
        if (image.is_null()) {
            last_error_ = "Godot could not create an RGBA image for the ONScripter frame";
            set_result(kResultInvalidState);
            return frame_texture_;
        }
        if (frame_texture_.is_null() ||
            frame_texture_->get_width() != static_cast<int32_t>(frame.width) ||
            frame_texture_->get_height() != static_cast<int32_t>(frame.height)) {
            frame_texture_ = ImageTexture::create_from_image(image);
        } else {
            frame_texture_->update(image);
        }
        frame_serial_ = frame.serial;
        set_result(kResultOk);
        return frame_texture_;
    }

    void release_frame_texture() {
        frame_texture_.unref();
        frame_serial_ = 0;
    }

    Dictionary debug_gpu_blend_self_test(const String &mode_name,
                                         int opacity) const {
        (void)mode_name;
        (void)opacity;
        Dictionary output;
        output["ok"] = false;
        output["error"] =
            "GPU blend diagnostics are not supported by the ONScripter software compositor";
        return output;
    }

    Dictionary debug_gpu_blend2_self_test(const String &mode_name,
                                          int opacity) const {
        return debug_gpu_blend_self_test(mode_name, opacity);
    }

    bool android_has_external_storage_permission() const { return true; }
    bool android_request_external_storage_permission() const { return true; }

    bool media_open(const String &path) {
        media_close();
        const CharString path_utf8 = path.utf8();
        const bool opened = runtime_.media_open(path_utf8.get_data());
        set_result(opened ? kResultOk : kResultInvalidArgument);
        if (!opened) {
            last_error_ = String::utf8(runtime_.last_error().c_str());
        }
        return opened;
    }
    void media_close() {
        runtime_.media_close();
        media_texture_.unref();
        media_frame_serial_ = UINT64_MAX;
        media_width_ = 0;
        media_height_ = 0;
    }
    int media_play() {
        const bool played = runtime_.media_play();
        set_result(played ? kResultOk : kResultInvalidState);
        return played ? kResultOk : kResultInvalidState;
    }
    int media_pause() {
        const bool paused = runtime_.media_pause();
        set_result(paused ? kResultOk : kResultInvalidState);
        return paused ? kResultOk : kResultInvalidState;
    }
    int media_seek(double position_seconds) {
        if (!std::isfinite(position_seconds) || position_seconds < 0.0) {
            set_result(kResultInvalidArgument);
            return kResultInvalidArgument;
        }
        const bool sought = runtime_.media_seek(static_cast<int64_t>(
            position_seconds * 1000.0));
        set_result(sought ? kResultOk : kResultInvalidState);
        return sought ? kResultOk : kResultInvalidState;
    }
    int media_set_rate(double playback_rate) {
        const bool changed = runtime_.media_set_rate(playback_rate);
        set_result(changed ? kResultOk : kResultInvalidArgument);
        return changed ? kResultOk : kResultInvalidArgument;
    }
    String media_get_subtitle_tracks_json() {
        return String::utf8(
            runtime_.media_subtitle_tracks_json().c_str());
    }
    bool media_extract_subtitle(int stream_index, const String &output_path) {
        const CharString output_utf8 = output_path.utf8();
        const bool extracted = runtime_.media_extract_subtitle(
            stream_index, output_utf8.get_data());
        set_result(extracted ? kResultOk : kResultInvalidArgument);
        return extracted;
    }
    Dictionary media_get_state() {
        const aetherkiri::onscripter::MediaState state =
            runtime_.media_state();
        media_width_ = state.width;
        media_height_ = state.height;
        Dictionary output;
        output["status"] = static_cast<int64_t>(state.status);
        output["position"] =
            static_cast<double>(state.position_ms) / 1000.0;
        output["duration"] =
            static_cast<double>(state.duration_ms) / 1000.0;
        output["rate"] = state.playback_rate;
        output["width"] = static_cast<int64_t>(state.width);
        output["height"] = static_cast<int64_t>(state.height);
        output["frame_serial"] =
            static_cast<int64_t>(state.frame_serial);
        output["frame_ready"] = state.frame_ready;
        output["seekable"] = state.seekable;
        output["has_audio"] = state.has_audio;
        output["has_video"] = state.has_video;
        return output;
    }
    Ref<Texture2D> media_update_texture() {
        aetherkiri::onscripter::Frame frame;
        if (!runtime_.read_media_frame(frame)) {
            return media_texture_;
        }
        if (media_texture_.is_valid() &&
            media_frame_serial_ == frame.serial) {
            return media_texture_;
        }
        PackedByteArray data;
        data.resize(static_cast<int64_t>(frame.rgba.size()));
        if (!frame.rgba.empty()) {
            std::memcpy(
                data.ptrw(), frame.rgba.data(), frame.rgba.size());
        }
        Ref<Image> image = Image::create_from_data(
            static_cast<int32_t>(frame.width),
            static_cast<int32_t>(frame.height), false,
            Image::FORMAT_RGBA8, data);
        if (image.is_null()) {
            return media_texture_;
        }
        if (media_texture_.is_null() ||
            media_texture_->get_width() !=
                static_cast<int32_t>(frame.width) ||
            media_texture_->get_height() !=
                static_cast<int32_t>(frame.height)) {
            media_texture_ = ImageTexture::create_from_image(image);
        } else {
            media_texture_->update(image);
        }
        media_width_ = frame.width;
        media_height_ = frame.height;
        media_frame_serial_ = frame.serial;
        return media_texture_;
    }

protected:
    static void _bind_methods() {
        ClassDB::bind_method(
            D_METHOD("initialize_engine", "writable_path", "cache_path"),
            &AetherOnscripterPlayer::initialize_engine);
        ClassDB::bind_method(D_METHOD("destroy_engine"),
                             &AetherOnscripterPlayer::destroy_engine);
        ClassDB::bind_method(D_METHOD("is_initialized"),
                             &AetherOnscripterPlayer::is_initialized);
        ClassDB::bind_method(D_METHOD("is_game_open"),
                             &AetherOnscripterPlayer::is_game_open);
        ClassDB::bind_method(D_METHOD("get_last_result"),
                             &AetherOnscripterPlayer::get_last_result);
        ClassDB::bind_method(D_METHOD("get_last_error"),
                             &AetherOnscripterPlayer::get_last_error);
        ClassDB::bind_method(D_METHOD("set_render_backend", "backend"),
                             &AetherOnscripterPlayer::set_render_backend);
        ClassDB::bind_method(D_METHOD("get_render_backend"),
                             &AetherOnscripterPlayer::get_render_backend);
        ClassDB::bind_method(D_METHOD("set_engine_option", "key", "value"),
                             &AetherOnscripterPlayer::set_engine_option);
        ClassDB::bind_method(D_METHOD("set_surface_size", "width", "height"),
                             &AetherOnscripterPlayer::set_surface_size);
        ClassDB::bind_method(D_METHOD("open_game", "game_root_path", "async"),
                             &AetherOnscripterPlayer::open_game, DEFVAL(true));
        ClassDB::bind_method(D_METHOD("tick", "delta_seconds"),
                             &AetherOnscripterPlayer::tick);
        ClassDB::bind_method(D_METHOD("pause"),
                             &AetherOnscripterPlayer::pause);
        ClassDB::bind_method(D_METHOD("resume"),
                             &AetherOnscripterPlayer::resume);
        ClassDB::bind_method(D_METHOD("send_pointer_event", "type", "pointer_id",
                                      "x", "y", "delta_x", "delta_y", "button",
                                      "modifiers"),
                             &AetherOnscripterPlayer::send_pointer_event,
                             DEFVAL(0));
        ClassDB::bind_method(D_METHOD("send_key_event", "pressed", "key_code",
                                      "modifiers", "unicode_codepoint"),
                             &AetherOnscripterPlayer::send_key_event);
        ClassDB::bind_method(D_METHOD("get_startup_state"),
                             &AetherOnscripterPlayer::get_startup_state);
        ClassDB::bind_method(D_METHOD("drain_startup_logs"),
                             &AetherOnscripterPlayer::drain_startup_logs);
        ClassDB::bind_method(D_METHOD("set_diagnostic_config", "enabled",
                                      "session_id", "category_mask",
                                      "slow_frame_threshold_ms", "max_events"),
                             &AetherOnscripterPlayer::set_diagnostic_config,
                             DEFVAL(20), DEFVAL(2000));
        ClassDB::bind_method(D_METHOD("mark_diagnostic_event", "label"),
                             &AetherOnscripterPlayer::mark_diagnostic_event);
        ClassDB::bind_method(D_METHOD("drain_diagnostic_events"),
                             &AetherOnscripterPlayer::drain_diagnostic_events);
        ClassDB::bind_method(D_METHOD("get_renderer_info"),
                             &AetherOnscripterPlayer::get_renderer_info);
        ClassDB::bind_method(D_METHOD("get_memory_stats"),
                             &AetherOnscripterPlayer::get_memory_stats);
        ClassDB::bind_method(D_METHOD("get_plugin_debug_info"),
                             &AetherOnscripterPlayer::get_plugin_debug_info);
        ClassDB::bind_method(D_METHOD("get_frame_texture_backend"),
                             &AetherOnscripterPlayer::get_frame_texture_backend);
        ClassDB::bind_method(D_METHOD("read_frame_rgba"),
                             &AetherOnscripterPlayer::read_frame_rgba);
        ClassDB::bind_method(D_METHOD("update_frame_texture"),
                             &AetherOnscripterPlayer::update_frame_texture);
        ClassDB::bind_method(D_METHOD("release_frame_texture"),
                             &AetherOnscripterPlayer::release_frame_texture);
        ClassDB::bind_method(
            D_METHOD("debug_gpu_blend_self_test", "mode", "opacity"),
            &AetherOnscripterPlayer::debug_gpu_blend_self_test, DEFVAL(255));
        ClassDB::bind_method(
            D_METHOD("debug_gpu_blend2_self_test", "mode", "opacity"),
            &AetherOnscripterPlayer::debug_gpu_blend2_self_test, DEFVAL(255));
        ClassDB::bind_method(
            D_METHOD("android_has_external_storage_permission"),
            &AetherOnscripterPlayer::android_has_external_storage_permission);
        ClassDB::bind_method(
            D_METHOD("android_request_external_storage_permission"),
            &AetherOnscripterPlayer::android_request_external_storage_permission);

        ClassDB::bind_method(D_METHOD("media_open", "path"),
                             &AetherOnscripterPlayer::media_open);
        ClassDB::bind_method(D_METHOD("media_close"),
                             &AetherOnscripterPlayer::media_close);
        ClassDB::bind_method(D_METHOD("media_play"),
                             &AetherOnscripterPlayer::media_play);
        ClassDB::bind_method(D_METHOD("media_pause"),
                             &AetherOnscripterPlayer::media_pause);
        ClassDB::bind_method(D_METHOD("media_seek", "position_seconds"),
                             &AetherOnscripterPlayer::media_seek);
        ClassDB::bind_method(D_METHOD("media_set_rate", "playback_rate"),
                             &AetherOnscripterPlayer::media_set_rate);
        ClassDB::bind_method(
            D_METHOD("media_get_subtitle_tracks_json"),
            &AetherOnscripterPlayer::media_get_subtitle_tracks_json);
        ClassDB::bind_method(
            D_METHOD("media_extract_subtitle", "stream_index", "output_path"),
            &AetherOnscripterPlayer::media_extract_subtitle);
        ClassDB::bind_method(D_METHOD("media_get_state"),
                             &AetherOnscripterPlayer::media_get_state);
        ClassDB::bind_method(D_METHOD("media_update_texture"),
                             &AetherOnscripterPlayer::media_update_texture);
    }

private:
    void set_result(int result) {
        switch (result) {
        case kResultOk: last_result_ = "OK"; break;
        case kResultInvalidArgument: last_result_ = "INVALID_ARGUMENT"; break;
        case kResultInvalidState: last_result_ = "INVALID_STATE"; break;
        case kResultNotSupported: last_result_ = "NOT_SUPPORTED"; break;
        default: last_result_ = "INTERNAL_ERROR"; break;
        }
        if (result == kResultOk) {
            last_error_ = "";
        }
    }

    aetherkiri::onscripter::Runtime runtime_;
    bool game_open_requested_ = false;
    String backend_ = "Godot Texture";
    String last_result_ = "OK";
    String last_error_;
    Ref<ImageTexture> frame_texture_;
    uint64_t frame_serial_ = 0;
    Ref<ImageTexture> media_texture_;
    uint64_t media_frame_serial_ = UINT64_MAX;
    uint32_t media_width_ = 0;
    uint32_t media_height_ = 0;
};

} // namespace

void InitializeAetherOnscripter(ModuleInitializationLevel level) {
    if (level == MODULE_INITIALIZATION_LEVEL_SCENE) {
        ClassDB::register_class<AetherOnscripterPlayer>();
    }
}

} // namespace godot
