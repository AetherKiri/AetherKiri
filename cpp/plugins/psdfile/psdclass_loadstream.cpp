#include "psdclass.h"

#include "psdparse.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <mutex>

namespace {

// Adapt a KiriKiri storage stream to psdparse's shared, seekable byte source.
// StreamReader clones keep this source alive while decoded layer data still
// references the PSD, so the underlying stream must be owned here as well.
class TJSBinaryStreamSource final : public psd::StreamReader::Source {
public:
    TJSBinaryStreamSource(tTJSBinaryStream *stream, size_t total_size) :
        stream_(stream), total_size_(total_size) {}

    ~TJSBinaryStreamSource() override { delete stream_; }

    size_t size() const override { return total_size_; }

    size_t read(uint8_t *out, size_t offset, size_t length) override {
        if(stream_ == nullptr || out == nullptr || offset >= total_size_ ||
           length == 0) {
            return 0;
        }

        const size_t available = total_size_ - offset;
        const size_t requested = std::min(length, available);
        const size_t max_read =
            static_cast<size_t>(std::numeric_limits<tjs_uint>::max());
        const tjs_uint read_size =
            static_cast<tjs_uint>(std::min(requested, max_read));

        // Seek and Read form one operation. Parser sub-readers share this
        // source and may be consumed from different layer decode jobs.
        std::lock_guard<std::mutex> lock(mutex_);
        stream_->Seek(static_cast<tjs_int64>(offset), TJS_BS_SEEK_SET);
        return static_cast<size_t>(stream_->Read(out, read_size));
    }

private:
    tTJSBinaryStream *stream_;
    size_t total_size_;
    std::mutex mutex_;
};

} // namespace

bool PSD::loadStream(const ttstr &filename) {
    clearData();
    isLoaded = false;

    tTJSBinaryStream *stream = TVPCreateStream(filename, TJS_BS_READ);
    if(stream == nullptr) {
        return false;
    }

    const tjs_uint64 stream_size = stream->GetSize();
    if(stream_size == 0 ||
       stream_size > static_cast<tjs_uint64>(std::numeric_limits<int>::max())) {
        delete stream;
        return false;
    }

    auto source = std::make_shared<TJSBinaryStreamSource>(
        stream, static_cast<size_t>(stream_size));
    psd::StreamReader reader(source);
    isLoaded = loadFromReader(reader);
    if(!isLoaded) {
        clearData();
    }
    return isLoaded;
}
