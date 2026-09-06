#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "godot/GodotGpuBridge.h"
#include "godot/GodotRenderManager.h"
#include "LayerBitmapIntf.h"
#include "LayerIntf.h"
#include "motionplayer/D3DAdaptor.h"
#include "../../../bridge/godot_extension/src/GodotGpuBarrierShadowPlanner.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
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
    BatchBegin,
    BatchEnd,
};

TriangleDrawCall g_triangle_draw_call;
int g_blend_rect_calls = 0;
int g_read_rgba_calls = 0;
int g_update_rgba_calls = 0;
uint32_t g_last_blend_mode = 0;
uint32_t g_last_blend_color = 0;
int g_last_blend_opacity = 0;
uint64_t g_next_texture_handle = 1;
uint64_t g_next_readback_handle = 101;
uint64_t g_last_discarded_readback = 0;
bool g_last_create_pixels_were_null = false;
bool g_readback_ready = false;
bool g_reject_batch = false;
bool g_throw_batch_end = false;
uint64_t g_last_batch_token = 0;
std::vector<GpuCall> g_gpu_calls;

uint64_t CreateTestTexture(uint32_t, uint32_t, const void *pixels, uint32_t) {
    g_last_create_pixels_were_null = pixels == nullptr;
    return g_next_texture_handle++;
}

void ReleaseTestTexture(uint64_t) {}

bool ReadTestGrayTexture(uint64_t, void *out_pixels, size_t out_pixels_size,
                         uint32_t stride_bytes) {
    ++g_read_rgba_calls;
    if(out_pixels == nullptr || out_pixels_size < 24 || stride_bytes != 12)
        return false;

    const std::array<std::uint8_t, 24> rgba = {
        11, 11, 11, 255, 22, 22, 22, 255, 33, 33, 33, 255,
        44, 44, 44, 255, 55, 55, 55, 255, 66, 66, 66, 255,
    };
    std::memcpy(out_pixels, rgba.data(), rgba.size());
    return true;
}

bool UpdateTestTexture(uint64_t, const void *, uint32_t,
                       const tTVPRect *) {
    ++g_update_rgba_calls;
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
                   uint32_t mode, int opacity, uint32_t color) {
    g_gpu_calls.push_back(GpuCall::Blend);
    ++g_blend_rect_calls;
    g_last_blend_mode = mode;
    g_last_blend_color = color;
    g_last_blend_opacity = opacity;
    return true;
}

bool FlushTestGpu() {
    g_gpu_calls.push_back(GpuCall::Flush);
    return true;
}

uint64_t BeginTestGpuBatch() {
    g_gpu_calls.push_back(GpuCall::BatchBegin);
    return g_reject_batch ? 0 : 0xabcdu;
}

bool EndTestGpuBatch(uint64_t token) {
    g_gpu_calls.push_back(GpuCall::BatchEnd);
    g_last_batch_token = token;
    if(g_throw_batch_end) {
        throw std::runtime_error("test GPU batch end failure");
    }
    return token == 0xabcdu;
}

class TestGpuBridge {
public:
    TestGpuBridge() {
        g_triangle_draw_call = {};
        g_blend_rect_calls = 0;
        g_read_rgba_calls = 0;
        g_update_rgba_calls = 0;
        g_last_blend_mode = 0;
        g_last_blend_color = 0;
        g_last_blend_opacity = 0;
        g_next_texture_handle = 1;
        g_next_readback_handle = 101;
        g_last_discarded_readback = 0;
        g_last_create_pixels_were_null = false;
        g_readback_ready = false;
        g_reject_batch = false;
        g_throw_batch_end = false;
        g_last_batch_token = 0;
        g_gpu_calls.clear();
        TVPGodotGpuBridgeCallbacks callbacks{};
        callbacks.create_rgba = CreateTestTexture;
        callbacks.release_texture = ReleaseTestTexture;
        callbacks.update_rgba = UpdateTestTexture;
        callbacks.draw_triangles = DrawTestTriangles;
        callbacks.blend_rect = BlendTestRect;
        callbacks.read_rgba = ReadTestGrayTexture;
        callbacks.begin_read_rgba = BeginTestReadback;
        callbacks.poll_read_rgba = PollTestReadback;
        callbacks.discard_read_rgba = DiscardTestReadback;
        callbacks.flush = FlushTestGpu;
        TVPGodotGpuBridgeRegister(&callbacks);
        TVPGodotGpuBatchCallbacks batch_callbacks{};
        batch_callbacks.struct_size = sizeof(batch_callbacks);
        batch_callbacks.abi_version =
            TVP_GODOT_GPU_BATCH_CALLBACKS_ABI_VERSION;
        batch_callbacks.begin_batch = BeginTestGpuBatch;
        batch_callbacks.end_batch = EndTestGpuBatch;
        TVPGodotGpuBatchRegister(&batch_callbacks);
    }

    ~TestGpuBridge() {
        TVPGodotGpuBatchRegister(nullptr);
        TVPGodotGpuBridgeRegister(nullptr);
    }
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

TEST_CASE("Godot GPU barrier shadow leaves independent resources unordered") {
    GodotGpuBarrierShadowPlanner planner;

    CHECK_FALSE(planner.record({1}, {2}).barrier);
    CHECK_FALSE(planner.record({3}, {4}).barrier);
    CHECK(planner.counters().barriers == 0);

    CHECK(planner.finish());
    CHECK(planner.counters().barriers == 1);
    CHECK(planner.counters().raw == 0);
    CHECK(planner.counters().waw == 0);
    CHECK(planner.counters().war == 0);
}

TEST_CASE("Godot GPU barrier shadow classifies whole-RID hazards") {
    SECTION("read after write") {
        GodotGpuBarrierShadowPlanner planner;
        CHECK_FALSE(planner.record({}, {10}).barrier);
        const auto step = planner.record({10}, {});
        CHECK(step.barrier);
        CHECK(step.raw);
        CHECK_FALSE(step.waw);
        CHECK_FALSE(step.war);
        CHECK(planner.counters().raw == 1);
    }

    SECTION("write after write") {
        GodotGpuBarrierShadowPlanner planner;
        CHECK_FALSE(planner.record({}, {10}).barrier);
        const auto step = planner.record({}, {10});
        CHECK(step.barrier);
        CHECK_FALSE(step.raw);
        CHECK(step.waw);
        CHECK_FALSE(step.war);
        CHECK(planner.counters().waw == 1);
    }

    SECTION("write after read") {
        GodotGpuBarrierShadowPlanner planner;
        CHECK_FALSE(planner.record({10}, {}).barrier);
        const auto step = planner.record({}, {10});
        CHECK(step.barrier);
        CHECK_FALSE(step.raw);
        CHECK_FALSE(step.waw);
        CHECK(step.war);
        CHECK(planner.counters().war == 1);
    }
}

TEST_CASE("Godot GPU barrier shadow treats fill as write-only") {
    GodotGpuBarrierShadowPlanner planner;

    // FILL_ARGB contributes only its destination to the write set.
    CHECK_FALSE(planner.record({}, {21}).barrier);
    const auto step = planner.record({}, {21});
    CHECK(step.barrier);
    CHECK(step.waw);
    CHECK_FALSE(step.war);
}

TEST_CASE("Godot GPU barrier shadow resets and finishes epochs") {
    GodotGpuBarrierShadowPlanner planner;

    CHECK_FALSE(planner.record({31}, {}).barrier);
    CHECK_FALSE(planner.finish());
    CHECK_FALSE(planner.has_pending_reads());
    CHECK_FALSE(planner.has_pending_writes());

    CHECK_FALSE(planner.record({}, {32}).barrier);
    CHECK(planner.finish());
    CHECK(planner.counters().barriers == 1);

    CHECK_FALSE(planner.record({}, {33}).barrier);
    planner.reset();
    CHECK_FALSE(planner.record({33}, {}).barrier);
    CHECK_FALSE(planner.finish());
    CHECK(planner.counters().barriers == 1);
}

TEST_CASE("Godot GPU barrier planner separates Live2D epochs") {
    GodotGpuBarrierShadowPlanner planner;

    CHECK_FALSE(planner.record({41}, {42}).barrier);
    CHECK_FALSE(planner.record({43}, {44}).barrier);

    // Entering Live2D finishes the non-Live2D epoch. Its pending write needs
    // one final barrier, and Live2D accesses are intentionally not recorded.
    CHECK(planner.finish());
    CHECK(planner.counters().barriers == 1);

    // A new non-Live2D epoch may reuse an earlier RID without another hazard
    // barrier because the boundary barrier already made that write visible.
    CHECK_FALSE(planner.record({42}, {45}).barrier);

    // Ending the compute list finishes the new epoch exactly once.
    CHECK(planner.finish());
    CHECK_FALSE(planner.finish());
    CHECK(planner.counters().barriers == 2);
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

TEST_CASE("Godot textures skip zero-filled RGBA staging uploads") {
    TestGpuBridge bridge;

    GodotTexture2D empty_rgba(nullptr, 0, 8, 8, TVPTextureFormat::RGBA);
    REQUIRE(empty_rgba.EnsureGpuHandle());
    CHECK(g_last_create_pixels_were_null);

    const std::array<std::uint8_t, 4> initialized_pixel = {1, 2, 3, 4};
    GodotTexture2D initialized_rgba(initialized_pixel.data(), 4, 1, 1,
                                    TVPTextureFormat::RGBA);
    REQUIRE(initialized_rgba.EnsureGpuHandle());
    CHECK_FALSE(g_last_create_pixels_were_null);

    GodotTexture2D empty_gray(nullptr, 0, 1, 1, TVPTextureFormat::Gray);
    REQUIRE(empty_gray.EnsureGpuHandle());
    CHECK_FALSE(g_last_create_pixels_were_null);
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

TEST_CASE("Godot immediate GPU drain flushes pending alpha sources") {
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

    REQUIRE(g_gpu_calls.size() == 2);
    CHECK(g_gpu_calls[0] == GpuCall::Flush);
    CHECK(g_gpu_calls[1] == GpuCall::Blend);
}

TEST_CASE("Godot GPU batch scope defers pending alpha source flushes") {
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

    {
        TVPGodotGpuBatchScope batch;
        REQUIRE(batch.active());
        CHECK(TVPGodotGpuBridgeBatchActive());
        manager.OperateRect(
            method, &dst, &dst, tTVPRect(0, 0, 256, 256),
            tRenderTexRectArray(&source_element, 1));
        REQUIRE(g_gpu_calls.size() == 2);
        CHECK(g_gpu_calls[0] == GpuCall::BatchBegin);
        CHECK(g_gpu_calls[1] == GpuCall::Blend);
    }

    CHECK_FALSE(TVPGodotGpuBridgeBatchActive());
    REQUIRE(g_gpu_calls.size() == 3);
    CHECK(g_gpu_calls[2] == GpuCall::BatchEnd);
    CHECK(g_last_batch_token == 0xabcdu);
}

TEST_CASE("Godot GPU batch scope finishes exactly once") {
    TestGpuBridge bridge;
    TVPGodotGpuBatchScope batch;
    REQUIRE(batch.active());
    REQUIRE(batch.finish());
    REQUIRE(batch.finish());
    REQUIRE(g_gpu_calls.size() == 2);
    CHECK(g_gpu_calls[0] == GpuCall::BatchBegin);
    CHECK(g_gpu_calls[1] == GpuCall::BatchEnd);
}

TEST_CASE("Godot GPU batch scope destructor contains host exceptions") {
    TestGpuBridge bridge;
    g_throw_batch_end = true;
    {
        TVPGodotGpuBatchScope batch;
        REQUIRE(batch.active());
        CHECK(TVPGodotGpuBridgeBatchActive());
    }
    CHECK_FALSE(TVPGodotGpuBridgeBatchActive());
    REQUIRE(g_gpu_calls.size() == 2);
    CHECK(g_gpu_calls[0] == GpuCall::BatchBegin);
    CHECK(g_gpu_calls[1] == GpuCall::BatchEnd);
}

TEST_CASE("Godot GPU batch scope remains active across nesting") {
    TestGpuBridge bridge;
    {
        TVPGodotGpuBatchScope outer;
        REQUIRE(outer.active());
        {
            TVPGodotGpuBatchScope inner;
            REQUIRE(inner.active());
            CHECK(TVPGodotGpuBridgeBatchActive());
        }
        CHECK(TVPGodotGpuBridgeBatchActive());
    }
    CHECK_FALSE(TVPGodotGpuBridgeBatchActive());
    REQUIRE(g_gpu_calls.size() == 4);
    CHECK(g_gpu_calls[0] == GpuCall::BatchBegin);
    CHECK(g_gpu_calls[1] == GpuCall::BatchBegin);
    CHECK(g_gpu_calls[2] == GpuCall::BatchEnd);
    CHECK(g_gpu_calls[3] == GpuCall::BatchEnd);
}

TEST_CASE("Godot GPU batch scope falls back when unavailable or rejected") {
    SECTION("callbacks unavailable") {
        TVPGodotGpuBatchRegister(nullptr);
        TVPGodotGpuBridgeRegister(nullptr);
        TVPGodotGpuBatchScope batch;
        CHECK_FALSE(batch.active());
        CHECK(batch.finish());
    }

    SECTION("undersized callback table is rejected") {
        TVPGodotGpuBatchCallbacks callbacks{};
        callbacks.struct_size = sizeof(callbacks) - 1;
        callbacks.abi_version =
            TVP_GODOT_GPU_BATCH_CALLBACKS_ABI_VERSION;
        callbacks.begin_batch = BeginTestGpuBatch;
        callbacks.end_batch = EndTestGpuBatch;
        TVPGodotGpuBatchRegister(&callbacks);
        TVPGodotGpuBatchScope batch;
        CHECK_FALSE(batch.active());
        CHECK(batch.finish());
        TVPGodotGpuBatchRegister(nullptr);
    }

    SECTION("unknown callback ABI is rejected") {
        TVPGodotGpuBatchCallbacks callbacks{};
        callbacks.struct_size = sizeof(callbacks);
        callbacks.abi_version =
            TVP_GODOT_GPU_BATCH_CALLBACKS_ABI_VERSION + 1;
        callbacks.begin_batch = BeginTestGpuBatch;
        callbacks.end_batch = EndTestGpuBatch;
        TVPGodotGpuBatchRegister(&callbacks);
        TVPGodotGpuBatchScope batch;
        CHECK_FALSE(batch.active());
        CHECK(batch.finish());
        TVPGodotGpuBatchRegister(nullptr);
    }

    SECTION("batch rejected") {
        TestGpuBridge bridge;
        g_reject_batch = true;
        TVPGodotGpuBatchScope batch;
        CHECK_FALSE(batch.active());
        CHECK(batch.finish());
        REQUIRE(g_gpu_calls.size() == 1);
        CHECK(g_gpu_calls[0] == GpuCall::BatchBegin);
    }
}

TEST_CASE("D3D adaptor keeps one GPU transaction open across nested draws") {
    TestGpuBridge bridge;
    motion::D3DAdaptor adaptor;

    REQUIRE(adaptor.beginGpuBatch());
    REQUIRE(adaptor.beginGpuBatch());
    CHECK(TVPGodotGpuBridgeBatchActive());
    {
        TVPGodotGpuBatchScope playerDraw;
        REQUIRE(playerDraw.active());
        CHECK(TVPGodotGpuBridgeBatchActive());
    }
    CHECK(TVPGodotGpuBridgeBatchActive());

    REQUIRE(adaptor.endGpuBatch());
    CHECK(TVPGodotGpuBridgeBatchActive());
    REQUIRE(adaptor.endGpuBatch());
    CHECK_FALSE(TVPGodotGpuBridgeBatchActive());
    CHECK(adaptor.endGpuBatch());

    REQUIRE(g_gpu_calls.size() == 4);
    CHECK(g_gpu_calls[0] == GpuCall::BatchBegin);
    CHECK(g_gpu_calls[1] == GpuCall::BatchBegin);
    CHECK(g_gpu_calls[2] == GpuCall::BatchEnd);
    CHECK(g_gpu_calls[3] == GpuCall::BatchEnd);
}

TEST_CASE("D3D adaptor closes an unfinished GPU transaction on destruction") {
    TestGpuBridge bridge;
    {
        motion::D3DAdaptor adaptor;
        REQUIRE(adaptor.beginGpuBatch());
        CHECK(TVPGodotGpuBridgeBatchActive());
    }

    CHECK_FALSE(TVPGodotGpuBridgeBatchActive());
    REQUIRE(g_gpu_calls.size() == 2);
    CHECK(g_gpu_calls[0] == GpuCall::BatchBegin);
    CHECK(g_gpu_calls[1] == GpuCall::BatchEnd);
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

TEST_CASE("Godot textures retain CPU shadows after script writes") {
    TestGpuBridge bridge;
    const std::array<std::uint8_t, 8> pixels = {
        1, 2, 3, 255, 4, 5, 6, 255,
    };
    GodotTexture2D texture(pixels.data(), 8, 2, 1,
                           TVPTextureFormat::RGBA);
    REQUIRE(texture.EnsureGpuHandle());

    auto *first = static_cast<std::uint8_t *>(texture.GetScanLineForWrite(0));
    REQUIRE(first != nullptr);
    const int reads_after_first_write = g_read_rgba_calls;
    first[0] = 9;
    REQUIRE(texture.UploadCpuToGpu());
    REQUIRE(g_update_rgba_calls == 1);

    auto *second = static_cast<std::uint8_t *>(texture.GetScanLineForWrite(0));
    REQUIRE(second != nullptr);
    CHECK(g_read_rgba_calls == reads_after_first_write);
    CHECK(second[0] == 9);
}

TEST_CASE("Godot Gray textures update reused GPU glyph masks") {
    TestGpuBridge bridge;
    const std::array<std::uint8_t, 6> pixels = {1, 2, 3, 4, 5, 6};
    GodotTexture2D texture(pixels.data(), 3, 3, 2,
                           TVPTextureFormat::Gray);
    REQUIRE(texture.EnsureGpuHandle());

    auto *row = static_cast<std::uint8_t *>(texture.GetScanLineForWrite(0));
    REQUIRE(row != nullptr);
    row[1] = 99;
    REQUIRE(texture.EnsureGpuHandle());

    CHECK(g_update_rgba_calls == 1);
    const auto *retained = static_cast<const std::uint8_t *>(
        texture.GetScanLineForRead(0));
    REQUIRE(retained != nullptr);
    CHECK(retained[1] == 99);
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

TEST_CASE("Godot render manager applies glyph color maps on the GPU") {
    // ApplyColorMap_a is intentionally opt-in in production because KAG text
    // rendering emits thousands of tiny rectangles. The test host explicitly
    // enables this route so its GPU callback contract remains covered.
    TestGpuBridge bridge;
    const std::array<std::uint8_t, 8> mask = {0, 64, 128, 255, 0, 0, 0, 0};
    const std::array<std::uint8_t, 16> destination = {
        1, 2, 3, 4, 5, 6, 7, 8,
        9, 10, 11, 12, 13, 14, 15, 16,
    };
    GodotTexture2D src(mask.data(), 4, 4, 1, TVPTextureFormat::Gray);
    GodotTexture2D dst(destination.data(), 16, 4, 1,
                       TVPTextureFormat::RGBA);
    const tTVPRect rect(0, 0, 4, 1);
    std::pair<iTVPTexture2D *, tTVPRect> source(&src, rect);
    const tRenderTexRectArray textures(&source, 1);
    GodotRenderManager manager;
    iTVPRenderMethod *method = manager.GetRenderMethod("ApplyColorMap_a");
    REQUIRE(method != nullptr);
    method->SetParameterColor4B(method->EnumParameterID("color"),
                                0x00112233u);
    method->SetParameterOpa(method->EnumParameterID("opacity"), 192);

    manager.OperateRect(method, &dst, nullptr, rect, textures);

    CHECK(g_blend_rect_calls == 1);
    CHECK(g_last_blend_mode == TVP_GODOT_GPU_BLEND_APPLY_COLOR_MAP_A);
    CHECK(g_last_blend_color == 0x00112233u);
    CHECK(g_last_blend_opacity == 192);
}

TEST_CASE("Godot render manager composites additive-alpha layers on the GPU") {
    TestGpuBridge bridge;
    const std::array<std::uint8_t, 16> source_pixels = {
        1, 2, 3, 4, 5, 6, 7, 8,
        9, 10, 11, 12, 13, 14, 15, 16,
    };
    const std::array<std::uint8_t, 16> destination_pixels = {
        16, 15, 14, 13, 12, 11, 10, 9,
        8, 7, 6, 5, 4, 3, 2, 1,
    };
    GodotTexture2D src(source_pixels.data(), 16, 4, 1,
                       TVPTextureFormat::RGBA);
    GodotTexture2D dst(destination_pixels.data(), 16, 4, 1,
                       TVPTextureFormat::RGBA);
    GodotRenderMethod method(nullptr);
    method.SetName("AdditiveAlphaBlend");
    method.SetParameterOpa(-1, 192);
    const tTVPRect rect(0, 0, 4, 1);
    std::pair<iTVPTexture2D *, tTVPRect> source(&src, rect);
    const tRenderTexRectArray textures(&source, 1);
    GodotRenderManager manager;

    manager.OperateRect(&method, &dst, nullptr, rect, textures);

    CHECK(g_blend_rect_calls == 1);
    CHECK(g_last_blend_mode == TVP_GODOT_GPU_BLEND_ADDITIVE_ALPHA);
    CHECK(g_last_blend_opacity == 192);
}

TEST_CASE("Godot render manager composites additive-alpha destination layers on the GPU") {
    TestGpuBridge bridge;
    const std::array<std::uint8_t, 16> source_pixels = {
        1, 2, 3, 4, 5, 6, 7, 8,
        9, 10, 11, 12, 13, 14, 15, 16,
    };
    const std::array<std::uint8_t, 16> destination_pixels = {
        16, 15, 14, 13, 12, 11, 10, 9,
        8, 7, 6, 5, 4, 3, 2, 1,
    };
    GodotTexture2D src(source_pixels.data(), 16, 4, 1,
                       TVPTextureFormat::RGBA);
    GodotTexture2D dst(destination_pixels.data(), 16, 4, 1,
                       TVPTextureFormat::RGBA);
    GodotRenderMethod method(nullptr);
    method.SetName("AdditiveAlphaBlend_a");
    method.SetParameterOpa(-1, 192);
    const tTVPRect rect(0, 0, 4, 1);
    std::pair<iTVPTexture2D *, tTVPRect> source(&src, rect);
    const tRenderTexRectArray textures(&source, 1);
    GodotRenderManager manager;

    manager.OperateRect(&method, &dst, nullptr, rect, textures);

    CHECK(g_blend_rect_calls == 1);
    CHECK(g_last_blend_mode == TVP_GODOT_GPU_BLEND_ADDITIVE_ALPHA_A);
    CHECK(g_last_blend_opacity == 192);
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
