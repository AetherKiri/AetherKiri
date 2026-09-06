#include "engine_api.h"
#include "engine_options.h"
#include "engine_runtime_provider.h"
#include "GodotGpuBridge.h"
#include "GodotGpuBarrierShadowPlanner.h"
#include "ComplexRect.h"
#include "RuntimeTickPacer.h"
#include "presentation/RuntimePresentationSprite.h"
#include "frame_effect_host.h"
#if defined(AETHERKIRI_WITH_ONSCRIPTER)
#include "onscripter_runtime.h"
#endif
#if defined(AETHERKIRI_WITH_MINORI)
extern "C" engine_result_t aetherkiri_minori_register_runtime_provider();
#endif
#if defined(__APPLE__)
#include "apple_external_texture.h"
#if !defined(IOS_ENABLED)
#include "apple_vulkan_external_texture.h"
#endif
#endif
#if defined(__ANDROID__)
#include "android_external_texture.h"
#endif

#include <godot_cpp/classes/audio_effect_panner.hpp>
#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/classes/audio_stream_ogg_vorbis.hpp>
#include <godot_cpp/classes/audio_stream_player.hpp>
#include <godot_cpp/classes/confirmation_dialog.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/java_class.hpp>
#include <godot_cpp/classes/java_class_wrapper.hpp>
#include <godot_cpp/classes/java_object.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/rd_pipeline_color_blend_state.hpp>
#include <godot_cpp/classes/rd_pipeline_color_blend_state_attachment.hpp>
#include <godot_cpp/classes/rd_pipeline_depth_stencil_state.hpp>
#include <godot_cpp/classes/rd_pipeline_multisample_state.hpp>
#include <godot_cpp/classes/rd_pipeline_rasterization_state.hpp>
#include <godot_cpp/classes/rd_sampler_state.hpp>
#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_texture_view.hpp>
#include <godot_cpp/classes/rd_shader_source.hpp>
#include <godot_cpp/classes/rd_shader_spirv.hpp>
#include <godot_cpp/classes/rd_uniform.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/texture2drd.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/vector2i.hpp>

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <iomanip>
#include <limits>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <mutex>

#if defined(__APPLE__)
extern "C" {
int32_t aether_storekit_start(const char *product_id);
uint64_t aether_storekit_refresh_entitlement(const char *product_id);
uint64_t aether_storekit_purchase(const char *product_id);
uint64_t aether_storekit_restore(const char *product_id);
char *aether_storekit_copy_state_json();
char *aether_storekit_copy_state_json_for_product(const char *product_id);
void aether_storekit_free_string(char *value);
int32_t aether_native_launch_file_picker_present(
    const char *title, const char *initial_directory);
int32_t aether_native_cover_file_picker_present(
    const char *title, const char *initial_directory,
    const char *destination_directory);
int32_t aether_native_translation_model_file_picker_present(
    const char *title, const char *initial_directory);
char *aether_native_translation_model_restore_path(
    const char *fallback_path);
char *aether_native_launch_file_picker_copy_result_json();
void aether_native_launch_file_picker_free_string(char *value);
}
#endif

#if defined(__ANDROID__)
#include <jni.h>
#include <android/log.h>

extern JNIEnv* krkr_GetJNIEnv();
extern jobject krkr_GetApplicationContext();
#endif

namespace godot {

#if defined(AETHERKIRI_INTERNAL_FRAME_EFFECTS)
void RegisterAetherInternalFrameEffects();
void UnregisterAetherInternalFrameEffects();
#endif

#if defined(AETHERKIRI_INTERNAL_UNLOCK_GATE)
int32_t AetherInternalVerifyUnlockSecret(const char *candidate_utf8);
#endif

namespace {

#if defined(__ANDROID__)
void AndroidBridgeLog(const char *format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    __android_log_write(ANDROID_LOG_INFO, "AetherKiriBridge", buffer);
}
#else
void AndroidBridgeLog(const char *, ...) {}
#endif

struct GodotGpuTextureRecord {
    RID rid;
    Ref<Texture2DRD> texture;
    uint32_t width = 0;
    uint32_t height = 0;
    // AlphaBlend_d dispatches are asynchronous. A following scratch-layer
    // clear must not rewrite this RID until the queued shader has sampled it.
    bool requires_alpha_d_clear_version = false;
    // Retained CVPixelBuffer backing an imported Metal texture. It must live
    // until the queued RID release reaches the render thread.
    void *apple_pixel_buffer = nullptr;
    // Owns the VkImage that directly imports the IOSurface on macOS.
    void *apple_vulkan_external_texture = nullptr;
    // Owns the VkImage, imported device memory, and retained AHardwareBuffer
    // backing an Android shared E-mote texture.
    void *android_external_texture = nullptr;
};

struct ArtemisGpuShaderImage {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels;
};

struct ArtemisGpuShaderTexture {
    std::string name;
    ArtemisGpuShaderImage image;
};

struct ArtemisGpuShaderConstant {
    std::string name;
    std::vector<float> values;
};

struct ArtemisGpuShaderRequest {
    std::string shader_id;
    std::string fragment_source;
    ArtemisGpuShaderImage foreground;
    ArtemisGpuShaderImage mask;
    bool mask_uses_alpha = false;
    float alpha = 1.0f;
    uint32_t color_multiply = 0xffffffffu;
    std::vector<ArtemisGpuShaderTexture> textures;
    std::vector<ArtemisGpuShaderConstant> constants;
    std::string error;
};

std::mutex g_gpu_textures_mutex;
std::unordered_map<uint64_t, GodotGpuTextureRecord> g_gpu_textures;
uint64_t g_next_gpu_texture_id = 1;
std::atomic<uint64_t> g_gpu_textures_created{0};
std::atomic<uint64_t> g_gpu_textures_released{0};
std::atomic<uint64_t> g_gpu_texture_bytes_created{0};
std::atomic<uint64_t> g_gpu_texture_bytes_released{0};
struct GodotGpuOp {
    enum class Type {
        Update,
        Clear,
        Copy,
        CopySelf,
        CopyTriangles,
        DrawTriangles,
        DrawMaskedTriangles,
        Mosaic,
        Read,
        ReadAsync,
        Blend,
        Blend2,
        Blend3,
        ArtemisShader,
        PrepareNativeWrite,
        PublishNativeWrite,
        ImportApplePixelBuffer,
        ImportAndroidHardwareBuffer,
        Release,
        Flush,
    };

    Type type = Type::Update;
    RID src;
    RID src2;
    RID src3;
    RID dst;
    PackedByteArray data;
    uint32_t profile_width = 0;
    uint32_t profile_height = 0;
    double profile_pack_ms = 0.0;
    std::chrono::steady_clock::time_point profile_enqueued_at{};
    std::shared_ptr<ArtemisGpuShaderRequest> artemis_shader;
    std::vector<float> vertices;
    Color clear_color;
    Vector3 src_pos;
    Vector3 src2_pos;
    Vector3 src3_pos;
    Vector3 dst_pos;
    Vector3 size;
    Vector3 src_size;
    uint32_t mode = 0;
    int opacity = 255;
    uint32_t color = 0xffffffffu;
    bool preserve_minified_detail = false;
    bool result = false;
    bool done = false;
    uint64_t readback_request = 0;
    void *native_image = nullptr;
    void *native_resource = nullptr;
    uint32_t native_width = 0;
    uint32_t native_height = 0;
    uint64_t imported_texture = 0;
    uint64_t queue_sequence = 0;
    // Timestamp assigned when the operation enters the bridge queue.  This is
    // intentionally separate from profile_enqueued_at, which is also used to
    // measure the CPU packing interval for texture updates.
    std::chrono::steady_clock::time_point profile_submitted_at{};
    std::mutex done_mutex;
    std::condition_variable done_cv;
};

const char *GodotGpuOpTypeName(GodotGpuOp::Type type) {
    switch (type) {
        case GodotGpuOp::Type::Update: return "update";
        case GodotGpuOp::Type::Clear: return "clear";
        case GodotGpuOp::Type::Copy: return "copy";
        case GodotGpuOp::Type::CopySelf: return "copy_self";
        case GodotGpuOp::Type::CopyTriangles: return "copy_triangles";
        case GodotGpuOp::Type::DrawTriangles: return "draw_triangles";
        case GodotGpuOp::Type::DrawMaskedTriangles: return "draw_masked_triangles";
        case GodotGpuOp::Type::Mosaic: return "mosaic";
        case GodotGpuOp::Type::Read: return "read";
        case GodotGpuOp::Type::ReadAsync: return "read_async";
        case GodotGpuOp::Type::Blend: return "blend";
        case GodotGpuOp::Type::Blend2: return "blend2";
        case GodotGpuOp::Type::Blend3: return "blend3";
        case GodotGpuOp::Type::ArtemisShader: return "artemis_shader";
        case GodotGpuOp::Type::PrepareNativeWrite: return "prepare_native_write";
        case GodotGpuOp::Type::PublishNativeWrite: return "publish_native_write";
        case GodotGpuOp::Type::ImportApplePixelBuffer: return "import_apple_pixel_buffer";
        case GodotGpuOp::Type::ImportAndroidHardwareBuffer: return "import_android_hardware_buffer";
        case GodotGpuOp::Type::Release: return "release";
        case GodotGpuOp::Type::Flush: return "flush";
    }
    return "unknown";
}

bool GodotGpuOpProfileEnabled() {
    static const bool enabled = [] {
        const char *value = std::getenv("AETHERKIRI_GODOT_GPU_OP_PROFILE");
        return value != nullptr && value[0] != '\0' &&
               std::strcmp(value, "0") != 0;
    }();
    return enabled;
}

double GodotGpuOpProfileSlowMs() {
    static const double threshold = [] {
        const char *value = std::getenv("AETHERKIRI_GODOT_GPU_OP_SLOW_MS");
        if (value == nullptr || value[0] == '\0') return 5.0;
        char *end = nullptr;
        const double parsed = std::strtod(value, &end);
        return end != value && std::isfinite(parsed) && parsed > 0.0
                   ? parsed
                   : 5.0;
    }();
    return threshold;
}

void LogGodotGpuOpProfile(const char *phase,
                          const GodotGpuOp *op,
                          double elapsed_ms) {
    if (!GodotGpuOpProfileEnabled() || op == nullptr ||
        elapsed_ms < GodotGpuOpProfileSlowMs()) {
        return;
    }
    const uint32_t width = op->profile_width != 0
        ? op->profile_width
        : static_cast<uint32_t>(std::max(0.0f, op->size.x));
    const uint32_t height = op->profile_height != 0
        ? op->profile_height
        : static_cast<uint32_t>(std::max(0.0f, op->size.y));
    double queue_ms = 0.0;
    if (op->profile_submitted_at.time_since_epoch().count() != 0) {
        queue_ms = std::chrono::duration<double, std::milli>(
                       std::chrono::steady_clock::now() -
                       op->profile_submitted_at)
                       .count();
    }
    RenderingServer *server = RenderingServer::get_singleton();
    std::ostringstream message;
    message << std::fixed << std::setprecision(3)
            << "godot gpu op profile: phase="
            << (phase != nullptr ? phase : "unknown")
            << " type=" << GodotGpuOpTypeName(op->type)
            << " elapsed_ms=" << elapsed_ms
            << " queue_ms=" << queue_ms
            << " size=" << width << "x" << height
            << " render_thread="
            << (server != nullptr && server->is_on_render_thread() ? 1 : 0);
    UtilityFunctions::print(String(message.str().c_str()));
}

class GodotGpuOpExecutionProfile final {
public:
    explicit GodotGpuOpExecutionProfile(
        const std::shared_ptr<GodotGpuOp> &op)
        : op_(op.get()), enabled_(GodotGpuOpProfileEnabled()),
          started_(enabled_ ? std::chrono::steady_clock::now()
                            : std::chrono::steady_clock::time_point{}) {}

    ~GodotGpuOpExecutionProfile() {
        if (!enabled_) return;
        const double elapsed_ms = std::chrono::duration<double, std::milli>(
                                      std::chrono::steady_clock::now() - started_)
                                      .count();
        LogGodotGpuOpProfile("execute", op_, elapsed_ms);
    }

private:
    const GodotGpuOp *op_ = nullptr;
    bool enabled_ = false;
    std::chrono::steady_clock::time_point started_{};
};

class GodotGpuOpWaitProfile final {
public:
    GodotGpuOpWaitProfile(const std::shared_ptr<GodotGpuOp> &op, bool wait)
        : op_(op.get()), enabled_(wait && GodotGpuOpProfileEnabled()),
          started_(enabled_ ? std::chrono::steady_clock::now()
                            : std::chrono::steady_clock::time_point{}) {}

    ~GodotGpuOpWaitProfile() {
        if (!enabled_) return;
        const double elapsed_ms = std::chrono::duration<double, std::milli>(
                                      std::chrono::steady_clock::now() - started_)
                                      .count();
        LogGodotGpuOpProfile("wait", op_, elapsed_ms);
    }

private:
    const GodotGpuOp *op_ = nullptr;
    bool enabled_ = false;
    std::chrono::steady_clock::time_point started_{};
};

struct GodotGpuReadbackRequest {
    std::shared_ptr<GodotGpuOp> op;
    uint32_t width = 0;
    uint32_t height = 0;
};

std::mutex g_gpu_readbacks_mutex;
std::unordered_map<uint64_t, GodotGpuReadbackRequest> g_gpu_readbacks;
uint64_t g_next_gpu_readback_id = 1;

std::mutex g_gpu_op_queue_mutex;
std::deque<std::shared_ptr<GodotGpuOp>> g_gpu_op_queue;
bool g_gpu_op_drain_scheduled = false;
uint64_t g_next_gpu_op_sequence = 1;
uint64_t g_last_gpu_op_sequence = 0;
uint64_t g_gpu_batch_token = 0;
uint64_t g_next_gpu_batch_token = 1;
uint32_t g_gpu_batch_depth = 0;
std::thread::id g_gpu_batch_owner;
std::atomic<uint64_t> g_gpu_op_submitted{0};
std::atomic<uint64_t> g_gpu_op_completed{0};
std::atomic<uint64_t> g_gpu_op_failed{0};
std::atomic<uint64_t> g_gpu_copy_failed{0};
std::atomic<uint64_t> g_gpu_copy_triangles_failed{0};
std::atomic<uint64_t> g_gpu_detail_minify_ops{0};
std::atomic<uint64_t> g_gpu_native_character_aa_ops{0};
std::atomic<uint64_t> g_gpu_triangle_pipeline_failed{0};
std::atomic<uint64_t> g_gpu_triangle_buffer_failed{0};
std::atomic<uint64_t> g_gpu_triangle_uniform_failed{0};
std::atomic<uint64_t> g_gpu_blend_op_submitted{0};
std::atomic<uint64_t> g_gpu_queue_peak{0};
std::atomic<uint64_t> g_gpu_barriers{0};
std::atomic<uint64_t> g_gpu_alias_sources{0};
std::atomic<uint64_t> g_gpu_sync_timeouts{0};
std::atomic<uint64_t> g_gpu_alpha_d_clear_versions{0};
std::atomic<uint64_t> g_gpu_batch_started{0};
std::atomic<uint64_t> g_gpu_batch_ended{0};
std::atomic<uint64_t> g_gpu_batch_rejected{0};
std::atomic<uint64_t> g_gpu_batch_ops{0};
std::atomic<uint64_t> g_gpu_compute_batches{0};
std::atomic<uint64_t> g_gpu_compute_batch_ops{0};
std::atomic<uint64_t> g_gpu_nonlive_compute_ops{0};
std::atomic<uint64_t> g_gpu_nonlive_compute_barriers{0};
std::atomic<uint64_t> g_gpu_live2d_raster_batches{0};
std::atomic<uint64_t> g_gpu_live2d_raster_ops{0};
std::atomic<uint64_t> g_gpu_predicted_compute_barriers{0};
std::atomic<uint64_t> g_gpu_predicted_raw_hazards{0};
std::atomic<uint64_t> g_gpu_predicted_waw_hazards{0};
std::atomic<uint64_t> g_gpu_predicted_war_hazards{0};
std::atomic_bool g_frame_enhancement_detail_sampling{false};

constexpr uint32_t kGodotGpuPreserveMinifiedDetail = 0x40000000u;

constexpr auto kGodotGpuSyncWaitTimeout = std::chrono::milliseconds(900);

bool GodotGpuTextureProfileEnabled() {
    static const bool enabled = [] {
        const char *value =
            std::getenv("AETHERKIRI_GODOT_TEXTURE_PROFILE");
        if (value != nullptr && value[0] != '\0') {
            return std::strcmp(value, "0") != 0;
        }
        value = std::getenv("AETHERKIRI_MOTION_RENDER_PROFILE");
        return value != nullptr && value[0] != '\0' &&
               std::strcmp(value, "0") != 0;
    }();
    return enabled;
}

double GodotGpuTextureProfileSlowMs() {
    static const double threshold = [] {
        const char *value =
            std::getenv("AETHERKIRI_GODOT_TEXTURE_SLOW_MS");
        if (value == nullptr || value[0] == '\0') return 5.0;
        char *end = nullptr;
        const double parsed = std::strtod(value, &end);
        return end != value && std::isfinite(parsed) && parsed > 0.0
                   ? parsed
                   : 5.0;
    }();
    return threshold;
}

void LogGodotGpuUpdateProfile(const char *phase, uint32_t width,
                              uint32_t height, size_t bytes, double elapsed_ms,
                              double pack_ms, double queue_ms, bool result,
                              bool on_render_thread) {
    if (!GodotGpuTextureProfileEnabled() ||
        elapsed_ms < GodotGpuTextureProfileSlowMs()) {
        return;
    }
    std::ostringstream message;
    message << std::fixed << std::setprecision(3)
            << "godot gpu update profile: phase="
            << (phase != nullptr ? phase : "unknown")
            << " elapsed_ms=" << elapsed_ms << " pack_ms=" << pack_ms
            << " queue_ms=" << queue_ms << " size=" << width << "x"
            << height << " bytes=" << bytes << " result="
            << (result ? 1 : 0) << " render_thread="
            << (on_render_thread ? 1 : 0);
    UtilityFunctions::print(String(message.str().c_str()));
}

bool GodotGpuBarrierShadowEnabled() {
    static const bool enabled = [] {
        const char *value =
            std::getenv("AETHERKIRI_GODOT_SHADOW_BARRIERS");
        return value != nullptr && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}

bool Live2DHardwareRasterEnabled() {
    static const bool enabled = [] {
        const char *value =
            std::getenv("AETHERKIRI_LIVE2D_HARDWARE_RASTER");
        if (value != nullptr && value[0] != '\0') {
            return std::strcmp(value, "0") != 0;
        }
        // Apple and Android both use the Godot RenderingDevice path for Live2D
        // triangle rasterization. Keep the opt-out available for driver
        // diagnostics.
#if defined(__APPLE__) || defined(__ANDROID__)
        return true;
#else
        return false;
#endif
    }();
    return enabled;
}

void UpdateGpuQueuePeak(size_t value);

void EnqueueGodotGpuOpLocked(const std::shared_ptr<GodotGpuOp> &op) {
    if (op == nullptr) return;
    uint64_t sequence = g_next_gpu_op_sequence++;
    if (sequence == 0) sequence = g_next_gpu_op_sequence++;
    op->queue_sequence = sequence;
    g_last_gpu_op_sequence = sequence;
    g_gpu_op_queue.push_back(op);
    UpdateGpuQueuePeak(g_gpu_op_queue.size());
}

#if defined(__ANDROID__)
constexpr int kAndroidFlagActivityNewTask = 0x10000000;
constexpr int kAndroidPermissionGranted = 0;
constexpr int kAndroidStoragePermissionRequestCode = 1301;

void AndroidLogPrintf(const char *level, const char *format, ...) {
#if !defined(NDEBUG)
    char buffer[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    UtilityFunctions::print(String("[AetherKiri/android/") +
                            String(level != nullptr ? level : "info") +
                            String("] ") + String(buffer));
#else
    (void)level;
    (void)format;
#endif
}

#define AK_ANDROID_LOGI(...) AndroidLogPrintf("info", __VA_ARGS__)
#define AK_ANDROID_LOGW(...) AndroidLogPrintf("warn", __VA_ARGS__)

struct AndroidGodotStoragePermissionState {
    bool read = false;
    bool write = false;
    bool manage = false;
};

AndroidGodotStoragePermissionState AndroidGetGodotStoragePermissionState() {
    AndroidGodotStoragePermissionState state;
    OS *os = OS::get_singleton();
    if (os == nullptr) {
        AK_ANDROID_LOGW("storage permission Godot OS unavailable");
        return state;
    }

    const PackedStringArray granted = os->get_granted_permissions();
    state.read = granted.has(String("android.permission.READ_EXTERNAL_STORAGE"));
    state.write = granted.has(String("android.permission.WRITE_EXTERNAL_STORAGE"));
    state.manage = granted.has(String("android.permission.MANAGE_EXTERNAL_STORAGE"));
    AK_ANDROID_LOGI(
        "storage permission Godot OS state read=%d write=%d manage=%d count=%d",
        state.read ? 1 : 0, state.write ? 1 : 0, state.manage ? 1 : 0,
        granted.size());
    return state;
}

JavaClassWrapper *AndroidJavaWrapper() {
    JavaClassWrapper *wrapper = JavaClassWrapper::get_singleton();
    if (wrapper == nullptr) {
        AK_ANDROID_LOGW("storage permission Java bridge unavailable: no JavaClassWrapper");
    }
    return wrapper;
}

bool AndroidJavaHasException(JavaClassWrapper *wrapper, const char *stage) {
    if (wrapper == nullptr) {
        return false;
    }
    Ref<JavaObject> exception = wrapper->get_exception();
    if (exception.is_valid()) {
        AK_ANDROID_LOGW("storage permission Java bridge exception at %s",
                        stage != nullptr ? stage : "unknown");
        return true;
    }
    return false;
}

Object *AndroidVariantObject(const Variant &value) {
    if (value.get_type() != Variant::OBJECT) {
        return nullptr;
    }
    return static_cast<Object *>(value);
}

int AndroidHasExternalStoragePermissionViaGodotJava() {
    JavaClassWrapper *wrapper = AndroidJavaWrapper();
    if (wrapper == nullptr) {
        return -1;
    }

    Ref<JavaClass> environment = wrapper->wrap("android.os.Environment");
    if (environment.is_null() || AndroidJavaHasException(wrapper, "wrap Environment")) {
        AK_ANDROID_LOGW("storage permission Java bridge failed: Environment unavailable");
        return -1;
    }

    Variant result = environment->call("isExternalStorageManager");
    if (AndroidJavaHasException(wrapper, "Environment.isExternalStorageManager")) {
        return -1;
    }
    if (result.get_type() != Variant::BOOL &&
        result.get_type() != Variant::INT) {
        AK_ANDROID_LOGW("storage permission Java bridge failed: unexpected permission result type=%d",
                        static_cast<int>(result.get_type()));
        return -1;
    }

    const bool granted = result.get_type() == Variant::BOOL
        ? static_cast<bool>(result)
        : static_cast<int64_t>(result) != 0;
    AK_ANDROID_LOGI("storage permission Java bridge granted=%d", granted ? 1 : 0);
    return granted ? 1 : 0;
}

Object *AndroidGetApplicationContextViaGodotJava(JavaClassWrapper *wrapper,
                                                Variant &app_context_owner) {
    if (wrapper == nullptr) {
        return nullptr;
    }

    Ref<JavaClass> activity_thread = wrapper->wrap("android.app.ActivityThread");
    if (activity_thread.is_null() ||
        AndroidJavaHasException(wrapper, "wrap ActivityThread")) {
        AK_ANDROID_LOGW("storage permission Java bridge failed: ActivityThread unavailable");
        return nullptr;
    }

    app_context_owner = activity_thread->call("currentApplication");
    if (AndroidJavaHasException(wrapper, "ActivityThread.currentApplication")) {
        return nullptr;
    }
    Object *context = AndroidVariantObject(app_context_owner);
    if (context == nullptr) {
        AK_ANDROID_LOGW("storage permission Java bridge failed: currentApplication returned no object");
    }
    return context;
}

bool AndroidStartSettingsIntentViaGodotJava(JavaClassWrapper *wrapper,
                                           const char *action,
                                           bool include_package_uri) {
    if (wrapper == nullptr || action == nullptr) {
        return false;
    }

    Variant context_owner;
    Object *context = AndroidGetApplicationContextViaGodotJava(
        wrapper, context_owner);
    if (context == nullptr) {
        return false;
    }

    Variant package_name = context->call("getPackageName");
    if (AndroidJavaHasException(wrapper, "Context.getPackageName") ||
        package_name.get_type() != Variant::STRING) {
        AK_ANDROID_LOGW("storage permission Java bridge failed: package name unavailable");
        return false;
    }

    Ref<JavaClass> intent_class = wrapper->wrap("android.content.Intent");
    if (intent_class.is_null() || AndroidJavaHasException(wrapper, "wrap Intent")) {
        AK_ANDROID_LOGW("storage permission Java bridge failed: Intent unavailable");
        return false;
    }

    Variant intent_owner = intent_class->call("new");
    if (AndroidJavaHasException(wrapper, "Intent.new()") ||
        intent_owner.get_type() != Variant::OBJECT ||
        AndroidVariantObject(intent_owner) == nullptr) {
        AK_ANDROID_LOGW("storage permission Java bridge failed: empty intent create failed action=%s",
                        action);
        return false;
    }

    Object *intent = AndroidVariantObject(intent_owner);
    intent->call("setAction", String(action));
    if (AndroidJavaHasException(wrapper, "Intent.setAction")) {
        AK_ANDROID_LOGW("storage permission Java bridge failed: setAction failed action=%s",
                        action);
        return false;
    }

    if (include_package_uri) {
        Ref<JavaClass> uri_class = wrapper->wrap("android.net.Uri");
        if (uri_class.is_valid() &&
            !AndroidJavaHasException(wrapper, "wrap Uri")) {
            const String uri_text = String("package:") + String(package_name);
            Variant uri = uri_class->call("parse", uri_text);
            if (!AndroidJavaHasException(wrapper, "Uri.parse") &&
                AndroidVariantObject(uri) != nullptr) {
                intent->call("setData", uri);
                AndroidJavaHasException(wrapper, "Intent.setData");
            }
        }
    }

    intent->call("addFlags", kAndroidFlagActivityNewTask);
    AndroidJavaHasException(wrapper, "Intent.addFlags");

    context->call("startActivity", intent_owner);
    if (AndroidJavaHasException(wrapper, "Context.startActivity")) {
        AK_ANDROID_LOGW("storage permission Java bridge failed: startActivity failed action=%s",
                        action);
        return false;
    }

    AK_ANDROID_LOGI("storage permission Java bridge started settings action=%s include_package_uri=%d",
                    action, include_package_uri ? 1 : 0);
    return true;
}

bool AndroidRequestExternalStoragePermissionViaGodotJava() {
    JavaClassWrapper *wrapper = AndroidJavaWrapper();
    if (wrapper == nullptr) {
        return false;
    }

    bool ok = AndroidStartSettingsIntentViaGodotJava(
        wrapper, "android.settings.MANAGE_APP_ALL_FILES_ACCESS_PERMISSION",
        true);
    if (!ok) {
        ok = AndroidStartSettingsIntentViaGodotJava(
            wrapper, "android.settings.MANAGE_ALL_FILES_ACCESS_PERMISSION",
            false);
    }
    AK_ANDROID_LOGI("storage permission Java bridge request result=%d", ok ? 1 : 0);
    return ok;
}

void AndroidClearJniException(JNIEnv *env) {
    if (env != nullptr && env->ExceptionCheck()) {
        env->ExceptionClear();
    }
}

int AndroidGetSdkInt();

void AndroidDeleteLocalRef(JNIEnv *env, jobject ref) {
    if (env != nullptr && ref != nullptr) {
        env->DeleteLocalRef(ref);
    }
}

jobject AndroidGetApplicationContextLocal(JNIEnv *env) {
    if (env == nullptr) {
        return nullptr;
    }

    jobject context = krkr_GetApplicationContext();
    if (context != nullptr) {
        return env->NewLocalRef(context);
    }

    jclass activity_thread_class = env->FindClass("android/app/ActivityThread");
    if (activity_thread_class == nullptr) {
        AndroidClearJniException(env);
        return nullptr;
    }

    jmethodID current_application = env->GetStaticMethodID(
        activity_thread_class, "currentApplication",
        "()Landroid/app/Application;");
    if (current_application == nullptr) {
        AndroidClearJniException(env);
        env->DeleteLocalRef(activity_thread_class);
        return nullptr;
    }

    jobject app = env->CallStaticObjectMethod(activity_thread_class,
                                             current_application);
    env->DeleteLocalRef(activity_thread_class);
    if (env->ExceptionCheck() || app == nullptr) {
        AndroidClearJniException(env);
        return nullptr;
    }
    return app;
}

jclass AndroidFindClassWithAppClassLoader(JNIEnv *env, const char *class_name) {
    if (env == nullptr || class_name == nullptr) {
        return nullptr;
    }

    jclass cls = env->FindClass(class_name);
    if (cls != nullptr && !env->ExceptionCheck()) {
        return cls;
    }
    AndroidClearJniException(env);

    jobject app_context = AndroidGetApplicationContextLocal(env);
    if (app_context == nullptr) {
        return nullptr;
    }

    jclass context_class = env->FindClass("android/content/Context");
    if (context_class == nullptr) {
        AndroidClearJniException(env);
        env->DeleteLocalRef(app_context);
        return nullptr;
    }

    jmethodID get_class_loader = env->GetMethodID(
        context_class, "getClassLoader", "()Ljava/lang/ClassLoader;");
    if (get_class_loader == nullptr) {
        AndroidClearJniException(env);
        env->DeleteLocalRef(context_class);
        env->DeleteLocalRef(app_context);
        return nullptr;
    }

    jobject class_loader = env->CallObjectMethod(app_context, get_class_loader);
    env->DeleteLocalRef(context_class);
    env->DeleteLocalRef(app_context);
    if (env->ExceptionCheck() || class_loader == nullptr) {
        AndroidClearJniException(env);
        return nullptr;
    }

    jclass class_loader_class = env->FindClass("java/lang/ClassLoader");
    if (class_loader_class == nullptr) {
        AndroidClearJniException(env);
        env->DeleteLocalRef(class_loader);
        return nullptr;
    }

    jmethodID load_class = env->GetMethodID(
        class_loader_class, "loadClass",
        "(Ljava/lang/String;)Ljava/lang/Class;");
    if (load_class == nullptr) {
        AndroidClearJniException(env);
        env->DeleteLocalRef(class_loader_class);
        env->DeleteLocalRef(class_loader);
        return nullptr;
    }

    std::string dotted_name(class_name);
    for (char &ch : dotted_name) {
        if (ch == '/') {
            ch = '.';
        }
    }
    jstring java_class_name = env->NewStringUTF(dotted_name.c_str());
    jobject class_object = env->CallObjectMethod(class_loader, load_class,
                                                java_class_name);
    AndroidDeleteLocalRef(env, java_class_name);
    env->DeleteLocalRef(class_loader_class);
    env->DeleteLocalRef(class_loader);
    if (env->ExceptionCheck() || class_object == nullptr) {
        AndroidClearJniException(env);
        return nullptr;
    }
    return static_cast<jclass>(class_object);
}

jobject AndroidGetGodotActivityLocal(JNIEnv *env) {
    if (env == nullptr) {
        return nullptr;
    }

    jobject app_context = AndroidGetApplicationContextLocal(env);
    if (app_context == nullptr) {
        return nullptr;
    }

    jclass godot_class = AndroidFindClassWithAppClassLoader(
        env, "org/godotengine/godot/Godot");
    if (godot_class == nullptr) {
        env->DeleteLocalRef(app_context);
        return nullptr;
    }

    jmethodID get_instance = env->GetStaticMethodID(
        godot_class, "getInstance",
        "(Landroid/content/Context;)Lorg/godotengine/godot/Godot;");
    if (get_instance == nullptr) {
        AndroidClearJniException(env);
        env->DeleteLocalRef(godot_class);
        env->DeleteLocalRef(app_context);
        return nullptr;
    }

    jobject godot = env->CallStaticObjectMethod(godot_class, get_instance,
                                                app_context);
    env->DeleteLocalRef(app_context);
    if (env->ExceptionCheck() || godot == nullptr) {
        AndroidClearJniException(env);
        env->DeleteLocalRef(godot_class);
        return nullptr;
    }

    jmethodID get_activity = env->GetMethodID(
        godot_class, "getActivity", "()Landroid/app/Activity;");
    if (get_activity == nullptr) {
        AndroidClearJniException(env);
        env->DeleteLocalRef(godot);
        env->DeleteLocalRef(godot_class);
        return nullptr;
    }

    jobject activity = env->CallObjectMethod(godot, get_activity);
    env->DeleteLocalRef(godot);
    env->DeleteLocalRef(godot_class);
    if (env->ExceptionCheck() || activity == nullptr) {
        AndroidClearJniException(env);
        return nullptr;
    }
    return activity;
}

int AndroidGetSdkInt() {
    JNIEnv *env = krkr_GetJNIEnv();
    if (env == nullptr) {
        return 0;
    }

    jclass version_class = env->FindClass("android/os/Build$VERSION");
    if (version_class == nullptr) {
        AndroidClearJniException(env);
        return 0;
    }

    jfieldID sdk_int = env->GetStaticFieldID(version_class, "SDK_INT", "I");
    if (sdk_int == nullptr) {
        AndroidClearJniException(env);
        env->DeleteLocalRef(version_class);
        return 0;
    }

    const int result = env->GetStaticIntField(version_class, sdk_int);
    AndroidClearJniException(env);
    env->DeleteLocalRef(version_class);
    return result;
}

bool AndroidHasRuntimePermission(const char *permission) {
    JNIEnv *env = krkr_GetJNIEnv();
    if (env == nullptr || permission == nullptr) {
        return false;
    }

    jobject context = AndroidGetGodotActivityLocal(env);
    if (context == nullptr) {
        context = AndroidGetApplicationContextLocal(env);
    }
    if (context == nullptr) {
        return false;
    }

    jclass context_class = env->FindClass("android/content/Context");
    if (context_class == nullptr) {
        AndroidClearJniException(env);
        env->DeleteLocalRef(context);
        return false;
    }

    jmethodID check_self_permission = env->GetMethodID(
        context_class, "checkSelfPermission", "(Ljava/lang/String;)I");
    if (check_self_permission == nullptr) {
        AndroidClearJniException(env);
        env->DeleteLocalRef(context_class);
        env->DeleteLocalRef(context);
        return false;
    }

    jstring permission_string = env->NewStringUTF(permission);
    if (permission_string == nullptr) {
        AndroidClearJniException(env);
        env->DeleteLocalRef(context_class);
        env->DeleteLocalRef(context);
        return false;
    }

    const jint result = env->CallIntMethod(context, check_self_permission,
                                          permission_string);
    const bool ok =
        !env->ExceptionCheck() && result == kAndroidPermissionGranted;
    AK_ANDROID_LOGI("storage permission check permission=%s result=%d granted=%d",
                    permission, static_cast<int>(result), ok ? 1 : 0);
    AndroidClearJniException(env);
    env->DeleteLocalRef(permission_string);
    env->DeleteLocalRef(context_class);
    env->DeleteLocalRef(context);
    return ok;
}

bool AndroidRequestRuntimeStoragePermissions() {
    JNIEnv *env = krkr_GetJNIEnv();
    if (env == nullptr) {
        AK_ANDROID_LOGW("storage permission request failed: no JNI env");
        return false;
    }

    jobject activity = AndroidGetGodotActivityLocal(env);
    if (activity == nullptr) {
        AK_ANDROID_LOGW("storage permission request failed: no activity");
        return false;
    }

    jclass activity_class = env->FindClass("android/app/Activity");
    if (activity_class == nullptr) {
        AndroidClearJniException(env);
        env->DeleteLocalRef(activity);
        AK_ANDROID_LOGW("storage permission request failed: Activity unavailable");
        return false;
    }

    jmethodID request_permissions = env->GetMethodID(
        activity_class, "requestPermissions", "([Ljava/lang/String;I)V");
    jclass string_class = env->FindClass("java/lang/String");
    if (request_permissions == nullptr || string_class == nullptr) {
        AndroidClearJniException(env);
        AndroidDeleteLocalRef(env, string_class);
        env->DeleteLocalRef(activity_class);
        env->DeleteLocalRef(activity);
        AK_ANDROID_LOGW("storage permission request failed: requestPermissions unavailable");
        return false;
    }

    constexpr const char *permissions[] = {
        "android.permission.READ_EXTERNAL_STORAGE",
        "android.permission.WRITE_EXTERNAL_STORAGE",
    };
    constexpr jsize permission_count =
        static_cast<jsize>(sizeof(permissions) / sizeof(permissions[0]));
    jobjectArray permission_array = env->NewObjectArray(
        permission_count, string_class, nullptr);
    if (permission_array == nullptr) {
        AndroidClearJniException(env);
        env->DeleteLocalRef(string_class);
        env->DeleteLocalRef(activity_class);
        env->DeleteLocalRef(activity);
        AK_ANDROID_LOGW("storage permission request failed: permission array unavailable");
        return false;
    }

    for (jsize index = 0; index < permission_count; ++index) {
        jstring permission = env->NewStringUTF(permissions[index]);
        if (permission == nullptr) {
            AndroidClearJniException(env);
            env->DeleteLocalRef(permission_array);
            env->DeleteLocalRef(string_class);
            env->DeleteLocalRef(activity_class);
            env->DeleteLocalRef(activity);
            AK_ANDROID_LOGW("storage permission request failed: permission string unavailable");
            return false;
        }
        env->SetObjectArrayElement(permission_array, index, permission);
        env->DeleteLocalRef(permission);
    }

    env->CallVoidMethod(activity, request_permissions, permission_array,
                        kAndroidStoragePermissionRequestCode);
    const bool ok = !env->ExceptionCheck();
    AK_ANDROID_LOGI(
        "storage permission runtime request sdk=%d permissions=READ,WRITE dispatched=%d",
        AndroidGetSdkInt(), ok ? 1 : 0);
    AndroidClearJniException(env);
    env->DeleteLocalRef(permission_array);
    env->DeleteLocalRef(string_class);
    env->DeleteLocalRef(activity_class);
    env->DeleteLocalRef(activity);
    AK_ANDROID_LOGI("storage permission runtime request result=%d", ok ? 1 : 0);
    return ok;
}

bool AndroidHasExternalStoragePermission() {
    const AndroidGodotStoragePermissionState godot_state =
        AndroidGetGodotStoragePermissionState();
    if (godot_state.read || godot_state.write || godot_state.manage) {
        AK_ANDROID_LOGI("storage permission effective granted via Godot OS");
        return true;
    }

    const int sdk = AndroidGetSdkInt();
    if (sdk > 0 && sdk < 23) {
        return true;
    }
    if (sdk > 0 && sdk < 30) {
        AK_ANDROID_LOGI(
            "storage permission state sdk=%d read=%d write=%d effective_read=0",
            sdk, godot_state.read ? 1 : 0, godot_state.write ? 1 : 0);
        return false;
    }
    if (sdk <= 0) {
        AK_ANDROID_LOGW(
            "storage permission state unavailable: Android SDK/JNI not ready");
        return false;
    }

    const int java_result = AndroidHasExternalStoragePermissionViaGodotJava();
    if (java_result >= 0) {
        return java_result != 0;
    }

    JNIEnv *env = krkr_GetJNIEnv();
    if (env == nullptr) {
        return false;
    }

    jclass environment_class = env->FindClass("android/os/Environment");
    if (environment_class == nullptr) {
        AndroidClearJniException(env);
        return false;
    }

    jmethodID is_external_storage_manager = env->GetStaticMethodID(
        environment_class, "isExternalStorageManager", "()Z");
    if (is_external_storage_manager == nullptr) {
        AndroidClearJniException(env);
        env->DeleteLocalRef(environment_class);
        return false;
    }

    const jboolean result = env->CallStaticBooleanMethod(
        environment_class, is_external_storage_manager);
    const bool ok = !env->ExceptionCheck() && result == JNI_TRUE;
    AndroidClearJniException(env);
    env->DeleteLocalRef(environment_class);
    return ok;
}

bool AndroidStartSettingsIntent(JNIEnv *env, jobject context, const char *action,
                                bool include_package_uri) {
    if (env == nullptr || context == nullptr || action == nullptr) {
        AK_ANDROID_LOGW("storage permission settings intent skipped: invalid args");
        return false;
    }

    jclass intent_class = env->FindClass("android/content/Intent");
    jclass context_class = env->FindClass("android/content/Context");
    if (intent_class == nullptr || context_class == nullptr) {
        AK_ANDROID_LOGW("storage permission settings intent skipped: missing classes");
        AndroidClearJniException(env);
        AndroidDeleteLocalRef(env, intent_class);
        AndroidDeleteLocalRef(env, context_class);
        return false;
    }

    jstring action_string = env->NewStringUTF(action);
    if (action_string == nullptr) {
        AK_ANDROID_LOGW("storage permission settings intent skipped: action string failed");
        AndroidClearJniException(env);
        env->DeleteLocalRef(intent_class);
        env->DeleteLocalRef(context_class);
        return false;
    }

    jobject uri = nullptr;
    if (include_package_uri) {
        jmethodID get_package_name = env->GetMethodID(
            context_class, "getPackageName", "()Ljava/lang/String;");
        jclass uri_class = env->FindClass("android/net/Uri");
        jmethodID parse_uri = uri_class != nullptr
            ? env->GetStaticMethodID(uri_class, "parse",
                                     "(Ljava/lang/String;)Landroid/net/Uri;")
            : nullptr;
        if (get_package_name != nullptr && uri_class != nullptr &&
            parse_uri != nullptr) {
            auto package_name = static_cast<jstring>(
                env->CallObjectMethod(context, get_package_name));
            if (!env->ExceptionCheck() && package_name != nullptr) {
                const char *package_chars =
                    env->GetStringUTFChars(package_name, nullptr);
                if (package_chars != nullptr) {
                    std::string uri_text = "package:";
                    uri_text += package_chars;
                    env->ReleaseStringUTFChars(package_name, package_chars);
                    jstring uri_string = env->NewStringUTF(uri_text.c_str());
                    if (uri_string != nullptr) {
                        uri = env->CallStaticObjectMethod(uri_class, parse_uri,
                                                         uri_string);
                        env->DeleteLocalRef(uri_string);
                    }
                }
                env->DeleteLocalRef(package_name);
            }
        }
        AndroidClearJniException(env);
        AndroidDeleteLocalRef(env, uri_class);
    }

    jobject intent = nullptr;
    if (include_package_uri && uri != nullptr) {
        jmethodID constructor = env->GetMethodID(
            intent_class, "<init>",
            "(Ljava/lang/String;Landroid/net/Uri;)V");
        if (constructor != nullptr) {
            intent = env->NewObject(intent_class, constructor, action_string,
                                    uri);
        }
    }
    if (intent == nullptr) {
        AndroidClearJniException(env);
        jmethodID constructor = env->GetMethodID(
            intent_class, "<init>", "(Ljava/lang/String;)V");
        if (constructor != nullptr) {
            intent = env->NewObject(intent_class, constructor, action_string);
        }
    }

    if (intent == nullptr || env->ExceptionCheck()) {
        AK_ANDROID_LOGW("storage permission settings intent skipped: intent create failed action=%s", action);
        AndroidClearJniException(env);
        AndroidDeleteLocalRef(env, uri);
        env->DeleteLocalRef(action_string);
        env->DeleteLocalRef(intent_class);
        env->DeleteLocalRef(context_class);
        return false;
    }

    jmethodID add_flags = env->GetMethodID(
        intent_class, "addFlags", "(I)Landroid/content/Intent;");
    if (add_flags != nullptr) {
        jobject flagged = env->CallObjectMethod(
            intent, add_flags, kAndroidFlagActivityNewTask);
        AndroidClearJniException(env);
        AndroidDeleteLocalRef(env, flagged);
    } else {
        AndroidClearJniException(env);
    }

    jmethodID start_activity = env->GetMethodID(
        context_class, "startActivity", "(Landroid/content/Intent;)V");
    if (start_activity == nullptr) {
        AK_ANDROID_LOGW("storage permission settings intent skipped: startActivity not found action=%s", action);
        AndroidClearJniException(env);
        AndroidDeleteLocalRef(env, intent);
        AndroidDeleteLocalRef(env, uri);
        env->DeleteLocalRef(action_string);
        env->DeleteLocalRef(intent_class);
        env->DeleteLocalRef(context_class);
        return false;
    }

    env->CallVoidMethod(context, start_activity, intent);
    const bool ok = !env->ExceptionCheck();
    AK_ANDROID_LOGI("storage permission settings intent action=%s include_package_uri=%d ok=%d",
                    action, include_package_uri ? 1 : 0, ok ? 1 : 0);
    AndroidClearJniException(env);
    AndroidDeleteLocalRef(env, intent);
    AndroidDeleteLocalRef(env, uri);
    env->DeleteLocalRef(action_string);
    env->DeleteLocalRef(intent_class);
    env->DeleteLocalRef(context_class);
    return ok;
}

bool AndroidRequestExternalStoragePermission() {
    const AndroidGodotStoragePermissionState before =
        AndroidGetGodotStoragePermissionState();
    if (before.read || before.write || before.manage) {
        AK_ANDROID_LOGI("storage permission already granted");
        return true;
    }

    // Calling through Godot's OS keeps this path on the Activity that owns
    // the current Godot instance. This is important on Android 6-9, where
    // the extension may be loaded as an ELF dependency and JNI_OnLoad is not
    // guaranteed to have run for engine_api.so yet.
    OS *os = OS::get_singleton();
    if (os != nullptr) {
        const bool dispatched = os->request_permissions();
        AK_ANDROID_LOGI(
            "storage permission request via Godot OS dispatched=%d",
            dispatched ? 1 : 0);
        if (dispatched) {
            const int sdk = AndroidGetSdkInt();
            if (sdk <= 0 || sdk < 30) {
                return true;
            }
        }
    }

    const int sdk = AndroidGetSdkInt();
    AK_ANDROID_LOGI("storage permission request sdk=%d", sdk);
    if (sdk > 0 && sdk < 30) {
        return false;
    }
    if (AndroidHasExternalStoragePermission()) {
        return true;
    }

    if (AndroidRequestExternalStoragePermissionViaGodotJava()) {
        return true;
    }

    JNIEnv *env = krkr_GetJNIEnv();
    if (env == nullptr) {
        AK_ANDROID_LOGW("storage permission request failed: no JNI env");
        return false;
    }

    jobject context = AndroidGetGodotActivityLocal(env);
    AK_ANDROID_LOGI("storage permission request activity_context=%d", context != nullptr ? 1 : 0);
    if (context == nullptr) {
        context = AndroidGetApplicationContextLocal(env);
        AK_ANDROID_LOGI("storage permission request app_context=%d", context != nullptr ? 1 : 0);
    }
    if (context == nullptr) {
        AK_ANDROID_LOGW("storage permission request failed: no context");
        return false;
    }

    bool ok = AndroidStartSettingsIntent(
        env, context,
        "android.settings.MANAGE_APP_ALL_FILES_ACCESS_PERMISSION", true);
    if (!ok) {
        ok = AndroidStartSettingsIntent(
            env, context,
            "android.settings.MANAGE_ALL_FILES_ACCESS_PERMISSION", false);
    }
    AK_ANDROID_LOGI("storage permission request result=%d", ok ? 1 : 0);
    env->DeleteLocalRef(context);
    return ok;
}
#endif

struct GodotGpuPipelineState {
    RID blend_shader;
    RID blend_pipeline;
    RID fill_source_texture;
    RID alpha_blend_a_shader;
    RID alpha_blend_a_pipeline;
    RID alpha_convert_shader;
    RID alpha_convert_pipeline;
    RID blend2_shader;
    RID blend2_pipeline;
    RID blend3_shader;
    RID blend3_pipeline;
    RID copy_triangles_shader;
    RID copy_triangles_pipeline;
    RID draw_triangles_shader;
    RID draw_triangles_pipeline;
    RID draw_masked_triangles_shader;
    RID draw_masked_triangles_pipeline;
    RID live2d_raster_shader;
    RID live2d_raster_normal_pipeline;
    RID live2d_raster_add_pipeline;
    RID live2d_raster_multiply_pipeline;
    RID live2d_raster_mask_pipeline;
    RID live2d_raster_sampler;
    RID live2d_unpremultiply_shader;
    RID live2d_unpremultiply_pipeline;
    RID mosaic_shader;
    RID mosaic_pipeline;
    RID triangle_vertex_buffer;
    uint32_t triangle_vertex_buffer_capacity = 0;
    PackedByteArray triangle_vertex_buffer_data;
};

GodotGpuPipelineState *g_gpu_pipeline_state = nullptr;

struct ArtemisGpuShaderPipeline {
    RID shader;
    RID pipeline;
};

std::unordered_map<std::string, ArtemisGpuShaderPipeline>
    g_artemis_shader_pipeline_cache;

struct GodotGpuUniformSetKey {
    int64_t shader = 0;
    int64_t rid0 = 0;
    int64_t rid1 = 0;
    int64_t rid2 = 0;
    uint8_t count = 0;
    int64_t rid3 = 0;

    bool operator==(const GodotGpuUniformSetKey &other) const {
        return shader == other.shader && rid0 == other.rid0 &&
               rid1 == other.rid1 && rid2 == other.rid2 &&
               count == other.count && rid3 == other.rid3;
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
        combine(key.rid3);
        h ^= std::hash<int>{}(key.count);
        return h;
    }
};

std::unordered_map<GodotGpuUniformSetKey, RID, GodotGpuUniformSetKeyHash>
    g_gpu_uniform_set_cache;
std::unordered_map<int64_t, RID> g_live2d_framebuffer_cache;

Ref<RDTextureFormat> MakeRgbaTextureFormat(uint32_t width, uint32_t height);
bool ExecuteArtemisGpuShader(
    RenderingDevice *rd, const std::shared_ptr<GodotGpuOp> &op);
void FinishGodotGpuOp(const std::shared_ptr<GodotGpuOp> &op, bool result);

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
    // Operate on complete pixels instead of touching the alpha byte through a
    // byte-stride loop.  The frame is RGBA8888 and the host surface is always
    // 32-bit aligned, so this preserves the exact result while allowing the
    // compiler to vectorize the large 2560x1440 pass.
    constexpr uint32_t kOpaqueAlpha = 0xff000000u;
    for (uint32_t y = 0; y < height; ++y) {
        auto *row = reinterpret_cast<uint32_t *>(
            pixels + static_cast<size_t>(y) * stride_bytes);
        for (uint32_t x = 0; x < width; ++x) {
            row[x] |= kOpaqueAlpha;
        }
    }
}

RenderingDevice *MainRenderingDevice() {
    RenderingServer *server = RenderingServer::get_singleton();
    return server != nullptr ? server->get_rendering_device() : nullptr;
}

bool SupportsGodotRenderingDeviceGpu() {
    RenderingServer *server = RenderingServer::get_singleton();
    RenderingDevice *rd = MainRenderingDevice();
    if (server == nullptr || rd == nullptr) return false;

    const std::string method =
        std::string(server->get_current_rendering_method().utf8().get_data());
    const std::string driver =
        std::string(server->get_current_rendering_driver_name().utf8().get_data());
    return method.find("compatibility") == std::string::npos &&
           method.find("gl_compatibility") == std::string::npos &&
           driver.find("opengl") == std::string::npos &&
           driver.find("OpenGL") == std::string::npos;
}

bool DirectPresentGodotNativeFrameEnabled() {
    static const bool enabled = [] {
        const char *value = std::getenv("AETHERKIRI_GODOT_DIRECT_PRESENT");
        return value != nullptr && value[0] != '\0' &&
               std::strcmp(value, "0") != 0;
    }();
    return enabled;
}

void UpdateGpuQueuePeak(size_t value) {
    uint64_t current = g_gpu_queue_peak.load(std::memory_order_relaxed);
    while (value > current &&
           !g_gpu_queue_peak.compare_exchange_weak(
               current, static_cast<uint64_t>(value), std::memory_order_relaxed)) {
    }
}

void CountGpuOpResult(bool result) {
    g_gpu_op_completed.fetch_add(1, std::memory_order_relaxed);
    if (!result) {
        g_gpu_op_failed.fetch_add(1, std::memory_order_relaxed);
    }
}

String GetGodotGpuBridgeDebugInfo() {
    size_t queue_size = 0;
    size_t texture_count = 0;
    uint64_t live_texture_bytes = 0;
    std::unordered_map<uint64_t, size_t> texture_sizes;
    bool scheduled = false;
    bool batch_active = false;
    uint32_t batch_depth = 0;
    {
        std::lock_guard<std::mutex> lock(g_gpu_op_queue_mutex);
        queue_size = g_gpu_op_queue.size();
        scheduled = g_gpu_op_drain_scheduled;
        batch_active = g_gpu_batch_token != 0;
        batch_depth = g_gpu_batch_depth;
    }
    {
        std::lock_guard<std::mutex> lock(g_gpu_textures_mutex);
        texture_count = g_gpu_textures.size();
        for (const auto &entry : g_gpu_textures) {
            const auto width = entry.second.width;
            const auto height = entry.second.height;
            live_texture_bytes += static_cast<uint64_t>(width) * height * 4u;
            ++texture_sizes[(static_cast<uint64_t>(width) << 32u) | height];
        }
    }
    std::vector<std::pair<uint64_t, size_t>> common_texture_sizes(
        texture_sizes.begin(), texture_sizes.end());
    std::sort(common_texture_sizes.begin(), common_texture_sizes.end(),
              [](const auto &a, const auto &b) {
                  return a.second > b.second;
              });

    std::ostringstream out;
    out << " bridge_queue=" << queue_size
        << " bridge_scheduled=" << (scheduled ? 1 : 0)
        << " bridge_peak=" << g_gpu_queue_peak.load(std::memory_order_relaxed)
        << " bridge_ops=" << g_gpu_op_submitted.load(std::memory_order_relaxed)
        << " bridge_done=" << g_gpu_op_completed.load(std::memory_order_relaxed)
        << " bridge_failed=" << g_gpu_op_failed.load(std::memory_order_relaxed)
        << " bridge_copy_failed=" << g_gpu_copy_failed.load(std::memory_order_relaxed)
        << " bridge_tri_failed=" << g_gpu_copy_triangles_failed.load(std::memory_order_relaxed)
        << " bridge_detail_minify_ops="
        << g_gpu_detail_minify_ops.load(std::memory_order_relaxed)
        << " bridge_native_character_aa_ops="
        << g_gpu_native_character_aa_ops.load(std::memory_order_relaxed)
        << " bridge_tri_pipeline_failed=" << g_gpu_triangle_pipeline_failed.load(std::memory_order_relaxed)
        << " bridge_tri_buffer_failed=" << g_gpu_triangle_buffer_failed.load(std::memory_order_relaxed)
        << " bridge_tri_uniform_failed=" << g_gpu_triangle_uniform_failed.load(std::memory_order_relaxed)
        << " bridge_blends=" << g_gpu_blend_op_submitted.load(std::memory_order_relaxed)
        << " bridge_barriers=" << g_gpu_barriers.load(std::memory_order_relaxed)
        << " bridge_alias_sources=" << g_gpu_alias_sources.load(std::memory_order_relaxed)
        << " bridge_alpha_d_clear_versions="
        << g_gpu_alpha_d_clear_versions.load(std::memory_order_relaxed)
        << " bridge_timeouts=" << g_gpu_sync_timeouts.load(std::memory_order_relaxed)
        << " bridge_batch_active=" << (batch_active ? 1 : 0)
        << " bridge_batch_depth=" << batch_depth
        << " bridge_batches=" << g_gpu_batch_started.load(std::memory_order_relaxed)
        << " bridge_batch_ends=" << g_gpu_batch_ended.load(std::memory_order_relaxed)
        << " bridge_batch_rejected=" << g_gpu_batch_rejected.load(std::memory_order_relaxed)
        << " bridge_batch_ops=" << g_gpu_batch_ops.load(std::memory_order_relaxed)
        << " bridge_compute_batches=" << g_gpu_compute_batches.load(std::memory_order_relaxed)
        << " bridge_compute_batch_ops=" << g_gpu_compute_batch_ops.load(std::memory_order_relaxed)
        << " bridge_nonlive_compute_ops="
        << g_gpu_nonlive_compute_ops.load(std::memory_order_relaxed)
        << " bridge_nonlive_compute_barriers="
        << g_gpu_nonlive_compute_barriers.load(std::memory_order_relaxed)
        << " bridge_live2d_raster_enabled="
        << (Live2DHardwareRasterEnabled() ? 1 : 0)
        << " bridge_live2d_raster_batches="
        << g_gpu_live2d_raster_batches.load(std::memory_order_relaxed)
        << " bridge_live2d_raster_ops="
        << g_gpu_live2d_raster_ops.load(std::memory_order_relaxed)
        << " bridge_shadow_barriers_enabled="
        << (GodotGpuBarrierShadowEnabled() ? 1 : 0)
        << " bridge_predicted_compute_barriers="
        << g_gpu_predicted_compute_barriers.load(std::memory_order_relaxed)
        << " bridge_predicted_raw="
        << g_gpu_predicted_raw_hazards.load(std::memory_order_relaxed)
        << " bridge_predicted_waw="
        << g_gpu_predicted_waw_hazards.load(std::memory_order_relaxed)
        << " bridge_predicted_war="
        << g_gpu_predicted_war_hazards.load(std::memory_order_relaxed)
        << " bridge_textures=" << texture_count
        << " bridge_texture_live_mb="
        << (live_texture_bytes / (1024u * 1024u))
        << " bridge_texture_created="
        << g_gpu_textures_created.load(std::memory_order_relaxed)
        << " bridge_texture_released="
        << g_gpu_textures_released.load(std::memory_order_relaxed)
        << " bridge_texture_mb_created="
        << (g_gpu_texture_bytes_created.load(std::memory_order_relaxed) /
            (1024u * 1024u))
        << " bridge_texture_mb_released="
        << (g_gpu_texture_bytes_released.load(std::memory_order_relaxed) /
            (1024u * 1024u))
        << " bridge_texture_sizes=";
    for (size_t i = 0; i < std::min<size_t>(common_texture_sizes.size(), 4u);
         ++i) {
        if (i != 0) out << ',';
         out << static_cast<uint32_t>(common_texture_sizes[i].first >> 32u)
             << 'x'
             << static_cast<uint32_t>(common_texture_sizes[i].first)
             << ':' << common_texture_sizes[i].second;
     }
     return String::utf8(out.str().c_str());
}

void ApplyGodotGpuBarrier(RenderingDevice *rd) {
    if (rd == nullptr) return;
    // Godot 4.6 inserts RenderingDevice barriers automatically. Calling the
    // deprecated global barrier after every small bridge op floods startup
    // with warnings and stalls the title animation path.
    g_gpu_barriers.fetch_add(1, std::memory_order_relaxed);
}

bool IsBatchableBlendOp(const std::shared_ptr<GodotGpuOp> &op) {
    if (op == nullptr) return false;
    if (op->type == GodotGpuOp::Type::Blend) {
        return op->src != op->dst ||
               op->mode == TVP_GODOT_GPU_BLEND_FILL_ARGB ||
               op->mode == TVP_GODOT_GPU_BLEND_REMOVE_CONST_OPACITY ||
               op->mode == TVP_GODOT_GPU_BLEND_FILL_MASK;
    }
    if (op->type == GodotGpuOp::Type::Blend2) {
        return op->src != op->dst && op->src2 != op->dst;
    }
    if(op->type == GodotGpuOp::Type::Blend3) {
        return op->src != op->dst && op->src2 != op->dst &&
            op->src3 != op->dst;
    }
    return false;
}

bool HazardTrackedBlendBarriersEnabled() {
    static const bool enabled = [] {
        const char *value = std::getenv("AETHERKIRI_GODOT_HAZARD_BARRIERS");
        // Keep every queued blend strictly ordered by default.  Tracking only
        // the visible RID rectangles is not sufficient on Metal: Godot may
        // expose different RIDs backed by aliased texture storage, so an
        // apparently independent dispatch can still have a write-after-read
        // dependency.  Missing that dependency progressively over-blends
        // translucent full-screen layers (for example the white title fade).
        // The rectangle-based optimization remains available for profiling,
        // but must be explicitly enabled.
        return value != nullptr && value[0] != '\0' &&
               std::strcmp(value, "0") != 0;
    }();
    return enabled;
}

bool DeferredGodotGpuDrainEnabled() {
    static const bool enabled = [] {
        const char *value = std::getenv("AETHERKIRI_GODOT_DEFER_GPU_DRAIN");
        // Deferring operations created on Godot's render thread lets a clear
        // become visible before the following blends during fast page
        // transitions on Metal.  Execute those operations immediately by
        // default; the old batched behavior remains available for profiling
        // with AETHERKIRI_GODOT_DEFER_GPU_DRAIN=1.
        if (value == nullptr || value[0] == '\0') {
            return TVP_GODOT_DEFER_GPU_DRAIN_DEFAULT;
        }
        return std::strcmp(value, "0") != 0;
    }();
    return enabled;
}

bool ShouldScheduleGodotGpuDrainNow(const std::shared_ptr<GodotGpuOp> &op,
                                    bool /*wait*/) {
    // Wake the render thread on the first queued operation. The scheduled
    // flag below still coalesces producer-thread bursts into one queue drain.
    return op != nullptr;
}

struct GodotGpuPendingWrite {
    RID rid;
    int32_t left = 0;
    int32_t top = 0;
    int32_t right = 0;
    int32_t bottom = 0;
};

GodotGpuPendingWrite PendingWriteForRect(
    const RID &rid, const Vector3 &pos, const Vector3 &size) {
    GodotGpuPendingWrite write;
    write.rid = rid;
    write.left = static_cast<int32_t>(pos.x);
    write.top = static_cast<int32_t>(pos.y);
    write.right = write.left + static_cast<int32_t>(size.x);
    write.bottom = write.top + static_cast<int32_t>(size.y);
    return write;
}

bool PendingWritesOverlap(const GodotGpuPendingWrite &a,
                          const GodotGpuPendingWrite &b) {
    return a.rid == b.rid && a.left < b.right && b.left < a.right &&
           a.top < b.bottom && b.top < a.bottom;
}

// A bridge-owned RD texture is a distinct allocation for the lifetime of the
// queued batch. Imported Apple/Android textures are the exception: their RID
// can be a view over storage owned by another subsystem, so retain the old
// conservative ordering whenever one participates in a batch.
std::unordered_set<int64_t> ExternalGodotGpuRids() {
    std::unordered_set<int64_t> result;
    std::lock_guard<std::mutex> lock(g_gpu_textures_mutex);
    for (const auto &entry : g_gpu_textures) {
        const auto &record = entry.second;
        if (record.apple_pixel_buffer != nullptr ||
            record.apple_vulkan_external_texture != nullptr ||
            record.android_external_texture != nullptr) {
            if (record.rid.is_valid()) result.insert(record.rid.get_id());
        }
    }
    return result;
}

bool NonLiveGpuHazardTrackingSafe(
    const std::vector<std::shared_ptr<GodotGpuOp>> &ops) {
    const char *conservative =
        std::getenv("AETHERKIRI_GODOT_CONSERVATIVE_BARRIERS");
    if (conservative != nullptr && conservative[0] != '\0' &&
        std::strcmp(conservative, "0") != 0) {
        return false;
    }
    const auto external_rids = ExternalGodotGpuRids();
    if (external_rids.empty()) return true;
    for (const auto &op : ops) {
        if (op == nullptr) continue;
        const RID rids[] = {op->src, op->src2, op->src3, op->dst};
        for (const RID &rid : rids) {
            if (rid.is_valid() && external_rids.count(rid.get_id()) != 0)
                return false;
        }
    }
    return true;
}

bool BlendOpNeedsBarrierBeforeDispatch(
    const GodotGpuOp &op, const std::vector<GodotGpuPendingWrite> &writes) {
    if (writes.empty()) return false;
    const GodotGpuPendingWrite dst_rect =
        PendingWriteForRect(op.dst, op.dst_pos, op.size);
    const Vector3 src_extent =
        op.src_size.x > 0.0 && op.src_size.y > 0.0 ? op.src_size : op.size;
    const GodotGpuPendingWrite src_rect =
        PendingWriteForRect(op.src, op.src_pos, src_extent);
    const bool dual_source = op.type == GodotGpuOp::Type::Blend2 ||
        op.type == GodotGpuOp::Type::Blend3;
    const bool triple_source = op.type == GodotGpuOp::Type::Blend3;
    const GodotGpuPendingWrite src2_rect =
        dual_source ? PendingWriteForRect(op.src2, op.src2_pos, op.size)
                    : GodotGpuPendingWrite{};
    const GodotGpuPendingWrite src3_rect =
        triple_source ? PendingWriteForRect(op.src3, op.src3_pos, op.size)
                      : GodotGpuPendingWrite{};
    for (const auto &write : writes) {
        if (PendingWritesOverlap(write, dst_rect) ||
            PendingWritesOverlap(write, src_rect) ||
            (dual_source && PendingWritesOverlap(write, src2_rect)) ||
            (triple_source && PendingWritesOverlap(write, src3_rect))) {
            return true;
        }
    }
    return false;
}

PackedByteArray PackGpuPushConstants(const GodotGpuOp &op) {
    PackedByteArray data;
    data.resize(48);
    uint8_t *bytes = data.ptrw();
    if (bytes == nullptr) return data;
    const bool dual_source = op.type == GodotGpuOp::Type::Blend2 ||
        op.type == GodotGpuOp::Type::Blend3;
    const bool triple_source = op.type == GodotGpuOp::Type::Blend3;
    const bool triangles = op.type == GodotGpuOp::Type::CopyTriangles ||
                           op.type == GodotGpuOp::Type::DrawTriangles ||
                           op.type == GodotGpuOp::Type::DrawMaskedTriangles;
    const bool mosaic = op.type == GodotGpuOp::Type::Mosaic;
    const bool dimensioned = triangles || mosaic;
    const bool scaled_blend = op.type == GodotGpuOp::Type::Blend &&
        op.mode != TVP_GODOT_GPU_BLEND_FILL_ARGB &&
        op.src_size.x > 0.0 && op.src_size.y > 0.0 &&
        (op.src_size.x != op.size.x || op.src_size.y != op.size.y);
    int32_t values[12] = {
        static_cast<int32_t>(op.dst_pos.x),
        static_cast<int32_t>(op.dst_pos.y),
        static_cast<int32_t>(op.src_pos.x),
        static_cast<int32_t>(op.src_pos.y),
        static_cast<int32_t>(op.size.x),
        static_cast<int32_t>(op.size.y),
        static_cast<int32_t>(triple_source
                                 ? (op.mode | ((op.color & 0xffffu) << 16))
                                 : op.mode),
        static_cast<int32_t>(std::clamp(op.opacity, 0, 255)),
        (dimensioned || scaled_blend) ? static_cast<int32_t>(op.src_size.x) :
        dual_source ? static_cast<int32_t>(op.src2_pos.x)
                    : static_cast<int32_t>(op.color & 0xffu),
        (dimensioned || scaled_blend) ? static_cast<int32_t>(op.src_size.y) :
        dual_source ? static_cast<int32_t>(op.src2_pos.y)
                    : static_cast<int32_t>((op.color >> 8) & 0xffu),
        triple_source ? static_cast<int32_t>(op.src3_pos.x) :
        scaled_blend ? 1 :
        triangles ? static_cast<int32_t>(
                        op.color |
                        (op.preserve_minified_detail
                             ? kGodotGpuPreserveMinifiedDetail
                             : 0u)) :
        mosaic ? 0 :
        dual_source ? 0 : static_cast<int32_t>((op.color >> 16) & 0xffu),
        triple_source ? static_cast<int32_t>(op.src3_pos.y) :
        (dual_source || triangles || mosaic) ? 0
                                             : static_cast<int32_t>((op.color >> 24) & 0xffu),
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

vec4 sample_src_premul(vec2 src_coord, ivec2 src_min, ivec2 src_max) {
    ivec2 p0 = clamp(ivec2(floor(src_coord)), src_min, src_max);
    ivec2 p1 = clamp(p0 + ivec2(1), src_min, src_max);
    vec2 f = clamp(fract(src_coord), vec2(0.0), vec2(1.0));
    vec4 c00 = imageLoad(src_img, p0);
    vec4 c10 = imageLoad(src_img, ivec2(p1.x, p0.y));
    vec4 c01 = imageLoad(src_img, ivec2(p0.x, p1.y));
    vec4 c11 = imageLoad(src_img, p1);
    c00.rgb *= c00.a;
    c10.rgb *= c10.a;
    c01.rgb *= c01.a;
    c11.rgb *= c11.a;
    return mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);
}

vec4 load_src(ivec2 local) {
    if (pc.color0.z != 1) {
        return imageLoad(src_img, pc.rect0.zw + local);
    }
    ivec2 src_extent = max(pc.color0.xy, ivec2(1));
    vec2 source_step = vec2(src_extent) /
        vec2(max(pc.rect1.xy, ivec2(1)));
    vec2 src_coord = vec2(pc.rect0.zw) +
        (vec2(local) + vec2(0.5)) * source_step - vec2(0.5);
    ivec2 src_min = pc.rect0.zw;
    ivec2 src_max = src_min + src_extent - ivec2(1);
    vec4 premul;
    if (max(source_step.x, source_step.y) <= 1.0001) {
        premul = sample_src_premul(src_coord, src_min, src_max);
    } else {
        vec2 dx = vec2(source_step.x > 1.0001
                           ? source_step.x * 0.25
                           : 0.0,
                       0.0);
        vec2 dy = vec2(0.0,
                       source_step.y > 1.0001
                           ? source_step.y * 0.25
                           : 0.0);
        premul =
            (sample_src_premul(src_coord - dx - dy, src_min, src_max) +
             sample_src_premul(src_coord + dx - dy, src_min, src_max) +
             sample_src_premul(src_coord - dx + dy, src_min, src_max) +
             sample_src_premul(src_coord + dx + dy, src_min, src_max)) * 0.25;
    }
    premul.rgb = premul.a > 0.00001 ? premul.rgb / premul.a : vec3(0.0);
    return clamp(premul, vec4(0.0), vec4(1.0));
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

uint ps_screen_blend(uint d, uint s, uint opa) {
    uint src_alpha = (s >> 24) & 0xffu;
    uint a = opa == 255u ? src_alpha : ((src_alpha * opa) >> 8);
    uint dr = d & 0xffu;
    uint dg = (d >> 8) & 0xffu;
    uint db = (d >> 16) & 0xffu;
    uint sr = s & 0xffu;
    uint sg = (s >> 8) & 0xffu;
    uint sb = (s >> 16) & 0xffu;
    uint r = min(dr + (((sr - ((sr * dr) >> 8)) * a) >> 8), 255u);
    uint g = min(dg + (((sg - ((sg * dg) >> 8)) * a) >> 8), 255u);
    uint b = min(db + (((sb - ((sb * db) >> 8)) * a) >> 8), 255u);
    return (d & 0xff000000u) | r | (g << 8) | (b << 16);
}

uint ps_mul_blend(uint d, uint s, uint opa) {
    uint a = (s >> 24) & 0xffu;
    if (opa != 255u) {
        a = (a * opa) >> 8;
    }
    int dr = int(d & 0xffu);
    int dg = int((d >> 8) & 0xffu);
    int db = int((d >> 16) & 0xffu);
    int mr = (dr * int(s & 0xffu)) >> 8;
    int mg = (dg * int((s >> 8) & 0xffu)) >> 8;
    int mb = (db * int((s >> 16) & 0xffu)) >> 8;
    uint r = uint(clamp(dr + (((mr - dr) * int(a)) >> 8), 0, 255));
    uint g = uint(clamp(dg + (((mg - dg) * int(a)) >> 8), 0, 255));
    uint b = uint(clamp(db + (((mb - db) * int(a)) >> 8), 0, 255));
    return (d & 0xff000000u) | r | (g << 8) | (b << 16);
}

uint ps_add_blend(uint d, uint s, uint opa) {
    uint a = (s >> 24) & 0xffu;
    if (opa != 255u) {
        a = (a * opa) >> 8;
    }
    int dr = int(d & 0xffu);
    int dg = int((d >> 8) & 0xffu);
    int db = int((d >> 16) & 0xffu);
    int br = min(dr + int(s & 0xffu), 255);
    int bg = min(dg + int((s >> 8) & 0xffu), 255);
    int bb = min(db + int((s >> 16) & 0xffu), 255);
    uint r = uint(clamp(dr + (((br - dr) * int(a)) >> 8), 0, 255));
    uint g = uint(clamp(dg + (((bg - dg) * int(a)) >> 8), 0, 255));
    uint b = uint(clamp(db + (((bb - db) * int(a)) >> 8), 0, 255));
    return (d & 0xff000000u) | r | (g << 8) | (b << 16);
}

uint ps_sub_blend(uint d, uint s, uint opa) {
    uint a = (s >> 24) & 0xffu;
    if (opa != 255u) {
        a = (a * opa) >> 8;
    }
    int dr = int(d & 0xffu);
    int dg = int((d >> 8) & 0xffu);
    int db = int((d >> 16) & 0xffu);
    int br = max(dr + int(s & 0xffu) - 255, 0);
    int bg = max(dg + int((s >> 8) & 0xffu) - 255, 0);
    int bb = max(db + int((s >> 16) & 0xffu) - 255, 0);
    uint r = uint(clamp(dr + (((br - dr) * int(a)) >> 8), 0, 255));
    uint g = uint(clamp(dg + (((bg - dg) * int(a)) >> 8), 0, 255));
    uint b = uint(clamp(db + (((bb - db) * int(a)) >> 8), 0, 255));
    return (d & 0xff000000u) | r | (g << 8) | (b << 16);
}

uint soft_light_channel(uint source, uint destination) {
    float s = float(source) / 255.0;
    float d = float(destination) / 255.0;
    float exponent = source >= 128u
        ? 128.0 / max(float(source), 1.0)
        : (1.0 - s) / 0.5;
    // TVP's table stores an unsigned char, so conversion intentionally
    // truncates instead of rounding.
    return uint(clamp(pow(max(d, 0.0), exponent) * 255.0, 0.0, 255.0));
}

uint ps_soft_light_blend(uint d, uint s, uint opa) {
    uint a = (s >> 24) & 0xffu;
    if (opa != 255u) {
        a = (a * opa) >> 8;
    }
    int dr = int(d & 0xffu);
    int dg = int((d >> 8) & 0xffu);
    int db = int((d >> 16) & 0xffu);
    int sr = int(soft_light_channel(s & 0xffu, d & 0xffu));
    int sg = int(soft_light_channel((s >> 8) & 0xffu, (d >> 8) & 0xffu));
    int sb = int(soft_light_channel((s >> 16) & 0xffu, (d >> 16) & 0xffu));
    uint r = uint(clamp(dr + (((sr - dr) * int(a)) >> 8), 0, 255));
    uint g = uint(clamp(dg + (((sg - dg) * int(a)) >> 8), 0, 255));
    uint b = uint(clamp(db + (((sb - db) * int(a)) >> 8), 0, 255));
    return (d & 0xff000000u) | r | (g << 8) | (b << 16);
}

uint additive_alpha_blend_hda(uint d, uint s, uint opa) {
    if (opa != 255u) {
        uint sr = ((s & 0xffu) * opa) >> 8;
        uint sg = (((s >> 8) & 0xffu) * opa) >> 8;
        uint sb = (((s >> 16) & 0xffu) * opa) >> 8;
        uint sa = (((s >> 24) & 0xffu) * opa) >> 8;
        s = sr | (sg << 8) | (sb << 16) | (sa << 24);
    }
    uint inverse_alpha = ((~s) >> 24) & 0xffu;
    uint r = min((((d & 0xffu) * inverse_alpha) >> 8) + (s & 0xffu), 255u);
    uint g = min(((((d >> 8) & 0xffu) * inverse_alpha) >> 8) +
                 ((s >> 8) & 0xffu), 255u);
    uint b = min(((((d >> 16) & 0xffu) * inverse_alpha) >> 8) +
                 ((s >> 16) & 0xffu), 255u);
    return (d & 0xff000000u) | r | (g << 8) | (b << 16);
}

uint additive_alpha_blend_a(uint d, uint s, uint opa) {
    if (opa != 255u) {
        uint sr = ((s & 0xffu) * opa) >> 8;
        uint sg = (((s >> 8) & 0xffu) * opa) >> 8;
        uint sb = (((s >> 16) & 0xffu) * opa) >> 8;
        uint sa = (((s >> 24) & 0xffu) * opa) >> 8;
        s = sr | (sg << 8) | (sb << 16) | (sa << 24);
    }
    uint da = (d >> 24) & 0xffu;
    uint sa = (s >> 24) & 0xffu;
    uint out_alpha = da + sa - ((da * sa) >> 8);
    out_alpha -= out_alpha >> 8;
    return (additive_alpha_blend_hda(d, s, 255u) & 0x00ffffffu) |
           (out_alpha << 24);
}

uint apply_color_map_a(uint d, uint mask, uint opa, uvec3 color) {
    uint source_alpha = opa == 255u ? mask : ((mask * opa) >> 8);
    source_alpha -= source_alpha >> 8;
    uint inverse_alpha = source_alpha ^ 0xffu;
    uint dest_alpha = (d >> 24) & 0xffu;
    uint out_alpha = dest_alpha + source_alpha -
                     ((dest_alpha * source_alpha) >> 8);
    out_alpha -= out_alpha >> 8;
    uint r = min((((d & 0xffu) * inverse_alpha) >> 8) +
                 ((source_alpha * color.r) >> 8), 255u);
    uint g = min(((((d >> 8) & 0xffu) * inverse_alpha) >> 8) +
                 ((source_alpha * color.g) >> 8), 255u);
    uint b = min(((((d >> 16) & 0xffu) * inverse_alpha) >> 8) +
                 ((source_alpha * color.b) >> 8), 255u);
    return (out_alpha << 24) | r | (g << 8) | (b << 16);
}

uint remove_const_opacity(uint d, uint strength) {
    uint inv_strength = 255u - clamp(strength, 0u, 255u);
    uint a = (((d >> 24) & 0xffu) * inv_strength) >> 8;
    return (d & 0x00ffffffu) | (a << 24);
}

uint alpha_to_additive_alpha(uint c) {
    uint alpha = (c >> 24) & 0xffu;
    uint r = ((c & 0xffu) * alpha) >> 8;
    uint g = (((c >> 8) & 0xffu) * alpha) >> 8;
    uint b = (((c >> 16) & 0xffu) * alpha) >> 8;
    return (c & 0xff000000u) | r | (g << 8) | (b << 16);
}

uint additive_alpha_to_alpha(uint c) {
    uint alpha = (c >> 24) & 0xffu;
    if (alpha == 0u) return c & 0xff000000u;
    uint r = min((c & 0xffu) * 255u / alpha, 255u);
    uint g = min(((c >> 8) & 0xffu) * 255u / alpha, 255u);
    uint b = min(((c >> 16) & 0xffu) * 255u / alpha, 255u);
    return (c & 0xff000000u) | r | (g << 8) | (b << 16);
}

int reflect101_index(int value, int extent) {
    if (extent <= 1) return 0;
    int period = (extent - 1) * 2;
    int wrapped = value % period;
    if (wrapped < 0) wrapped += period;
    return wrapped < extent ? wrapped : period - wrapped;
}

void main() {
    ivec2 local = ivec2(gl_GlobalInvocationID.xy);
    if (local.x >= pc.rect1.x || local.y >= pc.rect1.y) {
        return;
    }

    ivec2 dst_pos = pc.rect0.xy + local;
    uint opa = uint(clamp(pc.rect1.w, 0, 255));
    uint out_color = 0u;

    if (pc.rect1.z == 26) {
        int radius_x = clamp(pc.rect1.w, 0, 64);
        int radius_y = clamp(pc.color0.x, 0, 64);
        ivec2 extent = max(pc.rect1.xy, ivec2(1));
        vec4 sum = vec4(0.0);
        int samples = 0;
        for (int oy = -radius_y; oy <= radius_y; ++oy) {
            int sy = reflect101_index(local.y + oy, extent.y);
            for (int ox = -radius_x; ox <= radius_x; ++ox) {
                int sx = reflect101_index(local.x + ox, extent.x);
                sum += imageLoad(src_img, pc.rect0.zw + ivec2(sx, sy));
                ++samples;
            }
        }
        out_color = pack_u8(vec4_to_u8(sum / float(max(samples, 1))));
    } else if (pc.rect1.z == 5) {
        out_color = (uint(pc.color0.x) & 0xffu) |
                    ((uint(pc.color0.y) & 0xffu) << 8) |
                    ((uint(pc.color0.z) & 0xffu) << 16) |
                    ((uint(pc.color0.w) & 0xffu) << 24);
    } else {
        uint d = pack_u8(vec4_to_u8(imageLoad(dst_img, dst_pos)));
        out_color = d;
        if (pc.rect1.z == 18) {
        out_color = (d & 0x00ffffffu) | (opa << 24);
        } else {
        uint s = pack_u8(vec4_to_u8(load_src(local)));
        if (pc.rect1.z == 20) {
        out_color = s;
        } else if (pc.rect1.z == 27) {
        out_color = (s & 0x00ffffffu) | 0xff000000u;
        } else if (pc.rect1.z == 19) {
        uint da = (d >> 24) & 0xffu;
        uint sa = (s >> 24) & 0xffu;
        int flags = pc.color0.x;
        bool threshold_mode = pc.color0.y != 0;
        uint out_alpha = da;
        if (flags == 1) {
            out_alpha = threshold_mode
                ? (sa < opa ? 0u : da)
                : ((da * sa) / 255u);
        } else if (flags == 2) {
            out_alpha = threshold_mode
                ? (sa >= opa ? 0u : da)
                : (((255u - sa) * da) / 255u);
        } else if (flags == 5 || flags == 6) {
            out_alpha = threshold_mode
                ? (sa >= opa ? 255u : da)
                : (sa + ((255u - sa) * da) / 255u);
        }
        out_color = (d & 0x00ffffffu) |
                    (min(out_alpha, 255u) << 24);
        } else if (pc.rect1.z == 1) {
        out_color = alpha_blend_hda_o(d, s, opa);
        } else if (pc.rect1.z == 2) {
        out_color = alpha_blend_d(d, s, opa);
        } else if (pc.rect1.z == 3) {
        out_color = (d & 0xff000000u) + (s & 0x00ffffffu);
        } else if (pc.rect1.z == 10) {
        out_color = const_alpha_blend_d(d, s, opa);
        } else if (pc.rect1.z == 11) {
        out_color = ps_screen_blend(d, s, opa);
        } else if (pc.rect1.z == 15) {
        out_color = ps_mul_blend(d, s, opa);
        } else if (pc.rect1.z == 16) {
        out_color = ps_add_blend(d, s, opa);
        } else if (pc.rect1.z == 17) {
        out_color = ps_sub_blend(d, s, opa);
        } else if (pc.rect1.z == 28) {
        out_color = ps_soft_light_blend(d, s, opa);
        } else if (pc.rect1.z == 23) {
        out_color = apply_color_map_a(
            d, s & 0xffu, opa,
            uvec3(uint(pc.color0.x), uint(pc.color0.y), uint(pc.color0.z)));
        } else if (pc.rect1.z == 24) {
        out_color = additive_alpha_blend_hda(d, s, opa);
        } else if (pc.rect1.z == 25) {
        out_color = additive_alpha_blend_a(d, s, opa);
        } else if (pc.rect1.z == 8) {
        out_color = remove_const_opacity(d, opa);
        } else if (pc.rect1.z == 29) {
        out_color = alpha_to_additive_alpha(s);
        } else if (pc.rect1.z == 30) {
        out_color = additive_alpha_to_alpha(s);
        }
        }
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

bool EnsureAlphaConvertPipeline(RenderingDevice *rd) {
    if (rd == nullptr) return false;
    if (g_gpu_pipeline_state == nullptr) {
        g_gpu_pipeline_state = new GodotGpuPipelineState();
    }
    if (g_gpu_pipeline_state->alpha_convert_pipeline.is_valid()) return true;

    Ref<RDShaderSource> source;
    source.instantiate();
    source->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);
    source->set_stage_source(
        RenderingDevice::SHADER_STAGE_COMPUTE,
        R"GLSL(#version 450
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(rgba8, set = 0, binding = 0) uniform image2D target_img;
layout(push_constant, std430) uniform Params {
    ivec4 rect0;
    ivec4 rect1;
    ivec4 color0;
} pc;

uvec4 vec4_to_u8(vec4 value) {
    return uvec4(round(clamp(value, vec4(0.0), vec4(1.0)) * 255.0));
}

vec4 unpack_u8(uint c) {
    return vec4(float(c & 0xffu),
                float((c >> 8) & 0xffu),
                float((c >> 16) & 0xffu),
                float((c >> 24) & 0xffu)) / 255.0;
}

uint pack_u8(uvec4 c) {
    return (c.r & 0xffu) |
           ((c.g & 0xffu) << 8) |
           ((c.b & 0xffu) << 16) |
           ((c.a & 0xffu) << 24);
}

uint alpha_to_additive_alpha(uint c) {
    uint alpha = (c >> 24) & 0xffu;
    uint r = ((c & 0xffu) * alpha) >> 8;
    uint g = (((c >> 8) & 0xffu) * alpha) >> 8;
    uint b = (((c >> 16) & 0xffu) * alpha) >> 8;
    return (c & 0xff000000u) | r | (g << 8) | (b << 16);
}

uint additive_alpha_to_alpha(uint c) {
    uint alpha = (c >> 24) & 0xffu;
    if (alpha == 0u) return c & 0xff000000u;
    uint r = min((c & 0xffu) * 255u / alpha, 255u);
    uint g = min(((c >> 8) & 0xffu) * 255u / alpha, 255u);
    uint b = min(((c >> 16) & 0xffu) * 255u / alpha, 255u);
    return (c & 0xff000000u) | r | (g << 8) | (b << 16);
}

void main() {
    ivec2 local = ivec2(gl_GlobalInvocationID.xy);
    if (local.x >= pc.rect1.x || local.y >= pc.rect1.y) return;
    ivec2 pos = pc.rect0.xy + local;
    uint color = pack_u8(vec4_to_u8(imageLoad(target_img, pos)));
    color = pc.rect1.z == 29
        ? alpha_to_additive_alpha(color)
        : additive_alpha_to_alpha(color);
    imageStore(target_img, pos, unpack_u8(color));
}
)GLSL");

    Ref<RDShaderSPIRV> spirv = rd->shader_compile_spirv_from_source(source);
    if (spirv.is_null()) return false;
    const String compile_error =
        spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE);
    if (!compile_error.is_empty()) {
        UtilityFunctions::printerr("Godot GPU alpha conversion shader compile error: ",
                                   compile_error);
        return false;
    }
    g_gpu_pipeline_state->alpha_convert_shader =
        rd->shader_create_from_spirv(spirv, "AetherKiriAlphaConvert");
    if (!g_gpu_pipeline_state->alpha_convert_shader.is_valid()) return false;
    g_gpu_pipeline_state->alpha_convert_pipeline =
        rd->compute_pipeline_create(g_gpu_pipeline_state->alpha_convert_shader);
    return g_gpu_pipeline_state->alpha_convert_pipeline.is_valid();
}

RID GetCachedAlphaConvertUniformSet(RenderingDevice *rd,
                                    const RID &target) {
    if (rd == nullptr || g_gpu_pipeline_state == nullptr) return RID();
    const GodotGpuUniformSetKey key{
        g_gpu_pipeline_state->alpha_convert_shader.get_id(), target.get_id(),
        0, 0, 1};
    const auto found = g_gpu_uniform_set_cache.find(key);
    if (found != g_gpu_uniform_set_cache.end() &&
        found->second.is_valid()) {
        return found->second;
    }
    Ref<RDUniform> image;
    image.instantiate();
    image->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
    image->set_binding(0);
    image->add_id(target);
    TypedArray<RDUniform> uniforms;
    uniforms.push_back(image);
    RID uniform_set = rd->uniform_set_create(
        uniforms, g_gpu_pipeline_state->alpha_convert_shader, 0);
    if (uniform_set.is_valid()) {
        g_gpu_uniform_set_cache[key] = uniform_set;
    }
    return uniform_set;
}

bool ExecuteGodotGpuAlphaConvert(RenderingDevice *rd,
                                 const std::shared_ptr<GodotGpuOp> &op) {
    if (rd == nullptr || op == nullptr || op->dst != op->src ||
        !EnsureAlphaConvertPipeline(rd)) {
        return false;
    }
    const RID uniform_set = GetCachedAlphaConvertUniformSet(rd, op->dst);
    if (!uniform_set.is_valid()) return false;
    const PackedByteArray push_constants = PackGpuPushConstants(*op);
    const int64_t compute_list = rd->compute_list_begin();
    rd->compute_list_bind_compute_pipeline(
        compute_list, g_gpu_pipeline_state->alpha_convert_pipeline);
    rd->compute_list_bind_uniform_set(compute_list, uniform_set, 0);
    rd->compute_list_set_push_constant(compute_list, push_constants, 48);
    rd->compute_list_dispatch(
        compute_list, static_cast<uint32_t>((op->size.x + 7) / 8),
        static_cast<uint32_t>((op->size.y + 7) / 8), 1);
    rd->compute_list_add_barrier(compute_list);
    rd->compute_list_end();
    ApplyGodotGpuBarrier(rd);
    return true;
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

vec4 sample_src_premul(vec2 src_coord, ivec2 src_min, ivec2 src_max) {
    ivec2 p0 = clamp(ivec2(floor(src_coord)), src_min, src_max);
    ivec2 p1 = clamp(p0 + ivec2(1), src_min, src_max);
    vec2 f = clamp(fract(src_coord), vec2(0.0), vec2(1.0));
    vec4 c00 = imageLoad(src_img, p0);
    vec4 c10 = imageLoad(src_img, ivec2(p1.x, p0.y));
    vec4 c01 = imageLoad(src_img, ivec2(p0.x, p1.y));
    vec4 c11 = imageLoad(src_img, p1);
    c00.rgb *= c00.a;
    c10.rgb *= c10.a;
    c01.rgb *= c01.a;
    c11.rgb *= c11.a;
    return mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);
}

vec4 load_src(ivec2 local) {
    if (pc.color0.z != 1) {
        return imageLoad(src_img, pc.rect0.zw + local);
    }
    ivec2 src_extent = max(pc.color0.xy, ivec2(1));
    vec2 source_step = vec2(src_extent) /
        vec2(max(pc.rect1.xy, ivec2(1)));
    vec2 src_coord = vec2(pc.rect0.zw) +
        (vec2(local) + vec2(0.5)) * source_step - vec2(0.5);
    ivec2 src_min = pc.rect0.zw;
    ivec2 src_max = src_min + src_extent - ivec2(1);
    vec4 premul;
    if (max(source_step.x, source_step.y) <= 1.0001) {
        premul = sample_src_premul(src_coord, src_min, src_max);
    } else {
        vec2 dx = vec2(source_step.x > 1.0001
                           ? source_step.x * 0.25
                           : 0.0,
                       0.0);
        vec2 dy = vec2(0.0,
                       source_step.y > 1.0001
                           ? source_step.y * 0.25
                           : 0.0);
        premul =
            (sample_src_premul(src_coord - dx - dy, src_min, src_max) +
             sample_src_premul(src_coord + dx - dy, src_min, src_max) +
             sample_src_premul(src_coord - dx + dy, src_min, src_max) +
             sample_src_premul(src_coord + dx + dy, src_min, src_max)) * 0.25;
    }
    premul.rgb = premul.a > 0.00001 ? premul.rgb / premul.a : vec3(0.0);
    return clamp(premul, vec4(0.0), vec4(1.0));
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
    uint s = pack_u8(vec4_to_u8(load_src(local)));
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
    } else if (pc.rect1.z == 21 || pc.rect1.z == 22) {
        uint src_alpha = (s1 >> 24) & 0xffu;
        uint mask_alpha = (s2 >> 24) & 0xffu;
        uint masked_alpha = pc.rect1.z == 21
            ? (src_alpha * mask_alpha) / 255u
            : (mask_alpha < 64u ? 0u : src_alpha);
        uint masked_src = (s1 & 0x00ffffffu) | (masked_alpha << 24);
        uint d = pack_u8(vec4_to_u8(imageLoad(dst_img, dst_pos)));
        out_color = alpha_blend_d(d, masked_src, opa);
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

bool EnsureBlend3Pipeline(RenderingDevice *rd) {
    if(rd == nullptr) return false;
    if(g_gpu_pipeline_state == nullptr) {
        g_gpu_pipeline_state = new GodotGpuPipelineState();
    }
    if(g_gpu_pipeline_state->blend3_pipeline.is_valid()) return true;

    Ref<RDShaderSource> source;
    source.instantiate();
    source->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);
    source->set_stage_source(
        RenderingDevice::SHADER_STAGE_COMPUTE,
        R"GLSL(#version 450
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(rgba8, set = 0, binding = 0) uniform readonly image2D src1_img;
layout(rgba8, set = 0, binding = 1) uniform readonly image2D src2_img;
layout(rgba8, set = 0, binding = 2) uniform readonly image2D rule_img;
layout(rgba8, set = 0, binding = 3) uniform image2D dst_img;
layout(push_constant, std430) uniform Params {
    ivec4 rect0;
    ivec4 rect1;
    ivec4 source_rects;
} pc;

vec4 blend_straight_alpha(vec4 s1, vec4 s2, float source_opacity) {
    // Match RenderManager_ogl's UnivTransBlend_d exactly.  This is not the
    // usual straight-alpha interpolation: the result is subsequently drawn
    // as an alpha layer, so the source contribution compensates for that
    // second composition step.
    float target_opacity = 1.0 - source_opacity;
    float numerator = s2.a * source_opacity;
    float denominator = numerator * (1.0 - s1.a * target_opacity) +
                        s1.a * target_opacity + 0.0001;
    float source_weight = numerator / denominator;
    return vec4(mix(s1.rgb, s2.rgb, clamp(source_weight, 0.0, 1.0)),
                mix(s2.a, s1.a, target_opacity));
}

void main() {
    ivec2 local = ivec2(gl_GlobalInvocationID.xy);
    if(local.x >= pc.rect1.x || local.y >= pc.rect1.y) return;

    ivec2 dst_pos = pc.rect0.xy + local;
    ivec2 src1_pos = pc.rect0.zw + local;
    ivec2 src2_pos = pc.source_rects.xy + local;
    ivec2 rule_pos = pc.source_rects.zw + local;
    vec4 s1 = imageLoad(src1_img, src1_pos);
    vec4 s2 = imageLoad(src2_img, src2_pos);
    int rule_value = int(round(clamp(imageLoad(rule_img, rule_pos).r,
                                     0.0, 1.0) * 255.0));
    int packed_mode = pc.rect1.z;
    int mode = packed_mode & 0xffff;
    int vague = max((packed_mode >> 16) & 0xffff, 1);
    int phase = pc.rect1.w;

    vec4 result;
    if(rule_value >= phase) {
        result = s1;
    } else if(rule_value < phase - vague) {
        result = s2;
    } else {
        int opacity = clamp(255 - ((rule_value - (phase - vague)) * 255 /
                                   vague), 0, 255);
        float blend_opacity = float(opacity) / 256.0;
        if(mode == 13) {
            result = blend_straight_alpha(s1, s2, blend_opacity);
        } else if(mode == 12) {
            // UnivTransBlend deliberately preserves s1's alpha.  Mixing the
            // alpha here makes a fully composited page translucent while the
            // rule edge crosses it, which appears as a whole-screen dark
            // flash over the previously presented frame.
            result = vec4(mix(s1.rgb, s2.rgb, blend_opacity), s1.a);
        } else {
            result = mix(s1, s2, blend_opacity);
        }
    }
    imageStore(dst_img, dst_pos, result);
}
)GLSL");

    Ref<RDShaderSPIRV> spirv = rd->shader_compile_spirv_from_source(source);
    if(spirv.is_null()) return false;
    const String compile_error =
        spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE);
    if(!compile_error.is_empty()) {
        UtilityFunctions::printerr("Godot GPU blend3 shader compile error: ",
                                   compile_error);
        return false;
    }
    g_gpu_pipeline_state->blend3_shader =
        rd->shader_create_from_spirv(spirv, "AetherKiriBlend3");
    if(!g_gpu_pipeline_state->blend3_shader.is_valid()) return false;
    g_gpu_pipeline_state->blend3_pipeline =
        rd->compute_pipeline_create(g_gpu_pipeline_state->blend3_shader);
    return g_gpu_pipeline_state->blend3_pipeline.is_valid();
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

vec4 load_bilinear_premul(ivec2 limit, vec2 edge_coord) {
    vec2 center_coord = clamp(edge_coord - vec2(0.5), vec2(0.0), vec2(limit));
    ivec2 p0 = ivec2(floor(center_coord));
    ivec2 p1 = clamp(p0 + ivec2(1), ivec2(0), limit);
    vec2 f = clamp(fract(center_coord), vec2(0.0), vec2(1.0));
    vec4 c00 = imageLoad(src_img, p0);
    vec4 c10 = imageLoad(src_img, ivec2(p1.x, p0.y));
    vec4 c01 = imageLoad(src_img, ivec2(p0.x, p1.y));
    vec4 c11 = imageLoad(src_img, p1);
    c00.rgb *= c00.a;
    c10.rgb *= c10.a;
    c01.rgb *= c01.a;
    c11.rgb *= c11.a;
    vec4 premul = mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);
    return clamp(premul, vec4(0.0), vec4(1.0));
}

vec4 straight_from_premul(vec4 premul) {
    if (premul.a > 0.00001) {
        premul.rgb /= premul.a;
    } else {
        premul.rgb = vec3(0.0);
    }
    return clamp(premul, vec4(0.0), vec4(1.0));
}

vec4 load_minified(ivec2 limit, vec2 edge_coord,
                    vec2 source_dx, vec2 source_dy,
                    bool preserve_detail) {
    // A single bilinear lookup aliases alpha edges when an E-mote surface is
    // presented at a fractional scale (0.5 is common in Artemis games).
    // Approximate the source footprint with destination-pixel subsamples. Keep the
    // fast single lookup at 1:1/upscale, and average premultiplied samples so
    // transparent texels cannot introduce dark colour fringes.
    float footprint = max(length(source_dx), length(source_dy));
    if (footprint <= 1.0001) {
        return straight_from_premul(
            load_bilinear_premul(limit, edge_coord));
    }
    if (preserve_detail) {
        // Keep half of the weight at the centre for fine eye/hair lines and
        // use four footprint corners for stable alpha-edge integration. Five
        // bilinear samples retain the accepted detail while avoiding the 36
        // image loads required by the previous 3x3 composite filter.
        vec2 dx = source_dx * 0.5;
        vec2 dy = source_dy * 0.5;
        vec4 premul = load_bilinear_premul(limit, edge_coord) * 0.5;
        premul +=
            (load_bilinear_premul(limit, edge_coord - dx - dy) +
             load_bilinear_premul(limit, edge_coord + dx - dy) +
             load_bilinear_premul(limit, edge_coord - dx + dy) +
             load_bilinear_premul(limit, edge_coord + dx + dy)) * 0.125;
        return straight_from_premul(premul);
    }
    // DXT E-mote atlases contain high-contrast one-pixel line art. At the
    // authored 0.5 presentation scale a bare bilinear lookup leaves that
    // content visibly stair-stepped. Sample four bilinear quadrants spanning
    // the complete reduction footprint. This covers the compressed source
    // blocks without the cost of a generic 3x3 post-process and retains
    // premultiplied alpha semantics at silhouette edges.
    vec2 dx = source_dx * 0.5;
    vec2 dy = source_dy * 0.5;
    vec4 premul =
        load_bilinear_premul(limit, edge_coord - dx - dy) +
        load_bilinear_premul(limit, edge_coord + dx - dy) +
        load_bilinear_premul(limit, edge_coord - dx + dy) +
        load_bilinear_premul(limit, edge_coord + dx + dy);
    return straight_from_premul(premul * 0.25);
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
    bool preserve_detail =
        (uint(pc.color0.z) & 0x40000000u) != 0u;
    vec4 out_color = imageLoad(dst_img, dst_pos);
    bool covered = false;

    for (int tri = 0; tri < tri_count; ++tri) {
        int vertex_base = pc.color0.w + tri * 4;
        vec4 tri_bounds = vertices.vertex[vertex_base + 3];
        if (any(lessThan(p, tri_bounds.xy)) ||
            any(greaterThan(p, tri_bounds.zw))) {
            continue;
        }
        vec4 v0 = vertices.vertex[vertex_base + 0];
        vec4 v1 = vertices.vertex[vertex_base + 1];
        vec4 v2 = vertices.vertex[vertex_base + 2];
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
            vec2 source10 = v1.zw - v0.zw;
            vec2 source20 = v2.zw - v0.zw;
            vec2 source_dx =
                source10 * ((d0.y - d2.y) / area) +
                source20 * ((d1.y - d0.y) / area);
            vec2 source_dy =
                source10 * ((d2.x - d0.x) / area) +
                source20 * ((d0.x - d1.x) / area);
            out_color = load_minified(
                src_limit, src_pos_f, source_dx, source_dy,
                preserve_detail);
            covered = true;
            // A tessellated surface has a single source sample at a pixel.
            // Stop after the first covering triangle instead of scanning the
            // rest of the mesh (and avoid sampling a shared edge twice).
            break;
        }
    }
    if (covered) {
        imageStore(dst_img, dst_pos, out_color);
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

bool EnsureDrawTrianglesPipeline(RenderingDevice *rd) {
    if (rd == nullptr) return false;
    if (g_gpu_pipeline_state == nullptr) {
        g_gpu_pipeline_state = new GodotGpuPipelineState();
    }
    if (g_gpu_pipeline_state->draw_triangles_pipeline.is_valid()) return true;

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

vec4 load_bilinear_premul(ivec2 limit, vec2 edge_coord,
                          bool source_premultiplied) {
    vec2 center_coord = clamp(edge_coord - vec2(0.5), vec2(0.0), vec2(limit));
    ivec2 p0 = ivec2(floor(center_coord));
    ivec2 p1 = clamp(p0 + ivec2(1), ivec2(0), limit);
    vec2 f = clamp(fract(center_coord), vec2(0.0), vec2(1.0));
    vec4 c00 = imageLoad(src_img, p0);
    vec4 c10 = imageLoad(src_img, ivec2(p1.x, p0.y));
    vec4 c01 = imageLoad(src_img, ivec2(p0.x, p1.y));
    vec4 c11 = imageLoad(src_img, p1);
    if (!source_premultiplied) {
        c00.rgb *= c00.a;
        c10.rgb *= c10.a;
        c01.rgb *= c01.a;
        c11.rgb *= c11.a;
    }
    vec4 premul = mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);
    return clamp(premul, vec4(0.0), vec4(1.0));
}

float premul_luma(vec4 color) {
    vec3 straight = color.a > 0.00001
        ? color.rgb / color.a : vec3(0.0);
    // Include alpha in the edge signal so the same filter also stabilizes
    // the outline of the transparent native character surface.
    return dot(straight, vec3(0.299, 0.587, 0.114)) * color.a;
}

vec4 load_native_character_antialiased(
        ivec2 limit, vec2 edge_coord, vec2 source_dx, vec2 source_dy) {
    vec4 center = load_bilinear_premul(limit, edge_coord, true);
    vec4 nw = load_bilinear_premul(
        limit, edge_coord - source_dx - source_dy, true);
    vec4 ne = load_bilinear_premul(
        limit, edge_coord + source_dx - source_dy, true);
    vec4 sw = load_bilinear_premul(
        limit, edge_coord - source_dx + source_dy, true);
    vec4 se = load_bilinear_premul(
        limit, edge_coord + source_dx + source_dy, true);
    float l_center = premul_luma(center);
    float l_nw = premul_luma(nw);
    float l_ne = premul_luma(ne);
    float l_sw = premul_luma(sw);
    float l_se = premul_luma(se);
    float range_min = min(
        l_center, min(min(l_nw, l_ne), min(l_sw, l_se)));
    float range_max = max(
        l_center, max(max(l_nw, l_ne), max(l_sw, l_se)));
    if (range_max - range_min <
            max(1.0 / 24.0, range_max * (1.0 / 8.0))) {
        return center;
    }

    vec2 direction = vec2(
        -((l_nw + l_ne) - (l_sw + l_se)),
         ((l_nw + l_sw) - (l_ne + l_se)));
    float reduce = max(
        (l_nw + l_ne + l_sw + l_se) * (0.25 / 8.0),
        1.0 / 128.0);
    // Two pixels is enough to join the one-pixel E-mote diagonals without
    // softening broad painted regions or the independently composited text.
    direction = clamp(
        direction / (min(abs(direction.x), abs(direction.y)) + reduce),
        vec2(-2.0), vec2(2.0));
    vec2 source_direction =
        source_dx * direction.x + source_dy * direction.y;
    vec4 sample_a = 0.5 * (
        load_bilinear_premul(
            limit, edge_coord + source_direction * (1.0 / 3.0 - 0.5),
            true) +
        load_bilinear_premul(
            limit, edge_coord + source_direction * (2.0 / 3.0 - 0.5),
            true));
    vec4 sample_b = sample_a * 0.5 + 0.25 * (
        load_bilinear_premul(
            limit, edge_coord - source_direction * 0.5, true) +
        load_bilinear_premul(
            limit, edge_coord + source_direction * 0.5, true));
    float l_b = premul_luma(sample_b);
    return (l_b < range_min || l_b > range_max) ? sample_a : sample_b;
}

vec4 load_minified(ivec2 limit, vec2 edge_coord,
                    vec2 source_dx, vec2 source_dy,
                    bool preserve_detail, bool source_premultiplied) {
    float footprint = max(length(source_dx), length(source_dy));
    vec4 premul;
    if (footprint <= 1.0001) {
        premul = source_premultiplied
            ? load_native_character_antialiased(
                  limit, edge_coord, source_dx, source_dy)
            : load_bilinear_premul(
                  limit, edge_coord, source_premultiplied);
    } else if (preserve_detail) {
        vec2 dx = source_dx * 0.5;
        vec2 dy = source_dy * 0.5;
        premul = load_bilinear_premul(
            limit, edge_coord, source_premultiplied) * 0.5;
        premul +=
            (load_bilinear_premul(
                 limit, edge_coord - dx - dy, source_premultiplied) +
             load_bilinear_premul(
                 limit, edge_coord + dx - dy, source_premultiplied) +
             load_bilinear_premul(
                 limit, edge_coord - dx + dy, source_premultiplied) +
             load_bilinear_premul(
                 limit, edge_coord + dx + dy, source_premultiplied)) * 0.125;
    } else {
        vec2 dx = source_dx * 0.5;
        vec2 dy = source_dy * 0.5;
        premul =
            (load_bilinear_premul(
                 limit, edge_coord - dx - dy, source_premultiplied) +
             load_bilinear_premul(
                 limit, edge_coord + dx - dy, source_premultiplied) +
             load_bilinear_premul(
                 limit, edge_coord - dx + dy, source_premultiplied) +
             load_bilinear_premul(
                 limit, edge_coord + dx + dy, source_premultiplied)) * 0.25;
    }
    if (premul.a > 0.00001) {
        premul.rgb /= premul.a;
    } else {
        premul.rgb = vec3(0.0);
    }
    return clamp(premul, vec4(0.0), vec4(1.0));
}

vec4 straight_from_premul(vec3 rgb, float a) {
    return clamp(vec4(a > 0.00001 ? rgb / a : vec3(0.0), a),
                 vec4(0.0), vec4(1.0));
}

vec4 blend_over(vec4 dst, vec4 src) {
    float out_a = src.a + dst.a * (1.0 - src.a);
    vec3 premul = src.rgb * src.a + dst.rgb * dst.a * (1.0 - src.a);
    return straight_from_premul(premul, out_a);
}

vec4 blend_add_compatible(vec4 dst, vec4 src) {
    float out_a = dst.a;
    vec3 premul = dst.rgb * dst.a + src.rgb * src.a;
    return straight_from_premul(premul, out_a);
}

vec4 blend_multiply_compatible(vec4 dst, vec4 src) {
    float out_a = dst.a;
    vec3 dst_premul = dst.rgb * dst.a;
    vec3 src_premul = src.rgb * src.a;
    vec3 premul = src_premul * dst_premul + dst_premul * (1.0 - src.a);
    return straight_from_premul(premul, out_a);
}

float color_burn(float src, float dst) {
    if (abs(dst - 1.0) < 0.000001) {
        return 1.0;
    }
    if (abs(src) < 0.000001) {
        return 0.0;
    }
    return 1.0 - min(1.0, (1.0 - dst) / src);
}

float color_dodge(float src, float dst) {
    if (dst <= 0.0) {
        return 0.0;
    }
    if (abs(src - 1.0) < 0.000001) {
        return 1.0;
    }
    return min(1.0, dst / (1.0 - src));
}

float overlay(float src, float dst) {
    float mul = 2.0 * src * dst;
    float scr = 1.0 - 2.0 * (1.0 - src) * (1.0 - dst);
    return dst < 0.5 ? mul : scr;
}

float soft_light(float src, float dst) {
    float val1 = dst - (1.0 - 2.0 * src) * dst * (1.0 - dst);
    float val2 = dst + (2.0 * src - 1.0) * dst *
                         ((16.0 * dst - 12.0) * dst + 3.0);
    float val3 = dst + (2.0 * src - 1.0) * (sqrt(dst) - dst);
    if (src <= 0.5) {
        return val1;
    }
    if (dst <= 0.25) {
        return val2;
    }
    return val3;
}

float hard_light(float src, float dst) {
    float mul = 2.0 * src * dst;
    float scr = 1.0 - 2.0 * (1.0 - src) * (1.0 - dst);
    return src < 0.5 ? mul : scr;
}

float linear_light(float src, float dst) {
    float burn = max(0.0, 2.0 * src + dst - 1.0);
    float dodge = min(1.0, 2.0 * (src - 0.5) + dst);
    return src < 0.5 ? burn : dodge;
}

vec3 color_blend(int mode, vec3 src, vec3 dst) {
    if (mode == 3) {
        return min(src + dst, vec3(1.0));
    }
    if (mode == 4) {
        return src + dst;
    }
    if (mode == 5) {
        return min(src, dst);
    }
    if (mode == 6) {
        return src * dst;
    }
    if (mode == 7) {
        return vec3(color_burn(src.r, dst.r), color_burn(src.g, dst.g),
                    color_burn(src.b, dst.b));
    }
    if (mode == 8) {
        return max(vec3(0.0), src + dst - vec3(1.0));
    }
    if (mode == 9) {
        return max(src, dst);
    }
    if (mode == 10) {
        return src + dst - src * dst;
    }
    if (mode == 11) {
        return vec3(color_dodge(src.r, dst.r), color_dodge(src.g, dst.g),
                    color_dodge(src.b, dst.b));
    }
    if (mode == 12) {
        return vec3(overlay(src.r, dst.r), overlay(src.g, dst.g),
                    overlay(src.b, dst.b));
    }
    if (mode == 13) {
        return vec3(soft_light(src.r, dst.r), soft_light(src.g, dst.g),
                    soft_light(src.b, dst.b));
    }
    if (mode == 14) {
        return vec3(hard_light(src.r, dst.r), hard_light(src.g, dst.g),
                    hard_light(src.b, dst.b));
    }
    if (mode == 15) {
        return vec3(linear_light(src.r, dst.r), linear_light(src.g, dst.g),
                    linear_light(src.b, dst.b));
    }
    return src;
}

vec4 blend_cubism(vec4 dst, vec4 src, int flags) {
    int color_mode = flags & 255;
    int alpha_mode = (flags >> 8) & 255;
    if (color_mode == 0 && alpha_mode == 0) {
        return blend_over(dst, src);
    }
    if (color_mode == 1 && alpha_mode == 0) {
        return blend_add_compatible(dst, src);
    }
    if (color_mode == 2 && alpha_mode == 0) {
        return blend_multiply_compatible(dst, src);
    }
    vec3 color = color_blend(color_mode, src.rgb, dst.rgb);
    vec3 parameter;
    if (alpha_mode == 1) {
        parameter = vec3(src.a * dst.a, 0.0, dst.a * (1.0 - src.a));
    } else if (alpha_mode == 2) {
        parameter = vec3(0.0, 0.0, dst.a * (1.0 - src.a));
    } else if (alpha_mode == 3) {
        parameter = vec3(min(src.a, dst.a), max(src.a - dst.a, 0.0),
                         max(dst.a - src.a, 0.0));
    } else if (alpha_mode == 4) {
        parameter = vec3(max(src.a + dst.a - 1.0, 0.0),
                         min(src.a, 1.0 - dst.a),
                         min(dst.a, 1.0 - src.a));
    } else {
        parameter = vec3(src.a * dst.a, src.a * (1.0 - dst.a),
                         dst.a * (1.0 - src.a));
    }
    return straight_from_premul(color * parameter.x +
                                src.rgb * parameter.y +
                                dst.rgb * parameter.z,
                                parameter.x + parameter.y + parameter.z);
}

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

uint ps_screen_blend(uint d, uint s, uint opa) {
    uint src_alpha = (s >> 24) & 0xffu;
    uint a = opa == 255u ? src_alpha : ((src_alpha * opa) >> 8);
    uint dr = d & 0xffu;
    uint dg = (d >> 8) & 0xffu;
    uint db = (d >> 16) & 0xffu;
    uint sr = s & 0xffu;
    uint sg = (s >> 8) & 0xffu;
    uint sb = (s >> 16) & 0xffu;
    uint r = min(dr + (((sr - ((sr * dr) >> 8)) * a) >> 8), 255u);
    uint g = min(dg + (((sg - ((sg * dg) >> 8)) * a) >> 8), 255u);
    uint b = min(db + (((sb - ((sb * db) >> 8)) * a) >> 8), 255u);
    return (d & 0xff000000u) | r | (g << 8) | (b << 16);
}

uint ps_mul_blend(uint d, uint s, uint opa) {
    uint a = (s >> 24) & 0xffu;
    if (opa != 255u) {
        a = (a * opa) >> 8;
    }
    int dr = int(d & 0xffu);
    int dg = int((d >> 8) & 0xffu);
    int db = int((d >> 16) & 0xffu);
    int mr = (dr * int(s & 0xffu)) >> 8;
    int mg = (dg * int((s >> 8) & 0xffu)) >> 8;
    int mb = (db * int((s >> 16) & 0xffu)) >> 8;
    uint r = uint(clamp(dr + (((mr - dr) * int(a)) >> 8), 0, 255));
    uint g = uint(clamp(dg + (((mg - dg) * int(a)) >> 8), 0, 255));
    uint b = uint(clamp(db + (((mb - db) * int(a)) >> 8), 0, 255));
    return (d & 0xff000000u) | r | (g << 8) | (b << 16);
}

uint ps_add_blend(uint d, uint s, uint opa) {
    uint a = (s >> 24) & 0xffu;
    if (opa != 255u) {
        a = (a * opa) >> 8;
    }
    int dr = int(d & 0xffu);
    int dg = int((d >> 8) & 0xffu);
    int db = int((d >> 16) & 0xffu);
    int br = min(dr + int(s & 0xffu), 255);
    int bg = min(dg + int((s >> 8) & 0xffu), 255);
    int bb = min(db + int((s >> 16) & 0xffu), 255);
    uint r = uint(clamp(dr + (((br - dr) * int(a)) >> 8), 0, 255));
    uint g = uint(clamp(dg + (((bg - dg) * int(a)) >> 8), 0, 255));
    uint b = uint(clamp(db + (((bb - db) * int(a)) >> 8), 0, 255));
    return (d & 0xff000000u) | r | (g << 8) | (b << 16);
}

uint ps_sub_blend(uint d, uint s, uint opa) {
    uint a = (s >> 24) & 0xffu;
    if (opa != 255u) {
        a = (a * opa) >> 8;
    }
    int dr = int(d & 0xffu);
    int dg = int((d >> 8) & 0xffu);
    int db = int((d >> 16) & 0xffu);
    int br = max(dr + int(s & 0xffu) - 255, 0);
    int bg = max(dg + int((s >> 8) & 0xffu) - 255, 0);
    int bb = max(db + int((s >> 16) & 0xffu) - 255, 0);
    uint r = uint(clamp(dr + (((br - dr) * int(a)) >> 8), 0, 255));
    uint g = uint(clamp(dg + (((bg - dg) * int(a)) >> 8), 0, 255));
    uint b = uint(clamp(db + (((bb - db) * int(a)) >> 8), 0, 255));
    return (d & 0xff000000u) | r | (g << 8) | (b << 16);
}

uint blend_tvp(uint d, uint s, uint opa, int mode) {
    if (mode == 1) {
        return alpha_blend_hda_o(d, s, opa);
    }
    if (mode == 2) {
        return alpha_blend_d(d, s, opa);
    }
    if (mode == 3) {
        return (d & 0xff000000u) | (s & 0x00ffffffu);
    }
    if (mode == 7) {
        return alpha_blend_a_d_o(d, s, opa);
    }
    if (mode == 10) {
        return const_alpha_blend_d(d, s, opa);
    }
    if (mode == 11) {
        return ps_screen_blend(d, s, opa);
    }
    if (mode == 15) {
        return ps_mul_blend(d, s, opa);
    }
    if (mode == 16) {
        return ps_add_blend(d, s, opa);
    }
    if (mode == 17) {
        return ps_sub_blend(d, s, opa);
    }
    return d;
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
    float opacity = clamp(float(pc.rect1.w) / 255.0, 0.0, 1.0);
    bool preserve_detail =
        (uint(pc.color0.z) & 0x40000000u) != 0u;
    bool source_premultiplied =
        (uint(pc.color0.z) & 0x20000000u) != 0u;
    int blend_flags = int(uint(pc.color0.z) & 0x1fffffffu);
    bool mask_write = (blend_flags & 131072) != 0;
    bool tvp_blend = !mask_write && (blend_flags & 65536) != 0;
    int tvp_blend_mode = blend_flags & 65535;
    vec4 dst = imageLoad(dst_img, dst_pos);
    bool covered = false;

    for (int tri = 0; tri < tri_count; ++tri) {
        int vertex_base = pc.color0.w + tri * 4;
        vec4 tri_bounds = vertices.vertex[vertex_base + 3];
        if (any(lessThan(p, tri_bounds.xy)) ||
            any(greaterThan(p, tri_bounds.zw))) {
            continue;
        }
        vec4 v0 = vertices.vertex[vertex_base + 0];
        vec4 v1 = vertices.vertex[vertex_base + 1];
        vec4 v2 = vertices.vertex[vertex_base + 2];
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
            vec2 source10 = v1.zw - v0.zw;
            vec2 source20 = v2.zw - v0.zw;
            vec2 source_dx =
                source10 * ((d0.y - d2.y) / area) +
                source20 * ((d1.y - d0.y) / area);
            vec2 source_dy =
                source10 * ((d2.x - d0.x) / area) +
                source20 * ((d0.x - d1.x) / area);
            vec4 src = load_minified(
                src_limit, src_pos_f, source_dx, source_dy,
                preserve_detail, source_premultiplied);
            if (tvp_blend) {
                uint d = pack_u8(vec4_to_u8(dst));
                uint s = pack_u8(vec4_to_u8(src));
                dst = unpack_u8(blend_tvp(
                    d, s, uint(clamp(pc.rect1.w, 0, 255)), tvp_blend_mode));
                // A single affine surface is tessellated into adjacent
                // triangles. Pixels on their shared edge must be blended only
                // once; the Cubism path intentionally keeps its own mesh
                // accumulation semantics below.
                covered = true;
                break;
            } else {
                // Texture alpha is authoritative.  Green is used by some
                // models as ordinary artwork, not as a runtime chroma key.
                src.a *= opacity;
                if (src.a <= 0.00001) {
                    continue;
                }
                if (mask_write) {
                    // Cubism mask textures start white and accumulate inverse
                    // source alpha.  Keep RGB white because the masked draw
                    // samples only alpha, matching the former CPU rasterizer.
                    dst = vec4(1.0, 1.0, 1.0,
                               dst.a * (1.0 - src.a));
                } else {
                    dst = blend_cubism(dst, src, blend_flags);
                }
            }
            covered = true;
            // Cubism ArtMeshes are tessellations, not independently stacked
            // triangles.  Once this pixel is covered, later triangles in the
            // same mesh cannot contribute another layer.
            break;
        }
    }
    if (covered) {
        imageStore(dst_img, dst_pos, dst);
    }
}
)GLSL");

    Ref<RDShaderSPIRV> spirv = rd->shader_compile_spirv_from_source(source);
    if (spirv.is_null()) return false;
    const String compile_error =
        spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE);
    if (!compile_error.is_empty()) {
        UtilityFunctions::printerr("Godot GPU draw triangles shader compile error: ",
                                   compile_error);
        return false;
    }
    g_gpu_pipeline_state->draw_triangles_shader =
        rd->shader_create_from_spirv(spirv, "AetherKiriDrawTriangles");
    if (!g_gpu_pipeline_state->draw_triangles_shader.is_valid()) return false;
    g_gpu_pipeline_state->draw_triangles_pipeline =
        rd->compute_pipeline_create(g_gpu_pipeline_state->draw_triangles_shader);
    return g_gpu_pipeline_state->draw_triangles_pipeline.is_valid();
}

bool EnsureDrawMaskedTrianglesPipeline(RenderingDevice *rd) {
    if (rd == nullptr) return false;
    if (g_gpu_pipeline_state == nullptr) {
        g_gpu_pipeline_state = new GodotGpuPipelineState();
    }
    if (g_gpu_pipeline_state->draw_masked_triangles_pipeline.is_valid()) {
        return true;
    }

    Ref<RDShaderSource> source;
    source.instantiate();
    source->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);
    source->set_stage_source(
        RenderingDevice::SHADER_STAGE_COMPUTE,
        R"GLSL(#version 450
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(std430, set = 0, binding = 0) readonly buffer Vertices {
    float value[];
} vertices;
layout(rgba8, set = 0, binding = 1) uniform readonly image2D src_img;
layout(rgba8, set = 0, binding = 2) uniform readonly image2D mask_img;
layout(rgba8, set = 0, binding = 3) uniform image2D dst_img;
layout(push_constant, std430) uniform Params {
    ivec4 rect0;
    ivec4 rect1;
    ivec4 color0;
} pc;

float edge(vec2 a, vec2 b, vec2 p) {
    return (p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x);
}

vec4 load_bilinear_premul(ivec2 limit, vec2 edge_coord) {
    vec2 center_coord = clamp(edge_coord - vec2(0.5), vec2(0.0), vec2(limit));
    ivec2 p0 = ivec2(floor(center_coord));
    ivec2 p1 = clamp(p0 + ivec2(1), ivec2(0), limit);
    vec2 f = clamp(fract(center_coord), vec2(0.0), vec2(1.0));
    vec4 c00 = imageLoad(src_img, p0);
    vec4 c10 = imageLoad(src_img, ivec2(p1.x, p0.y));
    vec4 c01 = imageLoad(src_img, ivec2(p0.x, p1.y));
    vec4 c11 = imageLoad(src_img, p1);
    c00.rgb *= c00.a;
    c10.rgb *= c10.a;
    c01.rgb *= c01.a;
    c11.rgb *= c11.a;
    vec4 premul = mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);
    return clamp(premul, vec4(0.0), vec4(1.0));
}

vec4 load_minified(ivec2 limit, vec2 edge_coord,
                    vec2 source_dx, vec2 source_dy,
                    bool preserve_detail) {
    float footprint = max(length(source_dx), length(source_dy));
    vec4 premul;
    if (footprint <= 1.0001) {
        premul = load_bilinear_premul(limit, edge_coord);
    } else if (preserve_detail) {
        vec2 dx = source_dx * 0.5;
        vec2 dy = source_dy * 0.5;
        premul = load_bilinear_premul(limit, edge_coord) * 0.5;
        premul +=
            (load_bilinear_premul(limit, edge_coord - dx - dy) +
             load_bilinear_premul(limit, edge_coord + dx - dy) +
             load_bilinear_premul(limit, edge_coord - dx + dy) +
             load_bilinear_premul(limit, edge_coord + dx + dy)) * 0.125;
    } else {
        vec2 dx = source_dx * 0.5;
        vec2 dy = source_dy * 0.5;
        premul =
            (load_bilinear_premul(limit, edge_coord - dx - dy) +
             load_bilinear_premul(limit, edge_coord + dx - dy) +
             load_bilinear_premul(limit, edge_coord - dx + dy) +
             load_bilinear_premul(limit, edge_coord + dx + dy)) * 0.25;
    }
    if (premul.a > 0.00001) {
        premul.rgb /= premul.a;
    } else {
        premul.rgb = vec3(0.0);
    }
    return clamp(premul, vec4(0.0), vec4(1.0));
}

float load_mask_bilinear(vec2 edge_coord) {
    ivec2 limit = imageSize(mask_img) - ivec2(1);
    vec2 center_coord = clamp(edge_coord - vec2(0.5), vec2(0.0), vec2(limit));
    ivec2 p0 = ivec2(floor(center_coord));
    ivec2 p1 = clamp(p0 + ivec2(1), ivec2(0), limit);
    vec2 f = clamp(fract(center_coord), vec2(0.0), vec2(1.0));
    float a00 = imageLoad(mask_img, p0).a;
    float a10 = imageLoad(mask_img, ivec2(p1.x, p0.y)).a;
    float a01 = imageLoad(mask_img, ivec2(p0.x, p1.y)).a;
    float a11 = imageLoad(mask_img, p1).a;
    return clamp(mix(mix(a00, a10, f.x), mix(a01, a11, f.x), f.y),
                 0.0, 1.0);
}

float load_mask_minified(vec2 edge_coord,
                          vec2 mask_dx, vec2 mask_dy) {
    float footprint = max(length(mask_dx), length(mask_dy));
    if (footprint <= 1.0001) {
        return load_mask_bilinear(edge_coord);
    }
    vec2 dx = mask_dx * 0.5;
    vec2 dy = mask_dy * 0.5;
    return (load_mask_bilinear(edge_coord - dx - dy) +
            load_mask_bilinear(edge_coord + dx - dy) +
            load_mask_bilinear(edge_coord - dx + dy) +
            load_mask_bilinear(edge_coord + dx + dy)) * 0.25;
}

vec4 straight_from_premul(vec3 rgb, float a) {
    return clamp(vec4(a > 0.00001 ? rgb / a : vec3(0.0), a),
                 vec4(0.0), vec4(1.0));
}

vec4 blend_over(vec4 dst, vec4 src) {
    float out_a = src.a + dst.a * (1.0 - src.a);
    vec3 premul = src.rgb * src.a + dst.rgb * dst.a * (1.0 - src.a);
    return straight_from_premul(premul, out_a);
}

vec4 blend_add_compatible(vec4 dst, vec4 src) {
    float out_a = dst.a;
    vec3 premul = dst.rgb * dst.a + src.rgb * src.a;
    return straight_from_premul(premul, out_a);
}

vec4 blend_multiply_compatible(vec4 dst, vec4 src) {
    float out_a = dst.a;
    vec3 dst_premul = dst.rgb * dst.a;
    vec3 src_premul = src.rgb * src.a;
    vec3 premul = src_premul * dst_premul + dst_premul * (1.0 - src.a);
    return straight_from_premul(premul, out_a);
}

float color_burn(float src, float dst) {
    if (abs(dst - 1.0) < 0.000001) {
        return 1.0;
    }
    if (abs(src) < 0.000001) {
        return 0.0;
    }
    return 1.0 - min(1.0, (1.0 - dst) / src);
}

float color_dodge(float src, float dst) {
    if (dst <= 0.0) {
        return 0.0;
    }
    if (abs(src - 1.0) < 0.000001) {
        return 1.0;
    }
    return min(1.0, dst / (1.0 - src));
}

float overlay(float src, float dst) {
    float mul = 2.0 * src * dst;
    float scr = 1.0 - 2.0 * (1.0 - src) * (1.0 - dst);
    return dst < 0.5 ? mul : scr;
}

float soft_light(float src, float dst) {
    float val1 = dst - (1.0 - 2.0 * src) * dst * (1.0 - dst);
    float val2 = dst + (2.0 * src - 1.0) * dst *
                         ((16.0 * dst - 12.0) * dst + 3.0);
    float val3 = dst + (2.0 * src - 1.0) * (sqrt(dst) - dst);
    if (src <= 0.5) {
        return val1;
    }
    if (dst <= 0.25) {
        return val2;
    }
    return val3;
}

float hard_light(float src, float dst) {
    float mul = 2.0 * src * dst;
    float scr = 1.0 - 2.0 * (1.0 - src) * (1.0 - dst);
    return src < 0.5 ? mul : scr;
}

float linear_light(float src, float dst) {
    float burn = max(0.0, 2.0 * src + dst - 1.0);
    float dodge = min(1.0, 2.0 * (src - 0.5) + dst);
    return src < 0.5 ? burn : dodge;
}

vec3 color_blend(int mode, vec3 src, vec3 dst) {
    if (mode == 3) {
        return min(src + dst, vec3(1.0));
    }
    if (mode == 4) {
        return src + dst;
    }
    if (mode == 5) {
        return min(src, dst);
    }
    if (mode == 6) {
        return src * dst;
    }
    if (mode == 7) {
        return vec3(color_burn(src.r, dst.r), color_burn(src.g, dst.g),
                    color_burn(src.b, dst.b));
    }
    if (mode == 8) {
        return max(vec3(0.0), src + dst - vec3(1.0));
    }
    if (mode == 9) {
        return max(src, dst);
    }
    if (mode == 10) {
        return src + dst - src * dst;
    }
    if (mode == 11) {
        return vec3(color_dodge(src.r, dst.r), color_dodge(src.g, dst.g),
                    color_dodge(src.b, dst.b));
    }
    if (mode == 12) {
        return vec3(overlay(src.r, dst.r), overlay(src.g, dst.g),
                    overlay(src.b, dst.b));
    }
    if (mode == 13) {
        return vec3(soft_light(src.r, dst.r), soft_light(src.g, dst.g),
                    soft_light(src.b, dst.b));
    }
    if (mode == 14) {
        return vec3(hard_light(src.r, dst.r), hard_light(src.g, dst.g),
                    hard_light(src.b, dst.b));
    }
    if (mode == 15) {
        return vec3(linear_light(src.r, dst.r), linear_light(src.g, dst.g),
                    linear_light(src.b, dst.b));
    }
    return src;
}

vec4 blend_cubism(vec4 dst, vec4 src, int flags) {
    int color_mode = flags & 255;
    int alpha_mode = (flags >> 8) & 255;
    if (color_mode == 0 && alpha_mode == 0) {
        return blend_over(dst, src);
    }
    if (color_mode == 1 && alpha_mode == 0) {
        return blend_add_compatible(dst, src);
    }
    if (color_mode == 2 && alpha_mode == 0) {
        return blend_multiply_compatible(dst, src);
    }
    vec3 color = color_blend(color_mode, src.rgb, dst.rgb);
    vec3 parameter;
    if (alpha_mode == 1) {
        parameter = vec3(src.a * dst.a, 0.0, dst.a * (1.0 - src.a));
    } else if (alpha_mode == 2) {
        parameter = vec3(0.0, 0.0, dst.a * (1.0 - src.a));
    } else if (alpha_mode == 3) {
        parameter = vec3(min(src.a, dst.a), max(src.a - dst.a, 0.0),
                         max(dst.a - src.a, 0.0));
    } else if (alpha_mode == 4) {
        parameter = vec3(max(src.a + dst.a - 1.0, 0.0),
                         min(src.a, 1.0 - dst.a),
                         min(dst.a, 1.0 - src.a));
    } else {
        parameter = vec3(src.a * dst.a, src.a * (1.0 - dst.a),
                         dst.a * (1.0 - src.a));
    }
    return straight_from_premul(color * parameter.x +
                                src.rgb * parameter.y +
                                dst.rgb * parameter.z,
                                parameter.x + parameter.y + parameter.z);
}

vec2 vertex_dst(int base, int vertex) {
    int i = base + vertex * 6;
    return vec2(vertices.value[i + 0], vertices.value[i + 1]);
}

vec2 vertex_src(int base, int vertex) {
    int i = base + vertex * 6;
    return vec2(vertices.value[i + 2], vertices.value[i + 3]);
}

vec2 vertex_mask(int base, int vertex) {
    int i = base + vertex * 6;
    return vec2(vertices.value[i + 4], vertices.value[i + 5]);
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
    float opacity = clamp(float(pc.rect1.w) / 255.0, 0.0, 1.0);
    bool preserve_detail =
        (uint(pc.color0.z) & 0x40000000u) != 0u;
    int blend_flags = int(uint(pc.color0.z) & 0x3fffffffu);
    bool inverted_mask = (blend_flags & 65536) != 0;
    blend_flags = blend_flags & 65535;
    vec4 dst = imageLoad(dst_img, dst_pos);
    bool covered = false;

    for (int tri = 0; tri < tri_count; ++tri) {
        int base = pc.color0.w + tri * 22;
        vec4 tri_bounds = vec4(vertices.value[base + 18],
                               vertices.value[base + 19],
                               vertices.value[base + 20],
                               vertices.value[base + 21]);
        if (any(lessThan(p, tri_bounds.xy)) ||
            any(greaterThan(p, tri_bounds.zw))) {
            continue;
        }
        vec2 d0 = vertex_dst(base, 0);
        vec2 d1 = vertex_dst(base, 1);
        vec2 d2 = vertex_dst(base, 2);
        float area = edge(d0, d1, d2);
        if (abs(area) < 0.00001) {
            continue;
        }
        float w0 = edge(d1, d2, p) / area;
        float w1 = edge(d2, d0, p) / area;
        float w2 = edge(d0, d1, p) / area;
        if (w0 >= -0.0001 && w1 >= -0.0001 && w2 >= -0.0001) {
            vec2 src_pos_f = vertex_src(base, 0) * w0 +
                             vertex_src(base, 1) * w1 +
                             vertex_src(base, 2) * w2;
            vec2 mask_pos_f = vertex_mask(base, 0) * w0 +
                              vertex_mask(base, 1) * w1 +
                              vertex_mask(base, 2) * w2;
            vec2 source10 = vertex_src(base, 1) - vertex_src(base, 0);
            vec2 source20 = vertex_src(base, 2) - vertex_src(base, 0);
            vec2 mask10 = vertex_mask(base, 1) - vertex_mask(base, 0);
            vec2 mask20 = vertex_mask(base, 2) - vertex_mask(base, 0);
            float dw1dx = (d0.y - d2.y) / area;
            float dw1dy = (d2.x - d0.x) / area;
            float dw2dx = (d1.y - d0.y) / area;
            float dw2dy = (d0.x - d1.x) / area;
            vec2 source_dx = source10 * dw1dx + source20 * dw2dx;
            vec2 source_dy = source10 * dw1dy + source20 * dw2dy;
            vec2 mask_dx = mask10 * dw1dx + mask20 * dw2dx;
            vec2 mask_dy = mask10 * dw1dy + mask20 * dw2dy;
            float mask_val = 1.0 - load_mask_minified(
                mask_pos_f, mask_dx, mask_dy);
            if (inverted_mask) {
                mask_val = 1.0 - mask_val;
            }
            if (mask_val <= 0.00001) {
                continue;
            }
            vec4 src = load_minified(
                src_limit, src_pos_f, source_dx, source_dy,
                preserve_detail);
            if (src.g >= 0.70 && src.g > src.r + 0.20 && src.g > src.b + 0.20) {
                src.a = 0.0;
            }
            src.a *= opacity * mask_val;
            if (src.a <= 0.00001) {
                continue;
            }
            dst = blend_cubism(dst, src, blend_flags);
            covered = true;
            // As above, a pixel belongs to one triangle of an ArtMesh.  This
            // turns the common case from a full 64-triangle scan into an
            // early-out while keeping drawable-to-drawable ordering intact.
            break;
        }
    }
    if (covered) {
        imageStore(dst_img, dst_pos, dst);
    }
}
)GLSL");

    Ref<RDShaderSPIRV> spirv = rd->shader_compile_spirv_from_source(source);
    if (spirv.is_null()) return false;
    const String compile_error =
        spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE);
    if (!compile_error.is_empty()) {
        UtilityFunctions::printerr(
            "Godot GPU draw masked triangles shader compile error: ",
            compile_error);
        return false;
    }
    g_gpu_pipeline_state->draw_masked_triangles_shader =
        rd->shader_create_from_spirv(spirv, "AetherKiriDrawMaskedTriangles");
    if (!g_gpu_pipeline_state->draw_masked_triangles_shader.is_valid()) {
        return false;
    }
    g_gpu_pipeline_state->draw_masked_triangles_pipeline =
        rd->compute_pipeline_create(
            g_gpu_pipeline_state->draw_masked_triangles_shader);
    return g_gpu_pipeline_state->draw_masked_triangles_pipeline.is_valid();
}

enum class Live2DRasterBlend {
    Normal,
    Add,
    Multiply,
    MaskWrite,
};

constexpr uint32_t kGodotGpuRasterMasked = 0x08000000u;

bool IsLive2DHardwareRasterOp(const std::shared_ptr<GodotGpuOp> &op) {
    if (!Live2DHardwareRasterEnabled() || op == nullptr ||
        op->src == op->dst ||
        (op->type != GodotGpuOp::Type::DrawTriangles &&
         op->type != GodotGpuOp::Type::DrawMaskedTriangles) ||
        (op->type == GodotGpuOp::Type::DrawMaskedTriangles &&
         op->src2 == op->dst)) {
        return false;
    }
    if (op->type == GodotGpuOp::Type::DrawTriangles &&
        (op->color & TVP_GODOT_GPU_BLEND_TVP_OPERATION) != 0u) {
        return false;
    }
    const bool mask_write =
        op->type == GodotGpuOp::Type::DrawTriangles &&
        (op->color & TVP_GODOT_GPU_BLEND_MASK_WRITE) != 0u;
    const uint32_t flags = op->color & 0xffffu;
    const uint32_t color_mode = flags & 0xffu;
    const uint32_t alpha_mode = (flags >> 8u) & 0xffu;
    return mask_write || (alpha_mode == 0u && color_mode <= 2u);
}

Live2DRasterBlend Live2DRasterBlendForOp(const GodotGpuOp &op) {
    if (op.type == GodotGpuOp::Type::DrawTriangles &&
        (op.color & TVP_GODOT_GPU_BLEND_MASK_WRITE) != 0u) {
        return Live2DRasterBlend::MaskWrite;
    }
    switch (op.color & 0xffu) {
        case 1u:
            return Live2DRasterBlend::Add;
        case 2u:
            return Live2DRasterBlend::Multiply;
        default:
            return Live2DRasterBlend::Normal;
    }
}

RID &Live2DRasterPipelineSlot(Live2DRasterBlend blend) {
    switch (blend) {
        case Live2DRasterBlend::Add:
            return g_gpu_pipeline_state->live2d_raster_add_pipeline;
        case Live2DRasterBlend::Multiply:
            return g_gpu_pipeline_state->live2d_raster_multiply_pipeline;
        case Live2DRasterBlend::MaskWrite:
            return g_gpu_pipeline_state->live2d_raster_mask_pipeline;
        default:
            return g_gpu_pipeline_state->live2d_raster_normal_pipeline;
    }
}

bool EnsureLive2DRasterShader(RenderingDevice *rd) {
    if (rd == nullptr) return false;
    if (g_gpu_pipeline_state == nullptr) {
        g_gpu_pipeline_state = new GodotGpuPipelineState();
    }
    if (g_gpu_pipeline_state->live2d_raster_shader.is_valid() &&
        g_gpu_pipeline_state->live2d_raster_sampler.is_valid()) {
        return true;
    }

    Ref<RDShaderSource> source;
    source.instantiate();
    source->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);
    source->set_stage_source(
        RenderingDevice::SHADER_STAGE_VERTEX,
        R"GLSL(#version 450
layout(std430, set = 0, binding = 0) readonly buffer Vertices {
    float value[];
} vertices;
layout(push_constant, std430) uniform Params {
    ivec4 rect0;
    ivec4 rect1;
    ivec4 color0;
} pc;
layout(location = 0) out vec2 source_coord;
layout(location = 1) out vec2 mask_coord;

void main() {
    int vertex_index = gl_VertexIndex;
    int triangle = vertex_index / 3;
    int corner = vertex_index - triangle * 3;
    bool masked = (uint(pc.color0.z) & 0x08000000u) != 0u;
    int base = pc.color0.w + triangle * (masked ? 22 : 16) +
               corner * (masked ? 6 : 4);
    vec2 destination = vec2(vertices.value[base + 0],
                            vertices.value[base + 1]);
    source_coord = vec2(vertices.value[base + 2],
                        vertices.value[base + 3]);
    mask_coord = masked
        ? vec2(vertices.value[base + 4], vertices.value[base + 5])
        : vec2(0.0);
    vec2 target_size = max(vec2(pc.rect0.zw), vec2(1.0));
    vec2 position = vec2(destination.x / target_size.x * 2.0 - 1.0,
                         destination.y / target_size.y * 2.0 - 1.0);
    gl_Position = vec4(position, 0.0, 1.0);
}
)GLSL");
    source->set_stage_source(
        RenderingDevice::SHADER_STAGE_FRAGMENT,
        R"GLSL(#version 450
layout(set = 0, binding = 1) uniform sampler2D source_texture;
layout(set = 0, binding = 2) uniform sampler2D mask_texture;
layout(push_constant, std430) uniform Params {
    ivec4 rect0;
    ivec4 rect1;
    ivec4 color0;
} pc;
layout(location = 0) in vec2 source_coord;
layout(location = 1) in vec2 mask_coord;
layout(location = 0) out vec4 fragment_color;

vec4 load_raster_source(vec2 edge_coord) {
    // Live2D textures contain large transparent/chroma-keyed regions. A
    // regular sampler interpolates their hidden RGB values before alpha is
    // applied, which turns transparent edges into dark/black patches. Match
    // the compute rasterizer and interpolate in premultiplied-alpha space.
    ivec2 limit = textureSize(source_texture, 0) - ivec2(1);
    vec2 center_coord = clamp(edge_coord - vec2(0.5), vec2(0.0), vec2(limit));
    ivec2 p0 = ivec2(floor(center_coord));
    ivec2 p1 = clamp(p0 + ivec2(1), ivec2(0), limit);
    vec2 f = clamp(fract(center_coord), vec2(0.0), vec2(1.0));
    vec4 c00 = texelFetch(source_texture, p0, 0);
    vec4 c10 = texelFetch(source_texture, ivec2(p1.x, p0.y), 0);
    vec4 c01 = texelFetch(source_texture, ivec2(p0.x, p1.y), 0);
    vec4 c11 = texelFetch(source_texture, p1, 0);
    c00.rgb *= c00.a;
    c10.rgb *= c10.a;
    c01.rgb *= c01.a;
    c11.rgb *= c11.a;
    vec4 premul = mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);
    if (premul.a > 0.00001) {
        premul.rgb /= premul.a;
    } else {
        premul.rgb = vec3(0.0);
    }
    return clamp(premul, vec4(0.0), vec4(1.0));
}

float load_raster_mask(vec2 edge_coord) {
    ivec2 limit = textureSize(mask_texture, 0) - ivec2(1);
    vec2 center_coord = clamp(edge_coord - vec2(0.5), vec2(0.0), vec2(limit));
    ivec2 p0 = ivec2(floor(center_coord));
    ivec2 p1 = clamp(p0 + ivec2(1), ivec2(0), limit);
    vec2 f = clamp(fract(center_coord), vec2(0.0), vec2(1.0));
    float a00 = texelFetch(mask_texture, p0, 0).a;
    float a10 = texelFetch(mask_texture, ivec2(p1.x, p0.y), 0).a;
    float a01 = texelFetch(mask_texture, ivec2(p0.x, p1.y), 0).a;
    float a11 = texelFetch(mask_texture, p1, 0).a;
    return clamp(mix(mix(a00, a10, f.x), mix(a01, a11, f.x), f.y),
                  0.0, 1.0);
}

void main() {
    vec4 source = load_raster_source(source_coord);
    // Texture alpha is authoritative; do not chroma-key valid green art.
    float alpha = source.a *
        clamp(float(pc.rect1.w) / 255.0, 0.0, 1.0);
    uint flags = uint(pc.color0.z);
    if ((flags & 0x08000000u) != 0u) {
        float mask_value = 1.0 -
            load_raster_mask(mask_coord);
        if ((flags & 0x00010000u) != 0u) {
            mask_value = 1.0 - mask_value;
        }
        alpha *= clamp(mask_value, 0.0, 1.0);
    }
    // The render target remains premultiplied for the whole drawable list.
    // A single compute pass converts the completed Live2D surface back to the
    // straight-alpha convention used by the rest of the bridge.
    fragment_color = vec4(source.rgb * alpha, alpha);
}
)GLSL");

    Ref<RDShaderSPIRV> spirv = rd->shader_compile_spirv_from_source(source);
    if (spirv.is_null()) return false;
    const String vertex_error = spirv->get_stage_compile_error(
        RenderingDevice::SHADER_STAGE_VERTEX);
    const String fragment_error = spirv->get_stage_compile_error(
        RenderingDevice::SHADER_STAGE_FRAGMENT);
    if (!vertex_error.is_empty() || !fragment_error.is_empty()) {
        UtilityFunctions::printerr(
            "Godot Live2D raster shader compile error: ", vertex_error,
            fragment_error);
        return false;
    }
    g_gpu_pipeline_state->live2d_raster_shader =
        rd->shader_create_from_spirv(spirv, "AetherKiriLive2DRaster");
    if (!g_gpu_pipeline_state->live2d_raster_shader.is_valid()) return false;

    Ref<RDSamplerState> sampler_state;
    sampler_state.instantiate();
    sampler_state->set_mag_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
    sampler_state->set_min_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
    sampler_state->set_mip_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
    sampler_state->set_repeat_u(
        RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
    sampler_state->set_repeat_v(
        RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
    sampler_state->set_repeat_w(
        RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
    g_gpu_pipeline_state->live2d_raster_sampler =
        rd->sampler_create(sampler_state);
    return g_gpu_pipeline_state->live2d_raster_sampler.is_valid();
}

bool EnsureLive2DRasterPipeline(RenderingDevice *rd, const RID &framebuffer,
                                Live2DRasterBlend blend) {
    if (!EnsureLive2DRasterShader(rd) || !framebuffer.is_valid()) return false;
    RID &pipeline = Live2DRasterPipelineSlot(blend);
    if (pipeline.is_valid()) return true;

    Ref<RDPipelineRasterizationState> rasterization;
    rasterization.instantiate();
    rasterization->set_cull_mode(RenderingDevice::POLYGON_CULL_DISABLED);
    Ref<RDPipelineMultisampleState> multisample;
    multisample.instantiate();
    Ref<RDPipelineDepthStencilState> depth_stencil;
    depth_stencil.instantiate();
    Ref<RDPipelineColorBlendStateAttachment> attachment;
    attachment.instantiate();
    attachment->set_enable_blend(true);
    attachment->set_color_blend_op(RenderingDevice::BLEND_OP_ADD);
    attachment->set_alpha_blend_op(RenderingDevice::BLEND_OP_ADD);
    attachment->set_write_r(blend != Live2DRasterBlend::MaskWrite);
    attachment->set_write_g(blend != Live2DRasterBlend::MaskWrite);
    attachment->set_write_b(blend != Live2DRasterBlend::MaskWrite);
    attachment->set_write_a(true);
    if (blend == Live2DRasterBlend::Normal) {
        attachment->set_src_color_blend_factor(
            RenderingDevice::BLEND_FACTOR_ONE);
        attachment->set_dst_color_blend_factor(
            RenderingDevice::BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
        attachment->set_src_alpha_blend_factor(
            RenderingDevice::BLEND_FACTOR_ONE);
        attachment->set_dst_alpha_blend_factor(
            RenderingDevice::BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
    } else if (blend == Live2DRasterBlend::Add) {
        attachment->set_src_color_blend_factor(
            RenderingDevice::BLEND_FACTOR_ONE);
        attachment->set_dst_color_blend_factor(
            RenderingDevice::BLEND_FACTOR_ONE);
        attachment->set_src_alpha_blend_factor(
            RenderingDevice::BLEND_FACTOR_ZERO);
        attachment->set_dst_alpha_blend_factor(
            RenderingDevice::BLEND_FACTOR_ONE);
    } else if (blend == Live2DRasterBlend::Multiply) {
        attachment->set_src_color_blend_factor(
            RenderingDevice::BLEND_FACTOR_DST_COLOR);
        attachment->set_dst_color_blend_factor(
            RenderingDevice::BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
        attachment->set_src_alpha_blend_factor(
            RenderingDevice::BLEND_FACTOR_ZERO);
        attachment->set_dst_alpha_blend_factor(
            RenderingDevice::BLEND_FACTOR_ONE);
    } else {
        attachment->set_src_color_blend_factor(
            RenderingDevice::BLEND_FACTOR_ZERO);
        attachment->set_dst_color_blend_factor(
            RenderingDevice::BLEND_FACTOR_ONE);
        attachment->set_src_alpha_blend_factor(
            RenderingDevice::BLEND_FACTOR_ZERO);
        attachment->set_dst_alpha_blend_factor(
            RenderingDevice::BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
    }
    TypedArray<Ref<RDPipelineColorBlendStateAttachment>> attachments;
    attachments.push_back(attachment);
    Ref<RDPipelineColorBlendState> color_blend;
    color_blend.instantiate();
    color_blend->set_attachments(attachments);
    pipeline = rd->render_pipeline_create(
        g_gpu_pipeline_state->live2d_raster_shader,
        rd->framebuffer_get_format(framebuffer), -1,
        RenderingDevice::RENDER_PRIMITIVE_TRIANGLES, rasterization,
        multisample, depth_stencil, color_blend);
    return pipeline.is_valid();
}

bool EnsureLive2DUnpremultiplyPipeline(RenderingDevice *rd) {
    if (rd == nullptr) return false;
    if (g_gpu_pipeline_state == nullptr) {
        g_gpu_pipeline_state = new GodotGpuPipelineState();
    }
    if (g_gpu_pipeline_state->live2d_unpremultiply_pipeline.is_valid()) {
        return true;
    }
    Ref<RDShaderSource> source;
    source.instantiate();
    source->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);
    source->set_stage_source(
        RenderingDevice::SHADER_STAGE_COMPUTE,
        R"GLSL(#version 450
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(rgba8, set = 0, binding = 0) uniform image2D target_image;
layout(push_constant, std430) uniform Params {
    ivec4 rect;
} pc;
void main() {
    ivec2 local = ivec2(gl_GlobalInvocationID.xy);
    if (local.x >= pc.rect.z || local.y >= pc.rect.w) return;
    ivec2 position = pc.rect.xy + local;
    vec4 color = imageLoad(target_image, position);
    color.rgb = color.a > 0.00001 ? color.rgb / color.a : vec3(0.0);
    imageStore(target_image, position, clamp(color, vec4(0.0), vec4(1.0)));
}
)GLSL");
    Ref<RDShaderSPIRV> spirv = rd->shader_compile_spirv_from_source(source);
    if (spirv.is_null()) return false;
    const String error = spirv->get_stage_compile_error(
        RenderingDevice::SHADER_STAGE_COMPUTE);
    if (!error.is_empty()) {
        UtilityFunctions::printerr(
            "Godot Live2D unpremultiply shader compile error: ", error);
        return false;
    }
    g_gpu_pipeline_state->live2d_unpremultiply_shader =
        rd->shader_create_from_spirv(spirv,
                                     "AetherKiriLive2DUnpremultiply");
    if (!g_gpu_pipeline_state->live2d_unpremultiply_shader.is_valid()) {
        return false;
    }
    g_gpu_pipeline_state->live2d_unpremultiply_pipeline =
        rd->compute_pipeline_create(
            g_gpu_pipeline_state->live2d_unpremultiply_shader);
    return g_gpu_pipeline_state->live2d_unpremultiply_pipeline.is_valid();
}

bool EnsureMosaicPipeline(RenderingDevice *rd) {
    if (rd == nullptr) return false;
    if (g_gpu_pipeline_state == nullptr) {
        g_gpu_pipeline_state = new GodotGpuPipelineState();
    }
    if (g_gpu_pipeline_state->mosaic_pipeline.is_valid()) return true;

    Ref<RDShaderSource> source;
    source.instantiate();
    source->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);
    source->set_stage_source(
        RenderingDevice::SHADER_STAGE_COMPUTE,
        R"GLSL(#version 450
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(std430, set = 0, binding = 0) readonly buffer Rects {
    float value[];
} rects;
layout(rgba8, set = 0, binding = 1) uniform readonly image2D src_img;
layout(rgba8, set = 0, binding = 2) uniform image2D dst_img;
layout(push_constant, std430) uniform Params {
    ivec4 rect0;
    ivec4 rect1;
    ivec4 color0;
} pc;

void main() {
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
    ivec2 limit = max(pc.rect1.xy, ivec2(0));
    if (pos.x >= limit.x || pos.y >= limit.y) {
        return;
    }

    ivec2 block = max(pc.color0.xy, ivec2(1));
    int rect_count = max(pc.rect1.z, 0);
    for (int i = 0; i < rect_count; ++i) {
        int base = i * 4;
        ivec4 rect = ivec4(round(vec4(rects.value[base + 0],
                                      rects.value[base + 1],
                                      rects.value[base + 2],
                                      rects.value[base + 3])));
        if (rect.z <= 0 || rect.w <= 0) {
            continue;
        }
        ivec2 rect_min = rect.xy;
        ivec2 rect_max = rect.xy + rect.zw;
        if (pos.x < rect_min.x || pos.y < rect_min.y ||
            pos.x >= rect_max.x || pos.y >= rect_max.y) {
            continue;
        }

        ivec2 rel = pos - rect_min;
        ivec2 sample_pos = rect_min + (rel / block) * block + block / 2;
        sample_pos = clamp(sample_pos, rect_min, rect_max - ivec2(1));
        imageStore(dst_img, pos, imageLoad(src_img, sample_pos));
        return;
    }
}
)GLSL");

    Ref<RDShaderSPIRV> spirv = rd->shader_compile_spirv_from_source(source);
    if (spirv.is_null()) return false;
    const String compile_error =
        spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE);
    if (!compile_error.is_empty()) {
        UtilityFunctions::printerr("Godot GPU mosaic shader compile error: ",
                                   compile_error);
        return false;
    }
    g_gpu_pipeline_state->mosaic_shader =
        rd->shader_create_from_spirv(spirv, "AetherKiriMosaic");
    if (!g_gpu_pipeline_state->mosaic_shader.is_valid()) return false;
    g_gpu_pipeline_state->mosaic_pipeline =
        rd->compute_pipeline_create(g_gpu_pipeline_state->mosaic_shader);
    return g_gpu_pipeline_state->mosaic_pipeline.is_valid();
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

void InvalidateGodotGpuUniformSetsForResource(RenderingDevice *rd,
                                              const RID &resource) {
    if (!resource.is_valid()) return;
    const int64_t resource_id = resource.get_id();
    for (auto it = g_gpu_uniform_set_cache.begin();
         it != g_gpu_uniform_set_cache.end();) {
        const GodotGpuUniformSetKey &key = it->first;
        const bool references_resource =
            key.rid0 == resource_id || key.rid1 == resource_id ||
            key.rid2 == resource_id || key.rid3 == resource_id;
        if (!references_resource) {
            ++it;
            continue;
        }
        if (rd != nullptr && it->second.is_valid()) {
            rd->free_rid(it->second);
        }
        it = g_gpu_uniform_set_cache.erase(it);
    }
}

bool UpdateGodotGpuTriangleVertexBuffer(RenderingDevice *rd,
                                        const PackedByteArray &data,
                                        RID &vertex_buffer) {
    vertex_buffer = RID();
    if (rd == nullptr || data.is_empty()) {
        return false;
    }
    // The batched path uploads vertices before PrepareGodotGpuTriangles()
    // initializes a shader pipeline. Initialize the shared state here so the
    // first presentation frame cannot fail indefinitely waiting for some
    // unrelated GPU operation to create it.
    if (g_gpu_pipeline_state == nullptr) {
        g_gpu_pipeline_state = new GodotGpuPipelineState();
    }
    const uint64_t required = static_cast<uint64_t>(data.size());
    if (required > std::numeric_limits<uint32_t>::max()) return false;

    const PackedByteArray &cached =
        g_gpu_pipeline_state->triangle_vertex_buffer_data;
    const bool data_unchanged =
        cached.size() == data.size() &&
        (data.is_empty() ||
         std::memcmp(cached.ptr(), data.ptr(),
                     static_cast<size_t>(data.size())) == 0);
    if (g_gpu_pipeline_state->triangle_vertex_buffer.is_valid() &&
        g_gpu_pipeline_state->triangle_vertex_buffer_capacity >= required &&
        data_unchanged) {
        vertex_buffer = g_gpu_pipeline_state->triangle_vertex_buffer;
        return true;
    }

    const auto recreate_buffer = [&]() -> bool {
        const uint64_t capacity = required;
        if (capacity > std::numeric_limits<uint32_t>::max()) return false;

        if (g_gpu_pipeline_state->triangle_vertex_buffer.is_valid()) {
            // Uniform sets retain the buffer RID. Keep unrelated texture-only
            // sets alive while replacing this shared vertex buffer.
            InvalidateGodotGpuUniformSetsForResource(
                rd, g_gpu_pipeline_state->triangle_vertex_buffer);
            rd->free_rid(g_gpu_pipeline_state->triangle_vertex_buffer);
        }
        g_gpu_pipeline_state->triangle_vertex_buffer =
            rd->storage_buffer_create(static_cast<uint32_t>(capacity), data);
        g_gpu_pipeline_state->triangle_vertex_buffer_capacity =
            g_gpu_pipeline_state->triangle_vertex_buffer.is_valid()
                ? static_cast<uint32_t>(capacity) : 0;
        if (!g_gpu_pipeline_state->triangle_vertex_buffer.is_valid()) {
            g_gpu_pipeline_state->triangle_vertex_buffer_data.clear();
            return false;
        }
        g_gpu_pipeline_state->triangle_vertex_buffer_data = data;
        return true;
    };

    if (!g_gpu_pipeline_state->triangle_vertex_buffer.is_valid() ||
        g_gpu_pipeline_state->triangle_vertex_buffer_capacity < required) {
        if (!recreate_buffer()) return false;
    } else if (rd->buffer_update(
                   g_gpu_pipeline_state->triangle_vertex_buffer, 0,
                   static_cast<uint32_t>(required), data) == OK) {
        g_gpu_pipeline_state->triangle_vertex_buffer_data = data;
    } else if (!recreate_buffer()) {
        return false;
    }

    vertex_buffer = g_gpu_pipeline_state->triangle_vertex_buffer;
    return vertex_buffer.is_valid();
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

RID GetCachedBlend3UniformSet(RenderingDevice *rd, const RID &shader,
                              const RID &src1, const RID &src2,
                              const RID &src3, const RID &dst) {
    const GodotGpuUniformSetKey key{shader.get_id(), src1.get_id(),
                                    src2.get_id(), src3.get_id(), 4,
                                    dst.get_id()};
    auto it = g_gpu_uniform_set_cache.find(key);
    if(it != g_gpu_uniform_set_cache.end() && it->second.is_valid()) {
        return it->second;
    }

    TypedArray<RDUniform> uniforms;
    const RID resources[] = {src1, src2, src3, dst};
    for(int binding = 0; binding < 4; ++binding) {
        Ref<RDUniform> uniform;
        uniform.instantiate();
        uniform->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
        uniform->set_binding(binding);
        uniform->add_id(resources[binding]);
        uniforms.push_back(uniform);
    }
    RID uniform_set = rd->uniform_set_create(uniforms, shader, 0);
    if(uniform_set.is_valid()) g_gpu_uniform_set_cache[key] = uniform_set;
    return uniform_set;
}

RID GetCachedTriangleUniformSet(RenderingDevice *rd, const RID &shader,
                                const RID &vertex_buffer, const RID &src,
                                const RID &mask, const RID &dst,
                                bool masked) {
    const GodotGpuUniformSetKey key{
        shader.get_id(), vertex_buffer.get_id(), src.get_id(),
        masked ? mask.get_id() : dst.get_id(),
        static_cast<uint8_t>(masked ? 4 : 3),
        masked ? dst.get_id() : 0};
    auto it = g_gpu_uniform_set_cache.find(key);
    if (it != g_gpu_uniform_set_cache.end() && it->second.is_valid()) {
        return it->second;
    }

    TypedArray<RDUniform> uniforms;
    const RID resources[] = {vertex_buffer, src, mask, dst};
    const int resource_count = masked ? 4 : 3;
    for (int binding = 0; binding < resource_count; ++binding) {
        Ref<RDUniform> uniform;
        uniform.instantiate();
        uniform->set_uniform_type(
            binding == 0 ? RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER
                         : RenderingDevice::UNIFORM_TYPE_IMAGE);
        uniform->set_binding(binding);
        const RID resource = !masked && binding == 2 ? dst : resources[binding];
        uniform->add_id(resource);
        uniforms.push_back(uniform);
    }
    RID uniform_set = rd->uniform_set_create(uniforms, shader, 0);
    if (uniform_set.is_valid()) {
        g_gpu_uniform_set_cache[key] = uniform_set;
    }
    return uniform_set;
}

RID GetLive2DFramebuffer(RenderingDevice *rd, const RID &target) {
    if (rd == nullptr || !target.is_valid()) return RID();
    const int64_t target_id = target.get_id();
    const auto found = g_live2d_framebuffer_cache.find(target_id);
    if (found != g_live2d_framebuffer_cache.end() &&
        found->second.is_valid()) {
        return found->second;
    }
    TypedArray<RID> attachments;
    attachments.push_back(target);
    RID framebuffer = rd->framebuffer_create(attachments);
    if (framebuffer.is_valid()) {
        g_live2d_framebuffer_cache[target_id] = framebuffer;
    }
    return framebuffer;
}

void InvalidateLive2DFramebuffer(RenderingDevice *rd, const RID &target) {
    if (!target.is_valid()) return;
    const auto found = g_live2d_framebuffer_cache.find(target.get_id());
    if (found == g_live2d_framebuffer_cache.end()) return;
    if (rd != nullptr && found->second.is_valid()) {
        rd->free_rid(found->second);
    }
    g_live2d_framebuffer_cache.erase(found);
}

RID GetCachedLive2DRasterUniformSet(RenderingDevice *rd,
                                    const RID &vertex_buffer,
                                    const RID &source,
                                    const RID &mask) {
    if (rd == nullptr || g_gpu_pipeline_state == nullptr) return RID();
    const RID shader = g_gpu_pipeline_state->live2d_raster_shader;
    const RID sampler = g_gpu_pipeline_state->live2d_raster_sampler;
    const GodotGpuUniformSetKey key{
        shader.get_id(), vertex_buffer.get_id(), source.get_id(),
        mask.get_id(), 0x82u, sampler.get_id()};
    const auto found = g_gpu_uniform_set_cache.find(key);
    if (found != g_gpu_uniform_set_cache.end() &&
        found->second.is_valid()) {
        return found->second;
    }

    Ref<RDUniform> vertices;
    vertices.instantiate();
    vertices->set_uniform_type(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER);
    vertices->set_binding(0);
    vertices->add_id(vertex_buffer);
    Ref<RDUniform> source_texture;
    source_texture.instantiate();
    source_texture->set_uniform_type(
        RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
    source_texture->set_binding(1);
    source_texture->add_id(sampler);
    source_texture->add_id(source);
    Ref<RDUniform> mask_texture;
    mask_texture.instantiate();
    mask_texture->set_uniform_type(
        RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
    mask_texture->set_binding(2);
    mask_texture->add_id(sampler);
    mask_texture->add_id(mask);
    TypedArray<RDUniform> uniforms;
    uniforms.push_back(vertices);
    uniforms.push_back(source_texture);
    uniforms.push_back(mask_texture);
    RID uniform_set = rd->uniform_set_create(uniforms, shader, 0);
    if (uniform_set.is_valid()) {
        g_gpu_uniform_set_cache[key] = uniform_set;
    }
    return uniform_set;
}

RID GetCachedLive2DUnpremultiplyUniformSet(RenderingDevice *rd,
                                           const RID &target) {
    if (rd == nullptr || g_gpu_pipeline_state == nullptr) return RID();
    const RID shader = g_gpu_pipeline_state->live2d_unpremultiply_shader;
    const GodotGpuUniformSetKey key{
        shader.get_id(), target.get_id(), 0, 0, 0x81u};
    const auto found = g_gpu_uniform_set_cache.find(key);
    if (found != g_gpu_uniform_set_cache.end() &&
        found->second.is_valid()) {
        return found->second;
    }
    Ref<RDUniform> image;
    image.instantiate();
    image->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
    image->set_binding(0);
    image->add_id(target);
    TypedArray<RDUniform> uniforms;
    uniforms.push_back(image);
    RID uniform_set = rd->uniform_set_create(uniforms, shader, 0);
    if (uniform_set.is_valid()) {
        g_gpu_uniform_set_cache[key] = uniform_set;
    }
    return uniform_set;
}

bool UnpremultiplyLive2DRasterTarget(RenderingDevice *rd, const RID &target,
                                     int left, int top, int right,
                                     int bottom) {
    if (right <= left || bottom <= top ||
        !EnsureLive2DUnpremultiplyPipeline(rd)) {
        return false;
    }
    const RID uniform_set =
        GetCachedLive2DUnpremultiplyUniformSet(rd, target);
    if (!uniform_set.is_valid()) return false;
    int32_t values[4] = {left, top, right - left, bottom - top};
    PackedByteArray constants;
    constants.resize(sizeof(values));
    if (uint8_t *bytes = constants.ptrw()) {
        std::memcpy(bytes, values, sizeof(values));
    }
    const int64_t compute_list = rd->compute_list_begin();
    rd->compute_list_bind_compute_pipeline(
        compute_list, g_gpu_pipeline_state->live2d_unpremultiply_pipeline);
    rd->compute_list_bind_uniform_set(compute_list, uniform_set, 0);
    rd->compute_list_set_push_constant(compute_list, constants,
                                       sizeof(values));
    rd->compute_list_dispatch(
        compute_list, static_cast<uint32_t>((right - left + 7) / 8),
        static_cast<uint32_t>((bottom - top + 7) / 8), 1);
    rd->compute_list_end();
    ApplyGodotGpuBarrier(rd);
    return true;
}

void ExecuteGodotGpuLive2DRasterBatch(
    RenderingDevice *rd,
    const std::vector<std::shared_ptr<GodotGpuOp>> &ops) {
    if (ops.empty()) return;
    g_gpu_live2d_raster_batches.fetch_add(1, std::memory_order_relaxed);
    g_gpu_live2d_raster_ops.fetch_add(
        static_cast<uint64_t>(ops.size()), std::memory_order_relaxed);
    if (rd == nullptr) {
        for (const auto &op : ops) FinishGodotGpuOp(op, false);
        return;
    }

    std::vector<size_t> float_offsets(ops.size(), 0);
    std::vector<float> combined_vertices;
    for (size_t index = 0; index < ops.size(); ++index) {
        while ((combined_vertices.size() & 3u) != 0u) {
            combined_vertices.push_back(0.0f);
        }
        float_offsets[index] = combined_vertices.size();
        combined_vertices.insert(combined_vertices.end(),
                                 ops[index]->vertices.begin(),
                                 ops[index]->vertices.end());
    }
    PackedByteArray vertex_data;
    vertex_data.resize(static_cast<int64_t>(combined_vertices.size() *
                                            sizeof(float)));
    if (uint8_t *bytes = vertex_data.ptrw()) {
        std::memcpy(bytes, combined_vertices.data(),
                    combined_vertices.size() * sizeof(float));
    }
    RID vertex_buffer;
    if (!UpdateGodotGpuTriangleVertexBuffer(rd, vertex_data, vertex_buffer) ||
        !EnsureLive2DRasterShader(rd)) {
        for (const auto &op : ops) FinishGodotGpuOp(op, false);
        return;
    }

    std::vector<bool> results(ops.size(), false);
    std::vector<RID> raster_uniform_sets(ops.size());
    std::vector<PackedByteArray> raster_constants(ops.size());
    size_t group_begin = 0;
    while (group_begin < ops.size()) {
        const RID target = ops[group_begin]->dst;
        const bool mask_write =
            Live2DRasterBlendForOp(*ops[group_begin]) ==
            Live2DRasterBlend::MaskWrite;
        size_t group_end = group_begin + 1u;
        while (group_end < ops.size() && ops[group_end]->dst == target &&
               ((Live2DRasterBlendForOp(*ops[group_end]) ==
                 Live2DRasterBlend::MaskWrite) == mask_write)) {
            ++group_end;
        }

        const RID framebuffer = GetLive2DFramebuffer(rd, target);
        int union_left = std::numeric_limits<int>::max();
        int union_top = std::numeric_limits<int>::max();
        int union_right = std::numeric_limits<int>::min();
        int union_bottom = std::numeric_limits<int>::min();
        bool drew = false;
        bool group_ready = framebuffer.is_valid();
        for (size_t index = group_begin;
             index < group_end && group_ready; ++index) {
            const auto &op = ops[index];
            const Live2DRasterBlend blend = Live2DRasterBlendForOp(*op);
            if (!EnsureLive2DRasterPipeline(rd, framebuffer, blend)) {
                group_ready = false;
                break;
            }
            const RID mask =
                op->type == GodotGpuOp::Type::DrawMaskedTriangles
                    ? op->src2 : op->src;
            raster_uniform_sets[index] = GetCachedLive2DRasterUniformSet(
                rd, vertex_buffer, op->src, mask);
            if (!raster_uniform_sets[index].is_valid()) {
                group_ready = false;
                break;
            }
            raster_constants[index] = PackGpuPushConstants(*op);
            if (uint8_t *bytes = raster_constants[index].ptrw()) {
                int32_t color_flags = 0;
                std::memcpy(&color_flags, bytes + 10 * sizeof(int32_t),
                            sizeof(color_flags));
                if (op->type == GodotGpuOp::Type::DrawMaskedTriangles) {
                    color_flags |=
                        static_cast<int32_t>(kGodotGpuRasterMasked);
                }
                const int32_t vertex_offset =
                    static_cast<int32_t>(float_offsets[index]);
                std::memcpy(bytes + 10 * sizeof(int32_t), &color_flags,
                            sizeof(color_flags));
                std::memcpy(bytes + 11 * sizeof(int32_t), &vertex_offset,
                            sizeof(vertex_offset));
            }
        }
        int64_t draw_list = -1;
        if (group_ready) {
            draw_list = rd->draw_list_begin(framebuffer);
        }
        for (size_t index = group_begin;
             index < group_end && draw_list >= 0; ++index) {
            const auto &op = ops[index];
            const Live2DRasterBlend blend = Live2DRasterBlendForOp(*op);
            rd->draw_list_bind_render_pipeline(
                draw_list, Live2DRasterPipelineSlot(blend));
            rd->draw_list_bind_uniform_set(
                draw_list, raster_uniform_sets[index], 0);
            rd->draw_list_set_push_constant(
                draw_list, raster_constants[index], 48);
            rd->draw_list_enable_scissor(
                draw_list, Rect2(op->dst_pos.x, op->dst_pos.y,
                                 op->size.x, op->size.y));
            rd->draw_list_draw(draw_list, false, 1,
                               static_cast<uint32_t>(op->mode * 3u));
            results[index] = true;
            drew = true;
            if (!mask_write) {
                union_left = std::min(
                    union_left, static_cast<int>(op->dst_pos.x));
                union_top = std::min(
                    union_top, static_cast<int>(op->dst_pos.y));
                union_right = std::max(
                    union_right,
                    static_cast<int>(op->dst_pos.x + op->size.x));
                union_bottom = std::max(
                    union_bottom,
                    static_cast<int>(op->dst_pos.y + op->size.y));
            }
        }
        if (draw_list >= 0) {
            rd->draw_list_end();
        }
        if (drew) ApplyGodotGpuBarrier(rd);
        if (drew && !mask_write &&
            !UnpremultiplyLive2DRasterTarget(
                rd, target, union_left, union_top, union_right,
                union_bottom)) {
            for (size_t index = group_begin; index < group_end; ++index) {
                results[index] = false;
            }
        }
        group_begin = group_end;
    }

    for (size_t index = 0; index < ops.size(); ++index) {
        FinishGodotGpuOp(ops[index], results[index]);
    }
}

bool DispatchGodotGpuBlend(RenderingDevice *rd,
                           const std::shared_ptr<GodotGpuOp> &op,
                           int64_t compute_list,
                           std::vector<RID> &uniform_sets,
                           const RID &source_override = RID()) {
    const bool alpha_blend_a = op->mode == TVP_GODOT_GPU_BLEND_ALPHA_BLEND_A;
    if (alpha_blend_a) {
        if (!EnsureAlphaBlendAPipeline(rd)) return false;
    } else if (!EnsureBlendPipeline(rd)) {
        return false;
    }

    RID source = source_override.is_valid() ? source_override : op->src;
    if (op->mode == TVP_GODOT_GPU_BLEND_FILL_ARGB) {
        // FillARGB does not sample its source, but the shared blend shader
        // still declares one. Binding the destination RID as both the
        // readonly source and writable destination is undefined on
        // Metal/Vulkan and can leave transparent text surfaces uncleared.
        // Bind a harmless non-aliased image instead while keeping the clear
        // in the ordered compute batch.
        if (!g_gpu_pipeline_state->fill_source_texture.is_valid()) {
            Ref<RDTextureView> view;
            view.instantiate();
            TypedArray<PackedByteArray> initial_data;
            g_gpu_pipeline_state->fill_source_texture = rd->texture_create(
                MakeRgbaTextureFormat(1, 1), view, initial_data);
        }
        if (!g_gpu_pipeline_state->fill_source_texture.is_valid()) {
            return false;
        }
        source = g_gpu_pipeline_state->fill_source_texture;
    }
    RID uniform_set = GetCachedBlendUniformSet(
        rd,
        alpha_blend_a ? g_gpu_pipeline_state->alpha_blend_a_shader
                      : g_gpu_pipeline_state->blend_shader,
        source, op->dst);
    if (!uniform_set.is_valid()) return false;
    (void)uniform_sets;

    PackedByteArray push_constants = PackGpuPushConstants(*op);
    if (source_override.is_valid() && op->mode == TVP_GODOT_GPU_BLEND_BOX_BLUR_ALPHA) {
        // The alias scratch texture is tightly sized to the source rectangle.
        // Its local origin replaces the original source texture coordinates.
        int32_t zero = 0;
        uint8_t *bytes = push_constants.ptrw();
        if (bytes != nullptr) {
            std::memcpy(bytes + 2 * sizeof(int32_t), &zero, sizeof(zero));
            std::memcpy(bytes + 3 * sizeof(int32_t), &zero, sizeof(zero));
        }
    }
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

bool DispatchGodotGpuBlend3(RenderingDevice *rd,
                            const std::shared_ptr<GodotGpuOp> &op,
                            int64_t compute_list,
                            std::vector<RID> &uniform_sets) {
    if(!EnsureBlend3Pipeline(rd)) return false;
    RID uniform_set = GetCachedBlend3UniformSet(
        rd, g_gpu_pipeline_state->blend3_shader, op->src, op->src2,
        op->src3, op->dst);
    if(!uniform_set.is_valid()) return false;
    (void)uniform_sets;
    const PackedByteArray push_constants = PackGpuPushConstants(*op);
    rd->compute_list_bind_compute_pipeline(
        compute_list, g_gpu_pipeline_state->blend3_pipeline);
    rd->compute_list_bind_uniform_set(compute_list, uniform_set, 0);
    rd->compute_list_set_push_constant(compute_list, push_constants, 48);
    rd->compute_list_dispatch(compute_list,
                              static_cast<uint32_t>((op->size.x + 7) / 8),
                              static_cast<uint32_t>((op->size.y + 7) / 8), 1);
    return true;
}

bool ExecuteGodotGpuBlend(RenderingDevice *rd,
                          const std::shared_ptr<GodotGpuOp> &op) {
    if (rd == nullptr || op == nullptr) return false;
    const bool alpha_convert =
        op->mode == TVP_GODOT_GPU_BLEND_ALPHA_TO_ADDITIVE_ALPHA ||
        op->mode == TVP_GODOT_GPU_BLEND_ADDITIVE_ALPHA_TO_ALPHA;
    if (alpha_convert && op->src == op->dst) {
        // Use a single read/write image binding for in-place conversion.  A
        // texture bound simultaneously as readonly source and writable
        // destination is not portable across Metal (iOS/macOS) and Vulkan.
        return ExecuteGodotGpuAlphaConvert(rd, op);
    }
    const bool alias_box_blur =
        op->src == op->dst &&
        op->mode == TVP_GODOT_GPU_BLEND_BOX_BLUR_ALPHA;
    if (op->src == op->dst && !alias_box_blur &&
        op->mode != TVP_GODOT_GPU_BLEND_FILL_ARGB &&
        op->mode != TVP_GODOT_GPU_BLEND_REMOVE_CONST_OPACITY &&
        op->mode != TVP_GODOT_GPU_BLEND_FILL_MASK &&
        op->mode != TVP_GODOT_GPU_BLEND_ALPHA_TO_ADDITIVE_ALPHA &&
        op->mode != TVP_GODOT_GPU_BLEND_ADDITIVE_ALPHA_TO_ALPHA) {
        g_gpu_alias_sources.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    std::vector<RID> uniform_sets;
    RID alias_source;
    if (alias_box_blur) {
        Ref<RDTextureView> view;
        view.instantiate();
        TypedArray<PackedByteArray> initial_data;
        alias_source = rd->texture_create(
            MakeRgbaTextureFormat(static_cast<uint32_t>(op->size.x),
                                  static_cast<uint32_t>(op->size.y)),
            view, initial_data);
        if (!alias_source.is_valid()) return false;
        if (rd->texture_copy(op->src, alias_source, op->src_pos, Vector3(),
                             op->size, 0, 0, 0, 0) != OK) {
            rd->free_rid(alias_source);
            return false;
        }
        ApplyGodotGpuBarrier(rd);
    }
    int64_t compute_list = rd->compute_list_begin();
    const bool ok = DispatchGodotGpuBlend(
        rd, op, compute_list, uniform_sets, alias_source);
    if (ok) {
        rd->compute_list_add_barrier(compute_list);
    }
    rd->compute_list_end();
    if (ok) {
        ApplyGodotGpuBarrier(rd);
    }
    for (const RID &uniform_set : uniform_sets) {
        rd->free_rid(uniform_set);
    }
    if (alias_source.is_valid()) {
        InvalidateGodotGpuUniformSetsForResource(rd, alias_source);
        rd->free_rid(alias_source);
    }
    return ok;
}

bool ExecuteGodotGpuBlend2(RenderingDevice *rd,
                           const std::shared_ptr<GodotGpuOp> &op) {
    if (rd == nullptr || op == nullptr) return false;
    if (op->src == op->dst || op->src2 == op->dst) {
        g_gpu_alias_sources.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    std::vector<RID> uniform_sets;
    int64_t compute_list = rd->compute_list_begin();
    const bool ok = DispatchGodotGpuBlend2(rd, op, compute_list, uniform_sets);
    if (ok) {
        rd->compute_list_add_barrier(compute_list);
    }
    rd->compute_list_end();
    if (ok) {
        ApplyGodotGpuBarrier(rd);
    }
    for (const RID &uniform_set : uniform_sets) {
        rd->free_rid(uniform_set);
    }
    return ok;
}

struct GodotGpuPreparedTriangles {
    RID pipeline;
    RID uniform_set;
    RID vertex_buffer;
    RID temp_src;
    PackedByteArray push_constants;
    uint32_t groups_x = 0;
    uint32_t groups_y = 0;
    bool owns_uniform_set = false;
    bool owns_vertex_buffer = false;
};

void FreeGodotGpuPreparedTriangles(RenderingDevice *rd,
                                   GodotGpuPreparedTriangles &prepared) {
    if (rd == nullptr) return;
    if (prepared.owns_uniform_set && prepared.uniform_set.is_valid()) {
        rd->free_rid(prepared.uniform_set);
    }
    if (prepared.temp_src.is_valid()) {
        // Batched triangle preparation may cache a set containing this
        // one-operation alias copy. Remove only that dependent set before the
        // temporary texture goes away.
        InvalidateGodotGpuUniformSetsForResource(rd, prepared.temp_src);
        rd->free_rid(prepared.temp_src);
    }
    if (prepared.owns_vertex_buffer && prepared.vertex_buffer.is_valid()) {
        InvalidateGodotGpuUniformSetsForResource(rd,
                                                 prepared.vertex_buffer);
        rd->free_rid(prepared.vertex_buffer);
    }
    prepared = {};
}

bool PrepareGodotGpuTriangles(RenderingDevice *rd,
                              const std::shared_ptr<GodotGpuOp> &op,
                              bool draw, bool masked,
                              GodotGpuPreparedTriangles &prepared,
                              const RID &shared_vertex_buffer = RID(),
                              int32_t vertex_offset = 0,
                              bool cache_uniform_set = false) {
    prepared = {};
    if (rd == nullptr || op == nullptr || op->vertices.empty()) return false;
    if (masked) {
        if (!EnsureDrawMaskedTrianglesPipeline(rd)) {
            g_gpu_triangle_pipeline_failed.fetch_add(
                1, std::memory_order_relaxed);
            return false;
        }
    } else if (!(draw ? EnsureDrawTrianglesPipeline(rd)
                      : EnsureCopyTrianglesPipeline(rd))) {
        g_gpu_triangle_pipeline_failed.fetch_add(
            1, std::memory_order_relaxed);
        return false;
    }

    if (shared_vertex_buffer.is_valid()) {
        prepared.vertex_buffer = shared_vertex_buffer;
    } else {
        PackedByteArray vertex_data;
        vertex_data.resize(
            static_cast<int64_t>(op->vertices.size() * sizeof(float)));
        if (uint8_t *bytes = vertex_data.ptrw()) {
            std::memcpy(bytes, op->vertices.data(),
                        op->vertices.size() * sizeof(float));
        }
        prepared.vertex_buffer =
            rd->storage_buffer_create(vertex_data.size(), vertex_data);
        prepared.owns_vertex_buffer = prepared.vertex_buffer.is_valid();
    }
    if (!prepared.vertex_buffer.is_valid()) {
        g_gpu_triangle_buffer_failed.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    RID sample_src = op->src;
    if (!masked && op->src == op->dst) {
        Ref<RDTextureView> view;
        view.instantiate();
        TypedArray<PackedByteArray> initial_data;
        prepared.temp_src = rd->texture_create(
            MakeRgbaTextureFormat(static_cast<uint32_t>(op->src_size.x),
                                  static_cast<uint32_t>(op->src_size.y)),
            view, initial_data);
        if (!prepared.temp_src.is_valid()) {
            FreeGodotGpuPreparedTriangles(rd, prepared);
            return false;
        }
        const Error copied = rd->texture_copy(
            op->src, prepared.temp_src, Vector3(), Vector3(), op->src_size,
            0, 0, 0, 0);
        if (copied != OK) {
            FreeGodotGpuPreparedTriangles(rd, prepared);
            return false;
        }
        ApplyGodotGpuBarrier(rd);
        sample_src = prepared.temp_src;
    }

    const RID shader = masked
        ? g_gpu_pipeline_state->draw_masked_triangles_shader
        : draw ? g_gpu_pipeline_state->draw_triangles_shader
               : g_gpu_pipeline_state->copy_triangles_shader;
    prepared.pipeline = masked
        ? g_gpu_pipeline_state->draw_masked_triangles_pipeline
        : draw ? g_gpu_pipeline_state->draw_triangles_pipeline
               : g_gpu_pipeline_state->copy_triangles_pipeline;
    if (cache_uniform_set) {
        prepared.uniform_set = GetCachedTriangleUniformSet(
            rd, shader, prepared.vertex_buffer, sample_src, op->src2,
            op->dst, masked);
    } else {
        TypedArray<RDUniform> uniforms;
        const RID resources[] = {prepared.vertex_buffer, sample_src,
                                 op->src2, op->dst};
        const int resource_count = masked ? 4 : 3;
        for (int binding = 0; binding < resource_count; ++binding) {
            Ref<RDUniform> uniform;
            uniform.instantiate();
            uniform->set_uniform_type(
                binding == 0
                    ? RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER
                    : RenderingDevice::UNIFORM_TYPE_IMAGE);
            uniform->set_binding(binding);
            const RID resource = !masked && binding == 2
                ? op->dst : resources[binding];
            uniform->add_id(resource);
            uniforms.push_back(uniform);
        }
        prepared.uniform_set = rd->uniform_set_create(uniforms, shader, 0);
        prepared.owns_uniform_set = prepared.uniform_set.is_valid();
    }
    if (!prepared.uniform_set.is_valid()) {
        g_gpu_triangle_uniform_failed.fetch_add(1, std::memory_order_relaxed);
        FreeGodotGpuPreparedTriangles(rd, prepared);
        return false;
    }

    prepared.push_constants = PackGpuPushConstants(*op);
    if (uint8_t *bytes = prepared.push_constants.ptrw()) {
        std::memcpy(bytes + 11 * sizeof(int32_t), &vertex_offset,
                    sizeof(vertex_offset));
    }
    prepared.groups_x = static_cast<uint32_t>((op->size.x + 7) / 8);
    prepared.groups_y = static_cast<uint32_t>((op->size.y + 7) / 8);
    return prepared.groups_x > 0 && prepared.groups_y > 0;
}

void DispatchGodotGpuPreparedTriangles(
    RenderingDevice *rd, int64_t compute_list,
    const GodotGpuPreparedTriangles &prepared) {
    rd->compute_list_bind_compute_pipeline(compute_list, prepared.pipeline);
    rd->compute_list_bind_uniform_set(compute_list, prepared.uniform_set, 0);
    rd->compute_list_set_push_constant(compute_list, prepared.push_constants, 48);
    rd->compute_list_dispatch(compute_list, prepared.groups_x,
                              prepared.groups_y, 1);
}

bool ExecuteGodotGpuTriangles(RenderingDevice *rd,
                              const std::shared_ptr<GodotGpuOp> &op,
                              bool draw) {
    GodotGpuPreparedTriangles prepared;
    if (!PrepareGodotGpuTriangles(rd, op, draw, false, prepared)) return false;

    int64_t compute_list = rd->compute_list_begin();
    DispatchGodotGpuPreparedTriangles(rd, compute_list, prepared);
    rd->compute_list_add_barrier(compute_list);
    rd->compute_list_end();
    ApplyGodotGpuBarrier(rd);
    FreeGodotGpuPreparedTriangles(rd, prepared);
    return true;
}

bool ExecuteGodotGpuMaskedTriangles(RenderingDevice *rd,
                                    const std::shared_ptr<GodotGpuOp> &op) {
    GodotGpuPreparedTriangles prepared;
    if (!PrepareGodotGpuTriangles(rd, op, true, true, prepared)) return false;

    int64_t compute_list = rd->compute_list_begin();
    DispatchGodotGpuPreparedTriangles(rd, compute_list, prepared);
    rd->compute_list_add_barrier(compute_list);
    rd->compute_list_end();
    ApplyGodotGpuBarrier(rd);
    FreeGodotGpuPreparedTriangles(rd, prepared);
    return true;
}

bool ExecuteGodotGpuMosaic(RenderingDevice *rd,
                           const std::shared_ptr<GodotGpuOp> &op) {
    if (rd == nullptr || op == nullptr || op->vertices.empty() ||
        op->size.x <= 0 || op->size.y <= 0 || !EnsureMosaicPipeline(rd)) {
        return false;
    }

    PackedByteArray rect_data;
    rect_data.resize(static_cast<int64_t>(op->vertices.size() * sizeof(float)));
    if (uint8_t *bytes = rect_data.ptrw()) {
        std::memcpy(bytes, op->vertices.data(),
                    op->vertices.size() * sizeof(float));
    }
    RID rect_buffer = rd->storage_buffer_create(rect_data.size(), rect_data);
    if (!rect_buffer.is_valid()) return false;

    Ref<RDTextureView> view;
    view.instantiate();
    TypedArray<PackedByteArray> initial_data;
    RID sample_src = rd->texture_create(
        MakeRgbaTextureFormat(static_cast<uint32_t>(op->size.x),
                              static_cast<uint32_t>(op->size.y)),
        view, initial_data);
    if (!sample_src.is_valid()) {
        rd->free_rid(rect_buffer);
        return false;
    }
    const Error copied = rd->texture_copy(op->dst, sample_src, Vector3(),
                                          Vector3(), op->size, 0, 0, 0, 0);
    if (copied != OK) {
        rd->free_rid(sample_src);
        rd->free_rid(rect_buffer);
        return false;
    }
    ApplyGodotGpuBarrier(rd);

    Ref<RDUniform> rect_uniform;
    rect_uniform.instantiate();
    rect_uniform->set_uniform_type(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER);
    rect_uniform->set_binding(0);
    rect_uniform->add_id(rect_buffer);

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
    uniforms.push_back(rect_uniform);
    uniforms.push_back(src_uniform);
    uniforms.push_back(dst_uniform);
    RID uniform_set = rd->uniform_set_create(
        uniforms, g_gpu_pipeline_state->mosaic_shader, 0);
    if (!uniform_set.is_valid()) {
        rd->free_rid(sample_src);
        rd->free_rid(rect_buffer);
        return false;
    }

    const PackedByteArray push_constants = PackGpuPushConstants(*op);
    int64_t compute_list = rd->compute_list_begin();
    rd->compute_list_bind_compute_pipeline(
        compute_list, g_gpu_pipeline_state->mosaic_pipeline);
    rd->compute_list_bind_uniform_set(compute_list, uniform_set, 0);
    rd->compute_list_set_push_constant(compute_list, push_constants, 48);
    rd->compute_list_dispatch(compute_list,
                              static_cast<uint32_t>((op->size.x + 7) / 8),
                              static_cast<uint32_t>((op->size.y + 7) / 8),
                              1);
    rd->compute_list_add_barrier(compute_list);
    rd->compute_list_end();
    ApplyGodotGpuBarrier(rd);
    rd->free_rid(uniform_set);
    rd->free_rid(sample_src);
    rd->free_rid(rect_buffer);
    return true;
}

bool ExecuteGodotGpuOp(RenderingDevice *rd, const std::shared_ptr<GodotGpuOp> &op) {
    if (rd == nullptr || op == nullptr) return false;
    GodotGpuOpExecutionProfile execution_profile(op);
    bool result = false;
    bool wrote_texture = false;
    switch (op->type) {
        case GodotGpuOp::Type::Update: {
            const auto update_started = std::chrono::steady_clock::now();
            const double queue_ms =
                op->profile_enqueued_at.time_since_epoch().count() != 0
                    ? std::chrono::duration<double, std::milli>(
                          update_started - op->profile_enqueued_at)
                          .count()
                    : 0.0;
            result = rd->texture_update(op->dst, 0, op->data) == OK;
            const double update_ms =
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - update_started)
                    .count();
            RenderingServer *server = RenderingServer::get_singleton();
            LogGodotGpuUpdateProfile(
                "texture_update", op->profile_width, op->profile_height,
                static_cast<size_t>(op->data.size()), update_ms,
                op->profile_pack_ms, queue_ms, result,
                server != nullptr && server->is_on_render_thread());
            wrote_texture = true;
            break;
        }
        case GodotGpuOp::Type::Clear:
            result = rd->texture_clear(op->dst, op->clear_color, 0, 1, 0, 1) == OK;
            wrote_texture = true;
            break;
        case GodotGpuOp::Type::Copy:
            result = rd->texture_copy(op->src, op->dst, op->src_pos, op->dst_pos,
                                      op->size, 0, 0, 0, 0) == OK;
            wrote_texture = true;
            break;
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
            if (copy_to_temp == OK) {
                ApplyGodotGpuBarrier(rd);
            }
            const Error copy_to_dst =
                copy_to_temp == OK
                    ? rd->texture_copy(temp, op->dst, Vector3(), op->dst_pos,
                                       op->size, 0, 0, 0, 0)
                    : FAILED;
            result = copy_to_temp == OK && copy_to_dst == OK;
            wrote_texture = true;
            if (result) {
                ApplyGodotGpuBarrier(rd);
            }
            rd->free_rid(temp);
            wrote_texture = false;
            break;
        }
        case GodotGpuOp::Type::CopyTriangles:
            return ExecuteGodotGpuTriangles(rd, op, false);
        case GodotGpuOp::Type::DrawTriangles:
            return ExecuteGodotGpuTriangles(rd, op, true);
        case GodotGpuOp::Type::DrawMaskedTriangles:
            return ExecuteGodotGpuMaskedTriangles(rd, op);
        case GodotGpuOp::Type::Mosaic:
            return ExecuteGodotGpuMosaic(rd, op);
        case GodotGpuOp::Type::Read:
            op->data = rd->texture_get_data(op->src, 0);
            return !op->data.is_empty();
        case GodotGpuOp::Type::ReadAsync:
            // Asynchronous reads are submitted by
            // BeginGodotGpuReadbackOnRenderThread after all preceding writes
            // in the ordered bridge queue have been drained.
            return false;
        case GodotGpuOp::Type::Blend:
            return ExecuteGodotGpuBlend(rd, op);
        case GodotGpuOp::Type::Blend2:
            return ExecuteGodotGpuBlend2(rd, op);
        case GodotGpuOp::Type::Blend3: {
            std::vector<RID> uniform_sets;
            int64_t compute_list = rd->compute_list_begin();
            const bool ok =
                DispatchGodotGpuBlend3(rd, op, compute_list, uniform_sets);
            if(ok) rd->compute_list_add_barrier(compute_list);
            rd->compute_list_end();
            if(ok) ApplyGodotGpuBarrier(rd);
            return ok;
        }
        case GodotGpuOp::Type::ArtemisShader:
            return ExecuteArtemisGpuShader(rd, op);
        case GodotGpuOp::Type::PrepareNativeWrite:
            // submit()/sync() are only valid for a local RenderingDevice.
            // Poll a marker on Godot's Metal queue instead. A target remains
            // retired while the marker is pending, so the runtime can render
            // another buffer without blocking the CPU or racing IOSurface
            // storage still sampled by Metal.
#if defined(__APPLE__)
            return AetherApplePollMetalCommandQueue(
                rd->get_driver_resource(
                    RenderingDevice::DRIVER_RESOURCE_COMMAND_QUEUE,
                    RID(), 0));
#else
            ApplyGodotGpuBarrier(rd);
            return true;
#endif
        case GodotGpuOp::Type::PublishNativeWrite:
            return true;
        case GodotGpuOp::Type::ImportApplePixelBuffer: {
#if defined(__APPLE__)
            if (op->native_image == nullptr || op->native_width == 0 ||
                op->native_height == 0) {
                return false;
            }
            const uint64_t device = rd->get_driver_resource(
                RenderingDevice::DRIVER_RESOURCE_LOGICAL_DEVICE, RID(), 0);
            void *vulkan_external_texture = nullptr;
            RID rid;
#if defined(IOS_ENABLED)
            const uint64_t native_texture =
                AetherAppleCreateMetalTextureFromPixelBuffer(
                    device, op->native_image, op->native_width,
                    op->native_height);
            if (native_texture == 0) return false;
            rid = rd->texture_create_from_extension(
                RenderingDevice::TEXTURE_TYPE_2D,
                RenderingDevice::DATA_FORMAT_B8G8R8A8_UNORM,
                RenderingDevice::TEXTURE_SAMPLES_1,
                BitField<RenderingDevice::TextureUsageBits>(
                    RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
                    RenderingDevice::TEXTURE_USAGE_STORAGE_BIT |
                    RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT),
                native_texture, op->native_width, op->native_height, 1, 1);
            if (!rid.is_valid()) {
                AetherAppleReleaseMetalTexture(native_texture);
                return false;
            }
#else
            const uint64_t physical_device = rd->get_driver_resource(
                RenderingDevice::DRIVER_RESOURCE_PHYSICAL_DEVICE, RID(), 0);
            const uint64_t command_queue = rd->get_driver_resource(
                RenderingDevice::DRIVER_RESOURCE_COMMAND_QUEUE, RID(), 0);
            const uint32_t queue_family = static_cast<uint32_t>(
                rd->get_driver_resource(
                    RenderingDevice::DRIVER_RESOURCE_QUEUE_FAMILY, RID(), 0));
            const uint64_t native_texture =
                AetherAppleCreateVulkanTextureFromPixelBuffer(
                    device, physical_device, command_queue, queue_family,
                    op->native_image,
                    op->native_width, op->native_height,
                    &vulkan_external_texture);
            if (native_texture == 0 ||
                vulkan_external_texture == nullptr) {
                return false;
            }
            rid = rd->texture_create_from_extension(
                RenderingDevice::TEXTURE_TYPE_2D,
                RenderingDevice::DATA_FORMAT_B8G8R8A8_UNORM,
                RenderingDevice::TEXTURE_SAMPLES_1,
                BitField<RenderingDevice::TextureUsageBits>(
                RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
                RenderingDevice::TEXTURE_USAGE_STORAGE_BIT |
                RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT |
                RenderingDevice::TEXTURE_USAGE_CAN_COPY_TO_BIT),
                native_texture, op->native_width, op->native_height, 1, 1);
            if (!rid.is_valid()) {
                AetherAppleReleaseVulkanTexture(vulkan_external_texture);
                return false;
            }
#endif

            GodotGpuTextureRecord record;
            record.rid = rid;
            record.width = op->native_width;
            record.height = op->native_height;
            record.apple_pixel_buffer = op->native_image;
            record.apple_vulkan_external_texture = vulkan_external_texture;
            AetherAppleRetainPixelBuffer(record.apple_pixel_buffer);
            record.texture.instantiate();
            record.texture->set_texture_rd_rid(rid);
            {
                std::lock_guard<std::mutex> lock(g_gpu_textures_mutex);
                op->imported_texture = g_next_gpu_texture_id++;
                g_gpu_textures[op->imported_texture] = record;
            }
            g_gpu_textures_created.fetch_add(1, std::memory_order_relaxed);
            g_gpu_texture_bytes_created.fetch_add(
                static_cast<uint64_t>(op->native_width) *
                    op->native_height * 4u,
                std::memory_order_relaxed);
            return true;
#else
            return false;
#endif
        }
        case GodotGpuOp::Type::ImportAndroidHardwareBuffer: {
#if defined(__ANDROID__)
            if (op->native_image == nullptr || op->native_width == 0 ||
                op->native_height == 0) {
                return false;
            }
            const uint64_t device = rd->get_driver_resource(
                RenderingDevice::DRIVER_RESOURCE_LOGICAL_DEVICE, RID(), 0);
            const uint64_t physical_device = rd->get_driver_resource(
                RenderingDevice::DRIVER_RESOURCE_PHYSICAL_DEVICE, RID(), 0);
            void *external_texture = nullptr;
            const uint64_t native_texture =
                AetherAndroidCreateVulkanTextureFromHardwareBuffer(
                    device, physical_device, op->native_image,
                    op->native_width, op->native_height, &external_texture);
            if (native_texture == 0 || external_texture == nullptr) {
                return false;
            }
            const RID rid = rd->texture_create_from_extension(
                RenderingDevice::TEXTURE_TYPE_2D,
                RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM,
                RenderingDevice::TEXTURE_SAMPLES_1,
                BitField<RenderingDevice::TextureUsageBits>(
                    RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
                    RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT),
                native_texture, op->native_width, op->native_height, 1, 1);
            if (!rid.is_valid()) {
                AetherAndroidReleaseVulkanTexture(external_texture);
                return false;
            }

            GodotGpuTextureRecord record;
            record.rid = rid;
            record.width = op->native_width;
            record.height = op->native_height;
            record.android_external_texture = external_texture;
            record.texture.instantiate();
            record.texture->set_texture_rd_rid(rid);
            {
                std::lock_guard<std::mutex> lock(g_gpu_textures_mutex);
                op->imported_texture = g_next_gpu_texture_id++;
                g_gpu_textures[op->imported_texture] = record;
            }
            g_gpu_textures_created.fetch_add(1, std::memory_order_relaxed);
            g_gpu_texture_bytes_created.fetch_add(
                static_cast<uint64_t>(op->native_width) *
                    op->native_height * 4u,
                std::memory_order_relaxed);
            return true;
#else
            return false;
#endif
        }
        case GodotGpuOp::Type::Release:
            InvalidateLive2DFramebuffer(rd, op->dst);
            InvalidateGodotGpuUniformSetsForResource(rd, op->dst);
#if defined(__APPLE__)
            if (op->dst.is_valid()) rd->free_rid(op->dst);
#if !defined(IOS_ENABLED)
            AetherAppleReleaseVulkanTexture(op->native_resource);
#endif
            AetherAppleReleasePixelBuffer(op->native_image);
#elif defined(__ANDROID__)
            AetherAndroidReleaseVulkanTexture(op->native_resource);
            rd->free_rid(op->dst);
#else
            rd->free_rid(op->dst);
#endif
            return true;
        case GodotGpuOp::Type::Flush:
            // The main RenderingDevice is submitted by RenderingServer at the
            // frame boundary. Calling RenderingDevice::submit() here is both
            // unnecessary and invalid for that main instance. The barrier is
            // enough to retain ordering inside Godot's command graph.
            ApplyGodotGpuBarrier(rd);
            return true;
    }
    if (result && wrote_texture) {
        ApplyGodotGpuBarrier(rd);
    }
    return result;
}

void FinishGodotGpuOp(const std::shared_ptr<GodotGpuOp> &op, bool result) {
    // PrepareNativeWrite is a non-blocking readiness probe. A false result
    // means Metal is still consuming the retired IOSurface, not that the GPU
    // bridge failed; the native producer keeps that target retired and retries.
    const bool expected_not_ready =
        !result && op != nullptr &&
        op->type == GodotGpuOp::Type::PrepareNativeWrite;
    CountGpuOpResult(result || expected_not_ready);
    if (!result && !expected_not_ready && op != nullptr) {
        if (op->type == GodotGpuOp::Type::Copy) {
            g_gpu_copy_failed.fetch_add(1, std::memory_order_relaxed);
        } else if (op->type == GodotGpuOp::Type::CopyTriangles) {
            g_gpu_copy_triangles_failed.fetch_add(
                1, std::memory_order_relaxed);
        }
    }
    {
        std::lock_guard<std::mutex> done_lock(op->done_mutex);
        op->result = result;
        op->done = true;
    }
    op->done_cv.notify_one();
}

void CompleteGodotGpuReadback(PackedByteArray data,
                              uint64_t completed_request) {
    std::shared_ptr<GodotGpuOp> completed_op;
    {
        std::lock_guard<std::mutex> lock(g_gpu_readbacks_mutex);
        const auto found = g_gpu_readbacks.find(completed_request);
        if(found == g_gpu_readbacks.end()) return;
        completed_op = found->second.op;
    }
    completed_op->data = std::move(data);
    FinishGodotGpuOp(completed_op, !completed_op->data.is_empty());
}

bool BeginGodotGpuReadbackOnRenderThread(
    RenderingDevice *rd, const std::shared_ptr<GodotGpuOp> &op) {
    if(rd == nullptr || op == nullptr ||
       op->type != GodotGpuOp::Type::ReadAsync ||
       op->readback_request == 0) {
        return false;
    }
    return rd->texture_get_data_async(
               op->src, 0,
               callable_mp_static(&CompleteGodotGpuReadback)
                   .bind(op->readback_request)) == OK;
}

bool IsBatchableTriangleOp(const std::shared_ptr<GodotGpuOp> &op) {
    if (op == nullptr || op->src == op->dst) return false;
    switch (op->type) {
        case GodotGpuOp::Type::CopyTriangles:
        case GodotGpuOp::Type::DrawTriangles:
            return true;
        case GodotGpuOp::Type::DrawMaskedTriangles:
            return op->src2 != op->dst;
        default:
            return false;
    }
}

bool IsLive2DTriangleOp(const std::shared_ptr<GodotGpuOp> &op) {
    if (!IsBatchableTriangleOp(op)) return false;
    if (op->type == GodotGpuOp::Type::DrawMaskedTriangles) return true;
    return op->type == GodotGpuOp::Type::DrawTriangles &&
           (op->color & TVP_GODOT_GPU_BLEND_TVP_OPERATION) == 0;
}

GodotGpuBarrierShadowPlanner::Step RecordGodotGpuNonLiveShadowAccess(
    GodotGpuBarrierShadowPlanner &planner, const GodotGpuOp &op) {
    const int64_t src = op.src.get_id();
    const int64_t src2 = op.src2.get_id();
    const int64_t src3 = op.src3.get_id();
    const int64_t dst = op.dst.get_id();
    switch (op.type) {
        case GodotGpuOp::Type::CopyTriangles:
        case GodotGpuOp::Type::DrawTriangles:
            return planner.record({src, dst}, {dst});
        case GodotGpuOp::Type::DrawMaskedTriangles:
            return planner.record({src, src2, dst}, {dst});
        case GodotGpuOp::Type::Blend:
            if (op.mode == TVP_GODOT_GPU_BLEND_FILL_ARGB) {
                return planner.record({}, {dst});
            }
            return planner.record({src, dst}, {dst});
        case GodotGpuOp::Type::Blend2:
            return planner.record({src, src2, dst}, {dst});
        case GodotGpuOp::Type::Blend3:
            return planner.record({src, src2, src3, dst}, {dst});
        default:
            return {};
    }
}

void PublishGodotGpuBarrierShadowCounters(
    const GodotGpuBarrierShadowPlanner::Counters &counters) {
    g_gpu_predicted_compute_barriers.fetch_add(
        counters.barriers, std::memory_order_relaxed);
    g_gpu_predicted_raw_hazards.fetch_add(counters.raw,
                                          std::memory_order_relaxed);
    g_gpu_predicted_waw_hazards.fetch_add(counters.waw,
                                          std::memory_order_relaxed);
    g_gpu_predicted_war_hazards.fetch_add(counters.war,
                                          std::memory_order_relaxed);
}

bool TriangleOpNeedsBarrierBeforeDispatch(
    const GodotGpuOp &op,
    const std::vector<GodotGpuPendingWrite> &writes) {
    if (writes.empty()) return false;
    const GodotGpuPendingWrite dst_rect =
        PendingWriteForRect(op.dst, op.dst_pos, op.size);
    for (const auto &write : writes) {
        if (PendingWritesOverlap(write, dst_rect)) return true;
    }
    return false;
}

void ExecuteGodotGpuTriangleBatch(
    RenderingDevice *rd,
    const std::vector<std::shared_ptr<GodotGpuOp>> &ops) {
    if (ops.empty()) return;
    if (rd == nullptr) {
        for (const auto &op : ops) FinishGodotGpuOp(op, false);
        return;
    }

    std::vector<GodotGpuPreparedTriangles> prepared(ops.size());
    std::vector<bool> results(ops.size(), false);
    std::vector<size_t> float_offsets(ops.size(), 0);
    std::vector<float> combined_vertices;
    for (size_t i = 0; i < ops.size(); ++i) {
        while ((combined_vertices.size() & 3u) != 0u) {
            combined_vertices.push_back(0.0f);
        }
        float_offsets[i] = combined_vertices.size();
        combined_vertices.insert(combined_vertices.end(),
                                 ops[i]->vertices.begin(),
                                 ops[i]->vertices.end());
    }
    PackedByteArray vertex_data;
    vertex_data.resize(static_cast<int64_t>(combined_vertices.size() *
                                            sizeof(float)));
    if (uint8_t *bytes = vertex_data.ptrw()) {
        std::memcpy(bytes, combined_vertices.data(),
                    combined_vertices.size() * sizeof(float));
    }
    RID shared_vertex_buffer;
    if (!UpdateGodotGpuTriangleVertexBuffer(rd, vertex_data,
                                            shared_vertex_buffer)) {
        for (const auto &op : ops) FinishGodotGpuOp(op, false);
        return;
    }

    bool any_prepared = false;
    for (size_t i = 0; i < ops.size(); ++i) {
        const bool draw = ops[i]->type != GodotGpuOp::Type::CopyTriangles;
        const bool masked =
            ops[i]->type == GodotGpuOp::Type::DrawMaskedTriangles;
        const int32_t vertex_offset = static_cast<int32_t>(
            masked ? float_offsets[i] : float_offsets[i] / 4u);
        results[i] = PrepareGodotGpuTriangles(
            rd, ops[i], draw, masked, prepared[i], shared_vertex_buffer,
            vertex_offset, true);
        any_prepared = any_prepared || results[i];
    }

    if (any_prepared) {
        const int64_t compute_list = rd->compute_list_begin();
        for (size_t i = 0; i < ops.size(); ++i) {
            if (!results[i]) continue;
            DispatchGodotGpuPreparedTriangles(rd, compute_list, prepared[i]);
            // E-mote layers frequently reuse the preceding destination as a
            // later source. Keep native draw order while amortizing the much
            // more expensive compute-list begin/end submission on Metal.
            rd->compute_list_add_barrier(compute_list);
        }
        rd->compute_list_end();
        ApplyGodotGpuBarrier(rd);
    }

    for (size_t i = 0; i < ops.size(); ++i) {
        FreeGodotGpuPreparedTriangles(rd, prepared[i]);
        FinishGodotGpuOp(ops[i], results[i]);
    }
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
    std::vector<bool> results(ops.size(), false);
    std::vector<GodotGpuPendingWrite> pending_writes;
    bool any_dispatched = false;
    const bool hazard_tracked_barriers = HazardTrackedBlendBarriersEnabled();
    int64_t compute_list = rd->compute_list_begin();
    for (size_t i = 0; i < ops.size(); ++i) {
        const auto &op = ops[i];
        if (hazard_tracked_barriers &&
            BlendOpNeedsBarrierBeforeDispatch(*op, pending_writes)) {
            rd->compute_list_add_barrier(compute_list);
            pending_writes.clear();
        }
        if (op->type == GodotGpuOp::Type::Blend) {
            results[i] = DispatchGodotGpuBlend(rd, op, compute_list, uniform_sets);
        } else if (op->type == GodotGpuOp::Type::Blend2) {
            results[i] = DispatchGodotGpuBlend2(rd, op, compute_list, uniform_sets);
        } else if(op->type == GodotGpuOp::Type::Blend3) {
            results[i] = DispatchGodotGpuBlend3(rd, op, compute_list,
                                                uniform_sets);
        }
        if (results[i]) {
            any_dispatched = true;
            if (hazard_tracked_barriers) {
                pending_writes.push_back(
                    PendingWriteForRect(op->dst, op->dst_pos, op->size));
            } else {
                rd->compute_list_add_barrier(compute_list);
            }
        }
    }
    if (hazard_tracked_barriers && any_dispatched) {
        rd->compute_list_add_barrier(compute_list);
    }
    rd->compute_list_end();
    if (any_dispatched) {
        ApplyGodotGpuBarrier(rd);
    }
    for (const RID &uniform_set : uniform_sets) {
        rd->free_rid(uniform_set);
    }
    for (size_t i = 0; i < ops.size(); ++i) {
        FinishGodotGpuOp(ops[i], results[i]);
    }
}

void ExecuteGodotGpuComputeBatchLegacy(
    RenderingDevice *rd,
    const std::vector<std::shared_ptr<GodotGpuOp>> &ops) {
    if (ops.empty()) return;
    g_gpu_compute_batches.fetch_add(1, std::memory_order_relaxed);
    g_gpu_compute_batch_ops.fetch_add(
        static_cast<uint64_t>(ops.size()), std::memory_order_relaxed);
    if (rd == nullptr) {
        for (const auto &op : ops) FinishGodotGpuOp(op, false);
        return;
    }

    std::vector<GodotGpuPreparedTriangles> prepared(ops.size());
    std::vector<bool> results(ops.size(), false);
    std::vector<size_t> float_offsets(ops.size(), 0);
    std::vector<float> combined_vertices;
    bool has_triangles = false;
    for (size_t i = 0; i < ops.size(); ++i) {
        if (!IsBatchableTriangleOp(ops[i])) continue;
        has_triangles = true;
        while ((combined_vertices.size() & 3u) != 0u) {
            combined_vertices.push_back(0.0f);
        }
        float_offsets[i] = combined_vertices.size();
        combined_vertices.insert(combined_vertices.end(),
                                 ops[i]->vertices.begin(),
                                 ops[i]->vertices.end());
    }

    RID shared_vertex_buffer;
    bool vertices_ready = !has_triangles;
    if (has_triangles) {
        PackedByteArray vertex_data;
        vertex_data.resize(static_cast<int64_t>(combined_vertices.size() *
                                                sizeof(float)));
        if (uint8_t *bytes = vertex_data.ptrw()) {
            std::memcpy(bytes, combined_vertices.data(),
                        combined_vertices.size() * sizeof(float));
        }
        vertices_ready = UpdateGodotGpuTriangleVertexBuffer(
            rd, vertex_data, shared_vertex_buffer);
    }

    if (vertices_ready) {
        for (size_t i = 0; i < ops.size(); ++i) {
            if (!IsBatchableTriangleOp(ops[i])) continue;
            const bool draw =
                ops[i]->type != GodotGpuOp::Type::CopyTriangles;
            const bool masked =
                ops[i]->type == GodotGpuOp::Type::DrawMaskedTriangles;
            const int32_t vertex_offset = static_cast<int32_t>(
                masked ? float_offsets[i] : float_offsets[i] / 4u);
            results[i] = PrepareGodotGpuTriangles(
                rd, ops[i], draw, masked, prepared[i], shared_vertex_buffer,
                vertex_offset, true);
        }
    }

    std::vector<RID> unused_uniform_sets;
    bool any_dispatched = false;
    uint64_t nonlive_compute_ops = 0;
    uint64_t nonlive_compute_barriers = 0;
    std::vector<GodotGpuPendingWrite> live2d_pending_writes;
    std::vector<GodotGpuPendingWrite> nonlive_pending_writes;
    const bool nonlive_hazard_tracking_safe =
        NonLiveGpuHazardTrackingSafe(ops);
    const bool shadow_enabled = GodotGpuBarrierShadowEnabled();
    std::unique_ptr<GodotGpuBarrierShadowPlanner> nonlive_shadow;
    if (shadow_enabled) {
        try {
            nonlive_shadow =
                std::make_unique<GodotGpuBarrierShadowPlanner>();
        } catch (...) {
            // Optional diagnostics must never affect command submission.
        }
    }
    const int64_t compute_list = rd->compute_list_begin();
    for (size_t i = 0; i < ops.size(); ++i) {
        const bool live2d_triangle = IsLive2DTriangleOp(ops[i]);
        if (live2d_triangle && nonlive_shadow != nullptr) {
            // Live2D has its own rectangle-aware ordering below. End the
            // diagnostic whole-RID non-Live2D epoch without changing the
            // real command list.
            nonlive_shadow->finish();
        }
        if (live2d_triangle && !nonlive_pending_writes.empty()) {
            rd->compute_list_add_barrier(compute_list);
            nonlive_pending_writes.clear();
        }
        if (live2d_triangle && TriangleOpNeedsBarrierBeforeDispatch(
                                   *ops[i], live2d_pending_writes)) {
            rd->compute_list_add_barrier(compute_list);
            live2d_pending_writes.clear();
        } else if (!live2d_triangle && !live2d_pending_writes.empty()) {
            rd->compute_list_add_barrier(compute_list);
            live2d_pending_writes.clear();
        }
        if (!live2d_triangle &&
            (!nonlive_hazard_tracking_safe ||
             BlendOpNeedsBarrierBeforeDispatch(*ops[i],
                                                nonlive_pending_writes))) {
            rd->compute_list_add_barrier(compute_list);
            nonlive_pending_writes.clear();
        }
        const bool batchable_triangle = IsBatchableTriangleOp(ops[i]);
        if (batchable_triangle) {
            if (results[i]) {
                DispatchGodotGpuPreparedTriangles(rd, compute_list,
                                                  prepared[i]);
            }
        } else if (ops[i]->type == GodotGpuOp::Type::Blend) {
            results[i] = DispatchGodotGpuBlend(
                rd, ops[i], compute_list, unused_uniform_sets);
        } else if (ops[i]->type == GodotGpuOp::Type::Blend2) {
            results[i] = DispatchGodotGpuBlend2(
                rd, ops[i], compute_list, unused_uniform_sets);
        } else if (ops[i]->type == GodotGpuOp::Type::Blend3) {
            results[i] = DispatchGodotGpuBlend3(
                rd, ops[i], compute_list, unused_uniform_sets);
        }
        if (results[i]) {
            any_dispatched = true;
            if (live2d_triangle) {
                // Cubism drawables use explicit, non-aliased textures.  Only
                // overlapping destination rectangles need read-after-write
                // ordering because the shader samples the existing target.
                // This removes hundreds of redundant Metal barriers for
                // disjoint ArtMeshes while preserving drawable order where
                // pixels can actually overlap.
                live2d_pending_writes.push_back(PendingWriteForRect(
                    ops[i]->dst, ops[i]->dst_pos, ops[i]->size));
            } else {
                ++nonlive_compute_ops;
                nonlive_pending_writes.push_back(PendingWriteForRect(
                    ops[i]->dst, ops[i]->dst_pos, ops[i]->size));
                if (!nonlive_hazard_tracking_safe) {
                    // Imported textures may alias storage outside this batch;
                    // preserve the historical barrier after each operation.
                    rd->compute_list_add_barrier(compute_list);
                    nonlive_pending_writes.clear();
                    ++nonlive_compute_barriers;
                }
                if (nonlive_shadow != nullptr) {
                    try {
                        RecordGodotGpuNonLiveShadowAccess(
                            *nonlive_shadow, *ops[i]);
                    } catch (...) {
                        // Shadow accounting must never alter rendering. If
                        // its bookkeeping cannot allocate, discard the batch
                        // sample and keep submitting commands unchanged.
                        nonlive_shadow.reset();
                    }
                }
            }
        }
    }
    if (nonlive_shadow != nullptr) {
        nonlive_shadow->finish();
        if (shadow_enabled) {
            PublishGodotGpuBarrierShadowCounters(
                nonlive_shadow->counters());
        }
    }
    if (nonlive_compute_barriers != 0) {
        g_gpu_nonlive_compute_barriers.fetch_add(
            nonlive_compute_barriers, std::memory_order_relaxed);
    }
    if (nonlive_compute_ops != 0) {
        g_gpu_nonlive_compute_ops.fetch_add(
            nonlive_compute_ops, std::memory_order_relaxed);
    }
    if (!live2d_pending_writes.empty()) {
        rd->compute_list_add_barrier(compute_list);
    }
    if (!nonlive_pending_writes.empty()) {
        rd->compute_list_add_barrier(compute_list);
    }
    rd->compute_list_end();
    if (any_dispatched) ApplyGodotGpuBarrier(rd);

    for (size_t i = 0; i < ops.size(); ++i) {
        FreeGodotGpuPreparedTriangles(rd, prepared[i]);
        FinishGodotGpuOp(ops[i], results[i]);
    }
}

void ExecuteGodotGpuComputeBatch(
    RenderingDevice *rd,
    const std::vector<std::shared_ptr<GodotGpuOp>> &ops) {
    if (ops.empty()) return;
    if (!Live2DHardwareRasterEnabled()) {
        ExecuteGodotGpuComputeBatchLegacy(rd, ops);
        return;
    }

    std::vector<std::shared_ptr<GodotGpuOp>> segment;
    bool segment_is_raster = false;
    const auto flush_segment = [&]() {
        if (segment.empty()) return;
        if (segment_is_raster) {
            ExecuteGodotGpuLive2DRasterBatch(rd, segment);
        } else {
            ExecuteGodotGpuComputeBatchLegacy(rd, segment);
        }
        segment.clear();
    };
    for (const auto &op : ops) {
        const bool raster = IsLive2DHardwareRasterOp(op);
        if (!segment.empty() && raster != segment_is_raster) {
            flush_segment();
        }
        if (segment.empty()) segment_is_raster = raster;
        segment.push_back(op);
    }
    flush_segment();
}

void DrainGodotGpuOpsOnRenderThreadImpl(bool force_batch_drain,
                                        uint64_t sequence_cutoff = 0) {
    RenderingDevice *rd = MainRenderingDevice();
    std::vector<std::shared_ptr<GodotGpuOp>> compute_batch;
    const auto flush_compute = [&]() {
        ExecuteGodotGpuComputeBatch(rd, compute_batch);
        compute_batch.clear();
    };
    for (;;) {
        std::shared_ptr<GodotGpuOp> op;
        {
            std::lock_guard<std::mutex> lock(g_gpu_op_queue_mutex);
            if (!force_batch_drain && g_gpu_batch_token != 0) {
                // A callback may already have been posted when the producer
                // opens a batch. Leave its ordered operations untouched until
                // the matching end callback releases the gate.
                g_gpu_op_drain_scheduled = false;
                break;
            }
            if (g_gpu_op_queue.empty()) {
                if (!force_batch_drain) g_gpu_op_drain_scheduled = false;
                break;
            }
            if (force_batch_drain && sequence_cutoff != 0 &&
                g_gpu_op_queue.front()->queue_sequence > sequence_cutoff) {
                // A bounded split/end callback may run after another producer
                // has already opened its next batch. Never consume operations
                // beyond the sequence captured by the callback that owns this
                // drain.
                break;
            }
            op = g_gpu_op_queue.front();
            g_gpu_op_queue.pop_front();
        }

        if (IsBatchableBlendOp(op)) {
            compute_batch.push_back(op);
            continue;
        }
        if (IsBatchableTriangleOp(op)) {
            compute_batch.push_back(op);
            continue;
        }

        flush_compute();

        if(op->type == GodotGpuOp::Type::ReadAsync) {
            // The readback belongs to the same command stream as the clear,
            // triangle and blend operations that produced this frame. Submit
            // it only after those operations have been encoded. Starting it
            // directly from BridgeBeginReadRgba races Metal and can capture a
            // transparent or partially composited E-mote surface.
            if(!BeginGodotGpuReadbackOnRenderThread(rd, op)) {
                FinishGodotGpuOp(op, false);
            }
            continue;
        }

        // Alias blends are executed separately because sampling and writing the
        // same storage image in one dispatch is undefined on Metal/Vulkan.
        FinishGodotGpuOp(op, ExecuteGodotGpuOp(rd, op));
    }
    flush_compute();
}

void DrainGodotGpuOpsOnRenderThread() {
    DrainGodotGpuOpsOnRenderThreadImpl(false);
}

void ForceDrainGodotGpuOpsOnRenderThread(uint64_t sequence_cutoff) {
    DrainGodotGpuOpsOnRenderThreadImpl(true, sequence_cutoff);
}

bool RunGodotGpuOp(const std::shared_ptr<GodotGpuOp> &op, bool wait) {
    GodotGpuOpWaitProfile wait_profile(op, wait);
    RenderingServer *server = RenderingServer::get_singleton();
    RenderingDevice *rd = MainRenderingDevice();
    if (op == nullptr) return false;
    if (op->profile_submitted_at.time_since_epoch().count() == 0) {
        op->profile_submitted_at = std::chrono::steady_clock::now();
    }
    g_gpu_op_submitted.fetch_add(1, std::memory_order_relaxed);
    if (op->type == GodotGpuOp::Type::Blend ||
        op->type == GodotGpuOp::Type::Blend2 ||
        op->type == GodotGpuOp::Type::Blend3) {
        g_gpu_blend_op_submitted.fetch_add(1, std::memory_order_relaxed);
    }
    if (server == nullptr || rd == nullptr) {
        CountGpuOpResult(false);
        return false;
    }
    if (server->is_on_render_thread()) {
        bool explicit_batch_active = false;
        uint64_t force_sequence_cutoff = 0;
        {
            std::lock_guard<std::mutex> lock(g_gpu_op_queue_mutex);
            explicit_batch_active = g_gpu_batch_token != 0;
            if (explicit_batch_active) {
                EnqueueGodotGpuOpLocked(op);
                force_sequence_cutoff = op->queue_sequence;
                g_gpu_batch_ops.fetch_add(1, std::memory_order_relaxed);
            }
        }
        if (explicit_batch_active) {
            if (!wait) return true;
            // Synchronous reads and shader requests remain legal inside a
            // producer batch. Force a split so the caller cannot deadlock on
            // the queue gate. The sequence cutoff prevents a delayed split
            // from consuming a later producer batch.
            ForceDrainGodotGpuOpsOnRenderThread(force_sequence_cutoff);
            return op->result;
        }
        if (DeferredGodotGpuDrainEnabled() ||
            op->type == GodotGpuOp::Type::ReadAsync) {
            {
                std::lock_guard<std::mutex> lock(g_gpu_op_queue_mutex);
                EnqueueGodotGpuOpLocked(op);
            }
            if (!wait && op->type != GodotGpuOp::Type::Flush &&
                op->type != GodotGpuOp::Type::ReadAsync) {
                return true;
            }
            DrainGodotGpuOpsOnRenderThread();
            return wait ? op->result : true;
        }
        const bool result = ExecuteGodotGpuOp(rd, op);
        FinishGodotGpuOp(op, result);
        return result;
    }

    bool should_schedule = false;
    bool should_force_schedule = false;
    uint64_t force_sequence_cutoff = 0;
    {
        std::lock_guard<std::mutex> lock(g_gpu_op_queue_mutex);
        EnqueueGodotGpuOpLocked(op);
        force_sequence_cutoff = op->queue_sequence;
        const bool explicit_batch_active = g_gpu_batch_token != 0;
        if (explicit_batch_active) {
            g_gpu_batch_ops.fetch_add(1, std::memory_order_relaxed);
        }
        if (explicit_batch_active && wait) {
            // Do not rely on an earlier normal callback: it may observe the
            // active gate and return before this synchronous request arrives.
            should_force_schedule = true;
        } else if (!explicit_batch_active &&
                   ShouldScheduleGodotGpuDrainNow(op, wait) &&
            !g_gpu_op_drain_scheduled) {
            g_gpu_op_drain_scheduled = true;
            should_schedule = true;
        }
    }
    if (should_force_schedule) {
        server->call_on_render_thread(
            callable_mp_static(&ForceDrainGodotGpuOpsOnRenderThread)
                .bind(force_sequence_cutoff));
    } else if (should_schedule) {
        server->call_on_render_thread(
            callable_mp_static(&DrainGodotGpuOpsOnRenderThread));
    }

    if (!wait) {
        return true;
    }
    std::unique_lock<std::mutex> done_lock(op->done_mutex);
    const auto wait_timeout =
        op->type == GodotGpuOp::Type::ArtemisShader
            ? std::chrono::seconds(10)
            : kGodotGpuSyncWaitTimeout;
    if (!op->done_cv.wait_for(done_lock, wait_timeout,
                              [&]() { return op->done; })) {
        g_gpu_sync_timeouts.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return op->result;
}

bool RunGodotGpuOpAsync(const std::shared_ptr<GodotGpuOp> &op) {
    return RunGodotGpuOp(op, false);
}

bool RunGodotGpuOpSync(const std::shared_ptr<GodotGpuOp> &op) {
    return RunGodotGpuOp(op, true);
}

uint64_t BridgeBeginBatch() {
    RenderingServer *server = RenderingServer::get_singleton();
    if (server == nullptr || MainRenderingDevice() == nullptr) return 0;

    const auto caller = std::this_thread::get_id();
    std::lock_guard<std::mutex> lock(g_gpu_op_queue_mutex);
    if (g_gpu_batch_token != 0) {
        if (g_gpu_batch_owner != caller) {
            g_gpu_batch_rejected.fetch_add(1, std::memory_order_relaxed);
            return 0;
        }
        ++g_gpu_batch_depth;
        return g_gpu_batch_token;
    }

    uint64_t token = g_next_gpu_batch_token++;
    if (token == 0) token = g_next_gpu_batch_token++;
    g_gpu_batch_token = token;
    g_gpu_batch_depth = 1;
    g_gpu_batch_owner = caller;
    g_gpu_batch_started.fetch_add(1, std::memory_order_relaxed);
    return token;
}

bool BridgeEndBatch(uint64_t batch_token) {
    RenderingServer *server = RenderingServer::get_singleton();
    const bool on_render_thread =
        server != nullptr && server->is_on_render_thread();
    bool has_queued_ops = false;
    bool should_schedule = false;
    uint64_t batch_sequence_cutoff = 0;
    {
        std::lock_guard<std::mutex> lock(g_gpu_op_queue_mutex);
        if (batch_token == 0 || batch_token != g_gpu_batch_token ||
            g_gpu_batch_owner != std::this_thread::get_id() ||
            g_gpu_batch_depth == 0) {
            g_gpu_batch_rejected.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        if (g_gpu_batch_depth > 1) {
            --g_gpu_batch_depth;
            return true;
        }

        g_gpu_batch_token = 0;
        g_gpu_batch_depth = 0;
        g_gpu_batch_owner = {};
        g_gpu_batch_ended.fetch_add(1, std::memory_order_relaxed);
        has_queued_ops = !g_gpu_op_queue.empty();
        batch_sequence_cutoff = g_last_gpu_op_sequence;
        if (has_queued_ops && !on_render_thread && server != nullptr &&
            !g_gpu_op_drain_scheduled) {
            g_gpu_op_drain_scheduled = true;
            should_schedule = true;
        }
    }

    if (!has_queued_ops) return true;
    if (server == nullptr) return false;
    if (on_render_thread) {
        ForceDrainGodotGpuOpsOnRenderThread(batch_sequence_cutoff);
    } else if (should_schedule) {
        server->call_on_render_thread(
            callable_mp_static(&DrainGodotGpuOpsOnRenderThread));
    }
    return true;
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

std::string ReplaceShaderMatches(
    const std::string &input, const std::regex &pattern,
    const std::function<std::string(const std::smatch &)> &replacement) {
    std::string output;
    std::string::const_iterator cursor = input.begin();
    std::smatch match;
    while (std::regex_search(cursor, input.end(), match, pattern)) {
        output.append(cursor, match[0].first);
        output += replacement(match);
        cursor = match[0].second;
    }
    output.append(cursor, input.end());
    return output;
}

std::string ArtemisShaderFloat(float value) {
    if (!std::isfinite(value)) value = 0.0f;
    std::ostringstream output;
    output << std::setprecision(9) << value;
    std::string text = output.str();
    if (text.find_first_of(".eE") == std::string::npos) text += ".0";
    return text;
}

const ArtemisGpuShaderConstant *FindArtemisShaderConstant(
    const ArtemisGpuShaderRequest &request, const std::string &name) {
    for (const auto &constant : request.constants) {
        if (constant.name == name) return &constant;
    }
    return nullptr;
}

std::vector<float> ArtemisShaderUniformValues(
    const ArtemisGpuShaderRequest &request, const std::string &name) {
    if (name == "alpha") return {request.alpha};
    if (name == "colorMultiply") {
        return {
            static_cast<float>((request.color_multiply >> 16u) & 0xffu) /
                255.0f,
            static_cast<float>((request.color_multiply >> 8u) & 0xffu) /
                255.0f,
            static_cast<float>(request.color_multiply & 0xffu) / 255.0f,
        };
    }
    // These are only consumed by Artemis' built-in transition shader. A
    // custom layer outside an active transition observes OpenGL's initial
    // uniform value of zero.
    if (name == "maskTransitionVague" || name == "maskTransitionStep") {
        return {0.0f};
    }
    const ArtemisGpuShaderConstant *constant =
        FindArtemisShaderConstant(request, name);
    return constant != nullptr ? constant->values : std::vector<float>{};
}

uint32_t ArtemisShaderComponentCount(const std::string &type) {
    if (type == "vec2" || type == "ivec2" || type == "bvec2") return 2;
    if (type == "vec3" || type == "ivec3" || type == "bvec3") return 3;
    if (type == "vec4" || type == "ivec4" || type == "bvec4") return 4;
    if (type == "mat2") return 4;
    if (type == "mat3") return 9;
    if (type == "mat4") return 16;
    return 1;
}

std::string ArtemisShaderTypedValue(const std::string &type,
                                    const std::vector<float> &source_values,
                                    size_t source_offset = 0) {
    const uint32_t components = ArtemisShaderComponentCount(type);
    std::vector<std::string> values;
    values.reserve(components);
    for (uint32_t index = 0; index < components; ++index) {
        const float value =
            source_offset + index < source_values.size()
                ? source_values[source_offset + index]
                : 0.0f;
        if (type == "int" || type.rfind("ivec", 0) == 0) {
            values.push_back(std::to_string(static_cast<int32_t>(value)));
        } else if (type == "bool" || type.rfind("bvec", 0) == 0) {
            values.push_back(value != 0.0f ? "true" : "false");
        } else {
            values.push_back(ArtemisShaderFloat(value));
        }
    }
    if (components == 1) return values.front();
    std::string output = type + "(";
    for (size_t index = 0; index < values.size(); ++index) {
        if (index != 0) output += ", ";
        output += values[index];
    }
    output += ")";
    return output;
}

struct ArtemisTranslatedShader {
    struct Uniform {
        std::string type;
        std::string name;
        uint32_t array_count = 0;
        uint32_t binding = 0;
        std::vector<float> values;
    };

    std::string vertex_source;
    std::string fragment_source;
    std::vector<std::string> samplers;
    std::vector<Uniform> uniforms;
};

ArtemisTranslatedShader TranslateArtemisFragmentShader(
    const ArtemisGpuShaderRequest &request) {
    ArtemisTranslatedShader translated;
    translated.vertex_source = R"GLSL(#version 450
layout(location = 0) out vec2 resultCoord0;
layout(location = 1) out vec2 resultCoord1;
void main() {
    vec2 position;
    if (gl_VertexIndex == 0) {
        position = vec2(-1.0, -1.0);
    } else if (gl_VertexIndex == 1) {
        position = vec2(3.0, -1.0);
    } else {
        position = vec2(-1.0, 3.0);
    }
    gl_Position = vec4(position, 0.0, 1.0);
    resultCoord0 = position * 0.5 + vec2(0.5);
    resultCoord1 = resultCoord0;
}
)GLSL";

    std::string source = request.fragment_source;
    source = std::regex_replace(
        source, std::regex(R"(^[ \t]*#[ \t]*version[^\r\n]*(?:\r?\n|$))",
                           std::regex::icase | std::regex::multiline),
        "");
    source = std::regex_replace(
        source,
        std::regex(R"(^[ \t]*#[ \t]*extension[^\r\n]*(?:\r?\n|$))",
                   std::regex::icase | std::regex::multiline),
        "");
    source = std::regex_replace(
        source,
        std::regex(
            R"(\bprecision\s+(?:lowp|mediump|highp)\s+\w+\s*;)",
            std::regex::icase),
        "");
    source = std::regex_replace(
        source,
        std::regex(
            R"(\bvarying\s+(?:(?:lowp|mediump|highp)\s+)?[^;]+;)",
            std::regex::icase),
        "");
    source = std::regex_replace(
        source,
        std::regex(
            R"(\battribute\s+(?:(?:lowp|mediump|highp)\s+)?[^;]+;)",
            std::regex::icase),
        "");

    uint32_t next_binding = 0;
    const std::regex sampler_pattern(
        R"(\buniform\s+(?:(?:lowp|mediump|highp)\s+)?sampler2D\s+([A-Za-z_][A-Za-z0-9_]*)\s*;)",
        std::regex::icase);
    source = ReplaceShaderMatches(
        source, sampler_pattern, [&](const std::smatch &match) {
            const std::string name = match[1].str();
            translated.samplers.push_back(name);
            return "layout(set = 0, binding = " +
                std::to_string(next_binding++) +
                ") uniform sampler2D " + name + ";";
        });

    const std::regex uniform_pattern(
        R"(\buniform\s+(?:(?:lowp|mediump|highp)\s+)?(float|vec2|vec3|vec4|int|ivec2|ivec3|ivec4|bool|bvec2|bvec3|bvec4|mat2|mat3|mat4)\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s*\[\s*([0-9]+)\s*\])?\s*;)",
        std::regex::icase);
    source = ReplaceShaderMatches(
        source, uniform_pattern, [&](const std::smatch &match) {
            const std::string type = match[1].str();
            const std::string name = match[2].str();
            const uint32_t array_count =
                match[3].matched
                    ? static_cast<uint32_t>(
                          std::max(1, std::stoi(match[3].str())))
                    : 0u;
            ArtemisTranslatedShader::Uniform uniform;
            uniform.type = type;
            uniform.name = name;
            uniform.array_count = array_count;
            uniform.binding = next_binding++;
            uniform.values = ArtemisShaderUniformValues(request, name);
            translated.uniforms.push_back(std::move(uniform));
            return "layout(std140, set = 0, binding = " +
                std::to_string(translated.uniforms.back().binding) +
                ") uniform ArtemisUniform" +
                std::to_string(translated.uniforms.size() - 1u) + " { " +
                type + " " + name +
                (array_count != 0
                     ? "[" + std::to_string(array_count) + "]"
                     : "") +
                "; };";
        });

    source = std::regex_replace(source, std::regex(R"(\btexture2D\s*\()"),
                                "texture(");
    source = std::regex_replace(
        source, std::regex(R"(\bgl_FragData\s*\[\s*0\s*\])"),
        "artemisFragmentColor");
    source = std::regex_replace(source, std::regex(R"(\bgl_FragColor\b)"),
                                "artemisFragmentColor");

    translated.fragment_source =
        "#version 450\n"
        "layout(location = 0) in vec2 resultCoord0;\n"
        "layout(location = 1) in vec2 resultCoord1;\n"
        "layout(location = 0) out vec4 artemisFragmentColor;\n" +
        source;
    return translated;
}

const ArtemisGpuShaderImage *FindArtemisShaderImage(
    const ArtemisGpuShaderRequest &request, const std::string &name) {
    if (name == "textureFore") return &request.foreground;
    if (name == "textureMask") return &request.mask;
    for (const auto &texture : request.textures) {
        if (texture.name == name) return &texture.image;
    }
    return nullptr;
}

PackedByteArray PackArtemisShaderImage(
    const ArtemisGpuShaderImage &image) {
    PackedByteArray packed;
    packed.resize(static_cast<int64_t>(image.pixels.size()));
    if (!image.pixels.empty() && packed.ptrw() != nullptr) {
        std::memcpy(packed.ptrw(), image.pixels.data(), image.pixels.size());
    }
    return packed;
}

PackedByteArray PackArtemisShaderUniform(
    const ArtemisTranslatedShader::Uniform &uniform) {
    const uint32_t components = ArtemisShaderComponentCount(uniform.type);
    uint32_t element_size = 16;
    if (uniform.type == "mat2") element_size = 32;
    if (uniform.type == "mat3") element_size = 48;
    if (uniform.type == "mat4") element_size = 64;
    const uint32_t element_count =
        uniform.array_count != 0 ? uniform.array_count : 1u;
    PackedByteArray packed;
    packed.resize(static_cast<int64_t>(element_size) * element_count);
    uint8_t *bytes = packed.ptrw();
    if (bytes == nullptr) return packed;
    std::memset(bytes, 0, static_cast<size_t>(packed.size()));

    const bool integer =
        uniform.type == "int" || uniform.type.rfind("ivec", 0) == 0;
    const bool boolean =
        uniform.type == "bool" || uniform.type.rfind("bvec", 0) == 0;
    const bool matrix = uniform.type.rfind("mat", 0) == 0;
    for (uint32_t element = 0; element < element_count; ++element) {
        const size_t source_base =
            static_cast<size_t>(element) * components;
        for (uint32_t component = 0; component < components; ++component) {
            const float value =
                source_base + component < uniform.values.size()
                    ? uniform.values[source_base + component]
                    : 0.0f;
            size_t byte_offset =
                static_cast<size_t>(element) * element_size;
            if (matrix) {
                const uint32_t rows =
                    uniform.type == "mat2"
                        ? 2u
                        : (uniform.type == "mat3" ? 3u : 4u);
                const uint32_t column = component / rows;
                const uint32_t row = component % rows;
                byte_offset += static_cast<size_t>(column) * 16u +
                    static_cast<size_t>(row) * 4u;
            } else {
                byte_offset += static_cast<size_t>(component) * 4u;
            }
            if (integer || boolean) {
                const int32_t encoded =
                    boolean ? (value != 0.0f ? 1 : 0)
                            : static_cast<int32_t>(value);
                std::memcpy(bytes + byte_offset, &encoded, sizeof(encoded));
            } else {
                const float encoded = std::isfinite(value) ? value : 0.0f;
                std::memcpy(bytes + byte_offset, &encoded, sizeof(encoded));
            }
        }
    }
    return packed;
}

bool ExecuteArtemisGpuShader(
    RenderingDevice *rd, const std::shared_ptr<GodotGpuOp> &op) {
    if (rd == nullptr || op == nullptr || op->artemis_shader == nullptr) {
        return false;
    }
    ArtemisGpuShaderRequest &request = *op->artemis_shader;
    if (request.fragment_source.empty() || request.foreground.width == 0 ||
        request.foreground.height == 0) {
        request.error = "invalid Artemis fragment shader request";
        return false;
    }

    const ArtemisTranslatedShader translated =
        TranslateArtemisFragmentShader(request);

    std::vector<RID> owned_rids;
    const auto own = [&](RID rid) {
        if (rid.is_valid()) owned_rids.push_back(rid);
        return rid;
    };
    const auto cleanup = [&]() {
        for (auto it = owned_rids.rbegin(); it != owned_rids.rend(); ++it) {
            rd->free_rid(*it);
        }
    };

    Ref<RDTextureView> texture_view;
    texture_view.instantiate();
    TypedArray<PackedByteArray> no_data;
    RID output_texture = own(rd->texture_create(
        MakeRgbaTextureFormat(request.foreground.width,
                              request.foreground.height),
        texture_view, no_data));
    if (!output_texture.is_valid()) {
        request.error = "Godot failed to allocate Artemis shader output";
        cleanup();
        return false;
    }
    TypedArray<RID> framebuffer_textures;
    framebuffer_textures.push_back(output_texture);
    RID framebuffer = own(rd->framebuffer_create(framebuffer_textures));
    if (!framebuffer.is_valid()) {
        request.error = "Godot failed to create Artemis shader framebuffer";
        cleanup();
        return false;
    }

    RID shader;
    RID pipeline;
    const std::string cache_key = request.fragment_source;
    const auto cached = g_artemis_shader_pipeline_cache.find(cache_key);
    if (cached != g_artemis_shader_pipeline_cache.end() &&
        cached->second.shader.is_valid() &&
        cached->second.pipeline.is_valid()) {
        shader = cached->second.shader;
        pipeline = cached->second.pipeline;
    } else {
        Ref<RDShaderSource> shader_source;
        shader_source.instantiate();
        shader_source->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);
        shader_source->set_stage_source(
            RenderingDevice::SHADER_STAGE_VERTEX,
            String::utf8(translated.vertex_source.c_str()));
        shader_source->set_stage_source(
            RenderingDevice::SHADER_STAGE_FRAGMENT,
            String::utf8(translated.fragment_source.c_str()));
        Ref<RDShaderSPIRV> spirv =
            rd->shader_compile_spirv_from_source(shader_source);
        if (spirv.is_null()) {
            request.error = "Godot failed to compile Artemis shader source";
            cleanup();
            return false;
        }
        const String vertex_error = spirv->get_stage_compile_error(
            RenderingDevice::SHADER_STAGE_VERTEX);
        const String fragment_error = spirv->get_stage_compile_error(
            RenderingDevice::SHADER_STAGE_FRAGMENT);
        if (!vertex_error.is_empty() || !fragment_error.is_empty()) {
            request.error =
                std::string(vertex_error.utf8().get_data()) +
                std::string(fragment_error.utf8().get_data());
            UtilityFunctions::printerr(
                "Artemis fragment shader compile error [",
                String::utf8(request.shader_id.c_str()), "]: ",
                String::utf8(request.error.c_str()));
            cleanup();
            return false;
        }
        shader = rd->shader_create_from_spirv(
            spirv,
            String("Artemis/") + String::utf8(request.shader_id.c_str()));
        if (!shader.is_valid()) {
            request.error = "Godot failed to create Artemis fragment shader";
            cleanup();
            return false;
        }

        Ref<RDPipelineRasterizationState> rasterization;
        rasterization.instantiate();
        rasterization->set_cull_mode(RenderingDevice::POLYGON_CULL_DISABLED);
        Ref<RDPipelineMultisampleState> multisample;
        multisample.instantiate();
        Ref<RDPipelineDepthStencilState> depth_stencil;
        depth_stencil.instantiate();
        Ref<RDPipelineColorBlendStateAttachment> attachment;
        attachment.instantiate();
        attachment->set_enable_blend(false);
        attachment->set_write_r(true);
        attachment->set_write_g(true);
        attachment->set_write_b(true);
        attachment->set_write_a(true);
        TypedArray<Ref<RDPipelineColorBlendStateAttachment>> attachments;
        attachments.push_back(attachment);
        Ref<RDPipelineColorBlendState> color_blend;
        color_blend.instantiate();
        color_blend->set_attachments(attachments);

        pipeline = rd->render_pipeline_create(
            shader, rd->framebuffer_get_format(framebuffer), -1,
            RenderingDevice::RENDER_PRIMITIVE_TRIANGLES, rasterization,
            multisample, depth_stencil, color_blend);
        if (!pipeline.is_valid()) {
            request.error = "Godot failed to create Artemis shader pipeline";
            rd->free_rid(shader);
            cleanup();
            return false;
        }
        g_artemis_shader_pipeline_cache[cache_key] = {shader, pipeline};
    }

    RID sampler;
    TypedArray<RDUniform> uniforms;
    if (!translated.samplers.empty()) {
        Ref<RDSamplerState> sampler_state;
        sampler_state.instantiate();
        sampler_state->set_mag_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
        sampler_state->set_min_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
        sampler_state->set_mip_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
        sampler_state->set_repeat_u(
            RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
        sampler_state->set_repeat_v(
            RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
        sampler_state->set_repeat_w(
            RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
        sampler = own(rd->sampler_create(sampler_state));
        if (!sampler.is_valid()) {
            request.error = "Godot failed to create Artemis shader sampler";
            cleanup();
            return false;
        }
    }

    const ArtemisGpuShaderImage missing_image{
        1, 1, std::vector<uint8_t>{0, 0, 0, 255}};
    for (size_t index = 0; index < translated.samplers.size(); ++index) {
        const ArtemisGpuShaderImage *image =
            FindArtemisShaderImage(request, translated.samplers[index]);
        if (image == nullptr || image->width == 0 || image->height == 0 ||
            image->pixels.size() !=
                static_cast<size_t>(image->width) * image->height * 4u) {
            image = &missing_image;
        }
        TypedArray<PackedByteArray> initial_data;
        initial_data.push_back(PackArtemisShaderImage(*image));
        RID texture = own(rd->texture_create(
            MakeRgbaTextureFormat(image->width, image->height), texture_view,
            initial_data));
        if (!texture.is_valid()) {
            request.error = "Godot failed to upload Artemis shader texture " +
                translated.samplers[index];
            cleanup();
            return false;
        }
        Ref<RDUniform> uniform;
        uniform.instantiate();
        uniform->set_uniform_type(
            RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
        uniform->set_binding(static_cast<int32_t>(index));
        uniform->add_id(sampler);
        uniform->add_id(texture);
        uniforms.push_back(uniform);
    }
    for (const auto &shader_uniform : translated.uniforms) {
        PackedByteArray data = PackArtemisShaderUniform(shader_uniform);
        RID buffer = own(rd->uniform_buffer_create(
            static_cast<uint32_t>(data.size()), data));
        if (!buffer.is_valid()) {
            request.error =
                "Godot failed to upload Artemis shader uniform " +
                shader_uniform.name;
            cleanup();
            return false;
        }
        Ref<RDUniform> uniform;
        uniform.instantiate();
        uniform->set_uniform_type(
            RenderingDevice::UNIFORM_TYPE_UNIFORM_BUFFER);
        uniform->set_binding(static_cast<int32_t>(shader_uniform.binding));
        uniform->add_id(buffer);
        uniforms.push_back(uniform);
    }

    RID uniform_set;
    if (!translated.samplers.empty() || !translated.uniforms.empty()) {
        uniform_set = own(rd->uniform_set_create(uniforms, shader, 0));
        if (!uniform_set.is_valid()) {
            request.error =
                "Godot failed to bind Artemis shader texture uniforms";
            cleanup();
            return false;
        }
    }

    const int64_t draw_list = rd->draw_list_begin(framebuffer);
    rd->draw_list_bind_render_pipeline(draw_list, pipeline);
    if (uniform_set.is_valid()) {
        rd->draw_list_bind_uniform_set(draw_list, uniform_set, 0);
    }
    rd->draw_list_draw(draw_list, false, 1, 3);
    rd->draw_list_end();
    ApplyGodotGpuBarrier(rd);
    op->data = rd->texture_get_data(output_texture, 0);
    const size_t expected_size =
        static_cast<size_t>(request.foreground.width) *
        request.foreground.height * 4u;
    const bool success =
        static_cast<size_t>(op->data.size()) == expected_size;
    if (!success) {
        request.error = "Godot returned an incomplete Artemis shader image";
    }
    cleanup();
    return success;
}

void WriteArtemisShaderError(const std::string &message, char *error_utf8,
                             uint32_t error_size) {
    if (error_utf8 == nullptr || error_size == 0) return;
    const size_t copy_size =
        std::min(message.size(), static_cast<size_t>(error_size - 1u));
    if (copy_size != 0) {
        std::memcpy(error_utf8, message.data(), copy_size);
    }
    error_utf8[copy_size] = '\0';
}

bool CopyArtemisShaderImage(const engine_runtime_shader_image_v1_t &source,
                            bool allow_empty, ArtemisGpuShaderImage *output,
                            std::string *error) {
    if (output == nullptr) return false;
    output->width = source.width;
    output->height = source.height;
    output->pixels.clear();
    if (source.width == 0 || source.height == 0) {
        if (allow_empty && source.width == 0 && source.height == 0) return true;
        if (error != nullptr) *error = "Artemis shader image is empty";
        return false;
    }
    const uint64_t tight_stride = static_cast<uint64_t>(source.width) * 4u;
    const uint64_t stride =
        source.stride_bytes != 0 ? source.stride_bytes : tight_stride;
    const uint64_t required =
        (static_cast<uint64_t>(source.height) - 1u) * stride + tight_stride;
    const uint64_t tight_size =
        tight_stride * static_cast<uint64_t>(source.height);
    if (source.pixels_rgba == nullptr || stride < tight_stride ||
        required > source.pixels_size ||
        tight_size > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        if (error != nullptr) {
            *error = "Artemis shader image buffer is invalid";
        }
        return false;
    }
    output->pixels.resize(static_cast<size_t>(tight_size));
    for (uint32_t y = 0; y < source.height; ++y) {
        std::memcpy(output->pixels.data() +
                        static_cast<size_t>(y) *
                            static_cast<size_t>(tight_stride),
                    source.pixels_rgba +
                        static_cast<size_t>(y) *
                            static_cast<size_t>(stride),
                    static_cast<size_t>(tight_stride));
    }
    return true;
}

engine_result_t ExecuteArtemisFragmentShader(
    void *, const engine_runtime_fragment_shader_request_v1_t *native_request,
    char *error_utf8, uint32_t error_size) {
    if (native_request == nullptr ||
        native_request->struct_size <
            offsetof(engine_runtime_fragment_shader_request_v1_t,
                     output_pixels_size) +
                sizeof(native_request->output_pixels_size) ||
        native_request->api_version !=
            ENGINE_RUNTIME_FRAGMENT_SHADER_API_VERSION ||
        native_request->fragment_source_utf8 == nullptr ||
        native_request->output_pixels_rgba == nullptr) {
        WriteArtemisShaderError("invalid Artemis shader ABI request",
                                error_utf8, error_size);
        return ENGINE_RESULT_INVALID_ARGUMENT;
    }
    if (!SupportsGodotRenderingDeviceGpu() || MainRenderingDevice() == nullptr) {
        WriteArtemisShaderError(
            "Godot RenderingDevice shader backend is unavailable", error_utf8,
            error_size);
        return ENGINE_RESULT_NOT_SUPPORTED;
    }

    auto request = std::make_shared<ArtemisGpuShaderRequest>();
    request->shader_id = native_request->shader_id_utf8 != nullptr
        ? native_request->shader_id_utf8
        : "";
    request->fragment_source = native_request->fragment_source_utf8;
    request->mask_uses_alpha = native_request->mask_uses_alpha != 0;
    request->alpha = native_request->alpha;
    request->color_multiply = native_request->color_multiply;
    if (!CopyArtemisShaderImage(native_request->foreground, false,
                                &request->foreground, &request->error) ||
        !CopyArtemisShaderImage(native_request->mask, true, &request->mask,
                                &request->error)) {
        WriteArtemisShaderError(request->error, error_utf8, error_size);
        return ENGINE_RESULT_INVALID_ARGUMENT;
    }
    const size_t expected_output_size =
        static_cast<size_t>(request->foreground.width) *
        request->foreground.height * 4u;
    if (native_request->output_pixels_size < expected_output_size) {
        WriteArtemisShaderError("Artemis shader output buffer is too small",
                                error_utf8, error_size);
        return ENGINE_RESULT_INVALID_ARGUMENT;
    }
    if (native_request->texture_count != 0 &&
        native_request->textures == nullptr) {
        WriteArtemisShaderError("Artemis shader texture array is missing",
                                error_utf8, error_size);
        return ENGINE_RESULT_INVALID_ARGUMENT;
    }
    if (native_request->constant_count != 0 &&
        native_request->constants == nullptr) {
        WriteArtemisShaderError("Artemis shader constant array is missing",
                                error_utf8, error_size);
        return ENGINE_RESULT_INVALID_ARGUMENT;
    }

    request->textures.reserve(native_request->texture_count);
    for (uint32_t index = 0; index < native_request->texture_count; ++index) {
        const auto &source = native_request->textures[index];
        ArtemisGpuShaderTexture texture;
        texture.name = source.name_utf8 != nullptr ? source.name_utf8 : "";
        if (texture.name.empty() ||
            !CopyArtemisShaderImage(source.image, false, &texture.image,
                                    &request->error)) {
            if (request->error.empty()) {
                request->error = "Artemis shader texture name is empty";
            }
            WriteArtemisShaderError(request->error, error_utf8, error_size);
            return ENGINE_RESULT_INVALID_ARGUMENT;
        }
        request->textures.push_back(std::move(texture));
    }
    request->constants.reserve(native_request->constant_count);
    for (uint32_t index = 0; index < native_request->constant_count; ++index) {
        const auto &source = native_request->constants[index];
        if (source.name_utf8 == nullptr ||
            (source.value_count != 0 && source.values == nullptr)) {
            WriteArtemisShaderError(
                "Artemis shader constant entry is invalid", error_utf8,
                error_size);
            return ENGINE_RESULT_INVALID_ARGUMENT;
        }
        ArtemisGpuShaderConstant constant;
        constant.name = source.name_utf8;
        if (source.value_count != 0) {
            constant.values.assign(source.values,
                                   source.values + source.value_count);
        }
        request->constants.push_back(std::move(constant));
    }

    auto op = std::make_shared<GodotGpuOp>();
    op->type = GodotGpuOp::Type::ArtemisShader;
    op->artemis_shader = request;
    if (!RunGodotGpuOpSync(op)) {
        const std::string message = request->error.empty()
            ? "Godot failed to execute Artemis fragment shader"
            : request->error;
        WriteArtemisShaderError(message, error_utf8, error_size);
        return ENGINE_RESULT_INTERNAL_ERROR;
    }
    if (static_cast<size_t>(op->data.size()) != expected_output_size) {
        WriteArtemisShaderError(
            "Godot returned an invalid Artemis shader output size", error_utf8,
            error_size);
        return ENGINE_RESULT_INTERNAL_ERROR;
    }
    std::memcpy(native_request->output_pixels_rgba, op->data.ptr(),
                expected_output_size);
    WriteArtemisShaderError("", error_utf8, error_size);
    return ENGINE_RESULT_OK;
}

uint64_t BridgeCreateRgba(uint32_t width, uint32_t height, const void *pixels,
                          uint32_t stride_bytes) {
    RenderingDevice *rd = MainRenderingDevice();
    if (rd == nullptr || !SupportsGodotRenderingDeviceGpu() ||
        width == 0 || height == 0) {
        return 0;
    }

    const auto create_started = std::chrono::steady_clock::now();
    Ref<RDTextureView> view;
    view.instantiate();
    TypedArray<PackedByteArray> initial_data;
    RenderingServer *server = RenderingServer::get_singleton();
    const bool on_render_thread =
        server != nullptr && server->is_on_render_thread();
    const bool use_device_clear = pixels == nullptr && on_render_thread;
    if (!use_device_clear) {
        initial_data.push_back(
            PackRgbaBytes(pixels, width, height, stride_bytes));
    }
    RID rid = rd->texture_create(MakeRgbaTextureFormat(width, height), view,
                                 initial_data);
    // RenderingDevice accepts an empty initial-data array, but its contents
    // are undefined. Fresh KiriKiri render targets require transparent black,
    // so establish the same state with a device-side clear. This avoids
    // allocating, zeroing and uploading a width*height*4 staging array for
    // large PSD/transition scratch textures.
    if (rid.is_valid() && use_device_clear &&
        rd->texture_clear(rid, Color(0.0, 0.0, 0.0, 0.0), 0, 1, 0, 1) != OK) {
        rd->free_rid(rid);
        rid = RID();
    }
    const double create_ms = std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() -
                                 create_started)
                                 .count();
    LogGodotGpuUpdateProfile(
        "texture_create", width, height,
        pixels != nullptr
            ? static_cast<size_t>(width) * static_cast<size_t>(height) * 4u
            : 0u,
        create_ms, 0.0, 0.0, rid.is_valid(),
        on_render_thread);
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
    g_gpu_textures_created.fetch_add(1, std::memory_order_relaxed);
    g_gpu_texture_bytes_created.fetch_add(
        static_cast<uint64_t>(width) * height * 4u,
        std::memory_order_relaxed);
    return id;
}

uint64_t BridgeImportApplePixelBuffer(void *native_image, uint32_t width,
                                      uint32_t height) {
#if defined(__APPLE__)
    if (native_image == nullptr || width == 0 || height == 0 ||
        !SupportsGodotRenderingDeviceGpu()) {
        return 0;
    }
    auto op = std::make_shared<GodotGpuOp>();
    op->type = GodotGpuOp::Type::ImportApplePixelBuffer;
    op->native_image = native_image;
    op->native_width = width;
    op->native_height = height;
    return RunGodotGpuOpSync(op) ? op->imported_texture : 0;
#else
    (void)native_image;
    (void)width;
    (void)height;
    return 0;
#endif
}

bool BridgePrepareForNativeWrite(uint64_t texture) {
    GodotGpuTextureRecord record;
    {
        std::lock_guard<std::mutex> lock(g_gpu_textures_mutex);
        const auto found = g_gpu_textures.find(texture);
        if (found == g_gpu_textures.end()) return false;
        record = found->second;
    }
    if (!record.rid.is_valid()) return false;
#if defined(__APPLE__)
    // All native surfaces rendered during one Godot display frame were consumed
    // by the same ordered Metal queue. One completed marker therefore makes
    // every sufficiently retired surface in this scene batch reusable.
    Engine *engine = Engine::get_singleton();
    const uint64_t drawn_frame = engine != nullptr
        ? static_cast<uint64_t>(engine->get_frames_drawn())
        : UINT64_MAX;
    static std::mutex wait_mutex;
    static uint64_t last_waited_drawn_frame = UINT64_MAX;
    std::lock_guard<std::mutex> wait_lock(wait_mutex);
    if (drawn_frame != UINT64_MAX &&
        drawn_frame == last_waited_drawn_frame) {
        return true;
    }
#endif
    auto op = std::make_shared<GodotGpuOp>();
    op->type = GodotGpuOp::Type::PrepareNativeWrite;
    op->src = record.rid;
    const bool prepared = RunGodotGpuOpSync(op);
#if defined(__APPLE__)
    if (prepared && drawn_frame != UINT64_MAX) {
        last_waited_drawn_frame = drawn_frame;
    }
#endif
    return prepared;
}

bool BridgePublishNativeWrite(uint64_t texture) {
#if defined(__APPLE__) && !defined(IOS_ENABLED)
    GodotGpuTextureRecord record;
    {
        std::lock_guard<std::mutex> lock(g_gpu_textures_mutex);
        const auto found = g_gpu_textures.find(texture);
        if (found == g_gpu_textures.end()) return false;
        record = found->second;
    }
    // The VkImage directly imports the producer's IOSurface. The OpenGL fence
    // and PrepareForExport() flush make this generation visible; no second
    // Metal copy or out-of-band write to a MoltenVK-owned texture is needed.
    return record.rid.is_valid() &&
           record.apple_vulkan_external_texture != nullptr;
#else
    (void)texture;
    return true;
#endif
}

uint64_t BridgeImportAndroidHardwareBuffer(void *native_image,
                                           uint32_t width,
                                           uint32_t height) {
#if defined(__ANDROID__)
    if (native_image == nullptr || width == 0 || height == 0 ||
        !SupportsGodotRenderingDeviceGpu()) {
        return 0;
    }
    auto op = std::make_shared<GodotGpuOp>();
    op->type = GodotGpuOp::Type::ImportAndroidHardwareBuffer;
    op->native_image = native_image;
    op->native_width = width;
    op->native_height = height;
    return RunGodotGpuOpSync(op) ? op->imported_texture : 0;
#else
    (void)native_image;
    (void)width;
    (void)height;
    return 0;
#endif
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
    g_gpu_textures_released.fetch_add(1, std::memory_order_relaxed);
    g_gpu_texture_bytes_released.fetch_add(
        static_cast<uint64_t>(record.width) * record.height * 4u,
        std::memory_order_relaxed);
    record.texture.unref();
    if (record.rid.is_valid()) {
        auto op = std::make_shared<GodotGpuOp>();
        op->type = GodotGpuOp::Type::Release;
        op->dst = record.rid;
        op->native_image = record.apple_pixel_buffer != nullptr
            ? record.apple_pixel_buffer
            : nullptr;
        op->native_resource = record.apple_vulkan_external_texture != nullptr
            ? record.apple_vulkan_external_texture
            : record.android_external_texture;
        // Texture operations are consumed in queue order. Waiting here turns
        // every short-lived E-mote scratch layer into a render-thread round
        // trip; enqueue the release after its last use instead.
        RunGodotGpuOpAsync(op);
    }
}

bool BridgeUpdateRgba(uint64_t texture, const void *pixels,
                      uint32_t stride_bytes, const tTVPRect *rect) {
    if (pixels == nullptr) return false;
    GodotGpuTextureRecord record;
    {
        std::lock_guard<std::mutex> lock(g_gpu_textures_mutex);
        auto it = g_gpu_textures.find(texture);
        if (it == g_gpu_textures.end()) return false;
        record = it->second;
    }
    if (rect == nullptr || rect->left != 0 || rect->top != 0 ||
        rect->right != static_cast<int>(record.width) ||
        rect->bottom != static_cast<int>(record.height)) {
        return false;
    }
    const auto bridge_started = std::chrono::steady_clock::now();
    PackedByteArray data =
        PackRgbaBytes(pixels, record.width, record.height, stride_bytes);
    const auto packed_at = std::chrono::steady_clock::now();
    const double pack_ms = std::chrono::duration<double, std::milli>(
                               packed_at - bridge_started)
                               .count();

    // Updating a texture that Godot presented recently can make MoltenVK wait
    // for the old VkImage's fence. PSD slice uploads are full replacements, so
    // large surfaces can use a fresh RID and retire the old RID in normal GPU
    // queue order. This preserves the exact uploaded pixels while avoiding a
    // 20-35 ms texture_update stall on the render thread.
    constexpr uint64_t kVersionedUploadMinPixels = 256u * 1024u;
    const uint64_t pixel_count =
        static_cast<uint64_t>(record.width) * record.height;
    RenderingServer *server = RenderingServer::get_singleton();
    const bool on_render_thread =
        server != nullptr && server->is_on_render_thread();
    const bool can_version_upload =
        on_render_thread &&
        pixel_count >= kVersionedUploadMinPixels &&
        record.apple_pixel_buffer == nullptr &&
        record.apple_vulkan_external_texture == nullptr &&
        record.android_external_texture == nullptr;
    if (can_version_upload) {
        RenderingDevice *rd = MainRenderingDevice();
        Ref<RDTextureView> view;
        view.instantiate();
        TypedArray<PackedByteArray> initial_data;
        initial_data.push_back(data);
        RID replacement = rd != nullptr
            ? rd->texture_create(
                  MakeRgbaTextureFormat(record.width, record.height), view,
                  initial_data)
            : RID();
        if (replacement.is_valid()) {
            bool replaced = false;
            {
                std::lock_guard<std::mutex> lock(g_gpu_textures_mutex);
                auto it = g_gpu_textures.find(texture);
                if (it != g_gpu_textures.end() &&
                    it->second.rid == record.rid &&
                    it->second.apple_pixel_buffer == nullptr &&
                    it->second.apple_vulkan_external_texture == nullptr &&
                    it->second.android_external_texture == nullptr) {
                    it->second.rid = replacement;
                    it->second.requires_alpha_d_clear_version = false;
                    it->second.texture->set_texture_rd_rid(replacement);
                    replaced = true;
                }
            }
            if (replaced) {
                auto release = std::make_shared<GodotGpuOp>();
                release->type = GodotGpuOp::Type::Release;
                release->dst = record.rid;
                const bool result = RunGodotGpuOpAsync(release);
                const double bridge_ms =
                    std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - bridge_started)
                        .count();
                LogGodotGpuUpdateProfile(
                    "texture_replace_rgba", record.width, record.height,
                    static_cast<size_t>(data.size()), bridge_ms, pack_ms, 0.0,
                    result,
                    on_render_thread);
                return result;
            }
            rd->free_rid(replacement);
        }
    }

    auto op = std::make_shared<GodotGpuOp>();
    op->type = GodotGpuOp::Type::Update;
    op->dst = record.rid;
    op->data = data;
    op->profile_width = record.width;
    op->profile_height = record.height;
    op->profile_pack_ms = pack_ms;
    op->profile_enqueued_at = packed_at;
    const bool result = RunGodotGpuOpAsync(op);
    const double bridge_ms = std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() -
                                 bridge_started)
                                 .count();
    LogGodotGpuUpdateProfile(
        "bridge_update_rgba", record.width, record.height,
        static_cast<size_t>(data.size()), bridge_ms, pack_ms, 0.0, result,
        on_render_thread);
    return result;
}

bool BridgeClearRgba(uint64_t texture, uint32_t argb, const tTVPRect *rect) {
    GodotGpuTextureRecord record;
    {
        std::lock_guard<std::mutex> lock(g_gpu_textures_mutex);
        auto it = g_gpu_textures.find(texture);
        if (it == g_gpu_textures.end()) return false;
        record = it->second;
    }
    if (rect == nullptr) {
        return false;
    }
    if (rect->left < 0 || rect->top < 0 ||
        rect->right > static_cast<int>(record.width) ||
        rect->bottom > static_cast<int>(record.height) ||
        rect->right <= rect->left || rect->bottom <= rect->top) {
        return false;
    }
    const bool full_transparent_clear = argb == 0 && rect->left == 0 &&
        rect->top == 0 && rect->right == static_cast<int>(record.width) &&
        rect->bottom == static_cast<int>(record.height);
    const bool can_version_clear =
        RenderingServer::get_singleton() != nullptr &&
        RenderingServer::get_singleton()->is_on_render_thread() &&
        full_transparent_clear &&
        record.requires_alpha_d_clear_version &&
        record.apple_pixel_buffer == nullptr &&
        record.apple_vulkan_external_texture == nullptr &&
        record.android_external_texture == nullptr;
    if (can_version_clear) {
        RenderingDevice *rd = MainRenderingDevice();
        Ref<RDTextureView> view;
        view.instantiate();
        TypedArray<PackedByteArray> initial_data;
        RID replacement = rd != nullptr
            ? rd->texture_create(MakeRgbaTextureFormat(record.width,
                                                       record.height),
                                 view, initial_data)
            : RID();
        if (replacement.is_valid() &&
            rd->texture_clear(replacement, Color(0.0, 0.0, 0.0, 0.0),
                              0, 1, 0, 1) != OK) {
            rd->free_rid(replacement);
            replacement = RID();
        }
        if (replacement.is_valid()) {
            bool replaced = false;
            {
                std::lock_guard<std::mutex> lock(g_gpu_textures_mutex);
                auto it = g_gpu_textures.find(texture);
                if (it != g_gpu_textures.end() &&
                    it->second.rid == record.rid &&
                    it->second.apple_pixel_buffer == nullptr &&
                    it->second.apple_vulkan_external_texture == nullptr &&
                    it->second.android_external_texture == nullptr) {
                    it->second.rid = replacement;
                    it->second.requires_alpha_d_clear_version = false;
                    it->second.texture->set_texture_rd_rid(replacement);
                    replaced = true;
                }
            }
            if (replaced) {
                g_gpu_alpha_d_clear_versions.fetch_add(
                    1, std::memory_order_relaxed);
                auto release = std::make_shared<GodotGpuOp>();
                release->type = GodotGpuOp::Type::Release;
                release->dst = record.rid;
                return RunGodotGpuOpAsync(release);
            }
            rd->free_rid(replacement);
        }
    }
    auto op = std::make_shared<GodotGpuOp>();
    // Keep full clears in the same compute list as the E-mote draws that
    // immediately follow them. RenderingDevice::texture_clear forced the
    // pending compute batch to end, producing dozens of Metal encoders per
    // animated frame. The fill shader writes the same RGBA value and already
    // participates in normal read/write ordering.
    op->type = GodotGpuOp::Type::Blend;
    op->src = record.rid;
    op->dst = record.rid;
    op->dst_pos = Vector3(rect->left, rect->top, 0);
    op->src_pos = op->dst_pos;
    op->size = Vector3(rect->right - rect->left,
                       rect->bottom - rect->top, 1);
    op->mode = TVP_GODOT_GPU_BLEND_FILL_ARGB;
    op->opacity = 255;
    op->color = argb;
    return RunGodotGpuOpAsync(op);
}

bool BridgeCopyRect(uint64_t dst, uint64_t src, const tTVPRect *dst_rect,
                    const tTVPRect *src_rect) {
    if (dst_rect == nullptr || src_rect == nullptr) return false;
    GodotGpuTextureRecord dst_record;
    GodotGpuTextureRecord src_record;
    {
        std::lock_guard<std::mutex> lock(g_gpu_textures_mutex);
        auto dst_it = g_gpu_textures.find(dst);
        auto src_it = g_gpu_textures.find(src);
        if (dst_it == g_gpu_textures.end() || src_it == g_gpu_textures.end()) {
            return false;
        }
        dst_record = dst_it->second;
        src_record = src_it->second;
    }
    const int width = dst_rect->right - dst_rect->left;
    const int height = dst_rect->bottom - dst_rect->top;
    if (width <= 0 || height <= 0 ||
        width != src_rect->right - src_rect->left ||
        height != src_rect->bottom - src_rect->top) {
        return false;
    }
    auto op = std::make_shared<GodotGpuOp>();
    // A non-aliasing copy can run through the same compute list as the E-mote
    // clears, affine draws, blends and masks surrounding it. texture_copy
    // otherwise terminates that batch for every child layer, creating hundreds
    // of Metal submissions per frame. The shader path uses integer imageLoad /
    // imageStore conversion and copies all RGBA channels exactly.
    op->type =
        dst == src ? GodotGpuOp::Type::CopySelf : GodotGpuOp::Type::Blend;
    op->src = src_record.rid;
    op->dst = dst_record.rid;
    op->src_pos = Vector3(src_rect->left, src_rect->top, 0);
    op->dst_pos = Vector3(dst_rect->left, dst_rect->top, 0);
    op->size = Vector3(width, height, 1);
    op->mode = TVP_GODOT_GPU_BLEND_COPY_RGBA;
    op->opacity = 255;
    return RunGodotGpuOpAsync(op);
}

void AppendGodotGpuTriangleBounds(std::vector<float> &vertices,
                                  const tTVPPointD *points) {
    const float min_x = static_cast<float>(
        std::min({points[0].x, points[1].x, points[2].x}) - 0.25);
    const float min_y = static_cast<float>(
        std::min({points[0].y, points[1].y, points[2].y}) - 0.25);
    const float max_x = static_cast<float>(
        std::max({points[0].x, points[1].x, points[2].x}) + 0.25);
    const float max_y = static_cast<float>(
        std::max({points[0].y, points[1].y, points[2].y}) + 0.25);
    vertices.push_back(min_x);
    vertices.push_back(min_y);
    vertices.push_back(max_x);
    vertices.push_back(max_y);
}

bool ShouldPreserveMinifiedTriangleDetail(uint32_t triangle_count,
                                          const tTVPPointD *dst_points,
                                          const tTVPPointD *src_points,
                                          bool force_detail_sampling = false) {
    const bool detail_sampling_enabled =
        force_detail_sampling ||
        g_frame_enhancement_detail_sampling.load(std::memory_order_acquire);
    if (!detail_sampling_enabled || triangle_count == 0 ||
        dst_points == nullptr || src_points == nullptr) {
        return false;
    }

    // Ignore tiny fractional transforms. The detail path is reserved for a
    // real reduction, such as a full-resolution character image composited
    // into a dialogue portrait, so normal 1:1 layers retain the fast path.
    constexpr double kMinifiedScale = 0.9;
    constexpr double kLengthEpsilon = 1.0e-6;
    for (uint32_t triangle = 0; triangle < triangle_count; ++triangle) {
        const uint32_t base = triangle * 3u;
        for (uint32_t edge = 0; edge < 3u; ++edge) {
            const uint32_t next = (edge + 1u) % 3u;
            const double src_dx = src_points[base + next].x -
                                  src_points[base + edge].x;
            const double src_dy = src_points[base + next].y -
                                  src_points[base + edge].y;
            const double dst_dx = dst_points[base + next].x -
                                  dst_points[base + edge].x;
            const double dst_dy = dst_points[base + next].y -
                                  dst_points[base + edge].y;
            const double src_length_squared =
                src_dx * src_dx + src_dy * src_dy;
            if (src_length_squared <= kLengthEpsilon * kLengthEpsilon) {
                continue;
            }
            const double dst_length_squared =
                dst_dx * dst_dx + dst_dy * dst_dy;
            if (dst_length_squared <
                kMinifiedScale * kMinifiedScale * src_length_squared) {
                return true;
            }
        }
    }
    return false;
}

bool BridgeCopyTriangles(uint64_t dst, uint64_t src, uint32_t triangle_count,
                         const tTVPRect *clip_rect,
                         const tTVPPointD *dst_points,
                         const tTVPPointD *src_points) {
    if (clip_rect == nullptr || dst_points == nullptr || src_points == nullptr ||
        triangle_count == 0) {
        return false;
    }
    GodotGpuTextureRecord dst_record;
    GodotGpuTextureRecord src_record;
    {
        std::lock_guard<std::mutex> lock(g_gpu_textures_mutex);
        auto dst_it = g_gpu_textures.find(dst);
        auto src_it = g_gpu_textures.find(src);
        if (dst_it == g_gpu_textures.end() || src_it == g_gpu_textures.end()) {
            return false;
        }
        dst_record = dst_it->second;
        src_record = src_it->second;
    }
    const int width = clip_rect->right - clip_rect->left;
    const int height = clip_rect->bottom - clip_rect->top;
    if (width <= 0 || height <= 0 || triangle_count > 64) {
        return false;
    }
    auto op = std::make_shared<GodotGpuOp>();
    op->type = GodotGpuOp::Type::CopyTriangles;
    op->src = src_record.rid;
    op->dst = dst_record.rid;
    op->dst_pos = Vector3(clip_rect->left, clip_rect->top, 0);
    op->src_pos = Vector3(dst_record.width, dst_record.height, 0);
    op->size = Vector3(width, height, 1);
    op->src_size = Vector3(src_record.width, src_record.height, 1);
    op->mode = triangle_count;
    // Copy triangles do not have blend flags. Keep the reserved detail bit
    // clear unless the enhancement-specific minification path selects it.
    op->color = 0u;
    op->preserve_minified_detail = ShouldPreserveMinifiedTriangleDetail(
        triangle_count, dst_points, src_points);
    if (op->preserve_minified_detail) {
        g_gpu_detail_minify_ops.fetch_add(1, std::memory_order_relaxed);
    }
    op->vertices.reserve(static_cast<size_t>(triangle_count) * 16u);
    for (uint32_t triangle = 0; triangle < triangle_count; ++triangle) {
        const uint32_t vertex_base = triangle * 3u;
        for (uint32_t vertex = 0; vertex < 3u; ++vertex) {
            const uint32_t i = vertex_base + vertex;
            op->vertices.push_back(static_cast<float>(dst_points[i].x));
            op->vertices.push_back(static_cast<float>(dst_points[i].y));
            op->vertices.push_back(static_cast<float>(src_points[i].x));
            op->vertices.push_back(static_cast<float>(src_points[i].y));
        }
        AppendGodotGpuTriangleBounds(op->vertices, dst_points + vertex_base);
    }
    return RunGodotGpuOpAsync(op);
}

bool BridgeDrawTriangles(uint64_t dst, uint64_t src, uint32_t triangle_count,
                         const tTVPRect *clip_rect,
                         const tTVPPointD *dst_points,
                         const tTVPPointD *src_points, float opacity,
                         uint32_t blend_mode) {
    if (clip_rect == nullptr || dst_points == nullptr || src_points == nullptr ||
        triangle_count == 0) {
        return false;
    }
    GodotGpuTextureRecord dst_record;
    GodotGpuTextureRecord src_record;
    {
        std::lock_guard<std::mutex> lock(g_gpu_textures_mutex);
        auto dst_it = g_gpu_textures.find(dst);
        auto src_it = g_gpu_textures.find(src);
        if (dst_it == g_gpu_textures.end() || src_it == g_gpu_textures.end()) {
            return false;
        }
        dst_record = dst_it->second;
        src_record = src_it->second;
    }
    const int width = clip_rect->right - clip_rect->left;
    const int height = clip_rect->bottom - clip_rect->top;
    if (width <= 0 || height <= 0 || triangle_count > 64) {
        return false;
    }
    auto op = std::make_shared<GodotGpuOp>();
    op->type = GodotGpuOp::Type::DrawTriangles;
    op->src = src_record.rid;
    op->dst = dst_record.rid;
    op->dst_pos = Vector3(clip_rect->left, clip_rect->top, 0);
    op->src_pos = Vector3(dst_record.width, dst_record.height, 0);
    op->size = Vector3(width, height, 1);
    op->src_size = Vector3(src_record.width, src_record.height, 1);
    op->mode = triangle_count;
    op->opacity = static_cast<int>(std::round(std::clamp(opacity, 0.0f, 1.0f) * 255.0f));
    op->color = blend_mode;
    // Native character frames can use a larger authored canvas and commonly
    // present at 0.5x. Keep the centre-weighted minification path enabled for
    // explicitly tagged premultiplied sources so fine eye and hair detail
    // survives even when global frame enhancement is off.
    const bool native_character_source =
        (blend_mode & TVP_GODOT_GPU_TRIANGLE_SOURCE_PREMULTIPLIED) != 0u;
    if (native_character_source) {
        g_gpu_native_character_aa_ops.fetch_add(
            1, std::memory_order_relaxed);
    }
    op->preserve_minified_detail = ShouldPreserveMinifiedTriangleDetail(
        triangle_count, dst_points, src_points, native_character_source);
    if (op->preserve_minified_detail) {
        g_gpu_detail_minify_ops.fetch_add(1, std::memory_order_relaxed);
    }
    op->vertices.reserve(static_cast<size_t>(triangle_count) * 16u);
    for (uint32_t triangle = 0; triangle < triangle_count; ++triangle) {
        const uint32_t vertex_base = triangle * 3u;
        for (uint32_t vertex = 0; vertex < 3u; ++vertex) {
            const uint32_t i = vertex_base + vertex;
            op->vertices.push_back(static_cast<float>(dst_points[i].x));
            op->vertices.push_back(static_cast<float>(dst_points[i].y));
            op->vertices.push_back(static_cast<float>(src_points[i].x));
            op->vertices.push_back(static_cast<float>(src_points[i].y));
        }
        AppendGodotGpuTriangleBounds(op->vertices, dst_points + vertex_base);
    }
    return RunGodotGpuOpAsync(op);
}

bool BridgeDrawMaskedTriangles(uint64_t dst, uint64_t src, uint64_t mask,
                               uint32_t triangle_count,
                               const tTVPRect *clip_rect,
                               const tTVPPointD *dst_points,
                               const tTVPPointD *src_points,
                               const tTVPPointD *mask_points, float opacity,
                               uint32_t blend_mode, bool inverted_mask) {
    if (clip_rect == nullptr || dst_points == nullptr || src_points == nullptr ||
        mask_points == nullptr || triangle_count == 0) {
        return false;
    }
    GodotGpuTextureRecord dst_record;
    GodotGpuTextureRecord src_record;
    GodotGpuTextureRecord mask_record;
    {
        std::lock_guard<std::mutex> lock(g_gpu_textures_mutex);
        auto dst_it = g_gpu_textures.find(dst);
        auto src_it = g_gpu_textures.find(src);
        auto mask_it = g_gpu_textures.find(mask);
        if (dst_it == g_gpu_textures.end() ||
            src_it == g_gpu_textures.end() ||
            mask_it == g_gpu_textures.end()) {
            return false;
        }
        dst_record = dst_it->second;
        src_record = src_it->second;
        mask_record = mask_it->second;
    }
    const int width = clip_rect->right - clip_rect->left;
    const int height = clip_rect->bottom - clip_rect->top;
    if (width <= 0 || height <= 0 || triangle_count > 64) {
        return false;
    }
    auto op = std::make_shared<GodotGpuOp>();
    op->type = GodotGpuOp::Type::DrawMaskedTriangles;
    op->src = src_record.rid;
    op->src2 = mask_record.rid;
    op->dst = dst_record.rid;
    op->dst_pos = Vector3(clip_rect->left, clip_rect->top, 0);
    op->src_pos = Vector3(dst_record.width, dst_record.height, 0);
    op->size = Vector3(width, height, 1);
    op->src_size = Vector3(src_record.width, src_record.height, 1);
    op->mode = triangle_count;
    op->opacity = static_cast<int>(
        std::round(std::clamp(opacity, 0.0f, 1.0f) * 255.0f));
    op->color = (blend_mode & 0xffffu) | (inverted_mask ? 0x10000u : 0u);
    op->preserve_minified_detail = ShouldPreserveMinifiedTriangleDetail(
        triangle_count, dst_points, src_points);
    if (op->preserve_minified_detail) {
        g_gpu_detail_minify_ops.fetch_add(1, std::memory_order_relaxed);
    }
    op->vertices.reserve(static_cast<size_t>(triangle_count) * 22u);
    for (uint32_t triangle = 0; triangle < triangle_count; ++triangle) {
        const uint32_t vertex_base = triangle * 3u;
        for (uint32_t vertex = 0; vertex < 3u; ++vertex) {
            const uint32_t i = vertex_base + vertex;
            op->vertices.push_back(static_cast<float>(dst_points[i].x));
            op->vertices.push_back(static_cast<float>(dst_points[i].y));
            op->vertices.push_back(static_cast<float>(src_points[i].x));
            op->vertices.push_back(static_cast<float>(src_points[i].y));
            op->vertices.push_back(static_cast<float>(mask_points[i].x));
            op->vertices.push_back(static_cast<float>(mask_points[i].y));
        }
        AppendGodotGpuTriangleBounds(op->vertices, dst_points + vertex_base);
    }
    return RunGodotGpuOpAsync(op);
}

bool BridgeMosaicRects(uint64_t texture, const tTVPRect *rects,
                       uint32_t rect_count, uint32_t block_x,
                       uint32_t block_y) {
    if (rects == nullptr || rect_count == 0 ||
        (block_x <= 1 && block_y <= 1)) {
        return false;
    }

    GodotGpuTextureRecord record;
    {
        std::lock_guard<std::mutex> lock(g_gpu_textures_mutex);
        auto it = g_gpu_textures.find(texture);
        if (it == g_gpu_textures.end()) return false;
        record = it->second;
    }
    if (record.width == 0 || record.height == 0) return false;

    auto op = std::make_shared<GodotGpuOp>();
    op->type = GodotGpuOp::Type::Mosaic;
    op->dst = record.rid;
    op->size = Vector3(record.width, record.height, 1);
    op->src_size = Vector3(std::max<uint32_t>(block_x, 1u),
                           std::max<uint32_t>(block_y, 1u), 1);
    op->vertices.reserve(static_cast<size_t>(rect_count) * 4u);

    uint32_t valid_count = 0;
    for (uint32_t i = 0; i < rect_count; ++i) {
        const int left = std::clamp(rects[i].left, 0,
                                    static_cast<int>(record.width));
        const int top = std::clamp(rects[i].top, 0,
                                   static_cast<int>(record.height));
        const int right = std::clamp(rects[i].right, 0,
                                     static_cast<int>(record.width));
        const int bottom = std::clamp(rects[i].bottom, 0,
                                      static_cast<int>(record.height));
        if (right <= left || bottom <= top) continue;
        op->vertices.push_back(static_cast<float>(left));
        op->vertices.push_back(static_cast<float>(top));
        op->vertices.push_back(static_cast<float>(right - left));
        op->vertices.push_back(static_cast<float>(bottom - top));
        ++valid_count;
    }

    if (valid_count == 0) return false;
    op->mode = valid_count;
    return RunGodotGpuOpAsync(op);
}

bool BridgeBlendRect(uint64_t dst, uint64_t src, const tTVPRect *dst_rect,
                     const tTVPRect *src_rect, uint32_t mode, int opacity,
                     uint32_t color) {
    if (dst_rect == nullptr || src_rect == nullptr) return false;
    GodotGpuTextureRecord dst_record;
    GodotGpuTextureRecord src_record;
    {
        std::lock_guard<std::mutex> lock(g_gpu_textures_mutex);
        auto dst_it = g_gpu_textures.find(dst);
        auto src_it = g_gpu_textures.find(src);
        if (dst_it == g_gpu_textures.end() || src_it == g_gpu_textures.end()) {
            return false;
        }
        dst_record = dst_it->second;
        src_record = src_it->second;
        if (mode == TVP_GODOT_GPU_BLEND_ALPHA_D) {
            src_it->second.requires_alpha_d_clear_version = true;
        }
    }
    const int width = dst_rect->right - dst_rect->left;
    const int height = dst_rect->bottom - dst_rect->top;
    const int src_width = src_rect->right - src_rect->left;
    const int src_height = src_rect->bottom - src_rect->top;
    if (width <= 0 || height <= 0 || src_width <= 0 || src_height <= 0) {
        return false;
    }
    auto op = std::make_shared<GodotGpuOp>();
    op->type = GodotGpuOp::Type::Blend;
    op->src = src_record.rid;
    op->dst = dst_record.rid;
    op->src_pos = Vector3(src_rect->left, src_rect->top, 0);
    op->dst_pos = Vector3(dst_rect->left, dst_rect->top, 0);
    op->size = Vector3(width, height, 1);
    op->src_size = Vector3(src_width, src_height, 1);
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
    GodotGpuTextureRecord dst_record;
    GodotGpuTextureRecord src1_record;
    GodotGpuTextureRecord src2_record;
    {
        std::lock_guard<std::mutex> lock(g_gpu_textures_mutex);
        auto dst_it = g_gpu_textures.find(dst);
        auto src1_it = g_gpu_textures.find(src1);
        auto src2_it = g_gpu_textures.find(src2);
        if (dst_it == g_gpu_textures.end() || src1_it == g_gpu_textures.end() ||
            src2_it == g_gpu_textures.end()) {
            return false;
        }
        dst_record = dst_it->second;
        src1_record = src1_it->second;
        src2_record = src2_it->second;
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
    op->src = src1_record.rid;
    op->src2 = src2_record.rid;
    op->dst = dst_record.rid;
    op->src_pos = Vector3(src1_rect->left, src1_rect->top, 0);
    op->src2_pos = Vector3(src2_rect->left, src2_rect->top, 0);
    op->dst_pos = Vector3(dst_rect->left, dst_rect->top, 0);
    op->size = Vector3(width, height, 1);
    op->mode = mode;
    op->opacity = opacity;
    op->color = color;
    return RunGodotGpuOpAsync(op);
}

bool BridgeBlendRect3(
    uint64_t dst, uint64_t src1, uint64_t src2, uint64_t src3,
    const tTVPRect *dst_rect, const tTVPRect *src1_rect,
    const tTVPRect *src2_rect, const tTVPRect *src3_rect, uint32_t mode,
    int opacity, uint32_t color) {
    if(dst_rect == nullptr || src1_rect == nullptr || src2_rect == nullptr ||
       src3_rect == nullptr) {
        return false;
    }
    GodotGpuTextureRecord dst_record;
    GodotGpuTextureRecord src1_record;
    GodotGpuTextureRecord src2_record;
    GodotGpuTextureRecord src3_record;
    {
        std::lock_guard<std::mutex> lock(g_gpu_textures_mutex);
        auto dst_it = g_gpu_textures.find(dst);
        auto src1_it = g_gpu_textures.find(src1);
        auto src2_it = g_gpu_textures.find(src2);
        auto src3_it = g_gpu_textures.find(src3);
        if(dst_it == g_gpu_textures.end() ||
           src1_it == g_gpu_textures.end() ||
           src2_it == g_gpu_textures.end() ||
           src3_it == g_gpu_textures.end()) {
            return false;
        }
        dst_record = dst_it->second;
        src1_record = src1_it->second;
        src2_record = src2_it->second;
        src3_record = src3_it->second;
    }
    const int width = dst_rect->right - dst_rect->left;
    const int height = dst_rect->bottom - dst_rect->top;
    const auto same_size = [&](const tTVPRect *rect) {
        return width == rect->right - rect->left &&
            height == rect->bottom - rect->top;
    };
    if(width <= 0 || height <= 0 || !same_size(src1_rect) ||
       !same_size(src2_rect) || !same_size(src3_rect)) {
        return false;
    }

    auto op = std::make_shared<GodotGpuOp>();
    op->type = GodotGpuOp::Type::Blend3;
    op->src = src1_record.rid;
    op->src2 = src2_record.rid;
    op->src3 = src3_record.rid;
    op->dst = dst_record.rid;
    op->src_pos = Vector3(src1_rect->left, src1_rect->top, 0);
    op->src2_pos = Vector3(src2_rect->left, src2_rect->top, 0);
    op->src3_pos = Vector3(src3_rect->left, src3_rect->top, 0);
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
    GodotGpuTextureRecord record;
    {
        std::lock_guard<std::mutex> lock(g_gpu_textures_mutex);
        auto it = g_gpu_textures.find(texture);
        if (it == g_gpu_textures.end()) return false;
        record = it->second;
    }
    const uint32_t tight_stride = record.width * 4u;
    const uint32_t dst_stride = stride_bytes != 0 ? stride_bytes : tight_stride;
    if (out_pixels_size < static_cast<size_t>(dst_stride) * record.height) {
        return false;
    }
    auto op = std::make_shared<GodotGpuOp>();
    op->type = GodotGpuOp::Type::Read;
    op->src = record.rid;
    op->profile_width = record.width;
    op->profile_height = record.height;
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

uint64_t BridgeBeginReadRgba(uint64_t texture) {
    GodotGpuTextureRecord record;
    {
        std::lock_guard<std::mutex> lock(g_gpu_textures_mutex);
        const auto found = g_gpu_textures.find(texture);
        if(found == g_gpu_textures.end()) return 0;
        record = found->second;
    }
    auto op = std::make_shared<GodotGpuOp>();
    op->type = GodotGpuOp::Type::ReadAsync;
    op->src = record.rid;
    uint64_t request = 0;
    {
        std::lock_guard<std::mutex> lock(g_gpu_readbacks_mutex);
        request = g_next_gpu_readback_id++;
        if(request == 0) request = g_next_gpu_readback_id++;
        g_gpu_readbacks.emplace(
            request, GodotGpuReadbackRequest{op, record.width, record.height});
    }
    op->readback_request = request;
    if(!RunGodotGpuOpAsync(op)) {
        std::lock_guard<std::mutex> lock(g_gpu_readbacks_mutex);
        g_gpu_readbacks.erase(request);
        return 0;
    }
    return request;
}

bool BridgePollReadRgba(uint64_t request, void *out_pixels,
                        size_t out_pixels_size, uint32_t stride_bytes,
                        bool *ready) {
    if(ready) *ready = false;
    if(request == 0 || out_pixels == nullptr) return false;
    GodotGpuReadbackRequest record;
    {
        std::lock_guard<std::mutex> lock(g_gpu_readbacks_mutex);
        const auto found = g_gpu_readbacks.find(request);
        if(found == g_gpu_readbacks.end()) return false;
        record = found->second;
    }
    {
        std::lock_guard<std::mutex> lock(record.op->done_mutex);
        if(!record.op->done) return true;
    }
    if(ready) *ready = true;
    const uint32_t tight_stride = record.width * 4u;
    const uint32_t dst_stride = stride_bytes != 0 ? stride_bytes : tight_stride;
    const size_t required = static_cast<size_t>(dst_stride) * record.height;
    bool success = record.op->result && out_pixels_size >= required;
    if(success) {
        const uint8_t *source = record.op->data.ptr();
        success = source != nullptr &&
            static_cast<size_t>(record.op->data.size()) >=
                static_cast<size_t>(tight_stride) * record.height;
        if(success) {
            auto *destination = static_cast<uint8_t *>(out_pixels);
            for(uint32_t y = 0; y < record.height; ++y) {
                std::memcpy(destination + static_cast<size_t>(y) * dst_stride,
                            source + static_cast<size_t>(y) * tight_stride,
                            tight_stride);
            }
        }
    }
    {
        std::lock_guard<std::mutex> lock(g_gpu_readbacks_mutex);
        g_gpu_readbacks.erase(request);
    }
    return success;
}

void BridgeDiscardReadRgba(uint64_t request) {
    if(request == 0) return;
    std::lock_guard<std::mutex> lock(g_gpu_readbacks_mutex);
    g_gpu_readbacks.erase(request);
}

bool BridgeFlush() {
    auto op = std::make_shared<GodotGpuOp>();
    op->type = GodotGpuOp::Type::Flush;
    // The caller publishes the destination texture immediately after flush.
    // Wait only until the render thread has encoded and submitted the work;
    // ExecuteGodotGpuOp intentionally does not call RenderingDevice::sync().
    return RunGodotGpuOpSync(op);
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

uint32_t CpuPsScreenBlend(uint32_t d, uint32_t s, int opacity) {
    const uint32_t src_alpha = (s >> 24) & 0xffu;
    const uint32_t opa = static_cast<uint32_t>(std::clamp(opacity, 0, 255));
    const uint32_t a = opa == 255u ? src_alpha : ((src_alpha * opa) >> 8);
    const uint32_t dr = d & 0xffu;
    const uint32_t dg = (d >> 8) & 0xffu;
    const uint32_t db = (d >> 16) & 0xffu;
    const uint32_t sr = s & 0xffu;
    const uint32_t sg = (s >> 8) & 0xffu;
    const uint32_t sb = (s >> 16) & 0xffu;
    const uint32_t r =
        std::min(dr + (((sr - ((sr * dr) >> 8)) * a) >> 8), 255u);
    const uint32_t g =
        std::min(dg + (((sg - ((sg * dg) >> 8)) * a) >> 8), 255u);
    const uint32_t b =
        std::min(db + (((sb - ((sb * db) >> 8)) * a) >> 8), 255u);
    return (d & 0xff000000u) | r | (g << 8) | (b << 16);
}

uint32_t CpuPsMulBlend(uint32_t d, uint32_t s, int opacity) {
    const uint32_t opa = static_cast<uint32_t>(std::clamp(opacity, 0, 255));
    uint32_t a = (s >> 24) & 0xffu;
    if (opa != 255u) {
        a = (a * opa) >> 8;
    }
    const int dr = static_cast<int>(d & 0xffu);
    const int dg = static_cast<int>((d >> 8) & 0xffu);
    const int db = static_cast<int>((d >> 16) & 0xffu);
    const int mr = (dr * static_cast<int>(s & 0xffu)) >> 8;
    const int mg = (dg * static_cast<int>((s >> 8) & 0xffu)) >> 8;
    const int mb = (db * static_cast<int>((s >> 16) & 0xffu)) >> 8;
    const uint32_t r = static_cast<uint32_t>(
        std::clamp(dr + (((mr - dr) * static_cast<int>(a)) >> 8), 0, 255));
    const uint32_t g = static_cast<uint32_t>(
        std::clamp(dg + (((mg - dg) * static_cast<int>(a)) >> 8), 0, 255));
    const uint32_t b = static_cast<uint32_t>(
        std::clamp(db + (((mb - db) * static_cast<int>(a)) >> 8), 0, 255));
    return (d & 0xff000000u) | r | (g << 8) | (b << 16);
}

uint32_t CpuPsAddBlend(uint32_t d, uint32_t s, int opacity) {
    const uint32_t opa = static_cast<uint32_t>(std::clamp(opacity, 0, 255));
    uint32_t a = (s >> 24) & 0xffu;
    if (opa != 255u) {
        a = (a * opa) >> 8;
    }
    const int dr = static_cast<int>(d & 0xffu);
    const int dg = static_cast<int>((d >> 8) & 0xffu);
    const int db = static_cast<int>((d >> 16) & 0xffu);
    const int br = std::min(dr + static_cast<int>(s & 0xffu), 255);
    const int bg =
        std::min(dg + static_cast<int>((s >> 8) & 0xffu), 255);
    const int bb =
        std::min(db + static_cast<int>((s >> 16) & 0xffu), 255);
    const uint32_t r = static_cast<uint32_t>(
        std::clamp(dr + (((br - dr) * static_cast<int>(a)) >> 8), 0, 255));
    const uint32_t g = static_cast<uint32_t>(
        std::clamp(dg + (((bg - dg) * static_cast<int>(a)) >> 8), 0, 255));
    const uint32_t b = static_cast<uint32_t>(
        std::clamp(db + (((bb - db) * static_cast<int>(a)) >> 8), 0, 255));
    return (d & 0xff000000u) | r | (g << 8) | (b << 16);
}

uint32_t CpuPsSubBlend(uint32_t d, uint32_t s, int opacity) {
    const uint32_t opa = static_cast<uint32_t>(std::clamp(opacity, 0, 255));
    uint32_t a = (s >> 24) & 0xffu;
    if (opa != 255u) {
        a = (a * opa) >> 8;
    }
    const int dr = static_cast<int>(d & 0xffu);
    const int dg = static_cast<int>((d >> 8) & 0xffu);
    const int db = static_cast<int>((d >> 16) & 0xffu);
    const int br =
        std::max(dr + static_cast<int>(s & 0xffu) - 255, 0);
    const int bg =
        std::max(dg + static_cast<int>((s >> 8) & 0xffu) - 255, 0);
    const int bb =
        std::max(db + static_cast<int>((s >> 16) & 0xffu) - 255, 0);
    const uint32_t r = static_cast<uint32_t>(
        std::clamp(dr + (((br - dr) * static_cast<int>(a)) >> 8), 0, 255));
    const uint32_t g = static_cast<uint32_t>(
        std::clamp(dg + (((bg - dg) * static_cast<int>(a)) >> 8), 0, 255));
    const uint32_t b = static_cast<uint32_t>(
        std::clamp(db + (((bb - db) * static_cast<int>(a)) >> 8), 0, 255));
    return (d & 0xff000000u) | r | (g << 8) | (b << 16);
}

uint32_t CpuPsSoftLightBlend(uint32_t d, uint32_t s, int opacity) {
    const auto channel = [](uint32_t source, uint32_t destination) {
        const double sf = static_cast<double>(source) / 255.0;
        const double df = static_cast<double>(destination) / 255.0;
        const double exponent = source >= 128u
            ? 128.0 / static_cast<double>(std::max(source, 1u))
            : (1.0 - sf) / 0.5;
        return static_cast<uint32_t>(std::clamp(
            static_cast<int>(std::pow(std::max(df, 0.0), exponent) * 255.0),
            0, 255));
    };
    uint32_t a = (s >> 24) & 0xffu;
    const uint32_t opa = static_cast<uint32_t>(std::clamp(opacity, 0, 255));
    if (opa != 255u) a = (a * opa) >> 8;
    const int dr = static_cast<int>(d & 0xffu);
    const int dg = static_cast<int>((d >> 8) & 0xffu);
    const int db = static_cast<int>((d >> 16) & 0xffu);
    const int sr = static_cast<int>(channel(s & 0xffu, d & 0xffu));
    const int sg = static_cast<int>(channel((s >> 8) & 0xffu,
                                            (d >> 8) & 0xffu));
    const int sb = static_cast<int>(channel((s >> 16) & 0xffu,
                                            (d >> 16) & 0xffu));
    const uint32_t r = static_cast<uint32_t>(std::clamp(
        dr + (((sr - dr) * static_cast<int>(a)) >> 8), 0, 255));
    const uint32_t g = static_cast<uint32_t>(std::clamp(
        dg + (((sg - dg) * static_cast<int>(a)) >> 8), 0, 255));
    const uint32_t b = static_cast<uint32_t>(std::clamp(
        db + (((sb - db) * static_cast<int>(a)) >> 8), 0, 255));
    return (d & 0xff000000u) | r | (g << 8) | (b << 16);
}

uint32_t CpuAdditiveAlphaBlendHda(uint32_t d, uint32_t s, int opacity) {
    const uint32_t opa = static_cast<uint32_t>(std::clamp(opacity, 0, 255));
    if(opa != 255u) {
        const uint32_t rb = ((s & 0x00ff00ffu) * opa >> 8) & 0x00ff00ffu;
        const uint32_t ga = ((s >> 8 & 0x00ff00ffu) * opa) & 0xff00ff00u;
        s = rb | ga;
    }
    const uint32_t inverse_alpha = (~s) >> 24;
    const uint32_t r = std::min(
        (((d & 0xffu) * inverse_alpha) >> 8) + (s & 0xffu), 255u);
    const uint32_t g = std::min(
        (((d >> 8 & 0xffu) * inverse_alpha) >> 8) +
            (s >> 8 & 0xffu),
        255u);
    const uint32_t b = std::min(
        (((d >> 16 & 0xffu) * inverse_alpha) >> 8) +
            (s >> 16 & 0xffu),
        255u);
    return (d & 0xff000000u) | r | (g << 8) | (b << 16);
}

uint32_t CpuApplyColorMapA(uint32_t d, uint32_t mask, int opacity,
                           uint32_t color) {
    const uint32_t opa = static_cast<uint32_t>(std::clamp(opacity, 0, 255));
    uint32_t source_alpha = opa == 255u ? mask : ((mask * opa) >> 8);
    source_alpha -= source_alpha >> 8;
    const uint32_t inverse_alpha = source_alpha ^ 0xffu;
    uint32_t out_alpha = ((d >> 24) & 0xffu) + source_alpha -
                         ((((d >> 24) & 0xffu) * source_alpha) >> 8);
    out_alpha -= out_alpha >> 8;
    const uint32_t r = std::min(
        (((d & 0xffu) * inverse_alpha) >> 8) +
            ((source_alpha * (color & 0xffu)) >> 8),
        255u);
    const uint32_t g = std::min(
        (((d >> 8 & 0xffu) * inverse_alpha) >> 8) +
            ((source_alpha * (color >> 8 & 0xffu)) >> 8),
        255u);
    const uint32_t b = std::min(
        (((d >> 16 & 0xffu) * inverse_alpha) >> 8) +
            ((source_alpha * (color >> 16 & 0xffu)) >> 8),
        255u);
    return (out_alpha << 24) | r | (g << 8) | (b << 16);
}

uint32_t CpuAdditiveAlphaToAlpha(uint32_t color) {
    const uint32_t alpha = color >> 24;
    if (alpha == 0u) return color & 0xff000000u;
    const uint32_t r = std::min((color & 0xffu) * 255u / alpha, 255u);
    const uint32_t g = std::min(((color >> 8) & 0xffu) * 255u / alpha, 255u);
    const uint32_t b = std::min(((color >> 16) & 0xffu) * 255u / alpha, 255u);
    return (color & 0xff000000u) | r | (g << 8) | (b << 16);
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
        case TVP_GODOT_GPU_BLEND_FILL_MASK:
            return (d & 0x00ffffffu) |
                   (static_cast<uint32_t>(std::clamp(opacity, 0, 255)) << 24);
        case TVP_GODOT_GPU_BLEND_COPY_RGBA:
            return s;
        case TVP_GODOT_GPU_BLEND_COPY_OPAQUE:
            return (s & 0x00ffffffu) | 0xff000000u;
        case TVP_GODOT_GPU_BLEND_ALPHA_BLEND_A:
            return CpuAlphaBlendA(d, s, opacity);
        case TVP_GODOT_GPU_BLEND_CONST_ALPHA_D:
            return CpuConstAlphaBlendD(d, s, opacity);
        case TVP_GODOT_GPU_BLEND_PS_SCREEN:
            return CpuPsScreenBlend(d, s, opacity);
        case TVP_GODOT_GPU_BLEND_PS_MULTIPLY:
            return CpuPsMulBlend(d, s, opacity);
        case TVP_GODOT_GPU_BLEND_PS_ADD:
            return CpuPsAddBlend(d, s, opacity);
        case TVP_GODOT_GPU_BLEND_PS_SUBTRACT:
            return CpuPsSubBlend(d, s, opacity);
        case TVP_GODOT_GPU_BLEND_PS_SOFT_LIGHT:
            return CpuPsSoftLightBlend(d, s, opacity);
        case TVP_GODOT_GPU_BLEND_APPLY_COLOR_MAP_A:
            return CpuApplyColorMapA(d, s & 0xffu, opacity, color);
        case TVP_GODOT_GPU_BLEND_ADDITIVE_ALPHA:
            return CpuAdditiveAlphaBlendHda(d, s, opacity);
        case TVP_GODOT_GPU_BLEND_ADDITIVE_ALPHA_A: {
            const uint32_t opa =
                static_cast<uint32_t>(std::clamp(opacity, 0, 255));
            if(opa != 255u) {
                const uint32_t rb =
                    ((s & 0x00ff00ffu) * opa >> 8) & 0x00ff00ffu;
                const uint32_t ga =
                    ((s >> 8 & 0x00ff00ffu) * opa) & 0xff00ff00u;
                s = rb | ga;
            }
            const uint32_t da = d >> 24;
            const uint32_t sa = s >> 24;
            uint32_t out_alpha = da + sa - ((da * sa) >> 8);
            out_alpha -= out_alpha >> 8;
            return (CpuAdditiveAlphaBlendHda(d, s, 255) & 0x00ffffffu) |
                (out_alpha << 24);
        }
        case TVP_GODOT_GPU_BLEND_ALPHA_TO_ADDITIVE_ALPHA:
            return CpuAlphaToAdditiveAlpha(s);
        case TVP_GODOT_GPU_BLEND_ADDITIVE_ALPHA_TO_ALPHA:
            return CpuAdditiveAlphaToAlpha(s);
        default:
            return s;
    }
}

uint32_t CpuBlend2Reference(uint32_t mode, uint32_t dst, uint32_t src1,
                            uint32_t src2, int opacity) {
    switch (mode) {
        case TVP_GODOT_GPU_BLEND_CONST_ALPHA_SD:
            return CpuConstAlphaBlendSD(src1, src2, opacity);
        case TVP_GODOT_GPU_BLEND_CONST_ALPHA_SD_D:
            return CpuConstAlphaBlendSDD(src1, src2, opacity);
        case TVP_GODOT_GPU_BLEND_ALPHA_D_MASK_MULTIPLY: {
            const uint32_t src_alpha = (src1 >> 24) & 0xffu;
            const uint32_t mask_alpha = (src2 >> 24) & 0xffu;
            const uint32_t masked_src =
                (src1 & 0x00ffffffu) |
                (((src_alpha * mask_alpha) / 255u) << 24);
            return CpuAlphaBlendD(dst, masked_src, opacity);
        }
        case TVP_GODOT_GPU_BLEND_ALPHA_D_MASK_THRESHOLD: {
            const uint32_t mask_alpha = (src2 >> 24) & 0xffu;
            const uint32_t masked_src = mask_alpha < 64u
                ? (src1 & 0x00ffffffu)
                : src1;
            return CpuAlphaBlendD(dst, masked_src, opacity);
        }
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
    if (lower == "copyopaqueimage" || lower == "copy_opaque_image") {
        return TVP_GODOT_GPU_BLEND_COPY_OPAQUE;
    }
    if (lower == "fillargb" || lower == "fill") {
        return TVP_GODOT_GPU_BLEND_FILL_ARGB;
    }
    if (lower == "removeconstopacity" || lower == "remove_const_opacity") {
        return TVP_GODOT_GPU_BLEND_REMOVE_CONST_OPACITY;
    }
    if (lower == "fillmask" || lower == "fill_mask") {
        return TVP_GODOT_GPU_BLEND_FILL_MASK;
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
    if (lower == "alphablend_d_mask_multiply" ||
        lower == "alpha_blend_d_mask_multiply") {
        return TVP_GODOT_GPU_BLEND_ALPHA_D_MASK_MULTIPLY;
    }
    if (lower == "alphablend_d_mask_threshold" ||
        lower == "alpha_blend_d_mask_threshold") {
        return TVP_GODOT_GPU_BLEND_ALPHA_D_MASK_THRESHOLD;
    }
    if (lower == "additivealphablend_a" ||
        lower == "additive_alpha_blend_a") {
        return TVP_GODOT_GPU_BLEND_ADDITIVE_ALPHA_A;
    }
    if (lower == "additivealphablend" || lower == "additive_alpha_blend") {
        return TVP_GODOT_GPU_BLEND_ADDITIVE_ALPHA;
    }
    if (lower == "alphatoadditivealpha" ||
        lower == "alpha_to_additive_alpha") {
        return TVP_GODOT_GPU_BLEND_ALPHA_TO_ADDITIVE_ALPHA;
    }
    if (lower == "additivealphatoalpha" ||
        lower == "additive_alpha_to_alpha") {
        return TVP_GODOT_GPU_BLEND_ADDITIVE_ALPHA_TO_ALPHA;
    }
    if (lower == "applycolormap_a" || lower == "apply_color_map_a") {
        return TVP_GODOT_GPU_BLEND_APPLY_COLOR_MAP_A;
    }
    if (lower == "psscreenblend" || lower == "ps_screen_blend") {
        return TVP_GODOT_GPU_BLEND_PS_SCREEN;
    }
    if (lower == "psmulblend" || lower == "ps_mul_blend") {
        return TVP_GODOT_GPU_BLEND_PS_MULTIPLY;
    }
    if (lower == "psaddblend" || lower == "ps_add_blend") {
        return TVP_GODOT_GPU_BLEND_PS_ADD;
    }
    if (lower == "pssubblend" || lower == "ps_sub_blend") {
        return TVP_GODOT_GPU_BLEND_PS_SUBTRACT;
    }
    if (lower == "pssoftlightblend" || lower == "ps_soft_light_blend") {
        return TVP_GODOT_GPU_BLEND_PS_SOFT_LIGHT;
    }
    return 0;
}

void ReleaseGodotGpuPipeline() {
    RenderingDevice *rd = MainRenderingDevice();
    if (rd != nullptr) {
        for (const auto &entry : g_live2d_framebuffer_cache) {
            if (entry.second.is_valid()) rd->free_rid(entry.second);
        }
        for (const auto &entry : g_artemis_shader_pipeline_cache) {
            if (entry.second.pipeline.is_valid()) {
                rd->free_rid(entry.second.pipeline);
            }
            if (entry.second.shader.is_valid()) {
                rd->free_rid(entry.second.shader);
            }
        }
    }
    g_live2d_framebuffer_cache.clear();
    g_artemis_shader_pipeline_cache.clear();
    if (g_gpu_pipeline_state == nullptr) return;
    if (rd != nullptr) {
        ClearGodotGpuUniformSetCache(rd);
        if (g_gpu_pipeline_state->fill_source_texture.is_valid()) {
            rd->free_rid(g_gpu_pipeline_state->fill_source_texture);
        }
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
        if (g_gpu_pipeline_state->alpha_convert_pipeline.is_valid()) {
            rd->free_rid(g_gpu_pipeline_state->alpha_convert_pipeline);
        }
        if (g_gpu_pipeline_state->alpha_convert_shader.is_valid()) {
            rd->free_rid(g_gpu_pipeline_state->alpha_convert_shader);
        }
        if (g_gpu_pipeline_state->blend2_pipeline.is_valid()) {
            rd->free_rid(g_gpu_pipeline_state->blend2_pipeline);
        }
        if (g_gpu_pipeline_state->blend2_shader.is_valid()) {
            rd->free_rid(g_gpu_pipeline_state->blend2_shader);
        }
        if(g_gpu_pipeline_state->blend3_pipeline.is_valid()) {
            rd->free_rid(g_gpu_pipeline_state->blend3_pipeline);
        }
        if(g_gpu_pipeline_state->blend3_shader.is_valid()) {
            rd->free_rid(g_gpu_pipeline_state->blend3_shader);
        }
        if (g_gpu_pipeline_state->copy_triangles_pipeline.is_valid()) {
            rd->free_rid(g_gpu_pipeline_state->copy_triangles_pipeline);
        }
        if (g_gpu_pipeline_state->copy_triangles_shader.is_valid()) {
            rd->free_rid(g_gpu_pipeline_state->copy_triangles_shader);
        }
        if (g_gpu_pipeline_state->draw_triangles_pipeline.is_valid()) {
            rd->free_rid(g_gpu_pipeline_state->draw_triangles_pipeline);
        }
        if (g_gpu_pipeline_state->draw_triangles_shader.is_valid()) {
            rd->free_rid(g_gpu_pipeline_state->draw_triangles_shader);
        }
        if (g_gpu_pipeline_state->draw_masked_triangles_pipeline.is_valid()) {
            rd->free_rid(g_gpu_pipeline_state->draw_masked_triangles_pipeline);
        }
        if (g_gpu_pipeline_state->draw_masked_triangles_shader.is_valid()) {
            rd->free_rid(g_gpu_pipeline_state->draw_masked_triangles_shader);
        }
        if (g_gpu_pipeline_state->live2d_raster_normal_pipeline.is_valid()) {
            rd->free_rid(
                g_gpu_pipeline_state->live2d_raster_normal_pipeline);
        }
        if (g_gpu_pipeline_state->live2d_raster_add_pipeline.is_valid()) {
            rd->free_rid(g_gpu_pipeline_state->live2d_raster_add_pipeline);
        }
        if (g_gpu_pipeline_state->live2d_raster_multiply_pipeline.is_valid()) {
            rd->free_rid(
                g_gpu_pipeline_state->live2d_raster_multiply_pipeline);
        }
        if (g_gpu_pipeline_state->live2d_raster_mask_pipeline.is_valid()) {
            rd->free_rid(g_gpu_pipeline_state->live2d_raster_mask_pipeline);
        }
        if (g_gpu_pipeline_state->live2d_raster_shader.is_valid()) {
            rd->free_rid(g_gpu_pipeline_state->live2d_raster_shader);
        }
        if (g_gpu_pipeline_state->live2d_raster_sampler.is_valid()) {
            rd->free_rid(g_gpu_pipeline_state->live2d_raster_sampler);
        }
        if (g_gpu_pipeline_state->live2d_unpremultiply_pipeline.is_valid()) {
            rd->free_rid(
                g_gpu_pipeline_state->live2d_unpremultiply_pipeline);
        }
        if (g_gpu_pipeline_state->live2d_unpremultiply_shader.is_valid()) {
            rd->free_rid(g_gpu_pipeline_state->live2d_unpremultiply_shader);
        }
        if (g_gpu_pipeline_state->mosaic_pipeline.is_valid()) {
            rd->free_rid(g_gpu_pipeline_state->mosaic_pipeline);
        }
        if (g_gpu_pipeline_state->mosaic_shader.is_valid()) {
            rd->free_rid(g_gpu_pipeline_state->mosaic_shader);
        }
        if (g_gpu_pipeline_state->triangle_vertex_buffer.is_valid()) {
            rd->free_rid(g_gpu_pipeline_state->triangle_vertex_buffer);
        }
    }
    delete g_gpu_pipeline_state;
    g_gpu_pipeline_state = nullptr;
}

void ReleaseRemainingGodotGpuTextures() {
    {
        std::lock_guard<std::mutex> lock(g_gpu_readbacks_mutex);
        g_gpu_readbacks.clear();
    }
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
            op->native_image = record.apple_pixel_buffer != nullptr
                ? record.apple_pixel_buffer
                : nullptr;
            op->native_resource = record.apple_vulkan_external_texture != nullptr
                ? record.apple_vulkan_external_texture
                : record.android_external_texture;
            RunGodotGpuOpSync(op);
        }
    }
}

} // namespace

class AetherRuntimePlayer final : public Node {
    GDCLASS(AetherRuntimePlayer, Node)

public:
    AetherRuntimePlayer()
        : frame_effect_provider_(CreateFrameEffectProvider()) {}
    ~AetherRuntimePlayer() override {
        destroy_engine();
        AudioServer *server = AudioServer::get_singleton();
        if (server != nullptr) {
            for (const StringName &bus : runtime_audio_buses_) {
                const int32_t index = server->get_bus_index(bus);
                if (index >= 0) server->remove_bus(index);
            }
        }
    }

    bool initialize_engine(const String &writable_path, const String &cache_path) {
        if (handle_ != nullptr) {
            AndroidBridgeLog("event=initialize_engine_already_initialized runtime=%s",
                             runtime_id_.utf8().get_data());
            return true;
        }

        AndroidBridgeLog("event=initialize_engine_begin writable=%s cache=%s",
                         writable_path.utf8().get_data(),
                         cache_path.utf8().get_data());

        TVPGodotGpuBridgeCallbacks callbacks{};
        callbacks.create_rgba = BridgeCreateRgba;
        callbacks.release_texture = BridgeReleaseTexture;
        callbacks.update_rgba = BridgeUpdateRgba;
        callbacks.clear_rgba = BridgeClearRgba;
        callbacks.copy_rect = BridgeCopyRect;
        callbacks.copy_triangles = BridgeCopyTriangles;
        callbacks.draw_triangles = BridgeDrawTriangles;
        callbacks.draw_masked_triangles = BridgeDrawMaskedTriangles;
        callbacks.mosaic_rects = BridgeMosaicRects;
        callbacks.blend_rect = BridgeBlendRect;
        callbacks.blend_rect2 = BridgeBlendRect2;
        callbacks.blend_rect3 = BridgeBlendRect3;
        callbacks.read_rgba = BridgeReadRgba;
        callbacks.begin_read_rgba = BridgeBeginReadRgba;
        callbacks.poll_read_rgba = BridgePollReadRgba;
        callbacks.discard_read_rgba = BridgeDiscardReadRgba;
        callbacks.flush = BridgeFlush;
        engine_register_godot_gpu_bridge(&callbacks);
        TVPGodotGpuBatchCallbacks batch_callbacks{};
        batch_callbacks.struct_size = sizeof(batch_callbacks);
        batch_callbacks.abi_version =
            TVP_GODOT_GPU_BATCH_CALLBACKS_ABI_VERSION;
        batch_callbacks.begin_batch = BridgeBeginBatch;
        batch_callbacks.end_batch = BridgeEndBatch;
        engine_register_godot_gpu_batch_bridge(&batch_callbacks);
        TVPGodotGpuExternalTextureCallbacks external_texture_callbacks{};
        external_texture_callbacks.struct_size =
            sizeof(external_texture_callbacks);
        external_texture_callbacks.abi_version =
            TVP_GODOT_GPU_EXTERNAL_TEXTURE_CALLBACKS_ABI_VERSION;
        external_texture_callbacks.import_apple_pixel_buffer =
            BridgeImportApplePixelBuffer;
        external_texture_callbacks.prepare_for_native_write =
            BridgePrepareForNativeWrite;
        external_texture_callbacks.import_android_hardware_buffer =
            BridgeImportAndroidHardwareBuffer;
        external_texture_callbacks.publish_native_write =
            BridgePublishNativeWrite;
        engine_register_godot_gpu_external_texture_bridge(
            &external_texture_callbacks);

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
        if (result == ENGINE_RESULT_OK) {
            sync_frame_effect_source_mode(true);
        }
        AndroidBridgeLog("event=initialize_engine_result result=%s error=%s",
                         last_result_.utf8().get_data(),
                         last_error_.utf8().get_data());
        return result == ENGINE_RESULT_OK;
    }

    void destroy_engine() {
        media_close();
        clear_runtime_message();
        clear_runtime_save_slots();
        runtime_presentation_sprite_.Clear();
        reset_runtime_tick_timing();
        if (handle_ == nullptr) {
            return;
        }
        release_frame_texture();
        const engine_result_t result = engine_destroy(handle_);
        BridgeFlush();
        if (result != ENGINE_RESULT_OK) {
            last_result_ = ResultToString(result);
            last_error_ = LastError(handle_);
        }
        handle_ = nullptr;
        game_open_ = false;
        runtime_id_ = "auto";
    }

    void release_frame_texture() {
        if (frame_effect_provider_ != nullptr) {
            frame_effect_provider_->release(main_rendering_device());
        }
        frame_effect_active_ = false;
        frame_effect_pipeline_ = "none";
        frame_effect_error_ = "";
        frame_source_width_ = 0;
        frame_source_height_ = 0;
        release_rd_texture(true);
        release_presentation_textures(true);
        frame_texture_.unref();
        frame_texture_backend_ = "none";
    }

    bool is_frame_enhancement_built() const {
        return frame_effect_provider_ != nullptr;
    }

    bool is_text_translation_available() const {
        return engine_is_text_translation_available() != 0;
    }

    int get_text_translation_state() const {
        return static_cast<int>(engine_get_text_translation_state());
    }

    Dictionary get_text_translation_stats() const {
        Dictionary output;
        engine_text_translation_stats_t stats{};
        stats.struct_size = sizeof(stats);
        if (engine_get_text_translation_stats(&stats) != ENGINE_RESULT_OK) {
            return output;
        }
        output["state"] = static_cast<int64_t>(stats.state);
        output["backend"] = static_cast<int64_t>(stats.backend);
        output["active_jobs"] = static_cast<int64_t>(stats.active_jobs);
        output["model_file_bytes"] = static_cast<int64_t>(stats.model_file_bytes);
        output["model_tensor_bytes"] = static_cast<int64_t>(stats.model_tensor_bytes);
        output["context_state_bytes"] = static_cast<int64_t>(stats.context_state_bytes);
        output["model_resident_bytes_estimate"] =
            static_cast<int64_t>(stats.model_resident_bytes_estimate);
        output["priority_queue_entries"] =
            static_cast<int64_t>(stats.priority_queue_entries);
        output["prefetch_queue_entries"] =
            static_cast<int64_t>(stats.prefetch_queue_entries);
        output["cache_entries"] = static_cast<int64_t>(stats.cache_entries);
        output["failed_entries"] = static_cast<int64_t>(stats.failed_entries);
        output["cache_hits"] = static_cast<int64_t>(stats.cache_hits);
        output["cache_misses"] = static_cast<int64_t>(stats.cache_misses);
        output["translations_started"] =
            static_cast<int64_t>(stats.translations_started);
        output["translations_completed"] =
            static_cast<int64_t>(stats.translations_completed);
        output["translations_failed"] =
            static_cast<int64_t>(stats.translations_failed);
        output["translations_cancelled"] =
            static_cast<int64_t>(stats.translations_cancelled);
        output["model_load_us"] = static_cast<int64_t>(stats.model_load_us);
        output["last_inference_us"] =
            static_cast<int64_t>(stats.last_inference_us);
        output["max_inference_us"] = static_cast<int64_t>(stats.max_inference_us);
        output["last_synchronous_wait_us"] =
            static_cast<int64_t>(stats.last_synchronous_wait_us);
        output["total_synchronous_wait_us"] =
            static_cast<int64_t>(stats.total_synchronous_wait_us);
        return output;
    }

    bool is_frame_enhancement_available() const {
        if (frame_effect_provider_ == nullptr) {
            return false;
        }
        String reason;
        return frame_effect_provider_->is_available(main_rendering_device(),
                                                     &reason);
    }

    void set_frame_enhancement_enabled(bool enabled) {
        frame_effect_enabled_ = enabled;
        g_frame_enhancement_detail_sampling.store(
            enabled && frame_effect_provider_ != nullptr,
            std::memory_order_release);
        frame_effect_active_ = false;
        frame_effect_error_ = "";
        frame_effect_bypass_due_to_error_ = false;
        if (frame_effect_provider_ == nullptr) {
            sync_frame_effect_source_mode();
            return;
        }
        frame_effect_provider_->set_enabled(enabled);
        if (!enabled) {
            frame_effect_provider_->release(main_rendering_device());
            frame_effect_pipeline_ = "none";
        }
        sync_frame_effect_source_mode();
    }

    void set_frame_native_output_enabled(bool enabled) {
        frame_native_output_enabled_ = enabled;
        sync_frame_effect_source_mode();
    }

    void set_frame_enhancement_mode(const String &mode) {
        frame_effect_mode_ = mode.strip_edges().to_lower();
        if (frame_effect_mode_.is_empty()) {
            frame_effect_mode_ = "auto";
        }
        frame_effect_active_ = false;
        frame_effect_bypass_due_to_error_ = false;
        if (frame_effect_provider_ != nullptr) {
            frame_effect_provider_->set_mode(frame_effect_mode_);
        }
        sync_frame_effect_source_mode();
    }

    void set_frame_enhancement_custom_chain(const PackedStringArray &chain) {
        PackedStringArray normalized;
        for (int64_t index = 0; index < chain.size(); ++index) {
            const String algorithm = chain[index].strip_edges().to_lower();
            if (!algorithm.is_empty()) normalized.push_back(algorithm);
        }
        if (normalized == frame_effect_custom_chain_) return;
        frame_effect_custom_chain_ = normalized;
        frame_effect_active_ = false;
        frame_effect_bypass_due_to_error_ = false;
        if (frame_effect_provider_ != nullptr) {
            frame_effect_provider_->release(main_rendering_device());
        }
        sync_frame_effect_source_mode();
    }

    void set_frame_enhancement_target_size(int width, int height) {
        frame_effect_target_width_ = static_cast<uint32_t>(std::max(0, width));
        frame_effect_target_height_ = static_cast<uint32_t>(std::max(0, height));
    }

    Vector2i get_frame_source_size() const {
        return Vector2i(static_cast<int32_t>(frame_source_width_),
                        static_cast<int32_t>(frame_source_height_));
    }

    Dictionary get_frame_enhancement_status() const {
        Dictionary result;
        result["built"] = frame_effect_provider_ != nullptr;
        result["enabled"] = frame_effect_enabled_;
        result["layer_detail_preservation"] =
            g_frame_enhancement_detail_sampling.load(
                std::memory_order_acquire);
        result["active"] = frame_effect_active_;
        result["mode"] = frame_effect_mode_;
        result["custom_chain"] = frame_effect_custom_chain_;
        result["pipeline"] = frame_effect_pipeline_;
        result["error"] = frame_effect_error_;
        result["source_width"] = static_cast<int64_t>(frame_source_width_);
        result["source_height"] = static_cast<int64_t>(frame_source_height_);
        result["target_width"] = static_cast<int64_t>(frame_effect_target_width_);
        result["target_height"] = static_cast<int64_t>(frame_effect_target_height_);
        result["native_output_requested"] = frame_native_output_enabled_;
        result["raw_source_output"] = frame_effect_raw_source_output_;
        result["platform_raw_source"] = platform_prefers_raw_source();
        result["bypassed_after_error"] = frame_effect_bypass_due_to_error_;
        result["runtime"] = runtime_id_;

        String reason = "provider_not_built";
        bool available = false;
        if (frame_effect_provider_ != nullptr) {
            available = frame_effect_provider_->is_available(
                main_rendering_device(), &reason);
            result["provider"] = frame_effect_provider_->status();
        }
        result["available"] = available;
        result["reason"] = reason;
        return result;
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
        AndroidBridgeLog("event=set_engine_option key=%s value=%s result=%s error=%s",
                         key_utf8.get_data(), value_utf8.get_data(),
                         last_result_.utf8().get_data(),
                         last_error_.utf8().get_data());
        if (result == ENGINE_RESULT_OK &&
            key.strip_edges().to_lower() == "runtime") {
            runtime_id_ = value.strip_edges().to_lower();
            if (runtime_id_.is_empty()) {
                runtime_id_ = "auto";
            }
            // The iOS source-output policy depends on whether this player is
            // hosting KiriKiri or ONScripter. Refresh the pending frame option
            // after the runtime selection becomes known.
            sync_frame_effect_source_mode(true);
        }
        return result;
    }

    int submit_platform_response(const String &operation,
                                 const String &argument) {
        if (handle_ == nullptr) {
            last_result_ = "INVALID_STATE";
            last_error_ = "engine is not initialized";
            return ENGINE_RESULT_INVALID_STATE;
        }
        const CharString operation_utf8 = operation.utf8();
        const CharString argument_utf8 = argument.utf8();
        const engine_result_t result = engine_submit_platform_response(
            handle_, operation_utf8.get_data(), argument_utf8.get_data());
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

        AndroidBridgeLog("event=open_game_begin path=%s async=%d runtime=%s",
                         game_root_path.utf8().get_data(), async ? 1 : 0,
                         runtime_id_.utf8().get_data());

        CharString path_utf8 = game_root_path.utf8();
        const String normalized_runtime = runtime_id_.strip_edges().to_lower();
        artemis_logical_frame_pacing_ = normalized_runtime == "artemis" ||
            (normalized_runtime == "auto" &&
             engine_probe_runtime_provider("artemis", path_utf8.get_data()) > 0);
        reset_runtime_tick_timing(false);
        const engine_result_t result = async
            ? engine_open_game_async(handle_, path_utf8.get_data(), nullptr)
            : engine_open_game(handle_, path_utf8.get_data(), nullptr);
        game_open_ = result == ENGINE_RESULT_OK;
        if (!game_open_) {
            artemis_logical_frame_pacing_ = false;
        }
        update_last_error(result);
        AndroidBridgeLog("event=open_game_result result=%s error=%s game_open=%d",
                         last_result_.utf8().get_data(),
                         last_error_.utf8().get_data(), game_open_ ? 1 : 0);
        return result;
    }

    int tick(double delta_seconds) {
        if (handle_ == nullptr) {
            return ENGINE_RESULT_INVALID_STATE;
        }
        double runtime_delta_seconds = std::max(0.0, delta_seconds);
        if (artemis_logical_frame_pacing_) {
            const auto step = artemis_logical_frame_pacer_.Advance(
                runtime_delta_seconds);
            if (!step.should_tick) {
                drain_platform_requests();
                last_result_ = ResultToString(ENGINE_RESULT_OK);
                last_error_ = "";
                return ENGINE_RESULT_OK;
            }
            runtime_delta_seconds = step.delta_seconds;
        }
        const uint32_t delta_ms =
            runtime_tick_quantizer_.Quantize(runtime_delta_seconds);
        const engine_result_t result = engine_tick(handle_, delta_ms);
        update_runtime_message_reveal(runtime_delta_seconds);
        drain_platform_requests();
        update_runtime_message_layout();
        runtime_presentation_sprite_.Tick(get_viewport(), runtime_delta_seconds);
        update_runtime_save_slot_layout();
        update_runtime_audio_fades(runtime_delta_seconds);
        update_runtime_movie();
        update_last_error(result);
        return result;
    }

    int pause() {
        if (handle_ == nullptr) {
            return ENGINE_RESULT_INVALID_STATE;
        }
        const engine_result_t result = engine_pause(handle_);
        if (result == ENGINE_RESULT_OK) reset_runtime_tick_timing(false);
        update_last_error(result);
        return result;
    }

    int resume() {
        if (handle_ == nullptr) {
            return ENGINE_RESULT_INVALID_STATE;
        }
        const engine_result_t result = engine_resume(handle_);
        if (result == ENGINE_RESULT_OK) reset_runtime_tick_timing(false);
        update_last_error(result);
        return result;
    }

    bool media_open(const String &path) {
        media_close();
        if (handle_ == nullptr) {
            last_result_ = "INVALID_STATE";
            last_error_ = "engine is not initialized";
            return false;
        }
        const CharString path_utf8 = path.utf8();
        const engine_result_t result =
            engine_media_open(handle_, path_utf8.get_data(), &media_);
        update_last_error(result);
        return result == ENGINE_RESULT_OK;
    }

    void media_close() {
        if (media_ != nullptr) {
            engine_media_destroy(media_);
            media_ = nullptr;
        }
        media_texture_.unref();
        media_rgba_buffer_ = PackedByteArray();
        media_frame_serial_ = UINT64_MAX;
        media_width_ = 0;
        media_height_ = 0;
    }

    int media_play() {
        if (media_ == nullptr) return ENGINE_RESULT_INVALID_STATE;
        const engine_result_t result = engine_media_play(media_);
        update_last_error(result);
        return result;
    }

    int media_pause() {
        if (media_ == nullptr) return ENGINE_RESULT_INVALID_STATE;
        const engine_result_t result = engine_media_pause(media_);
        update_last_error(result);
        return result;
    }

    int media_seek(double position_seconds) {
        if (media_ == nullptr || !std::isfinite(position_seconds)) {
            return ENGINE_RESULT_INVALID_ARGUMENT;
        }
        const auto position_ms = static_cast<int64_t>(
            std::max(0.0, position_seconds) * 1000.0);
        const engine_result_t result = engine_media_seek(media_, position_ms);
        update_last_error(result);
        return result;
    }

    int media_set_rate(double playback_rate) {
        if (media_ == nullptr) return ENGINE_RESULT_INVALID_STATE;
        const engine_result_t result =
            engine_media_set_rate(media_, playback_rate);
        update_last_error(result);
        return result;
    }

    String media_get_subtitle_tracks_json() {
        if (media_ == nullptr) return "[]";
        std::vector<char> buffer(64 * 1024);
        uint32_t bytes_written = 0;
        const engine_result_t result =
            engine_media_get_subtitle_tracks_json(
                media_, buffer.data(),
                static_cast<uint32_t>(buffer.size()), &bytes_written);
        update_last_error(result);
        if (result != ENGINE_RESULT_OK || bytes_written == 0) {
            return "[]";
        }
        return String::utf8(buffer.data(), bytes_written);
    }

    bool media_extract_subtitle(int stream_index,
                                const String &output_path) {
        if (media_ == nullptr || stream_index < 0 ||
            output_path.is_empty()) {
            return false;
        }
        const CharString output_utf8 = output_path.utf8();
        const engine_result_t result = engine_media_extract_subtitle(
            media_, stream_index, output_utf8.get_data());
        update_last_error(result);
        return result == ENGINE_RESULT_OK;
    }

    Dictionary media_get_state() {
        Dictionary output;
        output["status"] = static_cast<int64_t>(ENGINE_MEDIA_STATUS_IDLE);
        output["position"] = 0.0;
        output["duration"] = 0.0;
        output["rate"] = 1.0;
        output["width"] = 0;
        output["height"] = 0;
        output["frame_serial"] = 0;
        output["frame_ready"] = false;
        output["seekable"] = false;
        output["has_audio"] = false;
        output["has_video"] = false;
        if (media_ == nullptr) return output;

        engine_media_state_t state{};
        state.struct_size = sizeof(state);
        const engine_result_t result = engine_media_get_state(media_, &state);
        update_last_error(result);
        if (result != ENGINE_RESULT_OK) return output;
        media_width_ = state.width;
        media_height_ = state.height;
        output["status"] = static_cast<int64_t>(state.status);
        output["position"] = static_cast<double>(state.position_ms) / 1000.0;
        output["duration"] = static_cast<double>(state.duration_ms) / 1000.0;
        output["rate"] = state.playback_rate;
        output["width"] = static_cast<int64_t>(state.width);
        output["height"] = static_cast<int64_t>(state.height);
        output["frame_serial"] = static_cast<int64_t>(state.frame_serial);
        output["frame_ready"] = state.frame_ready != 0;
        output["seekable"] = state.seekable != 0;
        output["has_audio"] = state.has_audio != 0;
        output["has_video"] = state.has_video != 0;
        return output;
    }

    Ref<Texture2D> media_update_texture() {
        if (media_ == nullptr || media_width_ == 0 || media_height_ == 0) {
            return media_texture_;
        }
        const size_t byte_count = static_cast<size_t>(media_width_) *
                                  static_cast<size_t>(media_height_) * 4u;
        if (media_rgba_buffer_.size() != static_cast<int64_t>(byte_count)) {
            media_rgba_buffer_.resize(static_cast<int64_t>(byte_count));
        }
        engine_frame_desc_t desc{};
        desc.struct_size = sizeof(desc);
        const engine_result_t result = engine_media_read_frame_rgba(
            media_, media_rgba_buffer_.ptrw(), byte_count, &desc);
        update_last_error(result);
        if (result != ENGINE_RESULT_OK || desc.width == 0 ||
            desc.height == 0) {
            return media_texture_;
        }
        if (media_texture_.is_valid() &&
            media_frame_serial_ == desc.frame_serial) {
            return media_texture_;
        }
        Ref<Image> image = Image::create_from_data(
            static_cast<int32_t>(desc.width),
            static_cast<int32_t>(desc.height), false, Image::FORMAT_RGBA8,
            media_rgba_buffer_);
        if (image.is_null()) return media_texture_;
        if (media_texture_.is_null() ||
            media_texture_->get_width() != static_cast<int32_t>(desc.width) ||
            media_texture_->get_height() != static_cast<int32_t>(desc.height)) {
            media_texture_ = ImageTexture::create_from_image(image);
        } else {
            media_texture_->update(image);
        }
        media_frame_serial_ = desc.frame_serial;
        return media_texture_;
    }

    int send_pointer_event(int type, int pointer_id, double x, double y,
                           double delta_x, double delta_y, int button,
                           int modifiers = 0) {
        if (handle_ == nullptr) {
            return ENGINE_RESULT_INVALID_STATE;
        }
        if (runtime_movie_active_ && runtime_movie_skippable_ &&
            type == ENGINE_INPUT_EVENT_POINTER_DOWN) {
            finish_runtime_movie("result=skipped");
            return ENGINE_RESULT_OK;
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
        event.modifiers = modifiers;
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
        engine_result_t result = engine_send_input(handle_, &event);
        // KAG edit controls receive printable characters through a distinct
        // text-input event; a key-down alone only handles navigation and
        // editing commands such as Backspace.
        if (result == ENGINE_RESULT_OK && pressed &&
            unicode_codepoint > 0) {
            event.type = ENGINE_INPUT_EVENT_TEXT_INPUT;
            result = engine_send_input(handle_, &event);
        }
        update_last_error(result);
        return result;
    }

    int send_text_input(const String& text) {
        if (handle_ == nullptr) {
            return ENGINE_RESULT_INVALID_STATE;
        }
        engine_result_t result = ENGINE_RESULT_OK;
        for (int64_t index = 0; index < text.length(); ++index) {
            const char32_t codepoint = text[index];
            if (codepoint == 0 || codepoint == U'\r' || codepoint == U'\n') {
                continue;
            }
            engine_input_event_t event{};
            event.struct_size = sizeof(event);
            event.type = ENGINE_INPUT_EVENT_TEXT_INPUT;
            event.unicode_codepoint = static_cast<uint32_t>(codepoint);
            result = engine_send_input(handle_, &event);
            if (result != ENGINE_RESULT_OK) {
                break;
            }
        }
        update_last_error(result);
        return result;
    }

    Dictionary get_text_input_state() {
        Dictionary state;
        state["available"] = false;
        state["ime_active"] = false;
        state["ime_mode"] = 0;
        state["attention_point_valid"] = false;
        state["attention_x"] = 0;
        state["attention_y"] = 0;
        state["text_available"] = false;
        state["text"] = String();
        state["selection_start"] = 0;
        state["selection_end"] = 0;
        if (handle_ == nullptr) {
            return state;
        }

        engine_text_input_state_t native_state{};
        native_state.struct_size = sizeof(native_state);
        const engine_result_t result =
            engine_get_text_input_state(handle_, &native_state);
        update_last_error(result);
        if (result != ENGINE_RESULT_OK) {
            return state;
        }

        state["available"] = true;
        state["ime_active"] = native_state.ime_active != 0;
        state["ime_mode"] = native_state.ime_mode;
        state["attention_point_valid"] =
            native_state.attention_point_valid != 0;
        state["attention_x"] = native_state.attention_x;
        state["attention_y"] = native_state.attention_y;
        state["text_available"] = native_state.text_available != 0;
        state["selection_start"] = native_state.selection_start;
        state["selection_end"] = native_state.selection_end;
        if (native_state.text_available != 0 &&
            native_state.text_utf8_bytes > 0) {
            std::vector<char> text_buffer(
                static_cast<size_t>(native_state.text_utf8_bytes) + 1u, '\0');
            uint32_t bytes_written = 0;
            const engine_result_t text_result = engine_copy_text_input_text(
                handle_, text_buffer.data(),
                static_cast<uint32_t>(text_buffer.size()), &bytes_written);
            update_last_error(text_result);
            if (text_result != ENGINE_RESULT_OK) {
                state["text_available"] = false;
                return state;
            }
            state["text"] = String::utf8(text_buffer.data(), bytes_written);
        }
        return state;
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

    int set_diagnostic_config(bool enabled, const String &session_id,
                              int64_t category_mask,
                              int slow_frame_threshold_ms = 20,
                              int max_events = 2000) {
        if (handle_ == nullptr) {
            return ENGINE_RESULT_INVALID_STATE;
        }
        const CharString session_utf8 = session_id.utf8();
        engine_diagnostic_config_t config{};
        config.struct_size = sizeof(config);
        config.enabled = enabled ? 1u : 0u;
        config.category_mask = static_cast<uint64_t>(
            std::max<int64_t>(0, category_mask));
        config.slow_frame_threshold_us = static_cast<uint32_t>(
            std::max(0, slow_frame_threshold_ms) * 1000);
        config.max_events = static_cast<uint32_t>(
            std::clamp(max_events, 64, 10000));
        config.host_monotonic_origin_us =
            Time::get_singleton()->get_ticks_usec();
        config.session_id_utf8 = session_utf8.get_data();
        const engine_result_t result =
            engine_set_diagnostic_config(handle_, &config);
        update_last_error(result);
        return result;
    }

    int64_t mark_diagnostic_event(const String &label) {
        if (handle_ == nullptr) {
            return -1;
        }
        const CharString label_utf8 = label.utf8();
        uint64_t sequence = 0;
        const engine_result_t result = engine_mark_diagnostic_event(
            handle_, label_utf8.get_data(), &sequence);
        update_last_error(result);
        return result == ENGINE_RESULT_OK ? static_cast<int64_t>(sequence) : -1;
    }

    String drain_diagnostic_events() {
        if (handle_ == nullptr) {
            return String();
        }
        std::vector<char> buffer(256 * 1024);
        uint32_t bytes_written = 0;
        const engine_result_t result = engine_drain_diagnostic_events(
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
        // Runtime providers are free to include detailed timing fields. Keep
        // this buffer comfortably above the current Artemis diagnostics so a
        // successful render path never appears as an empty renderer string.
        char buffer[8192] = {};
        const engine_result_t result =
            engine_get_renderer_info(handle_, buffer, sizeof(buffer));
        update_last_error(result);
        if (result != ENGINE_RESULT_OK) {
            return String();
        }
        RenderingServer *server = RenderingServer::get_singleton();
        String godot_info;
        if (server != nullptr) {
            godot_info = " godot_method=" + server->get_current_rendering_method() +
                         " godot_driver=" + server->get_current_rendering_driver_name() +
                         " rd_gpu=" + String(SupportsGodotRenderingDeviceGpu() ? "1" : "0");
        }
        godot_info += " source_query_ms=" + String::num(source_query_ms_, 6) +
                      " present_copy_ms=" + String::num(present_copy_ms_, 6);
        return String::utf8(buffer) + godot_info + GetGodotGpuBridgeDebugInfo();
    }

    Dictionary get_memory_stats() {
        Dictionary output;
        if (handle_ == nullptr) {
            return output;
        }
        engine_memory_stats_t stats{};
        stats.struct_size = sizeof(stats);
        const engine_result_t result = engine_get_memory_stats(handle_, &stats);
        update_last_error(result);
        if (result != ENGINE_RESULT_OK) {
            return output;
        }
        output["self_used_mb"] = static_cast<int64_t>(stats.self_used_mb);
        output["system_free_mb"] = static_cast<int64_t>(stats.system_free_mb);
        output["system_total_mb"] = static_cast<int64_t>(stats.system_total_mb);
        output["process_resident_bytes"] =
            static_cast<int64_t>(stats.process_resident_bytes);
        output["process_physical_footprint_bytes"] =
            static_cast<int64_t>(stats.process_physical_footprint_bytes);
        output["process_peak_physical_footprint_bytes"] =
            static_cast<int64_t>(stats.process_peak_physical_footprint_bytes);
        output["process_available_bytes"] =
            static_cast<int64_t>(stats.process_available_bytes);
        output["graphic_cache_bytes"] = static_cast<int64_t>(stats.graphic_cache_bytes);
        output["graphic_cache_limit_bytes"] = static_cast<int64_t>(stats.graphic_cache_limit_bytes);
        output["xp3_segment_cache_bytes"] = static_cast<int64_t>(stats.xp3_segment_cache_bytes);
        output["psb_cache_bytes"] = static_cast<int64_t>(stats.psb_cache_bytes);
        output["psb_cache_entries"] = static_cast<int64_t>(stats.psb_cache_entries);
        output["psb_cache_entry_limit"] = static_cast<int64_t>(stats.psb_cache_entry_limit);
        output["psb_cache_hits"] = static_cast<int64_t>(stats.psb_cache_hits);
        output["psb_cache_misses"] = static_cast<int64_t>(stats.psb_cache_misses);
        output["archive_cache_entries"] = static_cast<int64_t>(stats.archive_cache_entries);
        output["archive_cache_limit"] = static_cast<int64_t>(stats.archive_cache_limit);
        output["autopath_cache_entries"] = static_cast<int64_t>(stats.autopath_cache_entries);
        output["autopath_cache_limit"] = static_cast<int64_t>(stats.autopath_cache_limit);
        output["autopath_table_entries"] = static_cast<int64_t>(stats.autopath_table_entries);
        RenderingServer *server = RenderingServer::get_singleton();
        if (server != nullptr) {
            output["gpu_texture_bytes"] = static_cast<int64_t>(
                server->get_rendering_info(
                    RenderingServer::RENDERING_INFO_TEXTURE_MEM_USED));
            output["gpu_buffer_bytes"] = static_cast<int64_t>(
                server->get_rendering_info(
                    RenderingServer::RENDERING_INFO_BUFFER_MEM_USED));
            output["gpu_total_bytes"] = static_cast<int64_t>(
                server->get_rendering_info(
                    RenderingServer::RENDERING_INFO_VIDEO_MEM_USED));
        }
        return output;
    }

    String get_plugin_debug_info() {
        if (handle_ == nullptr) {
            return String();
        }
        std::vector<char> buffer(64 * 1024);
        uint32_t bytes_written = 0;
        const engine_result_t result = engine_get_plugin_debug_info(
            handle_, buffer.data(), static_cast<uint32_t>(buffer.size()),
            &bytes_written);
        update_last_error(result);
        if (result != ENGINE_RESULT_OK || bytes_written == 0) {
            return String();
        }
        return String::utf8(buffer.data(), bytes_written);
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
        // A setting can be applied before the RenderingDevice is ready. Retry
        // only that availability transition; a real processing failure sets
        // the bypass flag and remains on the safe surface path.
        if (frame_effect_enabled_ && !frame_effect_bypass_due_to_error_ &&
            !frame_effect_raw_source_output_) {
            sync_frame_effect_source_mode();
        }
        Ref<Texture2D> source = update_source_frame_texture();
        if (source.is_null()) {
            frame_effect_active_ = false;
            return source;
        }

        frame_source_width_ = static_cast<uint32_t>(std::max(0, source->get_width()));
        frame_source_height_ = static_cast<uint32_t>(std::max(0, source->get_height()));
        frame_effect_active_ = false;
        if (!frame_effect_enabled_ || frame_effect_provider_ == nullptr ||
            frame_source_width_ == 0 || frame_source_height_ == 0) {
            return source;
        }

        RenderingDevice *rd = main_rendering_device();
        String unavailable_reason;
        if (!frame_effect_provider_->is_available(rd, &unavailable_reason)) {
            frame_effect_error_ = unavailable_reason;
            frame_effect_bypass_due_to_error_ = true;
            sync_frame_effect_source_mode();
            return source;
        }

        RenderingServer *server = RenderingServer::get_singleton();
        if (server == nullptr) {
            frame_effect_error_ = "rendering_server_unavailable";
            frame_effect_bypass_due_to_error_ = true;
            sync_frame_effect_source_mode();
            return source;
        }
        const RID source_texture = server->texture_get_rd_texture(source->get_rid());
        if (!source_texture.is_valid()) {
            frame_effect_error_ = "source_texture_has_no_rendering_device_rid";
            frame_effect_bypass_due_to_error_ = true;
            sync_frame_effect_source_mode();
            return source;
        }

        FrameEffectRequest request;
        request.rendering_device = rd;
        request.source_texture = source_texture;
        request.input_width = frame_source_width_;
        request.input_height = frame_source_height_;
        request.target_width = frame_effect_target_width_ > 0
            ? frame_effect_target_width_
            : frame_source_width_;
        request.target_height = frame_effect_target_height_ > 0
            ? frame_effect_target_height_
            : frame_source_height_;
        request.frame_serial = frame_texture_serial_;
        request.mode = frame_effect_mode_;
        request.custom_chain = frame_effect_custom_chain_;

        FrameEffectOutput output;
        String error;
        if (!frame_effect_provider_->process(request, &output, &error) ||
            output.texture.is_null()) {
            frame_effect_error_ = error.is_empty()
                ? String("frame_effect_provider_failed")
                : error;
            frame_effect_bypass_due_to_error_ = true;
            sync_frame_effect_source_mode();
            return source;
        }

        frame_effect_active_ = true;
        frame_effect_pipeline_ = output.pipeline;
        frame_effect_error_ = "";
        return output.texture;
    }

    Ref<Texture2D> update_source_frame_texture() {
        if (handle_ == nullptr) {
            return Ref<Texture2D>();
        }
        if (runtime_movie_active_) {
            Ref<Texture2D> texture = media_update_texture();
            if (texture.is_valid()) {
                frame_texture_backend_ = "minori_movie";
                return texture;
            }
        }
        source_query_ms_ = 0.0;
        present_copy_ms_ = 0.0;
        const std::string normalized_backend = NormalizeBackend(backend_);
        if (normalized_backend == ENGINE_RENDERER_GODOT_NATIVE ||
            normalized_backend == ENGINE_RENDERER_GPU_BRIDGE) {
            uint64_t texture_id = 0;
            uint32_t width = 0;
            uint32_t height = 0;
            uint64_t serial = 0;
            const auto query_started = std::chrono::steady_clock::now();
            engine_result_t gpu_result = engine_get_godot_native_frame_texture(
                handle_, &texture_id, &width, &height, &serial);
            source_query_ms_ = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - query_started).count();
            if (gpu_result == ENGINE_RESULT_OK && texture_id != 0) {
                uint32_t presentation_state =
                    ENGINE_GODOT_PRESENTATION_STATE_NONE;
                if (engine_get_godot_presentation_state(
                        handle_, &presentation_state) == ENGINE_RESULT_OK &&
                    (presentation_state &
                     ENGINE_GODOT_PRESENTATION_STATE_RESET_HISTORY) != 0) {
                    // The provider replaced a GPU-composed scene with a CPU
                    // handoff without changing its logical frame serial.
                    // Discard the delayed slot so the deleted character is
                    // not presented once more before the new source.
                    release_presentation_textures(true);
                }
                if (normalized_backend == ENGINE_RENDERER_GPU_BRIDGE) {
                    const auto present_started = std::chrono::steady_clock::now();
                    Ref<Texture2D> presented_texture =
                        update_presented_bridge_texture(
                            texture_id, width, height, serial,
                            "godot_external_presented");
                    present_copy_ms_ = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - present_started).count();
                    if (presented_texture.is_valid()) {
                        frame_texture_.unref();
                        release_imported_texture();
                        return presented_texture;
                    }
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
                    if (DirectPresentGodotNativeFrameEnabled()) {
                        Ref<Texture2D> native_texture =
                            ResolveBridgeTexture(texture_id);
                        if (native_texture.is_valid()) {
                            release_imported_texture();
                            release_presentation_textures(true);
                            frame_texture_.unref();
                            frame_texture_serial_ = serial;
                            frame_texture_backend_ = "godot_native_gpu_direct";
                            return native_texture;
                        }
                    }
                    const auto present_started = std::chrono::steady_clock::now();
                    Ref<Texture2D> presented_texture =
                        update_presented_bridge_texture(
                            texture_id, width, height, serial,
                            "godot_native_gpu_presented");
                    present_copy_ms_ = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - present_started).count();
                    if (presented_texture.is_valid()) {
                        frame_texture_.unref();
                        release_imported_texture();
                        return presented_texture;
                    }
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

        const size_t size =
            static_cast<size_t>(desc.stride_bytes) * desc.height;
        // Reuse the readback storage across frames.  The engine overwrites
        // the complete buffer, so allocating a temporary PackedByteArray on
        // every tick only adds a large zero-fill and allocator hit before the
        // actual frame copy.  Keep this buffer private to the player and let
        // Image::create_from_data() consume it after the read completes.
        if (frame_rgba_buffer_.size() != static_cast<int64_t>(size)) {
            frame_rgba_buffer_.resize(static_cast<int64_t>(size));
        }
        result = engine_read_frame_rgba(handle_, frame_rgba_buffer_.ptrw(), size);
        update_last_error(result);
        if (result != ENGINE_RESULT_OK) {
            return Ref<Texture2D>();
        }
        ForceOpaqueAlpha(frame_rgba_buffer_, desc.stride_bytes, desc.width,
                         desc.height);

        const bool prefer_rd_texture =
            normalized_backend == ENGINE_RENDERER_GODOT_NATIVE ||
            normalized_backend == ENGINE_RENDERER_GPU_BRIDGE;
        if (prefer_rd_texture) {
            Ref<Texture2D> rd_texture = update_rd_texture(desc, frame_rgba_buffer_);
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
            frame_rgba_buffer_);
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

    Dictionary debug_frame_enhancement_self_test() {
        Dictionary result;
        result["built"] = frame_effect_provider_ != nullptr;
        if (frame_effect_provider_ == nullptr) {
            result["ok"] = false;
            result["error"] = "provider_not_built";
            return result;
        }

        RenderingDevice *rd = main_rendering_device();
        String unavailable_reason;
        if (!frame_effect_provider_->is_available(rd, &unavailable_reason)) {
            result["ok"] = false;
            result["error"] = unavailable_reason;
            return result;
        }

        constexpr uint32_t kWidth = 16;
        constexpr uint32_t kHeight = 16;
        PackedByteArray pixels;
        pixels.resize(kWidth * kHeight * 4u);
        uint8_t *bytes = pixels.ptrw();
        for (uint32_t y = 0; y < kHeight; ++y) {
            for (uint32_t x = 0; x < kWidth; ++x) {
                const size_t offset = (static_cast<size_t>(y) * kWidth + x) * 4u;
                const bool checker = ((x / 4u) + (y / 4u)) % 2u != 0u;
                bytes[offset + 0u] = checker ? 224u : static_cast<uint8_t>(x * 11u);
                bytes[offset + 1u] = checker ? 96u : static_cast<uint8_t>(y * 13u);
                bytes[offset + 2u] = checker ? 32u : 160u;
                bytes[offset + 3u] = 255u;
            }
        }

        Ref<RDTextureView> view;
        view.instantiate();
        TypedArray<PackedByteArray> initial_data;
        initial_data.push_back(pixels);
        RID source_rid = rd->texture_create(
            MakeRgbaTextureFormat(kWidth, kHeight), view, initial_data);
        if (!source_rid.is_valid()) {
            result["ok"] = false;
            result["error"] = "source_texture_allocation_failed";
            return result;
        }

        const bool previous_enabled = frame_effect_enabled_;
        frame_effect_provider_->set_enabled(true);
        const Dictionary initial_provider_status = frame_effect_provider_->status();
        const int64_t initial_processed_frames = static_cast<int64_t>(
            initial_provider_status.get("processed_frames", 0));
        const int64_t initial_cache_hits = static_cast<int64_t>(
            initial_provider_status.get("cache_hits", 0));
        const int64_t initial_restore_runs = static_cast<int64_t>(
            initial_provider_status.get("anime4k_restore_runs", 0));
        const int64_t initial_restore_passes = static_cast<int64_t>(
            initial_provider_status.get("anime4k_restore_pass_dispatches", 0));
        const int64_t initial_neural_runs = static_cast<int64_t>(
            initial_provider_status.get("neural_upscale_runs", 0));
        const int64_t initial_compiled_pipelines = static_cast<int64_t>(
            initial_provider_status.get("compiled_pipeline_count", 0));
        const int64_t initial_pipeline_attempts = static_cast<int64_t>(
            initial_provider_status.get("pipeline_compile_attempts", 0));
        const int64_t initial_texture_reuse_hits = static_cast<int64_t>(
            initial_provider_status.get("texture_reuse_hits", 0));
        const int64_t initial_uniform_set_cache_hits = static_cast<int64_t>(
            initial_provider_status.get("uniform_set_cache_hits", 0));
        FrameEffectRequest request;
        request.rendering_device = rd;
        request.source_texture = source_rid;
        request.input_width = kWidth;
        request.input_height = kHeight;
        FrameEffectOutput output;
        String error;
        Array pipelines;
        Array compiled_pipeline_counts;
        Array allocated_texture_counts;
        Array allocated_texture_bytes;
        Array allocated_uniform_set_counts;
        Array texture_layouts;
        Array chain_stage_orders;
        Array chain_dispatch_counts;
        Array chain_peak_widths;
        Array chain_peak_heights;
        bool ok = true;
        const std::array<String, 15> modes = {
            "anime4k", "fsr1", "bicubic", "lanczos",
            "ravu", "cunny", "nnedi3",
            "chain_4k_max", "chain_lossless", "chain_ultra",
            "chain_detail", "chain_balanced", "chain_soft",
            "chain_light", "chain_basic"};
        const std::array<uint32_t, 15> target_sizes = {
            24u, 22u, 23u, 26u, 31u, 30u, 29u,
            32u, 33u, 34u, 35u, 36u, 37u, 38u, 39u};
        for (size_t index = 0; index < modes.size(); ++index) {
            request.target_width = target_sizes[index];
            request.target_height = target_sizes[index];
            request.frame_serial = static_cast<uint64_t>(index + 1u);
            request.mode = modes[index];
            FrameEffectOutput pass_output;
            String pass_error;
            const bool pass_ok = frame_effect_provider_->process(
                request, &pass_output, &pass_error);
            ok = ok && pass_ok && pass_output.texture.is_valid() &&
                pass_output.width == request.target_width &&
                pass_output.height == request.target_height;
            if (!pass_error.is_empty()) error = pass_error;
            pipelines.push_back(pass_output.pipeline);
            const Dictionary pass_status = frame_effect_provider_->status();
            compiled_pipeline_counts.push_back(
                pass_status.get("compiled_pipeline_count", 0));
            allocated_texture_counts.push_back(
                pass_status.get("allocated_texture_count", 0));
            allocated_texture_bytes.push_back(
                pass_status.get("allocated_texture_bytes", 0));
            allocated_uniform_set_counts.push_back(
                pass_status.get("allocated_uniform_set_count", 0));
            texture_layouts.push_back(pass_status.get("texture_layout", ""));
            const Dictionary chain_status = pass_status.get("chain", Dictionary());
            chain_stage_orders.push_back(
                chain_status.get("stage_order", Array()));
            chain_dispatch_counts.push_back(
                chain_status.get("last_dispatch_count", 0));
            chain_peak_widths.push_back(
                chain_status.get("peak_internal_width", 0));
            chain_peak_heights.push_back(
                chain_status.get("peak_internal_height", 0));
            output = pass_output;
        }
        // A new serial with an unchanged source, mode and geometry must reuse
        // the texture graph and persistent uniform sets while still producing
        // a newly double-buffered output frame.
        request.frame_serial = static_cast<uint64_t>(modes.size() + 1u);
        FrameEffectOutput reused_output;
        String reused_error;
        const bool reuse_ok = frame_effect_provider_->process(
            request, &reused_output, &reused_error);
        ok = ok && reuse_ok && reused_output.texture.is_valid() &&
            reused_output.texture != output.texture;
        if (!reused_error.is_empty()) error = reused_error;
        output = reused_output;
        FrameEffectOutput cached_output;
        String cached_error;
        const bool cache_ok = frame_effect_provider_->process(
            request, &cached_output, &cached_error);
        ok = ok && cache_ok && cached_output.texture == output.texture;
        if (!cached_error.is_empty()) error = cached_error;

        const Dictionary provider_status = frame_effect_provider_->status();
        const int64_t processed_delta = static_cast<int64_t>(
            provider_status.get("processed_frames", 0)) - initial_processed_frames;
        const int64_t cache_hit_delta = static_cast<int64_t>(
            provider_status.get("cache_hits", 0)) - initial_cache_hits;
        const int64_t restore_run_delta = static_cast<int64_t>(
            provider_status.get("anime4k_restore_runs", 0)) - initial_restore_runs;
        const int64_t restore_pass_delta = static_cast<int64_t>(
            provider_status.get("anime4k_restore_pass_dispatches", 0)) -
            initial_restore_passes;
        const int64_t neural_run_delta = static_cast<int64_t>(
            provider_status.get("neural_upscale_runs", 0)) -
            initial_neural_runs;
        const int64_t compiled_pipeline_delta = static_cast<int64_t>(
            provider_status.get("compiled_pipeline_count", 0)) -
            initial_compiled_pipelines;
        const int64_t pipeline_attempt_delta = static_cast<int64_t>(
            provider_status.get("pipeline_compile_attempts", 0)) -
            initial_pipeline_attempts;
        const int64_t texture_reuse_delta = static_cast<int64_t>(
            provider_status.get("texture_reuse_hits", 0)) -
            initial_texture_reuse_hits;
        const int64_t uniform_set_cache_hit_delta = static_cast<int64_t>(
            provider_status.get("uniform_set_cache_hits", 0)) -
            initial_uniform_set_cache_hits;
        // The original recommended profile still runs one protected four-pass
        // restore. The eight new profiles use their separate ordered executor;
        // the final lightweight chain runs one additional serial to validate
        // graph/uniform reuse.
        ok = ok && processed_delta == 16 && cache_hit_delta >= 1 &&
            restore_run_delta == 1 && restore_pass_delta == 4 &&
            neural_run_delta == 3 && compiled_pipeline_delta == 95 &&
            pipeline_attempt_delta == 95 &&
            texture_reuse_delta >= 1 && uniform_set_cache_hit_delta >= 1;

        int64_t visible_pixels = 0;
        int64_t opaque_pixels = 0;
        RenderingServer *server = RenderingServer::get_singleton();
        RID output_rid;
        if (server != nullptr && output.texture.is_valid()) {
            output_rid = server->texture_get_rd_texture(output.texture->get_rid());
        }
        if (output_rid.is_valid()) {
            const PackedByteArray output_pixels = rd->texture_get_data(output_rid, 0);
            const int64_t expected_bytes =
                static_cast<int64_t>(output.width) * output.height * 4;
            ok = ok && output_pixels.size() == expected_bytes;
            for (int64_t offset = 0; offset + 3 < output_pixels.size(); offset += 4) {
                if (output_pixels[offset] > 8 || output_pixels[offset + 1] > 8 ||
                    output_pixels[offset + 2] > 8) {
                    ++visible_pixels;
                }
                if (output_pixels[offset + 3] == 255) ++opaque_pixels;
            }
            ok = ok && visible_pixels > 0 &&
                opaque_pixels == static_cast<int64_t>(output.width) * output.height;
        } else {
            ok = false;
        }

        // Exercise the exact public resolution targets on Metal with the
        // lightweight ordered chain. This validates real allocation,
        // dispatch, final RGBA8 conversion, and opaque output at 1080p, 2K,
        // and 4K without making the self-test run the intentionally extreme
        // VL supersample graph at full resolution.
        Array exact_resolution_sizes;
        Array exact_resolution_bytes;
        const std::array<Vector2i, 3> exact_targets = {
            Vector2i(1920, 1080), Vector2i(2560, 1440),
            Vector2i(3840, 2160)};
        request.mode = "chain_basic";
        for (size_t index = 0; index < exact_targets.size(); ++index) {
            request.target_width = static_cast<uint32_t>(exact_targets[index].x);
            request.target_height = static_cast<uint32_t>(exact_targets[index].y);
            request.frame_serial = static_cast<uint64_t>(100u + index);
            FrameEffectOutput exact_output;
            String exact_error;
            const bool exact_ok = frame_effect_provider_->process(
                request, &exact_output, &exact_error);
            if (!exact_error.is_empty()) error = exact_error;
            RID exact_rid;
            if (server != nullptr && exact_output.texture.is_valid()) {
                exact_rid = server->texture_get_rd_texture(
                    exact_output.texture->get_rid());
            }
            PackedByteArray exact_pixels;
            if (exact_rid.is_valid()) {
                exact_pixels = rd->texture_get_data(exact_rid, 0);
            }
            const int64_t exact_expected_bytes =
                static_cast<int64_t>(request.target_width) *
                request.target_height * 4;
            bool exact_opaque = exact_pixels.size() == exact_expected_bytes;
            if (exact_opaque && exact_pixels.size() >= 4) {
                const std::array<int64_t, 3> sample_offsets = {
                    3, (exact_pixels.size() / 8) * 4 + 3,
                    exact_pixels.size() - 1};
                for (int64_t sample_offset : sample_offsets) {
                    exact_opaque = exact_opaque &&
                        exact_pixels[sample_offset] == 255;
                }
            }
            ok = ok && exact_ok && exact_output.texture.is_valid() &&
                exact_output.width == request.target_width &&
                exact_output.height == request.target_height && exact_opaque;
            exact_resolution_sizes.push_back(
                String::num_int64(request.target_width) + String("x") +
                String::num_int64(request.target_height));
            exact_resolution_bytes.push_back(exact_pixels.size());
        }

        // Compile and execute every algorithm exposed by the custom-chain UI
        // as an independent graph. Keeping these tests small avoids combining
        // multiple fixed 2x stages into an impractically large texture while
        // still exercising their real Metal pipelines and size semantics.
        const std::array<String, 16> custom_algorithms = {
            "anime4k_upscale_s", "anime4k_upscale_l", "anime4k_upscale_vl",
            "anime4k_restore_s", "anime4k_restore_soft_s",
            "anime4k_restore_soft_m", "anime4k_restore_l",
            "anime4k_restore_vl", "fsr1_easu", "fsr1_rcas", "bicubic",
            "lanczos", "fxaa", "ravu_lite_r2", "cunny_2x4c",
            "nnedi3_nns16"};
        const std::array<String, 6> custom_double_algorithms = {
            "anime4k_upscale_s", "anime4k_upscale_l",
            "anime4k_upscale_vl", "ravu_lite_r2", "cunny_2x4c",
            "nnedi3_nns16"};
        const std::array<String, 3> custom_fit_algorithms = {
            "fsr1_easu", "bicubic", "lanczos"};
        Array custom_algorithm_names;
        Array custom_stage_orders;
        Array custom_dispatch_counts;
        request.mode = "custom";
        for (size_t index = 0; index < custom_algorithms.size(); ++index) {
            custom_algorithm_names.push_back(custom_algorithms[index]);
            request.custom_chain.clear();
            request.custom_chain.push_back(custom_algorithms[index]);
            const bool doubles = std::find(
                custom_double_algorithms.begin(), custom_double_algorithms.end(),
                custom_algorithms[index]) != custom_double_algorithms.end();
            const bool fits = std::find(
                custom_fit_algorithms.begin(), custom_fit_algorithms.end(),
                custom_algorithms[index]) != custom_fit_algorithms.end();
            request.target_width = doubles ? 32u : (fits ? 23u : 16u);
            request.target_height = request.target_width;
            request.frame_serial = static_cast<uint64_t>(200u + index);
            FrameEffectOutput custom_output;
            String custom_error;
            const bool custom_ok = frame_effect_provider_->process(
                request, &custom_output, &custom_error);
            if (!custom_error.is_empty()) error = custom_error;
            RID custom_rid;
            if (server != nullptr && custom_output.texture.is_valid()) {
                custom_rid = server->texture_get_rd_texture(
                    custom_output.texture->get_rid());
            }
            PackedByteArray custom_pixels;
            if (custom_rid.is_valid()) {
                custom_pixels = rd->texture_get_data(custom_rid, 0);
            }
            const int64_t custom_expected_bytes =
                static_cast<int64_t>(request.target_width) *
                request.target_height * 4;
            const bool custom_alpha_ok =
                custom_pixels.size() == custom_expected_bytes &&
                custom_pixels.size() >= 4 && custom_pixels[3] == 255 &&
                custom_pixels[custom_pixels.size() - 1] == 255;
            ok = ok && custom_ok && custom_output.texture.is_valid() &&
                custom_output.width == request.target_width &&
                custom_output.height == request.target_height &&
                custom_alpha_ok;
            const Dictionary custom_pass_status =
                frame_effect_provider_->status().get("chain", Dictionary());
            const Array custom_order =
                custom_pass_status.get("stage_order", Array());
            ok = ok && custom_order.size() == 1 &&
                custom_order[0] == custom_algorithms[index];
            custom_stage_orders.push_back(custom_order);
            custom_dispatch_counts.push_back(
                custom_pass_status.get("last_dispatch_count", 0));
        }

        request.custom_chain = PackedStringArray();
        request.custom_chain.push_back("anime4k_upscale_s");
        request.custom_chain.push_back("bicubic");
        request.custom_chain.push_back("anime4k_restore_soft_s");
        request.custom_chain.push_back("fsr1_rcas");
        request.target_width = 30u;
        request.target_height = 30u;
        request.frame_serial = 300u;
        FrameEffectOutput ordered_custom_output;
        String ordered_custom_error;
        const bool ordered_custom_ok = frame_effect_provider_->process(
            request, &ordered_custom_output, &ordered_custom_error);
        if (!ordered_custom_error.is_empty()) error = ordered_custom_error;
        Dictionary ordered_custom_status =
            frame_effect_provider_->status().get("chain", Dictionary());
        const Array ordered_custom_stages =
            ordered_custom_status.get("stage_order", Array());
        const Array expected_custom_stages = Array::make(
            "anime4k_upscale_s", "bicubic",
            "anime4k_restore_soft_s", "fsr1_rcas");
        ok = ok && ordered_custom_ok &&
            ordered_custom_output.texture.is_valid() &&
            ordered_custom_output.width == 30u &&
            ordered_custom_output.height == 30u &&
            ordered_custom_stages == expected_custom_stages;

        const int64_t custom_cache_hits_before = static_cast<int64_t>(
            ordered_custom_status.get("cache_hits", 0));
        const int64_t custom_reuse_hits_before = static_cast<int64_t>(
            ordered_custom_status.get("texture_reuse_hits", 0));
        FrameEffectOutput custom_cached_output;
        String custom_cached_error;
        const bool custom_cached_ok = frame_effect_provider_->process(
            request, &custom_cached_output, &custom_cached_error);
        request.frame_serial = 301u;
        FrameEffectOutput custom_reused_output;
        String custom_reused_error;
        const bool custom_reused_ok = frame_effect_provider_->process(
            request, &custom_reused_output, &custom_reused_error);
        if (!custom_cached_error.is_empty()) error = custom_cached_error;
        if (!custom_reused_error.is_empty()) error = custom_reused_error;
        const Dictionary custom_reused_status =
            frame_effect_provider_->status().get("chain", Dictionary());
        const bool custom_cache_verified = custom_cached_ok &&
            custom_reused_ok &&
            custom_cached_output.texture == ordered_custom_output.texture &&
            custom_reused_output.texture != ordered_custom_output.texture &&
            static_cast<int64_t>(custom_reused_status.get("cache_hits", 0)) ==
                custom_cache_hits_before + 1 &&
            static_cast<int64_t>(custom_reused_status.get(
                "texture_reuse_hits", 0)) == custom_reuse_hits_before + 1;
        ok = ok && custom_cache_verified;

        // An empty custom list is valid. It receives only the documented
        // implicit final fit when source and target dimensions differ.
        request.custom_chain.clear();
        request.target_width = 24u;
        request.target_height = 24u;
        request.frame_serial = 302u;
        FrameEffectOutput empty_custom_output;
        String empty_custom_error;
        const bool empty_custom_ok = frame_effect_provider_->process(
            request, &empty_custom_output, &empty_custom_error);
        if (!empty_custom_error.is_empty()) error = empty_custom_error;
        const Dictionary empty_custom_status =
            frame_effect_provider_->status().get("chain", Dictionary());
        const Array empty_custom_stages =
            empty_custom_status.get("stage_order", Array());
        ok = ok && empty_custom_ok && empty_custom_output.texture.is_valid() &&
            empty_custom_stages == Array::make("implicit_output_fit");

        request.custom_chain.push_back("not_an_algorithm");
        request.frame_serial = 303u;
        FrameEffectOutput invalid_custom_output;
        String invalid_custom_error;
        const bool invalid_custom_ok = frame_effect_provider_->process(
            request, &invalid_custom_output, &invalid_custom_error);
        ok = ok && !invalid_custom_ok &&
            invalid_custom_error.begins_with("unknown_custom_algorithm:");

        const Dictionary final_custom_provider_status =
            frame_effect_provider_->status();
        const int64_t custom_pipeline_delta = static_cast<int64_t>(
            final_custom_provider_status.get("compiled_pipeline_count", 0)) -
            static_cast<int64_t>(provider_status.get(
                "compiled_pipeline_count", 0));
        ok = ok && custom_pipeline_delta == 10;
        result["ok"] = ok;
        result["error"] = error;
        result["width"] = static_cast<int64_t>(output.width);
        result["height"] = static_cast<int64_t>(output.height);
        result["pipeline"] = output.pipeline;
        result["pipelines"] = pipelines;
        result["compiled_pipeline_counts"] = compiled_pipeline_counts;
        result["allocated_texture_counts"] = allocated_texture_counts;
        result["allocated_texture_bytes"] = allocated_texture_bytes;
        result["allocated_uniform_set_counts"] = allocated_uniform_set_counts;
        result["texture_layouts"] = texture_layouts;
        result["chain_stage_orders"] = chain_stage_orders;
        result["chain_dispatch_counts"] = chain_dispatch_counts;
        result["chain_peak_widths"] = chain_peak_widths;
        result["chain_peak_heights"] = chain_peak_heights;
        result["visible_pixels"] = visible_pixels;
        result["opaque_pixels"] = opaque_pixels;
        result["provider"] = provider_status;
        result["processed_delta"] = processed_delta;
        result["anime4k_restore_run_delta"] = restore_run_delta;
        result["anime4k_restore_pass_delta"] = restore_pass_delta;
        result["neural_upscale_run_delta"] = neural_run_delta;
        result["compiled_pipeline_delta"] = compiled_pipeline_delta;
        result["pipeline_compile_attempt_delta"] = pipeline_attempt_delta;
        result["texture_reuse_delta"] = texture_reuse_delta;
        result["uniform_set_cache_hit_delta"] = uniform_set_cache_hit_delta;
        result["exact_resolution_sizes"] = exact_resolution_sizes;
        result["exact_resolution_bytes"] = exact_resolution_bytes;
        result["custom_algorithms"] = custom_algorithm_names;
        result["custom_stage_orders"] = custom_stage_orders;
        result["custom_dispatch_counts"] = custom_dispatch_counts;
        result["ordered_custom_stages"] = ordered_custom_stages;
        result["empty_custom_stages"] = empty_custom_stages;
        result["invalid_custom_error"] = invalid_custom_error;
        result["custom_pipeline_delta"] = custom_pipeline_delta;
        result["custom_cache_verified"] = custom_cache_verified;

        frame_effect_provider_->release(rd);
        frame_effect_provider_->set_enabled(previous_enabled);
        rd->free_rid(source_rid);
        return result;
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
        const bool clear_after_alpha_d =
            mode != TVP_GODOT_GPU_BLEND_ALPHA_D ||
            BridgeClearRgba(src_texture, 0, &rect);
        std::vector<uint32_t> actual(kWidth * kHeight);
        const bool read = BridgeReadRgba(dst_texture, actual.data(),
                                         actual.size() * sizeof(uint32_t),
                                         kWidth * sizeof(uint32_t));
        bool cleared_source_is_zero = true;
        if (mode == TVP_GODOT_GPU_BLEND_ALPHA_D) {
            std::vector<uint32_t> cleared_source(kWidth * kHeight, 0xffffffffu);
            const bool read_cleared_source = BridgeReadRgba(
                src_texture, cleared_source.data(),
                cleared_source.size() * sizeof(uint32_t),
                kWidth * sizeof(uint32_t));
            cleared_source_is_zero = read_cleared_source &&
                std::all_of(cleared_source.begin(), cleared_source.end(),
                            [](uint32_t pixel) { return pixel == 0; });
        }
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

        result["ok"] = blended && clear_after_alpha_d && read &&
            cleared_source_is_zero && mismatches == 0;
        result["mode"] = mode_name;
        result["opacity"] = opacity;
        result["blended"] = blended;
        result["read"] = read;
        result["clear_after_alpha_d"] = clear_after_alpha_d;
        result["cleared_source_is_zero"] = cleared_source_is_zero;
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
            mode != TVP_GODOT_GPU_BLEND_CONST_ALPHA_SD_D &&
            mode != TVP_GODOT_GPU_BLEND_ALPHA_D_MASK_MULTIPLY &&
            mode != TVP_GODOT_GPU_BLEND_ALPHA_D_MASK_THRESHOLD) {
            result["ok"] = false;
            result["error"] = "unknown blend2 mode";
            return result;
        }

        constexpr uint32_t kWidth = 8;
        constexpr uint32_t kHeight = 8;
        std::vector<uint32_t> src1(kWidth * kHeight);
        std::vector<uint32_t> src2(kWidth * kHeight);
        std::vector<uint32_t> dst(kWidth * kHeight);
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
                const uint32_t ad = (37u + x * 27u + y * 15u) & 0xffu;
                const uint32_t rd = (x * 17u + y * 31u + 23u) & 0xffu;
                const uint32_t gd = (x * 41u + y * 11u + 29u) & 0xffu;
                const uint32_t bd = (x * 7u + y * 37u + 43u) & 0xffu;
                src1[i] = r1 | (g1 << 8) | (b1 << 16) | (a1 << 24);
                src2[i] = r2 | (g2 << 8) | (b2 << 16) | (a2 << 24);
                dst[i] = rd | (gd << 8) | (bd << 16) | (ad << 24);
                expected[i] = CpuBlend2Reference(
                    mode, dst[i], src1[i], src2[i], opacity);
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

    Dictionary debug_artemis_shader_self_test() {
        Dictionary result;
        const std::array<uint8_t, 8> foreground = {
            255, 0, 0, 255, 0, 255, 0, 255};
        const std::array<uint8_t, 8> custom_texture = {
            0, 0, 255, 255, 255, 255, 255, 255};
        const char *source = R"GLSL(
precision mediump float;
varying vec2 resultCoord1;
uniform sampler2D textureFore;
uniform sampler2D tintTexture;
uniform float alpha;
uniform vec3 colorMultiply;
uniform float weights[2];
void main() {
    vec4 foregroundColor = texture2D(textureFore, resultCoord1);
    vec4 tintColor = texture2D(tintTexture, resultCoord1);
    gl_FragColor = vec4(
        foregroundColor.rgb * colorMultiply * weights[0] +
            tintColor.rgb * weights[1],
        foregroundColor.a * alpha);
}
)GLSL";
        engine_runtime_shader_texture_v1_t texture{};
        texture.name_utf8 = "tintTexture";
        texture.image = {
            2, 1, 8, custom_texture.data(), custom_texture.size()};
        std::array<float, 2> weights = {0.5f, 0.25f};
        engine_runtime_shader_constant_v1_t constant{};
        constant.name_utf8 = "weights";
        constant.values = weights.data();
        constant.value_count = static_cast<uint32_t>(weights.size());
        std::array<uint8_t, 8> output{};
        engine_runtime_fragment_shader_request_v1_t request{};
        request.struct_size = sizeof(request);
        request.api_version = ENGINE_RUNTIME_FRAGMENT_SHADER_API_VERSION;
        request.shader_id_utf8 = "__aether_shader_self_test";
        request.fragment_source_utf8 = source;
        request.foreground = {
            2, 1, 8, foreground.data(), foreground.size()};
        request.alpha = 0.5f;
        request.color_multiply = 0x0080ffffu;
        request.textures = &texture;
        request.texture_count = 1;
        request.constants = &constant;
        request.constant_count = 1;
        request.output_pixels_rgba = output.data();
        request.output_pixels_size = output.size();
        std::array<char, 4096> error{};
        const engine_result_t first = ExecuteArtemisFragmentShader(
            nullptr, &request, error.data(),
            static_cast<uint32_t>(error.size()));
        const std::array<uint8_t, 8> first_expected = {
            64, 0, 64, 128, 64, 191, 64, 128};
        const bool first_pixels =
            first == ENGINE_RESULT_OK && output == first_expected;

        // Execute the same compiled program with different dynamic uniforms.
        // This catches accidental compile-time constant substitution in the
        // cache as well as uniform-buffer packing mistakes.
        weights = {0.0f, 1.0f};
        request.alpha = 0.25f;
        output.fill(0);
        error.fill(0);
        const engine_result_t second = ExecuteArtemisFragmentShader(
            nullptr, &request, error.data(),
            static_cast<uint32_t>(error.size()));
        const std::array<uint8_t, 8> second_expected = {
            0, 0, 255, 64, 255, 255, 255, 64};
        const bool second_pixels =
            second == ENGINE_RESULT_OK && output == second_expected;

        PackedByteArray pixels;
        pixels.resize(output.size());
        if (pixels.ptrw() != nullptr) {
            std::memcpy(pixels.ptrw(), output.data(), output.size());
        }
        result["ok"] = first_pixels && second_pixels;
        result["first_result"] = static_cast<int64_t>(first);
        result["first_pixels_ok"] = first_pixels;
        result["second_result"] = static_cast<int64_t>(second);
        result["second_pixels_ok"] = second_pixels;
        result["pixels"] = pixels;
        result["error"] = String::utf8(error.data());
        return result;
    }

    bool android_has_external_storage_permission() const {
#if defined(__ANDROID__)
        return AndroidHasExternalStoragePermission();
#else
        return true;
#endif
    }

    bool android_request_external_storage_permission() const {
#if defined(__ANDROID__)
        return AndroidRequestExternalStoragePermission();
#else
        return true;
#endif
    }

    bool iap_start(const String &product_id) const {
#if defined(__APPLE__)
        const CharString utf8 = product_id.utf8();
        return aether_storekit_start(utf8.get_data()) != 0;
#else
        (void)product_id;
        return false;
#endif
    }

    int64_t iap_refresh_entitlement(const String &product_id) const {
#if defined(__APPLE__)
        const CharString utf8 = product_id.utf8();
        return static_cast<int64_t>(
            aether_storekit_refresh_entitlement(utf8.get_data()));
#else
        (void)product_id;
        return 0;
#endif
    }

    int64_t iap_purchase(const String &product_id) const {
#if defined(__APPLE__)
        const CharString utf8 = product_id.utf8();
        return static_cast<int64_t>(
            aether_storekit_purchase(utf8.get_data()));
#else
        (void)product_id;
        return 0;
#endif
    }

    int64_t iap_restore(const String &product_id) const {
#if defined(__APPLE__)
        const CharString utf8 = product_id.utf8();
        return static_cast<int64_t>(
            aether_storekit_restore(utf8.get_data()));
#else
        (void)product_id;
        return 0;
#endif
    }

    String iap_get_state_json(const String &product_id) const {
#if defined(__APPLE__)
        const CharString utf8 = product_id.utf8();
        char *json = aether_storekit_copy_state_json_for_product(utf8.get_data());
        if (json == nullptr) {
            return "{\"available\":false,\"last_error\":\"StoreKit state unavailable\"}";
        }
        const String result = String::utf8(json);
        aether_storekit_free_string(json);
        return result;
#else
        (void)product_id;
        return "{\"available\":false,\"product_state\":\"unsupported\",\"entitled\":false}";
#endif
    }

    // The compiled-in unlock digest lives in AetherInternal. Public fallbacks
    // never report a match, so release artifacts without the package cannot
    // open the secret unlock path.
    bool verify_unlock_secret(const String &candidate) const {
#if defined(AETHERKIRI_INTERNAL_UNLOCK_GATE)
        const CharString candidate_utf8 = candidate.utf8();
        return AetherInternalVerifyUnlockSecret(candidate_utf8.get_data()) != 0;
#else
        (void)candidate;
        return false;
#endif
    }

    bool native_launch_file_picker_open(
            const String &title, const String &initial_directory) const {
#if defined(__APPLE__)
        const CharString title_utf8 = title.utf8();
        const CharString directory_utf8 = initial_directory.utf8();
        return aether_native_launch_file_picker_present(
                   title_utf8.get_data(), directory_utf8.get_data()) != 0;
#else
        (void)title;
        (void)initial_directory;
        return false;
#endif
    }

    bool native_cover_file_picker_open(
            const String &title,
            const String &initial_directory,
            const String &destination_directory) const {
#if defined(__APPLE__)
        const CharString title_utf8 = title.utf8();
        const CharString directory_utf8 = initial_directory.utf8();
        const CharString destination_utf8 = destination_directory.utf8();
        return aether_native_cover_file_picker_present(
                   title_utf8.get_data(), directory_utf8.get_data(),
                   destination_utf8.get_data()) != 0;
#else
        (void)title;
        (void)initial_directory;
        (void)destination_directory;
        return false;
#endif
    }

    bool native_translation_model_file_picker_open(
            const String &title, const String &initial_directory) const {
#if defined(__APPLE__)
        const CharString title_utf8 = title.utf8();
        const CharString directory_utf8 = initial_directory.utf8();
        return aether_native_translation_model_file_picker_present(
                   title_utf8.get_data(), directory_utf8.get_data()) != 0;
#else
        (void)title;
        (void)initial_directory;
        return false;
#endif
    }

    String native_translation_model_restore_path(
            const String &fallback_path) const {
#if defined(__APPLE__)
        const CharString fallback_utf8 = fallback_path.utf8();
        char *path = aether_native_translation_model_restore_path(
            fallback_utf8.get_data());
        if (path == nullptr) {
            return fallback_path;
        }
        const String result = String::utf8(path);
        aether_native_launch_file_picker_free_string(path);
        return result;
#else
        return fallback_path;
#endif
    }

    String native_launch_file_picker_take_result_json() const {
#if defined(__APPLE__)
        char *json = aether_native_launch_file_picker_copy_result_json();
        if (json == nullptr) {
            return "";
        }
        const String result = String::utf8(json);
        aether_native_launch_file_picker_free_string(json);
        return result;
#else
        return "";
#endif
    }

    int64_t probe_runtime(const String &runtime_id,
                          const String &game_root_path) const {
        const CharString runtime_utf8 = runtime_id.utf8();
        const CharString path_utf8 = game_root_path.utf8();
        return static_cast<int64_t>(engine_probe_runtime_provider(
            runtime_utf8.get_data(), path_utf8.get_data()));
    }

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("initialize_engine", "writable_path", "cache_path"),
                             &AetherRuntimePlayer::initialize_engine);
        ClassDB::bind_method(D_METHOD("destroy_engine"),
                             &AetherRuntimePlayer::destroy_engine);
        ClassDB::bind_method(D_METHOD("is_initialized"),
                             &AetherRuntimePlayer::is_initialized);
        ClassDB::bind_method(D_METHOD("is_game_open"),
                             &AetherRuntimePlayer::is_game_open);
        ClassDB::bind_method(D_METHOD("get_last_result"),
                             &AetherRuntimePlayer::get_last_result);
        ClassDB::bind_method(D_METHOD("get_last_error"),
                             &AetherRuntimePlayer::get_last_error);
        ClassDB::bind_method(D_METHOD("set_render_backend", "backend"),
                             &AetherRuntimePlayer::set_render_backend);
        ClassDB::bind_method(D_METHOD("get_render_backend"),
                             &AetherRuntimePlayer::get_render_backend);
        ClassDB::bind_method(D_METHOD("set_engine_option", "key", "value"),
                             &AetherRuntimePlayer::set_engine_option);
        ClassDB::bind_method(
            D_METHOD("submit_platform_response", "operation", "argument"),
            &AetherRuntimePlayer::submit_platform_response);
        ClassDB::bind_method(D_METHOD("set_surface_size", "width", "height"),
                             &AetherRuntimePlayer::set_surface_size);
        ClassDB::bind_method(D_METHOD("open_game", "game_root_path", "async"),
                             &AetherRuntimePlayer::open_game, DEFVAL(true));
        ClassDB::bind_method(D_METHOD("tick", "delta_seconds"),
                             &AetherRuntimePlayer::tick);
        ClassDB::bind_method(D_METHOD("pause"), &AetherRuntimePlayer::pause);
        ClassDB::bind_method(D_METHOD("resume"), &AetherRuntimePlayer::resume);
        ClassDB::bind_method(D_METHOD("media_open", "path"),
                             &AetherRuntimePlayer::media_open);
        ClassDB::bind_method(D_METHOD("media_close"),
                             &AetherRuntimePlayer::media_close);
        ClassDB::bind_method(D_METHOD("media_play"),
                             &AetherRuntimePlayer::media_play);
        ClassDB::bind_method(D_METHOD("media_pause"),
                             &AetherRuntimePlayer::media_pause);
        ClassDB::bind_method(D_METHOD("media_seek", "position_seconds"),
                             &AetherRuntimePlayer::media_seek);
        ClassDB::bind_method(D_METHOD("media_set_rate", "playback_rate"),
                             &AetherRuntimePlayer::media_set_rate);
        ClassDB::bind_method(D_METHOD("media_get_subtitle_tracks_json"),
                             &AetherRuntimePlayer::media_get_subtitle_tracks_json);
        ClassDB::bind_method(D_METHOD("media_extract_subtitle", "stream_index",
                                      "output_path"),
                             &AetherRuntimePlayer::media_extract_subtitle);
        ClassDB::bind_method(D_METHOD("media_get_state"),
                             &AetherRuntimePlayer::media_get_state);
        ClassDB::bind_method(D_METHOD("media_update_texture"),
                             &AetherRuntimePlayer::media_update_texture);
        ClassDB::bind_method(D_METHOD("send_pointer_event", "type", "pointer_id",
                                      "x", "y", "delta_x", "delta_y", "button",
                                      "modifiers"),
                             &AetherRuntimePlayer::send_pointer_event,
                             DEFVAL(0));
        ClassDB::bind_method(D_METHOD("send_key_event", "pressed", "key_code",
                                      "modifiers", "unicode_codepoint"),
                             &AetherRuntimePlayer::send_key_event);
        ClassDB::bind_method(D_METHOD("send_text_input", "text"),
                             &AetherRuntimePlayer::send_text_input);
        ClassDB::bind_method(D_METHOD("get_text_input_state"),
                             &AetherRuntimePlayer::get_text_input_state);
        ClassDB::bind_method(D_METHOD("get_startup_state"),
                             &AetherRuntimePlayer::get_startup_state);
        ClassDB::bind_method(D_METHOD("drain_startup_logs"),
                             &AetherRuntimePlayer::drain_startup_logs);
        ClassDB::bind_method(D_METHOD("set_diagnostic_config", "enabled",
                                      "session_id", "category_mask",
                                      "slow_frame_threshold_ms", "max_events"),
                             &AetherRuntimePlayer::set_diagnostic_config,
                             DEFVAL(20), DEFVAL(2000));
        ClassDB::bind_method(D_METHOD("mark_diagnostic_event", "label"),
                             &AetherRuntimePlayer::mark_diagnostic_event);
        ClassDB::bind_method(D_METHOD("drain_diagnostic_events"),
                             &AetherRuntimePlayer::drain_diagnostic_events);
        ClassDB::bind_method(D_METHOD("get_renderer_info"),
                             &AetherRuntimePlayer::get_renderer_info);
        ClassDB::bind_method(D_METHOD("get_memory_stats"),
                             &AetherRuntimePlayer::get_memory_stats);
        ClassDB::bind_method(D_METHOD("get_plugin_debug_info"),
                             &AetherRuntimePlayer::get_plugin_debug_info);
        ClassDB::bind_method(D_METHOD("get_frame_texture_backend"),
                             &AetherRuntimePlayer::get_frame_texture_backend);
        ClassDB::bind_method(D_METHOD("is_frame_enhancement_built"),
                             &AetherRuntimePlayer::is_frame_enhancement_built);
        ClassDB::bind_method(D_METHOD("is_text_translation_available"),
                             &AetherRuntimePlayer::is_text_translation_available);
        ClassDB::bind_method(D_METHOD("get_text_translation_state"),
                             &AetherRuntimePlayer::get_text_translation_state);
        ClassDB::bind_method(D_METHOD("get_text_translation_stats"),
                             &AetherRuntimePlayer::get_text_translation_stats);
        ClassDB::bind_method(D_METHOD("is_frame_enhancement_available"),
                             &AetherRuntimePlayer::is_frame_enhancement_available);
        ClassDB::bind_method(D_METHOD("set_frame_enhancement_enabled", "enabled"),
                             &AetherRuntimePlayer::set_frame_enhancement_enabled);
        ClassDB::bind_method(D_METHOD("set_frame_native_output_enabled", "enabled"),
                             &AetherRuntimePlayer::set_frame_native_output_enabled);
        ClassDB::bind_method(D_METHOD("set_frame_enhancement_mode", "mode"),
                             &AetherRuntimePlayer::set_frame_enhancement_mode);
        ClassDB::bind_method(D_METHOD("set_frame_enhancement_custom_chain", "chain"),
                             &AetherRuntimePlayer::set_frame_enhancement_custom_chain);
        ClassDB::bind_method(D_METHOD("set_frame_enhancement_target_size", "width", "height"),
                             &AetherRuntimePlayer::set_frame_enhancement_target_size);
        ClassDB::bind_method(D_METHOD("get_frame_source_size"),
                             &AetherRuntimePlayer::get_frame_source_size);
        ClassDB::bind_method(D_METHOD("get_frame_enhancement_status"),
                             &AetherRuntimePlayer::get_frame_enhancement_status);
        ClassDB::bind_method(D_METHOD("read_frame_rgba"),
                             &AetherRuntimePlayer::read_frame_rgba);
        ClassDB::bind_method(D_METHOD("update_frame_texture"),
                             &AetherRuntimePlayer::update_frame_texture);
        ClassDB::bind_method(D_METHOD("release_frame_texture"),
                             &AetherRuntimePlayer::release_frame_texture);
        ClassDB::bind_method(D_METHOD("debug_frame_enhancement_self_test"),
                             &AetherRuntimePlayer::debug_frame_enhancement_self_test);
        ClassDB::bind_method(D_METHOD("debug_gpu_blend_self_test", "mode", "opacity"),
                             &AetherRuntimePlayer::debug_gpu_blend_self_test,
                             DEFVAL(255));
        ClassDB::bind_method(D_METHOD("debug_gpu_blend2_self_test", "mode", "opacity"),
                             &AetherRuntimePlayer::debug_gpu_blend2_self_test,
                             DEFVAL(255));
        ClassDB::bind_method(D_METHOD("debug_artemis_shader_self_test"),
                             &AetherRuntimePlayer::debug_artemis_shader_self_test);
        ClassDB::bind_method(D_METHOD("android_has_external_storage_permission"),
                             &AetherRuntimePlayer::android_has_external_storage_permission);
        ClassDB::bind_method(D_METHOD("android_request_external_storage_permission"),
                             &AetherRuntimePlayer::android_request_external_storage_permission);
        ClassDB::bind_method(D_METHOD("iap_start", "product_id"),
                             &AetherRuntimePlayer::iap_start);
        ClassDB::bind_method(D_METHOD("iap_refresh_entitlement", "product_id"),
                             &AetherRuntimePlayer::iap_refresh_entitlement);
        ClassDB::bind_method(D_METHOD("iap_purchase", "product_id"),
                             &AetherRuntimePlayer::iap_purchase);
        ClassDB::bind_method(D_METHOD("iap_restore", "product_id"),
                             &AetherRuntimePlayer::iap_restore);
        ClassDB::bind_method(D_METHOD("iap_get_state_json", "product_id"),
                             &AetherRuntimePlayer::iap_get_state_json);
        ClassDB::bind_method(D_METHOD("verify_unlock_secret", "candidate"),
                             &AetherRuntimePlayer::verify_unlock_secret);
        ClassDB::bind_method(
            D_METHOD("native_launch_file_picker_open", "title", "initial_directory"),
            &AetherRuntimePlayer::native_launch_file_picker_open);
        ClassDB::bind_method(
            D_METHOD("native_cover_file_picker_open", "title", "initial_directory",
                     "destination_directory"),
            &AetherRuntimePlayer::native_cover_file_picker_open);
        ClassDB::bind_method(
            D_METHOD("native_translation_model_file_picker_open", "title",
                     "initial_directory"),
            &AetherRuntimePlayer::native_translation_model_file_picker_open);
        ClassDB::bind_method(
            D_METHOD("native_translation_model_restore_path", "fallback_path"),
            &AetherRuntimePlayer::native_translation_model_restore_path);
        ClassDB::bind_method(
            D_METHOD("native_launch_file_picker_take_result_json"),
            &AetherRuntimePlayer::native_launch_file_picker_take_result_json);
        ClassDB::bind_method(D_METHOD("probe_runtime", "runtime_id", "game_root_path"),
                             &AetherRuntimePlayer::probe_runtime);
        ADD_SIGNAL(MethodInfo(
            "platform_request",
            PropertyInfo(Variant::STRING, "operation"),
            PropertyInfo(Variant::STRING, "argument")));
    }

private:
    void drain_platform_requests() {
        std::array<char, 64> operation{};
        std::array<char, 8192> argument{};
        for (size_t count = 0; count < 256; ++count) {
            uint32_t available = 0;
            const engine_result_t result = engine_poll_platform_request(
                handle_, operation.data(), static_cast<uint32_t>(operation.size()),
                argument.data(), static_cast<uint32_t>(argument.size()),
                &available);
            if (result != ENGINE_RESULT_OK || available == 0) {
                return;
            }
            const String name = String::utf8(operation.data());
            const String value = String::utf8(argument.data());
            if (name == "open_browser") {
                OS::get_singleton()->shell_open(value);
            } else if (name == "call_native") {
                if (has_connections("platform_request")) {
                    emit_signal("platform_request", name, value);
                } else {
                    // Native CPlatform::CallNativeMethod returns an empty
                    // string when no platform module handles the request.
                    submit_platform_response(name, "result=");
                }
            } else if (name == "purchase") {
                if (has_connections("platform_request")) {
                    emit_signal("platform_request", name, value);
                } else {
                    // Desktop hosts and mobile shells without a billing
                    // adapter must complete the native wait deterministically.
                    submit_platform_response(
                        name,
                        "result=-1&title=&description=&price=&token="
                        "&error_response=-1&error_message="
                        "In%20App%20Billing%20is%20unavailable");
                }
            } else if (name == "text_input") {
                show_runtime_text_input(value);
            } else if (name == "presentation_sprite") {
                runtime_presentation_sprite_.Update(this, get_viewport(), value);
            } else if (name == "minori_save_slots") {
                update_runtime_save_slots(value);
            } else if (name == "minori_message") {
                if (value.is_empty()) {
                    clear_runtime_message();
                } else {
                    const PackedStringArray fields = value.split("\t", true, 3);
                    if (fields.size() == 4) {
                        update_runtime_message(
                            fields[0], fields[1], fields[2].to_int(), fields[3]);
                    } else {
                        clear_runtime_message();
                        UtilityFunctions::printerr("Malformed Minori message payload");
                    }
                }
            } else if (name == "minori_message_reveal") {
                reveal_runtime_message();
            } else if (name == "minori_audio") {
                const PackedStringArray fields = value.split("\t");
                if (fields.size() != 8 || !update_runtime_audio(fields)) {
                    submit_platform_response(
                        name, "result=error&message=audio_playback_failed");
                }
            } else if (name == "minori_movie") {
                const PackedStringArray fields = value.split("\t");
                if (fields.size() != 6 || !media_open(fields[0]) ||
                    media_play() != ENGINE_RESULT_OK) {
                    const String message = last_error_.replace("\n", " ");
                    submit_platform_response(
                        name, "result=error&message=" + message);
                    media_close();
                } else {
                    runtime_movie_active_ = true;
                    runtime_movie_skippable_ = fields[5] == "1";
                    update_runtime_movie();
                }
            } else {
                emit_signal("platform_request", name, value);
            }
        }
    }

    void show_runtime_text_input(const String &value) {
        const PackedStringArray fields = value.split("\t", true, 2);
        if (fields.size() != 3) {
            UtilityFunctions::printerr("Malformed text_input payload");
            submit_platform_response("text_input", "result=cancel");
            return;
        }
        finish_runtime_text_input(false);
        runtime_text_input_dialog_ = memnew(ConfirmationDialog);
        runtime_text_input_edit_ = memnew(LineEdit);
        runtime_text_input_dialog_->set_title(fields[0]);
        runtime_text_input_dialog_->set_text(fields[1]);
        runtime_text_input_dialog_->set_size(Vector2i(460, 190));
        runtime_text_input_edit_->set_text(fields[2]);
        runtime_text_input_edit_->set_custom_minimum_size(Vector2(0, 42));
        runtime_text_input_edit_->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
        runtime_text_input_dialog_->add_child(runtime_text_input_edit_);
        add_child(runtime_text_input_dialog_);
        runtime_text_input_dialog_->connect(
            "confirmed", callable_mp(this, &AetherRuntimePlayer::confirm_runtime_text_input));
        runtime_text_input_dialog_->connect(
            "canceled", callable_mp(this, &AetherRuntimePlayer::cancel_runtime_text_input));
        runtime_text_input_edit_->connect(
            "text_submitted",
            callable_mp(this, &AetherRuntimePlayer::submit_runtime_text_input));
        runtime_text_input_dialog_->popup_centered();
        runtime_text_input_edit_->grab_focus();
        runtime_text_input_edit_->select_all();
    }

    void confirm_runtime_text_input() { finish_runtime_text_input(true); }
    void cancel_runtime_text_input() { finish_runtime_text_input(false); }
    void submit_runtime_text_input(const String &) { finish_runtime_text_input(true); }

    void finish_runtime_text_input(bool accepted) {
        if (runtime_text_input_dialog_ == nullptr) return;
        String argument = "result=cancel";
        if (accepted && runtime_text_input_edit_ != nullptr) {
            argument = "result=ok\t" + runtime_text_input_edit_->get_text();
        }
        runtime_text_input_dialog_->queue_free();
        runtime_text_input_dialog_ = nullptr;
        runtime_text_input_edit_ = nullptr;
        submit_platform_response("text_input", argument);
    }

    void clear_runtime_save_slots() {
        for (Label *&label : runtime_save_slot_labels_) {
            if (label != nullptr) label->queue_free();
            label = nullptr;
        }
    }

    void update_runtime_save_slots(const String &value) {
        clear_runtime_save_slots();
        if (value.is_empty()) return;
        const PackedStringArray rows = value.split("\n", false);
        for (const String &row : rows) {
            const PackedStringArray fields = row.split("\t", true, 5);
            if (fields.size() != 6) continue;
            const int32_t slot = fields[0].to_int();
            if (slot < 0 || slot >= 10) continue;
            Label *label = create_runtime_message_label(
                "MinoriSaveSlot" + String::num_int64(slot + 1), true);
            label->set_text(
                "Slot " + String::num_int64(slot + 1) + "\n" + fields[5]);
            label->add_theme_color_override(
                "font_color", Color(0.28, 0.20, 0.24, 1.0));
            runtime_save_slot_positions_[slot] =
                Vector2(fields[1].to_float(), fields[2].to_float());
            runtime_save_slot_sizes_[slot] =
                Vector2(fields[3].to_float(), fields[4].to_float());
            runtime_save_slot_labels_[slot] = label;
        }
        update_runtime_save_slot_layout();
    }

    void update_runtime_save_slot_layout() {
        if (get_viewport() == nullptr) return;
        constexpr double kLogicalWidth = 1280.0;
        constexpr double kLogicalHeight = 720.0;
        const Vector2 viewport_size = get_viewport()->get_visible_rect().size;
        const double scale = std::min(
            static_cast<double>(viewport_size.x) / kLogicalWidth,
            static_cast<double>(viewport_size.y) / kLogicalHeight);
        const Vector2 offset(
            (viewport_size.x - kLogicalWidth * scale) * 0.5,
            (viewport_size.y - kLogicalHeight * scale) * 0.5);
        const int32_t font_size =
            std::max(1, static_cast<int32_t>(std::lround(18.0 * scale)));
        for (int32_t slot = 0; slot < 10; ++slot) {
            Label *label = runtime_save_slot_labels_[slot];
            if (label == nullptr) continue;
            const Vector2 logical_position = runtime_save_slot_positions_[slot];
            const Vector2 logical_size = runtime_save_slot_sizes_[slot];
            label->set_position(offset + logical_position * scale);
            label->set_size(logical_size * scale);
            label->add_theme_font_size_override("font_size", font_size);
        }
    }

    void clear_runtime_message() {
        runtime_message_reveal_active_ = false;
        runtime_message_reveal_started_ = false;
        runtime_message_reveal_elapsed_ms_ = 0.0;
        runtime_message_reveal_step_ = 0;
        runtime_message_reveal_total_steps_ = 0;
        runtime_message_reveal_schedule_.clear();
        if (runtime_message_speaker_label_ != nullptr) {
            runtime_message_speaker_label_->queue_free();
            runtime_message_speaker_label_ = nullptr;
        }
        if (runtime_message_label_ != nullptr) {
            runtime_message_label_->queue_free();
            runtime_message_label_ = nullptr;
        }
    }

    Label *create_runtime_message_label(const String &name, bool wrap) {
        Label *label = memnew(Label);
        label->set_name(name);
        label->set_z_index(100);
        if (wrap) {
            label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
        }
        label->set_vertical_alignment(VERTICAL_ALIGNMENT_TOP);
        label->add_theme_color_override(
            "font_color", Color(1.0, 1.0, 1.0, 1.0));
        const Ref<Font> font = ResourceLoader::get_singleton()->load(
            "res://assets/fonts/aetherkiri-runtime-cjk.otf");
        if (font.is_valid()) {
            label->add_theme_font_override("font", font);
        }
        add_child(label);
        return label;
    }

    void update_runtime_message(
        const String &speaker,
        const String &text,
        int32_t total_steps,
        const String &schedule
    ) {
        if (text.is_empty()) {
            clear_runtime_message();
            return;
        }
        if (runtime_message_label_ == nullptr) {
            runtime_message_label_ = create_runtime_message_label(
                "MinoriMessageText", true);
        }
        runtime_message_label_->set_text(text);
        runtime_message_reveal_schedule_.clear();
        const PackedStringArray schedule_fields = schedule.split(",", false);
        for (int32_t index = 0; index < schedule_fields.size(); ++index) {
            runtime_message_reveal_schedule_.push_back(static_cast<int32_t>(
                std::max<int64_t>(0, schedule_fields[index].to_int())));
        }
        runtime_message_reveal_total_steps_ = std::max(0, total_steps);
        runtime_message_reveal_step_ = 0;
        runtime_message_reveal_elapsed_ms_ = 0.0;
        runtime_message_reveal_started_ = false;
        runtime_message_reveal_active_ = runtime_message_reveal_total_steps_ > 0;
        runtime_message_label_->set_visible_characters(
            runtime_message_reveal_active_ ? 0 : -1);
        if (speaker.is_empty()) {
            if (runtime_message_speaker_label_ != nullptr) {
                runtime_message_speaker_label_->queue_free();
                runtime_message_speaker_label_ = nullptr;
            }
        } else {
            if (runtime_message_speaker_label_ == nullptr) {
                runtime_message_speaker_label_ = create_runtime_message_label(
                    "MinoriMessageSpeaker", false);
            }
            runtime_message_speaker_label_->set_text(speaker);
        }
        update_runtime_message_layout();
    }

    void reveal_runtime_message() {
        runtime_message_reveal_active_ = false;
        runtime_message_reveal_started_ = true;
        runtime_message_reveal_step_ = runtime_message_reveal_total_steps_;
        if (runtime_message_label_ != nullptr) {
            runtime_message_label_->set_visible_characters(-1);
        }
    }

    void update_runtime_message_reveal(double delta_seconds) {
        if (!runtime_message_reveal_active_ || runtime_message_label_ == nullptr) {
            return;
        }
        if (!runtime_message_reveal_started_) {
            runtime_message_reveal_started_ = true;
            runtime_message_reveal_step_ = 1;
        } else {
            runtime_message_reveal_elapsed_ms_ +=
                std::max(0.0, delta_seconds) * 1000.0;
            while (runtime_message_reveal_elapsed_ms_ >= 100.0 &&
                   runtime_message_reveal_step_ < runtime_message_reveal_total_steps_) {
                runtime_message_reveal_elapsed_ms_ -= 100.0;
                ++runtime_message_reveal_step_;
            }
        }
        int32_t visible_characters = 0;
        for (const int32_t step : runtime_message_reveal_schedule_) {
            if (step > runtime_message_reveal_step_) break;
            ++visible_characters;
        }
        runtime_message_label_->set_visible_characters(visible_characters);
        if (runtime_message_reveal_step_ >= runtime_message_reveal_total_steps_) {
            runtime_message_reveal_active_ = false;
            runtime_message_label_->set_visible_characters(-1);
        }
    }

    void update_runtime_message_layout() {
        if (runtime_message_label_ == nullptr || get_viewport() == nullptr) {
            return;
        }
        constexpr double kLogicalWidth = 1280.0;
        constexpr double kLogicalHeight = 720.0;
        const Vector2 viewport_size = get_viewport()->get_visible_rect().size;
        const double scale = std::min(
            static_cast<double>(viewport_size.x) / kLogicalWidth,
            static_cast<double>(viewport_size.y) / kLogicalHeight);
        const Vector2 offset(
            (viewport_size.x - kLogicalWidth * scale) * 0.5,
            (viewport_size.y - kLogicalHeight * scale) * 0.5);
        const int32_t font_size =
            std::max(1, static_cast<int32_t>(std::lround(26.0 * scale)));
        const bool has_speaker = runtime_message_speaker_label_ != nullptr;
        runtime_message_label_->set_position(
            offset + Vector2(120.0 * scale, (has_speaker ? 590.0 : 552.0) * scale));
        runtime_message_label_->set_size(Vector2(
            1040.0 * scale, (has_speaker ? 130.0 : 168.0) * scale));
        runtime_message_label_->add_theme_font_size_override("font_size", font_size);
        if (has_speaker) {
            runtime_message_speaker_label_->set_position(
                offset + Vector2(120.0 * scale, 552.0 * scale));
            runtime_message_speaker_label_->set_size(
                Vector2(1040.0 * scale, 38.0 * scale));
            runtime_message_speaker_label_->add_theme_font_size_override(
                "font_size", font_size);
        }
    }

    struct RuntimeAudioFade {
        AudioStreamPlayer *player = nullptr;
        double interval_ms = 0.0;
        double elapsed_ms = 0.0;
        int32_t level = 0;
        int32_t target = 0;
        int32_t step = 0;
        bool stop_after = false;
    };

    void finish_runtime_audio_fade(RuntimeAudioFade &fade) {
        if (fade.player != nullptr) {
            fade.player->set_volume_linear(static_cast<float>(fade.target) / 100.0f);
            if (fade.stop_after) {
                fade.player->stop();
                fade.player->queue_free();
            }
        }
        fade = RuntimeAudioFade{};
    }

    void update_runtime_audio_fades(double delta_seconds) {
        RuntimeAudioFade *fades[] = {&runtime_bgm_fade_in_, &runtime_bgm_fade_out_};
        for (RuntimeAudioFade *fade : fades) {
            if (fade->player == nullptr) continue;
            fade->elapsed_ms += delta_seconds * 1000.0;
            while (fade->elapsed_ms >= fade->interval_ms && fade->level != fade->target) {
                fade->elapsed_ms -= fade->interval_ms;
                fade->level += fade->step;
                if ((fade->step > 0 && fade->level > fade->target) ||
                    (fade->step < 0 && fade->level < fade->target)) {
                    fade->level = fade->target;
                }
                fade->player->set_volume_linear(
                    static_cast<float>(fade->level) / 100.0f);
            }
            if (fade->level == fade->target) finish_runtime_audio_fade(*fade);
        }
    }

    void fade_out_runtime_bgm(int32_t interval_ms) {
        if (runtime_bgm_fade_out_.player != nullptr) {
            runtime_bgm_fade_out_.player->stop();
            runtime_bgm_fade_out_.player->queue_free();
            runtime_bgm_fade_out_ = RuntimeAudioFade{};
        }
        AudioStreamPlayer *player = runtime_audio_players_[0];
        runtime_audio_players_[0] = nullptr;
        if (runtime_bgm_fade_in_.player == player) {
            runtime_bgm_fade_in_ = RuntimeAudioFade{};
        }
        if (player == nullptr) return;
        const int32_t level = std::clamp(
            static_cast<int32_t>(std::lround(player->get_volume_linear() * 100.0f)),
            0, 100);
        if (interval_ms < 1 || level == 0) {
            player->stop();
            player->queue_free();
            return;
        }
        runtime_bgm_fade_out_ =
            RuntimeAudioFade{player, static_cast<double>(interval_ms), 0.0,
                             level, 0, -1, true};
    }

    bool update_runtime_audio(const PackedStringArray &fields) {
        if (fields.size() != 8) return false;
        const int32_t slot = fields[0].to_int();
        if (slot < 0 || slot >= static_cast<int32_t>(runtime_audio_players_.size())) {
            return false;
        }
        const int32_t fade_in = fields[6].to_int();
        const int32_t fade_out = fields[7].to_int();
        if (slot == 0 && runtime_audio_players_[0] != nullptr &&
            runtime_audio_names_[0] == fields[5]) {
            AudioStreamPlayer *player = runtime_audio_players_[0];
            runtime_bgm_fade_in_ = RuntimeAudioFade{};
            const int32_t level = std::clamp(
                static_cast<int32_t>(std::lround(player->get_volume_linear() * 100.0f)),
                0, 100);
            const int32_t target = std::clamp(
                static_cast<int32_t>(fields[3].to_int()), 0, 100);
            if (fade_in < 1 || level == target) {
                player->set_volume_linear(static_cast<float>(target) / 100.0f);
            } else {
                runtime_bgm_fade_in_ = RuntimeAudioFade{
                    player, static_cast<double>(fade_in), 0.0, level, target,
                    target > level ? 1 : -1, false};
            }
            return true;
        }
        if (slot == 0) {
            fade_out_runtime_bgm(fade_out);
            runtime_audio_names_[0] = String();
            if (fields[1].is_empty()) return true;
        }
        AudioStreamPlayer *&player = runtime_audio_players_[slot];
        AudioServer *server = AudioServer::get_singleton();
        if (runtime_audio_buses_[slot].is_empty()) {
            if (server == nullptr) return false;
            const String bus_name =
                "MinoriAudio" + String::num_uint64(get_instance_id()) + "_" +
                String::num_int64(slot);
            server->add_bus();
            const int32_t bus_index = server->get_bus_count() - 1;
            server->set_bus_name(bus_index, bus_name);
            Ref<AudioEffectPanner> panner;
            panner.instantiate();
            server->add_bus_effect(bus_index, panner);
            runtime_audio_buses_[slot] = StringName(bus_name);
        }
        if (player == nullptr) {
            player = memnew(AudioStreamPlayer);
            add_child(player);
            player->set_bus(runtime_audio_buses_[slot]);
        }
        player->stop();
        if (fields[1].is_empty()) return true;
        Ref<AudioStreamOggVorbis> stream =
            AudioStreamOggVorbis::load_from_file(fields[1]);
        if (stream.is_null()) return false;
        stream->set_loop(fields[2] == "1");
        player->set_stream(stream);
        const int32_t target_volume = std::clamp(
            static_cast<int32_t>(fields[3].to_int()), 0, 100);
        const int32_t initial_volume = slot == 0 && fade_in >= 1 ? 0 : target_volume;
        player->set_volume_linear(static_cast<float>(initial_volume) / 100.0f);
        const int32_t bus_index = server->get_bus_index(runtime_audio_buses_[slot]);
        Ref<AudioEffectPanner> panner = server->get_bus_effect(bus_index, 0);
        if (panner.is_valid()) {
            panner->set_pan(static_cast<float>(fields[4].to_int()) / 100.0f);
        }
        player->play();
        runtime_audio_names_[slot] = fields[5];
        if (slot == 0 && initial_volume != target_volume) {
            runtime_bgm_fade_in_ = RuntimeAudioFade{
                player, static_cast<double>(fade_in), 0.0, initial_volume,
                target_volume, 1, false};
        }
        return true;
    }

    void finish_runtime_movie(const String &result) {
        if (!runtime_movie_active_) return;
        runtime_movie_active_ = false;
        runtime_movie_skippable_ = false;
        media_close();
        submit_platform_response("minori_movie", result);
    }

    void update_runtime_movie() {
        if (!runtime_movie_active_ || media_ == nullptr) return;
        engine_media_state_t state{};
        state.struct_size = sizeof(state);
        const engine_result_t result = engine_media_get_state(media_, &state);
        if (result != ENGINE_RESULT_OK ||
            state.status == ENGINE_MEDIA_STATUS_ERROR) {
            finish_runtime_movie("result=error&message=media_playback_failed");
            return;
        }
        media_width_ = state.width;
        media_height_ = state.height;
        if (state.status == ENGINE_MEDIA_STATUS_ENDED) {
            finish_runtime_movie("result=complete");
        }
    }

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

    void release_presentation_textures(bool free_rids) {
        for (size_t i = 0; i < frame_present_textures_.size(); ++i) {
            frame_present_textures_[i].unref();
            if (frame_present_rids_[i].is_valid()) {
                if (free_rids) {
                    auto op = std::make_shared<GodotGpuOp>();
                    op->type = GodotGpuOp::Type::Release;
                    op->dst = frame_present_rids_[i];
                    RunGodotGpuOpSync(op);
                }
                frame_present_rids_[i] = RID();
            }
        }
        frame_present_width_ = 0;
        frame_present_height_ = 0;
        frame_present_current_slot_ = 0;
        frame_present_pending_slot_ = 0;
        frame_present_has_current_ = false;
        frame_present_has_pending_ = false;
        // The copy op may still be queued on Godot's render thread. Keep the
        // shared_ptr alive until the queued operation reaches the release
        // fence below, then drop our reference. The queue owns its own
        // reference, so clearing this member is safe after RunGodotGpuOpSync
        // has ordered the releases behind all preceding work.
        frame_present_pending_op_.reset();
        frame_present_pending_draw_frame_ = 0;
        frame_present_serial_ = UINT64_MAX;
        if (frame_texture_backend_ == "godot_native_gpu_presented" ||
            frame_texture_backend_ == "godot_external_presented") {
            frame_texture_backend_ = "none";
        }
    }

    Ref<Texture2D> update_rd_texture(const engine_frame_desc_t &desc,
                                     const PackedByteArray &data) {
        RenderingDevice *rd = main_rendering_device();
        if (rd == nullptr || !SupportsGodotRenderingDeviceGpu()) {
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
                RenderingDevice::TEXTURE_USAGE_STORAGE_BIT |
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

    bool ensure_presentation_textures(uint32_t width, uint32_t height) {
        RenderingDevice *rd = main_rendering_device();
        if (rd == nullptr || !SupportsGodotRenderingDeviceGpu() ||
            width == 0 || height == 0) {
            return false;
        }

        const bool reusable =
            frame_present_width_ == width &&
            frame_present_height_ == height &&
            frame_present_rids_[0].is_valid() &&
            frame_present_rids_[1].is_valid() &&
            frame_present_textures_[0].is_valid() &&
            frame_present_textures_[1].is_valid();
        if (reusable) {
            return true;
        }

        release_presentation_textures(true);
        Ref<RDTextureFormat> format = MakeRgbaTextureFormat(width, height);
        Ref<RDTextureView> view;
        view.instantiate();
        TypedArray<PackedByteArray> initial_data;

        for (size_t i = 0; i < frame_present_rids_.size(); ++i) {
            frame_present_rids_[i] = rd->texture_create(format, view, initial_data);
            if (!frame_present_rids_[i].is_valid()) {
                release_presentation_textures(true);
                return false;
            }
            frame_present_textures_[i].instantiate();
            frame_present_textures_[i]->set_texture_rd_rid(frame_present_rids_[i]);
        }
        frame_present_width_ = width;
        frame_present_height_ = height;
        frame_present_current_slot_ = 0;
        frame_present_pending_slot_ = 0;
        frame_present_has_current_ = false;
        frame_present_has_pending_ = false;
        frame_present_pending_draw_frame_ = 0;
        frame_present_serial_ = UINT64_MAX;
        return true;
    }

    Ref<Texture2D> update_presented_bridge_texture(uint64_t texture_id,
                                                   uint32_t width,
                                                   uint32_t height,
                                                   uint64_t serial,
                                                   const char *backend_name) {
        if (texture_id == 0 || width == 0 || height == 0) {
            return Ref<Texture2D>();
        }
        GodotGpuTextureRecord source;
        if (!ResolveBridgeTextureRecord(texture_id, source) ||
            !source.rid.is_valid()) {
            return Ref<Texture2D>();
        }
        if (!ensure_presentation_textures(width, height)) {
            return Ref<Texture2D>();
        }
        // The texture contents can change while the engine serial remains the
        // same (for example, native idle animation). Always enqueue
        // the current GPU contents instead of treating the serial as a texture
        // revision. Promote only the copy encoded for the previous engine
        // frame. Godot submits its main RenderingDevice at the render-frame
        // boundary; a
        // texture copied and returned during the same process tick can still
        // contain its old transparent contents when Canvas samples it. This
        // one-frame pipeline publishes a slot only after it has crossed a real
        // RenderingServer submission, while the other slot receives the new
        // copy. It avoids both CPU readback and illegal main-device submit().
        Engine *engine = Engine::get_singleton();
        const uint64_t drawn_frame = engine != nullptr
            ? static_cast<uint64_t>(engine->get_frames_drawn())
            : 0;
        if (frame_present_has_pending_) {
            bool pending_done = false;
            bool pending_result = false;
            if (frame_present_pending_op_) {
                std::lock_guard<std::mutex> done_lock(
                    frame_present_pending_op_->done_mutex);
                pending_done = frame_present_pending_op_->done;
                pending_result = frame_present_pending_op_->result;
            }
            const bool pending_complete =
                drawn_frame > frame_present_pending_draw_frame_;
            if (pending_complete && pending_done && pending_result) {
                frame_present_current_slot_ = frame_present_pending_slot_;
                frame_present_has_current_ = true;
                frame_present_has_pending_ = false;
                frame_present_pending_op_.reset();
                // The RD storage stays stable while its pixels change. Notify
                // TextureRect/Canvas explicitly so the already-bound
                // Texture2DRD is invalidated without replacing the Resource
                // object that the scene owns.
                frame_present_textures_[frame_present_current_slot_]
                    ->emit_changed();
            } else if (pending_complete && pending_done) {
                // A failed asynchronous copy must not permanently block the
                // presentation ring. Drop the failed slot and enqueue a fresh
                // copy below; the current slot (if any) remains valid while
                // that retry is in flight.
                frame_present_has_pending_ = false;
                frame_present_pending_op_.reset();
            } else {
                frame_texture_serial_ = serial;
                frame_texture_backend_ = backend_name;
                if (frame_present_has_current_) {
                    return frame_present_textures_[frame_present_current_slot_];
                }
                return source.texture;
            }
        }
        const size_t next_slot = frame_present_has_current_
            ? 1u - frame_present_current_slot_
            : 0u;
        auto op = std::make_shared<GodotGpuOp>();
        op->type = GodotGpuOp::Type::Copy;
        op->src = source.rid;
        op->dst = frame_present_rids_[next_slot];
        op->src_pos = Vector3();
        op->dst_pos = Vector3();
        op->size = Vector3(width, height, 1);
        // Do not wait for the render thread here. This method runs from the
        // Godot main thread, while the copy is encoded on the RenderingServer
        // render thread. A synchronous wait couples a game-script frame to
        // the next render submission and turns a character/face texture burst
        // into a 100+ ms main-thread spike. The ring already delays promotion
        // until a later drawn frame; retaining the op lets us additionally
        // verify that the asynchronous copy really completed before exposing
        // the destination texture.
        if (!RunGodotGpuOpAsync(op)) {
            frame_texture_backend_ = "godot_native_gpu_present_timeout";
            if (frame_present_textures_[frame_present_current_slot_].is_valid()) {
                return frame_present_textures_[frame_present_current_slot_];
            }
            return Ref<Texture2D>();
        }
        frame_present_pending_slot_ = next_slot;
        frame_present_has_pending_ = true;
        frame_present_pending_op_ = std::move(op);
        frame_present_pending_draw_frame_ = drawn_frame;
        frame_present_serial_ = serial;
        frame_texture_serial_ = serial;
        frame_texture_backend_ = backend_name;
        if (frame_present_has_current_) {
            return frame_present_textures_[frame_present_current_slot_];
        }
        // The first queued slot has not crossed a Godot render boundary yet.
        // Keep the provider-owned texture for that single startup frame;
        // subsequent calls use only the completed presentation ring.
        return source.texture;
    }

    Ref<Texture2D> update_imported_gpu_bridge_texture(uint64_t texture_id,
                                                      uint32_t width,
                                                      uint32_t height) {
        RenderingDevice *rd = main_rendering_device();
        if (rd == nullptr || !SupportsGodotRenderingDeviceGpu() ||
            texture_id == 0 || width == 0 || height == 0) {
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

    void reset_runtime_tick_timing(bool clear_artemis_mode = true) {
        artemis_logical_frame_pacer_.Reset();
        runtime_tick_quantizer_.Reset();
        if (clear_artemis_mode) artemis_logical_frame_pacing_ = false;
    }

    void sync_frame_effect_source_mode(bool force = false) {
        bool publish_raw_source =
            frame_native_output_enabled_ || platform_prefers_raw_source();
        if (frame_effect_enabled_ && !frame_effect_bypass_due_to_error_ &&
            frame_effect_provider_ != nullptr) {
            String reason;
            const bool effect_available = frame_effect_provider_->is_available(
                main_rendering_device(), &reason);
            publish_raw_source = publish_raw_source || effect_available;
            if (!effect_available && frame_effect_error_.is_empty()) {
                frame_effect_error_ = reason;
            }
        }
        if (handle_ == nullptr ||
            (!force && publish_raw_source == frame_effect_raw_source_output_)) {
            frame_effect_raw_source_output_ = publish_raw_source;
            return;
        }

        engine_option_t option{};
        option.key_utf8 = ENGINE_OPTION_FRAME_OUTPUT;
        option.value_utf8 = publish_raw_source
            ? ENGINE_FRAME_OUTPUT_RAW_SOURCE
            : ENGINE_FRAME_OUTPUT_SURFACE;
        const engine_result_t result = engine_set_option(handle_, &option);
        if (result == ENGINE_RESULT_OK) {
            frame_effect_raw_source_output_ = publish_raw_source;
        } else {
            frame_effect_raw_source_output_ = false;
            frame_effect_error_ = LastError(handle_);
        }
    }

    bool platform_prefers_raw_source() const {
#if defined(IOS_ENABLED)
        // Prefer the configured high-resolution surface on iOS. Keep the
        // previous raw-source route as an explicit emergency fallback for
        // devices whose Metal bridge cannot sample the scaled surface.
        const char *raw_source = std::getenv("AETHERKIRI_IOS_RAW_SOURCE");
        return raw_source != nullptr && raw_source[0] != '\0' &&
               std::strcmp(raw_source, "0") != 0;
#else
        return false;
#endif
    }

    engine_handle_t handle_ = nullptr;
    engine_media_handle_t media_ = nullptr;
    bool game_open_ = false;
    String backend_ = "Godot Native";
    String last_result_;
    String last_error_;
    String frame_texture_backend_ = "none";
    double source_query_ms_ = 0.0;
    double present_copy_ms_ = 0.0;
    String runtime_id_ = "auto";
    aetherkiri::godot_host::ArtemisLogicalFramePacer
        artemis_logical_frame_pacer_;
    aetherkiri::godot_host::RuntimeTickMillisecondQuantizer
        runtime_tick_quantizer_;
    bool artemis_logical_frame_pacing_ = false;
    Ref<ImageTexture> frame_texture_;
    PackedByteArray frame_rgba_buffer_;
    Ref<Texture2DRD> frame_rd_texture_;
    RID frame_rd_rid_;
    uint32_t frame_rd_width_ = 0;
    uint32_t frame_rd_height_ = 0;
    Ref<Texture2DRD> frame_imported_texture_;
    RID frame_imported_rid_;
    uint64_t frame_imported_source_id_ = 0;
    uint32_t frame_imported_width_ = 0;
    uint32_t frame_imported_height_ = 0;
    std::array<Ref<Texture2DRD>, 2> frame_present_textures_;
    std::array<RID, 2> frame_present_rids_;
    uint32_t frame_present_width_ = 0;
    uint32_t frame_present_height_ = 0;
    size_t frame_present_current_slot_ = 0;
    size_t frame_present_pending_slot_ = 0;
    bool frame_present_has_current_ = false;
    bool frame_present_has_pending_ = false;
    std::shared_ptr<GodotGpuOp> frame_present_pending_op_;
    uint64_t frame_present_pending_draw_frame_ = 0;
    uint64_t frame_present_serial_ = UINT64_MAX;
    uint64_t frame_texture_serial_ = UINT64_MAX;
    std::unique_ptr<FrameEffectProvider> frame_effect_provider_;
    bool frame_effect_enabled_ = false;
    bool frame_effect_active_ = false;
    bool frame_native_output_enabled_ = false;
    bool frame_effect_raw_source_output_ = false;
    bool frame_effect_bypass_due_to_error_ = false;
    String frame_effect_mode_ = "auto";
    PackedStringArray frame_effect_custom_chain_;
    String frame_effect_pipeline_ = "none";
    String frame_effect_error_;
    uint32_t frame_effect_target_width_ = 0;
    uint32_t frame_effect_target_height_ = 0;
    uint32_t frame_source_width_ = 0;
    uint32_t frame_source_height_ = 0;
    Ref<ImageTexture> media_texture_;
    PackedByteArray media_rgba_buffer_;
    uint64_t media_frame_serial_ = UINT64_MAX;
    uint32_t media_width_ = 0;
    uint32_t media_height_ = 0;
    bool runtime_movie_active_ = false;
    bool runtime_movie_skippable_ = false;
    Label *runtime_message_speaker_label_ = nullptr;
    Label *runtime_message_label_ = nullptr;
    RuntimePresentationSprite runtime_presentation_sprite_;
    ConfirmationDialog *runtime_text_input_dialog_ = nullptr;
    LineEdit *runtime_text_input_edit_ = nullptr;
    std::array<Label *, 10> runtime_save_slot_labels_{};
    std::array<Vector2, 10> runtime_save_slot_positions_{};
    std::array<Vector2, 10> runtime_save_slot_sizes_{};
    bool runtime_message_reveal_active_ = false;
    bool runtime_message_reveal_started_ = false;
    double runtime_message_reveal_elapsed_ms_ = 0.0;
    int32_t runtime_message_reveal_step_ = 0;
    int32_t runtime_message_reveal_total_steps_ = 0;
    std::vector<int32_t> runtime_message_reveal_schedule_;
    std::array<AudioStreamPlayer *, 5> runtime_audio_players_{};
    std::array<StringName, 5> runtime_audio_buses_{};
    std::array<String, 5> runtime_audio_names_{};
    RuntimeAudioFade runtime_bgm_fade_in_{};
    RuntimeAudioFade runtime_bgm_fade_out_{};
};

void InitializeAetherRuntime(ModuleInitializationLevel level) {
    if (level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
#if defined(AETHERKIRI_INTERNAL_FRAME_EFFECTS)
    RegisterAetherInternalFrameEffects();
#endif
#if defined(AETHERKIRI_WITH_ONSCRIPTER)
    aetherkiri::onscripter::RegisterRuntimeProvider();
#endif
#if defined(AETHERKIRI_WITH_MINORI)
    if (aetherkiri_minori_register_runtime_provider() != ENGINE_RESULT_OK) {
        UtilityFunctions::printerr("Failed to register Minori runtime provider");
    }
#endif
    const engine_result_t shader_result =
        engine_set_runtime_fragment_shader_executor(
            ExecuteArtemisFragmentShader, nullptr);
    if (shader_result != ENGINE_RESULT_OK) {
        UtilityFunctions::printerr(
            "Failed to register Artemis fragment shader backend: ",
            ResultToString(shader_result));
    }
    ClassDB::register_class<AetherRuntimePlayer>();
}

void DeinitializeAetherRuntime(ModuleInitializationLevel level) {
    if (level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    engine_set_runtime_fragment_shader_executor(nullptr, nullptr);
    BridgeFlush();
    ReleaseRemainingGodotGpuTextures();
    ReleaseGodotGpuPipeline();
    engine_register_godot_gpu_batch_bridge(nullptr);
    engine_register_godot_gpu_external_texture_bridge(nullptr);
    engine_register_godot_gpu_bridge(nullptr);
#if defined(AETHERKIRI_INTERNAL_FRAME_EFFECTS)
    UnregisterAetherInternalFrameEffects();
#endif
}

} // namespace godot

extern "C" {

GDExtensionBool GDE_EXPORT aether_kiri_library_init(
    GDExtensionInterfaceGetProcAddress get_proc_address,
    GDExtensionClassLibraryPtr library,
    GDExtensionInitialization *initialization) {
    godot::GDExtensionBinding::InitObject init_obj(
        get_proc_address, library, initialization);
    init_obj.register_initializer(godot::InitializeAetherRuntime);
    init_obj.register_terminator(godot::DeinitializeAetherRuntime);
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
