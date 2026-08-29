#include "portableResourceBundle.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <set>

namespace AetherKiri::ResourceBundle {
namespace {

constexpr char kMagic[] = "AKRRES01";
constexpr std::size_t kHeaderBytes = 16; // magic, version, entry count
constexpr std::size_t kEntryHeaderBytes = 20; // type/name/lang/length

void setError(std::string *error, const char *message) {
    if(error)
        *error = message ? message : "resource bundle error";
}

bool addSize(std::size_t &value, const std::uint64_t delta) {
    if(delta > std::numeric_limits<std::size_t>::max() - value)
        return false;
    value += static_cast<std::size_t>(delta);
    return true;
}

void putU32(std::vector<std::uint8_t> &out, const std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffu));
}

void putU64(std::vector<std::uint8_t> &out, const std::uint64_t value) {
    for(unsigned int shift = 0; shift < 64; shift += 8)
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
}

bool readU32(const std::vector<std::uint8_t> &in, std::size_t &pos,
            std::uint32_t &value) {
    if(pos > in.size() || in.size() - pos < 4)
        return false;
    value = static_cast<std::uint32_t>(in[pos]) |
        (static_cast<std::uint32_t>(in[pos + 1]) << 8) |
        (static_cast<std::uint32_t>(in[pos + 2]) << 16) |
        (static_cast<std::uint32_t>(in[pos + 3]) << 24);
    pos += 4;
    return true;
}

bool readU64(const std::vector<std::uint8_t> &in, std::size_t &pos,
            std::uint64_t &value) {
    if(pos > in.size() || in.size() - pos < 8)
        return false;
    value = 0;
    for(unsigned int shift = 0; shift < 64; shift += 8)
        value |= static_cast<std::uint64_t>(in[pos++]) << shift;
    return true;
}

bool validKey(const std::string &key) {
    if(key.empty() || key.size() > kMaxNameBytes ||
       (key.front() != '@' && key.front() != '='))
        return false;
    // The container is length-delimited, but embedded NULs are rejected so a
    // C/TJS consumer cannot observe two identities for one persisted entry.
    return std::find(key.begin(), key.end(), '\0') == key.end();
}

} // namespace

bool Decode(const std::vector<std::uint8_t> &encoded,
            std::vector<Entry> &entries, std::string *error) {
    entries.clear();
    if(encoded.size() < kHeaderBytes || encoded.size() > kMaxContainerBytes) {
        setError(error, "resource bundle size is invalid");
        return false;
    }
    if(std::memcmp(encoded.data(), kMagic, sizeof(kMagic) - 1) != 0) {
        setError(error, "resource bundle magic is invalid");
        return false;
    }

    std::size_t pos = sizeof(kMagic) - 1;
    std::uint32_t version = 0;
    std::uint32_t count = 0;
    if(!readU32(encoded, pos, version) || !readU32(encoded, pos, count) ||
       version != kVersion || count > kMaxEntries) {
        setError(error, "resource bundle header is invalid");
        return false;
    }

    std::set<std::string> unique;
    std::size_t payloadTotal = 0;
    entries.reserve(count);
    for(std::uint32_t index = 0; index < count; ++index) {
        std::uint32_t typeLength = 0;
        std::uint32_t nameLength = 0;
        std::uint32_t language = 0;
        std::uint64_t dataLength = 0;
        if(!readU32(encoded, pos, typeLength) ||
           !readU32(encoded, pos, nameLength) ||
           !readU32(encoded, pos, language) ||
           !readU64(encoded, pos, dataLength) ||
           typeLength == 0 || typeLength > kMaxNameBytes ||
           nameLength == 0 || nameLength > kMaxNameBytes ||
           dataLength > kMaxPayloadBytes) {
            entries.clear();
            setError(error, "resource bundle entry header is invalid");
            return false;
        }
        const std::uint64_t namesLength =
            static_cast<std::uint64_t>(typeLength) + nameLength;
        if(namesLength > encoded.size() - pos ||
           dataLength > encoded.size() - pos - namesLength) {
            entries.clear();
            setError(error, "resource bundle entry is truncated");
            return false;
        }

        Entry entry;
        entry.type.assign(reinterpret_cast<const char *>(encoded.data() + pos),
                          typeLength);
        pos += typeLength;
        entry.name.assign(reinterpret_cast<const char *>(encoded.data() + pos),
                          nameLength);
        pos += nameLength;
        if(!validKey(entry.type) || !validKey(entry.name)) {
            entries.clear();
            setError(error, "resource bundle key is invalid");
            return false;
        }
        entry.language = language;
        entry.bytes.assign(encoded.begin() + static_cast<std::ptrdiff_t>(pos),
                           encoded.begin() + static_cast<std::ptrdiff_t>(
                               pos + static_cast<std::size_t>(dataLength)));
        pos += static_cast<std::size_t>(dataLength);
        if(!addSize(payloadTotal, dataLength) ||
           payloadTotal > kMaxContainerBytes) {
            entries.clear();
            setError(error, "resource bundle payload is too large");
            return false;
        }
        const std::string identity = entry.type + '\0' + entry.name + '\0' +
            std::to_string(entry.language);
        if(!unique.insert(identity).second) {
            entries.clear();
            setError(error, "resource bundle contains duplicate entries");
            return false;
        }
        entries.push_back(std::move(entry));
    }
    if(pos != encoded.size()) {
        entries.clear();
        setError(error, "resource bundle has trailing bytes");
        return false;
    }
    return true;
}

bool Encode(const std::vector<Entry> &entries,
            std::vector<std::uint8_t> &encoded, std::string *error) {
    encoded.clear();
    if(entries.size() > kMaxEntries) {
        setError(error, "too many resource bundle entries");
        return false;
    }

    std::size_t total = kHeaderBytes;
    std::size_t payloadTotal = 0;
    std::set<std::string> unique;
    for(const auto &entry : entries) {
        if(!validKey(entry.type) || !validKey(entry.name) ||
           entry.bytes.size() > kMaxPayloadBytes) {
            setError(error, "resource bundle entry is invalid");
            return false;
        }
        const std::string identity = entry.type + '\0' + entry.name + '\0' +
            std::to_string(entry.language);
        if(!unique.insert(identity).second) {
            setError(error, "resource bundle contains duplicate entries");
            return false;
        }
        if(!addSize(total, kEntryHeaderBytes) ||
           !addSize(total, entry.type.size()) ||
           !addSize(total, entry.name.size()) ||
           !addSize(total, entry.bytes.size()) ||
           !addSize(payloadTotal, entry.bytes.size()) ||
           total > kMaxContainerBytes || payloadTotal > kMaxContainerBytes) {
            setError(error, "resource bundle is too large");
            return false;
        }
    }

    encoded.reserve(total);
    encoded.insert(encoded.end(), kMagic, kMagic + sizeof(kMagic) - 1);
    putU32(encoded, kVersion);
    putU32(encoded, static_cast<std::uint32_t>(entries.size()));
    for(const auto &entry : entries) {
        putU32(encoded, static_cast<std::uint32_t>(entry.type.size()));
        putU32(encoded, static_cast<std::uint32_t>(entry.name.size()));
        putU32(encoded, entry.language);
        putU64(encoded, static_cast<std::uint64_t>(entry.bytes.size()));
        encoded.insert(encoded.end(), entry.type.begin(), entry.type.end());
        encoded.insert(encoded.end(), entry.name.begin(), entry.name.end());
        encoded.insert(encoded.end(), entry.bytes.begin(), entry.bytes.end());
    }
    return encoded.size() == total;
}

} // namespace AetherKiri::ResourceBundle
