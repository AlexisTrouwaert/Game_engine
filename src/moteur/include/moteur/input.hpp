#pragma once

#include <SDL3/SDL.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace moteur {

// An action the game declares ("move", "skill_1"...): its index in Input.
using ActionId = int;
constexpr ActionId kNoAction = -1;

// Which kind of device the player used last: the interface shows the matching button names, and
// may hide the mouse cursor when a gamepad is in use.
enum class InputDevice { KeyboardMouse, Gamepad };

// One physical control. Its text form, used in the bindings file:
//   key:<name>        a key by its position on the keyboard (SDL scancode name, as on a US QWERTY
//                     keyboard: "key:Q" is the key labelled A on an AZERTY keyboard)
//   mouse:<button>    left, right, middle, x1, x2
//   wheel:up, wheel:down
//   pad:<button>      a, b, x, y (positions: a is the bottom button, Cross on a PlayStation pad),
//                     back, start, leftstick, rightstick, leftshoulder, rightshoulder, dpup,
//                     dpdown, dpleft, dpright...
//   pad:<axis>+ / -   a stick or trigger direction used as a button: leftx-, righty+, lefttrigger+
struct InputSource {
    enum class Kind { Key, MouseButton, Wheel, GamepadButton, GamepadAxis };
    Kind kind = Kind::Key;
    int code = 0;       // SDL_Scancode, mouse button (SDL_BUTTON_*), SDL_GamepadButton, SDL_GamepadAxis
    int direction = 0;  // Wheel and GamepadAxis: +1 or -1

    bool operator==(const InputSource&) const = default;
    auto operator<=>(const InputSource&) const = default;
};

// The source written as `text`, or nothing if it is not valid.
std::optional<InputSource> parse_input_source(std::string_view text);
// The text form of a source (parse_input_source gives it back).
std::string input_source_text(const InputSource& source);
// What to show the player: a key as printed on their keyboard (the A of an AZERTY keyboard for
// "key:Q"), "Clic gauche", "Molette haut", "Manette A"...
std::string input_source_label(const InputSource& source);

// The state of a button action over one tick of the game.
struct ButtonState {
    bool down = false;  // held at the time of the tick
    int presses = 0;    // times it went down since the previous tick
    int releases = 0;   // times it went up since the previous tick
};

// Everything update() can read in one tick: what a replay stores and gives back.
struct InputFrame {
    std::vector<ButtonState> buttons;  // by action (axes: unused)
    std::vector<glm::vec2> axes;       // by action (buttons: unused)
    glm::vec2 pointer{-1.0f};          // window pixels; negative: outside the window
};

// A recording of what update() read, tick after tick: replayed, it gives the same run again (tests,
// bug reports, captures with input). Actions are stored by name, so a recording survives new
// actions being added to the game.
struct InputRecording {
    std::vector<std::string> actions;
    std::vector<bool> axis;          // by action: an axis rather than a button
    std::vector<InputFrame> frames;  // one per tick, from the first
};
std::string input_recording_to_json(const InputRecording& recording);
// Throws std::runtime_error naming `source_name` if the text is not a recording.
InputRecording input_recording_from_json(std::string_view json_text, const std::string& source_name);

// The player's input, as actions rather than keys.
//
// The game declares its actions (add_button, add_axis), then loads the bindings: a JSON file with
// one or several profiles (for instance "move with the mouse" and "move with the keys"), each
// binding every action to sources, keyboard, mouse and gamepad together:
//
//   {"version": 1, "profile": "click",
//    "profiles": {"click": {"label": "Déplacement au clic",
//                           "buttons": {"move_to": ["mouse:left"], "skill_1": ["key:Q", "pad:x"]},
//                           "axes": {"move": {"up": ["key:W"], "down": ["key:S"], "left": ["key:A"],
//                                             "right": ["key:D"], "stick": "left"}}}}}
//
// A second file, the player's (same format, usually a few entries), is applied on top: it may
// choose the profile and change bindings; the rest keeps the defaults.
//
// Timing: events arrive whenever the frame polls them; the game reads the actions at a fixed rate
// (update). A press is kept until a tick has seen it, and counted by that tick only: a short tap
// between two ticks is never lost, and never seen twice when two ticks run in the same frame. The
// Application calls end_tick() after each update().
class Input {
public:
    Input();
    ~Input();
    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;

    // --- Actions
    ActionId add_button(const std::string& name);
    ActionId add_axis(const std::string& name);  // 2D: x to the right, y up (forward on screen)
    ActionId find(std::string_view name) const;   // kNoAction if unknown
    // Forgets every action and binding (a scene declares its own).
    void clear_actions();
    std::size_t action_count() const { return actions_.size(); }
    const std::string& action_name(ActionId action) const;
    bool is_axis(ActionId action) const;

    // --- Bindings
    // Throws std::runtime_error naming `source_name` for an invalid file (bad JSON, unknown source,
    // unknown profile). Actions the game did not declare are ignored with a warning in the log.
    void load_bindings(std::string_view json_text, const std::string& source_name);
    void apply_user_bindings(std::string_view json_text, const std::string& source_name);
    // The player's file: the profile chosen, and the bindings changed from the defaults.
    std::string user_bindings_json() const;
    std::vector<std::string> profiles() const;
    std::string profile_label(const std::string& profile) const;
    const std::string& profile() const { return profile_; }
    void set_profile(const std::string& profile);  // throws std::invalid_argument if unknown
    // The sources of a button action in the current profile.
    std::vector<InputSource> sources(ActionId action) const;
    // "A / Manette X" (keyboard and mouse first, then gamepad); for an axis, its directions. With
    // `device`, only that device's sources (what the player is using: see last_device()).
    std::string describe(ActionId action, std::optional<InputDevice> device = std::nullopt) const;

    // --- Events (the Application feeds them)
    void process_event(const SDL_Event& event);
    // The pointer, in window pixels (the Application converts from points); negative: outside.
    void set_pointer(glm::vec2 pixels);
    // Every source up (the window lost the focus: nothing stays held forever).
    void release_all();
    // false: real events are ignored (runs that must not depend on the desk, and replays).
    void set_enabled(bool enabled);
    bool enabled() const { return enabled_; }
    // After each update(): the presses and releases it has seen are consumed.
    void end_tick();
    // true: every action reads as idle and the pointer as outside, without losing anything (the
    // StateStack mutes the input for the states under the top one while they update).
    void set_muted(bool muted) { muted_ = muted; }
    bool muted() const { return muted_; }

    // --- What the game reads in update()
    bool down(ActionId action) const;
    bool pressed(ActionId action) const { return presses(action) > 0; }
    bool released(ActionId action) const;
    int presses(ActionId action) const;
    glm::vec2 axis(ActionId action) const;  // length at most 1
    glm::vec2 pointer() const { return muted_ ? glm::vec2(-1.0f) : frame_.pointer; }
    InputDevice last_device() const { return last_device_; }
    int gamepad_count() const { return static_cast<int>(gamepads_.size()); }

    // --- Replays
    // The state update() would read now (to record it).
    InputFrame frame() const;
    // Replaces the state update() reads (a recorded tick). Real events should be disabled.
    void set_frame(const InputFrame& frame);
    // Tick `tick` of `recording`, matched to this game's actions by name (actions the recording does
    // not know stay idle). Past its end: nothing held, pointer outside.
    void set_frame(const InputRecording& recording, std::size_t tick);
    // Starts a recording of this game's actions (frames are added by the Application).
    InputRecording start_recording() const;

    // Stick dead zone (radial, 0 to 1) and the point where a stick or trigger direction counts as a
    // pressed button.
    float dead_zone = 0.2f;
    float axis_press_threshold = 0.5f;

private:
    struct AxisBinding {
        std::vector<InputSource> up, down, left, right;
        int stick = -1;  // 0 left stick, 1 right stick, -1 none
    };
    struct Profile {
        std::string label;
        std::map<std::string, std::vector<InputSource>> buttons;  // by action name
        std::map<std::string, AxisBinding> axes;
    };
    struct Action {
        std::string name;
        bool axis = false;
    };

    // Recomputes which button actions are down after a source changed, counting the transitions.
    void source_changed(const InputSource& source, bool down);
    bool source_down(const InputSource& source) const;
    bool any_down(const std::vector<InputSource>& sources) const;
    glm::vec2 stick(int which) const;
    void refresh_buttons(std::optional<InputSource> pulsed = std::nullopt);
    // Fills bound_buttons_ and bound_axes_ from the profile (the player's changes over the defaults).
    void resolve();
    void parse_profiles(std::string_view json_text, const std::string& source_name, bool user);

    std::vector<Action> actions_;
    std::map<std::string, Profile> defaults_;  // the profiles of the defaults file
    std::map<std::string, Profile> user_;      // the player's changes (same shape, sparse)
    std::string profile_;
    std::string user_profile_;                 // the profile the player chose, if any

    std::vector<std::vector<InputSource>> bound_buttons_;  // by action, for the current profile
    std::vector<AxisBinding> bound_axes_;
    std::vector<InputSource> held_;            // sources down now (keys, buttons)
    std::map<int, float> gamepad_axes_;        // SDL_GamepadAxis -> -1..1
    std::vector<SDL_Gamepad*> gamepads_;
    InputFrame frame_;                         // what update() reads; pending presses accumulate here
    bool replaying_ = false;
    bool enabled_ = true;
    bool muted_ = false;
    InputDevice last_device_ = InputDevice::KeyboardMouse;
};

}  // namespace moteur
