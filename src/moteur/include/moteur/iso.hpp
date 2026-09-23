#pragma once

#include <glm/glm.hpp>

#include "moteur/camera.hpp"

namespace moteur {

// A block of tiles, both bounds included.
struct TileRange {
    glm::ivec2 min{0};
    glm::ivec2 max{-1};  // empty by default
    bool empty() const { return max.x < min.x || max.y < min.y; }
};

// The classic 2:1 isometric projection between a tile grid and the world.
//
// Tile space: tile (i, j) covers the square [i, i + 1) x [j, j + 1); x grows towards the lower
// right of the screen, y towards the lower left. Fractional coordinates are positions inside a tile.
// World space: pixels, y pointing down (see Camera2D).
//
//   world.x = (tx - ty) * tile_width / 2
//   world.y = (tx + ty) * tile_height / 2
//
// A tile is a diamond of tile_width x tile_height pixels whose top corner is at to_world(i, j).
class IsoProjection {
public:
    // Typical sizes: 64x32 or 128x64. Throws std::invalid_argument if a size is not positive.
    IsoProjection(float tile_width, float tile_height);

    float tile_width() const { return tile_width_; }
    float tile_height() const { return tile_height_; }

    // Position in the world of a point of the tile grid. `height` lifts the point above the
    // ground, in pixels: it moves up on the screen.
    glm::vec2 to_world(glm::vec2 tile, float height = 0.0f) const;

    // Inverse of to_world() for a point on the ground.
    glm::vec2 from_world(glm::vec2 world) const;

    // The tile under a world position, for example under the mouse. Negative coordinates work.
    glm::ivec2 tile_at(glm::vec2 world) const;

    // Top-left corner of the tile_width x tile_height box in which the tile's image is drawn.
    glm::vec2 tile_sprite_position(glm::ivec2 tile) const;

    // Position of the middle of the diamond.
    glm::vec2 tile_center(glm::ivec2 tile) const;

    // Every tile that may touch `world_area`, plus `margin` tiles around it. The screen rectangle
    // becomes a diamond-shaped region of the grid, so this is a rectangle of tiles that contains it.
    // Use a margin for images taller than a tile (walls, trees), which stick out above their tile.
    TileRange tiles_in(const Rect& world_area, int margin = 1) const;

private:
    float tile_width_;
    float tile_height_;
};

}  // namespace moteur
