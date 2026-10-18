#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "moteur/character_controller.hpp"
#include "moteur/collision.hpp"
#include "moteur/collision_scene.hpp"
#include "moteur/level_collision.hpp"
#include "moteur/model_import.hpp"
#include "moteur/movement.hpp"
#include "moteur/spatial_hash.hpp"
#include "moteur/world.hpp"

#include "test_levels.hpp"

// Milestone 9, part 5: the logic in 3D: the level's collision triangles in a bounding volume
// hierarchy, the queries, and characters moving against it (walls, steps, slopes, bridges, ledges).

namespace {

// A small deterministic generator (the tests must not depend on the library's distributions).
struct Lcg {
    std::uint32_t state = 12345u;
    float next() {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) / 16777216.0f;
    }
};

using test_levels::box;
using test_levels::test_level;

// Walks a character from `feet` by `direction` (unit, on the plane) at 4 m/s for `seconds` in ticks
// of 1/30 s; returns the last move and keeps every height in `heights`.
moteur::CharacterMove walk(const moteur::CollisionScene& scene, glm::vec3 feet, glm::vec3 direction, float seconds,
                           std::vector<float>* heights = nullptr, moteur::CharacterMotion motion = {}) {
    constexpr float dt = 1.0f / 30.0f;
    moteur::CharacterMove move;
    move.position = feet;
    move.motion = motion;
    const int ticks = static_cast<int>(seconds / dt + 0.5f);
    for (int i = 0; i < ticks; ++i) {
        move = moteur::move_character(scene, move.position, direction * (4.0f * dt), dt, move.motion, {});
        if (heights != nullptr) {
            heights->push_back(move.position.y);
        }
    }
    return move;
}

}  // namespace

TEST_CASE("collision scene: node names give the kind of collision") {
    CHECK(moteur::is_collision_node("COL_wall_north"));
    CHECK(moteur::is_collision_node("ucx_crate"));
    CHECK_FALSE(moteur::is_collision_node("collar"));
    CHECK(moteur::surface_kind_of_node("COL_wall_north") == moteur::SurfaceKind::Wall);
    CHECK(moteur::surface_kind_of_node("COL_Walkable") == moteur::SurfaceKind::Walkable);
    CHECK(moteur::surface_kind_of_node("UCX_obstacle.001") == moteur::SurfaceKind::Obstacle);
    CHECK(moteur::surface_kind_of_node("COL_void") == moteur::SurfaceKind::Void);
    CHECK_FALSE(moteur::surface_kind_of_node("COL_crate").has_value());  // by the slope then
    CHECK_FALSE(moteur::surface_kind_of_node("crate").has_value());
}

TEST_CASE("collision scene: the tree answers as testing every triangle would") {
    Lcg random;
    moteur::CollisionMeshData mesh;
    for (int i = 0; i < 600; ++i) {
        const glm::vec3 centre(random.next() * 40.0f - 20.0f, random.next() * 10.0f, random.next() * 40.0f - 20.0f);
        const auto corner = [&] {
            return centre + glm::vec3(random.next() - 0.5f, random.next() - 0.5f, random.next() - 0.5f) * 3.0f;
        };
        mesh.add_triangle(corner(), corner(), corner(),
                          i % 3 == 0 ? moteur::SurfaceKind::Wall : moteur::SurfaceKind::Walkable);
    }
    moteur::CollisionScene scene;
    CHECK(scene.add(mesh, glm::mat4(1.0f)) == 600);
    CHECK_FALSE(scene.raycast({0, 5, 0}, {1, 0, 0}, 100.0f).has_value());  // not built: sees nothing
    scene.build();
    CHECK(scene.built());
    CHECK(scene.node_count() > 1);

    int hits = 0;
    for (int r = 0; r < 300; ++r) {
        const glm::vec3 origin(random.next() * 50.0f - 25.0f, random.next() * 12.0f - 1.0f, random.next() * 50.0f - 25.0f);
        glm::vec3 direction(random.next() - 0.5f, random.next() - 0.5f, random.next() - 0.5f);
        direction /= std::sqrt(glm::dot(direction, direction));
        const std::uint32_t mask = r % 2 == 0 ? moteur::kSurfaceAll : moteur::kSurfaceWall;
        // Brute force: the nearest triangle on the ray.
        float best = 1e30f;
        for (std::uint32_t i = 0; i < scene.triangles().size(); ++i) {
            const moteur::CollisionTriangle& t = scene.triangles()[i];
            if ((moteur::surface_bit(t.kind) & mask) == 0) {
                continue;
            }
            // Ray against the triangle's plane, then inside by the edges.
            const float denominator = glm::dot(t.normal, direction);
            if (std::fabs(denominator) < 1e-6f) {
                continue;
            }
            const float distance = glm::dot(t.normal, t.a - origin) / denominator;
            if (distance < 0.0f || distance > 60.0f) {
                continue;
            }
            const glm::vec3 p = origin + direction * distance;
            const bool inside = glm::dot(glm::cross(t.b - t.a, p - t.a), t.normal) >= 0.0f &&
                                glm::dot(glm::cross(t.c - t.b, p - t.b), t.normal) >= 0.0f &&
                                glm::dot(glm::cross(t.a - t.c, p - t.c), t.normal) >= 0.0f;
            if (inside) {
                best = std::min(best, distance);
            }
        }
        const std::optional<moteur::RayHit> hit = scene.raycast(origin, direction, 60.0f, mask);
        if (best < 1e30f) {
            REQUIRE(hit.has_value());
            CHECK(hit->distance == doctest::Approx(best).epsilon(1e-3));
            ++hits;
        } else {
            CHECK_FALSE(hit.has_value());
        }
    }
    CHECK(hits > 20);

    // Boxes and spheres: the same triangles as by testing them all.
    for (int q = 0; q < 50; ++q) {
        const glm::vec3 c(random.next() * 40.0f - 20.0f, random.next() * 10.0f, random.next() * 40.0f - 20.0f);
        const float radius = 0.5f + random.next() * 2.0f;
        std::vector<moteur::Contact> contacts;
        scene.sphere_contacts(c, radius, moteur::kSurfaceAll, contacts);
        std::size_t expected = 0;
        for (const moteur::CollisionTriangle& t : scene.triangles()) {
            const glm::vec3 p = moteur::closest_point_on_triangle(c, t.a, t.b, t.c);
            expected += glm::dot(p - c, p - c) < radius * radius ? 1 : 0;
        }
        CHECK(contacts.size() == expected);
        for (std::size_t i = 1; i < contacts.size(); ++i) {
            CHECK(contacts[i - 1].depth >= contacts[i].depth);  // deepest first
        }
    }
}

TEST_CASE("collision scene: the ground under a point, on two heights") {
    const moteur::CollisionScene scene = test_level();
    // Under the bridge: the ground; on it: the deck.
    const auto under = scene.ground_at({10.5f, 0.1f, 3.0f}, 0.35f, 0.4f, 0.7071f);
    REQUIRE(under.has_value());
    CHECK(under->height == doctest::Approx(0.0f));
    const auto on = scene.ground_at({10.5f, 2.8f, 3.0f}, 0.35f, 0.4f, 0.7071f);
    REQUIRE(on.has_value());
    CHECK(on->height == doctest::Approx(2.7f));
    CHECK(on->kind == moteur::SurfaceKind::Walkable);
    // Nothing within reach above a pit of air.
    CHECK_FALSE(scene.ground_at({10.5f, 1.5f, 3.0f}, 0.35f, 0.4f, 0.7071f).has_value());
    // A line of sight under the bridge, and one through the wall.
    CHECK(scene.segment_clear({8, 1, 3}, {13, 1, 3}));
    CHECK_FALSE(scene.segment_clear({4, 1, -5}, {7, 1, -5}));
}

TEST_CASE("character controller: walls stop and slide, steps climbed, too high a step refused") {
    const moteur::CollisionScene scene = test_level();
    // Straight into the wall (x = 5): stops a radius before it.
    moteur::CharacterMove move = walk(scene, {0, 0, -5}, {1, 0, 0}, 3.0f);
    CHECK(move.position.x == doctest::Approx(5.0f - 0.35f).epsilon(0.01));
    CHECK(move.hit_wall);
    CHECK(move.position.y == doctest::Approx(0.0f));
    // Diagonally: it slides along the wall (z keeps going).
    const glm::vec3 diagonal = glm::normalize(glm::vec3(1, 0, -1));
    move = walk(scene, {4, 0, -2}, diagonal, 1.0f);
    CHECK(move.position.x < 5.0f - 0.34f);
    CHECK(move.position.z < -4.0f);

    // Up the stairs: never down, never jumping more than a step, at the top 2 m high.
    std::vector<float> heights;
    move = walk(scene, {-11, 0, 10}, {1, 0, 0}, 2.0f, &heights);
    CHECK(move.position.y == doctest::Approx(2.0f));
    CHECK(move.motion.grounded);
    for (std::size_t i = 1; i < heights.size(); ++i) {
        CHECK(heights[i] >= heights[i - 1] - 1e-5f);
        CHECK(heights[i] - heights[i - 1] <= 0.2f + 1e-4f);
    }
    // And down again: stuck to the steps (no fall, no bounce).
    heights.clear();
    move = walk(scene, move.position, {-1, 0, 0}, 2.5f, &heights);
    CHECK(move.position.y == doctest::Approx(0.0f));
    for (std::size_t i = 1; i < heights.size(); ++i) {
        CHECK(heights[i] <= heights[i - 1] + 1e-5f);
    }
    CHECK(move.motion.grounded);

    // A block of 0.6 m: a wall for a step of 0.35 m.
    move = walk(scene, {1, 0, 13}, {0, 0, 1}, 2.0f);
    CHECK(move.position.z == doctest::Approx(15.0f - 0.35f).epsilon(0.01));
    CHECK(move.position.y == doctest::Approx(0.0f));
    // A step under a ceiling too low for the capsule: refused.
    move = walk(scene, {15, 0, 12}, {0, 0, 1}, 1.5f);
    CHECK(move.position.y == doctest::Approx(0.0f));
    CHECK(move.position.z < 14.0f);
}

TEST_CASE("character controller: slopes, under and over a bridge, off a ledge") {
    const moteur::CollisionScene scene = test_level();
    // The 30 degree ramp is walked up; the 60 degree one is a wall.
    moteur::CharacterMove move = walk(scene, {-16, 0, -10}, {1, 0, 0}, 1.0f);
    CHECK(move.position.y > 1.0f);
    CHECK(move.motion.grounded);
    move = walk(scene, {-16, 0, -15}, {1, 0, 0}, 1.0f);
    CHECK(move.position.y < 0.36f);
    CHECK(move.position.x < -15.0f + 0.2f);

    // Under the bridge: the deck (2.5 m) is above the head; on it: it walks along it.
    move = walk(scene, {7, 0, 3}, {1, 0, 0}, 2.0f);
    CHECK(move.position.x > 13.0f);
    CHECK(move.position.y == doctest::Approx(0.0f));
    move = walk(scene, {10.5f, 2.7f, -1.5f}, {0, 0, 1}, 2.0f);
    CHECK(move.position.y == doctest::Approx(2.7f));
    CHECK(move.position.z > 6.0f);

    // Off the edge of the platform (2 m): it falls and lands on the ground, never below it.
    std::vector<float> heights;
    move = walk(scene, {-4, 2, 10}, {1, 0, 0}, 2.0f, &heights);
    CHECK(move.position.y == doctest::Approx(0.0f));
    CHECK(move.motion.grounded);
    for (const float h : heights) {
        CHECK(h >= 0.0f);
    }
    CHECK(std::any_of(heights.begin(), heights.end(), [](float h) { return h > 0.1f && h < 1.9f; }));  // it fell
}

TEST_CASE("character controller: the same moves give the same results, bit for bit") {
    const moteur::CollisionScene scene = test_level();
    const auto run = [&] {
        std::vector<glm::vec3> path;
        moteur::CharacterMove move;
        move.position = {-12, 0, 9.5f};
        Lcg random;
        for (int i = 0; i < 600; ++i) {
            const glm::vec3 wanted(random.next() - 0.3f, 0.0f, random.next() - 0.5f);
            move = moteur::move_character(scene, move.position, wanted * 0.2f, 1.0f / 30.0f, move.motion, {});
            path.push_back(move.position);
        }
        return path;
    };
    const std::vector<glm::vec3> a = run();
    const std::vector<glm::vec3> b = run();
    REQUIRE(a.size() == b.size());
    CHECK(std::memcmp(a.data(), b.data(), a.size() * sizeof(glm::vec3)) == 0);
}

TEST_CASE("movement in 3D: movers walk to their goal over the level; colliders on two floors do not push") {
    const moteur::CollisionScene scene = test_level();
    entt::registry registry;
    const entt::entity hero = registry.create();
    registry.emplace<moteur::Transform>(hero).position = {-12, 0, 10};
    registry.emplace<moteur::Collider>(hero);
    registry.emplace<moteur::Mover>(hero).go_to(glm::vec3(-5, 2, 10));  // up the stairs, onto the platform

    // One on the bridge, one under it, at the same place on the plane.
    const entt::entity above = registry.create();
    registry.emplace<moteur::Transform>(above).position = {10.5f, 2.7f, 3};
    registry.emplace<moteur::Collider>(above);
    const entt::entity below = registry.create();
    registry.emplace<moteur::Transform>(below).position = {10.5f, 0, 3};
    registry.emplace<moteur::Collider>(below);

    moteur::SpatialHash hash;
    moteur::MovementStats stats;
    for (int tick = 0; tick < 120; ++tick) {
        moteur::move_movers(registry, scene, 1.0f / 30.0f, moteur::NavSources{}, stats);
        moteur::separate_colliders(registry, scene, hash);
        moteur::finish_movers(registry, 1.0f / 30.0f);
    }
    const moteur::Mover& mover = registry.get<moteur::Mover>(hero);
    CHECK(mover.mode == moteur::MoveMode::Stop);  // arrived (then Idle at the next tick)
    CHECK(mover.path_status == moteur::PathStatus::Found);
    CHECK(registry.get<moteur::Transform>(hero).position.y == doctest::Approx(2.0f));
    CHECK(registry.get<moteur::Transform>(hero).position.x == doctest::Approx(-5.0f).epsilon(0.01));
    CHECK(registry.get<moteur::Transform>(above).position == glm::vec3(10.5f, 2.7f, 3));
    CHECK(registry.get<moteur::Transform>(below).position == glm::vec3(10.5f, 0, 3));
    // The hash knows the floors: a query on the bridge finds only the one on it.
    const auto found = hash.query_circle({10.5f, 3}, 0.5f, ~0u, {2.6f, 3.0f});
    REQUIRE(found.size() == 1);
    CHECK(found.front() == above);
}

TEST_CASE("model import: collision nodes become the model's collision, never drawn") {
    moteur::ModelData data;
    moteur::ModelPart visible;
    visible.node = "pillar";
    visible.name = "pillar/mesh#0";
    visible.mesh = moteur::make_cube();
    data.parts.push_back(visible);
    moteur::ModelPart wall;
    wall.node = "COL_wall";
    wall.name = "COL_wall/mesh#0";
    wall.mesh = moteur::make_cube();
    wall.transform[3] = glm::vec4(0.0f, 0.5f, 0.0f, 1.0f);
    data.parts.push_back(wall);

    moteur::ModelData named = data;
    moteur::import_model(named, {}, "pillar");
    REQUIRE(named.parts.size() == 1);
    CHECK(named.parts.front().node == "pillar");
    CHECK(named.collision.triangle_count() == 12);
    CHECK(std::all_of(named.collision.kinds.begin(), named.collision.kinds.end(),
                      [](moteur::SurfaceKind k) { return k == moteur::SurfaceKind::Wall; }));
    CHECK(named.collision.bounds().max.y == doctest::Approx(1.0f));  // moved by its node

    moteur::ModelImportSettings visible_too;
    visible_too.collision = moteur::ModelImportSettings::Collision::Visible;
    moteur::ModelData both = data;
    moteur::import_model(both, visible_too, "pillar");
    CHECK(both.collision.triangle_count() == 24);
    // The visible cube by its slope: its top is walkable, its sides and bottom walls.
    const auto walkable = std::count(both.collision.kinds.begin(), both.collision.kinds.end(), moteur::SurfaceKind::Walkable);
    CHECK(walkable == 2);

    moteur::ModelImportSettings none;
    none.collision = moteur::ModelImportSettings::Collision::None;
    moteur::ModelData without = data;
    moteur::import_model(without, none, "pillar");
    CHECK(without.parts.size() == 1);
    CHECK(without.collision.empty());

    // The prepared form keeps it; damaged collision is refused.
    const std::vector<std::uint8_t> bytes = moteur::encode_prepared_model(named, 1);
    const moteur::ModelData back = moteur::decode_prepared_model(bytes.data(), bytes.size(), "pillar.mmdl");
    CHECK(back.collision.vertices == named.collision.vertices);
    CHECK(back.collision.indices == named.collision.indices);
    CHECK(back.collision.kinds == named.collision.kinds);
    moteur::ModelData broken = named;
    broken.collision.indices.back() = 1000;
    const std::vector<std::uint8_t> bad = moteur::encode_prepared_model(broken, 1);
    CHECK_THROWS_WITH_AS(moteur::decode_prepared_model(bad.data(), bad.size(), "pillar.mmdl"),
                         doctest::Contains("past the vertices"), std::runtime_error);

    moteur::ModelImportSettings read;
    std::vector<std::string> warnings;
    read = moteur::ModelImportSettings::from_json(nlohmann::json{{"collision", "visible"}}, warnings);
    CHECK(read.collision == moteur::ModelImportSettings::Collision::Visible);
    CHECK(warnings.empty());
    read = moteur::ModelImportSettings::from_json(nlohmann::json{{"collision", "all"}}, warnings);
    CHECK(warnings.size() == 1);
    CHECK(read.hash() != visible_too.hash());
}

TEST_CASE("level collision: the entities marked StaticCollision make the scene") {
    moteur::World world;
    entt::registry& registry = world.registry();
    const entt::entity crate = registry.create();
    moteur::Transform& transform = registry.emplace<moteur::Transform>(crate);
    transform.position = {3, 0, 0};
    moteur::StaticCollision& collision = registry.emplace<moteur::StaticCollision>(crate);
    collision.boxes.push_back({{0, 0.5f, 0}, {0.5f, 0.5f, 0.5f}, moteur::SurfaceKind::Obstacle, moteur::SurfaceKind::Obstacle});
    registry.emplace<moteur::Transform>(registry.create());  // not marked: nothing

    moteur::CollisionScene scene;
    CHECK(moteur::gather_static_collision(world, scene) == 12);
    CHECK(scene.built());
    CHECK(scene.triangles().front().body == entt::to_integral(crate));
    const auto hit = scene.raycast({0, 0.5f, 0}, {1, 0, 0}, 10.0f, moteur::kSurfaceSolid);
    REQUIRE(hit.has_value());
    CHECK(hit->distance == doctest::Approx(2.5f));
}
