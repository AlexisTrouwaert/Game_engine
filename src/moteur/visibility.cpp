#include "moteur/visibility.hpp"

#include "moteur/profiler.hpp"

#include <cmath>
#include <cstdint>
#include <limits>

namespace moteur {

namespace {

bool blocks(const NavGrid& grid, glm::ivec2 cell, RayBlock block) {
    return block == RayBlock::Opaque ? grid.opaque(cell) : !grid.walkable(cell);
}

// The walk of raycast(), with the start and end cells optionally ignored (line of sight).
std::optional<GridHit> walk(const NavGrid& grid, glm::vec2 from, glm::vec2 to, RayBlock block, bool skip_ends) {
    glm::ivec2 cell = cell_at(from);
    const glm::ivec2 last = cell_at(to);
    auto hit_here = [&](glm::ivec2 c) { return !(skip_ends && (c == cell_at(from) || c == last)) && blocks(grid, c, block); };
    const glm::vec2 d = to - from;
    const float length = std::sqrt(glm::dot(d, d));
    if (hit_here(cell)) {
        return GridHit{cell, from, glm::vec2(0.0f), 0.0f};
    }
    const glm::ivec2 step(d.x > 0.0f ? 1 : (d.x < 0.0f ? -1 : 0), d.y > 0.0f ? 1 : (d.y < 0.0f ? -1 : 0));
    constexpr float kNever = std::numeric_limits<float>::infinity();
    // t (0 at from, 1 at to) where the ray crosses the next vertical / horizontal grid line.
    auto first_crossing = [&](int axis) {
        if (step[axis] == 0) {
            return kNever;
        }
        const float boundary = static_cast<float>(cell[axis] + (step[axis] > 0 ? 1 : 0));
        return (boundary - from[axis]) / d[axis];
    };
    float t_max[2] = {first_crossing(0), first_crossing(1)};
    const float t_delta[2] = {step.x != 0 ? std::abs(1.0f / d.x) : kNever, step.y != 0 ? std::abs(1.0f / d.y) : kNever};
    auto make_hit = [&](glm::ivec2 c, float t, glm::vec2 normal) {
        return GridHit{c, from + d * t, normal, t * length};
    };
    while (cell != last) {
        if (t_max[0] < t_max[1]) {
            const float t = t_max[0];
            if (t > 1.0f) break;
            cell.x += step.x;
            t_max[0] += t_delta[0];
            if (hit_here(cell)) return make_hit(cell, t, {static_cast<float>(-step.x), 0.0f});
        } else if (t_max[1] < t_max[0]) {
            const float t = t_max[1];
            if (t > 1.0f) break;
            cell.y += step.y;
            t_max[1] += t_delta[1];
            if (hit_here(cell)) return make_hit(cell, t, {0.0f, static_cast<float>(-step.y)});
        } else {
            // Exactly through a corner: blocked if either cell beside it blocks.
            const float t = t_max[0];
            if (t > 1.0f || t == kNever) break;
            const glm::ivec2 side_x(cell.x + step.x, cell.y);
            const glm::ivec2 side_y(cell.x, cell.y + step.y);
            if (hit_here(side_x)) return make_hit(side_x, t, {static_cast<float>(-step.x), 0.0f});
            if (hit_here(side_y)) return make_hit(side_y, t, {0.0f, static_cast<float>(-step.y)});
            cell += step;
            t_max[0] += t_delta[0];
            t_max[1] += t_delta[1];
            if (hit_here(cell)) return make_hit(cell, t, {static_cast<float>(-step.x), 0.0f});
        }
    }
    return std::nullopt;
}

// An exact slope (2 col - 1) / (2 depth) and the like: n / d with d > 0.
struct Fraction {
    std::int64_t n, d;
};
bool less_equal(Fraction a, Fraction b) { return a.n * b.d <= b.n * a.d; }
std::int64_t floor_div(std::int64_t a, std::int64_t b) {
    return a / b - ((a % b != 0) && ((a < 0) != (b < 0)) ? 1 : 0);
}
std::int64_t ceil_div(std::int64_t a, std::int64_t b) { return -floor_div(-a, b); }
// round(depth * slope), ties up / down, as in Albert Ford's algorithm.
std::int64_t round_ties_up(std::int64_t depth, Fraction s) { return floor_div(2 * depth * s.n + s.d, 2 * s.d); }
std::int64_t round_ties_down(std::int64_t depth, Fraction s) { return ceil_div(2 * depth * s.n - s.d, 2 * s.d); }

struct Row {
    int depth;
    Fraction start, end;
};

}  // namespace

std::optional<GridHit> raycast(const NavGrid& grid, glm::vec2 from, glm::vec2 to, RayBlock block) {
    return walk(grid, from, to, block, false);
}

bool line_of_sight(const NavGrid& grid, glm::vec2 a, glm::vec2 b) {
    return !walk(grid, a, b, RayBlock::Opaque, true) && !walk(grid, b, a, RayBlock::Opaque, true);
}

void FieldOfView::mark(glm::ivec2 cell) {
    const glm::ivec2 d = cell - origin_;
    if (d.x * d.x + d.y * d.y > radius_ * radius_) {
        return;
    }
    const int side = 2 * radius_ + 1;
    std::uint8_t& seen = window_[static_cast<std::size_t>((d.y + radius_) * side + (d.x + radius_))];
    if (!seen) {
        seen = 1;
        cells_.push_back(cell);
    }
}

void FieldOfView::compute(const NavGrid& grid, glm::ivec2 origin, int radius) {
    MOTEUR_PROFILE("champ de vision");
    origin_ = origin;
    radius_ = std::max(radius, 0);
    version_ = grid.version();
    const int side = 2 * radius_ + 1;
    window_.assign(static_cast<std::size_t>(side * side), 0);
    cells_.clear();
    mark(origin);
    for (int quadrant = 0; quadrant < 4; ++quadrant) {
        // (depth, col) of the quadrant to a cell: north, east, south, west.
        auto transform = [&](int depth, std::int64_t col) {
            const int c = static_cast<int>(col);
            switch (quadrant) {
                case 0: return glm::ivec2(origin.x + c, origin.y - depth);
                case 1: return glm::ivec2(origin.x + depth, origin.y + c);
                case 2: return glm::ivec2(origin.x + c, origin.y + depth);
                default: return glm::ivec2(origin.x - depth, origin.y + c);
            }
        };
        std::vector<Row> rows{{1, {-1, 1}, {1, 1}}};
        while (!rows.empty()) {
            Row row = rows.back();
            rows.pop_back();
            if (row.depth > radius_) {
                continue;
            }
            const std::int64_t min_col = round_ties_up(row.depth, row.start);
            const std::int64_t max_col = round_ties_down(row.depth, row.end);
            int previous = -1;  // -1 none, 0 floor, 1 wall
            for (std::int64_t col = min_col; col <= max_col; ++col) {
                const glm::ivec2 cell = transform(row.depth, col);
                const bool wall = grid.opaque(cell);
                const bool symmetric = less_equal(Fraction{row.depth * row.start.n, row.start.d}, Fraction{col, 1}) &&
                                       less_equal(Fraction{col, 1}, Fraction{row.depth * row.end.n, row.end.d});
                if (wall || symmetric) {
                    mark(cell);
                }
                const Fraction slope{2 * col - 1, 2 * static_cast<std::int64_t>(row.depth)};
                if (previous == 1 && !wall) {
                    row.start = slope;
                }
                if (previous == 0 && wall) {
                    rows.push_back({row.depth + 1, row.start, slope});
                }
                previous = wall ? 1 : 0;
            }
            if (previous == 0) {
                rows.push_back({row.depth + 1, row.start, row.end});
            }
        }
    }
}

bool FieldOfView::visible(glm::ivec2 cell) const {
    const glm::ivec2 d = cell - origin_;
    if (radius_ < 0 || std::abs(d.x) > radius_ || std::abs(d.y) > radius_) {
        return false;
    }
    const int side = 2 * radius_ + 1;
    return window_[static_cast<std::size_t>((d.y + radius_) * side + (d.x + radius_))] != 0;
}

ExploredMap::ExploredMap(int width, int height)
    : width_(width), height_(height), cells_(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0) {}

int ExploredMap::add(const FieldOfView& view) {
    int added = 0;
    for (const glm::ivec2 cell : view.cells()) {
        if (cell.x < 0 || cell.y < 0 || cell.x >= width_ || cell.y >= height_) {
            continue;
        }
        std::uint8_t& seen = cells_[static_cast<std::size_t>(cell.y * width_ + cell.x)];
        if (!seen) {
            seen = 1;
            ++added;
        }
    }
    count_ += added;
    return added;
}

bool ExploredMap::mark(glm::ivec2 cell) {
    if (cell.x < 0 || cell.y < 0 || cell.x >= width_ || cell.y >= height_) {
        return false;
    }
    std::uint8_t& seen = cells_[static_cast<std::size_t>(cell.y * width_ + cell.x)];
    if (seen) {
        return false;
    }
    seen = 1;
    ++count_;
    return true;
}

bool ExploredMap::explored(glm::ivec2 cell) const {
    if (cell.x < 0 || cell.y < 0 || cell.x >= width_ || cell.y >= height_) {
        return false;
    }
    return cells_[static_cast<std::size_t>(cell.y * width_ + cell.x)] != 0;
}

}  // namespace moteur
