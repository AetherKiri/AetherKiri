#pragma once

// A small, platform-neutral resource container used by resourceRW.dll when
// the host cannot edit PE resources (macOS, Linux, Android and Web).  The
// container deliberately has no TJS or Storage dependency so it can be
// fuzzed and contract-tested independently of the plugin registry.

#include <cstdint>
#include <string>
#include <vector>

namespace AetherKiri::ResourceBundle {

struct Entry {
    // Type and name are canonical keys.  The adapter prefixes integer values
    // with '@' and string values with '='; keeping the distinction here avoids
    // collisions such as resource 10 and resource "10".
    std::string type;
    std::string name;
    std::uint32_t language = 0;
    std::vector<std::uint8_t> bytes;
};

constexpr std::uint32_t kVersion = 1;
constexpr std::size_t kMaxEntries = 4096;
constexpr std::size_t kMaxNameBytes = 1u * 1024u * 1024u;
constexpr std::size_t kMaxPayloadBytes = 128u * 1024u * 1024u;
constexpr std::size_t kMaxContainerBytes = 256u * 1024u * 1024u;

// Decode/encode the deterministic little-endian AKRRES01 container.  On
// failure the output is left empty and error receives a short diagnostic.
bool Decode(const std::vector<std::uint8_t> &encoded,
            std::vector<Entry> &entries, std::string *error = nullptr);
bool Encode(const std::vector<Entry> &entries,
            std::vector<std::uint8_t> &encoded, std::string *error = nullptr);

} // namespace AetherKiri::ResourceBundle
