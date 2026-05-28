#include <minizip/ioapi.h>
#include <minizip/zip.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include <iomanip>
#include <condition_variable>
#include <map>
#include "Platform.h"
#include "ConfigManager/LocaleConfigManager.h"
#include "StorageImpl.h"

static void ClearDumps(const std::string &dumpdir,
                       std::vector<std::string> &allDumps) {
    for(const std::string &path : allDumps) {
        remove((dumpdir + "/" + path).c_str());
    }
    // allDumps.clear();
}

static std::map<std::string, tTVPMemoryStream *> _inmemFiles;

struct zlib_inmem_func64 : public zlib_filefunc64_def {
    static voidpf ZCALLBACK fopen64_file_func(voidpf opaque,
                                              const void *filename, int mode) {
        tTVPMemoryStream *str = new tTVPMemoryStream;
        _inmemFiles[(const char *)filename] = str;
        return str;
    }

    static uLong ZCALLBACK fread_file_func(voidpf opaque, voidpf stream,
                                            void *buf, uLong size) {
        tTVPMemoryStream *str = (tTVPMemoryStream *)stream;
        return str->Read(buf, size);
    }

    static uLong ZCALLBACK fwrite_file_func(voidpf opaque, voidpf stream,
                                             const void *buf, uLong size) {
        tTVPMemoryStream *str = (tTVPMemoryStream *)stream;
        return str->Write(buf, size);
    }

    static ZPOS64_T ZCALLBACK ftell64_file_func(voidpf opaque, voidpf stream) {
        tTVPMemoryStream *str = (tTVPMemoryStream *)stream;
        return str->GetPosition();
    }

    static long ZCALLBACK fseek64_file_func(voidpf opaque, voidpf stream,
                                            ZPOS64_T offset, int origin) {
        int fseek_origin = TJS_BS_SEEK_SET;
        switch(origin) {
            case ZLIB_FILEFUNC_SEEK_CUR:
                fseek_origin = TJS_BS_SEEK_CUR;
                break;
            case ZLIB_FILEFUNC_SEEK_END:
                fseek_origin = TJS_BS_SEEK_END;
                break;
            case ZLIB_FILEFUNC_SEEK_SET:
                fseek_origin = TJS_BS_SEEK_SET;
                break;
            default:
                return -1;
        }
        tTVPMemoryStream *str = (tTVPMemoryStream *)stream;
        str->Seek(offset, fseek_origin);
        return 0;
    }

    static int ZCALLBACK fclose_file_func(voidpf opaque, voidpf stream) {
        return 0;
    }

    static int ZCALLBACK ferror_file_func(voidpf opaque, voidpf stream) {
        return 0;
    }

    zlib_inmem_func64() {
        zopen64_file = fopen64_file_func;
        zread_file = fread_file_func;
        zwrite_file = fwrite_file_func;
        ztell64_file = ftell64_file_func;
        zseek64_file = fseek64_file_func;
        zclose_file = fclose_file_func;
        zerror_file = ferror_file_func;
        opaque = nullptr;
    }
};

static zlib_filefunc64_def *GetZlibIOFunc() {
    static zlib_inmem_func64 func;
    return &func;
}

static std::string url_encode(const std::string &value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for(std::string::value_type c : value) {
        // Keep alphanumeric and other accepted characters intact
        if(isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
            continue;
        }

        // Any other characters are percent-encoded
        escaped << '%' << std::setw(2) << int((unsigned char)c);
    }

    return escaped.str();
}

// 将 struct tm 转换为 MS-DOS 时间格式
uint32_t convert_to_dos_date(const struct tm *time) {
    uint16_t dos_date = 0;
    uint16_t dos_time = 0;

    // 日期部分：年份（从1980年开始）、月份、天数
    dos_date = ((time->tm_year - 80) << 9) | // 年份偏移，从 1980 年算起
        ((time->tm_mon + 1) << 5) | // 月份（1-12）
        (time->tm_mday); // 天数（1-31）

    // 时间部分：小时、分钟、秒（以2秒为单位）
    dos_time = (time->tm_hour << 11) | // 小时（0-23）
        (time->tm_min << 5) | // 分钟（0-59）
        (time->tm_sec / 2); // 秒（以2秒为单位）

    // 合并日期和时间为 32 位
    return ((uint32_t)dos_date << 16) | dos_time;
}

#define FLAG_UTF8 (1 << 11)
static void SendDumps(std::string dumpdir, std::vector<std::string> allDumps,
                      std::string packageName, std::string versionStr) {
    // Dump upload will be re-implemented via platform-native HTTP or host channel.
    spdlog::warn("SendDumps: HTTP upload is currently disabled.");
    spdlog::warn("SendDumps: {} dump file(s) will be deleted without uploading.", allDumps.size());
    for (const std::string &filename : allDumps) {
        std::string fullpath = dumpdir + "/" + filename;
        remove(fullpath.c_str());
    }
}

void TVPCheckAndSendDumps(const std::string &dumpdir,
                          const std::string &packageName,
                          const std::string &versionStr) {
    std::vector<std::string> allDumps;
    TVPListDir(dumpdir, [&](const std::string &name, int mask) {
        if(mask & (S_IFREG | S_IFDIR)) {
            if(name.size() <= 4)
                return;
            if(name.substr(name.size() - 4) != ".dmp")
                return;
            allDumps.emplace_back(name);
        }
    });
    if(!allDumps.empty()) {
        std::string title =
            LocaleConfigManager::GetInstance()->GetText("crash_report");
        std::string msgfmt =
            LocaleConfigManager::GetInstance()->GetText("crash_report_msg");
        char buf[256];
        sprintf(buf, msgfmt.c_str(), allDumps.size());
        if(TVPShowSimpleMessageBoxYesNo(buf, title) == 0) {
            static std::thread dumpthread;
            dumpthread =
                std::thread([dumpdir, allDumps, packageName, versionStr] {
                    SendDumps(dumpdir, allDumps, packageName, versionStr);
                });
        } else {
            ClearDumps(dumpdir, allDumps);
        }
    }
}