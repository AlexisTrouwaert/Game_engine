// Regression tests of the defects found while documenting the API (milestone 8, part 9): world.

#include <doctest/doctest.h>

#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include <stdexcept>
#include <string>
#include <vector>

#include "moteur/animation_clock.hpp"
#include "moteur/animation_data.hpp"
#include "moteur/animation_graph.hpp"
#include "moteur/animation_set.hpp"
#include "moteur/animator.hpp"
#include "moteur/collision.hpp"
#include "moteur/input.hpp"
#include "moteur/map_data.hpp"
#include "moteur/map_document.hpp"
#include "moteur/model.hpp"
#include "moteur/movement.hpp"
#include "moteur/pathfinding.hpp"
#include "moteur/skeleton.hpp"
#include "moteur/world.hpp"

namespace {

// --- Grids drawn in characters: '.' floor, '#' wall.
struct Grid {
    moteur::NavGrid nav;
    moteur::TileMap tiles{1, 1};
    moteur::Tileset tileset;
};

Grid make_grid(const std::vector<std::string>& rows) {
    Grid g;
    const moteur::TileId floor = g.tileset.add({"", true, false});
    const moteur::TileId wall = g.tileset.add({"", false, true});
    g.tiles = moteur::TileMap(static_cast<int>(rows[0].size()), static_cast<int>(rows.size()), 2);
    for (int j = 0; j < static_cast<int>(rows.size()); ++j) {
        for (int i = 0; i < static_cast<int>(rows[0].size()); ++i) {
            g.tiles.set(0, {i, j}, floor);
            if (rows[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)] == '#') {
                g.tiles.set(1, {i, j}, wall);
            }
        }
    }
    g.nav = moteur::NavGrid(g.tiles, g.tileset);
    return g;
}

// Two rooms of 3 x 3 that do not join.
const std::vector<std::string> kTwoRooms = {
    "#########",
    "#...#...#",
    "#...#...#",
    "#...#...#",
    "#########",
};

entt::entity add_mover(entt::registry& registry, glm::vec2 at, float radius) {
    const entt::entity e = registry.create();
    moteur::Transform transform;
    transform.position = {at.x, 0.0f, at.y};
    registry.emplace<moteur::Transform>(e, transform);
    registry.emplace<moteur::Collider>(e, moteur::Collider{radius});
    registry.emplace<moteur::Mover>(e);
    return e;
}

void tick(entt::registry& registry, const Grid& g, const moteur::ClearanceMap& clearance, const moteur::FlowField* field) {
    const float dt = 1.0f / 60.0f;
    moteur::MovementStats stats;
    moteur::plan_paths(registry, g.nav, clearance, 4, stats);
    moteur::move_movers(registry, g.nav, dt, field, stats);
    moteur::finish_movers(registry, dt);
}

SDL_Event key(SDL_Scancode scancode, bool down) {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.scancode = scancode;
    event.key.down = down;
    return event;
}

// --- Animation: a two-joint body and still clips.
moteur::SkeletonData body() {
    moteur::SkeletonData skeleton;
    skeleton.joints.push_back({"root", -1, {}});
    skeleton.joints.push_back({"spine", 0, {}});
    return skeleton;
}

moteur::ClipData held(const char* name, float seconds) {
    moteur::ClipData clip;
    clip.name = name;
    clip.duration = seconds;
    clip.tracks.resize(2);
    for (moteur::JointTrack& track : clip.tracks) {
        track.translations.times = {0.0f, seconds};
        track.translations.values = {glm::vec3(0.0f), glm::vec3(0.0f)};
    }
    return clip;
}

const char* kSet = R"({ "version": 1, "fade_ticks": 4,
  "graph": {
    "parameters": { "vitesse": "float" },
    "layers": [ { "start": "repos",
                  "states": { "repos": { "motion": "Idle" }, "course": { "motion": "Run" } },
                  "transitions": [ { "from": "repos", "to": "course",
                                     "when": { "param": "vitesse", "op": ">", "value": 0.5 } } ] } ] } })";

struct Rig {
    moteur::SkeletonData data = body();
    moteur::Asset<moteur::Skeleton> skeleton = moteur::make_asset(moteur::Skeleton::create(data, "body"));
    moteur::Asset<moteur::ClipLibrary> clips = moteur::make_asset(
        moteur::ClipLibrary::create(data, {held("Idle", 1.0f), held("Run", 0.5f), held("Attack", 0.5f)}, "body.glb"));
    moteur::Asset<moteur::AnimationSet> set = moteur::make_asset(moteur::AnimationSet::parse(kSet, "body.json"));
};

// --- World: what it collects.
struct Recorder final : moteur::WorldSink {
    std::vector<moteur::Aabb> boxes;
    void mesh(const moteur::Mesh&, const glm::mat4&, const moteur::Material&, const moteur::Aabb& bounds) override {
        boxes.push_back(bounds);
    }
    void light(const moteur::PointLight&) override {}
    void billboard(const moteur::Texture&, glm::vec3, glm::vec2, const moteur::BillboardOptions&) override {}
};

// Two parts of half-size `half`, one above the other.
moteur::Asset<moteur::Model> two_parts(float half) {
    moteur::Model model;
    for (int k = 0; k < 2; ++k) {
        moteur::Model::Part part;
        part.mesh.bounds.add(glm::vec3(-half));
        part.mesh.bounds.add(glm::vec3(half));
        part.transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, static_cast<float>(k), 0.0f));
        model.parts.push_back(std::move(part));
    }
    return moteur::make_asset(std::move(model));
}

}  // namespace

TEST_CASE("Review (world): a replay ends with stop_replay(), and live input comes back") {
    moteur::Input input;
    const moteur::ActionId skill = input.add_button("skill");
    input.load_bindings(R"({"version": 1, "profile": "p",
                            "profiles": {"p": {"label": "P", "buttons": {"skill": ["key:Q"]}}}})",
                        "bindings.json");
    CHECK_FALSE(input.replaying());
    input.set_enabled(false);  // as the Application does for a replay
    moteur::InputFrame frame;
    frame.buttons.resize(1);
    frame.buttons[0] = {true, 1, 0};
    frame.pointer = {10.0f, 20.0f};
    input.set_frame(frame);
    CHECK(input.replaying());
    CHECK(input.pressed(skill));

    // Enabling the real input does not end the replay: its events are still ignored.
    input.set_enabled(true);
    input.process_event(key(SDL_SCANCODE_Q, false));
    input.set_pointer({1.0f, 1.0f});
    CHECK(input.down(skill));
    CHECK(input.pointer() == glm::vec2(10.0f, 20.0f));

    // Stopped: idle at once (what the replay held goes up), then the real events count again.
    input.stop_replay();
    CHECK_FALSE(input.replaying());
    CHECK_FALSE(input.down(skill));
    CHECK_FALSE(input.pressed(skill));
    CHECK(input.released(skill));
    CHECK(input.pointer().x < 0.0f);
    input.end_tick();
    input.process_event(key(SDL_SCANCODE_Q, true));
    CHECK(input.pressed(skill));
    CHECK(input.down(skill));
    input.set_pointer({5.0f, 6.0f});
    CHECK(input.pointer() == glm::vec2(5.0f, 6.0f));
    input.stop_replay();  // not replaying: nothing happens
    CHECK(input.down(skill));
}

TEST_CASE("Review (world): a mover that cannot path reports NoPath, not Arrived") {
    const Grid g = make_grid(kTwoRooms);
    const moteur::ClearanceMap clearance(g.nav);

    // The start fits nowhere (rooms of 3 cells, a circle of 1.6 m): no path at all.
    {
        entt::registry registry;
        const entt::entity e = add_mover(registry, {2.5f, 2.5f}, 1.6f);
        registry.get<moteur::Mover>(e).go_to({6.5f, 2.5f});
        tick(registry, g, clearance, nullptr);
        const moteur::Mover& mover = registry.get<moteur::Mover>(e);
        CHECK(mover.path_status == moteur::PathStatus::None);
        CHECK(mover.state == moteur::MoveState::NoPath);
        CHECK(mover.mode == moteur::MoveMode::Stop);
    }
    // The goal is in the other room: a Partial path, walked to its end, ends as NoPath.
    {
        entt::registry registry;
        const entt::entity e = add_mover(registry, {1.5f, 2.5f}, 0.3f);
        registry.get<moteur::Mover>(e).go_to({6.5f, 2.5f});
        int ticks = 0;
        while (registry.get<moteur::Mover>(e).mode != moteur::MoveMode::Stop && ticks < 600) {
            tick(registry, g, clearance, nullptr);
            ++ticks;
        }
        const moteur::Mover& mover = registry.get<moteur::Mover>(e);
        CHECK(mover.path_status == moteur::PathStatus::Partial);
        CHECK(mover.state == moteur::MoveState::NoPath);
        CHECK(moteur::plane_position(registry, e).x > 3.0f);  // it did walk to the room's far side
    }
    // Already in the nearest reachable cell: an empty Partial path, NoPath at once.
    {
        entt::registry registry;
        const entt::entity e = add_mover(registry, {3.5f, 2.5f}, 0.3f);
        registry.get<moteur::Mover>(e).go_to({6.5f, 2.5f});
        tick(registry, g, clearance, nullptr);
        CHECK(registry.get<moteur::Mover>(e).state == moteur::MoveState::NoPath);
    }
    // A goal that is reached still ends as Arrived.
    {
        entt::registry registry;
        const entt::entity e = add_mover(registry, {1.5f, 1.5f}, 0.3f);
        registry.get<moteur::Mover>(e).go_to({3.5f, 3.5f});
        int ticks = 0;
        while (registry.get<moteur::Mover>(e).mode != moteur::MoveMode::Stop && ticks < 600) {
            tick(registry, g, clearance, nullptr);
            ++ticks;
        }
        CHECK(registry.get<moteur::Mover>(e).path_status == moteur::PathStatus::Found);
        CHECK(registry.get<moteur::Mover>(e).state == moteur::MoveState::Arrived);
    }
}

TEST_CASE("Review (world): a flow field that reached nothing is not valid, its movers stay Idle") {
    const Grid g = make_grid(kTwoRooms);
    const moteur::ClearanceMap clearance(g.nav);
    moteur::FlowField field;
    CHECK_FALSE(field.valid());
    CHECK(field.compute(g.nav, clearance, {2.5f, 2.5f}, 1.6f) == 0);  // nothing fits near the target
    CHECK(field.target_cell().x < 0);
    CHECK_FALSE(field.valid());
    entt::registry registry;
    const entt::entity e = add_mover(registry, {6.5f, 2.5f}, 0.3f);
    registry.get<moteur::Mover>(e).follow_field(0.5f);
    tick(registry, g, clearance, &field);
    CHECK(registry.get<moteur::Mover>(e).state == moteur::MoveState::Idle);

    CHECK(field.compute(g.nav, clearance, {2.5f, 2.5f}, 0.3f) > 0);
    CHECK(field.valid());
}

TEST_CASE("Review (world): a flow field whose target is in a wall leads to the moved target cell") {
    const Grid g = make_grid({
        "#######",
        "#.....#",
        "#.....#",
        "#.....#",
        "#######",
    });
    const moteur::ClearanceMap clearance(g.nav);
    moteur::FlowField field;
    REQUIRE(field.compute(g.nav, clearance, {3.5f, 0.5f}, 0.3f) > 0);  // in the top wall
    REQUIRE(field.target_cell() == glm::ivec2(3, 1));
    CHECK(field.target() == glm::vec2(3.5f, 0.5f));
    CHECK(field.aim() == glm::vec2(3.5f, 1.5f));
    const glm::vec2 d = field.direction({3.2f, 1.8f});  // on the target cell: towards its middle
    CHECK(d.x == doctest::Approx(0.70710678f));
    CHECK(d.y == doctest::Approx(-0.70710678f));

    // A target that stands in its cell is aimed at exactly, as before.
    moteur::FlowField open;
    REQUIRE(open.compute(g.nav, clearance, {3.2f, 2.3f}, 0.3f) > 0);
    CHECK(open.aim() == glm::vec2(3.2f, 2.3f));

    // A mover following it stops in the middle of the cell, not pressed against the wall.
    entt::registry registry;
    const entt::entity e = add_mover(registry, {1.5f, 3.5f}, 0.3f);
    registry.get<moteur::Mover>(e).follow_field(0.0f);
    for (int i = 0; i < 240; ++i) {
        tick(registry, g, clearance, &field);
    }
    const glm::vec2 at = moteur::plane_position(registry, e);
    CHECK(at.x == doctest::Approx(3.5f).epsilon(0.001));
    CHECK(at.y == doctest::Approx(1.5f).epsilon(0.001));
}

TEST_CASE("Review (world): a default MapData is a valid one-cell map") {
    const moteur::MapData map;
    CHECK(map.width() == 1);
    CHECK(map.height() == 1);
    REQUIRE(map.symbols().size() == 1);
    CHECK(map.symbols()[0].character == ' ');
    CHECK(map.symbols()[0].tiles.empty());
    CHECK(map.cell_symbol({0, 0}) == 0);
    CHECK(map.to_ascii() == " \n");
    CHECK(map.tile_names().empty());
    // parse() replaces it whole: the legend of the file only.
    const moteur::MapData parsed = moteur::MapData::parse(
        R"({"version": 1, "tiles": {"floor": {}}, "legend": {".": ["floor"], " ": []}, "rows": [". ."]})", "m.json");
    CHECK(parsed.symbols().size() == 2);
    CHECK(parsed.to_ascii() == ". .\n");
}

TEST_CASE("Review (world): a tile type cannot take the name of another one") {
    moteur::MapDocument doc(3, 2, 1, "floor");
    const moteur::TileId wall = doc.add_tile_type({"wall", false, true, ""});
    REQUIRE(wall == 2);
    doc.set_tile_type(wall, {"floor", true, false, ""});  // refused: two "floor" keys
    CHECK(doc.tile_types()[1].name == "wall");
    CHECK_FALSE(doc.tile_types()[1].walkable);
    doc.set_tile_type(wall, {"", true, false, ""});  // refused: an empty key
    CHECK(doc.tile_types()[1].name == "wall");
    doc.set_tile_type(wall, {"wall", true, true, ""});  // its own name: changed
    CHECK(doc.tile_types()[1].walkable);
    doc.set_tile_type(wall, {"stone", true, true, ""});  // a new name: changed
    CHECK(doc.tile_types()[1].name == "stone");
    const moteur::MapData map = doc.to_map_data();
    CHECK(map.tile_names() == std::vector<std::string>{"floor", "stone"});
}

TEST_CASE("Review (world): replacing the model of a still entity drops its cached boxes") {
    moteur::World world;
    entt::registry& registry = world.registry();
    const entt::entity entity = registry.create();
    moteur::Transform transform;
    transform.position = {10.0f, 0.0f, 0.0f};
    registry.emplace<moteur::Transform>(entity, transform);
    registry.emplace<moteur::ModelComponent>(entity, moteur::ModelComponent{two_parts(0.5f), {}});
    Recorder first;
    world.collect(first, 1.0f);
    REQUIRE(first.boxes.size() == 2);
    CHECK(world.cached() == 1);

    // Another model of as many parts, bigger: its boxes, not the old ones.
    registry.replace<moteur::ModelComponent>(entity, moteur::ModelComponent{two_parts(2.0f), {}});
    CHECK(world.cached() == 0);
    Recorder second;
    world.collect(second, 1.0f);
    REQUIRE(second.boxes.size() == 2);
    CHECK(second.boxes[0].min == glm::vec3(8.0f, -2.0f, -2.0f));
    CHECK(world.cached() == 1);

    // Patched, removed and added again: dropped each time.
    registry.patch<moteur::ModelComponent>(entity, [](moteur::ModelComponent& m) { m.model = two_parts(1.0f); });
    CHECK(world.cached() == 0);
    Recorder third;
    world.collect(third, 1.0f);
    REQUIRE(third.boxes.size() == 2);
    CHECK(third.boxes[0].min == glm::vec3(9.0f, -1.0f, -1.0f));
    registry.remove<moteur::ModelComponent>(entity);
    CHECK(world.cached() == 0);
    registry.emplace<moteur::ModelComponent>(entity, moteur::ModelComponent{two_parts(0.25f), {}});
    Recorder fourth;
    world.collect(fourth, 1.0f);
    REQUIRE(fourth.boxes.size() == 2);
    CHECK(fourth.boxes[0].min == glm::vec3(9.75f, -0.25f, -0.25f));
}

TEST_CASE("Review (world): Animator::load_state() is all or nothing") {
    const Rig rig;
    moteur::Animator a = moteur::Animator::create(rig.skeleton, rig.clips, rig.set);
    a.play("Idle");
    a.play("Attack", {.layer = 1, .loop = false});
    a.advance(7);
    a.set_speed(2.0);
    nlohmann::json broken = a.save_state();
    broken["layers"][1]["motions"][0]["clip"] = "nexiste_pas";  // read after the speeds and layer 0

    moteur::Animator b = moteur::Animator::create(rig.skeleton, rig.clips, rig.set);
    b.play("Run");
    b.advance(3);
    const nlohmann::json before = b.save_state();
    CHECK_THROWS_AS(b.load_state(broken), std::runtime_error);
    CHECK(b.save_state() == before);
    nlohmann::json no_layers = a.save_state();
    no_layers["layers"][1].erase("weights");  // a JSON error, late
    CHECK_THROWS_AS(b.load_state(no_layers), std::runtime_error);
    CHECK(b.save_state() == before);

    b.load_state(a.save_state());
    CHECK(b.save_state() == a.save_state());
}

TEST_CASE("Review (world): a state saved without a graph stops the running one") {
    const Rig rig;
    moteur::Animator plain = moteur::Animator::create(rig.skeleton, rig.clips, rig.set);
    plain.play("Idle");
    plain.advance(5);
    const nlohmann::json saved = plain.save_state();
    REQUIRE_FALSE(saved.contains("graph"));

    moteur::Animator running = moteur::Animator::create(rig.skeleton, rig.clips, rig.set);
    running.start_graph();
    running.set_parameter("vitesse", 2.0);
    running.advance(3);
    REQUIRE(running.graph_active());
    running.load_state(saved);
    CHECK_FALSE(running.graph_active());
    CHECK(running.save_state() == saved);
    // Advancing does not bring the old graph back.
    running.advance(10);
    plain.advance(10);
    CHECK(running.save_state() == plain.save_state());
}

TEST_CASE("Review (world): empty skeletons and clips answer safely") {
    const moteur::Skeleton empty;
    CHECK(empty.name().empty());
    CHECK(empty.data().joints.empty());
    CHECK(empty.joint_count() == 0);
    moteur::Skeleton made = moteur::Skeleton::create(body(), "body");
    const moteur::Skeleton taken = std::move(made);
    CHECK(taken.name() == "body");
    CHECK(made.name().empty());  // NOLINT(bugprone-use-after-move): moved-from is empty, documented
    CHECK(made.data().joints.empty());

    const moteur::SkeletalClip clip;
    CHECK(clip.name().empty());
    CHECK(clip.duration() == 0.0f);
    CHECK(clip.duration_ticks() == 1);
    CHECK(clip.bytes() == 0);
}

TEST_CASE("Review (world): ClipClock::set_state() clamps the time as set_time() does") {
    moteur::ClipClock once;
    once.set_state({10, true, 50 * moteur::ClipClock::kOne, moteur::ClipClock::kOne, true});
    CHECK(once.time() == 10 * moteur::ClipClock::kOne);
    CHECK(once.finished());
    CHECK(once.cycle_time() == once.cycle_length());
    once.set_state({10, true, -5, moteur::ClipClock::kOne, true});
    CHECK(once.time() == 0);
    moteur::ClipClock loop;
    loop.set_state({10, false, 50 * moteur::ClipClock::kOne, moteur::ClipClock::kOne, true});
    CHECK(loop.time() == 50 * moteur::ClipClock::kOne);  // a loop keeps counting
    loop.set_state({10, false, -5, moteur::ClipClock::kOne, true});
    CHECK(loop.time() == 0);
    CHECK(loop.cycle_time() == 0);
    // A valid state is given back exactly.
    const moteur::ClipClock::State valid{12, true, 7 * moteur::ClipClock::kOne + 250, 1500, true};
    moteur::ClipClock exact;
    exact.set_state(valid);
    CHECK(exact.state().time == valid.time);
}

TEST_CASE("Review (world): AnimationGraph::parse() throws std::runtime_error naming the source") {
    const nlohmann::json missing = nlohmann::json::parse(R"({ "layers": [ { "states": { "a": {} } } ] })");  // no start
    CHECK_THROWS_WITH_AS(moteur::AnimationGraph::parse(missing, "g.json"), doctest::Contains("'g.json': graph:"),
                         std::runtime_error);
    CHECK_THROWS_AS(moteur::AnimationGraph::parse(missing), std::runtime_error);
    const nlohmann::json wrong_type = nlohmann::json::parse(R"({ "layers": [ { "states": { "a": {} }, "start": 3 } ] })");
    CHECK_THROWS_AS(moteur::AnimationGraph::parse(wrong_type, "g.json"), std::runtime_error);
    const nlohmann::json bad_start = nlohmann::json::parse(R"({ "layers": [ { "states": { "a": {} }, "start": "b" } ] })");
    CHECK_THROWS_WITH_AS(moteur::AnimationGraph::parse(bad_start, "g.json"),
                         doctest::Contains("'g.json': graph > layers[0] > start: 'b' is not a state"), std::runtime_error);
    CHECK_THROWS_WITH_AS(moteur::AnimationGraph::parse(bad_start), "graph > layers[0] > start: 'b' is not a state of the layer",
                         std::runtime_error);
    // Through the set: the file named once, the JSON error a std::runtime_error as before.
    CHECK_THROWS_WITH_AS(moteur::AnimationSet::parse(R"({ "version": 1, "graph": { "layers": [ { "states": { "a": {} } } ] } })",
                                                     "set.json"),
                         doctest::Contains("Animation set 'set.json'"), std::runtime_error);
}
