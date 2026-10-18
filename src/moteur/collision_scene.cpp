#include "moteur/collision_scene.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include "moteur/profiler.hpp"

namespace moteur {

namespace {

constexpr std::uint32_t kLeafSize = 4;
constexpr float kInfinity = std::numeric_limits<float>::infinity();

char lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool starts_with_nocase(std::string_view text, std::string_view prefix) {
    if (text.size() < prefix.size()) {
        return false;
    }
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (lower(text[i]) != lower(prefix[i])) {
            return false;
        }
    }
    return true;
}

Aabb triangle_box(const CollisionTriangle& t) {
    Aabb box;
    box.add(t.a);
    box.add(t.b);
    box.add(t.c);
    return box;
}

bool boxes_overlap(const Aabb& a, const Aabb& b) {
    return a.min.x <= b.max.x && a.max.x >= b.min.x && a.min.y <= b.max.y && a.max.y >= b.min.y && a.min.z <= b.max.z &&
           a.max.z >= b.min.z;
}

// The ray (origin, 1 / direction) meets the box between 0 and `max_distance`.
bool ray_meets_box(const Aabb& box, glm::vec3 origin, glm::vec3 inverse, float max_distance) {
    float near = 0.0f;
    float far = max_distance;
    for (int axis = 0; axis < 3; ++axis) {
        float t0 = (box.min[axis] - origin[axis]) * inverse[axis];
        float t1 = (box.max[axis] - origin[axis]) * inverse[axis];
        if (std::isnan(t0) || std::isnan(t1)) {  // 0 * infinity: the ray lies in the slab's plane
            if (origin[axis] < box.min[axis] || origin[axis] > box.max[axis]) {
                return false;
            }
            continue;
        }
        if (t0 > t1) {
            std::swap(t0, t1);
        }
        near = std::max(near, t0);
        far = std::min(far, t1);
        if (near > far) {
            return false;
        }
    }
    return true;
}

// Möller and Trumbore: the distance along the ray, both faces; nullopt when it misses.
std::optional<float> ray_triangle(glm::vec3 origin, glm::vec3 direction, const CollisionTriangle& t) {
    const glm::vec3 e1 = t.b - t.a;
    const glm::vec3 e2 = t.c - t.a;
    const glm::vec3 p = glm::cross(direction, e2);
    const float det = glm::dot(e1, p);
    if (std::fabs(det) < 1e-12f) {
        return std::nullopt;  // parallel
    }
    const float inverse = 1.0f / det;
    const glm::vec3 s = origin - t.a;
    const float u = glm::dot(s, p) * inverse;
    if (u < 0.0f || u > 1.0f) {
        return std::nullopt;
    }
    const glm::vec3 q = glm::cross(s, e1);
    const float v = glm::dot(direction, q) * inverse;
    if (v < 0.0f || u + v > 1.0f) {
        return std::nullopt;
    }
    const float distance = glm::dot(e2, q) * inverse;
    if (distance < 0.0f) {
        return std::nullopt;
    }
    return distance;
}

// The points of [p1, q1] and [p2, q2] nearest each other (Ericson 5.1.9); returns the squared distance.
float closest_segments(glm::vec3 p1, glm::vec3 q1, glm::vec3 p2, glm::vec3 q2, glm::vec3& c1, glm::vec3& c2) {
    const glm::vec3 d1 = q1 - p1;
    const glm::vec3 d2 = q2 - p2;
    const glm::vec3 r = p1 - p2;
    const float a = glm::dot(d1, d1);
    const float e = glm::dot(d2, d2);
    const float f = glm::dot(d2, r);
    float s = 0.0f;
    float t = 0.0f;
    constexpr float kEpsilon = 1e-12f;
    if (a <= kEpsilon && e <= kEpsilon) {
        s = t = 0.0f;
    } else if (a <= kEpsilon) {
        t = std::clamp(f / e, 0.0f, 1.0f);
    } else {
        const float c = glm::dot(d1, r);
        if (e <= kEpsilon) {
            s = std::clamp(-c / a, 0.0f, 1.0f);
        } else {
            const float b = glm::dot(d1, d2);
            const float denominator = a * e - b * b;
            s = denominator != 0.0f ? std::clamp((b * f - c * e) / denominator, 0.0f, 1.0f) : 0.0f;
            t = (b * s + f) / e;
            if (t < 0.0f) {
                t = 0.0f;
                s = std::clamp(-c / a, 0.0f, 1.0f);
            } else if (t > 1.0f) {
                t = 1.0f;
                s = std::clamp((b - c) / a, 0.0f, 1.0f);
            }
        }
    }
    c1 = p1 + d1 * s;
    c2 = p2 + d2 * t;
    const glm::vec3 d = c1 - c2;
    return glm::dot(d, d);
}

// A contact from the core's point `core` and the triangle's nearest point, if within `radius`.
std::optional<Contact> make_contact(std::uint32_t index, const CollisionTriangle& t, glm::vec3 core, glm::vec3 point,
                                    glm::vec3 reference, float radius) {
    const glm::vec3 d = core - point;
    const float distance2 = glm::dot(d, d);
    if (distance2 >= radius * radius) {
        return std::nullopt;
    }
    Contact contact;
    contact.triangle = index;
    contact.point = point;
    const float distance = std::sqrt(distance2);
    if (distance > 1e-6f) {
        contact.normal = d / distance;
    } else {
        // The core touches the triangle: out by its face, on the side of the reference point.
        contact.normal = glm::dot(reference - t.a, t.normal) >= 0.0f ? t.normal : -t.normal;
    }
    contact.depth = radius - distance;
    return contact;
}

void sort_contacts(std::vector<Contact>& contacts) {
    std::sort(contacts.begin(), contacts.end(), [](const Contact& a, const Contact& b) {
        if (a.depth != b.depth) {
            return a.depth > b.depth;
        }
        return a.triangle < b.triangle;
    });
}

}  // namespace

std::optional<SurfaceKind> surface_kind_of_node(std::string_view node) {
    if (!is_collision_node(node)) {
        return std::nullopt;
    }
    std::string_view rest = node.substr(4);
    while (!rest.empty() && (rest.front() == '_' || rest.front() == '-' || rest.front() == ' ')) {
        rest.remove_prefix(1);
    }
    if (starts_with_nocase(rest, "walk") || starts_with_nocase(rest, "floor") || starts_with_nocase(rest, "ground")) {
        return SurfaceKind::Walkable;
    }
    if (starts_with_nocase(rest, "wall")) {
        return SurfaceKind::Wall;
    }
    if (starts_with_nocase(rest, "obstacle") || starts_with_nocase(rest, "block")) {
        return SurfaceKind::Obstacle;
    }
    if (starts_with_nocase(rest, "void") || starts_with_nocase(rest, "pit")) {
        return SurfaceKind::Void;
    }
    return std::nullopt;
}

bool is_collision_node(std::string_view node) {
    return starts_with_nocase(node, "COL_") || starts_with_nocase(node, "UCX_");
}

// --- CollisionMeshData -------------------------------------------------------------------------

void CollisionMeshData::add_triangle(glm::vec3 a, glm::vec3 b, glm::vec3 c, SurfaceKind kind) {
    const auto first = static_cast<std::uint32_t>(vertices.size());
    vertices.insert(vertices.end(), {a, b, c});
    indices.insert(indices.end(), {first, first + 1, first + 2});
    kinds.push_back(kind);
}

void CollisionMeshData::add_quad(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, SurfaceKind kind) {
    const auto first = static_cast<std::uint32_t>(vertices.size());
    vertices.insert(vertices.end(), {a, b, c, d});
    indices.insert(indices.end(), {first, first + 1, first + 2, first, first + 2, first + 3});
    kinds.insert(kinds.end(), {kind, kind});
}

void CollisionMeshData::add_box(const Aabb& box, SurfaceKind top, SurfaceKind sides) {
    const glm::vec3 l = box.min;
    const glm::vec3 h = box.max;
    add_quad({l.x, h.y, l.z}, {l.x, h.y, h.z}, {h.x, h.y, h.z}, {h.x, h.y, l.z}, top);    // +y
    add_quad({l.x, l.y, l.z}, {h.x, l.y, l.z}, {h.x, l.y, h.z}, {l.x, l.y, h.z}, sides);  // -y
    add_quad({h.x, l.y, l.z}, {h.x, h.y, l.z}, {h.x, h.y, h.z}, {h.x, l.y, h.z}, sides);  // +x
    add_quad({l.x, l.y, l.z}, {l.x, l.y, h.z}, {l.x, h.y, h.z}, {l.x, h.y, l.z}, sides);  // -x
    add_quad({l.x, l.y, h.z}, {h.x, l.y, h.z}, {h.x, h.y, h.z}, {l.x, h.y, h.z}, sides);  // +z
    add_quad({l.x, l.y, l.z}, {l.x, h.y, l.z}, {h.x, h.y, l.z}, {h.x, l.y, l.z}, sides);  // -z
}

namespace {

// A local point (x along the flight, y up, z across) turned by quarter turns about +y and moved.
glm::vec3 turned(glm::vec3 start, int quarter_turns, glm::vec3 local) {
    switch (((quarter_turns % 4) + 4) % 4) {
        case 1: return start + glm::vec3(local.z, local.y, -local.x);
        case 2: return start + glm::vec3(-local.x, local.y, -local.z);
        case 3: return start + glm::vec3(-local.z, local.y, local.x);
        default: return start + local;
    }
}

Aabb turned_box(glm::vec3 start, int quarter_turns, glm::vec3 low, glm::vec3 high) {
    Aabb box;
    box.add(turned(start, quarter_turns, low));
    box.add(turned(start, quarter_turns, high));
    return box;
}

}  // namespace

void CollisionMeshData::add_stairs(glm::vec3 start, int quarter_turns, int count, float rise, float run, float width) {
    for (int i = 0; i < count; ++i) {
        const float x0 = run * static_cast<float>(i);
        add_box(turned_box(start, quarter_turns, {x0, 0.0f, -width * 0.5f},
                           {x0 + run, rise * static_cast<float>(i + 1), width * 0.5f}));
    }
}

void CollisionMeshData::add_ramp(glm::vec3 start, int quarter_turns, float length, float rise, float width) {
    const float w = width * 0.5f;
    const auto p = [&](float x, float y, float z) { return turned(start, quarter_turns, {x, y, z}); };
    // The slope (normal up and back), the back wall, the two sides, the bottom. The local frame is
    // turned about +y only, so the winding seen from outside is kept.
    add_quad(p(0, 0, w), p(length, rise, w), p(length, rise, -w), p(0, 0, -w), SurfaceKind::Walkable);
    add_quad(p(length, 0, -w), p(length, rise, -w), p(length, rise, w), p(length, 0, w), SurfaceKind::Wall);
    add_triangle(p(0, 0, w), p(length, 0, w), p(length, rise, w), SurfaceKind::Wall);
    add_triangle(p(0, 0, -w), p(length, rise, -w), p(length, 0, -w), SurfaceKind::Wall);
    add_quad(p(0, 0, -w), p(length, 0, -w), p(length, 0, w), p(0, 0, w), SurfaceKind::Wall);
}

void CollisionMeshData::append(const CollisionMeshData& other, const glm::mat4& transform) {
    const auto first = static_cast<std::uint32_t>(vertices.size());
    vertices.reserve(vertices.size() + other.vertices.size());
    for (const glm::vec3& v : other.vertices) {
        vertices.emplace_back(transform * glm::vec4(v, 1.0f));
    }
    // A mirroring transform turns the triangles inside out: their winding is flipped back.
    const bool mirrored = glm::determinant(glm::mat3(transform)) < 0.0f;
    indices.reserve(indices.size() + other.indices.size());
    for (std::size_t i = 0; i + 2 < other.indices.size(); i += 3) {
        indices.push_back(first + other.indices[i]);
        indices.push_back(first + other.indices[mirrored ? i + 2 : i + 1]);
        indices.push_back(first + other.indices[mirrored ? i + 1 : i + 2]);
    }
    kinds.insert(kinds.end(), other.kinds.begin(), other.kinds.end());
}

Aabb CollisionMeshData::bounds() const {
    Aabb box;
    for (const glm::vec3& v : vertices) {
        box.add(v);
    }
    return box;
}

void CollisionMeshData::validate(std::string_view name) const {
    const std::string where(name);
    if (indices.size() % 3 != 0) {
        throw std::runtime_error(where + ": collision indices are not whole triangles");
    }
    if (kinds.size() != indices.size() / 3) {
        throw std::runtime_error(where + ": collision has " + std::to_string(kinds.size()) + " kinds for " +
                                 std::to_string(indices.size() / 3) + " triangles");
    }
    for (const std::uint32_t index : indices) {
        if (index >= vertices.size()) {
            throw std::runtime_error(where + ": a collision index is past the vertices");
        }
    }
    for (const glm::vec3& v : vertices) {
        if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) {
            throw std::runtime_error(where + ": a collision vertex is not finite");
        }
    }
    for (const SurfaceKind kind : kinds) {
        if (static_cast<unsigned>(kind) > static_cast<unsigned>(SurfaceKind::Void)) {
            throw std::runtime_error(where + ": unknown collision surface kind");
        }
    }
}

// --- CollisionScene ----------------------------------------------------------------------------

void CollisionScene::clear() {
    triangles_.clear();
    boxes_.clear();
    order_.clear();
    nodes_.clear();
    bounds_ = Aabb{};
    built_ = false;
}

std::size_t CollisionScene::add(const CollisionMeshData& mesh, const glm::mat4& transform, std::uint32_t body) {
    const bool mirrored = glm::determinant(glm::mat3(transform)) < 0.0f;
    std::size_t added = 0;
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        CollisionTriangle t;
        t.a = glm::vec3(transform * glm::vec4(mesh.vertices[mesh.indices[i]], 1.0f));
        t.b = glm::vec3(transform * glm::vec4(mesh.vertices[mesh.indices[mirrored ? i + 2 : i + 1]], 1.0f));
        t.c = glm::vec3(transform * glm::vec4(mesh.vertices[mesh.indices[mirrored ? i + 1 : i + 2]], 1.0f));
        const glm::vec3 n = glm::cross(t.b - t.a, t.c - t.a);
        const float length = std::sqrt(glm::dot(n, n));
        if (!(length > 1e-10f)) {
            continue;  // no area (or not finite)
        }
        t.normal = n / length;
        t.kind = i / 3 < mesh.kinds.size() ? mesh.kinds[i / 3] : SurfaceKind::Walkable;
        t.body = body;
        triangles_.push_back(t);
        ++added;
    }
    built_ = false;
    return added;
}

void CollisionScene::build_node(std::uint32_t slot, std::uint32_t first, std::uint32_t count,
                                std::vector<glm::vec3>& centres) {
    Aabb box;
    Aabb centre_box;
    for (std::uint32_t i = first; i < first + count; ++i) {
        const Aabb& b = boxes_[order_[i]];
        box.add(b.min);
        box.add(b.max);
        centre_box.add(centres[order_[i]]);
    }
    nodes_[slot].box = box;
    if (count <= kLeafSize) {
        nodes_[slot].first = first;
        nodes_[slot].count = count;
        return;
    }
    // Split at the median of the centres along the longest side of their box; ties by index.
    const glm::vec3 size = centre_box.size();
    const int axis = size.x >= size.y && size.x >= size.z ? 0 : (size.y >= size.z ? 1 : 2);
    const auto begin = order_.begin() + first;
    std::sort(begin, begin + count, [&centres, axis](std::uint32_t a, std::uint32_t b) {
        if (centres[a][axis] != centres[b][axis]) {
            return centres[a][axis] < centres[b][axis];
        }
        return a < b;
    });
    const std::uint32_t half = count / 2;
    // The two children side by side: left at `first`, right at `first + 1`.
    const auto left = static_cast<std::uint32_t>(nodes_.size());
    nodes_.emplace_back();
    nodes_.emplace_back();
    nodes_[slot].first = left;
    nodes_[slot].count = 0;
    build_node(left, first, half, centres);
    build_node(left + 1, first + half, count - half, centres);
}

void CollisionScene::build() {
    MOTEUR_PROFILE("collision scene: build");
    boxes_.resize(triangles_.size());
    std::vector<glm::vec3> centres(triangles_.size());
    bounds_ = Aabb{};
    for (std::size_t i = 0; i < triangles_.size(); ++i) {
        boxes_[i] = triangle_box(triangles_[i]);
        centres[i] = (triangles_[i].a + triangles_[i].b + triangles_[i].c) * (1.0f / 3.0f);
        bounds_.add(boxes_[i].min);
        bounds_.add(boxes_[i].max);
    }
    order_.resize(triangles_.size());
    for (std::size_t i = 0; i < order_.size(); ++i) {
        order_[i] = static_cast<std::uint32_t>(i);
    }
    nodes_.clear();
    if (!triangles_.empty()) {
        nodes_.reserve(triangles_.size() * 2 / kLeafSize + 2);
        nodes_.emplace_back();
        build_node(0, 0, static_cast<std::uint32_t>(triangles_.size()), centres);
    }
    built_ = true;
    ++version_;
}

template <typename Visit>
void CollisionScene::for_each_in_box(const Aabb& box, Visit&& visit) const {
    if (!built_ || nodes_.empty()) {
        return;
    }
    std::array<std::uint32_t, 128> stack{};
    std::size_t top = 0;
    stack[top++] = 0;
    while (top > 0) {
        const Node& node = nodes_[stack[--top]];
        if (!boxes_overlap(node.box, box)) {
            continue;
        }
        if (node.count > 0) {
            for (std::uint32_t i = node.first; i < node.first + node.count; ++i) {
                if (boxes_overlap(boxes_[order_[i]], box) && !visit(order_[i])) {
                    return;
                }
            }
        } else if (top + 2 <= stack.size()) {
            stack[top++] = node.first + 1;
            stack[top++] = node.first;
        }
    }
}

std::optional<RayHit> CollisionScene::raycast(glm::vec3 origin, glm::vec3 direction, float max_distance,
                                              std::uint32_t mask) const {
    if (!built_ || nodes_.empty() || !(max_distance >= 0.0f)) {
        return std::nullopt;
    }
    const glm::vec3 inverse(1.0f / direction.x, 1.0f / direction.y, 1.0f / direction.z);
    std::optional<RayHit> best;
    float limit = max_distance;
    std::array<std::uint32_t, 128> stack{};
    std::size_t top = 0;
    stack[top++] = 0;
    while (top > 0) {
        const Node& node = nodes_[stack[--top]];
        if (!ray_meets_box(node.box, origin, inverse, limit)) {
            continue;
        }
        if (node.count > 0) {
            for (std::uint32_t i = node.first; i < node.first + node.count; ++i) {
                const std::uint32_t index = order_[i];
                const CollisionTriangle& t = triangles_[index];
                if ((surface_bit(t.kind) & mask) == 0) {
                    continue;
                }
                const std::optional<float> distance = ray_triangle(origin, direction, t);
                if (!distance || *distance > limit) {
                    continue;
                }
                if (best && *distance == best->distance && index > best->triangle) {
                    continue;
                }
                best = RayHit{*distance, origin + direction * *distance, t.normal, index};
                limit = *distance;
            }
        } else if (top + 2 <= stack.size()) {
            stack[top++] = node.first + 1;
            stack[top++] = node.first;
        }
    }
    return best;
}

bool CollisionScene::segment_clear(glm::vec3 from, glm::vec3 to, std::uint32_t mask) const {
    const glm::vec3 d = to - from;
    const float length = std::sqrt(glm::dot(d, d));
    if (length <= 0.0f) {
        return true;
    }
    return !raycast(from, d / length, length, mask).has_value();
}

std::optional<GroundHit> CollisionScene::ground_at(glm::vec3 point, float max_up, float max_down, float min_normal_y,
                                                   std::uint32_t mask) const {
    if (!built_ || nodes_.empty()) {
        return std::nullopt;
    }
    const float top = point.y + max_up;
    const float bottom = point.y - max_down;
    Aabb column;
    column.add({point.x, bottom, point.z});
    column.add({point.x, top, point.z});
    std::optional<GroundHit> best;
    const glm::vec3 origin(point.x, top, point.z);
    for_each_in_box(column, [&](std::uint32_t index) {
        const CollisionTriangle& t = triangles_[index];
        if ((surface_bit(t.kind) & mask) == 0 || t.normal.y < min_normal_y || t.normal.y <= 0.0f) {
            return true;
        }
        const std::optional<float> distance = ray_triangle(origin, {0.0f, -1.0f, 0.0f}, t);
        if (!distance) {
            return true;
        }
        const float height = top - *distance;
        if (height < bottom) {
            return true;
        }
        if (!best || height > best->height || (height == best->height && index < best->triangle)) {
            best = GroundHit{height, t.normal, t.kind, index};
        }
        return true;
    });
    return best;
}

void CollisionScene::sphere_contacts(glm::vec3 centre, float radius, std::uint32_t mask, std::vector<Contact>& out) const {
    out.clear();
    Aabb box;
    box.add(centre - glm::vec3(radius));
    box.add(centre + glm::vec3(radius));
    for_each_in_box(box, [&](std::uint32_t index) {
        const CollisionTriangle& t = triangles_[index];
        if ((surface_bit(t.kind) & mask) != 0) {
            const glm::vec3 point = closest_point_on_triangle(centre, t.a, t.b, t.c);
            if (const std::optional<Contact> contact = make_contact(index, t, centre, point, centre, radius)) {
                out.push_back(*contact);
            }
        }
        return true;
    });
    sort_contacts(out);
}

void CollisionScene::capsule_contacts(glm::vec3 a, glm::vec3 b, float radius, std::uint32_t mask,
                                      std::vector<Contact>& out) const {
    out.clear();
    Aabb box;
    box.add(glm::min(a, b) - glm::vec3(radius));
    box.add(glm::max(a, b) + glm::vec3(radius));
    const glm::vec3 middle = (a + b) * 0.5f;
    for_each_in_box(box, [&](std::uint32_t index) {
        const CollisionTriangle& t = triangles_[index];
        if ((surface_bit(t.kind) & mask) != 0) {
            glm::vec3 core;
            const glm::vec3 point = closest_points_segment_triangle(a, b, t.a, t.b, t.c, core);
            if (const std::optional<Contact> contact = make_contact(index, t, core, point, middle, radius)) {
                out.push_back(*contact);
            }
        }
        return true;
    });
    sort_contacts(out);
}

void CollisionScene::box_triangles(const Aabb& box, std::uint32_t mask, std::vector<std::uint32_t>& out) const {
    out.clear();
    for_each_in_box(box, [&](std::uint32_t index) {
        const CollisionTriangle& t = triangles_[index];
        if ((surface_bit(t.kind) & mask) == 0) {
            return true;
        }
        // Separating axis test of the triangle and the box (Akenine-Möller): box axes are covered
        // by the boxes' overlap; the triangle's plane and the nine edge cross products remain.
        const glm::vec3 centre = box.center();
        const glm::vec3 half = box.size() * 0.5f;
        const glm::vec3 v0 = t.a - centre, v1 = t.b - centre, v2 = t.c - centre;
        const glm::vec3 edges[3] = {v1 - v0, v2 - v1, v0 - v2};
        bool separated = false;
        for (int e = 0; e < 3 && !separated; ++e) {
            for (int axis = 0; axis < 3 && !separated; ++axis) {
                glm::vec3 unit(0.0f);
                unit[axis] = 1.0f;
                const glm::vec3 l = glm::cross(unit, edges[e]);
                const float p0 = glm::dot(v0, l), p1 = glm::dot(v1, l), p2 = glm::dot(v2, l);
                const float r = half.x * std::fabs(l.x) + half.y * std::fabs(l.y) + half.z * std::fabs(l.z);
                separated = std::min({p0, p1, p2}) > r || std::max({p0, p1, p2}) < -r;
            }
        }
        if (!separated) {
            const float r = half.x * std::fabs(t.normal.x) + half.y * std::fabs(t.normal.y) + half.z * std::fabs(t.normal.z);
            separated = std::fabs(glm::dot(t.normal, v0)) > r;
        }
        if (!separated) {
            out.push_back(index);
        }
        return true;
    });
    std::sort(out.begin(), out.end());
}

glm::vec3 closest_point_on_triangle(glm::vec3 p, glm::vec3 a, glm::vec3 b, glm::vec3 c) {
    const glm::vec3 ab = b - a, ac = c - a, ap = p - a;
    const float d1 = glm::dot(ab, ap), d2 = glm::dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) {
        return a;
    }
    const glm::vec3 bp = p - b;
    const float d3 = glm::dot(ab, bp), d4 = glm::dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) {
        return b;
    }
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        return a + ab * (d1 / (d1 - d3));
    }
    const glm::vec3 cp = p - c;
    const float d5 = glm::dot(ab, cp), d6 = glm::dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) {
        return c;
    }
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        return a + ac * (d2 / (d2 - d6));
    }
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    }
    const float denominator = 1.0f / (va + vb + vc);
    return a + ab * (vb * denominator) + ac * (vc * denominator);
}

glm::vec3 closest_points_segment_triangle(glm::vec3 p, glm::vec3 q, glm::vec3 a, glm::vec3 b, glm::vec3 c,
                                          glm::vec3& on_segment) {
    // The segment crosses the triangle: distance 0 there.
    CollisionTriangle t;
    t.a = a;
    t.b = b;
    t.c = c;
    const glm::vec3 d = q - p;
    const float length = std::sqrt(glm::dot(d, d));
    if (length > 1e-9f) {
        if (const std::optional<float> distance = ray_triangle(p, d / length, t); distance && *distance <= length) {
            on_segment = p + d * (*distance / length);
            return on_segment;
        }
    }
    // Otherwise the nearest of: each end to the triangle, the segment to each edge.
    glm::vec3 best_triangle = closest_point_on_triangle(p, a, b, c);
    on_segment = p;
    glm::vec3 diff = p - best_triangle;
    float best = glm::dot(diff, diff);
    const glm::vec3 at_q = closest_point_on_triangle(q, a, b, c);
    diff = q - at_q;
    if (glm::dot(diff, diff) < best) {
        best = glm::dot(diff, diff);
        best_triangle = at_q;
        on_segment = q;
    }
    const glm::vec3 corners[4] = {a, b, c, a};
    for (int e = 0; e < 3; ++e) {
        glm::vec3 c1, c2;
        const float distance2 = closest_segments(p, q, corners[e], corners[e + 1], c1, c2);
        if (distance2 < best) {
            best = distance2;
            on_segment = c1;
            best_triangle = c2;
        }
    }
    return best_triangle;
}

}  // namespace moteur
