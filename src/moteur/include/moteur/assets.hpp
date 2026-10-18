#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "moteur/animation_set.hpp"
#include "moteur/asset_cache.hpp"
#include "moteur/environment.hpp"
#include "moteur/font.hpp"
#include "moteur/map_data.hpp"
#include "moteur/model.hpp"
#include "moteur/particles.hpp"
#include "moteur/renderer.hpp"
#include "moteur/skeleton.hpp"
#include "moteur/asset_registry.hpp"
#include "moteur/model_import.hpp"
#include "moteur/prefab.hpp"
#include "moteur/reflection_io.hpp"
#include "moteur/sound.hpp"
#include "moteur/texture_atlas.hpp"

namespace moteur {

/// The kinds of asset the manager caches, in the order of Assets::stats() and Assets::infos().
enum class AssetType : std::size_t {
    Texture, Model, Environment, Font, Atlas, Sound, Music, Skeleton, Clips, AnimationSet, Map, Effect, Prefab,
    Count
};

/// One line of the asset statistics: a type of asset and what it holds.
struct AssetTypeStats {
    std::string type;          ///< the type's name, as in the messages ("texture", "model"...)
    std::size_t count = 0;     ///< assets in the cache
    std::size_t bytes = 0;     ///< their memory: on the GPU, or in main memory for sounds and musics
    std::size_t loads = 0;     ///< successful loads and reloads since the start
    std::size_t failures = 0;  ///< loads that failed (placeholders shown, or errors thrown)
};

/// The asset manager: every file the game loads is asked for here, by its path inside the assets
/// directory ("models/barrel/barrel.gltf"). It is loaded once and shared: asking again gives the
/// same asset, whoever asks (see AssetCache). An asset is freed by collect_garbage() once nobody
/// holds it any more, which the game calls when it changes scene: load the next scene first, then
/// release the previous one, then collect, and what both use is never reloaded.
///
/// A missing or broken texture, model or environment is replaced by a visible placeholder (magenta
/// checks, a magenta cube, a grey sky), a sound by a beep, a music by silence, a particle effect by
/// one without emitters, and the error is logged with the file name. A font, an atlas, a skeleton,
/// clips, an animation set or a map has no placeholder: its error is thrown (std::runtime_error,
/// or the loader's own exception). Every function taking a path throws std::invalid_argument for a
/// path normalize_asset_path() refuses; a path whose case differs from the disk's counts as a
/// broken file (see check_asset_case()).
///
/// Paths are relative to the assets directory, with either separator; the cache keys are their
/// normalized form (see normalize_asset_path()), plus the options that change the result (a
/// texture's settings, a font's size).
///
/// The textures of glTF models go through the texture cache: two models using the same image file
/// share one texture.
///
/// Hot reload (development): with enable_hot_reload(), the source assets directory is watched
/// (efsw); a changed file is copied into the assets directory, and the assets made from it are
/// loaded again in place during update(). Handles stay valid and see the new content; pointers
/// into an asset (a model's parts or materials) must not be kept from one frame to the next.
/// Atlases are not reloaded (their sprites are referenced directly).
///
/// Loading is synchronous: never inside a frame. To keep a loading screen moving, prefetch the
/// files first (prefetch_model()...): they are read and decoded on the job system's threads, and the
/// synchronous call that follows only creates the GPU objects (see the loading of the slice).
///
/// Main thread only, between frames for the loads (they create GPU objects). Application owns the
/// game's (Application::assets()), rooted at the assets/ folder next to the executable.
class Assets {
public:
    /// `root`: the assets directory, ending with a separator. Loads nothing yet. `renderer` must
    /// outlive the manager (it creates the GPU objects of the assets).
    Assets(Renderer& renderer, std::string root);
    /// Stops the hot reload's watcher first, then frees every cached asset (handles still held
    /// elsewhere keep theirs alive, and must go before the renderer).
    ~Assets();

    Assets(const Assets&) = delete;
    Assets& operator=(const Assets&) = delete;

    /// An image file (KTX2, which keeps its own compression, or PNG, JPEG... decoded), as a texture
    /// made with `settings` (color space, mipmaps, premultiplied alpha, normal map): the same file
    /// with other settings is another texture.
    Asset<Texture> texture(std::string_view path, const TextureSettings& settings = {});
    /// A glTF model (.gltf or .glb), with its meshes, materials and textures; its image files go
    /// through texture(), so models sharing an image share its texture. Imported (milestone 9,
    /// part 4: levels of detail, triangle and vertex orders, its embedded images in KTX2; settings
    /// from its `.meta`, see ModelImportSettings): its prepared form is kept in the import cache
    /// (`<cache><path>.mmdl`, see set_import_cache()) and used while its sources do not change; the
    /// shipped game has only that form, in its archives.
    Asset<Model> model(std::string_view path);
    Asset<Environment> environment(std::string_view path);  ///< an equirectangular .hdr
    /// A TrueType font rasterized at `pixel_height` pixels: each size is a font of its own.
    Asset<Font> font(std::string_view path, float pixel_height);
    Asset<TextureAtlas> atlas(std::string_view path);  ///< the .json of an atlas
    /// A short sound, decoded into memory (WAV, OGG, FLAC, MP3); a failed one beeps instead.
    Asset<Sound> sound(std::string_view path);
    /// A music or an ambience, kept compressed and decoded while it plays; a failed one is silent.
    Asset<Music> music(std::string_view path);
    /// The skeleton of a glTF model (its meshes and textures are not read). Throws if the file has
    /// none: there is no sensible placeholder.
    Asset<Skeleton> skeleton(std::string_view path);
    /// Every clip of a glTF file, built for its skeleton (a file of clips only is fine). They play
    /// on any skeleton of the same structure: check with ClipLibrary::mismatch(). Throws if the
    /// file cannot be read or has no skeleton.
    Asset<ClipLibrary> clips(std::string_view path);
    /// The description of a character's skeletal animations (a .json: ground speeds, blend spaces,
    /// masks, crossfades). Throws if it cannot be read or is not valid. Reloaded in place: an
    /// Animator takes the new values for the motions it starts afterwards.
    Asset<AnimationSet> animation_set(std::string_view path);
    /// A map (a .json whose cells are drawn in characters, see MapData). Throws if it cannot be read
    /// or is not valid. Reloaded in place, with MapData::revision() one higher: the scene compares it
    /// to rebuild what it made from the map.
    Asset<MapData> map(std::string_view path);
    /// A particle effect (a .json, see ParticleEffect). Throws if it cannot be read or is not valid.
    /// Reloaded in place: running effects take the new values.
    Asset<ParticleEffect> particle_effect(std::string_view path);
    /// A prefab (a `.prefab.json`, see Prefab), its bases and nested prefabs loaded through this
    /// manager, its asset fields (models...) loaded and held. Throws if it cannot be read or is not
    /// valid (with every problem), or for a cycle of prefabs. Reloaded with the files it is made from
    /// (its own, its bases', its nested prefabs'): what is already instantiated keeps its entities.
    Asset<Prefab> prefab(std::string_view path);

    /// Imports every model of `registry` whose prepared form in `cache` (see set_import_cache(); `root`
    /// is the program's assets folder, both ending with a separator) is missing or out of date, in parallel on
    /// the job system, without a GPU: what a project's opening does in an editor, so that the first
    /// run does not import while it plays (the checks run it before their captures). Returns one line
    /// per model imported or failed, and fills `failures`. Blocks until done. Development only (the
    /// shipped game has its prepared forms in its archives).
    static std::vector<std::string> import_all(const std::string& root, const std::string& cache, const AssetRegistry& registry,
                                               std::size_t& failures);

    /// Where imported models keep their prepared form (ending with a separator): by default the
    /// hidden folder `.imported/` of the assets, where the shipped game's archives have them. The
    /// Application moves it next to the sources in development (`.imported/` beside the folder of
    /// ApplicationConfig::assets_source_directory), shared by every build of the program.
    void set_import_cache(std::string folder) { import_cache_ = std::move(folder); }
    /// See set_import_cache().
    const std::string& import_cache() const { return import_cache_; }

    /// The import settings of the model `key` (normalized), from its `.meta` (defaults without one;
    /// a wrong setting is logged and keeps its default).
    ModelImportSettings model_import_settings(const std::string& key) const;

    /// The identities of the assets (milestone 9, part 4): built by the Application at start-up
    /// (scanned from the asset folders and their `.meta` in development, the frozen registry of the
    /// archives in the shipped game).
    ///@{
    AssetRegistry& registry() { return registry_; }
    const AssetRegistry& registry() const { return registry_; }
    ///@}
    /// The asset key of `id` through the registry; throws std::runtime_error for an unknown one.
    std::string key_of(const AssetId& id) const;
    /// The key a reference to an asset designates (see the reflection's asset values: `{"id": ...,
    /// "path": ...}`, or a bare path): the identifier's when the registry knows it, else the path
    /// written next to it (with a log: the asset was moved without its `.meta`, or is not
    /// identified yet). Throws std::runtime_error when neither gives one.
    std::string resolve_reference(const nlohmann::json& value) const;
    /// How a reference to the asset `key` is written: `{"id": "<uuid>", "path": "<key>"}` when the
    /// registry knows it, else the bare path.
    nlohmann::json reference_to(const std::string& key) const;

    /// For the reflection (reflection_io.hpp): loads into `handle` (an Asset<T> of `type`) the asset
    /// a reference designates (resolve_reference()); false with `problem` for a value that names no
    /// asset or a type the manager does not load (textures load as colour textures: sRGB, mipmaps).
    /// `null` or "" gives an empty handle.
    bool load_reflected(const TypeInfo& type, const nlohmann::json& value, void* handle, std::string& problem);
    /// The reference to the asset `handle` holds (an Asset<T> of `type`), as reference_to() writes
    /// it; null for an empty handle or an asset the manager did not load (one made by the game).
    nlohmann::json key_of_reflected(const TypeInfo& type, const void* handle) const;
    /// ReadOptions with the fields `flags` and the asset fields loaded by this manager.
    ReadOptions read_options(std::uint32_t flags);
    /// WriteOptions with the fields `flags` and the asset fields written as their paths.
    WriteOptions write_options(std::uint32_t flags) const;
    /// What Prefab::parse() needs to load through this manager.
    Prefab::Context prefab_context();

    /// Loading in the background (milestone 8, part 5): starts reading and decoding the file on the
    /// job system's threads (see jobs(); no GPU, no cache), and returns at once. The next call of
    /// the same kind with the same path (model(path) for prefetch_model(path), and so on; the same
    /// settings for a texture) on the main thread takes the result, waiting for it if it is not
    /// ready yet. Nothing happens if the asset is cached or already being prefetched. A model also
    /// decodes the image files its materials use, for the texture cache. Errors are those of the
    /// later call (a placeholder, or an exception), as without prefetching. A result nobody takes
    /// is dropped by collect_garbage() once decoded. Main thread.
    ///@{
    void prefetch_model(std::string_view path);
    void prefetch_texture(std::string_view path, const TextureSettings& settings = {});
    void prefetch_skeleton(std::string_view path);
    void prefetch_clips(std::string_view path);
    void prefetch_sound(std::string_view path);
    /// A prefab's models (and those of its bases and nested prefabs), found by reading its file
    /// now (small JSON) and prefetched as prefetch_model() does; the prefab itself is read by
    /// prefab() afterwards. A file that cannot be read is left to prefab() to report.
    void prefetch_prefab(std::string_view path);
    ///@}
    /// Prefetched files still being decoded: 0 once every later call is fast.
    std::size_t prefetching() const;
    /// Prefetched files not taken yet (decoded or not).
    std::size_t prefetched() const { return prepared_.size(); }

    /// Where an asset is: root() + its normalized path (in an archive or on disk; see read_file()).
    /// Checks nothing.
    std::string file_path(std::string_view path) const;
    /// Whether the asset's file is there, in the mounted archives or on disk (for optional test
    /// assets, which a scene does without rather than showing a placeholder), or, for a model, its
    /// prepared form (the shipped game). Does not load it.
    bool exists(std::string_view path) const;
    /// Why the model was replaced by the placeholder; empty if it loaded (or was never asked for).
    std::string model_error(std::string_view path) const;
    /// The assets directory given to the constructor, ending with a separator.
    const std::string& root() const { return root_; }

    /// Frees the assets nobody holds, models first (so that the textures only they held go too), and
    /// the prefetched results nobody took. Returns how many assets were freed. Between frames only:
    /// the GPU may still use what the last frame drew. Application's StateStack calls it after each
    /// transition.
    std::size_t collect_garbage();

    /// Watches `source_directory` (the assets/ folder of the source tree) and reloads what changes
    /// there (see update()); replaces an earlier watch. Returns false, with a log, if it cannot be
    /// watched. Always false in the shipping configuration (MOTEUR_SHIPPING), which has no hot
    /// reload. Application calls it for ApplicationConfig::assets_source_directory.
    bool enable_hot_reload(const std::string& source_directory);
    /// Also watches `folder`, an asset folder mounted in place (mount_asset_folder()): its changed
    /// files are reloaded without being copied. False, with a log, if it cannot be watched; always
    /// false in the shipping configuration.
    bool watch_mounted_folder(const std::string& folder);
    /// Whether enable_hot_reload() succeeded: changes of the sources are being watched.
    bool hot_reload() const { return watcher_ != nullptr; }
    /// Applies the changes seen since the last call (a file is taken once it has not changed for a
    /// moment, as editors often write in several steps). Called by Application between frames.
    void update();
    /// Called by update() with the key of every file that changed (after the assets made from it
    /// were reloaded): for what is not an asset of this manager (the data tables). Listeners are
    /// kept for the manager's life (there is no removal), called in the order they were added.
    void add_file_listener(std::function<void(const std::string& key)> listener) {
        file_listeners_.push_back(std::move(listener));
    }

    /// false: models use the plain images of their KHR_texture_basisu textures rather than the KTX2
    /// ones (to compare the two). For the models loaded afterwards.
    void set_prefer_ktx2(bool prefer) { prefer_ktx2_ = prefer; }

    /// The statistics of each type, indexed by AssetType.
    std::vector<AssetTypeStats> stats() const;
    /// Every cached asset, by type (indexed by AssetType). Copies and measures every asset: for the
    /// debug panel, not every frame.
    std::vector<std::vector<AssetInfo>> infos() const;
    /// Loads the asset `key` of that type again, in place (the debug panel's button). False, with a
    /// log, if it failed (the previous content stays) or is not cached. Between frames only, as any load.
    bool reload(AssetType type, const std::string& key);
    /// Whether the assets of that type can be reloaded (not atlases: their sprites are referenced directly).
    static bool reloadable(AssetType type) { return type != AssetType::Atlas; }

private:
    class Watcher;
    struct Prepared;  ///< a file read and decoded in the background (see prefetch_model)

    void prefetch(const std::string& prepared_key, std::function<void(Prepared&)> work);
    /// The prefetched result for that key, once done (waits for it), removed from the waiting list;
    /// null if nothing was prefetched. Rethrows the error of the decoding.
    std::shared_ptr<Prepared> take_prepared(const std::string& prepared_key);

    /// Normalizes `path` and checks its case against the disk (throws on a wrong case).
    std::string key_of(std::string_view path) const;
    void reload_file(const std::string& key);

    Renderer& renderer_;
    std::string root_;
    AssetCache<Texture> textures_;
    AssetCache<Model> models_;
    AssetCache<Environment> environments_;
    AssetCache<Font> fonts_;
    AssetCache<TextureAtlas> atlases_;
    AssetCache<Sound> sounds_;
    AssetCache<Music> musics_;
    AssetCache<Skeleton> skeletons_;
    AssetCache<ClipLibrary> clips_;
    AssetCache<AnimationSet> animation_sets_;
    AssetCache<MapData> maps_;
    AssetCache<ParticleEffect> effects_;
    AssetCache<Prefab> prefabs_;
    std::vector<std::string> prefabs_loading_;  ///< the prefabs being loaded now, to refuse a cycle
    std::unique_ptr<Watcher> watcher_;
    std::vector<std::unique_ptr<Watcher>> mounted_watchers_;  ///< mounted asset folders, reloaded in place
    AssetRegistry registry_;
    std::string import_cache_;  ///< see set_import_cache()
    std::unordered_map<std::string, std::shared_ptr<Prepared>> prepared_;  ///< main thread only
    std::vector<std::function<void(const std::string&)>> file_listeners_;
    bool prefer_ktx2_ = true;
};

}  // namespace moteur
