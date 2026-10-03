#include "renpy_mobile_adapter.h"

#if defined(__ANDROID__)
#include <jni.h>
#include <android/native_window.h>
#include <dlfcn.h>

// These accessors are exported by engine_api's existing Android JNI bridge.
// They let the adapter validate the already-running VM/context without
// constructing or launching a second Activity.
extern JNIEnv* krkr_GetJNIEnv();
extern JavaVM* krkr_GetJavaVM();
extern jobject krkr_GetApplicationContext();
extern jobject krkr_GetHostActivity();
extern ANativeWindow* krkr_GetNativeWindow();
#endif

namespace aetherkiri::renpy::mobile {

#if defined(__ANDROID__)
namespace {

bool ClearJavaException(JNIEnv* env) {
  if (!env->ExceptionCheck()) return false;
  env->ExceptionClear();
  return true;
}

bool RequireStaticMethod(JNIEnv* env, jclass klass, const char* class_name,
                         const char* method_name, const char* signature,
                         std::string* error) {
  if (env->GetStaticMethodID(klass, method_name, signature) != nullptr) {
    return true;
  }
  ClearJavaException(env);
  *error = "Ren'Py mobile preflight: Java callback " +
           std::string(class_name) + "." + method_name + signature +
           " is missing";
  return false;
}

bool RequireInstanceMethod(JNIEnv* env, jclass klass, const char* class_name,
                           const char* method_name, const char* signature,
                           std::string* error) {
  if (env->GetMethodID(klass, method_name, signature) != nullptr) {
    return true;
  }
  ClearJavaException(env);
  *error = "Ren'Py mobile preflight: Java callback " +
           std::string(class_name) + "." + method_name + signature +
           " is missing";
  return false;
}

jclass FindRequiredClass(JNIEnv* env, const char* class_name,
                         std::string* error) {
  jclass klass = env->FindClass(class_name);
  if (klass != nullptr) return klass;
  ClearJavaException(env);

  // Preflight normally runs from a native Godot thread. FindClass uses that
  // thread's loader, which may not be the APK class loader, so retry through
  // the already-bound Activity before declaring a callback unavailable.
  jobject activity = krkr_GetHostActivity();
  if (activity != nullptr) {
    jclass activity_class = env->GetObjectClass(activity);
    if (activity_class != nullptr) {
      jmethodID get_loader =
          env->GetMethodID(activity_class, "getClassLoader",
                           "()Ljava/lang/ClassLoader;");
      if (get_loader != nullptr) {
        jobject loader = env->CallObjectMethod(activity, get_loader);
        jclass loader_class = env->FindClass("java/lang/ClassLoader");
        ClearJavaException(env);
        if (loader != nullptr && loader_class != nullptr) {
          jmethodID load_class =
              env->GetMethodID(loader_class, "loadClass",
                               "(Ljava/lang/String;)Ljava/lang/Class;");
          if (load_class != nullptr) {
            std::string binary_name(class_name);
            for (char& character : binary_name) {
              if (character == '/') character = '.';
            }
            jstring name = env->NewStringUTF(binary_name.c_str());
            jobject loaded = env->CallObjectMethod(loader, load_class, name);
            env->DeleteLocalRef(name);
            if (loaded != nullptr && !ClearJavaException(env)) {
              env->DeleteLocalRef(loader);
              env->DeleteLocalRef(loader_class);
              env->DeleteLocalRef(activity_class);
              return reinterpret_cast<jclass>(loaded);
            }
            ClearJavaException(env);
          }
        }
        if (loader != nullptr) env->DeleteLocalRef(loader);
        if (loader_class != nullptr) env->DeleteLocalRef(loader_class);
      }
      ClearJavaException(env);
      env->DeleteLocalRef(activity_class);
    }
  }
  *error = "Ren'Py mobile preflight: required Java class " +
           std::string(class_name) +
           " is unavailable; the staged RAPT sources are assets-only and "
           "must be compiled into the host APK";
  return nullptr;
}

bool CheckJavaCallbacks(JNIEnv* env, std::string* error) {
  jclass sdl = FindRequiredClass(env, "org/libsdl/app/SDLActivity", error);
  if (sdl == nullptr) return false;
  const bool sdl_ok =
      RequireStaticMethod(env, sdl, "org.libsdl.app.SDLActivity",
                          "nativeSetupJNI", "()I", error) &&
      RequireStaticMethod(env, sdl, "org.libsdl.app.SDLActivity",
                          "nativeRunMain",
                          "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/Object;)I",
                          error) &&
      RequireStaticMethod(env, sdl, "org.libsdl.app.SDLActivity",
                          "onNativeSurfaceCreated", "()V", error) &&
      RequireStaticMethod(env, sdl, "org.libsdl.app.SDLActivity",
                          "onNativeSurfaceChanged", "()V", error) &&
      RequireStaticMethod(env, sdl, "org.libsdl.app.SDLActivity",
                          "onNativeSurfaceDestroyed", "()V", error) &&
      RequireStaticMethod(env, sdl, "org.libsdl.app.SDLActivity",
                          "nativeSetScreenResolution", "(IIIIF)V", error) &&
      RequireStaticMethod(env, sdl, "org.libsdl.app.SDLActivity",
                          "onNativeResize", "()V", error) &&
      RequireStaticMethod(env, sdl, "org.libsdl.app.SDLActivity",
                          "onNativeKeyDown", "(I)V", error) &&
      RequireStaticMethod(env, sdl, "org.libsdl.app.SDLActivity",
                          "onNativeKeyUp", "(I)V", error) &&
      RequireStaticMethod(env, sdl, "org.libsdl.app.SDLActivity",
                          "onNativeTouch", "(IIIFFF)V", error) &&
      RequireStaticMethod(env, sdl, "org.libsdl.app.SDLActivity",
                          "nativePause", "()V", error) &&
      RequireStaticMethod(env, sdl, "org.libsdl.app.SDLActivity",
                          "nativeResume", "()V", error) &&
      RequireStaticMethod(env, sdl, "org.libsdl.app.SDLActivity",
                          "nativeQuit", "()V", error) &&
      RequireStaticMethod(env, sdl, "org.libsdl.app.SDLActivity",
                          "getNativeSurface", "()Landroid/view/Surface;",
                          error) &&
      RequireStaticMethod(env, sdl, "org.libsdl.app.SDLActivity",
                          "getContext", "()Landroid/content/Context;", error);
  env->DeleteLocalRef(sdl);
  if (!sdl_ok) return false;

  jclass python =
      FindRequiredClass(env, "org/renpy/android/PythonSDLActivity", error);
  if (python == nullptr) return false;
  const bool python_ok =
      RequireInstanceMethod(env, python, "org.renpy.android.PythonSDLActivity",
                            "nativeSetEnv",
                            "(Ljava/lang/String;Ljava/lang/String;)V", error);
  env->DeleteLocalRef(python);
  return python_ok;
}

const char* MissingNativeExport(void* handle) {
  static constexpr const char* kRequired[] = {
      "SDL_main",
      "JNI_OnLoad",
      "SDL_AndroidGetJNIEnv",
      "SDL_AndroidGetActivity",
      "Java_org_libsdl_app_SDLActivity_nativeSetupJNI",
      "Java_org_libsdl_app_SDLActivity_nativeRunMain",
      "Java_org_libsdl_app_SDLActivity_onNativeSurfaceCreated",
      "Java_org_libsdl_app_SDLActivity_onNativeSurfaceChanged",
      "Java_org_libsdl_app_SDLActivity_onNativeSurfaceDestroyed",
      "Java_org_libsdl_app_SDLActivity_nativeSetScreenResolution",
      "Java_org_libsdl_app_SDLActivity_onNativeResize",
      "Java_org_libsdl_app_SDLActivity_onNativeKeyDown",
      "Java_org_libsdl_app_SDLActivity_onNativeKeyUp",
      "Java_org_libsdl_app_SDLActivity_onNativeTouch",
      "Java_org_libsdl_app_SDLActivity_nativePause",
      "Java_org_libsdl_app_SDLActivity_nativeResume",
      "Java_org_libsdl_app_SDLActivity_nativeQuit",
      "Java_org_libsdl_app_SDLActivity_getNativeSurface",
      "Java_org_libsdl_app_SDLActivity_getContext",
      "Java_org_renpy_android_PythonSDLActivity_nativeSetEnv",
  };
  for (const char* symbol : kRequired) {
    dlerror();
    if (dlsym(handle, symbol) == nullptr) return symbol;
  }
  return nullptr;
}

}  // namespace
#endif

engine_result_t BootstrapAdapter::Preflight(const BootstrapRequest& request) {
  if (request.game_root_path_utf8 == nullptr ||
      request.game_root_path_utf8[0] == '\0') {
    last_error_ = "Ren'Py mobile bootstrap requires a non-empty game root";
    return ENGINE_RESULT_INVALID_ARGUMENT;
  }
  if (running_) {
    last_error_ = "Ren'Py mobile bootstrap is already running";
    return ENGINE_RESULT_INVALID_STATE;
  }

#if !defined(__ANDROID__)
  (void)request;
  last_error_ =
      "Ren'Py mobile preflight is only available on an Android host";
  return ENGINE_RESULT_NOT_SUPPORTED;
#else
  void* host_activity = request.existing_host_activity;
  if (host_activity == nullptr) host_activity = krkr_GetHostActivity();
  if (host_activity == nullptr) {
    last_error_ =
        "Ren'Py mobile preflight requires the existing host Activity; "
        "a second Activity is never created";
    return ENGINE_RESULT_NOT_SUPPORTED;
  }
  if (krkr_GetJavaVM() == nullptr || krkr_GetApplicationContext() == nullptr) {
    last_error_ =
        "Ren'Py mobile preflight requires the existing Android JavaVM and "
        "Application Context from EngineBridge";
    return ENGINE_RESULT_NOT_SUPPORTED;
  }

  JNIEnv* env = krkr_GetJNIEnv();
  if (env == nullptr) {
    last_error_ =
        "Ren'Py mobile preflight could not attach the current thread to the "
        "Android JavaVM";
    return ENGINE_RESULT_NOT_SUPPORTED;
  }
  if (!CheckJavaCallbacks(env, &last_error_)) {
    return ENGINE_RESULT_NOT_SUPPORTED;
  }

  ANativeWindow* window = krkr_GetNativeWindow();
  if (window == nullptr) {
    last_error_ =
        "Ren'Py mobile preflight requires a bound host SDL surface; call "
        "EngineBridge.nativeSetSurface before opening the game";
    return ENGINE_RESULT_NOT_SUPPORTED;
  }
  const int32_t width = ANativeWindow_getWidth(window);
  const int32_t height = ANativeWindow_getHeight(window);
  ANativeWindow_release(window);
  if (width <= 0 || height <= 0) {
    last_error_ =
        "Ren'Py mobile preflight found a host SDL surface with invalid "
        "dimensions";
    return ENGINE_RESULT_NOT_SUPPORTED;
  }

  if (native_library_handle_ != nullptr) {
    dlclose(native_library_handle_);
    native_library_handle_ = nullptr;
  }
  void* library = dlopen("librenpython.so", RTLD_NOW | RTLD_LOCAL);
  if (library == nullptr) {
    const char* detail = dlerror();
    last_error_ = "Ren'Py mobile preflight could not load librenpython.so";
    if (detail != nullptr) {
      last_error_.append(": ");
      last_error_.append(detail);
    }
    return ENGINE_RESULT_NOT_SUPPORTED;
  }
  if (const char* missing = MissingNativeExport(library); missing != nullptr) {
    last_error_ = "Ren'Py mobile preflight: librenpython.so is missing export ";
    last_error_.append(missing);
    dlclose(library);
    return ENGINE_RESULT_NOT_SUPPORTED;
  }
  native_library_handle_ = library;
  last_error_ =
      "Ren'Py mobile preflight passed host Activity, Java callbacks, SDL "
      "surface, and librenpython exports; SDL/Python lifecycle bridge is not "
      "started by this boundary";
  return ENGINE_RESULT_NOT_SUPPORTED;
#endif
}

engine_result_t BootstrapAdapter::Start(const BootstrapRequest& request) {
  return Preflight(request);
}

engine_result_t BootstrapAdapter::Stop() {
  // Start() cannot currently transition to running, but make destruction
  // idempotent so the provider can call Stop() unconditionally.
  running_ = false;
#if defined(__ANDROID__)
  if (native_library_handle_ != nullptr) {
    dlclose(native_library_handle_);
    native_library_handle_ = nullptr;
  }
#endif
  last_error_.clear();
  return ENGINE_RESULT_OK;
}

}  // namespace aetherkiri::renpy::mobile
