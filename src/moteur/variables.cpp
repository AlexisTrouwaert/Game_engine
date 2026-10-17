#include "moteur/variables.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace moteur {

namespace {

std::string number_text(double value, Variable::Type type) {
    if (type == Variable::Type::Bool || type == Variable::Type::Int) {
        return std::to_string(static_cast<long long>(value));
    }
    // The shortest text that reads back to the same float (std::to_chars without a precision).
    char text[64];
    const auto [end, error] = std::to_chars(text, text + sizeof(text), static_cast<float>(value));
    return error == std::errc() ? std::string(text, end) : std::string();
}

// std::clamp needs min <= max: bounds the other way round (or NaN) are refused at the declaration.
void check_bounds(const std::string& name, double min, double max) {
    if (!(min <= max)) {
        throw std::invalid_argument("Variables: '" + name + "' declared with its minimum above its maximum");
    }
}

// A value between quotes, with `\`, `"` and line ends escaped: any text survives the file (one
// line per variable).
std::string quoted_value(std::string_view value) {
    std::string text = "\"";
    for (const char c : value) {
        switch (c) {
            case '\\': text += "\\\\"; break;
            case '"': text += "\\\""; break;
            case '\n': text += "\\n"; break;
            case '\r': text += "\\r"; break;
            default: text += c; break;
        }
    }
    return text += '"';
}

// Reads a `set name "value"` line of the file. False for a line to skip: empty, a comment ('#'),
// or wrong (then `error` says why; it stays empty for the first two). A value without quotes is the
// rest of the line.
bool read_line(std::string line, std::string& name, std::string& value, std::string& error) {
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    const std::size_t first = line.find_first_not_of(" \t");
    if (first == std::string::npos || line[first] == '#') {
        return false;
    }
    std::istringstream words(line);
    std::string command;
    words >> command >> name;
    std::getline(words, value);
    value.erase(0, value.find_first_not_of(' '));
    if (command != "set" || name.empty()) {
        error = "line ignored: " + line;
        return false;
    }
    if (value.empty() || value.front() != '"') {
        return true;
    }
    std::string text;
    std::size_t i = 1;
    for (; i < value.size() && value[i] != '"'; ++i) {
        const char next = i + 1 < value.size() ? value[i + 1] : '\0';
        if (value[i] == '\\' && (next == '\\' || next == '"' || next == 'n' || next == 'r')) {
            text += next == 'n' ? '\n' : next == 'r' ? '\r' : next;
            ++i;
        } else {  // another backslash is itself (a Windows path written before the escapes)
            text += value[i];
        }
    }
    if (i + 1 != value.size()) {  // no closing quote, or something after it
        error = name + ": badly quoted value, line ignored: " + line;
        return false;
    }
    value = std::move(text);
    return true;
}

// A value of the preferences file (set_pending()): only archived variables are kept there, so
// another one named in the file is not set; a bad value is reported, the variable keeps its own.
void apply_from_file(Variable& variable, const std::string& value) {
    if ((variable.flags() & Variable::Archive) == 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Variables: '%s' is not kept in the preferences: ignored",
                    variable.name().c_str());
        return;
    }
    std::string error;
    if (!variable.set_text(value, error)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Variables: %s: %s (value ignored)", variable.name().c_str(),
                    error.c_str());
    }
}

}  // namespace

std::string Variable::value_text() const {
    return type_ == Type::Text ? text_ : number_text(number_, type_);
}

std::string Variable::default_text() const {
    return type_ == Type::Text ? default_text_ : number_text(default_number_, type_);
}

bool Variable::is_default() const {
    return type_ == Type::Text ? text_ == default_text_ : number_ == default_number_;
}

void Variable::set_number(double value) {
    if (type_ == Type::Bool) {
        value = value != 0.0 ? 1.0 : 0.0;
    } else {
        value = std::clamp(value, min_, max_);
        if (type_ == Type::Int) {
            value = std::round(value);
        }
    }
    if (value != number_) {
        number_ = value;
        ++revision_;
    }
}

bool Variable::set_text(std::string_view text, std::string& error) {
    if (type_ == Type::Text) {
        if (text_ != text) {
            text_ = std::string(text);
            ++revision_;
        }
        return true;
    }
    if (type_ == Type::Bool) {
        if (text == "1" || text == "on" || text == "true" || text == "oui") {
            set_number(1.0);
            return true;
        }
        if (text == "0" || text == "off" || text == "false" || text == "non") {
            set_number(0.0);
            return true;
        }
        error = "expected 0 or 1 (on, off)";
        return false;
    }
    double value = 0.0;
    const std::string copy(text);
    try {
        std::size_t used = 0;
        value = std::stod(copy, &used);
        if (used != copy.size()) {
            throw std::invalid_argument("trailing");
        }
    } catch (const std::exception&) {
        error = "'" + copy + "' is not a number";
        return false;
    }
    if (type_ == Type::Float) {
        // Kept as the float it is read as, like set_float(): the text written for a float (its
        // bounds included) reads back to the same value, and to the same default. Beyond a float's
        // range, infinite (refused below).
        value = std::fabs(value) <= std::numeric_limits<float>::max()
                    ? static_cast<double>(static_cast<float>(value))
                    : std::copysign(HUGE_VAL, value);
    }
    if (!std::isfinite(value) || value < min_ || value > max_ || (type_ == Type::Int && value != std::round(value))) {
        error = "expected " + std::string(type_ == Type::Int ? "an integer" : "a number") + " between " +
                number_text(min_, type_) + " and " + number_text(max_, type_);
        return false;
    }
    set_number(value);
    return true;
}

void Variable::reset() {
    if (type_ == Type::Text) {
        if (text_ != default_text_) {
            text_ = default_text_;
            ++revision_;
        }
    } else {
        set_number(default_number_);
    }
}

Variable& Variables::add(std::string name, Variable::Type type, std::uint32_t flags, std::string help) {
    if (Variable* existing = find(name)) {
        if (existing->type_ != type) {
            throw std::logic_error("Variables: '" + name + "' declared again with another type");
        }
        return *existing;
    }
    auto variable = std::make_unique<Variable>();
    variable->name_ = std::move(name);
    variable->type_ = type;
    variable->flags_ = flags;
    variable->help_ = std::move(help);
    variables_.push_back(std::move(variable));
    return *variables_.back();
}

Variable& Variables::add_bool(std::string name, bool value, std::string help, std::uint32_t flags) {
    const bool fresh = find(name) == nullptr;
    Variable& v = add(std::move(name), Variable::Type::Bool, flags, std::move(help));
    if (fresh) {
        v.number_ = v.default_number_ = value ? 1.0 : 0.0;
        v.max_ = 1.0;
        apply_pending(v);
    }
    return v;
}

Variable& Variables::add_int(std::string name, int value, int min, int max, std::string help, std::uint32_t flags) {
    check_bounds(name, min, max);
    const bool fresh = find(name) == nullptr;
    Variable& v = add(std::move(name), Variable::Type::Int, flags, std::move(help));
    if (fresh) {
        v.min_ = min;
        v.max_ = max;
        v.number_ = v.default_number_ = std::clamp(value, min, max);
        apply_pending(v);
    }
    return v;
}

Variable& Variables::add_float(std::string name, float value, float min, float max, std::string help, std::uint32_t flags) {
    check_bounds(name, min, max);
    const bool fresh = find(name) == nullptr;
    Variable& v = add(std::move(name), Variable::Type::Float, flags, std::move(help));
    if (fresh) {
        v.min_ = min;
        v.max_ = max;
        v.number_ = v.default_number_ = std::clamp(value, min, max);
        apply_pending(v);
    }
    return v;
}

Variable& Variables::add_text(std::string name, std::string value, std::string help, std::uint32_t flags) {
    const bool fresh = find(name) == nullptr;
    Variable& v = add(std::move(name), Variable::Type::Text, flags, std::move(help));
    if (fresh) {
        v.text_ = v.default_text_ = std::move(value);
        apply_pending(v);
    }
    return v;
}

Variable* Variables::find(std::string_view name) {
    for (const auto& variable : variables_) {
        if (variable->name_ == name) {
            return variable.get();
        }
    }
    return nullptr;
}

const Variable* Variables::find(std::string_view name) const {
    return const_cast<Variables*>(this)->find(name);
}

std::vector<const Variable*> Variables::list(std::string_view prefix) const {
    std::vector<const Variable*> result;
    for (const auto& variable : variables_) {
        if (variable->name_.compare(0, prefix.size(), prefix) == 0) {
            result.push_back(variable.get());
        }
    }
    std::sort(result.begin(), result.end(), [](const Variable* a, const Variable* b) { return a->name() < b->name(); });
    return result;
}

std::string Variables::archived_text() const {
    std::string text;
    for (const Variable* variable : list()) {
        if ((variable->flags() & Variable::Archive) != 0 && !variable->is_default()) {
            text.append("set ").append(variable->name()).append(" ").append(quoted_value(variable->value_text())) += "\n";
        }
    }
    // Those of variables not declared in this run: kept for a later one.
    for (const auto& [name, value] : pending_) {
        text.append("set ").append(name).append(" ").append(quoted_value(value)) += "\n";
    }
    return text;
}

void Variables::set_pending(std::string_view text) {
    std::istringstream lines{std::string(text)};
    std::string line;
    while (std::getline(lines, line)) {
        std::string name, value, error;
        if (!read_line(line, name, value, error)) {
            if (!error.empty()) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Variables: %s", error.c_str());
            }
            continue;
        }
        if (Variable* variable = find(name)) {
            apply_from_file(*variable, value);
        } else {
            pending_[name] = value;  // whether it is archived is known when it is declared
        }
    }
}

void Variables::apply_pending(Variable& variable) {
    const auto found = pending_.find(variable.name());
    if (found != pending_.end()) {
        apply_from_file(variable, found->second);
        pending_.erase(found);
    }
}

void Variables::apply_archived(std::string_view text, std::vector<std::string>& problems) {
    std::istringstream lines{std::string(text)};
    std::string line;
    while (std::getline(lines, line)) {
        std::string name, value, error;
        if (!read_line(line, name, value, error)) {
            if (!error.empty()) {
                problems.push_back(std::move(error));
            }
            continue;
        }
        Variable* variable = find(name);
        if (variable == nullptr) {
            problems.push_back("line ignored: " + line);
        } else if (!variable->set_text(value, error)) {
            std::string problem = name;
            problem.append(": ").append(error);
            problems.push_back(std::move(problem));
        }
    }
}

}  // namespace moteur
