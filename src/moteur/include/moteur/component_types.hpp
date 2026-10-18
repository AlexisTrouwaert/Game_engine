#pragma once

#include <entt/entity/registry.hpp>
#include <nlohmann/json.hpp>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "moteur/reflection.hpp"

namespace moteur {

/// A component type known by name (milestone 9, part 3): its key in saves, prefabs and levels, its
/// description (reflection.hpp), and what the engine does with it without knowing its C++ type
/// (read it on an entity, give it to an entity, remove it). Made by ComponentTypes::add().
struct ComponentType {
    /// Turns an older saved or written form of the value into the current one (a value written as
    /// an array before it was described, a tag written as `true`). Given the JSON as found; returns
    /// it as the description reads it.
    using Upgrade = std::function<nlohmann::json(const nlohmann::json& value)>;

    std::string name;               ///< the key in saves, prefabs and levels ("transform"); stable
    const TypeInfo* type = nullptr;  ///< the component's description
    entt::id_type id = 0;           ///< entt::type_id<T>().hash()
    bool tag = false;               ///< an empty struct (Hidden): present or not
    Upgrade upgrade;                ///< older forms; empty: none

    /// The component of `entity` (for a tag, a pointer to a shared empty object), or null if it has
    /// none.
    void* (*get)(entt::registry& registry, entt::entity entity) = nullptr;
    /// Gives `entity` a copy of `value` (an object of #type): emplace_or_replace, so the signals fire.
    void (*assign)(entt::registry& registry, entt::entity entity, const void* value) = nullptr;
    /// Removes it from `entity`, if it has it.
    void (*remove)(entt::registry& registry, entt::entity entity) = nullptr;

    /// The value as its saved or written form reads it: upgrade() applied when there is one.
    nlohmann::json upgraded(const nlohmann::json& value) const { return upgrade ? upgrade(value) : value; }
};

/// Every component type known by name: the engine's (add_engine_components(), made when the
/// registry is first asked for), then the game's, registered at start-up. What the saves
/// (ComponentSerializers::add_registered()), the inspector, the prefabs and the level editor use to
/// handle any component. One per program: component_types(). Main thread.
class ComponentTypes {
public:
    /// Registers the described component `T` under `name` (stable: saves, prefabs and levels write
    /// it). Registering the same type again under the same name returns its entry (with `upgrade`
    /// replaced if one is given); another name for a type already registered, or a name already
    /// taken by another type, throws std::logic_error.
    template <typename T>
    const ComponentType& add(std::string name, ComponentType::Upgrade upgrade = {});

    /// The component type named `name`, or null.
    const ComponentType* find(std::string_view name) const;
    /// The component type of this EnTT type hash (entt::type_id<T>().hash()), or null.
    const ComponentType* find(entt::id_type id) const;
    /// The component type of `T`, or null.
    template <typename T>
    const ComponentType* find() const {
        return find(entt::type_id<T>().hash());
    }
    /// Every registered type, in the order of registration.
    const std::vector<std::unique_ptr<ComponentType>>& all() const { return types_; }

    /// The engine's described components: Transform (`transform`), Name (`name`), Hidden (`hidden`),
    /// Parent (`parent`), BoneAttachment (`bone_attachment`), LightSource (`light`), Collider
    /// (`collider`), Mover (`mover`), with the upgrades from the forms written before milestone 9
    /// (arrays, bare strings, `true` for a tag); ModelComponent (`model`), for prefabs and
    /// levels (never saved: the game rebuilds it); LightHalo (`halo`).
    void add_engine_components();

private:
    const ComponentType& insert(std::unique_ptr<ComponentType> type);
    std::vector<std::unique_ptr<ComponentType>> types_;
};

/// The program's component types, the engine's registered at the first call. Main thread.
ComponentTypes& component_types();

/// Upgrades for ComponentTypes::add(): a value written as an array before the component was
/// described (`[0.35, 1, ...]`) read as the object of these keys, in order; a value written bare
/// (`0.5`, `"name"`) read as `{key: value}`. Objects go through unchanged.
///@{
ComponentType::Upgrade upgrade_from_array(std::vector<std::string> keys);
ComponentType::Upgrade upgrade_from_bare(std::string key);
///@}

namespace component_detail {
inline char g_tag_object = 0;  ///< what ComponentType::get() gives for a tag
}

template <typename T>
const ComponentType& ComponentTypes::add(std::string name, ComponentType::Upgrade upgrade) {
    auto type = std::make_unique<ComponentType>();
    type->name = std::move(name);
    type->type = &type_info<T>();
    type->id = entt::type_id<T>().hash();
    type->upgrade = std::move(upgrade);
    if constexpr (std::is_empty_v<T>) {
        type->tag = true;
        type->get = [](entt::registry& registry, entt::entity entity) -> void* {
            return registry.all_of<T>(entity) ? &component_detail::g_tag_object : nullptr;
        };
        type->assign = [](entt::registry& registry, entt::entity entity, const void*) { registry.emplace_or_replace<T>(entity); };
    } else {
        type->get = [](entt::registry& registry, entt::entity entity) -> void* { return registry.try_get<T>(entity); };
        type->assign = [](entt::registry& registry, entt::entity entity, const void* value) {
            registry.emplace_or_replace<T>(entity, *static_cast<const T*>(value));
        };
    }
    type->remove = [](entt::registry& registry, entt::entity entity) { registry.remove<T>(entity); };
    return insert(std::move(type));
}

}  // namespace moteur
