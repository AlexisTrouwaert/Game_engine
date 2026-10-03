#include "moteur/particles.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "moteur/assets.hpp"
#include "moteur/billboard_renderer.hpp"
#include "moteur/camera3d.hpp"
#include "moteur/mesh_renderer.hpp"

namespace moteur {

namespace {

[[noreturn]] void fail(const std::string& source, const std::string& message) {
    throw std::runtime_error("Effect '" + source + "': " + message);
}

glm::vec2 range_of(const nlohmann::json& json, const char* key, glm::vec2 fallback) {
    if (!json.contains(key)) {
        return fallback;
    }
    const nlohmann::json& value = json.at(key);
    if (value.is_number()) {
        const float v = value.get<float>();
        return {v, v};
    }
    return {value.at(0).get<float>(), value.at(1).get<float>()};
}

glm::vec3 vec3_of(const nlohmann::json& value) {
    return {value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>()};
}

glm::vec4 vec4_of(const nlohmann::json& value) {
    return {value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>(), value.at(3).get<float>()};
}

template <typename T, typename Read>
ParticleCurve<T> curve_of(const nlohmann::json& json, const char* key, T fallback, Read read, const std::string& source) {
    ParticleCurve<T> curve;
    if (!json.contains(key)) {
        curve.times = {0.0f};
        curve.values = {fallback};
        return curve;
    }
    const nlohmann::json& value = json.at(key);
    if (!value.is_array() || value.empty()) {
        fail(source, std::string("\"") + key + "\" needs keys [[time, value], ...]");
    }
    if (!value[0].is_array() || (value[0].size() == 2 && !value[0][0].is_number())) {
        curve.times = {0.0f};  // a single value
        curve.values = {read(value)};
        return curve;
    }
    if (value[0].size() == 2 && value[0][1].is_number() && !std::is_same_v<T, float>) {
        fail(source, std::string("\"") + key + "\": a key's value must be a color");
    }
    for (const nlohmann::json& k : value) {
        curve.times.push_back(std::clamp(k.at(0).get<float>(), 0.0f, 1.0f));
        curve.values.push_back(read(k.at(1)));
    }
    for (std::size_t i = 1; i < curve.times.size(); ++i) {
        if (curve.times[i] < curve.times[i - 1]) {
            fail(source, std::string("\"") + key + "\": times must go up");
        }
    }
    return curve;
}

float wrap_pi(float a) {
    constexpr float kTwoPi = 6.28318531f;
    return a - kTwoPi * std::floor(a / kTwoPi);
}

}  // namespace

template <typename T>
T ParticleCurve<T>::at(float t) const {
    if (times.size() == 1 || t <= times.front()) {
        return values.front();
    }
    if (t >= times.back()) {
        return values.back();
    }
    std::size_t i = 1;
    while (times[i] < t) {
        ++i;
    }
    const float span = times[i] - times[i - 1];
    const float f = span > 0.0f ? (t - times[i - 1]) / span : 1.0f;
    return values[i - 1] + (values[i] - values[i - 1]) * f;
}

template struct ParticleCurve<float>;
template struct ParticleCurve<glm::vec4>;

ParticleEffect ParticleEffect::parse(std::string_view json_text, const std::string& source) {
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(json_text);
    } catch (const nlohmann::json::exception& e) {
        fail(source, std::string("invalid JSON: ") + e.what());
    }
    ParticleEffect effect;
    effect.source_ = source;
    try {
        if (!json.is_object() || json.value("version", 0) != kVersion) {
            fail(source, "expected an object with \"version\": " + std::to_string(kVersion));
        }
        for (const nlohmann::json& e : json.at("emitters")) {
            EmitterDesc d;
            d.texture = e.value("texture", std::string());
            if (d.texture.empty()) {
                fail(source, "an emitter has no \"texture\"");
            }
            if (e.contains("frames")) {
                d.frames = {std::max(1, e.at("frames").at(0).get<int>()), std::max(1, e.at("frames").at(1).get<int>())};
            }
            d.animate = e.value("animate", false);
            d.burst = std::max(0, e.value("burst", 0));
            d.rate = std::max(0.0f, e.value("rate", 0.0f));
            d.duration = std::max(0.0f, e.value("duration", 0.0f));
            d.loop = e.value("loop", false);
            if (d.burst == 0 && (d.rate <= 0.0f || (d.duration <= 0.0f && !d.loop))) {
                fail(source, "an emitter emits nothing (no burst, or no rate and duration)");
            }
            d.lifetime = range_of(e, "lifetime", {1.0f, 1.0f});
            const std::string shape = e.value("shape", std::string("point"));
            if (shape == "point") d.shape = EmitterShape::Point;
            else if (shape == "sphere") d.shape = EmitterShape::Sphere;
            else if (shape == "disc") d.shape = EmitterShape::Disc;
            else if (shape == "cone") d.shape = EmitterShape::Cone;
            else fail(source, "unknown shape \"" + shape + "\"");
            d.radius = e.value("radius", 0.0f);
            if (e.contains("direction")) {
                const glm::vec3 direction = vec3_of(e.at("direction"));
                d.direction = glm::length(direction) > 0.0f ? glm::normalize(direction) : glm::vec3(0.0f, 1.0f, 0.0f);
            }
            d.cos_angle = std::cos(glm::radians(std::clamp(e.value("angle", 0.0f), 0.0f, 180.0f)));
            d.speed = range_of(e, "speed", {0.0f, 0.0f});
            d.gravity = e.value("gravity", 0.0f);
            d.drag = std::max(0.0f, e.value("drag", 0.0f));
            d.size = curve_of<float>(e, "size", 0.1f, [](const nlohmann::json& v) { return v.get<float>(); }, source);
            d.color = curve_of<glm::vec4>(e, "color", glm::vec4(1.0f), [](const nlohmann::json& v) { return vec4_of(v); },
                                          source);
            d.spin = range_of(e, "spin", {0.0f, 0.0f});
            d.additive = e.value("additive", false);
            const std::string facing = e.value("facing", std::string("camera"));
            if (facing == "camera") d.facing = BillboardFacing::Camera;
            else if (facing == "upright") d.facing = BillboardFacing::Upright;
            else if (facing == "flat") d.facing = BillboardFacing::Flat;
            else fail(source, "unknown facing \"" + facing + "\"");
            if (e.contains("light")) {
                const nlohmann::json& l = e.at("light");
                d.light.enabled = true;
                if (l.contains("color")) {
                    d.light.color = vec3_of(l.at("color"));
                }
                d.light.intensity = l.value("intensity", 1.0f);
                d.light.range = l.value("range", 5.0f);
                d.light.flicker = std::clamp(l.value("flicker", 0.0f), 0.0f, 1.0f);
            }
            effect.emitters_.push_back(std::move(d));
        }
        if (effect.emitters_.empty()) {
            fail(source, "no emitter");
        }
    } catch (const nlohmann::json::exception& e) {
        fail(source, e.what());
    }
    return effect;
}

bool ParticleEffect::loops() const {
    return std::any_of(emitters_.begin(), emitters_.end(), [](const EmitterDesc& e) { return e.loop; });
}

ParticleSystem::ParticleSystem(std::size_t max_particles, std::uint32_t seed)
    : max_particles_(max_particles), random_(seed != 0 ? seed : 1u) {
    particles_.reserve(std::min<std::size_t>(max_particles_, 4096));
}

float ParticleSystem::random() {
    random_ ^= random_ << 13;
    random_ ^= random_ >> 17;
    random_ ^= random_ << 5;
    return static_cast<float>(random_ >> 8) / 16777216.0f;
}

float ParticleSystem::between(glm::vec2 range) {
    return range.x + (range.y - range.x) * random();
}

ParticleSystem::Effect* ParticleSystem::find(EffectId id) {
    for (Effect& e : effects_) {
        if (e.id == id && id != kNoEffect) {
            return &e;
        }
    }
    return nullptr;
}

const ParticleSystem::Effect* ParticleSystem::find(EffectId id) const {
    return const_cast<ParticleSystem*>(this)->find(id);
}

EffectId ParticleSystem::play(const Asset<ParticleEffect>& data, glm::vec3 position, float scale) {
    std::size_t slot = effects_.size();
    for (std::size_t i = 0; i < effects_.size(); ++i) {
        if (effects_[i].id == kNoEffect) {
            slot = i;
            break;
        }
    }
    if (slot == effects_.size()) {
        effects_.emplace_back();
    }
    Effect& effect = effects_[slot];
    effect = Effect{};
    effect.id = next_id_++;
    if (next_id_ == kNoEffect) {
        next_id_ = 1;
    }
    effect.data = data;
    effect.position = position;
    effect.scale = scale;
    const std::size_t emitters = data->emitters().size();
    effect.owed.assign(emitters, 0.0f);
    effect.cycles.assign(emitters, 0);
    return effect.id;
}

void ParticleSystem::move(EffectId id, glm::vec3 position) {
    if (Effect* effect = find(id)) {
        effect->position = position;
    }
}

void ParticleSystem::stop(EffectId id) {
    if (Effect* effect = find(id)) {
        effect->stopping = true;
    }
}

void ParticleSystem::kill(EffectId id) {
    Effect* effect = find(id);
    if (effect == nullptr) {
        return;
    }
    const auto index = static_cast<std::uint32_t>(effect - effects_.data());
    std::erase_if(particles_, [index](const Particle& p) { return p.effect == index; });
    *effect = Effect{};
}

bool ParticleSystem::alive(EffectId id) const {
    return find(id) != nullptr;
}

void ParticleSystem::clear() {
    particles_.clear();
    effects_.clear();
}

void ParticleSystem::emit(Effect& effect, std::uint32_t index, std::size_t emitter, int count) {
    const EmitterDesc& d = effect.data->emitters()[emitter];
    for (int i = 0; i < count; ++i) {
        if (particles_.size() >= max_particles_) {
            refused_ += count - i;
            return;
        }
        Particle p{};
        glm::vec3 offset(0.0f);
        glm::vec3 direction = d.direction;
        // A random direction: on the unit sphere (z uniform, angle uniform).
        const float z = random() * 2.0f - 1.0f;
        const float a = random() * 6.28318531f;
        const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
        const glm::vec3 sphere(r * std::cos(a), z, r * std::sin(a));
        switch (d.shape) {
            case EmitterShape::Point:
                break;
            case EmitterShape::Sphere:
                offset = sphere * (d.radius * std::cbrt(random()));
                direction = sphere;
                break;
            case EmitterShape::Disc: {
                const float rr = d.radius * std::sqrt(random());
                offset = glm::vec3(std::cos(a) * rr, 0.0f, std::sin(a) * rr);
                break;
            }
            case EmitterShape::Cone: {
                // A direction within the cone around d.direction: cos uniform in [cos_angle, 1].
                const float c = 1.0f - random() * (1.0f - d.cos_angle);
                const float s = std::sqrt(std::max(0.0f, 1.0f - c * c));
                const glm::vec3 helper = std::abs(d.direction.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
                const glm::vec3 u = glm::normalize(glm::cross(d.direction, helper));
                const glm::vec3 v = glm::cross(d.direction, u);
                direction = d.direction * c + (u * std::cos(a) + v * std::sin(a)) * s;
                offset = direction * (d.radius * random());
                break;
            }
        }
        p.position = effect.position + offset * effect.scale;
        p.velocity = direction * (between(d.speed) * effect.scale);
        p.life = std::max(between(d.lifetime), 0.01f);
        p.rotation = random() * 6.28318531f;
        p.spin = between(d.spin);
        p.scale = effect.scale;
        p.effect = index;
        p.emitter = static_cast<std::uint16_t>(emitter);
        const int frames = d.frames.x * d.frames.y;
        p.frame = static_cast<std::uint16_t>(d.animate ? 0 : std::min(frames - 1, static_cast<int>(random() * static_cast<float>(frames))));
        particles_.push_back(p);
        ++effect.particles;
    }
}

void ParticleSystem::update(float seconds) {
    const float dt = std::clamp(seconds, 0.0f, 0.1f);
    // Emission.
    for (std::uint32_t index = 0; index < effects_.size(); ++index) {
        Effect& effect = effects_[index];
        if (effect.id == kNoEffect) {
            continue;
        }
        const std::vector<EmitterDesc>& emitters = effect.data->emitters();
        effect.owed.resize(emitters.size(), 0.0f);  // the file may have changed (hot reload)
        effect.cycles.resize(emitters.size(), 0);
        const float before = effect.time;
        effect.time += dt;
        effect.flicker = random();
        for (std::size_t e = 0; e < emitters.size(); ++e) {
            const EmitterDesc& d = emitters[e];
            if (effect.stopping) {
                continue;
            }
            const float cycle = d.duration;
            // Bursts: at the start, and at each new cycle of a looping emitter.
            const int cycle_now = d.loop && cycle > 0.0f ? static_cast<int>(effect.time / cycle) + 1 : 1;
            while (effect.cycles[e] < cycle_now && d.burst > 0) {
                emit(effect, index, e, d.burst);
                ++effect.cycles[e];
            }
            effect.cycles[e] = std::max(effect.cycles[e], cycle_now);
            // Rate: while running.
            if (d.rate > 0.0f && (d.loop || before < d.duration)) {
                const float running = d.loop ? dt : std::min(effect.time, d.duration) - before;
                effect.owed[e] += d.rate * running;
                const int count = static_cast<int>(effect.owed[e]);
                effect.owed[e] -= static_cast<float>(count);
                emit(effect, index, e, count);
            }
        }
    }

    // Motion and ageing.
    for (Particle& p : particles_) {
        const Effect& effect = effects_[p.effect];
        const std::vector<EmitterDesc>& emitters = effect.data->emitters();
        if (p.emitter >= emitters.size()) {
            p.age = p.life;  // its emitter is gone (file changed)
            continue;
        }
        const EmitterDesc& d = emitters[p.emitter];
        p.age += dt;
        p.velocity.y -= d.gravity * p.scale * dt;
        p.velocity *= std::max(0.0f, 1.0f - d.drag * dt);
        p.position += p.velocity * dt;
        p.rotation = wrap_pi(p.rotation + p.spin * dt);
    }
    for (std::size_t i = 0; i < particles_.size();) {
        if (particles_[i].age >= particles_[i].life) {
            --effects_[particles_[i].effect].particles;
            particles_[i] = particles_.back();
            particles_.pop_back();
        } else {
            ++i;
        }
    }

    // Finished effects: nothing more to emit and no particle left.
    for (Effect& effect : effects_) {
        if (effect.id == kNoEffect || effect.particles > 0) {
            continue;
        }
        bool done = effect.stopping;
        if (!done) {
            done = true;
            for (const EmitterDesc& d : effect.data->emitters()) {
                done = done && !d.loop && effect.time >= d.duration;
            }
        }
        if (done) {
            effect = Effect{};
        }
    }
}

const Texture* ParticleSystem::texture(const std::string& path, Assets& assets) {
    auto found = textures_.find(path);
    if (found == textures_.end()) {
        TextureSettings settings;
        settings.srgb = true;
        settings.mipmaps = true;
        found = textures_.emplace(path, assets.texture(path, settings)).first;
    }
    return &*found->second;
}

void ParticleSystem::draw(BillboardRenderer& billboards, Assets& assets, const Frustum* view) {
    std::vector<std::uint8_t> shown(effects_.size(), 1);
    if (view != nullptr) {
        for (std::size_t i = 0; i < effects_.size(); ++i) {
            const Effect& effect = effects_[i];
            if (effect.id == kNoEffect) {
                continue;
            }
            Aabb box;
            box.min = effect.position - glm::vec3(4.0f * effect.scale);
            box.max = effect.position + glm::vec3(4.0f * effect.scale);
            shown[i] = view->intersects(box) ? 1 : 0;
        }
    }
    for (const Particle& p : particles_) {
        if (!shown[p.effect]) {
            continue;
        }
        const std::vector<EmitterDesc>& emitters = effects_[p.effect].data->emitters();
        if (p.emitter >= emitters.size()) {
            continue;
        }
        const EmitterDesc& d = emitters[p.emitter];
        const float t = p.age / p.life;
        BillboardOptions options;
        options.color = d.color.at(t);
        options.additive = d.additive;
        options.facing = d.facing;
        options.rotation = d.facing == BillboardFacing::Upright ? 0.0f : p.rotation;
        int frame = p.frame;
        if (d.animate) {
            frame = std::min(d.frames.x * d.frames.y - 1, static_cast<int>(t * static_cast<float>(d.frames.x * d.frames.y)));
        }
        const glm::vec2 cell(1.0f / static_cast<float>(d.frames.x), 1.0f / static_cast<float>(d.frames.y));
        const glm::vec2 corner(static_cast<float>(frame % d.frames.x) * cell.x, static_cast<float>(frame / d.frames.x) * cell.y);
        options.uv_rect = glm::vec4(corner, corner + cell);
        const float size = d.size.at(t) * p.scale;
        if (size <= 0.0f || (options.color.a <= 0.0f && !d.additive)) {
            continue;
        }
        glm::vec3 position = p.position;
        if (d.facing == BillboardFacing::Flat) {
            position.y = std::max(position.y, 0.01f);
        }
        billboards.draw(*texture(d.texture, assets), position, glm::vec2(size), options);
    }
}

void ParticleSystem::add_lights(MeshRenderer& meshes, int max_lights, glm::vec3 focus) {
    struct Candidate {
        float distance2;
        PointLight light;
    };
    std::vector<Candidate> candidates;
    for (const Effect& effect : effects_) {
        if (effect.id == kNoEffect || (effect.stopping && effect.particles == 0)) {
            continue;
        }
        for (const EmitterDesc& d : effect.data->emitters()) {
            if (!d.light.enabled) {
                continue;
            }
            PointLight light;
            light.position = effect.position + glm::vec3(0.0f, 0.2f * effect.scale, 0.0f);
            light.color = d.light.color;
            light.intensity = d.light.intensity * (1.0f - d.light.flicker * effect.flicker);
            if (effect.stopping) {
                light.intensity *= 0.5f;
            }
            light.range = d.light.range * effect.scale;
            const glm::vec3 to = light.position - focus;
            candidates.push_back({glm::dot(to, to), light});
        }
    }
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate& a, const Candidate& b) { return a.distance2 < b.distance2; });
    const auto count = std::min<std::size_t>(candidates.size(), static_cast<std::size_t>(std::max(max_lights, 0)));
    for (std::size_t i = 0; i < count; ++i) {
        meshes.add_light(candidates[i].light);
    }
}

ParticleStats ParticleSystem::stats() const {
    ParticleStats stats;
    for (const Effect& effect : effects_) {
        if (effect.id == kNoEffect) {
            continue;
        }
        ++stats.effects;
        for (const EmitterDesc& d : effect.data->emitters()) {
            stats.lights += d.light.enabled ? 1 : 0;
        }
    }
    stats.particles = static_cast<int>(particles_.size());
    stats.refused = refused_;
    return stats;
}

}  // namespace moteur
