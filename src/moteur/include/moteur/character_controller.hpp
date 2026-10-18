#pragma once

#include <glm/glm.hpp>

#include "moteur/collision_scene.hpp"
#include "moteur/reflection.hpp"

namespace moteur {

// Moving a character against the level in 3D (milestone 9, part 5): a capsule standing on its feet
// slides along the walls, climbs steps up to a height, walks up and down slopes up to an angle,
// sticks to the ground going down stairs and falls from ledges. Kinematic: no mass, no physics
// simulation (that is milestone 10); the logic stays deterministic: plain float arithmetic, no
// trigonometry, contacts in a fixed order, a bounded number of passes.

/// The shape of a character for the collisions: a vertical capsule whose bottom is at its feet (the
/// Transform's position).
struct CharacterShape {
    float radius = 0.35f;  ///< metres
    float height = 1.8f;   ///< metres, from the feet to the top of the head (at least 2 x radius)
};

/// How characters move against the level; the same for every character of a game, as a rule.
struct ControllerSettings {
    float step_height = 0.35f;        ///< the highest step climbed without a jump, metres
    /// The steepest ground stood on, as the lowest y of its normal: 0.707 is 45 degrees (cos). Steeper
    /// faces are walls. (A cosine rather than an angle: the logic computes no trigonometry.)
    float min_ground_normal_y = 0.7071f;
    float snap_distance = 0.4f;       ///< how far below the feet the ground is followed (down stairs), metres
    float gravity = 20.0f;            ///< metres per second squared, while falling
    float max_fall_speed = 40.0f;     ///< metres per second
    float skin = 0.005f;              ///< the gap kept from the walls, metres
    int max_passes = 6;               ///< passes pushing the capsule out of the walls per step
};
MOTEUR_REFLECT(ControllerSettings,
    MOTEUR_FIELD(step_height, {.min = 0.0, .max = 5.0, .unit = "m", .flags = kFieldDefault | kFieldLogic}),
    MOTEUR_FIELD(min_ground_normal_y, {.min = 0.0, .max = 1.0, .doc = "the cosine of the steepest walkable slope",
                                       .flags = kFieldDefault | kFieldLogic}),
    MOTEUR_FIELD(snap_distance, {.min = 0.0, .max = 5.0, .unit = "m", .flags = kFieldDefault | kFieldLogic}),
    MOTEUR_FIELD(gravity, {.min = 0.0, .max = 1000.0, .unit = "m/s2", .flags = kFieldDefault | kFieldLogic}),
    MOTEUR_FIELD(max_fall_speed, {.min = 0.0, .max = 1000.0, .unit = "m/s", .flags = kFieldDefault | kFieldLogic}),
    MOTEUR_FIELD(skin, {.min = 0.0, .max = 0.1, .unit = "m", .flags = kFieldDefault | kFieldLogic}),
    MOTEUR_FIELD(max_passes, {.min = 1.0, .max = 32.0, .flags = kFieldDefault | kFieldLogic}));

/// Where a character is with respect to the ground, carried from one move to the next.
struct CharacterMotion {
    bool grounded = true;                      ///< standing on ground (false: falling)
    float vertical_speed = 0.0f;               ///< metres per second, up positive (falling: negative)
    glm::vec3 ground_normal{0.0f, 1.0f, 0.0f}; ///< the ground's normal while grounded
    SurfaceKind ground_kind = SurfaceKind::Walkable;  ///< what it stands on while grounded
};

/// What move_character() did.
struct CharacterMove {
    glm::vec3 position{0.0f};  ///< the new feet position
    CharacterMotion motion;    ///< to give to the next move
    bool hit_wall = false;     ///< something stopped part of the horizontal move
    glm::vec3 wall_normal{0.0f};  ///< the last wall's normal on the plane (unit) when hit_wall
    bool landed = false;       ///< was falling and reached the ground during this move
    bool stepped = false;      ///< climbed or went down a step (the feet changed height by more than 1 cm on ground)
};

/// Moves a character standing at `feet` by `horizontal` (its y is ignored) for `dt` seconds:
/// in steps no longer than half its radius, each pushed out of the walls (sliding along them),
/// then put on the ground under it if that is within a step up or `snap_distance` down, else it
/// starts to fall (gravity, `max_fall_speed`) until it lands. A step under a ceiling too low for the
/// capsule is refused (the step's move is undone). Only SurfaceKind Walkable, Wall and Obstacle
/// block; Void is ground that lets it fall. Deterministic.
CharacterMove move_character(const CollisionScene& scene, glm::vec3 feet, glm::vec3 horizontal, float dt,
                             const CharacterMotion& motion, const CharacterShape& shape,
                             const ControllerSettings& settings = {});

/// The capsule at `feet` pushed out of the walls on the plane (its height kept): what the separation
/// of characters calls after pushing them apart. Gives `feet` when nothing overlaps.
glm::vec3 push_out_of_level(const CollisionScene& scene, glm::vec3 feet, const CharacterShape& shape,
                            const ControllerSettings& settings = {});

/// True if the capsule at `feet` overlaps no wall nor obstacle above its step (where it would stand).
bool character_fits(const CollisionScene& scene, glm::vec3 feet, const CharacterShape& shape,
                    const ControllerSettings& settings = {});

/// The ground a character at `feet` would stand on: within a step above and `snap_distance` below,
/// walkable by its slope. nullopt when it would fall.
std::optional<GroundHit> ground_under(const CollisionScene& scene, glm::vec3 feet, const ControllerSettings& settings = {});

}  // namespace moteur
