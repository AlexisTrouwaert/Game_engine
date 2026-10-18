#include <doctest/doctest.h>

#include <map>
#include <memory>
#include <stdexcept>
#include <string>

#include "moteur/collision.hpp"
#include "moteur/prefab.hpp"
#include "moteur/world.hpp"

// Milestone 9, part 3: prefabs, nested and with variants, read by the reflection and instantiated
// into a World. No GPU: the prefabs refer to no asset here.

namespace prefab_test {

// A game's component with a reference to another entity of the prefab.
struct Lever {
    entt::entity door = entt::null;
    float force = 1.0f;
};
MOTEUR_REFLECT(Lever, MOTEUR_FIELD(door), MOTEUR_FIELD(force, {.min = 0.0}));

// Prefab files in memory, loaded on demand like Assets::prefab() does, cycles refused.
struct Library {
    std::map<std::string, std::string> files;
    std::map<std::string, std::shared_ptr<const moteur::Prefab>> loaded;
    std::vector<std::string> loading;
    moteur::DataIssues issues;

    std::shared_ptr<const moteur::Prefab> get(const std::string& path) {
        if (const auto found = loaded.find(path); found != loaded.end()) {
            return found->second;
        }
        if (std::find(loading.begin(), loading.end(), path) != loading.end()) {
            throw std::runtime_error("cycle at " + path);
        }
        const auto file = files.find(path);
        if (file == files.end()) {
            throw std::runtime_error("no prefab " + path);
        }
        loading.push_back(path);
        moteur::Prefab::Context context;
        context.prefab = [this](const std::string& other) { return get(other); };
        try {
            auto prefab = std::make_shared<const moteur::Prefab>(moteur::Prefab::parse(file->second, path, context, issues));
            loading.pop_back();
            return loaded[path] = prefab;
        } catch (...) {
            loading.pop_back();
            throw;
        }
    }
};

const char* const kLamp = R"({
  "version": 1,
  "entities": [
    {"id": 1, "name": "lamp", "components": {"transform": {"scale": [1, 1, 1]}, "collider": {"radius": 0.2}}},
    {"id": 2, "parent": 1, "name": "flame", "components": {
       "transform": {"position": [0, 3.5, 0]},
       "light": {"color": [1, 0.5, 0.25], "intensity": 12, "range": 12, "casts_shadows": true, "shadow_near": 0.55}}}
  ]
})";

const char* const kGate = R"({
  "version": 1,
  "entities": [
    {"id": 1, "name": "gate", "components": {"transform": {}}},
    {"id": 2, "parent": 1, "name": "door", "components": {"transform": {"position": [1, 0, 0]}}},
    {"id": 3, "parent": 1, "name": "lever", "components": {"transform": {}, "lever": {"door": 2, "force": 3}}},
    {"id": 4, "parent": 1, "prefab": "lamp", "components": {"transform": {"position": [-1, 0, 0]}},
     "overrides": {"2": {"light": {"intensity": 5}}}}
  ]
})";

const char* const kBlueGate = R"({
  "version": 1,
  "base": "gate",
  "overrides": {"4/2": {"light": {"color": [0.2, 0.4, 1]}}, "3": {"lever": {"force": 9, "door": null}}},
  "removed": ["2"],
  "entities": [{"id": 5, "parent": 1, "name": "banner", "components": {"transform": {"position": [0, 2, 0]}}}]
})";

}  // namespace prefab_test

using namespace prefab_test;

TEST_CASE("prefab: entities, parents and components read by their descriptions") {
    moteur::component_types().add<Lever>("lever");
    Library library;
    library.files = {{"lamp", kLamp}};
    const auto lamp = library.get("lamp");
    REQUIRE(lamp->entities().size() == 2);
    CHECK(lamp->entities()[0].path == "1");
    CHECK(lamp->entities()[0].parent == -1);
    CHECK(lamp->entities()[1].parent == 0);
    CHECK(lamp->entities()[1].name == "flame");

    moteur::World world;
    moteur::PrefabPlacement placement;
    placement.position = {10.0f, 0.0f, 5.0f};
    const moteur::PrefabInstance instance = moteur::instantiate(world, *lamp, placement);
    entt::registry& registry = world.registry();
    REQUIRE(instance.entities.size() == 2);
    CHECK(registry.get<moteur::Transform>(instance.root).position == glm::vec3(10.0f, 0.0f, 5.0f));
    CHECK(registry.get<moteur::Collider>(instance.root).radius == 0.2f);
    const entt::entity flame = instance.entities[1];
    CHECK(registry.get<moteur::Parent>(flame).entity == instance.root);
    CHECK(registry.get<moteur::Name>(flame).value == "flame");
    const moteur::LightSource& light = registry.get<moteur::LightSource>(flame);
    CHECK(light.intensity == 12.0f);
    CHECK(light.shadow_near == 0.55f);
    CHECK(light.casts_shadows);
    CHECK(registry.get<moteur::Transform>(flame).position.y == 3.5f);
    CHECK(world.world_position(flame, 1.0f) == glm::vec3(10.0f, 3.5f, 5.0f));
}

TEST_CASE("prefab: nested prefabs, overrides, references between entities, variants") {
    moteur::component_types().add<Lever>("lever");
    Library library;
    library.files = {{"lamp", kLamp}, {"gate", kGate}, {"blue_gate", kBlueGate}};
    const auto gate = library.get("gate");
    // 1 gate, 2 door, 3 lever, 4 (the lamp's root), 4/2 (its flame).
    REQUIRE(gate->entities().size() == 5);
    CHECK(gate->find("4/2") >= 0);
    CHECK(gate->dependencies() == std::vector<std::string>{"lamp"});

    moteur::World world;
    const moteur::PrefabInstance instance = moteur::instantiate(world, *gate);
    entt::registry& registry = world.registry();
    const auto entity = [&](const char* path) { return instance.entities[static_cast<std::size_t>(gate->find(path))]; };
    CHECK(registry.get<moteur::Name>(entity("4")).value == "lamp");
    CHECK(registry.get<moteur::Parent>(entity("4")).entity == entity("1"));
    CHECK(registry.get<moteur::Transform>(entity("4")).position == glm::vec3(-1.0f, 0.0f, 0.0f));
    CHECK(registry.get<moteur::Collider>(entity("4")).radius == 0.2f);  // the lamp's root keeps its components
    CHECK(registry.get<moteur::LightSource>(entity("4/2")).intensity == 5.0f);  // overridden
    CHECK(registry.get<moteur::LightSource>(entity("4/2")).range == 12.0f);     // the rest kept
    CHECK(registry.get<moteur::Parent>(entity("4/2")).entity == entity("4"));
    CHECK(registry.get<Lever>(entity("3")).door == entity("2"));  // mapped to the new entity
    CHECK(registry.get<Lever>(entity("3")).force == 3.0f);

    // The variant: a field changed deep inside, an entity removed, another added.
    const auto blue = library.get("blue_gate");
    CHECK(blue->find("2") == -1);
    CHECK(blue->find("5") >= 0);
    CHECK(blue->dependencies() == std::vector<std::string>{"gate", "lamp"});
    moteur::World other;
    const moteur::PrefabInstance variant = moteur::instantiate(other, *blue);
    const auto in_variant = [&](const char* path) { return variant.entities[static_cast<std::size_t>(blue->find(path))]; };
    const moteur::LightSource& light = other.registry().get<moteur::LightSource>(in_variant("4/2"));
    CHECK(light.color == glm::vec3(0.2f, 0.4f, 1.0f));
    CHECK(light.intensity == 5.0f);  // the base's override is kept
    CHECK(other.registry().get<Lever>(in_variant("3")).force == 9.0f);
    // The lever pointed to the removed door: the variant had to clear the reference (a reference
    // to an entity the variant no longer has is an error).
    CHECK(other.registry().get<Lever>(in_variant("3")).door == entt::entity{entt::null});
    library.files["broken_gate"] = R"({"version": 1, "base": "gate", "removed": ["2"]})";
    CHECK_THROWS_WITH_AS(library.get("broken_gate"), doctest::Contains("no entity '2'"), std::runtime_error);
}

TEST_CASE("prefab: per-instance overrides, as a level stores them") {
    Library library;
    library.files = {{"lamp", kLamp}};
    const auto lamp = library.get("lamp");
    moteur::World world;
    moteur::PrefabPlacement placement;
    placement.overrides = nlohmann::json::parse(R"({"2": {"light": {"intensity": 2}, "hidden": {}}, "9": {}})");
    moteur::DataIssues issues;
    const moteur::PrefabInstance instance = moteur::instantiate(world, *lamp, placement, &issues);
    CHECK(world.registry().get<moteur::LightSource>(instance.entities[1]).intensity == 2.0f);
    CHECK(world.registry().get<moteur::LightSource>(instance.entities[1]).range == 12.0f);
    CHECK(world.registry().all_of<moteur::Hidden>(instance.entities[1]));
    CHECK(issues.errors() == 1);  // entity 9
}

TEST_CASE("prefab: invalid files are refused with what is wrong") {
    Library library;
    const auto refused = [&library](const char* text, const char* expected) {
        library.files["bad"] = text;
        library.loaded.erase("bad");
        try {
            library.get("bad");
            FAIL("accepted: " << text);
        } catch (const std::runtime_error& e) {
            CHECK_MESSAGE(std::string(e.what()).find(expected) != std::string::npos, e.what());
        }
    };
    refused(R"({"version": 2})", "version");
    refused(R"({"version": 1, "entities": [{"id": 1}, {"id": 2}]})", "2 entities without a parent");
    refused(R"({"version": 1, "entities": [{"id": 1}, {"id": 1, "parent": 1}]})", "already used");
    refused(R"({"version": 1, "entities": [{"id": 1}, {"id": 2, "parent": 7}]})", "no entity '7'");
    refused(R"({"version": 1, "entities": [{"id": 1, "components": {"light": {"range": -1}}}]})", "out of range");
    refused(R"({"version": 1, "entities": [{"id": 1, "prefab": "bad"}]})", "cycle");
    refused(R"({"version": 1, "overrides": {}})", "for a variant");
    // An unknown component is left out with a warning, not an error.
    library.files["odd"] = R"({"version": 1, "entities": [{"id": 1, "components": {"sparkle": {}}}]})";
    CHECK(library.get("odd")->entities()[0].components.empty());
    CHECK(library.issues.text().find("sparkle: unknown component") != std::string::npos);
}
