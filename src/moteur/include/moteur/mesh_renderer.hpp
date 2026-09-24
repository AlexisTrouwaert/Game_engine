#pragma once

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#include <vector>

#include "moteur/environment.hpp"
#include "moteur/gpu_resource.hpp"
#include "moteur/material.hpp"
#include "moteur/mesh_batcher.hpp"
#include "moteur/renderer.hpp"
#include "moteur/shadow.hpp"

namespace moteur {

struct Mesh;
struct Model;
class DebugLineBuffer;

// How the meshes are shown: lit (the game), or a debug view. The debug views other than the
// wireframe skip the tone mapping, so their values are seen as they are.
enum class MeshView {
    Lit,
    Wireframe,  // lit, edges of the triangles only
    Normals,    // the shading normal (normal map included), as a color: n * 0.5 + 0.5
    BaseColor,  // the surface color alone, without light
    Distance,   // distance to the camera: near is white, MeshRenderer::kDistanceViewRange metres black
};

// Debug lines the MeshRenderer adds itself (to Renderer::debug_lines()) after culling.
struct MeshDebug {
    bool bounds = false;          // the box of every mesh drawn by the camera (green)
    bool shadow_frustum = false;  // what the sun's shadow map covers (yellow)
    bool lights = false;          // the range of each point light (orange; red when it has a shadow)
};

// A light that shines in every direction from a point (a torch, a spell), fading with distance
// and reaching zero at `range` (the KHR_lights_punctual falloff).
struct PointLight {
    glm::vec3 position{0.0f};
    glm::vec3 color{1.0f};   // linear
    float intensity = 1.0f;  // radiance at 1 m
    float range = 10.0f;     // metres
    // May cast shadows. Only a few lights get one each frame (ShadowOptions::point_budget): those
    // in view nearest to the middle of the screen; the others light without shadows.
    bool casts_shadows = false;
};

// How shadows are drawn: the sun's, and the point lights' (PointLight::casts_shadows). Changing
// `resolution`, `point_budget` or `point_resolution` recreates the texture concerned.
struct ShadowOptions {
    bool enabled = true;         // the sun's shadows
    int resolution = 2048;       // texels per side of the sun's shadow map
    float normal_offset = 1.5f;  // in shadow map texels: moves the lookup off the surface ("acne")
    float depth_bias = 0.0005f;  // subtracted from the depth before the comparison
    float max_height = 6.0f;     // see ShadowSettings

    int point_budget = 4;              // point lights with a shadow per frame, at most kMaxShadowedPointLights
    int point_resolution = 512;        // texels per side of each of the six faces of a light
    float point_normal_offset = 1.5f;  // in texels, as normal_offset
    float point_depth_bias = 0.02f;    // metres
};

// Draws 3D meshes with physically based shading (the glTF metal / roughness model), in the
// "scene" pass, in linear HDR colors, with depth test and write.
//
// Light comes from three sources, added together:
// - the environment (image-based lighting: diffuse from spherical harmonics, specular from the
//   prefiltered environment), scaled by its intensity;
// - the sun, a directional light, with shadows: a "shadow" pass renders the depth of every mesh
//   seen from the sun first (fit_sun_shadow() places it), and the shading compares against it;
// - up to kMaxPointLights point lights, given again every frame.
//
// Like sprites, the game only *records* draws during a frame; they are executed when the frame
// ends. Then, for each pass (the camera's and the sun's), the draws outside its view are dropped
// (frustum culling, with each draw's box in the world) and the others are grouped by mesh and
// textures: one instanced draw call per group, whatever the colors and factors of the materials
// (they travel with each instance). Draw a floor of 400 tiles with two colors: one draw call.
//
//   meshes.set_camera(camera.view_projection(), camera.position());  // every frame
//   meshes.add_light({torch_position, orange, 20.0f, 8.0f});         // every frame
//   meshes.draw(model, world_matrix);
class MeshRenderer {
public:
    static constexpr int kMaxPointLights = 32;
    static constexpr int kMaxShadowedPointLights = 8;
    static constexpr float kDistanceViewRange = 60.0f;

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

    // Frustum culling, on by default. Off, every draw goes to both passes: for comparisons and
    // debugging only (the image must not change, only the cost).
    void set_culling(bool on) { culling_ = on; }
    bool culling() const { return culling_; }

    void set_view(MeshView view) { view_ = view; }
    MeshView view() const { return view_; }
    void set_debug(const MeshDebug& debug) { debug_ = debug; }
    const MeshDebug& debug() const { return debug_; }
    // The camera given to set_camera() this frame.
    const glm::mat4& view_projection() const { return view_projection_; }

    void set_shadows(const ShadowOptions& options) { shadow_options_ = options; }
    const ShadowOptions& shadows() const { return shadow_options_; }
    // Where the shadow map looked in the last frame (for debugging displays).
    const ShadowFrame& shadow_frame() const { return shadow_frame_; }

    // Point light shadows are kept from frame to frame while nothing they depend on changes (the
    // light, and the shadow casters in its sphere, compared draw by draw). Forget them all, for
    // example to measure the cost of drawing them every frame.
    void invalidate_point_shadows() { point_slots_.reset(point_slots_.count()); }

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

    // Records a draw whose box in the world is already known (`bounds`: the mesh's box moved by
    // `world`), for decor that does not move: saves recomputing it every frame.
    void draw(const Mesh& mesh, const glm::mat4& world, const Material& material, const Aabb& bounds);

    // Number of draws recorded so far in the current frame.
    std::size_t queued() const { return draws_.size(); }
    // True when this frame has something to draw (draws and a camera): the "scene" pass runs then.
    bool has_work() const { return has_camera_ && !draws_.empty(); }

private:
    friend class Renderer;  // drives render() and clear() at the right moments, reads the atlas layout

    // Before any render pass: culls and groups the draws of both passes (fitting the shadow map
    // first), and uploads the instances.
    void prepare(SDL_GPUCommandBuffer* commands, RenderStats& stats);
    void render(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, RenderStats& stats);
    // The "shadow" pass, before the scene: whether it runs this frame, the depth texture it draws
    // into (created or resized by prepare()), and the draws themselves.
    bool wants_shadows() const { return has_work() && shadow_options_.enabled && sun_intensity_ > 0.0f; }
    SDL_GPUTexture* shadow_map() const { return shadow_map_.get(); }
    void render_shadows(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, RenderStats& stats);
    void prepare_shadows();
    // The "point shadows" pass, after the sun's: only when a light's shadow must be drawn again.
    // The atlas keeps the other tiles (loaded, not cleared), unless it was just created.
    bool wants_point_shadows() const { return has_work() && !point_jobs_.empty() && point_renders_ > 0; }
    SDL_GPUTexture* point_shadow_atlas() const { return point_atlas_.get(); }
    bool point_shadow_atlas_is_new() const { return point_atlas_new_; }
    void render_point_shadows(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, RenderStats& stats);
    void prepare_point_shadows(RenderStats& stats);
    // After prepare(): the lines asked for by debug().
    void add_debug_lines(DebugLineBuffer& lines) const;
    void ensure_instance_capacity(std::size_t instances);
    // The pipelines of the "scene" pass, for `samples` per pixel (MSAA): made again by the Renderer
    // when the anti-aliasing changes.
    void create_scene_pipelines(SDL_GPUSampleCount samples);
    void clear() {
        draws_.clear();
        lights_.clear();
        point_jobs_.clear();
        point_renders_ = 0;
        dropped_lights_ = 0;
        has_camera_ = false;
        shadows_drawn_ = false;
    }

    GpuGraphicsPipeline single_sided_;
    GpuGraphicsPipeline double_sided_;
    GpuGraphicsPipeline wireframe_single_sided_;
    GpuGraphicsPipeline wireframe_double_sided_;
    MeshView view_ = MeshView::Lit;
    MeshDebug debug_;
    GpuGraphicsPipeline shadow_single_sided_;
    GpuGraphicsPipeline shadow_double_sided_;
    Renderer& renderer_;
    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUTextureFormat color_format_ = SDL_GPU_TEXTUREFORMAT_INVALID;  // of the "scene" pass
    SDL_GPUTextureFormat depth_format_ = SDL_GPU_TEXTUREFORMAT_INVALID;
    GpuShader vertex_shader_;
    GpuShader fragment_shader_;
    SDL_GPUTextureFormat shadow_format_ = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
    GpuTexture shadow_map_;
    int shadow_map_size_ = 0;
    GpuSampler shadow_sampler_;
    ShadowOptions shadow_options_;
    ShadowFrame shadow_frame_;
    bool shadows_drawn_ = false;  // this frame
    // Point light shadows: one atlas, a row of six tiles per shadowed light.
    GpuGraphicsPipeline point_single_sided_;
    GpuGraphicsPipeline point_double_sided_;
    GpuGraphicsPipeline point_clear_;
    GpuTexture point_atlas_;
    GpuTexture point_atlas_stand_in_;  // 1 x 1, bound while there is no atlas
    int point_tile_ = 0;               // texels per side of a tile, 0 without atlas
    int point_rows_ = 0;
    bool point_atlas_new_ = false;     // created this frame: cleared by the pass
    PointShadowSlots point_slots_;
    struct PointShadowJob {
        int light = 0;                 // in lights_
        int row = 0;                   // in the atlas
        bool render = false;           // drawn this frame
        PointShadowFaces faces;
        std::size_t first_batcher = 0; // in point_batchers_, six of them, when rendered
    };
    std::vector<PointShadowJob> point_jobs_;
    int point_renders_ = 0;
    std::vector<MeshBatcher> point_batchers_;
    std::vector<std::vector<std::uint32_t>> point_casters_;  // per selected light: casters inside its sphere
    std::size_t point_instance_base_ = 0;       // where their instances start in the instance buffer
    GpuSampler material_sampler_;
    GpuSampler environment_sampler_;
    // 1 x 1 stand-ins for missing textures: white (color, roughness / metal, occlusion, emissive),
    // flat normal, and a black environment for when there is none.
    Texture white_;
    Texture flat_normal_;
    GpuTexture black_environment_;

    std::vector<MeshDraw> draws_;
    MeshBatcher main_batcher_;
    MeshBatcher shadow_batcher_;
    // The instances of both passes, one after the other (the main pass first), sent every frame.
    GpuBuffer instance_buffer_;
    GpuTransferBuffer instance_transfer_;
    std::size_t instance_capacity_ = 0;
    std::vector<PointLight> lights_;
    int dropped_lights_ = 0;
    glm::mat4 view_projection_{1.0f};
    glm::vec3 eye_{0.0f};
    bool has_camera_ = false;
    bool culling_ = true;
    glm::vec3 to_sun_{0.0f, 1.0f, 0.0f};
    glm::vec3 sun_color_{1.0f};
    float sun_intensity_ = 0.0f;
    const Environment* environment_ = nullptr;
    float environment_intensity_ = 1.0f;
};

}  // namespace moteur
