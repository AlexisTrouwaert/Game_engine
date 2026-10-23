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
#include "moteur/ui.hpp"

#include <filesystem>
#include <functional>

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

// The local time in the language's order ("03/10/2026 14:22" in French: `date-time`).
std::string local_time(const StatesDemo& demo, std::int64_t seconds) {
    SDL_DateTime t{};
    if (!SDL_TimeToDateTime(static_cast<SDL_Time>(seconds) * SDL_NS_PER_SECOND, &t, true)) {
        return "?";
    }
    const auto two = [](int value) {
        char text[8];
        std::snprintf(text, sizeof(text), "%02d", value);
        return std::string(text);
    };
    return demo.tr("date-time", {{"day", two(t.day)}, {"month", two(t.month)}, {"year", std::to_string(t.year)},
                                 {"hour", two(t.hour)}, {"minute", two(t.minute)}});
}

std::string slot_label(const StatesDemo& demo, const std::string& name) {
    if (name.starts_with("emplacement_")) {
        return demo.tr("slot-manual", {{"number", name.substr(12)}});
    }
    if (name.starts_with("auto_")) {
        return demo.tr("slot-auto", {{"number", name.substr(5)}});
    }
    return name;
}

// A screen of the slice (milestone 9 bis, part 6): its document (assets/ui/*.ui) is open while the
// state is on the stack, filled through the interface's model, and its events are handled in the
// tick. The documents of the states below stay drawn under it (a dimmed pause over the game).
class Screen : public moteur::GameState {
public:
    Screen(StatesDemo& demo, std::string path, int layer) : demo_(demo), path_(std::move(path)), layer_(layer) {}

    bool transparent() const override { return true; }
    bool blocking() const override { return true; }

    void enter() override {
        document_ = &demo_.ui().open(path_, layer_);
        refresh();
    }
    void exit() override {
        demo_.ui().close(path_);
        document_ = nullptr;
    }
    void update(double) override {
        for (const moteur::UiEvent& event : demo_.events(path_)) {
            if (event.kind == moteur::UiEvent::Kind::Activate || event.kind == moteur::UiEvent::Kind::Back) {
                if (!event.action.empty()) {
                    action(event.action, event.index);
                }
            } else if (event.kind == moteur::UiEvent::Kind::Change) {
                changed(event);
            }
        }
        refresh();
    }

protected:
    // Writes what the screen shows into the model; called every tick (an unchanged value costs
    // nothing), so that a change of language or of device shows at once.
    virtual void refresh() {}
    // A button (or the document's back-action): `index` is the item of a list, -1 outside one.
    virtual void action(const std::string& name, int index) = 0;
    // A control changed its value (already written in the model).
    virtual void changed(const moteur::UiEvent&) {}

    moteur::UiModel& model() { return demo_.ui().model(); }
    moteur::UiDocument& document() { return *document_; }
    void back() {
        if (!stack()->has_pending()) {
            stack()->pop();
        }
    }
    void push(std::unique_ptr<moteur::GameState> state) {
        if (!stack()->has_pending()) {
            stack()->push(std::move(state));
        }
    }

    StatesDemo& demo_;

private:
    std::string path_;
    int layer_;
    moteur::UiDocument* document_ = nullptr;
};

// The layers of the documents: what is pushed over draws above and takes the input.
constexpr int kLayerBase = 0;      // title, loading
constexpr int kLayerPause = 10;
constexpr int kLayerMenu = 20;     // options, slots
constexpr int kLayerPage = 30;     // the options' pages
constexpr int kLayerDialog = 40;   // a question, a key to press

// A list of the model (`<repeat source>`, `<select source>`): its count and each item's name.
void set_list(moteur::UiModel& model, const std::string& list, const std::vector<std::string>& names) {
    model.set_count(list, static_cast<int>(names.size()));
    for (std::size_t i = 0; i < names.size(); ++i) {
        model.set(list + "." + std::to_string(i) + ".name", names[i]);
    }
}

// --- The display options (milestone 8, part 8): chosen with the arrows, applied with "Apply"; a new
// mode or screen is kept only if the player answers within 15 seconds.
class DisplayOptionsState final : public Screen {
public:
    explicit DisplayOptionsState(StatesDemo& demo) : Screen(demo, "ui/options_display.ui", kLayerPage), pending_(demo.app().display()) {
        for (int mode = 0; mode < 4; ++mode) {  // the modes the GPU can do
            if (demo.app().renderer().supports(static_cast<moteur::AntiAliasing>(mode))) {
                anti_aliasing_.push_back(static_cast<moteur::AntiAliasing>(mode));
            }
        }
    }

    const char* name() const override { return "options_display"; }

    void exit() override {
        demo_.ui().close(kConfirm);
        Screen::exit();
    }

    void update(double dt) override {
        if (confirming_) {
            for (const moteur::UiEvent& event : demo_.events(kConfirm)) {
                if (event.action == "keep") {
                    confirming_ = false;
                    demo_.ui().close(kConfirm);
                } else if (event.action == "cancel") {
                    revert();
                }
            }
            if (confirming_ && --confirm_ticks_ <= 0) {
                revert();
            }
        }
        Screen::update(dt);
    }

protected:
    void refresh() override {
        moteur::UiModel& m = model();
        const std::vector<std::string> screens = moteur::displays();
        std::vector<std::string> names;
        for (int i = 0; i < std::max(1, static_cast<int>(screens.size())); ++i) {
            names.push_back(i < static_cast<int>(screens.size()) ? screens[static_cast<std::size_t>(i)]
                                                                 : demo_.tr("options-display-screen-number", {{"number", i + 1}}));
        }
        set_list(m, "displays", names);
        names.clear();
        for (const int limit : kFrameLimits) {
            names.push_back(demo_.tr("options-display-frame-limit", {{"limit", limit}}));
        }
        set_list(m, "frame_limits", names);
        names.clear();
        for (int percent = 50; percent <= 100; percent += 10) {
            names.push_back(demo_.tr("common-percent", {{"value", percent / 100.0}}));
        }
        set_list(m, "render_scales", names);
        names.clear();
        for (const moteur::AntiAliasing mode : anti_aliasing_) {
            names.push_back(anti_aliasing_label(mode));
        }
        set_list(m, "anti_aliasing", names);
        names.clear();
        for (const int percent : kUiScales) {
            names.push_back(demo_.tr("common-percent", {{"value", percent / 100.0}}));
        }
        set_list(m, "ui_scales", names);

        m.set("display.mode", static_cast<int>(pending_.mode));
        m.set("display.screen", pending_.display);
        m.set("display.vsync", pending_.vsync ? 1 : 0);
        m.set("display.frame_limit", index_of(kFrameLimits, pending_.frame_limit));
        m.set("display.render_scale", std::clamp((static_cast<int>(std::lround(pending_.render_scale * 100.0f)) - 50) / 10, 0, 5));
        const auto aa = std::find(anti_aliasing_.begin(), anti_aliasing_.end(), pending_.anti_aliasing);
        m.set("display.anti_aliasing", aa == anti_aliasing_.end() ? 0 : static_cast<int>(aa - anti_aliasing_.begin()));
        m.set("display.shadows", static_cast<int>(pending_.shadows));
        m.set("display.ui_scale", index_of(kUiScales, demo_.ui_scale_percent()));
        if (confirming_) {
            m.set("confirm.title", demo_.tr("options-display-confirm-title"));
            m.set("confirm.text", demo_.tr("options-display-confirm-countdown",
                                           {{"seconds", (confirm_ticks_ + moteur::tick_rate() - 1) / moteur::tick_rate()}}));
        }
    }

    void changed(const moteur::UiEvent&) override {
        const moteur::UiModel& m = model();
        const auto at = [&m](const char* path) { return static_cast<int>(m.number(path)); };
        pending_.mode = static_cast<moteur::WindowMode>(at("display.mode"));
        pending_.display = at("display.screen");
        pending_.vsync = at("display.vsync") != 0;
        pending_.frame_limit = kFrameLimits[std::clamp(at("display.frame_limit"), 0, 5)];
        pending_.render_scale = static_cast<float>(50 + std::clamp(at("display.render_scale"), 0, 5) * 10) / 100.0f;
        if (!anti_aliasing_.empty()) {
            pending_.anti_aliasing = anti_aliasing_[static_cast<std::size_t>(
                std::clamp(at("display.anti_aliasing"), 0, static_cast<int>(anti_aliasing_.size()) - 1))];
        }
        pending_.shadows = static_cast<moteur::ShadowQuality>(std::clamp(at("display.shadows"), 0, 2));
        // The interface's size applies at once (nothing to confirm: the menu stays readable).
        demo_.set_ui_scale_percent(kUiScales[std::clamp(at("display.ui_scale"), 0, static_cast<int>(std::size(kUiScales)) - 1)]);
    }

    void action(const std::string& name, int) override {
        if (name == "apply") {
            apply();
        } else if (name == "back") {
            back();
        }
    }

private:
    static constexpr const char* kConfirm = "ui/confirm.ui";
    static constexpr int kFrameLimits[] = {0, 30, 60, 120, 144, 240};
    static constexpr int kUiScales[] = {75, 90, 100, 110, 125, 150, 175, 200};

    template <std::size_t N>
    static int index_of(const int (&values)[N], int value) {
        for (std::size_t i = 0; i < N; ++i) {
            if (values[i] == value) {
                return static_cast<int>(i);
            }
        }
        return 0;
    }

    std::string anti_aliasing_label(moteur::AntiAliasing mode) const {
        switch (mode) {
            case moteur::AntiAliasing::None:
                return demo_.tr("anti-aliasing-none");
            case moteur::AntiAliasing::Fxaa:
                return "FXAA";
            case moteur::AntiAliasing::Msaa2:
                return "MSAA 2x";
            default:
                return "MSAA 4x";
        }
    }

    void apply() {
        previous_ = demo_.app().display();
        demo_.app().apply_display(pending_);
        if (pending_.changes_screen(previous_)) {
            confirming_ = true;
            confirm_ticks_ = static_cast<int>(moteur::ticks_from_seconds(15.0));
            refresh();  // the question's texts, before the dialog is laid out
            demo_.ui().open(kConfirm, kLayerDialog);
        }
    }

    void revert() {
        demo_.app().apply_display(previous_);
        pending_ = previous_;
        confirming_ = false;
        demo_.ui().close(kConfirm);
    }

    moteur::DisplaySettings pending_;
    moteur::DisplaySettings previous_;
    std::vector<moteur::AntiAliasing> anti_aliasing_;
    bool confirming_ = false;
    int confirm_ticks_ = 0;
};

// --- The audio options (milestone 8, part 12): the player's volumes, heard as they change (the
// sliders' click is an interface sound, played after the change).
class AudioOptionsState final : public Screen {
public:
    explicit AudioOptionsState(StatesDemo& demo) : Screen(demo, "ui/options_audio.ui", kLayerPage) {}

    const char* name() const override { return "options_audio"; }

protected:
    void refresh() override {
        const moteur::Audio& audio = demo_.app().audio();
        for (int row = 0; row < 5; ++row) {
            const float volume = row == 0 ? audio.master_volume() : audio.group_volume(kGroups[row - 1]);
            const std::string path = std::string("audio.") + kNames[row];
            model().set(path, std::round(volume * 10.0f) / 10.0f);
            model().set(path + "_text", demo_.tr("audio-volume", {{"volume", std::round(volume * 100.0f) / 100.0f}}));
        }
    }

    void changed(const moteur::UiEvent&) override {
        moteur::Audio& audio = demo_.app().audio();
        for (int row = 0; row < 5; ++row) {
            const float volume = static_cast<float>(model().number(std::string("audio.") + kNames[row]));
            if (row == 0) {
                audio.set_master_volume(volume);
            } else {
                audio.set_group_volume(kGroups[row - 1], volume);
            }
        }
    }

    void action(const std::string& name, int) override {
        if (name == "back" && !stack()->has_pending()) {
            demo_.app().save_audio_settings();
            stack()->pop();
        }
    }

private:
    // Row 0 is the master volume; the others, the groups in the order of the rows.
    static constexpr const char* kNames[] = {"master", "music", "effects", "ambience", "interface"};
    static constexpr moteur::SoundGroup kGroups[] = {moteur::SoundGroup::Music, moteur::SoundGroup::Effects,
                                                     moteur::SoundGroup::Ambience, moteur::SoundGroup::Interface};
};

// --- The controls (milestone 8, part 12): the layout (profile), and each action rebound, for the
// device the player confirms with (the keyboard and mouse, or the gamepad).
class ControlsOptionsState final : public Screen {
public:
    explicit ControlsOptionsState(StatesDemo& demo) : Screen(demo, "ui/options_controls.ui", kLayerPage) {}

    const char* name() const override { return "options_controls"; }

    void exit() override {
        demo_.app().input().cancel_capture();
        demo_.ui().close(kCapture);
        Screen::exit();
    }

    void update(double dt) override {
        if (capturing_) {
            moteur::Input& input = demo_.app().input();
            if (!input.capturing()) {
                if (const std::optional<moteur::InputSource> source = input.take_captured()) {
                    captured(*source);
                }
            }
            return;
        }
        Screen::update(dt);
    }

protected:
    void refresh() override {
        moteur::UiModel& m = model();
        moteur::Input& input = demo_.app().input();
        const bool pad = input.last_device() == moteur::InputDevice::Gamepad;
        m.set("controls.device", demo_.tr(pad ? "controls-device-pad" : "controls-device-keyboard"));
        const std::vector<std::string> profiles = input.profiles();
        std::vector<std::string> names;
        for (const std::string& profile : profiles) {
            names.push_back(profile_label(demo_.app(), profile));
        }
        set_list(m, "profiles", names);
        const auto at = std::find(profiles.begin(), profiles.end(), input.profile());
        m.set("controls.profile", at == profiles.end() ? 0 : static_cast<int>(at - profiles.begin()));
        const moteur::Localization& localization = demo_.app().localization();
        m.set_count("controls", static_cast<int>(kEntries.size()));
        for (std::size_t i = 0; i < kEntries.size(); ++i) {
            std::string keys;
            for (const moteur::InputSource& source : sources(kEntries[i])) {
                if (on_gamepad(source) == pad) {
                    keys += (keys.empty() ? "" : ", ") + moteur::input_source_label(source, &localization);
                }
            }
            const std::string item = "controls." + std::to_string(i) + ".";
            m.set(item + "label", entry_label(kEntries[i]));
            m.set(item + "keys", keys.empty() ? demo_.tr("input-none") : keys);
        }
        std::vector<moteur::LocalizedArg> args;
        for (const auto& [name, value] : message_args_) {
            args.emplace_back(name, value);
        }
        m.set("controls.message", message_.empty() ? std::string() : demo_.tr(message_, args));
        if (capturing_) {
            const bool capture_pad = device_ == moteur::InputDevice::Gamepad;
            m.set("capture.action", entry_label(kEntries[static_cast<std::size_t>(entry_)]));
            m.set("capture.press", demo_.tr(capture_pad ? "controls-press-pad" : "controls-press-keyboard"));
            m.set("capture.cancel", demo_.tr(capture_pad ? "controls-cancel-pad" : "controls-cancel-keyboard"));
        }
    }

    void changed(const moteur::UiEvent& event) override {
        if (event.action != "profile") {
            return;
        }
        moteur::Input& input = demo_.app().input();
        const std::vector<std::string> profiles = input.profiles();
        const int index = static_cast<int>(model().number("controls.profile"));
        if (index >= 0 && index < static_cast<int>(profiles.size())) {
            input.set_profile(profiles[static_cast<std::size_t>(index)]);
            message_.clear();
            message_args_.clear();
            save();
        }
    }

    void action(const std::string& name, int index) override {
        if (name == "rebind" && index >= 0 && index < static_cast<int>(kEntries.size())) {
            start_capture(index);
        } else if (name == "reset") {
            demo_.app().input().reset_all_sources();
            message_ = "controls-reset-done";
            message_args_.clear();
            save();
        } else if (name == "back") {
            back();
        }
    }

private:
    static constexpr const char* kCapture = "ui/capture.ui";

    // What the player can rebind: a button action, or one direction of an axis action.
    struct Entry {
        const char* label;  // message identifier
        int number;         // its $number (the skills), 0 for none
        const char* action;
        int direction;  // -1: a button; otherwise a moteur::AxisDirection
    };
    static constexpr int kButton = -1;
    static constexpr std::array<Entry, 21> kEntries{{
        {"action-move-to", 0, "move_to", kButton},
        {"action-forward", 0, "move", static_cast<int>(moteur::AxisDirection::Up)},
        {"action-backward", 0, "move", static_cast<int>(moteur::AxisDirection::Down)},
        {"action-left", 0, "move", static_cast<int>(moteur::AxisDirection::Left)},
        {"action-right", 0, "move", static_cast<int>(moteur::AxisDirection::Right)},
        {"action-skill", 1, "skill_1", kButton},
        {"action-skill", 2, "skill_2", kButton},
        {"action-skill", 3, "skill_3", kButton},
        {"action-skill", 4, "skill_4", kButton},
        {"action-skill", 5, "skill_5", kButton},
        {"action-skill", 6, "skill_6", kButton},
        {"action-zoom-in", 0, "zoom_in", kButton},
        {"action-zoom-out", 0, "zoom_out", kButton},
        {"action-select", 0, "select", kButton},
        {"action-next", 0, "next", kButton},
        {"action-follow", 0, "follow", kButton},
        {"action-camera-up", 0, "camera", static_cast<int>(moteur::AxisDirection::Up)},
        {"action-camera-down", 0, "camera", static_cast<int>(moteur::AxisDirection::Down)},
        {"action-camera-left", 0, "camera", static_cast<int>(moteur::AxisDirection::Left)},
        {"action-camera-right", 0, "camera", static_cast<int>(moteur::AxisDirection::Right)},
        {"action-pause", 0, "pause", kButton},
    }};

    std::string entry_label(const Entry& entry) const {
        return entry.number > 0 ? demo_.tr(entry.label, {{"number", entry.number}}) : demo_.tr(entry.label);
    }

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

    void start_capture(int entry) {
        entry_ = entry;
        device_ = demo_.app().input().last_device();  // the device the player confirmed with
        capturing_ = true;
        message_.clear();
        message_args_.clear();
        refresh();  // the dialog's texts
        demo_.ui().open(kCapture, kLayerDialog);
        demo_.app().input().begin_capture();
    }

    void end_capture() {
        capturing_ = false;
        demo_.ui().close(kCapture);
    }

    void captured(const moteur::InputSource& source) {
        moteur::Input& input = demo_.app().input();
        const bool pad = device_ == moteur::InputDevice::Gamepad;
        const bool cancel = pad ? source == moteur::InputSource{moteur::InputSource::Kind::GamepadButton, SDL_GAMEPAD_BUTTON_START, 0}
                                : source == moteur::InputSource{moteur::InputSource::Kind::Key, SDL_SCANCODE_ESCAPE, 0};
        if (cancel) {
            end_capture();
            return;
        }
        if (on_gamepad(source) != pad) {
            input.begin_capture();  // the other device: still waiting for this one
            return;
        }
        end_capture();
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
                taken += (taken.empty() ? "" : ", ") + entry_label(other);
            }
        }
        // This device's sources are replaced; the other device's stay.
        std::vector<moteur::InputSource> list = sources(entry);
        std::erase_if(list, [pad](const moteur::InputSource& s) { return on_gamepad(s) == pad; });
        list.push_back(source);
        set_sources(entry, std::move(list));
        demo_.play_confirm();
        message_.clear();
        message_args_.clear();
        if (!taken.empty()) {
            message_ = "controls-taken";
            message_args_ = {{"key", moteur::input_source_label(source, &demo_.app().localization())}, {"actions", taken}};
        }
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
            message_ = "controls-save-failed";
            message_args_.clear();
        }
    }

    bool capturing_ = false;
    int entry_ = 0;
    moteur::InputDevice device_ = moteur::InputDevice::KeyboardMouse;
    std::string message_;  // a message identifier, and its variables (put in words in refresh())
    std::vector<std::pair<std::string, std::string>> message_args_;
};

// --- The language (milestone 9 bis, part 5): the languages offered, by their own names; the
// choice applies at once.
class LanguageOptionsState final : public Screen {
public:
    explicit LanguageOptionsState(StatesDemo& demo) : Screen(demo, "ui/options_language.ui", kLayerPage) {}

    const char* name() const override { return "options_language"; }

protected:
    void refresh() override {
        const moteur::Localization& localization = demo_.app().localization();
        std::vector<std::string> names;
        for (const moteur::LanguageInfo& info : localization.languages()) {
            names.push_back((info.code == localization.language() ? "• " : "") + info.name);  // • the current one
        }
        set_list(model(), "languages", names);
    }

    void action(const std::string& name, int index) override {
        const std::vector<moteur::LanguageInfo>& languages = demo_.app().localization().languages();
        if (name == "language" && index >= 0 && index < static_cast<int>(languages.size())) {
            demo_.app().set_language(languages[static_cast<std::size_t>(index)].code);
        } else if (name == "back") {
            back();
        }
    }
};

// --- The options: a menu of their pages, over the title or the pause.
class OptionsState final : public Screen {
public:
    explicit OptionsState(StatesDemo& demo) : Screen(demo, "ui/options.ui", kLayerMenu) {}

    const char* name() const override { return "options"; }

protected:
    void action(const std::string& name, int) override {
        if (name == "display") {
            push(std::make_unique<DisplayOptionsState>(demo_));
        } else if (name == "audio") {
            push(std::make_unique<AudioOptionsState>(demo_));
        } else if (name == "controls") {
            push(std::make_unique<ControlsOptionsState>(demo_));
        } else if (name == "language") {
            push(std::make_unique<LanguageOptionsState>(demo_));
        } else if (name == "back") {
            back();
        }
    }
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
    void enter() override { demo_.set_music("jeu"); }

private:
    Demo3D::Options with_save(Demo3D::Options options, std::shared_ptr<const moteur::SaveGame> saved) {
        options.saved_game = std::move(saved);
        options.ui = &demo_.ui();  // the overlay drawn with the menus
        options.sound = demo_.sound();  // the sounds through the same events
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
class PauseState final : public Screen {
public:
    // leave: the autopilot goes back to the title from this pause (otherwise it resumes).
    PauseState(StatesDemo& demo, bool leave) : Screen(demo, "ui/pause.ui", kLayerPause), leave_(leave) {}

    const char* name() const override { return "pause"; }

    // The rule chosen for the sound: the music goes on, lower; the world's sounds wait.
    void enter() override {
        Screen::enter();
        document().set_visible(false);  // until the thumbnail of the saves is taken
        set_paused(true);
    }
    void exit() override {
        Screen::exit();
        set_paused(false);
    }

    void update(double dt) override {
        ++ticks_;
        Screen::update(dt);
        if (demo_.options().cycles > 0 && ticks_ == kPauseTicks) {
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
        document().set_visible(true);
        if (demo_.capture_pending() && ticks_ >= 2) {
            renderer.request_capture(demo_.options().capture_path);
            demo_.capture_done();
        }
    }

protected:
    void action(const std::string& name, int) override {
        if (name == "resume") {
            resume();
        } else if (name == "save") {
            open_slots(true);
        } else if (name == "options") {
            push(make_options(demo_));
        } else if (name == "quit") {
            back_to_title();
        }
    }

private:
    void set_paused(bool paused) {
        // A snapshot of the bank (milestone 9 bis, part 7): the music lower, the world's sounds paused.
        if (moteur::AudioEvents* sound = demo_.sound()) {
            if (paused) {
                sound->start_snapshot("pause");
            } else {
                sound->stop_snapshot("pause");
            }
        }
    }
    void open_slots(bool saving);
    void resume() { back(); }
    void back_to_title() {
        if (!stack()->has_pending()) {
            stack()->reset(make_title(demo_));
        }
    }

    bool leave_;
    int ticks_ = 0;
    bool thumbnail_taken_ = false;
};

// --- The slots: to save into (over the pause) or to load from (over the title).
class SlotsState final : public Screen {
public:
    SlotsState(StatesDemo& demo, bool saving) : Screen(demo, "ui/slots.ui", kLayerMenu), saving_(saving) {}

    const char* name() const override { return saving_ ? "sauvegarder" : "charger"; }
    bool transparent() const override { return saving_; }  // the frozen game under it

    void enter() override {
        load_rows();
        Screen::enter();
        // Over the title, the screen of its own; over the pause, the game dimmed under it.
        document().root()->set_class(saving_ ? "shade" : "opaque", true);
    }
    void exit() override {
        forget_thumbnails();
        Screen::exit();
    }

    void render(moteur::Renderer& renderer, double) override {
        if (!saving_) {
            renderer.set_clear_color(0.06f, 0.05f, 0.09f);
        }
    }

protected:
    void refresh() override {
        moteur::UiModel& m = model();
        m.set("slots.title", demo_.tr(saving_ ? "slots-save-title" : "slots-load-title"));
        m.set("slots.status", status_);
        m.set_count("slots", static_cast<int>(rows_.size()));
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            const Row& row = rows_[i];
            const std::string item = "slots." + std::to_string(i) + ".";
            std::string line1;
            std::string line2;
            if (!row.exists) {
                line1 = demo_.tr("slot-empty");
            } else if (!row.slot.readable) {
                line1 = demo_.tr("slot-unreadable", {{"error", row.slot.error}});
            } else {
                const nlohmann::json& summary = row.slot.header.summary;
                line1 = demo_.tr(row.slot.from_backup ? "slot-when-backup" : "slot-when",
                                 {{"date", local_time(demo_, row.slot.header.time)}, {"seconds", row.slot.header.play_seconds}});
                line2 = demo_.tr("slot-summary", {{"health", summary.value("vie", 0.0)}, {"creatures", summary.value("creatures", 0)}});
            }
            m.set(item + "thumbnail", row.thumbnail.gpu ? thumbnail_name(i) : std::string());
            m.set(item + "name", slot_label(demo_, row.name));
            m.set(item + "line1", line1);
            m.set(item + "line2", line2);
        }
    }

    void action(const std::string& name, int index) override {
        if (name == "back") {
            back();
        } else if (name == "slot" && index >= 0 && index < static_cast<int>(rows_.size())) {
            choose(static_cast<std::size_t>(index));
        }
    }

private:
    struct Row {
        std::string name;
        bool exists = false;
        moteur::SaveSlots::Slot slot;
        moteur::Texture thumbnail;
    };

    static std::string thumbnail_name(std::size_t index) { return "slot_thumbnail_" + std::to_string(index); }

    void forget_thumbnails() {
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            demo_.ui().set_texture(thumbnail_name(i), nullptr);
        }
    }

    void load_rows() {
        forget_thumbnails();
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
        // Once the rows are in place (the textures do not move any more).
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            if (rows_[i].thumbnail.gpu) {
                demo_.ui().set_texture(thumbnail_name(i), &rows_[i].thumbnail);
            }
        }
    }

    void choose(std::size_t index) {
        const Row& row = rows_[index];
        if (saving_) {
            std::string error;
            if (demo_.save_game(row.name, demo_.pause_thumbnail(), error)) {
                status_ = demo_.tr("slots-saved", {{"slot", slot_label(demo_, row.name)}});
            } else {
                status_ = demo_.tr("slots-failed", {{"error", error}});
            }
            load_rows();
        } else if (row.exists) {
            demo_.load_game(row.name);
        }
    }

    bool saving_;
    std::vector<Row> rows_;
    std::string status_;
};

void PauseState::open_slots(bool saving) {
    push(std::make_unique<SlotsState>(demo_, saving));
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
        : LoadingState(std::move(steps), "loading-world", std::move(make_next)), demo_(demo) {}

    void enter() override {
        demo_.ui().open(kDocument, kLayerBase);
        demo_.ui().model().set("loading.progress", 0.0);
        demo_.loading_started();
    }
    void exit() override { demo_.ui().close(kDocument); }

protected:
    void draw(moteur::Renderer& renderer, float progress, const std::string& label) override {
        renderer.set_clear_color(0.02f, 0.02f, 0.03f);
        moteur::UiModel& model = demo_.ui().model();
        model.set("loading.progress", progress);
        model.set("loading.step", demo_.tr(label));  // the steps' labels are message identifiers
    }

    void failed(const std::string& error) override {
        SDL_Log("States: loading failed: %s", error.c_str());
        demo_.set_error(error);
        stack()->reset(make_title(demo_));
    }

private:
    static constexpr const char* kDocument = "ui/loading.ui";
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
        moteur::Asset<moteur::SoundBank> bank;
    };
    auto held = std::make_shared<Held>();
    moteur::Assets& assets = demo.app().assets();
    std::vector<moteur::LoadingState::Step> steps = {
        // Milestone 8, part 5: every file read and decoded on the job system's threads while the
        // screen keeps moving; the steps below then only create the GPU objects.
        {"loading-files",
         [&assets, held] {
             held->font = assets.font(Demo3D::kFont);
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
         },
         [&assets] { return assets.prefetching() == 0; }},
        {"loading-barrel",
         [&assets, held] {
             if (assets.exists(Demo3D::kBarrel)) {
                 held->barrel = assets.model(Demo3D::kBarrel);
             }
         }},
        {"loading-decor",
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
        {"loading-characters",
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
        {"loading-sounds",
         [&demo, held] {
             // The bank's sounds (milestone 9 bis, part 7): loaded with the screens' events, held by
             // them for the whole run; the bank is held here too, should the game be loaded alone.
             if (moteur::AudioEvents* sound = demo.sound(); sound != nullptr && sound->bank() != nullptr) {
                 held->bank = demo.app().assets().sound_bank(Demo3D::kBank);
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
class TitleState final : public Screen {
public:
    explicit TitleState(StatesDemo& demo) : Screen(demo, "ui/title.ui", kLayerBase) {}

    const char* name() const override { return "title"; }
    bool transparent() const override { return false; }
    bool blocking() const override { return false; }

    void enter() override {
        demo_.set_music("titre");
        can_continue_ = !demo_.latest_slot().empty();
        Screen::enter();
    }
    void uncovered() override { can_continue_ = !demo_.latest_slot().empty(); }  // back from "Charger"

    void update(double dt) override {
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
            return;
        }
        Screen::update(dt);
    }

    void render(moteur::Renderer& renderer, double) override { renderer.set_clear_color(0.06f, 0.05f, 0.09f); }

protected:
    void refresh() override {
        moteur::UiModel& m = model();
        m.set("title.has_error", !demo_.error().empty());
        m.set("title.error", demo_.error().empty() ? std::string() : demo_.tr("title-error", {{"error", demo_.error()}}));
        m.set("title.can_continue", can_continue_);
    }

    void action(const std::string& name, int) override {
        if (name == "play") {
            play();
        } else if (name == "continue") {
            demo_.load_game(demo_.latest_slot());
        } else if (name == "load") {
            push(std::make_unique<SlotsState>(demo_, false));
        } else if (name == "options") {
            push(make_options(demo_));
        } else if (name == "quit") {
            demo_.finish();
        }
    }

private:
    void play() {
        if (!stack()->has_pending()) {
            demo_.set_error({});
            stack()->replace(make_loading(demo_));
        }
    }

    int ticks_ = 0;
    bool finished_ = false;
    bool can_continue_ = false;
};

std::unique_ptr<moteur::GameState> make_title(StatesDemo& demo) {
    return std::make_unique<TitleState>(demo);
}

// The screens of the tour of the interface, in turn, with their depth on the stack: 1 over the
// title, 2 over the options.
struct TourScreen {
    const char* name;  // the capture's name
    int depth;
    std::function<std::unique_ptr<moteur::GameState>(StatesDemo&)> make;
};

const std::vector<TourScreen>& tour_screens() {
    static const std::vector<TourScreen> screens = {
        {"title", 0, nullptr},
        {"options", 1, [](StatesDemo& demo) { return make_options(demo); }},
        {"options_display", 2, [](StatesDemo& demo) { return std::make_unique<DisplayOptionsState>(demo); }},
        {"options_audio", 2, [](StatesDemo& demo) { return std::make_unique<AudioOptionsState>(demo); }},
        {"options_controls", 2, [](StatesDemo& demo) { return std::make_unique<ControlsOptionsState>(demo); }},
        {"options_language", 2, [](StatesDemo& demo) { return std::make_unique<LanguageOptionsState>(demo); }},
        {"load", 1, [](StatesDemo& demo) { return std::make_unique<SlotsState>(demo, false); }},
    };
    return screens;
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
    ui_scale_ = &variables.add_int("ui.scale", 100, 75, 200, "taille de l'interface, en pour cent", moteur::Variable::Archive);
    ui_ = std::make_unique<moteur::Ui>(app, Demo3D::kFont);  // the shipped fonts (no Chinese)
    if (app.assets().exists(Demo3D::kAmbience) && app.assets().exists(Demo3D::kBank)) {  // tools/audio/fetch_test_sounds.py
        sound_ = std::make_unique<moteur::AudioEvents>(app.audio(), app.assets());
        sound_->load(Demo3D::kBank);
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
    if (sound_) {
        sound_->stop_all();
    }
}

void StatesDemo::on_event(const SDL_Event& event) {
    ui_->on_event(event);
    stack_.on_event(event);
}

void StatesDemo::tour_tick() {
    constexpr int kHold = 30;  // ticks on each screen: its transitions (0.12 s) are long over
    if (++tour_ticks_ < kHold || stack_.has_pending()) {
        return;
    }
    tour_ticks_ = 0;
    const std::vector<TourScreen>& screens = tour_screens();
    tour_problems_ = std::max(tour_problems_, 0);
    if (tour_step_ >= static_cast<int>(screens.size())) {
        // The last capture was taken with the previous frame.
        std::cout << "ui tour: " << screens.size() << " screens, " << tour_problems_ << " problems\n";
        app_.report_value("ui_problems", tour_problems_);
        finish();
        return;
    }
    // The screen on show: captured with the next frame, its problems counted (once each).
    const TourScreen& shown = screens[static_cast<std::size_t>(tour_step_)];
    tour_capture_ = options_.ui_tour + "/" + shown.name + ".png";
    for (const std::string& problem : ui_->problems()) {
        if (std::find(tour_seen_.begin(), tour_seen_.end(), problem) == tour_seen_.end()) {
            tour_seen_.push_back(problem);
            std::cout << "ui tour: " << shown.name << ": " << problem << '\n';
            ++tour_problems_;
        }
    }
    if (++tour_step_ < static_cast<int>(screens.size())) {
        const TourScreen& next = screens[static_cast<std::size_t>(tour_step_)];
        for (int depth = shown.depth; depth >= next.depth; --depth) {
            stack_.pop();
        }
        stack_.push(next.make(*this));
    }
}

void StatesDemo::update(double dt) {
    elapsed_ += dt;
    if (options_.run_seconds > 0.0 && elapsed_ >= options_.run_seconds) {
        finish();
    }
    ui_seconds_ += dt;
    // The interface first: its events are those of this tick, read by the states.
    ui_->tick(moteur::UiInput::from_actions(app_.input()));
    events_ = ui_->take_events();
    stack_.update(dt);
    if (!options_.ui_tour.empty()) {
        tour_tick();
    }
    // The menus' sounds, once the states have handled the events (a volume heard as it is now).
    for (const moteur::UiEvent& event : events_) {
        if (event.kind == moteur::UiEvent::Kind::Focus || event.kind == moteur::UiEvent::Kind::Change) {
            play_click();
        } else if (event.kind == moteur::UiEvent::Kind::Activate) {
            play_confirm();
        }
    }
}

std::vector<moteur::UiEvent> StatesDemo::events(std::string_view document) const {
    std::vector<moteur::UiEvent> out;
    for (const moteur::UiEvent& event : events_) {
        if (event.document == document) {
            out.push_back(event);
        }
    }
    return out;
}

int StatesDemo::ui_scale_percent() const {
    return ui_scale_->as_int();
}

void StatesDemo::set_ui_scale_percent(int percent) {
    ui_scale_->set_int(percent);
}

void StatesDemo::render(moteur::Renderer& renderer, double alpha) {
    if (!pending_thumbnail_.empty()) {
        renderer.request_capture(pending_thumbnail_, kThumbnailWidth);  // this frame, as it is
        pending_thumbnail_.clear();
    }
    stack_.render(renderer, alpha);
    // The screen's density (a window moved to a Retina screen keeps its interface's size) times the
    // player's choice. The interface's clock is the ticks' (transitions in step with a replay).
    float density = app_.to_pixels({1.0f, 0.0f}).x - app_.to_pixels({0.0f, 0.0f}).x;
    density = density > 0.0f ? density : 1.0f;
    ui_->set_scale(density * static_cast<float>(ui_scale_->as_int()) / 100.0f);
    ui_->render(renderer, ui_seconds_);
    ui_seconds_ = 0.0;
    if (sound_) {
        // The events after the frame's listener (the game's camera): real time for their fades.
        const Uint64 now = SDL_GetTicksNS();
        const float dt = sound_clock_ == 0 ? 0.0f : static_cast<float>(static_cast<double>(now - sound_clock_) * 1e-9);
        sound_clock_ = now;
        sound_->update(std::min(dt, 0.1f));
    }
    if (!tour_capture_.empty()) {
        renderer.request_capture(tour_capture_);
        tour_capture_.clear();
    }
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
        set_error(tr("save-unreadable", {{"slot", slot_label(*this, slot)}, {"error", error}}));
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
    if (sound_) {
        sound_->play("interface/clic");
    }
}

void StatesDemo::play_confirm() {
    if (sound_) {
        sound_->play("interface/validation");
    }
}

void StatesDemo::set_music(std::string_view section) {
    if (!sound_) {
        return;
    }
    if (music_ == moteur::kNoEvent || !sound_->playing(music_)) {
        music_ = sound_->play("musique/tranche", {.labels = {{"section", std::string(section)}}});
    } else {
        sound_->set_parameter(music_, "section", section);
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
