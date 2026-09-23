#include "moteur/camera3d.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace moteur {

namespace {

// Distance of an orthographic eye from its target: only the near and far planes depend on it,
// so it just has to put the eye clear of anything a level contains.
constexpr float kOrthographicDistance = 100.0f;
// How far behind the target the far plane reaches. Kept tight: depth precision is spread between
// the near and far planes.
constexpr float kDepthBeyondTarget = 150.0f;

}  // namespace

void Camera3D::set_angles(float yaw_degrees, float pitch_degrees) {
    yaw_ = yaw_degrees;
    pitch_ = std::clamp(pitch_degrees, 1.0f, 89.0f);
}

void Camera3D::set_visible_height(float metres) {
    visible_height_ = std::max(metres, 0.1f);
}

void Camera3D::set_field_of_view(float degrees) {
    fov_ = std::clamp(degrees, 5.0f, 120.0f);
}

float Camera3D::isometric_pitch() {
    return glm::degrees(std::atan(1.0f / std::sqrt(2.0f)));
}

glm::vec3 Camera3D::forward() const {
    // Yaw 0 looks towards -Z; positive yaw turns counter-clockwise seen from above. Pitch looks down.
    const float yaw = glm::radians(yaw_);
    const float pitch = glm::radians(pitch_);
    return glm::vec3(-std::sin(yaw) * std::cos(pitch), -std::sin(pitch), -std::cos(yaw) * std::cos(pitch));
}

float Camera3D::distance() const {
    if (projection_ == Projection::Orthographic) {
        return kOrthographicDistance;
    }
    // The visible height at the target: 2 * distance * tan(fov / 2).
    return visible_height_ * 0.5f / std::tan(glm::radians(fov_) * 0.5f);
}

glm::vec3 Camera3D::position() const {
    return target_ - forward() * distance();
}

glm::mat4 Camera3D::view() const {
    return glm::lookAtRH(position(), target_, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 Camera3D::projection_matrix() const {
    const float aspect = viewport_.y > 0.0f ? viewport_.x / viewport_.y : 1.0f;
    const float far_plane = distance() + kDepthBeyondTarget;
    if (projection_ == Projection::Orthographic) {
        const float half_height = visible_height_ * 0.5f;
        const float half_width = half_height * aspect;
        return glm::orthoRH_ZO(-half_width, half_width, -half_height, half_height, 0.1f, far_plane);
    }
    // The near plane is pushed as far as possible: most of the depth precision sits near it.
    const float near_plane = std::max(0.1f, distance() * 0.05f);
    return glm::perspectiveRH_ZO(glm::radians(fov_), aspect, near_plane, far_plane);
}

}  // namespace moteur
