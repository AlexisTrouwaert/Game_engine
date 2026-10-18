#include <doctest/doctest.h>

#include <map>
#include <memory>
#include <stdexcept>
#include <string>

#include "moteur/level.hpp"
#include "moteur/level_bake.hpp"
#include "moteur/map_data.hpp"
#include "moteur/navigation.hpp"
#include "moteur/prefab.hpp"

// Milestone 9, part 8: levels of production: the source document (stable identifiers, layers,
// points, volumes, ground), the migration of the old maps, the state saved with a game, and the
// preparation cut in chunks (collision, navigation tiles joined across chunks, instances), with its
// files.

namespace {

// Prefabs from texts, without the asset manager.
struct Prefabs {
    std::map<std::string, std::string> files;
    std::map<std::string, std::shared_ptr<const moteur::Prefab>> loaded;
    std::shared_ptr<const moteur::Prefab> get(const std::string& path) {
        if (const auto found = loaded.find(path); found != loaded.end()) {
            return found->second;
        }
        const auto file = files.find(path);
        if (file == files.end()) {
            throw std::runtime_error("no prefab " + path);
        }
        moteur::Prefab::Context context;
        context.prefab = [this](const std::string& other) { return get(other); };
        moteur::DataIssues issues;
        return loaded[path] = std::make_shared<const moteur::Prefab>(moteur::Prefab::parse(file->second, path, context, issues));
    }
};

// A floor slab of 8 x 8 m (top at y 0) and a wall block of 1 x 2 x 1 m, both with collision boxes.
Prefabs blockout() {
    Prefabs prefabs;
    prefabs.files["floor.prefab.json"] = R"({"version": 1, "entities": [{"id": 1, "components": {
        "transform": {}, "static_collision": {"from_model": false, "boxes": [
            {"centre": [4, -0.5, 4], "half_size": [4, 0.5, 4], "top": "walkable", "sides": "wall"}]}}}]})";
    prefabs.files["wall.prefab.json"] = R"({"version": 1, "entities": [{"id": 1, "components": {
        "transform": {}, "static_collision": {"from_model": false, "boxes": [
            {"centre": [0, 1, 0], "half_size": [0.5, 1, 0.5], "top": "wall", "sides": "wall"}]}}}]})";
    return prefabs;
}

moteur::LevelSources sources_of(Prefabs& prefabs) {
    moteur::LevelSources sources;
    sources.prefab = [&prefabs](const std::string& key) { return prefabs.get(key); };
    sources.model_collision = [](const std::string&) -> const moteur::CollisionMeshData* { return nullptr; };
    sources.resolve = [](const nlohmann::json& value) { return value.get<std::string>(); };
    return sources;
}

std::string resolve(const nlohmann::json& value) {
    if (!value.is_string()) {
        throw std::runtime_error("expected a path");
    }
    return value.get<std::string>();
}

// A level of 4 x 4 floor slabs (32 x 32 m) and a wall across x = 16 with a gap at z 12 .. 14.
moteur::LevelDocument test_level() {
    moteur::LevelDocument level;
    level.chunk_size = 16.0f;
    level.ground.cell_size = 8.0f;
    level.ground.width = 4;
    level.ground.height = 4;
    level.ground.prefabs = {"floor.prefab.json"};
    level.ground.cells.assign(16, 1);
    for (int z = 0; z < 32; ++z) {
        if (z >= 12 && z < 14) {
            continue;
        }
        moteur::LevelInstance wall;
        wall.prefab = "wall.prefab.json";
        wall.transform.position = {16.5f, 0.0f, static_cast<float>(z) + 0.5f};
        level.add(wall);
    }
    level.points.push_back({"start", {4, 0, 4}, glm::quat(1, 0, 0, 0)});
    return level;
}

}  // namespace

TEST_CASE("level document: written and read back, stable identifiers, errors refused") {
    moteur::LevelDocument level = test_level();
    level.description = "test";
    level.layers.push_back({"decor", false, true});
    moteur::LevelInstance lamp;
    lamp.prefab = "lamp.prefab.json";
    lamp.layer = "decor";
    lamp.transform.position = {3, 0, 5};
    lamp.overrides = {{"2", {{"light", {{"intensity", 4}}}}}};
    const std::uint32_t lamp_id = level.add(lamp);
    moteur::LevelVolume water;
    water.kind = "nav_area";
    water.box.add({1, -1, 1});
    water.box.add({3, 1, 3});
    water.properties = {{"area", "water"}};
    level.add(water);

    const nlohmann::json json = level.to_json([](const std::string& key) { return nlohmann::json(key); });
    moteur::DataIssues issues;
    const moteur::LevelDocument back = moteur::LevelDocument::parse(json.dump(), "test.level.json", resolve, issues);
    CHECK(issues.all().empty());
    CHECK(back.instances.size() == level.instances.size());
    CHECK(back.next_id() == level.next_id());
    REQUIRE(back.instance(lamp_id) != nullptr);
    CHECK(back.instance(lamp_id)->layer == "decor");
    CHECK(back.instance(lamp_id)->transform.position == glm::vec3(3, 0, 5));
    CHECK(back.instance(lamp_id)->overrides == lamp.overrides);
    CHECK(back.layers.size() == 2);
    CHECK_FALSE(back.layers[1].visible);
    CHECK(back.ground.cells == level.ground.cells);
    CHECK(back.volumes.size() == 1);
    CHECK(back.point("start") != nullptr);
    // A removed instance's identifier is not given again.
    moteur::LevelDocument edited = back;
    edited.instances.pop_back();
    const std::uint32_t next = edited.add(moteur::LevelInstance{0, "wall.prefab.json"});
    CHECK(next > lamp_id);

    // Errors: twice the same identifier, an instance without prefab, a ground of the wrong size.
    nlohmann::json bad = json;
    bad["instances"][1]["id"] = bad["instances"][0]["id"];
    bad["instances"][2].erase("prefab");
    bad["ground"]["cells"] = {1, 1};
    issues.clear();
    CHECK_THROWS_WITH_AS(moteur::LevelDocument::parse(bad.dump(), "bad.level.json", resolve, issues), doctest::Contains("error"),
                         std::runtime_error);
    CHECK(issues.errors() == 3);
    CHECK_THROWS_WITH_AS(moteur::LevelDocument::parse(R"({"version": 2})", "old.level.json", resolve, issues),
                         doctest::Contains("migrate"), std::runtime_error);
}

TEST_CASE("level: the maps of version 1 and 2 migrate; the state of a game is saved") {
    const moteur::MapData map = moteur::MapData::parse(R"({
        "version": 2,
        "tiles": {"floor": {}, "wall": {"walkable": false, "opaque": true}},
        "legend": {".": ["floor"], "#": ["floor", "wall"], "@": {"tiles": ["floor"], "point": "start"}},
        "rows": ["#####", "#@..#", "#####"],
        "objects": [{"id": "o1", "type": "torch", "x": 2.5, "z": 1.5, "rotation": 90}]
    })", "test.json");
    moteur::MapMigration rules;
    rules.ground_tile = "floor";
    rules.ground_prefab = "floor_1m.prefab.json";
    rules.tiles["wall"] = "wall.prefab.json";
    rules.objects["torch"] = "torch.prefab.json";
    moteur::DataIssues issues;
    const moteur::LevelDocument level = moteur::migrate_map(map, rules, issues);
    // Floor: one run per row (3 rows); walls: a run of 5, two of 1, a run of 5; the torch.
    int floors = 0, walls = 0, torches = 0;
    for (const moteur::LevelInstance& i : level.instances) {
        floors += i.prefab == "floor_1m.prefab.json" ? 1 : 0;
        walls += i.prefab == "wall.prefab.json" ? 1 : 0;
        torches += i.prefab == "torch.prefab.json" ? 1 : 0;
    }
    CHECK(floors == 3);
    CHECK(walls == 4);
    CHECK(torches == 1);
    REQUIRE(level.point("start") != nullptr);
    CHECK(level.point("start")->position == glm::vec3(1.5f, 0.0f, 1.5f));
    for (const moteur::LevelInstance& i : level.instances) {
        if (i.prefab == "wall.prefab.json" && i.transform.scale.x == 5.0f) {
            CHECK(i.transform.position.x == doctest::Approx(2.5f));
        }
    }

    moteur::LevelState state;
    state.set(7, {{"1", {{"light", {{"intensity", 0}}}}}});
    state.set(7, {{"1", {{"light", {{"color", {1, 0, 0}}}}}}});  // merged
    state.remove(9);
    const moteur::LevelState back = moteur::LevelState::from_json(state.to_json());
    CHECK(back.removed(9));
    CHECK_FALSE(back.removed(7));
    CHECK(back.overrides(7)["1"]["light"]["intensity"] == 0);
    CHECK(back.overrides(7)["1"]["light"].contains("color"));
    CHECK(back.overrides(5).empty());
}

TEST_CASE("level bake: chunks, collision, navigation tiles joined across chunks, files") {
    Prefabs prefabs = blockout();
    const moteur::LevelSources sources = sources_of(prefabs);
    const moteur::LevelDocument level = test_level();
    moteur::LevelBakeStats stats;
    const moteur::BakedLevel baked = moteur::bake_level(level, sources, &stats);
    MESSAGE("baked " << stats.instances << " instances, " << stats.collision_triangles << " triangles, " << stats.chunks
                     << " chunks, " << stats.nav_tiles << " navigation tiles in " << stats.milliseconds << " ms");
    CHECK(baked.header.chunks_x == 3);  // 0 .. 32 m and the wall's edge at 17 m: 0, 16, 32
    CHECK(baked.header.chunks_z >= 2);
    CHECK(stats.instances == 16 + 30);
    CHECK(stats.collision_triangles == (16 + 30) * 12);
    CHECK(stats.nav_tiles >= 4);

    // The files: the same once read back; a damaged one refused.
    const std::vector<std::uint8_t> header_bytes = moteur::encode_level_header(baked.header);
    const moteur::LevelHeader header = moteur::decode_level_header(header_bytes, "test.level.mlvl");
    CHECK(header.chunks_x == baked.header.chunks_x);
    CHECK(header.present == baked.header.present);
    REQUIRE(header.point("start") != nullptr);
    std::vector<moteur::LevelChunk> chunks;
    for (const moteur::LevelChunk& chunk : baked.chunks) {
        const std::vector<std::uint8_t> bytes = moteur::encode_level_chunk(chunk);
        chunks.push_back(moteur::decode_level_chunk(bytes, "chunk.mlvc"));
        CHECK(chunks.back().collision.vertices == chunk.collision.vertices);
        CHECK(chunks.back().instances.size() == chunk.instances.size());
    }
    std::vector<std::uint8_t> damaged = moteur::encode_level_chunk(baked.chunks.front());
    damaged[damaged.size() / 2] ^= 0x10;
    CHECK_THROWS_WITH_AS(moteur::decode_level_chunk(damaged, "chunk.mlvc"), doctest::Contains("checksum"), std::runtime_error);

    // The navigation, chunk by chunk: across the wall by its gap, through chunks loaded one by one.
    moteur::NavMesh mesh;
    mesh.start_tiled(header.navigation.front());
    for (const moteur::LevelChunk& chunk : chunks) {
        if (!chunk.nav_tiles.front().empty()) {
            mesh.add_tile(chunk.nav_tiles.front(), "chunk.mlvc");
        }
    }
    const moteur::NavPath across = mesh.find_path({4, 0, 4}, {28, 0, 4});
    REQUIRE(across.status == moteur::PathStatus::Found);
    CHECK(std::any_of(across.points.begin(), across.points.end(), [](glm::vec3 p) { return p.z > 11.0f; }));  // by the gap
    // The chunk holding the gap unloaded: no way across any more.
    const glm::ivec2 gap = header.chunk_of({16.5f, 0, 13});
    mesh.remove_tile(gap.x, gap.y);
    CHECK(mesh.find_path({4, 0, 4}, {28, 0, 4}).status != moteur::PathStatus::Found);
}
