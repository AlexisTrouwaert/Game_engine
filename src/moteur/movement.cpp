#include "moteur/movement.hpp"

#include <algorithm>
#include <cmath>

#include "moteur/collision.hpp"
#include "moteur/world.hpp"

namespace moteur {

namespace {

constexpr float kDefaultRadius = 0.3f;
constexpr int kBlockedTicks = 30;       // half a second at 60 Hz barely moving: blocked
constexpr float kSlowShare = 0.2f;      // "barely": under this share of its speed

// The entities with a Mover, in the order of their identifiers.
std::vector<entt::entity> sorted_movers(entt::registry& registry) {
    std::vector<entt::entity> entities;
    for (const entt::entity entity : registry.view<Transform, Mover>()) {
        entities.push_back(entity);
    }
    std::sort(entities.begin(), entities.end(),
              [](entt::entity a, entt::entity b) { return entt::to_integral(a) < entt::to_integral(b); });
    return entities;
}

float length_of(glm::vec2 v) {
    return std::sqrt(glm::dot(v, v));
}

// Turns `facing` towards `wanted` (both unit) by a share of the way; opposite: through the left.
glm::vec2 turn(glm::vec2 facing, glm::vec2 wanted, float share) {
    if (glm::dot(facing, wanted) < -0.99f) {
        wanted = glm::vec2(-facing.y, facing.x);
    }
    const glm::vec2 mixed = facing + (wanted - facing) * std::min(share, 1.0f);
    const float length = length_of(mixed);
    return length > 1e-6f ? mixed / length : wanted;
}

}  // namespace

void Mover::go_to(glm::vec2 point) {
    mode = MoveMode::ToPoint;
    goal = point;
    needs_path = true;
}

void Mover::move(glm::vec2 wanted) {
    mode = MoveMode::Direct;
    direction = wanted;
    needs_path = false;
    path.clear();
}

void Mover::follow_field(float stop_within) {
    mode = MoveMode::FollowField;
    stop_distance = stop_within;
    needs_path = false;
    path.clear();
}

void Mover::stop() {
    mode = MoveMode::Stop;
    needs_path = false;
    path.clear();
}

glm::quat facing_rotation(glm::vec2 facing) {
    // Angle a from +z towards +x: cos a = facing.y (z), sin a = facing.x. Half-angle formulas.
    const float c = std::clamp(facing.y, -1.0f, 1.0f);
    const float half_cos = std::sqrt((1.0f + c) * 0.5f);
    float half_sin = std::sqrt((1.0f - c) * 0.5f);
    if (facing.x < 0.0f) {
        half_sin = -half_sin;
    }
    return glm::quat(half_cos, 0.0f, half_sin, 0.0f);
}

void plan_paths(entt::registry& registry, const NavGrid& grid, const ClearanceMap& clearance, int max_paths,
                MovementStats& stats, int max_nodes) {
    for (const entt::entity entity : sorted_movers(registry)) {
        Mover& mover = registry.get<Mover>(entity);
        if (!mover.needs_path || mover.mode != MoveMode::ToPoint) {
            continue;
        }
        if (stats.paths >= max_paths) {
            mover.state = MoveState::Waiting;
            ++stats.waiting;
            continue;
        }
        const Collider* collider = registry.try_get<Collider>(entity);
        PathOptions options;
        options.radius = collider ? collider->radius : kDefaultRadius;
        options.max_nodes = max_nodes;
        const PathResult result = find_path(grid, clearance, plane_position(registry, entity), mover.goal, options);
        ++stats.paths;
        stats.expanded += result.expanded;
        mover.needs_path = false;
        mover.path = result.points;
        mover.next_point = 0;
        mover.path_status = result.status;
        mover.slow_ticks = 0;
        mover.state = result.status == PathStatus::None ? MoveState::NoPath
                      : mover.path.empty()               ? MoveState::Arrived
                                                         : MoveState::Moving;
    }
}

void move_movers(entt::registry& registry, const NavGrid& grid, float dt, const FlowField* field, MovementStats& stats) {
    const FlowField* const fields[1] = {field};
    move_movers(registry, grid, dt, fields, stats);
}

void move_movers(entt::registry& registry, const NavGrid& grid, float dt, std::span<const FlowField* const> fields,
                 MovementStats& stats) {
    for (const entt::entity entity : sorted_movers(registry)) {
        Mover& mover = registry.get<Mover>(entity);
        ++stats.movers;
        const glm::vec2 start = plane_position(registry, entity);
        mover.tick_start = start;
        const Collider* collider = registry.try_get<Collider>(entity);
        const float radius = collider ? collider->radius : kDefaultRadius;
        const bool walls = collider == nullptr || collider->blocked_by_walls;
        float budget = mover.speed * dt;  // metres it may walk this tick

        glm::vec2 wanted(0.0f);  // the move of this tick
        switch (mover.mode) {
            case MoveMode::Stop:
                mover.state = MoveState::Idle;
                break;
            case MoveMode::Direct: {
                const float amount = std::min(length_of(mover.direction), 1.0f);
                if (amount > 0.0f) {
                    wanted = mover.direction / length_of(mover.direction) * (budget * amount);
                    mover.state = MoveState::Moving;
                } else {
                    mover.state = MoveState::Idle;
                }
                break;
            }
            case MoveMode::ToPoint: {
                if (mover.needs_path) {
                    break;  // waiting for plan_paths
                }
                glm::vec2 at = start;
                // A shortcut: the point after the next one in a straight line (pushed off the way).
                if (mover.next_point + 1 < mover.path.size() &&
                    segment_clear(grid, at, mover.path[mover.next_point + 1], radius)) {
                    ++mover.next_point;
                }
                while (mover.next_point < mover.path.size() && budget > 0.0f) {
                    const glm::vec2 to = mover.path[mover.next_point] - at;
                    const float distance = length_of(to);
                    if (distance <= budget) {
                        at = mover.path[mover.next_point];
                        budget -= distance;
                        ++mover.next_point;
                    } else {
                        at += to * (budget / distance);
                        budget = 0.0f;
                    }
                }
                wanted = at - start;
                if (mover.next_point >= mover.path.size()) {
                    mover.state = MoveState::Arrived;
                    mover.mode = MoveMode::Stop;
                } else {
                    mover.state = MoveState::Moving;
                }
                break;
            }
            case MoveMode::FollowField: {
                const FlowField* field = mover.field < fields.size() ? fields[mover.field] : nullptr;
                if (field == nullptr || !field->valid()) {
                    mover.state = MoveState::Idle;
                    break;
                }
                const glm::vec2 to_target = field->target() - start;
                if (length_of(to_target) <= mover.stop_distance) {
                    mover.state = MoveState::Arrived;
                    break;
                }
                const glm::vec2 direction = field->direction(start);
                if (direction == glm::vec2(0.0f)) {
                    mover.state = MoveState::Blocked;  // outside the field
                    break;
                }
                wanted = direction * std::min(budget, length_of(to_target) - mover.stop_distance);
                mover.state = MoveState::Moving;
                break;
            }
        }
        if (wanted == glm::vec2(0.0f)) {
            continue;
        }
        const float wanted_length = length_of(wanted);
        mover.facing = turn(mover.facing, wanted / wanted_length, mover.turn_rate * dt);
        const glm::vec2 end = walls ? move_circle(grid, start, wanted, radius) : start + wanted;
        const glm::quat rotation = facing_rotation(mover.facing);
        registry.patch<Transform>(entity, [&](Transform& t) {
            t.position.x = end.x;
            t.position.z = end.y;
            t.rotation = rotation;
        });
    }
}

void finish_movers(entt::registry& registry, float dt) {
    for (const entt::entity entity : sorted_movers(registry)) {
        Mover& mover = registry.get<Mover>(entity);
        const glm::vec2 travelled = plane_position(registry, entity) - mover.tick_start;
        mover.actual_speed = dt > 0.0f ? length_of(travelled) / dt : 0.0f;
        if (mover.state == MoveState::Moving && mover.actual_speed < mover.speed * kSlowShare) {
            if (++mover.slow_ticks >= kBlockedTicks) {
                mover.state = MoveState::Blocked;
                mover.slow_ticks = 0;
                if (mover.mode == MoveMode::ToPoint) {
                    mover.needs_path = true;  // try again from here (every half second while blocked)
                }
            }
        } else {
            mover.slow_ticks = 0;
        }
    }
}

}  // namespace moteur
