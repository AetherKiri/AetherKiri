#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <emscripten/heap.h>
#endif

#include <spdlog/spdlog.h>

#include "EventIntf.h"
#include "Platform.h"
#include "StorageImpl.h"
#include "SysInitImpl.h"
#include "tjsString.h"

namespace {

std::string WebWritableRoot() {
    return "/userfs";
}

bool EnsureDirectory(const std::string &path) {
    std::error_code ec;
    if(std::filesystem::exists(path, ec)) {
        return std::filesystem::is_directory(path, ec);
    }
    return std::filesystem::create_directories(path, ec);
}

} // namespace

bool TVPDeleteFile(const std::string &filename) {
    return unlink(filename.c_str()) == 0;
}

bool TVPRenameFile(const std::string &from, const std::string &to) {
    return rename(from.c_str(), to.c_str()) == 0;
}

bool TVPCreateFolders(const ttstr &folder);

static bool _TVPCreateFolders(const ttstr &folder) {
    if(folder.IsEmpty()) {
        return true;
    }

    if(TVPCheckExistentLocalFolder(folder)) {
        return true;
    }

    std::error_code ec;
    return std::filesystem::create_directories(folder.AsStdString(), ec) || !ec;
}

bool TVPCreateFolders(const ttstr &folder) {
    if(folder.IsEmpty()) {
        return true;
    }
    return _TVPCreateFolders(folder);
}

std::vector<std::string> TVPGetDriverPath() {
    EnsureDirectory(WebWritableRoot());
    return { WebWritableRoot(), "/tmp", "/" };
}

std::string TVPGetDefaultFileDir() {
    EnsureDirectory(WebWritableRoot());
    return WebWritableRoot();
}

std::vector<std::string> TVPGetAppStoragePath() {
    EnsureDirectory(WebWritableRoot());
    return { WebWritableRoot() };
}

void TVPGetMemoryInfo(TVPMemoryInfo &m) {
    std::memset(&m, 0, sizeof(m));
#ifdef __EMSCRIPTEN__
    m.VirtualTotal = static_cast<unsigned long>(emscripten_get_heap_size() / 1024);
    m.VirtualUsed = static_cast<unsigned long>(emscripten_get_heap_size() / 1024);
#endif
}

tjs_int TVPGetSystemFreeMemory() {
    return -1;
}

tjs_int TVPGetSelfUsedMemory() {
#ifdef __EMSCRIPTEN__
    return static_cast<tjs_int>(emscripten_get_heap_size() / (1024 * 1024));
#else
    return -1;
#endif
}

void TVPRelinquishCPU() {
}

void TVPSendToOtherApp(const std::string &filename) {}

bool TVPCheckStartupArg() {
    return false;
}

void TVPControlAdDialog(int, int, int) {}

void TVPForceSwapBuffer() {}

void TVPExitApplication(int code) {
    TVPDeliverCompactEvent(TVP_COMPACT_LEVEL_MAX);
    TVPTerminated = true;
    TVPTerminateCode = code;
}

bool TVPWriteDataToFile(const ttstr &filepath, const void *data,
                        unsigned int len) {
    FILE *handle = fopen(filepath.AsStdString().c_str(), "wb");
    if(handle == nullptr) {
        return false;
    }
    bool ret = fwrite(data, 1, len, handle) == len;
    fclose(handle);
    return ret;
}

bool TVPCheckStartupPath(const std::string &path) {
    return true;
}

std::string TVPGetCurrentLanguage() {
#ifdef __EMSCRIPTEN__
    char *language = emscripten_run_script_string(
        "typeof navigator !== 'undefined' && navigator.language ? navigator.language.replace('-', '_') : 'en_US'");
    if(language != nullptr && language[0] != '\0') {
        std::string result(language);
        free(language);
        return result;
    }
    if(language != nullptr) {
        free(language);
    }
#endif
    return "en_US";
}

void TVPProcessInputEvents() {}

int TVPShowSimpleInputBox(ttstr &text, const ttstr &caption,
                          const ttstr &prompt,
                          const std::vector<ttstr> &vecButtons) {
    spdlog::warn("TVPShowSimpleInputBox is not implemented on Web");
    return 0;
}

int TVPShowSimpleMessageBox(const ttstr &text, const ttstr &caption,
                            const std::vector<ttstr> &vecButtons) {
    spdlog::warn("TVPShowSimpleMessageBox(Web): {} - {}",
                 caption.AsStdString(), text.AsStdString());
    return 0;
}

extern "C" int TVPShowSimpleMessageBox(const char *pszText,
                                       const char *pszTitle,
                                       unsigned int nButton,
                                       const char **btnText) {
    std::vector<ttstr> vecButtons{};
    for(unsigned int i = 0; i < nButton; ++i) {
        vecButtons.emplace_back(btnText[i]);
    }
    return TVPShowSimpleMessageBox(pszText, pszTitle, vecButtons);
}

tjs_uint32 TVPGetRoughTickCount32() {
#ifdef __EMSCRIPTEN__
    auto web_now_ms = []() {
        double now = emscripten_get_now();
        if(!std::isfinite(now) || now < 0.0) {
            now = std::chrono::duration<double, std::milli>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count();
        }
        return now;
    };
    static const double start = web_now_ms();
    double elapsed = web_now_ms() - start;
    if(!std::isfinite(elapsed) || elapsed < 0.0) {
        elapsed = 0.0;
    }
    return static_cast<tjs_uint32>(
        static_cast<tjs_uint64>(elapsed) & 0xffffffffu);
#else
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<tjs_uint32>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
#endif
}

bool TVP_stat(const char *name, tTVP_stat &s) {
    struct stat t {};
    if(stat(name, &t) != 0) {
        return false;
    }

    s.st_mode = t.st_mode;
    s.st_size = t.st_size;
    s.st_atime = t.st_atim.tv_sec;
    s.st_mtime = t.st_mtim.tv_sec;
    s.st_ctime = t.st_ctim.tv_sec;
    return true;
}

bool TVP_stat(const tjs_char *name, tTVP_stat &s) {
    return TVP_stat(ttstr{ name }.AsStdString().c_str(), s);
}

bool TVP_utime(const char *name, time_t modtime) {
    timeval mt[2];
    mt[0].tv_sec = modtime;
    mt[0].tv_usec = 0;
    mt[1].tv_sec = modtime;
    mt[1].tv_usec = 0;
    return utimes(name, mt) == 0;
}
