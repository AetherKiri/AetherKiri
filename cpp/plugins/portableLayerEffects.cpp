#include "portableLayerEffects.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace AetherKiri::LayerEffects {
namespace {

struct Hsl {
    double h = 0.0; // [0, 1)
    double s = 0.0; // [0, 1]
    double l = 0.0; // [0, 1]
};

struct Rgb {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
};

int clampByte(double value) {
    if(value <= 0.0)
        return 0;
    if(value >= 255.0)
        return 255;
    return static_cast<int>(value);
}

bool clipRect(const ImageView &image, int &left, int &top, int &width,
              int &height) {
    if(!image.pixels || image.width <= 0 || image.height <= 0 ||
       image.pitch < image.width * 4)
        return false;

    const long long right = std::min<long long>(image.width,
                                                static_cast<long long>(left) +
                                                    std::max(0, width));
    const long long bottom = std::min<long long>(image.height,
                                                  static_cast<long long>(top) +
                                                      std::max(0, height));
    left = std::max(0, left);
    top = std::max(0, top);
    width = static_cast<int>(right - left);
    height = static_cast<int>(bottom - top);
    return width > 0 && height > 0;
}

Hsl rgbToHsl(const Rgb &rgb) {
    const double maxValue = std::max({rgb.r, rgb.g, rgb.b});
    const double minValue = std::min({rgb.r, rgb.g, rgb.b});
    Hsl out;
    out.l = (maxValue + minValue) * 0.5;
    const double delta = maxValue - minValue;
    if(delta <= 1e-12) {
        out.h = 0.0;
        out.s = 0.0;
        return out;
    }

    out.s = out.l <= 0.5 ? delta / (maxValue + minValue)
                         : delta / (2.0 - maxValue - minValue);
    if(maxValue == rgb.r)
        out.h = (rgb.g - rgb.b) / delta + (rgb.g < rgb.b ? 6.0 : 0.0);
    else if(maxValue == rgb.g)
        out.h = (rgb.b - rgb.r) / delta + 2.0;
    else
        out.h = (rgb.r - rgb.g) / delta + 4.0;
    out.h /= 6.0;
    return out;
}

double hueToRgb(double p, double q, double t) {
    if(t < 0.0)
        t += 1.0;
    if(t > 1.0)
        t -= 1.0;
    if(t < 1.0 / 6.0)
        return p + (q - p) * 6.0 * t;
    if(t < 1.0 / 2.0)
        return q;
    if(t < 2.0 / 3.0)
        return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
    return p;
}

Rgb hslToRgb(const Hsl &hsl) {
    if(hsl.s <= 1e-12)
        return {hsl.l, hsl.l, hsl.l};
    const double q = hsl.l < 0.5 ? hsl.l * (1.0 + hsl.s)
                                 : hsl.l + hsl.s - hsl.l * hsl.s;
    const double p = 2.0 * hsl.l - q;
    return {hueToRgb(p, q, hsl.h + 1.0 / 3.0), hueToRgb(p, q, hsl.h),
            hueToRgb(p, q, hsl.h - 1.0 / 3.0)};
}

bool validPixelRange(const ImageView &image, int left, int top, int width,
                     int height) {
    return left >= 0 && top >= 0 && width > 0 && height > 0 &&
        left <= image.width && top <= image.height &&
        width <= image.width - left && height <= image.height - top;
}

} // namespace

bool applyLight(ImageView image, int left, int top, int width, int height,
                int brightness, int contrast) {
    if(!clipRect(image, left, top, width, height))
        return false;

    // This is the lookup-table formula used by krkrz's layerExImage:
    // contrast is a percentage around the midpoint and brightness is an
    // offset from neutral gray (128).
    const double factor = (100.0 + static_cast<double>(contrast)) / 100.0;
    const int offset = brightness + 128;
    std::uint8_t lut[256];
    for(int i = 0; i < 256; ++i) {
        lut[i] = static_cast<std::uint8_t>(clampByte(
            (static_cast<double>(i) - 128.0) * factor + offset));
    }

    for(int y = top; y < top + height; ++y) {
        auto *row = image.pixels + static_cast<std::ptrdiff_t>(y) * image.pitch +
            static_cast<std::ptrdiff_t>(left) * 4;
        for(int x = 0; x < width; ++x) {
            row[0] = lut[row[0]];
            row[1] = lut[row[1]];
            row[2] = lut[row[2]];
            row += 4;
        }
    }
    return true;
}

bool applyColorize(ImageView image, int left, int top, int width, int height,
                   int hue, int saturation, double blend) {
    if(!clipRect(image, left, top, width, height))
        return false;

    hue = std::clamp(hue, 0, 255);
    saturation = std::clamp(saturation, 0, 255);
    blend = std::clamp(blend, 0.0, 1.0);
    const double targetHue = static_cast<double>(hue) / 255.0;
    const double targetSaturation = static_cast<double>(saturation) / 255.0;

    for(int y = top; y < top + height; ++y) {
        auto *row = image.pixels + static_cast<std::ptrdiff_t>(y) * image.pitch +
            static_cast<std::ptrdiff_t>(left) * 4;
        for(int x = 0; x < width; ++x) {
            const Rgb source{row[2] / 255.0, row[1] / 255.0,
                             row[0] / 255.0};
            Hsl target = rgbToHsl(source);
            target.h = targetHue;
            target.s = targetSaturation;
            const Rgb recolored = hslToRgb(target);
            row[2] = static_cast<std::uint8_t>(clampByte(
                (recolored.r * 255.0) * blend + source.r * 255.0 * (1.0 - blend)));
            row[1] = static_cast<std::uint8_t>(clampByte(
                (recolored.g * 255.0) * blend + source.g * 255.0 * (1.0 - blend)));
            row[0] = static_cast<std::uint8_t>(clampByte(
                (recolored.b * 255.0) * blend + source.b * 255.0 * (1.0 - blend)));
            row += 4;
        }
    }
    return true;
}

bool applyGrayScale(ImageView image, int left, int top, int width, int height) {
    if(!clipRect(image, left, top, width, height))
        return false;
    for(int y = top; y < top + height; ++y) {
        auto *row = image.pixels + static_cast<std::ptrdiff_t>(y) * image.pitch +
            static_cast<std::ptrdiff_t>(left) * 4;
        for(int x = 0; x < width; ++x) {
            const int gray = (static_cast<int>(row[2]) * 77 +
                              static_cast<int>(row[1]) * 150 +
                              static_cast<int>(row[0]) * 29 + 128) >> 8;
            row[0] = row[1] = row[2] = static_cast<std::uint8_t>(gray);
            row += 4;
        }
    }
    return true;
}

bool applyInvert(ImageView image, int left, int top, int width, int height) {
    if(!clipRect(image, left, top, width, height))
        return false;
    for(int y = top; y < top + height; ++y) {
        auto *row = image.pixels + static_cast<std::ptrdiff_t>(y) * image.pitch +
            static_cast<std::ptrdiff_t>(left) * 4;
        for(int x = 0; x < width; ++x) {
            row[0] = static_cast<std::uint8_t>(255 - row[0]);
            row[1] = static_cast<std::uint8_t>(255 - row[1]);
            row[2] = static_cast<std::uint8_t>(255 - row[2]);
            row += 4;
        }
    }
    return true;
}

bool applyMosaic(ImageView image, int left, int top, int width, int height,
                 int blockSize) {
    if(blockSize <= 0 || !clipRect(image, left, top, width, height))
        return false;

    // Snapshot the selected rectangle.  This makes adjacent blocks
    // independent and also gives deterministic behavior when the caller
    // mosaics a region in-place repeatedly.
    const std::size_t rowBytes = static_cast<std::size_t>(width) * 4;
    std::vector<std::uint8_t> source(static_cast<std::size_t>(height) * rowBytes);
    for(int y = 0; y < height; ++y) {
        const auto *row = image.pixels +
            static_cast<std::ptrdiff_t>(top + y) * image.pitch +
            static_cast<std::ptrdiff_t>(left) * 4;
        std::copy_n(row, rowBytes, source.data() + static_cast<std::size_t>(y) * rowBytes);
    }

    blockSize = std::clamp(blockSize, 1, 1024);
    for(int by = 0; by < height; by += blockSize) {
        const int blockHeight = std::min(blockSize, height - by);
        for(int bx = 0; bx < width; bx += blockSize) {
            const int blockWidth = std::min(blockSize, width - bx);
            std::uint64_t sum[4] = {0, 0, 0, 0};
            for(int y = 0; y < blockHeight; ++y) {
                const auto *row = source.data() +
                    static_cast<std::size_t>(by + y) * rowBytes +
                    static_cast<std::size_t>(bx) * 4;
                for(int x = 0; x < blockWidth; ++x) {
                    for(int channel = 0; channel < 4; ++channel)
                        sum[channel] += row[channel];
                    row += 4;
                }
            }
            const std::uint64_t count = static_cast<std::uint64_t>(blockWidth) *
                static_cast<std::uint64_t>(blockHeight);
            for(int y = 0; y < blockHeight; ++y) {
                auto *row = image.pixels +
                    static_cast<std::ptrdiff_t>(top + by + y) * image.pitch +
                    static_cast<std::ptrdiff_t>(left + bx) * 4;
                for(int x = 0; x < blockWidth; ++x) {
                    for(int channel = 0; channel < 4; ++channel)
                        row[channel] = static_cast<std::uint8_t>(sum[channel] / count);
                    row += 4;
                }
            }
        }
    }
    return true;
}

} // namespace AetherKiri::LayerEffects
