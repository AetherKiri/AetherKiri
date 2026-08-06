#include "engine_runtime_provider_registry.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace {

std::mutex g_provider_mutex;
std::vector<aetherkiri::runtime::RegisteredProvider> g_providers;
uint64_t g_next_registration_order = 0;
std::mutex g_fragment_shader_mutex;
engine_runtime_fragment_shader_execute_fn g_fragment_shader_execute = nullptr;
void* g_fragment_shader_user_data = nullptr;

std::string NormalizeRuntimeId(const char* value) {
  std::string normalized = value != nullptr ? value : "";
  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return normalized;
}

bool IsValidRuntimeId(const std::string& value) {
  if (value.empty()) return false;
  return std::all_of(value.begin(), value.end(), [](unsigned char c) {
    return std::isalnum(c) != 0 || c == '-' || c == '_' || c == '.';
  });
}

}  // namespace

namespace aetherkiri::runtime {

std::vector<RegisteredProvider> SnapshotProviders() {
  std::lock_guard<std::mutex> guard(g_provider_mutex);
  return g_providers;
}

engine_runtime_fragment_shader_host_v1_t SnapshotFragmentShaderHost() {
  std::lock_guard<std::mutex> guard(g_fragment_shader_mutex);
  return {sizeof(engine_runtime_fragment_shader_host_v1_t),
          ENGINE_RUNTIME_FRAGMENT_SHADER_API_VERSION,
          g_fragment_shader_user_data, g_fragment_shader_execute};
}

}  // namespace aetherkiri::runtime

extern "C" {

engine_result_t engine_register_runtime_provider(
    const engine_runtime_provider_v1_t* provider) {
  if (provider == nullptr ||
      provider->struct_size < ENGINE_RUNTIME_PROVIDER_V1_MIN_SIZE) {
    return ENGINE_RESULT_INVALID_ARGUMENT;
  }
  const uint32_t host_major =
      (ENGINE_RUNTIME_PROVIDER_API_VERSION >> 24u) & 0xffu;
  const uint32_t provider_major = (provider->api_version >> 24u) & 0xffu;
  if (provider_major != host_major) return ENGINE_RESULT_NOT_SUPPORTED;

  const std::string runtime_id = NormalizeRuntimeId(provider->runtime_id_utf8);
  if (!IsValidRuntimeId(runtime_id) || provider->probe == nullptr ||
      provider->create == nullptr || provider->destroy == nullptr ||
      provider->open_game == nullptr || provider->tick == nullptr) {
    return ENGINE_RESULT_INVALID_ARGUMENT;
  }
  if (runtime_id == "auto" || runtime_id == "kirikiri" || runtime_id == "legacy") {
    return ENGINE_RESULT_INVALID_ARGUMENT;
  }
#if !defined(AETHERKIRI_ENABLE_ARTEMIS_RUNTIME)
  // Artemis is an internal preview.  This registry-level gate is deliberate:
  // even a provider linked or injected by mistake cannot make a non-Debug
  // product recognize an Artemis directory during automatic probing.
  if (runtime_id == "artemis") return ENGINE_RESULT_NOT_SUPPORTED;
#endif

  std::lock_guard<std::mutex> guard(g_provider_mutex);
  const auto found = std::find_if(
      g_providers.begin(), g_providers.end(), [&](const auto& registered) {
        return registered.runtime_id == runtime_id;
      });
  if (found != g_providers.end()) {
    return found->api == provider ? ENGINE_RESULT_OK : ENGINE_RESULT_INVALID_STATE;
  }
  g_providers.push_back(
      {provider, runtime_id, g_next_registration_order++});
  return ENGINE_RESULT_OK;
}

engine_result_t engine_set_runtime_fragment_shader_executor(
    engine_runtime_fragment_shader_execute_fn execute, void* user_data) {
  std::lock_guard<std::mutex> guard(g_fragment_shader_mutex);
  g_fragment_shader_execute = execute;
  g_fragment_shader_user_data = execute != nullptr ? user_data : nullptr;
  return ENGINE_RESULT_OK;
}

uint32_t engine_get_runtime_provider_count(void) {
  std::lock_guard<std::mutex> guard(g_provider_mutex);
  return static_cast<uint32_t>(g_providers.size());
}

engine_result_t engine_get_runtime_provider_id(uint32_t index,
                                               char* out_buffer,
                                               uint32_t buffer_size,
                                               uint32_t* out_bytes_written) {
  if (out_bytes_written == nullptr) return ENGINE_RESULT_INVALID_ARGUMENT;
  *out_bytes_written = 0;
  if (out_buffer == nullptr || buffer_size == 0) {
    return ENGINE_RESULT_INVALID_ARGUMENT;
  }
  std::lock_guard<std::mutex> guard(g_provider_mutex);
  if (index >= g_providers.size()) return ENGINE_RESULT_INVALID_ARGUMENT;
  const std::string& value = g_providers[index].runtime_id;
  if (value.size() + 1u > buffer_size) return ENGINE_RESULT_INVALID_ARGUMENT;
  std::memcpy(out_buffer, value.c_str(), value.size() + 1u);
  *out_bytes_written = static_cast<uint32_t>(value.size());
  return ENGINE_RESULT_OK;
}

int32_t engine_probe_runtime_provider(const char* runtime_id_utf8,
                                      const char* game_root_path_utf8) {
  const std::string runtime_id = NormalizeRuntimeId(runtime_id_utf8);
  if (!IsValidRuntimeId(runtime_id) || game_root_path_utf8 == nullptr ||
      game_root_path_utf8[0] == '\0') {
    return -1;
  }
  const auto providers = aetherkiri::runtime::SnapshotProviders();
  const auto found = std::find_if(
      providers.begin(), providers.end(), [&](const auto& candidate) {
        return candidate.runtime_id == runtime_id;
      });
  if (found == providers.end() || found->api == nullptr ||
      found->api->probe == nullptr) {
    return -1;
  }
  try {
    return std::max<int32_t>(
        0, found->api->probe(found->api->provider_user_data,
                            game_root_path_utf8));
  } catch (...) {
    return 0;
  }
}

}  // extern "C"
