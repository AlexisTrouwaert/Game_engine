#pragma once

#include <glm/glm.hpp>

namespace moteur {

// An axis-aligned rectangle, for example the part of the world a camera can see.
struct Rect {
    glm::vec2 min{0.0f};
    glm::vec2 max{0.0f};

    glm::vec2 size() const { return max - min; }
    glm::vec2 center() const { return (min + max) * 0.5f; }
    bool contains(glm::vec2 point) const {
        return point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y;
    }
};

// Converts a position expressed in window points (what SDL reports for the mouse) into pixels
// (what the renderer draws in). The two differ on high-density screens, where one point is
// several pixels. Returns the point unchanged if a size is zero.
glm::vec2 window_to_pixels(glm::vec2 point, glm::vec2 window_size_points, glm::vec2 window_size_pixels);

// Decides which part of the world is seen, and how big.
//
// World space: units are pixels at zoom 1, origin anywhere, y pointing down.
// Screen space: pixels of the window, origin at the top left, y pointing down.
//
//   screen = world * zoom + translation
//
// `position` is the world point shown at the center of the window. `zoom` is how many screen
// pixels one world pixel covers: 2 makes everything twice as big. The camera does not rotate.
//
// Pixel snapping (on by default): the translation is rounded to whole screen pixels, so that
// pixel art is never drawn at fractional positions. Without it, moving the camera smoothly makes
// texels of different sizes shimmer and can open one-pixel seams between tiles. Drawing and
// picking both use the snapped translation, so they always agree.
class Camera2D {
public:
    // Size of the window in pixels: set it every frame from the renderer, so that resizing works.
    void set_viewport(glm::vec2 size) { viewport_ = size; }
    glm::vec2 viewport() const { return viewport_; }

    void set_position(glm::vec2 world) { position_ = world; }
    glm::vec2 position() const { return position_; }

    // Throws std::invalid_argument if zoom is not positive.
    void set_zoom(float zoom);
    float zoom() const { return zoom_; }

    void set_pixel_snapping(bool enabled) { snapping_ = enabled; }
    bool pixel_snapping() const { return snapping_; }

    // Call at the start of every fixed update, before moving the camera: it remembers where the
    // camera was so that rendering can interpolate between two updates.
    void begin_update() { previous_position_ = position_; }

    // A copy of the camera placed between its previous and current positions (alpha in [0, 1]).
    // Use it for drawing and picking, so the camera moves smoothly at any frame rate.
    Camera2D interpolated(double alpha) const;

    // Maps world coordinates to clip space (what the GPU expects), for the y-down pixel convention.
    glm::mat4 view_projection() const;

    glm::vec2 world_to_screen(glm::vec2 world) const;
    glm::vec2 screen_to_world(glm::vec2 screen) const;

    // The part of the world currently on screen.
    Rect visible_rect() const;

private:
    glm::vec2 translation() const;

    glm::vec2 viewport_{1280.0f, 720.0f};
    glm::vec2 position_{0.0f};
    glm::vec2 previous_position_{0.0f};
    float zoom_ = 1.0f;
    bool snapping_ = true;
};

}  // namespace moteur
