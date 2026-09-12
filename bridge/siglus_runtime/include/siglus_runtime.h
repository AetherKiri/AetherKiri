#pragma once

#include <cstdint>

namespace aetherkiri::siglus {

// Registers the siglus_rs backend with the shared engine runtime dispatcher.
// Registration is process-wide and idempotent; hosts should call it before
// creating their first engine handle.
void RegisterRuntimeProvider();

}  // namespace aetherkiri::siglus
