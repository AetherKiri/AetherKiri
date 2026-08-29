#include "portableResourceObjects.h"

#include "ncbind.hpp"
#include "simplebinder.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

namespace AetherKiri::ResourceObjects {
namespace {

constexpr std::size_t kIconHeaderBytes = 6;
constexpr std::size_t kIconEntryBytes = 16;
constexpr std::size_t kGroupEntryBytes = 14;
constexpr std::uint16_t kIconType = 1;
constexpr std::uint16_t kCursorType = 2;

bool addSize(std::size_t &value, std::size_t amount) {
    if(amount > std::numeric_limits<std::size_t>::max() - value)
        return false;
    value += amount;
    return true;
}

std::uint16_t readU16(const std::uint8_t *p) {
    return static_cast<std::uint16_t>(p[0]) |
           (static_cast<std::uint16_t>(p[1]) << 8);
}

std::uint32_t readU32(const std::uint8_t *p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

void putU16(std::vector<std::uint8_t> &out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xffu));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
}

void putU32(std::vector<std::uint8_t> &out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffu));
}

bool validOctet(const tTJSVariant &value, const std::uint8_t *&data,
                std::size_t &size) {
    data = nullptr;
    size = 0;
    if(value.Type() != tvtOctet)
        return false;
    const auto *octet = value.AsOctetNoAddRef();
    if(!octet)
        return true;
    size = octet->GetLength();
    data = octet->GetData();
    return size <= kMaxObjectBytes && (size == 0 || data != nullptr);
}

void setOctet(tTJSVariant *result, const std::vector<std::uint8_t> &data) {
    if(!result)
        return;
    if(data.size() > std::numeric_limits<tjs_uint>::max()) {
        result->Clear();
        return;
    }
    auto *octet = TJSAllocVariantOctet(
        data.empty() ? nullptr : data.data(), static_cast<tjs_uint>(data.size()));
    if(!octet) {
        result->Clear();
        return;
    }
    *result = octet;
    octet->Release();
}

tTJSVariant makeIntegerArray(const std::vector<std::uint32_t> &values) {
    iTJSDispatch2 *array = TJSCreateArrayObject();
    if(!array)
        return tTJSVariant();
    for(std::size_t i = 0; i < values.size(); ++i) {
        tTJSVariant value(static_cast<tjs_int64>(values[i]));
        array->PropSetByNum(TJS_MEMBERENSURE, static_cast<tjs_int>(i), &value,
                            array);
    }
    tTJSVariant result(array, array);
    array->Release();
    return result;
}

} // namespace

// -------------------------------------------------------------------------
// ICO/CUR image and RT_GROUP_ICON/CURSOR codecs
// -------------------------------------------------------------------------

void IconImage::clear() {
    type_ = kIconType;
    entries_.clear();
}

const IconEntry *IconImage::entry(std::size_t index) const {
    return index < entries_.size() ? &entries_[index] : nullptr;
}

IconEntry *IconImage::entry(std::size_t index) {
    return index < entries_.size() ? &entries_[index] : nullptr;
}

bool IconImage::load(const std::uint8_t *data, std::size_t size) {
    clear();
    if(!data || size < kIconHeaderBytes || size > kMaxObjectBytes)
        return false;
    const std::uint16_t reserved = readU16(data);
    const std::uint16_t type = readU16(data + 2);
    const std::uint16_t count = readU16(data + 4);
    if(reserved != 0 || (type != kIconType && type != kCursorType) ||
       count == 0 || count > kMaxIconImages)
        return false;
    const std::size_t tableBytes = kIconHeaderBytes +
        static_cast<std::size_t>(count) * kIconEntryBytes;
    if(tableBytes > size)
        return false;

    type_ = type;
    entries_.reserve(count);
    std::size_t payloadTotal = 0;
    for(std::size_t i = 0; i < count; ++i) {
        const std::uint8_t *p = data + kIconHeaderBytes + i * kIconEntryBytes;
        IconEntry entry;
        entry.width = p[0];
        entry.height = p[1];
        entry.colorCount = p[2];
        entry.reserved = p[3];
        entry.first = readU16(p + 4);
        entry.second = readU16(p + 6);
        entry.bytesInRes = readU32(p + 8);
        entry.imageOffset = readU32(p + 12);
        const std::size_t offset = entry.imageOffset;
        const std::size_t imageSize = entry.bytesInRes;
        if(imageSize > kMaxObjectBytes || offset < tableBytes ||
           offset > size || imageSize > size - offset ||
           !addSize(payloadTotal, imageSize) ||
           payloadTotal > kMaxObjectBytes)
            return clear(), false;
        entry.image.assign(data + offset, data + offset + imageSize);
        entry.id = -1;
        entries_.push_back(std::move(entry));
    }
    return true;
}

bool IconImage::save(std::vector<std::uint8_t> &data) const {
    data.clear();
    if(entries_.empty() || entries_.size() > kMaxIconImages)
        return false;
    std::size_t total = kIconHeaderBytes + entries_.size() * kIconEntryBytes;
    for(const auto &entry : entries_) {
        if(entry.image.size() > kMaxObjectBytes || !addSize(total, entry.image.size()) ||
           total > kMaxObjectBytes)
            return false;
    }
    if(total > std::numeric_limits<std::uint32_t>::max())
        return false;
    data.reserve(total);
    putU16(data, 0);
    putU16(data, type_ == kCursorType ? kCursorType : kIconType);
    putU16(data, static_cast<std::uint16_t>(entries_.size()));
    std::size_t imageOffset = kIconHeaderBytes + entries_.size() * kIconEntryBytes;
    for(const auto &entry : entries_) {
        data.push_back(entry.width);
        data.push_back(entry.height);
        data.push_back(entry.colorCount);
        data.push_back(entry.reserved);
        putU16(data, entry.first);
        putU16(data, entry.second);
        putU32(data, static_cast<std::uint32_t>(entry.image.size()));
        putU32(data, static_cast<std::uint32_t>(imageOffset));
        imageOffset += entry.image.size();
    }
    for(const auto &entry : entries_)
        data.insert(data.end(), entry.image.begin(), entry.image.end());
    return data.size() == total;
}

void IconImage::setCursor(bool value) {
    const std::uint16_t newType = value ? kCursorType : kIconType;
    if(type_ == newType)
        return;
    type_ = newType;
    for(auto &entry : entries_) {
        // The two WORDs have different meanings in ICO and CUR entries.  Do
        // not leak cursor hotspots into an ICO as planes/bits (or vice versa)
        // when a script toggles isCursor before serializing the object.
        entry.first = 0;
        entry.second = 0;
    }
}

bool IconImage::getID(std::size_t index, int &id) const {
    const auto *item = entry(index);
    if(!item)
        return false;
    id = item->id;
    return true;
}

bool IconImage::setID(std::size_t index, int id) {
    auto *item = entry(index);
    if(!item || id < -1 || id > std::numeric_limits<std::uint16_t>::max())
        return false;
    item->id = id;
    return true;
}

const std::vector<std::uint8_t> *IconImage::getImage(std::size_t index) const {
    const auto *item = entry(index);
    return item ? &item->image : nullptr;
}

bool IconImage::setImage(std::size_t index, const std::uint8_t *data,
                         std::size_t size) {
    auto *item = entry(index);
    if(!item || size > kMaxObjectBytes ||
       size > std::numeric_limits<std::uint32_t>::max() ||
       (size != 0 && !data))
        return false;
    if(size == 0)
        item->image.clear();
    else
        item->image.assign(data, data + size);
    item->bytesInRes = static_cast<std::uint32_t>(size);
    return true;
}

bool IconImage::getHotSpot(std::size_t index, std::uint16_t &x,
                           std::uint16_t &y) const {
    const auto *item = entry(index);
    if(!item || !isCursor())
        return false;
    x = item->first;
    y = item->second;
    return true;
}

bool IconImage::setHotSpot(std::size_t index, std::uint16_t x,
                           std::uint16_t y) {
    if(!isCursor())
        setCursor(true);
    auto *item = entry(index);
    if(!item)
        return false;
    item->first = x;
    item->second = y;
    return true;
}

void IconGroup::clear() {
    type_ = kIconType;
    entries_.clear();
}

const IconEntry *IconGroup::entry(std::size_t index) const {
    return index < entries_.size() ? &entries_[index] : nullptr;
}

bool IconGroup::load(const std::uint8_t *data, std::size_t size) {
    clear();
    if(!data || size < kIconHeaderBytes || size > kMaxObjectBytes)
        return false;
    const std::uint16_t reserved = readU16(data);
    const std::uint16_t type = readU16(data + 2);
    const std::uint16_t count = readU16(data + 4);
    if(reserved != 0 || (type != kIconType && type != kCursorType) ||
       count == 0 || count > kMaxIconImages)
        return false;
    const std::size_t tableBytes = kIconHeaderBytes +
        static_cast<std::size_t>(count) * kGroupEntryBytes;
    if(tableBytes > size)
        return false;
    type_ = type;
    entries_.reserve(count);
    for(std::size_t i = 0; i < count; ++i) {
        const std::uint8_t *p = data + kIconHeaderBytes + i * kGroupEntryBytes;
        IconEntry entry;
        entry.width = p[0];
        entry.height = p[1];
        entry.colorCount = p[2];
        entry.reserved = p[3];
        entry.first = readU16(p + 4);
        entry.second = readU16(p + 6);
        entry.bytesInRes = readU32(p + 8);
        entry.id = readU16(p + 12);
        entries_.push_back(std::move(entry));
    }
    return true;
}

bool IconGroup::save(std::vector<std::uint8_t> &data) const {
    data.clear();
    if(entries_.empty() || entries_.size() > kMaxIconImages)
        return false;
    const std::size_t total = kIconHeaderBytes + entries_.size() * kGroupEntryBytes;
    if(total > kMaxObjectBytes)
        return false;
    data.reserve(total);
    putU16(data, 0);
    putU16(data, type_ == kCursorType ? kCursorType : kIconType);
    putU16(data, static_cast<std::uint16_t>(entries_.size()));
    for(const auto &entry : entries_) {
        data.push_back(entry.width);
        data.push_back(entry.height);
        data.push_back(entry.colorCount);
        data.push_back(entry.reserved);
        putU16(data, entry.first);
        putU16(data, entry.second);
        putU32(data, entry.bytesInRes);
        putU16(data, static_cast<std::uint16_t>(entry.id < 0 ? 0xffff : entry.id));
    }
    return data.size() == total;
}

bool IconGroup::fromImage(const IconImage &image) {
    clear();
    if(image.count() == 0 || image.count() > kMaxIconImages)
        return false;
    type_ = image.type() == kCursorType ? kCursorType : kIconType;
    entries_.reserve(image.count());
    for(std::size_t i = 0; i < image.count(); ++i) {
        const auto *source = image.entry(i);
        if(!source)
            return clear(), false;
        IconEntry entry = *source;
        entry.image.clear();
        entries_.push_back(std::move(entry));
    }
    return true;
}

bool IconGroup::toImage(IconImage &image) const {
    if(entries_.empty() || entries_.size() > kMaxIconImages)
        return false;
    image.clear();
    image.type_ = type_ == kCursorType ? kCursorType : kIconType;
    image.entries_.reserve(entries_.size());
    for(const auto &source : entries_) {
        IconEntry entry = source;
        entry.image.clear();
        image.entries_.push_back(std::move(entry));
    }
    return true;
}

bool IconGroup::getID(std::size_t index, int &id) const {
    const auto *item = entry(index);
    if(!item)
        return false;
    id = item->id;
    return true;
}

bool IconGroup::setID(std::size_t index, int id) {
    if(index >= entries_.size() || id < -1 ||
       id > std::numeric_limits<std::uint16_t>::max())
        return false;
    entries_[index].id = id;
    return true;
}

// -------------------------------------------------------------------------
// VS_VERSION_INFO parser/serializer
// -------------------------------------------------------------------------

namespace {

struct VersionNode {
    std::size_t start = 0;
    std::size_t end = 0;
    std::size_t valueStart = 0;
    std::size_t valueEnd = 0;
    std::size_t childrenStart = 0;
    std::uint16_t valueLength = 0;
    std::uint16_t type = 0;
    std::u16string key;
};

bool align4(std::size_t value, std::size_t limit, std::size_t &aligned) {
    if(value > std::numeric_limits<std::size_t>::max() - 3)
        return false;
    aligned = (value + 3u) & ~std::size_t(3u);
    return aligned <= limit;
}

bool parseVersionNode(const std::uint8_t *data, std::size_t start,
                      std::size_t limit, VersionNode &node, int depth) {
    if(!data || depth > 32 || start > limit || limit - start < 6)
        return false;
    const std::uint16_t length = readU16(data + start);
    const std::uint16_t valueLength = readU16(data + start + 2);
    const std::uint16_t type = readU16(data + start + 4);
    if(length < 6 || length > limit - start)
        return false;
    node = VersionNode{};
    node.start = start;
    node.end = start + length;
    node.valueLength = valueLength;
    node.type = type;

    std::size_t keyEnd = start + 6;
    constexpr std::size_t kMaxKeyUnits = 4096;
    while(keyEnd + 2 <= node.end && keyEnd - (start + 6) <= kMaxKeyUnits * 2) {
        const std::uint16_t unit = readU16(data + keyEnd);
        keyEnd += 2;
        if(unit == 0)
            break;
        node.key.push_back(static_cast<char16_t>(unit));
    }
    if(keyEnd > node.end || node.key.size() > kMaxKeyUnits ||
       keyEnd < start + 8 || readU16(data + keyEnd - 2) != 0)
        return false;

    if(!align4(keyEnd, node.end, node.valueStart))
        return false;
    const std::size_t valueBytes = type == 1
        ? static_cast<std::size_t>(valueLength) * 2u
        : static_cast<std::size_t>(valueLength);
    if(valueBytes > node.end - node.valueStart)
        return false;
    node.valueEnd = node.valueStart + valueBytes;
    if(!align4(node.valueEnd, node.end, node.childrenStart))
        return false;
    return true;
}

bool allZero(const std::uint8_t *data, std::size_t begin, std::size_t end) {
    for(std::size_t i = begin; i < end; ++i)
        if(data[i] != 0)
            return false;
    return true;
}

template <typename Callback>
bool eachChild(const std::uint8_t *data, const VersionNode &parent,
               Callback &&callback) {
    std::size_t pos = parent.childrenStart;
    while(pos < parent.end) {
        if(parent.end - pos < 2 || allZero(data, pos, parent.end))
            return true;
        VersionNode child;
        if(!parseVersionNode(data, pos, parent.end, child, 1))
            return false;
        if(child.end <= pos || !callback(child))
            return false;
        std::size_t next = 0;
        if(!align4(child.end, parent.end, next) || next <= pos)
            return false;
        pos = next;
    }
    return pos == parent.end || allZero(data, pos, parent.end);
}

bool readVersionString(const std::uint8_t *data, const VersionNode &node,
                       std::u16string &value) {
    value.clear();
    if(node.type != 1 || node.valueStart > node.valueEnd ||
       (node.valueEnd - node.valueStart) % 2 != 0 ||
       node.valueEnd - node.valueStart > kMaxObjectBytes)
        return false;
    const std::size_t units = (node.valueEnd - node.valueStart) / 2;
    value.reserve(units);
    for(std::size_t i = 0; i < units; ++i) {
        const std::uint16_t unit = readU16(data + node.valueStart + i * 2);
        if(unit == 0)
            break;
        value.push_back(static_cast<char16_t>(unit));
    }
    return true;
}

bool parseHexLanguage(const std::u16string &key, std::uint32_t &value) {
    if(key.size() != 8)
        return false;
    value = 0;
    for(char16_t unit : key) {
        unsigned int digit = 0;
        if(unit >= u'0' && unit <= u'9')
            digit = unit - u'0';
        else if(unit >= u'a' && unit <= u'f')
            digit = unit - u'a' + 10;
        else if(unit >= u'A' && unit <= u'F')
            digit = unit - u'A' + 10;
        else
            return false;
        value = (value << 4) | digit;
    }
    return true;
}

void appendUtf16(std::vector<std::uint8_t> &out, const std::u16string &text,
                 bool terminate = true) {
    for(char16_t unit : text)
        putU16(out, static_cast<std::uint16_t>(unit));
    if(terminate)
        putU16(out, 0);
}

void pad4(std::vector<std::uint8_t> &out) {
    while((out.size() & 3u) != 0)
        out.push_back(0);
}

std::vector<std::uint8_t> makeVersionNode(
    const std::u16string &key, std::uint16_t type,
    const std::vector<std::uint8_t> &value,
    const std::vector<std::vector<std::uint8_t>> &children) {
    std::vector<std::uint8_t> out;
    out.reserve(64 + value.size());
    out.resize(6, 0);
    appendUtf16(out, key);
    pad4(out);
    out.insert(out.end(), value.begin(), value.end());
    pad4(out);
    for(const auto &child : children)
        out.insert(out.end(), child.begin(), child.end());
    pad4(out);
    if(out.size() > std::numeric_limits<std::uint16_t>::max())
        return {};
    const std::size_t valueUnits = type == 1 ? value.size() / 2u : value.size();
    if(valueUnits > std::numeric_limits<std::uint16_t>::max())
        return {};
    out[0] = static_cast<std::uint8_t>(out.size() & 0xffu);
    out[1] = static_cast<std::uint8_t>((out.size() >> 8) & 0xffu);
    out[2] = static_cast<std::uint8_t>(valueUnits & 0xffu);
    out[3] = static_cast<std::uint8_t>((valueUnits >> 8) & 0xffu);
    out[4] = static_cast<std::uint8_t>(type & 0xffu);
    out[5] = static_cast<std::uint8_t>(type >> 8);
    return out;
}

std::u16string upperVersionKey(const std::u16string &key) {
    std::u16string result = key;
    for(char16_t &unit : result)
        if(unit >= u'a' && unit <= u'z')
            unit = static_cast<char16_t>(unit - (u'a' - u'A'));
    return result;
}

} // namespace

void VersionInfo::clear() {
    fixed_ = FixedInfo{};
    strings_.clear();
    translations_.clear();
    initialized_ = false;
}

void VersionInfo::reset(std::uint32_t language) {
    clear();
    fixed_.signature = 0xFEEF04BDu;
    fixed_.fileFlagsMask = 0x3Fu;
    fixed_.fileFlags = 0x10u;
    fixed_.fileOS = 0x00040004u;
    strings_.emplace(language, StringTable{});
    translations_.push_back(language);
    initialized_ = true;
}

bool VersionInfo::load(const std::uint8_t *data, std::size_t size) {
    clear();
    if(!data || size < 6 || size > kMaxObjectBytes)
        return false;
    VersionNode root;
    if(!parseVersionNode(data, 0, size, root, 0) ||
       root.start != 0 || root.key != u"VS_VERSION_INFO" ||
       root.type != 0 || root.valueEnd - root.valueStart < 52)
        return false;
    // A valid version resource may have up to three bytes of final alignment.
    if(root.end > size || (size - root.end > 3 &&
                           !allZero(data, root.end, size)))
        return false;
    std::array<std::uint32_t *, 13> fixedFields{
        &fixed_.signature, &fixed_.structVersion, &fixed_.fileVersionMS,
        &fixed_.fileVersionLS, &fixed_.productVersionMS,
        &fixed_.productVersionLS, &fixed_.fileFlagsMask, &fixed_.fileFlags,
        &fixed_.fileOS, &fixed_.fileType, &fixed_.fileSubtype,
        &fixed_.fileDateMS, &fixed_.fileDateLS};
    for(std::size_t i = 0; i < fixedFields.size(); ++i)
        *fixedFields[i] = readU32(data + root.valueStart + i * 4);
    if(fixed_.signature != 0xFEEF04BDu)
        return false;

    bool childOk = eachChild(data, root, [&](const VersionNode &child) {
        if(child.key == u"StringFileInfo") {
            return eachChild(data, child, [&](const VersionNode &table) {
                std::uint32_t language = 0;
                if(!parseHexLanguage(table.key, language))
                    return true; // Unknown table; preserve other records.
                auto &target = strings_[language];
                return eachChild(data, table, [&](const VersionNode &string) {
                    if(string.key.empty() || string.key.size() > 1024)
                        return false;
                    std::u16string value;
                    if(!readVersionString(data, string, value) ||
                       strings_.size() > kMaxVersionStrings)
                        return false;
                    target[string.key] = std::move(value);
                    return target.size() <= kMaxVersionStrings;
                });
            });
        }
        if(child.key == u"VarFileInfo") {
            return eachChild(data, child, [&](const VersionNode &var) {
                if(var.key != u"Translation" || var.type != 0 ||
                   (var.valueEnd - var.valueStart) % 4 != 0)
                    return true;
                const std::size_t count = (var.valueEnd - var.valueStart) / 4;
                if(count > kMaxVersionStrings)
                    return false;
                for(std::size_t i = 0; i < count; ++i) {
                    const std::uint32_t language =
                        readU32(data + var.valueStart + i * 4);
                    if(std::find(translations_.begin(), translations_.end(),
                                 language) == translations_.end())
                        translations_.push_back(language);
                }
                return true;
            });
        }
        return true;
    });
    if(!childOk)
        return clear(), false;
    for(const auto &pair : strings_)
        if(std::find(translations_.begin(), translations_.end(), pair.first) ==
           translations_.end())
            translations_.push_back(pair.first);
    initialized_ = true;
    return true;
}

bool VersionInfo::save(std::vector<std::uint8_t> &data) const {
    data.clear();
    if(!initialized_ || strings_.size() > kMaxVersionStrings ||
       translations_.size() > kMaxVersionStrings)
        return false;

    std::vector<std::vector<std::uint8_t>> stringTables;
    for(const auto &language : strings_) {
        std::array<char16_t, 9> key{};
        static constexpr char16_t hex[] = u"0123456789ABCDEF";
        std::uint32_t value = language.first;
        for(int i = 7; i >= 0; --i) {
            key[static_cast<std::size_t>(i)] = hex[value & 0x0fu];
            value >>= 4;
        }
        std::vector<std::vector<std::uint8_t>> strings;
        for(const auto &pair : language.second) {
            if(pair.first.empty() || pair.first.size() > 1024 ||
               pair.second.size() > 32760)
                return false;
            std::vector<std::uint8_t> valueBytes;
            appendUtf16(valueBytes, pair.second);
            strings.push_back(makeVersionNode(pair.first, 1, valueBytes, {}));
            if(strings.back().empty())
                return false;
        }
        stringTables.push_back(makeVersionNode(
            std::u16string(key.data(), 8), 1, {}, strings));
        if(stringTables.back().empty())
            return false;
    }
    std::vector<std::vector<std::uint8_t>> rootChildren;
    if(!stringTables.empty()) {
        rootChildren.push_back(makeVersionNode(u"StringFileInfo", 1, {},
                                               stringTables));
        if(rootChildren.back().empty())
            return false;
    }
    if(!translations_.empty()) {
        std::vector<std::uint8_t> translationBytes;
        translationBytes.reserve(translations_.size() * 4u);
        for(const auto language : translations_)
            putU32(translationBytes, language);
        std::vector<std::vector<std::uint8_t>> vars;
        vars.push_back(makeVersionNode(u"Translation", 0, translationBytes, {}));
        if(vars.back().empty())
            return false;
        rootChildren.push_back(makeVersionNode(u"VarFileInfo", 1, {}, vars));
        if(rootChildren.back().empty())
            return false;
    }

    std::vector<std::uint8_t> fixedBytes;
    fixedBytes.reserve(52);
    const std::array<std::uint32_t, 13> fields{
        fixed_.signature, fixed_.structVersion, fixed_.fileVersionMS,
        fixed_.fileVersionLS, fixed_.productVersionMS,
        fixed_.productVersionLS, fixed_.fileFlagsMask, fixed_.fileFlags,
        fixed_.fileOS, fixed_.fileType, fixed_.fileSubtype, fixed_.fileDateMS,
        fixed_.fileDateLS};
    for(const auto field : fields)
        putU32(fixedBytes, field);
    data = makeVersionNode(u"VS_VERSION_INFO", 0, fixedBytes, rootChildren);
    return !data.empty() && data.size() <= kMaxObjectBytes;
}

bool VersionInfo::changeString(const std::u16string &key,
                               const std::u16string &value,
                               std::uint32_t language) {
    if(!initialized_ || key.empty() || key.size() > 1024 || value.size() > 32760)
        return false;
    auto table = strings_.find(language);
    if(table == strings_.end())
        return false;
    table->second[key] = value;
    return true;
}

bool VersionInfo::changeInfo(const std::u16string &key, std::uint64_t value) {
    if(!initialized_)
        return false;
    const std::u16string normalized = upperVersionKey(key);
    auto set32 = [value](std::uint32_t &target) {
        target = static_cast<std::uint32_t>(value);
        return true;
    };
    if(normalized == u"SIGNATURE") return set32(fixed_.signature);
    if(normalized == u"STRUCVERSION") return set32(fixed_.structVersion);
    if(normalized == u"FILEFLAGSMASK") return set32(fixed_.fileFlagsMask);
    if(normalized == u"FILEFLAGS") return set32(fixed_.fileFlags);
    if(normalized == u"FILEOS") return set32(fixed_.fileOS);
    if(normalized == u"FILETYPE") return set32(fixed_.fileType);
    if(normalized == u"FILESUBTYPE") return set32(fixed_.fileSubtype);
    if(normalized == u"FILEVERSION") {
        fixed_.fileVersionMS = static_cast<std::uint32_t>(value >> 32);
        fixed_.fileVersionLS = static_cast<std::uint32_t>(value);
        return true;
    }
    if(normalized == u"PRODUCTVERSION") {
        fixed_.productVersionMS = static_cast<std::uint32_t>(value >> 32);
        fixed_.productVersionLS = static_cast<std::uint32_t>(value);
        return true;
    }
    if(normalized == u"FILEDATE") {
        fixed_.fileDateMS = static_cast<std::uint32_t>(value >> 32);
        fixed_.fileDateLS = static_cast<std::uint32_t>(value);
        return true;
    }
    return false;
}

bool VersionInfo::addLanguage(std::uint32_t language) {
    if(!initialized_ || strings_.count(language) != 0 ||
       std::find(translations_.begin(), translations_.end(), language) !=
           translations_.end() ||
       strings_.size() >= kMaxVersionStrings ||
       translations_.size() >= kMaxVersionStrings)
        return false;
    strings_.emplace(language, StringTable{});
    translations_.push_back(language);
    // Match krkrz's VersionInfo policy: a newly inferred translation marks
    // the fixed file info as dynamically generated.  This bit is harmless
    // for portable sidecars and lets a subsequent native conversion retain
    // the same metadata signal.
    fixed_.fileFlagsMask |= 0x10u;
    fixed_.fileFlags |= 0x10u;
    return true;
}

bool VersionInfo::removeLanguage(std::uint32_t language) {
    if(!initialized_)
        return false;
    const auto stringIt = strings_.find(language);
    const auto oldSize = translations_.size();
    const bool translationPresent =
        std::find(translations_.begin(), translations_.end(), language) !=
        translations_.end();
    // A malformed/hand-authored version resource can contain only one side of
    // the StringFileInfo/Translation pair.  Remove whichever side exists,
    // but report false when neither side exists; this keeps the operation
    // useful for portable resources without pretending an absent language was
    // deleted.
    if(stringIt == strings_.end() && !translationPresent)
        return false;
    const bool stringsPresent = stringIt != strings_.end();
    if(stringsPresent)
        strings_.erase(stringIt);
    translations_.erase(std::remove(translations_.begin(), translations_.end(),
                                    language), translations_.end());
    return stringsPresent || translations_.size() != oldSize;
}

bool VersionInfo::copyLanguage(std::uint32_t source,
                               std::uint32_t destination) {
    if(!initialized_ || source == destination ||
       strings_.count(destination) != 0 ||
       std::find(translations_.begin(), translations_.end(), destination) !=
           translations_.end() ||
       strings_.size() >= kMaxVersionStrings ||
       translations_.size() >= kMaxVersionStrings)
        return false;
    const auto sourceTable = strings_.find(source);
    const bool sourceTranslation =
        std::find(translations_.begin(), translations_.end(), source) !=
        translations_.end();
    if(sourceTable == strings_.end() && !sourceTranslation)
        return false;
    // Normally copy the complete StringFileInfo table.  If an input resource
    // contains a translation entry without a string table, retain that
    // language's shape by creating an empty destination table; scripts can
    // then populate it with changeString just like an added language.
    if(sourceTable != strings_.end())
        strings_.emplace(destination, sourceTable->second);
    else
        strings_.emplace(destination, StringTable{});
    translations_.push_back(destination);
    fixed_.fileFlagsMask |= 0x10u;
    fixed_.fileFlags |= 0x10u;
    return true;
}

std::vector<std::uint32_t> VersionInfo::languages() const {
    std::vector<std::uint32_t> result = translations_;
    for(const auto &pair : strings_)
        if(std::find(result.begin(), result.end(), pair.first) == result.end())
            result.push_back(pair.first);
    return result;
}

// -------------------------------------------------------------------------
// TJS bindings.  The names and signatures intentionally mirror krkrz's
// ResourceRW.hpp so existing scripts can use the portable codecs unchanged.
// -------------------------------------------------------------------------

class PortableResourceIconImage {
public:
    static tjs_error TJS_INTF_METHOD factory(
        PortableResourceIconImage **result, tjs_int count, tTJSVariant **params,
        iTJSDispatch2 *) {
        if(!result)
            return TJS_E_FAIL;
        auto *instance = new (std::nothrow) PortableResourceIconImage();
        if(!instance)
            return TJS_E_FAIL;
        if(count > 0 && params && params[0] &&
           params[0]->Type() == tvtOctet) {
            const std::uint8_t *data = nullptr;
            std::size_t size = 0;
            if(!validOctet(*params[0], data, size) ||
               !instance->image_.load(data, size)) {
                delete instance;
                return TJS_E_INVALIDPARAM;
            }
        }
        *result = instance;
        return TJS_S_OK;
    }

    tjs_error fromOctet(tTJSVariant *result, tTJSVariant *value) {
        const std::uint8_t *data = nullptr;
        std::size_t size = 0;
        if(!value || !validOctet(*value, data, size))
            return TJS_E_INVALIDPARAM;
        const bool ok = image_.load(data, size);
        if(result)
            *result = ok ? static_cast<tjs_int>(image_.count()) : 0;
        return TJS_S_OK;
    }

    tjs_error toOctet(tTJSVariant *result) {
        if(!result)
            return TJS_S_OK;
        std::vector<std::uint8_t> data;
        if(!image_.save(data)) {
            result->Clear();
            return TJS_S_OK;
        }
        setOctet(result, data);
        return TJS_S_OK;
    }

    tjs_error setID(tTJSVariant *result, tTJSVariant *index,
                    tTJSVariant *id) {
        if(!index || !id)
            return TJS_E_INVALIDPARAM;
        const bool ok = image_.setID(static_cast<std::size_t>(
                                         static_cast<tjs_int64>(*index)),
                                     static_cast<tjs_int>(*id));
        if(result) *result = ok;
        return TJS_S_OK;
    }

    tjs_error getID(tTJSVariant *result, tTJSVariant *index) {
        if(!index)
            return TJS_E_INVALIDPARAM;
        int id = -1;
        image_.getID(static_cast<std::size_t>(static_cast<tjs_int64>(*index)), id);
        if(result) *result = id;
        return TJS_S_OK;
    }

    tjs_error setImage(tTJSVariant *result, tTJSVariant *index,
                       tTJSVariant *value) {
        if(!index || !value)
            return TJS_E_INVALIDPARAM;
        const std::uint8_t *data = nullptr;
        std::size_t size = 0;
        if(!validOctet(*value, data, size))
            return TJS_E_INVALIDPARAM;
        const bool ok = image_.setImage(
            static_cast<std::size_t>(static_cast<tjs_int64>(*index)), data, size);
        if(result) *result = ok;
        return TJS_S_OK;
    }

    tjs_error getImage(tTJSVariant *result, tTJSVariant *index) {
        if(!result || !index)
            return index ? TJS_S_OK : TJS_E_INVALIDPARAM;
        const auto *image = image_.getImage(
            static_cast<std::size_t>(static_cast<tjs_int64>(*index)));
        if(!image) {
            result->Clear();
            return TJS_S_OK;
        }
        setOctet(result, *image);
        return TJS_S_OK;
    }

    tjs_error setHotSpot(tTJSVariant *result, tTJSVariant *index,
                         tTJSVariant *x, tTJSVariant *y) {
        if(!index || !x || !y)
            return TJS_E_INVALIDPARAM;
        const bool ok = image_.setHotSpot(
            static_cast<std::size_t>(static_cast<tjs_int64>(*index)),
            static_cast<std::uint16_t>(static_cast<tjs_int>(*x)),
            static_cast<std::uint16_t>(static_cast<tjs_int>(*y)));
        if(result) *result = ok;
        return TJS_S_OK;
    }

    tjs_error getHotSpot(tTJSVariant *result, tTJSVariant *index) {
        if(!index)
            return TJS_E_INVALIDPARAM;
        std::uint16_t x = 0, y = 0;
        if(result) {
            if(!image_.getHotSpot(
                   static_cast<std::size_t>(static_cast<tjs_int64>(*index)), x, y))
                result->Clear();
            else {
                iTJSDispatch2 *array = TJSCreateArrayObject();
                if(!array)
                    return TJS_E_FAIL;
                tTJSVariant vx(static_cast<tjs_int>(x));
                tTJSVariant vy(static_cast<tjs_int>(y));
                array->PropSetByNum(TJS_MEMBERENSURE, 0, &vx, array);
                array->PropSetByNum(TJS_MEMBERENSURE, 1, &vy, array);
                *result = tTJSVariant(array, array);
                array->Release();
            }
        }
        return TJS_S_OK;
    }

    tjs_int getCount() const { return static_cast<tjs_int>(image_.count()); }
    bool getIsCursor() const { return image_.isCursor(); }
    void setIsCursor(bool value) { image_.setCursor(value); }

    const IconImage &image() const { return image_; }
    IconImage &image() { return image_; }

    static tjs_error TJS_INTF_METHOD fromOctetCb(
        tTJSVariant *result, tjs_int count, tTJSVariant **params,
        PortableResourceIconImage *self) {
        if(!self || count < 1 || !params || !params[0])
            return TJS_E_BADPARAMCOUNT;
        return self->fromOctet(result, params[0]);
    }
    static tjs_error TJS_INTF_METHOD toOctetCb(
        tTJSVariant *result, tjs_int, tTJSVariant **,
        PortableResourceIconImage *self) {
        return self ? self->toOctet(result) : TJS_E_NATIVECLASSCRASH;
    }
    static tjs_error TJS_INTF_METHOD setIDCb(
        tTJSVariant *result, tjs_int count, tTJSVariant **params,
        PortableResourceIconImage *self) {
        if(!self || count < 2 || !params || !params[0] || !params[1])
            return TJS_E_BADPARAMCOUNT;
        return self->setID(result, params[0], params[1]);
    }
    static tjs_error TJS_INTF_METHOD getIDCb(
        tTJSVariant *result, tjs_int count, tTJSVariant **params,
        PortableResourceIconImage *self) {
        if(!self || count < 1 || !params || !params[0])
            return TJS_E_BADPARAMCOUNT;
        return self->getID(result, params[0]);
    }
    static tjs_error TJS_INTF_METHOD setImageCb(
        tTJSVariant *result, tjs_int count, tTJSVariant **params,
        PortableResourceIconImage *self) {
        if(!self || count < 2 || !params || !params[0] || !params[1])
            return TJS_E_BADPARAMCOUNT;
        return self->setImage(result, params[0], params[1]);
    }
    static tjs_error TJS_INTF_METHOD getImageCb(
        tTJSVariant *result, tjs_int count, tTJSVariant **params,
        PortableResourceIconImage *self) {
        if(!self || count < 1 || !params || !params[0])
            return TJS_E_BADPARAMCOUNT;
        return self->getImage(result, params[0]);
    }
    static tjs_error TJS_INTF_METHOD setHotSpotCb(
        tTJSVariant *result, tjs_int count, tTJSVariant **params,
        PortableResourceIconImage *self) {
        if(!self || count < 3 || !params || !params[0] || !params[1] ||
           !params[2])
            return TJS_E_BADPARAMCOUNT;
        return self->setHotSpot(result, params[0], params[1], params[2]);
    }
    static tjs_error TJS_INTF_METHOD getHotSpotCb(
        tTJSVariant *result, tjs_int count, tTJSVariant **params,
        PortableResourceIconImage *self) {
        if(!self || count < 1 || !params || !params[0])
            return TJS_E_BADPARAMCOUNT;
        return self->getHotSpot(result, params[0]);
    }

private:
    IconImage image_;
};

class PortableResourceIconGroup {
public:
    static tjs_error TJS_INTF_METHOD factory(
        PortableResourceIconGroup **result, tjs_int count, tTJSVariant **params,
        iTJSDispatch2 *) {
        if(!result)
            return TJS_E_FAIL;
        auto *instance = new (std::nothrow) PortableResourceIconGroup();
        if(!instance)
            return TJS_E_FAIL;
        if(count > 0 && params && params[0]) {
            tjs_error error = TJS_S_OK;
            if(params[0]->Type() == tvtOctet)
                error = instance->fromOctet(nullptr, params[0]);
            else if(params[0]->Type() == tvtObject)
                error = instance->fromIcon(nullptr, params[0]);
            else
                error = TJS_E_INVALIDPARAM;
            if(TJS_FAILED(error)) {
                delete instance;
                return error;
            }
        }
        *result = instance;
        return TJS_S_OK;
    }

    tjs_error fromIcon(tTJSVariant *result, tTJSVariant *value) {
        if(!value || value->Type() != tvtObject || !value->AsObjectNoAddRef())
            return TJS_E_INVALIDPARAM;
        auto *icon = SimpleBinder::BindUtil::GetInstance(
            value->AsObjectNoAddRef(), static_cast<PortableResourceIconImage *>(nullptr));
        if(!icon || !group_.fromImage(icon->image()))
            return TJS_E_INVALIDPARAM;
        if(result) *result = static_cast<tjs_int>(group_.count());
        return TJS_S_OK;
    }

    tjs_error toIcon(tTJSVariant *result) {
        if(!result)
            return TJS_S_OK;
        iTJSDispatch2 *classObject =
            SimpleBinder::BindUtil::GetObject(TJS_W("ResourceIconImage"));
        if(!classObject)
            return TJS_E_NATIVECLASSCRASH;
        iTJSDispatch2 *object = nullptr;
        const tjs_error error = classObject->CreateNew(
            0, nullptr, nullptr, &object, 0, nullptr, classObject);
        if(TJS_FAILED(error) || !object)
            return TJS_FAILED(error) ? error : TJS_E_NATIVECLASSCRASH;
        auto *icon = SimpleBinder::BindUtil::GetInstance(
            object, static_cast<PortableResourceIconImage *>(nullptr));
        if(!icon || !group_.toImage(icon->image())) {
            object->Release();
            return TJS_E_NATIVECLASSCRASH;
        }
        *result = tTJSVariant(object, object);
        object->Release();
        return TJS_S_OK;
    }

    tjs_error fromOctet(tTJSVariant *result, tTJSVariant *value) {
        const std::uint8_t *data = nullptr;
        std::size_t size = 0;
        if(!value || !validOctet(*value, data, size))
            return TJS_E_INVALIDPARAM;
        const bool ok = group_.load(data, size);
        if(result) *result = ok ? static_cast<tjs_int>(group_.count()) : 0;
        return TJS_S_OK;
    }

    tjs_error toOctet(tTJSVariant *result) {
        if(!result)
            return TJS_S_OK;
        std::vector<std::uint8_t> data;
        if(!group_.save(data)) {
            result->Clear();
            return TJS_S_OK;
        }
        setOctet(result, data);
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD fromIconCb(
        tTJSVariant *result, tjs_int count, tTJSVariant **params,
        PortableResourceIconGroup *self) {
        if(!self || count < 1 || !params || !params[0])
            return TJS_E_BADPARAMCOUNT;
        return self->fromIcon(result, params[0]);
    }
    static tjs_error TJS_INTF_METHOD toIconCb(
        tTJSVariant *result, tjs_int, tTJSVariant **,
        PortableResourceIconGroup *self) {
        return self ? self->toIcon(result) : TJS_E_NATIVECLASSCRASH;
    }
    static tjs_error TJS_INTF_METHOD fromOctetCb(
        tTJSVariant *result, tjs_int count, tTJSVariant **params,
        PortableResourceIconGroup *self) {
        if(!self || count < 1 || !params || !params[0])
            return TJS_E_BADPARAMCOUNT;
        return self->fromOctet(result, params[0]);
    }
    static tjs_error TJS_INTF_METHOD toOctetCb(
        tTJSVariant *result, tjs_int, tTJSVariant **,
        PortableResourceIconGroup *self) {
        return self ? self->toOctet(result) : TJS_E_NATIVECLASSCRASH;
    }

private:
    IconGroup group_;
};

class PortableResourceVersionInfo {
public:
    static tjs_error TJS_INTF_METHOD factory(
        PortableResourceVersionInfo **result, tjs_int count,
        tTJSVariant **params, iTJSDispatch2 *) {
        if(!result)
            return TJS_E_FAIL;
        auto *instance = new (std::nothrow) PortableResourceVersionInfo();
        if(!instance)
            return TJS_E_FAIL;
        if(count > 0 && params && params[0] &&
           params[0]->Type() == tvtInteger)
            instance->info_.reset(static_cast<std::uint32_t>(
                static_cast<tjs_int64>(*params[0])));
        *result = instance;
        return TJS_S_OK;
    }

    tjs_error changeString(tTJSVariant *result, tTJSVariant *key,
                           tTJSVariant *value, tTJSVariant *language) {
        if(!key || !value || !language || key->Type() != tvtString ||
           value->Type() != tvtString || language->Type() != tvtInteger)
            return TJS_E_INVALIDPARAM;
        const bool ok = info_.changeString(
            ttstr(*key).AsUtf16String(), ttstr(*value).AsUtf16String(),
            static_cast<std::uint32_t>(static_cast<tjs_int64>(*language)));
        if(result) *result = ok;
        return TJS_S_OK;
    }

    tjs_error changeInfo(tTJSVariant *result, tTJSVariant *key,
                         tTJSVariant *value) {
        if(!key || !value || key->Type() != tvtString ||
           value->Type() != tvtInteger)
            return TJS_E_INVALIDPARAM;
        const bool ok = info_.changeInfo(
            ttstr(*key).AsUtf16String(),
            static_cast<std::uint64_t>(static_cast<tjs_int64>(*value)));
        if(result) *result = ok;
        return TJS_S_OK;
    }

    tTJSVariant getLangList() const { return makeIntegerArray(info_.languages()); }

    tjs_error addLang(tTJSVariant *result, tTJSVariant *language) {
        if(!language || language->Type() != tvtInteger)
            return TJS_E_INVALIDPARAM;
        const bool ok = info_.addLanguage(static_cast<std::uint32_t>(
            static_cast<tjs_int64>(*language)));
        if(result) *result = ok;
        return TJS_S_OK;
    }

    tjs_error removeLang(tTJSVariant *result, tTJSVariant *language) {
        if(!language || language->Type() != tvtInteger)
            return TJS_E_INVALIDPARAM;
        const bool ok = info_.removeLanguage(static_cast<std::uint32_t>(
            static_cast<tjs_int64>(*language)));
        if(result) *result = ok;
        return TJS_S_OK;
    }

    tjs_error copyLang(tTJSVariant *result, tTJSVariant *source,
                       tTJSVariant *destination) {
        if(!source || !destination || source->Type() != tvtInteger ||
           destination->Type() != tvtInteger)
            return TJS_E_INVALIDPARAM;
        const bool ok = info_.copyLanguage(
            static_cast<std::uint32_t>(static_cast<tjs_int64>(*source)),
            static_cast<std::uint32_t>(static_cast<tjs_int64>(*destination)));
        if(result) *result = ok;
        return TJS_S_OK;
    }

    tjs_error fromOctet(tTJSVariant *result, tTJSVariant *value) {
        const std::uint8_t *data = nullptr;
        std::size_t size = 0;
        if(!value || !validOctet(*value, data, size))
            return TJS_E_INVALIDPARAM;
        const bool ok = info_.load(data, size);
        if(result) *result = ok;
        return TJS_S_OK;
    }

    tjs_error toOctet(tTJSVariant *result) {
        if(!result)
            return TJS_S_OK;
        std::vector<std::uint8_t> data;
        if(!info_.save(data)) {
            result->Clear();
            return TJS_S_OK;
        }
        setOctet(result, data);
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD changeStringCb(
        tTJSVariant *result, tjs_int count, tTJSVariant **params,
        PortableResourceVersionInfo *self) {
        if(!self || count < 3 || !params || !params[0] || !params[1] ||
           !params[2])
            return TJS_E_BADPARAMCOUNT;
        return self->changeString(result, params[0], params[1], params[2]);
    }
    static tjs_error TJS_INTF_METHOD changeInfoCb(
        tTJSVariant *result, tjs_int count, tTJSVariant **params,
        PortableResourceVersionInfo *self) {
        if(!self || count < 2 || !params || !params[0] || !params[1])
            return TJS_E_BADPARAMCOUNT;
        return self->changeInfo(result, params[0], params[1]);
    }
    static tjs_error TJS_INTF_METHOD addLangCb(
        tTJSVariant *result, tjs_int count, tTJSVariant **params,
        PortableResourceVersionInfo *self) {
        if(!self || count < 1 || !params || !params[0])
            return TJS_E_BADPARAMCOUNT;
        return self->addLang(result, params[0]);
    }
    static tjs_error TJS_INTF_METHOD removeLangCb(
        tTJSVariant *result, tjs_int count, tTJSVariant **params,
        PortableResourceVersionInfo *self) {
        if(!self || count < 1 || !params || !params[0])
            return TJS_E_BADPARAMCOUNT;
        return self->removeLang(result, params[0]);
    }
    static tjs_error TJS_INTF_METHOD copyLangCb(
        tTJSVariant *result, tjs_int count, tTJSVariant **params,
        PortableResourceVersionInfo *self) {
        if(!self || count < 2 || !params || !params[0] || !params[1])
            return TJS_E_BADPARAMCOUNT;
        return self->copyLang(result, params[0], params[1]);
    }
    static tjs_error TJS_INTF_METHOD fromOctetCb(
        tTJSVariant *result, tjs_int count, tTJSVariant **params,
        PortableResourceVersionInfo *self) {
        if(!self || count < 1 || !params || !params[0])
            return TJS_E_BADPARAMCOUNT;
        return self->fromOctet(result, params[0]);
    }
    static tjs_error TJS_INTF_METHOD toOctetCb(
        tTJSVariant *result, tjs_int, tTJSVariant **,
        PortableResourceVersionInfo *self) {
        return self ? self->toOctet(result) : TJS_E_NATIVECLASSCRASH;
    }

private:
    VersionInfo info_;
};

} // namespace AetherKiri::ResourceObjects

using AetherKiri::ResourceObjects::PortableResourceIconGroup;
using AetherKiri::ResourceObjects::PortableResourceIconImage;
using AetherKiri::ResourceObjects::PortableResourceVersionInfo;

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("resourceRW.dll")

NCB_REGISTER_CLASS_DIFFER(ResourceIconImage, PortableResourceIconImage) {
    Factory(&PortableResourceIconImage::factory);
    NCB_METHOD_RAW_CALLBACK(fromOctet, &PortableResourceIconImage::fromOctetCb,
                            0);
    NCB_METHOD_RAW_CALLBACK(toOctet, &PortableResourceIconImage::toOctetCb, 0);
    NCB_METHOD_RAW_CALLBACK(setID, &PortableResourceIconImage::setIDCb, 0);
    NCB_METHOD_RAW_CALLBACK(getID, &PortableResourceIconImage::getIDCb, 0);
    NCB_METHOD_RAW_CALLBACK(setImage, &PortableResourceIconImage::setImageCb, 0);
    NCB_METHOD_RAW_CALLBACK(getImage, &PortableResourceIconImage::getImageCb, 0);
    NCB_METHOD_RAW_CALLBACK(setHotSpot,
                            &PortableResourceIconImage::setHotSpotCb, 0);
    NCB_METHOD_RAW_CALLBACK(getHotSpot,
                            &PortableResourceIconImage::getHotSpotCb, 0);
    NCB_PROPERTY_RO(count, getCount);
    NCB_PROPERTY(isCursor, getIsCursor, setIsCursor);
}

NCB_REGISTER_CLASS_DIFFER(ResourceIconGroup, PortableResourceIconGroup) {
    Factory(&PortableResourceIconGroup::factory);
    NCB_METHOD_RAW_CALLBACK(fromIcon, &PortableResourceIconGroup::fromIconCb, 0);
    NCB_METHOD_RAW_CALLBACK(toIcon, &PortableResourceIconGroup::toIconCb, 0);
    NCB_METHOD_RAW_CALLBACK(fromOctet, &PortableResourceIconGroup::fromOctetCb,
                            0);
    NCB_METHOD_RAW_CALLBACK(toOctet, &PortableResourceIconGroup::toOctetCb, 0);
}

NCB_REGISTER_CLASS_DIFFER(ResourceVersionInfo, PortableResourceVersionInfo) {
    Factory(&PortableResourceVersionInfo::factory);
    NCB_METHOD_RAW_CALLBACK(changeString,
                            &PortableResourceVersionInfo::changeStringCb, 0);
    NCB_METHOD_RAW_CALLBACK(changeInfo,
                            &PortableResourceVersionInfo::changeInfoCb, 0);
    NCB_METHOD(getLangList);
    NCB_METHOD_RAW_CALLBACK(addLang, &PortableResourceVersionInfo::addLangCb, 0);
    NCB_METHOD_RAW_CALLBACK(removeLang,
                            &PortableResourceVersionInfo::removeLangCb, 0);
    NCB_METHOD_RAW_CALLBACK(copyLang,
                            &PortableResourceVersionInfo::copyLangCb, 0);
    NCB_METHOD_RAW_CALLBACK(fromOctet,
                            &PortableResourceVersionInfo::fromOctetCb, 0);
    NCB_METHOD_RAW_CALLBACK(toOctet,
                            &PortableResourceVersionInfo::toOctetCb, 0);
}
