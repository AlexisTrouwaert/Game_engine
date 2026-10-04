#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "moteur/map_data.hpp"

namespace moteur {

// A map being edited (milestone 7, parts 7 and 8): what the map editor changes and writes back in
// the format of MapData (version 2), with undo and redo. Plain CPU code, tested without a GPU.
//
// A cell holds a stack of tiles (one per layer) and at most one named point; the file draws each
// cell with the legend's character for that pair. The legend of the file is kept (its characters
// stay); a pair that has no character yet gets one when the map is written.
//
// Writing gives a stable text: one row per line, one object per line, numbers written as short as
// they read back exactly: editing one cell changes one line of the file.
class MapDocument {
public:
    struct TileType {
        std::string name;
        bool walkable = true;
        bool opaque = false;
        std::string region;
    };
    struct Issue {
        enum class Level { Warning, Error };
        Level level = Level::Warning;
        std::string message;
        std::vector<glm::ivec2> cells;  // where (for the editor to show)
    };

    MapDocument() = default;
    // An empty map: every cell holds `floor` (a tile type created with that name).
    MapDocument(int width, int height, int layers, const std::string& floor);
    static MapDocument from(const MapData& map);

    // --- Size and tiles
    int width() const { return width_; }
    int height() const { return height_; }
    int layer_count() const { return layers_; }
    bool contains(glm::ivec2 cell) const { return cell.x >= 0 && cell.y >= 0 && cell.x < width_ && cell.y < height_; }
    // Tile ids are 1 + an index in tile_types() (0: no tile).
    TileId tile(int layer, glm::ivec2 cell) const;
    // Records the change in the current action (see begin_action). False if unchanged or outside.
    bool set_tile(int layer, glm::ivec2 cell, TileId id);
    // The whole stack of a cell (one id per layer).
    std::vector<TileId> stack(glm::ivec2 cell) const;
    void set_stack(glm::ivec2 cell, const std::vector<TileId>& stack);

    // --- Tile types and legend
    const std::vector<TileType>& tile_types() const { return types_; }
    TileId tile_id(const std::string& name) const;
    // Adds a type (or gives the id of the one with that name). Recorded.
    TileId add_tile_type(const TileType& type);
    void set_tile_type(TileId id, const TileType& type);
    const std::vector<MapSymbol>& legend() const { return legend_; }
    // Adds or replaces the legend's entry for `character`. Recorded.
    void set_symbol(const MapSymbol& symbol);
    // The stacks of the legend (what a palette shows): the legend's symbols without point.
    std::vector<MapSymbol> palette() const;

    // --- Points (one per cell at most)
    std::optional<std::string> point_at(glm::ivec2 cell) const;
    void set_point(glm::ivec2 cell, const std::string& name);  // empty name: removes
    std::map<std::string, std::vector<glm::ivec2>> points() const;

    // --- Objects and connectors
    const std::vector<MapObject>& objects() const { return objects_; }
    const MapObject* object(const std::string& id) const;
    // Adds with a fresh identifier ("o<n>") if `object.id` is empty or taken; returns its id.
    std::string add_object(MapObject object);
    void update_object(const MapObject& object);  // by id
    void remove_object(const std::string& id);
    const std::vector<MapConnector>& connectors() const { return connectors_; }
    void set_connectors(std::vector<MapConnector> connectors);

    // --- The rest of the file (the setters record the change; the fields are read as they are)
    std::string description;
    bool chunk = false;
    void set_description(std::string text);
    void set_chunk(bool value);

    // --- Tools (each records into the current action)
    // Every cell of a straight line (Bresenham), both ends included.
    static std::vector<glm::ivec2> line(glm::ivec2 a, glm::ivec2 b);
    // Sets a filled or hollow rectangle (corners in any order) to `stack`. Returns cells changed.
    int rect(glm::ivec2 a, glm::ivec2 b, const std::vector<TileId>& stack, bool hollow);
    // Flood fill: the 4-connected cells holding the same stack as `start` get `stack`.
    int fill(glm::ivec2 start, const std::vector<TileId>& stack);
    // Grows (positive) or shrinks (negative) each side; points, objects and connectors move with
    // the cells (those that fall outside go). New cells hold `stack`.
    void resize(int left, int top, int right, int bottom, const std::vector<TileId>& stack);

    // --- Undo and redo: the changes between begin_action() and end_action() are one step (a
    // whole brush stroke, not one cell).
    void begin_action(std::string name);
    void end_action();
    bool in_action() const { return current_.has_value(); }
    // Forgets every step (a map just created or opened: nothing to undo).
    void clear_history() {
        current_.reset();
        undo_.clear();
        redo_.clear();
    }
    bool can_undo() const { return !undo_.empty(); }
    bool can_redo() const { return !redo_.empty(); }
    bool undo();
    bool redo();
    const std::string& undo_name() const;
    // Grows at every change (undo and redo included): a view rebuilds when it changes.
    std::uint64_t revision() const { return revision_; }

    // --- Checks and writing
    // Missing start (unless a chunk), cells that cannot be reached from the start, points or
    // objects outside, connectors not on a walkable cell of the edge.
    std::vector<Issue> validate() const;
    // The JSON text of the map (version 2), stable from one write to the next.
    std::string to_json() const;
    // The tiles and tile set, as the game reads them (to check, to try).
    MapData to_map_data(const std::string& source = "document") const;

private:
    // The state outside the tiles, saved whole when an action changes it.
    struct Extras {
        std::vector<TileType> types;
        std::vector<MapSymbol> legend;
        std::map<std::pair<int, int>, std::string> points;  // (x, y) -> name
        std::vector<MapObject> objects;
        std::vector<MapConnector> connectors;
        std::string description;
        bool chunk = false;
        int width = 0, height = 0, layers = 0;
        std::vector<TileId> cells;  // whole grid: only for a resize
        std::uint32_t next_object = 1;
    };
    struct TileEdit {
        int layer;
        glm::ivec2 cell;
        TileId before, after;
    };
    struct Action {
        std::string name;
        std::vector<TileEdit> tiles;
        std::optional<Extras> extras_before;  // set when the action changed something else
        std::optional<Extras> extras_after;
        bool resized = false;
    };

    std::size_t index(int layer, glm::ivec2 cell) const {
        return (static_cast<std::size_t>(layer) * static_cast<std::size_t>(height_) + static_cast<std::size_t>(cell.y)) *
                   static_cast<std::size_t>(width_) + static_cast<std::size_t>(cell.x);
    }
    Extras extras(bool with_cells) const;
    void restore(const Extras& extras);
    void touch_extras(bool with_cells = false);  // before changing what Extras holds
    void changed();

    int width_ = 0;
    int height_ = 0;
    int layers_ = 0;
    std::vector<TileId> cells_;  // layer after layer, row after row
    std::vector<TileType> types_;
    std::vector<MapSymbol> legend_;
    std::map<std::pair<int, int>, std::string> points_;
    std::vector<MapObject> objects_;
    std::vector<MapConnector> connectors_;
    std::uint32_t next_object_ = 1;

    std::optional<Action> current_;
    std::vector<Action> undo_;
    std::vector<Action> redo_;
    std::uint64_t revision_ = 0;
};

}  // namespace moteur
