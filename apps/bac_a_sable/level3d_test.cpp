#include "level3d_test.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>

#include "moteur/color.hpp"
#include "moteur/console.hpp"
#include "moteur/debug_lines.hpp"
#include "moteur/file_io.hpp"
#include "moteur/paths.hpp"
#include "moteur/level_collision.hpp"
#include "moteur/variables.hpp"
#include "moteur/renderer.hpp"

namespace {

constexpr float kTick = 1.0f / 30.0f;

glm::vec3 linear(float r, float g, float b) {
    return moteur::srgb_to_linear(glm::vec3(r, g, b));
}

moteur::Material surface(glm::vec3 color, float roughness, bool fades = false) {
    moteur::Material material;
    material.base_color = glm::vec4(color, 1.0f);
    material.roughness = roughness;
    material.fades = fades;  // the level: cut away above the hero, faded before it
    return material;
}

moteur::Aabb box(glm::vec3 low, glm::vec3 high) {
    moteur::Aabb b;
    b.add(low);
    b.add(high);
    return b;
}

double seconds_since(Uint64 start) {
    return static_cast<double>(SDL_GetPerformanceCounter() - start) / static_cast<double>(SDL_GetPerformanceFrequency());
}

// The triangles of one kind as a drawable mesh: flat shaded (each triangle its own vertices).
moteur::MeshData mesh_of(const moteur::CollisionMeshData& level, moteur::SurfaceKind kind) {
    moteur::MeshData mesh;
    for (std::size_t t = 0; t < level.triangle_count(); ++t) {
        if (level.kinds[t] != kind) {
            continue;
        }
        const glm::vec3 a = level.vertices[level.indices[t * 3]];
        const glm::vec3 b = level.vertices[level.indices[t * 3 + 1]];
        const glm::vec3 c = level.vertices[level.indices[t * 3 + 2]];
        const glm::vec3 normal = glm::normalize(glm::cross(b - a, c - a));
        // Texture coordinates from the dominant axis, one unit per metre.
        const auto uv = [&normal](glm::vec3 p) {
            const glm::vec3 n = glm::abs(normal);
            return n.y >= n.x && n.y >= n.z ? glm::vec2(p.x, p.z) : n.x >= n.z ? glm::vec2(p.z, p.y) : glm::vec2(p.x, p.y);
        };
        for (const glm::vec3 p : {a, b, c}) {
            moteur::Vertex3D v;
            v.position = p;
            v.normal = normal;
            v.uv = uv(p);
            mesh.indices.push_back(static_cast<std::uint32_t>(mesh.vertices.size()));
            mesh.vertices.push_back(v);
        }
    }
    moteur::compute_tangents(mesh);
    return mesh;
}

// The autopilot's tour: up the stairs, across the platform, off its edge, under the bridge, round
// to the foot of the ramp, up it and along the bridge, then back (straight lines: no navigation
// mesh yet, part 6).
const glm::vec3 kTour[] = {{-11, 0, 10},    {-7.5f, 2, 10}, {-3.5f, 2, 10},  {-1.5f, 0, 10},   {8, 0, 3},
                           {13, 0, 3},      {13, 0, -14},   {10.5f, 0, -14}, {10.5f, 2.5f, -4}, {10.5f, 2.7f, 7},
                           {10.5f, 2.7f, -1}, {10.5f, 0, -14}, {8, 0, -3},      {0, 0, 0}};

}  // namespace

Level3DTest::Level3DTest(moteur::Application& app, const Options& options, bool standalone)
    : app_(app), options_(options), standalone_(standalone) {
    moteur::Renderer& renderer = app.renderer();
    cube_ = moteur::make_asset(moteur::Mesh::create(renderer, moteur::make_cube(), "level3d.cube"));
    sky_ = moteur::Environment::create(renderer, moteur::make_sky(256, 128), "level3d.sky");

    moteur::Input& input = app.input();
    input.clear_actions();
    input.add_button("move_to");
    input.add_axis("move");
    input.add_axis("camera");
    for (const char* name : {"zoom_in", "zoom_out", "toggle_wall", "spawn", "spawn_big", "effect", "follow", "projection"}) {
        input.add_button(name);
    }
    input.load_bindings(moteur::read_text_file(app.assets().file_path("input/world.json")), "input/world.json");
    actions_ = {input.find("move_to"), input.find("move"),     input.find("camera"),      input.find("zoom_in"),
                input.find("zoom_out"), input.find("follow"), input.find("toggle_wall"), input.find("spawn")};
    debug_flags_ = moteur::WorldDebugFlags::declare(app.console().variables());

    moteur::ShadowOptions shadows;
    renderer.meshes().set_shadows(shadows);
    renderer.meshes().set_ids(true);  // the hover's outline (part 7)
    renderer.meshes().set_culling(true);
    renderer.set_render_scale(1.0f);
    renderer.set_exposure(1.0f);

    build_level();
    hero_ = spawn_character({0, 0, 0}, linear(0.85f, 0.8f, 0.3f), true);
    // Walkers on the bridge and under it, crossing each other at the same place of the plane.
    const glm::vec3 colors[2] = {linear(0.3f, 0.6f, 0.9f), linear(0.8f, 0.3f, 0.3f)};
    for (int i = 0; i < 4; ++i) {
        const float x = 10.2f + 0.2f * static_cast<float>(i);
        const entt::entity above = spawn_character({x, 2.7f, -1.0f + static_cast<float>(i) * 2.0f}, colors[0], false);
        world_.registry().emplace<Walker>(above, Walker{{x, 2.7f, -1.5f}, {x, 2.7f, 7.5f}, i % 2 == 0});
        const entt::entity below = spawn_character({x, 0.0f, 7.0f - static_cast<float>(i) * 2.0f}, colors[1], false);
        world_.registry().emplace<Walker>(below, Walker{{x, 0.0f, -1.5f}, {x, 0.0f, 7.5f}, i % 2 != 0});
    }
    if (options_.crowd > 0) {
        spawn_monsters({-10.0f, 0.0f, -15.0f}, options_.crowd);
    }
    camera_.set_angles(-45.0f, 50.0f);
    camera_.set_visible_height(18.0f);
    camera_.set_target({0.0f, 0.0f, 0.0f});
    camera_.begin_update();
}

void Level3DTest::build_level() {
    level_ = {};
    level_.add_box(box({-20, -1, -20}, {20, 0, 20}));                                  // ground
    level_.add_box(box({5, 0, -20}, {6, 3, -4}), moteur::SurfaceKind::Wall);           // a wall
    level_.add_box(box({-20, 0, 19}, {20, 3, 20}), moteur::SurfaceKind::Wall);         // the border walls
    level_.add_box(box({-20, 0, -20}, {20, 3, -19}), moteur::SurfaceKind::Wall);
    level_.add_box(box({-20, 0, -19}, {-19, 3, 19}), moteur::SurfaceKind::Wall);
    level_.add_box(box({19, 0, -19}, {20, 3, 19}), moteur::SurfaceKind::Wall);
    level_.add_stairs({-10, 0, 10}, 0, 10, 0.2f, 0.3f, 2.0f);                          // up to 2 m
    level_.add_box(box({-7, 0, 9}, {-3, 2, 11}));                                      // the platform (its edge at x -3)
    level_.add_box(box({9.5f, 2.5f, -2}, {11.5f, 2.7f, 8}));                          // the bridge deck, 2 m wide
    level_.add_box(box({9.3f, 0, 7.8f}, {9.5f, 3.3f, 8}), moteur::SurfaceKind::Wall);  // its far posts
    level_.add_box(box({11.5f, 0, 7.8f}, {11.7f, 3.3f, 8}), moteur::SurfaceKind::Wall);
    level_.add_ramp({10.5f, 0, -12}, 3, 6.0f, 2.5f, 2.0f);                            // up to the bridge (along +z)
    level_.add_box(box({9.5f, 0, -6}, {11.5f, 2.5f, -2}));                            // between the ramp and the deck
    level_.add_box(box({0, 0, 15}, {2, 0.6f, 17}), moteur::SurfaceKind::Obstacle);     // too high a step
    level_.add_ramp({-15, 0, -10}, 0, 3.4641f, 2.0f, 2.0f);                           // 30 degrees
    level_.add_ramp({-15, 0, -15}, 0, 1.1547f, 2.0f, 2.0f);                           // 60 degrees: a wall
    level_.add_box(box({14, 0, 14}, {16, 0.2f, 16}));                                  // a step under a low ceiling
    level_.add_box(box({14, 1.6f, 13}, {16, 3, 17}), moteur::SurfaceKind::Wall);

    build_collision();
    const Uint64 nav_start = SDL_GetPerformanceCounter();
    navmesh_.build(scene_, moteur::NavAgent{});
    const glm::vec3 entry(0.0f);
    navmesh_.prune(std::span(&entry, 1));  // the ground inside closed blocks is out of reach
    nav_build_ms_ = seconds_since(nav_start) * 1000.0;

    moteur::Renderer& renderer = app_.renderer();
    entt::registry& registry = world_.registry();
    const struct {
        moteur::SurfaceKind kind;
        glm::vec3 color;
    } looks[] = {{moteur::SurfaceKind::Walkable, linear(0.55f, 0.53f, 0.5f)},
                 {moteur::SurfaceKind::Wall, linear(0.35f, 0.33f, 0.36f)},
                 {moteur::SurfaceKind::Obstacle, linear(0.5f, 0.35f, 0.2f)}};
    for (const auto& look : looks) {
        moteur::MeshData mesh = mesh_of(level_, look.kind);
        if (mesh.indices.empty()) {
            continue;
        }
        const entt::entity entity = registry.create();
        registry.emplace<moteur::Transform>(entity);
        registry.emplace<moteur::MeshComponent>(
            entity, moteur::make_asset(moteur::Mesh::create(renderer, mesh, "level3d.level")), surface(look.color, 0.8f, true));
    }
}

void Level3DTest::build_collision() {
    const Uint64 start = SDL_GetPerformanceCounter();
    scene_.clear();
    scene_.add(level_, glm::mat4(1.0f), 1);
    moteur::CollisionMeshData crates;
    for (const Crate& crate : crates_) {
        crates.add_box(crate.box, moteur::SurfaceKind::Obstacle, moteur::SurfaceKind::Obstacle);
    }
    scene_.add(crates, glm::mat4(1.0f), 2);
    scene_.build();
    build_ms_ = seconds_since(start) * 1000.0;
}

void Level3DTest::toggle_crate(glm::vec3 at) {
    entt::registry& registry = world_.registry();
    for (std::size_t i = 0; i < crates_.size(); ++i) {
        const moteur::Aabb& b = crates_[i].box;
        if (at.x >= b.min.x && at.x <= b.max.x && at.z >= b.min.z && at.z <= b.max.z) {
            const Crate gone = crates_[i];
            crates_.erase(crates_.begin() + static_cast<std::ptrdiff_t>(i));
            registry.destroy(gone.entity);
            build_collision();
            const Uint64 start = SDL_GetPerformanceCounter();
            navmesh_.remove_obstacle(scene_, gone.obstacle);
            nav_rebuild_ms_ = seconds_since(start) * 1000.0;
            chase_target_ = glm::vec3(1e9f);  // the field again
            return;
        }
    }
    Crate crate;
    crate.box = box({at.x - 0.6f, at.y, at.z - 0.6f}, {at.x + 0.6f, at.y + 1.0f, at.z + 0.6f});
    crate.entity = registry.create();
    moteur::Transform& t = registry.emplace<moteur::Transform>(crate.entity);
    t.position = crate.box.center();
    t.scale = crate.box.size();
    registry.emplace<moteur::MeshComponent>(crate.entity, cube_, surface(linear(0.55f, 0.38f, 0.2f), 0.7f));
    crates_.push_back(crate);
    build_collision();
    const Uint64 start = SDL_GetPerformanceCounter();
    crates_.back().obstacle = navmesh_.add_obstacle(scene_, crate.box);
    nav_rebuild_ms_ = seconds_since(start) * 1000.0;
    chase_target_ = glm::vec3(1e9f);
}

void Level3DTest::spawn_monsters(glm::vec3 around, int count) {
    entt::registry& registry = world_.registry();
    int made = 0;
    for (int ring = 0; made < count; ++ring) {
        const int on_ring = std::max(1, ring * 6);
        for (int k = 0; k < on_ring && made < count; ++k) {
            // Points on rings 0.8 m apart, without trigonometry: on the sides of a hexagon.
            const float r = static_cast<float>(ring) * 0.8f;
            const float s = static_cast<float>(k) / static_cast<float>(on_ring) * 6.0f;
            const int side = static_cast<int>(s);
            const float f = s - static_cast<float>(side);
            const glm::vec2 corners[7] = {{1, 0}, {0.5f, 0.866f}, {-0.5f, 0.866f}, {-1, 0}, {-0.5f, -0.866f}, {0.5f, -0.866f}, {1, 0}};
            const glm::vec2 p = (corners[side] + (corners[side + 1] - corners[side]) * f) * r;
            glm::vec3 at = around + glm::vec3(p.x, 0.0f, p.y);
            if (const auto on_mesh = navmesh_.nearest_point(at, {0.5f, 2.0f, 0.5f})) {
                at = *on_mesh;
            } else {
                continue;
            }
            const entt::entity monster = spawn_character(at, linear(0.75f, 0.2f, 0.15f), false);
            moteur::Mover& mover = registry.get<moteur::Mover>(monster);
            mover.speed = 3.0f;
            mover.avoid = true;
            mover.follow_field(1.0f);
            registry.get<moteur::Collider>(monster).radius = 0.3f;
            registry.emplace<Monster>(monster);
            ++made;
        }
        if (ring > 200) {
            break;
        }
    }
}

entt::entity Level3DTest::spawn_character(glm::vec3 at, glm::vec3 color, bool hero) {
    entt::registry& registry = world_.registry();
    const entt::entity entity = registry.create();
    moteur::Transform& transform = registry.emplace<moteur::Transform>(entity);
    transform.position = at;
    registry.emplace<moteur::PreviousTransform>(entity, transform);
    moteur::Collider& collider = registry.emplace<moteur::Collider>(entity);
    collider.radius = 0.35f;
    collider.height = 1.8f;
    moteur::Mover& mover = registry.emplace<moteur::Mover>(entity);
    mover.speed = hero ? 4.5f : 2.0f;
    if (!hero) {
        moteur::Selectable& selectable = registry.emplace<moteur::Selectable>(entity);
        selectable.radius = 0.45f;  // a little more than the collider: easier to click
        selectable.height = 1.9f;
    }
    // The body: a column on the entity, drawn from its feet.
    const entt::entity body = registry.create();
    moteur::Transform& shape = registry.emplace<moteur::Transform>(body);
    shape.position = {0.0f, 0.9f, 0.0f};
    shape.scale = {0.6f, 1.8f, 0.6f};
    registry.emplace<moteur::PreviousTransform>(body, shape);
    registry.emplace<moteur::Parent>(body, entity);
    registry.emplace<moteur::MeshComponent>(body, cube_, surface(color, 0.6f));
    return entity;
}

void Level3DTest::apply_input(float dt) {
    const moteur::Input& input = app_.input();
    const int zoom = input.presses(actions_.zoom_out) - input.presses(actions_.zoom_in);
    if (zoom != 0) {
        camera_.set_visible_height(
            std::clamp(camera_.visible_height() * std::pow(1.0f / 0.9f, static_cast<float>(zoom)), 4.0f, 80.0f));
    }
    if (input.pressed(actions_.follow)) {
        follow_camera_ = !follow_camera_;
    }
    const glm::vec3 forward = camera_.forward();
    const glm::vec3 ahead = glm::normalize(glm::vec3(forward.x, 0.0f, forward.z));
    const glm::vec3 right = glm::cross(ahead, glm::vec3(0.0f, 1.0f, 0.0f));
    if (const glm::vec2 direction = input.axis(actions_.camera); direction != glm::vec2(0.0f)) {
        follow_camera_ = false;
        camera_.set_target(camera_.target() + (right * direction.x + ahead * direction.y) * (camera_.visible_height() * dt));
    }

    // What the pointer is over, for the logic: rays against the selectable shapes and the level
    // (a monster first, else the ground put on the navigation mesh).
    pointed_.reset();
    const glm::vec2 pointer = input.pointer();
    const bool over_panel = !standalone_ && ImGui::GetIO().WantCaptureMouse;
    if (pointer.x >= 0.0f && pointer.y >= 0.0f && !over_panel) {
        pointed_ = moteur::pick(world_.registry(), camera_.screen_ray(pointer), &scene_, &navmesh_);
    }
    moteur::Mover& hero = world_.registry().get<moteur::Mover>(hero_);
    if (options_.autopilot) {
        return;
    }
    if (pointed_ && input.pressed(actions_.move_to)) {
        // A monster: to it (its feet); the ground: there.
        hero.go_to(pointed_->entity != entt::null ? world_.registry().get<moteur::Transform>(pointed_->entity).position
                                                  : pointed_->point);
    }
    if (pointed_ && input.pressed(actions_.toggle_wall)) {
        toggle_crate(pointed_->point);
    }
    if (pointed_ && input.pressed(actions_.spawn)) {
        spawn_monsters(pointed_->point, 12);
    }
    if (const glm::vec2 direction = input.axis(actions_.move); direction != glm::vec2(0.0f)) {
        const glm::vec3 wanted = right * direction.x + ahead * direction.y;
        hero.move({wanted.x, wanted.z});
    } else if (hero.mode == moteur::MoveMode::Direct) {
        hero.stop();
    }
}

void Level3DTest::think() {
    entt::registry& registry = world_.registry();
    for (const auto& [entity, walker, mover, transform] : registry.view<Walker, moteur::Mover, moteur::Transform>().each()) {
        const glm::vec3 target = walker.to_b ? walker.b : walker.a;
        const glm::vec2 to(target.x - transform.position.x, target.z - transform.position.z);
        if (glm::dot(to, to) < 0.04f) {
            walker.to_b = !walker.to_b;
        }
        const glm::vec3 next = walker.to_b ? walker.b : walker.a;
        mover.move({next.x - transform.position.x, next.z - transform.position.z});
    }
    if (options_.autopilot) {
        // The next point of the tour once there (or stuck: then the one after).
        moteur::Mover& hero = registry.get<moteur::Mover>(hero_);
        if (hero.mode == moteur::MoveMode::Stop || hero.state == moteur::MoveState::Blocked) {
            const glm::vec3 p = registry.get<moteur::Transform>(hero_).position;
            SDL_Log("Level 3D: tour step %zu %s at (%.2f, %.2f, %.2f)", tour_,
                    hero.state == moteur::MoveState::Blocked ? "blocked" : "reached", p.x, p.y, p.z);
            hero.go_to(kTour[tour_ % std::size(kTour)]);
            ++tour_;
        }
    }
}

void Level3DTest::update(double dt) {
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
    const bool frozen = options_.freeze_after_ticks > 0 && ticks_ > options_.freeze_after_ticks;
    if (frozen) {
        return;
    }
    const auto step = static_cast<float>(dt);
    apply_input(step);
    think();
    movement_stats_ = {};
    // The chase field again when the hero has moved a metre (or the mesh changed).
    const glm::vec3 hero = world_.registry().get<moteur::Transform>(hero_).position;
    Uint64 start = SDL_GetPerformanceCounter();
    if (glm::length(hero - chase_target_) > 1.0f || chase_.version() != navmesh_.version()) {
        chase_.compute(navmesh_, hero, 80.0f);
        chase_target_ = hero;
        field_ms_ = seconds_since(start) * 1000.0;
    }
    start = SDL_GetPerformanceCounter();
    const moteur::NavMesh* const meshes[1] = {&navmesh_};
    moteur::plan_paths(world_.registry(), meshes, 8, movement_stats_);
    paths_ms_ = paths_ms_ * 0.9 + seconds_since(start) * 1000.0 * 0.1;
    const moteur::NavFlowField* const fields[1] = {&chase_};
    moteur::NavSources nav;
    nav.meshes = meshes;
    nav.fields = fields;
    nav.neighbours = &hash_;
    start = SDL_GetPerformanceCounter();
    moteur::move_movers(world_.registry(), scene_, step, nav, movement_stats_, controller_);
    move_ms_ = move_ms_ * 0.9 + seconds_since(start) * 1000.0 * 0.1;
    start = SDL_GetPerformanceCounter();
    collision_stats_ = moteur::separate_colliders(world_.registry(), scene_, hash_, {}, controller_);
    moteur::finish_movers(world_.registry(), step);
    collide_ms_ = collide_ms_ * 0.9 + seconds_since(start) * 1000.0 * 0.1;
    if (follow_camera_) {
        camera_.follow(world_.registry().get<moteur::Transform>(hero_).position, step, 0.2f);
    }
}

void Level3DTest::render(moteur::Renderer& renderer, double alpha) {
    const bool frozen = options_.freeze_after_ticks > 0 && ticks_ > options_.freeze_after_ticks;
    if (!options_.capture_path.empty() && !capture_done_ && frozen) {
        renderer.request_capture(options_.capture_path);
        capture_done_ = true;
    }
    renderer.set_clear_color(0.03f, 0.03f, 0.04f);
    // The hover, by the id buffer (a frame or two late: display only): outlined. The autopilot aims
    // at a walker instead of the pointer, and says whether the id buffer finds it there.
    glm::vec2 pointer = app_.input().pointer();
    entt::entity aimed = entt::null;
    if (options_.autopilot) {
        for (const auto& [entity, walker] : world_.registry().view<Walker>().each()) {
            aimed = entity;
        }
        pointer = glm::vec2(-1.0f);
        if (aimed != entt::null) {
            camera_.set_viewport({static_cast<float>(renderer.width()), static_cast<float>(renderer.height())});
            if (const auto on_screen = camera_.interpolated(alpha).world_to_screen(
                    world_.world_position(aimed, static_cast<float>(alpha)) + glm::vec3(0.0f, 1.0f, 0.0f))) {
                pointer = *on_screen;
            }
        }
    }
    if (pointer.x >= 0.0f && pointer.y >= 0.0f) {
        renderer.request_pick(pointer);
    }
    hovered_ = world_.entity_of_pick(renderer.picked_id());
    if (options_.autopilot && ticks_ % 60 == 0 && aimed != entt::null) {
        SDL_Log("Level 3D: id buffer at the aimed walker: %s", hovered_ != entt::null ? "found a character" : "nothing");
    }
    renderer.set_outline(hovered_ != entt::null ? world_.pick_id_of(hovered_) : 0u);
    camera_.set_viewport({static_cast<float>(renderer.width()), static_cast<float>(renderer.height())});
    const moteur::Camera3D camera = camera_.interpolated(alpha);
    const auto blend = static_cast<float>(alpha);
    moteur::MeshRenderer& meshes = renderer.meshes();
    meshes.set_camera(camera.view_projection(), camera.position());
    renderer.billboards().set_camera(camera);
    meshes.set_sun(glm::normalize(glm::vec3(0.4f, 0.8f, 0.3f)), linear(1.0f, 0.93f, 0.8f), 2.5f);
    meshes.set_environment(&*sky_, 0.8f);
    // The level fades before the hero, and what is more than 2.2 m above it is cut away (the deck).
    moteur::Cutout cutout;
    cutout.enabled = true;
    cutout.focus = world_.world_position(hero_, blend) + glm::vec3(0.0f, 1.0f, 0.0f);
    cutout.ceiling = 1.2f;
    cutout.ceiling_radius = 6.0f;
    meshes.set_cutout(cutout);
    moteur::CollectOptions collect;
    collect.view = camera.frustum();
    world_.submit(renderer, blend, collect);

    moteur::DebugLineBuffer& lines = renderer.debug_lines().lines();
    moteur::WorldDebugSources sources;
    sources.world = &world_;
    sources.scene = &scene_;
    sources.navmesh = &navmesh_;
    sources.highlight = hero_;
    moteur::draw_world_debug(lines, debug_flags_, sources, camera.target(), camera.visible_height() * 1.1f, blend);
    if (pointed_ && pointed_->entity == entt::null) {
        lines.circle(pointed_->point + glm::vec3(0.0f, 0.02f, 0.0f), {0, 1, 0}, 0.3f, {1.0f, 0.9f, 0.2f, 1.0f}, 24, true);
    }
}

void Level3DTest::draw_controls() {
    const entt::registry& registry = world_.registry();
    const moteur::Mover& hero = registry.get<moteur::Mover>(hero_);
    const glm::vec3 p = registry.get<moteur::Transform>(hero_).position;
    ImGui::Text("Level: %zu triangles, %zu nodes, built in %.2f ms", scene_.triangles().size(), scene_.node_count(), build_ms_);
    ImGui::Text("Navigation: %d tiles, %d polygons, built in %.1f ms; last rebuild %.1f ms", navmesh_.tile_count(),
                navmesh_.polygon_count(), nav_build_ms_, nav_rebuild_ms_);
    ImGui::Text("Chase field %.2f ms, paths %.3f ms, monsters %zu", field_ms_, paths_ms_,
                static_cast<std::size_t>(registry.view<Monster>().size()));
    ImGui::Text("Hero: (%.2f, %.2f, %.2f) %s, vertical %.2f m/s", p.x, p.y, p.z, hero.grounded ? "on the ground" : "falling",
                hero.vertical_speed);
    ImGui::Text("Movement %.3f ms, collisions %.3f ms (%d pairs, %d overlaps)", move_ms_, collide_ms_,
                collision_stats_.pairs_tested, collision_stats_.overlaps);
    if (pointed_) {
        ImGui::Text("Pointer (rays): (%.2f, %.2f, %.2f), %s", pointed_->point.x, pointed_->point.y, pointed_->point.z,
                    std::string(moteur::enum_name(pointed_->kind)).c_str());
    }
    ImGui::Text("Hovered (id buffer): %s", hovered_ != entt::null ? "a character" : "nothing");
    for (moteur::Variable* flag : {debug_flags_.collision, debug_flags_.navmesh, debug_flags_.paths}) {
        bool value = flag != nullptr && flag->as_bool();
        if (flag != nullptr && ImGui::Checkbox(flag->name().c_str(), &value)) {
            flag->set_bool(value);
        }
    }
    ImGui::Checkbox("Autopilot", &options_.autopilot);
    ImGui::SliderFloat("Step height", &controller_.step_height, 0.0f, 1.0f, "%.2f m");
    ImGui::SliderFloat("Steepest ground (normal y)", &controller_.min_ground_normal_y, 0.3f, 1.0f, "%.3f");
    ImGui::TextWrapped("Click: go to the pointed point (a path on the navigation mesh). Keys or stick: walk. "
                       "M: monsters under the pointer. Right click: put or take away a crate. Arrows: camera. "
                       "F: follow the hero.");
}
