#include "world_test.hpp"

#include <glm/gtc/constants.hpp>
#include <imgui.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

#include "moteur/billboard_renderer.hpp"
#include "moteur/color.hpp"
#include "moteur/debug_lines.hpp"
#include "moteur/debug_tools.hpp"
#include "moteur/mesh_renderer.hpp"
#include "moteur/paths.hpp"

namespace {

constexpr int kBlock = 16;              // the floor is merged in blocks of 16 x 16 cells, one mesh per tone
constexpr float kHeroRadius = 0.35f;
constexpr float kMonsterRadius = 0.35f;
constexpr float kBigRadius = 0.7f;
constexpr int kSightRadius = 14;        // cells: the hero's field of view
constexpr float kMonsterSight = 12.0f;  // metres: how far a monster sees the hero
constexpr int kPathsPerTick = 8;
constexpr int kFieldCost = 400;         // tenths of a cell: the chase field reaches 40 cells
const char* const kEffectsDirectory = "effects";

glm::vec3 linear(float r, float g, float b) {
    return moteur::srgb_to_linear(glm::vec3(r, g, b));
}

moteur::Material surface(glm::vec3 color, float roughness, bool casts_shadow = true) {
    moteur::Material material;
    material.base_color = glm::vec4(color, 1.0f);
    material.roughness = roughness;
    material.casts_shadow = casts_shadow;
    return material;
}

moteur::Material glowing(glm::vec3 emissive) {
    moteur::Material material;
    material.base_color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    material.emissive = emissive;
    material.casts_shadow = false;
    return material;
}

moteur::Transform place(glm::vec3 position, glm::vec3 scale) {
    moteur::Transform transform;
    transform.position = position;
    transform.scale = scale;
    return transform;
}

// A color of its own for each named point (the same at every run).
glm::vec3 point_color(const std::string& name) {
    std::uint32_t hash = 2166136261u;
    for (const char c : name) {
        hash = (hash ^ static_cast<unsigned char>(c)) * 16777619u;
    }
    const float hue = static_cast<float>(hash % 360u) / 60.0f;
    const float x = 1.0f - std::abs(std::fmod(hue, 2.0f) - 1.0f);
    const int sector = static_cast<int>(hue);
    const glm::vec3 colors[6] = {{1, x, 0}, {x, 1, 0}, {0, 1, x}, {0, x, 1}, {x, 0, 1}, {1, 0, x}};
    return colors[sector % 6];
}

std::filesystem::path utf8_path(const std::string& text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

std::string path_utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

std::vector<std::string> json_files(const std::string& directory) {
    std::vector<std::string> names;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(utf8_path(directory), error)) {
        if (entry.path().extension() == ".json") {
            names.push_back(path_utf8(entry.path().stem()));
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

double seconds_since(Uint64 start) {
    return static_cast<double>(SDL_GetPerformanceCounter() - start) / static_cast<double>(SDL_GetPerformanceFrequency());
}

// A square on the ground around a cell, `inset` metres inside its edges.
void cell_square(moteur::DebugLineBuffer& lines, glm::ivec2 cell, float inset, glm::vec4 color, float height = 0.03f) {
    const float x0 = static_cast<float>(cell.x) + inset, x1 = static_cast<float>(cell.x + 1) - inset;
    const float z0 = static_cast<float>(cell.y) + inset, z1 = static_cast<float>(cell.y + 1) - inset;
    lines.line({x0, height, z0}, {x1, height, z0}, color);
    lines.line({x1, height, z0}, {x1, height, z1}, color);
    lines.line({x1, height, z1}, {x0, height, z1}, color);
    lines.line({x0, height, z1}, {x0, height, z0}, color);
}

}  // namespace

std::vector<std::string> WorldTest::map_names(const moteur::Assets& assets) {
    return json_files(assets.root() + "maps");
}

WorldTest::WorldTest(moteur::Application& app, const Options& options, bool standalone)
    : app_(app), options_(options), standalone_(standalone), particles_(20000, 99) {
    moteur::Renderer& renderer = app.renderer();
    cube_ = moteur::make_asset(moteur::Mesh::create(renderer, moteur::make_cube(), "world.cube"));
    tile_ = moteur::make_asset(moteur::Mesh::create(renderer, moteur::make_plane(), "world.tile"));
    sky_ = moteur::Environment::create(renderer, moteur::make_sky(256, 128), "world.sky");

    moteur::Input& input = app.input();
    input.clear_actions();
    input.add_button("move_to");
    input.add_axis("move");
    input.add_axis("camera");
    for (const char* name : {"zoom_in", "zoom_out", "toggle_wall", "spawn", "spawn_big", "effect", "follow", "projection"}) {
        input.add_button(name);
    }
    input.load_bindings(moteur::read_text_file(app.assets().file_path("input/world.json")), "input/world.json");
    actions_ = {input.find("move_to"),     input.find("move"),  input.find("camera"),    input.find("zoom_in"),
                input.find("zoom_out"),    input.find("toggle_wall"), input.find("spawn"), input.find("spawn_big"),
                input.find("effect"),      input.find("follow"), input.find("projection")};

    moteur::ShadowOptions shadows;
    renderer.meshes().set_shadows(shadows);
    renderer.meshes().set_culling(true);
    renderer.set_render_scale(1.0f);
    renderer.set_exposure(1.0f);

    effect_names_ = json_files(app.assets().root() + kEffectsDirectory);
    if (app.assets().exists("effects/sparks.json")) {
        sparks_ = app.assets().particle_effect("effects/sparks.json");
    }
    effect_choice_ = static_cast<int>(std::find(effect_names_.begin(), effect_names_.end(), "sparks") - effect_names_.begin());
    if (effect_choice_ >= static_cast<int>(effect_names_.size())) {
        effect_choice_ = 0;
    }

    names_ = map_names(app.assets());
    if (names_.empty()) {
        throw std::runtime_error("Monde : aucune carte dans assets/maps");
    }
    const auto found = std::find(names_.begin(), names_.end(), options_.map);
    if (found == names_.end()) {
        throw std::runtime_error("Monde : pas de carte « " + options_.map + " » dans assets/maps");
    }
    load_map(*found);
    if (!error_.empty()) {
        throw std::runtime_error(error_);
    }
    if (moteur::DebugTools* tools = app.debug_tools()) {
        tools->watch(world_, "Monde");
    }
}

WorldTest::~WorldTest() {
    moteur::MeshRenderer& meshes = app_.renderer().meshes();
    meshes.set_fog({});
    meshes.set_cutout({});
    if (options_.report && timings_.ticks > 0) {
        const auto n = static_cast<double>(timings_.ticks);
        SDL_Log("perf: world  %d monsters, %d colliders", static_cast<int>(world_.registry().view<Monster>().size()),
                collision_stats_.colliders);
        SDL_Log("perf: world  ai %.3f, paths %.3f, fields %.3f, moves %.3f, collisions %.3f, sight %.3f ms per tick (%ld ticks)",
                timings_.ai / n, timings_.paths / n, timings_.fields / n, timings_.moves / n, timings_.collisions / n,
                timings_.sight / n, timings_.ticks);
        SDL_Log("perf: world  total %.3f ms per tick",
                (timings_.ai + timings_.paths + timings_.fields + timings_.moves + timings_.collisions + timings_.sight) / n);
        if (timings_.frames > 0) {
            SDL_Log("perf: world  particles %.3f ms per frame, %d alive", timings_.particles / static_cast<double>(timings_.frames),
                    particles_.stats().particles);
        }
    }
    if (moteur::DebugTools* tools = app_.debug_tools()) {
        tools->forget(world_);
    }
}

void WorldTest::load_map(const std::string& name) {
    // A broken file: the error is shown in the panel and the previous map stays.
    try {
        map_ = app_.assets().map("maps/" + name + ".json");
    } catch (const std::exception& e) {
        error_ = e.what();
        return;
    }
    error_.clear();
    chosen_ = static_cast<int>(std::find(names_.begin(), names_.end(), name) - names_.begin());
    reset_tiles();
    camera_.set_visible_height(16.0f);
    const glm::vec2 hero = hero_position();
    camera_.set_target({hero.x, 0.0f, hero.y});
    camera_.begin_update();
}

glm::vec2 WorldTest::hero_position() const {
    return hero_ != entt::null ? moteur::plane_position(world_.registry(), hero_) : glm::vec2(0.0f);
}

void WorldTest::reset_tiles() {
    tiles_ = map_->tiles();
    grid_ = moteur::NavGrid(tiles_, map_->tileset());
    built_revision_ = map_->revision();
    toggled_ = 0;
    world_.registry().clear();
    hero_ = entt::null;
    particles_.clear();
    fires_.clear();
    trail_ = moteur::kNoEffect;
    explored_ = moteur::ExploredMap(grid_.width(), grid_.height());
    fog_cells_.assign(static_cast<std::size_t>(grid_.width()) * static_cast<std::size_t>(grid_.height()), 0);
    app_.renderer().meshes().set_fog_cells(grid_.width(), grid_.height(), fog_cells_);
    view_cell_ = glm::ivec2(-1);
    field_cell_ = glm::ivec2(-1);
    grid_changed();
    build_decor();

    const moteur::MapData& map = *map_;
    const std::vector<glm::ivec2>& start = map.points("depart");
    spawn_hero(start.empty() ? glm::vec2(map.width(), map.height()) * 0.5f : moteur::cell_centre(start.front()));
    start_fires();
    if (options_.autopilot) {
        for (const glm::ivec2 cell : map.points("monstres")) {
            spawn_group(moteur::cell_centre(cell), 3, false);
        }
        const std::vector<glm::ivec2>& goal = map.points("but");
        if (!goal.empty()) {
            world_.registry().get<moteur::Mover>(hero_).go_to(moteur::cell_centre(goal.front()));
        }
    }
    if (options_.crowd > 0) {
        // Anywhere a monster fits, at random (the same every run).
        for (int i = 0, tries = 0; i < options_.crowd && tries < options_.crowd * 50; ++tries) {
            const glm::vec2 p(random_.next() * static_cast<float>(grid_.width()), random_.next() * static_cast<float>(grid_.height()));
            if (!moteur::circle_fits(grid_, p, kMonsterRadius) || glm::length(p - hero_position()) < 4.0f) {
                continue;
            }
            const entt::entity monster = spawn_monster(p, false);
            Monster& m = world_.registry().get<Monster>(monster);
            m.always_chase = true;
            m.chasing = true;
            ++i;
        }
    }
    if (options_.fires > 0 && app_.assets().exists("effects/fire.json")) {
        const moteur::Asset<moteur::ParticleEffect> fire = app_.assets().particle_effect("effects/fire.json");
        const glm::vec2 hero = hero_position();
        for (int i = 0; i < options_.fires; ++i) {
            const glm::vec2 p = hero + glm::vec2(random_.next() * 2.0f - 1.0f, random_.next() * 2.0f - 1.0f) * 8.0f;
            fires_.push_back(particles_.play(fire, {p.x, 0.05f, p.y}));
        }
    }
    update_sight();
}

void WorldTest::grid_changed() {
    clearance_ = moteur::ClearanceMap(grid_);
}

void WorldTest::add_static(const moteur::Asset<moteur::Mesh>& mesh, const moteur::Transform& transform,
                           const moteur::Material& material) {
    entt::registry& registry = world_.registry();
    const entt::entity entity = registry.create();
    registry.emplace<moteur::Transform>(entity, transform);
    registry.emplace<moteur::MeshComponent>(entity, mesh, material);
}

void WorldTest::build_decor() {
    // The decor only: everything without a Mover goes (the characters stay).
    entt::registry& registry = world_.registry();
    std::vector<entt::entity> decor;
    for (const entt::entity entity : registry.view<moteur::MeshComponent>(entt::exclude<moteur::Mover>)) {
        decor.push_back(entity);
    }
    registry.destroy(decor.begin(), decor.end());

    const moteur::MapData& map = *map_;
    const int width = grid_.width();
    const int height = grid_.height();
    const moteur::TileId pillar = map.tile_id("pilier");

    // Floor: every cell holding a tile, in two tones; holes stay empty, over a dark bottom.
    const moteur::Material tones[2] = {surface(linear(0.55f, 0.56f, 0.5f), 0.85f, false),
                                       surface(linear(0.45f, 0.46f, 0.41f), 0.85f, false)};
    const moteur::MeshData quad = moteur::make_plane();
    for (int bj = 0; bj < height; bj += kBlock) {
        for (int bi = 0; bi < width; bi += kBlock) {
            for (int tone = 0; tone < 2; ++tone) {
                moteur::MeshData block;
                for (int j = bj; j < std::min(bj + kBlock, height); ++j) {
                    for (int i = bi; i < std::min(bi + kBlock, width); ++i) {
                        bool any = false;
                        for (int layer = 0; layer < tiles_.layer_count(); ++layer) {
                            any = any || tiles_.at(layer, {i, j}) != moteur::kNoTile;
                        }
                        if (!any || ((i + j) & 1) != tone) {
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
                if (!block.indices.empty()) {
                    add_static(moteur::make_asset(moteur::Mesh::create(app_.renderer(), block, "world.floor block")),
                               moteur::Transform{}, tones[tone]);
                }
            }
        }
    }
    add_static(tile_,
               place({static_cast<float>(width) * 0.5f, -3.0f, static_cast<float>(height) * 0.5f},
                     {static_cast<float>(width) + 40.0f, 1.0f, static_cast<float>(height) + 40.0f}),
               surface(linear(0.04f, 0.04f, 0.05f), 1.0f, false));

    // What stands on the cells, by what the grid says of them (the pillars by their tile's name).
    // Walls, pillars and tall grass fade where they would hide the hero.
    moteur::Material stone = surface(linear(0.62f, 0.58f, 0.52f), 0.8f);
    moteur::Material dark_stone = surface(linear(0.42f, 0.4f, 0.38f), 0.7f);
    const moteur::Material wood = surface(linear(0.45f, 0.3f, 0.18f), 0.75f);
    moteur::Material grass = surface(linear(0.25f, 0.45f, 0.18f), 0.9f);
    stone.fades = dark_stone.fades = grass.fades = true;
    for (int j = 0; j < height; ++j) {
        for (int i = 0; i < width; ++i) {
            const glm::vec3 foot(static_cast<float>(i) + 0.5f, 0.0f, static_cast<float>(j) + 0.5f);
            bool is_pillar = false;
            bool any = false;
            for (int layer = 0; layer < tiles_.layer_count(); ++layer) {
                const moteur::TileId id = tiles_.at(layer, {i, j});
                is_pillar = is_pillar || (pillar != moteur::kNoTile && id == pillar);
                any = any || id != moteur::kNoTile;
            }
            const bool walkable = grid_.walkable({i, j});
            const bool opaque = grid_.opaque({i, j});
            if (!walkable && opaque && is_pillar) {
                add_static(cube_, place(foot + glm::vec3(0.0f, 1.25f, 0.0f), {0.6f, 2.5f, 0.6f}), dark_stone);
            } else if (!walkable && opaque) {
                add_static(cube_, place(foot + glm::vec3(0.0f, 0.75f, 0.0f), {1.0f, 1.5f, 1.0f}), stone);
            } else if (!walkable && any) {
                add_static(cube_, place(foot + glm::vec3(0.0f, 0.3f, 0.0f), {0.9f, 0.6f, 0.25f}), wood);
            } else if (walkable && opaque) {
                add_static(cube_, place(foot + glm::vec3(0.0f, 0.45f, 0.0f), {0.8f, 0.9f, 0.8f}), grass);
            }
        }
    }

    // Named points: a glowing square each.
    for (const auto& [name, cells] : map.points()) {
        for (const glm::ivec2 cell : cells) {
            const glm::vec2 c = moteur::cell_centre(cell);
            add_static(tile_, place({c.x, 0.01f, c.y}, glm::vec3(0.6f)), glowing(point_color(name) * 1.5f));
        }
    }
}

void WorldTest::spawn_hero(glm::vec2 at) {
    entt::registry& registry = world_.registry();
    hero_ = registry.create();
    const glm::vec2 p = moteur::nearest_fit(grid_, at, kHeroRadius).value_or(at);
    const moteur::Transform transform = place({p.x, 0.85f, p.y}, {0.6f, 1.7f, 0.6f});
    registry.emplace<moteur::Transform>(hero_, transform);
    registry.emplace<moteur::PreviousTransform>(hero_, transform);
    registry.emplace<moteur::Name>(hero_, "Héros");
    moteur::Material body = surface(linear(0.25f, 0.45f, 0.85f), 0.5f);
    registry.emplace<moteur::MeshComponent>(hero_, cube_, body);
    moteur::Collider collider;
    collider.radius = kHeroRadius;
    collider.push_weight = 4;  // the monsters give way more than it does
    registry.emplace<moteur::Collider>(hero_, collider);
    moteur::Mover mover;
    mover.speed = 4.5f;
    registry.emplace<moteur::Mover>(hero_, mover);
}

entt::entity WorldTest::spawn_monster(glm::vec2 at, bool big) {
    entt::registry& registry = world_.registry();
    const float radius = big ? kBigRadius : kMonsterRadius;
    const glm::vec2 p = moteur::nearest_fit(grid_, at, radius).value_or(at);
    const entt::entity monster = registry.create();
    const glm::vec3 size = big ? glm::vec3(1.2f, 2.2f, 1.2f) : glm::vec3(0.5f, 1.4f, 0.5f);
    const moteur::Transform transform = place({p.x, size.y * 0.5f, p.y}, size);
    registry.emplace<moteur::Transform>(monster, transform);
    registry.emplace<moteur::PreviousTransform>(monster, transform);
    registry.emplace<moteur::Name>(monster, big ? "Gros monstre" : "Monstre");
    registry.emplace<moteur::MeshComponent>(monster, cube_, surface(big ? linear(0.5f, 0.2f, 0.45f) : linear(0.7f, 0.2f, 0.15f), 0.6f));
    moteur::Collider collider;
    collider.radius = radius;
    collider.push_weight = big ? 3 : 1;
    registry.emplace<moteur::Collider>(monster, collider);
    moteur::Mover mover;
    mover.speed = big ? 2.2f : 3.2f;
    mover.field = big ? 1 : 0;
    registry.emplace<moteur::Mover>(monster, mover);
    Monster m;
    m.big = big;
    registry.emplace<Monster>(monster, m);
    return monster;
}

void WorldTest::spawn_group(glm::vec2 around, int count, bool big) {
    for (int i = 0; i < count; ++i) {
        const glm::vec2 offset(random_.next() * 2.0f - 1.0f, random_.next() * 2.0f - 1.0f);
        spawn_monster(around + offset * 1.2f, big);
    }
}

void WorldTest::start_fires() {
    for (const moteur::EffectId fire : fires_) {
        particles_.kill(fire);
    }
    fires_.clear();
    if (!fires_on_ || !app_.assets().exists("effects/fire.json")) {
        return;
    }
    const moteur::Asset<moteur::ParticleEffect> fire = app_.assets().particle_effect("effects/fire.json");
    for (const glm::ivec2 cell : map_->points("but")) {
        const glm::vec2 c = moteur::cell_centre(cell);
        fires_.push_back(particles_.play(fire, {c.x, 0.05f, c.y}));
    }
}

void WorldTest::toggle_wall(glm::ivec2 cell) {
    const moteur::TileId wall = map_->tile_id("mur");
    const moteur::TileId floor = map_->tile_id("sol");
    if (wall == moteur::kNoTile || floor == moteur::kNoTile || tiles_.layer_count() < 2) {
        return;  // a map without "sol" and "mur" on two layers: nothing to put
    }
    if (grid_.walkable(cell)) {
        tiles_.set(0, cell, floor);
        tiles_.set(1, cell, wall);
    } else {
        tiles_.set(0, cell, floor);
        for (int layer = 1; layer < tiles_.layer_count(); ++layer) {
            tiles_.set(layer, cell, moteur::kNoTile);
        }
    }
    if (grid_.refresh(tiles_, map_->tileset(), cell)) {
        ++toggled_;
        clearance_.update_around(cell);
        build_decor();
        // Whoever stands in the new wall is put back where it fits; paths are planned again.
        for (auto [entity, mover, collider] : world_.registry().view<moteur::Mover, moteur::Collider>().each()) {
            const glm::vec2 p = moteur::plane_position(world_.registry(), entity);
            const glm::vec2 out = moteur::push_out_of_walls(grid_, p, collider.radius);
            if (out != p) {
                world_.registry().patch<moteur::Transform>(entity, [out](moteur::Transform& t) {
                    t.position.x = out.x;
                    t.position.z = out.y;
                });
            }
            if (mover.mode == moteur::MoveMode::ToPoint) {
                mover.needs_path = true;
            }
        }
    }
}

std::string WorldTest::point_at(glm::ivec2 cell) const {
    for (const auto& [name, cells] : map_->points()) {
        if (std::find(cells.begin(), cells.end(), cell) != cells.end()) {
            return name;
        }
    }
    return {};
}

void WorldTest::on_event(const SDL_Event& event) {
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
        if (standalone_) {
            app_.quit();
        } else {
            stop_requested_ = true;
        }
    }
}

void WorldTest::apply_input(float dt) {
    const moteur::Input& input = app_.input();
    const int zoom = input.presses(actions_.zoom_out) - input.presses(actions_.zoom_in);
    if (zoom != 0) {
        camera_.set_visible_height(
            std::clamp(camera_.visible_height() * std::pow(1.0f / 0.9f, static_cast<float>(zoom)), 4.0f, 100.0f));
    }
    if (input.pressed(actions_.projection)) {
        camera_.set_projection(camera_.projection() == moteur::Projection::Perspective ? moteur::Projection::Orthographic
                                                                                       : moteur::Projection::Perspective);
    }
    if (input.pressed(actions_.follow)) {
        follow_camera_ = !follow_camera_;
    }
    const glm::vec3 forward = camera_.forward();
    const glm::vec3 ahead = glm::normalize(glm::vec3(forward.x, 0.0f, forward.z));
    const glm::vec3 right = glm::cross(ahead, glm::vec3(0.0f, 1.0f, 0.0f));
    if (const glm::vec2 direction = input.axis(actions_.camera); direction != glm::vec2(0.0f)) {
        follow_camera_ = false;
        const float speed = camera_.visible_height() * 1.2f;
        camera_.set_target(camera_.target() + (right * direction.x + ahead * direction.y) * (speed * dt));
    }

    // The pointed ground, with the camera of this tick.
    hovered_.reset();
    pointer_ground_.reset();
    const glm::vec2 pointer = input.pointer();
    const bool over_panel = !standalone_ && ImGui::GetIO().WantCaptureMouse;
    if (pointer.x >= 0.0f && pointer.y >= 0.0f && !over_panel) {
        if (const auto ground = camera_.ground_point(pointer)) {
            pointer_ground_ = ground;
            if (const glm::ivec2 cell = moteur::cell_at({ground->x, ground->z}); grid_.contains(cell)) {
                hovered_ = cell;
            }
        }
    }
    if (hovered_ && input.pressed(actions_.toggle_wall)) {
        toggle_wall(*hovered_);
    }
    if (pointer_ground_ && input.pressed(actions_.spawn)) {
        spawn_group({pointer_ground_->x, pointer_ground_->z}, 6, false);
    }
    if (pointer_ground_ && input.pressed(actions_.spawn_big)) {
        spawn_monster({pointer_ground_->x, pointer_ground_->z}, true);
    }
    if (pointer_ground_ && input.pressed(actions_.effect) && !effect_names_.empty()) {
        const std::string path = std::string(kEffectsDirectory) + "/" + effect_names_[static_cast<std::size_t>(effect_choice_)] + ".json";
        try {
            particles_.play(app_.assets().particle_effect(path), *pointer_ground_ + glm::vec3(0.0f, 0.6f, 0.0f));
        } catch (const std::exception& e) {
            error_ = e.what();
        }
    }

    if (hero_ == entt::null) {
        return;
    }
    moteur::Mover& hero = world_.registry().get<moteur::Mover>(hero_);
    const glm::vec2 at = hero_position();
    // Click: a path to the point; held: towards the pointer, straight when the way is clear.
    if (pointer_ground_ && input.pressed(actions_.move_to)) {
        pressed_on_ground_ = true;
        hero.go_to({pointer_ground_->x, pointer_ground_->z});
        follow_repath_ = 10;
    } else if (pointer_ground_ && pressed_on_ground_ && input.down(actions_.move_to)) {
        const glm::vec2 goal(pointer_ground_->x, pointer_ground_->z);
        if (glm::length(goal - at) > 0.2f && moteur::segment_clear(grid_, at, goal, kHeroRadius)) {
            hero.mode = moteur::MoveMode::ToPoint;
            hero.goal = goal;
            hero.path = {goal};
            hero.next_point = 0;
            hero.needs_path = false;
        } else if (--follow_repath_ <= 0 && glm::length(goal - hero.goal) > 0.5f) {
            hero.go_to(goal);
            follow_repath_ = 10;
        }
    }
    if (!input.down(actions_.move_to)) {
        pressed_on_ground_ = false;
    }
    // Keys or stick, relative to the screen: straight, sliding on the walls.
    if (const glm::vec2 direction = input.axis(actions_.move); direction != glm::vec2(0.0f)) {
        const glm::vec3 d = right * direction.x + ahead * direction.y;
        hero.move({d.x, d.z});
    } else if (hero.mode == moteur::MoveMode::Direct) {
        hero.stop();
    }
}

void WorldTest::think(float /*dt*/) {
    // The monsters: they chase the hero once they see it, and give up after a while without.
    entt::registry& registry = world_.registry();
    if (hero_ == entt::null) {
        return;
    }
    const glm::vec2 hero = hero_position();
    std::vector<entt::entity> monsters(registry.view<Monster>().begin(), registry.view<Monster>().end());
    std::sort(monsters.begin(), monsters.end(), [](entt::entity a, entt::entity b) { return entt::to_integral(a) < entt::to_integral(b); });
    for (const entt::entity entity : monsters) {
        Monster& monster = registry.get<Monster>(entity);
        moteur::Mover& mover = registry.get<moteur::Mover>(entity);
        moteur::Collider& collider = registry.get<moteur::Collider>(entity);
        const glm::vec2 p = moteur::plane_position(registry, entity);
        const float distance = glm::length(hero - p);
        const bool sees = monster.always_chase ||
                          (distance <= kMonsterSight && moteur::line_of_sight(grid_, p, hero));
        if (sees) {
            monster.chasing = true;
            monster.lost_ticks = 0;
        } else if (monster.chasing && ++monster.lost_ticks > 180) {
            monster.chasing = false;  // three seconds without seeing it
        }
        const float reach = kHeroRadius + collider.radius + 0.15f;
        if (monster.chasing) {
            if (mover.mode != moteur::MoveMode::FollowField) {
                mover.follow_field(reach - 0.05f);
            }
        } else if (mover.mode != moteur::MoveMode::Stop) {
            mover.stop();
        }
        // In reach: it stands its ground while it strikes (immovable), sparks at each blow.
        const bool striking = monster.chasing && distance <= reach;
        collider.push_weight = striking ? 0 : (monster.big ? 3 : 1);
        if (striking) {
            if (--monster.strike_ticks <= 0) {
                monster.strike_ticks = 50;
                if (sparks_ && !monster.always_chase) {
                    const glm::vec2 hit = p + (hero - p) * 0.6f;
                    particles_.play(sparks_, {hit.x, 1.0f, hit.y}, 0.7f);
                }
            }
        } else {
            monster.strike_ticks = 20;
        }
    }
}

void WorldTest::update_fields() {
    if (hero_ == entt::null) {
        return;
    }
    const glm::vec2 hero = hero_position();
    const glm::ivec2 cell = moteur::cell_at(hero);
    if (cell == field_cell_ && grid_.version() == field_version_) {
        return;
    }
    field_cell_ = cell;
    field_version_ = grid_.version();
    const int reach = options_.crowd > 0 ? 14 * (grid_.width() + grid_.height()) : kFieldCost;
    fields_[0].compute(grid_, clearance_, hero, kMonsterRadius, reach);
    fields_[1].compute(grid_, clearance_, hero, kBigRadius, reach);
}

void WorldTest::update_sight() {
    if (hero_ == entt::null) {
        return;
    }
    const glm::ivec2 cell = moteur::cell_at(hero_position());
    if (cell != view_cell_ || grid_.version() != view_version_) {
        view_cell_ = cell;
        view_version_ = grid_.version();
        // What was in sight becomes explored; then what is in sight now.
        for (const glm::ivec2 c : view_.cells()) {
            if (grid_.contains(c)) {
                fog_cells_[static_cast<std::size_t>(c.y * grid_.width() + c.x)] = 128;
            }
        }
        view_.compute(grid_, cell, kSightRadius);
        explored_.add(view_);
        for (const glm::ivec2 c : view_.cells()) {
            if (grid_.contains(c)) {
                fog_cells_[static_cast<std::size_t>(c.y * grid_.width() + c.x)] = 255;
            }
        }
        app_.renderer().meshes().set_fog_cells(grid_.width(), grid_.height(), fog_cells_);
    }
    // Monsters out of sight are hidden (the fog would only dim them).
    entt::registry& registry = world_.registry();
    for (const entt::entity entity : registry.view<Monster>()) {
        const bool seen = !fog_on_ || view_.visible(moteur::cell_at(moteur::plane_position(registry, entity)));
        if (seen) {
            registry.remove<moteur::Hidden>(entity);
        } else if (!registry.all_of<moteur::Hidden>(entity)) {
            registry.emplace<moteur::Hidden>(entity);
        }
    }
}

void WorldTest::update(double dt) {
    elapsed_ += dt;
    ++ticks_;
    if (options_.run_seconds > 0.0 && elapsed_ >= options_.run_seconds) {
        if (standalone_) {
            app_.quit();
        } else {
            stop_requested_ = true;
        }
    }
    camera_.begin_update();
    world_.begin_tick();
    if (map_->revision() != built_revision_) {
        reset_tiles();  // the file changed (hot reload): the walls put by hand go
    }
    const bool frozen = options_.freeze_after_ticks > 0 && ticks_ > options_.freeze_after_ticks;
    if (frozen) {
        return;
    }
    const auto step = static_cast<float>(dt);
    apply_input(step);

    // The systems of a tick, in their order.
    Uint64 start = SDL_GetPerformanceCounter();
    think(step);
    timings_.ai += seconds_since(start) * 1000.0;
    start = SDL_GetPerformanceCounter();
    update_fields();
    timings_.fields += seconds_since(start) * 1000.0;
    movement_stats_ = {};
    start = SDL_GetPerformanceCounter();
    moteur::plan_paths(world_.registry(), grid_, clearance_, kPathsPerTick, movement_stats_);
    timings_.paths += seconds_since(start) * 1000.0;
    start = SDL_GetPerformanceCounter();
    const moteur::FlowField* const fields[2] = {&fields_[0], &fields_[1]};
    moteur::move_movers(world_.registry(), grid_, step, fields, movement_stats_);
    timings_.moves += seconds_since(start) * 1000.0;
    start = SDL_GetPerformanceCounter();
    collision_stats_ = moteur::separate_colliders(world_.registry(), grid_, hash_);
    moteur::finish_movers(world_.registry(), step);
    timings_.collisions += seconds_since(start) * 1000.0;
    start = SDL_GetPerformanceCounter();
    update_sight();
    timings_.sight += seconds_since(start) * 1000.0;
    ++timings_.ticks;
    if (ticks_ - shown_at_ >= 30) {
        shown_at_ = ticks_;
        shown_ = timings_;
    }

    // A frozen capture needs particles that do not depend on the frame rate: they follow the ticks.
    if (!options_.capture_path.empty()) {
        particles_.update(step);
    }
    if (trail_on_ && trail_ == moteur::kNoEffect && app_.assets().exists("effects/magic_trail.json")) {
        trail_ = particles_.play(app_.assets().particle_effect("effects/magic_trail.json"), glm::vec3(0.0f));
    } else if (!trail_on_ && trail_ != moteur::kNoEffect) {
        particles_.stop(trail_);
        trail_ = moteur::kNoEffect;
    }
    if (follow_camera_ && hero_ != entt::null) {
        const glm::vec2 p = hero_position();
        camera_.follow({p.x, 0.0f, p.y}, step, 0.2f);
    }
}

void WorldTest::render(moteur::Renderer& renderer, double alpha) {
    const bool frozen = options_.freeze_after_ticks > 0 && ticks_ > options_.freeze_after_ticks;
    if (!options_.capture_path.empty() && !capture_done_ && frozen) {
        renderer.request_capture(options_.capture_path);
        capture_done_ = true;
    }
    renderer.set_clear_color(0.03f, 0.03f, 0.04f);
    camera_.set_viewport({static_cast<float>(renderer.width()), static_cast<float>(renderer.height())});
    const moteur::Camera3D camera = camera_.interpolated(alpha);
    const auto blend = static_cast<float>(alpha);

    moteur::MeshRenderer& meshes = renderer.meshes();
    meshes.set_camera(camera.view_projection(), camera.position());
    renderer.billboards().set_camera(camera);
    const float yaw = glm::radians(-60.0f), elevation = glm::radians(50.0f);
    meshes.set_sun({std::cos(elevation) * std::cos(yaw), std::sin(elevation), std::cos(elevation) * std::sin(yaw)},
                   linear(1.0f, 0.93f, 0.8f), 2.5f);
    meshes.set_environment(&*sky_, 0.8f);

    moteur::FogOfWar fog;
    fog.enabled = fog_on_;
    meshes.set_fog(fog);
    moteur::Cutout cutout;
    if (cutout_on_ && hero_ != entt::null) {
        cutout.enabled = true;
        cutout.focus = world_.world_position(hero_, blend);
    }
    meshes.set_cutout(cutout);

    moteur::CollectOptions collect;
    collect.view = camera.frustum();
    world_.submit(renderer, blend, collect);

    // Particles: real time, except for frozen captures (see update()).
    const std::uint64_t now = SDL_GetTicksNS();
    const float frame_seconds = last_frame_ns_ == 0 ? 0.0f : static_cast<float>(now - last_frame_ns_) * 1e-9f;
    last_frame_ns_ = now;
    const Uint64 start = SDL_GetPerformanceCounter();
    if (trail_ != moteur::kNoEffect && hero_ != entt::null) {
        particles_.move(trail_, world_.world_position(hero_, blend) + glm::vec3(0.0f, 0.4f, 0.0f));
    }
    if (options_.capture_path.empty() && !frozen) {
        particles_.update(frame_seconds);
    }
    const moteur::Frustum view = camera.frustum();
    particles_.draw(renderer.billboards(), app_.assets(), &view);
    particles_.add_lights(meshes, 8, camera.target());
    timings_.particles += seconds_since(start) * 1000.0;
    ++timings_.frames;

    if (hovered_) {
        const glm::vec2 c = moteur::cell_centre(*hovered_);
        meshes.draw(*tile_, place({c.x, 0.02f, c.y}, glm::vec3(1.0f)).matrix(), glowing({0.9f, 0.75f, 0.2f}));
    }
    draw_overlays(renderer, camera, blend);
}

void WorldTest::draw_overlays(moteur::Renderer& renderer, const moteur::Camera3D& camera, float alpha) {
    moteur::DebugLineBuffer& lines = renderer.debug_lines().lines();
    const entt::registry& registry = world_.registry();
    // Only the cells around what the camera looks at.
    const glm::vec3 target = camera.target();
    const int reach = static_cast<int>(camera.visible_height() * 1.1f) + 2;
    const glm::ivec2 low = glm::max(moteur::cell_at({target.x, target.z}) - reach, glm::ivec2(0));
    const glm::ivec2 high = glm::min(moteur::cell_at({target.x, target.z}) + reach, glm::ivec2(grid_.width() - 1, grid_.height() - 1));

    if (show_grid_ || show_clearance_ || show_view_) {
        for (int j = low.y; j <= high.y; ++j) {
            for (int i = low.x; i <= high.x; ++i) {
                const glm::ivec2 cell(i, j);
                if (show_grid_ && !grid_.walkable(cell)) {
                    cell_square(lines, cell, 0.05f, grid_.opaque(cell) ? glm::vec4(1, 0.2f, 0.2f, 1) : glm::vec4(1, 0.6f, 0.1f, 1), 1.6f);
                }
                if (show_clearance_ && grid_.walkable(cell)) {
                    const float c = clearance_.clearance(cell) / moteur::ClearanceMap::kMaxClearance;
                    cell_square(lines, cell, 0.45f - 0.4f * c, glm::vec4(1.0f - c, c, 0.2f, 1));
                }
                if (show_view_ && view_.visible(cell)) {
                    cell_square(lines, cell, 0.15f, glm::vec4(0.3f, 1.0f, 0.4f, 1), 0.04f);
                }
            }
        }
    }
    if (show_field_ && fields_[0].valid()) {
        for (int j = low.y; j <= high.y; ++j) {
            for (int i = low.x; i <= high.x; ++i) {
                const glm::ivec2 next = fields_[0].next({i, j});
                if (next.x < 0 || next == glm::ivec2(i, j)) {
                    continue;
                }
                const glm::vec2 a = moteur::cell_centre({i, j});
                const glm::vec2 b = a + (moteur::cell_centre(next) - a) * 0.4f;
                lines.line({a.x, 0.05f, a.y}, {b.x, 0.05f, b.y}, {0.3f, 0.7f, 1.0f, 1});
                lines.circle({b.x, 0.05f, b.y}, {0, 1, 0}, 0.04f, {0.3f, 0.7f, 1.0f, 1}, 6);
            }
        }
    }
    if (show_colliders_) {
        for (auto [entity, collider] : registry.view<moteur::Collider>().each()) {
            if (registry.all_of<moteur::Hidden>(entity)) {
                continue;
            }
            const glm::vec3 p = world_.world_position(entity, alpha);
            const glm::vec4 color = entity == hero_ ? glm::vec4(1.0f) : (collider.push_weight == 0 ? glm::vec4(1, 0.9f, 0.2f, 1) : glm::vec4(1, 0.35f, 0.3f, 1));
            lines.circle({p.x, 0.05f, p.z}, {0, 1, 0}, collider.radius, color, 24, true);
        }
    }
    if (show_paths_) {
        for (auto [entity, mover] : registry.view<moteur::Mover>().each()) {
            if (mover.mode != moteur::MoveMode::ToPoint || mover.next_point >= mover.path.size()) {
                continue;
            }
            const glm::vec3 p = world_.world_position(entity, alpha);
            glm::vec3 from(p.x, 0.08f, p.z);
            for (std::size_t i = mover.next_point; i < mover.path.size(); ++i) {
                const glm::vec3 to(mover.path[i].x, 0.08f, mover.path[i].y);
                lines.line(from, to, {0.2f, 1.0f, 1.0f, 1}, true);
                lines.circle(to, {0, 1, 0}, 0.08f, {0.2f, 1.0f, 1.0f, 1}, 8, true);
                from = to;
            }
        }
    }
    if (hero_ != entt::null && (show_query_ || show_ray_)) {
        const glm::vec3 hp = world_.world_position(hero_, alpha);
        const glm::vec2 facing = registry.get<moteur::Mover>(hero_).facing;
        if (show_query_) {
            // The monsters in a cone of 60 degrees each side, 3 m ahead: what a sweeping blow hits.
            constexpr float kRange = 3.0f;
            const glm::vec2 left(facing.x * 0.5f - facing.y * 0.866f, facing.y * 0.5f + facing.x * 0.866f);
            const glm::vec2 right(facing.x * 0.5f + facing.y * 0.866f, facing.y * 0.5f - facing.x * 0.866f);
            const glm::vec3 o(hp.x, 0.06f, hp.z);
            lines.line(o, o + glm::vec3(left.x, 0, left.y) * kRange, {1, 0.3f, 1, 1}, true);
            lines.line(o, o + glm::vec3(right.x, 0, right.y) * kRange, {1, 0.3f, 1, 1}, true);
            for (const entt::entity hit : hash_.query_cone({hp.x, hp.z}, facing, kRange, 0.5f)) {
                if (hit == hero_ || !registry.valid(hit)) {
                    continue;
                }
                const glm::vec3 p = world_.world_position(hit, alpha);
                lines.circle({p.x, 0.1f, p.z}, {0, 1, 0}, 0.5f, {1, 0.3f, 1, 1}, 16, true);
            }
        }
        if (show_ray_ && pointer_ground_) {
            const glm::vec2 from(hp.x, hp.z);
            const glm::vec2 to(pointer_ground_->x, pointer_ground_->z);
            const auto hit = moteur::raycast(grid_, from, to, moteur::RayBlock::Unwalkable);
            const glm::vec2 end = hit ? hit->point : to;
            lines.line({from.x, 0.9f, from.y}, {end.x, 0.9f, end.y}, hit ? glm::vec4(1, 0.4f, 0.2f, 1) : glm::vec4(0.4f, 1, 0.4f, 1), true);
            if (hit) {
                lines.circle({end.x, 0.9f, end.y}, {hit->normal.x, 0, hit->normal.y}, 0.15f, {1, 0.4f, 0.2f, 1}, 12, true);
            }
        }
    }

    if (standalone_) {
        return;  // no ImGui frame without the menu
    }
    ImDrawList* labels = ImGui::GetBackgroundDrawList();
    const glm::vec2 pixels_per_point = app_.to_pixels({1.0f, 1.0f}) - app_.to_pixels({0.0f, 0.0f});
    auto label = [&](glm::vec3 world, const std::string& text, ImU32 color) {
        if (const auto screen = camera.world_to_screen(world)) {
            const glm::vec2 points = *screen / pixels_per_point;
            labels->AddText({points.x + 1.0f, points.y + 1.0f}, IM_COL32(0, 0, 0, 200), text.c_str());
            labels->AddText({points.x, points.y}, color, text.c_str());
        }
    };
    if (show_axes_) {
        lines.axes({0.0f, 0.02f, 0.0f}, 3.0f);
        label({3.2f, 0.0f, 0.0f}, "x", IM_COL32(255, 90, 90, 255));
        label({0.0f, 0.0f, 3.2f}, "z", IM_COL32(90, 140, 255, 255));
    }
    if (show_points_) {
        for (const auto& [name, cells] : map_->points()) {
            const glm::vec3 color = point_color(name);
            for (const glm::ivec2 cell : cells) {
                const glm::vec2 c = moteur::cell_centre(cell);
                label({c.x, 0.3f, c.y}, name,
                      IM_COL32(static_cast<int>(color.r * 255.0f), static_cast<int>(color.g * 255.0f),
                               static_cast<int>(color.b * 255.0f), 255));
            }
        }
    }
}

void WorldTest::draw_controls() {
    if (ImGui::BeginCombo("Carte", names_[static_cast<std::size_t>(chosen_)].c_str())) {
        for (std::size_t i = 0; i < names_.size(); ++i) {
            if (ImGui::Selectable(names_[i].c_str(), static_cast<int>(i) == chosen_)) {
                load_map(names_[i]);
            }
        }
        ImGui::EndCombo();
    }
    if (!error_.empty()) {
        ImGui::TextColored({1.0f, 0.4f, 0.4f, 1.0f}, "%s", error_.c_str());
    }
    const moteur::MapData& map = *map_;
    ImGui::TextWrapped("%s", map.description().c_str());
    ImGui::Text("%d x %d cases, %d calques ; grille version %llu, %d mur(s) changé(s)", map.width(), map.height(),
                tiles_.layer_count(), static_cast<unsigned long long>(grid_.version()), toggled_);
    if (ImGui::Button("Recommencer")) {
        reset_tiles();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Caméra sur le héros", &follow_camera_);
    ImGui::TextDisabled("Clic : aller (maintenu : suivre) ; ZQSD : marcher ; M : monstres ; B : gros monstre ;");
    ImGui::TextDisabled("E : effet ; clic droit : mur ; flèches : caméra ; Espace : suivre ; molette : zoom");

    ImGui::SeparatorText("Affichage");
    if (ImGui::Checkbox("Brouillard", &fog_on_)) {
        update_sight();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Murs tramés", &cutout_on_);
    ImGui::SameLine();
    if (ImGui::Checkbox("Feux", &fires_on_)) {
        start_fires();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Traînée", &trail_on_);
    ImGui::Checkbox("Grille", &show_grid_);
    ImGui::SameLine();
    ImGui::Checkbox("Dégagement", &show_clearance_);
    ImGui::SameLine();
    ImGui::Checkbox("Cercles", &show_colliders_);
    ImGui::SameLine();
    ImGui::Checkbox("Chemins", &show_paths_);
    ImGui::Checkbox("Flow field", &show_field_);
    ImGui::SameLine();
    ImGui::Checkbox("Champ de vision", &show_view_);
    ImGui::SameLine();
    ImGui::Checkbox("Cône", &show_query_);
    ImGui::SameLine();
    ImGui::Checkbox("Rayon", &show_ray_);
    ImGui::Checkbox("Axes", &show_axes_);
    ImGui::SameLine();
    ImGui::Checkbox("Noms des points", &show_points_);
    if (!effect_names_.empty() &&
        ImGui::BeginCombo("Effet (E)", effect_names_[static_cast<std::size_t>(effect_choice_)].c_str())) {
        for (std::size_t i = 0; i < effect_names_.size(); ++i) {
            if (ImGui::Selectable(effect_names_[i].c_str(), static_cast<int>(i) == effect_choice_)) {
                effect_choice_ = static_cast<int>(i);
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SeparatorText("Systèmes (ms par tick, moyennes)");
    if (shown_.ticks > 0) {
        const auto n = static_cast<double>(shown_.ticks);
        ImGui::Text("IA %.3f  chemins %.3f  flow fields %.3f  déplacements %.3f", shown_.ai / n, shown_.paths / n,
                    shown_.fields / n, shown_.moves / n);
        ImGui::Text("collisions %.3f  vision %.3f  particules %.3f par image", shown_.collisions / n, shown_.sight / n,
                    shown_.frames > 0 ? shown_.particles / static_cast<double>(shown_.frames) : 0.0);
    }
    const auto monsters = world_.registry().view<Monster>().size();
    ImGui::Text("%zu monstres ; %d paires testées, %d chevauchements ; %d A* (%d cases)", static_cast<std::size_t>(monsters),
                collision_stats_.pairs_tested, collision_stats_.overlaps, movement_stats_.paths, movement_stats_.expanded);
    const moteur::ParticleStats ps = particles_.stats();
    ImGui::Text("%d effets, %d particules (budget %zu, %ld refusées)", ps.effects, ps.particles, particles_.budget(), ps.refused);
    ImGui::Text("Exploré : %d cases ; en vue : %zu", explored_.count(), view_.cells().size());

    ImGui::SeparatorText("Case sous le pointeur");
    if (hovered_) {
        const glm::ivec2 cell = *hovered_;
        ImGui::Text("(%d, %d) : %s, %s, dégagement %.2f m", cell.x, cell.y, grid_.walkable(cell) ? "praticable" : "bloquée",
                    grid_.opaque(cell) ? "opaque" : "transparente", clearance_.clearance(cell));
        std::string tiles;
        for (int layer = 0; layer < tiles_.layer_count(); ++layer) {
            const std::string& name = map.tile_name(tiles_.at(layer, cell));
            tiles += (layer > 0 ? " | " : "") + (name.empty() ? std::string("-") : name);
        }
        ImGui::Text("Calques : %s ; flow field : %d", tiles.c_str(), fields_[0].cost(cell));
        if (const std::string point = point_at(cell); !point.empty()) {
            ImGui::Text("Point : %s", point.c_str());
        }
    } else {
        ImGui::TextDisabled("aucune");
    }

    ImGui::Checkbox("Grille en caractères", &show_ascii_);
    if (show_ascii_) {
        ImGui::TextDisabled(". praticable  # bloquée et opaque  x bloquée, transparente  %% praticable, opaque");
        ImGui::BeginChild("ascii", {0.0f, 260.0f}, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
        const std::string text = grid_.to_ascii();
        ImGui::TextUnformatted(text.c_str(), text.c_str() + text.size());
        ImGui::EndChild();
    }
}
