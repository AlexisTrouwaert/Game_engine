#include "moteur/world_save.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <string_view>

#include "moteur/animator.hpp"
#include "moteur/collision.hpp"
#include "moteur/movement.hpp"
#include "moteur/reflection_io.hpp"
#include "moteur/world.hpp"

namespace moteur {


nlohmann::json to_json_value(const glm::vec2& v) {
    return {v.x, v.y};
}
nlohmann::json to_json_value(const glm::vec3& v) {
    return {v.x, v.y, v.z};
}
nlohmann::json to_json_value(const glm::quat& q) {
    return {q.w, q.x, q.y, q.z};
}
glm::vec2 vec2_from_json(const nlohmann::json& j) {
    return {j.at(0).get<float>(), j.at(1).get<float>()};
}
glm::vec3 vec3_from_json(const nlohmann::json& j) {
    return {j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>()};
}
glm::quat quat_from_json(const nlohmann::json& j) {
    return glm::quat(j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>(), j.at(3).get<float>());
}

std::uint64_t SaveContext::id_of(entt::entity entity) const {
    if (registry_ == nullptr || entity == entt::null || !registry_->valid(entity)) {
        return 0;
    }
    const PersistentId* id = registry_->try_get<PersistentId>(entity);
    return id != nullptr ? id->value : 0;
}

entt::entity SaveContext::entity_of(std::uint64_t id) const {
    const auto found = entities_.find(id);
    return found == entities_.end() ? entt::entity{entt::null} : found->second;
}

void ComponentSerializers::add_reflected(const ComponentType& type) {
    Entry entry;
    entry.name = type.name;
    entry.type = type.id;
    const ComponentType* component = &type;
    entry.write = [component](const entt::registry& registry, entt::entity entity,
                              const SaveContext& context) -> std::optional<nlohmann::json> {
        const void* value = component->get(const_cast<entt::registry&>(registry), entity);
        if (value == nullptr) {
            return std::nullopt;
        }
        WriteOptions options;
        options.flags = kFieldSave;
        options.entity = [&context](entt::entity e) { return nlohmann::json(context.id_of(e)); };
        return write_json(value, *component->type, options);
    };
    entry.read = [component, tables = tables_](entt::registry& registry, entt::entity entity, const nlohmann::json& json,
                                               const SaveContext& context) {
        ReadOptions options;
        options.flags = kFieldSave;
        options.tables = *tables;
        options.warn_unknown = false;  // a field removed since the save was written: dropped
        options.entity = [&context](const nlohmann::json& value, entt::entity& out, std::string& problem) {
            if (!value.is_number_unsigned() && !value.is_number_integer()) {
                problem = "expected a persistent identifier, found " + value.dump();
                return false;
            }
            const auto id = value.get<std::uint64_t>();
            out = id == 0 ? entt::entity{entt::null} : context.entity_of(id);
            if (id != 0 && out == entt::null) {
                problem = "entity " + std::to_string(id) + " missing from the save";
                return false;
            }
            return true;
        };
        ScratchObject value(*component->type);
        if (const void* current = component->get(registry, entity)) {
            component->type->copy(value.get(), current);  // what the spawn gave it, then the saved fields
        }
        DataIssues issues;
        if (!read_json(component->upgraded(json), value.get(), *component->type, component->name, issues, options)) {
            std::string text = issues.text();
            while (!text.empty() && text.back() == '\n') {
                text.pop_back();
            }
            throw std::runtime_error(text);
        }
        component->assign(registry, entity, value.get());
    };
    std::erase_if(entries_, [&type](const Entry& e) { return e.type == type.id; });
    entries_.push_back(std::move(entry));
    known_.insert(type.id);
}

void ComponentSerializers::add_registered() {
    for (const std::unique_ptr<ComponentType>& type : component_types().all()) {
        // A type with nothing to save (a model: rebuilt) stays out; so does one already handled.
        const bool saved = std::any_of(type->type->fields.begin(), type->type->fields.end(),
                                       [](const FieldInfo& field) { return (field.flags & kFieldSave) != 0; });
        if ((saved || type->tag) && known_.count(type->id) == 0) {
            add_reflected(*type);
        }
    }
}

void ComponentSerializers::add_engine_components() {
    ComponentTypes& types = component_types();
    for (const ComponentType* type : {types.find<Transform>(), types.find<Name>(), types.find<Hidden>(), types.find<Parent>(),
                                      types.find<BoneAttachment>(), types.find<LightSource>(), types.find<Collider>(),
                                      types.find<Mover>(), types.find<LightHalo>()}) {
        add_reflected(*type);
    }
    add<Animator>(
        "animator", [](const Animator& a, const SaveContext&) { return a.save_state(); },
        [](entt::registry& r, entt::entity e, const nlohmann::json& j, const SaveContext&) {
            Animator* animator = r.try_get<Animator>(e);
            if (animator == nullptr) {
                throw std::runtime_error("the entity has no Animator (the game must create it with the character)");
            }
            animator->load_state(j);
        });
    known_.insert(entt::type_id<PersistentId>().hash());
    ignore<PreviousTransform>();
    ignore<MeshComponent>();
    ignore<ModelComponent>();
    ignore<Billboard>();
    ignore<AnimationPose>();
}

nlohmann::json ComponentSerializers::save(const entt::registry& registry) const {
    SaveContext context;
    context.registry_ = &registry;
    std::vector<std::pair<std::uint64_t, entt::entity>> entities;
    for (const auto& [entity, id] : registry.view<PersistentId>().each()) {
        entities.emplace_back(id.value, entity);
    }
    std::sort(entities.begin(), entities.end());
    nlohmann::json records = nlohmann::json::array();
    for (const auto& [id, entity] : entities) {
        nlohmann::json components = nlohmann::json::object();
        for (const Entry& entry : entries_) {
            if (auto value = entry.write(registry, entity, context)) {
                components[entry.name] = std::move(*value);
            }
        }
        records.push_back({{"id", id}, {"c", std::move(components)}});
    }
    return records;
}

void ComponentSerializers::load(entt::registry& registry, const nlohmann::json& records, const Spawn& spawn,
                                std::vector<std::string>& problems) const {
    SaveContext context;
    context.registry_ = &registry;
    std::vector<std::pair<entt::entity, const nlohmann::json*>> created;
    std::unordered_set<std::uint64_t> seen;  // the valid identifiers met, spawned or not
    if (!records.is_array()) {
        problems.push_back("the entities are not an array");
        return;
    }
    for (std::size_t i = 0; i < records.size(); ++i) {
        const nlohmann::json& record = records[i];
        // A damaged record (no identifier, no components) is a problem of its own: it is not spawned.
        const auto id_value = record.is_object() ? record.find("id") : record.end();
        if (!record.is_object() || id_value == record.end() || !id_value->is_number_integer() ||
            (!id_value->is_number_unsigned() && id_value->get<std::int64_t>() < 0)) {
            problems.push_back("record " + std::to_string(i) + ": no \"id\" (a non-negative integer)");
            continue;
        }
        const auto components = record.find("c");
        if (components == record.end() || !components->is_object()) {
            problems.push_back("entity " + id_value->dump() + ": no \"c\" (an object of components)");
            continue;
        }
        const std::uint64_t id = id_value->get<std::uint64_t>();
        // 0 means "no entity" (SaveContext::id_of()), and an identifier names one entity: the first
        // record of an identifier is kept, a later one is not spawned (it would take its references).
        if (id == 0) {
            problems.push_back("record " + std::to_string(i) + ": \"id\" 0 (identifiers start at 1)");
            continue;
        }
        if (!seen.insert(id).second) {
            problems.push_back("record " + std::to_string(i) + ": \"id\" " + std::to_string(id) +
                               " already given to an earlier record");
            continue;
        }
        const entt::entity entity = spawn(registry, record);
        if (entity == entt::null) {
            problems.push_back("entity " + std::to_string(id) + ": the game cannot recreate it");
            continue;
        }
        registry.emplace_or_replace<PersistentId>(entity, id);
        context.entities_[id] = entity;
        created.emplace_back(entity, &record);
    }
    for (const auto& [entity, record] : created) {
        for (const auto& [name, value] : record->at("c").items()) {
            const auto entry = std::find_if(entries_.begin(), entries_.end(), [&name](const Entry& e) { return e.name == name; });
            if (entry == entries_.end()) {
                problems.push_back("entity " + record->at("id").dump() + ": unknown component '" + name + "'");
                continue;
            }
            try {
                entry->read(registry, entity, value, context);
            } catch (const std::exception& e) {
                problems.push_back("entity " + record->at("id").dump() + " > " + name + ": " + e.what());
            }
        }
        if (const Transform* transform = registry.try_get<Transform>(entity)) {
            registry.emplace_or_replace<PreviousTransform>(entity, *transform);  // nothing glides at first
        }
    }
}

std::vector<std::string> ComponentSerializers::unknown_components(const entt::registry& registry) const {
    std::vector<std::string> names;
    for (auto [id, storage] : registry.storage()) {
        if (known_.count(storage.info().hash()) != 0) {
            continue;
        }
        for (const auto& [entity, persistent] : registry.view<PersistentId>().each()) {
            if (storage.contains(entity)) {
                names.emplace_back(storage.info().name());
                break;
            }
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

}  // namespace moteur
