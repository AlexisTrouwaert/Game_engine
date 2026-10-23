#include "materials_test.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>

#include "moteur/color.hpp"
#include "moteur/fixed_timestep.hpp"
#include "moteur/renderer.hpp"

namespace {

constexpr const char* kGhost = "models/fantome/fantome.glb";
constexpr const char* kLava = "materials/test/lave.material.json";
constexpr int kGhostTurn = 360;  // ticks for one turn round the pillar

glm::vec3 linear(float r, float g, float b) {
    return moteur::srgb_to_linear(glm::vec3(r, g, b));
}

moteur::Material surface(glm::vec3 color, float roughness) {
    moteur::Material material;
    material.base_color = glm::vec4(color, 1.0f);
    material.roughness = roughness;
    return material;
}

moteur::Material glass(glm::vec3 color, float alpha) {
    moteur::Material material = surface(color, 0.1f);
    material.base_color.a = alpha;
    material.blend = true;
    material.casts_shadow = false;
    material.double_sided = true;
    return material;
}

}  // namespace

MaterialsTest::MaterialsTest(moteur::Application& app, const Options& options, bool standalone)
    : app_(app), options_(options), standalone_(standalone) {
    moteur::Renderer& renderer = app.renderer();
    cube_ = moteur::make_asset(moteur::Mesh::create(renderer, moteur::make_cube(), "materials.cube"));
    sphere_ = moteur::make_asset(moteur::Mesh::create(renderer, moteur::make_sphere(0.5f, 32, 16), "materials.sphere"));
    plane_ = moteur::make_asset(moteur::Mesh::create(renderer, moteur::make_plane(), "materials.plane"));
    sky_ = moteur::Environment::create(renderer, moteur::make_sky(256, 128), "materials.sky");
    renderer.meshes().set_shadows(moteur::ShadowOptions{});
    renderer.meshes().set_culling(true);
    renderer.set_render_scale(1.0f);
    renderer.set_exposure(1.0f);

    entt::registry& registry = world_.registry();
    add_mesh(plane_, {0.0f, 0.0f, 0.0f}, {16.0f, 1.0f, 12.0f}, surface(linear(0.45f, 0.43f, 0.4f), 0.85f));

    // The pillar, the ghost round it, and the same ghost with its veil replaced (by name).
    add_mesh(cube_, {-4.0f, 1.5f, 0.0f}, {0.7f, 3.0f, 0.7f}, surface(linear(0.55f, 0.52f, 0.48f), 0.8f));
    if (app.assets().exists(kGhost)) {
        ghost_ = registry.create();
        registry.emplace<moteur::Transform>(ghost_);
        registry.emplace<moteur::ModelComponent>(ghost_, moteur::ModelComponent{app.assets().model(kGhost)});
        moteur::Material red = glass(linear(0.9f, 0.2f, 0.15f), 0.45f);
        red.emissive = linear(0.5f, 0.05f, 0.02f);
        red_ghost_ = registry.create();
        moteur::Transform place;
        place.position = {-6.5f, 0.2f, 3.0f};
        registry.emplace<moteur::Transform>(red_ghost_, place);
        moteur::ModelComponent model{app.assets().model(kGhost)};
        model.materials.push_back({"fantome_voile", moteur::make_asset(std::move(red))});
        registry.emplace<moteur::ModelComponent>(red_ghost_, std::move(model));
    }

    // Two panes of glass, one behind the other, and a sphere seen through them.
    add_mesh(cube_, {-0.6f, 1.0f, 2.5f}, {1.6f, 2.0f, 0.05f}, glass(linear(0.3f, 0.6f, 1.0f), 0.35f));
    add_mesh(cube_, {0.4f, 1.0f, 2.0f}, {1.6f, 2.0f, 0.05f}, glass(linear(1.0f, 0.7f, 0.2f), 0.35f));
    add_mesh(sphere_, {0.0f, 0.6f, 1.0f}, glm::vec3(1.2f), surface(linear(0.2f, 0.7f, 0.3f), 0.4f));

    // Dissolves: 0, 35 % and 70 %, then one going round.
    for (int i = 0; i < 3; ++i) {
        const entt::entity box = add_mesh(cube_, {2.5f + 1.6f * static_cast<float>(i), 0.6f, -2.5f}, glm::vec3(1.2f),
                                          surface(linear(0.7f, 0.68f, 0.62f), 0.6f));
        moteur::MaterialEffects effects;
        effects.dissolve = 0.35f * static_cast<float>(i);
        registry.emplace<moteur::MaterialEffects>(box, effects);
    }
    cycling_ = add_mesh(cube_, {7.3f, 0.6f, -2.5f}, glm::vec3(1.2f), surface(linear(0.7f, 0.68f, 0.62f), 0.6f));
    registry.emplace<moteur::MaterialEffects>(cycling_);
    flashing_ = add_mesh(sphere_, {3.5f, 0.6f, 1.5f}, glm::vec3(1.2f), surface(linear(0.6f, 0.15f, 0.12f), 0.5f));
    registry.emplace<moteur::MaterialEffects>(flashing_);

    // The material file: lava flowing and beating.
    if (app.assets().exists(kLava)) {
        const entt::entity lava = registry.create();
        moteur::Transform place;
        place.position = {6.0f, 0.02f, 2.0f};
        place.scale = {3.0f, 1.0f, 2.0f};
        registry.emplace<moteur::Transform>(lava, place);
        registry.emplace<moteur::MeshComponent>(lava, plane_, *app.assets().material(kLava));
    }

    camera_.set_angles(-30.0f, 35.0f);
    camera_.set_visible_height(9.0f);
    camera_.set_target({0.5f, 0.8f, 0.5f});
    camera_.begin_update();
}

entt::entity MaterialsTest::add_mesh(const moteur::Asset<moteur::Mesh>& mesh, glm::vec3 position, glm::vec3 scale,
                                     const moteur::Material& material) {
    entt::registry& registry = world_.registry();
    const entt::entity entity = registry.create();
    moteur::Transform place;
    place.position = position;
    place.scale = scale;
    registry.emplace<moteur::Transform>(entity, place);
    registry.emplace<moteur::MeshComponent>(entity, mesh, material);
    return entity;
}

void MaterialsTest::update(double dt) {
    elapsed_ += dt;
    if (options_.run_seconds > 0.0 && elapsed_ >= options_.run_seconds) {
        if (standalone_) {
            app_.quit();
        } else {
            stop_requested_ = true;
        }
    }
    camera_.begin_update();
    world_.begin_tick();
    if (options_.freeze_after_ticks > 0 && ticks_ >= options_.freeze_after_ticks) {
        return;
    }
    ++ticks_;
    entt::registry& registry = world_.registry();
    if (ghost_ != entt::null) {
        const float angle = static_cast<float>(ticks_ % kGhostTurn) / kGhostTurn * glm::two_pi<float>();
        moteur::Transform& transform = registry.get<moteur::Transform>(ghost_);
        transform.position = glm::vec3(-4.0f + 1.5f * std::cos(angle), 0.2f, 1.5f * std::sin(angle));
        transform.rotation = glm::angleAxis(-angle, glm::vec3(0.0f, 1.0f, 0.0f));
        registry.patch<moteur::Transform>(ghost_);
    }
    registry.get<moteur::MaterialEffects>(cycling_).dissolve = static_cast<float>(ticks_ % 240) / 240.0f;
    moteur::MaterialEffects& flash = registry.get<moteur::MaterialEffects>(flashing_);
    if (ticks_ % moteur::tick_rate() == 0) {
        flash.flash = {3.0f, 2.7f, 2.3f, 1.0f};
    } else {
        flash.flash.a = std::max(0.0f, flash.flash.a - 0.1f);
    }
}

void MaterialsTest::render(moteur::Renderer& renderer, double alpha) {
    const bool frozen = options_.freeze_after_ticks > 0 && ticks_ >= options_.freeze_after_ticks;
    if (!options_.capture_path.empty() && !capture_done_ && frozen) {
        renderer.request_capture(options_.capture_path);
        capture_done_ = true;
    }
    renderer.set_clear_color(0.05f, 0.05f, 0.07f);
    camera_.set_viewport({static_cast<float>(renderer.width()), static_cast<float>(renderer.height())});
    const moteur::Camera3D camera = camera_.interpolated(frozen ? 1.0 : alpha);
    moteur::MeshRenderer& meshes = renderer.meshes();
    meshes.set_camera(camera.view_projection(), camera.position());
    meshes.set_time(static_cast<double>(ticks_) / moteur::tick_rate());
    renderer.billboards().set_camera(camera);
    meshes.set_sun(glm::normalize(glm::vec3(0.5f, 0.8f, 0.35f)), linear(1.0f, 0.93f, 0.8f), 2.5f);
    meshes.set_environment(&*sky_, 0.8f);
    moteur::CollectOptions collect;
    collect.view = camera.frustum();
    world_.submit(renderer, frozen ? 1.0f : static_cast<float>(alpha), collect);
}

void MaterialsTest::draw_controls() {
    ImGui::Text("Tick %ld; the ghost goes round the pillar in %d ticks", ticks_, kGhostTurn);
    const moteur::RenderStats& stats = app_.renderer().stats();
    ImGui::Text("Meshes %d, draw calls %d (meshes %d)", stats.meshes, stats.draw_calls, stats.mesh_draw_calls);
}
