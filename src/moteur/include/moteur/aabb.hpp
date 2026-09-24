#pragma once

#include <glm/glm.hpp>

namespace moteur {

// An axis-aligned box. Empty (min > max) until a point is added.
struct Aabb {
    glm::vec3 min{1.0f};
    glm::vec3 max{-1.0f};

    bool empty() const { return max.x < min.x || max.y < min.y || max.z < min.z; }
    void add(glm::vec3 point) {
        if (empty()) {
            min = max = point;
            return;
        }
        min = glm::min(min, point);
        max = glm::max(max, point);
    }
    glm::vec3 center() const { return (min + max) * 0.5f; }
    glm::vec3 size() const { return max - min; }
    glm::vec3 corner(int i) const {  // i in [0, 8)
        return {(i & 1) ? max.x : min.x, (i & 2) ? max.y : min.y, (i & 4) ? max.z : min.z};
    }
};

// True when the box and the sphere share at least a point.
inline bool intersects_sphere(const Aabb& box, glm::vec3 center, float radius) {
    if (box.empty()) {
        return false;
    }
    const glm::vec3 nearest = glm::clamp(center, box.min, box.max);
    const glm::vec3 d = nearest - center;
    return glm::dot(d, d) <= radius * radius;
}

// The box around `box` once moved by `transform` (its 8 corners, transformed and wrapped again).
// Rotations make it larger than the object, never smaller: safe for culling.
inline Aabb transform_box(const Aabb& box, const glm::mat4& transform) {
    Aabb result;
    if (box.empty()) {
        return result;
    }
    for (int i = 0; i < 8; ++i) {
        result.add(glm::vec3(transform * glm::vec4(box.corner(i), 1.0f)));
    }
    return result;
}

}  // namespace moteur
