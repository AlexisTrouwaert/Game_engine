#include "moteur/audio.hpp"

#include <SDL3/SDL.h>

#include <miniaudio.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <utility>

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
    bool buffer_initialized = false;
    bool sound_initialized = false;
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
    engine_.reset();
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
        gain *= place_sound(listener_, *options.position, attenuation).gain;
    }
    return gain;
}

void Audio::apply_placement(Voice& voice) {
    Placement placement;
    if (voice.options.position) {
        placement = place_sound(listener_, *voice.options.position, attenuation);
    }
    voice.loudness = voice.volume * placement.gain;
    ma_sound_set_volume(&voice.sound, voice.loudness);
    ma_sound_set_pan(&voice.sound, placement.pan);
}

void Audio::apply_placement(MusicVoice& stream) {
    Placement placement;
    if (stream.options.position) {
        placement = place_sound(listener_, *stream.options.position, attenuation);
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
    ma_sound_group* group = &e.groups[static_cast<std::size_t>(o.group)];
    const ma_result result = ma_sound_init_from_data_source(&e.engine, &voice->buffer, MA_SOUND_FLAG_NO_SPATIALIZATION,
                                                            group, &voice->sound);
    if (result != MA_SUCCESS) {
        SDL_Log("Audio: cannot play '%s': %s", sound.name.c_str(), ma_result_description(result));
        return;
    }
    voice->sound_initialized = true;
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

std::unique_ptr<Audio::MusicVoice> Audio::open_stream(const Asset<Music>& music, SoundGroup group) {
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
                                                            &e.groups[static_cast<std::size_t>(group)], &voice->sound);
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
    std::unique_ptr<MusicVoice> voice = open_stream(stream, options.group);
    if (!voice) {
        return kNoSound;
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
    ma_sound_group_set_volume(node, slider_gain(g.slider));
    if (g.paused) {
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
    try {
        if (json.contains("master")) {
            master_ = std::clamp(json["master"].get<float>(), 0.0f, 1.0f);
        }
        if (json.contains("volumes")) {
            for (int i = 0; i < kSoundGroupCount; ++i) {
                const char* name = sound_group_name(static_cast<SoundGroup>(i));
                if (json["volumes"].contains(name)) {
                    groups_[static_cast<std::size_t>(i)].slider = std::clamp(json["volumes"][name].get<float>(), 0.0f, 1.0f);
                }
            }
        }
        if (json.contains("mute_in_background")) {
            mute_in_background_ = json["mute_in_background"].get<bool>();
        }
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error("Audio settings '" + source_name + "': " + e.what());
    }
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
    std::erase_if(voices_, [](const std::unique_ptr<Voice>& voice) {
        return !voice->sound_initialized || !ma_sound_is_playing(&voice->sound) || ma_sound_at_end(&voice->sound);
    });
    std::erase_if(fading_musics_, [](const std::unique_ptr<MusicVoice>& music) {
        return !ma_sound_is_playing(&music->sound);
    });
    std::erase_if(streams_, [](const std::unique_ptr<MusicVoice>& stream) {
        return !ma_sound_is_playing(&stream->sound) || ma_sound_at_end(&stream->sound);
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
        stats.device_name = e.initialized ? "(sans périphérique)" : "(aucun)";
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
