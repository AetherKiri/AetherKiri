#include "engine_api.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(__ANDROID__)
#include <android/log.h>
#define AETHER_DISPATCH_DIAG_LOG(message) \
  __android_log_print(ANDROID_LOG_INFO, "AetherArtemisDiag", "%s", message)
#else
#define AETHER_DISPATCH_DIAG_LOG(message) ((void)0)
#endif

#include "engine_runtime_provider_registry.h"
#include "legacy_engine_api.h"
#if defined(ENGINE_API_USE_KRKR2_RUNTIME)
#include "environ/Platform.h"
#include "visual/RenderManager.h"
#endif

#if defined(AETHERKIRI_INTERNAL_ARTEMIS) && \
    defined(AETHERKIRI_ENABLE_ARTEMIS_RUNTIME)
extern "C" void AetherInternalRegisterArtemisRuntime(void);
#endif

namespace {

enum class BackendKind { kUndecided, kLegacy, kProvider };

struct DispatchHandle {
  std::recursive_mutex mutex;
  engine_handle_t legacy = nullptr;
  BackendKind backend = BackendKind::kUndecided;
  const engine_runtime_provider_v1_t* provider = nullptr;
  void* runtime = nullptr;
  engine_create_desc_t create_desc{};
  std::string writable_path;
  std::string cache_path;
  std::string requested_runtime = "auto";
#if defined(NDEBUG) && !defined(__ANDROID__)
  bool artemis_beta_allowed = false;
#else
  bool artemis_beta_allowed = true;
#endif
  std::unordered_map<std::string, std::string> pending_options;
  uint32_t surface_width = 0;
  uint32_t surface_height = 0;
  bool has_surface_size = false;
  engine_runtime_host_v1_t host{};
  engine_runtime_fragment_shader_host_v1_t fragment_shader_host{};
  std::thread startup_thread;
  uint32_t startup_state = ENGINE_STARTUP_STATE_IDLE;
  std::deque<std::string> startup_logs;
  struct PlatformRequest {
    std::string operation;
    std::string argument;
  };
  std::deque<PlatformRequest> platform_requests;
  std::string last_error;
  bool provider_resume_pending = false;
};

std::recursive_mutex g_dispatch_registry_mutex;
std::unordered_set<engine_handle_t> g_dispatch_handles;
std::unordered_map<engine_media_handle_t, engine_handle_t>
    g_dispatch_media_handles;
thread_local std::string g_dispatch_thread_error;

bool ActivateProviderAudioSessionForHost() {
#if defined(__APPLE__) && TARGET_OS_IPHONE && \
    defined(ENGINE_API_USE_KRKR2_RUNTIME)
  return TVPActivateAudioSessionForHost();
#else
  return true;
#endif
}

#define PROVIDER_HAS(provider, member)                                      \
  ((provider) != nullptr &&                                                  \
   (provider)->struct_size >=                                                \
       offsetof(engine_runtime_provider_v1_t, member) +                      \
           sizeof(((engine_runtime_provider_v1_t*)0)->member) &&             \
   (provider)->member != nullptr)

std::string Normalize(const char* value) {
  std::string normalized = value != nullptr ? value : "";
  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return normalized;
}

void SetThreadError(const char* message) {
  g_dispatch_thread_error = message != nullptr ? message : "";
}

engine_result_t ThreadError(engine_result_t result, const char* message) {
  SetThreadError(message);
  return result;
}

DispatchHandle* Cast(engine_handle_t handle) {
  return reinterpret_cast<DispatchHandle*>(handle);
}

engine_result_t ValidateHandleLocked(engine_handle_t handle,
                                     DispatchHandle** out_handle) {
  if (handle == nullptr) {
    return ThreadError(ENGINE_RESULT_INVALID_ARGUMENT, "engine handle is null");
  }
  if (g_dispatch_handles.find(handle) == g_dispatch_handles.end()) {
    return ThreadError(ENGINE_RESULT_INVALID_ARGUMENT,
                       "engine handle is invalid or already destroyed");
  }
  *out_handle = Cast(handle);
  return ENGINE_RESULT_OK;
}

void SetProviderError(DispatchHandle* handle, engine_result_t result,
                      const char* fallback) {
  if (result == ENGINE_RESULT_OK) {
    handle->last_error.clear();
    SetThreadError(nullptr);
    return;
  }
  const char* provider_error = nullptr;
  if (PROVIDER_HAS(handle->provider, get_last_error)) {
    provider_error = handle->provider->get_last_error(handle->runtime);
  }
  handle->last_error = provider_error != nullptr && provider_error[0] != '\0'
                           ? provider_error
                           : fallback;
  SetThreadError(handle->last_error.c_str());
}

void SetLegacyError(DispatchHandle* handle, engine_result_t result,
                    const char* fallback) {
  if (result == ENGINE_RESULT_OK) {
    handle->last_error.clear();
    SetThreadError(nullptr);
    return;
  }
  const char* legacy_error = engine_legacy_get_last_error(handle->legacy);
  if (legacy_error == nullptr || legacy_error[0] == '\0') {
    legacy_error = engine_legacy_get_last_error(nullptr);
  }
  handle->last_error = legacy_error != nullptr && legacy_error[0] != '\0'
                           ? legacy_error
                           : fallback;
  SetThreadError(handle->last_error.c_str());
}

engine_result_t Unsupported(DispatchHandle* handle, const char* operation) {
  handle->last_error = std::string("runtime provider does not implement ") + operation;
  SetThreadError(handle->last_error.c_str());
  return ENGINE_RESULT_NOT_SUPPORTED;
}

engine_result_t CheckArtemisBetaAccess(DispatchHandle* handle) {
  if (handle->backend != BackendKind::kProvider || handle->provider == nullptr ||
      Normalize(handle->provider->runtime_id_utf8) != "artemis" ||
      handle->artemis_beta_allowed) {
    return ENGINE_RESULT_OK;
  }
  handle->last_error = "Artemis runtime requires active beta access";
  return ThreadError(ENGINE_RESULT_NOT_SUPPORTED, handle->last_error.c_str());
}

void HostLog(void* user_data, uint32_t level, const char* subsystem,
             const char* message) {
  auto* handle = static_cast<DispatchHandle*>(user_data);
  if (handle == nullptr) return;
  std::lock_guard<std::recursive_mutex> guard(handle->mutex);
  std::string line;
  switch (level) {
    case ENGINE_RUNTIME_LOG_ERROR: line = "error "; break;
    case ENGINE_RUNTIME_LOG_WARNING: line = "warning "; break;
    case ENGINE_RUNTIME_LOG_DEBUG: line = "debug "; break;
    case ENGINE_RUNTIME_LOG_TRACE: line = "trace "; break;
    default: line = "info "; break;
  }
  if (subsystem != nullptr && subsystem[0] != '\0') {
    line += "[";
    line += subsystem;
    line += "] ";
  }
  line += message != nullptr ? message : "";
  handle->startup_logs.push_back(std::move(line));
}

uint64_t HostMonotonicTimeMicros(void*) {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch())
                                   .count());
}

void HostPlatformRequest(void* user_data, const char* operation,
                         const char* argument) {
  auto* handle = static_cast<DispatchHandle*>(user_data);
  if (handle == nullptr || operation == nullptr || operation[0] == '\0') {
    return;
  }
  std::lock_guard<std::recursive_mutex> guard(handle->mutex);
  // Bound hostile or broken script spam without losing the newest request.
  if (handle->platform_requests.size() >= 256u) {
    handle->platform_requests.pop_front();
  }
  handle->platform_requests.push_back(
      {operation, argument != nullptr ? argument : ""});
}

engine_result_t SelectBackendLocked(DispatchHandle* handle,
                                    const char* game_root_path_utf8) {
  if (handle->backend != BackendKind::kUndecided) return ENGINE_RESULT_OK;

  const std::string requested = Normalize(handle->requested_runtime.c_str());
  if (requested.empty() || requested == "auto") {
    const auto providers = aetherkiri::runtime::SnapshotProviders();
    const aetherkiri::runtime::RegisteredProvider* selected = nullptr;
    int32_t selected_score = 0;
    for (const auto& candidate : providers) {
      int32_t score = 0;
      try {
        score = candidate.api->probe(candidate.api->provider_user_data,
                                     game_root_path_utf8);
      } catch (...) {
        score = 0;
      }
      if (score <= 0) continue;
      if (selected == nullptr || score > selected_score ||
          (score == selected_score &&
           candidate.api->priority > selected->api->priority) ||
          (score == selected_score &&
           candidate.api->priority == selected->api->priority &&
           candidate.registration_order < selected->registration_order)) {
        selected = &candidate;
        selected_score = score;
      }
    }
    if (selected != nullptr) handle->provider = selected->api;
  } else if (requested == "kirikiri" || requested == "legacy") {
    handle->backend = BackendKind::kLegacy;
    return ENGINE_RESULT_OK;
  } else {
    const auto providers = aetherkiri::runtime::SnapshotProviders();
    const auto found = std::find_if(
        providers.begin(), providers.end(), [&](const auto& candidate) {
          return candidate.runtime_id == requested;
        });
    if (found == providers.end()) {
      handle->last_error = "requested runtime provider is not registered: " + requested;
      SetThreadError(handle->last_error.c_str());
      return ENGINE_RESULT_NOT_SUPPORTED;
    }
    handle->provider = found->api;
  }

  if (handle->provider == nullptr) {
    handle->backend = BackendKind::kLegacy;
    return ENGINE_RESULT_OK;
  }

  void* runtime = nullptr;
  engine_result_t result = ENGINE_RESULT_INTERNAL_ERROR;
  try {
    AETHER_DISPATCH_DIAG_LOG("SelectBackend before provider create");
    result = handle->provider->create(handle->provider->provider_user_data,
                                      &handle->host, &handle->create_desc,
                                      &runtime);
    AETHER_DISPATCH_DIAG_LOG("SelectBackend after provider create");
  } catch (...) {
    result = ENGINE_RESULT_INTERNAL_ERROR;
  }
  handle->runtime = runtime;
  if (result != ENGINE_RESULT_OK || runtime == nullptr) {
    SetProviderError(handle,
                     result == ENGINE_RESULT_OK ? ENGINE_RESULT_INTERNAL_ERROR : result,
                     "runtime provider creation failed");
    if (runtime != nullptr) handle->provider->destroy(runtime);
    handle->runtime = nullptr;
    return result == ENGINE_RESULT_OK ? ENGINE_RESULT_INTERNAL_ERROR : result;
  }
  handle->backend = BackendKind::kProvider;

  if (PROVIDER_HAS(handle->provider, set_option)) {
    for (const auto& option_value : handle->pending_options) {
      engine_option_t option{};
      option.key_utf8 = option_value.first.c_str();
      option.value_utf8 = option_value.second.c_str();
      result = handle->provider->set_option(handle->runtime, &option);
      if (result != ENGINE_RESULT_OK) {
        SetProviderError(handle, result, "runtime provider rejected an option");
        return result;
      }
    }
  }
  if (handle->has_surface_size) {
    if (!PROVIDER_HAS(handle->provider, set_surface_size)) {
      return Unsupported(handle, "set_surface_size");
    }
    result = handle->provider->set_surface_size(
        handle->runtime, handle->surface_width, handle->surface_height);
    if (result != ENGINE_RESULT_OK) {
      SetProviderError(handle, result,
                       "runtime provider rejected the render surface size");
      return result;
    }
  }
  handle->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

template <typename LegacyCall, typename ProviderCall>
engine_result_t Route(engine_handle_t public_handle, const char* operation,
                      LegacyCall&& legacy_call, ProviderCall&& provider_call) {
  std::lock_guard<std::recursive_mutex> registry_guard(g_dispatch_registry_mutex);
  DispatchHandle* handle = nullptr;
  auto result = ValidateHandleLocked(public_handle, &handle);
  if (result != ENGINE_RESULT_OK) return result;
  std::lock_guard<std::recursive_mutex> guard(handle->mutex);
  if (handle->backend != BackendKind::kProvider) {
    return legacy_call(handle->legacy);
  }
  result = provider_call(handle);
  if (result == ENGINE_RESULT_NOT_SUPPORTED) return Unsupported(handle, operation);
  SetProviderError(handle, result, operation);
  return result;
}

template <typename LegacyCall>
engine_result_t RouteMedia(engine_media_handle_t media, const char* operation,
                           LegacyCall&& legacy_call) {
  if (media == nullptr) {
    return ThreadError(ENGINE_RESULT_INVALID_ARGUMENT, "media handle is null");
  }
  std::lock_guard<std::recursive_mutex> registry_guard(
      g_dispatch_registry_mutex);
  const auto found = g_dispatch_media_handles.find(media);
  if (found == g_dispatch_media_handles.end()) {
    return ThreadError(ENGINE_RESULT_INVALID_ARGUMENT,
                       "media handle is invalid or already destroyed");
  }
  DispatchHandle* owner = nullptr;
  const auto validation = ValidateHandleLocked(found->second, &owner);
  if (validation != ENGINE_RESULT_OK) return validation;
  std::lock_guard<std::recursive_mutex> guard(owner->mutex);
  const auto result = legacy_call(media);
  SetLegacyError(owner, result, operation);
  return result;
}

engine_result_t DrainProviderStartupLogs(DispatchHandle* handle, char* out_buffer,
                                         uint32_t buffer_size,
                                         uint32_t* out_bytes_written) {
  if (out_bytes_written == nullptr || out_buffer == nullptr || buffer_size == 0) {
    return ENGINE_RESULT_INVALID_ARGUMENT;
  }
  uint32_t written = 0;
  while (!handle->startup_logs.empty()) {
    const std::string& line = handle->startup_logs.front();
    const size_t needed = line.size() + 1u;
    if (needed > buffer_size - written) break;
    std::memcpy(out_buffer + written, line.data(), line.size());
    written += static_cast<uint32_t>(line.size());
    out_buffer[written++] = '\n';
    handle->startup_logs.pop_front();
  }
  out_buffer[std::min(written, buffer_size - 1u)] = '\0';
  *out_bytes_written = written;
  return ENGINE_RESULT_OK;
}

}  // namespace

extern "C" {

engine_result_t engine_get_runtime_api_version(uint32_t* out_api_version) {
  if (out_api_version == nullptr) {
    return ThreadError(ENGINE_RESULT_INVALID_ARGUMENT, "out_api_version is null");
  }
  *out_api_version = ENGINE_API_VERSION;
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_create(const engine_create_desc_t* desc,
                              engine_handle_t* out_handle) {
  if (desc == nullptr || out_handle == nullptr) {
    return ThreadError(ENGINE_RESULT_INVALID_ARGUMENT,
                       "engine_create requires non-null desc and out_handle");
  }
  *out_handle = nullptr;
#if defined(AETHERKIRI_INTERNAL_ARTEMIS) && \
    defined(AETHERKIRI_ENABLE_ARTEMIS_RUNTIME)
  AetherInternalRegisterArtemisRuntime();
#endif
  if (desc->struct_size < sizeof(engine_create_desc_t)) {
    return ThreadError(ENGINE_RESULT_INVALID_ARGUMENT,
                       "engine_create_desc_t.struct_size is too small");
  }
  const uint32_t host_major = (ENGINE_API_VERSION >> 24u) & 0xffu;
  const uint32_t caller_major = (desc->api_version >> 24u) & 0xffu;
  if (host_major != caller_major) {
    return ThreadError(ENGINE_RESULT_NOT_SUPPORTED,
                       "unsupported engine API major version");
  }

  auto* handle = new (std::nothrow) DispatchHandle();
  if (handle == nullptr) {
    return ThreadError(ENGINE_RESULT_INTERNAL_ERROR,
                       "failed to allocate engine dispatch handle");
  }
  handle->writable_path = desc->writable_path_utf8 != nullptr
                              ? desc->writable_path_utf8
                              : "";
  handle->cache_path = desc->cache_path_utf8 != nullptr ? desc->cache_path_utf8 : "";
  handle->create_desc = *desc;
  handle->create_desc.writable_path_utf8 = handle->writable_path.empty()
                                                ? nullptr
                                                : handle->writable_path.c_str();
  handle->create_desc.cache_path_utf8 = handle->cache_path.empty()
                                             ? nullptr
                                             : handle->cache_path.c_str();
  handle->host.struct_size = sizeof(handle->host);
  handle->host.api_version = ENGINE_RUNTIME_PROVIDER_API_VERSION;
  handle->host.user_data = handle;
  handle->host.log = HostLog;
  handle->host.monotonic_time_micros = HostMonotonicTimeMicros;
  handle->host.platform_request = HostPlatformRequest;
  handle->fragment_shader_host =
      aetherkiri::runtime::SnapshotFragmentShaderHost();
  if (handle->fragment_shader_host.execute != nullptr) {
    handle->host.reserved_ptr[0] = &handle->fragment_shader_host;
  }

  const auto legacy_result = engine_legacy_create(desc, &handle->legacy);
  if (legacy_result != ENGINE_RESULT_OK) {
    delete handle;
    return legacy_result;
  }
  const auto public_handle = reinterpret_cast<engine_handle_t>(handle);
  {
    std::lock_guard<std::recursive_mutex> guard(g_dispatch_registry_mutex);
    g_dispatch_handles.insert(public_handle);
  }
  *out_handle = public_handle;
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_poll_platform_request(
    engine_handle_t public_handle, char* operation_buffer,
    uint32_t operation_buffer_size, char* argument_buffer,
    uint32_t argument_buffer_size, uint32_t* out_available) {
  if (out_available == nullptr) {
    return ThreadError(ENGINE_RESULT_INVALID_ARGUMENT,
                       "engine_poll_platform_request requires out_available");
  }
  *out_available = 0;
  std::lock_guard<std::recursive_mutex> registry_guard(
      g_dispatch_registry_mutex);
  DispatchHandle* handle = nullptr;
  const engine_result_t validation =
      ValidateHandleLocked(public_handle, &handle);
  if (validation != ENGINE_RESULT_OK) return validation;
  std::lock_guard<std::recursive_mutex> guard(handle->mutex);
  if (handle->platform_requests.empty()) return ENGINE_RESULT_OK;
  if (operation_buffer == nullptr || operation_buffer_size == 0 ||
      argument_buffer == nullptr || argument_buffer_size == 0) {
    return ThreadError(ENGINE_RESULT_INVALID_ARGUMENT,
                       "platform request output buffers are invalid");
  }
  const DispatchHandle::PlatformRequest& request =
      handle->platform_requests.front();
  if (request.operation.size() + 1u > operation_buffer_size ||
      request.argument.size() + 1u > argument_buffer_size) {
    return ThreadError(ENGINE_RESULT_INVALID_ARGUMENT,
                       "platform request output buffer is too small");
  }
  std::memcpy(operation_buffer, request.operation.c_str(),
              request.operation.size() + 1u);
  std::memcpy(argument_buffer, request.argument.c_str(),
              request.argument.size() + 1u);
  handle->platform_requests.pop_front();
  *out_available = 1;
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_submit_platform_response(
    engine_handle_t public_handle, const char* operation_utf8,
    const char* argument_utf8) {
  if (operation_utf8 == nullptr || operation_utf8[0] == '\0') {
    return ThreadError(ENGINE_RESULT_INVALID_ARGUMENT,
                       "platform response operation is empty");
  }
  std::lock_guard<std::recursive_mutex> registry_guard(
      g_dispatch_registry_mutex);
  DispatchHandle* handle = nullptr;
  const engine_result_t validation =
      ValidateHandleLocked(public_handle, &handle);
  if (validation != ENGINE_RESULT_OK) return validation;
  std::lock_guard<std::recursive_mutex> guard(handle->mutex);
  if (handle->backend != BackendKind::kProvider ||
      handle->provider == nullptr || handle->runtime == nullptr) {
    return Unsupported(handle, "platform responses");
  }
  if (!PROVIDER_HAS(handle->provider, submit_platform_response)) {
    return Unsupported(handle, "platform responses");
  }
  const engine_result_t result =
      handle->provider->submit_platform_response(
          handle->runtime, operation_utf8,
          argument_utf8 != nullptr ? argument_utf8 : "");
  SetProviderError(handle, result, "runtime rejected platform response");
  return result;
}

engine_result_t engine_destroy(engine_handle_t public_handle) {
  if (public_handle == nullptr) {
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }
  DispatchHandle* handle = nullptr;
  std::vector<engine_media_handle_t> owned_media;
  {
    std::lock_guard<std::recursive_mutex> guard(g_dispatch_registry_mutex);
    const auto found = g_dispatch_handles.find(public_handle);
    if (found == g_dispatch_handles.end()) {
      return ThreadError(ENGINE_RESULT_INVALID_ARGUMENT,
                         "engine handle is invalid or already destroyed");
    }
    for (auto media = g_dispatch_media_handles.begin();
         media != g_dispatch_media_handles.end();) {
      if (media->second == public_handle) {
        owned_media.push_back(media->first);
        media = g_dispatch_media_handles.erase(media);
      } else {
        ++media;
      }
    }
    g_dispatch_handles.erase(found);
    handle = Cast(public_handle);
  }

  std::thread startup_thread;
  {
    std::lock_guard<std::recursive_mutex> guard(handle->mutex);
    startup_thread = std::move(handle->startup_thread);
  }
  if (startup_thread.joinable()) startup_thread.join();
  if (handle->provider != nullptr && handle->runtime != nullptr) {
    handle->provider->destroy(handle->runtime);
    handle->runtime = nullptr;
  }
  for (const auto media : owned_media) {
    engine_legacy_media_destroy(media);
  }
  const auto result = engine_legacy_destroy(handle->legacy);
  delete handle;
  SetThreadError(nullptr);
  return result;
}

engine_result_t engine_media_open(engine_handle_t public_handle,
                                  const char* path_utf8,
                                  engine_media_handle_t* out_media) {
  if (path_utf8 == nullptr || path_utf8[0] == '\0' || out_media == nullptr) {
    return ThreadError(ENGINE_RESULT_INVALID_ARGUMENT,
                       "media path and output handle are required");
  }
  *out_media = nullptr;
  std::lock_guard<std::recursive_mutex> registry_guard(
      g_dispatch_registry_mutex);
  DispatchHandle* handle = nullptr;
  const auto validation = ValidateHandleLocked(public_handle, &handle);
  if (validation != ENGINE_RESULT_OK) return validation;
  std::lock_guard<std::recursive_mutex> guard(handle->mutex);
  engine_media_handle_t legacy_media = nullptr;
  const auto result = engine_legacy_media_open(handle->legacy, path_utf8,
                                               &legacy_media);
  SetLegacyError(handle, result, "legacy media player failed to open media");
  if (result != ENGINE_RESULT_OK) return result;
  if (legacy_media == nullptr) {
    handle->last_error = "legacy media player returned an empty handle";
    SetThreadError(handle->last_error.c_str());
    return ENGINE_RESULT_INTERNAL_ERROR;
  }
  g_dispatch_media_handles.emplace(legacy_media, public_handle);
  *out_media = legacy_media;
  return ENGINE_RESULT_OK;
}

engine_result_t engine_media_destroy(engine_media_handle_t media) {
  if (media == nullptr) {
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }
  std::lock_guard<std::recursive_mutex> registry_guard(
      g_dispatch_registry_mutex);
  const auto found = g_dispatch_media_handles.find(media);
  if (found == g_dispatch_media_handles.end()) {
    return ThreadError(ENGINE_RESULT_INVALID_ARGUMENT,
                       "media handle is invalid or already destroyed");
  }
  DispatchHandle* owner = nullptr;
  const auto validation = ValidateHandleLocked(found->second, &owner);
  if (validation != ENGINE_RESULT_OK) return validation;
  std::lock_guard<std::recursive_mutex> guard(owner->mutex);
  const auto result = engine_legacy_media_destroy(media);
  g_dispatch_media_handles.erase(found);
  SetLegacyError(owner, result, "legacy media player failed to close media");
  return result;
}

engine_result_t engine_media_play(engine_media_handle_t media) {
  return RouteMedia(media, "legacy media player failed to play media",
                    [](engine_media_handle_t legacy) {
                      return engine_legacy_media_play(legacy);
                    });
}

engine_result_t engine_media_pause(engine_media_handle_t media) {
  return RouteMedia(media, "legacy media player failed to pause media",
                    [](engine_media_handle_t legacy) {
                      return engine_legacy_media_pause(legacy);
                    });
}

engine_result_t engine_media_seek(engine_media_handle_t media,
                                  int64_t position_ms) {
  return RouteMedia(media, "legacy media player failed to seek media",
                    [&](engine_media_handle_t legacy) {
                      return engine_legacy_media_seek(legacy, position_ms);
                    });
}

engine_result_t engine_media_set_rate(engine_media_handle_t media,
                                      double playback_rate) {
  return RouteMedia(media, "legacy media player failed to set playback rate",
                    [&](engine_media_handle_t legacy) {
                      return engine_legacy_media_set_rate(legacy,
                                                          playback_rate);
                    });
}

engine_result_t engine_media_get_state(engine_media_handle_t media,
                                       engine_media_state_t* out_state) {
  return RouteMedia(media, "legacy media player failed to read media state",
                    [&](engine_media_handle_t legacy) {
                      return engine_legacy_media_get_state(legacy, out_state);
                    });
}

engine_result_t engine_media_get_subtitle_tracks_json(
    engine_media_handle_t media, char* out_buffer, uint32_t buffer_size,
    uint32_t* out_bytes_written) {
  return RouteMedia(
      media, "legacy media player failed to list subtitle tracks",
      [&](engine_media_handle_t legacy) {
        return engine_legacy_media_get_subtitle_tracks_json(
            legacy, out_buffer, buffer_size, out_bytes_written);
      });
}

engine_result_t engine_media_extract_subtitle(
    engine_media_handle_t media, int32_t stream_index,
    const char* output_path_utf8) {
  return RouteMedia(media, "legacy media player failed to extract subtitles",
                    [&](engine_media_handle_t legacy) {
                      return engine_legacy_media_extract_subtitle(
                          legacy, stream_index, output_path_utf8);
                    });
}

engine_result_t engine_media_read_frame_rgba(
    engine_media_handle_t media, void* out_pixels, size_t out_pixels_size,
    engine_frame_desc_t* out_frame_desc) {
  return RouteMedia(media, "legacy media player failed to read video frame",
                    [&](engine_media_handle_t legacy) {
                      return engine_legacy_media_read_frame_rgba(
                          legacy, out_pixels, out_pixels_size, out_frame_desc);
                    });
}

engine_result_t engine_open_game(engine_handle_t public_handle,
                                 const char* game_root_path_utf8,
                                 const char* startup_script_utf8) {
  if (game_root_path_utf8 == nullptr || game_root_path_utf8[0] == '\0') {
    return ThreadError(ENGINE_RESULT_INVALID_ARGUMENT,
                       "game_root_path_utf8 is null or empty");
  }
  std::lock_guard<std::recursive_mutex> registry_guard(g_dispatch_registry_mutex);
  DispatchHandle* handle = nullptr;
  auto result = ValidateHandleLocked(public_handle, &handle);
  if (result != ENGINE_RESULT_OK) return result;
  std::lock_guard<std::recursive_mutex> guard(handle->mutex);
  if (handle->startup_thread.joinable()) {
    return ThreadError(ENGINE_RESULT_INVALID_STATE,
                       "an asynchronous startup task must finish before reopening");
  }
  result = SelectBackendLocked(handle, game_root_path_utf8);
  if (result != ENGINE_RESULT_OK) return result;
  result = CheckArtemisBetaAccess(handle);
  if (result != ENGINE_RESULT_OK) return result;
  if (handle->backend == BackendKind::kLegacy) {
    return engine_legacy_open_game(handle->legacy, game_root_path_utf8,
                                   startup_script_utf8);
  }
  handle->startup_state = ENGINE_STARTUP_STATE_RUNNING;
  AETHER_DISPATCH_DIAG_LOG("engine_open_game before provider open_game");
  result = handle->provider->open_game(handle->runtime, game_root_path_utf8,
                                       startup_script_utf8);
  AETHER_DISPATCH_DIAG_LOG("engine_open_game after provider open_game");
  handle->startup_state = result == ENGINE_RESULT_OK
                              ? ENGINE_STARTUP_STATE_SUCCEEDED
                              : ENGINE_STARTUP_STATE_FAILED;
  handle->startup_logs.push_back(result == ENGINE_RESULT_OK
                                     ? "runtime provider open_game => OK"
                                     : "runtime provider open_game => FAILED");
  SetProviderError(handle, result, "runtime provider failed to open game");
  return result;
}

engine_result_t engine_open_game_async(engine_handle_t public_handle,
                                       const char* game_root_path_utf8,
                                       const char* startup_script_utf8) {
  if (game_root_path_utf8 == nullptr || game_root_path_utf8[0] == '\0') {
    return ThreadError(ENGINE_RESULT_INVALID_ARGUMENT,
                       "game_root_path_utf8 is null or empty");
  }
  std::lock_guard<std::recursive_mutex> registry_guard(g_dispatch_registry_mutex);
  DispatchHandle* handle = nullptr;
  auto result = ValidateHandleLocked(public_handle, &handle);
  if (result != ENGINE_RESULT_OK) return result;
  std::lock_guard<std::recursive_mutex> guard(handle->mutex);
  if (handle->startup_thread.joinable()) {
    handle->last_error = "an asynchronous startup task already exists";
    return ThreadError(ENGINE_RESULT_INVALID_STATE, handle->last_error.c_str());
  }
  result = SelectBackendLocked(handle, game_root_path_utf8);
  if (result != ENGINE_RESULT_OK) {
    if (handle->last_error.empty()) {
      handle->last_error = "failed to select a runtime backend";
    }
    return ThreadError(result, handle->last_error.c_str());
  }
  result = CheckArtemisBetaAccess(handle);
  if (result != ENGINE_RESULT_OK) return result;
  if (handle->backend == BackendKind::kLegacy) {
    return engine_legacy_open_game_async(handle->legacy, game_root_path_utf8,
                                         startup_script_utf8);
  }

  const std::string root(game_root_path_utf8);
  const std::string startup = startup_script_utf8 != nullptr
                                  ? startup_script_utf8
                                  : "";
  handle->startup_state = ENGINE_STARTUP_STATE_RUNNING;
  handle->startup_thread = std::thread([handle, root, startup]() {
    const char* startup_value = startup.empty() ? nullptr : startup.c_str();
    AETHER_DISPATCH_DIAG_LOG("engine_open_game_async before provider open_game");
    const auto open_result = handle->provider->open_game(
        handle->runtime, root.c_str(), startup_value);
    AETHER_DISPATCH_DIAG_LOG("engine_open_game_async after provider open_game");
    std::lock_guard<std::recursive_mutex> thread_guard(handle->mutex);
    handle->startup_state = open_result == ENGINE_RESULT_OK
                                ? ENGINE_STARTUP_STATE_SUCCEEDED
                                : ENGINE_STARTUP_STATE_FAILED;
    handle->startup_logs.push_back(open_result == ENGINE_RESULT_OK
                                       ? "runtime provider open_game => OK"
                                       : "runtime provider open_game => FAILED");
    SetProviderError(handle, open_result,
                     "runtime provider failed to open game asynchronously");
  });
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_get_startup_state(engine_handle_t public_handle,
                                         uint32_t* out_state) {
  if (out_state == nullptr) {
    return ThreadError(ENGINE_RESULT_INVALID_ARGUMENT, "out_state is null");
  }
  return Route(public_handle, "get_startup_state",
               [&](engine_handle_t legacy) {
                 return engine_legacy_get_startup_state(legacy, out_state);
               },
               [&](DispatchHandle* handle) {
                 *out_state = handle->startup_state;
                 return ENGINE_RESULT_OK;
               });
}

engine_result_t engine_drain_startup_logs(engine_handle_t public_handle,
                                          char* out_buffer,
                                          uint32_t buffer_size,
                                          uint32_t* out_bytes_written) {
  return Route(public_handle, "drain_startup_logs",
               [&](engine_handle_t legacy) {
                 return engine_legacy_drain_startup_logs(
                     legacy, out_buffer, buffer_size, out_bytes_written);
               },
               [&](DispatchHandle* handle) {
                 return DrainProviderStartupLogs(handle, out_buffer, buffer_size,
                                                 out_bytes_written);
               });
}

engine_result_t engine_tick(engine_handle_t public_handle, uint32_t delta_ms) {
  return Route(public_handle, "tick",
               [&](engine_handle_t legacy) {
                 return engine_legacy_tick(legacy, delta_ms);
               },
               [&](DispatchHandle* handle) {
                 if (handle->provider_resume_pending) {
                   if (!ActivateProviderAudioSessionForHost()) {
                     // UIApplication may still be foreground-inactive when
                     // Godot emits APPLICATION_RESUMED. Keep the provider
                     // paused and retry on the next host tick instead of
                     // reopening its audio device against an inactive route.
                     return ENGINE_RESULT_OK;
                   }
                   if (!PROVIDER_HAS(handle->provider, resume)) {
                     return ENGINE_RESULT_NOT_SUPPORTED;
                   }
                   const engine_result_t resume_result =
                       handle->provider->resume(handle->runtime);
                   if (resume_result != ENGINE_RESULT_OK) {
                     return resume_result;
                   }
                   handle->provider_resume_pending = false;
                 }
                 const engine_result_t result =
                     handle->provider->tick(handle->runtime, delta_ms);
#if defined(ENGINE_API_USE_KRKR2_RUNTIME)
                 // Provider runtimes bypass the legacy EngineLoop, which is
                 // normally responsible for draining textures whose intrusive
                 // reference count reached zero during the frame.  Artemis
                 // uses the same KiriKiri render manager for E-mote, so leaving
                 // this queue undrained retains every superseded Metal texture.
                 iTVPTexture2D::RecycleProcess();
#endif
                 return result;
               });
}

engine_result_t engine_pause(engine_handle_t public_handle) {
  return Route(public_handle, "pause", engine_legacy_pause,
               [](DispatchHandle* handle) {
                 handle->provider_resume_pending = false;
                 return PROVIDER_HAS(handle->provider, pause)
                            ? handle->provider->pause(handle->runtime)
                            : ENGINE_RESULT_NOT_SUPPORTED;
               });
}

engine_result_t engine_resume(engine_handle_t public_handle) {
  return Route(public_handle, "resume", engine_legacy_resume,
               [](DispatchHandle* handle) {
                 if (!PROVIDER_HAS(handle->provider, resume)) {
                   return ENGINE_RESULT_NOT_SUPPORTED;
                 }
                 if (!ActivateProviderAudioSessionForHost()) {
                   handle->provider_resume_pending = true;
                   return ENGINE_RESULT_OK;
                 }
                 const engine_result_t result =
                     handle->provider->resume(handle->runtime);
                 if (result == ENGINE_RESULT_OK) {
                   handle->provider_resume_pending = false;
                 }
                 return result;
               });
}

engine_result_t engine_set_option(engine_handle_t public_handle,
                                  const engine_option_t* option) {
  if (option == nullptr || option->key_utf8 == nullptr ||
      option->key_utf8[0] == '\0' || option->value_utf8 == nullptr) {
    return ThreadError(ENGINE_RESULT_INVALID_ARGUMENT,
                       "option key/value must be non-null and key must be non-empty");
  }
  std::lock_guard<std::recursive_mutex> registry_guard(g_dispatch_registry_mutex);
  DispatchHandle* handle = nullptr;
  auto result = ValidateHandleLocked(public_handle, &handle);
  if (result != ENGINE_RESULT_OK) return result;
  std::lock_guard<std::recursive_mutex> guard(handle->mutex);
  const std::string key = Normalize(option->key_utf8);
  if (key == "artemis_beta_allowed") {
    const std::string value = Normalize(option->value_utf8);
    handle->artemis_beta_allowed =
        value == "1" || value == "true" || value == "yes" || value == "on";
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }
  if (key == "runtime") {
    if (handle->backend != BackendKind::kUndecided) {
      handle->last_error = "runtime option must be set before opening a game";
      return ThreadError(ENGINE_RESULT_INVALID_STATE, handle->last_error.c_str());
    }
    const std::string requested_runtime = Normalize(option->value_utf8);
#if !defined(AETHERKIRI_ENABLE_ARTEMIS_RUNTIME)
    if (requested_runtime == "artemis") {
      handle->last_error =
          "Artemis runtime is available only in internal Debug builds";
      return ThreadError(ENGINE_RESULT_NOT_SUPPORTED,
                         handle->last_error.c_str());
    }
#endif
    handle->requested_runtime = requested_runtime;
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }
  handle->pending_options[option->key_utf8] = option->value_utf8;
  if (handle->backend == BackendKind::kProvider) {
    if (!PROVIDER_HAS(handle->provider, set_option)) {
      return Unsupported(handle, "set_option");
    }
    result = handle->provider->set_option(handle->runtime, option);
    SetProviderError(handle, result, "runtime provider rejected option");
    return result;
  }
  return engine_legacy_set_option(handle->legacy, option);
}

engine_result_t engine_set_surface_size(engine_handle_t public_handle,
                                        uint32_t width, uint32_t height) {
  if (width == 0 || height == 0) {
    return ThreadError(ENGINE_RESULT_INVALID_ARGUMENT,
                       "surface width and height must be greater than zero");
  }
  std::lock_guard<std::recursive_mutex> registry_guard(g_dispatch_registry_mutex);
  DispatchHandle* handle = nullptr;
  auto result = ValidateHandleLocked(public_handle, &handle);
  if (result != ENGINE_RESULT_OK) return result;
  std::lock_guard<std::recursive_mutex> guard(handle->mutex);

  if (handle->backend == BackendKind::kProvider) {
    if (!PROVIDER_HAS(handle->provider, set_surface_size)) {
      return Unsupported(handle, "set_surface_size");
    }
    result =
        handle->provider->set_surface_size(handle->runtime, width, height);
    SetProviderError(handle, result, "set_surface_size");
  } else {
    result = engine_legacy_set_surface_size(handle->legacy, width, height);
  }
  if (result == ENGINE_RESULT_OK) {
    handle->surface_width = width;
    handle->surface_height = height;
    handle->has_surface_size = true;
  }
  return result;
}

engine_result_t engine_get_frame_desc(engine_handle_t public_handle,
                                      engine_frame_desc_t* out_frame_desc) {
  return Route(public_handle, "get_frame_desc",
               [&](engine_handle_t legacy) {
                 return engine_legacy_get_frame_desc(legacy, out_frame_desc);
               },
               [&](DispatchHandle* handle) {
                 return PROVIDER_HAS(handle->provider, get_frame_desc)
                            ? handle->provider->get_frame_desc(handle->runtime,
                                                               out_frame_desc)
                            : ENGINE_RESULT_NOT_SUPPORTED;
               });
}

engine_result_t engine_read_frame_rgba(engine_handle_t public_handle,
                                       void* out_pixels,
                                       size_t out_pixels_size) {
  return Route(public_handle, "read_frame_rgba",
               [&](engine_handle_t legacy) {
                 return engine_legacy_read_frame_rgba(legacy, out_pixels,
                                                      out_pixels_size);
               },
               [&](DispatchHandle* handle) {
                 return PROVIDER_HAS(handle->provider, read_frame_rgba)
                            ? handle->provider->read_frame_rgba(
                                  handle->runtime, out_pixels, out_pixels_size)
                            : ENGINE_RESULT_NOT_SUPPORTED;
               });
}

engine_result_t engine_get_godot_native_frame_texture(
    engine_handle_t public_handle, uint64_t* out_texture_id,
    uint32_t* out_width, uint32_t* out_height, uint64_t* out_frame_serial) {
  return Route(public_handle, "get_godot_native_frame_texture",
               [&](engine_handle_t legacy) {
#if defined(ENGINE_API_USE_KRKR2_RUNTIME)
                 return engine_legacy_get_godot_native_frame_texture(
                     legacy, out_texture_id, out_width, out_height,
                     out_frame_serial);
#else
                 (void)legacy;
                 return ENGINE_RESULT_NOT_SUPPORTED;
#endif
               },
               [&](DispatchHandle* handle) {
                 return PROVIDER_HAS(handle->provider,
                                     get_godot_native_frame_texture)
                            ? handle->provider->get_godot_native_frame_texture(
                                  handle->runtime, out_texture_id, out_width,
                                  out_height, out_frame_serial)
                            : ENGINE_RESULT_NOT_SUPPORTED;
               });
}

engine_result_t engine_get_godot_presentation_state(
    engine_handle_t public_handle, uint32_t* out_state_flags) {
  if (out_state_flags == nullptr) {
    return ThreadError(ENGINE_RESULT_INVALID_ARGUMENT,
                       "Godot presentation state output is null");
  }
  *out_state_flags = ENGINE_GODOT_PRESENTATION_STATE_NONE;
  return Route(public_handle, "get_godot_presentation_state",
               [&](engine_handle_t legacy) {
                 (void)legacy;
                 return ENGINE_RESULT_OK;
               },
               [&](DispatchHandle* handle) {
                 if (!PROVIDER_HAS(handle->provider,
                                   get_godot_presentation_state)) {
                   return ENGINE_RESULT_OK;
                 }
                 return handle->provider->get_godot_presentation_state(
                     handle->runtime, out_state_flags);
               });
}

engine_result_t engine_get_host_native_window(engine_handle_t public_handle,
                                              void** out_window_handle) {
  return Route(public_handle, "get_host_native_window",
               [&](engine_handle_t legacy) {
                 return engine_legacy_get_host_native_window(legacy,
                                                              out_window_handle);
               },
               [&](DispatchHandle* handle) {
                 return PROVIDER_HAS(handle->provider, get_host_native_window)
                            ? handle->provider->get_host_native_window(
                                  handle->runtime, out_window_handle)
                            : ENGINE_RESULT_NOT_SUPPORTED;
               });
}

engine_result_t engine_get_host_native_view(engine_handle_t public_handle,
                                            void** out_view_handle) {
  return Route(public_handle, "get_host_native_view",
               [&](engine_handle_t legacy) {
                 return engine_legacy_get_host_native_view(legacy,
                                                            out_view_handle);
               },
               [&](DispatchHandle* handle) {
                 return PROVIDER_HAS(handle->provider, get_host_native_view)
                            ? handle->provider->get_host_native_view(
                                  handle->runtime, out_view_handle)
                            : ENGINE_RESULT_NOT_SUPPORTED;
               });
}

engine_result_t engine_send_input(engine_handle_t public_handle,
                                  const engine_input_event_t* event) {
  return Route(public_handle, "send_input",
               [&](engine_handle_t legacy) {
                 return engine_legacy_send_input(legacy, event);
               },
               [&](DispatchHandle* handle) {
                 return PROVIDER_HAS(handle->provider, send_input)
                            ? handle->provider->send_input(handle->runtime, event)
                            : ENGINE_RESULT_NOT_SUPPORTED;
               });
}

engine_result_t engine_get_text_input_state(
    engine_handle_t public_handle, engine_text_input_state_t* out_state) {
  if (out_state == nullptr) {
    return ThreadError(ENGINE_RESULT_INVALID_ARGUMENT,
                       "text input state output is null");
  }
  if (out_state->struct_size < sizeof(engine_text_input_state_t)) {
    return ThreadError(ENGINE_RESULT_INVALID_ARGUMENT,
                       "engine_text_input_state_t.struct_size is too small");
  }
  return Route(public_handle, "get_text_input_state",
               [&](engine_handle_t legacy) {
                 return engine_legacy_get_text_input_state(legacy, out_state);
               },
               [&](DispatchHandle* handle) {
                 engine_text_input_state_t snapshot{};
                 snapshot.struct_size = sizeof(snapshot);
                 if (!PROVIDER_HAS(handle->provider, get_text_input_state)) {
                   *out_state = snapshot;
                   return ENGINE_RESULT_OK;
                 }
                 uint32_t state_flags = ENGINE_TEXT_INPUT_STATE_NONE;
                 const engine_result_t result =
                     handle->provider->get_text_input_state(
                         handle->runtime, &state_flags);
                 if (result != ENGINE_RESULT_OK) return result;
                 if ((state_flags & ENGINE_TEXT_INPUT_STATE_ACTIVE) != 0) {
                   snapshot.ime_active = 1;
                   snapshot.attention_point_valid = 1;
                   snapshot.attention_x = static_cast<int32_t>(
                       handle->surface_width > 0 ? handle->surface_width / 2u
                                                 : 640u);
                   snapshot.attention_y = static_cast<int32_t>(
                       handle->surface_height > 0 ? handle->surface_height / 2u
                                                  : 360u);
                 }
                 *out_state = snapshot;
                 return ENGINE_RESULT_OK;
               });
}

engine_result_t engine_copy_text_input_text(engine_handle_t public_handle,
                                            char* out_buffer,
                                            uint32_t buffer_size,
                                            uint32_t* out_bytes_written) {
  if (out_buffer == nullptr || buffer_size == 0 ||
      out_bytes_written == nullptr) {
    return ThreadError(
        ENGINE_RESULT_INVALID_ARGUMENT,
        "engine_copy_text_input_text requires a buffer and byte count");
  }
  out_buffer[0] = '\0';
  *out_bytes_written = 0;
  return Route(public_handle, "copy_text_input_text",
               [&](engine_handle_t legacy) {
                 return engine_legacy_copy_text_input_text(
                     legacy, out_buffer, buffer_size, out_bytes_written);
               },
               [&](DispatchHandle*) { return ENGINE_RESULT_OK; });
}

engine_result_t engine_get_main_menu_json(engine_handle_t public_handle,
                                          char* out_buffer,
                                          uint32_t buffer_size,
                                          uint32_t* out_bytes_written) {
  return Route(public_handle, "get_main_menu_json",
               [&](engine_handle_t legacy) {
                 return engine_legacy_get_main_menu_json(
                     legacy, out_buffer, buffer_size, out_bytes_written);
               },
               [&](DispatchHandle* handle) {
                 return PROVIDER_HAS(handle->provider, get_main_menu_json)
                            ? handle->provider->get_main_menu_json(
                                  handle->runtime, out_buffer, buffer_size,
                                  out_bytes_written)
                            : ENGINE_RESULT_NOT_SUPPORTED;
               });
}

engine_result_t engine_activate_menu_item(engine_handle_t public_handle,
                                          const char* item_path_utf8) {
  return Route(public_handle, "activate_menu_item",
               [&](engine_handle_t legacy) {
                 return engine_legacy_activate_menu_item(legacy,
                                                          item_path_utf8);
               },
               [&](DispatchHandle* handle) {
                 return PROVIDER_HAS(handle->provider, activate_menu_item)
                            ? handle->provider->activate_menu_item(
                                  handle->runtime, item_path_utf8)
                            : ENGINE_RESULT_NOT_SUPPORTED;
               });
}

engine_result_t engine_set_render_target_iosurface(engine_handle_t public_handle,
                                                   uint32_t iosurface_id,
                                                   uint32_t width,
                                                   uint32_t height) {
  return Route(public_handle, "set_render_target_iosurface",
               [&](engine_handle_t legacy) {
                 return engine_legacy_set_render_target_iosurface(
                     legacy, iosurface_id, width, height);
               },
               [&](DispatchHandle* handle) {
                 return PROVIDER_HAS(handle->provider,
                                     set_render_target_iosurface)
                            ? handle->provider->set_render_target_iosurface(
                                  handle->runtime, iosurface_id, width, height)
                            : ENGINE_RESULT_NOT_SUPPORTED;
               });
}

engine_result_t engine_set_render_target_surface(engine_handle_t public_handle,
                                                 void* native_window,
                                                 uint32_t width,
                                                 uint32_t height) {
  return Route(public_handle, "set_render_target_surface",
               [&](engine_handle_t legacy) {
                 return engine_legacy_set_render_target_surface(
                     legacy, native_window, width, height);
               },
               [&](DispatchHandle* handle) {
                 return PROVIDER_HAS(handle->provider, set_render_target_surface)
                            ? handle->provider->set_render_target_surface(
                                  handle->runtime, native_window, width, height)
                            : ENGINE_RESULT_NOT_SUPPORTED;
               });
}

engine_result_t engine_get_frame_rendered_flag(engine_handle_t public_handle,
                                               uint32_t* out_rendered) {
  return Route(public_handle, "get_frame_rendered_flag",
               [&](engine_handle_t legacy) {
                 return engine_legacy_get_frame_rendered_flag(legacy,
                                                               out_rendered);
               },
               [&](DispatchHandle* handle) {
                 return PROVIDER_HAS(handle->provider,
                                     get_frame_rendered_flag)
                            ? handle->provider->get_frame_rendered_flag(
                                  handle->runtime, out_rendered)
                            : ENGINE_RESULT_NOT_SUPPORTED;
               });
}

engine_result_t engine_get_renderer_info(engine_handle_t public_handle,
                                         char* out_buffer,
                                         uint32_t buffer_size) {
  return Route(public_handle, "get_renderer_info",
               [&](engine_handle_t legacy) {
                 return engine_legacy_get_renderer_info(legacy, out_buffer,
                                                         buffer_size);
               },
               [&](DispatchHandle* handle) {
                 return PROVIDER_HAS(handle->provider, get_renderer_info)
                            ? handle->provider->get_renderer_info(
                                  handle->runtime, out_buffer, buffer_size)
                            : ENGINE_RESULT_NOT_SUPPORTED;
               });
}

engine_result_t engine_get_memory_stats(engine_handle_t public_handle,
                                        engine_memory_stats_t* out_stats) {
  return Route(public_handle, "get_memory_stats",
               [&](engine_handle_t legacy) {
                 return engine_legacy_get_memory_stats(legacy, out_stats);
               },
               [&](DispatchHandle* handle) {
                 return PROVIDER_HAS(handle->provider, get_memory_stats)
                            ? handle->provider->get_memory_stats(handle->runtime,
                                                                 out_stats)
                            : ENGINE_RESULT_NOT_SUPPORTED;
               });
}

engine_result_t engine_get_plugin_debug_info(engine_handle_t public_handle,
                                             char* out_buffer,
                                             uint32_t buffer_size,
                                             uint32_t* out_bytes_written) {
  return Route(public_handle, "get_plugin_debug_info",
               [&](engine_handle_t legacy) {
                 return engine_legacy_get_plugin_debug_info(
                     legacy, out_buffer, buffer_size, out_bytes_written);
               },
               [&](DispatchHandle* handle) {
                 return PROVIDER_HAS(handle->provider, get_plugin_debug_info)
                            ? handle->provider->get_plugin_debug_info(
                                  handle->runtime, out_buffer, buffer_size,
                                  out_bytes_written)
                            : ENGINE_RESULT_NOT_SUPPORTED;
               });
}

/* Diagnostics are host-owned and deliberately remain available for every
 * provider through the legacy diagnostic queue. */
engine_result_t engine_set_diagnostic_config(
    engine_handle_t public_handle, const engine_diagnostic_config_t* config) {
  std::lock_guard<std::recursive_mutex> registry_guard(g_dispatch_registry_mutex);
  DispatchHandle* handle = nullptr;
  const auto result = ValidateHandleLocked(public_handle, &handle);
  return result == ENGINE_RESULT_OK
             ? engine_legacy_set_diagnostic_config(handle->legacy, config)
             : result;
}

engine_result_t engine_mark_diagnostic_event(engine_handle_t public_handle,
                                             const char* label_utf8,
                                             uint64_t* out_sequence) {
  std::lock_guard<std::recursive_mutex> registry_guard(g_dispatch_registry_mutex);
  DispatchHandle* handle = nullptr;
  const auto result = ValidateHandleLocked(public_handle, &handle);
  return result == ENGINE_RESULT_OK
             ? engine_legacy_mark_diagnostic_event(handle->legacy, label_utf8,
                                                   out_sequence)
             : result;
}

engine_result_t engine_drain_diagnostic_events(engine_handle_t public_handle,
                                               char* out_buffer,
                                               uint32_t buffer_size,
                                               uint32_t* out_bytes_written) {
  std::lock_guard<std::recursive_mutex> registry_guard(g_dispatch_registry_mutex);
  DispatchHandle* handle = nullptr;
  const auto result = ValidateHandleLocked(public_handle, &handle);
  return result == ENGINE_RESULT_OK
             ? engine_legacy_drain_diagnostic_events(
                   handle->legacy, out_buffer, buffer_size, out_bytes_written)
             : result;
}

const char* engine_get_last_error(engine_handle_t public_handle) {
  if (!g_dispatch_thread_error.empty()) {
    return g_dispatch_thread_error.c_str();
  }
  if (public_handle == nullptr) return g_dispatch_thread_error.c_str();
  std::lock_guard<std::recursive_mutex> registry_guard(g_dispatch_registry_mutex);
  DispatchHandle* handle = nullptr;
  if (ValidateHandleLocked(public_handle, &handle) != ENGINE_RESULT_OK) {
    return g_dispatch_thread_error.c_str();
  }
  std::lock_guard<std::recursive_mutex> guard(handle->mutex);
  if (handle->backend == BackendKind::kProvider || !handle->last_error.empty()) {
    return handle->last_error.c_str();
  }
  return engine_legacy_get_last_error(handle->legacy);
}

}  // extern "C"
