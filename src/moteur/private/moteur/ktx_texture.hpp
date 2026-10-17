#pragma once

#include <SDL3/SDL_gpu.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "moteur/renderer.hpp"

namespace moteur {

// A texture ready for the GPU with all its levels (mipmaps): block-compressed (BC7, BC5) or plain
// RGBA8. Plain data: testable without a GPU. create_compressed_texture() uploads it.
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

// For the shipped game (milestone 8, part 6): Basis Universal content transcoded once and for all
// to BC7, or BC5 for a two-channel texture (a normal map), and written as a KTX2 file again,
// Zstandard-supercompressed; decode_ktx2() then takes it as it is. Empty when there is nothing to
// do: content already BC7, BC5 or RGBA8, or a size that is not a multiple of 4 (BC works on 4 x 4
// blocks: such a texture keeps its UASTC and is transcoded at load). Throws like decode_ktx2().
std::vector<std::uint8_t> transcode_ktx2_for_shipping(const void* data, std::size_t size, const std::string& name);

// A texture of `image`, every level as given, in its GPU format (block-compressed or not), through
// Renderer::create_texture_levels(); gpu_bytes is the size of all its levels. Throws
// std::invalid_argument without any level, std::runtime_error if the GPU refuses the texture.
Texture create_compressed_texture(Renderer& renderer, const CompressedImage& image, const char* debug_name = nullptr);

}  // namespace moteur
