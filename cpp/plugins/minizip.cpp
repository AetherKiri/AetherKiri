#include "ncbind.hpp"
#include "tp_stub.h"
#include "UtilStreams.h"

#include <array>
#include <ctime>
#include <map>
#include <memory>
#include <set>
#include <string>

#include <minizip/unzip.h>
#include <minizip/zip.h>

#define NCB_MODULE_NAME TJS_W("minizip.dll")

namespace {

constexpr tjs_uint kBufferSize = 64 * 1024;
constexpr uLong kUtf8Flag = 1 << 11;

std::string toUtf8(const ttstr &value) {
    std::string out;
    const tjs_int len = TVPWideCharToUtf8String(value.c_str(), nullptr);
    if(len <= 0)
        return out;
    out.resize(static_cast<size_t>(len));
    TVPWideCharToUtf8String(value.c_str(), out.data());
    return out;
}

ttstr localPathForRead(const ttstr &name) {
    ttstr placed = TVPGetPlacedPath(name);
    if(placed.IsEmpty())
        TVPThrowExceptionMessage((name + TJS_W(" not exists.")).c_str());
    return TVPGetLocallyAccessibleName(placed);
}

ttstr localPathForWrite(const ttstr &name) {
    ttstr local = TVPNormalizeStorageName(name);
    TVPGetLocalName(local);
    return local;
}

bool splitZipStorageName(const ttstr &name, ttstr &domain, ttstr &path) {
    const tjs_char *raw = name.c_str();
    const tjs_char *slash = TJS_strchr(raw, TJS_W('/'));
    if(!slash)
        return false;
    domain = ttstr(raw, slash - raw);
    path = ttstr(slash + 1);
    return !domain.IsEmpty() && !path.IsEmpty();
}

void setProp(iTJSDispatch2 *object, const tjs_char *name, const tTJSVariant &value) {
    tTJSVariant copy(value);
    object->PropSet(TJS_MEMBERENSURE, name, nullptr, &copy, object);
}

} // namespace

class Zip {
public:
    Zip() = default;
    ~Zip() { close(); }

    static tjs_error open(tTJSVariant *, tjs_int numparams, tTJSVariant **param,
                          Zip *self) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        if(!self)
            return TJS_E_NATIVECLASSCRASH;

        self->close();
        ttstr filename = param[0]->GetString();
        const int overwrite =
            numparams > 1 ? static_cast<tjs_int>(*param[1]) : 0;
        if(overwrite == 0 && !TVPGetPlacedPath(filename).IsEmpty())
            TVPThrowExceptionMessage((filename + TJS_W(" exists.")).c_str());

        const int append =
            overwrite == 2 ? APPEND_STATUS_ADDINZIP : APPEND_STATUS_CREATE;
        self->path = localPathForWrite(filename);
        self->zip = zipOpen64(toUtf8(self->path).c_str(), append);
        if(!self->zip)
            TVPThrowExceptionMessage((filename + TJS_W(" can't open.")).c_str());
        return TJS_S_OK;
    }

    void close() {
        if(zip) {
            zipClose(zip, nullptr);
            zip = nullptr;
        }
    }

    static tjs_error add(tTJSVariant *result, tjs_int numparams,
                         tTJSVariant **param, Zip *self) {
        if(numparams < 2)
            return TJS_E_BADPARAMCOUNT;
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        if(!self->zip)
            TVPThrowExceptionMessage(TJS_W("don't open zipfile"));

        ttstr src = param[0]->GetString();
        ttstr dest = param[1]->GetString();
        const int level =
            numparams > 2 && param[2]->Type() != tvtVoid
                ? static_cast<tjs_int>(*param[2])
                : Z_DEFAULT_COMPRESSION;
        if(numparams > 3 && param[3]->Type() == tvtString)
            TVPThrowExceptionMessage(
                TJS_W("minizip password encryption is not supported"));

        std::unique_ptr<tTJSBinaryStream> input(
            TVPCreateStream(TVPGetPlacedPath(src), TJS_BS_READ));
        if(!input)
            TVPThrowExceptionMessage((src + TJS_W(" not exists.")).c_str());

        zip_fileinfo info{};
        const std::time_t now = std::time(nullptr);
        std::tm localTime{};
        localtime_r(&now, &localTime);
        info.tmz_date.tm_year = localTime.tm_year + 1900;
        info.tmz_date.tm_mon = localTime.tm_mon;
        info.tmz_date.tm_mday = localTime.tm_mday;
        info.tmz_date.tm_hour = localTime.tm_hour;
        info.tmz_date.tm_min = localTime.tm_min;
        info.tmz_date.tm_sec = localTime.tm_sec;

        const std::string destUtf8 = toUtf8(dest);
        bool ok = zipOpenNewFileInZip4_64(
                      self->zip, destUtf8.c_str(), &info, nullptr, 0, nullptr,
                      0, nullptr, level != 0 ? Z_DEFLATED : 0, level, 0,
                      -MAX_WBITS, DEF_MEM_LEVEL, Z_DEFAULT_STRATEGY, nullptr, 0,
                      0, kUtf8Flag, 0) == ZIP_OK;
        if(ok) {
            std::array<tjs_uint8, kBufferSize> buffer{};
            tjs_uint read = 0;
            while((read = input->Read(buffer.data(), buffer.size())) > 0) {
                if(zipWriteInFileInZip(self->zip, buffer.data(), read) != ZIP_OK) {
                    ok = false;
                    break;
                }
            }
            zipCloseFileInZip(self->zip);
        }

        if(result)
            *result = ok;
        return TJS_S_OK;
    }

private:
    zipFile zip = nullptr;
    ttstr path;
};

class Unzip {
public:
    Unzip() = default;
    ~Unzip() { close(); }

    void open(const tjs_char *filename) {
        close();
        ttstr local = localPathForRead(filename);
        zip = unzOpen64(toUtf8(local).c_str());
        if(!zip)
            TVPThrowExceptionMessage((ttstr(filename) + TJS_W(" can't open.")).c_str());
    }

    void close() {
        if(zip) {
            unzClose(zip);
            zip = nullptr;
        }
    }

    static tjs_error list(tTJSVariant *result, tjs_int, tTJSVariant **,
                          Unzip *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        if(!self->zip)
            TVPThrowExceptionMessage(TJS_W("don't open zipfile"));

        iTJSDispatch2 *array = TJSCreateArrayObject();
        tjs_int index = 0;
        if(unzGoToFirstFile(self->zip) == UNZ_OK) {
            do {
                char filename[1024] = {};
                unz_file_info64 info{};
                if(unzGetCurrentFileInfo64(self->zip, &info, filename,
                                           sizeof(filename), nullptr, 0, nullptr,
                                           0) == UNZ_OK) {
                    iTJSDispatch2 *dict = TJSCreateDictionaryObject();
                    setProp(dict, TJS_W("filename"),
                            tTJSVariant(ttstr(std::string(filename).c_str())));
                    setProp(dict, TJS_W("uncompressed_size"),
                            tTJSVariant(static_cast<tjs_int64>(
                                info.uncompressed_size)));
                    setProp(dict, TJS_W("compressed_size"),
                            tTJSVariant(static_cast<tjs_int64>(
                                info.compressed_size)));
                    setProp(dict, TJS_W("crypted"),
                            tTJSVariant((info.flag & 1) != 0));
                    setProp(dict, TJS_W("deflated"),
                            tTJSVariant(info.compression_method == Z_DEFLATED));
                    setProp(dict, TJS_W("deflateLevel"),
                            tTJSVariant(static_cast<tjs_int>((info.flag & 0x6) /
                                                             2)));
                    setProp(dict, TJS_W("crc"),
                            tTJSVariant(static_cast<tjs_int64>(info.crc)));
                    tTJSVariant item(dict, dict);
                    array->PropSetByNum(TJS_MEMBERENSURE, index++, &item, array);
                    dict->Release();
                }
            } while(unzGoToNextFile(self->zip) == UNZ_OK);
        }

        if(result)
            *result = tTJSVariant(array, array);
        array->Release();
        return TJS_S_OK;
    }

    static tjs_error extract(tTJSVariant *result, tjs_int numparams,
                             tTJSVariant **param, Unzip *self) {
        if(numparams < 2)
            return TJS_E_BADPARAMCOUNT;
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        if(!self->zip)
            TVPThrowExceptionMessage(TJS_W("don't open zipfile"));
        if(numparams > 2 && param[2]->Type() == tvtString)
            TVPThrowExceptionMessage(
                TJS_W("minizip password encryption is not supported"));

        const std::string source = toUtf8(param[0]->GetString());
        bool ok = false;
        if(unzLocateFile(self->zip, source.c_str(), 1) == UNZ_OK &&
           unzOpenCurrentFile(self->zip) == UNZ_OK) {
            std::unique_ptr<tTJSBinaryStream> output(
                TVPCreateStream(param[1]->GetString(), TJS_BS_WRITE));
            if(!output)
                TVPThrowExceptionMessage(
                    (ttstr(param[1]->GetString()) + TJS_W(" can't open.")).c_str());
            std::array<tjs_uint8, kBufferSize> buffer{};
            int read = 0;
            while((read = unzReadCurrentFile(self->zip, buffer.data(),
                                             buffer.size())) > 0) {
                output->WriteBuffer(buffer.data(), static_cast<tjs_uint>(read));
            }
            ok = read == 0;
            unzCloseCurrentFile(self->zip);
        }

        if(result)
            *result = ok;
        return TJS_S_OK;
    }

private:
    unzFile zip = nullptr;
};

class ZipStorageMedia : public iTVPStorageMedia {
public:
    void AddRef() override { ++refCount; }

    void Release() override {
        if(refCount == 1)
            delete this;
        else
            --refCount;
    }

    void GetName(ttstr &name) override { name = TJS_W("zip"); }
    void NormalizeDomainName(ttstr &) override {}
    void NormalizePathName(ttstr &) override {}

    bool CheckExistentStorage(const ttstr &name) override {
        ttstr domain;
        ttstr path;
        if(!splitZipStorageName(name, domain, path))
            return false;
        return locate(domain, path);
    }

    tTJSBinaryStream *Open(const ttstr &name, tjs_uint32 flags) override {
        if((flags & TJS_BS_ACCESS_MASK) != TJS_BS_READ)
            TVPThrowExceptionMessage(TJS_W("zip storage is read-only"));

        ttstr domain;
        ttstr path;
        if(!splitZipStorageName(name, domain, path))
            TVPThrowExceptionMessage(TJS_W("invalid zip storage path"));

        std::vector<tjs_uint8> data = readEntry(domain, path);
        return new tTVPMemoryStream(data.empty() ? nullptr : data.data(),
                                    static_cast<tjs_uint>(data.size()));
    }

    void GetListAt(const ttstr &name, iTVPStorageLister *lister) override {
        ttstr domain;
        ttstr path;
        if(!splitZipStorageName(name, domain, path))
            return;

        auto it = mounts.find(domain);
        if(it == mounts.end())
            return;

        unzFile zip = unzOpen64(toUtf8(it->second).c_str());
        if(!zip)
            return;

        ttstr prefix = path;
        if(!prefix.IsEmpty() && prefix[prefix.GetLen() - 1] != TJS_W('/'))
            prefix += TJS_W('/');

        std::set<ttstr> names;
        if(unzGoToFirstFile(zip) == UNZ_OK) {
            do {
                char filename[1024] = {};
                unz_file_info64 info{};
                if(unzGetCurrentFileInfo64(zip, &info, filename,
                                           sizeof(filename), nullptr, 0, nullptr,
                                           0) == UNZ_OK) {
                    ttstr item(filename);
                    if(prefix.IsEmpty() ||
                       TJS_strncmp(item.c_str(), prefix.c_str(),
                                   prefix.GetLen()) == 0) {
                        const tjs_char *rest =
                            item.c_str() + (prefix.IsEmpty() ? 0 : prefix.GetLen());
                        const tjs_char *slash = TJS_strchr(rest, TJS_W('/'));
                        if(!slash && *rest)
                            names.insert(ttstr(rest));
                    }
                }
            } while(unzGoToNextFile(zip) == UNZ_OK);
        }
        unzClose(zip);

        for(const auto &item : names)
            lister->Add(item);
    }

    void GetLocallyAccessibleName(ttstr &name) override { name = TJS_W(""); }

    bool mount(const ttstr &domain, const ttstr &zipfile) {
        if(domain.IsEmpty())
            return false;
        mounts[domain] = localPathForRead(zipfile);
        return true;
    }

    bool unmount(const ttstr &domain) { return mounts.erase(domain) != 0; }

private:
    bool locate(const ttstr &domain, const ttstr &path) {
        auto it = mounts.find(domain);
        if(it == mounts.end())
            return false;
        unzFile zip = unzOpen64(toUtf8(it->second).c_str());
        if(!zip)
            return false;
        const std::string entry = toUtf8(path);
        const bool found = unzLocateFile(zip, entry.c_str(), 1) == UNZ_OK;
        unzClose(zip);
        return found;
    }

    std::vector<tjs_uint8> readEntry(const ttstr &domain, const ttstr &path) {
        auto it = mounts.find(domain);
        if(it == mounts.end())
            TVPThrowExceptionMessage(TJS_W("zip domain is not mounted"));

        unzFile zip = unzOpen64(toUtf8(it->second).c_str());
        if(!zip)
            TVPThrowExceptionMessage(TJS_W("zip archive can't open"));

        std::vector<tjs_uint8> data;
        const std::string entry = toUtf8(path);
        if(unzLocateFile(zip, entry.c_str(), 1) != UNZ_OK ||
           unzOpenCurrentFile(zip) != UNZ_OK) {
            unzClose(zip);
            TVPThrowExceptionMessage((path + TJS_W(" not found in zip")).c_str());
        }

        std::array<tjs_uint8, kBufferSize> buffer{};
        int read = 0;
        while((read = unzReadCurrentFile(zip, buffer.data(), buffer.size())) > 0) {
            data.insert(data.end(), buffer.begin(), buffer.begin() + read);
        }
        unzCloseCurrentFile(zip);
        unzClose(zip);
        return data;
    }

    tjs_uint refCount = 1;
    std::map<ttstr, ttstr> mounts;
};

ZipStorageMedia *g_zipStorage = nullptr;

class StoragesZip {
public:
    static bool mountZip(const tjs_char *name, const tjs_char *zipfile) {
        return g_zipStorage && g_zipStorage->mount(name, zipfile);
    }

    static bool unmountZip(const tjs_char *name) {
        return g_zipStorage && g_zipStorage->unmount(name);
    }
};

void InitZipStorage() {
    if(!g_zipStorage) {
        g_zipStorage = new ZipStorageMedia();
        TVPRegisterStorageMedia(g_zipStorage);
    }
}

void DoneZipStorage() {
    if(g_zipStorage) {
        TVPUnregisterStorageMedia(g_zipStorage);
        g_zipStorage->Release();
        g_zipStorage = nullptr;
    }
}

NCB_REGISTER_CLASS(Zip) {
    Constructor();
    RawCallback(TJS_W("open"), &Class::open, 0);
    NCB_METHOD(close);
    RawCallback(TJS_W("add"), &Class::add, 0);
}

NCB_REGISTER_CLASS(Unzip) {
    Constructor();
    NCB_METHOD(open);
    NCB_METHOD(close);
    RawCallback(TJS_W("list"), &Class::list, 0);
    RawCallback(TJS_W("extract"), &Class::extract, 0);
}

NCB_ATTACH_CLASS(StoragesZip, Storages) {
    NCB_METHOD(mountZip);
    NCB_METHOD(unmountZip);
}

NCB_PRE_REGIST_CALLBACK(InitZipStorage);
NCB_POST_UNREGIST_CALLBACK(DoneZipStorage);
