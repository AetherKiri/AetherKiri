// Compatibility bridge for the legacy krkrz renderer task API.  AetherKiri
// uses TVPExecThreadTask(int, function<int>) rather than the old begin/queue/
// end calls, so collect the independent scanline tasks and execute them with
// the engine's thread pool at the end of each resample operation.
#ifndef AETHER_KRKRZ_THREAD_COMPAT_H
#define AETHER_KRKRZ_THREAD_COMPAT_H

#include <cstddef>
#include <functional>
#include <vector>

namespace aether_krkrz_thread_compat {

using Task = std::function<void()>;
inline thread_local std::vector<Task> tasks;

inline void Begin(int threadCount) {
    tasks.clear();
    tasks.reserve(threadCount > 0 ? static_cast<std::size_t>(threadCount) : 0);
}

template <typename Func>
inline void Enqueue(Func func, void *param) {
    tasks.emplace_back([func, param] { func(param); });
}

inline void End() {
    if(!tasks.empty()) {
        auto *taskList = &tasks;
        TVPExecThreadTask(static_cast<int>(tasks.size()),
                          [taskList](int index) { (*taskList)[index](); });
    }
    tasks.clear();
}

} // namespace aether_krkrz_thread_compat

#define TVPBeginThreadTask(num) \
    ::aether_krkrz_thread_compat::Begin((num))
#define TVPEndThreadTask() ::aether_krkrz_thread_compat::End()
#define TVP_THREAD_PARAM(param) static_cast<void *>(param)
#define TVPExecThreadTask(func, param) \
    ::aether_krkrz_thread_compat::Enqueue((func), (param))

#ifndef KRKRZ_THREAD_PIXEL_SCALE
#define KRKRZ_THREAD_PIXEL_SCALE 100
#endif

#endif // AETHER_KRKRZ_THREAD_COMPAT_H
