#include "AndroidUtils.h"
#include <minizip/unzip.h>
#include "zlib.h"
#include <map>
#include <string>
#include <vector>
#include "tjs.h"
#include "MsgIntf.h"
#include "md5.h"
#include "DebugIntf.h"
#include <condition_variable>
#include <mutex>
#include "KrkrJniHelper.h"
#include <set>
#include <sstream>
#include "SysInitIntf.h"
#include "ConfigManager/LocaleConfigManager.h"
#include "Platform.h"
#include <EGL/egl.h>
#include "krkr_egl_context.h"
#include <queue>
#include <unistd.h>
#include <fcntl.h>
#include <android/log.h>
#include "TickCount.h"
#include "StorageImpl.h"
#include "ConfigManager/IndividualConfigManager.h"
#include "EventIntf.h"
#include "RenderManager.h"
#include <sys/stat.h>
#include <cerrno>
#include <cstdio>

using JniHelper = krkr::JniHelper;
using JniMethodInfo = krkr::JniHelper::MethodInfo;

#define KR2ActJavaPath "org/tvp/kirikiri2/KR2Activity"
// #define KR2EntryJavaPath "org/tvp/kirikiri2/Kirikiroid2"

namespace {

constexpr const char *kFallbackPackageName = "org.github.krkr2.aetherkiri";

std::string FallbackInternalStoragePath() {
    return std::string("/storage/emulated/0/Android/data/") +
           kFallbackPackageName + "/files";
}

void DeleteLocalRefIf(JNIEnv *env, jobject ref) {
    if(env != nullptr && ref != nullptr) {
        env->DeleteLocalRef(ref);
    }
}

tjs_int ReadProcMemAvailableMB() {
    FILE *file = std::fopen("/proc/meminfo", "r");
    if(!file)
        return -1;
    char line[256];
    long mem_available_kb = -1;
    long mem_free_kb = -1;
    while(std::fgets(line, sizeof(line), file)) {
        long value = 0;
        if(std::sscanf(line, "MemAvailable: %ld kB", &value) == 1) {
            mem_available_kb = value;
            break;
        }
        if(std::sscanf(line, "MemFree: %ld kB", &value) == 1)
            mem_free_kb = value;
    }
    std::fclose(file);
    const long available_kb =
        mem_available_kb >= 0 ? mem_available_kb : mem_free_kb;
    return available_kb >= 0
               ? static_cast<tjs_int>(available_kb / 1024)
               : -1;
}

tjs_int ReadProcSelfRssMB() {
    FILE *file = std::fopen("/proc/self/statm", "r");
    if(!file)
        return -1;
    unsigned long total_pages = 0;
    unsigned long resident_pages = 0;
    const int read = std::fscanf(file, "%lu %lu", &total_pages,
                                 &resident_pages);
    std::fclose(file);
    if(read != 2)
        return -1;
    return static_cast<tjs_int>(
        resident_pages * static_cast<unsigned long>(getpagesize()) /
        (1024UL * 1024UL));
}

} // namespace

// Declared in android_jni_bridge.cpp; provides the host Application Context
// as a fallback when KR2Activity is not available.
extern jobject krkr_GetApplicationContext();

unsigned int __page_size = getpagesize();

void TVPPrintLog(const char *str) {
    __android_log_print(ANDROID_LOG_INFO, "kr2 debug info", "%s", str);
}

tjs_int TVPGetSystemFreeMemory() {
    // The embedded Godot host does not use KR2Activity. Looking up its static
    // memory helpers from the render thread periodically crosses JNI (and may
    // repeatedly miss the class), producing visible frame stalls. /proc is
    // available to the app process and gives the values needed by the memory
    // governor without touching Java.
    return ReadProcMemAvailableMB();
}

tjs_int TVPGetSelfUsedMemory() {
    return ReadProcSelfRssMB();
}

void TVPForceSwapBuffer() {
    // Use the engine's EGL context manager instead of eglGetCurrentDisplay(),
    // which may return EGL_NO_DISPLAY in headless Pbuffer mode.
    // Only swap when a WindowSurface is attached (Android SurfaceTexture mode)
    // AND the frame was actually rendered (dirty flag set by UpdateDrawBuffer).
    //
    // Without the dirty check, eglSwapBuffers is called every tick even when
    // no new content was drawn.  In double-buffered mode this causes the
    // front/back buffers to alternate between the current frame and a stale
    // frame, producing visible flicker ("previous image overlaid").
    auto& egl = krkr::GetEngineEGLContext();
    if (egl.HasNativeWindow()) {
        if (!egl.ConsumeFrameDirty()) {
            // No new content — skip swap to avoid double-buffer flicker.
            return;
        }
        const EGLBoolean ok = eglSwapBuffers(egl.GetDisplay(), egl.GetWindowSurface());
        if (ok != EGL_TRUE) {
            __android_log_print(ANDROID_LOG_WARN, "krkr2",
                                "TVPForceSwapBuffer: eglSwapBuffers failed err=0x%x",
                                eglGetError());
        } else {
            egl.MarkFramePresented();
        }
    }
    // In Pbuffer mode, swap is a no-op — engine_tick handles readback.
}

std::string TVPGetDeviceID() {
    std::string ret;

    // use pure jni to avoid java code
    // 	jclass classID = pEnv->FindClass(KR2EntryJavaPath);
    // 	std::string strtmp("()L"); strtmp += KR2EntryJavaPath; strtmp
    // += ";"; 	jmethodID methodGetInstance =
    // pEnv->GetStaticMethodID(classID, "GetInstance",
    // strtmp.c_str()); 	jobject sInstance =
    // pEnv->CallStaticObjectMethod(classID, methodGetInstance);
    // jmethodID getSystemService = pEnv->GetMethodID(classID,
    // "getSystemService",
    // "(Ljava/lang/String;)Ljava/lang/Object;"); 	jstring jstrPhone
    // = pEnv->NewStringUTF("phone"); 	jobject telephonyManager =
    // pEnv->CallObjectMethod(sInstance, getSystemService, jstrPhone);
    // 	pEnv->DeleteLocalRef(jstrPhone);
    //
    // 	jclass clsTelephonyManager =
    // pEnv->FindClass("android/telephony/TelephonyManager");
    // jmethodID getDeviceId = pEnv->GetMethodID(clsTelephonyManager,
    // "getDeviceId",
    // "()Ljava/lang/String;"); 	jstring jstrDevID =
    // (jstring)pEnv->CallObjectMethod(telephonyManager, getDeviceId);
    // if (jstrDevID) { 		const char *p =
    // pEnv->GetStringUTFChars(jstrDevID, 0); if (p
    // && *p) { 			ret = "DevID="; 			ret +=
    // p; pEnv->ReleaseStringUTFChars(jstrDevID, p); 		} else {
    // if (p) {
    // pEnv->ReleaseStringUTFChars(jstrDevID, p);
    // 			}
    // 			jmethodID getContentResolver =
    // pEnv->GetMethodID(classID, "getContentResolver",
    // "()Landroid/content/ContentResolver;"); 			jobject
    // contentResolver = pEnv->CallObjectMethod(sInstance,
    // getContentResolver);
    //
    // 			jclass clsSecure =
    // pEnv->FindClass("android/provider/Settings/Secure");
    // if (clsSecure) { 				jmethodID Secure_getString =
    // pEnv->GetMethodID(clsSecure, "getString",
    // "(Landroid/content/ContentResolver;Ljava/lang/String;)Ljava/lang/String;");
    // 				jstring jastrAndroid_ID =
    // pEnv->NewStringUTF("android_id"); 				jstring
    // jstrAndroidID =
    // (jstring)pEnv->CallStaticObjectMethod(clsSecure,
    // Secure_getString, contentResolver, jastrAndroid_ID);
    // if (jstrAndroidID) { 					const char *p =
    // pEnv->GetStringUTFChars(jstrAndroidID, 0); if (p && strlen(p) >
    // 8 && strcmp(p, "9774d56d682e549c")) { ret = "AndroidID="; ret
    // += p;
    // 					}
    // 				}
    // 				pEnv->ReleaseStringUTFChars(jstrAndroidID, p);
    // 				pEnv->DeleteLocalRef(jastrAndroid_ID);
    // 			}
    // 		}
    // 	}
    // 	if (ret.empty())
    {
        JniMethodInfo methodInfo;
        if(JniHelper::getStaticMethodInfo(methodInfo, KR2ActJavaPath,
                                          "getDeviceId",
                                          "()Ljava/lang/String;")) {
            auto result = (jstring)methodInfo.env->CallStaticObjectMethod(
                methodInfo.classID, methodInfo.methodID);
            ret = JniHelper::jstring2string(result);
            methodInfo.env->DeleteLocalRef(result);
            methodInfo.env->DeleteLocalRef(methodInfo.classID);
            char *t = (char *)ret.c_str();
            while(*t) {
                if(*t == ':') {
                    *t = '=';
                    break;
                }
                t++;
            }
        }
    }

    return ret;
}

static jobject GetKR2ActInstance() {
    JniMethodInfo methodInfo;
    std::string strtmp("()L");
    strtmp += KR2ActJavaPath;
    strtmp += ";";
    if(JniHelper::getStaticMethodInfo(methodInfo, KR2ActJavaPath, "GetInstance",
                                      strtmp.c_str())) {
        jobject ret = methodInfo.env->CallStaticObjectMethod(
            methodInfo.classID, methodInfo.methodID);
        methodInfo.env->DeleteLocalRef(methodInfo.classID);
        return ret;
    }
    // Fallback for embedded host mode: KR2Activity doesn't exist,
    // use the Application Context stored by the host plugin.
    // Create a new local ref so callers can safely DeleteLocalRef on it.
    jobject ctx = krkr_GetApplicationContext();
    if (ctx) {
        JNIEnv* env = JniHelper::getEnv();
        if (env) {
            return env->NewLocalRef(ctx);
        }
    }
    return 0;
}

static std::string GetApkStoragePath() {
    JniMethodInfo methodInfo;
    jobject sInstance = GetKR2ActInstance();
    if(sInstance == nullptr) {
        return "";
    }
    if(!JniHelper::getMethodInfo(methodInfo, "android/content/Context",
                                 "getApplicationInfo",
                                 "()Landroid/content/pm/ApplicationInfo;")) {
        DeleteLocalRefIf(methodInfo.env, sInstance);
        return "";
    }
    jobject ApplicationInfo =
        methodInfo.env->CallObjectMethod(sInstance, methodInfo.methodID);
    jclass clsApplicationInfo =
        methodInfo.env->FindClass("android/content/pm/ApplicationInfo");
    jfieldID id_sourceDir = methodInfo.env->GetFieldID(
        clsApplicationInfo, "sourceDir", "Ljava/lang/String;");
    methodInfo.env->DeleteLocalRef(sInstance);
    std::string result;
    if(ApplicationInfo != nullptr && id_sourceDir != nullptr) {
        result = JniHelper::jstring2string(
            (jstring)methodInfo.env->GetObjectField(ApplicationInfo, id_sourceDir));
    }
    DeleteLocalRefIf(methodInfo.env, clsApplicationInfo);
    DeleteLocalRefIf(methodInfo.env, ApplicationInfo);
    return result;
}

static std::string GetPackageName() {
    JniMethodInfo methodInfo;
    jobject sInstance = GetKR2ActInstance();
    if(sInstance == nullptr) {
        return kFallbackPackageName;
    }
    if(!JniHelper::getMethodInfo(methodInfo, "android/content/ContextWrapper",
                                 "getPackageName", "()Ljava/lang/String;")) {
        DeleteLocalRefIf(methodInfo.env, sInstance);
        return kFallbackPackageName;
    }
    std::string result = JniHelper::jstring2string(
        (jstring)methodInfo.env->CallObjectMethod(sInstance, methodInfo.methodID));
    methodInfo.env->DeleteLocalRef(sInstance);
    return result.empty() ? kFallbackPackageName : result;
}

// from unzip.cpp
#define FLAG_UTF8 (1 << 11)
extern zlib_filefunc64_def TVPZlibFileFunc;
class ZipFile {
    unzFile uf;
    bool utf8{};

public:
    ZipFile() : uf(nullptr) {}
    ~ZipFile() {
        if(uf) {
            unzClose(uf);
            uf = nullptr;
        }
    }
    bool Open(const char *filename) {
        if((uf = unzOpen(filename)) == nullptr) {
            ttstr msg = filename;
            msg += TJS_W(" can't open.");
            TVPThrowExceptionMessage(msg.c_str());
            return false;
        }
        // UTF8¤Ê¥Õ¥¡¥¤¥ëÃû¤«¤É¤¦¤«¤ÎÅÐ¶¨¡£×î³õ¤Î¥Õ¥¡¥¤¥ë¤Ç›Q¤á¤ë
        unzGoToFirstFile(uf);
        unz_file_info file_info;
        if(unzGetCurrentFileInfo(uf, &file_info, nullptr, 0, nullptr, 0,
                                 nullptr, 0) == UNZ_OK) {
            utf8 = (file_info.flag & FLAG_UTF8) != 0;
            return true;
        }
        return false;
    }
    bool GetData(std::vector<unsigned char> &buff, const char *filename) {
        bool ret = false;
        if(unzLocateFile(uf, filename, 0) == UNZ_OK) {
            int result = unzOpenCurrentFile(uf);
            if(result == UNZ_OK) {
                unz_file_info info;
                unzGetCurrentFileInfo(uf, &info, nullptr, 0, nullptr, 0,
                                      nullptr, 0);
                buff.resize(info.uncompressed_size);
                unsigned int size =
                    unzReadCurrentFile(uf, &buff[0], info.uncompressed_size);
                if(size == info.uncompressed_size)
                    ret = true;
                unzCloseCurrentFile(uf);
            }
        }
        return ret;
    }
    tjs_int64 GetMD5InZip(const char *filename) {
        std::vector<unsigned char> buff;
        if(!GetData(buff, filename))
            return 0;
        md5_state_t state;
        md5_init(&state);
        md5_append(&state, (const md5_byte_t *)&buff[0], buff.size());
        union {
            tjs_int64 _s64[2];
            tjs_uint8 _u8[16];
        } digest{};
        md5_finish(&state, digest._u8);
        return digest._s64[0] ^ digest._s64[1];
    }

private:
    unzFile zipFile{};
};

std::string TVPGetDeviceLanguage() {
    // use pure jni to avoid java code
    JniMethodInfo methodInfo;
    if(!JniHelper::getStaticMethodInfo(methodInfo, "java/util/Locale",
                                       "getDefault", "()Ljava/util/Locale;"))
        return "";
    jobject LocaleObj = methodInfo.env->CallStaticObjectMethod(
        methodInfo.classID, methodInfo.methodID);
    if(!JniHelper::getMethodInfo(methodInfo, "java/util/Locale", "getLanguage",
                                 "()Ljava/lang/String;"))
        return "";
    jstring languageID = (jstring)methodInfo.env->CallObjectMethod(
        LocaleObj, methodInfo.methodID);
    methodInfo.env->DeleteLocalRef(methodInfo.classID);
    return JniHelper::jstring2string(languageID);
}

std::string TVPGetPackageVersionString() {
    JniMethodInfo methodInfo;
    if(JniHelper::getStaticMethodInfo(methodInfo, KR2ActJavaPath, "GetVersion",
                                      "()Ljava/lang/String;")) {
        return JniHelper::jstring2string(
            (jstring)methodInfo.env->CallStaticObjectMethod(
                methodInfo.classID, methodInfo.methodID));
    }
    return "";
}

static std::vector<std::string> &split(const std::string &s, char delim,
                                       std::vector<std::string> &elems) {
    std::stringstream ss(s);
    std::string item;
    while(std::getline(ss, item, delim)) {
        elems.emplace_back(item);
    }
    return elems;
}

static std::string File_getAbsolutePath(jobject FileObj) {
    if(!FileObj)
        return "";
    JniMethodInfo methodInfo;
    if(!JniHelper::getMethodInfo(methodInfo, "java/io/File", "exists", "()Z"))
        return "";
    if(!methodInfo.env->CallBooleanMethod(FileObj, methodInfo.methodID))
        return "";
    if(!JniHelper::getMethodInfo(methodInfo, "java/io/File", "getAbsolutePath",
                                 "()Ljava/lang/String;"))
        return "";
    jstring path =
        (jstring)methodInfo.env->CallObjectMethod(FileObj, methodInfo.methodID);
    std::string ret = JniHelper::jstring2string(path);
    return ret;
}

static std::string GetInternalStoragePath() {
    jobject sInstance = GetKR2ActInstance();
    if(sInstance == nullptr) {
        return FallbackInternalStoragePath();
    }
    JniMethodInfo methodInfo;
    if(!JniHelper::getMethodInfo(methodInfo, "android/content/ContextWrapper",
                                 "getFilesDir", "()Ljava/io/File;")) {
        DeleteLocalRefIf(methodInfo.env, sInstance);
        return FallbackInternalStoragePath();
    }
    jobject FileObj =
        methodInfo.env->CallObjectMethod(sInstance, methodInfo.methodID);
    std::string result = File_getAbsolutePath(FileObj);
    DeleteLocalRefIf(methodInfo.env, FileObj);
    methodInfo.env->DeleteLocalRef(sInstance);
    return result.empty() ? FallbackInternalStoragePath() : result;
}

std::string Android_GetDumpStoragePath() {
    return GetInternalStoragePath() + "/dump";
}

static int InsertFilepathInto(JNIEnv *env, std::vector<std::string> &vec,
                              jobjectArray FileObjs) {
    int count = env->GetArrayLength(FileObjs);
    for(int i = 0; i < count; ++i) {
        jobject FileObj = env->GetObjectArrayElement(FileObjs, i);
        std::string path = File_getAbsolutePath(FileObj);
        if(!path.empty())
            vec.emplace_back(path);
    }
    return count;
}

static int GetExternalStoragePath(std::vector<std::string> &ret) {
    int count = 0;
    JniMethodInfo methodInfo;
    jobject sInstance = GetKR2ActInstance();
    if(sInstance == nullptr) {
        ret.emplace_back(FallbackInternalStoragePath());
        ret.emplace_back("/storage/emulated/0");
        return 2;
    }
    // 	if (JniHelper::getMethodInfo(methodInfo,
    // "android/content/Context", "getExternalMediaDirs",
    // "()[Ljava/io/File;")) { 		jobjectArray FileObjs =
    // (jobjectArray)methodInfo.env->CallObjectMethod(sInstance,
    // methodInfo.methodID); 		if(FileObjs) count +=
    // InsertFilepathInto(methodInfo.env, ret, FileObjs);
    // 	}
    if(JniHelper::getMethodInfo(methodInfo, "android/content/Context",
                                "getExternalFilesDirs",
                                "(Ljava/lang/String;)[Ljava/io/File;")) {
        jobjectArray FileObjs = (jobjectArray)methodInfo.env->CallObjectMethod(
            sInstance, methodInfo.methodID, nullptr);
        if(FileObjs)
            count += InsertFilepathInto(methodInfo.env, ret, FileObjs);
    } else if(JniHelper::getMethodInfo(methodInfo, "android/content/Context",
                                       "getExternalFilesDir",
                                       "(Ljava/lang/String;)Ljava/io/File;")) {
        jobject FileObj = methodInfo.env->CallObjectMethod(
            sInstance, methodInfo.methodID, nullptr);
        if(FileObj) {
            ret.emplace_back(File_getAbsolutePath(FileObj));
            ++count;
        }
    }
    DeleteLocalRefIf(methodInfo.env, sInstance);
    return count;
}

std::vector<std::string> TVPGetAppStoragePath() {
    std::vector<std::string> ret;
    std::string internal = GetInternalStoragePath();
    if(!internal.empty()) {
        ret.emplace_back(internal);
    }
    GetExternalStoragePath(ret);
    return ret;
}

std::vector<std::string> TVPGetDriverPath() {
    std::vector<std::string> ret;
    jobject sInstance = GetKR2ActInstance();
    JniMethodInfo methodInfo;
    if(sInstance != nullptr &&
       JniHelper::getMethodInfo(methodInfo, KR2ActJavaPath, "getStoragePath",
                                "()[Ljava/lang/String;")) {
        jobjectArray PathObjs = (jobjectArray)methodInfo.env->CallObjectMethod(
            sInstance, methodInfo.methodID);
        if(PathObjs) {
            int count = methodInfo.env->GetArrayLength(PathObjs);
            for(int i = 0; i < count; ++i) {
                jstring path =
                    (jstring)methodInfo.env->GetObjectArrayElement(PathObjs, i);
                if(path)
                    ret.emplace_back(JniHelper::jstring2string(path));
            }
        }
    }
    DeleteLocalRefIf(methodInfo.env, sInstance);

    if(!ret.empty())
        return ret;

    // embedded host mode fallback: prefer app-scoped directories from Context APIs.
    std::vector<std::string> app_paths = TVPGetAppStoragePath();
    for(const auto &p : app_paths) {
        if(!p.empty()) ret.emplace_back(p);
    }
    if(!ret.empty()) {
        return ret;
    }

    char buffer[256] = { 0 };

    // enum all mounted storages
    FILE *fp = fopen("/proc/mounts", "r");
    if(!fp) {
        return ret;
    }
    std::set<std::string> mounted;
    while(fgets(buffer, sizeof(buffer), fp)) {
        std::vector<std::string> tabs;
        split(buffer, ' ', tabs);
        if(tabs.size() < 4)
            continue;
        if(mounted.find(tabs[0]) != mounted.end())
            continue;
        const std::string &path = tabs[1];
        if(tabs[3].find("rw,") != std::string::npos &&
           (tabs[2] == "vfat" || path.find("/mnt") != std::string::npos)) {
            if(path.find("/mnt/secure") != std::string::npos ||
               path.find("/mnt/asec") != std::string::npos ||
               path.find("/mnt/mapper") != std::string::npos ||
               path.find("/mnt/obb") != std::string::npos ||
               tabs[0] == "tmpfs" || tabs[2] == "tmpfs") {
                continue;
            }
            mounted.insert(tabs[0]);
            ret.emplace_back(path);
        }
    }
    fclose(fp);

    return ret;
}

// extern "C" int TVPCheckValidate()
// {
//     JNIEnv *pEnv = 0;
//     bool ret = true;
//
//     if (! getEnv(&pEnv))
//     {
//         return false;
//     }
// 	{
// 		jclass classID = pEnv->FindClass(KR2EntryJavaPath);
// 		std::string strtmp("()L"); strtmp += KR2EntryJavaPath; strtmp
// +=
// ";"; 		jmethodID methodGetInstance =
// pEnv->GetStaticMethodID(classID, "GetInstance", strtmp.c_str());
// jobject sInstance = pEnv->CallStaticObjectMethod(classID,
// methodGetInstance);
//
// 		jclass clsPreferenceManager =
// pEnv->FindClass("android.preference.PreferenceManager"); jmethodID
// getDefaultSharedPreferences =
// pEnv->GetMethodID(clsPreferenceManager,
// "getDefaultSharedPreferences",
// "(Landroid/content/Context;)Landroid.preference.PreferenceManager;");
// jobject PreferenceManager =
// pEnv->CallStaticObjectMethod(clsPreferenceManager,
// getDefaultSharedPreferences, sInstance); 		jmethodID getString
// = pEnv->GetMethodID(clsPreferenceManager, "getString",
// "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
// jstring jstrConstAPPID = pEnv->NewStringUTF("APP_ID"); jstring
// jstrNull = pEnv->NewStringUTF(""); 		jstring jstrAPPID =
// (jstring)pEnv->CallObjectMethod(PreferenceManager, getString,
// jstrConstAPPID, jstrNull);
// pEnv->DeleteLocalRef(jstrConstAPPID);
// 		pEnv->DeleteLocalRef(jstrNull);
// 		const char *p = pEnv->GetStringUTFChars(jstrAPPID, 0);
// 		if(0x929e08af != adler32(0, (const Bytef*)p, strlen(p))) ret =
// false; 		pEnv->ReleaseStringUTFChars(jstrAPPID, p);
// 	}
//
//     return ret;
// }
namespace kr2android {
    std::condition_variable MessageBoxCond;
    std::mutex MessageBoxLock;
    int MsgBoxRet = -2;
    std::string MessageBoxRetText;
} // namespace kr2android
using namespace kr2android;

namespace {

void ClearJniException(JNIEnv *env) {
    if(env != nullptr && env->ExceptionCheck()) {
        env->ExceptionClear();
    }
}

jobject GetApplicationContextLocal(JNIEnv *env) {
    if(env == nullptr) return nullptr;

    jobject ctx = krkr_GetApplicationContext();
    if(ctx != nullptr) {
        return env->NewLocalRef(ctx);
    }

    jclass activityThreadClass = env->FindClass("android/app/ActivityThread");
    if(activityThreadClass == nullptr) {
        ClearJniException(env);
        return nullptr;
    }

    jmethodID currentApplication = env->GetStaticMethodID(
        activityThreadClass, "currentApplication",
        "()Landroid/app/Application;");
    if(currentApplication == nullptr) {
        ClearJniException(env);
        env->DeleteLocalRef(activityThreadClass);
        return nullptr;
    }

    jobject app = env->CallStaticObjectMethod(activityThreadClass,
                                             currentApplication);
    env->DeleteLocalRef(activityThreadClass);
    if(env->ExceptionCheck() || app == nullptr) {
        ClearJniException(env);
        return nullptr;
    }
    return app;
}

jclass FindClassWithAppClassLoader(JNIEnv *env, const char *className) {
    if(env == nullptr || className == nullptr) return nullptr;

    jclass cls = env->FindClass(className);
    if(cls != nullptr && !env->ExceptionCheck()) {
        return cls;
    }
    ClearJniException(env);

    jobject appContext = GetApplicationContextLocal(env);
    if(appContext == nullptr) return nullptr;

    jclass contextClass = env->FindClass("android/content/Context");
    if(contextClass == nullptr) {
        ClearJniException(env);
        env->DeleteLocalRef(appContext);
        return nullptr;
    }

    jmethodID getClassLoader = env->GetMethodID(
        contextClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
    if(getClassLoader == nullptr) {
        ClearJniException(env);
        env->DeleteLocalRef(contextClass);
        env->DeleteLocalRef(appContext);
        return nullptr;
    }

    jobject classLoader = env->CallObjectMethod(appContext, getClassLoader);
    env->DeleteLocalRef(contextClass);
    env->DeleteLocalRef(appContext);
    if(env->ExceptionCheck() || classLoader == nullptr) {
        ClearJniException(env);
        return nullptr;
    }

    jclass classLoaderClass = env->FindClass("java/lang/ClassLoader");
    if(classLoaderClass == nullptr) {
        ClearJniException(env);
        env->DeleteLocalRef(classLoader);
        return nullptr;
    }

    jmethodID loadClass = env->GetMethodID(
        classLoaderClass, "loadClass",
        "(Ljava/lang/String;)Ljava/lang/Class;");
    if(loadClass == nullptr) {
        ClearJniException(env);
        env->DeleteLocalRef(classLoaderClass);
        env->DeleteLocalRef(classLoader);
        return nullptr;
    }

    std::string dottedName(className);
    for(char &c : dottedName) {
        if(c == '/') c = '.';
    }
    jstring classNameJava = env->NewStringUTF(dottedName.c_str());
    jobject classObject =
        env->CallObjectMethod(classLoader, loadClass, classNameJava);

    env->DeleteLocalRef(classNameJava);
    env->DeleteLocalRef(classLoaderClass);
    env->DeleteLocalRef(classLoader);

    if(env->ExceptionCheck() || classObject == nullptr) {
        ClearJniException(env);
        return nullptr;
    }

    return static_cast<jclass>(classObject);
}

jobject GetGodotActivity(JNIEnv *env) {
    if(env == nullptr) return nullptr;

    jobject appContext = GetApplicationContextLocal(env);
    if(appContext == nullptr) return nullptr;

    jclass godotClass = FindClassWithAppClassLoader(env,
                                                    "org/godotengine/godot/Godot");
    if(godotClass == nullptr) {
        env->DeleteLocalRef(appContext);
        return nullptr;
    }

    jmethodID getInstance = env->GetStaticMethodID(
        godotClass, "getInstance",
        "(Landroid/content/Context;)Lorg/godotengine/godot/Godot;");
    if(getInstance == nullptr) {
        ClearJniException(env);
        env->DeleteLocalRef(godotClass);
        env->DeleteLocalRef(appContext);
        return nullptr;
    }

    jobject godot = env->CallStaticObjectMethod(godotClass, getInstance,
                                                appContext);
    env->DeleteLocalRef(appContext);
    if(env->ExceptionCheck() || godot == nullptr) {
        ClearJniException(env);
        env->DeleteLocalRef(godotClass);
        return nullptr;
    }

    jmethodID getActivity = env->GetMethodID(
        godotClass, "getActivity", "()Landroid/app/Activity;");
    if(getActivity == nullptr) {
        ClearJniException(env);
        env->DeleteLocalRef(godot);
        env->DeleteLocalRef(godotClass);
        return nullptr;
    }

    jobject activity = env->CallObjectMethod(godot, getActivity);
    env->DeleteLocalRef(godot);
    env->DeleteLocalRef(godotClass);
    if(env->ExceptionCheck() || activity == nullptr) {
        ClearJniException(env);
        return nullptr;
    }
    return activity;
}

void JNICALL GodotDialogCallback(JNIEnv * /* env */, jclass /* clazz */,
                                 jint result) {
    std::lock_guard<std::mutex> lk(MessageBoxLock);
    MsgBoxRet = static_cast<int>(result);
    MessageBoxCond.notify_all();
}

bool RegisterGodotDialogCallback(JNIEnv *env, jclass dialogUtilsClass) {
    static std::mutex registerMutex;
    static bool registered = false;

    if(env == nullptr || dialogUtilsClass == nullptr) return false;
    std::lock_guard<std::mutex> guard(registerMutex);
    if(registered) return true;

    JNINativeMethod methods[] = {
        {const_cast<char *>("dialogCallback"), const_cast<char *>("(I)V"),
         reinterpret_cast<void *>(&GodotDialogCallback)},
    };
    if(env->RegisterNatives(dialogUtilsClass, methods, 1) != JNI_OK) {
        ClearJniException(env);
        __android_log_print(ANDROID_LOG_WARN, "krkr2",
                            "RegisterNatives(DialogUtils.dialogCallback) failed");
        return false;
    }

    registered = true;
    return true;
}

jobjectArray NewJavaButtonArray(JNIEnv *env, unsigned int nButton,
                                const char **btnText) {
    if(env == nullptr) return nullptr;
    jclass strcls = env->FindClass("java/lang/String");
    if(strcls == nullptr) {
        ClearJniException(env);
        return nullptr;
    }

    jobjectArray btns = env->NewObjectArray(nButton, strcls, nullptr);
    env->DeleteLocalRef(strcls);
    if(btns == nullptr) {
        ClearJniException(env);
        return nullptr;
    }

    for(unsigned int i = 0; i < nButton; ++i) {
        const char *text = (btnText != nullptr && btnText[i] != nullptr)
            ? btnText[i]
            : "";
        jstring jstrBtn = env->NewStringUTF(text);
        if(jstrBtn == nullptr) {
            ClearJniException(env);
            continue;
        }
        env->SetObjectArrayElement(btns, i, jstrBtn);
        env->DeleteLocalRef(jstrBtn);
    }
    return btns;
}

jobject GetStaticObjectFieldByName(JNIEnv *env, jclass cls,
                                   const char *firstName,
                                   const char *secondName,
                                   const char *signature) {
    if(env == nullptr || cls == nullptr || signature == nullptr) return nullptr;

    jfieldID field = nullptr;
    if(firstName != nullptr) {
        field = env->GetStaticFieldID(cls, firstName, signature);
        if(field == nullptr) ClearJniException(env);
    }
    if(field == nullptr && secondName != nullptr) {
        field = env->GetStaticFieldID(cls, secondName, signature);
        if(field == nullptr) ClearJniException(env);
    }
    if(field == nullptr) return nullptr;

    jobject value = env->GetStaticObjectField(cls, field);
    if(env->ExceptionCheck() || value == nullptr) {
        ClearJniException(env);
        return nullptr;
    }
    return value;
}

int WaitForMessageBoxResult() {
    std::unique_lock<std::mutex> lk(MessageBoxLock);
    while(MsgBoxRet == -2) {
        MessageBoxCond.wait_for(lk, std::chrono::milliseconds(200));
        if(MsgBoxRet == -2) {
            TVPForceSwapBuffer(); // update opengl events
        }
    }
    return MsgBoxRet;
}

bool ShowGodotMessageBox(const char *pszText, const char *pszTitle,
                         unsigned int nButton, const char **btnText) {
    JNIEnv *env = JniHelper::getEnv();
    if(env == nullptr) return false;

    jobject activity = GetGodotActivity(env);
    if(activity == nullptr) return false;

    jclass dialogUtilsClass = FindClassWithAppClassLoader(
        env, "org/godotengine/godot/utils/DialogUtils");
    if(dialogUtilsClass == nullptr) {
        env->DeleteLocalRef(activity);
        return false;
    }

    if(!RegisterGodotDialogCallback(env, dialogUtilsClass)) {
        env->DeleteLocalRef(dialogUtilsClass);
        env->DeleteLocalRef(activity);
        return false;
    }

    jobject companion = GetStaticObjectFieldByName(
        env, dialogUtilsClass, "INSTANCE", "Companion",
        "Lorg/godotengine/godot/utils/DialogUtils$Companion;");
    if(companion == nullptr) {
        env->DeleteLocalRef(dialogUtilsClass);
        env->DeleteLocalRef(activity);
        return false;
    }

    jclass companionClass = env->GetObjectClass(companion);
    if(companionClass == nullptr) {
        ClearJniException(env);
        env->DeleteLocalRef(companion);
        env->DeleteLocalRef(dialogUtilsClass);
        env->DeleteLocalRef(activity);
        return false;
    }

    jmethodID showDialog = env->GetMethodID(
        companionClass, "showDialog$lib_templateDebug",
        "(Landroid/app/Activity;Ljava/lang/String;Ljava/lang/String;"
        "[Ljava/lang/String;)V");
    if(showDialog == nullptr) {
        ClearJniException(env);
        env->DeleteLocalRef(companionClass);
        env->DeleteLocalRef(companion);
        env->DeleteLocalRef(dialogUtilsClass);
        env->DeleteLocalRef(activity);
        return false;
    }

    jstring jstrTitle = env->NewStringUTF(pszTitle != nullptr ? pszTitle : "");
    jstring jstrText = env->NewStringUTF(pszText != nullptr ? pszText : "");
    jobjectArray btns = NewJavaButtonArray(env, nButton, btnText);
    if(jstrTitle == nullptr || jstrText == nullptr || btns == nullptr) {
        ClearJniException(env);
        if(jstrTitle != nullptr) env->DeleteLocalRef(jstrTitle);
        if(jstrText != nullptr) env->DeleteLocalRef(jstrText);
        if(btns != nullptr) env->DeleteLocalRef(btns);
        env->DeleteLocalRef(companionClass);
        env->DeleteLocalRef(companion);
        env->DeleteLocalRef(dialogUtilsClass);
        env->DeleteLocalRef(activity);
        return false;
    }

    MsgBoxRet = -2;
    env->CallVoidMethod(companion, showDialog, activity, jstrTitle, jstrText,
                        btns);
    const bool ok = !env->ExceptionCheck();
    ClearJniException(env);

    env->DeleteLocalRef(jstrTitle);
    env->DeleteLocalRef(jstrText);
    env->DeleteLocalRef(btns);
    env->DeleteLocalRef(companionClass);
    env->DeleteLocalRef(companion);
    env->DeleteLocalRef(dialogUtilsClass);
    env->DeleteLocalRef(activity);
    return ok;
}

} // namespace

int TVPShowSimpleMessageBox(const char *pszText, const char *pszTitle,
                            unsigned int nButton, const char **btnText) {
    // main.gd intercepts this control log and displays it through OS.alert().
    ttstr logMsg = ttstr("[ALERT_DIALOG] ") + (pszTitle ? pszTitle : "AetherKiri") + ttstr(" | ") + (pszText ? pszText : "");
    TVPAddImportantLog(logMsg);
    return 0;
}

#ifdef __ANDROID__
extern "C" JNIEXPORT void JNICALL
Java_org_tvp_kirikiri2_KR2Activity_nativeOnMessageBoxResult(
    JNIEnv* /* env */, jclass /* clazz */, jint result) {
    std::lock_guard<std::mutex> lk(MessageBoxLock);
    MsgBoxRet = static_cast<int>(result);
    MessageBoxCond.notify_all();
}

extern "C" JNIEXPORT void JNICALL
Java_org_tvp_kirikiri2_KR2Activity_nativeOnInputBoxResult(
    JNIEnv* /* env */, jclass /* clazz */, jint result, jstring text) {
    std::lock_guard<std::mutex> lk(MessageBoxLock);
    MsgBoxRet = static_cast<int>(result);
    MessageBoxRetText = JniHelper::jstring2string(text);
    MessageBoxCond.notify_all();
}

extern "C" JNIEXPORT void JNICALL
Java_org_godotengine_godot_utils_DialogUtils_dialogCallback(
    JNIEnv* env, jclass clazz, jint result) {
    GodotDialogCallback(env, clazz, result);
}
#endif

int TVPShowSimpleMessageBox(const ttstr &text, const ttstr &caption,
                            const std::vector<ttstr> &vecButtons) {
    tTJSNarrowStringHolder pszText(text.c_str());
    tTJSNarrowStringHolder pszTitle(caption.c_str());
    std::vector<const char *> btnText;
    btnText.reserve(vecButtons.size());
    std::vector<std::string> btnTextHold;
    btnTextHold.reserve(vecButtons.size());
    for(const ttstr &btn : vecButtons) {
        btnTextHold.emplace_back(btn.AsStdString());
        btnText.emplace_back(btnTextHold.back().c_str());
    }
    return TVPShowSimpleMessageBox(pszText, pszTitle, btnText.size(),
                                   btnText.empty() ? nullptr : &btnText[0]);
}

int TVPShowSimpleInputBox(ttstr &text, const ttstr &caption,
                          const ttstr &prompt,
                          const std::vector<ttstr> &vecButtons) {
    JniMethodInfo methodInfo;
    if(JniHelper::getStaticMethodInfo(
           methodInfo, "org/tvp/kirikiri2/KR2Activity", "ShowInputBox",
           "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/"
           "String;[Ljava/"
           "lang/"
           "String;)V")) {
        jstring jstrTitle =
            methodInfo.env->NewStringUTF(caption.AsStdString().c_str());
        jstring jstrText =
            methodInfo.env->NewStringUTF(text.AsStdString().c_str());
        jstring jstrPrompt =
            methodInfo.env->NewStringUTF(prompt.AsStdString().c_str());
        jclass strcls = methodInfo.env->FindClass("java/lang/String");
        jobjectArray btns =
            methodInfo.env->NewObjectArray(vecButtons.size(), strcls, nullptr);
        for(unsigned int i = 0; i < vecButtons.size(); ++i) {
            jstring jstrBtn = methodInfo.env->NewStringUTF(
                vecButtons[i].AsStdString().c_str());
            methodInfo.env->SetObjectArrayElement(btns, i, jstrBtn);
            methodInfo.env->DeleteLocalRef(jstrBtn);
        }

        MsgBoxRet = -2;
        methodInfo.env->CallStaticVoidMethod(methodInfo.classID,
                                             methodInfo.methodID, jstrTitle,
                                             jstrPrompt, jstrText, btns);

        methodInfo.env->DeleteLocalRef(jstrTitle);
        methodInfo.env->DeleteLocalRef(jstrText);
        methodInfo.env->DeleteLocalRef(jstrPrompt);
        methodInfo.env->DeleteLocalRef(btns);
        methodInfo.env->DeleteLocalRef(methodInfo.classID);

        std::unique_lock<std::mutex> lk(MessageBoxLock);
        while(MsgBoxRet == -2) {
            MessageBoxCond.wait_for(lk, std::chrono::milliseconds(200));
            if(MsgBoxRet == -2) {
                TVPForceSwapBuffer(); // update opengl events
            }
        }
        text = MessageBoxRetText;
        return MsgBoxRet;
    }
    return -1;
}

extern std::string Android_ShowInputDialog(const char *pszTitle,
                                           const char *pszInitText);
extern std::string Android_ShowFileBrowser(const char *pszTitle,
                                           const char *pszInitDir,
                                           bool showEditor);
extern ttstr TVPGetAppPath();
extern ttstr TVPGetLocallyAccessibleName(const ttstr &name);

std::vector<ttstr> Android_GetExternalStoragePath() {
    auto to_file_uri = [](const std::string &path) -> std::string {
        if(path.empty()) return "file:///";
        if(path[0] == '/') return "file://" + path;
        return "file:///" + path;
    };
    static std::vector<ttstr> ret;
    if(ret.empty()) {
        std::vector<std::string> pathlist;
        GetExternalStoragePath(pathlist);
        for(const std::string &path : pathlist) {
            ret.emplace_back(to_file_uri(path));
        }
    }
    return ret;
}

ttstr Android_GetInternalStoragePath() {
    auto to_file_uri = [](const std::string &path) -> std::string {
        if(path.empty()) return "file:///";
        if(path[0] == '/') return "file://" + path;
        return "file:///" + path;
    };
    static ttstr strPath;
    if(strPath.IsEmpty()) {
        strPath = to_file_uri(GetInternalStoragePath());
    }
    return strPath;
}

ttstr Android_GetApkStoragePath() {
    auto to_file_uri = [](const std::string &path) -> std::string {
        if(path.empty()) return "file:///";
        if(path[0] == '/') return "file://" + path;
        return "file:///" + path;
    };
    static ttstr strPath;
    if(strPath.IsEmpty()) {
        strPath = to_file_uri(GetApkStoragePath());
    }
    return strPath;
}

struct _eventQueueNode {
    std::function<void()> func;
    _eventQueueNode *prev;
    _eventQueueNode *next;
};

static std::atomic<_eventQueueNode *> _lastQueuedEvents(nullptr);
static void _processEvents(float) {
    _eventQueueNode *q = _lastQueuedEvents.exchange(nullptr);
    if(q) {
        q->next = nullptr;
        while(q->prev) {
            q->prev->next = q;
            q = q->prev;
        }
    }
    while(q) {
        q->func();
        _eventQueueNode *nq = q->next;
        delete q;
        q = nq;
    }
}

void Android_PushEvents(const std::function<void()> &func) {
    _eventQueueNode *node = new _eventQueueNode;
    node->func = func;
    node->next = nullptr;
    node->prev = nullptr;
    while(!_lastQueuedEvents.compare_exchange_weak(node->prev, node)) {
    }
}

void TVPCheckAndSendDumps(const std::string &dumpdir,
                          const std::string &packageName,
                          const std::string &versionStr);
bool TVPCheckStartupArg() {
    // check dump
    TVPCheckAndSendDumps(Android_GetDumpStoragePath(), GetPackageName(),
                         TVPGetPackageVersionString());

    // Event processing is now driven by engine_api tick loop.
    // The old cocos2d Scheduler-based _processEvents registration
    // has been removed. Events are processed via Android_PushEvents()
    // which is called during engine_tick.

    return false;
}

void TVPControlAdDialog(int adType, int arg1, int arg2) {
    JniMethodInfo methodInfo;
    if(JniHelper::getStaticMethodInfo(methodInfo,
                                      "org/tvp/kirikiri2/KR2Activity",
                                      "MessageController", "(III)V")) {
        methodInfo.env->CallStaticVoidMethod(
            methodInfo.classID, methodInfo.methodID, adType, arg1, arg2);
        methodInfo.env->DeleteLocalRef(methodInfo.classID);
    }
}

static int _GetAndroidSDKVersion() {
    JNIEnv *pEnv = JniHelper::getEnv();
    if(pEnv == nullptr) return 0;
    jclass classID = pEnv->FindClass("android/os/Build$VERSION");
    if(classID == nullptr) return 0;
    jfieldID idSDK_INT = pEnv->GetStaticFieldID(classID, "SDK_INT", "I");
    if(idSDK_INT == nullptr) {
        pEnv->DeleteLocalRef(classID);
        return 0;
    }
    int result = pEnv->GetStaticIntField(classID, idSDK_INT);
    pEnv->DeleteLocalRef(classID);
    return result;
}
static int GetAndroidSDKVersion() {
    static int result = _GetAndroidSDKVersion();
    return result;
}

static bool IsLollipop() { return GetAndroidSDKVersion() >= 21; }

static bool IsOreo() { return GetAndroidSDKVersion() >= 26; }

// 这里的编码就要使用locale编码了
// 因为调用了tjstr参数的函数 tjstr处理不了utf-8编码
bool TVPCheckStartupPath(const std::string &path) {
    // check writing permission first
    size_t pos = path.find_last_of('/');
    if(pos == std::string::npos)
        return false;
    std::string parent = path.substr(0, pos);
    if(JniHelper::getEnv() == nullptr) {
        std::string savePath = parent + "/savedata";
        if(!TVPCheckExistentLocalFolder(savePath)) {
            TVPCreateFolders(savePath);
        }
        return access(parent.c_str(), W_OK) == 0 ||
               TVPCheckExistentLocalFolder(savePath);
    }

    JniMethodInfo methodInfo;
    bool success = false;
    if(JniHelper::getStaticMethodInfo(
           methodInfo, "org/tvp/kirikiri2/KR2Activity", "isWritableNormalOrSaf",
           "(Ljava/lang/String;)Z")) {
        jstring jstrPath = methodInfo.env->NewStringUTF(parent.c_str());
        success = methodInfo.env->CallStaticBooleanMethod(
            methodInfo.classID, methodInfo.methodID, jstrPath);
        methodInfo.env->DeleteLocalRef(jstrPath);
        if(success) {
            parent += "/savedata";
            if(!TVPCheckExistentLocalFolder(parent)) {
                TVPCreateFolders(parent);
            }
            jstrPath = methodInfo.env->NewStringUTF(parent.c_str());
            success = methodInfo.env->CallStaticBooleanMethod(
                methodInfo.classID, methodInfo.methodID, jstrPath);
            methodInfo.env->DeleteLocalRef(jstrPath);
        }
    }

    if(!success) {
        std::vector<std::string> paths;
        paths.emplace_back(GetInternalStoragePath());
        GetExternalStoragePath(paths);
        std::string msg =
            LocaleConfigManager::GetInstance()->GetText("use_internal_path");
        if(!paths.empty()) {
            pos = msg.find("%1");
            if(pos != std::string::npos) {
                msg = msg.replace(msg.begin() + pos, msg.begin() + pos + 2,
                                  paths.back());
            }
        }
        std::vector<ttstr> btns;
        btns.emplace_back(
            LocaleConfigManager::GetInstance()->GetText("continue_run"));
        bool isLOLLIPOP = IsLollipop();
        if(isLOLLIPOP)
            btns.emplace_back(LocaleConfigManager::GetInstance()->GetText(
                "get_sdcard_permission"));
        else
            btns.emplace_back(
                LocaleConfigManager::GetInstance()->GetText("cancel"));
        int result = TVPShowSimpleMessageBox(
            msg,
            LocaleConfigManager::GetInstance()->GetText("readonly_storage"),
            btns);

        if(result != 0)
            return false;
    }

    // check adreno GPU issue
    // 	if
    // (IndividualConfigManager::GetInstance()->GetValue<std::string>("renderer",
    // "software") == "opengl") {
    // TVPOnOpenGLRendererSelected(false);
    // 	}
    return true;
}

// POSIX fallback: recursively create directories
static bool _posix_mkdirs(const std::string &path) {
    if (path.empty()) return false;
    std::string tmp = path;
    for (size_t i = 1; i < tmp.size(); ++i) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            mkdir(tmp.c_str(), 0755);
            tmp[i] = '/';
        }
    }
    return mkdir(tmp.c_str(), 0755) == 0 || errno == EEXIST;
}

bool TVPCreateFolders(const ttstr &folder) {
    JniMethodInfo methodInfo;
    if(JniHelper::getStaticMethodInfo(
           methodInfo, "org/tvp/kirikiri2/KR2Activity", "CreateFolders",
           "(Ljava/lang/String;)Z")) {
        jstring jstr =
            methodInfo.env->NewStringUTF(folder.AsStdString().c_str());
        bool ret = methodInfo.env->CallStaticBooleanMethod(
            methodInfo.classID, methodInfo.methodID, jstr);
        methodInfo.env->DeleteLocalRef(jstr);
        methodInfo.env->DeleteLocalRef(methodInfo.classID);
        return ret;
    }
    // POSIX fallback for embedded host mode (no KR2Activity)
    return _posix_mkdirs(folder.AsStdString());
}

static bool TVPWriteDataToFileJava(const std::string &filename,
                                   const void *data, unsigned int size) {
    JniMethodInfo methodInfo;
    if(JniHelper::getStaticMethodInfo(methodInfo,
                                      "org/tvp/kirikiri2/KR2Activity",
                                      "WriteFile", "(Ljava/lang/String;[B)Z")) {
        bool ret = false;
        int retry = 3;
        do {
            jstring jstr = methodInfo.env->NewStringUTF(filename.c_str());
            jbyteArray arr = methodInfo.env->NewByteArray(size);
            methodInfo.env->SetByteArrayRegion(arr, 0, size, (jbyte *)data);
            ret = methodInfo.env->CallStaticBooleanMethod(
                methodInfo.classID, methodInfo.methodID, jstr, arr);
            methodInfo.env->DeleteLocalRef(arr);
            methodInfo.env->DeleteLocalRef(jstr);
            methodInfo.env->DeleteLocalRef(methodInfo.classID);
        } while(access(filename.c_str(), F_OK) != 0 && --retry);
        return ret;
    }
    return false;
}

bool TVPWriteDataToFile(const ttstr &filepath, const void *data,
                        unsigned int size) {
    std::string filename = filepath.AsStdString();
    while(access(filename.c_str(), F_OK) == 0) {
        // for number filename suffix issue
        time_t t = time(nullptr);
        std::vector<char> buffer;
        buffer.resize(filename.size() + 32);
        sprintf(&buffer.front(), "%s.%d.bak", filename.c_str(), (int)t);
        std::string tempname = &buffer.front();
        if(rename(filename.c_str(), tempname.c_str()) == 0) {
            // file api is OK
            FILE *fp = fopen(filename.c_str(), "wb");
            if(fp) {
                bool ret = fwrite(data, 1, size, fp) == size;
                fclose(fp);
                remove(tempname.c_str());
                return ret;
            }
        }
        bool ret = TVPWriteDataToFileJava(filename, data, size);
        if(access(tempname.c_str(), F_OK) == 0) {
            TVPDeleteFile(tempname);
        }
        return ret;
    }
    FILE *fp = fopen(filename.c_str(), "wb");
    if(fp) {
        // file api is OK
        int writed = fwrite(data, 1, size, fp);
        fclose(fp);
        return writed == size;
    }
    return TVPWriteDataToFileJava(filename, data, size);
}

std::string TVPGetCurrentLanguage() {
    JniMethodInfo t;
    std::string ret("");

    if(JniHelper::getStaticMethodInfo(t, "org/tvp/kirikiri2/KR2Activity",
                                      "getLocaleName",
                                      "()Ljava/lang/String;")) {
        jstring str =
            (jstring)t.env->CallStaticObjectMethod(t.classID, t.methodID);
        t.env->DeleteLocalRef(t.classID);
        ret = JniHelper::jstring2string(str);
        t.env->DeleteLocalRef(str);
    }

    // Fallback for embedded host mode: use standard Java Locale API
    if(ret.empty()) {
        ret = TVPGetDeviceLanguage();
    }

    return ret;
}

void TVPExitApplication(int code) {
    TVPDeliverCompactEvent(TVP_COMPACT_LEVEL_MAX);
    // Guard: only recycle textures if the render manager was already
    // initialised.  Calling TVPIsSoftwareRenderManager() when no
    // render manager exists would trigger OpenGL init (which needs a
    // valid GL context that may not exist in embedded host mode).
    try {
        if(!TVPIsSoftwareRenderManager())
            iTVPTexture2D::RecycleProcess();
    } catch(...) {
        // Ignore – we are shutting down anyway.
    }
    JniMethodInfo t;
    if(JniHelper::getStaticMethodInfo(t, "org/tvp/kirikiri2/KR2Activity",
                                      "exit", "()V")) {
        t.env->CallStaticVoidMethod(t.classID, t.methodID);
        t.env->DeleteLocalRef(t.classID);
        return;
    }
    // In Android/embedded host mode, forcing process-wide exit can race with
    // worker threads and trigger FORTIFY mutex checks.
    (void)code;
}

void TVPHideIME() {
    JniMethodInfo methodInfo;
    if(JniHelper::getStaticMethodInfo(methodInfo,
                                      "org/tvp/kirikiri2/KR2Activity",
                                      "hideTextInput", "()V")) {
        methodInfo.env->CallStaticVoidMethod(methodInfo.classID,
                                             methodInfo.methodID);
    }
}

void TVPShowIME(int x, int y, int w, int h) {
    JniMethodInfo methodInfo;
    if(JniHelper::getStaticMethodInfo(methodInfo,
                                      "org/tvp/kirikiri2/KR2Activity",
                                      "showTextInput", "(IIII)V")) {
        methodInfo.env->CallStaticVoidMethod(methodInfo.classID,
                                             methodInfo.methodID, x, y, w, h);
    }
}

void TVPProcessInputEvents() {}

bool TVPDeleteFile(const std::string &filename) {
    JniMethodInfo methodInfo;
    if(JniHelper::getStaticMethodInfo(methodInfo,
                                      "org/tvp/kirikiri2/KR2Activity",
                                      "DeleteFile", "(Ljava/lang/String;)Z")) {
        jstring jstr = methodInfo.env->NewStringUTF(filename.c_str());
        bool ret = methodInfo.env->CallStaticBooleanMethod(
            methodInfo.classID, methodInfo.methodID, jstr);
        methodInfo.env->DeleteLocalRef(jstr);
        methodInfo.env->DeleteLocalRef(methodInfo.classID);
        return ret;
    }
    // POSIX fallback for embedded host mode
    return remove(filename.c_str()) == 0;
}

bool TVPRenameFile(const std::string &from, const std::string &to) {
    JniMethodInfo methodInfo;
    if(JniHelper::getStaticMethodInfo(
           methodInfo, "org/tvp/kirikiri2/KR2Activity", "RenameFile",
           "(Ljava/lang/String;Ljava/lang/String;)Z")) {
        jstring jstr = methodInfo.env->NewStringUTF(from.c_str());
        jstring jstr2 = methodInfo.env->NewStringUTF(to.c_str());
        bool ret = methodInfo.env->CallStaticBooleanMethod(
            methodInfo.classID, methodInfo.methodID, jstr, jstr2);
        methodInfo.env->DeleteLocalRef(jstr);
        methodInfo.env->DeleteLocalRef(jstr2);
        methodInfo.env->DeleteLocalRef(methodInfo.classID);
        return ret;
    }
    // POSIX fallback for embedded host mode
    return rename(from.c_str(), to.c_str()) == 0;
}

tjs_uint32 TVPGetRoughTickCount32() {
    tjs_uint32 uptime = 0;
    struct timespec on;
    if(clock_gettime(CLOCK_MONOTONIC, &on) == 0)
        uptime = on.tv_sec * 1000 + on.tv_nsec / 1000000;
    return uptime;
}

bool TVP_stat(const tjs_char *name, tTVP_stat &s) {
    tTJSNarrowStringHolder holder(name);
    return TVP_stat(holder, s);
}

#undef st_atime
#undef st_ctime
#undef st_mtime
// int stat64(const char* __path, struct stat64* __buf)
// __INTRODUCED_IN(21); // force link it !
bool TVP_stat(const char *name, tTVP_stat &s) {
    struct stat t;
    // static_assert(sizeof(t.st_size) == 4, "");
    static_assert(sizeof(t.st_size) == 8, "");
    bool ret = !stat(name, &t);
    s.st_mode = t.st_mode;
    s.st_size = t.st_size;
    s.st_atime = t.st_atim.tv_sec;
    s.st_mtime = t.st_mtim.tv_sec;
    s.st_ctime = t.st_ctim.tv_sec;
    return ret;
}

void TVPSendToOtherApp(const std::string &filename) {}
