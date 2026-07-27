#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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
    };

    inline constexpr std::uint32_t kPSBFileExtensionAbiVersion = 1;

    bool registerPSBFileExtension(const PSBFileExtensionV1 *extension);
    const PSBFileExtensionV1 *psbFileExtension();
}
