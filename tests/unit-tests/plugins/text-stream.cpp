#include <catch2/catch_test_macros.hpp>

#include "TextStream.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

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
