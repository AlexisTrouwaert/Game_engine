#include "moteur/prefab.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

#include "moteur/document.hpp"
#include "moteur/reflection_io.hpp"
#include "moteur/world.hpp"

namespace moteur {

void merge_fields(nlohmann::json& target, const nlohmann::json& patch) {
    if (!patch.is_object() || !target.is_object()) {
        target = patch;
        return;
    }
    for (const auto& [key, value] : patch.items()) {
        const auto found = target.find(key);
        if (found != target.end() && found->is_object() && value.is_object()) {
            merge_fields(*found, value);
        } else {
            target[key] = value;
        }
    }
}

namespace {

constexpr int kVersion = 1;

// An entity while the file is flattened: its components still as JSON.
struct Raw {
    std::string path;
    std::string name;
    std::string parent;  // the parent's path; empty for the root
    nlohmann::json components = nlohmann::json::object();
};

std::string id_text(const nlohmann::json& value, const std::string& where) {
    if (value.is_number_unsigned() || (value.is_number_integer() && value.get<std::int64_t>() > 0)) {
        if (value.get<std::uint64_t>() == 0) {
            throw std::runtime_error(where + ": identifiers start at 1");
        }
        return std::to_string(value.get<std::uint64_t>());
    }
    throw std::runtime_error(where + ": expected a positive integer identifier, found " + value.dump());
}

// A path written in an override or a "removed" list: "2", "3/2", or a number.
std::string path_text(const nlohmann::json& value, const std::string& where) {
    if (value.is_string()) {
        return value.get<std::string>();
    }
    return id_text(value, where);
}

// Entity references in `json` (a value of `type`) moved under `prefix` ("3": "2" becomes "3/2"),
// when a prefab is nested into another.
void prefix_entity_refs(nlohmann::json& json, const TypeInfo& type, const std::string& prefix) {
    switch (type.kind) {
        case TypeKind::Entity:
            if (json.is_string() || json.is_number_integer()) {
                json = prefix + "/" + (json.is_string() ? json.get<std::string>() : std::to_string(json.get<std::int64_t>()));
            }
            break;
        case TypeKind::Struct:
            if (json.is_object()) {
                for (const FieldInfo& field : type.fields) {
                    if (const auto found = json.find(field.name); found != json.end()) {
                        prefix_entity_refs(*found, *field.type, prefix);
                    }
                    for (const std::string& alias : field.aliases) {
                        if (const auto found = json.find(alias); found != json.end()) {
                            prefix_entity_refs(*found, *field.type, prefix);
                        }
                    }
                }
            }
            break;
        case TypeKind::Array:
            if (json.is_array()) {
                for (nlohmann::json& element : json) {
                    prefix_entity_refs(element, *type.element, prefix);
                }
            }
            break;
        case TypeKind::Optional:
            prefix_entity_refs(json, *type.element, prefix);
            break;
        default:
            break;
    }
}

// The placeholder of an entity of the prefab inside a component value: its index, until
// instantiate() maps it to the entity made for it.
entt::entity placeholder(std::size_t index) {
    return static_cast<entt::entity>(static_cast<entt::id_type>(index));
}

bool has_entity_fields(const TypeInfo& type) {
    switch (type.kind) {
        case TypeKind::Entity: return true;
        case TypeKind::Array:
        case TypeKind::Optional: return has_entity_fields(*type.element);
        case TypeKind::Struct:
            return std::any_of(type.fields.begin(), type.fields.end(),
                               [](const FieldInfo& field) { return has_entity_fields(*field.type); });
        default: return false;
    }
}

void map_entities(void* value, const TypeInfo& type, const std::vector<entt::entity>& entities) {
    switch (type.kind) {
        case TypeKind::Entity: {
            auto& entity = *static_cast<entt::entity*>(value);
            if (entity != entt::null) {
                const auto index = static_cast<std::size_t>(static_cast<entt::id_type>(entity));
                entity = index < entities.size() ? entities[index] : entt::entity{entt::null};
            }
            break;
        }
        case TypeKind::Array:
            for (std::size_t i = 0; i < type.array_size(value); ++i) {
                map_entities(type.array_at(value, i), *type.element, entities);
            }
            break;
        case TypeKind::Optional:
            if (type.optional_has(value)) {
                map_entities(type.optional_emplace(value), *type.element, entities);
            }
            break;
        case TypeKind::Struct:
            for (const FieldInfo& field : type.fields) {
                map_entities(field.in(value), *field.type, entities);
            }
            break;
        default:
            break;
    }
}

ReadOptions read_options(const Prefab::Context* context, const std::vector<Raw>* raws, const Prefab* prefab) {
    ReadOptions options;
    options.flags = kFieldData;
    if (context != nullptr) {
        options.asset = context->asset;
    }
    options.entity = [raws, prefab](const nlohmann::json& value, entt::entity& entity, std::string& problem) {
        std::string path;
        if (value.is_string()) {
            path = value.get<std::string>();
        } else if (value.is_number_integer()) {
            path = std::to_string(value.get<std::int64_t>());
        } else {
            problem = "expected the identifier of an entity of the prefab, found " + value.dump();
            return false;
        }
        std::int64_t index = -1;
        if (raws != nullptr) {
            for (std::size_t i = 0; i < raws->size(); ++i) {
                if ((*raws)[i].path == path) {
                    index = static_cast<std::int64_t>(i);
                }
            }
        } else if (prefab != nullptr) {
            index = prefab->find(path);
        }
        if (index < 0) {
            problem = "no entity '" + path + "' in the prefab";
            return false;
        }
        entity = placeholder(static_cast<std::size_t>(index));
        return true;
    };
    return options;
}

}  // namespace

std::int32_t Prefab::find(std::string_view path) const {
    for (std::size_t i = 0; i < entities_.size(); ++i) {
        if (entities_[i].path == path) {
            return static_cast<std::int32_t>(i);
        }
    }
    return -1;
}

Prefab Prefab::parse(std::string_view text, const std::string& key, const Context& context, DataIssues& issues) {
    nlohmann::json json;
    try {
        json = parse_document(text);
    } catch (const std::runtime_error& e) {
        throw std::runtime_error(key + ": " + e.what());
    }
    if (!json.is_object() || !json.contains("version") || json["version"] != kVersion) {
        throw std::runtime_error(key + ": expected \"version\": " + std::to_string(kVersion));
    }
    Prefab prefab;
    prefab.key_ = key;
    std::vector<Raw> raws;
    std::set<std::string> dependencies;
    const auto add_dependencies = [&dependencies](const Prefab& other) {
        dependencies.insert(other.key());
        dependencies.insert(other.dependencies().begin(), other.dependencies().end());
    };
    const auto find_raw = [&raws](const std::string& path) -> Raw* {
        for (Raw& raw : raws) {
            if (raw.path == path) {
                return &raw;
            }
        }
        return nullptr;
    };
    const auto from_prefab = [](const Prefab& other, const std::string& prefix, std::vector<Raw>& out) {
        for (const Entity& entity : other.entities()) {
            Raw raw;
            raw.path = prefix.empty() ? entity.path : prefix + "/" + entity.path;
            raw.name = entity.name;
            raw.parent = entity.parent < 0 ? std::string()
                                           : (prefix.empty() ? other.entities()[static_cast<std::size_t>(entity.parent)].path
                                                             : prefix + "/" + other.entities()[static_cast<std::size_t>(entity.parent)].path);
            raw.components = other.merged().contains(entity.path) ? other.merged()[entity.path] : nlohmann::json::object();
            if (!prefix.empty()) {
                for (auto& [name, value] : raw.components.items()) {
                    if (const ComponentType* type = component_types().find(name)) {
                        prefix_entity_refs(value, *type->type, prefix);
                    }
                }
            }
            out.push_back(std::move(raw));
        }
    };
    const auto apply_overrides = [&](const nlohmann::json& overrides, const std::string& prefix, const std::string& where) {
        if (!overrides.is_object()) {
            throw std::runtime_error(where + ": expected an object of entities");
        }
        for (const auto& [path, components] : overrides.items()) {
            const std::string full = prefix.empty() ? path : std::string(prefix).append("/").append(path);
            Raw* raw = find_raw(full);
            if (raw == nullptr) {
                throw std::runtime_error(std::string(where).append(" > ").append(path).append(": no such entity"));
            }
            if (!components.is_object()) {
                throw std::runtime_error(std::string(where).append(" > ").append(path).append(": expected an object of components"));
            }
            nlohmann::json patch = components;
            if (!prefix.empty()) {
                for (auto& [name, value] : patch.items()) {
                    if (const ComponentType* type = component_types().find(name)) {
                        prefix_entity_refs(value, *type->type, prefix);
                    }
                }
            }
            merge_fields(raw->components, patch);
        }
    };

    // 1. A variant starts from its base.
    if (const auto base = json.find("base"); base != json.end()) {
        if (!base->is_string()) {
            throw std::runtime_error(key + " > base: expected the path of a prefab");
        }
        const std::shared_ptr<const Prefab> parent = context.prefab(base->get<std::string>());
        add_dependencies(*parent);
        from_prefab(*parent, std::string(), raws);
        if (const auto removed = json.find("removed"); removed != json.end()) {
            if (!removed->is_array()) {
                throw std::runtime_error(key + " > removed: expected an array of entities");
            }
            for (const nlohmann::json& value : *removed) {
                const std::string path = path_text(value, key + " > removed");
                if (find_raw(path) == nullptr) {
                    throw std::runtime_error(std::string(key).append(" > removed > ").append(path).append(": no such entity in the base"));
                }
                // The entity, and everything under it (by path or by parent).
                std::set<std::string> gone = {path};
                bool grew = true;
                while (grew) {
                    grew = false;
                    for (const Raw& raw : raws) {
                        if (!gone.contains(raw.path) && (gone.contains(raw.parent) || raw.path.starts_with(path + "/"))) {
                            gone.insert(raw.path);
                            grew = true;
                        }
                    }
                }
                std::erase_if(raws, [&gone](const Raw& raw) { return gone.contains(raw.path); });
            }
        }
        if (const auto overrides = json.find("overrides"); overrides != json.end()) {
            apply_overrides(*overrides, std::string(), key + " > overrides");
        }
    } else if (json.contains("removed") || json.contains("overrides")) {
        throw std::runtime_error(key + ": \"removed\" and \"overrides\" are for a variant (with a \"base\")");
    }

    // 2. The file's own entities, nested prefabs expanded.
    if (const auto entities = json.find("entities"); entities != json.end()) {
        if (!entities->is_array()) {
            throw std::runtime_error(key + " > entities: expected an array");
        }
        for (std::size_t i = 0; i < entities->size(); ++i) {
            const nlohmann::json& entry = (*entities)[i];
            const std::string where = key + " > entities[" + std::to_string(i) + "]";
            if (!entry.is_object() || !entry.contains("id")) {
                throw std::runtime_error(where + ": expected an object with an \"id\"");
            }
            const std::string path = id_text(entry["id"], where + " > id");
            if (find_raw(path) != nullptr) {
                throw std::runtime_error(std::string(where).append(" > id: ").append(path).append(" is already used"));
            }
            std::string parent;
            if (const auto p = entry.find("parent"); p != entry.end() && !p->is_null()) {
                parent = path_text(*p, where + " > parent");
            }
            const nlohmann::json own = entry.value("components", nlohmann::json::object());
            if (!own.is_object()) {
                throw std::runtime_error(where + " > components: expected an object");
            }
            if (const auto nested = entry.find("prefab"); nested != entry.end()) {
                if (!nested->is_string()) {
                    throw std::runtime_error(where + " > prefab: expected the path of a prefab");
                }
                const std::shared_ptr<const Prefab> inner = context.prefab(nested->get<std::string>());
                add_dependencies(*inner);
                std::vector<Raw> added;
                from_prefab(*inner, path, added);
                // The nested root is this entity: its path, its parent, its name, its components over the root's.
                const std::string inner_root = added.front().path;
                for (Raw& raw : added) {
                    if (raw.path == inner_root) {
                        raw.path = path;
                        raw.parent = parent;
                        raw.name = entry.value("name", raw.name);
                        merge_fields(raw.components, own);
                    } else if (raw.parent == inner_root) {
                        raw.parent = path;
                    }
                }
                for (Raw& raw : added) {
                    raws.push_back(std::move(raw));
                }
                if (const auto overrides = entry.find("overrides"); overrides != entry.end()) {
                    apply_overrides(*overrides, path, where + " > overrides");
                }
            } else {
                if (entry.contains("overrides")) {
                    throw std::runtime_error(where + " > overrides: only for a nested prefab (with \"prefab\")");
                }
                Raw raw;
                raw.path = path;
                raw.parent = parent;
                raw.name = entry.value("name", std::string());
                raw.components = own;
                raws.push_back(std::move(raw));
            }
        }
    }

    // 3. One root, known parents, parents before children (the root first).
    std::vector<Raw> ordered;
    {
        std::size_t roots = 0;
        for (const Raw& raw : raws) {
            if (raw.parent.empty()) {
                ++roots;
            } else if (find_raw(raw.parent) == nullptr) {
                throw std::runtime_error(key + " > " + raw.path + " > parent: no entity '" + raw.parent + "'");
            }
        }
        if (roots != 1) {
            throw std::runtime_error(key + ": " + std::to_string(roots) + " entities without a parent (one root expected)");
        }
        std::set<std::string> placed;
        while (ordered.size() < raws.size()) {
            const std::size_t before = ordered.size();
            for (const Raw& raw : raws) {
                if (!placed.contains(raw.path) && (raw.parent.empty() || placed.contains(raw.parent))) {
                    placed.insert(raw.path);
                    ordered.push_back(raw);
                }
            }
            if (ordered.size() == before) {
                throw std::runtime_error(key + ": the parents of some entities form a cycle");
            }
        }
    }

    // 4. Each component read once by its description.
    const std::size_t errors = issues.errors();
    const std::size_t first_issue = issues.all().size();
    const ReadOptions options = read_options(&context, &ordered, nullptr);
    for (const Raw& raw : ordered) {
        Entity entity;
        entity.path = raw.path;
        entity.name = raw.name;
        for (std::size_t p = 0; p < ordered.size(); ++p) {
            if (ordered[p].path == raw.parent) {
                entity.parent = static_cast<std::int32_t>(p);
            }
        }
        prefab.merged_[raw.path] = raw.components;
        for (const auto& [name, value] : raw.components.items()) {
            const std::string where = std::string(key).append(" > ").append(raw.path).append(" > ").append(name);
            const ComponentType* type = component_types().find(name);
            if (type == nullptr) {
                issues.warning(where, "unknown component (not registered in component_types()): left out");
                continue;
            }
            auto object = std::make_shared<ScratchObject>(*type->type);
            read_json(type->upgraded(value), object->get(), *type->type, where, issues, options);
            entity.components.push_back({type, std::move(object)});
        }
        prefab.entities_.push_back(std::move(entity));
    }
    if (issues.errors() > errors) {
        std::string found;
        for (std::size_t i = first_issue; i < issues.all().size(); ++i) {
            const DataIssue& issue = issues.all()[i];
            if (issue.level == DataIssue::Level::Error) {
                found += "\n" + issue.where + ": " + issue.message;
            }
        }
        throw std::runtime_error(key + ": invalid prefab:" + found);
    }
    prefab.dependencies_.assign(dependencies.begin(), dependencies.end());
    return prefab;
}

PrefabInstance instantiate(World& world, const Prefab& prefab, const PrefabPlacement& placement, DataIssues* issues,
                           const Prefab::Context* context) {
    entt::registry& registry = world.registry();
    PrefabInstance instance;
    const std::vector<Prefab::Entity>& entities = prefab.entities();
    instance.entities.reserve(entities.size());
    for (std::size_t i = 0; i < entities.size(); ++i) {
        instance.entities.push_back(registry.create());
    }
    // The placement's overrides, read over copies of the prefab's values.
    std::map<std::pair<std::size_t, std::string>, std::shared_ptr<ScratchObject>> replaced;
    if (placement.overrides.is_object()) {
        DataIssues local;
        DataIssues& found = issues != nullptr ? *issues : local;
        const ReadOptions options = read_options(context, nullptr, &prefab);
        for (const auto& [path, components] : placement.overrides.items()) {
            const std::int32_t index = prefab.find(path);
            const std::string where = prefab.key() + " (instance) > " + path;
            if (index < 0) {
                found.error(where, "no such entity in the prefab");
                continue;
            }
            if (!components.is_object()) {
                found.error(where, "expected an object of components");
                continue;
            }
            for (const auto& [name, value] : components.items()) {
                const ComponentType* type = component_types().find(name);
                if (type == nullptr) {
                    found.warning(std::string(where).append(" > ").append(name), "unknown component: left out");
                    continue;
                }
                auto object = std::make_shared<ScratchObject>(*type->type);
                for (const Prefab::Component& component : entities[static_cast<std::size_t>(index)].components) {
                    if (component.type == type) {
                        type->type->copy(object->get(), component.value->get());
                    }
                }
                if (read_json(type->upgraded(value), object->get(), *type->type, std::string(where).append(" > ").append(name), found,
                              options)) {
                    replaced[{static_cast<std::size_t>(index), name}] = std::move(object);
                }
            }
        }
    }
    for (std::size_t i = 0; i < entities.size(); ++i) {
        const Prefab::Entity& source = entities[i];
        const entt::entity entity = instance.entities[i];
        std::vector<const ComponentType*> given;
        const auto give = [&](const ComponentType& type, const ScratchObject& value) {
            if (has_entity_fields(*type.type)) {
                ScratchObject mapped(*type.type);
                type.type->copy(mapped.get(), value.get());
                map_entities(mapped.get(), *type.type, instance.entities);
                type.assign(registry, entity, mapped.get());
            } else {
                type.assign(registry, entity, value.get());
            }
            given.push_back(&type);
        };
        for (const Prefab::Component& component : source.components) {
            const auto found = replaced.find({i, component.type->name});
            give(*component.type, found != replaced.end() ? *found->second : *component.value);
        }
        // Components the instance adds (overridden on an entity that did not have them).
        for (const auto& [where, object] : replaced) {
            const ComponentType* type = component_types().find(where.second);
            if (where.first == i && std::find(given.begin(), given.end(), type) == given.end()) {
                give(*type, *object);
            }
        }
        if (!source.name.empty() && !registry.all_of<Name>(entity)) {
            registry.emplace<Name>(entity, source.name);
        }
        if (source.parent >= 0) {
            registry.emplace_or_replace<Parent>(entity, instance.entities[static_cast<std::size_t>(source.parent)]);
        }
    }
    // The root, placed: the placement composed with its own Transform.
    instance.root = instance.entities.front();
    Transform own;
    if (const Transform* t = registry.try_get<Transform>(instance.root)) {
        own = *t;
    }
    Transform placed;
    placed.position = placement.position + placement.rotation * (placement.scale * own.position);
    placed.rotation = placement.rotation * own.rotation;
    placed.scale = placement.scale * own.scale;
    registry.emplace_or_replace<Transform>(instance.root, placed);
    if (placement.moving) {
        registry.emplace_or_replace<PreviousTransform>(instance.root, placed);
    }
    if (placement.parent != entt::null) {
        registry.emplace_or_replace<Parent>(instance.root, placement.parent);
    }
    return instance;
}

}  // namespace moteur
