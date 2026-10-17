#include "moteur/cook.hpp"

#include <SDL3/SDL.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <system_error>

#include "moteur/animation_set.hpp"
#include "moteur/archive.hpp"
#include "moteur/file_io.hpp"
#include "moteur/image.hpp"
#include "moteur/ktx_texture.hpp"
#include "moteur/map_data.hpp"
#include "moteur/model.hpp"
#include "moteur/particles.hpp"
#include "moteur/sound.hpp"

namespace moteur {

namespace {

namespace fs = std::filesystem;

// Raise when a change of this tool changes what it writes: everything is prepared again.
constexpr int kCookVersion = 1;

fs::path utf8_path(const std::string& text) {
    return fs::path(std::u8string(text.begin(), text.end()));
}

std::string utf8_string(const fs::path& path) {
    const std::u8string text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string extension_of(const std::string& path) {
    const std::size_t dot = path.rfind('.');
    const std::size_t slash = path.rfind('/');
    return dot == std::string::npos || (slash != std::string::npos && dot < slash) ? std::string() : lower(path.substr(dot));
}

std::string directory_of(const std::string& path) {
    const std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

// "a/./b/../c" -> "a/c"; the key of an asset, as the engine writes it.
std::string normal_key(const std::string& path) {
    return utf8_string(utf8_path(path).lexically_normal());
}

std::uint64_t fnv1a(const std::vector<std::uint8_t>& bytes, std::uint64_t hash = 14695981039346656037ull) {
    for (const std::uint8_t byte : bytes) {
        hash = (hash ^ byte) * 1099511628211ull;
    }
    return hash;
}

// What a shipped file becomes.
enum class Kind { Copy, Texture };

struct Source {
    std::string absolute;  // where it is read
    std::string root;      // the source folder it comes from
};

}  // namespace

bool glob_match(const std::string& glob, const std::string& path) {
    // Recursive on '**/' (any folders, possibly none), '**' (any characters, '/' included), '*'
    // within a folder name.
    const auto match = [](const auto& self, std::size_t g, std::size_t p, const std::string& gl, const std::string& pa) -> bool {
        while (g < gl.size()) {
            if (gl.compare(g, 3, "**/") == 0) {
                for (std::size_t q = p; q <= pa.size(); ++q) {
                    if ((q == p || pa[q - 1] == '/') && self(self, g + 3, q, gl, pa)) {
                        return true;
                    }
                }
                return false;
            }
            if (gl.compare(g, 2, "**") == 0) {
                // Any run of characters, across folders; the rest of the glob must still match ("**"
                // at the end: everything below; "models/**.gltf": the .gltf files below models/).
                for (std::size_t q = p; q <= pa.size(); ++q) {
                    if (self(self, g + 2, q, gl, pa)) {
                        return true;
                    }
                }
                return false;
            }
            if (gl[g] == '*') {
                for (std::size_t q = p; q <= pa.size(); ++q) {
                    if (self(self, g + 1, q, gl, pa)) {
                        return true;
                    }
                    if (q < pa.size() && pa[q] == '/') {
                        break;
                    }
                }
                return false;
            }
            if (p >= pa.size() || gl[g] != pa[p]) {
                return false;
            }
            ++g;
            ++p;
        }
        return p == pa.size();
    };
    return match(match, 0, 0, glob, path);
}

std::string CookReport::text() const {
    std::string out = "Cook: " + std::to_string(shipped) + " file(s) shipped, " + std::to_string(prepared) +
                      " cooked this run, " + std::to_string(transcoded) + " texture(s) transcoded to BC7/BC5; " +
                      std::to_string(bytes_in / 1024) + " KiB of sources -> " + std::to_string(bytes_out / 1024) + " KiB; " +
                      std::to_string(orphans.size()) + " source(s) not shipped; " + std::to_string(errors.size()) +
                      " error(s), " + std::to_string(warnings.size()) + " warning(s)\n";
    for (const Pack& pack : archives) {
        out += "archive " + pack.name + ": " + std::to_string(pack.files) + " file(s), " + std::to_string(pack.bytes / 1024) +
               " KiB\n";
    }
    for (const std::string& error : errors) {
        out += "error: " + error + "\n";
    }
    for (const std::string& warning : warnings) {
        out += "warning: " + warning + "\n";
    }
    return out;
}

// A key of the manifest, or the French name it had until the formats moved to English (2026-10-17):
// still read while the manifests are migrated, with a warning that says which key to write.
template <typename T>
T manifest_value(const nlohmann::json& object, const char* key, const char* old_key, T fallback, CookReport& report,
                 const std::string& where) {
    if (object.contains(key)) {
        return object.value(key, fallback);
    }
    if (object.contains(old_key)) {
        report.warnings.push_back(where + ": key '" + old_key + "' is deprecated, write '" + key + "'");
        return object.value(old_key, fallback);
    }
    return fallback;
}

CookReport cook(const CookOptions& options) {
    CookReport report;

    // The manifest.
    const auto manifest_bytes = read_file_bytes(options.manifest);
    if (!manifest_bytes) {
        throw std::runtime_error("Cook: shipping manifest not found: " + options.manifest);
    }
    nlohmann::json manifest;
    try {
        manifest = nlohmann::json::parse(manifest_bytes->begin(), manifest_bytes->end());
    } catch (const std::exception& e) {
        throw std::runtime_error("Cook: cannot parse the manifest '" + options.manifest + "': " + e.what());
    }
    if (manifest.value("version", 0) != 1) {
        throw std::runtime_error("Cook: manifest '" + options.manifest + "': version 1 expected");
    }
    const std::string manifest_name = utf8_string(utf8_path(options.manifest).filename());
    const auto includes = manifest_value(manifest, "include", "inclure", std::vector<std::string>{}, report, manifest_name);
    const auto excludes = manifest_value(manifest, "exclude", "exclure", std::vector<std::string>{}, report, manifest_name);
    std::string data_folder = manifest_value(manifest, "data", "donnees", std::string(), report, manifest_name);
    if (!data_folder.empty() && data_folder.back() != '/') {
        data_folder += '/';
    }
    const nlohmann::json checks = manifest_value(manifest, "check", "verifier", nlohmann::json::object(), report, manifest_name);
    const std::string maps_folder = manifest_value(checks, "maps", "cartes", std::string("maps/"), report, manifest_name);
    const std::string effects_folder = manifest_value(checks, "effects", "effets", std::string("effects/"), report, manifest_name);
    const std::string animations_folder = checks.value("animations", std::string("animations/"));

    // Every source file, by key (a later folder replaces an earlier one's file).
    std::map<std::string, Source> sources;
    std::string data_root;  // the source folder that holds the tables
    for (const std::string& root : options.sources) {
        std::error_code error;
        const fs::path base = utf8_path(root);
        if (!fs::is_directory(base, error)) {
            report.errors.push_back("source directory not found: " + root);
            continue;
        }
        if (!data_folder.empty() && fs::is_directory(base / utf8_path(data_folder), error)) {
            data_root = root;
        }
        for (fs::recursive_directory_iterator it(base, error), end; it != end; it.increment(error)) {
            if (error) {
                break;
            }
            if (it->is_regular_file()) {
                const std::string key = utf8_string(fs::relative(it->path(), base));
                sources[key] = {utf8_string(it->path()), root};
            }
        }
    }

    // The selection: the manifest's globs, without the tables' JSON nor the manifest.
    std::map<std::string, Kind> shipped;
    const auto excluded = [&](const std::string& key) {
        if (key == manifest_name || (!data_folder.empty() && key.starts_with(data_folder))) {
            return true;
        }
        return std::any_of(excludes.begin(), excludes.end(), [&key](const std::string& glob) { return glob_match(glob, key); });
    };
    const auto kind_of = [](const std::string& key) { return extension_of(key) == ".ktx2" ? Kind::Texture : Kind::Copy; };
    for (const std::string& glob : includes) {
        bool any = false;
        for (const auto& [key, source] : sources) {
            if (glob_match(glob, key) && !excluded(key)) {
                shipped[key] = kind_of(key);
                any = true;
            }
        }
        if (!any) {
            report.warnings.push_back("'" + glob + "' matches no file");
        }
    }

    // Dependencies: the files of the glTF models, the textures of the effects.
    std::vector<std::string> pending;
    pending.reserve(shipped.size());
    for (const auto& [key, kind] : shipped) {
        pending.push_back(key);
    }
    const auto require = [&](const std::string& from, const std::string& key) {
        if (shipped.contains(key)) {
            return;
        }
        if (!sources.contains(key)) {
            report.errors.push_back(from + ": file '" + key + "' not found");
            return;
        }
        shipped[key] = kind_of(key);
        pending.push_back(key);
    };
    while (!pending.empty()) {
        const std::string key = pending.back();
        pending.pop_back();
        const std::string extension = extension_of(key);
        const std::string& path = sources.at(key).absolute;
        try {
            if (extension == ".gltf" || extension == ".glb") {
                GltfOptions gltf;
                gltf.decode_external_images = false;  // the shipped game reads them through its texture cache
                gltf.prefer_ktx2 = true;               // and prefers the KTX2: their plain images stay behind
                const ModelData data = load_gltf(path, gltf);
                for (const std::string& file : data.files) {
                    require(key, normal_key(directory_of(key) + file));
                }
                for (const ModelImage& image : data.images) {
                    if (!image.file.empty()) {
                        require(key, normal_key(directory_of(key) + image.file));
                    }
                }
            } else if (key.starts_with(effects_folder) && extension == ".json") {
                const auto bytes = read_file_bytes(path);
                const ParticleEffect effect = ParticleEffect::parse(
                    std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()), key);
                for (const EmitterDesc& emitter : effect.emitters()) {
                    require(key, normal_key(emitter.texture));
                }
            }
        } catch (const std::exception& e) {
            report.errors.push_back(e.what());
        }
    }

    // What the previous run wrote, and from what.
    const fs::path out = utf8_path(options.output);
    const fs::path out_assets = out / "assets";
    std::error_code error;
    fs::create_directories(out_assets, error);
    const std::string stamps_path = utf8_string(out / "cook_state.json");
    nlohmann::json stamps = nlohmann::json::object();
    // The textures the previous run transcoded (the others were copied as they are). A state written
    // before this list existed does not say: its textures are prepared again.
    std::set<std::string> was_transcoded;
    bool knows_transcoded = false;
    if (!options.force) {
        if (const auto bytes = read_file_bytes(stamps_path)) {
            try {
                const nlohmann::json previous = nlohmann::json::parse(bytes->begin(), bytes->end());
                if (previous.value("version", 0) == kCookVersion) {
                    if (previous.contains("transcoded")) {  // read first: damaged, nothing is kept
                        was_transcoded = previous.at("transcoded").get<std::set<std::string>>();
                        knows_transcoded = true;
                    }
                    stamps = previous.value("files", nlohmann::json::object());
                }
            } catch (const std::exception& e) {
                // Damaged state: every file is prepared again, which is always correct, only slower.
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Cook: '%s' unreadable (%s), every file is prepared again",
                            stamps_path.c_str(), e.what());
            }
        }
    }
    nlohmann::json new_stamps = nlohmann::json::object();
    nlohmann::json new_transcoded = nlohmann::json::array();

    // Prepare each file.
    const CompressedFormats bc{true, true};
    for (const auto& [key, kind] : shipped) {
        const auto bytes = read_file_bytes(sources.at(key).absolute);
        if (!bytes) {
            report.errors.push_back("cannot read '" + key + "'");
            continue;
        }
        report.bytes_in += bytes->size();
        const std::string stamp = std::to_string(fnv1a(*bytes, static_cast<std::uint64_t>(kCookVersion) * 1099511628211ull));
        const fs::path target = out_assets / utf8_path(key);
        ++report.shipped;
        if (stamps.contains(key) && stamps[key] == stamp && fs::is_regular_file(target, error) &&
            (kind != Kind::Texture || knows_transcoded)) {
            new_stamps[key] = stamp;
            report.bytes_out += static_cast<std::size_t>(fs::file_size(target, error));
            if (kind == Kind::Texture && was_transcoded.contains(key)) {
                ++report.transcoded;  // still transcoded, from an earlier run
                new_transcoded.push_back(key);
            }
            continue;
        }
        // A file gets its stamp only once its output is written: one that failed (an older output
        // may be left in place) is prepared again, and its error reported again, at the next cook.
        std::vector<std::uint8_t> written = *bytes;
        bool transcoded_now = false;
        try {
            if (kind == Kind::Texture) {
                std::vector<std::uint8_t> transcoded = transcode_ktx2_for_shipping(bytes->data(), bytes->size(), key);
                if (!transcoded.empty()) {
                    written = std::move(transcoded);
                    transcoded_now = true;
                }
            }
        } catch (const std::exception& e) {
            report.errors.push_back(e.what());
            continue;
        }
        fs::create_directories(target.parent_path(), error);
        std::string write_error;
        if (!write_file_atomic(utf8_string(target), written.data(), written.size(), write_error)) {
            report.errors.push_back(write_error);
            continue;
        }
        new_stamps[key] = stamp;
        if (transcoded_now) {
            ++report.transcoded;
            new_transcoded.push_back(key);
        }
        report.bytes_out += written.size();
        ++report.prepared;
    }

    // The data tables, compiled.
    if (!data_folder.empty()) {
        if (!options.compile_data) {
            report.errors.push_back("the manifest has tables ('" + data_folder + "') but the game gives no way to compile them");
        } else if (data_root.empty()) {
            report.errors.push_back("table directory '" + data_folder + "' not found in the sources");
        } else {
            DataIssues issues;
            std::string root = data_root;
            if (!root.empty() && root.back() != '/' && root.back() != '\\') {
                root += '/';
            }
            const fs::path target = out_assets / utf8_path(data_folder);
            fs::create_directories(target, error);
            std::string directory = utf8_string(target);
            if (directory.back() != '/') {
                directory += '/';
            }
            if (!options.compile_data(root, directory, issues)) {
                report.errors.push_back("tables: " + issues.text());
            }
            for (const auto& entry : fs::directory_iterator(target, error)) {
                if (entry.is_regular_file()) {
                    const std::string key = data_folder + utf8_string(entry.path().filename());
                    new_stamps[key] = "tables";
                    ++report.shipped;
                    report.bytes_out += static_cast<std::size_t>(entry.file_size());
                }
            }
        }
    }

    // The shaders.
    if (!options.shaders.empty()) {
        const fs::path from = utf8_path(options.shaders);
        const fs::path to = out / "shaders";
        fs::create_directories(to, error);
        for (const auto& entry : fs::directory_iterator(from, error)) {
            if (entry.is_regular_file()) {
                fs::copy_file(entry.path(), to / entry.path().filename(), fs::copy_options::update_existing, error);
            }
        }
        if (error) {
            report.errors.push_back("shaders: " + error.message());
            error.clear();
        }
    }

    // Leftovers of an earlier run (a file no longer shipped) go away.
    std::vector<fs::path> stale;
    for (fs::recursive_directory_iterator it(out_assets, error), end; it != end; it.increment(error)) {
        if (error) {
            break;
        }
        if (it->is_regular_file() && !new_stamps.contains(utf8_string(fs::relative(it->path(), out_assets)))) {
            stale.push_back(it->path());
        }
    }
    for (const fs::path& path : stale) {
        fs::remove(path, error);
    }

    // Read back with the engine's loaders.
    for (const auto& [key, kind] : shipped) {
        const std::string path = utf8_string(out_assets / utf8_path(key));
        const std::string extension = extension_of(key);
        try {
            if (extension == ".gltf" || extension == ".glb") {
                GltfOptions gltf;
                gltf.decode_external_images = false;
                (void)load_gltf(path, gltf);
                continue;
            }
            const auto bytes = read_file_bytes(path);
            if (!bytes) {
                report.errors.push_back("'" + key + "' missing from the output");
                continue;
            }
            const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
            if (extension == ".ktx2") {
                (void)decode_ktx2(bytes->data(), bytes->size(), key, TextureSettings{}, bc);
            } else if (extension == ".png" || extension == ".jpg" || extension == ".jpeg") {
                (void)decode_image(bytes->data(), bytes->size(), key);
            } else if (extension == ".wav" || extension == ".ogg" || extension == ".flac") {
                (void)decode_sound(*bytes, key);
            } else if (extension == ".json" && key.starts_with(maps_folder)) {
                (void)MapData::parse(text, key);
            } else if (extension == ".json" && key.starts_with(effects_folder)) {
                (void)ParticleEffect::parse(text, key);
            } else if (extension == ".json" && key.starts_with(animations_folder)) {
                (void)AnimationSet::parse(text, key);
            } else if (extension == ".json") {
                (void)nlohmann::json::parse(text);
            }
        } catch (const std::exception& e) {
            report.errors.push_back("read-back: " + std::string(e.what()));
        }
    }

    // Licenses: a shipped asset of a credited kind needs a credits entry for its file or folder.
    std::vector<std::string> credited;        // folders: an entry covers the files of its file's folder
    std::set<std::string> credited_files;     // and its file itself (the only way for one at the root)
    for (const char* credits : {"credits_moteur.json", "credits.json"}) {
        if (!shipped.contains(credits)) {
            continue;
        }
        try {
            const auto bytes = read_file_bytes(sources.at(credits).absolute);
            const nlohmann::json doc = nlohmann::json::parse(bytes->begin(), bytes->end());
            for (const auto& [section, entries] : doc.items()) {
                if (!entries.is_array()) {
                    continue;
                }
                for (const nlohmann::json& entry : entries) {
                    for (const char* field : {"file", "license_file"}) {
                        std::string file = entry.value(field, std::string());
                        if (file.starts_with("assets/")) {
                            credited_files.insert(file.substr(7));
                            credited.push_back(directory_of(file.substr(7)));
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            report.warnings.push_back(std::string("cannot read the credits: ") + e.what());
        }
    }
    for (const auto& [key, kind] : shipped) {
        const bool needs_credit = key.starts_with("models/") || key.starts_with("audio/") ||
                                  key.starts_with("environments/") || extension_of(key) == ".ttf";
        if (!needs_credit || extension_of(key) == ".bin") {
            continue;
        }
        // An empty folder (a file at the root) covers only its own file, not every asset.
        const bool covered = credited_files.contains(key) ||
                             std::any_of(credited.begin(), credited.end(), [&key](const std::string& folder) {
                                 return !folder.empty() && key.starts_with(folder);
                             });
        if (!covered) {
            report.warnings.push_back("'" + key + "': no credits entry covers this file");
        }
    }

    // Orphans: sources nothing ships (the tables' JSON excepted: they ship compiled).
    for (const auto& [key, source] : sources) {
        if (!shipped.contains(key) && key != manifest_name && (data_folder.empty() || !key.starts_with(data_folder))) {
            report.orphans.push_back(key);
        }
    }

    // The archives: the manifest's groups, the rest and the shaders into "common".
    if (report.ok()) {
        struct Group {
            std::string name;
            std::vector<std::string> globs;
            std::vector<ArchiveInput> inputs;
        };
        std::vector<Group> groups;
        for (const nlohmann::json& group : manifest.value("archives", nlohmann::json::array())) {
            groups.push_back({manifest_value(group, "name", "nom", std::string("unnamed"), report, manifest_name),
                              manifest_value(group, "include", "inclure", std::vector<std::string>{}, report, manifest_name), {}});
        }
        groups.push_back({"common", {"**"}, {}});
        const auto add = [&](const fs::path& folder, const std::string& prefix, bool shaders) {
            for (fs::recursive_directory_iterator it(folder, error), end; it != end; it.increment(error)) {
                if (error || !it->is_regular_file()) {
                    continue;
                }
                const std::string key = utf8_string(fs::relative(it->path(), folder));
                auto bytes = read_file_bytes(utf8_string(it->path()));
                if (!bytes) {
                    report.errors.push_back("cannot read '" + key + "' for the archive");
                    continue;
                }
                for (Group& group : groups) {
                    const bool match = shaders ? group.name == "common"
                                               : std::any_of(group.globs.begin(), group.globs.end(),
                                                             [&key](const std::string& glob) { return glob_match(glob, key); });
                    if (match) {
                        group.inputs.push_back({prefix + key, std::move(*bytes), archive_should_compress(key)});
                        break;
                    }
                }
            }
        };
        add(out_assets, "assets/", false);
        if (fs::is_directory(out / "shaders", error)) {
            add(out / "shaders", "shaders/", true);
        }
        const fs::path packs = out / "archives";
        fs::remove_all(packs, error);
        fs::create_directories(packs, error);
        for (Group& group : groups) {
            if (group.inputs.empty()) {
                continue;
            }
            CookReport::Pack pack;
            pack.name = group.name + ".pak";
            pack.files = group.inputs.size();
            std::string pack_error;
            const std::string path = utf8_string(packs / utf8_path(pack.name));
            if (!write_archive(path, std::move(group.inputs), pack_error)) {
                report.errors.push_back(pack_error);
                continue;
            }
            pack.bytes = static_cast<std::size_t>(fs::file_size(utf8_path(path), error));
            report.archives.push_back(pack);
        }
    }

    nlohmann::json written = {{"version", kCookVersion}, {"files", new_stamps}, {"transcoded", new_transcoded}};
    std::string write_error;
    if (!write_file_atomic(stamps_path, written.dump(1), write_error)) {
        report.errors.push_back(write_error);
    }
    return report;
}

int cook_main(int argc, char* argv[], std::function<bool(const std::string&, const std::string&, DataIssues&)> compile_data) {
    CookOptions options;
    options.compile_data = std::move(compile_data);
    bool list_orphans = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--sources") {
            while (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0) {
                options.sources.push_back(argv[++i]);
            }
        } else if (arg == "--manifest" && i + 1 < argc) {
            options.manifest = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            options.output = argv[++i];
        } else if (arg == "--shaders" && i + 1 < argc) {
            options.shaders = argv[++i];
        } else if (arg == "--force") {
            options.force = true;
        } else if (arg == "--orphans") {
            list_orphans = true;
        } else {
            std::cerr << "usage: " << argv[0]
                      << " --sources DIR... --manifest FILE --output DIR [--shaders DIR] [--force] [--orphans]\n";
            return 2;
        }
    }
    if (options.sources.empty() || options.manifest.empty() || options.output.empty()) {
        std::cerr << "cook: --sources, --manifest and --output are required\n";
        return 2;
    }
    try {
        const Uint64 start = SDL_GetPerformanceCounter();
        const CookReport report = cook(options);
        std::cout << report.text();
        if (list_orphans) {
            for (const std::string& orphan : report.orphans) {
                std::cout << "not shipped: " << orphan << '\n';
            }
        }
        std::cout << "duration: "
                  << static_cast<double>(SDL_GetPerformanceCounter() - start) / static_cast<double>(SDL_GetPerformanceFrequency())
                  << " s\n";
        return report.ok() ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

}  // namespace moteur
