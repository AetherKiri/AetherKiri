#include <catch2/catch_test_macros.hpp>

#include "tjsCommHead.h"
#include "GraphicsLoaderIntf.h"
#include "UtilStreams.h"
#include "tvpgl.h"
#include "upstream_bridge/layerExSaveCodecs.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {

struct DecodeContext {
    tjs_uint width = 0;
    tjs_uint height = 0;
    std::vector<tjs_uint8> pixels;
    std::vector<std::pair<ttstr, ttstr>> metadata;
};

int sizeCallback(void *opaque, tjs_uint width, tjs_uint height,
                 tTVPGraphicPixelFormat) {
    auto *context = static_cast<DecodeContext *>(opaque);
    context->width = width;
    context->height = height;
    context->pixels.assign(static_cast<size_t>(width) * height * 4, 0);
    return static_cast<int>(width * 4);
}

void *scanlineCallback(void *opaque, tjs_int y) {
    auto *context = static_cast<DecodeContext *>(opaque);
    if(y < 0 || static_cast<tjs_uint>(y) >= context->height)
        return nullptr;
    return context->pixels.data() + static_cast<size_t>(y) * context->width * 4;
}

void metadataCallback(void *opaque, const ttstr &name, const ttstr &value) {
    auto *context = static_cast<DecodeContext *>(opaque);
    context->metadata.emplace_back(name, value);
}

void appendU32(std::vector<tjs_uint8> &bytes, std::uint32_t value) {
    bytes.push_back(static_cast<tjs_uint8>(value & 0xff));
    bytes.push_back(static_cast<tjs_uint8>((value >> 8) & 0xff));
    bytes.push_back(static_cast<tjs_uint8>((value >> 16) & 0xff));
    bytes.push_back(static_cast<tjs_uint8>((value >> 24) & 0xff));
}

std::vector<tjs_uint8> makeRawTLG5() {
    static constexpr char header[] = "TLG5.0\0raw\x1a\0";
    std::vector<tjs_uint8> raw(header, header + 11);
    raw.push_back(3); // RGB
    appendU32(raw, 1); // width
    appendU32(raw, 1); // height
    appendU32(raw, 1); // block height
    appendU32(raw, 0); // one ignored block-size entry
    static constexpr tjs_uint8 channels[] = {10, 20, 30};
    for(const tjs_uint8 channel : channels) {
        raw.push_back(1); // uncompressed channel
        appendU32(raw, 1);
        raw.push_back(channel);
    }
    return raw;
}

std::vector<tjs_uint8> makeTLG5WithOversizedRawChannel() {
    std::vector<tjs_uint8> raw = makeRawTLG5();
    // The first channel header starts after the 11-byte magic, component
    // count, dimensions, block height, and one block-table entry.
    raw[29] = 2; // encoded size (little-endian)
    return raw;
}

std::vector<tjs_uint8> makeTruncatedTLG6FilterStream() {
    static constexpr char header[] = "TLG6.0\0raw\x1a\0";
    std::vector<tjs_uint8> raw(header, header + 11);
    raw.insert(raw.end(), {3, 0, 0, 0}); // RGB, standard flags
    appendU32(raw, 1);                   // width
    appendU32(raw, 1);                   // height
    appendU32(raw, 8);                   // maximum entropy bit length
    appendU32(raw, 0x100);               // filter stream claims 256 bytes
    raw.push_back(0);                    // deliberately truncated payload
    return raw;
}

std::vector<tjs_uint8> makeTLG6SinglePixel(bool malformedEntropy) {
    static constexpr char header[] = "TLG6.0\0raw\x1a\0";
    std::vector<tjs_uint8> raw(header, header + 11);
    raw.insert(raw.end(), {1, 0, 0, 0}); // grayscale, standard flags
    appendU32(raw, 1);                   // width
    appendU32(raw, 1);                   // height
    appendU32(raw, 2);                   // maximum entropy bit length

    // One filter entry (type 0), encoded as two LZSS literal bytes: a flags
    // byte followed by the literal itself.
    appendU32(raw, 2);
    raw.insert(raw.end(), {0, 0});

    // The entropy stream starts with the zero/non-zero state bit and then a
    // gamma-coded run length of one.  0b10 (LSB first) is a valid one-pixel
    // zero run; 0b00 is intentionally unterminated for the rejection test.
    appendU32(raw, 2);
    raw.push_back(malformedEntropy ? 0x00 : 0x02);
    return raw;
}

std::vector<tjs_uint8> makeTLG6SingleNonzeroPixel() {
    static constexpr char header[] = "TLG6.0\0raw\x1a\0";
    std::vector<tjs_uint8> raw(header, header + 11);
    raw.insert(raw.end(), {1, 0, 0, 0});
    appendU32(raw, 1);
    appendU32(raw, 1);
    appendU32(raw, 4);
    appendU32(raw, 2);
    raw.insert(raw.end(), {0, 0});
    appendU32(raw, 4);
    // LSB-first: initial non-zero state (1), gamma(1) (1), then m=1
    // with k=0 (unary 0 + terminator 1).
    raw.push_back(0x0b);
    return raw;
}

std::vector<tjs_uint8> makeTLG6SingleEscapedPixel() {
    static constexpr char header[] = "TLG6.0\0raw\x1a\0";
    std::vector<tjs_uint8> raw(header, header + 11);
    raw.insert(raw.end(), {1, 0, 0, 0});
    appendU32(raw, 1);
    appendU32(raw, 1);
    appendU32(raw, 40);
    appendU32(raw, 2);
    raw.insert(raw.end(), {0, 0});
    appendU32(raw, 40);
    // Initial state and gamma(1) occupy the first two bits.  Thirty zero
    // quotient bits reach the decoder's five-byte escape, whose fifth byte
    // carries quotient 100 (k=0 for the first prediction value).
    raw.insert(raw.end(), {0x03, 0x00, 0x00, 0x00, 100});
    return raw;
}

std::vector<tjs_uint8> makeSDS(const std::string &tag) {
    static constexpr char header[] = "TLG0.0\0sds\x1a\0";
    const std::vector<tjs_uint8> raw = makeRawTLG5();
    std::vector<tjs_uint8> bytes(header, header + 11);
    appendU32(bytes, static_cast<std::uint32_t>(raw.size()));
    bytes.insert(bytes.end(), raw.begin(), raw.end());
    bytes.insert(bytes.end(), {'t', 'a', 'g', 's'});
    appendU32(bytes, static_cast<std::uint32_t>(tag.size()));
    bytes.insert(bytes.end(), tag.begin(), tag.end());
    return bytes;
}

void initVisualKernels() {
    static bool initialized = false;
    if(!initialized) {
        TVPInitTVPGL();
        initialized = true;
    }
}

} // namespace

TEST_CASE("TLG SDS metadata decodes bounded UTF-8 names and values") {
    initVisualKernels();
    const std::string tag =
        "6:\xE5\x90\x8D\xE5\xAD\x97=6:\xE4\xB8\x96\xE7\x95\x8C,";
    const std::vector<tjs_uint8> bytes = makeSDS(tag);
    tTVPMemoryStream stream;
    stream.SetSize(static_cast<tjs_uint>(bytes.size()));
    stream.Write(bytes.data(), static_cast<tjs_uint>(bytes.size()));
    stream.SetPosition(0);

    DecodeContext context;
    TVPLoadTLG(nullptr, &context, &sizeCallback, &scanlineCallback,
               &metadataCallback, &stream, 0, glmNormal);

    REQUIRE(context.width == 1);
    REQUIRE(context.height == 1);
    REQUIRE(context.metadata.size() == 1);
    CHECK(context.metadata[0].first == TJS_W("名字"));
    CHECK(context.metadata[0].second == TJS_W("世界"));
    REQUIRE(context.pixels.size() == 4);
    CHECK(context.pixels[3] == 0xff);
}

TEST_CASE("TLG SDS rejects truncated metadata instead of reading past a chunk") {
    initVisualKernels();
    const std::vector<tjs_uint8> bytes = makeSDS("1:a=1:b");
    tTVPMemoryStream stream;
    stream.SetSize(static_cast<tjs_uint>(bytes.size()));
    stream.Write(bytes.data(), static_cast<tjs_uint>(bytes.size()));
    stream.SetPosition(0);
    DecodeContext context;
    REQUIRE_THROWS(TVPLoadTLG(nullptr, &context, &sizeCallback,
                               &scanlineCallback, &metadataCallback, &stream,
                               0, glmNormal));
}

TEST_CASE("TLG5 rejects a raw channel larger than the current block") {
    initVisualKernels();
    const std::vector<tjs_uint8> bytes = makeTLG5WithOversizedRawChannel();
    tTVPMemoryStream stream;
    stream.SetSize(static_cast<tjs_uint>(bytes.size()));
    stream.Write(bytes.data(), static_cast<tjs_uint>(bytes.size()));
    stream.SetPosition(0);
    DecodeContext context;
    REQUIRE_THROWS(TVPLoadTLG(nullptr, &context, &sizeCallback,
                               &scanlineCallback, &metadataCallback, &stream,
                               0, glmNormal));
}

TEST_CASE("TLG6 rejects a truncated chroma-filter stream") {
    initVisualKernels();
    const std::vector<tjs_uint8> bytes = makeTruncatedTLG6FilterStream();
    tTVPMemoryStream stream;
    stream.SetSize(static_cast<tjs_uint>(bytes.size()));
    stream.Write(bytes.data(), static_cast<tjs_uint>(bytes.size()));
    stream.SetPosition(0);
    DecodeContext context;
    REQUIRE_THROWS(TVPLoadTLG(nullptr, &context, &sizeCallback,
                               &scanlineCallback, &metadataCallback, &stream,
                               0, glmNormal));
}

TEST_CASE("TLG6 Golomb preflight accepts a valid grayscale stream") {
    initVisualKernels();
    const std::vector<tjs_uint8> bytes = makeTLG6SinglePixel(false);
    tTVPMemoryStream stream;
    stream.SetSize(static_cast<tjs_uint>(bytes.size()));
    stream.Write(bytes.data(), static_cast<tjs_uint>(bytes.size()));
    stream.SetPosition(0);
    DecodeContext context;
    TVPLoadTLG(nullptr, &context, &sizeCallback, &scanlineCallback,
               &metadataCallback, &stream, 0, glmNormal);
    REQUIRE(context.width == 1);
    REQUIRE(context.height == 1);
    REQUIRE(context.pixels.size() == 4);
    CHECK(context.pixels[0] == 0);
    CHECK(context.pixels[1] == 0);
    CHECK(context.pixels[2] == 0);
    CHECK(context.pixels[3] == 0);
}

TEST_CASE("TLG6 Golomb preflight rejects an unterminated run") {
    initVisualKernels();
    const std::vector<tjs_uint8> bytes = makeTLG6SinglePixel(true);
    tTVPMemoryStream stream;
    stream.SetSize(static_cast<tjs_uint>(bytes.size()));
    stream.Write(bytes.data(), static_cast<tjs_uint>(bytes.size()));
    stream.SetPosition(0);
    DecodeContext context;
    REQUIRE_THROWS(TVPLoadTLG(nullptr, &context, &sizeCallback,
                               &scanlineCallback, &metadataCallback, &stream,
                               0, glmNormal));
}

TEST_CASE("TLG6 Golomb preflight preserves a non-zero value") {
    initVisualKernels();
    const std::vector<tjs_uint8> bytes = makeTLG6SingleNonzeroPixel();
    tTVPMemoryStream stream;
    stream.SetSize(static_cast<tjs_uint>(bytes.size()));
    stream.Write(bytes.data(), static_cast<tjs_uint>(bytes.size()));
    stream.SetPosition(0);
    DecodeContext context;
    TVPLoadTLG(nullptr, &context, &sizeCallback, &scanlineCallback,
               &metadataCallback, &stream, 0, glmNormal);
    REQUIRE(context.width == 1);
    REQUIRE(context.height == 1);
    REQUIRE(context.pixels.size() == 4);
    CHECK(context.pixels[0] == 0);
    CHECK(context.pixels[1] == 0);
    CHECK(context.pixels[2] == 1);
    CHECK(context.pixels[3] == 0);
}

TEST_CASE("TLG6 Golomb preflight accepts the extended quotient escape") {
    initVisualKernels();
    const std::vector<tjs_uint8> bytes = makeTLG6SingleEscapedPixel();
    tTVPMemoryStream stream;
    stream.SetSize(static_cast<tjs_uint>(bytes.size()));
    stream.Write(bytes.data(), static_cast<tjs_uint>(bytes.size()));
    stream.SetPosition(0);
    DecodeContext context;
    TVPLoadTLG(nullptr, &context, &sizeCallback, &scanlineCallback,
               &metadataCallback, &stream, 0, glmNormal);
    REQUIRE(context.width == 1);
    REQUIRE(context.height == 1);
    REQUIRE(context.pixels.size() == 4);
}

TEST_CASE("TLG5 loader round-trips a compressed krkrz layerExSave stream") {
    initVisualKernels();

    // A deliberately repetitive image makes the pinned SlideCompressor take
    // its compressed path for at least one channel, exercising the same
    // bounded preflight used by production TLG5 loading.
    constexpr tjs_uint width = 32;
    constexpr tjs_uint height = 16;
    std::vector<tjs_uint8> expected(static_cast<size_t>(width) * height * 4);
    for(tjs_uint y = 0; y < height; ++y) {
        for(tjs_uint x = 0; x < width; ++x) {
            auto *pixel = expected.data() +
                          (static_cast<size_t>(y) * width + x) * 4;
            pixel[0] = static_cast<tjs_uint8>((x / 8) ? 20 : 10); // B
            pixel[1] = static_cast<tjs_uint8>((y / 4) ? 40 : 30); // G
            pixel[2] = 80;                                        // R
            pixel[3] = 255;
        }
    }

    std::vector<tjs_uint8> encoded;
    REQUIRE(aether::krkrz::layer_save::encodeTlg5(
        expected.data(), static_cast<int>(width), static_cast<int>(height),
        static_cast<int>(width * 4), encoded));

    bool sawCompressedChannel = false;
    const std::uint32_t firstBlockSize =
        static_cast<std::uint32_t>(encoded[24]) |
        (static_cast<std::uint32_t>(encoded[25]) << 8) |
        (static_cast<std::uint32_t>(encoded[26]) << 16) |
        (static_cast<std::uint32_t>(encoded[27]) << 24);
    REQUIRE(firstBlockSize > 0);
    size_t position = 24 + 4 * 4; // four block-size entries precede data
    const size_t firstBlockEnd = position + firstBlockSize;
    REQUIRE(firstBlockEnd <= encoded.size());
    for(int channel = 0; channel < 4 && position + 5 <= firstBlockEnd;
        ++channel) {
        const tjs_uint8 mode = encoded[position];
        const std::uint32_t size = static_cast<std::uint32_t>(encoded[position + 1]) |
                                   (static_cast<std::uint32_t>(encoded[position + 2]) << 8) |
                                   (static_cast<std::uint32_t>(encoded[position + 3]) << 16) |
                                   (static_cast<std::uint32_t>(encoded[position + 4]) << 24);
        position += 5;
        REQUIRE(size <= firstBlockEnd - position);
        sawCompressedChannel = sawCompressedChannel || mode == 0;
        position += size;
    }
    REQUIRE(sawCompressedChannel);

    tTVPMemoryStream stream;
    stream.SetSize(static_cast<tjs_uint>(encoded.size()));
    stream.Write(encoded.data(), static_cast<tjs_uint>(encoded.size()));
    stream.SetPosition(0);
    DecodeContext context;
    TVPLoadTLG(nullptr, &context, &sizeCallback, &scanlineCallback,
               &metadataCallback, &stream, 0, glmNormal);
    REQUIRE(context.width == width);
    REQUIRE(context.height == height);
    CHECK(context.pixels == expected);
}
