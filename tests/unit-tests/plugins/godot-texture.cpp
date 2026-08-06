#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "godot/GodotGpuBridge.h"
#include "godot/GodotRenderManager.h"
#include "LayerBitmapIntf.h"
#include "LayerIntf.h"

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

enum class GpuCall {
    Flush,
    Blend,
};

TriangleDrawCall g_triangle_draw_call;
int g_blend_rect_calls = 0;
uint64_t g_next_texture_handle = 1;
uint64_t g_next_readback_handle = 101;
uint64_t g_last_discarded_readback = 0;
bool g_readback_ready = false;
std::vector<GpuCall> g_gpu_calls;

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

uint64_t BeginTestReadback(uint64_t texture) {
    return texture != 0 ? g_next_readback_handle++ : 0;
}

bool PollTestReadback(uint64_t request, void *out_pixels,
                      size_t out_pixels_size, uint32_t stride_bytes,
                      bool *ready) {
    if(ready) *ready = g_readback_ready;
    if(request == 0 || out_pixels == nullptr || !g_readback_ready) {
        return request != 0 && out_pixels != nullptr;
    }
    if(out_pixels_size < 8 || stride_bytes != 8) return false;
    const std::array<std::uint8_t, 8> rgba = {
        1, 2, 3, 4, 5, 6, 7, 8,
    };
    std::memcpy(out_pixels, rgba.data(), rgba.size());
    return true;
}

void DiscardTestReadback(uint64_t request) {
    g_last_discarded_readback = request;
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

bool BlendTestRect(uint64_t, uint64_t, const tTVPRect *, const tTVPRect *,
                   uint32_t, int, uint32_t) {
    g_gpu_calls.push_back(GpuCall::Blend);
    ++g_blend_rect_calls;
    return true;
}

bool FlushTestGpu() {
    g_gpu_calls.push_back(GpuCall::Flush);
    return true;
}

class TestGpuBridge {
public:
    TestGpuBridge() {
        g_triangle_draw_call = {};
        g_blend_rect_calls = 0;
        g_next_texture_handle = 1;
        g_next_readback_handle = 101;
        g_last_discarded_readback = 0;
        g_readback_ready = false;
        g_gpu_calls.clear();
        TVPGodotGpuBridgeCallbacks callbacks{};
        callbacks.create_rgba = CreateTestTexture;
        callbacks.release_texture = ReleaseTestTexture;
        callbacks.draw_triangles = DrawTestTriangles;
        callbacks.blend_rect = BlendTestRect;
        callbacks.read_rgba = ReadTestGrayTexture;
        callbacks.begin_read_rgba = BeginTestReadback;
        callbacks.poll_read_rgba = PollTestReadback;
        callbacks.discard_read_rgba = DiscardTestReadback;
        callbacks.flush = FlushTestGpu;
        TVPGodotGpuBridgeRegister(&callbacks);
    }

    ~TestGpuBridge() { TVPGodotGpuBridgeRegister(nullptr); }
};

} // namespace

TEST_CASE("script pixel colors preserve RGB channel order") {
    const std::array<std::uint8_t, 4> red_pixel = {0xfe, 0x00, 0x00, 0xff};
    GodotTexture2D texture(red_pixel.data(), 4, 1, 1,
                           TVPTextureFormat::RGBA);

    // RGBA bytes read as a little-endian integer are 0xAABBGGRR.
    CHECK(texture.GetPoint(0, 0) == 0xff0000fe);
    // KiriKiri scripts always observe the documented 0xRRGGBB value.
    CHECK(TVPFromActualColor(texture.GetPoint(0, 0)) == 0x00fe0000);
    CHECK(TVPFromActualColor(0xff332211) == 0x00112233);
    CHECK(TVPToActualColor(0x00112233) == 0x00112233);
}

TEST_CASE("Godot textures expose asynchronous GPU readback") {
    TestGpuBridge bridge;
    std::array<std::uint8_t, 8> pixels{};
    GodotTexture2D texture(pixels.data(), 8, 2, 1,
                           TVPTextureFormat::RGBA);
    REQUIRE(texture.EnsureGpuHandle());

    const uint64_t request = texture.BeginGpuReadback();
    REQUIRE(request == 101);
    bool ready = true;
    CHECK(texture.PollGpuReadback(request, pixels.data(), pixels.size(), 8,
                                  &ready));
    CHECK_FALSE(ready);

    g_readback_ready = true;
    REQUIRE(texture.PollGpuReadback(request, pixels.data(), pixels.size(), 8,
                                    &ready));
    CHECK(ready);
    CHECK(pixels == std::array<std::uint8_t, 8>{1, 2, 3, 4, 5, 6, 7, 8});

    texture.DiscardGpuReadback(request);
    CHECK(g_last_discarded_readback == request);
}

TEST_CASE("Godot nearest scaled alpha uses the software sampler") {
    TestGpuBridge bridge;
    std::array<std::uint8_t, 16> source_pixels = {
        0, 0, 0, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 0, 0, 0, 255,
    };
    std::vector<std::uint8_t> destination_pixels(256 * 256 * 4);
    GodotTexture2D src(source_pixels.data(), 8, 2, 2,
                       TVPTextureFormat::RGBA);
    GodotTexture2D dst(destination_pixels.data(), 256 * 4, 256, 256,
                       TVPTextureFormat::RGBA);
    GodotRenderManager manager;

    const int stretch = manager.EnumParameterID("StretchType");
    REQUIRE(stretch >= 0);
    iTVPRenderMethod *method = manager.GetRenderMethod("AlphaBlend_d");
    tRenderTexRectArray::Element source_element(
        &src, tTVPRect(0, 0, 2, 2));

    manager.SetParameterInt(stretch, stNearest);
    manager.OperateRect(
        method, &dst, &dst, tTVPRect(0, 0, 256, 256),
        tRenderTexRectArray(&source_element, 1));
    CHECK(g_blend_rect_calls == 0);

    manager.SetParameterInt(stretch, stLinear);
    manager.OperateRect(
        method, &dst, &dst, tTVPRect(0, 0, 256, 256),
        tRenderTexRectArray(&source_element, 1));
    CHECK(g_blend_rect_calls == 1);
}

TEST_CASE("Godot deferred GPU drain keeps alpha blends in the frame batch") {
    TestGpuBridge bridge;
    std::vector<std::uint8_t> pixels(256u * 256u * 4u, 0xffu);
    GodotTexture2D src(pixels.data(), 256 * 4, 256, 256,
                       TVPTextureFormat::RGBA);
    GodotTexture2D dst(pixels.data(), 256 * 4, 256, 256,
                       TVPTextureFormat::RGBA);
    REQUIRE(src.EnsureGpuHandle());
    REQUIRE(dst.EnsureGpuHandle());
    src.MarkGpuDirty();

    GodotRenderManager manager;
    iTVPRenderMethod *method = manager.GetRenderMethod("AlphaBlend_d");
    tRenderTexRectArray::Element source_element(
        &src, tTVPRect(0, 0, 256, 256));

    manager.OperateRect(
        method, &dst, &dst, tTVPRect(0, 0, 256, 256),
        tRenderTexRectArray(&source_element, 1));

    REQUIRE(g_gpu_calls.size() == 1);
    CHECK(g_gpu_calls[0] == GpuCall::Blend);
}

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

TEST_CASE("Godot DirectCopy preserves Gray texture byte addressing") {
    std::array<std::uint8_t, 8> source_pixels = {
        10, 11, 12, 13, 14, 15, 16, 17,
    };
    std::array<std::uint8_t, 8> destination_pixels = {
        90, 91, 92, 93, 94, 95, 96, 97,
    };
    GodotTexture2D src(source_pixels.data(), 8, 8, 1,
                       TVPTextureFormat::Gray);
    GodotTexture2D dst(destination_pixels.data(), 8, 8, 1,
                       TVPTextureFormat::Gray);
    GodotRenderManager manager;
    iTVPRenderMethod *method = manager.GetRenderMethod("Copy");
    REQUIRE(method != nullptr);
    tRenderTexRectArray::Element source_element(
        &src, tTVPRect(1, 0, 5, 1));

    manager.OperateRect(
        method, &dst, &dst, tTVPRect(2, 0, 6, 1),
        tRenderTexRectArray(&source_element, 1));

    const auto *row = static_cast<const std::uint8_t *>(
        dst.GetScanLineForRead(0));
    REQUIRE(row != nullptr);
    CHECK(std::array<std::uint8_t, 8>{
              row[0], row[1], row[2], row[3],
              row[4], row[5], row[6], row[7],
          } == std::array<std::uint8_t, 8>{
              90, 91, 11, 12, 13, 14, 96, 97,
          });
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

TEST_CASE("Godot uniform textures resize without materializing old pixels") {
    GodotTexture2D texture(nullptr, 0, 2, 2, TVPTextureFormat::RGBA);
    const uint32_t color = 0xff563412u;

    REQUIRE(texture.SetUniformColor(color, tTVPRect(0, 0, 2, 2)));
    REQUIRE(texture.TrySetSizeWithFill(1920, 1080, color));

    CHECK(texture.GetWidth() == 1920);
    CHECK(texture.GetHeight() == 1080);
    CHECK(texture.GetPoint(0, 0) == color);
    CHECK(texture.GetPoint(1919, 1079) == color);
}

TEST_CASE("Godot uniform resize fast path rejects modified or live GPU textures") {
    GodotTexture2D modified(nullptr, 0, 2, 2, TVPTextureFormat::RGBA);
    REQUIRE(modified.SetUniformColor(0xff000000u,
                                     tTVPRect(0, 0, 2, 2)));
    modified.SetPoint(0, 0, 0xffffffffu);
    CHECK_FALSE(modified.TrySetSizeWithFill(4, 4, 0xff000000u));

    TestGpuBridge bridge;
    GodotTexture2D uploaded(nullptr, 0, 2, 2, TVPTextureFormat::RGBA);
    REQUIRE(uploaded.EnsureGpuHandle());
    CHECK_FALSE(uploaded.SetUniformColor(0xff000000u,
                                         tTVPRect(0, 0, 2, 2)));
    CHECK_FALSE(uploaded.TrySetSizeWithFill(4, 4, 0u));
}

TEST_CASE("Godot uniform clones preserve crop and transparent growth semantics") {
    GodotRenderManager manager;
    GodotTexture2D source(nullptr, 0, 4, 4, TVPTextureFormat::RGBA);
    const uint32_t color = 0xff563412u;
    REQUIRE(source.SetUniformColor(color, tTVPRect(0, 0, 4, 4)));

    iTVPTexture2D *cropped = manager.CreateTexture2D(2, 2, &source);
    REQUIRE(cropped != nullptr);
    CHECK(cropped->GetPoint(1, 1) == color);
    delete cropped;

    iTVPTexture2D *expanded = manager.CreateTexture2D(6, 6, &source);
    REQUIRE(expanded != nullptr);
    CHECK(expanded->GetPoint(3, 3) == color);
    CHECK(expanded->GetPoint(5, 5) == 0u);
    delete expanded;
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
