#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <span>
#include <vector>

#include "moteur/character_controller.hpp"
#include "moteur/nav_grid.hpp"
#include "moteur/navigation.hpp"
#include "moteur/pathfinding.hpp"
#include "moteur/reflection.hpp"
#include "moteur/spatial_hash.hpp"

namespace moteur {

// Moving characters (milestone 6, part 5; in 3D since milestone 9, part 5): one system for the hero
// and the creatures, on the ground plane of a NavGrid or against the CollisionScene of a level. A Mover says how the entity wants to move (to a point by a path, in a direction, along
// a flow field); the systems of the tick plan the paths, move the entities (sliding on the walls,
// with move_circle when they have a Collider), turn them, and measure the speed really travelled,
// for the animation. Deterministic: entities in the order of their identifiers, no trigonometry.
//
// The order of a tick (written once by the game):
//   1. the game's decisions          mover.go_to(...), mover.move(...), mover.follow_field()...
//   2. plan_paths(...)               A* for the movers that asked, within a budget per tick
//   3. move_movers(...)              the wanted moves, against the walls
//   4. separate_colliders(...)       characters pushed apart (collision.hpp)
//   5. finish_movers(...)            speed really travelled, arrival, blocked
//   6. animation                     Animator::set_move_speed(mover.actual_speed / scale)

/// How a Mover wants to move; set by Mover::go_to(), Mover::move(), Mover::follow_field() and
/// Mover::stop().
enum class MoveMode : std::uint8_t {
    Stop,         ///< stays where it is
    ToPoint,      ///< to `goal`, by a path (planned by plan_paths)
    Direct,       ///< along `direction` (keys or stick), sliding on the walls; no path
    FollowField,  ///< along the FlowField given to move_movers, until within stop_distance of its target
};
MOTEUR_REFLECT_ENUM(MoveMode, "stop", "to_point", "direct", "follow_field");

/// What a Mover did during the last tick, written by plan_paths(), move_movers() and finish_movers().
/// A state lasts until the next of these systems changes it: Arrived from a ToPoint move, for
/// example, turns into Idle at the next move_movers() (the mode went back to Stop).
enum class MoveState : std::uint8_t {
    Idle,     ///< not asked to move
    Waiting,  ///< a path is asked for and not planned yet (budget of the tick spent)
    Moving,   ///< moving towards its goal, in its direction or along its field this tick
    Arrived,  ///< reached its goal (ToPoint, a Found path) or came within stop_distance (FollowField)
    Blocked,  ///< asked to move but barely moved for a while (crowd, another character in a corridor),
              ///< or outside its flow field
    /// ToPoint and the goal cannot be reached: no way at all (path_status None: the start does not
    /// fit anywhere), or it walked a Partial path to its end, the reachable point nearest the goal.
    /// The mode goes back to Stop, as for Arrived.
    NoPath,
};
MOTEUR_REFLECT_ENUM(MoveState, "idle", "waiting", "moving", "arrived", "blocked", "no_path");

/// An entity that moves: a component, used with a Transform. The game sets the wish (through go_to(),
/// move(), follow_field(), stop(), and the tuning fields); the systems of the tick (plan_paths(),
/// move_movers(), finish_movers()) write the rest. Distances in metres; speeds and directions on the
/// plane (x, z), heights given by the ground (in a 3D level) or 0 (on a NavGrid).
struct Mover {
    float speed = 3.0f;          ///< metres per second at most
    /// How fast it faces its way: each tick it turns by `turn_rate` x dt of what remains (at most
    /// all of it), so about the share of the turn done per second. It turns only while it moves.
    float turn_rate = 14.0f;
    float stop_distance = 0.0f;  ///< FollowField: stops within this distance of the target (reach)
    std::uint8_t field = 0;      ///< FollowField: which of the fields given to move_movers (by size)

    MoveMode mode = MoveMode::Stop;  ///< the current wish; ToPoint goes back to Stop on arrival
    glm::vec3 goal{0.0f};        ///< ToPoint (on a NavGrid only x and z count)
    glm::vec2 direction{0.0f};   ///< Direct: length at most 1 (longer is cut to 1), scales the speed

    /// The path (ToPoint), from plan_paths: the points still to walk through.
    std::vector<glm::vec3> path;
    std::size_t next_point = 0;  ///< index in `path` of the point it walks to now
    /// How the last search for this mover ended: Partial means it walks to the nearest reachable
    /// point, and will report NoPath there (Arrived only at the end of a Found path).
    PathStatus path_status = PathStatus::None;
    /// True when plan_paths() must plan a path (set by go_to() and when blocked); the mover stands
    /// still until it is planned.
    bool needs_path = false;

    /// Results, written by the systems.
    MoveState state = MoveState::Idle;
    glm::vec2 facing{0.0f, 1.0f};   ///< unit, on the plane; the Transform's rotation follows it
    float actual_speed = 0.0f;      ///< metres per second travelled during the last tick
    glm::vec3 tick_start{0.0f};     ///< where it was when move_movers began
    int slow_ticks = 0;             ///< ticks in a row it barely moved while asked to
    /// In a 3D level (move_movers() with a CollisionScene): standing on the ground, or falling, and
    /// how fast vertically. Unused on a NavGrid.
    ///@{
    bool grounded = true;
    float vertical_speed = 0.0f;
    ///@}
    /// In a 3D level: which navigation mesh it walks on (an index in NavSources::meshes: one per
    /// size of creature), the polygon under it (kept from tick to tick to find it cheaply; not
    /// saved), and whether it steers round the movers ahead of it (local avoidance: the crowds;
    /// off for the hero, who goes where the player says).
    ///@{
    std::uint8_t agent = 0;
    NavPolyRef nav_poly = 0;
    bool avoid = false;
    ///@}

    void go_to(glm::vec3 point);     ///< plans a path (again) to the point: ToPoint, at the next plan_paths()
    void go_to(glm::vec2 point) { go_to(glm::vec3(point.x, 0.0f, point.y)); }  ///< on the plane (x, z), height 0
    void move(glm::vec2 direction);  ///< Direct; (0, 0) stops
    /// Follows the flow field chosen by `field` until within `stop_within` metres of its target
    /// (sets stop_distance).
    void follow_field(float stop_within);
    /// Stops where it is: mode Stop, the path dropped.
    void stop();
};
// The keys of the saves of milestone 7 ("turn", "stop"...) are still read (aliases).
MOTEUR_REFLECT(Mover,
    MOTEUR_FIELD(speed, {.min = 0.0, .max = 1000.0, .unit = "m/s", .doc = "the highest speed",
                         .flags = kFieldDefault | kFieldLogic}),
    MOTEUR_FIELD(turn_rate, {.min = 0.0, .max = 1000.0, .unit = "1/s", .doc = "how fast it faces its way",
                             .flags = kFieldDefault | kFieldLogic, .aliases = {"turn"}}),
    MOTEUR_FIELD(stop_distance, {.min = 0.0, .unit = "m", .doc = "follow field: stops this near its target",
                                 .flags = kFieldDefault | kFieldLogic, .aliases = {"stop"}}),
    MOTEUR_FIELD(field, {.doc = "follow field: which of the fields given to move_movers()",
                         .flags = kFieldDefault | kFieldLogic}),
    MOTEUR_FIELD(mode, {.flags = kFieldEdit | kFieldSave | kFieldLogic}),
    MOTEUR_FIELD(goal, {.unit = "m", .flags = kFieldEdit | kFieldSave | kFieldLogic}),
    MOTEUR_FIELD(direction, {.flags = kFieldEdit | kFieldSave | kFieldLogic}),
    MOTEUR_FIELD(path, {.unit = "m", .flags = kFieldSave | kFieldLogic}),
    MOTEUR_FIELD(next_point, {.flags = kFieldSave | kFieldLogic, .aliases = {"next"}}),
    MOTEUR_FIELD(path_status, {.flags = kFieldEdit | kFieldReadOnly | kFieldSave | kFieldLogic, .aliases = {"status"}}),
    MOTEUR_FIELD(needs_path, {.flags = kFieldSave | kFieldLogic}),
    MOTEUR_FIELD(state, {.flags = kFieldEdit | kFieldReadOnly | kFieldSave | kFieldLogic}),
    MOTEUR_FIELD(facing, {.flags = kFieldEdit | kFieldReadOnly | kFieldSave | kFieldLogic}),
    MOTEUR_FIELD(actual_speed, {.unit = "m/s", .flags = kFieldEdit | kFieldReadOnly | kFieldSave | kFieldLogic,
                                .aliases = {"actual"}}),
    MOTEUR_FIELD(tick_start, {.flags = kFieldSave | kFieldLogic, .aliases = {"start"}}),
    MOTEUR_FIELD(slow_ticks, {.flags = kFieldSave | kFieldLogic, .aliases = {"slow"}}),
    MOTEUR_FIELD(agent, {.doc = "3D: which navigation mesh (by creature size)", .flags = kFieldDefault | kFieldLogic}),
    MOTEUR_FIELD(avoid, {.doc = "3D: steers round the movers ahead (crowds)", .flags = kFieldDefault | kFieldLogic}),
    MOTEUR_FIELD(grounded, {.flags = kFieldEdit | kFieldReadOnly | kFieldSave | kFieldLogic}),
    MOTEUR_FIELD(vertical_speed, {.unit = "m/s", .flags = kFieldEdit | kFieldReadOnly | kFieldSave | kFieldLogic}));

/// Mover's saved form before milestone 9, part 5 (goal, path and tick start on the plane: [x, z]),
/// read as 3D points ([x, 0, z]): the upgrade registered with the "mover" component type.
nlohmann::json upgrade_mover(const nlohmann::json& value);

/// The rotation about +y that turns +z (a glTF model's front) towards `facing` (unit, on the plane),
/// computed with square roots only. `facing` is (x, z); a non-unit vector gives a wrong angle.
glm::quat facing_rotation(glm::vec2 facing);

/// What the movement systems did, for the tools. The systems add to it: reset it each tick.
struct MovementStats {
    int movers = 0;    ///< entities move_movers() handled
    int paths = 0;     ///< A* searches this tick
    int expanded = 0;  ///< their cells taken from the open lists
    int waiting = 0;   ///< movers still waiting for a path (budget spent)
};

/// Plans the paths asked for (Mover::needs_path), at most `max_paths` per tick in the order of the
/// entities' identifiers; the others wait (state Waiting) for the next tick. The radius is the
/// Collider's when the entity has one, else 0.3 m. `max_nodes` is PathOptions::max_nodes of each
/// search. Sets the path, path_status and state (Moving; for an empty path, Arrived when Found, else
/// NoPath, which move_movers() keeps). Adds to `stats`. Same thread rules as separate_colliders().
void plan_paths(entt::registry& registry, const NavGrid& grid, const ClearanceMap& clearance, int max_paths,
                MovementStats& stats, int max_nodes = 20000);

/// Moves every entity with a Transform and a Mover by its wanted move for `dt` seconds. `fields`:
/// the flow fields of the FollowField movers, chosen by Mover::field (one per size of creature;
/// missing or null: they stand still). Without a Collider an entity moves as a 0.3 m circle blocked
/// by walls. Writes the Transform's x, z and rotation with registry.patch, only for the entities that
/// move; sets Mover::tick_start, Mover::facing and Mover::state. Adds to `stats`.
void move_movers(entt::registry& registry, const NavGrid& grid, float dt, std::span<const FlowField* const> fields,
                 MovementStats& stats);
/// The same with a single field (or none).
void move_movers(entt::registry& registry, const NavGrid& grid, float dt, const FlowField* field, MovementStats& stats);

/// What the movers of a 3D level walk on: the navigation meshes (one per size of creature, chosen
/// by Mover::agent), the flow fields of the FollowField movers (chosen by Mover::field), and the
/// colliders of the previous separation (for the local avoidance). Missing or null entries count as
/// absent. Nothing is owned.
struct NavSources {
    std::span<const NavMesh* const> meshes;       ///< by Mover::agent
    std::span<const NavFlowField* const> fields;  ///< by Mover::field
    const SpatialHash* neighbours = nullptr;  ///< as separate_colliders() left it; null: no avoidance
    float avoidance_horizon = 0.5f;           ///< seconds looked ahead for collisions to avoid
};

/// In a 3D level: plans the paths asked for (Mover::needs_path) on the movers' navigation meshes
/// (Mover::agent), at most `max_paths` per tick in the order of their identifiers; the others wait.
/// Sets the path (3D corners), path_status and state as plan_paths() on a grid does. A mover whose
/// mesh is missing goes straight to its goal.
void plan_paths(entt::registry& registry, std::span<const NavMesh* const> meshes, int max_paths, MovementStats& stats,
                const NavFilter& filter = {});

/// In a 3D level: moves every entity with a Transform and a Mover against `scene` (move_character():
/// slides on the walls, climbs steps, follows the ground, falls), its capsule from its Collider
/// (radius, height; without one 0.3 x 1.8 m). Paths are walked on the plane, the height coming from
/// the ground; a ToPoint mover still waiting for a path (no mesh) goes straight to its goal.
/// FollowField reads its NavFlowField at the polygon under it (Mover::nav_poly, kept up to date).
/// Movers with Mover::avoid steer round those ahead of them (time to collision against the six
/// nearest neighbours' last velocities, within `nav.avoidance_horizon`; no trigonometry). Writes the
/// Transform's position and rotation with registry.patch, and Mover::grounded, vertical_speed and
/// nav_poly. Adds to `stats`.
void move_movers(entt::registry& registry, const CollisionScene& scene, float dt, const NavSources& nav,
                 MovementStats& stats, const ControllerSettings& settings = {});

/// The capsule of an entity for the controller: its Collider's radius and height, else 0.3 x 1.8 m.
CharacterShape character_shape(const entt::registry& registry, entt::entity entity);

/// After the collisions: the speed really travelled (on the plane), arrival and blocked states. Sets
/// Mover::actual_speed from the move since move_movers() (0 when `dt` <= 0). A Moving mover under a
/// fifth of its speed for half a second of ticks becomes Blocked, and a ToPoint one asks for a new
/// path from where it is.
void finish_movers(entt::registry& registry, float dt);

}  // namespace moteur
