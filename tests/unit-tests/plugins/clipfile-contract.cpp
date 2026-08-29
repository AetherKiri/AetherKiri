#include <catch2/catch_test_macros.hpp>

#include "clipfile.h"
#include "clipwrite.h"

#include <array>
#include <cstdint>
#include <cstring>

#include <sqlite3.h>

namespace {

void putBigEndian64(std::uint8_t *out, std::uint64_t value) {
    for(int i = 7; i >= 0; --i) {
        out[i] = static_cast<std::uint8_t>(value & 0xffu);
        value >>= 8;
    }
}

} // namespace

TEST_CASE("clipparse rejects truncated and header-only data safely") {
    clip::ClipFile reader;

    const std::array<std::uint8_t, 8> truncated{{'C', 'S', 'F', 'C',
                                                   'H', 'U', 'N', 'K'}};
    CHECK_FALSE(reader.loadFromMemory(truncated.data(), truncated.size()));
    CHECK_FALSE(reader.error().empty());

    // A structurally valid outer header with no chunks must fail with the
    // parser's explicit CHNKSQLi diagnostic, not dereference past the buffer.
    std::array<std::uint8_t, 64> header{};
    std::memcpy(header.data(), "CSFCHUNK", 8);
    putBigEndian64(header.data() + 8, header.size());
    putBigEndian64(header.data() + 16, header.size());
    CHECK_FALSE(reader.loadFromMemory(header.data(), header.size()));
    CHECK(reader.error() == "no CHNKSQLi chunk");

    reader.clear();
    CHECK(reader.layers().empty());
    CHECK(reader.canvasWidth() == 0);
}

TEST_CASE("clip writer keeps an unloaded instance inert") {
    clip::ClipWriter writer;
    CHECK(writer.externalCount() == 0);
    CHECK(writer.save("__aether_missing_clip_writer__.clip") == 0);
    CHECK(writer.error() == "not loaded");
    writer.clear();
    CHECK(writer.externalCount() == 0);
}

TEST_CASE("clip adapter uses the shared deserialize-capable SQLite owner") {
    // clipparse and Aether's sqlite:// VFS must resolve to one modern
    // process-wide owner.  The deserialize API is the capability that the
    // upstream CLIP parser requires and is absent from the old bundled copy.
    CHECK(sqlite3_libversion_number() >= 3045000);

    sqlite3 *db = nullptr;
    REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);
    sqlite3_int64 serialized_size = 0;
    unsigned char *serialized =
        sqlite3_serialize(db, "main", &serialized_size, 0);
    REQUIRE(serialized != nullptr);
    CHECK(serialized_size >= 0);
    sqlite3_free(serialized);
    CHECK(sqlite3_close(db) == SQLITE_OK);
}
