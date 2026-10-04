#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "moteur/application.hpp"
#include "moteur/save_file.hpp"
#include "moteur/state_stack.hpp"

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
    void render(moteur::Renderer& renderer, double alpha) override;
    void draw_controls() override;
    bool stop_requested() const override { return stop_requested_; }
    bool uses_escape() const override;

    // For the states (they keep a reference to the scene).
    moteur::Application& app() { return app_; }
    const Options& options() const { return options_; }
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

    // The loading screen started, and the game it built is ready: logs how long it took, and what
    // the assets and the process hold then.
    void loading_started();
    void loading_done();
    double last_load_ms() const { return last_load_ms_; }

    // The menus' sounds (interface group: they go on during the pause).
    void play_click();
    void play_confirm();

    // The musics of the title and of the game (milestone 4, part 7), held for the whole test so that
    // going back and forth does not load them again. Without the test sounds, silent.
    const moteur::Asset<moteur::Music>& title_music() const { return title_music_; }
    const moteur::Asset<moteur::Music>& game_music() const { return game_music_; }

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
    moteur::Asset<moteur::Music> title_music_;
    moteur::Asset<moteur::Music> game_music_;
    moteur::Asset<moteur::Sound> click_;
    moteur::Asset<moteur::Sound> confirm_;
    Uint64 loading_start_ = 0;
    double last_load_ms_ = 0.0;
    moteur::SaveSlots slots_;
    Demo3D* world_ = nullptr;
    double since_autosave_ = 0.0;  // seconds of play
    int next_auto_ = 0;
    std::string pending_thumbnail_;  // captured with the next frame
    moteur::Variable* save_binary_ = nullptr;  // "save.binary": CBOR (the shipped game's) instead of JSON
    moteur::Variable* save_auto_ = nullptr;    // "save.auto": seconds between automatic saves
    moteur::StateStack stack_;  // last: its states refer to the members above
};
