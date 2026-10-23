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

class Localization;

/// An action the game declares ("move", "skill_1"...): its index in Input, in the order of
/// declaration (0, 1, 2...). Valid until Input::clear_actions().
using ActionId = int;
/// No action: what Input::find() returns for an unknown name.
constexpr ActionId kNoAction = -1;

/// Which kind of device the player used last: the interface shows the matching button names, and
/// may hide the mouse cursor when a gamepad is in use.
enum class InputDevice { KeyboardMouse, Gamepad };

/// One physical control. Its text form, used in the bindings file:
/// - `key:<name>`: a key by its position on the keyboard (SDL scancode name, as on a US QWERTY
///   keyboard: "key:Q" is the key labelled A on an AZERTY keyboard);
/// - `mouse:<button>`: left, right, middle, x1, x2;
/// - `wheel:up`, `wheel:down`: one notch of the wheel, a press and a release at once;
/// - `pad:<button>`: a, b, x, y (positions: a is the bottom button, Cross on a PlayStation pad),
///   back, start, leftstick, rightstick, leftshoulder, rightshoulder, dpup, dpdown, dpleft,
///   dpright... (SDL gamepad button names);
/// - `pad:<axis>+` or `pad:<axis>-`: a stick or trigger direction used as a button, down past
///   Input::axis_press_threshold: leftx-, righty+, lefttrigger+...
///
/// The device part is not case sensitive; mouse and wheel names neither.
struct InputSource {
    /// The kind of control, which says what `code` holds.
    enum class Kind { Key, MouseButton, Wheel, GamepadButton, GamepadAxis };
    Kind kind = Kind::Key;  ///< the kind of control
    int code = 0;       ///< SDL_Scancode, mouse button (SDL_BUTTON_*), SDL_GamepadButton, SDL_GamepadAxis
    int direction = 0;  ///< Wheel and GamepadAxis: +1 or -1

    /// Compared member by member: sources can be sorted, searched and used as map keys.
    ///@{
    bool operator==(const InputSource&) const = default;
    auto operator<=>(const InputSource&) const = default;
    ///@}
};

/// The source written as `text` (see InputSource for the forms), or nothing if it is not valid.
std::optional<InputSource> parse_input_source(std::string_view text);
/// The text form of a source (parse_input_source gives it back). Not for the player: see
/// input_source_label().
std::string input_source_text(const InputSource& source);
/// What to show the player: a key as printed on their keyboard (the A of an AZERTY keyboard for
/// "key:Q"), "Left click", "Wheel up", "Pad A"... in the language of `localization` (the engine's
/// `input-*` messages, `locales/<language>/moteur.ftl`; English without one). Gamepad buttons are
/// named as on an Xbox pad, by position.
std::string input_source_label(const InputSource& source, const Localization* localization = nullptr);

/// A direction of an axis action, for rebinding its keys (Input::set_axis_sources()).
enum class AxisDirection { Up, Down, Left, Right };

/// The state of a button action over one tick of the game.
struct ButtonState {
    bool down = false;  ///< held at the time of the tick
    int presses = 0;    ///< times it went down since the previous tick
    int releases = 0;   ///< times it went up since the previous tick
};

/// Everything the game's update can read from the Input in one tick: what a replay stores and gives
/// back.
struct InputFrame {
    std::vector<ButtonState> buttons;  ///< by action (axes: unused)
    std::vector<glm::vec2> axes;       ///< by action (buttons: unused)
    glm::vec2 pointer{-1.0f};          ///< window pixels; negative: outside the window
    /// The console's game commands run at the start of this tick (see Console): not the input's,
    /// recorded alongside it so that a replay runs them again.
    std::vector<std::string> commands;
};

/// A recording of what the game's update read, tick after tick: replayed, it gives the same run
/// again (tests, bug reports, captures with input). Actions are stored by name, so a recording
/// survives new actions being added to the game. Make one with Input::start_recording(), replay it
/// with Input::set_frame().
struct InputRecording {
    std::vector<std::string> actions;  ///< the action names; the frames' entries follow this order
    std::vector<bool> axis;          ///< by action: an axis rather than a button
    std::vector<InputFrame> frames;  ///< one per tick, from the first
};
/// The recording as compact JSON (version 1): only what is not idle is written for each frame.
std::string input_recording_to_json(const InputRecording& recording);
/// Reads what input_recording_to_json() wrote. Throws std::runtime_error naming `source_name` if
/// the text is not a recording (bad JSON, other version, an action index out of range).
InputRecording input_recording_from_json(std::string_view json_text, const std::string& source_name);

/// The player's input, as actions rather than keys.
///
/// The game declares its actions (add_button, add_axis), then loads the bindings: a JSON file with
/// one or several profiles (for instance "move with the mouse" and "move with the keys"), each
/// binding every action to sources, keyboard, mouse and gamepad together:
///
/// ```
/// {"version": 1, "profile": "click",
///  "profiles": {"click": {"label": "Déplacement au clic",
///                         "buttons": {"move_to": ["mouse:left"], "skill_1": ["key:Q", "pad:x"]},
///                         "axes": {"move": {"up": ["key:W"], "down": ["key:S"], "left": ["key:A"],
///                                           "right": ["key:D"], "stick": "left"}}}}}
/// ```
///
/// A second file, the player's (same format, usually a few entries), is applied on top: it may
/// choose the profile and change bindings; the rest keeps the defaults.
///
/// Timing: events arrive whenever the frame polls them; the game reads the actions at a fixed rate
/// (its update). A press is kept until a tick has seen it, and counted by that tick only: a short tap
/// between two ticks is never lost, and never seen twice when two ticks run in the same frame. The
/// Application calls end_tick() after each update.
///
/// Everything is called from the main thread. The reading functions taking an ActionId throw
/// std::out_of_range for an id that is not an action (except while muted, when they read idle).
class Input {
public:
    /// The strings of describe() and the labels (the Application gives its own); null: English.
    void set_localization(const Localization* localization) { localization_ = localization; }

    /// No action, no binding, no gamepad open (they are opened as SDL announces them).
    Input();
    /// Closes the gamepads it opened.
    ~Input();
    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;

    // --- Actions
    /// Declares a button action (pressed, held, released). Throws std::invalid_argument if an
    /// action already has this name. Its bindings come from the files already loaded, if any.
    ActionId add_button(const std::string& name);
    /// Declares an axis action, as add_button(). 2D: x to the right, y up (forward on screen).
    ActionId add_axis(const std::string& name);
    ActionId find(std::string_view name) const;   ///< kNoAction if unknown; O(actions)
    /// Forgets every action and binding (a scene declares its own, then loads its bindings), stops a
    /// replay and enables the real events again.
    void clear_actions();
    /// How many actions are declared: the ids are 0 to action_count() - 1.
    std::size_t action_count() const { return actions_.size(); }
    /// The name `action` was declared with. Throws std::out_of_range for an invalid id.
    const std::string& action_name(ActionId action) const;
    /// Whether `action` was declared with add_axis(). Throws std::out_of_range for an invalid id.
    bool is_axis(ActionId action) const;

    // --- Bindings
    /// Loads the defaults file (the format in the class description), replacing the profiles and
    /// forgetting the player's changes. Its `profile` key chooses the profile; without it, the first
    /// one by name. Throws std::runtime_error naming `source_name` for an invalid file (bad JSON,
    /// other version, no profile, unknown source, a `profile` not among the profiles). Actions the
    /// game did not declare are ignored with a warning in the log.
    void load_bindings(std::string_view json_text, const std::string& source_name);
    /// Applies the player's file on top of the defaults (load_bindings() first): each binding it
    /// gives replaces the default one, and its `profile` key chooses the profile. Throws
    /// std::runtime_error naming `source_name` for an invalid file (bad JSON, other version, unknown
    /// source); profiles that are not in the defaults, and undeclared actions, are ignored with a
    /// warning in the log.
    void apply_user_bindings(std::string_view json_text, const std::string& source_name);
    /// The player's file: the profile chosen, and the bindings changed from the defaults.
    std::string user_bindings_json() const;
    /// The names of the profiles of the defaults file, sorted.
    std::vector<std::string> profiles() const;
    /// What to show the player for `profile`: its `label`, or its name when it has none or is unknown.
    std::string profile_label(const std::string& profile) const;
    /// The name of the current profile; empty before load_bindings().
    const std::string& profile() const { return profile_; }
    /// Chooses the current profile, as the player's choice (user_bindings_json() saves it).
    void set_profile(const std::string& profile);  ///< throws std::invalid_argument if unknown
    /// The sources of a button action in the current profile (empty for an axis). Throws
    /// std::out_of_range for an invalid id.
    std::vector<InputSource> sources(ActionId action) const;
    /// "A / Manette X" (keyboard and mouse first, then gamepad); for an axis, its directions and its
    /// stick. With `device`, only that device's sources (what the player is using: see
    /// last_device()). "(aucune)" when nothing is bound.
    std::string describe(ActionId action, std::optional<InputDevice> device = std::nullopt) const;

    // --- Rebinding (the game's options menu)
    /// Binds a button action to `sources` in the current profile, as the player's change: saved by
    /// user_bindings_json(), the other actions and profiles untouched. Giving back the profile's
    /// default sources removes the change; an empty list leaves the action unbound. Several actions
    /// may share a source (the game decides whether to allow it: see actions_using()). Throws
    /// std::out_of_range for an invalid id, std::invalid_argument for an axis action.
    void set_sources(ActionId action, std::vector<InputSource> sources);
    /// The sources of one direction of an axis action in the current profile (its stick is not
    /// rebound). Throws std::out_of_range for an invalid id, std::invalid_argument for a button.
    std::vector<InputSource> axis_sources(ActionId axis, AxisDirection direction) const;
    /// Binds one direction of an axis action to `sources`, as set_sources() does for a button.
    void set_axis_sources(ActionId axis, AxisDirection direction, std::vector<InputSource> sources);
    /// Whether the player changed the binding of `action` in the current profile. Throws
    /// std::out_of_range for an invalid id.
    bool changed_by_player(ActionId action) const;
    /// Gives `action` its default binding back (current profile). Throws std::out_of_range for an
    /// invalid id.
    void reset_sources(ActionId action);
    /// Gives every action of the current profile its default binding back.
    void reset_all_sources();
    /// The actions bound to `source` in the current profile (axis directions included), in the
    /// order of declaration: what a menu shows before taking a source already in use.
    std::vector<ActionId> actions_using(const InputSource& source) const;

    /// Waits for the player to press what to bind: the next key, mouse button, wheel notch, gamepad
    /// button, or stick / trigger pushed past axis_press_threshold, is kept for take_captured(), and
    /// no action sees any input until then (everything held is released at once). The game decides
    /// what cancels (Escape, the gamepad's Start...) and whether a source of the other device is
    /// accepted.
    void begin_capture();
    /// Stops waiting; nothing is kept.
    void cancel_capture();
    /// Whether begin_capture() is still waiting.
    bool capturing() const { return capturing_; }
    /// The source pressed during the capture, once (the next call returns nothing).
    std::optional<InputSource> take_captured();

    // --- Events (the Application feeds them)
    /// Takes one SDL event: keys, mouse buttons and wheel, gamepad buttons and axes, focus loss.
    /// Gamepads are opened and closed here as they come and go, even while disabled. Other events
    /// are ignored, as are all of them while disabled or replaying.
    void process_event(const SDL_Event& event);
    /// The pointer, in window pixels (the Application converts from points); negative: outside.
    /// Ignored while disabled or replaying.
    void set_pointer(glm::vec2 pixels);
    /// Every source up (the window lost the focus: nothing stays held forever).
    void release_all();
    /// false: real events are ignored (runs that must not depend on the desk, and replays); what
    /// is held is released and the pointer goes outside. true does not end a replay (stop_replay()).
    void set_enabled(bool enabled);
    /// Whether real events are taken (set_enabled()).
    bool enabled() const { return enabled_; }
    /// After each update of the game: the presses and releases it has seen are consumed.
    void end_tick();
    /// true: every action reads as idle and the pointer as outside, without losing anything (the
    /// StateStack mutes the input for the states under the top one while they update).
    void set_muted(bool muted) { muted_ = muted; }
    /// Whether the input reads as idle (set_muted()).
    bool muted() const { return muted_; }

    // --- What the game reads in its update
    /// The button action is held at this tick (one of its sources is down).
    bool down(ActionId action) const;
    /// The button action went down at least once since the previous tick.
    bool pressed(ActionId action) const { return presses(action) > 0; }
    /// The button action went up at least once since the previous tick.
    bool released(ActionId action) const;
    /// How many times the button action went down since the previous tick (several quick taps, or
    /// wheel notches, between two ticks).
    int presses(ActionId action) const;
    /// The axis action: its keys (each direction 0 or 1) plus its stick (after the dead zone),
    /// length at most 1. Zero for a button action.
    glm::vec2 axis(ActionId action) const;
    /// The pointer in window pixels; negative (-1, -1) when outside the window, disabled or muted.
    glm::vec2 pointer() const { return muted_ ? glm::vec2(-1.0f) : frame_.pointer; }
    /// The device the player used last (a stick counts once past the dead zone).
    InputDevice last_device() const { return last_device_; }
    /// How many gamepads are connected and open.
    int gamepad_count() const { return static_cast<int>(gamepads_.size()); }

    // --- Replays
    /// The state the game's update would read now, axes included (to record it).
    InputFrame frame() const;
    /// Replaces the state the game's update reads (a recorded tick), and starts replaying: real
    /// events are ignored from then on, until stop_replay() (or clear_actions()), whatever
    /// set_enabled() says. Entries missing from `frame` are idle.
    void set_frame(const InputFrame& frame);
    /// Tick `tick` of `recording`, matched to this game's actions by name (actions the recording does
    /// not know stay idle). Past its end: nothing held, pointer outside.
    void set_frame(const InputRecording& recording, std::size_t tick);
    /// Ends a replay and goes back to live input: every action reads as idle (a button the replay
    /// held counts one release), the pointer is outside until the next set_pointer(), and real
    /// events are taken again (if enabled()). Nothing happens when not replaying.
    void stop_replay();
    /// Whether set_frame() has started a replay that stop_replay() has not ended.
    bool replaying() const { return replaying_; }
    /// Starts a recording of this game's actions (frames are added by the Application).
    InputRecording start_recording() const;

    /// Stick dead zone (radial, 0 to 1): below it a stick reads 0, above it the rest of the range is
    /// rescaled to 0..1.
    float dead_zone = 0.2f;
    /// The point (0 to 1) where a stick or trigger direction counts as a pressed button
    /// (`pad:<axis>+` sources).
    float axis_press_threshold = 0.5f;

private:
    struct AxisBinding {
        std::vector<InputSource> up, down, left, right;
        int stick = -1;  ///< 0 left stick, 1 right stick, -1 none
    };
    struct Profile {
        std::string label;
        std::map<std::string, std::vector<InputSource>> buttons;  ///< by action name
        std::map<std::string, AxisBinding> axes;
    };
    struct Action {
        std::string name;
        bool axis = false;
    };

    /// Recomputes which button actions are down after a source changed, counting the transitions.
    void source_changed(const InputSource& source, bool down);
    bool source_down(const InputSource& source) const;
    bool any_down(const std::vector<InputSource>& sources) const;
    glm::vec2 stick(int which) const;
    void refresh_buttons(std::optional<InputSource> pulsed = std::nullopt);
    /// Fills bound_buttons_ and bound_axes_ from the profile (the player's changes over the defaults).
    void resolve();
    void parse_profiles(std::string_view json_text, const std::string& source_name, bool user);
    /// During a capture: keeps the first source pressed; true when the event was taken.
    bool capture_event(const SDL_Event& event);
    /// The default binding of an action in the current profile.
    std::vector<InputSource> default_sources(const std::string& name) const;
    AxisBinding default_axis(const std::string& name) const;
    /// Removes the player's entry for the current profile when it holds no change any more.
    void prune_user_profile();

    std::vector<Action> actions_;
    std::map<std::string, Profile> defaults_;  ///< the profiles of the defaults file
    std::map<std::string, Profile> user_;      ///< the player's changes (same shape, sparse)
    std::string profile_;
    const Localization* localization_ = nullptr;
    std::string user_profile_;                 ///< the profile the player chose, if any

    std::vector<std::vector<InputSource>> bound_buttons_;  ///< by action, for the current profile
    std::vector<AxisBinding> bound_axes_;
    std::vector<InputSource> held_;            ///< sources down now (keys, buttons)
    std::map<int, float> gamepad_axes_;        ///< SDL_GamepadAxis -> -1..1
    std::vector<SDL_Gamepad*> gamepads_;
    InputFrame frame_;                         ///< what update() reads; pending presses accumulate here
    bool replaying_ = false;
    bool enabled_ = true;
    bool muted_ = false;
    bool capturing_ = false;
    std::optional<InputSource> captured_;
    InputDevice last_device_ = InputDevice::KeyboardMouse;
};

}  // namespace moteur
