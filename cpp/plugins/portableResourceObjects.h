#pragma once

// Platform-neutral counterparts of the object helpers exposed by krkrz's
// resourceRW plug-in.  The Win32 plug-in stores these objects in PE resource
// records; Aether stores the same byte-level formats in the portable
// resourceRW sidecar.  Keeping the codecs independent of TJS makes them easy
// to fuzz and prevents a malformed icon/version blob from crossing the script
// ABI unchecked.

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace AetherKiri::ResourceObjects {

constexpr std::size_t kMaxObjectBytes = 64u * 1024u * 1024u;
constexpr std::size_t kMaxIconImages = 256;
constexpr std::size_t kMaxVersionStrings = 4096;

struct IconEntry {
    std::uint8_t width = 0;
    std::uint8_t height = 0;
    std::uint8_t colorCount = 0;
    std::uint8_t reserved = 0;
    // For an ICO these are planes/bits; for a CUR they are hotspot x/y.
    std::uint16_t first = 0;
    std::uint16_t second = 0;
    std::uint32_t bytesInRes = 0;
    std::uint32_t imageOffset = 0;
    int id = -1;
    std::vector<std::uint8_t> image;
};

class IconImage {
public:
    void clear();
    bool load(const std::uint8_t *data, std::size_t size);
    bool save(std::vector<std::uint8_t> &data) const;

    std::size_t count() const { return entries_.size(); }
    bool isCursor() const { return type_ == 2; }
    void setCursor(bool value);

    bool getID(std::size_t index, int &id) const;
    bool setID(std::size_t index, int id);
    const std::vector<std::uint8_t> *getImage(std::size_t index) const;
    bool setImage(std::size_t index, const std::uint8_t *data,
                  std::size_t size);
    bool getHotSpot(std::size_t index, std::uint16_t &x,
                    std::uint16_t &y) const;
    bool setHotSpot(std::size_t index, std::uint16_t x, std::uint16_t y);

    const IconEntry *entry(std::size_t index) const;
    IconEntry *entry(std::size_t index);
    std::uint16_t type() const { return type_; }

private:
    friend class IconGroup;
    std::uint16_t type_ = 1;
    std::vector<IconEntry> entries_;
};

class IconGroup {
public:
    void clear();
    bool load(const std::uint8_t *data, std::size_t size);
    bool save(std::vector<std::uint8_t> &data) const;
    bool fromImage(const IconImage &image);
    bool toImage(IconImage &image) const;

    std::size_t count() const { return entries_.size(); }
    bool getID(std::size_t index, int &id) const;
    bool setID(std::size_t index, int id);
    const IconEntry *entry(std::size_t index) const;

private:
    std::uint16_t type_ = 1;
    std::vector<IconEntry> entries_;
};

// A bounded, standards-compatible VS_VERSION_INFO representation.  It
// intentionally keeps only the fields that ResourceVersionInfo can mutate;
// unknown records are ignored on load and regenerated in canonical order.
class VersionInfo {
public:
    void clear();
    void reset(std::uint32_t language = 0x041104b0u);
    bool load(const std::uint8_t *data, std::size_t size);
    bool save(std::vector<std::uint8_t> &data) const;

    bool changeString(const std::u16string &key, const std::u16string &value,
                      std::uint32_t language);
    bool changeInfo(const std::u16string &key, std::uint64_t value);
    bool addLanguage(std::uint32_t language);
    bool removeLanguage(std::uint32_t language);
    bool copyLanguage(std::uint32_t source, std::uint32_t destination);
    std::vector<std::uint32_t> languages() const;

private:
    struct FixedInfo {
        std::uint32_t signature = 0;
        std::uint32_t structVersion = 0;
        std::uint32_t fileVersionMS = 0;
        std::uint32_t fileVersionLS = 0;
        std::uint32_t productVersionMS = 0;
        std::uint32_t productVersionLS = 0;
        std::uint32_t fileFlagsMask = 0;
        std::uint32_t fileFlags = 0;
        std::uint32_t fileOS = 0;
        std::uint32_t fileType = 0;
        std::uint32_t fileSubtype = 0;
        std::uint32_t fileDateMS = 0;
        std::uint32_t fileDateLS = 0;
    } fixed_;

    using StringTable = std::map<std::u16string, std::u16string>;
    std::map<std::uint32_t, StringTable> strings_;
    std::vector<std::uint32_t> translations_;
    bool initialized_ = false;
};

} // namespace AetherKiri::ResourceObjects
