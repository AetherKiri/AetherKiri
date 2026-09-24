#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include "tjsString.h" 

namespace AetherKiri {
namespace Hxv4 {

class Hxv4Session;

void blake2s_compress(uint32_t state[8], const uint8_t block[64], uint32_t totlen, bool is_last);
void blake2s_256(const uint8_t* in, size_t inlen, uint8_t out[32]);
uint64_t Hxv4_CalcHashPrefix(const tjs_char* full_path);

struct FilterRuntimeState {
    uint32_t boundary0_pos0, boundary0_pos1, boundary0_key;
    uint8_t  boundary0_byte0, boundary0_byte1;
    uint32_t boundary1_pos0, boundary1_pos1, boundary1_key;
    uint8_t  boundary1_byte0, boundary1_byte1;
    uint64_t split_offset;
    uint8_t  bulk_key[16];
    bool     has_bulk = false;
    void Apply(uint8_t* data, uint32_t size, uint64_t offset) const;
};

FilterRuntimeState SetupDripVM(uint64_t key64, const Hxv4Session* session);

} // namespace Hxv4
} // namespace AetherKiri