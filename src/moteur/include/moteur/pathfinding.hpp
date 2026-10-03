#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

#include "moteur/nav_grid.hpp"

namespace moteur {

// Pathfinding on the game grid (milestone 6, part 4): A* with eight directions and integer costs
// (10 straight, 14 diagonal, octile heuristic), never cutting a corner, for a circle of a given
// radius (ClearanceMap), smoothed into straight lines; and flow fields, one search that leads every
// creature heading for the same target. Everything is integer or exactly rounded: the same paths on
// every machine. Ties are broken by a written rule (lower f, then lower h, then lower cell index).

// For every walkable cell, the radius of the largest circle centred on the cell's middle that
// touches no blocked cell, up to kMaxClearance; 0 for blocked cells. A circle of radius r may stand
// on a cell, and the pathfinding may use it, when clearance(cell) >= r.
class ClearanceMap {
public:
    static constexpr float kMaxClearance = 4.0f;

    ClearanceMap() = default;
    explicit ClearanceMap(const NavGrid& grid);

    float clearance(glm::ivec2 cell) const {
        return grid_ && grid_->contains(cell) ? values_[index(cell)] : 0.0f;
    }
    bool fits(glm::ivec2 cell, float radius) const { return clearance(cell) >= radius; }
    // After a cell of the grid changed (NavGrid::refresh or set): computes again the cells it can
    // affect, and takes the grid's new version.
    void update_around(glm::ivec2 cell);
    // The grid version it was computed for: compute again when the grid's differs.
    std::uint64_t version() const { return version_; }
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

enum class PathStatus {
    Found,    // to the goal
    Partial,  // the goal cannot be reached (blocked, cut off, too far for max_nodes): to the reachable
              // cell nearest to it
    None,     // the start itself is not usable (no cell around it fits the circle)
};

struct PathOptions {
    float radius = 0.35f;
    int max_nodes = 20000;  // cells expanded at most; past it, the nearest cell found so far
    bool smooth = true;     // straight lines where the circle passes (segment_clear)
};

struct PathResult {
    PathStatus status = PathStatus::None;
    // From the start (excluded) to the end: the points to walk through, in metres on the plane.
    // The last is the goal itself when found and the circle fits there, else the end cell's middle.
    std::vector<glm::vec2> points;
    int expanded = 0;  // cells taken from the open list
    int cost = 0;      // in tenths of a cell (10 per straight step)
};

// Finds a path for a circle from `start` to `goal`. Uses `clearance` (computed for this grid version).
PathResult find_path(const NavGrid& grid, const ClearanceMap& clearance, glm::vec2 start, glm::vec2 goal,
                     const PathOptions& options = {});

// A flow field: from every cell within reach, the next cell towards the target, by the shortest way
// (Dijkstra over the same costs and corner rule as A*). Computed once for a target; read by every
// creature heading there. Cells beyond `max_cost` (tenths of a cell) or unreachable have no
// direction.
class FlowField {
public:
    static constexpr int kUnreached = -1;

    FlowField() = default;

    // Computes the field towards `target` for circles of `radius`. Returns the cells reached.
    int compute(const NavGrid& grid, const ClearanceMap& clearance, glm::vec2 target, float radius,
                int max_cost = 400);

    bool valid() const { return !costs_.empty(); }
    glm::ivec2 target_cell() const { return target_cell_; }
    glm::vec2 target() const { return target_; }
    float radius() const { return radius_; }
    std::uint64_t version() const { return version_; }  // of the grid it was computed on
    int width() const { return width_; }
    int height() const { return height_; }

    // The cost from the cell to the target, kUnreached if none.
    int cost(glm::ivec2 cell) const;
    // The next cell towards the target; the cell itself for the target cell; (-1, -1) if unreached.
    glm::ivec2 next(glm::ivec2 cell) const;
    // A unit direction from `position` towards the target (towards the middle of the next cell, or
    // the target itself on the target cell); (0, 0) if the position's cell is unreached or at the
    // target.
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
    std::vector<std::int8_t> next_;  // index in kNeighbours, -1 none, 8 the target itself
};

}  // namespace moteur
