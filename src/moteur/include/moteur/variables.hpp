#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace moteur {

// Variables to tune the game while it runs (milestone 7, part 3): named with dots by system
// ("fog.enabled", "debug.paths", "time.scale"), typed, bounded, with a default and a help line. Each
// system declares its own once and keeps the handle (a stable reference), then reads it where it
// needs it; the console sets them by name ("set fog.enabled 0").
class Variable {
public:
    enum class Type { Bool, Int, Float, Text };
    enum Flags : std::uint32_t {
        None = 0,
        Archive = 1,  // kept in the player's preferences (variables.cfg)
        Logic = 2,    // changes the game's logic: set through a game command (queued, replayed)
    };

    const std::string& name() const { return name_; }
    const std::string& help() const { return help_; }
    Type type() const { return type_; }
    std::uint32_t flags() const { return flags_; }

    bool as_bool() const { return number_ != 0.0; }
    int as_int() const { return static_cast<int>(number_); }
    float as_float() const { return static_cast<float>(number_); }
    const std::string& as_text() const { return text_; }
    // The value as the console shows it ("1", "0.5", "texte").
    std::string value_text() const;
    std::string default_text() const;
    bool is_default() const;
    // Grows by one at each change: a system can notice it without being called.
    std::uint64_t revision() const { return revision_; }

    // Parses and sets. False, with the reason in `error`, if the text is not a valid value.
    bool set_text(std::string_view text, std::string& error);
    void set_bool(bool value) { set_number(value ? 1.0 : 0.0); }
    void set_int(int value) { set_number(value); }
    void set_float(float value) { set_number(value); }
    void reset();

private:
    friend class Variables;
    void set_number(double value);

    std::string name_;
    std::string help_;
    Type type_ = Type::Bool;
    std::uint32_t flags_ = None;
    double number_ = 0.0;
    double default_number_ = 0.0;
    double min_ = 0.0;
    double max_ = 0.0;
    std::string text_;
    std::string default_text_;
    std::uint64_t revision_ = 0;
};

class Variables {
public:
    // Declaring a name twice gives the first variable back if the type matches (scenes declare
    // their variables each time they start); throws std::logic_error otherwise.
    Variable& add_bool(std::string name, bool value, std::string help, std::uint32_t flags = Variable::None);
    Variable& add_int(std::string name, int value, int min, int max, std::string help, std::uint32_t flags = Variable::None);
    Variable& add_float(std::string name, float value, float min, float max, std::string help,
                        std::uint32_t flags = Variable::None);
    Variable& add_text(std::string name, std::string value, std::string help, std::uint32_t flags = Variable::None);

    Variable* find(std::string_view name);
    const Variable* find(std::string_view name) const;
    // Every variable whose name starts with `prefix`, sorted by name.
    std::vector<const Variable*> list(std::string_view prefix = {}) const;
    std::size_t size() const { return variables_.size(); }

    // "set name value" lines of the archived variables that differ from their default.
    std::string archived_text() const;
    // Applies such lines (unknown names and bad values are skipped, listed in `problems`).
    void apply_archived(std::string_view text, std::vector<std::string>& problems);
    // Keeps such lines for variables not declared yet: each is applied when its variable is
    // declared (the player's preferences, read before the scenes declare their variables).
    void set_pending(std::string_view text);

private:
    Variable& add(std::string name, Variable::Type type, std::uint32_t flags, std::string help);
    void apply_pending(Variable& variable);

    std::vector<std::unique_ptr<Variable>> variables_;  // stable addresses
    std::map<std::string, std::string> pending_;
};

}  // namespace moteur
