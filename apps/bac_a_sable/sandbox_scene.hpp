#pragma once

#include <cstdint>

#include <imgui.h>

#include "moteur/application.hpp"
#include "moteur/renderer.hpp"

// What the sandbox's menu needs from a test scene, on top of moteur::Game: a panel of its own
// settings while it runs, and a way to ask to stop (Escape, or --run-seconds reached).
class SandboxScene : public moteur::Game {
public:
    // ImGui widgets of the running test, drawn inside the menu's "Test en cours" panel.
    virtual void draw_controls() {}
    virtual bool stop_requested() const = 0;
    // true: Escape belongs to the scene now (a pause key), and the menu does not stop the test on it.
    virtual bool uses_escape() const { return false; }
};

// The anti-aliasing choice of the "Rendu" panels (milestone 3, part 12 bis): a player setting, kept
// from test to test. The modes the GPU cannot do are greyed out.
inline void anti_aliasing_combo(moteur::Renderer& renderer) {
    static const char* const kNames[] = {"Aucun", "FXAA", "MSAA 2x", "MSAA 4x"};
    const int current = static_cast<int>(renderer.anti_aliasing());
    if (ImGui::BeginCombo("Anticrénelage", kNames[current])) {
        for (int i = 0; i < IM_ARRAYSIZE(kNames); ++i) {
            const auto mode = static_cast<moteur::AntiAliasing>(i);
            ImGui::BeginDisabled(!renderer.supports(mode));
            if (ImGui::Selectable(kNames[i], i == current)) {
                renderer.set_anti_aliasing(mode);
            }
            ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
}

// Small deterministic random generator, so that a scene is the same on every run and every OS.
class Random {
public:
    explicit Random(std::uint32_t seed) : state_(seed) {}
    // Uniform in [0, 1).
    float next() {
        state_ = state_ * 1664525u + 1013904223u;
        return static_cast<float>(state_ >> 8) / 16777216.0f;
    }

private:
    std::uint32_t state_;
};

// The walls of the demo maps (2D and 3D): a lattice of grid lines with gaps for doorways, so the
// map looks like a set of 10 x 10 rooms without needing a level editor.
inline bool demo_wall(int i, int j) {
    return (i % 10 == 0 && j % 4 != 0) || (j % 10 == 0 && i % 4 != 0);
}
