#include "moteur/data_reader.hpp"

#include <algorithm>
#include <cmath>

namespace moteur {

namespace {

const nlohmann::json& empty_object() {
    static const nlohmann::json kEmpty = nlohmann::json::object();
    return kEmpty;
}

std::string type_name(const nlohmann::json& value) {
    switch (value.type()) {
        case nlohmann::json::value_t::null: return "null";
        case nlohmann::json::value_t::object: return "un objet";
        case nlohmann::json::value_t::array: return "une liste";
        case nlohmann::json::value_t::string: return "un texte";
        case nlohmann::json::value_t::boolean: return "un booléen";
        case nlohmann::json::value_t::number_integer:
        case nlohmann::json::value_t::number_unsigned: return "un entier";
        case nlohmann::json::value_t::number_float: return "un nombre";
        default: return "une valeur";
    }
}

std::string number_text(double value) {
    std::string text = nlohmann::json(value).dump();
    return text;
}

}  // namespace

void DataIssues::error(std::string where, std::string message) {
    issues_.push_back({DataIssue::Level::Error, std::move(where), std::move(message)});
    ++errors_;
}

void DataIssues::warning(std::string where, std::string message) {
    issues_.push_back({DataIssue::Level::Warning, std::move(where), std::move(message)});
}

void DataIssues::append(const DataIssues& other) {
    for (const DataIssue& issue : other.issues_) {
        issues_.push_back(issue);
    }
    errors_ += other.errors_;
}

std::string DataIssues::text() const {
    std::string text;
    for (const DataIssue& issue : issues_) {
        text += issue.level == DataIssue::Level::Error ? "erreur : " : "avertissement : ";
        text += issue.where;
        text += " : ";
        text += issue.message;
        text += '\n';
    }
    return text;
}

DataReader::DataReader(const nlohmann::json& value, std::string where, DataIssues& issues)
    : value_(&value), where_(std::move(where)), issues_(&issues) {
    if (!value.is_object()) {
        issues_->error(where_, "attendu un objet, trouvé " + type_name(value));
        value_ = &empty_object();
    }
}

std::string DataReader::at(std::string_view key) const {
    return where_ + " > " + std::string(key);
}

bool DataReader::has(std::string_view key) const {
    return value_->contains(key);
}

void DataReader::error(std::string_view field, const std::string& message) {
    issues_->error(at(field), message);
}

void DataReader::warning(std::string_view field, const std::string& message) {
    issues_->warning(at(field), message);
}

const nlohmann::json* DataReader::field(std::string_view key, bool required) {
    read_.emplace_back(key);
    const auto found = value_->find(key);
    if (found == value_->end() || found->is_null()) {
        if (required) {
            issues_->error(at(key), "champ obligatoire absent");
        }
        return nullptr;
    }
    return &*found;
}

float DataReader::number(std::string_view key, float min, float max, std::optional<float> fallback) {
    const nlohmann::json* value = field(key, !fallback);
    if (value == nullptr) {
        return fallback.value_or(min);
    }
    if (!value->is_number()) {
        issues_->error(at(key), "attendu un nombre, trouvé " + type_name(*value));
        return fallback.value_or(min);
    }
    const double v = value->get<double>();
    if (!std::isfinite(v) || v < min || v > max) {
        issues_->error(at(key), number_text(v) + " hors des bornes [" + number_text(min) + ", " + number_text(max) + "]");
        return fallback.value_or(std::clamp(static_cast<float>(v), min, max));
    }
    return static_cast<float>(v);
}

int DataReader::integer(std::string_view key, int min, int max, std::optional<int> fallback) {
    const nlohmann::json* value = field(key, !fallback);
    if (value == nullptr) {
        return fallback.value_or(min);
    }
    if (!value->is_number_integer()) {
        issues_->error(at(key), "attendu un entier, trouvé " + type_name(*value));
        return fallback.value_or(min);
    }
    const std::int64_t v = value->get<std::int64_t>();
    if (v < min || v > max) {
        issues_->error(at(key), std::to_string(v) + " hors des bornes [" + std::to_string(min) + ", " + std::to_string(max) + "]");
        return fallback.value_or(static_cast<int>(std::clamp<std::int64_t>(v, min, max)));
    }
    return static_cast<int>(v);
}

bool DataReader::boolean(std::string_view key, std::optional<bool> fallback) {
    const nlohmann::json* value = field(key, !fallback);
    if (value == nullptr) {
        return fallback.value_or(false);
    }
    if (!value->is_boolean()) {
        issues_->error(at(key), "attendu true ou false, trouvé " + type_name(*value));
        return fallback.value_or(false);
    }
    return value->get<bool>();
}

std::string DataReader::text(std::string_view key, std::optional<std::string> fallback) {
    const nlohmann::json* value = field(key, !fallback);
    if (value == nullptr) {
        return fallback.value_or(std::string());
    }
    if (!value->is_string()) {
        issues_->error(at(key), "attendu un texte, trouvé " + type_name(*value));
        return fallback.value_or(std::string());
    }
    return value->get<std::string>();
}

int DataReader::choice(std::string_view key, std::initializer_list<std::string_view> names, std::optional<int> fallback) {
    const nlohmann::json* value = field(key, !fallback);
    if (value == nullptr) {
        return fallback.value_or(0);
    }
    std::string allowed;
    for (const std::string_view name : names) {
        allowed += (allowed.empty() ? "" : ", ") + std::string(name);
    }
    if (!value->is_string()) {
        issues_->error(at(key), "attendu l'un de : " + allowed);
        return fallback.value_or(0);
    }
    const std::string text = value->get<std::string>();
    int index = 0;
    for (const std::string_view name : names) {
        if (name == text) {
            return index;
        }
        ++index;
    }
    issues_->error(at(key), "« " + text + " » n'est pas l'un de : " + allowed);
    return fallback.value_or(0);
}

namespace {

// Reads `count` numbers (or between `min_count` and `count`) into `out`; false with an error if not.
bool read_numbers(const nlohmann::json& value, std::size_t min_count, std::size_t count, float* out, std::string& problem) {
    if (!value.is_array() || value.size() < min_count || value.size() > count) {
        problem = "attendu une liste de " + (min_count == count ? std::to_string(count) : std::to_string(min_count) + " ou " + std::to_string(count)) + " nombres";
        return false;
    }
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (!value[i].is_number() || !std::isfinite(value[i].get<double>())) {
            problem = "l'élément " + std::to_string(i) + " n'est pas un nombre";
            return false;
        }
        out[i] = value[i].get<float>();
    }
    return true;
}

}  // namespace

glm::vec2 DataReader::vec2(std::string_view key, std::optional<glm::vec2> fallback) {
    const nlohmann::json* value = field(key, !fallback);
    glm::vec2 v = fallback.value_or(glm::vec2(0.0f));
    std::string problem;
    if (value != nullptr && !read_numbers(*value, 2, 2, &v.x, problem)) {
        issues_->error(at(key), problem);
        return fallback.value_or(glm::vec2(0.0f));
    }
    return v;
}

glm::vec3 DataReader::vec3(std::string_view key, std::optional<glm::vec3> fallback) {
    const nlohmann::json* value = field(key, !fallback);
    glm::vec3 v = fallback.value_or(glm::vec3(0.0f));
    std::string problem;
    if (value != nullptr && !read_numbers(*value, 3, 3, &v.x, problem)) {
        issues_->error(at(key), problem);
        return fallback.value_or(glm::vec3(0.0f));
    }
    return v;
}

glm::vec4 DataReader::color(std::string_view key, std::optional<glm::vec4> fallback) {
    const nlohmann::json* value = field(key, !fallback);
    if (value == nullptr) {
        return fallback.value_or(glm::vec4(1.0f));
    }
    glm::vec4 v(1.0f);
    std::string problem;
    if (!read_numbers(*value, 3, 4, &v.x, problem)) {
        issues_->error(at(key), problem);
        return fallback.value_or(glm::vec4(1.0f));
    }
    return v;
}

std::vector<std::string> DataReader::texts(std::string_view key, bool optional) {
    const nlohmann::json* value = field(key, !optional);
    std::vector<std::string> result;
    if (value == nullptr) {
        return result;
    }
    if (!value->is_array()) {
        issues_->error(at(key), "attendu une liste de textes, trouvé " + type_name(*value));
        return result;
    }
    for (std::size_t i = 0; i < value->size(); ++i) {
        if (!(*value)[i].is_string()) {
            issues_->error(at(key) + "[" + std::to_string(i) + "]", "attendu un texte");
            continue;
        }
        result.push_back((*value)[i].get<std::string>());
    }
    return result;
}

DataReader DataReader::object(std::string_view key, bool optional) {
    const nlohmann::json* value = field(key, !optional);
    if (value == nullptr) {
        return DataReader(empty_object(), at(key), *issues_);
    }
    return DataReader(*value, at(key), *issues_);
}

std::vector<DataReader> DataReader::objects(std::string_view key, bool optional) {
    const nlohmann::json* value = field(key, !optional);
    std::vector<DataReader> readers;
    if (value == nullptr) {
        return readers;
    }
    if (!value->is_array()) {
        issues_->error(at(key), "attendu une liste d'objets, trouvé " + type_name(*value));
        return readers;
    }
    for (std::size_t i = 0; i < value->size(); ++i) {
        readers.emplace_back((*value)[i], at(key) + "[" + std::to_string(i) + "]", *issues_);
    }
    return readers;
}

void DataReader::finish() {
    for (const auto& [key, value] : value_->items()) {
        if (!key.empty() && key[0] == '_') {
            continue;  // a comment
        }
        if (std::find(read_.begin(), read_.end(), key) == read_.end()) {
            issues_->warning(at(key), "champ inconnu (faute de frappe ?)");
        }
    }
}

}  // namespace moteur
