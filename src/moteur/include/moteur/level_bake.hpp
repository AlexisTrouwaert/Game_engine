#pragma once

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "moteur/collision_scene.hpp"
#include "moteur/level.hpp"
#include "moteur/navigation.hpp"

namespace moteur {

class Prefab;

// Preparing a level (milestone 9, part 8): from its source document, the static collision of every
// placed prefab (its entities' StaticCollision, with their models' collision), cut along a regular
// grid of chunks; the navigation meshes of the whole level, built once and kept tile by tile (a tile
// is a chunk, so the tiles of neighbouring chunks are already joined); and the instances of each
// chunk. Written as a header (`.mlvl`) and one file per chunk (`.mlvc`): only the chunks around the
// hero are read (level_streamer.hpp). The preparation (cook) and the editor bake; the development
// build bakes in memory when no prepared form is there.

/// Where the baker finds what a level refers to.
struct LevelSources {
    /// The prefab of an asset key (loaded, parsed); throws std::runtime_error when it cannot.
    std::function<std::shared_ptr<const Prefab>(const std::string& key)> prefab;
    /// The collision geometry of a model, by its asset key (Model::collision, or the import's); null
    /// for none.
    std::function<const CollisionMeshData*(const std::string& key)> model_collision;
    /// An asset reference of a component ({"id", "path"} or a path) as a key; throws for a bad one.
    std::function<std::string(const nlohmann::json& reference)> resolve;
};

/// An instance as a chunk holds it: what the streamer instantiates.
struct ChunkInstance {
    std::uint32_t id = 0;       ///< LevelInstance::id; 0 for the ground's tiles (not saved, never changed)
    std::string prefab;         ///< asset key
    Transform transform;  ///< where its root goes
    nlohmann::json overrides = nlohmann::json::object();  ///< PrefabPlacement::overrides
    nlohmann::json properties = nlohmann::json::object();  ///< LevelInstance::properties (the game's)
};

/// One chunk of a prepared level.
struct LevelChunk {
    glm::ivec2 coord{0};                     ///< in the grid (0 .. chunks - 1)
    std::vector<ChunkInstance> instances;    ///< whose root is in the chunk
    CollisionMeshData collision;             ///< world space: the triangles whose centre is in the chunk
    std::vector<std::vector<std::uint8_t>> nav_tiles;  ///< one per agent (NavMesh::tile_data()), empty: none
};

/// A prepared level: what every chunk shares.
struct LevelHeader {
    std::string description;  ///< the document's
    float chunk_size = 32.0f;  ///< metres
    glm::vec3 origin{0.0f};   ///< the lowest corner of chunk (0, 0)
    int chunks_x = 0;  ///< chunks along x
    int chunks_z = 0;  ///< chunks along z
    Aabb bounds;              ///< of everything in the level
    std::vector<LevelPoint> points;  ///< the level's named points
    std::vector<LevelVolume> volumes;  ///< the level's volumes
    LevelLighting lighting;  ///< the level's (apply_lighting())
    std::vector<NavTiling> navigation;  ///< one per agent: start_tiled() then add the chunks' tiles
    std::vector<std::uint8_t> present;  ///< chunks_x x chunks_z: 1 for a chunk with something in it

    /// The chunk holding the point (clamped to the grid).
    glm::ivec2 chunk_of(glm::vec3 point) const;
    /// Whether chunk `coord` exists and holds something.
    bool has_chunk(glm::ivec2 coord) const;
    /// The point named `name`, or null.
    const LevelPoint* point(const std::string& name) const;
};

/// A level baked: its header and its chunks (only those holding something).
struct BakedLevel {
    LevelHeader header;  ///< what every chunk shares
    std::vector<LevelChunk> chunks;  ///< those holding something
};

/// What bake_level() did.
struct LevelBakeStats {
    int instances = 0;  ///< placed prefabs, ground tiles included
    int collision_triangles = 0;  ///< of the whole level
    int chunks = 0;  ///< holding something
    int nav_tiles = 0;  ///< of every agent, with polygons
    double milliseconds = 0.0;  ///< the whole bake
};

/// The static collision of a prefab placed by `transform` with `overrides`: every entity with a
/// StaticCollision, its model's collision (from_model) and its boxes, at the entity's place (the
/// prefab's hierarchy of Transforms). Appended to `out`. What bake_level() does for each instance;
/// the editor uses it for a piece it moves.
void prefab_collision(const Prefab& prefab, const Transform& transform, const nlohmann::json& overrides,
                      const LevelSources& sources, CollisionMeshData& out);

/// Bakes `level`: the collision of its instances and ground tiles, cut in chunks; for each agent the
/// navigation mesh of the whole level (pruned from the level's points: what no point reaches is
/// dropped), kept per chunk; the chunks' instances. Throws std::runtime_error for a prefab that
/// cannot be read (with its instance) or a navigation Recast refuses. Long: the preparation, the
/// editor's "bake" (seconds for a big level).
BakedLevel bake_level(const LevelDocument& level, const LevelSources& sources, LevelBakeStats* stats = nullptr);

/// The version of the prepared files (`MLVL`, `MLVC`): 2 since the instances' properties.
inline constexpr std::uint32_t kPreparedLevelVersion = 2;

/// The header's file (`MLVL`, see FORMATS.md).
std::vector<std::uint8_t> encode_level_header(const LevelHeader& header);
/// Reads encode_level_header()'s file. Throws std::runtime_error naming `name` for another format or
/// version, or a damaged file (its checksum).
LevelHeader decode_level_header(const std::vector<std::uint8_t>& bytes, const std::string& name);
/// A chunk's file (`MLVC`).
std::vector<std::uint8_t> encode_level_chunk(const LevelChunk& chunk);
/// Reads encode_level_chunk()'s file; throws as decode_level_header() does.
LevelChunk decode_level_chunk(const std::vector<std::uint8_t>& bytes, const std::string& name);

/// The asset key of a level's prepared header: `levels/a.level.json` -> `levels/a.level.mlvl`.
std::string prepared_level_key(const std::string& level_key);
/// The asset key of one of its chunks: `levels/a.level.chunks/3_4.mlvc`.
std::string prepared_chunk_key(const std::string& level_key, glm::ivec2 coord);

}  // namespace moteur
