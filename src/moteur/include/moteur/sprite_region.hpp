#pragma once

#include <glm/glm.hpp>

namespace moteur {

struct Texture;

/// A sprite as stored in an atlas: a rectangle of a texture, plus what is needed to draw it as
/// if it had never been trimmed or moved. Usually obtained from TextureAtlas::region(), and drawn
/// with SpriteRenderer::draw(const SpriteRegion&, glm::vec2, float, const SpriteOptions&).
/// All positions are in pixels of the original image, origin at its top left, y pointing down.
struct SpriteRegion {
    /// The atlas page, owned by the TextureAtlas: valid as long as the atlas lives.
    const Texture* texture = nullptr;
    glm::vec4 uv_rect{0.0f};            ///< u0, v0, u1, v1 of the (trimmed) image in the page, texel edges
    glm::vec2 size{0.0f};               ///< the trimmed image, in pixels
    glm::vec2 source_size{0.0f};        ///< the original image, in pixels
    glm::vec2 offset{0.0f};             ///< top-left corner of the trimmed image inside the original, in pixels
    glm::vec2 pivot{0.0f};              ///< the anchor point, in pixels of the original image
};

/// Where and how big to draw a region: the result of place_region().
struct PlacedSprite {
    glm::vec2 position;  ///< top-left corner of the trimmed image, in the units of the anchor
    glm::vec2 size;      ///< the trimmed image's size times the scale
};

/// Places the region so that its pivot lands exactly on `anchor`, at the given scale. With a flip,
/// the image is mirrored around the pivot's own position, so a character flipped to face the other
/// way keeps its feet where they were; the offset and the pivot are mirrored together with it.
/// The flips only move the rectangle: mirroring the texture itself is up to the caller
/// (SpriteOptions::flip_x and flip_y do both). Pure computation, no allocation.
PlacedSprite place_region(const SpriteRegion& region, glm::vec2 anchor, float scale = 1.0f, bool flip_x = false,
                          bool flip_y = false);

}  // namespace moteur
