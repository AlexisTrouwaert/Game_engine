#include "demo3d.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>

#include "moteur/billboard_renderer.hpp"
#include "moteur/color.hpp"
#include "moteur/fixed_timestep.hpp"
#include "moteur/image.hpp"
#include "moteur/mesh_renderer.hpp"
#include "moteur/paths.hpp"
#include "moteur/sprite_renderer.hpp"

namespace {

constexpr float kFontPixelHeight = 20.0f;
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

glm::mat4 place(glm::vec3 position, glm::vec3 scale, float yaw = 0.0f) {
    return glm::scale(glm::rotate(glm::translate(glm::mat4(1.0f), position), yaw, glm::vec3(0.0f, 1.0f, 0.0f)), scale);
}

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

    cube_ = moteur::Mesh::create(renderer, moteur::make_cube(), "demo.cube");
    tile_ = moteur::Mesh::create(renderer, moteur::make_plane(), "demo.tile");
    sphere_ = moteur::Mesh::create(renderer, moteur::make_sphere(0.5f, 16, 8), "demo.sphere");
    rock_ = moteur::Mesh::create(renderer, moteur::make_sphere(0.5f, 7, 4), "demo.rock");  // faceted
    font_ = moteur::Font::load(renderer, moteur::asset_path("fonts/Inter-Regular.ttf"), kFontPixelHeight);
    sky_ = moteur::Environment::create(renderer, moteur::make_sky(512, 256), "demo.sky");
    try {
        barrel_ = moteur::Model::load(renderer, moteur::asset_path("models/polyhaven/wine_barrel_01/wine_barrel_01_1k.gltf"));
    } catch (const std::exception& e) {
        SDL_Log("Demo 3D: no barrel model (%s), crates instead", e.what());  // tools/models/fetch_test_models.py
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
    glow_ = renderer.create_texture(glow, mask, "demo.glow");
    moteur::Image white;
    white.width = 1;
    white.height = 1;
    white.pixels = {255, 255, 255, 255};
    white_ = renderer.create_texture(white, "demo.white");

    build_map();
    build_floor();
    build_decor();
    spawn_creatures();

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
    if (options_.fake_mouse) {
        mouse_ = options_.fake_mouse_position;
    }
}

Demo3D::~Demo3D() = default;

void Demo3D::add_static(const moteur::Mesh& mesh, const glm::mat4& world, const moteur::Material& material) {
    statics_.push_back({&mesh, world, material, moteur::transform_box(mesh.bounds, world)});
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
        floor_blocks_.reserve(static_cast<std::size_t>(blocks * blocks * 2));  // statics_ points into it
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
                    floor_blocks_.push_back(moteur::Mesh::create(app_.renderer(), block, "demo.floor block"));
                    add_static(floor_blocks_.back(), glm::mat4(1.0f), tone == 0 ? light : dark);
                }
            }
        }
    }
    floor_draws_ = statics_.size();
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

    // A brazier in the middle of each room: a post, and a flame whose light is a torch.
    static constexpr glm::vec3 kFlames[3] = {{1.0f, 0.5f, 0.15f}, {1.0f, 0.65f, 0.25f}, {1.0f, 0.42f, 0.1f}};
    const moteur::Material iron = surface(linear(0.3f, 0.3f, 0.32f), 0.5f, 1.0f);
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            if (is_brazier_cell(i, j)) {
                const glm::vec3 foot(static_cast<float>(i) + 0.5f, 0.0f, static_cast<float>(j) + 0.5f);
                add_static(cube_, place(foot + glm::vec3(0.0f, 0.6f, 0.0f), {0.25f, 1.2f, 0.25f}), iron);
                torches_.push_back({foot + glm::vec3(0.0f, 1.45f, 0.0f), kFlames[torches_.size() % 3]});
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
            const glm::mat4 world = place(spot, glm::vec3(1.0f), yaw);
            for (const moteur::Model::Part& part : barrel_->parts) {
                const moteur::Material material =
                    part.material >= 0 ? barrel_->materials[static_cast<std::size_t>(part.material)] : moteur::Material{};
                add_static(part.mesh, world * part.transform, material);
            }
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
    creatures_.reserve(static_cast<std::size_t>(options_.creatures));
    for (int k = 0; k < options_.creatures; ++k) {
        glm::vec2 position(0.0f);
        do {
            position = {1.0f + random.next() * (n - 2.0f), 1.0f + random.next() * (n - 2.0f)};
        } while (!walkable(position));
        const glm::vec2 direction = kDirections[static_cast<std::size_t>(random.next() * 8.0f) % 8];
        const float pace = 0.75f + 0.5f * random.next();
        const glm::vec3 color = linear(0.35f + 0.55f * random.next(), 0.35f + 0.55f * random.next(), 0.35f + 0.55f * random.next());
        const float health = 0.3f + 0.7f * random.next();
        creatures_.push_back({position, position, direction * (kCreatureSpeed * pace), color, health, std::nullopt});
    }
    // Start next to the first creature.
    if (!creatures_.empty()) {
        const glm::vec2 first = creatures_.front().position;
        camera_.set_target({first.x, 0.0f, first.y});
    }
}

bool Demo3D::walkable(glm::vec2 p) const {
    return map_->walkable(tileset_, glm::ivec2(glm::floor(p)));
}

void Demo3D::move_creatures(float dt) {
    for (Creature& creature : creatures_) {
        if (creature.goal) {
            const glm::vec2 to_goal = *creature.goal - creature.position;
            const float distance = glm::length(to_goal);
            if (distance <= kSentSpeed * dt) {
                creature.position = *creature.goal;
                creature.goal.reset();
                creature.velocity = glm::vec2(0.0f);  // arrived: waits there
                continue;
            }
            const glm::vec2 next = creature.position + to_goal / distance * (kSentSpeed * dt);
            if (walkable(next)) {
                creature.position = next;
            } else {
                creature.goal.reset();  // a wall in the way, and no pathfinding yet: it stops
                creature.velocity = glm::vec2(0.0f);
            }
            continue;
        }
        const glm::vec2 next = creature.position + creature.velocity * dt;
        if (walkable(next)) {
            creature.position = next;
        } else {
            creature.velocity = -creature.velocity;  // turns back before a wall, as in the 2D demo
        }
    }
}

void Demo3D::move_camera(float dt) {
    const bool* keys = SDL_GetKeyboardState(nullptr);
    glm::vec2 direction(0.0f);  // x: right, y: forward on screen
    if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]) direction.x -= 1.0f;
    if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) direction.x += 1.0f;
    if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) direction.y += 1.0f;
    if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]) direction.y -= 1.0f;
    if (direction == glm::vec2(0.0f)) {
        return;
    }
    const glm::vec3 forward = camera_.forward();
    const glm::vec3 ahead = glm::normalize(glm::vec3(forward.x, 0.0f, forward.z));
    const glm::vec3 right = glm::cross(ahead, glm::vec3(0.0f, 1.0f, 0.0f));
    const float speed = camera_.visible_height() * 1.2f;  // the same speed on screen at any zoom
    glm::vec3 target = camera_.target() + (right * direction.x + ahead * direction.y) * (speed * dt / glm::length(direction));
    const auto n = static_cast<float>(options_.map_size);
    target.x = std::clamp(target.x, 0.0f, n);
    target.z = std::clamp(target.z, 0.0f, n);
    camera_.set_target(target);
}

int Demo3D::creature_near(glm::vec3 ground, float radius) const {
    int best = -1;
    float best_distance = radius * radius;
    for (std::size_t k = 0; k < creatures_.size(); ++k) {
        const glm::vec2 d = creatures_[k].position - glm::vec2(ground.x, ground.z);
        const float distance = glm::dot(d, d);
        if (distance <= best_distance) {
            best_distance = distance;
            best = static_cast<int>(k);
        }
    }
    return best;
}

void Demo3D::on_event(const SDL_Event& event) {
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
        if (standalone_) {
            app_.quit();
        } else {
            stop_requested_ = true;
        }
        return;
    }
    if (options_.no_input) {
        return;
    }
    switch (event.type) {
        case SDL_EVENT_MOUSE_MOTION:
            if (!options_.fake_mouse) {
                mouse_ = app_.to_pixels({event.motion.x, event.motion.y});
            }
            break;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            if (!options_.fake_mouse) {
                mouse_ = glm::vec2(-1.0f);
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            if (event.wheel.y != 0.0f) {
                camera_.set_visible_height(
                    std::clamp(camera_.visible_height() * (event.wheel.y > 0.0f ? 0.9f : 1.0f / 0.9f), 4.0f, 60.0f));
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            const glm::vec2 pixel = app_.to_pixels({event.button.x, event.button.y});
            const auto ground = drawn_camera_.ground_point(pixel);
            if (!ground) {
                break;
            }
            if (event.button.button == SDL_BUTTON_RIGHT) {
                // Picking a creature: the nearest to the pointed ground (see the part 11 notes).
                const int picked = creature_near(*ground, 1.5f);
                if (picked >= 0) {
                    selected_ = picked;
                }
            } else if (event.button.button == SDL_BUTTON_LEFT && !creatures_.empty() &&
                       walkable({ground->x, ground->z})) {
                creatures_[static_cast<std::size_t>(selected_)].goal = glm::vec2(ground->x, ground->z);
            }
            break;
        }
        case SDL_EVENT_KEY_DOWN:
            if (event.key.repeat) {
                break;
            }
            if (event.key.key == SDLK_F) {
                follow_ = !follow_;
            } else if (event.key.key == SDLK_TAB && !creatures_.empty()) {
                selected_ = (selected_ + 1) % static_cast<int>(creatures_.size());
            } else if (event.key.key == SDLK_P) {
                camera_.set_projection(camera_.projection() == moteur::Projection::Perspective
                                           ? moteur::Projection::Orthographic
                                           : moteur::Projection::Perspective);
            }
            break;
        default:
            break;
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
    camera_.begin_update();
    for (Creature& creature : creatures_) {
        creature.previous = creature.position;
    }
    const bool frozen = options_.freeze_after_ticks > 0 && ticks_ > options_.freeze_after_ticks;
    if (frozen) {
        return;
    }
    const auto step = static_cast<float>(dt);
    move_creatures(step);
    if (follow_ && !creatures_.empty()) {
        const glm::vec2 p = creatures_[static_cast<std::size_t>(selected_)].position;
        camera_.follow({p.x, 0.0f, p.y}, step, 0.25f);
    } else if (!options_.no_input) {
        move_camera(step);
    }
}

void Demo3D::render(moteur::Renderer& renderer, double alpha) {
    ++frames_;
    if (!options_.capture_path.empty() && !capture_done_ && options_.freeze_after_ticks > 0 &&
        ticks_ > options_.freeze_after_ticks) {
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

    // Decor, floor and walls: their boxes were computed once.
    for (const StaticDraw& draw : statics_) {
        meshes.draw(*draw.mesh, draw.world, draw.material, draw.bounds);
    }

    // Creatures: a body and a head, between their last two positions.
    const moteur::Material skin = surface(linear(0.85f, 0.7f, 0.55f), 0.6f);
    for (std::size_t k = 0; k < creatures_.size(); ++k) {
        const Creature& creature = creatures_[k];
        const glm::vec2 p = moteur::interpolate(creature.previous, creature.position, blend);
        const glm::vec3 foot(p.x, 0.0f, p.y);
        meshes.draw(cube_, place(foot + glm::vec3(0.0f, 0.5f, 0.0f), {0.45f, 1.0f, 0.45f}), surface(creature.color, 0.7f));
        meshes.draw(sphere_, place(foot + glm::vec3(0.0f, 1.2f, 0.0f), glm::vec3(0.38f)), skin);
        if (static_cast<int>(k) == selected_) {
            meshes.draw(tile_, place(foot + glm::vec3(0.0f, 0.01f, 0.0f), glm::vec3(1.1f)), glowing({0.3f, 1.2f, 0.4f}));
            if (creature.goal) {
                meshes.draw(tile_, place({creature.goal->x, 0.012f, creature.goal->y}, glm::vec3(0.4f)),
                            glowing({1.2f, 1.0f, 0.2f}));
            }
        }
    }

    // Torches: every flame is drawn, but only the lights nearest to the camera are sent (the
    // engine takes at most 32), and among them the engine gives shadows to its budget.
    std::vector<std::size_t> order(torches_.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    const glm::vec3 focus = camera.target();
    const auto distance2 = [&](std::size_t i) {
        const glm::vec3 d = torches_[i].position - focus;
        return glm::dot(d, d);
    };
    const auto lit = std::min(order.size(), static_cast<std::size_t>(std::max(torch_lights_, 0)));
    std::partial_sort(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(lit), order.end(),
                      [&](std::size_t a, std::size_t b) { return distance2(a) < distance2(b) || (distance2(a) == distance2(b) && a < b); });
    for (std::size_t k = 0; k < lit; ++k) {
        const Torch& torch = torches_[order[k]];
        meshes.add_light({torch.position, torch.color, 5.0f, 7.0f, torch_shadows_});
    }
    moteur::BillboardOptions halo;
    halo.additive = true;
    for (const Torch& torch : torches_) {
        meshes.draw(sphere_, place(torch.position, glm::vec3(0.18f)), glowing(torch.color * 10.0f));
        halo.color = glm::vec4(torch.color * 1.5f, 1.0f);
        billboards.draw(glow_, torch.position, {1.0f, 1.0f}, halo);
    }

    // The cell under the mouse.
    hovered_cell_.reset();
    if (mouse_.x >= 0.0f && mouse_.y >= 0.0f) {
        if (const auto ground = camera.ground_point(mouse_)) {
            const glm::ivec2 cell(static_cast<int>(std::floor(ground->x)), static_cast<int>(std::floor(ground->z)));
            if (cell.x >= 0 && cell.y >= 0 && cell.x < options_.map_size && cell.y < options_.map_size) {
                hovered_cell_ = cell;
                meshes.draw(tile_, place({static_cast<float>(cell.x) + 0.5f, 0.005f, static_cast<float>(cell.y) + 0.5f}, glm::vec3(1.0f)),
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
        for (std::size_t k = 0; k < creatures_.size(); ++k) {
            const Creature& creature = creatures_[k];
            const glm::vec2 p = moteur::interpolate(creature.previous, creature.position, blend);
            const auto head = camera.world_to_screen({p.x, 1.6f, p.y});
            if (!head || head->x < -bar.x || head->y < -bar.y || head->x > view.x + bar.x || head->y > view.y + bar.y) {
                continue;
            }
            const glm::vec2 corner = glm::round(*head - glm::vec2(bar.x * 0.5f, bar.y));
            moteur::SpriteOptions frame;
            frame.tint = {0.0f, 0.0f, 0.0f, 0.7f};
            frame.depth = 1.0f;
            screen.draw(white_, corner - glm::vec2(ui), bar + glm::vec2(2.0f * ui), frame);
            moteur::SpriteOptions fill;
            fill.tint = glm::vec4(glm::mix(glm::vec3(0.85f, 0.15f, 0.1f), glm::vec3(0.2f, 0.8f, 0.25f), creature.health), 1.0f);
            if (static_cast<int>(k) == selected_) {
                fill.tint = {0.95f, 0.85f, 0.3f, 1.0f};  // the selected one stands out
            }
            fill.depth = 1.1f;
            screen.draw(white_, corner, {std::round(bar.x * creature.health), bar.y}, fill);
        }
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
                  options_.map_size, options_.map_size, creatures_.size(), statics_.size(), torches_.size());
    font_->draw(screen, text, corner, title);
    const moteur::RenderStats& s = last_stats_;
    std::snprintf(text, sizeof(text), "scène : %d / %d maillages, %zu triangles, %d draw calls ; ombre : %d / %d, %d draw calls",
                  s.meshes, s.meshes_submitted, s.triangles, s.mesh_draw_calls, s.shadow_casters, s.shadow_casters_submitted,
                  s.shadow_draw_calls);
    font_->draw(screen, text, corner + glm::vec2(0.0f, line), detail);
    std::snprintf(text, sizeof(text), "torches : %d ombrées, %d recalculées, %d draw calls ; billboards : %d ; draw calls en tout : %d",
                  s.point_shadow_lights, s.point_shadow_updates, s.point_shadow_draw_calls, s.billboards, s.draw_calls);
    font_->draw(screen, text, corner + glm::vec2(0.0f, 2.0f * line), detail);
    if (hovered_cell_) {
        std::snprintf(text, sizeof(text), "Case (%d, %d). Clic droit : choisir une créature, clic gauche : l'y envoyer, Tab : la suivante, F : la suivre (%s)",
                      hovered_cell_->x, hovered_cell_->y, follow_ ? "oui" : "non");
    } else {
        std::snprintf(text, sizeof(text), "Clic droit : choisir une créature, clic gauche : l'y envoyer, Tab : la suivante, F : la suivre (%s)",
                      follow_ ? "oui" : "non");
    }
    font_->draw(screen, text, corner + glm::vec2(0.0f, 3.0f * line), detail);
}

void Demo3D::draw_controls() {
    moteur::Renderer& renderer = app_.renderer();
    moteur::MeshRenderer& meshes = renderer.meshes();
    ImGui::PushItemWidth(200.0f);
    ImGui::SeparatorText("Caméra");
    ImGui::Checkbox("Suivre la créature choisie (F)", &follow_);
    if (!creatures_.empty()) {
        ImGui::Text("Créature choisie : n° %d (Tab : la suivante)", selected_);
    }
    ImGui::SeparatorText("Lumière");
    ImGui::SliderFloat("Soleil", &sun_intensity_, 0.0f, 10.0f, "%.1f");
    ImGui::SliderFloat("Hauteur du soleil (°)", &sun_elevation_, 5.0f, 90.0f, "%.0f");
    ImGui::SliderFloat("Direction du soleil (°)", &sun_yaw_, 0.0f, 360.0f, "%.0f");
    moteur::ShadowOptions shadows = meshes.shadows();
    bool changed = ImGui::Checkbox("Ombres du soleil", &shadows.enabled);
    ImGui::SliderInt("Torches éclairantes", &torch_lights_, 0, moteur::MeshRenderer::kMaxPointLights);
    ImGui::Checkbox("Ombres des torches", &torch_shadows_);
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
}
