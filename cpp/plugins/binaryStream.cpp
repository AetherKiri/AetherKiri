#include "ncbind.hpp"
#include "tp_stub.h"

#include <algorithm>
#include <array>
#include <memory>
#include <vector>

#include <zlib.h>

#define NCB_MODULE_NAME TJS_W("binaryStream.dll")

namespace {

constexpr tjs_uint kCopyBufferSize = 1024 * 1024;
constexpr tjs_uint kCompressBufferSize = 1024 * 1024;

void error(const ttstr &message) {
    TVPThrowExceptionMessage((ttstr(TJS_W("BinaryStream: ")) + message).c_str());
}

bool hasFilter(ncbPropAccessor &elm) {
    tTJSVariantType type = tvtVoid;
    return elm.HasValue(TJS_W("filter"), 0, &type) && type != tvtVoid;
}

tjs_int parseMode(const tTJSVariant &value) {
    if(value.Type() == tvtInteger)
        return static_cast<tjs_int>(value.AsInteger());

    ttstr mode = value.AsStringNoAddRef();
    if(mode.IsEmpty() || mode == TJS_W("r") || mode == TJS_W("rb") ||
       mode == TJS_W("read"))
        return TJS_BS_READ;
    if(mode == TJS_W("w") || mode == TJS_W("wb") || mode == TJS_W("write"))
        return TJS_BS_WRITE;
    if(mode == TJS_W("a") || mode == TJS_W("ab") || mode == TJS_W("append"))
        return TJS_BS_APPEND;
    if(mode == TJS_W("u") || mode == TJS_W("r+") || mode == TJS_W("rb+") ||
       mode == TJS_W("update"))
        return TJS_BS_UPDATE;

    error(ttstr(TJS_W("unsupported mode: ")) + mode);
    return TJS_BS_READ;
}

} // namespace

class BinaryStream {
public:
    BinaryStream() = default;
    ~BinaryStream() { close(); }

    static tjs_error factory(BinaryStream **result, tjs_int numparams,
                             tTJSVariant **param, iTJSDispatch2 *) {
        auto *self = new BinaryStream();
        if(numparams >= 1) {
            const tjs_int openMode =
                numparams >= 2 ? parseMode(*param[1]) : TJS_BS_READ;
            try {
                self->openRaw(param[0]->GetString(), openMode);
            } catch(...) {
                delete self;
                throw;
            }
        }
        *result = self;
        return TJS_S_OK;
    }

    void open(const tjs_char *storageName, tTJSVariant openMode) {
        openRaw(storageName, parseMode(openMode));
    }

    void openRaw(const tjs_char *storageName, tjs_int openMode) {
        close();
        stream.reset(TVPCreateStream(storageName, openMode & TJS_BS_ACCESS_MASK));
        if(!stream)
            error(ttstr(TJS_W("cannot open: ")) + storageName);
        storage = storageName;
        mode = openMode;
    }

    void close() {
        stream.reset();
        storage = TJS_W("");
        mode = -1;
    }

    tjs_int64 seek(tjs_int64 position, tjs_int whence) {
        ensureOpen();
        if(whence != TJS_BS_SEEK_SET && whence != TJS_BS_SEEK_CUR &&
           whence != TJS_BS_SEEK_END)
            error(TJS_W("invalid whence value"));
        return static_cast<tjs_int64>(stream->Seek(position, whence));
    }

    tjs_int64 tell() { return seek(0, TJS_BS_SEEK_CUR); }
    tjs_int getMode() const { return mode; }
    ttstr getStorage() const { return storage; }

    static tjs_error read(tTJSVariant *result, tjs_int numparams,
                          tTJSVariant **param, BinaryStream *self) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        if(!self)
            return TJS_E_NATIVECLASSCRASH;

        tTJSVariantOctet *octet = self->readOctet(param[0]->AsInteger());
        if(result) {
            if(octet)
                *result = octet;
            else
                result->Clear();
        }
        if(octet)
            octet->Release();
        return TJS_S_OK;
    }

    static tjs_error write(tTJSVariant *result, tjs_int numparams,
                           tTJSVariant **param, BinaryStream *self) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        if(!self)
            return TJS_E_NATIVECLASSCRASH;

        tjs_uint written = 0;
        if(param[0]->Type() == tvtOctet) {
            tTJSVariantOctet *octet = param[0]->AsOctetNoAddRef();
            if(octet)
                written = self->writeBytes(octet->GetData(), octet->GetLength());
        } else if(param[0]->Type() == tvtString) {
            tTJSVariantString *str = param[0]->AsStringNoAddRef();
            if(str) {
                written = self->writeBytes(
                    reinterpret_cast<const tjs_uint8 *>(
                        str->operator const tjs_char *()),
                    static_cast<tjs_uint>((str->GetLength() + 1) *
                                          sizeof(tjs_char)));
            }
        } else {
            error(TJS_W("invalid data type"));
        }

        if(result)
            *result = static_cast<tjs_int64>(written);
        return TJS_S_OK;
    }

    static tjs_error readI8(tTJSVariant *r, tjs_int, tTJSVariant **,
                            BinaryStream *self) {
        return self ? self->readInteger(r, 1, false) : TJS_E_NATIVECLASSCRASH;
    }
    static tjs_error readI8LE(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                              BinaryStream *self) {
        return readI8(r, n, p, self);
    }
    static tjs_error readI8BE(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                              BinaryStream *self) {
        return readI8(r, n, p, self);
    }
    static tjs_error readI16LE(tTJSVariant *r, tjs_int, tTJSVariant **,
                               BinaryStream *self) {
        return self ? self->readInteger(r, 2, false) : TJS_E_NATIVECLASSCRASH;
    }
    static tjs_error readI32LE(tTJSVariant *r, tjs_int, tTJSVariant **,
                               BinaryStream *self) {
        return self ? self->readInteger(r, 4, false) : TJS_E_NATIVECLASSCRASH;
    }
    static tjs_error readI64LE(tTJSVariant *r, tjs_int, tTJSVariant **,
                               BinaryStream *self) {
        return self ? self->readInteger(r, 8, false) : TJS_E_NATIVECLASSCRASH;
    }
    static tjs_error readI16BE(tTJSVariant *r, tjs_int, tTJSVariant **,
                               BinaryStream *self) {
        return self ? self->readInteger(r, 2, true) : TJS_E_NATIVECLASSCRASH;
    }
    static tjs_error readI32BE(tTJSVariant *r, tjs_int, tTJSVariant **,
                               BinaryStream *self) {
        return self ? self->readInteger(r, 4, true) : TJS_E_NATIVECLASSCRASH;
    }
    static tjs_error readI64BE(tTJSVariant *r, tjs_int, tTJSVariant **,
                               BinaryStream *self) {
        return self ? self->readInteger(r, 8, true) : TJS_E_NATIVECLASSCRASH;
    }

    static tjs_error writeI8(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                             BinaryStream *self) {
        return self ? self->writeInteger(r, n, p, 1, false)
                    : TJS_E_NATIVECLASSCRASH;
    }
    static tjs_error writeI8LE(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                               BinaryStream *self) {
        return writeI8(r, n, p, self);
    }
    static tjs_error writeI8BE(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                               BinaryStream *self) {
        return writeI8(r, n, p, self);
    }
    static tjs_error writeI16LE(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                BinaryStream *self) {
        return self ? self->writeInteger(r, n, p, 2, false)
                    : TJS_E_NATIVECLASSCRASH;
    }
    static tjs_error writeI32LE(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                BinaryStream *self) {
        return self ? self->writeInteger(r, n, p, 4, false)
                    : TJS_E_NATIVECLASSCRASH;
    }
    static tjs_error writeI64LE(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                BinaryStream *self) {
        return self ? self->writeInteger(r, n, p, 8, false)
                    : TJS_E_NATIVECLASSCRASH;
    }
    static tjs_error writeI16BE(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                BinaryStream *self) {
        return self ? self->writeInteger(r, n, p, 2, true)
                    : TJS_E_NATIVECLASSCRASH;
    }
    static tjs_error writeI32BE(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                BinaryStream *self) {
        return self ? self->writeInteger(r, n, p, 4, true)
                    : TJS_E_NATIVECLASSCRASH;
    }
    static tjs_error writeI64BE(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                BinaryStream *self) {
        return self ? self->writeInteger(r, n, p, 8, true)
                    : TJS_E_NATIVECLASSCRASH;
    }

    static tjs_error copy(tTJSVariant *result, tjs_int numparams,
                          tTJSVariant **param, BinaryStream *self) {
        return self ? self->copyLike(result, numparams, param, Mode::Copy)
                    : TJS_E_NATIVECLASSCRASH;
    }
    static tjs_error compress(tTJSVariant *result, tjs_int numparams,
                              tTJSVariant **param, BinaryStream *self) {
        return self ? self->copyLike(result, numparams, param, Mode::Compress)
                    : TJS_E_NATIVECLASSCRASH;
    }
    static tjs_error decompress(tTJSVariant *result, tjs_int numparams,
                                tTJSVariant **param, BinaryStream *self) {
        return self ? self->copyLike(result, numparams, param, Mode::Decompress)
                    : TJS_E_NATIVECLASSCRASH;
    }

    void setProgressCallback(tTJSVariant callbackValue) {
        hasCallback = callbackValue.Type() == tvtObject &&
                      callbackValue.AsObjectNoAddRef() != nullptr;
        callback = hasCallback ? callbackValue.AsObjectClosureNoAddRef()
                               : tTJSVariantClosure(nullptr, nullptr);
    }

    void setFilter(const tjs_char *dll) {
        if(dll && dll[0] != 0)
            error(TJS_W("external copy filters are not supported"));
    }

private:
    enum class Mode { Copy, Compress, Decompress };

    struct Options {
        tjs_int64 offset = 0;
        tjs_int64 length = 0;
        bool nocopy = false;
        bool md5 = false;
        int compressionLevel = Z_BEST_COMPRESSION;
        ncbPropAccessor *accessor = nullptr;
    };

    void ensureOpen() const {
        if(!stream)
            error(TJS_W("stream not opened"));
    }

    tTJSVariantOctet *readOctet(tjs_int64 size) {
        ensureOpen();
        if(size <= 0)
            return nullptr;
        if(size > static_cast<tjs_int64>(static_cast<tjs_uint32>(size)))
            error(TJS_W("too large read size"));

        std::vector<tjs_uint8> data(static_cast<size_t>(size));
        const tjs_uint read =
            stream->Read(data.data(), static_cast<tjs_uint>(data.size()));
        return read > 0 ? TJSAllocVariantOctet(data.data(), read) : nullptr;
    }

    tjs_uint writeBytes(const tjs_uint8 *data, tjs_uint size) {
        ensureOpen();
        if(size == 0)
            return 0;
        const tjs_uint written = stream->Write(data, size);
        if(written != size)
            error(TJS_W("write failed"));
        return written;
    }

    tjs_error readInteger(tTJSVariant *result, int size, bool bigEndian) {
        ensureOpen();
        std::array<tjs_uint8, 8> data{};
        const tjs_uint read = stream->Read(data.data(), size);
        if(read == 0) {
            if(result)
                result->Clear();
            return TJS_S_OK;
        }

        tjs_uint64 value = 0;
        if(bigEndian) {
            for(int i = 0; i < size; ++i)
                value = (value << 8) | data[i];
        } else {
            for(int i = size - 1; i >= 0; --i)
                value = (value << 8) | data[i];
        }
        if(result)
            *result = static_cast<tjs_int64>(value);
        return TJS_S_OK;
    }

    tjs_error writeInteger(tTJSVariant *result, tjs_int numparams,
                           tTJSVariant **param, int size, bool bigEndian) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        const tjs_uint64 value = static_cast<tjs_uint64>(param[0]->AsInteger());
        std::array<tjs_uint8, 8> data{};
        for(int i = 0; i < size; ++i) {
            const int shift = (bigEndian ? size - 1 - i : i) * 8;
            data[i] = static_cast<tjs_uint8>((value >> shift) & 0xff);
        }
        const tjs_uint written = writeBytes(data.data(), size);
        if(result)
            *result = static_cast<tjs_int64>(written);
        return TJS_S_OK;
    }

    tjs_error copyLike(tTJSVariant *result, tjs_int numparams,
                       tTJSVariant **param, Mode copyMode) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        ensureOpen();

        Options options;
        std::unique_ptr<ncbPropAccessor> accessor;
        if(numparams >= 2) {
            accessor = std::make_unique<ncbPropAccessor>(*param[1]);
            if(accessor->IsValid()) {
                options.accessor = accessor.get();
                loadOptions(options, copyMode);
            }
        }

        ttstr sourceName = param[0]->GetString();
        std::unique_ptr<tTJSBinaryStream> source(
            TVPCreateStream(sourceName, TJS_BS_READ));
        if(!source)
            error(ttstr(TJS_W("storage not found: ")) + sourceName);
        if(options.offset > 0)
            source->Seek(options.offset, TJS_BS_SEEK_SET);

        TVP_md5_state_t md5State;
        if(options.md5)
            TVP_md5_init(&md5State);

        tjs_uint32 adler = ::adler32(0L, Z_NULL, 0);
        tjs_int64 readCount = 0;
        tjs_int64 writeCount = 0;
        std::vector<tjs_uint8> buffer(kCopyBufferSize);

        if(copyMode == Mode::Copy)
            doCopy(*source, sourceName, options, buffer, adler, md5State,
                   readCount, writeCount);
        else
            doZlib(*source, sourceName, options, buffer, adler, md5State,
                   readCount, writeCount, copyMode == Mode::Compress);

        saveOptions(options, readCount, adler, md5State);
        if(result)
            *result = options.nocopy ? static_cast<tjs_int64>(0) : writeCount;
        return TJS_S_OK;
    }

    void loadOptions(Options &options, Mode copyMode) {
        auto &elm = *options.accessor;
        options.offset =
            elm.GetValue(TJS_W("offset"), ncbTypedefs::Tag<tjs_int64>());
        options.length =
            elm.GetValue(TJS_W("length"), ncbTypedefs::Tag<tjs_int64>());
        options.nocopy = elm.getIntValue(TJS_W("nocopy")) != 0;
        options.md5 = elm.getIntValue(TJS_W("md5")) != 0;
        if(copyMode == Mode::Compress) {
            options.compressionLevel =
                elm.getIntValue(TJS_W("comp_lv"), Z_BEST_COMPRESSION);
            options.compressionLevel =
                std::clamp(options.compressionLevel, 0, 9);
        }
        if(hasFilter(elm))
            error(TJS_W("external copy filters are not supported"));
    }

    void saveOptions(Options &options, tjs_int64 readCount, tjs_uint32 adler,
                     TVP_md5_state_t &md5State) {
        if(!options.accessor)
            return;
        auto &elm = *options.accessor;
        elm.SetValue(TJS_W("read"), readCount);
        elm.SetValue(TJS_W("hash"), adler);
        if(options.md5) {
            tjs_uint8 digest[16];
            TVP_md5_finish(&md5State, digest);
            tTJSVariantOctet *octet = TJSAllocVariantOctet(digest, 16);
            tTJSVariant value;
            value = octet;
            elm.SetValue(TJS_W("digest"), value);
            octet->Release();
        }
    }

    tjs_uint readChunk(tTJSBinaryStream &source, Options &options,
                       std::vector<tjs_uint8> &buffer, tjs_int64 readCount) {
        tjs_uint requested = static_cast<tjs_uint>(buffer.size());
        if(options.length > 0) {
            const tjs_int64 remain = options.length - readCount;
            if(remain <= 0)
                return 0;
            requested = static_cast<tjs_uint>(
                std::min<tjs_int64>(requested, remain));
        }
        return source.Read(buffer.data(), requested);
    }

    void writeCopied(const tjs_uint8 *data, tjs_uint size, Options &options,
                     TVP_md5_state_t &md5State, tjs_int64 &writeCount) {
        if(size == 0)
            return;
        if(options.md5)
            TVP_md5_append(&md5State, data, static_cast<int>(size));
        if(options.nocopy)
            writeCount += size;
        else
            writeCount += writeBytes(data, size);
    }

    bool progress(const ttstr &sourceName, tjs_int64 readCount) {
        if(!hasCallback)
            return false;
        tTJSVariant result;
        tTJSVariant sourceValue(sourceName);
        tTJSVariant readValue(readCount);
        tTJSVariant *params[] = { &sourceValue, &readValue };
        return TJS_SUCCEEDED(callback.FuncCall(0, nullptr, nullptr, &result, 2,
                                               params, nullptr)) &&
               static_cast<bool>(result);
    }

    void doCopy(tTJSBinaryStream &source, const ttstr &sourceName,
                Options &options, std::vector<tjs_uint8> &buffer,
                tjs_uint32 &adler, TVP_md5_state_t &md5State,
                tjs_int64 &readCount, tjs_int64 &writeCount) {
        while(true) {
            const tjs_uint read = readChunk(source, options, buffer, readCount);
            if(read == 0)
                break;
            adler = ::adler32(adler, buffer.data(), read);
            writeCopied(buffer.data(), read, options, md5State, writeCount);
            readCount += read;
            if(progress(sourceName, readCount))
                break;
        }
    }

    void doZlib(tTJSBinaryStream &source, const ttstr &sourceName,
                Options &options, std::vector<tjs_uint8> &buffer,
                tjs_uint32 &adler, TVP_md5_state_t &md5State,
                tjs_int64 &readCount, tjs_int64 &writeCount, bool compress) {
        z_stream z{};
        int zret = compress ? deflateInit(&z, options.compressionLevel)
                            : inflateInit(&z);
        if(zret != Z_OK)
            error(TJS_W("zlib setup failed"));

        std::vector<tjs_uint8> output(kCompressBufferSize);
        try {
            bool inputEnded = false;
            while(!inputEnded) {
                const tjs_uint read = readChunk(source, options, buffer, readCount);
                inputEnded = read == 0;
                if(read > 0) {
                    if(compress)
                        adler = ::adler32(adler, buffer.data(), read);
                    z.next_in = buffer.data();
                    z.avail_in = read;
                    readCount += read;
                } else {
                    z.next_in = nullptr;
                    z.avail_in = 0;
                }

                const int flush = inputEnded ? Z_FINISH : Z_NO_FLUSH;
                do {
                    z.next_out = output.data();
                    z.avail_out = static_cast<uInt>(output.size());
                    zret = compress ? deflate(&z, flush) : inflate(&z, flush);
                    if(zret != Z_OK && zret != Z_STREAM_END &&
                       !(compress && zret == Z_BUF_ERROR && inputEnded))
                        error(TJS_W("zlib procedure failed"));

                    const tjs_uint produced =
                        static_cast<tjs_uint>(output.size() - z.avail_out);
                    if(produced > 0) {
                        if(!compress)
                            adler = ::adler32(adler, output.data(), produced);
                        writeCopied(output.data(), produced, options, md5State,
                                    writeCount);
                    }
                } while(z.avail_out == 0);

                if(zret == Z_STREAM_END)
                    break;
                if(read > 0 && progress(sourceName, readCount))
                    break;
            }
        } catch(...) {
            compress ? deflateEnd(&z) : inflateEnd(&z);
            throw;
        }
        compress ? deflateEnd(&z) : inflateEnd(&z);
    }

    std::unique_ptr<tTJSBinaryStream> stream;
    ttstr storage;
    tjs_int mode = -1;
    bool hasCallback = false;
    tTJSVariantClosure callback{ nullptr, nullptr };
};

NCB_REGISTER_CLASS(BinaryStream) {
    Factory(&Class::factory);
    Method(TJS_W("open"), &Class::open);
    Method(TJS_W("close"), &Class::close);
    Method(TJS_W("seek"), &Class::seek);
    Method(TJS_W("tell"), &Class::tell);
    Property(TJS_W("storage"), &Class::getStorage, (int)0);
    Property(TJS_W("mode"), &Class::getMode, (int)0);
    RawCallback(TJS_W("read"), &Class::read, 0);
    RawCallback(TJS_W("write"), &Class::write, 0);
    RawCallback(TJS_W("copy"), &Class::copy, 0);
    RawCallback(TJS_W("compress"), &Class::compress, 0);
    RawCallback(TJS_W("decompress"), &Class::decompress, 0);
    Method(TJS_W("setProgressCallback"), &Class::setProgressCallback);
    Method(TJS_W("setFilter"), &Class::setFilter);
    RawCallback(TJS_W("readI8"), &Class::readI8, 0);
    RawCallback(TJS_W("readI8LE"), &Class::readI8LE, 0);
    RawCallback(TJS_W("readI8BE"), &Class::readI8BE, 0);
    RawCallback(TJS_W("readI16LE"), &Class::readI16LE, 0);
    RawCallback(TJS_W("readI32LE"), &Class::readI32LE, 0);
    RawCallback(TJS_W("readI64LE"), &Class::readI64LE, 0);
    RawCallback(TJS_W("readI16BE"), &Class::readI16BE, 0);
    RawCallback(TJS_W("readI32BE"), &Class::readI32BE, 0);
    RawCallback(TJS_W("readI64BE"), &Class::readI64BE, 0);
    RawCallback(TJS_W("writeI8"), &Class::writeI8, 0);
    RawCallback(TJS_W("writeI8LE"), &Class::writeI8LE, 0);
    RawCallback(TJS_W("writeI8BE"), &Class::writeI8BE, 0);
    RawCallback(TJS_W("writeI16LE"), &Class::writeI16LE, 0);
    RawCallback(TJS_W("writeI32LE"), &Class::writeI32LE, 0);
    RawCallback(TJS_W("writeI64LE"), &Class::writeI64LE, 0);
    RawCallback(TJS_W("writeI16BE"), &Class::writeI16BE, 0);
    RawCallback(TJS_W("writeI32BE"), &Class::writeI32BE, 0);
    RawCallback(TJS_W("writeI64BE"), &Class::writeI64BE, 0);
    Variant(TJS_W("bsRead"), static_cast<tjs_int>(TJS_BS_READ), 0);
    Variant(TJS_W("bsWrite"), static_cast<tjs_int>(TJS_BS_WRITE), 0);
    Variant(TJS_W("bsAppend"), static_cast<tjs_int>(TJS_BS_APPEND), 0);
    Variant(TJS_W("bsUpdate"), static_cast<tjs_int>(TJS_BS_UPDATE), 0);
    Variant(TJS_W("bsSeekSet"), static_cast<tjs_int>(TJS_BS_SEEK_SET), 0);
    Variant(TJS_W("bsSeekCur"), static_cast<tjs_int>(TJS_BS_SEEK_CUR), 0);
    Variant(TJS_W("bsSeekEnd"), static_cast<tjs_int>(TJS_BS_SEEK_END), 0);
    Variant(TJS_W("READ"), static_cast<tjs_int>(TJS_BS_READ), 0);
    Variant(TJS_W("WRITE"), static_cast<tjs_int>(TJS_BS_WRITE), 0);
    Variant(TJS_W("APPEND"), static_cast<tjs_int>(TJS_BS_APPEND), 0);
    Variant(TJS_W("UPDATE"), static_cast<tjs_int>(TJS_BS_UPDATE), 0);
    Variant(TJS_W("SEEK_SET"), static_cast<tjs_int>(TJS_BS_SEEK_SET), 0);
    Variant(TJS_W("SEEK_CUR"), static_cast<tjs_int>(TJS_BS_SEEK_CUR), 0);
    Variant(TJS_W("SEEK_END"), static_cast<tjs_int>(TJS_BS_SEEK_END), 0);
}
