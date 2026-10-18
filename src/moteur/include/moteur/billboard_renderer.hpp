#pragma once

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#include <cstddef>
#include <memory>

#include "moteur/billboard_batcher.hpp"
#include "moteur/gpu_resource.hpp"
#include "moteur/renderer.hpp"

namespace moteur {

class Camera3D;

/// Optional parameters of a billboard. The defaults show the whole texture, white, opaque, facing the camera.
struct BillboardOptions {
    glm::vec4 color{1.0f};    ///< linear, rgb may exceed 1 (it glows once tone mapped); a: opacity
    bool additive = false;    ///< adds light instead of covering (fire, sparks, glows)
    BillboardFacing facing = BillboardFacing::Camera;  ///< which way it turns
    glm::vec4 uv_rect{0.0f, 0.0f, 1.0f, 1.0f};         ///< u0, v0, u1, v1: the part of the texture to show
    float rotation = 0.0f;    ///< radians, in its own plane
    /// Soft particle (milestone 7): fades over this many metres as it nears the surface behind it
    /// (smoke on the ground, fire against a wall). 0: a hard edge, and nothing more to compute.
    /// Negative values count as 0.
    float soft = 0.0f;
};

/// Sprites in the 3D world (milestone 3, part 10): textured rectangles turned towards the camera,
/// drawn in the "scene" pass after the meshes, in linear HDR colors. They are hidden by what is in
/// front of them (depth test against the meshes) but do not hide anything themselves (no depth
/// write): being transparent, they are sorted from the farthest to the nearest instead.
///
/// Textures follow the sprite convention (premultiplied alpha, see TextureSettings), and hold colors,
/// so create them with TextureSettings::srgb = true (and mipmaps, since billboards shrink with
/// distance). The Renderer owns the instance (Renderer::billboards()) and drives it; a game records
/// billboards from the thread that runs the frame, between Renderer::begin_frame() and
/// Renderer::end_frame(). Particle effects (ParticleSystem) are drawn through it.
///
/// ```
/// billboards.set_camera(camera);                              // every frame, the drawn camera
/// billboards.draw(glow, torch_position, {0.8f, 0.8f}, {.color = {4, 2, 1, 1}, .additive = true});
/// ```
class BillboardRenderer {
public:
    /// `color_format` and `depth_format`: the targets of the "scene" pass it draws in. Created by
    /// the Renderer; a game has no reason to make one. Throws std::runtime_error if a GPU sampler
    /// or the pipeline cannot be created.
    BillboardRenderer(Renderer& renderer, SDL_GPUTextureFormat color_format, SDL_GPUTextureFormat depth_format);

    /// The camera of this frame (the interpolated one, as for the meshes). Forgotten when the frame
    /// ends: without it, nothing is drawn. The camera is read during the call (its matrix and
    /// axes are copied). Billboards are aligned with the screen axes derived from its forward
    /// direction and the world's up (+y): always defined, since Camera3D keeps its pitch within
    /// [1, 89] degrees (it never looks straight down).
    void set_camera(const Camera3D& camera);

    /// Records a billboard centered on `center` (world, metres), `size` metres wide and tall.
    /// The texture must stay alive until the end of the frame. Only copies a few values (no GPU
    /// work until the frame ends).
    void draw(const Texture& texture, glm::vec3 center, glm::vec2 size, const BillboardOptions& options = {});

    /// A soft round glow (white, radial falloff, premultiplied, mipmapped), made once by the
    /// renderer: the texture of the halos of lights (LightHalo), free for the game's glows too.
    const Texture& glow_texture() const { return *glow_; }
    /// Whether set_camera() was called this frame.
    bool has_camera() const { return has_camera_; }
    /// The position of this frame's camera (set_camera()).
    glm::vec3 eye() const { return view_.eye; }

    /// Number of billboards recorded so far in the current frame.
    std::size_t queued() const { return batcher_.count(); }
    /// True if this frame has something to draw: a camera and at least one billboard.
    bool has_work() const { return has_camera_ && batcher_.count() > 0; }

private:
    friend class Renderer;  ///< drives prepare(), render() and clear()

    void prepare(SDL_GPUCommandBuffer* commands, RenderStats& stats);
    void render(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, RenderStats& stats);
    void clear() {
        batcher_.begin();
        has_camera_ = false;
    }
    void ensure_capacity(std::size_t billboards);
    /// For `samples` per pixel (MSAA): made again by the Renderer when the anti-aliasing changes.
    void create_pipeline(SDL_GPUSampleCount samples);

    Renderer& renderer_;
    SDL_GPUTextureFormat color_format_;
    SDL_GPUTextureFormat depth_format_;
    GpuShader vertex_shader_;
    GpuShader fragment_shader_;
    GpuGraphicsPipeline pipeline_;
    GpuSampler sampler_;
    GpuSampler depth_sampler_;  ///< the scene's distances: the nearest texel
    GpuBuffer index_buffer_;
    GpuBuffer vertex_buffer_;
    GpuTransferBuffer transfer_buffer_;
    std::size_t capacity_ = 0;
    BillboardBatcher batcher_;
    BillboardView view_;
    glm::mat4 view_projection_{1.0f};
    bool has_camera_ = false;
    std::unique_ptr<Texture> glow_;  ///< see glow_texture()
};

}  // namespace moteur
