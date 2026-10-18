#include "moteur/asset_registry.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <random>
#include <stdexcept>
#include <system_error>

#include "moteur/cbor_check.hpp"
#include "moteur/file_io.hpp"

namespace moteur {

namespace {

constexpr int kMetaVersion = 1;
constexpr std::uint32_t kRegistryVersion = 1;
const char kRegistryMagic[4] = {'M', 'R', 'E', 'G'};

std::filesystem::path utf8_path(const std::string& text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

std::string path_utf8(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool ends_with(std::string_view text, std::string_view end) {
    return text.size() >= end.size() && text.substr(text.size() - end.size()) == end;
}

std::string lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](char c) {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    });
    return out;
}

}  // namespace

// --- AssetId ------------------------------------------------------------------------------------

AssetId AssetId::generate() {
    // std::random_device is the system's generator (BCryptGenRandom, /dev/urandom): 128 bits of it,
    // then the version (4) and the variant (10xx) bits of RFC 9562.
    static std::random_device device;
    const auto word = [] {
        return (static_cast<std::uint64_t>(device()) << 32) | static_cast<std::uint64_t>(device());
    };
    AssetId id;
    do {
        id.high = (word() & 0xFFFFFFFFFFFF0FFFull) | 0x0000000000004000ull;
        id.low = (word() & 0x3FFFFFFFFFFFFFFFull) | 0x8000000000000000ull;
    } while (!id.valid());
    return id;
}

std::optional<AssetId> AssetId::parse(std::string_view text) {
    if (text.size() != 36 || text[8] != '-' || text[13] != '-' || text[18] != '-' || text[23] != '-') {
        return std::nullopt;
    }
    AssetId id;
    int nibbles = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            continue;
        }
        const int value = hex_value(text[i]);
        if (value < 0) {
            return std::nullopt;
        }
        std::uint64_t& half = nibbles < 16 ? id.high : id.low;
        half = (half << 4) | static_cast<std::uint64_t>(value);
        ++nibbles;
    }
    return id;
}

std::string AssetId::text() const {
    char buffer[37];
    std::snprintf(buffer, sizeof(buffer), "%08x-%04x-%04x-%04x-%012llx", static_cast<unsigned>(high >> 32),
                  static_cast<unsigned>((high >> 16) & 0xFFFF), static_cast<unsigned>(high & 0xFFFF),
                  static_cast<unsigned>(low >> 48), static_cast<unsigned long long>(low & 0xFFFFFFFFFFFFull));
    return buffer;
}

// --- .meta ---------------------------------------------------------------------------------------

AssetMeta parse_asset_meta(std::string_view text, const std::string& name) {
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(text);
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error(name + ": invalid JSON: " + e.what());
    }
    if (!json.is_object() || json.value("version", 0) != kMetaVersion) {
        throw std::runtime_error(name + ": expected an object with \"version\": " + std::to_string(kMetaVersion));
    }
    const auto id = json.find("id");
    const std::optional<AssetId> parsed = id != json.end() && id->is_string() ? AssetId::parse(id->get<std::string>()) : std::nullopt;
    if (!parsed || !parsed->valid()) {
        throw std::runtime_error(name + ": \"id\" is not a UUID");
    }
    AssetMeta meta;
    meta.id = *parsed;
    if (const auto import = json.find("import"); import != json.end()) {
        if (!import->is_object()) {
            throw std::runtime_error(name + ": \"import\" is not an object");
        }
        meta.import = *import;
    }
    return meta;
}

std::string asset_meta_text(const AssetMeta& meta) {
    const nlohmann::json json = {{"version", kMetaVersion}, {"id", meta.id.text()}, {"import", meta.import}};
    return json.dump(2) + "\n";
}

bool is_identified_asset(std::string_view key) {
    const std::string name = lower(key);
    if (ends_with(name, ".meta") || ends_with(name, ".bin") || ends_with(name, ".md") || ends_with(name, ".txt") ||
        name.starts_with("data/")) {
        return false;
    }
    // Hidden files and folders (".git", ".DS_Store"...), and the import cache.
    for (std::size_t start = 0; start < name.size();) {
        const std::size_t end = name.find('/', start);
        if (name[start] == '.') {
            return false;
        }
        start = end == std::string::npos ? name.size() : end + 1;
    }
    return true;
}

// --- Registry ------------------------------------------------------------------------------------

void AssetRegistry::clear() {
    records_.clear();
    by_key_.clear();
}

void AssetRegistry::add(AssetRecord record) {
    if (const auto old = by_key_.find(record.key); old != by_key_.end()) {
        records_.erase(old->second);
        by_key_.erase(old);
    }
    if (const auto taken = records_.find(record.id); taken != records_.end()) {
        by_key_.erase(taken->second.key);
        records_.erase(taken);
    }
    by_key_[record.key] = record.id;
    records_[record.id] = std::move(record);
}

const AssetRecord* AssetRegistry::find(const AssetId& id) const {
    const auto found = records_.find(id);
    return found != records_.end() ? &found->second : nullptr;
}

const AssetRecord* AssetRegistry::find(std::string_view key) const {
    const auto found = by_key_.find(key);
    return found != by_key_.end() ? find(found->second) : nullptr;
}

AssetScanReport AssetRegistry::scan(const std::vector<std::string>& roots, bool write_missing) {
    clear();
    AssetScanReport report;
    struct Found {
        std::string key;
        std::string root;
        bool has_meta = false;
        AssetMeta meta;
    };
    std::vector<Found> found;
    std::map<std::string, std::size_t, std::less<>> by_key;  // first root wins
    std::vector<std::string> metas;                          // keys of the .meta files seen (without ".meta")
    for (const std::string& root : roots) {
        std::error_code error;
        const std::filesystem::path base = utf8_path(root);
        if (!std::filesystem::is_directory(base, error)) {
            continue;
        }
        for (auto it = std::filesystem::recursive_directory_iterator(
                 base, std::filesystem::directory_options::skip_permission_denied, error);
             it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
            if (error) {
                report.problems.push_back(root + ": " + error.message());
                break;
            }
            const std::string name = path_utf8(it->path().filename());
            if (it->is_directory(error)) {
                if (!name.empty() && name[0] == '.') {
                    it.disable_recursion_pending();  // .git, the import cache...
                }
                continue;
            }
            const std::string key = path_utf8(std::filesystem::relative(it->path(), base, error));
            if (ends_with(key, ".meta")) {
                metas.push_back(key.substr(0, key.size() - 5));
                continue;
            }
            if (!is_identified_asset(key)) {
                continue;
            }
            if (by_key.contains(key)) {
                report.shadowed.push_back(key);
                continue;
            }
            Found asset;
            asset.key = key;
            asset.root = root;
            const std::string meta_path = root + key + ".meta";
            if (std::filesystem::exists(utf8_path(meta_path), error)) {
                try {
                    const std::optional<std::vector<std::uint8_t>> bytes = read_file_bytes(meta_path);
                    if (!bytes) {
                        throw std::runtime_error(meta_path + ": cannot be read");
                    }
                    asset.meta = parse_asset_meta({reinterpret_cast<const char*>(bytes->data()), bytes->size()}, meta_path);
                    asset.has_meta = true;
                } catch (const std::exception& e) {
                    report.problems.push_back(e.what());
                }
            }
            by_key[key] = found.size();
            found.push_back(std::move(asset));
        }
    }
    for (const std::string& meta : metas) {
        if (by_key.find(meta) == by_key.end() && is_identified_asset(meta)) {
            report.orphans.push_back(meta + ".meta");
        }
    }
    std::sort(report.orphans.begin(), report.orphans.end());

    // Identifiers: kept; a missing one made; a duplicated one (a copy) made again for the path that
    // sorts last, so that the original keeps its identity whatever order the disk lists them in.
    std::sort(found.begin(), found.end(), [](const Found& a, const Found& b) { return a.key < b.key; });
    std::map<AssetId, std::size_t> owner;
    for (std::size_t i = 0; i < found.size(); ++i) {
        Found& asset = found[i];
        bool write = false;
        if (!asset.has_meta) {
            asset.meta.id = AssetId::generate();
            write = true;
            report.created.push_back(asset.key);
        } else if (owner.contains(asset.meta.id)) {
            report.reassigned.push_back(asset.key + " (had the identifier of " + found[owner[asset.meta.id]].key + ")");
            asset.meta.id = AssetId::generate();
            write = true;
        }
        owner[asset.meta.id] = i;
        if (write && write_missing) {
            std::string error;
            if (!write_file_atomic(asset.root + asset.key + ".meta", asset_meta_text(asset.meta), error)) {
                report.problems.push_back("cannot write " + asset.key + ".meta: " + error);
            }
        }
        AssetRecord record;
        record.id = asset.meta.id;
        record.key = asset.key;
        record.root = asset.root;
        record.import = asset.meta.import;
        add(std::move(record));
    }
    report.assets = records_.size();
    return report;
}

bool AssetRegistry::move(std::string_view from, std::string_view to, std::string& error) {
    const AssetRecord* record = find(from);
    if (record == nullptr) {
        error = "unknown asset '" + std::string(from) + "'";
        return false;
    }
    if (record->root.empty()) {
        error = "the registry is frozen (no folder to move files in)";
        return false;
    }
    if (find(to) != nullptr || path_exists(record->root + std::string(to))) {
        error = "'" + std::string(to) + "' exists already";
        return false;
    }
    const std::string root = record->root;
    std::error_code code;
    const std::filesystem::path target = utf8_path(root + std::string(to));
    std::filesystem::create_directories(target.parent_path(), code);
    std::filesystem::rename(utf8_path(root + std::string(from)), target, code);
    if (code) {
        error = "cannot move '" + std::string(from) + "': " + code.message();
        return false;
    }
    std::filesystem::rename(utf8_path(root + std::string(from) + ".meta"), utf8_path(root + std::string(to) + ".meta"), code);
    if (code) {
        // The asset moved without its identity: put it back rather than lose it.
        std::error_code back;
        std::filesystem::rename(target, utf8_path(root + std::string(from)), back);
        error = "cannot move the .meta of '" + std::string(from) + "': " + code.message();
        return false;
    }
    AssetRecord moved = *record;
    moved.key = std::string(to);
    add(std::move(moved));
    return true;
}

std::vector<std::uint8_t> AssetRegistry::encode() const {
    nlohmann::json rows = nlohmann::json::array();
    for (const auto& [id, record] : records_) {
        rows.push_back({id.text(), record.key, record.import});
    }
    std::vector<std::uint8_t> bytes(kRegistryMagic, kRegistryMagic + 4);
    for (int i = 0; i < 4; ++i) {
        bytes.push_back(static_cast<std::uint8_t>((kRegistryVersion >> (8 * i)) & 0xFF));
    }
    const std::vector<std::uint8_t> body = nlohmann::json::to_cbor(rows);
    bytes.insert(bytes.end(), body.begin(), body.end());
    return bytes;
}

AssetRegistry AssetRegistry::decode(const std::vector<std::uint8_t>& bytes, const std::string& name) {
    if (bytes.size() < 8 || std::memcmp(bytes.data(), kRegistryMagic, 4) != 0) {
        throw std::runtime_error(name + ": not an asset registry (MREG)");
    }
    const std::uint32_t version = static_cast<std::uint32_t>(bytes[4]) | (static_cast<std::uint32_t>(bytes[5]) << 8) |
                                  (static_cast<std::uint32_t>(bytes[6]) << 16) | (static_cast<std::uint32_t>(bytes[7]) << 24);
    if (version != kRegistryVersion) {
        throw std::runtime_error(name + ": registry format " + std::to_string(version) + " (expected " +
                                 std::to_string(kRegistryVersion) + ")");
    }
    std::string problem;
    if (!check_cbor_structure(bytes.data() + 8, bytes.size() - 8, problem)) {
        throw std::runtime_error(name + ": unreadable: " + problem);
    }
    AssetRegistry registry;
    try {
        const nlohmann::json rows = nlohmann::json::from_cbor(bytes.begin() + 8, bytes.end());
        for (const nlohmann::json& row : rows) {
            const std::optional<AssetId> id = AssetId::parse(row.at(0).get<std::string>());
            if (!id) {
                throw std::runtime_error("an identifier is not a UUID");
            }
            AssetRecord record;
            record.id = *id;
            record.key = row.at(1).get<std::string>();
            record.import = row.at(2);
            registry.add(std::move(record));
        }
    } catch (const std::exception& e) {
        throw std::runtime_error(name + ": unreadable: " + e.what());
    }
    return registry;
}

}  // namespace moteur
