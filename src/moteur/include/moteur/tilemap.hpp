#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "moteur/iso.hpp"

namespace moteur {

// Identifies a kind of tile in a Tileset. 0 means "no tile here".
using TileId = std::uint16_t;
inline constexpr TileId kNoTile = 0;

// What a kind of tile looks like and how the game treats it. The properties live here, not in
// the map, so a map stays a plain grid of small numbers.
struct TileType {
    std::string region;     // sprite name in an atlas; empty if the game draws it some other way
    bool walkable = true;   // creatures can stand on it (collision and pathfinding)
    bool opaque = false;    // blocks the line of sight (visibility)
};

// The kinds of tiles a map uses, numbered from 1.
class Tileset {
public:
    // Returns the new type's id. Throws std::length_error past 65 535 types.
    TileId add(TileType type);

    // Throws std::out_of_range for kNoTile or an id that was never added.
    const TileType& type(TileId id) const;
    std::size_t size() const { return types_.size(); }

private:
    std::vector<TileType> types_;  // id 1 is types_[0]
};

// A grid of tiles in layers (for example ground, then walls and decor). Tile (i, j) of the map is
// tile (i, j) of IsoProjection. Every layer covers the whole map; empty cells hold kNoTile.
class TileMap {
public:
    // Every cell starts empty. Throws std::invalid_argument if a size is not positive.
    TileMap(int width, int height, int layers = 1);

    int width() const { return width_; }
    int height() const { return height_; }
    int layer_count() const { return layers_; }

    bool contains(glm::ivec2 tile) const {
        return tile.x >= 0 && tile.y >= 0 && tile.x < width_ && tile.y < height_;
    }

    // Throw std::out_of_range outside the map or for a layer that does not exist.
    TileId at(int layer, glm::ivec2 tile) const;
    void set(int layer, glm::ivec2 tile, TileId id);
    // Sets every cell of a layer.
    void fill(int layer, TileId id);

    // The part of `range` that lies inside the map; empty if none. With IsoProjection::tiles_in(),
    // gives the tiles to draw.
    TileRange clip(const TileRange& range) const;

    // True if the tile is inside the map and no layer holds a tile that is not walkable.
    bool walkable(const Tileset& tileset, glm::ivec2 tile) const;

private:
    std::size_t index(int layer, glm::ivec2 tile) const;

    int width_;
    int height_;
    int layers_;
    std::vector<TileId> cells_;  // layer after layer, each row after row
};

}  // namespace moteur
