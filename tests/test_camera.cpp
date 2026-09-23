#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <stdexcept>

#include "moteur/camera.hpp"

using moteur::Camera2D;

namespace {

Camera2D camera(glm::vec2 position = {0.0f, 0.0f}, float zoom = 1.0f, glm::vec2 viewport = {1280.0f, 720.0f}) {
    Camera2D cam;
    cam.set_viewport(viewport);
    cam.set_position(position);
    cam.set_zoom(zoom);
    return cam;
}

bool near_vec(glm::vec2 a, glm::vec2 b, float tolerance = 1e-3f) {
    return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance;
}

}  // namespace

TEST_CASE("the camera position is shown at the center of the window") {
    const Camera2D cam = camera({0.0f, 0.0f});
    CHECK(cam.world_to_screen({0.0f, 0.0f}) == glm::vec2(640.0f, 360.0f));

    const Camera2D moved = camera({100.0f, -40.0f});
    CHECK(moved.world_to_screen({100.0f, -40.0f}) == glm::vec2(640.0f, 360.0f));
    CHECK(moved.screen_to_world({640.0f, 360.0f}) == glm::vec2(100.0f, -40.0f));
}

TEST_CASE("zoom scales distances around the center") {
    const Camera2D cam = camera({0.0f, 0.0f}, 2.0f);
    CHECK(cam.world_to_screen({10.0f, 0.0f}) == glm::vec2(660.0f, 360.0f));
    CHECK(cam.world_to_screen({0.0f, -5.0f}) == glm::vec2(640.0f, 350.0f));
    CHECK(cam.screen_to_world({660.0f, 360.0f}) == glm::vec2(10.0f, 0.0f));
}

TEST_CASE("the y axis points down on the screen") {
    const Camera2D cam = camera();
    CHECK(cam.world_to_screen({0.0f, 10.0f}).y > cam.world_to_screen({0.0f, 0.0f}).y);
}

TEST_CASE("world to screen to world is the identity") {
    for (const bool snapping : {true, false}) {
        Camera2D cam = camera({123.4f, -56.7f}, 3.0f);
        cam.set_pixel_snapping(snapping);
        for (float x = -500.0f; x <= 500.0f; x += 77.7f) {
            for (float y = -400.0f; y <= 400.0f; y += 63.3f) {
                const glm::vec2 world(x, y);
                CHECK(near_vec(cam.screen_to_world(cam.world_to_screen(world)), world));
            }
        }
    }
}

TEST_CASE("pixel snapping puts the world on whole screen pixels") {
    Camera2D cam = camera({0.3f, 0.7f});
    // Without snapping the origin lands between pixels.
    cam.set_pixel_snapping(false);
    CHECK(near_vec(cam.world_to_screen({0.0f, 0.0f}), {639.7f, 359.3f}));
    // With snapping it lands on a whole pixel, so a sprite at an integer world position is not blurred or shifted.
    cam.set_pixel_snapping(true);
    CHECK(cam.world_to_screen({0.0f, 0.0f}) == glm::vec2(640.0f, 359.0f));
    CHECK(cam.world_to_screen({5.0f, 5.0f}) == glm::vec2(645.0f, 364.0f));
}

TEST_CASE("with snapping, integer world positions always land on whole pixels") {
    Camera2D cam = camera({0.0f, 0.0f}, 3.0f, {1281.0f, 721.0f});  // odd size: the center is at .5
    for (float px = -10.0f; px <= 10.0f; px += 0.37f) {
        cam.set_position({px, px * 0.5f + 0.13f});
        const glm::vec2 screen = cam.world_to_screen({7.0f, -3.0f});
        CHECK(screen.x == std::round(screen.x));
        CHECK(screen.y == std::round(screen.y));
    }
}

TEST_CASE("the visible rectangle covers exactly the window") {
    const Camera2D cam = camera({100.0f, 50.0f}, 2.0f);
    const moteur::Rect visible = cam.visible_rect();
    CHECK(visible.min == glm::vec2(-220.0f, -130.0f));
    CHECK(visible.max == glm::vec2(420.0f, 230.0f));
    CHECK(visible.contains({100.0f, 50.0f}));
    CHECK_FALSE(visible.contains({500.0f, 50.0f}));
}

TEST_CASE("zooming in shows less of the world") {
    const glm::vec2 wide = camera({0.0f, 0.0f}, 1.0f).visible_rect().size();
    const glm::vec2 narrow = camera({0.0f, 0.0f}, 4.0f).visible_rect().size();
    CHECK(narrow.x == doctest::Approx(wide.x / 4.0f));
    CHECK(narrow.y == doctest::Approx(wide.y / 4.0f));
}

TEST_CASE("resizing the window keeps the position at the center") {
    Camera2D cam = camera({10.0f, 20.0f});
    cam.set_viewport({800.0f, 600.0f});
    CHECK(cam.world_to_screen({10.0f, 20.0f}) == glm::vec2(400.0f, 300.0f));
}

TEST_CASE("interpolation places the camera between two updates") {
    Camera2D cam = camera({0.0f, 0.0f});
    cam.begin_update();
    cam.set_position({10.0f, 20.0f});

    CHECK(cam.interpolated(0.0).position() == glm::vec2(0.0f, 0.0f));
    CHECK(cam.interpolated(0.5).position() == glm::vec2(5.0f, 10.0f));
    CHECK(cam.interpolated(1.0).position() == glm::vec2(10.0f, 20.0f));
    CHECK(cam.position() == glm::vec2(10.0f, 20.0f));  // the original is untouched
}

TEST_CASE("an interpolated camera keeps the other settings") {
    Camera2D cam = camera({0.0f, 0.0f}, 3.0f, {800.0f, 600.0f});
    cam.set_pixel_snapping(false);
    const Camera2D copy = cam.interpolated(0.5);
    CHECK(copy.zoom() == 3.0f);
    CHECK(copy.viewport() == glm::vec2(800.0f, 600.0f));
    CHECK_FALSE(copy.pixel_snapping());
}

TEST_CASE("the view-projection maps the window corners to the clip-space corners") {
    const Camera2D cam = camera({100.0f, 50.0f}, 2.0f);
    const glm::mat4 vp = cam.view_projection();
    const moteur::Rect visible = cam.visible_rect();

    const glm::vec4 top_left = vp * glm::vec4(visible.min, 0.0f, 1.0f);
    const glm::vec4 bottom_right = vp * glm::vec4(visible.max, 0.0f, 1.0f);
    const glm::vec4 center = vp * glm::vec4(100.0f, 50.0f, 0.0f, 1.0f);

    CHECK(top_left.x == doctest::Approx(-1.0f));
    CHECK(top_left.y == doctest::Approx(1.0f));  // the top of the window is +1 in clip space
    CHECK(bottom_right.x == doctest::Approx(1.0f));
    CHECK(bottom_right.y == doctest::Approx(-1.0f));
    CHECK(center.x == doctest::Approx(0.0f));
    CHECK(center.y == doctest::Approx(0.0f));
}

TEST_CASE("the view-projection agrees with world_to_screen") {
    Camera2D cam = camera({33.3f, -12.5f}, 3.0f);
    const glm::mat4 vp = cam.view_projection();
    const glm::vec2 viewport = cam.viewport();
    for (const glm::vec2 world : {glm::vec2(0.0f), glm::vec2(50.0f, 20.0f), glm::vec2(-70.0f, 90.0f)}) {
        const glm::vec4 clip = vp * glm::vec4(world, 0.0f, 1.0f);
        const glm::vec2 screen_from_clip((clip.x * 0.5f + 0.5f) * viewport.x, (0.5f - clip.y * 0.5f) * viewport.y);
        CHECK(near_vec(screen_from_clip, cam.world_to_screen(world), 1e-2f));
    }
}

TEST_CASE("the zoom must be positive") {
    Camera2D cam;
    CHECK_THROWS_AS(cam.set_zoom(0.0f), std::invalid_argument);
    CHECK_THROWS_AS(cam.set_zoom(-1.0f), std::invalid_argument);
    CHECK_THROWS_AS(cam.set_zoom(std::numeric_limits<float>::quiet_NaN()), std::invalid_argument);
    CHECK(cam.zoom() == 1.0f);
}

TEST_CASE("window points convert to pixels on a high-density screen") {
    // A 640x360 point window drawn at 1280x720 pixels: one point is two pixels.
    CHECK(moteur::window_to_pixels({100.0f, 50.0f}, {640.0f, 360.0f}, {1280.0f, 720.0f}) == glm::vec2(200.0f, 100.0f));
    // Same size: nothing changes.
    CHECK(moteur::window_to_pixels({100.0f, 50.0f}, {1280.0f, 720.0f}, {1280.0f, 720.0f}) == glm::vec2(100.0f, 50.0f));
    // A degenerate window does not divide by zero.
    CHECK(moteur::window_to_pixels({100.0f, 50.0f}, {0.0f, 0.0f}, {1280.0f, 720.0f}) == glm::vec2(100.0f, 50.0f));
}
