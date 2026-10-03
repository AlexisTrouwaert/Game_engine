#pragma once

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "moteur/tilemap.hpp"

namespace moteur {

// The cell that holds a point of the ground plane (x, z): cell (i, j) covers [i, i + 1) x [j, j + 1).
inline glm::ivec2 cell_at(glm::vec2 point) {
    return {static_cast<int>(std::floor(point.x)), static_cast<int>(std::floor(point.y))};
}

// The middle of a cell, on the ground plane (x, z).
inline glm::vec2 cell_centre(glm::ivec2 cell) {
    return {static_cast<float>(cell.x) + 0.5f, static_cast<float>(cell.y) + 0.5f};
}

// The game grid (milestone 6, part 2): for each cell of a TileMap, whether creatures can stand on
// it and whether it blocks the line of sight, all layers combined. Collisions, pathfinding and
// visibility read this rather than the tiles. Computed once from the map; a changed cell (a door
// opening, a wall falling) is read again with refresh() or set directly with set(), which changes
// version(): paths and fields of view computed on an older version must be computed again.
//
// A cell is walkable if it holds at least one tile and none of its tiles is unwalkable: a cell with
// no tile at all is a hole (unlike TileMap::walkable, which only looks for blocking tiles). It is
// opaque if one of its tiles is. Outside the grid, nothing is walkable and everything is opaque.
class NavGrid {
public:
    NavGrid() = default;  // 0 x 0
    NavGrid(const TileMap& map, const Tileset& tileset);

    int width() const { return width_; }
    int height() const { return height_; }
    bool contains(glm::ivec2 cell) const {
        return cell.x >= 0 && cell.y >= 0 && cell.x < width_ && cell.y < height_;
    }

    bool walkable(glm::ivec2 cell) const { return contains(cell) && (cells_[index(cell)] & kWalkable) != 0; }
    bool opaque(glm::ivec2 cell) const { return !contains(cell) || (cells_[index(cell)] & kOpaque) != 0; }

    // Reads the cell again from the map. Returns true, and changes version(), if it changed.
    // Throws std::out_of_range outside the grid.
    bool refresh(const TileMap& map, const Tileset& tileset, glm::ivec2 cell);
    // Sets the cell. Returns true, and changes version(), if it changed. Throws std::out_of_range
    // outside the grid.
    bool set(glm::ivec2 cell, bool walkable, bool opaque);

    // Starts at 0 and grows by one at each change.
    std::uint64_t version() const { return version_; }

    // The grid in characters, one line per row (row 0 = z 0), each ending with '\n':
    // '.' walkable, '#' blocked and opaque, 'x' blocked but see-through (a fence, a hole),
    // '%' walkable but opaque (tall grass).
    std::string to_ascii() const;

private:
    static constexpr std::uint8_t kWalkable = 1;
    static constexpr std::uint8_t kOpaque = 2;

    std::size_t index(glm::ivec2 cell) const {
        return static_cast<std::size_t>(cell.y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(cell.x);
    }
    static std::uint8_t read(const TileMap& map, const Tileset& tileset, glm::ivec2 cell);
    bool store(glm::ivec2 cell, std::uint8_t flags);

    int width_ = 0;
    int height_ = 0;
    std::vector<std::uint8_t> cells_;
    std::uint64_t version_ = 0;
};

}  // namespace moteur
