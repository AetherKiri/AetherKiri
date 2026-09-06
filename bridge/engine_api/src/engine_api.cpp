#include "legacy_engine_api_rename.h"
#include "engine_api.h"
#include "engine_input_queue_gate.h"

#if defined(ENGINE_API_USE_KRKR2_RUNTIME)

#include <algorithm>
#include <cstdarg>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <new>
#include <string>
#include <unordered_set>
#include <mutex>
#include <thread>
#include <memory>
#include <sstream>
#include <vector>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#include <mach/mach.h>
#include <mach/task_info.h>
#include <sys/ucontext.h>
#endif
#if defined(__ANDROID__)
#include <android/log.h>
#include <android/native_window.h>
// Defined in android_jni_bridge.cpp (C++ linkage)
ANativeWindow* krkr_GetNativeWindow();
void krkr_GetSurfaceDimensions(uint32_t*, uint32_t*);
#endif
#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
#if defined(__has_include)
#if __has_include(<execinfo.h>)
#define ENGINE_API_HAS_EXECINFO 1
#include <execinfo.h>
#endif
#else
#define ENGINE_API_HAS_EXECINFO 1
#include <execinfo.h>
#endif
#endif

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/sink.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include "environ/Application.h"
#include "environ/combase.h"
#include "environ/Platform.h"
#include "environ/EngineBootstrap.h"
#include "environ/EngineLoop.h"
#include "environ/MainScene.h"
#include "base/StorageIntf.h"
#include "base/EventIntf.h"
#include "base/impl/EventImpl.h"
#include "base/impl/StorageImpl.h"
#include "base/ScriptMgnIntf.h"
#include "base/SysInitIntf.h"
#include "base/impl/SysInitImpl.h"
#include "visual/GraphicsLoaderIntf.h"
#if defined(KRKR_ENABLE_GPU_BRIDGE)
#include "visual/ogl/ogl_common.h"
#include "visual/ogl/krkr_egl_context.h"
#endif
#include "visual/ogl/angle_backend.h"
#include "visual/impl/WindowImpl.h"
#include "visual/WindowIntf.h"
#include "visual/RenderManager.h"
#include "visual/godot/GodotRenderManager.h"
#include "visual/godot/GodotGpuBridge.h"
#include "psbfile/PSBMedia.h"
#include "sound/win32/WaveImpl.h"
#include "sound/win32/WaveMixer.h"
#include "tjsDebug.h"
#include "engine_options.h"
#include "PluginCallTracer.hpp"
#include "PluginImpl.h"
#include "base/impl/StorageImpl.h"
#include "movie/ffmpeg/KRMoviePlayer.h"
#include "utils/win32/TimerImpl.h"

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
}

// Mock bypass toggle (defined in tjsVariant.cpp, namespace TJS)
namespace TJS { void TVPSetMockEnabled(bool enabled); }

// Console log file toggle (default ON — existing behavior)
static bool g_ConsoleLogFileEnabled = true;

bool TVPIsConsoleLogFileEnabled() { return g_ConsoleLogFileEnabled; }

int TVPDrawSceneOnce(int interval);

extern "C" void TVPRegisterKrkrGLESPluginAnchor();
extern "C" void TVPRegisterKrkrLive2DPluginAnchor();
extern "C" void TVPRegisterPSDPluginAnchor();
extern "C" void TVPRegisterMotionPlayerPluginAnchor();
extern "C" void TVPRegisterLayerExDrawPluginAnchor();
extern "C" void TVPRegisterKAGParserExPluginAnchor();

extern "C" const char* TJSGetRecentExecArgTrace();
extern "C" void TJS_CollectOrphanedICCs(bool force);
extern "C" void TVPRegisterScriptsExPluginAnchor();
extern "C" void TVPRegisterCSVParserPluginAnchor();
extern "C" void TVPRegisterFstatPluginAnchor();
extern "C" bool TVPHostGetLatestFrameDesc(uint32_t* width, uint32_t* height,
                                           uint32_t* stride_bytes,
                                           uint64_t* serial);
extern "C" bool TVPHostCopyLatestFrameRGBA(void* out_pixels,
                                            size_t out_pixels_size,
                                            uint32_t* width,
                                            uint32_t* height,
                                            uint32_t* stride_bytes,
                                            uint64_t* serial);
extern "C" bool TVPHostGetLatestGodotGpuFrame(uint64_t* texture,
                                               uint32_t* width,
                                               uint32_t* height,
                                               uint64_t* serial);
extern "C" void TVPHostActivateMainWindow();
extern "C" void TVPHostSetSurfaceSize(uint32_t width, uint32_t height);
extern "C" void TVPHostSetPreferGpuFrame(bool prefer_gpu_frame);
extern "C" void TVPHostSetPublishRawSourceFrame(bool publish_raw_source);
extern "C" void TVPHostResetForGameSession();
extern "C" void AetherKiriMotionResetForGameSession();
extern void TVPClearScnearioCache();
extern "C" void TVPHostGetTextInputState(uint32_t* ime_active,
                                           int32_t* ime_mode,
                                           uint32_t* attention_point_valid,
                                           int32_t* attention_x,
                                           int32_t* attention_y,
                                           uint32_t* text_available,
                                           uint32_t* text_utf8_bytes,
                                           int32_t* selection_start,
                                           int32_t* selection_end);
extern "C" uint32_t TVPHostCopyTextInputText(char* out_buffer,
                                               uint32_t buffer_size);

struct engine_handle_s {
  std::recursive_mutex mutex;
  std::string last_error;
  int state = 0;
  std::thread::id owner_thread;
  bool runtime_owner = false;
  uint64_t tick_count = 0;

  // Frame state — readback buffer and tracking
  struct FrameState {
    uint32_t surface_width = 1280;
    uint32_t surface_height = 720;
    uint64_t serial = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride_bytes = 0;
    std::vector<uint8_t> rgba;
    bool ready = false;
    bool rendered_this_tick = false;
  } frame;

  // Frame rate limiting (0 = unlimited / follow vsync)
  struct FpsLimitState {
    uint32_t limit = 0;
    uint64_t interval_us = 0;
    std::chrono::steady_clock::time_point last_render_time{};
    bool initialized = false;
  } fps;

  // Input event queue
  struct InputState {
    std::deque<engine_input_event_t> pending_events;
    aetherkiri::engine_api::PrimaryClickQueueGate primary_click_gate;
    size_t coalesced_events = 0;
    std::unordered_set<intptr_t> active_pointer_ids;
    bool native_mouse_callbacks_disabled = false;
  } input;

  // Render target state
  struct RenderTargetState {
    krkr::AngleBackend angle_backend = krkr::AngleBackend::OpenGLES;
    std::string renderer = ENGINE_RENDERER_GODOT_NATIVE;
    bool publish_raw_source_frame = false;
    bool iosurface_attached = false;
    bool native_window_attached = false;
  } render;

  struct StartupState {
    std::mutex mutex;
    uint32_t state = ENGINE_STARTUP_STATE_IDLE;
    std::deque<std::string> logs;
    std::thread worker;
    bool worker_running = false;
  } startup;

  struct MemoryOptionState {
    int psb_cache_mb = 0;
    int psb_cache_entries = 0;
  } memory_options;

  struct DiagnosticState {
    std::mutex mutex;
    bool enabled = false;
    uint64_t category_mask = ENGINE_DIAGNOSTIC_CATEGORY_LIFECYCLE |
                             ENGINE_DIAGNOSTIC_CATEGORY_RENDER;
    uint32_t slow_frame_threshold_us = 20000;
    size_t max_events = 2000;
    uint64_t sequence = 0;
    uint64_t dropped = 0;
    uint64_t suppressed_slow_frames = 0;
    std::chrono::steady_clock::time_point last_slow_frame_log{};
    int64_t monotonic_offset_us = 0;
    std::string session_id;
    std::deque<std::string> events;
  } diagnostics;
};

class StandaloneMediaPlayer final : public KRMovie::TVPMoviePlayer {
 public:
  struct EmbeddedSubtitleTrack {
    int stream_index = -1;
    AVCodecID codec_id = AV_CODEC_ID_NONE;
    std::string codec;
    std::string language;
    std::string title;
    bool is_default = false;
  };

  StandaloneMediaPlayer() {
    m_pPlayer->SetCallback([this](KRMovieEvent event, void*) {
      if (event == KRMovieEvent::Ended) {
        ended_.store(true);
      }
    });
  }

  ~StandaloneMediaPlayer() override {
    if (m_pPlayer != nullptr) {
      m_pPlayer->SetCallback({});
    }
    ShutdownPlayer();
  }

  void Play() override {
    ended_.store(false);
    KRMovie::TVPMoviePlayer::Play();
  }

  bool Open(const char* path_utf8, std::string* error) {
    if (path_utf8 == nullptr || *path_utf8 == '\0') {
      if (error != nullptr) *error = "media path is empty";
      return false;
    }
    const ttstr path(path_utf8);
    IStream* stream = nullptr;
    std::error_code local_file_error;
    const bool is_local_file =
        std::filesystem::is_regular_file(path_utf8, local_file_error);
    if (is_local_file) {
      try {
        // Standalone media paths come from the host file picker/library and
        // are already native absolute paths. Opening them through the TVP
        // storage normalizer can remap an iOS Documents path against the
        // current visual-novel root and reject an otherwise readable file.
        // Keep TVP storage handling as the fallback for archive-backed media,
        // but bypass normalization for ordinary host files.
        stream = TVPCreateIStream(
            new tTVPLocalFileStream(path, path, TJS_BS_READ));
      } catch (...) {
        stream = nullptr;
      }
    }
    if (stream == nullptr) {
      stream = TVPCreateIStream(path, TJS_BS_READ);
    }
    if (stream == nullptr) {
      if (error != nullptr) *error = "unable to open media file";
      return false;
    }
    uint64_t size = 0;
    std::error_code file_size_error;
    const auto native_size = std::filesystem::file_size(path_utf8,
                                                        file_size_error);
    if (!file_size_error) {
      size = static_cast<uint64_t>(native_size);
    }
    const std::string extension = [] (const std::string& value) {
      const auto dot = value.find_last_of('.');
      return dot == std::string::npos ? std::string() : value.substr(dot + 1);
    }(path_utf8);
    ended_.store(false);
    const bool opened = m_pPlayer->OpenFromStream(
        stream, path.c_str(), ttstr(extension.c_str()).c_str(), size);
    stream->Release();
    if (!opened) {
      if (error != nullptr) *error = "FFmpeg could not open this media file";
      return false;
    }
    long width = 0;
    long height = 0;
    GetVideoSize(&width, &height);
    width_ = static_cast<uint32_t>(std::max<long>(0, width));
    height_ = static_cast<uint32_t>(std::max<long>(0, height));
    media_path_ = path_utf8;
    ProbeEmbeddedSubtitleTracks();
    return true;
  }

  bool UpdateFrame() {
    if (m_pPlayer == nullptr) return false;
    BitmapPicture picture;
    const double clock = m_pPlayer->GetClock() / DVD_TIME_BASE;
    {
      std::unique_lock<std::mutex> lock(m_mtxPicture);
      if (m_usedPicture <= 0 || m_picture[m_curPicture].pts > clock) {
        return false;
      }
      do {
        picture.MoveFrom(m_picture[m_curPicture]);
        --m_usedPicture;
        if (++m_curPicture >= MAX_BUFFER_COUNT) m_curPicture = 0;
      } while (m_usedPicture > 0 && m_picture[m_curPicture].pts <= clock);
      m_condPicture.notify_all();
    }
    FrameMove();
    if (picture.rgba == nullptr || picture.width <= 0 || picture.height <= 0) {
      return false;
    }
    width_ = static_cast<uint32_t>(picture.width);
    height_ = static_cast<uint32_t>(picture.height);
    const size_t byte_count =
        static_cast<size_t>(width_) * static_cast<size_t>(height_) * 4u;
    latest_rgba_.assign(picture.rgba, picture.rgba + byte_count);
    ++frame_serial_;
    return true;
  }

  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }
  uint64_t frame_serial() const { return frame_serial_; }
  bool frame_ready() const { return !latest_rgba_.empty(); }
  bool ended() const { return ended_.load(); }
  const std::vector<uint8_t>& latest_rgba() const { return latest_rgba_; }
  KRMovie::BasePlayer* player() const { return m_pPlayer; }

  std::string EmbeddedSubtitleTracksJson() const {
    std::ostringstream json;
    json << '[';
    for (size_t index = 0; index < embedded_subtitle_tracks_.size(); ++index) {
      if (index != 0) json << ',';
      const auto& track = embedded_subtitle_tracks_[index];
      json << "{\"stream_index\":" << track.stream_index
           << ",\"codec\":\"" << EscapeJson(track.codec)
           << "\",\"language\":\"" << EscapeJson(track.language)
           << "\",\"title\":\"" << EscapeJson(track.title)
           << "\",\"default\":" << (track.is_default ? "true" : "false")
           << '}';
    }
    json << ']';
    return json.str();
  }

  bool ExtractEmbeddedSubtitle(int stream_index, const char* output_path_utf8,
                               std::string* error) const {
    if (output_path_utf8 == nullptr || *output_path_utf8 == '\0') {
      if (error != nullptr) *error = "subtitle output path is empty";
      return false;
    }
    const auto selected = std::find_if(
        embedded_subtitle_tracks_.begin(), embedded_subtitle_tracks_.end(),
        [stream_index](const EmbeddedSubtitleTrack& track) {
          return track.stream_index == stream_index;
        });
    if (selected == embedded_subtitle_tracks_.end()) {
      if (error != nullptr) *error = "embedded subtitle stream was not found";
      return false;
    }

    AVFormatContext* format = nullptr;
    if (avformat_open_input(&format, media_path_.c_str(), nullptr, nullptr) < 0) {
      if (error != nullptr) *error = "FFmpeg could not reopen the media file";
      return false;
    }
    const auto close_format = [&format]() {
      if (format != nullptr) avformat_close_input(&format);
    };
    if (avformat_find_stream_info(format, nullptr) < 0 ||
        stream_index < 0 ||
        stream_index >= static_cast<int>(format->nb_streams)) {
      close_format();
      if (error != nullptr) *error = "FFmpeg could not read subtitle streams";
      return false;
    }

    AVStream* stream = format->streams[stream_index];
    if (stream == nullptr || stream->codecpar == nullptr ||
        stream->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE ||
        !IsTextSubtitleCodec(stream->codecpar->codec_id)) {
      close_format();
      if (error != nullptr) *error = "subtitle stream is not text based";
      return false;
    }

    struct SubtitlePacketCue {
      int64_t start_ms = 0;
      int64_t end_ms = 0;
      std::string payload;
    };
    std::vector<SubtitlePacketCue> cues;
    AVPacket* packet = av_packet_alloc();
    if (packet == nullptr) {
      close_format();
      if (error != nullptr) *error = "unable to allocate subtitle packet";
      return false;
    }
    while (av_read_frame(format, packet) >= 0) {
      if (packet->stream_index == stream_index && packet->data != nullptr &&
          packet->size > 0) {
        const int64_t timestamp =
            packet->pts != AV_NOPTS_VALUE ? packet->pts : packet->dts;
        if (timestamp != AV_NOPTS_VALUE) {
          SubtitlePacketCue cue;
          cue.start_ms = std::max<int64_t>(
              0, av_rescale_q(timestamp, stream->time_base, AVRational{1, 1000}));
          const int64_t duration_ms =
              packet->duration > 0
                  ? av_rescale_q(packet->duration, stream->time_base,
                                 AVRational{1, 1000})
                  : 0;
          cue.end_ms = cue.start_ms + std::max<int64_t>(0, duration_ms);
          cue.payload.assign(reinterpret_cast<const char*>(packet->data),
                             static_cast<size_t>(packet->size));
          NormalizeSubtitlePayload(&cue.payload);
          if (!cue.payload.empty()) cues.push_back(std::move(cue));
        }
      }
      av_packet_unref(packet);
    }
    av_packet_free(&packet);
    close_format();

    std::sort(cues.begin(), cues.end(),
              [](const SubtitlePacketCue& left,
                 const SubtitlePacketCue& right) {
                return left.start_ms < right.start_ms;
              });
    for (size_t index = 0; index < cues.size(); ++index) {
      if (cues[index].end_ms <= cues[index].start_ms) {
        const int64_t next_start =
            index + 1 < cues.size() ? cues[index + 1].start_ms
                                    : cues[index].start_ms + 5000;
        cues[index].end_ms =
            std::max<int64_t>(cues[index].start_ms + 100,
                              std::min<int64_t>(next_start,
                                                cues[index].start_ms + 5000));
      }
    }

    std::ofstream output(output_path_utf8, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
      if (error != nullptr) *error = "unable to create subtitle sidecar";
      return false;
    }
    output << "[Script Info]\n"
              "ScriptType: v4.00+\n\n"
              "[Events]\n"
              "Format: Layer, Start, End, Style, Name, MarginL, MarginR, "
              "MarginV, Effect, Text\n";
    const bool is_ass = selected->codec_id == AV_CODEC_ID_ASS ||
                        selected->codec_id == AV_CODEC_ID_SSA;
    for (const auto& cue : cues) {
      const std::string start = FormatAssTimestamp(cue.start_ms);
      const std::string end = FormatAssTimestamp(cue.end_ms);
      if (is_ass) {
        output << "Dialogue: "
               << BuildAssDialogueBody(cue.payload, start, end) << '\n';
      } else {
        output << "Dialogue: 0," << start << ',' << end
               << ",Default,,0,0,0,," << cue.payload << '\n';
      }
    }
    if (!output.good()) {
      if (error != nullptr) *error = "unable to write subtitle sidecar";
      return false;
    }
    return true;
  }

 private:
  static bool IsTextSubtitleCodec(AVCodecID codec_id) {
    switch (codec_id) {
      case AV_CODEC_ID_ASS:
      case AV_CODEC_ID_SSA:
      case AV_CODEC_ID_SUBRIP:
      case AV_CODEC_ID_WEBVTT:
      case AV_CODEC_ID_TEXT:
      case AV_CODEC_ID_MOV_TEXT:
        return true;
      default:
        return false;
    }
  }

  static std::string EscapeJson(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (const unsigned char character : value) {
      switch (character) {
        case '\"': escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\b': escaped += "\\b"; break;
        case '\f': escaped += "\\f"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
          if (character < 0x20u) {
            char buffer[7] = {};
            std::snprintf(buffer, sizeof(buffer), "\\u%04x", character);
            escaped += buffer;
          } else {
            escaped.push_back(static_cast<char>(character));
          }
      }
    }
    return escaped;
  }

  static void NormalizeSubtitlePayload(std::string* payload) {
    if (payload == nullptr) return;
    std::string normalized;
    normalized.reserve(payload->size());
    for (size_t index = 0; index < payload->size(); ++index) {
      const char character = (*payload)[index];
      if (character == '\0') continue;
      if (character == '\r' || character == '\n') {
        if (character == '\r' && index + 1 < payload->size() &&
            (*payload)[index + 1] == '\n') {
          ++index;
        }
        normalized += "\\N";
      } else {
        normalized.push_back(character);
      }
    }
    *payload = std::move(normalized);
  }

  static std::string FormatAssTimestamp(int64_t milliseconds) {
    milliseconds = std::max<int64_t>(0, milliseconds);
    const int64_t total_seconds = milliseconds / 1000;
    const int64_t centiseconds = (milliseconds % 1000) / 10;
    const int64_t hours = total_seconds / 3600;
    const int64_t minutes = (total_seconds % 3600) / 60;
    const int64_t seconds = total_seconds % 60;
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "%lld:%02lld:%02lld.%02lld",
                  static_cast<long long>(hours),
                  static_cast<long long>(minutes),
                  static_cast<long long>(seconds),
                  static_cast<long long>(centiseconds));
    return buffer;
  }

  static std::string BuildAssDialogueBody(const std::string& packet_payload,
                                          const std::string& start,
                                          const std::string& end) {
    const size_t read_order_end = packet_payload.find(',');
    const size_t layer_end =
        read_order_end == std::string::npos
            ? std::string::npos
            : packet_payload.find(',', read_order_end + 1);
    if (read_order_end == std::string::npos ||
        layer_end == std::string::npos) {
      return "0," + start + "," + end + ",Default,,0,0,0,," +
             packet_payload;
    }
    const std::string layer =
        packet_payload.substr(read_order_end + 1,
                              layer_end - read_order_end - 1);
    const std::string remaining = packet_payload.substr(layer_end + 1);
    return layer + "," + start + "," + end + "," + remaining;
  }

  void ProbeEmbeddedSubtitleTracks() {
    embedded_subtitle_tracks_.clear();
    AVFormatContext* format = nullptr;
    if (avformat_open_input(&format, media_path_.c_str(), nullptr, nullptr) < 0) {
      return;
    }
    if (avformat_find_stream_info(format, nullptr) < 0) {
      avformat_close_input(&format);
      return;
    }
    for (unsigned int index = 0; index < format->nb_streams; ++index) {
      AVStream* stream = format->streams[index];
      if (stream == nullptr || stream->codecpar == nullptr ||
          stream->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE ||
          !IsTextSubtitleCodec(stream->codecpar->codec_id)) {
        continue;
      }
      EmbeddedSubtitleTrack track;
      track.stream_index = static_cast<int>(index);
      track.codec_id = stream->codecpar->codec_id;
      const char* codec_name = avcodec_get_name(track.codec_id);
      track.codec = codec_name != nullptr ? codec_name : "text";
      if (const AVDictionaryEntry* language =
              av_dict_get(stream->metadata, "language", nullptr, 0)) {
        track.language = language->value != nullptr ? language->value : "";
      }
      if (const AVDictionaryEntry* title =
              av_dict_get(stream->metadata, "title", nullptr, 0)) {
        track.title = title->value != nullptr ? title->value : "";
      }
      track.is_default = (stream->disposition & AV_DISPOSITION_DEFAULT) != 0;
      embedded_subtitle_tracks_.push_back(std::move(track));
    }
    avformat_close_input(&format);
  }

  std::atomic_bool ended_{false};
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  uint64_t frame_serial_ = 0;
  std::vector<uint8_t> latest_rgba_;
  std::string media_path_;
  std::vector<EmbeddedSubtitleTrack> embedded_subtitle_tracks_;
};

struct engine_media_handle_s {
  std::recursive_mutex mutex;
  engine_handle_t owner = nullptr;
  std::unique_ptr<StandaloneMediaPlayer> player;
};

namespace {

#if defined(__ANDROID__)
void AndroidInfoLog(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  __android_log_vprint(ANDROID_LOG_INFO, "krkr2", fmt, args);
  va_end(args);
}
#endif

enum class EngineState {
  kCreated = 0,
  kOpened,
  kPaused,
  kDestroyed,
};

inline int ToStateValue(EngineState state) {
  return static_cast<int>(state);
}

std::recursive_mutex g_registry_mutex;
std::unordered_set<engine_handle_t> g_live_handles;
thread_local std::string g_thread_error;
engine_handle_t g_runtime_owner = nullptr;
bool g_runtime_active = false;
bool g_runtime_started_once = false;
bool g_engine_bootstrapped = false;
bool g_runtime_startup_active = false;
engine_handle_t g_runtime_startup_owner = nullptr;
std::once_flag g_loggers_init_once;
std::shared_ptr<spdlog::sinks::sink> g_startup_log_sink;
std::mutex g_game_log_file_sink_mutex;
std::shared_ptr<spdlog::sinks::sink> g_game_log_file_sink;
std::string g_game_log_file_path;
constexpr size_t kMaxStartupLogs = 4000;

void PushRuntimeSpdlogToStartupQueue(const spdlog::details::log_msg& msg);

uint64_t DurationUs(std::chrono::steady_clock::time_point begin,
                    std::chrono::steady_clock::time_point end) {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
          .count());
}

uint64_t SteadyMonotonicUs() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now().time_since_epoch()).count());
}

uint64_t AlignedMonotonicUs(int64_t offset_us) {
  const int64_t aligned = static_cast<int64_t>(SteadyMonotonicUs()) + offset_us;
  return aligned > 0 ? static_cast<uint64_t>(aligned) : 0;
}

std::string JsonEscape(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size() + 16);
  for (const unsigned char c : value) {
    switch (c) {
      case '\"': escaped += "\\\""; break;
      case '\\': escaped += "\\\\"; break;
      case '\b': escaped += "\\b"; break;
      case '\f': escaped += "\\f"; break;
      case '\n': escaped += "\\n"; break;
      case '\r': escaped += "\\r"; break;
      case '\t': escaped += "\\t"; break;
      default:
        if (c < 0x20u) {
          char buffer[7] = {};
          std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
          escaped += buffer;
        } else {
          escaped.push_back(static_cast<char>(c));
        }
    }
  }
  return escaped;
}

uint64_t PushDiagnosticEvent(engine_handle_s* impl, const char* layer,
                             const char* subsystem, const char* level,
                             const char* event, uint64_t duration_us,
                             const std::string& fields_json = "{}") {
  if (impl == nullptr) return 0;
  std::lock_guard<std::mutex> guard(impl->diagnostics.mutex);
  auto& state = impl->diagnostics;
  if (!state.enabled) return 0;
  const uint64_t sequence = ++state.sequence;
  while (state.events.size() >= state.max_events) {
    state.events.pop_front();
    ++state.dropped;
  }
  std::ostringstream line;
  line << "{\"schema\":1,\"session\":\"" << JsonEscape(state.session_id)
       << "\",\"sequence\":" << sequence
       << ",\"monotonic_us\":" << AlignedMonotonicUs(state.monotonic_offset_us)
       << ",\"platform\":\"native\",\"layer\":\"" << JsonEscape(layer)
       << "\",\"subsystem\":\"" << JsonEscape(subsystem)
       << "\",\"level\":\"" << JsonEscape(level)
       << "\",\"event\":\"" << JsonEscape(event)
       << "\",\"duration_us\":" << duration_us
       << ",\"queue_dropped\":" << state.dropped
       << ",\"fields\":" << (fields_json.empty() ? "{}" : fields_json) << "}";
  state.events.push_back(line.str());
  return sequence;
}

uint64_t EngineTickSpikeThresholdUs() {
  static const uint64_t threshold = []() -> uint64_t {
    const char* value = std::getenv("AETHERKIRI_ENGINE_TICK_SPIKE_MS");
    if (value == nullptr || *value == '\0') {
      return 0;
    }
    const double ms = std::atof(value);
    if (ms <= 0.0) {
      return 0;
    }
    return static_cast<uint64_t>(ms * 1000.0);
  }();
  return threshold;
}

bool EnvFlagEnabled(const char* name) {
  const char* value = std::getenv(name);
  if (value == nullptr || *value == '\0') {
    return false;
  }
  return std::strcmp(value, "0") != 0 &&
         std::strcmp(value, "false") != 0 &&
         std::strcmp(value, "off") != 0 &&
         std::strcmp(value, "no") != 0;
}

bool ShouldUseGodotGpuFrameForRenderer(const std::string& renderer) {
  if (renderer == ENGINE_RENDERER_DEBUG_CPU) {
    return false;
  }
#if defined(__APPLE__) && TARGET_OS_IPHONE
  return !EnvFlagEnabled("AETHERKIRI_IOS_DISABLE_GODOT_GPU_FASTPATH");
#else
  return true;
#endif
}

class StartupLogSink final : public spdlog::sinks::sink {
 public:
  void log(const spdlog::details::log_msg& msg) override {
    PushRuntimeSpdlogToStartupQueue(msg);
  }

  void flush() override {}
  void set_pattern(const std::string&) override {}
  void set_formatter(std::unique_ptr<spdlog::formatter>) override {}
};

std::shared_ptr<spdlog::logger> EnsureNamedLogger(const char* name) {
  if (auto logger = spdlog::get(name); logger != nullptr) {
    return logger;
  }
  return spdlog::stdout_color_mt(name);
}

#if !defined(_WIN32)
void CrashSignalHandler(int sig, siginfo_t* info, void* context) {
  spdlog::critical("FATAL SIGNAL {} received! fault={}", sig,
                   info ? info->si_addr : nullptr);
#if defined(__APPLE__) && defined(__aarch64__)
  if (auto* uctx = static_cast<ucontext_t*>(context);
      uctx && uctx->uc_mcontext) {
    const auto& ss = uctx->uc_mcontext->__ss;
    spdlog::critical(
        "registers pc={} lr={} sp={} fp={} x0={} x1={} x2={} x3={} x8={} "
        "x19={} x20={} x21={} x22={} x23={} x24={} x25={} x26={} x27={} "
        "x28={}",
        reinterpret_cast<void*>(ss.__pc), reinterpret_cast<void*>(ss.__lr),
        reinterpret_cast<void*>(ss.__sp), reinterpret_cast<void*>(ss.__fp),
        reinterpret_cast<void*>(ss.__x[0]), reinterpret_cast<void*>(ss.__x[1]),
        reinterpret_cast<void*>(ss.__x[2]), reinterpret_cast<void*>(ss.__x[3]),
        reinterpret_cast<void*>(ss.__x[8]), reinterpret_cast<void*>(ss.__x[19]),
        reinterpret_cast<void*>(ss.__x[20]), reinterpret_cast<void*>(ss.__x[21]),
        reinterpret_cast<void*>(ss.__x[22]), reinterpret_cast<void*>(ss.__x[23]),
        reinterpret_cast<void*>(ss.__x[24]), reinterpret_cast<void*>(ss.__x[25]),
        reinterpret_cast<void*>(ss.__x[26]), reinterpret_cast<void*>(ss.__x[27]),
        reinterpret_cast<void*>(ss.__x[28]));
  }
#else
  (void)context;
#endif
  if (const char* trace = std::getenv("AETHERKIRI_TJS_CRASH_TRACE");
      trace && *trace && *trace != '0') {
    try {
      const auto tjs_trace = TJS::TJSGetStackTraceString(32, TJS_W("\n  <-- "));
      if (!tjs_trace.IsEmpty()) {
        spdlog::critical("TJS stack:\n  {}", tjs_trace.AsStdString());
      }
    } catch (...) {
      spdlog::critical("TJS stack: <unavailable>");
    }
  }
  if (const char* trace = std::getenv("AETHERKIRI_EXEC_ARG_TRACE");
      trace && *trace && *trace != '0') {
    if (const char* recent = TJSGetRecentExecArgTrace();
        recent && *recent) {
      spdlog::critical("Recent TJS argument trace:\n{}", recent);
    }
  }

  // Print a mini backtrace where libc provides execinfo.
#if defined(ENGINE_API_HAS_EXECINFO)
  void* frames[32];
  int count = backtrace(frames, 32);
  char** symbols = backtrace_symbols(frames, count);
  if (symbols) {
    for (int i = 0; i < count; ++i) {
      spdlog::critical("  [{}] {}", i, symbols[i]);
    }
    free(symbols);
  }
#endif

  spdlog::default_logger()->flush();
  // Re-raise so the OS generates a proper crash report
  signal(sig, SIG_DFL);
  raise(sig);
}
#endif

void PrintNativeBacktrace(const char* prefix) {
#if defined(ENGINE_API_HAS_EXECINFO)
  void* frames[48];
  int count = backtrace(frames, 48);
  char** symbols = backtrace_symbols(frames, count);
  if (symbols) {
    for (int i = 0; i < count; ++i) {
      spdlog::critical("{} [{}] {}", prefix, i, symbols[i]);
    }
    free(symbols);
  }
#else
  (void)prefix;
#endif
}

void CrashTerminateHandler() {
  try {
    auto ex = std::current_exception();
    if (ex) {
      std::rethrow_exception(ex);
    }
    spdlog::critical("std::terminate called without active exception");
  } catch (const std::exception& e) {
    spdlog::critical("std::terminate called after exception: {}", e.what());
  } catch (...) {
    spdlog::critical("std::terminate called after unknown exception");
  }
  PrintNativeBacktrace("terminate");
  spdlog::default_logger()->flush();
  std::abort();
}

void InstallCrashSignalHandlers() {
  std::set_terminate(CrashTerminateHandler);
#if !defined(_WIN32)
  struct sigaction action {};
  action.sa_sigaction = CrashSignalHandler;
  sigemptyset(&action.sa_mask);
  action.sa_flags = SA_SIGINFO;
  sigaction(SIGSEGV, &action, nullptr);
  sigaction(SIGABRT, &action, nullptr);
  sigaction(SIGBUS, &action, nullptr);
  sigaction(SIGFPE, &action, nullptr);
#endif
}

void EnsureInternalPluginAnchorsLinked() {
#if defined(KRKR_ENABLE_GPU_BRIDGE)
  TVPRegisterKrkrGLESPluginAnchor();
#endif
  TVPRegisterKrkrLive2DPluginAnchor();
  TVPRegisterPSDPluginAnchor();
  TVPRegisterMotionPlayerPluginAnchor();
  TVPRegisterLayerExDrawPluginAnchor();
  TVPRegisterKAGParserExPluginAnchor();
  TVPRegisterScriptsExPluginAnchor();
  TVPRegisterCSVParserPluginAnchor();
  TVPRegisterFstatPluginAnchor();
}

void EnsureRuntimeLoggersInitialized() {
  std::call_once(g_loggers_init_once, []() {
    spdlog::set_level(spdlog::level::info);
    spdlog::flush_on(spdlog::level::warn);
    auto core_logger = EnsureNamedLogger("core");
    auto tjs2_logger = EnsureNamedLogger("tjs2");
    auto plugin_logger = EnsureNamedLogger("plugin");
    g_startup_log_sink = std::make_shared<StartupLogSink>();
    auto attach_sink = [](const std::shared_ptr<spdlog::logger>& logger) {
      if (logger == nullptr || g_startup_log_sink == nullptr) {
        return;
      }
      auto& sinks = logger->sinks();
      const auto already_attached = std::any_of(
          sinks.begin(), sinks.end(),
          [](const std::shared_ptr<spdlog::sinks::sink>& sink) {
            return sink.get() == g_startup_log_sink.get();
          });
      if (!already_attached) {
        sinks.push_back(g_startup_log_sink);
      }
    };
    attach_sink(core_logger);
    attach_sink(tjs2_logger);
    attach_sink(plugin_logger);
    if (core_logger != nullptr) {
      spdlog::set_default_logger(core_logger);
    }
    InstallCrashSignalHandlers();
  });
}

void AttachGameLogFileSink(const std::string& log_file_path) {
  std::lock_guard<std::mutex> sink_guard(g_game_log_file_sink_mutex);

  if (g_game_log_file_sink == nullptr ||
      g_game_log_file_path != log_file_path) {
    if (g_game_log_file_sink != nullptr) {
      for (const char* name : {"core", "tjs2", "plugin"}) {
        if (auto logger = spdlog::get(name)) {
          auto& sinks = logger->sinks();
          sinks.erase(
              std::remove_if(
                  sinks.begin(), sinks.end(),
                  [](const std::shared_ptr<spdlog::sinks::sink>& sink) {
                    return sink.get() == g_game_log_file_sink.get();
                  }),
              sinks.end());
        }
      }
    }
    g_game_log_file_sink =
        std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_file_path, true);
    g_game_log_file_path = log_file_path;
  }

  for (const char* name : {"core", "tjs2", "plugin"}) {
    if (auto logger = spdlog::get(name)) {
      auto& sinks = logger->sinks();
      const auto already_attached = std::any_of(
          sinks.begin(), sinks.end(),
          [](const std::shared_ptr<spdlog::sinks::sink>& sink) {
            return sink.get() == g_game_log_file_sink.get();
          });
      if (!already_attached) {
        sinks.push_back(g_game_log_file_sink);
      }
    }
  }
}

void SetThreadError(const char* message) {
  g_thread_error = (message != nullptr) ? message : "";
}

engine_result_t SetThreadErrorAndReturn(engine_result_t result,
                                        const char* message) {
  SetThreadError(message);
  return result;
}

bool IsHandleLiveLocked(engine_handle_t handle) {
  return g_live_handles.find(handle) != g_live_handles.end();
}

engine_result_t ValidateHandleLocked(engine_handle_t handle,
                                     engine_handle_s** out_impl) {
  if (handle == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "engine handle is null");
  }
  if (!IsHandleLiveLocked(handle)) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "engine handle is invalid or already destroyed");
  }
  *out_impl = reinterpret_cast<engine_handle_s*>(handle);
  return ENGINE_RESULT_OK;
}

void SetHandleErrorLocked(engine_handle_s* impl, const char* message) {
  impl->last_error = (message != nullptr) ? message : "";
}

engine_result_t SetHandleErrorAndReturnLocked(engine_handle_s* impl,
                                              engine_result_t result,
                                              const char* message) {
  SetHandleErrorLocked(impl, message);
  return result;
}

engine_result_t ValidateHandleThreadLocked(engine_handle_s* impl) {
  if (impl->owner_thread != std::this_thread::get_id()) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "engine handle must be used on the thread where engine_create was called");
  }
  return ENGINE_RESULT_OK;
}

void ClearHandleErrorLocked(engine_handle_s* impl) {
  impl->last_error.clear();
}

void PushStartupLog(engine_handle_s* impl, const std::string& message) {
  std::lock_guard<std::mutex> guard(impl->startup.mutex);
  impl->startup.logs.push_back(message);
  while (impl->startup.logs.size() > kMaxStartupLogs) {
    impl->startup.logs.pop_front();
  }
}

void PushRuntimeSpdlogToStartupQueue(const spdlog::details::log_msg& msg) {
  engine_handle_s* target = nullptr;
  {
    // The startup sink can run on decoder/player worker threads.  Never wait
    // for the engine registry from a log sink: engine_tick may own that mutex
    // while synchronously joining the very worker that emitted this message.
    // The regular spdlog sinks still receive the line, so dropping only this
    // best-effort diagnostics copy is preferable to deadlocking shutdown.
    std::unique_lock<std::recursive_mutex> registry_guard(
        g_registry_mutex, std::try_to_lock);
    if (!registry_guard.owns_lock()) {
      return;
    }
    engine_handle_t target_handle = nullptr;
    if (g_runtime_startup_active && g_runtime_startup_owner != nullptr) {
      target_handle = g_runtime_startup_owner;
    } else if (g_runtime_active && g_runtime_owner != nullptr) {
      target_handle = g_runtime_owner;
    }
    if (target_handle == nullptr) {
      return;
    }
    if (!IsHandleLiveLocked(target_handle)) {
      return;
    }
    target = reinterpret_cast<engine_handle_s*>(target_handle);
  }
  if (target == nullptr) {
    return;
  }

  const auto level_sv = spdlog::level::to_string_view(msg.level);
  const std::string level(level_sv.data(), level_sv.size());
  std::string logger(msg.logger_name.data(), msg.logger_name.size());
  if (logger.empty()) {
    logger = "core";
  }
  const std::string payload(msg.payload.data(), msg.payload.size());

  std::string line;
  line.reserve(logger.size() + level.size() + payload.size() + 8);
  line.append("[");
  line.append(logger);
  line.append("] [");
  line.append(level);
  line.append("] ");
  line.append(payload);
  PushStartupLog(target, line);
  const char* diagnostic_level =
      msg.level >= spdlog::level::err ? "error" :
      (msg.level >= spdlog::level::warn ? "warning" : "info");
  bool include_log = msg.level >= spdlog::level::warn;
  {
    std::lock_guard<std::mutex> diagnostic_guard(target->diagnostics.mutex);
    include_log = include_log ||
        target->diagnostics.category_mask == ENGINE_DIAGNOSTIC_CATEGORY_ALL;
  }
  if (include_log) {
    PushDiagnosticEvent(target, "engine", logger.c_str(), diagnostic_level,
                        "log", 0,
                        "{\"message\":\"" + JsonEscape(payload) + "\"}");
  }
}

void SetStartupState(engine_handle_s* impl, uint32_t state) {
  std::lock_guard<std::mutex> guard(impl->startup.mutex);
  impl->startup.state = state;
}

uint32_t GetStartupState(engine_handle_s* impl) {
  std::lock_guard<std::mutex> guard(impl->startup.mutex);
  return impl->startup.state;
}

void ResetStartupState(engine_handle_s* impl) {
  std::lock_guard<std::mutex> guard(impl->startup.mutex);
  impl->startup.state = ENGINE_STARTUP_STATE_IDLE;
  impl->startup.logs.clear();
}

void MarkStartupWorkerRunning(engine_handle_s* impl, bool running) {
  std::lock_guard<std::mutex> guard(impl->startup.mutex);
  impl->startup.worker_running = running;
}

std::thread DetachStartupWorker(engine_handle_s* impl) {
  std::lock_guard<std::mutex> guard(impl->startup.mutex);
  if (!impl->startup.worker.joinable()) {
    return std::thread();
  }
  return std::move(impl->startup.worker);
}

bool EnsureEngineRuntimeInitialized(uint32_t width, uint32_t height,
                                    krkr::AngleBackend backend = krkr::AngleBackend::OpenGLES) {
  if (g_engine_bootstrapped) {
    return true;
  }
  if (!TVPEngineBootstrap::Initialize(width, height, backend)) {
    return false;
  }
  g_engine_bootstrapped = true;
  return true;
}

void StartHostEngineLoop(engine_handle_s* impl) {
  EngineLoop::CreateInstance();
  if (auto* loop = EngineLoop::GetInstance(); loop != nullptr) {
    loop->ResetPointerState();
    loop->Start();
  }
  if (auto* scene = TVPMainScene::GetInstance(); scene != nullptr) {
    scene->scheduleUpdate();
  }
  TVPHostSetSurfaceSize(impl->frame.surface_width, impl->frame.surface_height);
  TVPHostSetPublishRawSourceFrame(impl->render.publish_raw_source_frame);
}

void MarkRuntimeOpenedForHost(engine_handle_t handle,
                              engine_handle_s* impl,
                              const char* startup_log_message,
                              bool mark_startup_succeeded) {
  StartHostEngineLoop(impl);

  bool should_log = startup_log_message != nullptr &&
                    startup_log_message[0] != '\0';
  {
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    if (!IsHandleLiveLocked(handle)) {
      return;
    }

    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    if (impl->state == ToStateValue(EngineState::kDestroyed)) {
      return;
    }

    should_log = should_log &&
                 !(g_runtime_active && g_runtime_owner == handle &&
                   impl->state == ToStateValue(EngineState::kOpened));

    if (g_runtime_startup_active && g_runtime_startup_owner == handle) {
      g_runtime_startup_active = false;
      g_runtime_startup_owner = nullptr;
    }
    g_runtime_active = true;
    g_runtime_owner = handle;
    g_runtime_started_once = true;

    impl->runtime_owner = true;
    impl->input.native_mouse_callbacks_disabled = true;
    impl->frame.width = 0;
    impl->frame.height = 0;
    impl->frame.stride_bytes = 0;
    impl->frame.rgba.clear();
    impl->frame.ready = false;
    impl->input.active_pointer_ids.clear();
    impl->input.pending_events.clear();
    impl->input.primary_click_gate.reset();
    impl->input.coalesced_events = 0;
    impl->state = ToStateValue(EngineState::kOpened);
    ClearHandleErrorLocked(impl);
  }

  if (mark_startup_succeeded) {
    SetStartupState(impl, ENGINE_STARTUP_STATE_SUCCEEDED);
  }
  if (should_log) {
    PushStartupLog(impl, startup_log_message);
  }
}

void ClearRuntimeOwnerIfMatching(engine_handle_t handle, engine_handle_s* impl) {
  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  if (!IsHandleLiveLocked(handle)) {
    return;
  }
  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (g_runtime_active && g_runtime_owner == handle) {
    g_runtime_active = false;
    g_runtime_owner = nullptr;
    g_runtime_started_once = false;
    impl->runtime_owner = false;
    if (impl->state == ToStateValue(EngineState::kOpened)) {
      impl->state = ToStateValue(EngineState::kCreated);
    }
  }
  if (g_runtime_startup_active && g_runtime_startup_owner == handle) {
    g_runtime_startup_active = false;
    g_runtime_startup_owner = nullptr;
  }
}

struct FrameReadbackLayout {
  int32_t read_x = 0;
  int32_t read_y = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t stride_bytes = 0;
};

FrameReadbackLayout GetFrameReadbackLayoutLocked(engine_handle_s* impl) {
  FrameReadbackLayout layout;
  layout.width = impl->frame.surface_width;
  layout.height = impl->frame.surface_height;

  // Read back the full render target instead of GL_VIEWPORT. UpdateDrawBuffer()
  // leaves GL_VIEWPORT set to the game letterbox rectangle, but the host's
  // software path needs a stable full-surface image. Cropping to the viewport
  // makes iOS rotation/input transitions expose stale side content.
#if defined(KRKR_ENABLE_GPU_BRIDGE)
  auto& egl = krkr::GetEngineEGLContext();
  if (egl.IsValid()) {
    uint32_t egl_w = 0;
    uint32_t egl_h = 0;
    if (egl.HasIOSurface()) {
      egl_w = egl.GetIOSurfaceWidth();
      egl_h = egl.GetIOSurfaceHeight();
    } else if (egl.HasNativeWindow()) {
      egl_w = egl.GetNativeWindowWidth();
      egl_h = egl.GetNativeWindowHeight();
    } else {
      egl_w = egl.GetWidth();
      egl_h = egl.GetHeight();
    }
    if (egl_w > 0 && egl_h > 0) {
      layout.read_x = 0;
      layout.read_y = 0;
      layout.width = egl_w;
      layout.height = egl_h;
    }
  }
#endif

  if (layout.width == 0) {
    layout.width = 1;
  }
  if (layout.height == 0) {
    layout.height = 1;
  }
  layout.stride_bytes = layout.width * 4u;
  return layout;
}

bool ReadCurrentFrameRgba(const FrameReadbackLayout& layout, void* out_pixels) {
  if (layout.width == 0 || layout.height == 0 || out_pixels == nullptr) {
    return false;
  }

#if defined(KRKR_ENABLE_GPU_BRIDGE)
  glFinish();
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glReadPixels(static_cast<GLint>(layout.read_x),
               static_cast<GLint>(layout.read_y),
               static_cast<GLsizei>(layout.width),
               static_cast<GLsizei>(layout.height), GL_RGBA,
               GL_UNSIGNED_BYTE, out_pixels);
  const GLenum read_pixels_error = glGetError();

  if (read_pixels_error != GL_NO_ERROR) {
    return false;
  }

  auto* bytes = static_cast<uint8_t*>(out_pixels);
  const size_t row_bytes = static_cast<size_t>(layout.stride_bytes);
  std::vector<uint8_t> row_buffer(row_bytes);
  const uint32_t half_rows = layout.height / 2u;
  for (uint32_t y = 0; y < half_rows; ++y) {
    uint8_t* row_top = bytes + static_cast<size_t>(y) * row_bytes;
    uint8_t* row_bottom =
        bytes + static_cast<size_t>(layout.height - 1u - y) * row_bytes;
    std::memcpy(row_buffer.data(), row_top, row_bytes);
    std::memcpy(row_top, row_bottom, row_bytes);
    std::memcpy(row_bottom, row_buffer.data(), row_bytes);
  }

  static int readback_log_count = 0;
  if (readback_log_count < 8) {
    uint32_t min_x = layout.width;
    uint32_t min_y = layout.height;
    uint32_t max_x = 0;
    uint32_t max_y = 0;
    uint64_t non_black = 0;
    uint64_t alpha_nonzero = 0;
    uint64_t r_sum = 0;
    uint64_t g_sum = 0;
    uint64_t b_sum = 0;
    for (uint32_t y = 0; y < layout.height; ++y) {
      const uint8_t* row = bytes + static_cast<size_t>(y) * row_bytes;
      for (uint32_t x = 0; x < layout.width; ++x) {
        const uint8_t* px = row + static_cast<size_t>(x) * 4u;
        r_sum += px[0];
        g_sum += px[1];
        b_sum += px[2];
        if (px[3] != 0) {
          alpha_nonzero += 1;
        }
        if (px[0] != 0 || px[1] != 0 || px[2] != 0) {
          non_black += 1;
          min_x = std::min(min_x, x);
          min_y = std::min(min_y, y);
          max_x = std::max(max_x, x);
          max_y = std::max(max_y, y);
        }
      }
    }
    if (non_black == 0) {
      spdlog::info("engine_readback: {}x{} at {},{} all black alpha_nonzero={}",
                   layout.width, layout.height, layout.read_x, layout.read_y,
                   alpha_nonzero);
    } else {
      spdlog::info(
          "engine_readback: {}x{} at {},{} non_black={} bbox=({},{} {}x{}) "
          "rgb_sum=({},{},{}) alpha_nonzero={}",
          layout.width, layout.height, layout.read_x, layout.read_y,
          non_black, min_x, min_y, max_x - min_x + 1, max_y - min_y + 1,
          r_sum, g_sum, b_sum, alpha_nonzero);
    }
    readback_log_count += 1;
  }

  return true;
#else
  (void)layout;
  (void)out_pixels;
  return false;
#endif
}

bool CopyHostFrameLocked(engine_handle_s* impl) {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t stride = 0;
  uint64_t serial = 0;
  if (!TVPHostGetLatestFrameDesc(&width, &height, &stride, &serial) ||
      width == 0 || height == 0 || stride == 0) {
    return false;
  }

  const size_t required_size =
      static_cast<size_t>(stride) * static_cast<size_t>(height);
  if (required_size == 0) {
    return false;
  }

  impl->frame.rgba.resize(required_size);
  if (!TVPHostCopyLatestFrameRGBA(impl->frame.rgba.data(), required_size,
                                  &width, &height, &stride, &serial)) {
    return false;
  }

  impl->frame.width = width;
  impl->frame.height = height;
  impl->frame.stride_bytes = stride;
  impl->frame.serial = serial;
  impl->frame.ready = true;
  return true;
}

bool CaptureGodotNativeGpuFrameLocked(engine_handle_s* impl) {
  if (impl == nullptr ||
      (impl->render.renderer != ENGINE_RENDERER_GODOT_NATIVE &&
       impl->render.renderer != ENGINE_RENDERER_GPU_BRIDGE)) {
    return false;
  }
  uint64_t texture = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint64_t serial = 0;
  if (!TVPHostGetLatestGodotGpuFrame(&texture, &width, &height, &serial) ||
      texture == 0 || width == 0 || height == 0) {
    return false;
  }
  impl->frame.rgba.clear();
  impl->frame.width = width;
  impl->frame.height = height;
  impl->frame.stride_bytes = width * 4u;
  impl->frame.serial = serial;
  impl->frame.ready = true;
  return true;
}

bool IsFinitePointerValue(double value) {
  return std::isfinite(value);
}

void AppendEscapedJsonString(std::string& out, const std::string& value) {
  out.push_back('"');
  for (const unsigned char c : value) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (c < 0x20u) {
          char escaped[7];
          std::snprintf(escaped, sizeof(escaped), "\\u%04x",
                        static_cast<unsigned int>(c));
          out += escaped;
        } else {
          out.push_back(static_cast<char>(c));
        }
        break;
    }
  }
  out.push_back('"');
}

void AppendEscapedJsonString(std::string& out, const ttstr& value) {
  AppendEscapedJsonString(out, value.AsStdString());
}

void AppendEscapedJsonString(std::string& out, const char* value) {
  if (value == nullptr) {
    AppendEscapedJsonString(out, std::string());
    return;
  }
  AppendEscapedJsonString(out, std::string(value));
}

bool TryGetProperty(iTJSDispatch2* object, const tjs_char* name,
                    tTJSVariant* out_value) {
  return object != nullptr &&
         TJS_SUCCEEDED(object->PropGet(0, name, nullptr, out_value, object));
}

bool TryGetBoolProperty(iTJSDispatch2* object, const tjs_char* name,
                        bool default_value) {
  tTJSVariant value;
  if (!TryGetProperty(object, name, &value) || value.Type() == tvtVoid) {
    return default_value;
  }
  return static_cast<bool>(value);
}

ttstr GetStringProperty(iTJSDispatch2* object, const tjs_char* name) {
  tTJSVariant value;
  if (!TryGetProperty(object, name, &value) || value.Type() == tvtVoid) {
    return ttstr();
  }
  return ttstr(value);
}

bool TryGetMainMenuObject(tTJSVariant* out_menu_variant) {
  if (out_menu_variant == nullptr) {
    return false;
  }

  const tjs_int window_count = TVPGetWindowCount();
  for (tjs_int i = 0; i < window_count; ++i) {
    tTJSNI_Window* window = TVPGetWindowListAt(i);
    if (window == nullptr) {
      continue;
    }

    iTJSDispatch2* window_dispatch = window->GetWindowDispatch();
    if (window_dispatch == nullptr) {
      continue;
    }

    const bool ok =
        TJS_SUCCEEDED(window_dispatch->PropGet(0, TJS_W("menu"), nullptr,
                                              out_menu_variant, window_dispatch));
    window_dispatch->Release();
    if (ok && out_menu_variant->Type() == tvtObject &&
        out_menu_variant->AsObjectNoAddRef() != nullptr) {
      return true;
    }
  }

  return false;
}

void AppendMenuJsonNode(iTJSDispatch2* item, const std::string& path,
                        std::string& out) {
  out += "{\"path\":";
  AppendEscapedJsonString(out, path);
  out += ",\"caption\":";
  AppendEscapedJsonString(out, GetStringProperty(item, TJS_W("caption")));
  out += ",\"enabled\":";
  out += TryGetBoolProperty(item, TJS_W("enabled"), true) ? "true" : "false";
  out += ",\"visible\":";
  out += TryGetBoolProperty(item, TJS_W("visible"), true) ? "true" : "false";
  out += ",\"checked\":";
  out += TryGetBoolProperty(item, TJS_W("checked"), false) ? "true" : "false";
  out += ",\"radio\":";
  out += TryGetBoolProperty(item, TJS_W("radio"), false) ? "true" : "false";
  out += ",\"children\":";

  tTJSVariant children_variant;
  if (!TryGetProperty(item, TJS_W("children"), &children_variant) ||
      children_variant.Type() != tvtObject ||
      children_variant.AsObjectNoAddRef() == nullptr) {
    out += "[]";
    out.push_back('}');
    return;
  }

  iTJSDispatch2* children = children_variant.AsObjectNoAddRef();
  out.push_back('[');
  bool first = true;
  for (tjs_int index = 0;; ++index) {
    tTJSVariant child_variant;
    if (TJS_FAILED(children->PropGetByNum(0, index, &child_variant, children)) ||
        child_variant.Type() == tvtVoid) {
      break;
    }
    iTJSDispatch2* child = child_variant.AsObjectNoAddRef();
    if (child == nullptr) {
      continue;
    }
    if (!first) {
      out.push_back(',');
    }
    first = false;
    const std::string child_path =
        path.empty() ? std::to_string(index) : path + "/" + std::to_string(index);
    AppendMenuJsonNode(child, child_path, out);
  }
  out += "]}";
}

std::string BuildMainMenuJson() {
  tTJSVariant root_menu_variant;
  if (!TryGetMainMenuObject(&root_menu_variant)) {
    return "[]";
  }

  iTJSDispatch2* root_menu = root_menu_variant.AsObjectNoAddRef();
  if (root_menu == nullptr) {
    return "[]";
  }

  tTJSVariant children_variant;
  if (!TryGetProperty(root_menu, TJS_W("children"), &children_variant) ||
      children_variant.Type() != tvtObject ||
      children_variant.AsObjectNoAddRef() == nullptr) {
    return "[]";
  }

  iTJSDispatch2* children = children_variant.AsObjectNoAddRef();
  std::string out = "[";
  bool first = true;
  for (tjs_int index = 0;; ++index) {
    tTJSVariant child_variant;
    if (TJS_FAILED(children->PropGetByNum(0, index, &child_variant, children)) ||
        child_variant.Type() == tvtVoid) {
      break;
    }
    iTJSDispatch2* child = child_variant.AsObjectNoAddRef();
    if (child == nullptr) {
      continue;
    }
    if (!first) {
      out.push_back(',');
    }
    first = false;
    AppendMenuJsonNode(child, std::to_string(index), out);
  }
  out.push_back(']');
  return out;
}

bool ParseMenuPath(const char* path_utf8, std::vector<tjs_int>* out_segments) {
  if (path_utf8 == nullptr || out_segments == nullptr) {
    return false;
  }

  std::string path(path_utf8);
  if (path.empty()) {
    return false;
  }

  size_t start = 0;
  while (start < path.size()) {
    const size_t slash = path.find('/', start);
    const std::string part = path.substr(
        start, slash == std::string::npos ? std::string::npos : slash - start);
    if (part.empty()) {
      return false;
    }
    char* end_ptr = nullptr;
    const long index = std::strtol(part.c_str(), &end_ptr, 10);
    if (end_ptr == nullptr || *end_ptr != '\0' || index < 0) {
      return false;
    }
    out_segments->push_back(static_cast<tjs_int>(index));
    if (slash == std::string::npos) {
      break;
    }
    start = slash + 1;
  }

  return !out_segments->empty();
}

bool ResolveMenuItemByPath(const std::vector<tjs_int>& path_segments,
                           tTJSVariant* out_item_variant) {
  if (out_item_variant == nullptr || path_segments.empty()) {
    return false;
  }

  tTJSVariant current_variant;
  if (!TryGetMainMenuObject(&current_variant) ||
      current_variant.Type() != tvtObject ||
      current_variant.AsObjectNoAddRef() == nullptr) {
    return false;
  }

  for (const tjs_int segment : path_segments) {
    iTJSDispatch2* current = current_variant.AsObjectNoAddRef();
    if (current == nullptr) {
      return false;
    }

    tTJSVariant children_variant;
    if (!TryGetProperty(current, TJS_W("children"), &children_variant) ||
        children_variant.Type() != tvtObject ||
        children_variant.AsObjectNoAddRef() == nullptr) {
      return false;
    }

    iTJSDispatch2* children = children_variant.AsObjectNoAddRef();
    tTJSVariant next_variant;
    if (TJS_FAILED(children->PropGetByNum(0, segment, &next_variant, children)) ||
        next_variant.Type() != tvtObject ||
        next_variant.AsObjectNoAddRef() == nullptr) {
      return false;
    }

    current_variant = next_variant;
  }

  *out_item_variant = current_variant;
  return true;
}

engine_result_t CopyUtf8StringToBuffer(const std::string& text, char* out_buffer,
                                       uint32_t buffer_size,
                                       uint32_t* out_bytes_written) {
  if (out_bytes_written == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_bytes_written is null");
  }
  if (out_buffer == nullptr || buffer_size == 0) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_buffer is null or buffer_size is 0");
  }

  const uint32_t copy_bytes = static_cast<uint32_t>(
      std::min<size_t>(text.size(), static_cast<size_t>(buffer_size - 1)));
  if (copy_bytes > 0) {
    std::memcpy(out_buffer, text.data(), copy_bytes);
  }
  out_buffer[copy_bytes] = '\0';
  *out_bytes_written = copy_bytes;
  return ENGINE_RESULT_OK;
}

engine_result_t DispatchInputEventNow(engine_handle_s* impl,
                                      const engine_input_event_t& event,
                                      const char** out_error_message) {
  auto* loop = EngineLoop::GetInstance();
  if (loop == nullptr) {
    if (out_error_message != nullptr) {
      *out_error_message = "engine loop is unavailable";
    }
    return ENGINE_RESULT_INVALID_STATE;
  }

  // Convert engine_input_event_t → EngineInputEvent (bridge → core)
  EngineInputEvent core_event;
  core_event.type = event.type;
  core_event.x = event.x;
  core_event.y = event.y;
  core_event.delta_x = event.delta_x;
  core_event.delta_y = event.delta_y;
  core_event.pointer_id = event.pointer_id;
  core_event.button = event.button;
  core_event.key_code = event.key_code;
  core_event.modifiers = event.modifiers;
  core_event.unicode_codepoint = event.unicode_codepoint;

  if (!loop->HandleInputEvent(core_event)) {
    if (out_error_message != nullptr) {
      *out_error_message = "input event dispatch failed (no active window?)";
    }
    return ENGINE_RESULT_INVALID_STATE;
  }

  if (out_error_message != nullptr) {
    *out_error_message = nullptr;
  }
  return ENGINE_RESULT_OK;
}

engine_result_t OpenGameCore(engine_handle_t handle,
                             engine_handle_s* impl,
                             const char* game_root_path_utf8) {
  if (game_root_path_utf8 == nullptr || game_root_path_utf8[0] == '\0') {
    return ENGINE_RESULT_INVALID_ARGUMENT;
  }

  if (!EnsureEngineRuntimeInitialized(impl->frame.surface_width,
                                      impl->frame.surface_height,
                                      impl->render.angle_backend)) {
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    SetHandleErrorLocked(impl, "failed to initialize engine runtime for host mode");
    return ENGINE_RESULT_INTERNAL_ERROR;
  }

  EnsureRuntimeLoggersInitialized();
  EnsureInternalPluginAnchorsLinked();
  // Cache options set via engine_set_option() are already stored in
  // TVPEarlySetOptions and will be merged during TVPSystemInit().
  // Do NOT call TVPGetCommandLine() here — it triggers
  // TVPInitProgramArgumentsAndDataPath() which caches TVPGetAppPath()
  // as empty (TVPProjectDir is not set yet), corrupting path resolution.

  std::string normalized_game_root_path(game_root_path_utf8);
  // Only append trailing slash for directory paths.  Archive files
  // (.xp3, .zip, etc.) must keep their original extension so the
  // storage system can recognise them as archives.
  auto ends_with = [](const std::string &s, const std::string &suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
  };
  std::string lower_game_root_path = normalized_game_root_path;
  std::transform(lower_game_root_path.begin(),
                 lower_game_root_path.end(),
                 lower_game_root_path.begin(),
                 [](unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });
  const bool looks_like_archive =
      ends_with(lower_game_root_path, ".xp3") ||
      ends_with(lower_game_root_path, ".exe") ||
      ends_with(lower_game_root_path, ".zip") ||
      ends_with(lower_game_root_path, ".7z")  ||
      ends_with(lower_game_root_path, ".tar");
  if (!looks_like_archive &&
      !normalized_game_root_path.empty() &&
      normalized_game_root_path.back() != '/' &&
      normalized_game_root_path.back() != '\\') {
    normalized_game_root_path.push_back('/');
  }

  std::string sidecar_root_path = normalized_game_root_path;
  if (looks_like_archive) {
    const auto separator = sidecar_root_path.find_last_of("/\\");
    sidecar_root_path = separator == std::string::npos
        ? "."
        : sidecar_root_path.substr(0, separator);
  }

  spdlog::info(
      "engine_open_game: runtime initialized, starting application with path: {} (normalized: {})",
      game_root_path_utf8, normalized_game_root_path);
#if defined(__ANDROID__)
  AndroidInfoLog("engine_open_game: input='%s' normalized='%s'",
                 game_root_path_utf8, normalized_game_root_path.c_str());
#endif

  try {
#if defined(__EMSCRIPTEN__)
    std::string log_file_path = TVPGetDefaultFileDir();
#else
    std::string log_file_path = sidecar_root_path;
#endif
    if (!log_file_path.empty() && log_file_path.back() != '/') {
      log_file_path += "/";
    }
    log_file_path += "krkr2.log";
    AttachGameLogFileSink(log_file_path);
    spdlog::info("engine_open_game: File logger successfully attached to {}", log_file_path);
  } catch (const std::exception& e) {
    spdlog::error("engine_open_game: Failed to create log file: {}", e.what());
  }
  spdlog::default_logger()->flush();

  // Remember the path even while tracing is disabled so the in-app debug
  // console can enable a bounded trace after the game has already started.
  {
#if defined(__EMSCRIPTEN__)
    std::string trace_path = TVPGetDefaultFileDir();
#else
    std::string trace_path = sidecar_root_path;
#endif
    if (!trace_path.empty() && trace_path.back() != '/') trace_path += "/";
    trace_path += "plugin_trace.log";
    PluginCallTracer::Instance().SetLogFilePath(trace_path);
    PluginCallTracer::Instance().ResetDebugStats();
  }

  const bool prefer_gpu_after_startup =
      ShouldUseGodotGpuFrameForRenderer(impl->render.renderer);
  TVPSetGodotRenderManagerGpuFastPathEnabled(false);
  TVPHostSetPreferGpuFrame(false);
  auto restore_startup_gpu_path = [&]() {
    TVPSetGodotRenderManagerGpuFastPathEnabled(prefer_gpu_after_startup);
    TVPHostSetPreferGpuFrame(prefer_gpu_after_startup);
  };

  try {
    // A previous title may have ended through System.exit while its Window
    // and activation callbacks were still retained by script-side cycles.
    // Start every embedded session from a process-neutral application state.
    Application->ResetForHostSession();
    spdlog::debug("engine_open_game: calling Application->StartApplication...");
#if defined(__ANDROID__)
    AndroidInfoLog("engine_open_game: calling StartApplication('%s')",
                   normalized_game_root_path.c_str());
#endif
    spdlog::default_logger()->flush();
    Application->StartApplication(ttstr(normalized_game_root_path.c_str()));
    spdlog::info("engine_open_game: StartApplication returned successfully");
#if defined(__EMSCRIPTEN__)
    TVPHostActivateMainWindow();
#endif
#if defined(__ANDROID__)
    AndroidInfoLog("engine_open_game: StartApplication returned successfully");
#endif
  } catch (const std::exception& e) {
    restore_startup_gpu_path();
    spdlog::error("engine_open_game: StartApplication threw std::exception: {}",
                  e.what());
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    SetHandleErrorLocked(impl, "StartApplication threw an exception");
    return ENGINE_RESULT_INTERNAL_ERROR;
  } catch (...) {
    restore_startup_gpu_path();
    spdlog::error("engine_open_game: StartApplication threw unknown exception");
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    SetHandleErrorLocked(impl, "StartApplication threw an exception");
    return ENGINE_RESULT_INTERNAL_ERROR;
  }
  restore_startup_gpu_path();

  if (TVPTerminated) {
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    SetHandleErrorLocked(impl, "runtime requested termination during startup");
    return ENGINE_RESULT_INVALID_STATE;
  }

  MarkRuntimeOpenedForHost(handle, impl, nullptr, false);
  return ENGINE_RESULT_OK;
}

void RunOpenGameAsync(engine_handle_t handle,
                      engine_handle_s* impl,
                      std::string game_root_path_utf8) {
  PushStartupLog(impl, "engine_open_game_async: worker started");
  TVPTerminated = false;
  TVPTerminateCode = 0;
  TVPSystemUninitCalled = false;
  TVPTerminateOnWindowClose = false;
  TVPTerminateOnNoWindowStartup = false;
  TVPHostSuppressProcessExit = true;

  const engine_result_t open_result =
      OpenGameCore(handle, impl, game_root_path_utf8.c_str());

  // Startup runs on a worker thread; release current GL context here so the
  // owner thread can safely make it current before ticking/rendering.
#if defined(KRKR_ENABLE_GPU_BRIDGE)
  auto& egl = krkr::GetEngineEGLContext();
  if (egl.IsValid()) {
    egl.ReleaseCurrent();
  }
#endif

  if (open_result == ENGINE_RESULT_OK) {
    PushStartupLog(impl, "engine_open_game => OK");
    SetStartupState(impl, ENGINE_STARTUP_STATE_SUCCEEDED);
  } else {
    std::string error_text;
    {
      std::lock_guard<std::recursive_mutex> guard(impl->mutex);
      error_text = impl->last_error;
    }
    if (error_text.empty()) {
      error_text = "unknown startup error";
    }
    PushStartupLog(impl, "ERROR: " + error_text);
    ClearRuntimeOwnerIfMatching(handle, impl);
    SetStartupState(impl, ENGINE_STARTUP_STATE_FAILED);
  }
  MarkStartupWorkerRunning(impl, false);
}

}  // namespace

extern std::string TVPEngineApi_GetGlobalException();
extern void TVPEngineApi_SetGlobalException(const std::string& msg);

extern "C" {

void engine_register_godot_gpu_bridge(const void* callbacks) {
  TVPGodotGpuBridgeRegister(
      static_cast<const TVPGodotGpuBridgeCallbacks*>(callbacks));
}

void engine_register_godot_gpu_batch_bridge(const void* callbacks) {
  TVPGodotGpuBatchRegister(
      static_cast<const TVPGodotGpuBatchCallbacks*>(callbacks));
}

void engine_register_godot_gpu_external_texture_bridge(
    const void* callbacks) {
  TVPGodotGpuExternalTextureRegister(
      static_cast<const TVPGodotGpuExternalTextureCallbacks*>(callbacks));
}

void TVPEngineApiNotifyWebStartupReady() {
#if defined(__EMSCRIPTEN__)
  engine_handle_t handle = nullptr;
  engine_handle_s* impl = nullptr;
  {
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    if (!g_runtime_startup_active || g_runtime_startup_owner == nullptr) {
      return;
    }
    if (!IsHandleLiveLocked(g_runtime_startup_owner)) {
      return;
    }
    handle = g_runtime_startup_owner;
    impl = reinterpret_cast<engine_handle_s*>(handle);
  }
  if (impl == nullptr) {
    return;
  }
  spdlog::info("engine_open_game_async: Web HostWindowLayer is available; waiting for startup script to finish");
  PushStartupLog(
      impl,
      "engine_open_game_async: Web HostWindowLayer is available; waiting for startup script to finish");
#endif
}

engine_result_t engine_get_runtime_api_version(uint32_t* out_api_version) {
  if (out_api_version == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_api_version is null");
  }
  *out_api_version = ENGINE_API_VERSION;
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_create(const engine_create_desc_t* desc,
                              engine_handle_t* out_handle) {
  if (desc == nullptr || out_handle == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "engine_create requires non-null desc and out_handle");
  }

  if (desc->struct_size < sizeof(engine_create_desc_t)) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "engine_create_desc_t.struct_size is too small");
  }

  const uint32_t expected_major = (ENGINE_API_VERSION >> 24u) & 0xFFu;
  const uint32_t caller_major = (desc->api_version >> 24u) & 0xFFu;
  if (caller_major != expected_major) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_NOT_SUPPORTED,
                                   "unsupported engine API major version");
  }

  EnsureRuntimeLoggersInitialized();
  EnsureInternalPluginAnchorsLinked();
  TVPHostSuppressProcessExit = true;

  auto* impl = new (std::nothrow) engine_handle_s();
  if (impl == nullptr) {
    *out_handle = nullptr;
    return SetThreadErrorAndReturn(ENGINE_RESULT_INTERNAL_ERROR,
                                   "failed to allocate engine handle");
  }

  impl->state = ToStateValue(EngineState::kCreated);
  impl->owner_thread = std::this_thread::get_id();
  impl->runtime_owner = false;

  auto handle = reinterpret_cast<engine_handle_t>(impl);
  {
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    g_live_handles.insert(handle);
  }

  *out_handle = handle;
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_destroy(engine_handle_t handle) {
  if (handle == nullptr) {
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }

  engine_handle_s* impl = nullptr;
  bool owned_runtime = false;
  bool needs_session_cleanup = false;
  std::thread startup_worker;

  {
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    auto result = ValidateHandleLocked(handle, &impl);
    if (result != ENGINE_RESULT_OK) {
      return result;
    }

    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    result = ValidateHandleThreadLocked(impl);
    if (result != ENGINE_RESULT_OK) {
      return result;
    }

    owned_runtime = (g_runtime_active && g_runtime_owner == handle);
    const uint32_t startup_state = GetStartupState(impl);
    needs_session_cleanup =
        owned_runtime ||
        (g_runtime_startup_active && g_runtime_startup_owner == handle) ||
        startup_state == ENGINE_STARTUP_STATE_RUNNING ||
        startup_state == ENGINE_STARTUP_STATE_SUCCEEDED ||
        startup_state == ENGINE_STARTUP_STATE_FAILED;
    if (owned_runtime) {
      g_runtime_active = false;
      g_runtime_owner = nullptr;
      impl->runtime_owner = false;
    }
    if (g_runtime_startup_active && g_runtime_startup_owner == handle) {
      g_runtime_startup_active = false;
      g_runtime_startup_owner = nullptr;
    }
    startup_worker = DetachStartupWorker(impl);
    SetStartupState(impl, ENGINE_STARTUP_STATE_IDLE);

    impl->state = ToStateValue(EngineState::kDestroyed);
    ClearHandleErrorLocked(impl);
    g_live_handles.erase(handle);
  }

  if (startup_worker.joinable()) {
    startup_worker.join();
  }

  if (needs_session_cleanup) {
    if (auto* loop = EngineLoop::GetInstance(); loop != nullptr) {
      loop->ResetPointerState();
    }
    try {
      Application->OnDeactivate();
    } catch (...) {
    }
    try {
      TVPShutdownSoundForHost();
    } catch (const std::exception& e) {
      spdlog::warn("engine_destroy: TVPShutdownSoundForHost ignored exception: {}", e.what());
    } catch (...) {
      spdlog::warn("engine_destroy: TVPShutdownSoundForHost ignored unknown exception");
    }
    try {
      Application->StopImageLoadThread();
    } catch (const std::exception& e) {
      spdlog::warn("engine_destroy: StopImageLoadThread ignored exception: {}", e.what());
    } catch (...) {
      spdlog::warn("engine_destroy: StopImageLoadThread ignored unknown exception");
    }
    try {
      Application->FilterUserMessage(
          [](std::deque<std::tuple<void*, int, tTVPApplication::tMsg>>& queue) {
            queue.clear();
          });
    } catch (const std::exception& e) {
      spdlog::warn("engine_destroy: FilterUserMessage ignored exception: {}", e.what());
    } catch (...) {
      spdlog::warn("engine_destroy: FilterUserMessage ignored unknown exception");
    }
    try {
      // Compatibility render caches retain TJS variants and native Layer
      // addresses. Release them while the old script world is still valid,
      // then clear the generic Layer routing tables before another title can
      // reuse those addresses.
      //
      // Timer and continuous-event producer threads must be joined first.
      // Legacy AtExit handlers run only once in an embedded process, so they
      // cannot protect the second and later End Game -> open title cycles.
      TVPResetTimerForHostSession();
      TVPResetEventPlatformForHostSession();
      AetherKiriMotionResetForGameSession();
      TVPResetLayerStateForHostSession();
      TVPResetEventsForHostSession();
      // Let TJS release and invalidate its own Window graph. Forcing owner
      // invalidation before script-engine shutdown can recursively finalize a
      // multi-window title and crash in tTJSCustomObject::Finalize.
      Application->OnExit();
      // tTJS script blocks can leave intermediate-code contexts in the
      // process-wide orphan registry while their final Release is deferred.
      // Never let those contexts (and their captured old-world variants)
      // survive until a compact event in the next title.
      TJS_CollectOrphanedICCs(true);
      TVPResetEventsForHostSession();
      TVPResetWindowRegistryForHostSession();
      TVPClearGraphicCache();
      TVPClearScnearioCache();
      TVPClearArchiveCache();
      TVPHostResetForGameSession();
      Application->ResetForHostSession();
      TVPResetAutoPathsForGameSession();
      TVPResetSystemInitStateForHostSession();
    } catch (const std::exception& e) {
      spdlog::warn("engine_destroy: session cleanup ignored exception: {}", e.what());
    } catch (...) {
      spdlog::warn("engine_destroy: session cleanup ignored unknown exception");
    }

    // Avoid triggering platform exit() path in the host process.
    TVPTerminated = false;
    TVPTerminateCode = 0;
    TVPSystemUninitCalled = false;
    TVPEngineApi_SetGlobalException("");
    g_runtime_started_once = false;
  }

  delete impl;
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_open_game(engine_handle_t handle,
                                 const char* game_root_path_utf8,
                                 const char* startup_script_utf8) {
  (void)startup_script_utf8;

  if (game_root_path_utf8 == nullptr || game_root_path_utf8[0] == '\0') {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "game_root_path_utf8 is null or empty");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  if (impl->state == ToStateValue(EngineState::kDestroyed)) {
    return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                         "engine is already destroyed");
  }
  if (g_runtime_startup_active && g_runtime_startup_owner == handle) {
    return SetHandleErrorAndReturnLocked(
        impl, ENGINE_RESULT_INVALID_STATE, "engine startup is already running");
  }

  if (g_runtime_active) {
    if (g_runtime_owner != handle) {
      return SetHandleErrorAndReturnLocked(
          impl,
          ENGINE_RESULT_INVALID_STATE,
          "runtime is already active on another engine handle");
    }

    impl->state = ToStateValue(EngineState::kOpened);
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }

  if (g_runtime_started_once) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_NOT_SUPPORTED,
        "runtime restart is not supported yet; restart process to open another game");
  }
  TVPTerminated = false;
  TVPTerminateCode = 0;
  TVPSystemUninitCalled = false;
  TVPTerminateOnWindowClose = false;
  TVPTerminateOnNoWindowStartup = false;
  TVPHostSuppressProcessExit = true;
  g_runtime_startup_active = true;
  g_runtime_startup_owner = handle;
  ResetStartupState(impl);
  SetStartupState(impl, ENGINE_STARTUP_STATE_RUNNING);
  PushStartupLog(impl, "engine_open_game: starting");
  ClearHandleErrorLocked(impl);

  const engine_result_t open_result =
      OpenGameCore(handle, impl, game_root_path_utf8);
  if (open_result == ENGINE_RESULT_OK) {
    SetStartupState(impl, ENGINE_STARTUP_STATE_SUCCEEDED);
    PushStartupLog(impl, "engine_open_game => OK");
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }

  if (open_result == ENGINE_RESULT_INTERNAL_ERROR) {
    SetHandleErrorLocked(impl, "StartApplication threw an exception");
  } else if (open_result == ENGINE_RESULT_INVALID_STATE && TVPTerminated) {
    SetHandleErrorLocked(impl, "runtime requested termination during startup");
  } else {
    SetHandleErrorLocked(impl, "engine_open_game failed");
  }
  ClearRuntimeOwnerIfMatching(handle, impl);
  SetStartupState(impl, ENGINE_STARTUP_STATE_FAILED);
  PushStartupLog(impl, std::string("ERROR: ") + impl->last_error);
  return open_result;
}

engine_result_t engine_open_game_async(engine_handle_t handle,
                                       const char* game_root_path_utf8,
                                       const char* startup_script_utf8) {
  (void)startup_script_utf8;

  if (game_root_path_utf8 == nullptr || game_root_path_utf8[0] == '\0') {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "game_root_path_utf8 is null or empty");
  }

  std::thread stale_worker;
  std::string game_root_copy(game_root_path_utf8);
  engine_handle_s* impl = nullptr;
  {
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    auto result = ValidateHandleLocked(handle, &impl);
    if (result != ENGINE_RESULT_OK) {
      return result;
    }

    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    result = ValidateHandleThreadLocked(impl);
    if (result != ENGINE_RESULT_OK) {
      return result;
    }

    if (impl->state == ToStateValue(EngineState::kDestroyed)) {
      return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                           "engine is already destroyed");
    }
    if (g_runtime_startup_active && g_runtime_startup_owner == handle) {
      return SetHandleErrorAndReturnLocked(
          impl, ENGINE_RESULT_INVALID_STATE, "engine startup is already running");
    }
    if (g_runtime_active && g_runtime_owner == handle) {
      SetStartupState(impl, ENGINE_STARTUP_STATE_SUCCEEDED);
      ClearHandleErrorLocked(impl);
      SetThreadError(nullptr);
      return ENGINE_RESULT_OK;
    }
    if (g_runtime_active && g_runtime_owner != handle) {
      return SetHandleErrorAndReturnLocked(
          impl,
          ENGINE_RESULT_INVALID_STATE,
          "runtime is already active on another engine handle");
    }
    if (g_runtime_started_once) {
      return SetHandleErrorAndReturnLocked(
          impl,
          ENGINE_RESULT_NOT_SUPPORTED,
          "runtime restart is not supported yet; restart process to open another game");
    }

    stale_worker = DetachStartupWorker(impl);
    ResetStartupState(impl);
    SetStartupState(impl, ENGINE_STARTUP_STATE_RUNNING);
    PushStartupLog(impl, "engine_open_game_async: queued startup");
    MarkStartupWorkerRunning(impl, true);
    g_runtime_startup_active = true;
    g_runtime_startup_owner = handle;

    try {
      impl->startup.worker =
          std::thread([handle, impl, game_root_copy]() mutable {
            try {
              RunOpenGameAsync(handle, impl, std::move(game_root_copy));
            } catch (const std::exception& e) {
              spdlog::critical("engine_open_game_async: startup thread exception: {}", e.what());
              PushStartupLog(impl, std::string("ERROR: startup thread exception: ") + e.what());
              ClearRuntimeOwnerIfMatching(handle, impl);
              SetStartupState(impl, ENGINE_STARTUP_STATE_FAILED);
              MarkStartupWorkerRunning(impl, false);
            } catch (...) {
              spdlog::critical("engine_open_game_async: startup thread unknown exception");
              PushStartupLog(impl, "ERROR: startup thread unknown exception");
              ClearRuntimeOwnerIfMatching(handle, impl);
              SetStartupState(impl, ENGINE_STARTUP_STATE_FAILED);
              MarkStartupWorkerRunning(impl, false);
            }
          });
    } catch (...) {
      MarkStartupWorkerRunning(impl, false);
      SetStartupState(impl, ENGINE_STARTUP_STATE_FAILED);
      g_runtime_startup_active = false;
      g_runtime_startup_owner = nullptr;
      return SetHandleErrorAndReturnLocked(
          impl, ENGINE_RESULT_INTERNAL_ERROR, "failed to create startup thread");
    }

    ClearHandleErrorLocked(impl);
  }
  if (stale_worker.joinable()) {
    stale_worker.join();
  }
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_get_startup_state(engine_handle_t handle,
                                         uint32_t* out_state) {
  if (out_state == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_state is null");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }
  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  *out_state = GetStartupState(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_drain_startup_logs(engine_handle_t handle,
                                          char* out_buffer,
                                          uint32_t buffer_size,
                                          uint32_t* out_bytes_written) {
  if (out_bytes_written == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_bytes_written is null");
  }
  if (out_buffer == nullptr || buffer_size == 0) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_buffer is null or buffer_size is 0");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }
  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  uint32_t written = 0;
  {
    std::lock_guard<std::mutex> startup_guard(impl->startup.mutex);
    while (!impl->startup.logs.empty()) {
      const std::string line = impl->startup.logs.front();
      const uint32_t needed = static_cast<uint32_t>(line.size() + 1u);
      if (written + needed > buffer_size) {
        break;
      }
      std::memcpy(out_buffer + written, line.data(), line.size());
      written += static_cast<uint32_t>(line.size());
      out_buffer[written++] = '\n';
      impl->startup.logs.pop_front();
    }
  }
  if (written < buffer_size) {
    out_buffer[written] = '\0';
  } else {
    out_buffer[buffer_size - 1] = '\0';
  }
  *out_bytes_written = written;
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_tick(engine_handle_t handle, uint32_t delta_ms) {
  (void)delta_ms;

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }
  if (g_runtime_startup_active && g_runtime_startup_owner == handle) {
    return SetHandleErrorAndReturnLocked(
        impl, ENGINE_RESULT_INVALID_STATE, "engine startup is still running");
  }

  if (!g_runtime_active || g_runtime_owner != handle) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "engine_open_game must succeed before engine_tick");
  }

  if (impl->state == ToStateValue(EngineState::kPaused)) {
    return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                         "engine is paused");
  }

  if (impl->state != ToStateValue(EngineState::kOpened)) {
    return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                         "engine is not in opened state");
  }
  impl->tick_count += 1;

  // iOS may deliver the first foreground callback before the shared
  // AVAudioSession is ready. The renderer keeps its logical streams locked
  // and retries device activation with a short internal backoff.
  if (::Application && TVPIsAudioRendererSuspendedForHost()) {
    ::Application->RetryAudioRendererForHost();
  }
  TVPPollAudioRendererForHost();

  const auto tick_start = std::chrono::steady_clock::now();
  size_t dispatched_inputs = 0;
  const size_t coalesced_inputs = impl->input.coalesced_events;
  impl->input.coalesced_events = 0;
  while (!impl->input.pending_events.empty()) {
    const engine_input_event_t queued_event = impl->input.pending_events.front();
    impl->input.pending_events.pop_front();
    impl->input.primary_click_gate.on_dequeued(queued_event);
    dispatched_inputs += 1;

    const char* dispatch_error = nullptr;
    const engine_result_t dispatch_result =
        DispatchInputEventNow(impl, queued_event, &dispatch_error);
    if (dispatch_result != ENGINE_RESULT_OK) {
      return SetHandleErrorAndReturnLocked(
          impl, dispatch_result,
          dispatch_error != nullptr ? dispatch_error : "input dispatch failed");
    }
  }
  const auto after_input = std::chrono::steady_clock::now();

#if defined(__ANDROID__) && defined(KRKR_ENABLE_GPU_BRIDGE)
  // Auto-attach pending ANativeWindow from JNI bridge.
  // The Kotlin plugin calls nativeSetSurface() which stores the
  // ANativeWindow in a global variable. Here we detect it and
  // attach it as the EGL WindowSurface render target so that
  // eglSwapBuffers delivers frames to the host external texture.
  if (!impl->render.native_window_attached) {
    ANativeWindow* pending_window = krkr_GetNativeWindow();
    if (pending_window) {
      uint32_t win_w = 0, win_h = 0;
      krkr_GetSurfaceDimensions(&win_w, &win_h);
      auto& egl = krkr::GetEngineEGLContext();
      if (win_w > 0 && win_h > 0) {
        bool attached = false;
        if (!egl.IsValid()) {
          // EGL context not yet initialized — use InitializeWithWindow
          // to create EGL display + context + WindowSurface in one step,
          // bypassing Pbuffer which may not be supported on this device.
          AndroidInfoLog("engine_tick: EGL not valid, InitializeWithWindow %ux%u", win_w, win_h);
          if (egl.InitializeWithWindow(pending_window, win_w, win_h, impl->render.angle_backend)) {
            attached = true;
            AndroidInfoLog("engine_tick: InitializeWithWindow success");
          } else {
            AndroidInfoLog("engine_tick: InitializeWithWindow failed");
          }
        } else {
          // EGL already initialized (Pbuffer) — attach WindowSurface
          AndroidInfoLog("engine_tick: EGL valid, AttachNativeWindow %ux%u", win_w, win_h);
          if (egl.AttachNativeWindow(pending_window, win_w, win_h)) {
            attached = true;
            AndroidInfoLog("engine_tick: AttachNativeWindow success");
          } else {
            AndroidInfoLog("engine_tick: AttachNativeWindow failed");
          }
        }
        if (attached) {
          impl->render.native_window_attached = true;
          spdlog::info("engine_tick: auto-attached ANativeWindow {}x{}", win_w, win_h);
          AndroidInfoLog("engine_tick: auto-attached ANativeWindow %ux%u", win_w, win_h);
          // Update window size for the draw device
          if (TVPMainWindow) {
            auto* dd = TVPMainWindow->GetDrawDevice();
            if (dd) {
              dd->SetWindowSize(static_cast<tjs_int>(win_w),
                                static_cast<tjs_int>(win_h));
            }
          }
        }
      } else if (impl->tick_count % 120 == 0) {
        AndroidInfoLog("engine_tick: pending ANativeWindow but size is 0 (%ux%u)",
                       win_w, win_h);
      }
      // Release the ref acquired by krkr_GetNativeWindow()
      ANativeWindow_release(pending_window);
    } else if (impl->tick_count % 180 == 0) {
      AndroidInfoLog("engine_tick: waiting for ANativeWindow (tick=%llu)",
                     static_cast<unsigned long long>(impl->tick_count));
    }
  } else {
    // Already attached — check if the JNI side has detached the window.
    ANativeWindow* current_window = krkr_GetNativeWindow();
    if (current_window) {
      ANativeWindow_release(current_window);
    } else {
      // Window was detached from JNI side — revert to Pbuffer
      auto& egl = krkr::GetEngineEGLContext();
      egl.DetachNativeWindow();
      impl->render.native_window_attached = false;
      spdlog::info("engine_tick: ANativeWindow detached, reverted to Pbuffer mode");
      AndroidInfoLog("engine_tick: ANativeWindow detached -> Pbuffer");
    }
  }
#endif

  if (TVPTerminated) {
    std::string except_msg = TVPEngineApi_GetGlobalException();
    if (!except_msg.empty()) {
      return SetHandleErrorAndReturnLocked(
          impl, ENGINE_RESULT_INTERNAL_ERROR, except_msg.c_str());
    } else {
      return SetHandleErrorAndReturnLocked(
          impl, ENGINE_RESULT_INVALID_STATE, "runtime has been terminated");
    }
  }

  // Frame rate limiting: when fps_limit > 0, skip rendering if not enough
  // time has elapsed since the last rendered frame. Input events above are
  // always processed regardless of the limit.
  if (impl->fps.limit > 0) {
    const auto now = std::chrono::steady_clock::now();
    if (impl->fps.initialized) {
      const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
          now - impl->fps.last_render_time).count();
      if (static_cast<uint64_t>(elapsed_us) < impl->fps.interval_us) {
        // Not yet time for next frame — skip rendering
        impl->frame.rendered_this_tick = false;
        ClearHandleErrorLocked(impl);
        SetThreadError(nullptr);
        return ENGINE_RESULT_OK;
      }
      // Advance the deadline by exactly one frame interval instead of
      // snapping to `now`. This eliminates the cumulative drift that
      // occurs when vsync intervals don't evenly divide the target
      // frame interval (e.g. 60 Hz vsync vs 30 fps target: 16.6 ms
      // does not divide 33.3 ms evenly, causing every other frame to
      // wait an extra vsync and dropping to ~20-24 fps).
      //
      // If we've fallen behind by more than one full interval (e.g.
      // the app was suspended), snap to `now` to avoid a burst of
      // catch-up renders.
      const auto ideal_next = impl->fps.last_render_time +
          std::chrono::microseconds(impl->fps.interval_us);
      if (now - ideal_next > std::chrono::microseconds(impl->fps.interval_us)) {
        // Fallen too far behind — reset to now
        impl->fps.last_render_time = now;
      } else {
        impl->fps.last_render_time = ideal_next;
      }
    } else {
      impl->fps.last_render_time = now;
      impl->fps.initialized = true;
    }
  }

  // Async startup may have made the EGL context current on a worker thread.
  // Ensure the owner/tick thread has a current context before any GL work.
#if defined(KRKR_ENABLE_GPU_BRIDGE)
  {
    auto& egl = krkr::GetEngineEGLContext();
    if (egl.IsValid() && !egl.MakeCurrent()) {
      return SetHandleErrorAndReturnLocked(
          impl,
          ENGINE_RESULT_INVALID_STATE,
          "failed to make EGL context current before engine_tick");
    }
  }
#endif

  // Drive one full frame (scene update + render + swap). In host mode
  // we must call Application->Run() which processes messages, triggers
  // scene composition, and invokes BasicDrawDevice::Show() →
  // form->UpdateDrawBuffer() — the actual rendering path.
  // TVPDrawSceneOnce() only restores GL state and calls SwapBuffer,
  // which is insufficient.
  if (::Application) {
    // Event delivery can compose many PSD layers before UpdateContent() opens
    // its narrower draw-device batch. Keep the complete application step in
    // one ordered Godot GPU batch and drain it before TVPDrawSceneOnce(). This
    // removes a render-thread submit/fence boundary from each fillRect and
    // operateTo call without deferring any work across the presented frame.
    TVPGodotGpuBatchScope gpu_batch;
    ::Application->Run();
    if (auto* loop = EngineLoop::GetInstance(); loop != nullptr) {
      loop->CompleteInputFrame();
    }
    TVPRepairKagNoTransWait();
    TVPRepairKagEnvironmentWorldReset();
    TVPDeliverWindowCanvasDrawEvents();
  }
  const auto after_application_run = std::chrono::steady_clock::now();
  ::TVPDrawSceneOnce(0);
  const auto after_draw_scene = std::chrono::steady_clock::now();

  // Process deferred texture deletions. iTVPTexture2D::Release() uses
  // delayed deletion — textures are queued in _toDeleteTextures and only
  // freed when RecycleProcess() is called. Without this, every texture
  // released during the frame (via Independ/SetSize/Recreate) accumulates
  // indefinitely, causing a memory leak — especially visible in OpenGL
  // mode where each texture also holds GPU resources.
  iTVPTexture2D::RecycleProcess();
  const auto after_recycle = std::chrono::steady_clock::now();

  auto log_tick_spike = [&](const char* frame_backend) {
    uint64_t threshold_us = EngineTickSpikeThresholdUs();
    {
      std::lock_guard<std::mutex> diagnostic_guard(impl->diagnostics.mutex);
      if (impl->diagnostics.enabled && impl->diagnostics.slow_frame_threshold_us > 0) {
        threshold_us = impl->diagnostics.slow_frame_threshold_us;
      }
    }
    if (threshold_us == 0) {
      return;
    }
    const auto tick_end = std::chrono::steady_clock::now();
    const uint64_t total_us = DurationUs(tick_start, tick_end);
    if (total_us < threshold_us) {
      return;
    }
    uint64_t suppressed_slow_frames = 0;
    {
      std::lock_guard<std::mutex> diagnostic_guard(impl->diagnostics.mutex);
      const auto now = std::chrono::steady_clock::now();
      if (impl->diagnostics.last_slow_frame_log.time_since_epoch().count() > 0 &&
          now - impl->diagnostics.last_slow_frame_log <
              std::chrono::seconds(1)) {
        ++impl->diagnostics.suppressed_slow_frames;
        return;
      }
      impl->diagnostics.last_slow_frame_log = now;
      suppressed_slow_frames = impl->diagnostics.suppressed_slow_frames;
      impl->diagnostics.suppressed_slow_frames = 0;
    }
    spdlog::warn(
        "engine_tick_spike tick={} total_us={} input_us={} app_us={} "
        "draw_us={} recycle_us={} capture_us={} inputs={} coalesced_inputs={} "
        "renderer={} frame_backend={} suppressed={}",
        static_cast<unsigned long long>(impl->tick_count), total_us,
        DurationUs(tick_start, after_input),
        DurationUs(after_input, after_application_run),
        DurationUs(after_application_run, after_draw_scene),
        DurationUs(after_draw_scene, after_recycle),
        DurationUs(after_recycle, tick_end), dispatched_inputs, coalesced_inputs,
        impl->render.renderer, frame_backend,
        static_cast<unsigned long long>(suppressed_slow_frames));
    std::ostringstream fields;
    fields << "{\"tick\":" << impl->tick_count
           << ",\"input_us\":" << DurationUs(tick_start, after_input)
           << ",\"app_us\":" << DurationUs(after_input, after_application_run)
           << ",\"draw_us\":" << DurationUs(after_application_run, after_draw_scene)
           << ",\"recycle_us\":" << DurationUs(after_draw_scene, after_recycle)
           << ",\"capture_us\":" << DurationUs(after_recycle, tick_end)
           << ",\"inputs\":" << dispatched_inputs
           << ",\"coalesced_inputs\":" << coalesced_inputs
           << ",\"suppressed\":" << suppressed_slow_frames
           << ",\"renderer\":\"" << JsonEscape(impl->render.renderer)
           << "\",\"frame_backend\":\"" << JsonEscape(frame_backend) << "\"}";
    PushDiagnosticEvent(impl, "engine", "render", "warning",
                        "engine_tick_spike", total_us, fields.str());
  };

  if (TVPTerminated) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "runtime requested termination");
  }

  impl->frame.rendered_this_tick = false;

  // In IOSurface mode, the engine renders directly to the shared IOSurface
  // via the FBO — no need for glReadPixels. Skip the expensive readback.
  if (impl->render.native_window_attached) {
    // Android WindowSurface mode — TVPForceSwapBuffer() (called by
    // TVPDrawSceneOnce above) only swaps when UpdateDrawBuffer produced new
    // content, so follow the presented serial instead of advancing per tick.
#if defined(__ANDROID__) && defined(KRKR_ENABLE_GPU_BRIDGE)
    const uint64_t presented_serial =
        krkr::GetEngineEGLContext().GetPresentedFrameSerial();
    if (presented_serial != impl->frame.serial) {
      impl->frame.serial = presented_serial;
      impl->frame.rendered_this_tick = true;
    }
    impl->frame.ready = impl->frame.serial != 0;
#else
    impl->frame.serial += 1;
    impl->frame.ready = true;
    impl->frame.rendered_this_tick = true;
#endif
  } else if (!impl->render.iosurface_attached) {
    // GodotNative/DebugCpu host path: BasicDrawDevice handed the final
    // composited texture to HostWindowLayer::UpdateDrawBuffer().
    const uint64_t previous_serial = impl->frame.serial;
    if (CaptureGodotNativeGpuFrameLocked(impl)) {
      impl->frame.rendered_this_tick = impl->frame.serial != previous_serial;
      log_tick_spike("godot_native_gpu");
      ClearHandleErrorLocked(impl);
      SetThreadError(nullptr);
      return ENGINE_RESULT_OK;
    }
    if (CopyHostFrameLocked(impl)) {
      impl->frame.rendered_this_tick = impl->frame.serial != previous_serial;
      log_tick_spike("host_copy");
      ClearHandleErrorLocked(impl);
      SetThreadError(nullptr);
      return ENGINE_RESULT_OK;
    }

    // Legacy Pbuffer readback path (slow, for backward compatibility)
    const FrameReadbackLayout layout = GetFrameReadbackLayoutLocked(impl);
    const size_t required_size =
        static_cast<size_t>(layout.stride_bytes) *
        static_cast<size_t>(layout.height);
    if (impl->frame.rgba.size() != required_size) {
      impl->frame.rgba.assign(required_size, 0);
    }

    if (required_size > 0 &&
        ReadCurrentFrameRgba(layout, impl->frame.rgba.data())) {
      impl->frame.width = layout.width;
      impl->frame.height = layout.height;
      impl->frame.stride_bytes = layout.stride_bytes;
      impl->frame.ready = true;
      impl->frame.serial += 1;
      impl->frame.rendered_this_tick = true;
    } else if (!impl->frame.ready && required_size > 0) {
      std::fill(impl->frame.rgba.begin(), impl->frame.rgba.end(), 0);
      impl->frame.width = layout.width;
      impl->frame.height = layout.height;
      impl->frame.stride_bytes = layout.stride_bytes;
      impl->frame.ready = true;
      impl->frame.serial += 1;
      impl->frame.rendered_this_tick = true;
    }
  } else {
    // IOSurface mode — just increment frame serial, no readback needed.
    // The render output is already in the shared IOSurface.
#if defined(KRKR_ENABLE_GPU_BRIDGE)
    glFlush(); // Ensure GPU commands are submitted
#endif
    impl->frame.serial += 1;
    impl->frame.ready = true;
    impl->frame.rendered_this_tick = true;
  }

  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  log_tick_spike(impl->render.native_window_attached
                     ? "native_window"
                     : (impl->render.iosurface_attached ? "iosurface"
                                                        : "readback"));
#if defined(__ANDROID__)
  if (impl->tick_count % 120 == 0) {
    AndroidInfoLog("engine_tick: tick=%llu rendered=%d serial=%llu native_window=%d iosurface=%d frame_ready=%d",
                   static_cast<unsigned long long>(impl->tick_count),
                   impl->frame.rendered_this_tick ? 1 : 0,
                   static_cast<unsigned long long>(impl->frame.serial),
                   impl->render.native_window_attached ? 1 : 0,
                   impl->render.iosurface_attached ? 1 : 0,
                   impl->frame.ready ? 1 : 0);
  }
#endif
  return ENGINE_RESULT_OK;
}

engine_result_t engine_pause(engine_handle_t handle) {
  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  if (!g_runtime_active || g_runtime_owner != handle) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "engine_open_game must succeed before engine_pause");
  }

  if (impl->state == ToStateValue(EngineState::kPaused)) {
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }

  if (impl->state != ToStateValue(EngineState::kOpened)) {
    return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                         "engine_pause requires opened state");
  }

  Application->OnDeactivate();
  impl->input.active_pointer_ids.clear();
  impl->input.pending_events.clear();
  impl->input.primary_click_gate.reset();
  impl->input.coalesced_events = 0;
  if (auto* loop = EngineLoop::GetInstance(); loop != nullptr) {
    loop->ResetPointerState();
  }
  impl->state = ToStateValue(EngineState::kPaused);
  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_resume(engine_handle_t handle) {
  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  if (!g_runtime_active || g_runtime_owner != handle) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "engine_open_game must succeed before engine_resume");
  }

  if (impl->state == ToStateValue(EngineState::kOpened)) {
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }

  if (impl->state != ToStateValue(EngineState::kPaused)) {
    return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                         "engine_resume requires paused state");
  }

  Application->OnActivate();
  impl->state = ToStateValue(EngineState::kOpened);
  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_set_option(engine_handle_t handle,
                                  const engine_option_t* option) {
  if (option == nullptr || option->key_utf8 == nullptr || option->key_utf8[0] == '\0') {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "option and option->key_utf8 must be non-null/non-empty");
  }
  if (option->value_utf8 == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "option->value_utf8 must be non-null");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  // Handle fps_limit option: controls C++ side frame rate throttling
  const std::string key(option->key_utf8);
  if (key == ENGINE_OPTION_FPS_LIMIT) {
    const int fps = std::atoi(option->value_utf8);
    impl->fps.limit = fps > 0 ? static_cast<uint32_t>(fps) : 0;
    impl->fps.interval_us = fps > 0 ? (1000000u / static_cast<uint32_t>(fps)) : 0;
    // Reset timing so the next tick renders immediately
    impl->fps.initialized = false;
    spdlog::info("engine_set_option: fps_limit={} (interval={}us)",
                 impl->fps.limit, impl->fps.interval_us);
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }

  if (key == ENGINE_OPTION_RENDER_BACKEND || key == ENGINE_OPTION_RENDERER) {
    const std::string value(option->value_utf8);
    const char *renderer = ENGINE_RENDERER_GODOT_NATIVE;
    if (value == ENGINE_RENDER_BACKEND_GPU_BRIDGE || value == ENGINE_RENDERER_GPU_BRIDGE) {
      renderer = ENGINE_RENDERER_GPU_BRIDGE;
    } else if (value == ENGINE_RENDER_BACKEND_DEBUG_CPU || value == ENGINE_RENDERER_DEBUG_CPU ||
               value == ENGINE_RENDERER_SOFTWARE) {
      renderer = ENGINE_RENDERER_DEBUG_CPU;
      spdlog::warn("engine_set_option: DebugCpu renderer selected; this is a fallback path");
    }
    if (g_engine_bootstrapped) {
      spdlog::warn("engine_set_option: renderer changed after engine initialization; "
                   "restart current game session to apply");
    }
    impl->render.renderer = renderer;
    const bool prefer_gpu = ShouldUseGodotGpuFrameForRenderer(impl->render.renderer);
    TVPHostSetPreferGpuFrame(prefer_gpu);
    TVPSetGodotRenderManagerGpuFastPathEnabled(prefer_gpu);
    TVPSetCommandLine(TJS_W("renderer"), ttstr(renderer).c_str());
    spdlog::info("engine_set_option: renderer={} prefer_gpu_frame={}",
                 renderer, prefer_gpu);
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }

  if (key == ENGINE_OPTION_FRAME_OUTPUT) {
    const std::string value(option->value_utf8);
    if (value != ENGINE_FRAME_OUTPUT_SURFACE &&
        value != ENGINE_FRAME_OUTPUT_RAW_SOURCE) {
      return SetHandleErrorAndReturnLocked(
          impl, ENGINE_RESULT_INVALID_ARGUMENT,
          "frame_output must be 'surface' or 'raw_source'");
    }
    impl->render.publish_raw_source_frame =
        value == ENGINE_FRAME_OUTPUT_RAW_SOURCE;
    TVPHostSetPublishRawSourceFrame(impl->render.publish_raw_source_frame);
    spdlog::info("engine_set_option: frame_output={}", value);
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }

  // Handle plugin_trace option: enable/disable plugin call tracing
  if (key == ENGINE_OPTION_PLUGIN_TRACE) {
    const std::string v(option->value_utf8);
    const bool enabled = (v == "1" || v == "true");
    PluginCallTracer::Instance().SetEnabled(enabled);
    spdlog::info("engine_set_option: plugin_trace={}", enabled);
    // Also store in command line so TVPLoadInternalPlugins can read it
    TVPSetCommandLine(ttstr(option->key_utf8).c_str(), ttstr(option->value_utf8));
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }

  if (key == ENGINE_OPTION_PLUGIN_LOAD_MODE) {
    const std::string v(option->value_utf8);
    const char* mode = (v == ENGINE_PLUGIN_LOAD_MODE_AETHER_ALL)
                           ? ENGINE_PLUGIN_LOAD_MODE_AETHER_ALL
                           : ENGINE_PLUGIN_LOAD_MODE_KRKRSDL3;
    TVPSetPluginLoadMode(ttstr(mode));
    TVPSetCommandLine(ttstr(option->key_utf8).c_str(), ttstr(mode));
    spdlog::info("engine_set_option: plugin_load_mode={}", mode);
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }

  // Handle mock_enabled option: enable/disable runtime mock bypass
  if (key == ENGINE_OPTION_MOCK_ENABLED) {
    const std::string v(option->value_utf8);
    const bool enabled = (v == "1" || v == "true");
    TJS::TVPSetMockEnabled(enabled);
    spdlog::info("engine_set_option: mock_enabled={}", enabled);
    TVPSetCommandLine(ttstr(option->key_utf8).c_str(), ttstr(option->value_utf8));
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }

  // Handle console_log_file option: enable/disable krkr.console.log file output
  if (key == ENGINE_OPTION_CONSOLE_LOG_FILE) {
    const std::string v(option->value_utf8);
    const bool enabled = (v == "1" || v == "true");
    g_ConsoleLogFileEnabled = enabled;
    spdlog::info("engine_set_option: console_log_file={}", enabled);
    TVPSetCommandLine(ttstr(option->key_utf8).c_str(), ttstr(option->value_utf8));
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }

  // Handle trace_log option: enable/disable spdlog trace-level logging
  if (key == ENGINE_OPTION_TRACE_LOG) {
    const std::string v(option->value_utf8);
    const bool enabled = (v == "1" || v == "true");
    spdlog::set_level(enabled ? spdlog::level::trace : spdlog::level::info);
    spdlog::flush_on(enabled ? spdlog::level::trace : spdlog::level::warn);
    spdlog::info("engine_set_option: trace_log={}", enabled);
    TVPSetCommandLine(ttstr(option->key_utf8).c_str(), ttstr(option->value_utf8));
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }

  if (key == "input_trace") {
    const std::string v(option->value_utf8);
    const bool enabled = (v == "1" || v == "true");
#if defined(_WIN32)
    _putenv_s("AETHERKIRI_INPUT_TRACE", enabled ? "1" : "");
#else
    if (enabled) {
      setenv("AETHERKIRI_INPUT_TRACE", "1", 1);
    } else {
      unsetenv("AETHERKIRI_INPUT_TRACE");
    }
#endif
    spdlog::info("engine_set_option: input_trace={}", enabled);
    TVPSetCommandLine(ttstr(option->key_utf8).c_str(), ttstr(option->value_utf8));
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }

  // Handle export_scripts option: enable/disable TJS script export
  if (key == ENGINE_OPTION_EXPORT_SCRIPTS) {
    const std::string v(option->value_utf8);
    const bool enabled = (v == "1" || v == "true");
    spdlog::info("engine_set_option: export_scripts={}", enabled);
    TVPSetCommandLine(ttstr(option->key_utf8).c_str(), ttstr(option->value_utf8));
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }

  if (key == ENGINE_OPTION_ERROR_DIALOG_LOGS) {
    const std::string v(option->value_utf8);
    const bool enabled = (v == "1" || v == "true");
    spdlog::info("engine_set_option: error_dialog_logs={}", enabled);
    TVPSetCommandLine(ttstr(option->key_utf8).c_str(), ttstr(option->value_utf8));
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }

  TVPSetCommandLine(ttstr(option->key_utf8).c_str(), ttstr(option->value_utf8));

  if (key == ENGINE_OPTION_ARCHIVE_CACHE_COUNT) {
    const int count = std::atoi(option->value_utf8);
    if (g_engine_bootstrapped && count > 0) {
      TVPSetArchiveCacheCount(static_cast<tjs_uint>(count));
    }
  } else if (key == ENGINE_OPTION_AUTOPATH_CACHE_COUNT) {
    const int count = std::atoi(option->value_utf8);
    if (g_engine_bootstrapped && count > 0) {
      TVPSetAutoPathCacheMaxCount(static_cast<tjs_uint>(count));
    }
  } else if (key == ENGINE_OPTION_PSB_CACHE_MB) {
    const int cache_mb = std::atoi(option->value_utf8);
    if (cache_mb > 0) {
      impl->memory_options.psb_cache_mb = cache_mb;
    }
    if (g_engine_bootstrapped && impl->memory_options.psb_cache_entries > 0 &&
        impl->memory_options.psb_cache_mb > 0) {
      PSB::SetPSBMediaCacheBudget(
          static_cast<size_t>(impl->memory_options.psb_cache_entries),
          static_cast<size_t>(impl->memory_options.psb_cache_mb) * 1024ULL *
              1024ULL);
    }
  } else if (key == ENGINE_OPTION_PSB_CACHE_ENTRIES) {
    const int entries = std::atoi(option->value_utf8);
    if (entries > 0) {
      impl->memory_options.psb_cache_entries = entries;
    }
    if (g_engine_bootstrapped && impl->memory_options.psb_cache_entries > 0 &&
        impl->memory_options.psb_cache_mb > 0) {
      PSB::SetPSBMediaCacheBudget(
          static_cast<size_t>(impl->memory_options.psb_cache_entries),
          static_cast<size_t>(impl->memory_options.psb_cache_mb) * 1024ULL *
              1024ULL);
    }
  }

  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_set_surface_size(engine_handle_t handle,
                                        uint32_t width,
                                        uint32_t height) {
  if (width == 0 || height == 0) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "width and height must be > 0");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  if (impl->state == ToStateValue(EngineState::kDestroyed)) {
    return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                         "engine is already destroyed");
  }

  impl->frame.surface_width = width;
  impl->frame.surface_height = height;
  impl->frame.width = 0;
  impl->frame.height = 0;
  impl->frame.stride_bytes = 0;
  impl->frame.rgba.clear();
  impl->frame.ready = false;
  TVPHostSetSurfaceSize(width, height);

  // Propagate the new surface size to the bridge context and viewport.
  if (g_runtime_active && g_runtime_owner == handle) {
#if defined(KRKR_ENABLE_GPU_BRIDGE)
    auto& egl = krkr::GetEngineEGLContext();
    if (egl.IsValid()) {
      if (egl.HasNativeWindow()) {
        // Android WindowSurface mode: setDefaultBufferSize() already
        // changed the SurfaceTexture buffer dimensions. The EGL surface
        // auto-adapts on next eglSwapBuffers. Update our stored
        // dimensions so UpdateDrawBuffer() uses the correct viewport.
        egl.UpdateNativeWindowSize(width, height);
      } else {
        // Pbuffer mode (macOS / iOS): resize the Pbuffer surface.
        const uint32_t cur_w = egl.GetWidth();
        const uint32_t cur_h = egl.GetHeight();
        if (cur_w != width || cur_h != height) {
          egl.Resize(width, height);
          glViewport(0, 0, static_cast<GLsizei>(width),
                     static_cast<GLsizei>(height));
        }
      }
    }
#endif

    // Only update WindowSize here — DestRect is exclusively managed by
    // UpdateDrawBuffer() which calculates the correct letterbox viewport.
    if (TVPMainWindow) {
      auto* dd = TVPMainWindow->GetDrawDevice();
      if (dd) {
        dd->SetWindowSize(static_cast<tjs_int>(width),
                          static_cast<tjs_int>(height));
      }
      TVPMainWindow->RequestUpdate();
    }
    if (auto* scene = TVPMainScene::GetInstance(); scene != nullptr) {
      scene->scheduleUpdate();
    }
  }

  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_get_frame_desc(engine_handle_t handle,
                                      engine_frame_desc_t* out_frame_desc) {
  if (out_frame_desc == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_frame_desc is null");
  }
  if (out_frame_desc->struct_size < sizeof(engine_frame_desc_t)) {
    return SetThreadErrorAndReturn(
        ENGINE_RESULT_INVALID_ARGUMENT,
        "engine_frame_desc_t.struct_size is too small");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  if (impl->state == ToStateValue(EngineState::kDestroyed)) {
    return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                         "engine is already destroyed");
  }

  if (!impl->frame.ready) {
    CopyHostFrameLocked(impl);
  }

  FrameReadbackLayout layout;
  if (impl->frame.ready && impl->frame.width > 0 && impl->frame.height > 0 &&
      impl->frame.stride_bytes > 0) {
    layout.width = impl->frame.width;
    layout.height = impl->frame.height;
    layout.stride_bytes = impl->frame.stride_bytes;
  } else {
    layout = GetFrameReadbackLayoutLocked(impl);
  }

  std::memset(out_frame_desc, 0, sizeof(*out_frame_desc));
  out_frame_desc->struct_size = sizeof(engine_frame_desc_t);
  out_frame_desc->width = layout.width;
  out_frame_desc->height = layout.height;
  out_frame_desc->stride_bytes = layout.stride_bytes;
  out_frame_desc->pixel_format = ENGINE_PIXEL_FORMAT_RGBA8888;
  out_frame_desc->frame_serial = impl->frame.serial;

  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_read_frame_rgba(engine_handle_t handle,
                                       void* out_pixels,
                                       size_t out_pixels_size) {
  if (out_pixels == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_pixels is null");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  if (impl->state != ToStateValue(EngineState::kOpened) &&
      impl->state != ToStateValue(EngineState::kPaused)) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "engine_open_game must succeed before engine_read_frame_rgba");
  }

  if (!impl->frame.ready) {
    CopyHostFrameLocked(impl);
  }

  FrameReadbackLayout layout;
  if (impl->frame.ready && impl->frame.width > 0 && impl->frame.height > 0 &&
      impl->frame.stride_bytes > 0) {
    layout.width = impl->frame.width;
    layout.height = impl->frame.height;
    layout.stride_bytes = impl->frame.stride_bytes;
  } else {
    layout = GetFrameReadbackLayoutLocked(impl);
    const size_t required_size =
        static_cast<size_t>(layout.stride_bytes) *
        static_cast<size_t>(layout.height);
    impl->frame.rgba.assign(required_size, 0);
    impl->frame.width = layout.width;
    impl->frame.height = layout.height;
    impl->frame.stride_bytes = layout.stride_bytes;
    impl->frame.ready = true;
  }

  size_t required_size =
      static_cast<size_t>(layout.stride_bytes) *
      static_cast<size_t>(layout.height);
  if (out_pixels_size < required_size) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_ARGUMENT,
        "out_pixels_size is smaller than required frame buffer size");
  }

  if (impl->frame.rgba.size() < required_size) {
    if (CopyHostFrameLocked(impl) && impl->frame.width > 0 &&
        impl->frame.height > 0 && impl->frame.stride_bytes > 0) {
      layout.width = impl->frame.width;
      layout.height = impl->frame.height;
      layout.stride_bytes = impl->frame.stride_bytes;
      required_size =
          static_cast<size_t>(layout.stride_bytes) *
          static_cast<size_t>(layout.height);
      if (out_pixels_size < required_size) {
        return SetHandleErrorAndReturnLocked(
            impl,
            ENGINE_RESULT_INVALID_ARGUMENT,
            "out_pixels_size is smaller than required frame buffer size");
      }
    }
  }

  if (impl->frame.rgba.size() < required_size) {
    impl->frame.rgba.resize(required_size, 0);
  }
  std::memcpy(out_pixels, impl->frame.rgba.data(), required_size);

  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_media_open(engine_handle_t engine,
                                  const char* path_utf8,
                                  engine_media_handle_t* out_media) {
  if (path_utf8 == nullptr || *path_utf8 == '\0' || out_media == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "media path and output handle are required");
  }
  *out_media = nullptr;
  engine_handle_s* owner = nullptr;
  {
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    const auto result = ValidateHandleLocked(engine, &owner);
    if (result != ENGINE_RESULT_OK) return result;
  }
  auto media = std::make_unique<engine_media_handle_s>();
  media->owner = engine;
  media->player = std::make_unique<StandaloneMediaPlayer>();
  std::string error;
  if (!media->player->Open(path_utf8, &error)) {
    SetHandleErrorLocked(owner, error.c_str());
    return ENGINE_RESULT_IO_ERROR;
  }
  *out_media = media.release();
  ClearHandleErrorLocked(owner);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_media_destroy(engine_media_handle_t media) {
  if (media == nullptr) return ENGINE_RESULT_OK;
  auto* impl = reinterpret_cast<engine_media_handle_s*>(media);
  {
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    if (impl->player != nullptr) {
      impl->player->Stop();
      impl->player.reset();
    }
  }
  delete impl;
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_media_play(engine_media_handle_t media) {
  if (media == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "media handle is null");
  }
  auto* impl = reinterpret_cast<engine_media_handle_s*>(media);
  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->player == nullptr) return ENGINE_RESULT_INVALID_STATE;
  impl->player->Play();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_media_pause(engine_media_handle_t media) {
  if (media == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "media handle is null");
  }
  auto* impl = reinterpret_cast<engine_media_handle_s*>(media);
  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->player == nullptr) return ENGINE_RESULT_INVALID_STATE;
  impl->player->Pause();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_media_seek(engine_media_handle_t media,
                                  int64_t position_ms) {
  if (media == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "media handle is null");
  }
  auto* impl = reinterpret_cast<engine_media_handle_s*>(media);
  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->player == nullptr) return ENGINE_RESULT_INVALID_STATE;
  const int64_t duration = impl->player->player()->GetTotalTime();
  impl->player->SetPosition(static_cast<uint64_t>(
      std::clamp<int64_t>(position_ms, 0, std::max<int64_t>(0, duration))));
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_media_set_rate(engine_media_handle_t media,
                                      double playback_rate) {
  if (media == nullptr || !std::isfinite(playback_rate) ||
      playback_rate < 0.5 || playback_rate > 2.0) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "playback rate must be between 0.5 and 2.0");
  }
  auto* impl = reinterpret_cast<engine_media_handle_s*>(media);
  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->player == nullptr) return ENGINE_RESULT_INVALID_STATE;
  impl->player->SetPlayRate(playback_rate);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_media_get_state(engine_media_handle_t media,
                                       engine_media_state_t* out_state) {
  if (media == nullptr || out_state == nullptr ||
      out_state->struct_size < sizeof(engine_media_state_t)) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "media state output is invalid");
  }
  auto* impl = reinterpret_cast<engine_media_handle_s*>(media);
  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->player == nullptr) return ENGINE_RESULT_INVALID_STATE;
  impl->player->UpdateFrame();
  auto* player = impl->player->player();
  std::memset(out_state, 0, sizeof(*out_state));
  out_state->struct_size = sizeof(*out_state);
  out_state->width = impl->player->width();
  out_state->height = impl->player->height();
  out_state->position_ms = player->GetTime();
  out_state->duration_ms = player->GetTotalTime();
  out_state->playback_rate = player->GetSpeed();
  out_state->frame_serial = impl->player->frame_serial();
  out_state->frame_ready = impl->player->frame_ready() ? 1u : 0u;
  out_state->seekable = player->CanSeek() ? 1u : 0u;
  out_state->has_audio = player->GetAudioStreamCount() > 0 ? 1u : 0u;
  out_state->has_video = player->GetVideoStreamCount() > 0 ? 1u : 0u;
  if (impl->player->ended()) {
    out_state->status = ENGINE_MEDIA_STATUS_ENDED;
  } else if (player->GetSpeed() == 0.0) {
    out_state->status = ENGINE_MEDIA_STATUS_PAUSED;
  } else {
    out_state->status = ENGINE_MEDIA_STATUS_PLAYING;
  }
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_media_get_subtitle_tracks_json(
    engine_media_handle_t media, char* out_buffer, uint32_t buffer_size,
    uint32_t* out_bytes_written) {
  if (media == nullptr || out_buffer == nullptr || buffer_size == 0 ||
      out_bytes_written == nullptr) {
    return SetThreadErrorAndReturn(
        ENGINE_RESULT_INVALID_ARGUMENT,
        "media subtitle track output buffer is invalid");
  }
  auto* impl = reinterpret_cast<engine_media_handle_s*>(media);
  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->player == nullptr) return ENGINE_RESULT_INVALID_STATE;
  const std::string payload = impl->player->EmbeddedSubtitleTracksJson();
  if (payload.size() + 1u > buffer_size) {
    *out_bytes_written = 0;
    out_buffer[0] = '\0';
    SetHandleErrorLocked(impl->owner,
                         "media subtitle track output buffer is too small");
    return ENGINE_RESULT_INVALID_ARGUMENT;
  }
  std::memcpy(out_buffer, payload.data(), payload.size());
  out_buffer[payload.size()] = '\0';
  *out_bytes_written = static_cast<uint32_t>(payload.size());
  ClearHandleErrorLocked(impl->owner);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_media_extract_subtitle(
    engine_media_handle_t media, int32_t stream_index,
    const char* output_path_utf8) {
  if (media == nullptr || stream_index < 0 || output_path_utf8 == nullptr ||
      *output_path_utf8 == '\0') {
    return SetThreadErrorAndReturn(
        ENGINE_RESULT_INVALID_ARGUMENT,
        "media subtitle stream and output path are required");
  }
  auto* impl = reinterpret_cast<engine_media_handle_s*>(media);
  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->player == nullptr) return ENGINE_RESULT_INVALID_STATE;
  std::string error;
  if (!impl->player->ExtractEmbeddedSubtitle(stream_index, output_path_utf8,
                                             &error)) {
    SetHandleErrorLocked(impl->owner, error.c_str());
    return ENGINE_RESULT_IO_ERROR;
  }
  ClearHandleErrorLocked(impl->owner);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_media_read_frame_rgba(
    engine_media_handle_t media, void* out_pixels, size_t out_pixels_size,
    engine_frame_desc_t* out_frame_desc) {
  if (media == nullptr || out_pixels == nullptr || out_frame_desc == nullptr ||
      out_frame_desc->struct_size < sizeof(engine_frame_desc_t)) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "media frame output is invalid");
  }
  auto* impl = reinterpret_cast<engine_media_handle_s*>(media);
  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->player == nullptr || !impl->player->frame_ready()) {
    return ENGINE_RESULT_INVALID_STATE;
  }
  const auto& rgba = impl->player->latest_rgba();
  if (out_pixels_size < rgba.size()) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "media frame buffer is too small");
  }
  std::memcpy(out_pixels, rgba.data(), rgba.size());
  std::memset(out_frame_desc, 0, sizeof(*out_frame_desc));
  out_frame_desc->struct_size = sizeof(*out_frame_desc);
  out_frame_desc->width = impl->player->width();
  out_frame_desc->height = impl->player->height();
  out_frame_desc->stride_bytes = impl->player->width() * 4u;
  out_frame_desc->pixel_format = ENGINE_PIXEL_FORMAT_RGBA8888;
  out_frame_desc->frame_serial = impl->player->frame_serial();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_get_godot_native_frame_texture(
    engine_handle_t handle, uint64_t* out_texture_id, uint32_t* out_width,
    uint32_t* out_height, uint64_t* out_frame_serial) {
  if (out_texture_id == nullptr || out_width == nullptr ||
      out_height == nullptr || out_frame_serial == nullptr) {
    return SetThreadErrorAndReturn(
        ENGINE_RESULT_INVALID_ARGUMENT,
        "out_texture_id/out_width/out_height/out_frame_serial must be non-null");
  }
  *out_texture_id = 0;
  *out_width = 0;
  *out_height = 0;
  *out_frame_serial = 0;

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  if (impl->state != ToStateValue(EngineState::kOpened) &&
      impl->state != ToStateValue(EngineState::kPaused)) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "engine_open_game must succeed before engine_get_godot_native_frame_texture");
  }

  uint64_t texture = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint64_t serial = 0;
  if (!TVPHostGetLatestGodotGpuFrame(&texture, &width, &height, &serial) ||
      texture == 0 || width == 0 || height == 0) {
    return SetHandleErrorAndReturnLocked(
        impl, ENGINE_RESULT_NOT_SUPPORTED,
        "current frame is not backed by a Godot native GPU texture");
  }

  *out_texture_id = texture;
  *out_width = width;
  *out_height = height;
  *out_frame_serial = serial;
  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_get_host_native_window(engine_handle_t handle,
                                              void** out_window_handle) {
  if (out_window_handle == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_window_handle is null");
  }
  *out_window_handle = nullptr;

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  if (!g_runtime_active || g_runtime_owner != handle) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "engine_open_game must succeed before engine_get_host_native_window");
  }

#if defined(TARGET_OS_MAC) && TARGET_OS_MAC && !TARGET_OS_IPHONE
  // No native GLFW window in headless pbuffer mode.
  return SetHandleErrorAndReturnLocked(
      impl,
      ENGINE_RESULT_NOT_SUPPORTED,
      "engine_get_host_native_window is not supported in headless pbuffer mode");
#else
  return SetHandleErrorAndReturnLocked(
      impl,
      ENGINE_RESULT_NOT_SUPPORTED,
      "engine_get_host_native_window is only supported on macOS runtime");
#endif
}

engine_result_t engine_get_host_native_view(engine_handle_t handle,
                                            void** out_view_handle) {
  if (out_view_handle == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_view_handle is null");
  }
  *out_view_handle = nullptr;

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  if (!g_runtime_active || g_runtime_owner != handle) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "engine_open_game must succeed before engine_get_host_native_view");
  }

  // No native GLFW window in headless pbuffer mode; native view is unavailable.
  return SetHandleErrorAndReturnLocked(
      impl,
      ENGINE_RESULT_NOT_SUPPORTED,
      "engine_get_host_native_view is not supported in headless pbuffer mode");
}

engine_result_t engine_send_input(engine_handle_t handle,
                                  const engine_input_event_t* event) {
  if (event == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "event is null");
  }
  if (event->struct_size < sizeof(engine_input_event_t)) {
    return SetThreadErrorAndReturn(
        ENGINE_RESULT_INVALID_ARGUMENT,
        "engine_input_event_t.struct_size is too small");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  if (impl->state == ToStateValue(EngineState::kPaused)) {
    return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                         "engine is paused");
  }
  if (impl->state != ToStateValue(EngineState::kOpened)) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "engine_open_game must succeed before engine_send_input");
  }

  switch (event->type) {
    case ENGINE_INPUT_EVENT_POINTER_DOWN:
    case ENGINE_INPUT_EVENT_POINTER_MOVE:
    case ENGINE_INPUT_EVENT_POINTER_UP:
    case ENGINE_INPUT_EVENT_POINTER_SCROLL:
    case ENGINE_INPUT_EVENT_KEY_DOWN:
    case ENGINE_INPUT_EVENT_KEY_UP:
    case ENGINE_INPUT_EVENT_TEXT_INPUT:
    case ENGINE_INPUT_EVENT_BACK:
      break;
    default:
      return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_NOT_SUPPORTED,
                                           "unsupported input event type");
  }

  if (event->type == ENGINE_INPUT_EVENT_POINTER_DOWN ||
      event->type == ENGINE_INPUT_EVENT_POINTER_MOVE ||
      event->type == ENGINE_INPUT_EVENT_POINTER_UP ||
      event->type == ENGINE_INPUT_EVENT_POINTER_SCROLL) {
    if (!IsFinitePointerValue(event->x) || !IsFinitePointerValue(event->y) ||
        !IsFinitePointerValue(event->delta_x) ||
        !IsFinitePointerValue(event->delta_y)) {
      return SetHandleErrorAndReturnLocked(
          impl, ENGINE_RESULT_INVALID_ARGUMENT,
          "pointer coordinates contain non-finite values");
    }
  }

  if (!impl->input.primary_click_gate.should_enqueue(*event)) {
    impl->input.coalesced_events += 1;
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }

  impl->input.pending_events.push_back(*event);
  constexpr size_t kMaxQueuedInputs = 512;
  if (impl->input.pending_events.size() > kMaxQueuedInputs) {
    impl->input.primary_click_gate.on_dequeued(
        impl->input.pending_events.front());
    impl->input.pending_events.pop_front();
  }

  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_get_text_input_state(
    engine_handle_t handle, engine_text_input_state_t* out_state) {
  if (out_state == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_state is null");
  }
  if (out_state->struct_size < sizeof(engine_text_input_state_t)) {
    return SetThreadErrorAndReturn(
        ENGINE_RESULT_INVALID_ARGUMENT,
        "engine_text_input_state_t.struct_size is too small");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }
  if (impl->state != ToStateValue(EngineState::kOpened) &&
      impl->state != ToStateValue(EngineState::kPaused)) {
    return SetHandleErrorAndReturnLocked(
        impl, ENGINE_RESULT_INVALID_STATE,
        "engine_open_game must succeed before engine_get_text_input_state");
  }

  engine_text_input_state_t snapshot{};
  snapshot.struct_size = sizeof(snapshot);
  TVPHostGetTextInputState(&snapshot.ime_active, &snapshot.ime_mode,
                           &snapshot.attention_point_valid,
                           &snapshot.attention_x, &snapshot.attention_y,
                           &snapshot.text_available,
                           &snapshot.text_utf8_bytes,
                           &snapshot.selection_start,
                           &snapshot.selection_end);
  *out_state = snapshot;
  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_copy_text_input_text(
    engine_handle_t handle, char* out_buffer, uint32_t buffer_size,
    uint32_t* out_bytes_written) {
  if (out_buffer == nullptr || buffer_size == 0 ||
      out_bytes_written == nullptr) {
    return SetThreadErrorAndReturn(
        ENGINE_RESULT_INVALID_ARGUMENT,
        "engine_copy_text_input_text requires a buffer and byte count");
  }
  out_buffer[0] = '\0';
  *out_bytes_written = 0;

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }
  if (impl->state != ToStateValue(EngineState::kOpened) &&
      impl->state != ToStateValue(EngineState::kPaused)) {
    return SetHandleErrorAndReturnLocked(
        impl, ENGINE_RESULT_INVALID_STATE,
        "engine_open_game must succeed before engine_copy_text_input_text");
  }

  *out_bytes_written = TVPHostCopyTextInputText(out_buffer, buffer_size);
  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_get_main_menu_json(engine_handle_t handle,
                                          char* out_buffer,
                                          uint32_t buffer_size,
                                          uint32_t* out_bytes_written) {
  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }
  if (impl->state != ToStateValue(EngineState::kOpened) &&
      impl->state != ToStateValue(EngineState::kPaused)) {
    return SetHandleErrorAndReturnLocked(
        impl, ENGINE_RESULT_INVALID_STATE,
        "engine_open_game must succeed before engine_get_main_menu_json");
  }

  const std::string json = BuildMainMenuJson();
  result = CopyUtf8StringToBuffer(json, out_buffer, buffer_size, out_bytes_written);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_activate_menu_item(engine_handle_t handle,
                                          const char* item_path_utf8) {
  if (item_path_utf8 == nullptr || item_path_utf8[0] == '\0') {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "item_path_utf8 is null or empty");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }
  if (impl->state != ToStateValue(EngineState::kOpened) &&
      impl->state != ToStateValue(EngineState::kPaused)) {
    return SetHandleErrorAndReturnLocked(
        impl, ENGINE_RESULT_INVALID_STATE,
        "engine_open_game must succeed before engine_activate_menu_item");
  }

  std::vector<tjs_int> path_segments;
  if (!ParseMenuPath(item_path_utf8, &path_segments)) {
    return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_ARGUMENT,
                                         "invalid menu item path");
  }

  tTJSVariant item_variant;
  if (!ResolveMenuItemByPath(path_segments, &item_variant) ||
      item_variant.Type() != tvtObject ||
      item_variant.AsObjectNoAddRef() == nullptr) {
    return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_ARGUMENT,
                                         "menu item path not found");
  }

  iTJSDispatch2* item = item_variant.AsObjectNoAddRef();
  if (!TryGetBoolProperty(item, TJS_W("enabled"), true) ||
      !TryGetBoolProperty(item, TJS_W("visible"), true)) {
    return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                         "menu item is disabled or hidden");
  }

  tTJSVariant fire_click_variant;
  if (!TryGetProperty(item, TJS_W("fireClick"), &fire_click_variant) ||
      fire_click_variant.Type() != tvtObject ||
      fire_click_variant.AsObjectNoAddRef() == nullptr) {
    return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_NOT_SUPPORTED,
                                         "menu item cannot be activated");
  }

  tTJSVariantClosure fire_click = fire_click_variant.AsObjectClosureNoAddRef();
  const tjs_error call_result =
      fire_click.FuncCall(0, nullptr, nullptr, nullptr, 0, nullptr, item);
  if (TJS_FAILED(call_result)) {
    return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INTERNAL_ERROR,
                                         "menu item activation failed");
  }

  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_set_render_target_iosurface(engine_handle_t handle,
                                                    uint32_t iosurface_id,
                                                    uint32_t width,
                                                    uint32_t height) {
  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  if (!g_runtime_active || g_runtime_owner != handle) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "engine_open_game must succeed before engine_set_render_target_iosurface");
  }

#if defined(__APPLE__) && defined(KRKR_ENABLE_GPU_BRIDGE)
  auto& egl = krkr::GetEngineEGLContext();
  if (!egl.IsValid()) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "EGL context not initialized");
  }

  if (iosurface_id == 0) {
    // Detach — revert to Pbuffer mode
    egl.DetachIOSurface();
    impl->render.iosurface_attached = false;
    spdlog::info("engine_set_render_target_iosurface: detached, Pbuffer mode");
  } else {
    if (width == 0 || height == 0) {
      return SetHandleErrorAndReturnLocked(
          impl,
          ENGINE_RESULT_INVALID_ARGUMENT,
          "width and height must be > 0 when setting IOSurface");
    }
    if (!egl.AttachIOSurface(iosurface_id, width, height)) {
      return SetHandleErrorAndReturnLocked(
          impl,
          ENGINE_RESULT_INTERNAL_ERROR,
          "failed to attach IOSurface as render target");
    }
    impl->render.iosurface_attached = true;
    spdlog::info("engine_set_render_target_iosurface: attached id={} {}x{}",
                 iosurface_id, width, height);

    // Only update WindowSize here — DestRect is exclusively managed by
    // UpdateDrawBuffer() which calculates the correct letterbox viewport.
    // Setting DestRect here would overwrite the viewport offset and cause
    // mouse Y-axis misalignment when game aspect ratio != surface aspect ratio.
    if (TVPMainWindow) {
      auto* dd = TVPMainWindow->GetDrawDevice();
      if (dd) {
        dd->SetWindowSize(static_cast<tjs_int>(width),
                          static_cast<tjs_int>(height));
      }
      TVPMainWindow->RequestUpdate();
    }
    if (auto* scene = TVPMainScene::GetInstance(); scene != nullptr) {
      scene->scheduleUpdate();
    }
  }

  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
#else
  (void)iosurface_id;
  (void)width;
  (void)height;
  return SetHandleErrorAndReturnLocked(
      impl,
      ENGINE_RESULT_NOT_SUPPORTED,
      "IOSurface render target requires the GPU Bridge backend");
#endif
}

engine_result_t engine_set_render_target_surface(engine_handle_t handle,
                                                  void* native_window,
                                                  uint32_t width,
                                                  uint32_t height) {
  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  if (!g_runtime_active || g_runtime_owner != handle) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "engine_open_game must succeed before engine_set_render_target_surface");
  }

#if defined(__ANDROID__) && defined(KRKR_ENABLE_GPU_BRIDGE)
  auto& egl = krkr::GetEngineEGLContext();
  if (!egl.IsValid()) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "EGL context not initialized");
  }

  if (native_window == nullptr) {
    // Detach — revert to Pbuffer mode
    egl.DetachNativeWindow();
    impl->render.native_window_attached = false;
    spdlog::info("engine_set_render_target_surface: detached, Pbuffer mode");
  } else {
    if (width == 0 || height == 0) {
      return SetHandleErrorAndReturnLocked(
          impl,
          ENGINE_RESULT_INVALID_ARGUMENT,
          "width and height must be > 0 when setting Surface");
    }
    if (!egl.AttachNativeWindow(native_window, width, height)) {
      return SetHandleErrorAndReturnLocked(
          impl,
          ENGINE_RESULT_INTERNAL_ERROR,
          "failed to attach Android Surface as render target");
    }
    impl->render.native_window_attached = true;
    spdlog::info("engine_set_render_target_surface: attached {}x{}", width, height);

    // Update window size for the draw device
    if (TVPMainWindow) {
      auto* dd = TVPMainWindow->GetDrawDevice();
      if (dd) {
        dd->SetWindowSize(static_cast<tjs_int>(width),
                          static_cast<tjs_int>(height));
      }
    }
  }

  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
#else
  (void)native_window;
  (void)width;
  (void)height;
  return SetHandleErrorAndReturnLocked(
      impl,
      ENGINE_RESULT_NOT_SUPPORTED,
      "Surface render target requires the GPU Bridge backend");
#endif
}

engine_result_t engine_get_frame_rendered_flag(engine_handle_t handle,
                                                uint32_t* out_flag) {
  if (out_flag == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_flag is null");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    *out_flag = 0;
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  *out_flag = impl->frame.rendered_this_tick ? 1 : 0;
  impl->frame.rendered_this_tick = false;

  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_get_renderer_info(engine_handle_t handle,
                                         char* out_buffer,
                                         uint32_t buffer_size) {
  if (out_buffer == nullptr || buffer_size == 0) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_buffer is null or buffer_size is 0");
  }
  out_buffer[0] = '\0';

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  if (!g_runtime_active || g_runtime_owner != handle) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "engine_open_game must succeed before engine_get_renderer_info");
  }

  const std::string& selected_renderer = impl->render.renderer;
  const bool prefer_gpu_frame =
      ShouldUseGodotGpuFrameForRenderer(selected_renderer);
  const bool native_frame_renderer =
      selected_renderer == ENGINE_RENDERER_GODOT_NATIVE ||
      selected_renderer == ENGINE_RENDERER_GPU_BRIDGE;
  const std::string host_frame =
      native_frame_renderer && prefer_gpu_frame ? "godot_native_texture"
                                                : "rgba_copy";
  const std::string fallback =
      selected_renderer == ENGINE_RENDERER_DEBUG_CPU
          ? "debug_cpu"
          : selected_renderer == ENGINE_RENDERER_GPU_BRIDGE && prefer_gpu_frame
                ? "gpu_bridge_godot_texture"
                : native_frame_renderer && !prefer_gpu_frame
                      ? "cpu_composite final_upload=godot_rd"
                      : "see_fallback_ops final_upload=godot_rd";

#if defined(KRKR_ENABLE_GPU_BRIDGE)
  std::string gpu_info;
  if (selected_renderer == ENGINE_RENDERER_GPU_BRIDGE) {
    auto& egl = krkr::GetEngineEGLContext();
    if (egl.IsValid() && egl.MakeCurrent()) {
      const char* gl_renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
      const char* gl_version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
      gpu_info = " gpu_renderer=" + std::string(gl_renderer ? gl_renderer : "(unknown)") +
                 " gpu_api=" + std::string(gl_version ? gl_version : "(unknown)");
    } else {
      gpu_info = " bridge=egl_unavailable";
    }
  }
  std::string info = "backend=" + selected_renderer +
                     " path=godot_rendering_device host_frame=" + host_frame +
                     " fallback=" + fallback +
                     " gpu_fastpath=" + (prefer_gpu_frame ? "1" : "0") +
                     gpu_info +
                     TVPGetGodotRenderManagerFallbackStats();
#else
  std::string info = "backend=" + selected_renderer +
                     " path=godot_rendering_device host_frame=" + host_frame +
                     " fallback=" + fallback +
                     " gpu_fastpath=" + (prefer_gpu_frame ? "1" : "0") +
                     TVPGetGodotRenderManagerFallbackStats();
#endif
  std::strncpy(out_buffer, info.c_str(), buffer_size - 1);
  out_buffer[buffer_size - 1] = '\0';

  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_get_memory_stats(engine_handle_t handle,
                                        engine_memory_stats_t* out_stats) {
  if (out_stats == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_stats is null");
  }
  if (out_stats->struct_size < sizeof(engine_memory_stats_t)) {
    return SetThreadErrorAndReturn(
        ENGINE_RESULT_INVALID_ARGUMENT,
        "engine_memory_stats_t.struct_size is too small");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::memset(out_stats, 0, sizeof(*out_stats));
  out_stats->struct_size = sizeof(engine_memory_stats_t);
  TVPMemoryInfo meminfo{};
  TVPGetMemoryInfo(meminfo);
  out_stats->self_used_mb = static_cast<uint32_t>(
      std::max<tjs_int>(0, TVPGetSelfUsedMemory()));
  out_stats->system_free_mb = static_cast<uint32_t>(
      std::max<tjs_int>(0, TVPGetSystemFreeMemory()));
  out_stats->system_total_mb = static_cast<uint32_t>(meminfo.MemTotal / 1024);

  out_stats->process_resident_bytes =
      static_cast<uint64_t>(out_stats->self_used_mb) * 1024u * 1024u;
  out_stats->process_physical_footprint_bytes =
      out_stats->process_resident_bytes;
  out_stats->process_peak_physical_footprint_bytes =
      out_stats->process_physical_footprint_bytes;
#if defined(__APPLE__)
  task_vm_info_data_t vm_info{};
  mach_msg_type_number_t vm_info_count = TASK_VM_INFO_COUNT;
  if (task_info(mach_task_self(), TASK_VM_INFO,
                reinterpret_cast<task_info_t>(&vm_info),
                &vm_info_count) == KERN_SUCCESS) {
    out_stats->process_resident_bytes = vm_info.resident_size;
    out_stats->process_physical_footprint_bytes = vm_info.phys_footprint;
    if (vm_info.ledger_phys_footprint_peak > 0) {
      out_stats->process_peak_physical_footprint_bytes =
          static_cast<uint64_t>(vm_info.ledger_phys_footprint_peak);
    }
#if TARGET_OS_IPHONE
    out_stats->process_available_bytes = vm_info.limit_bytes_remaining;
#endif
  }
#endif
  out_stats->process_peak_physical_footprint_bytes = std::max(
      out_stats->process_peak_physical_footprint_bytes,
      out_stats->process_physical_footprint_bytes);

  out_stats->graphic_cache_bytes = TVPGetGraphicCacheTotalBytes();
  out_stats->graphic_cache_limit_bytes = TVPGetGraphicCacheLimit();
  out_stats->xp3_segment_cache_bytes = TVPGetXP3SegmentCacheTotalBytes();

  out_stats->archive_cache_entries = TVPGetArchiveCacheCount();
  out_stats->archive_cache_limit = TVPGetArchiveCacheLimit();
  out_stats->autopath_cache_entries = TVPGetAutoPathCacheCount();
  out_stats->autopath_cache_limit = TVPGetAutoPathCacheLimit();
  out_stats->autopath_table_entries = TVPGetAutoPathTableCount();

  PSB::PSBMediaCacheStats psb_stats{};
  if (PSB::GetPSBMediaCacheStats(psb_stats)) {
    out_stats->psb_cache_bytes = psb_stats.bytesInUse;
    out_stats->psb_cache_entries = static_cast<uint32_t>(psb_stats.entryCount);
    out_stats->psb_cache_entry_limit =
        static_cast<uint32_t>(psb_stats.entryLimit);
    out_stats->psb_cache_hits = psb_stats.hitCount;
    out_stats->psb_cache_misses = psb_stats.missCount;
  }

  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_get_plugin_debug_info(
    engine_handle_t handle, char* out_buffer, uint32_t buffer_size,
    uint32_t* out_bytes_written) {
  if (out_buffer == nullptr || buffer_size == 0 || out_bytes_written == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "plugin debug output buffer is invalid");
  }
  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) return result;
  std::lock_guard<std::recursive_mutex> guard(impl->mutex);

  const auto snapshot = PluginCallTracer::Instance().GetDebugSnapshot();
  auto append_array = [](std::ostringstream& stream,
                         const std::vector<std::string>& items) {
    stream << '[';
    for (size_t index = 0; index < items.size(); ++index) {
      if (index != 0) stream << ',';
      stream << '"' << JsonEscape(items[index]) << '"';
    }
    stream << ']';
  };
  std::ostringstream json;
  json << "{\"tracing_enabled\":" << (snapshot.tracingEnabled ? "true" : "false")
       << ",\"method_calls\":" << snapshot.methodCalls
       << ",\"property_gets\":" << snapshot.propertyGets
       << ",\"property_sets\":" << snapshot.propertySets
       << ",\"load_succeeded\":" << snapshot.loadSucceeded
       << ",\"load_failed\":" << snapshot.loadFailed
       << ",\"load_fallback\":" << snapshot.loadFallback
       << ",\"missing_members\":" << snapshot.missingMembers
       << ",\"loaded_plugins\":";
  append_array(json, snapshot.loadedPlugins);
  json << ",\"failed_plugins\":";
  append_array(json, snapshot.failedPlugins);
  json << ",\"fallback_plugins\":";
  append_array(json, snapshot.fallbackPlugins);
  json << ",\"recent_missing_members\":";
  append_array(json, snapshot.recentMissingMembers);
  json << '}';

  const std::string payload = json.str();
  if (payload.size() + 1u > buffer_size) {
    *out_bytes_written = 0;
    out_buffer[0] = '\0';
    return SetHandleErrorAndReturnLocked(
        impl, ENGINE_RESULT_INVALID_ARGUMENT,
        "plugin debug output buffer is too small");
  }
  std::memcpy(out_buffer, payload.data(), payload.size());
  out_buffer[payload.size()] = '\0';
  *out_bytes_written = static_cast<uint32_t>(payload.size());
  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_set_diagnostic_config(
    engine_handle_t handle, const engine_diagnostic_config_t* config) {
  if (config == nullptr || config->struct_size < sizeof(*config)) {
    return SetThreadErrorAndReturn(
        ENGINE_RESULT_INVALID_ARGUMENT,
        "engine_diagnostic_config_t is null or too small");
  }
  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) return result;
  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  {
    std::lock_guard<std::mutex> diagnostic_guard(impl->diagnostics.mutex);
    auto& state = impl->diagnostics;
    state.enabled = config->enabled != 0;
    state.category_mask = config->category_mask & ENGINE_DIAGNOSTIC_CATEGORY_ALL;
    state.slow_frame_threshold_us = config->slow_frame_threshold_us;
    state.max_events = std::clamp<size_t>(config->max_events, 64u, 10000u);
    state.monotonic_offset_us = config->host_monotonic_origin_us > 0
        ? static_cast<int64_t>(config->host_monotonic_origin_us) -
              static_cast<int64_t>(SteadyMonotonicUs())
        : 0;
    state.session_id = config->session_id_utf8 != nullptr
        ? config->session_id_utf8 : "";
    state.sequence = 0;
    state.dropped = 0;
    state.events.clear();
  }
  if (config->enabled != 0) {
    PushDiagnosticEvent(impl, "engine", "lifecycle", "info",
                        "diagnostic_session_started", 0,
                        "{\"category_mask\":" +
                            std::to_string(config->category_mask) +
                            ",\"slow_frame_threshold_us\":" +
                            std::to_string(config->slow_frame_threshold_us) + "}");
  }
  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_mark_diagnostic_event(engine_handle_t handle,
                                             const char* label_utf8,
                                             uint64_t* out_sequence) {
  if (label_utf8 == nullptr || out_sequence == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "label_utf8 and out_sequence are required");
  }
  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) return result;
  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  bool enabled = false;
  {
    std::lock_guard<std::mutex> diagnostic_guard(impl->diagnostics.mutex);
    enabled = impl->diagnostics.enabled;
  }
  if (!enabled) {
    return SetHandleErrorAndReturnLocked(
        impl, ENGINE_RESULT_INVALID_STATE, "diagnostic session is not enabled");
  }
  *out_sequence = PushDiagnosticEvent(
      impl, "engine", "lifecycle", "info", "issue_marker", 0,
      "{\"label\":\"" + JsonEscape(label_utf8) + "\"}");
  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_drain_diagnostic_events(
    engine_handle_t handle, char* out_buffer, uint32_t buffer_size,
    uint32_t* out_bytes_written) {
  if (out_bytes_written == nullptr || out_buffer == nullptr || buffer_size == 0) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "diagnostic output buffer is invalid");
  }
  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) return result;
  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  uint32_t written = 0;
  {
    std::lock_guard<std::mutex> diagnostic_guard(impl->diagnostics.mutex);
    auto& events = impl->diagnostics.events;
    while (!events.empty()) {
      const std::string& line = events.front();
      const uint32_t needed = static_cast<uint32_t>(line.size() + 1u);
      if (written + needed >= buffer_size) break;
      std::memcpy(out_buffer + written, line.data(), line.size());
      written += static_cast<uint32_t>(line.size());
      out_buffer[written++] = '\n';
      events.pop_front();
    }
  }
  out_buffer[std::min<uint32_t>(written, buffer_size - 1u)] = '\0';
  *out_bytes_written = written;
  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

const char* engine_get_last_error(engine_handle_t handle) {
  if (handle == nullptr) {
    return g_thread_error.c_str();
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  if (!IsHandleLiveLocked(handle)) {
    SetThreadError("engine handle is invalid or already destroyed");
    return g_thread_error.c_str();
  }
  auto* impl = reinterpret_cast<engine_handle_s*>(handle);
  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  return impl->last_error.c_str();
}

}  // extern "C"

#else

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <deque>
#include <new>
#include <sstream>
#include <string>
#include <unordered_set>
#include <mutex>

struct engine_handle_s {
  std::recursive_mutex mutex;
  std::string last_error;
  int state = 0;
  uint32_t surface_width = 1280;
  uint32_t surface_height = 720;
  uint64_t frame_serial = 0;
  uint32_t startup_state = ENGINE_STARTUP_STATE_IDLE;
  std::deque<std::string> startup_logs;
  bool plugin_tracing_enabled = false;
  bool diagnostics_enabled = false;
  uint64_t diagnostic_sequence = 0;
  uint64_t diagnostic_dropped = 0;
  size_t diagnostic_max_events = 2000;
  int64_t diagnostic_monotonic_offset_us = 0;
  std::string diagnostic_session;
  std::deque<std::string> diagnostic_events;
};

namespace {

enum class EngineState {
  kCreated = 0,
  kOpened,
  kPaused,
  kDestroyed,
};

inline int ToStateValue(EngineState state) {
  return static_cast<int>(state);
}

std::recursive_mutex g_registry_mutex;
std::unordered_set<engine_handle_t> g_live_handles;
thread_local std::string g_thread_error;

void SetThreadError(const char* message) {
  g_thread_error = (message != nullptr) ? message : "";
}

engine_result_t SetThreadErrorAndReturn(engine_result_t result,
                                        const char* message) {
  SetThreadError(message);
  return result;
}

bool IsHandleLiveLocked(engine_handle_t handle) {
  return g_live_handles.find(handle) != g_live_handles.end();
}

engine_result_t ValidateHandleLocked(engine_handle_t handle,
                                     engine_handle_s** out_impl) {
  if (handle == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "engine handle is null");
  }
  if (!IsHandleLiveLocked(handle)) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "engine handle is invalid or already destroyed");
  }
  *out_impl = reinterpret_cast<engine_handle_s*>(handle);
  return ENGINE_RESULT_OK;
}

void SetHandleErrorLocked(engine_handle_s* impl, const char* message) {
  impl->last_error = (message != nullptr) ? message : "";
}

std::string StubJsonEscape(const std::string& value) {
  std::string result;
  for (const unsigned char c : value) {
    if (c == '\"') result += "\\\"";
    else if (c == '\\') result += "\\\\";
    else if (c == '\b') result += "\\b";
    else if (c == '\f') result += "\\f";
    else if (c == '\n') result += "\\n";
    else if (c == '\r') result += "\\r";
    else if (c == '\t') result += "\\t";
    else if (c < 0x20u) {
      char buffer[7] = {};
      std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
      result += buffer;
    } else result.push_back(static_cast<char>(c));
  }
  return result;
}

uint64_t StubPushDiagnostic(engine_handle_s* impl, const char* event,
                            const std::string& fields) {
  if (!impl->diagnostics_enabled) return 0;
  const uint64_t sequence = ++impl->diagnostic_sequence;
  while (impl->diagnostic_events.size() >= impl->diagnostic_max_events) {
    impl->diagnostic_events.pop_front();
    ++impl->diagnostic_dropped;
  }
  const auto raw_now = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  const auto now = std::max<int64_t>(
      0, raw_now + impl->diagnostic_monotonic_offset_us);
  std::ostringstream line;
  line << "{\"schema\":1,\"session\":\""
       << StubJsonEscape(impl->diagnostic_session)
       << "\",\"sequence\":" << sequence
       << ",\"monotonic_us\":" << now
       << ",\"platform\":\"stub\",\"layer\":\"engine\""
       << ",\"subsystem\":\"lifecycle\",\"level\":\"info\""
       << ",\"event\":\"" << event << "\",\"duration_us\":0"
       << ",\"queue_dropped\":" << impl->diagnostic_dropped
       << ",\"fields\":" << fields << "}";
  impl->diagnostic_events.push_back(line.str());
  return sequence;
}

}  // namespace

extern "C" {

engine_result_t engine_get_runtime_api_version(uint32_t* out_api_version) {
  if (out_api_version == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_api_version is null");
  }
  *out_api_version = ENGINE_API_VERSION;
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_create(const engine_create_desc_t* desc,
                              engine_handle_t* out_handle) {
  if (desc == nullptr || out_handle == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "engine_create requires non-null desc and out_handle");
  }

  if (desc->struct_size < sizeof(engine_create_desc_t)) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "engine_create_desc_t.struct_size is too small");
  }

  const uint32_t expected_major = (ENGINE_API_VERSION >> 24u) & 0xFFu;
  const uint32_t caller_major = (desc->api_version >> 24u) & 0xFFu;
  if (caller_major != expected_major) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_NOT_SUPPORTED,
                                   "unsupported engine API major version");
  }

  auto* impl = new (std::nothrow) engine_handle_s();
  if (impl == nullptr) {
    *out_handle = nullptr;
    return SetThreadErrorAndReturn(ENGINE_RESULT_INTERNAL_ERROR,
                                   "failed to allocate engine handle");
  }
  impl->state = ToStateValue(EngineState::kCreated);

  auto handle = reinterpret_cast<engine_handle_t>(impl);
  {
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    g_live_handles.insert(handle);
  }

  *out_handle = handle;
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_destroy(engine_handle_t handle) {
  if (handle == nullptr) {
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }

  engine_handle_s* impl = nullptr;
  {
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    auto it = g_live_handles.find(handle);
    if (it == g_live_handles.end()) {
      return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                     "engine handle is invalid or already destroyed");
    }
    impl = reinterpret_cast<engine_handle_s*>(handle);
    g_live_handles.erase(it);
  }

  {
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    impl->state = ToStateValue(EngineState::kDestroyed);
    impl->last_error.clear();
  }
  delete impl;
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_open_game(engine_handle_t handle,
                                 const char* game_root_path_utf8,
                                 const char* startup_script_utf8) {
  (void)startup_script_utf8;

  if (game_root_path_utf8 == nullptr || game_root_path_utf8[0] == '\0') {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "game_root_path_utf8 is null or empty");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->state == ToStateValue(EngineState::kDestroyed)) {
    SetHandleErrorLocked(impl, "engine is already destroyed");
    return ENGINE_RESULT_INVALID_STATE;
  }

  impl->state = ToStateValue(EngineState::kOpened);
  impl->startup_state = ENGINE_STARTUP_STATE_SUCCEEDED;
  impl->startup_logs.clear();
  impl->startup_logs.push_back("engine_open_game => OK");
  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_open_game_async(engine_handle_t handle,
                                       const char* game_root_path_utf8,
                                       const char* startup_script_utf8) {
  return engine_open_game(handle, game_root_path_utf8, startup_script_utf8);
}

engine_result_t engine_get_startup_state(engine_handle_t handle,
                                         uint32_t* out_state) {
  if (out_state == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_state is null");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }
  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  *out_state = impl->startup_state;
  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_drain_startup_logs(engine_handle_t handle,
                                          char* out_buffer,
                                          uint32_t buffer_size,
                                          uint32_t* out_bytes_written) {
  if (out_bytes_written == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_bytes_written is null");
  }
  if (out_buffer == nullptr || buffer_size == 0) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_buffer is null or buffer_size is 0");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  uint32_t written = 0;
  while (!impl->startup_logs.empty()) {
    const std::string line = impl->startup_logs.front();
    const uint32_t needed = static_cast<uint32_t>(line.size() + 1u);
    if (written + needed > buffer_size) {
      break;
    }
    std::memcpy(out_buffer + written, line.data(), line.size());
    written += static_cast<uint32_t>(line.size());
    out_buffer[written++] = '\n';
    impl->startup_logs.pop_front();
  }
  if (written < buffer_size) {
    out_buffer[written] = '\0';
  } else {
    out_buffer[buffer_size - 1] = '\0';
  }
  *out_bytes_written = written;
  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_tick(engine_handle_t handle, uint32_t delta_ms) {
  (void)delta_ms;

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->state == ToStateValue(EngineState::kPaused)) {
    SetHandleErrorLocked(impl, "engine is paused");
    return ENGINE_RESULT_INVALID_STATE;
  }
  if (impl->state != ToStateValue(EngineState::kOpened)) {
    SetHandleErrorLocked(impl, "engine_open_game must succeed before engine_tick");
    return ENGINE_RESULT_INVALID_STATE;
  }

  impl->frame_serial += 1;
  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_pause(engine_handle_t handle) {
  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->state == ToStateValue(EngineState::kPaused)) {
    impl->last_error.clear();
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }
  if (impl->state != ToStateValue(EngineState::kOpened)) {
    SetHandleErrorLocked(impl, "engine_pause requires opened state");
    return ENGINE_RESULT_INVALID_STATE;
  }

  impl->state = ToStateValue(EngineState::kPaused);
  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_resume(engine_handle_t handle) {
  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->state == ToStateValue(EngineState::kOpened)) {
    impl->last_error.clear();
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }
  if (impl->state != ToStateValue(EngineState::kPaused)) {
    SetHandleErrorLocked(impl, "engine_resume requires paused state");
    return ENGINE_RESULT_INVALID_STATE;
  }

  impl->state = ToStateValue(EngineState::kOpened);
  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_set_option(engine_handle_t handle,
                                  const engine_option_t* option) {
  if (option == nullptr || option->key_utf8 == nullptr || option->key_utf8[0] == '\0') {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "option and option->key_utf8 must be non-null/non-empty");
  }
  if (option->value_utf8 == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "option->value_utf8 must be non-null");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->state == ToStateValue(EngineState::kDestroyed)) {
    SetHandleErrorLocked(impl, "engine is already destroyed");
    return ENGINE_RESULT_INVALID_STATE;
  }

  if (std::strcmp(option->key_utf8, "plugin_trace") == 0) {
    const std::string value(option->value_utf8);
    impl->plugin_tracing_enabled = value == "1" || value == "true";
  }

  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_set_surface_size(engine_handle_t handle,
                                        uint32_t width,
                                        uint32_t height) {
  if (width == 0 || height == 0) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "width and height must be > 0");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->state == ToStateValue(EngineState::kDestroyed)) {
    SetHandleErrorLocked(impl, "engine is already destroyed");
    return ENGINE_RESULT_INVALID_STATE;
  }

  impl->surface_width = width;
  impl->surface_height = height;
  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_get_frame_desc(engine_handle_t handle,
                                      engine_frame_desc_t* out_frame_desc) {
  if (out_frame_desc == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_frame_desc is null");
  }
  if (out_frame_desc->struct_size < sizeof(engine_frame_desc_t)) {
    return SetThreadErrorAndReturn(
        ENGINE_RESULT_INVALID_ARGUMENT,
        "engine_frame_desc_t.struct_size is too small");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->state == ToStateValue(EngineState::kDestroyed)) {
    SetHandleErrorLocked(impl, "engine is already destroyed");
    return ENGINE_RESULT_INVALID_STATE;
  }

  std::memset(out_frame_desc, 0, sizeof(*out_frame_desc));
  out_frame_desc->struct_size = sizeof(engine_frame_desc_t);
  out_frame_desc->width = impl->surface_width;
  out_frame_desc->height = impl->surface_height;
  out_frame_desc->stride_bytes = impl->surface_width * 4u;
  out_frame_desc->pixel_format = ENGINE_PIXEL_FORMAT_RGBA8888;
  out_frame_desc->frame_serial = impl->frame_serial;

  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_read_frame_rgba(engine_handle_t handle,
                                       void* out_pixels,
                                       size_t out_pixels_size) {
  if (out_pixels == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_pixels is null");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->state != ToStateValue(EngineState::kOpened) &&
      impl->state != ToStateValue(EngineState::kPaused)) {
    SetHandleErrorLocked(impl,
                         "engine_open_game must succeed before engine_read_frame_rgba");
    return ENGINE_RESULT_INVALID_STATE;
  }

  const size_t required_size =
      static_cast<size_t>(impl->surface_width) *
      static_cast<size_t>(impl->surface_height) * 4u;
  if (out_pixels_size < required_size) {
    SetHandleErrorLocked(
        impl,
        "out_pixels_size is smaller than required frame buffer size");
    return ENGINE_RESULT_INVALID_ARGUMENT;
  }

  std::memset(out_pixels, 0, required_size);
  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_media_open(engine_handle_t engine,
                                  const char* path_utf8,
                                  engine_media_handle_t* out_media) {
  (void)engine;
  (void)path_utf8;
  if (out_media != nullptr) *out_media = nullptr;
  return SetThreadErrorAndReturn(
      ENGINE_RESULT_NOT_SUPPORTED,
      "standalone media playback is not supported in stub builds");
}

engine_result_t engine_media_destroy(engine_media_handle_t media) {
  (void)media;
  return ENGINE_RESULT_OK;
}

engine_result_t engine_media_play(engine_media_handle_t media) {
  (void)media;
  return SetThreadErrorAndReturn(ENGINE_RESULT_NOT_SUPPORTED,
                                 "media playback is unavailable");
}

engine_result_t engine_media_pause(engine_media_handle_t media) {
  (void)media;
  return SetThreadErrorAndReturn(ENGINE_RESULT_NOT_SUPPORTED,
                                 "media playback is unavailable");
}

engine_result_t engine_media_seek(engine_media_handle_t media,
                                  int64_t position_ms) {
  (void)media;
  (void)position_ms;
  return SetThreadErrorAndReturn(ENGINE_RESULT_NOT_SUPPORTED,
                                 "media playback is unavailable");
}

engine_result_t engine_media_set_rate(engine_media_handle_t media,
                                      double playback_rate) {
  (void)media;
  (void)playback_rate;
  return SetThreadErrorAndReturn(ENGINE_RESULT_NOT_SUPPORTED,
                                 "media playback is unavailable");
}

engine_result_t engine_media_get_state(engine_media_handle_t media,
                                       engine_media_state_t* out_state) {
  (void)media;
  if (out_state != nullptr &&
      out_state->struct_size >= sizeof(engine_media_state_t)) {
    std::memset(out_state, 0, sizeof(*out_state));
    out_state->struct_size = sizeof(*out_state);
    out_state->status = ENGINE_MEDIA_STATUS_ERROR;
  }
  return SetThreadErrorAndReturn(ENGINE_RESULT_NOT_SUPPORTED,
                                 "media playback is unavailable");
}

engine_result_t engine_media_get_subtitle_tracks_json(
    engine_media_handle_t media, char* out_buffer, uint32_t buffer_size,
    uint32_t* out_bytes_written) {
  (void)media;
  if (out_buffer != nullptr && buffer_size > 0) out_buffer[0] = '\0';
  if (out_bytes_written != nullptr) *out_bytes_written = 0;
  return SetThreadErrorAndReturn(ENGINE_RESULT_NOT_SUPPORTED,
                                 "media subtitles are unavailable");
}

engine_result_t engine_media_extract_subtitle(
    engine_media_handle_t media, int32_t stream_index,
    const char* output_path_utf8) {
  (void)media;
  (void)stream_index;
  (void)output_path_utf8;
  return SetThreadErrorAndReturn(ENGINE_RESULT_NOT_SUPPORTED,
                                 "media subtitles are unavailable");
}

engine_result_t engine_media_read_frame_rgba(
    engine_media_handle_t media, void* out_pixels, size_t out_pixels_size,
    engine_frame_desc_t* out_frame_desc) {
  (void)media;
  (void)out_pixels;
  (void)out_pixels_size;
  (void)out_frame_desc;
  return SetThreadErrorAndReturn(ENGINE_RESULT_NOT_SUPPORTED,
                                 "media playback is unavailable");
}

engine_result_t engine_get_host_native_window(engine_handle_t handle,
                                              void** out_window_handle) {
  if (out_window_handle == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_window_handle is null");
  }
  *out_window_handle = nullptr;

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->state != ToStateValue(EngineState::kOpened) &&
      impl->state != ToStateValue(EngineState::kPaused)) {
    SetHandleErrorLocked(
        impl,
        "engine_open_game must succeed before engine_get_host_native_window");
    return ENGINE_RESULT_INVALID_STATE;
  }

  SetHandleErrorLocked(impl,
                       "engine_get_host_native_window is not supported");
  return ENGINE_RESULT_NOT_SUPPORTED;
}

engine_result_t engine_get_host_native_view(engine_handle_t handle,
                                            void** out_view_handle) {
  if (out_view_handle == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_view_handle is null");
  }
  *out_view_handle = nullptr;

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->state != ToStateValue(EngineState::kOpened) &&
      impl->state != ToStateValue(EngineState::kPaused)) {
    SetHandleErrorLocked(
        impl,
        "engine_open_game must succeed before engine_get_host_native_view");
    return ENGINE_RESULT_INVALID_STATE;
  }

  SetHandleErrorLocked(impl, "engine_get_host_native_view is not supported");
  return ENGINE_RESULT_NOT_SUPPORTED;
}

engine_result_t engine_send_input(engine_handle_t handle,
                                  const engine_input_event_t* event) {
  if (event == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "event is null");
  }
  if (event->struct_size < sizeof(engine_input_event_t)) {
    return SetThreadErrorAndReturn(
        ENGINE_RESULT_INVALID_ARGUMENT,
        "engine_input_event_t.struct_size is too small");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->state == ToStateValue(EngineState::kPaused)) {
    SetHandleErrorLocked(impl, "engine is paused");
    return ENGINE_RESULT_INVALID_STATE;
  }
  if (impl->state != ToStateValue(EngineState::kOpened)) {
    SetHandleErrorLocked(impl,
                         "engine_open_game must succeed before engine_send_input");
    return ENGINE_RESULT_INVALID_STATE;
  }

  switch (event->type) {
    case ENGINE_INPUT_EVENT_POINTER_DOWN:
    case ENGINE_INPUT_EVENT_POINTER_MOVE:
    case ENGINE_INPUT_EVENT_POINTER_UP:
    case ENGINE_INPUT_EVENT_POINTER_SCROLL:
    case ENGINE_INPUT_EVENT_KEY_DOWN:
    case ENGINE_INPUT_EVENT_KEY_UP:
    case ENGINE_INPUT_EVENT_TEXT_INPUT:
    case ENGINE_INPUT_EVENT_BACK:
      break;
    default:
      SetHandleErrorLocked(impl, "unsupported input event type");
      return ENGINE_RESULT_NOT_SUPPORTED;
  }

  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_get_text_input_state(
    engine_handle_t handle, engine_text_input_state_t* out_state) {
  if (out_state == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_state is null");
  }
  if (out_state->struct_size < sizeof(engine_text_input_state_t)) {
    return SetThreadErrorAndReturn(
        ENGINE_RESULT_INVALID_ARGUMENT,
        "engine_text_input_state_t.struct_size is too small");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->state != ToStateValue(EngineState::kOpened) &&
      impl->state != ToStateValue(EngineState::kPaused)) {
    SetHandleErrorLocked(
        impl,
        "engine_open_game must succeed before engine_get_text_input_state");
    return ENGINE_RESULT_INVALID_STATE;
  }

  engine_text_input_state_t snapshot{};
  snapshot.struct_size = sizeof(snapshot);
  *out_state = snapshot;
  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_copy_text_input_text(
    engine_handle_t handle, char* out_buffer, uint32_t buffer_size,
    uint32_t* out_bytes_written) {
  if (out_buffer == nullptr || buffer_size == 0 ||
      out_bytes_written == nullptr) {
    return SetThreadErrorAndReturn(
        ENGINE_RESULT_INVALID_ARGUMENT,
        "engine_copy_text_input_text requires a buffer and byte count");
  }
  out_buffer[0] = '\0';
  *out_bytes_written = 0;

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->state != ToStateValue(EngineState::kOpened) &&
      impl->state != ToStateValue(EngineState::kPaused)) {
    SetHandleErrorLocked(
        impl,
        "engine_open_game must succeed before engine_copy_text_input_text");
    return ENGINE_RESULT_INVALID_STATE;
  }

  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_get_main_menu_json(engine_handle_t handle,
                                          char* out_buffer,
                                          uint32_t buffer_size,
                                          uint32_t* out_bytes_written) {
  (void)handle;
  if (out_bytes_written == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_bytes_written is null");
  }
  if (out_buffer == nullptr || buffer_size == 0) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_buffer is null or buffer_size is 0");
  }

  const char* empty_menu = "[]";
  const uint32_t copy_bytes = static_cast<uint32_t>(
      std::min<size_t>(2u, static_cast<size_t>(buffer_size - 1)));
  if (copy_bytes > 0) {
    std::memcpy(out_buffer, empty_menu, copy_bytes);
  }
  out_buffer[copy_bytes] = '\0';
  *out_bytes_written = copy_bytes;
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_activate_menu_item(engine_handle_t handle,
                                          const char* item_path_utf8) {
  (void)handle;
  if (item_path_utf8 == nullptr || item_path_utf8[0] == '\0') {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "item_path_utf8 is null or empty");
  }
  return SetThreadErrorAndReturn(ENGINE_RESULT_NOT_SUPPORTED,
                                 "engine_activate_menu_item is not supported in stub build");
}

engine_result_t engine_set_render_target_iosurface(engine_handle_t handle,
                                                    uint32_t iosurface_id,
                                                    uint32_t width,
                                                    uint32_t height) {
  (void)iosurface_id;
  (void)width;
  (void)height;

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  SetHandleErrorLocked(impl,
                       "engine_set_render_target_iosurface is not supported in stub build");
  return ENGINE_RESULT_NOT_SUPPORTED;
}

engine_result_t engine_set_render_target_surface(engine_handle_t handle,
                                                  void* native_window,
                                                  uint32_t width,
                                                  uint32_t height) {
  (void)native_window;
  (void)width;
  (void)height;

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  SetHandleErrorLocked(impl,
                       "engine_set_render_target_surface is not supported in stub build");
  return ENGINE_RESULT_NOT_SUPPORTED;
}

engine_result_t engine_get_frame_rendered_flag(engine_handle_t handle,
                                                uint32_t* out_flag) {
  if (out_flag == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_flag is null");
  }
  *out_flag = 0;

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_get_renderer_info(engine_handle_t handle,
                                         char* out_buffer,
                                         uint32_t buffer_size) {
  (void)handle;
  if (out_buffer == nullptr || buffer_size == 0) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_buffer is null or buffer_size is 0");
  }
  out_buffer[0] = '\0';

  // Stub build — return a placeholder string.
  const char* stub_info = "Stub (no runtime)";
  std::strncpy(out_buffer, stub_info, buffer_size - 1);
  out_buffer[buffer_size - 1] = '\0';
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_get_memory_stats(engine_handle_t handle,
                                        engine_memory_stats_t* out_stats) {
  if (out_stats == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_stats is null");
  }
  if (out_stats->struct_size < sizeof(engine_memory_stats_t)) {
    return SetThreadErrorAndReturn(
        ENGINE_RESULT_INVALID_ARGUMENT,
        "engine_memory_stats_t.struct_size is too small");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::memset(out_stats, 0, sizeof(*out_stats));
  out_stats->struct_size = sizeof(engine_memory_stats_t);
  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_get_plugin_debug_info(
    engine_handle_t handle, char* out_buffer, uint32_t buffer_size,
    uint32_t* out_bytes_written) {
  if (out_buffer == nullptr || buffer_size == 0 || out_bytes_written == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "plugin debug output buffer is invalid");
  }
  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) return result;
  std::string payload = "{\"tracing_enabled\":";
  payload += impl->plugin_tracing_enabled ? "true" : "false";
  payload +=
      ",\"method_calls\":0,\"property_gets\":0,"
      "\"property_sets\":0,\"load_succeeded\":0,\"load_failed\":0,"
      "\"load_fallback\":0,\"missing_members\":0,\"loaded_plugins\":[],"
      "\"failed_plugins\":[],\"fallback_plugins\":[],"
      "\"recent_missing_members\":[]}";
  const size_t length = payload.size();
  if (length + 1u > buffer_size) {
    *out_bytes_written = 0;
    out_buffer[0] = '\0';
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "plugin debug output buffer is too small");
  }
  std::memcpy(out_buffer, payload.c_str(), length + 1u);
  *out_bytes_written = static_cast<uint32_t>(length);
  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_set_diagnostic_config(
    engine_handle_t handle, const engine_diagnostic_config_t* config) {
  if (config == nullptr || config->struct_size < sizeof(*config)) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "engine_diagnostic_config_t is invalid");
  }
  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) return result;
  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  impl->diagnostics_enabled = config->enabled != 0;
  impl->diagnostic_sequence = 0;
  impl->diagnostic_dropped = 0;
  impl->diagnostic_max_events = std::clamp<size_t>(config->max_events, 64u, 10000u);
  const auto raw_now = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  impl->diagnostic_monotonic_offset_us = config->host_monotonic_origin_us > 0
      ? static_cast<int64_t>(config->host_monotonic_origin_us) - raw_now
      : 0;
  impl->diagnostic_session = config->session_id_utf8 != nullptr
      ? config->session_id_utf8 : "";
  impl->diagnostic_events.clear();
  if (impl->diagnostics_enabled) {
    StubPushDiagnostic(impl, "diagnostic_session_started", "{}");
  }
  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_mark_diagnostic_event(engine_handle_t handle,
                                             const char* label_utf8,
                                             uint64_t* out_sequence) {
  if (label_utf8 == nullptr || out_sequence == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "label_utf8 and out_sequence are required");
  }
  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) return result;
  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (!impl->diagnostics_enabled) {
    SetHandleErrorLocked(impl, "diagnostic session is not enabled");
    return ENGINE_RESULT_INVALID_STATE;
  }
  *out_sequence = StubPushDiagnostic(
      impl, "issue_marker",
      "{\"label\":\"" + StubJsonEscape(label_utf8) + "\"}");
  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_drain_diagnostic_events(
    engine_handle_t handle, char* out_buffer, uint32_t buffer_size,
    uint32_t* out_bytes_written) {
  if (out_bytes_written == nullptr || out_buffer == nullptr || buffer_size == 0) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "diagnostic output buffer is invalid");
  }
  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) return result;
  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  uint32_t written = 0;
  while (!impl->diagnostic_events.empty()) {
    const std::string& line = impl->diagnostic_events.front();
    const uint32_t needed = static_cast<uint32_t>(line.size() + 1u);
    if (written + needed >= buffer_size) break;
    std::memcpy(out_buffer + written, line.data(), line.size());
    written += static_cast<uint32_t>(line.size());
    out_buffer[written++] = '\n';
    impl->diagnostic_events.pop_front();
  }
  out_buffer[std::min<uint32_t>(written, buffer_size - 1u)] = '\0';
  *out_bytes_written = written;
  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

const char* engine_get_last_error(engine_handle_t handle) {
  if (handle == nullptr) {
    return g_thread_error.c_str();
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  if (!IsHandleLiveLocked(handle)) {
    SetThreadError("engine handle is invalid or already destroyed");
    return g_thread_error.c_str();
  }
  auto* impl = reinterpret_cast<engine_handle_s*>(handle);
  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  return impl->last_error.c_str();
}

}  // extern "C"

#endif
