#pragma once

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

// Loads a PNG file. Throws std::runtime_error if the file is missing or cannot be decoded.
Image load_image(const std::string& path);

// Multiplies the color of every pixel by its alpha, rounding to the nearest value. This is the
// form the GPU blends and filters correctly: with straight alpha, filtering a pixel next to a
// transparent one drags in the (meaningless) color of the transparent pixel and leaves a halo.
// The renderer applies it when it creates a texture, so files on disk stay in straight alpha.
void premultiply_alpha(Image& image);

}  // namespace moteur
