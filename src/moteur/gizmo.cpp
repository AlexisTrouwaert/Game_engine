#include "moteur/gizmo.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace moteur {

namespace {

constexpr int kRingSegments = 48;
constexpr float kPlaneNear = 0.25f;  // the plane squares, as shares of the arrows' length
constexpr float kPlaneFar = 0.45f;
constexpr float kMiddle = 7.0f;      // pixels: half the middle square
constexpr float kPi = 3.14159265358979f;

float distance_to_segment(glm::vec2 p, glm::vec2 a, glm::vec2 b) {
    const glm::vec2 ab = b - a;
    const float length2 = glm::dot(ab, ab);
    const float t = length2 > 0.0f ? std::clamp(glm::dot(p - a, ab) / length2, 0.0f, 1.0f) : 0.0f;
    return glm::length(p - (a + ab * t));
}

// Whether `p` is inside the convex quad a b c d (either winding).
bool inside_quad(glm::vec2 p, const std::array<glm::vec2, 4>& quad) {
    int positive = 0, negative = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        const glm::vec2 e = quad[(i + 1) % 4] - quad[i];
        const glm::vec2 w = p - quad[i];
        const float cross = e.x * w.y - e.y * w.x;
        (cross >= 0.0f ? positive : negative)++;
    }
    return positive == 4 || negative == 4;
}

// The point of the line (origin, axis) nearest to the ray; nullopt when they are (nearly) parallel.
std::optional<glm::vec3> closest_on_line(glm::vec3 origin, glm::vec3 axis, const Ray& ray) {
    const glm::vec3 w0 = origin - ray.origin;
    const float b = glm::dot(axis, ray.direction);
    const float denominator = 1.0f - b * b;
    if (denominator < 1e-4f) {
        return std::nullopt;
    }
    const float d = glm::dot(axis, w0);
    const float e = glm::dot(ray.direction, w0);
    const float s = (b * e - d) / denominator;
    return origin + axis * s;
}

std::optional<glm::vec3> hit_plane(glm::vec3 point, glm::vec3 normal, const Ray& ray) {
    const float facing = glm::dot(normal, ray.direction);
    if (std::fabs(facing) < 1e-4f) {
        return std::nullopt;
    }
    const float t = glm::dot(point - ray.origin, normal) / facing;
    if (t < 0.0f) {
        return std::nullopt;
    }
    return ray.origin + ray.direction * t;
}

// Two unit vectors across `axis` (a ring's plane).
void across(glm::vec3 axis, glm::vec3& u, glm::vec3& v) {
    const glm::vec3 other = std::fabs(axis.y) < 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    u = glm::normalize(glm::cross(axis, other));
    v = glm::cross(axis, u);
}

bool is_axis(GizmoHandle handle) {
    return handle == GizmoHandle::X || handle == GizmoHandle::Y || handle == GizmoHandle::Z;
}

bool is_plane(GizmoHandle handle) {
    return handle == GizmoHandle::XY || handle == GizmoHandle::XZ || handle == GizmoHandle::YZ;
}

// The two axes of a plane handle.
void plane_axes(GizmoHandle handle, glm::vec3& a, glm::vec3& b) {
    switch (handle) {
        case GizmoHandle::XY:
            a = {1.0f, 0.0f, 0.0f};
            b = {0.0f, 1.0f, 0.0f};
            break;
        case GizmoHandle::YZ:
            a = {0.0f, 1.0f, 0.0f};
            b = {0.0f, 0.0f, 1.0f};
            break;
        default:
            a = {1.0f, 0.0f, 0.0f};
            b = {0.0f, 0.0f, 1.0f};
            break;
    }
}

constexpr std::array<GizmoHandle, 3> kAxes = {GizmoHandle::X, GizmoHandle::Y, GizmoHandle::Z};
constexpr std::array<GizmoHandle, 3> kPlanes = {GizmoHandle::XY, GizmoHandle::XZ, GizmoHandle::YZ};

}  // namespace

glm::vec3 gizmo_axis(GizmoHandle handle) {
    switch (handle) {
        case GizmoHandle::X:
        case GizmoHandle::YZ:
            return {1.0f, 0.0f, 0.0f};
        case GizmoHandle::Y:
        case GizmoHandle::XZ:
            return {0.0f, 1.0f, 0.0f};
        case GizmoHandle::Z:
        case GizmoHandle::XY:
            return {0.0f, 0.0f, 1.0f};
        default:
            return {0.0f, 0.0f, 0.0f};
    }
}

float snap_value(float value, float step) {
    return step > 0.0f ? std::round(value / step) * step : value;
}

glm::vec3 snap_position(glm::vec3 value, float step) {
    return {snap_value(value.x, step), snap_value(value.y, step), snap_value(value.z, step)};
}

float Gizmo::world_size(const Camera3D& camera, glm::vec3 origin) const {
    // A metre across the view at the origin, in pixels.
    const glm::mat4 view = camera.view();
    const glm::vec3 right(view[0][0], view[1][0], view[2][0]);
    const std::optional<glm::vec2> a = camera.world_to_screen(origin);
    const std::optional<glm::vec2> b = camera.world_to_screen(origin + right);
    if (!a || !b) {
        return 1.0f;
    }
    const float pixels_per_metre = glm::length(*b - *a);
    return pixels_per_metre > 1e-3f ? size_pixels / pixels_per_metre : 1.0f;
}

std::vector<GizmoLine> Gizmo::shape(const Camera3D& camera, glm::vec3 origin, GizmoMode mode) const {
    std::vector<GizmoLine> lines;
    const float length = world_size(camera, origin);
    const std::optional<glm::vec2> centre = camera.world_to_screen(origin);
    if (!centre) {
        return lines;
    }
    const auto segment = [&](glm::vec3 a, glm::vec3 b, GizmoHandle handle) {
        const std::optional<glm::vec2> sa = camera.world_to_screen(a);
        const std::optional<glm::vec2> sb = camera.world_to_screen(b);
        if (sa && sb) {
            lines.push_back({*sa, *sb, handle});
        }
    };
    const auto square = [&](glm::vec2 c, float half, GizmoHandle handle) {
        lines.push_back({c + glm::vec2(-half, -half), c + glm::vec2(half, -half), handle});
        lines.push_back({c + glm::vec2(half, -half), c + glm::vec2(half, half), handle});
        lines.push_back({c + glm::vec2(half, half), c + glm::vec2(-half, half), handle});
        lines.push_back({c + glm::vec2(-half, half), c + glm::vec2(-half, -half), handle});
    };
    switch (mode) {
        case GizmoMode::Translate:
            for (const GizmoHandle handle : kAxes) {
                const glm::vec3 end = origin + gizmo_axis(handle) * length;
                segment(origin, end, handle);
                // The arrow's head: two strokes back from the tip, in the screen.
                const std::optional<glm::vec2> tip = camera.world_to_screen(end);
                if (tip && glm::length(*tip - *centre) > 1.0f) {
                    const glm::vec2 back = glm::normalize(*centre - *tip) * 12.0f;
                    const glm::vec2 side(-back.y * 0.45f, back.x * 0.45f);
                    lines.push_back({*tip, *tip + back + side, handle});
                    lines.push_back({*tip, *tip + back - side, handle});
                }
            }
            for (const GizmoHandle handle : kPlanes) {
                glm::vec3 a, b;
                plane_axes(handle, a, b);
                const glm::vec3 p0 = origin + (a + b) * (kPlaneNear * length);
                const glm::vec3 p1 = origin + (a * kPlaneFar + b * kPlaneNear) * length;
                const glm::vec3 p2 = origin + (a + b) * (kPlaneFar * length);
                const glm::vec3 p3 = origin + (a * kPlaneNear + b * kPlaneFar) * length;
                segment(p0, p1, handle);
                segment(p1, p2, handle);
                segment(p2, p3, handle);
                segment(p3, p0, handle);
            }
            square(*centre, kMiddle, GizmoHandle::All);
            break;
        case GizmoMode::Rotate:
            for (const GizmoHandle handle : kAxes) {
                glm::vec3 u, v;
                across(gizmo_axis(handle), u, v);
                for (int i = 0; i < kRingSegments; ++i) {
                    const float a0 = 2.0f * kPi * static_cast<float>(i) / kRingSegments;
                    const float a1 = 2.0f * kPi * static_cast<float>(i + 1) / kRingSegments;
                    segment(origin + (u * std::cos(a0) + v * std::sin(a0)) * length,
                            origin + (u * std::cos(a1) + v * std::sin(a1)) * length, handle);
                }
            }
            break;
        case GizmoMode::Scale:
            for (const GizmoHandle handle : kAxes) {
                const glm::vec3 end = origin + gizmo_axis(handle) * length;
                segment(origin, end, handle);
                if (const std::optional<glm::vec2> tip = camera.world_to_screen(end)) {
                    square(*tip, 5.0f, handle);
                }
            }
            square(*centre, kMiddle, GizmoHandle::All);
            break;
    }
    return lines;
}

GizmoHandle Gizmo::hit(const Camera3D& camera, glm::vec3 origin, GizmoMode mode, glm::vec2 pointer) const {
    const float length = world_size(camera, origin);
    const std::optional<glm::vec2> centre = camera.world_to_screen(origin);
    if (!centre) {
        return GizmoHandle::None;
    }
    // The middle, then the planes: they sit over the axes.
    if (mode != GizmoMode::Rotate && std::fabs(pointer.x - centre->x) <= kMiddle + 2.0f &&
        std::fabs(pointer.y - centre->y) <= kMiddle + 2.0f) {
        return GizmoHandle::All;
    }
    if (mode == GizmoMode::Translate) {
        for (const GizmoHandle handle : kPlanes) {
            glm::vec3 a, b;
            plane_axes(handle, a, b);
            const std::array<glm::vec3, 4> corners = {origin + (a + b) * (kPlaneNear * length),
                                                      origin + (a * kPlaneFar + b * kPlaneNear) * length,
                                                      origin + (a + b) * (kPlaneFar * length),
                                                      origin + (a * kPlaneNear + b * kPlaneFar) * length};
            std::array<glm::vec2, 4> quad;
            bool seen = true;
            for (std::size_t i = 0; i < 4; ++i) {
                const std::optional<glm::vec2> s = camera.world_to_screen(corners[i]);
                seen = seen && s.has_value();
                quad[i] = s.value_or(glm::vec2(0.0f));
            }
            if (seen && inside_quad(pointer, quad)) {
                return handle;
            }
        }
    }
    GizmoHandle best = GizmoHandle::None;
    float best_distance = grab_pixels;
    for (const GizmoLine& line : shape(camera, origin, mode)) {
        if (line.handle == GizmoHandle::All || is_plane(line.handle)) {
            continue;
        }
        const float d = distance_to_segment(pointer, line.a, line.b);
        if (d < best_distance) {
            best_distance = d;
            best = line.handle;
        }
    }
    return best;
}

std::optional<glm::vec3> Gizmo::on_handle(const Ray& ray) const {
    switch (mode_) {
        case GizmoMode::Translate:
            if (is_axis(dragging_)) {
                return closest_on_line(origin_, gizmo_axis(dragging_), ray);
            }
            if (is_plane(dragging_)) {
                return hit_plane(origin_, gizmo_axis(dragging_), ray);
            }
            return std::nullopt;  // All: in the view's plane, set in begin()
        case GizmoMode::Rotate:
            return hit_plane(origin_, gizmo_axis(dragging_), ray);
        case GizmoMode::Scale:
            if (is_axis(dragging_)) {
                return closest_on_line(origin_, gizmo_axis(dragging_), ray);
            }
            return std::nullopt;
    }
    return std::nullopt;
}

bool Gizmo::begin(const Camera3D& camera, glm::vec3 origin, GizmoMode mode, GizmoHandle handle, glm::vec2 pointer) {
    dragging_ = GizmoHandle::None;
    if (handle == GizmoHandle::None) {
        return false;
    }
    const std::optional<glm::vec2> centre = camera.world_to_screen(origin);
    if (!centre) {
        return false;
    }
    mode_ = mode;
    origin_ = origin;
    start_pointer_ = pointer;
    screen_origin_ = *centre;
    dragging_ = handle;
    if (handle == GizmoHandle::All) {
        if (mode == GizmoMode::Translate) {
            const std::optional<glm::vec3> hit = hit_plane(origin, -camera.forward(), camera.screen_ray(pointer));
            if (!hit) {
                dragging_ = GizmoHandle::None;
                return false;
            }
            start_ = *hit;
        }
        return true;  // Scale: by the distance to the middle, in the screen
    }
    const std::optional<glm::vec3> grabbed = on_handle(camera.screen_ray(pointer));
    if (!grabbed) {
        dragging_ = GizmoHandle::None;
        return false;
    }
    start_ = *grabbed;
    if (mode == GizmoMode::Scale && std::fabs(glm::dot(start_ - origin_, gizmo_axis(handle))) < 1e-4f) {
        dragging_ = GizmoHandle::None;  // grabbed at the origin: no factor can be measured
        return false;
    }
    return true;
}

GizmoDelta Gizmo::drag(const Camera3D& camera, glm::vec2 pointer, const GizmoSnap& snap) const {
    GizmoDelta delta;
    if (dragging_ == GizmoHandle::None) {
        return delta;
    }
    const Ray ray = camera.screen_ray(pointer);
    switch (mode_) {
        case GizmoMode::Translate: {
            if (dragging_ == GizmoHandle::All) {
                if (const std::optional<glm::vec3> p = hit_plane(origin_, -camera.forward(), ray)) {
                    delta.translation = snap_position(*p - start_, snap.translation);
                }
            } else if (const std::optional<glm::vec3> p = on_handle(ray)) {
                if (is_axis(dragging_)) {
                    const glm::vec3 axis = gizmo_axis(dragging_);
                    delta.translation = axis * snap_value(glm::dot(*p - start_, axis), snap.translation);
                } else {
                    glm::vec3 moved = snap_position(*p - start_, snap.translation);
                    const glm::vec3 normal = gizmo_axis(dragging_);
                    moved -= normal * glm::dot(moved, normal);  // stays in the plane
                    delta.translation = moved;
                }
            }
            break;
        }
        case GizmoMode::Rotate: {
            const glm::vec3 axis = gizmo_axis(dragging_);
            delta.axis = axis;
            if (const std::optional<glm::vec3> p = on_handle(ray)) {
                const glm::vec3 v0 = start_ - origin_;
                const glm::vec3 v1 = *p - origin_;
                if (glm::length(v0) > 1e-5f && glm::length(v1) > 1e-5f) {
                    const float angle = std::atan2(glm::dot(glm::cross(v0, v1), axis), glm::dot(v0, v1));
                    delta.angle = snap_value(angle, snap.angle);
                }
            }
            break;
        }
        case GizmoMode::Scale: {
            float factor = 1.0f;
            if (dragging_ == GizmoHandle::All) {
                const float d0 = glm::length(start_pointer_ - screen_origin_);
                const float d1 = glm::length(pointer - screen_origin_);
                factor = d0 > 1.0f ? d1 / d0 : 1.0f;
            } else if (const std::optional<glm::vec3> p = on_handle(ray)) {
                const glm::vec3 axis = gizmo_axis(dragging_);
                const float t0 = glm::dot(start_ - origin_, axis);
                factor = glm::dot(*p - origin_, axis) / t0;
            }
            factor = std::max(snap_value(factor, snap.scale), snap.scale > 0.0f ? snap.scale : 0.01f);
            if (dragging_ == GizmoHandle::All) {
                delta.scale = glm::vec3(factor);
            } else {
                delta.scale = glm::vec3(1.0f) + gizmo_axis(dragging_) * (factor - 1.0f);
            }
            break;
        }
    }
    return delta;
}

}  // namespace moteur
