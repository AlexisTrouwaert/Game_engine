#include "moteur/input.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace moteur {

namespace {

struct MouseName {
    const char* text;
    int button;
    const char* label;
};
constexpr MouseName kMouseButtons[] = {
    {"left", SDL_BUTTON_LEFT, "Clic gauche"},    {"right", SDL_BUTTON_RIGHT, "Clic droit"},
    {"middle", SDL_BUTTON_MIDDLE, "Clic milieu"}, {"x1", SDL_BUTTON_X1, "Souris 4"},
    {"x2", SDL_BUTTON_X2, "Souris 5"},
};

std::string lower(std::string_view text) {
    std::string result(text);
    for (char& c : result) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return result;
}

std::vector<InputSource> parse_sources(const nlohmann::json& list, const std::string& where) {
    if (!list.is_array()) {
        throw std::runtime_error(where + ": a list of sources is expected");
    }
    std::vector<InputSource> sources;
    for (const nlohmann::json& item : list) {
        const std::string text = item.get<std::string>();
        const std::optional<InputSource> source = parse_input_source(text);
        if (!source) {
            throw std::runtime_error(where + ": unknown source '" + text + "'");
        }
        sources.push_back(*source);
    }
    return sources;
}

nlohmann::json sources_json(const std::vector<InputSource>& sources) {
    nlohmann::json list = nlohmann::json::array();
    for (const InputSource& source : sources) {
        list.push_back(input_source_text(source));
    }
    return list;
}

// A stick's position, without its dead zone: 0 inside it, then rescaled so that the edge of the dead
// zone is 0 and the full tilt is 1 (no jump when leaving it).
glm::vec2 apply_dead_zone(glm::vec2 value, float dead_zone) {
    const float length = glm::length(value);
    if (length <= dead_zone || length <= 0.0f) {
        return glm::vec2(0.0f);
    }
    const float scaled = std::min(1.0f, (length - dead_zone) / (1.0f - dead_zone));
    return value / length * scaled;
}

}  // namespace

std::optional<InputSource> parse_input_source(std::string_view text) {
    const std::size_t colon = text.find(':');
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string device = lower(text.substr(0, colon));
    const std::string name(text.substr(colon + 1));
    InputSource source;
    if (device == "key") {
        const SDL_Scancode scancode = SDL_GetScancodeFromName(name.c_str());
        if (scancode == SDL_SCANCODE_UNKNOWN) {
            return std::nullopt;
        }
        source.kind = InputSource::Kind::Key;
        source.code = scancode;
        return source;
    }
    if (device == "mouse") {
        for (const MouseName& mouse : kMouseButtons) {
            if (lower(name) == mouse.text) {
                source.kind = InputSource::Kind::MouseButton;
                source.code = mouse.button;
                return source;
            }
        }
        return std::nullopt;
    }
    if (device == "wheel") {
        source.kind = InputSource::Kind::Wheel;
        if (lower(name) == "up") {
            source.direction = 1;
        } else if (lower(name) == "down") {
            source.direction = -1;
        } else {
            return std::nullopt;
        }
        return source;
    }
    if (device == "pad") {
        const SDL_GamepadButton button = SDL_GetGamepadButtonFromString(name.c_str());
        if (button != SDL_GAMEPAD_BUTTON_INVALID) {
            source.kind = InputSource::Kind::GamepadButton;
            source.code = button;
            return source;
        }
        if (name.size() > 1 && (name.back() == '+' || name.back() == '-')) {
            const std::string axis_name = name.substr(0, name.size() - 1);
            const SDL_GamepadAxis axis = SDL_GetGamepadAxisFromString(axis_name.c_str());
            if (axis != SDL_GAMEPAD_AXIS_INVALID) {
                source.kind = InputSource::Kind::GamepadAxis;
                source.code = axis;
                source.direction = name.back() == '+' ? 1 : -1;
                return source;
            }
        }
        return std::nullopt;
    }
    return std::nullopt;
}

std::string input_source_text(const InputSource& source) {
    switch (source.kind) {
        case InputSource::Kind::Key:
            return std::string("key:") + SDL_GetScancodeName(static_cast<SDL_Scancode>(source.code));
        case InputSource::Kind::MouseButton:
            for (const MouseName& mouse : kMouseButtons) {
                if (mouse.button == source.code) {
                    return std::string("mouse:") + mouse.text;
                }
            }
            return "mouse:?";
        case InputSource::Kind::Wheel:
            return source.direction > 0 ? "wheel:up" : "wheel:down";
        case InputSource::Kind::GamepadButton:
            return std::string("pad:") + SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(source.code));
        case InputSource::Kind::GamepadAxis:
            return std::string("pad:") + SDL_GetGamepadStringForAxis(static_cast<SDL_GamepadAxis>(source.code)) +
                   (source.direction > 0 ? "+" : "-");
    }
    return "?";
}

std::string input_source_label(const InputSource& source) {
    switch (source.kind) {
        case InputSource::Kind::Key: {
            // The key as printed on the player's keyboard, whatever its layout.
            const auto scancode = static_cast<SDL_Scancode>(source.code);
            const SDL_Keycode key = SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, false);
            const char* name = key != SDLK_UNKNOWN ? SDL_GetKeyName(key) : "";
            return name[0] != '\0' ? name : SDL_GetScancodeName(scancode);
        }
        case InputSource::Kind::MouseButton:
            for (const MouseName& mouse : kMouseButtons) {
                if (mouse.button == source.code) {
                    return mouse.label;
                }
            }
            return "Souris ?";
        case InputSource::Kind::Wheel:
            return source.direction > 0 ? "Molette haut" : "Molette bas";
        case InputSource::Kind::GamepadButton:
            switch (static_cast<SDL_GamepadButton>(source.code)) {
                // Names of an Xbox pad, the most common; by position (A is the bottom button).
                case SDL_GAMEPAD_BUTTON_SOUTH: return "Manette A";
                case SDL_GAMEPAD_BUTTON_EAST: return "Manette B";
                case SDL_GAMEPAD_BUTTON_WEST: return "Manette X";
                case SDL_GAMEPAD_BUTTON_NORTH: return "Manette Y";
                case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return "Manette LB";
                case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return "Manette RB";
                case SDL_GAMEPAD_BUTTON_LEFT_STICK: return "Manette L3";
                case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return "Manette R3";
                case SDL_GAMEPAD_BUTTON_DPAD_UP: return "Croix haut";
                case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return "Croix bas";
                case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return "Croix gauche";
                case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return "Croix droite";
                case SDL_GAMEPAD_BUTTON_START: return "Manette Start";
                case SDL_GAMEPAD_BUTTON_BACK: return "Manette Select";
                default:
                    return std::string("Manette ") + SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(source.code));
            }
        case InputSource::Kind::GamepadAxis:
            switch (static_cast<SDL_GamepadAxis>(source.code)) {
                case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: return "Manette LT";
                case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return "Manette RT";
                default:
                    return std::string("Manette ") + SDL_GetGamepadStringForAxis(static_cast<SDL_GamepadAxis>(source.code)) +
                           (source.direction > 0 ? "+" : "-");
            }
    }
    return "?";
}

Input::Input() = default;

Input::~Input() {
    for (SDL_Gamepad* gamepad : gamepads_) {
        SDL_CloseGamepad(gamepad);
    }
}

// --- Actions

ActionId Input::add_button(const std::string& name) {
    if (find(name) != kNoAction) {
        throw std::invalid_argument("Input: action '" + name + "' declared twice");
    }
    actions_.push_back({name, false});
    frame_.buttons.resize(actions_.size());
    frame_.axes.resize(actions_.size());
    resolve();
    return static_cast<ActionId>(actions_.size() - 1);
}

ActionId Input::add_axis(const std::string& name) {
    if (find(name) != kNoAction) {
        throw std::invalid_argument("Input: action '" + name + "' declared twice");
    }
    actions_.push_back({name, true});
    frame_.buttons.resize(actions_.size());
    frame_.axes.resize(actions_.size());
    resolve();
    return static_cast<ActionId>(actions_.size() - 1);
}

ActionId Input::find(std::string_view name) const {
    for (std::size_t i = 0; i < actions_.size(); ++i) {
        if (actions_[i].name == name) {
            return static_cast<ActionId>(i);
        }
    }
    return kNoAction;
}

void Input::clear_actions() {
    actions_.clear();
    defaults_.clear();
    user_.clear();
    profile_.clear();
    user_profile_.clear();
    bound_buttons_.clear();
    bound_axes_.clear();
    frame_.buttons.clear();
    frame_.axes.clear();
    replaying_ = false;
    enabled_ = true;
}

const std::string& Input::action_name(ActionId action) const {
    return actions_.at(static_cast<std::size_t>(action)).name;
}

bool Input::is_axis(ActionId action) const {
    return actions_.at(static_cast<std::size_t>(action)).axis;
}

// --- Bindings

void Input::parse_profiles(std::string_view json_text, const std::string& source_name, bool user) {
    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(json_text);
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error("Bindings '" + source_name + "': invalid JSON: " + e.what());
    }
    try {
        if (doc.at("version").get<int>() != 1) {
            throw std::runtime_error("format version " + doc.at("version").dump() + " is not supported (expected 1)");
        }
        std::map<std::string, Profile> profiles;
        if (doc.contains("profiles")) {
            for (const auto& [name, body] : doc.at("profiles").items()) {
                Profile profile;
                profile.label = body.value("label", std::string());
                if (body.contains("buttons")) {
                    for (const auto& [action, list] : body.at("buttons").items()) {
                        profile.buttons[action] = parse_sources(list, "profile '" + name + "', action '" + action + "'");
                    }
                }
                if (body.contains("axes")) {
                    for (const auto& [action, axis] : body.at("axes").items()) {
                        const std::string where = "profile '" + name + "', axis '" + action + "'";
                        AxisBinding binding;
                        binding.up = parse_sources(axis.value("up", nlohmann::json::array()), where);
                        binding.down = parse_sources(axis.value("down", nlohmann::json::array()), where);
                        binding.left = parse_sources(axis.value("left", nlohmann::json::array()), where);
                        binding.right = parse_sources(axis.value("right", nlohmann::json::array()), where);
                        const std::string stick = axis.value("stick", std::string());
                        if (stick == "left") {
                            binding.stick = 0;
                        } else if (stick == "right") {
                            binding.stick = 1;
                        } else if (!stick.empty()) {
                            throw std::runtime_error(where + ": stick must be \"left\" or \"right\"");
                        }
                        profile.axes[action] = binding;
                    }
                }
                profiles[name] = std::move(profile);
            }
        }
        const std::string chosen = doc.value("profile", std::string());
        if (user) {
            for (auto& [name, profile] : profiles) {
                if (!defaults_.contains(name)) {
                    SDL_Log("Bindings '%s': profile '%s' is not in the defaults, ignored", source_name.c_str(), name.c_str());
                    continue;
                }
                Profile& merged = user_[name];
                for (auto& [action, sources] : profile.buttons) {
                    merged.buttons[action] = std::move(sources);
                }
                for (auto& [action, binding] : profile.axes) {
                    merged.axes[action] = std::move(binding);
                }
            }
            if (!chosen.empty()) {
                if (defaults_.contains(chosen)) {
                    user_profile_ = chosen;
                    profile_ = chosen;
                } else {
                    SDL_Log("Bindings '%s': profile '%s' does not exist, ignored", source_name.c_str(), chosen.c_str());
                }
            }
        } else {
            if (profiles.empty()) {
                throw std::runtime_error("no profile");
            }
            if (!chosen.empty() && !profiles.contains(chosen)) {
                throw std::runtime_error("the profile '" + chosen + "' is not among the profiles");
            }
            defaults_ = std::move(profiles);
            user_.clear();
            user_profile_.clear();
            profile_ = chosen.empty() ? defaults_.begin()->first : chosen;
        }
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error("Bindings '" + source_name + "': " + e.what());
    } catch (const std::runtime_error& e) {
        const std::string what = e.what();
        if (what.rfind("Bindings '", 0) == 0) {
            throw;
        }
        throw std::runtime_error("Bindings '" + source_name + "': " + what);
    }
    // Names the game does not know: probably a typo, or a binding for another version.
    for (const auto& [name, profile] : user ? user_ : defaults_) {
        for (const auto& [action, sources] : profile.buttons) {
            if (find(action) == kNoAction || is_axis(find(action))) {
                SDL_Log("Bindings '%s': profile '%s': '%s' is not a button action of the game", source_name.c_str(),
                        name.c_str(), action.c_str());
            }
        }
        for (const auto& [action, binding] : profile.axes) {
            if (find(action) == kNoAction || !is_axis(find(action))) {
                SDL_Log("Bindings '%s': profile '%s': '%s' is not an axis action of the game", source_name.c_str(),
                        name.c_str(), action.c_str());
            }
        }
    }
    resolve();
}

void Input::load_bindings(std::string_view json_text, const std::string& source_name) {
    parse_profiles(json_text, source_name, false);
}

void Input::apply_user_bindings(std::string_view json_text, const std::string& source_name) {
    parse_profiles(json_text, source_name, true);
}

std::string Input::user_bindings_json() const {
    nlohmann::json doc;
    doc["version"] = 1;
    if (!user_profile_.empty()) {
        doc["profile"] = user_profile_;
    }
    if (!user_.empty()) {
        nlohmann::json profiles = nlohmann::json::object();
        for (const auto& [name, profile] : user_) {
            nlohmann::json body = nlohmann::json::object();
            for (const auto& [action, sources] : profile.buttons) {
                body["buttons"][action] = sources_json(sources);
            }
            for (const auto& [action, binding] : profile.axes) {
                nlohmann::json axis = {{"up", sources_json(binding.up)},
                                       {"down", sources_json(binding.down)},
                                       {"left", sources_json(binding.left)},
                                       {"right", sources_json(binding.right)}};
                if (binding.stick >= 0) {
                    axis["stick"] = binding.stick == 0 ? "left" : "right";
                }
                body["axes"][action] = axis;
            }
            profiles[name] = body;
        }
        doc["profiles"] = profiles;
    }
    return doc.dump(2) + "\n";
}

std::vector<std::string> Input::profiles() const {
    std::vector<std::string> names;
    for (const auto& [name, profile] : defaults_) {
        names.push_back(name);
    }
    return names;
}

std::string Input::profile_label(const std::string& profile) const {
    const auto found = defaults_.find(profile);
    if (found == defaults_.end()) {
        return profile;
    }
    return found->second.label.empty() ? profile : found->second.label;
}

void Input::set_profile(const std::string& profile) {
    if (!defaults_.contains(profile)) {
        throw std::invalid_argument("Input: no profile '" + profile + "'");
    }
    profile_ = profile;
    user_profile_ = profile;
    resolve();
}

std::vector<InputSource> Input::sources(ActionId action) const {
    return bound_buttons_.at(static_cast<std::size_t>(action));
}

std::string Input::describe(ActionId action, std::optional<InputDevice> device) const {
    // Keyboard and mouse first, then the gamepad.
    std::vector<std::string> keyboard;
    std::vector<std::string> gamepad;
    const auto add = [&](const InputSource& source) {
        const bool pad = source.kind == InputSource::Kind::GamepadButton || source.kind == InputSource::Kind::GamepadAxis;
        (pad ? gamepad : keyboard).push_back(input_source_label(source));
    };
    if (is_axis(action)) {
        const AxisBinding& binding = bound_axes_.at(static_cast<std::size_t>(action));
        for (const auto* direction : {&binding.up, &binding.left, &binding.down, &binding.right}) {
            for (const InputSource& source : *direction) {
                add(source);
            }
        }
        if (binding.stick >= 0) {
            gamepad.push_back(binding.stick == 0 ? "Stick gauche" : "Stick droit");
        }
    } else {
        for (const InputSource& source : bound_buttons_.at(static_cast<std::size_t>(action))) {
            add(source);
        }
    }
    if (device == InputDevice::KeyboardMouse) {
        gamepad.clear();
    } else if (device == InputDevice::Gamepad) {
        keyboard.clear();
    }
    std::string text;
    for (const std::vector<std::string>* group : {&keyboard, &gamepad}) {
        std::string part;
        for (const std::string& label : *group) {
            part += (part.empty() ? "" : " ") + label;
        }
        if (!part.empty()) {
            text += (text.empty() ? "" : " / ") + part;
        }
    }
    return text.empty() ? "(aucune)" : text;
}

void Input::resolve() {
    bound_buttons_.assign(actions_.size(), {});
    bound_axes_.assign(actions_.size(), {});
    const auto defaults = defaults_.find(profile_);
    const auto user = user_.find(profile_);
    for (std::size_t i = 0; i < actions_.size(); ++i) {
        const std::string& name = actions_[i].name;
        if (actions_[i].axis) {
            if (user != user_.end() && user->second.axes.contains(name)) {
                bound_axes_[i] = user->second.axes.at(name);
            } else if (defaults != defaults_.end() && defaults->second.axes.contains(name)) {
                bound_axes_[i] = defaults->second.axes.at(name);
            }
        } else {
            if (user != user_.end() && user->second.buttons.contains(name)) {
                bound_buttons_[i] = user->second.buttons.at(name);
            } else if (defaults != defaults_.end() && defaults->second.buttons.contains(name)) {
                bound_buttons_[i] = defaults->second.buttons.at(name);
            }
        }
    }
    // A change of binding must not leave an action held by a key that no longer drives it.
    refresh_buttons();
}

// --- Events

bool Input::source_down(const InputSource& source) const {
    if (source.kind == InputSource::Kind::GamepadAxis) {
        const auto found = gamepad_axes_.find(source.code);
        return found != gamepad_axes_.end() && found->second * static_cast<float>(source.direction) >= axis_press_threshold;
    }
    return std::find(held_.begin(), held_.end(), source) != held_.end();
}

bool Input::any_down(const std::vector<InputSource>& sources) const {
    return std::any_of(sources.begin(), sources.end(), [this](const InputSource& source) { return source_down(source); });
}

void Input::refresh_buttons(std::optional<InputSource> pulsed) {
    if (replaying_) {
        return;
    }
    for (std::size_t i = 0; i < actions_.size() && i < bound_buttons_.size(); ++i) {
        if (actions_[i].axis) {
            continue;
        }
        const std::vector<InputSource>& sources = bound_buttons_[i];
        ButtonState& state = frame_.buttons[i];
        const bool now = any_down(sources);
        if (pulsed && std::find(sources.begin(), sources.end(), *pulsed) != sources.end() && !now) {
            // A wheel notch: down and up at once, never held.
            ++state.presses;
            ++state.releases;
            continue;
        }
        if (now && !state.down) {
            ++state.presses;
        } else if (!now && state.down) {
            ++state.releases;
        }
        state.down = now;
    }
}

void Input::source_changed(const InputSource& source, bool down) {
    const auto found = std::find(held_.begin(), held_.end(), source);
    if (down && found == held_.end()) {
        held_.push_back(source);
    } else if (!down && found != held_.end()) {
        held_.erase(found);
    }
    refresh_buttons();
}

void Input::process_event(const SDL_Event& event) {
    // Gamepads are opened and closed whatever happens: they must be there when input comes back.
    if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
        if (SDL_Gamepad* gamepad = SDL_OpenGamepad(event.gdevice.which)) {
            gamepads_.push_back(gamepad);
            SDL_Log("Input: gamepad '%s' connected", SDL_GetGamepadName(gamepad));
        }
        return;
    }
    if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
        for (auto it = gamepads_.begin(); it != gamepads_.end(); ++it) {
            if (SDL_GetGamepadID(*it) == event.gdevice.which) {
                SDL_Log("Input: gamepad '%s' disconnected", SDL_GetGamepadName(*it));
                SDL_CloseGamepad(*it);
                gamepads_.erase(it);
                break;
            }
        }
        if (gamepads_.empty()) {
            // Nothing held by a gamepad that is gone.
            std::erase_if(held_, [](const InputSource& source) { return source.kind == InputSource::Kind::GamepadButton; });
            gamepad_axes_.clear();
            refresh_buttons();
        }
        return;
    }
    if (!enabled_ || replaying_) {
        return;
    }
    switch (event.type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            last_device_ = InputDevice::KeyboardMouse;
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.repeat) {
                break;  // the system's key repeat is not a new press
            }
            source_changed({InputSource::Kind::Key, static_cast<int>(event.key.scancode), 0}, event.type == SDL_EVENT_KEY_DOWN);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            last_device_ = InputDevice::KeyboardMouse;
            source_changed({InputSource::Kind::MouseButton, event.button.button, 0}, event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
            break;
        case SDL_EVENT_MOUSE_WHEEL: {
            last_device_ = InputDevice::KeyboardMouse;
            float y = event.wheel.y;
            if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                y = -y;
            }
            if (y != 0.0f) {
                refresh_buttons(InputSource{InputSource::Kind::Wheel, 0, y > 0.0f ? 1 : -1});
            }
            break;
        }
        case SDL_EVENT_MOUSE_MOTION:
            last_device_ = InputDevice::KeyboardMouse;
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            last_device_ = InputDevice::Gamepad;
            source_changed({InputSource::Kind::GamepadButton, event.gbutton.button, 0},
                           event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
            const float value = std::clamp(static_cast<float>(event.gaxis.value) / 32767.0f, -1.0f, 1.0f);
            gamepad_axes_[event.gaxis.axis] = value;
            if (std::abs(value) > dead_zone) {
                last_device_ = InputDevice::Gamepad;
            }
            refresh_buttons();
            break;
        }
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            release_all();
            break;
        default:
            break;
    }
}

void Input::set_pointer(glm::vec2 pixels) {
    if (enabled_ && !replaying_) {
        frame_.pointer = pixels;
    }
}

void Input::release_all() {
    held_.clear();
    gamepad_axes_.clear();
    refresh_buttons();
}

void Input::set_enabled(bool enabled) {
    enabled_ = enabled;
    if (!enabled) {
        release_all();
        frame_.pointer = glm::vec2(-1.0f);
    }
}

void Input::end_tick() {
    for (ButtonState& state : frame_.buttons) {
        state.presses = 0;
        state.releases = 0;
    }
}

// --- Reading

bool Input::down(ActionId action) const {
    if (muted_) {
        return false;
    }
    return frame_.buttons.at(static_cast<std::size_t>(action)).down;
}

bool Input::released(ActionId action) const {
    if (muted_) {
        return false;
    }
    return frame_.buttons.at(static_cast<std::size_t>(action)).releases > 0;
}

int Input::presses(ActionId action) const {
    if (muted_) {
        return 0;
    }
    return frame_.buttons.at(static_cast<std::size_t>(action)).presses;
}

glm::vec2 Input::stick(int which) const {
    const auto value = [this](SDL_GamepadAxis axis) {
        const auto found = gamepad_axes_.find(axis);
        return found != gamepad_axes_.end() ? found->second : 0.0f;
    };
    const glm::vec2 raw = which == 0 ? glm::vec2(value(SDL_GAMEPAD_AXIS_LEFTX), -value(SDL_GAMEPAD_AXIS_LEFTY))
                                     : glm::vec2(value(SDL_GAMEPAD_AXIS_RIGHTX), -value(SDL_GAMEPAD_AXIS_RIGHTY));
    return apply_dead_zone(raw, dead_zone);  // y up, as the action (SDL's stick y points down)
}

glm::vec2 Input::axis(ActionId action) const {
    if (muted_) {
        return glm::vec2(0.0f);
    }
    const auto index = static_cast<std::size_t>(action);
    if (replaying_) {
        return frame_.axes.at(index);
    }
    const AxisBinding& binding = bound_axes_.at(index);
    glm::vec2 value(0.0f);
    value.x = (any_down(binding.right) ? 1.0f : 0.0f) - (any_down(binding.left) ? 1.0f : 0.0f);
    value.y = (any_down(binding.up) ? 1.0f : 0.0f) - (any_down(binding.down) ? 1.0f : 0.0f);
    if (binding.stick >= 0) {
        value += stick(binding.stick);
    }
    const float length = glm::length(value);
    return length > 1.0f ? value / length : value;  // diagonals are not faster
}

// --- Replays

InputFrame Input::frame() const {
    InputFrame frame = frame_;
    for (std::size_t i = 0; i < actions_.size(); ++i) {
        frame.axes[i] = actions_[i].axis ? axis(static_cast<ActionId>(i)) : glm::vec2(0.0f);
    }
    return frame;
}

void Input::set_frame(const InputFrame& frame) {
    replaying_ = true;
    frame_ = frame;
    frame_.buttons.resize(actions_.size());
    frame_.axes.resize(actions_.size());
}

void Input::set_frame(const InputRecording& recording, std::size_t tick) {
    InputFrame frame;
    frame.buttons.resize(actions_.size());
    frame.axes.resize(actions_.size());
    if (tick < recording.frames.size()) {
        const InputFrame& recorded = recording.frames[tick];
        frame.pointer = recorded.pointer;
        for (std::size_t r = 0; r < recording.actions.size(); ++r) {
            const ActionId action = find(recording.actions[r]);
            if (action == kNoAction) {
                continue;
            }
            const auto a = static_cast<std::size_t>(action);
            if (r < recorded.buttons.size()) {
                frame.buttons[a] = recorded.buttons[r];
            }
            if (r < recorded.axes.size()) {
                frame.axes[a] = recorded.axes[r];
            }
        }
    }
    set_frame(frame);
}

InputRecording Input::start_recording() const {
    InputRecording recording;
    for (const Action& action : actions_) {
        recording.actions.push_back(action.name);
        recording.axis.push_back(action.axis);
    }
    return recording;
}

// Frames store only what is not idle: [action, down, presses, releases] for buttons, [action, x, y]
// for axes, and the pointer.
std::string input_recording_to_json(const InputRecording& recording) {
    nlohmann::json doc;
    doc["version"] = 1;
    doc["actions"] = recording.actions;
    nlohmann::json axes = nlohmann::json::array();
    for (bool axis : recording.axis) {
        axes.push_back(axis);
    }
    doc["axes"] = axes;
    nlohmann::json frames = nlohmann::json::array();
    for (const InputFrame& frame : recording.frames) {
        nlohmann::json item;
        item["p"] = {frame.pointer.x, frame.pointer.y};
        nlohmann::json buttons = nlohmann::json::array();
        for (std::size_t i = 0; i < frame.buttons.size(); ++i) {
            const ButtonState& state = frame.buttons[i];
            if (state.down || state.presses > 0 || state.releases > 0) {
                buttons.push_back({i, state.down ? 1 : 0, state.presses, state.releases});
            }
        }
        if (!buttons.empty()) {
            item["b"] = buttons;
        }
        nlohmann::json values = nlohmann::json::array();
        for (std::size_t i = 0; i < frame.axes.size(); ++i) {
            if (frame.axes[i] != glm::vec2(0.0f)) {
                values.push_back({i, frame.axes[i].x, frame.axes[i].y});
            }
        }
        if (!values.empty()) {
            item["a"] = values;
        }
        frames.push_back(item);
    }
    doc["frames"] = frames;
    return doc.dump() + "\n";
}

InputRecording input_recording_from_json(std::string_view json_text, const std::string& source_name) {
    try {
        const nlohmann::json doc = nlohmann::json::parse(json_text);
        if (doc.at("version").get<int>() != 1) {
            throw std::runtime_error("format version " + doc.at("version").dump() + " is not supported (expected 1)");
        }
        InputRecording recording;
        recording.actions = doc.at("actions").get<std::vector<std::string>>();
        recording.axis = doc.at("axes").get<std::vector<bool>>();
        const std::size_t count = recording.actions.size();
        for (const nlohmann::json& item : doc.at("frames")) {
            InputFrame frame;
            frame.buttons.resize(count);
            frame.axes.resize(count);
            frame.pointer = {item.at("p").at(0).get<float>(), item.at("p").at(1).get<float>()};
            for (const nlohmann::json& b : item.value("b", nlohmann::json::array())) {
                const std::size_t i = b.at(0).get<std::size_t>();
                if (i >= count) {
                    throw std::runtime_error("an action index is out of range");
                }
                frame.buttons[i] = {b.at(1).get<int>() != 0, b.at(2).get<int>(), b.at(3).get<int>()};
            }
            for (const nlohmann::json& a : item.value("a", nlohmann::json::array())) {
                const std::size_t i = a.at(0).get<std::size_t>();
                if (i >= count) {
                    throw std::runtime_error("an action index is out of range");
                }
                frame.axes[i] = {a.at(1).get<float>(), a.at(2).get<float>()};
            }
            recording.frames.push_back(std::move(frame));
        }
        return recording;
    } catch (const std::exception& e) {
        throw std::runtime_error("Input recording '" + source_name + "': " + e.what());
    }
}

}  // namespace moteur
