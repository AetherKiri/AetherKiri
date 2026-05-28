#include "engine_api.h"
#include "engine_options.h"
#include "GodotGpuBridge.h"
#include "ComplexRect.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_texture_view.hpp>
#include <godot_cpp/classes/rd_shader_source.hpp>
#include <godot_cpp/classes/rd_shader_spirv.hpp>
#include <godot_cpp/classes/rd_uniform.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/texture2drd.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <condition_variable>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace godot {
namespace {

struct GodotGpuTextureRecord {
    RID rid;
    Ref<Texture2DRD> texture;
    uint32_t width = 0;
    uint32_t height = 0;
};

std::mutex g_gpu_textures_mutex;
std::unordered_map<uint64_t, GodotGpuTextureRecord> g_gpu_textures;
uint64_t g_next_gpu_texture_id = 1;

struct GodotGpuOp {
    enum class Type {
        Update,
        Clear,
        Copy,
        CopySelf,
        CopyTriangles,
        Read,
        Blend,
        Blend2,
        Release,
        Flush,
    };

    Type type = Type::Update;
    RID src;
    RID src2;
    RID dst;
    PackedByteArray data;
    std::vector<float> vertices;
    Color clear_color;
    Vector3 src_pos;
    Vector3 src2_pos;
    Vector3 dst_pos;
    Vector3 size;
    Vector3 src_size;
    uint32_t mode = 0;
    int opacity = 255;
    uint32_t color = 0xffffffffu;
    bool result = false;
    bool done = false;
    std::mutex done_mutex;
    std::condition_variable done_cv;
};

std::mutex g_gpu_op_queue_mutex;
std::deque<std::shared_ptr<GodotGpuOp>> g_gpu_op_queue;
bool g_gpu_op_drain_scheduled = false;

struct GodotGpuPipelineState {
    RID blend_shader;
    RID blend_pipeline;
    RID alpha_blend_a_shader;
    RID alpha_blend_a_pipeline;
    RID blend2_shader;
    RID blend2_pipeline;
    RID copy_triangles_shader;
    RID copy_triangles_pipeline;
};

GodotGpuPipelineState *g_gpu_pipeline_state = nullptr;

struct GodotGpuUniformSetKey {
    int64_t shader = 0;
    int64_t rid0 = 0;
    int64_t rid1 = 0;
    int64_t rid2 = 0;
    uint8_t count = 0;

    bool operator==(const GodotGpuUniformSetKey &other) const {
        return shader == other.shader && rid0 == other.rid0 &&
               rid1 == other.rid1 && rid2 == other.rid2 &&
               count == other.count;
    }
};

struct GodotGpuUniformSetKeyHash {
    size_t operator()(const GodotGpuUniformSetKey &key) const {
        size_t h = std::hash<int64_t>{}(key.shader);
        const auto combine = [&h](int64_t value) {
            h ^= std::hash<int64_t>{}(value) + 0x9e3779b97f4a7c15ULL +
                 (h << 6) + (h >> 2);
        };
        combine(key.rid0);
        combine(key.rid1);
        combine(key.rid2);
        h ^= std::hash<int>{}(key.count);
        return h;
    }
};

std::unordered_map<GodotGpuUniformSetKey, RID, GodotGpuUniformSetKeyHash>
    g_gpu_uniform_set_cache;

const char *NormalizeBackend(const String &backend) {
    const String lower = backend.to_lower();
    if (lower == "gpu bridge" || lower == "gpubridge" ||
        lower == ENGINE_RENDERER_GPU_BRIDGE) {
        return ENGINE_RENDERER_GPU_BRIDGE;
    }
    if (lower == "debug cpu" || lower == "debugcpu" ||
        lower == ENGINE_RENDERER_DEBUG_CPU) {
        return ENGINE_RENDERER_DEBUG_CPU;
    }
    return ENGINE_RENDERER_GODOT_NATIVE;
}

String ResultToString(engine_result_t result) {
    switch (result) {
        case ENGINE_RESULT_OK:
            return "OK";
        case ENGINE_RESULT_INVALID_ARGUMENT:
            return "INVALID_ARGUMENT";
        case ENGINE_RESULT_INVALID_STATE:
            return "INVALID_STATE";
        case ENGINE_RESULT_NOT_SUPPORTED:
            return "NOT_SUPPORTED";
        case ENGINE_RESULT_IO_ERROR:
            return "IO_ERROR";
        case ENGINE_RESULT_INTERNAL_ERROR:
            return "INTERNAL_ERROR";
        default:
            return "UNKNOWN";
    }
}

String LastError(engine_handle_t handle) {
    const char *error = engine_get_last_error(handle);
    return error != nullptr ? String::utf8(error) : String();
}

void ForceOpaqueAlpha(PackedByteArray &data, uint32_t stride_bytes,
                      uint32_t width, uint32_t height) {
    if (stride_bytes < width * 4u || width == 0 || height == 0) {
        return;
    }
    uint8_t *pixels = data.ptrw();
    if (pixels == nullptr) {
        return;
    }
    for (uint32_t y = 0; y < height; ++y) {
        uint8_t *row = pixels + static_cast<size_t>(y) * stride_bytes;
        for (uint32_t x = 0; x < width; ++x) {
            row[x * 4u + 3u] = 0xffu;
        }
    }
}

RenderingDevice *MainRenderingDevice() {
    RenderingServer *server = RenderingServer::get_singleton();
    return server != nullptr ? server->get_rendering_device() : nullptr;
}

PackedByteArray PackGpuPushConstants(const GodotGpuOp &op) {
    PackedByteArray data;
    data.resize(48);
    uint8_t *bytes = data.ptrw();
    if (bytes == nullptr) return data;
    const bool dual_source = op.type == GodotGpuOp::Type::Blend2;
    const bool copy_triangles = op.type == GodotGpuOp::Type::CopyTriangles;
    int32_t values[12] = {
        static_cast<int32_t>(op.dst_pos.x),
        static_cast<int32_t>(op.dst_pos.y),
        static_cast<int32_t>(op.src_pos.x),
        static_cast<int32_t>(op.src_pos.y),
        static_cast<int32_t>(op.size.x),
        static_cast<int32_t>(op.size.y),
        static_cast<int32_t>(op.mode),
        static_cast<int32_t>(std::clamp(op.opacity, 0, 255)),
        copy_triangles ? static_cast<int32_t>(op.src_size.x) :
        dual_source ? static_cast<int32_t>(op.src2_pos.x)
                    : static_cast<int32_t>(op.color & 0xffu),
        copy_triangles ? static_cast<int32_t>(op.src_size.y) :
        dual_source ? static_cast<int32_t>(op.src2_pos.y)
                    : static_cast<int32_t>((op.color >> 8) & 0xffu),
        (dual_source || copy_triangles) ? 0 : static_cast<int32_t>((op.color >> 16) & 0xffu),
        (dual_source || copy_triangles) ? 0 : static_cast<int32_t>((op.color >> 24) & 0xffu),
    };
    std::memcpy(bytes, values, sizeof(values));
    return data;
}

bool EnsureBlendPipeline(RenderingDevice *rd) {
    if (rd == nullptr) return false;
    if (g_gpu_pipeline_state == nullptr) {
        g_gpu_pipeline_state = new GodotGpuPipelineState();
    }
    if (g_gpu_pipeline_state->blend_pipeline.is_valid()) return true;

    Ref<RDShaderSource> source;
    source.instantiate();
    source->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);
    source->set_stage_source(
        RenderingDevice::SHADER_STAGE_COMPUTE,
        R"GLSL(#version 450
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(rgba8, set = 0, binding = 0) uniform readonly image2D src_img;
layout(rgba8, set = 0, binding = 1) uniform image2D dst_img;
layout(push_constant, std430) uniform Params {
    ivec4 rect0;
    ivec4 rect1;
    ivec4 color0;
} pc;

uvec4 vec4_to_u8(vec4 value) {
    return uvec4(round(clamp(value, vec4(0.0), vec4(1.0)) * 255.0));
}

uint pack_u8(uvec4 c) {
    return (c.r & 0xffu) |
           ((c.g & 0xffu) << 8) |
           ((c.b & 0xffu) << 16) |
           ((c.a & 0xffu) << 24);
}

vec4 unpack_u8(uint c) {
    return vec4(float(c & 0xffu),
                float((c >> 8) & 0xffu),
                float((c >> 16) & 0xffu),
                float((c >> 24) & 0xffu)) / 255.0;
}

uint alpha_blend_hda_o(uint d, uint s, uint opa) {
    uint sopa = (((s >> 24) & 0xffu) * opa) >> 8;
    int dr = int(d & 0xffu);
    int dg = int((d >> 8) & 0xffu);
    int db = int((d >> 16) & 0xffu);
    int sr = int(s & 0xffu);
    int sg = int((s >> 8) & 0xffu);
    int sb = int((s >> 16) & 0xffu);
    uint r = uint(clamp(dr + (((sr - dr) * int(sopa)) >> 8), 0, 255));
    uint g = uint(clamp(dg + (((sg - dg) * int(sopa)) >> 8), 0, 255));
    uint b = uint(clamp(db + (((sb - db) * int(sopa)) >> 8), 0, 255));
    return (d & 0xff000000u) | r | (g << 8) | (b << 16);
}

uint opacity_on_opacity(uint dest_alpha, uint src_alpha) {
    if (dest_alpha == 0u) {
        return 255u;
    }
    uint denom = dest_alpha * (255u - src_alpha) + 255u * src_alpha;
    if (denom == 0u) {
        return 255u;
    }
    return min((255u * 255u * src_alpha) / denom, 255u);
}

uint negative_mul_alpha(uint dest_alpha, uint src_alpha) {
    return 255u - (((255u - dest_alpha) * (255u - src_alpha)) / 255u);
}

uint alpha_blend_d(uint d, uint s, uint opa) {
    uint effective_alpha = (s >> 24) & 0xffu;
    if (opa == 255u) {
        if (s <= 0x00ffffffu) {
            return d;
        }
        if (s >= 0xff000000u) {
            return s;
        }
        if (d <= 0x00ffffffu) {
            return s;
        }
    } else {
        effective_alpha = (effective_alpha * opa) >> 8;
    }

    uint dest_alpha = (d >> 24) & 0xffu;
    uint blend_alpha = opacity_on_opacity(dest_alpha, effective_alpha);
    uint out_alpha = negative_mul_alpha(dest_alpha, effective_alpha);
    int dr = int(d & 0xffu);
    int dg = int((d >> 8) & 0xffu);
    int db = int((d >> 16) & 0xffu);
    int sr = int(s & 0xffu);
    int sg = int((s >> 8) & 0xffu);
    int sb = int((s >> 16) & 0xffu);
    uint r = uint(clamp(dr + (((sr - dr) * int(blend_alpha)) >> 8), 0, 255));
    uint g = uint(clamp(dg + (((sg - dg) * int(blend_alpha)) >> 8), 0, 255));
    uint b = uint(clamp(db + (((sb - db) * int(blend_alpha)) >> 8), 0, 255));
    return (out_alpha << 24) | r | (g << 8) | (b << 16);
}

uint const_alpha_blend_d(uint d, uint s, uint opa) {
    uint dest_alpha = (d >> 24) & 0xffu;
    uint blend_alpha = opacity_on_opacity(dest_alpha, opa);
    uint out_alpha = negative_mul_alpha(dest_alpha, opa);
    int dr = int(d & 0xffu);
    int dg = int((d >> 8) & 0xffu);
    int db = int((d >> 16) & 0xffu);
    int sr = int(s & 0xffu);
    int sg = int((s >> 8) & 0xffu);
    int sb = int((s >> 16) & 0xffu);
    uint r = uint(clamp(dr + (((sr - dr) * int(blend_alpha)) >> 8), 0, 255));
    uint g = uint(clamp(dg + (((sg - dg) * int(blend_alpha)) >> 8), 0, 255));
    uint b = uint(clamp(db + (((sb - db) * int(blend_alpha)) >> 8), 0, 255));
    return (out_alpha << 24) | r | (g << 8) | (b << 16);
}

uint remove_const_opacity(uint d, uint strength) {
    uint inv_strength = 255u - clamp(strength, 0u, 255u);
    uint a = (((d >> 24) & 0xffu) * inv_strength) >> 8;
    return (d & 0x00ffffffu) | (a << 24);
}

void main() {
    ivec2 local = ivec2(gl_GlobalInvocationID.xy);
    if (local.x >= pc.rect1.x || local.y >= pc.rect1.y) {
        return;
    }

    ivec2 dst_pos = pc.rect0.xy + local;
    ivec2 src_pos = pc.rect0.zw + local;
    uint d = pack_u8(vec4_to_u8(imageLoad(dst_img, dst_pos)));
    uint opa = uint(clamp(pc.rect1.w, 0, 255));
    uint out_color = d;

    if (pc.rect1.z == 1) {
        uint s = pack_u8(vec4_to_u8(imageLoad(src_img, src_pos)));
        out_color = alpha_blend_hda_o(d, s, opa);
    } else if (pc.rect1.z == 2) {
        uint s = pack_u8(vec4_to_u8(imageLoad(src_img, src_pos)));
        out_color = alpha_blend_d(d, s, opa);
    } else if (pc.rect1.z == 3) {
        uint s = pack_u8(vec4_to_u8(imageLoad(src_img, src_pos)));
        out_color = (d & 0xff000000u) + (s & 0x00ffffffu);
    } else if (pc.rect1.z == 10) {
        uint s = pack_u8(vec4_to_u8(imageLoad(src_img, src_pos)));
        out_color = const_alpha_blend_d(d, s, opa);
    } else if (pc.rect1.z == 5) {
        out_color = (uint(pc.color0.x) & 0xffu) |
                    ((uint(pc.color0.y) & 0xffu) << 8) |
                    ((uint(pc.color0.z) & 0xffu) << 16) |
                    ((uint(pc.color0.w) & 0xffu) << 24);
    } else if (pc.rect1.z == 8) {
        out_color = remove_const_opacity(d, opa);
    }

    imageStore(dst_img, dst_pos, unpack_u8(out_color));
}
)GLSL");

    Ref<RDShaderSPIRV> spirv = rd->shader_compile_spirv_from_source(source);
    if (spirv.is_null()) return false;
    const String compile_error =
        spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE);
    if (!compile_error.is_empty()) {
        UtilityFunctions::printerr("Godot GPU blend shader compile error: ",
                                   compile_error);
        return false;
    }
    g_gpu_pipeline_state->blend_shader =
        rd->shader_create_from_spirv(spirv, "AetherKiriBlend");
    if (!g_gpu_pipeline_state->blend_shader.is_valid()) return false;
    g_gpu_pipeline_state->blend_pipeline =
        rd->compute_pipeline_create(g_gpu_pipeline_state->blend_shader);
    return g_gpu_pipeline_state->blend_pipeline.is_valid();
}

bool EnsureAlphaBlendAPipeline(RenderingDevice *rd) {
    if (rd == nullptr) return false;
    if (g_gpu_pipeline_state == nullptr) {
        g_gpu_pipeline_state = new GodotGpuPipelineState();
    }
    if (g_gpu_pipeline_state->alpha_blend_a_pipeline.is_valid()) return true;

    Ref<RDShaderSource> source;
    source.instantiate();
    source->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);
    source->set_stage_source(
        RenderingDevice::SHADER_STAGE_COMPUTE,
        R"GLSL(#version 450
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(rgba8, set = 0, binding = 0) uniform readonly image2D src_img;
layout(rgba8, set = 0, binding = 1) uniform image2D dst_img;
layout(push_constant, std430) uniform Params {
    ivec4 rect0;
    ivec4 rect1;
    ivec4 color0;
} pc;

uvec4 vec4_to_u8(vec4 value) {
    return uvec4(round(clamp(value, vec4(0.0), vec4(1.0)) * 255.0));
}

uint pack_u8(uvec4 c) {
    return (c.r & 0xffu) |
           ((c.g & 0xffu) << 8) |
           ((c.b & 0xffu) << 16) |
           ((c.a & 0xffu) << 24);
}

vec4 unpack_u8(uint c) {
    return vec4(float(c & 0xffu),
                float((c >> 8) & 0xffu),
                float((c >> 16) & 0xffu),
                float((c >> 24) & 0xffu)) / 255.0;
}

uint saturated_add(uint a, uint b) {
    uint tmp = ((a & b) + (((a ^ b) >> 1) & 0x7f7f7f7fu)) & 0x80808080u;
    tmp = (tmp << 1) - (tmp >> 7);
    return (a + b - tmp) | tmp;
}

uint mul_color(uint color, uint fac) {
    return (((((color & 0x00ff00u) * fac) & 0x00ff0000u) +
             (((color & 0xff00ffu) * fac) & 0xff00ff00u)) >> 8);
}

uint alpha_to_additive_alpha(uint c) {
    return mul_color(c, c >> 24) + (c & 0xff000000u);
}

uint add_alpha_blend_a_a(uint d, uint s) {
    uint dopa = d >> 24;
    uint sopa = s >> 24;
    dopa = dopa + sopa - ((dopa * sopa) >> 8);
    dopa -= dopa >> 8;
    sopa ^= 0xffu;
    s &= 0x00ffffffu;
    return (dopa << 24) +
           saturated_add((((d & 0xff00ffu) * sopa >> 8) & 0xff00ffu) +
                         (((d & 0x00ff00u) * sopa >> 8) & 0x00ff00u),
                         s);
}

uint alpha_blend_a_d_o(uint d, uint s, uint opa) {
    if (opa != 255u) {
        s = (s & 0x00ffffffu) + (((((s >> 24) * opa) >> 8) & 0xffu) << 24);
    }
    return add_alpha_blend_a_a(d, alpha_to_additive_alpha(s));
}

void main() {
    ivec2 local = ivec2(gl_GlobalInvocationID.xy);
    if (local.x >= pc.rect1.x || local.y >= pc.rect1.y) {
        return;
    }

    ivec2 dst_pos = pc.rect0.xy + local;
    ivec2 src_pos = pc.rect0.zw + local;
    uint s = pack_u8(vec4_to_u8(imageLoad(src_img, src_pos)));
    uint d = pack_u8(vec4_to_u8(imageLoad(dst_img, dst_pos)));
    uint opa = uint(clamp(pc.rect1.w, 0, 255));
    imageStore(dst_img, dst_pos, unpack_u8(alpha_blend_a_d_o(d, s, opa)));
}
)GLSL");

    Ref<RDShaderSPIRV> spirv = rd->shader_compile_spirv_from_source(source);
    if (spirv.is_null()) return false;
    const String compile_error =
        spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE);
    if (!compile_error.is_empty()) {
        UtilityFunctions::printerr("Godot GPU AlphaBlend_a shader compile error: ",
                                   compile_error);
        return false;
    }
    g_gpu_pipeline_state->alpha_blend_a_shader =
        rd->shader_create_from_spirv(spirv, "AetherKiriAlphaBlendA");
    if (!g_gpu_pipeline_state->alpha_blend_a_shader.is_valid()) return false;
    g_gpu_pipeline_state->alpha_blend_a_pipeline =
        rd->compute_pipeline_create(g_gpu_pipeline_state->alpha_blend_a_shader);
    return g_gpu_pipeline_state->alpha_blend_a_pipeline.is_valid();
}

bool EnsureBlend2Pipeline(RenderingDevice *rd) {
    if (rd == nullptr) return false;
    if (g_gpu_pipeline_state == nullptr) {
        g_gpu_pipeline_state = new GodotGpuPipelineState();
    }
    if (g_gpu_pipeline_state->blend2_pipeline.is_valid()) return true;

    Ref<RDShaderSource> source;
    source.instantiate();
    source->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);
    source->set_stage_source(
        RenderingDevice::SHADER_STAGE_COMPUTE,
        R"GLSL(#version 450
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(rgba8, set = 0, binding = 0) uniform readonly image2D src1_img;
layout(rgba8, set = 0, binding = 1) uniform readonly image2D src2_img;
layout(rgba8, set = 0, binding = 2) uniform image2D dst_img;
layout(push_constant, std430) uniform Params {
    ivec4 rect0;
    ivec4 rect1;
    ivec4 src2_rect;
} pc;

uvec4 vec4_to_u8(vec4 value) {
    return uvec4(round(clamp(value, vec4(0.0), vec4(1.0)) * 255.0));
}

uint pack_u8(uvec4 c) {
    return (c.r & 0xffu) |
           ((c.g & 0xffu) << 8) |
           ((c.b & 0xffu) << 16) |
           ((c.a & 0xffu) << 24);
}

vec4 unpack_u8(uint c) {
    return vec4(float(c & 0xffu),
                float((c >> 8) & 0xffu),
                float((c >> 16) & 0xffu),
                float((c >> 24) & 0xffu)) / 255.0;
}

uint opacity_on_opacity(uint dest_alpha, uint src_alpha) {
    if (dest_alpha == 0u) {
        return 255u;
    }
    uint denom = dest_alpha * (255u - src_alpha) + 255u * src_alpha;
    if (denom == 0u) {
        return 255u;
    }
    return min((255u * 255u * src_alpha) / denom, 255u);
}

uint const_alpha_blend_sd(uint s1, uint s2, uint opa) {
    uint s1_rb = s1 & 0xff00ffu;
    s1_rb = (s1_rb + (((s2 & 0xff00ffu) - s1_rb) * opa >> 8)) & 0xff00ffu;
    uint s1_g = s1 & 0xff00u;
    uint s2_g = s2 & 0xff00u;
    return s1_rb | ((s1_g + ((s2_g - s1_g) * opa >> 8)) & 0xff00u);
}

uint const_alpha_blend_sd_d(uint s1, uint s2, uint opa_in) {
    uint opa = opa_in;
    if (opa > 127u) {
        opa += 1u;
    }
    uint iopa = 256u - opa;
    uint a1 = s1 >> 24;
    uint a2 = s2 >> 24;
    uint alpha = opacity_on_opacity((a1 * iopa) >> 8, (a2 * opa) >> 8);
    uint s1_rb = s1 & 0xff00ffu;
    s1_rb = (s1_rb + (((s2 & 0xff00ffu) - s1_rb) * alpha >> 8)) & 0xff00ffu;
    uint s1_g = s1 & 0xff00u;
    uint s2_g = s2 & 0xff00u;
    s1_rb |= (a1 + ((a2 - a1) * opa >> 8)) << 24;
    return s1_rb | ((s1_g + ((s2_g - s1_g) * alpha >> 8)) & 0xff00u);
}

void main() {
    ivec2 local = ivec2(gl_GlobalInvocationID.xy);
    if (local.x >= pc.rect1.x || local.y >= pc.rect1.y) {
        return;
    }

    ivec2 dst_pos = pc.rect0.xy + local;
    ivec2 src1_pos = pc.rect0.zw + local;
    ivec2 src2_pos = pc.src2_rect.xy + local;
    uint s1 = pack_u8(vec4_to_u8(imageLoad(src1_img, src1_pos)));
    uint s2 = pack_u8(vec4_to_u8(imageLoad(src2_img, src2_pos)));
    uint opa = uint(clamp(pc.rect1.w, 0, 255));
    uint out_color = s2;

    if (pc.rect1.z == 4) {
        out_color = const_alpha_blend_sd(s1, s2, opa);
    } else if (pc.rect1.z == 9) {
        out_color = const_alpha_blend_sd_d(s1, s2, opa);
    }

    imageStore(dst_img, dst_pos, unpack_u8(out_color));
}
)GLSL");

    Ref<RDShaderSPIRV> spirv = rd->shader_compile_spirv_from_source(source);
    if (spirv.is_null()) return false;
    const String compile_error =
        spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE);
    if (!compile_error.is_empty()) {
        UtilityFunctions::printerr("Godot GPU blend2 shader compile error: ",
                                   compile_error);
        return false;
    }
    g_gpu_pipeline_state->blend2_shader =
        rd->shader_create_from_spirv(spirv, "AetherKiriBlend2");
    if (!g_gpu_pipeline_state->blend2_shader.is_valid()) return false;
    g_gpu_pipeline_state->blend2_pipeline =
        rd->compute_pipeline_create(g_gpu_pipeline_state->blend2_shader);
    return g_gpu_pipeline_state->blend2_pipeline.is_valid();
}

bool EnsureCopyTrianglesPipeline(RenderingDevice *rd) {
    if (rd == nullptr) return false;
    if (g_gpu_pipeline_state == nullptr) {
        g_gpu_pipeline_state = new GodotGpuPipelineState();
    }
    if (g_gpu_pipeline_state->copy_triangles_pipeline.is_valid()) return true;

    Ref<RDShaderSource> source;
    source.instantiate();
    source->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);
    source->set_stage_source(
        RenderingDevice::SHADER_STAGE_COMPUTE,
        R"GLSL(#version 450
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(std430, set = 0, binding = 0) readonly buffer Vertices {
    vec4 vertex[];
} vertices;
layout(rgba8, set = 0, binding = 1) uniform readonly image2D src_img;
layout(rgba8, set = 0, binding = 2) uniform image2D dst_img;
layout(push_constant, std430) uniform Params {
    ivec4 rect0;
    ivec4 rect1;
    ivec4 color0;
} pc;

float edge(vec2 a, vec2 b, vec2 p) {
    return (p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x);
}

void main() {
    ivec2 local = ivec2(gl_GlobalInvocationID.xy);
    if (local.x >= pc.rect1.x || local.y >= pc.rect1.y) {
        return;
    }

    ivec2 dst_pos = pc.rect0.xy + local;
    vec2 p = vec2(dst_pos) + vec2(0.5);
    int tri_count = pc.rect1.z;
    ivec2 src_limit = max(pc.color0.xy - ivec2(1), ivec2(0));

    for (int tri = 0; tri < tri_count; ++tri) {
        vec4 v0 = vertices.vertex[tri * 3 + 0];
        vec4 v1 = vertices.vertex[tri * 3 + 1];
        vec4 v2 = vertices.vertex[tri * 3 + 2];
        vec2 d0 = v0.xy;
        vec2 d1 = v1.xy;
        vec2 d2 = v2.xy;
        float area = edge(d0, d1, d2);
        if (abs(area) < 0.00001) {
            continue;
        }
        float w0 = edge(d1, d2, p) / area;
        float w1 = edge(d2, d0, p) / area;
        float w2 = edge(d0, d1, p) / area;
        if (w0 >= -0.0001 && w1 >= -0.0001 && w2 >= -0.0001) {
            vec2 src_pos_f = v0.zw * w0 + v1.zw * w1 + v2.zw * w2;
            ivec2 src_pos = clamp(ivec2(floor(src_pos_f)), ivec2(0), src_limit);
            imageStore(dst_img, dst_pos, imageLoad(src_img, src_pos));
            return;
        }
    }
}
)GLSL");

    Ref<RDShaderSPIRV> spirv = rd->shader_compile_spirv_from_source(source);
    if (spirv.is_null()) return false;
    const String compile_error =
        spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE);
    if (!compile_error.is_empty()) {
        UtilityFunctions::printerr("Godot GPU copy triangles shader compile error: ",
                                   compile_error);
        return false;
    }
    g_gpu_pipeline_state->copy_triangles_shader =
        rd->shader_create_from_spirv(spirv, "AetherKiriCopyTriangles");
    if (!g_gpu_pipeline_state->copy_triangles_shader.is_valid()) return false;
    g_gpu_pipeline_state->copy_triangles_pipeline =
        rd->compute_pipeline_create(g_gpu_pipeline_state->copy_triangles_shader);
    return g_gpu_pipeline_state->copy_triangles_pipeline.is_valid();
}

void ClearGodotGpuUniformSetCache(RenderingDevice *rd) {
    if (rd != nullptr) {
        for (const auto &entry : g_gpu_uniform_set_cache) {
            if (entry.second.is_valid()) {
                rd->free_rid(entry.second);
            }
        }
    }
    g_gpu_uniform_set_cache.clear();
}

RID GetCachedBlendUniformSet(RenderingDevice *rd, const RID &shader,
                             const RID &src, const RID &dst) {
    const GodotGpuUniformSetKey key{
        shader.get_id(), src.get_id(), dst.get_id(), 0, 2};
    auto it = g_gpu_uniform_set_cache.find(key);
    if (it != g_gpu_uniform_set_cache.end() && it->second.is_valid()) {
        return it->second;
    }

    Ref<RDUniform> src_uniform;
    src_uniform.instantiate();
    src_uniform->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
    src_uniform->set_binding(0);
    src_uniform->add_id(src);

    Ref<RDUniform> dst_uniform;
    dst_uniform.instantiate();
    dst_uniform->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
    dst_uniform->set_binding(1);
    dst_uniform->add_id(dst);

    TypedArray<RDUniform> uniforms;
    uniforms.push_back(src_uniform);
    uniforms.push_back(dst_uniform);
    RID uniform_set = rd->uniform_set_create(uniforms, shader, 0);
    if (uniform_set.is_valid()) {
        g_gpu_uniform_set_cache[key] = uniform_set;
    }
    return uniform_set;
}

RID GetCachedBlend2UniformSet(RenderingDevice *rd, const RID &shader,
                              const RID &src1, const RID &src2,
                              const RID &dst) {
    const GodotGpuUniformSetKey key{
        shader.get_id(), src1.get_id(), src2.get_id(), dst.get_id(), 3};
    auto it = g_gpu_uniform_set_cache.find(key);
    if (it != g_gpu_uniform_set_cache.end() && it->second.is_valid()) {
        return it->second;
    }

    Ref<RDUniform> src1_uniform;
    src1_uniform.instantiate();
    src1_uniform->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
    src1_uniform->set_binding(0);
    src1_uniform->add_id(src1);

    Ref<RDUniform> src2_uniform;
    src2_uniform.instantiate();
    src2_uniform->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
    src2_uniform->set_binding(1);
    src2_uniform->add_id(src2);

    Ref<RDUniform> dst_uniform;
    dst_uniform.instantiate();
    dst_uniform->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
    dst_uniform->set_binding(2);
    dst_uniform->add_id(dst);

    TypedArray<RDUniform> uniforms;
    uniforms.push_back(src1_uniform);
    uniforms.push_back(src2_uniform);
    uniforms.push_back(dst_uniform);
    RID uniform_set = rd->uniform_set_create(uniforms, shader, 0);
    if (uniform_set.is_valid()) {
        g_gpu_uniform_set_cache[key] = uniform_set;
    }
    return uniform_set;
}

bool DispatchGodotGpuBlend(RenderingDevice *rd,
                           const std::shared_ptr<GodotGpuOp> &op,
                           int64_t compute_list,
                           std::vector<RID> &uniform_sets) {
    const bool alpha_blend_a = op->mode == TVP_GODOT_GPU_BLEND_ALPHA_BLEND_A;
    if (alpha_blend_a) {
        if (!EnsureAlphaBlendAPipeline(rd)) return false;
    } else if (!EnsureBlendPipeline(rd)) {
        return false;
    }

    RID uniform_set = GetCachedBlendUniformSet(
        rd,
        alpha_blend_a ? g_gpu_pipeline_state->alpha_blend_a_shader
                      : g_gpu_pipeline_state->blend_shader,
        op->src, op->dst);
    if (!uniform_set.is_valid()) return false;
    (void)uniform_sets;

    const PackedByteArray push_constants = PackGpuPushConstants(*op);
    rd->compute_list_bind_compute_pipeline(
        compute_list,
        alpha_blend_a ? g_gpu_pipeline_state->alpha_blend_a_pipeline
                      : g_gpu_pipeline_state->blend_pipeline);
    rd->compute_list_bind_uniform_set(compute_list, uniform_set, 0);
    rd->compute_list_set_push_constant(compute_list, push_constants, 48);
    rd->compute_list_dispatch(compute_list,
                              static_cast<uint32_t>((op->size.x + 7) / 8),
                              static_cast<uint32_t>((op->size.y + 7) / 8),
                              1);
    return true;
}

bool DispatchGodotGpuBlend2(RenderingDevice *rd,
                            const std::shared_ptr<GodotGpuOp> &op,
                            int64_t compute_list,
                            std::vector<RID> &uniform_sets) {
    if (!EnsureBlend2Pipeline(rd)) return false;

    RID uniform_set = GetCachedBlend2UniformSet(
        rd, g_gpu_pipeline_state->blend2_shader, op->src, op->src2, op->dst);
    if (!uniform_set.is_valid()) return false;
    (void)uniform_sets;

    const PackedByteArray push_constants = PackGpuPushConstants(*op);
    rd->compute_list_bind_compute_pipeline(compute_list,
                                           g_gpu_pipeline_state->blend2_pipeline);
    rd->compute_list_bind_uniform_set(compute_list, uniform_set, 0);
    rd->compute_list_set_push_constant(compute_list, push_constants, 48);
    rd->compute_list_dispatch(compute_list,
                              static_cast<uint32_t>((op->size.x + 7) / 8),
                              static_cast<uint32_t>((op->size.y + 7) / 8),
                              1);
    return true;
}

bool ExecuteGodotGpuBlend(RenderingDevice *rd,
                          const std::shared_ptr<GodotGpuOp> &op) {
    std::vector<RID> uniform_sets;
    int64_t compute_list = rd->compute_list_begin();
    const bool ok = DispatchGodotGpuBlend(rd, op, compute_list, uniform_sets);
    rd->compute_list_end();
    for (const RID &uniform_set : uniform_sets) {
        rd->free_rid(uniform_set);
    }
    return ok;
}

bool ExecuteGodotGpuBlend2(RenderingDevice *rd,
                           const std::shared_ptr<GodotGpuOp> &op) {
    std::vector<RID> uniform_sets;
    int64_t compute_list = rd->compute_list_begin();
    const bool ok = DispatchGodotGpuBlend2(rd, op, compute_list, uniform_sets);
    rd->compute_list_end();
    for (const RID &uniform_set : uniform_sets) {
        rd->free_rid(uniform_set);
    }
    return ok;
}

Ref<RDTextureFormat> MakeRgbaTextureFormat(uint32_t width, uint32_t height);

bool ExecuteGodotGpuCopyTriangles(RenderingDevice *rd,
                                  const std::shared_ptr<GodotGpuOp> &op) {
    if (rd == nullptr || op == nullptr || op->vertices.empty() ||
        !EnsureCopyTrianglesPipeline(rd)) {
        return false;
    }

    PackedByteArray vertex_data;
    vertex_data.resize(static_cast<int64_t>(op->vertices.size() * sizeof(float)));
    if (uint8_t *bytes = vertex_data.ptrw()) {
        std::memcpy(bytes, op->vertices.data(), op->vertices.size() * sizeof(float));
    }
    RID vertex_buffer = rd->storage_buffer_create(vertex_data.size(), vertex_data);
    if (!vertex_buffer.is_valid()) return false;

    RID sample_src = op->src;
    RID temp_src;
    if (op->src == op->dst) {
        Ref<RDTextureView> view;
        view.instantiate();
        TypedArray<PackedByteArray> initial_data;
        temp_src = rd->texture_create(
            MakeRgbaTextureFormat(static_cast<uint32_t>(op->src_size.x),
                                  static_cast<uint32_t>(op->src_size.y)),
            view, initial_data);
        if (!temp_src.is_valid()) {
            rd->free_rid(vertex_buffer);
            return false;
        }
        const Error copied = rd->texture_copy(
            op->src, temp_src, Vector3(), Vector3(), op->src_size, 0, 0, 0, 0);
        if (copied != OK) {
            rd->free_rid(temp_src);
            rd->free_rid(vertex_buffer);
            return false;
        }
        sample_src = temp_src;
    }

    Ref<RDUniform> vertex_uniform;
    vertex_uniform.instantiate();
    vertex_uniform->set_uniform_type(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER);
    vertex_uniform->set_binding(0);
    vertex_uniform->add_id(vertex_buffer);

    Ref<RDUniform> src_uniform;
    src_uniform.instantiate();
    src_uniform->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
    src_uniform->set_binding(1);
    src_uniform->add_id(sample_src);

    Ref<RDUniform> dst_uniform;
    dst_uniform.instantiate();
    dst_uniform->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
    dst_uniform->set_binding(2);
    dst_uniform->add_id(op->dst);

    TypedArray<RDUniform> uniforms;
    uniforms.push_back(vertex_uniform);
    uniforms.push_back(src_uniform);
    uniforms.push_back(dst_uniform);
    RID uniform_set = rd->uniform_set_create(
        uniforms, g_gpu_pipeline_state->copy_triangles_shader, 0);
    if (!uniform_set.is_valid()) {
        if (temp_src.is_valid()) rd->free_rid(temp_src);
        rd->free_rid(vertex_buffer);
        return false;
    }

    const PackedByteArray push_constants = PackGpuPushConstants(*op);
    int64_t compute_list = rd->compute_list_begin();
    rd->compute_list_bind_compute_pipeline(
        compute_list, g_gpu_pipeline_state->copy_triangles_pipeline);
    rd->compute_list_bind_uniform_set(compute_list, uniform_set, 0);
    rd->compute_list_set_push_constant(compute_list, push_constants, 48);
    rd->compute_list_dispatch(compute_list,
                              static_cast<uint32_t>((op->size.x + 7) / 8),
                              static_cast<uint32_t>((op->size.y + 7) / 8),
                              1);
    rd->compute_list_end();
    rd->free_rid(uniform_set);
    if (temp_src.is_valid()) rd->free_rid(temp_src);
    rd->free_rid(vertex_buffer);
    return true;
}

bool ExecuteGodotGpuOp(RenderingDevice *rd, const std::shared_ptr<GodotGpuOp> &op) {
    if (rd == nullptr || op == nullptr) return false;
    switch (op->type) {
        case GodotGpuOp::Type::Update:
            return rd->texture_update(op->dst, 0, op->data) == OK;
        case GodotGpuOp::Type::Clear:
            return rd->texture_clear(op->dst, op->clear_color, 0, 1, 0, 1) == OK;
        case GodotGpuOp::Type::Copy:
            return rd->texture_copy(op->src, op->dst, op->src_pos, op->dst_pos,
                                    op->size, 0, 0, 0, 0) == OK;
        case GodotGpuOp::Type::CopySelf: {
            Ref<RDTextureView> view;
            view.instantiate();
            TypedArray<PackedByteArray> initial_data;
            RID temp = rd->texture_create(
                MakeRgbaTextureFormat(static_cast<uint32_t>(op->size.x),
                                      static_cast<uint32_t>(op->size.y)),
                view, initial_data);
            if (!temp.is_valid()) return false;
            const Error copy_to_temp =
                rd->texture_copy(op->src, temp, op->src_pos, Vector3(), op->size,
                                 0, 0, 0, 0);
            const Error copy_to_dst =
                copy_to_temp == OK
                    ? rd->texture_copy(temp, op->dst, Vector3(), op->dst_pos,
                                       op->size, 0, 0, 0, 0)
                    : FAILED;
            rd->free_rid(temp);
            return copy_to_temp == OK && copy_to_dst == OK;
        }
        case GodotGpuOp::Type::CopyTriangles:
            return ExecuteGodotGpuCopyTriangles(rd, op);
        case GodotGpuOp::Type::Read:
            op->data = rd->texture_get_data(op->src, 0);
            return !op->data.is_empty();
        case GodotGpuOp::Type::Blend:
            return ExecuteGodotGpuBlend(rd, op);
        case GodotGpuOp::Type::Blend2:
            return ExecuteGodotGpuBlend2(rd, op);
        case GodotGpuOp::Type::Release:
            ClearGodotGpuUniformSetCache(rd);
            rd->free_rid(op->dst);
            return true;
        case GodotGpuOp::Type::Flush:
            return true;
    }
    return false;
}

void FinishGodotGpuOp(const std::shared_ptr<GodotGpuOp> &op, bool result) {
    {
        std::lock_guard<std::mutex> done_lock(op->done_mutex);
        op->result = result;
        op->done = true;
    }
    op->done_cv.notify_one();
}

void ExecuteGodotGpuBlendBatch(
    RenderingDevice *rd,
    const std::vector<std::shared_ptr<GodotGpuOp>> &ops) {
    if (ops.empty()) return;
    if (rd == nullptr) {
        for (const auto &op : ops) {
            FinishGodotGpuOp(op, false);
        }
        return;
    }

    std::vector<RID> uniform_sets;
    uniform_sets.reserve(ops.size());
    std::vector<bool> results(ops.size(), false);
    int64_t compute_list = rd->compute_list_begin();
    for (size_t i = 0; i < ops.size(); ++i) {
        results[i] = DispatchGodotGpuBlend(rd, ops[i], compute_list, uniform_sets);
    }
    rd->compute_list_end();
    for (const RID &uniform_set : uniform_sets) {
        rd->free_rid(uniform_set);
    }
    for (size_t i = 0; i < ops.size(); ++i) {
        FinishGodotGpuOp(ops[i], results[i]);
    }
}

void DrainGodotGpuOpsOnRenderThread() {
    RenderingDevice *rd = MainRenderingDevice();
    std::vector<std::shared_ptr<GodotGpuOp>> blend_batch;
    for (;;) {
        std::shared_ptr<GodotGpuOp> op;
        {
            std::lock_guard<std::mutex> lock(g_gpu_op_queue_mutex);
            if (g_gpu_op_queue.empty()) {
                g_gpu_op_drain_scheduled = false;
                break;
            }
            op = g_gpu_op_queue.front();
            g_gpu_op_queue.pop_front();
        }

        if (op->type == GodotGpuOp::Type::Blend) {
            blend_batch.push_back(op);
            continue;
        }

        ExecuteGodotGpuBlendBatch(rd, blend_batch);
        blend_batch.clear();
        FinishGodotGpuOp(op, ExecuteGodotGpuOp(rd, op));
    }
    ExecuteGodotGpuBlendBatch(rd, blend_batch);
}

bool RunGodotGpuOp(const std::shared_ptr<GodotGpuOp> &op, bool wait) {
    RenderingServer *server = RenderingServer::get_singleton();
    RenderingDevice *rd = MainRenderingDevice();
    if (server == nullptr || rd == nullptr || op == nullptr) return false;
    if (server->is_on_render_thread()) {
        return ExecuteGodotGpuOp(rd, op);
    }

    bool should_schedule = false;
    {
        std::lock_guard<std::mutex> lock(g_gpu_op_queue_mutex);
        g_gpu_op_queue.push_back(op);
        if (!g_gpu_op_drain_scheduled) {
            g_gpu_op_drain_scheduled = true;
            should_schedule = true;
        }
    }
    if (should_schedule) {
        server->call_on_render_thread(
            callable_mp_static(&DrainGodotGpuOpsOnRenderThread));
    }

    if (!wait) {
        return true;
    }
    std::unique_lock<std::mutex> done_lock(op->done_mutex);
    op->done_cv.wait(done_lock, [&]() { return op->done; });
    return op->result;
}

bool RunGodotGpuOpAsync(const std::shared_ptr<GodotGpuOp> &op) {
    return RunGodotGpuOp(op, false);
}

bool RunGodotGpuOpSync(const std::shared_ptr<GodotGpuOp> &op) {
    return RunGodotGpuOp(op, true);
}

PackedByteArray PackRgbaBytes(const void *pixels, uint32_t width,
                              uint32_t height, uint32_t stride_bytes) {
    PackedByteArray data;
    const uint32_t tight_stride = width * 4u;
    data.resize(static_cast<int64_t>(tight_stride) * height);
    uint8_t *dst = data.ptrw();
    if (dst == nullptr) return data;
    if (pixels == nullptr) {
        std::memset(dst, 0, static_cast<size_t>(tight_stride) * height);
        return data;
    }
    const auto *src = static_cast<const uint8_t *>(pixels);
    const uint32_t src_stride = stride_bytes != 0 ? stride_bytes : tight_stride;
    for (uint32_t y = 0; y < height; ++y) {
        std::memcpy(dst + static_cast<size_t>(y) * tight_stride,
                    src + static_cast<size_t>(y) * src_stride,
                    tight_stride);
    }
    return data;
}

Ref<RDTextureFormat> MakeRgbaTextureFormat(uint32_t width, uint32_t height) {
    Ref<RDTextureFormat> format;
    format.instantiate();
    format->set_format(RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM);
    format->set_width(width);
    format->set_height(height);
    format->set_depth(1);
    format->set_array_layers(1);
    format->set_mipmaps(1);
    format->set_texture_type(RenderingDevice::TEXTURE_TYPE_2D);
    format->set_samples(RenderingDevice::TEXTURE_SAMPLES_1);
    format->set_usage_bits(BitField<RenderingDevice::TextureUsageBits>(
        RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
        RenderingDevice::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT |
        RenderingDevice::TEXTURE_USAGE_STORAGE_BIT |
        RenderingDevice::TEXTURE_USAGE_CAN_UPDATE_BIT |
        RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT |
        RenderingDevice::TEXTURE_USAGE_CAN_COPY_TO_BIT));
    return format;
}

uint64_t BridgeCreateRgba(uint32_t width, uint32_t height, const void *pixels,
                          uint32_t stride_bytes) {
    RenderingDevice *rd = MainRenderingDevice();
    if (rd == nullptr || width == 0 || height == 0) return 0;

    Ref<RDTextureView> view;
    view.instantiate();
    TypedArray<PackedByteArray> initial_data;
    initial_data.push_back(PackRgbaBytes(pixels, width, height, stride_bytes));
    RID rid = rd->texture_create(MakeRgbaTextureFormat(width, height), view,
                                 initial_data);
    if (!rid.is_valid()) return 0;

    GodotGpuTextureRecord record;
    record.rid = rid;
    record.width = width;
    record.height = height;
    record.texture.instantiate();
    record.texture->set_texture_rd_rid(rid);

    std::lock_guard<std::mutex> lock(g_gpu_textures_mutex);
    const uint64_t id = g_next_gpu_texture_id++;
    g_gpu_textures[id] = record;
    return id;
}

void BridgeReleaseTexture(uint64_t texture) {
    GodotGpuTextureRecord record;
    {
        std::lock_guard<std::mutex> lock(g_gpu_textures_mutex);
        auto it = g_gpu_textures.find(texture);
        if (it == g_gpu_textures.end()) return;
        record = it->second;
        g_gpu_textures.erase(it);
    }
    record.texture.unref();
    if (record.rid.is_valid()) {
        auto op = std::make_shared<GodotGpuOp>();
        op->type = GodotGpuOp::Type::Release;
        op->dst = record.rid;
        RunGodotGpuOpSync(op);
    }
}

bool BridgeUpdateRgba(uint64_t texture, const void *pixels,
                      uint32_t stride_bytes, const tTVPRect *rect) {
    if (pixels == nullptr) return false;
    std::lock_guard<std::mutex> lock(g_gpu_textures_mutex);
    auto it = g_gpu_textures.find(texture);
    if (it == g_gpu_textures.end()) return false;
    const auto &record = it->second;
    if (rect == nullptr || rect->left != 0 || rect->top != 0 ||
        rect->right != static_cast<int>(record.width) ||
        rect->bottom != static_cast<int>(record.height)) {
        return false;
    }
    PackedByteArray data =
        PackRgbaBytes(pixels, record.width, record.height, stride_bytes);
    auto op = std::make_shared<GodotGpuOp>();
    op->type = GodotGpuOp::Type::Update;
    op->dst = record.rid;
    op->data = data;
    return RunGodotGpuOpSync(op);
}

bool BridgeClearRgba(uint64_t texture, uint32_t argb, const tTVPRect *rect) {
    std::lock_guard<std::mutex> lock(g_gpu_textures_mutex);
    auto it = g_gpu_textures.find(texture);
    if (it == g_gpu_textures.end()) return false;
    const auto &record = it->second;
    if (rect == nullptr) {
        return false;
    }
    const bool full_rect =
        rect->left == 0 && rect->top == 0 &&
        rect->right == static_cast<int>(record.width) &&
        rect->bottom == static_cast<int>(record.height);
    if (rect->left < 0 || rect->top < 0 ||
        rect->right > static_cast<int>(record.width) ||
        rect->bottom > static_cast<int>(record.height) ||
        rect->right <= rect->left || rect->bottom <= rect->top) {
        return false;
    }
    const float a = static_cast<float>((argb >> 24) & 0xffu) / 255.0f;
    const float r = static_cast<float>(argb & 0xffu) / 255.0f;
    const float g = static_cast<float>((argb >> 8) & 0xffu) / 255.0f;
    const float b = static_cast<float>((argb >> 16) & 0xffu) / 255.0f;
    auto op = std::make_shared<GodotGpuOp>();
    op->dst = record.rid;
    if (full_rect) {
        op->type = GodotGpuOp::Type::Clear;
        op->clear_color = Color(r, g, b, a);
    } else {
        op->type = GodotGpuOp::Type::Blend;
        op->src = record.rid;
        op->dst_pos = Vector3(rect->left, rect->top, 0);
        op->src_pos = op->dst_pos;
        op->size = Vector3(rect->right - rect->left,
                           rect->bottom - rect->top, 1);
        op->mode = TVP_GODOT_GPU_BLEND_FILL_ARGB;
        op->opacity = 255;
        op->color = argb;
    }
    return RunGodotGpuOpSync(op);
}

bool BridgeCopyRect(uint64_t dst, uint64_t src, const tTVPRect *dst_rect,
                    const tTVPRect *src_rect) {
    if (dst_rect == nullptr || src_rect == nullptr) return false;
    std::lock_guard<std::mutex> lock(g_gpu_textures_mutex);
    auto dst_it = g_gpu_textures.find(dst);
    auto src_it = g_gpu_textures.find(src);
    if (dst_it == g_gpu_textures.end() || src_it == g_gpu_textures.end()) {
        return false;
    }
    const int width = dst_rect->right - dst_rect->left;
    const int height = dst_rect->bottom - dst_rect->top;
    if (width <= 0 || height <= 0 ||
        width != src_rect->right - src_rect->left ||
        height != src_rect->bottom - src_rect->top) {
        return false;
    }
    auto op = std::make_shared<GodotGpuOp>();
    op->type = dst == src ? GodotGpuOp::Type::CopySelf : GodotGpuOp::Type::Copy;
    op->src = src_it->second.rid;
    op->dst = dst_it->second.rid;
    op->src_pos = Vector3(src_rect->left, src_rect->top, 0);
    op->dst_pos = Vector3(dst_rect->left, dst_rect->top, 0);
    op->size = Vector3(width, height, 1);
    return RunGodotGpuOpSync(op);
}

bool BridgeCopyTriangles(uint64_t dst, uint64_t src, uint32_t triangle_count,
                         const tTVPRect *clip_rect,
                         const tTVPPointD *dst_points,
                         const tTVPPointD *src_points) {
    if (clip_rect == nullptr || dst_points == nullptr || src_points == nullptr ||
        triangle_count == 0) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_gpu_textures_mutex);
    auto dst_it = g_gpu_textures.find(dst);
    auto src_it = g_gpu_textures.find(src);
    if (dst_it == g_gpu_textures.end() || src_it == g_gpu_textures.end()) {
        return false;
    }
    const int width = clip_rect->right - clip_rect->left;
    const int height = clip_rect->bottom - clip_rect->top;
    if (width <= 0 || height <= 0 || triangle_count > 64) {
        return false;
    }

    auto op = std::make_shared<GodotGpuOp>();
    op->type = GodotGpuOp::Type::CopyTriangles;
    op->src = src_it->second.rid;
    op->dst = dst_it->second.rid;
    op->dst_pos = Vector3(clip_rect->left, clip_rect->top, 0);
    op->src_pos = Vector3(0, 0, 0);
    op->size = Vector3(width, height, 1);
    op->src_size = Vector3(src_it->second.width, src_it->second.height, 1);
    op->mode = triangle_count;
    op->vertices.reserve(static_cast<size_t>(triangle_count) * 12u);
    for (uint32_t i = 0; i < triangle_count * 3u; ++i) {
        op->vertices.push_back(static_cast<float>(dst_points[i].x));
        op->vertices.push_back(static_cast<float>(dst_points[i].y));
        op->vertices.push_back(static_cast<float>(src_points[i].x));
        op->vertices.push_back(static_cast<float>(src_points[i].y));
    }
    return RunGodotGpuOpAsync(op);
}

bool BridgeBlendRect(uint64_t dst, uint64_t src, const tTVPRect *dst_rect,
                     const tTVPRect *src_rect, uint32_t mode, int opacity,
                     uint32_t color) {
    if (dst_rect == nullptr || src_rect == nullptr) return false;
    std::lock_guard<std::mutex> lock(g_gpu_textures_mutex);
    auto dst_it = g_gpu_textures.find(dst);
    auto src_it = g_gpu_textures.find(src);
    if (dst_it == g_gpu_textures.end() || src_it == g_gpu_textures.end()) {
        return false;
    }
    const int width = dst_rect->right - dst_rect->left;
    const int height = dst_rect->bottom - dst_rect->top;
    if (width <= 0 || height <= 0 ||
        width != src_rect->right - src_rect->left ||
        height != src_rect->bottom - src_rect->top) {
        return false;
    }

    auto op = std::make_shared<GodotGpuOp>();
    op->type = GodotGpuOp::Type::Blend;
    op->src = src_it->second.rid;
    op->dst = dst_it->second.rid;
    op->src_pos = Vector3(src_rect->left, src_rect->top, 0);
    op->dst_pos = Vector3(dst_rect->left, dst_rect->top, 0);
    op->size = Vector3(width, height, 1);
    op->mode = mode;
    op->opacity = opacity;
    op->color = color;
    return RunGodotGpuOpAsync(op);
}

bool BridgeBlendRect2(uint64_t dst, uint64_t src1, uint64_t src2,
                      const tTVPRect *dst_rect, const tTVPRect *src1_rect,
                      const tTVPRect *src2_rect, uint32_t mode, int opacity,
                      uint32_t color) {
    if (dst_rect == nullptr || src1_rect == nullptr || src2_rect == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_gpu_textures_mutex);
    auto dst_it = g_gpu_textures.find(dst);
    auto src1_it = g_gpu_textures.find(src1);
    auto src2_it = g_gpu_textures.find(src2);
    if (dst_it == g_gpu_textures.end() || src1_it == g_gpu_textures.end() ||
        src2_it == g_gpu_textures.end()) {
        return false;
    }
    const int width = dst_rect->right - dst_rect->left;
    const int height = dst_rect->bottom - dst_rect->top;
    if (width <= 0 || height <= 0 ||
        width != src1_rect->right - src1_rect->left ||
        height != src1_rect->bottom - src1_rect->top ||
        width != src2_rect->right - src2_rect->left ||
        height != src2_rect->bottom - src2_rect->top) {
        return false;
    }

    auto op = std::make_shared<GodotGpuOp>();
    op->type = GodotGpuOp::Type::Blend2;
    op->src = src1_it->second.rid;
    op->src2 = src2_it->second.rid;
    op->dst = dst_it->second.rid;
    op->src_pos = Vector3(src1_rect->left, src1_rect->top, 0);
    op->src2_pos = Vector3(src2_rect->left, src2_rect->top, 0);
    op->dst_pos = Vector3(dst_rect->left, dst_rect->top, 0);
    op->size = Vector3(width, height, 1);
    op->mode = mode;
    op->opacity = opacity;
    op->color = color;
    return RunGodotGpuOpAsync(op);
}

bool BridgeReadRgba(uint64_t texture, void *out_pixels, size_t out_pixels_size,
                    uint32_t stride_bytes) {
    if (out_pixels == nullptr) return false;
    std::lock_guard<std::mutex> lock(g_gpu_textures_mutex);
    auto it = g_gpu_textures.find(texture);
    if (it == g_gpu_textures.end()) return false;
    const auto &record = it->second;
    const uint32_t tight_stride = record.width * 4u;
    const uint32_t dst_stride = stride_bytes != 0 ? stride_bytes : tight_stride;
    if (out_pixels_size < static_cast<size_t>(dst_stride) * record.height) {
        return false;
    }
    auto op = std::make_shared<GodotGpuOp>();
    op->type = GodotGpuOp::Type::Read;
    op->src = record.rid;
    if (!RunGodotGpuOpSync(op)) return false;
    PackedByteArray data = op->data;
    const uint8_t *src = data.ptr();
    auto *dst = static_cast<uint8_t *>(out_pixels);
    if (src == nullptr) return false;
    for (uint32_t y = 0; y < record.height; ++y) {
        std::memcpy(dst + static_cast<size_t>(y) * dst_stride,
                    src + static_cast<size_t>(y) * tight_stride,
                    tight_stride);
    }
    return true;
}

bool BridgeFlush() {
    auto op = std::make_shared<GodotGpuOp>();
    op->type = GodotGpuOp::Type::Flush;
    return RunGodotGpuOpAsync(op);
}

Ref<Texture2D> ResolveBridgeTexture(uint64_t texture) {
    std::lock_guard<std::mutex> lock(g_gpu_textures_mutex);
    auto it = g_gpu_textures.find(texture);
    if (it == g_gpu_textures.end()) return Ref<Texture2D>();
    return it->second.texture;
}

bool ResolveBridgeTextureRecord(uint64_t texture, GodotGpuTextureRecord &record) {
    std::lock_guard<std::mutex> lock(g_gpu_textures_mutex);
    auto it = g_gpu_textures.find(texture);
    if (it == g_gpu_textures.end()) return false;
    record = it->second;
    return true;
}

uint32_t CpuAlphaBlendHda(uint32_t d, uint32_t s, int opacity) {
    const uint32_t sopa =
        (((s >> 24) & 0xffu) * static_cast<uint32_t>(std::clamp(opacity, 0, 255))) >> 8;
    const auto blend = [sopa](uint32_t dc, uint32_t sc) -> uint32_t {
        const int value = static_cast<int>(dc) +
                          (((static_cast<int>(sc) - static_cast<int>(dc)) *
                            static_cast<int>(sopa)) >> 8);
        return static_cast<uint32_t>(std::clamp(value, 0, 255));
    };
    return (d & 0xff000000u) |
           blend(d & 0xffu, s & 0xffu) |
           (blend((d >> 8) & 0xffu, (s >> 8) & 0xffu) << 8) |
           (blend((d >> 16) & 0xffu, (s >> 16) & 0xffu) << 16);
}

uint32_t CpuOpacityOnOpacity(uint32_t dest_alpha, uint32_t src_alpha) {
    if (dest_alpha == 0u) return 255u;
    const uint32_t denom =
        dest_alpha * (255u - src_alpha) + 255u * src_alpha;
    if (denom == 0u) return 255u;
    return std::min<uint32_t>((255u * 255u * src_alpha) / denom, 255u);
}

uint32_t CpuNegativeMulAlpha(uint32_t dest_alpha, uint32_t src_alpha) {
    return 255u - (((255u - dest_alpha) * (255u - src_alpha)) / 255u);
}

uint32_t CpuAlphaBlendD(uint32_t d, uint32_t s, int opacity) {
    const uint32_t opa = static_cast<uint32_t>(std::clamp(opacity, 0, 255));
    uint32_t effective_alpha = (s >> 24) & 0xffu;
    if (opa == 255u) {
        if (s <= 0x00ffffffu) return d;
        if (s >= 0xff000000u) return s;
        if (d <= 0x00ffffffu) return s;
    } else {
        effective_alpha = (effective_alpha * opa) >> 8;
    }

    const uint32_t dest_alpha = (d >> 24) & 0xffu;
    const uint32_t blend_alpha =
        CpuOpacityOnOpacity(dest_alpha, effective_alpha);
    const uint32_t out_alpha = CpuNegativeMulAlpha(dest_alpha, effective_alpha);
    const auto blend = [blend_alpha](uint32_t dc, uint32_t sc) -> uint32_t {
        const int value = static_cast<int>(dc) +
                          (((static_cast<int>(sc) - static_cast<int>(dc)) *
                            static_cast<int>(blend_alpha)) >> 8);
        return static_cast<uint32_t>(std::clamp(value, 0, 255));
    };
    return (out_alpha << 24) |
           blend(d & 0xffu, s & 0xffu) |
           (blend((d >> 8) & 0xffu, (s >> 8) & 0xffu) << 8) |
           (blend((d >> 16) & 0xffu, (s >> 16) & 0xffu) << 16);
}

uint32_t CpuCopyColor(uint32_t d, uint32_t s) {
    return (d & 0xff000000u) | (s & 0x00ffffffu);
}

uint32_t CpuFillArgb(uint32_t, uint32_t color) {
    return color;
}

uint32_t CpuRemoveConstOpacity(uint32_t d, int strength) {
    const uint32_t inv_strength =
        255u - static_cast<uint32_t>(std::clamp(strength, 0, 255));
    const uint32_t a = (((d >> 24) & 0xffu) * inv_strength) >> 8;
    return (d & 0x00ffffffu) | (a << 24);
}

uint32_t CpuSaturatedAdd(uint32_t a, uint32_t b) {
    uint32_t tmp = ((a & b) + (((a ^ b) >> 1) & 0x7f7f7f7fu)) & 0x80808080u;
    tmp = (tmp << 1) - (tmp >> 7);
    return (a + b - tmp) | tmp;
}

uint32_t CpuMulColor(uint32_t color, uint32_t fac) {
    return (((((color & 0x00ff00u) * fac) & 0x00ff0000u) +
             (((color & 0xff00ffu) * fac) & 0xff00ff00u)) >> 8);
}

uint32_t CpuAlphaToAdditiveAlpha(uint32_t c) {
    return CpuMulColor(c, c >> 24) + (c & 0xff000000u);
}

uint32_t CpuAddAlphaBlendAA(uint32_t d, uint32_t s) {
    uint32_t dopa = d >> 24;
    uint32_t sopa = s >> 24;
    dopa = dopa + sopa - ((dopa * sopa) >> 8);
    dopa -= dopa >> 8;
    sopa ^= 0xffu;
    s &= 0x00ffffffu;
    return (dopa << 24) +
           CpuSaturatedAdd((((d & 0xff00ffu) * sopa >> 8) & 0xff00ffu) +
                               (((d & 0x00ff00u) * sopa >> 8) & 0x00ff00u),
                           s);
}

uint32_t CpuAlphaBlendA(uint32_t d, uint32_t s, int opacity) {
    const uint32_t opa = static_cast<uint32_t>(std::clamp(opacity, 0, 255));
    if (opa != 255u) {
        s = (s & 0x00ffffffu) + (((((s >> 24) * opa) >> 8) & 0xffu) << 24);
    }
    return CpuAddAlphaBlendAA(d, CpuAlphaToAdditiveAlpha(s));
}

uint32_t CpuConstAlphaBlendD(uint32_t d, uint32_t s, int opacity) {
    const uint32_t opa = static_cast<uint32_t>(std::clamp(opacity, 0, 255));
    const uint32_t dest_alpha = d >> 24;
    const uint32_t alpha = CpuOpacityOnOpacity(dest_alpha, opa);
    const uint32_t out_alpha = CpuNegativeMulAlpha(dest_alpha, opa);
    uint32_t d_rb = d & 0xff00ffu;
    d_rb = ((d_rb + (((s & 0xff00ffu) - d_rb) * alpha >> 8)) &
            0xff00ffu) |
           (out_alpha << 24);
    uint32_t d_g = d & 0xff00u;
    uint32_t s_g = s & 0xff00u;
    return d_rb | ((d_g + ((s_g - d_g) * alpha >> 8)) & 0xff00u);
}

uint32_t CpuConstAlphaBlendSD(uint32_t s1, uint32_t s2, int opacity) {
    const uint32_t opa = static_cast<uint32_t>(std::clamp(opacity, 0, 255));
    uint32_t s1_rb = s1 & 0xff00ffu;
    s1_rb = (s1_rb + (((s2 & 0xff00ffu) - s1_rb) * opa >> 8)) &
             0xff00ffu;
    uint32_t s1_g = s1 & 0xff00u;
    uint32_t s2_g = s2 & 0xff00u;
    return s1_rb | ((s1_g + ((s2_g - s1_g) * opa >> 8)) & 0xff00u);
}

uint32_t CpuConstAlphaBlendSDD(uint32_t s1, uint32_t s2, int opacity) {
    uint32_t opa = static_cast<uint32_t>(std::clamp(opacity, 0, 255));
    if (opa > 127u) {
        opa += 1u;
    }
    const uint32_t iopa = 256u - opa;
    const uint32_t a1 = s1 >> 24;
    const uint32_t a2 = s2 >> 24;
    const uint32_t alpha =
        CpuOpacityOnOpacity((a1 * iopa) >> 8, (a2 * opa) >> 8);
    uint32_t s1_rb = s1 & 0xff00ffu;
    s1_rb = (s1_rb + (((s2 & 0xff00ffu) - s1_rb) * alpha >> 8)) &
             0xff00ffu;
    uint32_t s1_g = s1 & 0xff00u;
    uint32_t s2_g = s2 & 0xff00u;
    s1_rb |= (a1 + ((a2 - a1) * opa >> 8)) << 24;
    return s1_rb | ((s1_g + ((s2_g - s1_g) * alpha >> 8)) & 0xff00u);
}

uint32_t CpuBlendReference(uint32_t mode, uint32_t d, uint32_t s,
                           int opacity, uint32_t color) {
    switch (mode) {
        case TVP_GODOT_GPU_BLEND_ALPHA:
            return CpuAlphaBlendHda(d, s, opacity);
        case TVP_GODOT_GPU_BLEND_ALPHA_D:
            return CpuAlphaBlendD(d, s, opacity);
        case TVP_GODOT_GPU_BLEND_COPY_COLOR:
            return CpuCopyColor(d, s);
        case TVP_GODOT_GPU_BLEND_FILL_ARGB:
            return CpuFillArgb(d, color);
        case TVP_GODOT_GPU_BLEND_REMOVE_CONST_OPACITY:
            return CpuRemoveConstOpacity(d, opacity);
        case TVP_GODOT_GPU_BLEND_ALPHA_BLEND_A:
            return CpuAlphaBlendA(d, s, opacity);
        case TVP_GODOT_GPU_BLEND_CONST_ALPHA_D:
            return CpuConstAlphaBlendD(d, s, opacity);
        default:
            return s;
    }
}

uint32_t CpuBlend2Reference(uint32_t mode, uint32_t src1, uint32_t src2,
                            int opacity) {
    switch (mode) {
        case TVP_GODOT_GPU_BLEND_CONST_ALPHA_SD:
            return CpuConstAlphaBlendSD(src1, src2, opacity);
        case TVP_GODOT_GPU_BLEND_CONST_ALPHA_SD_D:
            return CpuConstAlphaBlendSDD(src1, src2, opacity);
        default:
            return src2;
    }
}

uint32_t BlendModeFromName(const String &mode_name) {
    const String lower = mode_name.to_lower();
    if (lower == "alphablend" || lower == "alpha") {
        return TVP_GODOT_GPU_BLEND_ALPHA;
    }
    if (lower == "alphablend_d" || lower == "alpha_blend_d") {
        return TVP_GODOT_GPU_BLEND_ALPHA_D;
    }
    if (lower == "copycolor" || lower == "copy_color") {
        return TVP_GODOT_GPU_BLEND_COPY_COLOR;
    }
    if (lower == "fillargb" || lower == "fill") {
        return TVP_GODOT_GPU_BLEND_FILL_ARGB;
    }
    if (lower == "removeconstopacity" || lower == "remove_const_opacity") {
        return TVP_GODOT_GPU_BLEND_REMOVE_CONST_OPACITY;
    }
    if (lower == "alphablend_a" || lower == "alpha_blend_a") {
        return TVP_GODOT_GPU_BLEND_ALPHA_BLEND_A;
    }
    if (lower == "constalphablend_d" || lower == "const_alpha_blend_d") {
        return TVP_GODOT_GPU_BLEND_CONST_ALPHA_D;
    }
    if (lower == "constalphablend_sd" || lower == "const_alpha_blend_sd") {
        return TVP_GODOT_GPU_BLEND_CONST_ALPHA_SD;
    }
    if (lower == "constalphablend_sd_d" || lower == "const_alpha_blend_sd_d") {
        return TVP_GODOT_GPU_BLEND_CONST_ALPHA_SD_D;
    }
    return 0;
}

void ReleaseGodotGpuPipeline() {
    if (g_gpu_pipeline_state == nullptr) return;
    RenderingDevice *rd = MainRenderingDevice();
    if (rd != nullptr) {
        ClearGodotGpuUniformSetCache(rd);
        if (g_gpu_pipeline_state->blend_pipeline.is_valid()) {
            rd->free_rid(g_gpu_pipeline_state->blend_pipeline);
        }
        if (g_gpu_pipeline_state->blend_shader.is_valid()) {
            rd->free_rid(g_gpu_pipeline_state->blend_shader);
        }
        if (g_gpu_pipeline_state->alpha_blend_a_pipeline.is_valid()) {
            rd->free_rid(g_gpu_pipeline_state->alpha_blend_a_pipeline);
        }
        if (g_gpu_pipeline_state->alpha_blend_a_shader.is_valid()) {
            rd->free_rid(g_gpu_pipeline_state->alpha_blend_a_shader);
        }
        if (g_gpu_pipeline_state->blend2_pipeline.is_valid()) {
            rd->free_rid(g_gpu_pipeline_state->blend2_pipeline);
        }
        if (g_gpu_pipeline_state->blend2_shader.is_valid()) {
            rd->free_rid(g_gpu_pipeline_state->blend2_shader);
        }
        if (g_gpu_pipeline_state->copy_triangles_pipeline.is_valid()) {
            rd->free_rid(g_gpu_pipeline_state->copy_triangles_pipeline);
        }
        if (g_gpu_pipeline_state->copy_triangles_shader.is_valid()) {
            rd->free_rid(g_gpu_pipeline_state->copy_triangles_shader);
        }
    }
    delete g_gpu_pipeline_state;
    g_gpu_pipeline_state = nullptr;
}

void ReleaseRemainingGodotGpuTextures() {
    std::vector<GodotGpuTextureRecord> records;
    {
        std::lock_guard<std::mutex> lock(g_gpu_textures_mutex);
        records.reserve(g_gpu_textures.size());
        for (auto &entry : g_gpu_textures) {
            records.push_back(entry.second);
        }
        g_gpu_textures.clear();
    }

    for (auto &record : records) {
        record.texture.unref();
        if (record.rid.is_valid()) {
            auto op = std::make_shared<GodotGpuOp>();
            op->type = GodotGpuOp::Type::Release;
            op->dst = record.rid;
            RunGodotGpuOpSync(op);
        }
    }
}

} // namespace

class AetherKiriPlayer final : public Node {
    GDCLASS(AetherKiriPlayer, Node)

public:
    AetherKiriPlayer() = default;
    ~AetherKiriPlayer() override { destroy_engine(); }

    bool initialize_engine(const String &writable_path, const String &cache_path) {
        if (handle_ != nullptr) {
            return true;
        }

        TVPGodotGpuBridgeCallbacks callbacks{};
        callbacks.create_rgba = BridgeCreateRgba;
        callbacks.release_texture = BridgeReleaseTexture;
        callbacks.update_rgba = BridgeUpdateRgba;
        callbacks.clear_rgba = BridgeClearRgba;
        callbacks.copy_rect = BridgeCopyRect;
        callbacks.copy_triangles = BridgeCopyTriangles;
        callbacks.blend_rect = BridgeBlendRect;
        callbacks.blend_rect2 = BridgeBlendRect2;
        callbacks.read_rgba = BridgeReadRgba;
        callbacks.flush = BridgeFlush;
        TVPGodotGpuBridgeRegister(&callbacks);

        CharString writable_utf8 = writable_path.utf8();
        CharString cache_utf8 = cache_path.utf8();

        engine_create_desc_t desc{};
        desc.struct_size = sizeof(desc);
        desc.api_version = ENGINE_API_VERSION;
        desc.writable_path_utf8 = writable_utf8.get_data();
        desc.cache_path_utf8 = cache_utf8.get_data();

        const engine_result_t result = engine_create(&desc, &handle_);
        last_result_ = ResultToString(result);
        last_error_ = LastError(handle_);
        return result == ENGINE_RESULT_OK;
    }

    void destroy_engine() {
        if (handle_ == nullptr) {
            return;
        }
        release_rd_texture(false);
        const engine_result_t result = engine_destroy(handle_);
        BridgeFlush();
        if (result != ENGINE_RESULT_OK) {
            last_result_ = ResultToString(result);
            last_error_ = LastError(handle_);
        }
        handle_ = nullptr;
        game_open_ = false;
    }

    void release_frame_texture() {
        release_rd_texture(true);
        frame_texture_.unref();
        frame_texture_backend_ = "none";
    }

    bool is_initialized() const { return handle_ != nullptr; }

    bool is_game_open() const { return game_open_; }

    String get_last_result() const { return last_result_; }

    String get_last_error() const { return last_error_; }

    int set_render_backend(const String &backend) {
        if (handle_ == nullptr) {
            last_result_ = "INVALID_STATE";
            last_error_ = "engine is not initialized";
            return ENGINE_RESULT_INVALID_STATE;
        }

        engine_option_t option{};
        option.key_utf8 = ENGINE_OPTION_RENDERER;
        option.value_utf8 = NormalizeBackend(backend);
        const engine_result_t result = engine_set_option(handle_, &option);
        if (result == ENGINE_RESULT_OK) {
            backend_ = backend;
        }
        update_last_error(result);
        return result;
    }

    String get_render_backend() const { return backend_; }

    int set_engine_option(const String &key, const String &value) {
        if (handle_ == nullptr) {
            last_result_ = "INVALID_STATE";
            last_error_ = "engine is not initialized";
            return ENGINE_RESULT_INVALID_STATE;
        }

        const CharString key_utf8 = key.utf8();
        const CharString value_utf8 = value.utf8();
        engine_option_t option{};
        option.key_utf8 = key_utf8.get_data();
        option.value_utf8 = value_utf8.get_data();
        const engine_result_t result = engine_set_option(handle_, &option);
        update_last_error(result);
        return result;
    }

    int set_surface_size(int width, int height) {
        if (handle_ == nullptr) {
            return ENGINE_RESULT_INVALID_STATE;
        }
        const engine_result_t result = engine_set_surface_size(
            handle_, static_cast<uint32_t>(std::max(1, width)),
            static_cast<uint32_t>(std::max(1, height)));
        update_last_error(result);
        return result;
    }

    int open_game(const String &game_root_path, bool async) {
        if (handle_ == nullptr) {
            last_result_ = "INVALID_STATE";
            last_error_ = "engine is not initialized";
            return ENGINE_RESULT_INVALID_STATE;
        }

        CharString path_utf8 = game_root_path.utf8();
        const engine_result_t result = async
            ? engine_open_game_async(handle_, path_utf8.get_data(), nullptr)
            : engine_open_game(handle_, path_utf8.get_data(), nullptr);
        game_open_ = result == ENGINE_RESULT_OK;
        update_last_error(result);
        return result;
    }

    int tick(double delta_seconds) {
        if (handle_ == nullptr) {
            return ENGINE_RESULT_INVALID_STATE;
        }
        const auto delta_ms = static_cast<uint32_t>(
            std::max(0.0, delta_seconds) * 1000.0);
        const engine_result_t result = engine_tick(handle_, delta_ms);
        update_last_error(result);
        return result;
    }

    int pause() {
        if (handle_ == nullptr) {
            return ENGINE_RESULT_INVALID_STATE;
        }
        const engine_result_t result = engine_pause(handle_);
        update_last_error(result);
        return result;
    }

    int resume() {
        if (handle_ == nullptr) {
            return ENGINE_RESULT_INVALID_STATE;
        }
        const engine_result_t result = engine_resume(handle_);
        update_last_error(result);
        return result;
    }

    int send_pointer_event(int type, int pointer_id, double x, double y,
                           double delta_x, double delta_y, int button) {
        if (handle_ == nullptr) {
            return ENGINE_RESULT_INVALID_STATE;
        }
        engine_input_event_t event{};
        event.struct_size = sizeof(event);
        event.type = static_cast<uint32_t>(type);
        event.x = x;
        event.y = y;
        event.delta_x = delta_x;
        event.delta_y = delta_y;
        event.pointer_id = pointer_id;
        event.button = button;
        const engine_result_t result = engine_send_input(handle_, &event);
        update_last_error(result);
        return result;
    }

    int send_key_event(bool pressed, int key_code, int modifiers,
                       int unicode_codepoint) {
        if (handle_ == nullptr) {
            return ENGINE_RESULT_INVALID_STATE;
        }
        engine_input_event_t event{};
        event.struct_size = sizeof(event);
        event.type = pressed ? ENGINE_INPUT_EVENT_KEY_DOWN : ENGINE_INPUT_EVENT_KEY_UP;
        event.key_code = key_code;
        event.modifiers = modifiers;
        event.unicode_codepoint = static_cast<uint32_t>(
            std::max(0, unicode_codepoint));
        const engine_result_t result = engine_send_input(handle_, &event);
        update_last_error(result);
        return result;
    }

    int get_startup_state() {
        if (handle_ == nullptr) {
            return ENGINE_STARTUP_STATE_IDLE;
        }
        uint32_t state = ENGINE_STARTUP_STATE_IDLE;
        const engine_result_t result = engine_get_startup_state(handle_, &state);
        update_last_error(result);
        return static_cast<int>(state);
    }

    String drain_startup_logs() {
        if (handle_ == nullptr) {
            return String();
        }
        std::vector<char> buffer(64 * 1024);
        uint32_t bytes_written = 0;
        const engine_result_t result = engine_drain_startup_logs(
            handle_, buffer.data(), static_cast<uint32_t>(buffer.size()),
            &bytes_written);
        update_last_error(result);
        if (result != ENGINE_RESULT_OK || bytes_written == 0) {
            return String();
        }
        return String::utf8(buffer.data(), bytes_written);
    }

    String get_renderer_info() {
        if (handle_ == nullptr) {
            return String();
        }
        char buffer[512] = {};
        const engine_result_t result =
            engine_get_renderer_info(handle_, buffer, sizeof(buffer));
        update_last_error(result);
        return result == ENGINE_RESULT_OK ? String::utf8(buffer) : String();
    }

    String get_frame_texture_backend() const { return frame_texture_backend_; }

    Dictionary read_frame_rgba() {
        Dictionary output;
        if (handle_ == nullptr) {
            return output;
        }

        engine_frame_desc_t desc{};
        desc.struct_size = sizeof(desc);
        engine_result_t result = engine_get_frame_desc(handle_, &desc);
        update_last_error(result);
        if (result != ENGINE_RESULT_OK || desc.width == 0 || desc.height == 0 ||
            desc.stride_bytes == 0) {
            return output;
        }

        PackedByteArray data;
        const size_t size =
            static_cast<size_t>(desc.stride_bytes) * desc.height;
        data.resize(static_cast<int64_t>(size));
        result = engine_read_frame_rgba(handle_, data.ptrw(), size);
        update_last_error(result);
        if (result != ENGINE_RESULT_OK) {
            return output;
        }

        output["width"] = static_cast<int64_t>(desc.width);
        output["height"] = static_cast<int64_t>(desc.height);
        output["stride_bytes"] = static_cast<int64_t>(desc.stride_bytes);
        output["frame_serial"] = static_cast<int64_t>(desc.frame_serial);
        output["rgba"] = data;
        return output;
    }

    Ref<Texture2D> update_frame_texture() {
        if (handle_ == nullptr) {
            return Ref<Texture2D>();
        }

        const std::string normalized_backend = NormalizeBackend(backend_);
        if (normalized_backend == ENGINE_RENDERER_GODOT_NATIVE ||
            normalized_backend == ENGINE_RENDERER_GPU_BRIDGE) {
            uint64_t texture_id = 0;
            uint32_t width = 0;
            uint32_t height = 0;
            uint64_t serial = 0;
            engine_result_t gpu_result = engine_get_godot_native_frame_texture(
                handle_, &texture_id, &width, &height, &serial);
            if (gpu_result == ENGINE_RESULT_OK && texture_id != 0) {
                if (normalized_backend == ENGINE_RENDERER_GPU_BRIDGE) {
                    Ref<Texture2D> imported_texture =
                        update_imported_gpu_bridge_texture(texture_id, width,
                                                           height);
                    if (imported_texture.is_valid()) {
                        frame_texture_.unref();
                        frame_texture_serial_ = serial;
                        frame_texture_backend_ = "godot_external_import";
                        return imported_texture;
                    }
                    Ref<Texture2D> bridge_texture = ResolveBridgeTexture(texture_id);
                    if (bridge_texture.is_valid()) {
                        frame_texture_.unref();
                        frame_texture_serial_ = serial;
                        frame_texture_backend_ = "godot_native_gpu_bridge";
                        return bridge_texture;
                    }
                } else {
                    Ref<Texture2D> native_texture = ResolveBridgeTexture(texture_id);
                    if (native_texture.is_valid()) {
                        release_imported_texture();
                        frame_texture_.unref();
                        frame_texture_serial_ = serial;
                        frame_texture_backend_ = "godot_native_gpu";
                        return native_texture;
                    }
                }
            }
        }

        engine_frame_desc_t desc{};
        desc.struct_size = sizeof(desc);
        engine_result_t result = engine_get_frame_desc(handle_, &desc);
        update_last_error(result);
        if (result != ENGINE_RESULT_OK || desc.width == 0 || desc.height == 0 ||
            desc.stride_bytes == 0) {
            return Ref<Texture2D>();
        }

        if (frame_texture_.is_valid() && desc.frame_serial == frame_texture_serial_) {
            return frame_texture_;
        }

        PackedByteArray data;
        const size_t size =
            static_cast<size_t>(desc.stride_bytes) * desc.height;
        data.resize(static_cast<int64_t>(size));
        result = engine_read_frame_rgba(handle_, data.ptrw(), size);
        update_last_error(result);
        if (result != ENGINE_RESULT_OK) {
            return Ref<Texture2D>();
        }
        ForceOpaqueAlpha(data, desc.stride_bytes, desc.width, desc.height);

        const bool prefer_rd_texture =
            normalized_backend == ENGINE_RENDERER_GODOT_NATIVE ||
            normalized_backend == ENGINE_RENDERER_GPU_BRIDGE;
        if (prefer_rd_texture) {
            Ref<Texture2D> rd_texture = update_rd_texture(desc, data);
            if (rd_texture.is_valid()) {
                frame_texture_.unref();
                frame_texture_serial_ = desc.frame_serial;
                frame_texture_backend_ = "rendering_device";
                return rd_texture;
            }
            frame_texture_backend_ = "image_texture_fallback";
        }

        Ref<Image> image = Image::create_from_data(
            static_cast<int32_t>(desc.width),
            static_cast<int32_t>(desc.height),
            false,
            Image::FORMAT_RGBA8,
            data);
        if (image.is_null()) {
            return Ref<Texture2D>();
        }

        if (frame_texture_.is_null() ||
            frame_texture_->get_width() != static_cast<int32_t>(desc.width) ||
            frame_texture_->get_height() != static_cast<int32_t>(desc.height)) {
            frame_texture_ = ImageTexture::create_from_image(image);
        } else {
            frame_texture_->update(image);
        }
        frame_texture_serial_ = desc.frame_serial;
        if (!prefer_rd_texture) {
            frame_texture_backend_ = "image_texture";
        }
        return frame_texture_;
    }

    Dictionary debug_gpu_blend_self_test(const String &mode_name, int opacity) {
        Dictionary result;
        const uint32_t mode = BlendModeFromName(mode_name);
        if (mode == 0) {
            result["ok"] = false;
            result["error"] = "unknown blend mode";
            return result;
        }

        constexpr uint32_t kWidth = 8;
        constexpr uint32_t kHeight = 8;
        std::vector<uint32_t> src(kWidth * kHeight);
        std::vector<uint32_t> dst(kWidth * kHeight);
        std::vector<uint32_t> expected(kWidth * kHeight);
        for (uint32_t y = 0; y < kHeight; ++y) {
            for (uint32_t x = 0; x < kWidth; ++x) {
                const uint32_t i = y * kWidth + x;
                const uint32_t sa = (17u + x * 29u + y * 37u) & 0xffu;
                const uint32_t sr = (x * 41u + y * 11u + 3u) & 0xffu;
                const uint32_t sg = (x * 13u + y * 47u + 5u) & 0xffu;
                const uint32_t sb = (x * 7u + y * 31u + 9u) & 0xffu;
                const uint32_t da = (191u + x * 3u + y * 5u) & 0xffu;
                const uint32_t dr = (x * 19u + y * 23u + 101u) & 0xffu;
                const uint32_t dg = (x * 53u + y * 17u + 67u) & 0xffu;
                const uint32_t db = (x * 29u + y * 43u + 31u) & 0xffu;
                src[i] = sr | (sg << 8) | (sb << 16) | (sa << 24);
                dst[i] = dr | (dg << 8) | (db << 16) | (da << 24);
                expected[i] = CpuBlendReference(
                    mode, dst[i], src[i], opacity, 0x7f3366ccu);
            }
        }

        const uint64_t src_texture = BridgeCreateRgba(
            kWidth, kHeight, src.data(), kWidth * sizeof(uint32_t));
        const uint64_t dst_texture = BridgeCreateRgba(
            kWidth, kHeight, dst.data(), kWidth * sizeof(uint32_t));
        if (src_texture == 0 || dst_texture == 0) {
            if (src_texture != 0) BridgeReleaseTexture(src_texture);
            if (dst_texture != 0) BridgeReleaseTexture(dst_texture);
            result["ok"] = false;
            result["error"] = "failed to create debug textures";
            return result;
        }

        const tTVPRect rect(0, 0, static_cast<int>(kWidth), static_cast<int>(kHeight));
        const bool blended = BridgeBlendRect(dst_texture, src_texture, &rect, &rect,
                                            mode, opacity, 0x7f3366ccu);
        std::vector<uint32_t> actual(kWidth * kHeight);
        const bool read = BridgeReadRgba(dst_texture, actual.data(),
                                         actual.size() * sizeof(uint32_t),
                                         kWidth * sizeof(uint32_t));
        BridgeReleaseTexture(src_texture);
        BridgeReleaseTexture(dst_texture);

        int mismatches = 0;
        int first_index = -1;
        uint32_t first_expected = 0;
        uint32_t first_actual = 0;
        if (blended && read) {
            for (size_t i = 0; i < expected.size(); ++i) {
                if (expected[i] != actual[i]) {
                    if (first_index < 0) {
                        first_index = static_cast<int>(i);
                        first_expected = expected[i];
                        first_actual = actual[i];
                    }
                    mismatches += 1;
                }
            }
        }

        result["ok"] = blended && read && mismatches == 0;
        result["mode"] = mode_name;
        result["opacity"] = opacity;
        result["blended"] = blended;
        result["read"] = read;
        result["mismatches"] = mismatches;
        result["first_index"] = first_index;
        result["first_expected"] = static_cast<int64_t>(first_expected);
        result["first_actual"] = static_cast<int64_t>(first_actual);
        return result;
    }

    Dictionary debug_gpu_blend2_self_test(const String &mode_name, int opacity) {
        Dictionary result;
        const uint32_t mode = BlendModeFromName(mode_name);
        if (mode != TVP_GODOT_GPU_BLEND_CONST_ALPHA_SD &&
            mode != TVP_GODOT_GPU_BLEND_CONST_ALPHA_SD_D) {
            result["ok"] = false;
            result["error"] = "unknown blend2 mode";
            return result;
        }

        constexpr uint32_t kWidth = 8;
        constexpr uint32_t kHeight = 8;
        std::vector<uint32_t> src1(kWidth * kHeight);
        std::vector<uint32_t> src2(kWidth * kHeight);
        std::vector<uint32_t> dst(kWidth * kHeight, 0);
        std::vector<uint32_t> expected(kWidth * kHeight);
        for (uint32_t y = 0; y < kHeight; ++y) {
            for (uint32_t x = 0; x < kWidth; ++x) {
                const uint32_t i = y * kWidth + x;
                const uint32_t a1 = (19u + x * 31u + y * 17u) & 0xffu;
                const uint32_t r1 = (x * 23u + y * 7u + 11u) & 0xffu;
                const uint32_t g1 = (x * 5u + y * 43u + 13u) & 0xffu;
                const uint32_t b1 = (x * 37u + y * 3u + 17u) & 0xffu;
                const uint32_t a2 = (173u + x * 9u + y * 21u) & 0xffu;
                const uint32_t r2 = (x * 11u + y * 29u + 97u) & 0xffu;
                const uint32_t g2 = (x * 47u + y * 19u + 61u) & 0xffu;
                const uint32_t b2 = (x * 13u + y * 41u + 53u) & 0xffu;
                src1[i] = r1 | (g1 << 8) | (b1 << 16) | (a1 << 24);
                src2[i] = r2 | (g2 << 8) | (b2 << 16) | (a2 << 24);
                expected[i] = CpuBlend2Reference(mode, src1[i], src2[i],
                                                 opacity);
            }
        }

        const uint64_t src1_texture = BridgeCreateRgba(
            kWidth, kHeight, src1.data(), kWidth * sizeof(uint32_t));
        const uint64_t src2_texture = BridgeCreateRgba(
            kWidth, kHeight, src2.data(), kWidth * sizeof(uint32_t));
        const uint64_t dst_texture = BridgeCreateRgba(
            kWidth, kHeight, dst.data(), kWidth * sizeof(uint32_t));
        if (src1_texture == 0 || src2_texture == 0 || dst_texture == 0) {
            if (src1_texture != 0) BridgeReleaseTexture(src1_texture);
            if (src2_texture != 0) BridgeReleaseTexture(src2_texture);
            if (dst_texture != 0) BridgeReleaseTexture(dst_texture);
            result["ok"] = false;
            result["error"] = "failed to create debug textures";
            return result;
        }

        const tTVPRect rect(0, 0, static_cast<int>(kWidth), static_cast<int>(kHeight));
        const bool blended = BridgeBlendRect2(
            dst_texture, src1_texture, src2_texture, &rect, &rect, &rect,
            mode, opacity, 0);
        std::vector<uint32_t> actual(kWidth * kHeight);
        const bool read = BridgeReadRgba(dst_texture, actual.data(),
                                         actual.size() * sizeof(uint32_t),
                                         kWidth * sizeof(uint32_t));
        BridgeReleaseTexture(src1_texture);
        BridgeReleaseTexture(src2_texture);
        BridgeReleaseTexture(dst_texture);

        int mismatches = 0;
        int first_index = -1;
        uint32_t first_expected = 0;
        uint32_t first_actual = 0;
        if (blended && read) {
            for (size_t i = 0; i < expected.size(); ++i) {
                if (expected[i] != actual[i]) {
                    if (first_index < 0) {
                        first_index = static_cast<int>(i);
                        first_expected = expected[i];
                        first_actual = actual[i];
                    }
                    mismatches += 1;
                }
            }
        }

        result["ok"] = blended && read && mismatches == 0;
        result["mode"] = mode_name;
        result["opacity"] = opacity;
        result["blended"] = blended;
        result["read"] = read;
        result["mismatches"] = mismatches;
        result["first_index"] = first_index;
        result["first_expected"] = static_cast<int64_t>(first_expected);
        result["first_actual"] = static_cast<int64_t>(first_actual);
        return result;
    }

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("initialize_engine", "writable_path", "cache_path"),
                             &AetherKiriPlayer::initialize_engine);
        ClassDB::bind_method(D_METHOD("destroy_engine"),
                             &AetherKiriPlayer::destroy_engine);
        ClassDB::bind_method(D_METHOD("is_initialized"),
                             &AetherKiriPlayer::is_initialized);
        ClassDB::bind_method(D_METHOD("is_game_open"),
                             &AetherKiriPlayer::is_game_open);
        ClassDB::bind_method(D_METHOD("get_last_result"),
                             &AetherKiriPlayer::get_last_result);
        ClassDB::bind_method(D_METHOD("get_last_error"),
                             &AetherKiriPlayer::get_last_error);
        ClassDB::bind_method(D_METHOD("set_render_backend", "backend"),
                             &AetherKiriPlayer::set_render_backend);
        ClassDB::bind_method(D_METHOD("get_render_backend"),
                             &AetherKiriPlayer::get_render_backend);
        ClassDB::bind_method(D_METHOD("set_engine_option", "key", "value"),
                             &AetherKiriPlayer::set_engine_option);
        ClassDB::bind_method(D_METHOD("set_surface_size", "width", "height"),
                             &AetherKiriPlayer::set_surface_size);
        ClassDB::bind_method(D_METHOD("open_game", "game_root_path", "async"),
                             &AetherKiriPlayer::open_game, DEFVAL(true));
        ClassDB::bind_method(D_METHOD("tick", "delta_seconds"),
                             &AetherKiriPlayer::tick);
        ClassDB::bind_method(D_METHOD("pause"), &AetherKiriPlayer::pause);
        ClassDB::bind_method(D_METHOD("resume"), &AetherKiriPlayer::resume);
        ClassDB::bind_method(D_METHOD("send_pointer_event", "type", "pointer_id",
                                      "x", "y", "delta_x", "delta_y", "button"),
                             &AetherKiriPlayer::send_pointer_event);
        ClassDB::bind_method(D_METHOD("send_key_event", "pressed", "key_code",
                                      "modifiers", "unicode_codepoint"),
                             &AetherKiriPlayer::send_key_event);
        ClassDB::bind_method(D_METHOD("get_startup_state"),
                             &AetherKiriPlayer::get_startup_state);
        ClassDB::bind_method(D_METHOD("drain_startup_logs"),
                             &AetherKiriPlayer::drain_startup_logs);
        ClassDB::bind_method(D_METHOD("get_renderer_info"),
                             &AetherKiriPlayer::get_renderer_info);
        ClassDB::bind_method(D_METHOD("get_frame_texture_backend"),
                             &AetherKiriPlayer::get_frame_texture_backend);
        ClassDB::bind_method(D_METHOD("read_frame_rgba"),
                             &AetherKiriPlayer::read_frame_rgba);
        ClassDB::bind_method(D_METHOD("update_frame_texture"),
                             &AetherKiriPlayer::update_frame_texture);
        ClassDB::bind_method(D_METHOD("release_frame_texture"),
                             &AetherKiriPlayer::release_frame_texture);
        ClassDB::bind_method(D_METHOD("debug_gpu_blend_self_test", "mode", "opacity"),
                             &AetherKiriPlayer::debug_gpu_blend_self_test,
                             DEFVAL(255));
        ClassDB::bind_method(D_METHOD("debug_gpu_blend2_self_test", "mode", "opacity"),
                             &AetherKiriPlayer::debug_gpu_blend2_self_test,
                             DEFVAL(255));
    }

private:
    RenderingDevice *main_rendering_device() const {
        RenderingServer *server = RenderingServer::get_singleton();
        return server != nullptr ? server->get_rendering_device() : nullptr;
    }

    void release_imported_texture() {
        frame_imported_texture_.unref();
        if (frame_imported_rid_.is_valid()) {
            RenderingDevice *rd = main_rendering_device();
            if (rd != nullptr) {
                rd->free_rid(frame_imported_rid_);
            }
            frame_imported_rid_ = RID();
        }
        frame_imported_source_id_ = 0;
        frame_imported_width_ = 0;
        frame_imported_height_ = 0;
        if (frame_texture_backend_ == "godot_external_import" ||
            frame_texture_backend_ == "godot_native_gpu_bridge") {
            frame_texture_backend_ = "none";
        }
    }

    void release_rd_texture(bool free_rid) {
        release_imported_texture();
        frame_rd_texture_.unref();
        if (frame_rd_rid_.is_valid()) {
            if (free_rid) {
                auto op = std::make_shared<GodotGpuOp>();
                op->type = GodotGpuOp::Type::Release;
                op->dst = frame_rd_rid_;
                RunGodotGpuOpSync(op);
            }
            frame_rd_rid_ = RID();
        }
        frame_rd_width_ = 0;
        frame_rd_height_ = 0;
        if (frame_texture_backend_ == "rendering_device") {
            frame_texture_backend_ = "none";
        }
    }

    Ref<Texture2D> update_rd_texture(const engine_frame_desc_t &desc,
                                     const PackedByteArray &data) {
        RenderingDevice *rd = main_rendering_device();
        if (rd == nullptr) {
            return Ref<Texture2D>();
        }

        const bool needs_recreate =
            frame_rd_texture_.is_null() || !frame_rd_rid_.is_valid() ||
            frame_rd_width_ != desc.width || frame_rd_height_ != desc.height;
        if (needs_recreate) {
            release_rd_texture(false);

            Ref<RDTextureFormat> format;
            format.instantiate();
            format->set_format(RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM);
            format->set_width(desc.width);
            format->set_height(desc.height);
            format->set_depth(1);
            format->set_array_layers(1);
            format->set_mipmaps(1);
            format->set_texture_type(RenderingDevice::TEXTURE_TYPE_2D);
            format->set_samples(RenderingDevice::TEXTURE_SAMPLES_1);
            format->set_usage_bits(BitField<RenderingDevice::TextureUsageBits>(
                RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
                RenderingDevice::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT |
                RenderingDevice::TEXTURE_USAGE_CAN_UPDATE_BIT |
                RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT |
                RenderingDevice::TEXTURE_USAGE_CAN_COPY_TO_BIT));

            Ref<RDTextureView> view;
            view.instantiate();

            TypedArray<PackedByteArray> initial_data;
            initial_data.push_back(data);
            frame_rd_rid_ = rd->texture_create(format, view, initial_data);
            if (!frame_rd_rid_.is_valid()) {
                return Ref<Texture2D>();
            }

            frame_rd_texture_.instantiate();
            frame_rd_texture_->set_texture_rd_rid(frame_rd_rid_);
            frame_rd_width_ = desc.width;
            frame_rd_height_ = desc.height;
        } else {
            const Error error = rd->texture_update(frame_rd_rid_, 0, data);
            if (error != OK) {
                release_rd_texture(false);
                return Ref<Texture2D>();
            }
        }

        return frame_rd_texture_;
    }

    Ref<Texture2D> update_imported_gpu_bridge_texture(uint64_t texture_id,
                                                      uint32_t width,
                                                      uint32_t height) {
        RenderingDevice *rd = main_rendering_device();
        if (rd == nullptr || texture_id == 0 || width == 0 || height == 0) {
            return Ref<Texture2D>();
        }
        if (frame_imported_texture_.is_valid() &&
            frame_imported_rid_.is_valid() &&
            frame_imported_source_id_ == texture_id &&
            frame_imported_width_ == width &&
            frame_imported_height_ == height) {
            return frame_imported_texture_;
        }

        GodotGpuTextureRecord source;
        if (!ResolveBridgeTextureRecord(texture_id, source) ||
            !source.rid.is_valid()) {
            return Ref<Texture2D>();
        }

        const uint64_t native_handle = rd->texture_get_native_handle(source.rid);
        if (native_handle == 0) {
            return Ref<Texture2D>();
        }

        RID imported_rid = rd->texture_create_from_extension(
            RenderingDevice::TEXTURE_TYPE_2D,
            RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM,
            RenderingDevice::TEXTURE_SAMPLES_1,
            BitField<RenderingDevice::TextureUsageBits>(
                RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
                RenderingDevice::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT |
                RenderingDevice::TEXTURE_USAGE_STORAGE_BIT |
                RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT |
                RenderingDevice::TEXTURE_USAGE_CAN_COPY_TO_BIT),
            native_handle, width, height, 1, 1);
        if (!imported_rid.is_valid()) {
            return Ref<Texture2D>();
        }

        release_imported_texture();
        frame_imported_rid_ = imported_rid;
        frame_imported_texture_.instantiate();
        frame_imported_texture_->set_texture_rd_rid(frame_imported_rid_);
        frame_imported_source_id_ = texture_id;
        frame_imported_width_ = width;
        frame_imported_height_ = height;
        return frame_imported_texture_;
    }

    void update_last_error(engine_result_t result) {
        last_result_ = ResultToString(result);
        last_error_ = LastError(handle_);
    }

    engine_handle_t handle_ = nullptr;
    bool game_open_ = false;
    String backend_ = "Godot Native";
    String last_result_;
    String last_error_;
    String frame_texture_backend_ = "none";
    Ref<ImageTexture> frame_texture_;
    Ref<Texture2DRD> frame_rd_texture_;
    RID frame_rd_rid_;
    uint32_t frame_rd_width_ = 0;
    uint32_t frame_rd_height_ = 0;
    Ref<Texture2DRD> frame_imported_texture_;
    RID frame_imported_rid_;
    uint64_t frame_imported_source_id_ = 0;
    uint32_t frame_imported_width_ = 0;
    uint32_t frame_imported_height_ = 0;
    uint64_t frame_texture_serial_ = UINT64_MAX;
};

void InitializeAetherKiri(ModuleInitializationLevel level) {
    if (level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    ClassDB::register_class<AetherKiriPlayer>();
}

void DeinitializeAetherKiri(ModuleInitializationLevel level) {
    if (level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    BridgeFlush();
    ReleaseRemainingGodotGpuTextures();
    ReleaseGodotGpuPipeline();
    TVPGodotGpuBridgeRegister(nullptr);
}

} // namespace godot

extern "C" {

GDExtensionBool GDE_EXPORT aether_kiri_library_init(
    GDExtensionInterfaceGetProcAddress get_proc_address,
    GDExtensionClassLibraryPtr library,
    GDExtensionInitialization *initialization) {
    godot::GDExtensionBinding::InitObject init_obj(
        get_proc_address, library, initialization);
    init_obj.register_initializer(godot::InitializeAetherKiri);
    init_obj.register_terminator(godot::DeinitializeAetherKiri);
    init_obj.set_minimum_library_initialization_level(
        godot::MODULE_INITIALIZATION_LEVEL_SCENE);
    return init_obj.init();
}

engine_result_t aether_kiri_set_render_backend(engine_handle_t handle,
                                               const char *backend) {
    engine_option_t option{};
    option.key_utf8 = ENGINE_OPTION_RENDERER;
    if (backend == nullptr) {
        option.value_utf8 = ENGINE_RENDERER_GODOT_NATIVE;
    } else if (std::strcmp(backend, ENGINE_RENDER_BACKEND_GPU_BRIDGE) == 0 ||
               std::strcmp(backend, ENGINE_RENDERER_GPU_BRIDGE) == 0) {
        option.value_utf8 = ENGINE_RENDERER_GPU_BRIDGE;
    } else if (std::strcmp(backend, ENGINE_RENDER_BACKEND_DEBUG_CPU) == 0 ||
               std::strcmp(backend, ENGINE_RENDERER_DEBUG_CPU) == 0) {
        option.value_utf8 = ENGINE_RENDERER_DEBUG_CPU;
    } else {
        option.value_utf8 = ENGINE_RENDERER_GODOT_NATIVE;
    }
    return engine_set_option(handle, &option);
}

const char *aether_kiri_default_render_backend() {
    return ENGINE_RENDER_BACKEND_GODOT_NATIVE;
}

}
