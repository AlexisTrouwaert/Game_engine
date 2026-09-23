#include "moteur/camera.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <stdexcept>

namespace moteur {

glm::vec2 window_to_pixels(glm::vec2 point, glm::vec2 window_size_points, glm::vec2 window_size_pixels) {
    if (window_size_points.x <= 0.0f || window_size_points.y <= 0.0f) {
        return point;
    }
    return point * (window_size_pixels / window_size_points);
}

void Camera2D::set_zoom(float zoom) {
    if (!(zoom > 0.0f)) {
        throw std::invalid_argument("Camera2D: zoom must be positive");
    }
    zoom_ = zoom;
}

Camera2D Camera2D::interpolated(double alpha) const {
    Camera2D copy = *this;
    copy.position_ = glm::mix(previous_position_, position_, static_cast<float>(alpha));
    copy.previous_position_ = copy.position_;
    return copy;
}

// The translation of `screen = world * zoom + translation`. It puts `position` at the center of
// the window, rounded to whole pixels when snapping is on.
glm::vec2 Camera2D::translation() const {
    const glm::vec2 exact = viewport_ * 0.5f - position_ * zoom_;
    return snapping_ ? glm::round(exact) : exact;
}

glm::mat4 Camera2D::view_projection() const {
    const glm::mat4 projection = glm::orthoRH_ZO(0.0f, viewport_.x, viewport_.y, 0.0f, -1.0f, 1.0f);
    const glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(translation(), 0.0f)) *
                           glm::scale(glm::mat4(1.0f), glm::vec3(zoom_, zoom_, 1.0f));
    return projection * view;
}

glm::vec2 Camera2D::world_to_screen(glm::vec2 world) const {
    return world * zoom_ + translation();
}

glm::vec2 Camera2D::screen_to_world(glm::vec2 screen) const {
    return (screen - translation()) / zoom_;
}

Rect Camera2D::visible_rect() const {
    return {screen_to_world({0.0f, 0.0f}), screen_to_world(viewport_)};
}

}  // namespace moteur
