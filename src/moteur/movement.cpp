#include "moteur/movement.hpp"

#include "moteur/fixed_timestep.hpp"
#include "moteur/jobs.hpp"
#include "moteur/profiler.hpp"

#include <algorithm>
#include <cmath>

#include "moteur/collision.hpp"
#include "moteur/collision_scene.hpp"
#include "moteur/navigation.hpp"
#include "moteur/world.hpp"

namespace moteur {

namespace {

constexpr float kDefaultRadius = 0.3f;
constexpr float kDefaultHeight = 1.8f;
constexpr double kBlockedSeconds = 0.5;  // barely moving for this long: blocked
constexpr float kSlowShare = 0.2f;      // "barely": under this share of its speed

// The entities with a Mover, in the order of their identifiers.
// What a mover's job decided, applied after all of them (move_movers()).
struct MoverStep {
    bool moved = false;
    glm::vec3 end{0.0f};       ///< where it goes (the grid: x and z only)
    glm::vec2 facing{0.0f};    ///< where it faces then
};
constexpr std::size_t kMoversPerJob = 32;  // a job's share: a few microseconds of work

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

glm::vec2 flat(glm::vec3 p) {
    return {p.x, p.z};
}

// A path of the plane as points at height `y`.
std::vector<glm::vec3> raised(const std::vector<glm::vec2>& points, float y) {
    std::vector<glm::vec3> out;
    out.reserve(points.size());
    for (const glm::vec2 p : points) {
        out.emplace_back(p.x, y, p.y);
    }
    return out;
}

// Turns a [x, z] array into [x, 0, z]; anything else as it is.
nlohmann::json raise(const nlohmann::json& value) {
    if (value.is_array() && value.size() == 2 && value[0].is_number() && value[1].is_number()) {
        return nlohmann::json::array({value[0], 0.0, value[1]});
    }
    return value;
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

nlohmann::json upgrade_mover(const nlohmann::json& value) {
    if (!value.is_object()) {
        return value;
    }
    nlohmann::json out = value;
    for (const char* key : {"goal", "tick_start", "start"}) {
        if (out.contains(key)) {
            out[key] = raise(out[key]);
        }
    }
    if (out.contains("path") && out["path"].is_array()) {
        for (nlohmann::json& point : out["path"]) {
            point = raise(point);
        }
    }
    return out;
}

CharacterShape character_shape(const entt::registry& registry, entt::entity entity) {
    if (const Collider* collider = registry.try_get<Collider>(entity)) {
        return {collider->radius, std::max(collider->height, collider->radius * 2.0f)};
    }
    return {kDefaultRadius, kDefaultHeight};
}

void Mover::go_to(glm::vec3 point) {
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
    MOTEUR_PROFILE("paths");
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
        const PathResult result = find_path(grid, clearance, plane_position(registry, entity), flat(mover.goal), options);
        ++stats.paths;
        stats.expanded += result.expanded;
        mover.needs_path = false;
        mover.path = raised(result.points, registry.get<Transform>(entity).position.y);
        mover.next_point = 0;
        mover.path_status = result.status;
        mover.slow_ticks = 0;
        // An empty path ends at once: Arrived only if it is at the goal (a Partial one is not).
        mover.state = !mover.path.empty()                 ? MoveState::Moving
                      : result.status == PathStatus::Found ? MoveState::Arrived
                                                           : MoveState::NoPath;
    }
}

void move_movers(entt::registry& registry, const NavGrid& grid, float dt, const FlowField* field, MovementStats& stats) {
    const FlowField* const fields[1] = {field};
    move_movers(registry, grid, dt, fields, stats);
}

void move_movers(entt::registry& registry, const NavGrid& grid, float dt, std::span<const FlowField* const> fields,
                 MovementStats& stats) {
    MOTEUR_PROFILE("movement");
    // Each mover's move over the cores (milestone 9, part 9): a mover reads only its own components
    // and the grid, and writes only its own Mover. The Transforms are written afterwards, in the
    // order of the identifiers (their signals): the same result as one after the other.
    const std::vector<entt::entity> movers = sorted_movers(registry);
    stats.movers += static_cast<int>(movers.size());
    std::vector<MoverStep> steps(movers.size());
    jobs().parallel_for(movers.size(), [&](std::size_t i) {
        const entt::entity entity = movers[i];
        Mover& mover = registry.get<Mover>(entity);
        const glm::vec2 start = plane_position(registry, entity);
        mover.tick_start = registry.get<Transform>(entity).position;
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
                    segment_clear(grid, at, flat(mover.path[mover.next_point + 1]), radius)) {
                    ++mover.next_point;
                }
                while (mover.next_point < mover.path.size() && budget > 0.0f) {
                    const glm::vec2 to = flat(mover.path[mover.next_point]) - at;
                    const float distance = length_of(to);
                    if (distance <= budget) {
                        at = flat(mover.path[mover.next_point]);
                        budget -= distance;
                        ++mover.next_point;
                    } else {
                        at += to * (budget / distance);
                        budget = 0.0f;
                    }
                }
                wanted = at - start;
                if (mover.next_point >= mover.path.size()) {
                    // The end of its path: the goal, or as near as it could go (Partial, None).
                    mover.state = mover.path_status == PathStatus::Found ? MoveState::Arrived : MoveState::NoPath;
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
                // Never past the field's end (aim(): the target, or the cell moved off a target in a wall).
                wanted = direction * std::min({budget, length_of(to_target) - mover.stop_distance,
                                               length_of(field->aim() - start)});
                mover.state = MoveState::Moving;
                break;
            }
        }
        if (wanted == glm::vec2(0.0f)) {
            return;
        }
        const float wanted_length = length_of(wanted);
        mover.facing = turn(mover.facing, wanted / wanted_length, mover.turn_rate * dt);
        const glm::vec2 end = walls ? move_circle(grid, start, wanted, radius) : start + wanted;
        steps[i] = {true, {end.x, 0.0f, end.y}, mover.facing};
    }, kMoversPerJob);
    for (std::size_t i = 0; i < movers.size(); ++i) {
        if (steps[i].moved) {
            const glm::quat rotation = facing_rotation(steps[i].facing);
            registry.patch<Transform>(movers[i], [&](Transform& t) {
                t.position.x = steps[i].end.x;
                t.position.z = steps[i].end.z;
                t.rotation = rotation;
            });
        }
    }
}

void plan_paths(entt::registry& registry, std::span<const NavMesh* const> meshes, int max_paths, MovementStats& stats,
                const NavFilter& filter) {
    MOTEUR_PROFILE("paths");
    for (const entt::entity entity : sorted_movers(registry)) {
        Mover& mover = registry.get<Mover>(entity);
        if (!mover.needs_path || mover.mode != MoveMode::ToPoint) {
            continue;
        }
        const NavMesh* mesh = mover.agent < meshes.size() ? meshes[mover.agent] : nullptr;
        if (mesh == nullptr || mesh->empty()) {
            continue;  // move_movers() sends it straight
        }
        if (stats.paths >= max_paths) {
            mover.state = MoveState::Waiting;
            ++stats.waiting;
            continue;
        }
        const NavPath result = mesh->find_path(registry.get<Transform>(entity).position, mover.goal, filter);
        ++stats.paths;
        stats.expanded += result.expanded;
        mover.needs_path = false;
        mover.path = result.points;
        mover.next_point = 0;
        mover.path_status = result.status;
        mover.slow_ticks = 0;
        mover.state = !mover.path.empty()                 ? MoveState::Moving
                      : result.status == PathStatus::Found ? MoveState::Arrived
                                                           : MoveState::NoPath;
    }
}

namespace {

// The wanted move turned away from the neighbours it would run into within `horizon` seconds
// (time to the closest approach of the two circles, from their velocities): each threat pushes
// sideways from where the other will be, the sooner the harder. Only the kAvoidNeighbours nearest
// within reach count (a crowd is dense: the farther ones change little and cost much); ties by
// identifier. The length of the move is kept. `near` is scratch memory.
constexpr int kAvoidNeighbours = 6;

glm::vec2 avoid_neighbours(const entt::registry& registry, entt::entity self, glm::vec2 position, glm::vec2 wanted,
                           float dt, float radius, HeightSpan heights, const SpatialHash& hash, float horizon,
                           std::vector<std::uint32_t>& near) {
    const float length = length_of(wanted);
    if (length <= 0.0f || dt <= 0.0f) {
        return wanted;
    }
    const glm::vec2 velocity = wanted / dt;
    const float speed = length / dt;
    const float reach = radius * 2.0f + speed * horizon;
    hash.candidates(position - glm::vec2(reach), position + glm::vec2(reach), near);
    // The nearest few, by distance then identifier (a fixed order, whatever the buckets').
    struct Near {
        float distance2;
        std::uint32_t index;
    };
    Near best[kAvoidNeighbours];
    int count = 0;
    const std::vector<SpatialHash::Entry>& entries = hash.entries();
    for (const std::uint32_t index : near) {
        const SpatialHash::Entry& e = entries[index];
        if (e.entity == self || !e.heights.overlaps(heights)) {
            continue;
        }
        const glm::vec2 d = e.position - position;
        const float distance2 = glm::dot(d, d);
        const float limit = reach + e.radius;
        if (distance2 > limit * limit) {
            continue;
        }
        const auto before = [&entries](const Near& x, const Near& y) {
            return x.distance2 != y.distance2
                       ? x.distance2 < y.distance2
                       : entt::to_integral(entries[x.index].entity) < entt::to_integral(entries[y.index].entity);
        };
        const Near candidate{distance2, index};
        if (count < kAvoidNeighbours) {
            best[count++] = candidate;
        } else if (before(candidate, best[count - 1])) {
            best[count - 1] = candidate;
        } else {
            continue;
        }
        for (int k = count - 1; k > 0 && before(best[k], best[k - 1]); --k) {
            std::swap(best[k], best[k - 1]);
        }
    }
    glm::vec2 push(0.0f);
    for (int k = 0; k < count; ++k) {
        const SpatialHash::Entry& e = entries[best[k].index];
        glm::vec2 v(0.0f);
        if (const Mover* m = registry.try_get<Mover>(e.entity)) {
            v = m->facing * m->actual_speed;
        }
        // Relative motion: when are they nearest, and how near?
        const glm::vec2 d = e.position - position;
        const glm::vec2 w = velocity - v;
        const float w2 = glm::dot(w, w);
        if (w2 < 1e-6f) {
            continue;
        }
        const float tau = glm::dot(d, w) / w2;  // time of the closest approach
        if (tau <= 0.0f || tau > horizon) {
            continue;
        }
        const glm::vec2 apart = d - w * tau;  // the other, seen from self, at that time
        const float gap = length_of(apart);
        const float limit = radius + e.radius + 0.1f;
        if (gap >= limit) {
            continue;
        }
        // Away from where the other will be (straight at it: to the right, by a fixed rule).
        const glm::vec2 side = gap > 1e-4f ? -apart / gap : glm::vec2(w.y, -w.x) / std::sqrt(w2);
        push += side * ((limit - gap) / limit) * ((horizon - tau) / horizon);
    }
    if (push == glm::vec2(0.0f)) {
        return wanted;
    }
    const glm::vec2 steered = wanted / length + push;
    const float steered_length = length_of(steered);
    return steered_length > 1e-5f ? steered * (length / steered_length) : wanted;
}

}  // namespace

void move_movers(entt::registry& registry, const CollisionScene& scene, float dt, const NavSources& nav,
                 MovementStats& stats, const ControllerSettings& settings) {
    MOTEUR_PROFILE("movement");
    // Over the cores, as the grid's: a mover writes only its own Mover, but its facing, which its
    // neighbours' avoidance reads (their velocities as the tick starts, whatever the order); the
    // facings and the Transforms are written afterwards, in the order of the identifiers.
    const std::vector<entt::entity> movers = sorted_movers(registry);
    stats.movers += static_cast<int>(movers.size());
    std::vector<MoverStep> steps(movers.size());
    jobs().parallel_for(movers.size(), [&](std::size_t i) {
        thread_local std::vector<std::uint32_t> near;  // scratch of the avoidance
        const entt::entity entity = movers[i];
        Mover& mover = registry.get<Mover>(entity);
        const glm::vec3 position = registry.get<Transform>(entity).position;
        const glm::vec2 start = flat(position);
        mover.tick_start = position;
        const Collider* collider = registry.try_get<Collider>(entity);
        const bool walls = collider == nullptr || collider->blocked_by_walls;
        const NavMesh* mesh = mover.agent < nav.meshes.size() ? nav.meshes[mover.agent] : nullptr;
        if (mesh != nullptr && !mesh->empty()) {
            mover.nav_poly = mesh->locate(position, mover.nav_poly);
        } else {
            mover.nav_poly = 0;
        }
        float budget = mover.speed * dt;

        glm::vec2 wanted(0.0f);
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
                    if (mesh != nullptr && !mesh->empty()) {
                        break;  // waiting for plan_paths
                    }
                    // No mesh for it: straight to the goal.
                    mover.needs_path = false;
                    mover.path = {mover.goal};
                    mover.next_point = 0;
                    mover.path_status = PathStatus::Found;
                    mover.slow_ticks = 0;
                }
                glm::vec2 at = start;
                while (mover.next_point < mover.path.size() && budget > 0.0f) {
                    const glm::vec2 to = flat(mover.path[mover.next_point]) - at;
                    const float distance = length_of(to);
                    if (distance <= budget) {
                        at = flat(mover.path[mover.next_point]);
                        budget -= distance;
                        ++mover.next_point;
                    } else {
                        at += to * (budget / distance);
                        budget = 0.0f;
                    }
                }
                wanted = at - start;
                if (mover.next_point >= mover.path.size()) {
                    mover.state = mover.path_status == PathStatus::Found ? MoveState::Arrived : MoveState::NoPath;
                    mover.mode = MoveMode::Stop;
                } else {
                    mover.state = MoveState::Moving;
                }
                break;
            }
            case MoveMode::FollowField: {
                const NavFlowField* field = mover.field < nav.fields.size() ? nav.fields[mover.field] : nullptr;
                if (field == nullptr || !field->valid()) {
                    mover.state = MoveState::Idle;
                    break;
                }
                const glm::vec2 to_target = flat(field->target()) - start;
                if (length_of(to_target) <= mover.stop_distance) {
                    mover.state = MoveState::Arrived;
                    break;
                }
                const glm::vec2 direction = field->direction(position, mover.nav_poly);
                if (direction == glm::vec2(0.0f)) {
                    mover.state = MoveState::Blocked;  // off the mesh, or outside the field
                    break;
                }
                wanted = direction * std::min(budget, length_of(to_target) - mover.stop_distance);
                mover.state = MoveState::Moving;
                break;
            }
        }
        if (mover.avoid && nav.neighbours != nullptr && wanted != glm::vec2(0.0f)) {
            const float radius = collider ? collider->radius : kDefaultRadius;
            const float height = collider ? collider->height : kDefaultHeight;
            wanted = avoid_neighbours(registry, entity, start, wanted, dt, radius, {position.y, position.y + height},
                                      *nav.neighbours, nav.avoidance_horizon, near);
        }
        // Even standing still, a character in the air keeps falling.
        if (wanted == glm::vec2(0.0f) && mover.grounded) {
            return;
        }
        glm::vec2 facing = mover.facing;
        if (wanted != glm::vec2(0.0f)) {
            facing = turn(mover.facing, wanted / length_of(wanted), mover.turn_rate * dt);
        }
        glm::vec3 end;
        if (walls) {
            CharacterMotion motion;
            motion.grounded = mover.grounded;
            motion.vertical_speed = mover.vertical_speed;
            const CharacterMove moved = move_character(scene, position, {wanted.x, 0.0f, wanted.y}, dt, motion,
                                                       character_shape(registry, entity), settings);
            end = moved.position;
            mover.grounded = moved.motion.grounded;
            mover.vertical_speed = moved.motion.vertical_speed;
        } else {
            end = position + glm::vec3(wanted.x, 0.0f, wanted.y);
        }
        steps[i] = {true, end, facing};
    }, kMoversPerJob);
    for (std::size_t i = 0; i < movers.size(); ++i) {
        if (steps[i].moved) {
            registry.get<Mover>(movers[i]).facing = steps[i].facing;
            const glm::quat rotation = facing_rotation(steps[i].facing);
            registry.patch<Transform>(movers[i], [&](Transform& t) {
                t.position = steps[i].end;
                t.rotation = rotation;
            });
        }
    }
}

void finish_movers(entt::registry& registry, float dt) {
    MOTEUR_PROFILE("movement: finish");
    for (const entt::entity entity : sorted_movers(registry)) {
        Mover& mover = registry.get<Mover>(entity);
        const glm::vec2 travelled = plane_position(registry, entity) - flat(mover.tick_start);
        mover.actual_speed = dt > 0.0f ? length_of(travelled) / dt : 0.0f;
        if (mover.state == MoveState::Moving && mover.actual_speed < mover.speed * kSlowShare) {
            if (++mover.slow_ticks >= ticks_from_seconds(kBlockedSeconds)) {
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
