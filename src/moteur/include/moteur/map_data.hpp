#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "moteur/tilemap.hpp"

namespace moteur {

// A map read from a file (milestone 6, part 2): a TileMap, the Tileset it uses, and named points.
// A JSON file whose map is drawn in characters, one per cell:
//
//   {
//     "version": 1,
//     "description": "Two rooms and a door",         // optional, shown by the tools
//     "tiles": {                                      // the kinds of tiles, by name
//       "floor":  {},                                 // walkable, see-through (the defaults)
//       "wall":   { "walkable": false, "opaque": true },
//       "fence":  { "walkable": false },              // blocks the way, not the sight
//       "pillar": { "walkable": false, "opaque": true, "region": "pillar" }
//     },
//     "legend": {                                     // one character = what the cell holds
//       ".": ["floor"],                               // one tile per layer, from layer 0
//       "#": ["floor", "wall"],
//       " ": [],                                      // nothing: a hole
//       "@": { "tiles": ["floor"], "point": "start" } // and a named point on that cell
//     },
//     "rows": [                                       // row 0 is z = 0, column 0 is x = 0
//       "#####",
//       "#@..#",
//       "#####"
//     ]
//   }
//
// The map has as many layers as the longest list of the legend. The cell of column i, row j is
// tile (i, j): it covers x in [i, i + 1), z in [j, j + 1) (1 m cells, milestone 3).
// Tile ids follow the alphabetical order of the tile names, from 1.
//
// The file can be written back by to_ascii(): every cell is the legend's character for what it
// holds, so two characters of the legend may not mean the same thing.
class MapData {
public:
    static constexpr int kVersion = 1;

    // Throws std::runtime_error naming `source` and, for a cell, its column and row: invalid JSON,
    // unknown version, unknown tile, legend key that is not one character, two characters meaning
    // the same, empty map, rows of different lengths, character missing from the legend.
    static MapData parse(std::string_view json_text, const std::string& source);

    const std::string& source() const { return source_; }
    const std::string& description() const { return description_; }
    const Tileset& tileset() const { return tileset_; }
    const TileMap& tiles() const { return tiles_; }
    int width() const { return tiles_.width(); }
    int height() const { return tiles_.height(); }

    // The id of the tile called `name`; kNoTile if there is none.
    TileId tile_id(const std::string& name) const;
    // The name of tile `id`; empty for kNoTile or an unknown id.
    const std::string& tile_name(TileId id) const;

    // Every named point and its cells, in reading order (row after row, left to right).
    const std::map<std::string, std::vector<glm::ivec2>>& points() const { return points_; }
    // The cells of `name`; empty if none.
    const std::vector<glm::ivec2>& points(const std::string& name) const;
    // The first cell of `name`. Throws std::out_of_range if there is none.
    glm::ivec2 point(const std::string& name) const;

    // The map drawn with the legend's characters, one line per row, each ending with '\n'.
    std::string to_ascii() const;

    // How many times the asset was reloaded (hot reload): a scene that builds something from the
    // map (decor, grids) compares it with the revision it built from.
    std::uint64_t revision() const { return revision_; }
    void set_revision(std::uint64_t revision) { revision_ = revision; }

private:
    struct Symbol {
        char character = ' ';
        std::vector<TileId> tiles;  // per layer; shorter than the map's layers: empty above
        std::string point;
    };

    std::string source_;
    std::string description_;
    Tileset tileset_;
    TileMap tiles_{1, 1};
    std::vector<std::string> names_;  // tile id i is names_[i - 1]
    std::vector<Symbol> symbols_;
    std::vector<std::uint8_t> cell_symbols_;  // index in symbols_, row after row
    std::map<std::string, std::vector<glm::ivec2>> points_;
    std::uint64_t revision_ = 0;
};

}  // namespace moteur
