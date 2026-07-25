#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "godot/GodotGpuBridge.h"
#include "godot/GodotRenderManager.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace {

struct TriangleDrawCall {
    int calls = 0;
    uint64_t dst = 0;
    uint64_t src = 0;
    uint32_t triangle_count = 0;
    float opacity = 0.0f;
    uint32_t blend_mode = 0;
};

TriangleDrawCall g_triangle_draw_call;
uint64_t g_next_texture_handle = 1;

uint64_t CreateTestTexture(uint32_t, uint32_t, const void *, uint32_t) {
    return g_next_texture_handle++;
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

bool DrawTestTriangles(uint64_t dst, uint64_t src, uint32_t triangle_count,
                       const tTVPRect *, const tTVPPointD *,
                       const tTVPPointD *, float opacity,
                       uint32_t blend_mode) {
    ++g_triangle_draw_call.calls;
    g_triangle_draw_call.dst = dst;
    g_triangle_draw_call.src = src;
    g_triangle_draw_call.triangle_count = triangle_count;
    g_triangle_draw_call.opacity = opacity;
    g_triangle_draw_call.blend_mode = blend_mode;
    return true;
}

class TestGpuBridge {
public:
    TestGpuBridge() {
        g_triangle_draw_call = {};
        g_next_texture_handle = 1;
        TVPGodotGpuBridgeCallbacks callbacks{};
        callbacks.create_rgba = CreateTestTexture;
        callbacks.release_texture = ReleaseTestTexture;
        callbacks.draw_triangles = DrawTestTriangles;
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

TEST_CASE("Godot texture updates clip off-texture rectangles") {
    GodotTexture2D texture(nullptr, 0, 2, 2, TVPTextureFormat::RGBA);
    const std::array<std::uint8_t, 16> pixels = {
        1, 0, 0, 255, 2, 0, 0, 255,
        3, 0, 0, 255, 4, 0, 0, 255,
    };

    texture.Update(pixels.data(), TVPTextureFormat::RGBA, 8,
                   tTVPRect(-1, -1, 1, 1));

    CHECK(texture.GetPoint(0, 0) == 0xff000004u);
    CHECK(texture.GetPoint(1, 0) == 0u);
    CHECK(texture.GetPoint(0, 1) == 0u);
}

TEST_CASE("Godot texture updates reallocate when the pixel format changes") {
    GodotTexture2D texture(nullptr, 0, 2, 1, TVPTextureFormat::Gray);
    const std::array<std::uint8_t, 8> pixels = {
        1, 2, 3, 255, 4, 5, 6, 255,
    };

    texture.Update(pixels.data(), TVPTextureFormat::RGBA, 8,
                   tTVPRect(0, 0, 2, 1));

    CHECK(texture.GetFormat() == TVPTextureFormat::RGBA);
    CHECK(texture.GetPitch() == 8);
    CHECK(texture.GetPoint(1, 0) == 0xff060504u);
}

TEST_CASE("Godot textures tag Kirikiri triangle blend modes") {
    TestGpuBridge bridge;
    std::array<std::uint8_t, 16> pixels = {
        0, 0, 0, 255, 0, 0, 0, 255,
        0, 0, 0, 255, 0, 0, 0, 255,
    };
    GodotTexture2D dst(pixels.data(), 8, 2, 2, TVPTextureFormat::RGBA);
    GodotTexture2D src(pixels.data(), 8, 2, 2, TVPTextureFormat::RGBA);
    REQUIRE(dst.EnsureGpuHandle());
    REQUIRE(src.EnsureGpuHandle());

    const tTVPRect clip(0, 0, 2, 2);
    const std::array<tTVPPointD, 3> points = {
        tTVPPointD{0.0, 0.0},
        tTVPPointD{2.0, 0.0},
        tTVPPointD{0.0, 2.0},
    };
    REQUIRE(dst.BlendTrianglesGpuFrom(
        &src, 1, clip, points.data(), points.data(),
        TVP_GODOT_GPU_BLEND_ALPHA_D, 128));

    CHECK(g_triangle_draw_call.calls == 1);
    CHECK(g_triangle_draw_call.dst == dst.GetGodotGpuHandle());
    CHECK(g_triangle_draw_call.src == src.GetGodotGpuHandle());
    CHECK(g_triangle_draw_call.triangle_count == 1);
    CHECK(g_triangle_draw_call.opacity == Catch::Approx(128.0f / 255.0f));
    CHECK(g_triangle_draw_call.blend_mode ==
          (TVP_GODOT_GPU_TRIANGLE_TVP_BLEND |
           TVP_GODOT_GPU_BLEND_ALPHA_D));
}

TEST_CASE("Godot render manager routes affine alpha blends to GPU triangles") {
    TestGpuBridge bridge;
    std::vector<std::uint8_t> pixels(256u * 256u * 4u, 0xffu);
    GodotTexture2D dst(pixels.data(), 256 * 4, 256, 256,
                       TVPTextureFormat::RGBA);
    GodotTexture2D src(pixels.data(), 256 * 4, 256, 256,
                       TVPTextureFormat::RGBA);
    const tTVPRect clip(0, 0, 256, 256);
    const std::array<tTVPPointD, 3> points = {
        tTVPPointD{0.0, 0.0},
        tTVPPointD{256.0, 0.0},
        tTVPPointD{0.0, 256.0},
    };
    std::pair<iTVPTexture2D *, const tTVPPointD *> source{
        &src, points.data()};
    const tRenderTexQuadArray textures(&source, 1);
    GodotRenderManager manager;

    const std::array<std::pair<const char *, uint32_t>, 6> methods = {{
        {"AlphaBlend", TVP_GODOT_GPU_BLEND_ALPHA},
        {"AlphaBlend_d", TVP_GODOT_GPU_BLEND_ALPHA_D},
        {"PerspectiveAlphaBlend_a", TVP_GODOT_GPU_BLEND_ALPHA_BLEND_A},
        {"PsAddBlend", TVP_GODOT_GPU_BLEND_PS_ADD},
        {"PsSubBlend", TVP_GODOT_GPU_BLEND_PS_SUBTRACT},
        {"PsMulBlend", TVP_GODOT_GPU_BLEND_PS_MULTIPLY},
    }};
    for (const auto &[name, expected_mode] : methods) {
        GodotRenderMethod method(nullptr);
        method.SetName(name);
        method.SetParameterOpa(-1, 128);
        g_triangle_draw_call = {};

        manager.OperateTriangles(&method, 1, &dst, nullptr, clip,
                                 points.data(), textures);

        INFO("render method: " << name);
        CHECK(g_triangle_draw_call.calls == 1);
        CHECK(g_triangle_draw_call.opacity ==
              Catch::Approx(128.0f / 255.0f));
        CHECK(g_triangle_draw_call.blend_mode ==
              (TVP_GODOT_GPU_TRIANGLE_TVP_BLEND | expected_mode));
    }
}
