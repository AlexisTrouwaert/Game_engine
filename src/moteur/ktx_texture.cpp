#include "moteur/ktx_texture.hpp"

#include <ktx.h>

#include <algorithm>
#include <cstdlib>
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

// The KTX2 header (KTX 2.0 specification, section 3): 80 bytes, then one level index entry per
// mip level.
constexpr std::size_t kHeaderSize = 80;
constexpr std::size_t kLevelEntrySize = 24;
constexpr std::uint32_t kMaxDimension = 16384;  // the largest 2D texture Direct3D 12 and Metal take
constexpr std::uint32_t kSupercompressionNone = 0;
constexpr std::uint32_t kSupercompressionBasisLz = 1;
constexpr std::uint32_t kSupercompressionZlib = 3;

template <typename T>
T read_le(const std::uint8_t* at) {
    T value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        value |= static_cast<T>(static_cast<T>(at[i]) << (8 * i));
    }
    return value;
}

bool range_inside(std::uint64_t offset, std::uint64_t length, std::size_t size) {
    return offset <= size && length <= size - offset;
}

// Checks every size the header declares against the file and against what the texture's format
// and dimensions make exact, before libktx allocates anything from them: libktx allocates the
// declared (uncompressed) sizes first, and a damaged file asked it for 4 GB (found by fuzzing,
// milestone 8 part 8). Only what decode_ktx2 handles passes: plain 2D textures in Basis Universal,
// BC7, BC5 or RGBA8.
void validate_header(const std::uint8_t* data, std::size_t size, const std::string& name) {
    const auto refuse = [&name](const std::string& why) {
        throw std::runtime_error("KTX2 texture '" + name + "': damaged or unsupported header: " + why);
    };
    if (size < kHeaderSize) {
        refuse("file too short");
    }
    const auto vk_format = read_le<std::uint32_t>(data + 12);
    const auto width = read_le<std::uint32_t>(data + 20);
    const auto height = read_le<std::uint32_t>(data + 24);
    const auto depth = read_le<std::uint32_t>(data + 28);
    const auto layers = read_le<std::uint32_t>(data + 32);
    const auto faces = read_le<std::uint32_t>(data + 36);
    const auto level_count = read_le<std::uint32_t>(data + 40);
    const auto supercompression = read_le<std::uint32_t>(data + 44);
    const auto dfd_offset = read_le<std::uint32_t>(data + 48);
    const auto dfd_length = read_le<std::uint32_t>(data + 52);
    const auto kvd_offset = read_le<std::uint32_t>(data + 56);
    const auto kvd_length = read_le<std::uint32_t>(data + 60);
    const auto sgd_offset = read_le<std::uint64_t>(data + 64);
    const auto sgd_length = read_le<std::uint64_t>(data + 72);

    if (width == 0 || height == 0 || width > kMaxDimension || height > kMaxDimension || depth != 0 || layers != 0 ||
        faces != 1) {
        refuse("only plain 2D textures of 1 to " + std::to_string(kMaxDimension) + " pixels are supported (" +
               std::to_string(width) + "x" + std::to_string(height) + ")");
    }
    std::uint32_t max_levels = 1;
    for (std::uint32_t side = std::max(width, height); side > 1; side /= 2) {
        ++max_levels;
    }
    const std::uint32_t levels = std::max<std::uint32_t>(level_count, 1);
    if (levels > max_levels) {
        refuse(std::to_string(level_count) + " mip levels for " + std::to_string(width) + "x" + std::to_string(height));
    }
    if (supercompression > kSupercompressionZlib) {
        refuse("supercompression scheme " + std::to_string(supercompression));
    }
    if (size < kHeaderSize + levels * kLevelEntrySize || !range_inside(dfd_offset, dfd_length, size) ||
        !range_inside(kvd_offset, kvd_length, size) || !range_inside(sgd_offset, sgd_length, size)) {
        refuse("a part of the file lies beyond its end");
    }

    // The exact size of each level once decompressed: 16 bytes per 4x4 block (UASTC, BC7, BC5), 4
    // per pixel (RGBA8). BasisLZ (ETC1S) levels have no fixed size: the specification has them
    // declare 0.
    std::uint64_t block_bytes = 0;  // 0: four bytes per pixel
    switch (vk_format) {
        case 0:  // Basis Universal
        case kVkBc5UnormBlock:
        case kVkBc7UnormBlock:
        case kVkBc7SrgbBlock:
            block_bytes = 16;
            break;
        case kVkR8G8B8A8Unorm:
        case kVkR8G8B8A8Srgb:
            break;
        default:
            refuse("format " + std::to_string(vk_format) + " (Basis Universal, BC7, BC5 or RGBA8 only)");
    }
    const bool basis_lz = supercompression == kSupercompressionBasisLz;
    if (basis_lz && vk_format != 0) {
        refuse("BasisLZ supercompression of a format other than Basis Universal");
    }
    std::uint64_t previous_offset = 0;  // of the level before (a larger one, stored after this one)
    for (std::uint32_t level = 0; level < levels; ++level) {
        const std::uint8_t* entry = data + kHeaderSize + level * kLevelEntrySize;
        const auto offset = read_le<std::uint64_t>(entry);
        const auto length = read_le<std::uint64_t>(entry + 8);
        // The specification stores the levels smallest first: each one ends before the larger one
        // starts. libktx counts on it (its data size is the first level's end minus the last one's
        // start, which wraps around otherwise: found by long fuzzing, milestone 8 part 8).
        if (level > 0 && (offset > previous_offset || length > previous_offset - offset)) {
            refuse("mip level " + std::to_string(level) + " is not stored before mip level " + std::to_string(level - 1));
        }
        previous_offset = offset;
        const auto uncompressed = read_le<std::uint64_t>(entry + 16);
        if (!range_inside(offset, length, size)) {
            refuse("mip level " + std::to_string(level) + " lies beyond the end of the file");
        }
        const std::uint64_t w = std::max<std::uint32_t>(width >> level, 1);
        const std::uint64_t h = std::max<std::uint32_t>(height >> level, 1);
        const std::uint64_t expected = block_bytes != 0 ? (w + 3) / 4 * ((h + 3) / 4) * block_bytes : w * h * 4;
        const bool exact = basis_lz ? uncompressed == 0
                                    : uncompressed == expected &&
                                          (supercompression != kSupercompressionNone || length == expected);
        if (!exact) {
            refuse("mip level " + std::to_string(level) + " declares " + std::to_string(uncompressed) + " bytes, " +
                   std::to_string(basis_lz ? 0 : expected) + " expected");
        }
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
    validate_header(static_cast<const std::uint8_t*>(data), size, name);
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

std::vector<std::uint8_t> transcode_ktx2_for_shipping(const void* data, std::size_t size, const std::string& name) {
    if (!is_ktx2(data, size)) {
        throw std::runtime_error("KTX2 texture '" + name + "': not a KTX2 file");
    }
    validate_header(static_cast<const std::uint8_t*>(data), size, name);
    ktxTexture2* raw = nullptr;
    check(ktxTexture2_CreateFromMemory(static_cast<const ktx_uint8_t*>(data), size,
                                       KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &raw),
          name, "cannot read it");
    const std::unique_ptr<ktxTexture2, KtxDeleter> source(raw);
    if (!ktxTexture2_NeedsTranscoding(source.get()) || source->baseWidth % 4 != 0 || source->baseHeight % 4 != 0 ||
        source->numDimensions != 2 || source->isCubemap || source->isArray) {
        return {};
    }
    const bool two_channels = ktxTexture2_GetNumComponents(source.get()) == 2;
    check(ktxTexture2_TranscodeBasis(source.get(), two_channels ? KTX_TTF_BC5_RG : KTX_TTF_BC7_RGBA, 0), name,
          "cannot transcode it");

    ktxTextureCreateInfo info{};
    info.vkFormat = two_channels ? kVkBc5UnormBlock : kVkBc7UnormBlock;  // sRGB or not: chosen at load
    info.baseWidth = source->baseWidth;
    info.baseHeight = source->baseHeight;
    info.baseDepth = 1;
    info.numDimensions = 2;
    info.numLevels = source->numLevels;
    info.numLayers = 1;
    info.numFaces = 1;
    info.isArray = KTX_FALSE;
    info.generateMipmaps = KTX_FALSE;
    ktxTexture2* made = nullptr;
    check(ktxTexture2_Create(&info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &made), name, "cannot create the BC texture");
    const std::unique_ptr<ktxTexture2, KtxDeleter> target(made);
    const ktx_uint8_t* bytes = ktxTexture_GetData(ktxTexture(source.get()));
    for (ktx_uint32_t level = 0; level < source->numLevels; ++level) {
        ktx_size_t offset = 0;
        check(ktxTexture_GetImageOffset(ktxTexture(source.get()), level, 0, 0, &offset), name, "bad level");
        const ktx_size_t level_size = ktxTexture_GetImageSize(ktxTexture(source.get()), level);
        check(ktxTexture_SetImageFromMemory(ktxTexture(target.get()), level, 0, 0, bytes + offset, level_size), name,
              "cannot store a level");
    }
    check(ktxTexture2_DeflateZstd(target.get(), 18), name, "cannot supercompress it");
    ktx_uint8_t* written = nullptr;
    ktx_size_t written_size = 0;
    check(ktxTexture_WriteToMemory(ktxTexture(target.get()), &written, &written_size), name, "cannot write it");
    std::vector<std::uint8_t> file(written, written + written_size);
    std::free(written);
    return file;
}

Texture create_compressed_texture(Renderer& renderer, const CompressedImage& image, const char* debug_name) {
    Texture texture;
    texture.gpu = renderer.create_texture_levels(image.format, image.width, image.height, image.levels, debug_name);
    texture.width = image.width;
    texture.height = image.height;
    for (const std::vector<std::uint8_t>& level : image.levels) {
        texture.gpu_bytes += level.size();
    }
    return texture;
}

}  // namespace moteur
