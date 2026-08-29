// Direct source bridge for krkrz's AVX2 image resampler.  See the SSE2 bridge
// for the legacy-thread to Aether task-pool translation.
#include "../../tjs2/tjsCommHead.h"
#include "../LayerBitmapIntf.h"
#include "../LayerBitmapImpl.h"
#include "../../utils/ThreadIntf.h"
#include "ResampleImageSIMD.h"

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || \
    defined(__x86_64__)

#include <utility>
#include <vector>

namespace aether_krkrz_resample_thread_avx2 {

using LegacyTask = void (TJS_USERENTRY *)(void *);

struct Queue {
    int expected = 0;
    Queue *parent = nullptr;
    std::vector<std::pair<LegacyTask, void *>> tasks;
};

inline thread_local Queue *current = nullptr;

inline void begin(int expected) {
    Queue *parent = current;
    current = new Queue;
    current->parent = parent;
    current->expected = expected > 0 ? expected : 1;
    current->tasks.reserve(static_cast<size_t>(current->expected));
}

inline void exec(LegacyTask task, void *param) {
    if (!task) return;
    if (!current) {
        task(param);
        return;
    }
    current->tasks.emplace_back(task, param);
}

inline void end() {
    Queue *queue = current;
    if (!queue) return;
    current = queue->parent;
    if (queue->tasks.empty()) {
        delete queue;
        return;
    }
    auto tasks = std::move(queue->tasks);
    const bool nested = current != nullptr;
    delete queue;
    if (nested) {
        for (const auto &task : tasks) task.first(task.second);
    } else {
        TVPExecThreadTask(static_cast<int>(tasks.size()), [&](int index) {
            tasks[static_cast<size_t>(index)].first(
                tasks[static_cast<size_t>(index)].second);
        });
    }
}

} // namespace aether_krkrz_resample_thread_avx2

#define TVPBeginThreadTask(n) \
    ::aether_krkrz_resample_thread_avx2::begin(static_cast<int>(n))
#define TVPExecThreadTask(task, param) \
    ::aether_krkrz_resample_thread_avx2::exec((task), (param))
#define TVPEndThreadTask() ::aether_krkrz_resample_thread_avx2::end()
#ifndef TVP_THREAD_PARAM
#define TVP_THREAD_PARAM(param) (param)
#endif
#ifndef KRKRZ_THREAD_PIXEL_SCALE
#define KRKRZ_THREAD_PIXEL_SCALE 100
#endif

#define tTVPBaseBitmap iTVPBaseBitmap

#include "../../../../third_party/krkrz_dev/src/core/common/visual/gl/ResampleImageAVX2.cpp"

#undef tTVPBaseBitmap
#undef KRKRZ_THREAD_PIXEL_SCALE
#undef TVP_THREAD_PARAM
#undef TVPEndThreadTask
#undef TVPExecThreadTask
#undef TVPBeginThreadTask

#endif
