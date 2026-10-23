#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "moteur/aabb.hpp"
#include "moteur/asset_cache.hpp"
#include "moteur/audio.hpp"
#include "moteur/sound_bank.hpp"

namespace moteur {

class Assets;

/// An event playing (AudioEvents::play()); 0 is none. Stays valid (and harmless) after it ended.
using EventId = std::uint32_t;
/// No event: what AudioEvents::play() returns when it cannot play.
constexpr EventId kNoEvent = 0;

/// How to play an event.
struct EventOptions {
    std::optional<glm::vec3> position;  ///< in the world (a spatial event); none: at the listener
    /// Its local parameters at the start: numbers, and labels.
    std::vector<std::pair<std::string, float>> parameters;
    std::vector<std::pair<std::string, std::string>> labels;  ///< labelled parameters, by label
};

/// A zone of the world with its reverberation (a ReverbDescription of the bank): heard when the
/// listener is inside, faded over `fade` metres at its edges.
struct ReverbZone {
    std::string reverb;  ///< a name of the bank's "reverbs"
    Aabb box;            ///< where it is heard fully
    float fade = 2.0f;   ///< metres
};

/// The sounds of a game as events (milestone 9 bis, part 7), modelled on FMOD Studio: the game
/// plays "combat/epee/impact", never a file; the bank (data, FORMATS 25) says which sounds, how
/// varied, on which bus, how far they carry, what parameters change them. Buses, VCAs and
/// snapshots mix them; zones give the place's reverberation; the geometry muffles what it hides.
/// A later migration to FMOD would replace this class's insides, not the game's calls.
///
/// ```
/// moteur::AudioEvents events(app.audio(), app.assets());
/// events.load("audio/banks/tranche.bank.json");
/// events.set_global("sol", "dalle");                        // the hero walks on slabs
/// events.play("personnage/pas", {.position = foot});        // a footstep, its set chosen by "sol"
/// const auto music = events.play("musique/tranche");
/// events.set_parameter(music, "section", "combat");         // changes on the next bar
/// events.start_snapshot("pause");                            // the music lower, the world paused
/// // Each frame, after the listener was set:
/// events.update(dt);
/// ```
///
/// Presentation only: the game's logic never reads it (a replay may play the events again, it
/// never depends on them). Main thread.
class AudioEvents {
public:
    /// What muffles a sound: 0 when nothing is between `listener` and `source`, 1 when a wall is
    /// (a ray through the level's collision, AudioEvents::set_occlusion_test()).
    using OcclusionTest = std::function<float(glm::vec3 listener, glm::vec3 source)>;

    /// Where the bank and its sounds come from: the asset manager in a game, anything in a test.
    struct Loaders {
        std::function<Asset<SoundBank>(const std::string&)> bank;  ///< a `.bank.json` (throws)
        std::function<Asset<Sound>(const std::string&)> sound;     ///< a sound held in memory
        std::function<Asset<Music>(const std::string&)> music;  ///< a streamed file
    };

    /// Takes the bank and its sounds from `assets` (hot reloaded).
    AudioEvents(Audio& audio, Assets& assets);
    /// Takes them from `loaders` (a test's sounds, a tool's).
    AudioEvents(Audio& audio, Loaders loaders);
    ~AudioEvents();
    AudioEvents(const AudioEvents&) = delete;
    AudioEvents& operator=(const AudioEvents&) = delete;

    /// Loads a bank (Assets::sound_bank()), its buses into the Audio, its sounds. A bank edited
    /// while the game runs is taken at the next update(). Throws std::runtime_error if it cannot be
    /// read or is not valid. One bank at a time (another call replaces it).
    void load(const std::string& bank);
    /// The bank loaded; null before load().
    const SoundBank* bank() const;
    /// Whether the bank has the event `name`.
    bool has(std::string_view name) const;

    /// Plays the event `name`. kNoEvent if there is no such event (logged once), if its instance
    /// limit refuses it, or without an audio output.
    EventId play(std::string_view name, const EventOptions& options = {});
    /// Stops an instance, fading out as its event says (`fade`), or at once.
    void stop(EventId event, bool fade = true);
    /// Every instance (of the events whose name starts with `prefix`).
    void stop_all(std::string_view prefix = {}, bool fade = true);
    /// Whether the instance plays (not ended, refused, stolen or stopping).
    bool playing(EventId event) const;
    /// Moves a spatial instance (a sound following its source).
    void set_position(EventId event, glm::vec3 position);
    /// A local parameter of an instance (a number, or a label of a labelled parameter); a music's
    /// section parameter changes its section on the next bar (or beat).
    void set_parameter(EventId event, std::string_view name, float value);
    /// The same, by label; an unknown label changes nothing.
    void set_parameter(EventId event, std::string_view name, std::string_view label);
    /// A global parameter of the bank (for every instance that has no local one of that name).
    void set_global(std::string_view name, float value);
    /// The same, by label ("sol": "dalle"); an unknown label changes nothing.
    void set_global(std::string_view name, std::string_view label);
    float global(std::string_view name) const;  ///< its value (a label's index); 0 if none

    /// Snapshots: the bank's bus settings, faded in while active (the pause lowers the music).
    void start_snapshot(std::string_view name);
    /// Fades the snapshot out.
    void stop_snapshot(std::string_view name);
    /// Whether it was started (and not stopped).
    bool snapshot_active(std::string_view name) const;
    /// A VCA of the bank: a gain over its buses (1: as mixed).
    void set_vca(std::string_view name, float volume);
    float vca(std::string_view name) const;  ///< its gain; 1 if none

    /// The zones of the place (a level's): the listener's zone gives the reverberation.
    void set_reverb_zones(std::vector<ReverbZone> zones);
    /// How occlusion is measured (none: nothing is occluded). Called for each occluded event's
    /// instance every few updates.
    void set_occlusion_test(OcclusionTest test);

    /// Each frame, after Audio::set_listener(): automations, occlusion, snapshots' fades, zones,
    /// music transitions, ended instances, the bank reloaded. `dt`: seconds of real time.
    void update(float dt);

    /// For the debug panel.
    struct Instance {
        EventId id = kNoEvent;  ///< what play() returned
        std::string event;      ///< its event's name
        std::string bus;        ///< its event's bus
        bool spatial = false;   ///< placed in the world
        float occlusion = 0.0f; ///< 0 heard directly, 1 behind a wall (smoothed)
        std::string section;  ///< a music's
    };
    /// The instances playing.
    std::vector<Instance> instances() const;
    /// Counters, for the debug panel.
    struct Stats {
        int instances = 0; ///< playing now
        int started = 0;   ///< since load()
        int refused = 0;   ///< by instance limits
        int stolen = 0;    ///< stopped to make room for a new one
        std::string reverb;  ///< the zone's reverberation heard ("" none)
        float reverb_wet = 0.0f;  ///< how much of it, faded at the zone's edges
    };
    /// The counters now.
    Stats stats() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace moteur
