#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <string>
#include <vector>

#include "moteur/gpu_resource.hpp"

namespace moteur {

class Renderer;

// Image-based lighting (milestone 3, part 7): the light that reaches a surface from every direction
// of the surroundings (sky, walls), not just from the lights. PBR surfaces need it: a metal
// reflects its surroundings, and nothing is ever lit by its lights alone.
//
// The CPU side (this header, except Environment) is plain data and pure functions, unit-tested.

// Linear radiance around a point, as an equirectangular image: x goes once around the vertical axis,
// y from straight up (row 0) to straight down. See direction_to_equirect().
struct EnvironmentImage {
    int width = 0;
    int height = 0;
    std::vector<glm::vec3> texels;  // row after row

    glm::vec3& at(int x, int y) { return texels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)]; }
    const glm::vec3& at(int x, int y) const { return texels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)]; }
    // Bilinear sample in a unit direction; wraps around horizontally.
    glm::vec3 sample(glm::vec3 direction) const;
};

// Where a unit direction lands in the image, in [0, 1] x [0, 1] (u around, v from up to down), and
// back. The shaders use the same formulas (mesh.frag.hlsl).
glm::vec2 direction_to_equirect(glm::vec3 direction);
glm::vec3 equirect_to_direction(glm::vec2 uv);

// A sky without sun (the sun is a directional light: drawing it here too would count it twice):
// `zenith` straight up, `horizon` at the horizon, `ground` below it, smoothly blended. Linear colors.
struct SkySettings {
    glm::vec3 zenith{0.25f, 0.45f, 0.9f};
    glm::vec3 horizon{0.75f, 0.8f, 0.9f};
    glm::vec3 ground{0.18f, 0.16f, 0.14f};
};
EnvironmentImage make_sky(int width, int height, const SkySettings& settings = {});

// An equirectangular .hdr image (Radiance format, as Poly Haven publishes them). Throws
// std::runtime_error naming the file if it cannot be read.
EnvironmentImage load_environment(const std::string& path);

// The diffuse part: the environment's irradiance as 9 spherical harmonics coefficients (order 2).
// Smooth by nature, so 9 numbers per color are enough (Ramamoorthi and Hanrahan, 2001).
struct IrradianceSH {
    glm::vec3 coefficients[9] = {};
    // What a white Lambert surface facing `normal` reflects: irradiance / pi. A diffuse surface of
    // color `albedo` reflects albedo * evaluate(normal).
    glm::vec3 evaluate(glm::vec3 normal) const;
};
IrradianceSH project_irradiance(const EnvironmentImage& environment);

// The specular part: the environment blurred as a rough surface would reflect it, one image per
// roughness step. Level k has roughness k / (levels - 1) and half the size of level k - 1, so the
// levels form the mipmap chain of one texture. `samples`: GGX samples per texel (noise vs time).
std::vector<EnvironmentImage> prefilter_specular(const EnvironmentImage& environment, int base_width, int levels,
                                                 int samples = 64);

// An environment ready for the GPU: the prefiltered specular images as one float texture with its
// mipmaps, and the diffuse harmonics. Built once, at loading time.
struct Environment {
    GpuTexture specular;       // RGBA16F, equirectangular, one mip per roughness step
    int specular_levels = 0;
    IrradianceSH irradiance;
    std::size_t gpu_bytes = 0;

    static constexpr int kBaseWidth = 256;  // of the sharpest specular level
    static constexpr int kLevels = 6;       // 256 x 128 down to 8 x 4

    // Prefilters `source` and uploads it (waits for the GPU: loading time only).
    static Environment create(Renderer& renderer, const EnvironmentImage& source, const char* name = nullptr);
};

}  // namespace moteur
