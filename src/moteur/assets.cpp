#include "moteur/assert.hpp"
#include "moteur/assets.hpp"

#include <SDL3/SDL.h>

#if !defined(MOTEUR_SHIPPING)
#include <efsw/efsw.hpp>
#endif

#include <any>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <system_error>
#include <utility>

#include "moteur/jobs.hpp"
#include "moteur/ktx_texture.hpp"
#include "moteur/model_import.hpp"
#include "moteur/file_io.hpp"
#include "moteur/paths.hpp"

namespace moteur {

namespace {

std::filesystem::path utf8_path(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

std::string utf8_string(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}

// The directory part of a key, with its trailing '/', or "" at the root.
std::string directory_of(const std::string& key) {
    const std::size_t slash = key.rfind('/');
    return slash == std::string::npos ? std::string() : key.substr(0, slash + 1);
}

// The options that change a texture belong to its key: the same image as a color and as data are
// two textures.
std::string texture_key(const std::string& key, const TextureSettings& settings) {
    std::string suffix;
    const auto add = [&suffix](const char* flag) { suffix += (suffix.empty() ? "#" : ",") + std::string(flag); };
    if (settings.srgb) {
        add("srgb");
    }
    if (settings.mipmaps) {
        add("mipmaps");
    }
    if (!settings.premultiply) {
        add("straight");
    }
    if (settings.normal_map) {
        add("normal");
    }
    return key + suffix;
}

// Magenta and black checks: impossible to miss, and obviously not a real texture.
Texture make_placeholder_texture(Renderer& renderer) {
    constexpr int kSize = 8;
    Image image;
    image.width = kSize;
    image.height = kSize;
    image.pixels.resize(kSize * kSize * 4);
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const bool on = ((x / 2) + (y / 2)) % 2 == 0;
            std::uint8_t* pixel = &image.pixels[(static_cast<std::size_t>(y) * kSize + static_cast<std::size_t>(x)) * 4];
            pixel[0] = on ? 255 : 0;
            pixel[1] = 0;
            pixel[2] = on ? 255 : 0;
            pixel[3] = 255;
        }
    }
    return renderer.create_texture(image, "placeholder texture");
}

Model make_placeholder_model(Renderer& renderer) {
    ModelData data;
    ModelPart part;
    part.name = "placeholder";
    part.mesh = make_cube();
    part.material = 0;
    data.parts.push_back(std::move(part));
    ModelMaterial material;
    material.name = "placeholder";
    material.base_color = {1.0f, 0.0f, 1.0f, 1.0f};
    material.metallic = 0.0f;
    material.roughness = 1.0f;
    data.materials.push_back(material);
    return Model::create(renderer, data, "placeholder model");
}

// The stamp of an imported model: what it is made from (its file and the files next to it it uses,
// read through read_file()), its import settings and the importer's version. 0 if a file is missing.
std::uint64_t model_stamp(const std::string& root, const std::string& key, const std::vector<std::string>& files,
                          const ModelImportSettings& settings) {
    std::uint64_t hash = 14695981039346656037ull ^ (static_cast<std::uint64_t>(kPreparedModelVersion) << 32) ^ settings.hash();
    const auto add = [&hash](const FileData& file) {
        const auto* bytes = static_cast<const std::uint8_t*>(file.data());
        for (std::size_t i = 0; i < file.size(); ++i) {
            hash = (hash ^ bytes[i]) * 1099511628211ull;
        }
        hash = (hash ^ file.size()) * 1099511628211ull;
    };
    try {
        add(read_file(root + key));
        const std::string directory = directory_of(key);
        for (const std::string& file : files) {
            add(read_file(std::string(root).append(normalize_asset_path(std::string(directory).append(file)))));
        }
    } catch (const std::exception&) {
        return 0;
    }
    return hash == 0 ? 1 : hash;
}

// The files a model is made from, as they are on the disk now: their size and time, beside its
// prepared form (`.mmdl.files`): when nothing changed, the import is known to be up to date without
// reading the sources again (development).
nlohmann::json source_signature(const std::string& root, const std::string& key, const std::vector<std::string>& files) {
    nlohmann::json out = nlohmann::json::array();
    std::vector<std::string> paths = {root + key};
    const std::string directory = directory_of(key);
    for (const std::string& file : files) {
        paths.push_back(std::string(root).append(normalize_asset_path(std::string(directory).append(file))));
    }
    for (const std::string& path : paths) {
        const std::string disk = disk_path(path);
        std::error_code error;
        const auto size = std::filesystem::file_size(utf8_path(disk), error);
        out.push_back({disk, error ? -1 : static_cast<long long>(size), file_time(disk)});
    }
    return out;
}

// The default import cache: a hidden folder of the assets, where the shipped game's archives have
// the prepared forms (see cook()).
std::string default_import_cache(const std::string& root) {
    return root + ".imported/";
}

// The model of `key` as the engine draws it (milestone 9, part 4): its prepared form if it is up to
// date (or if the source is not there: the shipped game), else imported now from its glTF and
// written to the import cache. Any thread.
ModelData load_imported_model(const std::string& root, const std::string& cache, const std::string& key,
                              const ModelImportSettings& settings) {
    const std::string artifact = cache + key + ".mmdl";
    // One import of a file at a time (its model, skeleton and clips may be prefetched together): the
    // others wait, then find its result in the import cache.
    static std::mutex locks_mutex;
    static std::map<std::string, std::shared_ptr<std::mutex>> locks;
    std::shared_ptr<std::mutex> lock_of_file;
    {
        const std::lock_guard guard(locks_mutex);
        std::shared_ptr<std::mutex>& slot = locks[artifact];
        if (!slot) {
            slot = std::make_shared<std::mutex>();
        }
        lock_of_file = slot;
    }
    const std::lock_guard importing(*lock_of_file);
    const bool has_source = file_exists(root + key);
    if (file_exists(artifact)) {
        try {
            const FileData file = read_file(artifact);
            std::uint64_t stamp = 0;
            ModelData data = decode_prepared_model(file.data(), file.size(), key, &stamp);
            if (!has_source) {
                return data;
            }
            // The quick check: the same files, sizes and times as when it was imported (and the
            // same settings and importer: in the stamp, saved with them).
            const nlohmann::json signature = source_signature(root, key, data.files);
            try {
                const FileData seen = read_file(artifact + ".files");
                const nlohmann::json saved = nlohmann::json::parse(std::string_view(static_cast<const char*>(seen.data()), seen.size()));
                if (saved.at("files") == signature && saved.at("stamp") == std::to_string(stamp) &&
                    saved.at("settings") == std::to_string(settings.hash())) {
                    return data;
                }
            } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): no signature yet: the full check below
            }
            const std::uint64_t now = model_stamp(root, key, data.files, settings);
            if (now == stamp) {
                const std::string text = nlohmann::json{{"stamp", std::to_string(stamp)}, {"settings", std::to_string(settings.hash())},
                                                        {"files", signature}}.dump();
                std::string ignored;
                (void)write_file_atomic(artifact + ".files", text, ignored);
                return data;
            }
            SDL_Log("Assets: '%s' changed since its import (%llx, imported %llx): imported again", key.c_str(),
                    static_cast<unsigned long long>(now), static_cast<unsigned long long>(stamp));
        } catch (const std::exception& e) {
            if (!has_source) {
                throw;
            }
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Assets: '%s' imported again (%s)", key.c_str(), e.what());
        }
    }
    const Uint64 start = SDL_GetPerformanceCounter();
    GltfOptions options;
    options.decode_external_images = false;  // through the texture cache
    options.require_meshes = false;          // a file of clips is imported too
    ModelData data = load_gltf(root + key, options);
    import_model(data, settings, key);
    [[maybe_unused]] const std::uint64_t stamp = model_stamp(root, key, data.files, settings);  // kept in development only
#if !defined(MOTEUR_SHIPPING)
    const std::vector<std::uint8_t> bytes = encode_prepared_model(data, stamp);
    std::error_code ignored;
    std::filesystem::create_directories(utf8_path(artifact).parent_path(), ignored);
    std::string error;
    if (!write_file_atomic(artifact, bytes.data(), bytes.size(), error)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Assets: cannot keep the import of '%s': %s", key.c_str(), error.c_str());
    } else {
        const std::string text = nlohmann::json{{"stamp", std::to_string(stamp)}, {"settings", std::to_string(settings.hash())},
                                                {"files", source_signature(root, key, data.files)}}.dump();
        (void)write_file_atomic(artifact + ".files", text, error);
    }
#endif
    std::size_t levels = 0;
    for (const ModelPart& part : data.parts) {
        levels += part.mesh.lods.size();
    }
    SDL_Log("Assets: '%s' imported in %.0f ms (%zu parts, %zu levels of detail)", key.c_str(),
            static_cast<double>(SDL_GetPerformanceCounter() - start) * 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency()),
            data.parts.size(), levels);
    return data;
}

}  // namespace

#if defined(MOTEUR_SHIPPING)
// The shipped game does not watch its files (no efsw): see enable_hot_reload().
class Assets::Watcher {};
#else
// Receives efsw's notifications on its own thread; update() takes them on the main thread.
class Assets::Watcher : public efsw::FileWatchListener {
public:
    explicit Watcher(std::filesystem::path source) : source_(std::move(source)) {}

    bool start() {
        const efsw::WatchID id = watcher_.addWatch(utf8_string(source_), this, true);
        if (id < 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Assets: cannot watch '%s': %s", utf8_string(source_).c_str(),
                         efsw::Errors::Log::getLastErrorLog().c_str());
            return false;
        }
        watcher_.watch();
        return true;
    }

    void handleFileAction(efsw::WatchID, const std::string& dir, const std::string& filename, efsw::Action action,
                          const std::string&) override {
        if (action != efsw::Actions::Add && action != efsw::Actions::Modified && action != efsw::Actions::Moved) {
            return;
        }
        const std::filesystem::path relative = (utf8_path(dir) / utf8_path(filename)).lexically_relative(source_);
        const std::string text = utf8_string(relative);
        if (text.empty() || text.rfind("..", 0) == 0) {
            return;
        }
        const std::lock_guard lock(mutex_);
        changed_[text] = SDL_GetTicks();
    }

    // The files that have not changed for `quiet_ms`, removed from the pending list.
    std::vector<std::string> settled(Uint64 quiet_ms) {
        std::vector<std::string> files;
        const Uint64 now = SDL_GetTicks();
        const std::lock_guard lock(mutex_);
        for (auto it = changed_.begin(); it != changed_.end();) {
            if (now - it->second >= quiet_ms) {
                files.push_back(it->first);
                it = changed_.erase(it);
            } else {
                ++it;
            }
        }
        return files;
    }

    const std::filesystem::path& source() const { return source_; }

private:
    std::filesystem::path source_;
    std::mutex mutex_;
    std::map<std::string, Uint64> changed_;  // relative path -> time of the last change
    efsw::FileWatcher watcher_;              // last: stopped (its thread joined) before the rest goes
};
#endif

Assets::Assets(Renderer& renderer, std::string root)
    : renderer_(renderer),
      root_(std::move(root)),
      textures_("texture", [&renderer] { return make_placeholder_texture(renderer); },
                [](const Texture& texture) { return texture.gpu_bytes; }),
      models_("model", [&renderer] { return make_placeholder_model(renderer); },
              [](const Model& model) { return model.gpu_bytes; },
              // Scenes keep pointers to the parts of a model (its meshes, its textures): keep them valid.
              [](Model& current, Model&& fresh) { current.replace_in_place(std::move(fresh)); }),
      environments_("environment",
                    [&renderer] { return Environment::create(renderer, make_sky(64, 32), "placeholder environment"); },
                    [](const Environment& environment) { return environment.gpu_bytes; }),
      fonts_("font", {}, [](const Font& font) { return font.gpu_bytes(); }),
      atlases_("atlas", {}, [](const TextureAtlas& atlas) { return atlas.gpu_bytes(); }),
      sounds_("sound", [] { return placeholder_sound(); }, [](const Sound& sound) { return sound.bytes(); }),
      musics_("music", [] { return Music{}; }, [](const Music& music) { return music.memory(); }),
      // Players point to skeletons and clips: a reload keeps them where they are.
      skeletons_("skeleton", {}, [](const Skeleton& skeleton) { return skeleton.bytes(); },
                 [](Skeleton& current, Skeleton&& fresh) { current.replace_in_place(std::move(fresh)); }),
      clips_("clips", {}, [](const ClipLibrary& clips) { return clips.bytes(); },
             [](ClipLibrary& current, ClipLibrary&& fresh) { current.replace_in_place(std::move(fresh)); }),
      animation_sets_("animation set"),
      maps_("map", {}, {}, [](MapData& current, MapData&& fresh) {
          fresh.set_revision(current.revision() + 1);
          current = std::move(fresh);
      }),
      // A broken effect is only missing from the screen: an effect without emitters stands in.
      effects_("particle effect", [] { return ParticleEffect{}; }),
      prefabs_("prefab"),
      import_cache_(default_import_cache(root_)) {}


std::string Assets::file_path(std::string_view path) const {
    return root_ + normalize_asset_path(path);
}

bool Assets::exists(std::string_view path) const {
    // In the archives, or on disk; a model of the shipped game is there by its prepared form only.
    const std::string key = normalize_asset_path(path);
    return file_exists(root_ + key) || file_exists(import_cache_ + key + ".mmdl");
}

std::vector<std::string> Assets::import_all(const std::string& root, const std::string& cache, const AssetRegistry& registry,
                                            std::size_t& failures) {
    struct Task {
        std::string key;
        ModelImportSettings settings;
        std::string line;
        bool failed = false;
    };
    std::vector<Task> tasks;
    for (const auto& [id, record] : registry.records()) {
        if (record.key.ends_with(".glb") || record.key.ends_with(".gltf")) {
            std::vector<std::string> warnings;
            tasks.push_back({record.key, ModelImportSettings::from_json(record.import, warnings), {}, false});
        }
    }
    std::vector<JobHandle> handles;
    handles.reserve(tasks.size());
    for (Task& task : tasks) {
        handles.push_back(jobs().submit([&root, &cache, &task] {
            const Uint64 start = SDL_GetPerformanceCounter();
            try {
                (void)load_imported_model(root, cache, task.key, task.settings);
                task.line = task.key + ": ready (" +
                            std::to_string(static_cast<long long>(static_cast<double>(SDL_GetPerformanceCounter() - start) * 1000.0 /
                                                                  static_cast<double>(SDL_GetPerformanceFrequency()))) +
                            " ms)";
            } catch (const std::exception& e) {
                task.line = task.key + ": " + e.what();
                task.failed = true;
            }
        }));
    }
    for (const JobHandle& handle : handles) {
        jobs().wait(handle);
    }
    std::vector<std::string> lines;
    failures = 0;
    for (const Task& task : tasks) {
        lines.push_back(task.line);
        failures += task.failed ? 1 : 0;
    }
    return lines;
}

ModelImportSettings Assets::model_import_settings(const std::string& key) const {
    const AssetRecord* record = registry_.find(key);
    if (record == nullptr) {
        return {};
    }
    std::vector<std::string> warnings;
    ModelImportSettings settings = ModelImportSettings::from_json(record->import, warnings);
    for (const std::string& warning : warnings) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Assets: %s.meta: %s", key.c_str(), warning.c_str());
    }
    return settings;
}

std::string Assets::model_error(std::string_view path) const {
    return models_.error(normalize_asset_path(path));
}

std::string Assets::key_of(std::string_view path) const {
    std::string key = normalize_asset_path(path);
    check_asset_case(root_, key);
    return key;
}

// --- Loading in the background ------------------------------------------------------------------

// What a prefetch leaves for the main thread: one of these, by the kind of asset.
struct DecodedTexture {
    std::optional<CompressedImage> compressed;  // a KTX2 file
    Image image;                                // any other image
};

struct PreparedModel {
    ModelData data;
    std::vector<std::pair<std::string, DecodedTexture>> textures;  // texture keys of its image files
    std::vector<std::optional<CompressedImage>> embedded;          // its images inside it, transcoded (by index)
};

struct Assets::Prepared {
    JobHandle job;
    std::any value;                  // DecodedTexture, PreparedModel, Skeleton, ClipLibrary, Sound
    std::vector<std::string> files;  // the files it read, for the hot reload
};

// The watchers go first: their threads must not call into a half-destroyed manager. Then the
// prefetches still running: their work reads files and writes the import cache, which must be done
// before the program ends (SDL quits after the manager).
Assets::~Assets() {
    watcher_.reset();
    mounted_watchers_.clear();
    for (auto& [key, prepared] : prepared_) {
        try {
            jobs().wait(prepared->job);
        } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): its error would have been the later load's
        }
    }
}

namespace {

DecodedTexture decode_texture_file(const std::string& path, const std::string& key, const TextureSettings& settings,
                                   const CompressedFormats& formats) {
    const FileData file = read_file(path);
    DecodedTexture decoded;
    if (is_ktx2(file.data(), file.size())) {
        decoded.compressed = decode_ktx2(file.data(), file.size(), key, settings, formats);
    } else {
        decoded.image = decode_image(file.data(), file.size(), key);
    }
    return decoded;
}

}  // namespace

void Assets::prefetch(const std::string& prepared_key, std::function<void(Prepared&)> work) {
    if (prepared_.contains(prepared_key)) {
        return;
    }
    auto prepared = std::make_shared<Prepared>();
    prepared->job = jobs().submit([prepared, work = std::move(work)] { work(*prepared); });
    prepared_.emplace(prepared_key, std::move(prepared));
}

std::shared_ptr<Assets::Prepared> Assets::take_prepared(const std::string& prepared_key) {
    const auto found = prepared_.find(prepared_key);
    if (found == prepared_.end()) {
        return nullptr;
    }
    std::shared_ptr<Prepared> prepared = std::move(found->second);
    prepared_.erase(found);
    jobs().wait(prepared->job);  // rethrows the decoding's error, inside the cache's loader
    return prepared;
}

std::size_t Assets::prefetching() const {
    std::size_t running = 0;
    for (const auto& [key, prepared] : prepared_) {
        (void)key;
        running += prepared->job.done() ? 0 : 1;
    }
    return running;
}

void Assets::prefetch_texture(std::string_view path, const TextureSettings& settings) {
    MOTEUR_ASSERT_MAIN_THREAD("Assets");
    const std::string key = normalize_asset_path(path);
    const std::string cache_key = texture_key(key, settings);
    if (textures_.contains(cache_key)) {
        return;
    }
    prefetch("texture:" + cache_key, [path = root_ + key, key, settings, formats = renderer_.compressed_formats()](Prepared& out) {
        out.value = decode_texture_file(path, key, settings, formats);
    });
}

void Assets::prefetch_model(std::string_view path) {
    MOTEUR_ASSERT_MAIN_THREAD("Assets");
    const std::string key = normalize_asset_path(path);
    if (models_.contains(key)) {
        return;
    }
    prefetch("model:" + key, [root = root_, cache = import_cache_, key, prefer_ktx2 = prefer_ktx2_, formats = renderer_.compressed_formats(),
                              import = model_import_settings(key)](Prepared& out) {
        PreparedModel model;
        if (prefer_ktx2) {
            model.data = load_imported_model(root, cache, key, import);
        } else {  // to compare with the plain images: the glTF as it is
            GltfOptions options;
            options.decode_external_images = false;
            options.prefer_ktx2 = false;
            model.data = load_gltf(root + key, options);
        }
        // Its images inside it (KTX2), transcoded here rather than on the main thread.
        model.embedded.resize(model.data.images.size());
        jobs().parallel_for(model.data.images.size(), [&model, &formats](std::size_t i) {
            const ModelImage& image = model.data.images[i];
            if (image.file.empty() && !image.ktx2.empty()) {
                try {
                    model.embedded[i] = decode_ktx2(image.ktx2.data(), image.ktx2.size(), image.name,
                                                    model_texture_settings(image), formats);
                } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): Model::create decodes it again and reports
                }
            }
        });
        // Its image files, decoded as the texture cache will ask for them (failures are left to the
        // cache, which shows its placeholder and logs).
        const std::string directory = directory_of(key);
        for (const ModelImage& image : model.data.images) {
            if (image.file.empty()) {
                continue;
            }
            const std::string image_key = normalize_asset_path(directory + image.file);
            const TextureSettings settings = model_texture_settings(image);
            try {
                model.textures.emplace_back(texture_key(image_key, settings),
                                            decode_texture_file(root + image_key, image_key, settings, formats));
            } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): reported by the texture cache
                // Not prefetched: the texture cache decodes it again when asked, then shows its
                // placeholder and logs the error once; logging here too would report it twice.
            }
        }
        out.value = std::move(model);
    });
}

Asset<Texture> Assets::texture(std::string_view path, const TextureSettings& settings) {
    MOTEUR_ASSERT_MAIN_THREAD("Assets");
    const std::string key = normalize_asset_path(path);
    return textures_.get(texture_key(key, settings), [this, key, settings](std::vector<std::string>& files) {
        files.push_back(key);
        check_asset_case(root_, key);
        if (const std::shared_ptr<Prepared> prepared = take_prepared("texture:" + texture_key(key, settings))) {
            DecodedTexture& decoded = std::any_cast<DecodedTexture&>(prepared->value);
            return decoded.compressed ? create_compressed_texture(renderer_, *decoded.compressed, key.c_str())
                                      : renderer_.create_texture(decoded.image, settings, key.c_str());
        }
        const FileData file = read_file(root_ + key);
        if (is_ktx2(file.data(), file.size())) {
            return create_compressed_texture(
                renderer_, decode_ktx2(file.data(), file.size(), key, settings, renderer_.compressed_formats()), key.c_str());
        }
        return renderer_.create_texture(decode_image(file.data(), file.size(), key), settings, key.c_str());
    });
}

Asset<Model> Assets::model(std::string_view path) {
    MOTEUR_ASSERT_MAIN_THREAD("Assets");
    const std::string key = normalize_asset_path(path);
    return models_.get(key, [this, key](std::vector<std::string>& files) {
        files.push_back(key);
        check_asset_case(root_, key);
        const std::string directory = directory_of(key);
        ModelData data;
        std::vector<std::shared_ptr<Texture>> embedded;  // its images inside it, transcoded in the background
        if (const std::shared_ptr<Prepared> prepared = take_prepared("model:" + key)) {
            PreparedModel& model = std::any_cast<PreparedModel&>(prepared->value);
            data = std::move(model.data);
            embedded.resize(model.embedded.size());
            for (std::size_t i = 0; i < model.embedded.size(); ++i) {
                if (model.embedded[i]) {
                    embedded[i] = std::make_shared<Texture>(
                        create_compressed_texture(renderer_, *model.embedded[i], data.images[i].name.c_str()));
                }
            }
            for (auto& [texture_cache_key, decoded] : model.textures) {  // for the texture cache, just below
                if (!textures_.contains(texture_cache_key) && !prepared_.contains("texture:" + texture_cache_key)) {
                    auto ready = std::make_shared<Prepared>();
                    ready->value = std::move(decoded);
                    prepared_.emplace("texture:" + texture_cache_key, std::move(ready));
                }
            }
        } else if (prefer_ktx2_) {
            data = load_imported_model(root_, import_cache_, key, model_import_settings(key));
        } else {  // to compare with the plain images: the glTF as it is
            GltfOptions options;
            options.decode_external_images = false;
            options.prefer_ktx2 = false;
            data = load_gltf(root_ + key, options);
        }
        for (const std::string& file : data.files) {
            files.push_back(normalize_asset_path(directory + file));
        }
        if (data.parts.empty()) {  // a file of clips: no model in it
            throw std::runtime_error("Model '" + key + "': no triangle mesh in the default scene");
        }
        return Model::create(
            renderer_, data, key,
            [this, &directory](const ModelImage& image, const TextureSettings& settings) {
                return texture(directory + image.file, settings).handle();
            },
            embedded);
    });
}

Asset<Environment> Assets::environment(std::string_view path) {
    MOTEUR_ASSERT_MAIN_THREAD("Assets");
    const std::string key = normalize_asset_path(path);
    return environments_.get(key, [this, key](std::vector<std::string>& files) {
        files.push_back(key);
        check_asset_case(root_, key);
        return Environment::create(renderer_, load_environment(root_ + key), key.c_str());
    });
}

Asset<Font> Assets::font(std::string_view path, float pixel_height) {
    MOTEUR_ASSERT_MAIN_THREAD("Assets");
    const std::string key = normalize_asset_path(path);
    return fonts_.get(key + "#" + std::to_string(pixel_height), [this, key, pixel_height](std::vector<std::string>& files) {
        files.push_back(key);
        check_asset_case(root_, key);
        return Font::load(renderer_, root_ + key, pixel_height);
    });
}

Asset<TextureAtlas> Assets::atlas(std::string_view path) {
    MOTEUR_ASSERT_MAIN_THREAD("Assets");
    const std::string key = key_of(path);
    return atlases_.get(key, [this, key](std::vector<std::string>&) {  // no files: never reloaded
        return TextureAtlas::load(renderer_, root_ + key);
    });
}

void Assets::prefetch_sound(std::string_view path) {
    MOTEUR_ASSERT_MAIN_THREAD("Assets");
    const std::string key = normalize_asset_path(path);
    if (sounds_.contains(key)) {
        return;
    }
    prefetch("sound:" + key, [path = root_ + key, key](Prepared& out) {
        const FileData file = read_file(path);
        const auto* bytes = static_cast<const std::uint8_t*>(file.data());
        out.value = decode_sound({bytes, file.size()}, key);
    });
}

Asset<Sound> Assets::sound(std::string_view path) {
    MOTEUR_ASSERT_MAIN_THREAD("Assets");
    const std::string key = normalize_asset_path(path);
    return sounds_.get(key, [this, key](std::vector<std::string>& files) {
        files.push_back(key);
        check_asset_case(root_, key);
        if (const std::shared_ptr<Prepared> prepared = take_prepared("sound:" + key)) {
            return std::move(std::any_cast<Sound&>(prepared->value));
        }
        const FileData file = read_file(root_ + key);
        const auto* bytes = static_cast<const std::uint8_t*>(file.data());
        return decode_sound({bytes, file.size()}, key);
    });
}

Asset<Music> Assets::music(std::string_view path) {
    MOTEUR_ASSERT_MAIN_THREAD("Assets");
    const std::string key = normalize_asset_path(path);
    return musics_.get(key, [this, key](std::vector<std::string>& files) {
        files.push_back(key);
        check_asset_case(root_, key);
        const FileData file = read_file(root_ + key);
        const auto* bytes = static_cast<const std::uint8_t*>(file.data());
        return open_music(std::vector<std::uint8_t>(bytes, bytes + file.size()), key);
    });
}

namespace {

// A glTF file read for its skeleton and clips only.
// The clips of an imported file (their prepared form), or of one read from glTF.
ClipLibrary clips_of(const ModelData& data, const std::string& key) {
    if (!data.prepared_clips.empty()) {
        return ClipLibrary::load(data.skeleton, data.prepared_clips, key);
    }
    return ClipLibrary::create(data.skeleton, data.clips, key);
}

// The skeleton and clips of a file, from its import (milestone 9, part 4: the shipped game has only
// that form).
ModelData read_animation_file(const std::string& root, const std::string& cache, const std::string& key,
                              std::vector<std::string>& files, const ModelImportSettings& settings) {
    ModelData data = load_imported_model(root, cache, key, settings);
    for (const std::string& file : data.files) {
        files.push_back(normalize_asset_path(directory_of(key) + file));
    }
    if (data.skeleton.empty()) {
        throw std::runtime_error("Model '" + key + "': no skeleton (no skin, no animated node)");
    }
    return data;
}

}  // namespace

void Assets::prefetch_skeleton(std::string_view path) {
    MOTEUR_ASSERT_MAIN_THREAD("Assets");
    const std::string key = normalize_asset_path(path);
    if (skeletons_.contains(key)) {
        return;
    }
    prefetch("skeleton:" + key, [root = root_, cache = import_cache_, key, import = model_import_settings(key)](Prepared& out) {
        out.value = std::make_shared<Skeleton>(Skeleton::create(read_animation_file(root, cache, key, out.files, import).skeleton, key));
    });
}

void Assets::prefetch_clips(std::string_view path) {
    MOTEUR_ASSERT_MAIN_THREAD("Assets");
    const std::string key = normalize_asset_path(path);
    if (clips_.contains(key)) {
        return;
    }
    prefetch("clips:" + key, [root = root_, cache = import_cache_, key, import = model_import_settings(key)](Prepared& out) {
        const ModelData data = read_animation_file(root, cache, key, out.files, import);
        out.value = std::make_shared<ClipLibrary>(clips_of(data, key));
    });
}

Asset<Skeleton> Assets::skeleton(std::string_view path) {
    MOTEUR_ASSERT_MAIN_THREAD("Assets");
    const std::string key = normalize_asset_path(path);
    return skeletons_.get(key, [this, key](std::vector<std::string>& files) {
        files.push_back(key);
        check_asset_case(root_, key);
        if (const std::shared_ptr<Prepared> prepared = take_prepared("skeleton:" + key)) {
            files.insert(files.end(), prepared->files.begin(), prepared->files.end());
            return std::move(*std::any_cast<std::shared_ptr<Skeleton>&>(prepared->value));
        }
        return Skeleton::create(read_animation_file(root_, import_cache_, key, files, model_import_settings(key)).skeleton, key);
    });
}

Asset<ClipLibrary> Assets::clips(std::string_view path) {
    MOTEUR_ASSERT_MAIN_THREAD("Assets");
    const std::string key = normalize_asset_path(path);
    return clips_.get(key, [this, key](std::vector<std::string>& files) {
        files.push_back(key);
        check_asset_case(root_, key);
        if (const std::shared_ptr<Prepared> prepared = take_prepared("clips:" + key)) {
            files.insert(files.end(), prepared->files.begin(), prepared->files.end());
            return std::move(*std::any_cast<std::shared_ptr<ClipLibrary>&>(prepared->value));
        }
        const ModelData data = read_animation_file(root_, import_cache_, key, files, model_import_settings(key));
        return clips_of(data, key);
    });
}

Asset<AnimationSet> Assets::animation_set(std::string_view path) {
    MOTEUR_ASSERT_MAIN_THREAD("Assets");
    const std::string key = normalize_asset_path(path);
    return animation_sets_.get(key, [this, key](std::vector<std::string>& files) {
        files.push_back(key);
        check_asset_case(root_, key);
        const FileData file = read_file(root_ + key);
        return AnimationSet::parse({static_cast<const char*>(file.data()), file.size()}, key);
    });
}

Asset<MapData> Assets::map(std::string_view path) {
    MOTEUR_ASSERT_MAIN_THREAD("Assets");
    const std::string key = normalize_asset_path(path);
    return maps_.get(key, [this, key](std::vector<std::string>& files) {
        files.push_back(key);
        check_asset_case(root_, key);
        const FileData file = read_file(root_ + key);
        return MapData::parse({static_cast<const char*>(file.data()), file.size()}, key);
    });
}

Asset<ParticleEffect> Assets::particle_effect(std::string_view path) {
    MOTEUR_ASSERT_MAIN_THREAD("Assets");
    const std::string key = normalize_asset_path(path);
    return effects_.get(key, [this, key](std::vector<std::string>& files) {
        files.push_back(key);
        check_asset_case(root_, key);
        const FileData file = read_file(root_ + key);
        return ParticleEffect::parse({static_cast<const char*>(file.data()), file.size()}, key);
    });
}

Asset<Prefab> Assets::prefab(std::string_view path) {
    MOTEUR_ASSERT_MAIN_THREAD("Assets");
    const std::string key = normalize_asset_path(path);
    if (std::find(prefabs_loading_.begin(), prefabs_loading_.end(), key) != prefabs_loading_.end()) {
        std::string cycle;
        for (const std::string& loading : prefabs_loading_) {
            cycle += loading + " > ";
        }
        throw std::runtime_error("Prefab cycle: " + cycle + key);
    }
    return prefabs_.get(key, [this, key](std::vector<std::string>& files) {
        files.push_back(key);
        check_asset_case(root_, key);
        const FileData file = read_file(root_ + key);
        prefabs_loading_.push_back(key);
        struct Done {
            std::vector<std::string>& loading;
            ~Done() { loading.pop_back(); }
        } done{prefabs_loading_};
        DataIssues issues;
        Prefab prefab = Prefab::parse({static_cast<const char*>(file.data()), file.size()}, key, prefab_context(), issues);
        for (const DataIssue& issue : issues.all()) {
            if (issue.level == DataIssue::Level::Warning) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Prefab: %s: %s", issue.where.c_str(), issue.message.c_str());
            }
        }
        // A change in a base or a nested prefab reloads this one too.
        files.insert(files.end(), prefab.dependencies().begin(), prefab.dependencies().end());
        return prefab;
    });
}

void Assets::prefetch_prefab(std::string_view path) {
    MOTEUR_ASSERT_MAIN_THREAD("Assets");
    std::vector<std::string> pending = {normalize_asset_path(path)};
    std::vector<std::string> seen;
    while (!pending.empty()) {
        const std::string key = pending.back();
        pending.pop_back();
        if (std::find(seen.begin(), seen.end(), key) != seen.end() || prefabs_.contains(key)) {
            continue;
        }
        seen.push_back(key);
        nlohmann::json json;
        try {
            const FileData file = read_file(root_ + key);
            json = nlohmann::json::parse(std::string_view(static_cast<const char*>(file.data()), file.size()));
        } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): prefab() will report it
            continue;
        }
        // Every asset reference of the file: the models are prefetched, the prefabs read in turn.
        const auto visit = [&](const auto& self, const nlohmann::json& value) -> void {
            if (value.is_object()) {
                if (value.contains("path") || value.contains("id")) {
                    try {
                        const std::string target = resolve_reference(value);
                        if (target.ends_with(".glb") || target.ends_with(".gltf")) {
                            prefetch_model(target);
                        } else if (target.ends_with(".prefab.json")) {
                            pending.push_back(target);
                        }
                    } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): not a reference
                    }
                }
                for (const auto& [name, child] : value.items()) {
                    if ((name == "base" || name == "prefab") && child.is_string()) {
                        pending.push_back(normalize_asset_path(child.get<std::string>()));
                    } else {
                        self(self, child);
                    }
                }
            } else if (value.is_array()) {
                for (const nlohmann::json& child : value) {
                    self(self, child);
                }
            }
        };
        visit(visit, json);
    }
}

Prefab::Context Assets::prefab_context() {
    Prefab::Context context;
    context.prefab = [this](const std::string& path) -> std::shared_ptr<const Prefab> { return prefab(path).handle(); };
    context.asset = [this](const TypeInfo& type, const nlohmann::json& value, void* handle, std::string& problem) {
        return load_reflected(type, value, handle, problem);
    };
    return context;
}

std::string Assets::key_of(const AssetId& id) const {
    const AssetRecord* record = registry_.find(id);
    if (record == nullptr) {
        throw std::runtime_error("unknown asset identifier " + id.text());
    }
    return record->key;
}

std::string Assets::resolve_reference(const nlohmann::json& value) const {
    if (value.is_string()) {
        // A bare path (written by hand, or before identifiers); a bare identifier is accepted too.
        const std::string text = value.get<std::string>();
        if (const std::optional<AssetId> id = AssetId::parse(text)) {
            return key_of(*id);
        }
        return normalize_asset_path(text);
    }
    if (!value.is_object()) {
        throw std::runtime_error("expected an asset reference ({\"id\": ..., \"path\": ...} or a path), found " + value.dump());
    }
    const auto id_value = value.find("id");
    const auto path_value = value.find("path");
    const std::string path = path_value != value.end() && path_value->is_string() ? path_value->get<std::string>() : std::string();
    if (id_value != value.end() && id_value->is_string()) {
        const std::optional<AssetId> id = AssetId::parse(id_value->get<std::string>());
        if (!id) {
            throw std::runtime_error("\"" + id_value->get<std::string>() + "\" is not an asset identifier");
        }
        if (const AssetRecord* record = registry_.find(*id)) {
            return record->key;
        }
        if (path.empty()) {
            throw std::runtime_error("unknown asset " + id->text() + " (no path given)");
        }
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Assets: unknown identifier %s, '%s' used by its path", id->text().c_str(),
                    path.c_str());
    }
    if (path.empty()) {
        throw std::runtime_error("an asset reference without \"id\" nor \"path\"");
    }
    return normalize_asset_path(path);
}

nlohmann::json Assets::reference_to(const std::string& key) const {
    if (const AssetRecord* record = registry_.find(key)) {
        return {{"id", record->id.text()}, {"path", key}};
    }
    return key;
}

bool Assets::load_reflected(const TypeInfo& type, const nlohmann::json& value, void* handle, std::string& problem) {
    if (value.is_null() || (value.is_string() && value.get<std::string>().empty())) {
        return true;  // an empty handle (the field's default)
    }
    std::string path;
    try {
        path = resolve_reference(value);
    } catch (const std::exception& e) {
        problem = e.what();
        return false;
    }
    try {
        const std::type_index asset = type.asset_type;
        if (asset == typeid(Model)) {
            *static_cast<Asset<Model>*>(handle) = model(path);
        } else if (asset == typeid(Texture)) {
            *static_cast<Asset<Texture>*>(handle) = texture(path, TextureSettings{.srgb = true, .mipmaps = true});
        } else if (asset == typeid(Sound)) {
            *static_cast<Asset<Sound>*>(handle) = sound(path);
        } else if (asset == typeid(Music)) {
            *static_cast<Asset<Music>*>(handle) = music(path);
        } else if (asset == typeid(Skeleton)) {
            *static_cast<Asset<Skeleton>*>(handle) = skeleton(path);
        } else if (asset == typeid(ClipLibrary)) {
            *static_cast<Asset<ClipLibrary>*>(handle) = clips(path);
        } else if (asset == typeid(AnimationSet)) {
            *static_cast<Asset<AnimationSet>*>(handle) = animation_set(path);
        } else if (asset == typeid(ParticleEffect)) {
            *static_cast<Asset<ParticleEffect>*>(handle) = particle_effect(path);
        } else if (asset == typeid(MapData)) {
            *static_cast<Asset<MapData>*>(handle) = map(path);
        } else if (asset == typeid(Prefab)) {
            *static_cast<Asset<Prefab>*>(handle) = prefab(path);
        } else if (asset == typeid(Environment)) {
            *static_cast<Asset<Environment>*>(handle) = environment(path);
        } else {
            problem = "assets of this type are not loaded by the asset manager";
            return false;
        }
    } catch (const std::exception& e) {
        problem = e.what();
        return false;
    }
    return true;
}

nlohmann::json Assets::key_of_reflected(const TypeInfo& type, const void* handle) const {
    const std::type_index asset = type.asset_type;
    const auto key = [this, handle]<typename T>(const AssetCache<T>& cache) -> nlohmann::json {
        const T* pointer = static_cast<const Asset<T>*>(handle)->handle().get();
        if (pointer == nullptr) {
            return nullptr;
        }
        std::string found = cache.key_of(pointer);
        // A texture's key carries its settings after '#': the path is what comes before.
        found = found.substr(0, found.find('#'));
        return found.empty() ? nlohmann::json() : reference_to(found);
    };
    if (asset == typeid(Model)) {
        return key(models_);
    }
    if (asset == typeid(Texture)) {
        return key(textures_);
    }
    if (asset == typeid(Sound)) {
        return key(sounds_);
    }
    if (asset == typeid(Music)) {
        return key(musics_);
    }
    if (asset == typeid(Skeleton)) {
        return key(skeletons_);
    }
    if (asset == typeid(ClipLibrary)) {
        return key(clips_);
    }
    if (asset == typeid(AnimationSet)) {
        return key(animation_sets_);
    }
    if (asset == typeid(ParticleEffect)) {
        return key(effects_);
    }
    if (asset == typeid(MapData)) {
        return key(maps_);
    }
    if (asset == typeid(Prefab)) {
        return key(prefabs_);
    }
    if (asset == typeid(Environment)) {
        return key(environments_);
    }
    return nullptr;
}

ReadOptions Assets::read_options(std::uint32_t flags) {
    ReadOptions options;
    options.flags = flags;
    options.asset = [this](const TypeInfo& type, const nlohmann::json& value, void* handle, std::string& problem) {
        return load_reflected(type, value, handle, problem);
    };
    return options;
}

WriteOptions Assets::write_options(std::uint32_t flags) const {
    WriteOptions options;
    options.flags = flags;
    options.asset = [this](const TypeInfo& type, const void* handle) { return key_of_reflected(type, handle); };
    return options;
}

std::size_t Assets::collect_garbage() {
    MOTEUR_ASSERT_MAIN_THREAD("Assets");
    // Prefetched files nobody took: dropped once decoded (a job still running keeps its own
    // reference to the result, which goes with it).
    std::erase_if(prepared_, [](const auto& entry) { return entry.second->job.done(); });
    // Models first: they hold textures, which become free once the models are gone.
    std::size_t freed = models_.collect_garbage();
    freed += textures_.collect_garbage();
    freed += environments_.collect_garbage();
    freed += fonts_.collect_garbage();
    freed += atlases_.collect_garbage();
    freed += sounds_.collect_garbage();
    freed += musics_.collect_garbage();
    freed += skeletons_.collect_garbage();
    freed += clips_.collect_garbage();
    freed += animation_sets_.collect_garbage();
    freed += maps_.collect_garbage();
    freed += effects_.collect_garbage();
    // Prefabs hold models and other prefabs: freed first next time round, until nothing goes.
    for (std::size_t round = prefabs_.collect_garbage(); round > 0; round = prefabs_.collect_garbage()) {
        freed += round + models_.collect_garbage() + textures_.collect_garbage();
    }
    if (freed > 0) {
        SDL_Log("Assets: %zu freed", freed);
    }
    return freed;
}

bool Assets::watch_mounted_folder(const std::string& folder) {
#if defined(MOTEUR_SHIPPING)
    static_cast<void>(folder);
    return false;
#else
    auto watcher = std::make_unique<Watcher>(utf8_path(folder).lexically_normal());
    if (!watcher->start()) {
        return false;
    }
    mounted_watchers_.push_back(std::move(watcher));
    SDL_Log("Assets: hot reload of the mounted folder '%s' (in place)", folder.c_str());
    return true;
#endif
}

bool Assets::enable_hot_reload(const std::string& source_directory) {
#if defined(MOTEUR_SHIPPING)
    SDL_Log("Assets: no hot reload in the shipping configuration ('%s' ignored)", source_directory.c_str());
    return false;
#else
    watcher_.reset();
    auto watcher = std::make_unique<Watcher>(utf8_path(source_directory).lexically_normal());
    if (!watcher->start()) {
        return false;
    }
    watcher_ = std::move(watcher);
    SDL_Log("Assets: hot reload from '%s'", source_directory.c_str());
    return true;
#endif
}

void Assets::update() {
    MOTEUR_ASSERT_MAIN_THREAD("Assets");
#if !defined(MOTEUR_SHIPPING)
    constexpr Uint64 kQuietMs = 200;
    // Mounted folders are read in place: their changed files are only reloaded.
    for (const std::unique_ptr<Watcher>& mounted : mounted_watchers_) {
        for (const std::string& changed : mounted->settled(kQuietMs)) {
            std::string key;
            try {
                key = normalize_asset_path(changed);
            } catch (const std::invalid_argument&) {
                continue;
            }
            reload_file(key);
            for (const auto& listener : file_listeners_) {
                listener(key);
            }
        }
    }
    if (!watcher_) {
        return;
    }
    for (const std::string& changed : watcher_->settled(kQuietMs)) {
        std::string key;
        try {
            key = normalize_asset_path(changed);
        } catch (const std::invalid_argument&) {
            continue;
        }
        // The game reads the copy next to the executable: bring it up to date first.
        std::error_code error;
        const std::filesystem::path target = utf8_path(root_ + key);
        std::filesystem::create_directories(target.parent_path(), error);
        std::filesystem::copy_file(watcher_->source() / utf8_path(key), target,
                                   std::filesystem::copy_options::overwrite_existing, error);
        if (error) {
            if (std::filesystem::is_regular_file(watcher_->source() / utf8_path(key))) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Assets: cannot copy '%s': %s", key.c_str(),
                             error.message().c_str());
            }
            continue;  // a directory, or a file already gone again
        }
        reload_file(key);
        for (const auto& listener : file_listeners_) {
            listener(key);
        }
    }
#endif
}

void Assets::reload_file(const std::string& key) {
    // Textures before models: a model reloaded right after sees its new textures.
    for (const std::string& asset : textures_.keys_using(key)) {
        textures_.reload(asset);
    }
    for (const std::string& asset : models_.keys_using(key)) {
        models_.reload(asset);
    }
    for (const std::string& asset : environments_.keys_using(key)) {
        environments_.reload(asset);
    }
    for (const std::string& asset : fonts_.keys_using(key)) {
        fonts_.reload(asset);
    }
    // Voices keep the samples they play: a reload never pulls them from under the audio thread.
    for (const std::string& asset : sounds_.keys_using(key)) {
        sounds_.reload(asset);
    }
    for (const std::string& asset : musics_.keys_using(key)) {
        musics_.reload(asset);
    }
    for (const std::string& asset : skeletons_.keys_using(key)) {
        skeletons_.reload(asset);
    }
    for (const std::string& asset : clips_.keys_using(key)) {
        clips_.reload(asset);
    }
    for (const std::string& asset : animation_sets_.keys_using(key)) {
        animation_sets_.reload(asset);
    }
    for (const std::string& asset : maps_.keys_using(key)) {
        maps_.reload(asset);
    }
    for (const std::string& asset : effects_.keys_using(key)) {
        effects_.reload(asset);
    }
    // The prefab of the file first, then those made from it (its variants, the prefabs nesting it).
    std::vector<std::string> prefabs = prefabs_.keys_using(key);
    std::stable_partition(prefabs.begin(), prefabs.end(), [&key](const std::string& asset) { return asset == key; });
    for (const std::string& asset : prefabs) {
        prefabs_.reload(asset);
    }
}

namespace {

template <typename T>
AssetTypeStats stats_of(const AssetCache<T>& cache) {
    AssetTypeStats stats;
    stats.type = cache.type_name();
    stats.count = cache.size();
    stats.loads = cache.loads();
    stats.failures = cache.failures();
    for (const AssetInfo& info : cache.infos()) {
        stats.bytes += info.bytes;
    }
    return stats;
}

}  // namespace

std::vector<AssetTypeStats> Assets::stats() const {
    return {stats_of(textures_), stats_of(models_), stats_of(environments_),
            stats_of(fonts_),    stats_of(atlases_), stats_of(sounds_), stats_of(musics_),
            stats_of(skeletons_), stats_of(clips_), stats_of(animation_sets_), stats_of(maps_), stats_of(effects_),
            stats_of(prefabs_)};
}

std::vector<std::vector<AssetInfo>> Assets::infos() const {
    return {textures_.infos(), models_.infos(), environments_.infos(),
            fonts_.infos(),    atlases_.infos(), sounds_.infos(), musics_.infos(),
            skeletons_.infos(), clips_.infos(), animation_sets_.infos(), maps_.infos(), effects_.infos(),
            prefabs_.infos()};
}

bool Assets::reload(AssetType type, const std::string& key) {
    switch (type) {
        case AssetType::Texture: return textures_.reload(key);
        case AssetType::Model: return models_.reload(key);
        case AssetType::Environment: return environments_.reload(key);
        case AssetType::Font: return fonts_.reload(key);
        case AssetType::Atlas: return false;  // never reloaded: its sprites are referenced directly
        case AssetType::Sound: return sounds_.reload(key);
        case AssetType::Music: return musics_.reload(key);
        case AssetType::Skeleton: return skeletons_.reload(key);
        case AssetType::Clips: return clips_.reload(key);
        case AssetType::AnimationSet: return animation_sets_.reload(key);
        case AssetType::Map: return maps_.reload(key);
        case AssetType::Effect: return effects_.reload(key);
        case AssetType::Prefab: return prefabs_.reload(key);
        case AssetType::Count: break;
    }
    return false;
}

}  // namespace moteur
