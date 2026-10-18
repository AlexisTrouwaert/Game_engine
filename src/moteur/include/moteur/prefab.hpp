#pragma once

#include <entt/entity/registry.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "moteur/component_types.hpp"
#include "moteur/data_reader.hpp"
#include "moteur/reflection.hpp"

namespace moteur {

class World;
struct Transform;

/// A model of entities described in data (milestone 9, part 3): a street lamp with its light, a
/// door with its frame, a chest with its effects. A root entity, children, their components by the
/// reflection (component_types()), and references to assets; instantiated by the game, the editor
/// and the levels. Prefabs **nest** (an entity of a prefab is an instance of another, with
/// overrides) and have **variants** (a prefab that starts from another and changes some fields,
/// removes or adds entities), as Unity's nested prefabs and prefab variants. The format
/// (`*.prefab.json`, see FORMATS.md):
///
/// @code{.json}
/// {
///   "version": 1,
///   "base": "prefabs/lampadaire.prefab.json",          // optional: this prefab is a variant of that one
///   "entities": [
///     {"id": 1, "name": "lampadaire", "components": {
///        "transform": {}, "model": {"model": "models/props/lampadaire_gothique.glb"}}},
///     {"id": 2, "parent": 1, "name": "flamme", "components": {
///        "transform": {"position": [0, 3.52, 0]},
///        "light": {"color": [1, 0.55, 0.25], "intensity": 12, "range": 12, "casts_shadows": true}}},
///     {"id": 3, "parent": 1, "prefab": "prefabs/blason.prefab.json",   // a nested prefab
///      "components": {"transform": {"position": [0, 2, 0.1]}},         // over its root's
///      "overrides": {"2": {"light": {"intensity": 3}}}}                  // its entity 2
///   ],
///   "overrides": {"2": {"light": {"intensity": 20}}},   // variant: fields of the base's entities ("3/2": nested)
///   "removed": ["3"]                                     // variant: entities of the base left out
/// }
/// @endcode
///
/// Identifiers (`id`) are positive integers, given once by the author or the editor and never
/// changed (overrides, levels and saves name entities by them). An entity of a nested prefab is
/// named by its path: the instance's identifier, '/', its identifier inside ("3/2"). A component's
/// value in `components` and `overrides` holds only the fields that differ: overrides are merged
/// field by field into what they override (an array or a scalar replaces; an object merges). The
/// `parent` of each entity is the identifier of another of the same file; exactly one entity (the
/// root) has none.
///
/// Loading resolves the base and the nested prefabs (Assets::prefab()), merges everything, and
/// reads each component once with its description: instantiating only copies values. Asset fields
/// (a model) are loaded then and held by the prefab. Immutable once loaded; main thread.
class Prefab {
public:
    /// One component of an entity: its type and its value, ready to be copied.
    struct Component {
        const ComponentType* type = nullptr;         ///< the component's type
        std::shared_ptr<const ScratchObject> value;  ///< an object of type->type; shared by the variants
    };
    /// One entity once everything is merged, parents before their children.
    struct Entity {
        std::string path;          ///< its identifier, or its path through nested prefabs ("3/2")
        std::string name;          ///< for the tools; empty: none
        std::int32_t parent = -1;  ///< index in entities() of its parent; -1 for the root
        std::vector<Component> components;  ///< its components, in the order of the file
    };

    /// What a Prefab needs while it loads: the prefabs it refers to (Assets::prefab()), and the
    /// options that read asset and entity fields.
    struct Context {
        /// The prefab at that path, loaded (nested or base). Throws for a missing one, or a cycle.
        std::function<std::shared_ptr<const Prefab>(const std::string& path)> prefab;
        /// Asset fields: see ReadOptions::asset.
        std::function<bool(const TypeInfo& type, const nlohmann::json& value, void* handle, std::string& problem)> asset;
    };

    /// Reads a prefab file. `key` names it in messages. Problems go to `issues` (unknown component
    /// names are warnings: that component is left out); throws std::runtime_error with every error
    /// when there is one, or for JSON that cannot be read, another version, a cycle of bases or
    /// nested prefabs, several roots, an unknown parent or a duplicated identifier.
    static Prefab parse(std::string_view text, const std::string& key, const Context& context, DataIssues& issues);

    /// The asset key it was read from.
    const std::string& key() const { return key_; }
    /// Every entity, the root first, parents before their children.
    const std::vector<Entity>& entities() const { return entities_; }
    /// The index of the entity with this path, or -1.
    std::int32_t find(std::string_view path) const;
    /// The merged component values of each entity as JSON (path -> component -> value), as the
    /// editor shows and overrides them.
    const nlohmann::json& merged() const { return merged_; }
    /// The other prefab files it was made from (bases and nested, recursively): a change in one of
    /// them reloads this one.
    const std::vector<std::string>& dependencies() const { return dependencies_; }

private:
    std::string key_;
    std::vector<Entity> entities_;
    nlohmann::json merged_;
    std::vector<std::string> dependencies_;
};

/// How instantiate() places a prefab.
struct PrefabPlacement {
    /// Where the root goes: composed with the root's own Transform (the prefab's root offset).
    glm::vec3 position{0.0f};                    ///< metres
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};  ///< identity by default
    glm::vec3 scale{1.0f};                       ///< per axis
    entt::entity parent = entt::null;  ///< attaches the root to this entity (a Parent), if given
    bool moving = false;               ///< gives the root a PreviousTransform (it will move; interpolated)
    /// Per-instance overrides, as a prefab's: path -> component -> fields ({"2": {"light": {"intensity": 5}}}),
    /// read with the same rules. What a level stores for each instance.
    nlohmann::json overrides = nlohmann::json::object();
};

/// What instantiate() made.
struct PrefabInstance {
    entt::entity root = entt::null;     ///< the root's entity
    std::vector<entt::entity> entities;  ///< one per Prefab::entities(), in the same order
};

/// Creates the entities of `prefab` in `world` with their components (copies of the prefab's
/// values), their Parent links and their Name (the entity's name), the root placed by `placement`.
/// Entity fields between entities of the prefab are mapped to the new entities. Overrides of the
/// placement are read with `context` (asset fields); problems go to `issues`, the override that has
/// one is left out. Main thread.
PrefabInstance instantiate(World& world, const Prefab& prefab, const PrefabPlacement& placement = {},
                           DataIssues* issues = nullptr, const Prefab::Context* context = nullptr);

/// Merges `patch` into `target` field by field: objects merge recursively, anything else (arrays,
/// scalars, null) replaces. What overrides do.
void merge_fields(nlohmann::json& target, const nlohmann::json& patch);

}  // namespace moteur
