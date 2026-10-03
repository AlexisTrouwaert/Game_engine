#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <span>
#include <vector>

#include "moteur/nav_grid.hpp"
#include "moteur/pathfinding.hpp"

namespace moteur {

// Moving characters on the ground plane (milestone 6, part 5): one system for the hero and the
// creatures. A Mover says how the entity wants to move (to a point by a path, in a direction, along
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

enum class MoveMode : std::uint8_t {
    Stop,         // stays where it is
    ToPoint,      // to `goal`, by a path (planned by plan_paths)
    Direct,       // along `direction` (keys or stick), sliding on the walls; no path
    FollowField,  // along the FlowField given to move_movers, until within stop_distance of its target
};

enum class MoveState : std::uint8_t {
    Idle,     // not asked to move
    Waiting,  // a path is asked for and not planned yet (budget of the tick spent)
    Moving,
    Arrived,  // reached its goal (ToPoint) or came within stop_distance (FollowField)
    Blocked,  // asked to move but barely moved for a while (crowd, another character in a corridor)
    NoPath,   // ToPoint and no way at all (the start does not fit anywhere)
};

struct Mover {
    float speed = 3.0f;          // metres per second at most
    float turn_rate = 14.0f;     // how fast it faces its way: share of the turn done per second
    float stop_distance = 0.0f;  // FollowField: stops within this distance of the target (reach)
    std::uint8_t field = 0;      // FollowField: which of the fields given to move_movers (by size)

    MoveMode mode = MoveMode::Stop;
    glm::vec2 goal{0.0f};        // ToPoint
    glm::vec2 direction{0.0f};   // Direct: length at most 1, scales the speed

    // The path (ToPoint), from plan_paths: the points still to walk through.
    std::vector<glm::vec2> path;
    std::size_t next_point = 0;
    PathStatus path_status = PathStatus::None;
    bool needs_path = false;

    // Results, written by the systems.
    MoveState state = MoveState::Idle;
    glm::vec2 facing{0.0f, 1.0f};   // unit, on the plane; the Transform's rotation follows it
    float actual_speed = 0.0f;      // metres per second travelled during the last tick
    glm::vec2 tick_start{0.0f};     // where it was when move_movers began
    int slow_ticks = 0;             // ticks in a row it barely moved while asked to

    void go_to(glm::vec2 point);     // plans a path (again) to the point
    void move(glm::vec2 direction);  // Direct; (0, 0) stops
    void follow_field(float stop_within);
    void stop();
};

// The rotation about +y that turns +z (a glTF model's front) towards `facing` (unit, on the plane),
// computed with square roots only.
glm::quat facing_rotation(glm::vec2 facing);

struct MovementStats {
    int movers = 0;
    int paths = 0;     // A* searches this tick
    int expanded = 0;  // their cells taken from the open lists
    int waiting = 0;   // movers still waiting for a path (budget spent)
};

// Plans the paths asked for (Mover::needs_path), at most `max_paths` per tick in the order of the
// entities' identifiers; the others wait (state Waiting) for the next tick. The radius is the
// Collider's when the entity has one, else 0.3 m. Adds to `stats`.
void plan_paths(entt::registry& registry, const NavGrid& grid, const ClearanceMap& clearance, int max_paths,
                MovementStats& stats, int max_nodes = 20000);

// Moves every entity with a Transform and a Mover by its wanted move for `dt` seconds. `fields`:
// the flow fields of the FollowField movers, chosen by Mover::field (one per size of creature;
// missing or null: they stand still).
void move_movers(entt::registry& registry, const NavGrid& grid, float dt, std::span<const FlowField* const> fields,
                 MovementStats& stats);
// The same with a single field (or none).
void move_movers(entt::registry& registry, const NavGrid& grid, float dt, const FlowField* field, MovementStats& stats);

// After the collisions: the speed really travelled, arrival and blocked states.
void finish_movers(entt::registry& registry, float dt);

}  // namespace moteur
