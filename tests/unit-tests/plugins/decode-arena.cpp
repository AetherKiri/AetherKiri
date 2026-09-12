#include <catch2/catch_test_macros.hpp>

#include "TVPDecodeArena.h"

#include <cstdint>
#include <cstdlib>

TEST_CASE("decode arena growth keeps previously returned pointers stable") {
#if defined(__APPLE__) || defined(__linux__) || defined(__ANDROID__)
    TVPDecodeArena& arena = TVPDecodeArena::Instance();
    arena.Begin();

    auto* header = static_cast<uint8_t*>(arena.Alloc(64));
    REQUIRE(header != nullptr);
    for(size_t index = 0; index < 64; ++index)
        header[index] = static_cast<uint8_t>(index ^ 0xa5u);

    // Exceed the retained 4 MiB block. A moving arena would invalidate
    // header here while libpng still holds the equivalent png_struct pointer.
    void* overflow = arena.Alloc(5 * 1024 * 1024);
    REQUIRE(overflow != nullptr);
    CHECK(arena.Owns(header));
    CHECK(arena.Owns(overflow));
    for(size_t index = 0; index < 64; ++index)
        CHECK(header[index] == static_cast<uint8_t>(index ^ 0xa5u));

    void* fallback = std::malloc(32);
    REQUIRE(fallback != nullptr);
    CHECK_FALSE(arena.Owns(fallback));
    std::free(fallback);

    arena.End();
    CHECK(arena.GetLastAllocCount() == 2);
    CHECK(arena.GetLastPeakBytes() >= 5 * 1024 * 1024 + 64);

    // The normal block remains reusable after overflow segments are released.
    arena.Begin();
    REQUIRE(arena.Alloc(128) != nullptr);
    arena.End();
#else
    SUCCEED("decode arena is not enabled on this platform");
#endif
}
