#include <doctest/doctest.h>

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <string>
#include <vector>

#include "moteur/gizmo.hpp"
#include "moteur/level_bake.hpp"
#include "moteur/level_edit.hpp"

namespace {

moteur::LevelInstance piece(const std::string& prefab, glm::vec3 position) {
    moteur::LevelInstance instance;
    instance.prefab = prefab;
    instance.layer = "default";
    instance.transform.position = position;
    return instance;
}

// The document as written, but its next identifier (never goes back, even by undo).
std::string text_of(const moteur::LevelDocument& document) {
    nlohmann::json json = document.to_json([](const std::string& key) { return nlohmann::json(key); });
    json.erase("next_id");
    return json.dump();
}

}  // namespace

TEST_CASE("level edit: changes, one step per action, undo and redo back to the same document") {
    moteur::EditableLevel level;
    std::vector<moteur::LevelChange> changes;
    level.set_listener([&changes](const moteur::LevelChange& change) { changes.push_back(change); });
    const std::string empty = text_of(level.document());

    // Outside an action: each change is its own step.
    const std::uint32_t a = level.add(piece("prefabs/blockout/wall.prefab.json", {0, 0, 0}));
    const std::uint32_t b = level.add(piece("prefabs/blockout/pillar.prefab.json", {4, 0, 0}));
    CHECK(a != b);
    CHECK(level.can_undo());
    CHECK(level.undo_name() == "edit");
    REQUIRE(changes.size() == 2);
    CHECK(changes[0].kind == moteur::LevelChange::Kind::Instance);
    CHECK(changes[0].id == a);
    const std::string two = text_of(level.document());

    // A drag over many frames: one step, one change of the instance whatever the frames.
    level.begin_action("move");
    for (int frame = 1; frame <= 30; ++frame) {
        moteur::LevelInstance moved = *level.document().instance(a);
        moved.transform.position.x = 0.1f * static_cast<float>(frame);
        CHECK(level.set(moved));
    }
    CHECK_FALSE(level.set(*level.document().instance(a)));  // the same: no change
    level.end_action();
    CHECK(level.undo_name() == "move");
    CHECK(level.document().instance(a)->transform.position.x == doctest::Approx(3.0f));

    // Removed, then a point and the ground in one step.
    level.begin_action("several");
    CHECK(level.remove(b));
    CHECK_FALSE(level.remove(b));
    moteur::LevelPoint start;
    start.name = "start";
    start.position = {1, 0, 2};
    CHECK(level.set_point(start));
    moteur::LevelGround ground;
    ground.prefabs = {"prefabs/blockout/floor_8m.prefab.json"};
    CHECK(level.set_ground(ground));
    CHECK(level.paint_ground({2, 1}, 1));
    CHECK(level.paint_ground({-1, 0}, 1));  // grows to the left: the origin moves
    level.end_action();
    CHECK(level.document().instance(b) == nullptr);
    CHECK(level.document().ground.width == 4);
    CHECK(level.document().ground.height == 2);
    CHECK(level.document().ground.origin.x == doctest::Approx(-8.0f));
    const std::string after = text_of(level.document());

    // Undo the three steps of the end, then the two adds: back to the start; redo all: the same.
    CHECK(level.undo());
    CHECK(level.document().instance(b) != nullptr);
    CHECK(level.document().point("start") == nullptr);
    CHECK(level.document().ground.width == 0);
    CHECK(level.undo());
    CHECK(level.document().instance(a)->transform.position.x == doctest::Approx(0.0f));
    CHECK(text_of(level.document()) == two);
    CHECK(level.undo());
    CHECK(level.undo());
    CHECK_FALSE(level.undo());
    CHECK(text_of(level.document()) == empty);
    while (level.redo()) {
    }
    CHECK(text_of(level.document()) == after);

    // A new change clears what could be redone; an identifier is never given twice.
    CHECK(level.undo());
    CHECK(level.can_redo());
    const std::uint32_t c = level.add(piece("prefabs/blockout/crate.prefab.json", {0, 0, 4}));
    CHECK_FALSE(level.can_redo());
    CHECK(c > b);
}

TEST_CASE("level edit: an action that comes back to where it started is no step; settings and lighting") {
    moteur::EditableLevel level;
    const std::uint32_t a = level.add(piece("prefabs/blockout/wall.prefab.json", {0, 0, 0}));
    level.clear_history();
    level.begin_action("nothing");
    moteur::LevelInstance moved = *level.document().instance(a);
    moved.transform.position.y = 2.0f;
    CHECK(level.set(moved));
    moved.transform.position.y = 0.0f;
    CHECK(level.set(moved));
    level.end_action();
    CHECK_FALSE(level.can_undo());

    moteur::LevelSettings settings = level.settings();
    settings.lighting = moteur::night_lighting(settings.lighting);
    settings.chunk_size = 16.0f;
    CHECK(level.set_settings(settings));
    CHECK(level.document().navigation.tile_size == doctest::Approx(16.0f));
    CHECK(level.document().lighting.sun_intensity < 0.5f);
    settings.layers.clear();
    CHECK_THROWS_AS(level.set_settings(settings), std::invalid_argument);
    CHECK(level.undo());
    CHECK(level.document().chunk_size == doctest::Approx(32.0f));
    CHECK(level.document().lighting == moteur::LevelLighting{});

    // The lighting is written and read back with the level.
    moteur::LevelDocument document = level.document();
    document.lighting.sun_yaw = 12.0f;
    document.lighting.exposure = 1.5f;
    moteur::DataIssues issues;
    const moteur::LevelDocument read = moteur::LevelDocument::parse(
        document.to_json([](const std::string& key) { return nlohmann::json(key); }).dump(), "test.level.json",
        [](const nlohmann::json& reference) { return reference.get<std::string>(); }, issues);
    CHECK(read.lighting == document.lighting);
}

TEST_CASE("level edit: an instance's properties (the game's data) are kept by the level and its chunks") {
    moteur::LevelDocument document;
    moteur::LevelInstance spawner = piece("prefabs/blockout/crate.prefab.json", {1, 0, 1});
    spawner.properties = {{"personnage", "squelette_sbire"}, {"vie", 30}};
    document.add(spawner);
    moteur::DataIssues issues;
    const moteur::LevelDocument read = moteur::LevelDocument::parse(
        document.to_json([](const std::string& key) { return nlohmann::json(key); }).dump(), "test.level.json",
        [](const nlohmann::json& reference) { return reference.get<std::string>(); }, issues);
    REQUIRE(read.instances.size() == 1);
    CHECK(read.instances.front().properties == spawner.properties);
    CHECK(read.instances.front().overrides.empty());

    moteur::LevelChunk chunk;
    chunk.coord = {0, 0};
    chunk.instances.push_back({1, spawner.prefab, spawner.transform, nlohmann::json::object(), spawner.properties});
    const moteur::LevelChunk decoded = moteur::decode_level_chunk(moteur::encode_level_chunk(chunk), "chunk");
    REQUIRE(decoded.instances.size() == 1);
    CHECK(decoded.instances.front().properties == spawner.properties);
}

TEST_CASE("level edit: ground cells and growing the grid") {
    moteur::LevelGround ground;
    ground.cell_size = 8.0f;
    ground.origin = {16.0f, -8.0f};
    CHECK(moteur::ground_cell(ground, {17.0f, 0.0f, -7.0f}) == glm::ivec2(0, 0));
    CHECK(moteur::ground_cell(ground, {15.0f, 0.0f, -9.0f}) == glm::ivec2(-1, -1));
    ground.width = 2;
    ground.height = 1;
    ground.cells = {1, 2};
    const glm::ivec2 shift = moteur::grow_ground(ground, {-1, 0}, {2, 1});
    CHECK(shift == glm::ivec2(1, 0));
    CHECK(ground.width == 4);
    CHECK(ground.height == 2);
    CHECK(ground.origin.x == doctest::Approx(8.0f));
    CHECK(ground.cells == std::vector<std::uint8_t>{0, 1, 2, 0, 0, 0, 0, 0});  // the cells kept their place
}

TEST_CASE("gizmo: handles found under the pointer, drags along an axis, in a plane, about an axis") {
    moteur::Camera3D camera;
    camera.set_viewport({1280.0f, 720.0f});
    camera.set_angles(45.0f, 40.0f);
    camera.set_target({0.0f, 0.0f, 0.0f});
    camera.set_visible_height(10.0f);
    const glm::vec3 origin(0.0f);
    moteur::Gizmo gizmo;
    const float length = gizmo.world_size(camera, origin);
    CHECK(length > 0.0f);

    // The end of the x arrow is the x handle; the middle is All; far away, nothing.
    const glm::vec2 x_end = *camera.world_to_screen(origin + glm::vec3(length * 0.9f, 0, 0));
    CHECK(gizmo.hit(camera, origin, moteur::GizmoMode::Translate, x_end) == moteur::GizmoHandle::X);
    const glm::vec2 middle = *camera.world_to_screen(origin);
    CHECK(gizmo.hit(camera, origin, moteur::GizmoMode::Translate, middle) == moteur::GizmoHandle::All);
    CHECK(gizmo.hit(camera, origin, moteur::GizmoMode::Translate, middle + glm::vec2(400, 300)) == moteur::GizmoHandle::None);
    const glm::vec2 plane = *camera.world_to_screen(origin + glm::vec3(0.35f, 0.0f, 0.35f) * length);
    CHECK(gizmo.hit(camera, origin, moteur::GizmoMode::Translate, plane) == moteur::GizmoHandle::XZ);
    CHECK_FALSE(gizmo.shape(camera, origin, moteur::GizmoMode::Rotate).empty());

    // Along x: the pointer moved to where x = 2 is seen gives a move of 2 along x only, snapped.
    REQUIRE(gizmo.begin(camera, origin, moteur::GizmoMode::Translate, moteur::GizmoHandle::X, x_end));
    const glm::vec2 x_two = *camera.world_to_screen(origin + glm::vec3(length * 0.9f + 2.0f, 0, 0));
    moteur::GizmoDelta delta = gizmo.drag(camera, x_two);
    CHECK(delta.translation.x == doctest::Approx(2.0f).epsilon(0.01));
    CHECK(delta.translation.y == doctest::Approx(0.0f));
    CHECK(delta.translation.z == doctest::Approx(0.0f));
    delta = gizmo.drag(camera, x_two, {.translation = 0.5f});
    CHECK(delta.translation.x == doctest::Approx(2.0f));
    gizmo.end();

    // In the ground's plane.
    REQUIRE(gizmo.begin(camera, origin, moteur::GizmoMode::Translate, moteur::GizmoHandle::XZ, plane));
    const glm::vec2 moved = *camera.world_to_screen(origin + glm::vec3(0.35f, 0.0f, 0.35f) * length + glm::vec3(1.0f, 0.0f, -3.0f));
    delta = gizmo.drag(camera, moved);
    CHECK(delta.translation.x == doctest::Approx(1.0f).epsilon(0.01));
    CHECK(delta.translation.y == doctest::Approx(0.0f));
    CHECK(delta.translation.z == doctest::Approx(-3.0f).epsilon(0.01));
    gizmo.end();

    // A quarter turn about y, snapped to 15 degrees.
    // A point of the y ring only (x's ring is in the plane x = 0, z's in z = 0).
    const float c = 0.70710678f * length;
    const glm::vec2 ring = *camera.world_to_screen(origin + glm::vec3(c, 0.0f, c));
    REQUIRE(gizmo.hit(camera, origin, moteur::GizmoMode::Rotate, ring) == moteur::GizmoHandle::Y);
    REQUIRE(gizmo.begin(camera, origin, moteur::GizmoMode::Rotate, moteur::GizmoHandle::Y, ring));
    const glm::vec2 quarter = *camera.world_to_screen(origin + glm::vec3(c, 0.0f, -c * 0.97f));
    delta = gizmo.drag(camera, quarter, {.angle = glm::radians(15.0f)});
    CHECK(std::fabs(delta.angle) == doctest::Approx(glm::radians(90.0f)).epsilon(0.001));
    gizmo.end();

    // Scale along z: twice as far, twice the size.
    const glm::vec2 z_end = *camera.world_to_screen(origin + glm::vec3(0, 0, length));
    REQUIRE(gizmo.begin(camera, origin, moteur::GizmoMode::Scale, moteur::GizmoHandle::Z, z_end));
    delta = gizmo.drag(camera, *camera.world_to_screen(origin + glm::vec3(0, 0, length * 2.0f)));
    CHECK(delta.scale.z == doctest::Approx(2.0f).epsilon(0.01));
    CHECK(delta.scale.x == doctest::Approx(1.0f));
    gizmo.end();
    CHECK(moteur::snap_value(1.26f, 0.25f) == doctest::Approx(1.25f));
}
