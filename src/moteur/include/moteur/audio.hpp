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

// The families of sounds, each with its volume (the player's settings) and its pause.
enum class SoundGroup { Music, Effects, Ambience, Interface };
constexpr int kSoundGroupCount = 4;
const char* sound_group_name(SoundGroup group);  // "music", "effects"... (settings file)

// How to play a sound (Audio::play).
struct PlaySound {
    SoundGroup group = SoundGroup::Effects;
    float volume = 1.0f;
    float pitch = 1.0f;             // 2: an octave higher (and twice as fast)
    float volume_variation = 0.0f;  // +/- this fraction at random (0.1: 90 % to 110 %)
    float pitch_variation = 0.0f;   // +/- this fraction at random: variety for repeated sounds
    std::optional<glm::vec3> position;  // in the world (attenuated and panned); none: as is (interface)
    int priority = 0;               // higher keeps its voice when there are too many
    bool loop = false;              // until stop(): ambiences, a burning torch
    float fade_in = 0.0f;           // seconds
};

// A sound started by play(); 0 is none. Stays valid (and harmless) after the sound ended.
using SoundId = std::uint32_t;
constexpr SoundId kNoSound = 0;

struct AudioConfig {
    // false: no sound device (tests, or no audio wanted); everything works, mix() gives the output.
    bool device = true;
    // Mixing format; 0 lets the device choose. Without a device, 0 means 48000 Hz, stereo.
    int sample_rate = 0;
    int channels = 0;
};

// What the debug panel and the tests read.
struct AudioStats {
    int voices = 0;            // effects, ambiences and interface sounds playing
    int musics = 0;            // playing, fading ones included
    int streams = 0;           // play_stream() voices
    std::size_t played = 0;    // voices started since the beginning
    std::size_t merged = 0;    // requests merged with the same sound in the same frame
    std::size_t stolen = 0;    // voices stopped to make room for a stronger one
    std::size_t dropped_inaudible = 0;
    std::size_t dropped_sound_limit = 0;
    std::size_t dropped_voice_limit = 0;
    // The last sound not played because of a limit (or because it could not be heard), and why:
    // "inaudible", "sound_limit" or "voice_limit". Empty until one is refused.
    std::string last_refused;
    std::string last_refused_reason;
    // Since the last reset_peak() (1: full scale): the highest sample of the mix, of the output
    // after the limiter (at most 0.9), and the lowest gain the limiter had to apply.
    float mix_peak = 0.0f;
    float peak = 0.0f;
    float limiter_gain = 1.0f;
    bool device = false;       // a device is open and running
    int sample_rate = 0;
    int channels = 0;
    std::string device_name;
    // The device's buffer, in milliseconds: the delay between the mix and the speakers. A sound
    // asked for by a tick also waits for the end of its frame (up to one frame more).
    float device_latency_ms = 0.0f;
};

// Sound output (miniaudio): effects placed in the world, music streamed with crossfades, groups
// with volumes and pauses, a limit on the voices.
//
// Timing: the game asks for sounds from update(), at the fixed rate; they wait in a queue, which
// update(), called by the Application once per frame after the drawing, plays. The simulation never
// depends on the audio: nothing here feeds back into it, and the random variations have their own
// generator.
//
// Voices: at most `limits.max_voices` effects at once, `limits.max_per_sound` for the same file;
// requests of one frame for the same file become one (the loudest). Over a limit, a new sound takes
// the place of the weakest voice (lowest priority, then quietest, then oldest) if it is at least as
// strong; otherwise it is not played. See audio_rules.hpp.
//
// The output goes through a limiter: when many loud sounds add up above 0.9 of full scale, the
// whole output is lowered for a moment (instant attack, 150 ms release) rather than clipped.
//
// Placed sounds are heard from the listener (the centre of the view: set_listener()), with the
// gain and pan of place_sound(), recomputed every frame while they play.
//
// Everything is called from the main thread. If no device can be opened, the Audio stays silent
// and says so (stats().device); a device that disappears (headphones unplugged) is followed by the
// system's default one when the backend allows it, or retried every second.
class Audio {
public:
    explicit Audio(const AudioConfig& config = {});
    ~Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    // --- Effects, ambiences, interface
    SoundId play(const Asset<Sound>& sound, const PlaySound& options = {});
    // One of `variants`, at random (footsteps): the limit per sound counts each file apart.
    SoundId play(std::span<const Asset<Sound>> variants, const PlaySound& options = {});
    // A long sound decoded while it plays (an ambience, usually looped), in any group, placed or
    // not. Streams are few and long: they start at once and the voice limits leave them alone.
    SoundId play_stream(const Asset<Music>& stream, const PlaySound& options = {});
    void stop(SoundId sound, float fade_seconds = 0.0f);
    void set_position(SoundId sound, glm::vec3 position);
    void set_volume(SoundId sound, float volume);
    // Queued or playing (a sound dropped by the limits, or ended, is not).
    bool playing(SoundId sound) const;
    void stop_all(float fade_seconds = 0.0f);  // music included

    // --- Music: one at a time, the previous fading out while the next fades in.
    void play_music(const Asset<Music>& music, float crossfade_seconds = 1.5f, float volume = 1.0f);
    void stop_music(float fade_seconds = 1.5f);
    // The music playing (or starting), null if none.
    const Music* music() const;
    // Fades the music's own volume (not the player's setting): lower it under a pause menu.
    void set_music_volume(float volume, float fade_seconds = 0.5f);

    // --- Groups and the player's settings (sliders, 0 to 1: see slider_gain())
    void set_master_volume(float slider);
    float master_volume() const { return master_; }
    void set_group_volume(SoundGroup group, float slider);
    float group_volume(SoundGroup group) const { return groups_[static_cast<std::size_t>(group)].slider; }
    // A paused group keeps its sounds where they are, and resumes them.
    void set_group_paused(SoundGroup group, bool paused);
    bool group_paused(SoundGroup group) const { return groups_[static_cast<std::size_t>(group)].paused; }
    // Silent while the window does not have the focus (a player setting, on by default).
    void set_mute_in_background(bool mute);
    bool mute_in_background() const { return mute_in_background_; }
    void set_focus(bool focused);  // the Application calls it
    // The settings as JSON (the player's file), and back. apply throws std::runtime_error naming
    // `source_name` for a file that is not one.
    std::string settings_json() const;
    void apply_settings_json(std::string_view json_text, const std::string& source_name);
    // The settings changed since they were last loaded or saved (mark_settings_saved()).
    bool settings_changed() const { return settings_changed_; }
    void mark_settings_saved() { settings_changed_ = false; }

    // --- Listener and rules
    void set_listener(const Listener& listener) { listener_ = listener; }
    // The ground point at the centre of the view, and the screen's right: what a fixed camera hears.
    void set_listener(const Camera3D& camera);
    const Listener& listener() const { return listener_; }
    Attenuation attenuation;
    VoiceLimits limits;

    // --- Frame
    // Plays the queue, moves the placed voices, frees the ended ones, watches the device. Once per
    // frame (the Application does it).
    void update();
    // Without a device: mixes `frames` frames into `out` (channels x frames floats), as the device
    // would. For tests.
    void mix(std::span<float> out);

    AudioStats stats() const;
    void reset_peak();

    // One sound playing (effects, ambiences, interface; streams and music apart), for the debug panel.
    struct ActiveSound {
        std::string name;
        SoundGroup group = SoundGroup::Effects;
        float loudness = 0.0f;  // as the voice limit compares them (volume and distance)
        bool placed = false;    // has a position in the world
        bool looping = false;
        bool stopping = false;  // fading out (stolen or stopped)
    };
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
    struct Engine;  // miniaudio's objects

    float random_between(float low, float high);
    float loudness(const PlaySound& options, float volume) const;
    void start(Request& request, float loudness);
    void refused(const Request& request, const char* reason);
    void apply_placement(Voice& voice);
    void apply_placement(MusicVoice& stream);
    std::unique_ptr<MusicVoice> open_stream(const Asset<Music>& music, SoundGroup group);
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
    float master_ = 1.0f;
    bool mute_in_background_ = true;
    bool focused_ = true;
    bool settings_changed_ = false;
    Listener listener_;
    SoundId next_id_ = 1;
    std::uint64_t next_order_ = 1;
    std::uint32_t random_state_ = 0x9e3779b9u;
    AudioStats counters_;  // the counting fields only
    double retry_time_ = 0.0;
};

}  // namespace moteur
