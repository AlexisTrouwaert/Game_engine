#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "moteur/animation.hpp"
#include "moteur/application.hpp"
#include "moteur/camera.hpp"
#include "moteur/font.hpp"
#include "moteur/image.hpp"
#include "moteur/iso.hpp"
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
        // A camera view-projection applies to the whole frame (see SpriteRenderer::set_view_projection):
        // there is no separate screen-space pass to draw UI in yet. As a stand-in, the desired
        // screen position is converted through the camera's inverse transform every frame, and a
        // depth far above any world content keeps the text on top. It still scales with zoom,
        // unlike real screen-space UI text; a proper UI layer is future work (see the doc).
        moteur::TextOptions stats_options;
        stats_options.color = {0.6f, 1.0f, 0.6f, 1.0f};
        stats_options.align = moteur::TextAlign::Right;
        stats_options.max_width = 360.0f;
        stats_options.depth = 1.0e6f;
        const glm::vec2 stats_anchor = camera.screen_to_world({view_size_.x - 400.0f, 20.0f});
        font_->draw(sprites, line1, stats_anchor, stats_options);
        font_->draw(sprites, line2, stats_anchor + glm::vec2(0.0f, font_->line_height()), stats_options);
        font_->draw(sprites, line3, stats_anchor + glm::vec2(0.0f, 2.0f * font_->line_height()), stats_options);
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
    Sandbox(moteur::Application& app, double run_seconds) : app_(app), run_seconds_(run_seconds) {
        // The settings each test starts with; the list page can change them before launching.
        TestScene::Options sprites;
        sprites.movers = 3000;
        TestScene::Options iso;
        iso.iso = true;
        TestScene::Options atlas;
        atlas.atlas = true;
        TestScene::Options text;
        text.text = true;
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
    }

private:
    enum class Screen { Home, Tests, Running };
    enum class Action { None, ShowHome, ShowTests, Launch };
    enum class TestKind { Sprites, Iso, Atlas, Text, Demo };

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
        if (ImGui::Button("Quitter")) {
            app_.quit();
        }
        ImGui::End();
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
