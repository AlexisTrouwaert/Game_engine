#include "audio_test.hpp"

#include <imgui.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

namespace {

constexpr float kMapMetres = 32.0f;  // the map shows this far from the listener, each way
constexpr int kBurstSounds = 200;
constexpr int kBurstTicks = 60;      // one second at 60 ticks per second

const char* const kGroupNames[] = {"Musique", "Effets", "Ambiance", "Interface"};

}  // namespace

AudioTest::AudioTest(moteur::Application& app, const Options& options, bool standalone)
    : app_(app), options_(options), standalone_(standalone) {
    moteur::Assets& assets = app.assets();
    for (int i = 0; i < 5; ++i) {
        footsteps_.push_back(assets.sound("audio/kenney_impact/footstep_concrete_00" + std::to_string(i) + ".ogg"));
        impacts_.push_back(assets.sound("audio/kenney_impact/impactMetal_light_00" + std::to_string(i) + ".ogg"));
    }
    click_ = assets.sound("audio/kenney_interface/click_001.ogg");
    confirm_ = assets.sound("audio/kenney_interface/confirmation_001.ogg");
    fire_ = assets.sound("audio/effects/fire_1.wav");
    title_music_ = assets.music("audio/music/the_field_of_dreams.mp3");
    game_music_ = assets.music("audio/music/town_theme.mp3");
    ambience_ = assets.music("audio/ambience/forgotten_tombs.mp3");

    moteur::Audio& audio = app.audio();
    audio.set_listener(moteur::Listener{});  // the origin, right along X: the map's middle
    moteur::PlaySound fire;
    fire.loop = true;
    fire.position = glm::vec3(orbit_radius_, 0.0f, 0.0f);
    fire.fade_in = 0.5f;
    fire.priority = 1;  // the burst must not take its voice
    fire_voice_ = audio.play(fire_, fire);
    if (options_.burst) {
        start_burst();
    }
}

AudioTest::~AudioTest() {
    app_.audio().stop_all(0.2f);
}

void AudioTest::on_event(const SDL_Event& event) {
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE && standalone_) {
        app_.quit();
    }
}

void AudioTest::start_burst() {
    burst_left_ = kBurstSounds;
    burst_tick_ = 0;
    recent_.clear();
    app_.audio().reset_peak();
}

void AudioTest::play_impact(glm::vec3 position) {
    moteur::PlaySound impact;
    impact.position = position;
    impact.pitch_variation = 0.08f;
    impact.volume_variation = 0.1f;
    app_.audio().play(impacts_, impact);
    recent_.push_back(position);
    if (recent_.size() > 64) {
        recent_.erase(recent_.begin());
    }
}

void AudioTest::update(double dt) {
    elapsed_ += dt;
    if (options_.run_seconds > 0.0 && elapsed_ >= options_.run_seconds) {
        if (standalone_) {
            app_.quit();
        } else {
            stop_requested_ = true;
        }
    }
    moteur::Audio& audio = app_.audio();

    if (orbit_) {
        orbit_angle_ += orbit_speed_ * static_cast<float>(dt);
    }
    fire_position_ = glm::vec3(std::cos(orbit_angle_), 0.0f, -std::sin(orbit_angle_)) * orbit_radius_;
    audio.set_position(fire_voice_, fire_position_);

    // The burst: as many impacts per tick as needed to spread 200 over one second, all around.
    if (burst_left_ > 0) {
        ++burst_tick_;
        const int due = kBurstSounds * std::min(burst_tick_, kBurstTicks) / kBurstTicks - (kBurstSounds - burst_left_);
        for (int i = 0; i < due; ++i) {
            random_ = random_ * 1664525u + 1013904223u;
            const float angle = static_cast<float>(random_ >> 8) / 16777216.0f * 6.2831853f;
            random_ = random_ * 1664525u + 1013904223u;
            const float distance = 2.0f + static_cast<float>(random_ >> 8) / 16777216.0f * 20.0f;
            play_impact({std::cos(angle) * distance, 0.0f, std::sin(angle) * distance});
            --burst_left_;
        }
    }

    // Footsteps of someone walking by the listener, every 0.45 s.
    if (walking_) {
        step_timer_ -= dt;
        if (step_timer_ <= 0.0) {
            step_timer_ += 0.45;
            moteur::PlaySound step;
            step.position = glm::vec3(-3.0f, 0.0f, 2.0f);
            step.pitch_variation = 0.05f;
            step.volume_variation = 0.15f;
            audio.play(footsteps_, step);
        }
    }
    max_voices_ = std::max(max_voices_, audio.stats().voices);
}

void AudioTest::render(moteur::Renderer& renderer, double) {
    renderer.set_clear_color(0.07f, 0.08f, 0.1f);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 10.0f, viewport->WorkPos.y + 10.0f),
                            ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::Begin("Carte des sons", nullptr,
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoCollapse);
    draw_map();
    ImGui::End();
}

void AudioTest::draw_map() {
    const float size = 360.0f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("map", ImVec2(size, size));
    const float scale = size * 0.5f / kMapMetres;
    const ImVec2 centre(origin.x + size * 0.5f, origin.y + size * 0.5f);
    // Map: x to the right, -z up (as the screen of a game seen from above).
    const auto to_screen = [&](glm::vec3 p) { return ImVec2(centre.x + p.x * scale, centre.y + p.z * scale); };
    if (ImGui::IsItemClicked()) {
        const ImVec2 mouse = ImGui::GetMousePos();
        play_impact({(mouse.x - centre.x) / scale, 0.0f, (mouse.y - centre.y) / scale});
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + size, origin.y + size), IM_COL32(20, 24, 30, 255));
    const moteur::Attenuation& a = app_.audio().attenuation;
    draw->AddCircle(centre, a.max_distance * scale, IM_COL32(90, 90, 110, 255), 64);
    draw->AddCircle(centre, a.min_distance * scale, IM_COL32(140, 140, 170, 255), 48);
    draw->AddLine(ImVec2(centre.x - a.pan_distance * scale, origin.y + size - 12.0f),
                  ImVec2(centre.x + a.pan_distance * scale, origin.y + size - 12.0f), IM_COL32(120, 120, 140, 255), 2.0f);
    draw->AddText(ImVec2(origin.x + 6.0f, origin.y + size - 30.0f), IM_COL32(150, 150, 170, 255), "panoramique complet");
    draw->AddText(ImVec2(centre.x + a.min_distance * scale + 3.0f, centre.y - 8.0f), IM_COL32(150, 150, 170, 255), "plein");
    draw->AddText(ImVec2(centre.x + a.max_distance * scale - 40.0f, centre.y - 8.0f), IM_COL32(110, 110, 130, 255),
                  "muet");
    for (const glm::vec3& p : recent_) {
        draw->AddCircleFilled(to_screen(p), 3.0f, IM_COL32(200, 200, 220, 140));
    }
    if (walking_) {
        draw->AddCircleFilled(to_screen({-3.0f, 0.0f, 2.0f}), 5.0f, IM_COL32(120, 200, 255, 255));
    }
    draw->AddCircleFilled(to_screen(fire_position_), 7.0f, IM_COL32(255, 150, 40, 255));
    draw->AddTriangleFilled(ImVec2(centre.x, centre.y - 8.0f), ImVec2(centre.x - 6.0f, centre.y + 6.0f),
                            ImVec2(centre.x + 6.0f, centre.y + 6.0f), IM_COL32(120, 255, 140, 255));
    ImGui::TextDisabled("Triangle vert : l'auditeur ; orange : le feu ; clic : un impact à cet endroit.");
}

void AudioTest::draw_controls() {
    moteur::Audio& audio = app_.audio();
    const moteur::AudioStats stats = audio.stats();
    ImGui::Text("Sortie : %s, %d Hz, %d canaux, tampon %.1f ms%s", stats.device_name.c_str(), stats.sample_rate,
                stats.channels, static_cast<double>(stats.device_latency_ms), stats.device ? "" : " (arrêtée)");
    ImGui::Text("Voix : %d / %d (jusqu'à %d par son), flux : %d, musiques : %d", stats.voices, audio.limits.max_voices,
                audio.limits.max_per_sound, stats.streams, stats.musics);
    ImGui::Text("Jouées %zu, fusionnées %zu, remplacées %zu ; non jouées : %zu inaudibles, %zu limite par son, %zu limite "
                "totale",
                stats.played, stats.merged, stats.stolen, stats.dropped_inaudible, stats.dropped_sound_limit,
                stats.dropped_voice_limit);
    ImGui::Text("Crête du mélange %.2f, de la sortie %.2f%s ; limiteur jusqu'à x%.2f", static_cast<double>(stats.mix_peak),
                static_cast<double>(stats.peak), stats.peak >= 1.0f ? " (saturation)" : "",
                static_cast<double>(stats.limiter_gain));
    ImGui::SameLine();
    if (ImGui::SmallButton("Remettre à zéro")) {
        audio.reset_peak();
    }

    if (ImGui::CollapsingHeader("Volumes", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushItemWidth(200.0f);
        float master = audio.master_volume();
        if (ImGui::SliderFloat("Général", &master, 0.0f, 1.0f, "%.2f")) {
            audio.set_master_volume(master);
        }
        for (int i = 0; i < moteur::kSoundGroupCount; ++i) {
            const auto group = static_cast<moteur::SoundGroup>(i);
            float volume = audio.group_volume(group);
            if (ImGui::SliderFloat(kGroupNames[i], &volume, 0.0f, 1.0f, "%.2f")) {
                audio.set_group_volume(group, volume);
            }
            ImGui::SameLine();
            bool paused = audio.group_paused(group);
            if (ImGui::Checkbox((std::string("Pause##") + kGroupNames[i]).c_str(), &paused)) {
                audio.set_group_paused(group, paused);
            }
        }
        bool mute = audio.mute_in_background();
        if (ImGui::Checkbox("Muet quand la fenêtre n'a pas le focus", &mute)) {
            audio.set_mute_in_background(mute);
        }
        ImGui::PopItemWidth();
    }

    if (ImGui::CollapsingHeader("Sons", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("Pas")) {
            moteur::PlaySound step;
            step.pitch_variation = 0.05f;
            audio.play(footsteps_, step);
        }
        ImGui::SameLine();
        if (ImGui::Button("Impact")) {
            play_impact({0.0f, 0.0f, -2.0f});
        }
        ImGui::SameLine();
        if (ImGui::Button("Clic (interface)")) {
            moteur::PlaySound ui;
            ui.group = moteur::SoundGroup::Interface;
            audio.play(click_, ui);
        }
        ImGui::SameLine();
        if (ImGui::Button("Validation (interface)")) {
            moteur::PlaySound ui;
            ui.group = moteur::SoundGroup::Interface;
            audio.play(confirm_, ui);
        }
        ImGui::Checkbox("Quelqu'un marche près de l'auditeur", &walking_);
        if (ImGui::Button(burst_left_ > 0 ? "Rafale en cours..." : "Rafale : 200 impacts en une seconde")) {
            start_burst();
        }
        ImGui::TextDisabled("Voix au plus : %d", max_voices_);
    }

    if (ImGui::CollapsingHeader("Feu qui tourne", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushItemWidth(200.0f);
        ImGui::Checkbox("Tourner", &orbit_);
        ImGui::SliderFloat("Rayon (m)", &orbit_radius_, 0.0f, 35.0f, "%.1f");
        ImGui::SliderFloat("Vitesse (rad/s)", &orbit_speed_, 0.0f, 3.0f, "%.2f");
        ImGui::PopItemWidth();
        const moteur::Placement placement = moteur::place_sound(audio.listener(), fire_position_, audio.attenuation);
        ImGui::Text("Gain %.2f, panoramique %+.2f", static_cast<double>(placement.gain), static_cast<double>(placement.pan));
    }

    if (ImGui::CollapsingHeader("Musique et ambiance", ImGuiTreeNodeFlags_DefaultOpen)) {
        const moteur::Music* playing = audio.music();
        ImGui::Text("Musique : %s", playing != nullptr ? playing->name.c_str() : "aucune");
        if (ImGui::Button("Titre")) {
            audio.play_music(title_music_);
        }
        ImGui::SameLine();
        if (ImGui::Button("Jeu")) {
            audio.play_music(game_music_);
        }
        ImGui::SameLine();
        if (ImGui::Button("Arrêt")) {
            audio.stop_music();
        }
        ImGui::SameLine();
        if (ImGui::Button("Baisser (pause)")) {
            audio.set_music_volume(0.35f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Remonter")) {
            audio.set_music_volume(1.0f);
        }
        bool ambience = audio.playing(ambience_voice_);
        if (ImGui::Checkbox("Ambiance (flux en boucle)", &ambience)) {
            if (ambience) {
                moteur::PlaySound options;
                options.group = moteur::SoundGroup::Ambience;
                options.loop = true;
                options.fade_in = 1.0f;
                options.volume = 0.6f;
                ambience_voice_ = audio.play_stream(ambience_, options);
            } else {
                audio.stop(ambience_voice_, 1.0f);
            }
        }
    }
}

void AudioTest::report() const {
    const moteur::AudioStats stats = app_.audio().stats();
    std::cout << "audio: " << stats.device_name << ", " << stats.sample_rate << " Hz, " << stats.channels << " channels, "
              << stats.device_latency_ms << " ms of buffer\n";
    std::cout << "audio: " << stats.played << " played, " << stats.merged << " merged, " << stats.stolen << " stolen, "
              << stats.dropped_inaudible << " inaudible, " << stats.dropped_sound_limit << " over the limit per sound, "
              << stats.dropped_voice_limit << " over the voice limit\n";
    std::cout << "audio: at most " << max_voices_ << " voices (limit " << app_.audio().limits.max_voices << "), mix peak "
              << stats.mix_peak << ", output peak " << stats.peak << ", limiter down to x" << stats.limiter_gain << '\n';
}
