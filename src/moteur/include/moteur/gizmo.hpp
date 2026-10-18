#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <vector>

#include "moteur/camera3d.hpp"

namespace moteur {

// The handles of an editor (milestone 9, part 10): arrows to move along an axis, squares to move in
// a plane, rings to turn about an axis, cubes to scale. Their shape is computed in pixels (the same
// size wherever the object is), their dragging in world space. No drawing and no input here: the
// editor draws shape() and passes the pointer; plain math, unit-tested.

/// What the handles do.
enum class GizmoMode {
    Translate,  ///< arrows and plane squares
    Rotate,     ///< rings about each axis
    Scale,      ///< cubes on each axis, and one in the middle (all axes at once)
};

/// A handle of the gizmo.
enum class GizmoHandle : std::uint8_t {
    None,
    X,    ///< along (or about) x
    Y,    ///< along (or about) y
    Z,    ///< along (or about) z
    XY,   ///< in the plane z = constant (Translate)
    XZ,   ///< in the ground's plane (Translate)
    YZ,   ///< in the plane x = constant (Translate)
    All,  ///< the middle: uniform scale (Scale), or in the view's plane (Translate)
};

/// A segment of the gizmo's shape, in window pixels, with its handle (to color it).
struct GizmoLine {
    glm::vec2 a{0.0f};  ///< start, window pixels
    glm::vec2 b{0.0f};  ///< end, window pixels
    GizmoHandle handle = GizmoHandle::None;  ///< what it belongs to
};

/// What a drag did, from where it started (Gizmo::drag()).
struct GizmoDelta {
    glm::vec3 translation{0.0f};  ///< Translate: metres, world space
    glm::vec3 axis{0.0f, 1.0f, 0.0f};  ///< Rotate: the axis turned about (unit, world space)
    float angle = 0.0f;           ///< Rotate: radians, counter-clockwise looking down the axis
    glm::vec3 scale{1.0f};        ///< Scale: factors along x, y, z (1: unchanged)
};

/// Snapping of a drag (0: none).
struct GizmoSnap {
    float translation = 0.0f;  ///< metres: the translation is a multiple of it
    float angle = 0.0f;        ///< radians: the angle is a multiple of it
    float scale = 0.0f;        ///< the factors are multiples of it
};

/// A gizmo at a point of the world, seen by a camera.
class Gizmo {
public:
    /// The size of the handles, in pixels (the arrows' length).
    float size_pixels = 90.0f;
    /// How near the pointer must be to a handle, in pixels.
    float grab_pixels = 9.0f;

    /// The handle under `pointer` (window pixels) for a gizmo at `origin`, None if none. The nearest
    /// wins; the planes' squares and the middle before the axes they touch.
    GizmoHandle hit(const Camera3D& camera, glm::vec3 origin, GizmoMode mode, glm::vec2 pointer) const;
    /// The shape to draw: segments in window pixels (the parts behind the camera left out).
    std::vector<GizmoLine> shape(const Camera3D& camera, glm::vec3 origin, GizmoMode mode) const;

    /// Starts dragging `handle` with the pointer at `pointer`. False when the handle cannot be
    /// dragged from this view (an axis seen end-on).
    bool begin(const Camera3D& camera, glm::vec3 origin, GizmoMode mode, GizmoHandle handle, glm::vec2 pointer);
    /// What the drag did with the pointer now at `pointer`, from where it began, snapped.
    GizmoDelta drag(const Camera3D& camera, glm::vec2 pointer, const GizmoSnap& snap = {}) const;
    /// Ends the drag.
    void end() { dragging_ = GizmoHandle::None; }
    /// The handle being dragged; None when not dragging.
    GizmoHandle dragging() const { return dragging_; }

    /// The world length of the handles at `origin` seen by `camera` (size_pixels on the screen).
    float world_size(const Camera3D& camera, glm::vec3 origin) const;

private:
    std::optional<glm::vec3> on_handle(const Ray& ray) const;  ///< the point grabbed, for dragging_

    GizmoHandle dragging_ = GizmoHandle::None;
    GizmoMode mode_ = GizmoMode::Translate;
    glm::vec3 origin_{0.0f};
    glm::vec3 start_{0.0f};       ///< the point grabbed (on the axis, in the plane)
    glm::vec2 start_pointer_{0.0f};
    glm::vec2 screen_origin_{0.0f};
};

/// The unit vector of an axis handle (X, Y, Z); the plane's normal for XY, XZ, YZ.
glm::vec3 gizmo_axis(GizmoHandle handle);

/// Rounds `value` to a multiple of `step` (unchanged when step <= 0).
float snap_value(float value, float step);
/// Each coordinate rounded to a multiple of `step`.
glm::vec3 snap_position(glm::vec3 value, float step);

}  // namespace moteur
