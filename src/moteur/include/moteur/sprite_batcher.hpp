#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace moteur {

/// One corner of a sprite, laid out exactly as it is sent to the GPU.
struct SpriteVertex {
    /// Position, in pixels (or in the units of the view-projection given to the SpriteRenderer).
    ///@{
    float x, y;
    ///@}
    /// Texture coordinates, 0 to 1 across the texture.
    ///@{
    float u, v;
    ///@}
    /// Tint, straight alpha, 0 to 255: the shader premultiplies it, then multiplies the texture color by it.
    ///@{
    std::uint8_t r, g, b, a;
    ///@}
};
static_assert(sizeof(SpriteVertex) == 20, "SpriteVertex is uploaded as is: keep it tightly packed");

/// One corner of a glyph (text drawn from the distance-field atlas, milestone 9 bis part 4), as it
/// is sent to the GPU.
struct GlyphVertex {
    /// Position and atlas coordinates, as SpriteVertex.
    ///@{
    float x, y;
    float u, v;
    ///@}
    /// Fill color then outline color, straight alpha, 0 to 255.
    ///@{
    std::uint8_t r, g, b, a;
    std::uint8_t outline_r, outline_g, outline_b, outline_a;
    ///@}
    /// In pixels on screen: the dilation (bold), the outline width, the edge's softness (1: just
    /// antialiased; more for a shadow).
    ///@{
    float dilate, outline, softness;
    ///@}
};
static_assert(sizeof(GlyphVertex) == 36, "GlyphVertex is uploaded as is: keep it tightly packed");

/// How a glyph looks (SpriteBatcher::add_glyph()).
struct GlyphStyle {
    std::uint32_t outline_color = 0;  ///< packed by pack_color(); alpha 0: no outline
    float dilate = 0.0f;    ///< pixels: synthetic bold
    float outline = 0.0f;   ///< pixels
    float softness = 1.0f;  ///< pixels over which the edge fades
};

/// Converts a color with components in [0, 1] to four bytes, red in the lowest byte, alpha in the
/// highest. Components are clamped to [0, 1] and rounded to the nearest of the 256 steps.
std::uint32_t pack_color(glm::vec4 color);

/// What the game asks for: one textured rectangle.
struct SpriteDesc {
    const void* texture = nullptr;     ///< identity of the texture: sprites sharing it can be drawn together
    glm::vec2 position{0.0f};          ///< top-left corner, in pixels
    glm::vec2 size{0.0f};              ///< in pixels
    glm::vec4 uv_rect{0.0f, 0.0f, 1.0f, 1.0f};  ///< u0, v0, u1, v1: the part of the texture to show
    std::uint32_t tint = 0xFFFFFFFFu;  ///< straight-alpha color, packed by pack_color(); opaque white by default
    float depth = 0.0f;                ///< larger is drawn later, so on top
    bool flip_x = false;               ///< mirrors the texture left to right (swaps u0 and u1); the rectangle stays
    bool flip_y = false;               ///< mirrors the texture top to bottom (swaps v0 and v1); the rectangle stays
    /// Horizontal shifts of the top and bottom edges (a slanted glyph), in pixels.
    ///@{
    float top_shift = 0.0f;
    float bottom_shift = 0.0f;
    ///@}
    /// Only the part inside this rectangle is drawn (x0, y0, x1, y1, in the sprite's units), the
    /// texture coordinates cut with it: a scrolling list of the interface.
    glm::vec4 clip{0.0f};
    bool has_clip = false;  ///< false: the whole sprite is drawn (`clip` is ignored)
    static constexpr std::uint32_t kNotGlyph = 0xFFFFFFFFu;  ///< SpriteDesc::glyph of a sprite
    std::uint32_t glyph = kNotGlyph;   ///< index of its GlyphStyle (add_glyph()), or kNotGlyph for a sprite
};

/// A group of consecutive sprites (after sorting) that share a texture and a kind: one draw call.
struct SpriteRun {
    const void* texture;          ///< the SpriteDesc::texture of every sprite of the run
    /// Index of its first quad among the quads of its kind (vertices() or glyph_vertices()): its
    /// vertices start at `4 * first_sprite` there.
    std::uint32_t first_sprite;
    std::uint32_t count;          ///< number of sprites, at most the batcher's maximum per run
    bool glyphs = false;          ///< glyphs (the distance-field pipeline) rather than sprites
};

/// Turns the sprites recorded during a frame into what the GPU needs: vertices, and the list of
/// draw calls. It is plain CPU code with no GPU dependency, so it can be unit-tested.
///
/// ```
/// batcher.begin();
/// batcher.add(sprite);   // any number of times
/// batcher.finish();      // sort, build vertices, split into runs
/// upload batcher.vertices(); for each run in batcher.runs(): one draw call
/// ```
class SpriteBatcher {
public:
    /// A draw call uses 16-bit indices, so it can address at most 65 536 vertices: 16 384 quads.
    static constexpr std::uint32_t kMaxQuadsPerRun = 16384;

    /// `max_quads_per_run`: a run longer than this is split. Clamped to [1, kMaxQuadsPerRun], so
    /// every run can be drawn with 16-bit indices; smaller values are for tests.
    explicit SpriteBatcher(std::uint32_t max_quads_per_run = kMaxQuadsPerRun);

    /// Starts a new frame: forgets the sprites, vertices and runs of the previous one. Keeps the
    /// memory, so a steady scene stops allocating after its first frames.
    void begin();
    /// Records a sprite (copied). O(1), amortized; notes whether depths are still in order.
    void add(const SpriteDesc& sprite);
    /// Records a glyph: a quad of the glyph atlas drawn as a distance field (sprite.glyph is set here).
    void add_glyph(SpriteDesc sprite, const GlyphStyle& style);

    /// Sorts by depth (only if the sprites were not already recorded in depth order; sprites with
    /// the same depth keep the order in which they were recorded), builds four vertices per sprite
    /// and splits the sprites into runs. The sort is a stable radix sort, O(n), with no allocation
    /// once the scratch memory has grown to the frame's size. Call it once per frame, after the last add().
    void finish();

    /// Measurement switch: with false, every sprite gets its own run (its own draw call).
    void set_batching(bool enabled) { batching_ = enabled; }

    /// Number of sprites recorded since begin().
    std::size_t sprite_count() const { return sprites_.size(); }
    /// The vertices built by finish(): 4 per sprite (top left, top right, bottom right, bottom
    /// left), in draw order. Valid until the next begin().
    const std::vector<SpriteVertex>& vertices() const { return vertices_; }
    /// The glyphs' vertices built by finish(): 4 per glyph, in draw order. Valid until the next begin().
    const std::vector<GlyphVertex>& glyph_vertices() const { return glyph_vertices_; }
    /// Number of glyphs among the sprites.
    std::size_t glyph_count() const { return glyph_styles_.size(); }
    /// The draw calls built by finish(), in draw order. Valid until the next begin().
    const std::vector<SpriteRun>& runs() const { return runs_; }

private:
    std::uint32_t max_quads_per_run_;
    bool batching_ = true;
    bool sorted_ = true;  ///< true while depths are non-decreasing in recording order
    float max_depth_ = -std::numeric_limits<float>::infinity();

    /// Fills order_ so that sprites_[order_[i]] is the i-th sprite to draw (stable radix sort on depth).
    void sort_by_depth();

    std::vector<SpriteDesc> sprites_;
    std::vector<GlyphStyle> glyph_styles_;
    std::vector<SpriteVertex> vertices_;
    std::vector<GlyphVertex> glyph_vertices_;
    std::vector<SpriteRun> runs_;

    /// Scratch memory of the sort, kept between frames so that sorting does not allocate.
    std::vector<std::uint32_t> order_;
    std::vector<std::uint32_t> keys_;
    std::vector<std::uint32_t> order_scratch_;
    std::vector<std::uint32_t> keys_scratch_;
};

}  // namespace moteur
