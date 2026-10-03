#include "moteur/collision.hpp"

#include <algorithm>
#include <cmath>

#include "moteur/world.hpp"

namespace moteur {

namespace {

constexpr float kSlack = 1e-5f;  // a circle exactly touching a wall fits (rounding)

glm::vec2 closest_in_cell(glm::vec2 p, glm::ivec2 cell) {
    const glm::vec2 low(static_cast<float>(cell.x), static_cast<float>(cell.y));
    return glm::clamp(p, low, low + glm::vec2(1.0f));
}

bool overlaps(glm::vec2 centre, float radius, glm::ivec2 cell) {
    const glm::vec2 d = centre - closest_in_cell(centre, cell);
    const float reach = std::max(radius - kSlack, 0.0f);
    return glm::dot(d, d) < reach * reach || (radius <= kSlack && d == glm::vec2(0.0f));
}

// The distance squared from point p to segment [a, b].
float point_segment_distance2(glm::vec2 p, glm::vec2 a, glm::vec2 b) {
    const glm::vec2 ab = b - a;
    const float length2 = glm::dot(ab, ab);
    float t = length2 > 0.0f ? glm::dot(p - a, ab) / length2 : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    const glm::vec2 d = p - (a + ab * t);
    return glm::dot(d, d);
}

// Whether segment [a, b] crosses the closed square of `cell` (Liang-Barsky clipping).
bool segment_hits_cell(glm::vec2 a, glm::vec2 b, glm::ivec2 cell) {
    const glm::vec2 low(static_cast<float>(cell.x), static_cast<float>(cell.y));
    const glm::vec2 high = low + glm::vec2(1.0f);
    const glm::vec2 d = b - a;
    float t0 = 0.0f, t1 = 1.0f;
    for (int axis = 0; axis < 2; ++axis) {
        if (d[axis] == 0.0f) {
            if (a[axis] < low[axis] || a[axis] > high[axis]) {
                return false;
            }
            continue;
        }
        float near = (low[axis] - a[axis]) / d[axis];
        float far = (high[axis] - a[axis]) / d[axis];
        if (near > far) {
            std::swap(near, far);
        }
        t0 = std::max(t0, near);
        t1 = std::min(t1, far);
        if (t0 > t1) {
            return false;
        }
    }
    return true;
}

// The distance squared between segment [a, b] and the square of `cell`, when they do not cross: the
// closest pair has an end of the segment or a corner of the square.
float segment_cell_distance2(glm::vec2 a, glm::vec2 b, glm::ivec2 cell) {
    const glm::vec2 da = a - closest_in_cell(a, cell);
    const glm::vec2 db = b - closest_in_cell(b, cell);
    float best = std::min(glm::dot(da, da), glm::dot(db, db));
    const glm::vec2 low(static_cast<float>(cell.x), static_cast<float>(cell.y));
    for (const glm::vec2 corner : {low, low + glm::vec2(1.0f, 0.0f), low + glm::vec2(0.0f, 1.0f), low + glm::vec2(1.0f)}) {
        best = std::min(best, point_segment_distance2(corner, a, b));
    }
    return best;
}

// Directions for two circles exactly on the same point: chosen from their identifiers, not at random.
const glm::vec2 kDirections[8] = {{1.0f, 0.0f},  {0.70710678f, 0.70710678f},   {0.0f, 1.0f},  {-0.70710678f, 0.70710678f},
                                  {-1.0f, 0.0f}, {-0.70710678f, -0.70710678f}, {0.0f, -1.0f}, {0.70710678f, -0.70710678f}};

}  // namespace

bool circle_fits(const NavGrid& grid, glm::vec2 centre, float radius) {
    const glm::ivec2 from = cell_at(centre - glm::vec2(radius));
    const glm::ivec2 to = cell_at(centre + glm::vec2(radius));
    for (int j = from.y; j <= to.y; ++j) {
        for (int i = from.x; i <= to.x; ++i) {
            if (!grid.walkable({i, j}) && overlaps(centre, radius, {i, j})) {
                return false;
            }
        }
    }
    return true;
}

glm::vec2 push_out_of_walls(const NavGrid& grid, glm::vec2 centre, float radius) {
    glm::vec2 p = centre;
    for (int pass = 0; pass < 4; ++pass) {
        bool moved = false;
        const glm::ivec2 from = cell_at(p - glm::vec2(radius));
        const glm::ivec2 to = cell_at(p + glm::vec2(radius));
        for (int j = from.y; j <= to.y; ++j) {
            for (int i = from.x; i <= to.x; ++i) {
                if (grid.walkable({i, j}) || !overlaps(p, radius, {i, j})) {
                    continue;
                }
                const glm::vec2 q = closest_in_cell(p, {i, j});
                const glm::vec2 d = p - q;
                const float distance2 = glm::dot(d, d);
                if (distance2 > 0.0f) {
                    p = q + d * (radius / std::sqrt(distance2));
                } else {
                    // The centre inside the cell: out by the nearest face (ties: -x, +x, -z, +z).
                    const float faces[4] = {p.x - static_cast<float>(i), static_cast<float>(i + 1) - p.x,
                                            p.y - static_cast<float>(j), static_cast<float>(j + 1) - p.y};
                    int best = 0;
                    for (int f = 1; f < 4; ++f) {
                        if (faces[f] < faces[best]) {
                            best = f;
                        }
                    }
                    switch (best) {
                        case 0: p.x = static_cast<float>(i) - radius; break;
                        case 1: p.x = static_cast<float>(i + 1) + radius; break;
                        case 2: p.y = static_cast<float>(j) - radius; break;
                        default: p.y = static_cast<float>(j + 1) + radius; break;
                    }
                }
                moved = true;
            }
        }
        if (!moved) {
            return p;
        }
    }
    // Still in a wall (deep inside a thick one, or between walls closer than its size): the nearest
    // place where it fits, if any near.
    if (circle_fits(grid, p, radius)) {
        return p;
    }
    return nearest_fit(grid, p, radius, 4).value_or(p);
}

glm::vec2 move_circle(const NavGrid& grid, glm::vec2 from, glm::vec2 delta, float radius) {
    const float length = std::sqrt(glm::dot(delta, delta));
    const float step = std::max(radius * 0.5f, 0.05f);
    const int steps = std::clamp(static_cast<int>(std::ceil(length / step)), 1, 64);
    const glm::vec2 part = delta / static_cast<float>(steps);
    glm::vec2 p = from;
    for (int i = 0; i < steps; ++i) {
        p = push_out_of_walls(grid, p + part, radius);
    }
    return p;
}

std::optional<glm::vec2> nearest_fit(const NavGrid& grid, glm::vec2 centre, float radius, int search_cells) {
    if (circle_fits(grid, centre, radius)) {
        return centre;
    }
    const glm::ivec2 middle = cell_at(centre);
    std::optional<glm::vec2> best;
    float best_distance2 = 0.0f;
    for (int j = middle.y - search_cells; j <= middle.y + search_cells; ++j) {
        for (int i = middle.x - search_cells; i <= middle.x + search_cells; ++i) {
            const glm::vec2 c = cell_centre({i, j});
            if (!grid.walkable({i, j}) || !circle_fits(grid, c, radius)) {
                continue;
            }
            const glm::vec2 d = c - centre;
            const float distance2 = glm::dot(d, d);
            if (!best || distance2 < best_distance2) {
                best = c;
                best_distance2 = distance2;
            }
        }
    }
    return best;
}

bool segment_clear(const NavGrid& grid, glm::vec2 from, glm::vec2 to, float radius) {
    const glm::ivec2 low = cell_at(glm::min(from, to) - glm::vec2(radius));
    const glm::ivec2 high = cell_at(glm::max(from, to) + glm::vec2(radius));
    const float reach = std::max(radius - kSlack, 0.0f);
    const glm::vec2 d = to - from;
    for (int j = low.y; j <= high.y; ++j) {
        // Only the cells near the part of the segment within this row (grown by the radius).
        float x0 = std::min(from.x, to.x), x1 = std::max(from.x, to.x);
        if (d.y != 0.0f) {
            float t0 = (static_cast<float>(j) - radius - from.y) / d.y;
            float t1 = (static_cast<float>(j + 1) + radius - from.y) / d.y;
            if (t0 > t1) {
                std::swap(t0, t1);
            }
            t0 = std::max(t0, 0.0f);
            t1 = std::min(t1, 1.0f);
            if (t0 > t1) {
                continue;
            }
            x0 = std::min(from.x + d.x * t0, from.x + d.x * t1);
            x1 = std::max(from.x + d.x * t0, from.x + d.x * t1);
        }
        const int first = std::max(low.x, static_cast<int>(std::floor(x0 - radius)));
        const int last = std::min(high.x, static_cast<int>(std::floor(x1 + radius)));
        for (int i = first; i <= last; ++i) {
            if (grid.walkable({i, j})) {
                continue;
            }
            if (segment_hits_cell(from, to, {i, j}) || segment_cell_distance2(from, to, {i, j}) < reach * reach) {
                return false;
            }
        }
    }
    return true;
}

glm::vec2 plane_position(const entt::registry& registry, entt::entity entity) {
    const glm::vec3 p = registry.get<Transform>(entity).position;
    return {p.x, p.z};
}

CollisionStats separate_colliders(entt::registry& registry, const NavGrid& grid, SpatialHash& hash,
                                  const SeparationSettings& settings) {
    struct Item {
        entt::entity entity;
        glm::vec2 position;
        glm::vec2 start;
        Collider collider;
    };
    std::vector<Item> items;
    for (auto [entity, transform, collider] : registry.view<Transform, Collider>().each()) {
        const glm::vec2 p(transform.position.x, transform.position.z);
        items.push_back({entity, p, p, collider});
    }
    std::sort(items.begin(), items.end(),
              [](const Item& a, const Item& b) { return entt::to_integral(a.entity) < entt::to_integral(b.entity); });
    hash.clear();
    for (const Item& item : items) {
        hash.insert(item.entity, item.position, item.collider.radius, item.collider.layer);
    }

    CollisionStats stats;
    stats.colliders = static_cast<int>(items.size());
    std::vector<std::uint32_t> near;
    for (int iteration = 0; iteration < settings.iterations; ++iteration) {
        for (std::size_t i = 0; i < items.size(); ++i) {
            Item& a = items[i];
            const float reach_box = a.collider.radius + 0.25f;
            hash.candidates(a.start - glm::vec2(reach_box), a.start + glm::vec2(reach_box), near);
            std::sort(near.begin(), near.end());
            for (const std::uint32_t j : near) {
                if (j <= i) {
                    continue;  // each pair once, the lower index first
                }
                Item& b = items[j];
                if ((a.collider.layer & b.collider.mask) == 0 || (b.collider.layer & a.collider.mask) == 0) {
                    continue;
                }
                const int wa = a.collider.push_weight;
                const int wb = b.collider.push_weight;
                if (wa <= 0 && wb <= 0) {
                    continue;
                }
                ++stats.pairs_tested;
                glm::vec2 d = b.position - a.position;
                const float reach = a.collider.radius + b.collider.radius;
                const float distance2 = glm::dot(d, d);
                if (distance2 >= reach * reach) {
                    continue;
                }
                ++stats.overlaps;
                const float distance = std::sqrt(distance2);
                if (distance == 0.0f) {
                    d = kDirections[(entt::to_integral(a.entity) * 31u + entt::to_integral(b.entity)) % 8u];
                } else {
                    d /= distance;
                }
                const float overlap = (reach - distance) * settings.stiffness;
                // Each gives way by the other's share of the weight; an immovable one not at all.
                float share_a = 0.0f, share_b = 0.0f;
                if (wa <= 0) {
                    share_b = 1.0f;
                } else if (wb <= 0) {
                    share_a = 1.0f;
                } else {
                    share_a = static_cast<float>(wb) / static_cast<float>(wa + wb);
                    share_b = 1.0f - share_a;
                }
                a.position -= d * (overlap * share_a);
                b.position += d * (overlap * share_b);
            }
        }
    }

    for (Item& item : items) {
        if (item.collider.blocked_by_walls) {
            item.position = push_out_of_walls(grid, item.position, item.collider.radius);
        }
        if (item.position != item.start) {
            registry.patch<Transform>(item.entity, [&item](Transform& t) {
                t.position.x = item.position.x;
                t.position.z = item.position.y;
            });
        }
    }
    return stats;
}

}  // namespace moteur
