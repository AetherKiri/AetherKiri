#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

#include <spdlog/spdlog.h>

namespace aetherkiri {

struct RgbaImage {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels;
};

struct KtxHeader {
    uint8_t identifier[12];
    uint32_t endianness;
    uint32_t glType;
    uint32_t glTypeSize;
    uint32_t glFormat;
    uint32_t glInternalFormat;
    uint32_t glBaseInternalFormat;
    uint32_t pixelWidth;
    uint32_t pixelHeight;
    uint32_t pixelDepth;
    uint32_t numberOfArrayElements;
    uint32_t numberOfFaces;
    uint32_t numberOfMipmapLevels;
    uint32_t bytesOfKeyValueData;
};

struct BC7ModeInfo {
    uint8_t ns, pb, rb, isb, cb, ab, epb, spb, ib, ib2;
};

static constexpr BC7ModeInfo kBC7Mode[8] = {
    {3, 4, 0, 0, 4, 0, 1, 0, 3, 0},
    {2, 6, 0, 0, 6, 0, 0, 1, 3, 0},
    {3, 6, 0, 0, 5, 0, 0, 0, 2, 0},
    {2, 6, 0, 0, 7, 0, 1, 0, 2, 0},
    {1, 0, 2, 1, 5, 6, 0, 0, 2, 3},
    {1, 0, 2, 0, 7, 8, 0, 0, 2, 2},
    {1, 0, 0, 0, 7, 7, 1, 0, 4, 0},
    {2, 6, 0, 0, 5, 5, 1, 0, 2, 0},
};

static constexpr uint16_t kBC7P2[64] = {
    0xCCCC,0x8888,0xEEEE,0xECC8,0xC880,0xFEEC,0xFEC8,0xEC80,
    0xC800,0xFFEC,0xFE80,0xE800,0xFFE8,0xFF00,0xFFF0,0xF000,
    0xF710,0x008E,0x7100,0x08CE,0x008C,0x7310,0x3100,0x8CCE,
    0x088C,0x3110,0x6666,0x366C,0x17E8,0x0FF0,0x718E,0x399C,
    0xAAAA,0xF0F0,0x5A5A,0x33CC,0x3C3C,0x55AA,0x9696,0xA55A,
    0x73CE,0x13C8,0x324C,0x3BDC,0x6996,0xC33C,0x9966,0x0660,
    0x0272,0x04E4,0x4E40,0x2720,0xC936,0x936C,0x39C6,0x639C,
    0x9336,0x9CC6,0x817E,0xE718,0xCCF0,0x0FCC,0x7744,0xEE22,
};

static constexpr uint32_t kBC7P3[64] = {
    0xAA685050,0x6A5A5040,0x5A5A4200,0x5450A0A8,0xA5A50000,0xA0A05050,0x5555A0A0,0x5A5A5050,
    0xAA550000,0xAA555500,0xAAAA5500,0x90906090,0x94949494,0xA4A4A4A4,0xA9A59450,0x2A0A4250,
    0xA5945040,0x0A425054,0xA5A5A500,0x55A0A0A0,0xA8A85454,0x6A6A4040,0xA4A45000,0x1A1A0500,
    0x0050A4A4,0xAAA59090,0x14696914,0x69691400,0xA08585A0,0xAA821414,0x50A4A450,0x6A5A0200,
    0xA9A58000,0x5090A0A0,0xA8A09050,0x24242424,0x00AA5500,0x24924924,0x24492424,0xAA549524,
    0x0A0A0A0A,0xAA985002,0x0000A5A5,0x96960000,0xA5A5000A,0xA0A0A5A5,0x96000000,0x40804080,
    0xA9A8A9A8,0xAAAAAA44,0x2A4A5254,0x00000000,0xAAAAAAAA,0xAA0A0AAA,0x0A0A0A00,0x0000AAAA,
    0xAAA0AAA0,0x0A0A0000,0x0AA00AA0,0xAA00AA00,0x00AA00AA,0xA0A00A0A,0x0A0A0000,0xAAAA0000,
};

static constexpr uint8_t kBC7A2[64] = {
    15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
    15, 2, 8, 2, 2, 8, 8,15, 2, 8, 2, 2, 8, 8, 2, 2,
    15,15, 6, 8, 2, 8,15,15, 2, 8, 2, 2, 2,15,15, 6,
     6, 2, 6, 8,15,15, 2, 2,15,15,15,15,15, 2, 2,15,
};

static constexpr uint8_t kBC7A3a[64] = {
     3, 3,15,15, 8, 3,15,15, 8, 8, 6, 6, 6, 5, 3, 3,
     3, 3, 8,15, 3, 3, 6,10, 5, 8, 8, 6, 8, 5,15,15,
     8,15, 3, 5, 6,10, 8,15,15, 3,15, 5,15,15,15,15,
     3,15, 5, 5, 5, 8, 5,10, 5,10, 8,13,15,12, 3, 3,
};

static constexpr uint8_t kBC7A3b[64] = {
    15, 8, 8, 3,15,15, 3, 8,15,15,15,15,15,15,15, 8,
    15, 8,15, 3,15, 8,15, 8, 3,15, 6,10,15,15,10, 8,
    15, 3,15,10,10, 8, 9,10, 6,15, 8,15, 3, 6, 6, 8,
    15, 3,15,15,15,15,15,15,15,15,15,15, 3,15,15, 8,
};

static constexpr uint8_t kBC7W2[4] = {0, 21, 43, 64};
static constexpr uint8_t kBC7W3[8] = {0, 9, 18, 27, 37, 46, 55, 64};
static constexpr uint8_t kBC7W4[16] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};

static inline uint8_t BC7Lerp(int e0, int e1, int w) {
    return static_cast<uint8_t>(((64 - w) * e0 + w * e1 + 32) >> 6);
}

static inline void DecodeBC7Block(const uint8_t *src, uint8_t out[4][4][4]) {
    uint64_t lo = 0;
    uint64_t hi = 0;
    std::memcpy(&lo, src, 8);
    std::memcpy(&hi, src + 8, 8);

    uint32_t modeBits = static_cast<uint32_t>(lo) & 0xFF;
    if (!modeBits) {
        std::memset(out, 0, 64);
        return;
    }
    int mode = __builtin_ctz(modeBits);
    int bp = mode + 1;
    auto rd = [&](int n) -> uint32_t {
        if (!n) return 0;
        uint32_t v;
        if (bp < 64) {
            v = static_cast<uint32_t>(lo >> bp);
            if (bp + n > 64)
                v |= static_cast<uint32_t>(hi << (64 - bp));
        } else {
            v = static_cast<uint32_t>(hi >> (bp - 64));
        }
        bp += n;
        return v & ((1u << n) - 1);
    };

    const BC7ModeInfo &m = kBC7Mode[mode];
    int part = rd(m.pb);
    int rot = rd(m.rb);
    int isel = rd(m.isb);

    int ep[3][2][4] = {};
    for (int c = 0; c < 3; c++)
        for (int s = 0; s < m.ns; s++) {
            ep[s][0][c] = rd(m.cb);
            ep[s][1][c] = rd(m.cb);
        }
    if (m.ab)
        for (int s = 0; s < m.ns; s++) {
            ep[s][0][3] = rd(m.ab);
            ep[s][1][3] = rd(m.ab);
        }

    int numCh = m.ab ? 4 : 3;
    if (m.epb) {
        for (int s = 0; s < m.ns; s++)
            for (int e = 0; e < 2; e++) {
                int pb = rd(1);
                for (int c = 0; c < numCh; c++) ep[s][e][c] = (ep[s][e][c] << 1) | pb;
            }
    } else if (m.spb) {
        for (int s = 0; s < m.ns; s++) {
            int pb = rd(1);
            for (int e = 0; e < 2; e++)
                for (int c = 0; c < numCh; c++) ep[s][e][c] = (ep[s][e][c] << 1) | pb;
        }
    }

    int cbits = m.cb + ((m.epb || m.spb) ? 1 : 0);
    int abits = m.ab ? m.ab + ((m.epb || m.spb) ? 1 : 0) : 0;
    for (int s = 0; s < m.ns; s++)
        for (int e = 0; e < 2; e++) {
            for (int c = 0; c < 3; c++)
                ep[s][e][c] = (ep[s][e][c] << (8 - cbits)) | (ep[s][e][c] >> (2 * cbits - 8));
            if (m.ab)
                ep[s][e][3] = (ep[s][e][3] << (8 - abits)) | (ep[s][e][3] >> (2 * abits - 8));
            else
                ep[s][e][3] = 255;
        }

    int anchors[3] = {0, 0, 0};
    if (m.ns >= 2) anchors[1] = kBC7A2[part];
    if (m.ns >= 3) {
        anchors[1] = kBC7A3a[part];
        anchors[2] = kBC7A3b[part];
    }

    const uint8_t *wt1 = (m.ib == 2) ? kBC7W2 : (m.ib == 3) ? kBC7W3 : kBC7W4;
    int idx1[16];
    for (int i = 0; i < 16; i++) {
        int sub = 0;
        if (m.ns == 2) sub = (kBC7P2[part] >> i) & 1;
        else if (m.ns == 3) sub = (kBC7P3[part] >> (2 * i)) & 3;
        bool isAnc = (i == anchors[sub]);
        idx1[i] = rd(m.ib - (isAnc ? 1 : 0));
    }

    int idx2[16] = {};
    const uint8_t *wt2 = nullptr;
    if (m.ib2) {
        wt2 = (m.ib2 == 2) ? kBC7W2 : (m.ib2 == 3) ? kBC7W3 : kBC7W4;
        for (int i = 0; i < 16; i++)
            idx2[i] = rd(m.ib2 - (i == 0 ? 1 : 0));
    }

    for (int i = 0; i < 16; i++) {
        int px = i & 3;
        int py = i >> 2;
        int sub = 0;
        if (m.ns == 2) sub = (kBC7P2[part] >> i) & 1;
        else if (m.ns == 3) sub = (kBC7P3[part] >> (2 * i)) & 3;

        int w1 = wt1[idx1[i]];
        uint8_t r = BC7Lerp(ep[sub][0][0], ep[sub][1][0], w1);
        uint8_t g = BC7Lerp(ep[sub][0][1], ep[sub][1][1], w1);
        uint8_t b = BC7Lerp(ep[sub][0][2], ep[sub][1][2], w1);
        uint8_t a = BC7Lerp(ep[sub][0][3], ep[sub][1][3], w1);

        if (m.ib2) {
            int w2v = wt2[idx2[i]];
            if (isel == 0) {
                a = BC7Lerp(ep[sub][0][3], ep[sub][1][3], w2v);
            } else {
                r = BC7Lerp(ep[sub][0][0], ep[sub][1][0], w2v);
                g = BC7Lerp(ep[sub][0][1], ep[sub][1][1], w2v);
                b = BC7Lerp(ep[sub][0][2], ep[sub][1][2], w2v);
            }
        }

        if (rot == 1) {
            std::swap(a, r);
        } else if (rot == 2) {
            std::swap(a, g);
        } else if (rot == 3) {
            std::swap(a, b);
        }

        out[py][px][0] = r;
        out[py][px][1] = g;
        out[py][px][2] = b;
        out[py][px][3] = a;
    }
}

static inline bool DecodeBC7RGBA(const uint8_t *data, uint32_t dataSize,
                                 uint32_t width, uint32_t height,
                                 std::vector<uint8_t> &outRGBA) {
    uint32_t blockWidth = (width + 3) / 4;
    uint32_t blockHeight = (height + 3) / 4;
    uint64_t expected = static_cast<uint64_t>(blockWidth) * blockHeight * 16u;
    if (!data || dataSize < expected) return false;

    outRGBA.resize(static_cast<size_t>(width) * height * 4);

    auto decodeRows = [&](uint32_t rowStart, uint32_t rowEnd) {
        for (uint32_t by = rowStart; by < rowEnd; by++) {
            const uint8_t *block = data + static_cast<size_t>(by) * blockWidth * 16u;
            for (uint32_t bx = 0; bx < blockWidth; bx++, block += 16) {
                uint8_t rgba[4][4][4];
                DecodeBC7Block(block, rgba);
                uint32_t px0 = bx * 4;
                uint32_t py0 = by * 4;
                for (int y = 0; y < 4; y++) {
                    uint32_t dstY = py0 + static_cast<uint32_t>(y);
                    if (dstY >= height) break;
                    for (int x = 0; x < 4; x++) {
                        uint32_t dstX = px0 + static_cast<uint32_t>(x);
                        if (dstX >= width) break;
                        size_t off = (static_cast<size_t>(dstY) * width + dstX) * 4u;
                        std::memcpy(&outRGBA[off], rgba[y][x], 4);
                    }
                }
            }
        }
    };

    unsigned numThreads = 1;
    if (blockHeight > 64) {
        unsigned hw = std::thread::hardware_concurrency();
        numThreads = std::min(std::max(hw, 1u), 8u);
    }

    auto t0 = std::chrono::steady_clock::now();
    if (numThreads <= 1) {
        decodeRows(0, blockHeight);
    } else {
        std::vector<std::thread> threads;
        threads.reserve(numThreads);
        uint32_t rowsPerThread = blockHeight / numThreads;
        uint32_t remainder = blockHeight % numThreads;
        uint32_t start = 0;
        for (unsigned t = 0; t < numThreads; t++) {
            uint32_t end = start + rowsPerThread + (t < remainder ? 1 : 0);
            threads.emplace_back(decodeRows, start, end);
            start = end;
        }
        for (auto &thread : threads) thread.join();
    }
    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    spdlog::info("bc7_ktx: BC7 decode {}x{}: {} ms ({} threads)",
                 width, height, ms, numThreads);
    return true;
}

static inline bool DecodeKtxToRgba(const uint8_t *data, size_t dataSize,
                                   RgbaImage &out) {
    static constexpr uint8_t kMagic[12] = {
        0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A
    };
    static constexpr uint32_t kKtxEndian = 0x04030201u;
    static constexpr uint32_t kGlCompressedRgbaBptcUnorm = 0x8E8Cu;
    static constexpr uint32_t kGlRgba = 0x1908u;
    static constexpr uint32_t kGlUnsignedByte = 0x1401u;

    out = {};
    if (!data || dataSize < sizeof(KtxHeader)) return false;
    const auto *hdr = reinterpret_cast<const KtxHeader *>(data);
    if (std::memcmp(hdr->identifier, kMagic, sizeof(kMagic)) != 0) return false;
    if (hdr->endianness != kKtxEndian) {
        spdlog::warn("bc7_ktx: unsupported KTX endianness 0x{:08X}", hdr->endianness);
        return false;
    }
    if (hdr->pixelWidth == 0 || hdr->pixelHeight == 0 ||
        hdr->pixelDepth != 0 || hdr->numberOfArrayElements != 0 ||
        hdr->numberOfFaces > 1) {
        spdlog::warn("bc7_ktx: unsupported KTX dimensions/faces {}x{} depth={} arrays={} faces={}",
                     hdr->pixelWidth, hdr->pixelHeight, hdr->pixelDepth,
                     hdr->numberOfArrayElements, hdr->numberOfFaces);
        return false;
    }

    const uint8_t *ptr = data + sizeof(KtxHeader) + hdr->bytesOfKeyValueData;
    const uint8_t *end = data + dataSize;
    if (ptr + 4 > end) return false;
    uint32_t imageSize = 0;
    std::memcpy(&imageSize, ptr, 4);
    ptr += 4;
    if (ptr + imageSize > end) return false;

    out.width = hdr->pixelWidth;
    out.height = hdr->pixelHeight;
    const bool compressed = hdr->glType == 0 && hdr->glFormat == 0;
    if (compressed && hdr->glInternalFormat == kGlCompressedRgbaBptcUnorm) {
        return DecodeBC7RGBA(ptr, imageSize, out.width, out.height, out.pixels);
    }

    if (!compressed && hdr->glType == kGlUnsignedByte &&
        hdr->glFormat == kGlRgba && hdr->glInternalFormat == kGlRgba) {
        const uint64_t expected = static_cast<uint64_t>(out.width) * out.height * 4u;
        if (imageSize < expected) return false;
        out.pixels.assign(ptr, ptr + expected);
        return true;
    }

    spdlog::warn("bc7_ktx: unsupported KTX internal=0x{:04X} type=0x{:04X} format=0x{:04X}",
                 hdr->glInternalFormat, hdr->glType, hdr->glFormat);
    return false;
}

static inline std::string WithKtxExtension(std::string path) {
    const size_t dot = path.find_last_of('.');
    if (dot != std::string::npos) path = path.substr(0, dot);
    path += ".ktx";
    return path;
}

} // namespace aetherkiri
