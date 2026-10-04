#include "moteur/pathfinding.hpp"

#include "moteur/profiler.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <queue>

#include "moteur/collision.hpp"

namespace moteur {

namespace {

// Straight neighbours first, then diagonals; this order breaks ties between equal ways.
// Opposites are paired (n ^ 1 is the way back).
const glm::ivec2 kNeighbours[8] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, -1}, {-1, 1}, {1, -1}};
constexpr int kStraight = 10;
constexpr int kDiagonal = 14;
constexpr int kLookAhead = 24;  // smoothing tries straight lines over at most this many points

int octile(glm::ivec2 a, glm::ivec2 b) {
    const int dx = std::abs(a.x - b.x);
    const int dy = std::abs(a.y - b.y);
    return kStraight * (dx + dy) + (kDiagonal - 2 * kStraight) * std::min(dx, dy);
}

// Whether the step from `cell` in direction `n` is allowed for the circle: the cell reached fits,
// and a diagonal needs both straight cells beside it (no corner cut).
bool can_step(const ClearanceMap& clearance, glm::ivec2 cell, int n, float radius) {
    const glm::ivec2 d = kNeighbours[n];
    if (!clearance.fits(cell + d, radius)) {
        return false;
    }
    return n < 4 || (clearance.fits(cell + glm::ivec2(d.x, 0), radius) && clearance.fits(cell + glm::ivec2(0, d.y), radius));
}

// The nearest cell around `cell` where the circle fits (the cell itself first); (-1, -1) if none
// within `reach` cells. Ties: row then column order.
glm::ivec2 nearest_fitting_cell(const ClearanceMap& clearance, glm::ivec2 cell, glm::vec2 from, float radius, int reach) {
    if (clearance.fits(cell, radius)) {
        return cell;
    }
    glm::ivec2 best(-1);
    float best_distance2 = 0.0f;
    for (int j = cell.y - reach; j <= cell.y + reach; ++j) {
        for (int i = cell.x - reach; i <= cell.x + reach; ++i) {
            if (!clearance.fits({i, j}, radius)) {
                continue;
            }
            const glm::vec2 d = cell_centre({i, j}) - from;
            const float distance2 = glm::dot(d, d);
            if (best.x < 0 || distance2 < best_distance2) {
                best = {i, j};
                best_distance2 = distance2;
            }
        }
    }
    return best;
}

// Scratch arrays of the searches, kept from one search to the next (stamped, not cleared).
struct Scratch {
    std::vector<std::uint32_t> stamp;
    std::vector<int> g;
    std::vector<int> parent;
    std::vector<std::uint8_t> closed;
    std::uint32_t generation = 0;

    void begin(std::size_t cells) {
        if (stamp.size() != cells) {
            stamp.assign(cells, 0);
            g.assign(cells, 0);
            parent.assign(cells, -1);
            closed.assign(cells, 0);
            generation = 0;
        }
        if (++generation == 0) {
            std::fill(stamp.begin(), stamp.end(), 0);
            generation = 1;
        }
    }
    bool seen(std::size_t i) const { return stamp[i] == generation; }
    void touch(std::size_t i) {
        if (stamp[i] != generation) {
            stamp[i] = generation;
            g[i] = INT_MAX;
            parent[i] = -1;
            closed[i] = 0;
        }
    }
};

thread_local Scratch scratch;

}  // namespace

ClearanceMap::ClearanceMap(const NavGrid& grid)
    : grid_(&grid),
      values_(static_cast<std::size_t>(grid.width()) * static_cast<std::size_t>(grid.height()), 0.0f),
      version_(grid.version()) {
    for (int j = 0; j < grid.height(); ++j) {
        for (int i = 0; i < grid.width(); ++i) {
            values_[index({i, j})] = compute({i, j});
        }
    }
}

float ClearanceMap::compute(glm::ivec2 cell) const {
    const NavGrid& grid = *grid_;
    if (!grid.walkable(cell)) {
        return 0.0f;
    }
    // Ring after ring around the cell: a blocked cell on ring k is at least k - 0.5 away, so the
    // search stops as soon as the nearest found is nearer than the next ring can be.
    const glm::vec2 middle = cell_centre(cell);
    float best2 = kMaxClearance * kMaxClearance;
    const int reach = static_cast<int>(std::ceil(kMaxClearance)) + 1;
    for (int ring = 1; ring <= reach; ++ring) {
        const float nearest = static_cast<float>(ring) - 0.5f;
        if (best2 <= nearest * nearest) {
            break;
        }
        for (int y = cell.y - ring; y <= cell.y + ring; ++y) {
            const bool edge_row = y == cell.y - ring || y == cell.y + ring;
            for (int x = cell.x - ring; x <= cell.x + ring; x += edge_row ? 1 : 2 * ring) {
                if (grid.walkable({x, y})) {
                    continue;
                }
                const glm::vec2 low(static_cast<float>(x), static_cast<float>(y));
                const glm::vec2 d = middle - glm::clamp(middle, low, low + glm::vec2(1.0f));
                best2 = std::min(best2, glm::dot(d, d));
            }
        }
    }
    return std::sqrt(best2);
}

void ClearanceMap::update_around(glm::ivec2 cell) {
    if (grid_ == nullptr) {
        return;
    }
    const int reach = static_cast<int>(std::ceil(kMaxClearance)) + 1;
    for (int j = std::max(0, cell.y - reach); j <= std::min(grid_->height() - 1, cell.y + reach); ++j) {
        for (int i = std::max(0, cell.x - reach); i <= std::min(grid_->width() - 1, cell.x + reach); ++i) {
            values_[index({i, j})] = compute({i, j});
        }
    }
    version_ = grid_->version();
}

PathResult find_path(const NavGrid& grid, const ClearanceMap& clearance, glm::vec2 start, glm::vec2 goal,
                     const PathOptions& options) {
    MOTEUR_PROFILE("A*");
    PathResult result;
    const float radius = options.radius;
    const glm::ivec2 start_cell = nearest_fitting_cell(clearance, cell_at(start), start, radius, 2);
    if (start_cell.x < 0) {
        return result;
    }
    const glm::ivec2 goal_cell = cell_at(goal);
    const int width = grid.width();
    auto index_of = [width](glm::ivec2 c) { return static_cast<std::size_t>(c.y * width + c.x); };
    auto cell_of = [width](std::size_t i) { return glm::ivec2(static_cast<int>(i) % width, static_cast<int>(i) / width); };

    scratch.begin(static_cast<std::size_t>(width) * static_cast<std::size_t>(grid.height()));
    struct Open {
        int f, h;
        std::size_t index;
        bool operator>(const Open& o) const {
            return f != o.f ? f > o.f : (h != o.h ? h > o.h : index > o.index);
        }
    };
    std::priority_queue<Open, std::vector<Open>, std::greater<Open>> open;
    const std::size_t first = index_of(start_cell);
    scratch.touch(first);
    scratch.g[first] = 0;
    open.push({octile(start_cell, goal_cell), octile(start_cell, goal_cell), first});

    std::size_t best = first;
    int best_h = octile(start_cell, goal_cell);
    bool found = false;
    while (!open.empty() && result.expanded < options.max_nodes) {
        const Open top = open.top();
        open.pop();
        if (scratch.closed[top.index]) {
            continue;  // an older, longer entry
        }
        scratch.closed[top.index] = 1;
        ++result.expanded;
        const glm::ivec2 cell = cell_of(top.index);
        if (top.h < best_h || (top.h == best_h && scratch.g[top.index] < scratch.g[best])) {
            best = top.index;
            best_h = top.h;
        }
        if (cell == goal_cell) {
            found = true;
            break;
        }
        for (int n = 0; n < 8; ++n) {
            if (!can_step(clearance, cell, n, radius)) {
                continue;
            }
            const glm::ivec2 next = cell + kNeighbours[n];
            const std::size_t ni = index_of(next);
            scratch.touch(ni);
            if (scratch.closed[ni]) {
                continue;
            }
            const int g = scratch.g[top.index] + (n < 4 ? kStraight : kDiagonal);
            if (g < scratch.g[ni]) {
                scratch.g[ni] = g;
                scratch.parent[ni] = static_cast<int>(top.index);
                const int h = octile(next, goal_cell);
                open.push({g + h, h, ni});
            }
        }
    }

    const std::size_t end = found ? index_of(goal_cell) : best;
    result.status = found ? PathStatus::Found : PathStatus::Partial;
    result.cost = scratch.g[end];
    std::vector<glm::ivec2> cells;
    for (int i = static_cast<int>(end); i >= 0; i = scratch.parent[static_cast<std::size_t>(i)]) {
        cells.push_back(cell_of(static_cast<std::size_t>(i)));
    }
    std::reverse(cells.begin(), cells.end());

    // The points: the start, the middles of the cells after the first, and the end.
    std::vector<glm::vec2> points;
    points.push_back(start);
    for (std::size_t i = 1; i < cells.size(); ++i) {
        points.push_back(cell_centre(cells[i]));
    }
    const bool exact_goal = found && circle_fits(grid, goal, radius);
    if (exact_goal) {
        if (cells.size() > 1) {
            points.back() = goal;
        } else {
            points.push_back(goal);
        }
    } else if (cells.size() == 1 && start_cell != cell_at(start)) {
        points.push_back(cell_centre(start_cell));  // the start did not fit: back into a cell that does
    }

    if (options.smooth && points.size() > 2) {
        std::vector<glm::vec2> smooth;
        smooth.push_back(points[0]);
        std::size_t anchor = 0;
        while (anchor + 1 < points.size()) {
            std::size_t reach = anchor + 1;
            const std::size_t last = std::min(points.size() - 1, anchor + kLookAhead);
            for (std::size_t j = anchor + 2; j <= last; ++j) {
                if (!segment_clear(grid, points[anchor], points[j], radius)) {
                    break;
                }
                reach = j;
            }
            smooth.push_back(points[reach]);
            anchor = reach;
        }
        points = std::move(smooth);
    }
    result.points.assign(points.begin() + 1, points.end());
    return result;
}

int FlowField::compute(const NavGrid& grid, const ClearanceMap& clearance, glm::vec2 target, float radius, int max_cost) {
    MOTEUR_PROFILE("flow field");
    width_ = grid.width();
    height_ = grid.height();
    radius_ = radius;
    target_ = target;
    version_ = grid.version();
    const std::size_t cells = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
    costs_.assign(cells, kUnreached);
    next_.assign(cells, -1);
    target_cell_ = nearest_fitting_cell(clearance, cell_at(target), target, radius, 3);
    if (target_cell_.x < 0) {
        return 0;
    }
    using Entry = std::pair<int, std::size_t>;  // cost, index: ties by index
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
    const std::size_t first = index(target_cell_);
    costs_[first] = 0;
    next_[first] = 8;
    open.push({0, first});
    int reached = 0;
    while (!open.empty()) {
        const auto [cost, i] = open.top();
        open.pop();
        if (cost != costs_[i]) {
            continue;
        }
        ++reached;
        const glm::ivec2 cell(static_cast<int>(i) % width_, static_cast<int>(i) / width_);
        for (int n = 0; n < 8; ++n) {
            if (!can_step(clearance, cell, n, radius)) {
                continue;
            }
            const glm::ivec2 other = cell + kNeighbours[n];
            const int c = cost + (n < 4 ? kStraight : kDiagonal);
            if (c > max_cost) {
                continue;
            }
            const std::size_t oi = index(other);
            if (costs_[oi] == kUnreached || c < costs_[oi]) {
                costs_[oi] = c;
                next_[oi] = static_cast<std::int8_t>(n ^ 1);  // back the way: kNeighbours pairs opposites
                open.push({c, oi});
            }
        }
    }
    return reached;
}

int FlowField::cost(glm::ivec2 cell) const {
    if (cell.x < 0 || cell.y < 0 || cell.x >= width_ || cell.y >= height_) {
        return kUnreached;
    }
    return costs_[index(cell)];
}

glm::ivec2 FlowField::next(glm::ivec2 cell) const {
    if (cost(cell) == kUnreached) {
        return glm::ivec2(-1);
    }
    const std::int8_t n = next_[index(cell)];
    return n == 8 ? cell : cell + kNeighbours[n];
}

glm::vec2 FlowField::direction(glm::vec2 position) const {
    const glm::ivec2 cell = cell_at(position);
    if (cost(cell) == kUnreached) {
        return glm::vec2(0.0f);
    }
    const glm::ivec2 to = next(cell);
    const glm::vec2 aim = to == cell ? target_ : cell_centre(to);
    const glm::vec2 d = aim - position;
    const float length = std::sqrt(glm::dot(d, d));
    return length > 1e-4f ? d / length : glm::vec2(0.0f);
}

}  // namespace moteur
