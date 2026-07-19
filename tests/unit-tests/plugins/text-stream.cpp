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
