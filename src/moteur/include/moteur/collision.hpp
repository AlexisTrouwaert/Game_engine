#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <vector>

#include "moteur/nav_grid.hpp"
#include "moteur/spatial_hash.hpp"

namespace moteur {

// Collisions on the ground plane (milestone 6, part 3). Characters are circles on the plane (x, z of
// their Transform); they never enter a cell of the NavGrid that is not walkable, and push each other
// apart. No physics: no mass, no bounce, no stacking.
//
// Determinism: plain float arithmetic (+ - * / sqrt, exactly rounded in IEEE 754), no trigonometry,
// entities handled in the order of their identifiers, pairs in a fixed order, ties broken by rules.

// An entity that collides: a circle of `radius` metres centred on its Transform's (x, z).
struct Collider {
    float radius = 0.35f;
    // What it is (bits), and what it collides with: two colliders push each other apart only if
    // each one's layer is in the other's mask.
    std::uint32_t layer = 1;
    std::uint32_t mask = ~0u;
    // Who gives way when two overlap: each moves by the share of the other's weight (equal weights:
    // half each). 0: immovable this tick (an obstacle, a monster striking); two immovable never move.
    int push_weight = 1;
    // false: walls do not stop it (a ghost); other colliders still do, as their masks say.
    bool blocked_by_walls = true;
};

// True if the circle touches no blocked cell (nor the outside of the grid).
bool circle_fits(const NavGrid& grid, glm::vec2 centre, float radius);

// The circle pushed out of the blocked cells it overlaps, along the shortest way out of each (the
// closest point of the cell's square), in a few passes. A centre deep inside a wall goes to the
// nearest face. Gives `centre` when it already fits.
glm::vec2 push_out_of_walls(const NavGrid& grid, glm::vec2 centre, float radius);

// Moves a circle by `delta`, in steps shorter than half its radius, pushed out of the walls after
// each step: against a wall, the part of the move along the wall is kept (it slides), and it goes
// round corners. Returns the new centre.
glm::vec2 move_circle(const NavGrid& grid, glm::vec2 from, glm::vec2 delta, float radius);

// The nearest place within `search_cells` cells where the circle fits: `centre` itself if it fits,
// else a cell centre (the nearest; ties: row then column order). nullopt if there is none.
std::optional<glm::vec2> nearest_fit(const NavGrid& grid, glm::vec2 centre, float radius, int search_cells = 8);

// True if a circle of `radius` can go in a straight line from `from` to `to` without touching a
// blocked cell (a circle cast; radius 0: a ray through walkable cells only).
bool segment_clear(const NavGrid& grid, glm::vec2 from, glm::vec2 to, float radius);

struct SeparationSettings {
    float stiffness = 0.6f;  // the share of an overlap removed per iteration (soft)
    int iterations = 2;
};

// What the last separate_colliders() did, for the tools.
struct CollisionStats {
    int colliders = 0;
    int pairs_tested = 0;
    int overlaps = 0;
};

// The collision system of a tick, after the moves: fills `hash` with every entity having a
// Transform and a Collider (for the queries of the rest of the tick), then pushes overlapping
// colliders apart (soft, `settings`) and out of the walls. Writes the Transforms it moves with
// registry.patch. Returns its statistics.
CollisionStats separate_colliders(entt::registry& registry, const NavGrid& grid, SpatialHash& hash,
                                  const SeparationSettings& settings = {});

// The plane position of an entity's Transform.
glm::vec2 plane_position(const entt::registry& registry, entt::entity entity);

}  // namespace moteur
