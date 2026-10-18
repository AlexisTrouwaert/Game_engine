#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "moteur/aabb.hpp"
#include "moteur/reflection.hpp"

namespace moteur {

// The world of the logic in 3D (milestone 9, part 5): the static collision geometry of a level, as
// triangles marked by what they are, in a bounding volume hierarchy, with the queries the logic
// asks (rays, spheres, capsules, boxes, the ground under a point). Plain CPU code, no GPU.
//
// Determinism: plain float arithmetic (+ - * / sqrt), no trigonometry; the tree is built in a fixed
// order (ties broken by the triangle's index) and every query returns its results in a fixed order
// (by distance, then by triangle index), so two runs, or two machines with the same compiler
// settings, give the same answers.

/// What a collision triangle is, for the logic. Set by the import (the node's name, or the slope of
/// a visible mesh: see ModelImportSettings::collision) and kept in the level.
enum class SurfaceKind : std::uint8_t {
    Walkable,  ///< ground: characters stand and walk on it (if not too steep), navigation is built on it
    Wall,      ///< blocks characters and sight; never ground
    Obstacle,  ///< blocks characters (a crate, a fence) but not necessarily sight; not navigable ground
    Void,      ///< marks where there is no ground (a pit, the edge of the world): blocks nothing,
               ///< characters fall through; the ground query reports it so that the game can react
};
MOTEUR_REFLECT_ENUM(SurfaceKind, "walkable", "wall", "obstacle", "void");

/// The SurfaceKind of a collision node's name: `COL_walk...`, `COL_wall...`, `COL_obstacle...`,
/// `COL_void...` (also `UCX_` instead of `COL_`, case ignored); nullopt for `COL_` alone or another
/// word (the slope decides then). Not a collision node at all: see is_collision_node().
std::optional<SurfaceKind> surface_kind_of_node(std::string_view node);
/// True for a node whose meshes are collision only (never drawn): its name starts with `COL_` or
/// `UCX_` (Unreal's convention), case ignored.
bool is_collision_node(std::string_view node);

/// Bit masks of SurfaceKind, for the queries: which kinds they see.
enum SurfaceMask : std::uint32_t {
    kSurfaceWalkable = 1u << static_cast<unsigned>(SurfaceKind::Walkable),
    kSurfaceWall = 1u << static_cast<unsigned>(SurfaceKind::Wall),
    kSurfaceObstacle = 1u << static_cast<unsigned>(SurfaceKind::Obstacle),
    kSurfaceVoid = 1u << static_cast<unsigned>(SurfaceKind::Void),
    kSurfaceSolid = kSurfaceWalkable | kSurfaceWall | kSurfaceObstacle,  ///< what blocks characters
    kSurfaceSight = kSurfaceWalkable | kSurfaceWall,                     ///< what blocks lines of sight
    kSurfaceAll = 0xFu,
};

/// The bit of `kind` in a SurfaceMask.
constexpr std::uint32_t surface_bit(SurfaceKind kind) {
    return 1u << static_cast<unsigned>(kind);
}

/// Collision triangles as a model or a level piece holds them: indexed, one kind per triangle, in
/// the model's space (metres). Built by the import (ModelData::collision) or by code.
struct CollisionMeshData {
    std::vector<glm::vec3> vertices;     ///< positions
    std::vector<std::uint32_t> indices;  ///< three per triangle, counter-clockwise seen from the outside
    std::vector<SurfaceKind> kinds;      ///< one per triangle

    /// The number of triangles (indices / 3).
    std::size_t triangle_count() const { return indices.size() / 3; }
    /// True without any triangle.
    bool empty() const { return indices.empty(); }
    /// Appends a triangle of three new vertices.
    void add_triangle(glm::vec3 a, glm::vec3 b, glm::vec3 c, SurfaceKind kind);
    /// Appends a quad (two triangles a b c, a c d) of four new vertices.
    void add_quad(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, SurfaceKind kind);
    /// Appends a closed box: its top of kind `top`, its bottom and sides of kind `sides`, faces
    /// outwards. For blocking out levels and tests.
    void add_box(const Aabb& box, SurfaceKind top = SurfaceKind::Walkable, SurfaceKind sides = SurfaceKind::Wall);
    /// Appends a flight of `count` steps (closed boxes) going from `start` (the foot of the first
    /// step, at its middle) along +x rotated by `rotation` (a multiple of 90 degrees as one of
    /// 0, 1, 2, 3 quarter turns about +y), each `rise` high and `run` deep, `width` wide.
    void add_stairs(glm::vec3 start, int quarter_turns, int count, float rise, float run, float width);
    /// Appends a ramp (a walkable top, wall sides and back) from `start` (bottom edge, at its middle)
    /// along +x turned by `quarter_turns`, `length` long, rising by `rise`, `width` wide.
    void add_ramp(glm::vec3 start, int quarter_turns, float length, float rise, float width);
    /// Appends `other` moved by `transform`.
    void append(const CollisionMeshData& other, const glm::mat4& transform);
    /// The box around every vertex.
    Aabb bounds() const;
    /// Throws std::runtime_error naming `name` if the counts do not match (indices not a multiple of
    /// 3, an index past the vertices, not one kind per triangle) or a vertex is not finite.
    void validate(std::string_view name) const;
};

/// One triangle of a CollisionScene, in world space.
struct CollisionTriangle {
    glm::vec3 a{0.0f};  ///< first corner (counter-clockwise seen from the front)
    glm::vec3 b{0.0f};  ///< second corner
    glm::vec3 c{0.0f};  ///< third corner
    glm::vec3 normal{0.0f, 1.0f, 0.0f};   ///< unit, towards the front (from the winding)
    SurfaceKind kind = SurfaceKind::Walkable;  ///< what it is for the logic
    std::uint32_t body = 0;               ///< what it belongs to (given to CollisionScene::add)
};

/// Where a ray met a triangle.
struct RayHit {
    float distance = 0.0f;       ///< along the ray, in metres (the ray's direction is unit)
    glm::vec3 point{0.0f};       ///< origin + direction * distance
    glm::vec3 normal{0.0f};      ///< the triangle's normal (front or back: as it is stored)
    std::uint32_t triangle = 0;  ///< index in CollisionScene::triangles()
};

/// The ground under a point (CollisionScene::ground_at()).
struct GroundHit {
    float height = 0.0f;         ///< y of the ground under the point
    glm::vec3 normal{0.0f, 1.0f, 0.0f};        ///< the ground's normal (unit, up)
    SurfaceKind kind = SurfaceKind::Walkable;  ///< what the ground is
    std::uint32_t triangle = 0;  ///< index in CollisionScene::triangles()
};

/// A triangle a shape overlaps, and how to get out of it.
struct Contact {
    std::uint32_t triangle = 0;  ///< index in CollisionScene::triangles()
    glm::vec3 point{0.0f};       ///< the triangle's point nearest the shape's core (centre or segment)
    glm::vec3 normal{0.0f};      ///< unit, from the triangle towards the shape's core
    float depth = 0.0f;          ///< how far the shape is inside (radius - distance), > 0
};

/// The static collision geometry of a level (or of a chunk of it, milestone 9 part 8): triangles in
/// world space in a bounding volume hierarchy. Fill it with add(), then build() before querying;
/// queries on a scene not built see nothing. Queries are const and thread-safe (each uses its own
/// stack); add() and build() are not.
class CollisionScene {
public:
    /// Removes every triangle.
    void clear();
    /// Adds `mesh` moved by `transform` (an affine matrix: translation, rotation, uniform or
    /// non-uniform scale), its triangles tagged with `body`. Degenerate triangles (no area) are left
    /// out. Returns how many were added. The scene must be built again.
    std::size_t add(const CollisionMeshData& mesh, const glm::mat4& transform, std::uint32_t body = 0);
    /// Builds the tree. O(n log n); a few milliseconds for a hundred thousand triangles.
    void build();
    /// True once built and not changed since.
    bool built() const { return built_; }
    /// Grows by one at each build(): what was computed on an older version (a navigation mesh, a
    /// cached query) must be computed again.
    std::uint64_t version() const { return version_; }

    /// Every triangle, in the order they were added.
    const std::vector<CollisionTriangle>& triangles() const { return triangles_; }
    /// The box around every triangle.
    const Aabb& bounds() const { return bounds_; }
    /// The number of nodes of the tree (for the tools).
    std::size_t node_count() const { return nodes_.size(); }

    /// The nearest triangle the ray meets, of the kinds in `mask`, within `max_distance` metres.
    /// `direction` must be unit. Both faces count. Ties: the lower triangle index.
    std::optional<RayHit> raycast(glm::vec3 origin, glm::vec3 direction, float max_distance,
                                  std::uint32_t mask = kSurfaceAll) const;
    /// True if nothing of `mask` lies on the segment from `from` to `to` (a line of sight).
    bool segment_clear(glm::vec3 from, glm::vec3 to, std::uint32_t mask = kSurfaceSight) const;
    /// The ground under `point`: the highest triangle of `mask` met by a vertical ray going down
    /// from `max_up` metres above the point to `max_down` metres below it whose normal points up at
    /// least `min_normal_y` (steeper faces are walls, never ground). Ties: the lower index.
    std::optional<GroundHit> ground_at(glm::vec3 point, float max_up, float max_down, float min_normal_y = 0.0f,
                                       std::uint32_t mask = kSurfaceWalkable | kSurfaceObstacle | kSurfaceVoid) const;

    /// The triangles of `mask` the sphere overlaps (touching does not count), deepest first, then
    /// by index. Clears `out` first.
    void sphere_contacts(glm::vec3 centre, float radius, std::uint32_t mask, std::vector<Contact>& out) const;
    /// The same for a capsule: the points within `radius` of the segment from `a` to `b`.
    void capsule_contacts(glm::vec3 a, glm::vec3 b, float radius, std::uint32_t mask, std::vector<Contact>& out) const;
    /// The indices of the triangles of `mask` whose box overlaps `box` and that cut it, sorted.
    /// Clears `out` first.
    void box_triangles(const Aabb& box, std::uint32_t mask, std::vector<std::uint32_t>& out) const;

private:
    struct Node {
        Aabb box;
        std::uint32_t first = 0;  ///< leaf: first index in order_; inner: index of the left child
        std::uint32_t count = 0;  ///< leaf: number of triangles; 0 for an inner node (right child = first + 1)
    };
    // Calls visit(triangle index) for every triangle whose box may meet `box`; visit returns false
    // to stop.
    template <typename Visit>
    void for_each_in_box(const Aabb& box, Visit&& visit) const;
    void build_node(std::uint32_t slot, std::uint32_t first, std::uint32_t count, std::vector<glm::vec3>& centres);

    std::vector<CollisionTriangle> triangles_;
    std::vector<Aabb> boxes_;           ///< one per triangle
    std::vector<std::uint32_t> order_;  ///< triangle indices, grouped by leaf
    std::vector<Node> nodes_;           ///< nodes_[0] is the root
    Aabb bounds_;
    bool built_ = false;
    std::uint64_t version_ = 0;
};

/// The point of the triangle (a, b, c) nearest `p` (Ericson, Real-Time Collision Detection, 5.1.5).
glm::vec3 closest_point_on_triangle(glm::vec3 p, glm::vec3 a, glm::vec3 b, glm::vec3 c);
/// The points of the segment [p, q] and of the triangle (a, b, c) nearest each other: `on_segment`
/// and the returned point of the triangle.
glm::vec3 closest_points_segment_triangle(glm::vec3 p, glm::vec3 q, glm::vec3 a, glm::vec3 b, glm::vec3 c,
                                          glm::vec3& on_segment);

}  // namespace moteur
