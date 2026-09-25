#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

#include "moteur/application.hpp"
#include "moteur/audio.hpp"

#include "sandbox_scene.hpp"

// The audio test (milestone 4, part 7): every test sound and music, a torch fire going round the
// listener, the group volumes, and a burst of 200 impacts in one second to check the limits.
//
// A map seen from above shows the listener in the middle (the centre of the view, in a game), the
// distances where sounds start to fade and fall silent, and the sources: a click on the map plays
// an impact there. The sounds are those of tools/audio/fetch_test_sounds.py; missing ones beep.
class AudioTest final : public SandboxScene {
public:
    struct Options {
        bool burst = false;         // --audio-burst: a burst at the start (with --run-seconds: a check)
        double run_seconds = 0.0;   // > 0 quits by itself (standalone) or asks to stop (menu)
    };

    AudioTest(moteur::Application& app, const Options& options, bool standalone);
    ~AudioTest() override;

    void on_event(const SDL_Event& event) override;
    void update(double dt) override;
    void render(moteur::Renderer& renderer, double alpha) override;
    void draw_controls() override;
    bool stop_requested() const override { return stop_requested_; }

    // For the command line: the most voices seen at once, and the output's peak.
    int max_voices_seen() const { return max_voices_; }
    void report() const;

private:
    void start_burst();
    void draw_map();
    void play_impact(glm::vec3 position);

    moteur::Application& app_;
    Options options_;
    bool standalone_;
    bool stop_requested_ = false;
    double elapsed_ = 0.0;

    std::vector<moteur::Asset<moteur::Sound>> footsteps_;
    std::vector<moteur::Asset<moteur::Sound>> impacts_;
    moteur::Asset<moteur::Sound> click_;
    moteur::Asset<moteur::Sound> confirm_;
    moteur::Asset<moteur::Sound> fire_;
    moteur::Asset<moteur::Music> title_music_;
    moteur::Asset<moteur::Music> game_music_;
    moteur::Asset<moteur::Music> ambience_;

    // The fire going round the listener.
    moteur::SoundId fire_voice_ = moteur::kNoSound;
    bool orbit_ = true;
    float orbit_radius_ = 10.0f;
    float orbit_speed_ = 0.5f;  // radians per second
    float orbit_angle_ = 0.0f;
    glm::vec3 fire_position_{0.0f};
    moteur::SoundId ambience_voice_ = moteur::kNoSound;

    // The burst: 200 impacts spread over one second, from the fixed-rate update.
    int burst_left_ = 0;
    int burst_tick_ = 0;
    std::uint32_t random_ = 12345;
    std::vector<glm::vec3> recent_;  // where the last impacts were asked for (shown on the map)
    int max_voices_ = 0;
    bool walking_ = false;
    double step_timer_ = 0.0;
};
