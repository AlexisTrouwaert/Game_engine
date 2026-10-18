#include "moteur/picking.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "moteur/nav_grid.hpp"
#include "moteur/navigation.hpp"
#include "moteur/profiler.hpp"
#include "moteur/world.hpp"

namespace moteur {

std::optional<float> ray_capsule(const Ray& ray, glm::vec3 feet, float radius, float height) {
    // Inigo Quilez's ray-capsule intersection: the cylinder between the two centres, then the caps.
    const glm::vec3 pa = feet + glm::vec3(0.0f, radius, 0.0f);
    const glm::vec3 pb = feet + glm::vec3(0.0f, std::max(height - radius, radius), 0.0f);
    const glm::vec3 ba = pb - pa;
    const glm::vec3 oa = ray.origin - pa;
    const float baba = glm::dot(ba, ba);
    const float bard = glm::dot(ba, ray.direction);
    const float baoa = glm::dot(ba, oa);
    const float rdoa = glm::dot(ray.direction, oa);
    const float oaoa = glm::dot(oa, oa);
    const float a = baba - bard * bard;
    const float b = baba * rdoa - baoa * bard;
    const float c = baba * oaoa - baoa * baoa - radius * radius * baba;
    float h = b * b - a * c;
    if (a > 1e-8f && h >= 0.0f) {
        const float t = (-b - std::sqrt(h)) / a;
        const float y = baoa + t * bard;
        if (y > 0.0f && y < baba && t >= 0.0f) {
            return t;
        }
    }
    // The caps (the sphere at the end the ray comes from first, then the other).
    std::optional<float> best;
    for (const glm::vec3 centre : {pa, pb}) {
        const glm::vec3 oc = ray.origin - centre;
        const float bb = glm::dot(ray.direction, oc);
        const float cc = glm::dot(oc, oc) - radius * radius;
        h = bb * bb - cc;
        if (h >= 0.0f) {
            const float t = -bb - std::sqrt(h);
            if (t >= 0.0f && (!best || t < *best)) {
                best = t;
            }
        }
    }
    return best;
}

std::optional<float> ray_box(const Ray& ray, glm::vec3 low, glm::vec3 high) {
    float near = 0.0f;
    float far = std::numeric_limits<float>::infinity();
    for (int axis = 0; axis < 3; ++axis) {
        const float o = ray.origin[axis];
        const float d = ray.direction[axis];
        if (std::fabs(d) < 1e-12f) {
            if (o < low[axis] || o > high[axis]) {
                return std::nullopt;
            }
            continue;
        }
        float t0 = (low[axis] - o) / d;
        float t1 = (high[axis] - o) / d;
        if (t0 > t1) {
            std::swap(t0, t1);
        }
        near = std::max(near, t0);
        far = std::min(far, t1);
        if (near > far) {
            return std::nullopt;
        }
    }
    return near;
}

std::optional<PickHit> pick(const entt::registry& registry, const Ray& ray, const CollisionScene* level,
                            const NavMesh* navmesh, float max_distance) {
    MOTEUR_PROFILE("pick");
    // What the level hides beyond: the first solid triangle.
    std::optional<RayHit> wall;
    if (level != nullptr) {
        wall = level->raycast(ray.origin, ray.direction, max_distance, kSurfaceSolid);
    }
    const float limit = wall ? wall->distance + 1e-3f : max_distance;

    std::optional<PickHit> best;
    for (const auto& [entity, transform, selectable] : registry.view<Transform, Selectable>().each()) {
        if (!selectable.enabled) {
            continue;
        }
        std::optional<float> t;
        if (selectable.box) {
            const glm::vec3 centre = transform.position + selectable.centre * transform.scale;
            const glm::vec3 half = selectable.half_size * glm::abs(transform.scale);
            t = ray_box(ray, centre - half, centre + half);
        } else {
            t = ray_capsule(ray, transform.position, selectable.radius, selectable.height);
        }
        if (!t || *t > limit) {
            continue;
        }
        const bool better = !best || selectable.kind > best->kind ||
                            (selectable.kind == best->kind &&
                             (*t < best->distance ||
                              (*t == best->distance && entt::to_integral(entity) < entt::to_integral(best->entity))));
        if (better) {
            best = PickHit{entity, selectable.kind, ray.origin + ray.direction * *t, *t};
        }
    }
    if (best) {
        return best;
    }
    // The ground: the first solid surface met, put on the navigation mesh (a wall gives the walkable
    // point nearest where it was hit).
    if (!wall) {
        return std::nullopt;
    }
    PickHit ground;
    ground.point = wall->point;
    ground.distance = wall->distance;
    if (navmesh != nullptr && !navmesh->empty()) {
        if (const std::optional<glm::vec3> on_mesh = navmesh->nearest_point(wall->point, {1.0f, 2.0f, 1.0f})) {
            ground.point = *on_mesh;
        } else if (level->triangles()[wall->triangle].kind != SurfaceKind::Walkable) {
            return std::nullopt;  // a wall with no ground near it
        }
    } else if (level->triangles()[wall->triangle].kind != SurfaceKind::Walkable) {
        return std::nullopt;
    }
    return ground;
}

bool line_of_sight(const CollisionScene& level, glm::vec3 a, glm::vec3 b, std::uint32_t mask) {
    return level.segment_clear(a, b, mask) && level.segment_clear(b, a, mask);
}

NavGrid floor_sight_grid(const NavMesh& navmesh, const CollisionScene& level, glm::vec2 origin, int width, int height,
                         float level_height, float eye_height) {
    MOTEUR_PROFILE("floor sight grid");
    NavGrid grid(width, height);
    std::vector<std::uint32_t> found;
    for (int j = 0; j < height; ++j) {
        for (int i = 0; i < width; ++i) {
            const float x = origin.x + static_cast<float>(i);
            const float z = origin.y + static_cast<float>(j);
            const glm::vec3 centre(x + 0.5f, level_height, z + 0.5f);
            glm::vec3 nearest;
            const bool walkable = navmesh.nearest_polygon(centre, {0.45f, 0.5f, 0.45f}, &nearest) != 0 &&
                                  std::fabs(nearest.x - centre.x) <= 0.5f && std::fabs(nearest.z - centre.z) <= 0.5f &&
                                  std::fabs(nearest.y - level_height) <= 0.5f;
            Aabb cell;
            cell.add({x + 0.05f, level_height + 0.3f, z + 0.05f});
            cell.add({x + 0.95f, level_height + eye_height, z + 0.95f});
            level.box_triangles(cell, kSurfaceWall, found);
            bool opaque = !found.empty();
            if (!opaque) {
                // Inside a thick wall no face crosses the cell: a ray from its middle then leaves the
                // solid first (the face it meets turns its back to it).
                const glm::vec3 middle(x + 0.5f, level_height + eye_height * 0.5f, z + 0.5f);
                const std::optional<RayHit> out = level.raycast(middle, {1.0f, 0.0f, 0.0f}, 1000.0f, kSurfaceWall);
                opaque = out.has_value() && out->normal.x > 0.0f;
            }
            grid.set({i, j}, walkable, opaque);
        }
    }
    return grid;
}

}  // namespace moteur
