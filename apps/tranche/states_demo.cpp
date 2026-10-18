#include "states_demo.hpp"

#include "moteur/fixed_timestep.hpp"
#include "moteur/display.hpp"

#if !defined(MOTEUR_SHIPPING)
#include <imgui.h>
#endif

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>

#include "moteur/assets.hpp"
#include "moteur/file_io.hpp"
#include "moteur/gpu_resource.hpp"
#include "moteur/image.hpp"
#include "moteur/process_memory.hpp"

#include <filesystem>

namespace {

// Autopilot, in ticks (60 per second): time on the title, in the game before each pause, and in
// each pause.
constexpr int kTitleTicks = 2;
constexpr int kFirstPauseAt = 20;
constexpr int kSecondPauseAt = 30;
constexpr int kPauseTicks = 5;

double megabytes(std::size_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

std::unique_ptr<moteur::GameState> make_title(StatesDemo& demo);
std::unique_ptr<moteur::GameState> make_options(StatesDemo& demo);
std::unique_ptr<moteur::GameState> make_loading(StatesDemo& demo, std::shared_ptr<const moteur::SaveGame> saved = nullptr);

constexpr int kThumbnailWidth = 320;  // pixels across
const char* const kManualSlots[] = {"emplacement_1", "emplacement_2", "emplacement_3"};

std::filesystem::path utf8_path(const std::string& text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

// "03/10/2026 14:22" in local time.
std::string local_time(std::int64_t seconds) {
    SDL_DateTime t{};
    if (!SDL_TimeToDateTime(static_cast<SDL_Time>(seconds) * SDL_NS_PER_SECOND, &t, true)) {
        return "?";
    }
    char text[32];
    std::snprintf(text, sizeof(text), "%02d/%02d/%04d %02d:%02d", t.day, t.month, t.year, t.hour, t.minute);
    return text;
}

std::string slot_label(const std::string& name) {
    if (name.starts_with("emplacement_")) {
        return "Emplacement " + name.substr(12);
    }
    if (name.starts_with("auto_")) {
        return "Sauvegarde automatique " + name.substr(5);
    }
    return name;
}

// A column of buttons (SliceMenu: actions, or a click, read in the tick) with the menu's sounds.
class Menu {
public:
    explicit Menu(std::vector<std::string> items) : menu_(std::move(items)) {}
    void set_items(std::vector<std::string> items) { menu_.set_items(std::move(items)); }
    const std::string& item(int index) const { return menu_.item(index); }
    int focus() const { return menu_.focus(); }

    // The item chosen in this tick, or -1.
    int update(StatesDemo& demo) {
        bool moved = false;
        bool chosen = false;
        const int item = menu_.update(demo.app(), moved, chosen);
        if (moved) {
            demo.play_click();
        }
        if (chosen) {
            demo.play_confirm();
        }
        return item;
    }
    void draw(SliceUi& ui, float width_points = 0.0f) { menu_.draw(ui, width_points); }
    void set_rect(int index, const SliceUi::Rect& rect) { menu_.set_rect(index, rect); }
    void set_visible_rows(int rows) { menu_.set_visible_rows(rows); }

private:
    SliceMenu menu_;
};

// --- The display options (milestone 8, part 8): over the title or the pause.
class DisplayOptionsState final : public moteur::GameState {
public:
    explicit DisplayOptionsState(StatesDemo& demo) : demo_(demo), pending_(demo.app().display()) {}

    const char* name() const override { return "options_display"; }
    bool transparent() const override { return true; }
    bool blocking() const override { return true; }

    void update(double) override {
        moteur::Input& input = demo_.app().input();
        const auto pressed = [&input](const char* name) {
            const moteur::ActionId action = input.find(name);
            return action != moteur::kNoAction && input.pressed(action);
        };
        if (confirming_) {
            // A new mode or screen: kept only if the player can still answer (15 s).
            if (--confirm_ticks_ <= 0) {
                revert();
                return;
            }
            if (const int chosen = confirm_.update(demo_); chosen >= 0) {
                if (chosen == 0) {
                    confirming_ = false;  // kept
                } else {
                    revert();
                }
            }
            return;
        }
        if (pressed("pause")) {
            back();
            return;
        }
        const int row = menu_.focus();
        const int step = pressed("menu_right") ? 1 : pressed("menu_left") ? -1 : 0;
        if (step != 0 && row < kApply) {
            change(row, step);
            demo_.play_click();
        }
        if (const int chosen = menu_.update(demo_); chosen >= 0) {
            if (chosen < kApply) {
                change(chosen, 1);
            } else if (chosen == kApply) {
                apply();
            } else {
                back();
            }
        }
    }

    void render(moteur::Renderer& renderer, double) override {
        SliceUi& ui = demo_.ui();
        ui.dim(renderer, 0.6f, 2);
        ui.begin_panel(renderer, 440.0f, 2);
        if (confirming_) {
            ui.title("Garder ces réglages ?");
            ui.text("Retour aux réglages précédents dans " + std::to_string((confirm_ticks_ + moteur::tick_rate() - 1) / moteur::tick_rate()) + " s.",
                    SliceUi::Tone::Dim);
            ui.space();
            confirm_.draw(ui);
            ui.end_panel();
            return;
        }
        ui.title("Affichage");
        menu_.set_items(labels());
        menu_.draw(ui);
        ui.text("Flèches gauche et droite : changer ; Entrée : appliquer", SliceUi::Tone::Dim);
        ui.end_panel();
    }

private:
    static constexpr int kApply = 7;
    static constexpr int kFrameLimits[] = {0, 30, 60, 120, 144, 240};

    std::vector<std::string> labels() const {
        const std::vector<std::string> screens = moteur::displays();
        const std::string screen = pending_.display < static_cast<int>(screens.size())
                                       ? screens[static_cast<std::size_t>(pending_.display)]
                                       : "Écran " + std::to_string(pending_.display + 1);
        char scale[32];
        std::snprintf(scale, sizeof(scale), "%d %%", static_cast<int>(std::lround(pending_.render_scale * 100.0f)));
        return {std::string("Mode : ") + mode_label(pending_.mode),
                "Écran : " + screen,
                std::string("Synchronisation verticale : ") + (pending_.vsync ? "oui" : "non"),
                "Limite d'images : " + (pending_.frame_limit == 0 ? std::string("aucune") : std::to_string(pending_.frame_limit) + " par seconde"),
                std::string("Résolution de la 3D : ") + scale,
                std::string("Anticrénelage : ") + anti_aliasing_label(pending_.anti_aliasing),
                std::string("Ombres : ") + shadows_label(pending_.shadows),
                "Appliquer",
                "Retour"};
    }

    // The game's own words for the engine's settings (the engine shows no text to the player).
    static const char* mode_label(moteur::WindowMode mode) {
        return mode == moteur::WindowMode::Fullscreen ? "Plein écran" : "Fenêtré";
    }
    static const char* shadows_label(moteur::ShadowQuality quality) {
        switch (quality) {
            case moteur::ShadowQuality::Low:
                return "Basses";
            case moteur::ShadowQuality::Medium:
                return "Moyennes";
            default:
                return "Hautes";
        }
    }
    static const char* anti_aliasing_label(moteur::AntiAliasing mode) {
        switch (mode) {
            case moteur::AntiAliasing::None:
                return "Aucun";
            case moteur::AntiAliasing::Fxaa:
                return "FXAA";
            case moteur::AntiAliasing::Msaa2:
                return "MSAA 2x";
            default:
                return "MSAA 4x";
        }
    }

    void change(int row, int step) {
        const auto cycle = [step](int value, int count) { return ((value + step) % count + count) % count; };
        switch (row) {
            case 0:
                pending_.mode = static_cast<moteur::WindowMode>(cycle(static_cast<int>(pending_.mode), 2));
                break;
            case 1:
                pending_.display = cycle(pending_.display, std::max(1, static_cast<int>(moteur::displays().size())));
                break;
            case 2:
                pending_.vsync = !pending_.vsync;
                break;
            case 3: {
                int index = 0;
                for (int i = 0; i < 6; ++i) {
                    index = kFrameLimits[i] == pending_.frame_limit ? i : index;
                }
                pending_.frame_limit = kFrameLimits[cycle(index, 6)];
                break;
            }
            case 4: {
                const int percent = static_cast<int>(std::lround(pending_.render_scale * 100.0f));
                pending_.render_scale = static_cast<float>(50 + cycle((percent - 50) / 10, 6) * 10) / 100.0f;
                break;
            }
            case 5: {
                moteur::AntiAliasing mode = pending_.anti_aliasing;
                for (int tries = 0; tries < 4; ++tries) {  // the modes the GPU can do
                    mode = static_cast<moteur::AntiAliasing>(cycle(static_cast<int>(mode), 4));
                    if (demo_.app().renderer().supports(mode)) {
                        break;
                    }
                }
                pending_.anti_aliasing = mode;
                break;
            }
            case 6:
                pending_.shadows = static_cast<moteur::ShadowQuality>(cycle(static_cast<int>(pending_.shadows), 3));
                break;
            default:
                break;
        }
    }

    void apply() {
        previous_ = demo_.app().display();
        demo_.app().apply_display(pending_);
        if (pending_.changes_screen(previous_)) {
            confirming_ = true;
            confirm_ticks_ = static_cast<int>(moteur::ticks_from_seconds(15.0));
        }
    }

    void revert() {
        demo_.app().apply_display(previous_);
        pending_ = previous_;
        confirming_ = false;
    }

    void back() {
        if (!stack()->has_pending()) {
            stack()->pop();
        }
    }

    StatesDemo& demo_;
    moteur::DisplaySettings pending_;
    moteur::DisplaySettings previous_;
    bool confirming_ = false;
    int confirm_ticks_ = 0;
    Menu menu_{{"Retour"}};
    Menu confirm_{{"Garder", "Annuler"}};
};

// --- The audio options (milestone 8, part 12): the player's volumes, heard as they change.
class AudioOptionsState final : public moteur::GameState {
public:
    explicit AudioOptionsState(StatesDemo& demo) : demo_(demo) {}

    const char* name() const override { return "options_audio"; }
    bool transparent() const override { return true; }
    bool blocking() const override { return true; }

    void update(double) override {
        moteur::Input& input = demo_.app().input();
        const auto pressed = [&input](const char* name) {
            const moteur::ActionId action = input.find(name);
            return action != moteur::kNoAction && input.pressed(action);
        };
        if (pressed("pause")) {
            back();
            return;
        }
        const int row = menu_.focus();
        const int step = pressed("menu_right") ? 1 : pressed("menu_left") ? -1 : 0;
        if (step != 0 && row < kBack) {
            change(row, step, false);
        }
        if (const int chosen = menu_.update(demo_); chosen >= 0) {
            if (chosen < kBack) {
                change(chosen, 1, true);
            } else {
                back();
            }
        }
    }

    void render(moteur::Renderer& renderer, double) override {
        SliceUi& ui = demo_.ui();
        ui.dim(renderer, 0.6f, 2);
        ui.begin_panel(renderer, 440.0f, 2);
        ui.title("Audio");
        menu_.set_items(labels());
        menu_.draw(ui);
        ui.text("Flèches gauche et droite : changer", SliceUi::Tone::Dim);
        ui.end_panel();
    }

private:
    static constexpr int kBack = 5;

    // Row -1 is the master volume; the others, the groups in the order of the rows.
    static constexpr moteur::SoundGroup kGroups[] = {moteur::SoundGroup::Music, moteur::SoundGroup::Effects,
                                                     moteur::SoundGroup::Ambience, moteur::SoundGroup::Interface};

    float value(int row) const {
        const moteur::Audio& audio = demo_.app().audio();
        return row == 0 ? audio.master_volume() : audio.group_volume(kGroups[row - 1]);
    }

    std::vector<std::string> labels() const {
        const char* names[] = {"Volume général", "Musique", "Effets", "Ambiance", "Interface"};
        std::vector<std::string> items;
        for (int row = 0; row < kBack; ++row) {
            items.push_back(std::string(names[row]) + " : " + std::to_string(static_cast<int>(std::lround(value(row) * 100.0f))) + " %");
        }
        items.emplace_back("Retour");
        return items;
    }

    // Steps of 10 %; Enter goes round (100 % then 0 %), the arrows stop at the ends.
    void change(int row, int step, bool wrap) {
        int percent = static_cast<int>(std::lround(value(row) * 10.0f)) * 10 + step * 10;
        percent = wrap ? (percent > 100 ? 0 : percent) : std::clamp(percent, 0, 100);
        moteur::Audio& audio = demo_.app().audio();
        const float slider = static_cast<float>(percent) / 100.0f;
        if (row == 0) {
            audio.set_master_volume(slider);
        } else {
            audio.set_group_volume(kGroups[row - 1], slider);
        }
        demo_.play_click();  // an interface sound: heard at the new volume
    }

    void back() {
        if (!stack()->has_pending()) {
            demo_.app().save_audio_settings();
            stack()->pop();
        }
    }

    StatesDemo& demo_;
    Menu menu_{{"Retour"}};
};

// --- The controls (milestone 8, part 12): the layout (profile), and each action rebound, for the
// device the player confirms with (the keyboard and mouse, or the gamepad).
class ControlsOptionsState final : public moteur::GameState {
public:
    explicit ControlsOptionsState(StatesDemo& demo) : demo_(demo) { menu_.set_visible_rows(8); }

    const char* name() const override { return "options_controls"; }
    bool transparent() const override { return true; }
    bool blocking() const override { return true; }

    void exit() override { demo_.app().input().cancel_capture(); }

    void update(double) override {
        moteur::Input& input = demo_.app().input();
        if (capturing_) {
            if (!input.capturing()) {
                if (const std::optional<moteur::InputSource> source = input.take_captured()) {
                    captured(*source);
                }
            }
            return;
        }
        const auto pressed = [&input](const char* name) {
            const moteur::ActionId action = input.find(name);
            return action != moteur::kNoAction && input.pressed(action);
        };
        if (pressed("pause")) {
            back();
            return;
        }
        const int count = static_cast<int>(kEntries.size());
        const int row = menu_.focus();
        if (row == 0 && (pressed("menu_right") || pressed("menu_left"))) {
            next_profile(pressed("menu_left") ? -1 : 1);
        }
        if (const int chosen = menu_.update(demo_); chosen >= 0) {
            if (chosen == 0) {
                next_profile(1);
            } else if (chosen <= count) {
                start_capture(chosen - 1);
            } else if (chosen == count + 1) {
                input.reset_all_sources();
                message_ = "Touches par défaut rétablies.";
                save();
            } else {
                back();
            }
        }
    }

    void render(moteur::Renderer& renderer, double) override {
        SliceUi& ui = demo_.ui();
        ui.dim(renderer, 0.6f, 2);
        ui.begin_panel(renderer, 560.0f, 2);
        ui.title("Commandes");
        if (capturing_) {
            const bool pad = device_ == moteur::InputDevice::Gamepad;
            ui.text(std::string(kEntries[static_cast<std::size_t>(entry_)].label));
            ui.space();
            ui.text(pad ? "Appuyez sur un bouton de la manette." : "Appuyez sur une touche ou un bouton de la souris.");
            ui.text(pad ? "Start : annuler" : "Échap : annuler", SliceUi::Tone::Dim);
            ui.end_panel();
            return;
        }
        const bool pad = demo_.app().input().last_device() == moteur::InputDevice::Gamepad;
        ui.text(pad ? "Manette (le clavier : confirmer au clavier)" : "Clavier et souris (la manette : confirmer à la manette)",
                SliceUi::Tone::Dim);
        menu_.set_items(labels());
        menu_.draw(ui);
        ui.text(message_.empty() ? std::string("Entrée : changer la touche") : message_,
                message_.empty() ? SliceUi::Tone::Dim : SliceUi::Tone::Accent);
        ui.end_panel();
    }

private:
    // What the player can rebind: a button action, or one direction of an axis action.
    struct Entry {
        const char* label;
        const char* action;
        int direction;  // -1: a button; otherwise a moteur::AxisDirection
    };
    static constexpr int kButton = -1;
    static constexpr std::array<Entry, 21> kEntries{{
        {"Se déplacer (au clic)", "move_to", kButton},
        {"Avancer", "move", static_cast<int>(moteur::AxisDirection::Up)},
        {"Reculer", "move", static_cast<int>(moteur::AxisDirection::Down)},
        {"Aller à gauche", "move", static_cast<int>(moteur::AxisDirection::Left)},
        {"Aller à droite", "move", static_cast<int>(moteur::AxisDirection::Right)},
        {"Compétence 1", "skill_1", kButton},
        {"Compétence 2", "skill_2", kButton},
        {"Compétence 3", "skill_3", kButton},
        {"Compétence 4", "skill_4", kButton},
        {"Compétence 5", "skill_5", kButton},
        {"Compétence 6", "skill_6", kButton},
        {"Zoom avant", "zoom_in", kButton},
        {"Zoom arrière", "zoom_out", kButton},
        {"Choisir une créature", "select", kButton},
        {"Créature suivante", "next", kButton},
        {"Suivre la créature", "follow", kButton},
        {"Caméra : haut", "camera", static_cast<int>(moteur::AxisDirection::Up)},
        {"Caméra : bas", "camera", static_cast<int>(moteur::AxisDirection::Down)},
        {"Caméra : gauche", "camera", static_cast<int>(moteur::AxisDirection::Left)},
        {"Caméra : droite", "camera", static_cast<int>(moteur::AxisDirection::Right)},
        {"Pause", "pause", kButton},
    }};

    static bool on_gamepad(const moteur::InputSource& source) {
        return source.kind == moteur::InputSource::Kind::GamepadButton || source.kind == moteur::InputSource::Kind::GamepadAxis;
    }

    std::vector<moteur::InputSource> sources(const Entry& entry) const {
        const moteur::Input& input = demo_.app().input();
        const moteur::ActionId action = input.find(entry.action);
        if (action == moteur::kNoAction) {
            return {};
        }
        return entry.direction == kButton ? input.sources(action)
                                          : input.axis_sources(action, static_cast<moteur::AxisDirection>(entry.direction));
    }

    void set_sources(const Entry& entry, std::vector<moteur::InputSource> list) {
        moteur::Input& input = demo_.app().input();
        const moteur::ActionId action = input.find(entry.action);
        if (entry.direction == kButton) {
            input.set_sources(action, std::move(list));
        } else {
            input.set_axis_sources(action, static_cast<moteur::AxisDirection>(entry.direction), std::move(list));
        }
    }

    std::vector<std::string> labels() const {
        const moteur::Input& input = demo_.app().input();
        const bool pad = input.last_device() == moteur::InputDevice::Gamepad;
        std::vector<std::string> items;
        items.push_back("Disposition : " + input.profile_label(input.profile()));
        for (const Entry& entry : kEntries) {
            std::string keys;
            for (const moteur::InputSource& source : sources(entry)) {
                if (on_gamepad(source) == pad) {
                    keys += (keys.empty() ? "" : ", ") + moteur::input_source_label(source);
                }
            }
            items.push_back(std::string(entry.label) + " : " + (keys.empty() ? "(aucune)" : keys));
        }
        items.emplace_back("Rétablir les touches par défaut");
        items.emplace_back("Retour");
        return items;
    }

    void next_profile(int step) {
        moteur::Input& input = demo_.app().input();
        const std::vector<std::string> profiles = input.profiles();
        if (profiles.size() < 2) {
            return;
        }
        const auto at = std::find(profiles.begin(), profiles.end(), input.profile());
        const int index = at == profiles.end() ? 0 : static_cast<int>(at - profiles.begin());
        const int count = static_cast<int>(profiles.size());
        input.set_profile(profiles[static_cast<std::size_t>(((index + step) % count + count) % count)]);
        demo_.play_click();
        message_.clear();
        save();
    }

    void start_capture(int entry) {
        entry_ = entry;
        device_ = demo_.app().input().last_device();  // the device the player confirmed with
        capturing_ = true;
        message_.clear();
        demo_.app().input().begin_capture();
    }

    void captured(const moteur::InputSource& source) {
        moteur::Input& input = demo_.app().input();
        const bool pad = device_ == moteur::InputDevice::Gamepad;
        const bool cancel = pad ? source == moteur::InputSource{moteur::InputSource::Kind::GamepadButton, SDL_GAMEPAD_BUTTON_START, 0}
                                : source == moteur::InputSource{moteur::InputSource::Kind::Key, SDL_SCANCODE_ESCAPE, 0};
        if (cancel) {
            capturing_ = false;
            return;
        }
        if (on_gamepad(source) != pad) {
            input.begin_capture();  // the other device: still waiting for this one
            return;
        }
        capturing_ = false;
        const Entry& entry = kEntries[static_cast<std::size_t>(entry_)];
        // The entries that had this source lose it (one source, one action of the game).
        std::string taken;
        for (const Entry& other : kEntries) {
            if (&other == &entry) {
                continue;
            }
            std::vector<moteur::InputSource> list = sources(other);
            if (std::erase(list, source) > 0) {
                set_sources(other, std::move(list));
                taken += (taken.empty() ? "" : ", ") + std::string(other.label);
            }
        }
        // This device's sources are replaced; the other device's stay.
        std::vector<moteur::InputSource> list = sources(entry);
        std::erase_if(list, [pad](const moteur::InputSource& s) { return on_gamepad(s) == pad; });
        list.push_back(source);
        set_sources(entry, std::move(list));
        demo_.play_confirm();
        message_ = taken.empty() ? std::string() : moteur::input_source_label(source) + " retiré de : " + taken;
        save();
    }

    // The player's file, written at each change (demo3d_bindings.json, read by Demo3D::declare_actions).
    void save() {
        const std::string directory = demo_.app().preferences_directory();
        if (directory.empty()) {
            return;
        }
        std::string error;
        if (!moteur::write_file_atomic(directory + "demo3d_bindings.json", demo_.app().input().user_bindings_json(), error)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Controls: %s", error.c_str());
            message_ = "Impossible d'enregistrer les touches.";
        }
    }

    void back() {
        if (!stack()->has_pending()) {
            stack()->pop();
        }
    }

    StatesDemo& demo_;
    Menu menu_{{"Retour"}};
    bool capturing_ = false;
    int entry_ = 0;
    moteur::InputDevice device_ = moteur::InputDevice::KeyboardMouse;
    std::string message_;
};

// --- The options: a menu of their pages, over the title or the pause.
class OptionsState final : public moteur::GameState {
public:
    explicit OptionsState(StatesDemo& demo) : demo_(demo) {}

    const char* name() const override { return "options"; }
    bool transparent() const override { return true; }
    bool blocking() const override { return true; }

    void update(double) override {
        moteur::Input& input = demo_.app().input();
        const moteur::ActionId pause = input.find("pause");
        if (pause != moteur::kNoAction && input.pressed(pause)) {
            back();
            return;
        }
        if (const int chosen = menu_.update(demo_); chosen >= 0 && !stack()->has_pending()) {
            if (chosen == 0) {
                stack()->push(std::make_unique<DisplayOptionsState>(demo_));
            } else if (chosen == 1) {
                stack()->push(std::make_unique<AudioOptionsState>(demo_));
            } else if (chosen == 2) {
                stack()->push(std::make_unique<ControlsOptionsState>(demo_));
            } else {
                back();
            }
        }
    }

    void render(moteur::Renderer& renderer, double) override {
        SliceUi& ui = demo_.ui();
        ui.dim(renderer, 0.5f, 1);
        ui.begin_panel(renderer, 320.0f, 1);
        ui.title("Options");
        menu_.draw(ui);
        ui.end_panel();
    }

private:
    void back() {
        if (!stack()->has_pending()) {
            stack()->pop();
        }
    }

    StatesDemo& demo_;
    Menu menu_{{"Affichage", "Audio", "Commandes", "Retour"}};
};

std::unique_ptr<moteur::GameState> make_options(StatesDemo& demo) {
    return std::make_unique<OptionsState>(demo);
}

// --- The game: the 3D demo, built by the loading screen's last step.
class GameplayState final : public moteur::GameState {
public:
    GameplayState(StatesDemo& demo, std::shared_ptr<const moteur::SaveGame> saved)
        : demo_(demo), world_(std::make_unique<Demo3D>(demo.app(), with_save(demo.options().demo, std::move(saved)), false)) {
        demo_.set_world(world_.get());
    }
    ~GameplayState() override {
        if (demo_.world() == world_.get()) {
            demo_.set_world(nullptr);
        }
    }

    const char* name() const override { return "game"; }
    Demo3D& world() { return *world_; }

    void update(double dt) override;
    void render(moteur::Renderer& renderer, double alpha) override { world_->render(renderer, alpha); }

    // The pause must freeze the world: the demo counts its ticks, which must not move under it.
    void covered() override {
        covered_ = true;
        ticks_when_covered_ = world_->ticks();
    }
    void uncovered() override { check_frozen(); }
    void exit() override { check_frozen(); }  // left from the pause ("Retour au titre")
    void enter() override { demo_.app().audio().play_music(demo_.game_music()); }

private:
    static Demo3D::Options with_save(Demo3D::Options options, std::shared_ptr<const moteur::SaveGame> saved) {
        options.saved_game = std::move(saved);
        return options;
    }
    void check_frozen() {
        if (covered_) {
            demo_.check_frozen(world_->ticks() == ticks_when_covered_);
            covered_ = false;
        }
    }

    StatesDemo& demo_;
    std::unique_ptr<Demo3D> world_;
    long ticks_ = 0;  // ticks this state was updated (not paused)
    bool covered_ = false;
    long ticks_when_covered_ = 0;
};

// --- The pause: over the game, which stays visible and frozen.
class PauseState final : public moteur::GameState {
public:
    // leave: the autopilot goes back to the title from this pause (otherwise it resumes).
    PauseState(StatesDemo& demo, bool leave) : demo_(demo), leave_(leave) {}

    const char* name() const override { return "pause"; }
    bool transparent() const override { return true; }
    bool blocking() const override { return true; }

    // The rule chosen for the sound: the music goes on, lower; the world's sounds wait.
    void enter() override { set_paused(true); }
    void exit() override { set_paused(false); }

    void update(double) override {
        ++ticks_;
        moteur::Input& input = demo_.app().input();
        const moteur::ActionId pause = input.find("pause");
        if (pause != moteur::kNoAction && input.pressed(pause)) {
            resume();
        } else if (const int chosen = menu_.update(demo_); chosen >= 0) {
            choose(chosen);
        } else if (demo_.options().cycles > 0 && ticks_ == kPauseTicks) {
            if (leave_) {
                back_to_title();
            } else {
                resume();
            }
        }
    }

    void render(moteur::Renderer& renderer, double) override {
        if (!thumbnail_taken_) {
            // The first frame of the pause: the game as it is, without the menu, for the saves.
            renderer.request_capture(demo_.pause_thumbnail(), kThumbnailWidth);
            thumbnail_taken_ = true;
            return;
        }
        if (demo_.capture_pending() && ticks_ >= 2) {
            renderer.request_capture(demo_.options().capture_path);
            demo_.capture_done();
        }
        // The world, darkened, under the menu.
        SliceUi& ui = demo_.ui();
        ui.dim(renderer, 0.47f);
        ui.begin_panel(renderer, 300.0f);
        ui.title("Pause");
        ui.text("Le monde est figé dessous.", SliceUi::Tone::Dim);
        ui.space();
        menu_.draw(ui);
        ui.text("Échap ou Start : reprendre", SliceUi::Tone::Dim);
        ui.end_panel();
    }

private:
    void set_paused(bool paused) {
        moteur::Audio& audio = demo_.app().audio();
        audio.set_music_volume(paused ? 0.35f : 1.0f, 0.3f);
        audio.set_group_paused(moteur::SoundGroup::Effects, paused);
        audio.set_group_paused(moteur::SoundGroup::Ambience, paused);
    }
    void choose(int item) {
        const std::string& chosen = menu_.item(item);
        if (chosen == "Reprendre") {
            resume();
        } else if (chosen == "Sauvegarder") {
            open_slots(true);
        } else if (chosen == "Options") {
            if (!stack()->has_pending()) {
                stack()->push(make_options(demo_));
            }
        } else {
            back_to_title();
        }
    }
    void open_slots(bool saving);
    void resume() {
        if (!stack()->has_pending()) {
            stack()->pop();
        }
    }
    void back_to_title() {
        if (!stack()->has_pending()) {
            stack()->reset(make_title(demo_));
        }
    }

    StatesDemo& demo_;
    bool leave_;
    int ticks_ = 0;
    bool thumbnail_taken_ = false;
    Menu menu_{{"Reprendre", "Sauvegarder", "Options", "Retour au titre"}};
};

// --- The slots: to save into (over the pause) or to load from (over the title).
class SlotsState final : public moteur::GameState {
public:
    SlotsState(StatesDemo& demo, bool saving) : demo_(demo), saving_(saving) {}

    const char* name() const override { return saving_ ? "sauvegarder" : "charger"; }
    bool transparent() const override { return saving_; }  // the frozen game under it
    bool blocking() const override { return true; }

    void enter() override { refresh(); }

    void update(double) override {
        moteur::Input& input = demo_.app().input();
        const moteur::ActionId pause = input.find("pause");
        if (pause != moteur::kNoAction && input.pressed(pause)) {
            back();
        } else if (const int chosen = menu_.update(demo_); chosen >= 0) {
            choose(chosen);
        }
    }

    void render(moteur::Renderer& renderer, double) override {
        if (!saving_) {
            renderer.set_clear_color(0.06f, 0.05f, 0.09f);
        }
        SliceUi& ui = demo_.ui();
        if (saving_) {
            ui.dim(renderer, 0.6f, 1);  // the pause's panel, darkened under this one
        }
        ui.begin_panel(renderer, 600.0f, 1);  // over the pause's panel
        ui.title(saving_ ? "Sauvegarder la partie" : "Charger une partie");
        ui.space();
        for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
            const Row& row = rows_[static_cast<std::size_t>(i)];
            std::vector<SliceUi::Line> lines = {{slot_label(row.name), SliceUi::Tone::Normal}};
            if (!row.exists) {
                lines.push_back({"(vide)", SliceUi::Tone::Dim});
            } else if (!row.slot.readable) {
                lines.push_back({"Illisible : " + row.slot.error, SliceUi::Tone::Error});
            } else {
                const nlohmann::json& summary = row.slot.header.summary;
                char text[160];
                std::snprintf(text, sizeof(text), "%s, %.0f min de jeu%s", local_time(row.slot.header.time).c_str(),
                              row.slot.header.play_seconds / 60.0, row.slot.from_backup ? " (copie de secours)" : "");
                lines.push_back({text, SliceUi::Tone::Dim});
                std::snprintf(text, sizeof(text), "Vie %d %%, %d créatures debout",
                              static_cast<int>(summary.value("vie", 0.0) * 100.0), summary.value("creatures", 0));
                lines.push_back({text, SliceUi::Tone::Dim});
            }
            menu_.set_rect(i, ui.card(row.thumbnail.gpu ? &row.thumbnail : nullptr, {160.0f, 90.0f}, lines,
                                      i == menu_.focus()));
        }
        const int back = static_cast<int>(rows_.size());
        menu_.set_rect(back, ui.button("Retour", menu_.focus() == back, 160.0f));
        if (!status_.empty()) {
            ui.space();
            ui.text(status_);
        }
        ui.text("Flèches et Entrée ; Échap : retour", SliceUi::Tone::Dim);
        ui.end_panel();
    }

private:
    struct Row {
        std::string name;
        bool exists = false;
        moteur::SaveSlots::Slot slot;
        moteur::Texture thumbnail;
    };

    void refresh() {
        rows_.clear();
        const std::vector<moteur::SaveSlots::Slot> slots = demo_.slots().list();
        const auto make_row = [&](const std::string& name) {
            Row row;
            row.name = name;
            for (const moteur::SaveSlots::Slot& slot : slots) {
                if (slot.name == name) {
                    row.exists = true;
                    row.slot = slot;
                }
            }
            // Its thumbnail, if any (a bad file leaves the place empty).
            if (row.exists && !row.slot.thumbnail.empty()) {
                if (const auto bytes = moteur::read_file_bytes(row.slot.thumbnail)) {
                    try {
                        const moteur::Image image = moteur::decode_image(bytes->data(), bytes->size(), row.slot.thumbnail);
                        row.thumbnail = demo_.app().renderer().create_texture(image, "save thumbnail");
                    } catch (const std::exception& e) {
                        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Save: thumbnail %s: %s", row.slot.thumbnail.c_str(), e.what());
                    }
                }
            }
            rows_.push_back(std::move(row));
        };
        if (saving_) {
            for (const char* name : kManualSlots) {
                make_row(name);
            }
        } else {
            for (const moteur::SaveSlots::Slot& slot : slots) {  // every slot, the most recent first
                make_row(slot.name);
            }
        }
        std::vector<std::string> items;
        for (const Row& row : rows_) {
            items.push_back(row.name);
        }
        items.push_back("Retour");
        menu_.set_items(items);
    }

    void choose(int index) {
        if (index >= static_cast<int>(rows_.size())) {
            back();
            return;
        }
        const Row& row = rows_[static_cast<std::size_t>(index)];
        if (saving_) {
            std::string error;
            if (demo_.save_game(row.name, demo_.pause_thumbnail(), error)) {
                status_ = "Sauvegardé dans " + slot_label(row.name) + ".";
            } else {
                status_ = "Échec : " + error;
            }
            refresh();
        } else if (row.exists) {
            demo_.load_game(row.name);
        }
    }

    void back() {
        if (!stack()->has_pending()) {
            stack()->pop();
        }
    }

    StatesDemo& demo_;
    bool saving_;
    std::vector<Row> rows_;
    std::string status_;
    Menu menu_{{"Retour"}};
};

void PauseState::open_slots(bool saving) {
    if (!stack()->has_pending()) {
        stack()->push(std::make_unique<SlotsState>(demo_, saving));
    }
}

void GameplayState::update(double dt) {
    ++ticks_;
    if (world_->pause_pressed()) {
        stack()->push(std::make_unique<PauseState>(demo_, false));
    } else if (demo_.options().cycles > 0 && (ticks_ == kFirstPauseAt || ticks_ == kSecondPauseAt)) {
        stack()->push(std::make_unique<PauseState>(demo_, ticks_ == kSecondPauseAt));
    }
    world_->update(dt);
    demo_.played(dt);
}

// --- Loading: the demo's files first (kept until the world is built, which then finds them in the
// cache), then the world.
class DemoLoadingState final : public moteur::LoadingState {
public:
    DemoLoadingState(StatesDemo& demo, std::vector<Step> steps, MakeState make_next)
        : LoadingState(std::move(steps), "Construction du monde", std::move(make_next)), demo_(demo) {}

    void enter() override { demo_.loading_started(); }

protected:
    void draw(moteur::Renderer& renderer, float progress, const std::string& label) override {
        renderer.set_clear_color(0.02f, 0.02f, 0.03f);
        SliceUi& ui = demo_.ui();
        ui.begin_panel(renderer, 360.0f);
        ui.title("Chargement");
        ui.progress(progress);
        ui.text(label, SliceUi::Tone::Dim);
        ui.end_panel();
    }

    void failed(const std::string& error) override {
        SDL_Log("States: loading failed: %s", error.c_str());
        demo_.set_error(error);
        stack()->reset(make_title(demo_));
    }

private:
    StatesDemo& demo_;
};

std::unique_ptr<moteur::GameState> make_loading(StatesDemo& demo, std::shared_ptr<const moteur::SaveGame> saved) {
    struct Held {
        moteur::Asset<moteur::Font> font;
        moteur::Asset<moteur::Model> barrel;
        std::vector<moteur::Asset<moteur::Model>> decor;  // the ARPG's cobblestones and street lamp
        std::vector<moteur::Asset<moteur::Prefab>> prefabs;  // the ARPG's street lamp
        std::vector<moteur::Asset<moteur::Model>> characters;
        std::vector<moteur::Asset<moteur::Skeleton>> skeletons;
        std::vector<moteur::Asset<moteur::ClipLibrary>> clips;
        moteur::Asset<moteur::AnimationSet> animation_set;
        std::vector<moteur::Asset<moteur::Sound>> sounds;
        moteur::Asset<moteur::Music> ambience;
    };
    auto held = std::make_shared<Held>();
    moteur::Assets& assets = demo.app().assets();
    std::vector<moteur::LoadingState::Step> steps = {
        // Milestone 8, part 5: every file read and decoded on the job system's threads while the
        // screen keeps moving; the steps below then only create the GPU objects.
        {"Lecture des fichiers",
         [&assets, held] {
             held->font = assets.font(Demo3D::kFont, Demo3D::kFontPixelHeight);
             if (assets.exists(Demo3D::kBarrel)) {
                 assets.prefetch_model(Demo3D::kBarrel);
             }
             for (const char* path : {Demo3D::kPavingA, Demo3D::kPavingB}) {
                 if (assets.exists(path)) {
                     assets.prefetch_model(path);
                 }
             }
             if (assets.exists(Demo3D::kStreetLamp)) {
                 assets.prefetch_prefab(Demo3D::kStreetLamp);
             }
             if (Demo3D::characters_available(assets)) {
                 for (const char* path : {Demo3D::kKnight, Demo3D::kSkeletons[0], Demo3D::kSkeletons[1]}) {
                     assets.prefetch_model(path);
                     assets.prefetch_skeleton(path);
                     assets.prefetch_clips(path);
                 }
                 assets.prefetch_model(Demo3D::kSword);
             }
             if (assets.exists(Demo3D::kAmbience)) {
                 for (const char* path : Demo3D::kSteps) {
                     assets.prefetch_sound(path);
                 }
                 for (const char* path : Demo3D::kImpacts) {
                     assets.prefetch_sound(path);
                 }
             }
         },
         [&assets] { return assets.prefetching() == 0; }},
        {"Modèle du tonneau",
         [&assets, held] {
             if (assets.exists(Demo3D::kBarrel)) {
                 held->barrel = assets.model(Demo3D::kBarrel);
             }
         }},
        {"Pavés et lampadaires",
         [&assets, held] {
             for (const char* path : {Demo3D::kPavingA, Demo3D::kPavingB}) {
                 if (assets.exists(path)) {
                     held->decor.push_back(assets.model(path));
                 }
             }
             if (assets.exists(Demo3D::kStreetLamp)) {
                 held->prefabs.push_back(assets.prefab(Demo3D::kStreetLamp));
             }
         }},
        {"Personnages animés",
         [&assets, held] {
             if (!Demo3D::characters_available(assets)) {  // tools/models/fetch_test_characters.py
                 return;
             }
             for (const char* path : {Demo3D::kKnight, Demo3D::kSkeletons[0], Demo3D::kSkeletons[1]}) {
                 held->characters.push_back(assets.model(path));
                 held->skeletons.push_back(assets.skeleton(path));
                 held->clips.push_back(assets.clips(path));
             }
             held->characters.push_back(assets.model(Demo3D::kSword));
             held->animation_set = assets.animation_set(Demo3D::kAnimationSet);
         }},
        {"Sons",
         [&assets, held] {
             if (assets.exists(Demo3D::kAmbience)) {  // tools/audio/fetch_test_sounds.py
                 for (const char* path : Demo3D::kSteps) {
                     held->sounds.push_back(assets.sound(path));
                 }
                 for (const char* path : Demo3D::kImpacts) {
                     held->sounds.push_back(assets.sound(path));
                 }
                 held->ambience = assets.music(Demo3D::kAmbience);
             }
         }},
    };
    // `held` lives in the steps until the loading state goes, after the world was built.
    return std::make_unique<DemoLoadingState>(demo, std::move(steps), [&demo, saved] {
        auto game = std::make_unique<GameplayState>(demo, saved);
        demo.loading_done();
        return game;
    });
}

// --- The title screen.
class TitleState final : public moteur::GameState {
public:
    explicit TitleState(StatesDemo& demo) : demo_(demo) {}

    const char* name() const override { return "title"; }
    void enter() override {
        demo_.app().audio().play_music(demo_.title_music());
        std::vector<std::string> items = {"Jouer"};
        if (!demo_.latest_slot().empty()) {
            items.push_back("Continuer");
        }
        items.push_back("Charger");
        items.push_back("Options");
        items.push_back("Quitter");
        menu_.set_items(items);
    }
    void uncovered() override { enter(); }  // back from "Charger"

    void update(double) override {
        if (ticks_++ == 0) {
            demo_.measure();  // the previous game is gone and its assets collected by now
        }
        if (demo_.options().cycles > 0) {
            if (demo_.cycles_done() >= demo_.options().cycles) {
                if (!finished_) {
                    finished_ = true;
                    demo_.finish();
                }
            } else if (ticks_ >= kTitleTicks) {
                play();
            }
        } else if (const int chosen = menu_.update(demo_); chosen >= 0) {
            choose(chosen);
        }
    }

    void render(moteur::Renderer& renderer, double) override {
        renderer.set_clear_color(0.06f, 0.05f, 0.09f);
        SliceUi& ui = demo_.ui();
        ui.begin_panel(renderer, 440.0f);
        ui.title("Tranche jouable");
        ui.text("Titre, chargement, la carte avec un héros, pause.", SliceUi::Tone::Dim);
        ui.text("Flèches et Entrée, croix et A de la manette, ou la souris.", SliceUi::Tone::Dim);
        if (!demo_.error().empty()) {
            ui.text("Échec : " + demo_.error(), SliceUi::Tone::Error);
        }
        ui.space();
        menu_.draw(ui);
        ui.end_panel();
    }

private:
    void choose(int item) {
        const std::string& chosen = menu_.item(item);
        if (chosen == "Jouer") {
            play();
        } else if (chosen == "Continuer") {
            demo_.load_game(demo_.latest_slot());
        } else if (chosen == "Charger") {
            if (!stack()->has_pending()) {
                stack()->push(std::make_unique<SlotsState>(demo_, false));
            }
        } else if (chosen == "Options") {
            if (!stack()->has_pending()) {
                stack()->push(make_options(demo_));
            }
        } else {
            demo_.finish();
        }
    }
    void play() {
        if (!stack()->has_pending()) {
            demo_.set_error({});
            stack()->replace(make_loading(demo_));
        }
    }

    StatesDemo& demo_;
    int ticks_ = 0;
    bool finished_ = false;
    Menu menu_{{"Jouer", "Charger", "Quitter"}};
};

std::unique_ptr<moteur::GameState> make_title(StatesDemo& demo) {
    return std::make_unique<TitleState>(demo);
}

}  // namespace

StatesDemo::StatesDemo(moteur::Application& app, const Options& options, bool standalone)
    : app_(app), options_(options), standalone_(standalone), slots_(app.preferences_directory() + "sauvegardes/"), stack_(app) {
    moteur::Variables& variables = app.console().variables();
    // Declared again by every run of the scene: the same variables (and the player's values).
    save_binary_ = &variables.add_bool("save.binary", false, "sauvegardes en CBOR (celles du jeu livré) plutôt qu'en JSON",
                                       moteur::Variable::Archive);
    save_auto_ = &variables.add_int("save.auto", 120, 0, 3600, "secondes de jeu entre deux sauvegardes automatiques (0 : jamais)",
                                    moteur::Variable::Archive);
    app.console().add("save", "[emplacement]", "sauvegarde la partie en cours (emplacement_1 par défaut)", moteur::Console::Kind::Tool,
                      [this](const moteur::Console::Args& args, moteur::Console& out) {
                          std::string error;
                          const std::string slot = args.empty() ? "emplacement_1" : args[0];
                          if (save_game(slot, {}, error)) {
                              out.print("sauvegardé dans " + slots_.path(slot));
                          } else {
                              out.error(error);
                          }
                      });
    app.console().add("load", "[emplacement]", "charge une sauvegarde (la plus récente par défaut)", moteur::Console::Kind::Tool,
                      [this](const moteur::Console::Args& args, moteur::Console& out) {
                          const std::string slot = args.empty() ? latest_slot() : args[0];
                          if (slot.empty()) {
                              out.error("aucune sauvegarde lisible");
                              return;
                          }
                          out.print("chargement de " + slot);
                          load_game(slot);
                      },
                      [this](std::size_t index, const std::string& prefix) {
                          std::vector<std::string> names;
                          for (const moteur::SaveSlots::Slot& slot : slots_.list()) {
                              if (index == 0 && slot.name.starts_with(prefix)) {
                                  names.push_back(slot.name);
                              }
                          }
                          return names;
                      });
    options_.demo.hero = true;  // the game of the slice
    Demo3D::declare_actions(app);  // the menus' actions, before the demo exists
    ui_ = std::make_unique<SliceUi>(app);
    if (app.assets().exists("audio/music/the_field_of_dreams.mp3")) {  // tools/audio/fetch_test_sounds.py
        title_music_ = app.assets().music("audio/music/the_field_of_dreams.mp3");
        game_music_ = app.assets().music("audio/music/town_theme.mp3");
        click_ = app.assets().sound("audio/kenney_interface/click_001.ogg");
        confirm_ = app.assets().sound("audio/kenney_interface/confirmation_001.ogg");
    }
    stack_.push(make_title(*this));
    stack_.apply_pending();
#if !defined(MOTEUR_SHIPPING)
    if (moteur::DebugTools* tools = app.debug_tools()) {
        tools->watch(stack_, "Test des états");
    }
#endif
}

StatesDemo::~StatesDemo() {
    app_.console().remove("save");
    app_.console().remove("load");
#if !defined(MOTEUR_SHIPPING)
    if (moteur::DebugTools* tools = app_.debug_tools()) {
        tools->forget(stack_);
    }
#endif
    app_.audio().stop_music(0.5f);
}

void StatesDemo::on_event(const SDL_Event& event) {
    stack_.on_event(event);
}

void StatesDemo::update(double dt) {
    elapsed_ += dt;
    if (options_.run_seconds > 0.0 && elapsed_ >= options_.run_seconds) {
        finish();
    }
    stack_.update(dt);
}

void StatesDemo::render(moteur::Renderer& renderer, double alpha) {
    if (!pending_thumbnail_.empty()) {
        renderer.request_capture(pending_thumbnail_, kThumbnailWidth);  // this frame, as it is
        pending_thumbnail_.clear();
    }
    stack_.render(renderer, alpha);
}

bool StatesDemo::save_game(const std::string& slot, const std::string& thumbnail, std::string& error) {
    if (world_ == nullptr) {
        error = "aucune partie en cours";
        return false;
    }
    if (slot.empty() || slot.find_first_of("/\\:.") != std::string::npos) {
        error = "nom d'emplacement invalide : " + slot;
        return false;
    }
    const Uint64 start = SDL_GetPerformanceCounter();
    moteur::SaveGame game = world_->save();
    SDL_Time now = 0;
    SDL_GetCurrentTime(&now);
    game.header.game_version = "bac_a_sable (jalon 7)";
    game.header.time = static_cast<std::int64_t>(now / SDL_NS_PER_SECOND);
    game.header.play_seconds = world_->elapsed();
    game.header.summary = {{"lieu", "tranche"}, {"vie", world_->hero_health()}, {"creatures", world_->creatures_standing()},
                           {"tick", world_->ticks()}};
    const moteur::SaveEncoding encoding = save_binary_->as_bool() ? moteur::SaveEncoding::Cbor : moteur::SaveEncoding::Json;
    if (!slots_.write(slot, game, encoding, error)) {
        return false;
    }
    std::error_code copy_error;
    if (!thumbnail.empty() && std::filesystem::exists(utf8_path(thumbnail), copy_error)) {
        std::filesystem::copy_file(utf8_path(thumbnail), utf8_path(slots_.thumbnail_path(slot)),
                                   std::filesystem::copy_options::overwrite_existing, copy_error);
    } else {
        pending_thumbnail_ = slots_.thumbnail_path(slot);
    }
    SDL_Log("Save: '%s' written in %.1f ms (%s)", slot.c_str(),
            static_cast<double>(SDL_GetPerformanceCounter() - start) * 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency()),
            encoding == moteur::SaveEncoding::Cbor ? "CBOR" : "JSON");
    return true;
}

void StatesDemo::load_game(const std::string& slot) {
    std::string error;
    bool from_backup = false;
    std::optional<moteur::SaveGame> game = slots_.read(slot, error, nullptr, &from_backup);
    if (!game) {
        set_error("sauvegarde « " + slot_label(slot) + " » illisible : " + error);
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Save: %s", error_.c_str());
        stack_.reset(make_title(*this));
        return;
    }
    if (from_backup) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Save: '%s' was damaged: its backup is loaded", slot.c_str());
    }
    set_error({});
    since_autosave_ = 0.0;
    stack_.reset(make_loading(*this, std::make_shared<const moteur::SaveGame>(std::move(*game))));
}

void StatesDemo::played(double dt) {
    const int every = save_auto_->as_int();
    if (every <= 0 || world_ == nullptr) {
        return;
    }
    since_autosave_ += dt;
    if (since_autosave_ < static_cast<double>(every)) {
        return;
    }
    since_autosave_ = 0.0;
    const std::string slot = next_auto_ == 0 ? "auto_1" : "auto_2";  // in turn: a damaged write keeps the other
    next_auto_ = 1 - next_auto_;
    std::string error;
    if (!save_game(slot, {}, error)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Save: automatic save failed: %s", error.c_str());
    }
}

std::string StatesDemo::latest_slot() const {
    for (const moteur::SaveSlots::Slot& slot : slots_.list()) {  // the most recent first
        if (slot.readable) {
            return slot.name;
        }
    }
    return {};
}

bool StatesDemo::uses_escape() const {
    const moteur::GameState* top = stack_.top();
    return top != nullptr && (std::strcmp(top->name(), "game") == 0 || std::strcmp(top->name(), "pause") == 0 ||
                              std::strcmp(top->name(), "sauvegarder") == 0 || std::strcmp(top->name(), "charger") == 0);
}

void StatesDemo::measure() {
    Measure measure;
    for (const moteur::AssetTypeStats& type : app_.assets().stats()) {
        measure.assets += type.count;
        measure.asset_bytes += type.bytes;
    }
    measure.gpu_objects = moteur::gpu_resource_counts().total();
    measure.process_bytes = moteur::process_memory_bytes();
    measures_.push_back(measure);
    // The measurement report (--report-json, milestone 8, part 4): the first cycle loads what stays
    // for good, so the counts after it are the reference of a leak.
    app_.report_value("cycles", cycles_done());
    if (measures_.size() == 2) {
        app_.report_value("gpu_objects_after_cycle_1", static_cast<double>(measure.gpu_objects));
        app_.report_value("memory_mb_after_cycle_1", megabytes(measure.process_bytes));
        app_.report_value("assets_after_cycle_1", static_cast<double>(measure.assets));
    }
    app_.report_value("gpu_objects_last_cycle", static_cast<double>(measure.gpu_objects));
    app_.report_value("memory_mb_last_cycle", megabytes(measure.process_bytes));
    app_.report_value("assets_last_cycle", static_cast<double>(measure.assets));
    SDL_Log("States: cycle %d: %zu assets (%.1f MB), %ld GPU objects, process %.1f MB, heap %.1f MB", cycles_done(),
            measure.assets, megabytes(measure.asset_bytes), measure.gpu_objects, megabytes(measure.process_bytes),
            megabytes(moteur::process_heap_bytes()));
    app_.report_value("heap_mb_last_cycle", megabytes(moteur::process_heap_bytes()));
}

void StatesDemo::loading_started() {
    loading_start_ = SDL_GetPerformanceCounter();
}

void StatesDemo::loading_done() {
    last_load_ms_ = 1000.0 * static_cast<double>(SDL_GetPerformanceCounter() - loading_start_) /
                    static_cast<double>(SDL_GetPerformanceFrequency());
    std::size_t asset_bytes = 0;
    for (const moteur::AssetTypeStats& type : app_.assets().stats()) {
        asset_bytes += type.bytes;
    }
    ++loads_;
    load_max_ms_ = std::max(load_max_ms_, last_load_ms_);
    if (loads_ == 1) {
        app_.report_value("slice_load_ms_first", last_load_ms_);  // nothing in memory yet
    }
    app_.report_value("slice_load_ms_last", last_load_ms_);
    app_.report_value("slice_load_ms_max", load_max_ms_);
    SDL_Log("Slice: loaded in %.0f ms; assets %.1f MB, %ld GPU objects, process %.1f MB", last_load_ms_,
            megabytes(asset_bytes), moteur::gpu_resource_counts().total(), megabytes(moteur::process_memory_bytes()));
}

void StatesDemo::play_click() {
    if (click_) {
        moteur::PlaySound options;
        options.group = moteur::SoundGroup::Interface;
        options.volume = 0.5f;
        app_.audio().play(click_, options);
    }
}

void StatesDemo::play_confirm() {
    if (confirm_) {
        moteur::PlaySound options;
        options.group = moteur::SoundGroup::Interface;
        options.volume = 0.6f;
        app_.audio().play(confirm_, options);
    }
}

void StatesDemo::finish() {
    if (standalone_) {
        app_.quit();
    } else {
        stop_requested_ = true;
    }
}

void StatesDemo::report() const {
    std::cout << "states: " << cycles_done() << " cycles, " << frozen_failures_ << " pauses where the world moved\n";
    if (measures_.size() < 2) {
        return;
    }
    // The first cycle loads what stays for good (pipelines, shared assets): compare from the end of it.
    const Measure& first = measures_[1];
    const Measure& last = measures_.back();
    std::cout << "states: after cycle 1: " << first.assets << " assets, " << first.gpu_objects << " GPU objects, "
              << megabytes(first.process_bytes) << " MB\n";
    std::cout << "states: after cycle " << cycles_done() << ": " << last.assets << " assets, " << last.gpu_objects
              << " GPU objects, " << megabytes(last.process_bytes) << " MB\n";
    const bool stable = first.assets == last.assets && first.asset_bytes == last.asset_bytes &&
                        first.gpu_objects == last.gpu_objects;
    std::cout << "states: assets and GPU objects " << (stable ? "stable" : "GROWING") << '\n';
    // The process's memory wanders by a few MB from one measure to the next (allocator, driver):
    // a leak shows as a floor that rises. The lowest of the first ten cycles against the last ten.
    const std::size_t count = measures_.size() - 1;
    const std::size_t window = std::min<std::size_t>(10, count);
    std::size_t low_start = SIZE_MAX, low_end = SIZE_MAX, high = 0;
    for (std::size_t i = 1; i <= count; ++i) {
        const std::size_t bytes = measures_[i].process_bytes;
        if (i <= window) {
            low_start = std::min(low_start, bytes);
        }
        if (i > count - window) {
            low_end = std::min(low_end, bytes);
        }
        high = std::max(high, bytes);
    }
    const double drift = megabytes(low_end) - megabytes(low_start);
    char line[200];
    std::snprintf(line, sizeof(line),
                  "states: process memory %.1f to %.1f MB; lowest of the first %zu cycles %.1f MB, of the last %zu %.1f MB (%+.1f MB)",
                  megabytes(std::min(low_start, low_end)), megabytes(high), window, megabytes(low_start), window,
                  megabytes(low_end), drift);
    std::cout << line << '\n';
}

#if !defined(MOTEUR_SHIPPING)
void StatesDemo::draw_controls() {
    std::string states;
    for (std::size_t i = 0; i < stack_.size(); ++i) {
        states += (i > 0 ? " > " : "") + std::string(stack_.at(i)->name());
    }
    ImGui::Text("Pile d'états : %s", states.c_str());
    ImGui::Text("Cycles : %d%s", cycles_done(), options_.cycles > 0 ? " (pilote automatique)" : "");
    if (!measures_.empty()) {
        const Measure& last = measures_.back();
        ImGui::Text("Au dernier titre : %zu assets (%.1f Mo GPU), %ld objets GPU, processus %.1f Mo", last.assets,
                    megabytes(last.asset_bytes), last.gpu_objects, megabytes(last.process_bytes));
    }
    if (last_load_ms_ > 0.0) {
        ImGui::Text("Dernier chargement : %.0f ms", last_load_ms_);
    }
    if (frozen_failures_ > 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%d pauses où le monde a bougé", frozen_failures_);
    }
    if (auto* game = dynamic_cast<GameplayState*>(stack_.top()); game != nullptr) {
        if (ImGui::CollapsingHeader("Démo 3D")) {
            game->world().draw_controls();
        }
    }
}
#endif
