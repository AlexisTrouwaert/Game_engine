#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

// The identity of an entity across saves: unique in the game, given by a counter of the game
// (never reused), written in the save.
struct PersistentId {
    std::uint64_t value = 0;
};

// The references between saved entities, while saving and loading.
class SaveContext {
public:
    // While saving: the persistent identifier of an entity (0 if none or null).
    std::uint64_t id_of(entt::entity entity) const;
    // While loading: the entity created for a persistent identifier (null if none).
    entt::entity entity_of(std::uint64_t id) const;

private:
    friend class ComponentSerializers;
    const entt::registry* registry_ = nullptr;
    std::unordered_map<std::uint64_t, entt::entity> entities_;
};

class ComponentSerializers {
public:
    template <typename T>
    using Write = std::function<nlohmann::json(const T& component, const SaveContext& context)>;
    // Gives the entity its component (emplace_or_replace), from the saved value.
    using Read = std::function<void(entt::registry& registry, entt::entity entity, const nlohmann::json& value,
                                    const SaveContext& context)>;
    // Creates the entity of a record (and what it needs to be seen: models, Animator...). Its
    // components are given their values afterwards.
    using Spawn = std::function<entt::entity(entt::registry& registry, const nlohmann::json& record)>;

    // `name`: the component's key in the save (stable: renaming it breaks old saves).
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
    // A tag component (no data): saved as present.
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
    // A component the game rebuilds and never saves (a model, a mesh, a cached pose).
    template <typename T>
    void ignore() {
        known_.insert(entt::type_id<T>().hash());
    }

    // The engine's components: Transform, Name, Hidden, Parent, BoneAttachment, LightSource,
    // Collider, Mover, Animator; PersistentId and PreviousTransform handled here; models, meshes,
    // billboards and poses ignored (rebuilt).
    void add_engine_components();

    // Every persistent entity, in the order of their identifiers: [{"id": n, "c": {name: value}}].
    nlohmann::json save(const entt::registry& registry) const;
    // Creates the entities of `records` (spawn), gives them their components and PreviousTransform
    // = Transform (nothing glides on the first frame). Problems (unknown component, a reference to
    // a missing entity, an error in a component) go to `problems`; the rest is loaded.
    void load(entt::registry& registry, const nlohmann::json& records, const Spawn& spawn,
              std::vector<std::string>& problems) const;
    // Components of persistent entities that are neither saved nor ignored (their type names).
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
};

// Helpers for serializers: GLM values as JSON arrays (floats written exactly).
nlohmann::json to_json_value(const glm::vec2& v);
nlohmann::json to_json_value(const glm::vec3& v);
nlohmann::json to_json_value(const glm::quat& q);  // w, x, y, z
glm::vec2 vec2_from_json(const nlohmann::json& j);
glm::vec3 vec3_from_json(const nlohmann::json& j);
glm::quat quat_from_json(const nlohmann::json& j);

}  // namespace moteur
