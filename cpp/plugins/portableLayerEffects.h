#pragma once

#include <cstdint>

namespace AetherKiri::LayerEffects {

// A small scan-line view used by the legacy layer plug-in adapters.  The
// engine owns the storage; these helpers only mutate the requested pixels.
struct ImageView {
    std::uint8_t *pixels = nullptr;
    int width = 0;
    int height = 0;
    int pitch = 0;
};

// The routines operate on premultiplied/straight BGRA exactly as the KiriKiri
// software bitmap ABI exposes it (B, G, R, A bytes).  Alpha is never changed
// by light/colorize/gray/invert.  Return false for an invalid view or empty
// rectangle; callers can then report a genuine compatibility failure.
bool applyLight(ImageView image, int left, int top, int width, int height,
                int brightness, int contrast);
bool applyColorize(ImageView image, int left, int top, int width, int height,
                   int hue, int saturation, double blend);
bool applyGrayScale(ImageView image, int left, int top, int width, int height);
bool applyInvert(ImageView image, int left, int top, int width, int height);
bool applyMosaic(ImageView image, int left, int top, int width, int height,
                 int blockSize);

} // namespace AetherKiri::LayerEffects
