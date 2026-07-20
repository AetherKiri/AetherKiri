#include "kbadDataPack.h"

#include "tjsArray.h"
#include "tjsDictionary.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Byte = std::uint8_t;
using KbadString = std::basic_string<tjs_char>;

constexpr char KBAD_SIGNATURE[] = "KBAD100";
constexpr std::size_t KBAD_HEADER_SIZE = 8;
constexpr std::size_t KBAD_MAX_DEPTH = 128;
constexpr std::size_t KBAD_MAX_CONTAINER_ITEMS = 1u << 24;

bool hasKbadSignature(const Byte *data, std::size_t size) {
    return data && size >= KBAD_HEADER_SIZE &&
           std::equal(std::begin(KBAD_SIGNATURE),
                      std::end(KBAD_SIGNATURE) - 1, data) &&
           data[7] == 0;
}

tTJSVariant makeDictionary() {
    iTJSDispatch2 *dictionary = TJSCreateDictionaryObject();
    tTJSVariant value(dictionary, dictionary);
    dictionary->Release();
    return value;
}

tTJSVariant makeArray() {
    iTJSDispatch2 *array = TJSCreateArrayObject();
    tTJSVariant value(array, array);
    array->Release();
    return value;
}

void setDictionaryValue(iTJSDispatch2 *dictionary, const KbadString &key,
                        tTJSVariant &value) {
    if(dictionary)
        dictionary->PropSet(TJS_MEMBERENSURE, key.c_str(), nullptr, &value,
                            dictionary);
}

class KbadDecoder {
public:
    KbadDecoder(const Byte *data, std::size_t size) : data_(data), size_(size) {}

    tTJSVariant decode() {
        position_ = KBAD_HEADER_SIZE;
        tTJSVariant value = decodeValue(0);
        if(position_ != size_)
            throw std::runtime_error("trailing KBAD data");
        return value;
    }

private:
    Byte readU8() {
        require(1);
        return data_[position_++];
    }

    std::uint16_t readU16() {
        require(2);
        const std::uint16_t value = data_[position_] |
            (static_cast<std::uint16_t>(data_[position_ + 1]) << 8);
        position_ += 2;
        return value;
    }

    std::uint32_t readU32() {
        require(4);
        const std::uint32_t value = data_[position_] |
            (static_cast<std::uint32_t>(data_[position_ + 1]) << 8) |
            (static_cast<std::uint32_t>(data_[position_ + 2]) << 16) |
            (static_cast<std::uint32_t>(data_[position_ + 3]) << 24);
        position_ += 4;
        return value;
    }

    std::uint64_t readU64() {
        require(8);
        std::uint64_t value = 0;
        for(int shift = 0; shift < 64; shift += 8)
            value |= static_cast<std::uint64_t>(data_[position_++]) << shift;
        return value;
    }

    float readFloat32() {
        const std::uint32_t bits = readU32();
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    double readFloat64() {
        const std::uint64_t bits = readU64();
        double value = 0.0;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    KbadString readString(std::size_t characterCount) {
        if(characterCount > (size_ - position_) / 2)
            throw std::runtime_error("truncated KBAD string");

        KbadString value;
        value.reserve(characterCount);
        for(std::size_t index = 0; index < characterCount; ++index) {
            const tjs_char character = static_cast<tjs_char>(
                data_[position_] |
                (static_cast<std::uint16_t>(data_[position_ + 1]) << 8));
            position_ += 2;
            if(character != 0)
                value.push_back(character);
        }
        return value;
    }

    tTJSVariant decodeValue(std::size_t depth) {
        if(depth > KBAD_MAX_DEPTH)
            throw std::runtime_error("KBAD nesting is too deep");

        const Byte type = readU8();
        if(type <= 0x7f)
            return tTJSVariant(static_cast<tTVInteger>(type));
        if(type >= 0xe0)
            return tTJSVariant(static_cast<tTVInteger>(
                static_cast<std::int8_t>(type)));

        if(type >= 0x80 && type <= 0x8f)
            return decodeMap(type & 0x0f, depth + 1);
        if(type >= 0x90 && type <= 0x9f)
            return decodeArray(type & 0x0f, depth + 1);
        if(type >= 0xa0 && type <= 0xbf)
            return tTJSVariant(ttstr(readString(type & 0x1f)));

        switch(type) {
        case 0xc0:
        case 0xc1:
            return tTJSVariant();
        case 0xc2:
            return tTJSVariant(false);
        case 0xc3:
            return tTJSVariant(true);
        case 0xc4:
            return tTJSVariant(ttstr(readString(readU8())));
        case 0xc5:
            return tTJSVariant(ttstr(readString(readU16())));
        case 0xc6:
            return tTJSVariant(ttstr(readString(readU32())));
        case 0xca:
            return tTJSVariant(static_cast<tTVReal>(readFloat32()));
        case 0xcb:
            return tTJSVariant(static_cast<tTVReal>(readFloat64()));
        case 0xcc:
            return tTJSVariant(static_cast<tTVInteger>(readU8()));
        case 0xcd:
            return tTJSVariant(static_cast<tTVInteger>(readU16()));
        case 0xce:
            return tTJSVariant(static_cast<tTVInteger>(readU32()));
        case 0xcf:
            return tTJSVariant(static_cast<tTVInteger>(readU64()));
        case 0xd0:
            return tTJSVariant(static_cast<tTVInteger>(
                static_cast<std::int8_t>(readU8())));
        case 0xd1:
            return tTJSVariant(static_cast<tTVInteger>(
                static_cast<std::int16_t>(readU16())));
        case 0xd2:
            return tTJSVariant(static_cast<tTVInteger>(
                static_cast<std::int32_t>(readU32())));
        case 0xd3:
            return tTJSVariant(static_cast<tTVInteger>(
                static_cast<std::int64_t>(readU64())));
        case 0xd9:
            return tTJSVariant(ttstr(readString(readU8())));
        case 0xda:
            return tTJSVariant(ttstr(readString(readU16())));
        case 0xdb:
            return tTJSVariant(ttstr(readString(readU32())));
        case 0xdc:
            return decodeArray(readU16(), depth + 1);
        case 0xdd:
            return decodeArray(readU32(), depth + 1);
        case 0xde:
            return decodeMap(readU16(), depth + 1);
        case 0xdf:
            return decodeMap(readU32(), depth + 1);
        default:
            throw std::runtime_error("unsupported KBAD type");
        }
    }

    tTJSVariant decodeArray(std::size_t count, std::size_t depth) {
        validateContainerCount(count);
        tTJSVariant result = makeArray();
        iTJSDispatch2 *array = result.AsObjectNoAddRef();
        for(std::size_t index = 0; index < count; ++index) {
            tTJSVariant value = decodeValue(depth);
            array->PropSetByNum(TJS_MEMBERENSURE, static_cast<tjs_int>(index),
                                &value, array);
        }
        return result;
    }

    tTJSVariant decodeMap(std::size_t count, std::size_t depth) {
        validateContainerCount(count);
        tTJSVariant result = makeDictionary();
        iTJSDispatch2 *dictionary = result.AsObjectNoAddRef();
        for(std::size_t index = 0; index < count; ++index) {
            tTJSVariant keyValue = decodeValue(depth);
            tTJSVariant value = decodeValue(depth);
            const tjs_char *key = keyValue.GetString();
            if(key)
                setDictionaryValue(dictionary, KbadString(key), value);
        }
        return result;
    }

    static void validateContainerCount(std::size_t count) {
        if(count > KBAD_MAX_CONTAINER_ITEMS ||
           count > static_cast<std::size_t>(std::numeric_limits<tjs_int>::max()))
            throw std::runtime_error("KBAD container is too large");
    }

    void require(std::size_t count) const {
        if(position_ > size_ || count > size_ - position_)
            throw std::runtime_error("truncated KBAD data");
    }

    const Byte *data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t position_ = KBAD_HEADER_SIZE;
};

} // namespace

bool TVPDecodeKbadDataPack(const void *data, std::size_t size,
                           tTJSVariant *result) {
    const auto *bytes = static_cast<const Byte *>(data);
    if(!hasKbadSignature(bytes, size))
        return false;

    KbadDecoder decoder(bytes, size);
    tTJSVariant decoded = decoder.decode();
    if(result)
        *result = decoded;
    return true;
}

bool TVPLoadKbadDataPack(tTJSBinaryStream *stream, tTJSVariant *result) {
    if(!stream)
        return false;

    const tjs_uint64 size64 = stream->GetSize();
    if(size64 > static_cast<tjs_uint64>(
                    std::numeric_limits<tjs_uint>::max()))
        throw std::runtime_error("KBAD storage is too large");

    const std::size_t size = static_cast<std::size_t>(size64);
    std::vector<Byte> bytes(size);
    stream->SetPosition(0);
    if(size > 0)
        stream->ReadBuffer(bytes.data(), static_cast<tjs_uint>(size));
    return TVPDecodeKbadDataPack(bytes.data(), bytes.size(), result);
}
