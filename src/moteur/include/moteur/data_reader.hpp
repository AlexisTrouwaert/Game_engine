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
// it is ("monstres/squelettes.json > squelette_guerrier > vie : 0 est sous le minimum 1"), all of
// them at once rather than the first only.

// One problem found in the data.
struct DataIssue {
    enum class Level { Warning, Error };
    Level level = Level::Error;
    std::string where;    // file > row > field
    std::string message;
};

// The problems of a load: errors refuse the data, warnings (an unknown field: a typo?) do not.
class DataIssues {
public:
    void error(std::string where, std::string message);
    void warning(std::string where, std::string message);
    void append(const DataIssues& other);
    void clear() { issues_.clear(); }

    bool has_errors() const { return errors_ > 0; }
    std::size_t errors() const { return errors_; }
    std::size_t warnings() const { return issues_.size() - errors_; }
    const std::vector<DataIssue>& all() const { return issues_; }
    // One line per issue: "erreur : where : message".
    std::string text() const;

private:
    std::vector<DataIssue> issues_;
    std::size_t errors_ = 0;
};

template <typename T>
class DataTable;

// A reference from one row to a row of another table (or the same one), by identifier. Read as an
// identifier, then resolved by the table's link step (DataLinker) into the row itself; a reference
// that does not resolve is an error of the load. Rows never move once loaded (a hot reload keeps
// every row where it was), so a resolved reference stays valid.
template <typename T>
struct DataRef {
    std::string id;                       // as written in the file; empty: no reference
    const DataTable<T>* table = nullptr;  // set by the link step
    std::uint32_t index = 0;

    bool resolved() const { return table != nullptr; }
    explicit operator bool() const { return resolved(); }
    const T& operator*() const { return (*table)[index]; }
    const T* operator->() const { return &(*table)[index]; }
};

// Reads the fields of one JSON object (a row, or an object inside it). Each read records the field
// as known; finish() then warns about the fields nobody read. Keys starting with '_' are comments.
//
//   MonsterData parse_monster(moteur::DataReader& row) {
//       MonsterData m;
//       m.life = row.integer("vie", 1, 100000);                 // required
//       m.speed = row.number("vitesse", 0.0f, 20.0f, 3.0f);     // optional, 3 by default
//       m.loot = row.reference<LootData>("butin", true);        // optional reference
//       return m;
//   }
//
// A missing required field, a value of the wrong type or out of its bounds is an error, and the
// fallback (or a zero value) is returned so that reading can go on and report everything.
class DataReader {
public:
    DataReader(const nlohmann::json& value, std::string where, DataIssues& issues);

    const std::string& where() const { return where_; }
    bool has(std::string_view key) const;
    // A problem the game finds itself (two fields that do not agree), reported at `field`.
    void error(std::string_view field, const std::string& message);
    void warning(std::string_view field, const std::string& message);

    float number(std::string_view key, float min, float max, std::optional<float> fallback = std::nullopt);
    int integer(std::string_view key, int min, int max, std::optional<int> fallback = std::nullopt);
    bool boolean(std::string_view key, std::optional<bool> fallback = std::nullopt);
    std::string text(std::string_view key, std::optional<std::string> fallback = std::nullopt);
    // One of `names`: returns its index.
    int choice(std::string_view key, std::initializer_list<std::string_view> names, std::optional<int> fallback = std::nullopt);
    glm::vec2 vec2(std::string_view key, std::optional<glm::vec2> fallback = std::nullopt);
    glm::vec3 vec3(std::string_view key, std::optional<glm::vec3> fallback = std::nullopt);
    // Three or four numbers (alpha 1 if three).
    glm::vec4 color(std::string_view key, std::optional<glm::vec4> fallback = std::nullopt);
    std::vector<std::string> texts(std::string_view key, bool optional = false);

    template <typename T>
    DataRef<T> reference(std::string_view key, bool optional = false) {
        DataRef<T> ref;
        ref.id = text(key, optional ? std::optional<std::string>(std::string()) : std::nullopt);
        return ref;
    }
    template <typename T>
    std::vector<DataRef<T>> references(std::string_view key, bool optional = false) {
        std::vector<DataRef<T>> refs;
        for (std::string& id : texts(key, optional)) {
            refs.push_back(DataRef<T>{std::move(id)});
        }
        return refs;
    }

    // A nested object, read with its own reader (finish() it too). Missing: an error unless
    // optional, and a reader over an empty object.
    DataReader object(std::string_view key, bool optional = false);
    // A list of objects, each with its reader.
    std::vector<DataReader> objects(std::string_view key, bool optional = false);

    // Warns about the fields that were never read. Call once reading is done.
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
