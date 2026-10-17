#include "moteur/world_save.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <string_view>

#include "moteur/animator.hpp"
#include "moteur/collision.hpp"
#include "moteur/movement.hpp"
#include "moteur/world.hpp"

namespace moteur {

namespace {

// Enumerations are saved by name (review of milestones 1 to 7, 7.2): a number would silently
// change meaning the day the enumeration is reordered. The names are the file's, never renamed.
constexpr std::array<std::string_view, 4> kMoveModes = {"stop", "to_point", "direct", "follow_field"};
constexpr std::array<std::string_view, 6> kMoveStates = {"idle", "waiting", "moving", "arrived", "blocked", "no_path"};
constexpr std::array<std::string_view, 3> kPathStatuses = {"found", "partial", "none"};
static_assert(static_cast<std::size_t>(MoveMode::FollowField) + 1 == kMoveModes.size(), "a MoveMode without a saved name");
static_assert(static_cast<std::size_t>(MoveState::NoPath) + 1 == kMoveStates.size(), "a MoveState without a saved name");
static_assert(static_cast<std::size_t>(PathStatus::None) + 1 == kPathStatuses.size(), "a PathStatus without a saved name");

template <typename Enum, std::size_t N>
std::string name_of(Enum value, const std::array<std::string_view, N>& names) {
    return std::string(names.at(static_cast<std::size_t>(value)));
}

// A name of `names`; the number of saves written before names (development saves of milestone 7)
// is still read.
template <typename Enum, std::size_t N>
Enum enum_from_json(const nlohmann::json& j, const std::array<std::string_view, N>& names, const char* what) {
    if (j.is_number_integer()) {
        const auto index = j.get<std::int64_t>();
        if (index >= 0 && static_cast<std::size_t>(index) < N) {
            return static_cast<Enum>(index);
        }
    } else if (j.is_string()) {
        const std::string text = j.get<std::string>();
        for (std::size_t i = 0; i < N; ++i) {
            if (names[i] == text) {
                return static_cast<Enum>(i);
            }
        }
    }
    throw std::runtime_error(std::string("unknown ") + what + " " + j.dump());
}

}  // namespace

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

void ComponentSerializers::add_engine_components() {
    add<Transform>(
        "transform",
        [](const Transform& t, const SaveContext&) {
            return nlohmann::json{{"p", to_json_value(t.position)}, {"r", to_json_value(t.rotation)}, {"s", to_json_value(t.scale)}};
        },
        [](entt::registry& r, entt::entity e, const nlohmann::json& j, const SaveContext&) {
            Transform t;
            t.position = vec3_from_json(j.at("p"));
            t.rotation = quat_from_json(j.at("r"));
            t.scale = vec3_from_json(j.at("s"));
            r.emplace_or_replace<Transform>(e, t);
        });
    add<Name>(
        "name", [](const Name& n, const SaveContext&) { return nlohmann::json(n.value); },
        [](entt::registry& r, entt::entity e, const nlohmann::json& j, const SaveContext&) {
            r.emplace_or_replace<Name>(e, j.get<std::string>());
        });
    add_tag<Hidden>("hidden");
    add<Parent>(
        "parent", [](const Parent& p, const SaveContext& c) { return nlohmann::json(c.id_of(p.entity)); },
        [](entt::registry& r, entt::entity e, const nlohmann::json& j, const SaveContext& c) {
            const entt::entity parent = c.entity_of(j.get<std::uint64_t>());
            if (parent == entt::null) {
                throw std::runtime_error("parent " + j.dump() + " missing from the save");
            }
            r.emplace_or_replace<Parent>(e, parent);
        });
    add<BoneAttachment>(
        "bone_attachment", [](const BoneAttachment& b, const SaveContext&) { return nlohmann::json(b.point); },
        [](entt::registry& r, entt::entity e, const nlohmann::json& j, const SaveContext&) {
            r.emplace_or_replace<BoneAttachment>(e, j.get<std::string>());
        });
    add<LightSource>(
        "light",
        [](const LightSource& l, const SaveContext&) {
            return nlohmann::json{to_json_value(l.color), l.intensity, l.range, l.casts_shadows};
        },
        [](entt::registry& r, entt::entity e, const nlohmann::json& j, const SaveContext&) {
            r.emplace_or_replace<LightSource>(e, vec3_from_json(j.at(0)), j.at(1).get<float>(), j.at(2).get<float>(),
                                              j.at(3).get<bool>());
        });
    add<Collider>(
        "collider",
        [](const Collider& c, const SaveContext&) {
            return nlohmann::json{c.radius, c.layer, c.mask, c.push_weight, c.blocked_by_walls};
        },
        [](entt::registry& r, entt::entity e, const nlohmann::json& j, const SaveContext&) {
            Collider c;
            c.radius = j.at(0).get<float>();
            c.layer = j.at(1).get<std::uint32_t>();
            c.mask = j.at(2).get<std::uint32_t>();
            c.push_weight = j.at(3).get<int>();
            c.blocked_by_walls = j.at(4).get<bool>();
            r.emplace_or_replace<Collider>(e, c);
        });
    add<Mover>(
        "mover",
        [](const Mover& m, const SaveContext&) {
            nlohmann::json path = nlohmann::json::array();
            for (const glm::vec2& p : m.path) {
                path.push_back(to_json_value(p));
            }
            return nlohmann::json{{"speed", m.speed},
                                  {"turn", m.turn_rate},
                                  {"stop", m.stop_distance},
                                  {"field", m.field},
                                  {"mode", name_of(m.mode, kMoveModes)},
                                  {"goal", to_json_value(m.goal)},
                                  {"direction", to_json_value(m.direction)},
                                  {"path", path},
                                  {"next", m.next_point},
                                  {"status", name_of(m.path_status, kPathStatuses)},
                                  {"needs_path", m.needs_path},
                                  {"state", name_of(m.state, kMoveStates)},
                                  {"facing", to_json_value(m.facing)},
                                  {"actual", m.actual_speed},
                                  {"start", to_json_value(m.tick_start)},
                                  {"slow", m.slow_ticks}};
        },
        [](entt::registry& r, entt::entity e, const nlohmann::json& j, const SaveContext&) {
            Mover m;
            m.speed = j.at("speed").get<float>();
            m.turn_rate = j.at("turn").get<float>();
            m.stop_distance = j.at("stop").get<float>();
            m.field = j.at("field").get<std::uint8_t>();
            m.mode = enum_from_json<MoveMode>(j.at("mode"), kMoveModes, "move mode");
            m.goal = vec2_from_json(j.at("goal"));
            m.direction = vec2_from_json(j.at("direction"));
            for (const nlohmann::json& p : j.at("path")) {
                m.path.push_back(vec2_from_json(p));
            }
            m.next_point = j.at("next").get<std::size_t>();
            m.path_status = enum_from_json<PathStatus>(j.at("status"), kPathStatuses, "path status");
            m.needs_path = j.at("needs_path").get<bool>();
            m.state = enum_from_json<MoveState>(j.at("state"), kMoveStates, "move state");
            m.facing = vec2_from_json(j.at("facing"));
            m.actual_speed = j.at("actual").get<float>();
            m.tick_start = vec2_from_json(j.at("start"));
            m.slow_ticks = j.at("slow").get<int>();
            r.emplace_or_replace<Mover>(e, std::move(m));
        });
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
