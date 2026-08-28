#include <catch2/catch_test_macros.hpp>

#include "TextStream.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <zlib.h>

namespace {

class TemporaryFile {
public:
    explicit TemporaryFile(const std::string &name) {
        const auto unique = std::chrono::steady_clock::now()
                                .time_since_epoch()
                                .count();
        path = std::filesystem::temp_directory_path() /
               (name + "-" + std::to_string(unique));
    }

    ~TemporaryFile() {
        std::error_code error;
        std::filesystem::remove(path, error);
    }

    std::filesystem::path path;
};

void appendLe64(std::vector<unsigned char> &bytes, std::uint64_t value) {
    for(int shift = 0; shift < 64; shift += 8)
        bytes.push_back(static_cast<unsigned char>(value >> shift));
}

std::vector<unsigned char> makeBmpWithTextPayload(
    const std::vector<unsigned char> &payload) {
    constexpr std::uint32_t imageSize = 54;
    std::vector<unsigned char> bytes(imageSize, 0);
    bytes[0] = 'B';
    bytes[1] = 'M';
    bytes[2] = static_cast<unsigned char>(imageSize);
    bytes[10] = static_cast<unsigned char>(imageSize);
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}

void writeBytes(const TemporaryFile &file,
                const std::vector<unsigned char> &bytes) {
    std::ofstream output(file.path, std::ios::binary);
    REQUIRE(output.good());
    output.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    REQUIRE(output.good());
}

} // namespace

TEST_CASE("UTF-8 BOM text streams end at the payload boundary") {
    TemporaryFile file("aetherkiri-utf8-bom.tjs");
    const std::array<unsigned char, 19> content = {
        0xEF, 0xBB, 0xBF,
        'v',  'a',  'r',  ' ',  'a',  'n',  's',  'w',  'e',  'r',
        ' ',  '=',  ' ',  '4',  '2',  ';',
    };
    {
        std::ofstream output(file.path, std::ios::binary);
        REQUIRE(output.good());
        output.write(reinterpret_cast<const char *>(content.data()),
                     static_cast<std::streamsize>(content.size()));
        REQUIRE(output.good());
    }

    std::unique_ptr<iTJSTextReadStream> stream(
        TVPCreateTextStreamForRead(ttstr(file.path.string()), TJS_W("")));
    tTJSString decoded;
    REQUIRE(stream->Read(decoded, 0) == 16);
    CHECK(ttstr(decoded) == TJS_W("var answer = 42;"));
}

TEST_CASE("UTF-32 BOMs take precedence over their UTF-16 prefixes") {
    const std::array<unsigned char, 4> little_endian = {
        0xFF, 0xFE, 0x00, 0x00,
    };
    const std::array<unsigned char, 4> big_endian = {
        0x00, 0x00, 0xFE, 0xFF,
    };
    std::uint8_t bom_size = 0;

    CHECK(checkTextEncoding(little_endian.data(), little_endian.size(),
                            bom_size) == "UTF-32LE");
    CHECK(bom_size == 4);

    bom_size = 0;
    CHECK(checkTextEncoding(big_endian.data(), big_endian.size(), bom_size) ==
          "UTF-32BE");
    CHECK(bom_size == 4);
}

TEST_CASE("invalid EUC-JP guesses fall back to lossless GBK") {
    // GBK-encoded Japanese from a translated KAG system script.  Its kana
    // prefix strongly resembles EUC-JP, but 0x84 0x49 is not valid EUC-JP.
    const std::array<unsigned char, 44> content = {
        0xA5, 0xC6, 0xA5, 0xAD, 0xA5, 0xB9, 0xA5, 0xC8, 0xA4, 0xCE, 0xA5,
        0xEC, 0xA5, 0xF3, 0xA5, 0xC0, 0xA5, 0xEA, 0xA5, 0xF3, 0xA5, 0xB0,
        0x84, 0x49, 0xC0, 0xED, 0xA4, 0xF2, 0xD0, 0xD0, 0xA4, 0xA6, 0xA4,
        0xBF, 0xA4, 0xE1, 0xA4, 0xCE, 0xA5, 0xAF, 0xA5, 0xE9, 0xA5, 0xB9,
    };
    std::uint8_t bom_size = 0;

    CHECK(checkTextEncoding(content.data(), content.size(), bom_size) ==
          "GBK");
    CHECK(bom_size == 0);
}

TEST_CASE(
    "explicit legacy text encoding overrides an ambiguous detector guess") {
    TemporaryFile file("aetherkiri-explicit-gbk.stand");
    // filename:'愛莉a' encoded as GBK.  The non-ASCII bytes are also a valid
    // CP932 sequence, so detector-only decoding produces mojibake.
    const std::array<unsigned char, 16> content = {
        'f',  'i',  'l',  'e',  'n',  'a',  'm',  'e',
        ':',  '\'', 0x90, 0xDB, 0xC0, 0xF2, 'a',  '\'',
    };
    {
        std::ofstream output(file.path, std::ios::binary);
        REQUIRE(output.good());
        output.write(reinterpret_cast<const char *>(content.data()),
                     static_cast<std::streamsize>(content.size()));
        REQUIRE(output.good());
    }

    struct RestoreDefaultEncoding {
        ~RestoreDefaultEncoding() { TVPSetDefaultReadEncoding(TJS_W("utf-8")); }
    } restore;
    TVPSetDefaultReadEncoding(TJS_W("gbk"));

    std::unique_ptr<iTJSTextReadStream> stream(
        TVPCreateTextStreamForRead(ttstr(file.path.string()), TJS_W("")));
    tTJSString decoded;
    REQUIRE(stream->Read(decoded, 0) == 14);
    CHECK(ttstr(decoded) == TJS_W("filename:'愛莉a'"));
}

TEST_CASE("BOM-less CP932 scripts use the legacy KiriKiri fallback") {
    TemporaryFile file("aetherkiri-cp932.tjs");
    const std::array<unsigned char, 21> content = {
        'v',  'a',  'r',  ' ',  'n',  'a',  'm',  'e',  ' ',  '=',  ' ',
        '"', 0x91, 0xCC, 0x8C, 0xB1, 0x94, 0xC5, '"',  ';',  '\n',
    };
    {
        std::ofstream output(file.path, std::ios::binary);
        REQUIRE(output.good());
        output.write(reinterpret_cast<const char *>(content.data()),
                     static_cast<std::streamsize>(content.size()));
        REQUIRE(output.good());
    }

    std::unique_ptr<iTJSTextReadStream> stream(
        TVPCreateTextStreamForRead(ttstr(file.path.string()), TJS_W("")));
    tTJSString decoded;
    REQUIRE(stream->Read(decoded, 0) == 18);
    CHECK(ttstr(decoded) == TJS_W("var name = \"\u4f53\u9a13\u7248\";\n"));
}

TEST_CASE("BMP save thumbnails expose an appended UTF-16 text payload") {
    TemporaryFile file("aetherkiri-bmp-save-utf16.bmp");
    const std::u16string text = u"(const) %[\"slot\" => 1]";
    std::vector<unsigned char> payload = {0xff, 0xfe};
    for(const char16_t character : text) {
        payload.push_back(static_cast<unsigned char>(character));
        payload.push_back(static_cast<unsigned char>(character >> 8));
    }
    writeBytes(file, makeBmpWithTextPayload(payload));

    std::unique_ptr<iTJSTextReadStream> stream(
        TVPCreateTextStreamForRead(ttstr(file.path.string()), TJS_W("")));
    tTJSString decoded;
    REQUIRE(stream->Read(decoded, 0) == text.size());
    CHECK(ttstr(decoded) == TJS_W("(const) %[\"slot\" => 1]"));
}

TEST_CASE("BMP save thumbnails expose an appended compressed text payload") {
    TemporaryFile file("aetherkiri-bmp-save-compressed.bmp");
    const std::u16string text = u"(const) [1, 2, 3]";
    const auto *source = reinterpret_cast<const unsigned char *>(text.data());
    const auto sourceSize = static_cast<uLong>(text.size() * sizeof(char16_t));
    uLongf compressedSize = compressBound(sourceSize);
    std::vector<unsigned char> compressed(compressedSize);
    REQUIRE(compress2(compressed.data(), &compressedSize, source, sourceSize,
                      Z_BEST_SPEED) == Z_OK);
    compressed.resize(compressedSize);

    std::vector<unsigned char> payload = {0xfe, 0xfe, 0x02, 0xff, 0xfe};
    appendLe64(payload, compressed.size());
    appendLe64(payload, sourceSize);
    payload.insert(payload.end(), compressed.begin(), compressed.end());
    writeBytes(file, makeBmpWithTextPayload(payload));

    std::unique_ptr<iTJSTextReadStream> stream(
        TVPCreateTextStreamForRead(ttstr(file.path.string()), TJS_W("")));
    tTJSString decoded;
    REQUIRE(stream->Read(decoded, 0) == text.size());
    CHECK(ttstr(decoded) == TJS_W("(const) [1, 2, 3]"));
}
