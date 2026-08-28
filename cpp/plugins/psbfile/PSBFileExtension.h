#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "tjs.h"

namespace PSB {
    struct PSBFileExtensionV1 {
        std::uint32_t abiVersion = 0;
        bool (*isCompressedFrame)(
            const std::uint8_t *data, std::size_t size) = nullptr;
        bool (*decompressFrame)(
            const std::uint8_t *data,
            std::size_t size,
            std::vector<std::uint8_t> &output,
            std::string &error) = nullptr;
        // Some older native PSB plug-ins used a UTF-16 TJS dictionary as a
        // PIMG index and stored each resource in a sibling file.  Keep the
        // format-specific reconstruction in the optional runtime package;
        // the public loader only owns the dispatch and returned root.
        bool (*loadExternalPimg)(
            const std::uint8_t *data,
            std::size_t size,
            const ttstr &sourceName,
            tTJSVariant &root,
            std::string &error) = nullptr;
    };

    inline constexpr std::uint32_t kPSBFileExtensionAbiVersion = 2;

    bool registerPSBFileExtension(const PSBFileExtensionV1 *extension);
    const PSBFileExtensionV1 *psbFileExtension();
}
