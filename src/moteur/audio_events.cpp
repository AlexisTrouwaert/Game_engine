#include "moteur/audio_events.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <utility>

#include "moteur/assets.hpp"

namespace moteur {

namespace {

constexpr float kOpenCutoff = 22000.0f;     // Hz: no filtering
constexpr float kOccludedCutoff = 900.0f;   // Hz: behind a wall
constexpr float kOccludedGain = 0.45f;      // what a wall leaves of the volume
constexpr float kOcclusionGlide = 8.0f;     // per second: how fast it follows the test
constexpr int kOcclusionEvery = 4;          // an instance is tested every this many updates
constexpr float kScheduleAhead = 0.05f;     // seconds: music sections start this far ahead, together

int base_group_index(const std::string& bus) {
    const std::string root = bus.substr(0, bus.find('/'));
    if (root == "music") return 0;
    if (root == "ambience") return 2;
    if (root == "interface") return 3;
    return 1;  // effects
}

}  // namespace

struct AudioEvents::State {
    Audio& audio;
    Loaders loaders;
    Asset<SoundBank> bank;
    std::uint64_t bank_seen = 0;  // the bank's content the state was built for (a reload changes it)
    std::map<std::string, int, std::less<>> buses;  // bank name -> Audio bus
    std::map<std::string, Asset<Sound>, std::less<>> sounds;
    std::map<std::string, Asset<Music>, std::less<>> streams;
    std::map<std::string, float, std::less<>> globals;
    std::map<std::string, float, std::less<>> vcas;
    struct Snapshot {
        bool active = false;
        float weight = 0.0f;  // 0 to 1, faded
    };
    std::map<std::string, Snapshot, std::less<>> snapshots;
    std::vector<ReverbZone> zones;
    OcclusionTest occlusion;
    std::set<std::string, std::less<>> unknown;  // events asked for that do not exist (logged once)
    std::map<std::string, std::size_t, std::less<>> sequence;  // per event: its next sound (sequence, shuffle)
    std::map<std::string, std::vector<std::size_t>, std::less<>> bags;  // shuffle: the order left
    std::uint32_t random = 0x2545F491u;
    EventId next_id = 1;
    std::uint64_t next_order = 1;
    std::uint64_t updates = 0;
    Stats stats;

    struct Running {
        EventId id = kNoEvent;
        const AudioEventDescription* event = nullptr;
        std::string name;
        std::vector<SoundId> sounds;  // the instance's voice, or a music's section and its layers
        std::vector<int> layer_of;    // per sound: -1 the section, else its layer's index
        std::map<std::string, float, std::less<>> parameters;  // local
        std::optional<glm::vec3> position;
        float base_volume = 1.0f;  // its random variation
        float base_pitch = 1.0f;
        float occlusion = 0.0f;    // smoothed
        float occlusion_target = 0.0f;
        bool dirty = true;         // automations to apply
        std::uint64_t order = 0;
        bool stopping = false;
        // A music: its section, and the one it goes to.
        std::string section;
    };
    std::vector<Running> running;

    State(Audio& a, Loaders l) : audio(a), loaders(std::move(l)) {}

    float uniform() {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        return static_cast<float>(random >> 8) / 16777216.0f;
    }

    // A parameter's value for an instance: its own, the bank's global, the definition's default.
    float value(const Running& r, std::string_view name) const {
        if (const auto local = r.parameters.find(name); local != r.parameters.end()) return local->second;
        if (const auto global = globals.find(name); global != globals.end()) return global->second;
        return 0.0f;
    }
    const AudioParameter* definition(const AudioEventDescription& e, std::string_view name) const {
        if (const auto local = e.parameters.find(std::string(name)); local != e.parameters.end()) return &local->second;
        const auto& globals_defined = bank->parameters();
        if (const auto global = globals_defined.find(std::string(name)); global != globals_defined.end()) return &global->second;
        return nullptr;
    }

    void build() {
        // The bank's buses (parents first), the global parameters, the sounds.
        buses.clear();
        for (const char* name : {"music", "effects", "ambience", "interface"}) {
            buses[name] = audio.find_bus(name);
        }
        for (const AudioBusDescription& b : bank->buses()) {
            const int index = audio.add_bus(b.name, buses.at(b.parent));
            buses[b.name] = index;
        }
        for (const auto& [name, p] : bank->parameters()) {
            if (globals.count(name) == 0) {
                globals[name] = p.initial;
            }
        }
        for (const auto& [name, s] : bank->snapshots()) {
            snapshots.try_emplace(name);
        }
        for (const auto& [name, list] : bank->vcas()) {
            vcas.try_emplace(name, 1.0f);
        }
        sounds.clear();
        streams.clear();
        for (const auto& [name, e] : bank->events()) {
            const auto add = [&](const std::string& file, bool stream) {
                try {
                    if (stream) {
                        streams.try_emplace(file, loaders.music(file));
                    } else {
                        sounds.try_emplace(file, loaders.sound(file));
                    }
                } catch (const std::exception& error) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Audio events: '%s': %s", name.c_str(), error.what());
                }
            };
            for (const std::string& f : e.sounds) add(f, e.stream);
            for (const auto& [label, list] : e.cases) {
                for (const std::string& f : list) add(f, e.stream);
            }
            for (const auto& [label, section] : e.music.sections) {
                add(section.file, true);
                for (const MusicSection::Layer& layer : section.layers) add(layer.file, true);
            }
        }
        bank_seen = bank->id();
        apply_mix();
    }

    // Bus volumes: the bank's, the VCAs', the snapshots' (faded), and their pauses.
    void apply_mix() {
        std::map<std::string, float, std::less<>> volume;
        std::map<std::string, bool, std::less<>> paused;
        for (const auto& [name, index] : buses) {
            volume[name] = 1.0f;
            paused[name] = false;
        }
        for (const AudioBusDescription& b : bank->buses()) {
            volume[b.name] *= b.volume;
        }
        for (const auto& [name, list] : bank->vcas()) {
            const float v = vcas.count(name) != 0 ? vcas.at(name) : 1.0f;
            for (const std::string& b : list) volume[b] *= v;
        }
        for (const auto& [name, s] : bank->snapshots()) {
            const Snapshot& state = snapshots[name];
            for (const auto& [b, settings] : s.buses) {
                volume[b] *= 1.0f + (settings.volume - 1.0f) * state.weight;
                paused[b] = paused[b] || (state.active && settings.paused);
            }
        }
        for (const auto& [name, index] : buses) {
            audio.set_bus_volume(index, volume[name]);
            audio.set_bus_paused(index, paused[name]);
        }
    }

    // The sounds to choose from, and the key of their sequence (the event, and the switch's label).
    const std::vector<std::string>* sound_set(const AudioEventDescription& e, const Running& r, std::string& key) const {
        key = e.name;
        if (e.switch_parameter.empty()) {
            return &e.sounds;
        }
        const AudioParameter* p = definition(e, e.switch_parameter);
        const auto index = static_cast<std::size_t>(std::lround(value(r, e.switch_parameter)));
        if (p == nullptr || index >= p->labels.size()) {
            return nullptr;
        }
        key += "#" + p->labels[index];
        const auto found = e.cases.find(p->labels[index]);
        return found != e.cases.end() ? &found->second : nullptr;
    }

    std::size_t pick(const AudioEventDescription& e, std::size_t count, const std::string& key) {
        if (count <= 1) {
            return 0;
        }
        switch (e.order) {
            case AudioEventDescription::Order::Sequence:
                return sequence[key]++ % count;
            case AudioEventDescription::Order::Shuffle: {
                // Each sound once per round, in a random order; never the same one twice in a row.
                std::vector<std::size_t>& bag = bags[key];
                if (bag.empty()) {
                    for (std::size_t i = 0; i < count; ++i) bag.push_back(i);
                    for (std::size_t i = count - 1; i > 0; --i) {
                        std::swap(bag[i], bag[static_cast<std::size_t>(uniform() * static_cast<float>(i + 1)) % (i + 1)]);
                    }
                    if (bag.back() == sequence[key] && count > 1) std::swap(bag.front(), bag.back());
                }
                const std::size_t chosen = bag.back();
                bag.pop_back();
                sequence[key] = chosen;
                return chosen;
            }
            case AudioEventDescription::Order::Random:
                break;
        }
        return std::min(count - 1, static_cast<std::size_t>(uniform() * static_cast<float>(count)));
    }

    // The automations' values for an instance (volume and pitch multiply, lowpass the lowest).
    void automations(const Running& r, float& volume, float& pitch, float& lowpass) const {
        volume = 1.0f;
        pitch = 1.0f;
        lowpass = kOpenCutoff;
        for (const AudioAutomation& a : r.event->automation) {
            const float v = a.curve.at(value(r, a.parameter));
            switch (a.property) {
                case AudioAutomation::Property::Volume: volume *= std::max(0.0f, v); break;
                case AudioAutomation::Property::Pitch: pitch *= std::max(0.05f, v); break;
                case AudioAutomation::Property::Lowpass: lowpass = std::min(lowpass, std::max(20.0f, v)); break;
            }
        }
    }

    PlaySound options_for(const Running& r) const {
        const AudioEventDescription& e = *r.event;
        PlaySound o;
        o.bus = buses.count(e.bus) != 0 ? buses.at(e.bus) : -1;
        o.group = static_cast<SoundGroup>(base_group_index(e.bus));
        float volume = 1.0f, pitch = 1.0f, lowpass = kOpenCutoff;
        automations(r, volume, pitch, lowpass);
        o.volume = r.base_volume * volume;
        o.pitch = r.base_pitch * pitch;
        o.loop = e.loop;
        o.fade_in = e.fade_in;
        o.priority = 255 - e.priority;  // the bank's 0 is the highest
        if (e.spatial && r.position) {
            o.position = r.position;
            Attenuation a;
            a.min_distance = e.min_distance;
            a.max_distance = e.max_distance;
            o.attenuation = a;
        }
        o.filtered = e.occlusion || std::any_of(e.automation.begin(), e.automation.end(), [](const AudioAutomation& a) {
                         return a.property == AudioAutomation::Property::Lowpass;
                     });
        o.lowpass = lowpass;
        return o;
    }

    void start_section(Running& r, const std::string& label, std::uint64_t at, float fade) {
        const auto found = r.event->music.sections.find(label);
        if (found == r.event->music.sections.end()) {
            return;
        }
        const MusicSection& section = found->second;
        PlaySound o = options_for(r);
        o.loop = true;
        o.start_at = at;
        o.fade_in = fade;
        const auto play_stream = [&](const std::string& file, int layer, float volume) {
            const auto stream = streams.find(file);
            if (stream == streams.end() || !stream->second) {
                return;
            }
            PlaySound lo = o;
            lo.volume = o.volume * volume;
            const SoundId id = audio.play_stream(stream->second, lo);
            if (id != kNoSound) {
                r.sounds.push_back(id);
                r.layer_of.push_back(layer);
            }
        };
        play_stream(section.file, -1, 1.0f);
        for (std::size_t i = 0; i < section.layers.size(); ++i) {
            const MusicSection::Layer& layer = section.layers[i];
            play_stream(layer.file, static_cast<int>(i), layer.automation.curve.at(value(r, layer.automation.parameter)));
        }
        r.section = label;
    }

    // A music's section changes: on the next bar (or beat, or at once), crossfaded.
    void change_section(Running& r, const std::string& label) {
        if (label == r.section || r.event->music.sections.count(label) == 0) {
            return;
        }
        const AudioEventDescription::Music& m = r.event->music;
        const MusicSection& current = m.sections.at(r.section);
        const int rate = audio.sample_rate();
        std::uint64_t at = audio.time_frames() + static_cast<std::uint64_t>(kScheduleAhead * static_cast<float>(rate));
        const std::optional<double> position = r.sounds.empty() ? std::nullopt : audio.stream_seconds(r.sounds.front());
        if (position && m.quantize != AudioEventDescription::Music::Quantize::Now) {
            const double beat = 60.0 / static_cast<double>(current.tempo);
            const double step = m.quantize == AudioEventDescription::Music::Quantize::Bar ? beat * current.beats_per_bar : beat;
            const double next = std::ceil((*position + kScheduleAhead) / step) * step;
            at = audio.time_frames() + static_cast<std::uint64_t>(std::max(0.0, next - *position) * rate);
        }
        const auto fade_frames = static_cast<std::uint64_t>(m.crossfade * static_cast<float>(rate));
        for (const SoundId id : r.sounds) {
            audio.stop_at(id, at + fade_frames, m.crossfade);
        }
        r.sounds.clear();
        r.layer_of.clear();
        start_section(r, label, at, m.crossfade);
    }

    void apply(Running& r) {
        // Automations and occlusion, on every voice of the instance.
        float volume = 1.0f, pitch = 1.0f, lowpass = kOpenCutoff;
        automations(r, volume, pitch, lowpass);
        const float occluded = 1.0f - (1.0f - kOccludedGain) * r.occlusion;
        lowpass = std::min(lowpass, kOpenCutoff + (kOccludedCutoff - kOpenCutoff) * r.occlusion);
        for (std::size_t i = 0; i < r.sounds.size(); ++i) {
            float v = r.base_volume * volume * occluded;
            if (r.event->is_music && r.layer_of[i] >= 0) {
                const MusicSection& s = r.event->music.sections.at(r.section);
                const MusicSection::Layer& layer = s.layers[static_cast<std::size_t>(r.layer_of[i])];
                v *= layer.automation.curve.at(value(r, layer.automation.parameter));
            }
            audio.set_volume(r.sounds[i], v);
            audio.set_pitch(r.sounds[i], r.base_pitch * pitch);
            audio.set_lowpass(r.sounds[i], lowpass >= kOpenCutoff ? 0.0f : lowpass);
        }
        r.dirty = false;
    }

    Running* find(EventId id) {
        for (Running& r : running) {
            if (r.id == id) return &r;
        }
        return nullptr;
    }

    void stop(Running& r, bool fade) {
        for (const SoundId id : r.sounds) {
            audio.stop(id, fade ? r.event->fade_out : 0.0f);
        }
        r.stopping = true;
    }
};

AudioEvents::AudioEvents(Audio& audio, Assets& assets)
    : AudioEvents(audio, Loaders{[&assets](const std::string& path) { return assets.sound_bank(path); },
                                 [&assets](const std::string& path) { return assets.sound(path); },
                                 [&assets](const std::string& path) { return assets.music(path); }}) {}

AudioEvents::AudioEvents(Audio& audio, Loaders loaders) : state_(std::make_unique<State>(audio, std::move(loaders))) {}

AudioEvents::~AudioEvents() {
    for (State::Running& r : state_->running) {
        state_->stop(r, false);
    }
}

void AudioEvents::load(const std::string& bank) {
    for (State::Running& r : state_->running) {
        state_->stop(r, true);
    }
    state_->running.clear();
    state_->bank = state_->loaders.bank(bank);  // throws
    state_->build();
}

const SoundBank* AudioEvents::bank() const {
    return state_->bank ? &*state_->bank : nullptr;
}

bool AudioEvents::has(std::string_view name) const {
    return state_->bank && state_->bank->event(name) != nullptr;
}

EventId AudioEvents::play(std::string_view name, const EventOptions& options) {
    State& s = *state_;
    if (!s.bank) {
        return kNoEvent;
    }
    const AudioEventDescription* event = s.bank->event(name);
    if (event == nullptr) {
        if (s.unknown.insert(std::string(name)).second) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Audio events: no event '%.*s' in '%s'", static_cast<int>(name.size()),
                        name.data(), s.bank->source().c_str());
        }
        return kNoEvent;
    }
    State::Running r;
    r.event = event;
    r.name = event->name;
    r.position = options.position;
    for (const auto& [p, v] : event->parameters) {
        r.parameters[p] = v.initial;
    }
    for (const auto& [p, v] : options.parameters) {
        r.parameters[p] = v;
    }
    for (const auto& [p, label] : options.labels) {
        const AudioParameter* definition = s.definition(*event, p);
        const int index = definition != nullptr ? definition->label(label) : -1;
        if (index >= 0) {
            r.parameters[p] = static_cast<float>(index);
        }
    }
    // The instance limit: refused, or the oldest or quietest one stopped.
    if (event->max_instances > 0) {
        std::vector<State::Running*> same;
        for (State::Running& other : s.running) {
            if (other.event == event && !other.stopping) same.push_back(&other);
        }
        if (static_cast<int>(same.size()) >= event->max_instances) {
            if (event->steal == AudioEventDescription::Steal::None) {
                ++s.stats.refused;
                return kNoEvent;
            }
            State::Running* victim = same.front();  // the oldest
            if (event->steal == AudioEventDescription::Steal::Quietest) {
                // The farthest from the listener is the quietest (one without position: at it).
                float farthest = -1.0f;
                for (State::Running* other : same) {
                    const float d = other->position ? glm::length(*other->position - s.audio.listener().position) : 0.0f;
                    if (d > farthest) {
                        farthest = d;
                        victim = other;
                    }
                }
            }
            s.stop(*victim, false);
            ++s.stats.stolen;
        }
    }
    r.base_volume = event->volume.x + (event->volume.y - event->volume.x) * s.uniform();
    r.base_pitch = event->pitch.x + (event->pitch.y - event->pitch.x) * s.uniform();
    r.id = s.next_id++;
    r.order = s.next_order++;
    if (event->is_music) {
        const AudioParameter* p = s.definition(*event, event->music.parameter);
        const auto index = static_cast<std::size_t>(std::lround(s.value(r, event->music.parameter)));
        const std::string label = p != nullptr && index < p->labels.size() ? p->labels[index] : event->music.sections.begin()->first;
        s.start_section(r, label, s.audio.time_frames() + static_cast<std::uint64_t>(kScheduleAhead * static_cast<float>(s.audio.sample_rate())),
                        event->fade_in);
    } else {
        std::string key;
        const std::vector<std::string>* set = s.sound_set(*event, r, key);
        if (set == nullptr || set->empty()) {
            return kNoEvent;  // a switch on a label without sounds: silent on purpose
        }
        const std::size_t chosen = s.pick(*event, set->size(), key);
        const std::string& file = (*set)[chosen];
        const PlaySound o = s.options_for(r);
        SoundId id = kNoSound;
        if (event->stream) {
            if (const auto found = s.streams.find(file); found != s.streams.end() && found->second) {
                id = s.audio.play_stream(found->second, o);
            }
        } else if (const auto found = s.sounds.find(file); found != s.sounds.end() && found->second) {
            id = s.audio.play(found->second, o);
        }
        if (id == kNoSound) {
            return kNoEvent;
        }
        r.sounds.push_back(id);
        r.layer_of.push_back(-1);
    }
    r.dirty = false;  // its options already carry the automations
    ++s.stats.started;
    const EventId id = r.id;
    s.running.push_back(std::move(r));
    return id;
}

void AudioEvents::stop(EventId event, bool fade) {
    if (State::Running* r = state_->find(event)) {
        state_->stop(*r, fade);
    }
}

void AudioEvents::stop_all(std::string_view prefix, bool fade) {
    for (State::Running& r : state_->running) {
        if (r.name.starts_with(prefix)) {
            state_->stop(r, fade);
        }
    }
}

bool AudioEvents::playing(EventId event) const {
    const State::Running* r = state_->find(event);
    if (r == nullptr || r->stopping) {
        return false;
    }
    return std::any_of(r->sounds.begin(), r->sounds.end(), [this](SoundId id) { return state_->audio.playing(id); });
}

void AudioEvents::set_position(EventId event, glm::vec3 position) {
    if (State::Running* r = state_->find(event)) {
        r->position = position;
        if (r->event->spatial) {
            for (const SoundId id : r->sounds) {
                state_->audio.set_position(id, position);
            }
        }
    }
}

void AudioEvents::set_parameter(EventId event, std::string_view name, float value) {
    if (State::Running* r = state_->find(event)) {
        r->parameters[std::string(name)] = value;
        r->dirty = true;
    }
}

void AudioEvents::set_parameter(EventId event, std::string_view name, std::string_view label) {
    State::Running* r = state_->find(event);
    if (r == nullptr) {
        return;
    }
    const AudioParameter* definition = state_->definition(*r->event, name);
    const int index = definition != nullptr ? definition->label(label) : -1;
    if (index < 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Audio events: '%s': no label '%.*s' for '%.*s'", r->name.c_str(),
                    static_cast<int>(label.size()), label.data(), static_cast<int>(name.size()), name.data());
        return;
    }
    r->parameters[std::string(name)] = static_cast<float>(index);
    r->dirty = true;
    if (r->event->is_music && name == r->event->music.parameter) {
        state_->change_section(*r, std::string(label));
    }
}

void AudioEvents::set_global(std::string_view name, float value) {
    auto found = state_->globals.find(name);
    if (found == state_->globals.end() || found->second == value) {
        return;
    }
    found->second = value;
    for (State::Running& r : state_->running) {
        r.dirty = true;
    }
}

void AudioEvents::set_global(std::string_view name, std::string_view label) {
    if (!state_->bank) {
        return;
    }
    const auto& definitions = state_->bank->parameters();
    const auto found = definitions.find(std::string(name));
    const int index = found != definitions.end() ? found->second.label(label) : -1;
    if (index >= 0) {
        set_global(name, static_cast<float>(index));
    }
}

float AudioEvents::global(std::string_view name) const {
    const auto found = state_->globals.find(name);
    return found != state_->globals.end() ? found->second : 0.0f;
}

void AudioEvents::start_snapshot(std::string_view name) {
    if (const auto found = state_->snapshots.find(name); found != state_->snapshots.end()) {
        found->second.active = true;
        state_->apply_mix();  // the pauses at once
    }
}

void AudioEvents::stop_snapshot(std::string_view name) {
    if (const auto found = state_->snapshots.find(name); found != state_->snapshots.end()) {
        found->second.active = false;
        state_->apply_mix();
    }
}

bool AudioEvents::snapshot_active(std::string_view name) const {
    const auto found = state_->snapshots.find(name);
    return found != state_->snapshots.end() && found->second.active;
}

void AudioEvents::set_vca(std::string_view name, float volume) {
    if (const auto found = state_->vcas.find(name); found != state_->vcas.end()) {
        found->second = std::max(0.0f, volume);
        state_->apply_mix();
    }
}

float AudioEvents::vca(std::string_view name) const {
    const auto found = state_->vcas.find(name);
    return found != state_->vcas.end() ? found->second : 1.0f;
}

void AudioEvents::set_reverb_zones(std::vector<ReverbZone> zones) {
    state_->zones = std::move(zones);
}

void AudioEvents::set_occlusion_test(OcclusionTest test) {
    state_->occlusion = std::move(test);
}

void AudioEvents::update(float dt) {
    State& s = *state_;
    if (!s.bank) {
        return;
    }
    ++s.updates;
    if (s.bank->id() != s.bank_seen) {
        SDL_Log("Audio events: '%s' reloaded", s.bank->source().c_str());
        for (State::Running& r : s.running) {
            s.stop(r, true);
        }
        s.running.clear();
        s.build();
    }
    // Instances whose sounds all ended go.
    std::erase_if(s.running, [&](const State::Running& r) {
        return std::none_of(r.sounds.begin(), r.sounds.end(), [&](SoundId id) { return s.audio.playing(id); });
    });

    // Snapshots fade; the mix follows.
    bool fading = false;
    for (auto& [name, snapshot] : s.snapshots) {
        const SnapshotDescription& d = s.bank->snapshots().at(name);
        const float target = snapshot.active ? 1.0f : 0.0f;
        if (snapshot.weight != target) {
            const float step = d.fade > 0.0f ? dt / d.fade : 1.0f;
            snapshot.weight = target > snapshot.weight ? std::min(target, snapshot.weight + step) : std::max(target, snapshot.weight - step);
            fading = true;
        }
    }
    if (fading) {
        s.apply_mix();
    }

    // The place's reverberation: the zone the listener is deepest in.
    const glm::vec3 listener = s.audio.listener().position;
    const ReverbZone* best = nullptr;
    float best_amount = 0.0f;
    for (const ReverbZone& zone : s.zones) {
        if (zone.box.empty()) continue;
        const glm::vec3 inside = glm::min(listener - zone.box.min, zone.box.max - listener);
        const float depth = std::min({inside.x, inside.y, inside.z});
        const float amount = zone.fade > 0.0f ? std::clamp(depth / zone.fade, 0.0f, 1.0f) : (depth >= 0.0f ? 1.0f : 0.0f);
        if (amount > best_amount) {
            best_amount = amount;
            best = &zone;
        }
    }
    ReverbSettings reverb;
    s.stats.reverb.clear();
    if (best != nullptr) {
        if (const auto preset = s.bank->reverbs().find(best->reverb); preset != s.bank->reverbs().end()) {
            reverb.room = preset->second.room;
            reverb.damping = preset->second.damping;
            reverb.wet = preset->second.wet * best_amount;
            s.stats.reverb = best->reverb;
        }
    }
    s.stats.reverb_wet = reverb.wet;
    for (const std::string& bus : s.bank->reverb_buses()) {
        s.audio.set_bus_reverb(s.audio.find_bus(bus), reverb);
    }

    // Occlusion (a few instances per update), automations.
    for (State::Running& r : s.running) {
        if (r.stopping) continue;
        if (r.event->occlusion && r.position && s.occlusion && (r.id + s.updates) % kOcclusionEvery == 0) {
            r.occlusion_target = std::clamp(s.occlusion(listener, *r.position), 0.0f, 1.0f);
        }
        if (r.occlusion != r.occlusion_target) {
            const float step = kOcclusionGlide * dt;
            r.occlusion = r.occlusion_target > r.occlusion ? std::min(r.occlusion_target, r.occlusion + step)
                                                           : std::max(r.occlusion_target, r.occlusion - step);
            r.dirty = true;
        }
        if (r.dirty) {
            s.apply(r);
        }
    }
    s.stats.instances = static_cast<int>(s.running.size());
}

std::vector<AudioEvents::Instance> AudioEvents::instances() const {
    std::vector<Instance> out;
    for (const State::Running& r : state_->running) {
        out.push_back({r.id, r.name, r.event->bus, r.event->spatial, r.occlusion, r.section});
    }
    return out;
}

AudioEvents::Stats AudioEvents::stats() const {
    return state_->stats;
}

}  // namespace moteur
