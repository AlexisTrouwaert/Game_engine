#include "moteur/tilemap.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace moteur {

TileId Tileset::add(TileType type) {
    if (types_.size() >= std::numeric_limits<TileId>::max()) {
        throw std::length_error("a tileset holds at most 65535 tile types");
    }
    types_.push_back(std::move(type));
    return static_cast<TileId>(types_.size());
}

const TileType& Tileset::type(TileId id) const {
    if (id == kNoTile || id > types_.size()) {
        throw std::out_of_range("no tile type with id " + std::to_string(id) + " (the tileset has " +
                                std::to_string(types_.size()) + ")");
    }
    return types_[id - 1u];
}

TileMap::TileMap(int width, int height, int layers) : width_(width), height_(height), layers_(layers) {
    if (width <= 0 || height <= 0 || layers <= 0) {
        throw std::invalid_argument("a tile map needs a positive size and layer count, got " + std::to_string(width) +
                                    "x" + std::to_string(height) + " with " + std::to_string(layers) + " layers");
    }
    cells_.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * static_cast<std::size_t>(layers),
                  kNoTile);
}

std::size_t TileMap::index(int layer, glm::ivec2 tile) const {
    if (layer < 0 || layer >= layers_ || !contains(tile)) {
        throw std::out_of_range("tile (" + std::to_string(tile.x) + ", " + std::to_string(tile.y) + ") of layer " +
                                std::to_string(layer) + " is outside the " + std::to_string(width_) + "x" +
                                std::to_string(height_) + " map with " + std::to_string(layers_) + " layers");
    }
    return (static_cast<std::size_t>(layer) * static_cast<std::size_t>(height_) + static_cast<std::size_t>(tile.y)) *
               static_cast<std::size_t>(width_) +
           static_cast<std::size_t>(tile.x);
}

TileId TileMap::at(int layer, glm::ivec2 tile) const {
    return cells_[index(layer, tile)];
}

void TileMap::set(int layer, glm::ivec2 tile, TileId id) {
    cells_[index(layer, tile)] = id;
}

void TileMap::fill(int layer, TileId id) {
    const std::size_t first = index(layer, {0, 0});
    std::fill_n(cells_.begin() + static_cast<std::ptrdiff_t>(first),
                static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), id);
}

TileRange TileMap::clip(const TileRange& range) const {
    // An empty range stays empty: narrowing its bounds cannot make max catch up with min.
    TileRange result;
    result.min = glm::max(range.min, glm::ivec2(0));
    result.max = glm::min(range.max, glm::ivec2(width_ - 1, height_ - 1));
    return result;
}

bool TileMap::walkable(const Tileset& tileset, glm::ivec2 tile) const {
    if (!contains(tile)) {
        return false;
    }
    for (int layer = 0; layer < layers_; ++layer) {
        const TileId id = cells_[index(layer, tile)];
        if (id != kNoTile && !tileset.type(id).walkable) {
            return false;
        }
    }
    return true;
}

}  // namespace moteur
