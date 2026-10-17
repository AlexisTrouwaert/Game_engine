#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace moteur {

/// Variables to tune the game while it runs (milestone 7, part 3): named with dots by system
/// ("fog.enabled", "debug.paths", "time.scale"), typed, bounded, with a default and a help line. Each
/// system declares its own once and keeps the handle (a stable reference), then reads it where it
/// needs it; the console sets them by name ("set fog.enabled 0").
///
/// Variables are made by Variables (add_bool() and the others), which owns them. Main thread only.
class Variable {
public:
    /// What the variable holds. Bool, Int and Float keep a number (Bool: 0 or 1); Text a string.
    enum class Type { Bool, Int, Float, Text };
    /// Flags given when declaring the variable, combined with `|`.
    enum Flags : std::uint32_t {
        None = 0,     ///< a tool variable, not saved
        Archive = 1,  ///< kept in the player's preferences (variables.cfg)
        Logic = 2,    ///< changes the game's logic: set through a game command (queued, replayed)
    };

    /// The dotted name, as typed in the console ("fog.enabled").
    const std::string& name() const { return name_; }
    /// One line saying what the variable does (the console's `vars` and `get` show it).
    const std::string& help() const { return help_; }
    /// The type the variable was declared with.
    Type type() const { return type_; }
    /// The Flags it was declared with.
    std::uint32_t flags() const { return flags_; }

    /// The value as a number, whatever the numeric type (Bool: true if not 0); false for Text.
    bool as_bool() const { return number_ != 0.0; }
    /// The value as a number, truncated toward zero; 0 for Text.
    int as_int() const { return static_cast<int>(number_); }
    /// The value as a number; 0 for Text.
    float as_float() const { return static_cast<float>(number_); }
    /// The value of a Text variable; empty for the other types.
    const std::string& as_text() const { return text_; }
    /// The value as the console shows it ("1", "0.5", "texte"); a Float in the shortest text that
    /// set_text() reads back to the same float.
    std::string value_text() const;
    /// The default value (the one it was declared with), as value_text() writes it.
    std::string default_text() const;
    /// The value is the default one.
    bool is_default() const;
    /// Grows by one at each change: a system can notice it without being called.
    std::uint64_t revision() const { return revision_; }

    /// Parses and sets. Bool takes 1, 0, on, off, true, false, oui, non; Int and Float a number
    /// within the bounds (not clamped: refused), a Float's rounded to a float first; Text any text. False, with the reason in `error`,
    /// if the text is not a valid value (the variable then keeps its value).
    bool set_text(std::string_view text, std::string& error);
    /// Sets a Bool (or a number to 1 or 0, clamped to its bounds).
    void set_bool(bool value) { set_number(value ? 1.0 : 0.0); }
    /// Sets a number, clamped to the bounds (a Bool becomes 1 for any value but 0).
    void set_int(int value) { set_number(value); }
    /// Sets a number, clamped to the bounds and rounded for an Int (a Bool becomes 1 for any value
    /// but 0). The number setters do nothing to a Text variable.
    void set_float(float value) { set_number(value); }
    /// Back to the default value.
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

/// The set of every Variable (the Console owns one: Console::variables()). Variables are never
/// removed: the references and pointers it gives stay valid as long as the Variables.
class Variables {
public:
    /// Declares a variable with its default `value`, its bounds `min` to `max` (Int and Float; the
    /// default is clamped to them), a `help` line and its Variable::Flags; a value kept by
    /// set_pending() for this name is applied at once (only to an archived variable, as set_pending()). Declaring a name twice gives the first
    /// variable back, unchanged (default, bounds, help and flags of the second call are ignored), if
    /// the type matches (scenes declare their variables each time they start); throws
    /// std::logic_error otherwise. A `min` above `max` (or a NaN bound) throws
    /// std::invalid_argument naming the variable. O(variables).
    ///@{
    Variable& add_bool(std::string name, bool value, std::string help, std::uint32_t flags = Variable::None);
    Variable& add_int(std::string name, int value, int min, int max, std::string help, std::uint32_t flags = Variable::None);
    Variable& add_float(std::string name, float value, float min, float max, std::string help,
                        std::uint32_t flags = Variable::None);
    Variable& add_text(std::string name, std::string value, std::string help, std::uint32_t flags = Variable::None);
    ///@}

    /// The variable called `name`, null if none. O(variables).
    ///@{
    Variable* find(std::string_view name);
    const Variable* find(std::string_view name) const;
    ///@}
    /// Every variable whose name starts with `prefix`, sorted by name.
    std::vector<const Variable*> list(std::string_view prefix = {}) const;
    /// How many variables are declared.
    std::size_t size() const { return variables_.size(); }

    /// `set name "value"` lines (one per line, sorted by name) of the archived variables that differ
    /// from their default, then those still pending (set_pending()) for variables not declared in
    /// this run, so that saving does not lose them. In the quotes, `\`, `"`, a line feed and a
    /// carriage return are written `\\`, `\"`, `\n`, `\r`: any text value reads back the same.
    std::string archived_text() const;
    /// Applies such lines (a value without quotes is the rest of the line, as typed); empty lines and
    /// lines starting with '#' are skipped. Unknown names, other commands, badly quoted values and
    /// bad values are skipped too, each with a line appended to `problems`.
    void apply_archived(std::string_view text, std::vector<std::string>& problems);
    /// Reads such lines (the player's preferences, variables.cfg, read before the scenes declare
    /// their variables): applies them to the archived variables already declared and keeps those of
    /// names not declared yet, each applied when its variable is declared. A line naming a variable
    /// that is not archived (the file only holds archived ones), a bad value or a malformed line is
    /// ignored with a warning in the log.
    void set_pending(std::string_view text);

private:
    Variable& add(std::string name, Variable::Type type, std::uint32_t flags, std::string help);
    void apply_pending(Variable& variable);

    std::vector<std::unique_ptr<Variable>> variables_;  ///< stable addresses
    std::map<std::string, std::string> pending_;
};

}  // namespace moteur
