#include "tjsCommHead.h"
#include "FontStream.h"

#include "MsgIntf.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <list>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using FontBytes = std::vector<std::uint8_t>;
using FontBytesPtr = std::shared_ptr<const FontBytes>;

constexpr std::size_t kDefaultMaxBytes = 64u * 1024u * 1024u;
constexpr std::size_t kDefaultMaxEntries = 8;
constexpr std::size_t kMaxSingleFontBytes = 32u * 1024u * 1024u;

class SharedFontStream final : public tTJSBinaryStream {
public:
    explicit SharedFontStream(FontBytesPtr bytes) : Bytes(std::move(bytes)) {}

    tjs_uint64 Seek(tjs_int64 offset, int whence) override {
        const tjs_int64 size = static_cast<tjs_int64>(Bytes->size());
        tjs_int64 base = 0;
        switch(whence) {
            case TJS_BS_SEEK_SET: base = 0; break;
            case TJS_BS_SEEK_CUR: base = static_cast<tjs_int64>(Position); break;
            case TJS_BS_SEEK_END: base = size; break;
            default: return Position;
        }
        // Avoid signed overflow when a malformed caller supplies an extreme
        // offset.  The stream contract says that an invalid seek leaves the
        // current position unchanged.
        if((offset > 0 && offset > size - base) ||
           (offset < 0 && offset < -base))
            return Position;
        const tjs_int64 next = base + offset;
        if(next >= 0 && next <= size)
            Position = static_cast<tjs_uint64>(next);
        return Position;
    }

    tjs_uint Read(void *buffer, tjs_uint readSize) override {
        if(!buffer || Position >= Bytes->size() || readSize == 0)
            return 0;
        const std::size_t remaining =
            Bytes->size() - static_cast<std::size_t>(Position);
        const std::size_t count = std::min<std::size_t>(remaining, readSize);
        std::copy_n(Bytes->data() + static_cast<std::size_t>(Position), count,
                    static_cast<std::uint8_t *>(buffer));
        Position += count;
        return static_cast<tjs_uint>(count);
    }

    tjs_uint Write(const void *, tjs_uint) override {
        TVPThrowExceptionMessage(TVPWriteError);
        return 0;
    }

    void SetEndOfStorage() override { TVPThrowExceptionMessage(TVPWriteError); }

    tjs_uint64 GetSize() override { return Bytes->size(); }

private:
    FontBytesPtr Bytes;
    tjs_uint64 Position = 0;
};

struct CacheEntry {
    FontBytesPtr Bytes;
    std::size_t Size = 0;
};

std::mutex CacheMutex;
std::unordered_map<std::string, std::weak_ptr<const FontBytes>> Cache;
std::list<CacheEntry> MRU;
std::size_t MaxBytes = kDefaultMaxBytes;
std::size_t MaxEntries = kDefaultMaxEntries;
std::size_t CurrentBytes = 0;

void Touch(const FontBytesPtr &bytes) {
    for(auto it = MRU.begin(); it != MRU.end(); ++it) {
        if(it->Bytes == bytes) {
            CurrentBytes -= it->Size;
            MRU.erase(it);
            break;
        }
    }
    MRU.push_back(CacheEntry{bytes, bytes->size()});
    CurrentBytes += bytes->size();
    while((MaxEntries == 0 || MRU.size() > MaxEntries) ||
          CurrentBytes > MaxBytes) {
        if(MRU.empty())
            break;
        CurrentBytes -= MRU.front().Size;
        MRU.pop_front();
    }
}

FontBytesPtr Find(const std::string &key) {
    auto it = Cache.find(key);
    if(it == Cache.end())
        return {};
    auto bytes = it->second.lock();
    if(!bytes)
        Cache.erase(it);
    return bytes;
}

} // namespace

tTJSBinaryStream *TVPCreateCachedFontStream(tTJSBinaryStream *source,
                                             const ttstr &cacheKey) {
    if(!source)
        return nullptr;

    const tjs_uint64 size = source->GetSize();
    if(size == 0 || size > kMaxSingleFontBytes ||
       size > static_cast<tjs_uint64>(std::numeric_limits<std::size_t>::max()))
        return source;

    const std::string key = cacheKey.AsStdString();
    // An empty key means the caller cannot identify the underlying file.  Do
    // not alias unrelated streams under that key; preserve the legacy stream
    // ownership/position semantics instead.
    if(key.empty())
        return source;

    const tjs_uint64 originalPosition = source->GetPosition();
    std::lock_guard<std::mutex> lock(CacheMutex);
    if(auto bytes = Find(key)) {
        Touch(bytes);
        delete source;
        return new SharedFontStream(std::move(bytes));
    }

    auto mutableBytes = std::make_shared<FontBytes>(static_cast<std::size_t>(size));
    source->SetPosition(0);
    std::size_t offset = 0;
    while(offset < mutableBytes->size()) {
        const tjs_uint want = static_cast<tjs_uint>(std::min<std::size_t>(
            mutableBytes->size() - offset,
            static_cast<std::size_t>(std::numeric_limits<tjs_uint>::max())));
        const tjs_uint got = source->Read(mutableBytes->data() + offset, want);
        if(got == 0)
            break;
        offset += got;
    }
    if(offset != mutableBytes->size()) {
        source->SetPosition(originalPosition);
        return source;
    }

    FontBytesPtr bytes = std::move(mutableBytes);
    Cache[key] = bytes;
    Touch(bytes);
    delete source;
    return new SharedFontStream(std::move(bytes));
}

void TVPClearFontStreamCache() {
    std::lock_guard<std::mutex> lock(CacheMutex);
    Cache.clear();
    MRU.clear();
    CurrentBytes = 0;
}

void TVPSetFontStreamCacheLimits(std::size_t maxBytes,
                                 std::size_t maxEntries) {
    std::lock_guard<std::mutex> lock(CacheMutex);
    MaxBytes = maxBytes;
    MaxEntries = maxEntries;
    while((MaxEntries == 0 || MRU.size() > MaxEntries) ||
          CurrentBytes > MaxBytes) {
        if(MRU.empty())
            break;
        CurrentBytes -= MRU.front().Size;
        MRU.pop_front();
    }
}
