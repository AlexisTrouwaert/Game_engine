#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace moteur {

/// A block of tiles, both bounds included.
struct TileRange {
    glm::ivec2 min{0};   ///< the first column and row
    glm::ivec2 max{-1};  ///< the last column and row, included; empty by default
    /// True when the range holds no tile (max below min on an axis).
    bool empty() const { return max.x < min.x || max.y < min.y; }
};

/// Identifies a kind of tile in a Tileset. 0 means "no tile here".
using TileId = std::uint16_t;
/// The id of an empty cell: no tile, never a Tileset entry.
inline constexpr TileId kNoTile = 0;

/// What a kind of tile looks like and how the game treats it. The properties live here, not in
/// the map, so a map stays a plain grid of small numbers.
struct TileType {
    std::string region;     ///< sprite name in an atlas; empty if the game draws it some other way
    bool walkable = true;   ///< creatures can stand on it (collision and pathfinding)
    bool opaque = false;    ///< blocks the line of sight (visibility)
};

/// The kinds of tiles a map uses, numbered from 1.
class Tileset {
public:
    /// Adds a kind of tile. Returns the new type's id: the previous size() + 1. Throws
    /// std::length_error past 65 535 types.
    TileId add(TileType type);

    /// The type with this id. Throws std::out_of_range for kNoTile or an id that was never added.
    /// The reference stays valid until the next add().
    const TileType& type(TileId id) const;
    /// The number of types added; the valid ids are 1 to size().
    std::size_t size() const { return types_.size(); }

private:
    std::vector<TileType> types_;  ///< id 1 is types_[0]
};

/// A grid of tiles in layers (for example ground, then walls and decor): the game's grid, one cell
/// per metre of the ground (cell (i, j) covers [i, i + 1) x [j, j + 1) on x and z, as cell_at()
/// says). Every layer covers the whole map; empty cells hold kNoTile. Layers are numbered from 0.
/// The map stores ids only: it does not check them against a Tileset.
class TileMap {
public:
    /// A `width` x `height` map (in cells) of `layers` layers. Every cell starts empty. Throws
    /// std::invalid_argument if a size or the layer count is not positive.
    TileMap(int width, int height, int layers = 1);

    /// The size in cells (= metres) and the number of layers, fixed at construction.
    ///@{
    int width() const { return width_; }
    int height() const { return height_; }
    int layer_count() const { return layers_; }
    ///@}

    /// True if 0 <= x < width() and 0 <= y < height().
    bool contains(glm::ivec2 tile) const {
        return tile.x >= 0 && tile.y >= 0 && tile.x < width_ && tile.y < height_;
    }

    /// Read and write one cell of one layer. Throw std::out_of_range outside the map or for a layer
    /// that does not exist.
    ///@{
    TileId at(int layer, glm::ivec2 tile) const;
    void set(int layer, glm::ivec2 tile, TileId id);
    ///@}
    /// Sets every cell of a layer. Throws std::out_of_range for a layer that does not exist.
    void fill(int layer, TileId id);

    /// The part of `range` that lies inside the map; empty if none.
    TileRange clip(const TileRange& range) const;

    /// True if the tile is inside the map and no layer holds a tile that is not walkable (an empty
    /// cell is walkable here; NavGrid treats it as a hole). Throws std::out_of_range if the cell
    /// holds an id `tileset` does not have.
    bool walkable(const Tileset& tileset, glm::ivec2 tile) const;

private:
    std::size_t index(int layer, glm::ivec2 tile) const;

    int width_;
    int height_;
    int layers_;
    std::vector<TileId> cells_;  ///< layer after layer, each row after row
};

}  // namespace moteur
