/**
 * @file KrkrJniHelper.cpp
 * @brief Lightweight JNI helper implementation — replaces cocos2d::JniHelper.
 */

#ifdef __ANDROID__

#include "KrkrJniHelper.h"
#include <android/log.h>
#include <mutex>
#include <string>

#define LOG_TAG "krkr2"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Declared in bridge/engine_api/src/android_jni_bridge.cpp
extern jobject krkr_GetApplicationContext();

namespace krkr {

static JavaVM* g_javaVM = nullptr;
static std::mutex g_jvm_mutex;

namespace {

// Try to get an application context even before host plugin JNI has run.
jobject ResolveApplicationContext(JNIEnv* env) {
    if (!env) return nullptr;
    jobject appContextGlobal = krkr_GetApplicationContext();
    if (!appContextGlobal) {
        jclass activityThreadClass = env->FindClass("android/app/ActivityThread");
        if (!activityThreadClass) {
            env->ExceptionClear();
            return nullptr;
        }
        jmethodID currentApplication = env->GetStaticMethodID(
            activityThreadClass, "currentApplication", "()Landroid/app/Application;");
        if (!currentApplication) {
            env->ExceptionClear();
            env->DeleteLocalRef(activityThreadClass);
            return nullptr;
        }
        jobject app = env->CallStaticObjectMethod(activityThreadClass, currentApplication);
        env->DeleteLocalRef(activityThreadClass);
        if (env->ExceptionCheck() || !app) {
            env->ExceptionClear();
            return nullptr;
        }
        return app;
    }

    return env->NewLocalRef(appContextGlobal);
}

jclass FindClassWithAppClassLoader(JNIEnv* env, const char* className) {
    if (!env || !className) return nullptr;

    jobject appContext = ResolveApplicationContext(env);
    if (!appContext) {
        return nullptr;
    }

    jclass contextClass = env->FindClass("android/content/Context");
    if (!contextClass) {
        env->ExceptionClear();
        env->DeleteLocalRef(appContext);
        return nullptr;
    }

    jmethodID getClassLoader = env->GetMethodID(
        contextClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
    if (!getClassLoader) {
        env->ExceptionClear();
        env->DeleteLocalRef(contextClass);
        env->DeleteLocalRef(appContext);
        return nullptr;
    }

    jobject classLoader =
        env->CallObjectMethod(appContext, getClassLoader);
    if (env->ExceptionCheck() || !classLoader) {
        env->ExceptionClear();
        env->DeleteLocalRef(contextClass);
        env->DeleteLocalRef(appContext);
        return nullptr;
    }

    jclass classLoaderClass = env->FindClass("java/lang/ClassLoader");
    if (!classLoaderClass) {
        env->ExceptionClear();
        env->DeleteLocalRef(classLoader);
        env->DeleteLocalRef(contextClass);
        env->DeleteLocalRef(appContext);
        return nullptr;
    }

    jmethodID loadClass = env->GetMethodID(
        classLoaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    if (!loadClass) {
        env->ExceptionClear();
        env->DeleteLocalRef(classLoaderClass);
        env->DeleteLocalRef(classLoader);
        env->DeleteLocalRef(contextClass);
        env->DeleteLocalRef(appContext);
        return nullptr;
    }

    std::string dottedName(className);
    for (char& c : dottedName) {
        if (c == '/') c = '.';
    }
    jstring classNameJava = env->NewStringUTF(dottedName.c_str());
    jobject classObject = env->CallObjectMethod(classLoader, loadClass, classNameJava);

    env->DeleteLocalRef(classNameJava);
    env->DeleteLocalRef(classLoaderClass);
    env->DeleteLocalRef(classLoader);
    env->DeleteLocalRef(contextClass);
    env->DeleteLocalRef(appContext);

    if (env->ExceptionCheck() || !classObject) {
        env->ExceptionClear();
        return nullptr;
    }

    return reinterpret_cast<jclass>(classObject);
}

jclass FindClass(JNIEnv* env, const char* className) {
    if (!env || !className) return nullptr;

    jclass classID = env->FindClass(className);
    if (classID) {
        return classID;
    }

    env->ExceptionClear();
    return FindClassWithAppClassLoader(env, className);
}

} // namespace

void JniHelper::setJavaVM(JavaVM* vm) {
    std::lock_guard<std::mutex> lock(g_jvm_mutex);
    g_javaVM = vm;
}

JavaVM* JniHelper::getJavaVM() {
    std::lock_guard<std::mutex> lock(g_jvm_mutex);
    return g_javaVM;
}

JNIEnv* JniHelper::getEnv() {
    JavaVM* vm = getJavaVM();
    if (!vm) {
        LOGE("JniHelper::getEnv: JavaVM is null");
        return nullptr;
    }

    JNIEnv* env = nullptr;
    jint status = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            LOGE("JniHelper::getEnv: failed to attach thread");
            return nullptr;
        }
    } else if (status != JNI_OK) {
        LOGE("JniHelper::getEnv: GetEnv failed with status %d", status);
        return nullptr;
    }
    return env;
}

std::string JniHelper::jstring2string(jstring str) {
    if (!str) return "";

    JNIEnv* env = getEnv();
    if (!env) return "";

    const char* chars = env->GetStringUTFChars(str, nullptr);
    if (!chars) return "";

    std::string result(chars);
    env->ReleaseStringUTFChars(str, chars);
    return result;
}

bool JniHelper::getStaticMethodInfo(MethodInfo& info,
                                     const char* className,
                                     const char* methodName,
                                     const char* signature) {
    JNIEnv* env = getEnv();
    if (!env) return false;

    jclass classID = FindClass(env, className);
    if (!classID) {
        LOGE("JniHelper: class '%s' not found", className);
        return false;
    }

    jmethodID methodID = env->GetStaticMethodID(classID, methodName, signature);
    if (!methodID) {
        LOGE("JniHelper: static method '%s.%s%s' not found",
             className, methodName, signature);
        env->ExceptionClear();
        env->DeleteLocalRef(classID);
        return false;
    }

    info.env = env;
    info.classID = classID;
    info.methodID = methodID;
    return true;
}

bool JniHelper::getMethodInfo(MethodInfo& info,
                               const char* className,
                               const char* methodName,
                               const char* signature) {
    JNIEnv* env = getEnv();
    if (!env) return false;

    jclass classID = FindClass(env, className);
    if (!classID) {
        LOGE("JniHelper: class '%s' not found", className);
        return false;
    }

    jmethodID methodID = env->GetMethodID(classID, methodName, signature);
    if (!methodID) {
        LOGE("JniHelper: method '%s.%s%s' not found",
             className, methodName, signature);
        env->ExceptionClear();
        env->DeleteLocalRef(classID);
        return false;
    }

    info.env = env;
    info.classID = classID;
    info.methodID = methodID;
    return true;
}

} // namespace krkr

#endif // __ANDROID__
