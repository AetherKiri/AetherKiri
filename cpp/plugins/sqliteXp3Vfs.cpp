#include "CharacterSet.h"
#include "StorageIntf.h"
#include "ncbind.hpp"
#include "sqlite/sqlite3.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <random>
#include <vector>

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

#define NCB_MODULE_NAME TJS_W("sqlite3_xp3_vfs.dll")

namespace {

struct Xp3SqliteFile {
    const sqlite3_io_methods *methods;
    tTJSBinaryStream *stream;
};

ttstr fromUtf8(const char *text) {
    if(!text)
        return ttstr();
    const tjs_int length = TVPUtf8ToWideCharString(text, nullptr);
    std::vector<tjs_char> wide(static_cast<size_t>(length) + 1, 0);
    if(length > 0)
        TVPUtf8ToWideCharString(text, wide.data());
    return ttstr(wide.data());
}

int xp3Close(sqlite3_file *id) {
    auto *file = reinterpret_cast<Xp3SqliteFile *>(id);
    delete file->stream;
    file->stream = nullptr;
    return SQLITE_OK;
}

int xp3Read(sqlite3_file *id, void *buffer, int amount, sqlite3_int64 offset) {
    auto *file = reinterpret_cast<Xp3SqliteFile *>(id);
    if(!file->stream)
        return SQLITE_IOERR_READ;

    const tjs_uint64 pos = file->stream->Seek(
        static_cast<tjs_uint64>(offset), TJS_BS_SEEK_SET);
    if(pos != static_cast<tjs_uint64>(offset))
        return SQLITE_IOERR_SEEK;

    const tjs_uint read = file->stream->Read(buffer, amount);
    if(read == static_cast<tjs_uint>(amount))
        return SQLITE_OK;

    if(read < static_cast<tjs_uint>(amount))
        std::memset(static_cast<char *>(buffer) + read, 0,
                    static_cast<size_t>(amount) - read);
    return SQLITE_IOERR_SHORT_READ;
}

int xp3Write(sqlite3_file *, const void *, int, sqlite3_int64) {
    return SQLITE_READONLY;
}

int xp3Truncate(sqlite3_file *, sqlite3_int64) { return SQLITE_READONLY; }

int xp3Sync(sqlite3_file *, int) { return SQLITE_OK; }

int xp3FileSize(sqlite3_file *id, sqlite3_int64 *size) {
    auto *file = reinterpret_cast<Xp3SqliteFile *>(id);
    if(!file->stream)
        return SQLITE_IOERR_FSTAT;
    *size = static_cast<sqlite3_int64>(file->stream->GetSize());
    return SQLITE_OK;
}

int xp3Lock(sqlite3_file *, int) { return SQLITE_OK; }

int xp3Unlock(sqlite3_file *, int) { return SQLITE_OK; }

int xp3CheckReservedLock(sqlite3_file *, int *reserved) {
    if(reserved)
        *reserved = 0;
    return SQLITE_OK;
}

int xp3FileControl(sqlite3_file *, int op, void *arg) {
    if(op == SQLITE_FCNTL_LOCKSTATE) {
        *static_cast<int *>(arg) = 0;
        return SQLITE_OK;
    }
    return SQLITE_NOTFOUND;
}

int xp3SectorSize(sqlite3_file *) { return 512; }

int xp3DeviceCharacteristics(sqlite3_file *) { return 0; }

const sqlite3_io_methods kXp3IoMethods = {
    1,
    xp3Close,
    xp3Read,
    xp3Write,
    xp3Truncate,
    xp3Sync,
    xp3FileSize,
    xp3Lock,
    xp3Unlock,
    xp3CheckReservedLock,
    xp3FileControl,
    xp3SectorSize,
    xp3DeviceCharacteristics,
};

int xp3Open(sqlite3_vfs *, const char *name, sqlite3_file *id, int flags,
            int *outFlags) {
    if((flags & (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                 SQLITE_OPEN_DELETEONCLOSE)) != 0) {
        return SQLITE_READONLY;
    }

    std::memset(id, 0, sizeof(Xp3SqliteFile));
    auto *file = reinterpret_cast<Xp3SqliteFile *>(id);
    file->methods = &kXp3IoMethods;

    try {
        file->stream = TVPCreateStream(fromUtf8(name), TJS_BS_READ);
    } catch(...) {
        file->stream = nullptr;
        return SQLITE_CANTOPEN;
    }

    if(!file->stream)
        return SQLITE_CANTOPEN;
    if(outFlags)
        *outFlags = SQLITE_OPEN_READONLY;
    return SQLITE_OK;
}

int xp3Delete(sqlite3_vfs *, const char *, int) { return SQLITE_READONLY; }

int xp3Access(sqlite3_vfs *, const char *name, int flags, int *result) {
    bool exists = false;
    if(flags == SQLITE_ACCESS_EXISTS || flags == SQLITE_ACCESS_READ)
        exists = TVPIsExistentStorage(fromUtf8(name));
    if(result)
        *result = exists ? 1 : 0;
    return SQLITE_OK;
}

int xp3FullPathname(sqlite3_vfs *, const char *relative, int fullSize,
                    char *full) {
    sqlite3_snprintf(fullSize, full, "%s", relative ? relative : "");
    return SQLITE_OK;
}

int xp3Randomness(sqlite3_vfs *, int bytes, char *out) {
    static std::mt19937 rng(static_cast<unsigned int>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    for(int i = 0; i < bytes; ++i)
        out[i] = static_cast<char>(rng() & 0xff);
    return bytes;
}

int xp3Sleep(sqlite3_vfs *, int microseconds) {
    sqlite3_sleep((microseconds + 999) / 1000);
    return ((microseconds + 999) / 1000) * 1000;
}

int xp3CurrentTime(sqlite3_vfs *, double *now) {
    sqlite3_int64 current = 0;
    sqlite3_vfs *defaultVfs = sqlite3_vfs_find(nullptr);
    if(defaultVfs && defaultVfs->xCurrentTimeInt64 &&
       defaultVfs->xCurrentTimeInt64(defaultVfs, &current) == SQLITE_OK) {
        *now = current / 86400000.0;
        return SQLITE_OK;
    }

    *now = 2440587.5 + static_cast<double>(std::time(nullptr)) / 86400.0;
    return SQLITE_OK;
}

sqlite3_vfs *getXp3Vfs() {
    static sqlite3_vfs vfs = {
        1,
        static_cast<int>(sizeof(Xp3SqliteFile)),
        4096,
        nullptr,
        "xp3",
        nullptr,
        xp3Open,
        xp3Delete,
        xp3Access,
        xp3FullPathname,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        xp3Randomness,
        xp3Sleep,
        xp3CurrentTime,
        nullptr,
    };
    return &vfs;
}

bool g_registered = false;

void registerXp3Vfs() {
    if(!g_registered) {
        sqlite3_vfs_register(getXp3Vfs(), 0);
        g_registered = true;
    }
}

void unregisterXp3Vfs() {
    if(g_registered) {
        sqlite3_vfs_unregister(getXp3Vfs());
        g_registered = false;
    }
}

} // namespace

void AetherKiriRegisterXp3SqliteVfs() { registerXp3Vfs(); }

NCB_PRE_REGIST_CALLBACK(registerXp3Vfs);
NCB_POST_UNREGIST_CALLBACK(unregisterXp3Vfs);
