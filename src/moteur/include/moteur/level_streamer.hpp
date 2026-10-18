#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <map>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "moteur/collision_scene.hpp"
#include "moteur/jobs.hpp"
#include "moteur/level.hpp"
#include "moteur/level_bake.hpp"
#include "moteur/navigation.hpp"
#include "moteur/reflection.hpp"

namespace moteur {

class Assets;
class World;

// Loading a level chunk by chunk (milestone 9, part 8): the chunks around a focus (the hero, the
// camera) are read and decoded in the background, their prefabs prefetched, then instantiated on
// the main thread a few at a time (a budget per update: no frame waits for a whole chunk); the
// level's collision scene is rebuilt in the background and swapped in; the chunks' navigation tiles
// are added and removed as they come and go (the neighbours' tiles join). Far chunks are unloaded,
// with a margin so that walking back and forth at an edge does not reload them; at most
// StreamingSettings::max_chunks stay. A closed zone loads everything at once (load_all) and waits
// for ready() behind a loading screen.
//
// The logic stays deterministic when it only acts in chunks that are loaded (ready()): what a chunk
// holds is the same whenever it loads.

/// Marks the root entity of a level's instance: its identifier in the level (LevelInstance::id),
/// for the LevelState and the saves. Not saved itself (the level makes it again).
struct LevelInstanceId {
    std::uint32_t id = 0;  ///< LevelInstance::id
};
MOTEUR_REFLECT(LevelInstanceId, MOTEUR_FIELD(id, {.flags = kFieldEdit | kFieldReadOnly | kFieldLogic}));

/// How LevelStreamer loads.
struct StreamingSettings {
    float load_radius = 48.0f;    ///< metres from the focus: chunks within are loaded
    float unload_radius = 80.0f;  ///< metres: chunks beyond are unloaded (more than load_radius: a margin)
    int max_chunks = 64;          ///< loaded at most (the memory budget): the farthest go first
    double instantiate_budget_ms = 2.0;  ///< per update, for creating the entities of loaded chunks
    bool load_all = false;        ///< a closed zone: every chunk, whatever the focus
    /// The chunks are made in the order of their coordinates (z, then x), whatever order the
    /// background reads them in: the entities, the navigation tiles and the instance hook's calls
    /// come in the same order on every run (a game whose logic depends on it: replays, saves). A
    /// chunk not read yet holds back the ones after it.
    bool ordered = false;
};

/// What a LevelStreamer did, for the tools and the measures.
struct StreamingStats {
    int chunks_loaded = 0;     ///< instantiated, in the collision scene and the navigation
    int chunks_pending = 0;    ///< being read, prefetched or instantiated
    int instances = 0;         ///< entities' roots made by the level now alive
    int collision_triangles = 0;  ///< in the collision scene now
    double last_update_ms = 0.0;  ///< the main thread's share of the last update()
    double worst_update_ms = 0.0; ///< since open()
};

/// The sources of bake_level() from the asset manager: prefabs by Assets::prefab(), models'
/// collision by Assets::model() (they stay loaded as long as the returned sources live), references
/// by Assets::resolve_reference(). What the editor and a development build bake with.
LevelSources asset_level_sources(Assets& assets);

/// A level's source document read through `assets` (LevelDocument::parse()). Throws as it does.
LevelDocument read_level(Assets& assets, const std::string& level_key, DataIssues& issues);

/// A level loaded chunk by chunk into a World. Main thread (it reads the world and the assets).
class LevelStreamer {
public:
    /// Loads into `world` with `assets`, both kept by reference (they outlive it).
    LevelStreamer(Assets& assets, World& world, const StreamingSettings& settings = {});
    ~LevelStreamer();
    LevelStreamer(const LevelStreamer&) = delete;
    LevelStreamer& operator=(const LevelStreamer&) = delete;

    /// Opens the prepared level of `level_key` (prepared_level_key(): its header, then its chunks by
    /// prepared_chunk_key()). Throws std::runtime_error when the header cannot be read. Closes the
    /// previous level first.
    void open(const std::string& level_key);
    /// Opens a level baked in memory (the editor, or a development build without its prepared form):
    /// its chunks are taken from `baked` instead of files.
    void open(BakedLevel baked, const std::string& level_key);
    /// The prepared form of `level_key` from the assets if they have it (the shipped game, a cooked
    /// build); else from the import cache (Assets::import_cache()) if it is up to date with what the
    /// level is made from (its file, its prefabs and their bases, its models and their import
    /// settings: sizes and times, as the models' prepared forms); else the level baked from its
    /// source now (seconds), and written to the import cache for the next time. Development.
    void open_any(const std::string& level_key);
    /// Destroys what the level made and forgets it (its state stays: state()).
    void close();
    /// Whether a level is open.
    bool is_open() const { return open_; }

    /// Where the chunks are loaded around (the hero). Kept.
    void set_focus(glm::vec3 focus) { focus_ = focus; }
    /// The settings given to the constructor, to change (load_all for a closed zone).
    StreamingSettings& settings() { return settings_; }

    /// Moves the loading on: starts the reads of the chunks now wanted, takes what the background
    /// finished (prefabs, tiles, collision), instantiates within the budget, unloads the far chunks.
    /// Call it once per frame (or tick), from the main thread.
    void update();
    /// Blocks until every wanted chunk is loaded (a loading screen's last step, a test). Returns
    /// false after `timeout_seconds`.
    bool finish(double timeout_seconds = 30.0);
    /// True when every chunk the focus wants is loaded and the collision scene has them.
    bool ready() const;

    /// The level's header (points, volumes, the grid). Valid while open.
    const LevelHeader& header() const { return header_; }
    /// The static collision of the loaded chunks.
    const CollisionScene& collision() const { return *collision_; }
    /// Its navigation meshes, one per agent (NavSources::meshes), with the loaded chunks' tiles.
    std::span<const NavMesh* const> meshes() const { return mesh_pointers_; }
    /// What changed in the level during the game: applied to every instance made from now on (an
    /// instance already made keeps what it has: change it too). Saved with the game.
    LevelState& state() { return state_; }
    /// See state().
    const LevelState& state() const { return state_; }
    /// The root entity of instance `id` while its chunk is loaded, else null.
    entt::entity entity_of(std::uint32_t id) const;
    /// Whether chunk `coord` is loaded.
    bool loaded(glm::ivec2 coord) const;
    /// What the last update() did.
    const StreamingStats& stats() const { return stats_; }
    /// Called for each instance once it is made (its chunk loaded), with its root entity and what
    /// the level says of it (its properties: the game's data): where the game gives it its own
    /// components (a spawner's monsters, a chest's loot). Main thread, inside update() or finish().
    void set_instance_hook(std::function<void(entt::entity root, const ChunkInstance& instance)> hook) {
        instance_hook_ = std::move(hook);
    }

private:
    struct Chunk;
    void open_from(const std::string& level_key, const std::string& root);  ///< the prepared files of `root` (empty: the assets)
    void want(glm::ivec2 coord);
    void unload(glm::ivec2 coord);
    void rebuild_collision();

    Assets& assets_;
    World& world_;
    StreamingSettings settings_;
    bool open_ = false;
    std::string key_;
    std::string prepared_root_;  ///< where the prepared files are read; empty: the assets (Assets::file_path())
    LevelHeader header_;
    std::map<std::pair<int, int>, std::shared_ptr<LevelChunk>> in_memory_;  ///< open(BakedLevel)
    std::map<std::pair<int, int>, std::unique_ptr<Chunk>> chunks_;           ///< wanted or loaded
    std::vector<std::unique_ptr<NavMesh>> meshes_;
    std::vector<const NavMesh*> mesh_pointers_;
    std::unique_ptr<CollisionScene> collision_;
    struct CollisionBuild;
    std::shared_ptr<CollisionBuild> collision_build_;  ///< a rebuild in the background
    bool collision_dirty_ = false;
    std::map<std::uint32_t, entt::entity> instances_;
    std::function<void(entt::entity, const ChunkInstance&)> instance_hook_;
    LevelState state_;
    glm::vec3 focus_{0.0f};
    StreamingStats stats_;
};

}  // namespace moteur
