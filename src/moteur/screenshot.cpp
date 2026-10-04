#include "moteur/screenshot.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <cstddef>
#include <vector>

#include "moteur/file_io.hpp"

namespace moteur {

bool write_capture_png(const std::string& path, const void* pixels, std::uint32_t width, std::uint32_t height,
                       SDL_GPUTextureFormat format, int max_width) {
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

    // Reduced by a whole factor: each pixel the average of a block (a thumbnail).
    if (max_width > 0 && width > static_cast<std::uint32_t>(max_width)) {
        const std::uint32_t factor = (width + static_cast<std::uint32_t>(max_width) - 1) / static_cast<std::uint32_t>(max_width);
        const std::uint32_t w = width / factor;
        const std::uint32_t h = std::max<std::uint32_t>(1, height / factor);
        std::vector<std::uint8_t> small(static_cast<std::size_t>(w) * h * 4);
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                std::uint32_t sum[3] = {0, 0, 0};
                for (std::uint32_t dy = 0; dy < factor; ++dy) {
                    const std::uint8_t* row = &rgba[((static_cast<std::size_t>(y) * factor + dy) * width + x * factor) * 4];
                    for (std::uint32_t dx = 0; dx < factor; ++dx) {
                        sum[0] += row[dx * 4 + 0];
                        sum[1] += row[dx * 4 + 1];
                        sum[2] += row[dx * 4 + 2];
                    }
                }
                std::uint8_t* out = &small[(static_cast<std::size_t>(y) * w + x) * 4];
                for (int c = 0; c < 3; ++c) {
                    out[c] = static_cast<std::uint8_t>(sum[c] / (factor * factor));
                }
                out[3] = 255;
            }
        }
        rgba = std::move(small);
        width = w;
        height = h;
    }

    std::vector<std::uint8_t> png;
    const auto append = [](void* context, void* data, int size) {
        auto* bytes = static_cast<std::vector<std::uint8_t>*>(context);
        bytes->insert(bytes->end(), static_cast<std::uint8_t*>(data), static_cast<std::uint8_t*>(data) + size);
    };
    if (stbi_write_png_to_func(append, &png, static_cast<int>(width), static_cast<int>(height), 4, rgba.data(),
                               static_cast<int>(width) * 4) == 0) {
        SDL_Log("stbi_write_png failed for '%s'", path.c_str());
        return false;
    }
    std::string error;
    if (!write_file_atomic(path, png.data(), png.size(), error)) {
        SDL_Log("Capture: %s", error.c_str());
        return false;
    }
    return true;
}

}  // namespace moteur
