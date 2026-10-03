#include "renpy_mobile_adapter.h"

#include <cassert>
#include <cstring>

int main() {
  using aetherkiri::renpy::mobile::BootstrapAdapter;
  using aetherkiri::renpy::mobile::BootstrapRequest;

  BootstrapAdapter adapter;

  BootstrapRequest invalid{};
  assert(adapter.Start(invalid) == ENGINE_RESULT_INVALID_ARGUMENT);
  assert(!adapter.running());

  BootstrapRequest request{};
  request.game_root_path_utf8 = "/game";
  int host_token = 1;
  request.existing_host_activity = &host_token;
  assert(adapter.Start(request) == ENGINE_RESULT_NOT_SUPPORTED);
  assert(!adapter.running());
  const char* error = adapter.last_error().c_str();
#if defined(__ANDROID__)
  // Android may stop at the JavaVM/context boundary before reaching the
  // dependency guard; both are deliberate NOT_SUPPORTED contracts.
  assert(std::strstr(error, "RAPT") != nullptr ||
         std::strstr(error, "JavaVM") != nullptr);
#else
  assert(std::strstr(error, "Android host") != nullptr);
#endif

  // The provider may call Stop during destruction even after a failed Start.
  assert(adapter.Stop() == ENGINE_RESULT_OK);
  assert(!adapter.running());
  assert(adapter.last_error().empty());

  request.existing_host_activity = nullptr;
  assert(adapter.Start(request) == ENGINE_RESULT_NOT_SUPPORTED);
#if defined(__ANDROID__)
  assert(std::strstr(adapter.last_error().c_str(), "second Activity") !=
         nullptr);
#else
  assert(std::strstr(adapter.last_error().c_str(), "Android host") != nullptr);
#endif
  return 0;
}
