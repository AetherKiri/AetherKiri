#pragma once

#include <cstddef>

// Declaration-only shim.  WaveLoopManager.cpp renames these symbols to a
// translation-unit-specific allocator before including the upstream source;
// this prevents a future krkrz SoundAllocator from colliding at link time.
extern "C" void *sound_malloc(std::size_t size);
extern "C" void sound_free(void *pointer);
