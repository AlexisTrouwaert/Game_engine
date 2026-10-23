#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "moteur/asset_cache.hpp"
#include "moteur/audio_rules.hpp"
#include "moteur/sound.hpp"

namespace moteur {

class Camera3D;

/// The families of sounds, each with its volume (the player's settings) and its pause.
enum class SoundGroup { Music, Effects, Ambience, Interface };
/// How many SoundGroup values there are (they are numbered from 0).
constexpr int kSoundGroupCount = 4;
/// The group's key in the settings file: "music", "effects", "ambience" or "interface" ("?" for a
/// value out of the enum). A static string.
const char* sound_group_name(SoundGroup group);

/// How to play a sound (Audio::play).
struct PlaySound {
    SoundGroup group = SoundGroup::Effects;  ///< its volume slider and pause
    float volume = 1.0f;  ///< a gain (1: as recorded), before the group's and master volumes
    float pitch = 1.0f;             ///< 2: an octave higher (and twice as fast)
    float volume_variation = 0.0f;  ///< +/- this fraction at random (0.1: 90 % to 110 %)
    float pitch_variation = 0.0f;   ///< +/- this fraction at random: variety for repeated sounds
    std::optional<glm::vec3> position;  ///< in the world (attenuated and panned); none: as is (interface)
    int priority = 0;               ///< higher keeps its voice when there are too many
    bool loop = false;              ///< until stop(): ambiences, a burning torch
    float fade_in = 0.0f;           ///< seconds
    // Milestone 9 bis, part 7 (what the events use):
    int bus = -1;  ///< a bus of Audio::add_bus() (then `group` only names it in the debug panel); -1: the group's
    std::optional<Attenuation> attenuation;  ///< its own distances; none: Audio::attenuation
    bool filtered = false;  ///< a low-pass filter on the voice (Audio::set_lowpass(): occlusion, automation)
    float lowpass = 0.0f;   ///< its cutoff in Hz when `filtered`; 0: open
    std::uint64_t start_at = 0;  ///< the engine's time (Audio::time_frames()) to start at, sample exact; 0: now
};

/// The settings of a bus's reverberation (Audio::set_bus_reverb()): see ReverbDescription.
struct ReverbSettings {
    float room = 0.5f;     ///< 0 to 1: how long it rings
    float damping = 0.5f;  ///< 0 to 1: how fast the highs die
    float wet = 0.0f;      ///< 0 to 1: how much of it is heard (0: dry)
};

/// A sound started by Audio::play() or Audio::play_stream(); 0 is none. Stays valid (and harmless)
/// after the sound ended: the functions taking one then do nothing.
using SoundId = std::uint32_t;
/// No sound: what Audio::play() returns when it cannot play.
constexpr SoundId kNoSound = 0;

/// How the Audio opens its output.
struct AudioConfig {
    /// false: no sound device (tests, or no audio wanted); everything works, Audio::mix() gives
    /// the output.
    bool device = true;
    /// The mixing rate in Hz; 0 lets the device choose. Without a device, 0 means 48000 Hz.
    int sample_rate = 0;
    /// The mixing channel count; 0 lets the device choose. Without a device, 0 means stereo.
    int channels = 0;
};

/// What the debug panel and the tests read.
struct AudioStats {
    int voices = 0;            ///< effects, ambiences and interface sounds playing
    int musics = 0;            ///< playing, fading ones included
    int streams = 0;           ///< play_stream() voices
    std::size_t played = 0;    ///< voices started since the beginning
    std::size_t merged = 0;    ///< requests merged with the same sound in the same frame
    std::size_t stolen = 0;    ///< voices stopped to make room for a stronger one
    std::size_t dropped_inaudible = 0;    ///< requests not played: too quiet (kInaudible)
    std::size_t dropped_sound_limit = 0;  ///< requests not played: VoiceLimits::max_per_sound
    std::size_t dropped_voice_limit = 0;  ///< requests not played: VoiceLimits::max_voices
    /// The name of the last sound not played because of a limit (or because it could not be
    /// heard). Empty until one is refused.
    std::string last_refused;
    /// Why last_refused was not played: "inaudible", "sound_limit" or "voice_limit". Empty until
    /// one is refused.
    std::string last_refused_reason;
    /// The highest sample of the mix, before the limiter, since the last Audio::reset_peak() (1:
    /// full scale; may go above 1).
    float mix_peak = 0.0f;
    /// The highest sample of the output, after the limiter (at most 0.9), since the last
    /// Audio::reset_peak().
    float peak = 0.0f;
    /// The lowest gain the limiter had to apply since the last Audio::reset_peak() (1: none).
    float limiter_gain = 1.0f;
    bool device = false;       ///< a device is open and running
    int sample_rate = 0;       ///< Hz, of the mix
    int channels = 0;          ///< of the mix
    /// The output device's name; "(no device)" without one by choice, "(none)" if none could be
    /// opened.
    std::string device_name;
    /// The device's buffer, in milliseconds: the delay between the mix and the speakers. A sound
    /// asked for by a tick also waits for the end of its frame (up to one frame more).
    float device_latency_ms = 0.0f;
};

/// Sound output (miniaudio): effects placed in the world, music streamed with crossfades, groups
/// with volumes and pauses, a limit on the voices.
///
/// Timing: the game asks for sounds from its own update, at the fixed rate; they wait in a queue,
/// which Audio::update(), called by the Application once per frame after the drawing, plays. The simulation never
/// depends on the audio: nothing here feeds back into it, and the random variations have their own
/// generator.
///
/// Voices: at most `limits.max_voices` effects at once, `limits.max_per_sound` for the same file;
/// requests of one frame for the same file become one (the loudest). Over a limit, a new sound takes
/// the place of the weakest voice (lowest priority, then quietest, then oldest) if it is at least as
/// strong; otherwise it is not played. See audio_rules.hpp.
///
/// The output goes through a limiter: when many loud sounds add up above 0.9 of full scale, the
/// whole output is lowered for a moment (instant attack, 150 ms release) rather than clipped.
///
/// Placed sounds are heard from the listener (the centre of the view: set_listener()), with the
/// gain and pan of place_sound(), recomputed every frame while they play.
///
/// Everything is called from the main thread. If no device can be opened, the Audio stays silent
/// and says so (stats().device); a device that disappears (headphones unplugged) is followed by the
/// system's default one when the backend allows it, or retried every second.
class Audio {
public:
    /// Opens the output described by `config`. Never throws for want of a device: the Audio then
    /// stays silent (logged), every call is harmless and play() returns kNoSound.
    explicit Audio(const AudioConfig& config = {});
    /// Stops every voice and closes the device.
    ~Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    // --- Effects, ambiences, interface
    /// Queues `sound` for the next update(), which decides whether it gets a voice (see the class
    /// description). Returns its id, or kNoSound for a null or empty sound, or without an output.
    /// The voice keeps the samples alive while it plays.
    SoundId play(const Asset<Sound>& sound, const PlaySound& options = {});
    /// One of `variants`, at random (footsteps): the limit per sound counts each file apart.
    /// kNoSound if `variants` is empty.
    SoundId play(std::span<const Asset<Sound>> variants, const PlaySound& options = {});
    /// A long sound decoded while it plays (an ambience, usually looped), in any group, placed or
    /// not. Streams are few and long: they start at once and the voice limits leave them alone.
    /// `options.volume_variation` and `options.pitch_variation` are ignored. kNoSound if the stream
    /// is null or empty, cannot be decoded, or without an output.
    SoundId play_stream(const Asset<Music>& stream, const PlaySound& options = {});
    /// Stops a sound (or stream), fading it out over `fade_seconds`; a queued one is simply
    /// removed. Does nothing for an ended or unknown id. Not the music: stop_music().
    void stop(SoundId sound, float fade_seconds = 0.0f);
    /// Moves a placed sound (or stream) in the world, metres; its gain and pan follow at once.
    /// A sound played without a position becomes placed.
    void set_position(SoundId sound, glm::vec3 position);
    /// Changes the volume of a sound (or stream), as PlaySound::volume; it replaces the random
    /// variation the sound was given.
    void set_volume(SoundId sound, float volume);
    /// Queued or playing (a sound dropped by the limits, ended, or stopping, is not).
    bool playing(SoundId sound) const;
    void stop_all(float fade_seconds = 0.0f);  ///< every sound and stream, music included

    // --- Music: one at a time, the previous fading out while the next fades in.
    /// Plays `music` looped in the SoundGroup::Music group: the music playing fades out over
    /// `crossfade_seconds` while this one fades in. `volume` is the music's own gain. If `music`
    /// is already the music playing, only its volume changes. Does nothing for a null or empty
    /// music, one that cannot be decoded, or without an output.
    void play_music(const Asset<Music>& music, float crossfade_seconds = 1.5f, float volume = 1.0f);
    /// Fades the music out over `fade_seconds`, then frees it; nothing if no music plays.
    void stop_music(float fade_seconds = 1.5f);
    /// The music playing (or starting), null if none. Valid until the music changes or stops.
    const Music* music() const;
    /// Fades the music's own volume (not the player's setting) over `fade_seconds`: lower it under
    /// a pause menu. It multiplies the `volume` given to play_music() (1 restores it), and lasts
    /// until the next music. Nothing if no music plays.
    void set_music_volume(float volume, float fade_seconds = 0.5f);

    // --- Groups and the player's settings (sliders, 0 to 1: see slider_gain())
    /// The master slider, 0 to 1 (clamped); marks the settings changed.
    void set_master_volume(float slider);
    float master_volume() const { return master_; }  ///< the master slider, 0 to 1
    /// The slider of `group`, 0 to 1 (clamped); marks the settings changed.
    void set_group_volume(SoundGroup group, float slider);
    /// The slider of `group`, 0 to 1.
    float group_volume(SoundGroup group) const { return groups_[static_cast<std::size_t>(group)].slider; }
    /// A paused group keeps its sounds where they are, and resumes them. Not a saved setting.
    void set_group_paused(SoundGroup group, bool paused);
    /// Whether `group` is paused (set_group_paused()).
    bool group_paused(SoundGroup group) const { return groups_[static_cast<std::size_t>(group)].paused; }
    /// Silent while the window does not have the focus (a player setting, on by default).
    void set_mute_in_background(bool mute);
    /// Whether the Audio is silent while the window does not have the focus.
    bool mute_in_background() const { return mute_in_background_; }
    void set_focus(bool focused);  ///< the Application calls it
    /// The settings as JSON (the player's file): `version` (1), `master`, `volumes` (one slider per
    /// sound_group_name()) and `mute_in_background`.
    std::string settings_json() const;
    /// Applies settings written by settings_json(); missing keys keep their value, sliders are
    /// clamped to 0..1, and the settings count as saved afterwards. Throws std::runtime_error naming
    /// `source_name` for text that is not a version 1 settings file, or a value of the wrong type;
    /// nothing is changed then (all the settings or none).
    void apply_settings_json(std::string_view json_text, const std::string& source_name);
    /// The settings changed since they were last loaded or saved (mark_settings_saved()).
    bool settings_changed() const { return settings_changed_; }
    /// Tells the Audio its settings were written: settings_changed() is false until the next change.
    void mark_settings_saved() { settings_changed_ = false; }

    // --- Listener and rules
    /// Where placed sounds are heard from; they follow it at the next update().
    void set_listener(const Listener& listener) { listener_ = listener; }
    /// The ground point at the centre of the view, and the screen's right: what a fixed camera hears.
    void set_listener(const Camera3D& camera);
    /// Where placed sounds are heard from (set_listener()).
    const Listener& listener() const { return listener_; }
    /// How distance changes the placed sounds; read every time a placement is computed.
    Attenuation attenuation;
    /// The voice limits, used by update() for each new sound.
    VoiceLimits limits;

    // --- Frame
    /// Plays the queue, moves the placed voices, frees the ended ones, watches the device. Once per
    /// frame (the Application does it).
    void update();
    /// Without a device: mixes `out.size() / channels` frames into `out` (interleaved floats), as
    /// the device would. For tests. With a device (or none opened), fills `out` with zeros.
    void mix(std::span<float> out);

    /// The counters and the device's state, as of now. Asks the device for its name: not for every
    /// frame of a hot path.
    AudioStats stats() const;
    /// Restarts AudioStats::mix_peak, AudioStats::peak and AudioStats::limiter_gain.
    void reset_peak();

    // --- Buses (milestone 9 bis, part 7: the mixer under the events) ---
    // The four groups are the base buses 0 to 3 ("music", "effects", "ambience", "interface"):
    // their volume is the player's slider times their own mix volume (snapshots). Other buses are
    // made under them, a tree of miniaudio sound groups.

    /// A bus `name` under `parent` (a bus index); returns its index. The same name again returns
    /// the existing bus. Throws std::invalid_argument for a parent out of range.
    int add_bus(const std::string& name, int parent);
    /// The bus `name` ("effects", "effects/pas"...), or -1.
    int find_bus(std::string_view name) const;
    int bus_count() const;  ///< the base buses and those added
    const std::string& bus_name(int bus) const;  ///< "" out of range
    /// The bus's own gain (1: as is), times the player's slider for a base bus; changes at once
    /// (the caller fades it: AudioEvents' snapshots).
    void set_bus_volume(int bus, float volume);
    float bus_volume(int bus) const;  ///< as set; 1 out of range
    /// Pauses a bus (and everything under it); with the player's pause of a base group.
    void set_bus_paused(int bus, bool paused);
    /// A reverberation on a base bus (inserted at the first call); `wet` 0: dry.
    void set_bus_reverb(int bus, const ReverbSettings& settings);

    /// A voice's low-pass cutoff in Hz (played with PlaySound::filtered); 0: open.
    void set_lowpass(SoundId sound, float hz);
    /// A voice's or stream's pitch (2: an octave higher).
    void set_pitch(SoundId sound, float pitch);
    /// Stops a sound at the engine's time `frame` (Audio::time_frames()), fading out over
    /// `fade_seconds` before it: music transitions on the bar.
    void stop_at(SoundId sound, std::uint64_t frame, float fade_seconds);
    /// The engine's clock, in frames at sample_rate() (what PlaySound::start_at counts).
    std::uint64_t time_frames() const;
    int sample_rate() const;  ///< the mixing rate, Hz (0 without engine)
    /// Where a stream is in its file, in seconds (its own clock: music bars); none if not playing.
    std::optional<double> stream_seconds(SoundId sound) const;

    /// One sound playing (effects, ambiences, interface; streams and music apart), for the debug panel.
    struct ActiveSound {
        std::string name;                        ///< the Sound's name
        SoundGroup group = SoundGroup::Effects;  ///< as played (PlaySound::group)
        float loudness = 0.0f;  ///< as the voice limit compares them (volume and distance)
        bool placed = false;    ///< has a position in the world
        bool looping = false;   ///< played with PlaySound::loop
        bool stopping = false;  ///< fading out (stolen or stopped)
    };
    /// The voices of effects, ambiences and interface sounds, stopping ones included. Allocates.
    std::vector<ActiveSound> active_sounds() const;

private:
    struct Voice;
    struct MusicVoice;
    struct Request {
        SoundId id;
        Asset<Sound> sound;
        PlaySound options;
    };
    struct Group {
        float slider = 1.0f;
        bool paused = false;
    };
    struct Engine;  ///< miniaudio's objects

    float random_between(float low, float high);
    float loudness(const PlaySound& options, float volume) const;
    void start(Request& request, float loudness);
    void refused(const Request& request, const char* reason);
    void apply_placement(Voice& voice);
    void apply_placement(MusicVoice& stream);
    std::unique_ptr<MusicVoice> open_stream(const Asset<Music>& music, SoundGroup group, int bus = -1);
    struct Bus;
    void* bus_node(SoundGroup group, int bus);  ///< the ma_sound_group a voice plays into
    void apply_bus(int bus);
    void apply_master();
    void apply_group(SoundGroup group);
    void watch_device();

    std::unique_ptr<Engine> engine_;
    std::vector<Request> queue_;
    std::vector<std::unique_ptr<Voice>> voices_;
    std::unique_ptr<MusicVoice> music_;
    std::vector<std::unique_ptr<MusicVoice>> fading_musics_;
    std::vector<std::unique_ptr<MusicVoice>> streams_;
    std::array<Group, kSoundGroupCount> groups_{};
    std::vector<std::unique_ptr<Bus>> buses_;  ///< the base ones first (see add_bus())
    float master_ = 1.0f;
    bool mute_in_background_ = true;
    bool focused_ = true;
    bool settings_changed_ = false;
    Listener listener_;
    SoundId next_id_ = 1;
    std::uint64_t next_order_ = 1;
    std::uint32_t random_state_ = 0x9e3779b9u;
    AudioStats counters_;  ///< the counting fields only
    double retry_time_ = 0.0;
};

}  // namespace moteur
