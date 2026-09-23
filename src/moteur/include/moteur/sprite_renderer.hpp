#pragma once

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>

#include "moteur/gpu_resource.hpp"
#include "moteur/renderer.hpp"
#include "moteur/sprite_batcher.hpp"
#include "moteur/sprite_region.hpp"

namespace moteur {

// Optional parameters of a sprite. The defaults show the whole texture, untinted, unflipped.
struct SpriteOptions {
    glm::vec4 uv_rect{0.0f, 0.0f, 1.0f, 1.0f};  // u0, v0, u1, v1: the part of the texture to show
    glm::vec4 tint{1.0f, 1.0f, 1.0f, 1.0f};     // multiplied with the texture; alpha fades the sprite
    float depth = 0.0f;                          // larger is drawn later, so on top
    bool flip_x = false;
    bool flip_y = false;
};

// Draws textured rectangles.
//
// During a frame the game only *records* sprites with draw(); nothing reaches the GPU yet. When
// the frame ends, the sprites are sorted by depth, their vertices are sent to the GPU in one
// upload, and consecutive sprites that share a texture are drawn with a single draw call.
//
// Sprites with the same depth are drawn in the order they were recorded. If the game already
// records sprites in depth order (or all with the default depth 0), no sorting happens at all.
class SpriteRenderer {
public:
    explicit SpriteRenderer(Renderer& renderer);

    // Records a sprite. `position` is its top-left corner and `size` its extent, both in pixels,
    // origin at the top left of the window, y pointing down. The texture must stay alive until
    // the end of the frame.
    void draw(const Texture& texture, glm::vec2 position, glm::vec2 size, const SpriteOptions& options = {});

    // Records a sprite of an atlas so that its pivot lands on `anchor`, `scale` times bigger.
    // Trimmed margins are put back, and flips (in `options`) mirror the sprite around its pivot.
    // options.uv_rect is ignored: the region gives it.
    void draw(const SpriteRegion& region, glm::vec2 anchor, float scale = 1.0f, const SpriteOptions& options = {});

    // Sets the transformation applied to the sprites of this frame, usually camera.view_projection().
    // It applies to the whole frame (the last call wins) and is forgotten when the frame ends. Without
    // it, sprites are placed directly in window pixels: origin at the top left, y pointing down.
    void set_view_projection(const glm::mat4& view_projection) {
        view_projection_ = view_projection;
        has_view_projection_ = true;
    }

    // Measurement switch: with false, every sprite gets its own draw call, as if nothing were
    // batched. The picture is identical; only the number of draw calls changes.
    void set_batching(bool enabled) { batcher_.set_batching(enabled); }

    // Number of sprites recorded so far in the current frame.
    std::size_t queued() const { return batcher_.sprite_count(); }

private:
    friend class Renderer;  // drives prepare(), render() and clear() at the right moments

    // Phase 1, before the render pass: sorts, builds the vertices and copies them to the GPU.
    void prepare(SDL_GPUCommandBuffer* commands, RenderStats& stats);
    // Phase 2, inside the render pass: issues one draw call per run.
    void render(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, std::uint32_t width,
                std::uint32_t height, RenderStats& stats);
    void clear() {
        batcher_.begin();
        has_view_projection_ = false;
    }

    // Makes the vertex buffer big enough for `sprites` sprites, keeping its size a power of two.
    void ensure_capacity(std::size_t sprites);

    Renderer& renderer_;
    GpuGraphicsPipeline pipeline_;
    GpuBuffer vertex_buffer_;             // rewritten every frame with the vertices of all sprites
    GpuTransferBuffer transfer_buffer_;   // staging area for that rewrite
    std::size_t capacity_ = 0;            // in sprites
    GpuBuffer index_buffer_;              // static: the pattern 0,1,2,0,2,3 for every quad
    GpuSampler sampler_;
    SpriteBatcher batcher_;
    glm::mat4 view_projection_{1.0f};
    bool has_view_projection_ = false;
};

}  // namespace moteur
