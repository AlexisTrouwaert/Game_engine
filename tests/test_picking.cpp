#include <doctest/doctest.h>

#include <cmath>

#include "moteur/camera3d.hpp"
#include "moteur/nav_grid.hpp"
#include "moteur/navigation.hpp"
#include "moteur/picking.hpp"
#include "moteur/visibility.hpp"
#include "moteur/world.hpp"

#include "test_levels.hpp"

// Milestone 9, part 7: sight and selection in 3D: lines of sight against the level, the grid of a
// floor for the fog of war, and the mouse's rays with their priorities.

namespace {

using test_levels::box;

moteur::Ray ray_from(glm::vec3 origin, glm::vec3 target) {
    moteur::Ray ray;
    ray.origin = origin;
    ray.direction = glm::normalize(target - origin);
    return ray;
}

entt::entity selectable(entt::registry& registry, glm::vec3 at, moteur::PickKind kind) {
    const entt::entity e = registry.create();
    registry.emplace<moteur::Transform>(e).position = at;
    moteur::Selectable& s = registry.emplace<moteur::Selectable>(e);
    s.kind = kind;
    return e;
}

}  // namespace

TEST_CASE("picking: shapes, priorities, half hidden, and the ground through a hole") {
    // A ground, a pillar, and a floor above with a hole in it.
    moteur::CollisionMeshData mesh;
    mesh.add_box(box({-20, -1, -20}, {20, 0, 20}));
    mesh.add_box(box({-0.5f, 0, -0.5f}, {0.5f, 3, 0.5f}), moteur::SurfaceKind::Wall);  // the pillar at the origin
    mesh.add_box(box({6, 3, -4}, {14, 3.2f, 1}));   // the upper floor, x 6..14, z -4..4 but for the hole z 1..2
    mesh.add_box(box({6, 3, 2}, {14, 3.2f, 4}));
    moteur::CollisionScene level;
    level.add(mesh, glm::mat4(1.0f));
    level.build();

    // The shapes themselves.
    const moteur::Ray straight = ray_from({-5, 1, 0}, {5, 1, 0});
    CHECK(moteur::ray_capsule(straight, {0, 0, 0}, 0.5f, 1.8f).value_or(-1.0f) == doctest::Approx(4.5f));
    CHECK_FALSE(moteur::ray_capsule(straight, {0, 0, 2}, 0.5f, 1.8f).has_value());
    CHECK(moteur::ray_capsule(ray_from({0, 10, 0}, {0, 0, 0}), {0, 0, 0}, 0.5f, 1.8f).value_or(-1.0f) == doctest::Approx(8.2f));
    CHECK(moteur::ray_box(straight, {-1, 0, -1}, {1, 2, 1}).value_or(-1.0f) == doctest::Approx(4.0f));

    entt::registry registry;
    // A monster half behind the pillar (seen from the camera, its left half shows).
    const entt::entity monster = selectable(registry, {0.3f, 0, 1.5f}, moteur::PickKind::Character);
    const glm::vec3 eye(0, 12, 12);
    // Aimed at its visible side: picked, though the pillar is nearer along other rays.
    std::optional<moteur::PickHit> hit = moteur::pick(registry, ray_from(eye, {0.6f, 1.0f, 1.5f}), &level);
    REQUIRE(hit.has_value());
    CHECK(hit->entity == monster);
    // Aimed through the pillar at it: the pillar hides it, the ground is not even reached (a wall).
    hit = moteur::pick(registry, ray_from({0, 1, -6}, {0.3f, 1.0f, 1.5f}), &level);
    CHECK((!hit.has_value() || hit->entity != monster));

    // An item lying at a monster's feet: the monster wins (priority), though the item is nearer.
    const entt::entity item = selectable(registry, {-6, 0, 0}, moteur::PickKind::Item);
    moteur::Selectable& item_shape = registry.get<moteur::Selectable>(item);
    item_shape.box = true;
    item_shape.centre = {0, 0.2f, 0};
    item_shape.half_size = {0.4f, 0.2f, 0.4f};
    const entt::entity standing = selectable(registry, {-6, 0, 0.6f}, moteur::PickKind::Character);
    hit = moteur::pick(registry, ray_from({-6, 10, 6}, {-6, 0.1f, 0.2f}), &level);
    REQUIRE(hit.has_value());
    CHECK(hit->entity == standing);
    registry.get<moteur::Selectable>(standing).enabled = false;  // dead: the item now
    hit = moteur::pick(registry, ray_from({-6, 10, 6}, {-6, 0.1f, 0.2f}), &level);
    REQUIRE(hit.has_value());
    CHECK(hit->entity == item);

    // The ground of the floor below, through the hole of the floor above; the floor above elsewhere.
    hit = moteur::pick(registry, ray_from({10, 12, 1.5f}, {10, 0, 1.5f}), &level);
    REQUIRE(hit.has_value());
    CHECK(static_cast<bool>(hit->entity == entt::null));
    CHECK(hit->kind == moteur::PickKind::Ground);
    CHECK(hit->point.y == doctest::Approx(0.0f));
    hit = moteur::pick(registry, ray_from({10, 12, 3}, {10, 0, 3}), &level);
    REQUIRE(hit.has_value());
    CHECK(hit->point.y == doctest::Approx(3.2f));
}

TEST_CASE("sight in 3D: walls, floors above, symmetric; the grid of a floor") {
    const moteur::CollisionScene level = test_levels::test_level();
    // Through the wall (x 5..6, z < 0): no; round it: yes. Both ways the same.
    CHECK_FALSE(moteur::line_of_sight(level, {3, 1.6f, -5}, {8, 1.6f, -5}));
    CHECK(moteur::line_of_sight(level, {3, 1.6f, 5}, {8, 1.6f, 5}));
    CHECK(moteur::line_of_sight(level, {8, 1.6f, 5}, {3, 1.6f, 5}));
    // From the bridge's deck to someone right under it: the deck is in the way.
    CHECK_FALSE(moteur::line_of_sight(level, {10.5f, 4.3f, 3}, {10.5f, 1.6f, 3}));
    // From the deck to someone beside the bridge, out from under it: seen.
    CHECK(moteur::line_of_sight(level, {10.5f, 4.3f, 3}, {14, 1.6f, 3}));

    moteur::NavMesh mesh;
    mesh.build(level, {});
    const moteur::NavGrid ground = moteur::floor_sight_grid(mesh, level, {-20, -20}, 40, 40, 0.0f);
    CHECK(ground.walkable({20, 20}));      // (0.5, 0.5): open
    CHECK(ground.opaque({25, 15}));        // (5.5, -4.5): the wall
    CHECK_FALSE(ground.opaque({30, 23}));  // (10.5, 3.5): under the deck, nothing at eye height
    // A field of view on that floor: the wall hides what is behind it.
    moteur::FieldOfView view;
    view.compute(ground, {22, 15}, 12);  // at (2.5, -4.5)
    CHECK(view.visible({24, 15}));
    CHECK_FALSE(view.visible({28, 15}));  // (8.5, -4.5): behind the wall
}
