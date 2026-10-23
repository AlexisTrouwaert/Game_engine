#pragma once

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <functional>
#include <string>
#include <string_view>

#include "moteur/asset_cache.hpp"

namespace moteur {

struct Texture;

/// The factors and switches of a PBR surface, as glTF describes it (metal / roughness model): what
/// Material and DrawMaterial share. Colors are linear.
struct MaterialFactors {
    /// Linear RGBA, multiplied by the base color texture. Its alpha is compared to `alpha_cutoff`, or,
    /// for a `blend` material, is how opaque the surface is.
    glm::vec4 base_color{1.0f};
    float metallic = 0.0f;              ///< 0 dielectric (wood, stone, plastic), 1 metal
    float roughness = 0.5f;             ///< 0 mirror, 1 fully matte
    float normal_scale = 1.0f;          ///< strength of the normal map
    float occlusion_strength = 1.0f;    ///< 0 ignores the occlusion map
    glm::vec3 emissive{0.0f};           ///< light the surface gives off by itself (linear, can exceed 1)
    bool double_sided = false;          ///< draw back faces too (leaves, thin cloth), lit from their side
    bool casts_shadow = true;           ///< false for light sources (a flame) and effects
    bool fades = false;                 ///< a wall or tall decor: dithered away where it would hide the
                                        ///< hero (MeshRenderer::set_cutout); not for skinned meshes
    /// Alpha test (glTF alphaMode MASK): where the alpha of base_color times its texture is below this,
    /// the surface is cut out, in the scene and in every shadow (leaves, grids, torn cloth). 0: opaque.
    float alpha_cutoff = 0.0f;
    /// Transparency (glTF alphaMode BLEND; milestone 9 bis, part 2): drawn after the opaque surfaces,
    /// object by object from the farthest to the nearest, the alpha of base_color times its texture
    /// telling how much of what is behind shows through (its emitted light is weighted by it too).
    /// Lit like the others; casts no shadow; never cut out for the hero.
    /// Objects that cross each other may sort wrongly (sorted by their boxes' centres, not per pixel).
    bool blend = false;
    /// The texture coordinates move by this much per second (flowing water, lava, a magic circle).
    glm::vec2 uv_scroll{0.0f};
    /// The emitted light beats this many times per second, between 40 % and 100 % (0: steady).
    float emissive_pulse = 0.0f;
};

/// What changes one object's look for a moment, on top of its materials (milestone 9 bis, part 2):
/// a MaterialEffects component on an entity (World::collect gives it to all its meshes), or
/// MeshRenderer::set_effects() for direct draws. Part of the game's state (a monster's death in
/// progress), driven by its ticks.
struct MaterialEffects {
    /// 0 whole, 1 gone: the surface burns away in a noise pattern fixed to the object (and so does
    /// its shadow), a glowing band at the edge.
    float dissolve = 0.0f;
    glm::vec3 dissolve_edge{6.0f, 2.2f, 0.5f};  ///< the glow of the burning edge (linear, HDR)
    /// A hit: rgb the color the lit surface is pulled towards (linear, may exceed 1), a how much (0 to 1).
    glm::vec4 flash{0.0f};

    bool operator==(const MaterialEffects&) const = default;  ///< same values
};

/// What one draw of a frame uses of a Material (MeshRenderer::draw()): its factors and its textures,
/// not owned (no count of references per draw and per frame). The textures must stay alive until the
/// end of the frame: the Material they come from holds them.
struct DrawMaterial : MaterialFactors {
    const Texture* base_color_texture = nullptr;          ///< sRGB color, multiplies base_color; null: white
    const Texture* metallic_roughness_texture = nullptr;  ///< linear; roughness in green, metalness in blue; null: white
    const Texture* normal_texture = nullptr;              ///< linear, tangent space; null: a flat surface
    const Texture* occlusion_texture = nullptr;           ///< linear, occlusion in red; null: white (no occlusion)
    const Texture* emissive_texture = nullptr;            ///< sRGB color, multiplies emissive; null: white
};

/// A PBR surface, as glTF describes it (metal / roughness model). Every texture is optional: a
/// missing one counts as white (or, for the normal map, as a flat surface), so the factors alone
/// describe the surface. Colors are linear.
///
/// Textures follow the glTF conventions, and must be created accordingly (see TextureSettings):
/// - base_color_texture, emissive_texture: colors, sRGB (TextureSettings::srgb = true);
/// - metallic_roughness_texture: data, linear; roughness in green, metalness in blue;
/// - occlusion_texture: data, linear; in red (it may be the same image as the previous one, as in
///   the "ARM" textures of Poly Haven: occlusion, roughness, metal in red, green, blue);
/// - normal_texture: data, linear; tangent space, green pointing up in the image.
///
/// It **holds** its textures (milestone 9, part 4: Asset handles, shared with the texture cache or
/// the model that made them): a component with a Material keeps them alive, and a hot reload or a
/// collection of the asset cache never leaves it pointing at a freed texture. A copy shares them.
struct Material : MaterialFactors {
    Asset<Texture> base_color_texture;          ///< sRGB color, multiplies base_color; empty: white
    Asset<Texture> metallic_roughness_texture;  ///< linear; roughness in green, metalness in blue; empty: white
    Asset<Texture> normal_texture;              ///< linear, tangent space; empty: a flat surface
    Asset<Texture> occlusion_texture;           ///< linear, occlusion in red; empty: white (no occlusion)
    Asset<Texture> emissive_texture;            ///< sRGB color, multiplies emissive; empty: white

    /// The draw's view of it: its factors, its textures' addresses.
    DrawMaterial view() const {
        DrawMaterial out;
        static_cast<MaterialFactors&>(out) = *this;
        out.base_color_texture = base_color_texture.handle().get();
        out.metallic_roughness_texture = metallic_roughness_texture.handle().get();
        out.normal_texture = normal_texture.handle().get();
        out.occlusion_texture = occlusion_texture.handle().get();
        out.emissive_texture = emissive_texture.handle().get();
        return out;
    }
};

/// What a texture of a material file is for: how it is created (see TextureSettings).
enum class MaterialTexture {
    Color,   ///< base color, emissive: sRGB, mipmaps
    Data,    ///< metal and roughness, occlusion: linear, mipmaps
    Normal,  ///< a normal map: linear, mipmaps, two channels when compressed
};
/// Loads the texture a material file names (`reference`: a path, or `{"id", "path"}`) for `role`.
using MaterialTextureLoader = std::function<Asset<Texture>(const nlohmann::json& reference, MaterialTexture role)>;

/// Reads a material file (`.material.json`, FORMATS 24; milestone 9 bis, part 2): a game's material,
/// referenced by its key (Assets::material()), that can replace a model's own
/// (ModelComponent::materials). `name` is for the messages; `texture` loads the textures it names.
/// Throws std::runtime_error naming the file and the key at fault: bad JSON, an unknown key (a typo
/// is an error), a value of the wrong kind or out of range, a texture that cannot be loaded.
Material parse_material(std::string_view text, const std::string& name, const MaterialTextureLoader& texture);

}  // namespace moteur
