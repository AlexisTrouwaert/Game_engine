#include "moteur/reflection.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <system_error>

#include "moteur/reflection_io.hpp"

namespace moteur {

// --- Registry by name ----------------------------------------------------------------------------

namespace {

struct Registry {
    std::mutex mutex;
    std::map<std::string, const TypeInfo*, std::less<>> types;
};

Registry& registry() {
    static Registry r;
    return r;
}

}  // namespace

namespace reflection_detail {

void register_type(const TypeInfo& type) {
    Registry& r = registry();
    const std::lock_guard lock(r.mutex);
    const auto [found, added] = r.types.emplace(type.name, &type);
    if (!added && found->second != &type) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Reflection: two types are described as '%s': find_type() gives the first one", type.name.c_str());
    }
}

}  // namespace reflection_detail

const TypeInfo* find_type(std::string_view name) {
    Registry& r = registry();
    const std::lock_guard lock(r.mutex);
    const auto found = r.types.find(name);
    return found != r.types.end() ? found->second : nullptr;
}

bool FieldInfo::answers_to(std::string_view key) const {
    return name == key || std::find(aliases.begin(), aliases.end(), key) != aliases.end();
}

const FieldInfo* TypeInfo::field(std::string_view key) const {
    for (const FieldInfo& f : fields) {
        if (f.name == key) {
            return &f;
        }
    }
    for (const FieldInfo& f : fields) {
        if (f.answers_to(key)) {
            return &f;
        }
    }
    return nullptr;
}

// --- Scalars -------------------------------------------------------------------------------------

namespace {

std::int64_t get_signed(const void* p, std::uint8_t bytes) {
    switch (bytes) {
        case 1: { std::int8_t v; std::memcpy(&v, p, 1); return v; }
        case 2: { std::int16_t v; std::memcpy(&v, p, 2); return v; }
        case 4: { std::int32_t v; std::memcpy(&v, p, 4); return v; }
        default: { std::int64_t v; std::memcpy(&v, p, 8); return v; }
    }
}

std::uint64_t get_unsigned(const void* p, std::uint8_t bytes) {
    switch (bytes) {
        case 1: { std::uint8_t v; std::memcpy(&v, p, 1); return v; }
        case 2: { std::uint16_t v; std::memcpy(&v, p, 2); return v; }
        case 4: { std::uint32_t v; std::memcpy(&v, p, 4); return v; }
        default: { std::uint64_t v; std::memcpy(&v, p, 8); return v; }
    }
}

void set_signed(void* p, std::uint8_t bytes, std::int64_t value) {
    switch (bytes) {
        case 1: { const auto v = static_cast<std::int8_t>(value); std::memcpy(p, &v, 1); break; }
        case 2: { const auto v = static_cast<std::int16_t>(value); std::memcpy(p, &v, 2); break; }
        case 4: { const auto v = static_cast<std::int32_t>(value); std::memcpy(p, &v, 4); break; }
        default: std::memcpy(p, &value, 8); break;
    }
}

void set_unsigned(void* p, std::uint8_t bytes, std::uint64_t value) {
    switch (bytes) {
        case 1: { const auto v = static_cast<std::uint8_t>(value); std::memcpy(p, &v, 1); break; }
        case 2: { const auto v = static_cast<std::uint16_t>(value); std::memcpy(p, &v, 2); break; }
        case 4: { const auto v = static_cast<std::uint32_t>(value); std::memcpy(p, &v, 4); break; }
        default: std::memcpy(p, &value, 8); break;
    }
}

// The range of an integer type, as doubles (exact up to 2^53, enough for bounds).
double int_min(const TypeInfo& type) {
    if (!type.is_signed) {
        return 0.0;
    }
    return -std::ldexp(1.0, type.bytes * 8 - 1);
}
double int_max(const TypeInfo& type) {
    return std::ldexp(1.0, type.bytes * 8 - (type.is_signed ? 1 : 0)) - 1.0;
}

std::int64_t enum_index(const void* p, const TypeInfo& type) {
    return type.is_signed ? get_signed(p, type.bytes) : static_cast<std::int64_t>(get_unsigned(p, type.bytes));
}

void set_enum_index(void* p, const TypeInfo& type, std::int64_t index) {
    if (type.is_signed) {
        set_signed(p, type.bytes, index);
    } else {
        set_unsigned(p, type.bytes, static_cast<std::uint64_t>(index));
    }
}

std::string json_type_name(const nlohmann::json& value) {
    switch (value.type()) {
        case nlohmann::json::value_t::null: return "null";
        case nlohmann::json::value_t::object: return "an object";
        case nlohmann::json::value_t::array: return "an array";
        case nlohmann::json::value_t::string: return "a string";
        case nlohmann::json::value_t::boolean: return "a boolean";
        case nlohmann::json::value_t::number_integer:
        case nlohmann::json::value_t::number_unsigned: return "an integer";
        case nlohmann::json::value_t::number_float: return "a number";
        default: return "a value";
    }
}

// Numbers in messages exactly as DataReader writes them: a value as its double, a bound of a float
// field as the float it is ("0.1", "20.0").
std::string value_text(double value) {
    return nlohmann::json(value).dump();
}

std::string bound_text(double bound, bool as_float) {
    if (!as_float) {
        return value_text(bound);
    }
    char buffer[32];
    const auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), static_cast<float>(bound));
    std::string text(buffer, error == std::errc() ? end : buffer);
    if (text.find_first_of(".en") == std::string::npos) {
        text += ".0";
    }
    return text;
}

std::string int_text(double value) {
    return std::to_string(static_cast<long long>(value));
}

struct Reading {
    DataIssues& issues;
    const ReadOptions& options;
};

bool read_value(const nlohmann::json& json, void* value, const TypeInfo& type, const FieldInfo* field,
                const std::string& where, Reading& reading);

// `count` numbers of `json` into `out` (floats); false with an error.
bool read_floats(const nlohmann::json& json, std::size_t min_count, std::size_t count, float* out, const FieldInfo* field,
                 const std::string& where, DataIssues& issues) {
    if (!json.is_array() || json.size() < min_count || json.size() > count) {
        issues.error(where, "expected an array of " +
                                (min_count == count ? std::to_string(count)
                                                    : std::to_string(min_count) + " or " + std::to_string(count)) +
                                " numbers");
        return false;
    }
    for (std::size_t i = 0; i < json.size(); ++i) {
        if (!json[i].is_number() || !std::isfinite(json[i].get<double>())) {
            issues.error(where, "element " + std::to_string(i) + " is not a number");
            return false;
        }
        const double v = json[i].get<double>();
        if (field != nullptr && (v < field->min || v > field->max)) {
            issues.error(where, "element " + std::to_string(i) + ": " + value_text(v) + " out of range [" +
                                    bound_text(field->min, true) + ", " + bound_text(field->max, true) + "]");
            return false;
        }
        out[i] = json[i].get<float>();
    }
    return true;
}

bool read_struct_object(const nlohmann::json& json, void* object, const TypeInfo& type, const std::string& where,
                        Reading& reading) {
    const std::size_t errors = reading.issues.errors();
    DataReader reader(json, where, reading.issues);
    read_fields(reader, object, type, reading.options);
    if (reading.options.warn_unknown) {
        reader.finish();
    }
    return reading.issues.errors() == errors;
}

bool read_value(const nlohmann::json& json, void* value, const TypeInfo& type, const FieldInfo* field,
                const std::string& where, Reading& reading) {
    DataIssues& issues = reading.issues;
    switch (type.kind) {
        case TypeKind::Bool:
            if (!json.is_boolean()) {
                issues.error(where, "expected true or false, found " + json_type_name(json));
                return false;
            }
            *static_cast<bool*>(value) = json.get<bool>();
            return true;
        case TypeKind::Int: {
            if (!json.is_number_integer()) {
                issues.error(where, "expected an integer, found " + json_type_name(json));
                return false;
            }
            const double low = std::max(int_min(type), field != nullptr ? field->min : -INFINITY);
            const double high = std::min(int_max(type), field != nullptr ? field->max : INFINITY);
            if (json.is_number_unsigned()) {
                const auto v = json.get<std::uint64_t>();
                if (static_cast<double>(v) > high || static_cast<double>(v) < low) {
                    issues.error(where, std::to_string(v) + " out of range [" + int_text(low) + ", " + int_text(high) + "]");
                    return false;
                }
                if (type.is_signed) {
                    set_signed(value, type.bytes, static_cast<std::int64_t>(v));
                } else {
                    set_unsigned(value, type.bytes, v);
                }
            } else {
                const auto v = json.get<std::int64_t>();
                if (static_cast<double>(v) > high || static_cast<double>(v) < low) {
                    issues.error(where, std::to_string(v) + " out of range [" + int_text(low) + ", " + int_text(high) + "]");
                    return false;
                }
                if (type.is_signed) {
                    set_signed(value, type.bytes, v);
                } else {
                    set_unsigned(value, type.bytes, static_cast<std::uint64_t>(v));
                }
            }
            return true;
        }
        case TypeKind::Float: {
            if (!json.is_number()) {
                issues.error(where, "expected a number, found " + json_type_name(json));
                return false;
            }
            const double v = json.get<double>();
            const double low = field != nullptr ? field->min : -INFINITY;
            const double high = field != nullptr ? field->max : INFINITY;
            if (!std::isfinite(v) || v < low || v > high) {
                issues.error(where, value_text(v) + " out of range [" + bound_text(low, type.bytes == 4) + ", " +
                                        bound_text(high, type.bytes == 4) + "]");
                return false;
            }
            if (type.bytes == 4) {
                *static_cast<float*>(value) = json.get<float>();  // the same conversion as a hand-written reader
            } else {
                *static_cast<double*>(value) = v;
            }
            return true;
        }
        case TypeKind::String:
            if (!json.is_string()) {
                issues.error(where, "expected a string, found " + json_type_name(json));
                return false;
            }
            *static_cast<std::string*>(value) = json.get<std::string>();
            return true;
        case TypeKind::FloatVector: {
            float v[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            const bool color = field != nullptr && (field->flags & kFieldColor) != 0 && type.components == 4;
            if (!read_floats(json, color ? 3 : type.components, type.components, v, field, where, issues)) {
                return false;
            }
            std::memcpy(value, v, sizeof(float) * type.components);
            return true;
        }
        case TypeKind::IntVector: {
            if (!json.is_array() || json.size() != type.components) {
                issues.error(where, "expected an array of " + std::to_string(type.components) + " integers");
                return false;
            }
            int v[3] = {};
            for (std::size_t i = 0; i < json.size(); ++i) {
                if (!json[i].is_number_integer() || json[i].get<std::int64_t>() < INT32_MIN ||
                    json[i].get<std::int64_t>() > INT32_MAX) {
                    issues.error(where, "element " + std::to_string(i) + " is not an integer");
                    return false;
                }
                v[i] = static_cast<int>(json[i].get<std::int64_t>());
            }
            std::memcpy(value, v, sizeof(int) * type.components);
            return true;
        }
        case TypeKind::Quat: {
            float v[4] = {};
            if (!read_floats(json, 4, 4, v, nullptr, where, issues)) {
                return false;
            }
            *static_cast<glm::quat*>(value) = glm::quat(v[0], v[1], v[2], v[3]);
            return true;
        }
        case TypeKind::Enum: {
            std::string allowed;
            for (const std::string& name : type.enum_names) {
                allowed += (allowed.empty() ? "" : ", ") + name;
            }
            if (json.is_string()) {
                const std::string text = json.get<std::string>();
                const auto found = std::find(type.enum_names.begin(), type.enum_names.end(), text);
                if (found == type.enum_names.end()) {
                    issues.error(where, "'" + text + "' is not one of: " + allowed);
                    return false;
                }
                set_enum_index(value, type, found - type.enum_names.begin());
                return true;
            }
            // The number of a development save written before names: still read.
            if (json.is_number_integer() && json.get<std::int64_t>() >= 0 &&
                json.get<std::int64_t>() < static_cast<std::int64_t>(type.enum_names.size())) {
                set_enum_index(value, type, json.get<std::int64_t>());
                return true;
            }
            issues.error(where, "expected one of: " + allowed);
            return false;
        }
        case TypeKind::Struct:
            return read_struct_object(json, value, type, where, reading);
        case TypeKind::Array: {
            if (!json.is_array()) {
                issues.error(where, "expected an array, found " + json_type_name(json));
                return false;
            }
            type.array_resize(value, json.size());
            bool ok = true;
            for (std::size_t i = 0; i < json.size(); ++i) {
                ok = read_value(json[i], type.array_at(value, i), *type.element, field, where + "[" + std::to_string(i) + "]",
                                reading) && ok;
            }
            return ok;
        }
        case TypeKind::Optional:
            if (json.is_null()) {
                type.optional_reset(value);
                return true;
            }
            return read_value(json, type.optional_emplace(value), *type.element, field, where, reading);
        case TypeKind::DataRef:
            if (!json.is_string()) {
                issues.error(where, "expected a string, found " + json_type_name(json));
                return false;
            }
            *type.ref_id(value) = json.get<std::string>();
            if (reading.options.tables != nullptr && !type.ref_id(value)->empty() &&
                !type.ref_bind(value, *reading.options.tables)) {
                issues.error(where, "'" + *type.ref_id(value) + "' does not exist in its table");
                return false;
            }
            return true;
        case TypeKind::Entity: {
            if (json.is_null()) {
                *static_cast<entt::entity*>(value) = entt::null;
                return true;
            }
            std::string problem = "entity references cannot be read here";
            entt::entity entity = entt::null;
            if (!reading.options.entity || !reading.options.entity(json, entity, problem)) {
                issues.error(where, problem);
                return false;
            }
            *static_cast<entt::entity*>(value) = entity;
            return true;
        }
        case TypeKind::Asset: {
            std::string problem = "assets cannot be read here";
            if (!reading.options.asset || !reading.options.asset(type, json, value, problem)) {
                issues.error(where, problem);
                return false;
            }
            return true;
        }
    }
    return false;
}

nlohmann::json write_value(const void* value, const TypeInfo& type, const WriteOptions& options);

}  // namespace

ScratchObject::ScratchObject(const TypeInfo& type) : type_(&type) {
    storage_ = ::operator new(type.size, std::align_val_t(type.align));
    try {
        type.construct(storage_);
    } catch (...) {
        ::operator delete(storage_, std::align_val_t(type.align));
        throw;
    }
}

ScratchObject::~ScratchObject() {
    type_->destroy(storage_);
    ::operator delete(storage_, std::align_val_t(type_->align));
}

namespace {

nlohmann::json write_struct(const void* object, const TypeInfo& type, const WriteOptions& options) {
    nlohmann::json json = nlohmann::json::object();
    // The defaults to compare with, for skip_defaults: a default-constructed struct.
    std::optional<ScratchObject> defaults_object;
    const void* defaults = nullptr;
    if (options.skip_defaults) {
        defaults = defaults_object.emplace(type).get();
    }
    for (const FieldInfo& field : type.fields) {
        if ((field.flags & options.flags) == 0) {
            continue;
        }
        const void* member = field.in(object);
        if (defaults != nullptr && values_equal(member, field.in(defaults), *field.type)) {
            continue;
        }
        json[field.name] = write_value(member, *field.type, options);
    }
    return json;
}

nlohmann::json write_value(const void* value, const TypeInfo& type, const WriteOptions& options) {
    switch (type.kind) {
        case TypeKind::Bool:
            return *static_cast<const bool*>(value);
        case TypeKind::Int:
            if (type.is_signed) {
                return get_signed(value, type.bytes);
            }
            return get_unsigned(value, type.bytes);
        case TypeKind::Float:
            if (type.bytes == 4) {
                return *static_cast<const float*>(value);
            }
            return *static_cast<const double*>(value);
        case TypeKind::String:
            return *static_cast<const std::string*>(value);
        case TypeKind::FloatVector: {
            nlohmann::json array = nlohmann::json::array();
            const auto* v = static_cast<const float*>(value);
            for (int i = 0; i < type.components; ++i) {
                array.push_back(v[i]);
            }
            return array;
        }
        case TypeKind::IntVector: {
            nlohmann::json array = nlohmann::json::array();
            const auto* v = static_cast<const int*>(value);
            for (int i = 0; i < type.components; ++i) {
                array.push_back(v[i]);
            }
            return array;
        }
        case TypeKind::Quat: {
            const auto& q = *static_cast<const glm::quat*>(value);
            return {q.w, q.x, q.y, q.z};
        }
        case TypeKind::Enum: {
            const std::int64_t index = enum_index(value, type);
            if (index >= 0 && index < static_cast<std::int64_t>(type.enum_names.size())) {
                return type.enum_names[static_cast<std::size_t>(index)];
            }
            return index;  // a value without a name: kept as a number (read back the same)
        }
        case TypeKind::Struct:
            return write_struct(value, type, options);
        case TypeKind::Array: {
            nlohmann::json array = nlohmann::json::array();
            const std::size_t count = type.array_size(value);
            for (std::size_t i = 0; i < count; ++i) {
                array.push_back(write_value(type.array_at(const_cast<void*>(value), i), *type.element, options));
            }
            return array;
        }
        case TypeKind::Optional:
            if (!type.optional_has(value)) {
                return nullptr;
            }
            return write_value(type.optional_emplace(const_cast<void*>(value)), *type.element, options);
        case TypeKind::DataRef:
            return *type.ref_id(const_cast<void*>(value));
        case TypeKind::Entity:
            return options.entity ? options.entity(*static_cast<const entt::entity*>(value)) : nlohmann::json();
        case TypeKind::Asset:
            return options.asset ? options.asset(type, value) : nlohmann::json();
    }
    return nullptr;
}

}  // namespace

nlohmann::json write_json(const void* value, const TypeInfo& type, const WriteOptions& options) {
    return write_value(value, type, options);
}

bool read_json(const nlohmann::json& json, void* value, const TypeInfo& type, const std::string& where,
               DataIssues& issues, const ReadOptions& options) {
    const std::size_t errors = issues.errors();
    Reading reading{issues, options};
    read_value(json, value, type, nullptr, where, reading);
    return issues.errors() == errors;
}

void read_fields(DataReader& reader, void* object, const TypeInfo& type, const ReadOptions& options) {
    Reading reading{reader.issues(), options};
    const nlohmann::json& json = reader.json();
    for (const FieldInfo& field : type.fields) {
        if ((field.flags & options.flags) == 0) {
            continue;
        }
        // The field's key first, then its older names (a renamed field still reads old files).
        std::string key = field.name;
        const auto present = [&json](const std::string& k) {
            const auto found = json.find(k);
            return found != json.end() && !found->is_null();
        };
        if (!present(key)) {
            for (const std::string& alias : field.aliases) {
                if (present(alias)) {
                    key = alias;
                    break;
                }
            }
        } else {
            for (const std::string& alias : field.aliases) {
                if (present(alias)) {
                    reader.warning(alias, "older name of '" + field.name + "', ignored: '" + field.name + "' is given too");
                }
            }
        }
        for (const std::string& alias : field.aliases) {
            reader.mark_read(alias);
        }
        const nlohmann::json* value = reader.value(key, field.required && key == field.name && !present(key));
        if (value == nullptr) {
            continue;  // missing: the struct's value stays (or "required field missing" was reported)
        }
        read_value(*value, field.in(object), *field.type, &field, reader.location(key), reading);
    }
}

void link_references(void* object, const TypeInfo& type, DataLinker& linker, const std::string& path) {
    const auto link_value = [&linker](auto& self, void* value, const TypeInfo& t, const FieldInfo& field,
                                      const std::string& at) -> void {
        switch (t.kind) {
            case TypeKind::DataRef:
                t.ref_resolve(value, linker, at, !field.required);
                break;
            case TypeKind::Struct:
                link_references(value, t, linker, at);
                break;
            case TypeKind::Array:
                for (std::size_t i = 0; i < t.array_size(value); ++i) {
                    self(self, t.array_at(value, i), *t.element, field, at + "[" + std::to_string(i) + "]");
                }
                break;
            case TypeKind::Optional:
                if (t.optional_has(value)) {
                    self(self, t.optional_emplace(value), *t.element, field, at);
                }
                break;
            default:
                break;
        }
    };
    for (const FieldInfo& field : type.fields) {
        link_value(link_value, field.in(object), *field.type, field, path.empty() ? field.name : path + " > " + field.name);
    }
}

bool values_equal(const void* a, const void* b, const TypeInfo& type) {
    switch (type.kind) {
        case TypeKind::Bool:
            return *static_cast<const bool*>(a) == *static_cast<const bool*>(b);
        case TypeKind::Int:
            return get_unsigned(a, type.bytes) == get_unsigned(b, type.bytes);
        case TypeKind::Float:
            if (type.bytes == 4) {
                return *static_cast<const float*>(a) == *static_cast<const float*>(b);
            }
            return *static_cast<const double*>(a) == *static_cast<const double*>(b);
        case TypeKind::String:
            return *static_cast<const std::string*>(a) == *static_cast<const std::string*>(b);
        case TypeKind::FloatVector:
            for (int i = 0; i < type.components; ++i) {
                if (static_cast<const float*>(a)[i] != static_cast<const float*>(b)[i]) {
                    return false;
                }
            }
            return true;
        case TypeKind::IntVector:
            return std::memcmp(a, b, sizeof(int) * type.components) == 0;
        case TypeKind::Quat:
            return *static_cast<const glm::quat*>(a) == *static_cast<const glm::quat*>(b);
        case TypeKind::Enum:
            return enum_index(a, type) == enum_index(b, type);
        case TypeKind::Struct:
            for (const FieldInfo& field : type.fields) {
                if (!values_equal(field.in(a), field.in(b), *field.type)) {
                    return false;
                }
            }
            return true;
        case TypeKind::Array: {
            const std::size_t count = type.array_size(a);
            if (count != type.array_size(b)) {
                return false;
            }
            for (std::size_t i = 0; i < count; ++i) {
                if (!values_equal(type.array_at(const_cast<void*>(a), i), type.array_at(const_cast<void*>(b), i), *type.element)) {
                    return false;
                }
            }
            return true;
        }
        case TypeKind::Optional: {
            const bool has = type.optional_has(a);
            if (has != type.optional_has(b)) {
                return false;
            }
            return !has || values_equal(type.optional_emplace(const_cast<void*>(a)), type.optional_emplace(const_cast<void*>(b)),
                                        *type.element);
        }
        case TypeKind::DataRef:
            return *type.ref_id(const_cast<void*>(a)) == *type.ref_id(const_cast<void*>(b));
        case TypeKind::Entity:
            return *static_cast<const entt::entity*>(a) == *static_cast<const entt::entity*>(b);
        case TypeKind::Asset:
            return type.same_asset(a, b);
    }
    return false;
}

double number_value(const void* value, const TypeInfo& type) {
    switch (type.kind) {
        case TypeKind::Bool: return *static_cast<const bool*>(value) ? 1.0 : 0.0;
        case TypeKind::Int:
            return type.is_signed ? static_cast<double>(get_signed(value, type.bytes))
                                  : static_cast<double>(get_unsigned(value, type.bytes));
        case TypeKind::Float:
            return type.bytes == 4 ? static_cast<double>(*static_cast<const float*>(value)) : *static_cast<const double*>(value);
        default: return 0.0;
    }
}

void set_number_value(void* value, const TypeInfo& type, double number) {
    switch (type.kind) {
        case TypeKind::Bool: *static_cast<bool*>(value) = number != 0.0; break;
        case TypeKind::Int: {
            const double clamped = std::clamp(std::round(number), int_min(type), int_max(type));
            if (type.is_signed) {
                set_signed(value, type.bytes, static_cast<std::int64_t>(clamped));
            } else {
                set_unsigned(value, type.bytes, static_cast<std::uint64_t>(clamped));
            }
            break;
        }
        case TypeKind::Float:
            if (type.bytes == 4) {
                *static_cast<float*>(value) = static_cast<float>(number);
            } else {
                *static_cast<double*>(value) = number;
            }
            break;
        default: break;
    }
}

// --- JSON Schema ---------------------------------------------------------------------------------

namespace {

nlohmann::json schema_of(const TypeInfo& type, const FieldInfo* field, std::uint32_t flags, nlohmann::json& defs);

nlohmann::json struct_schema(const TypeInfo& type, std::uint32_t flags, nlohmann::json& defs) {
    nlohmann::json schema = {{"type", "object"}, {"additionalProperties", true}};
    nlohmann::json properties = nlohmann::json::object();
    nlohmann::json required = nlohmann::json::array();
    for (const FieldInfo& field : type.fields) {
        if ((field.flags & flags) == 0) {
            continue;
        }
        nlohmann::json property = schema_of(*field.type, &field, flags, defs);
        std::string description = field.doc;
        if (!field.unit.empty()) {
            description += (description.empty() ? "" : " ") + std::string("(") + field.unit + ")";
        }
        if (!description.empty()) {
            property["description"] = description;
        }
        properties[field.name] = property;
        for (const std::string& alias : field.aliases) {
            properties[alias] = {{"deprecated", true}, {"description", "older name of '" + field.name + "'"}};
        }
        if (field.required) {
            required.push_back(field.name);
        }
    }
    // Comments: any key starting with '_'.
    schema["patternProperties"] = {{"^_", nlohmann::json::object()}};
    schema["properties"] = properties;
    if (!required.empty()) {
        schema["required"] = required;
    }
    return schema;
}

nlohmann::json schema_of(const TypeInfo& type, const FieldInfo* field, std::uint32_t flags, nlohmann::json& defs) {
    const auto bounded = [field](nlohmann::json schema) {
        if (field != nullptr && std::isfinite(field->min)) {
            schema["minimum"] = field->min;
        }
        if (field != nullptr && std::isfinite(field->max)) {
            schema["maximum"] = field->max;
        }
        return schema;
    };
    switch (type.kind) {
        case TypeKind::Bool: return {{"type", "boolean"}};
        case TypeKind::Int: {
            nlohmann::json schema = bounded({{"type", "integer"}});
            if (!schema.contains("minimum") && !type.is_signed) {
                schema["minimum"] = 0;
            }
            return schema;
        }
        case TypeKind::Float: return bounded({{"type", "number"}});
        case TypeKind::String: return {{"type", "string"}};
        case TypeKind::FloatVector: {
            const bool color = field != nullptr && (field->flags & kFieldColor) != 0 && type.components == 4;
            return {{"type", "array"}, {"items", bounded({{"type", "number"}})},
                    {"minItems", color ? 3 : type.components}, {"maxItems", type.components}};
        }
        case TypeKind::IntVector:
            return {{"type", "array"}, {"items", {{"type", "integer"}}}, {"minItems", type.components},
                    {"maxItems", type.components}};
        case TypeKind::Quat:
            return {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 4}, {"maxItems", 4},
                    {"description", "quaternion w, x, y, z"}};
        case TypeKind::Enum:
            if (!defs.contains(type.name)) {
                defs[type.name] = {{"enum", type.enum_names}};
            }
            return {{"$ref", "#/$defs/" + type.name}};
        case TypeKind::Struct:
            if (!defs.contains(type.name)) {
                defs[type.name] = nlohmann::json::object();  // first: a struct that contains itself
                defs[type.name] = struct_schema(type, flags, defs);
            }
            return {{"$ref", "#/$defs/" + type.name}};
        case TypeKind::Array:
            return {{"type", "array"}, {"items", schema_of(*type.element, field, flags, defs)}};
        case TypeKind::Optional:
            return {{"anyOf", {schema_of(*type.element, field, flags, defs), {{"type", "null"}}}}};
        case TypeKind::DataRef: return {{"type", "string"}, {"description", "identifier of a row"}};
        case TypeKind::Entity: return {{"type", {"integer", "null"}}, {"description", "an entity"}};
        case TypeKind::Asset: return {{"description", "an asset"}};
    }
    return nlohmann::json::object();
}

}  // namespace

nlohmann::json json_schema(const TypeInfo& type, std::uint32_t flags) {
    nlohmann::json defs = nlohmann::json::object();
    nlohmann::json root = schema_of(type, nullptr, flags, defs);
    nlohmann::json schema = {{"$schema", "https://json-schema.org/draft/2020-12/schema"}, {"title", type.name}};
    if (type.kind == TypeKind::Struct) {
        schema.update(defs[type.name]);
        defs.erase(type.name);
        // A field of the same type as the root refers to it by #/$defs: keep it there too.
        defs[type.name] = struct_schema(type, flags, defs);
    } else {
        schema.update(root);
    }
    if (!defs.empty()) {
        schema["$defs"] = defs;
    }
    return schema;
}

}  // namespace moteur
