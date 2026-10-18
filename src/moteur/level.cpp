#include "moteur/level.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

#include "moteur/color.hpp"
#include "moteur/document.hpp"
#include "moteur/map_data.hpp"
#include "moteur/prefab.hpp"
#include "moteur/reflection_io.hpp"
#include "moteur/renderer.hpp"

namespace moteur {

namespace {

glm::vec3 vec3_of(const nlohmann::json& value, const std::string& where, DataIssues& issues, glm::vec3 fallback) {
    if (value.is_array() && value.size() == 3 && value[0].is_number() && value[1].is_number() && value[2].is_number()) {
        return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
    }
    issues.error(where, "expected [x, y, z]");
    return fallback;
}

nlohmann::json json_of(glm::vec3 v) {
    return nlohmann::json::array({v.x, v.y, v.z});
}

nlohmann::json json_of(glm::quat q) {
    return nlohmann::json::array({q.w, q.x, q.y, q.z});
}

glm::quat quat_of(const nlohmann::json& value, const std::string& where, DataIssues& issues) {
    if (value.is_array() && value.size() == 4 && std::all_of(value.begin(), value.end(), [](const auto& v) { return v.is_number(); })) {
        const glm::quat q(value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>());
        const float length = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
        if (length > 1e-6f) {
            return glm::quat(q.w / length, q.x / length, q.y / length, q.z / length);
        }
    }
    issues.error(where, "expected a rotation [w, x, y, z]");
    return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

}  // namespace

LevelDocument LevelDocument::parse(std::string_view text, const std::string& key,
                                   const std::function<std::string(const nlohmann::json&)>& resolve, DataIssues& issues) {
    nlohmann::json json;
    try {
        json = parse_document(text);
    } catch (const std::runtime_error& e) {
        throw std::runtime_error(key + ": " + e.what());
    }
    if (!json.is_object()) {
        throw std::runtime_error(key + ": expected an object");
    }
    const int version = json.value("version", 0);
    if (version != kVersion) {
        throw std::runtime_error(key + ": level format " + std::to_string(version) + " (expected 3; the maps of version 1 and 2 "
                                 "are migrated with migrate_map())");
    }
    const std::size_t errors_before = issues.errors();
    LevelDocument doc;
    doc.layers.clear();
    doc.description = json.value("description", std::string());
    doc.chunk_size = json.value("chunk_size", 32.0f);
    if (!(doc.chunk_size >= 4.0f && doc.chunk_size <= 1024.0f)) {
        issues.error(key + " > chunk_size", "expected metres in [4, 1024]");
        doc.chunk_size = 32.0f;
    }
    const auto reference = [&](const nlohmann::json& value, const std::string& where) -> std::string {
        try {
            return resolve(value);
        } catch (const std::exception& e) {
            issues.error(where, e.what());
            return {};
        }
    };

    std::set<std::string> layer_names;
    if (const auto layers = json.find("layers"); layers != json.end() && layers->is_array()) {
        for (std::size_t i = 0; i < layers->size(); ++i) {
            const nlohmann::json& l = (*layers)[i];
            const std::string where = key + " > layers > " + std::to_string(i);
            LevelLayer layer;
            layer.name = l.value("name", std::string());
            layer.visible = l.value("visible", true);
            layer.locked = l.value("locked", false);
            if (layer.name.empty() || !layer_names.insert(layer.name).second) {
                issues.error(where, "a layer needs a name of its own");
                continue;
            }
            doc.layers.push_back(layer);
        }
    }
    if (doc.layers.empty()) {
        doc.layers.push_back(LevelLayer{"default"});
        layer_names.insert("default");
    }

    std::set<std::uint32_t> ids;
    std::uint32_t highest = 0;
    const auto take_id = [&](const nlohmann::json& value, const std::string& where) -> std::uint32_t {
        if (!value.is_number_unsigned() || value.get<std::uint64_t>() == 0 || value.get<std::uint64_t>() > 0xFFFFFFFFull) {
            issues.error(where + " > id", "expected an identifier (a whole number above 0)");
            return 0;
        }
        const auto id = value.get<std::uint32_t>();
        if (!ids.insert(id).second) {
            issues.error(where + " > id", "identifier " + std::to_string(id) + " used twice");
            return 0;
        }
        highest = std::max(highest, id);
        return id;
    };
    if (const auto instances = json.find("instances"); instances != json.end() && instances->is_array()) {
        for (std::size_t i = 0; i < instances->size(); ++i) {
            const nlohmann::json& e = (*instances)[i];
            const std::string where = key + " > instances > " + std::to_string(i);
            if (!e.is_object()) {
                issues.error(where, "expected an object");
                continue;
            }
            LevelInstance instance;
            instance.id = take_id(e.value("id", nlohmann::json()), where);
            if (const auto prefab = e.find("prefab"); prefab != e.end()) {
                instance.prefab = reference(*prefab, where + " > prefab");
            } else {
                issues.error(where, "an instance needs a prefab");
            }
            instance.layer = e.value("layer", std::string("default"));
            if (!layer_names.contains(instance.layer)) {
                issues.warning(where + " > layer", "unknown layer '" + instance.layer + "': added");
                layer_names.insert(instance.layer);
                doc.layers.push_back(LevelLayer{instance.layer});
            }
            if (const auto transform = e.find("transform"); transform != e.end()) {
                read_json(*transform, instance.transform, where + " > transform", issues, {.flags = kFieldData});
            }
            if (const auto overrides = e.find("overrides"); overrides != e.end()) {
                if (overrides->is_object()) {
                    instance.overrides = *overrides;
                } else {
                    issues.error(where + " > overrides", "expected an object");
                }
            }
            if (const auto properties = e.find("properties"); properties != e.end()) {
                if (properties->is_object()) {
                    instance.properties = *properties;
                } else {
                    issues.error(where + " > properties", "expected an object");
                }
            }
            if (instance.id != 0 && !instance.prefab.empty()) {
                doc.instances.push_back(std::move(instance));
            }
        }
    }
    std::set<std::string> point_names;
    if (const auto points = json.find("points"); points != json.end() && points->is_array()) {
        for (std::size_t i = 0; i < points->size(); ++i) {
            const nlohmann::json& p = (*points)[i];
            const std::string where = key + " > points > " + std::to_string(i);
            LevelPoint point;
            point.name = p.value("name", std::string());
            if (point.name.empty() || !point_names.insert(point.name).second) {
                issues.error(where, "a point needs a name of its own");
                continue;
            }
            point.position = vec3_of(p.value("position", nlohmann::json()), where + " > position", issues, glm::vec3(0.0f));
            if (p.contains("rotation")) {
                point.rotation = quat_of(p["rotation"], where + " > rotation", issues);
            }
            doc.points.push_back(point);
        }
    }
    if (const auto volumes = json.find("volumes"); volumes != json.end() && volumes->is_array()) {
        for (std::size_t i = 0; i < volumes->size(); ++i) {
            const nlohmann::json& v = (*volumes)[i];
            const std::string where = key + " > volumes > " + std::to_string(i);
            LevelVolume volume;
            volume.id = take_id(v.value("id", nlohmann::json()), where);
            volume.kind = v.value("kind", std::string());
            volume.box.add(vec3_of(v.value("min", nlohmann::json()), where + " > min", issues, glm::vec3(0.0f)));
            volume.box.add(vec3_of(v.value("max", nlohmann::json()), where + " > max", issues, glm::vec3(0.0f)));
            if (const auto properties = v.find("properties"); properties != v.end() && properties->is_object()) {
                volume.properties = *properties;
            }
            if (volume.id != 0) {
                doc.volumes.push_back(std::move(volume));
            }
        }
    }
    if (const auto ground = json.find("ground"); ground != json.end() && ground->is_object()) {
        const std::string where = key + " > ground";
        LevelGround& g = doc.ground;
        const nlohmann::json origin = ground->value("origin", nlohmann::json::array({0.0, 0.0}));
        if (origin.is_array() && origin.size() == 2 && origin[0].is_number() && origin[1].is_number()) {
            g.origin = {origin[0].get<float>(), origin[1].get<float>()};
        } else {
            issues.error(where + " > origin", "expected [x, z]");
        }
        g.cell_size = ground->value("cell_size", 8.0f);
        g.width = ground->value("width", 0);
        g.height = ground->value("height", 0);
        g.level = ground->value("level", 0.0f);
        if (!(g.cell_size > 0.0f) || g.width < 0 || g.height < 0 || g.width > 4096 || g.height > 4096) {
            issues.error(where, "expected a positive cell size and a size of at most 4096 x 4096 cells");
            g = LevelGround{};
        }
        if (const auto prefabs = ground->find("prefabs"); prefabs != ground->end() && prefabs->is_array()) {
            for (std::size_t i = 0; i < prefabs->size(); ++i) {
                g.prefabs.push_back(reference((*prefabs)[i], where + " > prefabs > " + std::to_string(i)));
            }
        }
        if (const auto cells = ground->find("cells"); cells != ground->end() && cells->is_array()) {
            for (const nlohmann::json& c : *cells) {
                const int value = c.is_number_integer() ? c.get<int>() : -1;
                if (value < 0 || value > static_cast<int>(g.prefabs.size())) {
                    issues.error(where + " > cells", "a cell names no ground prefab");
                    break;
                }
                g.cells.push_back(static_cast<std::uint8_t>(value));
            }
        }
        if (g.cells.size() != static_cast<std::size_t>(g.width) * static_cast<std::size_t>(g.height)) {
            issues.error(where + " > cells", "expected " + std::to_string(g.width * g.height) + " cells (width x height)");
            g.cells.assign(static_cast<std::size_t>(g.width) * static_cast<std::size_t>(g.height), 0);
        }
    }
    if (const auto lighting = json.find("lighting"); lighting != json.end()) {
        read_json(*lighting, doc.lighting, key + " > lighting", issues, {.flags = kFieldData});
    }
    if (const auto navigation = json.find("navigation"); navigation != json.end() && navigation->is_object()) {
        const std::string where = key + " > navigation";
        if (const auto agents = navigation->find("agents"); agents != navigation->end() && agents->is_array()) {
            doc.agents.clear();
            for (std::size_t i = 0; i < agents->size(); ++i) {
                NavAgent agent;
                read_json((*agents)[i], agent, where + " > agents > " + std::to_string(i), issues, {.flags = kFieldData});
                doc.agents.push_back(agent);
            }
        }
        if (const auto build = navigation->find("build"); build != navigation->end()) {
            read_json(*build, doc.navigation, where + " > build", issues, {.flags = kFieldData});
        }
    }
    if (doc.agents.empty()) {
        doc.agents.emplace_back();
    }
    doc.navigation.tile_size = doc.chunk_size;
    doc.next_id_ = std::max(json.value("next_id", 1u), highest + 1);
    if (issues.errors() > errors_before) {
        throw std::runtime_error(key + ": " + std::to_string(issues.errors() - errors_before) + " error(s) in the level");
    }
    return doc;
}

nlohmann::json LevelDocument::to_json(const std::function<nlohmann::json(const std::string&)>& reference) const {
    nlohmann::json json = nlohmann::json::object();
    json["version"] = kVersion;
    if (!description.empty()) {
        json["description"] = description;
    }
    json["chunk_size"] = chunk_size;
    json["next_id"] = next_id_;
    nlohmann::json layer_list = nlohmann::json::array();
    for (const LevelLayer& layer : layers) {
        layer_list.push_back({{"name", layer.name}, {"visible", layer.visible}, {"locked", layer.locked}});
    }
    json["layers"] = layer_list;
    nlohmann::json instance_list = nlohmann::json::array();
    for (const LevelInstance& instance : instances) {
        nlohmann::json e = {{"id", instance.id}, {"prefab", reference(instance.prefab)}, {"layer", instance.layer},
                            {"transform", write_json(instance.transform, {.flags = kFieldData})}};
        if (!instance.overrides.empty()) {
            e["overrides"] = instance.overrides;
        }
        if (!instance.properties.empty()) {
            e["properties"] = instance.properties;
        }
        instance_list.push_back(std::move(e));
    }
    json["instances"] = instance_list;
    nlohmann::json point_list = nlohmann::json::array();
    for (const LevelPoint& point : points) {
        point_list.push_back({{"name", point.name}, {"position", json_of(point.position)}, {"rotation", json_of(point.rotation)}});
    }
    json["points"] = point_list;
    nlohmann::json volume_list = nlohmann::json::array();
    for (const LevelVolume& volume : volumes) {
        volume_list.push_back({{"id", volume.id}, {"kind", volume.kind}, {"min", json_of(volume.box.min)},
                               {"max", json_of(volume.box.max)}, {"properties", volume.properties}});
    }
    json["volumes"] = volume_list;
    if (ground.width > 0 && ground.height > 0) {
        nlohmann::json prefab_list = nlohmann::json::array();
        for (const std::string& prefab : ground.prefabs) {
            prefab_list.push_back(reference(prefab));
        }
        json["ground"] = {{"origin", nlohmann::json::array({ground.origin.x, ground.origin.y})},
                          {"cell_size", ground.cell_size},
                          {"width", ground.width},
                          {"height", ground.height},
                          {"level", ground.level},
                          {"prefabs", prefab_list},
                          {"cells", ground.cells}};
    }
    nlohmann::json agent_list = nlohmann::json::array();
    for (const NavAgent& agent : agents) {
        agent_list.push_back(write_json(agent, {.flags = kFieldData}));
    }
    NavBuildSettings build = navigation;
    build.tile_size = chunk_size;  // a tile is a chunk, whatever was set (as parse() reads it)
    json["navigation"] = {{"agents", agent_list}, {"build", write_json(build, {.flags = kFieldData})}};
    json["lighting"] = write_json(lighting, {.flags = kFieldData});
    return json;
}

LevelLighting night_lighting(const LevelLighting& day) {
    LevelLighting night = day;
    night.sun_elevation = std::max(day.sun_elevation, 35.0f);
    night.sun_color = {0.62f, 0.72f, 1.0f};
    night.sun_intensity = 0.12f;
    night.sky_intensity = 0.06f;
    night.background = {0.01f, 0.012f, 0.02f};
    return night;
}

glm::vec3 sun_direction(const LevelLighting& lighting) {
    // Once a frame, not the logic: trigonometry is fine here.
    const float yaw = glm::radians(lighting.sun_yaw);
    const float elevation = glm::radians(lighting.sun_elevation);
    return {std::cos(elevation) * std::cos(yaw), std::sin(elevation), std::cos(elevation) * std::sin(yaw)};
}

void apply_lighting(Renderer& renderer, const LevelLighting& lighting, const Environment* sky) {
    MeshRenderer& meshes = renderer.meshes();
    meshes.set_sun(sun_direction(lighting), srgb_to_linear(lighting.sun_color), lighting.sun_intensity);
    meshes.set_environment(sky, lighting.sky_intensity);
    renderer.set_exposure(lighting.exposure);
    renderer.set_clear_color(lighting.background.r, lighting.background.g, lighting.background.b);
}

std::uint32_t LevelDocument::add(LevelInstance instance) {
    instance.id = next_id_++;
    if (instance.layer.empty()) {
        instance.layer = layers.empty() ? std::string("default") : layers.front().name;
    }
    instances.push_back(std::move(instance));
    return instances.back().id;
}

std::uint32_t LevelDocument::add(LevelVolume volume) {
    volume.id = next_id_++;
    volumes.push_back(std::move(volume));
    return volumes.back().id;
}

const LevelInstance* LevelDocument::instance(std::uint32_t id) const {
    for (const LevelInstance& instance : instances) {
        if (instance.id == id) {
            return &instance;
        }
    }
    return nullptr;
}

const LevelPoint* LevelDocument::point(std::string_view name) const {
    for (const LevelPoint& point : points) {
        if (point.name == name) {
            return &point;
        }
    }
    return nullptr;
}

Aabb LevelDocument::bounds() const {
    Aabb box;
    for (const LevelInstance& instance : instances) {
        box.add(instance.transform.position);
    }
    for (const LevelPoint& point : points) {
        box.add(point.position);
    }
    for (const LevelVolume& volume : volumes) {
        box.add(volume.box.min);
        box.add(volume.box.max);
    }
    if (ground.width > 0 && ground.height > 0) {
        box.add({ground.origin.x, ground.level, ground.origin.y});
        box.add({ground.origin.x + ground.cell_size * static_cast<float>(ground.width), ground.level,
                 ground.origin.y + ground.cell_size * static_cast<float>(ground.height)});
    }
    return box;
}

// --- Migration -----------------------------------------------------------------------------------

LevelDocument migrate_map(const MapData& map, const MapMigration& rules, DataIssues& issues) {
    LevelDocument doc;
    doc.description = "Migrated from a map of version 1 or 2";
    doc.chunk_size = rules.chunk_size;
    const TileMap& tiles = map.tiles();
    // A cell holds the type `name` on any of its layers.
    const auto holds = [&](glm::ivec2 cell, TileId id) {
        for (int layer = 0; layer < tiles.layer_count(); ++layer) {
            if (tiles.at(layer, cell) == id) {
                return true;
            }
        }
        return false;
    };
    // Runs of cells of one type along x, row after row: one scaled instance each.
    const auto runs = [&](TileId id, const std::string& prefab, float y_scale) {
        for (int z = 0; z < tiles.height(); ++z) {
            int x = 0;
            while (x < tiles.width()) {
                if (!holds({x, z}, id)) {
                    ++x;
                    continue;
                }
                int end = x;
                while (end < tiles.width() && holds({end, z}, id)) {
                    ++end;
                }
                LevelInstance instance;
                instance.prefab = prefab;
                instance.transform.position = {static_cast<float>(x + end) * 0.5f, 0.0f, static_cast<float>(z) + 0.5f};
                instance.transform.scale = {static_cast<float>(end - x), y_scale, 1.0f};
                doc.add(std::move(instance));
                x = end;
            }
        }
    };
    if (!rules.ground_tile.empty() && !rules.ground_prefab.empty()) {
        const TileId ground = map.tile_id(rules.ground_tile);
        if (ground == kNoTile) {
            issues.warning("migration", "no tile type '" + rules.ground_tile + "' for the ground");
        } else {
            runs(ground, rules.ground_prefab, 1.0f);
        }
    }
    for (const auto& [name, prefab] : rules.tiles) {
        const TileId id = map.tile_id(name);
        if (id == kNoTile) {
            issues.warning("migration", "no tile type '" + name + "' in the map");
            continue;
        }
        runs(id, prefab, 1.0f);
    }
    for (const auto& [name, cells] : map.points()) {
        for (std::size_t i = 0; i < cells.size(); ++i) {
            LevelPoint point;
            point.name = cells.size() == 1 ? name : name + "_" + std::to_string(i + 1);
            point.position = {static_cast<float>(cells[i].x) + 0.5f, 0.0f, static_cast<float>(cells[i].y) + 0.5f};
            doc.points.push_back(point);
        }
    }
    for (const MapObject& object : map.objects()) {
        if (std::find(rules.object_points.begin(), rules.object_points.end(), object.type) != rules.object_points.end()) {
            LevelPoint point;
            point.name = object.type + "_" + object.id;
            point.position = object.position;
            doc.points.push_back(point);
            continue;
        }
        const auto found = rules.objects.find(object.type);
        if (found == rules.objects.end()) {
            issues.warning("migration > objects > " + object.id, "no prefab for the type '" + object.type + "': left out");
            continue;
        }
        LevelInstance instance;
        instance.prefab = found->second;
        instance.transform.position = object.position;
        // Degrees about y into a quaternion: a tool's conversion, not the logic's (no rule against
        // trigonometry here).
        const float half = object.rotation * 0.5f * 0.0174532925f;
        instance.transform.rotation = glm::quat(std::cos(half), 0.0f, std::sin(half), 0.0f);
        instance.transform.scale = glm::vec3(object.scale);
        if (!object.props.empty()) {
            instance.properties = object.props;  // the game's data, not fields of the prefab
        }
        doc.add(std::move(instance));
    }
    for (const MapConnector& connector : map.connectors()) {
        LevelVolume volume;
        volume.kind = "connector";
        volume.box.add({static_cast<float>(connector.cell.x), 0.0f, static_cast<float>(connector.cell.y)});
        volume.box.add({static_cast<float>(connector.cell.x + 1), 2.0f, static_cast<float>(connector.cell.y + 1)});
        static const char* const kDirections[] = {"north", "east", "south", "west"};
        volume.properties = {{"name", connector.name},
                             {"kind", connector.kind},
                             {"direction", kDirections[static_cast<int>(connector.direction)]}};
        doc.add(std::move(volume));
    }
    doc.navigation.tile_size = doc.chunk_size;
    return doc;
}

// --- LevelState ----------------------------------------------------------------------------------

void LevelState::set(std::uint32_t id, const nlohmann::json& overrides) {
    nlohmann::json& target = overrides_[id];
    if (!target.is_object()) {
        target = nlohmann::json::object();
    }
    merge_fields(target, overrides);
}

void LevelState::remove(std::uint32_t id) {
    const auto at = std::lower_bound(removed_.begin(), removed_.end(), id);
    if (at == removed_.end() || *at != id) {
        removed_.insert(at, id);
    }
}

bool LevelState::removed(std::uint32_t id) const {
    return std::binary_search(removed_.begin(), removed_.end(), id);
}

const nlohmann::json& LevelState::overrides(std::uint32_t id) const {
    static const nlohmann::json kNone = nlohmann::json::object();
    const auto found = overrides_.find(id);
    return found != overrides_.end() ? found->second : kNone;
}

void LevelState::clear() {
    overrides_.clear();
    removed_.clear();
}

nlohmann::json LevelState::to_json() const {
    nlohmann::json overrides = nlohmann::json::object();
    for (const auto& [id, value] : overrides_) {
        overrides[std::to_string(id)] = value;
    }
    return {{"overrides", overrides}, {"removed", removed_}};
}

LevelState LevelState::from_json(const nlohmann::json& json) {
    LevelState state;
    try {
        for (const auto& [id, value] : json.at("overrides").items()) {
            state.overrides_[static_cast<std::uint32_t>(std::stoul(id))] = value;
        }
        state.removed_ = json.at("removed").get<std::vector<std::uint32_t>>();
        std::sort(state.removed_.begin(), state.removed_.end());
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("level state: ") + e.what());
    }
    return state;
}

}  // namespace moteur
