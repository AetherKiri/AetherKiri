#pragma once
#include <cstdint>
#include <memory>

namespace aetherkiri::siglus {
// Retired, triple-buffered IOSurface targets. Fall back to CPU when the host
// cannot import or synchronize the producer's textures.
class SharedFrame {
public:
    SharedFrame();
    ~SharedFrame();
    bool begin(void *runtime, uint32_t width, uint32_t height);
    bool publish(uint64_t serial);
    void reset(void *runtime);
    bool get(uint64_t *texture, uint32_t *width, uint32_t *height, uint64_t *serial) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
