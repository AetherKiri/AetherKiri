#include <catch2/catch_test_macros.hpp>

#include "godot/GodotGpuBridge.h"
#include "godot/GodotRenderManager.h"

#include <array>
#include <cstdint>
#include <cstring>

namespace {

uint64_t CreateTestTexture(uint32_t, uint32_t, const void *, uint32_t) {
    return 1;
}

void ReleaseTestTexture(uint64_t) {}

bool ReadTestGrayTexture(uint64_t, void *out_pixels, size_t out_pixels_size,
                         uint32_t stride_bytes) {
    if(out_pixels == nullptr || out_pixels_size < 24 || stride_bytes != 12)
        return false;

    const std::array<std::uint8_t, 24> rgba = {
        11, 11, 11, 255, 22, 22, 22, 255, 33, 33, 33, 255,
        44, 44, 44, 255, 55, 55, 55, 255, 66, 66, 66, 255,
    };
    std::memcpy(out_pixels, rgba.data(), rgba.size());
    return true;
}

class TestGpuBridge {
public:
    TestGpuBridge() {
        TVPGodotGpuBridgeCallbacks callbacks{};
        callbacks.create_rgba = CreateTestTexture;
        callbacks.release_texture = ReleaseTestTexture;
        callbacks.read_rgba = ReadTestGrayTexture;
        TVPGodotGpuBridgeRegister(&callbacks);
    }

    ~TestGpuBridge() { TVPGodotGpuBridgeRegister(nullptr); }
};

} // namespace

TEST_CASE("Godot textures expose Gray province pixels") {
    std::array<std::uint8_t, 8> pixels = {
        1, 2, 3, 0xee,
        4, 5, 6, 0xee,
    };
    GodotTexture2D texture(pixels.data(), 4, 3, 2,
                           TVPTextureFormat::Gray);

    CHECK(texture.GetPoint(0, 0) == 1);
    CHECK(texture.GetPoint(2, 1) == 6);
    CHECK(texture.GetPoint(-1, 0) == 0);
    CHECK(texture.GetPoint(3, 0) == 0);

    texture.SetPoint(1, 1, 0x1234);
    CHECK(texture.GetPoint(1, 1) == 0x34);

    const auto *row = static_cast<const std::uint8_t *>(
        texture.GetScanLineForRead(1));
    REQUIRE(row != nullptr);
    CHECK(row[0] == 4);
    CHECK(row[1] == 0x34);
    CHECK(row[2] == 6);
    CHECK(row[3] == 0xee);
}

TEST_CASE("Godot Gray textures extract province pixels after GPU writes") {
    TestGpuBridge bridge;
    std::array<std::uint8_t, 8> pixels = {
        1, 2, 3, 0xee,
        4, 5, 6, 0xee,
    };
    GodotTexture2D texture(pixels.data(), 4, 3, 2,
                           TVPTextureFormat::Gray);

    REQUIRE(texture.EnsureGpuHandle());
    texture.MarkGpuDirty();

    CHECK(texture.GetPoint(0, 0) == 11);
    CHECK(texture.GetPoint(2, 0) == 33);
    CHECK(texture.GetPoint(0, 1) == 44);
    CHECK(texture.GetPoint(2, 1) == 66);

    const auto *row = static_cast<const std::uint8_t *>(
        texture.GetScanLineForRead(1));
    REQUIRE(row != nullptr);
    CHECK(row[0] == 44);
    CHECK(row[1] == 55);
    CHECK(row[2] == 66);
    CHECK(row[3] == 0xee);
}
