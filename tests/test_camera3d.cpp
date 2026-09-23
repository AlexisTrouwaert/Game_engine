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
