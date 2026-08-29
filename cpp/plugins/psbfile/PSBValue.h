//
// Created by lidong on 25-3-15.
//
#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>

#include "tjs.h"
#include "BitConverter.h"
#include "Consts.h"
#include "PSBExtension.h"

namespace PSB {

    enum class PSBObjType : unsigned char {
        None = 0x0,
        Null = 0x1,
        False = 0x2,
        True = 0x3,

        // int
        NumberN0 = 0x4,
        NumberN1 = 0x5,
        NumberN2 = 0x6,
        NumberN3 = 0x7,
        NumberN4 = 0x8,
        NumberN5 = 0x9,
        NumberN6 = 0xA,
        NumberN7 = 0xB,
        NumberN8 = 0xC,

        // array N(NUMBER) is count mask
        ArrayN1 = 0xD,
        ArrayN2 = 0xE,
        ArrayN3 = 0xF,
        ArrayN4 = 0x10,
        ArrayN5 = 0x11,
        ArrayN6 = 0x12,
        ArrayN7 = 0x13,
        ArrayN8 = 0x14,

        // index of key name, only used in PSBv1 (according to GMMan's doc)
        KeyNameN1 = 0x11,
        KeyNameN2 = 0x12,
        KeyNameN3 = 0x13,
        KeyNameN4 = 0x14,

        // index of strings table
        StringN1 = 0x15,
        StringN2 = 0x16,
        StringN3 = 0x17,
        StringN4 = 0x18,

        // resource of thunk
        ResourceN1 = 0x19,
        ResourceN2 = 0x1A,
        ResourceN3 = 0x1B,
        ResourceN4 = 0x1C,

        // fpu value
        Float0 = 0x1D,
        Float = 0x1E,
        Double = 0x1F,

        // objects
        List = 0x20, // object list
        Objects = 0x21, // object dictionary

        ExtraChunkN1 = 0x22,
        ExtraChunkN2 = 0x23,
        ExtraChunkN3 = 0x24,
        ExtraChunkN4 = 0x25,

    };

    namespace detail {
        // PSB collection counts are encoded in 1..8 little-endian bytes. A
        // malformed file must not be able to turn that width into a truncated
        // uint32_t or an unbounded allocation.  The limit is deliberately
        // generous for authored scenario data while keeping hostile input
        // bounded on all supported hosts.
        inline constexpr std::uint64_t kMaxCollectionEntries = 16ULL * 1024ULL * 1024ULL;

        inline std::vector<std::uint32_t>
        ReadPackedCollection(int countWidth, TJS::tTJSBinaryStream *stream,
                             std::uint8_t &entryLength) {
            if(!stream || countWidth < 0 || countWidth > 8) {
                throw std::runtime_error("PSB collection has invalid count width");
            }

            std::array<std::uint8_t, 8> countBytes{};
            if(countWidth != 0) {
                stream->ReadBuffer(countBytes.data(),
                                   static_cast<tjs_uint>(countWidth));
            }
            std::uint64_t count = 0;
            for(int i = 0; i < countWidth; ++i) {
                count |= static_cast<std::uint64_t>(countBytes[static_cast<std::size_t>(i)])
                    << (i * 8);
            }
            if(count > kMaxCollectionEntries ||
               count > static_cast<std::uint64_t>(
                           std::numeric_limits<std::size_t>::max())) {
                throw std::runtime_error("PSB collection is too large");
            }

            const auto encodedLength = stream->ReadI8LE();
            const auto firstLength = static_cast<std::uint8_t>(PSBObjType::NumberN8);
            constexpr std::uint8_t kMaxEntryLength = 4;
            if(encodedLength < firstLength ||
               encodedLength > static_cast<std::uint8_t>(firstLength + kMaxEntryLength)) {
                throw std::runtime_error("PSB collection has invalid entry width");
            }
            entryLength = static_cast<std::uint8_t>(encodedLength - firstLength);

            const auto position = stream->GetPosition();
            const auto size = stream->GetSize();
            if(position > size) {
                throw std::runtime_error("PSB collection starts past end of stream");
            }
            const auto remaining = size - position;
            if(entryLength != 0 &&
               count > remaining / static_cast<std::uint64_t>(entryLength)) {
                throw std::runtime_error("PSB collection entries exceed stream");
            }

            const auto byteCount = count * static_cast<std::uint64_t>(entryLength);
            if(byteCount > static_cast<std::uint64_t>(
                               std::numeric_limits<tjs_uint>::max())) {
                throw std::runtime_error("PSB collection byte count is too large");
            }
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(byteCount));
            if(!bytes.empty()) {
                stream->ReadBuffer(bytes.data(), static_cast<tjs_uint>(byteCount));
            }

            std::vector<std::uint32_t> values;
            values.reserve(static_cast<std::size_t>(count));
            for(std::uint64_t i = 0; i < count; ++i) {
                std::uint32_t result = 0;
                for(std::uint8_t j = 0; j < entryLength; ++j) {
                    result |= static_cast<std::uint32_t>(
                                  bytes[static_cast<std::size_t>(i * entryLength + j)])
                        << (j * 8);
                }
                values.push_back(result);
            }
            return values;
        }
    } // namespace detail

    class IPSBValue {
    public:
        virtual ~IPSBValue() = default;
        [[nodiscard]] virtual PSBObjType getType() const = 0;
        [[nodiscard]] virtual std::string toString() = 0;
        [[nodiscard]] virtual tTJSVariant toTJSVal() const = 0;
    };

    class IPSBCollection;
    class IPSBChild : public IPSBValue {
    public:
        // Collections own their children.  The reverse link must not keep the
        // complete parsed PSB tree alive after its motion snapshot is evicted.
        std::weak_ptr<IPSBCollection> parent;

        std::string path;
    };

    class IPSBCollection : public IPSBChild {
    public:
        using K = std::string;
        using V = std::shared_ptr<IPSBValue>;

        virtual V operator[](int index) = 0;
        virtual V operator[](const K &key) = 0;

        virtual V operator[](int index) const = 0;
        virtual V operator[](const K &key) const = 0;
    };

    class IPSBSingleton {
    public:
        // Singleton values can be referenced by several collections, but none
        // of those back-references participate in ownership.
        std::vector<std::weak_ptr<IPSBCollection>> parents;
    };

    struct PSBNull : IPSBValue {
        [[nodiscard]] PSBObjType getType() const override {
            return PSB::PSBObjType::Null;
        }
        std::string toString() override { return "null"; }

        [[nodiscard]] tTJSVariant toTJSVal() const override;
    };

    struct PSBBool : IPSBValue {
        bool value{};
        explicit PSBBool(bool value = false) { this->value = value; }

        [[nodiscard]] PSBObjType getType() const override {
            return value ? PSB::PSBObjType::True : PSB::PSBObjType::False;
        }

        std::string toString() override { return value ? "true" : "false"; }

        [[nodiscard]] tTJSVariant toTJSVal() const override;
    };

    enum class PSBNumberType {
        Int,
        Long,
        Float,
        Double,
    };

    struct PSBNumber : IPSBValue {

        std::vector<std::uint8_t> data;

        PSBNumberType numberType;

        explicit PSBNumber() : data(8), numberType(PSBNumberType::Long) {}

        explicit PSBNumber(int val) :
            data(BitConverter::toByteArray(val)),
            numberType(PSBNumberType::Int) {}

        explicit PSBNumber(std::vector<std::uint8_t> data, PSBNumberType type) :
            data(std::move(data)), numberType(type) {}
        explicit PSBNumber(PSBObjType objType, TJS::tTJSBinaryStream *stream);

        [[nodiscard]] tTJSVariant toTJSVal() const override;

        [[nodiscard]] std::string toString() override {
            switch(numberType) {
                case PSBNumberType::Int:
                    return std::to_string(getValue<int>());
                case PSBNumberType::Float:
                    return std::to_string(getValue<float>());
                case PSBNumberType::Double:
                    return std::to_string(getValue<double>());
                case PSBNumberType::Long:
                default:
                    return std::to_string(getValue<long>());
            }
        }

        [[nodiscard]] std::int64_t getLongValue() const;

        [[nodiscard]] float getFloatValue() const;

        template <typename T>
        void setValue(T value) {
            data = BitConverter::toByteArray(value);
        }

        template <typename T>
        [[nodiscard]] T getValue() const {
            return BitConverter::fromByteArray<T>(data);
        }

        [[nodiscard]] PSBObjType getType() const override;

        explicit operator int() const {
            if(this->numberType != PSBNumberType::Int) {
                throw std::runtime_error("not int type!");
            }
            return this->getValue<int>();
        }
    };

    struct PSBArray : IPSBValue {
        std::uint8_t entryLength{ 4 };
        std::vector<std::uint32_t> value{};
        explicit PSBArray() = default;
        explicit PSBArray(int n, TJS::tTJSBinaryStream *stream) {
            value = detail::ReadPackedCollection(n, stream, entryLength);
        }

        [[nodiscard]] tTJSVariant toTJSVal() const override;

        std::string toString() override {
            return fmt::format("Array[{}]", value.size());
        }

        std::uint32_t operator[](int index) const { return value[index]; }

        [[nodiscard]] PSBObjType getType() const override {
            switch(Extension::getSize(value.size())) {
                case 0:
                case 1:
                    return PSBObjType::ArrayN1;
                case 2:
                    return PSBObjType::ArrayN2;
                case 3:
                    return PSBObjType::ArrayN3;
                case 4:
                    return PSBObjType::ArrayN4;
                case 5:
                    return PSBObjType::ArrayN5;
                case 6:
                    return PSBObjType::ArrayN6;
                case 7:
                    return PSBObjType::ArrayN7;
                case 8:
                    return PSBObjType::ArrayN8;
                default:
                    throw std::runtime_error("Not a valid array");
            }
        }
    };

    struct PSBString : IPSBValue {
        std::optional<std::uint32_t> index{};

        std::string value;

        explicit PSBString(int n, TJS::tTJSBinaryStream *stream) {
            std::uint32_t tmp{};
            stream->Read(&tmp, n);
            index = tmp;
        }

        explicit PSBString(std::string value = "",
                           std::optional<std::uint32_t> index = {}) :
            index(index), value(std::move(value)) {}


        [[nodiscard]] tTJSVariant toTJSVal() const override;

        std::string toString() override { return value; }

        [[nodiscard]] PSBObjType getType() const override {

            switch(Extension::getSize(index.value_or(0))) {
                case 0:
                case 1:
                    return PSBObjType::StringN1;
                case 2:
                    return PSBObjType::StringN2;
                case 3:
                    return PSBObjType::StringN3;
                case 4:
                    return PSBObjType::StringN4;
                default:
                    throw std::runtime_error("String index has wrong size");
            }
        }
    };

    struct PSBResource : IPSBValue, IPSBSingleton {
        bool isExtra = false;
        std::optional<std::uint32_t> index{};
        std::vector<std::uint8_t> data{};

        explicit PSBResource() = default;
        explicit PSBResource(int n, TJS::tTJSBinaryStream *stream) {
            std::uint32_t tmp{};
            stream->Read(&tmp, n);
            index = tmp;
        }

        PSBResource(const PSBResource &) = default;

        [[nodiscard]] tTJSVariant toTJSVal() const override;

        std::string toString() override {
            return fmt::format("{{({})}}{{{}}}",
                               isExtra ? Consts::ExtraResourceIdentifier
                                       : Consts::ResourceIdentifier,
                               index.value_or(-1));
        }

        [[nodiscard]] PSBObjType getType() const override {

            switch(Extension::getSize(index.value_or(0))) {
                case 0:
                case 1:
                    return isExtra ? PSBObjType::ExtraChunkN1
                                   : PSBObjType::ResourceN1;
                case 2:
                    return isExtra ? PSBObjType::ExtraChunkN2
                                   : PSBObjType::ResourceN2;
                case 3:
                    return isExtra ? PSBObjType::ExtraChunkN3
                                   : PSBObjType::ResourceN3;
                case 4:
                    return isExtra ? PSBObjType::ExtraChunkN4
                                   : PSBObjType::ResourceN4;
                default:
                    throw std::runtime_error("Not a valid resource");
            }
        }
    };

    class PSBDictionary : public IPSBCollection {
        using Map = std::unordered_map<K, V>;

    public:
        explicit PSBDictionary() = default;
        explicit PSBDictionary(int capacity) : _map(capacity) {}

        template <typename T>
        bool tryGetPsbValue(const K &key, T *&val) {
            auto it = _map.find(key);
            if(it != _map.end()) {
                val = dynamic_cast<T *>(it->second);
                return val != nullptr;
            }
            val = nullptr;
            return false;
        }

        std::string toString() override {
            return fmt::format("Dictionary[{}]", _map.size());
        }

        void emplace(const K &key, const V &val) { _map.emplace(key, val); }

        [[nodiscard]] auto begin() const { return _map.begin(); }
        [[nodiscard]] auto end() const { return _map.end(); }
        [[nodiscard]] auto find(const K &key) const { return _map.find(key); }
        [[nodiscard]] size_t size() const { return _map.size(); }

        [[nodiscard]] V operator[](int index) override { return get(index); }
        [[nodiscard]] V operator[](const K &key) override { return get(key); }
        [[nodiscard]] V operator[](int index) const override {
            return get(index);
        }
        [[nodiscard]] V operator[](const K &key) const override {
            return get(key);
        }

        [[nodiscard]] PSBObjType getType() const override {
            return PSBObjType::Objects;
        }

        void unionWith(const PSBDictionary &dic) {
            for(const auto &[key, val] : dic) {
                if(_map.find(key) != _map.end()) {
                    auto *childDic =
                        dynamic_cast<PSBDictionary *>(_map[key].get());
                    auto *otherDic = dynamic_cast<PSBDictionary *>(val.get());
                    if(childDic && otherDic) {
                        childDic->unionWith(*otherDic);
                    }
                } else {
                    _map.emplace(key, val);
                }
            }
        }

        [[nodiscard]] tTJSVariant toTJSVal() const override;

    private:
        [[nodiscard]] V get(const K &key) const {
            auto it = _map.find(key);
            return it != _map.end() ? it->second : nullptr;
        }

        [[nodiscard]] V get(int index) const {
            return operator[](fmt::format("{}", index));
        }

    private:
        Map _map{};
        // IPSBCollection *parent = nullptr;
    };

    class PSBList : public IPSBCollection {

        using V = std::shared_ptr<IPSBValue>;
        using Vec = std::vector<V>;

    public:
        std::uint8_t entryLength{ 4 };

        explicit PSBList(size_t capacity) { _vec.reserve(capacity); }

        [[nodiscard]] tTJSVariant toTJSVal() const override;

        std::string toString() override {
            return fmt::format("List[{}]", _vec.size());
        }

        void push_back(const V &val) { _vec.push_back(val); }
        [[nodiscard]] auto begin() const { return _vec.begin(); }
        [[nodiscard]] auto end() const { return _vec.end(); }

        [[nodiscard]] V operator[](int index) override { return get(index); }
        [[nodiscard]] V operator[](const K &key) override { return get(key); }
        [[nodiscard]] V operator[](int index) const override {
            return get(index);
        }
        [[nodiscard]] V operator[](const K &key) const override {
            return get(key);
        }

        [[nodiscard]] PSBObjType getType() const override {
            return PSBObjType::List;
        }

        [[nodiscard]] size_t size() const { return _vec.size(); }

        static std::vector<std::uint32_t>
        loadIntoList(int n, TJS::tTJSBinaryStream *stream) {
            std::uint8_t entryLength = 0;
            return detail::ReadPackedCollection(n, stream, entryLength);
        }

    private:
        [[nodiscard]] V get(const K &key) const {
            assert(false && "not implement method: operator[](std::string)!");
            return nullptr;
        }

        [[nodiscard]] V get(int index) const { return _vec[index]; }

    private:
        Vec _vec{};
    };
} // namespace PSB
