#include "moteur/variables.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace moteur {

namespace {

std::string number_text(double value, Variable::Type type) {
    if (type == Variable::Type::Bool || type == Variable::Type::Int) {
        return std::to_string(static_cast<long long>(value));
    }
    std::ostringstream text;
    text << static_cast<float>(value);
    return text.str();
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
        error = "attendu 0 ou 1 (on, off)";
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
        error = "« " + copy + " » n'est pas un nombre";
        return false;
    }
    if (!std::isfinite(value) || value < min_ || value > max_ || (type_ == Type::Int && value != std::round(value))) {
        error = "attendu " + std::string(type_ == Type::Int ? "un entier" : "un nombre") + " entre " +
                number_text(min_, type_) + " et " + number_text(max_, type_);
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
            text += "set " + variable->name() + " \"" + variable->value_text() + "\"\n";
        }
    }
    // Those of variables not declared in this run: kept for a later one.
    for (const auto& [name, value] : pending_) {
        text += "set " + name + " \"" + value + "\"\n";
    }
    return text;
}

void Variables::set_pending(std::string_view text) {
    std::istringstream lines{std::string(text)};
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream words(line);
        std::string command, name, value;
        words >> command >> name;
        std::getline(words, value);
        value.erase(0, value.find_first_not_of(' '));
        if (!value.empty() && value.back() == '\r') {
            value.pop_back();
        }
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        if (command != "set" || name.empty()) {
            continue;
        }
        if (Variable* variable = find(name)) {
            std::string error;
            variable->set_text(value, error);
        } else {
            pending_[name] = value;
        }
    }
}

void Variables::apply_pending(Variable& variable) {
    const auto found = pending_.find(variable.name());
    if (found != pending_.end()) {
        std::string error;
        variable.set_text(found->second, error);
        pending_.erase(found);
    }
}

void Variables::apply_archived(std::string_view text, std::vector<std::string>& problems) {
    std::istringstream lines{std::string(text)};
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream words(line);
        std::string command, name, value;
        words >> command >> name;
        std::getline(words, value);
        value.erase(0, value.find_first_not_of(' '));
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        Variable* variable = find(name);
        std::string error;
        if (command != "set" || variable == nullptr) {
            problems.push_back("ligne ignorée : " + line);
        } else if (!variable->set_text(value, error)) {
            problems.push_back(name + " : " + error);
        }
    }
}

}  // namespace moteur
