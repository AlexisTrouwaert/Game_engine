#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace moteur {

// One corner of a sprite, laid out exactly as it is sent to the GPU.
struct SpriteVertex {
    float x, y;                     // position in pixels
    float u, v;                     // texture coordinates
    std::uint8_t r, g, b, a;        // tint, multiplied with the texture color
};
static_assert(sizeof(SpriteVertex) == 20, "SpriteVertex is uploaded as is: keep it tightly packed");

// Converts a color with components in [0, 1] to four bytes, red in the lowest byte.
std::uint32_t pack_color(glm::vec4 color);

// What the game asks for: one textured rectangle.
struct SpriteDesc {
    const void* texture = nullptr;     // identity of the texture: sprites sharing it can be drawn together
    glm::vec2 position{0.0f};          // top-left corner, in pixels
    glm::vec2 size{0.0f};              // in pixels
    glm::vec4 uv_rect{0.0f, 0.0f, 1.0f, 1.0f};  // u0, v0, u1, v1: the part of the texture to show
    std::uint32_t tint = 0xFFFFFFFFu;  // see pack_color()
    float depth = 0.0f;                // larger is drawn later, so on top
    bool flip_x = false;
    bool flip_y = false;
};

// A group of consecutive sprites (after sorting) that share a texture: one draw call.
struct SpriteRun {
    const void* texture;
    std::uint32_t first_sprite;
    std::uint32_t count;
};

// Turns the sprites recorded during a frame into what the GPU needs: vertices, and the list of
// draw calls. It is plain CPU code with no GPU dependency, so it can be unit-tested.
//
//   batcher.begin();
//   batcher.add(sprite);   // any number of times
//   batcher.finish();      // sort, build vertices, split into runs
//   upload batcher.vertices(); for each run in batcher.runs(): one draw call
class SpriteBatcher {
public:
    // A draw call uses 16-bit indices, so it can address at most 65 536 vertices: 16 384 quads.
    static constexpr std::uint32_t kMaxQuadsPerRun = 16384;

    explicit SpriteBatcher(std::uint32_t max_quads_per_run = kMaxQuadsPerRun);

    void begin();
    void add(const SpriteDesc& sprite);

    // Sorts by depth (only if the sprites were not already recorded in depth order; sprites with
    // the same depth keep the order in which they were recorded), builds four vertices per sprite
    // and splits the sprites into runs.
    void finish();

    // Measurement switch: with false, every sprite gets its own run (its own draw call).
    void set_batching(bool enabled) { batching_ = enabled; }

    std::size_t sprite_count() const { return sprites_.size(); }
    const std::vector<SpriteVertex>& vertices() const { return vertices_; }  // 4 per sprite, in draw order
    const std::vector<SpriteRun>& runs() const { return runs_; }

private:
    std::uint32_t max_quads_per_run_;
    bool batching_ = true;
    bool sorted_ = true;  // true while depths are non-decreasing in recording order
    float max_depth_ = -std::numeric_limits<float>::infinity();

    // Fills order_ so that sprites_[order_[i]] is the i-th sprite to draw (stable radix sort on depth).
    void sort_by_depth();

    std::vector<SpriteDesc> sprites_;
    std::vector<SpriteVertex> vertices_;
    std::vector<SpriteRun> runs_;

    // Scratch memory of the sort, kept between frames so that sorting does not allocate.
    std::vector<std::uint32_t> order_;
    std::vector<std::uint32_t> keys_;
    std::vector<std::uint32_t> order_scratch_;
    std::vector<std::uint32_t> keys_scratch_;
};

}  // namespace moteur
