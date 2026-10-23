#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "moteur/application.hpp"
#include "moteur/save_file.hpp"
#include "moteur/audio_events.hpp"
#include "moteur/state_stack.hpp"
#include "moteur/ui.hpp"

#include "demo3d.hpp"
#include "sandbox_scene.hpp"

// The playable slice (milestone 4, part 9), grown from the game states test of part 6: a title
// screen, a loading screen, the 3D demo as the game with a hero of its own (Demo3D::Options::hero),
// and a pause over it (the world stays visible, frozen), all on a moteur::StateStack.
//
// Music (part 7): each screen has its own, crossfaded; the pause lowers it and stops the effects
// and ambiences of the game until it resumes. The menus click and confirm (interface sounds).
//
// The menus read actions (menu_up, menu_down, menu_confirm: arrows and Enter, the pad's cross and
// A), so that a gamepad and a replay drive them as well as the mouse. By hand: "Jouer" on the
// title, Escape (or Start) pauses and resumes, "Retour au titre" leaves the game. With `cycles` > 0, an autopilot goes title -> game -> pause -> game -> pause -> title that
// many times, and after each return to the title logs what stays in memory: assets, GPU objects,
// the process's memory. They must not grow from one cycle to the next.
//
// Saves (milestone 7, part 11): "Sauvegarder" in the pause (three slots, with a thumbnail of the
// game), "Continuer" (the most recent save) and "Charger" on the title, an automatic save every two
// minutes of play (two slots in turn; console variable save.auto, in seconds, 0: off), and the
// console commands "save [emplacement]" and "load [emplacement]". The slots are in the player's
// preferences (sauvegardes/); a damaged one shows as unreadable (its backup is read when it can).
class StatesDemo final : public SandboxScene {
public:
    struct Options {
        Demo3D::Options demo;      // the game's world
        int cycles = 0;            // > 0: the autopilot, then it stops (standalone: quits)
        double run_seconds = 0.0;  // > 0: stops (standalone: quits) after this long
        // Non-empty: a PNG of the first pause (the frozen world under it), or, when
        // demo.freeze_after_ticks is set, the demo's own capture once the game has frozen.
        std::string capture_path;
        // Non-empty: the tour of the interface (milestone 9 bis, part 6): each menu opened in turn
        // and captured into this folder (<screen>.png), the problems of its files counted; then it
        // stops (see ui_problems()).
        std::string ui_tour;
    };

    // What stays in memory once back on the title screen.
    struct Measure {
        std::size_t assets = 0;
        std::size_t asset_bytes = 0;  // their GPU memory
        long gpu_objects = 0;
        std::size_t process_bytes = 0;
    };

    StatesDemo(moteur::Application& app, const Options& options, bool standalone);
    ~StatesDemo() override;

    void on_event(const SDL_Event& event) override;
    void update(double dt) override;
    bool waiting() override { return stack_.waiting(); }
    void render(moteur::Renderer& renderer, double alpha) override;
#if !defined(MOTEUR_SHIPPING)
    void draw_controls() override;
#endif
    bool stop_requested() const override { return stop_requested_; }
    bool uses_escape() const override;

    // For the states (they keep a reference to the scene).
    moteur::Application& app() { return app_; }
    // A text of the player in the current language (locales/<language>/tranche.ftl).
    std::string tr(std::string_view id, moteur::LocalizedArgs args = {}) const { return app_.localization().format(id, args); }
    std::string tr(std::string_view id, const std::vector<moteur::LocalizedArg>& args) const {
        return app_.localization().format(id, args);
    }
    const Options& options() const { return options_; }
    // The interface its screens are drawn with (assets/ui/*.ui; no Dear ImGui: the shipped game's).
    moteur::Ui& ui() { return *ui_; }
    // The interface's events of this tick that came from `document` (its path).
    std::vector<moteur::UiEvent> events(std::string_view document) const;
    // The player's size of the interface (console variable ui.scale, 75 to 200).
    int ui_scale_percent() const;
    void set_ui_scale_percent(int percent);
    moteur::StateStack& stack() { return stack_; }
    int cycles_done() const { return static_cast<int>(measures_.size()) - 1; }
    // Back on the title screen, assets collected: records a measure (the first one is the start).
    void measure();
    // The autopilot has run its cycles: stop.
    void finish();
    // A pause checked that the game did not move under it (false: it did).
    void check_frozen(bool frozen) { frozen_failures_ += frozen ? 0 : 1; }
    bool capture_pending() const { return !options_.capture_path.empty() && options_.demo.freeze_after_ticks == 0 && !capture_done_; }
    void capture_done() { capture_done_ = true; }
    const std::string& error() const { return error_; }
    void set_error(const std::string& error) { error_ = error; }

    // One line on the command line: whether the cycles leaked.
    void report() const;
    // After the tour of the interface: the problems found in its screens (unreadable documents,
    // unknown properties, texts that name no message); -1 before it ended.
    int ui_problems() const { return tour_problems_; }

    // The loading screen started, and the game it built is ready: logs how long it took, and what
    // the assets and the process hold then.
    void loading_started();
    void loading_done();
    double last_load_ms() const { return last_load_ms_; }

    // The menus' sounds (interface bus: they go on during the pause).
    void play_click();
    void play_confirm();

    // The sounds as events (milestone 9 bis, part 7): the slice's bank, for the screens and the
    // game (Demo3D::Options::sound). Null without the test sounds: silent.
    moteur::AudioEvents* sound() { return sound_.get(); }
    // The music, one event for the whole test: its section follows the screen ("titre", "jeu"),
    // changed on the next bar.
    void set_music(std::string_view section);

    // Saves.
    moteur::SaveSlots& slots() { return slots_; }
    // The game being played (null outside it): what is saved.
    void set_world(Demo3D* world) { world_ = world; }
    Demo3D* world() const { return world_; }
    // Writes the game into `slot`; its thumbnail is copied from `thumbnail` (a PNG), or captured at
    // the next frame when it is empty. False with `error`.
    bool save_game(const std::string& slot, const std::string& thumbnail, std::string& error);
    // Reads `slot` (or its backup) and starts the game from it; if it cannot, back to the title with
    // the reason.
    void load_game(const std::string& slot);
    // A game of play went by (the game's update): the automatic save when its time comes.
    void played(double dt);
    // The most recent readable slot, or empty.
    std::string latest_slot() const;
    // The thumbnail the pause takes as it opens (the game without the menu).
    std::string pause_thumbnail() const { return slots_.directory() + "_pause.png"; }

private:
    moteur::Application& app_;
    Options options_;
    bool standalone_;
    bool stop_requested_ = false;
    bool capture_done_ = false;
    double elapsed_ = 0.0;
    int frozen_failures_ = 0;
    std::string error_;
    std::vector<Measure> measures_;
    std::unique_ptr<moteur::AudioEvents> sound_;
    moteur::EventId music_ = moteur::kNoEvent;
    Uint64 sound_clock_ = 0;  ///< the last frame's time, for the events' fades
    Uint64 loading_start_ = 0;
    int loads_ = 0;
    double load_max_ms_ = 0.0;
    double last_load_ms_ = 0.0;
    moteur::SaveSlots slots_;
    Demo3D* world_ = nullptr;
    double since_autosave_ = 0.0;  // seconds of play
    int next_auto_ = 0;
    std::string pending_thumbnail_;  // captured with the next frame
    moteur::Variable* save_binary_ = nullptr;  // "save.binary": CBOR (the shipped game's) instead of JSON
    moteur::Variable* save_auto_ = nullptr;    // "save.auto": seconds between automatic saves
    moteur::Variable* ui_scale_ = nullptr;     // "ui.scale": the interface's size, in percent
    std::unique_ptr<moteur::Ui> ui_;
    std::vector<moteur::UiEvent> events_;  // of this tick
    double ui_seconds_ = 0.0;              // ticked since the last frame
    // The tour of the interface.
    void tour_tick();
    int tour_step_ = 0;     // the screen on show
    int tour_ticks_ = 0;    // ticks on it
    int tour_problems_ = -1;
    std::vector<std::string> tour_seen_;  // the problems already counted
    std::string tour_capture_;  // requested with the next frame
    moteur::StateStack stack_;  // last: its states refer to the members above
};
