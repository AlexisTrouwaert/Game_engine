#pragma once

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

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
//       ".": ["floor"],                               // one tile per layer, from layer 0 ("-": none)
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
//
// Version 2 (milestone 7, part 8) adds, all optional:
//
//   "chunk": true,                       // a piece of map (a room), assembled by the game: no start needed
//   "objects": [                         // what is not a tile: decor, lights, effects, spawns...
//     {"id": "o1", "type": "arbre", "x": 12.5, "z": 4.25, "y": 0, "rotation": 30, "scale": 1,
//      "props": {"hauteur": 1.6}}        // the type's own properties (the game reads them)
//   ],
//   "connectors": [                      // where a chunk joins another: a cell on its edge
//     {"name": "nord", "x": 5, "z": 0, "direction": "nord", "kind": "porte"}
//   ]
//
// Objects are descriptions, not entities: the game turns them into entities when it builds the
// scene. Their identifiers are unique in the map.

// An object placed in a map (see MapData).
struct MapObject {
    std::string id;
    std::string type;
    glm::vec3 position{0.0f};  // x, y (height), z, metres
    float rotation = 0.0f;     // degrees about the vertical axis
    float scale = 1.0f;
    nlohmann::json props = nlohmann::json::object();
};

// Where a chunk joins another (see MapData).
struct MapConnector {
    enum class Direction { North, East, South, West };  // north: towards z = 0
    std::string name;
    std::string kind;
    glm::ivec2 cell{0};
    Direction direction = Direction::North;
};

// One character of a map's legend: what a cell drawn with it holds.
struct MapSymbol {
    char character = ' ';
    std::vector<TileId> tiles;  // per layer; shorter than the map's layers: empty above
    std::string point;
};

class MapData {
public:
    static constexpr int kVersion = 2;  // versions 1 and 2 are read

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

    int version() const { return version_; }
    bool chunk() const { return chunk_; }
    const std::vector<MapObject>& objects() const { return objects_; }
    const std::vector<MapConnector>& connectors() const { return connectors_; }
    // The legend, and the symbol (index in symbols()) of each cell: what an editor starts from.
    const std::vector<MapSymbol>& symbols() const { return symbols_; }
    std::uint8_t cell_symbol(glm::ivec2 cell) const {
        return cell_symbols_[static_cast<std::size_t>(cell.y * tiles_.width() + cell.x)];
    }
    // The tile types by name, as written in the file (with their region).
    const std::vector<std::string>& tile_names() const { return names_; }

    // How many times the asset was reloaded (hot reload): a scene that builds something from the
    // map (decor, grids) compares it with the revision it built from.
    std::uint64_t revision() const { return revision_; }
    void set_revision(std::uint64_t revision) { revision_ = revision; }

private:
    using Symbol = MapSymbol;

    std::string source_;
    std::string description_;
    Tileset tileset_;
    TileMap tiles_{1, 1};
    std::vector<std::string> names_;  // tile id i is names_[i - 1]
    std::vector<Symbol> symbols_;
    std::vector<std::uint8_t> cell_symbols_;  // index in symbols_, row after row
    std::map<std::string, std::vector<glm::ivec2>> points_;
    std::uint64_t revision_ = 0;
    int version_ = 1;
    bool chunk_ = false;
    std::vector<MapObject> objects_;
    std::vector<MapConnector> connectors_;
};

}  // namespace moteur
