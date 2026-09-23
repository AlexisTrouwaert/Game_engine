#include "moteur/sprite_batcher.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <utility>

namespace moteur {

namespace {

std::uint32_t to_byte(float value) {
    return static_cast<std::uint32_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

// Maps a float to an unsigned integer that sorts in the same order as the float does: flip the
// sign bit of positive numbers, and all the bits of negative ones (whose bit patterns run backwards).
std::uint32_t sortable_bits(float value) {
    const auto bits = std::bit_cast<std::uint32_t>(value);
    return (bits & 0x80000000u) != 0 ? ~bits : (bits | 0x80000000u);
}

}  // namespace

std::uint32_t pack_color(glm::vec4 color) {
    return to_byte(color.r) | (to_byte(color.g) << 8) | (to_byte(color.b) << 16) | (to_byte(color.a) << 24);
}

SpriteBatcher::SpriteBatcher(std::uint32_t max_quads_per_run)
    : max_quads_per_run_(std::max<std::uint32_t>(max_quads_per_run, 1)) {}

void SpriteBatcher::begin() {
    // clear() keeps the memory, so a steady scene does not allocate after the first frames.
    sprites_.clear();
    vertices_.clear();
    runs_.clear();
    sorted_ = true;
    max_depth_ = -std::numeric_limits<float>::infinity();
}

void SpriteBatcher::add(const SpriteDesc& sprite) {
    if (sprite.depth < max_depth_) {
        sorted_ = false;
    } else {
        max_depth_ = sprite.depth;
    }
    sprites_.push_back(sprite);
}

void SpriteBatcher::sort_by_depth() {
    const std::size_t count = sprites_.size();
    order_.resize(count);
    keys_.resize(count);
    order_scratch_.resize(count);
    keys_scratch_.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        order_[i] = static_cast<std::uint32_t>(i);
        keys_[i] = sortable_bits(sprites_[i].depth);
    }

    // Least-significant-digit radix sort, 8 bits at a time. Each pass keeps the relative order
    // of equal digits, so the whole sort is stable: sprites with the same depth stay in the
    // order they were recorded. It moves 8 bytes per sprite instead of the whole sprite.
    for (int shift = 0; shift < 32; shift += 8) {
        std::size_t offsets[256] = {};
        for (std::size_t i = 0; i < count; ++i) {
            ++offsets[(keys_[i] >> shift) & 0xFFu];
        }

        // If every sprite has the same digit, this pass would change nothing.
        bool useful = true;
        for (const std::size_t n : offsets) {
            if (n == count) {
                useful = false;
                break;
            }
        }
        if (!useful) {
            continue;
        }

        std::size_t total = 0;
        for (std::size_t& offset : offsets) {
            const std::size_t n = offset;
            offset = total;
            total += n;
        }
        for (std::size_t i = 0; i < count; ++i) {
            const std::size_t position = offsets[(keys_[i] >> shift) & 0xFFu]++;
            keys_scratch_[position] = keys_[i];
            order_scratch_[position] = order_[i];
        }
        keys_.swap(keys_scratch_);
        order_.swap(order_scratch_);
    }
}

void SpriteBatcher::finish() {
    if (!sorted_) {
        sort_by_depth();
    }

    vertices_.resize(sprites_.size() * 4);
    runs_.clear();

    for (std::size_t i = 0; i < sprites_.size(); ++i) {
        const SpriteDesc& sprite = sprites_[sorted_ ? i : order_[i]];

        float u0 = sprite.uv_rect.x, v0 = sprite.uv_rect.y, u1 = sprite.uv_rect.z, v1 = sprite.uv_rect.w;
        if (sprite.flip_x) std::swap(u0, u1);
        if (sprite.flip_y) std::swap(v0, v1);

        const float x0 = sprite.position.x, y0 = sprite.position.y;
        const float x1 = x0 + sprite.size.x, y1 = y0 + sprite.size.y;
        const auto r = static_cast<std::uint8_t>(sprite.tint & 0xFFu);
        const auto g = static_cast<std::uint8_t>((sprite.tint >> 8) & 0xFFu);
        const auto b = static_cast<std::uint8_t>((sprite.tint >> 16) & 0xFFu);
        const auto a = static_cast<std::uint8_t>((sprite.tint >> 24) & 0xFFu);

        // Corners in the order the index pattern 0, 1, 2, 0, 2, 3 expects: top left, top right,
        // bottom right, bottom left.
        SpriteVertex* quad = &vertices_[i * 4];
        quad[0] = {x0, y0, u0, v0, r, g, b, a};
        quad[1] = {x1, y0, u1, v0, r, g, b, a};
        quad[2] = {x1, y1, u1, v1, r, g, b, a};
        quad[3] = {x0, y1, u0, v1, r, g, b, a};

        const bool continues_run = batching_ && !runs_.empty() && runs_.back().texture == sprite.texture &&
                                   runs_.back().count < max_quads_per_run_;
        if (continues_run) {
            ++runs_.back().count;
        } else {
            runs_.push_back({sprite.texture, static_cast<std::uint32_t>(i), 1});
        }
    }
}

}  // namespace moteur
