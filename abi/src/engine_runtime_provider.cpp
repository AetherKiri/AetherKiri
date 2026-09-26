#include "engine_runtime_provider_registry.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace {

std::mutex g_provider_mutex;
std::vector<aetherkiri::runtime::RegisteredProvider> g_providers;
uint64_t g_next_registration_order = 0;
bool g_legacy_builtin_registered = false;
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

std::string LowercaseFileName(const std::filesystem::path& path) {
  std::string name = path.filename().string();
  std::transform(name.begin(), name.end(), name.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return name;
}

/* Advisory directory probe for the built-in KiriKiri backend. A data.xp3 or
 * any *.xp3 archive marks the directory as a KiriKiri game. The score never
 * participates in automatic provider selection (the legacy backend is the
 * fallback when nothing else claims a directory); it only answers
 * engine_probe_runtime_provider("kirikiri", ...) queries. */
int32_t LegacyKirikiriProbe(void*, const char* game_root_path_utf8) {
  if (game_root_path_utf8 == nullptr || game_root_path_utf8[0] == '\0') {
    return 0;
  }
  std::error_code error;
  int32_t score = 0;
  std::filesystem::path root(game_root_path_utf8);
  for (auto it = std::filesystem::directory_iterator(root, error);
       !error && it != std::filesystem::directory_iterator();
       it.increment(error)) {
    std::error_code entry_error;
    if (!it->is_regular_file(entry_error) || entry_error) continue;
    const std::string name = LowercaseFileName(it->path());
    if (name == "data.xp3") return 100;
    if (name.size() > 4u && name.compare(name.size() - 4u, 4u, ".xp3") == 0) {
      score = std::max<int32_t>(score, 80);
    }
  }
  return score;
}

/* Descriptor for the built-in KiriKiri backend. Only the metadata and the
 * probe are meaningful: the dispatch layer routes this entry to the
 * engine_legacy_* surface (see RegisteredProvider::is_legacy_builtin), so
 * no vtable forwarding is required. */
const engine_runtime_provider_v1_t kLegacyKirikiriProvider = [] {
  engine_runtime_provider_v1_t provider{};
  provider.struct_size = sizeof(provider);
  provider.api_version = ENGINE_RUNTIME_PROVIDER_API_VERSION;
  provider.runtime_id_utf8 = "kirikiri";
  provider.display_name_utf8 = "KiriKiri";
  provider.priority = INT32_MIN;
  provider.probe = LegacyKirikiriProbe;
  return provider;
}();

void EnsureLegacyRegisteredLocked() {
  if (g_legacy_builtin_registered) return;
  g_legacy_builtin_registered = true;
  g_providers.push_back(
      {&kLegacyKirikiriProvider, "kirikiri", g_next_registration_order++,
       /*is_legacy_builtin=*/true});
}

}  // namespace

namespace aetherkiri::runtime {

std::vector<RegisteredProvider> SnapshotProviders() {
  std::lock_guard<std::mutex> guard(g_provider_mutex);
  EnsureLegacyRegisteredLocked();
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
  // "auto" is the selection wildcard and "legacy" is a dispatch-level alias
  // for the built-in KiriKiri backend, so neither may be claimed by an
  // external provider. "kirikiri" belongs to the built-in backend that the
  // registry always installs before any external registration.
  if (runtime_id == "auto" || runtime_id == "legacy") {
    return ENGINE_RESULT_INVALID_ARGUMENT;
  }
#if !defined(AETHERKIRI_ENABLE_ARTEMIS_RUNTIME)
  // Artemis is an internal preview.  This registry-level gate is deliberate:
  // even a provider linked or injected by mistake cannot make a non-Debug
  // product recognize an Artemis directory during automatic probing.
  if (runtime_id == "artemis") return ENGINE_RESULT_NOT_SUPPORTED;
#endif

  std::lock_guard<std::mutex> guard(g_provider_mutex);
  EnsureLegacyRegisteredLocked();
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
  EnsureLegacyRegisteredLocked();
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
  EnsureLegacyRegisteredLocked();
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
  // Probing the built-in backend must not create or start a runtime; the
  // advisory score alone answers whether the directory looks like a
  // KiriKiri game.
  try {
    return std::max<int32_t>(
        0, found->api->probe(found->api->provider_user_data,
                            game_root_path_utf8));
  } catch (...) {
    return 0;
  }
}

}  // extern "C"
