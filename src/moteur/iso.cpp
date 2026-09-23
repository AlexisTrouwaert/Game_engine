#include "moteur/iso.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace moteur {

IsoProjection::IsoProjection(float tile_width, float tile_height)
    : tile_width_(tile_width), tile_height_(tile_height) {
    if (!(tile_width > 0.0f) || !(tile_height > 0.0f)) {
        throw std::invalid_argument("IsoProjection: tile size must be positive");
    }
}

glm::vec2 IsoProjection::to_world(glm::vec2 tile, float height) const {
    return {(tile.x - tile.y) * tile_width_ * 0.5f, (tile.x + tile.y) * tile_height_ * 0.5f - height};
}

glm::vec2 IsoProjection::from_world(glm::vec2 world) const {
    const float a = world.x / (tile_width_ * 0.5f);   // tx - ty
    const float b = world.y / (tile_height_ * 0.5f);  // tx + ty
    return {(a + b) * 0.5f, (b - a) * 0.5f};
}

glm::ivec2 IsoProjection::tile_at(glm::vec2 world) const {
    const glm::vec2 tile = from_world(world);
    return {static_cast<int>(std::floor(tile.x)), static_cast<int>(std::floor(tile.y))};
}

glm::vec2 IsoProjection::tile_sprite_position(glm::ivec2 tile) const {
    return to_world(glm::vec2(tile)) - glm::vec2(tile_width_ * 0.5f, 0.0f);
}

glm::vec2 IsoProjection::tile_center(glm::ivec2 tile) const {
    return to_world(glm::vec2(tile) + glm::vec2(0.5f));
}

TileRange IsoProjection::tiles_in(const Rect& world_area, int margin) const {
    // A tile is inside a rectangle if its center is; the tile-space image of a rectangle is a
    // diamond, so the bounding box of its four corners is enough to contain every such tile.
    const glm::vec2 corners[4] = {
        from_world({world_area.min.x, world_area.min.y}),
        from_world({world_area.max.x, world_area.min.y}),
        from_world({world_area.min.x, world_area.max.y}),
        from_world({world_area.max.x, world_area.max.y}),
    };
    glm::vec2 low = corners[0];
    glm::vec2 high = corners[0];
    for (const glm::vec2& corner : corners) {
        low = glm::min(low, corner);
        high = glm::max(high, corner);
    }
    TileRange range;
    range.min = {static_cast<int>(std::floor(low.x)) - margin, static_cast<int>(std::floor(low.y)) - margin};
    range.max = {static_cast<int>(std::floor(high.x)) + margin, static_cast<int>(std::floor(high.y)) + margin};
    return range;
}

}  // namespace moteur
