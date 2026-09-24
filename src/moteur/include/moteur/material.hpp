#pragma once

#include <glm/glm.hpp>

namespace moteur {

struct Texture;

// A PBR surface, as glTF describes it (metal / roughness model). Every texture is optional: a
// missing one counts as white (or, for the normal map, as a flat surface), so the factors alone
// describe the surface. Colors are linear.
//
// Textures follow the glTF conventions, and must be created accordingly (see TextureSettings):
// - base_color_texture, emissive_texture: colors, sRGB (TextureSettings::srgb = true);
// - metallic_roughness_texture: data, linear; roughness in green, metalness in blue;
// - occlusion_texture: data, linear; in red (it may be the same image as the previous one, as in
//   the "ARM" textures of Poly Haven: occlusion, roughness, metal in red, green, blue);
// - normal_texture: data, linear; tangent space, green pointing up in the image.
struct Material {
    glm::vec4 base_color{1.0f};
    float metallic = 0.0f;              // 0 dielectric (wood, stone, plastic), 1 metal
    float roughness = 0.5f;             // 0 mirror, 1 fully matte
    float normal_scale = 1.0f;          // strength of the normal map
    float occlusion_strength = 1.0f;    // 0 ignores the occlusion map
    glm::vec3 emissive{0.0f};           // light the surface gives off by itself (linear, can exceed 1)
    bool double_sided = false;          // draw back faces too (leaves, thin cloth), lit from their side
    bool casts_shadow = true;           // false for light sources (a flame) and effects

    const Texture* base_color_texture = nullptr;
    const Texture* metallic_roughness_texture = nullptr;
    const Texture* normal_texture = nullptr;
    const Texture* occlusion_texture = nullptr;
    const Texture* emissive_texture = nullptr;
};

}  // namespace moteur
