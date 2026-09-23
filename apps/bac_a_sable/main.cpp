#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "moteur/application.hpp"
#include "moteur/camera.hpp"
#include "moteur/font.hpp"
#include "moteur/image.hpp"
#include "moteur/iso.hpp"
#include "moteur/paths.hpp"
#include "moteur/sprite_renderer.hpp"
#include "moteur/texture_atlas.hpp"
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
};

class Sandbox final : public moteur::Game {
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

    Sandbox(moteur::Application& app, const Options& options)
        : app_(app),
          texture_(app.renderer().create_texture(moteur::load_image(moteur::asset_path("sprite.png")))),
          options_(options),
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
            creature_sprite_ = &test_atlas_->region("walk_03");
            font_ = moteur::Font::load(app.renderer(), moteur::asset_path("fonts/Inter-Regular.ttf"), kFontPixelHeight);

            Random random(options.seed);
            static constexpr glm::vec2 kDirections[8] = {
                {-1.0f, -1.0f}, {-1.0f, 0.0f}, {-1.0f, 1.0f}, {0.0f, -1.0f},
                {0.0f, 1.0f}, {1.0f, -1.0f}, {1.0f, 0.0f}, {1.0f, 1.0f},
            };
            constexpr float kCreatureSpeed = 2.0f;  // tiles per second
            creatures_.reserve(static_cast<std::size_t>(options.movers));
            for (int i = 0; i < options.movers; ++i) {
                const glm::vec2 position(1.0f + random.next() * static_cast<float>(options.map_size - 2),
                                         1.0f + random.next() * static_cast<float>(options.map_size - 2));
                const glm::vec2 direction = kDirections[static_cast<std::size_t>(random.next() * 8.0f) % 8];
                const glm::vec4 tint(0.5f + 0.5f * random.next(), 0.5f + 0.5f * random.next(),
                                     0.5f + 0.5f * random.next(), 1.0f);
                creatures_.push_back({position, direction * kCreatureSpeed, tint});
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
            app_.quit();
        }
        if (!options_.iso || options_.no_input) {
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
            app_.quit();
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

    long ticks() const { return ticks_; }
    long frames() const { return frames_; }
    double elapsed() const { return elapsed_; }
    glm::ivec2 hovered_tile() const { return hovered_tile_; }
    bool has_hovered_tile() const { return has_hovered_tile_; }

private:
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

    void move_creatures(float dt) {
        const float last = static_cast<float>(options_.map_size - 1);
        for (Creature& creature : creatures_) {
            creature.position += creature.velocity * dt;
            if (creature.position.x < 0.0f || creature.position.x > last) {
                creature.velocity.x = -creature.velocity.x;
                creature.position.x = glm::clamp(creature.position.x, 0.0f, last);
            }
            if (creature.position.y < 0.0f || creature.position.y > last) {
                creature.velocity.y = -creature.velocity.y;
                creature.position.y = glm::clamp(creature.position.y, 0.0f, last);
            }
        }
    }

    // Placeholder walls: a lattice of grid lines with gaps for doorways, so the demo map looks
    // like a set of rooms without needing dedicated level data or art.
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

    // The demo scene of the part 9 milestone: the isometric tilemap of --iso, with procedural
    // walls added, several thousand creatures wandering over it, and a stats overlay. Reduced
    // scope: creatures use a static sprite instead of the 8-direction animations of part 6 (not
    // yet implemented), and walls are a tinted placeholder box, not dedicated art.
    void render_demo(moteur::Renderer& renderer, double alpha) {
        moteur::SpriteRenderer& sprites = renderer.sprites();

        camera_.set_viewport(view_size_);
        const moteur::Camera2D camera = camera_.interpolated(alpha);
        sprites.set_view_projection(camera.view_projection());

        const moteur::TileRange range = iso_.tiles_in(camera.visible_rect(), 2);
        const int last = options_.map_size - 1;
        const int first_i = std::max(range.min.x, 0), last_i = std::min(range.max.x, last);
        const int first_j = std::max(range.min.y, 0), last_j = std::min(range.max.y, last);

        // Ground: grouped by kind so both textures batch into as few draw calls as possible.
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

        // Walls: a placeholder tinted box, lifted above the ground, sorted by the same ground
        // depth as the creatures so a creature correctly passes behind or in front of one.
        moteur::SpriteOptions wall_options;
        wall_options.tint = {0.35f, 0.30f, 0.25f, 1.0f};
        for (int i = first_i; i <= last_i; ++i) {
            for (int j = first_j; j <= last_j; ++j) {
                if (is_wall(i, j)) {
                    wall_options.depth = static_cast<float>(i + j);
                    const glm::vec2 base = iso_.to_world(glm::vec2(i, j), kTileHeight * 1.5f);
                    sprites.draw(texture_, base, glm::vec2(kTileWidth * 0.5f, kTileHeight * 1.5f), wall_options);
                }
            }
        }

        // Creatures: not interpolated between ticks (same simplification as the stress-test
        // movers), sorted by tile-space depth against both the walls and each other.
        for (const Creature& creature : creatures_) {
            moteur::SpriteOptions options;
            options.tint = creature.tint;
            options.depth = creature.position.x + creature.position.y;
            sprites.draw(*creature_sprite_, iso_.to_world(creature.position), 1.0f, options);
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
        // A camera view-projection applies to the whole frame (see SpriteRenderer::set_view_projection):
        // there is no separate screen-space pass to draw UI in yet. As a stand-in, the desired
        // screen position is converted through the camera's inverse transform every frame, and a
        // depth far above any world content keeps the text on top. It still scales with zoom,
        // unlike real screen-space UI text; a proper UI layer is future work (see the doc).
        moteur::TextOptions stats_options;
        stats_options.color = {0.6f, 1.0f, 0.6f, 1.0f};
        stats_options.align = moteur::TextAlign::Right;
        stats_options.max_width = 260.0f;
        stats_options.depth = 1.0e6f;
        const glm::vec2 stats_anchor = camera.screen_to_world({view_size_.x - 300.0f, 20.0f});
        font_->draw(sprites, line1, stats_anchor, stats_options);
        font_->draw(sprites, line2, stats_anchor + glm::vec2(0.0f, font_->line_height()), stats_options);
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
    const moteur::SpriteRegion* creature_sprite_ = nullptr;
    std::vector<Creature> creatures_;
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

}  // namespace

int main(int argc, char** argv) {
    Sandbox::Options options;
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

        moteur::Application app(config);
        Sandbox game(app, options);
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
