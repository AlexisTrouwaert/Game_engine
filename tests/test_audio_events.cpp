#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "moteur/audio.hpp"
#include "moteur/audio_events.hpp"
#include "moteur/sound.hpp"
#include "moteur/sound_bank.hpp"

namespace {

constexpr int kRate = 48000;

std::vector<std::uint8_t> sine_wav(float frequency, float seconds, float amplitude) {
    const int frames = static_cast<int>(seconds * kRate);
    std::vector<float> samples(static_cast<std::size_t>(frames));
    for (int i = 0; i < frames; ++i) {
        samples[static_cast<std::size_t>(i)] = amplitude * std::sin(2.0f * 3.14159265f * frequency * static_cast<float>(i) / kRate);
    }
    return moteur::encode_wav(samples, 1, kRate);
}

moteur::Audio silent_audio() {
    moteur::AudioConfig config;
    config.device = false;
    config.sample_rate = kRate;
    config.channels = 2;
    return moteur::Audio(config);
}

// The bank of these tests: "loud.wav" and "quiet.wav" are sines, "silence.wav" is silent.
constexpr const char* kBank = R"({
  "version": 1,
  "buses": { "effects/pas": { "volume": 0.5 } },
  "reverbs": { "salle": { "room": 0.8, "damping": 0.4, "wet": 0.5 } },
  "reverb_buses": ["effects"],
  "snapshots": { "pause": { "fade": 0, "buses": { "effects": { "paused": true } } } },
  "vcas": { "combat": ["effects"] },
  "parameters": { "sol": { "labels": ["pave", "dalle"], "default": "pave" } },
  "events": {
    "bip": { "sounds": ["loud.wav"], "loop": true, "max_instances": 2, "steal": "none" },
    "vole": { "sounds": ["loud.wav"], "loop": true, "max_instances": 1, "steal": "oldest" },
    "pas": { "bus": "effects/pas", "loop": true,
             "sounds": { "parameter": "sol", "cases": { "pave": ["silence.wav"], "dalle": ["loud.wav"] } } },
    "mur": { "sounds": ["loud.wav"], "loop": true, "spatial": { "min": 1, "max": 50, "occlusion": true } },
    "musique": {
      "bus": "music",
      "parameters": { "section": { "labels": ["a", "b"] } },
      "music": { "parameter": "section", "quantize": "beat", "crossfade": 0.05,
                 "sections": { "a": { "file": "a.wav", "tempo": 600 }, "b": { "file": "b.wav", "tempo": 600 } } }
    }
  }
})";

struct Fixture {
    moteur::Audio audio = silent_audio();
    std::map<std::string, std::vector<std::uint8_t>> files{{"loud.wav", sine_wav(440.0f, 0.5f, 0.5f)},
                                                           {"silence.wav", sine_wav(440.0f, 0.5f, 0.0f)},
                                                           {"a.wav", sine_wav(220.0f, 1.0f, 0.3f)},
                                                           {"b.wav", sine_wav(330.0f, 1.0f, 0.3f)}};
    std::string bank_text = kBank;
    moteur::AudioEvents events{audio, moteur::AudioEvents::Loaders{
                                          [this](const std::string& path) {
                                              return moteur::make_asset(moteur::SoundBank::parse(bank_text, path));
                                          },
                                          [this](const std::string& path) {
                                              return moteur::make_asset(moteur::decode_sound(files.at(path), path));
                                          },
                                          [this](const std::string& path) {
                                              return moteur::make_asset(moteur::open_music(files.at(path), path));
                                          }}};

    Fixture() { events.load("test.bank.json"); }

    // A frame: the events, the queue, then `frames` mixed; the energy of the left channel.
    double step(int frames = 4800) {
        events.update(static_cast<float>(frames) / kRate);
        audio.update();
        std::vector<float> out(static_cast<std::size_t>(frames) * 2);
        audio.mix(out);
        double energy = 0.0;
        for (int i = 0; i < frames; ++i) {
            energy += out[static_cast<std::size_t>(2 * i)] * out[static_cast<std::size_t>(2 * i)];
        }
        return energy;
    }
};

const char* kMinimal = R"({"version": 1, "events": {"e": {"sounds": ["a.wav"]}}})";

std::string with(const std::string& events_or_more) {
    return R"({"version": 1, )" + events_or_more + "}";
}

void check_refused(const std::string& text, const std::string& expected) {
    CAPTURE(text);
    try {
        moteur::SoundBank::parse(text, "test.bank.json");
        FAIL("accepted");
    } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        CAPTURE(message);
        CHECK(message.find("test.bank.json") != std::string::npos);
        CHECK(message.find(expected) != std::string::npos);
    }
}

}  // namespace

TEST_CASE("sound bank: events, buses parents first, the files it names") {
    const moteur::SoundBank bank = moteur::SoundBank::parse(kBank, "test.bank.json");
    CHECK(bank.source() == "test.bank.json");
    CHECK(bank.events().size() == 5);
    const moteur::AudioEventDescription* pas = bank.event("pas");
    REQUIRE(pas != nullptr);
    CHECK(pas->bus == "effects/pas");
    CHECK(pas->switch_parameter == "sol");
    CHECK(pas->cases.at("dalle") == std::vector<std::string>{"loud.wav"});
    CHECK(bank.event("absent") == nullptr);
    REQUIRE(bank.buses().size() == 1);
    CHECK(bank.buses()[0].parent == "effects");
    CHECK(bank.buses()[0].volume == doctest::Approx(0.5f));
    CHECK(bank.parameters().at("sol").label("dalle") == 1);
    CHECK(bank.parameters().at("sol").initial == doctest::Approx(0.0f));
    const moteur::AudioEventDescription* music = bank.event("musique");
    REQUIRE(music != nullptr);
    CHECK(music->is_music);
    CHECK(music->music.quantize == moteur::AudioEventDescription::Music::Quantize::Beat);
    const std::vector<std::string> files = bank.files();
    for (const char* f : {"loud.wav", "silence.wav", "a.wav", "b.wav"}) {
        CHECK(std::find(files.begin(), files.end(), f) != files.end());
    }
    CHECK(moteur::SoundBank::is_base_bus("effects"));
    CHECK_FALSE(moteur::SoundBank::is_base_bus("effects/pas"));
    // Each parse is a bank of its own (a reload is seen by it).
    CHECK(moteur::SoundBank::parse(kMinimal, "a").id() != moteur::SoundBank::parse(kMinimal, "a").id());
}

TEST_CASE("sound bank: what it refuses names the bank and the place") {
    check_refused("{", "not valid");
    check_refused(R"({"version": 2, "events": {"e": {"sounds": ["a.wav"]}}})", "version 2");
    check_refused(with(R"("evnets": {})"), "unknown key 'evnets'");
    check_refused(with(R"("events": {})"), "no events");
    check_refused(with(R"("events": {"e": {"sounds": ["a.wav"], "volum": 1}})"), "unknown key 'volum'");
    check_refused(with(R"("events": {"e": {}})"), "no sounds");
    check_refused(with(R"("events": {"e": {"sounds": ["a.wav"], "bus": "nulle_part"}})"), "no bus 'nulle_part'");
    check_refused(with(R"("buses": {"pas/x": {}}, "events": {"e": {"sounds": ["a.wav"]}})"), "its parent 'pas'");
    check_refused(with(R"("events": {"e": {"sounds": ["a.wav"], "pitch": [1.2, 0.8]}})"), "upside down");
    check_refused(with(R"("events": {"e": {"sounds": ["a.wav"], "order": "melange"}})"), "order");
    check_refused(with(R"("events": {"e": {"sounds": {"parameter": "sol", "cases": {"x": ["a.wav"]}}}})"), "'sol'");
    check_refused(with(R"("events": {"e": {"sounds": ["a.wav"], "automation": [{"parameter": "rien", "property": "volume", "points": [[0, 1]]}]}})"),
                  "no parameter 'rien'");
    check_refused(with(R"("parameters": {"p": {}}, "events": {"e": {"sounds": ["a.wav"], "automation": [{"parameter": "p", "property": "volume", "points": [[1, 1], [0, 1]]}]}})"),
                  "x increasing");
    check_refused(with(R"("parameters": {"s": {"labels": ["a"]}}, "events": {"e": {"music": {"parameter": "s", "sections": {}}}})"),
                  "no section");
    check_refused(with(R"("vcas": {"v": ["absent"]}, "events": {"e": {"sounds": ["a.wav"]}})"), "no bus 'absent'");
}

TEST_CASE("audio curve: linear between its points, flat beyond") {
    moteur::AudioCurve curve{{{0.0f, 1.0f}, {1.0f, 3.0f}, {3.0f, 0.0f}}};
    CHECK(curve.at(-1.0f) == doctest::Approx(1.0f));
    CHECK(curve.at(0.5f) == doctest::Approx(2.0f));
    CHECK(curve.at(2.0f) == doctest::Approx(1.5f));
    CHECK(curve.at(9.0f) == doctest::Approx(0.0f));
    CHECK(moteur::AudioCurve{}.at(0.4f) == doctest::Approx(1.0f));
}

TEST_CASE("audio events: an event plays by name, an unknown one does not") {
    Fixture f;
    CHECK(f.events.has("bip"));
    CHECK(f.events.play("absent") == moteur::kNoEvent);
    const moteur::EventId bip = f.events.play("bip");
    REQUIRE(bip != moteur::kNoEvent);
    CHECK(f.events.playing(bip));
    f.step();
    CHECK(f.step() > 0.0);
    REQUIRE(f.events.instances().size() == 1);
    CHECK(f.events.instances()[0].event == "bip");
    f.events.stop(bip, false);
    CHECK_FALSE(f.events.playing(bip));
    f.step();
    CHECK(f.step() == doctest::Approx(0.0));
    CHECK(f.events.instances().empty());
}

TEST_CASE("audio events: instance limits refuse, or steal the oldest") {
    Fixture f;
    CHECK(f.events.play("bip") != moteur::kNoEvent);
    CHECK(f.events.play("bip") != moteur::kNoEvent);
    CHECK(f.events.play("bip") == moteur::kNoEvent);  // two at most, no stealing
    CHECK(f.events.stats().refused == 1);

    const moteur::EventId first = f.events.play("vole");
    const moteur::EventId second = f.events.play("vole");
    CHECK(second != moteur::kNoEvent);
    CHECK_FALSE(f.events.playing(first));
    CHECK(f.events.playing(second));
    CHECK(f.events.stats().stolen == 1);
}

TEST_CASE("audio events: a labelled parameter chooses the set of sounds (a switch)") {
    Fixture f;
    f.events.play("pas");  // "pave" by default: the silent one
    f.step();
    CHECK(f.step() == doctest::Approx(0.0));
    f.events.stop_all("pas", false);
    f.step();

    f.events.set_global("sol", "dalle");
    CHECK(f.events.global("sol") == doctest::Approx(1.0f));
    f.events.play("pas");
    f.step();
    const double on_slabs = f.step();
    CHECK(on_slabs > 0.0);

    // The same sound on the base bus is louder than on "effects/pas" (its volume is 0.5).
    f.events.stop_all({}, false);
    f.step();
    f.events.play("bip");
    f.step();
    CHECK(f.step() > on_slabs * 2.0);
}

TEST_CASE("audio events: a snapshot pauses a bus, a VCA lowers it") {
    Fixture f;
    f.events.play("bip");
    f.step();
    const double full = f.step();
    REQUIRE(full > 0.0);

    f.events.start_snapshot("pause");
    CHECK(f.events.snapshot_active("pause"));
    f.step();
    CHECK(f.step() == doctest::Approx(0.0));
    CHECK(f.events.instances().size() == 1);  // paused, not stopped
    f.events.stop_snapshot("pause");
    f.step();
    f.step();
    CHECK(f.step() == doctest::Approx(full).epsilon(0.1));

    f.events.set_vca("combat", 0.0f);
    CHECK(f.events.vca("combat") == doctest::Approx(0.0f));
    f.step();
    CHECK(f.step() == doctest::Approx(0.0));
}

TEST_CASE("audio events: the listener's zone gives the reverberation, and it rings on") {
    Fixture f;
    f.events.set_reverb_zones({{"salle", moteur::Aabb{{-5.0f, -5.0f, -5.0f}, {5.0f, 5.0f, 5.0f}}, 1.0f}});
    f.audio.set_listener(moteur::Listener{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}});
    f.step();
    CHECK(f.events.stats().reverb == "salle");
    CHECK(f.events.stats().reverb_wet == doctest::Approx(0.5f));

    // A sound stopped at once still rings in the room.
    const moteur::EventId bip = f.events.play("bip");
    f.step();
    f.step();
    f.events.stop(bip, false);
    f.step(480);
    CHECK(f.step(2400) > 0.0);

    f.audio.set_listener(moteur::Listener{{50.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}});
    f.step();
    CHECK(f.events.stats().reverb.empty());
}

TEST_CASE("audio events: the geometry between them muffles a sound") {
    Fixture f;
    f.audio.set_listener(moteur::Listener{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}});
    bool wall = false;
    f.events.set_occlusion_test([&wall](glm::vec3, glm::vec3) { return wall ? 1.0f : 0.0f; });
    f.events.play("mur", {.position = glm::vec3(0.0f, 0.0f, -5.0f)});
    for (int i = 0; i < 10; ++i) f.step();
    const double open = f.step();
    CHECK(f.events.instances().at(0).occlusion == doctest::Approx(0.0f));

    wall = true;
    for (int i = 0; i < 20; ++i) f.step();
    CHECK(f.events.instances().at(0).occlusion > 0.9f);
    CHECK(f.step() < open * 0.5);
}

TEST_CASE("audio events: a music changes section on the beat") {
    Fixture f;
    const moteur::EventId music = f.events.play("musique");
    REQUIRE(music != moteur::kNoEvent);
    f.step();
    REQUIRE(f.events.instances().size() == 1);
    CHECK(f.events.instances()[0].section == "a");
    CHECK(f.step() > 0.0);

    f.events.set_parameter(music, "section", "b");
    for (int i = 0; i < 6; ++i) f.step();  // 600 ms: past the next beat (100 ms) and the crossfade
    CHECK(f.events.instances()[0].section == "b");
    CHECK(f.step() > 0.0);
    CHECK(f.audio.stats().musics + f.audio.stats().streams >= 1);
}
