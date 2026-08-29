#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "portableResourceObjects.h"

using namespace AetherKiri::ResourceObjects;

TEST_CASE("portable resource icon image and group round-trip") {
    // A tiny PNG-like payload is enough for the container; the codec preserves
    // the image bytes and directory metadata without requiring an image
    // decoder.
    const std::vector<std::uint8_t> imageBytes{0x89, 0x50, 0x4e, 0x47};
    std::vector<std::uint8_t> ico{
        0, 0, 1, 0, 1, 0,
        16, 16, 0, 0, 1, 0, 32, 0, 4, 0, 0, 0, 22, 0, 0, 0,
        0x89, 0x50, 0x4e, 0x47};

    IconImage image;
    REQUIRE(image.load(ico.data(), ico.size()));
    REQUIRE(image.count() == 1);
    CHECK_FALSE(image.isCursor());
    CHECK(image.setID(0, 7));
    CHECK(image.setImage(0, imageBytes.data(), imageBytes.size()));

    std::vector<std::uint8_t> encoded;
    REQUIRE(image.save(encoded));
    IconImage decoded;
    REQUIRE(decoded.load(encoded.data(), encoded.size()));
    REQUIRE(decoded.count() == 1);
    int id = -1;
    REQUIRE(decoded.getID(0, id));
    // The standard ICO directory has no resource-ID field.  IDs belong to
    // the RT_GROUP_ICON wrapper and therefore intentionally reset when a raw
    // ICO is serialized and loaded on its own.
    CHECK(id == -1);
    REQUIRE(decoded.getImage(0) != nullptr);
    CHECK(*decoded.getImage(0) == imageBytes);

    IconGroup group;
    REQUIRE(group.fromImage(image));
    REQUIRE(group.count() == 1);
    std::vector<std::uint8_t> groupBytes;
    REQUIRE(group.save(groupBytes));
    IconGroup groupDecoded;
    REQUIRE(groupDecoded.load(groupBytes.data(), groupBytes.size()));
    CHECK(groupDecoded.count() == 1);
    REQUIRE(groupDecoded.getID(0, id));
    CHECK(id == 7);
    IconImage groupImage;
    REQUIRE(groupDecoded.toImage(groupImage));
    CHECK(groupImage.count() == 1);
    REQUIRE(groupImage.getID(0, id));
    CHECK(id == 7);
}

TEST_CASE("portable resource cursor hotspots are represented safely") {
    IconImage cursor;
    const std::vector<std::uint8_t> cur{
        0, 0, 2, 0, 1, 0,
        16, 16, 0, 0, 4, 0, 0, 0, 1, 0, 0, 0, 22, 0, 0, 0,
        1};
    REQUIRE(cursor.load(cur.data(), cur.size()));
    CHECK(cursor.isCursor());
    CHECK(cursor.setHotSpot(0, 3, 4));
    std::uint16_t x = 0, y = 0;
    REQUIRE(cursor.getHotSpot(0, x, y));
    CHECK(x == 3);
    CHECK(y == 4);
    cursor.setCursor(false);
    CHECK_FALSE(cursor.getHotSpot(0, x, y));
    cursor.setCursor(true);
    CHECK(cursor.getHotSpot(0, x, y));
    CHECK(x == 0);
    CHECK(y == 0);
}

TEST_CASE("portable version info supports mutation and standard serialization") {
    VersionInfo info;
    info.reset(0x041104b0u);
    REQUIRE(info.changeString(u"ProductName", u"AetherKiri", 0x041104b0u));
    REQUIRE(info.changeInfo(u"FileVersion", 0x0001000200030004ULL));
    REQUIRE(info.addLanguage(0x040904b0u));
    REQUIRE(info.changeString(u"ProductName", u"AetherKiri EN", 0x040904b0u));
    CHECK_FALSE(info.addLanguage(0x040904b0u));
    CHECK_FALSE(info.copyLanguage(0x041104b0u, 0x040904b0u));
    REQUIRE(info.copyLanguage(0x040904b0u, 0x080904b0u));
    CHECK_FALSE(info.copyLanguage(0x080904b0u, 0x080904b0u));
    CHECK(info.removeLanguage(0x080904b0u));
    CHECK_FALSE(info.removeLanguage(0x080904b0u));

    std::vector<std::uint8_t> encoded;
    REQUIRE(info.save(encoded));
    VersionInfo decoded;
    REQUIRE(decoded.load(encoded.data(), encoded.size()));
    const auto languages = decoded.languages();
    CHECK(languages.size() == 2);
    CHECK(decoded.changeString(u"ProductName", u"updated", 0x040904b0u));

    // A malformed/truncated node must not be accepted or allocate without a
    // bound.
    encoded.resize(encoded.size() - 1);
    CHECK_FALSE(decoded.load(encoded.data(), encoded.size()));
}
