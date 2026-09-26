/**
 * @file KrkrJniHelper.h
 * @brief Lightweight JNI helper — replaces cocos2d::JniHelper.
 *
 * Provides thread-safe JNI environment access and common string conversion
 * utilities needed by the engine's Android platform code.
 */
#pragma once

#ifdef __ANDROID__

#include <jni.h>
#include <string>

namespace krkr {

class JniHelper {
public:
    /// Get the process JavaVM (owned by the engine_api JNI bridge, which
    /// stores it in JNI_OnLoad and recovers it from the Android runtime at a
    /// low rate before that).
    static JavaVM* getJavaVM();

    /// Get a JNIEnv for the current thread (attaches if needed).
    static JNIEnv* getEnv();

    /// Convert jstring → std::string (UTF-8). Handles null safely.
    static std::string jstring2string(jstring str);

    /// Method lookup result.
    struct MethodInfo {
        JNIEnv* env = nullptr;
        jclass classID = nullptr;
        jmethodID methodID = nullptr;
    };

    /**
     * Look up a static method on a Java class.
     * @param info      Output: env, classID, methodID
     * @param className JNI class name (e.g. "org/tvp/kirikiri2/KR2Activity")
     * @param methodName Method name
     * @param signature  JNI method signature
     * @return true if found, false otherwise
     *
     * NOTE: The caller is responsible for calling
     *       env->DeleteLocalRef(info.classID) after use.
     */
    static bool getStaticMethodInfo(MethodInfo& info,
                                     const char* className,
                                     const char* methodName,
                                     const char* signature);

    /**
     * Look up an instance method on a Java class.
     * @param info      Output: env, classID, methodID
     * @param className JNI class name
     * @param methodName Method name
     * @param signature  JNI method signature
     * @return true if found, false otherwise
     */
    static bool getMethodInfo(MethodInfo& info,
                               const char* className,
                               const char* methodName,
                               const char* signature);
};

} // namespace krkr

// Compatibility aliases for code that used cocos2d::JniHelper directly
using JniMethodInfo = krkr::JniHelper::MethodInfo;

#endif // __ANDROID__
