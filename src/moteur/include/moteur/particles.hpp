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

// Particle effects (milestone 6, part 7): light, numerous, purely visual effects (sparks, flames,
// smoke, blood, dust, magic) described in JSON and drawn as billboards. They never touch the game's
// logic: they live in real time (frames, not ticks) with their own random generator.
//
// An effect file:
//
//   {
//     "version": 1,
//     "emitters": [
//       {
//         "texture": "particles/spark.png",   // a texture of the assets (sRGB, premultiplied)
//         "frames": [1, 1],                    // columns and rows of images in the texture
//         "animate": false,                    // true: the images in order over the life; false: one at random
//         "burst": 24,                         // particles at once when the emitter starts
//         "rate": 0,                           // particles per second while it runs
//         "duration": 0,                       // seconds it runs (0: the burst only)
//         "loop": false,                       // runs again and again until stopped
//         "lifetime": [0.25, 0.5],             // seconds, at random between the two
//         "shape": "sphere",                   // point, sphere, disc (on the ground), cone
//         "radius": 0.05,                      // of the sphere or disc, metres
//         "direction": [0, 1, 0],              // of the cone, and of "speed" for a point
//         "angle": 60,                         // half-angle of the cone, degrees
//         "speed": [2, 5],                     // m/s at the start
//         "gravity": 9.8,                      // m/s² downwards (negative: rises)
//         "drag": 2.0,                         // the speed lost per second, as a share
//         "size": [[0, 0.08], [1, 0.02]],      // metres over the life (0 to 1), linear between keys
//         "color": [[0, [6, 3, 1, 1]], [1, [2, 0.4, 0, 0]]],  // linear, may exceed 1; a: opacity
//         "spin": [-3, 3],                     // radians per second
//         "additive": true,
//         "facing": "camera",                  // camera, upright, flat
//         "light": { "color": [1, 0.6, 0.3], "intensity": 6, "range": 5, "flicker": 0.2 }
//       }
//     ]
//   }
template <typename T>
struct ParticleCurve {
    std::vector<float> times;  // ascending, in [0, 1]
    std::vector<T> values;

    T at(float t) const;
};

enum class EmitterShape : std::uint8_t { Point, Sphere, Disc, Cone };

struct EmitterLight {
    bool enabled = false;
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
    float range = 5.0f;
    float flicker = 0.0f;  // share of the intensity that wavers
};

struct EmitterDesc {
    std::string texture;
    glm::ivec2 frames{1, 1};
    bool animate = false;
    int burst = 0;
    float rate = 0.0f;
    float duration = 0.0f;
    bool loop = false;
    glm::vec2 lifetime{1.0f, 1.0f};
    EmitterShape shape = EmitterShape::Point;
    float radius = 0.0f;
    glm::vec3 direction{0.0f, 1.0f, 0.0f};  // unit
    float cos_angle = 1.0f;                  // of the cone's half-angle
    glm::vec2 speed{0.0f, 0.0f};
    float gravity = 0.0f;
    float drag = 0.0f;
    ParticleCurve<float> size;
    ParticleCurve<glm::vec4> color;
    glm::vec2 spin{0.0f, 0.0f};
    bool additive = false;
    BillboardFacing facing = BillboardFacing::Camera;
    EmitterLight light;
};

class ParticleEffect {
public:
    static constexpr int kVersion = 1;

    // Throws std::runtime_error naming `source`: invalid JSON, unknown version, shape or facing,
    // no emitter, an emitter without texture or that emits nothing, curves without keys.
    static ParticleEffect parse(std::string_view json_text, const std::string& source);

    const std::string& source() const { return source_; }
    const std::vector<EmitterDesc>& emitters() const { return emitters_; }
    // True if an emitter loops: the effect lasts until stopped.
    bool loops() const;

private:
    std::string source_;
    std::vector<EmitterDesc> emitters_;
};

using EffectId = std::uint32_t;
inline constexpr EffectId kNoEffect = 0;

struct ParticleStats {
    int effects = 0;
    int particles = 0;
    int lights = 0;
    long refused = 0;  // particles not born because the budget was full (since the start)
};

// Runs the effects of a scene: one per scene, updated and drawn every frame.
//
//   EffectId fire = particles.play(assets.particle_effect("effects/fire.json"), brazier);
//   particles.update(frame_seconds);               // every frame, real time
//   particles.draw(renderer.billboards(), assets, &view);  // in render()
//   particles.add_lights(renderer.meshes(), 8);
class ParticleSystem {
public:
    // `max_particles`: the budget of the whole scene; beyond, new particles are refused.
    explicit ParticleSystem(std::size_t max_particles = 20000, std::uint32_t seed = 12345);

    // Starts an effect at `position`. `scale` multiplies sizes, speeds and radii.
    EffectId play(const Asset<ParticleEffect>& effect, glm::vec3 position, float scale = 1.0f);
    // Where it emits from now on (an effect that follows a character or a bone).
    void move(EffectId effect, glm::vec3 position);
    // Stops emitting; its particles end their life. A finished effect is forgotten by itself.
    void stop(EffectId effect);
    // Removes it and its particles at once.
    void kill(EffectId effect);
    bool alive(EffectId effect) const;
    void clear();

    // Ages and moves the particles, emits new ones. `seconds`: real time, clamped to 0.1 s.
    void update(float seconds);
    // The particles as billboards; those of effects whose emitter is outside `view` (grown by a few
    // metres) are skipped when a view is given. Textures are asked of `assets` (sRGB, mipmaps).
    void draw(BillboardRenderer& billboards, Assets& assets, const Frustum* view = nullptr);
    // The lights of the effects that have one (at most `max_lights`, the nearest to `focus`).
    void add_lights(MeshRenderer& meshes, int max_lights, glm::vec3 focus);

    ParticleStats stats() const;
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
        std::uint32_t effect;   // index in effects_
        std::uint16_t emitter;
        std::uint16_t frame;
    };
    struct Effect {
        EffectId id = kNoEffect;
        Asset<ParticleEffect> data;
        glm::vec3 position{0.0f};
        float scale = 1.0f;
        float time = 0.0f;              // seconds since it started
        bool stopping = false;
        std::vector<float> owed;        // per emitter: particles due but not yet born (fractions)
        std::vector<int> cycles;        // per emitter: bursts done
        int particles = 0;
        float flicker = 0.0f;
    };

    float random();                 // [0, 1)
    float between(glm::vec2 range);
    void emit(Effect& effect, std::uint32_t index, std::size_t emitter, int count);
    const Texture* texture(const std::string& path, Assets& assets);
    Effect* find(EffectId id);
    const Effect* find(EffectId id) const;

    std::size_t max_particles_;
    std::uint32_t random_;
    EffectId next_id_ = 1;
    std::vector<Effect> effects_;   // a slot is free when its id is kNoEffect
    std::vector<Particle> particles_;
    std::map<std::string, Asset<Texture>> textures_;
    long refused_ = 0;
};

}  // namespace moteur
