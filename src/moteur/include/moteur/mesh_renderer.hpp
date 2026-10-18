#pragma once

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#include <vector>

#include "moteur/environment.hpp"
#include "moteur/gpu_resource.hpp"
#include "moteur/jobs.hpp"
#include "moteur/material.hpp"
#include "moteur/mesh_batcher.hpp"
#include "moteur/renderer.hpp"
#include "moteur/shadow.hpp"

namespace moteur {

struct Mesh;
struct Model;
class DebugLineBuffer;

/// How the meshes are shown: lit (the game), or a debug view. The debug views other than the
/// wireframe skip the tone mapping, so their values are seen as they are.
enum class MeshView {
    Lit,        ///< the game: lit, shadowed, tone mapped
    Wireframe,  ///< lit, edges of the triangles only
    Normals,    ///< the shading normal (normal map included), as a color: n * 0.5 + 0.5
    BaseColor,  ///< the surface color alone, without light
    Distance,   ///< distance to the camera: near is white, MeshRenderer::kDistanceViewRange metres black
    Weights,    ///< skinned meshes: the weight of one palette entry (set_weight_joint), blue 0 to red 1;
                ///< meshes without skin dark grey
};

/// Debug lines the MeshRenderer adds itself (to Renderer::debug_lines()) after culling.
struct MeshDebug {
    bool bounds = false;          ///< the box of every mesh drawn by the camera (green)
    bool shadow_frustum = false;  ///< what the sun's shadow map covers (yellow)
    bool lights = false;          ///< the range of each point light (orange; red when it has a shadow)
};

/// A light that shines in every direction from a point (a torch, a spell), fading with distance
/// and reaching zero at `range` (the KHR_lights_punctual falloff).
struct PointLight {
    glm::vec3 position{0.0f};  ///< world, metres
    glm::vec3 color{1.0f};   ///< linear
    float intensity = 1.0f;  ///< radiance at 1 m
    float range = 10.0f;     ///< metres; a light with a range of 0 or less never gets a shadow
    /// May cast shadows. Only a few lights get one each frame (ShadowOptions::point_budget): those
    /// in view nearest to the middle of the screen; the others light without shadows.
    bool casts_shadows = false;
    /// Metres: what is nearer to the light than this casts no shadow from it (the lantern, glass and
    /// frame around a flame: without it, their shadow darkens everything below). 0: automatic,
    /// max(0.02, range / 200). Measured along each face of the shadow cube, so a housing of half
    /// width w around the light needs a little more than w.
    float shadow_near = 0.0f;
};

/// The fog of war (milestone 6, part 8), applied to the meshes and the billboards: what was never
/// seen is dark, what was seen but is out of sight is dimmed and greyed. The game gives a value per
/// cell (MeshRenderer::set_fog_cells); the GPU filters between cells, for soft edges.
struct FogOfWar {
    bool enabled = false;       ///< off: no fog at all; on, it shows once set_fog_cells() was called
    glm::vec2 origin{0.0f};     ///< world (x, z) of the corner of cell (0, 0)
    float cell_size = 1.0f;     ///< metres
    float unexplored = 0.0f;    ///< brightness of what was never seen
    float explored = 0.3f;      ///< brightness of what was seen, out of sight
    float saturation = 0.35f;   ///< color kept out of sight (0 grey, 1 full)
};

/// Walls that would hide the hero (milestone 6, part 8): the meshes whose Material::fades is set
/// lose a dithered disc of pixels around the line from the camera to `focus`, in front of it only.
struct Cutout {
    bool enabled = false;   ///< off: nothing fades
    glm::vec3 focus{0.0f};  ///< world, the middle of the hero's body
    float radius = 1.6f;    ///< metres around the line of sight
    /// In a level of several floors (milestone 9, part 7): what fades (Material::fades) higher than
    /// `ceiling` metres above the focus, within `ceiling_radius` metres of it on the plane, is cut
    /// away (the floors and roofs above the hero), with a dithered edge. 0: off.
    float ceiling = 0.0f;
    float ceiling_radius = 12.0f;  ///< metres, see #ceiling
};

/// How shadows are drawn: the sun's, and the point lights' (PointLight::casts_shadows). Changing
/// `resolution`, `point_budget` or `point_resolution` recreates the texture concerned (at the next
/// frame). The player's caps (MeshRenderer::set_shadow_caps()) may lower the last three.
struct ShadowOptions {
    bool enabled = true;         ///< the sun's shadows
    int resolution = 2048;       ///< texels per side of the sun's shadow map, clamped to [256, 8192]
    float normal_offset = 1.5f;  ///< in shadow map texels: moves the lookup off the surface ("acne")
    float depth_bias = 0.0005f;  ///< subtracted from the depth (in [0, 1]) before the comparison
    float max_height = 6.0f;     ///< see ShadowSettings

    int point_budget = 4;              ///< point lights with a shadow per frame, clamped to [0, kMaxShadowedPointLights]
    int point_resolution = 512;        ///< texels per side of each of the six faces of a light, clamped to [64, 2048]
    float point_normal_offset = 1.5f;  ///< in texels, as normal_offset
    float point_depth_bias = 0.02f;    ///< metres
};

/// Draws 3D meshes with physically based shading (the glTF metal / roughness model), in the
/// "scene" pass, in linear HDR colors, with depth test and write.
///
/// Light comes from three sources, added together:
/// - the environment (image-based lighting: diffuse from spherical harmonics, specular from the
///   prefiltered environment), scaled by its intensity;
/// - the sun, a directional light, with shadows: a "shadow" pass renders the depth of every mesh
///   seen from the sun first (fit_sun_shadow() places it), and the shading compares against it;
/// - up to kMaxPointLights point lights, given again every frame.
///
/// Like sprites, the game only *records* draws during a frame; they are executed when the frame
/// ends. Then, for each pass (the camera's and the sun's), the draws outside its view are dropped
/// (frustum culling, with each draw's box in the world) and the others are grouped by mesh and
/// textures: one instanced draw call per group, whatever the colors and factors of the materials
/// (they travel with each instance). Draw a floor of 400 tiles with two colors: one draw call.
///
/// ```
/// meshes.set_camera(camera.view_projection(), camera.position());  // every frame
/// meshes.add_light({torch_position, orange, 20.0f, 8.0f});         // every frame
/// meshes.draw(model, world_matrix);
/// ```
///
/// The Renderer owns the one MeshRenderer (Renderer::meshes()); every call is for the main thread.
/// What a frame records (draws, lights, palettes, the camera) is forgotten when it ends; the
/// settings (sun, environment, shadows, fog, cutout, view) are kept from frame to frame.
class MeshRenderer {
public:
    static constexpr int kMaxPointLights = 32;          ///< point lights per frame; add_light() ignores the others
    static constexpr int kMaxShadowedPointLights = 8;   ///< the most ShadowOptions::point_budget can give
    static constexpr float kDistanceViewRange = 60.0f;  ///< metres: the distance shown black in MeshView::Distance

    /// `color_format` and `depth_format`: the targets of the "scene" pass. Made by the Renderer
    /// itself: loads the mesh shaders and creates the pipelines, samplers and stand-in textures;
    /// throws std::runtime_error if one of them fails.
    MeshRenderer(Renderer& renderer, SDL_GPUTextureFormat color_format, SDL_GPUTextureFormat depth_format);

    /// The camera of this frame, and where its eye is (reflections depend on it). Forgotten when the
    /// frame ends: without it, recorded meshes are not drawn.
    void set_camera(const glm::mat4& view_projection, glm::vec3 eye) {
        view_projection_ = view_projection;
        eye_ = eye;
        has_camera_ = true;
    }

    /// Kept from frame to frame. `to_light`: direction from the surfaces towards the sun, normalized
    /// here (zero: straight up). `color` is linear; `intensity` multiplies it (a clear midday sun is
    /// a few units). An intensity of 0 or less turns the sun off, and its shadow pass with it. No
    /// sun until this is called.
    void set_sun(glm::vec3 to_light, glm::vec3 color, float intensity);
    /// The surroundings the surfaces reflect, or none (then only the lights light them). The
    /// environment must outlive its use: until another call replaces it, or the last frame drawn
    /// with it ends. `intensity` multiplies its light. Kept from frame to frame.
    void set_environment(const Environment* environment, float intensity = 1.0f);

    /// Frustum culling, on by default. Off, every draw goes to both passes: for comparisons and
    /// debugging only (the image must not change, only the cost).
    void set_culling(bool on) { culling_ = on; }
    /// Levels of detail (milestone 9, part 4): each mesh with several (Mesh::lods) is drawn, in every
    /// pass, at the coarsest level whose error, seen from this frame's camera, is at most `pixels`
    /// on the screen. 1 by default; 0 always draws the full meshes (to compare). Kept.
    void set_lod_threshold(float pixels) { lod_threshold_ = pixels; }
    /// See set_lod_threshold().
    float lod_threshold() const { return lod_threshold_; }
    /// Whether frustum culling is on (set_culling()).
    bool culling() const { return culling_; }

    /// How the meshes are shown from the next frame on (MeshView::Lit by default). Kept.
    void set_view(MeshView view) { view_ = view; }
    /// For MeshView::Weights: the palette entry whose weights are shown (an index in a skin's joints,
    /// SkinData::joints; -1: none). The same entry for every skinned mesh: meant for one character,
    /// or characters sharing a skin layout (the KayKit ones).
    void set_weight_joint(int palette_index) { weight_joint_ = palette_index; }
    /// The palette entry given to set_weight_joint(); -1 by default.
    int weight_joint() const { return weight_joint_; }
    /// The view given to set_view().
    MeshView view() const { return view_; }
    /// Which debug lines the renderer adds itself (none by default). Kept.
    void set_debug(const MeshDebug& debug) { debug_ = debug; }
    /// The debug lines asked for with set_debug().
    const MeshDebug& debug() const { return debug_; }
    /// The camera given to set_camera() this frame.
    const glm::mat4& view_projection() const { return view_projection_; }

    /// How shadows are drawn, from the next frame on. Kept.
    void set_shadows(const ShadowOptions& options) { shadow_options_ = options; }

    /// Kept from frame to frame. Values given by set_fog_cells().
    void set_fog(const FogOfWar& fog) { fog_ = fog; }
    /// The fog given to set_fog().
    const FogOfWar& fog() const { return fog_; }
    /// One byte per cell, row after row (row 0 at z = origin.y): 0 never seen, 128 explored, 255 in
    /// sight. Copied, and sent with the next frame. A new size creates the texture, filled with
    /// these cells through the renderer's pending uploads (no wait): rare, a new map. Throws
    /// std::invalid_argument when `cells` does not hold `width * height` bytes or a size is not
    /// positive.
    void set_fog_cells(int width, int height, const std::vector<std::uint8_t>& cells);
    /// Walls fading before the hero, from the next frame on. Kept; give a new focus every frame.
    void set_cutout(const Cutout& cutout) { cutout_ = cutout; }
    /// The cutout given to set_cutout().
    const Cutout& cutout() const { return cutout_; }

    /// The id pass (milestone 9, part 7): after the scene, the visible pixel of every selectable
    /// thing is written with its pick id into a texture of its own (Renderer::request_pick() reads
    /// one pixel of it back, a frame later; the outline of Renderer::set_outline() follows it). Off
    /// by default: it draws the scene's meshes a second time (depth and id only). Kept.
    void set_ids(bool enabled) { ids_ = enabled; }
    /// Whether the id pass is on (set_ids()).
    bool ids() const { return ids_; }
    /// The pick id of the draws that follow, until the next call (World::submit() gives each
    /// entity's); cleared every frame. See MeshDraw::pick_id.
    void set_pick_id(std::uint32_t id) { pick_id_ = id; }
    /// For the billboards: the fog texture (a stand-in when there is none) and its two uniform rows.
    /// The texture and the sampler stay owned by the MeshRenderer: valid until the next
    /// set_fog_cells() of a new size.
    SDL_GPUTexture* fog_texture() const { return fog_texture_ ? fog_texture_.get() : fog_stand_in_.get(); }
    /// The sampler to read fog_texture() with (linear filtering, clamped).
    SDL_GPUSampler* fog_sampler() const { return fog_sampler_.get(); }
    /// The fog's two uniform rows, as the mesh and billboard shaders read them: `fog` = (on, never
    /// seen brightness, explored brightness, saturation), `rect` = (origin x, origin z, 1 / width,
    /// 1 / depth), both sizes in metres.
    void fog_uniforms(glm::vec4& fog, glm::vec4& rect) const;
    /// The options given to set_shadows() (before the player's caps).
    const ShadowOptions& shadows() const { return shadow_options_; }
    /// The player's caps on what set_shadows() asks for (DisplaySettings::shadows): the game keeps
    /// its options, the quality only lowers them. Defaults: no cap (8192, 8 and 2048, the largest
    /// values allowed anyway).
    void set_shadow_caps(int resolution, int point_budget, int point_resolution) {
        cap_resolution_ = resolution;
        cap_point_budget_ = point_budget;
        cap_point_resolution_ = point_resolution;
    }
    /// Where the shadow map looked in the last frame that drew the sun's shadows (for debugging
    /// displays).
    const ShadowFrame& shadow_frame() const { return shadow_frame_; }

    /// Point light shadows are kept from frame to frame while nothing they depend on changes (the
    /// light, and the shadow casters in its sphere, compared draw by draw). Forget them all, for
    /// example to measure the cost of drawing them every frame.
    void invalidate_point_shadows() { point_slots_.reset(point_slots_.count()); }

    /// A point light for this frame only (copied). Beyond kMaxPointLights, the extra lights are
    /// ignored (and counted in RenderStats::dropped_lights).
    void add_light(const PointLight& light);

    /// Records a mesh placed by `world` (object -> world) with its material (copied). The mesh and
    /// the material's textures must stay alive until the end of the frame. Computes the box in the
    /// world from Mesh::bounds. A skinned mesh drawn this way shows its bind pose.
    void draw(const Mesh& mesh, const glm::mat4& world, const Material& material);
    /// Shortcut for a plain surface: a linear color, optionally multiplied by an sRGB texture; the
    /// other material values are Material's defaults.
    void draw(const Mesh& mesh, const glm::mat4& world, glm::vec4 color = glm::vec4(1.0f),
              const Texture* texture = nullptr);
    /// Records every part of a model, each with its own transform (`world` times Model::Part's) and
    /// material (a default Material for a part without one). The model must stay alive until the end
    /// of the frame. Skinned parts show their bind pose: animate them with the draw that takes a
    /// Palette.
    void draw(const Model& model, const glm::mat4& world);

    /// Records a draw whose box in the world is already known (`bounds`: the mesh's box moved by
    /// `world`), for decor that does not move: saves recomputing it every frame.
    void draw(const Mesh& mesh, const glm::mat4& world, const Material& material, const Aabb& bounds);

    /// Skinning (milestone 5): the matrices of one pose for this frame (PoseSampler::palette: model
    /// matrix x inverse bind matrix of each joint of a skin), to give to the draws of the meshes
    /// that follow it. Sent to the GPU once, whatever the number of meshes using them.
    struct Palette {
        std::int32_t first = -1;  ///< index of its first matrix among the frame's; -1: none
        std::uint32_t size = 0;   ///< matrices
    };
    /// Copies `matrices` (only their three first rows: the last is always 0, 0, 0, 1) among this
    /// frame's palettes and returns where they are. Valid for this frame only.
    Palette add_palette(const std::vector<glm::mat4>& matrices);
    /// Records a skinned mesh (Mesh::skinned()): its vertices follow `palette`, then `world` (the
    /// entity's place). `bounds`: where it is in the world in this pose. A mesh without skin, or an
    /// empty Palette (`first` = -1), is drawn as a still mesh.
    void draw(const Mesh& mesh, const glm::mat4& world, const Material& material, const Aabb& bounds, Palette palette);
    /// Matrices recorded so far this frame.
    std::size_t palette_matrices() const { return palette_rows_.size() / 3; }

    /// Number of draws recorded so far in the current frame.
    std::size_t queued() const { return draws_.size(); }
    /// True when this frame has something to draw (draws and a camera): the "scene" pass runs then.
    bool has_work() const { return has_camera_ && !draws_.empty(); }

private:
    friend class Renderer;  ///< drives render() and clear() at the right moments, reads the atlas layout
    friend class ThumbnailRenderer;  ///< drives a MeshRenderer of its own, off screen

    /// Before any render pass: culls and groups the draws of both passes (fitting the shadow map
    /// first), and uploads the instances.
    void prepare(SDL_GPUCommandBuffer* commands, RenderStats& stats);
    void render(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, RenderStats& stats);
    /// The "shadow" pass, before the scene: whether it runs this frame, the depth texture it draws
    /// into (created or resized by prepare()), and the draws themselves.
    bool wants_shadows() const { return has_work() && shadow_options_.enabled && sun_intensity_ > 0.0f; }
    SDL_GPUTexture* shadow_map() const { return shadow_map_.get(); }
    void render_shadows(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, RenderStats& stats);
    /// The id pass (see set_ids()): the scene's batches again, into a R32_UINT target and a depth of
    /// its own (single-sampled). Pipelines made at the first call.
    void render_ids(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, RenderStats& stats);
    void prepare_shadows();
    void select_lods();  ///< MeshDraw::lod of every draw, from the camera (see set_lod_threshold())
    /// The "point shadows" pass, after the sun's: only when a light's shadow must be drawn again.
    /// The atlas keeps the other tiles (loaded, not cleared), unless it was just created.
    bool wants_point_shadows() const { return has_work() && !point_jobs_.empty() && point_renders_ > 0; }
    SDL_GPUTexture* point_shadow_atlas() const { return point_atlas_.get(); }
    bool point_shadow_atlas_is_new() const { return point_atlas_new_; }
    void render_point_shadows(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, RenderStats& stats);
    /// The point lights whose shadows are drawn this frame (indices in lights_), their atlas made:
    /// the main thread's part of the point shadows.
    std::vector<int> select_point_shadow_lights();
    /// What the shadow of the k-th of them depends on (its casters into point_casters_[k]): a task.
    std::uint64_t point_shadow_signature(int light_index, std::size_t k);
    /// Their atlas rows and the faces to draw again (their batches built in parallel).
    void finish_point_shadows(const std::vector<int>& lights, const std::vector<std::uint64_t>& signatures);
    /// After prepare(): the lines asked for by debug().
    void add_debug_lines(DebugLineBuffer& lines) const;
    void ensure_instance_capacity(std::size_t instances);
    void ensure_palette_capacity(std::size_t rows);
    /// Binds what the batch's pipeline reads: its vertices (and skin), its slice of the instances,
    /// the palettes for a skinned one, and its indices; then draws it.
    void draw_batch(SDL_GPURenderPass* pass, const MeshBatch& batch, std::size_t first_instance);
    /// The base color texture of an alpha-tested material, for its shadow (fragment sampler 0).
    void bind_cutout_texture(SDL_GPURenderPass* pass, const DrawMaterial& material);
    /// The pipelines of the "scene" pass, for `samples` per pixel (MSAA): made again by the Renderer
    /// when the anti-aliasing changes.
    void create_scene_pipelines(SDL_GPUSampleCount samples);
    void clear() {
        pick_id_ = 0;
        draws_.clear();
        lights_.clear();
        point_jobs_.clear();
        point_renders_ = 0;
        dropped_lights_ = 0;
        has_camera_ = false;
        shadows_drawn_ = false;
        palette_rows_.clear();
    }

    GpuGraphicsPipeline single_sided_;
    GpuGraphicsPipeline double_sided_;
    GpuGraphicsPipeline wireframe_single_sided_;
    GpuGraphicsPipeline wireframe_double_sided_;
    MeshView view_ = MeshView::Lit;
    int weight_joint_ = -1;
    MeshDebug debug_;
    GpuGraphicsPipeline shadow_single_sided_;
    GpuGraphicsPipeline shadow_double_sided_;
    /// The same for skinned meshes (mesh_skinned.vert.hlsl and the like).
    GpuGraphicsPipeline skinned_single_sided_;
    GpuGraphicsPipeline skinned_double_sided_;
    GpuGraphicsPipeline skinned_wireframe_single_sided_;
    GpuGraphicsPipeline skinned_wireframe_double_sided_;
    GpuGraphicsPipeline shadow_skinned_single_sided_;
    GpuGraphicsPipeline shadow_skinned_double_sided_;
    GpuGraphicsPipeline point_skinned_single_sided_;
    GpuGraphicsPipeline point_skinned_double_sided_;
    /// Alpha-tested materials (review of milestones 1 to 7, 3.1): their shadows sample the base color
    /// and drop what the cutoff removes (shadow_masked*.hlsl). [skinned][double sided].
    GpuGraphicsPipeline shadow_masked_[2][2];
    GpuGraphicsPipeline point_masked_[2][2];
    Renderer& renderer_;
    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUTextureFormat color_format_ = SDL_GPU_TEXTUREFORMAT_INVALID;  ///< of the "scene" pass
    SDL_GPUTextureFormat depth_format_ = SDL_GPU_TEXTUREFORMAT_INVALID;
    GpuShader vertex_shader_;
    GpuShader skinned_vertex_shader_;
    GpuShader fragment_shader_;
    bool ids_ = false;
    TaskGraph frame_tasks_;  ///< the preparation's tasks (milestone 9, part 9), kept from frame to frame
    std::uint32_t pick_id_ = 0;
    GpuGraphicsPipeline id_pipelines_[2][2];  ///< [skinned][double sided]
    SDL_GPUTextureFormat shadow_format_ = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
    GpuTexture shadow_map_;
    int shadow_map_size_ = 0;
    GpuSampler shadow_sampler_;
    ShadowOptions shadow_options_;
    int cap_resolution_ = 8192;
    int cap_point_budget_ = 8;
    int cap_point_resolution_ = 2048;
    ShadowFrame shadow_frame_;
    bool shadows_drawn_ = false;  ///< this frame
    /// Point light shadows: one atlas, a row of six tiles per shadowed light.
    GpuGraphicsPipeline point_single_sided_;
    GpuGraphicsPipeline point_double_sided_;
    GpuGraphicsPipeline point_clear_;
    GpuTexture point_atlas_;
    GpuTexture point_atlas_stand_in_;  ///< 1 x 1, bound while there is no atlas
    int point_tile_ = 0;               ///< texels per side of a tile, 0 without atlas
    int point_rows_ = 0;
    bool point_atlas_new_ = false;     ///< created this frame: cleared by the pass
    PointShadowSlots point_slots_;
    struct PointShadowJob {
        int light = 0;                 ///< in lights_
        int row = 0;                   ///< in the atlas
        bool render = false;           ///< drawn this frame
        PointShadowFaces faces;
        std::size_t first_batcher = 0; ///< in point_batchers_, six of them, when rendered
    };
    std::vector<PointShadowJob> point_jobs_;
    int point_renders_ = 0;
    std::vector<MeshBatcher> point_batchers_;
    std::vector<std::vector<std::uint32_t>> point_casters_;  ///< per selected light: casters inside its sphere
    std::size_t point_instance_base_ = 0;       ///< where their instances start in the instance buffer
    GpuSampler material_sampler_;
    GpuSampler environment_sampler_;
    /// 1 x 1 stand-ins for missing textures: white (color, roughness / metal, occlusion, emissive),
    /// flat normal, and a black environment for when there is none.
    Texture white_;
    Texture flat_normal_;
    GpuTexture black_environment_;
    /// The fog of war: its texture (R8, a texel per cell), a 1 x 1 stand-in, and the bytes to send.
    FogOfWar fog_;
    Cutout cutout_;
    GpuTexture fog_texture_;
    GpuTexture fog_stand_in_;
    GpuSampler fog_sampler_;
    GpuTransferBuffer fog_transfer_;
    glm::ivec2 fog_size_{0};
    std::vector<std::uint8_t> fog_cells_;
    bool fog_dirty_ = false;

    std::vector<MeshDraw> draws_;
    MeshBatcher main_batcher_;
    MeshBatcher shadow_batcher_;
    /// The instances of both passes, one after the other (the main pass first), sent every frame.
    GpuBuffer instance_buffer_;
    GpuTransferBuffer instance_transfer_;
    std::size_t instance_capacity_ = 0;
    /// The palettes of the frame: three rows (float4) per matrix, sent every frame into a storage
    /// buffer that the skinned vertex shaders read (skinning.hlsli).
    std::vector<glm::vec4> palette_rows_;
    GpuBuffer palette_buffer_;
    GpuTransferBuffer palette_transfer_;
    std::size_t palette_capacity_ = 0;  ///< rows
    std::vector<PointLight> lights_;
    int dropped_lights_ = 0;
    glm::mat4 view_projection_{1.0f};
    glm::vec3 eye_{0.0f};
    bool has_camera_ = false;
    bool culling_ = true;
    float lod_threshold_ = 1.0f;  ///< see set_lod_threshold()
    glm::vec3 to_sun_{0.0f, 1.0f, 0.0f};
    glm::vec3 sun_color_{1.0f};
    float sun_intensity_ = 0.0f;
    const Environment* environment_ = nullptr;
    float environment_intensity_ = 1.0f;
};

}  // namespace moteur
