#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "moteur/component_types.hpp"

namespace moteur {

// Saving the state of a world (milestone 7, part 6). Only the entities with a PersistentId are
// written, each with the components that have a serializer; the rest of the world (decor built
// from the map, effects, markers) is rebuilt by the game. Loading asks the game to create each
// entity from its record (with its visuals: a model, an Animator...), then gives the components
// their saved values, then resolves the references between entities (by persistent identifier,
// never by EnTT identifier, which depends on the order of creation).
//
// Writing the state, not what follows from it: flow fields, fields of view, poses are computed
// again. A component of a persistent entity that has neither a serializer nor ignore() is
// reported by unknown_components(): a forgotten state shows up before it breaks a game.

/// The identity of an entity across saves: unique in the game, given by a counter of the game
/// (never reused), written in the save. Only entities with this component are saved
/// (ComponentSerializers::save()); ComponentSerializers::load() gives it back to the entities it
/// recreates.
struct PersistentId {
    std::uint64_t value = 0;  ///< the identifier; never 0 (0 means "no entity" in SaveContext::id_of())
};

/// The references between saved entities, while saving and loading. Made by ComponentSerializers
/// and handed to the serializers' functions; valid only during that call.
class SaveContext {
public:
    /// The persistent identifier of an entity (0 if it is null, destroyed or has no PersistentId):
    /// what a Write function stores for a reference to another entity.
    std::uint64_t id_of(entt::entity entity) const;
    /// While loading: the entity created for a persistent identifier (`entt::null` if none, for
    /// instance when the referenced entity is missing from the save or could not be spawned). Every
    /// record's entity exists before any component is read, so references in any order resolve.
    entt::entity entity_of(std::uint64_t id) const;

private:
    friend class ComponentSerializers;
    const entt::registry* registry_ = nullptr;
    std::unordered_map<std::uint64_t, entt::entity> entities_;
};

/// The save and load functions of the components of a world (see the top of world_save.hpp): one
/// ComponentSerializers per game, filled at start-up (add_engine_components(), then the game's own
/// add(), add_tag() and ignore()), then used by save() and load(). Main thread; the registry must
/// not change during a call.
class ComponentSerializers {
public:
    /// Turns a component into its saved value (any JSON; references to entities through
    /// SaveContext::id_of()).
    template <typename T>
    using Write = std::function<nlohmann::json(const T& component, const SaveContext& context)>;
    /// Gives the entity its component (emplace_or_replace), from the saved value. May throw (a
    /// std::exception, such as nlohmann's for a missing key): load() reports it as a problem of that
    /// component and goes on.
    using Read = std::function<void(entt::registry& registry, entt::entity entity, const nlohmann::json& value,
                                    const SaveContext& context)>;
    /// Creates the entity of a record (and what it needs to be seen: models, Animator...). Its
    /// components are given their values afterwards. `record` is the whole saved record
    /// (`{"id": n, "c": {...}}`): the game can look at its components to decide what to create.
    /// Returning `entt::null` skips the record (reported as a problem).
    using Spawn = std::function<entt::entity(entt::registry& registry, const nlohmann::json& record)>;

    /// Saves and loads the component `T` with `write` and `read`. `name`: the component's key in the
    /// save (stable: renaming it breaks old saves), unique among the serializers (not checked).
    template <typename T>
    void add(std::string name, Write<T> write, Read read) {
        Entry entry;
        entry.name = std::move(name);
        entry.type = entt::type_id<T>().hash();
        entry.write = [write = std::move(write)](const entt::registry& registry, entt::entity entity,
                                                 const SaveContext& context) -> std::optional<nlohmann::json> {
            if (const T* component = registry.try_get<T>(entity)) {
                return write(*component, context);
            }
            return std::nullopt;
        };
        entry.read = std::move(read);
        entries_.push_back(std::move(entry));
        known_.insert(entt::type_id<T>().hash());
    }
    /// A tag component (no data): saved as present (`true`), given back with emplace_or_replace().
    template <typename T>
    void add_tag(std::string name) {
        Entry entry;
        entry.name = std::move(name);
        entry.type = entt::type_id<T>().hash();
        entry.write = [](const entt::registry& registry, entt::entity entity,
                         const SaveContext&) -> std::optional<nlohmann::json> {
            return registry.all_of<T>(entity) ? std::optional<nlohmann::json>(true) : std::nullopt;
        };
        entry.read = [](entt::registry& registry, entt::entity entity, const nlohmann::json&, const SaveContext&) {
            registry.emplace_or_replace<T>(entity);
        };
        entries_.push_back(std::move(entry));
        known_.insert(entt::type_id<T>().hash());
    }
    /// A described component registered in component_types() (milestone 9, part 3), saved by its
    /// description: the fields flagged kFieldSave, entities by persistent identifier (an
    /// identifier missing from the save is a problem), its older forms read through
    /// ComponentType::upgrade. Under the type's name. Adding a type already added replaces it.
    void add_reflected(const ComponentType& type);
    /// The same for `T`, registered in component_types() under `name` if it is not yet.
    template <typename T>
    void add_reflected(std::string name) {
        add_reflected(component_types().add<T>(std::move(name)));
    }
    /// The data tables the DataRef fields of described components are bound to when a save is read
    /// (a row missing from the table is a problem of that component). Null (the default): such
    /// fields are read as identifiers only. Must outlive the serializers' use.
    void set_tables(const DataTables* tables) { *tables_ = tables; }

    /// Every type of component_types() not added nor ignored yet that has something to save (a field
    /// flagged kFieldSave, or a tag), by its description.
    void add_registered();
    /// A component the game rebuilds and never saves (a model, a mesh, a cached pose): no longer
    /// reported by unknown_components().
    template <typename T>
    void ignore() {
        known_.insert(entt::type_id<T>().hash());
    }

    /// The engine's components: those of ComponentTypes::add_engine_components() by their
    /// description (add_reflected(): Transform `transform`, Name `name`, Hidden `hidden`, Parent
    /// `parent` by persistent identifier, BoneAttachment `bone_attachment`, LightSource `light`,
    /// Collider `collider`, Mover `mover`, enumerations by name; the forms written before milestone
    /// 9 still read), and Animator (`animator`, Animator::save_state(): the Spawn function must
    /// create the Animator, whose state is then loaded into it). PersistentId and PreviousTransform are handled
    /// by save() and load() themselves; MeshComponent, ModelComponent, Billboard and AnimationPose are
    /// ignored (rebuilt). Call once.
    void add_engine_components();

    /// Every persistent entity, in the order of their identifiers:
    /// `[{"id": n, "c": {key: value, ...}}, ...]`, with a value for each of its components that has a
    /// serializer. Allocates; O(persistent entities x serializers).
    nlohmann::json save(const entt::registry& registry) const;
    /// Creates the entities of `records` (as save() wrote them) with `spawn`, gives them their
    /// PersistentId and their components, and adds or replaces PreviousTransform = Transform on each
    /// one that has a Transform (nothing glides on the first frame). Problems (an entity the game
    /// cannot spawn, an unknown component, a reference to a missing entity, an error in a component,
    /// a record without a valid `"id"` or `"c"`, which is then not spawned; an `"id"` of 0 or one
    /// already given to an earlier record is not valid) are appended to
    /// `problems`, one line each; the rest is loaded. Load into a registry without those entities (a
    /// new World).
    void load(entt::registry& registry, const nlohmann::json& records, const Spawn& spawn,
              std::vector<std::string>& problems) const;
    /// Components of persistent entities that are neither saved nor ignored (their type names, as
    /// EnTT spells them: compiler-dependent), sorted: a state that a save would forget. For tests and
    /// development checks; O(component types x persistent entities).
    std::vector<std::string> unknown_components(const entt::registry& registry) const;

private:
    struct Entry {
        std::string name;
        entt::id_type type = 0;
        std::function<std::optional<nlohmann::json>(const entt::registry&, entt::entity, const SaveContext&)> write;
        Read read;
    };
    std::vector<Entry> entries_;
    std::unordered_set<entt::id_type> known_;
    /// Shared with the readers made by add_reflected(), so that copies of the serializers keep it.
    std::shared_ptr<const DataTables*> tables_ = std::make_shared<const DataTables*>(nullptr);
};

/// Helpers for serializers: GLM values as JSON arrays of numbers (floats written exactly, so a
/// value read back is the same bit for bit). Quaternions in glm's order: w, x, y, z. The readers
/// throw nlohmann::json's exception for an array too short or an element that is not a number
/// (which load() reports as a problem of the component).
///@{
nlohmann::json to_json_value(const glm::vec2& v);
nlohmann::json to_json_value(const glm::vec3& v);
nlohmann::json to_json_value(const glm::quat& q);
glm::vec2 vec2_from_json(const nlohmann::json& j);
glm::vec3 vec3_from_json(const nlohmann::json& j);
glm::quat quat_from_json(const nlohmann::json& j);
///@}

}  // namespace moteur
