#pragma once

#include <nlohmann/json.hpp>

#include <compare>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace moteur {

/// The permanent identity of an asset (milestone 9, part 4; decision: a UUID in a `.meta` file next
/// to it): given once when the asset first appears, never computed again, so that renaming or
/// moving the file breaks nothing that refers to it. A random UUID (version 4, RFC 9562), written
/// `"3f2a9c1e-7b4d-4e0a-9c55-0d1e2f3a4b5c"`.
struct AssetId {
    std::uint64_t high = 0;  ///< the first 8 bytes, big-endian as written
    std::uint64_t low = 0;   ///< the last 8 bytes

    /// False for the null identifier (all zero): no asset.
    bool valid() const { return high != 0 || low != 0; }
    /// A new random identifier (version 4, variant 1), from the system's random device.
    static AssetId generate();
    /// Reads the 36-character form (lowercase or uppercase hexadecimal, hyphens at 8, 13, 18, 23);
    /// nullopt for anything else.
    static std::optional<AssetId> parse(std::string_view text);
    /// The 36-character lowercase form.
    std::string text() const;

    /// Ordered and compared as 128-bit numbers.
    auto operator<=>(const AssetId&) const = default;
};

/// What an asset's `.meta` file holds (`<file>.meta`, JSON, next to the file):
///
/// @code{.json}
/// {"version": 1, "id": "3f2a9c1e-7b4d-4e0a-9c55-0d1e2f3a4b5c", "import": {"lods": 3}}
/// @endcode
///
/// `import`: the settings of its import (sRGB or data for a texture, levels of detail for a model,
/// see the importer of each type); an empty object for the defaults. The `.meta` always goes with
/// its asset (a lost `.meta` is a lost identity): versioned with it, moved with it.
struct AssetMeta {
    AssetId id;                                        ///< the asset's identity
    nlohmann::json import = nlohmann::json::object();  ///< its import settings
};

/// Reads a `.meta` file's text. `name` appears in messages. Throws std::runtime_error for invalid
/// JSON, another version, or a missing or malformed identifier.
AssetMeta parse_asset_meta(std::string_view text, const std::string& name);
/// The `.meta` text of `meta` (stable: keys sorted, two-space indentation, a final newline).
std::string asset_meta_text(const AssetMeta& meta);

/// Whether the asset key `key` (a normalized path) is an asset that gets a `.meta`: every file but
/// the `.meta` files themselves, glTF buffers (`.bin`, part of their model), the data tables
/// (`data/`, whose rows have identifiers of their own) and documentation (`.md`, `.txt`).
bool is_identified_asset(std::string_view key);

/// One asset known to the registry.
struct AssetRecord {
    AssetId id;           ///< its identity
    std::string key;      ///< its path in the assets ("models/props/lampadaire.glb")
    std::string root;     ///< the folder it was found under (ending with a separator), for the tools; empty when frozen
    nlohmann::json import = nlohmann::json::object();  ///< its import settings (AssetMeta::import)
};

/// What AssetRegistry::scan() found and did, for the log and the tools.
struct AssetScanReport {
    std::size_t assets = 0;                ///< identified assets
    std::vector<std::string> created;      ///< keys whose `.meta` was written (new assets)
    std::vector<std::string> reassigned;   ///< keys given a new identifier: a copy that had another's
    std::vector<std::string> orphans;      ///< `.meta` files without their asset (left alone)
    /// Keys found in a later root too (a mounted game's `credits.json` under the program's own): the
    /// first root's file is the asset, the later one is hidden (what read_file() does as well).
    std::vector<std::string> shadowed;
    std::vector<std::string> problems;     ///< unreadable `.meta` files...
};

/// The identities of the assets (milestone 9, part 4): identifier -> path and path -> identifier.
/// Built in development by scanning the asset folders and their `.meta` files (scan()), frozen by
/// the preparation into `asset_registry.mreg` for the shipped game (encode() / decode()). The asset
/// manager loads by identifier through it; data refer to assets by identifier, the path written next
/// to it for people. Main thread.
class AssetRegistry {
public:
    /// Scans `roots` (folders ending with a separator; an asset key is its path under its root, the
    /// first root winning when two have the same key: see AssetScanReport::shadowed) for their assets and
    /// `.meta` files. With `write_missing`, an asset without `.meta` gets one (a new identifier), and
    /// a duplicated identifier (a file copied with its `.meta`) is given a new one in the `.meta` of
    /// the copy (the path that sorts last), both written to disk; without it, they are left as they
    /// are and reported (an asset without `.meta` gets a temporary identifier for this run). Replaces
    /// what was known. Disk access: at start-up and in the tools.
    AssetScanReport scan(const std::vector<std::string>& roots, bool write_missing);

    /// Adds or replaces one record (the asset manager, for a file created while running; tests).
    void add(AssetRecord record);
    /// Forgets everything.
    void clear();

    /// The record of `id`, or null.
    const AssetRecord* find(const AssetId& id) const;
    /// The record of the asset key `key` (normalized), or null.
    const AssetRecord* find(std::string_view key) const;
    /// Every record, by identifier.
    const std::map<AssetId, AssetRecord>& records() const { return records_; }
    /// Number of assets.
    std::size_t size() const { return records_.size(); }

    /// Moves (renames) the asset `from` to `to` (asset keys, under the same root) with its `.meta`,
    /// and, for a `.gltf`, nothing else (its buffers and images keep their names: move the folder to
    /// move them all). Its identifier does not change: what refers to it still finds it. False with
    /// `error` if `from` is unknown, `to` exists, or the files cannot be moved.
    bool move(std::string_view from, std::string_view to, std::string& error);

    /// The frozen form, for the shipped game: `MREG`, format version (u32, little-endian), then CBOR
    /// `[[id, key, import], ...]` sorted by identifier (roots are not kept).
    std::vector<std::uint8_t> encode() const;
    /// Reads the frozen form; throws std::runtime_error naming `name` for anything else.
    static AssetRegistry decode(const std::vector<std::uint8_t>& bytes, const std::string& name);

private:
    std::map<AssetId, AssetRecord> records_;
    std::map<std::string, AssetId, std::less<>> by_key_;
};

}  // namespace moteur
