#ifndef AETHERKIRI_ENGINE_RUNTIME_PROVIDER_REGISTRY_H_
#define AETHERKIRI_ENGINE_RUNTIME_PROVIDER_REGISTRY_H_

#include <string>
#include <vector>

#include "engine_runtime_provider.h"

namespace aetherkiri::runtime {

struct RegisteredProvider {
  const engine_runtime_provider_v1_t* api = nullptr;
  std::string runtime_id;
  uint64_t registration_order = 0;
  /* The built-in KiriKiri ("kirikiri") backend is listed through the same
   * registry as every external provider, but the dispatch layer routes it to
   * the engine_legacy_* surface instead of the provider vtable: the legacy
   * engine communicates through per-handle startup-log and diagnostic queues
   * that only the dispatch layer can drain. */
  bool is_legacy_builtin = false;
};

std::vector<RegisteredProvider> SnapshotProviders();
engine_runtime_fragment_shader_host_v1_t SnapshotFragmentShaderHost();

}  // namespace aetherkiri::runtime

#endif  /* AETHERKIRI_ENGINE_RUNTIME_PROVIDER_REGISTRY_H_ */
