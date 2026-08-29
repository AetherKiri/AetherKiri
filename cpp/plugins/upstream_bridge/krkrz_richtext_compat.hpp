#pragma once

// The richtext plug-in is consumed as source from the pinned krkrz_dev
// submodule. Its implementation uses the historical plug-in stream contract
// (`iTJSBinaryStream::Destruct()`), while Aether owns an RAII
// `TJS::tTJSBinaryStream`. Keep this translation-unit shim local to the
// bridges so neither public ABI nor the upstream checkout is changed.

#include "krkrz_aether_compat.hpp"
#include "StorageIntf.h"
#include "FontServiceIntf.h"

#ifndef S_OK
#define S_OK TJS_S_OK
#define AETHER_RICHTEXT_UNDEF_S_OK 1
#endif

class AetherRichTextBinaryStream : public TJS::tTJSBinaryStream {
public:
    ~AetherRichTextBinaryStream() override = default;

    // The upstream loader calls this method after consuming a stream. The
    // Aether base is RAII, so deleting the wrapper is the exact equivalent.
    virtual void Destruct() { delete this; }
};

class AetherRichTextWrappedStream final : public AetherRichTextBinaryStream {
public:
    explicit AetherRichTextWrappedStream(TJS::tTJSBinaryStream *inner)
        : Inner(inner) {}

    ~AetherRichTextWrappedStream() override { delete Inner; }

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
    TJS::tTJSBinaryStream *Inner = nullptr;
};

inline AetherRichTextBinaryStream *AetherRichTextCreateStream(
    const ttstr &name, tjs_uint32 flags = 0) {
    TJS::tTJSBinaryStream *inner = ::TVPCreateStream(name, flags);
    if(!inner)
        return nullptr;
    try {
        return new AetherRichTextWrappedStream(inner);
    } catch(...) {
        delete inner;
        throw;
    }
}

// `main.cpp` contains legacy integer-to-pointer probes. Aether's variant
// intentionally exposes the integer conversion as tTVInteger; map only the
// included upstream translation unit, never the public headers.
#define iTJSBinaryStream AetherRichTextBinaryStream
#define TVPCreateStream AetherRichTextCreateStream
#define tjs_intptr_t tTVInteger
