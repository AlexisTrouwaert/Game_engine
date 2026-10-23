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
    // Capped: a longer run could not be drawn with the renderers' 16-bit indices.
    : max_quads_per_run_(std::clamp<std::uint32_t>(max_quads_per_run, 1, kMaxQuadsPerRun)) {}

void SpriteBatcher::begin() {
    // clear() keeps the memory, so a steady scene does not allocate after the first frames.
    sprites_.clear();
    glyph_styles_.clear();
    vertices_.clear();
    glyph_vertices_.clear();
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

void SpriteBatcher::add_glyph(SpriteDesc sprite, const GlyphStyle& style) {
    sprite.glyph = static_cast<std::uint32_t>(glyph_styles_.size());
    glyph_styles_.push_back(style);
    add(sprite);
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

    vertices_.resize((sprites_.size() - glyph_styles_.size()) * 4);
    glyph_vertices_.resize(glyph_styles_.size() * 4);
    runs_.clear();

    std::uint32_t sprite_quads = 0;
    std::uint32_t glyph_quads = 0;
    for (std::size_t i = 0; i < sprites_.size(); ++i) {
        const SpriteDesc& sprite = sprites_[sorted_ ? i : order_[i]];

        float u0 = sprite.uv_rect.x, v0 = sprite.uv_rect.y, u1 = sprite.uv_rect.z, v1 = sprite.uv_rect.w;
        if (sprite.flip_x) std::swap(u0, u1);
        if (sprite.flip_y) std::swap(v0, v1);

        float x0 = sprite.position.x, y0 = sprite.position.y;
        float x1 = x0 + sprite.size.x, y1 = y0 + sprite.size.y;
        if (sprite.has_clip) {
            // Cut to the clip rectangle, the texture coordinates with it (an empty result is a
            // quad of no area: the vertex count stays).
            const float cx0 = std::clamp(x0, sprite.clip.x, sprite.clip.z), cx1 = std::clamp(x1, sprite.clip.x, sprite.clip.z);
            const float cy0 = std::clamp(y0, sprite.clip.y, sprite.clip.w), cy1 = std::clamp(y1, sprite.clip.y, sprite.clip.w);
            const float w = x1 - x0, h = y1 - y0;
            if (w > 0.0f && h > 0.0f) {
                const float nu0 = u0 + (u1 - u0) * (cx0 - x0) / w, nu1 = u0 + (u1 - u0) * (cx1 - x0) / w;
                const float nv0 = v0 + (v1 - v0) * (cy0 - y0) / h, nv1 = v0 + (v1 - v0) * (cy1 - y0) / h;
                u0 = nu0;
                u1 = nu1;
                v0 = nv0;
                v1 = nv1;
            }
            x0 = cx0;
            x1 = cx1;
            y0 = cy0;
            y1 = cy1;
        }
        const float t = sprite.top_shift, d = sprite.bottom_shift;
        const auto r = static_cast<std::uint8_t>(sprite.tint & 0xFFu);
        const auto g = static_cast<std::uint8_t>((sprite.tint >> 8) & 0xFFu);
        const auto b = static_cast<std::uint8_t>((sprite.tint >> 16) & 0xFFu);
        const auto a = static_cast<std::uint8_t>((sprite.tint >> 24) & 0xFFu);

        // Corners in the order the index pattern 0, 1, 2, 0, 2, 3 expects: top left, top right,
        // bottom right, bottom left.
        const bool glyph = sprite.glyph != SpriteDesc::kNotGlyph;
        std::uint32_t quad_index = 0;
        if (!glyph) {
            quad_index = sprite_quads++;
            SpriteVertex* quad = &vertices_[static_cast<std::size_t>(quad_index) * 4];
            quad[0] = {x0 + t, y0, u0, v0, r, g, b, a};
            quad[1] = {x1 + t, y0, u1, v0, r, g, b, a};
            quad[2] = {x1 + d, y1, u1, v1, r, g, b, a};
            quad[3] = {x0 + d, y1, u0, v1, r, g, b, a};
        } else {
            quad_index = glyph_quads++;
            const GlyphStyle& style = glyph_styles_[sprite.glyph];
            const std::uint32_t oc = style.outline_color;
            const auto o_r = static_cast<std::uint8_t>(oc & 0xFFu);
            const auto o_g = static_cast<std::uint8_t>((oc >> 8) & 0xFFu);
            const auto o_b = static_cast<std::uint8_t>((oc >> 16) & 0xFFu);
            const auto o_a = static_cast<std::uint8_t>((oc >> 24) & 0xFFu);
            GlyphVertex* quad = &glyph_vertices_[static_cast<std::size_t>(quad_index) * 4];
            const float dl = style.dilate, ol = style.outline, sf = style.softness;
            quad[0] = {x0 + t, y0, u0, v0, r, g, b, a, o_r, o_g, o_b, o_a, dl, ol, sf};
            quad[1] = {x1 + t, y0, u1, v0, r, g, b, a, o_r, o_g, o_b, o_a, dl, ol, sf};
            quad[2] = {x1 + d, y1, u1, v1, r, g, b, a, o_r, o_g, o_b, o_a, dl, ol, sf};
            quad[3] = {x0 + d, y1, u0, v1, r, g, b, a, o_r, o_g, o_b, o_a, dl, ol, sf};
        }

        const bool continues_run = batching_ && !runs_.empty() && runs_.back().texture == sprite.texture &&
                                   runs_.back().glyphs == glyph && runs_.back().count < max_quads_per_run_;
        if (continues_run) {
            ++runs_.back().count;
        } else {
            runs_.push_back({sprite.texture, quad_index, 1, glyph});
        }
    }
}

}  // namespace moteur
