#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include "moteur/data_reader.hpp"

namespace moteur {

// Tables of game data (milestone 7, part 2): monsters, items, skills, effects... described in JSON,
// checked at load, referenced between them by identifier, reloaded while the game runs, and
// compiled to a binary form for the released game. The engine knows no field: the game gives, per
// table, a function that reads a row (DataReader) and, if the rows refer to other rows, one that
// resolves those references (DataLinker).
//
// A table is a directory of JSON files (assets/data/<table>/*.json), all their rows together:
//
//   {
//     "version": 1,
//     "defaults": { "vitesse": 3 },             // for the rows of this file without a base
//     "rows": {
//       "squelette": { "vie": 30, "vitesse": 2 },
//       "squelette_guerrier": { "base": "squelette", "vie": 50 },   // the other fields of "squelette"
//       "_note": "keys starting with _ are comments, here and in rows"
//     }
//   }
//
// Rules: identifiers are unique across the directory; a row with "base" starts from that row
// (resolved first; several levels; cycles refused; the base may be in another file of the table),
// otherwise from its file's "defaults"; then its own fields replace those at the first level (an
// object inside a row replaces the base's object as a whole).
//
// Order: rows get their index in the order of their identifiers at the first load; a hot reload
// keeps every existing row at its index and adds new ones at the end, so references stay valid.
// What must not depend on how the data was edited (a draw of loot) iterates in sorted(): the order
// of the identifiers, the same on every machine. A reload that removes a row is refused (a row may
// be in use): restart to remove rows.

class DataTables;

/// The rows of a table as read from its files, before the game's reading: identifiers and resolved
/// JSON objects (defaults and inheritance applied, comment keys removed). What the binary form
/// stores. The four vectors of rows are parallel (same size, same order).
struct DataRows {
    std::vector<std::string> ids;           ///< sorted
    std::vector<nlohmann::json> rows;       ///< each row's resolved JSON object, in the order of #ids
    std::vector<std::string> row_files;     ///< the file of each row (for messages)
    std::vector<std::string> files;         ///< the table's files, as asset keys, sorted
    std::uint64_t source_hash = 0;          ///< of the files' bytes
};

/// The type-independent part of a table: identifiers, files and resolved JSON of its rows, without
/// the game's type (see DataTable). Not copyable. Main thread, like DataTables.
class DataTableBase {
public:
    /// An empty table (not loaded()). Tables are made by DataTables::add().
    DataTableBase(std::string name, std::string directory) : name_(std::move(name)), directory_(std::move(directory)) {}
    virtual ~DataTableBase() = default;
    DataTableBase(const DataTableBase&) = delete;
    DataTableBase& operator=(const DataTableBase&) = delete;

    /// The table's name, as registered: the name of its compiled file (`<name>.mdat`) and of the
    /// `reload` command's argument ("monstres").
    const std::string& name() const { return name_; }
    /// Inside the assets directory, without a trailing separator ("data/monstres").
    const std::string& directory() const { return directory_; }
    /// The number of rows (0 until loaded).
    std::size_t size() const { return ids_.size(); }
    /// The identifier of the row at `index` (< size(); not checked).
    const std::string& id(std::uint32_t index) const { return ids_[index]; }
    /// The index of the row `id`, or nullopt. A hash lookup (allocates a string for the key).
    std::optional<std::uint32_t> index_of(std::string_view id) const;
    /// Row indices in the order of their identifiers.
    const std::vector<std::uint32_t>& sorted() const { return sorted_; }
    /// The row as the game's reader saw it (defaults and inheritance applied), for the tools.
    const nlohmann::json& resolved(std::uint32_t index) const { return resolved_[index]; }
    /// The file the row at `index` came from, as an asset key ("data/monstres/squelettes.json").
    const std::string& row_file(std::uint32_t index) const { return row_files_[index]; }
    /// Every JSON file of the table's directory, as asset keys, sorted (those that failed too).
    const std::vector<std::string>& files() const { return files_; }
    /// A 64-bit FNV-1a hash of the files' keys and bytes; from compiled data, the hash of the JSON
    /// it was compiled from (so a tool can tell whether the compiled data is out of date).
    std::uint64_t source_hash() const { return source_hash_; }
    /// Grows by one at each successful reload (not at DataTables::load_all()): a system that keeps
    /// something computed from the rows compares it to rebuild that.
    std::uint64_t revision() const { return revision_; }
    /// True once a load or a reload has given the table its rows; false if every load failed.
    bool loaded() const { return loaded_; }
    /// The JSON Schema of a row (milestone 9, part 3): given by add_reflected_table() from the row's
    /// description; null for a table read by hand.
    const nlohmann::json& schema() const { return schema_; }
    /// Gives the table its schema (see schema()).
    void set_schema(nlohmann::json schema) { schema_ = std::move(schema); }

    /// Reads the JSON files of `directory` under `root` (the assets directory, ending with a
    /// separator), not its subdirectories, in the order of their names. Problems go to `issues`
    /// (named after the table `name`); rows with problems are left out, and so are the rows of a
    /// file that cannot be read or has no `"version": 1`. Blocking file I/O.
    static DataRows read_json(const std::string& root, const std::string& directory, const std::string& name,
                              DataIssues& issues);
    /// The binary form of a table: `MDAT`, format version (u32, little-endian), then the table's
    /// name and rows in CBOR.
    static std::vector<std::uint8_t> encode(const std::string& name, const DataRows& rows);
    /// Reads the binary form written by encode(). Not that form, another format version, another
    /// table's name or damaged content: an error in `issues` (at `<name>.mdat`) and empty rows.
    static DataRows decode(const std::vector<std::uint8_t>& bytes, const std::string& name, DataIssues& issues);

protected:
    friend class DataTables;
    friend class DataLinker;

    /// Reads every row with the game's function into a fresh table of the same type (nothing of
    /// this one changes). Null if a row has an error.
    virtual std::unique_ptr<DataTableBase> build(DataRows&& rows, DataIssues& issues) const = 0;
    /// Resolves the references of every row (the game's link function). `reloading` and `fresh`:
    /// see DataLinker's constructor (null for a whole load).
    virtual void link(DataTables& tables, DataIssues& issues, const DataTableBase* reloading,
                      const DataTableBase* fresh) = 0;
    /// Puts the rows in `order` (order[i]: the current index of the row that goes to index i).
    virtual void reorder(const std::vector<std::uint32_t>& order);
    /// Takes the whole content of `fresh` (same type), already in its final order.
    virtual void take(DataTableBase& fresh);

    /// Takes the identifiers, resolved rows and files of `rows`, and rebuilds the index.
    void set_rows(DataRows&& rows);
    /// The order to give `fresh`'s rows so that this table's rows keep their index and new ones come
    /// after (sorted). False, with an error, if one of this table's rows is missing from `fresh`.
    bool stable_order(const DataTableBase& fresh, std::vector<std::uint32_t>& order, DataIssues& issues) const;
    /// Recomputes #index_ and #sorted_ from #ids_.
    void rebuild_index();

    std::string name_;                                    ///< see name()
    std::string directory_;                               ///< see directory()
    std::vector<std::string> ids_;                        ///< identifier of each row, by index
    std::vector<nlohmann::json> resolved_;                ///< resolved JSON of each row, by index
    std::vector<std::string> row_files_;                  ///< source file of each row, by index
    std::vector<std::string> files_;                      ///< see files()
    std::unordered_map<std::string, std::uint32_t> index_;  ///< identifier to index
    std::vector<std::uint32_t> sorted_;                   ///< see sorted()
    std::uint64_t source_hash_ = 0;                       ///< see source_hash()
    std::uint64_t revision_ = 0;                          ///< see revision()
    bool loaded_ = false;                                 ///< see loaded()
    nlohmann::json schema_;                               ///< see schema()
};

/// Resolves the references of one row (given to the game's link function, DataTable::Link). Valid
/// only during that call.
class DataLinker {
public:
    /// Made by the tables for each row: `where` locates the row in messages ("file > row");
    /// `reloading` is the table being reloaded (null for a whole load) and `fresh` its new content,
    /// in which references to that table are looked up.
    DataLinker(DataTables& tables, DataIssues& issues, std::string where, const DataTableBase* reloading,
               const DataTableBase* fresh)
        : tables_(tables), issues_(issues), where_(std::move(where)), reloading_(reloading), fresh_(fresh) {}

    /// Resolves `ref` (read from `field`) into its row. An empty identifier is an error unless
    /// optional. An unknown identifier is an error naming both sides, and so is a type `T` that has
    /// no table. On any failure the reference stays unresolved.
    template <typename T>
    void resolve(DataRef<T>& ref, std::string_view field, bool optional = false);
    /// Resolves every reference of a list (DataReader::references()); an empty identifier in it is an
    /// error.
    template <typename T>
    void resolve(std::vector<DataRef<T>>& refs, std::string_view field) {
        for (DataRef<T>& ref : refs) {
            resolve(ref, field);
        }
    }

private:
    DataTables& tables_;
    DataIssues& issues_;
    std::string where_;
    const DataTableBase* reloading_;  ///< the table being reloaded, if any
    const DataTableBase* fresh_;      ///< its new content: references to it are looked up there
};

/// A table of rows of the game's type `T` (a plain struct: MonsterData, ItemData...), registered
/// with DataTables::add(). Row `i` of rows() is the row of id(i). Every load or reload builds
/// fresh rows with the Parse function, then resolves their references with the Link function.
///
/// Pointers and references to rows (operator[](), rows(), find(), at()) are invalidated by a reload
/// of this table; indices and DataRef stay valid (rows keep their index). Main thread.
template <typename T>
class DataTable final : public DataTableBase {
public:
    /// Reads one row: called once per row at each load and reload, its fields read from `row`
    /// (DataReader::finish() is called after it). Errors reported to `row` refuse the load.
    using Parse = std::function<T(DataReader& row)>;
    /// Resolves the references of one row (DataLinker::resolve() on each DataRef), once every table
    /// is read. Optional: an empty function for rows without references.
    using Link = std::function<void(T& row, DataLinker& linker)>;

    /// An empty table; use DataTables::add(), which owns its tables.
    DataTable(std::string name, std::string directory, Parse parse, Link link)
        : DataTableBase(std::move(name), std::move(directory)), parse_(std::move(parse)), link_(std::move(link)) {}

    /// The row at `index` (< size(); not checked).
    const T& operator[](std::uint32_t index) const { return rows_[index]; }
    /// Every row, by index.
    const std::vector<T>& rows() const { return rows_; }
    /// The row `id`, or null if there is none.
    const T* find(std::string_view id) const {
        const auto index = index_of(id);
        return index ? &rows_[*index] : nullptr;
    }
    /// Throws std::out_of_range naming the table if there is no such row.
    const T& at(std::string_view id) const {
        if (const T* row = find(id)) {
            return *row;
        }
        throw std::out_of_range("Table '" + name_ + "': no row '" + std::string(id) + "'");
    }
    /// A reference to a row, for the game's code (a monster spawned by name); unresolved if there is
    /// no such row.
    DataRef<T> ref(std::string_view id) const {
        DataRef<T> r{std::string(id)};
        if (const auto index = index_of(id)) {
            r.table = this;
            r.index = *index;
        }
        return r;
    }

protected:
    std::unique_ptr<DataTableBase> build(DataRows&& rows, DataIssues& issues) const override {
        auto fresh = std::make_unique<DataTable<T>>(name_, directory_, parse_, link_);
        DataIssues found;
        fresh->rows_.reserve(rows.rows.size());
        for (std::size_t i = 0; i < rows.rows.size(); ++i) {
            DataReader reader(rows.rows[i], rows.row_files[i] + " > " + rows.ids[i], found);
            fresh->rows_.push_back(parse_(reader));
            reader.finish();
        }
        const bool ok = !found.has_errors();
        issues.append(found);
        if (!ok) {
            return nullptr;
        }
        fresh->set_rows(std::move(rows));
        return fresh;
    }

    void link(DataTables& tables, DataIssues& issues, const DataTableBase* reloading, const DataTableBase* fresh) override {
        if (!link_) {
            return;
        }
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            DataLinker linker(tables, issues, row_files_[i] + " > " + ids_[i], reloading, fresh);
            link_(rows_[i], linker);
        }
    }

    void reorder(const std::vector<std::uint32_t>& order) override {
        std::vector<T> rows;
        rows.reserve(order.size());
        for (const std::uint32_t from : order) {
            rows.push_back(std::move(rows_[from]));
        }
        rows_ = std::move(rows);
        DataTableBase::reorder(order);
    }

    void take(DataTableBase& fresh_base) override {
        auto& fresh = static_cast<DataTable<T>&>(fresh_base);
        rows_ = std::move(fresh.rows_);
        DataTableBase::take(fresh);
    }

private:
    Parse parse_;
    Link link_;
    std::vector<T> rows_;
};

/// Where the tables come from.
enum class DataSource {
    Json,      ///< the `*.json` files of each table's directory (`data/<table>/`): checked at load, reloaded while the game runs (development)
    Compiled,  ///< `<compiled directory><name>.mdat`, written by DataTables::compile(): the binary form (released game)
};

/// Every table of the game (Application::data()). The game registers its tables (add()), then loads
/// them all (load_all()); a changed file of a table reloads it (hot reload, through Assets:
/// reload_file()). Not thread-safe: the main thread.
class DataTables {
public:
    /// Registers the table of `T`, named `name` (its compiled file and its name for reload()), read
    /// from `directory` (inside the assets directory, without a trailing separator: "data/monstres")
    /// with `parse`, its references resolved with `link`. Empty until load_all(). The table is owned
    /// by this object; the reference returned stays valid as long as it. Throws std::logic_error if
    /// the name or the type is already registered.
    template <typename T>
    DataTable<T>& add(std::string name, std::string directory, typename DataTable<T>::Parse parse,
                      typename DataTable<T>::Link link = {}) {
        if (find(name) != nullptr || by_type_.count(std::type_index(typeid(T))) != 0) {
            throw std::logic_error("DataTables: table '" + name + "' or its type registered twice");
        }
        auto table = std::make_unique<DataTable<T>>(std::move(name), std::move(directory), std::move(parse), std::move(link));
        DataTable<T>& result = *table;
        by_type_.emplace(std::type_index(typeid(T)), table.get());
        tables_.push_back(std::move(table));
        return result;
    }

    /// The table of `T`. Throws std::logic_error if the type has no table.
    ///@{
    template <typename T>
    DataTable<T>& get() {
        const auto found = by_type_.find(std::type_index(typeid(T)));
        if (found == by_type_.end()) {
            throw std::logic_error(std::string("DataTables: no table of type ") + typeid(T).name());
        }
        return static_cast<DataTable<T>&>(*found->second);
    }
    template <typename T>
    const DataTable<T>& get() const {
        return const_cast<DataTables*>(this)->get<T>();
    }
    ///@}
    /// Whether a table of `T` is registered.
    template <typename T>
    bool has() const {
        return by_type_.count(std::type_index(typeid(T))) != 0;
    }
    /// The table named `name`, or null. O(number of tables).
    DataTableBase* find(std::string_view name);
    /// Every table, in the order of registration.
    const std::vector<std::unique_ptr<DataTableBase>>& tables() const { return tables_; }

    /// Where the next loads read from (DataSource::Json by default). With DataSource::Compiled,
    /// `compiled_directory` is the folder of the `.mdat` files, ending with a separator; they are
    /// found in the mounted archives first (read_file()). Set before load_all().
    void set_source(DataSource source, std::string compiled_directory = {});
    /// Where the tables are read from (see set_source()).
    DataSource source() const { return source_; }

    /// Loads every registered table (from `root`, the assets directory, ending with a separator),
    /// then resolves the references. Returns the problems (also kept: last_issues()). With errors,
    /// the tables that loaded stay usable; the game decides whether to go on. A table with an error
    /// in one of its rows keeps no row (it stays empty, or keeps its previous content); a file or row
    /// that cannot be read is left out and the rest of its table loads; a reference that does not
    /// resolve is an error and stays unresolved. Meant to be called once, at
    /// start-up: rows get new indices. Blocking file I/O; logs a summary and every issue.
    const DataIssues& load_all(const std::string& root);
    /// Reloads the table owning the asset `key` (a file `data/<table>/x.json` directly in a table's
    /// directory), if any: checked and linked first; on any error the previous content stays (see
    /// last_issues()). Returns the table concerned, even when its reload failed; null if no table
    /// owns `key` or the source is DataSource::Compiled.
    DataTableBase* reload_file(const std::string& root, const std::string& key);
    /// Reloads one table by name (`reload` command), as reload_file() does. False on errors, an
    /// unknown name included (see last_issues()).
    bool reload(const std::string& root, const std::string& name);
    /// The problems of the last load_all(), reload_file() or reload() (each replaces them).
    const DataIssues& last_issues() const { return issues_; }

    /// Writes the binary form of every table into `directory` (ending with a separator; created if
    /// needed) as `<name>.mdat`, rows in the order of their identifiers. First loads every table from
    /// JSON (load_all(), whatever source()): the tables' content is replaced. False, with the
    /// problems appended to `issues`, if that load has an error or a file cannot be written; the
    /// load's warnings are appended too.
    bool compile(const std::string& root, const std::string& directory, DataIssues& issues);

    /// Writes the schema of every table that has one (DataTableBase::schema()) into `directory`
    /// (ending with a separator; created if needed) as `<name>.schema.json`, for a text editor that
    /// checks the files of a table as they are typed (VS Code: `json.schemas`). False, with a
    /// message in `error`, if a file cannot be written. Returns how many were written in `count`.
    bool write_schemas(const std::string& directory, std::size_t& count, std::string& error) const;

private:
    friend class DataLinker;
    DataRows read(const std::string& root, const DataTableBase& table, DataIssues& issues) const;
    bool reload_table(const std::string& root, DataTableBase& table);

    std::vector<std::unique_ptr<DataTableBase>> tables_;
    std::unordered_map<std::type_index, DataTableBase*> by_type_;
    DataSource source_ = DataSource::Json;
    std::string compiled_directory_;
    DataIssues issues_;
};

template <typename T>
void DataLinker::resolve(DataRef<T>& ref, std::string_view field, bool optional) {
    ref.table = nullptr;
    if (ref.id.empty()) {
        if (!optional) {
            issues_.error(where_ + " > " + std::string(field), "empty reference");
        }
        return;
    }
    if (!tables_.has<T>()) {
        issues_.error(where_ + " > " + std::string(field), std::string("no table for this reference type"));
        return;
    }
    const DataTable<T>& target = tables_.get<T>();
    // While a table reloads, its references to itself look in its new content, whose rows will
    // take the same indices once taken (see DataTables::reload_table).
    const DataTableBase& lookup = (&target == reloading_ && fresh_ != nullptr) ? *fresh_ : target;
    const auto index = lookup.index_of(ref.id);
    if (!index) {
        issues_.error(where_ + " > " + std::string(field),
                      "'" + ref.id + "' does not exist in the table " + target.name());
        return;
    }
    ref.table = &target;
    ref.index = *index;
}

}  // namespace moteur
