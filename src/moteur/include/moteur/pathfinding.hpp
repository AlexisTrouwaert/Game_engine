#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

#include "moteur/nav_grid.hpp"
#include "moteur/reflection.hpp"

namespace moteur {

// Pathfinding on the game grid (milestone 6, part 4): A* with eight directions and integer costs
// (10 straight, 14 diagonal, octile heuristic), never cutting a corner, for a circle of a given
// radius (ClearanceMap), smoothed into straight lines; and flow fields, one search that leads every
// creature heading for the same target. Everything is integer or exactly rounded: the same paths on
// every machine. Ties are broken by a written rule (lower f, then lower h, then lower cell index).

/// For every walkable cell, the radius of the largest circle centred on the cell's middle that
/// touches no blocked cell, up to kMaxClearance; 0 for blocked cells. A circle of radius r may stand
/// on a cell, and the pathfinding may use it, when clearance(cell) >= r.
///
/// The map keeps a pointer to its NavGrid: the grid must outlive it, stay at the same address and
/// keep its size (build a new map for a new grid). Distances are in metres (cells).
class ClearanceMap {
public:
    /// The largest clearance stored, in metres: wider open spaces read as this value.
    static constexpr float kMaxClearance = 4.0f;

    ClearanceMap() = default;  ///< no grid: every clearance() is 0
    /// Computes every cell of `grid` (O(cells), each looking at the cells within 5 around it) and
    /// takes the grid's version().
    explicit ClearanceMap(const NavGrid& grid);

    /// The clearance of the cell in metres, in [0, kMaxClearance]; 0 outside the grid or without a
    /// grid.
    float clearance(glm::ivec2 cell) const {
        return grid_ && grid_->contains(cell) ? values_[index(cell)] : 0.0f;
    }
    /// True if a circle of `radius` metres may stand on the cell: clearance(cell) >= radius.
    bool fits(glm::ivec2 cell, float radius) const { return clearance(cell) >= radius; }
    /// After a cell of the grid changed (NavGrid::refresh or set): computes again the cells it can
    /// affect, and takes the grid's new version. Call it once per changed cell. Does nothing without
    /// a grid.
    void update_around(glm::ivec2 cell);
    /// The grid version it was computed for: compute again when the grid's differs.
    std::uint64_t version() const { return version_; }
    /// The grid it reads, nullptr for a default-constructed map. Not owned.
    const NavGrid* grid() const { return grid_; }

private:
    std::size_t index(glm::ivec2 cell) const {
        return static_cast<std::size_t>(cell.y) * static_cast<std::size_t>(grid_->width()) +
               static_cast<std::size_t>(cell.x);
    }
    float compute(glm::ivec2 cell) const;

    const NavGrid* grid_ = nullptr;
    std::vector<float> values_;
    std::uint64_t version_ = 0;
};

/// How a find_path() search ended.
enum class PathStatus {
    Found,    ///< to the goal
    Partial,  ///< the goal cannot be reached (blocked, cut off, too far for max_nodes): to the reachable
              ///< cell nearest to it
    None,     ///< the start itself is not usable (no cell within 2 cells of it fits the circle)
};
MOTEUR_REFLECT_ENUM(PathStatus, "found", "partial", "none");

/// The circle a find_path() search is for, and its limits.
struct PathOptions {
    float radius = 0.35f;   ///< of the circle, in metres (usually its Collider::radius)
    int max_nodes = 20000;  ///< cells expanded at most; past it, the nearest cell found so far
    bool smooth = true;     ///< straight lines where the circle passes (segment_clear)
};

/// What find_path() found.
struct PathResult {
    PathStatus status = PathStatus::None;  ///< how the search ended; None: `points` is empty
    /// From the start (excluded) to the end: the points to walk through, in metres on the plane.
    /// The last is the goal itself when found and the circle fits there, else the end cell's middle.
    /// Empty when there is nowhere to go (already in the end cell, or status None).
    std::vector<glm::vec2> points;
    int expanded = 0;  ///< cells taken from the open list
    int cost = 0;      ///< in tenths of a cell (10 per straight step, 14 diagonal), of the cell path before smoothing
};

/// Finds a path for a circle from `start` to `goal` (metres on the plane). Uses `clearance`
/// (computed for this grid version). A start that does not fit the circle starts from the nearest
/// cell that does, within 2 cells; a goal outside the grid or in a wall gives a Partial path to the
/// nearest reachable cell. Cost: up to `options.max_nodes` cells expanded; allocates its open list
/// and the result (the per-thread scratch arrays are kept between calls). Safe to call from
/// several threads at once on the same grid and map, as long as nothing modifies them meanwhile.
PathResult find_path(const NavGrid& grid, const ClearanceMap& clearance, glm::vec2 start, glm::vec2 goal,
                     const PathOptions& options = {});

/// A flow field: from every cell within reach, the next cell towards the target, by the shortest way
/// (Dijkstra over the same costs and corner rule as A*). Computed once for a target; read by every
/// creature heading there. Cells beyond `max_cost` (tenths of a cell) or unreachable have no
/// direction.
///
/// The field copies what it needs: it keeps no reference to the grid or the clearance map. It uses
/// one int and one byte per cell of the grid.
class FlowField {
public:
    /// The cost() of a cell the field does not reach.
    static constexpr int kUnreached = -1;

    FlowField() = default;  ///< not computed: valid() is false, every cell unreached

    /// Computes the field towards `target` (metres on the plane) for circles of `radius` metres,
    /// out to `max_cost` (tenths of a cell: the default 400 is 40 straight steps). The target cell
    /// is the nearest cell within 3 of the target where the circle fits. Returns the cells reached,
    /// the target cell included; 0 when no cell near the target fits (then every cell is unreached).
    /// O(cells) to reset, plus a Dijkstra over the cells reached; reallocates when the grid size
    /// changes.
    int compute(const NavGrid& grid, const ClearanceMap& clearance, glm::vec2 target, float radius,
                int max_cost = 400);

    /// True once compute() has found a target cell (on a grid of at least one cell); false before
    /// compute(), or when no cell near the target fits (compute() returned 0): such a field leads
    /// nowhere.
    bool valid() const { return !costs_.empty() && target_cell_.x >= 0; }
    /// The cell the field leads to; (-1, -1) before compute() or when no cell near the target fits.
    glm::ivec2 target_cell() const { return target_cell_; }
    /// The target point given to compute(), in metres.
    glm::vec2 target() const { return target_; }
    /// The point the field leads to, in metres: target() when it stands in target_cell(), else (the
    /// target cell was moved off a target that did not fit) the middle of target_cell(). target()
    /// before compute() or when no cell near the target fits.
    glm::vec2 aim() const;
    /// The circle radius it was computed for, in metres.
    float radius() const { return radius_; }
    /// The NavGrid::version() it was computed on: compute again when the grid's differs.
    std::uint64_t version() const { return version_; }  // of the grid it was computed on
    /// The size of the grid it was computed on, in cells.
    ///@{
    int width() const { return width_; }
    int height() const { return height_; }
    ///@}

    /// The cost from the cell to the target in tenths of a cell, kUnreached if none (or outside the
    /// grid).
    int cost(glm::ivec2 cell) const;
    /// The next cell towards the target; the cell itself for the target cell; (-1, -1) if unreached.
    glm::ivec2 next(glm::ivec2 cell) const;
    /// A unit direction from `position` towards the target (towards the middle of the next cell, or
    /// aim() on the target cell); (0, 0) if the position's cell is unreached or at aim(). On the
    /// target cell it aims at target() itself when the target stands there, at the cell's middle
    /// when the target cell was moved off a target that did not fit (never into the wall).
    glm::vec2 direction(glm::vec2 position) const;

private:
    std::size_t index(glm::ivec2 cell) const {
        return static_cast<std::size_t>(cell.y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(cell.x);
    }

    int width_ = 0;
    int height_ = 0;
    glm::ivec2 target_cell_{-1};
    glm::vec2 target_{0.0f};
    float radius_ = 0.0f;
    std::uint64_t version_ = 0;
    std::vector<int> costs_;
    std::vector<std::int8_t> next_;  ///< index in kNeighbours, -1 none, 8 the target itself
};

}  // namespace moteur
