#include "moteur/audio.hpp"

#include <SDL3/SDL.h>

#include <miniaudio.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <utility>

#include "moteur/audio_reverb.hpp"
#include "moteur/camera3d.hpp"

namespace moteur {

namespace {

constexpr float kStealFadeSeconds = 0.02f;  // a voice stopped for another: no click
constexpr double kRetrySeconds = 1.0;       // a lost device is tried again this often
constexpr float kLimiterCeiling = 0.9f;     // the output never goes above this (1 is full scale)
constexpr float kLimiterRelease = 0.15f;    // seconds for the limiter to let the level come back

ma_uint64 milliseconds(float seconds) {
    return static_cast<ma_uint64>(std::lround(std::max(seconds, 0.0f) * 1000.0f));
}

double now_seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}  // namespace

const char* sound_group_name(SoundGroup group) {
    switch (group) {
        case SoundGroup::Music: return "music";
        case SoundGroup::Effects: return "effects";
        case SoundGroup::Ambience: return "ambience";
        case SoundGroup::Interface: return "interface";
    }
    return "?";
}

// --- miniaudio's objects

namespace {

// A bus's reverberation, as a miniaudio node (one input, one output): the engine's Reverb.
struct ReverbNode {
    ma_node_base base;  // first: miniaudio sees the node through it
    std::unique_ptr<audio::Reverb> reverb;

    static void process(ma_node* node, const float** in, ma_uint32* frames_in, float** out, ma_uint32* frames_out) {
        auto* self = reinterpret_cast<ReverbNode*>(node);
        const ma_uint32 frames = std::min(*frames_in, *frames_out);
        self->reverb->process(in[0], out[0], frames);
        *frames_out = frames;
    }
};

ma_node_vtable reverb_vtable = {&ReverbNode::process, nullptr, 1, 1, 0};

}  // namespace

struct Audio::Engine {
    ma_engine engine;  // first: the device's user data points to it, and so to this Engine
    bool initialized = false;
    bool has_device = false;
    int channels = 0;
    int sample_rate = 0;
    std::array<ma_sound_group, kSoundGroupCount> groups{};
    bool groups_initialized = false;
    // The output limiter: its gain, and how fast the gain comes back up (audio thread only).
    float limiter_gain = 1.0f;
    float limiter_release = 0.0f;  // per frame
    // Written by the audio thread, read by the main one.
    std::atomic<float> peak{0.0f};        // of the output, after the limiter
    std::atomic<float> mix_peak{0.0f};    // of the mix, before it
    std::atomic<float> lowest_gain{1.0f}; // the limiter's
    std::atomic<bool> stopped{false};   // the device stopped without being asked to
    std::atomic<bool> rerouted{false};  // it now plays on another device (the new default one)

    ~Engine() {
        if (groups_initialized) {
            for (ma_sound_group& group : groups) {
                ma_sound_group_uninit(&group);
            }
        }
        if (initialized) {
            ma_engine_uninit(&engine);
        }
    }

    // The end of the mix, on the audio thread: a limiter, so that many loud sounds at once lower
    // the whole output for a moment instead of clipping it. Instant attack (no sample goes above
    // the ceiling), smooth release.
    static void on_process(void* user_data, float* frames, ma_uint64 frame_count) {
        auto* self = static_cast<Engine*>(user_data);
        const auto channels = static_cast<ma_uint64>(self->channels);
        float peak = self->peak.load(std::memory_order_relaxed);
        float mix_peak = self->mix_peak.load(std::memory_order_relaxed);
        float lowest = self->lowest_gain.load(std::memory_order_relaxed);
        float gain = self->limiter_gain;
        for (ma_uint64 f = 0; f < frame_count; ++f) {
            float* frame = frames + f * channels;
            float level = 0.0f;
            for (ma_uint64 c = 0; c < channels; ++c) {
                level = std::max(level, std::fabs(frame[c]));
            }
            mix_peak = std::max(mix_peak, level);
            gain += (1.0f - gain) * self->limiter_release;
            if (level * gain > kLimiterCeiling) {
                gain = kLimiterCeiling / level;
            }
            for (ma_uint64 c = 0; c < channels; ++c) {
                frame[c] *= gain;
            }
            peak = std::max(peak, level * gain);
            lowest = std::min(lowest, gain);
        }
        self->limiter_gain = gain;
        self->peak.store(peak, std::memory_order_relaxed);
        self->mix_peak.store(mix_peak, std::memory_order_relaxed);
        self->lowest_gain.store(lowest, std::memory_order_relaxed);
    }

    static void on_notification(const ma_device_notification* notification) {
        auto* self = reinterpret_cast<Engine*>(notification->pDevice->pUserData);  // the ma_engine
        if (self == nullptr) {
            return;
        }
        if (notification->type == ma_device_notification_type_stopped) {
            self->stopped.store(true);
        } else if (notification->type == ma_device_notification_type_rerouted) {
            self->rerouted.store(true);
        }
    }
};

struct Audio::Voice {
    SoundId id = kNoSound;
    Asset<Sound> asset;
    std::shared_ptr<const std::vector<float>> samples;  // alive while the audio thread reads them
    ma_audio_buffer_ref buffer{};
    ma_sound sound{};
    ma_lpf_node filter{};  // PlaySound::filtered: between the sound and its bus
    bool buffer_initialized = false;
    bool sound_initialized = false;
    bool filter_initialized = false;
    float cutoff = 0.0f;   // the filter's, Hz (0: open)
    PlaySound options;
    float volume = 1.0f;  // with its random variation
    std::uintptr_t key = 0;
    std::uint64_t order = 0;
    float loudness = 0.0f;
    bool stopping = false;

    ~Voice() {
        if (sound_initialized) {
            ma_sound_uninit(&sound);
        }
        if (filter_initialized) {
            ma_lpf_node_uninit(&filter, nullptr);
        }
        if (buffer_initialized) {
            ma_audio_buffer_ref_uninit(&buffer);
        }
    }
};

// A music, or a stream (play_stream()).
struct Audio::MusicVoice {
    SoundId id = kNoSound;  // streams only
    PlaySound options;      // streams only
    Asset<Music> asset;
    std::shared_ptr<const std::vector<std::uint8_t>> bytes;
    ma_decoder decoder{};
    ma_sound sound{};
    bool decoder_initialized = false;
    bool sound_initialized = false;
    bool stopping = false;

    ~MusicVoice() {
        if (sound_initialized) {
            ma_sound_uninit(&sound);
        }
        if (decoder_initialized) {
            ma_decoder_uninit(&decoder);
        }
    }
};

// A bus: a sound group (the base ones are the Engine's groups), its mix volume and pause, and a
// reverberation inserted after it.
struct Audio::Bus {
    std::string name;
    int parent = -1;
    float volume = 1.0f;
    bool paused = false;
    ma_sound_group own{};       // an added bus's group
    ma_sound_group* group = nullptr;
    bool own_initialized = false;
    ReverbNode reverb{};
    bool reverb_initialized = false;

    ~Bus() {
        if (own_initialized) {
            ma_sound_group_uninit(&own);
        }
        if (reverb_initialized) {
            ma_node_uninit(&reverb, nullptr);
        }
    }
};

// --- Construction

Audio::Audio(const AudioConfig& config) : engine_(std::make_unique<Engine>()) {
    Engine& e = *engine_;
    ma_engine_config engine_config = ma_engine_config_init();
    engine_config.onProcess = &Engine::on_process;
    engine_config.pProcessUserData = &e;
    if (config.device) {
        engine_config.notificationCallback = &Engine::on_notification;
        engine_config.channels = static_cast<ma_uint32>(std::max(config.channels, 0));
        engine_config.sampleRate = static_cast<ma_uint32>(std::max(config.sample_rate, 0));
    } else {
        engine_config.noDevice = MA_TRUE;
        engine_config.channels = static_cast<ma_uint32>(config.channels > 0 ? config.channels : 2);
        engine_config.sampleRate = static_cast<ma_uint32>(config.sample_rate > 0 ? config.sample_rate : 48000);
    }
    const ma_result result = ma_engine_init(&engine_config, &e.engine);
    if (result != MA_SUCCESS) {
        SDL_Log("Audio: no sound device (%s): the game stays silent", ma_result_description(result));
        return;
    }
    e.initialized = true;
    e.has_device = config.device;
    e.channels = static_cast<int>(ma_engine_get_channels(&e.engine));
    e.sample_rate = static_cast<int>(ma_engine_get_sample_rate(&e.engine));
    e.limiter_release = 1.0f - std::exp(-1.0f / (kLimiterRelease * static_cast<float>(e.sample_rate)));
    for (ma_sound_group& group : e.groups) {
        ma_sound_group_init(&e.engine, 0, nullptr, &group);
    }
    e.groups_initialized = true;
    for (int i = 0; i < kSoundGroupCount; ++i) {
        auto bus = std::make_unique<Bus>();
        bus->name = sound_group_name(static_cast<SoundGroup>(i));
        bus->group = &e.groups[static_cast<std::size_t>(i)];
        buses_.push_back(std::move(bus));
    }
    if (e.has_device) {
        const AudioStats s = stats();
        SDL_Log("Audio: %s, %d Hz, %d channels, %.1f ms of buffer", s.device_name.c_str(), e.sample_rate, e.channels,
                static_cast<double>(s.device_latency_ms));
    }
}

Audio::~Audio() {
    // Voices before the groups and the engine they play into.
    queue_.clear();
    voices_.clear();
    music_.reset();
    fading_musics_.clear();
    streams_.clear();
    // Children before their parents, then the base buses' reverbs, before the groups.
    while (!buses_.empty()) {
        buses_.pop_back();
    }
    engine_.reset();
}

// --- Buses

int Audio::add_bus(const std::string& name, int parent) {
    if (const int found = find_bus(name); found >= 0) {
        return found;
    }
    if (parent < 0 || parent >= static_cast<int>(buses_.size())) {
        throw std::invalid_argument("Audio: no bus " + std::to_string(parent) + " to put '" + name + "' under");
    }
    auto bus = std::make_unique<Bus>();
    bus->name = name;
    bus->parent = parent;
    if (engine_->initialized) {
        if (ma_sound_group_init(&engine_->engine, 0, buses_[static_cast<std::size_t>(parent)]->group, &bus->own) == MA_SUCCESS) {
            bus->own_initialized = true;
            bus->group = &bus->own;
        }
    }
    buses_.push_back(std::move(bus));
    return static_cast<int>(buses_.size()) - 1;
}

int Audio::find_bus(std::string_view name) const {
    for (std::size_t i = 0; i < buses_.size(); ++i) {
        if (buses_[i]->name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int Audio::bus_count() const {
    return static_cast<int>(buses_.size());
}

const std::string& Audio::bus_name(int bus) const {
    static const std::string none;
    return bus >= 0 && bus < bus_count() ? buses_[static_cast<std::size_t>(bus)]->name : none;
}

void Audio::set_bus_volume(int bus, float volume) {
    if (bus < 0 || bus >= bus_count()) {
        return;
    }
    buses_[static_cast<std::size_t>(bus)]->volume = std::max(0.0f, volume);
    apply_bus(bus);
}

float Audio::bus_volume(int bus) const {
    return bus >= 0 && bus < bus_count() ? buses_[static_cast<std::size_t>(bus)]->volume : 1.0f;
}

void Audio::set_bus_paused(int bus, bool paused) {
    if (bus < 0 || bus >= bus_count()) {
        return;
    }
    buses_[static_cast<std::size_t>(bus)]->paused = paused;
    apply_bus(bus);
}

void Audio::apply_bus(int index) {
    if (!engine_->initialized) {
        return;
    }
    if (index < kSoundGroupCount) {
        apply_group(static_cast<SoundGroup>(index));  // the player's slider and pause with the mix's
        return;
    }
    Bus& bus = *buses_[static_cast<std::size_t>(index)];
    if (bus.group == nullptr) {
        return;
    }
    ma_sound_group_set_volume(bus.group, bus.volume);
    if (bus.paused) {
        ma_sound_group_stop(bus.group);
    } else {
        ma_sound_group_start(bus.group);
    }
}

void Audio::set_bus_reverb(int index, const ReverbSettings& settings) {
    Engine& e = *engine_;
    if (!e.initialized || index < 0 || index >= kSoundGroupCount) {
        return;
    }
    Bus& bus = *buses_[static_cast<std::size_t>(index)];
    if (!bus.reverb_initialized) {
        if (settings.wet <= 0.0f) {
            return;  // nothing to hear: no node yet
        }
        // Inserted after the group: group -> reverb -> the engine's endpoint.
        bus.reverb.reverb = std::make_unique<audio::Reverb>(e.channels, e.sample_rate);
        const auto channels = static_cast<ma_uint32>(e.channels);
        ma_node_config config = ma_node_config_init();
        config.vtable = &reverb_vtable;
        config.pInputChannels = &channels;
        config.pOutputChannels = &channels;
        if (ma_node_init(ma_engine_get_node_graph(&e.engine), &config, nullptr, &bus.reverb) != MA_SUCCESS) {
            bus.reverb.reverb.reset();
            return;
        }
        bus.reverb_initialized = true;
        ma_node_attach_output_bus(&bus.reverb, 0, ma_engine_get_endpoint(&e.engine), 0);
        ma_node_attach_output_bus(bus.group, 0, &bus.reverb, 0);
    }
    bus.reverb.reverb->set(settings.room, settings.damping, settings.wet);
}

void* Audio::bus_node(SoundGroup group, int bus) {
    if (bus >= 0 && bus < bus_count() && buses_[static_cast<std::size_t>(bus)]->group != nullptr) {
        return buses_[static_cast<std::size_t>(bus)]->group;
    }
    return &engine_->groups[static_cast<std::size_t>(group)];
}

void Audio::set_lowpass(SoundId sound, float hz) {
    for (auto& voice : voices_) {
        if (voice->id != sound || !voice->filter_initialized) {
            continue;
        }
        // Small changes are not worth a new filter (it costs a reinit on the main thread).
        const float open = static_cast<float>(engine_->sample_rate) * 0.45f;
        const float cutoff = hz > 0.0f ? std::min(hz, open) : open;
        if (std::fabs(cutoff - voice->cutoff) <= voice->cutoff * 0.05f) {
            continue;
        }
        voice->cutoff = cutoff;
        const ma_lpf_config config = ma_lpf_config_init(ma_format_f32, static_cast<ma_uint32>(engine_->channels),
                                                        static_cast<ma_uint32>(engine_->sample_rate), cutoff, 2);
        ma_lpf_node_reinit(&config, &voice->filter);
    }
}

void Audio::set_pitch(SoundId sound, float pitch) {
    for (auto& voice : voices_) {
        if (voice->id == sound) {
            ma_sound_set_pitch(&voice->sound, std::max(0.05f, pitch));
        }
    }
    for (auto& stream : streams_) {
        if (stream->id == sound) {
            ma_sound_set_pitch(&stream->sound, std::max(0.05f, pitch));
        }
    }
}

void Audio::stop_at(SoundId sound, std::uint64_t frame, float fade_seconds) {
    const auto fade = static_cast<ma_uint64>(std::max(0.0f, fade_seconds) * static_cast<float>(engine_->sample_rate));
    for (auto& stream : streams_) {
        if (stream->id == sound && !stream->stopping) {
            ma_sound_set_stop_time_with_fade_in_pcm_frames(&stream->sound, frame, fade);
            stream->stopping = true;
        }
    }
    for (auto& voice : voices_) {
        if (voice->id == sound && !voice->stopping) {
            ma_sound_set_stop_time_with_fade_in_pcm_frames(&voice->sound, frame, fade);
            voice->stopping = true;
        }
    }
}

std::uint64_t Audio::time_frames() const {
    return engine_->initialized ? ma_engine_get_time_in_pcm_frames(&engine_->engine) : 0;
}

int Audio::sample_rate() const {
    return engine_->sample_rate;
}

std::optional<double> Audio::stream_seconds(SoundId sound) const {
    for (const auto& stream : streams_) {
        if (stream->id == sound && stream->sound_initialized) {
            float seconds = 0.0f;
            if (ma_sound_get_cursor_in_seconds(&stream->sound, &seconds) == MA_SUCCESS) {
                return static_cast<double>(seconds);
            }
        }
    }
    return std::nullopt;
}

// --- Effects

SoundId Audio::play(const Asset<Sound>& sound, const PlaySound& options) {
    if (!engine_->initialized || !sound || !sound->samples || sound->frames() == 0) {
        return kNoSound;
    }
    const SoundId id = next_id_++;
    queue_.push_back({id, sound, options});
    return id;
}

SoundId Audio::play(std::span<const Asset<Sound>> variants, const PlaySound& options) {
    if (variants.empty()) {
        return kNoSound;
    }
    const auto index = std::min(static_cast<std::size_t>(random_between(0.0f, 1.0f) * static_cast<float>(variants.size())),
                                variants.size() - 1);
    return play(variants[index], options);
}

void Audio::stop(SoundId sound, float fade_seconds) {
    std::erase_if(queue_, [sound](const Request& request) { return request.id == sound; });
    for (auto& stream : streams_) {
        if (stream->id == sound && !stream->stopping) {
            ma_sound_stop_with_fade_in_milliseconds(&stream->sound, milliseconds(fade_seconds));
            stream->stopping = true;
        }
    }
    for (auto& voice : voices_) {
        if (voice->id == sound && !voice->stopping) {
            ma_sound_stop_with_fade_in_milliseconds(&voice->sound, milliseconds(fade_seconds));
            voice->stopping = true;
        }
    }
}

void Audio::set_position(SoundId sound, glm::vec3 position) {
    for (Request& request : queue_) {
        if (request.id == sound) {
            request.options.position = position;
        }
    }
    for (auto& voice : voices_) {
        if (voice->id == sound) {
            voice->options.position = position;
            apply_placement(*voice);
        }
    }
    for (auto& stream : streams_) {
        if (stream->id == sound) {
            stream->options.position = position;
            apply_placement(*stream);
        }
    }
}

void Audio::set_volume(SoundId sound, float volume) {
    for (Request& request : queue_) {
        if (request.id == sound) {
            request.options.volume = volume;
        }
    }
    for (auto& voice : voices_) {
        if (voice->id == sound) {
            voice->volume = volume;
            apply_placement(*voice);
        }
    }
    for (auto& stream : streams_) {
        if (stream->id == sound) {
            stream->options.volume = volume;
            apply_placement(*stream);
        }
    }
}

bool Audio::playing(SoundId sound) const {
    if (sound == kNoSound) {
        return false;
    }
    for (const Request& request : queue_) {
        if (request.id == sound) {
            return true;
        }
    }
    for (const auto& voice : voices_) {
        if (voice->id == sound && !voice->stopping) {
            return true;
        }
    }
    for (const auto& stream : streams_) {
        if (stream->id == sound && !stream->stopping) {
            return true;
        }
    }
    return false;
}

void Audio::stop_all(float fade_seconds) {
    queue_.clear();
    for (auto& voice : voices_) {
        if (!voice->stopping) {
            ma_sound_stop_with_fade_in_milliseconds(&voice->sound, milliseconds(fade_seconds));
            voice->stopping = true;
        }
    }
    for (auto& stream : streams_) {
        if (!stream->stopping) {
            ma_sound_stop_with_fade_in_milliseconds(&stream->sound, milliseconds(fade_seconds));
            stream->stopping = true;
        }
    }
    stop_music(fade_seconds);
}

float Audio::random_between(float low, float high) {
    // xorshift32: the audio's own randomness, never the game's.
    random_state_ ^= random_state_ << 13;
    random_state_ ^= random_state_ >> 17;
    random_state_ ^= random_state_ << 5;
    const float unit = static_cast<float>(random_state_ >> 8) / 16777216.0f;
    return low + (high - low) * unit;
}

float Audio::loudness(const PlaySound& options, float volume) const {
    float gain = volume;
    if (options.position) {
        gain *= place_sound(listener_, *options.position, options.attenuation.value_or(attenuation)).gain;
    }
    return gain;
}

void Audio::apply_placement(Voice& voice) {
    Placement placement;
    if (voice.options.position) {
        placement = place_sound(listener_, *voice.options.position, voice.options.attenuation.value_or(attenuation));
    }
    voice.loudness = voice.volume * placement.gain;
    ma_sound_set_volume(&voice.sound, voice.loudness);
    ma_sound_set_pan(&voice.sound, placement.pan);
}

void Audio::apply_placement(MusicVoice& stream) {
    Placement placement;
    if (stream.options.position) {
        placement = place_sound(listener_, *stream.options.position, stream.options.attenuation.value_or(attenuation));
    }
    ma_sound_set_volume(&stream.sound, std::max(stream.options.volume, 0.0f) * placement.gain);
    ma_sound_set_pan(&stream.sound, placement.pan);
}

void Audio::start(Request& request, float) {
    Engine& e = *engine_;
    const Sound& sound = *request.sound;
    auto voice = std::make_unique<Voice>();
    voice->id = request.id;
    voice->asset = request.sound;
    voice->samples = sound.samples;
    voice->options = request.options;
    voice->key = reinterpret_cast<std::uintptr_t>(&*request.sound);
    voice->order = next_order_++;
    const PlaySound& o = request.options;
    voice->volume = std::max(0.0f, o.volume * (1.0f + random_between(-o.volume_variation, o.volume_variation)));

    if (ma_audio_buffer_ref_init(ma_format_f32, static_cast<ma_uint32>(sound.channels), voice->samples->data(),
                                 sound.frames(), &voice->buffer) != MA_SUCCESS) {
        return;
    }
    voice->buffer_initialized = true;
    voice->buffer.sampleRate = static_cast<ma_uint32>(sound.sample_rate);  // resampled to the engine's rate
    auto* group = static_cast<ma_sound_group*>(bus_node(o.group, o.bus));
    ma_node* into = group;
    if (o.filtered) {
        // A low-pass between the sound and its bus (occlusion, automations).
        const float open = static_cast<float>(e.sample_rate) * 0.45f;
        voice->cutoff = o.lowpass > 0.0f ? std::min(o.lowpass, open) : open;
        const ma_lpf_node_config config = ma_lpf_node_config_init(static_cast<ma_uint32>(e.channels),
                                                                  static_cast<ma_uint32>(e.sample_rate), voice->cutoff, 2);
        if (ma_lpf_node_init(ma_engine_get_node_graph(&e.engine), &config, nullptr, &voice->filter) == MA_SUCCESS) {
            voice->filter_initialized = true;
            ma_node_attach_output_bus(&voice->filter, 0, group, 0);
            into = &voice->filter;
        }
    }
    ma_sound_config config = ma_sound_config_init_2(&e.engine);
    config.pDataSource = &voice->buffer;
    config.flags = MA_SOUND_FLAG_NO_SPATIALIZATION;
    config.pInitialAttachment = into;
    const ma_result result = ma_sound_init_ex(&e.engine, &config, &voice->sound);
    if (result != MA_SUCCESS) {
        SDL_Log("Audio: cannot play '%s': %s", sound.name.c_str(), ma_result_description(result));
        return;
    }
    voice->sound_initialized = true;
    if (o.start_at > 0) {
        ma_sound_set_start_time_in_pcm_frames(&voice->sound, o.start_at);
    }
    ma_sound_set_pitch(&voice->sound, std::max(0.05f, o.pitch * (1.0f + random_between(-o.pitch_variation, o.pitch_variation))));
    ma_sound_set_looping(&voice->sound, o.loop ? MA_TRUE : MA_FALSE);
    apply_placement(*voice);
    if (o.fade_in > 0.0f) {
        ma_sound_set_fade_in_milliseconds(&voice->sound, 0.0f, 1.0f, milliseconds(o.fade_in));
    }
    ma_sound_start(&voice->sound);
    ++counters_.played;
    voices_.push_back(std::move(voice));
}

// --- Music

void Audio::play_music(const Asset<Music>& music, float crossfade_seconds, float volume) {
    if (!engine_->initialized || !music || !music->bytes || music->bytes->empty()) {
        return;
    }
    if (music_ && !music_->stopping && &*music_->asset == &*music) {
        ma_sound_set_volume(&music_->sound, volume);
        return;  // already playing it
    }
    stop_music(crossfade_seconds);

    std::unique_ptr<MusicVoice> voice = open_stream(music, SoundGroup::Music);
    if (!voice) {
        return;
    }
    ma_sound_set_looping(&voice->sound, MA_TRUE);
    ma_sound_set_volume(&voice->sound, volume);
    if (crossfade_seconds > 0.0f) {
        ma_sound_set_fade_in_milliseconds(&voice->sound, 0.0f, 1.0f, milliseconds(crossfade_seconds));
    }
    ma_sound_start(&voice->sound);
    music_ = std::move(voice);
}

std::unique_ptr<Audio::MusicVoice> Audio::open_stream(const Asset<Music>& music, SoundGroup group, int bus) {
    auto voice = std::make_unique<MusicVoice>();
    voice->asset = music;
    voice->bytes = music->bytes;
    const ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
    if (ma_decoder_init_memory(voice->bytes->data(), voice->bytes->size(), &config, &voice->decoder) != MA_SUCCESS) {
        SDL_Log("Audio: cannot play '%s'", music->name.c_str());
        return nullptr;
    }
    voice->decoder_initialized = true;
    Engine& e = *engine_;
    const ma_result result = ma_sound_init_from_data_source(&e.engine, &voice->decoder, MA_SOUND_FLAG_NO_SPATIALIZATION,
                                                            static_cast<ma_sound_group*>(bus_node(group, bus)), &voice->sound);
    if (result != MA_SUCCESS) {
        SDL_Log("Audio: cannot play '%s': %s", music->name.c_str(), ma_result_description(result));
        return nullptr;
    }
    voice->sound_initialized = true;
    return voice;
}

SoundId Audio::play_stream(const Asset<Music>& stream, const PlaySound& options) {
    if (!engine_->initialized || !stream || !stream->bytes || stream->bytes->empty()) {
        return kNoSound;
    }
    std::unique_ptr<MusicVoice> voice = open_stream(stream, options.group, options.bus);
    if (!voice) {
        return kNoSound;
    }
    if (options.start_at > 0) {
        ma_sound_set_start_time_in_pcm_frames(&voice->sound, options.start_at);
    }
    voice->id = next_id_++;
    voice->options = options;
    ma_sound_set_looping(&voice->sound, options.loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_pitch(&voice->sound, std::max(0.05f, options.pitch));
    apply_placement(*voice);
    if (options.fade_in > 0.0f) {
        ma_sound_set_fade_in_milliseconds(&voice->sound, 0.0f, 1.0f, milliseconds(options.fade_in));
    }
    ma_sound_start(&voice->sound);
    const SoundId id = voice->id;
    streams_.push_back(std::move(voice));
    return id;
}

void Audio::stop_music(float fade_seconds) {
    if (!music_) {
        return;
    }
    ma_sound_stop_with_fade_in_milliseconds(&music_->sound, milliseconds(fade_seconds));
    music_->stopping = true;
    fading_musics_.push_back(std::move(music_));
}

const Music* Audio::music() const {
    return music_ ? &*music_->asset : nullptr;
}

void Audio::set_music_volume(float volume, float fade_seconds) {
    if (music_) {
        ma_sound_set_fade_in_milliseconds(&music_->sound, -1.0f, std::max(volume, 0.0f), milliseconds(fade_seconds));
    }
}

// --- Groups and settings

void Audio::apply_master() {
    if (!engine_->initialized) {
        return;
    }
    const bool silent = mute_in_background_ && !focused_;
    ma_engine_set_volume(&engine_->engine, silent ? 0.0f : slider_gain(master_));
}

void Audio::apply_group(SoundGroup group) {
    if (!engine_->initialized) {
        return;
    }
    const Group& g = groups_[static_cast<std::size_t>(group)];
    ma_sound_group* node = &engine_->groups[static_cast<std::size_t>(group)];
    const Bus* mix = static_cast<std::size_t>(group) < buses_.size() ? buses_[static_cast<std::size_t>(group)].get() : nullptr;
    ma_sound_group_set_volume(node, slider_gain(g.slider) * (mix != nullptr ? mix->volume : 1.0f));
    if (g.paused || (mix != nullptr && mix->paused)) {
        ma_sound_group_stop(node);
    } else {
        ma_sound_group_start(node);
    }
}

void Audio::set_master_volume(float slider) {
    master_ = std::clamp(slider, 0.0f, 1.0f);
    settings_changed_ = true;
    apply_master();
}

void Audio::set_group_volume(SoundGroup group, float slider) {
    groups_[static_cast<std::size_t>(group)].slider = std::clamp(slider, 0.0f, 1.0f);
    settings_changed_ = true;
    apply_group(group);
}

void Audio::set_group_paused(SoundGroup group, bool paused) {
    groups_[static_cast<std::size_t>(group)].paused = paused;
    apply_group(group);
}

void Audio::set_mute_in_background(bool mute) {
    mute_in_background_ = mute;
    settings_changed_ = true;
    apply_master();
}

void Audio::set_focus(bool focused) {
    focused_ = focused;
    apply_master();
}

std::string Audio::settings_json() const {
    nlohmann::ordered_json json;
    json["version"] = 1;
    json["master"] = master_;
    nlohmann::ordered_json volumes;
    for (int i = 0; i < kSoundGroupCount; ++i) {
        volumes[sound_group_name(static_cast<SoundGroup>(i))] = groups_[static_cast<std::size_t>(i)].slider;
    }
    json["volumes"] = volumes;
    json["mute_in_background"] = mute_in_background_;
    return json.dump(2) + "\n";
}

void Audio::apply_settings_json(std::string_view json_text, const std::string& source_name) {
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(json_text);
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error("Audio settings '" + source_name + "': " + e.what());
    }
    if (!json.is_object() || json.value("version", 0) != 1) {
        throw std::runtime_error("Audio settings '" + source_name + "': not a version 1 settings file");
    }
    // Everything is read before anything changes: a bad value leaves the settings as they were.
    float master = master_;
    std::array<float, kSoundGroupCount> sliders{};
    for (int i = 0; i < kSoundGroupCount; ++i) {
        sliders[static_cast<std::size_t>(i)] = groups_[static_cast<std::size_t>(i)].slider;
    }
    bool mute_in_background = mute_in_background_;
    try {
        if (json.contains("master")) {
            master = std::clamp(json["master"].get<float>(), 0.0f, 1.0f);
        }
        if (json.contains("volumes")) {
            for (int i = 0; i < kSoundGroupCount; ++i) {
                const char* name = sound_group_name(static_cast<SoundGroup>(i));
                if (json["volumes"].contains(name)) {
                    sliders[static_cast<std::size_t>(i)] = std::clamp(json["volumes"][name].get<float>(), 0.0f, 1.0f);
                }
            }
        }
        if (json.contains("mute_in_background")) {
            mute_in_background = json["mute_in_background"].get<bool>();
        }
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error("Audio settings '" + source_name + "': " + e.what());
    }
    master_ = master;
    for (int i = 0; i < kSoundGroupCount; ++i) {
        groups_[static_cast<std::size_t>(i)].slider = sliders[static_cast<std::size_t>(i)];
    }
    mute_in_background_ = mute_in_background;
    apply_master();
    for (int i = 0; i < kSoundGroupCount; ++i) {
        apply_group(static_cast<SoundGroup>(i));
    }
    settings_changed_ = false;
}

void Audio::set_listener(const Camera3D& camera) {
    const glm::vec3 forward = camera.forward();
    glm::vec3 right(-forward.z, 0.0f, forward.x);  // forward turned a quarter clockwise, on the ground
    const float length = glm::length(right);
    listener_.position = camera.target();
    listener_.right = length > 1e-5f ? right / length : glm::vec3(1.0f, 0.0f, 0.0f);
}

// --- Frame

void Audio::watch_device() {
    Engine& e = *engine_;
    if (!e.has_device) {
        return;
    }
    ma_device* device = ma_engine_get_device(&e.engine);
    if (e.rerouted.exchange(false)) {
        SDL_Log("Audio: now playing on %s", stats().device_name.c_str());
    }
    if (e.stopped.load() && ma_device_get_state(device) != ma_device_state_started) {
        const double now = now_seconds();
        if (now >= retry_time_) {
            retry_time_ = now + kRetrySeconds;
            if (ma_device_start(device) == MA_SUCCESS) {
                e.stopped.store(false);
                SDL_Log("Audio: sound device started again (%s)", stats().device_name.c_str());
            }
        }
    } else if (e.stopped.load()) {
        e.stopped.store(false);
    }
}

void Audio::update() {
    if (!engine_->initialized) {
        queue_.clear();
        return;
    }
    watch_device();

    // Ended voices go (their sound handle with them: the asset can be collected).
    const ma_uint64 now = ma_engine_get_time_in_pcm_frames(&engine_->engine);
    const auto waiting = [now](const ma_sound& sound) {
        // Scheduled to start later (PlaySound::start_at): not playing yet, not ended.
        return ma_node_get_state_time(&sound, ma_node_state_started) > now;
    };
    std::erase_if(voices_, [&](const std::unique_ptr<Voice>& voice) {
        return !voice->sound_initialized ||
               ((!ma_sound_is_playing(&voice->sound) && !waiting(voice->sound)) || ma_sound_at_end(&voice->sound));
    });
    std::erase_if(fading_musics_, [](const std::unique_ptr<MusicVoice>& music) {
        return !ma_sound_is_playing(&music->sound);
    });
    std::erase_if(streams_, [&](const std::unique_ptr<MusicVoice>& stream) {
        return (!ma_sound_is_playing(&stream->sound) && !waiting(stream->sound)) || ma_sound_at_end(&stream->sound);
    });

    // The listener may have moved, and so may the sources.
    for (auto& voice : voices_) {
        if (voice->options.position) {
            apply_placement(*voice);
        }
    }
    for (auto& stream : streams_) {
        if (stream->options.position) {
            apply_placement(*stream);
        }
    }

    if (queue_.empty()) {
        return;
    }
    std::vector<Request> queue = std::move(queue_);
    queue_.clear();
    std::vector<VoiceInfo> requests;
    requests.reserve(queue.size());
    for (const Request& request : queue) {
        requests.push_back({reinterpret_cast<std::uintptr_t>(&*request.sound), loudness(request.options, request.options.volume),
                            request.options.priority, 0});
    }
    std::vector<std::size_t> kept;
    counters_.merged += merge_requests(requests, &kept);

    for (std::size_t k = 0; k < kept.size(); ++k) {
        // The voices that still play, as the rules see them (a stopping one is already on its way out).
        std::vector<VoiceInfo> playing;
        std::vector<Voice*> owners;
        for (auto& voice : voices_) {
            if (!voice->stopping) {
                playing.push_back({voice->key, voice->loudness, voice->options.priority, voice->order});
                owners.push_back(voice.get());
            }
        }
        const VoiceDecision decision = decide_voice(playing, requests[k], limits);
        switch (decision.kind) {
            case VoiceDecision::Kind::DropInaudible:
                ++counters_.dropped_inaudible;
                refused(queue[kept[k]], "inaudible");
                continue;
            case VoiceDecision::Kind::DropSoundLimit:
                ++counters_.dropped_sound_limit;
                refused(queue[kept[k]], "sound_limit");
                continue;
            case VoiceDecision::Kind::DropVoiceLimit:
                ++counters_.dropped_voice_limit;
                refused(queue[kept[k]], "voice_limit");
                continue;
            case VoiceDecision::Kind::Steal: {
                Voice* victim = owners[decision.victim];
                ma_sound_stop_with_fade_in_milliseconds(&victim->sound, milliseconds(kStealFadeSeconds));
                victim->stopping = true;
                ++counters_.stolen;
                break;
            }
            case VoiceDecision::Kind::Start: break;
        }
        start(queue[kept[k]], requests[k].loudness);
    }
}

void Audio::mix(std::span<float> out) {
    std::fill(out.begin(), out.end(), 0.0f);
    Engine& e = *engine_;
    if (!e.initialized || e.has_device || e.channels <= 0) {
        return;
    }
    ma_engine_read_pcm_frames(&e.engine, out.data(), out.size() / static_cast<std::size_t>(e.channels), nullptr);
}

AudioStats Audio::stats() const {
    const Engine& e = *engine_;
    AudioStats stats = counters_;
    stats.voices = static_cast<int>(std::count_if(voices_.begin(), voices_.end(),
                                                  [](const std::unique_ptr<Voice>& voice) { return !voice->stopping; }));
    stats.musics = (music_ ? 1 : 0) + static_cast<int>(fading_musics_.size());
    stats.streams = static_cast<int>(std::count_if(streams_.begin(), streams_.end(),
                                                   [](const std::unique_ptr<MusicVoice>& s) { return !s->stopping; }));
    stats.peak = e.peak.load(std::memory_order_relaxed);
    stats.mix_peak = e.mix_peak.load(std::memory_order_relaxed);
    stats.limiter_gain = e.lowest_gain.load(std::memory_order_relaxed);
    stats.sample_rate = e.sample_rate;
    stats.channels = e.channels;
    if (e.initialized && e.has_device) {
        ma_device* device = ma_engine_get_device(const_cast<ma_engine*>(&e.engine));
        stats.device = ma_device_get_state(device) == ma_device_state_started;
        char name[256] = {};
        if (ma_device_get_name(device, ma_device_type_playback, name, sizeof(name), nullptr) == MA_SUCCESS) {
            stats.device_name = name;
        }
        if (device->playback.internalSampleRate > 0) {
            stats.device_latency_ms = 1000.0f * static_cast<float>(device->playback.internalPeriodSizeInFrames) *
                                      static_cast<float>(device->playback.internalPeriods) /
                                      static_cast<float>(device->playback.internalSampleRate);
        }
    } else {
        stats.device_name = e.initialized ? "(no device)" : "(none)";
    }
    return stats;
}

void Audio::refused(const Request& request, const char* reason) {
    counters_.last_refused = request.sound->name;
    counters_.last_refused_reason = reason;
}

std::vector<Audio::ActiveSound> Audio::active_sounds() const {
    std::vector<ActiveSound> sounds;
    sounds.reserve(voices_.size());
    for (const auto& voice : voices_) {
        ActiveSound sound;
        sound.name = voice->asset->name;
        sound.group = voice->options.group;
        sound.loudness = voice->loudness;
        sound.placed = voice->options.position.has_value();
        sound.looping = voice->options.loop;
        sound.stopping = voice->stopping;
        sounds.push_back(std::move(sound));
    }
    return sounds;
}

void Audio::reset_peak() {
    engine_->peak.store(0.0f, std::memory_order_relaxed);
    engine_->mix_peak.store(0.0f, std::memory_order_relaxed);
    engine_->lowest_gain.store(1.0f, std::memory_order_relaxed);
}

}  // namespace moteur
