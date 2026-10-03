#include "renpy_mobile_loader.h"

#include <cstring>

#if defined(__ANDROID__) || defined(__APPLE__)
#include <dlfcn.h>
#endif

namespace aetherkiri::renpy::mobile {
namespace {

constexpr int kOk = RENPY_MOBILE_OK;

#if defined(__ANDROID__) || defined(__APPLE__)
template <typename Function>
Function ResolveSymbol(void* handle, const char* name) {
  return reinterpret_cast<Function>(dlsym(handle, name));
}
#endif

}  // namespace

Launcher::Launcher() = default;

Launcher::~Launcher() {
  Shutdown();
  Unload();
}

bool Launcher::Resolve() {
  if (resolved_) return init_ != nullptr;
  resolved_ = true;

#if defined(__ANDROID__)
  const char* library_name = "librenpython.so";
  library_handle_ = dlopen(library_name, RTLD_NOW | RTLD_LOCAL);
  if (!library_handle_) {
    const char* detail = dlerror();
    last_error_ = "host lifecycle launcher unavailable: could not load ";
    last_error_ += library_name;
    if (detail) {
      last_error_ += ": ";
      last_error_ += detail;
    }
    return false;
  }
  init_ = ResolveSymbol<InitFn>(library_handle_, "renpy_mobile_init");
  tick_ = ResolveSymbol<TickFn>(library_handle_, "renpy_mobile_tick");
  frame_ = ResolveSymbol<FrameFn>(library_handle_, "renpy_mobile_frame");
  input_ = ResolveSymbol<InputFn>(library_handle_, "renpy_mobile_input");
  pause_ = ResolveSymbol<PauseFn>(library_handle_, "renpy_mobile_pause");
  resume_ = ResolveSymbol<ResumeFn>(library_handle_, "renpy_mobile_resume");
  shutdown_ = ResolveSymbol<ShutdownFn>(library_handle_, "renpy_mobile_shutdown");
#elif defined(__APPLE__)
  // Resolve against the host image instead of emitting strong references to
  // optional symbols. This keeps the normal official Renios archive linkable
  // while allowing a rebuilt lifecycle archive to export the same ABI.
  init_ = ResolveSymbol<InitFn>(RTLD_DEFAULT, "renpy_mobile_init");
  tick_ = ResolveSymbol<TickFn>(RTLD_DEFAULT, "renpy_mobile_tick");
  frame_ = ResolveSymbol<FrameFn>(RTLD_DEFAULT, "renpy_mobile_frame");
  input_ = ResolveSymbol<InputFn>(RTLD_DEFAULT, "renpy_mobile_input");
  pause_ = ResolveSymbol<PauseFn>(RTLD_DEFAULT, "renpy_mobile_pause");
  resume_ = ResolveSymbol<ResumeFn>(RTLD_DEFAULT, "renpy_mobile_resume");
  shutdown_ = ResolveSymbol<ShutdownFn>(RTLD_DEFAULT, "renpy_mobile_shutdown");
#else
  last_error_ = "host lifecycle launcher unavailable on this platform";
#endif

  if (!init_ || !tick_ || !frame_ || !input_ || !pause_ || !resume_ ||
      !shutdown_) {
    last_error_ =
        "host lifecycle launcher unavailable: rebuilt Ren'Py payload must "
        "export renpy_mobile_init/tick/frame/input/pause/resume/shutdown";
    Unload();
    return false;
  }
  return true;
}

bool Launcher::available() {
  return Resolve();
}

int Launcher::Init(const renpy_mobile_config_t& config,
                  const renpy_mobile_host_t& host) {
  if (!Resolve()) return RENPY_MOBILE_NOT_IMPLEMENTED;
  if (initialized_) return RENPY_MOBILE_INVALID_STATE;
  const int result = init_(&config, &host);
  if (result == kOk) {
    initialized_ = true;
  } else {
    last_error_ = "host lifecycle launcher init failed with status " +
                  std::to_string(result);
  }
  return result;
}

int Launcher::Tick(uint32_t budget_ms) {
  if (!initialized_ || !tick_) return RENPY_MOBILE_INVALID_STATE;
  return tick_(budget_ms);
}

int Launcher::Frame(renpy_mobile_frame_t* out_frame) {
  if (!initialized_ || !frame_ || !out_frame) return RENPY_MOBILE_INVALID_STATE;
  return frame_(out_frame);
}

int Launcher::Input(const renpy_mobile_input_t& event) {
  if (!initialized_ || !input_) return RENPY_MOBILE_INVALID_STATE;
  return input_(&event);
}

int Launcher::Pause() {
  if (!initialized_ || !pause_) return RENPY_MOBILE_INVALID_STATE;
  return pause_();
}

int Launcher::Resume() {
  if (!initialized_ || !resume_) return RENPY_MOBILE_INVALID_STATE;
  return resume_();
}

void Launcher::Shutdown() {
  if (initialized_ && shutdown_) shutdown_();
  initialized_ = false;
}

void Launcher::Unload() {
#if defined(__ANDROID__)
  if (library_handle_) dlclose(library_handle_);
#endif
  library_handle_ = nullptr;
  init_ = nullptr;
  tick_ = nullptr;
  frame_ = nullptr;
  input_ = nullptr;
  pause_ = nullptr;
  resume_ = nullptr;
  shutdown_ = nullptr;
  resolved_ = false;
}

}  // namespace aetherkiri::renpy::mobile
