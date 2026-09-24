#include "Hxv4Crypto.h"
#include "Hxv4Decoder.h"
#include <cstring>

namespace AetherKiri {
namespace Hxv4 {

// ─── NATIVE BLAKE2S-256 ───
/* 
 * NOTE TO MAINTAINERS regarding Libsodium:
 * While libsodium is used for XChaCha20 decryption, i CANNOT use its `crypto_generichash`
 * (which implements Blake2b - 64-bit word size) here. The Hxv4 protocol strictly mandates
 * Blake2s (32-bit word size) for prefix hashing. The two variants produce fundamentally 
 * different digests. Therefore, i must maintain this pure byte-for-byte C++ implementation 
 * of Blake2s to ensure exact cryptographic compatibility with the original engine.
 */
static inline uint32_t rotr32(uint32_t w, unsigned c) { return (w >> c) | (w << (32 - c)); }
void blake2s_compress(uint32_t state[8], const uint8_t block[64], uint32_t totlen, bool is_last) {
    static const uint32_t IV[8] = { 0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A, 0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19 };
    static const uint8_t SIGMA[10][16] = {
        {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}, {14,10,4,8,9,15,13,6,1,12,0,2,11,7,5,3},
        {11,8,12,0,5,2,15,13,10,14,3,6,7,1,9,4}, {7,9,3,1,13,12,11,14,2,6,5,10,4,0,15,8},
        {9,0,5,7,2,4,10,15,14,1,11,12,6,8,3,13}, {2,12,6,10,0,11,8,3,4,13,7,5,15,14,1,9},
        {12,5,1,15,14,13,4,10,0,7,6,3,9,2,8,11}, {13,11,7,14,12,1,3,9,5,0,15,4,8,6,2,10},
        {6,15,14,9,11,3,0,8,12,2,13,7,1,4,10,5}, {10,2,8,4,7,6,1,5,15,11,9,14,3,12,13,0}
    };
    uint32_t m[16], v[16];
    std::memcpy(m, block, 64); std::memcpy(v, state, 32); std::memcpy(v + 8, IV, 32);
    v[12] ^= totlen; v[14] ^= is_last ? 0xFFFFFFFF : 0;
    for (int i = 0; i < 10; ++i) {
        auto G = [&](int a, int b, int c, int d, int x, int y) {
            v[a] = v[a] + v[b] + m[x]; v[d] = rotr32(v[d] ^ v[a], 16);
            v[c] = v[c] + v[d];        v[b] = rotr32(v[b] ^ v[c], 12);
            v[a] = v[a] + v[b] + m[y]; v[d] = rotr32(v[d] ^ v[a], 8);
            v[c] = v[c] + v[d];        v[b] = rotr32(v[b] ^ v[c], 7);
        };
        const uint8_t* s = SIGMA[i];
        G(0,4,8,12,s[0],s[1]);   G(1,5,9,13,s[2],s[3]);
        G(2,6,10,14,s[4],s[5]);  G(3,7,11,15,s[6],s[7]);
        G(0,5,10,15,s[8],s[9]);  G(1,6,11,12,s[10],s[11]);
        G(2,7,8,13,s[12],s[13]); G(3,4,9,14,s[14],s[15]);
    }
    for (int i = 0; i < 8; ++i) state[i] ^= v[i] ^ v[i + 8];
}

void blake2s_256(const uint8_t* in, size_t inlen, uint8_t out[32]) {
    uint32_t state[8] = { 0x6B08E647, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A, 0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19 };
    uint8_t block[64] = {0}; size_t offset = 0;
    while (inlen > 64) {
        blake2s_compress(state, in + offset, offset + 64, false);
        offset += 64; inlen -= 64;
    }
    std::memcpy(block, in + offset, inlen);
    blake2s_compress(state, block, offset + inlen, true);
    std::memcpy(out, state, 32);
}

uint64_t Hxv4_CalcHashPrefix(const tjs_char* full_path) {
    TJS::ttstr path(full_path);
    const tjs_char* s = path.c_str();
    const tjs_char* p = s + path.GetLen() - 1;
    while (p >= s && *p != TJS_W('/') && *p != TJS_W('\\') && *p != TJS_W(':') && *p != TJS_W('>')) p--;
    p++;
    TJS::ttstr fname(p);

    TJS::ttstr salted = fname + TJS_W("xp3hnp");
    std::vector<uint8_t> utf16le;
    for (int i = 0; i < salted.GetLen(); i++) {
        uint32_t c = static_cast<uint32_t>(salted.c_str()[i]);
        if (c >= 'A' && c <= 'Z') c += ('a' - 'A');
    
        if (c > 0xFFFF) { 
            c -= 0x10000;
            uint16_t high = 0xD800 | ((c >> 10) & 0x3FF);
            uint16_t low = 0xDC00 | (c & 0x3FF);
            utf16le.push_back(high & 0xFF);
            utf16le.push_back((high >> 8) & 0xFF);
            utf16le.push_back(low & 0xFF);
            utf16le.push_back((low >> 8) & 0xFF);
        } else {
            utf16le.push_back(c & 0xFF);
            utf16le.push_back((c >> 8) & 0xFF);
        }
    }

    uint8_t hash16[32];
    blake2s_256(utf16le.data(), utf16le.size(), hash16);
    uint64_t prefix16; std::memcpy(&prefix16, hash16, 8);
    return prefix16;
}

// ─── FILTER RUNTIME STATE ───
void FilterRuntimeState::Apply(uint8_t* data, uint32_t size, uint64_t offset) const {
    if (size == 0) return;
    uint64_t end = offset + size;

    if (has_bulk && offset < 16) {
        uint64_t overlap_end = (end < 16) ? end : 16;
        for (uint64_t i = offset; i < overlap_end; ++i) {
            data[i - offset] ^= bulk_key[i];
        }
    }

    auto apply_boundary = [&](uint32_t pos0, uint32_t pos1, uint32_t key, uint8_t byte0, uint8_t byte1, uint64_t chunk_start, uint64_t chunk_size, uint64_t buffer_start) {
        uint8_t xor_key = key & 0xFF;
        for (uint64_t i = 0; i < chunk_size; ++i) {
            data[buffer_start + i] ^= xor_key;
        }
        if (byte0 && chunk_start <= pos0 && pos0 < chunk_start + chunk_size) {
            data[buffer_start + pos0 - chunk_start] ^= byte0;
        }
        if (byte1 && chunk_start <= pos1 && pos1 < chunk_start + chunk_size) {
            data[buffer_start + pos1 - chunk_start] ^= byte1;
        }
    };

    uint64_t split = split_offset;
    if (split <= offset) apply_boundary(boundary1_pos0, boundary1_pos1, boundary1_key, boundary1_byte0, boundary1_byte1, offset, size, 0);
    else if (split < end) {
        uint64_t first_size = split - offset;
        apply_boundary(boundary0_pos0, boundary0_pos1, boundary0_key, boundary0_byte0, boundary0_byte1, offset, first_size, 0);
        apply_boundary(boundary1_pos0, boundary1_pos1, boundary1_key, boundary1_byte0, boundary1_byte1, split, end - split, first_size);
    } 
    else apply_boundary(boundary0_pos0, boundary0_pos1, boundary0_key, boundary0_byte0, boundary0_byte1, offset, size, 0);
}

// ─── DRIPVM ───
static uint32_t EvalRecords(const DripOp* records, int& pc, uint32_t result, uint32_t seed, uint32_t scratch, const Hxv4Session* session) {
    while (pc < 100) {
        const auto& op = records[pc++];
        if (op.op == 0x51D90) break;
        if (op.op == 0x17C60) { result = EvalRecords(records, pc, result, seed, scratch, session); continue; }
        switch(op.op) {
            case 0x17C50: result += op.param; break;
            case 0x17CB0: result += scratch; break;
            case 0x17CD0: result *= scratch; break;
            case 0x17CF0: result = scratch - result; break;
            case 0x17D10: result <<= (scratch & 0xF); break;
            case 0x17D30: result >>= (scratch & 0xF); break;
            case 0x17D50: result -= scratch; break;
            case 0x17D70: result = (2 * (result & ~op.param)) | ((op.param >> 1) & (result >> 1)); break;
            case 0x17DA0: result = op.param; break;
            case 0x17DB0: result = seed; break;
            case 0x17DD0: result -= 1; break;
            case 0x17DE0: result += 1; break;
            case 0x17DF0: result = -result; break;
            case 0x17E00: result = ~result; break;
            case 0x17E10: result = session->m_contextU32[op.param]; break;
            case 0x17E30: result = session->m_contextU32[op.param & result]; break;
            case 0x17E50: result -= op.param; break;
            case 0x17E60: scratch = result; break;
            case 0x17E80: result ^= op.param; break;
        }
    }
    return result;
}

static uint64_t DripGet64(uint32_t value, const Hxv4Session* session) {
    int lane = value & 0x7F; uint32_t seed = value >> 7;
    int pc = 0; uint32_t lo = EvalRecords(session->m_dripLanesPtrs[lane], pc, 0, seed, 0, session);
    pc = 0; uint32_t hi = EvalRecords(session->m_dripLanesPtrs[lane], pc, 0, ~seed, 0, session);
    return ((uint64_t)hi << 32) | lo;
}

FilterRuntimeState SetupDripVM(uint64_t key64, const Hxv4Session* session) {
    FilterRuntimeState fs;
    uint32_t key_lo = key64 & 0xFFFFFFFF;
    uint32_t key_hi = key64 >> 32;

    uint64_t bound0 = DripGet64(key_lo, session);
    fs.boundary0_pos0 = (bound0 >> 48) & 0xFFFF; fs.boundary0_pos1 = (bound0 >> 32) & 0xFFFF;
    if (fs.boundary0_pos0 == fs.boundary0_pos1) fs.boundary0_pos1++;
    fs.boundary0_byte0 = (bound0 >> 8) & 0xFF;   fs.boundary0_byte1 = (bound0 >> 16) & 0xFF;
    uint32_t b0_key = bound0 & 0xFF; if (b0_key == 0) b0_key = 0xA5;
    fs.boundary0_key = b0_key * 0x01010101;
    
    uint64_t bound1 = DripGet64(key_hi, session);
    fs.boundary1_pos0 = (bound1 >> 48) & 0xFFFF; fs.boundary1_pos1 = (bound1 >> 32) & 0xFFFF;
    if (fs.boundary1_pos0 == fs.boundary1_pos1) fs.boundary1_pos1++;
    fs.boundary1_byte0 = (bound1 >> 8) & 0xFF;   fs.boundary1_byte1 = (bound1 >> 16) & 0xFF;
    uint32_t b1_key = bound1 & 0xFF; if (b1_key == 0) b1_key = 0xA5;
    fs.boundary1_key = b1_key * 0x01010101;
    
    fs.split_offset = session->m_holderWords[5] + (session->m_holderWords[4] & (key64 >> 16));
    fs.has_bulk = true;
    
    uint64_t cur = ~key64;
    for (int out = 0; out < 16; ++out) {
        if (out % 8 == 0) cur = ~DripGet64(cur & 0xFFFFFFFF, session);
        fs.bulk_key[out] = (cur >> ((7 - (out % 8)) * 8)) & 0xFF;
    }

    return fs;
}

} // namespace Hxv4
} // namespace AetherKiri