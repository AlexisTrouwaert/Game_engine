#include "moteur/ktx_texture.hpp"

#include <ktx.h>

#include <cstring>
#include <memory>
#include <stdexcept>

namespace moteur {

namespace {

// VkFormat values of the formats handled here (vulkan_core.h), without including Vulkan.
constexpr ktx_uint32_t kVkR8G8B8A8Unorm = 37;
constexpr ktx_uint32_t kVkR8G8B8A8Srgb = 43;
constexpr ktx_uint32_t kVkBc5UnormBlock = 141;
constexpr ktx_uint32_t kVkBc7UnormBlock = 145;
constexpr ktx_uint32_t kVkBc7SrgbBlock = 146;

struct KtxDeleter {
    void operator()(ktxTexture2* texture) const { ktxTexture_Destroy(ktxTexture(texture)); }
};

void check(KTX_error_code result, const std::string& name, const char* what) {
    if (result != KTX_SUCCESS) {
        throw std::runtime_error("KTX2 texture '" + name + "': " + what + ": " + ktxErrorString(result));
    }
}

}  // namespace

bool is_ktx2(const void* data, std::size_t size) {
    static constexpr std::uint8_t kIdentifier[12] = {0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, '\r', '\n', 0x1A, '\n'};
    return data != nullptr && size >= sizeof(kIdentifier) && std::memcmp(data, kIdentifier, sizeof(kIdentifier)) == 0;
}

CompressedImage decode_ktx2(const void* data, std::size_t size, const std::string& name,
                            const TextureSettings& settings, const CompressedFormats& gpu) {
    if (!is_ktx2(data, size)) {
        throw std::runtime_error("KTX2 texture '" + name + "': not a KTX2 file");
    }
    ktxTexture2* raw = nullptr;
    check(ktxTexture2_CreateFromMemory(static_cast<const ktx_uint8_t*>(data), size,
                                       KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &raw),
          name, "cannot read it");
    const std::unique_ptr<ktxTexture2, KtxDeleter> texture(raw);

    if (texture->numDimensions != 2 || texture->isCubemap || texture->isArray || texture->numFaces != 1 ||
        texture->numLayers != 1) {
        throw std::runtime_error("KTX2 texture '" + name + "': only plain 2D textures are supported (no cube map, array or 3D)");
    }
    const int width = static_cast<int>(texture->baseWidth);
    const int height = static_cast<int>(texture->baseHeight);
    const bool whole_blocks = width % 4 == 0 && height % 4 == 0;
    const bool two_channels = ktxTexture2_GetNumComponents(texture.get()) == 2;

    CompressedImage image;
    image.width = width;
    image.height = height;
    bool swizzle_xy = false;  // a two-channel texture transcoded to RGBA: (x, x, x, y) -> (x, y, 0, 255)
    if (ktxTexture2_NeedsTranscoding(texture.get())) {
        ktx_transcode_fmt_e target = KTX_TTF_RGBA32;
        if (whole_blocks && two_channels && gpu.bc5) {
            target = KTX_TTF_BC5_RG;
        } else if (whole_blocks && !two_channels && gpu.bc7) {
            target = KTX_TTF_BC7_RGBA;
        }
        check(ktxTexture2_TranscodeBasis(texture.get(), target, 0), name, "cannot transcode it");
        image.transcoded = true;
        swizzle_xy = two_channels && target == KTX_TTF_RGBA32;
    }

    switch (texture->vkFormat) {
        case kVkBc7UnormBlock:
        case kVkBc7SrgbBlock:
            if (!gpu.bc7) {
                throw std::runtime_error("KTX2 texture '" + name + "': the GPU cannot read BC7");
            }
            image.format = settings.srgb ? SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM_SRGB : SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM;
            break;
        case kVkBc5UnormBlock:
            if (!gpu.bc5) {
                throw std::runtime_error("KTX2 texture '" + name + "': the GPU cannot read BC5");
            }
            image.format = SDL_GPU_TEXTUREFORMAT_BC5_RG_UNORM;
            break;
        case kVkR8G8B8A8Unorm:
        case kVkR8G8B8A8Srgb:
            image.format = settings.srgb ? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            break;
        default:
            throw std::runtime_error("KTX2 texture '" + name + "': format " + std::to_string(texture->vkFormat) +
                                     " is not supported (Basis Universal, BC7, BC5 or RGBA8 only)");
    }

    const ktx_uint8_t* bytes = ktxTexture_GetData(ktxTexture(texture.get()));
    for (ktx_uint32_t level = 0; level < texture->numLevels; ++level) {
        ktx_size_t offset = 0;
        check(ktxTexture_GetImageOffset(ktxTexture(texture.get()), level, 0, 0, &offset), name, "bad level");
        const ktx_size_t level_size = ktxTexture_GetImageSize(ktxTexture(texture.get()), level);
        std::vector<std::uint8_t> pixels(bytes + offset, bytes + offset + level_size);
        if (swizzle_xy) {
            for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
                pixels[i + 1] = pixels[i + 3];
                pixels[i + 2] = 0;
                pixels[i + 3] = 255;
            }
        }
        image.levels.push_back(std::move(pixels));
    }
    return image;
}

}  // namespace moteur
