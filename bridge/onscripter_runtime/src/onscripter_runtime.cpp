#include "onscripter_runtime.h"

#include <SDL.h>
#include <SDL_mixer.h>
#include <engine_api.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <sstream>
#include <system_error>
#include <thread>
#include <utility>

#if defined(ANDROID)
#include <sys/stat.h>
#endif

// The upstream executable does not expose its final composed surface. Access
// is kept in this one translation unit so the rest of AetherKiri depends only
// on the small Runtime interface above.
#define private public
#define protected public
#include "ONScripter.h"
#undef protected
#undef private
#include "gbk2utf16.h"
#include "sjis2utf16.h"

#undef SDL_FreeSurface

namespace fs = std::filesystem;

Coding2UTF16 *coding2utf16 = nullptr;
std::string g_stdoutpath = "stdout.txt";
std::string g_stderrpath = "stderr.txt";

// Upstream built-in layer effects refer to a process-global `ONScripter ons`.
// Keep the ABI-visible storage without registering a global destructor: the
// upstream `end` command releases SDL resources and exits before that
// destructor would normally run.
union ONScripterGlobalStorage {
    ONScripter value;

    ONScripterGlobalStorage() {}
    ~ONScripterGlobalStorage() {}
};

ONScripterGlobalStorage ons;

namespace {

std::mutex g_present_surface_mutex;
SDL_Surface *g_present_surface = nullptr;

class EmbeddedMovieHost {
public:
    virtual ~EmbeddedMovieHost() = default;
    virtual int play_video(const char *filename, bool click_to_skip,
                           bool loop) = 0;
    virtual void stop_video() = 0;
    virtual void configure_video(bool has_position, int x, int y, int width,
                                 int height, bool asynchronous) = 0;
};

std::mutex g_movie_host_mutex;
EmbeddedMovieHost *g_movie_host = nullptr;

} // namespace

extern "C" void SDLCALL
aetherkiri_onscripter_free_surface(SDL_Surface *surface) {
    std::lock_guard<std::mutex> lock(g_present_surface_mutex);
    if (surface == g_present_surface) {
        g_present_surface = nullptr;
    }
    SDL_FreeSurface(surface);
}

extern "C" int aetherkiri_onscripter_play_video(
    const char *filename, int click_to_skip, int loop) {
    EmbeddedMovieHost *host = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_movie_host_mutex);
        host = g_movie_host;
    }
    return host != nullptr
        ? host->play_video(filename, click_to_skip != 0, loop != 0)
        : 0;
}

extern "C" void aetherkiri_onscripter_stop_video() {
    EmbeddedMovieHost *host = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_movie_host_mutex);
        host = g_movie_host;
    }
    if (host != nullptr) {
        host->stop_video();
    }
}

extern "C" void aetherkiri_onscripter_configure_video(
    int has_position, int x, int y, int width, int height,
    int asynchronous) {
    EmbeddedMovieHost *host = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_movie_host_mutex);
        host = g_movie_host;
    }
    if (host != nullptr) {
        host->configure_video(has_position != 0, x, y, width, height,
                              asynchronous != 0);
    }
}

#if defined(ANDROID)
extern "C" int stat_ons(const char *path, struct stat *stat_buffer) {
    return ::stat(path, stat_buffer);
}
#endif

namespace {

struct HostExit final {
    int code = 0;
};

std::mutex g_current_directory_mutex;

std::string EnsureTrailingSeparator(const std::string &path) {
    if (path.empty()) {
        return path;
    }
    const char last = path.back();
    if (last == '/' || last == '\\') {
        return path;
    }
    return path + fs::path::preferred_separator;
}

std::string StableGameDirectoryName(const fs::path &root) {
    const std::string normalized = root.lexically_normal().generic_string();
    uint64_t hash = 1469598103934665603ull;
    for (const unsigned char value : normalized) {
        hash ^= static_cast<uint64_t>(value);
        hash *= 1099511628211ull;
    }
    std::string name = root.filename().string();
    if (name.empty()) {
        name = "game";
    }
    for (char &value : name) {
        const unsigned char byte = static_cast<unsigned char>(value);
        if (!(std::isalnum(byte) || value == '-' || value == '_')) {
            value = '_';
        }
    }
    char suffix[24] = {};
    std::snprintf(suffix, sizeof(suffix), "-%016llx",
                  static_cast<unsigned long long>(hash));
    return name + suffix;
}

bool HasScriptMarker(const fs::path &root) {
    static constexpr const char *kMarkers[] = {
        "0.txt", "00.txt", "nscr_sec.dat", "nscript.___",
        "nscript.dat", "onscript.nt2", "onscript.nt3",
    };
    std::error_code error;
    for (const char *marker : kMarkers) {
        if (fs::is_regular_file(root / marker, error)) {
            return true;
        }
        error.clear();
    }
    return false;
}

fs::path NormalizeGameRoot(const std::string &path) {
    fs::path root = fs::u8path(path);
    std::error_code error;
    if (fs::is_regular_file(root, error)) {
        root = root.parent_path();
    }
    return fs::absolute(root, error).lexically_normal();
}

SDL_Keycode WindowsVirtualKeyToSdl(int key_code) {
    if (key_code >= 0x30 && key_code <= 0x39) {
        return static_cast<SDL_Keycode>(SDLK_0 + key_code - 0x30);
    }
    if (key_code >= 0x41 && key_code <= 0x5a) {
        return static_cast<SDL_Keycode>(SDLK_a + key_code - 0x41);
    }
    if (key_code >= 0x70 && key_code <= 0x7b) {
        return static_cast<SDL_Keycode>(SDLK_F1 + key_code - 0x70);
    }
    switch (key_code) {
    case 0x08: return SDLK_BACKSPACE;
    case 0x09: return SDLK_TAB;
    case 0x0d: return SDLK_RETURN;
    case 0x10: return SDLK_LSHIFT;
    case 0x11: return SDLK_LCTRL;
    case 0x12: return SDLK_LALT;
    case 0x13: return SDLK_PAUSE;
    case 0x14: return SDLK_CAPSLOCK;
    case 0x1b: return SDLK_ESCAPE;
    case 0x20: return SDLK_SPACE;
    case 0x21: return SDLK_PAGEUP;
    case 0x22: return SDLK_PAGEDOWN;
    case 0x23: return SDLK_END;
    case 0x24: return SDLK_HOME;
    case 0x25: return SDLK_LEFT;
    case 0x26: return SDLK_UP;
    case 0x27: return SDLK_RIGHT;
    case 0x28: return SDLK_DOWN;
    case 0x2c: return SDLK_PRINTSCREEN;
    case 0x2d: return SDLK_INSERT;
    case 0x2e: return SDLK_DELETE;
    default:
        return key_code >= 0x20 && key_code <= 0x7e
            ? static_cast<SDL_Keycode>(key_code)
            : SDLK_UNKNOWN;
    }
}

SDL_Keymod EngineModifiersToSdl(int modifiers) {
    int result = KMOD_NONE;
    if ((modifiers & 0x01) != 0) result |= KMOD_SHIFT;
    if ((modifiers & 0x02) != 0) result |= KMOD_ALT;
    if ((modifiers & 0x04) != 0) result |= KMOD_CTRL;
    return static_cast<SDL_Keymod>(result);
}

std::string Utf8FromCodepoint(uint32_t codepoint) {
    std::string output;
    if (codepoint <= 0x7f) {
        output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ff) {
        output.push_back(static_cast<char>(0xc0u | (codepoint >> 6u)));
        output.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
    } else if (codepoint <= 0xffff) {
        output.push_back(static_cast<char>(0xe0u | (codepoint >> 12u)));
        output.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3fu)));
        output.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
    } else if (codepoint <= 0x10ffff) {
        output.push_back(static_cast<char>(0xf0u | (codepoint >> 18u)));
        output.push_back(static_cast<char>(0x80u | ((codepoint >> 12u) & 0x3fu)));
        output.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3fu)));
        output.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
    }
    return output;
}

} // namespace

extern "C" [[noreturn]] void aetherkiri_onscripter_host_exit(int code) {
    throw HostExit{code};
}

namespace aetherkiri::onscripter {

struct Runtime::Impl final : EmbeddedMovieHost {
    struct MovieConfiguration {
        bool has_position = false;
        bool asynchronous = false;
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
    };

    mutable std::mutex mutex;
    mutable std::mutex media_mutex;
    std::thread game_thread;
    std::atomic<StartupState> startup{StartupState::Idle};
    std::atomic<bool> initialized{false};
    std::atomic<bool> game_open{false};
    std::atomic<bool> ended{false};
    std::atomic<bool> stop_requested{false};
    std::atomic<double> input_device_scale_x{1.0};
    std::atomic<double> input_device_scale_y{1.0};
    std::atomic<int> input_device_offset_x{0};
    std::atomic<int> input_device_offset_y{0};
    ONScripter *ons = nullptr;
    std::string writable_path;
    std::string cache_path;
    std::string game_root;
    std::string default_font;
    std::string encoding = "gbk";
    std::string error;
    std::deque<std::string> logs;
    uint64_t frame_serial = 0;
    engine_handle_t media_engine = nullptr;
    engine_media_handle_t media = nullptr;
    MediaState latest_media_state;
    std::vector<uint8_t> media_rgba;
    bool media_is_script_movie = false;
    bool media_overlay_active = false;
    bool media_loop = false;
    bool media_click_to_skip = false;
    std::atomic<bool> media_skip_requested{false};
    MovieConfiguration next_movie_configuration;
    MovieConfiguration active_movie_configuration;

    void append_log(std::string message) {
        std::lock_guard<std::mutex> lock(mutex);
        logs.emplace_back(std::move(message));
        while (logs.size() > 256) {
            logs.pop_front();
        }
    }

    void set_error(std::string message) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            error = std::move(message);
            logs.emplace_back("[ONScripter Yuri] " + error);
        }
        startup.store(StartupState::Failed);
        game_open.store(false);
    }

    void set_media_error(std::string message) {
        std::lock_guard<std::mutex> lock(mutex);
        error = std::move(message);
        logs.emplace_back("[ONScripter Yuri media] " + error);
    }

    std::string engine_media_error(const std::string &fallback) const {
        if (media_engine == nullptr) {
            return fallback;
        }
        const char *message = engine_get_last_error(media_engine);
        return message != nullptr && *message != '\0'
            ? std::string(message)
            : fallback;
    }

    bool initialize_media_engine() {
        if (media_engine != nullptr) {
            return true;
        }
        engine_create_desc_t description{};
        description.struct_size = sizeof(description);
        description.api_version = ENGINE_API_VERSION;
        description.writable_path_utf8 = writable_path.c_str();
        description.cache_path_utf8 = cache_path.c_str();
        const engine_result_t result =
            engine_create(&description, &media_engine);
        if (result != ENGINE_RESULT_OK || media_engine == nullptr) {
            media_engine = nullptr;
            set_media_error(
                "failed to initialize the FFmpeg media host");
            return false;
        }
        return true;
    }

    void close_media_locked() {
        if (media != nullptr) {
            engine_media_destroy(media);
            media = nullptr;
        }
        latest_media_state = {};
        media_rgba.clear();
        media_overlay_active = false;
        media_is_script_movie = false;
        media_loop = false;
        media_click_to_skip = false;
        media_skip_requested.store(false);
    }

    void close_media() {
        std::lock_guard<std::mutex> media_lock(media_mutex);
        close_media_locked();
    }

    bool open_media_locked(const fs::path &path, bool script_movie) {
        close_media_locked();
        if (media_engine == nullptr && !initialize_media_engine()) {
            return false;
        }
        const std::string media_path = path.u8string();
        const engine_result_t result =
            engine_media_open(media_engine, media_path.c_str(), &media);
        if (result != ENGINE_RESULT_OK || media == nullptr) {
            media = nullptr;
            set_media_error(engine_media_error(
                "FFmpeg could not open the media file: " + media_path));
            return false;
        }
        media_is_script_movie = script_movie;
        return true;
    }

    fs::path resolve_script_media(const char *filename) {
        if (filename == nullptr || *filename == '\0') {
            return {};
        }
        const fs::path requested = fs::u8path(filename);
        std::error_code error_code;
        if (requested.is_absolute() &&
            fs::is_regular_file(requested, error_code)) {
            return requested;
        }
        error_code.clear();
        const fs::path direct = fs::u8path(game_root) / requested;
        if (fs::is_regular_file(direct, error_code)) {
            return direct;
        }
        if (ons == nullptr || ons->script_h.cBR == nullptr) {
            return {};
        }

        const size_t length = ons->script_h.cBR->getFileLength(filename);
        if (length == 0) {
            return {};
        }
        std::string extension = requested.extension().string();
        if (extension.size() > 16 ||
            !std::all_of(extension.begin(), extension.end(),
                         [](unsigned char value) {
                             return std::isalnum(value) || value == '.';
                         })) {
            extension = ".media";
        }
        const fs::path media_cache =
            fs::u8path(cache_path.empty() ? writable_path : cache_path) /
            "onscripter_media";
        fs::create_directories(media_cache, error_code);
        if (error_code) {
            set_media_error(
                "failed to create the ONScripter media cache");
            return {};
        }
        const std::string cache_key =
            StableGameDirectoryName(fs::u8path(game_root) / requested);
        const fs::path extracted = media_cache / (cache_key + extension);
        error_code.clear();
        if (fs::is_regular_file(extracted, error_code) &&
            fs::file_size(extracted, error_code) == length && !error_code) {
            return extracted;
        }

        std::vector<unsigned char> bytes(length);
        if (ons->script_h.cBR->getFile(filename, bytes.data()) != length) {
            set_media_error(
                "failed to extract archived movie: " +
                std::string(filename));
            return {};
        }
        std::ofstream output(extracted, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char *>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        if (!output.good()) {
            set_media_error(
                "failed to write the extracted movie cache");
            return {};
        }
        return extracted;
    }

    bool update_media_locked(bool allow_loop_restart) {
        if (media == nullptr) {
            return false;
        }
        engine_media_state_t state{};
        state.struct_size = sizeof(state);
        engine_result_t result = engine_media_get_state(media, &state);
        if (result != ENGINE_RESULT_OK) {
            set_media_error(engine_media_error(
                "failed to query FFmpeg media state"));
            return false;
        }
        if (state.status == ENGINE_MEDIA_STATUS_ENDED && media_loop &&
            allow_loop_restart) {
            if (engine_media_seek(media, 0) == ENGINE_RESULT_OK &&
                engine_media_play(media) == ENGINE_RESULT_OK) {
                state.status = ENGINE_MEDIA_STATUS_PLAYING;
                state.position_ms = 0;
            }
        }

        const size_t byte_count =
            static_cast<size_t>(state.width) *
            static_cast<size_t>(state.height) * 4u;
        const bool frame_changed =
            state.frame_ready != 0 && byte_count > 0 &&
            (media_rgba.size() != byte_count ||
             latest_media_state.frame_serial != state.frame_serial);

        latest_media_state.status = static_cast<int>(state.status);
        latest_media_state.position_ms = state.position_ms;
        latest_media_state.duration_ms = state.duration_ms;
        latest_media_state.playback_rate = state.playback_rate;
        latest_media_state.width = state.width;
        latest_media_state.height = state.height;
        latest_media_state.frame_serial = state.frame_serial;
        latest_media_state.frame_ready = state.frame_ready != 0;
        latest_media_state.seekable = state.seekable != 0;
        latest_media_state.has_audio = state.has_audio != 0;
        latest_media_state.has_video = state.has_video != 0;

        if (frame_changed) {
            media_rgba.resize(byte_count);
            engine_frame_desc_t frame{};
            frame.struct_size = sizeof(frame);
            result = engine_media_read_frame_rgba(
                media, media_rgba.data(), media_rgba.size(), &frame);
            if (result == ENGINE_RESULT_OK) {
                latest_media_state.width = frame.width;
                latest_media_state.height = frame.height;
                latest_media_state.frame_serial = frame.frame_serial;
                latest_media_state.frame_ready = true;
            }
        }
        if (media_is_script_movie &&
            state.status == ENGINE_MEDIA_STATUS_ENDED && !media_loop) {
            media_overlay_active = false;
        }
        return true;
    }

    bool poll_movie_events(bool click_to_skip) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                return true;
            }
            if (!click_to_skip) {
                continue;
            }
            if (event.type == SDL_MOUSEBUTTONUP) {
                media_skip_requested.store(true);
            } else if (event.type == SDL_KEYUP) {
                const SDL_Keycode key = event.key.keysym.sym;
                if (key == SDLK_RETURN || key == SDLK_SPACE ||
                    key == SDLK_ESCAPE) {
                    media_skip_requested.store(true);
                }
            }
        }
        return false;
    }

    int play_video(const char *filename, bool click_to_skip,
                   bool loop) override {
        const fs::path path = resolve_script_media(filename);
        if (path.empty()) {
            set_media_error(
                "movie file is unavailable: " +
                std::string(filename != nullptr ? filename : ""));
            next_movie_configuration = {};
            return 0;
        }

        MovieConfiguration configuration = next_movie_configuration;
        next_movie_configuration = {};
        {
            std::lock_guard<std::mutex> media_lock(media_mutex);
            if (!open_media_locked(path, true)) {
                return 0;
            }
            active_movie_configuration = configuration;
            media_overlay_active = true;
            media_loop = loop;
            media_click_to_skip = click_to_skip;
            media_skip_requested.store(false);
            if (engine_media_play(media) != ENGINE_RESULT_OK) {
                set_media_error(engine_media_error(
                    "FFmpeg could not start movie playback"));
                close_media_locked();
                return 0;
            }
        }
        append_log("[ONScripter Yuri media] playing: " +
                   path.u8string());
        if (configuration.asynchronous) {
            return 0;
        }

        while (!stop_requested.load()) {
            if (poll_movie_events(click_to_skip)) {
                stop_requested.store(true);
                close_media();
                return 1;
            }
            if (click_to_skip && media_skip_requested.load()) {
                close_media();
                return 0;
            }
            bool ended_now = false;
            {
                std::lock_guard<std::mutex> media_lock(media_mutex);
                if (media == nullptr) {
                    return 0;
                }
                if (!update_media_locked(true)) {
                    close_media_locked();
                    return 0;
                }
                ended_now =
                    latest_media_state.status == ENGINE_MEDIA_STATUS_ENDED &&
                    !media_loop;
            }
            if (ended_now) {
                close_media();
                return 0;
            }
            SDL_Delay(5);
        }
        close_media();
        return 1;
    }

    void stop_video() override {
        bool stopped = false;
        {
            std::lock_guard<std::mutex> media_lock(media_mutex);
            stopped = media != nullptr;
            close_media_locked();
        }
        if (stopped) {
            append_log("[ONScripter Yuri media] movie stop");
        }
    }

    void configure_video(bool has_position, int x, int y, int width,
                         int height, bool asynchronous) override {
        next_movie_configuration.has_position = has_position;
        next_movie_configuration.asynchronous = asynchronous;
        next_movie_configuration.x = x;
        next_movie_configuration.y = y;
        next_movie_configuration.width = width;
        next_movie_configuration.height = height;
    }

    void overlay_movie(Frame &frame) {
        std::lock_guard<std::mutex> media_lock(media_mutex);
        if (!media_overlay_active || !latest_media_state.frame_ready ||
            media_rgba.empty() || latest_media_state.width == 0 ||
            latest_media_state.height == 0 || frame.rgba.empty()) {
            return;
        }

        int target_x = 0;
        int target_y = 0;
        int target_width = static_cast<int>(frame.width);
        int target_height = static_cast<int>(frame.height);
        if (active_movie_configuration.has_position) {
            target_x = active_movie_configuration.x;
            target_y = active_movie_configuration.y;
            target_width = active_movie_configuration.width;
            target_height = active_movie_configuration.height;
        }
        if (target_width <= 0 || target_height <= 0) {
            return;
        }

        const int clipped_left = std::max(0, target_x);
        const int clipped_top = std::max(0, target_y);
        const int clipped_right = std::min(
            static_cast<int>(frame.width), target_x + target_width);
        const int clipped_bottom = std::min(
            static_cast<int>(frame.height), target_y + target_height);
        if (clipped_left >= clipped_right || clipped_top >= clipped_bottom) {
            return;
        }

        const size_t source_width = latest_media_state.width;
        const size_t source_height = latest_media_state.height;
        for (int destination_y = clipped_top;
             destination_y < clipped_bottom; ++destination_y) {
            const size_t source_y = std::min(
                source_height - 1,
                static_cast<size_t>(
                    (destination_y - target_y) *
                    static_cast<int64_t>(source_height) / target_height));
            for (int destination_x = clipped_left;
                 destination_x < clipped_right; ++destination_x) {
                const size_t source_x = std::min(
                    source_width - 1,
                    static_cast<size_t>(
                        (destination_x - target_x) *
                        static_cast<int64_t>(source_width) / target_width));
                const size_t source_offset =
                    (source_y * source_width + source_x) * 4u;
                const size_t destination_offset =
                    static_cast<size_t>(destination_y) *
                        frame.stride_bytes +
                    static_cast<size_t>(destination_x) * 4u;
                std::memcpy(frame.rgba.data() + destination_offset,
                            media_rgba.data() + source_offset, 4u);
            }
        }
    }

    void configure_encoding() {
        // ONScripter Yuri uses one process-wide converter, matching the
        // upstream executable and its one-game-per-process lifecycle.
        if (coding2utf16 != nullptr) {
            delete coding2utf16;
            coding2utf16 = nullptr;
        }
        if (encoding == "sjis" || encoding == "shift-jis" ||
            encoding == "shift_jis") {
            coding2utf16 = new SJIS2UTF16();
        } else {
            auto *converter = new GBK2UTF16();
            converter->force_utf8 = encoding == "utf8" || encoding == "utf-8";
            coding2utf16 = converter;
        }
    }

    void load_game_options(const fs::path &root) {
        std::ifstream arguments(root / "ons_args");
        if (!arguments) {
            return;
        }
        std::string value;
        while (arguments >> value) {
            if (value == "--enc:sjis") {
                encoding = "sjis";
            } else if (value == "--enc:utf8") {
                encoding = "utf8";
            } else if (value == "--enc:gbk") {
                encoding = "gbk";
            }
        }
    }

    void run_game(fs::path root) {
        try {
            SDL_SetHintWithPriority(SDL_HINT_VIDEODRIVER, "dummy",
                                    SDL_HINT_OVERRIDE);
            SDL_SetHintWithPriority(SDL_HINT_RENDER_DRIVER, "software",
                                    SDL_HINT_OVERRIDE);

            load_game_options(root);
            configure_encoding();
            ons = new (static_cast<void *>(&::ons)) ONScripter();
            ons->setWindowMode();
            ons->setVsyncOff();
            ons->setArchivePath(root.u8string().c_str());

            const fs::path save_root =
                fs::u8path(writable_path) / "onscripter_saves" /
                StableGameDirectoryName(root);
            std::error_code error_code;
            fs::create_directories(save_root, error_code);
            if (error_code) {
                throw std::runtime_error(
                    "failed to create save directory: " +
                    save_root.u8string());
            }
            ons->setSaveDir(save_root.u8string().c_str());

            fs::path font;
            if (!default_font.empty()) {
                font = fs::u8path(default_font);
            } else if (fs::is_regular_file(root / "default.ttf", error_code)) {
                font = root / "default.ttf";
            }
            if (!font.empty()) {
                ons->setFontFile(font.u8string().c_str());
            }

            // ScriptHandler opens the initial 0.txt/nscript.dat relative to
            // cwd even when --root is set. Limit that upstream behavior to
            // the synchronous script-read window, then restore Godot's cwd.
            {
                std::lock_guard<std::mutex> cwd_lock(
                    g_current_directory_mutex);
                const fs::path previous = fs::current_path(error_code);
                if (error_code) {
                    throw std::runtime_error(
                        "failed to read the process working directory");
                }
                fs::current_path(root, error_code);
                if (error_code) {
                    throw std::runtime_error(
                        "failed to enter the ONScripter game directory");
                }
                const int open_result = ons->openScript();
                std::error_code restore_error;
                fs::current_path(previous, restore_error);
                if (open_result != 0) {
                    throw std::runtime_error(
                        "ONScripter Yuri could not open the game script");
                }
                if (restore_error) {
                    throw std::runtime_error(
                        "failed to restore the process working directory");
                }
            }

            if (ons->init() != 0) {
                throw std::runtime_error(
                    "ONScripter Yuri initialization failed; check default.ttf and game assets");
            }

            // Godot sends coordinates in the final script surface. Upstream
            // SDL events instead expect dummy-window/device coordinates and
            // maps those back through screen_scale_ratio in its event loop.
            // Store the inverse transform so games whose script and dummy
            // window sizes differ still receive accurate button hits.
            input_device_scale_x.store(
                ons->screen_scale_ratio1 != 0.0f
                    ? 1.0 / ons->screen_scale_ratio1
                    : 1.0);
            input_device_scale_y.store(
                ons->screen_scale_ratio2 != 0.0f
                    ? 1.0 / ons->screen_scale_ratio2
                    : 1.0);
            input_device_offset_x.store(ons->render_view_rect.x);
            input_device_offset_y.store(ons->render_view_rect.y);

            {
                std::lock_guard<std::mutex> surface_lock(
                    g_present_surface_mutex);
                g_present_surface = ons->accumulation_surface;
            }
            startup.store(StartupState::Succeeded);
            game_open.store(true);
            append_log("[ONScripter Yuri] startup succeeded: " +
                       root.u8string());
            ons->executeLabel();
            ended.store(true);
            game_open.store(false);
        } catch (const HostExit &host_exit) {
            ended.store(true);
            game_open.store(false);
            if (host_exit.code == 0 || stop_requested.load()) {
                append_log("[ONScripter Yuri] runtime requested termination");
            } else {
                set_error("runtime terminated with exit code " +
                          std::to_string(host_exit.code));
            }
        } catch (const std::exception &exception) {
            set_error(exception.what());
        } catch (...) {
            set_error("unknown ONScripter Yuri runtime failure");
        }
    }
};

Runtime::Runtime() : impl_(std::make_unique<Impl>()) {}

Runtime::~Runtime() {
    shutdown();
}

bool Runtime::initialize(const std::string &writable_path,
                         const std::string &cache_path) {
    if (impl_->initialized.load()) {
        return true;
    }
    if (writable_path.empty()) {
        impl_->set_error("writable path is empty");
        return false;
    }
    impl_->writable_path = writable_path;
    impl_->cache_path = cache_path;
    impl_->startup.store(StartupState::Idle);
    if (!impl_->initialize_media_engine()) {
        return false;
    }
    {
        std::lock_guard<std::mutex> host_lock(g_movie_host_mutex);
        g_movie_host = impl_.get();
    }
    impl_->initialized.store(true);
    impl_->append_log("[ONScripter Yuri] host initialized");
    return true;
}

void Runtime::shutdown() {
    if (!impl_) {
        return;
    }
    impl_->stop_requested.store(true);
    if (impl_->game_thread.joinable()) {
        SDL_Event event{};
        event.type = SDL_QUIT;
        SDL_PushEvent(&event);
        impl_->game_thread.join();
    }
    // Also covers initialization failures which leave the upstream global
    // worker pool alive without reaching ONScripter::quit().
    aetherkiri_onscripter_shutdown_parallel();
    {
        std::lock_guard<std::mutex> host_lock(g_movie_host_mutex);
        if (g_movie_host == impl_.get()) {
            g_movie_host = nullptr;
        }
    }
    impl_->close_media();
    if (impl_->media_engine != nullptr) {
        engine_destroy(impl_->media_engine);
        impl_->media_engine = nullptr;
    }
    // The upstream `end` path frees SDL resources and then exits without
    // running the ONScripter destructor. Keep the same lifetime to avoid a
    // destructor double-free; the product supports one runtime per process.
    impl_->ons = nullptr;
    impl_->game_open.store(false);
    impl_->initialized.store(false);
}

bool Runtime::set_option(const std::string &key, const std::string &value) {
    if (key == "default_font") {
        impl_->default_font = value;
    } else if (key == "onscripter_encoding" || key == "script_encoding") {
        std::string normalized = value;
        std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                       [](unsigned char byte) {
                           return static_cast<char>(std::tolower(byte));
                       });
        impl_->encoding = normalized;
    }
    return true;
}

bool Runtime::open_game(const std::string &game_root_path) {
    if (!impl_->initialized.load()) {
        impl_->set_error("runtime is not initialized");
        return false;
    }
    if (impl_->game_thread.joinable()) {
        impl_->set_error(
            "ONScripter Yuri already ran in this process; restart Aether before opening another game");
        return false;
    }

    const fs::path root = NormalizeGameRoot(game_root_path);
    if (root.empty() || !HasScriptMarker(root)) {
        impl_->set_error(
            "no ONScripter script found (expected 0.txt, nscript.dat, onscript.nt2, or onscript.nt3)");
        return false;
    }

    impl_->game_root = root.u8string();
    impl_->startup.store(StartupState::Running);
    impl_->ended.store(false);
    impl_->stop_requested.store(false);
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->error.clear();
    }
    impl_->game_thread =
        std::thread([this, root]() { impl_->run_game(root); });
    return true;
}

bool Runtime::tick() {
    if (!impl_->initialized.load()) {
        return false;
    }
    {
        std::lock_guard<std::mutex> media_lock(impl_->media_mutex);
        if (impl_->media != nullptr) {
            impl_->update_media_locked(true);
        }
    }
    return !impl_->ended.load() &&
           impl_->startup.load() != StartupState::Failed;
}

bool Runtime::pause() {
    if (!impl_->game_open.load()) {
        return false;
    }
    Mix_Pause(-1);
    Mix_PauseMusic();
    {
        std::lock_guard<std::mutex> media_lock(impl_->media_mutex);
        if (impl_->media != nullptr) {
            engine_media_pause(impl_->media);
        }
    }
    return true;
}

bool Runtime::resume() {
    if (!impl_->game_open.load()) {
        return false;
    }
    Mix_ResumeMusic();
    Mix_Resume(-1);
    {
        std::lock_guard<std::mutex> media_lock(impl_->media_mutex);
        if (impl_->media != nullptr) {
            engine_media_play(impl_->media);
        }
    }
    return true;
}

bool Runtime::send_pointer_event(int type, double x, double y,
                                 double delta_x, double delta_y, int button,
                                 int modifiers) {
    if (!impl_->game_open.load()) {
        return false;
    }
    if (type == 3) {
        std::lock_guard<std::mutex> media_lock(impl_->media_mutex);
        if (impl_->media_is_script_movie &&
            impl_->media_click_to_skip) {
            impl_->media_skip_requested.store(true);
        }
    }
    if (type == 4) {
        SDL_Event event{};
        event.type = SDL_MOUSEWHEEL;
        event.wheel.x = static_cast<int>(std::lround(delta_x));
        event.wheel.y = static_cast<int>(std::lround(-delta_y));
        event.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
        return SDL_PushEvent(&event) == 1;
    }

    if (type == 2) {
        const double device_scale_x =
            impl_->input_device_scale_x.load();
        const double device_scale_y =
            impl_->input_device_scale_y.load();
        SDL_Event event{};
        event.type = SDL_MOUSEMOTION;
        event.motion.x = static_cast<int>(std::lround(
            x * device_scale_x + impl_->input_device_offset_x.load()));
        event.motion.y = static_cast<int>(std::lround(
            y * device_scale_y + impl_->input_device_offset_y.load()));
        event.motion.xrel = static_cast<int>(std::lround(
            delta_x * device_scale_x));
        event.motion.yrel = static_cast<int>(std::lround(
            delta_y * device_scale_y));
        event.motion.state = 0;
        if ((modifiers & 0x08) != 0) event.motion.state |= SDL_BUTTON_LMASK;
        if ((modifiers & 0x10) != 0) event.motion.state |= SDL_BUTTON_RMASK;
        if ((modifiers & 0x20) != 0) event.motion.state |= SDL_BUTTON_MMASK;
        return SDL_PushEvent(&event) == 1;
    }

    if (type == 1 || type == 3) {
        const int device_x = static_cast<int>(std::lround(
            x * impl_->input_device_scale_x.load() +
            impl_->input_device_offset_x.load()));
        const int device_y = static_cast<int>(std::lround(
            y * impl_->input_device_scale_y.load() +
            impl_->input_device_offset_y.load()));
        // ONScripter resolves a button press through `current_over_button`,
        // which is refreshed by SDL_MOUSEMOTION rather than by the button
        // event's coordinates. Godot can legitimately deliver a click
        // without a preceding motion event (for example when a new menu
        // appears beneath a stationary cursor), so make every press
        // self-contained.
        if (type == 1) {
            SDL_Event motion{};
            motion.type = SDL_MOUSEMOTION;
            motion.motion.x = device_x;
            motion.motion.y = device_y;
            motion.motion.state = 0;
            if (SDL_PushEvent(&motion) != 1) {
                return false;
            }
        }
        SDL_Event event{};
        event.type = type == 1 ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
        event.button.state = type == 1 ? SDL_PRESSED : SDL_RELEASED;
        event.button.x = device_x;
        event.button.y = device_y;
        if (button == 2 || (modifiers & 0x10) != 0) {
            event.button.button = SDL_BUTTON_RIGHT;
        } else if (button == 3 || (modifiers & 0x20) != 0) {
            event.button.button = SDL_BUTTON_MIDDLE;
        } else {
            event.button.button = SDL_BUTTON_LEFT;
        }
        event.button.clicks = 1;
        return SDL_PushEvent(&event) == 1;
    }
    return false;
}

bool Runtime::send_key_event(bool pressed, int key_code, int modifiers,
                             int unicode_codepoint) {
    if (!impl_->game_open.load()) {
        return false;
    }
    if (!pressed &&
        (key_code == 0x0d || key_code == 0x1b || key_code == 0x20)) {
        std::lock_guard<std::mutex> media_lock(impl_->media_mutex);
        if (impl_->media_is_script_movie &&
            impl_->media_click_to_skip) {
            impl_->media_skip_requested.store(true);
        }
    }
    const SDL_Keycode mapped = WindowsVirtualKeyToSdl(key_code);
    bool sent = true;
    if (mapped != SDLK_UNKNOWN) {
        SDL_Event event{};
        event.type = pressed ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.state = pressed ? SDL_PRESSED : SDL_RELEASED;
        event.key.repeat = (modifiers & 0x80) != 0 ? 1 : 0;
        event.key.keysym.sym = mapped;
        event.key.keysym.scancode = SDL_GetScancodeFromKey(mapped);
        event.key.keysym.mod = EngineModifiersToSdl(modifiers);
        sent = SDL_PushEvent(&event) == 1;
    }
    if (pressed && unicode_codepoint > 0) {
        const std::string text =
            Utf8FromCodepoint(static_cast<uint32_t>(unicode_codepoint));
        if (!text.empty() && text.size() < SDL_TEXTINPUTEVENT_TEXT_SIZE) {
            SDL_Event event{};
            event.type = SDL_TEXTINPUT;
            std::memcpy(event.text.text, text.data(), text.size());
            sent = SDL_PushEvent(&event) == 1 && sent;
        }
    }
    return sent;
}

bool Runtime::is_initialized() const {
    return impl_->initialized.load();
}

bool Runtime::is_game_open() const {
    return impl_->game_open.load();
}

bool Runtime::has_ended() const {
    return impl_->ended.load();
}

StartupState Runtime::startup_state() const {
    return impl_->startup.load();
}

std::string Runtime::last_error() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->error;
}

std::string Runtime::drain_logs() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::string output;
    while (!impl_->logs.empty()) {
        if (!output.empty()) {
            output.push_back('\n');
        }
        output += impl_->logs.front();
        impl_->logs.pop_front();
    }
    return output;
}

std::string Runtime::renderer_info() const {
    std::ostringstream output;
    output << "backend=onscripter_yuri host=godot frame=rgba8 "
              "transport=image_texture media=ffmpeg commands=full "
              "input_scale="
           << impl_->input_device_scale_x.load() << "x"
           << impl_->input_device_scale_y.load()
           << " input_offset="
           << impl_->input_device_offset_x.load() << ","
           << impl_->input_device_offset_y.load();
    return output.str();
}

bool Runtime::read_frame(Frame &frame) {
    if (!impl_->game_open.load() || impl_->ons == nullptr) {
        return false;
    }
    // Upstream releases this surface immediately before its `end` command
    // exits. The forced SDL_FreeSurface shim shares this lock, so Godot can
    // never copy from a surface while it is being destroyed.
    std::lock_guard<std::mutex> surface_guard(g_present_surface_mutex);
    SDL_Surface *surface = g_present_surface;
    if (surface == nullptr || surface->w <= 0 || surface->h <= 0 ||
        surface->pixels == nullptr || surface->format == nullptr) {
        return false;
    }

    const uint32_t width = static_cast<uint32_t>(surface->w);
    const uint32_t height = static_cast<uint32_t>(surface->h);
    const uint32_t stride = width * 4u;
    std::vector<uint8_t> rgba(static_cast<size_t>(stride) * height);
    if (SDL_LockSurface(surface) != 0) {
        return false;
    }
    const int convert_result = SDL_ConvertPixels(
        surface->w, surface->h, surface->format->format, surface->pixels,
        surface->pitch, SDL_PIXELFORMAT_RGBA32, rgba.data(),
        static_cast<int>(stride));
    SDL_UnlockSurface(surface);
    if (convert_result != 0) {
        return false;
    }

    frame.width = width;
    frame.height = height;
    frame.stride_bytes = stride;
    frame.serial = ++impl_->frame_serial;
    frame.rgba = std::move(rgba);
    impl_->overlay_movie(frame);
    return true;
}

bool Runtime::media_open(const std::string &path) {
    if (!impl_->initialized.load() || path.empty()) {
        return false;
    }
    std::error_code error_code;
    const fs::path normalized =
        fs::absolute(fs::u8path(path), error_code).lexically_normal();
    if (!fs::is_regular_file(normalized, error_code)) {
        impl_->set_media_error("media file does not exist: " + path);
        return false;
    }
    std::lock_guard<std::mutex> media_lock(impl_->media_mutex);
    return impl_->open_media_locked(normalized, false);
}

void Runtime::media_close() {
    impl_->close_media();
}

bool Runtime::media_play() {
    std::lock_guard<std::mutex> media_lock(impl_->media_mutex);
    if (impl_->media == nullptr) {
        return false;
    }
    return engine_media_play(impl_->media) == ENGINE_RESULT_OK;
}

bool Runtime::media_pause() {
    std::lock_guard<std::mutex> media_lock(impl_->media_mutex);
    if (impl_->media == nullptr) {
        return false;
    }
    return engine_media_pause(impl_->media) == ENGINE_RESULT_OK;
}

bool Runtime::media_seek(int64_t position_ms) {
    std::lock_guard<std::mutex> media_lock(impl_->media_mutex);
    if (impl_->media == nullptr || position_ms < 0) {
        return false;
    }
    return engine_media_seek(impl_->media, position_ms) == ENGINE_RESULT_OK;
}

bool Runtime::media_set_rate(double playback_rate) {
    std::lock_guard<std::mutex> media_lock(impl_->media_mutex);
    if (impl_->media == nullptr || !std::isfinite(playback_rate) ||
        playback_rate <= 0.0) {
        return false;
    }
    return engine_media_set_rate(
               impl_->media, playback_rate) == ENGINE_RESULT_OK;
}

MediaState Runtime::media_state() {
    std::lock_guard<std::mutex> media_lock(impl_->media_mutex);
    if (impl_->media != nullptr) {
        impl_->update_media_locked(true);
    }
    return impl_->latest_media_state;
}

bool Runtime::read_media_frame(Frame &frame) {
    std::lock_guard<std::mutex> media_lock(impl_->media_mutex);
    if (impl_->media != nullptr) {
        impl_->update_media_locked(true);
    }
    if (!impl_->latest_media_state.frame_ready ||
        impl_->media_rgba.empty()) {
        return false;
    }
    frame.width = impl_->latest_media_state.width;
    frame.height = impl_->latest_media_state.height;
    frame.stride_bytes = frame.width * 4u;
    frame.serial = impl_->latest_media_state.frame_serial;
    frame.rgba = impl_->media_rgba;
    return true;
}

std::string Runtime::media_subtitle_tracks_json() {
    std::lock_guard<std::mutex> media_lock(impl_->media_mutex);
    if (impl_->media == nullptr) {
        return "[]";
    }
    std::vector<char> buffer(64u * 1024u);
    uint32_t bytes_written = 0;
    if (engine_media_get_subtitle_tracks_json(
            impl_->media, buffer.data(),
            static_cast<uint32_t>(buffer.size()),
            &bytes_written) != ENGINE_RESULT_OK) {
        return "[]";
    }
    return std::string(buffer.data(), bytes_written);
}

bool Runtime::media_extract_subtitle(
    int stream_index, const std::string &output_path) {
    std::lock_guard<std::mutex> media_lock(impl_->media_mutex);
    if (impl_->media == nullptr || stream_index < 0 ||
        output_path.empty()) {
        return false;
    }
    return engine_media_extract_subtitle(
               impl_->media, stream_index,
               output_path.c_str()) == ENGINE_RESULT_OK;
}

bool Runtime::looks_like_game(const std::string &path) {
    return HasScriptMarker(NormalizeGameRoot(path));
}

} // namespace aetherkiri::onscripter
