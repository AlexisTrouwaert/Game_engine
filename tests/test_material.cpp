// Materials of the game (milestone 9 bis, part 2): the material file, a model's materials replaced
// by name, the effects of an entity given to its meshes.

#include <doctest/doctest.h>

#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include <stdexcept>
#include <string>
#include <vector>

#include "moteur/component_types.hpp"
#include "moteur/material.hpp"
#include "moteur/model.hpp"
#include "moteur/world.hpp"

namespace {

// Textures are not created: the loader records what it was asked for.
struct Loads {
    std::vector<std::pair<std::string, moteur::MaterialTexture>> asked;
    moteur::MaterialTextureLoader loader() {
        return [this](const nlohmann::json& reference, moteur::MaterialTexture role) {
            asked.emplace_back(reference.get<std::string>(), role);
            return moteur::Asset<moteur::Texture>{};
        };
    }
};

std::string error_of(const char* text) {
    Loads loads;
    try {
        moteur::parse_material(text, "m.material.json", loads.loader());
    } catch (const std::runtime_error& e) {
        return e.what();
    }
    return {};
}

}  // namespace

TEST_CASE("material file: values, textures by role, defaults") {
    Loads loads;
    const moteur::Material m = moteur::parse_material(R"({
        "version": 1,
        "base_color": [0.6, 0.8, 0.9, 0.4],
        "base_color_texture": "textures/voile.png",
        "normal_texture": "textures/voile_n.png",
        "metallic_roughness_texture": "textures/voile_mr.png",
        "roughness": 0.85,
        "emissive": [0.2, 0.4, 0.5], "emissive_strength": 2,
        "alpha": "blend",
        "double_sided": true,
        "uv_scroll": [0.1, -0.5],
        "emissive_pulse": 1.5
    })", "voile.material.json", loads.loader());
    CHECK(m.base_color == glm::vec4(0.6f, 0.8f, 0.9f, 0.4f));
    CHECK(m.roughness == doctest::Approx(0.85f));
    CHECK(m.metallic == 0.0f);  // the default
    CHECK(m.emissive.y == doctest::Approx(0.8f));
    CHECK(m.blend);
    CHECK_FALSE(m.casts_shadow);  // a transparent material casts no shadow
    CHECK(m.alpha_cutoff == 0.0f);
    CHECK(m.double_sided);
    CHECK(m.uv_scroll == glm::vec2(0.1f, -0.5f));
    CHECK(m.emissive_pulse == 1.5f);
    REQUIRE(loads.asked.size() == 3);
    CHECK(loads.asked[0] == std::pair<std::string, moteur::MaterialTexture>{"textures/voile.png", moteur::MaterialTexture::Color});
    CHECK(loads.asked[1].second == moteur::MaterialTexture::Data);
    CHECK(loads.asked[2].second == moteur::MaterialTexture::Normal);

    const moteur::Material mask = moteur::parse_material(R"({"alpha": "mask", "base_color": [1, 1, 1]})", "m", loads.loader());
    CHECK(mask.alpha_cutoff == 0.5f);  // glTF's default
    CHECK(mask.base_color.a == 1.0f);
    CHECK(mask.casts_shadow);
}

TEST_CASE("material file: mistakes are errors that name the file and the key") {
    CHECK(error_of(R"({"roughnes": 0.5})").find("unknown key 'roughnes'") != std::string::npos);
    CHECK(error_of(R"({"roughness": 2})").find("m.material.json > roughness") != std::string::npos);
    CHECK(error_of(R"({"base_color": [1, 1]})").find("base_color") != std::string::npos);
    CHECK(error_of(R"({"alpha": "glass"})").find("opaque, mask or blend") != std::string::npos);
    CHECK(error_of(R"({"alpha_cutoff": 0.5})").find("only for") != std::string::npos);
    CHECK(error_of(R"({"version": 2})").find("version 2") != std::string::npos);
    CHECK(error_of(R"({"double_sided": 1})").find("true or false") != std::string::npos);
    CHECK(error_of("[1, 2]").find("JSON object") != std::string::npos);
    CHECK(error_of("{").find("m.material.json") == 0);
    CHECK(error_of(R"({"alpha": "blend", "casts_shadow": true})").empty());  // accepted, still no shadow
}

namespace {

struct Recorder final : moteur::WorldSink {
    std::vector<glm::vec4> colors;      // base color of each mesh given
    std::vector<float> dissolves;       // the effects in force for each mesh
    const moteur::MaterialEffects* effects_now = nullptr;
    void mesh(const moteur::Mesh&, const glm::mat4&, const moteur::Material& material, const moteur::Aabb&) override {
        colors.push_back(material.base_color);
        dissolves.push_back(effects_now != nullptr ? effects_now->dissolve : 0.0f);
    }
    void light(const moteur::PointLight&) override {}
    void billboard(const moteur::Texture&, glm::vec3, glm::vec2, const moteur::BillboardOptions&) override {}
    void effects(const moteur::MaterialEffects* effects) override { effects_now = effects; }
};

// A model of two parts, materials "pierre" (grey) and "voile" (white), without GPU buffers.
moteur::Asset<moteur::Model> two_parts() {
    moteur::Model model;
    for (int i = 0; i < 2; ++i) {
        moteur::Model::Part part;
        part.mesh.index_count = 3;
        part.mesh.bounds.add(glm::vec3(-0.5f));
        part.mesh.bounds.add(glm::vec3(0.5f));
        part.material = i;
        model.parts.push_back(std::move(part));
    }
    moteur::Material stone;
    stone.base_color = {0.5f, 0.5f, 0.5f, 1.0f};
    model.materials = {stone, moteur::Material{}};
    model.material_names = {"pierre", "voile"};
    model.bounds.add(glm::vec3(-0.5f));
    model.bounds.add(glm::vec3(0.5f));
    return moteur::make_asset(std::move(model));
}

}  // namespace

TEST_CASE("World: a model's material replaced by name, the effects of an entity on its meshes") {
    moteur::World world;
    entt::registry& registry = world.registry();
    const moteur::Asset<moteur::Model> model = two_parts();
    moteur::Material ghost;
    ghost.base_color = {0.6f, 0.8f, 0.9f, 0.4f};
    ghost.blend = true;

    const entt::entity plain = registry.create();
    registry.emplace<moteur::Transform>(plain);
    registry.emplace<moteur::ModelComponent>(plain, moteur::ModelComponent{model});
    const entt::entity haunted = registry.create();
    registry.emplace<moteur::Transform>(haunted);
    moteur::ModelComponent replaced{model};
    replaced.materials.push_back({"voile", moteur::make_asset(moteur::Material(ghost))});
    replaced.materials.push_back({"absent", moteur::make_asset(moteur::Material(ghost))});  // no such material: ignored
    registry.emplace<moteur::ModelComponent>(haunted, replaced);
    moteur::MaterialEffects dying;
    dying.dissolve = 0.25f;
    registry.emplace<moteur::MaterialEffects>(haunted, dying);

    Recorder sink;
    world.collect(sink, 1.0f);
    REQUIRE(sink.colors.size() == 4);
    CHECK(sink.colors[0] == glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
    CHECK(sink.colors[1] == glm::vec4(1.0f));
    CHECK(sink.colors[2] == glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));  // "pierre" kept
    CHECK(sink.colors[3] == ghost.base_color);                    // "voile" replaced
    CHECK(sink.dissolves == std::vector<float>{0.0f, 0.0f, 0.25f, 0.25f});

    // The component goes through the reflection (prefabs, levels, saves).
    const moteur::ComponentType* type = moteur::component_types().find<moteur::MaterialEffects>();
    REQUIRE(type != nullptr);
    CHECK(type->name == "material_effects");
}
