/**
 * @file android_jni_bridge.cpp
 * @brief Android JNI entry point for the Godot-hosted engine bridge.
 *
 * engine_api.so is loaded by the Godot Android app. This file provides
 * Android-specific JNI initialization needed
 * by the engine runtime, including JavaVM storage for JNI calls
 * from native threads and the SurfaceTexture bridge.
 */

#include <jni.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <mutex>
#include "environ/android/KrkrJniHelper.h"

#define LOG_TAG "krkr2"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ---------------------------------------------------------------------------
// JavaVM global storage
// ---------------------------------------------------------------------------

static JavaVM* g_javaVM = nullptr;
static std::mutex g_jvm_mutex;

JavaVM* krkr_GetJavaVM() {
    std::lock_guard<std::mutex> lock(g_jvm_mutex);
    return g_javaVM;
}

JNIEnv* krkr_GetJNIEnv() {
    JavaVM* vm = krkr_GetJavaVM();
    if (!vm) return nullptr;

    JNIEnv* env = nullptr;
    jint status = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            LOGE("Failed to attach current thread to JVM");
            return nullptr;
        }
    }
    return env;
}

// ---------------------------------------------------------------------------
// ANativeWindow global storage for SurfaceTexture bridge
// ---------------------------------------------------------------------------

static ANativeWindow* g_native_window = nullptr;
static uint32_t g_surface_width = 0;
static uint32_t g_surface_height = 0;
static std::mutex g_surface_mutex;

// ---------------------------------------------------------------------------
// Global Application Context
// The Godot Android host loads engine_api.so directly; the bridge can receive
// an Application Context via JNI so engine code can call Context methods like
// getExternalFilesDirs, getFilesDir, etc.
// ---------------------------------------------------------------------------

static jobject g_app_context = nullptr;  // global ref
static std::mutex g_context_mutex;

jobject krkr_GetApplicationContext() {
    std::lock_guard<std::mutex> lock(g_context_mutex);
    return g_app_context;
}

ANativeWindow* krkr_GetNativeWindow() {
    std::lock_guard<std::mutex> lock(g_surface_mutex);
    if (g_native_window) {
        ANativeWindow_acquire(g_native_window);
    }
    return g_native_window;
}

void krkr_GetSurfaceDimensions(uint32_t* out_width, uint32_t* out_height) {
    std::lock_guard<std::mutex> lock(g_surface_mutex);
    if (out_width) *out_width = g_surface_width;
    if (out_height) *out_height = g_surface_height;
}

// ---------------------------------------------------------------------------
// JNI_OnLoad
// ---------------------------------------------------------------------------

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    (void)reserved;

    {
        std::lock_guard<std::mutex> lock(g_jvm_mutex);
        g_javaVM = vm;
    }

    // Also set JavaVM for the KrkrJniHelper (used by AndroidUtils.cpp)
    krkr::JniHelper::setJavaVM(vm);

    LOGI("krkr2 JNI_OnLoad: JavaVM stored");
    return JNI_VERSION_1_6;
}

// ---------------------------------------------------------------------------
// JNI bridge: Android host -> C++ engine
// Sets the Android Surface (from SurfaceTexture) as the render target.
// ---------------------------------------------------------------------------

extern "C" JNIEXPORT void JNICALL
Java_org_github_krkr2_aetherkiri_EngineBridge_nativeSetSurface(
    JNIEnv* env, jobject /* thiz */, jobject surface, jint width, jint height) {

    std::lock_guard<std::mutex> lock(g_surface_mutex);

    // Release previous window if any
    if (g_native_window) {
        ANativeWindow_release(g_native_window);
        g_native_window = nullptr;
        g_surface_width = 0;
        g_surface_height = 0;
    }

    if (surface) {
        g_native_window = ANativeWindow_fromSurface(env, surface);
        if (g_native_window) {
            g_surface_width = static_cast<uint32_t>(width);
            g_surface_height = static_cast<uint32_t>(height);
            LOGI("nativeSetSurface: ANativeWindow acquired (%dx%d)", width, height);
        } else {
            LOGE("nativeSetSurface: ANativeWindow_fromSurface failed");
        }
    } else {
        LOGI("nativeSetSurface: Surface detached (null)");
    }
}

extern "C" JNIEXPORT void JNICALL
Java_org_github_krkr2_aetherkiri_EngineBridge_nativeDetachSurface(
    JNIEnv* /* env */, jobject /* thiz */) {

    std::lock_guard<std::mutex> lock(g_surface_mutex);
    if (g_native_window) {
        ANativeWindow_release(g_native_window);
        g_native_window = nullptr;
        g_surface_width = 0;
        g_surface_height = 0;
        LOGI("nativeDetachSurface: ANativeWindow released");
    }
}

// ---------------------------------------------------------------------------
// JNI bridge: Android host -> C++ engine
// Passes the Android Application Context for use by engine code that
// needs a Context (storage paths, font enumeration, etc.)
// ---------------------------------------------------------------------------

extern "C" JNIEXPORT void JNICALL
Java_org_github_krkr2_aetherkiri_EngineBridge_nativeSetApplicationContext(
    JNIEnv* env, jobject /* thiz */, jobject context) {

    std::lock_guard<std::mutex> lock(g_context_mutex);

    // Release previous global ref if any
    if (g_app_context) {
        env->DeleteGlobalRef(g_app_context);
        g_app_context = nullptr;
    }

    if (context) {
        g_app_context = env->NewGlobalRef(context);
        LOGI("nativeSetApplicationContext: Application Context stored");
    } else {
        LOGW("nativeSetApplicationContext: null context passed");
    }
}
