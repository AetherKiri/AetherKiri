#include "FontImpl.h"
#include <ft2build.h>
#include FT_TRUETYPE_IDS_H
#include FT_SFNT_NAMES_H
#include FT_FREETYPE_H
#include "StorageIntf.h"
#include "SysInitIntf.h"
#include "DebugIntf.h"
#include "MsgIntf.h"
#include <map>
#include <cmath>
#include "Application.h"
#include "FontSystem.h"
#include "Platform.h"
#include "ConfigManager/IndividualConfigManager.h"
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <fstream>
#include <vector>
#include <algorithm>
#include <cctype>
#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>
#include "StorageImpl.h"
#include "BinaryStream.h"
#include <spdlog/spdlog.h>
#ifdef __APPLE__
#include <TargetConditionals.h>
#include <CoreFoundation/CoreFoundation.h>
#if TARGET_OS_IOS
#include <CoreText/CoreText.h>
#endif
#endif

tTJSHashTable<ttstr, TVPFontNamePathInfo, tTVPttstrHash> TVPFontNames;
static ttstr TVPDefaultFontName;
const ttstr &TVPGetDefaultFontName() { return TVPDefaultFontName; }
extern FontSystem *TVPFontSystem;

bool TVPSetDefaultFontName(const ttstr &fontName) {
    if(fontName.IsEmpty() || !TVPFindFont(fontName))
        return false;

    TVPDefaultFontName = fontName;
    if(TVPFontSystem)
        TVPFontSystem->SetDefaultFontName(fontName);
    spdlog::info("default font face set: {}", fontName.AsStdString());
    return true;
}

void TVPGetAllFontList(std::vector<ttstr> &list) {
    for(auto it = TVPFontNames.GetFirst(); !it.IsNull(); ++it) {
        list.push_back(it.GetKey());
    }
}

static FT_Library TVPFontLibrary;

const FT_Library TVPGetFontLibrary() {

    if(!TVPFontLibrary) {
        FT_Error error = FT_Init_FreeType(&TVPFontLibrary);
        if(error)
            TVPThrowExceptionMessage(
                (ttstr(TJS_W("Initialize FreeType failed, error = ")) +
                 TJSIntegerToString((tjs_int)error))
                    .c_str());
        TVPInitFontNames();
    }
    return TVPFontLibrary;
}

void TVPReleaseFontLibrary() {
    if(TVPFontLibrary) {
        FT_Done_FreeType(TVPFontLibrary);
    }
}
//---------------------------------------------------------------------------
static int TVPInternalEnumFonts(
    FT_Byte *pBuf, int buflen, const ttstr &FontPath,
    const std::function<tTJSBinaryStream *(TVPFontNamePathInfo *)> &getter,
    std::vector<ttstr> *fontNames = nullptr) {
    unsigned int faceCount = 0;
    FT_Face fontface;
    FT_Error error =
        FT_New_Memory_Face(TVPGetFontLibrary(), pBuf, buflen, 0, &fontface);
    if(error) {
        TVPAddLog(ttstr(TJS_W("Load Font \"") + FontPath + "\" failed (" +
                        TJSIntegerToString((int)error) + ")"));
        return faceCount;
    }
    int nFaceNum = fontface->num_faces;
    for(int i = 0; i < nFaceNum; ++i) {
        if(i > 0) {
            if(FT_New_Memory_Face(TVPGetFontLibrary(), pBuf, buflen, i,
                                  &fontface)) {
                continue;
            }
        }
        if(FT_IS_SCALABLE(fontface)) {
            FT_UInt namecount = FT_Get_Sfnt_Name_Count(fontface);
            int addCount = 0;
            for(FT_UInt j = 0; j < namecount; ++j) {
                FT_SfntName name;
                if(FT_Get_Sfnt_Name(fontface, j, &name)) {
                    continue;
                }
                if(name.name_id != TT_NAME_ID_FONT_FAMILY) {
                    continue;
                }
                if(name.platform_id != TT_PLATFORM_MICROSOFT) {
                    continue;
                }
                switch(name.language_id) { // for CJK names
                    case TT_MS_LANGID_JAPANESE_JAPAN:
                    case TT_MS_LANGID_CHINESE_GENERAL:
                    case TT_MS_LANGID_CHINESE_TAIWAN:
                    case TT_MS_LANGID_CHINESE_PRC:
                    case TT_MS_LANGID_CHINESE_HONG_KONG:
                    case TT_MS_LANGID_CHINESE_SINGAPORE:
                    case TT_MS_LANGID_KOREAN_EXTENDED_WANSUNG_KOREA:
                    case TT_MS_LANGID_KOREAN_JOHAB_KOREA:
                        break;
                    default:
                        continue;
                }
                ttstr fontname;
                if(name.encoding_id == TT_MS_ID_UNICODE_CS) {
                    std::vector<tjs_char> tmp;
                    int namelen = name.string_len / 2;
                    tmp.resize(namelen + 1);
                    for(int k = 0; k < namelen; ++k) {
                        tmp[k] = (name.string[k * 2] << 8) |
                            (name.string[k * 2 + 1]);
                    }
                    fontname = &tmp.front();
                } else {
                    continue;
                }
                TVPFontNamePathInfo info;
                info.Path = FontPath;
                info.Index = i;
                info.Getter = getter;
                TVPFontNames.Add(fontname, info);
                if(fontNames &&
                   std::find(fontNames->begin(), fontNames->end(), fontname) ==
                       fontNames->end()) {
                    fontNames->emplace_back(fontname);
                }
                addCount = 1;
            }
            /*if (!addCount)*/ {
                ttstr fontname((tjs_nchar *)fontface->family_name);
                TVPFontNamePathInfo info;
                info.Path = FontPath;
                info.Index = i;
                info.Getter = getter;
                TVPFontNames.Add(fontname, info);
                if(fontNames &&
                   std::find(fontNames->begin(), fontNames->end(), fontname) ==
                       fontNames->end()) {
                    fontNames->emplace_back(fontname);
                }
            }
            ++faceCount;
        }

        FT_Done_Face(fontface);
    }
    return faceCount;
}

/**
 *  only support little word!!!
 * @param FontPath font path str
 * @return load failed return 0, otherwise > 0
 */
int TVPEnumFontsProc(const ttstr &FontPath,
                     std::vector<ttstr> *fontNames) {
    if(!TVPIsExistentStorageNoSearch(FontPath)) {
        return 0;
    }

    tTJSBinaryStream *Stream = TVPCreateStream(FontPath, TJS_BS_READ);
    if(!Stream) {
        return 0;
    }
    int bufflen = Stream->GetSize();
    std::vector<FT_Byte> buf;
    buf.resize(bufflen);
    Stream->ReadBuffer(&buf.front(), bufflen);
    delete Stream;
    return TVPInternalEnumFonts(&buf.front(), bufflen, FontPath, nullptr,
                                fontNames);
}

tTJSBinaryStream *TVPCreateFontStream(const ttstr &fontname) {
    TVPFontNamePathInfo *info = TVPFindFont(fontname);
    if(!info) {
        info = TVPFontNames.Find(TVPDefaultFontName);
        if(!info)
            return nullptr;
    }
    if(info->Getter) {
        return info->Getter(info);
    }
    return TVPCreateBinaryStreamForRead(info->Path, TJS_W(""));
}

//---------------------------------------------------------------------------
#ifdef __ANDROID__
extern std::vector<ttstr> Android_GetExternalStoragePath();
extern ttstr Android_GetInternalStoragePath();
extern ttstr Android_GetApkStoragePath();
#endif
void TVPInitFontNames() {
    static bool TVPFontNamesInit = false;
    // enumlate all fonts
    if(TVPFontNamesInit)
        return;
    TVPFontNamesInit = true;
#ifdef __ANDROID__
    std::vector<ttstr> pathlist = Android_GetExternalStoragePath();
#endif
    auto tryReadFont = [](const std::string &path) -> std::vector<uint8_t> {
        std::ifstream ifs(path, std::ios::binary | std::ios::ate);
        if(!ifs.is_open())
            return {};
        auto size = ifs.tellg();
        if(size <= 0)
            return {};
        std::vector<uint8_t> data(static_cast<size_t>(size));
        ifs.seekg(0);
        ifs.read(reinterpret_cast<char *>(data.data()), size);
        return data;
    };

    auto tryLoadFontDirect = [&tryReadFont](const std::string &path,
                                            const std::string &label) -> bool {
        auto fdata = tryReadFont(path);
        if(fdata.empty())
            return false;
        spdlog::info("loaded font: {}", path);
        return TVPInternalEnumFonts(
                   fdata.data(), fdata.size(), label.c_str(),
                   [&tryReadFont](TVPFontNamePathInfo *info) -> tTJSBinaryStream * {
                       auto d = tryReadFont(info->Path.AsStdString());
                       if(d.empty())
                           return nullptr;
                       auto *ret = new tTVPMemoryStream();
                       ret->WriteBuffer(d.data(), d.size());
                       ret->SetPosition(0);
                       return ret;
                   }) > 0;
    };

    auto tryLoadFontStorageOrDirect = [&tryLoadFontDirect](const ttstr &path) -> bool {
        if(path.IsEmpty())
            return false;
        if(TVPEnumFontsProc(path))
            return true;
        std::string nativePath = path.AsStdString();
        return tryLoadFontDirect(nativePath, nativePath);
    };

    auto joinNativePath = [](std::string folder,
                             const std::string &leaf) -> std::string {
        if(folder.empty())
            return leaf;
        if(folder.back() != '/' && folder.back() != '\\')
            folder.push_back('/');
        folder += leaf;
        return folder;
    };

    auto tryLoadDefaultFontFromNativeDir =
        [&tryLoadFontDirect, &joinNativePath](const std::string &folder) -> bool {
            if(folder.empty())
                return false;
            static const char *kDefaultFontNames[] = {
                "default.ttf", "default.ttc", "default.otf", "default.otc",
                nullptr
            };
            for(const char **name = kDefaultFontNames; *name; ++name) {
                const std::string path = joinNativePath(folder, *name);
                if(tryLoadFontDirect(path, path))
                    return true;
            }
            return false;
        };

#ifdef __APPLE__
    auto appleBundleResourceDirs = []() -> std::vector<std::string> {
        std::vector<std::string> dirs;
        CFBundleRef bundle = CFBundleGetMainBundle();
        if(!bundle)
            return dirs;
        CFURLRef resourceURL = CFBundleCopyResourcesDirectoryURL(bundle);
        if(!resourceURL)
            return dirs;
        char path[PATH_MAX] = {};
        if(CFURLGetFileSystemRepresentation(resourceURL, true,
                                            reinterpret_cast<UInt8 *>(path),
                                            sizeof(path))) {
            dirs.emplace_back(path);
            std::string fontDir(path);
            if(!fontDir.empty() && fontDir.back() != '/')
                fontDir.push_back('/');
            fontDir += "fonts";
            dirs.emplace_back(std::move(fontDir));
        }
        CFRelease(resourceURL);
        return dirs;
    };
#endif

    auto isFontFilePath = [](std::string path) -> bool {
        auto slash = path.find_last_of("/\\");
        if(slash != std::string::npos)
            path = path.substr(slash + 1);
        auto dot = path.find_last_of('.');
        if(dot == std::string::npos)
            return false;
        std::string ext = path.substr(dot);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return ext == ".ttf" || ext == ".ttc" || ext == ".otf" || ext == ".otc";
    };

    auto enumDirectFontDir = [&tryLoadFontDirect, &isFontFilePath](const ttstr &folder) -> int {
        std::string dirPath = folder.AsStdString();
        if(dirPath.empty())
            return 0;
        DIR *dir = opendir(dirPath.c_str());
        if(!dir)
            return 0;
        int count = 0;
        while(auto *entry = readdir(dir)) {
            std::string name = entry->d_name;
            if(name == "." || name == ".." || !isFontFilePath(name))
                continue;
            std::string fullPath = dirPath;
            if(!fullPath.empty() && fullPath.back() != '/')
                fullPath.push_back('/');
            fullPath += name;
            struct stat st {};
            if(stat(fullPath.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
                continue;
            if(tryLoadFontDirect(fullPath, fullPath))
                ++count;
        }
        closedir(dir);
        return count;
    };

    auto enumStorageFontDir = [&tryLoadFontStorageOrDirect, &isFontFilePath](const ttstr &folder) {
        ttstr nativeFolder = folder;
        try {
            TVPGetLocalName(nativeFolder);
        } catch(...) {
            nativeFolder = folder;
        }
        TVPGetLocalFileListAt(nativeFolder, [&](const ttstr &, tTVPLocalFileInfo *s) {
            if(!(s->Mode & S_IFREG) || !s->NativeName)
                return;
            std::string nativeName = s->NativeName;
            if(!isFontFilePath(nativeName))
                return;
            ttstr fullPath = nativeFolder;
            if(fullPath.GetLastChar() != TJS_W('/'))
                fullPath += TJS_W("/");
            fullPath += ttstr(nativeName.c_str());
            tryLoadFontStorageOrDirect(fullPath);
        });
    };

    do {
        tTJSVariant defaultFontOpt;
        if(TVPGetCommandLine(TJS_W("default_font"), &defaultFontOpt)) {
            ttstr defaultFontPath(defaultFontOpt);
            if(tryLoadFontStorageOrDirect(defaultFontPath))
                break;
        }

        ttstr userFont =
            IndividualConfigManager::GetInstance()->GetValue<std::string>(
                "default_font", "");
        if(!userFont.IsEmpty() && tryLoadFontStorageOrDirect(userFont))
            break;

#ifdef __APPLE__
        for(const auto &folder : appleBundleResourceDirs()) {
            if(tryLoadDefaultFontFromNativeDir(folder))
                break;
        }
        if(TVPFontNames.GetCount() > 0)
            break;
#endif

        if(tryLoadFontStorageOrDirect(TVPGetAppPath() + "default.ttf"))
            break;
        if(tryLoadFontStorageOrDirect(TVPGetAppPath() + "default.ttc"))
            break;
        if(tryLoadFontStorageOrDirect(TVPGetAppPath() + "default.otf"))
            break;
        if(tryLoadFontStorageOrDirect(TVPGetAppPath() + "default.otc"))
            break;
#if defined(__ANDROID__)
        int fontCount = 0;
        for(const ttstr &path : pathlist) {
            fontCount += tryLoadFontStorageOrDirect(path + "/default.ttf");
            if(fontCount)
                break;
        }
        if(fontCount)
            break;

        if(tryLoadFontStorageOrDirect(Android_GetInternalStoragePath() + "/default.ttf"))
            break;

#elif defined(WIN32)
        if(TVPEnumFontsProc(TJS_W("file://./c/Windows/Fonts/msyh.ttf")))
            break;
        if(TVPEnumFontsProc(TJS_W("file://./c/Windows/Fonts/simhei.ttf")))
            break;
#elif defined(__APPLE__)
#if TARGET_OS_IOS
        // iOS: system fonts cannot be accessed via the engine's file://
        // storage system due to sandbox and path-normalization (lowercase).
        // Use direct POSIX reads via tryReadFont below instead.
        // (falls through to the "internal storage" block)
#else
        // macOS: system fonts are accessible via the engine's storage layer
        if(TVPEnumFontsProc(TJS_W("file://./System/Library/Fonts/PingFang.ttc")))
            break;
        if(TVPEnumFontsProc(
               TJS_W("file://./System/Library/Fonts/Hiragino Sans GB.ttc")))
            break;
        if(TVPEnumFontsProc(
               TJS_W("file://./System/Library/Fonts/Supplemental/Arial Unicode.ttf")))
            break;
#endif
#endif
        { // from internal storage (or system fonts on iOS)
#if defined(__ANDROID__)
            if(tryLoadFontDirect("/system/fonts/NotoSansSC-Regular.otf",
                                 "/system/fonts/NotoSansSC-Regular.otf"))
                break;
            if(tryLoadFontDirect("/system/fonts/DroidSansFallback.ttf",
                                 "/system/fonts/DroidSansFallback.ttf"))
                break;
            if(tryLoadFontDirect("/system/fonts/NotoSansCJK-Regular.ttc",
                                 "/system/fonts/NotoSansCJK-Regular.ttc"))
                break;
#endif

#if defined(__APPLE__) && TARGET_OS_IOS
            // iOS: use CoreText API to get system font data (sandbox-safe).
            {
                // Preferred font names in order: PingFang SC (CN), Hiragino Sans (JP)
                static const char *preferredFonts[] = {
                    "PingFangSC-Regular",
                    "HiraginoSans-W3",
                    "HiraMinProN-W3",
                    nullptr
                };
                bool loaded = false;
                for (const char **fname = preferredFonts; *fname && !loaded; ++fname) {
                    CFStringRef fontName = CFStringCreateWithCString(
                        kCFAllocatorDefault, *fname, kCFStringEncodingUTF8);
                    if (!fontName) continue;

                    CTFontRef ctFont = CTFontCreateWithName(fontName, 12.0, nullptr);
                    CFRelease(fontName);
                    if (!ctFont) continue;

                    // Get the font URL from the CTFont descriptor
                    CTFontDescriptorRef desc = CTFontCopyFontDescriptor(ctFont);
                    CFURLRef fontURL = desc
                        ? (CFURLRef)CTFontDescriptorCopyAttribute(desc, kCTFontURLAttribute)
                        : nullptr;

                    if (fontURL) {
                        char pathBuf[1024];
                        if (CFURLGetFileSystemRepresentation(fontURL, true,
                                (UInt8 *)pathBuf, sizeof(pathBuf))) {
                            std::string fontPath(pathBuf);
                            spdlog::info("iOS CoreText font path: {}", fontPath);
                            if (tryLoadFontDirect(fontPath, fontPath)) {
                                loaded = true;
                            }
                        }
                        CFRelease(fontURL);
                    }
                    if (desc) CFRelease(desc);
                    CFRelease(ctFont);
                }
                if (loaded) break;
            }
#endif

            auto data = tryReadFont("NotoSansCJK-Regular.ttc");
            if (data.empty()) {
                data = tryReadFont("fonts/NotoSansCJK-Regular.ttc");
            }
            if (data.empty()) {
                spdlog::warn("internal font file not found: NotoSansCJK-Regular.ttc");
            } else if(TVPInternalEnumFonts(
                   data.data(), data.size(), "NotoSansCJK-Regular.ttc",
                   [&tryReadFont](TVPFontNamePathInfo *info) -> tTJSBinaryStream * {
                       auto fdata = tryReadFont(info->Path.AsStdString());
                       if (fdata.empty()) return nullptr;
                       auto *ret = new tTVPMemoryStream();
                       ret->WriteBuffer(fdata.data(), fdata.size());
                       ret->SetPosition(0);
                       return ret;
                   }))
                break;
        }
    } while(false);
    if(TVPFontNames.GetCount() > 0) {
        // set default fontface name
        TVPDefaultFontName = TVPFontNames.GetLast().GetKey();
    }

    tTJSVariant fontDirOpt;
    if(TVPGetCommandLine(TJS_W("font_dir"), &fontDirOpt)) {
        enumDirectFontDir(ttstr(fontDirOpt));
    }

    // check exePath + "/fonts/*.ttf"
    {
#ifdef __ANDROID__
        enumDirectFontDir(Android_GetInternalStoragePath() + "/fonts");
        for(const ttstr &path : pathlist) {
            enumDirectFontDir(path + "/fonts");
        }
#endif
        enumStorageFontDir(TVPGetAppPath() + "/fonts");
    }

    if(TVPDefaultFontName.IsEmpty() && TVPFontNames.GetCount() > 0) {
        TVPDefaultFontName = TVPFontNames.GetLast().GetKey();
    }

    if(TVPDefaultFontName.IsEmpty()) {
        TVPShowSimpleMessageBox(
            ("Could not found any font.\nPlease ensure that at "
             "least \"default.ttf\" exists"),
            "Exception Occured");
    }
}
//---------------------------------------------------------------------------
TVPFontNamePathInfo *TVPFindFont(const ttstr &fontname) {
    // check existence of font
    TVPInitFontNames();

    TVPFontNamePathInfo *info = nullptr;
    if(!fontname.IsEmpty() && fontname[0] == TJS_W('@')) { // vertical version
        info = TVPFontNames.Find(fontname.c_str() + 1);
    }
    if(!info) {
        info = TVPFontNames.Find(fontname);
    }
    return info;
}

tjs_uint32 tTVPttstrHash::Make(const ttstr &val) {
    const tjs_char *ptr = val.c_str();
    if(*ptr == 0)
        return 0;
    tjs_uint32 v = 0;
    while(*ptr) {
        v += *ptr;
        v += (v << 10);
        v ^= (v >> 6);
        ptr++;
    }
    v += (v << 3);
    v ^= (v >> 11);
    v += (v << 15);
    if(!v)
        v = (tjs_uint32)-1;
    return v;
}
