#include "moteur/image.hpp"

#include <SDL3/SDL.h>

#include <stdexcept>

#include "moteur/paths.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG  // many glTF models store their textures as JPEG
#define STBI_ONLY_HDR   // Radiance .hdr environments (see environment.cpp)
#include <stb_image.h>

namespace moteur {

Image load_image(const std::string& path) {
    FileData file;
    try {
        file = read_file(path);  // not stbi_load: its fopen does not handle UTF-8 paths on Windows
    } catch (const std::runtime_error& e) {
        throw std::runtime_error("Cannot read image '" + path + "': " + e.what());
    }
    return decode_image(file.data(), file.size(), path);
}

Image decode_image(const void* data, std::size_t size, const std::string& name) {
    int width = 0, height = 0, channels = 0;
    stbi_uc* decoded = stbi_load_from_memory(static_cast<const stbi_uc*>(data), static_cast<int>(size), &width,
                                             &height, &channels, 4);
    if (decoded == nullptr) {
        throw std::runtime_error("Cannot decode image '" + name + "': " + stbi_failure_reason());
    }

    Image image;
    image.width = width;
    image.height = height;
    image.pixels.assign(decoded, decoded + static_cast<std::size_t>(width) * height * 4);
    stbi_image_free(decoded);
    return image;
}

void premultiply_alpha(Image& image) {
    for (std::size_t i = 0; i + 3 < image.pixels.size(); i += 4) {
        const unsigned alpha = image.pixels[i + 3];
        if (alpha == 255) {
            continue;
        }
        for (std::size_t channel = 0; channel < 3; ++channel) {
            image.pixels[i + channel] = static_cast<std::uint8_t>((image.pixels[i + channel] * alpha + 127) / 255);
        }
    }
}

}  // namespace moteur
