#include "moteur/world_debug.hpp"

#include <algorithm>

#include "moteur/collision.hpp"
#include "moteur/collision_scene.hpp"
#include "moteur/debug_lines.hpp"
#include "moteur/movement.hpp"
#include "moteur/navigation.hpp"
#include "moteur/nav_grid.hpp"
#include "moteur/pathfinding.hpp"
#include "moteur/variables.hpp"
#include "moteur/visibility.hpp"
#include "moteur/world.hpp"

namespace moteur {

namespace {

// A square on the ground around a cell, `inset` metres inside its edges.
void cell_square(DebugLineBuffer& lines, glm::ivec2 cell, float inset, glm::vec4 color, float height = 0.03f) {
    const float x0 = static_cast<float>(cell.x) + inset, x1 = static_cast<float>(cell.x + 1) - inset;
    const float z0 = static_cast<float>(cell.y) + inset, z1 = static_cast<float>(cell.y + 1) - inset;
    lines.line({x0, height, z0}, {x1, height, z0}, color);
    lines.line({x1, height, z0}, {x1, height, z1}, color);
    lines.line({x1, height, z1}, {x0, height, z1}, color);
    lines.line({x0, height, z1}, {x0, height, z0}, color);
}

bool on(const Variable* variable) {
    return variable != nullptr && variable->as_bool();
}

}  // namespace

WorldDebugFlags WorldDebugFlags::declare(Variables& variables) {
    WorldDebugFlags flags;
    flags.grid = &variables.add_bool("debug.grid", false, "blocked cells (red: opaque, orange: transparent)");
    flags.clearance = &variables.add_bool("debug.clearance", false, "cell clearance (bigger and greener square: more room)");
    flags.colliders = &variables.add_bool("debug.colliders", false, "collision circles (yellow: still this tick)");
    flags.paths = &variables.add_bool("debug.paths", false, "remaining paths");
    flags.field = &variables.add_bool("debug.flowfield", false, "flow field arrows");
    flags.view = &variables.add_bool("debug.view", false, "field of view cells");
    flags.collision = &variables.add_bool("debug.collision", false, "level collision triangles and capsules");
    flags.navmesh = &variables.add_bool("debug.navmesh", false, "navigation mesh triangles");
    return flags;
}

bool WorldDebugFlags::any() const {
    return on(grid) || on(clearance) || on(colliders) || on(paths) || on(field) || on(view) || on(collision) || on(navmesh);
}

void draw_world_debug(DebugLineBuffer& lines, const WorldDebugFlags& flags, const WorldDebugSources& sources,
                      glm::vec3 centre, float radius, float alpha) {
    const int reach = static_cast<int>(radius) + 2;
    glm::ivec2 low = cell_at({centre.x, centre.z}) - reach;
    glm::ivec2 high = cell_at({centre.x, centre.z}) + reach;
    if (sources.grid != nullptr) {
        low = glm::max(low, glm::ivec2(0));
        high = glm::min(high, glm::ivec2(sources.grid->width() - 1, sources.grid->height() - 1));
    }
    const bool grid = on(flags.grid) && sources.grid != nullptr;
    const bool clearance = on(flags.clearance) && sources.grid != nullptr && sources.clearance != nullptr;
    const bool view = on(flags.view) && sources.view != nullptr;
    if (grid || clearance || view) {
        for (int j = low.y; j <= high.y; ++j) {
            for (int i = low.x; i <= high.x; ++i) {
                const glm::ivec2 cell(i, j);
                if (grid && !sources.grid->walkable(cell)) {
                    cell_square(lines, cell, 0.05f, sources.grid->opaque(cell) ? glm::vec4(1, 0.2f, 0.2f, 1) : glm::vec4(1, 0.6f, 0.1f, 1), 1.6f);
                }
                if (clearance && sources.grid->walkable(cell)) {
                    const float c = sources.clearance->clearance(cell) / ClearanceMap::kMaxClearance;
                    cell_square(lines, cell, 0.45f - 0.4f * c, glm::vec4(1.0f - c, c, 0.2f, 1));
                }
                if (view && sources.view->visible(cell)) {
                    cell_square(lines, cell, 0.15f, glm::vec4(0.3f, 1.0f, 0.4f, 1), 0.04f);
                }
            }
        }
    }
    if (on(flags.field) && sources.field != nullptr && sources.field->valid()) {
        for (int j = low.y; j <= high.y; ++j) {
            for (int i = low.x; i <= high.x; ++i) {
                const glm::ivec2 next = sources.field->next({i, j});
                if (next.x < 0 || next == glm::ivec2(i, j)) {
                    continue;
                }
                const glm::vec2 a = cell_centre({i, j});
                const glm::vec2 b = a + (cell_centre(next) - a) * 0.4f;
                lines.line({a.x, 0.05f, a.y}, {b.x, 0.05f, b.y}, {0.3f, 0.7f, 1.0f, 1});
                lines.circle({b.x, 0.05f, b.y}, {0, 1, 0}, 0.04f, {0.3f, 0.7f, 1.0f, 1}, 6);
            }
        }
    }
    if (on(flags.collision) && sources.scene != nullptr) {
        Aabb area;
        area.add(centre - glm::vec3(radius, radius, radius));
        area.add(centre + glm::vec3(radius, radius, radius));
        std::vector<std::uint32_t> near;
        sources.scene->box_triangles(area, kSurfaceAll, near);
        for (const std::uint32_t index : near) {
            const CollisionTriangle& t = sources.scene->triangles()[index];
            const glm::vec4 color = t.kind == SurfaceKind::Walkable ? glm::vec4(0.3f, 1.0f, 0.4f, 1)
                                    : t.kind == SurfaceKind::Wall   ? glm::vec4(1.0f, 0.3f, 0.3f, 1)
                                    : t.kind == SurfaceKind::Obstacle ? glm::vec4(1.0f, 0.6f, 0.1f, 1)
                                                                      : glm::vec4(0.7f, 0.3f, 1.0f, 1);
            const glm::vec3 lift = t.normal * 0.01f;  // off the surface, not fighting with it
            lines.line(t.a + lift, t.b + lift, color);
            lines.line(t.b + lift, t.c + lift, color);
            lines.line(t.c + lift, t.a + lift, color);
        }
    }
    if (on(flags.navmesh) && sources.navmesh != nullptr) {
        Aabb area;
        area.add(centre - glm::vec3(radius, radius, radius));
        area.add(centre + glm::vec3(radius, radius, radius));
        draw_nav_mesh(lines, *sources.navmesh, area);
    }
    if (sources.world == nullptr) {
        return;
    }
    const World& world = *sources.world;
    const entt::registry& registry = world.registry();
    if (on(flags.colliders)) {
        for (const auto& [entity, collider] : registry.view<Collider>().each()) {
            if (registry.all_of<Hidden>(entity)) {
                continue;
            }
            const glm::vec3 p = world.world_position(entity, alpha);
            const glm::vec4 color = entity == sources.highlight ? glm::vec4(1.0f)
                                    : collider.push_weight == 0  ? glm::vec4(1, 0.9f, 0.2f, 1)
                                                                 : glm::vec4(1, 0.35f, 0.3f, 1);
            lines.circle({p.x, p.y + 0.05f, p.z}, {0, 1, 0}, collider.radius, color, 24, true);
        }
    }
    if (on(flags.paths)) {
        for (const auto& [entity, mover] : registry.view<Mover>().each()) {
            if (mover.mode != MoveMode::ToPoint || mover.next_point >= mover.path.size()) {
                continue;
            }
            const glm::vec3 p = world.world_position(entity, alpha);
            glm::vec3 from(p.x, p.y + 0.08f, p.z);
            for (std::size_t i = mover.next_point; i < mover.path.size(); ++i) {
                const glm::vec3 to = mover.path[i] + glm::vec3(0.0f, 0.08f, 0.0f);
                lines.line(from, to, {0.2f, 1.0f, 1.0f, 1}, true);
                lines.circle(to, {0, 1, 0}, 0.08f, {0.2f, 1.0f, 1.0f, 1}, 8, true);
                from = to;
            }
        }
    }
    if (on(flags.collision)) {
        for (const auto& [entity, collider] : registry.view<Collider>().each()) {
            if (registry.all_of<Hidden>(entity)) {
                continue;
            }
            const glm::vec3 p = world.world_position(entity, alpha);
            const float r = collider.radius;
            const float top = std::max(collider.height, 2.0f * r);
            const glm::vec4 color = entity == sources.highlight ? glm::vec4(1.0f) : glm::vec4(0.4f, 0.8f, 1.0f, 1);
            lines.circle(p + glm::vec3(0, r, 0), {0, 1, 0}, r, color, 16, true);
            lines.circle(p + glm::vec3(0, top - r, 0), {0, 1, 0}, r, color, 16, true);
            for (const glm::vec3 side : {glm::vec3(r, 0, 0), glm::vec3(-r, 0, 0), glm::vec3(0, 0, r), glm::vec3(0, 0, -r)}) {
                lines.line(p + side + glm::vec3(0, r, 0), p + side + glm::vec3(0, top - r, 0), color, true);
            }
        }
    }
}

}  // namespace moteur
