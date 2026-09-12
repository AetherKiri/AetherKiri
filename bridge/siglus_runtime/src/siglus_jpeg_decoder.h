#pragma once
#include <cstddef>
#include <cstdint>

namespace aetherkiri::siglus {
    // Borrowed buffers. Returns zero only for a complete RGBA8 image matching
    // the G00 header. Safe for concurrent callers (one decoder per call).
    int32_t DecodeJpeg(const uint8_t *source, size_t size, uint32_t width,
        uint32_t height, uint8_t *output, size_t output_size) noexcept;
}
