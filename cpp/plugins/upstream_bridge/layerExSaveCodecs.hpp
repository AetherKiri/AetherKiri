#pragma once

#include <cstdint>
#include <vector>

namespace aether::krkrz::layer_save {

/**
 * Encode an Aether BGRA layer image with the upstream krkrz_dev PNG codec.
 *
 * The caller owns the input buffer.  The pointer denotes the first logical
 * row; `pitch` may be positive or negative and is the signed byte offset to
 * the next row.  The output is a complete PNG file held in memory; no storage
 * or TJS objects are touched here.
 */
bool encodePng(const std::uint8_t *bgra, int width, int height, int pitch,
               std::vector<std::uint8_t> &output);

/**
 * Encode an 8-bit Aether province image as the fixed 256-entry palette PNG
 * used by krkrz_dev's layerExSave plug-in.  `pitch` is the signed byte offset
 * between logical rows and may be negative.
 */
bool encodeProvincePng(const std::uint8_t *indices, int width, int height,
                       int pitch, std::vector<std::uint8_t> &output);

/**
 * Encode an Aether BGRA layer image with the upstream krkrz_dev TLG5 codec.
 *
 * The caller owns the input buffer.  The pointer denotes the first logical
 * row; `pitch` may be positive or negative and is the signed byte offset to
 * the next row.  This adapter owns only the format framing and row/channel
 * preparation.  The LZSS sliding-window compressor itself is compiled
 * directly from the pinned krkrz_dev submodule (under its `lexsave` namespace).
 */
bool encodeTlg5(const std::uint8_t *bgra, int width, int height, int pitch,
               std::vector<std::uint8_t> &output);

} // namespace aether::krkrz::layer_save
