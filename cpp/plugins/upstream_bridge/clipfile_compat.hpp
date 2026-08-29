#pragma once

// The CLIP plugin in krkrz_dev is a complete parser/writer, but its original
// tp_stub exposes an `iTJSBinaryStream` interface with a self-destructing
// `Destruct()` method.  Aether deliberately owns a different RAII stream ABI.
// This shim keeps the upstream business sources untouched and adapts only the
// lifetime/stream boundary before they are included by the bridge translation
// units.

#include "krkrz_aether_compat.hpp"
#include "StorageIntf.h"

// The upstream clipfile translation units include the broad plugin
// `tp_stub.h` after this shim.  That header also publishes Aether's
// `iTJSBinaryStream` compatibility alias (via FontServiceIntf.h).  Prime it
// before introducing the scoped macro below; otherwise the macro rewrites
// the alias declaration itself and Clang/GCC report a conflicting typedef.
#include "tp_stub.h"
#include "EventIntf.h"

// clipfile's Windows-oriented wrapper uses a few compatibility names that
// are not part of Aether's portable plug-in headers.  Keep those shims local
// to the bridge translation units; the upstream checkout itself remains
// untouched and the names are undefined by the bridge source files below.
#ifndef _WIN32
#include "GraphicsLoaderIntf.h"
using BITMAPINFOHEADER = TVP_WIN_BITMAPINFOHEADER;
#endif

#ifndef S_OK
#define S_OK TJS_S_OK
#define AETHER_CLIP_UNDEF_S_OK 1
#endif

#ifndef TJS_strrchr
inline tjs_char *AetherClipStrrchr(const tjs_char *value, tjs_char needle) {
    if(!value)
        return nullptr;
    const tjs_char *last = nullptr;
    for(const tjs_char *cursor = value; *cursor; ++cursor) {
        if(*cursor == needle)
            last = cursor;
    }
    return const_cast<tjs_char *>(last);
}
#define TJS_strrchr AetherClipStrrchr
#define AETHER_CLIP_UNDEF_TJS_STRRCHR 1
#endif

// This is the *upstream-shaped* abstract stream type.  It derives from the
// Aether stream so a covariant `Open()` return remains valid at the storage
// boundary, but it deliberately has no constructor requirements: clipparse's
// own CLIPMemoryStream subclasses it with its native (buffer,size) ctor.
class AetherClipBinaryStream : public tTJSBinaryStream {
public:
    ~AetherClipBinaryStream() override = default;

    // krkrz callers use this instead of delete.  Aether's base class is RAII,
    // so the default implementation simply destroys the concrete object.
    virtual void Destruct() { delete this; }
};

class AetherClipWrappedStream final : public AetherClipBinaryStream {
public:
    explicit AetherClipWrappedStream(tTJSBinaryStream *inner) : Inner(inner) {}
    ~AetherClipWrappedStream() override { delete Inner; }

    tjs_uint64 Seek(tjs_int64 offset, int whence) override {
        return Inner ? Inner->Seek(offset, whence) : 0;
    }
    tjs_uint Read(void *buffer, tjs_uint size) override {
        return Inner ? Inner->Read(buffer, size) : 0;
    }
    tjs_uint Write(const void *buffer, tjs_uint size) override {
        return Inner ? Inner->Write(buffer, size) : 0;
    }
    void SetEndOfStorage() override {
        if(Inner)
            Inner->SetEndOfStorage();
    }
    tjs_uint64 GetSize() override { return Inner ? Inner->GetSize() : 0; }

private:
    tTJSBinaryStream *Inner = nullptr;
};

inline AetherClipBinaryStream *AetherClipCreateStream(const ttstr &name,
                                                       tjs_uint32 flags = 0) {
    tTJSBinaryStream *source = ::TVPCreateStream(name, flags);
    if(!source)
        return nullptr;
    try {
        return new AetherClipWrappedStream(source);
    } catch(...) {
        delete source;
        throw;
    }
}

// These macros are intentionally scoped to a bridge translation unit.  They
// must not leak into Aether's public headers or change the engine ABI.
#define iTJSBinaryStream AetherClipBinaryStream
#define TVPCreateStream AetherClipCreateStream
