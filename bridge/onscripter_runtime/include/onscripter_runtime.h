#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace aetherkiri::onscripter {

// Registers the ONScripterYuri backend with the shared engine runtime
// dispatcher. Registration is process-wide and idempotent; hosts should call
// it before creating their first engine handle.
void RegisterRuntimeProvider();

enum class StartupState : int {
    Idle = 0,
    Running = 1,
    Succeeded = 2,
    Failed = 3,
};

struct Frame {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride_bytes = 0;
    uint64_t serial = 0;
    std::vector<uint8_t> rgba;
};

struct MediaState {
    int status = 0;
    int64_t position_ms = 0;
    int64_t duration_ms = 0;
    double playback_rate = 1.0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t frame_serial = 0;
    bool frame_ready = false;
    bool seekable = false;
    bool has_audio = false;
    bool has_video = false;
};

class Runtime final {
public:
    Runtime();
    ~Runtime();

    Runtime(const Runtime &) = delete;
    Runtime &operator=(const Runtime &) = delete;

    bool initialize(const std::string &writable_path,
                    const std::string &cache_path);
    void shutdown();

    bool set_option(const std::string &key, const std::string &value);
    bool open_game(const std::string &game_root_path);
    bool tick();
    bool pause();
    bool resume();

    bool send_pointer_event(int type, int pointer_id, double x, double y,
                            double delta_x, double delta_y, int button,
                            int modifiers);
    bool send_key_event(bool pressed, int key_code, int modifiers,
                        int unicode_codepoint);

    bool is_initialized() const;
    bool is_game_open() const;
    bool has_ended() const;
    StartupState startup_state() const;
    std::string last_error() const;
    std::string drain_logs();
    std::string renderer_info() const;
    bool read_frame(Frame &frame);

    bool media_open(const std::string &path);
    void media_close();
    bool media_play();
    bool media_pause();
    bool media_seek(int64_t position_ms);
    bool media_set_rate(double playback_rate);
    MediaState media_state();
    bool read_media_frame(Frame &frame);
    std::string media_subtitle_tracks_json();
    bool media_extract_subtitle(int stream_index,
                                const std::string &output_path);

    static bool looks_like_game(const std::string &path);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace aetherkiri::onscripter
