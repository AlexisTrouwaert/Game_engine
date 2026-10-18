#include "moteur/navigation.hpp"

#include <DetourCommon.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <Recast.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <queue>
#include <set>
#include <stdexcept>

#include "moteur/binary_io.hpp"
#include "moteur/collision_scene.hpp"
#include "moteur/debug_lines.hpp"
#include "moteur/nav_grid.hpp"
#include "moteur/profiler.hpp"

namespace moteur {

namespace {

// Polygon flags of the mesh (Detour's filter includes and excludes by these).
constexpr unsigned short kFlagWalk = 1;
constexpr unsigned short kFlagDoor = 2;
constexpr unsigned short kFlagJump = 4;
constexpr unsigned short kFlagDisabled = 8;
constexpr unsigned short kFlagPruned = 16;  // unreachable from the seeds (NavMesh::prune())

constexpr int kMaxPathPolys = 4096;
constexpr int kMaxStraight = 256;
const glm::vec3 kPathExtents{2.0f, 4.0f, 2.0f};

const char kMagic[4] = {'M', 'N', 'A', 'V'};
constexpr std::uint32_t kVersion = 1;

std::uint64_t fnv1a(const std::uint8_t* data, std::size_t size) {
    std::uint64_t hash = 14695981039346656037ull;
    for (std::size_t i = 0; i < size; ++i) {
        hash = (hash ^ data[i]) * 1099511628211ull;
    }
    return hash;
}

// Recast reports through a context; its log is not needed (failures are returned).
class Context final : public rcContext {
public:
    Context() : rcContext(false) {}
};

struct NavMeshDeleter {
    void operator()(dtNavMesh* mesh) const { dtFreeNavMesh(mesh); }
};
struct QueryDeleter {
    void operator()(dtNavMeshQuery* query) const { dtFreeNavMeshQuery(query); }
};

unsigned short flags_of(NavArea area) {
    switch (area) {
        case NavArea::Door: return kFlagDoor;
        case NavArea::Jump: return kFlagJump;
        default: return kFlagWalk;
    }
}

int next_power_of_two(int v) {
    int p = 1;
    while (p < v) {
        p <<= 1;
    }
    return p;
}

int log2_of(int v) {
    int bits = 0;
    while ((1 << bits) < v) {
        ++bits;
    }
    return bits;
}

}  // namespace

struct NavMesh::Impl {
    std::unique_ptr<dtNavMesh, NavMeshDeleter> mesh;
    std::unique_ptr<dtNavMeshQuery, QueryDeleter> query;
    NavAgent agent;
    NavBuildSettings settings;
    std::vector<NavAreaVolume> areas;
    std::vector<NavLink> links;
    std::map<std::uint32_t, Aabb> obstacles;
    std::set<std::uint32_t> disabled_links;
    std::vector<glm::vec3> seeds;  ///< prune(): what must be reachable
    std::uint32_t next_obstacle = 1;
    Aabb bounds;
    int tiles_x = 0;
    int tiles_z = 0;
    int max_polys = 0;
    dtNavMeshParams params{};
    std::uint64_t version = 0;

    // The tiles (x, z) overlapping a box.
    void tile_range(const Aabb& box, int& x0, int& z0, int& x1, int& z1) const {
        const float size = settings.tile_size;
        x0 = std::max(0, static_cast<int>(std::floor((box.min.x - bounds.min.x) / size)));
        z0 = std::max(0, static_cast<int>(std::floor((box.min.z - bounds.min.z) / size)));
        x1 = std::min(tiles_x - 1, static_cast<int>(std::floor((box.max.x - bounds.min.x) / size)));
        z1 = std::min(tiles_z - 1, static_cast<int>(std::floor((box.max.z - bounds.min.z) / size)));
    }

    dtQueryFilter filter_of(const NavFilter& filter) const {
        dtQueryFilter f;
        unsigned short include = kFlagWalk;
        if (filter.doors) {
            include |= kFlagDoor;
        }
        if (filter.jumps) {
            include |= kFlagJump;
        }
        f.setIncludeFlags(include);
        f.setExcludeFlags(kFlagDisabled | kFlagPruned);
        for (std::size_t i = 0; i < kNavAreaCount; ++i) {
            f.setAreaCost(static_cast<int>(i), filter.area_costs[i]);
        }
        return f;
    }

    void init_mesh() {
        mesh.reset(dtAllocNavMesh());
        query.reset(dtAllocNavMeshQuery());
        if (!mesh || !query) {
            throw std::runtime_error("NavMesh: out of memory");
        }
        if (dtStatusFailed(mesh->init(&params))) {
            throw std::runtime_error("NavMesh: Detour refused the mesh's parameters");
        }
        if (dtStatusFailed(query->init(mesh.get(), kMaxPathPolys))) {
            throw std::runtime_error("NavMesh: Detour refused the search's parameters");
        }
    }

    // Opens or closes the links of the tile as disabled_links says.
    void apply_link_states(const dtMeshTile* tile) {
        if (tile == nullptr || tile->header == nullptr) {
            return;
        }
        const dtPolyRef base = mesh->getPolyRefBase(tile);
        for (int i = 0; i < tile->header->offMeshConCount; ++i) {
            const dtOffMeshConnection& link = tile->offMeshCons[i];
            const dtPolyRef ref = base | static_cast<dtPolyRef>(link.poly);
            unsigned short flags = 0;
            mesh->getPolyFlags(ref, &flags);
            flags = disabled_links.contains(link.userId) ? static_cast<unsigned short>(flags | kFlagDisabled)
                                                         : static_cast<unsigned short>(flags & ~kFlagDisabled);
            mesh->setPolyFlags(ref, flags);
        }
    }

    // Builds the tile (tx, tz) from `scene`; returns its polygon count (0: no tile).
    int build_tile(const CollisionScene& scene, int tx, int tz);

    // Marks kFlagPruned on every polygon not reachable from the seeds (none without seeds).
    void apply_prune() {
        const dtNavMesh* nav = mesh.get();
        if (nav == nullptr) {
            return;
        }
        std::set<dtPolyRef> reached;
        std::vector<dtPolyRef> pending;
        if (!seeds.empty()) {
            dtQueryFilter all;  // every polygon, whatever its flags
            all.setIncludeFlags(0xffff);
            all.setExcludeFlags(0);
            for (const glm::vec3& seed : seeds) {
                dtPolyRef ref = 0;
                float nearest[3];
                const float extents[3] = {2.0f, 4.0f, 2.0f};
                if (dtStatusSucceed(query->findNearestPoly(&seed.x, extents, &all, &ref, nearest)) && ref != 0 &&
                    reached.insert(ref).second) {
                    pending.push_back(ref);
                }
            }
            while (!pending.empty()) {
                const dtPolyRef ref = pending.back();
                pending.pop_back();
                const dtMeshTile* tile = nullptr;
                const dtPoly* poly = nullptr;
                nav->getTileAndPolyByRefUnsafe(ref, &tile, &poly);
                for (unsigned int l = poly->firstLink; l != DT_NULL_LINK; l = tile->links[l].next) {
                    const dtPolyRef next = tile->links[l].ref;
                    if (next != 0 && reached.insert(next).second) {
                        pending.push_back(next);
                    }
                }
            }
        }
        for (int i = 0; i < nav->getMaxTiles(); ++i) {
            const dtMeshTile* tile = nav->getTile(i);
            if (tile == nullptr || tile->header == nullptr) {
                continue;
            }
            const dtPolyRef base = nav->getPolyRefBase(tile);
            for (int p = 0; p < tile->header->polyCount; ++p) {
                const dtPolyRef ref = base | static_cast<dtPolyRef>(p);
                unsigned short flags = 0;
                mesh->getPolyFlags(ref, &flags);
                const bool keep = seeds.empty() || reached.contains(ref);
                flags = keep ? static_cast<unsigned short>(flags & ~kFlagPruned) : static_cast<unsigned short>(flags | kFlagPruned);
                mesh->setPolyFlags(ref, flags);
            }
        }
    }
};

int NavMesh::Impl::build_tile(const CollisionScene& scene, int tx, int tz) {
    // Whatever was there goes first.
    if (const dtTileRef old = mesh->getTileRefAt(tx, tz, 0)) {
        mesh->removeTile(old, nullptr, nullptr);
    }
    const float cs = settings.cell_size;
    const float ch = settings.cell_height;
    rcConfig cfg{};
    cfg.cs = cs;
    cfg.ch = ch;
    // Recast takes an angle; the engine keeps a cosine (build time: not the logic).
    cfg.walkableSlopeAngle = std::acos(std::clamp(agent.min_ground_normal_y, 0.0f, 1.0f)) * 57.2957795f;
    cfg.walkableHeight = static_cast<int>(std::ceil(agent.height / ch));
    cfg.walkableClimb = static_cast<int>(std::floor(agent.max_climb / ch));
    cfg.walkableRadius = static_cast<int>(std::ceil(agent.radius / cs));
    cfg.maxEdgeLen = static_cast<int>(settings.edge_max_length / cs);
    cfg.maxSimplificationError = settings.edge_max_error;
    cfg.minRegionArea = static_cast<int>(settings.region_min_size / (cs * cs));
    cfg.mergeRegionArea = static_cast<int>(settings.region_merge_size / (cs * cs));
    cfg.maxVertsPerPoly = DT_VERTS_PER_POLYGON;
    cfg.tileSize = static_cast<int>(settings.tile_size / cs);
    cfg.borderSize = cfg.walkableRadius + 3;
    cfg.width = cfg.tileSize + cfg.borderSize * 2;
    cfg.height = cfg.tileSize + cfg.borderSize * 2;
    cfg.detailSampleDist = settings.detail_sample_distance < 0.9f ? 0.0f : cs * settings.detail_sample_distance;
    cfg.detailSampleMaxError = ch * settings.detail_sample_max_error;
    const float tile_world = static_cast<float>(cfg.tileSize) * cs;
    cfg.bmin[0] = bounds.min.x + static_cast<float>(tx) * tile_world - static_cast<float>(cfg.borderSize) * cs;
    cfg.bmin[1] = bounds.min.y;
    cfg.bmin[2] = bounds.min.z + static_cast<float>(tz) * tile_world - static_cast<float>(cfg.borderSize) * cs;
    cfg.bmax[0] = bounds.min.x + static_cast<float>(tx + 1) * tile_world + static_cast<float>(cfg.borderSize) * cs;
    cfg.bmax[1] = bounds.max.y;
    cfg.bmax[2] = bounds.min.z + static_cast<float>(tz + 1) * tile_world + static_cast<float>(cfg.borderSize) * cs;

    // The level's triangles in the tile (with its border): walkable ground, and what blocks.
    Aabb box;
    box.add({cfg.bmin[0], cfg.bmin[1], cfg.bmin[2]});
    box.add({cfg.bmax[0], cfg.bmax[1], cfg.bmax[2]});
    std::vector<std::uint32_t> found;
    scene.box_triangles(box, kSurfaceSolid, found);
    if (found.empty()) {
        return 0;
    }
    std::vector<float> vertices;
    std::vector<unsigned char> triangle_areas;
    vertices.reserve(found.size() * 9);
    triangle_areas.reserve(found.size());
    for (const std::uint32_t index : found) {
        const CollisionTriangle& t = scene.triangles()[index];
        for (const glm::vec3& p : {t.a, t.b, t.c}) {
            vertices.insert(vertices.end(), {p.x, p.y, p.z});
        }
        const bool ground = t.kind == SurfaceKind::Walkable && t.normal.y >= agent.min_ground_normal_y;
        triangle_areas.push_back(ground ? static_cast<unsigned char>(NavArea::Ground) : RC_NULL_AREA);
    }

    Context context;
    struct Heightfield {
        rcHeightfield* p = rcAllocHeightfield();
        ~Heightfield() { rcFreeHeightField(p); }
    } solid;
    struct Compact {
        rcCompactHeightfield* p = rcAllocCompactHeightfield();
        ~Compact() { rcFreeCompactHeightfield(p); }
    } compact;
    struct Contours {
        rcContourSet* p = rcAllocContourSet();
        ~Contours() { rcFreeContourSet(p); }
    } contours;
    struct Poly {
        rcPolyMesh* p = rcAllocPolyMesh();
        ~Poly() { rcFreePolyMesh(p); }
    } poly;
    struct Detail {
        rcPolyMeshDetail* p = rcAllocPolyMeshDetail();
        ~Detail() { rcFreePolyMeshDetail(p); }
    } detail;
    if (!solid.p || !compact.p || !contours.p || !poly.p || !detail.p) {
        throw std::runtime_error("NavMesh: out of memory");
    }
    if (!rcCreateHeightfield(&context, *solid.p, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch)) {
        throw std::runtime_error("NavMesh: cannot create a heightfield");
    }
    if (!rcRasterizeTriangles(&context, vertices.data(), triangle_areas.data(), static_cast<int>(triangle_areas.size()),
                              *solid.p, cfg.walkableClimb)) {
        throw std::runtime_error("NavMesh: cannot rasterize the level");
    }
    rcFilterLowHangingWalkableObstacles(&context, cfg.walkableClimb, *solid.p);
    rcFilterLedgeSpans(&context, cfg.walkableHeight, cfg.walkableClimb, *solid.p);
    rcFilterWalkableLowHeightSpans(&context, cfg.walkableHeight, *solid.p);
    if (!rcBuildCompactHeightfield(&context, cfg.walkableHeight, cfg.walkableClimb, *solid.p, *compact.p)) {
        throw std::runtime_error("NavMesh: cannot build the compact heightfield");
    }
    if (!rcErodeWalkableArea(&context, cfg.walkableRadius, *compact.p)) {
        throw std::runtime_error("NavMesh: cannot erode by the agent's radius");
    }
    for (const NavAreaVolume& volume : areas) {
        const float bmin[3] = {volume.box.min.x, volume.box.min.y, volume.box.min.z};
        const float bmax[3] = {volume.box.max.x, volume.box.max.y, volume.box.max.z};
        rcMarkBoxArea(&context, bmin, bmax, static_cast<unsigned char>(volume.area), *compact.p);
    }
    for (const auto& [id, obstacle] : obstacles) {
        const float bmin[3] = {obstacle.min.x, obstacle.min.y, obstacle.min.z};
        const float bmax[3] = {obstacle.max.x, obstacle.max.y, obstacle.max.z};
        rcMarkBoxArea(&context, bmin, bmax, RC_NULL_AREA, *compact.p);
    }
    if (!rcBuildDistanceField(&context, *compact.p) ||
        !rcBuildRegions(&context, *compact.p, cfg.borderSize, cfg.minRegionArea, cfg.mergeRegionArea)) {
        throw std::runtime_error("NavMesh: cannot build the regions");
    }
    if (!rcBuildContours(&context, *compact.p, cfg.maxSimplificationError, cfg.maxEdgeLen, *contours.p)) {
        throw std::runtime_error("NavMesh: cannot build the contours");
    }
    if (contours.p->nconts == 0) {
        return 0;
    }
    if (!rcBuildPolyMesh(&context, *contours.p, cfg.maxVertsPerPoly, *poly.p)) {
        throw std::runtime_error("NavMesh: cannot build the polygons");
    }
    if (!rcBuildPolyMeshDetail(&context, *poly.p, *compact.p, cfg.detailSampleDist, cfg.detailSampleMaxError, *detail.p)) {
        throw std::runtime_error("NavMesh: cannot build the detail mesh");
    }
    if (poly.p->npolys == 0) {
        return 0;
    }
    if (poly.p->npolys > max_polys) {
        throw std::runtime_error("NavMesh: a tile has " + std::to_string(poly.p->npolys) + " polygons, more than " +
                                 std::to_string(max_polys) + ": make the tiles smaller");
    }
    for (int i = 0; i < poly.p->npolys; ++i) {
        poly.p->flags[i] = flags_of(static_cast<NavArea>(poly.p->areas[i]));
    }

    // The links that start in this tile (Detour keeps a link in the tile of its start).
    std::vector<float> link_vertices;
    std::vector<float> link_radii;
    std::vector<unsigned short> link_flags;
    std::vector<unsigned char> link_areas;
    std::vector<unsigned char> link_directions;
    std::vector<unsigned int> link_ids;
    const float x0 = bounds.min.x + static_cast<float>(tx) * tile_world;
    const float z0 = bounds.min.z + static_cast<float>(tz) * tile_world;
    for (const NavLink& link : links) {
        if (link.from.x < x0 || link.from.x >= x0 + tile_world || link.from.z < z0 || link.from.z >= z0 + tile_world) {
            continue;
        }
        link_vertices.insert(link_vertices.end(), {link.from.x, link.from.y, link.from.z, link.to.x, link.to.y, link.to.z});
        link_radii.push_back(link.radius);
        link_flags.push_back(flags_of(link.area));
        link_areas.push_back(static_cast<unsigned char>(link.area));
        link_directions.push_back(link.bidirectional ? DT_OFFMESH_CON_BIDIR : 0);
        link_ids.push_back(link.id);
    }

    dtNavMeshCreateParams params_tile{};
    params_tile.verts = poly.p->verts;
    params_tile.vertCount = poly.p->nverts;
    params_tile.polys = poly.p->polys;
    params_tile.polyAreas = poly.p->areas;
    params_tile.polyFlags = poly.p->flags;
    params_tile.polyCount = poly.p->npolys;
    params_tile.nvp = poly.p->nvp;
    params_tile.detailMeshes = detail.p->meshes;
    params_tile.detailVerts = detail.p->verts;
    params_tile.detailVertsCount = detail.p->nverts;
    params_tile.detailTris = detail.p->tris;
    params_tile.detailTriCount = detail.p->ntris;
    params_tile.offMeshConVerts = link_vertices.empty() ? nullptr : link_vertices.data();
    params_tile.offMeshConRad = link_radii.empty() ? nullptr : link_radii.data();
    params_tile.offMeshConFlags = link_flags.empty() ? nullptr : link_flags.data();
    params_tile.offMeshConAreas = link_areas.empty() ? nullptr : link_areas.data();
    params_tile.offMeshConDir = link_directions.empty() ? nullptr : link_directions.data();
    params_tile.offMeshConUserID = link_ids.empty() ? nullptr : link_ids.data();
    params_tile.offMeshConCount = static_cast<int>(link_radii.size());
    params_tile.walkableHeight = agent.height;
    params_tile.walkableRadius = agent.radius;
    params_tile.walkableClimb = agent.max_climb;
    params_tile.tileX = tx;
    params_tile.tileY = tz;
    params_tile.tileLayer = 0;
    rcVcopy(params_tile.bmin, poly.p->bmin);
    rcVcopy(params_tile.bmax, poly.p->bmax);
    params_tile.cs = cfg.cs;
    params_tile.ch = cfg.ch;
    params_tile.buildBvTree = true;
    unsigned char* data = nullptr;
    int size = 0;
    if (!dtCreateNavMeshData(&params_tile, &data, &size)) {
        throw std::runtime_error("NavMesh: Detour cannot make the tile's data");
    }
    if (dtStatusFailed(mesh->addTile(data, size, DT_TILE_FREE_DATA, 0, nullptr))) {
        dtFree(data);
        throw std::runtime_error("NavMesh: Detour refused a tile");
    }
    apply_link_states(mesh->getTileAt(tx, tz, 0));
    return poly.p->npolys;
}

NavMesh::NavMesh() : impl_(std::make_unique<Impl>()) {}
NavMesh::~NavMesh() = default;
NavMesh::NavMesh(NavMesh&&) noexcept = default;
NavMesh& NavMesh::operator=(NavMesh&&) noexcept = default;

NavBuildStats NavMesh::build(const CollisionScene& scene, const NavAgent& agent, const NavBuildSettings& settings,
                             std::span<const NavAreaVolume> areas, std::span<const NavLink> links) {
    MOTEUR_PROFILE("navigation: build");
    const Uint64 start = SDL_GetPerformanceCounter();
    Impl& m = *impl_;
    m.mesh.reset();
    m.query.reset();
    m.agent = agent;
    m.settings = settings;
    m.areas.assign(areas.begin(), areas.end());
    m.links.assign(links.begin(), links.end());
    m.bounds = scene.bounds();
    if (m.bounds.empty()) {
        throw std::runtime_error("NavMesh: the collision scene is empty (or not built)");
    }
    const float tile_world = static_cast<float>(static_cast<int>(settings.tile_size / settings.cell_size)) * settings.cell_size;
    NavTiling tiling;
    tiling.agent = agent;
    tiling.settings = settings;
    tiling.origin = m.bounds.min;
    tiling.tiles_x = std::max(1, static_cast<int>(std::ceil((m.bounds.max.x - m.bounds.min.x) / tile_world)));
    tiling.tiles_z = std::max(1, static_cast<int>(std::ceil((m.bounds.max.z - m.bounds.min.z) / tile_world)));
    const Aabb level_bounds = m.bounds;
    start_tiled(tiling);
    m.bounds = level_bounds;
    NavBuildStats stats;
    for (int z = 0; z < m.tiles_z; ++z) {
        for (int x = 0; x < m.tiles_x; ++x) {
            const int polygons = m.build_tile(scene, x, z);
            if (polygons > 0) {
                ++stats.tiles;
                stats.polygons += polygons;
            }
        }
    }
    m.apply_prune();
    ++m.version;
    stats.milliseconds = static_cast<double>(SDL_GetPerformanceCounter() - start) * 1000.0 /
                         static_cast<double>(SDL_GetPerformanceFrequency());
    return stats;
}

void NavMesh::start_tiled(const NavTiling& tiling) {
    Impl& m = *impl_;
    m.mesh.reset();
    m.query.reset();
    m.agent = tiling.agent;
    m.settings = tiling.settings;
    const float tile_world =
        static_cast<float>(static_cast<int>(tiling.settings.tile_size / tiling.settings.cell_size)) * tiling.settings.cell_size;
    m.settings.tile_size = tile_world;
    m.tiles_x = std::max(1, tiling.tiles_x);
    m.tiles_z = std::max(1, tiling.tiles_z);
    if (static_cast<long long>(m.tiles_x) * m.tiles_z > (1 << 22)) {
        throw std::runtime_error("NavMesh: the level needs too many tiles: make them larger");
    }
    // The bounds the tiles cover (heights: generous; build() sets the scene's).
    m.bounds = Aabb{};
    m.bounds.add(tiling.origin);
    m.bounds.add(tiling.origin + glm::vec3(static_cast<float>(m.tiles_x) * tile_world, 1000.0f,
                                           static_cast<float>(m.tiles_z) * tile_world));
    // Detour's 32-bit references: the tiles' bits, the polygons' bits, at least 10 for the salt.
    const int tile_bits = std::min(log2_of(next_power_of_two(m.tiles_x * m.tiles_z)), 14);
    const int poly_bits = 22 - tile_bits;
    m.max_polys = 1 << poly_bits;
    m.params = {};
    m.params.orig[0] = tiling.origin.x;
    m.params.orig[1] = tiling.origin.y;
    m.params.orig[2] = tiling.origin.z;
    m.params.tileWidth = tile_world;
    m.params.tileHeight = tile_world;
    m.params.maxTiles = 1 << tile_bits;
    m.params.maxPolys = m.max_polys;
    m.init_mesh();
    ++m.version;
}

NavTiling NavMesh::tiling() const {
    NavTiling tiling;
    tiling.agent = impl_->agent;
    tiling.settings = impl_->settings;
    tiling.origin = glm::vec3(impl_->params.orig[0], impl_->params.orig[1], impl_->params.orig[2]);
    tiling.tiles_x = impl_->tiles_x;
    tiling.tiles_z = impl_->tiles_z;
    return tiling;
}

NavBuildStats NavMesh::build_tiled(const CollisionScene& scene, const NavTiling& tiling, std::span<const NavAreaVolume> areas,
                                   std::span<const NavLink> links) {
    MOTEUR_PROFILE("navigation: build");
    const Uint64 start = SDL_GetPerformanceCounter();
    Impl& m = *impl_;
    if (scene.bounds().empty()) {
        throw std::runtime_error("NavMesh: the collision scene is empty (or not built)");
    }
    start_tiled(tiling);
    m.areas.assign(areas.begin(), areas.end());
    m.links.assign(links.begin(), links.end());
    // Heights from the scene (Recast's voxels span them), the tiles from the tiling.
    m.bounds.min.y = scene.bounds().min.y;
    m.bounds.max.y = scene.bounds().max.y;
    NavBuildStats stats;
    for (int z = 0; z < m.tiles_z; ++z) {
        for (int x = 0; x < m.tiles_x; ++x) {
            const int polygons = m.build_tile(scene, x, z);
            if (polygons > 0) {
                ++stats.tiles;
                stats.polygons += polygons;
            }
        }
    }
    m.apply_prune();
    ++m.version;
    stats.milliseconds = static_cast<double>(SDL_GetPerformanceCounter() - start) * 1000.0 /
                         static_cast<double>(SDL_GetPerformanceFrequency());
    return stats;
}

std::vector<std::uint8_t> NavMesh::tile_data(int x, int z) const {
    if (!impl_->mesh) {
        return {};
    }
    const dtMeshTile* tile = static_cast<const dtNavMesh*>(impl_->mesh.get())->getTileAt(x, z, 0);
    if (tile == nullptr || tile->header == nullptr || tile->dataSize <= 0) {
        return {};
    }
    // Its flags as they are now (pruned polygons, shut doors) travel with it.
    return std::vector<std::uint8_t>(tile->data, tile->data + tile->dataSize);
}

void NavMesh::add_tile(const std::vector<std::uint8_t>& data, const std::string& name) {
    Impl& m = *impl_;
    if (!m.mesh) {
        throw std::runtime_error(name + ": no tiled navigation mesh to add a tile to");
    }
    if (data.size() < sizeof(dtMeshHeader)) {
        throw std::runtime_error(name + ": a navigation tile is too small");
    }
    dtMeshHeader header;
    std::memcpy(&header, data.data(), sizeof(header));
    if (const dtTileRef old = m.mesh->getTileRefAt(header.x, header.y, header.layer)) {
        m.mesh->removeTile(old, nullptr, nullptr);
    }
    auto* copy = static_cast<unsigned char*>(dtAlloc(data.size(), DT_ALLOC_PERM));
    std::memcpy(copy, data.data(), data.size());
    if (dtStatusFailed(m.mesh->addTile(copy, static_cast<int>(data.size()), DT_TILE_FREE_DATA, 0, nullptr))) {
        dtFree(copy);
        throw std::runtime_error(name + ": Detour refused a navigation tile");
    }
    m.apply_link_states(m.mesh->getTileAt(header.x, header.y, header.layer));
    ++m.version;
}

void NavMesh::remove_tile(int x, int z) {
    Impl& m = *impl_;
    if (!m.mesh) {
        return;
    }
    if (const dtTileRef ref = m.mesh->getTileRefAt(x, z, 0)) {
        m.mesh->removeTile(ref, nullptr, nullptr);
        ++m.version;
    }
}

NavBuildStats NavMesh::rebuild(const CollisionScene& scene, const Aabb& area) {
    MOTEUR_PROFILE("navigation: rebuild");
    const Uint64 start = SDL_GetPerformanceCounter();
    Impl& m = *impl_;
    NavBuildStats stats;
    if (!m.mesh || area.empty()) {
        return stats;
    }
    int x0, z0, x1, z1;
    m.tile_range(area, x0, z0, x1, z1);
    for (int z = z0; z <= z1; ++z) {
        for (int x = x0; x <= x1; ++x) {
            const int polygons = m.build_tile(scene, x, z);
            if (polygons > 0) {
                ++stats.tiles;
                stats.polygons += polygons;
            }
        }
    }
    m.apply_prune();
    ++m.version;
    stats.milliseconds = static_cast<double>(SDL_GetPerformanceCounter() - start) * 1000.0 /
                         static_cast<double>(SDL_GetPerformanceFrequency());
    return stats;
}

std::uint32_t NavMesh::add_obstacle(const CollisionScene& scene, const Aabb& box) {
    const std::uint32_t id = impl_->next_obstacle++;
    impl_->obstacles[id] = box;
    Aabb grown = box;
    grown.add(box.min - glm::vec3(impl_->agent.radius));
    grown.add(box.max + glm::vec3(impl_->agent.radius));
    rebuild(scene, grown);
    return id;
}

void NavMesh::remove_obstacle(const CollisionScene& scene, std::uint32_t id) {
    const auto found = impl_->obstacles.find(id);
    if (found == impl_->obstacles.end()) {
        return;
    }
    Aabb grown = found->second;
    grown.add(found->second.min - glm::vec3(impl_->agent.radius));
    grown.add(found->second.max + glm::vec3(impl_->agent.radius));
    impl_->obstacles.erase(found);
    rebuild(scene, grown);
}

void NavMesh::prune(std::span<const glm::vec3> seeds) {
    impl_->seeds.assign(seeds.begin(), seeds.end());
    impl_->apply_prune();
    ++impl_->version;
}

void NavMesh::set_link_enabled(std::uint32_t id, bool enabled) {
    Impl& m = *impl_;
    if (enabled) {
        m.disabled_links.erase(id);
    } else {
        m.disabled_links.insert(id);
    }
    if (!m.mesh) {
        return;
    }
    for (int i = 0; i < m.mesh->getMaxTiles(); ++i) {
        m.apply_link_states(static_cast<const dtNavMesh*>(m.mesh.get())->getTile(i));
    }
}

bool NavMesh::empty() const {
    return !impl_->mesh;
}

std::uint64_t NavMesh::version() const {
    return impl_->version;
}

const NavAgent& NavMesh::agent() const {
    return impl_->agent;
}

const Aabb& NavMesh::bounds() const {
    return impl_->bounds;
}

int NavMesh::tile_count() const {
    int count = 0;
    if (impl_->mesh) {
        const dtNavMesh* mesh = impl_->mesh.get();
        for (int i = 0; i < mesh->getMaxTiles(); ++i) {
            const dtMeshTile* tile = mesh->getTile(i);
            count += tile != nullptr && tile->header != nullptr ? 1 : 0;
        }
    }
    return count;
}

int NavMesh::polygon_count() const {
    int count = 0;
    if (impl_->mesh) {
        const dtNavMesh* mesh = impl_->mesh.get();
        for (int i = 0; i < mesh->getMaxTiles(); ++i) {
            const dtMeshTile* tile = mesh->getTile(i);
            count += tile != nullptr && tile->header != nullptr ? tile->header->polyCount : 0;
        }
    }
    return count;
}

NavPolyRef NavMesh::nearest_polygon(glm::vec3 point, glm::vec3 half_extents, glm::vec3* nearest, const NavFilter& filter) const {
    if (!impl_->mesh) {
        return 0;
    }
    const dtQueryFilter f = impl_->filter_of(filter);
    dtPolyRef ref = 0;
    float at[3] = {point.x, point.y, point.z};
    float out[3];
    if (dtStatusFailed(impl_->query->findNearestPoly(at, &half_extents.x, &f, &ref, out)) || ref == 0) {
        return 0;
    }
    if (nearest != nullptr) {
        *nearest = {out[0], out[1], out[2]};
    }
    return ref;
}

NavPolyRef NavMesh::locate(glm::vec3 position, NavPolyRef hint) const {
    if (!impl_->mesh) {
        return 0;
    }
    if (hint != 0 && impl_->mesh->isValidPolyRef(static_cast<dtPolyRef>(hint))) {
        float closest[3];
        bool over = false;
        if (dtStatusSucceed(impl_->query->closestPointOnPoly(static_cast<dtPolyRef>(hint), &position.x, closest, &over)) &&
            over && std::fabs(closest[1] - position.y) < 1.0f) {
            return hint;
        }
    }
    return nearest_polygon(position, {0.5f, 1.0f, 0.5f});
}

std::optional<glm::vec3> NavMesh::nearest_point(glm::vec3 point, glm::vec3 half_extents, const NavFilter& filter) const {
    glm::vec3 nearest;
    if (nearest_polygon(point, half_extents, &nearest, filter) == 0) {
        return std::nullopt;
    }
    return nearest;
}

NavPath NavMesh::find_path(glm::vec3 from, glm::vec3 to, const NavFilter& filter, int max_nodes) const {
    MOTEUR_PROFILE("navigation: path");
    NavPath result;
    if (!impl_->mesh) {
        return result;
    }
    glm::vec3 start, end;
    const auto start_ref = static_cast<dtPolyRef>(nearest_polygon(from, kPathExtents, &start, filter));
    if (start_ref == 0) {
        return result;
    }
    auto end_ref = static_cast<dtPolyRef>(nearest_polygon(to, kPathExtents, &end, filter));
    const dtQueryFilter f = impl_->filter_of(filter);
    std::vector<dtPolyRef> polys(static_cast<std::size_t>(std::clamp(max_nodes, 1, kMaxPathPolys)));
    int count = 0;
    bool reached = end_ref != 0;
    if (end_ref == 0) {
        // The goal is off the mesh: towards the nearest point within a wider box.
        end_ref = static_cast<dtPolyRef>(nearest_polygon(to, kPathExtents * 4.0f, &end, filter));
        if (end_ref == 0) {
            return result;
        }
    }
    const dtStatus status = impl_->query->findPath(start_ref, end_ref, &start.x, &end.x, &f, polys.data(), &count,
                                                    static_cast<int>(polys.size()));
    if (dtStatusFailed(status) || count == 0) {
        return result;
    }
    result.expanded = count;
    if (polys[static_cast<std::size_t>(count - 1)] != end_ref || dtStatusDetail(status, DT_PARTIAL_RESULT)) {
        reached = false;
        float closest[3];
        impl_->query->closestPointOnPoly(polys[static_cast<std::size_t>(count - 1)], &end.x, closest, nullptr);
        end = {closest[0], closest[1], closest[2]};
    }
    float straight[kMaxStraight * 3];
    int straight_count = 0;
    impl_->query->findStraightPath(&start.x, &end.x, polys.data(), count, straight, nullptr, nullptr, &straight_count,
                                   kMaxStraight);
    for (int i = 1; i < straight_count; ++i) {  // the first corner is the start
        result.points.emplace_back(straight[i * 3], straight[i * 3 + 1], straight[i * 3 + 2]);
    }
    // Reached: the goal itself, not a point under it on another floor (within a step and a half).
    const glm::vec3 d = end - to;
    reached = reached && glm::dot(glm::vec2(d.x, d.z), glm::vec2(d.x, d.z)) < 0.01f &&
              std::fabs(d.y) <= impl_->agent.max_climb * 1.5f + impl_->settings.cell_height;
    result.status = reached ? PathStatus::Found : PathStatus::Partial;
    return result;
}

bool NavMesh::walkable_line(glm::vec3 from, glm::vec3 to, glm::vec3* hit, const NavFilter& filter) const {
    if (!impl_->mesh) {
        return false;
    }
    glm::vec3 start;
    const auto start_ref = static_cast<dtPolyRef>(nearest_polygon(from, {0.5f, 2.0f, 0.5f}, &start, filter));
    if (start_ref == 0) {
        if (hit != nullptr) {
            *hit = from;
        }
        return false;
    }
    const dtQueryFilter f = impl_->filter_of(filter);
    float t = 0.0f;
    float normal[3];
    dtPolyRef polys[64];
    int count = 0;
    impl_->query->raycast(start_ref, &start.x, &to.x, &f, &t, normal, polys, &count, 64);
    if (t == FLT_MAX) {
        return true;
    }
    if (hit != nullptr) {
        *hit = start + (to - start) * t;
    }
    return false;
}

std::optional<float> NavMesh::height_at(glm::vec3 point, float max_distance) const {
    glm::vec3 nearest;
    const NavPolyRef ref = nearest_polygon(point, {0.05f, max_distance, 0.05f}, &nearest);
    if (ref == 0) {
        return std::nullopt;
    }
    float height = 0.0f;
    if (dtStatusFailed(impl_->query->getPolyHeight(static_cast<dtPolyRef>(ref), &nearest.x, &height))) {
        return nearest.y;
    }
    return height;
}

void NavMesh::triangles(const Aabb& area, std::vector<glm::vec3>& corners, std::vector<NavArea>* areas) const {
    corners.clear();
    if (areas != nullptr) {
        areas->clear();
    }
    if (!impl_->mesh) {
        return;
    }
    int x0, z0, x1, z1;
    impl_->tile_range(area, x0, z0, x1, z1);
    const dtNavMesh* mesh = impl_->mesh.get();
    for (int z = z0; z <= z1; ++z) {
        for (int x = x0; x <= x1; ++x) {
            const dtMeshTile* tile = mesh->getTileAt(x, z, 0);
            if (tile == nullptr || tile->header == nullptr) {
                continue;
            }
            for (int i = 0; i < tile->header->polyCount; ++i) {
                const dtPoly& poly = tile->polys[i];
                if (poly.getType() == DT_POLYTYPE_OFFMESH_CONNECTION) {
                    continue;
                }
                const dtPolyDetail& d = tile->detailMeshes[i];
                for (int j = 0; j < d.triCount; ++j) {
                    const unsigned char* t = &tile->detailTris[(d.triBase + static_cast<unsigned int>(j)) * 4];
                    for (int k = 0; k < 3; ++k) {
                        const float* v = t[k] < poly.vertCount ? &tile->verts[poly.verts[t[k]] * 3]
                                                               : &tile->detailVerts[(d.vertBase + t[k] - poly.vertCount) * 3];
                        corners.emplace_back(v[0], v[1], v[2]);
                    }
                    if (areas != nullptr) {
                        areas->push_back(static_cast<NavArea>(poly.getArea()));
                    }
                }
            }
        }
    }
}

std::vector<std::uint8_t> NavMesh::save() const {
    const Impl& m = *impl_;
    if (!m.mesh) {
        throw std::runtime_error("NavMesh: nothing to save");
    }
    BinaryWriter out;
    out.raw(kMagic, 4);
    out.u32(kVersion);
    out.raw(&m.agent, sizeof(m.agent));
    out.raw(&m.settings, sizeof(m.settings));
    out.vec3(m.bounds.min);
    out.vec3(m.bounds.max);
    out.value<std::int32_t>(m.tiles_x);
    out.value<std::int32_t>(m.tiles_z);
    out.value<std::int32_t>(m.max_polys);
    out.raw(&m.params, sizeof(m.params));
    out.u32(static_cast<std::uint32_t>(m.areas.size()));
    for (const NavAreaVolume& volume : m.areas) {
        out.vec3(volume.box.min);
        out.vec3(volume.box.max);
        out.value<std::uint8_t>(static_cast<std::uint8_t>(volume.area));
    }
    out.u32(static_cast<std::uint32_t>(m.links.size()));
    for (const NavLink& link : m.links) {
        out.vec3(link.from);
        out.vec3(link.to);
        out.f32(link.radius);
        out.value<std::uint8_t>(link.bidirectional ? 1 : 0);
        out.value<std::uint8_t>(static_cast<std::uint8_t>(link.area));
        out.u32(link.id);
    }
    out.u32(static_cast<std::uint32_t>(m.obstacles.size()));
    for (const auto& [id, box] : m.obstacles) {
        out.u32(id);
        out.vec3(box.min);
        out.vec3(box.max);
    }
    out.u32(m.next_obstacle);
    out.u32(static_cast<std::uint32_t>(m.disabled_links.size()));
    for (const std::uint32_t id : m.disabled_links) {
        out.u32(id);
    }
    out.u32(static_cast<std::uint32_t>(m.seeds.size()));
    for (const glm::vec3& seed : m.seeds) {
        out.vec3(seed);
    }
    // Detour's tiles, as it stores them (their own header and magic, checked when added).
    const dtNavMesh* mesh = m.mesh.get();
    std::vector<const dtMeshTile*> tiles;
    for (int i = 0; i < mesh->getMaxTiles(); ++i) {
        const dtMeshTile* tile = mesh->getTile(i);
        if (tile != nullptr && tile->header != nullptr && tile->dataSize > 0) {
            tiles.push_back(tile);
        }
    }
    std::sort(tiles.begin(), tiles.end(), [](const dtMeshTile* a, const dtMeshTile* b) {
        return a->header->y != b->header->y ? a->header->y < b->header->y : a->header->x < b->header->x;
    });
    out.u32(static_cast<std::uint32_t>(tiles.size()));
    for (const dtMeshTile* tile : tiles) {
        const std::vector<std::uint8_t> data(tile->data, tile->data + tile->dataSize);
        out.blob(data);
    }
    std::vector<std::uint8_t> bytes = std::move(out.bytes());
    const std::uint64_t checksum = fnv1a(bytes.data() + 8, bytes.size() - 8);
    for (int i = 0; i < 8; ++i) {
        bytes.push_back(static_cast<std::uint8_t>((checksum >> (8 * i)) & 0xFF));
    }
    return bytes;
}

NavMesh NavMesh::load(const std::vector<std::uint8_t>& bytes, const std::string& name) {
    if (bytes.size() < 16 || std::memcmp(bytes.data(), kMagic, 4) != 0) {
        throw std::runtime_error(name + ": not a navigation mesh (MNAV)");
    }
    std::uint64_t checksum = 0;
    for (int i = 0; i < 8; ++i) {
        checksum |= static_cast<std::uint64_t>(bytes[bytes.size() - 8 + static_cast<std::size_t>(i)]) << (8 * i);
    }
    if (fnv1a(bytes.data() + 8, bytes.size() - 16) != checksum) {
        throw std::runtime_error(name + ": damaged navigation mesh (checksum)");
    }
    BinaryReader in(bytes.data(), bytes.size() - 8, name);
    char magic[4];
    in.raw(magic, 4);
    if (const std::uint32_t version = in.u32(); version != kVersion) {
        in.fail("navigation mesh format " + std::to_string(version) + " (expected " + std::to_string(kVersion) + ")");
    }
    NavMesh mesh;
    Impl& m = *mesh.impl_;
    in.raw(&m.agent, sizeof(m.agent));
    in.raw(&m.settings, sizeof(m.settings));
    m.bounds.min = in.vec3();
    m.bounds.max = in.vec3();
    m.tiles_x = in.value<std::int32_t>();
    m.tiles_z = in.value<std::int32_t>();
    m.max_polys = in.value<std::int32_t>();
    in.raw(&m.params, sizeof(m.params));
    if (m.tiles_x <= 0 || m.tiles_z <= 0 || m.max_polys <= 0 || m.params.maxTiles <= 0) {
        in.fail("wrong tiling");
    }
    const std::uint32_t areas = in.count(25);
    for (std::uint32_t i = 0; i < areas; ++i) {
        NavAreaVolume volume;
        volume.box.min = in.vec3();
        volume.box.max = in.vec3();
        volume.area = static_cast<NavArea>(in.value<std::uint8_t>());
        m.areas.push_back(volume);
    }
    const std::uint32_t links = in.count(34);
    for (std::uint32_t i = 0; i < links; ++i) {
        NavLink link;
        link.from = in.vec3();
        link.to = in.vec3();
        link.radius = in.f32();
        link.bidirectional = in.value<std::uint8_t>() != 0;
        link.area = static_cast<NavArea>(in.value<std::uint8_t>());
        link.id = in.u32();
        m.links.push_back(link);
    }
    const std::uint32_t obstacles = in.count(28);
    for (std::uint32_t i = 0; i < obstacles; ++i) {
        const std::uint32_t id = in.u32();
        Aabb box;
        box.min = in.vec3();
        box.max = in.vec3();
        m.obstacles[id] = box;
    }
    m.next_obstacle = in.u32();
    const std::uint32_t disabled = in.count(4);
    for (std::uint32_t i = 0; i < disabled; ++i) {
        m.disabled_links.insert(in.u32());
    }
    const std::uint32_t seeds = in.count(12);
    for (std::uint32_t i = 0; i < seeds; ++i) {
        m.seeds.push_back(in.vec3());
    }
    try {
        m.init_mesh();
    } catch (const std::runtime_error& e) {
        in.fail(e.what());
    }
    const std::uint32_t tiles = in.count(4);
    for (std::uint32_t i = 0; i < tiles; ++i) {
        const std::vector<std::uint8_t> data = in.blob();
        if (data.size() < sizeof(dtMeshHeader)) {
            in.fail("a tile is too small");
        }
        auto* copy = static_cast<unsigned char*>(dtAlloc(data.size(), DT_ALLOC_PERM));
        std::memcpy(copy, data.data(), data.size());
        if (dtStatusFailed(m.mesh->addTile(copy, static_cast<int>(data.size()), DT_TILE_FREE_DATA, 0, nullptr))) {
            dtFree(copy);
            in.fail("Detour refused tile " + std::to_string(i));
        }
    }
    if (in.left() != 0) {
        in.fail("bytes after the end");
    }
    for (int i = 0; i < m.mesh->getMaxTiles(); ++i) {
        m.apply_link_states(static_cast<const dtNavMesh*>(m.mesh.get())->getTile(i));
    }
    m.apply_prune();
    ++m.version;
    return mesh;
}

// --- NavFlowField ------------------------------------------------------------------------------

std::size_t NavFlowField::index_of(NavPolyRef poly) const {
    const std::uint64_t ip = poly & ((1ull << poly_bits_) - 1);
    const std::uint64_t it = (poly >> poly_bits_) & ((1ull << tile_bits_) - 1);
    if (it >= tile_first_.size() || tile_first_[it] < 0 || ip >= static_cast<std::uint64_t>(tile_count_[it])) {
        return SIZE_MAX;
    }
    return static_cast<std::size_t>(tile_first_[it]) + static_cast<std::size_t>(ip);
}

void NavFlowField::compute(const NavMesh& mesh, glm::vec3 target, float max_cost, const NavFilter& filter) {
    MOTEUR_PROFILE("navigation: flow field");
    costs_.clear();
    target_ = target;
    version_ = mesh.version();
    if (mesh.empty()) {
        return;
    }
    const NavMesh::Impl& m = mesh.impl();
    const dtNavMesh* nav = m.mesh.get();
    target_poly_ = mesh.nearest_polygon(target, kPathExtents, &target_, filter);
    if (target_poly_ == 0) {
        return;
    }
    poly_bits_ = static_cast<std::uint32_t>(log2_of(m.max_polys));
    tile_bits_ = static_cast<std::uint32_t>(log2_of(m.params.maxTiles));
    // One entry per real polygon: the tiles' polygons one after another.
    tile_first_.assign(static_cast<std::size_t>(nav->getMaxTiles()), -1);
    tile_count_.assign(static_cast<std::size_t>(nav->getMaxTiles()), 0);
    std::int32_t total = 0;
    for (int i = 0; i < nav->getMaxTiles(); ++i) {
        const dtMeshTile* tile = nav->getTile(i);
        if (tile != nullptr && tile->header != nullptr) {
            tile_first_[static_cast<std::size_t>(i)] = total;
            tile_count_[static_cast<std::size_t>(i)] = tile->header->polyCount;
            total += tile->header->polyCount;
        }
    }
    costs_.assign(static_cast<std::size_t>(total), Entry{});
    const dtQueryFilter f = m.filter_of(filter);

    // Dijkstra from the target over the polygons, by their centres; ties by reference.
    using Item = std::pair<float, NavPolyRef>;
    std::priority_queue<Item, std::vector<Item>, std::greater<>> open;
    const auto centre_of = [nav](NavPolyRef ref) {
        const dtMeshTile* tile = nullptr;
        const dtPoly* poly = nullptr;
        nav->getTileAndPolyByRefUnsafe(static_cast<dtPolyRef>(ref), &tile, &poly);
        glm::vec3 c(0.0f);
        for (int i = 0; i < poly->vertCount; ++i) {
            const float* v = &tile->verts[poly->verts[i] * 3];
            c += glm::vec3(v[0], v[1], v[2]);
        }
        return c / static_cast<float>(poly->vertCount);
    };
    costs_[index_of(target_poly_)] = {0.0f, target_, target_};
    open.emplace(0.0f, target_poly_);
    while (!open.empty()) {
        const auto [cost, ref] = open.top();
        open.pop();
        if (cost > costs_[index_of(ref)].cost) {
            continue;  // already reached cheaper
        }
        const dtMeshTile* tile = nullptr;
        const dtPoly* poly = nullptr;
        nav->getTileAndPolyByRefUnsafe(static_cast<dtPolyRef>(ref), &tile, &poly);
        const glm::vec3 here = ref == target_poly_ ? target_ : centre_of(ref);
        for (unsigned int l = poly->firstLink; l != DT_NULL_LINK; l = tile->links[l].next) {
            const dtLink& link = tile->links[l];
            const dtPolyRef next = link.ref;
            if (next == 0) {
                continue;
            }
            const dtMeshTile* next_tile = nullptr;
            const dtPoly* next_poly = nullptr;
            nav->getTileAndPolyByRefUnsafe(next, &next_tile, &next_poly);
            if (next_poly->getType() == DT_POLYTYPE_OFFMESH_CONNECTION || !f.passFilter(next, next_tile, next_poly)) {
                continue;
            }
            // The portal: the edge of `poly` shared with `next` (a part of it at a tile border).
            const float* v0 = &tile->verts[poly->verts[link.edge] * 3];
            const float* v1 = &tile->verts[poly->verts[(link.edge + 1) % poly->vertCount] * 3];
            glm::vec3 a(v0[0], v0[1], v0[2]);
            glm::vec3 b(v1[0], v1[1], v1[2]);
            if (link.side != 0xff) {
                const glm::vec3 d = b - a;
                const float s0 = static_cast<float>(link.bmin) / 255.0f;
                const float s1 = static_cast<float>(link.bmax) / 255.0f;
                b = a + d * s1;
                a = a + d * s0;
            }
            const glm::vec3 centre = centre_of(next);
            const float step = glm::length(centre - here) * f.getAreaCost(next_poly->getArea());
            const float reached = cost + step;
            const std::size_t at = index_of(next);
            if (at == SIZE_MAX) {
                continue;
            }
            Entry& entry = costs_[at];
            if (reached > max_cost || (entry.cost >= 0.0f && reached >= entry.cost)) {
                continue;
            }
            // `next` goes through this portal towards the target.
            entry.cost = reached;
            entry.a = a;
            entry.b = b;
            open.emplace(reached, next);
        }
    }
}

glm::vec2 NavFlowField::direction(glm::vec3 position, NavPolyRef poly) const {
    if (costs_.empty() || poly == 0) {
        return glm::vec2(0.0f);
    }
    const std::size_t index = index_of(poly);
    if (index == SIZE_MAX || costs_[index].cost < 0.0f) {
        return glm::vec2(0.0f);
    }
    glm::vec3 aim = target_;
    if (poly != target_poly_) {
        // The point of the portal nearest the mover, kept 10 % in from its ends: a turn is cut,
        // the corner of the wall is not touched.
        const Entry& entry = costs_[index];
        const glm::vec3 a = entry.a + (entry.b - entry.a) * 0.1f;
        const glm::vec3 b = entry.b + (entry.a - entry.b) * 0.1f;
        const glm::vec2 ab(b.x - a.x, b.z - a.z);
        const float length2 = glm::dot(ab, ab);
        const float t = length2 > 0.0f
                            ? std::clamp(glm::dot(glm::vec2(position.x - a.x, position.z - a.z), ab) / length2, 0.0f, 1.0f)
                            : 0.5f;
        aim = a + (b - a) * t;
    }
    const glm::vec2 d(aim.x - position.x, aim.z - position.z);
    const float length = std::sqrt(glm::dot(d, d));
    return length > 1e-5f ? d / length : glm::vec2(0.0f);
}

float NavFlowField::cost(NavPolyRef poly) const {
    if (costs_.empty() || poly == 0) {
        return -1.0f;
    }
    const std::size_t index = index_of(poly);
    return index == SIZE_MAX ? -1.0f : costs_[index].cost;
}

// --- Views and tools ---------------------------------------------------------------------------

NavGrid nav_grid_view(const NavMesh& mesh, glm::vec2 origin, int width, int height, float level, float tolerance) {
    NavGrid grid(width, height);
    for (int j = 0; j < height; ++j) {
        for (int i = 0; i < width; ++i) {
            const glm::vec3 centre(origin.x + static_cast<float>(i) + 0.5f, level, origin.y + static_cast<float>(j) + 0.5f);
            // Detour gives the nearest polygon whose box meets the query's: its nearest point may lie
            // outside the cell, or on another floor.
            glm::vec3 nearest;
            const bool walkable = mesh.nearest_polygon(centre, {0.45f, tolerance, 0.45f}, &nearest) != 0 &&
                                  std::fabs(nearest.x - centre.x) <= 0.5f && std::fabs(nearest.z - centre.z) <= 0.5f &&
                                  std::fabs(nearest.y - level) <= tolerance;
            grid.set({i, j}, walkable, !walkable);
        }
    }
    return grid;
}

void draw_nav_mesh(DebugLineBuffer& lines, const NavMesh& mesh, const Aabb& area) {
    std::vector<glm::vec3> corners;
    std::vector<NavArea> areas;
    mesh.triangles(area, corners, &areas);
    const glm::vec3 lift(0.0f, 0.03f, 0.0f);
    for (std::size_t t = 0; t + 2 < corners.size(); t += 3) {
        const NavArea a = areas[t / 3];
        const glm::vec4 color = a == NavArea::Water ? glm::vec4(0.2f, 0.5f, 1.0f, 1.0f)
                                : a == NavArea::Fire ? glm::vec4(1.0f, 0.4f, 0.1f, 1.0f)
                                                     : glm::vec4(0.2f, 0.8f, 1.0f, 1.0f);
        lines.line(corners[t] + lift, corners[t + 1] + lift, color);
        lines.line(corners[t + 1] + lift, corners[t + 2] + lift, color);
        lines.line(corners[t + 2] + lift, corners[t] + lift, color);
    }
}

}  // namespace moteur
