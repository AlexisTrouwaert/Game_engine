#pragma once

/// @file
/// What the reflection (reflection.hpp) gives for every described type: reading from data and saves
/// with located messages, writing, comparing, resolving the references to data tables, and a JSON
/// schema (milestone 9, part 3). Everything works on a TypeInfo and a pointer, without templates; the
/// typed helpers at the end only find the TypeInfo.
///
/// Values in JSON:
///
/// | Kind | JSON |
/// |---|---|
/// | bool | `true` / `false` |
/// | integer, float | a number (an integer for integers; bounds FieldInfo::min / max checked) |
/// | string | a string |
/// | vec2/3/4, ivec2/3 | an array of 2, 3 or 4 numbers (a colour vec4 may omit its alpha: 1) |
/// | quat | `[w, x, y, z]` |
/// | enumeration | its name (the number of older development saves is still read) |
/// | struct | an object of its fields (keys starting with `_` are comments) |
/// | std::vector | an array |
/// | std::optional | `null` when empty, else its value |
/// | DataRef | the row's identifier |
/// | entity | what ReadOptions::entity / WriteOptions::entity make of it (a persistent identifier in saves, an index in prefabs) |
/// | Asset | what ReadOptions::asset / WriteOptions::asset make of it (the asset's path) |

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "moteur/data_reader.hpp"
#include "moteur/reflection.hpp"

namespace moteur {

/// How write_json() writes.
struct WriteOptions {
    /// The fields written: those with at least one of these flags (FieldFlags).
    std::uint32_t flags = kFieldSave;
    /// Structs: only the fields whose value differs from a default-constructed struct's (what a
    /// prefab's override or a level's instance stores).
    bool skip_defaults = false;
    /// Entity fields: their JSON (the persistent identifier in a save). Empty: `null`.
    std::function<nlohmann::json(entt::entity)> entity;
    /// Asset fields: their JSON, from the field's type (TypeInfo::asset_type) and the Asset handle.
    /// Empty: `null`.
    std::function<nlohmann::json(const TypeInfo& type, const void* handle)> asset;
};

/// How read_json() and read_fields() read.
struct ReadOptions {
    /// The fields read: those with at least one of these flags (FieldFlags). Others in the JSON are
    /// unknown fields.
    std::uint32_t flags = kFieldData;
    /// Warn about keys that are no field (a typo?), as DataReader::finish() does.
    bool warn_unknown = true;
    /// Entity fields: the entity from its JSON; false with a problem when it cannot. Empty: entity
    /// fields are an error to read (null is read as entt::null).
    std::function<bool(const nlohmann::json& value, entt::entity& entity, std::string& problem)> entity;
    /// Asset fields: loads the asset named by `value` into the handle (an Asset<T> of
    /// TypeInfo::asset_type); false with a problem. Empty: asset fields are an error to read.
    std::function<bool(const TypeInfo& type, const nlohmann::json& value, void* handle, std::string& problem)> asset;
    /// DataRef fields: bound to their row in these tables as they are read (a save, read after the
    /// tables were loaded): an identifier with no row is an error. Null: identifiers only, resolved
    /// later by link_references() (the tables' own link step).
    const DataTables* tables = nullptr;
};

/// `value` (an object of `type`) as JSON, with `options` (see the table at the top of the file).
nlohmann::json write_json(const void* value, const TypeInfo& type, const WriteOptions& options = {});

/// Reads `json` into `value` (an object of `type`). Problems go to `issues`, located at `where`
/// ("data/monstres/a.json > squelette"); what reads is kept, what does not keeps its value (a
/// struct's missing fields keep theirs: start from a default object). True without error.
bool read_json(const nlohmann::json& json, void* value, const TypeInfo& type, const std::string& where,
               DataIssues& issues, const ReadOptions& options = {});

/// Reads the fields of `object` (a struct of `type`) from the object `reader` reads: each field from
/// its key (or an alias), missing fields keep their value unless FieldInfo::required. Marks every
/// key it reads; call reader.finish() afterwards to warn about the others (with
/// ReadOptions::warn_unknown, read_json() does it).
void read_fields(DataReader& reader, void* object, const TypeInfo& type, const ReadOptions& options = {});

/// Resolves every DataRef of `object` (fields, nested structs, arrays), each located at its path in
/// `path` (a field's name, "items[2] > loot"): the link step of a table read by the reflection.
/// Optional references: those whose field is not FieldInfo::required.
void link_references(void* object, const TypeInfo& type, DataLinker& linker, const std::string& path = {});

/// Exact comparison (floats bit for bit, as == compares them; strings, arrays and structs field by
/// field; DataRef by identifier; Asset by handle).
bool values_equal(const void* a, const void* b, const TypeInfo& type);

/// A JSON Schema (draft 2020-12) of `type` as data: the fields with one of `flags`, their bounds,
/// units and documentation, enumerations by their names, structs under `$defs`. For the editors and
/// external tools (a text editor that checks a table as it is typed).
nlohmann::json json_schema(const TypeInfo& type, std::uint32_t flags = kFieldData);

/// The number field `field` as a double (Int, Float, Bool), for the tools. 0 for other kinds.
double number_value(const void* value, const TypeInfo& type);
/// Sets a number field (Int, Float, Bool), clamped into the type's range.
void set_number_value(void* value, const TypeInfo& type, double number);

/// Typed helpers.
///@{
template <typename T>
nlohmann::json write_json(const T& value, const WriteOptions& options = {}) {
    return write_json(&value, type_info<T>(), options);
}
template <typename T>
bool read_json(const nlohmann::json& json, T& value, const std::string& where, DataIssues& issues,
               const ReadOptions& options = {}) {
    return read_json(json, &value, type_info<T>(), where, issues, options);
}
template <typename T>
void read_fields(DataReader& reader, T& object, const ReadOptions& options = {}) {
    read_fields(reader, &object, type_info<T>(), options);
}
template <typename T>
void link_references(T& object, DataLinker& linker) {
    link_references(&object, type_info<T>(), linker);
}
template <typename T>
bool values_equal(const T& a, const T& b) {
    return values_equal(&a, &b, type_info<T>());
}
///@}

/// Registers the data table of a described struct `T`: each row read by read_fields() (data fields),
/// its references resolved by link_references(); `check`, when given, then checks what the
/// description cannot say (two fields that must agree, a file's extension) and reports with
/// DataReader::error(). Same rules as DataTables::add().
template <typename T>
DataTable<T>& add_reflected_table(DataTables& tables, std::string name, std::string directory,
                                  std::function<void(T& row, DataReader& reader)> check = {}) {
    DataTable<T>& table = tables.add<T>(
        std::move(name), std::move(directory),
        [check = std::move(check)](DataReader& reader) {
            T row{};
            read_fields(reader, row);
            if (check) {
                check(row, reader);
            }
            return row;
        },
        [](T& row, DataLinker& linker) { link_references(row, linker); });
    nlohmann::json schema = json_schema(type_info<T>());
    // A table's file: {"version": 1, "defaults": {...}, "rows": {"id": row}}; a row may have a base.
    nlohmann::json row = schema;
    row.erase("$schema");
    row.erase("$defs");
    row["properties"]["base"] = {{"type", "string"}, {"description", "the row this one starts from"}};
    nlohmann::json file = {{"$schema", "https://json-schema.org/draft/2020-12/schema"},
                           {"title", "table " + table.name() + " (" + type_info<T>().name + ")"},
                           {"type", "object"},
                           {"required", {"version", "rows"}},
                           {"properties",
                            {{"version", {{"const", 1}}},
                             {"defaults", {{"type", "object"}}},
                             {"rows", {{"type", "object"}, {"additionalProperties", {{"$ref", "#/$defs/row"}}}}}}}};
    nlohmann::json defs = schema.contains("$defs") ? schema["$defs"] : nlohmann::json::object();
    defs["row"] = row;
    file["$defs"] = defs;
    table.set_schema(std::move(file));
    return table;
}

}  // namespace moteur
