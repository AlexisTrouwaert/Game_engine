#include "moteur/assets.hpp"

#include <SDL3/SDL.h>

#include <efsw/efsw.hpp>

#include <filesystem>
#include <map>
#include <mutex>
#include <system_error>
#include <utility>

#include "moteur/ktx_texture.hpp"
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
            std::uint8_t* pixel = &image.pixels[static_cast<std::size_t>((y * kSize + x) * 4)];
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

}  // namespace

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
      animations_("animations"),
      sounds_("sound", [] { return placeholder_sound(); }, [](const Sound& sound) { return sound.bytes(); }),
      musics_("music", [] { return Music{}; }, [](const Music& music) { return music.memory(); }) {}

// The watcher goes first: its thread must not call into a half-destroyed manager.
Assets::~Assets() {
    watcher_.reset();
}

std::string Assets::file_path(std::string_view path) const {
    return root_ + normalize_asset_path(path);
}

bool Assets::exists(std::string_view path) const {
    std::error_code error;
    return std::filesystem::is_regular_file(utf8_path(file_path(path)), error);
}

std::string Assets::model_error(std::string_view path) const {
    return models_.error(normalize_asset_path(path));
}

std::string Assets::key_of(std::string_view path) const {
    std::string key = normalize_asset_path(path);
    check_asset_case(root_, key);
    return key;
}

Asset<Texture> Assets::texture(std::string_view path, const TextureSettings& settings) {
    const std::string key = normalize_asset_path(path);
    return textures_.get(texture_key(key, settings), [this, key, settings](std::vector<std::string>& files) {
        files.push_back(key);
        check_asset_case(root_, key);
        const FileData file = read_file(root_ + key);
        if (is_ktx2(file.data(), file.size())) {
            return renderer_.create_texture(
                decode_ktx2(file.data(), file.size(), key, settings, renderer_.compressed_formats()), key.c_str());
        }
        return renderer_.create_texture(decode_image(file.data(), file.size(), key), settings, key.c_str());
    });
}

Asset<Model> Assets::model(std::string_view path) {
    const std::string key = normalize_asset_path(path);
    return models_.get(key, [this, key](std::vector<std::string>& files) {
        files.push_back(key);
        check_asset_case(root_, key);
        const std::string directory = directory_of(key);
        GltfOptions options;
        options.decode_external_images = false;  // through the texture cache, below
        options.prefer_ktx2 = prefer_ktx2_;
        const ModelData data = load_gltf(root_ + key, options);
        for (const std::string& file : data.files) {
            files.push_back(normalize_asset_path(directory + file));
        }
        return Model::create(renderer_, data, key, [this, &directory](const ModelImage& image, const TextureSettings& settings) {
            return texture(directory + image.file, settings).handle();
        });
    });
}

Asset<Environment> Assets::environment(std::string_view path) {
    const std::string key = normalize_asset_path(path);
    return environments_.get(key, [this, key](std::vector<std::string>& files) {
        files.push_back(key);
        check_asset_case(root_, key);
        return Environment::create(renderer_, load_environment(root_ + key), key.c_str());
    });
}

Asset<Font> Assets::font(std::string_view path, float pixel_height) {
    const std::string key = normalize_asset_path(path);
    return fonts_.get(key + "#" + std::to_string(pixel_height), [this, key, pixel_height](std::vector<std::string>& files) {
        files.push_back(key);
        check_asset_case(root_, key);
        return Font::load(renderer_, root_ + key, pixel_height);
    });
}

Asset<TextureAtlas> Assets::atlas(std::string_view path) {
    const std::string key = key_of(path);
    return atlases_.get(key, [this, key](std::vector<std::string>&) {  // no files: never reloaded
        return TextureAtlas::load(renderer_, root_ + key);
    });
}

Asset<AnimationLibrary> Assets::animations(std::string_view path) {
    const std::string key = key_of(path);
    return animations_.get(key, [this, key](std::vector<std::string>&) {  // no files: never reloaded
        return AnimationLibrary::load(root_ + key);
    });
}

Asset<Sound> Assets::sound(std::string_view path) {
    const std::string key = normalize_asset_path(path);
    return sounds_.get(key, [this, key](std::vector<std::string>& files) {
        files.push_back(key);
        check_asset_case(root_, key);
        const FileData file = read_file(root_ + key);
        const auto* bytes = static_cast<const std::uint8_t*>(file.data());
        return decode_sound({bytes, file.size()}, key);
    });
}

Asset<Music> Assets::music(std::string_view path) {
    const std::string key = normalize_asset_path(path);
    return musics_.get(key, [this, key](std::vector<std::string>& files) {
        files.push_back(key);
        check_asset_case(root_, key);
        const FileData file = read_file(root_ + key);
        const auto* bytes = static_cast<const std::uint8_t*>(file.data());
        return open_music(std::vector<std::uint8_t>(bytes, bytes + file.size()), key);
    });
}

std::size_t Assets::collect_garbage() {
    // Models first: they hold textures, which become free once the models are gone.
    std::size_t freed = models_.collect_garbage();
    freed += textures_.collect_garbage();
    freed += environments_.collect_garbage();
    freed += fonts_.collect_garbage();
    freed += atlases_.collect_garbage();
    freed += animations_.collect_garbage();
    freed += sounds_.collect_garbage();
    freed += musics_.collect_garbage();
    if (freed > 0) {
        SDL_Log("Assets: %zu freed", freed);
    }
    return freed;
}

bool Assets::enable_hot_reload(const std::string& source_directory) {
    watcher_.reset();
    auto watcher = std::make_unique<Watcher>(utf8_path(source_directory).lexically_normal());
    if (!watcher->start()) {
        return false;
    }
    watcher_ = std::move(watcher);
    SDL_Log("Assets: hot reload from '%s'", source_directory.c_str());
    return true;
}

void Assets::update() {
    if (!watcher_) {
        return;
    }
    constexpr Uint64 kQuietMs = 200;
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
    }
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
            stats_of(fonts_),    stats_of(atlases_), stats_of(animations_), stats_of(sounds_), stats_of(musics_)};
}

std::vector<std::vector<AssetInfo>> Assets::infos() const {
    return {textures_.infos(), models_.infos(), environments_.infos(),
            fonts_.infos(),    atlases_.infos(), animations_.infos(), sounds_.infos(), musics_.infos()};
}

bool Assets::reload(std::size_t type, const std::string& key) {
    switch (type) {  // the order of stats()
        case 0: return textures_.reload(key);
        case 1: return models_.reload(key);
        case 2: return environments_.reload(key);
        case 3: return fonts_.reload(key);
        case 4:  // atlases and animations: never reloaded (their sprites are referenced directly)
        case 5: return false;
        case 6: return sounds_.reload(key);
        case 7: return musics_.reload(key);
        default: return false;
    }
}

}  // namespace moteur
