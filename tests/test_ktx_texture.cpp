#include <doctest/doctest.h>

#include <ktx.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

#include "moteur/ktx_texture.hpp"

namespace {

constexpr ktx_uint32_t kVkR8G8Unorm = 16;
constexpr ktx_uint32_t kVkR8G8B8A8Unorm = 37;
constexpr ktx_uint32_t kVkR8G8B8A8Srgb = 43;

enum class Encoding { None, Uastc };

// A KTX2 file made in memory: `width` x `height`, every mipmap level down to 1 x 1 when `mipmaps`,
// each pixel of level 0 given by `pixel(x, y)` (`channels` bytes), smaller levels flat grey.
template <typename Pixel>
std::vector<std::uint8_t> make_ktx2(int width, int height, int channels, bool srgb, bool mipmaps, Encoding encoding,
                                    Pixel pixel, bool normal_map = false) {
    ktxTextureCreateInfo info = {};
    info.vkFormat = channels == 2 ? kVkR8G8Unorm : (srgb ? kVkR8G8B8A8Srgb : kVkR8G8B8A8Unorm);
    info.baseWidth = static_cast<ktx_uint32_t>(width);
    info.baseHeight = static_cast<ktx_uint32_t>(height);
    info.baseDepth = 1;
    info.numDimensions = 2;
    info.numLevels = 1;
    if (mipmaps) {
        for (int size = std::max(width, height); size > 1; size /= 2) {
            ++info.numLevels;
        }
    }
    info.numLayers = 1;
    info.numFaces = 1;
    info.isArray = KTX_FALSE;
    info.generateMipmaps = KTX_FALSE;
    ktxTexture2* texture = nullptr;
    REQUIRE(ktxTexture2_Create(&info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture) == KTX_SUCCESS);
    for (ktx_uint32_t level = 0; level < info.numLevels; ++level) {
        const int w = std::max(1, width >> level);
        const int h = std::max(1, height >> level);
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w * h * channels), 128);
        if (level == 0) {
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    const std::vector<std::uint8_t> value = pixel(x, y);
                    for (int c = 0; c < channels; ++c) {
                        pixels[static_cast<std::size_t>((y * w + x) * channels + c)] = value[static_cast<std::size_t>(c)];
                    }
                }
            }
        }
        REQUIRE(ktxTexture_SetImageFromMemory(ktxTexture(texture), level, 0, 0, pixels.data(), pixels.size()) == KTX_SUCCESS);
    }
    if (encoding == Encoding::Uastc) {
        ktxBasisParams params = {};
        params.structSize = sizeof(params);
        params.uastc = KTX_TRUE;
        params.threadCount = 1;
        params.uastcFlags = KTX_PACK_UASTC_LEVEL_DEFAULT;  // the level the conversion tool uses (2)
        params.normalMap = normal_map ? KTX_TRUE : KTX_FALSE;
        REQUIRE(ktxTexture2_CompressBasisEx(texture, &params) == KTX_SUCCESS);
    }
    ktx_uint8_t* bytes = nullptr;
    ktx_size_t size = 0;
    REQUIRE(ktxTexture_WriteToMemory(ktxTexture(texture), &bytes, &size) == KTX_SUCCESS);
    std::vector<std::uint8_t> file(bytes, bytes + size);
    std::free(bytes);
    ktxTexture_Destroy(ktxTexture(texture));
    return file;
}

// A smooth colored gradient, like most of a real texture: the codec approximates it closely. (A
// steep one, 30 levels per pixel in three independent channels, comes out up to 50 levels off: a
// 4 x 4 block has only a few colors to choose from.)
std::vector<std::uint8_t> gradient(int x, int y) {
    return {static_cast<std::uint8_t>(60 + x * 8), static_cast<std::uint8_t>(90 + y * 8), static_cast<std::uint8_t>(200 - x * 4), 255};
}

moteur::TextureSettings color_settings() {
    moteur::TextureSettings settings;
    settings.srgb = true;
    settings.mipmaps = true;
    settings.premultiply = false;
    return settings;
}

moteur::TextureSettings normal_settings() {
    moteur::TextureSettings settings;
    settings.mipmaps = true;
    settings.premultiply = false;
    settings.normal_map = true;
    return settings;
}

constexpr moteur::CompressedFormats kBc{true, true};
constexpr moteur::CompressedFormats kNoBc{false, false};

}  // namespace

TEST_CASE("is_ktx2 recognizes the KTX2 identifier") {
    const std::vector<std::uint8_t> file = make_ktx2(4, 4, 4, false, false, Encoding::None, gradient);
    CHECK(moteur::is_ktx2(file.data(), file.size()));
    const std::string png = "\x89PNG\r\n\x1a\n and more";
    CHECK_FALSE(moteur::is_ktx2(png.data(), png.size()));
    CHECK_FALSE(moteur::is_ktx2(file.data(), 11));
    CHECK_FALSE(moteur::is_ktx2(nullptr, 0));
}

TEST_CASE("decode_ktx2 transcodes UASTC colors to BC7, with the file's mipmaps") {
    const std::vector<std::uint8_t> file = make_ktx2(8, 8, 4, true, true, Encoding::Uastc, gradient);
    const moteur::CompressedImage image = moteur::decode_ktx2(file.data(), file.size(), "wood.ktx2", color_settings(), kBc);
    CHECK(image.format == SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM_SRGB);
    CHECK(image.transcoded);
    CHECK(image.width == 8);
    CHECK(image.height == 8);
    REQUIRE(image.levels.size() == 4);  // 8, 4, 2, 1
    CHECK(image.levels[0].size() == 4 * 16);  // 2 x 2 blocks of 16 bytes: 1 byte per pixel
    CHECK(image.levels[1].size() == 16);
    CHECK(image.levels[2].size() == 16);      // a level smaller than a block still takes one
    CHECK(image.levels[3].size() == 16);

    moteur::TextureSettings data = color_settings();
    data.srgb = false;
    CHECK(moteur::decode_ktx2(file.data(), file.size(), "arm.ktx2", data, kBc).format == SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM);
}

TEST_CASE("decode_ktx2 falls back to RGBA8 close to the source without BC7") {
    const std::vector<std::uint8_t> file = make_ktx2(8, 8, 4, true, false, Encoding::Uastc, gradient);
    const moteur::CompressedImage image = moteur::decode_ktx2(file.data(), file.size(), "wood.ktx2", color_settings(), kNoBc);
    CHECK(image.format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB);
    REQUIRE(image.levels.size() == 1);
    REQUIRE(image.levels[0].size() == 8 * 8 * 4);
    int worst = 0;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            const std::vector<std::uint8_t> expected = gradient(x, y);
            for (int c = 0; c < 4; ++c) {
                const int got = image.levels[0][static_cast<std::size_t>((y * 8 + x) * 4 + c)];
                worst = std::max(worst, std::abs(got - static_cast<int>(expected[static_cast<std::size_t>(c)])));
            }
        }
    }
    CHECK(worst <= 12);  // UASTC is close to lossless on a smooth gradient
}

TEST_CASE("decode_ktx2 uses RGBA8 when the size is not made of whole 4 x 4 blocks") {
    const std::vector<std::uint8_t> file = make_ktx2(6, 6, 4, true, false, Encoding::Uastc, gradient);
    const moteur::CompressedImage image = moteur::decode_ktx2(file.data(), file.size(), "odd.ktx2", color_settings(), kBc);
    CHECK(image.format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB);
    CHECK(image.levels[0].size() == 6 * 6 * 4);
}

TEST_CASE("decode_ktx2 turns a two-channel normal map into BC5, or into x and y in red and green") {
    // x varies along the image, y across it: both must come out in their own channel.
    const auto normal = [](int x, int y) {
        return std::vector<std::uint8_t>{static_cast<std::uint8_t>(40 + x * 20), static_cast<std::uint8_t>(200 - y * 20)};
    };
    const std::vector<std::uint8_t> file = make_ktx2(8, 8, 2, false, true, Encoding::Uastc, normal, true);

    const moteur::CompressedImage bc5 = moteur::decode_ktx2(file.data(), file.size(), "n.ktx2", normal_settings(), kBc);
    CHECK(bc5.format == SDL_GPU_TEXTUREFORMAT_BC5_RG_UNORM);
    CHECK(bc5.levels[0].size() == 4 * 16);

    const moteur::CompressedImage rgba = moteur::decode_ktx2(file.data(), file.size(), "n.ktx2", normal_settings(), kNoBc);
    CHECK(rgba.format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
    int worst = 0;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            const std::vector<std::uint8_t> expected = normal(x, y);
            const std::uint8_t* got = &rgba.levels[0][static_cast<std::size_t>((y * 8 + x) * 4)];
            worst = std::max({worst, std::abs(got[0] - static_cast<int>(expected[0])), std::abs(got[1] - static_cast<int>(expected[1]))});
            CHECK(got[2] == 0);
            CHECK(got[3] == 255);
        }
    }
    CHECK(worst <= 12);
}

TEST_CASE("decode_ktx2 takes an uncompressed RGBA8 file as it is") {
    const std::vector<std::uint8_t> file = make_ktx2(4, 2, 4, false, false, Encoding::None, gradient);
    moteur::TextureSettings settings;
    const moteur::CompressedImage image = moteur::decode_ktx2(file.data(), file.size(), "raw.ktx2", settings, kBc);
    CHECK(image.format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
    CHECK_FALSE(image.transcoded);
    REQUIRE(image.levels.size() == 1);
    const std::vector<std::uint8_t> expected = gradient(3, 1);
    CHECK(std::vector<std::uint8_t>(image.levels[0].begin() + (1 * 4 + 3) * 4, image.levels[0].begin() + (1 * 4 + 3) * 4 + 4) == expected);
}

TEST_CASE("decode_ktx2 names the file when it cannot read it") {
    const std::string junk = "not a texture at all";
    CHECK_THROWS_WITH_AS(moteur::decode_ktx2(junk.data(), junk.size(), "junk.ktx2", {}, kBc),
                         doctest::Contains("junk.ktx2"), std::runtime_error);
    std::vector<std::uint8_t> file = make_ktx2(8, 8, 4, true, true, Encoding::Uastc, gradient);
    file.resize(file.size() / 2);
    CHECK_THROWS_WITH_AS(moteur::decode_ktx2(file.data(), file.size(), "cut.ktx2", {}, kBc),
                         doctest::Contains("cut.ktx2"), std::runtime_error);
}
