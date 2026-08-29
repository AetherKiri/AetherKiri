// Direct source bridge for krkrz's SSE2 image resampler.
//
// The upstream source is compiled against Aether's Layer/Bitmap headers and
// the local legacy-thread shim below.  No implementation is copied into the
// parent repository and no upstream registry is linked.
#include "../../tjs2/tjsCommHead.h"
#include "../LayerBitmapIntf.h"
#include "../LayerBitmapImpl.h"
#include "../../utils/ThreadIntf.h"
#include "ResampleImageSIMD.h"

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || \
    defined(__x86_64__)

#include <utility>
#include <vector>

namespace aether_krkrz_resample_thread {

using LegacyTask = void (TJS_USERENTRY *)(void *);

struct Queue {
    int expected = 0;
    Queue *parent = nullptr;
    std::vector<std::pair<LegacyTask, void *>> tasks;
};

// Upstream's Begin/Exec/End contract is translated to Aether's indexed task
// API.  A thread-local queue keeps nested calls independent and avoids a
// process-global lock in the hot image path.
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
        // Re-entering the indexed pool from a worker can deadlock pools that
        // have no spare workers. A nested legacy group is drained directly.
        for (const auto &task : tasks) task.first(task.second);
    } else {
        TVPExecThreadTask(static_cast<int>(tasks.size()), [&](int index) {
            tasks[static_cast<size_t>(index)].first(
                tasks[static_cast<size_t>(index)].second);
        });
    }
}

} // namespace aether_krkrz_resample_thread

// The pinned source uses the historical queue API.  Keep these aliases local
// to this translation unit; Aether's public ThreadIntf remains unchanged.
#define TVPBeginThreadTask(n) \
    ::aether_krkrz_resample_thread::begin(static_cast<int>(n))
#define TVPExecThreadTask(task, param) \
    ::aether_krkrz_resample_thread::exec((task), (param))
#define TVPEndThreadTask() ::aether_krkrz_resample_thread::end()
#ifndef TVP_THREAD_PARAM
#define TVP_THREAD_PARAM(param) (param)
#endif
#ifndef KRKRZ_THREAD_PIXEL_SCALE
#define KRKRZ_THREAD_PIXEL_SCALE 100
#endif

// krkrz names the software bitmap interface tTVPBaseBitmap.  Aether's
// resampling entry point deliberately accepts the broader iTVPBaseBitmap so
// GPU-backed/alternate bitmap owners retain their scalar fallback.  The
// upstream leaf only calls methods provided by that interface, therefore the
// alias is safe and keeps the ABI boundary explicit.
#define tTVPBaseBitmap iTVPBaseBitmap

#include "../../../../third_party/krkrz_dev/src/core/common/visual/gl/ResampleImageSSE2.cpp"

#undef tTVPBaseBitmap
#undef KRKRZ_THREAD_PIXEL_SCALE
#undef TVP_THREAD_PARAM
#undef TVPEndThreadTask
#undef TVPExecThreadTask
#undef TVPBeginThreadTask

#endif
