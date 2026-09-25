#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#include "moteur/audio.hpp"
#include "moteur/audio_rules.hpp"
#include "moteur/sound.hpp"

namespace {

constexpr int kRate = 48000;

// A mono sine, as a sound asset (through a WAV file, as a real one would come).
moteur::Asset<moteur::Sound> tone(float frequency, float seconds, float amplitude = 0.5f) {
    const int frames = static_cast<int>(seconds * kRate);
    std::vector<float> samples(static_cast<std::size_t>(frames));
    for (int i = 0; i < frames; ++i) {
        samples[static_cast<std::size_t>(i)] = amplitude * std::sin(2.0f * 3.14159265f * frequency * static_cast<float>(i) / kRate);
    }
    const std::vector<std::uint8_t> wav = moteur::encode_wav(samples, 1, kRate);
    return moteur::make_asset(moteur::decode_sound(wav, "tone"));
}

// Energy of each channel of a stereo mix.
struct Energy {
    double left = 0.0, right = 0.0;
};
Energy mix_energy(moteur::Audio& audio, int frames) {
    std::vector<float> out(static_cast<std::size_t>(frames) * 2);
    audio.mix(out);
    Energy energy;
    for (int i = 0; i < frames; ++i) {
        energy.left += out[static_cast<std::size_t>(2 * i)] * out[static_cast<std::size_t>(2 * i)];
        energy.right += out[static_cast<std::size_t>(2 * i + 1)] * out[static_cast<std::size_t>(2 * i + 1)];
    }
    return energy;
}

moteur::Audio silent_audio() {
    moteur::AudioConfig config;
    config.device = false;
    config.sample_rate = kRate;
    config.channels = 2;
    return moteur::Audio(config);
}

}  // namespace

TEST_CASE("audio rules: attenuation and pan around the listener") {
    const moteur::Listener listener{{10.0f, 0.0f, 10.0f}, {1.0f, 0.0f, 0.0f}};
    moteur::Attenuation a;
    a.min_distance = 4.0f;
    a.max_distance = 24.0f;
    a.pan_distance = 10.0f;
    a.max_pan = 0.8f;

    const moteur::Placement here = moteur::place_sound(listener, {10.0f, 0.0f, 10.0f}, a);
    CHECK(here.gain == doctest::Approx(1.0f));
    CHECK(here.pan == doctest::Approx(0.0f));

    CHECK(moteur::place_sound(listener, {13.0f, 0.0f, 10.0f}, a).gain == doctest::Approx(1.0f));  // inside min_distance
    CHECK(moteur::place_sound(listener, {24.0f, 0.0f, 10.0f}, a).gain == doctest::Approx(0.25f));  // half way: a quarter
    CHECK(moteur::place_sound(listener, {34.0f, 0.0f, 10.0f}, a).gain == doctest::Approx(0.0f));
    CHECK(moteur::place_sound(listener, {50.0f, 0.0f, 10.0f}, a).gain == doctest::Approx(0.0f));

    CHECK(moteur::place_sound(listener, {15.0f, 0.0f, 10.0f}, a).pan == doctest::Approx(0.4f));
    CHECK(moteur::place_sound(listener, {30.0f, 0.0f, 10.0f}, a).pan == doctest::Approx(0.8f));   // never all on one side
    CHECK(moteur::place_sound(listener, {5.0f, 0.0f, 10.0f}, a).pan == doctest::Approx(-0.4f));
    CHECK(moteur::place_sound(listener, {10.0f, 0.0f, 2.0f}, a).pan == doctest::Approx(0.0f));    // ahead: centred
}

TEST_CASE("audio rules: slider gain") {
    CHECK(moteur::slider_gain(1.0f) == doctest::Approx(1.0f));
    CHECK(moteur::slider_gain(0.5f) == doctest::Approx(0.25f));
    CHECK(moteur::slider_gain(-1.0f) == doctest::Approx(0.0f));
    CHECK(moteur::slider_gain(2.0f) == doctest::Approx(1.0f));
}

TEST_CASE("audio rules: who gets a voice") {
    using Kind = moteur::VoiceDecision::Kind;
    moteur::VoiceLimits limits;
    limits.max_voices = 3;
    limits.max_per_sound = 2;
    std::vector<moteur::VoiceInfo> playing = {
        {1, 0.5f, 0, 1},  // sound 1, older
        {1, 0.5f, 0, 2},
        {2, 0.2f, 0, 3},
    };

    SUBCASE("too quiet") {
        CHECK(moteur::decide_voice({}, {9, 0.0001f, 0, 0}, limits).kind == Kind::DropInaudible);
    }
    SUBCASE("room left") {
        CHECK(moteur::decide_voice(std::span(playing).first(2), {2, 0.1f, 0, 0}, limits).kind == Kind::Start);
    }
    SUBCASE("the limit per sound: the oldest of equals goes") {
        const moteur::VoiceDecision d = moteur::decide_voice(playing, {1, 0.5f, 0, 0}, limits);
        CHECK(d.kind == Kind::Steal);
        CHECK(d.victim == 0);
    }
    SUBCASE("the limit per sound: a quieter one is not played") {
        CHECK(moteur::decide_voice(playing, {1, 0.4f, 0, 0}, limits).kind == Kind::DropSoundLimit);
    }
    SUBCASE("the limit per sound: priority wins over loudness") {
        CHECK(moteur::decide_voice(playing, {1, 0.1f, 1, 0}, limits).kind == Kind::Steal);
    }
    SUBCASE("all voices busy: the quietest goes") {
        const moteur::VoiceDecision d = moteur::decide_voice(playing, {3, 0.3f, 0, 0}, limits);
        CHECK(d.kind == Kind::Steal);
        CHECK(d.victim == 2);
    }
    SUBCASE("all voices busy: quieter than all of them") {
        CHECK(moteur::decide_voice(playing, {3, 0.1f, 0, 0}, limits).kind == Kind::DropVoiceLimit);
    }
    SUBCASE("all voices busy: a voice of higher priority is kept") {
        playing[2].priority = 1;
        CHECK(moteur::decide_voice(playing, {3, 1.0f, 0, 0}, limits).kind == Kind::Steal);
        CHECK(moteur::decide_voice(playing, {3, 1.0f, 0, 0}, limits).victim == 0);
    }
}

TEST_CASE("audio rules: requests of one frame for the same sound become the loudest") {
    std::vector<moteur::VoiceInfo> requests = {
        {1, 0.2f, 0, 0}, {2, 0.5f, 0, 0}, {1, 0.9f, 0, 0}, {1, 0.4f, 0, 0}, {3, 0.1f, 0, 0}, {2, 0.5f, 0, 0},
    };
    std::vector<std::size_t> kept;
    CHECK(moteur::merge_requests(requests, &kept) == 3);
    REQUIRE(requests.size() == 3);
    CHECK(requests[0].sound == 1);
    CHECK(requests[0].loudness == doctest::Approx(0.9f));  // the loudest, at the place of the first
    CHECK(requests[1].sound == 2);
    CHECK(requests[2].sound == 3);
    CHECK(kept == std::vector<std::size_t>{2, 1, 4});  // equal ones: the first stays
}

TEST_CASE("sound: decoding a WAV file, and a broken one") {
    std::vector<float> samples = {0.0f, 0.5f, -0.5f, 0.25f, 1.0f, -1.0f};  // three stereo frames
    const moteur::Sound sound = moteur::decode_sound(moteur::encode_wav(samples, 2, 22050), "stereo.wav");
    CHECK(sound.channels == 2);
    CHECK(sound.sample_rate == 22050);
    REQUIRE(sound.frames() == 3);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        CHECK((*sound.samples)[i] == doctest::Approx(samples[i]).epsilon(0.001));
    }
    CHECK(sound.bytes() == 6 * sizeof(float));

    const std::vector<std::uint8_t> junk = {1, 2, 3, 4, 5, 6, 7, 8};
    CHECK_THROWS_AS(moteur::decode_sound(junk, "junk.ogg"), std::runtime_error);
    CHECK_THROWS_AS(moteur::open_music(junk, "junk.ogg"), std::runtime_error);

    const moteur::Music music = moteur::open_music(moteur::encode_wav(samples, 2, 22050), "music.wav");
    CHECK(music.channels == 2);
    CHECK(music.seconds == doctest::Approx(3.0 / 22050.0));

    const moteur::Sound beep = moteur::placeholder_sound();
    CHECK(beep.frames() > 0);
}

TEST_CASE("audio: a placed sound is heard on its side, and fainter far away") {
    moteur::Audio audio = silent_audio();
    audio.set_listener(moteur::Listener{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}});
    const auto sound = tone(440.0f, 1.0f);

    moteur::PlaySound right;
    right.position = glm::vec3(10.0f, 0.0f, 0.0f);
    const moteur::SoundId id = audio.play(sound, right);
    CHECK(audio.playing(id));
    CHECK(audio.stats().voices == 0);  // queued until the end of the frame
    audio.update();
    CHECK(audio.stats().voices == 1);
    mix_energy(audio, 4800);  // past the volume smoothing
    const Energy on_right = mix_energy(audio, 4800);
    CHECK(on_right.right > 4.0 * on_right.left);

    audio.set_position(id, {-10.0f, 0.0f, 0.0f});
    audio.update();
    mix_energy(audio, 4800);
    const Energy on_left = mix_energy(audio, 4800);
    CHECK(on_left.left > 4.0 * on_left.right);

    audio.set_position(id, {-25.0f, 0.0f, 0.0f});  // attenuation.max_distance is 30 m
    audio.update();
    mix_energy(audio, 4800);
    const Energy far = mix_energy(audio, 4800);
    CHECK(far.left + far.right < 0.2 * (on_left.left + on_left.right));
}

TEST_CASE("audio: an ended sound frees its voice and its asset") {
    moteur::Audio audio = silent_audio();
    auto sound = tone(440.0f, 0.05f);
    std::weak_ptr<moteur::Sound> watch = sound.handle();
    const moteur::SoundId id = audio.play(sound);
    sound = {};
    audio.update();
    CHECK(audio.playing(id));
    CHECK_FALSE(watch.expired());  // the voice holds it while it plays

    mix_energy(audio, 4800);  // 100 ms: past its end
    audio.update();
    CHECK_FALSE(audio.playing(id));
    CHECK(audio.stats().voices == 0);
    CHECK(watch.expired());
}

TEST_CASE("audio: 200 sounds in one frame keep to the limits") {
    moteur::Audio audio = silent_audio();
    audio.limits.max_voices = 16;
    audio.limits.max_per_sound = 4;
    std::vector<moteur::Asset<moteur::Sound>> sounds;
    for (int i = 0; i < 10; ++i) {
        sounds.push_back(tone(200.0f + 50.0f * static_cast<float>(i), 1.0f, 0.1f));
    }
    for (int i = 0; i < 200; ++i) {
        audio.play(sounds[static_cast<std::size_t>(i % 10)]);
    }
    audio.update();
    moteur::AudioStats stats = audio.stats();
    CHECK(stats.merged == 190);  // one per sound in a frame
    CHECK(stats.voices == 10);

    // Then 20 frames of the same: at most 4 per sound, 16 in all.
    for (int frame = 0; frame < 20; ++frame) {
        for (int i = 0; i < 10; ++i) {
            audio.play(sounds[static_cast<std::size_t>(i)]);
        }
        audio.update();
        mix_energy(audio, 480);
        stats = audio.stats();
        CHECK(stats.voices <= 16);
    }
    CHECK(stats.voices == 16);
    CHECK(stats.stolen + stats.dropped_sound_limit + stats.dropped_voice_limit > 0);
}

TEST_CASE("audio: the last refused sound and the active ones, for the debug panel") {
    moteur::Audio audio = silent_audio();
    audio.limits.max_per_sound = 1;
    const moteur::Asset<moteur::Sound> sound = tone(440.0f, 1.0f);
    CHECK(audio.stats().last_refused.empty());
    moteur::PlaySound loop;
    loop.loop = true;
    audio.play(sound, loop);
    audio.update();
    moteur::PlaySound quiet;
    quiet.volume = 0.2f;  // weaker than the one playing: it does not take its place
    audio.play(sound, quiet);
    audio.update();
    const moteur::AudioStats stats = audio.stats();
    CHECK(stats.dropped_sound_limit == 1);
    CHECK(stats.last_refused == "tone");
    CHECK(stats.last_refused_reason == "sound_limit");
    const std::vector<moteur::Audio::ActiveSound> active = audio.active_sounds();
    REQUIRE(active.size() == 1);
    CHECK(active[0].name == "tone");
    CHECK(active[0].looping);
    CHECK_FALSE(active[0].placed);
    CHECK(active[0].group == moteur::SoundGroup::Effects);
}

TEST_CASE("audio: the limiter keeps many loud sounds under full scale") {
    moteur::Audio audio = silent_audio();
    audio.limits.max_per_sound = 16;
    std::vector<moteur::Asset<moteur::Sound>> sounds;
    for (int i = 0; i < 12; ++i) {
        sounds.push_back(tone(300.0f, 0.5f, 0.9f));  // all in phase: they add up to 10.8
    }
    for (const auto& sound : sounds) {
        audio.play(sound);
    }
    audio.update();
    std::vector<float> out(4800 * 2);
    audio.mix(out);
    const moteur::AudioStats stats = audio.stats();
    CHECK(stats.mix_peak > 5.0f);
    CHECK(stats.peak <= 0.9001f);
    CHECK(stats.limiter_gain < 0.2f);
    float highest = 0.0f;
    for (const float sample : out) {
        highest = std::max(highest, std::fabs(sample));
    }
    CHECK(highest <= 0.9001f);

    // Once they are gone, the gain comes back.
    for (int i = 0; i < 30; ++i) {
        audio.mix(out);
    }
    audio.update();
    audio.reset_peak();
    audio.play(tone(300.0f, 0.5f, 0.5f));
    audio.update();
    audio.mix(out);
    CHECK(audio.stats().limiter_gain == doctest::Approx(1.0f).epsilon(0.001));  // back, within 0.1 %
    CHECK(audio.stats().peak == doctest::Approx(0.5f).epsilon(0.02));
}

TEST_CASE("audio: group volumes, pauses and the master volume") {
    moteur::Audio audio = silent_audio();
    const auto sound = tone(440.0f, 2.0f);
    moteur::PlaySound effect;
    effect.loop = true;
    audio.play(sound, effect);
    audio.update();
    mix_energy(audio, 4800);
    const Energy full = mix_energy(audio, 4800);
    CHECK(full.left > 0.0);

    audio.set_group_paused(moteur::SoundGroup::Effects, true);
    const Energy paused = mix_energy(audio, 4800);
    CHECK(paused.left + paused.right == doctest::Approx(0.0));
    CHECK(audio.stats().voices == 1);  // kept, not stopped
    audio.set_group_paused(moteur::SoundGroup::Effects, false);
    mix_energy(audio, 4800);
    CHECK(mix_energy(audio, 4800).left == doctest::Approx(full.left).epsilon(0.05));

    audio.set_group_volume(moteur::SoundGroup::Effects, 0.5f);  // a quarter of the amplitude
    mix_energy(audio, 4800);
    CHECK(mix_energy(audio, 4800).left == doctest::Approx(full.left / 16.0).epsilon(0.05));
    audio.set_group_volume(moteur::SoundGroup::Interface, 0.0f);  // another group: no change
    CHECK(mix_energy(audio, 4800).left == doctest::Approx(full.left / 16.0).epsilon(0.05));

    audio.set_focus(false);  // mute in background, by default
    mix_energy(audio, 4800);
    CHECK(mix_energy(audio, 4800).left == doctest::Approx(0.0));
    audio.set_focus(true);
    audio.set_master_volume(0.0f);
    mix_energy(audio, 4800);
    CHECK(mix_energy(audio, 4800).left == doctest::Approx(0.0));
}

TEST_CASE("audio: music crossfades from one to the next") {
    moteur::Audio audio = silent_audio();
    std::vector<float> samples(kRate);  // one second of a sine
    for (int i = 0; i < kRate; ++i) {
        samples[static_cast<std::size_t>(i)] = 0.3f * std::sin(2.0f * 3.14159265f * 220.0f * static_cast<float>(i) / kRate);
    }
    const auto first = moteur::make_asset(moteur::open_music(moteur::encode_wav(samples, 1, kRate), "first.wav"));
    const auto second = moteur::make_asset(moteur::open_music(moteur::encode_wav(samples, 1, kRate), "second.wav"));

    audio.play_music(first, 0.1f);
    CHECK(audio.music() == &*first);
    mix_energy(audio, 9600);
    CHECK(mix_energy(audio, 4800).left > 0.0);
    audio.play_music(first, 0.1f);  // the same: nothing restarts
    CHECK(audio.stats().musics == 1);

    audio.play_music(second, 0.1f);
    CHECK(audio.music() == &*second);
    CHECK(audio.stats().musics == 2);  // the first fades out
    mix_energy(audio, 9600);           // 200 ms: faded
    audio.update();
    CHECK(audio.stats().musics == 1);

    audio.stop_music(0.05f);
    CHECK(audio.music() == nullptr);
    mix_energy(audio, 4800);
    audio.update();
    CHECK(audio.stats().musics == 0);
}

TEST_CASE("audio: a stream plays in its group, placed, until stopped") {
    moteur::Audio audio = silent_audio();
    audio.set_listener(moteur::Listener{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}});
    std::vector<float> samples(kRate / 10);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        samples[i] = 0.3f * std::sin(2.0f * 3.14159265f * 330.0f * static_cast<float>(i) / kRate);
    }
    const auto ambience = moteur::make_asset(moteur::open_music(moteur::encode_wav(samples, 1, kRate), "wind.wav"));
    moteur::PlaySound options;
    options.group = moteur::SoundGroup::Ambience;
    options.loop = true;
    options.position = glm::vec3(-10.0f, 0.0f, 0.0f);
    const moteur::SoundId id = audio.play_stream(ambience, options);
    CHECK(audio.playing(id));
    CHECK(audio.stats().streams == 1);
    CHECK(audio.stats().voices == 0);  // outside the voice limits
    mix_energy(audio, 9600);
    const Energy energy = mix_energy(audio, 9600);  // 200 ms of a 100 ms loop: it loops
    CHECK(energy.left > 4.0 * energy.right);

    audio.set_group_volume(moteur::SoundGroup::Ambience, 0.0f);
    mix_energy(audio, 4800);
    CHECK(mix_energy(audio, 4800).left == doctest::Approx(0.0));
    audio.stop(id, 0.01f);
    CHECK_FALSE(audio.playing(id));
    mix_energy(audio, 4800);
    audio.update();
    CHECK(audio.stats().streams == 0);
}

TEST_CASE("audio: settings file") {
    moteur::Audio audio = silent_audio();
    CHECK_FALSE(audio.settings_changed());
    audio.set_master_volume(0.8f);
    audio.set_group_volume(moteur::SoundGroup::Music, 0.3f);
    audio.set_mute_in_background(false);
    CHECK(audio.settings_changed());
    const std::string text = audio.settings_json();

    moteur::Audio other = silent_audio();
    other.apply_settings_json(text, "audio.json");
    CHECK(other.master_volume() == doctest::Approx(0.8f));
    CHECK(other.group_volume(moteur::SoundGroup::Music) == doctest::Approx(0.3f));
    CHECK(other.group_volume(moteur::SoundGroup::Effects) == doctest::Approx(1.0f));
    CHECK_FALSE(other.mute_in_background());
    CHECK_FALSE(other.settings_changed());

    other.apply_settings_json(R"({"version": 1, "volumes": {"effects": 0.5}})", "partial.json");
    CHECK(other.group_volume(moteur::SoundGroup::Effects) == doctest::Approx(0.5f));
    CHECK(other.master_volume() == doctest::Approx(0.8f));  // untouched
    CHECK_THROWS_AS(other.apply_settings_json("{", "broken.json"), std::runtime_error);
    CHECK_THROWS_AS(other.apply_settings_json(R"({"version": 2})", "future.json"), std::runtime_error);
}
