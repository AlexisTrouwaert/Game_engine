#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "moteur/asset_cache.hpp"
#include "moteur/billboard_batcher.hpp"

namespace moteur {

class Assets;
class BillboardRenderer;
class MeshRenderer;
struct Frustum;
struct Texture;

/// A value that varies over a particle's life: keys at times in [0, 1] (0 at birth, 1 at death),
/// linear between them. Before the first key it keeps the first value, after the last key the last.
template <typename T>
struct ParticleCurve {
    std::vector<float> times;  ///< ascending, in [0, 1]; at least one key
    std::vector<T> values;     ///< one per entry of `times`

    /// The value at `t` (share of the life, 0 to 1). O(number of keys).
    T at(float t) const;
};

/// Where an emitter's particles are born, and which way they leave (`"shape"` in the file).
enum class EmitterShape : std::uint8_t {
    Point,   ///< at the emitter, moving along EmitterDesc::direction
    Sphere,  ///< anywhere inside a ball of EmitterDesc::radius, moving outwards
    Disc,    ///< anywhere on a horizontal disc of EmitterDesc::radius, moving along EmitterDesc::direction
    Cone,    ///< moving in a random direction within the cone, born up to EmitterDesc::radius along it
};

/// A point light carried by an emitter (`"light"` in the file), added by ParticleSystem::add_lights().
struct EmitterLight {
    bool enabled = false;      ///< true when the emitter has a `light` object
    glm::vec3 color{1.0f};     ///< linear RGB
    float intensity = 1.0f;    ///< as PointLight::intensity
    float range = 5.0f;        ///< metres, times the effect's scale
    float flicker = 0.0f;      ///< share of the intensity that wavers (0 to 1), redrawn every update
};

/// One emitter of a ParticleEffect, as read from its JSON object (the keys are listed in
/// ParticleEffect's description). Times are seconds, distances metres, at scale 1.
struct EmitterDesc {
    std::string texture;              ///< asset path of the texture (`"texture"`), loaded sRGB with mipmaps
    glm::ivec2 frames{1, 1};          ///< columns and rows of images in the texture, each at least 1
    bool animate = false;             ///< true: the images in order over the life; false: one at random per particle
    int burst = 0;                    ///< particles at once when the emitter starts (and at each cycle if it loops)
    float rate = 0.0f;                ///< particles per second while it runs
    float duration = 0.0f;            ///< seconds it runs; 0: the burst only. For a looping emitter, the length of a cycle
    bool loop = false;                ///< runs again and again until ParticleSystem::stop()
    glm::vec2 lifetime{1.0f, 1.0f};   ///< min and max life of a particle, seconds (at least 0.01)
    EmitterShape shape = EmitterShape::Point;  ///< where particles are born
    float radius = 0.0f;              ///< size of the shape, metres
    glm::vec3 direction{0.0f, 1.0f, 0.0f};  ///< unit; axis of the cone, or direction of motion for a point or a disc
    float cos_angle = 1.0f;                  ///< cosine of the cone's half-angle (the file gives the angle in degrees)
    glm::vec2 speed{0.0f, 0.0f};      ///< min and max starting speed, m/s
    float gravity = 0.0f;             ///< m/s² downwards (negative: rises)
    float drag = 0.0f;                ///< share of the speed lost per second (>= 0)
    ParticleCurve<float> size;        ///< width and height over the life, metres (default 0.1)
    ParticleCurve<glm::vec4> color;   ///< linear color over the life, may exceed 1; a: opacity (default white)
    glm::vec2 spin{0.0f, 0.0f};       ///< min and max spin, radians per second (ignored when upright)
    bool additive = false;            ///< see BillboardOptions::additive
    BillboardFacing facing = BillboardFacing::Camera;  ///< see BillboardOptions::facing
    float soft = 0.0f;  ///< see BillboardOptions::soft
    EmitterLight light;               ///< the emitter's light, if `light.enabled`
};

/// A particle effect file, parsed and checked. Usually loaded (and hot reloaded) through
/// Assets::particle_effect(), then started with ParticleSystem::play(). Immutable once parsed.
///
/// Particle effects (milestone 6, part 7): light, numerous, purely visual effects (sparks, flames,
/// smoke, blood, dust, magic) described in JSON and drawn as billboards. They never touch the game's
/// logic: they live in real time (frames, not ticks) with their own random generator.
///
/// An effect file:
///
/// ```
/// {
///   "version": 1,
///   "emitters": [
///     {
///       "texture": "particles/spark.png",   // a texture of the assets (sRGB, premultiplied)
///       "frames": [1, 1],                    // columns and rows of images in the texture
///       "animate": false,                    // true: the images in order over the life; false: one at random
///       "burst": 24,                         // particles at once when the emitter starts
///       "rate": 0,                           // particles per second while it runs
///       "duration": 0,                       // seconds it runs (0: the burst only)
///       "loop": false,                       // runs again and again until stopped
///       "lifetime": [0.25, 0.5],             // seconds, at random between the two
///       "shape": "sphere",                   // point, sphere, disc (on the ground), cone
///       "radius": 0.05,                      // of the sphere or disc, or how far along the cone, metres
///       "direction": [0, 1, 0],              // of the cone, and of "speed" for a point or a disc
///       "angle": 60,                         // half-angle of the cone, degrees (0 to 180)
///       "speed": [2, 5],                     // m/s at the start
///       "gravity": 9.8,                      // m/s² downwards (negative: rises)
///       "drag": 2.0,                         // the speed lost per second, as a share
///       "size": [[0, 0.08], [1, 0.02]],      // metres over the life (0 to 1), linear between keys
///       "color": [[0, [6, 3, 1, 1]], [1, [2, 0.4, 0, 0]]],  // linear, may exceed 1; a: opacity
///       "spin": [-3, 3],                     // radians per second
///       "additive": true,
///       "facing": "camera",                  // camera, upright, flat
///       "soft": 0.5,                         // metres of fade before the surface behind (milestone 7); 0: hard
///       "light": { "color": [1, 0.6, 0.3], "intensity": 6, "range": 5, "flicker": 0.2 }
///     }
///   ]
/// }
/// ```
///
/// Only `version`, `emitters` and each emitter's `texture` are required; the other keys default to
/// the EmitterDesc defaults. A range (`lifetime`, `speed`, `spin`) may be a single number. A curve
/// (`size`, `color`) may be a single value instead of keys, written as a key's value (`"size": 0.2`,
/// `"color": [1, 0.5, 0, 1]`): it stays constant over the life. `scale` (ParticleSystem::play())
/// multiplies sizes, speeds, radii, gravity and the light's range.
class ParticleEffect {
public:
    /// The format version this engine reads (the file's `"version"`).
    static constexpr int kVersion = 1;

    /// Parses an effect file. `source` names it in error messages (its path, usually).
    /// Throws std::runtime_error naming `source`: invalid JSON, unknown version, shape or facing,
    /// no emitter, an emitter without texture or that emits nothing, curves that are neither a single value nor keys, or whose
    /// times go down, a negative `soft`, a value of the wrong type.
    static ParticleEffect parse(std::string_view json_text, const std::string& source);

    /// The `source` given to parse().
    const std::string& source() const { return source_; }
    /// The emitters, in file order; never empty.
    const std::vector<EmitterDesc>& emitters() const { return emitters_; }
    /// True if an emitter loops: the effect lasts until stopped.
    bool loops() const;

private:
    std::string source_;
    std::vector<EmitterDesc> emitters_;
};

/// Identifies an effect started by ParticleSystem::play(), for move(), stop(), kill() and alive().
/// An id is never reused while its effect lives; once the effect is over, calls with it do nothing.
using EffectId = std::uint32_t;
/// The id no effect has: a handy "nothing playing" value (play() never returns it).
inline constexpr EffectId kNoEffect = 0;

/// Counters of a ParticleSystem, for the debug overlay and budget tuning (ParticleSystem::stats()).
struct ParticleStats {
    int effects = 0;    ///< effects alive (emitting, or with particles left)
    int particles = 0;  ///< particles alive
    int lights = 0;     ///< emitters with a light among the living effects (before add_lights() keeps the nearest)
    long refused = 0;  ///< particles not born because the budget was full (since the start)
};

/// Runs the effects of a scene: one per scene, updated and drawn every frame. Not thread-safe:
/// use it from the thread that runs the frame.
///
/// ```
/// EffectId fire = particles.play(assets.particle_effect("effects/fire.json"), brazier);
/// particles.update(frame_seconds);               // every frame, real time
/// particles.draw(renderer.billboards(), assets, &view);  // in render(); view = camera.frustum()
/// particles.add_lights(renderer.meshes(), 8, camera.target());
/// ```
class ParticleSystem {
public:
    /// `max_particles`: the budget of the whole scene; beyond, new particles are refused (and
    /// counted in ParticleStats::refused). `seed` starts the random generator (0 counts as 1): the
    /// same seed and the same calls give the same particles.
    explicit ParticleSystem(std::size_t max_particles = 20000, std::uint32_t seed = 12345);

    /// Starts an effect at `position` (world, metres); its first particles are born at the next
    /// update(). `scale` multiplies sizes, speeds, radii, gravity and the light's range. The
    /// system keeps a reference to `effect` while it runs, so a hot reload changes it in place.
    /// An empty `effect` handle plays nothing and returns kNoEffect. O(number of effects).
    EffectId play(const Asset<ParticleEffect>& effect, glm::vec3 position, float scale = 1.0f);
    /// Where it emits from now on (an effect that follows a character or a bone). Particles
    /// already born stay where they are.
    void move(EffectId effect, glm::vec3 position);
    /// The height of the ground under the effect (metres; 0 until set): particles of a "flat"
    /// emitter lie 1 cm above it, never below. The effect's own position is not the ground (a death
    /// smoke starts at the chest); the game gives the floor's height for an effect on a raised floor
    /// or an upper level. Nothing for kNoEffect or an effect that is over.
    void set_ground(EffectId effect, float height);
    /// Stops emitting; its particles end their life. A finished effect is forgotten by itself.
    void stop(EffectId effect);
    /// Removes it and its particles at once. O(number of particles).
    void kill(EffectId effect);
    /// True while the effect emits or still has particles; false for kNoEffect and for an effect
    /// that is over, stopped and finished, or killed.
    bool alive(EffectId effect) const;
    /// Removes every effect and every particle at once (a new scene). Earlier ids are no longer alive.
    void clear();

    /// Ages and moves the particles, emits new ones, forgets finished effects. `seconds`: real time,
    /// clamped to [0, 0.1] s. O(number of particles).
    void update(float seconds);
    /// Records the particles as billboards; those of effects whose emitter is outside `view` (grown by
    /// 4 metres, times the effect's scale) are skipped when a view is given. Textures are asked of
    /// `assets` (sRGB, mipmaps) the first time a path is drawn and kept by the system afterwards.
    /// Particles of a "flat" emitter never go below 1 cm above their effect's ground (see
    /// set_ground()). Call it after BillboardRenderer::set_camera(), once per frame.
    void draw(BillboardRenderer& billboards, Assets& assets, const Frustum* view = nullptr);
    /// Adds to `meshes` the lights of the effects that have one (at most `max_lights`, the nearest
    /// to `focus`, usually what the camera looks at). Each light sits a little above its effect;
    /// a stopped effect's light is dimmed by half while its particles die. Call it every frame:
    /// MeshRenderer lights last one frame.
    void add_lights(MeshRenderer& meshes, int max_lights, glm::vec3 focus);

    /// The current counters. O(number of effects).
    ParticleStats stats() const;
    /// The `max_particles` given to the constructor.
    std::size_t budget() const { return max_particles_; }

private:
    struct Particle {
        glm::vec3 position;
        glm::vec3 velocity;
        float age;
        float life;
        float rotation;
        float spin;
        float scale;
        std::uint32_t effect;   ///< index in effects_
        std::uint16_t emitter;
        std::uint16_t frame;
    };
    struct Effect {
        EffectId id = kNoEffect;
        Asset<ParticleEffect> data;
        glm::vec3 position{0.0f};
        float ground = 0.0f;            ///< height of the ground under it (set_ground())
        float scale = 1.0f;
        float time = 0.0f;              ///< seconds since it started
        bool stopping = false;
        std::vector<float> owed;        ///< per emitter: particles due but not yet born (fractions)
        std::vector<int> cycles;        ///< per emitter: bursts done
        int particles = 0;
        float flicker = 0.0f;
    };

    float random();                 ///< [0, 1)
    float between(glm::vec2 range);
    void emit(Effect& effect, std::uint32_t index, std::size_t emitter, int count);
    const Texture* texture(const std::string& path, Assets& assets);
    Effect* find(EffectId id);
    const Effect* find(EffectId id) const;

    std::size_t max_particles_;
    std::uint32_t random_;
    EffectId next_id_ = 1;
    std::vector<Effect> effects_;   ///< a slot is free when its id is kNoEffect
    std::vector<Particle> particles_;
    std::map<std::string, Asset<Texture>> textures_;
    long refused_ = 0;
};

}  // namespace moteur
