#include "TextTransform.h"

#include <atomic>

namespace {

std::atomic<tTVPTextTransformCallback> g_text_transform_callback{nullptr};
std::atomic<tTVPTextPrefetchCallback> g_text_prefetch_callback{nullptr};

}  // namespace

void TVPSetTextTransformCallback(tTVPTextTransformCallback callback) {
    g_text_transform_callback.store(callback, std::memory_order_release);
}

void TVPSetTextPrefetchCallback(tTVPTextPrefetchCallback callback) {
    g_text_prefetch_callback.store(callback, std::memory_order_release);
}

std::string TVPTransformText(const char *runtime_id,
                             const std::string &input) {
    const auto callback =
        g_text_transform_callback.load(std::memory_order_acquire);
    if(callback == nullptr || input.empty())
        return input;

    std::string output;
    try {
        if(callback(runtime_id != nullptr ? runtime_id : "", input, &output) &&
           !output.empty())
            return output;
    } catch(...) {
        // Text rendering must remain fail-open: a translation problem must not
        // break the game or suppress its original text.
    }
    return input;
}

void TVPPrefetchText(const char *runtime_id, const std::string &input) {
    const auto callback =
        g_text_prefetch_callback.load(std::memory_order_acquire);
    if(callback == nullptr || input.empty())
        return;
    try {
        callback(runtime_id != nullptr ? runtime_id : "", input);
    } catch(...) {
        // Speculative work must never affect parser execution.
    }
}
