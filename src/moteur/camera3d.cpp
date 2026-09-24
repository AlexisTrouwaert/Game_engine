#include "moteur/camera3d.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include "moteur/fixed_timestep.hpp"

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

std::optional<glm::vec3> Ray::hit_height(float height) const {
    if (std::abs(direction.y) < 1e-6f) {
        return std::nullopt;  // parallel to the plane
    }
    const float t = (height - origin.y) / direction.y;
    if (t < 0.0f) {
        return std::nullopt;  // behind the origin
    }
    return origin + direction * t;
}

Frustum Frustum::from_view_projection(const glm::mat4& m) {
    // Rows of the matrix (GLM stores columns): clip = (row0.p, row1.p, row2.p, row3.p). Inside means
    // -w <= x <= w, -w <= y <= w, 0 <= z <= w (Gribb and Hartmann, for depth in [0, 1]).
    const auto row = [&m](int i) { return glm::vec4(m[0][i], m[1][i], m[2][i], m[3][i]); };
    Frustum frustum;
    frustum.planes[0] = row(3) + row(0);
    frustum.planes[1] = row(3) - row(0);
    frustum.planes[2] = row(3) + row(1);
    frustum.planes[3] = row(3) - row(1);
    frustum.planes[4] = row(2);
    frustum.planes[5] = row(3) - row(2);
    for (glm::vec4& plane : frustum.planes) {
        plane /= glm::length(glm::vec3(plane));
    }
    return frustum;
}

bool Frustum::contains(glm::vec3 point) const {
    for (const glm::vec4& plane : planes) {
        if (glm::dot(glm::vec3(plane), point) + plane.w < 0.0f) {
            return false;
        }
    }
    return true;
}

bool Frustum::intersects(const Aabb& box) const {
    if (box.empty()) {
        return false;
    }
    for (const glm::vec4& plane : planes) {
        // The corner farthest along the plane's normal: if even it is outside, the whole box is.
        const glm::vec3 far_corner(plane.x >= 0.0f ? box.max.x : box.min.x, plane.y >= 0.0f ? box.max.y : box.min.y,
                                   plane.z >= 0.0f ? box.max.z : box.min.z);
        if (glm::dot(glm::vec3(plane), far_corner) + plane.w < 0.0f) {
            return false;
        }
    }
    return true;
}

Ray Camera3D::screen_ray(glm::vec2 pixel) const {
    // Window pixel -> clip space (y up), then back through the camera, on the near and far planes.
    const glm::vec2 ndc(pixel.x / viewport_.x * 2.0f - 1.0f, 1.0f - pixel.y / viewport_.y * 2.0f);
    const glm::mat4 inverse = glm::inverse(view_projection());
    const glm::vec4 near_point = inverse * glm::vec4(ndc, 0.0f, 1.0f);
    const glm::vec4 far_point = inverse * glm::vec4(ndc, 1.0f, 1.0f);
    const glm::vec3 from = glm::vec3(near_point) / near_point.w;
    const glm::vec3 to = glm::vec3(far_point) / far_point.w;
    return {from, glm::normalize(to - from)};
}

std::optional<glm::vec3> Camera3D::ground_point(glm::vec2 pixel, float height) const {
    return screen_ray(pixel).hit_height(height);
}

std::optional<glm::vec2> Camera3D::world_to_screen(glm::vec3 world) const {
    const glm::vec4 clip = view_projection() * glm::vec4(world, 1.0f);
    if (clip.w <= 1e-6f) {
        return std::nullopt;
    }
    const glm::vec2 ndc = glm::vec2(clip) / clip.w;
    return glm::vec2((ndc.x * 0.5f + 0.5f) * viewport_.x, (0.5f - ndc.y * 0.5f) * viewport_.y);
}

Camera3D Camera3D::interpolated(double alpha) const {
    Camera3D copy = *this;
    const auto t = static_cast<float>(alpha);
    copy.target_ = interpolate(previous_target_, target_, t);
    copy.visible_height_ = interpolate(previous_visible_height_, visible_height_, t);
    copy.previous_target_ = copy.target_;
    copy.previous_visible_height_ = copy.visible_height_;
    return copy;
}

void Camera3D::follow(glm::vec3 goal, float dt, float half_life) {
    if (half_life <= 0.0f) {
        target_ = goal;
        return;
    }
    const float kept = std::exp2(-dt / half_life);  // fraction of the remaining distance still to cover
    target_ = goal + (target_ - goal) * kept;
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
