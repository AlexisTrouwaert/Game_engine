#pragma once

#include <SDL3/SDL_gpu.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "moteur/renderer.hpp"

namespace moteur {

// A texture ready for the GPU with all its levels (mipmaps): block-compressed (BC7, BC5) or plain
// RGBA8. Plain data: testable without a GPU. Renderer::create_texture() uploads it.
struct CompressedImage {
    SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_INVALID;
    int width = 0;   // of level 0, in pixels
    int height = 0;
    std::vector<std::vector<std::uint8_t>> levels;  // level 0 first, each tightly packed
    bool transcoded = false;  // from Basis Universal (UASTC or ETC1S), rather than stored as is
};

// True if the bytes start like a KTX2 file.
bool is_ktx2(const void* data, std::size_t size);

// Reads a KTX2 file (libktx) into the format the GPU will sample:
// - Basis Universal content (UASTC, or ETC1S) is transcoded: a two-channel texture (a normal map
//   encoded with --normal-mode: x in RGB, y in alpha) to BC5, anything else to BC7; to RGBA8 when
//   the GPU has no such format (`gpu`) or when the size is not a multiple of 4 (BC works on 4 x 4
//   blocks). A two-channel texture in RGBA8 gets x in red and y in green, like BC5.
// - BC7, BC5 or RGBA8 content is taken as it is (BC7 or BC5 only if the GPU reads it).
// The mipmaps are those of the file (none are computed). `settings.srgb` picks the sRGB variant of
// BC7 and RGBA8 (the engine knows the role of a texture better than the file); premultiplication
// does not apply (the textures are used as stored). `name` appears in errors.
// Throws std::runtime_error naming `name` if the file cannot be read or has an unsupported layout.
CompressedImage decode_ktx2(const void* data, std::size_t size, const std::string& name,
                            const TextureSettings& settings, const CompressedFormats& gpu);

}  // namespace moteur
