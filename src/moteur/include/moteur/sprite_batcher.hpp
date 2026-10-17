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
};

/// A group of consecutive sprites (after sorting) that share a texture: one draw call.
struct SpriteRun {
    const void* texture;          ///< the SpriteDesc::texture of every sprite of the run
    std::uint32_t first_sprite;   ///< index of its first sprite in draw order; its vertices start at `4 * first_sprite`
    std::uint32_t count;          ///< number of sprites, at most the batcher's maximum per run
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
    std::vector<SpriteVertex> vertices_;
    std::vector<SpriteRun> runs_;

    /// Scratch memory of the sort, kept between frames so that sorting does not allocate.
    std::vector<std::uint32_t> order_;
    std::vector<std::uint32_t> keys_;
    std::vector<std::uint32_t> order_scratch_;
    std::vector<std::uint32_t> keys_scratch_;
};

}  // namespace moteur
