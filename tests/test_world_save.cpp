#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "moteur/animator.hpp"
#include "moteur/collision.hpp"
#include "moteur/model.hpp"
#include "moteur/movement.hpp"
#include "moteur/skeleton.hpp"
#include "moteur/world.hpp"
#include "moteur/world_save.hpp"

// Milestone 7, part 6: the state of a world, saved and loaded.

namespace {

struct Health {
    float value = 1.0f;
};
struct Secret {  // a component nobody saves: reported
    int value = 0;
};

moteur::ComponentSerializers serializers() {
    moteur::ComponentSerializers s;
    s.add_engine_components();
    s.add<Health>(
        "sante", [](const Health& h, const moteur::SaveContext&) { return nlohmann::json(h.value); },
        [](entt::registry& r, entt::entity e, const nlohmann::json& j, const moteur::SaveContext&) {
            r.emplace_or_replace<Health>(e, j.get<float>());
        });
    return s;
}

}  // namespace

TEST_CASE("A world's persistent entities round-trip, references included") {
    entt::registry source;
    const entt::entity hero = source.create();
    source.emplace<moteur::PersistentId>(hero, 7u);
    moteur::Transform t;
    t.position = {1.1f, 0.0f, 2.3f};
    t.rotation = glm::quat(0.8f, 0.0f, 0.6f, 0.0f);
    source.emplace<moteur::Transform>(hero, t);
    source.emplace<moteur::Name>(hero, "héros");
    moteur::Mover mover;
    mover.speed = 3.25f;
    mover.go_to({9.5f, 4.5f});
    mover.path = {{2.0f, 3.0f}, {9.5f, 4.5f}};
    mover.needs_path = false;
    mover.next_point = 1;
    mover.facing = {0.6f, 0.8f};
    source.emplace<moteur::Mover>(hero, mover);
    moteur::Collider collider;
    collider.radius = 0.4f;
    collider.push_weight = 4;
    source.emplace<moteur::Collider>(hero, collider);
    source.emplace<Health>(hero, 0.37f);
    const entt::entity pet = source.create();
    source.emplace<moteur::PersistentId>(pet, 3u);
    source.emplace<moteur::Transform>(pet);
    source.emplace<moteur::Parent>(pet, hero);
    source.emplace<moteur::Hidden>(pet);
    (void)source.create();  // decor: not persistent, not saved

    const moteur::ComponentSerializers s = serializers();
    const nlohmann::json saved = nlohmann::json::parse(s.save(source).dump());  // through text, as in a file
    REQUIRE(saved.size() == 2);
    CHECK(saved[0]["id"] == 3);  // in the order of the identifiers

    entt::registry target;
    (void)target.create();  // something already there: EnTT identifiers differ
    std::vector<std::string> problems;
    s.load(target, saved, [](entt::registry& r, const nlohmann::json&) { return r.create(); }, problems);
    CHECK(problems.empty());
    entt::entity hero2 = entt::null, pet2 = entt::null;
    for (auto [entity, id] : target.view<moteur::PersistentId>().each()) {
        (id.value == 7 ? hero2 : pet2) = entity;
    }
    REQUIRE(hero2 != entt::entity{entt::null});
    REQUIRE(pet2 != entt::entity{entt::null});
    const moteur::Transform& t2 = target.get<moteur::Transform>(hero2);
    CHECK(t2.position == t.position);  // bit for bit
    CHECK(t2.rotation == t.rotation);
    CHECK(target.get<moteur::PreviousTransform>(hero2).value == t2);
    CHECK(target.get<moteur::Name>(hero2).value == "héros");
    const moteur::Mover& m2 = target.get<moteur::Mover>(hero2);
    CHECK(m2.speed == 3.25f);
    CHECK(m2.mode == moteur::MoveMode::ToPoint);
    CHECK(m2.path == mover.path);
    CHECK(m2.next_point == 1);
    CHECK(m2.facing == mover.facing);
    CHECK(target.get<moteur::Collider>(hero2).push_weight == 4);
    CHECK(target.get<Health>(hero2).value == 0.37f);
    CHECK(target.get<moteur::Parent>(pet2).entity == hero2);  // the reference, re-established
    CHECK(target.all_of<moteur::Hidden>(pet2));
}

TEST_CASE("Forgotten components and broken records are reported") {
    entt::registry registry;
    const entt::entity e = registry.create();
    registry.emplace<moteur::PersistentId>(e, 1u);
    registry.emplace<moteur::Transform>(e);
    registry.emplace<Secret>(e);
    const moteur::ComponentSerializers s = serializers();
    const std::vector<std::string> unknown = s.unknown_components(registry);
    REQUIRE(unknown.size() == 1);
    CHECK(unknown[0].find("Secret") != std::string::npos);

    const nlohmann::json records = nlohmann::json::parse(R"([
        {"id": 1, "c": {"inconnu": 1, "parent": 99, "transform": {"p": [0,0,0], "r": [1,0,0,0], "s": [1,1,1]}}},
        {"id": 2, "c": {}}
    ])");
    entt::registry target;
    std::vector<std::string> problems;
    s.load(target, records, [](entt::registry& r, const nlohmann::json& record) {
        return record["id"] == 2 ? entt::entity{entt::null} : r.create();
    }, problems);
    REQUIRE(problems.size() == 3);
    CHECK(problems[0].find("ne sait pas la recréer") != std::string::npos);
    CHECK(problems[1].find("composant inconnu") != std::string::npos);
    CHECK(problems[2].find("absent de la sauvegarde") != std::string::npos);
}

TEST_CASE("An Animator's state is saved and given back exactly") {
    const moteur::ModelData data = moteur::load_gltf(std::string(MOTEUR_SOURCE_ASSETS) + "models/skinned_reference.glb");
    const auto skeleton = moteur::make_asset(moteur::Skeleton::create(data.skeleton, "reference"));
    const auto clips = moteur::make_asset(moteur::ClipLibrary::create(data.skeleton, data.clips, "reference"));
    moteur::Animator a = moteur::Animator::create(skeleton, clips);
    const std::vector<std::string> names = clips->names();
    REQUIRE(names.size() >= 2);
    a.play(names[0], moteur::PlayOptions{0, 0, true, true, false});
    a.advance(23);
    a.play(names[1], moteur::PlayOptions{0, 10, false, true, false});  // a crossfade under way
    a.advance(4);
    a.set_speed(1.5);

    moteur::Animator b = moteur::Animator::create(skeleton, clips);
    b.load_state(nlohmann::json::parse(a.save_state().dump()));
    for (int tick = 0; tick < 60; ++tick) {
        a.advance(1);
        b.advance(1);
        CHECK(a.ticks() == b.ticks());
        CHECK(a.ratio(0.5f) == b.ratio(0.5f));
        CHECK(a.clip_name() == b.clip_name());
        CHECK(a.layer(0).motions.size() == b.layer(0).motions.size());
        for (std::size_t m = 0; m < a.layer(0).motions.size(); ++m) {
            CHECK(a.layer(0).motions[m].weight == b.layer(0).motions[m].weight);
            CHECK(a.layer(0).motions[m].clock.time() == b.layer(0).motions[m].clock.time());
        }
    }
    nlohmann::json broken = a.save_state();
    broken["layers"][0]["motions"][0]["clip"] = "nexiste_pas";
    CHECK_THROWS_AS(b.load_state(broken), std::runtime_error);
}
