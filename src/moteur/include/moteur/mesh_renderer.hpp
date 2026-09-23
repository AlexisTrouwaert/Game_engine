#pragma once

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#include <vector>

#include "moteur/environment.hpp"
#include "moteur/gpu_resource.hpp"
#include "moteur/material.hpp"
#include "moteur/renderer.hpp"

namespace moteur {

struct Mesh;
struct Model;

// A light that shines in every direction from a point (a torch, a spell), fading with distance
// and reaching zero at `range` (the KHR_lights_punctual falloff).
struct PointLight {
    glm::vec3 position{0.0f};
    glm::vec3 color{1.0f};   // linear
    float intensity = 1.0f;  // radiance at 1 m
    float range = 10.0f;     // metres
};

// Draws 3D meshes with physically based shading (the glTF metal / roughness model), in the
// "scene" pass, in linear HDR colors, with depth test and write.
//
// Light comes from three sources, added together:
// - the environment (image-based lighting: diffuse from spherical harmonics, specular from the
//   prefiltered environment), scaled by its intensity;
// - the sun, a directional light;
// - up to kMaxPointLights point lights, given again every frame.
//
// Like sprites, the game only *records* draws during a frame; they are executed when the frame
// ends. One draw call per recorded draw for now: grouping identical meshes (instancing) is part 9.
//
//   meshes.set_camera(camera.view_projection(), camera.position());  // every frame
//   meshes.add_light({torch_position, orange, 20.0f, 8.0f});         // every frame
//   meshes.draw(model, world_matrix);
class MeshRenderer {
public:
    static constexpr int kMaxPointLights = 32;

    // `color_format` and `depth_format`: the targets of the "scene" pass.
    MeshRenderer(Renderer& renderer, SDL_GPUTextureFormat color_format, SDL_GPUTextureFormat depth_format);

    // The camera of this frame, and where its eye is (reflections depend on it). Forgotten when the
    // frame ends: without it, recorded meshes are not drawn.
    void set_camera(const glm::mat4& view_projection, glm::vec3 eye) {
        view_projection_ = view_projection;
        eye_ = eye;
        has_camera_ = true;
    }

    // Kept from frame to frame. `to_light`: direction from the surfaces towards the sun, normalized
    // here. `color` is linear; `intensity` multiplies it (a clear midday sun is a few units).
    void set_sun(glm::vec3 to_light, glm::vec3 color, float intensity);
    // The surroundings the surfaces reflect, or none (then only the lights light them). The
    // environment must outlive its use. Kept from frame to frame.
    void set_environment(const Environment* environment, float intensity = 1.0f);

    // A point light for this frame only. Beyond kMaxPointLights, the extra lights are ignored
    // (and counted in RenderStats::dropped_lights).
    void add_light(const PointLight& light);

    // Records a mesh placed by `world` (object -> world) with its material. The mesh and the
    // material's textures must stay alive until the end of the frame.
    void draw(const Mesh& mesh, const glm::mat4& world, const Material& material);
    // Shortcut for a plain surface: a linear color, optionally multiplied by an sRGB texture.
    void draw(const Mesh& mesh, const glm::mat4& world, glm::vec4 color = glm::vec4(1.0f),
              const Texture* texture = nullptr);
    // Records every part of a model, each with its own transform and material.
    void draw(const Model& model, const glm::mat4& world);

    // Number of draws recorded so far in the current frame.
    std::size_t queued() const { return draws_.size(); }
    // True when this frame has something to draw (draws and a camera): the "scene" pass runs then.
    bool has_work() const { return has_camera_ && !draws_.empty(); }

private:
    friend class Renderer;  // drives render() and clear() at the right moments

    void render(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, RenderStats& stats);
    void clear() {
        draws_.clear();
        lights_.clear();
        dropped_lights_ = 0;
        has_camera_ = false;
    }

    struct Draw {
        const Mesh* mesh;
        glm::mat4 world;
        Material material;
    };

    GpuGraphicsPipeline single_sided_;
    GpuGraphicsPipeline double_sided_;
    GpuSampler material_sampler_;
    GpuSampler environment_sampler_;
    // 1 x 1 stand-ins for missing textures: white (color, roughness / metal, occlusion, emissive),
    // flat normal, and a black environment for when there is none.
    Texture white_;
    Texture flat_normal_;
    GpuTexture black_environment_;

    std::vector<Draw> draws_;
    std::vector<PointLight> lights_;
    int dropped_lights_ = 0;
    glm::mat4 view_projection_{1.0f};
    glm::vec3 eye_{0.0f};
    bool has_camera_ = false;
    glm::vec3 to_sun_{0.0f, 1.0f, 0.0f};
    glm::vec3 sun_color_{1.0f};
    float sun_intensity_ = 0.0f;
    const Environment* environment_ = nullptr;
    float environment_intensity_ = 1.0f;
};

}  // namespace moteur
