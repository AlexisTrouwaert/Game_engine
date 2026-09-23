#include "moteur/screenshot.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <cstddef>
#include <vector>

namespace moteur {

bool write_capture_png(const std::string& path, const void* pixels, std::uint32_t width, std::uint32_t height,
                       SDL_GPUTextureFormat format) {
    const bool bgra = format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM ||
                      format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB;

    const auto* src = static_cast<const std::uint8_t*>(pixels);
    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<std::uint8_t> rgba(count * 4);
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint8_t c0 = src[i * 4 + 0];
        const std::uint8_t c1 = src[i * 4 + 1];
        const std::uint8_t c2 = src[i * 4 + 2];
        rgba[i * 4 + 0] = bgra ? c2 : c0;
        rgba[i * 4 + 1] = c1;
        rgba[i * 4 + 2] = bgra ? c0 : c2;
        rgba[i * 4 + 3] = 255;  // the presented swapchain has no meaningful alpha of its own
    }

    if (stbi_write_png(path.c_str(), static_cast<int>(width), static_cast<int>(height), 4, rgba.data(),
                       static_cast<int>(width) * 4) == 0) {
        SDL_Log("stbi_write_png failed for '%s'", path.c_str());
        return false;
    }
    return true;
}

}  // namespace moteur
