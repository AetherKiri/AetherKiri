#pragma once

#include <cstdint>

namespace aetherkiri::onscripter {

struct PresentationViewport {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

constexpr PresentationViewport ResolvePresentationViewport(
    uint32_t source_width, uint32_t source_height,
    uint32_t aspect_width, uint32_t aspect_height) {
    PresentationViewport result{0, 0, source_width, source_height};
    if (source_width == 0 || source_height == 0 ||
        aspect_width == 0 || aspect_height == 0) {
        return result;
    }

    // `ons.wide` packages keep their original 4:3 script coordinates but
    // ask the platform wrapper to present the top widescreen viewport. Only
    // crop when the requested aspect is wider; a narrower request must not
    // discard script content implicitly.
    const uint64_t source_at_target_height =
        static_cast<uint64_t>(source_width) * aspect_height;
    const uint64_t target_at_source_height =
        static_cast<uint64_t>(source_height) * aspect_width;
    if (source_at_target_height >= target_at_source_height) {
        return result;
    }

    const uint32_t cropped_height = static_cast<uint32_t>(
        source_at_target_height / aspect_width);
    if (cropped_height > 0 && cropped_height < source_height) {
        result.height = cropped_height;
    }
    return result;
}

constexpr uint32_t ResolvePresentationScroll(
    uint32_t source_height, uint32_t viewport_y,
    uint32_t viewport_height, uint32_t current_scroll,
    int32_t delta) {
    if (source_height == 0 || viewport_height == 0 ||
        viewport_y >= source_height ||
        viewport_height > source_height - viewport_y) {
        return 0;
    }

    const uint32_t max_scroll =
        source_height - viewport_y - viewport_height;
    const int64_t requested =
        static_cast<int64_t>(current_scroll) + delta;
    if (requested <= 0) {
        return 0;
    }
    if (requested >= static_cast<int64_t>(max_scroll)) {
        return max_scroll;
    }
    return static_cast<uint32_t>(requested);
}

} // namespace aetherkiri::onscripter
