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

namespace {
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
       mux.Height == 0) {
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
            if(expected > chunkSize) {
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
    if(pixelCount > std::numeric_limits<size_t>::max() / 4) {
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
    // in the byte count.  TVPStringFromBMPUnicode handles surrogate-free BMP
    // names and keeps the storage API's native UTF-16 representation.
    ref.Container = TVPStringFromBMPUnicode(
        reinterpret_cast<const tjs_uint16 *>(chunk + 16),
        static_cast<tjs_int>(nameBytes / 2));
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
    if(size == 0 || size > std::numeric_limits<size_t>::max()) {
        error = "invalid TLG stream size";
        return false;
    }
    bytes.resize(static_cast<size_t>(size));
    src->SetPosition(0);
    const tjs_uint read = src->Read(bytes.data(), static_cast<tjs_uint>(size));
    src->SetPosition(originalPosition);
    if(read != size) {
        error = "short TLG stream read";
        return false;
    }
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
    if((colors != 3 && colors != 4) || width == 0 || height == 0 ||
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

    const tjs_uint64 qoiStart = 28ULL + qhdrSize + 8;
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
       rtableCount < bandCount) {
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
        const tjs_uint64 interleavedPixels =
            static_cast<tjs_uint64>(width) * imageCount * currentBandHeight;
        if(band >= bandDistSizes.size() ||
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
        if((colors != 3 && colors != 4) || width == 0 || height == 0) {
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
    if((colors != 3 && colors != 4) || width == 0 || height == 0) {
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
    if(size == 0 || size > std::numeric_limits<size_t>::max()) {
        error = "invalid TLG stream size";
        return false;
    }
    std::vector<tjs_uint8> bytes(static_cast<size_t>(size));
    src->SetPosition(0);
    const tjs_uint read = src->Read(bytes.data(), static_cast<tjs_uint>(size));
    src->SetPosition(originalPosition);
    if(read != size) {
        error = "short TLG stream read";
        return false;
    }
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
    if(canvasPixels > std::numeric_limits<size_t>::max() / 4)
        TVPThrowTLGMuxError("TLGmux canvas is too large");
    std::vector<tjs_uint8> canvas(static_cast<size_t>(canvasPixels) * 4, 0);

    for(size_t index = 0; index < mux.Slices.size(); ++index) {
        const auto &slice = mux.Slices[index];
        tjs_uint64 end = mux.Bytes.size();
        for(const auto &candidate : mux.Slices) {
            if(candidate.Offset > slice.Offset) {
                end = std::min(end, mux.DataBase + candidate.Offset);
            }
        }
        if(end <= mux.DataBase + slice.Offset || end > mux.Bytes.size())
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
    return colors == 3 || colors == 4;
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

    int blockcount = (int)((height - 1) / blockheight) + 1;

    // skip block size section
    src->SetPosition(src->GetPosition() + blockcount * sizeof(tjs_uint32));

    // decomperss
    sizecallback(callbackdata, width, height, colors == 3 ? gpfRGB : gpfRGBA);

    tjs_uint8 *inbuf = nullptr;
    tjs_uint8 *outbuf[4];
    tjs_uint8 *text = nullptr;
    tjs_int r = 0;
    for(int i = 0; i < colors; i++)
        outbuf[i] = nullptr;

    try {
        text = (tjs_uint8 *)TLGArenaAlloc(4096 + 32, 4) + 16;
        memset(text, 0, 4096);

        inbuf = (tjs_uint8 *)TLGArenaAlloc(blockheight * width + 10 + 16, 4);
        for(tjs_int i = 0; i < colors; i++)
            outbuf[i] =
                (tjs_uint8 *)TLGArenaAlloc(blockheight * width + 10 + 16, 4);

        tjs_uint8 *prevline = nullptr;
        for(tjs_int y_blk = 0; y_blk < height; y_blk += blockheight) {
            // read file and decompress
            for(tjs_int c = 0; c < colors; c++) {
                src->ReadBuffer(mark, 1);
                tjs_int size;
                size = src->ReadI32LE();
                if(mark[0] == 0) {
                    // modified LZSS compressed data
                    src->ReadBuffer(inbuf, size);
                    r = TVPTLG5DecompressSlide(outbuf[c], inbuf, size, text, r);
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

    tjs_int max_bit_length;

    max_bit_length = src->ReadI32LE();

    // set destination size
    sizecallback(callbackdata, width, height, colors == 3 ? gpfRGB : gpfRGBA);

    // compute some values
    tjs_int x_block_count = (tjs_int)((width - 1) / TVP_TLG6_W_BLOCK_SIZE) + 1;
    tjs_int y_block_count = (tjs_int)((height - 1) / TVP_TLG6_H_BLOCK_SIZE) + 1;
    tjs_int main_count = width / TVP_TLG6_W_BLOCK_SIZE;
    tjs_int fraction = width - main_count * TVP_TLG6_W_BLOCK_SIZE;

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
        bit_pool = (tjs_uint8 *)TLGArenaAlloc(max_bit_length / 8 + 5, 4);
        pixelbuf = (tjs_uint32 *)TLGArenaAlloc(
            sizeof(tjs_uint32) * width * TVP_TLG6_H_BLOCK_SIZE + 1, 4);
        filter_types =
            (tjs_uint8 *)TLGArenaAlloc(x_block_count * y_block_count + 16, 4);
        zeroline = (tjs_uint32 *)TLGArenaAlloc(width * sizeof(tjs_uint32), 4);
        LZSS_text = (tjs_uint8 *)TLGArenaAlloc(4096 + 32, 4) + 16;

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
            tjs_uint8 *inbuf = (tjs_uint8 *)TLGArenaAlloc(inbuf_size + 16, 4);
            try {
                src->ReadBuffer(inbuf, inbuf_size);
                TVPTLG5DecompressSlide(filter_types, inbuf, inbuf_size,
                                       LZSS_text, 0);
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
                tjs_int bit_length = src->ReadI32LE();

                // get compress method
                int method = (bit_length >> 30) & 3;
                bit_length &= 0x3fffffff;

                // compute byte length
                tjs_int byte_length = bit_length / 8;
                if(bit_length % 8)
                    byte_length++;

                // read source from input
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

        // read raw data size
        tjs_uint rawlen = src->ReadI32LE();

        // try to load TLG raw data
        TVPInternalLoadTLG(formatdata, callbackdata, sizecallback,
                           scanlinecallback, metainfopushcallback, src, keyidx,
                           mode);

        // seek to meta info data point
        src->Seek(rawlen + 11 + 4, TJS_BS_SEEK_SET);

        // read tag data
        while(true) {
            char chunkname[4];
            if(4 != src->Read(chunkname, 4))
                break;
            // cannot read more
            tjs_uint chunksize = src->ReadI32LE();
            if(!memcmp(chunkname, "tags", 4)) {
                // tag information
                char *tag = nullptr;
                char *name = nullptr;
                char *value = nullptr;
                try {
                    tag = new char[chunksize + 1];
                    src->ReadBuffer(tag, chunksize);
                    tag[chunksize] = 0;
                    if(metainfopushcallback) {
                        const char *tagp = tag;
                        const char *tagp_lim = tag + chunksize;
                        while(tagp < tagp_lim) {
                            tjs_uint namelen = 0;
                            while(*tagp >= '0' && *tagp <= '9')
                                namelen = namelen * 10 + *tagp - '0', tagp++;
                            if(*tagp != ':')
                                TVPThrowExceptionMessage(
                                    TVPTLGLoadError,
                                    (const tjs_char *)
                                        TVPTlgMalformedTagMissionColonAfterNameLength);
                            tagp++;
                            name = new char[namelen + 1];
                            memcpy(name, tagp, namelen);
                            name[namelen] = '\0';
                            tagp += namelen;
                            if(*tagp != '=')
                                TVPThrowExceptionMessage(
                                    TVPTLGLoadError,
                                    (const tjs_char *)
                                        TVPTlgMalformedTagMissionEqualsAfterName);
                            tagp++;
                            tjs_uint valuelen = 0;
                            while(*tagp >= '0' && *tagp <= '9')
                                valuelen = valuelen * 10 + *tagp - '0', tagp++;
                            if(*tagp != ':')
                                TVPThrowExceptionMessage(
                                    TVPTLGLoadError,
                                    (const tjs_char *)
                                        TVPTlgMalformedTagMissionColonAfterVaueLength);
                            tagp++;
                            value = new char[valuelen + 1];
                            memcpy(value, tagp, valuelen);
                            value[valuelen] = '\0';
                            tagp += valuelen;
                            if(*tagp != ',')
                                TVPThrowExceptionMessage(
                                    TVPTLGLoadError,
                                    (const tjs_char *)
                                        TVPTlgMalformedTagMissionCommaAfterTag);
                            tagp++;

                            // insert into name-value pairs ... TODO:
                            // utf-8 decode
                            metainfopushcallback(callbackdata, ttstr(name),
                                                 ttstr(value));

                            delete[] name, name = nullptr;
                            delete[] value, value = nullptr;
                        }
                    }
                } catch(...) {
                    if(tag)
                        delete[] tag;
                    if(name)
                        delete[] name;
                    if(value)
                        delete[] value;
                    throw;
                }

                if(tag)
                    delete[] tag;
                if(name)
                    delete[] name;
                if(value)
                    delete[] value;
            } else {
                // skip the chunk
                src->SetPosition(src->GetPosition() + chunksize);
            }
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
        tjs_uint rawlen = src->ReadI32LE();
        src->ReadBuffer(mark, 11);
        if(!memcmp("TLG5.0\x00raw\x1a\x00", mark, 11)) {
            src->ReadBuffer(mark, 1);
            colors = mark[0];
            width = src->ReadI32LE();
            height = src->ReadI32LE();
        } else if(!memcmp("TLG6.0\x00raw\x1a\x00", mark, 11)) {
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
        src->ReadBuffer(mark, 4);
        colors = mark[0]; // color component count
        width = src->ReadI32LE();
        height = src->ReadI32LE();
    } else {
        TVPThrowExceptionMessage(
            TVPTLGLoadError, (const tjs_char *)TVPInvalidTlgHeaderOrVersion);
    }
header_done:
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
