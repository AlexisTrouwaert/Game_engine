#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace moteur {

/// A curve of an automation: points (x: the parameter, y: the value), x increasing, linear between
/// them, flat beyond the ends.
struct AudioCurve {
    std::vector<glm::vec2> points;  ///< (parameter, value), x increasing
    /// The value at `x` (1 for an empty curve).
    float at(float x) const;
};

/// A parameter of a bank (key "parameters", global) or of an event (key "parameters" of the event,
/// local to each instance): a number in [min, max], or one of named labels (stored as its index).
struct AudioParameter {
    float min = 0.0f;                  ///< the lowest value (0 for a labelled one)
    float max = 1.0f;                  ///< the highest value (the last label's index for a labelled one)
    float initial = 0.0f;              ///< key "default"
    std::vector<std::string> labels;   ///< a labelled parameter (a surface: "pave", "dalle"...); empty: a number
    /// The label's index, or -1.
    int label(std::string_view name) const;
};

/// How an event's property follows a parameter (key "automation"): FMOD Studio's automation.
struct AudioAutomation {
    /// What it changes: a gain, a ratio, a cutoff in Hz.
    enum class Property { Volume, Pitch, Lowpass };
    std::string parameter;                ///< the parameter it follows (the event's, else the bank's)
    Property property = Property::Volume; ///< what it changes
    AudioCurve curve;                     ///< parameter to value
};

/// A section of an interactive music (key "music" of an event): a stream, its tempo, its layers.
struct MusicSection {
    std::string file;             ///< a music asset (streamed)
    float tempo = 120.0f;         ///< beats per minute
    int beats_per_bar = 4;        ///< what a bar is (the quantization "bar")
    /// A layer played in step with the section (a stem), its volume following a parameter.
    struct Layer {
        std::string file;            ///< a music asset (streamed)
        AudioAutomation automation;  ///< its volume
    };
    std::vector<Layer> layers;    ///< played in step with `file`
};

/// An event (key "events"): what the game plays by name ("combat/epee/impact"), never a file.
/// Modelled on FMOD Studio's events: a single instrument (sounds chosen at random, shuffled or in
/// sequence; or a labelled parameter choosing a set: a switch), variations, a bus, 3D attenuation
/// and occlusion, instance limits and stealing, automations; or an interactive music.
struct AudioEventDescription {
    /// How the next sound of a set is chosen: at random, each once in a shuffled order, in turn.
    enum class Order { Random, Shuffle, Sequence };
    /// What an instance beyond the limit does: stops the oldest, the quietest, or is refused.
    enum class Steal { Oldest, Quietest, None };
    std::string name;                                      ///< its key in "events"
    std::string bus = "effects";                           ///< the bus it plays into
    std::vector<std::string> sounds;                       ///< without a switch
    std::string switch_parameter;                          ///< a labelled parameter choosing `cases`
    std::map<std::string, std::vector<std::string>> cases; ///< by label
    Order order = Order::Random;                           ///< key "order"
    glm::vec2 volume{1.0f};  ///< a range: each play takes a gain within it
    glm::vec2 pitch{1.0f};   ///< a range: each play takes a ratio within it
    bool loop = false;       ///< plays until stopped
    bool stream = false;     ///< decoded while it plays (long sounds: ambiences); else in memory
    float fade_in = 0.0f;    ///< seconds
    float fade_out = 0.0f;   ///< seconds, when stopped
    bool spatial = false;    ///< placed in the world (key "spatial")
    float min_distance = 1.0f;   ///< full volume within it (metres)
    float max_distance = 30.0f;  ///< silent beyond it
    bool occlusion = false;  ///< muffled when the geometry is between it and the listener
    int max_instances = 0;   ///< 0: no limit
    Steal steal = Steal::Oldest;  ///< beyond `max_instances`
    int priority = 128;      ///< 0 highest, 255 lowest: what the voice limit keeps first
    std::map<std::string, AudioParameter> parameters;  ///< local to each instance
    std::vector<AudioAutomation> automation;           ///< its properties following parameters
    /// An interactive music (key "music"): sections chosen by a labelled parameter, changed on the
    /// next bar (or beat, or at once), crossfaded.
    struct Music {
        std::string parameter;  ///< labelled; its labels name the sections
        std::map<std::string, MusicSection> sections;  ///< by label
        /// When a change of section happens: on the next bar, the next beat, at once.
        enum class Quantize { Bar, Beat, Now };
        Quantize quantize = Quantize::Bar;  ///< key "quantize"
        float crossfade = 0.5f;  ///< seconds
    };
    bool is_music = false;  ///< `music` is used, not `sounds`
    Music music;            ///< key "music"
};

/// A mixing bus (key "buses"): "effects/pas" is under "effects". The base buses ("music",
/// "effects", "ambience", "interface") are the engine's, under the master: the player's volume
/// sliders are their VCAs.
struct AudioBusDescription {
    std::string name;    ///< its full name ("effects/pas")
    std::string parent;  ///< "" for a base bus
    float volume = 1.0f; ///< its mix gain
};

/// A reverberation (key "reverbs"): a room, heard in its zones (AudioEvents::set_reverb_zones()).
struct ReverbDescription {
    float room = 0.5f;     ///< size, 0 to 1: how long it rings
    float damping = 0.5f;  ///< 0 to 1: how fast the highs die
    float wet = 0.3f;      ///< 0 to 1: how much of it is heard
};

/// A snapshot (key "snapshots"): bus volumes and pauses, faded in while it is active.
struct SnapshotDescription {
    /// What it does to a bus.
    struct Bus {
        float volume = 1.0f;  ///< multiplies the bus's volume
        bool paused = false;  ///< pauses the bus while the snapshot is active
    };
    std::map<std::string, Bus> buses;  ///< by bus name
    float fade = 0.3f;  ///< seconds
};

/// A sound bank (`.bank.json`, FORMATS 25; milestone 9 bis, part 7): the events of a game, their
/// buses, VCAs, snapshots, parameters and reverbs, as data. Plain data, checked on its own (the
/// sounds are loaded by AudioEvents). Shipped as CBOR like every JSON document.
class SoundBank {
public:
    static constexpr int kVersion = 1;  ///< key "version"

    /// Reads a bank. `source` names it in messages. Throws std::runtime_error naming the bank and
    /// the place: bad JSON, version, an unknown key (a typo is an error), a bus whose parent is not
    /// declared, an event's bus, switch or automation parameter that does not exist, a range
    /// upside down, a music without sections.
    static SoundBank parse(std::string_view text, const std::string& source);

    /// The name given to parse() (its asset path).
    const std::string& source() const { return source_; }
    /// A number of its own, different for every bank parsed (AudioEvents sees a reload by it).
    std::uint64_t id() const { return id_; }
    /// Every event, by name.
    const std::map<std::string, AudioEventDescription, std::less<>>& events() const { return events_; }
    /// The event `name`, or null.
    const AudioEventDescription* event(std::string_view name) const;
    /// The buses it declares, parents before children (the base ones are not listed).
    const std::vector<AudioBusDescription>& buses() const { return buses_; }
    const std::map<std::string, std::vector<std::string>>& vcas() const { return vcas_; }  ///< name: buses
    /// Its snapshots, by name.
    const std::map<std::string, SnapshotDescription>& snapshots() const { return snapshots_; }
    const std::map<std::string, AudioParameter>& parameters() const { return parameters_; }  ///< global
    /// Its reverberations, by name (ReverbZone::reverb).
    const std::map<std::string, ReverbDescription>& reverbs() const { return reverbs_; }
    /// The base buses whose sounds the zones' reverberation colours (key "reverb_buses").
    const std::vector<std::string>& reverb_buses() const { return reverb_buses_; }
    /// Every sound and music file it names (to load, and for the cook).
    std::vector<std::string> files() const;

    /// The base buses: they always exist.
    static bool is_base_bus(std::string_view name);

private:
    std::string source_;
    std::uint64_t id_ = 0;
    std::map<std::string, AudioEventDescription, std::less<>> events_;
    std::vector<AudioBusDescription> buses_;
    std::map<std::string, std::vector<std::string>> vcas_;
    std::map<std::string, SnapshotDescription> snapshots_;
    std::map<std::string, AudioParameter> parameters_;
    std::map<std::string, ReverbDescription> reverbs_;
    std::vector<std::string> reverb_buses_;
};

/// What `--check-data` says of the banks: every `.bank.json` under the `audio/` folder of each
/// assets root read and checked (SoundBank::parse()). `errors`: "audio/banks/x.bank.json: what";
/// `warnings`: a sound it names that is in no root (the sounds are downloaded apart:
/// tools/audio/fetch_test_sounds.py; the cook refuses a missing one). Returns the banks read.
int check_sound_banks(const std::vector<std::string>& roots, std::vector<std::string>& errors,
                      std::vector<std::string>& warnings);

}  // namespace moteur
