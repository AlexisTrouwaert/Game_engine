#include <doctest/doctest.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <vector>

#include "moteur/collision.hpp"
#include "moteur/movement.hpp"
#include "moteur/nav_grid.hpp"
#include "moteur/navigation.hpp"
#include "moteur/spatial_hash.hpp"
#include "moteur/world.hpp"

#include "test_levels.hpp"

// Milestone 9, part 6: the navigation mesh (Recast and Detour behind the engine's API): paths over
// several heights, links off the mesh, doors, areas, obstacles, its saved form, and the crowds
// (flow fields over the polygons, local avoidance).

namespace {

using test_levels::box;

const moteur::NavMesh& level_mesh() {
    static const moteur::CollisionScene scene = test_levels::test_level();
    static const moteur::NavMesh mesh = [] {
        moteur::NavMesh made;
        made.build(scene, {});
        // The ground inside the closed platform is an island nothing reaches: pruned.
        const glm::vec3 entry(0.0f, 0.0f, 0.0f);
        made.prune(std::span(&entry, 1));
        return made;
    }();
    return mesh;
}

float flat_distance(glm::vec3 a, glm::vec3 b) {
    const glm::vec2 d(a.x - b.x, a.z - b.z);
    return std::sqrt(glm::dot(d, d));
}

}  // namespace

TEST_CASE("navigation: the mesh covers the floors, paths climb the stairs") {
    const moteur::NavMesh& mesh = level_mesh();
    REQUIRE_FALSE(mesh.empty());
    CHECK(mesh.tile_count() > 0);
    CHECK(mesh.polygon_count() > 10);

    // Up to the platform (2 m): by the stairs, the last corner on top.
    const moteur::NavPath up = mesh.find_path({-12, 0, 10}, {-5, 2, 10});
    REQUIRE(up.status == moteur::PathStatus::Found);
    REQUIRE_FALSE(up.points.empty());
    CHECK(up.points.back().y == doctest::Approx(2.0f).epsilon(0.05));
    CHECK(flat_distance(up.points.back(), {-5, 2, 10}) < 0.1f);
    // The platform's edge is 2 m high: no way down there; from its top to the ground by the stairs.
    const moteur::NavPath down = mesh.find_path({-4, 2, 10}, {0, 0, 10});
    REQUIRE(down.status == moteur::PathStatus::Found);
    CHECK(std::any_of(down.points.begin(), down.points.end(), [](glm::vec3 p) { return p.x < -9.0f; }));

    // Under the bridge: on the ground. The deck has no way up: the reachable point nearest it.
    const moteur::NavPath under = mesh.find_path({8, 0, 3}, {13, 0, 3});
    REQUIRE(under.status == moteur::PathStatus::Found);
    CHECK(std::fabs(under.points.back().y) < 0.15f);  // the mesh follows the ground within a voxel
    const moteur::NavPath deck = mesh.find_path({8, 0, 3}, {10.5f, 2.7f, 3});
    CHECK(deck.status == moteur::PathStatus::Partial);

    // Queries: the ground under a point on each floor; a straight walk blocked by the wall.
    CHECK(mesh.height_at({-5, 2.2f, 10}).value_or(-1.0f) == doctest::Approx(2.0f).epsilon(0.05));
    CHECK(std::fabs(mesh.height_at({0, 0.2f, 0}).value_or(-1.0f)) < 0.15f);
    CHECK(mesh.walkable_line({-2, 0, 0}, {2, 0, 0}));
    glm::vec3 hit;
    CHECK_FALSE(mesh.walkable_line({3, 0, -5}, {8, 0, -5}, &hit));
    CHECK(hit.x < 5.0f);
    CHECK(mesh.nearest_point({0, 50, 0}).has_value() == false);  // far above anything

    // The grid as a view of the mesh, one floor at a time.
    const moteur::NavGrid ground = moteur::nav_grid_view(mesh, {-20, -20}, 40, 40, 0.0f);
    const moteur::NavGrid upstairs = moteur::nav_grid_view(mesh, {-20, -20}, 40, 40, 2.0f, 0.5f);
    CHECK(ground.walkable({20, 20}));         // (0.5, 0.5): open ground
    CHECK_FALSE(ground.walkable({15, 30}));   // (-4.5, 10.5): inside the platform (pruned)
    CHECK(upstairs.walkable({15, 30}));       // on its top
    CHECK_FALSE(ground.walkable({25, 15}));   // (5.5, -4.5): the wall
}

TEST_CASE("navigation: links off the mesh, doors, areas and obstacles") {
    const moteur::CollisionScene scene = test_levels::test_level();
    // A ladder from the ground up to the bridge's deck (link 5).
    moteur::NavLink ladder;
    ladder.from = {12.6f, 0.0f, 3.0f};
    ladder.to = {10.5f, 2.7f, 3.0f};
    ladder.id = 5;
    // Burning ground across the middle.
    const moteur::NavAreaVolume fire{box({-2, -1, -8}, {2, 1, 8}), moteur::NavArea::Fire};
    moteur::NavMesh mesh;
    const moteur::NavBuildStats stats = mesh.build(scene, {}, {}, std::span(&fire, 1), std::span(&ladder, 1));
    MESSAGE("test level: " << stats.tiles << " tiles, " << stats.polygons << " polygons in " << stats.milliseconds << " ms");

    moteur::NavPath up = mesh.find_path({14, 0, 3}, {10.5f, 2.7f, 6});
    CHECK(up.status == moteur::PathStatus::Found);
    mesh.set_link_enabled(5, false);  // the door is shut
    CHECK(mesh.find_path({14, 0, 3}, {10.5f, 2.7f, 6}).status == moteur::PathStatus::Partial);
    mesh.set_link_enabled(5, true);
    moteur::NavFilter no_jumps;
    no_jumps.jumps = false;
    CHECK(mesh.find_path({14, 0, 3}, {10.5f, 2.7f, 6}, no_jumps).status == moteur::PathStatus::Partial);

    // The fire costs 10 a metre: the path goes round it; across it if fire costs nothing more.
    const moteur::NavPath round = mesh.find_path({-4, 0, 0}, {4, 0, 0});
    REQUIRE(round.status == moteur::PathStatus::Found);
    CHECK(std::any_of(round.points.begin(), round.points.end(), [](glm::vec3 p) { return std::fabs(p.z) > 7.5f; }));
    moteur::NavFilter fearless;
    fearless.area_costs[static_cast<std::size_t>(moteur::NavArea::Fire)] = 1.0f;
    const moteur::NavPath straight = mesh.find_path({-4, 0, 0}, {4, 0, 0}, fearless);
    CHECK(straight.points.size() == 1);

    // An obstacle across the passage under the bridge: round it; removed: straight again.
    const moteur::NavPath before = mesh.find_path({7, 0, 3}, {15, 0, 3});
    REQUIRE(before.points.size() == 1);
    const Uint64 start = SDL_GetPerformanceCounter();
    const std::uint32_t crate = mesh.add_obstacle(scene, box({8.5f, 0, 1}, {12.5f, 1, 5}));
    const double rebuild_ms = static_cast<double>(SDL_GetPerformanceCounter() - start) * 1000.0 /
                              static_cast<double>(SDL_GetPerformanceFrequency());
    MESSAGE("an obstacle: its tiles rebuilt in " << rebuild_ms << " ms");
    CHECK(rebuild_ms < 1000.0);  // the editor's target for a room (Release; Debug is slower but within)
    const moteur::NavPath around = mesh.find_path({7, 0, 3}, {15, 0, 3});
    REQUIRE(around.status == moteur::PathStatus::Found);
    CHECK(around.points.size() > 1);
    mesh.remove_obstacle(scene, crate);
    CHECK(mesh.find_path({7, 0, 3}, {15, 0, 3}).points.size() == 1);

    // The saved form: the same answers; damaged: refused.
    mesh.set_link_enabled(5, false);
    const std::vector<std::uint8_t> bytes = mesh.save();
    const moteur::NavMesh loaded = moteur::NavMesh::load(bytes, "level.mnav");
    CHECK(loaded.polygon_count() == mesh.polygon_count());
    CHECK(loaded.find_path({-4, 0, 0}, {4, 0, 0}).points == round.points);
    CHECK(loaded.find_path({14, 0, 3}, {10.5f, 2.7f, 6}).status == moteur::PathStatus::Partial);  // the door kept shut
    std::vector<std::uint8_t> damaged = bytes;
    damaged[damaged.size() / 2] ^= 0x40;
    CHECK_THROWS_WITH_AS(moteur::NavMesh::load(damaged, "level.mnav"), doctest::Contains("checksum"), std::runtime_error);
    CHECK_THROWS_WITH_AS(moteur::NavMesh::load({1, 2, 3}, "level.mnav"), doctest::Contains("MNAV"), std::runtime_error);
}

TEST_CASE("navigation: movers plan on the mesh; a crowd follows a flow field down the stairs") {
    const moteur::CollisionScene scene = test_levels::test_level();
    const moteur::NavMesh& mesh = level_mesh();
    const moteur::NavMesh* const meshes[1] = {&mesh};

    // A mover sent to the platform walks there by the stairs (it never falls).
    entt::registry registry;
    const entt::entity walker = registry.create();
    registry.emplace<moteur::Transform>(walker).position = {-12, 0, 10};
    registry.emplace<moteur::Collider>(walker);
    registry.emplace<moteur::Mover>(walker).go_to(glm::vec3(-4, 2, 10));
    moteur::MovementStats stats;
    moteur::SpatialHash hash;
    moteur::NavSources nav;
    nav.meshes = meshes;
    nav.neighbours = &hash;
    bool fell = false;
    for (int tick = 0; tick < 150; ++tick) {
        moteur::plan_paths(registry, meshes, 8, stats);
        moteur::move_movers(registry, scene, 1.0f / 30.0f, nav, stats);
        moteur::separate_colliders(registry, scene, hash);
        moteur::finish_movers(registry, 1.0f / 30.0f);
        fell = fell || !registry.get<moteur::Mover>(walker).grounded;
    }
    CHECK_FALSE(fell);
    CHECK(registry.get<moteur::Transform>(walker).position.y == doctest::Approx(2.0f));
    CHECK(flat_distance(registry.get<moteur::Transform>(walker).position, {-4, 2, 10}) < 0.1f);

    // A monster on the platform chases a target on the ground: down by the stairs.
    moteur::NavFlowField field;
    field.compute(mesh, {0, 0, 4});
    REQUIRE(field.valid());
    const moteur::NavFlowField* const fields[1] = {&field};
    nav.fields = fields;
    entt::registry chase;
    const entt::entity monster = chase.create();
    chase.emplace<moteur::Transform>(monster).position = {-4, 2, 10};
    chase.emplace<moteur::Collider>(monster);
    moteur::Mover& m = chase.emplace<moteur::Mover>(monster);
    m.follow_field(0.5f);
    m.avoid = true;
    for (int tick = 0; tick < 300; ++tick) {
        moteur::move_movers(chase, scene, 1.0f / 30.0f, nav, stats);
        moteur::separate_colliders(chase, scene, hash);
        moteur::finish_movers(chase, 1.0f / 30.0f);
    }
    CHECK(flat_distance(chase.get<moteur::Transform>(monster).position, {0, 0, 4}) < 1.0f);
    CHECK(chase.get<moteur::Transform>(monster).position.y == doctest::Approx(0.0f));
}

TEST_CASE("navigation: 500 monsters surround the hero without blocking, in the budget") {
    const moteur::CollisionScene scene = test_levels::test_level();
    const moteur::NavMesh& mesh = level_mesh();
    const moteur::NavMesh* const meshes[1] = {&mesh};
    moteur::NavFlowField field;
    const glm::vec3 hero(-12, 0, -2);
    field.compute(mesh, hero);
    REQUIRE(field.valid());
    const moteur::NavFlowField* const fields[1] = {&field};
    moteur::SpatialHash hash;
    moteur::NavSources nav;
    nav.meshes = meshes;
    nav.fields = fields;
    nav.neighbours = &hash;

    entt::registry registry;
    // 500 monsters on a grid across the open ground, 1 m apart.
    int placed = 0;
    for (int j = 0; placed < 500; ++j) {
        for (int i = 0; i < 25 && placed < 500; ++i) {
            const glm::vec3 p(-4.0f + static_cast<float>(i) * 0.9f, 0.0f, -18.0f + static_cast<float>(j) * 0.9f);
            const entt::entity e = registry.create();
            registry.emplace<moteur::Transform>(e).position = p;
            moteur::Collider& c = registry.emplace<moteur::Collider>(e);
            c.radius = 0.3f;
            moteur::Mover& m = registry.emplace<moteur::Mover>(e);
            m.speed = 3.5f;
            m.follow_field(0.8f);
            m.avoid = SDL_getenv("NO_AVOID") == nullptr;
            ++placed;
        }
    }
    moteur::MovementStats stats;
    double total_ms = 0.0;
    double move_ms = 0.0;
    constexpr int kTicks = 300;
    const auto ms_since = [](Uint64 from) {
        return static_cast<double>(SDL_GetPerformanceCounter() - from) * 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency());
    };
    for (int tick = 0; tick < kTicks; ++tick) {
        const Uint64 start = SDL_GetPerformanceCounter();
        moteur::move_movers(registry, scene, 1.0f / 30.0f, nav, stats);
        move_ms += ms_since(start);
        moteur::separate_colliders(registry, scene, hash);
        moteur::finish_movers(registry, 1.0f / 30.0f);
        total_ms += ms_since(start);
    }
    int near = 0;
    int off_mesh = 0;
    for (const auto& [e, t, m] : registry.view<moteur::Transform, moteur::Mover>().each()) {
        near += flat_distance(t.position, hero) < 9.0f ? 1 : 0;
        off_mesh += m.nav_poly == 0 ? 1 : 0;
    }
    MESSAGE("500 monsters: " << total_ms / kTicks << " ms per tick (of which movement and avoidance "
                             << move_ms / kTicks << " ms); " << near
                             << " within 9 m of the hero after 10 s");
    CHECK(near > 350);       // they came round it (the nearest ring is full)
    CHECK(off_mesh < 25);    // pushed off the mesh by the crowd: a few at most
}
