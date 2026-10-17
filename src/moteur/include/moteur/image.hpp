#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace moteur {

/// Decoded image: 8-bit RGBA, straight (non-premultiplied) alpha, rows stored top to bottom.
struct Image {
    int width = 0;   ///< in pixels
    int height = 0;  ///< in pixels
    /// `width * height * 4` bytes: R, G, B, A for each pixel, row after row, no padding between rows.
    std::vector<std::uint8_t> pixels;
};

/// Loads a PNG or JPEG file (any channel count, always expanded to RGBA8). `path` is UTF-8 and may
/// hold non-ASCII characters on Windows. Throws std::runtime_error, naming the file, if it is
/// missing or cannot be decoded. Reads the whole file and allocates the decoded pixels; safe to call
/// from any thread (no GPU involved).
Image load_image(const std::string& path);

/// Decodes a PNG or JPEG already in memory (for example a texture inside a .glb model). `name`
/// only appears in error messages. Throws std::runtime_error if the data cannot be decoded. The
/// data is only read during the call; the result owns its own copy of the pixels.
Image decode_image(const void* data, std::size_t size, const std::string& name);

/// Multiplies the color of every pixel by its alpha, rounding to the nearest value. This is the
/// form the GPU blends and filters correctly: with straight alpha, filtering a pixel next to a
/// transparent one drags in the (meaningless) color of the transparent pixel and leaves a halo.
/// The renderer applies it when it creates a texture (TextureSettings::premultiply, on by default),
/// so files on disk stay in straight alpha. Fully opaque pixels are left untouched. In place, O(pixels).
void premultiply_alpha(Image& image);

}  // namespace moteur
