#include "level_stream_test.hpp"

#include <imgui.h>

#include <cmath>

#include "moteur/collision.hpp"
#include "moteur/color.hpp"
#include "moteur/level.hpp"
#include "moteur/level_bake.hpp"
#include "moteur/renderer.hpp"

namespace {

constexpr float kSize = 512.0f;  // metres

double seconds_since(Uint64 start) {
    return static_cast<double>(SDL_GetPerformanceCounter() - start) / static_cast<double>(SDL_GetPerformanceFrequency());
}

// The generated level: a ground of 8 m tiles, and in each tile (but along the crossing, z near the
// middle) a wall, a pillar or crates, chosen by a small generator with a fixed seed.
moteur::LevelDocument generated_level() {
    moteur::LevelDocument level;
    level.description = "Generated for the streaming test: 512 x 512 m of blockout";
    level.chunk_size = 32.0f;
    level.ground.cell_size = 8.0f;
    level.ground.width = static_cast<int>(kSize / 8.0f);
    level.ground.height = static_cast<int>(kSize / 8.0f);
    level.ground.prefabs = {"prefabs/blockout/floor_8m.prefab.json"};
    level.ground.cells.assign(static_cast<std::size_t>(level.ground.width * level.ground.height), 1);
    std::uint32_t state = 2024u;
    const auto next = [&state] {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) / 16777216.0f;
    };
    for (int z = 0; z < level.ground.height; ++z) {
        for (int x = 0; x < level.ground.width; ++x) {
            const float cx = static_cast<float>(x) * 8.0f + 4.0f;
            const float cz = static_cast<float>(z) * 8.0f + 4.0f;
            if (std::fabs(cz - kSize * 0.5f) < 8.0f) {
                continue;  // the crossing stays clear
            }
            const float roll = next();
            moteur::LevelInstance piece;
            piece.transform.position = {cx + (next() - 0.5f) * 4.0f, 0.0f, cz + (next() - 0.5f) * 4.0f};
            if (roll < 0.35f) {
                piece.prefab = "prefabs/blockout/wall.prefab.json";
                piece.transform.scale = {2.0f + next() * 4.0f, 1.0f, 1.0f};
                level.add(piece);
            } else if (roll < 0.6f) {
                piece.prefab = "prefabs/blockout/pillar.prefab.json";
                level.add(piece);
            } else {
                const int crates = 2 + static_cast<int>(next() * 3.0f);
                for (int k = 0; k < crates; ++k) {
                    moteur::LevelInstance crate;
                    crate.prefab = "prefabs/blockout/crate.prefab.json";
                    crate.transform.position = piece.transform.position + glm::vec3(static_cast<float>(k) * 0.9f, 0.0f, (next() - 0.5f));
                    level.add(crate);
                }
            }
        }
    }
    level.points.push_back({"start", {8.0f, 0.0f, kSize * 0.5f}, glm::quat(1, 0, 0, 0)});
    return level;
}

}  // namespace

LevelStreamTest::LevelStreamTest(moteur::Application& app, const Options& options, bool standalone)
    : app_(app), options_(options), standalone_(standalone) {
    sky_ = moteur::Environment::create(app.renderer(), moteur::make_sky(256, 128), "stream.sky");
    moteur::ShadowOptions shadows;
    app.renderer().meshes().set_shadows(shadows);
    app.renderer().meshes().set_culling(true);
    moteur::StreamingSettings settings;
    settings.load_radius = 48.0f;
    settings.unload_radius = 80.0f;
    settings.max_chunks = 30;  // 30 of the 256 chunks at most: the whole level never is in memory
    streamer_ = std::make_unique<moteur::LevelStreamer>(app.assets(), world_, settings);

    const Uint64 start = SDL_GetPerformanceCounter();
    if (options_.level.empty()) {
        const moteur::LevelDocument level = generated_level();
        moteur::LevelBakeStats stats;
        moteur::BakedLevel baked = moteur::bake_level(level, moteur::asset_level_sources(app.assets()), &stats);
        SDL_Log("Level streaming: generated level baked: %d instances, %d triangles, %d chunks, %d navigation tiles in %.0f ms",
                stats.instances, stats.collision_triangles, stats.chunks, stats.nav_tiles, stats.milliseconds);
        streamer_->open(std::move(baked), "generated.level.json");
    } else {
        streamer_->open_any(options_.level);
    }
    bake_ms_ = seconds_since(start) * 1000.0;
    const moteur::LevelPoint* entry = streamer_->header().point("start");
    start_ = entry != nullptr ? entry->position : streamer_->header().bounds.center();
    streamer_->set_focus(start_);
    streamer_->finish(60.0);  // the first chunks before the first frame (a loading screen's work)

    entt::registry& registry = world_.registry();
    hero_ = registry.create();
    moteur::Transform& transform = registry.emplace<moteur::Transform>(hero_);
    transform.position = start_ + glm::vec3(0.0f, 0.2f, 0.0f);
    registry.emplace<moteur::PreviousTransform>(hero_, transform);
    registry.emplace<moteur::Collider>(hero_);
    moteur::Mover& mover = registry.emplace<moteur::Mover>(hero_);
    mover.speed = options_.speed;
    mover.grounded = false;
    camera_.set_angles(-90.0f, 55.0f);
    camera_.set_visible_height(40.0f);
    camera_.set_target(start_);
    camera_.begin_update();
}

LevelStreamTest::~LevelStreamTest() {
    SDL_Log("Level streaming: %.1f s, setup %.0f ms; streaming worst %.2f ms per update; at most %d chunks loaded, %d "
            "instances; the hero waited %ld ticks for its chunk",
            elapsed_, bake_ms_, streamer_->stats().worst_update_ms, max_loaded_, max_instances_, waiting_ticks_);
}

void LevelStreamTest::update(double dt) {
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
    entt::registry& registry = world_.registry();
    const glm::vec3 position = registry.get<moteur::Transform>(hero_).position;
    streamer_->set_focus(position);
    streamer_->update();
    max_loaded_ = std::max(max_loaded_, streamer_->stats().chunks_loaded);
    max_instances_ = std::max(max_instances_, streamer_->stats().instances);

    // Across, along +x; back to the start at the far side. It waits while the chunk under it is
    // not loaded (the logic only acts where the level is there).
    moteur::Mover& mover = registry.get<moteur::Mover>(hero_);
    const bool there = streamer_->loaded(streamer_->header().chunk_of(position)) && streamer_->collision().built();
    if (!there) {
        mover.stop();
        ++waiting_ticks_;
        return;
    }
    if (position.x > kSize - 16.0f && options_.level.empty()) {
        registry.patch<moteur::Transform>(hero_, [&](moteur::Transform& t) { t.position = start_ + glm::vec3(0, 0.2f, 0); });
        registry.get<moteur::PreviousTransform>(hero_).value = registry.get<moteur::Transform>(hero_);
        return;
    }
    mover.move({1.0f, 0.0f});
    moteur::MovementStats stats;
    moteur::NavSources nav;
    nav.meshes = streamer_->meshes();
    moteur::move_movers(registry, streamer_->collision(), step, nav, stats);
    moteur::separate_colliders(registry, streamer_->collision(), hash_);
    moteur::finish_movers(registry, step);
    camera_.follow(registry.get<moteur::Transform>(hero_).position, step, 0.2f);
}

void LevelStreamTest::render(moteur::Renderer& renderer, double alpha) {
    const bool frozen = options_.freeze_after_ticks > 0 && ticks_ > options_.freeze_after_ticks;
    if (!options_.capture_path.empty() && !capture_done_ && frozen) {
        renderer.request_capture(options_.capture_path);
        capture_done_ = true;
    }
    renderer.set_clear_color(0.05f, 0.06f, 0.08f);
    camera_.set_viewport({static_cast<float>(renderer.width()), static_cast<float>(renderer.height())});
    const moteur::Camera3D camera = camera_.interpolated(alpha);
    moteur::MeshRenderer& meshes = renderer.meshes();
    meshes.set_camera(camera.view_projection(), camera.position());
    renderer.billboards().set_camera(camera);
    meshes.set_sun(glm::normalize(glm::vec3(0.3f, 0.9f, 0.2f)), moteur::srgb_to_linear(glm::vec3(1.0f, 0.95f, 0.85f)), 2.5f);
    meshes.set_environment(&*sky_, 0.8f);
    moteur::CollectOptions collect;
    collect.view = camera.frustum();
    world_.submit(renderer, static_cast<float>(alpha), collect);
}

void LevelStreamTest::draw_controls() {
    const moteur::StreamingStats& s = streamer_->stats();
    const glm::vec3 p = world_.registry().get<moteur::Transform>(hero_).position;
    ImGui::Text("Hero (%.1f, %.1f, %.1f), chunk (%d, %d)", p.x, p.y, p.z, streamer_->header().chunk_of(p).x,
                streamer_->header().chunk_of(p).y);
    ImGui::Text("Chunks: %d loaded, %d on their way (of %d x %d); %d instances", s.chunks_loaded, s.chunks_pending,
                streamer_->header().chunks_x, streamer_->header().chunks_z, s.instances);
    ImGui::Text("Streaming: %.2f ms this update, %.2f ms at worst; collision %d triangles", s.last_update_ms, s.worst_update_ms,
                s.collision_triangles);
    ImGui::Text("Setup (bake and first chunks) %.0f ms; the hero waited %ld ticks", bake_ms_, waiting_ticks_);
}
