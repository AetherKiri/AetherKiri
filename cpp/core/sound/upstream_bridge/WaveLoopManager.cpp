// Aether ABI bridge for krkrz's WaveLoopManager implementation.
//
// The public header is included first, so the upstream translation unit is
// compiled against Aether's class layout (including DesiredFormat).  The
// implementation body remains in the pinned submodule; only the allocator
// and one Aether-specific virtual method live in this adapter.
#include "../../tjs2/tjsCommHead.h"
#include "WaveLoopManager.h"
#include "WaveIntf.h"

#include <cstddef>
#include <new>

extern "C" void *AetherKrkrzLoopSoundMalloc(std::size_t size) {
    // `new[]` preserves the upstream try/catch failure path.  Normalize zero
    // to one byte because zero-sized allocations are implementation-defined.
    return ::operator new[](size == 0 ? 1 : size);
}

extern "C" void AetherKrkrzLoopSoundFree(void *pointer) {
    ::operator delete[](pointer);
}

#define sound_malloc AetherKrkrzLoopSoundMalloc
#define sound_free AetherKrkrzLoopSoundFree
#include "../../../../third_party/krkrz_dev/src/core/common/sound/WaveLoopManager.cpp"
#undef sound_free
#undef sound_malloc

// Aether's decoder host added this virtual operation after the upstream
// WaveLoopManager API.  Keep it in the adapter so the upstream class body can
// be consumed unchanged while callers still get the Aether format contract.
bool tTVPWaveLoopManager::DesiredFormat(const tTVPWaveFormat &format) {
    if(!Decoder || !Decoder->DesiredFormat(format))
        return false;
    SetDecoder(Decoder);
    return true;
}
