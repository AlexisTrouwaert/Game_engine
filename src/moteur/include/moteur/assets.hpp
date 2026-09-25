#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "moteur/animation.hpp"
#include "moteur/asset_cache.hpp"
#include "moteur/environment.hpp"
#include "moteur/font.hpp"
#include "moteur/model.hpp"
#include "moteur/renderer.hpp"
#include "moteur/sound.hpp"
#include "moteur/texture_atlas.hpp"

namespace moteur {

// One line of the asset statistics: a type of asset and what it holds.
struct AssetTypeStats {
    std::string type;
    std::size_t count = 0;     // assets in the cache
    std::size_t bytes = 0;     // their memory: on the GPU, or in main memory for sounds and musics
    std::size_t loads = 0;     // successful loads and reloads since the start
    std::size_t failures = 0;  // loads that failed (placeholders shown, or errors thrown)
};

// The asset manager: every file the game loads is asked for here, by its path inside the assets
// directory ("models/barrel/barrel.gltf"). It is loaded once and shared: asking again gives the
// same asset, whoever asks (see AssetCache). An asset is freed by collect_garbage() once nobody
// holds it any more, which the game calls when it changes scene: load the next scene first, then
// release the previous one, then collect, and what both use is never reloaded.
//
// A missing or broken texture, model or environment is replaced by a visible placeholder (magenta
// checks, a magenta cube, a grey sky) and the error is logged with the file name. A font, an atlas
// or an animation file has no placeholder: its error is thrown.
//
// The textures of glTF models go through the texture cache: two models using the same image file
// share one texture.
//
// Hot reload (development): with enable_hot_reload(), the source assets directory is watched
// (efsw); a changed file is copied into the assets directory, and the assets made from it are
// loaded again in place during update(). Handles stay valid and see the new content; pointers
// into an asset (a model's parts or materials) must not be kept from one frame to the next.
// Atlases and animation files are not reloaded (their sprites are referenced directly).
//
// Loading is synchronous and waits for the GPU: never inside a frame.
class Assets {
public:
    // `root`: the assets directory, ending with a separator.
    Assets(Renderer& renderer, std::string root);
    ~Assets();

    Assets(const Assets&) = delete;
    Assets& operator=(const Assets&) = delete;

    Asset<Texture> texture(std::string_view path, const TextureSettings& settings = {});
    Asset<Model> model(std::string_view path);
    Asset<Environment> environment(std::string_view path);  // an equirectangular .hdr
    Asset<Font> font(std::string_view path, float pixel_height);
    Asset<TextureAtlas> atlas(std::string_view path);  // the .json of an atlas
    Asset<AnimationLibrary> animations(std::string_view path);
    // A short sound, decoded into memory (WAV, OGG, FLAC, MP3); a failed one beeps instead.
    Asset<Sound> sound(std::string_view path);
    // A music or an ambience, kept compressed and decoded while it plays; a failed one is silent.
    Asset<Music> music(std::string_view path);

    // Where an asset is on disk, and whether it is there (for optional test assets, which a scene
    // does without rather than showing a placeholder).
    std::string file_path(std::string_view path) const;
    bool exists(std::string_view path) const;
    // Why the model was replaced by the placeholder; empty if it loaded (or was never asked for).
    std::string model_error(std::string_view path) const;
    const std::string& root() const { return root_; }

    // Frees the assets nobody holds. Returns how many were freed. Between frames only.
    std::size_t collect_garbage();

    // Watches `source_directory` (the assets/ folder of the source tree) and reloads what changes
    // there. Returns false, with a log, if it cannot be watched.
    bool enable_hot_reload(const std::string& source_directory);
    bool hot_reload() const { return watcher_ != nullptr; }
    // Applies the changes seen since the last call (a file is taken once it has not changed for a
    // moment, as editors often write in several steps). Called by Application between frames.
    void update();

    // false: models use the plain images of their KHR_texture_basisu textures rather than the KTX2
    // ones (to compare the two). For the models loaded afterwards.
    void set_prefer_ktx2(bool prefer) { prefer_ktx2_ = prefer; }

    std::vector<AssetTypeStats> stats() const;
    // Every cached asset, by type (same order as stats()).
    std::vector<std::vector<AssetInfo>> infos() const;
    // Loads the asset `key` of the type at `type` (its index in stats()) again, in place (the debug
    // panel's button). False, with a log, if it failed (the previous content stays) or is not cached.
    // Between frames only, as any load.
    bool reload(std::size_t type, const std::string& key);
    // Whether the assets of that type can be reloaded (not atlases nor animations).
    static bool reloadable(std::size_t type) { return type != 4 && type != 5; }

private:
    class Watcher;

    // Normalizes `path` and checks its case against the disk (throws on a wrong case).
    std::string key_of(std::string_view path) const;
    void reload_file(const std::string& key);

    Renderer& renderer_;
    std::string root_;
    AssetCache<Texture> textures_;
    AssetCache<Model> models_;
    AssetCache<Environment> environments_;
    AssetCache<Font> fonts_;
    AssetCache<TextureAtlas> atlases_;
    AssetCache<AnimationLibrary> animations_;
    AssetCache<Sound> sounds_;
    AssetCache<Music> musics_;
    std::unique_ptr<Watcher> watcher_;
    bool prefer_ktx2_ = true;
};

}  // namespace moteur
