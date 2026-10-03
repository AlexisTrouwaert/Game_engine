#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <vector>

#include "moteur/nav_grid.hpp"

namespace moteur {

// Rays and sight on the game grid (milestone 6, parts 6 and 8). Pure logic, deterministic.

enum class RayBlock {
    Opaque,      // stopped by what blocks the sight (walls, tall grass): line of sight
    Unwalkable,  // stopped by what blocks the way (walls, fences, holes): what a projectile hits
};

struct GridHit {
    glm::ivec2 cell{0};        // the blocking cell
    glm::vec2 point{0.0f};     // where the ray enters it
    glm::vec2 normal{0.0f};    // the face it enters by ((0, 0) if the ray starts inside)
    float distance = 0.0f;     // from the start, in metres
};

// The first blocking cell along the segment [from, to] (Amanatides and Woo's grid walk), or nullopt.
// A ray exactly through the corner between two cells stops if either of them blocks: nothing passes
// through a corner. The cell of `from` counts (a ray starting in a wall hits at once).
std::optional<GridHit> raycast(const NavGrid& grid, glm::vec2 from, glm::vec2 to, RayBlock block = RayBlock::Opaque);

// Whether a and b see each other: no opaque cell between them (their own cells do not count, so a
// creature in tall grass still sees out). Symmetric: line_of_sight(a, b) == line_of_sight(b, a).
bool line_of_sight(const NavGrid& grid, glm::vec2 a, glm::vec2 b);

// The cells seen from a cell within a radius (symmetric shadowcasting, after Albert Ford: if A sees
// B then B sees A, no artefacts in corridors). Exact rational slopes: the same result everywhere.
// Opaque cells at the edge of what is seen are seen (the walls of a room), not what is behind them.
class FieldOfView {
public:
    // Computes what `origin` sees within `radius` cells (a disc: dx² + dy² <= radius²).
    void compute(const NavGrid& grid, glm::ivec2 origin, int radius);

    bool visible(glm::ivec2 cell) const;
    glm::ivec2 origin() const { return origin_; }
    int radius() const { return radius_; }
    std::uint64_t version() const { return version_; }  // of the grid it was computed on
    // The visible cells, in no particular order but always the same.
    const std::vector<glm::ivec2>& cells() const { return cells_; }

private:
    void mark(glm::ivec2 cell);

    glm::ivec2 origin_{0};
    int radius_ = -1;
    std::uint64_t version_ = 0;
    std::vector<std::uint8_t> window_;  // (2 radius + 1)², centred on the origin
    std::vector<glm::ivec2> cells_;
};

// The cells seen at least once (a bit per cell); they stay explored. Ready to be saved.
class ExploredMap {
public:
    ExploredMap() = default;
    ExploredMap(int width, int height);

    // Adds what `view` sees; returns how many cells were explored for the first time.
    int add(const FieldOfView& view);
    bool explored(glm::ivec2 cell) const;
    int count() const { return count_; }
    int width() const { return width_; }
    int height() const { return height_; }
    // One byte per cell, row after row (0 or 1): to save, and to draw.
    const std::vector<std::uint8_t>& cells() const { return cells_; }

private:
    int width_ = 0;
    int height_ = 0;
    int count_ = 0;
    std::vector<std::uint8_t> cells_;
};

}  // namespace moteur
