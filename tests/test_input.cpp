#include <doctest/doctest.h>

#include <cmath>
#include <stdexcept>
#include <string>

#include "moteur/input.hpp"

namespace {

SDL_Event key(SDL_Scancode scancode, bool down, bool repeat = false) {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.scancode = scancode;
    event.key.down = down;
    event.key.repeat = repeat;
    return event;
}

SDL_Event mouse(Uint8 button, bool down) {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button = button;
    event.button.down = down;
    return event;
}

SDL_Event wheel(float y) {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_WHEEL;
    event.wheel.y = y;
    event.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
    return event;
}

SDL_Event pad_axis(SDL_GamepadAxis axis, float value) {
    SDL_Event event{};
    event.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
    event.gaxis.axis = static_cast<Uint8>(axis);
    event.gaxis.value = static_cast<Sint16>(value * 32767.0f);
    return event;
}

SDL_Event pad_button(SDL_GamepadButton button, bool down) {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
    event.gbutton.button = static_cast<Uint8>(button);
    event.gbutton.down = down;
    return event;
}

// Two profiles, like the demo's: moving with the mouse, or with the keys.
const char* kBindings = R"({
  "version": 1,
  "profile": "click",
  "profiles": {
    "click": {"label": "Au clic",
              "buttons": {"move_to": ["mouse:left"], "skill": ["key:Q", "key:1", "pad:a"], "zoom_in": ["wheel:up"],
                          "trigger": ["pad:righttrigger+"]},
              "axes": {"move": {"stick": "left"}}},
    "keys": {"label": "Au clavier",
             "buttons": {"skill": ["mouse:right"]},
             "axes": {"move": {"up": ["key:W"], "down": ["key:S"], "left": ["key:A"], "right": ["key:D"], "stick": "left"}}}
  }
})";

struct Fixture {
    moteur::Input input;
    moteur::ActionId move_to, skill, zoom_in, trigger, move;
    Fixture() {
        move_to = input.add_button("move_to");
        skill = input.add_button("skill");
        zoom_in = input.add_button("zoom_in");
        trigger = input.add_button("trigger");
        move = input.add_axis("move");
        input.load_bindings(kBindings, "test.json");
    }
};

}  // namespace

TEST_CASE("input sources have a text form that reads back") {
    for (const char* text : {"key:Q", "key:Space", "key:Left", "mouse:left", "mouse:x2", "wheel:up", "wheel:down", "pad:a",
                             "pad:rightshoulder", "pad:dpup", "pad:lefttrigger+", "pad:leftx-"}) {
        const auto source = moteur::parse_input_source(text);
        REQUIRE_MESSAGE(source.has_value(), text);
        CHECK(moteur::input_source_text(*source) == text);
    }
    CHECK(moteur::parse_input_source("key:q") == moteur::parse_input_source("key:Q"));  // names ignore case
    for (const char* text : {"Q", "key:NoSuchKey", "mouse:side", "wheel:left", "pad:z", "pad:leftx", "joystick:a", ""}) {
        CHECK_FALSE_MESSAGE(moteur::parse_input_source(text).has_value(), text);
    }
    CHECK(moteur::input_source_label(*moteur::parse_input_source("mouse:right")) == "Clic droit");
    CHECK(moteur::input_source_label(*moteur::parse_input_source("wheel:down")) == "Molette bas");
}

TEST_CASE("a tap between two ticks is seen once, by the next tick") {
    Fixture f;
    f.input.process_event(key(SDL_SCANCODE_Q, true));
    f.input.process_event(key(SDL_SCANCODE_Q, false));
    // The tick after the tap: pressed and released, no longer held.
    CHECK(f.input.pressed(f.skill));
    CHECK(f.input.released(f.skill));
    CHECK_FALSE(f.input.down(f.skill));
    f.input.end_tick();
    // A second tick in the same frame: already consumed.
    CHECK_FALSE(f.input.pressed(f.skill));
    CHECK_FALSE(f.input.released(f.skill));
}

TEST_CASE("a held button is pressed once and stays down across ticks") {
    Fixture f;
    f.input.process_event(mouse(SDL_BUTTON_LEFT, true));
    CHECK(f.input.pressed(f.move_to));
    CHECK(f.input.down(f.move_to));
    f.input.end_tick();
    CHECK_FALSE(f.input.pressed(f.move_to));
    CHECK(f.input.down(f.move_to));
    f.input.process_event(mouse(SDL_BUTTON_LEFT, false));
    CHECK(f.input.released(f.move_to));
    CHECK_FALSE(f.input.down(f.move_to));
}

TEST_CASE("presses wait for a tick when a frame has none, and are all counted") {
    Fixture f;
    // A fast screen: three frames of events before the logic ticks.
    for (int i = 0; i < 3; ++i) {
        f.input.process_event(key(SDL_SCANCODE_Q, true));
        f.input.process_event(key(SDL_SCANCODE_Q, false));
    }
    CHECK(f.input.presses(f.skill) == 3);
    f.input.end_tick();
    CHECK(f.input.presses(f.skill) == 0);
}

TEST_CASE("an action bound to several sources goes down once and up with the last one") {
    Fixture f;
    f.input.process_event(key(SDL_SCANCODE_Q, true));
    f.input.process_event(key(SDL_SCANCODE_1, true));  // a second key of the same action
    CHECK(f.input.presses(f.skill) == 1);
    f.input.process_event(key(SDL_SCANCODE_Q, false));
    CHECK(f.input.down(f.skill));
    CHECK_FALSE(f.input.released(f.skill));
    f.input.process_event(key(SDL_SCANCODE_1, false));
    CHECK_FALSE(f.input.down(f.skill));
    CHECK(f.input.released(f.skill));
}

TEST_CASE("the system's key repeat is not a press") {
    Fixture f;
    f.input.process_event(key(SDL_SCANCODE_Q, true));
    f.input.end_tick();
    f.input.process_event(key(SDL_SCANCODE_Q, true, true));
    f.input.process_event(key(SDL_SCANCODE_Q, true, true));
    CHECK_FALSE(f.input.pressed(f.skill));
    CHECK(f.input.down(f.skill));
}

TEST_CASE("each wheel notch is a press, never held") {
    Fixture f;
    f.input.process_event(wheel(1.0f));
    f.input.process_event(wheel(1.0f));
    f.input.process_event(wheel(-1.0f));  // the other direction: another action
    CHECK(f.input.presses(f.zoom_in) == 2);
    CHECK_FALSE(f.input.down(f.zoom_in));
}

TEST_CASE("the move axis: keys make unit diagonals, the stick has a dead zone") {
    Fixture f;
    f.input.set_profile("keys");
    f.input.process_event(key(SDL_SCANCODE_W, true));
    f.input.process_event(key(SDL_SCANCODE_D, true));
    glm::vec2 value = f.input.axis(f.move);
    CHECK(glm::length(value) == doctest::Approx(1.0f));
    CHECK(value.x == doctest::Approx(value.y));
    f.input.release_all();
    CHECK(f.input.axis(f.move) == glm::vec2(0.0f));

    f.input.process_event(pad_axis(SDL_GAMEPAD_AXIS_LEFTX, 0.15f));  // inside the dead zone (0.2)
    CHECK(f.input.axis(f.move) == glm::vec2(0.0f));
    f.input.process_event(pad_axis(SDL_GAMEPAD_AXIS_LEFTX, 0.0f));
    f.input.process_event(pad_axis(SDL_GAMEPAD_AXIS_LEFTY, -1.0f));  // pushed forward: SDL's y points down
    value = f.input.axis(f.move);
    CHECK(value.x == doctest::Approx(0.0f));
    CHECK(value.y == doctest::Approx(1.0f).epsilon(0.001));
    f.input.process_event(pad_axis(SDL_GAMEPAD_AXIS_LEFTY, -0.6f));  // (0.6 - 0.2) / 0.8
    CHECK(f.input.axis(f.move).y == doctest::Approx(0.5f).epsilon(0.01));
    CHECK(f.input.last_device() == moteur::InputDevice::Gamepad);
}

TEST_CASE("a trigger works as a button past its threshold, and gamepad buttons like keys") {
    Fixture f;
    f.input.process_event(pad_axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0.3f));
    CHECK_FALSE(f.input.down(f.trigger));
    f.input.process_event(pad_axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0.8f));
    CHECK(f.input.pressed(f.trigger));
    f.input.process_event(pad_button(SDL_GAMEPAD_BUTTON_SOUTH, true));
    CHECK(f.input.pressed(f.skill));
}

TEST_CASE("profiles change the bindings, and a change never leaves an action held") {
    Fixture f;
    CHECK(f.input.profile() == "click");
    CHECK(f.input.profiles() == std::vector<std::string>{"click", "keys"});
    CHECK(f.input.profile_label("keys") == "Au clavier");
    f.input.process_event(key(SDL_SCANCODE_Q, true));
    CHECK(f.input.down(f.skill));
    f.input.set_profile("keys");  // skill is now the right button: Q no longer holds it
    CHECK_FALSE(f.input.down(f.skill));
    CHECK(f.input.sources(f.move_to).empty());
    f.input.process_event(mouse(SDL_BUTTON_RIGHT, true));
    CHECK(f.input.down(f.skill));
    CHECK(f.input.describe(f.skill) == "Clic droit");
    CHECK_THROWS_AS(f.input.set_profile("nope"), std::invalid_argument);
}

TEST_CASE("the player's file chooses the profile and changes some bindings only") {
    Fixture f;
    f.input.apply_user_bindings(R"({"version": 1, "profile": "keys",
        "profiles": {"keys": {"buttons": {"skill": ["key:E"]}}}})", "user.json");
    CHECK(f.input.profile() == "keys");
    f.input.process_event(key(SDL_SCANCODE_E, true));
    CHECK(f.input.down(f.skill));
    f.input.process_event(key(SDL_SCANCODE_W, true));  // the axis keeps its default binding
    CHECK(f.input.axis(f.move).y == doctest::Approx(1.0f));

    // What is written back reads the same.
    const std::string saved = f.input.user_bindings_json();
    Fixture g;
    g.input.apply_user_bindings(saved, "saved.json");
    CHECK(g.input.profile() == "keys");
    CHECK(g.input.sources(g.skill) == f.input.sources(f.skill));

    // A profile the defaults do not have is ignored, not fatal.
    Fixture h;
    h.input.apply_user_bindings(R"({"version": 1, "profile": "gone"})", "old.json");
    CHECK(h.input.profile() == "click");
}

TEST_CASE("invalid bindings files are refused with their name") {
    moteur::Input input;
    input.add_button("skill");
    CHECK_THROWS_WITH_AS(input.load_bindings("{not json", "broken.json"), doctest::Contains("broken.json"), std::runtime_error);
    CHECK_THROWS_WITH_AS(input.load_bindings(R"({"version": 1, "profiles": {"a": {"buttons": {"skill": ["key:NoSuchKey"]}}}})", "keys.json"),
                         doctest::Contains("key:NoSuchKey"), std::runtime_error);
    CHECK_THROWS_WITH_AS(input.load_bindings(R"({"version": 2, "profiles": {}})", "future.json"),
                         doctest::Contains("version"), std::runtime_error);
    CHECK_THROWS_WITH_AS(input.load_bindings(R"({"version": 1, "profile": "b", "profiles": {"a": {}}})", "profile.json"),
                         doctest::Contains("'b'"), std::runtime_error);
    CHECK_THROWS_AS(input.add_button("skill"), std::invalid_argument);
}

TEST_CASE("disabled input ignores events; losing the focus releases everything") {
    Fixture f;
    f.input.set_enabled(false);
    f.input.process_event(key(SDL_SCANCODE_Q, true));
    f.input.set_pointer({10.0f, 20.0f});
    CHECK_FALSE(f.input.down(f.skill));
    CHECK(f.input.pointer().x < 0.0f);
    f.input.set_enabled(true);
    f.input.process_event(key(SDL_SCANCODE_Q, true));
    SDL_Event focus{};
    focus.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    f.input.process_event(focus);
    CHECK_FALSE(f.input.down(f.skill));
}

TEST_CASE("a recording replays by action name, whatever the order of the actions") {
    Fixture f;
    moteur::InputRecording recording = f.input.start_recording();
    f.input.set_pointer({100.0f, 50.0f});
    f.input.process_event(key(SDL_SCANCODE_Q, true));
    f.input.process_event(pad_axis(SDL_GAMEPAD_AXIS_LEFTX, 1.0f));
    recording.frames.push_back(f.input.frame());
    f.input.end_tick();
    recording.frames.push_back(f.input.frame());

    const moteur::InputRecording read =
        moteur::input_recording_from_json(moteur::input_recording_to_json(recording), "run.json");
    REQUIRE(read.frames.size() == 2);

    // Another game version: the actions in another order, one more, one less.
    moteur::Input replay;
    const moteur::ActionId extra = replay.add_button("new_action");
    const moteur::ActionId move = replay.add_axis("move");
    const moteur::ActionId skill = replay.add_button("skill");
    replay.set_frame(read, 0);
    CHECK(replay.pressed(skill));
    CHECK(replay.down(skill));
    CHECK_FALSE(replay.down(extra));
    CHECK(replay.axis(move).x == doctest::Approx(1.0f));
    CHECK(replay.pointer() == glm::vec2(100.0f, 50.0f));
    replay.set_frame(read, 1);
    CHECK_FALSE(replay.pressed(skill));
    CHECK(replay.down(skill));
    replay.set_frame(read, 5);  // past the end: idle
    CHECK_FALSE(replay.down(skill));
    CHECK(replay.pointer().x < 0.0f);
    // Real events do not disturb a replay.
    replay.process_event(key(SDL_SCANCODE_Q, false));
    CHECK_FALSE(replay.released(skill));

    CHECK_THROWS_WITH_AS(moteur::input_recording_from_json("[]", "bad.json"), doctest::Contains("bad.json"), std::runtime_error);
}
