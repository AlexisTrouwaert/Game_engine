#pragma once

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#include <cstddef>

#include "moteur/billboard_batcher.hpp"
#include "moteur/gpu_resource.hpp"
#include "moteur/renderer.hpp"

namespace moteur {

class Camera3D;

struct BillboardOptions {
    glm::vec4 color{1.0f};    // linear, rgb may exceed 1 (it glows once tone mapped); a: opacity
    bool additive = false;    // adds light instead of covering (fire, sparks, glows)
    BillboardFacing facing = BillboardFacing::Camera;
    glm::vec4 uv_rect{0.0f, 0.0f, 1.0f, 1.0f};
};

// Sprites in the 3D world (milestone 3, part 10): textured rectangles turned towards the camera,
// drawn in the "scene" pass after the meshes, in linear HDR colors. They are hidden by what is in
// front of them (depth test against the meshes) but do not hide anything themselves (no depth
// write): being transparent, they are sorted from the farthest to the nearest instead.
//
// Textures follow the sprite convention (premultiplied alpha, see TextureSettings), and hold colors,
// so create them with TextureSettings::srgb = true (and mipmaps, since billboards shrink with
// distance).
//
//   billboards.set_camera(camera);                              // every frame, the drawn camera
//   billboards.draw(glow, torch_position, {0.8f, 0.8f}, {.color = {4, 2, 1, 1}, .additive = true});
class BillboardRenderer {
public:
    BillboardRenderer(Renderer& renderer, SDL_GPUTextureFormat color_format, SDL_GPUTextureFormat depth_format);

    // The camera of this frame (the interpolated one, as for the meshes). Forgotten when the frame
    // ends: without it, nothing is drawn.
    void set_camera(const Camera3D& camera);

    // The texture must stay alive until the end of the frame.
    void draw(const Texture& texture, glm::vec3 center, glm::vec2 size, const BillboardOptions& options = {});

    std::size_t queued() const { return batcher_.count(); }
    bool has_work() const { return has_camera_ && batcher_.count() > 0; }

private:
    friend class Renderer;  // drives prepare(), render() and clear()

    void prepare(SDL_GPUCommandBuffer* commands, RenderStats& stats);
    void render(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, RenderStats& stats);
    void clear() {
        batcher_.begin();
        has_camera_ = false;
    }
    void ensure_capacity(std::size_t billboards);
    // For `samples` per pixel (MSAA): made again by the Renderer when the anti-aliasing changes.
    void create_pipeline(SDL_GPUSampleCount samples);

    Renderer& renderer_;
    SDL_GPUTextureFormat color_format_;
    SDL_GPUTextureFormat depth_format_;
    GpuShader vertex_shader_;
    GpuShader fragment_shader_;
    GpuGraphicsPipeline pipeline_;
    GpuSampler sampler_;
    GpuBuffer index_buffer_;
    GpuBuffer vertex_buffer_;
    GpuTransferBuffer transfer_buffer_;
    std::size_t capacity_ = 0;
    BillboardBatcher batcher_;
    BillboardView view_;
    glm::mat4 view_projection_{1.0f};
    bool has_camera_ = false;
};

}  // namespace moteur
