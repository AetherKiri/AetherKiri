#ifndef AETHERKIRI_ENGINE_STARTUP_THREAD_H_
#define AETHERKIRI_ENGINE_STARTUP_THREAD_H_

#include <cstddef>
#include <thread>

#if defined(__APPLE__)
#include <exception>
#include <functional>
#include <memory>
#include <pthread.h>
#include <system_error>
#include <utility>
#endif

namespace aetherkiri::engine_api {

// Provider startup may parse/compile shaders as well as load scripts. Apple's
// default 512 KiB pthread stack is too small for debug Naga shader parsing.
// Match the macOS main-thread budget while keeping startup off the UI thread.
inline constexpr size_t kStartupThreadStackSize = 8 * 1024 * 1024;

#if defined(__APPLE__)
class StartupThread {
 public:
  StartupThread() noexcept = default;

  explicit StartupThread(std::function<void()> body) {
    auto task = std::make_unique<std::function<void()>>(std::move(body));
    pthread_attr_t attributes;
    Check(pthread_attr_init(&attributes));
    int result = pthread_attr_setstacksize(&attributes, kStartupThreadStackSize);
    if (result == 0) {
      result = pthread_create(&thread_, &attributes, Run, task.get());
    }
    pthread_attr_destroy(&attributes);
    Check(result);
    task.release();  // Owned by Run after successful pthread_create.
    joinable_ = true;
  }

  StartupThread(const StartupThread&) = delete;
  StartupThread& operator=(const StartupThread&) = delete;

  StartupThread(StartupThread&& other) noexcept { swap(other); }

  StartupThread& operator=(StartupThread&& other) noexcept {
    if (joinable_) std::terminate();
    swap(other);
    return *this;
  }

  ~StartupThread() {
    if (joinable_) std::terminate();
  }

  bool joinable() const noexcept { return joinable_; }

  void join() {
    if (!joinable_) {
      throw std::system_error(std::make_error_code(std::errc::invalid_argument));
    }
    Check(pthread_join(thread_, nullptr));
    joinable_ = false;
  }

  void swap(StartupThread& other) noexcept {
    std::swap(thread_, other.thread_);
    std::swap(joinable_, other.joinable_);
  }

 private:
  static void Check(int result) {
    if (result != 0) {
      throw std::system_error(result, std::generic_category(),
                              "create/join provider startup thread");
    }
  }

  static void* Run(void* opaque) noexcept {
    std::unique_ptr<std::function<void()>> task(
        static_cast<std::function<void()>*>(opaque));
    (*task)();
    return nullptr;
  }

  pthread_t thread_{};
  bool joinable_ = false;
};
#else
using StartupThread = std::thread;
#endif

}  // namespace aetherkiri::engine_api

#endif  // AETHERKIRI_ENGINE_STARTUP_THREAD_H_
