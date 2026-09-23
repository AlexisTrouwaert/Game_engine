#pragma once

#include <glm/glm.hpp>

namespace moteur {

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
// Provisional (milestone 3, part 4): picking (a ray from the mouse), the frustum, interpolation and
// following a target come with part 3.
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

    // The pitch at which the three axes look equally long in an orthographic view: the classic
    // "true isometric" angle, atan(1 / sqrt(2)), about 35.26 degrees.
    static float isometric_pitch();

private:
    glm::vec3 target_{0.0f};
    float yaw_ = 45.0f;
    float pitch_ = 50.0f;
    Projection projection_ = Projection::Perspective;
    float visible_height_ = 12.0f;
    float fov_ = 30.0f;
    glm::vec2 viewport_{1280.0f, 720.0f};
};

}  // namespace moteur
