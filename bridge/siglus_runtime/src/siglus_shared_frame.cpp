#include "siglus_shared_frame.h"
namespace aetherkiri::siglus {
struct SharedFrame::Impl {};
SharedFrame::SharedFrame() = default;
SharedFrame::~SharedFrame() = default;
bool SharedFrame::begin(void *, uint32_t, uint32_t) { return false; }
bool SharedFrame::publish(uint64_t) { return false; }
void SharedFrame::reset(void *) {}
bool SharedFrame::get(uint64_t *, uint32_t *, uint32_t *, uint64_t *) const { return false; }
}
