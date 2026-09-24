#include <doctest/doctest.h>

#include <glm/glm.hpp>

#include <cmath>

#include "moteur/billboard_batcher.hpp"
#include "moteur/camera3d.hpp"

namespace {

// The view of the chosen camera (perspective, 50 degrees down, turned 45 degrees).
moteur::BillboardView chosen_view() {
    moteur::Camera3D camera;
    camera.set_viewport({1280.0f, 720.0f});
    moteur::BillboardView view;
    view.eye = camera.position();
    view.forward = camera.forward();
    view.right = glm::normalize(glm::cross(view.forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    view.up = glm::cross(view.right, view.forward);
    return view;
}

moteur::BillboardDesc billboard(const void* texture, glm::vec3 center) {
    moteur::BillboardDesc desc;
    desc.texture = texture;
    desc.center = center;
    desc.size = {2.0f, 1.0f};
    return desc;
}

}  // namespace

TEST_CASE("BillboardBatcher: a camera-facing billboard lies across the view, the right size") {
    const moteur::BillboardView view = chosen_view();
    glm::vec3 corners[4];
    moteur::BillboardBatcher::corners(billboard(nullptr, {1.0f, 2.0f, 3.0f}), view, corners);
    // Its plane is perpendicular to the view direction.
    for (const glm::vec3& corner : corners) {
        CHECK(glm::dot(corner - glm::vec3(1.0f, 2.0f, 3.0f), view.forward) == doctest::Approx(0.0f).epsilon(1e-5));
    }
    CHECK(glm::length(corners[1] - corners[0]) == doctest::Approx(2.0f));  // width, along the top edge
    CHECK(glm::length(corners[3] - corners[0]) == doctest::Approx(1.0f));  // height
    // Top left is to the left and above: seen from the camera, not mirrored.
    CHECK(glm::dot(corners[1] - corners[0], view.right) > 0.0f);
    CHECK(glm::dot(corners[0] - corners[3], view.up) > 0.0f);
    // Centred.
    const glm::vec3 middle = (corners[0] + corners[1] + corners[2] + corners[3]) * 0.25f;
    CHECK(glm::length(middle - glm::vec3(1.0f, 2.0f, 3.0f)) < 1e-5f);
}

TEST_CASE("BillboardBatcher: an upright billboard keeps vertical edges and turns around the vertical") {
    const moteur::BillboardView view = chosen_view();
    moteur::BillboardDesc desc = billboard(nullptr, {0.0f, 1.0f, 0.0f});
    desc.facing = moteur::BillboardFacing::Upright;
    glm::vec3 corners[4];
    moteur::BillboardBatcher::corners(desc, view, corners);
    const glm::vec3 vertical = corners[0] - corners[3];
    CHECK(vertical.x == doctest::Approx(0.0f));
    CHECK(vertical.z == doctest::Approx(0.0f));
    CHECK(vertical.y == doctest::Approx(1.0f));
    const glm::vec3 across = corners[1] - corners[0];
    CHECK(across.y == doctest::Approx(0.0f));
    CHECK(glm::length(across) == doctest::Approx(2.0f));
    // It faces the camera horizontally: its width is perpendicular to the view, seen from above.
    const glm::vec3 flat_forward = glm::normalize(glm::vec3(view.forward.x, 0.0f, view.forward.z));
    CHECK(glm::dot(across, flat_forward) == doctest::Approx(0.0f).epsilon(1e-5));
}

TEST_CASE("BillboardBatcher: sorted from the farthest to the nearest, runs split by texture") {
    const moteur::BillboardView view = chosen_view();
    const int a = 0;
    const int b = 0;
    moteur::BillboardBatcher batcher;
    batcher.begin();
    // Along the view: each step of `forward` is one metre farther.
    batcher.add(billboard(&a, view.eye + view.forward * 10.0f));
    batcher.add(billboard(&a, view.eye + view.forward * 30.0f));
    batcher.add(billboard(&b, view.eye + view.forward * 20.0f));
    batcher.add(billboard(&a, view.eye + view.forward * 25.0f));
    batcher.add(billboard(&b, view.eye + view.forward * 20.0f + view.right * 3.0f));  // same depth as the third
    batcher.finish(view);

    REQUIRE(batcher.count() == 5);
    REQUIRE(batcher.vertices().size() == 20);
    // Order: 30 (a), 25 (a), 20 (b), 20 (b, recorded later), 10 (a).
    const auto depth_of = [&](std::size_t quad) {
        const moteur::BillboardVertex& v0 = batcher.vertices()[quad * 4];
        const moteur::BillboardVertex& v2 = batcher.vertices()[quad * 4 + 2];
        const glm::vec3 center = (glm::vec3(v0.x, v0.y, v0.z) + glm::vec3(v2.x, v2.y, v2.z)) * 0.5f;
        return glm::dot(center - view.eye, view.forward);
    };
    CHECK(depth_of(0) == doctest::Approx(30.0f));
    CHECK(depth_of(1) == doctest::Approx(25.0f));
    CHECK(depth_of(2) == doctest::Approx(20.0f));
    CHECK(depth_of(3) == doctest::Approx(20.0f));
    CHECK(depth_of(4) == doctest::Approx(10.0f));
    const moteur::BillboardVertex& fourth = batcher.vertices()[3 * 4];
    CHECK(glm::dot(glm::vec3(fourth.x, fourth.y, fourth.z) - view.eye, view.right) > 1.0f);  // the one to the right

    REQUIRE(batcher.runs().size() == 3);
    CHECK(batcher.runs()[0].texture == &a);
    CHECK(batcher.runs()[0].first == 0);
    CHECK(batcher.runs()[0].count == 2);
    CHECK(batcher.runs()[1].texture == &b);
    CHECK(batcher.runs()[1].count == 2);
    CHECK(batcher.runs()[2].texture == &a);
    CHECK(batcher.runs()[2].first == 4);

    // Recording again starts from nothing.
    batcher.begin();
    batcher.finish(view);
    CHECK(batcher.count() == 0);
    CHECK(batcher.runs().empty());
}

TEST_CASE("BillboardBatcher: colors are premultiplied, additive ones have no alpha, uv follow the corners") {
    const moteur::BillboardView view = chosen_view();
    moteur::BillboardBatcher batcher;
    batcher.begin();
    moteur::BillboardDesc glass = billboard(nullptr, {0.0f, 0.0f, 0.0f});
    glass.color = {1.0f, 0.5f, 4.0f, 0.5f};
    glass.uv_rect = {0.25f, 0.5f, 0.75f, 1.0f};
    batcher.add(glass);
    batcher.finish(view);
    const moteur::BillboardVertex& top_left = batcher.vertices()[0];
    CHECK(top_left.r == doctest::Approx(0.5f));
    CHECK(top_left.g == doctest::Approx(0.25f));
    CHECK(top_left.b == doctest::Approx(2.0f));
    CHECK(top_left.a == doctest::Approx(0.5f));
    CHECK(top_left.u == 0.25f);
    CHECK(top_left.v == 0.5f);
    const moteur::BillboardVertex& bottom_right = batcher.vertices()[2];
    CHECK(bottom_right.u == 0.75f);
    CHECK(bottom_right.v == 1.0f);

    batcher.begin();
    glass.additive = true;
    batcher.add(glass);
    batcher.finish(view);
    CHECK(batcher.vertices()[0].a == 0.0f);
    CHECK(batcher.vertices()[0].b == doctest::Approx(2.0f));  // the light itself is kept
}

TEST_CASE("BillboardBatcher: a run never exceeds the 16-bit index limit") {
    const moteur::BillboardView view = chosen_view();
    const int texture = 0;
    moteur::BillboardBatcher batcher;
    batcher.begin();
    const std::uint32_t total = moteur::BillboardBatcher::kMaxQuadsPerRun + 10;
    for (std::uint32_t i = 0; i < total; ++i) {
        batcher.add(billboard(&texture, {0.0f, 0.0f, 0.0f}));
    }
    batcher.finish(view);
    REQUIRE(batcher.runs().size() == 2);
    CHECK(batcher.runs()[0].count == moteur::BillboardBatcher::kMaxQuadsPerRun);
    CHECK(batcher.runs()[1].count == 10);
    CHECK(batcher.runs()[1].first == moteur::BillboardBatcher::kMaxQuadsPerRun);
}
