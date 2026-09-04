#include <catch2/catch_test_macros.hpp>

#include "UtilStreams.h"
#include "kbadDataPack.h"
#include "tjsNs0DataPack.h"

#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <vector>

namespace {

using Byte = std::uint8_t;

void appendString(std::vector<Byte> &bytes, std::string_view value) {
    REQUIRE(value.size() <= 31);
    bytes.push_back(static_cast<Byte>(0xa0u + value.size()));
    for(const char character : value) {
        bytes.push_back(static_cast<Byte>(character));
        bytes.push_back(0);
    }
}

void appendU16(std::vector<Byte> &bytes, std::uint16_t value) {
    bytes.push_back(0xcd);
    bytes.push_back(static_cast<Byte>(value));
    bytes.push_back(static_cast<Byte>(value >> 8));
}

void appendRaw16(std::vector<Byte> &bytes,
                 std::initializer_list<Byte> value) {
    REQUIRE(value.size() <= 0xffff);
    bytes.push_back(0xda);
    bytes.push_back(static_cast<Byte>(value.size()));
    bytes.push_back(static_cast<Byte>(value.size() >> 8));
    bytes.insert(bytes.end(), value);
}

tTJSVariant getIndex(const tTJSVariant &object, tjs_int index) {
    REQUIRE(object.Type() == tvtObject);
    iTJSDispatch2 *dispatch = object.AsObjectNoAddRef();
    REQUIRE(dispatch != nullptr);

    tTJSVariant result;
    REQUIRE(TJS_SUCCEEDED(
        dispatch->PropGetByNum(TJS_IGNOREPROP, index, &result, dispatch)));
    return result;
}

tTJSVariant getProperty(const tTJSVariant &object, const tjs_char *name) {
    REQUIRE(object.Type() == tvtObject);
    iTJSDispatch2 *dispatch = object.AsObjectNoAddRef();
    REQUIRE(dispatch != nullptr);

    tTJSVariant result;
    REQUIRE(TJS_SUCCEEDED(
        dispatch->PropGet(0, name, nullptr, &result, dispatch)));
    return result;
}

std::vector<Byte> makePbdMetadataFixture() {
    std::vector<Byte> bytes = { 'K', 'B', 'A', 'D', '1', '0', '0', 0 };
    bytes.push_back(0x92); // two-record array

    bytes.push_back(0x82); // canvas metadata
    appendString(bytes, "width");
    appendU16(bytes, 1920);
    appendString(bytes, "height");
    appendU16(bytes, 1080);

    bytes.push_back(0x82); // layer metadata
    appendString(bytes, "name");
    appendString(bytes, "com");
    appendString(bytes, "visible");
    bytes.push_back(1);
    return bytes;
}

} // namespace

TEST_CASE("KBAD data packs decode PBD canvas and layer metadata") {
    const std::vector<Byte> bytes = makePbdMetadataFixture();
    tTJSVariant root;

    REQUIRE(TVPDecodeKbadDataPack(bytes.data(), bytes.size(), &root));
    REQUIRE(root.Type() == tvtObject);
    CHECK(getProperty(root, TJS_W("count")).AsInteger() == 2);

    const tTJSVariant canvas = getIndex(root, 0);
    CHECK(getProperty(canvas, TJS_W("width")).AsInteger() == 1920);
    CHECK(getProperty(canvas, TJS_W("height")).AsInteger() == 1080);

    const tTJSVariant layer = getIndex(root, 1);
    CHECK(ttstr(getProperty(layer, TJS_W("name"))) == TJS_W("com"));
    CHECK(getProperty(layer, TJS_W("visible")).AsInteger() == 1);
}

TEST_CASE("KBAD data packs preserve fixed and length-prefixed octets") {
    std::vector<Byte> bytes = { 'K', 'B', 'A', 'D', '1', '0', '0', 0 };
    bytes.push_back(0x92); // two octets
    bytes.push_back(0xd9); // five-byte fixed octet
    bytes.insert(bytes.end(), { 0x00, 0x7f, 0x80, 0xfe, 0xff });
    appendRaw16(bytes, { 0x10, 0x20, 0x30, 0x40, 0x50, 0x60 });

    tTJSVariant root;
    REQUIRE(TVPDecodeKbadDataPack(bytes.data(), bytes.size(), &root));

    const tTJSVariant fixed = getIndex(root, 0);
    REQUIRE(fixed.Type() == tvtOctet);
    const tTJSVariantOctet *fixedOctet = fixed.AsOctetNoAddRef();
    REQUIRE(fixedOctet != nullptr);
    REQUIRE(fixedOctet->GetLength() == 5);
    CHECK(std::vector<Byte>(fixedOctet->GetData(),
                            fixedOctet->GetData() + fixedOctet->GetLength()) ==
          std::vector<Byte>{ 0x00, 0x7f, 0x80, 0xfe, 0xff });

    const tTJSVariant raw16 = getIndex(root, 1);
    REQUIRE(raw16.Type() == tvtOctet);
    const tTJSVariantOctet *raw16Octet = raw16.AsOctetNoAddRef();
    REQUIRE(raw16Octet != nullptr);
    REQUIRE(raw16Octet->GetLength() == 6);
    CHECK(std::vector<Byte>(raw16Octet->GetData(),
                            raw16Octet->GetData() + raw16Octet->GetLength()) ==
          std::vector<Byte>{ 0x10, 0x20, 0x30, 0x40, 0x50, 0x60 });
}

TEST_CASE("KBAD decoder rejects unrelated and truncated data") {
    const std::vector<Byte> unrelated = { 'T', 'J', 'S', '/', 'n', 's', '0', 0 };
    tTJSVariant result(static_cast<tjs_int>(77));
    CHECK_FALSE(
        TVPDecodeKbadDataPack(unrelated.data(), unrelated.size(), &result));
    CHECK(result.AsInteger() == 77);

    const std::vector<Byte> truncated = {
        'K', 'B', 'A', 'D', '1', '0', '0', 0, 0x91,
    };
    REQUIRE_THROWS(
        TVPDecodeKbadDataPack(truncated.data(), truncated.size(), &result));
}

TEST_CASE("registered structured loader accepts KBAD save containers") {
    const std::vector<Byte> bytes = makePbdMetadataFixture();
    tTVPMemoryStream stream(bytes.data(), static_cast<tjs_uint>(bytes.size()));
    tTJSVariant root;

    TVPRegisterTjsNs0DataPackLoader();
    REQUIRE(TJS::TJSLoadStructuredDataPack(&stream, &root));
    REQUIRE(root.Type() == tvtObject);
    CHECK(getProperty(getIndex(root, 0), TJS_W("width")).AsInteger() == 1920);
}
