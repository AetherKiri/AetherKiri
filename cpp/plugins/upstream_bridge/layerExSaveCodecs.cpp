#include "layerExSaveCodecs.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <new>
#include <vector>

#include <LodePNG/lodepng.h>
#include <tlg5/slide.h>

namespace aether::krkrz::layer_save {
namespace {

const std::uint8_t *rowAt(const std::uint8_t *pixels, int height, int pitch,
                          int y) {
    (void)height;
    // Match layerExSave's BufRefT contract exactly: the pointer denotes the
    // first logical row and pitch is the signed byte offset to the next row.
    // This also handles bottom-up buffers without inventing a second origin
    // convention for the adapter.
    const auto offset = static_cast<std::int64_t>(y) *
                        static_cast<std::int64_t>(pitch);
    return pixels + static_cast<std::ptrdiff_t>(offset);
}

bool validImage(const std::uint8_t *pixels, int width, int height, int pitch) {
    if(!pixels || width <= 0 || height <= 0 || pitch == 0)
        return false;
    const auto rowBytes = static_cast<std::int64_t>(width) * 4;
    const auto pitch64 = static_cast<std::int64_t>(pitch);
    const auto absolutePitch = pitch64 < 0 ? -pitch64 : pitch64;
    if(rowBytes > absolutePitch)
        return false;

    // Keep every pointer offset representable before handing the buffer to
    // an upstream codec.  This does not replace the caller's ownership/size
    // contract; it only prevents signed pointer arithmetic from overflowing
    // for hostile dimensions or strides.  For a negative pitch the first
    // logical row is still at `pixels`, so the valid range extends below it.
    const auto rowSpan = static_cast<std::int64_t>(height - 1) *
                         absolutePitch;
    const auto minOffset = pitch64 < 0 ? -rowSpan : 0;
    const auto maxOffset = pitch64 < 0 ? rowBytes : rowSpan + rowBytes;
    return minOffset >=
               static_cast<std::int64_t>(std::numeric_limits<std::ptrdiff_t>::min()) &&
           maxOffset <=
               static_cast<std::int64_t>(std::numeric_limits<std::ptrdiff_t>::max());
}

void appendU32LE(std::vector<std::uint8_t> &out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffu));
}

void putU32LE(std::vector<std::uint8_t> &out, std::size_t offset,
              std::uint32_t value) {
    if(offset + 4 > out.size())
        return;
    out[offset + 0] = static_cast<std::uint8_t>(value & 0xffu);
    out[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xffu);
    out[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xffu);
    out[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xffu);
}

} // namespace

bool encodePng(const std::uint8_t *bgra, int width, int height, int pitch,
               std::vector<std::uint8_t> &output) {
    output.clear();
    if(!validImage(bgra, width, height, pitch))
        return false;

    const auto widthSize = static_cast<std::size_t>(width);
    const auto heightSize = static_cast<std::size_t>(height);
    if(heightSize != 0 &&
       widthSize > std::numeric_limits<std::size_t>::max() / heightSize)
        return false;
    const auto pixelCount = widthSize * heightSize;
    if(pixelCount > std::numeric_limits<std::size_t>::max() / 4)
        return false;

    try {
        // Aether stores pixels as BGRA; LodePNG's RGBA input is the format
        // used by the upstream layerExSave implementation.
        std::vector<unsigned char> rgba(pixelCount * 4);
        for(int y = 0; y < height; ++y) {
            const auto *src = rowAt(bgra, height, pitch, y);
            auto *dst = rgba.data() + static_cast<std::size_t>(y) * widthSize * 4;
            for(int x = 0; x < width; ++x) {
                dst[0] = src[2];
                dst[1] = src[1];
                dst[2] = src[0];
                dst[3] = src[3];
                src += 4;
                dst += 4;
            }
        }

        // `lexsave` is the namespace used by the pinned krkrz_dev copy to
        // avoid colliding with other LodePNG users in the host process.
        const unsigned error = lexsave::lodepng::encode(
            output, rgba, static_cast<unsigned>(width),
            static_cast<unsigned>(height), lexsave::LCT_RGBA, 8);
        if(error != 0) {
            output.clear();
            return false;
        }
    } catch(const std::exception &) {
        output.clear();
        return false;
    }
    return !output.empty();
}

bool encodeTlg5(const std::uint8_t *bgra, int width, int height, int pitch,
                std::vector<std::uint8_t> &output) {
    output.clear();
    if(!validImage(bgra, width, height, pitch))
        return false;

    constexpr int colors = 4;
    constexpr int blockHeight = 4;
    const int blockCount = (height - 1) / blockHeight + 1;
    const auto widthSize = static_cast<std::size_t>(width);
    if(widthSize > std::numeric_limits<std::size_t>::max() / blockHeight)
        return false;
    const std::size_t pixelsPerBlock = widthSize * blockHeight;
    if(pixelsPerBlock > std::numeric_limits<std::uint32_t>::max() ||
       pixelsPerBlock > static_cast<std::size_t>(std::numeric_limits<long>::max()))
        return false;
    if(pixelsPerBlock >
       (std::numeric_limits<std::size_t>::max() - 64) / 9)
        return false;
    const std::size_t maxCompressed = pixelsPerBlock * 9 / 4 + 64;

    try {
        // TLG5 raw header: magic, channel count, width, height, block height.
        static constexpr char magic[] = "TLG5.0\x00raw\x1a\x00";
        output.insert(output.end(), magic, magic + 11);
        output.push_back(colors);
        appendU32LE(output, static_cast<std::uint32_t>(width));
        appendU32LE(output, static_cast<std::uint32_t>(height));
        appendU32LE(output, blockHeight);

        const std::size_t blockTable = output.size();
        const auto blockCountSize = static_cast<std::size_t>(blockCount);
        if(blockCountSize >
           (std::numeric_limits<std::size_t>::max() - output.size()) / 4) {
            output.clear();
            return false;
        }
        output.resize(output.size() + blockCountSize * 4, 0);

        lexsave::SlideCompressor compressor;
        std::vector<std::uint8_t> compressed(maxCompressed);

        for(int block = 0; block < blockCount; ++block) {
            const int firstY = block * blockHeight;
            const int lastY = std::min(height, firstY + blockHeight);
            const int rows = lastY - firstY;
            const std::size_t inputLength = widthSize *
                                             static_cast<std::size_t>(rows);
            if(inputLength > std::numeric_limits<std::uint32_t>::max() ||
               inputLength >
                   static_cast<std::size_t>(std::numeric_limits<long>::max())) {
                output.clear();
                return false;
            }
            std::vector<std::uint8_t> channels[colors];
            for(auto &channel : channels)
                channel.resize(inputLength);

            std::size_t inputOffset = 0;
            for(int y = firstY; y < lastY; ++y) {
                const auto *current = rowAt(bgra, height, pitch, y);
                const auto *upper = y == 0 ? nullptr
                                           : rowAt(bgra, height, pitch, y - 1);
                int previous[colors] = {0, 0, 0, 0};
                for(int x = 0; x < width; ++x) {
                    int value[colors];
                    for(int c = 0; c < colors; ++c) {
                        const int currentValue = current[c];
                        const int upperValue = upper ? upper[c] : 0;
                        const int delta = upper ? currentValue - upperValue
                                                : currentValue;
                        value[c] = delta - previous[c];
                        previous[c] = delta;
                    }
                    // This is the channel decorrelation used by upstream
                    // `savetlg5.cpp`; the narrowing conversion is intentional.
                    channels[0][inputOffset] =
                        static_cast<std::uint8_t>(value[0] - value[1]);
                    channels[1][inputOffset] =
                        static_cast<std::uint8_t>(value[1]);
                    channels[2][inputOffset] =
                        static_cast<std::uint8_t>(value[2] - value[1]);
                    channels[3][inputOffset] =
                        static_cast<std::uint8_t>(value[3]);
                    ++inputOffset;
                    current += 4;
                    if(upper)
                        upper += 4;
                }
            }

            const std::size_t blockStart = output.size();
            for(int c = 0; c < colors; ++c) {
                long compressedLength = 0;
                compressor.Store();
                compressor.Encode(
                    channels[c].data(), static_cast<long>(inputLength),
                    compressed.data(), compressedLength);
                const bool compressedFits =
                    compressedLength > 0 &&
                    static_cast<std::size_t>(compressedLength) < inputLength &&
                    static_cast<std::size_t>(compressedLength) <=
                        compressed.size();
                if(compressedFits) {
                    output.push_back(0);
                    appendU32LE(output,
                                static_cast<std::uint32_t>(compressedLength));
                    output.insert(output.end(), compressed.begin(),
                                  compressed.begin() + compressedLength);
                } else {
                    compressor.Restore();
                    output.push_back(1);
                    appendU32LE(output, static_cast<std::uint32_t>(inputLength));
                    output.insert(output.end(), channels[c].begin(),
                                  channels[c].end());
                }
            }
            const auto blockSize = output.size() - blockStart;
            if(blockSize > std::numeric_limits<std::uint32_t>::max()) {
                output.clear();
                return false;
            }
            putU32LE(output, blockTable + static_cast<std::size_t>(block) * 4,
                     static_cast<std::uint32_t>(blockSize));
        }
    } catch(const std::exception &) {
        output.clear();
        return false;
    }
    return !output.empty();
}

} // namespace aether::krkrz::layer_save
