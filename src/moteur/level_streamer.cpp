#include "moteur/level_streamer.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <set>
#include <stdexcept>

#include "moteur/assert.hpp"
#include "moteur/assets.hpp"
#include "moteur/file_io.hpp"
#include "moteur/model.hpp"
#include "moteur/paths.hpp"
#include "moteur/prefab.hpp"
#include "moteur/profiler.hpp"
#include "moteur/world.hpp"

namespace moteur {

namespace {

double ms_since(Uint64 start) {
    return static_cast<double>(SDL_GetPerformanceCounter() - start) * 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency());
}

}  // namespace

// One wanted or loaded chunk and where its loading stands.
struct LevelStreamer::Chunk {
    enum class Stage { Reading, Prefetching, Instantiating, Loaded };
    glm::ivec2 coord{0};
    Stage stage = Stage::Reading;
    JobHandle read;                         ///< the background read and decode (Reading)
    std::shared_ptr<LevelChunk> data;       ///< what it holds, once read
    std::string error;                      ///< a read that failed (logged once, the chunk stays empty)
    std::size_t next_instance = 0;          ///< Instantiating: the next of data->instances to make
    int prefetch_updates = 0;               ///< updates spent waiting for the prefetched prefabs
    std::vector<entt::entity> roots;        ///< what it made (destroyed when it unloads)
};

// A collision scene rebuilt in the background from the loaded chunks' meshes.
struct LevelStreamer::CollisionBuild {
    std::vector<std::shared_ptr<LevelChunk>> chunks;
    std::unique_ptr<CollisionScene> scene = std::make_unique<CollisionScene>();
    JobHandle job;
};

LevelSources asset_level_sources(Assets& assets) {
    // The models whose collision was given: kept loaded while the sources live.
    auto models = std::make_shared<std::map<std::string, Asset<Model>>>();
    LevelSources sources;
    sources.prefab = [&assets](const std::string& key) -> std::shared_ptr<const Prefab> {
        return assets.prefab(key).handle();
    };
    sources.model_collision = [&assets, models](const std::string& key) -> const CollisionMeshData* {
        auto [found, fresh] = models->try_emplace(key);
        if (fresh) {
            found->second = assets.model(key);
        }
        return found->second ? &found->second->collision : nullptr;
    };
    sources.resolve = [&assets](const nlohmann::json& reference) { return assets.resolve_reference(reference); };
    return sources;
}

LevelDocument read_level(Assets& assets, const std::string& level_key, DataIssues& issues) {
    const FileData bytes = read_file(assets.file_path(level_key));
    return LevelDocument::parse(std::string_view(static_cast<const char*>(bytes.data()), bytes.size()), level_key,
                                [&assets](const nlohmann::json& reference) { return assets.resolve_reference(reference); },
                                issues);
}

namespace {

// Where the file of an asset is on the disk: its folder of the registry (the sources, a mounted
// folder), else the assets'.
std::string source_file(const Assets& assets, const std::string& key) {
    if (const AssetRecord* record = assets.registry().find(key); record != nullptr && !record->root.empty()) {
        return record->root + key;
    }
    return disk_path(assets.file_path(key));
}

// The files as they are now: their size and time (-1 and 0 for a missing one), and the format.
nlohmann::json files_signature(const std::vector<std::string>& paths) {
    nlohmann::json files = nlohmann::json::array();
    for (const std::string& path : paths) {
        std::error_code error;
        const auto size = std::filesystem::file_size(std::filesystem::path(std::u8string(path.begin(), path.end())), error);
        files.push_back({path, error ? -1 : static_cast<long long>(size), file_time(path)});
    }
    return {{"format", kPreparedLevelVersion}, {"files", files}};
}

}  // namespace

void LevelStreamer::open_any(const std::string& level_key) {
    if (file_exists(assets_.file_path(prepared_level_key(level_key)))) {
        open(level_key);
        return;
    }
    // Development: the import cache's prepared form, if what the level is made from did not change.
    const std::string cache = assets_.import_cache();
    const std::string signature_path = cache + prepared_level_key(level_key) + ".files";
    if (!cache.empty() && file_exists(signature_path)) {
        try {
            const nlohmann::json saved = nlohmann::json::parse(read_text_file(signature_path));
            const std::vector<std::string> paths = saved.at("paths").get<std::vector<std::string>>();
            if (saved.at("signature") == files_signature(paths)) {
                open_from(level_key, cache);
                SDL_Log("Level '%s': prepared form of the import cache", level_key.c_str());
                return;
            }
        } catch (const std::exception& e) {
            close();
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Level '%s': import cache not used: %s", level_key.c_str(), e.what());
        }
    }

    DataIssues issues;
    const LevelDocument level = read_level(assets_, level_key, issues);
    for (const DataIssue& issue : issues.all()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Level: %s: %s", issue.where.c_str(), issue.message.c_str());
    }
    // The bake, its sources noted: what the prepared form depends on.
    LevelSources sources = asset_level_sources(assets_);
    auto prefabs = std::make_shared<std::set<std::string>>();
    auto models = std::make_shared<std::set<std::string>>();
    sources.prefab = [inner = sources.prefab, prefabs](const std::string& key) {
        prefabs->insert(key);
        return inner(key);
    };
    sources.model_collision = [inner = sources.model_collision, models](const std::string& key) {
        models->insert(key);
        return inner(key);
    };
    LevelBakeStats stats;
    BakedLevel baked = bake_level(level, sources, &stats);
    SDL_Log("Level '%s' baked in %.0f ms (no prepared form): %d instances, %d chunks, %d navigation tiles", level_key.c_str(),
            stats.milliseconds, stats.instances, stats.chunks, stats.nav_tiles);
    if (!cache.empty()) {
        std::vector<std::string> paths = {source_file(assets_, level_key)};
        for (const std::string& key : *prefabs) {
            paths.push_back(source_file(assets_, key));
            for (const std::string& base : assets_.prefab(key)->dependencies()) {
                paths.push_back(source_file(assets_, base));
            }
        }
        for (const std::string& key : *models) {
            paths.push_back(source_file(assets_, key));
            paths.push_back(source_file(assets_, key) + ".meta");  // its import settings
        }
        std::sort(paths.begin(), paths.end());
        paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
        std::string error;
        const auto write = [&error](const std::string& path, const std::vector<std::uint8_t>& bytes) {
            std::error_code ignored;
            std::filesystem::create_directories(std::filesystem::path(std::u8string(path.begin(), path.end())).parent_path(), ignored);
            return write_file_atomic(path, bytes.data(), bytes.size(), error);
        };
        bool written = write(cache + prepared_level_key(level_key), encode_level_header(baked.header));
        for (const LevelChunk& chunk : baked.chunks) {
            written = written && write(cache + prepared_chunk_key(level_key, chunk.coord), encode_level_chunk(chunk));
        }
        const nlohmann::json signature = {{"paths", paths}, {"signature", files_signature(paths)}};
        written = written && write_file_atomic(signature_path, signature.dump(), error);
        if (!written) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Level '%s': not kept in the import cache: %s", level_key.c_str(), error.c_str());
        }
    }
    open(std::move(baked), level_key);
}

LevelStreamer::LevelStreamer(Assets& assets, World& world, const StreamingSettings& settings)
    : assets_(assets), world_(world), settings_(settings), collision_(std::make_unique<CollisionScene>()) {}

LevelStreamer::~LevelStreamer() {
    close();
}

void LevelStreamer::open(const std::string& level_key) {
    open_from(level_key, std::string());
}

void LevelStreamer::open_from(const std::string& level_key, const std::string& root) {
    close();
    prepared_root_ = root;
    const std::string header_key = prepared_level_key(level_key);
    const FileData bytes = read_file(root.empty() ? assets_.file_path(header_key) : root + header_key);
    const std::vector<std::uint8_t> data(static_cast<const std::uint8_t*>(bytes.data()),
                                         static_cast<const std::uint8_t*>(bytes.data()) + bytes.size());
    header_ = decode_level_header(data, header_key);
    key_ = level_key;
    for (const NavTiling& tiling : header_.navigation) {
        auto mesh = std::make_unique<NavMesh>();
        mesh->start_tiled(tiling);
        mesh_pointers_.push_back(mesh.get());
        meshes_.push_back(std::move(mesh));
    }
    open_ = true;
    stats_ = {};
}

void LevelStreamer::open(BakedLevel baked, const std::string& level_key) {
    close();
    prepared_root_.clear();
    header_ = std::move(baked.header);
    key_ = level_key;
    for (LevelChunk& chunk : baked.chunks) {
        const std::pair<int, int> key{chunk.coord.x, chunk.coord.y};  // read before the chunk moves
        in_memory_[key] = std::make_shared<LevelChunk>(std::move(chunk));
    }
    for (const NavTiling& tiling : header_.navigation) {
        auto mesh = std::make_unique<NavMesh>();
        mesh->start_tiled(tiling);
        mesh_pointers_.push_back(mesh.get());
        meshes_.push_back(std::move(mesh));
    }
    open_ = true;
    stats_ = {};
}

void LevelStreamer::close() {
    MOTEUR_ASSERT_MAIN_THREAD("LevelStreamer");
    for (auto& [coord, chunk] : chunks_) {
        if (chunk->stage == Chunk::Stage::Reading) {
            jobs().wait(chunk->read);  // its job writes into the chunk
        }
        for (const entt::entity root : chunk->roots) {
            world_.destroy(root);
        }
    }
    if (collision_build_) {
        jobs().wait(collision_build_->job);
        collision_build_.reset();
    }
    chunks_.clear();
    in_memory_.clear();
    instances_.clear();
    meshes_.clear();
    mesh_pointers_.clear();
    collision_ = std::make_unique<CollisionScene>();
    header_ = LevelHeader{};
    open_ = false;
}

void LevelStreamer::want(glm::ivec2 coord) {
    auto chunk = std::make_unique<Chunk>();
    chunk->coord = coord;
    if (const auto found = in_memory_.find({coord.x, coord.y}); found != in_memory_.end()) {
        chunk->data = found->second;
        chunk->stage = Chunk::Stage::Prefetching;
    } else {
        // Read and decode in the background; the chunk's slot outlives the job (close() waits).
        Chunk* target = chunk.get();
        const std::string path = prepared_root_.empty() ? assets_.file_path(prepared_chunk_key(key_, coord))
                                                        : prepared_root_ + prepared_chunk_key(key_, coord);
        const std::string name = prepared_chunk_key(key_, coord);
        chunk->read = jobs().submit([target, path, name] {
            try {
                const FileData bytes = read_file(path);
                const std::vector<std::uint8_t> data(static_cast<const std::uint8_t*>(bytes.data()),
                                                     static_cast<const std::uint8_t*>(bytes.data()) + bytes.size());
                target->data = std::make_shared<LevelChunk>(decode_level_chunk(data, name));
            } catch (const std::exception& e) {
                target->error = e.what();
            }
        });
    }
    chunks_[{coord.x, coord.y}] = std::move(chunk);
}

void LevelStreamer::unload(glm::ivec2 coord) {
    const auto found = chunks_.find({coord.x, coord.y});
    if (found == chunks_.end()) {
        return;
    }
    Chunk& chunk = *found->second;
    if (chunk.stage == Chunk::Stage::Reading) {
        jobs().wait(chunk.read);
    }
    for (const entt::entity root : chunk.roots) {
        if (world_.registry().valid(root)) {
            if (const LevelInstanceId* id = world_.registry().try_get<LevelInstanceId>(root)) {
                instances_.erase(id->id);
            }
            world_.destroy(root);
        }
    }
    if (chunk.stage == Chunk::Stage::Loaded) {
        for (const std::unique_ptr<NavMesh>& mesh : meshes_) {
            mesh->remove_tile(coord.x, coord.y);
        }
        collision_dirty_ = true;
    }
    chunks_.erase(found);
}

void LevelStreamer::rebuild_collision() {
    // One rebuild at a time; the next one starts when it is done (collision_dirty_ stays set).
    if (collision_build_) {
        return;
    }
    auto build = std::make_shared<CollisionBuild>();
    for (const auto& [coord, chunk] : chunks_) {
        if (chunk->stage == Chunk::Stage::Loaded && chunk->data) {
            build->chunks.push_back(chunk->data);
        }
    }
    CollisionBuild* target = build.get();
    build->job = jobs().submit([target] {
        for (const std::shared_ptr<LevelChunk>& chunk : target->chunks) {
            target->scene->add(chunk->collision, glm::mat4(1.0f));
        }
        target->scene->build();
    });
    collision_build_ = std::move(build);
    collision_dirty_ = false;
}

void LevelStreamer::update() {
    MOTEUR_ASSERT_MAIN_THREAD("LevelStreamer");
    MOTEUR_PROFILE("level: streaming");
    if (!open_) {
        return;
    }
    const Uint64 start = SDL_GetPerformanceCounter();

    // Which chunks are wanted: around the focus (or all), nearest first.
    std::vector<std::pair<float, glm::ivec2>> wanted;
    for (int z = 0; z < header_.chunks_z; ++z) {
        for (int x = 0; x < header_.chunks_x; ++x) {
            if (!header_.has_chunk({x, z})) {
                continue;
            }
            const glm::vec2 centre(header_.origin.x + (static_cast<float>(x) + 0.5f) * header_.chunk_size,
                                   header_.origin.z + (static_cast<float>(z) + 0.5f) * header_.chunk_size);
            const glm::vec2 d = centre - glm::vec2(focus_.x, focus_.z);
            const float distance = std::sqrt(glm::dot(d, d)) - header_.chunk_size * 0.7071f;  // to the chunk, about
            if (settings_.load_all || distance <= settings_.load_radius) {
                wanted.emplace_back(distance, glm::ivec2(x, z));
            }
        }
    }
    std::sort(wanted.begin(), wanted.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first < b.first : (a.second.y != b.second.y ? a.second.y < b.second.y : a.second.x < b.second.x);
    });
    if (static_cast<int>(wanted.size()) > settings_.max_chunks) {
        wanted.resize(static_cast<std::size_t>(settings_.max_chunks));
    }
    for (const auto& [distance, coord] : wanted) {
        if (!chunks_.contains({coord.x, coord.y})) {
            want(coord);
        }
    }

    // Far chunks go (beyond the unload radius, or past the budget).
    if (!settings_.load_all) {
        std::vector<glm::ivec2> far;
        for (const auto& [key, chunk] : chunks_) {
            const glm::vec2 centre(header_.origin.x + (static_cast<float>(key.first) + 0.5f) * header_.chunk_size,
                                   header_.origin.z + (static_cast<float>(key.second) + 0.5f) * header_.chunk_size);
            const glm::vec2 d = centre - glm::vec2(focus_.x, focus_.z);
            const float distance = std::sqrt(glm::dot(d, d)) - header_.chunk_size * 0.7071f;
            const bool in_budget = std::any_of(wanted.begin(), wanted.end(), [&](const auto& w) {
                return w.second == glm::ivec2(key.first, key.second);
            });
            if (distance > settings_.unload_radius || (!in_budget && static_cast<int>(chunks_.size()) > settings_.max_chunks)) {
                far.emplace_back(key.first, key.second);
            }
        }
        for (const glm::ivec2 coord : far) {
            unload(coord);
        }
    }

    // The background: reads done, then the prefabs, then the entities within the budget.
    double budget = settings_.instantiate_budget_ms;
    for (auto& [key, chunk_pointer] : chunks_) {
        Chunk& chunk = *chunk_pointer;
        if (chunk.stage == Chunk::Stage::Reading) {
            if (!chunk.read.done()) {
                if (settings_.ordered) {
                    break;  // the ones after it wait (Settings::ordered)
                }
                continue;
            }
            if (!chunk.data) {
                if (!chunk.error.empty()) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Level: %s", chunk.error.c_str());
                }
                chunk.data = std::make_shared<LevelChunk>();
                chunk.data->coord = chunk.coord;
            }
            chunk.stage = Chunk::Stage::Prefetching;
        }
        if (chunk.stage == Chunk::Stage::Prefetching) {
            if (chunk.prefetch_updates == 0) {
                std::set<std::string> prefabs;
                for (const ChunkInstance& instance : chunk.data->instances) {
                    prefabs.insert(instance.prefab);
                }
                for (const std::string& prefab : prefabs) {
                    assets_.prefetch_prefab(prefab);
                }
            }
            // Wait for the prefetches (a few updates at most: then prefab() finishes them).
            if (assets_.prefetching() > 0 && ++chunk.prefetch_updates < 30) {
                if (settings_.ordered) {
                    break;
                }
                continue;
            }
            chunk.stage = Chunk::Stage::Instantiating;
        }
        if (chunk.stage == Chunk::Stage::Instantiating) {
            const Uint64 began = SDL_GetPerformanceCounter();
            while (chunk.next_instance < chunk.data->instances.size()) {
                const ChunkInstance& instance = chunk.data->instances[chunk.next_instance++];
                if (instance.id != 0 && state_.removed(instance.id)) {
                    continue;
                }
                try {
                    const Asset<Prefab> prefab = assets_.prefab(instance.prefab);
                    PrefabPlacement placement;
                    placement.position = instance.transform.position;
                    placement.rotation = instance.transform.rotation;
                    placement.scale = instance.transform.scale;
                    placement.overrides = instance.overrides;
                    if (instance.id != 0) {
                        merge_fields(placement.overrides, state_.overrides(instance.id));
                    }
                    DataIssues issues;
                    const Prefab::Context context = assets_.prefab_context();
                    const PrefabInstance made = instantiate(world_, *prefab, placement, &issues, &context);
                    if (instance.id != 0) {
                        world_.registry().emplace_or_replace<LevelInstanceId>(made.root, instance.id);
                        instances_[instance.id] = made.root;
                    }
                    chunk.roots.push_back(made.root);
                    if (instance_hook_) {
                        instance_hook_(made.root, instance);
                    }
                } catch (const std::exception& e) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Level '%s', instance %u: %s", key_.c_str(), instance.id, e.what());
                }
                if (ms_since(began) >= budget) {
                    break;
                }
            }
            budget -= ms_since(began);
            if (chunk.next_instance < chunk.data->instances.size()) {
                if (budget <= 0.0 || settings_.ordered) {
                    break;  // the rest at the next update
                }
                continue;
            }
            // Made: its navigation tiles join, the collision scene will have it.
            for (std::size_t agent = 0; agent < meshes_.size() && agent < chunk.data->nav_tiles.size(); ++agent) {
                if (!chunk.data->nav_tiles[agent].empty()) {
                    try {
                        meshes_[agent]->add_tile(chunk.data->nav_tiles[agent], prepared_chunk_key(key_, chunk.coord));
                    } catch (const std::exception& e) {
                        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Level: %s", e.what());
                    }
                }
            }
            chunk.stage = Chunk::Stage::Loaded;
            collision_dirty_ = true;
        }
    }

    // The collision scene: a rebuild finished is swapped in; another starts if the chunks changed.
    if (collision_build_ && collision_build_->job.done()) {
        collision_ = std::move(collision_build_->scene);
        collision_build_.reset();
    }
    if (collision_dirty_) {
        rebuild_collision();
    }

    stats_.chunks_loaded = 0;
    stats_.chunks_pending = 0;
    for (const auto& [key, chunk] : chunks_) {
        (chunk->stage == Chunk::Stage::Loaded ? stats_.chunks_loaded : stats_.chunks_pending) += 1;
    }
    stats_.instances = static_cast<int>(instances_.size());
    stats_.collision_triangles = static_cast<int>(collision_->triangles().size());
    stats_.last_update_ms = ms_since(start);
    stats_.worst_update_ms = std::max(stats_.worst_update_ms, stats_.last_update_ms);
}

bool LevelStreamer::ready() const {
    if (!open_ || collision_dirty_ || collision_build_) {
        return false;
    }
    return std::all_of(chunks_.begin(), chunks_.end(), [](const auto& entry) { return entry.second->stage == Chunk::Stage::Loaded; }) &&
           !chunks_.empty();
}

bool LevelStreamer::finish(double timeout_seconds) {
    const Uint64 start = SDL_GetPerformanceCounter();
    const double saved = settings_.instantiate_budget_ms;
    settings_.instantiate_budget_ms = 1e9;  // no budget: everything now
    bool done = false;
    while (!(done = ready()) && ms_since(start) < timeout_seconds * 1000.0) {
        update();
        if (collision_build_) {
            jobs().wait(collision_build_->job);
        }
        SDL_Delay(1);
    }
    settings_.instantiate_budget_ms = saved;
    return done;
}

entt::entity LevelStreamer::entity_of(std::uint32_t id) const {
    const auto found = instances_.find(id);
    return found != instances_.end() ? found->second : entt::null;
}

bool LevelStreamer::loaded(glm::ivec2 coord) const {
    const auto found = chunks_.find({coord.x, coord.y});
    return found != chunks_.end() && found->second->stage == Chunk::Stage::Loaded;
}

}  // namespace moteur
