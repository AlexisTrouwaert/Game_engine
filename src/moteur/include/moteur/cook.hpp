#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "moteur/data_table.hpp"

namespace moteur {

/// What cook() works from: the preparation of the assets for the shipped game ("cooking",
/// milestone 8, part 6), from the development sources to data ready to load, so that the shipped
/// game converts nothing when it starts. What it does:
/// - selects the files to ship from a delivery manifest (globs), plus what they depend on (the
///   buffers and images of a glTF model, the textures of a particle effect); a dependency that does
///   not exist is an error, a source file shipped by nothing is listed (orphans);
/// - transcodes the KTX2 textures in Basis Universal (UASTC) to BC7 or BC5 once and for all
///   (decision of milestone 3: both Windows and Apple Silicon read them), Zstandard-supercompressed;
/// - compiles the data tables to their binary form (.mdat), through the game's callback;
/// - copies the compiled shaders;
/// - reads every prepared file again with the engine's own loader (a model, a map, an effect, an
///   animation description, a texture): what does not load is an error now, not in a player's hands;
/// - warns about shipped assets that no credits entry covers (licenses);
/// - only redoes what changed (a stamp per file: its source's content and the tool's version); a
///   file whose preparation failed gets no stamp, so it is redone (and its error reported) each time.
///
/// The delivery manifest, a JSON file of the game's assets (not shipped itself):
///
/// @code{.json}
/// {"version": 1,
///  "include": ["maps/tranche.json", "models/characters/**", "effects/*.json", "audio/**"],
///  "exclude": ["**/*.blend"],
///  "data": "data"}
/// @endcode
///
/// `version` must be 1. Globs are relative to the assets: `*` matches within a folder name, `**`
/// then a slash any number of folders, another `**` anything, across folders (see glob_match()). `data` is the folder of the data tables: their JSON
/// stays behind, the compiled tables go into that folder of the output instead.
///
/// `check` names the folders whose files are read back with their loader (`{"maps": "maps/",
/// "effects": "effects/", "animations": "animations/"}`, the defaults).
///
/// Then everything is packed into archives (moteur/archive.hpp), `output/archives/NAME.pak`: a list
/// `"archives": [{"name": "audio", "include": ["audio/**"]}]` sends the files it matches into their
/// own archive (an update then changes only the archives touched; a file goes to the first group
/// that matches); the rest, and the shaders, go into common.pak. The archives are only written when
/// there was no error. The shipped program mounts them all (ApplicationConfig::archives_directory).
///
/// The French keys of the first manifests (`inclure`, `exclure`, `donnees`, `verifier`, `cartes`,
/// `effets`, `nom`) are still read, with a warning, until the manifests are migrated.
struct CookOptions {
    std::vector<std::string> sources;  ///< asset folders, in order (a later one's file replaces an earlier one's)
    std::string manifest;              ///< the delivery manifest
    std::string output;                ///< gets assets/, shaders/ and archives/ (and cook_state.json, the stamps of the files)
    std::string shaders;               ///< the compiled shaders of the build (copied), or empty
    /// Compiles the data tables of `root` (the source folder that holds them) into `directory`,
    /// reporting problems in `issues`; false if it failed. Empty: the game has no tables.
    std::function<bool(const std::string& root, const std::string& directory, DataIssues& issues)> compile_data;
    bool force = false;                ///< redo everything
};

/// What cook() did: counts, sizes, and every problem found. CookReport::text() is the summary a
/// tool prints.
struct CookReport {
    std::size_t shipped = 0;        ///< files in the output
    std::size_t prepared = 0;       ///< of which redone this time (the others were up to date)
    std::size_t transcoded = 0;     ///< KTX2 turned into BC7 / BC5
    std::size_t bytes_in = 0;       ///< of the sources shipped
    std::size_t bytes_out = 0;      ///< of the files in the output (before packing)
    std::vector<std::string> errors;    ///< what makes the result unfit to ship, one message each
    std::vector<std::string> warnings;  ///< what should be fixed but does not stop the shipping
    std::vector<std::string> orphans;  ///< source files nothing ships
    /// One archive written.
    struct Pack {
        std::string name;       ///< its file name in `output/archives/` ("common.pak")
        std::size_t files = 0;  ///< entries in it
        std::size_t bytes = 0;  ///< of the archive
    };
    std::vector<Pack> archives;  ///< the archives written, common.pak last
    /// No error: the output is ready to ship.
    bool ok() const { return errors.empty(); }
    std::string text() const;  ///< a summary, then the archives, the errors and warnings, one per line
};

/// Cooks the sources of `options` into `options.output` (see CookOptions). Long (it transcodes and
/// compresses), and blocks: for a tool, not for the game. Throws std::runtime_error only if the
/// manifest itself cannot be read, is not JSON, or is not version 1; every other problem is in the
/// report.
CookReport cook(const CookOptions& options);

/// The command line of a game's cooking tool, for its main():
/// @verbatim
/// TOOL --sources DIR [DIR...] --manifest FILE --output DIR [--shaders DIR] [--force] [--orphans]
/// @endverbatim
/// `--orphans` also lists the source files nothing ships. `compile_data` is passed on as
/// CookOptions::compile_data. Prints the report (and the duration) on the standard output; returns
/// the exit code: 0 ready to ship, 1 errors (or a manifest that cannot be read), 2 a bad command line.
int cook_main(int argc, char* argv[], std::function<bool(const std::string&, const std::string&, DataIssues&)> compile_data = {});

/// Does `path` (with '/' separators) match `glob`? `*` matches any characters but '/', `**` followed
/// by '/' any number of whole folders, none included (`models/**` + `/a.gltf` matches both
/// `models/a.gltf` and `models/x/y/a.gltf`), any other `**` any characters, '/' included, the rest
/// of the glob still having to match (a final `**`: everything below; `models/**.gltf`: the `.gltf`
/// files anywhere below `models/`); case-sensitive. Exposed for the tests.
bool glob_match(const std::string& glob, const std::string& path);

}  // namespace moteur
