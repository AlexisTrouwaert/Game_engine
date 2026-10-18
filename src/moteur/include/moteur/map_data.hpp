#pragma once

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "moteur/assert.hpp"
#include "moteur/tilemap.hpp"

namespace moteur {

/// The point where a whole map (not a chunk) is entered: the editor requires it. Files that name it
/// `depart` (its French name in the first files) are still read, with a warning.
inline constexpr const char* kStartPoint = "start";

/// An object placed in a map (see MapData): a description, not an entity.
struct MapObject {
    std::string id;            ///< unique in the map (JSON `id`); the editor gives `o1`, `o2`...
    std::string type;          ///< what the game builds from it (JSON `type`); MapData::parse() rejects an empty one
    glm::vec3 position{0.0f};  ///< x, y (height), z, metres
    float rotation = 0.0f;     ///< degrees about the vertical axis
    float scale = 1.0f;        ///< uniform scale factor
    /// The type's own properties (JSON `props`, always an object): the game reads them.
    nlohmann::json props = nlohmann::json::object();
};

/// Where a chunk joins another (see MapData).
struct MapConnector {
    /// The side of the chunk it opens on. In the file: `north`, `east`, `south`, `west` (the French
    /// `nord`, `est`, `sud`, `ouest` of the first files are still read).
    enum class Direction { North, East, South, West };  ///< north: towards z = 0
    std::string name;    ///< identifies it in the map (JSON `name`)
    std::string kind;    ///< free text for the game (a door, an opening...); may be empty
    glm::ivec2 cell{0};  ///< the cell it stands on (JSON `x`, `z`), inside the map
    Direction direction = Direction::North;  ///< JSON `direction`, `north` when missing
};

/// One character of a map's legend: what a cell drawn with it holds.
struct MapSymbol {
    char character = ' ';       ///< the character of the rows (a JSON key of `legend`)
    std::vector<TileId> tiles;  ///< per layer; shorter than the map's layers: empty above
    std::string point;          ///< the named point on its cells; empty for none
};

/// A map read from a file (milestone 6, part 2): a TileMap, the Tileset it uses, named points,
/// objects and connectors. Built by parse(); read-only afterwards (to edit one, see MapDocument).
/// A default-constructed map is valid and empty: one cell, a hole (symbol ' ', no tile), no tile
/// type, no point.
///
/// A JSON file whose map is drawn in characters, one per cell:
///
/// ```
/// {
///   "version": 1,
///   "description": "Two rooms and a door",         // optional, shown by the tools
///   "tiles": {                                      // the kinds of tiles, by name
///     "floor":  {},                                 // walkable, see-through (the defaults)
///     "wall":   { "walkable": false, "opaque": true },
///     "fence":  { "walkable": false },              // blocks the way, not the sight
///     "pillar": { "walkable": false, "opaque": true, "region": "pillar" }
///   },
///   "legend": {                                     // one character = what the cell holds
///     ".": ["floor"],                               // one tile per layer, from layer 0 ("-": none)
///     "#": ["floor", "wall"],
///     " ": [],                                      // nothing: a hole
///     "@": { "tiles": ["floor"], "point": "start" } // and a named point on that cell
///                                                   // (a list even for one tile: "floor" is refused)
///   },
///   "rows": [                                       // row 0 is z = 0, column 0 is x = 0
///     "#####",
///     "#@..#",
///     "#####"
///   ]
/// }
/// ```
///
/// The map has as many layers as the longest list of the legend. The cell of column i, row j is
/// tile (i, j): it covers x in [i, i + 1), z in [j, j + 1) (1 m cells). Tile ids follow the
/// alphabetical order of the tile names, from 1. A `\r` ending a row (Windows line ends) is ignored.
///
/// The map can be drawn back by to_ascii(): every cell is the legend's character for what it holds,
/// so two characters of the legend may not mean the same thing.
///
/// Version 2 (milestone 7, part 8) adds, all optional (a version 1 file that has one is refused):
///
/// ```
/// "chunk": true,                       // a piece of map (a room), assembled by the game: no start needed
/// "objects": [                         // what is not a tile: decor, lights, effects, spawns...
///   {"id": "o1", "type": "arbre", "x": 12.5, "z": 4.25, "y": 0, "rotation": 30, "scale": 1,
///    "props": {"hauteur": 1.6}}        // the type's own properties (the game reads them)
/// ],
/// "connectors": [                      // where a chunk joins another: a cell on its edge
///   {"name": "north_door", "x": 5, "z": 0, "direction": "north", "kind": "door"}
/// ]
/// ```
///
/// Directions are `north`, `east`, `south`, `west`. The French names of the first files (`nord`,
/// `est`, `sud`, `ouest`) are still read silently, and their start point `depart` (now kStartPoint)
/// with a warning in the log; the editor writes the English ones back.
///
/// Objects are descriptions, not entities: the game turns them into entities when it builds the
/// scene. Their identifiers are unique in the map.
class MapData {
public:
    static constexpr int kVersion = 2;  ///< versions 1 and 2 are read

    /// Reads a map from the text of its JSON file. `source` names it in the error messages (usually
    /// the file's path) and is kept as source().
    ///
    /// Throws std::runtime_error naming `source` and, for a cell, its column and row: invalid JSON,
    /// a `version` that is not the integer 1 or 2 (1.5 is refused), a missing `tiles`, `legend` or
    /// `rows`, a tile with an empty name, unknown tile, legend key that is not one character, legend tiles that are not a list (a single name),
    /// two characters meaning the same, more than 255 legend characters, empty map, rows of different
    /// lengths, character missing from the legend; `chunk`, `objects` or `connectors` in a version 1
    /// file; for version 2, an object with an empty or duplicate id, an empty type or `props` that is
    /// not an object, and a connector with an unknown direction or outside the map.
    static MapData parse(std::string_view json_text, const std::string& source);

    /// The `source` given to parse().
    const std::string& source() const { return source_; }
    /// The file's `description`; empty when it has none.
    const std::string& description() const { return description_; }
    /// The tile types of the file, ids in the alphabetical order of their names.
    const Tileset& tileset() const { return tileset_; }
    /// The grid of tile ids, one layer per position in the legend's lists.
    const TileMap& tiles() const { return tiles_; }
    /// The size in cells (= metres): the length of a row, and the number of rows.
    ///@{
    int width() const { return tiles_.width(); }
    int height() const { return tiles_.height(); }
    ///@}

    /// The id of the tile called `name`; kNoTile if there is none.
    TileId tile_id(const std::string& name) const;
    /// The name of tile `id`; empty for kNoTile or an unknown id.
    const std::string& tile_name(TileId id) const;

    /// Every named point and its cells, in reading order (row after row, left to right).
    const std::map<std::string, std::vector<glm::ivec2>>& points() const { return points_; }
    /// The cells of `name`; empty if none.
    const std::vector<glm::ivec2>& points(const std::string& name) const;
    /// The first cell of `name`. Throws std::out_of_range if there is none.
    glm::ivec2 point(const std::string& name) const;

    /// The map drawn with the legend's characters, one line per row (row 0 first), each ending with
    /// `'\n'`: the `rows` of the file. A default-constructed map is one hole: `" \n"`.
    std::string to_ascii() const;

    /// The file's `version`: 1 or 2 (kVersion).
    int version() const { return version_; }
    /// The file's `chunk`: a piece of map assembled by the game, which needs no start point; false for
    /// version 1.
    bool chunk() const { return chunk_; }
    /// The file's `objects`, in file order; empty for version 1.
    const std::vector<MapObject>& objects() const { return objects_; }
    /// The file's `connectors`, in file order; empty for version 1.
    const std::vector<MapConnector>& connectors() const { return connectors_; }
    /// The legend, what an editor starts from (with cell_symbol()), in the order of its characters
    /// (the JSON keys, sorted).
    const std::vector<MapSymbol>& symbols() const { return symbols_; }
    /// The symbol of a cell: its index in symbols(). `cell` must be inside the map (asserted in the
    /// development builds).
    std::uint8_t cell_symbol(glm::ivec2 cell) const {
        MOTEUR_ASSERT(cell.x >= 0 && cell.y >= 0 && cell.x < tiles_.width() && cell.y < tiles_.height(),
                      "MapData::cell_symbol: cell outside the map");
        return cell_symbols_[static_cast<std::size_t>(cell.y) * static_cast<std::size_t>(tiles_.width()) +
                             static_cast<std::size_t>(cell.x)];
    }
    /// The names of the tile types, as written in the file: tile id i is `tile_names()[i - 1]`
    /// (alphabetical order). Their properties are in tileset().
    const std::vector<std::string>& tile_names() const { return names_; }

    /// How many times the asset was reloaded (hot reload): a scene that builds something from the
    /// map (decor, grids) compares it with the revision it built from. 0 after parse().
    std::uint64_t revision() const { return revision_; }
    /// Sets revision(): for the asset system when it reloads the map, not for the game.
    void set_revision(std::uint64_t revision) { revision_ = revision; }

private:
    using Symbol = MapSymbol;

    std::string source_;
    std::string description_;
    Tileset tileset_;
    TileMap tiles_{1, 1};
    std::vector<std::string> names_;  ///< tile id i is names_[i - 1]
    /// Default-constructed: one cell drawn with one symbol, ' ', holding nothing (a hole).
    std::vector<Symbol> symbols_{Symbol{}};
    std::vector<std::uint8_t> cell_symbols_ = std::vector<std::uint8_t>(1, 0);  ///< index in symbols_, row after row
    std::map<std::string, std::vector<glm::ivec2>> points_;
    std::uint64_t revision_ = 0;
    int version_ = 1;
    bool chunk_ = false;
    std::vector<MapObject> objects_;
    std::vector<MapConnector> connectors_;
};

}  // namespace moteur
