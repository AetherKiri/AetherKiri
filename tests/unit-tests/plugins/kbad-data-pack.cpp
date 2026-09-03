#include <catch2/catch_test_macros.hpp>

#include "UtilStreams.h"
#include "kbadDataPack.h"
#include "tjsNs0DataPack.h"

#include <cstdint>
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
