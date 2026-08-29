//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// TLG5/6 decoder
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include "GraphicsLoaderIntf.h"
#include "CharacterSet.h"
#include "StorageIntf.h"
#include "MsgIntf.h"
#include "tjsUtils.h"
#include "tvpgl.h"
#include "tjsDictionary.h"
#include "TVPDecodeArena.h"

#include <stdlib.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string>
#include <vector>
#include <spdlog/spdlog.h>
#include <lz4.h>

// The pinned krkrz TLG implementation owns this prediction table.  The
// bounded preflight below reads it through the same C-linkage symbol as the
// upstream decoder; no second copy of the table is kept in Aether.
extern "C" char TVPTLG6GolombBitLengthTable[1024][4];

namespace {
constexpr tjs_uint64 kMaxTLGMaterializedBytes = 512ULL * 1024ULL * 1024ULL;
constexpr tjs_uint32 kMaxTLGMetadataChunkBytes = 16U * 1024U * 1024U;
// Keep attacker-controlled index counts bounded even when the container is
// syntactically small enough to fit in the materialized input limit.
constexpr tjs_uint64 kMaxTLGTableEntries = 1'000'000ULL;

bool TVPTLGFormatTraceEnabled() {
    static const bool enabled = [] {
        const char *value = std::getenv("AETHERKIRI_TLG_HEADER_TRACE");
        return value && *value && *value != '0';
    }();
    return enabled;
}

bool TLGDecodeTraceEnabled() {
    static const bool enabled = [] {
        const char *image_trace = std::getenv("AETHERKIRI_IMAGE_LOAD_TRACE");
        const char *motion_trace =
            std::getenv("AETHERKIRI_MOTION_RENDER_PROFILE");
        return (image_trace && *image_trace && *image_trace != '0') ||
               (motion_trace && *motion_trace && *motion_trace != '0');
    }();
    return enabled;
}

void TVPTraceTLGFormatStream(const char *stage, tTJSBinaryStream *src) {
    if(!TVPTLGFormatTraceEnabled() || !src)
        return;
    const tjs_uint64 position = src->GetPosition();
    const tjs_uint64 size = src->GetSize();
    unsigned char bytes[32] = {};
    const tjs_uint read = src->Read(bytes, sizeof(bytes));
    src->SetPosition(position);
    char hex[sizeof(bytes) * 2 + 1] = {};
    size_t out = 0;
    for(tjs_uint i = 0; i < read && out + 2 < sizeof(hex); ++i) {
        const int written = std::snprintf(hex + out, sizeof(hex) - out,
                                          "%02x", bytes[i]);
        if(written <= 0)
            break;
        out += static_cast<size_t>(written);
    }
    spdlog::info("TLGFormatTrace stage={} name={} size={} pos={} head={}",
                 stage ? stage : "?", TVPGetCurrentGraphicLoadName().AsStdString(),
                 static_cast<unsigned long long>(size),
                 static_cast<unsigned long long>(position), hex);
}

// Temporary, opt-in evidence collector for the proprietary TLGmux container.
// It is intentionally disabled unless explicitly requested and is removed
// once the decoder is implemented; keeping the stream position unchanged is
// important because this helper runs on the live image-load path.
void TVPDumpTLGMuxStream(tTJSBinaryStream *src) {
    static const bool enabled = [] {
        const char *value = std::getenv("AETHERKIRI_TLGMUX_DUMP");
        return value && *value && *value != '0';
    }();
    if(!enabled || !src)
        return;
    static std::atomic<int> sequence{0};
    const int index = sequence.fetch_add(1, std::memory_order_relaxed);
    if(index >= 12)
        return;
    const tjs_uint64 position = src->GetPosition();
    const tjs_uint64 size = src->GetSize();
    if(size == 0 || size > 128ULL * 1024ULL * 1024ULL)
        return;
    std::ofstream out("/tmp/aetherkiri-tlgmux-" + std::to_string(index) +
                          ".bin", std::ios::binary);
    if(!out)
        return;
    std::array<tjs_uint8, 64 * 1024> buffer{};
    src->SetPosition(0);
    tjs_uint64 remaining = size;
    while(remaining) {
        const tjs_uint want = static_cast<tjs_uint>(
            std::min<tjs_uint64>(remaining, buffer.size()));
        const tjs_uint got = src->Read(buffer.data(), want);
        if(got == 0)
            break;
        out.write(reinterpret_cast<const char *>(buffer.data()), got);
        remaining -= got;
    }
    src->SetPosition(position);
    spdlog::info("TLGmuxDump path=/tmp/aetherkiri-tlgmux-{}.bin size={} name={}",
                 index, static_cast<unsigned long long>(size),
                 TVPGetCurrentGraphicLoadName().AsStdString());
}

struct TVPTLGMuxSlice {
    tjs_uint32 Left = 0;
    tjs_uint32 Top = 0;
    tjs_uint32 Width = 0;
    tjs_uint32 Height = 0;
    tjs_uint64 Offset = 0;
};

struct TVPTLGMuxFile {
    tjs_uint8 Colors = 0;
    tjs_uint32 Width = 0;
    tjs_uint32 Height = 0;
    tjs_uint64 DataBase = 0;
    std::vector<TVPTLGMuxSlice> Slices;
    std::vector<tjs_uint8> Bytes;
};

static tjs_uint32 TVPReadTLGUInt32LE(const tjs_uint8 *p) {
    return static_cast<tjs_uint32>(p[0]) |
           (static_cast<tjs_uint32>(p[1]) << 8) |
           (static_cast<tjs_uint32>(p[2]) << 16) |
           (static_cast<tjs_uint32>(p[3]) << 24);
}

static tjs_uint64 TVPReadTLGUInt64LE(const tjs_uint8 *p) {
    tjs_uint64 value = 0;
    for(int i = 0; i < 8; ++i)
        value |= static_cast<tjs_uint64>(p[i]) << (i * 8);
    return value;
}

static bool TVPHasTLGBytes(const std::vector<tjs_uint8> &bytes,
                           tjs_uint64 offset, tjs_uint64 count) {
    return offset <= bytes.size() && count <= bytes.size() - offset;
}

static bool TVPIsTLGQOIHeader(const std::vector<tjs_uint8> &bytes,
                              tjs_uint64 offset) {
    static constexpr char kHeader[] = "TLGqoi\0raw\x1a";
    return TVPHasTLGBytes(bytes, offset, sizeof(kHeader) - 1) &&
           std::memcmp(bytes.data() + offset, kHeader,
                       sizeof(kHeader) - 1) == 0;
}

static bool TVPParseTLGMux(const std::vector<tjs_uint8> &bytes,
                           TVPTLGMuxFile &mux, std::string &error) {
    static constexpr char kHeader[] = "TLGmux\0idx\x1a";
    if(!TVPHasTLGBytes(bytes, 0, 20) ||
       std::memcmp(bytes.data(), kHeader, sizeof(kHeader) - 1) != 0) {
        error = "invalid TLGmux header";
        return false;
    }

    mux.Bytes = bytes;
    mux.Colors = bytes[11];
    mux.Width = TVPReadTLGUInt32LE(bytes.data() + 12);
    mux.Height = TVPReadTLGUInt32LE(bytes.data() + 16);
    if((mux.Colors != 3 && mux.Colors != 4) || mux.Width == 0 ||
       mux.Height == 0 ||
       static_cast<tjs_uint64>(mux.Width) * mux.Height >
           kMaxTLGMaterializedBytes / 4) {
        error = "unsupported TLGmux canvas header";
        return false;
    }

    tjs_uint64 position = 20;
    bool sawMuxChunk = false;
    tjs_uint64 dataCandidate = position;
    while(TVPHasTLGBytes(bytes, position, 8)) {
        const tjs_uint8 *chunk = bytes.data() + position;
        const tjs_uint32 chunkSize = TVPReadTLGUInt32LE(chunk + 4);
        const tjs_uint64 payload = position + 8;
        if(!TVPHasTLGBytes(bytes, payload, chunkSize)) {
            error = "truncated TLGmux chunk";
            return false;
        }

        if(std::memcmp(chunk, "CMUX", 4) == 0) {
            sawMuxChunk = true;
            if(chunkSize < 4) {
                error = "invalid TLGmux CMUX chunk";
                return false;
            }
            const tjs_uint8 *payloadBytes = bytes.data() + payload;
            const tjs_uint32 count = TVPReadTLGUInt32LE(payloadBytes);
            const tjs_uint64 expected = 4ULL + 24ULL * count;
            if(count > kMaxTLGTableEntries || expected > chunkSize) {
                error = "truncated TLGmux CMUX index";
                return false;
            }
            if(count > 0 && mux.Slices.size() >
                                std::numeric_limits<size_t>::max() - count) {
                error = "TLGmux index is too large";
                return false;
            }
            mux.Slices.reserve(mux.Slices.size() + count);
            for(tjs_uint32 i = 0; i < count; ++i) {
                const tjs_uint8 *entry = payloadBytes + 4 + 24ULL * i;
                TVPTLGMuxSlice slice;
                slice.Left = TVPReadTLGUInt32LE(entry + 0);
                slice.Top = TVPReadTLGUInt32LE(entry + 4);
                slice.Width = TVPReadTLGUInt32LE(entry + 8);
                slice.Height = TVPReadTLGUInt32LE(entry + 12);
                slice.Offset = TVPReadTLGUInt64LE(entry + 16);
                if(slice.Width == 0 || slice.Height == 0 ||
                   slice.Left > mux.Width || slice.Top > mux.Height ||
                   slice.Width > mux.Width - slice.Left ||
                   slice.Height > mux.Height - slice.Top) {
                    error = "TLGmux slice is outside the canvas";
                    return false;
                }
                mux.Slices.push_back(slice);
            }
            position = payload + chunkSize;
            dataCandidate = position;
            continue;
        }

        // PackinOne terminates the CMUX index with one ordinary chunk header
        // (usually a zero tag/zero length).  Its selected offsets are
        // relative to the position after that header and payload.
        position = payload + chunkSize;
        dataCandidate = position;
        break;
    }

    if(!sawMuxChunk || mux.Slices.empty()) {
        error = "TLGmux has no CMUX index";
        return false;
    }

    tjs_uint64 minimumOffset = mux.Slices.front().Offset;
    for(const auto &slice : mux.Slices)
        minimumOffset = std::min(minimumOffset, slice.Offset);

    // The first encoded image is the anchor for the CMUX-relative offsets.
    // Searching for the TLGqoi signature also tolerates mux writers that add
    // a non-CMUX metadata chunk between the index and the payload.
    tjs_uint64 firstImage = std::numeric_limits<tjs_uint64>::max();
    for(tjs_uint64 p = dataCandidate; TVPHasTLGBytes(bytes, p, 11); ++p) {
        if(TVPIsTLGQOIHeader(bytes, p)) {
            firstImage = p;
            break;
        }
    }
    if(firstImage == std::numeric_limits<tjs_uint64>::max()) {
        for(tjs_uint64 p = 20; TVPHasTLGBytes(bytes, p, 11); ++p) {
            if(TVPIsTLGQOIHeader(bytes, p)) {
                firstImage = p;
                break;
            }
        }
    }
    if(firstImage == std::numeric_limits<tjs_uint64>::max() ||
       firstImage < minimumOffset) {
        error = "TLGmux has no TLGqoi payload";
        return false;
    }
    mux.DataBase = firstImage - minimumOffset;

    for(const auto &slice : mux.Slices) {
        if(slice.Offset > std::numeric_limits<tjs_uint64>::max() - mux.DataBase) {
            error = "TLGmux slice offset overflows";
            return false;
        }
        const tjs_uint64 imageOffset = mux.DataBase + slice.Offset;
        if(imageOffset < mux.DataBase ||
           !TVPIsTLGQOIHeader(bytes, imageOffset) ||
           !TVPHasTLGBytes(bytes, imageOffset, 28)) {
            error = "TLGmux slice points outside the payload";
            return false;
        }
    }
    return true;
}

static bool TVPDecodeTLGQOI(const std::vector<tjs_uint8> &bytes,
                            tjs_uint64 offset, tjs_uint64 limit,
                            std::vector<tjs_uint8> &rgba, tjs_uint32 &width,
                            tjs_uint32 &height, tjs_uint8 &colors,
                            std::string &error) {
    if(!TVPIsTLGQOIHeader(bytes, offset) ||
       !TVPHasTLGBytes(bytes, offset, 28) || limit > bytes.size() ||
       offset >= limit) {
        error = "invalid TLGqoi payload";
        return false;
    }
    colors = bytes[offset + 11];
    width = TVPReadTLGUInt32LE(bytes.data() + offset + 12);
    height = TVPReadTLGUInt32LE(bytes.data() + offset + 16);
    if((colors != 3 && colors != 4) || width == 0 || height == 0) {
        error = "unsupported TLGqoi header";
        return false;
    }
    const tjs_uint64 pixelCount = static_cast<tjs_uint64>(width) * height;
    if(pixelCount > kMaxTLGMaterializedBytes / 4 ||
       pixelCount > std::numeric_limits<size_t>::max() / 4) {
        error = "TLGqoi image is too large";
        return false;
    }
    const tjs_uint64 payload = offset + 28;
    if(payload > limit) {
        error = "truncated TLGqoi payload";
        return false;
    }

    struct Pixel {
        tjs_uint8 R = 0;
        tjs_uint8 G = 0;
        tjs_uint8 B = 0;
        tjs_uint8 A = 0;
    };
    std::array<Pixel, 64> index{};
    Pixel pixel{0, 0, 0, 255};
    rgba.resize(static_cast<size_t>(pixelCount) * 4);
    tjs_uint64 produced = 0;
    tjs_uint64 cursor = payload;

    auto emit = [&](const Pixel &value) {
        const size_t out = static_cast<size_t>(produced) * 4;
        rgba[out + 0] = value.R;
        rgba[out + 1] = value.G;
        rgba[out + 2] = value.B;
        rgba[out + 3] = value.A;
        index[(value.R * 3 + value.G * 5 + value.B * 7 + value.A * 11) &
              63] = value;
        ++produced;
    };

    while(produced < pixelCount) {
        if(cursor >= limit) {
            error = "truncated TLGqoi pixel stream";
            return false;
        }
        const tjs_uint8 tag = bytes[cursor++];
        if(tag == 0xfe) {
            if(!TVPHasTLGBytes(bytes, cursor, 3) || cursor + 3 > limit) {
                error = "truncated TLGqoi RGB opcode";
                return false;
            }
            pixel.R = bytes[cursor++];
            pixel.G = bytes[cursor++];
            pixel.B = bytes[cursor++];
            emit(pixel);
        } else if(tag == 0xff) {
            if(!TVPHasTLGBytes(bytes, cursor, 4) || cursor + 4 > limit) {
                error = "truncated TLGqoi RGBA opcode";
                return false;
            }
            pixel.R = bytes[cursor++];
            pixel.G = bytes[cursor++];
            pixel.B = bytes[cursor++];
            pixel.A = bytes[cursor++];
            emit(pixel);
        } else if((tag & 0xc0) == 0x00) {
            pixel = index[tag & 0x3f];
            emit(pixel);
        } else if((tag & 0xc0) == 0x40) {
            pixel.R = static_cast<tjs_uint8>(pixel.R +
                                              ((tag >> 4 & 0x03) - 2));
            pixel.G = static_cast<tjs_uint8>(pixel.G +
                                              ((tag >> 2 & 0x03) - 2));
            pixel.B = static_cast<tjs_uint8>(pixel.B + ((tag & 0x03) - 2));
            emit(pixel);
        } else if((tag & 0xc0) == 0x80) {
            if(cursor >= limit) {
                error = "truncated TLGqoi luma opcode";
                return false;
            }
            const tjs_uint8 next = bytes[cursor++];
            const int dg = static_cast<int>(tag & 0x3f) - 32;
            const int dr = dg + static_cast<int>((next >> 4) & 0x0f) - 8;
            const int db = dg + static_cast<int>(next & 0x0f) - 8;
            pixel.R = static_cast<tjs_uint8>(pixel.R + dr);
            pixel.G = static_cast<tjs_uint8>(pixel.G + dg);
            pixel.B = static_cast<tjs_uint8>(pixel.B + db);
            emit(pixel);
        } else {
            const tjs_uint64 run = (tag & 0x3f) + 1ULL;
            if(run > pixelCount - produced) {
                error = "TLGqoi run exceeds image size";
                return false;
            }
            for(tjs_uint64 i = 0; i < run; ++i)
                emit(pixel);
        }
    }
    return true;
}

// Newer KiriKiri builds use two related TLG formats for event CGs.  A small
// TLGref file points at a TLGqoi+QHDR container, while the container stores
// several interleaved images in QOI/LZ4 bands.  Keep this implementation in
// the generic TLG loader: the format is independent of PackinOne and is also
// used by games which do not load that plug-in.
struct TVPTLGRefInfo {
    ttstr Container;
    tjs_uint32 Index = 0;
    tjs_uint32 Count = 0;
};

static bool TVPReadTLGLEB128(const std::vector<tjs_uint8> &bytes,
                             tjs_uint64 &position, tjs_uint64 limit,
                             tjs_uint64 &value) {
    value = 0;
    int shift = 0;
    while(position < limit && shift < 64) {
        const tjs_uint8 byte = bytes[static_cast<size_t>(position++)];
        const tjs_uint64 part = static_cast<tjs_uint64>(byte & 0x7f);
        if(shift >= 63 && part > (std::numeric_limits<tjs_uint64>::max() >> shift))
            return false;
        value |= part << shift;
        if((byte & 0x80) == 0)
            return true;
        shift += 7;
    }
    return false;
}

static bool TVPParseTLGRef(const std::vector<tjs_uint8> &bytes,
                           TVPTLGRefInfo &ref, std::string &error) {
    static constexpr char kHeader[] = "TLGref\0raw\x1a";
    if(!TVPHasTLGBytes(bytes, 0, 28) ||
       std::memcmp(bytes.data(), kHeader, sizeof(kHeader) - 1) != 0) {
        error = "invalid TLGref header";
        return false;
    }
    if(std::memcmp(bytes.data() + 20, "QREF", 4) != 0) {
        error = "TLGref has no QREF chunk";
        return false;
    }
    const tjs_uint32 chunkSize = TVPReadTLGUInt32LE(bytes.data() + 24);
    if(chunkSize < 16 || !TVPHasTLGBytes(bytes, 28, chunkSize)) {
        error = "truncated TLGref QREF chunk";
        return false;
    }
    const tjs_uint8 *chunk = bytes.data() + 28;
    ref.Index = TVPReadTLGUInt32LE(chunk + 4);
    ref.Count = TVPReadTLGUInt32LE(chunk + 8);
    const tjs_uint32 nameBytes = TVPReadTLGUInt32LE(chunk + 12);
    if((nameBytes & 1) != 0 || nameBytes > chunkSize - 16 || nameBytes < 2) {
        error = "invalid TLGref container name";
        return false;
    }
    // The game stores a BMP/UTF-16LE name and includes its terminating NUL
    // in the byte count.  Do not reinterpret the byte buffer as uint16_t:
    // a QREF chunk is only byte-aligned and that cast is undefined on strict
    // ARM targets.  Decode explicitly and keep the script's UTF-16 value
    // type at the ABI boundary.
    const size_t nameUnits = static_cast<size_t>(nameBytes / 2);
    if(nameUnits == 0 || chunk[16 + nameBytes - 2] != 0 ||
       chunk[16 + nameBytes - 1] != 0) {
        error = "TLGref container name is not NUL terminated";
        return false;
    }
    tjs_string containerName;
    containerName.reserve(nameUnits - 1);
    for(size_t index = 0; index + 1 < nameUnits; ++index) {
        const tjs_uint16 code = static_cast<tjs_uint16>(
            static_cast<tjs_uint16>(chunk[16 + index * 2]) |
            (static_cast<tjs_uint16>(chunk[16 + index * 2 + 1]) << 8));
        if(code == 0) {
            error = "TLGref container name contains an early NUL";
            return false;
        }
        containerName.push_back(static_cast<tjs_char>(code));
    }
    ref.Container = ttstr(containerName);
    if(ref.Container.IsEmpty()) {
        error = "empty TLGref container name";
        return false;
    }
    if(ref.Count == 0 || ref.Index >= ref.Count) {
        error = "TLGref image index is outside the container";
        return false;
    }
    return true;
}

static bool TVPReadTLGBytes(tTJSBinaryStream *src,
                            std::vector<tjs_uint8> &bytes,
                            std::string &error) {
    if(!src) {
        error = "null TLG stream";
        return false;
    }
    const tjs_uint64 originalPosition = src->GetPosition();
    const tjs_uint64 size = src->GetSize();
    if(size == 0 || size > kMaxTLGMaterializedBytes ||
       size > std::numeric_limits<size_t>::max()) {
        error = "invalid TLG stream size";
        return false;
    }
    bytes.resize(static_cast<size_t>(size));
    src->SetPosition(0);
    size_t offset = 0;
    while(offset < bytes.size()) {
        const size_t remaining = bytes.size() - offset;
        const tjs_uint want = static_cast<tjs_uint>(std::min<size_t>(
            remaining, std::numeric_limits<tjs_uint>::max()));
        const tjs_uint got = src->Read(bytes.data() + offset, want);
        if(got != want) {
            src->SetPosition(originalPosition);
            error = "short TLG stream read";
            return false;
        }
        offset += got;
    }
    src->SetPosition(originalPosition);
    return true;
}

static bool TVPDecompressTLGLZ4(const std::vector<tjs_uint8> &bytes,
                                tjs_uint64 &position, tjs_uint64 limit,
                                std::vector<tjs_uint8> &output,
                                std::string &error) {
    output.clear();
    std::vector<tjs_uint8> previous;
    while(position < limit) {
        if(!TVPHasTLGBytes(bytes, position, 4)) {
            error = "truncated TLGqoi LZ4 block header";
            return false;
        }
        const tjs_uint32 header = TVPReadTLGUInt32LE(bytes.data() + position);
        position += 4;
        const tjs_uint32 compressedSize = (header >> 16) & 0xffff;
        const bool carryover = (header & 0x8000) != 0;
        const tjs_uint32 decompressedSize = (header & 0x7fff) == 0
                                                ? 32768
                                                : (header & 0x7fff);
        if(compressedSize == 0 || !TVPHasTLGBytes(bytes, position, compressedSize)) {
            error = "truncated TLGqoi LZ4 block";
            return false;
        }
        std::vector<tjs_uint8> block(decompressedSize);
        const char *dictionary = nullptr;
        int dictionarySize = 0;
        if(carryover && !previous.empty()) {
            const size_t keep = std::min<size_t>(previous.size(), 64 * 1024);
            dictionary = reinterpret_cast<const char *>(previous.data() +
                                                         previous.size() - keep);
            dictionarySize = static_cast<int>(keep);
        }
        const int decoded = LZ4_decompress_safe_usingDict(
            reinterpret_cast<const char *>(bytes.data() + position),
            reinterpret_cast<char *>(block.data()),
            static_cast<int>(compressedSize),
            static_cast<int>(decompressedSize), dictionary, dictionarySize);
        position += compressedSize;
        if(decoded < 0 || decoded != static_cast<int>(decompressedSize)) {
            error = "invalid TLGqoi LZ4 block";
            return false;
        }
        if(output.size() > kMaxTLGMaterializedBytes ||
           static_cast<tjs_uint64>(decompressedSize) >
               kMaxTLGMaterializedBytes - output.size()) {
            error = "TLGqoi decompressed data is too large";
            return false;
        }
        output.insert(output.end(), block.begin(), block.end());
        previous.swap(block);
    }
    return true;
}

struct TVPTLGQOIPixel {
    tjs_uint8 R = 0;
    tjs_uint8 G = 0;
    tjs_uint8 B = 0;
    tjs_uint8 A = 255;
};

static bool TVPDecodeTLGQOIContainer(const std::vector<tjs_uint8> &bytes,
                                     tjs_uint32 selectedIndex,
                                     std::vector<tjs_uint8> &rgba,
                                     tjs_uint32 &width, tjs_uint32 &height,
                                     tjs_uint8 &colors, std::string &error) {
    if(!TVPIsTLGQOIHeader(bytes, 0) || !TVPHasTLGBytes(bytes, 0, 28) ||
       std::memcmp(bytes.data() + 20, "QHDR", 4) != 0) {
        error = "invalid TLGqoi+QHDR header";
        return false;
    }
    colors = bytes[11];
    width = TVPReadTLGUInt32LE(bytes.data() + 12);
    height = TVPReadTLGUInt32LE(bytes.data() + 16);
    const tjs_uint32 qhdrSize = TVPReadTLGUInt32LE(bytes.data() + 24);
    const tjs_uint64 canvasPixels = static_cast<tjs_uint64>(width) * height;
    if((colors != 3 && colors != 4) || width == 0 || height == 0 ||
       canvasPixels > kMaxTLGMaterializedBytes / 4 ||
       qhdrSize < 48 || !TVPHasTLGBytes(bytes, 28, qhdrSize)) {
        error = "unsupported TLGqoi+QHDR canvas";
        return false;
    }
    const tjs_uint8 *qhdr = bytes.data() + 28;
    const tjs_uint32 imageCount = TVPReadTLGUInt32LE(qhdr + 4);
    const tjs_uint32 bandHeight = TVPReadTLGUInt32LE(qhdr + 8);
    const tjs_uint32 bandCount = TVPReadTLGUInt32LE(qhdr + 12);
    const tjs_uint64 totalQOIBytes = TVPReadTLGUInt64LE(qhdr + 24);
    if(imageCount == 0 || bandHeight == 0 || bandCount == 0 ||
       selectedIndex >= imageCount ||
       !TVPHasTLGBytes(bytes, 28ULL + qhdrSize + 8, totalQOIBytes)) {
        error = "invalid TLGqoi+QHDR band metadata";
        return false;
    }
    const tjs_uint64 expectedBandCount =
        (static_cast<tjs_uint64>(height) + bandHeight - 1) / bandHeight;
    if(bandCount > expectedBandCount || bandCount > kMaxTLGTableEntries) {
        error = "invalid TLGqoi+QHDR band count";
        return false;
    }

    const tjs_uint64 qoiStart = 28ULL + qhdrSize + 8;
    if(qoiStart > bytes.size() || totalQOIBytes > bytes.size() - qoiStart) {
        error = "TLGqoi+QHDR pixel data is outside the file";
        return false;
    }
    const tjs_uint64 dtblOffset = qoiStart + totalQOIBytes;
    if(!TVPHasTLGBytes(bytes, dtblOffset, 8) ||
       std::memcmp(bytes.data() + dtblOffset, "DTBL", 4) != 0) {
        error = "TLGqoi+QHDR has no DTBL chunk";
        return false;
    }
    const tjs_uint32 dtblSize = TVPReadTLGUInt32LE(bytes.data() + dtblOffset + 4);
    if(!TVPHasTLGBytes(bytes, dtblOffset + 8, dtblSize)) {
        error = "truncated TLGqoi DTBL chunk";
        return false;
    }
    // Parse the count and values to validate the chunk.  The values are only
    // seek hints; decoding uses the exact qoiStart..DTBL range and resets the
    // QOI state at each band as the original engine does.
    tjs_uint64 tablePosition = dtblOffset + 8;
    const tjs_uint64 tableLimit = tablePosition + dtblSize;
    tjs_uint64 tableCount = 0;
    if(!TVPReadTLGLEB128(bytes, tablePosition, tableLimit, tableCount) ||
       tableCount > 2ULL * bandCount + 16) {
        error = "invalid TLGqoi DTBL table";
        return false;
    }
    for(tjs_uint64 i = 0; i < tableCount; ++i) {
        tjs_uint64 ignored = 0;
        if(!TVPReadTLGLEB128(bytes, tablePosition, tableLimit, ignored)) {
            error = "truncated TLGqoi DTBL table";
            return false;
        }
    }

    const tjs_uint64 rtblOffset = dtblOffset + 8ULL + dtblSize;
    if(!TVPHasTLGBytes(bytes, rtblOffset, 8) ||
       std::memcmp(bytes.data() + rtblOffset, "RTBL", 4) != 0) {
        error = "TLGqoi+QHDR has no RTBL chunk";
        return false;
    }
    const tjs_uint32 rtblSize = TVPReadTLGUInt32LE(bytes.data() + rtblOffset + 4);
    if(!TVPHasTLGBytes(bytes, rtblOffset + 8, rtblSize)) {
        error = "truncated TLGqoi RTBL chunk";
        return false;
    }
    tjs_uint64 rtablePosition = rtblOffset + 8;
    const tjs_uint64 rtableLimit = rtablePosition + rtblSize;
    tjs_uint64 rtableCount = 0;
    if(!TVPReadTLGLEB128(bytes, rtablePosition, rtableLimit, rtableCount) ||
       rtableCount < bandCount || rtableCount > kMaxTLGTableEntries) {
        error = "invalid TLGqoi RTBL table";
        return false;
    }
    std::vector<tjs_uint64> bandDistSizes;
    bandDistSizes.reserve(static_cast<size_t>(rtableCount));
    for(tjs_uint64 i = 0; i < rtableCount; ++i) {
        tjs_uint64 value = 0;
        if(!TVPReadTLGLEB128(bytes, rtablePosition, rtableLimit, value)) {
            error = "truncated TLGqoi RTBL table";
            return false;
        }
        bandDistSizes.push_back(value);
    }
    const tjs_uint64 distStart = rtblOffset + 8ULL + rtblSize;
    if(!TVPHasTLGBytes(bytes, distStart, 0)) {
        error = "invalid TLGqoi distribution offset";
        return false;
    }

    const tjs_uint64 pixelCount = static_cast<tjs_uint64>(width) * height;
    if(pixelCount > std::numeric_limits<size_t>::max() / 4)
        error = "TLGqoi image is too large";
    if(!error.empty())
        return false;
    rgba.assign(static_cast<size_t>(pixelCount) * 4, 0);

    tjs_uint64 qoiPosition = qoiStart;
    tjs_uint64 distributionPosition = distStart;
    for(tjs_uint32 band = 0; band < bandCount; ++band) {
        const tjs_uint32 bandTop = band * bandHeight;
        if(bandTop >= height)
            break;
        const tjs_uint32 currentBandHeight =
            std::min(bandHeight, height - bandTop);
        const tjs_uint64 bandPixels =
            static_cast<tjs_uint64>(width) * currentBandHeight;
        if(bandPixels == 0 ||
           imageCount > kMaxTLGMaterializedBytes / 4 / bandPixels) {
            error = "TLGqoi interleaved band is too large";
            return false;
        }
        const tjs_uint64 interleavedPixels = bandPixels * imageCount;
        if(band >= bandDistSizes.size() || distributionPosition > bytes.size() ||
           bandDistSizes[band] > bytes.size() - distributionPosition) {
            error = "TLGqoi distribution band is outside the file";
            return false;
        }
        const tjs_uint64 distributionSize = bandDistSizes[band];
        const tjs_uint64 distributionLimit = distributionPosition + distributionSize;
        std::vector<tjs_uint8> distribution;
        tjs_uint64 compressedPosition = distributionPosition;
        if(!TVPDecompressTLGLZ4(bytes, compressedPosition, distributionLimit,
                                distribution, error))
            return false;
        if(compressedPosition != distributionLimit) {
            error = "TLGqoi distribution band has trailing data";
            return false;
        }
        distributionPosition = distributionLimit;

        std::array<TVPTLGQOIPixel, 64> index{};
        TVPTLGQOIPixel pixel{0, 0, 0, 255};
        auto decodeOne = [&](TVPTLGQOIPixel &decoded,
                             tjs_uint64 &count) -> bool {
            if(qoiPosition >= dtblOffset) {
                error = "truncated TLGqoi pixel stream";
                return false;
            }
            const tjs_uint8 tag = bytes[static_cast<size_t>(qoiPosition++)];
            if(tag == 0xfe) {
                if(!TVPHasTLGBytes(bytes, qoiPosition, 3) ||
                   qoiPosition + 3 > dtblOffset) {
                    error = "truncated TLGqoi RGB opcode";
                    return false;
                }
                pixel.R = bytes[static_cast<size_t>(qoiPosition++)];
                pixel.G = bytes[static_cast<size_t>(qoiPosition++)];
                pixel.B = bytes[static_cast<size_t>(qoiPosition++)];
                count = 1;
            } else if(tag == 0xff) {
                if(!TVPHasTLGBytes(bytes, qoiPosition, 4) ||
                   qoiPosition + 4 > dtblOffset) {
                    error = "truncated TLGqoi RGBA opcode";
                    return false;
                }
                pixel.R = bytes[static_cast<size_t>(qoiPosition++)];
                pixel.G = bytes[static_cast<size_t>(qoiPosition++)];
                pixel.B = bytes[static_cast<size_t>(qoiPosition++)];
                pixel.A = bytes[static_cast<size_t>(qoiPosition++)];
                count = 1;
            } else if((tag & 0xc0) == 0x00) {
                pixel = index[tag & 0x3f];
                count = 1;
            } else if((tag & 0xc0) == 0x40) {
                pixel.R = static_cast<tjs_uint8>(pixel.R +
                                                   ((tag >> 4 & 0x03) - 2));
                pixel.G = static_cast<tjs_uint8>(pixel.G +
                                                   ((tag >> 2 & 0x03) - 2));
                pixel.B = static_cast<tjs_uint8>(pixel.B +
                                                   ((tag & 0x03) - 2));
                count = 1;
            } else if((tag & 0xc0) == 0x80) {
                if(qoiPosition >= dtblOffset) {
                    error = "truncated TLGqoi luma opcode";
                    return false;
                }
                const tjs_uint8 next = bytes[static_cast<size_t>(qoiPosition++)];
                const int dg = static_cast<int>(tag & 0x3f) - 32;
                const int dr = dg + static_cast<int>((next >> 4) & 0x0f) - 8;
                const int db = dg + static_cast<int>(next & 0x0f) - 8;
                pixel.R = static_cast<tjs_uint8>(pixel.R + dr);
                pixel.G = static_cast<tjs_uint8>(pixel.G + dg);
                pixel.B = static_cast<tjs_uint8>(pixel.B + db);
                count = 1;
            } else {
                count = (tag & 0x3f) + 1ULL;
            }
            index[(pixel.R * 3 + pixel.G * 5 + pixel.B * 7 + pixel.A * 11) &
                  63] = pixel;
            decoded = pixel;
            return true;
        };

        TVPTLGQOIPixel ignoredPixel;
        tjs_uint64 ignoredCount = 0;
        if(!decodeOne(ignoredPixel, ignoredCount) ||
           !decodeOne(ignoredPixel, ignoredCount))
            return false;
        tjs_uint64 distributionPositionInBand = 0;
        tjs_uint64 ignoredDistribution = 0;
        if(!TVPReadTLGLEB128(distribution, distributionPositionInBand,
                             distribution.size(), ignoredDistribution)) {
            error = "truncated TLGqoi distribution header";
            return false;
        }

        tjs_uint64 produced = 0;
        while(produced < interleavedPixels) {
            TVPTLGQOIPixel value;
            tjs_uint64 qoiCount = 0;
            if(!decodeOne(value, qoiCount))
                return false;
            tjs_uint64 mask = 0;
            if(!TVPReadTLGLEB128(distribution, distributionPositionInBand,
                                 distribution.size(), mask)) {
                error = "truncated TLGqoi distribution data";
                return false;
            }
            if(mask > std::numeric_limits<tjs_uint64>::max() - qoiCount) {
                error = "TLGqoi run length overflow";
                return false;
            }
            const tjs_uint64 run = std::min(mask + qoiCount,
                                            interleavedPixels - produced);
            for(tjs_uint64 i = 0; i < run; ++i) {
                const tjs_uint64 interleaved = produced + i;
                if(interleaved % imageCount != selectedIndex)
                    continue;
                const tjs_uint64 flat = interleaved / imageCount;
                const tjs_uint32 y = bandTop +
                                     static_cast<tjs_uint32>(flat / width);
                const tjs_uint32 x = static_cast<tjs_uint32>(flat % width);
                tjs_uint8 *destination = rgba.data() +
                    (static_cast<size_t>(y) * width + x) * 4;
                destination[0] = value.R;
                destination[1] = value.G;
                destination[2] = value.B;
                destination[3] = colors == 3 ? 0xff : value.A;
            }
            produced += run;
        }
    }
    return true;
}

static bool TVPOpenTLGRefContainer(const TVPTLGRefInfo &ref,
                                   tTJSBinaryStream *&stream,
                                   std::string &error) {
    stream = nullptr;
    try {
        stream = TVPCreateStream(ref.Container);
        if(!stream) {
            const ttstr current = TVPGetCurrentGraphicLoadName();
            const ttstr path = TVPExtractStoragePath(current);
            if(!path.IsEmpty())
                stream = TVPCreateStream(path + ref.Container);
        }
    } catch(...) {
        stream = nullptr;
    }
    if(!stream) {
        error = "TLGref container is not available: " +
                ref.Container.AsStdString();
        return false;
    }
    return true;
}

static void TVPEmitTLGImage(void *callbackdata,
                            tTVPGraphicSizeCallback sizecallback,
                            tTVPGraphicScanLineCallback scanlinecallback,
                            const std::vector<tjs_uint8> &rgba,
                            tjs_uint32 width, tjs_uint32 height,
                            tjs_uint8 colors) {
    sizecallback(callbackdata, width, height,
                 colors == 3 ? gpfRGB : gpfRGBA);
    for(tjs_uint32 y = 0; y < height; ++y) {
        void *line = scanlinecallback(callbackdata, y);
        if(line)
            std::memcpy(line, rgba.data() + static_cast<size_t>(y) * width * 4,
                        static_cast<size_t>(width) * 4);
        scanlinecallback(callbackdata, -1);
    }
}

static bool TVPDecodeTLGSpecialStream(tTJSBinaryStream *src,
                                      std::vector<tjs_uint8> &rgba,
                                      tjs_uint32 &width, tjs_uint32 &height,
                                      tjs_uint8 &colors, std::string &error) {
    std::vector<tjs_uint8> bytes;
    if(!TVPReadTLGBytes(src, bytes, error))
        return false;
    if(TVPIsTLGQOIHeader(bytes, 0)) {
        if(TVPHasTLGBytes(bytes, 20, 4) &&
           std::memcmp(bytes.data() + 20, "QHDR", 4) == 0)
            return TVPDecodeTLGQOIContainer(bytes, 0, rgba, width, height,
                                            colors, error);
        return TVPDecodeTLGQOI(bytes, 0, bytes.size(), rgba, width, height,
                               colors, error);
    }
    static constexpr char kRefHeader[] = "TLGref\0raw\x1a";
    if(TVPHasTLGBytes(bytes, 0, sizeof(kRefHeader) - 1) &&
       std::memcmp(bytes.data(), kRefHeader, sizeof(kRefHeader) - 1) == 0) {
        TVPTLGRefInfo ref;
        if(!TVPParseTLGRef(bytes, ref, error))
            return false;
        tTJSBinaryStream *container = nullptr;
        if(!TVPOpenTLGRefContainer(ref, container, error))
            return false;
        std::vector<tjs_uint8> containerBytes;
        const bool read = TVPReadTLGBytes(container, containerBytes, error);
        delete container;
        if(!read)
            return false;
        return TVPDecodeTLGQOIContainer(containerBytes, ref.Index, rgba, width,
                                        height, colors, error);
    }
    error = "unsupported special TLG header";
    return false;
}

static bool TVPReadTLGSpecialDimensions(tTJSBinaryStream *src,
                                         tjs_uint32 &width,
                                         tjs_uint32 &height,
                                         tjs_uint8 &colors,
                                         std::string &error) {
    std::vector<tjs_uint8> bytes;
    if(!TVPReadTLGBytes(src, bytes, error))
        return false;
    if(TVPIsTLGQOIHeader(bytes, 0)) {
        if(!TVPHasTLGBytes(bytes, 0, 20)) {
            error = "truncated TLGqoi header";
            return false;
        }
        width = TVPReadTLGUInt32LE(bytes.data() + 12);
        height = TVPReadTLGUInt32LE(bytes.data() + 16);
        colors = bytes[11];
        if((colors != 3 && colors != 4) || width == 0 || height == 0 ||
           static_cast<tjs_uint64>(width) * height >
               kMaxTLGMaterializedBytes / 4) {
            error = "invalid TLGqoi dimensions";
            return false;
        }
        if(TVPHasTLGBytes(bytes, 20, 4) &&
           std::memcmp(bytes.data() + 20, "QHDR", 4) == 0) {
            if(!TVPHasTLGBytes(bytes, 0, 28) ||
               TVPReadTLGUInt32LE(bytes.data() + 24) < 48) {
                error = "invalid TLGqoi+QHDR header";
                return false;
            }
        }
        return true;
    }
    static constexpr char kRefHeader[] = "TLGref\0raw\x1a";
    if(!TVPHasTLGBytes(bytes, 0, sizeof(kRefHeader) - 1) ||
       std::memcmp(bytes.data(), kRefHeader, sizeof(kRefHeader) - 1) != 0) {
        error = "unsupported special TLG header";
        return false;
    }
    TVPTLGRefInfo ref;
    if(!TVPParseTLGRef(bytes, ref, error))
        return false;
    tTJSBinaryStream *container = nullptr;
    if(!TVPOpenTLGRefContainer(ref, container, error))
        return false;
    std::vector<tjs_uint8> containerBytes;
    const bool read = TVPReadTLGBytes(container, containerBytes, error);
    delete container;
    if(!read)
        return false;
    if(!TVPIsTLGQOIHeader(containerBytes, 0) ||
       !TVPHasTLGBytes(containerBytes, 0, 20)) {
        error = "TLGref target is not a TLGqoi container";
        return false;
    }
    colors = containerBytes[11];
    width = TVPReadTLGUInt32LE(containerBytes.data() + 12);
    height = TVPReadTLGUInt32LE(containerBytes.data() + 16);
    if((colors != 3 && colors != 4) || width == 0 || height == 0 ||
       static_cast<tjs_uint64>(width) * height >
           kMaxTLGMaterializedBytes / 4) {
        error = "invalid TLGref target dimensions";
        return false;
    }
    return true;
}

static bool TVPReadTLGMuxStream(tTJSBinaryStream *src,
                                TVPTLGMuxFile &mux, std::string &error) {
    if(!src) {
        error = "null TLG stream";
        return false;
    }
    const tjs_uint64 originalPosition = src->GetPosition();
    const tjs_uint64 size = src->GetSize();
    if(size == 0 || size > kMaxTLGMaterializedBytes ||
       size > std::numeric_limits<size_t>::max()) {
        error = "invalid TLG stream size";
        return false;
    }
    std::vector<tjs_uint8> bytes(static_cast<size_t>(size));
    src->SetPosition(0);
    size_t offset = 0;
    while(offset < bytes.size()) {
        const size_t remaining = bytes.size() - offset;
        const tjs_uint want = static_cast<tjs_uint>(std::min<size_t>(
            remaining, std::numeric_limits<tjs_uint>::max()));
        const tjs_uint got = src->Read(bytes.data() + offset, want);
        if(got != want) {
            src->SetPosition(originalPosition);
            error = "short TLG stream read";
            return false;
        }
        offset += got;
    }
    src->SetPosition(originalPosition);
    return TVPParseTLGMux(bytes, mux, error);
}

static void TVPThrowTLGMuxError(const std::string &error) {
    const ttstr message(error.c_str());
    TVPThrowExceptionMessage(TVPTLGLoadError, message.c_str());
}

static void TVPLoadTLGMux(void *callbackdata,
                          tTVPGraphicSizeCallback sizecallback,
                          tTVPGraphicScanLineCallback scanlinecallback,
                          tTJSBinaryStream *src, tTVPGraphicLoadMode mode) {
    if(mode != glmNormal)
        TVPThrowTLGMuxError("TLGmux only supports full-color loading");

    TVPTLGMuxFile mux;
    std::string error;
    if(!TVPReadTLGMuxStream(src, mux, error))
        TVPThrowTLGMuxError(error);

    const tjs_uint64 canvasPixels = static_cast<tjs_uint64>(mux.Width) *
                                    mux.Height;
    if(canvasPixels > kMaxTLGMaterializedBytes / 4 ||
       canvasPixels > std::numeric_limits<size_t>::max() / 4)
        TVPThrowTLGMuxError("TLGmux canvas is too large");
    std::vector<tjs_uint8> canvas(static_cast<size_t>(canvasPixels) * 4, 0);

    for(size_t index = 0; index < mux.Slices.size(); ++index) {
        const auto &slice = mux.Slices[index];
        tjs_uint64 end = mux.Bytes.size();
        for(const auto &candidate : mux.Slices) {
            if(candidate.Offset > slice.Offset) {
                if(candidate.Offset <=
                   std::numeric_limits<tjs_uint64>::max() - mux.DataBase)
                    end = std::min(end, mux.DataBase + candidate.Offset);
            }
        }
        if(slice.Offset > std::numeric_limits<tjs_uint64>::max() - mux.DataBase ||
           end <= mux.DataBase + slice.Offset || end > mux.Bytes.size())
            TVPThrowTLGMuxError("invalid TLGmux slice bounds");

        std::vector<tjs_uint8> rgba;
        tjs_uint32 decodedWidth = 0;
        tjs_uint32 decodedHeight = 0;
        tjs_uint8 decodedColors = 0;
        if(!TVPDecodeTLGQOI(mux.Bytes, mux.DataBase + slice.Offset, end,
                            rgba, decodedWidth, decodedHeight, decodedColors,
                            error))
            TVPThrowTLGMuxError(error);

        const tjs_uint32 copyWidth = std::min(slice.Width, decodedWidth);
        const tjs_uint32 copyHeight = std::min(slice.Height, decodedHeight);
        for(tjs_uint32 y = 0; y < copyHeight; ++y) {
            const tjs_uint32 dstY = slice.Top + y;
            if(dstY >= mux.Height)
                break;
            const tjs_uint32 dstX = slice.Left;
            if(dstX >= mux.Width)
                continue;
            const tjs_uint32 visibleWidth =
                std::min(copyWidth, mux.Width - dstX);
            tjs_uint8 *dst = canvas.data() +
                             (static_cast<size_t>(dstY) * mux.Width + dstX) *
                                 4;
            const tjs_uint8 *srcPixels =
                rgba.data() + static_cast<size_t>(y) * decodedWidth * 4;
            for(tjs_uint32 x = 0; x < visibleWidth; ++x) {
                // PackinOne's TLGqoi payload is already in the RGBA byte
                // order consumed by the Godot texture bridge.  The legacy
                // TLG5/6 decoder writes TVP's historical BGRA scanlines, but
                // applying that swap here would make the TLGmux-only assets
                // (notably character layers and expressions) blue/yellow.
                dst[x * 4 + 0] = srcPixels[x * 4 + 0];
                dst[x * 4 + 1] = srcPixels[x * 4 + 1];
                dst[x * 4 + 2] = srcPixels[x * 4 + 2];
                dst[x * 4 + 3] = decodedColors == 3 ? 0xff : srcPixels[x * 4 + 3];
            }
        }
    }

    sizecallback(callbackdata, mux.Width, mux.Height,
                 mux.Colors == 3 ? gpfRGB : gpfRGBA);
    for(tjs_uint32 y = 0; y < mux.Height; ++y) {
        void *line = scanlinecallback(callbackdata, y);
        if(line)
            std::memcpy(line, canvas.data() + static_cast<size_t>(y) * mux.Width * 4,
                        static_cast<size_t>(mux.Width) * 4);
        scanlinecallback(callbackdata, -1);
    }
}

static bool TVPReadTLGMuxHeader(tTJSBinaryStream *src, tjs_int &width,
                                tjs_int &height, tjs_int &colors) {
    if(!src)
        return false;
    const tjs_uint64 position = src->GetPosition();
    unsigned char header[20] = {};
    const bool ok = src->Read(header, sizeof(header)) == sizeof(header) &&
                    !std::memcmp(header, "TLGmux\0idx\x1a", 11);
    src->SetPosition(position);
    if(!ok)
        return false;
    colors = header[11];
    width = static_cast<tjs_int>(TVPReadTLGUInt32LE(header + 12));
    height = static_cast<tjs_int>(TVPReadTLGUInt32LE(header + 16));
    return (colors == 3 || colors == 4) && width > 0 && height > 0 &&
           static_cast<tjs_uint64>(width) * height <=
               kMaxTLGMaterializedBytes / 4;
}

class TLGDecodeTrace {
    const char *version_;
    tjs_int width_ = 0;
    tjs_int height_ = 0;
    tjs_int colors_ = 0;
    std::chrono::steady_clock::time_point start_;

public:
    explicit TLGDecodeTrace(const char *version)
        : version_(version), start_(std::chrono::steady_clock::now()) {}

    void SetSize(tjs_int width, tjs_int height, tjs_int colors) {
        width_ = width;
        height_ = height;
        colors_ = colors;
    }

    ~TLGDecodeTrace() {
        if(!TLGDecodeTraceEnabled())
            return;
        const auto elapsed = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - start_)
                                  .count();
        if(elapsed < 5.0)
            return;
        spdlog::info(
            "tlg decode profile: version={} size={}x{} colors={} elapsed_ms={:.3f}",
            version_, width_, height_, colors_, elapsed);
    }
};
} // namespace

static inline void *TLGArenaAlloc(size_t size, int align) {
    // The decode paths add alignment/padding bytes because the pinned krkrz
    // leaves use aligned word reads.  Check the addition before handing an
    // attacker-controlled size to either allocator.
    if(align <= 0 || size > std::numeric_limits<size_t>::max() -
                          static_cast<size_t>(align) - 16)
        return nullptr;
    if(TVPDecodeArenaActive()) {
        void *p = TVPDecodeArenaAlloc(size + align + 16);
        if(p) return p;
    }
    return TJSAlignedAlloc(size, align);
}

static inline void TLGArenaDealloc(void *ptr) {
    if(TVPDecodeArenaActive() && TVPDecodeArenaOwns(ptr)) return;
    TJSAlignedDealloc(ptr);
}

static void TVPThrowMalformedTLGMetadata(const tjs_char *message) {
    TVPThrowExceptionMessage(TVPTLGLoadError, message);
}

// The historical TLG5 decoder is intentionally reused for the actual
// decompression (including the krkrz/SIMD dispatch), but its original API has
// no output bound and assumes a trusted stream.  Validate a slide first so a
// malformed channel cannot make that upstream routine read or write past its
// caller-owned buffers.  The validator mirrors the ring-buffer state machine
// and reports both the expected output length and the next ring position.
static bool TVPValidateTLG5Slide(const tjs_uint8 *input, size_t inputSize,
                                  size_t outputCapacity, tjs_int initialR,
                                  size_t &produced, tjs_int &finalR) {
    if(!input || initialR < 0 || initialR >= 4096)
        return false;
    size_t in = 0;
    produced = 0;
    int r = initialR;
    tjs_uint flags = 0;
    while(in < inputSize) {
        if(((flags >>= 1) & 0x100) == 0) {
            if(in >= inputSize)
                return false;
            flags = input[in++] | 0xff00;
            // Keep this condition byte-for-byte compatible with the legacy
            // fast path (which requires more than eight bytes remaining).
            if(flags == 0xff00 && r < (4096 - 8) &&
               inputSize - in > 8) {
                if(produced > outputCapacity ||
                   outputCapacity - produced < 8)
                    return false;
                produced += 8;
                r += 8;
                in += 8;
                flags = 0;
                continue;
            }
        }
        if(flags & 1) {
            if(inputSize - in < 2)
                return false;
            const tjs_uint16 in16 = static_cast<tjs_uint16>(input[in]) |
                                    (static_cast<tjs_uint16>(input[in + 1]) <<
                                     8);
            in += 2;
            tjs_uint mpos = in16 & 0xfff;
            tjs_uint mlen = (in16 >> 12) + 3;
            if(mlen == 18) {
                if(in >= inputSize)
                    return false;
                mlen += input[in++];
            }
            if(produced > outputCapacity ||
               static_cast<size_t>(mlen) > outputCapacity - produced)
                return false;
            produced += mlen;
            if((mpos + mlen) < 4096 && (r + mlen) < 4096) {
                r += static_cast<int>(mlen);
            } else {
                for(tjs_uint i = 0; i < mlen; ++i) {
                    ++r;
                    r &= 0xfff;
                    ++mpos;
                    mpos &= 0xfff;
                }
            }
        } else {
            if(in >= inputSize || produced >= outputCapacity)
                return false;
            ++in;
            ++produced;
            r = (r + 1) & 0xfff;
        }
    }
    finalR = r;
    return true;
}

// TLG6's historical Golomb decoder receives only a padded byte pointer.  It
// therefore cannot know the encoded bit length and, for malformed input,
// can keep scanning zero bits or index its prediction table out of bounds.
// Parse the same LSB-first grammar with an explicit bit limit before handing
// the stream to the pinned krkrz routine.  This keeps upstream decoding and
// SIMD reuse intact while making the Aether boundary safe for untrusted game
// assets.
class TLGBoundedBitReader {
    const tjs_uint8 *data_ = nullptr;
    size_t byte_length_ = 0;
    size_t bit_length_ = 0;
    size_t position_ = 0;

    tjs_uint8 byteAt(size_t index) const {
        return index < byte_length_ ? data_[index] : 0;
    }

public:
    TLGBoundedBitReader(const tjs_uint8 *data, size_t bitLength)
        : data_(data), byte_length_((bitLength + 7) / 8),
          bit_length_(bitLength) {}

    size_t position() const { return position_; }

    bool readBit(tjs_uint8 &value) {
        if(!data_ || position_ >= bit_length_)
            return false;
        value = static_cast<tjs_uint8>(
            (data_[position_ >> 3] >> (position_ & 7)) & 1U);
        ++position_;
        return true;
    }

    // Read up to 32 LSB-first bits without touching bytes beyond the encoded
    // byte count.  Missing high bits are treated as zero, matching the
    // decoder's deliberately zero-padded scratch buffer.
    bool readBits(unsigned count, tjs_uint32 &value) {
        if(count > 32 || position_ > bit_length_ ||
           bit_length_ - position_ < count)
            return false;
        const size_t byte = position_ >> 3;
        const unsigned shift = static_cast<unsigned>(position_ & 7);
        tjs_uint64 word = static_cast<tjs_uint64>(byteAt(byte));
        word |= static_cast<tjs_uint64>(byteAt(byte + 1)) << 8;
        word |= static_cast<tjs_uint64>(byteAt(byte + 2)) << 16;
        word |= static_cast<tjs_uint64>(byteAt(byte + 3)) << 24;
        word |= static_cast<tjs_uint64>(byteAt(byte + 4)) << 32;
        if(shift)
            word >>= shift;
        if(count == 32)
            value = static_cast<tjs_uint32>(word);
        else if(count == 0)
            value = 0;
        else
            value = static_cast<tjs_uint32>(
                word & ((static_cast<tjs_uint64>(1) << count) - 1U));
        position_ += count;
        return true;
    }

    // Peek the same four-byte window used by TVP_TLG6_FETCH_32BITS.  Bits
    // beyond the declared stream are zero, but no out-of-range byte is read.
    tjs_uint32 peekDecoderWord() const {
        if(position_ > bit_length_)
            return 0;
        const size_t byte = position_ >> 3;
        tjs_uint32 word = static_cast<tjs_uint32>(byteAt(byte));
        word |= static_cast<tjs_uint32>(byteAt(byte + 1)) << 8;
        word |= static_cast<tjs_uint32>(byteAt(byte + 2)) << 16;
        word |= static_cast<tjs_uint32>(byteAt(byte + 3)) << 24;
        return word >> (position_ & 7);
    }

    // Decode the gamma code used for zero/non-zero run lengths.  The
    // upstream routine stores the leading-zero count in an int, so values
    // requiring 31 or more leading zeros are invalid for its ABI even before
    // they could fit in a TLG6 block.
    bool readGamma(tjs_uint64 &value) {
        unsigned leadingZeros = 0;
        tjs_uint8 bit = 0;
        for(;;) {
            if(!readBit(bit))
                return false;
            if(bit)
                break;
            if(++leadingZeros > 30)
                return false;
        }
        tjs_uint32 payload = 0;
        if(!readBits(leadingZeros, payload))
            return false;
        value = (static_cast<tjs_uint64>(1) << leadingZeros) | payload;
        return value != 0;
    }

    // Read one modified Golomb/Rice value.  Normal values use a unary
    // quotient; the saver emits an aligned five-byte escape when the unary
    // prefix would be too long.  The pointer movement intentionally mirrors
    // the upstream decoder's escape path.
    bool readGolombCode(int k, tjs_uint64 &m) {
        if(k < 0 || k > 8 || position_ > bit_length_)
            return false;

        const tjs_uint32 word = peekDecoderWord();
        tjs_uint64 quotient = 0;
        if(word != 0) {
#if defined(__GNUC__) || defined(__clang__)
            const unsigned zeroBits = static_cast<unsigned>(
                __builtin_ctz(static_cast<unsigned>(word)));
#else
            unsigned zeroBits = 0;
            tjs_uint32 probe = word;
            while((probe & 1U) == 0) {
                ++zeroBits;
                probe >>= 1;
            }
#endif
            if(zeroBits >= 32 || bit_length_ - position_ < zeroBits + 1)
                return false;
            position_ += zeroBits + 1; // zero prefix plus its terminator
            quotient = zeroBits;
        } else {
            // TVPTLG6DecodeGolombValues_c advances its byte pointer by five
            // and reads the fifth byte as an eight-bit quotient.  Require the
            // complete escape payload, including the alignment bytes, to be
            // inside the declared bit stream.
            const size_t base = position_ >> 3;
            if(base > (std::numeric_limits<size_t>::max() / 8) - 5)
                return false;
            const size_t escapePosition = (base + 5) * 8;
            if(escapePosition > bit_length_ ||
               base + 4 >= byte_length_ ||
               bit_length_ - escapePosition < static_cast<size_t>(k))
                return false;
            quotient = byteAt(base + 4);
            position_ = escapePosition;
        }

        tjs_uint32 remainder = 0;
        if(!readBits(static_cast<unsigned>(k), remainder))
            return false;
        m = (quotient << static_cast<unsigned>(k)) | remainder;
        return true;
    }
};

static bool TVPValidateTLG6Golomb(const tjs_uint8 *bitPool,
                                  tjs_uint32 bitLength,
                                  tjs_int pixelCount) {
    if(!bitPool || bitLength == 0 || pixelCount <= 0)
        return false;

    TLGBoundedBitReader reader(bitPool, static_cast<size_t>(bitLength));
    tjs_uint8 initial = 0;
    if(!reader.readBit(initial))
        return false;
    bool zeroRun = initial == 0;
    tjs_int remaining = pixelCount;
    int n = 3;
    int a = 0;

    while(remaining > 0) {
        tjs_uint64 count = 0;
        if(!reader.readGamma(count) || count >
                                             static_cast<tjs_uint64>(remaining))
            return false;
        if(zeroRun) {
            remaining -= static_cast<tjs_int>(count);
            zeroRun = false;
            continue;
        }

        for(tjs_uint64 index = 0; index < count; ++index) {
            if(a < 0 || a >= 1024 || n < 0 || n >= 4)
                return false;
            const int k = static_cast<unsigned char>(
                TVPTLG6GolombBitLengthTable[a][n]);
            if(k < 0 || k > 8)
                return false;
            tjs_uint64 m = 0;
            if(!reader.readGolombCode(k, m))
                return false;
            const tjs_uint64 delta = m >> 1;
            if(delta > static_cast<tjs_uint64>(std::numeric_limits<int>::max() -
                                                a))
                return false;
            a += static_cast<int>(delta);
            if(--n < 0) {
                a >>= 1;
                n = 3;
            }
        }
        remaining -= static_cast<tjs_int>(count);
        zeroRun = true;
    }
    return reader.position() <= static_cast<size_t>(bitLength);
}

static bool TVPReadTLGDecimalLength(const std::string &tag, size_t &position,
                                    size_t &length) {
    const size_t start = position;
    length = 0;
    while(position < tag.size() && tag[position] >= '0' &&
          tag[position] <= '9') {
        const size_t digit = static_cast<size_t>(tag[position] - '0');
        if(length > (std::numeric_limits<size_t>::max() - digit) / 10)
            return false;
        length = length * 10 + digit;
        ++position;
    }
    return position != start && position < tag.size() && tag[position] == ':';
}

static void TVPParseTLGMetadataChunk(
    const std::string &tag, void *callbackdata,
    tTVPMetaInfoPushCallback metainfopushcallback) {
    size_t position = 0;
    while(position < tag.size()) {
        size_t nameLength = 0;
        if(!TVPReadTLGDecimalLength(tag, position, nameLength))
            TVPThrowMalformedTLGMetadata(
                (const tjs_char *)TVPTlgMalformedTagMissionColonAfterNameLength);
        ++position; // ':'
        if(nameLength > tag.size() - position)
            TVPThrowMalformedTLGMetadata(TJS_W("TLG metadata name is truncated"));
        const std::string name = tag.substr(position, nameLength);
        position += nameLength;
        if(position >= tag.size() || tag[position] != '=')
            TVPThrowMalformedTLGMetadata(
                (const tjs_char *)TVPTlgMalformedTagMissionEqualsAfterName);
        ++position;

        size_t valueLength = 0;
        if(!TVPReadTLGDecimalLength(tag, position, valueLength))
            TVPThrowMalformedTLGMetadata(
                (const tjs_char *)TVPTlgMalformedTagMissionColonAfterVaueLength);
        ++position; // ':'
        if(valueLength > tag.size() - position)
            TVPThrowMalformedTLGMetadata(TJS_W("TLG metadata value is truncated"));
        const std::string value = tag.substr(position, valueLength);
        position += valueLength;
        if(position >= tag.size() || tag[position] != ',')
            TVPThrowMalformedTLGMetadata(
                (const tjs_char *)TVPTlgMalformedTagMissionCommaAfterTag);
        ++position;

        tjs_string nameUtf16;
        tjs_string valueUtf16;
        if(!TVPUtf8ToUtf16(nameUtf16, name) ||
           !TVPUtf8ToUtf16(valueUtf16, value))
            TVPThrowMalformedTLGMetadata(
                TJS_W("TLG metadata is not valid UTF-8"));
        if(metainfopushcallback)
            metainfopushcallback(callbackdata, ttstr(nameUtf16),
                                 ttstr(valueUtf16));
    }
}

/*
        TLG5:
                Lossless graphics compression method designed for very
   fast decoding speed.

        TLG6:
                Lossless/near-lossless graphics compression method
   which is designed for high compression ratio and faster decoding.
   Decoding speed is somewhat slower than TLG5 because the algorithm
   is much more complex than TLG5. Though, the decoding speed (using
   SSE enabled code) is about 20 times faster than JPEG2000 lossless
   mode (using JasPer library) while the compression ratio can beat or
   compete with it. Summary of compression algorithm is described in
                environ/win32/krdevui/tpc/tlg6/TLG6Saver.cpp
                (in Japanese).
*/

//---------------------------------------------------------------------------
// TLG5 loading handler
//---------------------------------------------------------------------------
void TVPLoadTLG5(void *formatdata, void *callbackdata,
                 tTVPGraphicSizeCallback sizecallback,
                 tTVPGraphicScanLineCallback scanlinecallback,
                 tTJSBinaryStream *src, tjs_int keyidx,
                 tTVPGraphicLoadMode mode) {
    TLGDecodeTrace trace("5");
    // load TLG v5.0 lossless compressed graphic
    if(mode != glmNormal)
        TVPThrowExceptionMessage(
            TVPTLGLoadError,
            (const tjs_char *)TVPTlgUnsupportedUniversalTransitionRule);

    unsigned char mark[12];
    tjs_int width, height, colors, blockheight;
    src->ReadBuffer(mark, 1);
    colors = mark[0];
    width = src->ReadI32LE();
    height = src->ReadI32LE();
    blockheight = src->ReadI32LE();
    trace.SetSize(width, height, colors);

    if(colors != 3 && colors != 4)
        TVPThrowExceptionMessage(TVPTLGLoadError,
                                 (const tjs_char *)TVPUnsupportedColorType);
    if(width <= 0 || height <= 0 || blockheight <= 0 ||
       static_cast<tjs_uint64>(width) * static_cast<tjs_uint64>(height) >
           kMaxTLGMaterializedBytes / 4)
        TVPThrowMalformedTLGMetadata(TJS_W("invalid TLG5 dimensions"));

    const tjs_uint64 blockcount64 =
        (static_cast<tjs_uint64>(height) - 1) /
            static_cast<tjs_uint64>(blockheight) +
        1;
    if(blockcount64 > std::numeric_limits<tjs_int>::max())
        TVPThrowMalformedTLGMetadata(TJS_W("invalid TLG5 block count"));
    const tjs_uint64 streamSize = src->GetSize();
    const tjs_uint64 blockTableBytes = blockcount64 * sizeof(tjs_uint32);
    if(src->GetPosition() > streamSize ||
       blockTableBytes > streamSize - src->GetPosition())
        TVPThrowMalformedTLGMetadata(TJS_W("truncated TLG5 block table"));
    int blockcount = static_cast<int>(blockcount64);

    // skip block size section
    src->SetPosition(src->GetPosition() + blockcount * sizeof(tjs_uint32));

    // decomperss
    sizecallback(callbackdata, width, height, colors == 3 ? gpfRGB : gpfRGBA);

    tjs_uint8 *inbuf = nullptr;
    size_t inbufCapacity = 0;
    tjs_uint8 *outbuf[4];
    tjs_uint8 *text = nullptr;
    tjs_int r = 0;
    for(int i = 0; i < colors; i++)
        outbuf[i] = nullptr;

    try {
        void *textStorage = TLGArenaAlloc(4096 + 32, 4);
        if(!textStorage)
            TVPThrowMalformedTLGMetadata(TJS_W("insufficient TLG5 workspace"));
        text = static_cast<tjs_uint8 *>(textStorage) + 16;
        memset(text, 0, 4096);

        const size_t blockPixelCapacity =
            static_cast<size_t>(std::min(blockheight, height)) *
            static_cast<size_t>(width);
        if(blockPixelCapacity == 0 ||
           blockPixelCapacity > kMaxTLGMaterializedBytes - 32)
            TVPThrowMalformedTLGMetadata(TJS_W("invalid TLG5 block size"));
        inbufCapacity = blockPixelCapacity + 10 + 16;
        inbuf = (tjs_uint8 *)TLGArenaAlloc(inbufCapacity, 4);
        if(!inbuf)
            TVPThrowMalformedTLGMetadata(TJS_W("insufficient TLG5 workspace"));
        for(tjs_int i = 0; i < colors; i++)
            outbuf[i] =
                (tjs_uint8 *)TLGArenaAlloc(blockPixelCapacity + 10 + 16, 4);
        for(tjs_int i = 0; i < colors; i++)
            if(!outbuf[i])
                TVPThrowMalformedTLGMetadata(
                    TJS_W("insufficient TLG5 workspace"));

        tjs_uint8 *prevline = nullptr;
        for(tjs_int y_blk = 0; y_blk < height; y_blk += blockheight) {
            const tjs_int currentBlockHeight =
                std::min(blockheight, height - y_blk);
            const tjs_uint64 expectedChannelBytes =
                static_cast<tjs_uint64>(currentBlockHeight) *
                static_cast<tjs_uint64>(width);
            // read file and decompress
            for(tjs_int c = 0; c < colors; c++) {
                if(src->GetPosition() > streamSize ||
                   streamSize - src->GetPosition() < 5)
                    TVPThrowMalformedTLGMetadata(
                        TJS_W("truncated TLG5 channel header"));
                src->ReadBuffer(mark, 1);
                const tjs_uint32 encodedSize = src->ReadI32LE();
                if(encodedSize == 0 ||
                   static_cast<tjs_uint64>(encodedSize) >
                       kMaxTLGMaterializedBytes ||
                   encodedSize > streamSize - src->GetPosition())
                    TVPThrowMalformedTLGMetadata(
                        TJS_W("invalid TLG5 channel size"));
                if(mark[0] != 0 &&
                   static_cast<tjs_uint64>(encodedSize) != expectedChannelBytes)
                    TVPThrowMalformedTLGMetadata(
                        TJS_W("invalid TLG5 raw channel size"));
                if(mark[0] == 0 && encodedSize > inbufCapacity) {
                    TLGArenaDealloc(inbuf);
                    inbufCapacity = static_cast<size_t>(encodedSize) + 16;
                    inbuf = (tjs_uint8 *)TLGArenaAlloc(inbufCapacity, 4);
                    if(!inbuf)
                        TVPThrowMalformedTLGMetadata(
                            TJS_W("insufficient TLG5 workspace"));
                }
                const tjs_int size = static_cast<tjs_int>(encodedSize);
                if(mark[0] == 0) {
                    // modified LZSS compressed data
                    src->ReadBuffer(inbuf, size);
                    size_t produced = 0;
                    tjs_int validatedR = 0;
                    if(!TVPValidateTLG5Slide(
                           inbuf, static_cast<size_t>(size),
                           blockPixelCapacity, r, produced, validatedR) ||
                       produced != expectedChannelBytes)
                        TVPThrowMalformedTLGMetadata(
                            TJS_W("invalid TLG5 compressed channel"));
                    r = TVPTLG5DecompressSlide(outbuf[c], inbuf, size, text, r);
                    if(r != validatedR)
                        TVPThrowMalformedTLGMetadata(
                            TJS_W("invalid TLG5 compressed channel state"));
                } else {
                    // raw data
                    src->ReadBuffer(outbuf[c], size);
                }
            }

            // compose colors and store
            tjs_int y_lim = y_blk + blockheight;
            if(y_lim > height)
                y_lim = height;
            tjs_uint8 *outbufp[4];
            for(tjs_int c = 0; c < colors; c++)
                outbufp[c] = outbuf[c];
            for(tjs_int y = y_blk; y < y_lim; y++) {
                tjs_uint8 *current =
                    (tjs_uint8 *)scanlinecallback(callbackdata, y);
                tjs_uint8 *current_org = current;
                if(prevline) {
                    // not first line
                    switch(colors) {
                        case 3:
                            TVPTLG5ComposeColors3To4(current, prevline, outbufp,
                                                     width);
                            outbufp[0] += width;
                            outbufp[1] += width;
                            outbufp[2] += width;
                            break;
                        case 4:
                            TVPTLG5ComposeColors4To4(current, prevline, outbufp,
                                                     width);
                            outbufp[0] += width;
                            outbufp[1] += width;
                            outbufp[2] += width;
                            outbufp[3] += width;
                            break;
                    }
                } else {
                    // first line
                    switch(colors) {
                        case 3:
                            for(tjs_int pr = 0, pg = 0, pb = 0, x = 0;
                                x < width; x++) {
                                tjs_int r = outbufp[0][x];
                                tjs_int g = outbufp[1][x];
                                tjs_int b = outbufp[2][x];
                                b += g;
                                r += g;
                                0 [current++] = pb += b;
                                0 [current++] = pg += g;
                                0 [current++] = pr += r;
                                0 [current++] = 0xff;
                            }
                            outbufp[0] += width;
                            outbufp[1] += width;
                            outbufp[2] += width;
                            break;
                        case 4:
                            for(tjs_int pr = 0, pg = 0, pb = 0, pa = 0, x = 0;
                                x < width; x++) {
                                tjs_int r = outbufp[0][x];
                                tjs_int g = outbufp[1][x];
                                tjs_int b = outbufp[2][x];
                                tjs_int a = outbufp[3][x];
                                b += g;
                                r += g;
                                0 [current++] = pb += b;
                                0 [current++] = pg += g;
                                0 [current++] = pr += r;
                                0 [current++] = pa += a;
                            }
                            outbufp[0] += width;
                            outbufp[1] += width;
                            outbufp[2] += width;
                            outbufp[3] += width;
                            break;
                    }
                }
                scanlinecallback(callbackdata, -1);

                prevline = current_org;
            }
        }
    } catch(...) {
        if(inbuf)
            TLGArenaDealloc(inbuf);
        if(text)
            TLGArenaDealloc(text - 16);
        for(tjs_int i = 0; i < colors; i++)
            if(outbuf[i])
                TLGArenaDealloc(outbuf[i]);
        throw;
    }
    if(inbuf)
        TLGArenaDealloc(inbuf);
    if(text)
        TLGArenaDealloc(text - 16);
    for(tjs_int i = 0; i < colors; i++)
        if(outbuf[i])
            TLGArenaDealloc(outbuf[i]);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TLG6 loading handler
//---------------------------------------------------------------------------
void TVPLoadTLG6(void *formatdata, void *callbackdata,
                 tTVPGraphicSizeCallback sizecallback,
                 tTVPGraphicScanLineCallback scanlinecallback,
                 tTJSBinaryStream *src, tjs_int keyidx, bool palettized) {
    TLGDecodeTrace trace("6");
    // load TLG v6.0 lossless/near-lossless compressed graphic
#if 0
	if(palettized)
		TVPThrowExceptionMessage(TVPTLGLoadError, (const tjs_char*)TVPTlgUnsupportedUniversalTransitionRule );
#endif
    unsigned char buf[12];

    src->ReadBuffer(buf, 4);

    tjs_int colors = buf[0]; // color component count

    if(colors != 1 && colors != 4 && colors != 3)
        TVPThrowExceptionMessage(TVPTLGLoadError,
                                 ttstr(TVPUnsupportedColorCount) +
                                     ttstr((int)colors));

    if(buf[1] != 0) // data flag
        TVPThrowExceptionMessage(TVPTLGLoadError,
                                 (const tjs_char *)TVPDataFlagMustBeZero);

    if(buf[2] != 0) // color type  (currently always zero)
        TVPThrowExceptionMessage(TVPTLGLoadError,
                                 ttstr(TVPUnsupportedColorTypeColon) +
                                     ttstr((int)buf[1]));

    if(buf[3] != 0) // external golomb table (currently always zero)
        TVPThrowExceptionMessage(
            TVPTLGLoadError,
            (const tjs_char *)TVPUnsupportedExternalGolombBitLengthTable);

    tjs_int width, height;

    width = src->ReadI32LE();
    height = src->ReadI32LE();
    trace.SetSize(width, height, colors);

    if(width <= 0 || height <= 0 ||
       static_cast<tjs_uint64>(width) * static_cast<tjs_uint64>(height) >
           kMaxTLGMaterializedBytes / 4)
        TVPThrowMalformedTLGMetadata(TJS_W("invalid TLG6 dimensions"));

    tjs_int max_bit_length;

    max_bit_length = src->ReadI32LE();
    if(max_bit_length <= 0 ||
       static_cast<tjs_uint64>(max_bit_length) >
           kMaxTLGMaterializedBytes * 8)
        TVPThrowMalformedTLGMetadata(TJS_W("invalid TLG6 bit length"));
    const tjs_uint64 streamSize = src->GetSize();
    if(src->GetPosition() > streamSize)
        TVPThrowMalformedTLGMetadata(TJS_W("invalid TLG6 stream position"));

    // set destination size
    sizecallback(callbackdata, width, height, colors == 3 ? gpfRGB : gpfRGBA);

    // compute some values
    tjs_int x_block_count = (tjs_int)((width - 1) / TVP_TLG6_W_BLOCK_SIZE) + 1;
    tjs_int y_block_count = (tjs_int)((height - 1) / TVP_TLG6_H_BLOCK_SIZE) + 1;
    tjs_int main_count = width / TVP_TLG6_W_BLOCK_SIZE;
    tjs_int fraction = width - main_count * TVP_TLG6_W_BLOCK_SIZE;

    const tjs_uint64 bitPoolCapacity64 =
        static_cast<tjs_uint64>(max_bit_length) / 8 + 5;
    const tjs_uint64 pixelBufferBytes64 =
        static_cast<tjs_uint64>(width) * TVP_TLG6_H_BLOCK_SIZE *
            sizeof(tjs_uint32) +
        1;
    const tjs_uint64 filterCount64 =
        static_cast<tjs_uint64>(x_block_count) * y_block_count;
    const tjs_uint64 zeroLineBytes64 =
        static_cast<tjs_uint64>(width) * sizeof(tjs_uint32);
    const tjs_uint64 linePairBytes64 = zeroLineBytes64 * 2;
    if(bitPoolCapacity64 > kMaxTLGMaterializedBytes ||
       pixelBufferBytes64 > kMaxTLGMaterializedBytes ||
       filterCount64 > kMaxTLGMaterializedBytes ||
       zeroLineBytes64 > kMaxTLGMaterializedBytes ||
       (palettized && linePairBytes64 > kMaxTLGMaterializedBytes) ||
       bitPoolCapacity64 > std::numeric_limits<size_t>::max() ||
       pixelBufferBytes64 > std::numeric_limits<size_t>::max() ||
       filterCount64 > std::numeric_limits<size_t>::max() - 16 ||
       zeroLineBytes64 > std::numeric_limits<size_t>::max())
        TVPThrowMalformedTLGMetadata(TJS_W("TLG6 workspace is too large"));
    const size_t bitPoolCapacity = static_cast<size_t>(bitPoolCapacity64);
    const size_t pixelBufferBytes = static_cast<size_t>(pixelBufferBytes64);
    const size_t filterCount = static_cast<size_t>(filterCount64);
    const size_t zeroLineBytes = static_cast<size_t>(zeroLineBytes64);

    // prepare memory pointers
    tjs_uint8 *bit_pool = nullptr;
    tjs_uint32 *pixelbuf = nullptr; // pixel buffer
    tjs_uint8 *filter_types = nullptr;
    tjs_uint8 *LZSS_text = nullptr;
    tjs_uint32 *zeroline = nullptr;

    tjs_uint32 *tmpline[2] = { nullptr, nullptr };
    tjs_uint8 *grayline;
    try {
        // allocate memories
        bit_pool = (tjs_uint8 *)TLGArenaAlloc(bitPoolCapacity, 4);
        pixelbuf = (tjs_uint32 *)TLGArenaAlloc(pixelBufferBytes, 4);
        filter_types = (tjs_uint8 *)TLGArenaAlloc(filterCount + 16, 4);
        zeroline = (tjs_uint32 *)TLGArenaAlloc(zeroLineBytes, 4);
        void *lzssStorage = TLGArenaAlloc(4096 + 32, 4);
        if(lzssStorage)
            LZSS_text = static_cast<tjs_uint8 *>(lzssStorage) + 16;
        if(!bit_pool || !pixelbuf || !filter_types || !zeroline ||
           !LZSS_text)
            TVPThrowMalformedTLGMetadata(TJS_W("insufficient TLG6 workspace"));

        // initialize zero line (virtual y=-1 line)
        TVPFillARGB(zeroline, width, colors == 3 ? 0xff000000 : 0x00000000);
        // 0xff000000 for colors=3 makes alpha value opaque

        // initialize LZSS text (used by chroma filter type codes)
        {
            tjs_uint32 *p = (tjs_uint32 *)LZSS_text;
            for(tjs_uint32 i = 0; i < 32 * 0x01010101; i += 0x01010101) {
                for(tjs_uint32 j = 0; j < 16 * 0x01010101; j += 0x01010101)
                    p[0] = i, p[1] = j, p += 2;
            }
        }

        // read chroma filter types.
        // chroma filter types are compressed via LZSS as used by
        // TLG5.
        {
            tjs_int inbuf_size = src->ReadI32LE();
            const tjs_uint64 inputPosition = src->GetPosition();
            if(inbuf_size <= 0 || inputPosition > streamSize ||
               static_cast<tjs_uint64>(inbuf_size) > kMaxTLGMaterializedBytes ||
               static_cast<tjs_uint64>(inbuf_size) >
                   streamSize - inputPosition)
                TVPThrowMalformedTLGMetadata(
                    TJS_W("invalid TLG6 filter stream size"));
            const size_t inbufCapacity = static_cast<size_t>(inbuf_size) + 16;
            tjs_uint8 *inbuf = (tjs_uint8 *)TLGArenaAlloc(inbufCapacity, 4);
            if(!inbuf)
                TVPThrowMalformedTLGMetadata(
                    TJS_W("insufficient TLG6 workspace"));
            try {
                src->ReadBuffer(inbuf, inbuf_size);
                size_t produced = 0;
                tjs_int validatedR = 0;
                if(!TVPValidateTLG5Slide(inbuf, static_cast<size_t>(inbuf_size),
                                         filterCount, 0, produced, validatedR) ||
                   produced != filterCount)
                    TVPThrowMalformedTLGMetadata(
                        TJS_W("invalid TLG6 filter stream"));
                const tjs_int result = TVPTLG5DecompressSlide(
                    filter_types, inbuf, inbuf_size, LZSS_text, 0);
                if(result != validatedR)
                    TVPThrowMalformedTLGMetadata(
                        TJS_W("invalid TLG6 filter stream state"));
                for(size_t i = 0; i < filterCount; ++i)
                    if(filter_types[i] > 31)
                        TVPThrowMalformedTLGMetadata(
                            TJS_W("invalid TLG6 filter type"));
            } catch(...) {
                TLGArenaDealloc(inbuf);
                throw;
            }
            TLGArenaDealloc(inbuf);
        }

        // for each horizontal block group ...
        tjs_uint32 *prevline = zeroline;
        for(tjs_int y = 0; y < height; y += TVP_TLG6_H_BLOCK_SIZE) {
            tjs_int ylim = y + TVP_TLG6_H_BLOCK_SIZE;
            if(ylim >= height)
                ylim = height;

            tjs_int pixel_count = (ylim - y) * width;

            // decode values
            for(tjs_int c = 0; c < colors; c++) {
                // read bit length
                if(src->GetPosition() > streamSize ||
                   streamSize - src->GetPosition() < 4)
                    TVPThrowMalformedTLGMetadata(
                        TJS_W("truncated TLG6 entropy header"));
                const tjs_uint32 encodedBitLength = static_cast<tjs_uint32>(
                    src->ReadI32LE());

                // get compress method
                const int method = static_cast<int>(encodedBitLength >> 30);
                const tjs_uint32 bit_length = encodedBitLength & 0x3fffffffU;
                if(static_cast<tjs_uint64>(bit_length) >
                   static_cast<tjs_uint64>(max_bit_length))
                    TVPThrowMalformedTLGMetadata(
                        TJS_W("TLG6 entropy stream exceeds maximum bit length"));

                // compute byte length
                const tjs_uint64 byte_length64 =
                    (static_cast<tjs_uint64>(bit_length) + 7) / 8;
                if(byte_length64 > bitPoolCapacity ||
                   src->GetPosition() > streamSize ||
                   byte_length64 > streamSize - src->GetPosition())
                    TVPThrowMalformedTLGMetadata(
                        TJS_W("truncated TLG6 entropy stream"));
                const tjs_uint byte_length =
                    static_cast<tjs_uint>(byte_length64);

                // read source from input
                std::memset(bit_pool, 0, bitPoolCapacity);
                src->ReadBuffer(bit_pool, byte_length);

                // decode values
                // two most significant bits of bitlength are
                // entropy coding method;
                // 00 means Golomb method,
                // 01 means Gamma method (not yet suppoted),
                // 10 means modified LZSS method (not yet supported),
                // 11 means raw (uncompressed) data (not yet
                // supported).

                switch(method) {
                    case 0:
                        if(!TVPValidateTLG6Golomb(bit_pool, bit_length,
                                                 pixel_count))
                            TVPThrowMalformedTLGMetadata(
                                TJS_W("invalid TLG6 Golomb stream"));
                        if(c == 0 && colors != 1)
                            TVPTLG6DecodeGolombValuesForFirst(
                                (tjs_int8 *)pixelbuf, pixel_count, bit_pool);
                        else
                            TVPTLG6DecodeGolombValues((tjs_int8 *)pixelbuf + c,
                                                      pixel_count, bit_pool);
                        break;
                    default:
                        TVPThrowExceptionMessage(
                            TVPTLGLoadError,
                            (const tjs_char *)
                                TVPUnsupportedEntropyCodingMethod);
                }
            }

            // for each line
            unsigned char *ft =
                filter_types + (y / TVP_TLG6_H_BLOCK_SIZE) * x_block_count;
            int skipbytes = (ylim - y) * TVP_TLG6_W_BLOCK_SIZE;

            for(int yy = y; yy < ylim; yy++) {
                tjs_uint32 *curline;
                if(!palettized)
                    curline = (tjs_uint32 *)scanlinecallback(callbackdata, yy);
                else {
                    if(!tmpline[0]) {
                        tmpline[0] = (tjs_uint32 *)TLGArenaAlloc(
                            sizeof(tjs_uint32) * width, 4);
                        tmpline[1] = (tjs_uint32 *)TLGArenaAlloc(
                            sizeof(tjs_uint32) * width, 4);
                        if(!tmpline[0] || !tmpline[1])
                            TVPThrowMalformedTLGMetadata(
                                TJS_W("insufficient TLG6 workspace"));
                    }
                    curline = tmpline[yy & 1];
                    grayline = (tjs_uint8 *)scanlinecallback(callbackdata, yy);
                }

                int dir = (yy & 1) ^ 1;
                int oddskip = ((ylim - yy - 1) - (yy - y));
                if(main_count) {
                    int start = ((width < TVP_TLG6_W_BLOCK_SIZE)
                                     ? width
                                     : TVP_TLG6_W_BLOCK_SIZE) *
                        (yy - y);
                    TVPTLG6DecodeLine(prevline, curline, width, main_count, ft,
                                      skipbytes, pixelbuf + start,
                                      colors == 3 ? 0xff000000 : 0, oddskip,
                                      dir);
                }

                if(main_count != x_block_count) {
                    int ww = fraction;
                    if(ww > TVP_TLG6_W_BLOCK_SIZE)
                        ww = TVP_TLG6_W_BLOCK_SIZE;
                    int start = ww * (yy - y);
                    TVPTLG6DecodeLineGeneric(
                        prevline, curline, width, main_count, x_block_count, ft,
                        skipbytes, pixelbuf + start,
                        colors == 3 ? 0xff000000 : 0, oddskip, dir);
                }

                prevline = curline;
                if(palettized) {
                    for(int x = 0; x < width; ++x) {
                        grayline[x] = curline[x] & 0xFF; // red -> lumi
                    }
                }
                scanlinecallback(callbackdata, -1);
            }
        }
    } catch(...) {
        if(bit_pool)
            TLGArenaDealloc(bit_pool);
        if(pixelbuf)
            TLGArenaDealloc(pixelbuf);
        if(filter_types)
            TLGArenaDealloc(filter_types);
        if(zeroline)
            TLGArenaDealloc(zeroline);
        if(LZSS_text)
            TLGArenaDealloc(LZSS_text - 16);
        if(tmpline[0]) {
            TLGArenaDealloc(tmpline[0]);
            TLGArenaDealloc(tmpline[1]);
        }
        throw;
    }
    if(bit_pool)
        TLGArenaDealloc(bit_pool);
    if(pixelbuf)
        TLGArenaDealloc(pixelbuf);
    if(filter_types)
        TLGArenaDealloc(filter_types);
    if(zeroline)
        TLGArenaDealloc(zeroline);
    if(LZSS_text)
        TLGArenaDealloc(LZSS_text - 16);
    if(tmpline[0]) {
        TLGArenaDealloc(tmpline[0]);
        TLGArenaDealloc(tmpline[1]);
    }
}

//---------------------------------------------------------------------------
// TLG loading handler
//---------------------------------------------------------------------------
static void TVPInternalLoadTLG(void *formatdata, void *callbackdata,
                               tTVPGraphicSizeCallback sizecallback,
                               tTVPGraphicScanLineCallback scanlinecallback,
                               tTVPMetaInfoPushCallback metainfopushcallback,
                               tTJSBinaryStream *src, tjs_int keyidx,
                               tTVPGraphicLoadMode mode) {
    // read header
    unsigned char mark[12];
    src->ReadBuffer(mark, 11);

    // check for TLG raw data
    if(!memcmp("TLG5.0\x00raw\x1a\x00", mark, 11)) {
        TVPLoadTLG5(formatdata, callbackdata, sizecallback, scanlinecallback,
                    src, keyidx, mode);
    } else if(!memcmp("TLG6.0\x00raw\x1a\x00", mark, 11)) {
        TVPLoadTLG6(formatdata, callbackdata, sizecallback, scanlinecallback,
                    src, keyidx, mode != glmNormal);
    } else {
        TVPThrowExceptionMessage(
            TVPTLGLoadError, (const tjs_char *)TVPInvalidTlgHeaderOrVersion);
    }
}
//---------------------------------------------------------------------------

void TVPLoadTLG(void *formatdata, void *callbackdata,
                tTVPGraphicSizeCallback sizecallback,
                tTVPGraphicScanLineCallback scanlinecallback,
                tTVPMetaInfoPushCallback metainfopushcallback,
                tTJSBinaryStream *src, tjs_int keyidx,
                tTVPGraphicLoadMode mode) {
    TVPTraceTLGFormatStream("load", src);
    if(src) {
        unsigned char muxMagic[6] = {};
        const tjs_uint64 position = src->GetPosition();
        if(src->Read(muxMagic, sizeof(muxMagic)) == sizeof(muxMagic) &&
           !memcmp(muxMagic, "TLGmux", sizeof(muxMagic)))
            TVPDumpTLGMuxStream(src);
        src->SetPosition(position);
    }
    // read header
    unsigned char mark[12];
    src->ReadBuffer(mark, 11);

    if(!memcmp("TLGmux\0idx\x1a", mark, 11)) {
        src->Seek(0, TJS_BS_SEEK_SET);
        TVPLoadTLGMux(callbackdata, sizecallback, scanlinecallback, src, mode);
        return;
    }

    // TLGqoi and TLGref are used by modern event/CG assets.  They are not
    // TLG5/6 variants, so handle them before the legacy raw decoder sees the
    // header and reports a misleading "invalid TLG" error.
    if(!memcmp("TLGqoi\0raw\x1a", mark, 11) ||
       !memcmp("TLGref\0raw\x1a", mark, 11)) {
        if(mode != glmNormal)
            TVPThrowTLGMuxError("TLGqoi/TLGref only supports full-color loading");
        src->Seek(0, TJS_BS_SEEK_SET);
        std::vector<tjs_uint8> rgba;
        tjs_uint32 width = 0;
        tjs_uint32 height = 0;
        tjs_uint8 colors = 0;
        std::string error;
        if(!TVPDecodeTLGSpecialStream(src, rgba, width, height, colors, error)) {
            spdlog::warn("TLG special decode failed name={} error={}",
                         TVPGetCurrentGraphicLoadName().AsStdString(), error);
            TVPThrowTLGMuxError(error);
        }
        TVPEmitTLGImage(callbackdata, sizecallback, scanlinecallback, rgba,
                        width, height, colors);
        return;
    }

    // check for TLG0.0 sds
    if(!memcmp("TLG0.0\x00sds\x1a\x00", mark, 11)) {
        // read TLG0.0 Structured Data Stream

        // TLG0.0 SDS tagged data is simple "NAME=VALUE," string;
        // Each NAME and VALUE have length:content expression.
        // eg: 4:LEFT=2:20,3:TOP=3:120,4:TYPE=1:3,
        // The last ',' cannot be ommited.
        // Each string (name and value) must be encoded in utf-8.

        // read raw data size.  The size is a byte count after the four-byte
        // length field; validate it before handing the stream to the raw
        // decoder so malformed SDS files cannot make the metadata seek wrap.
        const tjs_uint rawlen = src->ReadI32LE();
        const tjs_uint64 streamSize = src->GetSize();
        const tjs_uint64 rawStart = src->GetPosition();
        if(rawlen == 0 || rawlen > kMaxTLGMaterializedBytes ||
           rawStart > streamSize ||
           static_cast<tjs_uint64>(rawlen) > streamSize - rawStart)
            TVPThrowMalformedTLGMetadata(TJS_W("TLG SDS raw payload is truncated"));
        const tjs_uint64 metadataStart = rawStart + rawlen;

        // try to load TLG raw data
        TVPInternalLoadTLG(formatdata, callbackdata, sizecallback,
                           scanlinecallback, metainfopushcallback, src, keyidx,
                           mode);
        if(src->GetPosition() > metadataStart)
            TVPThrowMalformedTLGMetadata(TJS_W("TLG SDS raw payload exceeds its declared size"));

        // Seek to the metadata point even when a decoder consumed fewer bytes
        // than declared (some old encoders pad the raw payload).
        src->SetPosition(metadataStart);

        // Read chunked metadata.  Every chunk has an eight-byte header; a
        // partial trailing header is malformed rather than silently ignored.
        while(src->GetPosition() < streamSize) {
            const tjs_uint64 chunkHeader = src->GetPosition();
            if(streamSize - chunkHeader < 8)
                TVPThrowMalformedTLGMetadata(TJS_W("TLG SDS metadata header is truncated"));
            char chunkname[4];
            src->ReadBuffer(chunkname, 4);
            const tjs_uint chunksize = src->ReadI32LE();
            const tjs_uint64 chunkStart = src->GetPosition();
            if(chunksize > kMaxTLGMetadataChunkBytes ||
               chunkStart > streamSize ||
               static_cast<tjs_uint64>(chunksize) > streamSize - chunkStart)
                TVPThrowMalformedTLGMetadata(TJS_W("TLG SDS metadata chunk is truncated"));
            if(!memcmp(chunkname, "tags", 4)) {
                std::string tag(static_cast<size_t>(chunksize), '\0');
                if(chunksize)
                    src->ReadBuffer(tag.data(), chunksize);
                TVPParseTLGMetadataChunk(tag, callbackdata,
                                         metainfopushcallback);
            }
            src->SetPosition(chunkStart + chunksize);
        } // while

    } else {
        src->Seek(0, TJS_BS_SEEK_SET); // rewind

        // try to load TLG raw data
        TVPInternalLoadTLG(formatdata, callbackdata, sizecallback,
                           scanlinecallback, metainfopushcallback, src, keyidx,
                           mode);
    }
}
//---------------------------------------------------------------------------
void TVPLoadHeaderTLG(void *formatdata, tTJSBinaryStream *src,
                      iTJSDispatch2 **dic) {
    if(dic == nullptr)
        return;

    TVPTraceTLGFormatStream("header", src);
    if(src) {
        unsigned char muxMagic[6] = {};
        const tjs_uint64 position = src->GetPosition();
        if(src->Read(muxMagic, sizeof(muxMagic)) == sizeof(muxMagic) &&
           !memcmp(muxMagic, "TLGmux", sizeof(muxMagic)))
            TVPDumpTLGMuxStream(src);
        src->SetPosition(position);
    }

    // read header
    unsigned char mark[12];
    src->ReadBuffer(mark, 11);

    tjs_int width = 0;
    tjs_int height = 0;
    tjs_int colors = 0;
    bool isTLG6 = false;
    if(!memcmp("TLGmux\0idx\x1a", mark, 11)) {
        src->Seek(0, TJS_BS_SEEK_SET);
        if(!TVPReadTLGMuxHeader(src, width, height, colors))
            TVPThrowExceptionMessage(
                TVPTLGLoadError,
                (const tjs_char *)TVPInvalidTlgHeaderOrVersion);
        goto header_done;
    }
    if(!memcmp("TLGqoi\0raw\x1a", mark, 11) ||
       !memcmp("TLGref\0raw\x1a", mark, 11)) {
        tjs_uint32 specialWidth = 0;
        tjs_uint32 specialHeight = 0;
        tjs_uint8 specialColors = 0;
        std::string error;
        src->Seek(0, TJS_BS_SEEK_SET);
        if(!TVPReadTLGSpecialDimensions(src, specialWidth, specialHeight,
                                        specialColors, error))
            TVPThrowExceptionMessage(
                TVPTLGLoadError, ttstr(error.c_str()).c_str());
        width = static_cast<tjs_int>(specialWidth);
        height = static_cast<tjs_int>(specialHeight);
        colors = static_cast<tjs_int>(specialColors);
        goto header_done;
    }
    // check for TLG0.0 sds
    if(!memcmp("TLG0.0\x00sds\x1a\x00", mark, 11)) {
        // read raw data size
        const tjs_uint rawlen = src->ReadI32LE();
        const tjs_uint64 rawStart = src->GetPosition();
        const tjs_uint64 streamSize = src->GetSize();
        if(rawlen == 0 || rawlen > kMaxTLGMaterializedBytes ||
           rawStart > streamSize ||
           static_cast<tjs_uint64>(rawlen) > streamSize - rawStart)
            TVPThrowMalformedTLGMetadata(TJS_W("TLG SDS raw payload is truncated"));
        src->ReadBuffer(mark, 11);
        if(!memcmp("TLG5.0\x00raw\x1a\x00", mark, 11)) {
            src->ReadBuffer(mark, 1);
            colors = mark[0];
            width = src->ReadI32LE();
            height = src->ReadI32LE();
        } else if(!memcmp("TLG6.0\x00raw\x1a\x00", mark, 11)) {
            isTLG6 = true;
            src->ReadBuffer(mark, 4);
            colors = mark[0]; // color component count
            width = src->ReadI32LE();
            height = src->ReadI32LE();
        } else {
            TVPThrowExceptionMessage(
                TVPTLGLoadError,
                (const tjs_char *)TVPInvalidTlgHeaderOrVersion);
        }
    } else if(!memcmp("TLG5.0\x00raw\x1a\x00", mark, 11)) {
        src->ReadBuffer(mark, 1);
        colors = mark[0];
        width = src->ReadI32LE();
        height = src->ReadI32LE();
    } else if(!memcmp("TLG6.0\x00raw\x1a\x00", mark, 11)) {
        isTLG6 = true;
        src->ReadBuffer(mark, 4);
        colors = mark[0]; // color component count
        width = src->ReadI32LE();
        height = src->ReadI32LE();
    } else {
        TVPThrowExceptionMessage(
            TVPTLGLoadError, (const tjs_char *)TVPInvalidTlgHeaderOrVersion);
    }
header_done:
    if(width <= 0 || height <= 0 ||
       static_cast<tjs_uint64>(width) * static_cast<tjs_uint64>(height) >
           kMaxTLGMaterializedBytes / 4 ||
       (isTLG6 ? (colors != 1 && colors != 3 && colors != 4)
               : (colors != 3 && colors != 4)))
        TVPThrowMalformedTLGMetadata(TJS_W("invalid TLG dimensions"));
    tjs_int bpp = colors * 8;
    *dic = TJSCreateDictionaryObject();
    tTJSVariant val(width);
    (*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("width"), nullptr, &val, (*dic));
    val = tTJSVariant(height);
    (*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("height"), nullptr, &val, (*dic));
    val = tTJSVariant(bpp);
    (*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("bpp"), nullptr, &val, (*dic));
}
//---------------------------------------------------------------------------
