#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <vector>

#include "moteur/character_controller.hpp"
#include "moteur/collision_scene.hpp"
#include "moteur/nav_grid.hpp"
#include "moteur/reflection.hpp"
#include "moteur/spatial_hash.hpp"

namespace moteur {

// Collisions on the ground plane (milestone 6, part 3). Characters are circles on the plane (x, z of
// their Transform); they never enter a cell of the NavGrid that is not walkable, and push each other
// apart. No physics: no mass, no bounce, no stacking.
//
// Determinism: plain float arithmetic (+ - * / sqrt, exactly rounded in IEEE 754), no trigonometry,
// entities handled in the order of their identifiers, pairs in a fixed order, ties broken by rules.

/// An entity that collides: a circle of `radius` metres centred on its Transform's (x, z).
/// A component: separate_colliders() handles every entity having a Transform and a Collider.
struct Collider {
    float radius = 0.35f;  ///< in metres; also the radius it is entered with in the SpatialHash
    /// What it is (bits), and what it collides with: two colliders push each other apart only if
    /// each one's layer is in the other's mask. The layer is also what SpatialHash queries filter on.
    ///@{
    std::uint32_t layer = 1;
    std::uint32_t mask = ~0u;
    ///@}
    /// Who gives way when two overlap: each moves by the share of the other's weight (equal weights:
    /// half each). 0: immovable this tick (an obstacle, a monster striking); two immovable never move.
    int push_weight = 1;
    /// false: walls do not stop it (a ghost); other colliders still do, as their masks say.
    bool blocked_by_walls = true;
    /// In a 3D level: the capsule's height from the feet, metres; two colliders push each other only
    /// where their heights overlap (one on a bridge, one under it do not). Unused on a NavGrid.
    float height = 1.8f;
};
MOTEUR_REFLECT(Collider,
    MOTEUR_FIELD(radius, {.min = 0.0, .max = 100.0, .unit = "m", .flags = kFieldDefault | kFieldLogic}),
    MOTEUR_FIELD(layer, {.doc = "what it is (bits)", .flags = kFieldDefault | kFieldLogic}),
    MOTEUR_FIELD(mask, {.doc = "what it collides with (bits)", .flags = kFieldDefault | kFieldLogic}),
    MOTEUR_FIELD(push_weight, {.min = 0.0, .doc = "who gives way: 0 never moves", .flags = kFieldDefault | kFieldLogic}),
    MOTEUR_FIELD(blocked_by_walls, {.flags = kFieldDefault | kFieldLogic}),
    MOTEUR_FIELD(height, {.min = 0.0, .max = 100.0, .unit = "m", .doc = "3D: the capsule's height",
                          .flags = kFieldDefault | kFieldLogic}));

/// True if the circle touches no blocked cell (nor the outside of the grid). Positions and radii of
/// these functions are in metres on the ground plane, one NavGrid cell per metre; exactly touching a
/// wall still fits. Cost grows with the number of cells under the circle.
bool circle_fits(const NavGrid& grid, glm::vec2 centre, float radius);

/// The circle pushed out of the blocked cells it overlaps, along the shortest way out of each (the
/// closest point of the cell's square), in a few passes. A centre deep inside a wall goes to the
/// nearest face. Gives `centre` when it already fits. If it is still stuck after 4 passes (between
/// walls closer than its size), the nearest_fit() within 4 cells; failing that, the last position,
/// which may still overlap a wall.
glm::vec2 push_out_of_walls(const NavGrid& grid, glm::vec2 centre, float radius);

/// Moves a circle by `delta`, in steps shorter than half its radius, pushed out of the walls after
/// each step: against a wall, the part of the move along the wall is kept (it slides), and it goes
/// round corners. Returns the new centre. Steps are at least 5 cm and at most 64, so a very long
/// `delta` takes longer steps (and may cut through a thin wall).
glm::vec2 move_circle(const NavGrid& grid, glm::vec2 from, glm::vec2 delta, float radius);

/// The nearest place within `search_cells` cells where the circle fits: `centre` itself if it fits,
/// else a cell centre (the nearest; ties: row then column order). nullopt if there is none.
/// Searches a square of (2 `search_cells` + 1) cells on a side: O(search_cells squared) circle tests.
std::optional<glm::vec2> nearest_fit(const NavGrid& grid, glm::vec2 centre, float radius, int search_cells = 8);

/// True if a circle of `radius` can go in a straight line from `from` to `to` without touching a
/// blocked cell (a circle cast; radius 0: a ray through walkable cells only).
bool segment_clear(const NavGrid& grid, glm::vec2 from, glm::vec2 to, float radius);

/// How hard separate_colliders() pushes overlapping colliders apart.
struct SeparationSettings {
    float stiffness = 0.6f;  ///< the share of an overlap removed per iteration (soft), in [0, 1]
    int iterations = 2;      ///< passes over every pair per tick; 0 disables the pushing (walls still act)
};

/// What the last separate_colliders() did, for the tools.
struct CollisionStats {
    int colliders = 0;     ///< entities with a Transform and a Collider
    int pairs_tested = 0;  ///< pairs whose layers and masks match and that can move, summed over the iterations
    int overlaps = 0;      ///< of those, the ones found overlapping (pushed apart), summed over the iterations
};

/// The collision system of a tick, after the moves: fills `hash` with every entity having a
/// Transform and a Collider (for the queries of the rest of the tick), then pushes overlapping
/// colliders apart (soft, `settings`) and out of the walls. Writes the Transforms it moves with
/// registry.patch (so `on_update` observers see them); only x and z change. Colliders with
/// Collider::blocked_by_walls false are not pushed out of walls. Deterministic: entities are handled
/// in the order of their identifiers. Call it from the thread that owns the registry, with nothing
/// else reading or writing it. Allocates a temporary
/// list of the colliders. Returns its statistics.
CollisionStats separate_colliders(entt::registry& registry, const NavGrid& grid, SpatialHash& hash,
                                  const SeparationSettings& settings = {});

/// The collision system of a tick in a 3D level: as the NavGrid version, but colliders push each other
/// only where their heights overlap (from the Transform's y, Collider::height up), and are pushed out
/// of the level's walls on the plane (push_out_of_level()). Fills `hash` with every collider (its
/// height span with it, for the queries). Same rules of order and threads.
CollisionStats separate_colliders(entt::registry& registry, const CollisionScene& scene, SpatialHash& hash,
                                  const SeparationSettings& settings = {}, const ControllerSettings& controller = {});

/// The plane position of an entity's Transform: its (x, z), in metres. The entity must have a
/// Transform (undefined behaviour otherwise, as for entt's `registry.get`).
glm::vec2 plane_position(const entt::registry& registry, entt::entity entity);

}  // namespace moteur
