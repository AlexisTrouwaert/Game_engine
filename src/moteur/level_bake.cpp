#include "moteur/level_bake.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <stdexcept>

#include "moteur/binary_io.hpp"
#include "moteur/document.hpp"
#include "moteur/level_collision.hpp"
#include "moteur/prefab.hpp"
#include "moteur/profiler.hpp"
#include "moteur/reflection_io.hpp"

namespace moteur {

namespace {

const char kHeaderMagic[4] = {'M', 'L', 'V', 'L'};
const char kChunkMagic[4] = {'M', 'L', 'V', 'C'};
constexpr std::uint32_t kFormatVersion = kPreparedLevelVersion;

std::uint64_t fnv1a(const std::uint8_t* data, std::size_t size) {
    std::uint64_t hash = 14695981039346656037ull;
    for (std::size_t i = 0; i < size; ++i) {
        hash = (hash ^ data[i]) * 1099511628211ull;
    }
    return hash;
}

// Appends the FNV-1a of everything after the magic and the version.
void seal(std::vector<std::uint8_t>& bytes) {
    const std::uint64_t checksum = fnv1a(bytes.data() + 8, bytes.size() - 8);
    for (int i = 0; i < 8; ++i) {
        bytes.push_back(static_cast<std::uint8_t>((checksum >> (8 * i)) & 0xFF));
    }
}

// Checks the magic, the version and the checksum; returns the reader positioned after the version.
BinaryReader open_sealed(const std::vector<std::uint8_t>& bytes, const char magic[4], const char* what, const std::string& name) {
    if (bytes.size() < 16 || std::memcmp(bytes.data(), magic, 4) != 0) {
        throw std::runtime_error(name + ": not a " + what);
    }
    std::uint64_t checksum = 0;
    for (int i = 0; i < 8; ++i) {
        checksum |= static_cast<std::uint64_t>(bytes[bytes.size() - 8 + static_cast<std::size_t>(i)]) << (8 * i);
    }
    if (fnv1a(bytes.data() + 8, bytes.size() - 16) != checksum) {
        throw std::runtime_error(name + ": damaged " + what + " (checksum)");
    }
    BinaryReader in(bytes.data(), bytes.size() - 8, name);
    char read_magic[4];
    in.raw(read_magic, 4);
    if (const std::uint32_t version = in.u32(); version != kFormatVersion) {
        in.fail(std::string(what) + " format " + std::to_string(version) + " (expected " + std::to_string(kFormatVersion) + ")");
    }
    return in;
}

nlohmann::json json_of(glm::vec3 v) {
    return nlohmann::json::array({v.x, v.y, v.z});
}

glm::vec3 vec3_of(const nlohmann::json& v) {
    return {v.at(0).get<float>(), v.at(1).get<float>(), v.at(2).get<float>()};
}

// A component of an entity of a prefab's merged values, or null.
const nlohmann::json* component(const nlohmann::json& merged, const std::string& path, const char* name) {
    const auto entity = merged.find(path);
    if (entity == merged.end() || !entity->is_object()) {
        return nullptr;
    }
    const auto found = entity->find(name);
    return found != entity->end() ? &*found : nullptr;
}

}  // namespace

void prefab_collision(const Prefab& prefab, const Transform& transform, const nlohmann::json& overrides,
                      const LevelSources& sources, CollisionMeshData& out) {
    nlohmann::json merged = prefab.merged();
    if (overrides.is_object()) {
        for (const auto& [path, components] : overrides.items()) {
            if (merged.contains(path)) {
                merge_fields(merged[path], components);
            }
        }
    }
    const std::vector<Prefab::Entity>& entities = prefab.entities();
    std::vector<glm::mat4> world(entities.size(), glm::mat4(1.0f));
    for (std::size_t i = 0; i < entities.size(); ++i) {
        const Prefab::Entity& entity = entities[i];
        Transform local;
        if (const nlohmann::json* t = component(merged, entity.path, "transform")) {
            DataIssues ignored;
            read_json(*t, local, entity.path, ignored, {.flags = kFieldData});
        }
        world[i] = entity.parent < 0 ? transform.matrix() * local.matrix()
                                     : world[static_cast<std::size_t>(entity.parent)] * local.matrix();
        const nlohmann::json* collision_json = component(merged, entity.path, "static_collision");
        if (collision_json == nullptr) {
            continue;
        }
        StaticCollision collision;
        DataIssues issues;
        read_json(*collision_json, collision, prefab.key() + " > " + entity.path + " > static_collision", issues,
                  {.flags = kFieldData});
        if (collision.from_model) {
            if (const nlohmann::json* model = component(merged, entity.path, "model"); model != nullptr && model->contains("model")) {
                const nlohmann::json& reference = (*model)["model"];
                if (!reference.is_null() && !(reference.is_string() && reference.get<std::string>().empty())) {
                    if (const CollisionMeshData* mesh = sources.model_collision(sources.resolve(reference))) {
                        out.append(*mesh, world[i]);
                    }
                }
            }
        }
        if (!collision.boxes.empty()) {
            CollisionMeshData boxes;
            for (const CollisionBox& box : collision.boxes) {
                Aabb aabb;
                aabb.add(box.centre - box.half_size);
                aabb.add(box.centre + box.half_size);
                boxes.add_box(aabb, box.top, box.sides);
            }
            out.append(boxes, world[i]);
        }
    }
}

glm::ivec2 LevelHeader::chunk_of(glm::vec3 point) const {
    const int x = static_cast<int>(std::floor((point.x - origin.x) / chunk_size));
    const int z = static_cast<int>(std::floor((point.z - origin.z) / chunk_size));
    return {std::clamp(x, 0, std::max(chunks_x - 1, 0)), std::clamp(z, 0, std::max(chunks_z - 1, 0))};
}

bool LevelHeader::has_chunk(glm::ivec2 coord) const {
    if (coord.x < 0 || coord.y < 0 || coord.x >= chunks_x || coord.y >= chunks_z) {
        return false;
    }
    const std::size_t index = static_cast<std::size_t>(coord.y) * static_cast<std::size_t>(chunks_x) + static_cast<std::size_t>(coord.x);
    return index < present.size() && present[index] != 0;
}

const LevelPoint* LevelHeader::point(const std::string& name) const {
    for (const LevelPoint& p : points) {
        if (p.name == name) {
            return &p;
        }
    }
    return nullptr;
}

BakedLevel bake_level(const LevelDocument& level, const LevelSources& sources, LevelBakeStats* stats) {
    MOTEUR_PROFILE("level: bake");
    const Uint64 start = SDL_GetPerformanceCounter();
    LevelBakeStats counts;
    // Every instance, the ground's tiles as instances without identity.
    std::vector<ChunkInstance> all;
    all.reserve(level.instances.size());
    for (const LevelInstance& instance : level.instances) {
        all.push_back({instance.id, instance.prefab, instance.transform, instance.overrides, instance.properties});
    }
    const LevelGround& ground = level.ground;
    for (int z = 0; z < ground.height; ++z) {
        for (int x = 0; x < ground.width; ++x) {
            const std::uint8_t cell = ground.cells[static_cast<std::size_t>(z) * static_cast<std::size_t>(ground.width) + static_cast<std::size_t>(x)];
            if (cell == 0 || cell > ground.prefabs.size()) {
                continue;
            }
            ChunkInstance tile;
            tile.prefab = ground.prefabs[cell - 1u];
            tile.transform.position = {ground.origin.x + ground.cell_size * static_cast<float>(x), ground.level,
                                       ground.origin.y + ground.cell_size * static_cast<float>(z)};
            all.push_back(std::move(tile));
        }
    }
    counts.instances = static_cast<int>(all.size());

    // Their collision, in world space.
    CollisionMeshData collision;
    std::map<std::string, std::shared_ptr<const Prefab>> prefabs;
    for (const ChunkInstance& instance : all) {
        auto& prefab = prefabs[instance.prefab];
        if (!prefab) {
            try {
                prefab = sources.prefab(instance.prefab);
            } catch (const std::exception& e) {
                throw std::runtime_error("level instance " + std::to_string(instance.id) + ": " + e.what());
            }
        }
        prefab_collision(*prefab, instance.transform, instance.overrides, sources, collision);
    }
    counts.collision_triangles = static_cast<int>(collision.triangle_count());

    // The grid of chunks over everything.
    LevelHeader header;
    header.description = level.description;
    header.chunk_size = level.chunk_size;
    header.points = level.points;
    header.volumes = level.volumes;
    header.lighting = level.lighting;
    Aabb bounds = level.bounds();
    const Aabb collision_bounds = collision.bounds();
    if (!collision_bounds.empty()) {
        bounds.add(collision_bounds.min);
        bounds.add(collision_bounds.max);
    }
    if (bounds.empty()) {
        bounds.add(glm::vec3(0.0f));
    }
    header.bounds = bounds;
    // The grid starts on a multiple of the chunk size: the chunks of a level stay where they were
    // when something is added at its edge.
    header.origin = {std::floor(bounds.min.x / level.chunk_size) * level.chunk_size, bounds.min.y,
                     std::floor(bounds.min.z / level.chunk_size) * level.chunk_size};
    header.chunks_x = std::max(1, static_cast<int>(std::ceil((bounds.max.x - header.origin.x) / level.chunk_size + 1e-4f)));
    header.chunks_z = std::max(1, static_cast<int>(std::ceil((bounds.max.z - header.origin.z) / level.chunk_size + 1e-4f)));
    if (static_cast<long long>(header.chunks_x) * header.chunks_z > 65536) {
        throw std::runtime_error("level: more than 65536 chunks: make them larger");
    }
    std::vector<LevelChunk> chunks(static_cast<std::size_t>(header.chunks_x) * static_cast<std::size_t>(header.chunks_z));
    for (int z = 0; z < header.chunks_z; ++z) {
        for (int x = 0; x < header.chunks_x; ++x) {
            chunks[static_cast<std::size_t>(z) * static_cast<std::size_t>(header.chunks_x) + static_cast<std::size_t>(x)].coord = {x, z};
        }
    }
    const auto chunk_at = [&](glm::vec3 point) -> LevelChunk& {
        const glm::ivec2 c = header.chunk_of(point);
        return chunks[static_cast<std::size_t>(c.y) * static_cast<std::size_t>(header.chunks_x) + static_cast<std::size_t>(c.x)];
    };
    for (ChunkInstance& instance : all) {
        chunk_at(instance.transform.position).instances.push_back(std::move(instance));
    }
    for (std::size_t t = 0; t < collision.triangle_count(); ++t) {
        const glm::vec3 a = collision.vertices[collision.indices[t * 3]];
        const glm::vec3 b = collision.vertices[collision.indices[t * 3 + 1]];
        const glm::vec3 c = collision.vertices[collision.indices[t * 3 + 2]];
        chunk_at((a + b + c) * (1.0f / 3.0f)).collision.add_triangle(a, b, c, collision.kinds[t]);
    }

    // The navigation of the whole level, one tile per chunk.
    if (!collision.empty()) {
        CollisionScene scene;
        scene.add(collision, glm::mat4(1.0f));
        scene.build();
        std::vector<NavAreaVolume> areas;
        for (const LevelVolume& volume : level.volumes) {
            if (volume.kind == "nav_area") {
                const std::string area = volume.properties.value("area", std::string("water"));
                areas.push_back({volume.box, area == "fire" ? NavArea::Fire : NavArea::Water});
            }
        }
        std::vector<glm::vec3> seeds;
        seeds.reserve(level.points.size());
        for (const LevelPoint& point : level.points) {
            seeds.push_back(point.position);
        }
        for (const NavAgent& agent : level.agents) {
            NavTiling tiling;
            tiling.agent = agent;
            tiling.settings = level.navigation;
            tiling.settings.tile_size = level.chunk_size;
            tiling.origin = header.origin;
            tiling.tiles_x = header.chunks_x;
            tiling.tiles_z = header.chunks_z;
            NavMesh mesh;
            mesh.build_tiled(scene, tiling, areas);
            mesh.prune(seeds);
            header.navigation.push_back(mesh.tiling());
            for (LevelChunk& chunk : chunks) {
                chunk.nav_tiles.push_back(mesh.tile_data(chunk.coord.x, chunk.coord.y));
                counts.nav_tiles += chunk.nav_tiles.back().empty() ? 0 : 1;
            }
        }
    }

    BakedLevel baked;
    header.present.assign(chunks.size(), 0);
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        LevelChunk& chunk = chunks[i];
        const bool any_tile = std::any_of(chunk.nav_tiles.begin(), chunk.nav_tiles.end(), [](const auto& t) { return !t.empty(); });
        if (chunk.instances.empty() && chunk.collision.empty() && !any_tile) {
            continue;
        }
        header.present[i] = 1;
        baked.chunks.push_back(std::move(chunk));
    }
    counts.chunks = static_cast<int>(baked.chunks.size());
    baked.header = std::move(header);
    counts.milliseconds = static_cast<double>(SDL_GetPerformanceCounter() - start) * 1000.0 /
                          static_cast<double>(SDL_GetPerformanceFrequency());
    if (stats != nullptr) {
        *stats = counts;
    }
    return baked;
}

// --- Files ---------------------------------------------------------------------------------------

std::vector<std::uint8_t> encode_level_header(const LevelHeader& header) {
    nlohmann::json json = nlohmann::json::object();
    json["description"] = header.description;
    json["chunk_size"] = header.chunk_size;
    json["origin"] = json_of(header.origin);
    json["chunks"] = nlohmann::json::array({header.chunks_x, header.chunks_z});
    json["bounds"] = nlohmann::json::array({json_of(header.bounds.min), json_of(header.bounds.max)});
    nlohmann::json points = nlohmann::json::array();
    for (const LevelPoint& p : header.points) {
        points.push_back({p.name, json_of(p.position), nlohmann::json::array({p.rotation.w, p.rotation.x, p.rotation.y, p.rotation.z})});
    }
    json["points"] = points;
    nlohmann::json volumes = nlohmann::json::array();
    for (const LevelVolume& v : header.volumes) {
        volumes.push_back({v.id, v.kind, json_of(v.box.min), json_of(v.box.max), v.properties});
    }
    json["volumes"] = volumes;
    json["lighting"] = write_json(header.lighting, {.flags = kFieldData});
    nlohmann::json navigation = nlohmann::json::array();
    for (const NavTiling& t : header.navigation) {
        navigation.push_back({write_json(t.agent, {.flags = kFieldData}), write_json(t.settings, {.flags = kFieldData}),
                              json_of(t.origin), t.tiles_x, t.tiles_z});
    }
    json["navigation"] = navigation;
    json["present"] = header.present;
    BinaryWriter out;
    out.raw(kHeaderMagic, 4);
    out.u32(kFormatVersion);
    out.blob(shipped_document(json));
    std::vector<std::uint8_t> bytes = std::move(out.bytes());
    seal(bytes);
    return bytes;
}

LevelHeader decode_level_header(const std::vector<std::uint8_t>& bytes, const std::string& name) {
    BinaryReader in = open_sealed(bytes, kHeaderMagic, "prepared level", name);
    const std::vector<std::uint8_t> cbor = in.blob();
    if (in.left() != 0) {
        in.fail("bytes after the end");
    }
    LevelHeader header;
    try {
        const nlohmann::json json = parse_document(std::string_view(reinterpret_cast<const char*>(cbor.data()), cbor.size()));
        header.description = json.at("description").get<std::string>();
        header.chunk_size = json.at("chunk_size").get<float>();
        header.origin = vec3_of(json.at("origin"));
        header.chunks_x = json.at("chunks").at(0).get<int>();
        header.chunks_z = json.at("chunks").at(1).get<int>();
        header.bounds.add(vec3_of(json.at("bounds").at(0)));
        header.bounds.add(vec3_of(json.at("bounds").at(1)));
        for (const nlohmann::json& p : json.at("points")) {
            LevelPoint point;
            point.name = p.at(0).get<std::string>();
            point.position = vec3_of(p.at(1));
            point.rotation = glm::quat(p.at(2).at(0).get<float>(), p.at(2).at(1).get<float>(), p.at(2).at(2).get<float>(),
                                       p.at(2).at(3).get<float>());
            header.points.push_back(point);
        }
        for (const nlohmann::json& v : json.at("volumes")) {
            LevelVolume volume;
            volume.id = v.at(0).get<std::uint32_t>();
            volume.kind = v.at(1).get<std::string>();
            volume.box.add(vec3_of(v.at(2)));
            volume.box.add(vec3_of(v.at(3)));
            volume.properties = v.at(4);
            header.volumes.push_back(std::move(volume));
        }
        DataIssues lighting_issues;
        read_json(json.at("lighting"), header.lighting, name + " > lighting", lighting_issues, {.flags = kFieldData});
        for (const nlohmann::json& t : json.at("navigation")) {
            NavTiling tiling;
            DataIssues issues;
            read_json(t.at(0), tiling.agent, name, issues, {.flags = kFieldData});
            read_json(t.at(1), tiling.settings, name, issues, {.flags = kFieldData});
            tiling.origin = vec3_of(t.at(2));
            tiling.tiles_x = t.at(3).get<int>();
            tiling.tiles_z = t.at(4).get<int>();
            header.navigation.push_back(tiling);
        }
        header.present = json.at("present").get<std::vector<std::uint8_t>>();
    } catch (const std::exception& e) {
        throw std::runtime_error(name + ": unreadable prepared level: " + e.what());
    }
    if (!(header.chunk_size > 0.0f) || header.chunks_x <= 0 || header.chunks_z <= 0 ||
        header.present.size() != static_cast<std::size_t>(header.chunks_x) * static_cast<std::size_t>(header.chunks_z)) {
        throw std::runtime_error(name + ": prepared level with a wrong grid");
    }
    return header;
}

std::vector<std::uint8_t> encode_level_chunk(const LevelChunk& chunk) {
    nlohmann::json instances = nlohmann::json::array();
    for (const ChunkInstance& i : chunk.instances) {
        instances.push_back({i.id, i.prefab, write_json(i.transform, {.flags = kFieldData}), i.overrides, i.properties});
    }
    BinaryWriter out;
    out.raw(kChunkMagic, 4);
    out.u32(kFormatVersion);
    out.value<std::int32_t>(chunk.coord.x);
    out.value<std::int32_t>(chunk.coord.y);
    out.blob(shipped_document(nlohmann::json{{"instances", instances}}));
    out.array(chunk.collision.vertices);
    out.array(chunk.collision.indices);
    out.array(chunk.collision.kinds);
    out.u32(static_cast<std::uint32_t>(chunk.nav_tiles.size()));
    for (const std::vector<std::uint8_t>& tile : chunk.nav_tiles) {
        out.blob(tile);
    }
    std::vector<std::uint8_t> bytes = std::move(out.bytes());
    seal(bytes);
    return bytes;
}

LevelChunk decode_level_chunk(const std::vector<std::uint8_t>& bytes, const std::string& name) {
    BinaryReader in = open_sealed(bytes, kChunkMagic, "level chunk", name);
    LevelChunk chunk;
    chunk.coord.x = in.value<std::int32_t>();
    chunk.coord.y = in.value<std::int32_t>();
    const std::vector<std::uint8_t> cbor = in.blob();
    chunk.collision.vertices = in.array<glm::vec3>();
    chunk.collision.indices = in.array<std::uint32_t>();
    chunk.collision.kinds = in.array<SurfaceKind>();
    const std::uint32_t tiles = in.count(4);
    for (std::uint32_t t = 0; t < tiles; ++t) {
        chunk.nav_tiles.push_back(in.blob());
    }
    if (in.left() != 0) {
        in.fail("bytes after the end");
    }
    try {
        chunk.collision.validate("collision");
    } catch (const std::runtime_error& e) {
        in.fail(e.what());
    }
    try {
        const nlohmann::json json = parse_document(std::string_view(reinterpret_cast<const char*>(cbor.data()), cbor.size()));
        for (const nlohmann::json& i : json.at("instances")) {
            ChunkInstance instance;
            instance.id = i.at(0).get<std::uint32_t>();
            instance.prefab = i.at(1).get<std::string>();
            DataIssues issues;
            read_json(i.at(2), instance.transform, name, issues, {.flags = kFieldData});
            instance.overrides = i.at(3);
            instance.properties = i.at(4);
            chunk.instances.push_back(std::move(instance));
        }
    } catch (const std::exception& e) {
        throw std::runtime_error(name + ": unreadable level chunk: " + e.what());
    }
    return chunk;
}

std::string prepared_level_key(const std::string& level_key) {
    const std::string suffix = ".json";
    const std::string stem = level_key.ends_with(suffix) ? level_key.substr(0, level_key.size() - suffix.size()) : level_key;
    return stem + ".mlvl";
}

std::string prepared_chunk_key(const std::string& level_key, glm::ivec2 coord) {
    const std::string suffix = ".json";
    const std::string stem = level_key.ends_with(suffix) ? level_key.substr(0, level_key.size() - suffix.size()) : level_key;
    return stem + ".chunks/" + std::to_string(coord.x) + "_" + std::to_string(coord.y) + ".mlvc";
}

}  // namespace moteur
