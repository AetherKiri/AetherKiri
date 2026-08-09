#pragma once

#include <string>
#include <string_view>

// Auto paths are applied in iteration order and the last path wins when two
// directories contain the same short storage name.  Keep archive directories
// deterministic, but apply the archive root last so a nested helper such as
// tools/startup.tjs cannot shadow the package's root startup.tjs.
struct tTVPArchiveAutoPathDirectoryLess {
    bool operator()(const std::u16string &left,
                    const std::u16string &right) const noexcept {
        if(left.empty() != right.empty())
            return !left.empty();
        return left < right;
    }
};

// A path-qualified storage request must keep its directory identity when it
// falls back from the loose project directory to a sibling archive.  The
// legacy auto-path table is keyed only by the short file name, so without this
// check `fgimage/hero/face.png` can resolve to an unrelated
// `image/face.png` that happened to be registered later.
inline bool TVPArchiveAutoPathDirectoryMatches(
    std::u16string_view autoPath,
    std::u16string_view relativeDirectory,
    char16_t archiveDelimiter = u'>') noexcept {
    const size_t delimiter = autoPath.find(archiveDelimiter);
    return delimiter != std::u16string_view::npos &&
        autoPath.substr(delimiter + 1) == relativeDirectory;
}
