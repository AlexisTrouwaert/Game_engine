#include "moteur/data_table.hpp"

#include "moteur/profiler.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace moteur {

namespace {

constexpr int kTableVersion = 1;
constexpr std::uint32_t kBinaryVersion = 1;
const char kMagic[4] = {'M', 'D', 'A', 'T'};

std::filesystem::path utf8_path(const std::string& text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

std::string path_utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

bool read_bytes(const std::filesystem::path& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    std::ostringstream text;
    text << file.rdbuf();
    out = text.str();
    return true;
}

void hash_bytes(std::uint64_t& hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash = (hash ^ bytes[i]) * 1099511628211ull;
    }
}

// Keys starting with '_' are comments: they do not reach the game's reader.
void drop_comments(nlohmann::json& object) {
    for (auto it = object.begin(); it != object.end();) {
        if (!it.key().empty() && it.key()[0] == '_') {
            it = object.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace

std::optional<std::uint32_t> DataTableBase::index_of(std::string_view id) const {
    const auto found = index_.find(std::string(id));
    if (found == index_.end()) {
        return std::nullopt;
    }
    return found->second;
}

DataRows DataTableBase::read_json(const std::string& root, const std::string& directory, const std::string& name,
                                  DataIssues& issues) {
    DataRows result;
    result.source_hash = 14695981039346656037ull;
    const std::filesystem::path folder = utf8_path(root + directory);
    std::error_code error;
    if (!std::filesystem::is_directory(folder, error)) {
        issues.error(directory, "dossier de la table " + name + " introuvable");
        return result;
    }
    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            names.push_back(path_utf8(entry.path().filename()));
        }
    }
    std::sort(names.begin(), names.end());

    // Every row of every file, before inheritance.
    struct Raw {
        nlohmann::json row;
        std::string file;
        std::size_t defaults;  // index in `defaults`
    };
    std::map<std::string, Raw> raws;  // sorted by identifier
    std::vector<nlohmann::json> defaults;
    for (const std::string& file_name : names) {
        const std::string key = directory + "/" + file_name;
        result.files.push_back(key);
        std::string bytes;
        if (!read_bytes(folder / utf8_path(file_name), bytes)) {
            issues.error(key, "lecture impossible");
            continue;
        }
        hash_bytes(result.source_hash, key.data(), key.size());
        hash_bytes(result.source_hash, bytes.data(), bytes.size());
        nlohmann::json json;
        try {
            json = nlohmann::json::parse(bytes);
        } catch (const nlohmann::json::exception& e) {
            issues.error(key, std::string("JSON invalide : ") + e.what());
            continue;
        }
        if (!json.is_object() || !json.contains("version") || json["version"] != kTableVersion) {
            issues.error(key, "attendu un objet avec \"version\": " + std::to_string(kTableVersion));
            continue;
        }
        nlohmann::json file_defaults = nlohmann::json::object();
        if (json.contains("defaults")) {
            if (!json["defaults"].is_object()) {
                issues.error(key + " > defaults", "attendu un objet");
                continue;
            }
            file_defaults = json["defaults"];
            drop_comments(file_defaults);
        }
        if (!json.contains("rows") || !json["rows"].is_object()) {
            issues.error(key, "attendu un objet \"rows\"");
            continue;
        }
        for (const auto& [field, value] : json.items()) {
            if (field != "version" && field != "defaults" && field != "rows" && !(field.size() > 0 && field[0] == '_')) {
                issues.warning(key + " > " + field, "champ inconnu au niveau du fichier");
            }
        }
        defaults.push_back(std::move(file_defaults));
        for (const auto& [id, row] : json["rows"].items()) {
            if (!id.empty() && id[0] == '_') {
                continue;  // a comment
            }
            if (id.empty()) {
                issues.error(key, "une ligne a un identifiant vide");
                continue;
            }
            if (!row.is_object()) {
                issues.error(key + " > " + id, "une ligne doit être un objet");
                continue;
            }
            if (const auto found = raws.find(id); found != raws.end()) {
                issues.error(key + " > " + id, "identifiant déjà défini dans " + found->second.file);
                continue;
            }
            raws.emplace(id, Raw{row, key, defaults.size() - 1});
        }
    }

    // Inheritance: each row resolved once, its base first.
    enum class State { Todo, Doing, Done, Failed };
    std::map<std::string, State> state;
    std::map<std::string, nlohmann::json> resolved;
    std::function<bool(const std::string&)> resolve = [&](const std::string& id) -> bool {
        State& s = state[id];
        if (s == State::Done) {
            return true;
        }
        if (s == State::Failed) {
            return false;
        }
        const Raw& raw = raws.at(id);
        if (s == State::Doing) {
            issues.error(raw.file + " > " + id + " > base", "héritage circulaire");
            s = State::Failed;
            return false;
        }
        s = State::Doing;
        nlohmann::json row;
        if (raw.row.contains("base")) {
            const nlohmann::json& base = raw.row["base"];
            if (!base.is_string() || raws.count(base.get<std::string>()) == 0) {
                issues.error(raw.file + " > " + id + " > base",
                             base.is_string() ? "« " + base.get<std::string>() + " » n'existe pas dans la table " + name
                                              : "attendu l'identifiant d'une ligne");
                state[id] = State::Failed;
                return false;
            }
            if (!resolve(base.get<std::string>())) {
                if (state[id] != State::Failed) {
                    issues.error(raw.file + " > " + id + " > base", "sa base « " + base.get<std::string>() + " » est invalide");
                }
                state[id] = State::Failed;
                return false;
            }
            row = resolved.at(base.get<std::string>());
        } else {
            row = defaults[raw.defaults];
        }
        for (const auto& [field, value] : raw.row.items()) {
            if (field != "base") {
                row[field] = value;
            }
        }
        drop_comments(row);
        resolved[id] = std::move(row);
        state[id] = State::Done;
        return true;
    };
    for (const auto& [id, raw] : raws) {
        if (resolve(id)) {
            result.ids.push_back(id);
            result.rows.push_back(resolved.at(id));
            result.row_files.push_back(raw.file);
        }
    }
    return result;
}

std::vector<std::uint8_t> DataTableBase::encode(const std::string& name, const DataRows& rows) {
    nlohmann::json payload;
    payload["table"] = name;
    payload["source_hash"] = rows.source_hash;
    payload["ids"] = rows.ids;
    payload["rows"] = rows.rows;
    payload["row_files"] = rows.row_files;
    payload["files"] = rows.files;
    const std::vector<std::uint8_t> cbor = nlohmann::json::to_cbor(payload);
    std::vector<std::uint8_t> bytes(kMagic, kMagic + 4);
    for (int i = 0; i < 4; ++i) {
        bytes.push_back(static_cast<std::uint8_t>((kBinaryVersion >> (8 * i)) & 0xFF));
    }
    bytes.insert(bytes.end(), cbor.begin(), cbor.end());
    return bytes;
}

DataRows DataTableBase::decode(const std::vector<std::uint8_t>& bytes, const std::string& name, DataIssues& issues) {
    DataRows rows;
    if (bytes.size() < 8 || !std::equal(kMagic, kMagic + 4, bytes.begin())) {
        issues.error(name + ".mdat", "pas une table compilée");
        return rows;
    }
    std::uint32_t version = 0;
    for (int i = 0; i < 4; ++i) {
        version |= static_cast<std::uint32_t>(bytes[static_cast<std::size_t>(4 + i)]) << (8 * i);
    }
    if (version != kBinaryVersion) {
        issues.error(name + ".mdat", "version " + std::to_string(version) + " du format compilé inconnue (attendu " +
                                         std::to_string(kBinaryVersion) + ") : recompiler les données");
        return rows;
    }
    try {
        const nlohmann::json payload = nlohmann::json::from_cbor(bytes.begin() + 8, bytes.end());
        if (payload.at("table").get<std::string>() != name) {
            issues.error(name + ".mdat", "contient la table " + payload.at("table").get<std::string>());
            return rows;
        }
        rows.source_hash = payload.at("source_hash").get<std::uint64_t>();
        rows.ids = payload.at("ids").get<std::vector<std::string>>();
        rows.rows = payload.at("rows").get<std::vector<nlohmann::json>>();
        rows.row_files = payload.at("row_files").get<std::vector<std::string>>();
        rows.files = payload.at("files").get<std::vector<std::string>>();
        if (rows.rows.size() != rows.ids.size() || rows.row_files.size() != rows.ids.size()) {
            issues.error(name + ".mdat", "contenu incohérent");
            return DataRows{};
        }
    } catch (const nlohmann::json::exception& e) {
        issues.error(name + ".mdat", std::string("illisible : ") + e.what());
        return DataRows{};
    }
    return rows;
}

void DataTableBase::set_rows(DataRows&& rows) {
    ids_ = std::move(rows.ids);
    resolved_ = std::move(rows.rows);
    row_files_ = std::move(rows.row_files);
    files_ = std::move(rows.files);
    source_hash_ = rows.source_hash;
    rebuild_index();
}

void DataTableBase::rebuild_index() {
    index_.clear();
    for (std::uint32_t i = 0; i < ids_.size(); ++i) {
        index_.emplace(ids_[i], i);
    }
    sorted_.resize(ids_.size());
    for (std::uint32_t i = 0; i < ids_.size(); ++i) {
        sorted_[i] = i;
    }
    std::sort(sorted_.begin(), sorted_.end(), [this](std::uint32_t a, std::uint32_t b) { return ids_[a] < ids_[b]; });
}

bool DataTableBase::stable_order(const DataTableBase& fresh, std::vector<std::uint32_t>& order, DataIssues& issues) const {
    order.clear();
    std::vector<std::uint8_t> taken(fresh.size(), 0);
    bool ok = true;
    for (std::uint32_t i = 0; i < ids_.size(); ++i) {
        const auto index = fresh.index_of(ids_[i]);
        if (!index) {
            issues.error(row_files_[i] + " > " + ids_[i],
                         "ligne retirée : rechargement refusé (une ligne peut être utilisée ; redémarrer pour retirer des lignes)");
            ok = false;
            continue;
        }
        order.push_back(*index);
        taken[*index] = 1;
    }
    if (!ok) {
        order.clear();
        return false;
    }
    for (const std::uint32_t index : fresh.sorted_) {
        if (!taken[index]) {
            order.push_back(index);
        }
    }
    return true;
}

void DataTableBase::reorder(const std::vector<std::uint32_t>& order) {
    std::vector<std::string> ids, files;
    std::vector<nlohmann::json> resolved;
    ids.reserve(order.size());
    for (const std::uint32_t from : order) {
        ids.push_back(std::move(ids_[from]));
        resolved.push_back(std::move(resolved_[from]));
        files.push_back(std::move(row_files_[from]));
    }
    ids_ = std::move(ids);
    resolved_ = std::move(resolved);
    row_files_ = std::move(files);
    rebuild_index();
}

void DataTableBase::take(DataTableBase& fresh) {
    ids_ = std::move(fresh.ids_);
    resolved_ = std::move(fresh.resolved_);
    row_files_ = std::move(fresh.row_files_);
    files_ = std::move(fresh.files_);
    index_ = std::move(fresh.index_);
    sorted_ = std::move(fresh.sorted_);
    source_hash_ = fresh.source_hash_;
    loaded_ = true;
}

DataTableBase* DataTables::find(std::string_view name) {
    for (const auto& table : tables_) {
        if (table->name() == name) {
            return table.get();
        }
    }
    return nullptr;
}

void DataTables::set_source(DataSource source, std::string compiled_directory) {
    source_ = source;
    compiled_directory_ = std::move(compiled_directory);
}

DataRows DataTables::read(const std::string& root, const DataTableBase& table, DataIssues& issues) const {
    if (source_ == DataSource::Json) {
        return DataTableBase::read_json(root, table.directory(), table.name(), issues);
    }
    std::string bytes;
    const std::string path = compiled_directory_ + table.name() + ".mdat";
    if (!read_bytes(utf8_path(path), bytes)) {
        issues.error(path, "table compilée introuvable");
        return DataRows{};
    }
    return DataTableBase::decode(std::vector<std::uint8_t>(bytes.begin(), bytes.end()), table.name(), issues);
}

const DataIssues& DataTables::load_all(const std::string& root) {
    const Uint64 start = SDL_GetPerformanceCounter();
    issues_.clear();
    for (const auto& table : tables_) {
        DataRows rows = read(root, *table, issues_);
        std::unique_ptr<DataTableBase> fresh = table->build(std::move(rows), issues_);
        if (fresh) {
            table->take(*fresh);
        }
    }
    for (const auto& table : tables_) {
        if (table->loaded()) {
            table->link(*this, issues_, nullptr, nullptr);
        }
    }
    std::size_t rows = 0;
    for (const auto& table : tables_) {
        rows += table->size();
    }
    SDL_Log("Data: %zu tables, %zu rows, %zu errors, %zu warnings (%s), %.2f ms", tables_.size(), rows, issues_.errors(),
            issues_.warnings(), source_ == DataSource::Json ? "JSON" : "compiled",
            static_cast<double>(SDL_GetPerformanceCounter() - start) * 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency()));
    for (const DataIssue& issue : issues_.all()) {
        if (issue.level == DataIssue::Level::Error) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Data: %s : %s", issue.where.c_str(), issue.message.c_str());
        } else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Data: %s : %s", issue.where.c_str(), issue.message.c_str());
        }
    }
    return issues_;
}

bool DataTables::reload_table(const std::string& root, DataTableBase& table) {
    MOTEUR_PROFILE("données : rechargement");
    DataIssues issues;
    DataRows rows = read(root, table, issues);
    std::unique_ptr<DataTableBase> fresh = issues.has_errors() ? nullptr : table.build(std::move(rows), issues);
    std::vector<std::uint32_t> order;
    if (fresh && table.stable_order(*fresh, order, issues)) {
        fresh->reorder(order);
        fresh->link(*this, issues, &table, fresh.get());
    }
    const bool ok = fresh != nullptr && !issues.has_errors();
    if (ok) {
        table.take(*fresh);
        ++table.revision_;
        SDL_Log("Data: table '%s' reloaded (%zu rows)", table.name().c_str(), table.size());
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Data: table '%s' not reloaded (the previous content stays)",
                     table.name().c_str());
    }
    for (const DataIssue& issue : issues.all()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Data: %s : %s", issue.where.c_str(), issue.message.c_str());
    }
    issues_ = std::move(issues);
    return ok;
}

DataTableBase* DataTables::reload_file(const std::string& root, const std::string& key) {
    if (source_ != DataSource::Json || key.size() < 5 || key.compare(key.size() - 5, 5, ".json") != 0) {
        return nullptr;
    }
    for (const auto& table : tables_) {
        const std::string prefix = table->directory() + "/";
        if (key.compare(0, prefix.size(), prefix) == 0 && key.find('/', prefix.size()) == std::string::npos) {
            reload_table(root, *table);
            return table.get();
        }
    }
    return nullptr;
}

bool DataTables::reload(const std::string& root, const std::string& name) {
    DataTableBase* table = find(name);
    if (table == nullptr) {
        issues_.clear();
        issues_.error(name, "pas de table de ce nom");
        return false;
    }
    return reload_table(root, *table);
}

bool DataTables::compile(const std::string& root, const std::string& directory, DataIssues& issues) {
    const DataSource source = source_;
    source_ = DataSource::Json;
    load_all(root);
    source_ = source;
    issues.append(issues_);
    if (issues_.has_errors()) {
        return false;
    }
    std::error_code error;
    std::filesystem::create_directories(utf8_path(directory), error);
    for (const auto& table : tables_) {
        DataRows rows;
        rows.source_hash = table->source_hash();
        rows.files = table->files();
        for (const std::uint32_t index : table->sorted()) {
            rows.ids.push_back(table->id(index));
            rows.rows.push_back(table->resolved(index));
            rows.row_files.push_back(table->row_file(index));
        }
        const std::vector<std::uint8_t> bytes = DataTableBase::encode(table->name(), rows);
        const std::string path = directory + table->name() + ".mdat";
        std::ofstream file(utf8_path(path), std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!file) {
            issues.error(path, "écriture impossible");
            return false;
        }
        SDL_Log("Data: compiled '%s' (%zu rows, %zu bytes)", table->name().c_str(), table->size(), bytes.size());
    }
    return true;
}

}  // namespace moteur
