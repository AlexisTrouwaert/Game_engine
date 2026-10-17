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

/// A map being edited (milestone 7, parts 7 and 8): what the map editor changes and writes back in
/// the format of MapData (version 2), with undo and redo. Plain CPU code, tested without a GPU.
///
/// A cell holds a stack of tiles (one per layer) and at most one named point; the file draws each
/// cell with the legend's character for that pair. The legend of the file is kept (its characters
/// stay); a pair that has no character yet gets one when the map is written.
///
/// Writing gives a stable text: one row per line, one object per line, numbers written as short as
/// they read back exactly: editing one cell changes one line of the file.
///
/// Every change is recorded for undo. A change made outside begin_action() / end_action() is a step
/// of its own: wrap a brush stroke, and the tools rect() and fill(), in an action, or each cell
/// they change becomes one undo step. The history keeps the last 500 steps.
///
/// Not thread-safe; an editor uses it from one thread.
class MapDocument {
public:
    /// A tile type as the editor sees it: its name and properties (the file's `tiles` entry).
    /// Unlike moteur::TileType, it carries its name; its id is its index in tile_types() + 1.
    struct TileType {
        std::string name;       ///< the key in the file's `tiles`; unique in the document
        bool walkable = true;   ///< creatures can stand on it
        bool opaque = false;    ///< blocks the line of sight
        std::string region;     ///< sprite name in an atlas; empty for none
    };
    /// A problem found by validate().
    struct Issue {
        /// Error: the map is not usable as it is (no start, start blocked, invalid map). Warning:
        /// probably a mistake (unreachable cells, objects or connectors out of place).
        enum class Level { Warning, Error };
        Level level = Level::Warning;   ///< how serious it is
        std::string message;            ///< in English, for the editor's list
        std::vector<glm::ivec2> cells;  ///< where (for the editor to show)
    };

    MapDocument() = default;  ///< 0 x 0, no layer, no tile type: use the other constructor or from()
    /// An empty map: every cell holds `floor` (a tile type created with that name) on layer 0.
    /// The legend draws it with `.`; sizes and the layer count below 1 are taken as 1.
    MapDocument(int width, int height, int layers, const std::string& floor);
    /// A document holding everything `map` read from its file (legend, points, objects,
    /// connectors), with an empty history. New object ids continue after the largest `o<n>` of the
    /// map.
    static MapDocument from(const MapData& map);

    // --- Size and tiles
    /// The size in cells (= metres) and the number of layers; resize() changes the size.
    ///@{
    int width() const { return width_; }
    int height() const { return height_; }
    int layer_count() const { return layers_; }
    ///@}
    /// True if 0 <= x < width() and 0 <= y < height().
    bool contains(glm::ivec2 cell) const { return cell.x >= 0 && cell.y >= 0 && cell.x < width_ && cell.y < height_; }
    /// Tile ids are 1 + an index in tile_types() (0: no tile). kNoTile outside the map or for a
    /// layer that does not exist.
    TileId tile(int layer, glm::ivec2 cell) const;
    /// Records the change in the current action (see begin_action). False if unchanged or outside,
    /// for a layer that does not exist or an id that is not in tile_types().
    bool set_tile(int layer, glm::ivec2 cell, TileId id);
    /// The whole stack of a cell (one id per layer). All kNoTile outside the map.
    std::vector<TileId> stack(glm::ivec2 cell) const;
    /// Sets every layer of a cell with set_tile(): missing layers get kNoTile, extra ids are
    /// ignored. Outside the map, nothing happens.
    void set_stack(glm::ivec2 cell, const std::vector<TileId>& stack);

    // --- Tile types and legend
    /// The tile types; id i is `tile_types()[i - 1]`.
    const std::vector<TileType>& tile_types() const { return types_; }
    /// The id of the type called `name`; kNoTile if there is none.
    TileId tile_id(const std::string& name) const;
    /// Adds a type (or gives the id of the one with that name). Recorded.
    TileId add_tile_type(const TileType& type);
    /// Replaces the type `id` (its name included). Recorded. Nothing happens (nothing recorded) for
    /// kNoTile or an unknown id, or when `type.name` is empty or is the name of another type.
    void set_tile_type(TileId id, const TileType& type);
    /// The legend kept from the file, plus the entries set_symbol() added. Cells whose pair (stack,
    /// point) is not in it get a new character only when the map is written (to_json()).
    const std::vector<MapSymbol>& legend() const { return legend_; }
    /// Adds or replaces the legend's entry for `character`. Recorded. Trailing empty layers of its
    /// stack are dropped.
    void set_symbol(const MapSymbol& symbol);
    /// The stacks of the legend (what a palette shows): the legend's symbols without point, each
    /// stack once, in legend order.
    std::vector<MapSymbol> palette() const;

    // --- Points (one per cell at most)
    /// The name of the point on the cell; nullopt if none.
    std::optional<std::string> point_at(glm::ivec2 cell) const;
    void set_point(glm::ivec2 cell, const std::string& name);  ///< empty name: removes; recorded; ignored outside the map
    /// Every named point and its cells in reading order (row after row), as MapData::points() gives
    /// them. Built on each call.
    std::map<std::string, std::vector<glm::ivec2>> points() const;

    // --- Objects and connectors
    /// The objects in the order they were added (written in that order).
    const std::vector<MapObject>& objects() const { return objects_; }
    /// The object with this id; nullptr if none. The pointer is valid until the next change of the
    /// objects (add, update, remove, resize, undo, redo).
    const MapObject* object(const std::string& id) const;
    /// Adds with a fresh identifier (`o<n>`) if `object.id` is empty or taken; returns its id.
    /// Recorded.
    std::string add_object(MapObject object);
    void update_object(const MapObject& object);  ///< by id; recorded; ignored for an unknown id
    /// Removes the object with this id. Recorded; nothing happens for an unknown id.
    void remove_object(const std::string& id);
    /// The connectors, in file order.
    const std::vector<MapConnector>& connectors() const { return connectors_; }
    /// Replaces every connector. Recorded.
    void set_connectors(std::vector<MapConnector> connectors);

    // --- The rest of the file (the setters record the change; the fields are read as they are)
    /// The file's `description` and `chunk` flag. Read them freely; change them with
    /// set_description() and set_chunk(): a direct write is neither recorded for undo nor counted by
    /// revision().
    ///@{
    std::string description;
    bool chunk = false;
    ///@}
    /// Change description and chunk. Recorded; nothing happens when the value is the same.
    ///@{
    void set_description(std::string text);
    void set_chunk(bool value);
    ///@}

    // --- Tools (each records into the current action)
    /// Every cell of a straight line (Bresenham), both ends included, from `a` to `b`. Does not
    /// touch the document (the brush draws on these cells).
    static std::vector<glm::ivec2> line(glm::ivec2 a, glm::ivec2 b);
    /// Sets a filled or hollow rectangle (corners in any order) to `stack`. Returns cells changed.
    /// The part outside the map is ignored.
    int rect(glm::ivec2 a, glm::ivec2 b, const std::vector<TileId>& stack, bool hollow);
    /// Flood fill: the 4-connected cells holding the same stack as `start` get `stack`. Returns the
    /// cells changed; 0 when `start` is outside or already holds `stack`.
    int fill(glm::ivec2 start, const std::vector<TileId>& stack);
    /// Grows (positive) or shrinks (negative) each side; points, objects and connectors move with
    /// the cells (those that fall outside go). New cells hold `stack`. The size stays at least 1 x 1;
    /// the layer count does not change. One undo step (the whole grid is saved).
    void resize(int left, int top, int right, int bottom, const std::vector<TileId>& stack);

    // --- Undo and redo: the changes between begin_action() and end_action() are one step (a
    // whole brush stroke, not one cell).
    /// Starts an undo step called `name` (shown by undo_name()). Ends the current one first, if any.
    void begin_action(std::string name);
    /// Ends the current step; a step that changed nothing is dropped. Clears the redo steps when it
    /// keeps one. Does nothing outside an action.
    void end_action();
    /// True between begin_action() and end_action().
    bool in_action() const { return current_.has_value(); }
    /// Forgets every step (a map just created or opened: nothing to undo).
    void clear_history() {
        current_.reset();
        undo_.clear();
        redo_.clear();
    }
    /// True when undo() (or redo()) has a step to apply.
    ///@{
    bool can_undo() const { return !undo_.empty(); }
    bool can_redo() const { return !redo_.empty(); }
    ///@}
    /// Undoes the last step, or does again the last undone one. Ends the current action first.
    /// False when there is none.
    ///@{
    bool undo();
    bool redo();
    ///@}
    /// The name of the step undo() would undo (for an "Undo ..." menu entry); empty if none.
    const std::string& undo_name() const;
    /// Grows at every change (undo and redo included): a view rebuilds when it changes.
    std::uint64_t revision() const { return revision_; }

    // --- Checks and writing
    /// The problems of the map, for the editor: missing start (unless a chunk), start on a blocked
    /// cell, walkable cells that cannot be reached from the start (by a 0.3 m circle), objects
    /// outside the map, connectors not on a walkable cell of the edge, and a map that does not read
    /// back. Builds the MapData, a NavGrid and a flow field: O(cells), allocates.
    std::vector<Issue> validate() const;
    /// The JSON text of the map (version 2), stable from one write to the next. Throws
    /// std::runtime_error when there are more different cells than free legend characters.
    std::string to_json() const;
    /// The tiles and tile set, as the game reads them (to check, to try): to_json() read back by
    /// MapData::parse(), with `source` in its error messages. Throws std::runtime_error as they do.
    MapData to_map_data(const std::string& source = "document") const;

private:
    /// The state outside the tiles, saved whole when an action changes it.
    struct Extras {
        std::vector<TileType> types;
        std::vector<MapSymbol> legend;
        std::map<std::pair<int, int>, std::string> points;  ///< (x, y) -> name
        std::vector<MapObject> objects;
        std::vector<MapConnector> connectors;
        std::string description;
        bool chunk = false;
        int width = 0, height = 0, layers = 0;
        std::vector<TileId> cells;  ///< whole grid: only for a resize
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
        std::optional<Extras> extras_before;  ///< set when the action changed something else
        std::optional<Extras> extras_after;
        bool resized = false;
    };

    std::size_t index(int layer, glm::ivec2 cell) const {
        return (static_cast<std::size_t>(layer) * static_cast<std::size_t>(height_) + static_cast<std::size_t>(cell.y)) *
                   static_cast<std::size_t>(width_) + static_cast<std::size_t>(cell.x);
    }
    Extras extras(bool with_cells) const;
    void restore(const Extras& extras);
    void touch_extras(bool with_cells = false);  ///< before changing what Extras holds
    void changed();

    int width_ = 0;
    int height_ = 0;
    int layers_ = 0;
    std::vector<TileId> cells_;  ///< layer after layer, row after row
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
