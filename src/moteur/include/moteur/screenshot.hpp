#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>

namespace moteur {

// Writes `width` x `height` tightly-packed pixels to `path` as a PNG. `format` is the source
// pixel format (as reported by Renderer::swapchain_format()): a B8G8R8A8 layout is converted to
// RGB, and the alpha channel is always written opaque, since a swapchain's alpha carries no
// meaning once presented. `max_width` > 0: reduced (averages of whole blocks of pixels) to at most
// that width, the proportions kept. The file is written atomically, at a UTF-8 path. Returns false
// and logs on failure.
bool write_capture_png(const std::string& path, const void* pixels, std::uint32_t width, std::uint32_t height,
                       SDL_GPUTextureFormat format, int max_width = 0);

}  // namespace moteur
