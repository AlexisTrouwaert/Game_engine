#pragma once

#include <string>
#include <vector>

namespace moteur {

/// One credited item: a library, a font or an asset, as read from a credits file (see Credits).
/// Every field is optional in the file and empty when absent; the credits screen shows what is set.
struct CreditEntry {
    std::string name;          ///< what is credited ("SDL", "Inter", "KayKit Dungeon Remastered")
    std::string version;       ///< its version, as written ("3.2.10")
    std::string license;       ///< the license's name or SPDX identifier ("zlib", "OFL-1.1", "CC0-1.0")
    std::string copyright;     ///< the copyright line, as written
    std::string authors;       ///< who made it, as one text
    std::string source;        ///< where it was obtained ("Poly Haven", a store, "made for the game")
    std::string url;           ///< a link to the project or the asset's page
    std::string license_file;  ///< full license text, shown on demand (path relative to the executable's folder)
    std::string file;          ///< an asset: shown as present or missing (path relative to the executable's folder)
};

/// What a program is made of and who made what it shows, for its "À propos" / credits screen: the
/// third-party libraries and fonts with their licenses, and the authors of the assets.
///
/// Read from JSON files (the engine's `credits_moteur.json`, from `assets_moteur/`, then the game's
/// `credits.json`, both found in the program's assets: asset_path()), each with optional lists
/// `"libraries"`, `"fonts"`, `"models"`, `"environments"`, `"sounds"` of objects whose keys are the
/// fields of CreditEntry; the lists of several files are appended in order. An entry with a
/// `"platform"` (`"windows"`, `"macos"`) is only kept on that OS; one with a `"build"` option is only
/// kept when the engine was built with it: `"MOTEUR_TRACY"` (the profiler), or `"DEVELOPMENT"` (the
/// development tools, absent from a shipping build; the older French name `"DEVELOPPEMENT"` is still
/// read). Any other `"build"` value drops the entry. Paths in entries are relative to the executable's
/// folder.
struct Credits {
    std::vector<CreditEntry> libraries;     ///< third-party code (`"libraries"`)
    std::vector<CreditEntry> fonts;         ///< typefaces (`"fonts"`)
    std::vector<CreditEntry> models;        ///< 3D models and their textures (`"models"`)
    std::vector<CreditEntry> environments;  ///< HDR environment maps (`"environments"`)
    std::vector<CreditEntry> sounds;        ///< sounds and music (`"sounds"`)
    std::string error;  ///< why a file could not be read (several: one per line); empty if all were read

    /// Appends the lists of one file (read with read_text_file(), so from the mounted archives first).
    /// Never throws: a missing or invalid file (a field of the wrong type included) adds a line to
    /// #error and appends nothing of that file.
    void add_file(const std::string& path);

    /// The credits of every file of `paths`, in order (add_file() on each).
    static Credits load(const std::vector<std::string>& paths);
};

}  // namespace moteur
