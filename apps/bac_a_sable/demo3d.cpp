#include "demo3d.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "moteur/billboard_renderer.hpp"
#include "moteur/color.hpp"
#include "moteur/fixed_timestep.hpp"
#include "moteur/image.hpp"
#include "moteur/mesh_renderer.hpp"
#include "moteur/paths.hpp"
#include "moteur/sprite_renderer.hpp"

namespace {

constexpr float kWallHeight = 1.5f;       // metres: high enough to block, low enough to see over
constexpr float kCreatureSpeed = 2.0f;    // cells per second, as in the 2D demo
constexpr float kSentSpeed = 3.0f;        // cells per second, when sent somewhere by a click
constexpr int kRoom = 10;                 // the walls make rooms of 10 x 10 cells (see demo_wall)

// A color picked by eye (sRGB) turned into the linear value the lighting works with.
glm::vec3 linear(float r, float g, float b) {
    return moteur::srgb_to_linear(glm::vec3(r, g, b));
}

moteur::Material surface(glm::vec3 color, float roughness, float metallic = 0.0f) {
    moteur::Material material;
    material.base_color = glm::vec4(color, 1.0f);
    material.roughness = roughness;
    material.metallic = metallic;
    return material;
}

moteur::Material glowing(glm::vec3 emissive) {
    moteur::Material material;
    material.base_color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    material.emissive = emissive;
    material.casts_shadow = false;
    return material;
}

moteur::Transform place(glm::vec3 position, glm::vec3 scale, float yaw = 0.0f) {
    moteur::Transform transform;
    transform.position = position;
    transform.rotation = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
    transform.scale = scale;
    return transform;
}

// The demo's own components: the game's, not the engine's.
struct Walker {  // a creature
    glm::vec2 velocity;             // cells per second
    std::optional<glm::vec2> goal;  // sent there by a click
};
struct Health {
    float value;  // [0, 1]
};

// Centre of a room, where its brazier stands.
bool is_brazier_cell(int i, int j) {
    return i % kRoom == kRoom / 2 && j % kRoom == kRoom / 2;
}

}  // namespace

Demo3D::Demo3D(moteur::Application& app, const Options& options, bool standalone)
    : app_(app), options_(options), standalone_(standalone) {
    options_.map_size = std::clamp(options_.map_size, 10, 400);
    options_.creatures = std::max(options_.creatures, 0);
    options_.decor = std::max(options_.decor, 0);
    moteur::Renderer& renderer = app.renderer();

    cube_ = moteur::make_asset(moteur::Mesh::create(renderer, moteur::make_cube(), "demo.cube"));
    tile_ = moteur::make_asset(moteur::Mesh::create(renderer, moteur::make_plane(), "demo.tile"));
    sphere_ = moteur::make_asset(moteur::Mesh::create(renderer, moteur::make_sphere(0.5f, 16, 8), "demo.sphere"));
    rock_ = moteur::make_asset(moteur::Mesh::create(renderer, moteur::make_sphere(0.5f, 7, 4), "demo.rock"));  // faceted
    font_ = app.assets().font(kFont, kFontPixelHeight);
    sky_ = moteur::Environment::create(renderer, moteur::make_sky(512, 256), "demo.sky");
    if (app.assets().exists(kBarrel)) {
        barrel_ = app.assets().model(kBarrel);
    } else {
        SDL_Log("Demo 3D: no barrel model (%s), crates instead", kBarrel);  // tools/models/fetch_test_models.py
    }

    // A soft round glow (torch halos), and a white pixel (health bars).
    moteur::Image glow;
    glow.width = 64;
    glow.height = 64;
    glow.pixels.resize(64 * 64 * 4);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            const float dx = (static_cast<float>(x) + 0.5f) / 32.0f - 1.0f;
            const float dy = (static_cast<float>(y) + 0.5f) / 32.0f - 1.0f;
            const float r = std::min(std::sqrt(dx * dx + dy * dy), 1.0f);
            const auto i = static_cast<std::size_t>((y * 64 + x) * 4);
            glow.pixels[i] = glow.pixels[i + 1] = glow.pixels[i + 2] = 255;
            glow.pixels[i + 3] = static_cast<std::uint8_t>(std::lround((1.0f - r) * (1.0f - r) * 255.0f));
        }
    }
    moteur::TextureSettings mask;
    mask.mipmaps = true;
    glow_ = moteur::make_asset(renderer.create_texture(glow, mask, "demo.glow"));
    moteur::Image white;
    white.width = 1;
    white.height = 1;
    white.pixels = {255, 255, 255, 255};
    white_ = renderer.create_texture(white, "demo.white");

    build_map();
    build_floor();
    build_decor();
    spawn_creatures();
    light_braziers();
    if (options_.hero) {
        spawn_hero();
        moteur::Assets& assets = app.assets();
        if (assets.exists(kAmbience)) {
            for (const char* path : kSteps) {
                steps_.push_back(assets.sound(path));
            }
            for (const char* path : kImpacts) {
                impacts_.push_back(assets.sound(path));
            }
            ambience_ = assets.music(kAmbience);
            moteur::PlaySound ambience;
            ambience.group = moteur::SoundGroup::Ambience;
            ambience.volume = 0.5f;
            ambience.loop = true;
            ambience.fade_in = 1.5f;
            ambience_voice_ = app.audio().play_stream(ambience_, ambience);
        }
    }

    // The engine's defaults, whatever an earlier test changed.
    moteur::ShadowOptions shadows;
    if (options_.point_budget >= 0) {
        shadows.point_budget = options_.point_budget;
    }
    renderer.meshes().set_shadows(shadows);
    renderer.meshes().set_culling(true);
    renderer.set_render_scale(1.0f);
    renderer.set_exposure(1.0f);

    const auto middle = static_cast<float>(options_.map_size) * 0.5f;
    camera_.set_target({middle, 0.0f, middle});
    if (hero_ != entt::null) {
        const glm::vec2 start = cell_position(hero_);
        camera_.set_target({start.x, 0.0f, start.y});
        follow_ = true;
    }
    if (options_.fake_mouse) {
        mouse_ = options_.fake_mouse_position;
    }

    declare_actions(app);
    moteur::Input& input = app.input();
    actions_ = find_actions(input);
    input.set_enabled(!options_.no_input);

    // The debug tools (menu mode): the world in the inspector, with the demo's own components.
    if (moteur::DebugTools* tools = app.debug_tools()) {
        tools->watch(world_, "Démo 3D");
        tools->components().add<Walker>("Marcheur (démo)", [](Walker& walker) {
            bool changed = ImGui::DragFloat2("Vitesse (cases/s)", &walker.velocity.x, 0.05f);
            if (walker.goal) {
                changed |= ImGui::DragFloat2("Destination", &walker.goal->x, 0.05f);
                ImGui::SameLine();
                if (ImGui::SmallButton("Oublier")) {
                    walker.goal.reset();
                    changed = true;
                }
            } else {
                ImGui::TextDisabled("Pas de destination");
            }
            return changed;
        });
        tools->components().add<Health>("Santé (démo)", [](Health& health) {
            return ImGui::SliderFloat("Vie", &health.value, 0.0f, 1.0f);
        });
    }
}

void Demo3D::declare_actions(moteur::Application& app) {
    // The actions, then their bindings: the defaults, and the player's changes over them.
    moteur::Input& input = app.input();
    input.clear_actions();
    input.add_button("move_to");
    input.add_axis("move");
    input.add_axis("camera");
    for (const char* name : {"zoom_in", "zoom_out", "select", "next", "follow", "projection"}) {
        input.add_button(name);
    }
    for (int i = 0; i < kSkills; ++i) {
        input.add_button("skill_" + std::to_string(i + 1));
    }
    input.add_button("pause");
    for (const char* name : {"menu_up", "menu_down", "menu_confirm"}) {
        input.add_button(name);
    }
    input.load_bindings(moteur::read_text_file(app.assets().file_path("input/demo3d.json")), "input/demo3d.json");
    const std::string directory = app.preferences_directory();
    const std::string user_file = directory.empty() ? std::string() : directory + "demo3d_bindings.json";
    if (!user_file.empty() && SDL_GetPathInfo(user_file.c_str(), nullptr)) {
        try {
            input.apply_user_bindings(moteur::read_text_file(user_file), user_file);
        } catch (const std::exception& e) {
            SDL_Log("Demo 3D: %s (the player's bindings are ignored)", e.what());
        }
    }
}

Demo3D::Actions Demo3D::find_actions(const moteur::Input& input) {
    Actions actions{};
    actions.move_to = input.find("move_to");
    actions.move = input.find("move");
    actions.camera = input.find("camera");
    actions.zoom_in = input.find("zoom_in");
    actions.zoom_out = input.find("zoom_out");
    actions.select = input.find("select");
    actions.next = input.find("next");
    actions.follow = input.find("follow");
    actions.projection = input.find("projection");
    for (int i = 0; i < kSkills; ++i) {
        actions.skills[i] = input.find("skill_" + std::to_string(i + 1));
    }
    actions.pause = input.find("pause");
    return actions;
}

std::string Demo3D::user_bindings_path() const {
    const std::string directory = app_.preferences_directory();
    return directory.empty() ? std::string() : directory + "demo3d_bindings.json";
}

Demo3D::~Demo3D() {
    if (moteur::DebugTools* tools = app_.debug_tools()) {
        tools->forget(world_);
    }
    if (ambience_voice_ != moteur::kNoSound) {
        app_.audio().stop(ambience_voice_, 0.5f);
    }
}

void Demo3D::add_static(const moteur::Asset<moteur::Mesh>& mesh, const moteur::Transform& transform, const moteur::Material& material) {
    entt::registry& registry = world_.registry();
    const entt::entity entity = registry.create();
    registry.emplace<moteur::Transform>(entity, transform);
    registry.emplace<moteur::MeshComponent>(entity, mesh, material);
    ++static_draws_;
}

void Demo3D::build_map() {
    const int n = options_.map_size;
    ground_ = tileset_.add({"", true, false});
    wall_ = tileset_.add({"", false, true});
    map_.emplace(n, n, 2);
    map_->fill(0, ground_);
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            if (demo_wall(i, j)) {
                map_->set(1, {i, j}, wall_);
            }
        }
    }
}

void Demo3D::build_floor() {
    const int n = options_.map_size;
    // The floor receives shadows but casts none: it is below everything.
    moteur::Material light = surface(linear(0.55f, 0.58f, 0.5f), 0.85f);
    moteur::Material dark = surface(linear(0.44f, 0.47f, 0.4f), 0.85f);
    light.casts_shadow = false;
    dark.casts_shadow = false;
    if (!options_.merged_floor) {
        // One tile per cell: the case measured against the merged floor below.
        for (int j = 0; j < n; ++j) {
            for (int i = 0; i < n; ++i) {
                const glm::vec3 centre(static_cast<float>(i) + 0.5f, 0.0f, static_cast<float>(j) + 0.5f);
                add_static(tile_, place(centre, glm::vec3(1.0f)), ((i + j) & 1) == 0 ? light : dark);
            }
        }
    } else {
        // One mesh per block of 10 x 10 cells and per color: few draws, but no culling inside a block.
        const int blocks = (n + kRoom - 1) / kRoom;
        const moteur::MeshData quad = moteur::make_plane();
        for (int bj = 0; bj < blocks; ++bj) {
            for (int bi = 0; bi < blocks; ++bi) {
                for (int tone = 0; tone < 2; ++tone) {
                    moteur::MeshData block;
                    for (int j = bj * kRoom; j < std::min((bj + 1) * kRoom, n); ++j) {
                        for (int i = bi * kRoom; i < std::min((bi + 1) * kRoom, n); ++i) {
                            if (((i + j) & 1) != tone) {
                                continue;
                            }
                            const auto base = static_cast<std::uint32_t>(block.vertices.size());
                            for (moteur::Vertex3D vertex : quad.vertices) {
                                vertex.position += glm::vec3(static_cast<float>(i) + 0.5f, 0.0f, static_cast<float>(j) + 0.5f);
                                block.vertices.push_back(vertex);
                            }
                            for (const std::uint32_t index : quad.indices) {
                                block.indices.push_back(base + index);
                            }
                        }
                    }
                    if (block.indices.empty()) {
                        continue;
                    }
                    add_static(moteur::make_asset(moteur::Mesh::create(app_.renderer(), block, "demo.floor block")),
                               moteur::Transform{}, tone == 0 ? light : dark);
                }
            }
        }
    }
}

void Demo3D::build_decor() {
    const int n = options_.map_size;
    // Walls: a 1.5 m block per wall cell.
    const moteur::Material stone = surface(linear(0.62f, 0.58f, 0.52f), 0.8f);
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            if (demo_wall(i, j)) {
                add_static(cube_, place({static_cast<float>(i) + 0.5f, kWallHeight * 0.5f, static_cast<float>(j) + 0.5f},
                                        {1.0f, kWallHeight, 1.0f}),
                           stone);
            }
        }
    }

    // A brazier in the middle of each room: a post, and a flame whose light is a torch (see
    // light_braziers()).
    const moteur::Material iron = surface(linear(0.3f, 0.3f, 0.32f), 0.5f, 1.0f);
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            if (is_brazier_cell(i, j)) {
                const glm::vec3 foot(static_cast<float>(i) + 0.5f, 0.0f, static_cast<float>(j) + 0.5f);
                add_static(cube_, place(foot + glm::vec3(0.0f, 0.6f, 0.0f), {0.25f, 1.2f, 0.25f}), iron);
                braziers_.push_back(foot + glm::vec3(0.0f, 1.45f, 0.0f));
            }
        }
    }

    // Rocks, trees, crates and barrels, on free cells, the same on every run of a seed.
    Random random(options_.seed + 7);
    const moteur::Material rock = surface(linear(0.5f, 0.5f, 0.48f), 0.9f);
    const moteur::Material bark = surface(linear(0.35f, 0.24f, 0.15f), 0.9f);
    const moteur::Material leaves = surface(linear(0.22f, 0.42f, 0.18f), 0.8f);
    const moteur::Material wood = surface(linear(0.6f, 0.44f, 0.26f), 0.7f);
    for (int k = 0; k < options_.decor; ++k) {
        glm::ivec2 cell(0);
        do {
            cell = {static_cast<int>(random.next() * static_cast<float>(n)), static_cast<int>(random.next() * static_cast<float>(n))};
        } while (!map_->walkable(tileset_, cell) || is_brazier_cell(cell.x, cell.y));
        const glm::vec3 spot(static_cast<float>(cell.x) + 0.2f + 0.6f * random.next(), 0.0f,
                             static_cast<float>(cell.y) + 0.2f + 0.6f * random.next());
        const float yaw = random.next() * glm::two_pi<float>();
        const float kind = random.next();
        const float size = random.next();
        if (kind < 0.4f) {
            const glm::vec3 scale = (0.3f + 0.5f * size) * glm::vec3(1.0f, 0.55f + 0.3f * random.next(), 0.8f + 0.4f * random.next());
            add_static(rock_, place(spot + glm::vec3(0.0f, scale.y * 0.3f, 0.0f), scale, yaw), rock);
        } else if (kind < 0.65f) {
            const float height = 1.0f + 0.8f * size;
            add_static(cube_, place(spot + glm::vec3(0.0f, height * 0.5f, 0.0f), {0.16f, height, 0.16f}, yaw), bark);
            const float crown = 0.9f + 0.7f * size;
            add_static(sphere_, place(spot + glm::vec3(0.0f, height + crown * 0.3f, 0.0f), glm::vec3(crown, crown * 0.85f, crown)), leaves);
        } else if (kind < 0.9f || !barrel_) {
            const float edge = 0.45f + 0.35f * size;
            add_static(cube_, place(spot + glm::vec3(0.0f, edge * 0.5f, 0.0f), glm::vec3(edge), yaw), wood);
        } else {
            entt::registry& registry = world_.registry();
            const entt::entity barrel = registry.create();
            registry.emplace<moteur::Transform>(barrel, place(spot, glm::vec3(1.0f), yaw));
            registry.emplace<moteur::ModelComponent>(barrel, barrel_);
            static_draws_ += barrel_->parts.size();
        }
    }
}

void Demo3D::spawn_creatures() {
    // As in the 2D demo: anywhere walkable, one of eight directions, each at its own pace.
    static constexpr glm::vec2 kDirections[8] = {
        {-1.0f, -1.0f}, {-1.0f, 0.0f}, {-1.0f, 1.0f}, {0.0f, -1.0f}, {0.0f, 1.0f}, {1.0f, -1.0f}, {1.0f, 0.0f}, {1.0f, 1.0f},
    };
    Random random(options_.seed);
    const auto n = static_cast<float>(options_.map_size);
    entt::registry& registry = world_.registry();
    const moteur::Material skin = surface(linear(0.85f, 0.7f, 0.55f), 0.6f);
    // A part of the creature, attached to it.
    const auto attach = [&registry](entt::entity creature, const moteur::Asset<moteur::Mesh>& mesh,
                                    const moteur::Transform& transform, const moteur::Material& material) {
        const entt::entity part = registry.create();
        registry.emplace<moteur::Transform>(part, transform);
        registry.emplace<moteur::Parent>(part, creature);
        registry.emplace<moteur::MeshComponent>(part, mesh, material);
        return part;
    };
    for (int k = 0; k < options_.creatures; ++k) {
        glm::vec2 position(0.0f);
        do {
            position = {1.0f + random.next() * (n - 2.0f), 1.0f + random.next() * (n - 2.0f)};
        } while (!walkable(position));
        const glm::vec2 direction = kDirections[static_cast<std::size_t>(random.next() * 8.0f) % 8];
        const float pace = 0.75f + 0.5f * random.next();
        const glm::vec3 color = linear(0.35f + 0.55f * random.next(), 0.35f + 0.55f * random.next(), 0.35f + 0.55f * random.next());
        const float health = 0.3f + 0.7f * random.next();
        // The creature is where its feet are; its body and head are attached to it.
        const entt::entity creature = registry.create();
        const moteur::Transform foot = place({position.x, 0.0f, position.y}, glm::vec3(1.0f));
        registry.emplace<moteur::Transform>(creature, foot);
        registry.emplace<moteur::PreviousTransform>(creature, foot);
        registry.emplace<Walker>(creature, direction * (kCreatureSpeed * pace), std::nullopt);
        registry.emplace<Health>(creature, health);
        registry.emplace<moteur::Name>(creature, "créature " + std::to_string(k));
        attach(creature, cube_, place({0.0f, 0.5f, 0.0f}, {0.45f, 1.0f, 0.45f}), surface(color, 0.7f));
        attach(creature, sphere_, place({0.0f, 1.2f, 0.0f}, glm::vec3(0.38f)), skin);
        if (k == 0) {
            selected_ = creature;
        }
        ++creatures_;
    }
    if (selected_ == entt::null) {
        return;
    }
    // Start next to the first creature, selected: a ring under it, and a mark where it is sent.
    const glm::vec2 first = cell_position(selected_);
    camera_.set_target({first.x, 0.0f, first.y});
    ring_ = attach(selected_, tile_, place({0.0f, 0.01f, 0.0f}, glm::vec3(1.1f)), glowing({0.3f, 1.2f, 0.4f}));
    registry.emplace<moteur::Name>(ring_, "sélection");
    goal_ = registry.create();
    registry.emplace<moteur::Transform>(goal_, place(glm::vec3(0.0f), glm::vec3(0.4f)));
    registry.emplace<moteur::MeshComponent>(goal_, tile_, glowing({1.2f, 1.0f, 0.2f}));
    registry.emplace<moteur::Hidden>(goal_);
    registry.emplace<moteur::Name>(goal_, "destination");
}

void Demo3D::spawn_hero() {
    entt::registry& registry = world_.registry();
    glm::vec2 start(2.5f, 2.5f);  // the first room, away from its brazier
    while (!walkable(start)) {
        start.x += 1.0f;
    }
    hero_ = registry.create();
    const moteur::Transform foot = place({start.x, 0.0f, start.y}, glm::vec3(1.0f));
    registry.emplace<moteur::Transform>(hero_, foot);
    registry.emplace<moteur::PreviousTransform>(hero_, foot);
    registry.emplace<Walker>(hero_, glm::vec2(0.0f), std::nullopt);
    registry.emplace<Health>(hero_, 1.0f);
    registry.emplace<moteur::Name>(hero_, "héros");
    // Taller than the creatures, in blue steel, with a sword at his side.
    const auto part = [&](const moteur::Asset<moteur::Mesh>& mesh, const moteur::Transform& transform, const moteur::Material& material) {
        const entt::entity entity = registry.create();
        registry.emplace<moteur::Transform>(entity, transform);
        registry.emplace<moteur::Parent>(entity, hero_);
        registry.emplace<moteur::MeshComponent>(entity, mesh, material);
    };
    part(cube_, place({0.0f, 0.65f, 0.0f}, {0.5f, 1.3f, 0.5f}), surface(linear(0.25f, 0.4f, 0.8f), 0.35f, 0.8f));
    part(sphere_, place({0.0f, 1.55f, 0.0f}, glm::vec3(0.42f)), surface(linear(0.85f, 0.7f, 0.55f), 0.6f));
    part(cube_, place({0.35f, 0.8f, 0.0f}, {0.06f, 1.0f, 0.12f}), surface(linear(0.8f, 0.8f, 0.85f), 0.2f, 1.0f));
    selected_ = hero_;
}

bool Demo3D::in_reach(entt::entity creature) const {
    constexpr float kReach = 2.0f;  // cells
    const glm::vec2 d = cell_position(creature) - cell_position(hero_);
    return glm::dot(d, d) <= kReach * kReach;
}

entt::entity Demo3D::creature_in_reach() const {
    const glm::vec2 hero = cell_position(hero_);
    const entt::entity nearest = creature_near({hero.x, 0.0f, hero.y}, 2.0f);
    return nearest != entt::null && in_reach(nearest) ? nearest : entt::null;
}

void Demo3D::strike(entt::entity creature) {
    entt::registry& registry = world_.registry();
    Health& health = registry.get<Health>(creature);
    health.value = std::max(health.value - 0.25f, 0.0f);
    if (health.value <= 0.0f) {
        Walker& walker = registry.get<Walker>(creature);  // down: it stops where it is
        walker.velocity = glm::vec2(0.0f);
        walker.goal.reset();
    }
    flashes_.push_back({0, creature, live_ticks_ + 45});
    ++hits_;
    if (!impacts_.empty()) {
        const glm::vec2 p = cell_position(creature);
        moteur::PlaySound impact;
        impact.position = glm::vec3(p.x, 1.0f, p.y);
        impact.pitch_variation = 0.08f;
        impact.volume_variation = 0.1f;
        app_.audio().play(impacts_, impact);
    }
}

void Demo3D::play_steps(glm::vec2 before) {
    constexpr float kStride = 0.85f;  // cells between two footsteps
    const glm::vec2 now = cell_position(hero_);
    step_distance_ += glm::length(now - before);
    if (step_distance_ < kStride) {
        return;
    }
    step_distance_ = std::fmod(step_distance_, kStride);
    if (!steps_.empty()) {
        moteur::PlaySound step;
        step.position = glm::vec3(now.x, 0.0f, now.y);
        step.volume = 0.6f;
        step.pitch_variation = 0.06f;
        step.volume_variation = 0.15f;
        app_.audio().play(steps_, step);
    }
}

void Demo3D::light_braziers() {
    // Every flame is drawn, but only the lights nearest to the camera are sent (the engine takes at
    // most 32; see render()), and among them the engine gives shadows to its budget.
    static constexpr glm::vec3 kFlames[3] = {{1.0f, 0.5f, 0.15f}, {1.0f, 0.65f, 0.25f}, {1.0f, 0.42f, 0.1f}};
    entt::registry& registry = world_.registry();
    moteur::BillboardOptions halo;
    halo.additive = true;
    for (const glm::vec3& position : braziers_) {
        const glm::vec3 color = kFlames[torches_ % 3];
        const entt::entity torch = registry.create();
        registry.emplace<moteur::Transform>(torch, place(position, glm::vec3(0.18f)));
        registry.emplace<moteur::MeshComponent>(torch, sphere_, glowing(color * 10.0f));
        registry.emplace<moteur::LightSource>(torch, color, 5.0f, 7.0f, torch_shadows_);
        halo.color = glm::vec4(color * 1.5f, 1.0f);
        registry.emplace<moteur::Billboard>(torch, glow_, glm::vec2(1.0f), halo);
        registry.emplace<moteur::Name>(torch, "torche " + std::to_string(torches_));
        ++torches_;
    }
}

glm::vec2 Demo3D::cell_position(entt::entity creature) const {
    const glm::vec3 p = world_.registry().get<moteur::Transform>(creature).position;
    return {p.x, p.z};
}

bool Demo3D::walkable(glm::vec2 p) const {
    return map_->walkable(tileset_, glm::ivec2(glm::floor(p)));
}

void Demo3D::move_creatures(float dt) {
    for (auto [entity, transform, walker] : world_.registry().view<moteur::Transform, Walker>().each()) {
        glm::vec2 position(transform.position.x, transform.position.z);
        if (walker.goal) {
            const glm::vec2 to_goal = *walker.goal - position;
            const float distance = glm::length(to_goal);
            if (distance <= kSentSpeed * dt) {
                position = *walker.goal;
                walker.goal.reset();
                walker.velocity = glm::vec2(0.0f);  // arrived: waits there
            } else if (const glm::vec2 next = position + to_goal / distance * (kSentSpeed * dt); walkable(next)) {
                position = next;
            } else {
                walker.goal.reset();  // a wall in the way, and no pathfinding yet: it stops
                walker.velocity = glm::vec2(0.0f);
            }
        } else if (const glm::vec2 next = position + walker.velocity * dt; walkable(next)) {
            position = next;
        } else {
            walker.velocity = -walker.velocity;  // turns back before a wall, as in the 2D demo
        }
        transform.position.x = position.x;
        transform.position.z = position.y;
    }
}

void Demo3D::show_selection() {
    if (selected_ == entt::null) {
        return;
    }
    entt::registry& registry = world_.registry();
    if (registry.get<moteur::Parent>(ring_).entity != selected_) {
        registry.patch<moteur::Parent>(ring_, [this](moteur::Parent& parent) { parent.entity = selected_; });
    }
    if (const std::optional<glm::vec2>& goal = registry.get<Walker>(selected_).goal) {
        // The mark is a still entity: moved through replace(), so that its cached place is dropped.
        const moteur::Transform mark = place({goal->x, 0.012f, goal->y}, glm::vec3(0.4f));
        if (registry.get<moteur::Transform>(goal_) != mark) {
            registry.replace<moteur::Transform>(goal_, mark);
        }
        registry.remove<moteur::Hidden>(goal_);
    } else {
        registry.emplace_or_replace<moteur::Hidden>(goal_);
    }
}

void Demo3D::screen_axes(glm::vec3& ahead, glm::vec3& right) const {
    const glm::vec3 forward = camera_.forward();
    ahead = glm::normalize(glm::vec3(forward.x, 0.0f, forward.z));
    right = glm::cross(ahead, glm::vec3(0.0f, 1.0f, 0.0f));
}

// `direction`: x to the right of the screen, y up the screen, length at most 1.
void Demo3D::move_camera(glm::vec2 direction, float dt) {
    if (direction == glm::vec2(0.0f)) {
        return;
    }
    glm::vec3 ahead, right;
    screen_axes(ahead, right);
    const float speed = camera_.visible_height() * 1.2f;  // the same speed on screen at any zoom
    glm::vec3 target = camera_.target() + (right * direction.x + ahead * direction.y) * (speed * dt);
    const auto n = static_cast<float>(options_.map_size);
    target.x = std::clamp(target.x, 0.0f, n);
    target.z = std::clamp(target.z, 0.0f, n);
    camera_.set_target(target);
}

entt::entity Demo3D::creature_near(glm::vec3 ground, float radius) const {
    entt::entity best = entt::null;
    float best_distance = radius * radius;
    // In the order of creation (reach()), so that a tie always goes to the same one.
    for (auto [entity, walker] : world_.registry().storage<Walker>()->reach()) {
        if (entity == hero_) {
            continue;  // the hero is no creature
        }
        const glm::vec2 d = cell_position(entity) - glm::vec2(ground.x, ground.z);
        const float distance = glm::dot(d, d);
        if (distance <= best_distance) {
            best_distance = distance;
            best = entity;
        }
    }
    return best;
}

entt::entity Demo3D::next_creature(entt::entity creature) const {
    // Walkers are never removed, so their storage keeps the order of creation.
    const auto& walkers = *world_.registry().storage<Walker>();
    return walkers[(walkers.index(creature) + 1) % walkers.size()];
}

// Escape stays a raw key: it leaves the scene whatever the bindings.
void Demo3D::on_event(const SDL_Event& event) {
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
        if (standalone_) {
            app_.quit();
        } else {
            stop_requested_ = true;
        }
    }
}

void Demo3D::apply_input(float dt) {
    const moteur::Input& input = app_.input();
    // Zoom: every notch of the wheel counts, even several in one tick.
    const int zoom = input.presses(actions_.zoom_out) - input.presses(actions_.zoom_in);
    if (zoom != 0) {
        camera_.set_visible_height(std::clamp(camera_.visible_height() * std::pow(1.0f / 0.9f, static_cast<float>(zoom)), 4.0f, 60.0f));
    }
    if (input.pressed(actions_.projection)) {
        camera_.set_projection(camera_.projection() == moteur::Projection::Perspective ? moteur::Projection::Orthographic
                                                                                       : moteur::Projection::Perspective);
    }
    if (input.pressed(actions_.follow)) {
        follow_ = !follow_;
    }
    if (selected_ == entt::null) {
        return;
    }
    if (hero_ == entt::null && input.pressed(actions_.next)) {
        selected_ = next_creature(selected_);
    }
    // The pointed ground, with the camera of this tick (not the interpolated one of the last frame
    // drawn): a replay gives the same result whatever the frame rate.
    const glm::vec2 pointer = options_.fake_mouse ? options_.fake_mouse_position : input.pointer();
    std::optional<glm::vec3> ground;
    if (pointer.x >= 0.0f && pointer.y >= 0.0f) {
        ground = camera_.ground_point(pointer);
    }
    if (hero_ == entt::null && ground && input.pressed(actions_.select)) {
        // Picking a creature: the nearest to the pointed ground (see the part 11 notes).
        if (const entt::entity picked = creature_near(*ground, 1.5f); picked != entt::null) {
            selected_ = picked;
        }
    }
    Walker& hero = world_.registry().get<Walker>(selected_);
    // The slice: a click on a creature strikes it when it is within reach, or walks up to it.
    if (hero_ != entt::null && ground && input.pressed(actions_.move_to)) {
        if (const entt::entity target = creature_near(*ground, 0.8f); target != entt::null) {
            pressed_on_creature_ = true;
            if (in_reach(target)) {
                strike(target);
            } else {
                hero.goal = cell_position(target);
            }
        }
    }
    if (!input.down(actions_.move_to)) {
        pressed_on_creature_ = false;
    }
    // Move by click: held, the creature keeps heading for the pointer.
    if (ground && input.down(actions_.move_to) && !pressed_on_creature_ && walkable({ground->x, ground->z})) {
        hero.goal = glm::vec2(ground->x, ground->z);
    }
    // Move by keys or stick, relative to the screen: a goal one step ahead.
    if (const glm::vec2 direction = input.axis(actions_.move); direction != glm::vec2(0.0f)) {
        glm::vec3 ahead, right;
        screen_axes(ahead, right);
        const glm::vec3 step = (right * direction.x + ahead * direction.y) * (kSentSpeed * dt);
        const glm::vec2 next = cell_position(selected_) + glm::vec2(step.x, step.z);
        hero.goal = walkable(next) ? std::optional<glm::vec2>(next) : std::nullopt;
        hero.velocity = glm::vec2(0.0f);
    }
    for (int i = 0; i < kSkills; ++i) {
        if (input.pressed(actions_.skills[i])) {
            ++skill_uses_[i];
            flashes_.push_back({i + 1, selected_, live_ticks_ + 45});
            // The slice: the first skill strikes the nearest creature within reach.
            if (i == 0 && hero_ != entt::null) {
                if (const entt::entity target = creature_in_reach(); target != entt::null) {
                    strike(target);
                }
            }
        }
    }
    std::erase_if(flashes_, [this](const SkillFlash& flash) { return flash.until_tick < live_ticks_; });
    if (!follow_) {
        move_camera(input.axis(actions_.camera), dt);
    }
}

void Demo3D::update(double dt) {
    elapsed_ += dt;
    ++ticks_;
    if (options_.run_seconds > 0.0 && elapsed_ >= options_.run_seconds) {
        if (standalone_) {
            app_.quit();
        } else {
            stop_requested_ = true;
        }
    }
    last_stats_ = app_.renderer().stats();
    stats_frozen_ = frozen_frames_ > 0;  // those of a frame drawn frozen (see render)
    // The systems of a tick, in their order.
    camera_.begin_update();
    world_.begin_tick();
    const bool frozen = options_.freeze_after_ticks > 0 && ticks_ > options_.freeze_after_ticks;
    if (frozen) {
        return;
    }
    ++live_ticks_;
    live_pointer_ = app_.input().pointer();
    const auto step = static_cast<float>(dt);
    const glm::vec2 hero_before = hero_ != entt::null ? cell_position(hero_) : glm::vec2(0.0f);
    apply_input(step);
    if (moteur::DebugTools* tools = app_.debug_tools(); tools != nullptr && selected_ != reported_selection_) {
        tools->select(selected_);  // a creature picked in the world shows in the inspector
        reported_selection_ = selected_;
    }
    move_creatures(step);
    if (hero_ != entt::null) {
        play_steps(hero_before);
    }
    show_selection();
    if (follow_ && selected_ != entt::null) {
        const glm::vec2 p = cell_position(selected_);
        camera_.follow({p.x, 0.0f, p.y}, step, 0.25f);
    }
}

void Demo3D::render(moteur::Renderer& renderer, double alpha) {
    ++frames_;
    const bool frozen = options_.freeze_after_ticks > 0 && ticks_ > options_.freeze_after_ticks;
    frozen_frames_ += frozen ? 1 : 0;
    // The statistics line shows the previous frame's: wait until it is a frozen one too, or the
    // capture would depend on whether the last frame before the freeze came just before it.
    if (!options_.capture_path.empty() && !capture_done_ && frozen && stats_frozen_) {
        renderer.request_capture(options_.capture_path);
        capture_done_ = true;
    }
    renderer.set_clear_color(0.05f, 0.06f, 0.08f);
    camera_.set_viewport({static_cast<float>(renderer.width()), static_cast<float>(renderer.height())});
    const moteur::Camera3D camera = camera_.interpolated(alpha);
    drawn_camera_ = camera;
    const auto blend = static_cast<float>(alpha);

    moteur::MeshRenderer& meshes = renderer.meshes();
    moteur::BillboardRenderer& billboards = renderer.billboards();
    meshes.set_camera(camera.view_projection(), camera.position());
    billboards.set_camera(camera);
    const float yaw = glm::radians(sun_yaw_), elevation = glm::radians(sun_elevation_);
    meshes.set_sun({std::cos(elevation) * std::cos(yaw), std::sin(elevation), std::cos(elevation) * std::sin(yaw)},
                   linear(1.0f, 0.93f, 0.8f), sun_intensity_);
    meshes.set_environment(&*sky_, 1.0f);

    // The world: decor, creatures, torches (the lights nearest to the middle of the view).
    const Uint64 collect_start = SDL_GetPerformanceCounter();
    moteur::CollectOptions collect;
    collect.light_focus = camera.target();
    collect.max_lights = std::max(torch_lights_, 0);
    world_.submit(renderer, blend, collect);
    collect_seconds_ += static_cast<double>(SDL_GetPerformanceCounter() - collect_start) / static_cast<double>(SDL_GetPerformanceFrequency());

    // The cell under the mouse. Frozen, the pointer of the last live tick: in a replay, the pointer
    // of the frame depends on how many ticks ran before it, and so would the capture.
    if (!options_.fake_mouse) {
        mouse_ = frozen ? live_pointer_ : app_.input().pointer();
    }
    hovered_cell_.reset();
    if (mouse_.x >= 0.0f && mouse_.y >= 0.0f) {
        if (const auto ground = camera.ground_point(mouse_)) {
            const glm::ivec2 cell(static_cast<int>(std::floor(ground->x)), static_cast<int>(std::floor(ground->z)));
            if (cell.x >= 0 && cell.y >= 0 && cell.x < options_.map_size && cell.y < options_.map_size) {
                hovered_cell_ = cell;
                meshes.draw(*tile_, place({static_cast<float>(cell.x) + 0.5f, 0.005f, static_cast<float>(cell.y) + 0.5f}, glm::vec3(1.0f)).matrix(),
                            glowing({0.9f, 0.75f, 0.2f}));
            }
        }
    }

    draw_overlay(renderer, camera, blend);
}

void Demo3D::draw_overlay(moteur::Renderer& renderer, const moteur::Camera3D& camera, float blend) {
    moteur::SpriteRenderer& screen = renderer.screen_sprites();
    const float ui = app_.to_pixels({1.0f, 0.0f}).x - app_.to_pixels({0.0f, 0.0f}).x;
    const glm::vec2 view(static_cast<float>(renderer.width()), static_cast<float>(renderer.height()));

    // Health bars over the creatures on screen: constant size, one depth per kind of element.
    if (health_bars_) {
        const glm::vec2 bar(48.0f * ui, 5.0f * ui);
        // In the order of creation: bars that overlap always cover each other the same way.
        for (auto [entity, health] : world_.registry().storage<Health>().reach()) {
            const glm::vec3 p = world_.world_position(entity, blend);
            const auto head = camera.world_to_screen({p.x, 1.6f, p.z});
            if (!head || head->x < -bar.x || head->y < -bar.y || head->x > view.x + bar.x || head->y > view.y + bar.y) {
                continue;
            }
            const glm::vec2 corner = glm::round(*head - glm::vec2(bar.x * 0.5f, bar.y));
            moteur::SpriteOptions frame;
            frame.tint = {0.0f, 0.0f, 0.0f, 0.7f};
            frame.depth = 1.0f;
            screen.draw(white_, corner - glm::vec2(ui), bar + glm::vec2(2.0f * ui), frame);
            moteur::SpriteOptions fill;
            fill.tint = glm::vec4(glm::mix(glm::vec3(0.85f, 0.15f, 0.1f), glm::vec3(0.2f, 0.8f, 0.25f), health.value), 1.0f);
            if (entity == selected_) {
                fill.tint = {0.95f, 0.85f, 0.3f, 1.0f};  // the selected one stands out
            }
            fill.depth = 1.1f;
            screen.draw(white_, corner, {std::round(bar.x * health.value), bar.y}, fill);
        }
    }

    // Skills just used: their number over the creature, rising and fading; the newest lowest, the
    // older ones stacked above it.
    for (std::size_t f = 0; f < flashes_.size(); ++f) {
        const SkillFlash& flash = flashes_[f];
        const auto newer = std::count_if(flashes_.begin() + static_cast<std::ptrdiff_t>(f) + 1, flashes_.end(),
                                         [&flash](const SkillFlash& other) { return other.creature == flash.creature; });
        if (!world_.registry().valid(flash.creature)) {
            continue;
        }
        const glm::vec3 p = world_.world_position(flash.creature, blend);
        const auto head = camera.world_to_screen({p.x, 1.9f, p.z});
        if (!head) {
            continue;
        }
        const float left = static_cast<float>(flash.until_tick - live_ticks_) / 45.0f;  // 1 when used, 0 at the end
        moteur::TextOptions style;
        style.align = moteur::TextAlign::Center;
        style.max_width = 200.0f;
        style.depth = 1.5f;
        style.color = {1.0f, 0.85f, 0.3f, std::clamp(left * 1.5f, 0.0f, 1.0f)};
        const std::string label = flash.skill == 0 ? std::string("Touché !") : "Compétence " + std::to_string(flash.skill);
        const float rise = (1.0f - left) * 30.0f * ui + static_cast<float>(newer) * font_->line_height();
        font_->draw(screen, label, *head - glm::vec2(100.0f, font_->line_height() + rise), style);
    }

    // Statistics and help, top left.
    const float line = font_->line_height();
    const glm::vec2 corner(20.0f, 40.0f);
    moteur::TextOptions title;
    title.depth = 2.0f;
    moteur::TextOptions detail;
    detail.depth = 2.0f;
    detail.color = {0.8f, 0.85f, 0.9f, 1.0f};
    char text[256];
    std::snprintf(text, sizeof(text), "Démo 3D : carte %dx%d, %zu créatures, %zu objets fixes, %zu torches",
                  options_.map_size, options_.map_size, creatures_, static_draws_, torches_);
    font_->draw(screen, text, corner, title);
    const moteur::RenderStats& s = last_stats_;
    std::snprintf(text, sizeof(text), "scène : %d / %d maillages, %zu triangles, %d draw calls ; ombre : %d / %d, %d draw calls",
                  s.meshes, s.meshes_submitted, s.triangles, s.mesh_draw_calls, s.shadow_casters, s.shadow_casters_submitted,
                  s.shadow_draw_calls);
    font_->draw(screen, text, corner + glm::vec2(0.0f, line), detail);
    std::snprintf(text, sizeof(text), "torches : %d ombrées, %d recalculées, %d draw calls ; billboards : %d ; draw calls en tout : %d",
                  s.point_shadow_lights, s.point_shadow_updates, s.point_shadow_draw_calls, s.billboards, s.draw_calls);
    font_->draw(screen, text, corner + glm::vec2(0.0f, 2.0f * line), detail);
    // The controls of the current profile, named as on the player's keyboard.
    // Only the device in use: the keys, or the gamepad's buttons.
    const moteur::Input& input = app_.input();
    const moteur::InputDevice device = input.last_device();
    const bool click_to_move = !input.sources(actions_.move_to).empty() && device == moteur::InputDevice::KeyboardMouse;
    std::string help = input.profile_label(input.profile()) + " : se déplacer " +
                       input.describe(click_to_move ? actions_.move_to : actions_.move, device) + ", compétences";
    for (int i = 0; i < kSkills; ++i) {
        if (!input.sources(actions_.skills[i]).empty()) {
            help += (i == 0 ? " " : ", ") + input.describe(actions_.skills[i], device);
        }
    }
    font_->draw(screen, help, corner + glm::vec2(0.0f, 3.0f * line), detail);
    if (hero_ != entt::null) {
        std::snprintf(text, sizeof(text), "%s sur une créature proche, ou %s : la frapper (coups : %d). %s : caméra suivie (%s), %s : pause",
                      input.describe(actions_.move_to, device).c_str(), input.describe(actions_.skills[0], device).c_str(), hits_,
                      input.describe(actions_.follow, device).c_str(), follow_ ? "oui" : "non",
                      input.describe(actions_.pause, device).c_str());
    } else {
        std::snprintf(text, sizeof(text), "%s%s : choisir une créature, %s : la suivante, %s : la suivre (%s)",
                      hovered_cell_ ? ("Case (" + std::to_string(hovered_cell_->x) + ", " + std::to_string(hovered_cell_->y) + "). ").c_str() : "",
                      input.describe(actions_.select, device).c_str(), input.describe(actions_.next, device).c_str(),
                      input.describe(actions_.follow, device).c_str(), follow_ ? "oui" : "non");
    }
    font_->draw(screen, text, corner + glm::vec2(0.0f, 4.0f * line), detail);
}

void Demo3D::draw_controls() {
    moteur::Renderer& renderer = app_.renderer();
    moteur::MeshRenderer& meshes = renderer.meshes();
    ImGui::PushItemWidth(200.0f);
    ImGui::SeparatorText("Commandes");
    moteur::Input& input = app_.input();
    if (ImGui::BeginCombo("Profil", input.profile_label(input.profile()).c_str())) {
        for (const std::string& profile : input.profiles()) {
            if (ImGui::Selectable(input.profile_label(profile).c_str(), profile == input.profile())) {
                input.set_profile(profile);
                // Kept for the next time, in the player's file.
                const std::string path = user_bindings_path();
                const std::string text = input.user_bindings_json();
                if (!path.empty() && !SDL_SaveFile(path.c_str(), text.data(), text.size())) {
                    SDL_Log("Demo 3D: cannot write '%s': %s", path.c_str(), SDL_GetError());
                }
            }
        }
        ImGui::EndCombo();
    }
    ImGui::Text("Dernier périphérique : %s ; manettes : %d",
                input.last_device() == moteur::InputDevice::Gamepad ? "manette" : "clavier et souris", input.gamepad_count());
    ImGui::TextDisabled("Actions et touches : DEBUG > Entrées");
    ImGui::Text("Compétences utilisées : %d %d %d %d %d %d", skill_uses_[0], skill_uses_[1], skill_uses_[2],
                skill_uses_[3], skill_uses_[4], skill_uses_[5]);
    ImGui::SeparatorText("Caméra");
    ImGui::Checkbox("Suivre la créature choisie", &follow_);
    if (selected_ != entt::null) {
        ImGui::Text("Créature choisie : %s", world_.registry().get<moteur::Name>(selected_).value.c_str());
    }
    ImGui::SeparatorText("Lumière");
    ImGui::SliderFloat("Soleil", &sun_intensity_, 0.0f, 10.0f, "%.1f");
    ImGui::SliderFloat("Hauteur du soleil (°)", &sun_elevation_, 5.0f, 90.0f, "%.0f");
    ImGui::SliderFloat("Direction du soleil (°)", &sun_yaw_, 0.0f, 360.0f, "%.0f");
    moteur::ShadowOptions shadows = meshes.shadows();
    bool changed = ImGui::Checkbox("Ombres du soleil", &shadows.enabled);
    ImGui::SliderInt("Torches éclairantes", &torch_lights_, 0, moteur::MeshRenderer::kMaxPointLights);
    if (ImGui::Checkbox("Ombres des torches", &torch_shadows_)) {
        for (auto [entity, light] : world_.registry().view<moteur::LightSource>().each()) {
            light.casts_shadows = torch_shadows_;
        }
    }
    changed |= ImGui::SliderInt("Torches ombrées (budget)", &shadows.point_budget, 0, moteur::MeshRenderer::kMaxShadowedPointLights);
    if (changed) {
        meshes.set_shadows(shadows);
    }
    ImGui::SeparatorText("Rendu");
    float scale = renderer.render_scale();
    if (ImGui::SliderFloat("Résolution de rendu", &scale, 0.25f, 1.0f, "%.2f")) {
        renderer.set_render_scale(scale);
    }
    anti_aliasing_combo(renderer);
    bool culling = meshes.culling();
    if (ImGui::Checkbox("Frustum culling", &culling)) {
        meshes.set_culling(culling);
    }
    ImGui::Checkbox("Barres de vie", &health_bars_);
    ImGui::PopItemWidth();
    const moteur::RenderStats& s = last_stats_;
    if (ImGui::BeginTable("passes", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Passe");
        ImGui::TableSetupColumn("Soumis");
        ImGui::TableSetupColumn("Dessinés");
        ImGui::TableSetupColumn("Triangles");
        ImGui::TableSetupColumn("Draw calls");
        ImGui::TableHeadersRow();
        const auto row = [](const char* name, int submitted, int drawn, std::size_t triangles, int calls) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(name);
            ImGui::TableNextColumn();
            ImGui::Text("%d", submitted);
            ImGui::TableNextColumn();
            ImGui::Text("%d", drawn);
            ImGui::TableNextColumn();
            ImGui::Text("%zu", triangles);
            ImGui::TableNextColumn();
            ImGui::Text("%d", calls);
        };
        row("Scène", s.meshes_submitted, s.meshes, s.triangles, s.mesh_draw_calls);
        row("Ombre", s.shadow_casters_submitted, s.shadow_casters, s.shadow_triangles, s.shadow_draw_calls);
        row("Torches", s.point_shadow_lights, s.point_shadow_casters, s.point_shadow_triangles, s.point_shadow_draw_calls);
        row("Billboards", s.billboards, s.billboards, static_cast<std::size_t>(s.billboards) * 2, s.billboard_draw_calls);
        ImGui::EndTable();
    }
    ImGui::Text("Draw calls en tout : %d (sprites et interface compris)", s.draw_calls);
    ImGui::Text("Monde : %zu entités, dont %zu fixes gardées ; collecte : %.3f ms par image (moyenne)", world_.entity_count(),
                world_.cached(), collect_ms());
}
