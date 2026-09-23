#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "moteur/animation.hpp"
#include "moteur/application.hpp"
#include "moteur/camera.hpp"
#include "moteur/camera3d.hpp"
#include "moteur/color.hpp"
#include "moteur/environment.hpp"
#include "moteur/font.hpp"
#include "moteur/image.hpp"
#include "moteur/iso.hpp"
#include "moteur/mesh.hpp"
#include "moteur/mesh_renderer.hpp"
#include "moteur/model.hpp"
#include "moteur/paths.hpp"
#include "moteur/sprite_renderer.hpp"
#include "moteur/texture_atlas.hpp"
#include "moteur/tilemap.hpp"
#include "moteur/version.hpp"

namespace {

constexpr float kSpriteScale = 8.0f;  // the main sprite: one texel covers 8x8 screen pixels
constexpr float kMoverSize = 32.0f;   // the stress-test sprites are drawn at their native size

constexpr float kTileWidth = 64.0f;   // isometric tiles: 2:1 diamonds
constexpr float kTileHeight = 32.0f;
constexpr float kCameraSpeed = 600.0f;  // screen pixels per second, whatever the zoom
constexpr float kMaxZoom = 8.0f;

constexpr float kFontPixelHeight = 20.0f;  // rasterized once at this physical pixel size

// A color picked by eye (as on a screen, sRGB) turned into the linear value the 3D lighting works with.
glm::vec4 screen_color(float r, float g, float b) {
    return glm::vec4(moteur::srgb_to_linear(glm::vec3(r, g, b)), 1.0f);
}

// Small deterministic random generator, so that a stress test is the same on every run and every OS.
class Random {
public:
    explicit Random(std::uint32_t seed) : state_(seed) {}
    // Uniform in [0, 1).
    float next() {
        state_ = state_ * 1664525u + 1013904223u;
        return static_cast<float>(state_ >> 8) / 16777216.0f;
    }

private:
    std::uint32_t state_;
};

struct Mover {
    glm::vec2 position;
    glm::vec2 velocity;  // pixels per second
    glm::vec4 tint;
};

// What the "À propos" window lists, read from assets/credits.json: the third-party libraries and
// fonts with their licenses, and the authors of the assets. Paths are relative to the executable.
struct Credits {
    struct Entry {
        std::string name;
        std::string version;
        std::string license;
        std::string copyright;
        std::string authors;
        std::string source;
        std::string url;
        std::string license_file;  // full license text, shown on demand
        std::string file;          // an asset: shown as present or missing
    };
    std::vector<Entry> libraries;
    std::vector<Entry> fonts;
    std::vector<Entry> models;
    std::string error;  // why the file could not be read, if it could not

    static Credits load(const std::string& path) {
        Credits credits;
        try {
            const nlohmann::json doc = nlohmann::json::parse(moteur::read_text_file(path));
            const auto read = [](const nlohmann::json& list) {
                std::vector<Entry> entries;
                for (const nlohmann::json& item : list) {
                    Entry entry;
                    entry.name = item.value("name", "");
                    entry.version = item.value("version", "");
                    entry.license = item.value("license", "");
                    entry.copyright = item.value("copyright", "");
                    entry.authors = item.value("authors", "");
                    entry.source = item.value("source", "");
                    entry.url = item.value("url", "");
                    entry.license_file = item.value("license_file", "");
                    entry.file = item.value("file", "");
                    entries.push_back(std::move(entry));
                }
                return entries;
            };
            credits.libraries = read(doc.value("libraries", nlohmann::json::array()));
            credits.fonts = read(doc.value("fonts", nlohmann::json::array()));
            credits.models = read(doc.value("models", nlohmann::json::array()));
        } catch (const std::exception& e) {
            credits.error = "Crédits illisibles (" + path + ") : " + e.what();
        }
        return credits;
    }
};

// A glTF model of assets/models/, shown by the 3D scene, or why it could not be loaded.
struct ShownModel {
    std::string file;
    std::optional<moteur::Model> model;
    std::string error;
    double load_ms = 0.0;
};

// A demo-scene inhabitant: moves over the tile grid in one of 8 directions, in tile units.
struct Creature {
    glm::vec2 position;  // tile space, fractional
    glm::vec2 velocity;  // tiles per second
    glm::vec4 tint;
    moteur::AnimationPlayer walk;
};

// One test scene of the engine, chosen by its Options: the sprite stress test, the isometric map,
// the atlas, the text, or the demo. Created when the test starts and destroyed when it stops.
class TestScene final : public moteur::Game {
public:
    struct Options {
        double run_seconds = 0.0;  // > 0 makes the program quit by itself (used for smoke tests)
        bool still = false;        // keeps the main sprite motionless in the middle of the window
        int movers = 0;            // extra sprites bouncing around, for the stress test
        bool batching = true;      // false: one draw call per sprite, to compare
        bool depth = false;        // sprites are sorted by their height on the window
        long freeze_after_ticks = 0;  // > 0 stops all motion after this many logic ticks

        // Atlas scene: every sprite of the test atlas, and one character placed in four ways.
        bool atlas = false;

        // Text scene: a French sentence, a wrapped paragraph, aligned lines, a live FPS counter.
        bool text = false;

        // 3D scene (milestone 3): a floor, cubes, a sphere and pillars, seen through a Camera3D
        // whose projection can be switched, to compare orthographic and perspective.
        bool render3d = false;
        bool orthographic = false;  // start with the orthographic projection instead of the chosen perspective
        float view_height = 0.0f;   // > 0: metres of world visible at the target (--camera X Z sets the target)
        float render_scale = 1.0f;  // fraction of the window's pixels the 3D is rendered at

        // Isometric scene: a map of tiles seen through a camera.
        bool iso = false;
        int map_size = 60;         // the map has map_size x map_size tiles
        bool interleave = false;   // true: alternate the two tile textures tile by tile (breaks batching)
        bool no_input = false;     // true: ignore the real keyboard and mouse (except Escape), for reproducible runs
        float zoom = 1.0f;
        bool has_camera = false;   // true: --camera gives the world position at the center of the window
        glm::vec2 camera = {0.0f, 0.0f};
        bool fake_mouse = false;   // true: the mouse is at fake_mouse_position (window pixels), events are ignored
        glm::vec2 fake_mouse_position = {-1.0f, -1.0f};

        // Demo scene: everything at once (tilemap, moving creatures, camera, stats overlay).
        bool demo = false;
        std::uint32_t seed = 12345;     // creature placement and movement
        std::string capture_path;       // non-empty: write a PNG once freeze_after_ticks is reached
    };

    // standalone: the scene is the whole program (launched from the command line), so Escape and
    // --run-seconds quit it. Otherwise they only ask the menu to stop the test (stop_requested()).
    TestScene(moteur::Application& app, const Options& options, bool standalone)
        : app_(app),
          texture_(app.renderer().create_texture(moteur::load_image(moteur::asset_path("sprite.png")))),
          options_(options),
          standalone_(standalone),
          iso_(kTileWidth, kTileHeight) {
        app.renderer().sprites().set_batching(options.batching);

        if (options.render3d) {
            font_ = moteur::Font::load(app.renderer(), moteur::asset_path("fonts/Inter-Regular.ttf"), kFontPixelHeight);
            cube_mesh_ = moteur::Mesh::create(app.renderer(), moteur::make_cube(), "cube");
            tile_mesh_ = moteur::Mesh::create(app.renderer(), moteur::make_plane(), "tile");
            sphere_mesh_ = moteur::Mesh::create(app.renderer(), moteur::make_sphere(0.5f, 32, 16), "sphere");
            camera3d_.set_projection(options.orthographic ? moteur::Projection::Orthographic
                                                          : moteur::Projection::Perspective);
            if (options.has_camera) {
                camera3d_.set_target({options.camera.x, 0.0f, options.camera.y});  // X, Z on the ground
            }
            if (options.view_height > 0.0f) {
                camera3d_.set_visible_height(options.view_height);
            }
            app.renderer().set_render_scale(options.render_scale);
            load_models(app.renderer());
            load_environments(app.renderer());
            return;
        }

        if (options.text) {
            font_ = moteur::Font::load(app.renderer(), moteur::asset_path("fonts/Inter-Regular.ttf"), kFontPixelHeight);
            // Printed so an external check can compare against the actual pixels on screen.
            const glm::vec2 sentence_size = font_->measure("Où étaient les œufs d'été ? « Ici. »");
            std::cout << "measured sentence: " << sentence_size.x << " " << sentence_size.y << '\n';
            moteur::TextOptions wrapped;
            wrapped.max_width = 420.0f;
            const glm::vec2 paragraph_size = font_->measure(
                "Ce moteur dessine maintenant du texte : chaque glyphe est rasterisé une fois dans "
                "un atlas, puis dessiné comme n'importe quel autre sprite, trié et regroupé par le "
                "même batch.",
                wrapped);
            std::cout << "measured paragraph: " << paragraph_size.x << " " << paragraph_size.y
                      << " (" << static_cast<int>(paragraph_size.y / font_->line_height()) << " lines)\n";
            return;
        }

        if (options.atlas) {
            test_atlas_ = moteur::TextureAtlas::load(app.renderer(), moteur::asset_path("test.json"));
            return;
        }

        if (options.iso) {
            // The ground tiles and the highlight all come from one atlas: they share a texture,
            // so they can be drawn together whatever their order.
            world_atlas_ = moteur::TextureAtlas::load(app.renderer(), moteur::asset_path("world.json"));
            tile_a_ = &world_atlas_->region("tile_a");
            tile_b_ = &world_atlas_->region("tile_b");
            highlight_ = &world_atlas_->region("tile_highlight");

            // Start in the middle of the map unless told otherwise.
            const auto middle = static_cast<float>(options.map_size) * 0.5f;
            camera_.set_position(options.has_camera ? options.camera : iso_.to_world({middle, middle}));
            camera_.set_zoom(options.zoom);
            if (options.fake_mouse) {
                mouse_ = options.fake_mouse_position;
            }
            return;
        }

        if (options.demo) {
            // Same tile atlas as --iso, plus the walking character (from the sprite-atlas test set)
            // as a stand-in for real creature art, and text for the stats overlay.
            world_atlas_ = moteur::TextureAtlas::load(app.renderer(), moteur::asset_path("world.json"));
            tile_a_ = &world_atlas_->region("tile_a");
            tile_b_ = &world_atlas_->region("tile_b");
            highlight_ = &world_atlas_->region("tile_highlight");
            test_atlas_ = moteur::TextureAtlas::load(app.renderer(), moteur::asset_path("test.json"));
            animations_ = moteur::AnimationLibrary::load(moteur::asset_path("animations.json"));
            animations_->check_regions(*test_atlas_);
            const moteur::AnimationClip& walk = animations_->clip("walk");
            font_ = moteur::Font::load(app.renderer(), moteur::asset_path("fonts/Inter-Regular.ttf"), kFontPixelHeight);

            // The map: a ground layer of two alternating kinds, and a layer of walls that the
            // creatures cannot cross. Walls have no art yet: the game draws them as tinted boxes.
            ground_a_ = tileset_.add({"tile_a", true, false});
            ground_b_ = tileset_.add({"tile_b", true, false});
            wall_ = tileset_.add({"", false, true});
            map_.emplace(options.map_size, options.map_size, 2);
            for (int j = 0; j < options.map_size; ++j) {
                for (int i = 0; i < options.map_size; ++i) {
                    map_->set(kGroundLayer, {i, j}, (i + j) % 2 == 0 ? ground_a_ : ground_b_);
                    if (is_wall(i, j)) {
                        map_->set(kWallLayer, {i, j}, wall_);
                    }
                }
            }

            Random random(options.seed);
            static constexpr glm::vec2 kDirections[8] = {
                {-1.0f, -1.0f}, {-1.0f, 0.0f}, {-1.0f, 1.0f}, {0.0f, -1.0f},
                {0.0f, 1.0f}, {1.0f, -1.0f}, {1.0f, 0.0f}, {1.0f, 1.0f},
            };
            constexpr float kCreatureSpeed = 2.0f;  // tiles per second
            creatures_.reserve(static_cast<std::size_t>(options.movers));
            for (int i = 0; i < options.movers; ++i) {
                glm::vec2 position(0.0f);
                do {  // never inside a wall
                    position = {1.0f + random.next() * static_cast<float>(options.map_size - 2),
                                1.0f + random.next() * static_cast<float>(options.map_size - 2)};
                } while (!map_->walkable(tileset_, glm::ivec2(glm::floor(position))));
                const glm::vec2 direction = kDirections[static_cast<std::size_t>(random.next() * 8.0f) % 8];
                const glm::vec4 tint(0.5f + 0.5f * random.next(), 0.5f + 0.5f * random.next(),
                                     0.5f + 0.5f * random.next(), 1.0f);
                // Faster creatures also step faster, so their feet keep up with the ground; each
                // starts at its own point of the cycle so the crowd does not walk in step.
                const float pace = 0.75f + 0.5f * random.next();
                Creature creature{position, direction * (kCreatureSpeed * pace), tint, moteur::AnimationPlayer(walk)};
                creature.walk.set_speed(pace);
                creature.walk.set_time(static_cast<std::int64_t>(random.next() * static_cast<float>(walk.cycle_ticks())) *
                                       moteur::AnimationPlayer::kSpeedOne);
                creatures_.push_back(std::move(creature));
            }

            const auto middle = static_cast<float>(options.map_size) * 0.5f;
            camera_.set_position(options.has_camera ? options.camera : iso_.to_world({middle, middle}));
            camera_.set_zoom(options.zoom);
            if (options.fake_mouse) {
                mouse_ = options.fake_mouse_position;
            }
            return;
        }

        Random random(12345);
        movers_.reserve(static_cast<std::size_t>(options.movers));
        for (int i = 0; i < options.movers; ++i) {
            const glm::vec2 position(random.next() * (view_size_.x - kMoverSize), random.next() * (view_size_.y - kMoverSize));
            const glm::vec2 velocity((random.next() - 0.5f) * 300.0f, (random.next() - 0.5f) * 300.0f);
            const glm::vec4 tint(0.5f + 0.5f * random.next(), 0.5f + 0.5f * random.next(), 0.5f + 0.5f * random.next(), 1.0f);
            movers_.push_back({position, velocity, tint});
        }
    }

    void on_event(const SDL_Event& event) override {
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
            stop();
        }
        if (options_.render3d && !options_.no_input) {
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_P && !event.key.repeat) {
                toggle_projection();
            } else if (event.type == SDL_EVENT_MOUSE_WHEEL && event.wheel.y != 0.0f) {
                camera3d_.set_visible_height(
                    std::clamp(camera3d_.visible_height() * (event.wheel.y > 0.0f ? 0.9f : 1.0f / 0.9f), 3.0f, 60.0f));
            }
            return;
        }
        if (!(options_.iso || options_.demo) || options_.no_input) {
            return;
        }
        if (event.type == SDL_EVENT_MOUSE_WHEEL && event.wheel.y != 0.0f) {
            // Whole zoom steps: pixel art stays sharp and free of shimmering.
            const float zoom = camera_.zoom() + (event.wheel.y > 0.0f ? 1.0f : -1.0f);
            camera_.set_zoom(std::clamp(zoom, 1.0f, kMaxZoom));
        }
        if (!options_.fake_mouse) {
            if (event.type == SDL_EVENT_MOUSE_MOTION) {
                mouse_ = app_.to_pixels({event.motion.x, event.motion.y});
            } else if (event.type == SDL_EVENT_WINDOW_MOUSE_LEAVE) {
                mouse_ = {-1.0f, -1.0f};
            }
        }
    }

    void update(double dt) override {
        elapsed_ += dt;
        ++ticks_;
        if (options_.run_seconds > 0.0 && elapsed_ >= options_.run_seconds) {
            stop();
        }

        const bool frozen = options_.freeze_after_ticks > 0 && ticks_ > options_.freeze_after_ticks;

        if (options_.render3d) {
            last_stats_ = app_.renderer().stats();  // see the demo scene below
            if (!frozen) {
                spin_ += static_cast<float>(dt);
                if (!options_.no_input) {
                    move_camera3d(static_cast<float>(dt));
                }
            }
            return;
        }
        if (options_.text) {
            return;  // nothing moves; render_text() keeps its own FPS counter
        }
        if (options_.atlas) {
            return;  // nothing moves
        }
        if (options_.iso) {
            camera_.begin_update();
            if (!frozen && !options_.no_input) {
                move_camera(static_cast<float>(dt));
            }
            return;
        }
        if (options_.demo) {
            // renderer.stats() is only valid from end_frame() until the next begin_frame(): this
            // is that window (update() runs before this iteration's begin_frame()), so it is the
            // last point at which the previous frame's real counts can still be read.
            last_stats_ = app_.renderer().stats();
            camera_.begin_update();
            if (!frozen && !options_.no_input) {
                move_camera(static_cast<float>(dt));
            }
            if (!frozen) {
                move_creatures(static_cast<float>(dt));
            }
            return;
        }

        // Logic state of the main sprite: its offset from the center of the window. The previous
        // state is kept so that rendering can interpolate between two fixed steps.
        previous_offset_ = offset_;
        if (frozen) {
            return;  // the picture no longer changes: two runs can be compared pixel by pixel
        }
        if (!options_.still) {
            const auto t = static_cast<float>(elapsed_);
            offset_ = {300.0f * std::sin(1.2f * t), 60.0f * std::sin(2.4f * t)};
        }

        // The stress-test sprites bounce inside the window.
        const auto step = static_cast<float>(dt);
        const glm::vec2 limit = view_size_ - glm::vec2(kMoverSize);
        for (Mover& mover : movers_) {
            mover.position += mover.velocity * step;
            if (mover.position.x < 0.0f || mover.position.x > limit.x) {
                mover.velocity.x = -mover.velocity.x;
                mover.position.x = glm::clamp(mover.position.x, 0.0f, limit.x);
            }
            if (mover.position.y < 0.0f || mover.position.y > limit.y) {
                mover.velocity.y = -mover.velocity.y;
                mover.position.y = glm::clamp(mover.position.y, 0.0f, limit.y);
            }
        }
    }

    void render(moteur::Renderer& renderer, double alpha) override {
        ++frames_;
        renderer.set_clear_color(0.20f, 0.25f, 0.35f);
        view_size_ = {static_cast<float>(renderer.width()), static_cast<float>(renderer.height())};

        // Deterministic scene + a fixed tick makes two runs with the same seed produce the same
        // capture: request it once the frame is frozen, whatever scene is running.
        if (!options_.capture_path.empty() && !captured_ && options_.freeze_after_ticks > 0 &&
            ticks_ > options_.freeze_after_ticks) {
            renderer.request_capture(options_.capture_path);
            captured_ = true;
        }

        if (options_.render3d) {
            render_3d(renderer);
            return;
        }
        if (options_.text) {
            render_text(renderer);
            return;
        }
        if (options_.atlas) {
            render_atlas(renderer);
            return;
        }
        if (options_.iso) {
            render_iso(renderer, alpha);
            return;
        }
        if (options_.demo) {
            render_demo(renderer, alpha);
            return;
        }

        moteur::SpriteRenderer& sprites = renderer.sprites();

        // The main sprite, interpolated between two fixed steps.
        const auto draw_main_sprite = [&] {
            const glm::vec2 size(static_cast<float>(texture_.width) * kSpriteScale,
                                 static_cast<float>(texture_.height) * kSpriteScale);
            const glm::vec2 offset = glm::mix(previous_offset_, offset_, static_cast<float>(alpha));
            moteur::SpriteOptions options;
            options.depth = options_.depth ? 2.0f : 0.0f;  // in front of everything
            sprites.draw(texture_, view_size_ * 0.5f - size * 0.5f + offset, size, options);
        };

        // With --depth the main sprite is recorded FIRST but has the largest depth, so it only
        // ends up on top if the sorting works. Without it, it is recorded last and is on top by order.
        if (options_.depth) {
            draw_main_sprite();
        }

        // The stress-test sprites are drawn at their current position, without interpolation.
        // With --depth, the lower a sprite is on the window, the more it is in front.
        for (const Mover& mover : movers_) {
            moteur::SpriteOptions options;
            options.tint = mover.tint;
            options.depth = options_.depth ? mover.position.y / view_size_.y : 0.0f;
            sprites.draw(texture_, mover.position, glm::vec2(kMoverSize), options);
        }

        if (!options_.depth) {
            draw_main_sprite();
        }
    }

    bool stop_requested() const { return stop_requested_; }
    long ticks() const { return ticks_; }
    long frames() const { return frames_; }
    double elapsed() const { return elapsed_; }
    glm::ivec2 hovered_tile() const { return hovered_tile_; }
    bool has_hovered_tile() const { return has_hovered_tile_; }

private:
    void stop() {
        if (standalone_) {
            app_.quit();
        } else {
            stop_requested_ = true;
        }
    }

    void move_camera(float dt) {
        const bool* keys = SDL_GetKeyboardState(nullptr);
        glm::vec2 direction(0.0f);
        if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]) direction.x -= 1.0f;
        if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) direction.x += 1.0f;
        if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) direction.y -= 1.0f;
        if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]) direction.y += 1.0f;
        if (direction != glm::vec2(0.0f)) {
            // The same speed on screen at any zoom: a zoomed-in camera moves less through the world.
            camera_.set_position(camera_.position() + glm::normalize(direction) * (kCameraSpeed * dt / camera_.zoom()));
        }
    }

    // One simulation tick of the crowd: a creature turns back when its next position would be in
    // a wall or off the map, and its walk animation advances on the same tick.
    void move_creatures(float dt) {
        for (Creature& creature : creatures_) {
            const glm::vec2 next = creature.position + creature.velocity * dt;
            if (map_->walkable(tileset_, glm::ivec2(glm::floor(next)))) {
                creature.position = next;
            } else {
                creature.velocity = -creature.velocity;
            }
            fired_.clear();
            creature.walk.advance(1, &fired_);
            steps_ += static_cast<long>(fired_.size());  // the walk clip only has "step" events
        }
    }

    // Placeholder walls: a lattice of grid lines with gaps for doorways, so the demo map looks
    // like a set of rooms without needing a level editor.
    static bool is_wall(int i, int j) {
        return (i % 10 == 0 && j % 4 != 0) || (j % 10 == 0 && i % 4 != 0);
    }

    // Once per rendered (not simulated) frame: frames per real second, shown by the text/demo
    // scenes. The window title already gives the exact FPS; this is a simplified display copy.
    void update_fps_counter() {
        ++fps_frames_since_;
        if (elapsed_ - fps_last_time_ >= 1.0) {
            fps_display_ = fps_frames_since_;
            fps_frames_since_ = 0;
            fps_last_time_ = elapsed_;
        }
    }

    void render_iso(moteur::Renderer& renderer, double alpha) {
        moteur::SpriteRenderer& sprites = renderer.sprites();

        // Drawing and picking use the same interpolated camera, so the tile under the mouse is
        // always the one drawn under it, even while the camera moves.
        camera_.set_viewport(view_size_);
        const moteur::Camera2D camera = camera_.interpolated(alpha);
        sprites.set_view_projection(camera.view_projection());

        // Only the tiles that can be seen: the visible part of the world, turned into tiles.
        const moteur::TileRange range = iso_.tiles_in(camera.visible_rect(), 2);
        const int last = options_.map_size - 1;
        const int first_i = std::max(range.min.x, 0), last_i = std::min(range.max.x, last);
        const int first_j = std::max(range.min.y, 0), last_j = std::min(range.max.y, last);
        // The tiles are placed by their pivot, the top corner of the diamond, which is exactly
        // the position of the tile in the world. Both kinds of tile are in the same atlas page,
        // so alternating them tile by tile costs no extra draw call.
        if (options_.interleave) {
            for (int i = first_i; i <= last_i; ++i) {
                for (int j = first_j; j <= last_j; ++j) {
                    sprites.draw(((i + j) % 2 == 0) ? *tile_a_ : *tile_b_, iso_.to_world(glm::vec2(i, j)));
                }
            }
        } else {
            // Grouping by kind. With a single atlas page it makes no difference to the draw calls.
            for (int parity = 0; parity < 2; ++parity) {
                const moteur::SpriteRegion& tile = parity == 0 ? *tile_a_ : *tile_b_;
                for (int i = first_i; i <= last_i; ++i) {
                    for (int j = first_j; j <= last_j; ++j) {
                        if ((i + j) % 2 == parity) {
                            sprites.draw(tile, iso_.to_world(glm::vec2(i, j)));
                        }
                    }
                }
            }
        }

        // A small sprite on the origin of the world, to see where it is on screen.
        moteur::SpriteOptions marker;
        marker.depth = 1.0f;
        sprites.draw(texture_, glm::vec2(-16.0f), glm::vec2(32.0f), marker);

        // The tile under the mouse.
        has_hovered_tile_ = false;
        if (mouse_.x >= 0.0f && mouse_.y >= 0.0f) {
            hovered_tile_ = iso_.tile_at(camera.screen_to_world(mouse_));
            has_hovered_tile_ = hovered_tile_.x >= 0 && hovered_tile_.x <= last && hovered_tile_.y >= 0 && hovered_tile_.y <= last;
        }
        if (has_hovered_tile_) {
            moteur::SpriteOptions highlight;
            highlight.tint = glm::vec4(1.0f, 1.0f, 1.0f, 0.6f);
            highlight.depth = 2.0f;
            sprites.draw(*highlight_, iso_.to_world(glm::vec2(hovered_tile_)), 1.0f, highlight);
        }
    }

    // Milestone 3, part 4: the first meshes. A 20 x 20 m checkered floor (one 1 m tile per grid
    // cell, cell (i, j) covering [i, i+1] x [j, j+1]), a wall of 1 m cubes, a spinning cube, a
    // sphere, a character-sized box (0.7 x 1.8 x 0.7 m) and four 3 m pillars in the corners, which
    // show best how the two projections differ.
    void render_3d(moteur::Renderer& renderer) {
        renderer.set_clear_color(0.05f, 0.06f, 0.08f);
        camera3d_.set_viewport(view_size_);
        moteur::MeshRenderer& meshes = renderer.meshes();
        meshes.set_camera(camera3d_.view_projection(), camera3d_.position());

        // Light: the sun, the surroundings, and torches circling the middle of the floor.
        const float sun_yaw = glm::radians(sun_yaw_), sun_elevation = glm::radians(sun_elevation_);
        meshes.set_sun({std::cos(sun_elevation) * std::cos(sun_yaw), std::sin(sun_elevation),
                        std::cos(sun_elevation) * std::sin(sun_yaw)},
                       glm::vec3(screen_color(1.0f, 0.93f, 0.8f)), sun_intensity_);
        meshes.set_environment(&environments_[static_cast<std::size_t>(environment_index_)].environment,
                               environment_intensity_);
        static const glm::vec3 kTorchColors[4] = {{1.0f, 0.45f, 0.12f}, {1.0f, 0.6f, 0.2f}, {0.3f, 0.5f, 1.0f},
                                                  {0.4f, 1.0f, 0.5f}};
        for (int k = 0; k < torch_count_; ++k) {
            const float angle = spin_ * 0.4f + glm::two_pi<float>() * static_cast<float>(k) / static_cast<float>(torch_count_);
            const glm::vec3 position(6.0f * std::cos(angle), 0.9f + 0.3f * std::sin(spin_ * 2.0f + static_cast<float>(k)),
                                     6.0f * std::sin(angle));
            const glm::vec3 color = kTorchColors[k % 4];
            meshes.add_light({position, color, torch_intensity_, 6.0f});
            moteur::Material flame;
            flame.base_color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            flame.emissive = color * 12.0f;  // a small bright source, well above 1 in HDR
            meshes.draw(sphere_mesh_, glm::scale(glm::translate(glm::mat4(1.0f), position), glm::vec3(0.15f)), flame);
        }

        const auto at = [](glm::vec3 position, glm::vec3 scale = glm::vec3(1.0f)) {
            return glm::scale(glm::translate(glm::mat4(1.0f), position), scale);
        };
        for (int j = -10; j < 10; ++j) {
            for (int i = -10; i < 10; ++i) {
                const bool light = ((i + j) & 1) == 0;
                const glm::vec4 color = light ? screen_color(0.55f, 0.6f, 0.5f) : screen_color(0.42f, 0.47f, 0.38f);
                meshes.draw(tile_mesh_, at({static_cast<float>(i) + 0.5f, 0.0f, static_cast<float>(j) + 0.5f}), color);
            }
        }
        for (int i = -6; i <= 2; ++i) {  // a wall, one cube per cell, along X
            meshes.draw(cube_mesh_, at({static_cast<float>(i) + 0.5f, 0.5f, -3.5f}), screen_color(0.6f, 0.55f, 0.5f));
        }
        const glm::mat4 spinning = glm::rotate(at({3.5f, 1.0f, 2.5f}), spin_, glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f)));
        meshes.draw(cube_mesh_, spinning, screen_color(0.85f, 0.35f, 0.25f));
        meshes.draw(sphere_mesh_, at({-2.5f, 0.5f, 2.5f}), screen_color(0.3f, 0.5f, 0.85f));
        meshes.draw(cube_mesh_, at({0.5f, 0.9f, 0.5f}, {0.7f, 1.8f, 0.7f}), screen_color(0.9f, 0.8f, 0.3f));
        for (const glm::vec2 corner : {glm::vec2(-8.5f, -8.5f), glm::vec2(8.5f, -8.5f), glm::vec2(-8.5f, 8.5f),
                                       glm::vec2(8.5f, 8.5f)}) {
            meshes.draw(cube_mesh_, at({corner.x, 1.5f, corner.y}, {0.5f, 3.0f, 0.5f}), screen_color(0.75f, 0.75f, 0.8f));
        }

        // The glTF models, in a row behind the other objects, one per 4 m slot, each standing on a
        // cell centre so that a 1 m cube exactly covers a cell. Their own origin is kept: that is
        // what the check is about. Only models larger than a slot are scaled down, and say so.
        moteur::TextOptions label;
        label.align = moteur::TextAlign::Center;
        label.max_width = 300.0f;
        for (std::size_t k = 0; k < models_.size(); ++k) {
            const glm::vec3 slot(-5.5f + 4.0f * static_cast<float>(k % 4), 0.0f, 5.5f + 4.0f * static_cast<float>(k / 4));
            const ShownModel& shown = models_[k];
            std::string caption = shown.file.substr(shown.file.find_last_of("/\\") + 1);  // no folders
            float top = 1.0f;
            if (shown.model) {
                const moteur::Model& model = *shown.model;
                const glm::vec3 size = model.bounds.size();
                const float largest = std::max({size.x, size.y, size.z});
                const float scale = largest > 3.5f ? 3.5f / largest : 1.0f;
                meshes.draw(model, at(slot, glm::vec3(scale)));
                top = model.bounds.max.y * scale;
                char details[96];
                std::snprintf(details, sizeof(details), "\n%zu triangles, %.1f ms", model.triangle_count, shown.load_ms);
                caption += details;
                if (scale < 1.0f) {
                    std::snprintf(details, sizeof(details), "\naffiché à l'échelle %.2f", static_cast<double>(scale));
                    caption += details;
                }
                label.color = {0.95f, 0.95f, 0.8f, 1.0f};
            } else {
                caption += "\n" + shown.error;
                label.color = {1.0f, 0.5f, 0.45f, 1.0f};
            }
            // Above the model, in window pixels: project a point of the world onto the screen.
            const glm::vec4 clip = camera3d_.view_projection() * glm::vec4(slot + glm::vec3(0.0f, top + 0.3f, 0.0f), 1.0f);
            if (clip.w > 0.0f) {
                const glm::vec2 ndc = glm::vec2(clip) / clip.w;
                const glm::vec2 pixel((ndc.x * 0.5f + 0.5f) * view_size_.x, (0.5f - ndc.y * 0.5f) * view_size_.y);
                const float lines = static_cast<float>(std::count(caption.begin(), caption.end(), '\n') + 1);
                font_->draw(renderer.screen_sprites(), caption,
                            pixel - glm::vec2(150.0f, lines * font_->line_height()), label);
            }
        }

        // The classic check of a PBR shader (as in Khronos' MetalRoughSpheres): metalness grows
        // from front to back, roughness from left to right. Mirror-like metals in the back left, matte
        // dielectrics in the front right.
        for (int row = 0; row < 7; ++row) {
            for (int column = 0; column < 7; ++column) {
                moteur::Material material;
                material.base_color = glm::vec4(0.9f, 0.9f, 0.9f, 1.0f);
                material.metallic = 1.0f - static_cast<float>(row) / 6.0f;
                material.roughness = static_cast<float>(column) / 6.0f;
                const glm::vec3 position(2.5f + static_cast<float>(column), 0.45f, -9.0f + static_cast<float>(row) * 0.9f);
                meshes.draw(sphere_mesh_, at(position, glm::vec3(0.85f)), material);
            }
        }

        // What is being compared, in window pixels over the scene.
        const bool ortho = camera3d_.projection() == moteur::Projection::Orthographic;
        moteur::SpriteRenderer& screen = renderer.screen_sprites();
        moteur::TextOptions title;
        title.color = {0.95f, 0.95f, 1.0f, 1.0f};
        const glm::vec2 corner(20.0f, 40.0f);
        const float line = font_->line_height();
        font_->draw(screen, ortho ? "Projection orthographique" : "Projection perspective", corner, title);
        moteur::TextOptions detail;
        detail.color = {0.7f, 0.75f, 0.85f, 1.0f};
        char text[160];
        if (ortho) {
            std::snprintf(text, sizeof(text), "inclinaison %.1f°, orientation %.0f°, hauteur visible %.1f m",
                          static_cast<double>(camera3d_.pitch()), static_cast<double>(camera3d_.yaw()),
                          static_cast<double>(camera3d_.visible_height()));
        } else {
            std::snprintf(text, sizeof(text),
                          "inclinaison %.1f°, orientation %.0f°, hauteur visible %.1f m, champ %.0f°, distance %.1f m",
                          static_cast<double>(camera3d_.pitch()), static_cast<double>(camera3d_.yaw()),
                          static_cast<double>(camera3d_.visible_height()), static_cast<double>(camera3d_.field_of_view()),
                          static_cast<double>(camera3d_.distance()));
        }
        font_->draw(screen, text, corner + glm::vec2(0.0f, line), detail);
        std::snprintf(text, sizeof(text), "%d maillages, %zu triangles, %d draw calls, rendu %ux%u, %d lumières. P : changer de projection",
                      last_stats_.meshes, last_stats_.triangles, last_stats_.draw_calls, renderer.scene_width(),
                      renderer.scene_height(), torch_count_ + 1);
        font_->draw(screen, text, corner + glm::vec2(0.0f, 2.0f * line), detail);
    }

    // Every .glb and .gltf file of assets/models/, sorted by name. A model that cannot be loaded is
    // shown as an error message in its slot rather than stopping the scene.
    void load_models(moteur::Renderer& renderer) {
        const std::string directory = moteur::asset_path("models");
        int count = 0;
        char** files = SDL_GlobDirectory(directory.c_str(), nullptr, 0, &count);
        std::vector<std::string> names;
        for (int i = 0; files != nullptr && i < count; ++i) {
            const std::string name = files[i];
            if (name.size() > 4 && (name.substr(name.size() - 4) == ".glb" || name.substr(name.size() - 5) == ".gltf")) {
                names.push_back(name);
            }
        }
        SDL_free(files);
        std::sort(names.begin(), names.end());
        for (const std::string& name : names) {
            ShownModel shown;
            shown.file = name;
            const Uint64 start = SDL_GetPerformanceCounter();
            try {
                shown.model = moteur::Model::load(renderer, directory + "/" + name);
                shown.load_ms = static_cast<double>(SDL_GetPerformanceCounter() - start) * 1000.0 /
                                static_cast<double>(SDL_GetPerformanceFrequency());
                SDL_Log("Model '%s': %zu parts, %zu triangles, %zu textures, loaded in %.1f ms", name.c_str(),
                        shown.model->parts.size(), shown.model->triangle_count, shown.model->textures.size(),
                        shown.load_ms);
            } catch (const std::exception& e) {
                shown.error = e.what();
                SDL_Log("%s", e.what());
            }
            models_.push_back(std::move(shown));
        }
    }

    // The surroundings the surfaces reflect: a procedural sky, and every .hdr image of
    // assets/environments/ (equirectangular, as Poly Haven publishes them).
    void load_environments(moteur::Renderer& renderer) {
        const Uint64 start = SDL_GetPerformanceCounter();
        environments_.push_back({"Ciel procédural", moteur::Environment::create(renderer, moteur::make_sky(512, 256), "sky")});
        SDL_Log("Environment 'sky': prefiltered in %.1f ms",
                static_cast<double>(SDL_GetPerformanceCounter() - start) * 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency()));
        const std::string directory = moteur::asset_path("environments");
        int count = 0;
        char** files = SDL_GlobDirectory(directory.c_str(), "*.hdr", 0, &count);
        std::vector<std::string> names;
        for (int i = 0; files != nullptr && i < count; ++i) {
            names.emplace_back(files[i]);
        }
        SDL_free(files);
        std::sort(names.begin(), names.end());
        for (const std::string& name : names) {
            const Uint64 begin = SDL_GetPerformanceCounter();
            try {
                const moteur::EnvironmentImage image = moteur::load_environment(directory + "/" + name);
                environments_.push_back({name, moteur::Environment::create(renderer, image, name.c_str())});
                SDL_Log("Environment '%s': %dx%d, prefiltered in %.1f ms", name.c_str(), image.width, image.height,
                        static_cast<double>(SDL_GetPerformanceCounter() - begin) * 1000.0 /
                            static_cast<double>(SDL_GetPerformanceFrequency()));
            } catch (const std::exception& e) {
                SDL_Log("%s", e.what());
            }
        }
        environment_index_ = static_cast<int>(environments_.size()) - 1;  // a real image when there is one
    }

    void toggle_projection() {
        camera3d_.set_projection(camera3d_.projection() == moteur::Projection::Orthographic
                                     ? moteur::Projection::Perspective
                                     : moteur::Projection::Orthographic);
    }

    // Arrows or ZQSD/WASD move the target over the ground, along the screen directions.
    void move_camera3d(float dt) {
        const bool* keys = SDL_GetKeyboardState(nullptr);
        glm::vec2 input(0.0f);
        if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]) input.x -= 1.0f;
        if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) input.x += 1.0f;
        if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) input.y += 1.0f;
        if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]) input.y -= 1.0f;
        if (input == glm::vec2(0.0f)) {
            return;
        }
        const glm::vec3 ahead = glm::normalize(glm::vec3(camera3d_.forward().x, 0.0f, camera3d_.forward().z));
        const glm::vec3 right = glm::cross(ahead, glm::vec3(0.0f, 1.0f, 0.0f));
        // Same speed on screen whatever the framing: about two thirds of the visible height per second.
        const float speed = camera3d_.visible_height() * 0.66f;
        const glm::vec2 step = glm::normalize(input) * speed * dt;
        camera3d_.set_target(camera3d_.target() + right * step.x + ahead * step.y);
    }

public:
    // Settings of the running test, shown by the menu in its panel (ImGui).
    void draw_controls() {
        if (!options_.render3d) {
            return;
        }
        int projection = camera3d_.projection() == moteur::Projection::Orthographic ? 0 : 1;
        ImGui::SeparatorText("Caméra");
        if (ImGui::RadioButton("Orthographique", &projection, 0)) {
            camera3d_.set_projection(moteur::Projection::Orthographic);
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Perspective", &projection, 1)) {
            camera3d_.set_projection(moteur::Projection::Perspective);
        }
        float pitch = camera3d_.pitch();
        float yaw = camera3d_.yaw();
        ImGui::PushItemWidth(200.0f);
        // Both sliders are drawn every frame, so neither call may be skipped by short-circuiting.
        const bool pitch_changed = ImGui::SliderFloat("Inclinaison (°)", &pitch, 10.0f, 80.0f, "%.1f");
        const bool yaw_changed = ImGui::SliderFloat("Orientation (°)", &yaw, 0.0f, 360.0f, "%.0f");
        if (pitch_changed || yaw_changed) {
            camera3d_.set_angles(yaw, pitch);
        }
        float height = camera3d_.visible_height();
        if (ImGui::SliderFloat("Hauteur visible (m)", &height, 3.0f, 60.0f, "%.1f")) {
            camera3d_.set_visible_height(height);
        }
        ImGui::BeginDisabled(camera3d_.projection() == moteur::Projection::Orthographic);
        float fov = camera3d_.field_of_view();
        if (ImGui::SliderFloat("Champ de vision (°)", &fov, 10.0f, 90.0f, "%.0f")) {
            camera3d_.set_field_of_view(fov);
        }
        ImGui::EndDisabled();
        ImGui::PopItemWidth();
        ImGui::SeparatorText("Lumière");
        ImGui::PushItemWidth(200.0f);
        if (ImGui::BeginCombo("Environnement", environments_[static_cast<std::size_t>(environment_index_)].name.c_str())) {
            for (std::size_t i = 0; i < environments_.size(); ++i) {
                if (ImGui::Selectable(environments_[i].name.c_str(), static_cast<int>(i) == environment_index_)) {
                    environment_index_ = static_cast<int>(i);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SliderFloat("Intensité environnement", &environment_intensity_, 0.0f, 4.0f, "%.2f");
        ImGui::SliderFloat("Soleil", &sun_intensity_, 0.0f, 10.0f, "%.1f");
        ImGui::SliderFloat("Hauteur du soleil (°)", &sun_elevation_, 5.0f, 90.0f, "%.0f");
        ImGui::SliderFloat("Direction du soleil (°)", &sun_yaw_, 0.0f, 360.0f, "%.0f");
        ImGui::SliderInt("Torches", &torch_count_, 0, moteur::MeshRenderer::kMaxPointLights);
        ImGui::SliderFloat("Intensité des torches", &torch_intensity_, 0.0f, 20.0f, "%.1f");
        ImGui::PopItemWidth();
        ImGui::SeparatorText("Rendu");
        ImGui::PushItemWidth(200.0f);
        float scale = app_.renderer().render_scale();
        if (ImGui::SliderFloat("Résolution de rendu", &scale, 0.25f, 1.0f, "%.2f")) {
            app_.renderer().set_render_scale(scale);
        }
        float exposure = app_.renderer().exposure();
        if (ImGui::SliderFloat("Exposition", &exposure, 0.1f, 8.0f, "%.2f", ImGuiSliderFlags_Logarithmic)) {
            app_.renderer().set_exposure(exposure);
        }
        ImGui::PopItemWidth();
        if (ImGui::Button("Réglage retenu")) {
            const moteur::Camera3D chosen;  // the defaults are the chosen framing
            camera3d_.set_projection(chosen.projection());
            camera3d_.set_angles(chosen.yaw(), chosen.pitch());
            camera3d_.set_field_of_view(chosen.field_of_view());
            camera3d_.set_visible_height(chosen.visible_height());
        }
        ImGui::SameLine();
        if (ImGui::Button("Vraie isométrie")) {
            camera3d_.set_projection(moteur::Projection::Orthographic);
            camera3d_.set_angles(45.0f, moteur::Camera3D::isometric_pitch());
        }
        ImGui::SameLine();
        if (ImGui::Button("Recentrer")) {
            camera3d_.set_target(glm::vec3(0.0f));
        }
    }

private:

    // A French sentence (accents, guillemets), a word-wrapped paragraph, the three alignments
    // side by side, and a live FPS counter: everything text-related in one screen.
    void render_text(moteur::Renderer& renderer) {
        moteur::SpriteRenderer& sprites = renderer.sprites();
        const moteur::Font& font = *font_;

        moteur::TextOptions basic;
        font.draw(sprites, "Où étaient les œufs d'été ? « Ici. »", {40.0f, 40.0f}, basic);

        moteur::TextOptions wrapped;
        wrapped.color = {1.0f, 0.85f, 0.55f, 1.0f};
        wrapped.max_width = 420.0f;
        font.draw(sprites,
                 "Ce moteur dessine maintenant du texte : chaque glyphe est rasterisé une fois dans "
                 "un atlas, puis dessiné comme n'importe quel autre sprite, trié et regroupé par le "
                 "même batch.",
                 {40.0f, 90.0f}, wrapped);

        // The same 300-pixel box, once per alignment, so the three results sit side by side.
        constexpr float box_x = 40.0f;
        constexpr float box_y = 260.0f;
        constexpr float box_width = 300.0f;
        moteur::TextOptions aligned;
        aligned.max_width = box_width;
        aligned.color = {0.6f, 0.85f, 1.0f, 1.0f};
        font.draw(sprites, "Gauche", {box_x, box_y}, aligned);
        aligned.align = moteur::TextAlign::Center;
        aligned.color = {0.6f, 1.0f, 0.7f, 1.0f};
        font.draw(sprites, "Centre", {box_x, box_y + font.line_height()}, aligned);
        aligned.align = moteur::TextAlign::Right;
        aligned.color = {1.0f, 0.7f, 0.7f, 1.0f};
        font.draw(sprites, "Droite", {box_x, box_y + 2.0f * font.line_height()}, aligned);

        // A simplified counter (frames per simulated second), just to show that text can be
        // redrawn every frame without a visible cost. The window title gives the exact FPS.
        update_fps_counter();
        moteur::TextOptions fps_options;
        fps_options.color = {0.6f, 1.0f, 0.6f, 1.0f};
        fps_options.align = moteur::TextAlign::Right;
        fps_options.max_width = 160.0f;
        font.draw(sprites, std::to_string(fps_display_) + " FPS", {view_size_.x - 200.0f, 20.0f}, fps_options);
    }

    // The demo scene of the part 9 milestone: a TileMap (ground and walls) seen through the
    // camera, several thousand creatures walking over it with their walk animation, and a stats
    // overlay. Reduced scope: one walk cycle mirrored by direction instead of 8 directions, and
    // walls are a tinted placeholder box, not dedicated art.
    void render_demo(moteur::Renderer& renderer, double alpha) {
        moteur::SpriteRenderer& sprites = renderer.sprites();

        camera_.set_viewport(view_size_);
        const moteur::Camera2D camera = camera_.interpolated(alpha);
        sprites.set_view_projection(camera.view_projection());

        // Only the tiles that can be seen, with room for the walls that stick out above theirs.
        const moteur::TileMap& map = *map_;
        const moteur::TileRange range = map.clip(iso_.tiles_in(camera.visible_rect(), 2));
        const int last = options_.map_size - 1;

        // Ground: grouped by kind so both textures batch into as few draw calls as possible.
        for (const moteur::TileId kind : {ground_a_, ground_b_}) {
            const moteur::SpriteRegion& tile = kind == ground_a_ ? *tile_a_ : *tile_b_;
            for (int i = range.min.x; i <= range.max.x; ++i) {
                for (int j = range.min.y; j <= range.max.y; ++j) {
                    if (map.at(kGroundLayer, {i, j}) == kind) {
                        sprites.draw(tile, iso_.to_world(glm::vec2(i, j)));
                    }
                }
            }
        }

        // Walls: a placeholder tinted box, lifted above the ground, sorted by the same ground
        // depth as the creatures so a creature correctly passes behind or in front of one.
        moteur::SpriteOptions wall_options;
        wall_options.tint = {0.35f, 0.30f, 0.25f, 1.0f};
        for (int i = range.min.x; i <= range.max.x; ++i) {
            for (int j = range.min.y; j <= range.max.y; ++j) {
                if (map.at(kWallLayer, {i, j}) == wall_) {
                    wall_options.depth = static_cast<float>(i + j);
                    const glm::vec2 base = iso_.to_world(glm::vec2(i, j), kTileHeight * 1.5f);
                    sprites.draw(texture_, base, glm::vec2(kTileWidth * 0.5f, kTileHeight * 1.5f), wall_options);
                }
            }
        }

        // Creatures: not interpolated between ticks (same simplification as the stress-test
        // movers), sorted by tile-space depth against both the walls and each other. The walk
        // cycle faces right; creatures heading left on screen (world.x = (x - y) * w / 2) use it
        // mirrored, around the pivot so their feet stay in place.
        for (const Creature& creature : creatures_) {
            moteur::SpriteOptions options;
            options.tint = creature.tint;
            options.depth = creature.position.x + creature.position.y;
            options.flip_x = creature.velocity.x - creature.velocity.y < 0.0f;
            sprites.draw(test_atlas_->region(creature.walk.region()), iso_.to_world(creature.position), 1.0f, options);
        }

        // The tile under the mouse, same as --iso.
        has_hovered_tile_ = false;
        if (mouse_.x >= 0.0f && mouse_.y >= 0.0f) {
            hovered_tile_ = iso_.tile_at(camera.screen_to_world(mouse_));
            has_hovered_tile_ = hovered_tile_.x >= 0 && hovered_tile_.x <= last && hovered_tile_.y >= 0 && hovered_tile_.y <= last;
        }
        if (has_hovered_tile_) {
            moteur::SpriteOptions highlight;
            highlight.tint = glm::vec4(1.0f, 1.0f, 1.0f, 0.6f);
            highlight.depth = static_cast<float>(2 * options_.map_size);  // always on top
            sprites.draw(*highlight_, iso_.to_world(glm::vec2(hovered_tile_)), 1.0f, highlight);
        }

        // Stats overlay: what the validation checklist asks for (FPS, frame time, sprite/lot/draw
        // call counts). last_stats_ is the previous frame's (see the comment in update()).
        update_fps_counter();
        const moteur::RenderStats& stats = last_stats_;
        const double frame_time_ms = fps_display_ > 0 ? 1000.0 / static_cast<double>(fps_display_) : 0.0;
        const std::string line1 = std::to_string(fps_display_) + " FPS (" + std::to_string(static_cast<int>(frame_time_ms)) + " ms)";
        const std::string line2 = std::to_string(stats.sprites) + " sprites, " + std::to_string(stats.draw_calls) + " lots/draw calls";
        const std::string line3 = std::to_string(steps_) + " pas (événements d'animation)";
        // Interface text: in window pixels, drawn over the world, whatever the camera and its zoom.
        moteur::SpriteRenderer& screen = renderer.screen_sprites();
        moteur::TextOptions stats_options;
        stats_options.color = {0.6f, 1.0f, 0.6f, 1.0f};
        stats_options.align = moteur::TextAlign::Right;
        stats_options.max_width = 360.0f;
        const glm::vec2 stats_anchor(view_size_.x - 400.0f, 20.0f);
        font_->draw(screen, line1, stats_anchor, stats_options);
        font_->draw(screen, line2, stats_anchor + glm::vec2(0.0f, font_->line_height()), stats_options);
        font_->draw(screen, line3, stats_anchor + glm::vec2(0.0f, 2.0f * font_->line_height()), stats_options);
    }

    // Every sprite of the test atlas in a grid, each at its pivot, then the walking character in
    // four ways at fixed places, which the automatic checks compare with the source images.
    void render_atlas(moteur::Renderer& renderer) {
        moteur::SpriteRenderer& sprites = renderer.sprites();
        const moteur::TextureAtlas& atlas = *test_atlas_;

        // A small red square shows where an anchor is.
        const auto mark = [&](glm::vec2 anchor) {
            moteur::SpriteOptions marker;
            marker.tint = glm::vec4(1.0f, 0.1f, 0.1f, 1.0f);
            marker.depth = 1.0f;
            sprites.draw(texture_, anchor - glm::vec2(2.0f), glm::vec2(4.0f), marker);
        };

        const std::vector<std::string> names = atlas.names();
        for (std::size_t i = 0; i < names.size(); ++i) {
            const glm::vec2 anchor(48.0f + static_cast<float>(i % 16) * 72.0f, 110.0f + static_cast<float>(i / 16) * 100.0f);
            sprites.draw(atlas.region(names[i]), anchor);
            mark(anchor);
        }

        const moteur::SpriteRegion& walk = atlas.region("walk_03");
        const auto draw_walk = [&](glm::vec2 anchor, float scale, bool flip_x) {
            moteur::SpriteOptions options;
            options.flip_x = flip_x;
            sprites.draw(walk, anchor, scale, options);
            mark(anchor);
        };
        draw_walk({300.0f, 480.0f}, 1.0f, false);
        draw_walk({500.0f, 480.0f}, 1.0f, true);
        draw_walk({760.0f, 480.0f}, 2.0f, false);
        draw_walk({1100.0f, 480.0f}, 3.0f, true);
    }

    moteur::Application& app_;
    moteur::Texture texture_;  // released automatically; the application outlives it (see gpu_resource.hpp)
    Options options_;
    bool standalone_;
    bool stop_requested_ = false;

    glm::vec2 view_size_ = {1280.0f, 720.0f};  // updated from the swapchain size every frame
    glm::vec2 offset_ = {0.0f, 0.0f};
    glm::vec2 previous_offset_ = {0.0f, 0.0f};
    std::vector<Mover> movers_;

    // Isometric scene.
    moteur::IsoProjection iso_;
    moteur::Camera2D camera_;
    std::optional<moteur::TextureAtlas> world_atlas_;
    const moteur::SpriteRegion* tile_a_ = nullptr;
    const moteur::SpriteRegion* tile_b_ = nullptr;
    const moteur::SpriteRegion* highlight_ = nullptr;
    std::optional<moteur::TextureAtlas> test_atlas_;
    glm::vec2 mouse_ = {-1.0f, -1.0f};  // window pixels; negative means "no mouse"
    glm::ivec2 hovered_tile_ = {0, 0};
    bool has_hovered_tile_ = false;

    // Demo scene.
    static constexpr int kGroundLayer = 0;
    static constexpr int kWallLayer = 1;
    moteur::Tileset tileset_;
    moteur::TileId ground_a_ = moteur::kNoTile;
    moteur::TileId ground_b_ = moteur::kNoTile;
    moteur::TileId wall_ = moteur::kNoTile;
    std::optional<moteur::TileMap> map_;
    std::optional<moteur::AnimationLibrary> animations_;  // must outlive the creatures' players
    std::vector<Creature> creatures_;
    std::vector<const moteur::AnimationEvent*> fired_;  // reused every tick
    long steps_ = 0;
    bool captured_ = false;
    moteur::RenderStats last_stats_;

    // 3D scene.
    moteur::Camera3D camera3d_;
    moteur::Mesh cube_mesh_;
    moteur::Mesh tile_mesh_;
    moteur::Mesh sphere_mesh_;
    float spin_ = 0.0f;  // seconds of rotation of the spinning cube
    struct NamedEnvironment {
        std::string name;
        moteur::Environment environment;
    };
    std::vector<NamedEnvironment> environments_;
    int environment_index_ = 0;
    float environment_intensity_ = 1.0f;
    float sun_intensity_ = 3.0f;
    float sun_elevation_ = 55.0f;  // degrees above the horizon
    float sun_yaw_ = 60.0f;        // degrees around the vertical axis, from +X towards +Z
    int torch_count_ = 16;
    float torch_intensity_ = 6.0f;
    std::vector<ShownModel> models_;

    // Text scene.
    std::optional<moteur::Font> font_;
    double fps_last_time_ = 0.0;
    int fps_frames_since_ = 0;
    int fps_display_ = 0;

    double elapsed_ = 0.0;
    long ticks_ = 0;
    long frames_ = 0;
};

// The program started without arguments: a menu bar (DEBUG > Tests moteur), a home screen, the
// page listing the test scenes with their settings, and the running test with a button to stop it.
class Sandbox final : public moteur::Game {
public:
    Sandbox(moteur::Application& app, double run_seconds)
        : app_(app), run_seconds_(run_seconds), credits_(Credits::load(moteur::asset_path("credits.json"))) {
        // The settings each test starts with; the list page can change them before launching.
        TestScene::Options sprites;
        sprites.movers = 3000;
        TestScene::Options iso;
        iso.iso = true;
        TestScene::Options atlas;
        atlas.atlas = true;
        TestScene::Options text;
        text.text = true;
        TestScene::Options render3d;
        render3d.render3d = true;
        TestScene::Options demo;
        demo.demo = true;
        demo.map_size = 100;
        demo.movers = 3000;

        tests_ = {
            {"Sprites et test de charge",
             "Un grand sprite qui se déplace et des petits sprites qui rebondissent : batching, tri en "
             "profondeur, nombre de draw calls (dans le titre de la fenêtre).",
             TestKind::Sprites, sprites},
            {"Carte isométrique",
             "Une carte de tuiles vue par la caméra, la tuile sous la souris surlignée. Flèches ou ZQSD "
             "pour se déplacer, molette pour zoomer.",
             TestKind::Iso, iso},
            {"Atlas de sprites",
             "Tous les sprites de l'atlas de test à leur pivot (carré rouge), puis un personnage normal, "
             "retourné, x2 et retourné x3.",
             TestKind::Atlas, atlas},
            {"Texte et polices",
             "Une phrase avec accents et guillemets, un paragraphe avec retour à la ligne, les trois "
             "alignements et un compteur de FPS.",
             TestKind::Text, text},
            {"Scène de démonstration",
             "Carte de tuiles avec murs, créatures animées qui font demi-tour devant les murs, "
             "statistiques. Mêmes commandes que la carte isométrique.",
             TestKind::Demo, demo},
            {"Rendu 3D : premiers maillages",
             "Sol en damier (cases de 1 m), cubes, sphère, personnage (1,8 m), piliers de 3 m, et les "
             "modèles glTF de assets/models. P change de projection ; réglages dans le panneau.",
             TestKind::Render3D, render3d},
        };
    }

    void on_event(const SDL_Event& event) override {
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE && !event.key.repeat) {
            // One step back: from a test to the list, from the list to the home screen.
            if (screen_ == Screen::Running) {
                request(Action::ShowTests);
            } else if (screen_ == Screen::Tests) {
                request(Action::ShowHome);
            }
            return;
        }
        if (scene_) {
            scene_->on_event(event);
        }
    }

    void update(double dt) override {
        elapsed_ += dt;
        if (run_seconds_ > 0.0 && elapsed_ >= run_seconds_) {
            app_.quit();
        }
        // Scenes are created and destroyed here, outside of any frame: loading uploads textures
        // and waits for the GPU, and a scene's textures may be in use by the frame being recorded.
        apply_request();
        if (scene_) {
            scene_->update(dt);
            if (scene_->stop_requested()) {
                request(Action::ShowTests);
            }
        }
    }

    void render(moteur::Renderer& renderer, double alpha) override {
        if (scene_) {
            scene_->render(renderer, alpha);
        } else {
            renderer.set_clear_color(0.08f, 0.09f, 0.11f);
        }

        draw_menu_bar();
        switch (screen_) {
            case Screen::Home: draw_home(); break;
            case Screen::Tests: draw_tests(); break;
            case Screen::Running: draw_running_panel(); break;
        }
        if (about_open_) {
            draw_about();
        }
    }

private:
    enum class Screen { Home, Tests, Running };
    enum class Action { None, ShowHome, ShowTests, Launch };
    enum class TestKind { Sprites, Iso, Atlas, Text, Demo, Render3D };

    struct TestEntry {
        const char* name;
        const char* description;
        TestKind kind;
        TestScene::Options options;
    };

    void request(Action action, int test = -1) {
        action_ = action;
        action_test_ = test;
    }

    void apply_request() {
        const Action action = action_;
        action_ = Action::None;
        switch (action) {
            case Action::None:
                return;
            case Action::ShowHome:
                scene_.reset();
                screen_ = Screen::Home;
                return;
            case Action::ShowTests:
                scene_.reset();
                screen_ = Screen::Tests;
                return;
            case Action::Launch:
                scene_.reset();  // the previous test, if any, releases its resources first
                try {
                    const TestEntry& test = tests_[static_cast<std::size_t>(action_test_)];
                    scene_ = std::make_unique<TestScene>(app_, test.options, false);
                    running_ = action_test_;
                    screen_ = Screen::Running;
                    error_.clear();
                } catch (const std::exception& e) {
                    scene_.reset();
                    screen_ = Screen::Tests;
                    error_ = e.what();
                }
                return;
        }
    }

    // The bar at the top of the window, on every screen.
    void draw_menu_bar() {
        if (!ImGui::BeginMainMenuBar()) {
            return;
        }
        if (ImGui::BeginMenu("DEBUG")) {
            if (ImGui::BeginMenu("Tests moteur")) {
                if (ImGui::MenuItem("Toutes les scènes...")) {
                    request(Action::ShowTests);
                }
                ImGui::Separator();
                for (std::size_t i = 0; i < tests_.size(); ++i) {
                    const bool current = screen_ == Screen::Running && running_ == static_cast<int>(i);
                    if (ImGui::MenuItem(tests_[i].name, nullptr, current)) {
                        request(Action::Launch, static_cast<int>(i));
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Accueil", nullptr, false, screen_ != Screen::Home)) {
                request(Action::ShowHome);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Aide")) {
            if (ImGui::MenuItem("À propos", nullptr, about_open_)) {
                about_open_ = !about_open_;
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // What the game will show one day; for now, a way into the debug tools.
    void draw_home() {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::Begin("Accueil", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings);
        ImGui::TextUnformatted("Moteur");
        ImGui::TextDisabled("SDL %s", moteur::sdl_version().c_str());
        ImGui::Separator();
        ImGui::TextUnformatted("Le jeu viendra ici.");
        ImGui::TextUnformatted("Les scènes de test sont dans le menu DEBUG > Tests moteur.");
        ImGui::Spacing();
        if (ImGui::Button("Tests moteur")) {
            request(Action::ShowTests);
        }
        ImGui::SameLine();
        if (ImGui::Button("À propos")) {
            about_open_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Quitter")) {
            app_.quit();
        }
        ImGui::End();
    }

    // Credits and licenses: what the program is made of, and who made the assets it shows.
    void draw_about() {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(720.0f, 520.0f), ImGuiCond_Appearing);
        ImGui::SetNextWindowBgAlpha(1.0f);  // opaque: the home screen must not show through
        if (!ImGui::Begin("À propos", &about_open_, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
            ImGui::End();
            return;
        }
        ImGui::TextUnformatted("Moteur : bac à sable du moteur de jeu");
        ImGui::TextDisabled("SDL %s (version chargée à l'exécution)", moteur::sdl_version().c_str());
        if (!credits_.error.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.4f, 1.0f));
            ImGui::TextWrapped("%s", credits_.error.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::SeparatorText("Bibliothèques");
        for (const Credits::Entry& entry : credits_.libraries) {
            draw_credit(entry);
        }
        ImGui::SeparatorText("Police");
        for (const Credits::Entry& entry : credits_.fonts) {
            draw_credit(entry);
        }
        ImGui::SeparatorText("Modèles 3D de test");
        ImGui::TextWrapped("Poly Haven publie ses modèles en CC0 (domaine public) : les citer n'est pas obligatoire, "
                           "mais leurs auteurs sont remerciés ici.");
        for (const Credits::Entry& entry : credits_.models) {
            draw_credit(entry);
        }
        ImGui::End();
    }

    void draw_credit(const Credits::Entry& entry) {
        const std::string title = entry.version.empty() ? entry.name : entry.name + " " + entry.version;
        ImGui::PushID(title.c_str());
        if (ImGui::TreeNode(title.c_str())) {
            if (!entry.authors.empty()) {
                ImGui::TextWrapped("Auteurs : %s", entry.authors.c_str());
            }
            ImGui::TextWrapped("Licence : %s", entry.license.c_str());
            if (!entry.copyright.empty()) {
                ImGui::TextWrapped("%s", entry.copyright.c_str());
            }
            if (!entry.source.empty()) {
                ImGui::TextWrapped("Source : %s", entry.source.c_str());
            }
            if (!entry.url.empty()) {
                ImGui::TextDisabled("%s", entry.url.c_str());
            }
            if (!entry.file.empty()) {
                const bool present = SDL_GetPathInfo((moteur::base_path() + entry.file).c_str(), nullptr);
                if (present) {
                    ImGui::TextDisabled("Fichier présent : %s", entry.file.c_str());
                } else {
                    ImGui::TextDisabled("Fichier absent : python tools/models/fetch_test_models.py le télécharge");
                }
            }
            if (!entry.license_file.empty() && ImGui::TreeNode("Texte de la licence")) {
                auto found = license_texts_.find(entry.license_file);
                if (found == license_texts_.end()) {
                    std::string text;
                    try {
                        text = moteur::read_text_file(moteur::base_path() + entry.license_file);
                    } catch (const std::exception& e) {
                        text = std::string("Texte introuvable : ") + e.what();
                    }
                    found = license_texts_.emplace(entry.license_file, std::move(text)).first;
                }
                ImGui::BeginChild("license", ImVec2(0.0f, 220.0f), ImGuiChildFlags_Borders);
                ImGui::PushTextWrapPos(0.0f);  // license files often hold a paragraph per line
                ImGui::TextUnformatted(found->second.c_str(), found->second.c_str() + found->second.size());
                ImGui::PopTextWrapPos();
                ImGui::EndChild();
                ImGui::TreePop();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    // Every test scene, its description, its settings and a button to start it.
    void draw_tests() {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::Begin("Tests moteur", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoBringToFrontOnFocus);
        if (ImGui::Button("< Accueil")) {
            request(Action::ShowHome);
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("Tests moteur : choisir une scène. Échap pendant un test revient ici.");
        if (!error_.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.4f, 1.0f));
            ImGui::TextWrapped("Le test n'a pas pu démarrer : %s", error_.c_str());
            ImGui::PopStyleColor();
        }

        for (std::size_t i = 0; i < tests_.size(); ++i) {
            TestEntry& test = tests_[i];
            TestScene::Options& options = test.options;
            ImGui::PushID(static_cast<int>(i));
            ImGui::Spacing();
            ImGui::SeparatorText(test.name);
            ImGui::TextWrapped("%s", test.description);
            ImGui::PushItemWidth(260.0f);
            switch (test.kind) {
                case TestKind::Sprites:
                    ImGui::SliderInt("Petits sprites", &options.movers, 0, 80000);
                    ImGui::Checkbox("Batching", &options.batching);
                    ImGui::SameLine();
                    ImGui::Checkbox("Tri en profondeur", &options.depth);
                    ImGui::SameLine();
                    ImGui::Checkbox("Grand sprite immobile", &options.still);
                    break;
                case TestKind::Iso:
                    ImGui::SliderInt("Taille de la carte", &options.map_size, 1, 400);
                    ImGui::SliderFloat("Zoom", &options.zoom, 1.0f, kMaxZoom, "%.0f");
                    ImGui::Checkbox("Alterner les textures tuile par tuile (casse le batching)", &options.interleave);
                    break;
                case TestKind::Demo:
                    ImGui::SliderInt("Créatures", &options.movers, 0, 20000);
                    ImGui::SliderInt("Taille de la carte", &options.map_size, 10, 400);
                    ImGui::InputScalar("Graine", ImGuiDataType_U32, &options.seed);
                    break;
                case TestKind::Render3D:
                    ImGui::Checkbox("Commencer en orthographique", &options.orthographic);
                    break;
                case TestKind::Atlas:
                case TestKind::Text:
                    break;  // nothing to set
            }
            ImGui::PopItemWidth();
            if (ImGui::Button("Lancer")) {
                request(Action::Launch, static_cast<int>(i));
            }
            ImGui::PopID();
        }
        ImGui::End();
    }

    // A small panel over the running test, in the bottom left corner, which the scenes leave free.
    void draw_running_panel() {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 10.0f, viewport->WorkPos.y + viewport->WorkSize.y - 10.0f),
                                ImGuiCond_Always, ImVec2(0.0f, 1.0f));
        ImGui::SetNextWindowBgAlpha(0.85f);
        ImGui::Begin("Test en cours", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
        ImGui::Text("Test en cours : %s", tests_[static_cast<std::size_t>(running_)].name);
        ImGui::TextDisabled("%.0f FPS - Échap pour arrêter", static_cast<double>(ImGui::GetIO().Framerate));
        if (scene_) {
            scene_->draw_controls();
        }
        if (ImGui::Button("Arrêter le test")) {
            request(Action::ShowTests);
        }
        ImGui::SameLine();
        if (ImGui::Button("Accueil")) {
            request(Action::ShowHome);
        }
        ImGui::End();
    }

    moteur::Application& app_;
    double run_seconds_;  // > 0 quits by itself, for smoke tests
    double elapsed_ = 0.0;
    std::vector<TestEntry> tests_;
    Screen screen_ = Screen::Home;
    std::unique_ptr<TestScene> scene_;  // the running test, if any
    int running_ = -1;                  // its index in tests_
    Action action_ = Action::None;      // asked by the interface, done by the next update()
    int action_test_ = -1;
    std::string error_;                 // why the last test could not start
    Credits credits_;
    bool about_open_ = false;
    std::map<std::string, std::string> license_texts_;  // loaded when first shown
};

}  // namespace

int main(int argc, char** argv) {
    // Without arguments (or with --menu), the program opens on its menu. With scene options, it runs
    // that scene directly, without any interface: that is what scripts and measurements use.
    bool menu = argc == 1;
    TestScene::Options options;
    bool vsync = true;
    bool report = false;
    bool map_explicit = false;
    bool sprites_explicit = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const bool has_one = i + 1 < argc;
        const bool has_two = i + 2 < argc;
        if (arg == "--run-seconds" && has_one) {
            options.run_seconds = std::strtod(argv[i + 1], nullptr);
        } else if (arg == "--sprites" && has_one) {
            options.movers = std::atoi(argv[i + 1]);
            sprites_explicit = true;
        } else if (arg == "--freeze-after" && has_one) {
            options.freeze_after_ticks = std::atol(argv[i + 1]);
        } else if (arg == "--map" && has_one) {
            options.map_size = std::max(1, std::atoi(argv[i + 1]));
            map_explicit = true;
        } else if (arg == "--zoom" && has_one) {
            options.zoom = std::max(0.1f, static_cast<float>(std::strtod(argv[i + 1], nullptr)));
        } else if (arg == "--camera" && has_two) {
            options.has_camera = true;
            options.camera = {static_cast<float>(std::strtod(argv[i + 1], nullptr)),
                              static_cast<float>(std::strtod(argv[i + 2], nullptr))};
        } else if (arg == "--mouse" && has_two) {
            options.fake_mouse = true;
            options.fake_mouse_position = {static_cast<float>(std::strtod(argv[i + 1], nullptr)),
                                           static_cast<float>(std::strtod(argv[i + 2], nullptr))};
        } else if (arg == "--iso") {
            options.iso = true;
        } else if (arg == "--demo") {
            options.demo = true;
        } else if (arg == "--seed" && has_one) {
            options.seed = static_cast<std::uint32_t>(std::strtoul(argv[i + 1], nullptr, 10));
        } else if (arg == "--capture" && has_one) {
            options.capture_path = argv[i + 1];
        } else if (arg == "--atlas") {
            options.atlas = true;
        } else if (arg == "--3d") {
            options.render3d = true;
        } else if (arg == "--ortho") {
            options.orthographic = true;
        } else if (arg == "--render-scale" && has_one) {
            options.render_scale = static_cast<float>(std::strtod(argv[i + 1], nullptr));
        } else if (arg == "--view-height" && has_one) {
            options.view_height = static_cast<float>(std::strtod(argv[i + 1], nullptr));
        } else if (arg == "--text") {
            options.text = true;
        } else if (arg == "--interleave") {
            options.interleave = true;
        } else if (arg == "--no-input") {
            options.no_input = true;
        } else if (arg == "--no-vsync") {
            vsync = false;
        } else if (arg == "--no-batching") {
            options.batching = false;
        } else if (arg == "--depth") {
            options.depth = true;
        } else if (arg == "--still") {
            options.still = true;
        } else if (arg == "--report") {
            report = true;
        } else if (arg == "--menu") {
            menu = true;
        }
    }
    if (options.demo) {
        if (!map_explicit) {
            options.map_size = 100;
        }
        if (!sprites_explicit) {
            options.movers = 3000;
        }
    }

    std::cout << "SDL " << moteur::sdl_version() << '\n';

    try {
        moteur::ApplicationConfig config;
        config.title = "bac a sable";
        config.vsync = vsync;
        config.report_performance = report;
        if (menu) {
            config.debug_ui = true;
            config.debug_ui_font = moteur::asset_path("fonts/Inter-Regular.ttf");  // accents
        }

        moteur::Application app(config);
        if (menu) {
            Sandbox sandbox(app, options.run_seconds);
            app.run(sandbox);
            return 0;
        }
        TestScene game(app, options, true);
        app.run(game);

        std::cout << "simulated " << game.elapsed() << " s in " << game.ticks() << " ticks, "
                  << game.frames() << " frames\n";
        if (options.iso) {
            if (game.has_hovered_tile()) {
                std::cout << "hovered tile: " << game.hovered_tile().x << " " << game.hovered_tile().y << '\n';
            } else {
                std::cout << "hovered tile: none\n";
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
