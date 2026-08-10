#include "frame_effect_host.h"

#include <mutex>

namespace godot {
namespace {

std::mutex g_factory_mutex;
FrameEffectProviderFactory g_factory = nullptr;

}  // namespace

bool RegisterFrameEffectProviderFactory(uint32_t host_api_version,
                                        FrameEffectProviderFactory factory) {
    if (host_api_version != AETHERKIRI_FRAME_EFFECT_HOST_API_VERSION ||
        factory == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> guard(g_factory_mutex);
    g_factory = factory;
    return true;
}

void UnregisterFrameEffectProviderFactory() {
    std::lock_guard<std::mutex> guard(g_factory_mutex);
    g_factory = nullptr;
}

std::unique_ptr<FrameEffectProvider> CreateFrameEffectProvider() {
    std::lock_guard<std::mutex> guard(g_factory_mutex);
    return g_factory != nullptr ? g_factory() : nullptr;
}

}  // namespace godot
