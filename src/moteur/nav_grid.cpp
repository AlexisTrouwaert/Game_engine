#include "moteur/nav_grid.hpp"

#include <stdexcept>

namespace moteur {

NavGrid::NavGrid(const TileMap& map, const Tileset& tileset)
    : width_(map.width()),
      height_(map.height()),
      cells_(static_cast<std::size_t>(map.width()) * static_cast<std::size_t>(map.height())) {
    for (int j = 0; j < height_; ++j) {
        for (int i = 0; i < width_; ++i) {
            cells_[index({i, j})] = read(map, tileset, {i, j});
        }
    }
}

std::uint8_t NavGrid::read(const TileMap& map, const Tileset& tileset, glm::ivec2 cell) {
    bool any = false;
    bool walkable = true;
    bool opaque = false;
    for (int layer = 0; layer < map.layer_count(); ++layer) {
        const TileId id = map.at(layer, cell);
        if (id == kNoTile) {
            continue;
        }
        const TileType& type = tileset.type(id);
        any = true;
        walkable = walkable && type.walkable;
        opaque = opaque || type.opaque;
    }
    return static_cast<std::uint8_t>((any && walkable ? kWalkable : 0) | (opaque ? kOpaque : 0));
}

bool NavGrid::store(glm::ivec2 cell, std::uint8_t flags) {
    if (!contains(cell)) {
        throw std::out_of_range("NavGrid: cell (" + std::to_string(cell.x) + ", " + std::to_string(cell.y) +
                                ") outside the grid");
    }
    std::uint8_t& current = cells_[index(cell)];
    if (current == flags) {
        return false;
    }
    current = flags;
    ++version_;
    return true;
}

bool NavGrid::refresh(const TileMap& map, const Tileset& tileset, glm::ivec2 cell) {
    if (!contains(cell)) {
        store(cell, 0);  // throws
    }
    return store(cell, read(map, tileset, cell));
}

bool NavGrid::set(glm::ivec2 cell, bool walkable, bool opaque) {
    return store(cell, static_cast<std::uint8_t>((walkable ? kWalkable : 0) | (opaque ? kOpaque : 0)));
}

std::string NavGrid::to_ascii() const {
    std::string text;
    text.reserve(static_cast<std::size_t>((width_ + 1) * height_));
    for (int j = 0; j < height_; ++j) {
        for (int i = 0; i < width_; ++i) {
            const bool w = walkable({i, j});
            const bool o = opaque({i, j});
            text += w ? (o ? '%' : '.') : (o ? '#' : 'x');
        }
        text += '\n';
    }
    return text;
}

}  // namespace moteur
