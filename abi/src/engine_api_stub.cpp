#include "legacy_engine_api_rename.h"
#include "engine_api.h"
#include "engine_input_queue_gate.h"


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

bool engine_legacy_activate_audio_session_for_host(void) {
  // The stub host has no KiriKiri audio session to activate.
  return true;
}

void engine_legacy_drain_texture_recycle(void) {
  // The stub host owns no shared KiriKiri render-manager textures.
}

engine_result_t engine_legacy_get_godot_native_frame_texture(
    engine_handle_t handle, uint64_t* out_texture_id, uint32_t* out_width,
    uint32_t* out_height, uint64_t* out_frame_serial) {
  (void)handle;
  (void)out_texture_id;
  (void)out_width;
  (void)out_height;
  (void)out_frame_serial;
  return ENGINE_RESULT_NOT_SUPPORTED;
}

}  // extern "C"


#include "engine_legacy_services.h"

/* The stub ships its engine_legacy_* surface as an installable services table
 * so the dispatch layer stays free of direct symbol links. The diagnostics
 * unit tests install it before the first engine_create call. */
extern "C" const engine_legacy_services_v1_t*
engine_stub_legacy_services(void) {
  static const engine_legacy_services_v1_t services = {
      sizeof(engine_legacy_services_v1_t),
      &engine_legacy_get_runtime_api_version,
      &engine_legacy_create,
      &engine_legacy_destroy,
      &engine_legacy_open_game,
      &engine_legacy_open_game_async,
      &engine_legacy_get_startup_state,
      &engine_legacy_drain_startup_logs,
      &engine_legacy_tick,
      &engine_legacy_pause,
      &engine_legacy_resume,
      &engine_legacy_set_option,
      &engine_legacy_set_surface_size,
      &engine_legacy_get_frame_desc,
      &engine_legacy_read_frame_rgba,
      &engine_legacy_media_open,
      &engine_legacy_media_destroy,
      &engine_legacy_media_play,
      &engine_legacy_media_pause,
      &engine_legacy_media_seek,
      &engine_legacy_media_set_rate,
      &engine_legacy_media_get_state,
      &engine_legacy_media_get_subtitle_tracks_json,
      &engine_legacy_media_extract_subtitle,
      &engine_legacy_media_read_frame_rgba,
      &engine_legacy_get_godot_native_frame_texture,
      &engine_legacy_get_host_native_window,
      &engine_legacy_get_host_native_view,
      &engine_legacy_send_input,
      &engine_legacy_get_text_input_state,
      &engine_legacy_copy_text_input_text,
      &engine_legacy_get_main_menu_json,
      &engine_legacy_activate_menu_item,
      &engine_legacy_set_render_target_iosurface,
      &engine_legacy_set_render_target_surface,
      &engine_legacy_get_frame_rendered_flag,
      &engine_legacy_get_renderer_info,
      &engine_legacy_get_memory_stats,
      &engine_legacy_get_plugin_debug_info,
      &engine_legacy_set_diagnostic_config,
      &engine_legacy_mark_diagnostic_event,
      &engine_legacy_drain_diagnostic_events,
      &engine_legacy_get_last_error,
      &engine_legacy_activate_audio_session_for_host,
      &engine_legacy_drain_texture_recycle,
      /*register_private_runtimes=*/nullptr,
  };
  return &services;
}

extern "C" engine_result_t engine_stub_install_legacy_services(void) {
  return engine_install_legacy_services(engine_stub_legacy_services());
}
