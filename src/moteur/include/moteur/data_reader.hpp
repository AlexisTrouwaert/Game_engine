#pragma once

#include <nlohmann/json.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace moteur {

// Typed reading of game data (milestone 7, part 2). The game describes the fields of each table in
// C++, with these readers: reading a field is checking it, and every problem is reported with where
// it is ("data/monstres/squelettes.json > squelette_guerrier > vie: 0 out of range [1, 100000]"),
// all of them at once rather than the first only.

/// One problem found in the data.
struct DataIssue {
    /// How serious: an Error refuses the data (the load or reload fails), a Warning is only reported.
    enum class Level { Warning, Error };
    Level level = Level::Error;  ///< Warning or Error
    std::string where;    ///< file > row > field
    std::string message;  ///< what is wrong, in English, for the log and the console
};

/// The problems of a load: errors refuse the data, warnings (an unknown field: a typo?) do not.
/// Issues are kept in the order they were found.
class DataIssues {
public:
    /// Records an error at `where` (file > row > field).
    void error(std::string where, std::string message);
    /// Records a warning at `where` (file > row > field).
    void warning(std::string where, std::string message);
    /// Appends a copy of every issue of `other`, after this one's.
    void append(const DataIssues& other);
    /// Forgets every issue (errors() and warnings() go back to 0).
    void clear() {
        issues_.clear();
        errors_ = 0;
    }

    /// True if at least one error was recorded.
    bool has_errors() const { return errors_ > 0; }
    /// How many issues are errors.
    std::size_t errors() const { return errors_; }
    /// How many issues are warnings.
    std::size_t warnings() const { return issues_.size() - errors_; }
    /// Every issue, in the order found.
    const std::vector<DataIssue>& all() const { return issues_; }
    /// One line per issue: "error: where: message" (or "warning: ..."), each ending with a newline.
    std::string text() const;

private:
    std::vector<DataIssue> issues_;
    std::size_t errors_ = 0;
};

template <typename T>
class DataTable;

/// A reference from one row to a row of another table (or the same one), by identifier. Read as an
/// identifier (DataReader::reference()), then resolved by the table's link step
/// (DataLinker::resolve()) into the row itself; a reference that does not resolve is an error of the
/// load. Rows keep their index once loaded (a hot reload keeps every row at its index), so a
/// resolved reference stays valid, and dereferencing it always gives the row's current content.
template <typename T>
struct DataRef {
    std::string id;                       ///< as written in the file; empty: no reference
    const DataTable<T>* table = nullptr;  ///< set by the link step
    std::uint32_t index = 0;              ///< the row's index in #table, set by the link step

    /// True once the link step found the row (false for an empty or unknown identifier).
    bool resolved() const { return table != nullptr; }
    /// resolved().
    explicit operator bool() const { return resolved(); }
    /// The row. Only on a resolved reference (undefined behaviour otherwise). The reference returned
    /// is invalidated by a reload of #table: dereference again rather than keeping it.
    const T& operator*() const { return (*table)[index]; }
    /// The row's members; same rules as operator*().
    const T* operator->() const { return &(*table)[index]; }
};

/// Reads the fields of one JSON object (a row, or an object inside it). Each read records the field
/// as known; finish() then warns about the fields nobody read. Keys starting with '_' are comments.
///
/// @code{.cpp}
/// MonsterData parse_monster(moteur::DataReader& row) {
///     MonsterData m;
///     m.life = row.integer("vie", 1, 100000);                 // required
///     m.speed = row.number("vitesse", 0.0f, 20.0f, 3.0f);     // optional, 3 by default
///     m.loot = row.reference<LootData>("butin", true);        // optional reference
///     return m;
/// }
/// @endcode
///
/// A missing required field, a value of the wrong type or out of its bounds is an error, and a value
/// is still returned so that reading can go on and report everything: the fallback if there is one,
/// otherwise a default (the bound `min` for numbers, clamped into the bounds when out of them; false,
/// "", 0, zero vectors, opaque white for colours). A JSON `null` counts as missing. A field without
/// fallback is required; one with a fallback is optional. Each problem is located at
/// `where() > key`.
///
/// The reader keeps pointers to the JSON value and to the DataIssues it was given: both must
/// outlive it. Not thread-safe.
class DataReader {
public:
    /// A reader over `value`, located at `where` in messages, reporting to `issues`. A `value` that
    /// is not an object is an error, and the reader then reads an empty object.
    DataReader(const nlohmann::json& value, std::string where, DataIssues& issues);

    /// Where this object is, for messages ("data/monstres/squelettes.json > squelette").
    const std::string& where() const { return where_; }
    /// Whether the object has the key (even with a null value). Does not count as reading it.
    bool has(std::string_view key) const;
    /// A problem the game finds itself (two fields that do not agree), reported at `field`.
    void error(std::string_view field, const std::string& message);
    /// A warning the game finds itself, reported at `field`; does not refuse the data.
    void warning(std::string_view field, const std::string& message);

    /// A number in [`min`, `max`] (an integer in the file is accepted). Not finite or out of the
    /// bounds: an error.
    float number(std::string_view key, float min, float max, std::optional<float> fallback = std::nullopt);
    /// An integer in [`min`, `max`]; a number written with a fraction or an exponent (`2.5`, `2.0`)
    /// is an error.
    int integer(std::string_view key, int min, int max, std::optional<int> fallback = std::nullopt);
    /// `true` or `false` (not 0 or 1).
    bool boolean(std::string_view key, std::optional<bool> fallback = std::nullopt);
    /// A string, as is (UTF-8).
    std::string text(std::string_view key, const std::optional<std::string>& fallback = std::nullopt);
    /// One of `names`: returns its index. Another string, or not a string, is an error naming the
    /// allowed values (the fallback, or 0, is returned).
    int choice(std::string_view key, std::initializer_list<std::string_view> names, std::optional<int> fallback = std::nullopt);
    /// An array of exactly two numbers.
    glm::vec2 vec2(std::string_view key, std::optional<glm::vec2> fallback = std::nullopt);
    /// An array of exactly three numbers.
    glm::vec3 vec3(std::string_view key, std::optional<glm::vec3> fallback = std::nullopt);
    /// Three or four numbers (alpha 1 if three), as written (no colour space conversion).
    glm::vec4 color(std::string_view key, std::optional<glm::vec4> fallback = std::nullopt);
    /// An array of strings. Missing: an error unless `optional`, and an empty list. An element that
    /// is not a string is an error and left out.
    std::vector<std::string> texts(std::string_view key, bool optional = false);

    /// A reference to a row of the table of `T`, read as its identifier (a string); resolved later
    /// by the link step (DataLinker::resolve()). With `optional`, a missing field gives an empty
    /// reference.
    template <typename T>
    DataRef<T> reference(std::string_view key, bool optional = false) {
        DataRef<T> ref;
        ref.id = text(key, optional ? std::optional<std::string>(std::string()) : std::nullopt);
        return ref;
    }
    /// A list of references, read as an array of identifiers (see texts() and reference()).
    template <typename T>
    std::vector<DataRef<T>> references(std::string_view key, bool optional = false) {
        std::vector<DataRef<T>> refs;
        for (std::string& id : texts(key, optional)) {
            refs.push_back(DataRef<T>{std::move(id)});
        }
        return refs;
    }

    /// A nested object, read with its own reader (finish() it too), located at `where() > key`.
    /// Missing: an error unless optional, and a reader over an empty object.
    DataReader object(std::string_view key, bool optional = false);
    /// A list of objects, each with its reader (located at `where() > key[i]`; finish() each).
    /// Missing: an error unless `optional`, and an empty list. Not an array: an error and an empty
    /// list.
    std::vector<DataReader> objects(std::string_view key, bool optional = false);

    /// Warns about the fields that were never read. Call once reading is done.
    void finish();

private:
    const nlohmann::json* field(std::string_view key, bool required);
    std::string at(std::string_view key) const;

    const nlohmann::json* value_;
    std::string where_;
    DataIssues* issues_;
    std::vector<std::string> read_;
};

}  // namespace moteur
