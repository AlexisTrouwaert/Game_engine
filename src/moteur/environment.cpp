#include "moteur/environment.hpp"

#include <SDL3/SDL.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/packing.hpp>
#include <stb_image.h>  // implementation compiled in image.cpp

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

#include "moteur/renderer.hpp"

namespace moteur {

namespace {

constexpr float kPi = glm::pi<float>();

// The 9 real spherical harmonics of order 0 to 2, in a unit direction.
void sh_basis(glm::vec3 d, float out[9]) {
    out[0] = 0.282095f;
    out[1] = 0.488603f * d.y;
    out[2] = 0.488603f * d.z;
    out[3] = 0.488603f * d.x;
    out[4] = 1.092548f * d.x * d.y;
    out[5] = 1.092548f * d.y * d.z;
    out[6] = 0.315392f * (3.0f * d.z * d.z - 1.0f);
    out[7] = 1.092548f * d.x * d.z;
    out[8] = 0.546274f * (d.x * d.x - d.y * d.y);
}

// The next smaller image of a mip chain: the average of each 2 x 2 block.
EnvironmentImage downsample(const EnvironmentImage& image) {
    EnvironmentImage half;
    half.width = std::max(1, image.width / 2);
    half.height = std::max(1, image.height / 2);
    half.texels.resize(static_cast<std::size_t>(half.width) * static_cast<std::size_t>(half.height));
    for (int y = 0; y < half.height; ++y) {
        for (int x = 0; x < half.width; ++x) {
            const int x0 = std::min(x * 2, image.width - 1), x1 = std::min(x * 2 + 1, image.width - 1);
            const int y0 = std::min(y * 2, image.height - 1), y1 = std::min(y * 2 + 1, image.height - 1);
            half.at(x, y) = (image.at(x0, y0) + image.at(x1, y0) + image.at(x0, y1) + image.at(x1, y1)) * 0.25f;
        }
    }
    return half;
}

// A low-discrepancy point set in [0, 1)^2: spreads GGX samples more evenly than random numbers.
glm::vec2 hammersley(std::uint32_t i, std::uint32_t count) {
    std::uint32_t bits = i;
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return {static_cast<float>(i) / static_cast<float>(count), static_cast<float>(bits) * 2.3283064365386963e-10f};
}

}  // namespace

glm::vec2 direction_to_equirect(glm::vec3 d) {
    return {std::atan2(d.z, d.x) / (2.0f * kPi) + 0.5f, std::acos(std::clamp(d.y, -1.0f, 1.0f)) / kPi};
}

glm::vec3 equirect_to_direction(glm::vec2 uv) {
    const float phi = (uv.x - 0.5f) * 2.0f * kPi;
    const float theta = uv.y * kPi;
    return {std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi)};
}

glm::vec3 EnvironmentImage::sample(glm::vec3 direction) const {
    const glm::vec2 uv = direction_to_equirect(direction);
    const float fx = uv.x * static_cast<float>(width) - 0.5f;
    const float fy = std::clamp(uv.y * static_cast<float>(height) - 0.5f, 0.0f, static_cast<float>(height - 1));
    const int x0 = static_cast<int>(std::floor(fx));
    const int y0 = static_cast<int>(std::floor(fy));
    const float tx = fx - static_cast<float>(x0), ty = fy - static_cast<float>(y0);
    const auto wrap = [this](int x) { return ((x % width) + width) % width; };
    const int y1 = std::min(y0 + 1, height - 1);
    const glm::vec3 top = glm::mix(at(wrap(x0), y0), at(wrap(x0 + 1), y0), tx);
    const glm::vec3 bottom = glm::mix(at(wrap(x0), y1), at(wrap(x0 + 1), y1), tx);
    return glm::mix(top, bottom, ty);
}

EnvironmentImage make_sky(int width, int height, const SkySettings& settings) {
    EnvironmentImage sky;
    sky.width = width;
    sky.height = height;
    sky.texels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        const float up = equirect_to_direction({0.0f, (static_cast<float>(y) + 0.5f) / static_cast<float>(height)}).y;
        glm::vec3 color;
        if (up >= 0.0f) {
            color = glm::mix(settings.horizon, settings.zenith, std::pow(up, 0.6f));
        } else {
            // A quick fade from the horizon to the ground, as haze near the horizon would do.
            color = glm::mix(settings.horizon, settings.ground, std::min(1.0f, -up * 8.0f));
        }
        for (int x = 0; x < width; ++x) {
            sky.at(x, y) = color;
        }
    }
    return sky;
}

EnvironmentImage load_environment(const std::string& path) {
    std::size_t size = 0;
    void* file = SDL_LoadFile(path.c_str(), &size);
    if (file == nullptr) {
        throw std::runtime_error("Environment '" + path + "': cannot read the file: " + SDL_GetError());
    }
    int width = 0, height = 0, channels = 0;
    float* pixels = stbi_loadf_from_memory(static_cast<const stbi_uc*>(file), static_cast<int>(size), &width, &height,
                                           &channels, 3);
    SDL_free(file);
    if (pixels == nullptr) {
        throw std::runtime_error("Environment '" + path + "': cannot decode it: " + stbi_failure_reason());
    }
    EnvironmentImage image;
    image.width = width;
    image.height = height;
    image.texels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (std::size_t i = 0; i < image.texels.size(); ++i) {
        image.texels[i] = {pixels[i * 3], pixels[i * 3 + 1], pixels[i * 3 + 2]};
    }
    stbi_image_free(pixels);
    return image;
}

glm::vec3 IrradianceSH::evaluate(glm::vec3 n) const {
    float basis[9];
    sh_basis(n, basis);
    glm::vec3 result(0.0f);
    for (int i = 0; i < 9; ++i) {
        result += coefficients[i] * basis[i];
    }
    return glm::max(result, glm::vec3(0.0f));
}

IrradianceSH project_irradiance(const EnvironmentImage& environment) {
    // Radiance coefficients, integrated over the sphere (each texel weighs its solid angle)...
    glm::vec3 radiance[9] = {};
    const float texel_area = (2.0f * kPi / static_cast<float>(environment.width)) * (kPi / static_cast<float>(environment.height));
    for (int y = 0; y < environment.height; ++y) {
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(environment.height);
        const float solid_angle = texel_area * std::sin(v * kPi);
        for (int x = 0; x < environment.width; ++x) {
            const glm::vec3 direction = equirect_to_direction({(static_cast<float>(x) + 0.5f) / static_cast<float>(environment.width), v});
            float basis[9];
            sh_basis(direction, basis);
            for (int i = 0; i < 9; ++i) {
                radiance[i] += environment.at(x, y) * (basis[i] * solid_angle);
            }
        }
    }
    // ...convolved with the clamped cosine (a diffuse surface), then divided by pi so evaluate()
    // gives what a white surface reflects.
    constexpr float kBand[3] = {kPi, 2.0f * kPi / 3.0f, kPi / 4.0f};
    constexpr int kBandOf[9] = {0, 1, 1, 1, 2, 2, 2, 2, 2};
    IrradianceSH sh;
    for (int i = 0; i < 9; ++i) {
        sh.coefficients[i] = radiance[i] * (kBand[kBandOf[i]] / kPi);
    }
    return sh;
}

std::vector<EnvironmentImage> prefilter_specular(const EnvironmentImage& environment, int base_width, int levels,
                                                 int samples) {
    // A mip chain of the source: far-apart GGX samples read a blurrier level, which removes the
    // noise a few dozen samples would otherwise leave ("filtered importance sampling").
    std::vector<EnvironmentImage> source{environment};
    while (source.back().width > 1) {
        source.push_back(downsample(source.back()));
    }
    const auto sample_level = [&](glm::vec3 direction, float level) {
        const int index = std::clamp(static_cast<int>(std::lround(level)), 0, static_cast<int>(source.size()) - 1);
        return source[static_cast<std::size_t>(index)].sample(direction);
    };
    const float source_texel = 4.0f * kPi / (static_cast<float>(environment.width) * static_cast<float>(environment.height));

    std::vector<EnvironmentImage> result;
    for (int level = 0; level < levels; ++level) {
        EnvironmentImage out;
        out.width = std::max(1, base_width >> level);
        out.height = std::max(1, out.width / 2);
        out.texels.resize(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height));
        const float roughness = levels > 1 ? static_cast<float>(level) / static_cast<float>(levels - 1) : 0.0f;
        const float a = roughness * roughness;
        // The source level whose texels match this output's size, for the sharpest level.
        const float matching_level = std::max(0.0f, std::log2(static_cast<float>(environment.width) / static_cast<float>(out.width)));

        for (int y = 0; y < out.height; ++y) {
            for (int x = 0; x < out.width; ++x) {
                const glm::vec3 n = equirect_to_direction({(static_cast<float>(x) + 0.5f) / static_cast<float>(out.width),
                                                           (static_cast<float>(y) + 0.5f) / static_cast<float>(out.height)});
                if (level == 0) {
                    out.at(x, y) = sample_level(n, matching_level);  // a mirror: just the environment
                    continue;
                }
                // A basis around n; the view and reflected directions are both taken equal to n
                // (the usual "split sum" simplification).
                const glm::vec3 helper = std::abs(n.y) < 0.999f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
                const glm::vec3 tangent = glm::normalize(glm::cross(helper, n));
                const glm::vec3 bitangent = glm::cross(n, tangent);
                glm::vec3 sum(0.0f);
                float weight = 0.0f;
                for (int i = 0; i < samples; ++i) {
                    const glm::vec2 xi = hammersley(static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(samples));
                    const float phi = 2.0f * kPi * xi.x;
                    const float cos_theta = std::sqrt((1.0f - xi.y) / (1.0f + (a * a - 1.0f) * xi.y));
                    const float sin_theta = std::sqrt(std::max(0.0f, 1.0f - cos_theta * cos_theta));
                    const glm::vec3 h = glm::normalize(tangent * (sin_theta * std::cos(phi)) +
                                                       bitangent * (sin_theta * std::sin(phi)) + n * cos_theta);
                    const glm::vec3 l = 2.0f * glm::dot(n, h) * h - n;
                    const float n_dot_l = glm::dot(n, l);
                    if (n_dot_l <= 0.0f) {
                        continue;
                    }
                    // Solid angle this sample stands for, against a source texel's: which level to read.
                    const float n_dot_h = std::max(glm::dot(n, h), 0.0f);
                    const float denominator = n_dot_h * n_dot_h * (a * a - 1.0f) + 1.0f;
                    const float d = (a * a) / (kPi * denominator * denominator);
                    const float pdf = d / 4.0f;  // D * NoH / (4 * VoH), with V = N
                    const float sample_angle = 1.0f / (static_cast<float>(samples) * pdf + 1e-6f);
                    const float lod = 0.5f * std::log2(sample_angle / source_texel) + 1.0f;
                    sum += sample_level(l, std::max(lod, matching_level)) * n_dot_l;
                    weight += n_dot_l;
                }
                out.at(x, y) = weight > 0.0f ? sum / weight : glm::vec3(0.0f);
            }
        }
        result.push_back(std::move(out));
    }
    return result;
}

Environment Environment::create(Renderer& renderer, const EnvironmentImage& source, const char* name) {
    Environment environment;
    environment.irradiance = project_irradiance(source);
    const std::vector<EnvironmentImage> levels = prefilter_specular(source, kBaseWidth, kLevels);
    std::vector<std::vector<std::uint8_t>> data;
    for (const EnvironmentImage& level : levels) {
        std::vector<std::uint8_t> bytes(level.texels.size() * 4 * sizeof(std::uint16_t));
        auto* halves = reinterpret_cast<std::uint16_t*>(bytes.data());
        for (std::size_t i = 0; i < level.texels.size(); ++i) {
            halves[i * 4 + 0] = glm::packHalf1x16(level.texels[i].r);
            halves[i * 4 + 1] = glm::packHalf1x16(level.texels[i].g);
            halves[i * 4 + 2] = glm::packHalf1x16(level.texels[i].b);
            halves[i * 4 + 3] = glm::packHalf1x16(1.0f);
        }
        data.push_back(std::move(bytes));
    }
    environment.specular = renderer.create_texture_levels(SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, levels[0].width,
                                                          levels[0].height, data, name != nullptr ? name : "environment");
    environment.specular_levels = kLevels;
    return environment;
}

}  // namespace moteur
