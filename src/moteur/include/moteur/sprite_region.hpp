#pragma once

#include <glm/glm.hpp>

namespace moteur {

struct Texture;

// A sprite as stored in an atlas: a rectangle of a texture, plus what is needed to draw it as
// if it had never been trimmed or moved.
struct SpriteRegion {
    const Texture* texture = nullptr;   // the atlas page (owned by the atlas)
    glm::vec4 uv_rect{0.0f};            // u0, v0, u1, v1 of the (trimmed) image in the page
    glm::vec2 size{0.0f};               // the trimmed image, in pixels
    glm::vec2 source_size{0.0f};        // the original image, in pixels
    glm::vec2 offset{0.0f};             // where the trimmed image was inside the original
    glm::vec2 pivot{0.0f};              // the anchor point, in pixels of the original image
};

// Where and how big to draw a region.
struct PlacedSprite {
    glm::vec2 position;  // top-left corner of the trimmed image
    glm::vec2 size;
};

// Places the region so that its pivot lands exactly on `anchor`, at the given scale. With a flip,
// the image is mirrored around the pivot's own position, so a character flipped to face the other
// way keeps its feet where they were; the offset and the pivot are mirrored together with it.
PlacedSprite place_region(const SpriteRegion& region, glm::vec2 anchor, float scale = 1.0f, bool flip_x = false,
                          bool flip_y = false);

}  // namespace moteur
