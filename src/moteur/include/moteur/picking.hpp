#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <vector>

#include "moteur/camera3d.hpp"
#include "moteur/collision_scene.hpp"
#include "moteur/reflection.hpp"

namespace moteur {

class NavGrid;
class NavMesh;

// Sight and selection in 3D (milestone 9, part 7): lines of sight against the level's geometry,
// the grid of what blocks the sight on one floor (for the fields of view and the fog of war), and
// what the mouse points at: rays from the camera against the shapes of what can be selected, with
// priorities, and the ground under the pointer put on the navigation mesh. Deterministic: the
// logic (a click, at the tick) reads these, never the GPU's id buffer (which only shows the hover).

/// What a selectable thing is, for the priorities of pick() (the higher wins when several are under
/// the pointer: a monster before the item at its feet, before the ground).
enum class PickKind : std::uint8_t {
    Ground = 0,   ///< not an entity: the walkable ground (PickHit::entity is null)
    Interactive,  ///< a lever, a door, a chest
    Item,         ///< something lying on the ground
    Character,    ///< a monster, a non-player character
};
MOTEUR_REFLECT_ENUM(PickKind, "ground", "interactive", "item", "character");

/// The shape an entity is selected by: a vertical capsule standing on the entity's position (a
/// character: Collider's radius and height are a good start), or a box around a point of it.
struct Selectable {
    PickKind kind = PickKind::Character;  ///< what it is (the priorities of pick())
    bool box = false;               ///< false: a capsule (radius, height); true: a box (centre, half_size)
    float radius = 0.4f;            ///< capsule: metres (a little more than the Collider: easier to click)
    float height = 1.8f;            ///< capsule: metres from the entity's position up
    glm::vec3 centre{0.0f, 0.5f, 0.0f};    ///< box: in the entity's space (scaled, not rotated)
    glm::vec3 half_size{0.5f};             ///< box: metres
    bool enabled = true;            ///< false: never picked (a dead monster)
};
MOTEUR_REFLECT(Selectable,
    MOTEUR_FIELD(kind, {.flags = kFieldDefault | kFieldLogic}),
    MOTEUR_FIELD(box, {.doc = "a box rather than a capsule", .flags = kFieldDefault | kFieldLogic}),
    MOTEUR_FIELD(radius, {.min = 0.0, .max = 50.0, .unit = "m", .flags = kFieldDefault | kFieldLogic}),
    MOTEUR_FIELD(height, {.min = 0.0, .max = 100.0, .unit = "m", .flags = kFieldDefault | kFieldLogic}),
    MOTEUR_FIELD(centre, {.unit = "m", .flags = kFieldDefault | kFieldLogic}),
    MOTEUR_FIELD(half_size, {.min = 0.0, .unit = "m", .flags = kFieldDefault | kFieldLogic}),
    MOTEUR_FIELD(enabled, {.flags = kFieldDefault | kFieldLogic}));

/// What pick() found.
struct PickHit {
    entt::entity entity = entt::null;  ///< null for the ground
    PickKind kind = PickKind::Ground;  ///< what it is (Ground: no entity)
    glm::vec3 point{0.0f};             ///< where the ray met it (world, metres)
    float distance = 0.0f;             ///< along the ray
};

/// The ray's first hit with the shape of each enabled Selectable (of entities with a Transform), in
/// front of the level's geometry (a monster half hidden by a pillar is picked by the part one sees);
/// among those, the highest PickKind wins, then the nearest, then the lowest identifier. With no
/// entity, the ground: the ray against the walkable geometry (the nearest Walkable triangle not
/// behind a wall), put on `navmesh` when given (within 1 m across). nullopt when nothing is met.
/// `ray.direction` must be unit. Deterministic.
std::optional<PickHit> pick(const entt::registry& registry, const Ray& ray, const CollisionScene* level,
                            const NavMesh* navmesh = nullptr, float max_distance = 500.0f);

/// The distance along `ray` to the capsule standing at `feet` (radius, height), or nullopt.
std::optional<float> ray_capsule(const Ray& ray, glm::vec3 feet, float radius, float height);
/// The distance along `ray` to the box [low, high], or nullopt (0 when the ray starts inside).
std::optional<float> ray_box(const Ray& ray, glm::vec3 low, glm::vec3 high);

/// Whether eyes at `a` and `b` see each other: nothing of `mask` (walls and floors by default) on
/// the segment between them. Symmetric (tested both ways): if A sees B, B sees A.
bool line_of_sight(const CollisionScene& level, glm::vec3 a, glm::vec3 b, std::uint32_t mask = kSurfaceSight);

/// The grid of one floor for the fields of view and the fog of war (FieldOfView, ExploredMap): a
/// cell of `width` x `height` metres from `origin` (x, z) is walkable when `navmesh` has ground on
/// that floor under its centre (within half a metre of `level`), and opaque when walls stand on that
/// floor in it, between `level` + 0.3 m and `level` + `eye_height` (what hides a character's body).
/// A floor above or below does not count: what is under the bridge stays hidden from the bridge's
/// grid only if a wall of that floor hides it. O(width x height) queries.
NavGrid floor_sight_grid(const NavMesh& navmesh, const CollisionScene& level, glm::vec2 origin, int width, int height,
                         float level_height, float eye_height = 1.6f);

}  // namespace moteur
