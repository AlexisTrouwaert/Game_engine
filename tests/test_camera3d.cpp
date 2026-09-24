#include <doctest/doctest.h>

#include <glm/glm.hpp>

#include "moteur/camera3d.hpp"

namespace {

// Normalized device coordinates of a world point: x and y in [-1, 1] on screen, z the depth in [0, 1].
glm::vec3 to_ndc(const moteur::Camera3D& camera, glm::vec3 world) {
    const glm::vec4 clip = camera.view_projection() * glm::vec4(world, 1.0f);
    return glm::vec3(clip) / clip.w;
}

// The same point in window pixels, centred on the middle of the window (y up).
glm::vec2 to_pixels(const moteur::Camera3D& camera, glm::vec3 world) {
    const glm::vec3 ndc = to_ndc(camera, world);
    return glm::vec2(ndc.x, ndc.y) * camera.viewport() * 0.5f;
}

moteur::Camera3D make_camera(moteur::Projection projection) {
    moteur::Camera3D camera;
    camera.set_target({10.0f, 0.0f, -4.0f});
    camera.set_angles(45.0f, 50.0f);
    camera.set_visible_height(12.0f);
    camera.set_field_of_view(30.0f);
    camera.set_viewport({1280.0f, 720.0f});
    camera.set_projection(projection);
    return camera;
}

}  // namespace

TEST_CASE("Camera3D puts its target in the middle of the window, within the depth range") {
    for (const moteur::Projection projection : {moteur::Projection::Orthographic, moteur::Projection::Perspective}) {
        const moteur::Camera3D camera = make_camera(projection);
        const glm::vec3 ndc = to_ndc(camera, camera.target());
        CHECK(ndc.x == doctest::Approx(0.0f).epsilon(1e-5));
        CHECK(ndc.y == doctest::Approx(0.0f).epsilon(1e-5));
        CHECK(ndc.z > 0.0f);
        CHECK(ndc.z < 1.0f);
    }
}

TEST_CASE("Camera3D: both projections frame the target identically") {
    // Half the visible height above the target, measured across the view, touches the top edge.
    for (const moteur::Projection projection : {moteur::Projection::Orthographic, moteur::Projection::Perspective}) {
        const moteur::Camera3D camera = make_camera(projection);
        const glm::vec3 forward = camera.forward();
        const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
        const glm::vec3 up = glm::cross(right, forward);
        const glm::vec3 top = camera.target() + up * (camera.visible_height() * 0.5f);
        CHECK(to_ndc(camera, top).y == doctest::Approx(1.0f).epsilon(1e-4));
        // And the aspect ratio sets the width: 16/9 of the height at the right edge.
        const glm::vec3 side = camera.target() + right * (camera.visible_height() * 0.5f * 1280.0f / 720.0f);
        CHECK(to_ndc(camera, side).x == doctest::Approx(1.0f).epsilon(1e-4));
    }
}

TEST_CASE("Camera3D: nearer points have a smaller depth") {
    for (const moteur::Projection projection : {moteur::Projection::Orthographic, moteur::Projection::Perspective}) {
        const moteur::Camera3D camera = make_camera(projection);
        const glm::vec3 nearer = camera.target() - camera.forward() * 2.0f;
        const glm::vec3 farther = camera.target() + camera.forward() * 2.0f;
        CHECK(to_ndc(camera, nearer).z < to_ndc(camera, camera.target()).z);
        CHECK(to_ndc(camera, camera.target()).z < to_ndc(camera, farther).z);
    }
}

TEST_CASE("Camera3D: only the perspective shrinks what is far away") {
    // A 1 m bar lying across the view at the target, and the same bar 10 m further along the ground.
    // Across the view, its length on screen only depends on its distance (a vertical pole would also
    // be seen from a different angle, which hides part of the effect).
    for (const moteur::Projection projection : {moteur::Projection::Orthographic, moteur::Projection::Perspective}) {
        const moteur::Camera3D camera = make_camera(projection);
        const glm::vec3 right = glm::normalize(glm::cross(camera.forward(), glm::vec3(0.0f, 1.0f, 0.0f)));
        const glm::vec3 away = glm::normalize(glm::vec3(camera.forward().x, 0.0f, camera.forward().z)) * 10.0f;
        const auto bar_length = [&](glm::vec3 centre) {
            return glm::length(to_pixels(camera, centre + right * 0.5f) - to_pixels(camera, centre - right * 0.5f));
        };
        const float near_bar = bar_length(camera.target());
        const float far_bar = bar_length(camera.target() + away);
        if (projection == moteur::Projection::Orthographic) {
            CHECK(far_bar == doctest::Approx(near_bar).epsilon(1e-4));
        } else {
            // 10 m along the ground adds 10 * cos(50°) = 6.4 m of depth to the 22.4 m of the target.
            CHECK(far_bar == doctest::Approx(near_bar * camera.distance() / (camera.distance() + 6.428f)).epsilon(1e-3));
        }
    }
}

TEST_CASE("Camera3D: at the isometric angle, the three axes look equally long") {
    moteur::Camera3D camera = make_camera(moteur::Projection::Orthographic);
    camera.set_angles(45.0f, moteur::Camera3D::isometric_pitch());
    CHECK(camera.pitch() == doctest::Approx(35.264f).epsilon(1e-3));
    const glm::vec2 origin = to_pixels(camera, camera.target());
    const float x = glm::length(to_pixels(camera, camera.target() + glm::vec3(1, 0, 0)) - origin);
    const float y = glm::length(to_pixels(camera, camera.target() + glm::vec3(0, 1, 0)) - origin);
    const float z = glm::length(to_pixels(camera, camera.target() + glm::vec3(0, 0, 1)) - origin);
    CHECK(x == doctest::Approx(y).epsilon(1e-4));
    CHECK(y == doctest::Approx(z).epsilon(1e-4));
}

TEST_CASE("Camera3D: yaw 0 looks towards -Z, and settings are clamped") {
    moteur::Camera3D camera;
    camera.set_angles(0.0f, 30.0f);
    CHECK(camera.forward().x == doctest::Approx(0.0f).epsilon(1e-6));
    CHECK(camera.forward().z < 0.0f);
    CHECK(camera.forward().y < 0.0f);
    CHECK(glm::length(camera.forward()) == doctest::Approx(1.0f));

    camera.set_angles(0.0f, 95.0f);
    CHECK(camera.pitch() == 89.0f);
    camera.set_angles(0.0f, -10.0f);
    CHECK(camera.pitch() == 1.0f);
    camera.set_visible_height(0.0f);
    CHECK(camera.visible_height() == doctest::Approx(0.1f));
    camera.set_field_of_view(500.0f);
    CHECK(camera.field_of_view() == 120.0f);
}

TEST_CASE("Camera3D picking: the ground point under a pixel projects back onto that pixel") {
    for (const moteur::Projection projection : {moteur::Projection::Perspective, moteur::Projection::Orthographic}) {
        for (const float visible_height : {6.0f, 12.0f, 40.0f}) {  // several zoom levels
            moteur::Camera3D camera = make_camera(projection);
            camera.set_visible_height(visible_height);
            // The middle of the window sees the target.
            const auto middle = camera.ground_point({640.0f, 360.0f});
            REQUIRE(middle.has_value());
            CHECK(glm::length(*middle - camera.target()) < 1e-3f);
            // Every corner and a few points in between, round trip through the world.
            for (const glm::vec2 pixel : {glm::vec2(0, 0), glm::vec2(1279, 0), glm::vec2(0, 719), glm::vec2(1279, 719),
                                          glm::vec2(200, 500), glm::vec2(1000, 100)}) {
                const auto ground = camera.ground_point(pixel);
                REQUIRE(ground.has_value());
                CHECK(ground->y == doctest::Approx(0.0f).epsilon(1e-4));
                const auto back = camera.world_to_screen(*ground);
                REQUIRE(back.has_value());
                CHECK(glm::length(*back - pixel) < 0.01f);
            }
        }
    }
}

TEST_CASE("Camera3D picking: a pixel near the top sees farther than one near the bottom") {
    const moteur::Camera3D camera = make_camera(moteur::Projection::Perspective);
    const glm::vec3 top = *camera.ground_point({640.0f, 10.0f});
    const glm::vec3 bottom = *camera.ground_point({640.0f, 710.0f});
    CHECK(glm::length(top - camera.position()) > glm::length(bottom - camera.position()));
    // Along the view direction: the top of the window is further ahead on the ground.
    const glm::vec3 ahead = glm::normalize(glm::vec3(camera.forward().x, 0.0f, camera.forward().z));
    CHECK(glm::dot(top - bottom, ahead) > 0.0f);
}

TEST_CASE("Camera3D world_to_screen: nothing for a point behind the camera") {
    const moteur::Camera3D camera = make_camera(moteur::Projection::Perspective);
    CHECK_FALSE(camera.world_to_screen(camera.position() - camera.forward() * 5.0f).has_value());
    CHECK(camera.world_to_screen(camera.target()).has_value());
}

TEST_CASE("Ray hit_height: in front only, never when parallel") {
    const moteur::Ray down{{0.0f, 10.0f, 0.0f}, {0.0f, -1.0f, 0.0f}};
    CHECK(down.hit_height(0.0f)->y == doctest::Approx(0.0f));
    CHECK(down.hit_height(2.0f)->y == doctest::Approx(2.0f));
    CHECK_FALSE(down.hit_height(20.0f).has_value());  // above the origin: behind it
    const moteur::Ray flat{{0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
    CHECK_FALSE(flat.hit_height(0.0f).has_value());
}

TEST_CASE("Frustum: boxes inside, outside, across an edge, and the visible ground") {
    for (const moteur::Projection projection : {moteur::Projection::Perspective, moteur::Projection::Orthographic}) {
        const moteur::Camera3D camera = make_camera(projection);
        const moteur::Frustum frustum = camera.frustum();
        const auto box = [](glm::vec3 center, float half) {
            moteur::Aabb b;
            b.add(center - glm::vec3(half));
            b.add(center + glm::vec3(half));
            return b;
        };
        CHECK(frustum.contains(camera.target()));
        CHECK(frustum.intersects(box(camera.target(), 0.5f)));
        CHECK_FALSE(frustum.intersects(box(camera.position() - camera.forward() * 10.0f, 0.5f)));  // behind
        const glm::vec3 right = glm::normalize(glm::cross(camera.forward(), glm::vec3(0, 1, 0)));
        CHECK_FALSE(frustum.intersects(box(camera.target() + right * 60.0f, 1.0f)));  // far to the side
        // A box straddling the right edge of the view is kept.
        const glm::vec3 edge = *camera.ground_point({1280.0f, 360.0f});
        CHECK(frustum.intersects(box(edge, 1.0f)));
        CHECK_FALSE(frustum.contains(edge + right * 2.0f));
        // Every visible ground point is inside.
        for (float x = 20.0f; x < 1280.0f; x += 310.0f) {
            for (float y = 20.0f; y < 720.0f; y += 170.0f) {
                CHECK(frustum.contains(*camera.ground_point({x, y}) + glm::vec3(0.0f, 0.001f, 0.0f)));
            }
        }
        CHECK_FALSE(frustum.intersects(moteur::Aabb{}));  // an empty box is never visible
    }
}

TEST_CASE("Camera3D interpolation between two fixed updates") {
    moteur::Camera3D camera = make_camera(moteur::Projection::Perspective);
    camera.begin_update();
    const glm::vec3 from = camera.target();
    camera.set_target(from + glm::vec3(4.0f, 0.0f, -2.0f));
    camera.set_visible_height(20.0f);
    const moteur::Camera3D halfway = camera.interpolated(0.5);
    CHECK(glm::length(halfway.target() - (from + glm::vec3(2.0f, 0.0f, -1.0f))) < 1e-5f);
    CHECK(halfway.visible_height() == doctest::Approx(16.0f));
    CHECK(glm::length(camera.interpolated(0.0).target() - from) < 1e-5f);
    CHECK(glm::length(camera.interpolated(1.0).target() - camera.target()) < 1e-5f);
}

TEST_CASE("Camera3D follow: half the way per half-life, whatever the tick rate") {
    moteur::Camera3D coarse;
    moteur::Camera3D fine;
    const glm::vec3 goal(10.0f, 0.0f, 0.0f);
    coarse.follow(goal, 0.2f, 0.2f);
    CHECK(coarse.target().x == doctest::Approx(5.0f));
    for (int i = 0; i < 4; ++i) {
        fine.follow(goal, 0.05f, 0.2f);
    }
    CHECK(fine.target().x == doctest::Approx(coarse.target().x).epsilon(1e-5));
    moteur::Camera3D snap;
    snap.follow(goal, 0.01f, 0.0f);  // no smoothing
    CHECK(snap.target() == goal);
}
