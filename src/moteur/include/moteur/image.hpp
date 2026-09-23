#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace moteur {

// Decoded image: 8-bit RGBA, straight (non-premultiplied) alpha, rows stored top to bottom.
struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
};

// Loads a PNG or JPEG file. Throws std::runtime_error if the file is missing or cannot be decoded.
Image load_image(const std::string& path);

// Decodes a PNG or JPEG already in memory (for example a texture inside a .glb model). `name`
// only appears in error messages. Throws std::runtime_error if the data cannot be decoded.
Image decode_image(const void* data, std::size_t size, const std::string& name);

// Multiplies the color of every pixel by its alpha, rounding to the nearest value. This is the
// form the GPU blends and filters correctly: with straight alpha, filtering a pixel next to a
// transparent one drags in the (meaningless) color of the transparent pixel and leaves a halo.
// The renderer applies it when it creates a texture, so files on disk stay in straight alpha.
void premultiply_alpha(Image& image);

}  // namespace moteur
