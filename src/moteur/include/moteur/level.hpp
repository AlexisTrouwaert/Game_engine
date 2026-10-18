#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "moteur/aabb.hpp"
#include "moteur/data_reader.hpp"
#include "moteur/navigation.hpp"
#include "moteur/world.hpp"

namespace moteur {

class MapData;
class Renderer;
struct Environment;

// The levels of production (milestone 9, part 8): a level is a list of placed prefabs (kit pieces,
// props, lights, spawns), named points, volumes, a painted ground and its navigation settings,
// written by the editor as a text document (version control, merges) and prepared into a binary
// header and one file per chunk of a regular grid (level_bake.hpp), loaded around the hero
// (level_streamer.hpp). It replaces the maps of milestones 6 and 7 (map_data.hpp), which migrate.

/// A prefab placed in a level.
struct LevelInstance {
    std::uint32_t id = 0;        ///< stable: given once by the document, kept by every save and edit
    std::string prefab;          ///< the prefab's asset key
    std::string layer;           ///< the layer it is on (LevelDocument::layers)
    Transform transform;         ///< where its root goes (PrefabPlacement)
    nlohmann::json overrides = nlohmann::json::object();  ///< per instance (PrefabPlacement::overrides)
    /// The game's data for this instance (an object): what a spawner spawns, a chest's loot, the
    /// properties of a migrated map object. The engine only carries it (LevelStreamer's instance hook).
    nlohmann::json properties = nlohmann::json::object();

    /// Exact comparison.
    bool operator==(const LevelInstance&) const = default;
};

/// A named place of a level: an entry, a spawn, a connector.
struct LevelPoint {
    std::string name;  ///< unique in the level
    glm::vec3 position{0.0f};  ///< metres
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};  ///< where it faces

    /// Exact comparison.
    bool operator==(const LevelPoint&) const = default;
};

/// A box of a level the game gives a meaning to: a zone, a trigger, an ambient sound, an area of the
/// navigation ("nav_area": `properties.area` = "water" or "fire").
struct LevelVolume {
    std::uint32_t id = 0;  ///< stable, from the same numbering as the instances
    std::string kind;      ///< free for the game, but for "nav_area"
    Aabb box;  ///< world, metres
    nlohmann::json properties = nlohmann::json::object();  ///< the game's (an object)

    /// Exact comparison.
    bool operator==(const LevelVolume&) const = default;
};

/// A layer of a level: instances grouped for the editor (shown, locked).
struct LevelLayer {
    std::string name;  ///< unique in the level
    bool visible = true;  ///< shown in the editor
    bool locked = false;  ///< not editable in the editor

    /// Exact comparison.
    bool operator==(const LevelLayer&) const = default;
};

/// The ground painted by the author: a grid of cells, each the index + 1 of a ground prefab (0:
/// none), placed at the cell's corner (a kit's ground tile of `cell_size` metres).
struct LevelGround {
    glm::vec2 origin{0.0f};         ///< x, z of the corner of cell (0, 0)
    float cell_size = 8.0f;         ///< metres
    int width = 0;                  ///< cells along x
    int height = 0;                 ///< cells along z
    float level = 0.0f;             ///< the height the tiles are put at
    std::vector<std::string> prefabs;   ///< the ground prefabs, by their asset keys
    std::vector<std::uint8_t> cells;    ///< width x height, row after row (z), 0: none

    /// Exact comparison.
    bool operator==(const LevelGround&) const = default;
};

/// How a level is lit (milestone 9, part 10): what the game and the editor both give the renderer
/// (apply_lighting()), so that the editor shows the level as the game does.
struct LevelLighting {
    float sun_yaw = 60.0f;        ///< degrees, around the vertical, from +x towards +z
    float sun_elevation = 55.0f;  ///< degrees above the horizon
    glm::vec3 sun_color{1.0f, 0.93f, 0.8f};  ///< sRGB
    float sun_intensity = 3.0f;   ///< the sun's light (0: none)
    float sky_intensity = 1.0f;   ///< the light of the sky (the environment), 0: none
    float exposure = 1.0f;        ///< multiplies the image before tone mapping
    glm::vec3 background{0.05f, 0.06f, 0.08f};  ///< sRGB, where nothing is drawn

    /// Exact comparison.
    bool operator==(const LevelLighting&) const = default;
};

MOTEUR_REFLECT(LevelLighting,
    MOTEUR_FIELD(sun_yaw, {.min = -360.0, .max = 360.0, .unit = "deg", .flags = kFieldDefault}),
    MOTEUR_FIELD(sun_elevation, {.min = -90.0, .max = 90.0, .unit = "deg", .flags = kFieldDefault}),
    MOTEUR_FIELD(sun_color, {.flags = kFieldDefault}),
    MOTEUR_FIELD(sun_intensity, {.min = 0.0, .max = 100.0, .flags = kFieldDefault}),
    MOTEUR_FIELD(sky_intensity, {.min = 0.0, .max = 10.0, .flags = kFieldDefault}),
    MOTEUR_FIELD(exposure, {.min = 0.01, .max = 100.0, .flags = kFieldDefault}),
    MOTEUR_FIELD(background, {.flags = kFieldDefault}));

/// The night of a level: a faint bluish moon where the sun was, almost no sky. The editor's "night"
/// view, to judge the lamps; a game may use it for its own nights.
LevelLighting night_lighting(const LevelLighting& day);

/// The direction towards the sun (unit, y up) of `lighting`.
glm::vec3 sun_direction(const LevelLighting& lighting);

/// Gives `lighting` to `renderer`: the sun and the sky (`sky`, may be null: no sky light) of its
/// meshes, the exposure and the background. Each frame, before drawing (the game and the editor).
void apply_lighting(Renderer& renderer, const LevelLighting& lighting, const Environment* sky);

/// A level as its source document says it.
class LevelDocument {
public:
    /// Format version of the document.
    static constexpr int kVersion = 3;

    std::string description;  ///< shown by the tools
    float chunk_size = 32.0f;  ///< metres: the chunks it is prepared and loaded by (and the navigation's tiles)
    std::vector<LevelLayer> layers{LevelLayer{"default"}};  ///< at least one
    std::vector<LevelInstance> instances;  ///< the placed prefabs
    std::vector<LevelPoint> points;  ///< named places
    std::vector<LevelVolume> volumes;  ///< boxes the game reads
    LevelGround ground;  ///< the painted ground (empty: none)
    LevelLighting lighting;  ///< the sun, the sky, the exposure
    std::vector<NavAgent> agents{NavAgent{}};  ///< one navigation mesh per size of creature
    NavBuildSettings navigation;               ///< their settings (tile_size: chunk_size)

    /// Reads a document (JSON, or CBOR as shipped). Asset references ({"id", "path"} or a path) are
    /// turned into keys by `resolve` (Assets::resolve_reference()), which throws for a bad one.
    /// Problems go to `issues` (with the paths of DataReader); throws std::runtime_error with them
    /// when there are errors: another version, duplicated or missing identifiers, an instance
    /// without prefab, a ground whose cells do not match its size.
    static LevelDocument parse(std::string_view text, const std::string& key,
                               const std::function<std::string(const nlohmann::json&)>& resolve, DataIssues& issues);
    /// The document as the editor writes it; `reference` turns a key into a reference
    /// (Assets::reference_to()).
    nlohmann::json to_json(const std::function<nlohmann::json(const std::string&)>& reference) const;

    /// Adds an instance, giving it the next identifier (its `id` is ignored); returns it.
    std::uint32_t add(LevelInstance instance);
    /// Adds a volume, giving it the next identifier; returns it.
    std::uint32_t add(LevelVolume volume);
    /// The identifier the next instance or volume will get. Never goes back: a removed identifier
    /// is not reused (saves refer to them).
    std::uint32_t next_id() const { return next_id_; }
    /// The instance with this identifier, or null.
    const LevelInstance* instance(std::uint32_t id) const;
    /// The point named `name`, or null.
    const LevelPoint* point(std::string_view name) const;
    /// The box around every instance's position, point and volume (and the ground).
    Aabb bounds() const;

private:
    std::uint32_t next_id_ = 1;
};

/// How migrate_map() turns a map of milestones 6 and 7 into a level.
struct MapMigration {
    /// For each tile type (by name), the prefab built on its cells: one instance per run of such
    /// cells along x, scaled along x to the run's length (a blockout wall of 1 x 1 m covers a run).
    std::map<std::string, std::string> tiles;
    /// The tile type that is the ground, and the prefab laid on its cells (in runs, as `tiles`).
    std::string ground_tile;  ///< see above
    std::string ground_prefab;  ///< see ground_tile
    /// For each object type of the map (MapObject::type), the prefab placed for it; others are left
    /// out with a warning.
    std::map<std::string, std::string> objects;
    /// Object types that become named points rather than instances (spawns: "monster" gives the
    /// points "monster_o12", ...); their properties are lost (a point has none).
    std::vector<std::string> object_points;
    float chunk_size = 32.0f;  ///< of the level made
};

/// A level from a map of milestones 6 and 7 (versions 1 and 2): its cells built as prefabs by the
/// rules, its named points as points (at the cells' centres), its objects as instances, its
/// connectors as volumes of kind "connector". Warnings (types without a rule) go to `issues`.
LevelDocument migrate_map(const MapData& map, const MapMigration& rules, DataIssues& issues);

/// What a level changed in its instances during a game (a door opened, a chest emptied, a barrel
/// broken), by instance identifier: saved with the game, applied to an instance whenever its chunk
/// loads, whatever chunk is loaded when the game is saved.
class LevelState {
public:
    /// Overrides for instance `id` (as LevelInstance::overrides), merged into what it had.
    void set(std::uint32_t id, const nlohmann::json& overrides);
    /// The instance is gone for good (destroyed, picked up): never instantiated again.
    void remove(std::uint32_t id);
    /// Whether instance `id` was removed.
    bool removed(std::uint32_t id) const;
    /// The overrides of instance `id` (an empty object when none).
    const nlohmann::json& overrides(std::uint32_t id) const;
    /// Forgets everything (a new game).
    void clear();
    /// As a save section: {"overrides": {"id": {...}}, "removed": [ids]}.
    nlohmann::json to_json() const;
    /// Reads to_json()'s form; throws std::runtime_error for another form.
    static LevelState from_json(const nlohmann::json& json);

private:
    std::map<std::uint32_t, nlohmann::json> overrides_;
    std::vector<std::uint32_t> removed_;  ///< sorted
};

}  // namespace moteur
