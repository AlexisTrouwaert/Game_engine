#pragma once

#include <SDL3/SDL.h>

#include <entt/core/hashed_string.hpp>
#include <entt/resource/cache.hpp>
#include <entt/resource/resource.hpp>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace moteur {

/// A shared handle to a loaded asset (EnTT's resource: a std::shared_ptr underneath). Cheap to copy,
/// fine to keep in a component; `*asset` and `asset->` reach the asset, `asset.handle()` gives the
/// std::shared_ptr. The asset stays in memory while a handle to it exists; a reload (hot reload)
/// changes it in place, so every handle sees the new content. Copying and releasing handles is
/// thread-safe; the asset itself is not (main thread, as its loads and reloads).
template <typename T>
using Asset = entt::resource<T>;

/// A handle to an object made by the game rather than loaded from a file (a generated mesh, a
/// texture drawn in code): it lives as long as its handles, outside any cache. An rvalue is moved
/// into the handle, an lvalue copied.
template <typename T>
Asset<std::remove_cvref_t<T>> make_asset(T&& value) {
    using Value = std::remove_cvref_t<T>;
    return Asset<Value>{std::make_shared<Value>(std::forward<T>(value))};
}

/// The canonical form of a path inside the assets directory: '/' separators, no "." components,
/// no leading or trailing '/'. The same file always gives the same key, whichever way it was
/// written ("models\\a.gltf", "./models/a.gltf", "models/x/../a.gltf"...). Case is kept: see
/// check_asset_case(). Touches no file. Throws std::invalid_argument for an empty path, an absolute
/// one (a leading separator, or a ':' anywhere, as in a drive letter), one going above the assets
/// directory with "..", or one naming no file (".").
std::string normalize_asset_path(std::string_view path);

/// Checks that `key` (a normalized path) is written with the exact case of the files on disk under
/// `root`. Windows and macOS accept a wrong case by default, but a case-sensitive file system (or an
/// archive) would not find the file: better to fail everywhere. Throws std::runtime_error naming the
/// component whose case differs. Components that do not exist are left to the loader to report.
/// Does nothing while archives are mounted (they compare paths exactly: a wrong case is simply not
/// found). Lists one directory per component of `key`: disk access, at load time only.
void check_asset_case(const std::string& root, const std::string& key);

/// What the debug panel shows about one cached asset.
struct AssetInfo {
    std::string key;        ///< its cache key: the normalized path, plus its options ("textures/a.png#srgb")
    std::size_t bytes = 0;  ///< on the GPU mostly; see AssetCache's `measure`
    long users = 0;         ///< handles held outside the cache
    bool failed = false;    ///< the placeholder stands in for it
    std::string error;      ///< why, when failed
    std::string reload_error;  ///< the last reload failed (the previous content stays): why
};

/// A cache of one type of asset: a key (a normalized path, plus the options that change the result,
/// such as "textures/a.png#srgb") always gives the same asset, loaded once. Storage and handles are
/// EnTT's resource_cache; this adds what a game needs around it:
/// - the load function is kept per asset, so that the asset can be reloaded (hot reload);
/// - a load that fails logs the error and gives the placeholder, when the type has one, so that a
///   missing file shows up on screen instead of stopping the game; without a placeholder, it throws;
/// - nothing is ever freed while in use: collect_garbage() drops the assets no handle refers to,
///   and is meant to be called between two scenes, never in the middle of a frame.
///
/// No GPU here: the load functions do the work, which keeps the cache testable on its own.
/// Assets owns one per type of asset; a game rarely needs its own. Not thread-safe: one thread (the
/// main one) uses a cache.
template <typename T>
class AssetCache {
public:
    /// Loads the asset; appends to `files` the files it is made from, as asset keys (the main file
    /// first, before anything can fail), which hot reload watches (see keys_using()). Throws to
    /// report a failure.
    using Load = std::function<T(std::vector<std::string>& files)>;
    /// Makes the stand-in of an asset that failed to load (a magenta texture...); it must not throw.
    using Placeholder = std::function<T()>;
    /// The memory an asset takes, in bytes (shown by the debug panel, see AssetInfo::bytes).
    using Measure = std::function<std::size_t(const T&)>;
    /// Puts the reloaded `fresh` into `current`, the object every handle points to. Throws to
    /// refuse (current is then left as it was). By default, a move assignment.
    using Replace = std::function<void(T& current, T&& fresh)>;

    /// `type_name` appears in messages ("texture", "model"). `placeholder`, when given, is called
    /// for each failed asset (each gets its own copy, which a later reload can replace); without
    /// one, get() rethrows the load's error. `measure` and `replace` are optional (0 bytes; a move
    /// assignment).
    explicit AssetCache(std::string type_name, Placeholder placeholder = {}, Measure measure = {}, Replace replace = {})
        : type_name_(std::move(type_name)),
          placeholder_(std::move(placeholder)),
          measure_(std::move(measure)),
          replace_(std::move(replace)) {}

    /// The asset under `key`, loaded by `load` the first time only (later calls ignore `load`). A
    /// load that throws gives the placeholder, with the error logged and kept (see error()), or,
    /// without a placeholder, rethrows and caches nothing. Throws std::logic_error if two different
    /// keys have the same 32-bit hash (rename one of the files).
    Asset<T> get(const std::string& key, Load load) {
        const entt::id_type id = entt::hashed_string::value(key.c_str(), key.size());
        if (const auto found = entries_.find(id); found != entries_.end()) {
            if (found->second.key != key) {
                // FNV-1a on 32 bits: unlikely with a few thousand assets, but it must not go unnoticed.
                throw std::logic_error("Asset keys '" + found->second.key + "' and '" + key + "' have the same hash");
            }
            return cache_[id];
        }
        Entry entry;
        entry.key = key;
        entry.load = std::move(load);
        T value = make(entry);
        cache_.load(id, std::move(value));
        if (!entry.failed) {
            ++loads_;
        }
        entries_.emplace(id, std::move(entry));
        return cache_[id];
    }

    /// Loads the asset under `key` again and replaces its content in place (every handle sees it).
    /// On failure the previous content stays, and the error is logged and kept in
    /// AssetInfo::reload_error. False if nothing was reloaded (failure, or `key` not cached). A
    /// placeholder that reloads successfully becomes the real asset.
    bool reload(const std::string& key) {
        const entt::id_type id = entt::hashed_string::value(key.c_str(), key.size());
        const auto found = entries_.find(id);
        if (found == entries_.end()) {
            return false;
        }
        Entry& entry = found->second;
        try {
            std::vector<std::string> files;
            T value = entry.load(files);
            if (replace_) {  // even for a placeholder: its users may point into it too
                replace_(*cache_[id], std::move(value));
            } else {
                *cache_[id] = std::move(value);
            }
            entry.files = std::move(files);
            entry.failed = false;
            entry.error.clear();
            entry.reload_error.clear();
            ++loads_;
            ++generation_;
            SDL_Log("Assets: %s '%s' reloaded", type_name_.c_str(), key.c_str());
            return true;
        } catch (const std::exception& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Assets: %s '%s' not reloaded: %s", type_name_.c_str(),
                         key.c_str(), e.what());
            entry.reload_error = e.what();
            return false;
        }
    }

    /// Frees the assets no handle refers to any more (between two scenes, never in the middle of a
    /// frame: the GPU may still use them). Returns how many were freed. O(n).
    std::size_t collect_garbage() {
        std::size_t freed = 0;
        for (auto it = entries_.begin(); it != entries_.end();) {
            if (users(it->first) == 0) {
                cache_.erase(it->first);
                it = entries_.erase(it);
                ++freed;
            } else {
                ++it;
            }
        }
        return freed;
    }

    /// Keys of the cached assets made from `file` (an asset key, as a Load function appends it to
    /// its `files`). O(n) over the cache.
    std::vector<std::string> keys_using(const std::string& file) const {
        std::vector<std::string> keys;
        for (const auto& [id, entry] : entries_) {
            for (const std::string& used : entry.files) {
                if (used == file) {
                    keys.push_back(entry.key);
                    break;
                }
            }
        }
        return keys;
    }

    /// What the debug panel shows of every cached asset, in no particular order. O(n), measures
    /// each asset.
    std::vector<AssetInfo> infos() const {
        std::vector<AssetInfo> result;
        for (const auto& [id, entry] : entries_) {
            AssetInfo info;
            info.key = entry.key;
            info.bytes = measure_ ? measure_(*cache_[id]) : 0;
            info.users = users(id);
            info.failed = entry.failed;
            info.error = entry.error;
            info.reload_error = entry.reload_error;
            result.push_back(std::move(info));
        }
        return result;
    }

    /// Why the asset under `key` is a placeholder; empty if it loaded, or is not in the cache.
    std::string error(const std::string& key) const {
        const auto found = entries_.find(entt::hashed_string::value(key.c_str(), key.size()));
        return found != entries_.end() ? found->second.error : std::string();
    }

    /// The key of the cached asset `asset` points to (an object of this cache), or empty. O(n).
    std::string key_of(const T* asset) const {
        for (const auto& [id, entry] : entries_) {
            if (cache_[id].handle().get() == asset) {
                return entry.key;
            }
        }
        return {};
    }

    /// Whether an asset is cached under `key` (loaded, or standing as a placeholder).
    bool contains(const std::string& key) const {
        return entries_.contains(entt::hashed_string::value(key.c_str(), key.size()));
    }
    /// The number of assets cached, placeholders included.
    std::size_t size() const { return entries_.size(); }
    /// Successful loads and reloads so far (placeholders excluded): the same asset asked twice counts once.
    std::size_t loads() const { return loads_; }
    /// Loads that failed so far (first loads only, with or without a placeholder; failed reloads
    /// are not counted).
    std::size_t failures() const { return failures_; }
    /// The name given to the constructor ("texture"), as in the messages.
    const std::string& type_name() const { return type_name_; }
    /// Successful reloads of assets of type T so far, in every cache of T: what keeps something
    /// derived from their content (the World's culling boxes of models) remembers the value it was
    /// computed at, and computes it again once the value has changed. A reload changes an asset
    /// in place: its handles, and the components holding them, give no other sign of it. Main
    /// thread, as the reloads.
    static std::uint64_t reload_generation() { return generation_; }

private:
    struct Entry {
        std::string key;
        Load load;
        std::vector<std::string> files;
        bool failed = false;
        std::string error;
        std::string reload_error;
    };

    /// Adapts EnTT's cache to values built outside of it (by the load functions).
    struct Loader {
        using result_type = std::shared_ptr<T>;
        result_type operator()(T&& value) const { return std::make_shared<T>(std::move(value)); }
    };

    /// Handles held outside the cache.
    long users(entt::id_type id) const {
        const std::shared_ptr<const T> handle = cache_[id].handle();
        return handle.use_count() - 2;  // neither the cache's own pointer nor this one
    }

    T make(Entry& entry) {
        try {
            return entry.load(entry.files);
        } catch (const std::exception& e) {
            ++failures_;
            if (!placeholder_) {
                throw;
            }
            entry.failed = true;
            entry.error = e.what();
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Assets: %s '%s' replaced by a placeholder: %s",
                         type_name_.c_str(), entry.key.c_str(), e.what());
            return placeholder_();
        }
    }

    std::string type_name_;
    Placeholder placeholder_;
    Measure measure_;
    Replace replace_;
    entt::resource_cache<T, Loader> cache_;
    std::unordered_map<entt::id_type, Entry> entries_;
    std::size_t loads_ = 0;
    std::size_t failures_ = 0;
    inline static std::uint64_t generation_ = 0;  ///< see reload_generation()
};

}  // namespace moteur
