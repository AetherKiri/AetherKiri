#include <cstdint>
#include <uchardet.h>
#include <zlib.h>
#include <optional>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>

#include <boost/locale/encoding.hpp>

#include "TextStream.h"

#include <opencv2/core/hal/interface.h>
#include <spdlog/spdlog.h>

#include "MsgIntf.h"
#include "UtilStreams.h"
#include "tjsError.h"
#include "CharacterSet.h"
#include "BinaryStream.h"

// Legacy KiriKiri scripts without a BOM are traditionally encoded as CP932.
// UTF-8 remains auto-detected before this fallback is used.
static std::string G_DefaultReadEncoding = "cp932";

// Text streams are often fed directly from a game archive.  Keep the
// decompressor and the UTF conversion bounded even when a malformed header
// advertises a multi-gigabyte payload.
static constexpr std::uint64_t kMaxTextStreamBytes =
    256ull * 1024ull * 1024ull;

static bool readU64LE(const std::uint8_t *data, std::size_t size,
                      std::size_t offset, std::uint64_t &value) {
    if(!data || offset > size || size - offset < sizeof(std::uint64_t))
        return false;
    value = 0;
    for(std::size_t i = 0; i < sizeof(std::uint64_t); ++i)
        value |= static_cast<std::uint64_t>(data[offset + i]) << (i * 8);
    return true;
}

static std::string toUpperAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::toupper(c));
                   });
    return value;
}

static std::string normalizeTextEncoding(std::string encoding) {
    const std::string upper = toUpperAscii(encoding);
    if(upper.empty())
        return encoding;

    if(upper == "SHIFT_JIS" || upper == "SHIFT-JIS" || upper == "SHIFTJIS" ||
       upper == "SJIS" || upper == "CP932" || upper == "MS932" ||
       upper == "WINDOWS-31J") {
        return "cp932";
    }
    if(upper == "WINDOWS-1252") {
        return "ASCII";
    }
    if(upper == "GBK" || upper == "CP936" || upper == "MS936" ||
       upper == "WINDOWS-936" || upper == "GB2312") {
        return "GBK";
    }
    if(upper == "GB18030") {
        return "GB18030";
    }
    if(upper == "BIG5" || upper == "CP950" || upper == "BIG-5" ||
       upper == "BIG5-HKSCS") {
        return "Big5";
    }
    if(upper == "UTF8") {
        return "UTF-8";
    }
    if(upper.rfind("MAC", 0) == 0) {
        return "cp932";
    }

    return encoding;
}

static bool isKnownNonUtf8GameEncoding(const std::string &encoding) {
    return encoding == "cp932" || encoding == "EUC-JP" ||
           encoding == "ISO-2022-JP" || encoding == "GBK" ||
           encoding == "GB18030" || encoding == "Big5";
}

static bool isStrictlyDecodable(const unsigned char *raw, size_t size,
                                const std::string &encoding) {
    try {
        boost::locale::conv::to_utf<wchar_t>(
            reinterpret_cast<const char *>(raw),
            reinterpret_cast<const char *>(raw + size), encoding,
            boost::locale::conv::stop);
        return true;
    } catch(...) {
        return false;
    }
}

static bool hasNonAsciiBytes(const unsigned char *raw, size_t size) {
    for(size_t i = 0; i < size; i++) {
        if(raw[i] >= 0x80)
            return true;
    }
    return false;
}

static bool isValidUTF8(const unsigned char *raw, size_t size) {
    size_t i = 0;
    while(i < size) {
        if(raw[i] < 0x80) {
            i++;
            continue;
        }
        int cont = 0;
        if((raw[i] & 0xE0) == 0xC0)
            cont = 1;
        else if((raw[i] & 0xF0) == 0xE0)
            cont = 2;
        else if((raw[i] & 0xF8) == 0xF0)
            cont = 3;
        else
            return false;
        i++;
        while(cont-- > 0) {
            if(i >= size || (raw[i] & 0xC0) != 0x80)
                return false;
            i++;
        }
    }
    return true;
}

static bool shouldPreferCP932ForStandMetadata(const ttstr &name) {
    ttstr shortName = TVPExtractStorageName(name).AsLowerCase();
    if(TVPExtractStorageExt(shortName) == TJS_W(".stand"))
        return true;
    if(shortName == TJS_W("facezoom.csv"))
        return true;

    constexpr tjs_int InfoSuffixLen = 9; // "_info.txt"
    if(shortName.GetLen() < InfoSuffixLen)
        return false;

    ttstr suffix(shortName.c_str() + shortName.GetLen() - InfoSuffixLen,
                 InfoSuffixLen);
    return suffix == TJS_W("_info.txt");
}

static std::optional<std::size_t> findEmbeddedBmpTextOffset(
    const std::vector<std::uint8_t> &raw) {
    // Some older KiriKiri save systems append saveStruct output to a BMP
    // thumbnail and then read the combined file as a text stream.  The BMP
    // file-size field marks the end of the image; only honor it when the tail
    // starts with a real KiriKiri text signature so ordinary bitmaps and
    // arbitrary trailing metadata keep their normal behavior.
    if(raw.size() < 14 || raw[0] != 'B' || raw[1] != 'M')
        return std::nullopt;

    const std::size_t imageSize = static_cast<std::size_t>(raw[2]) |
        (static_cast<std::size_t>(raw[3]) << 8) |
        (static_cast<std::size_t>(raw[4]) << 16) |
        (static_cast<std::size_t>(raw[5]) << 24);
    if(imageSize < 14 || imageSize >= raw.size())
        return std::nullopt;

    const std::size_t remaining = raw.size() - imageSize;
    const auto *tail = raw.data() + imageSize;
    const bool utf16Bom = remaining >= 2 &&
        ((tail[0] == 0xff && tail[1] == 0xfe) ||
         (tail[0] == 0xfe && tail[1] == 0xff));
    const bool utf8Bom = remaining >= 3 && tail[0] == 0xef &&
        tail[1] == 0xbb && tail[2] == 0xbf;
    const bool kirikiriCipher = remaining >= 3 && tail[0] == 0xfe &&
        tail[1] == 0xfe && tail[2] <= 2;
    if(!utf16Bom && !utf8Bom && !kirikiriCipher)
        return std::nullopt;
    return imageSize;
}

std::string checkTextEncoding(const void *buf, size_t size,
                              std::uint8_t &bomSize) {
    auto raw = static_cast<const unsigned char *>(buf);
    std::string encoding;
    // --- 检查 BOM ---
    if(size >= 4 && raw[0] == 0xFF && raw[1] == 0xFE && raw[2] == 0x00 &&
       raw[3] == 0x00) {
        // UTF-32LE BOM (must be checked before its UTF-16LE prefix)
        bomSize = 4;
        encoding = "UTF-32LE";
    } else if(size >= 4 && raw[0] == 0x00 && raw[1] == 0x00 &&
              raw[2] == 0xFE && raw[3] == 0xFF) {
        // UTF-32BE BOM
        bomSize = 4;
        encoding = "UTF-32BE";
    } else if(size >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {
        // UTF-16LE BOM
        bomSize = 2;
        encoding = "UTF-16LE";
    } else if(size >= 2 && raw[0] == 0xFE && raw[1] == 0xFF) {
        // UTF-16BE BOM
        bomSize = 2;
        encoding = "UTF-16BE";
    } else if(size >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) {
        // UTF-8 BOM
        bomSize = 3;
        encoding = "UTF-8";
    } else {
        // ---------- 普通文本：用 uchardet 检测编码 ----------
        uchardet_t ud = uchardet_new();
        uchardet_handle_data(ud, reinterpret_cast<const char *>(raw), size);
        uchardet_data_end(ud);
        encoding = uchardet_get_charset(ud);
        uchardet_delete(ud);
        encoding = normalizeTextEncoding(std::move(encoding));

        if(hasNonAsciiBytes(raw, size)) {
            if(encoding == "ASCII") {
                encoding.clear();
            } else if(encoding == "UTF-8") {
                if(!isValidUTF8(raw, size))
                    encoding.clear();
            } else if(!encoding.empty() &&
                      !isKnownNonUtf8GameEncoding(encoding)) {
                encoding.clear();
            } else if(!encoding.empty() &&
                      !isStrictlyDecodable(raw, size, encoding)) {
                // Statistical detectors can mistake GBK text containing many
                // Japanese glyphs for EUC-JP.  Never accept a legacy encoding
                // that cannot decode the complete byte stream: the default
                // conversion policy silently drops invalid bytes and can turn
                // quotes inside scripts into executable punctuation.
                if((encoding == "EUC-JP" || encoding == "ISO-2022-JP") &&
                   isStrictlyDecodable(raw, size, "GBK")) {
                    encoding = "GBK";
                } else {
                    encoding.clear();
                }
            }
        }
    }

    return encoding;
}

/*
 *  note: encryption of mode 0 or 1 ( simple crypt ) does never
 *  intend data pretection security.
 */
class tTVPTextReadStream : public iTJSTextReadStream {
    std::unique_ptr<tTJSBinaryStream> _stream{};
    std::u16string _buffer; // 全部文本，UTF-16
    size_t _pos = 0; // 当前读取位置

public:
    tTVPTextReadStream(const ttstr &name, const ttstr &mode) {
        _stream.reset(TVPCreateStream(name, TJS_BS_READ));
        if(!_stream)
            TVPThrowExceptionMessage(TJS_W("cannot open text stream"));
        const std::uint64_t ofs = parseModeNumber(
            mode.c_str(), TJS_W('o'), 255, 0).value_or(0);
        const std::uint64_t streamSize = _stream->GetSize();
        if(ofs > streamSize || streamSize - ofs > kMaxTextStreamBytes)
            TVPThrowExceptionMessage(TJS_W("text stream is too large"));
        _stream->SetPosition(ofs);

        std::size_t size = static_cast<std::size_t>(streamSize - ofs);
        std::vector<std::uint8_t> raw(size);
        if(size != 0)
            _stream->ReadBuffer(raw.data(), static_cast<tjs_uint>(size));

        if(ofs == 0) {
            if(const auto embeddedOffset = findEmbeddedBmpTextOffset(raw)) {
                raw.erase(raw.begin(), raw.begin() + *embeddedOffset);
                size = raw.size();
                spdlog::debug(
                    "Text stream selected embedded BMP payload: {} offset={} bytes={}",
                    name.AsStdString(), *embeddedOffset, size);
            }
        }

        // ---------- 检查是否加密/压缩 ----------
        if(size >= 3 && raw[0] == 0xFE && raw[1] == 0xFE) {
            std::uint8_t m = raw[2];
            if(m == 0 || m == 1) {
                size_t hdr = 3;
                if(size >= 5 && raw[3] == 0xFF && raw[4] == 0xFE)
                    hdr = 5; // skip unencrypted UTF-16LE BOM
                else if(size >= 5 && raw[3] == 0xFE && raw[4] == 0xFF)
                    hdr = 5; // skip unencrypted UTF-16BE BOM
                size_t data_size = size - hdr;
                if(data_size & 1) data_size--;
                size_t len = data_size / 2;
                _buffer.resize(len);
                for(size_t i = 0; i < len; i++) {
                    char16_t ch =
                        static_cast<char16_t>(raw[hdr + i * 2]) |
                        (static_cast<char16_t>(raw[hdr + i * 2 + 1]) << 8);
                    if(m == 0) {
                        if(ch >= 0x20)
                            ch ^= (((ch & 0xfe) << 8) ^ 1);
                    } else if(m == 1) {
                        ch =
                            ((ch & 0xaaaaaaaa) >> 1) | ((ch & 0x55555555) << 1);
                    }
                    _buffer[i] = ch;
                }
                return;
            }
            if(m == 2) {
                // 压缩流
                if(size < 3 + 2 + 16)
                    TVPThrowExceptionMessage(TVPUnsupportedCipherMode, name);

                // Read the little-endian sizes without an unaligned cast and
                // validate every range before allocating or copying.
                std::uint64_t compressed = 0;
                std::uint64_t uncompressed = 0;
                if(!readU64LE(raw.data(), size, 5, compressed) ||
                   !readU64LE(raw.data(), size, 13, uncompressed) ||
                   compressed > kMaxTextStreamBytes ||
                   uncompressed > kMaxTextStreamBytes ||
                   (uncompressed % sizeof(char16_t)) != 0 ||
                   compressed > size - (3 + 2 + 16) ||
                   compressed > std::numeric_limits<uLong>::max() ||
                   uncompressed > std::numeric_limits<uLongf>::max())
                    TVPThrowExceptionMessage(TVPUnsupportedCipherMode, name);

                const std::size_t compressedSize =
                    static_cast<std::size_t>(compressed);
                const std::size_t uncompressedSize =
                    static_cast<std::size_t>(uncompressed);
                const std::uint8_t *compressedData = raw.data() + 21;
                std::vector<std::uint8_t> uncompBuf(uncompressedSize);
                uLongf destLen = static_cast<uLongf>(uncompressedSize);
                const int ret = uncompress(
                    uncompBuf.empty() ? nullptr : uncompBuf.data(), &destLen,
                    compressedSize == 0 ? nullptr : compressedData,
                    static_cast<uLong>(compressedSize));
                if(ret != Z_OK || destLen != uncompressedSize)
                    TVPThrowExceptionMessage(TVPUnsupportedCipherMode, name);

                // The writer emits UTF-16LE.  Decode explicitly so the
                // reader remains well-defined on big-endian hosts and for
                // buffers whose alignment is only one byte.
                _buffer.resize(uncompressedSize / sizeof(char16_t));
                for(std::size_t i = 0; i < _buffer.size(); ++i) {
                    const std::size_t offset = i * sizeof(char16_t);
                    _buffer[i] = static_cast<char16_t>(
                        static_cast<std::uint16_t>(uncompBuf[offset]) |
                        (static_cast<std::uint16_t>(uncompBuf[offset + 1])
                         << 8));
                }
                return;
            }
            TVPThrowExceptionMessage(TVPUnsupportedCipherMode, name);
        }
        std::uint8_t bomSize = 0;
        std::string encoding = checkTextEncoding(raw.data(), size, bomSize);
        raw.erase(raw.begin(), raw.begin() + bomSize);
        size = raw.size();

        // Storages.setTextEncoding()/Scripts.textEncoding is an explicit game
        // instruction.  Legacy CJK byte streams are often valid in more than
        // one encoding, so a statistical guess (for example CP932 for GBK
        // bytes) must not override that instruction.  Keep UTF-8 as the
        // auto-detecting default for games that do not select an encoding.
        if(bomSize == 0 && G_DefaultReadEncoding != "UTF-8") {
            encoding = G_DefaultReadEncoding;
        } else if(bomSize == 0 && shouldPreferCP932ForStandMetadata(name) &&
                  hasNonAsciiBytes(raw.data(), size) &&
                  !isValidUTF8(raw.data(), size)) {
            encoding = "cp932";
        }

        if(encoding.empty() && G_DefaultReadEncoding == "UTF-8" &&
           hasNonAsciiBytes(raw.data(), size) && !isValidUTF8(raw.data(), size)) {
            encoding = "cp932";
        }

        if(encoding.empty())
            encoding = G_DefaultReadEncoding; // 默认回退

        if(encoding == "ASCII") {
            _buffer.resize(raw.size());
            for(std::size_t i = 0; i < raw.size(); ++i)
                _buffer[i] = static_cast<char16_t>(raw[i]);
            return;
        }

        if(encoding == "UTF-8") {
            _buffer = boost::locale::conv::utf_to_utf<char16_t>(
                reinterpret_cast<const char *>(raw.data()),
                reinterpret_cast<const char *>(raw.data() + raw.size()));
            return;
        }

        if(encoding == "UTF-16" || encoding == "UTF-16LE" ||
           encoding == "UTF-16BE") {
            if((raw.size() & 1u) != 0)
                TVPThrowExceptionMessage(TJS_W("invalid UTF-16 text stream"));
            const std::size_t len = raw.size() / 2;
            _buffer.resize(len);
            const bool bigEndian = encoding == "UTF-16BE";
            for(std::size_t i = 0; i < len; ++i) {
                const std::size_t offset = i * 2;
                const std::uint16_t first = raw[offset];
                const std::uint16_t second = raw[offset + 1];
                _buffer[i] = static_cast<char16_t>(
                    bigEndian ? ((first << 8) | second)
                              : (first | (second << 8)));
            }
            return;
        }

        if(encoding == "UTF-32" || encoding == "UTF-32LE" ||
           encoding == "UTF-32BE") {
            if((raw.size() & 3u) != 0)
                TVPThrowExceptionMessage(TJS_W("invalid UTF-32 text stream"));
            const std::size_t len = raw.size() / 4;
            std::u32string codepoints(len, U'\0');
            const bool bigEndian = encoding == "UTF-32BE";
            for(std::size_t i = 0; i < len; ++i) {
                const std::size_t offset = i * 4;
                const std::uint32_t b0 = raw[offset];
                const std::uint32_t b1 = raw[offset + 1];
                const std::uint32_t b2 = raw[offset + 2];
                const std::uint32_t b3 = raw[offset + 3];
                const std::uint32_t value = bigEndian
                    ? ((b0 << 24) | (b1 << 16) | (b2 << 8) | b3)
                    : (b0 | (b1 << 8) | (b2 << 16) | (b3 << 24));
                if(value > 0x10ffffu ||
                   (value >= 0xd800u && value <= 0xdfffu))
                    TVPThrowExceptionMessage(TJS_W("invalid UTF-32 text stream"));
                codepoints[i] = static_cast<char32_t>(value);
            }
            _buffer = boost::locale::conv::utf_to_utf<char16_t>(codepoints);
            return;
        }

        // 其他文本字符
        try {
            std::wstring wide = boost::locale::conv::to_utf<wchar_t>(
                reinterpret_cast<const char *>(raw.data()),
                reinterpret_cast<const char *>(raw.data() + raw.size()),
                encoding);
            _buffer = boost::locale::conv::utf_to_utf<char16_t>(wide);
        } catch(const std::exception &e) {
            spdlog::error(e.what());
            TVPThrowExceptionMessage(TJSNarrowToWideConversionError);
        }
    }

    ~tTVPTextReadStream() override = default;

    tjs_uint Read(tTJSString &targ, tjs_uint size) override {
        static_assert(sizeof(tjs_char) == sizeof(char16_t),
                      "Char size mismatch");
        if(_pos >= _buffer.size()) {
            targ.Clear();
            return 0;
        }
        size_t remain = _buffer.size() - _pos;
        size_t n = size ? size : remain;
        tjs_char *buf = targ.AllocBuffer(n);
        std::copy_n(_buffer.data() + _pos, n, buf);
        buf[n] = 0;
        _pos += n;
        targ.FixLen();
        return n;
    }

    void Destruct() override { delete this; }
};


class tTVPTextWriteStream : public iTJSTextWriteStream {
    // TODO: 32bit wchar_t support

    static constexpr size_t COMPRESSION_BUFFER_SIZE = 1024 * 1024;

    std::unique_ptr<tTJSBinaryStream> _stream{};
    tjs_int _cryptMode{};
    // -1 for no-crypt
    // 0: (unused)	(old buggy crypt mode)
    // 1: simple crypt
    // 2: complessed
    int _compressionLevel{}; // compression level of zlib

    std::unique_ptr<z_stream_s> _zStream{};
    tjs_uint _compressionSizePosition{ 0 };
    std::vector<Bytef> _compressionBuffer =
        std::vector<Bytef>(COMPRESSION_BUFFER_SIZE);
    bool _compressionFailed{ false };
    std::uint64_t _textBytesWritten{ 0 };

public:
    tTVPTextWriteStream(const ttstr &name, const ttstr &mode) {
        // mode supports following modes:
        // dN: deflate(compress) at mode N ( currently not implemented
        // ) cN: write in cipher at mode N ( currently n is ignored )
        // zN: write with compress at mode N ( N is compression level
        // ) oN: write from binary offset N (in bytes)

        // check c/z mode
        _cryptMode =
            parseModeNumber(mode.c_str(), TJS_W('c'), 1, -1).value_or(1);

        if(auto z = parseModeNumber(mode.c_str(), TJS_W('z'), 1,
                                    Z_DEFAULT_COMPRESSION)) {
            _compressionLevel = z.value();
        } else {
            _cryptMode = 2;
        }

        if(_cryptMode != -1 && _cryptMode != 1 && _cryptMode != 2)
            TVPThrowExceptionMessage(TVPUnsupportedModeString,
                                     TJS_W("unsupported cipher mode"));

        // check o mode
        const std::uint64_t ofs = parseModeNumber(
            mode.c_str(), TJS_W('o'), 255, 0).value_or(0);
        if(ofs != 0) {
            _stream.reset(TVPCreateStream(name, TJS_BS_UPDATE));
            if(!_stream || ofs > kMaxTextStreamBytes)
                TVPThrowExceptionMessage(TJS_W("text stream offset is invalid"));
            _stream->SetPosition(ofs);
        } else {
            _stream.reset(TVPCreateStream(name, TJS_BS_WRITE));
        }
        if(!_stream)
            TVPThrowExceptionMessage(TJS_W("cannot open text stream"));

        if(_cryptMode == 1 || _cryptMode == 2) {
            // simple crypt or compressed
            tjs_uint8 crypt_mode_sig[4];
            crypt_mode_sig[0] = crypt_mode_sig[1] = 0xfe;
            crypt_mode_sig[2] = static_cast<tjs_uint8>(_cryptMode);
            crypt_mode_sig[3] = 0;
            _stream->WriteBuffer(crypt_mode_sig, 3);
        }

        // now output text stream will write unicode texts
        static tjs_uint8 bommark[2] = { 0xff, 0xfe };
        _stream->WriteBuffer(bommark, 2);

        if(_cryptMode == 2) {
            // allocate and initialize zlib straem
            _zStream.reset(new z_stream_s());
            _zStream->zalloc = Z_NULL;
            _zStream->zfree = Z_NULL;
            _zStream->opaque = Z_NULL;
            if(deflateInit(_zStream.get(), _compressionLevel) != Z_OK) {
                _compressionFailed = true;
                TVPThrowExceptionMessage(TVPCompressionFailed);
            }

            _zStream->next_in = nullptr;
            _zStream->avail_in = 0;
            _zStream->next_out = _compressionBuffer.data();
            _zStream->avail_out = COMPRESSION_BUFFER_SIZE;

            // Compression Size (write dummy)
            const std::uint64_t position = _stream->GetPosition();
            if(position > std::numeric_limits<tjs_uint>::max() ||
               position > kMaxTextStreamBytes)
                TVPThrowExceptionMessage(TJS_W("text stream is too large"));
            _compressionSizePosition = static_cast<tjs_uint>(position);
            WriteI64LE(0);
            WriteI64LE(0);
        }
    }

    ~tTVPTextWriteStream() override {
        if(_cryptMode == 2) {

            if(!_compressionFailed) {
                try {
                    // close zlib stream
                    int result = 0;
                    do {
                        result = deflate(_zStream.get(), Z_FINISH);
                        if(result != Z_OK && result != Z_STREAM_END) {
                            TVPThrowExceptionMessage(TVPCompressionFailed);
                        }
                        if(_zStream->total_out > kMaxTextStreamBytes) {
                            TVPThrowExceptionMessage(
                                TJS_W("text stream is too large"));
                        }
                        _stream->WriteBuffer(_compressionBuffer.data(),
                                             COMPRESSION_BUFFER_SIZE -
                                                 _zStream->avail_out);
                        _zStream->next_out = _compressionBuffer.data();
                        _zStream->avail_out = COMPRESSION_BUFFER_SIZE;
                    } while(result != Z_STREAM_END);

                    // rollback and write compression size.
                    _stream->SetPosition(_compressionSizePosition);
                    WriteI64LE(_zStream->total_out);
                    WriteI64LE(_zStream->total_in);
                } catch(...) {
                    // delete zlib compress stream
                    if(_zStream) {
                        deflateEnd(_zStream.get());
                    }
                    throw;
                }
            }
            // delete zlib compress stream
            if(_zStream) {
                deflateEnd(_zStream.get());
            }
        }
    }

    void WriteI64LE(tjs_uint64 v) {
        // write 64bit little endian value to the file.
        tjs_uint8 buf[8];
        for(int i = 0; i < 8; i++) {
            buf[i] = static_cast<tjs_uint8>(v >> (i * 8));
        }
        _stream->WriteBuffer(buf, 8);
    }

    void Write(const ttstr &targ) override {
        tjs_int len = targ.GetLen();
        if(len < 0 || _textBytesWritten > kMaxTextStreamBytes ||
           static_cast<std::uint64_t>(len) >
               (kMaxTextStreamBytes - _textBytesWritten) /
                   sizeof(tjs_uint16))
            TVPThrowExceptionMessage(TJS_W("text stream is too large"));
        const std::size_t byteCount =
            static_cast<std::size_t>(len) * sizeof(tjs_uint16);
        auto buf = std::make_unique<tjs_uint16[]>(len + 1);
        const tjs_char *src = targ.c_str();
        tjs_int i;
        for(i = 0; i < len; i++) {
            buf[i] = src[i];
        }
        buf[i] = 0;

        if(_cryptMode == 1) {
            // simple crypt
            if(tjs_uint16 *p = buf.get()) {
                while(*p) {
                    tjs_char ch = *p;
                    ch = (ch & 0xaaaaaaaa) >> 1 | (ch & 0x55555555) << 1;
                    *p = ch;
                    p++;
                }
            }

            WriteRawData(buf.get(), byteCount);
        } else {
            WriteRawData(buf.get(), byteCount);
        }
        _textBytesWritten += byteCount;
    }

    void WriteRawData(void *ptr, size_t size) {
        if(size > kMaxTextStreamBytes || _textBytesWritten > kMaxTextStreamBytes ||
           _textBytesWritten > kMaxTextStreamBytes - size ||
           size > std::numeric_limits<uInt>::max())
            TVPThrowExceptionMessage(TJS_W("text stream is too large"));
        if(_cryptMode == 2) {
            // compressed with zlib stream.
            _zStream->next_in = static_cast<Bytef *>(ptr);
            _zStream->avail_in = static_cast<uInt>(size);

            while(_zStream->avail_in > 0) {
                int result = deflate(_zStream.get(), Z_NO_FLUSH);
                if(result != Z_OK) {
                    _compressionFailed = true;
                    TVPThrowExceptionMessage(TVPCompressionFailed);
                }
                if(_zStream->total_out > kMaxTextStreamBytes) {
                    _compressionFailed = true;
                    TVPThrowExceptionMessage(TJS_W("text stream is too large"));
                }
                if(_zStream->avail_out == 0) {
                    _stream->WriteBuffer(_compressionBuffer.data(),
                                         COMPRESSION_BUFFER_SIZE);
                    _zStream->next_out = _compressionBuffer.data();
                    _zStream->avail_out = COMPRESSION_BUFFER_SIZE;
                }
            }
        } else {
            _stream->WriteBuffer(ptr, size); // write directly
        }
    }

    void Destruct() override { delete this; }
};

iTJSTextReadStream *TVPCreateTextStreamForRead(const ttstr &name,
                                               const ttstr &mode) {
    return new tTVPTextReadStream(name, mode);
}

iTJSTextWriteStream *TVPCreateTextStreamForWrite(const ttstr &name,
                                                 const ttstr &mode) {
    return new tTVPTextWriteStream(name, mode);
}

//---------------------------------------------------------------------------
void TVPSetDefaultReadEncoding(const ttstr &encoding) {
    ttstr codestr = encoding;
    codestr.ToLowerCase();
    if(codestr == TJS_W("sjis") || codestr == TJS_W("shiftjis") ||
       codestr == TJS_W("shift_jis") || codestr == TJS_W("shift-jis")) {
        G_DefaultReadEncoding = "cp932";
    } else if(codestr == TJS_W("utf8") || codestr == TJS_W("utf-8")) {
        G_DefaultReadEncoding = "UTF-8";
    } else if(codestr == TJS_W("gbk") || codestr == TJS_W("cp936") ||
              codestr == TJS_W("gb2312")) {
        G_DefaultReadEncoding = "GBK";
    } else if(codestr == TJS_W("gb18030")) {
        G_DefaultReadEncoding = "GB18030";
    } else if(codestr == TJS_W("big5") || codestr == TJS_W("cp950") ||
              codestr == TJS_W("big-5")) {
        G_DefaultReadEncoding = "Big5";
    } else {
        G_DefaultReadEncoding =
            normalizeTextEncoding(encoding.AsStdString());
    }
}

//---------------------------------------------------------------------------
const tjs_char *TVPGetDefaultReadEncoding() {
    return ttstr{ G_DefaultReadEncoding }.c_str();
}
