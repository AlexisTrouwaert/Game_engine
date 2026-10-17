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

/// Linear radiance around a point, as an equirectangular image: x goes once around the vertical axis,
/// y from straight up (row 0) to straight down. See direction_to_equirect().
struct EnvironmentImage {
    int width = 0;   ///< in texels: once around
    int height = 0;  ///< in texels: from straight up to straight down (usually width / 2)
    std::vector<glm::vec3> texels;  ///< row after row, `width * height` of them

    /// The texel at column `x`, row `y`, both inside the image (not checked).
    glm::vec3& at(int x, int y) { return texels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)]; }
    /// The texel at column `x`, row `y`, both inside the image (not checked).
    const glm::vec3& at(int x, int y) const { return texels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)]; }
    /// Bilinear sample in a unit direction; wraps around horizontally, clamps at the poles. An
    /// empty image (a side <= 0) gives black.
    glm::vec3 sample(glm::vec3 direction) const;
};

/// Where a unit direction lands in the image, in [0, 1] x [0, 1] (u around, v from up to down);
/// equirect_to_direction() goes back. The shaders use the same formulas (mesh.frag.hlsl).
glm::vec2 direction_to_equirect(glm::vec3 direction);
/// The unit direction at `uv` of the image (u around, v from straight up at 0 to straight down at
/// 1): the inverse of direction_to_equirect().
glm::vec3 equirect_to_direction(glm::vec2 uv);

/// A sky without sun (the sun is a directional light: drawing it here too would count it twice):
/// `zenith` straight up, `horizon` at the horizon, `ground` below it, smoothly blended. Linear colors.
struct SkySettings {
    glm::vec3 zenith{0.25f, 0.45f, 0.9f};    ///< linear color straight up
    glm::vec3 horizon{0.75f, 0.8f, 0.9f};    ///< linear color at the horizon
    glm::vec3 ground{0.18f, 0.16f, 0.14f};   ///< linear color below the horizon (reached quickly, like haze)
};
/// A `width` x `height` equirectangular image of the sky `settings` describe (each row a single
/// color), to light a scene without an .hdr file. Plain CPU work.
EnvironmentImage make_sky(int width, int height, const SkySettings& settings = {});

/// An equirectangular .hdr image (Radiance format, as Poly Haven publishes them), read with
/// read_file() (the mounted archives, then the disk). Throws std::runtime_error naming the file if it
/// cannot be read or decoded.
EnvironmentImage load_environment(const std::string& path);

/// The diffuse part: the environment's irradiance as 9 spherical harmonics coefficients (order 2).
/// Smooth by nature, so 9 numbers per color are enough (Ramamoorthi and Hanrahan, 2001).
struct IrradianceSH {
    /// Bands 0 to 2, in the usual order of real harmonics (1, y, z, x, xy, yz, 3z² - 1, xz,
    /// x² - y²), already convolved with the cosine and divided by pi.
    glm::vec3 coefficients[9] = {};
    /// What a white Lambert surface facing `normal` reflects: irradiance / pi. A diffuse surface of
    /// color `albedo` reflects albedo * evaluate(normal). Never negative.
    glm::vec3 evaluate(glm::vec3 normal) const;
};
/// The IrradianceSH of `environment`, each texel weighted by its solid angle. O(texels): use a
/// small image or accept the cost at loading time.
IrradianceSH project_irradiance(const EnvironmentImage& environment);

/// The specular part: the environment blurred as a rough surface would reflect it, one image per
/// roughness step. Level k has roughness k / (levels - 1), a width of max(1, base_width >> k) and
/// half that height, so the levels form the mipmap chain of one texture. `samples`: GGX samples per
/// texel (noise vs time). Spread over the job system's threads (jobs()); the caller waits.
std::vector<EnvironmentImage> prefilter_specular(const EnvironmentImage& environment, int base_width, int levels,
                                                 int samples = 64);

/// An environment ready for the GPU: the prefiltered specular images as one float texture with its
/// mipmaps, and the diffuse harmonics. Built once, at loading time; give it to
/// MeshRenderer::set_environment(). Must be destroyed before the Renderer.
struct Environment {
    GpuTexture specular;       ///< RGBA16F, equirectangular, one mip per roughness step
    int specular_levels = 0;   ///< mip levels of `specular` (kLevels once created)
    IrradianceSH irradiance;   ///< the diffuse light
    std::size_t gpu_bytes = 0; ///< of `specular`, every level

    static constexpr int kBaseWidth = 256;  ///< of the sharpest specular level
    static constexpr int kLevels = 6;       ///< 256 x 128 down to 8 x 4

    /// Projects and prefilters `source` on the CPU (costly: prefilter_specular() with 64 samples per
    /// texel, spread over the job system; the call blocks until done), then uploads it with the renderer's pending uploads. Loading time only.
    /// `name` labels the texture for graphics debuggers ("environment" when null). Throws
    /// std::runtime_error if the GPU refuses the texture.
    static Environment create(Renderer& renderer, const EnvironmentImage& source, const char* name = nullptr);
};

}  // namespace moteur
