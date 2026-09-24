#include "moteur/shadow.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace moteur {

namespace {

// A point of the world seen at clip coordinates (x, y, depth).
glm::vec3 unproject(const glm::mat4& inverse, glm::vec3 clip) {
    const glm::vec4 world = inverse * glm::vec4(clip, 1.0f);
    return glm::vec3(world) / world.w;
}

// A camera-independent "up" for the sun's view: any vector not parallel to its direction.
glm::vec3 up_for(glm::vec3 direction) {
    return std::abs(direction.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f);
}

}  // namespace

ShadowFrame fit_sun_shadow(const glm::mat4& camera_view_projection, glm::vec3 to_sun, const ShadowSettings& settings) {
    const glm::mat4 inverse = glm::inverse(camera_view_projection);
    to_sun = glm::normalize(to_sun);

    // The visible part of the field: each corner ray, cut between the ceiling and the ground.
    std::vector<glm::vec3> points;
    for (const glm::vec2 corner : {glm::vec2(-1, -1), glm::vec2(1, -1), glm::vec2(1, 1), glm::vec2(-1, 1)}) {
        const glm::vec3 near_point = unproject(inverse, {corner, 0.0f});
        const glm::vec3 far_point = unproject(inverse, {corner, 1.0f});
        const glm::vec3 direction = glm::normalize(far_point - near_point);
        const auto at_height = [&](float height) {
            // Where the ray crosses y = height, limited to max_distance from the near plane.
            float distance = settings.max_distance;
            if (std::abs(direction.y) > 1e-6f) {
                const float t = (height - near_point.y) / direction.y;
                if (t > 0.0f) {
                    distance = std::min(distance, t);
                }
            }
            return near_point + direction * distance;
        };
        points.push_back(at_height(0.0f));
        points.push_back(at_height(settings.max_height));
    }

    // Their bounding sphere (centre of the box, farthest point): stable and cheap.
    glm::vec3 low = points[0], high = points[0];
    for (const glm::vec3& p : points) {
        low = glm::min(low, p);
        high = glm::max(high, p);
    }
    glm::vec3 center = (low + high) * 0.5f;
    float radius = 0.0f;
    for (const glm::vec3& p : points) {
        radius = std::max(radius, glm::length(p - center));
    }
    // Rounded up to a whole metre: the map keeps its size while the view moves, only zoom changes it.
    radius = std::ceil(radius);

    // Snap the centre to the texel grid of the sun's view, so the map slides by whole texels.
    const float texel = 2.0f * radius / static_cast<float>(settings.resolution);
    const glm::mat4 orientation = glm::lookAtRH(glm::vec3(0.0f), -to_sun, up_for(to_sun));
    glm::vec3 local = glm::vec3(orientation * glm::vec4(center, 1.0f));
    local.x = std::floor(local.x / texel) * texel;
    local.y = std::floor(local.y / texel) * texel;
    center = glm::vec3(glm::inverse(orientation) * glm::vec4(local, 1.0f));

    const float back = radius + settings.depth_margin;  // eye this far towards the sun
    const glm::mat4 view = glm::lookAtRH(center + to_sun * back, center, up_for(to_sun));
    const glm::mat4 projection = glm::orthoRH_ZO(-radius, radius, -radius, radius, 0.0f, back + radius);

    ShadowFrame frame;
    frame.view_projection = projection * view;
    frame.center = center;
    frame.radius = radius;
    frame.texel_size = texel;
    return frame;
}

PointShadowFaces point_shadow_faces(glm::vec3 position, float range, int tile_size) {
    // Directions and "up" vectors of the six faces (any up not parallel to the direction works:
    // the shader reads each face through the same matrix).
    static const glm::vec3 kDirections[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    static const glm::vec3 kUps[6] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};
    const float size = static_cast<float>(std::max(tile_size, 8));
    // Half the face spans 1 (90 degrees) plus the margin, in texels of half a tile.
    const float half_extent = 1.0f + 2.0f * static_cast<float>(kPointShadowMarginTexels) / size;
    const float field_of_view = 2.0f * std::atan(half_extent);
    const float near_plane = std::max(0.02f, range * 0.005f);
    const glm::mat4 projection = glm::perspectiveRH_ZO(field_of_view, 1.0f, near_plane, std::max(range, near_plane * 2.0f));
    PointShadowFaces faces;
    for (int f = 0; f < 6; ++f) {
        faces.view_projection[f] = projection * glm::lookAtRH(position, position + kDirections[f], kUps[f]);
    }
    faces.texel_size_per_metre = 2.0f * half_extent / size;
    return faces;
}

int point_shadow_face(glm::vec3 d) {
    const glm::vec3 a = glm::abs(d);
    if (a.x >= a.y && a.x >= a.z) {
        return d.x >= 0.0f ? 0 : 1;
    }
    if (a.y >= a.z) {
        return d.y >= 0.0f ? 2 : 3;
    }
    return d.z >= 0.0f ? 4 : 5;
}

std::vector<int> select_point_shadows(const std::vector<PointShadowCandidate>& candidates, const Frustum& view,
                                      glm::vec3 focus, int budget) {
    std::vector<int> selected;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const PointShadowCandidate& light = candidates[i];
        Aabb sphere_box;
        sphere_box.add(light.position - glm::vec3(light.range));
        sphere_box.add(light.position + glm::vec3(light.range));
        if (light.range > 0.0f && view.intersects(sphere_box)) {
            selected.push_back(static_cast<int>(i));
        }
    }
    const auto distance2 = [&](int i) {
        const glm::vec3 d = candidates[static_cast<std::size_t>(i)].position - focus;
        return glm::dot(d, d);
    };
    std::stable_sort(selected.begin(), selected.end(), [&](int a, int b) { return distance2(a) < distance2(b); });
    if (static_cast<int>(selected.size()) > std::max(budget, 0)) {
        selected.resize(static_cast<std::size_t>(std::max(budget, 0)));
    }
    return selected;
}

void PointShadowSlots::reset(int count) {
    slots_.assign(static_cast<std::size_t>(std::max(count, 0)), Slot{});
}

std::vector<PointShadowSlots::Assignment> PointShadowSlots::assign(const std::vector<std::uint64_t>& signatures) {
    const std::size_t n = std::min(signatures.size(), slots_.size());
    std::vector<Assignment> result(n);
    std::vector<bool> taken(slots_.size(), false);
    std::vector<bool> placed(n, false);
    // Shadows already drawn keep their tiles.
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t s = 0; s < slots_.size(); ++s) {
            if (!taken[s] && slots_[s].valid && slots_[s].signature == signatures[i]) {
                taken[s] = true;
                placed[i] = true;
                result[i] = {static_cast<int>(s), false};
                break;
            }
        }
    }
    // The others: an empty slot first, so kept shadows of lights out of the selection survive
    // longer, then any slot not used this frame.
    for (std::size_t i = 0; i < n; ++i) {
        if (placed[i]) {
            continue;
        }
        std::size_t chosen = slots_.size();
        for (std::size_t s = 0; s < slots_.size() && chosen == slots_.size(); ++s) {
            if (!taken[s] && !slots_[s].valid) {
                chosen = s;
            }
        }
        for (std::size_t s = 0; s < slots_.size() && chosen == slots_.size(); ++s) {
            if (!taken[s]) {
                chosen = s;
            }
        }
        taken[chosen] = true;
        slots_[chosen] = {true, signatures[i]};
        result[i] = {static_cast<int>(chosen), true};
    }
    return result;
}

}  // namespace moteur
