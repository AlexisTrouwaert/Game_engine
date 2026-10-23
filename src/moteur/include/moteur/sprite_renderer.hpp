#pragma once

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "moteur/gpu_resource.hpp"
#include "moteur/renderer.hpp"
#include "moteur/sprite_batcher.hpp"
#include "moteur/sprite_region.hpp"

namespace moteur {

/// Optional parameters of a sprite. The defaults show the whole texture, untinted, unflipped.
struct SpriteOptions {
    glm::vec4 uv_rect{0.0f, 0.0f, 1.0f, 1.0f};  ///< u0, v0, u1, v1: the part of the texture to show
    /// Multiplied with the texture; alpha fades the sprite. Components in [0, 1] (clamped), straight
    /// (non-premultiplied) alpha, stored in 8 bits each.
    glm::vec4 tint{1.0f, 1.0f, 1.0f, 1.0f};
    float depth = 0.0f;                          ///< larger is drawn later, so on top
    bool flip_x = false;                         ///< mirrors the sprite left to right
    bool flip_y = false;                         ///< mirrors the sprite top to bottom
    /// Drawn only inside this rectangle (x0, y0, x1, y1), when set: a scrolling list's content.
    std::optional<glm::vec4> clip;
};

/// How Font draws a glyph (SpriteRenderer::draw_glyph()). Colors straight alpha, in [0, 1].
struct GlyphLook {
    glm::vec4 color{1.0f};          ///< the fill
    glm::vec4 outline_color{0.0f};  ///< alpha 0: no outline
    float dilate = 0.0f;            ///< pixels: synthetic bold
    float outline = 0.0f;           ///< pixels
    float softness = 1.0f;          ///< pixels over which the edge fades (1: antialiasing; a shadow's blur)
};

/// Draws textured rectangles. The Renderer owns its instances (Renderer::sprites() and
/// Renderer::screen_sprites()) and drives them; a game only records sprites, from the thread that
/// runs the frame, between Renderer::begin_frame() and Renderer::end_frame().
///
/// Textures are sampled with nearest filtering (pixel art keeps its hard edges) and blended as
/// premultiplied alpha (see TextureSettings::premultiply).
///
/// During a frame the game only *records* sprites with draw(); nothing reaches the GPU yet. When
/// the frame ends, the sprites are sorted by depth, their vertices are sent to the GPU in one
/// upload, and consecutive sprites that share a texture are drawn with a single draw call.
///
/// Sprites with the same depth are drawn in the order they were recorded. If the game already
/// records sprites in depth order (or all with the default depth 0), no sorting happens at all.
class SpriteRenderer {
public:
    /// `depth_format`: the depth texture of the render pass this renderer draws in, or
    /// SDL_GPU_TEXTUREFORMAT_INVALID for a pass without one. Sprites never test or write depth; the
    /// pipeline only has to declare what the pass holds. `name` prefixes the GPU resources' debug
    /// names (`sprite` gives `sprite pipeline`, `sprite.vertices`...). Created by the Renderer;
    /// a game has no reason to make one. Throws std::runtime_error if the GPU pipeline cannot be created.
    SpriteRenderer(Renderer& renderer, SDL_GPUTextureFormat depth_format, std::string name);

    /// Records a sprite. `position` is its top-left corner and `size` its extent, both in pixels,
    /// origin at the top left of the window, y pointing down. The texture must stay alive until
    /// the end of the frame. Only copies a few values (no GPU work until the frame ends).
    void draw(const Texture& texture, glm::vec2 position, glm::vec2 size, const SpriteOptions& options = {});

    /// Records a sprite of an atlas so that its pivot lands on `anchor`, `scale` times bigger.
    /// Trimmed margins are put back, and flips (in `options`) mirror the sprite around its pivot.
    /// options.uv_rect is ignored: the region gives it. The region's atlas must stay alive until
    /// the end of the frame. See place_region().
    void draw(const SpriteRegion& region, glm::vec2 anchor, float scale = 1.0f, const SpriteOptions& options = {});

    /// Records a glyph of the distance-field atlas (Font::draw() calls it): the quad `position`,
    /// `size` showing `uv` of `page`, its top and bottom edges shifted by `top_shift` and
    /// `bottom_shift` pixels (italic). Drawn with the glyph pipeline (linear filtering, the
    /// multi-channel distance, outline and softness in the shader), in depth order with the sprites.
    void draw_glyph(const Texture& page, glm::vec2 position, glm::vec2 size, glm::vec4 uv, const GlyphLook& look,
                    float depth = 0.0f, float top_shift = 0.0f, float bottom_shift = 0.0f,
                    const std::optional<glm::vec4>& clip = std::nullopt);

    /// Sets the transformation applied to the sprites of this frame, usually a camera's view-projection
    /// matrix (for example Camera3D::view_projection(), to draw sprites in world units).
    /// It applies to the whole frame (the last call wins) and is forgotten when the frame ends. Without
    /// it, sprites are placed directly in window pixels: origin at the top left, y pointing down.
    void set_view_projection(const glm::mat4& view_projection) {
        view_projection_ = view_projection;
        has_view_projection_ = true;
    }

    /// Measurement switch: with false, every sprite gets its own draw call, as if nothing were
    /// batched. The picture is identical; only the number of draw calls changes.
    void set_batching(bool enabled) { batcher_.set_batching(enabled); }

    /// Number of sprites recorded so far in the current frame.
    std::size_t queued() const { return batcher_.sprite_count(); }

private:
    friend class Renderer;  ///< drives prepare(), render() and clear() at the right moments

    /// Phase 1, before the render pass: sorts, builds the vertices and copies them to the GPU.
    void prepare(SDL_GPUCommandBuffer* commands, RenderStats& stats);
    /// Phase 2, inside the render pass: issues one draw call per run.
    void render(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, std::uint32_t width,
                std::uint32_t height, RenderStats& stats);
    void clear() {
        batcher_.begin();
        has_view_projection_ = false;
    }

    /// Makes the vertex buffer big enough for `sprites` sprites, keeping its size a power of two.
    void ensure_capacity(std::size_t sprites);
    void ensure_glyph_capacity(std::size_t glyphs);

    Renderer& renderer_;
    std::string name_;
    GpuGraphicsPipeline pipeline_;
    GpuBuffer vertex_buffer_;             ///< rewritten every frame with the vertices of all sprites
    GpuTransferBuffer transfer_buffer_;   ///< staging area for that rewrite
    std::size_t capacity_ = 0;            ///< in sprites
    GpuBuffer index_buffer_;              ///< static: the pattern 0,1,2,0,2,3 for every quad
    GpuSampler sampler_;
    // The glyphs (milestone 9 bis, part 4): their own pipeline, vertices and filtering.
    GpuGraphicsPipeline glyph_pipeline_;
    GpuBuffer glyph_vertex_buffer_;
    GpuTransferBuffer glyph_transfer_buffer_;
    std::size_t glyph_capacity_ = 0;  ///< in glyphs
    GpuSampler glyph_sampler_;
    SpriteBatcher batcher_;
    glm::mat4 view_projection_{1.0f};
    bool has_view_projection_ = false;
};

}  // namespace moteur
