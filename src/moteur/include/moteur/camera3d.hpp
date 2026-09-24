#pragma once

#include <glm/glm.hpp>

#include <optional>

#include "moteur/aabb.hpp"

namespace moteur {

// A half-line from `origin` along `direction` (unit length).
struct Ray {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};

    // Where the ray crosses the horizontal plane y = height, if it does in front of its origin.
    std::optional<glm::vec3> hit_height(float height = 0.0f) const;
};

// The six planes around what a camera sees. Each plane is (normal, d), normal pointing inside:
// a point p is inside when dot(normal, p) + d >= 0 for all six.
struct Frustum {
    glm::vec4 planes[6] = {};  // left, right, bottom, top, near, far; all zero: contains everything

    // From any view-projection matrix whose clip depth is in [0, 1].
    static Frustum from_view_projection(const glm::mat4& view_projection);
    bool contains(glm::vec3 point) const;
    // True when the box may be visible: false only when it lies entirely outside one plane, which
    // never hides a visible box (a few invisible ones near the corners pass: fine for culling).
    bool intersects(const Aabb& box) const;
};

enum class Projection {
    Orthographic,  // parallel lines stay parallel: true isometric, sizes do not depend on distance
    Perspective,   // far things look smaller
};

// A camera that looks at a target point from fixed angles, as in an isometric ARPG.
//
// World conventions: right-handed, Y up, metres. Clip depth in [0, 1] (Direct3D 12 and Metal).
//
// Framing does not depend on the projection: `visible_height` is the height of world, in metres,
// that the window shows *at the target*. An orthographic and a perspective camera with the same
// settings frame the target identically; they only differ by the perspective itself.
//
// Defaults: the framing chosen for the game (milestone 3, part 4): perspective, 50 degrees below
// the horizon, 30 degree field of view, looking along the diagonal of the grid.
//
// Pixels are window pixels, origin at the top left, y pointing down (as Renderer::width() counts
// them; convert mouse positions with Application::to_pixels() first on high-density screens).
//
// Smooth motion, like Camera2D: call begin_update() at the start of each fixed update, move the
// camera, and draw (and pick) with interpolated(alpha).
class Camera3D {
public:
    void set_target(glm::vec3 target) { target_ = target; }
    glm::vec3 target() const { return target_; }

    // yaw: rotation around the vertical axis, in degrees (45 looks along the diagonal of the grid).
    // pitch: angle below the horizon, in degrees, clamped to [1, 89].
    void set_angles(float yaw_degrees, float pitch_degrees);
    float yaw() const { return yaw_; }
    float pitch() const { return pitch_; }

    void set_projection(Projection projection) { projection_ = projection; }
    Projection projection() const { return projection_; }

    // Metres of world shown vertically at the target. Clamped to at least 0.1.
    void set_visible_height(float metres);
    float visible_height() const { return visible_height_; }

    // Vertical field of view of the perspective projection, in degrees, clamped to [5, 120].
    void set_field_of_view(float degrees);
    float field_of_view() const { return fov_; }

    // Size of the window in pixels (its aspect ratio sets the visible width).
    void set_viewport(glm::vec2 size) { viewport_ = size; }
    glm::vec2 viewport() const { return viewport_; }

    // Where the eye is. In perspective, its distance to the target follows from the field of view
    // and the visible height; in orthographic, it is far enough back to see everything around the target.
    glm::vec3 position() const;
    // Unit vector from the eye towards the target.
    glm::vec3 forward() const;
    float distance() const;

    glm::mat4 view() const;
    glm::mat4 projection_matrix() const;
    glm::mat4 view_projection() const { return projection_matrix() * view(); }

    // The ray under a window pixel: what the mouse points at.
    Ray screen_ray(glm::vec2 pixel) const;
    // The point of the ground (y = height) under a window pixel, if the pixel sees the ground.
    std::optional<glm::vec3> ground_point(glm::vec2 pixel, float height = 0.0f) const;
    // Where a point of the world appears, in window pixels; nothing if it is behind the camera.
    // (It may be outside the window.)
    std::optional<glm::vec2> world_to_screen(glm::vec3 world) const;
    Frustum frustum() const { return Frustum::from_view_projection(view_projection()); }

    // Remembers the current target and framing, to interpolate from them (see interpolated()).
    void begin_update() {
        previous_target_ = target_;
        previous_visible_height_ = visible_height_;
    }
    // A copy between the state of the last begin_update() and the current one (alpha in [0, 1]).
    Camera3D interpolated(double alpha) const;
    // Moves the target towards `goal`, covering half the remaining distance every `half_life`
    // seconds: smooth, and the same at every tick rate (an exponential, not a fixed fraction per step).
    void follow(glm::vec3 goal, float dt, float half_life);

    // The pitch at which the three axes look equally long in an orthographic view: the classic
    // "true isometric" angle, atan(1 / sqrt(2)), about 35.26 degrees.
    static float isometric_pitch();

private:
    glm::vec3 target_{0.0f};
    glm::vec3 previous_target_{0.0f};
    float previous_visible_height_ = 12.0f;
    float yaw_ = 45.0f;
    float pitch_ = 50.0f;
    Projection projection_ = Projection::Perspective;
    float visible_height_ = 12.0f;
    float fov_ = 30.0f;
    glm::vec2 viewport_{1280.0f, 720.0f};
};

}  // namespace moteur
