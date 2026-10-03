#pragma once

#include "engine_api.h"

#include <string>

namespace aetherkiri::renpy::mobile {

/*
 * The Android RAPT package is built around PythonSDLActivity.  AetherKiri
 * already owns the process and its Godot Activity, so the eventual adapter
 * must attach RAPT to that Activity rather than starting another one.
 *
 * This boundary intentionally carries an opaque host pointer.  Android hosts
 * may pass a JNI jobject through it without making the public provider ABI
 * depend on jni.h; non-Android callers can leave it null.  The current
 * implementation is a compile-tested guard and returns NOT_SUPPORTED until
 * the staged PythonSDLActivity classes, librenpython, asset extraction, SDL
 * surface, and lifecycle hooks are linked into the generated Android app.
 */
struct BootstrapRequest {
  const char* game_root_path_utf8 = nullptr;
  const char* startup_script_utf8 = nullptr;
  void* existing_host_activity = nullptr;
};

class BootstrapAdapter final {
 public:
  BootstrapAdapter() = default;

  /* Validate the host-owned Android handoff and the staged RAPT payload.
   * This never calls SDL_main or starts a second Activity. */
  engine_result_t Preflight(const BootstrapRequest& request);

  engine_result_t Start(const BootstrapRequest& request);
  engine_result_t Stop();

  bool running() const { return running_; }
  const std::string& last_error() const { return last_error_; }

 private:
  bool running_ = false;
  std::string last_error_;
  void* native_library_handle_ = nullptr;
};

}  // namespace aetherkiri::renpy::mobile
