#ifndef AETHERKIRI_LUCA_RUNTIME_H_
#define AETHERKIRI_LUCA_RUNTIME_H_

namespace aetherkiri::luca {

/* Registers the LucaSystem (PROTOTYPE "LUCA System") runtime provider with
 * the engine runtime registry. Safe to call more than once; registration
 * failures keep the runtime unavailable without crashing the host. */
void RegisterRuntimeProvider();

}  // namespace aetherkiri::luca

#endif  // AETHERKIRI_LUCA_RUNTIME_H_
