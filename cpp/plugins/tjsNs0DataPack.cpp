#include "tjsNs0DataPack.h"

#include "tjsArray.h"
#include "tjsDictionary.h"

#include <lz4.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Byte = std::uint8_t;
using TjsString = std::basic_string<tjs_char>;

constexpr std::array<Byte, 4> TJS_NS0_MAGIC = { 'T', 'J', 'S', '/' };
constexpr std::size_t TJS_NS0_HEADER_SIZE = 16;
constexpr std::size_t TJS_NS0_MAX_DEPTH = 128;
constexpr std::size_t TJS_NS0_MAX_UNPACKED_SIZE = 256u * 1024u * 1024u;

std::uint32_t readLe32(const Byte *data) {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8) |
           (static_cast<std::uint32_t>(data[2]) << 16) |
           (static_cast<std::uint32_t>(data[3]) << 24);
}

void writeLe32(Byte *data, std::uint32_t value) {
    data[0] = static_cast<Byte>(value);
    data[1] = static_cast<Byte>(value >> 8);
    data[2] = static_cast<Byte>(value >> 16);
    data[3] = static_cast<Byte>(value >> 24);
}

std::uint32_t rotateLeft(std::uint32_t value, unsigned amount) {
    return (value << amount) | (value >> (32u - amount));
}

std::uint32_t rotateRight(std::uint32_t value, unsigned amount) {
    return (value >> amount) | (value << (32u - amount));
}

std::uint32_t xxh32Round(std::uint32_t accumulator, std::uint32_t input) {
    accumulator += input * 0x85ebca77u;
    accumulator = rotateLeft(accumulator, 13);
    return accumulator * 0x9e3779b1u;
}

std::uint32_t xxh32(const Byte *data, std::size_t size,
                    std::uint32_t seed) {
    constexpr std::uint32_t PRIME1 = 0x9e3779b1u;
    constexpr std::uint32_t PRIME2 = 0x85ebca77u;
    constexpr std::uint32_t PRIME3 = 0xc2b2ae3du;
    constexpr std::uint32_t PRIME4 = 0x27d4eb2fu;
    constexpr std::uint32_t PRIME5 = 0x165667b1u;

    std::size_t position = 0;
    std::uint32_t hash;
    if(size >= 16) {
        std::uint32_t v1 = seed + PRIME1 + PRIME2;
        std::uint32_t v2 = seed + PRIME2;
        std::uint32_t v3 = seed;
        std::uint32_t v4 = seed - PRIME1;
        do {
            v1 = xxh32Round(v1, readLe32(data + position));
            position += 4;
            v2 = xxh32Round(v2, readLe32(data + position));
            position += 4;
            v3 = xxh32Round(v3, readLe32(data + position));
            position += 4;
            v4 = xxh32Round(v4, readLe32(data + position));
            position += 4;
        } while(position <= size - 16);
        hash = rotateLeft(v1, 1) + rotateLeft(v2, 7) +
               rotateLeft(v3, 12) + rotateLeft(v4, 18);
    } else {
        hash = seed + PRIME5;
    }

    hash += static_cast<std::uint32_t>(size);
    while(position + 4 <= size) {
        hash += readLe32(data + position) * PRIME3;
        hash = rotateLeft(hash, 17) * PRIME4;
        position += 4;
    }
    while(position < size) {
        hash += static_cast<std::uint32_t>(data[position++]) * PRIME5;
        hash = rotateLeft(hash, 11) * PRIME1;
    }
    hash ^= hash >> 15;
    hash *= PRIME2;
    hash ^= hash >> 13;
    hash *= PRIME3;
    hash ^= hash >> 16;
    return hash;
}

void blake2sMix(std::array<std::uint32_t, 16> &state, int a, int b,
                int c, int d, std::uint32_t x, std::uint32_t y) {
    state[a] = state[a] + state[b] + x;
    state[d] = rotateRight(state[d] ^ state[a], 16);
    state[c] += state[d];
    state[b] = rotateRight(state[b] ^ state[c], 12);
    state[a] = state[a] + state[b] + y;
    state[d] = rotateRight(state[d] ^ state[a], 8);
    state[c] += state[d];
    state[b] = rotateRight(state[b] ^ state[c], 7);
}

void blake2sCompress(std::array<std::uint32_t, 8> &hash,
                     const Byte *block, std::uint64_t byteCount,
                     bool isLast) {
    static constexpr std::array<std::uint32_t, 8> IV = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
    };
    static constexpr Byte SIGMA[10][16] = {
        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
        { 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 },
        { 11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4 },
        { 7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8 },
        { 9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13 },
        { 2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9 },
        { 12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11 },
        { 13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10 },
        { 6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5 },
        { 10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0 },
    };

    std::array<std::uint32_t, 16> state;
    std::array<std::uint32_t, 16> message;
    for(std::size_t i = 0; i < hash.size(); ++i) {
        state[i] = hash[i];
        state[i + 8] = IV[i];
    }
    for(std::size_t i = 0; i < message.size(); ++i)
        message[i] = readLe32(block + i * 4);
    state[12] ^= static_cast<std::uint32_t>(byteCount);
    state[13] ^= static_cast<std::uint32_t>(byteCount >> 32);
    if(isLast)
        state[14] = ~state[14];

    for(std::size_t round = 0; round < 10; ++round) {
        const Byte *s = SIGMA[round];
        blake2sMix(state, 0, 4, 8, 12, message[s[0]], message[s[1]]);
        blake2sMix(state, 1, 5, 9, 13, message[s[2]], message[s[3]]);
        blake2sMix(state, 2, 6, 10, 14, message[s[4]], message[s[5]]);
        blake2sMix(state, 3, 7, 11, 15, message[s[6]], message[s[7]]);
        blake2sMix(state, 0, 5, 10, 15, message[s[8]], message[s[9]]);
        blake2sMix(state, 1, 6, 11, 12, message[s[10]], message[s[11]]);
        blake2sMix(state, 2, 7, 8, 13, message[s[12]], message[s[13]]);
        blake2sMix(state, 3, 4, 9, 14, message[s[14]], message[s[15]]);
    }
    for(std::size_t i = 0; i < hash.size(); ++i)
        hash[i] ^= state[i] ^ state[i + 8];
}

std::array<Byte, 32> makePackinOneKey(std::uint32_t seed,
                                      const std::vector<Byte> &iv) {
    static constexpr std::array<std::uint32_t, 8> IV = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
    };
    std::array<std::uint32_t, 8> hash = IV;
    hash[0] ^= 0x01010420u;

    std::array<Byte, 64> block{};
    writeLe32(block.data(), seed);
    if(iv.empty()) {
        blake2sCompress(hash, block.data(), block.size(), true);
    } else {
        blake2sCompress(hash, block.data(), block.size(), false);
        std::size_t position = 0;
        std::uint64_t byteCount = block.size();
        while(iv.size() - position > block.size()) {
            blake2sCompress(hash, iv.data() + position,
                            byteCount + block.size(), false);
            byteCount += block.size();
            position += block.size();
        }
        block.fill(0);
        const std::size_t remaining = iv.size() - position;
        std::copy_n(iv.data() + position, remaining, block.data());
        blake2sCompress(hash, block.data(), byteCount + remaining, true);
    }

    std::array<Byte, 32> result;
    for(std::size_t i = 0; i < hash.size(); ++i)
        writeLe32(result.data() + i * 4, hash[i]);
    return result;
}

void chachaQuarterRound(std::array<std::uint32_t, 16> &state, int a,
                        int b, int c, int d) {
    state[a] += state[b];
    state[d] = rotateLeft(state[d] ^ state[a], 16);
    state[c] += state[d];
    state[b] = rotateLeft(state[b] ^ state[c], 12);
    state[a] += state[b];
    state[d] = rotateLeft(state[d] ^ state[a], 8);
    state[c] += state[d];
    state[b] = rotateLeft(state[b] ^ state[c], 7);
}

std::array<std::uint32_t, 16>
makeChaCha8Block(const std::array<std::uint32_t, 16> &input) {
    auto output = input;
    for(int round = 0; round < 4; ++round) {
        chachaQuarterRound(output, 0, 4, 8, 12);
        chachaQuarterRound(output, 1, 5, 9, 13);
        chachaQuarterRound(output, 2, 6, 10, 14);
        chachaQuarterRound(output, 3, 7, 11, 15);
        chachaQuarterRound(output, 0, 5, 10, 15);
        chachaQuarterRound(output, 1, 6, 11, 12);
        chachaQuarterRound(output, 2, 7, 8, 13);
        chachaQuarterRound(output, 3, 4, 9, 14);
    }
    for(std::size_t i = 0; i < output.size(); ++i)
        output[i] += input[i];
    return output;
}

void decryptPackinOne(std::vector<Byte> &payload, std::uint32_t seed,
                      const std::vector<Byte> &iv) {
    const auto key = makePackinOneKey(seed, iv);
    std::array<std::uint32_t, 16> state = {
        0x61707865u, 0x3320646eu, 0x79622d32u, 0x6b206574u,
    };
    for(std::size_t i = 0; i < 8; ++i)
        state[i + 4] = readLe32(key.data() + i * 4);
    state[12] = 0;
    state[13] = 0;
    state[14] = xxh32(iv.data(), iv.size(), seed);
    state[15] = seed;

    std::uint32_t zeroFallback = seed ^ state[14];
    if(zeroFallback == 0)
        zeroFallback = seed != 0 ? seed : 0xffffffffu;

    std::array<Byte, 1024> keyStream;
    std::size_t position = 0;
    while(position < payload.size()) {
        const auto firstBlock = makeChaCha8Block(state);
        for(std::size_t i = 0; i < firstBlock.size(); ++i)
            writeLe32(keyStream.data() + i * 4, firstBlock[i]);
        for(std::size_t offset = 64; offset < keyStream.size(); offset += 4) {
            std::uint32_t value = readLe32(keyStream.data() + offset - 64);
            value ^= value << 13;
            value ^= value >> 17;
            value ^= value << 5;
            if(value == 0)
                value = zeroFallback;
            writeLe32(keyStream.data() + offset, value);
        }

        const std::size_t chunkSize =
            std::min(keyStream.size(), payload.size() - position);
        for(std::size_t i = 0; i < chunkSize; ++i)
            payload[position + i] ^= keyStream[i];
        position += chunkSize;
        if(++state[12] == 0)
            ++state[13];
    }
}

class ByteChecker {
public:
    explicit ByteChecker(std::uint32_t seed) :
        seed_((seed & 0xffffff00u) |
              ((seed ^ (seed >> 24)) & 0xffu)) {}

    Byte next(std::uint8_t type) {
        auto seed = bytes();
        if(type == 0)
            return seed[2];

        round(seed);
        seed_ = readLe32(seed.data());
        return seed[2];
    }

    std::uint32_t finalCheck() const {
        auto seed = bytes();
        round(seed);
        round(seed);
        round(seed);
        std::swap(seed[0], seed[2]);
        return readLe32(seed.data());
    }

private:
    std::uint32_t seed_;

    std::array<Byte, 4> bytes() const {
        return {
            static_cast<Byte>(seed_ & 0xff),
            static_cast<Byte>((seed_ >> 8) & 0xff),
            static_cast<Byte>((seed_ >> 16) & 0xff),
            static_cast<Byte>((seed_ >> 24) & 0xff),
        };
    }

    static void round(std::array<Byte, 4> &seed) {
        const Byte doubled = static_cast<Byte>(seed[0] * 2u);
        const Byte a = static_cast<Byte>(seed[0] ^ doubled);
        Byte b = static_cast<Byte>(a >> 2);
        b = static_cast<Byte>(b ^ seed[2]);
        b = static_cast<Byte>(b >> 3);
        b = static_cast<Byte>(b ^ seed[2]);
        b = static_cast<Byte>(b ^ a);

        seed[0] = seed[1];
        seed[1] = seed[2];
        seed[2] = b;
    }
};

class Reader {
public:
    Reader(const std::vector<Byte> &bytes, std::size_t position = 0) :
        bytes_(bytes), position_(position) {
        if(position_ > bytes_.size())
            throw std::runtime_error("invalid TJS/ns0 data offset");
    }

    std::size_t remaining() const { return bytes_.size() - position_; }

    Byte readU8() {
        require(1);
        return bytes_[position_++];
    }

    std::uint16_t readU16() {
        require(2);
        const std::uint16_t value =
            static_cast<std::uint16_t>(bytes_[position_]) |
            (static_cast<std::uint16_t>(bytes_[position_ + 1]) << 8);
        position_ += 2;
        return value;
    }

    std::uint32_t readU32() {
        require(4);
        const auto value = readLe32(bytes_.data() + position_);
        position_ += 4;
        return value;
    }

    std::uint64_t readU64() {
        const std::uint64_t low = readU32();
        const std::uint64_t high = readU32();
        return low | (high << 32);
    }

    TjsString readString() {
        const auto length = static_cast<std::size_t>(readU32());
        if(length > remaining() / 2)
            throw std::runtime_error("truncated TJS/ns0 string");

        TjsString value;
        value.reserve(length);
        for(std::size_t i = 0; i < length; ++i)
            value.push_back(static_cast<tjs_char>(readU16()));
        return value;
    }

private:
    const std::vector<Byte> &bytes_;
    std::size_t position_;

    void require(std::size_t size) const {
        if(size > remaining())
            throw std::runtime_error("truncated TJS/ns0 data");
    }
};

tTJSVariant makeArray() {
    iTJSDispatch2 *array = TJSCreateArrayObject();
    if(!array)
        throw std::runtime_error("cannot create TJS/ns0 array");
    tTJSVariant value(array, array);
    array->Release();
    return value;
}

tTJSVariant makeDictionary() {
    iTJSDispatch2 *dictionary = TJSCreateDictionaryObject();
    if(!dictionary)
        throw std::runtime_error("cannot create TJS/ns0 dictionary");
    tTJSVariant value(dictionary, dictionary);
    dictionary->Release();
    return value;
}

tTJSVariant readValue(Reader &reader, ByteChecker &checker,
                      std::size_t depth) {
    if(depth > TJS_NS0_MAX_DEPTH)
        throw std::runtime_error("TJS/ns0 nesting is too deep");

    const std::uint16_t encodedType = reader.readU16();
    const Byte type = static_cast<Byte>(encodedType & 0xff);
    const Byte actualCheck = static_cast<Byte>(encodedType >> 8);
    if(actualCheck != checker.next(type))
        throw std::runtime_error("TJS/ns0 byte check failed");

    switch(type) {
        case 0:
            return {};
        case 2: {
            const TjsString text = reader.readString();
            return tTJSVariant(ttstr(text.data(),
                                     static_cast<tjs_int>(text.size())));
        }
        case 4: {
            const std::uint64_t bits = reader.readU64();
            tjs_int64 value;
            std::memcpy(&value, &bits, sizeof(value));
            return tTJSVariant(value);
        }
        case 5: {
            const std::uint64_t bits = reader.readU64();
            double value;
            std::memcpy(&value, &bits, sizeof(value));
            return tTJSVariant(value);
        }
        case 0x81: {
            const auto count = static_cast<std::size_t>(reader.readU32());
            if(count > reader.remaining() / 2)
                throw std::runtime_error("invalid TJS/ns0 array size");

            tTJSVariant result = makeArray();
            iTJSDispatch2 *array = result.AsObjectNoAddRef();
            for(std::size_t i = 0; i < count; ++i) {
                if(i > static_cast<std::size_t>(
                           std::numeric_limits<tjs_int>::max()))
                    throw std::runtime_error("TJS/ns0 array is too large");
                tTJSVariant value = readValue(reader, checker, depth + 1);
                array->PropSetByNum(TJS_MEMBERENSURE,
                                    static_cast<tjs_int>(i), &value, array);
            }
            return result;
        }
        case 0xc1: {
            const auto count = static_cast<std::size_t>(reader.readU32());
            if(count > reader.remaining() / 6)
                throw std::runtime_error("invalid TJS/ns0 dictionary size");

            tTJSVariant result = makeDictionary();
            iTJSDispatch2 *dictionary = result.AsObjectNoAddRef();
            for(std::size_t i = 0; i < count; ++i) {
                const TjsString key = reader.readString();
                tTJSVariant value = readValue(reader, checker, depth + 1);
                dictionary->PropSet(TJS_MEMBERENSURE, key.c_str(), nullptr,
                                    &value, dictionary);
            }
            return result;
        }
        default:
            throw std::runtime_error("unsupported TJS/ns0 value type");
    }
}

std::vector<Byte> readAll(tTJSBinaryStream *stream) {
    const tjs_uint64 size64 = stream->GetSize();
    if(size64 > std::numeric_limits<tjs_uint>::max())
        throw std::runtime_error("TJS/ns0 data is too large");

    const auto size = static_cast<tjs_uint>(size64);
    std::vector<Byte> bytes(size);
    stream->Seek(0, TJS_BS_SEEK_SET);
    if(size > 0)
        stream->ReadBuffer(bytes.data(), size);
    return bytes;
}

bool decompressSizedLz4(const std::vector<Byte> &packed,
                        std::vector<Byte> &unpacked) {
    if(packed.size() < 4)
        return false;
    const std::uint32_t unpackedSize = readLe32(packed.data());
    const std::size_t packedSize = packed.size() - 4;
    if(unpackedSize > TJS_NS0_MAX_UNPACKED_SIZE ||
       unpackedSize > static_cast<std::uint32_t>(
                          std::numeric_limits<int>::max()) ||
       packedSize > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return false;

    unpacked.resize(unpackedSize);
    const int decoded = LZ4_decompress_safe(
        reinterpret_cast<const char *>(packed.data() + 4),
        reinterpret_cast<char *>(unpacked.data()),
        static_cast<int>(packedSize), static_cast<int>(unpackedSize));
    return decoded >= 0 && static_cast<std::uint32_t>(decoded) == unpackedSize;
}

bool decompressPackinOneLz4(const std::vector<Byte> &packed,
                            std::vector<Byte> &unpacked) {
    unpacked.clear();
    std::size_t position = 0;
    while(position < packed.size()) {
        if(packed.size() - position < 2)
            return false;
        const std::size_t blockSize =
            static_cast<std::size_t>(packed[position]) |
            (static_cast<std::size_t>(packed[position + 1]) << 8);
        position += 2;
        if(blockSize == 0 || blockSize > packed.size() - position ||
           blockSize > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            return false;

        std::size_t capacity = 64u * 1024u;
        std::vector<Byte> block;
        int decoded = -1;
        while(capacity <= TJS_NS0_MAX_UNPACKED_SIZE - unpacked.size()) {
            block.resize(capacity);
            decoded = LZ4_decompress_safe(
                reinterpret_cast<const char *>(packed.data() + position),
                reinterpret_cast<char *>(block.data()),
                static_cast<int>(blockSize), static_cast<int>(capacity));
            if(decoded >= 0)
                break;
            if(capacity > TJS_NS0_MAX_UNPACKED_SIZE / 2)
                break;
            capacity *= 2;
        }
        if(decoded <= 0 || static_cast<std::size_t>(decoded) >
                               TJS_NS0_MAX_UNPACKED_SIZE - unpacked.size())
            return false;
        unpacked.insert(unpacked.end(), block.begin(),
                        block.begin() + decoded);
        position += blockSize;
    }
    return !unpacked.empty();
}

std::vector<Byte> decompressLz4(const std::vector<Byte> &packed,
                                bool preferPackinOneFraming) {
    std::vector<Byte> unpacked;
    const bool firstSucceeded = preferPackinOneFraming
                                    ? decompressPackinOneLz4(packed, unpacked)
                                    : decompressSizedLz4(packed, unpacked);
    if(firstSucceeded)
        return unpacked;
    const bool fallbackSucceeded = preferPackinOneFraming
                                       ? decompressSizedLz4(packed, unpacked)
                                       : decompressPackinOneLz4(packed, unpacked);
    if(!fallbackSucceeded)
        throw std::runtime_error("invalid TJS/4s0 compressed data");
    return unpacked;
}

} // namespace

bool TVPLoadTjsNs0DataPack(tTJSBinaryStream *stream, tTJSVariant *result,
                           const ttstr &outerIv) {
    if(!stream)
        return false;

    const std::vector<Byte> bytes = readAll(stream);
    if(bytes.size() < TJS_NS0_HEADER_SIZE ||
       !std::equal(TJS_NS0_MAGIC.begin(), TJS_NS0_MAGIC.end(),
                   bytes.begin()) ||
       bytes[5] != 's' || bytes[6] != '0' || bytes[7] != 0)
        return false;

    const Byte compression = bytes[4];
    if(compression != 'n' && compression != '4')
        throw std::runtime_error("unsupported TJS/ns0 compression");

    const std::uint32_t seed = readLe32(bytes.data() + 8);
    const std::uint16_t crypt = static_cast<std::uint16_t>(bytes[12]) |
                                (static_cast<std::uint16_t>(bytes[13]) << 8);
    const std::uint16_t ivLength = static_cast<std::uint16_t>(bytes[14]) |
                                   (static_cast<std::uint16_t>(bytes[15]) << 8);
    if(TJS_NS0_HEADER_SIZE + ivLength > bytes.size())
        throw std::runtime_error("truncated TJS/ns0 IV");
    if(crypt != 0 && crypt != 1)
        throw std::runtime_error("unsupported TJS/ns0 encryption");

    std::vector<Byte> payload(bytes.begin() + TJS_NS0_HEADER_SIZE + ivLength,
                              bytes.end());
    if(crypt == 1) {
        std::vector<Byte> iv;
        if(!outerIv.IsEmpty()) {
            const std::string utf8Iv = outerIv.AsStdString();
            iv.assign(utf8Iv.begin(), utf8Iv.end());
        } else {
            iv.assign(bytes.begin() + TJS_NS0_HEADER_SIZE,
                      bytes.begin() + TJS_NS0_HEADER_SIZE + ivLength);
        }
        decryptPackinOne(payload, seed, iv);
    }
    if(compression == '4')
        payload = decompressLz4(payload, crypt == 1);

    Reader reader(payload);
    ByteChecker checker(seed);
    tTJSVariant value = readValue(reader, checker, 0);
    if(reader.readU32() != checker.finalCheck())
        throw std::runtime_error("TJS/ns0 checksum mismatch");

    if(result)
        *result = value;
    return true;
}

namespace {

bool loadTjsNs0DataPackWithoutOuterIv(tTJSBinaryStream *stream,
                                      tTJSVariant *result) {
    return TVPLoadTjsNs0DataPack(stream, result);
}

} // namespace

extern "C" void TVPRegisterTjsNs0DataPackLoader() {
    TJS::TJSSetStructuredDataPackLoader(&loadTjsNs0DataPackWithoutOuterIv);
}

namespace {

struct TjsNs0DataPackLoaderRegistration {
    TjsNs0DataPackLoaderRegistration() {
        TVPRegisterTjsNs0DataPackLoader();
    }
} TjsNs0DataPackLoaderRegistrationInstance;

} // namespace
