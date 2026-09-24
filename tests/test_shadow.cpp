#include <doctest/doctest.h>

#include <glm/glm.hpp>

#include <cmath>

#include "moteur/camera3d.hpp"
#include "moteur/shadow.hpp"

namespace {

const glm::vec3 kToSun = glm::normalize(glm::vec3(0.3f, 0.8f, 0.5f));

moteur::Camera3D camera_at(glm::vec3 target, float visible_height = 12.0f) {
    moteur::Camera3D camera;  // the chosen framing: perspective, 50 degrees, 30 degree field of view
    camera.set_target(target);
    camera.set_visible_height(visible_height);
    camera.set_viewport({1280.0f, 720.0f});
    return camera;
}

glm::vec3 to_shadow_clip(const moteur::ShadowFrame& frame, glm::vec3 world) {
    const glm::vec4 clip = frame.view_projection * glm::vec4(world, 1.0f);
    return glm::vec3(clip) / clip.w;
}

// Where the camera's ray through a window point (in [-1, 1]) meets the ground.
glm::vec3 ground_under(const moteur::Camera3D& camera, glm::vec2 ndc) {
    const glm::mat4 inverse = glm::inverse(camera.view_projection());
    glm::vec4 a = inverse * glm::vec4(ndc, 0.0f, 1.0f);
    glm::vec4 b = inverse * glm::vec4(ndc, 1.0f, 1.0f);
    const glm::vec3 near_point = glm::vec3(a) / a.w, far_point = glm::vec3(b) / b.w;
    const glm::vec3 direction = far_point - near_point;
    return near_point + direction * (-near_point.y / direction.y);
}

}  // namespace

TEST_CASE("fit_sun_shadow covers every point of the ground the camera sees") {
    const moteur::Camera3D camera = camera_at({3.0f, 0.0f, -2.0f});
    const moteur::ShadowFrame frame = moteur::fit_sun_shadow(camera.view_projection(), kToSun);
    for (float x = -1.0f; x <= 1.0f; x += 0.25f) {
        for (float y = -1.0f; y <= 1.0f; y += 0.25f) {
            for (const float height : {0.0f, 3.0f}) {  // the ground, and a caster above it
                const glm::vec3 clip = to_shadow_clip(frame, ground_under(camera, {x, y}) + glm::vec3(0, height, 0));
                CHECK(std::abs(clip.x) <= 1.0f);
                CHECK(std::abs(clip.y) <= 1.0f);
                CHECK(clip.z > 0.0f);
                CHECK(clip.z < 1.0f);
            }
        }
    }
}

TEST_CASE("fit_sun_shadow: what is closer to the sun has a smaller depth") {
    const moteur::Camera3D camera = camera_at({0.0f, 0.0f, 0.0f});
    const moteur::ShadowFrame frame = moteur::fit_sun_shadow(camera.view_projection(), kToSun);
    const glm::vec3 ground(1.0f, 0.0f, 1.0f);
    CHECK(to_shadow_clip(frame, ground + kToSun * 2.0f).z < to_shadow_clip(frame, ground).z);
    // And the two land on the same texel: a point and its shadow along the sun's rays.
    const glm::vec3 a = to_shadow_clip(frame, ground), b = to_shadow_clip(frame, ground + kToSun * 2.0f);
    CHECK(a.x == doctest::Approx(b.x).epsilon(1e-4));
    CHECK(a.y == doctest::Approx(b.y).epsilon(1e-4));
}

TEST_CASE("fit_sun_shadow: the map keeps its size when the camera moves, and grows when it zooms out") {
    const moteur::ShadowFrame here = moteur::fit_sun_shadow(camera_at({0, 0, 0}).view_projection(), kToSun);
    const moteur::ShadowFrame there = moteur::fit_sun_shadow(camera_at({7.3f, 0, -4.1f}).view_projection(), kToSun);
    CHECK(here.radius == there.radius);
    const moteur::ShadowFrame zoomed_out = moteur::fit_sun_shadow(camera_at({0, 0, 0}, 30.0f).view_projection(), kToSun);
    CHECK(zoomed_out.radius > here.radius);
    CHECK(here.texel_size == doctest::Approx(2.0f * here.radius / 2048.0f));
}

TEST_CASE("fit_sun_shadow: moving the camera slides the map by whole texels (no shimmering)") {
    // A fixed point of the world: its position in texels keeps the same fraction however the
    // camera moves, so it is always rasterized the same way.
    const glm::vec3 point(1.37f, 0.4f, -2.21f);
    const auto texel_fraction = [&](glm::vec3 target) {
        const moteur::ShadowFrame frame = moteur::fit_sun_shadow(camera_at(target).view_projection(), kToSun);
        const glm::vec3 clip = to_shadow_clip(frame, point);
        const glm::vec2 texels = (glm::vec2(clip) * 0.5f + 0.5f) * 2048.0f;
        return texels - glm::floor(texels);
    };
    const glm::vec2 reference = texel_fraction({0.0f, 0.0f, 0.0f});
    for (const glm::vec3 target : {glm::vec3(0.013f, 0, 0), glm::vec3(0.5f, 0, -0.27f), glm::vec3(-1.9f, 0, 2.2f)}) {
        const glm::vec2 fraction = texel_fraction(target);
        // Equal up to float precision (a fraction near 0 may come back near 1).
        CHECK(std::min(std::abs(fraction.x - reference.x), 1.0f - std::abs(fraction.x - reference.x)) < 0.02f);
        CHECK(std::min(std::abs(fraction.y - reference.y), 1.0f - std::abs(fraction.y - reference.y)) < 0.02f);
    }
}

TEST_CASE("Point shadow faces: every direction lands inside its face, with the margin") {
    const glm::vec3 light(2.0f, 1.0f, -3.0f);
    const float range = 6.0f;
    const int tile = 512;
    const moteur::PointShadowFaces faces = moteur::point_shadow_faces(light, range, tile);
    // Almost the full margin (the wider face also spreads the texels a little).
    const float margin = (static_cast<float>(moteur::kPointShadowMarginTexels) - 0.05f) / static_cast<float>(tile);
    std::uint32_t state = 7;
    const auto random = [&state] {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) / 16777216.0f * 2.0f - 1.0f;
    };
    for (int i = 0; i < 2000; ++i) {
        glm::vec3 d(random(), random(), random());
        if (glm::length(d) < 0.05f) {
            continue;
        }
        d = glm::normalize(d) * (0.5f + 5.0f * std::abs(random()));
        const int face = moteur::point_shadow_face(d);
        const glm::vec4 clip = faces.view_projection[face] * glm::vec4(light + d, 1.0f);
        REQUIRE(clip.w > 0.0f);
        const glm::vec2 uv = glm::vec2(clip.x, -clip.y) / clip.w * 0.5f + 0.5f;
        // The 90-degree part of the face, inside the margin on every side.
        CHECK(uv.x >= margin - 1e-5f);
        CHECK(uv.x <= 1.0f - margin + 1e-5f);
        CHECK(uv.y >= margin - 1e-5f);
        CHECK(uv.y <= 1.0f - margin + 1e-5f);
    }
    // The six axes land in the middle of their own face.
    const glm::vec3 axes[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int f = 0; f < 6; ++f) {
        CHECK(moteur::point_shadow_face(axes[f]) == f);
        const glm::vec4 clip = faces.view_projection[f] * glm::vec4(light + axes[f] * 3.0f, 1.0f);
        CHECK(clip.x / clip.w == doctest::Approx(0.0f).epsilon(1e-5));
        CHECK(clip.y / clip.w == doctest::Approx(0.0f).epsilon(1e-5));
    }
    // One texel covers about 2 / 512 of the distance.
    CHECK(faces.texel_size_per_metre == doctest::Approx(2.0f / 512.0f).epsilon(0.02));
}

TEST_CASE("Point shadow selection: visible lights, nearest to the focus, within the budget") {
    const moteur::Camera3D camera = camera_at({0.0f, 0.0f, 0.0f});
    const moteur::Frustum view = camera.frustum();
    std::vector<moteur::PointShadowCandidate> lights = {
        {{5.0f, 1.0f, 0.0f}, 6.0f},      // 0: visible, 5 m away
        {{300.0f, 1.0f, 0.0f}, 6.0f},    // 1: far out of view
        {{1.0f, 1.0f, 0.0f}, 6.0f},      // 2: visible, nearest
        {{-3.0f, 1.0f, 0.0f}, 6.0f},     // 3: visible, 3 m
        {{3.0f, 1.0f, 0.0f}, 6.0f},      // 4: visible, 3 m too: after 3 (given later)
        {{0.0f, 1.0f, 0.0f}, 0.0f},      // 5: no range
    };
    const std::vector<int> two = moteur::select_point_shadows(lights, view, {0, 1, 0}, 2);
    CHECK(two == std::vector<int>{2, 3});
    const std::vector<int> all = moteur::select_point_shadows(lights, view, {0, 1, 0}, 8);
    CHECK(all == std::vector<int>{2, 3, 4, 0});
    CHECK(moteur::select_point_shadows(lights, view, {0, 1, 0}, 0).empty());
    // A light out of view whose sphere reaches into it still counts.
    lights[1].range = 400.0f;
    CHECK(moteur::select_point_shadows(lights, view, {0, 1, 0}, 8).size() == 5);
}

TEST_CASE("Point shadow slots: unchanged shadows are kept, changed ones drawn again") {
    moteur::PointShadowSlots slots;
    slots.reset(3);
    // First frame: everything must be drawn, in free slots.
    auto first = slots.assign({10, 20});
    REQUIRE(first.size() == 2);
    CHECK(first[0].render);
    CHECK(first[1].render);
    CHECK(first[0].slot != first[1].slot);

    // Nothing changed: nothing drawn, same slots, even in another order.
    auto second = slots.assign({20, 10});
    CHECK_FALSE(second[0].render);
    CHECK_FALSE(second[1].render);
    CHECK(second[0].slot == first[1].slot);
    CHECK(second[1].slot == first[0].slot);

    // Light 20 changed (something moved near it): drawn again, in the free slot, 10 kept.
    auto third = slots.assign({10, 21});
    CHECK_FALSE(third[0].render);
    CHECK(third[0].slot == first[0].slot);
    CHECK(third[1].render);
    const int free_slot = 3 - first[0].slot - first[1].slot;  // the one never used
    CHECK(third[1].slot == free_slot);

    // 20 comes back: the slot that still holds it is reused without drawing.
    auto fourth = slots.assign({20});
    CHECK_FALSE(fourth[0].render);
    CHECK(fourth[0].slot == first[1].slot);

    // Three new lights: they take every slot, kept shadows are overwritten.
    auto fifth = slots.assign({1, 2, 3});
    for (const auto& assignment : fifth) {
        CHECK(assignment.render);
    }
    // More lights than slots: only as many as slots.
    CHECK(slots.assign({1, 2, 3, 4}).size() == 3);

    // After a reset (atlas recreated), everything is drawn again.
    slots.reset(3);
    CHECK(slots.assign({1})[0].render);
}

TEST_CASE("Aabb: intersection with a sphere") {
    moteur::Aabb box;
    box.add({0.0f, 0.0f, 0.0f});
    box.add({1.0f, 1.0f, 1.0f});
    CHECK(moteur::intersects_sphere(box, {0.5f, 0.5f, 0.5f}, 0.1f));   // inside
    CHECK(moteur::intersects_sphere(box, {2.0f, 0.5f, 0.5f}, 1.0f));   // touches a face
    CHECK_FALSE(moteur::intersects_sphere(box, {2.0f, 0.5f, 0.5f}, 0.9f));
    CHECK_FALSE(moteur::intersects_sphere(box, {2.0f, 2.0f, 2.0f}, 1.7f));  // corner at sqrt(3) = 1.732
    CHECK(moteur::intersects_sphere(box, {2.0f, 2.0f, 2.0f}, 1.74f));
    CHECK_FALSE(moteur::intersects_sphere(moteur::Aabb{}, {0.0f, 0.0f, 0.0f}, 100.0f));
}
