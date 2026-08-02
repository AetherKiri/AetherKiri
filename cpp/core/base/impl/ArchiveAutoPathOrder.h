#pragma once

#include <string>

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
