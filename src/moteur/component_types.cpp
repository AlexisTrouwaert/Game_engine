#include "moteur/component_types.hpp"

#include <stdexcept>

#include "moteur/assert.hpp"
#include "moteur/collision.hpp"
#include "moteur/level_collision.hpp"
#include "moteur/level_streamer.hpp"
#include "moteur/movement.hpp"
#include "moteur/picking.hpp"
#include "moteur/world.hpp"

namespace moteur {

const ComponentType& ComponentTypes::insert(std::unique_ptr<ComponentType> type) {
    for (const std::unique_ptr<ComponentType>& existing : types_) {
        if (existing->id == type->id) {
            if (existing->name != type->name) {
                throw std::logic_error("ComponentTypes: '" + existing->name + "' registered again as '" + type->name + "'");
            }
            if (type->upgrade) {
                existing->upgrade = std::move(type->upgrade);
            }
            return *existing;
        }
        if (existing->name == type->name) {
            throw std::logic_error("ComponentTypes: the name '" + type->name + "' is already taken by " + existing->type->name);
        }
    }
    types_.push_back(std::move(type));
    return *types_.back();
}

const ComponentType* ComponentTypes::find(std::string_view name) const {
    for (const std::unique_ptr<ComponentType>& type : types_) {
        if (type->name == name) {
            return type.get();
        }
    }
    return nullptr;
}

const ComponentType* ComponentTypes::find(entt::id_type id) const {
    for (const std::unique_ptr<ComponentType>& type : types_) {
        if (type->id == id) {
            return type.get();
        }
    }
    return nullptr;
}

ComponentType::Upgrade upgrade_from_array(std::vector<std::string> keys) {
    return [keys = std::move(keys)](const nlohmann::json& value) {
        if (!value.is_array()) {
            return value;
        }
        nlohmann::json object = nlohmann::json::object();
        for (std::size_t i = 0; i < keys.size() && i < value.size(); ++i) {
            object[keys[i]] = value[i];
        }
        return object;
    };
}

ComponentType::Upgrade upgrade_from_bare(std::string key) {
    return [key = std::move(key)](const nlohmann::json& value) {
        return value.is_object() ? value : nlohmann::json{{key, value}};
    };
}

void ComponentTypes::add_engine_components() {
    // Before milestone 9 some components were saved as arrays or bare values: read as objects now.
    add<Transform>("transform");
    add<Name>("name", upgrade_from_bare("value"));
    add<Hidden>("hidden", [](const nlohmann::json& value) { return value.is_object() ? value : nlohmann::json::object(); });
    add<Parent>("parent", upgrade_from_bare("entity"));
    add<BoneAttachment>("bone_attachment", upgrade_from_bare("point"));
    add<LightSource>("light", upgrade_from_array({"color", "intensity", "range", "casts_shadows"}));
    add<Collider>("collider", upgrade_from_array({"radius", "layer", "mask", "push_weight", "blocked_by_walls"}));
    add<Mover>("mover", upgrade_mover);
    add<ModelComponent>("model");
    add<MaterialEffects>("material_effects");
    add<LightHalo>("halo");
    add<StaticCollision>("static_collision");
    add<Selectable>("selectable");
    add<LevelInstanceId>("level_instance");
}

ComponentTypes& component_types() {
    MOTEUR_ASSERT_MAIN_THREAD("component_types()");
    static ComponentTypes types = [] {
        ComponentTypes made;
        made.add_engine_components();
        return made;
    }();
    return types;
}

}  // namespace moteur
