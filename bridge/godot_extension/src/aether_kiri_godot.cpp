#include "engine_api.h"
#include "engine_options.h"
#include "engine_runtime_provider.h"
#include "GodotGpuBridge.h"
#include "ComplexRect.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/java_class.hpp>
#include <godot_cpp/classes/java_class_wrapper.hpp>
#include <godot_cpp/classes/java_object.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
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
#include <godot_cpp/variant/variant.hpp>

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
#include <unordered_map>
#include <vector>
#include <mutex>

#if defined(__APPLE__)
extern "C" {
int32_t aether_storekit_start(const char *product_id);
uint64_t aether_storekit_refresh_entitlement(const char *product_id);
uint64_t aether_storekit_purchase(const char *product_id);
uint64_t aether_storekit_restore(const char *product_id);
char *aether_storekit_copy_state_json();
void aether_storekit_free_string(char *value);
}
#endif

#if defined(__ANDROID__)
#include <jni.h>

extern JNIEnv* krkr_GetJNIEnv();
extern jobject krkr_GetApplicationContext();
#endif

namespace godot {
namespace {

struct GodotGpuTextureRecord {
    RID rid;
    Ref<Texture2DRD> texture;
    uint32_t width = 0;
    uint32_t height = 0;
    // AlphaBlend_d dispatches are asynchronous. A following scratch-layer
    // clear must not rewrite this RID until the queued shader has sampled it.
    bool requires_alpha_d_clear_version = false;
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
        Blend,
        Blend2,
        Blend3,
        ArtemisShader,
        Release,
        Flush,
    };

    Type type = Type::Update;
    RID src;
    RID src2;
    RID src3;
    RID dst;
    PackedByteArray data;
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
    bool result = false;
    bool done = false;
    std::mutex done_mutex;
    std::condition_variable done_cv;
};

std::mutex g_gpu_op_queue_mutex;
std::deque<std::shared_ptr<GodotGpuOp>> g_gpu_op_queue;
bool g_gpu_op_drain_scheduled = false;
std::atomic<uint64_t> g_gpu_op_submitted{0};
std::atomic<uint64_t> g_gpu_op_completed{0};
std::atomic<uint64_t> g_gpu_op_failed{0};
std::atomic<uint64_t> g_gpu_copy_failed{0};
std::atomic<uint64_t> g_gpu_copy_triangles_failed{0};
std::atomic<uint64_t> g_gpu_triangle_pipeline_failed{0};
std::atomic<uint64_t> g_gpu_triangle_buffer_failed{0};
std::atomic<uint64_t> g_gpu_triangle_uniform_failed{0};
std::atomic<uint64_t> g_gpu_blend_op_submitted{0};
std::atomic<uint64_t> g_gpu_queue_peak{0};
std::atomic<uint64_t> g_gpu_barriers{0};
std::atomic<uint64_t> g_gpu_alias_sources{0};
std::atomic<uint64_t> g_gpu_sync_timeouts{0};
std::atomic<uint64_t> g_gpu_alpha_d_clear_versions{0};

constexpr auto kGodotGpuSyncWaitTimeout = std::chrono::milliseconds(900);

#if defined(__ANDROID__)
constexpr int kAndroidFlagActivityNewTask = 0x10000000;
constexpr int kAndroidPermissionGranted = 0;

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
    AndroidClearJniException(env);
    env->DeleteLocalRef(permission_string);
    env->DeleteLocalRef(context_class);
    env->DeleteLocalRef(context);
    return ok;
}

bool AndroidHasExternalStoragePermission() {
    const int sdk = AndroidGetSdkInt();
    if (sdk > 0 && sdk < 23) {
        return true;
    }
    if (sdk > 0 && sdk < 30) {
        return AndroidHasRuntimePermission(
            "android.permission.READ_EXTERNAL_STORAGE");
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
    const int sdk = AndroidGetSdkInt();
    AK_ANDROID_LOGI("storage permission request sdk=%d", sdk);
    if (sdk > 0 && sdk < 30) {
        AK_ANDROID_LOGI("storage permission request falling back to runtime permissions");
        return false;
    }
    if (AndroidHasExternalStoragePermission()) {
        AK_ANDROID_LOGI("storage permission already granted");
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
    RID alpha_blend_a_shader;
    RID alpha_blend_a_pipeline;
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

Ref<RDTextureFormat> MakeRgbaTextureFormat(uint32_t width, uint32_t height);
bool ExecuteArtemisGpuShader(
    RenderingDevice *rd, const std::shared_ptr<GodotGpuOp> &op);

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
        return value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
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
    {
        std::lock_guard<std::mutex> lock(g_gpu_op_queue_mutex);
        queue_size = g_gpu_op_queue.size();
        scheduled = g_gpu_op_drain_scheduled;
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
        << " bridge_tri_pipeline_failed=" << g_gpu_triangle_pipeline_failed.load(std::memory_order_relaxed)
        << " bridge_tri_buffer_failed=" << g_gpu_triangle_buffer_failed.load(std::memory_order_relaxed)
        << " bridge_tri_uniform_failed=" << g_gpu_triangle_uniform_failed.load(std::memory_order_relaxed)
        << " bridge_blends=" << g_gpu_blend_op_submitted.load(std::memory_order_relaxed)
        << " bridge_barriers=" << g_gpu_barriers.load(std::memory_order_relaxed)
        << " bridge_alias_sources=" << g_gpu_alias_sources.load(std::memory_order_relaxed)
        << " bridge_alpha_d_clear_versions="
        << g_gpu_alpha_d_clear_versions.load(std::memory_order_relaxed)
        << " bridge_timeouts=" << g_gpu_sync_timeouts.load(std::memory_order_relaxed)
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
        return value != nullptr && value[0] != '\0' &&
               std::strcmp(value, "0") != 0;
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
        triangles ? static_cast<int32_t>(op.color) :
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

vec4 load_src(ivec2 local) {
    if (pc.color0.z != 1) {
        return imageLoad(src_img, pc.rect0.zw + local);
    }
    ivec2 src_extent = max(pc.color0.xy, ivec2(1));
    vec2 src_coord = vec2(pc.rect0.zw) +
        (vec2(local) + vec2(0.5)) * vec2(src_extent) /
            vec2(max(pc.rect1.xy, ivec2(1))) -
        vec2(0.5);
    ivec2 src_min = pc.rect0.zw;
    ivec2 src_max = src_min + src_extent - ivec2(1);
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
    vec4 premul = mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);
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
    uint opa = uint(clamp(pc.rect1.w, 0, 255));
    uint out_color = 0u;

    if (pc.rect1.z == 5) {
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
        } else if (pc.rect1.z == 8) {
        out_color = remove_const_opacity(d, opa);
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

vec4 load_src(ivec2 local) {
    if (pc.color0.z != 1) {
        return imageLoad(src_img, pc.rect0.zw + local);
    }
    ivec2 src_extent = max(pc.color0.xy, ivec2(1));
    vec2 src_coord = vec2(pc.rect0.zw) +
        (vec2(local) + vec2(0.5)) * vec2(src_extent) /
            vec2(max(pc.rect1.xy, ivec2(1))) -
        vec2(0.5);
    ivec2 src_min = pc.rect0.zw;
    ivec2 src_max = src_min + src_extent - ivec2(1);
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
    vec4 premul = mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);
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

vec4 load_bilinear(ivec2 limit, vec2 edge_coord) {
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
    if (premul.a > 0.00001) {
        premul.rgb /= premul.a;
    } else {
        premul.rgb = vec3(0.0);
    }
    return clamp(premul, vec4(0.0), vec4(1.0));
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
            out_color = load_bilinear(src_limit, src_pos_f);
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

vec4 load_bilinear(ivec2 limit, vec2 edge_coord) {
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
    int blend_flags = pc.color0.z;
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
            vec4 src = load_bilinear(src_limit, src_pos_f);
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
                if (src.g >= 0.70 && src.g > src.r + 0.20 &&
                    src.g > src.b + 0.20) {
                    src.a = 0.0;
                }
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

vec4 load_bilinear(ivec2 limit, vec2 edge_coord) {
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
    if (premul.a > 0.00001) {
        premul.rgb /= premul.a;
    } else {
        premul.rgb = vec3(0.0);
    }
    return clamp(premul, vec4(0.0), vec4(1.0));
}

float load_mask(vec2 edge_coord) {
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
    int blend_flags = pc.color0.z;
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
            float mask_val = 1.0 - load_mask(mask_pos_f);
            if (inverted_mask) {
                mask_val = 1.0 - mask_val;
            }
            if (mask_val <= 0.00001) {
                continue;
            }
            vec4 src = load_bilinear(src_limit, src_pos_f);
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

        // Uniform sets retain the buffer RID, so discard them before replacing
        // the shared buffer. Texture-backed sets are lazily rebuilt as needed.
        ClearGodotGpuUniformSetCache(rd);
        if (g_gpu_pipeline_state->triangle_vertex_buffer.is_valid()) {
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
    if (op->src == op->dst &&
        op->mode != TVP_GODOT_GPU_BLEND_FILL_ARGB &&
        op->mode != TVP_GODOT_GPU_BLEND_REMOVE_CONST_OPACITY &&
        op->mode != TVP_GODOT_GPU_BLEND_FILL_MASK) {
        g_gpu_alias_sources.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    std::vector<RID> uniform_sets;
    int64_t compute_list = rd->compute_list_begin();
    const bool ok = DispatchGodotGpuBlend(rd, op, compute_list, uniform_sets);
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
    if (prepared.temp_src.is_valid()) rd->free_rid(prepared.temp_src);
    if (prepared.owns_vertex_buffer && prepared.vertex_buffer.is_valid()) {
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
    bool result = false;
    bool wrote_texture = false;
    switch (op->type) {
        case GodotGpuOp::Type::Update:
            result = rd->texture_update(op->dst, 0, op->data) == OK;
            wrote_texture = true;
            break;
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
        case GodotGpuOp::Type::Release:
            ClearGodotGpuUniformSetCache(rd);
            rd->free_rid(op->dst);
            return true;
        case GodotGpuOp::Type::Flush:
            ApplyGodotGpuBarrier(rd);
            return true;
    }
    if (result && wrote_texture) {
        ApplyGodotGpuBarrier(rd);
    }
    return result;
}

void FinishGodotGpuOp(const std::shared_ptr<GodotGpuOp> &op, bool result) {
    CountGpuOpResult(result);
    if (!result && op != nullptr) {
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

void ExecuteGodotGpuComputeBatch(
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
    std::vector<GodotGpuPendingWrite> live2d_pending_writes;
    const int64_t compute_list = rd->compute_list_begin();
    for (size_t i = 0; i < ops.size(); ++i) {
        const bool live2d_triangle = IsLive2DTriangleOp(ops[i]);
        if (live2d_triangle && TriangleOpNeedsBarrierBeforeDispatch(
                                   *ops[i], live2d_pending_writes)) {
            rd->compute_list_add_barrier(compute_list);
            live2d_pending_writes.clear();
        } else if (!live2d_triangle && !live2d_pending_writes.empty()) {
            rd->compute_list_add_barrier(compute_list);
            live2d_pending_writes.clear();
        }
        if (IsBatchableTriangleOp(ops[i])) {
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
                // Keep the more conservative E-mote/TVP behavior: those paths
                // can expose different RIDs backed by aliased storage.
                rd->compute_list_add_barrier(compute_list);
            }
        }
    }
    if (!live2d_pending_writes.empty()) {
        rd->compute_list_add_barrier(compute_list);
    }
    rd->compute_list_end();
    if (any_dispatched) ApplyGodotGpuBarrier(rd);

    for (size_t i = 0; i < ops.size(); ++i) {
        FreeGodotGpuPreparedTriangles(rd, prepared[i]);
        FinishGodotGpuOp(ops[i], results[i]);
    }
}

void DrainGodotGpuOpsOnRenderThread() {
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
            if (g_gpu_op_queue.empty()) {
                g_gpu_op_drain_scheduled = false;
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

        // Alias blends are executed separately because sampling and writing the
        // same storage image in one dispatch is undefined on Metal/Vulkan.
        FinishGodotGpuOp(op, ExecuteGodotGpuOp(rd, op));
    }
    flush_compute();
}

bool RunGodotGpuOp(const std::shared_ptr<GodotGpuOp> &op, bool wait) {
    RenderingServer *server = RenderingServer::get_singleton();
    RenderingDevice *rd = MainRenderingDevice();
    if (op == nullptr) return false;
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
        if (DeferredGodotGpuDrainEnabled()) {
            {
                std::lock_guard<std::mutex> lock(g_gpu_op_queue_mutex);
                g_gpu_op_queue.push_back(op);
                UpdateGpuQueuePeak(g_gpu_op_queue.size());
            }
            if (!wait && op->type != GodotGpuOp::Type::Flush) {
                return true;
            }
            DrainGodotGpuOpsOnRenderThread();
            return wait ? op->result : true;
        }
        const bool result = ExecuteGodotGpuOp(rd, op);
        CountGpuOpResult(result);
        return result;
    }

    bool should_schedule = false;
    {
        std::lock_guard<std::mutex> lock(g_gpu_op_queue_mutex);
        g_gpu_op_queue.push_back(op);
        UpdateGpuQueuePeak(g_gpu_op_queue.size());
        if (ShouldScheduleGodotGpuDrainNow(op, wait) &&
            !g_gpu_op_drain_scheduled) {
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
    g_gpu_textures_created.fetch_add(1, std::memory_order_relaxed);
    g_gpu_texture_bytes_created.fetch_add(
        static_cast<uint64_t>(width) * height * 4u,
        std::memory_order_relaxed);
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
    g_gpu_textures_released.fetch_add(1, std::memory_order_relaxed);
    g_gpu_texture_bytes_released.fetch_add(
        static_cast<uint64_t>(record.width) * record.height * 4u,
        std::memory_order_relaxed);
    record.texture.unref();
    if (record.rid.is_valid()) {
        auto op = std::make_shared<GodotGpuOp>();
        op->type = GodotGpuOp::Type::Release;
        op->dst = record.rid;
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
    PackedByteArray data =
        PackRgbaBytes(pixels, record.width, record.height, stride_bytes);
    auto op = std::make_shared<GodotGpuOp>();
    op->type = GodotGpuOp::Type::Update;
    op->dst = record.rid;
    op->data = data;
    return RunGodotGpuOpAsync(op);
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
    if (full_transparent_clear && record.requires_alpha_d_clear_version) {
        RenderingDevice *rd = MainRenderingDevice();
        Ref<RDTextureView> view;
        view.instantiate();
        TypedArray<PackedByteArray> initial_data;
        initial_data.push_back(PackRgbaBytes(
            nullptr, record.width, record.height, record.width * 4u));
        RID replacement = rd != nullptr
            ? rd->texture_create(MakeRgbaTextureFormat(record.width,
                                                       record.height),
                                 view, initial_data)
            : RID();
        if (replacement.is_valid()) {
            bool replaced = false;
            {
                std::lock_guard<std::mutex> lock(g_gpu_textures_mutex);
                auto it = g_gpu_textures.find(texture);
                if (it != g_gpu_textures.end() &&
                    it->second.rid == record.rid &&
                    it->second.requires_alpha_d_clear_version) {
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
    op->src_pos = Vector3(0, 0, 0);
    op->size = Vector3(width, height, 1);
    op->src_size = Vector3(src_record.width, src_record.height, 1);
    op->mode = triangle_count;
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
    op->src_pos = Vector3(0, 0, 0);
    op->size = Vector3(width, height, 1);
    op->src_size = Vector3(src_record.width, src_record.height, 1);
    op->mode = triangle_count;
    op->opacity = static_cast<int>(std::round(std::clamp(opacity, 0.0f, 1.0f) * 255.0f));
    op->color = blend_mode;
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
    op->src_pos = Vector3(0, 0, 0);
    op->size = Vector3(width, height, 1);
    op->src_size = Vector3(src_record.width, src_record.height, 1);
    op->mode = triangle_count;
    op->opacity = static_cast<int>(
        std::round(std::clamp(opacity, 0.0f, 1.0f) * 255.0f));
    op->color = (blend_mode & 0xffffu) | (inverted_mask ? 0x10000u : 0u);
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
    return 0;
}

void ReleaseGodotGpuPipeline() {
    RenderingDevice *rd = MainRenderingDevice();
    if (rd != nullptr) {
        for (const auto &entry : g_artemis_shader_pipeline_cache) {
            if (entry.second.pipeline.is_valid()) {
                rd->free_rid(entry.second.pipeline);
            }
            if (entry.second.shader.is_valid()) {
                rd->free_rid(entry.second.shader);
            }
        }
    }
    g_artemis_shader_pipeline_cache.clear();
    if (g_gpu_pipeline_state == nullptr) return;
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
        callbacks.draw_triangles = BridgeDrawTriangles;
        callbacks.draw_masked_triangles = BridgeDrawMaskedTriangles;
        callbacks.mosaic_rects = BridgeMosaicRects;
        callbacks.blend_rect = BridgeBlendRect;
        callbacks.blend_rect2 = BridgeBlendRect2;
        callbacks.blend_rect3 = BridgeBlendRect3;
        callbacks.read_rgba = BridgeReadRgba;
        callbacks.flush = BridgeFlush;
        engine_register_godot_gpu_bridge(&callbacks);

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
        media_close();
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
    }

    void release_frame_texture() {
        release_rd_texture(true);
        release_presentation_textures(true);
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
        drain_platform_requests();
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
        PackedByteArray rgba;
        rgba.resize(static_cast<int64_t>(byte_count));
        engine_frame_desc_t desc{};
        desc.struct_size = sizeof(desc);
        const engine_result_t result = engine_media_read_frame_rgba(
            media_, rgba.ptrw(), byte_count, &desc);
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
            rgba);
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
        char buffer[512] = {};
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
                    Ref<Texture2D> presented_texture =
                        update_presented_bridge_texture(
                            texture_id, width, height, serial,
                            "godot_external_presented");
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
                    Ref<Texture2D> presented_texture =
                        update_presented_bridge_texture(
                            texture_id, width, height, serial,
                            "godot_native_gpu_presented");
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

    String iap_get_state_json() const {
#if defined(__APPLE__)
        char *json = aether_storekit_copy_state_json();
        if (json == nullptr) {
            return "{\"available\":false,\"last_error\":\"StoreKit state unavailable\"}";
        }
        const String result = String::utf8(json);
        aether_storekit_free_string(json);
        return result;
#else
        return "{\"available\":false,\"product_state\":\"unsupported\",\"entitled\":false}";
#endif
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
        ClassDB::bind_method(
            D_METHOD("submit_platform_response", "operation", "argument"),
            &AetherKiriPlayer::submit_platform_response);
        ClassDB::bind_method(D_METHOD("set_surface_size", "width", "height"),
                             &AetherKiriPlayer::set_surface_size);
        ClassDB::bind_method(D_METHOD("open_game", "game_root_path", "async"),
                             &AetherKiriPlayer::open_game, DEFVAL(true));
        ClassDB::bind_method(D_METHOD("tick", "delta_seconds"),
                             &AetherKiriPlayer::tick);
        ClassDB::bind_method(D_METHOD("pause"), &AetherKiriPlayer::pause);
        ClassDB::bind_method(D_METHOD("resume"), &AetherKiriPlayer::resume);
        ClassDB::bind_method(D_METHOD("media_open", "path"),
                             &AetherKiriPlayer::media_open);
        ClassDB::bind_method(D_METHOD("media_close"),
                             &AetherKiriPlayer::media_close);
        ClassDB::bind_method(D_METHOD("media_play"),
                             &AetherKiriPlayer::media_play);
        ClassDB::bind_method(D_METHOD("media_pause"),
                             &AetherKiriPlayer::media_pause);
        ClassDB::bind_method(D_METHOD("media_seek", "position_seconds"),
                             &AetherKiriPlayer::media_seek);
        ClassDB::bind_method(D_METHOD("media_set_rate", "playback_rate"),
                             &AetherKiriPlayer::media_set_rate);
        ClassDB::bind_method(D_METHOD("media_get_subtitle_tracks_json"),
                             &AetherKiriPlayer::media_get_subtitle_tracks_json);
        ClassDB::bind_method(D_METHOD("media_extract_subtitle", "stream_index",
                                      "output_path"),
                             &AetherKiriPlayer::media_extract_subtitle);
        ClassDB::bind_method(D_METHOD("media_get_state"),
                             &AetherKiriPlayer::media_get_state);
        ClassDB::bind_method(D_METHOD("media_update_texture"),
                             &AetherKiriPlayer::media_update_texture);
        ClassDB::bind_method(D_METHOD("send_pointer_event", "type", "pointer_id",
                                      "x", "y", "delta_x", "delta_y", "button",
                                      "modifiers"),
                             &AetherKiriPlayer::send_pointer_event,
                             DEFVAL(0));
        ClassDB::bind_method(D_METHOD("send_key_event", "pressed", "key_code",
                                      "modifiers", "unicode_codepoint"),
                             &AetherKiriPlayer::send_key_event);
        ClassDB::bind_method(D_METHOD("send_text_input", "text"),
                             &AetherKiriPlayer::send_text_input);
        ClassDB::bind_method(D_METHOD("get_text_input_state"),
                             &AetherKiriPlayer::get_text_input_state);
        ClassDB::bind_method(D_METHOD("get_startup_state"),
                             &AetherKiriPlayer::get_startup_state);
        ClassDB::bind_method(D_METHOD("drain_startup_logs"),
                             &AetherKiriPlayer::drain_startup_logs);
        ClassDB::bind_method(D_METHOD("set_diagnostic_config", "enabled",
                                      "session_id", "category_mask",
                                      "slow_frame_threshold_ms", "max_events"),
                             &AetherKiriPlayer::set_diagnostic_config,
                             DEFVAL(20), DEFVAL(2000));
        ClassDB::bind_method(D_METHOD("mark_diagnostic_event", "label"),
                             &AetherKiriPlayer::mark_diagnostic_event);
        ClassDB::bind_method(D_METHOD("drain_diagnostic_events"),
                             &AetherKiriPlayer::drain_diagnostic_events);
        ClassDB::bind_method(D_METHOD("get_renderer_info"),
                             &AetherKiriPlayer::get_renderer_info);
        ClassDB::bind_method(D_METHOD("get_memory_stats"),
                             &AetherKiriPlayer::get_memory_stats);
        ClassDB::bind_method(D_METHOD("get_plugin_debug_info"),
                             &AetherKiriPlayer::get_plugin_debug_info);
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
        ClassDB::bind_method(D_METHOD("debug_artemis_shader_self_test"),
                             &AetherKiriPlayer::debug_artemis_shader_self_test);
        ClassDB::bind_method(D_METHOD("android_has_external_storage_permission"),
                             &AetherKiriPlayer::android_has_external_storage_permission);
        ClassDB::bind_method(D_METHOD("android_request_external_storage_permission"),
                             &AetherKiriPlayer::android_request_external_storage_permission);
        ClassDB::bind_method(D_METHOD("iap_start", "product_id"),
                             &AetherKiriPlayer::iap_start);
        ClassDB::bind_method(D_METHOD("iap_refresh_entitlement", "product_id"),
                             &AetherKiriPlayer::iap_refresh_entitlement);
        ClassDB::bind_method(D_METHOD("iap_purchase", "product_id"),
                             &AetherKiriPlayer::iap_purchase);
        ClassDB::bind_method(D_METHOD("iap_restore", "product_id"),
                             &AetherKiriPlayer::iap_restore);
        ClassDB::bind_method(D_METHOD("iap_get_state_json"),
                             &AetherKiriPlayer::iap_get_state_json);
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
            } else {
                emit_signal("platform_request", name, value);
            }
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
        if (frame_present_serial_ == serial &&
            frame_present_width_ == width &&
            frame_present_height_ == height &&
            frame_present_textures_[frame_present_current_slot_].is_valid()) {
            frame_texture_serial_ = serial;
            frame_texture_backend_ = backend_name;
            return frame_present_textures_[frame_present_current_slot_];
        }

        GodotGpuTextureRecord source;
        if (!ResolveBridgeTextureRecord(texture_id, source) ||
            !source.rid.is_valid()) {
            return Ref<Texture2D>();
        }
        if (!ensure_presentation_textures(width, height)) {
            return Ref<Texture2D>();
        }

        const size_t next_slot = 1u - frame_present_current_slot_;
        auto op = std::make_shared<GodotGpuOp>();
        op->type = GodotGpuOp::Type::Copy;
        op->src = source.rid;
        op->dst = frame_present_rids_[next_slot];
        op->src_pos = Vector3();
        op->dst_pos = Vector3();
        op->size = Vector3(width, height, 1);
        if (!RunGodotGpuOpSync(op)) {
            frame_texture_backend_ = "godot_native_gpu_present_timeout";
            if (frame_present_textures_[frame_present_current_slot_].is_valid()) {
                return frame_present_textures_[frame_present_current_slot_];
            }
            return Ref<Texture2D>();
        }

        frame_present_current_slot_ = next_slot;
        frame_present_serial_ = serial;
        frame_texture_serial_ = serial;
        frame_texture_backend_ = backend_name;
        return frame_present_textures_[frame_present_current_slot_];
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

    engine_handle_t handle_ = nullptr;
    engine_media_handle_t media_ = nullptr;
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
    std::array<Ref<Texture2DRD>, 2> frame_present_textures_;
    std::array<RID, 2> frame_present_rids_;
    uint32_t frame_present_width_ = 0;
    uint32_t frame_present_height_ = 0;
    size_t frame_present_current_slot_ = 0;
    uint64_t frame_present_serial_ = UINT64_MAX;
    uint64_t frame_texture_serial_ = UINT64_MAX;
    Ref<ImageTexture> media_texture_;
    uint64_t media_frame_serial_ = UINT64_MAX;
    uint32_t media_width_ = 0;
    uint32_t media_height_ = 0;
};

void InitializeAetherKiri(ModuleInitializationLevel level) {
    if (level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    const engine_result_t shader_result =
        engine_set_runtime_fragment_shader_executor(
            ExecuteArtemisFragmentShader, nullptr);
    if (shader_result != ENGINE_RESULT_OK) {
        UtilityFunctions::printerr(
            "Failed to register Artemis fragment shader backend: ",
            ResultToString(shader_result));
    }
    ClassDB::register_class<AetherKiriPlayer>();
}

void DeinitializeAetherKiri(ModuleInitializationLevel level) {
    if (level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    engine_set_runtime_fragment_shader_executor(nullptr, nullptr);
    BridgeFlush();
    ReleaseRemainingGodotGpuTextures();
    ReleaseGodotGpuPipeline();
    engine_register_godot_gpu_bridge(nullptr);
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
