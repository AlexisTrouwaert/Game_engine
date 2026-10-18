#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "moteur/aabb.hpp"
#include "moteur/pathfinding.hpp"
#include "moteur/reflection.hpp"

namespace moteur {

class CollisionScene;
class DebugLineBuffer;
class NavGrid;

// Navigation in 3D (milestone 9, part 6): a navigation mesh built from a level's CollisionScene,
// paths on it, the queries the logic asks, links off the mesh (jumps, ladders, doors), areas with
// costs, obstacles that change it locally, and flow fields over its polygons for crowds.
//
// Built with Recast and searched with Detour (zlib licence, decided 2026-10-17), behind this API:
// no type of theirs appears here, so that the implementation can change. Deterministic on a
// machine: the same level and settings give the same mesh, the same queries the same answers.

/// A polygon of a NavMesh (0: none). Valid until the tile holding it is built again (see
/// NavMesh::version()).
using NavPolyRef = std::uint64_t;

/// What a piece of the mesh is, for the costs of the searches (NavFilter::area_costs). Marked by
/// volumes (NavAreaVolume) and links (NavLink).
enum class NavArea : std::uint8_t {
    Ground = 1,  ///< the default
    Water,       ///< shallow water: slower, avoided
    Fire,        ///< burning ground: much avoided
    Door,        ///< a door's link (opened or closed with NavMesh::set_link_enabled())
    Jump,        ///< a jump or a ladder (a link)
};
MOTEUR_REFLECT_ENUM(NavArea, "none", "ground", "water", "fire", "door", "jump");

/// The number of NavArea values (and of NavFilter::area_costs), 0 (none) included.
inline constexpr std::size_t kNavAreaCount = 6;

/// The size of the characters a NavMesh is for: one mesh per size (the hero and small monsters,
/// big ones), each eroded by its radius.
struct NavAgent {
    float radius = 0.35f;     ///< metres
    float height = 1.8f;      ///< metres: lower ceilings are not walkable
    float max_climb = 0.35f;  ///< the highest step, metres (as ControllerSettings::step_height)
    /// The steepest walkable ground, as the lowest y of its normal (0.707: 45 degrees).
    float min_ground_normal_y = 0.7071f;

    /// Exact comparison.
    bool operator==(const NavAgent&) const = default;
};
MOTEUR_REFLECT(NavAgent,
    MOTEUR_FIELD(radius, {.min = 0.05, .max = 10.0, .unit = "m", .flags = kFieldDefault}),
    MOTEUR_FIELD(height, {.min = 0.1, .max = 20.0, .unit = "m", .flags = kFieldDefault}),
    MOTEUR_FIELD(max_climb, {.min = 0.0, .max = 5.0, .unit = "m", .flags = kFieldDefault}),
    MOTEUR_FIELD(min_ground_normal_y, {.min = 0.0, .max = 1.0, .flags = kFieldDefault}));

/// How finely a NavMesh is built (Recast's settings, in metres).
struct NavBuildSettings {
    float cell_size = 0.15f;         ///< voxels across: finer follows the walls better, costs more
    float cell_height = 0.1f;        ///< voxels up
    float tile_size = 24.0f;         ///< metres per tile: the unit rebuilt for an obstacle or an edit
    float region_min_size = 2.0f;    ///< square metres: smaller islands are dropped
    float region_merge_size = 20.0f; ///< square metres: smaller regions merge with a neighbour
    float edge_max_length = 12.0f;   ///< metres: longer contour edges are split
    float edge_max_error = 1.3f;     ///< voxels: how far the contour may stray from the walls
    float detail_sample_distance = 6.0f;   ///< voxels: the detail mesh's sample spacing (0: none)
    float detail_sample_max_error = 1.0f;  ///< voxels: its height error

    /// Exact comparison.
    bool operator==(const NavBuildSettings&) const = default;
};
MOTEUR_REFLECT(NavBuildSettings,
    MOTEUR_FIELD(cell_size, {.min = 0.02, .max = 2.0, .unit = "m", .flags = kFieldDefault}),
    MOTEUR_FIELD(cell_height, {.min = 0.02, .max = 2.0, .unit = "m", .flags = kFieldDefault}),
    MOTEUR_FIELD(tile_size, {.min = 4.0, .max = 256.0, .unit = "m", .flags = kFieldDefault}),
    MOTEUR_FIELD(region_min_size, {.min = 0.0, .unit = "m2", .flags = kFieldDefault}),
    MOTEUR_FIELD(region_merge_size, {.min = 0.0, .unit = "m2", .flags = kFieldDefault}),
    MOTEUR_FIELD(edge_max_length, {.min = 0.0, .unit = "m", .flags = kFieldDefault}),
    MOTEUR_FIELD(edge_max_error, {.min = 0.1, .max = 10.0, .flags = kFieldDefault}),
    MOTEUR_FIELD(detail_sample_distance, {.min = 0.0, .flags = kFieldDefault}),
    MOTEUR_FIELD(detail_sample_max_error, {.min = 0.0, .flags = kFieldDefault}));

/// A box of the level whose walkable ground is of another area (water, fire).
struct NavAreaVolume {
    Aabb box;                       ///< world, metres
    NavArea area = NavArea::Water;  ///< what the ground inside is
};

/// A link off the mesh between two points: a jump, a ladder, a door. Its ends must lie on the mesh
/// (within its radius).
struct NavLink {
    glm::vec3 from{0.0f};         ///< its start, on the mesh (metres)
    glm::vec3 to{0.0f};           ///< its end, on the mesh (metres)
    float radius = 0.5f;          ///< how near an end a point counts as on it, metres
    bool bidirectional = true;    ///< false: from -> to only (a drop)
    NavArea area = NavArea::Jump; ///< Jump (or ladder), Door (closed with NavMesh::set_link_enabled())
    std::uint32_t id = 0;         ///< the game's, for set_link_enabled() (0: none)
};

/// Which polygons a search may use, and what each area costs.
struct NavFilter {
    /// The cost of a metre in each NavArea (by its value; index 0 unused).
    std::array<float, kNavAreaCount> area_costs{1.0f, 1.0f, 3.0f, 10.0f, 1.0f, 1.5f};
    bool doors = true;  ///< false: never through a door's link, even open
    bool jumps = true;  ///< false: never by a jump or ladder link
};

/// A path on a NavMesh: its corners from the start (excluded) to the goal, or to the reachable point
/// nearest it.
struct NavPath {
    PathStatus status = PathStatus::None;  ///< Found, Partial (nearest reachable point), None
    std::vector<glm::vec3> points;         ///< the corners, the last one the end; empty for None
    int expanded = 0;                      ///< polygons searched
};

/// How a NavMesh is cut in tiles (milestone 9, part 8): what a prepared level stores, so that the
/// tiles of its chunks can be added and removed as they load (tile (x, z) of a level's mesh is its
/// chunk (x, z): the level's chunk size is the tile size).
struct NavTiling {
    NavAgent agent;             ///< the size of character
    NavBuildSettings settings;  ///< tile_size: the level's chunk size
    glm::vec3 origin{0.0f};  ///< the corner of tile (0, 0): the level's lowest corner
    int tiles_x = 0;         ///< tiles along x
    int tiles_z = 0;         ///< tiles along z
};

/// What NavMesh::build() and rebuild() did.
struct NavBuildStats {
    int tiles = 0;             ///< tiles built (with at least one polygon)
    int polygons = 0;          ///< in those tiles
    double milliseconds = 0.0; ///< the time it took
};

/// A navigation mesh for one size of character. Built from a CollisionScene: its Walkable
/// triangles (no steeper than NavAgent::min_ground_normal_y) are the ground; Wall and Obstacle
/// triangles block; Void ones are ignored. The world is cut in square tiles (NavBuildSettings::
/// tile_size) so that a change rebuilds only the tiles it touches.
///
/// Queries are const but share one search object: from the main thread only (or one NavMesh per
/// thread). Building and rebuilding are long (milliseconds per tile): at load time, in the
/// preparation, the editor, or for a door.
class NavMesh {
public:
    NavMesh();
    ~NavMesh();
    NavMesh(NavMesh&&) noexcept;             ///< takes the other's mesh (left empty)
    NavMesh& operator=(NavMesh&&) noexcept;  ///< takes the other's mesh (left empty)
    NavMesh(const NavMesh&) = delete;
    NavMesh& operator=(const NavMesh&) = delete;

    /// Builds the whole mesh of `scene` (which must be built) for `agent`. `areas` mark ground of
    /// other costs, `links` join points off the mesh. Throws std::runtime_error for settings Recast
    /// refuses or a level too big (more than 4 million tiles). Replaces what was there.
    NavBuildStats build(const CollisionScene& scene, const NavAgent& agent, const NavBuildSettings& settings = {},
                        std::span<const NavAreaVolume> areas = {}, std::span<const NavLink> links = {});
    /// Builds again the tiles overlapping `area` from `scene` (the level changed there: an edit, a
    /// wall destroyed), with the same settings, areas, links and obstacles. Polygon references in
    /// those tiles become invalid.
    NavBuildStats rebuild(const CollisionScene& scene, const Aabb& area);
    /// A box nothing may walk through (a closed gate, a crate pushed in the way): the tiles under it
    /// are built again. Returns its id (never 0).
    std::uint32_t add_obstacle(const CollisionScene& scene, const Aabb& box);
    /// Removes an obstacle and builds its tiles again. Nothing for an unknown id.
    void remove_obstacle(const CollisionScene& scene, std::uint32_t id);
    /// Opens or closes the links of `id` (a door): closed, no search goes through them.
    void set_link_enabled(std::uint32_t id, bool enabled);
    /// Keeps only the polygons one can reach from `seeds` (the level's entry points, its spawn
    /// points), through links too (doors open or not): Recast finds ground wherever there is room,
    /// inside a closed box as on top of it, and those unreachable islands would catch a click or a
    /// spawn. Kept: applied again after every rebuild, and saved. An empty list keeps everything.
    void prune(std::span<const glm::vec3> seeds);

    /// True before build() (or after a failed one).
    bool empty() const;
    /// Grows at each build, rebuild or obstacle change: paths and fields of older versions are stale.
    std::uint64_t version() const;
    /// The size of character it was built for.
    const NavAgent& agent() const;
    /// The box of the level it was built from.
    const Aabb& bounds() const;
    /// The number of tiles holding polygons.
    int tile_count() const;
    /// The number of polygons (links included).
    int polygon_count() const;

    /// The polygon nearest `point` within `half_extents` (a box around it), and the point of it
    /// nearest `point` in `nearest`. 0 when none.
    NavPolyRef nearest_polygon(glm::vec3 point, glm::vec3 half_extents, glm::vec3* nearest = nullptr,
                               const NavFilter& filter = {}) const;
    /// The polygon under `position` (a mover's feet): `hint` if the point is still over it (cheap:
    /// what a mover asks every tick), else the nearest within 0.5 m across and a metre up and down.
    /// 0 off the mesh.
    NavPolyRef locate(glm::vec3 position, NavPolyRef hint = 0) const;
    /// The point of the mesh nearest `point` within `half_extents`, or nullopt.
    std::optional<glm::vec3> nearest_point(glm::vec3 point, glm::vec3 half_extents = {1.0f, 2.0f, 1.0f},
                                           const NavFilter& filter = {}) const;
    /// A path from `from` to `to` (both put on the mesh first, within 2 m across and 4 m up and
    /// down): A* over the polygons, then straightened (funnel). Partial when `to` cannot be reached
    /// (to the reachable point nearest it), None when `from` is not on the mesh. `max_nodes` caps
    /// the polygons searched.
    NavPath find_path(glm::vec3 from, glm::vec3 to, const NavFilter& filter = {}, int max_nodes = 4096) const;
    /// True if one can walk in a straight line on the mesh from `from` to `to` (no edge of the mesh
    /// crossed); `hit`, if not null, gets where it stops otherwise.
    bool walkable_line(glm::vec3 from, glm::vec3 to, glm::vec3* hit = nullptr, const NavFilter& filter = {}) const;
    /// The ground of the mesh under `point` (within `max_distance` up and down), or nullopt.
    std::optional<float> height_at(glm::vec3 point, float max_distance = 2.0f) const;

    /// The polygons of the tiles overlapping `area` as triangles (three corners each), for the
    /// tools: debug lines, the editor. Areas in `areas` (one per triangle).
    void triangles(const Aabb& area, std::vector<glm::vec3>& corners, std::vector<NavArea>* areas = nullptr) const;

    /// For chunked levels (milestone 9, part 8): `build()` with the tile grid fixed by `tiling`
    /// (its origin and size: tile (x, z) covers origin + [x, x + 1) x tile size), so that a level's
    /// tiles line up with its chunks whatever the scene's bounds.
    NavBuildStats build_tiled(const CollisionScene& scene, const NavTiling& tiling, std::span<const NavAreaVolume> areas = {},
                              std::span<const NavLink> links = {});
    /// The tiling of this mesh (after build(), build_tiled(), or start_tiled()).
    NavTiling tiling() const;
    /// An empty mesh laid out by `tiling`, to which tiles are added one by one (add_tile()): what a
    /// chunked level does as its chunks load. Throws std::runtime_error if Detour refuses it.
    void start_tiled(const NavTiling& tiling);
    /// The tile (x, z) as Detour stores it (empty when it has no polygon): what a prepared level
    /// keeps per chunk.
    std::vector<std::uint8_t> tile_data(int x, int z) const;
    /// Adds a tile made by tile_data() (its neighbours already there are joined to it). Replaces the
    /// tile at its place. Throws std::runtime_error naming `name` if Detour refuses it (another
    /// version, damaged).
    void add_tile(const std::vector<std::uint8_t>& data, const std::string& name);
    /// Removes the tile (x, z) (its chunk unloaded); nothing if it is not there.
    void remove_tile(int x, int z);

    /// The whole mesh in binary (MNAV: settings, agent, areas, links, obstacles, and Detour's tiles),
    /// for a prepared level. Throws std::runtime_error if empty.
    std::vector<std::uint8_t> save() const;
    /// A mesh saved by save(). Throws std::runtime_error naming `name` for another format or
    /// version, or a damaged file (its checksum). It can be rebuilt (rebuild(), obstacles) only
    /// with the same level's CollisionScene.
    static NavMesh load(const std::vector<std::uint8_t>& bytes, const std::string& name);

    /// For NavFlowField (internal): Detour's mesh and the area of a polygon.
    struct Impl;
    /// See Impl.
    const Impl& impl() const { return *impl_; }

private:
    std::unique_ptr<Impl> impl_;
};

/// The distance to a target over a NavMesh's polygons, for crowds (many movers going to the same
/// place: the hero). Each polygon knows its next polygon towards the target and the point to aim
/// for on the edge between them; computed once per target (milliseconds), read in O(1) per mover.
/// Deterministic (ties broken by the polygon's reference).
class NavFlowField {
public:
    /// The field towards `target` over `mesh`, out to `max_cost` (metres times the area costs).
    /// Empty if the target is not on the mesh.
    void compute(const NavMesh& mesh, glm::vec3 target, float max_cost = 60.0f, const NavFilter& filter = {});
    /// True after a compute() that found the target on the mesh, for the mesh's version then.
    bool valid() const { return !costs_.empty(); }
    /// The target, put on the mesh.
    glm::vec3 target() const { return target_; }
    /// The mesh version it was computed on (NavMesh::version()).
    std::uint64_t version() const { return version_; }
    /// The direction on the plane (unit, or zero outside the field) from `position`, standing on
    /// polygon `poly` (NavMesh::nearest_polygon()), towards the target: to the aim point of its
    /// polygon, or the target itself in the target's polygon.
    glm::vec2 direction(glm::vec3 position, NavPolyRef poly) const;
    /// The cost from `poly` to the target, or a negative value outside the field.
    float cost(NavPolyRef poly) const;

private:
    struct Entry {
        float cost = -1.0f;   ///< negative: outside the field
        glm::vec3 a{0.0f};    ///< the portal to the next polygon (its two ends)
        glm::vec3 b{0.0f};
    };
    glm::vec3 target_{0.0f};
    NavPolyRef target_poly_ = 0;
    std::uint64_t version_ = 0;
    std::vector<Entry> costs_;              ///< one per polygon of the mesh, tile after tile
    std::vector<std::int32_t> tile_first_;  ///< by Detour tile index: its first entry in costs_ (-1: none)
    std::vector<std::int32_t> tile_count_;  ///< by Detour tile index: its polygon count
    std::uint32_t tile_bits_ = 0;           ///< Detour's reference layout
    std::uint32_t poly_bits_ = 0;
    /// The entry of `poly`, or SIZE_MAX if it is not one of the mesh's polygons.
    std::size_t index_of(NavPolyRef poly) const;
};

/// The NavGrid seen from a NavMesh (milestone 6's grid as a view of the mesh): a cell of `width` x
/// `height` metres from `origin` (x, z) is walkable if the mesh has ground within `tolerance`
/// metres of `level` under its centre; opaque if not walkable. For the fog of war and the quick
/// queries of a floor.
NavGrid nav_grid_view(const NavMesh& mesh, glm::vec2 origin, int width, int height, float level, float tolerance = 1.0f);

/// Draws the polygons of `mesh` within `area` (blue ground, green links...) into `lines`.
void draw_nav_mesh(DebugLineBuffer& lines, const NavMesh& mesh, const Aabb& area);

}  // namespace moteur
