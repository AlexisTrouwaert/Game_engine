#include "level_editor.hpp"

#include <SDL3/SDL.h>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <system_error>

#include "moteur/color.hpp"
#include "moteur/debug_lines.hpp"
#include "moteur/debug_tools.hpp"
#include "moteur/file_io.hpp"
#include "moteur/level_streamer.hpp"
#include "moteur/paths.hpp"
#include "moteur/picking.hpp"
#include "moteur/prefab.hpp"
#include "moteur/reflection_io.hpp"
#include "moteur/renderer.hpp"

#include "demo3d.hpp"

namespace {

constexpr const char* kPayload = "LEVEL_ASSET";
constexpr int kThumbnailsPerFrame = 2;
constexpr double kNavigationPause = 0.4;  // seconds without a change before the navigation is built again
constexpr float kLibraryWidth = 330.0f;
constexpr float kInspectorWidth = 340.0f;

std::filesystem::path utf8_path(const std::string& text) {
    return {std::u8string(text.begin(), text.end())};
}

bool ends_with(std::string_view text, std::string_view end) {
    return text.size() >= end.size() && text.compare(text.size() - end.size(), end.size(), end) == 0;
}

std::string lower(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

double ms_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

std::uint64_t mix(std::uint64_t hash, std::uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
    return hash;
}

std::uint64_t file_stamp(const std::string& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(utf8_path(path), error);
    if (error) {
        return 0;
    }
    return mix(static_cast<std::uint64_t>(size), static_cast<std::uint64_t>(moteur::file_time(path)));
}

// The fields of `after` that differ from `before` (objects compared field by field).
nlohmann::json changed_fields(const nlohmann::json& before, const nlohmann::json& after) {
    nlohmann::json changes = nlohmann::json::object();
    if (!after.is_object()) {
        return changes;
    }
    for (auto it = after.begin(); it != after.end(); ++it) {
        const auto old = before.is_object() ? before.find(it.key()) : before.end();
        if (before.is_object() && old != before.end() && *old == it.value()) {
            continue;
        }
        if (it.value().is_object() && before.is_object() && old != before.end() && old->is_object()) {
            nlohmann::json inner = changed_fields(*old, it.value());
            if (!inner.empty()) {
                changes[it.key()] = std::move(inner);
            }
        } else {
            changes[it.key()] = it.value();
        }
    }
    return changes;
}

// `overrides` without what equals `base` (overrides set back to the prefab's values go away).
void prune_overrides(nlohmann::json& overrides, const nlohmann::json& base) {
    if (!overrides.is_object()) {
        return;
    }
    for (auto it = overrides.begin(); it != overrides.end();) {
        const auto there = base.is_object() ? base.find(it.key()) : base.end();
        if (base.is_object() && there != base.end()) {
            if (it.value().is_object() && there->is_object()) {
                prune_overrides(it.value(), *there);
                if (it.value().empty()) {
                    it = overrides.erase(it);
                    continue;
                }
            } else if (it.value() == *there) {
                it = overrides.erase(it);
                continue;
            }
        }
        ++it;
    }
}

ImU32 handle_color(moteur::GizmoHandle handle, bool hot) {
    if (hot) {
        return IM_COL32(255, 220, 60, 255);
    }
    switch (handle) {
        case moteur::GizmoHandle::X: return IM_COL32(230, 70, 70, 255);
        case moteur::GizmoHandle::Y: return IM_COL32(90, 210, 80, 255);
        case moteur::GizmoHandle::Z: return IM_COL32(80, 130, 240, 255);
        case moteur::GizmoHandle::XY: return IM_COL32(120, 120, 240, 200);
        case moteur::GizmoHandle::XZ: return IM_COL32(120, 220, 120, 200);
        case moteur::GizmoHandle::YZ: return IM_COL32(240, 120, 120, 200);
        default: return IM_COL32(235, 235, 235, 255);
    }
}

ImVec2 to_imgui(glm::vec2 pixel) {
    const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
    return {pixel.x / std::max(scale.x, 1e-3f), pixel.y / std::max(scale.y, 1e-3f)};
}

ImTextureRef texture_ref(const moteur::Texture& texture) {
    return ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture.gpu.get())));
}

}  // namespace

// --- Setup ------------------------------------------------------------------------------------

std::string LevelEditor::levels_directory(moteur::Application& app) {
#ifdef MOTEUR_ASSETS_SOURCE_DIR
    const std::string source = MOTEUR_ASSETS_SOURCE_DIR;
    std::error_code error;
    if (std::filesystem::is_directory(utf8_path(source + "/levels"), error)) {
        return source + "/levels/";
    }
#endif
    return app.assets().root() + "levels/";
}

LevelEditor::LevelEditor(moteur::Application& app, const Options& options, bool standalone)
    : app_(app), options_(options), standalone_(standalone),
      levels_dir_(options.levels_directory.empty() ? levels_directory(app) : options.levels_directory),
      sources_(moteur::asset_level_sources(app.assets())) {
    if (levels_dir_.back() != '/' && levels_dir_.back() != '\\') {
        levels_dir_ += '/';
    }
    moteur::Renderer& renderer = app.renderer();
    sky_ = moteur::Environment::create(renderer, moteur::make_sky(256, 128), "level_editor.sky");
    renderer.meshes().set_shadows(moteur::ShadowOptions{});
    renderer.meshes().set_culling(true);
    renderer.meshes().set_ids(true);  // what the pointer is over, and the selection by click
    renderer.set_render_scale(1.0f);
    thumbnail_renderer_ = std::make_unique<moteur::ThumbnailRenderer>(renderer, 96);
    thumbnails_ = std::make_unique<moteur::ThumbnailCache>(renderer, app.preferences_directory() + "thumbnails/");
    level_.set_listener([this](const moteur::LevelChange& change) { on_change(change); });

    camera_.set_angles(yaw_, pitch_);
    camera_.set_visible_height(30.0f);
    scan_library();
    refresh_levels();
    if (options_.level.empty() || !open(options_.level)) {
        if (!options_.level.empty()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Level editor: %s", status_.c_str());
        }
        moteur::LevelDocument fresh;
        moteur::LevelPoint start;
        start.name = "start";
        fresh.points.push_back(start);
        adopt(std::move(fresh));
        key_.clear();
        saved_revision_ = level_.revision();
    }
    if (options_.focus) {
        camera_.set_target(*options_.focus);
        camera_.set_visible_height(options_.focus_height);
    }
    show_collision_ = options_.show_collision;
    if (moteur::DebugTools* tools = app.debug_tools()) {
        tools->watch(world_, "Level editor");
    }
}

LevelEditor::~LevelEditor() {
    trial_.reset();
    if (navigation_running_) {
        try {
            moteur::jobs().wait(navigation_job_);
        } catch (const std::exception& e) {  // the job keeps its error to itself; the editor is closing
            SDL_Log("Level editor: navigation on closing: %s", e.what());
        }
    }
    if (modified()) {
        std::string error;
        if (moteur::write_file_atomic(recovery_path(), level_json().dump(2) + "\n", error)) {
            SDL_Log("Level editor: unsaved changes kept in %s", recovery_path().c_str());
        } else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Level editor: %s", error.c_str());
        }
    }
    if (moteur::DebugTools* tools = app_.debug_tools()) {
        tools->forget(world_);
    }
}

// --- File -------------------------------------------------------------------------------------

std::string LevelEditor::level_path(const std::string& key) const {
    const std::string name = key.rfind("levels/", 0) == 0 ? key.substr(7) : key;
    return levels_dir_ + name;
}

std::string LevelEditor::recovery_path() const {
    std::string name = key_.empty() ? std::string("sans_nom.level.json") : key_.substr(key_.find_last_of('/') + 1);
    return app_.preferences_directory() + "editeur_niveau_" + name;
}

nlohmann::json LevelEditor::level_json() const {
    const moteur::Assets& assets = app_.assets();
    return level_.document().to_json([&assets](const std::string& key) { return assets.reference_to(key); });
}

void LevelEditor::refresh_levels() {
    level_names_.clear();
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(utf8_path(levels_dir_), error)) {
        const std::u8string name = entry.path().filename().u8string();
        const std::string text(name.begin(), name.end());
        if (ends_with(text, ".level.json")) {
            level_names_.push_back("levels/" + text);
        }
    }
    std::sort(level_names_.begin(), level_names_.end());
}

bool LevelEditor::open(const std::string& key) {
    const std::string path = level_path(key);
    try {
        moteur::DataIssues issues;
        moteur::Assets& assets = app_.assets();
        moteur::LevelDocument document = moteur::LevelDocument::parse(
            moteur::read_text_file(path), key, [&assets](const nlohmann::json& reference) { return assets.resolve_reference(reference); },
            issues);
        adopt(std::move(document));
    } catch (const std::exception& e) {
        status_ = e.what();
        status_error_ = true;
        return false;
    }
    key_ = key;
    file_time_ = moteur::file_time(path);
    external_change_ = false;
    saved_revision_ = level_.revision();
    const std::int64_t recovery = moteur::file_time(recovery_path());
    recovery_found_ = recovery != 0 && recovery > file_time_;
    status_ = "Opened: " + path;
    status_error_ = false;
    return true;
}

void LevelEditor::adopt(moteur::LevelDocument document) {
    level_ = moteur::EditableLevel(std::move(document));
    level_.set_listener([this](const moteur::LevelChange& change) { on_change(change); });
    selection_.clear();
    painting_ = false;
    widget_action_ = false;
    gizmo_.end();
    if (!level_.document().layers.empty()) {
        layer_ = level_.document().layers.front().name;
    }
    ground_brush_ = level_.document().ground.prefabs.empty() ? 0 : 1;
    rebuild_all();
    frame_level();
}

bool LevelEditor::save(bool overwrite) {
    if (key_.empty()) {
        open_save_as_ = true;
        return false;
    }
    const std::string path = level_path(key_);
    if (!overwrite && moteur::file_time(path) != file_time_) {
        confirm_overwrite_ = true;  // changed (or made) by someone else since it was opened
        return false;
    }
    std::string error;
    if (!moteur::write_file_atomic(path, level_json().dump(2) + "\n", error)) {
        status_ = "Save failed: " + error;
        status_error_ = true;
        return false;
    }
    file_time_ = moteur::file_time(path);
    external_change_ = false;
    saved_revision_ = level_.revision();
    std::error_code ignored;
    std::filesystem::remove(utf8_path(recovery_path()), ignored);
    recovery_found_ = false;
    status_ = "Saved: " + path;
    status_error_ = false;
    refresh_levels();
    return true;
}

// --- Library ----------------------------------------------------------------------------------

void LevelEditor::scan_library() {
    library_.clear();
    kit_grids_.clear();
    kit_families_.clear();
    moteur::Assets& assets = app_.assets();
    std::set<std::string> keys;
    std::map<std::string, std::string> roots;  // where each file really is (a mounted folder, the sources)
    for (const auto& [id, record] : assets.registry().records()) {
        keys.insert(record.key);
        roots[record.key] = record.root;
    }
    const auto source_path = [&](const std::string& key) {
        const auto root = roots.find(key);
        return root != roots.end() && !root->second.empty() ? root->second + key : assets.file_path(key);
    };
    // The kits and the catalogues of props: the snapping grid and the family of their pieces.
    for (const std::string& key : keys) {
        if (!ends_with(key, ".kit.json") && !ends_with(key, "catalogue.json")) {
            continue;
        }
        try {
            const std::string text = moteur::read_text_file(assets.file_path(key));
            if (text.empty()) {
                SDL_Log("Level editor: %s is empty (left out)", key.c_str());
                continue;
            }
            const nlohmann::json kit = nlohmann::json::parse(text);
            for (const nlohmann::json& prop : kit.value("props", nlohmann::json::array())) {
                const std::string prefab = prop.value("prefab", std::string());
                if (!prefab.empty()) {
                    kit_grids_[prefab] = prop.value("grid", 0.0f);
                    kit_families_[prefab] = prop.value("family", std::string());
                }
            }
        } catch (const std::exception& e) {
            SDL_Log("Level editor: kit %s: %s", key.c_str(), e.what());
        }
    }
    for (const std::string& key : keys) {
        const bool prefab = ends_with(key, ".prefab.json");
        const bool model = ends_with(key, ".glb") || ends_with(key, ".gltf");
        if (!prefab && !model) {
            continue;
        }
        const std::size_t dot = key.find('.', key.find_last_of('/') + 1);
        const std::string stem = key.substr(0, dot);
        if (model && keys.count(stem + ".prefab.json") != 0) {
            continue;  // shown as its prefab
        }
        LibraryItem item;
        item.key = key;
        item.prefab = prefab;
        item.folder = key.substr(0, key.find_last_of('/') + 1);
        item.name = stem.substr(stem.find_last_of('/') + 1);
        const std::string path = source_path(key);
        item.fingerprint = mix(mix(std::hash<std::string>{}(key), file_stamp(path)), file_stamp(source_path(stem + ".glb")));
        if (prefab) {
            try {
                const std::string text = moteur::read_text_file(path);
                item.light = text.find("\"light\"") != std::string::npos;
            } catch (const std::exception& e) {
                SDL_Log("Level editor: %s: %s", key.c_str(), e.what());  // listed anyway; its thumbnail will say more
            }
        }
        const std::string folder = lower(item.folder);
        item.ground = folder.find("/sols/") != std::string::npos || folder.find("/ground") != std::string::npos ||
                      lower(item.name).find("floor") != std::string::npos || lower(item.name).rfind("sol_", 0) == 0;
        if (const auto grid = kit_grids_.find(key); grid != kit_grids_.end()) {
            item.grid = grid->second;
            item.family = kit_families_[key];
        }
        library_.push_back(std::move(item));
    }
    std::sort(library_.begin(), library_.end(), [](const LibraryItem& a, const LibraryItem& b) {
        return a.folder != b.folder ? a.folder < b.folder : a.name < b.name;
    });
}

bool LevelEditor::make_prefab_for_model(const LibraryItem& item) {
    // A prefab of one entity: the model, and its collision from the model's own (COL_ / UCX_ nodes,
    // or its visible triangles as the import decides).
    moteur::Assets& assets = app_.assets();
    const std::string model_path = assets.file_path(item.key);
    const std::string prefab_path = model_path.substr(0, model_path.size() - (ends_with(item.key, ".gltf") ? 5 : 4)) + ".prefab.json";
    nlohmann::json prefab = {
        {"version", 1},
        {"entities",
         nlohmann::json::array({{{"id", 1},
                                 {"name", item.name},
                                 {"components",
                                  {{"transform", nlohmann::json::object()},
                                   {"model", {{"model", assets.reference_to(item.key)}}},
                                   {"static_collision", {{"from_model", true}}}}}}})}};
    std::string error;
    if (!moteur::write_file_atomic(prefab_path, prefab.dump(2) + "\n", error)) {
        status_ = "The prefab cannot be written: " + error;
        status_error_ = true;
        return false;
    }
    // Its identity (.meta) and its key, then the library again.
    std::vector<std::string> roots;
    for (const auto& [id, record] : assets.registry().records()) {
        if (!record.root.empty() && std::find(roots.begin(), roots.end(), record.root) == roots.end()) {
            roots.push_back(record.root);
        }
    }
    (void)assets.registry().scan(roots, true);
    scan_library();
    status_ = "Prefab made: " + prefab_path;
    status_error_ = false;
    return true;
}

void LevelEditor::ensure_thumbnails() {
    if (thumbnails_wanted_.empty()) {
        return;
    }
    moteur::Assets& assets = app_.assets();
    int drawn = 0;
    for (auto it = thumbnails_wanted_.begin(); it != thumbnails_wanted_.end() && drawn < kThumbnailsPerFrame;) {
        const std::string key = *it;
        it = thumbnails_wanted_.erase(it);
        const auto item = std::find_if(library_.begin(), library_.end(), [&key](const LibraryItem& i) { return i.key == key; });
        if (item == library_.end() || thumbnails_failed_.count(key) != 0 || thumbnails_->find(key, item->fingerprint) != nullptr) {
            continue;
        }
        try {
            moteur::World world;
            if (item->prefab) {
                const moteur::Asset<moteur::Prefab> prefab = assets.prefab(key);
                const moteur::Prefab::Context context = assets.prefab_context();
                moteur::DataIssues issues;
                (void)moteur::instantiate(world, *prefab, {}, &issues, &context);
            } else {
                const entt::entity entity = world.registry().create();
                world.registry().emplace<moteur::Transform>(entity);
                world.registry().emplace<moteur::ModelComponent>(entity, moteur::ModelComponent{assets.model(key), {}});
            }
            moteur::Aabb bounds;
            for (const auto& [entity, model] : world.registry().view<moteur::ModelComponent>().each()) {
                if (model.model) {
                    const moteur::Aabb box = moteur::transform_box(model.model->bounds, world.world_matrix(entity, 1.0f));
                    if (!box.empty()) {
                        bounds.add(box.min);
                        bounds.add(box.max);
                    }
                }
            }
            for (const auto& [entity, mesh] : world.registry().view<moteur::MeshComponent>().each()) {
                if (mesh.mesh) {
                    const moteur::Aabb box = moteur::transform_box(mesh.mesh->bounds, world.world_matrix(entity, 1.0f));
                    if (!box.empty()) {
                        bounds.add(box.min);
                        bounds.add(box.max);
                    }
                }
            }
            const moteur::Image image = thumbnail_renderer_->render(world, bounds, &*sky_);
            thumbnails_->store(key, item->fingerprint, image);
            ++thumbnails_drawn_;
        } catch (const std::exception& e) {
            SDL_Log("Level editor: no thumbnail for %s: %s", key.c_str(), e.what());
            thumbnails_failed_.insert(key);
        }
        ++drawn;
    }
}

// --- Edits ------------------------------------------------------------------------------------

float LevelEditor::snap_step() const {
    if (!selection_.empty()) {
        if (const moteur::LevelInstance* instance = level_.document().instance(selection_.front())) {
            if (const auto grid = kit_grids_.find(instance->prefab); grid != kit_grids_.end() && grid->second > 0.0f) {
                return grid->second;
            }
        }
    }
    if (tool_ == Tool::Place && !chosen_.empty()) {
        if (const auto grid = kit_grids_.find(chosen_); grid != kit_grids_.end() && grid->second > 0.0f) {
            return grid->second;
        }
    }
    return grid_step_;
}

glm::vec3 LevelEditor::drop_point(glm::vec2 pointer) const {
    const moteur::Ray ray = camera_.screen_ray(pointer);
    if (surface_snap_ && scene_.built()) {
        if (const std::optional<moteur::RayHit> hit = scene_.raycast(ray.origin, ray.direction, 2000.0f)) {
            return hit->point;
        }
    }
    const float ground = level_.document().ground.width > 0 ? level_.document().ground.level : 0.0f;
    return ray.hit_height(ground).value_or(camera_.target());
}

void LevelEditor::place(const std::string& prefab, glm::vec3 position) {
    moteur::LevelInstance instance;
    instance.prefab = prefab;
    instance.layer = layer_;
    float step = grid_step_;
    if (const auto grid = kit_grids_.find(prefab); grid != kit_grids_.end() && grid->second > 0.0f) {
        step = grid->second;
    }
    if (snap_) {
        position.x = moteur::snap_value(position.x, step);
        position.z = moteur::snap_value(position.z, step);
    }
    instance.transform.position = position;
    level_.begin_action("place " + prefab.substr(prefab.find_last_of('/') + 1));
    const std::uint32_t id = level_.add(std::move(instance));
    level_.end_action();
    selection_ = {id};
}

void LevelEditor::select(std::uint32_t id, bool add, bool toggle) {
    const auto it = std::find(selection_.begin(), selection_.end(), id);
    if (toggle) {
        if (it != selection_.end()) {
            selection_.erase(it);
        } else if (id != 0) {
            selection_.push_back(id);
        }
        return;
    }
    if (!add) {
        selection_.clear();
    }
    if (id != 0 && std::find(selection_.begin(), selection_.end(), id) == selection_.end()) {
        selection_.push_back(id);
    }
}

void LevelEditor::duplicate_selection() {
    if (selection_.empty()) {
        return;
    }
    const float step = snap_ ? snap_step() : 1.0f;
    std::vector<std::uint32_t> copies;
    level_.begin_action("duplicate");
    for (const std::uint32_t id : selection_) {
        if (const moteur::LevelInstance* instance = level_.document().instance(id)) {
            moteur::LevelInstance copy = *instance;
            copy.transform.position.x += step;
            copies.push_back(level_.add(std::move(copy)));
        }
    }
    level_.end_action();
    selection_ = copies;
}

void LevelEditor::delete_selection() {
    if (selection_.empty()) {
        return;
    }
    level_.begin_action("delete");
    for (const std::uint32_t id : selection_) {
        level_.remove(id);
    }
    level_.end_action();
    selection_.clear();
}

void LevelEditor::apply_gizmo(const moteur::GizmoDelta& delta) {
    for (const auto& [id, start] : drag_start_) {
        const moteur::LevelInstance* instance = level_.document().instance(id);
        if (instance == nullptr) {
            continue;
        }
        moteur::LevelInstance changed = *instance;
        moteur::Transform& t = changed.transform;
        switch (gizmo_mode_) {
            case moteur::GizmoMode::Translate:
                t.position = start.position + delta.translation;
                break;
            case moteur::GizmoMode::Rotate: {
                const glm::quat turn = glm::angleAxis(delta.angle, delta.axis);
                t.position = drag_pivot_ + turn * (start.position - drag_pivot_);
                t.rotation = glm::normalize(turn * start.rotation);
                break;
            }
            case moteur::GizmoMode::Scale:
                t.scale = start.scale * delta.scale;
                t.position = drag_pivot_ + (start.position - drag_pivot_) * delta.scale;
                break;
        }
        level_.set(changed);
    }
}

void LevelEditor::undo() {
    if (level_.undo()) {
        status_ = "Undone";
        status_error_ = false;
    }
}

void LevelEditor::redo() {
    if (level_.redo()) {
        status_ = "Redone";
        status_error_ = false;
    }
}

void LevelEditor::paint_at(glm::vec3 point, bool erase) {
    const moteur::LevelGround& ground = level_.document().ground;
    if (ground.prefabs.empty() || (!erase && ground_brush_ == 0)) {
        return;
    }
    const int reach = brush_size_ - 1;
    for (int dz = -reach; dz <= reach; ++dz) {
        for (int dx = -reach; dx <= reach; ++dx) {
            // Painting may grow the grid (its origin moves): each cell from the point again.
            const moteur::LevelGround& now = level_.document().ground;
            const glm::vec3 at = point + glm::vec3(static_cast<float>(dx), 0.0f, static_cast<float>(dz)) * now.cell_size;
            level_.paint_ground(moteur::ground_cell(now, at), erase ? 0 : ground_brush_);
        }
    }
}

void LevelEditor::track_item(const char* action) {
    if (ImGui::IsItemActivated() && !level_.in_action()) {
        level_.begin_action(action);
        widget_action_ = true;
    }
    if (ImGui::IsItemDeactivated() && widget_action_) {
        level_.end_action();
        widget_action_ = false;
    }
}

// --- View -------------------------------------------------------------------------------------

void LevelEditor::on_change(const moteur::LevelChange& change) {
    navigation_timer_ = 0.0;
    navigation_wanted_ = true;
    switch (change.kind) {
        case moteur::LevelChange::Kind::Instance:
            dirty_instances_.insert(change.id);
            if (level_.document().instance(change.id) == nullptr) {
                std::erase(selection_, change.id);
            }
            break;
        case moteur::LevelChange::Kind::Ground:
            ground_dirty_ = true;
            break;
        case moteur::LevelChange::Kind::Settings:
            // Layers shown or hidden: their instances again.
            for (const moteur::LevelInstance& instance : level_.document().instances) {
                dirty_instances_.insert(instance.id);
            }
            break;
        default:
            break;
    }
}

void LevelEditor::rebuild_instance(std::uint32_t id) {
    if (const auto it = roots_.find(id); it != roots_.end()) {
        world_.destroy(it->second);
        roots_.erase(it);
    }
    instance_collision_.erase(id);
    instance_bounds_.erase(id);
    collision_dirty_ = true;
    const moteur::LevelInstance* instance = level_.document().instance(id);
    if (instance == nullptr) {
        return;
    }
    for (const moteur::LevelLayer& layer : level_.document().layers) {
        if (layer.name == instance->layer && !layer.visible) {
            return;
        }
    }
    moteur::Assets& assets = app_.assets();
    try {
        const moteur::Asset<moteur::Prefab> prefab = assets.prefab(instance->prefab);
        const moteur::Prefab::Context context = assets.prefab_context();
        moteur::PrefabPlacement placement;
        placement.position = instance->transform.position;
        placement.rotation = instance->transform.rotation;
        placement.scale = instance->transform.scale;
        placement.overrides = instance->overrides;
        moteur::DataIssues issues;
        const moteur::PrefabInstance made = moteur::instantiate(world_, *prefab, placement, &issues, &context);
        entt::registry& registry = world_.registry();
        registry.emplace_or_replace<moteur::LevelInstanceId>(made.root, id);
        registry.emplace_or_replace<moteur::Selectable>(made.root);
        roots_[id] = made.root;
        // Its box: every mesh of its entities, where they are.
        moteur::Aabb bounds;
        for (const entt::entity entity : made.entities) {
            const glm::mat4 world = world_.world_matrix(entity, 1.0f);
            if (const moteur::ModelComponent* model = registry.try_get<moteur::ModelComponent>(entity); model != nullptr && model->model) {
                const moteur::Aabb box = moteur::transform_box(model->model->bounds, world);
                if (!box.empty()) {
                    bounds.add(box.min);
                    bounds.add(box.max);
                }
            }
            if (const moteur::MeshComponent* mesh = registry.try_get<moteur::MeshComponent>(entity); mesh != nullptr && mesh->mesh) {
                const moteur::Aabb box = moteur::transform_box(mesh->mesh->bounds, world);
                if (!box.empty()) {
                    bounds.add(box.min);
                    bounds.add(box.max);
                }
            }
        }
        if (bounds.empty()) {
            bounds.add(instance->transform.position - glm::vec3(0.25f));
            bounds.add(instance->transform.position + glm::vec3(0.25f));
        }
        instance_bounds_[id] = bounds;
        moteur::CollisionMeshData collision;
        moteur::prefab_collision(*prefab, instance->transform, instance->overrides, sources_, collision);
        instance_collision_[id] = std::move(collision);
        if (!issues.all().empty()) {
            if (++instance_issues_ <= 3) {
                SDL_Log("Level editor: instance %u (%s): %s: %s", id, instance->prefab.c_str(), issues.all().front().where.c_str(),
                        issues.all().front().message.c_str());
            }
            last_issue_ = instance->prefab + ": " + issues.all().front().message;
        }
    } catch (const std::exception& e) {
        ++instance_issues_;
        last_issue_ = e.what();
    }
}

void LevelEditor::rebuild_ground() {
    for (const entt::entity entity : ground_entities_) {
        world_.destroy(entity);
    }
    ground_entities_.clear();
    ground_collision_ = {};
    collision_dirty_ = true;
    const moteur::LevelGround& ground = level_.document().ground;
    moteur::Assets& assets = app_.assets();
    const moteur::Prefab::Context context = assets.prefab_context();
    std::vector<moteur::Asset<moteur::Prefab>> prefabs;
    for (const std::string& key : ground.prefabs) {
        try {
            prefabs.push_back(assets.prefab(key));
        } catch (const std::exception& e) {
            prefabs.emplace_back();
            ++instance_issues_;
            last_issue_ = e.what();
        }
    }
    for (int z = 0; z < ground.height; ++z) {
        for (int x = 0; x < ground.width; ++x) {
            const std::uint8_t value = ground.cells[static_cast<std::size_t>(z) * static_cast<std::size_t>(ground.width) + static_cast<std::size_t>(x)];
            if (value == 0 || value > prefabs.size() || !prefabs[value - 1u]) {
                continue;
            }
            const moteur::Prefab& prefab = *prefabs[value - 1u];
            moteur::PrefabPlacement placement;
            placement.position = {ground.origin.x + static_cast<float>(x) * ground.cell_size, ground.level,
                                  ground.origin.y + static_cast<float>(z) * ground.cell_size};
            const moteur::PrefabInstance made = moteur::instantiate(world_, prefab, placement, nullptr, &context);
            ground_entities_.push_back(made.root);
            moteur::Transform transform;
            transform.position = placement.position;
            moteur::prefab_collision(prefab, transform, nlohmann::json::object(), sources_, ground_collision_);
        }
    }
}

void LevelEditor::rebuild_all() {
    const auto start = std::chrono::steady_clock::now();
    for (const auto& [id, root] : roots_) {
        world_.destroy(root);
    }
    roots_.clear();
    instance_collision_.clear();
    instance_bounds_.clear();
    instance_issues_ = 0;
    for (const moteur::LevelInstance& instance : level_.document().instances) {
        rebuild_instance(instance.id);
    }
    rebuild_ground();
    dirty_instances_.clear();
    ground_dirty_ = false;
    rebuild_collision();
    build_ms_ = ms_since(start);
    navigation_wanted_ = true;
    navigation_timer_ = kNavigationPause;  // at once
}

void LevelEditor::rebuild_collision() {
    scene_.clear();
    for (const moteur::LevelInstance& instance : level_.document().instances) {
        if (const auto it = instance_collision_.find(instance.id); it != instance_collision_.end() && !it->second.empty()) {
            scene_.add(it->second, glm::mat4(1.0f), instance.id);
        }
    }
    if (!ground_collision_.empty()) {
        scene_.add(ground_collision_, glm::mat4(1.0f), 0);
    }
    scene_.build();
    collision_dirty_ = false;
}

void LevelEditor::start_navigation() {
    if (navigation_running_) {
        return;
    }
    // A copy of what it needs: the editor goes on changing the level meanwhile.
    auto geometry = std::make_shared<moteur::CollisionMeshData>(ground_collision_);
    for (const moteur::LevelInstance& instance : level_.document().instances) {
        if (const auto it = instance_collision_.find(instance.id); it != instance_collision_.end()) {
            geometry->append(it->second, glm::mat4(1.0f));
        }
    }
    std::vector<glm::vec3> seeds;
    for (const moteur::LevelPoint& point : level_.document().points) {
        seeds.push_back(point.position);
    }
    const std::vector<moteur::NavAgent> agents = level_.document().agents;
    moteur::NavBuildSettings settings = level_.document().navigation;
    settings.tile_size = level_.document().chunk_size;
    navigation_result_ = std::make_shared<std::vector<moteur::NavMesh>>();
    navigation_error_ = std::make_shared<std::string>();
    auto result = navigation_result_;
    auto error = navigation_error_;
    navigation_ms_ = 0.0;
    auto spent = navigation_spent_;
    navigation_job_ = moteur::jobs().submit([geometry, seeds, agents, settings, result, error, spent] {
        const auto start = std::chrono::steady_clock::now();
        try {
            if (geometry->empty()) {
                return;
            }
            moteur::CollisionScene scene;
            scene.add(*geometry, glm::mat4(1.0f));
            scene.build();
            for (const moteur::NavAgent& agent : agents) {
                moteur::NavMesh mesh;
                mesh.build(scene, agent, settings);
                if (!seeds.empty()) {
                    mesh.prune(seeds);
                }
                result->push_back(std::move(mesh));
            }
        } catch (const std::exception& e) {
            *error = e.what();
        }
        *spent = ms_since(start);
    });
    navigation_running_ = true;
    navigation_wanted_ = false;
}

void LevelEditor::finish_navigation() {
    if (!navigation_running_ || !navigation_job_.done()) {
        return;
    }
    moteur::jobs().wait(navigation_job_);
    navigation_running_ = false;
    navigation_ms_ = *navigation_spent_;
    if (!navigation_error_->empty()) {
        status_ = "Navigation: " + *navigation_error_;
        status_error_ = true;
        return;
    }
    navigation_ = std::move(*navigation_result_);
}

void LevelEditor::frame_level() {
    const moteur::Aabb bounds = level_.document().bounds();
    if (bounds.empty()) {
        camera_.set_target({0.0f, 0.0f, 0.0f});
        camera_.set_visible_height(30.0f);
        return;
    }
    camera_.set_target(bounds.center());
    const glm::vec3 size = bounds.size();
    camera_.set_visible_height(std::clamp(std::max(size.x, size.z) * 0.8f, 10.0f, 600.0f));
}

glm::vec3 LevelEditor::selection_centre() const {
    glm::vec3 sum(0.0f);
    int count = 0;
    for (const std::uint32_t id : selection_) {
        if (const moteur::LevelInstance* instance = level_.document().instance(id)) {
            sum += instance->transform.position;
            ++count;
        }
    }
    return count > 0 ? sum / static_cast<float>(count) : glm::vec3(0.0f);
}

void LevelEditor::update_camera(float dt) {
    const bool has_ui = ImGui::GetCurrentContext() != nullptr;
    if (has_ui && ImGui::GetIO().WantTextInput) {
        return;
    }
    const bool* keys = SDL_GetKeyboardState(nullptr);
    if (keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL]) {
        return;
    }
    glm::vec2 direction(0.0f);
    direction.x = (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT] ? 1.0f : 0.0f);
    direction.y = (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN] ? 1.0f : 0.0f);
    if (direction == glm::vec2(0.0f) || orbiting_) {
        return;
    }
    const glm::vec3 forward = camera_.forward();
    glm::vec3 ahead(forward.x, 0.0f, forward.z);
    ahead = glm::length(ahead) > 1e-3f ? glm::normalize(ahead) : glm::vec3(0.0f, 0.0f, -1.0f);
    const glm::vec3 right = glm::cross(ahead, glm::vec3(0.0f, 1.0f, 0.0f));
    camera_.set_target(camera_.target() + (right * direction.x + ahead * direction.y) * (camera_.visible_height() * 0.9f * dt));
}

// --- Events and update ------------------------------------------------------------------------

void LevelEditor::on_event(const SDL_Event& event) {
    if (trial_) {
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE && !event.key.repeat) {
            end_trial_ = true;
            return;
        }
        trial_->on_event(event);
        return;
    }
    const bool has_ui = ImGui::GetCurrentContext() != nullptr;
    const ImGuiIO* io = has_ui ? &ImGui::GetIO() : nullptr;
    switch (event.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            if (io != nullptr && io->WantCaptureMouse) {
                return;
            }
            const glm::vec2 pointer = app_.to_pixels({event.button.x, event.button.y});
            last_mouse_ = pointer;
            if (event.button.button == SDL_BUTTON_RIGHT) {
                orbiting_ = true;
                return;
            }
            if (event.button.button == SDL_BUTTON_MIDDLE) {
                panning_ = true;
                return;
            }
            if (event.button.button != SDL_BUTTON_LEFT) {
                return;
            }
            const SDL_Keymod mods = SDL_GetModState();
            switch (tool_) {
                case Tool::Select: {
                    if (!selection_.empty() && hovered_handle_ != moteur::GizmoHandle::None &&
                        gizmo_.begin(camera_, selection_centre(), gizmo_mode_, hovered_handle_, pointer)) {
                        drag_start_.clear();
                        for (const std::uint32_t id : selection_) {
                            if (const moteur::LevelInstance* instance = level_.document().instance(id)) {
                                drag_start_[id] = instance->transform;
                            }
                        }
                        drag_pivot_ = selection_centre();
                        level_.begin_action(gizmo_mode_ == moteur::GizmoMode::Translate ? "move"
                                            : gizmo_mode_ == moteur::GizmoMode::Rotate  ? "turn"
                                                                                        : "scale");
                        return;
                    }
                    // The instance under the pointer, from the id pass (what is hovered).
                    pending_pick_ = pointer;
                    pending_pick_frames_ = 0;
                    pick_add_ = (mods & SDL_KMOD_CTRL) != 0;
                    pick_toggle_ = (mods & SDL_KMOD_SHIFT) != 0;
                    return;
                }
                case Tool::Place:
                    if (!chosen_.empty()) {
                        place(chosen_, drop_point(pointer));
                    }
                    return;
                case Tool::Paint:
                case Tool::Erase:
                    level_.begin_action(tool_ == Tool::Paint ? "paint the ground" : "erase the ground");
                    painting_ = true;
                    return;
                case Tool::Point: {
                    moteur::LevelPoint point;
                    point.name = point_name_;
                    if (const moteur::LevelPoint* existing = level_.document().point(point_name_)) {
                        point = *existing;
                    }
                    point.position = drop_point(pointer);
                    level_.begin_action("point " + point_name_);
                    level_.set_point(point);
                    level_.end_action();
                    return;
                }
            }
            return;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.button == SDL_BUTTON_RIGHT) {
                orbiting_ = false;
            } else if (event.button.button == SDL_BUTTON_MIDDLE) {
                panning_ = false;
            } else if (event.button.button == SDL_BUTTON_LEFT) {
                if (gizmo_.dragging() != moteur::GizmoHandle::None) {
                    gizmo_.end();
                    level_.end_action();
                    drag_start_.clear();
                }
                if (painting_) {
                    painting_ = false;
                    level_.end_action();
                }
            }
            return;
        case SDL_EVENT_MOUSE_MOTION: {
            const glm::vec2 pointer = app_.to_pixels({event.motion.x, event.motion.y});
            const glm::vec2 moved = pointer - last_mouse_;
            last_mouse_ = pointer;
            if (orbiting_) {
                yaw_ += moved.x * 0.3f;
                pitch_ = std::clamp(pitch_ + moved.y * 0.3f, 10.0f, 89.0f);
                camera_.set_angles(yaw_, pitch_);
            } else if (panning_) {
                const glm::vec3 forward = camera_.forward();
                glm::vec3 ahead(forward.x, 0.0f, forward.z);
                ahead = glm::length(ahead) > 1e-3f ? glm::normalize(ahead) : glm::vec3(0.0f, 0.0f, -1.0f);
                const glm::vec3 right = glm::cross(ahead, glm::vec3(0.0f, 1.0f, 0.0f));
                const float metres = camera_.visible_height() / std::max(camera_.viewport().y, 1.0f);
                camera_.set_target(camera_.target() - (right * moved.x - ahead * moved.y) * metres);
            }
            return;
        }
        case SDL_EVENT_MOUSE_WHEEL: {
            if (io != nullptr && io->WantCaptureMouse) {
                return;
            }
            camera_.set_visible_height(std::clamp(camera_.visible_height() * std::pow(0.88f, event.wheel.y), 2.0f, 800.0f));
            return;
        }
        case SDL_EVENT_KEY_DOWN:
            break;
        default:
            return;
    }
    if (io != nullptr && io->WantTextInput) {
        return;
    }
    const bool ctrl = (event.key.mod & SDL_KMOD_CTRL) != 0;
    const bool shift = (event.key.mod & SDL_KMOD_SHIFT) != 0;
    const SDL_Keycode key = event.key.key;
    if (ctrl) {
        if (key == SDLK_Z && !shift) undo();
        else if (key == SDLK_Y || (key == SDLK_Z && shift)) redo();
        else if (key == SDLK_S && !event.key.repeat) save(false);
        else if (key == SDLK_D && !event.key.repeat) duplicate_selection();
        else if (key == SDLK_N && !event.key.repeat) open_new_ = true;
        return;
    }
    if (event.key.repeat) {
        return;
    }
    switch (key) {
        case SDLK_ESCAPE:
            if (!selection_.empty()) {
                selection_.clear();
            } else if (tool_ != Tool::Select) {
                tool_ = Tool::Select;
            } else if (!standalone_) {
                if (modified()) confirm_quit_ = true;
                else stop_requested_ = true;
            }
            break;
        case SDLK_Q: tool_ = Tool::Select; break;
        case SDLK_W: if (shift) gizmo_mode_ = moteur::GizmoMode::Translate; break;
        case SDLK_E: gizmo_mode_ = moteur::GizmoMode::Rotate; tool_ = Tool::Select; break;
        case SDLK_R: gizmo_mode_ = moteur::GizmoMode::Scale; tool_ = Tool::Select; break;
        case SDLK_T: gizmo_mode_ = moteur::GizmoMode::Translate; tool_ = Tool::Select; break;
        case SDLK_B: tool_ = Tool::Paint; break;
        case SDLK_X: tool_ = Tool::Erase; break;
        case SDLK_P: tool_ = Tool::Point; break;
        case SDLK_G: snap_ = !snap_; break;
        case SDLK_N: night_ = !night_; break;
        case SDLK_F:
            if (!selection_.empty()) {
                camera_.set_target(selection_centre());
            }
            break;
        case SDLK_HOME: frame_level(); break;
        case SDLK_DELETE: delete_selection(); break;
        case SDLK_END: {
            // Down onto what is under the selection (its own collision left out: from below its box).
            level_.begin_action("drop to the surface");
            for (const std::uint32_t id : selection_) {
                const moteur::LevelInstance* instance = level_.document().instance(id);
                const auto box = instance_bounds_.find(id);
                if (instance == nullptr || box == instance_bounds_.end() || !scene_.built()) {
                    continue;
                }
                const glm::vec3 from(instance->transform.position.x, box->second.min.y - 0.01f, instance->transform.position.z);
                if (const std::optional<moteur::RayHit> hit = scene_.raycast(from, {0.0f, -1.0f, 0.0f}, 500.0f)) {
                    moteur::LevelInstance changed = *instance;
                    changed.transform.position.y += hit->point.y - box->second.min.y;
                    level_.set(changed);
                }
            }
            level_.end_action();
            break;
        }
        case SDLK_LEFTBRACKET:
        case SDLK_RIGHTBRACKET: {
            const float sign = key == SDLK_LEFTBRACKET ? -1.0f : 1.0f;
            if (tool_ == Tool::Paint || tool_ == Tool::Erase) {
                brush_size_ = std::clamp(brush_size_ + static_cast<int>(sign), 1, 6);
            } else if (!selection_.empty()) {
                moteur::GizmoDelta turn;
                turn.axis = {0.0f, 1.0f, 0.0f};
                turn.angle = glm::radians(sign * (shift ? 90.0f : angle_step_));
                drag_start_.clear();
                for (const std::uint32_t id : selection_) {
                    if (const moteur::LevelInstance* instance = level_.document().instance(id)) {
                        drag_start_[id] = instance->transform;
                    }
                }
                drag_pivot_ = selection_centre();
                const moteur::GizmoMode mode = gizmo_mode_;
                gizmo_mode_ = moteur::GizmoMode::Rotate;
                level_.begin_action("turn");
                apply_gizmo(turn);
                level_.end_action();
                gizmo_mode_ = mode;
                drag_start_.clear();
            }
            break;
        }
        case SDLK_F5: trial_request_ = 1; break;
        case SDLK_F6: trial_request_ = 2; break;
        default: break;
    }
}

void LevelEditor::update(double dt) {
    elapsed_ += dt;
    if (options_.run_seconds > 0.0 && elapsed_ >= options_.run_seconds) {
        if (standalone_) {
            app_.quit();
        } else {
            stop_requested_ = true;
        }
    }
    // The trial is made and ended here, outside a frame (it loads and uploads).
    if (end_trial_) {
        trial_.reset();
        end_trial_ = false;
    }
    if (trial_request_ != 0) {
        start_trial(trial_request_ == 2);
        trial_request_ = 0;
    }
    if (options_.self_test) {
        run_self_test();
    }
    if (trial_) {
        trial_->update(dt);
        if (trial_->stop_requested()) {
            end_trial_ = true;
        }
        return;
    }
    update_camera(static_cast<float>(dt));
    const glm::vec2 pointer = app_.input().pointer();
    const bool over_panel = ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse;
    pointer_point_.reset();
    if (pointer.x >= 0.0f && pointer.y >= 0.0f && !over_panel) {
        pointer_point_ = drop_point(pointer);
    }

    // Dragging the handles, painting.
    if (gizmo_.dragging() != moteur::GizmoHandle::None) {
        moteur::GizmoSnap snap;
        if (snap_) {
            snap.translation = snap_step();
            snap.angle = glm::radians(angle_step_);
            snap.scale = 0.1f;
        }
        apply_gizmo(gizmo_.drag(camera_, pointer, snap));
    } else if (!selection_.empty() && tool_ == Tool::Select && !over_panel) {
        hovered_handle_ = gizmo_.hit(camera_, selection_centre(), gizmo_mode_, pointer);
    } else {
        hovered_handle_ = moteur::GizmoHandle::None;
    }
    if (painting_ && pointer_point_) {
        const moteur::Ray ray = camera_.screen_ray(pointer);
        if (const std::optional<glm::vec3> on_ground = ray.hit_height(level_.document().ground.level)) {
            paint_at(*on_ground, tool_ == Tool::Erase);
        }
    }

    // What changed: built again (the instances one by one, the collision once).
    for (const std::uint32_t id : dirty_instances_) {
        rebuild_instance(id);
    }
    dirty_instances_.clear();
    if (ground_dirty_) {
        rebuild_ground();
        ground_dirty_ = false;
    }
    if (collision_dirty_ && gizmo_.dragging() == moteur::GizmoHandle::None && !painting_) {
        rebuild_collision();
    }
    finish_navigation();
    navigation_timer_ += dt;
    if (navigation_wanted_ && !navigation_running_ && navigation_timer_ >= kNavigationPause && !collision_dirty_) {
        start_navigation();
    }
    ensure_thumbnails();

    // The file changed by someone else?
    check_timer_ += dt;
    if (!key_.empty() && check_timer_ > 1.0) {
        check_timer_ = 0.0;
        if (moteur::file_time(level_path(key_)) != file_time_) {
            external_change_ = true;
        }
    }
}

// --- Rendering --------------------------------------------------------------------------------

void LevelEditor::render(moteur::Renderer& renderer, double alpha) {
    ++frames_;
    if (trial_) {
        trial_->render(renderer, alpha);
        if (ImGui::GetCurrentContext() != nullptr) {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 10.0f, viewport->WorkPos.y + 10.0f),
                                    ImGuiCond_Always, ImVec2(1.0f, 0.0f));
            ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Trying the level", nullptr, ImGuiWindowFlags_NoMove)) {
                if (ImGui::Button("Back to the editor (Esc)")) {
                    end_trial_ = true;
                }
                ImGui::Separator();
                trial_->draw_controls();
            }
            ImGui::End();
        }
        return;
    }
    if (!options_.capture_path.empty() && !capture_done_ && frames_ > 30 && !navigation_running_ && !options_.self_test) {
        renderer.request_capture(options_.capture_path);
        capture_done_ = true;
    }
    camera_.set_viewport({static_cast<float>(renderer.width()), static_cast<float>(renderer.height())});
    moteur::MeshRenderer& meshes = renderer.meshes();
    meshes.set_camera(camera_.view_projection(), camera_.position());
    renderer.billboards().set_camera(camera_);
    const moteur::LevelLighting& lighting = level_.document().lighting;
    moteur::apply_lighting(renderer, night_ ? moteur::night_lighting(lighting) : lighting, &*sky_);
    moteur::CollectOptions collect;
    collect.view = camera_.frustum();
    collect.light_focus = camera_.target();
    world_.submit(renderer, static_cast<float>(alpha), collect);

    // What the pointer is over (the id pass, a frame late): outlined; a click waits for it.
    const glm::vec2 pointer = app_.input().pointer();
    renderer.request_pick(pointer);
    const std::uint32_t hovered = renderer.picked_id();
    renderer.set_outline(tool_ == Tool::Select && hovered_handle_ == moteur::GizmoHandle::None ? hovered : 0u);
    if (pending_pick_) {
        // The answer for (about) the clicked pixel, or after a few frames whatever is there.
        if (glm::length(renderer.picked_pixel() - *pending_pick_) < 2.0f || ++pending_pick_frames_ > 4) {
            std::uint32_t id = 0;
            const entt::entity entity = world_.entity_of_pick(renderer.picked_id());
            if (entity != entt::null) {
                if (const moteur::LevelInstanceId* instance = world_.registry().try_get<moteur::LevelInstanceId>(entity)) {
                    id = instance->id;
                }
            }
            select(id, pick_add_, pick_toggle_);
            pending_pick_.reset();
        }
    }

    draw_overlays(renderer, camera_);
    if (ImGui::GetCurrentContext() != nullptr) {
        draw_gizmo(camera_);
        draw_panels();
        draw_popups();
        // An asset dragged from the library and let go over the view: placed there.
        const ImGuiPayload* payload = ImGui::GetDragDropPayload();
        if (payload != nullptr && payload->IsDataType(kPayload)) {
            dragged_asset_.assign(static_cast<const char*>(payload->Data), static_cast<std::size_t>(payload->DataSize));
        }
        if (!dragged_asset_.empty() && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) && pointer.x >= 0.0f) {
                place(dragged_asset_, drop_point(pointer));
                tool_ = Tool::Select;
            }
            dragged_asset_.clear();
        }
    }
}

void LevelEditor::draw_overlays(moteur::Renderer& renderer, const moteur::Camera3D& camera) {
    moteur::DebugLineBuffer& lines = renderer.debug_lines().lines();
    const moteur::LevelDocument& document = level_.document();
    // The grid around the view's target, on the ground's level.
    const float ground = document.ground.width > 0 ? document.ground.level : 0.0f;
    if (show_grid_) {
        const float step = std::max(snap_step(), 0.25f);
        const float span = std::clamp(camera.visible_height(), 8.0f, 120.0f);
        const int count = std::min(static_cast<int>(span / step), 160);
        const glm::vec3 centre(moteur::snap_value(camera.target().x, step), ground, moteur::snap_value(camera.target().z, step));
        const glm::vec4 color(0.5f, 0.55f, 0.6f, 0.25f);
        for (int i = -count; i <= count; ++i) {
            const float d = static_cast<float>(i) * step;
            lines.line(centre + glm::vec3(d, 0.01f, -span), centre + glm::vec3(d, 0.01f, span), color);
            lines.line(centre + glm::vec3(-span, 0.01f, d), centre + glm::vec3(span, 0.01f, d), color);
        }
    }
    // The painted ground's grid while painting.
    if ((tool_ == Tool::Paint || tool_ == Tool::Erase) && document.ground.cell_size > 0.0f) {
        const moteur::LevelGround& g = document.ground;
        const glm::vec4 color(0.95f, 0.75f, 0.3f, 0.6f);
        for (int x = 0; x <= g.width; ++x) {
            const float px = g.origin.x + static_cast<float>(x) * g.cell_size;
            lines.line({px, g.level + 0.05f, g.origin.y}, {px, g.level + 0.05f, g.origin.y + static_cast<float>(g.height) * g.cell_size}, color);
        }
        for (int z = 0; z <= g.height; ++z) {
            const float pz = g.origin.y + static_cast<float>(z) * g.cell_size;
            lines.line({g.origin.x, g.level + 0.05f, pz}, {g.origin.x + static_cast<float>(g.width) * g.cell_size, g.level + 0.05f, pz}, color);
        }
        if (pointer_point_) {
            const glm::ivec2 cell = moteur::ground_cell(g, *pointer_point_);
            const int reach = brush_size_ - 1;
            const glm::vec3 low(g.origin.x + static_cast<float>(cell.x - reach) * g.cell_size, g.level,
                                g.origin.y + static_cast<float>(cell.y - reach) * g.cell_size);
            const float size = static_cast<float>(2 * reach + 1) * g.cell_size;
            moteur::Aabb brush;
            brush.add(low);
            brush.add(low + glm::vec3(size, 0.1f, size));
            lines.box(brush, tool_ == Tool::Erase ? glm::vec4(1.0f, 0.35f, 0.3f, 1.0f) : glm::vec4(0.4f, 1.0f, 0.4f, 1.0f), true);
        }
    }
    // The selection.
    for (const std::uint32_t id : selection_) {
        if (const auto box = instance_bounds_.find(id); box != instance_bounds_.end()) {
            lines.box(box->second, {1.0f, 0.8f, 0.25f, 1.0f}, true);
        }
    }
    // The points.
    for (const moteur::LevelPoint& point : document.points) {
        const glm::vec4 color(0.3f, 0.9f, 1.0f, 1.0f);
        lines.line(point.position, point.position + glm::vec3(0.0f, 1.5f, 0.0f), color, true);
        lines.circle(point.position + glm::vec3(0.0f, 0.02f, 0.0f), {0.0f, 1.0f, 0.0f}, 0.4f, color, 16, true);
    }
    // What is dragged from the library: where it would go.
    if (!dragged_asset_.empty() && pointer_point_) {
        moteur::Aabb spot;
        spot.add(*pointer_point_ - glm::vec3(0.5f, 0.0f, 0.5f));
        spot.add(*pointer_point_ + glm::vec3(0.5f, 1.0f, 0.5f));
        lines.box(spot, {0.4f, 1.0f, 0.5f, 1.0f}, true);
    }
    // The navigation and the surfaces nothing walks on, near the view.
    moteur::Aabb near;
    const float reach = std::clamp(camera.visible_height() * 1.5f, 20.0f, 200.0f);
    near.add(camera.target() - glm::vec3(reach, 50.0f, reach));
    near.add(camera.target() + glm::vec3(reach, 50.0f, reach));
    if (show_navigation_ && !navigation_.empty()) {
        moteur::draw_nav_mesh(lines, navigation_.front(), near);
    }
    if (show_collision_ && scene_.built()) {
        int drawn = 0;
        for (const moteur::CollisionTriangle& t : scene_.triangles()) {
            const bool inside = t.a.x >= near.min.x && t.a.x <= near.max.x && t.a.z >= near.min.z && t.a.z <= near.max.z;
            if (t.kind == moteur::SurfaceKind::Walkable || !inside || ++drawn > 20000) {
                continue;
            }
            const glm::vec4 color = t.kind == moteur::SurfaceKind::Wall ? glm::vec4(1.0f, 0.3f, 0.25f, 0.6f)
                                                                         : glm::vec4(1.0f, 0.6f, 0.2f, 0.6f);
            lines.line(t.a, t.b, color);
            lines.line(t.b, t.c, color);
            lines.line(t.c, t.a, color);
        }
    }
    // Names of the points: near the view's target only (a level has hundreds).
    if (ImGui::GetCurrentContext() != nullptr && camera.visible_height() < 90.0f) {
        ImDrawList* draw = ImGui::GetBackgroundDrawList();
        const float radius = camera.visible_height() * 0.6f;
        for (const moteur::LevelPoint& point : document.points) {
            const glm::vec3 offset = point.position - camera.target();
            if (offset.x * offset.x + offset.z * offset.z > radius * radius) {
                continue;
            }
            if (const std::optional<glm::vec2> pixel = camera.world_to_screen(point.position + glm::vec3(0.0f, 1.6f, 0.0f))) {
                const ImVec2 p = to_imgui(*pixel);
                draw->AddText(ImVec2(p.x + 1.0f, p.y + 1.0f), IM_COL32(0, 0, 0, 200), point.name.c_str());
                draw->AddText(p, IM_COL32(80, 230, 255, 255), point.name.c_str());
            }
        }
    }
}

void LevelEditor::draw_gizmo(const moteur::Camera3D& camera) {
    if (selection_.empty() || tool_ != Tool::Select) {
        return;
    }
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    const moteur::GizmoHandle hot = gizmo_.dragging() != moteur::GizmoHandle::None ? gizmo_.dragging() : hovered_handle_;
    for (const moteur::GizmoLine& line : gizmo_.shape(camera, selection_centre(), gizmo_mode_)) {
        draw->AddLine(to_imgui(line.a), to_imgui(line.b), handle_color(line.handle, line.handle == hot), 2.5f);
    }
}

// --- Panels -----------------------------------------------------------------------------------

void LevelEditor::draw_controls() {
    if (trial_) {
        ImGui::TextUnformatted("Trying the level: Esc returns to the editor.");
        return;
    }
    ImGui::TextWrapped("Level editor: library on the left, inspector on the right. Right button: turn the view; "
                       "middle button or WASD: move it; wheel: zoom; F5: try the level.");
    if (ImGui::Button("Quit the editor")) {
        if (modified()) confirm_quit_ = true;
        else stop_requested_ = true;
    }
}

void LevelEditor::draw_panels() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    // The library, on the left.
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 8.0f, viewport->WorkPos.y + 8.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(kLibraryWidth, viewport->WorkSize.y - 16.0f), ImGuiCond_FirstUseEver);
    const std::string title = "Level: " + (key_.empty() ? std::string("(untitled)") : key_) + (modified() ? " *" : "") + "###level_editor";
    if (ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_MenuBar)) {
        draw_menu();
        if (recovery_found_) {
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "An unsaved copy of this level exists.");
            if (ImGui::Button("Open the copy")) {
                try {
                    moteur::DataIssues issues;
                    moteur::Assets& assets = app_.assets();
                    adopt(moteur::LevelDocument::parse(moteur::read_text_file(recovery_path()), key_,
                                                       [&assets](const nlohmann::json& r) { return assets.resolve_reference(r); }, issues));
                    saved_revision_ = ~0ull;
                } catch (const std::exception& e) {
                    status_ = e.what();
                    status_error_ = true;
                }
                recovery_found_ = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete the copy")) {
                std::error_code ignored;
                std::filesystem::remove(utf8_path(recovery_path()), ignored);
                recovery_found_ = false;
            }
            ImGui::Separator();
        }
        if (external_change_) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "The file changed on disk.");
            if (ImGui::Button("Reload the file")) {
                if (modified()) {
                    pending_open_ = key_;
                    confirm_open_ = true;
                } else {
                    open(key_);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Keep my version")) {
                file_time_ = moteur::file_time(level_path(key_));
                external_change_ = false;
            }
            ImGui::Separator();
        }
        draw_toolbar();
        ImGui::Separator();
        draw_library();
    }
    ImGui::End();

    // The inspector, on the right.
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - kInspectorWidth - 8.0f, viewport->WorkPos.y + 8.0f),
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(kInspectorWidth, viewport->WorkSize.y - 16.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Inspector###level_inspector")) {
        draw_inspector();
        ImGui::Separator();
        draw_level_settings();
    }
    ImGui::End();
}

void LevelEditor::draw_menu() {
    if (!ImGui::BeginMenuBar()) {
        return;
    }
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New", "Ctrl+N")) {
            open_new_ = true;
        }
        if (ImGui::BeginMenu("Open")) {
            for (const std::string& name : level_names_) {
                if (ImGui::MenuItem(name.c_str(), nullptr, name == key_)) {
                    if (modified()) {
                        pending_open_ = name;
                        confirm_open_ = true;
                    } else {
                        open(name);
                    }
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Save", "Ctrl+S")) {
            save(false);
        }
        if (ImGui::MenuItem("Save as...")) {
            open_save_as_ = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Rescan the assets")) {
            scan_library();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        const std::string undo_label = "Undo " + level_.undo_name();
        const std::string redo_label = "Redo " + level_.redo_name();
        if (ImGui::MenuItem(undo_label.c_str(), "Ctrl+Z", false, level_.can_undo())) {
            undo();
        }
        if (ImGui::MenuItem(redo_label.c_str(), "Ctrl+Y", false, level_.can_redo())) {
            redo();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, !selection_.empty())) {
            duplicate_selection();
        }
        if (ImGui::MenuItem("Delete", "Del", false, !selection_.empty())) {
            delete_selection();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Night (N)", nullptr, &night_);
        ImGui::MenuItem("Navigation mesh", nullptr, &show_navigation_);
        ImGui::MenuItem("Walls and obstacles", nullptr, &show_collision_);
        ImGui::MenuItem("Grid", nullptr, &show_grid_);
        if (ImGui::MenuItem("Frame the level", "Home")) {
            frame_level();
        }
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Try (F5)")) {
        trial_request_ = 1;
    }
    ImGui::EndMenuBar();
}

void LevelEditor::draw_toolbar() {
    const auto tool_button = [this](const char* label, Tool tool, const char* tip) {
        const bool active = tool_ == tool;
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        if (ImGui::Button(label)) {
            tool_ = tool;
        }
        if (active) {
            ImGui::PopStyleColor();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tip);
        }
        ImGui::SameLine();
    };
    tool_button("Select", Tool::Select, "Select and move with the handles (Q)");
    tool_button("Place", Tool::Place, "Place the asset chosen in the library at each click");
    tool_button("Paint", Tool::Paint, "Paint the ground (B); [ and ] change the brush");
    tool_button("Erase", Tool::Erase, "Erase the ground (X)");
    tool_button("Point", Tool::Point, "Put a named point (P)");
    ImGui::NewLine();
    if (tool_ == Tool::Select) {
        int mode = static_cast<int>(gizmo_mode_);
        ImGui::RadioButton("Move (T)", &mode, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Turn (E)", &mode, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Scale (R)", &mode, 2);
        gizmo_mode_ = static_cast<moteur::GizmoMode>(mode);
    }
    ImGui::Checkbox("Snap (G)", &snap_);
    ImGui::SameLine();
    ImGui::Checkbox("On surfaces", &surface_snap_);
    ImGui::SetNextItemWidth(90.0f);
    ImGui::DragFloat("Grid (m)", &grid_step_, 0.05f, 0.05f, 16.0f, "%.2f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f);
    ImGui::DragFloat("Angle", &angle_step_, 1.0f, 1.0f, 90.0f, "%.0f");
    if (tool_ == Tool::Point) {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%s", point_name_.c_str());
        if (ImGui::InputText("Point name", buffer, sizeof(buffer))) {
            point_name_ = buffer;
        }
    }
    if (tool_ == Tool::Paint || tool_ == Tool::Erase) {
        const moteur::LevelGround& ground = level_.document().ground;
        if (ground.prefabs.empty()) {
            ImGui::TextWrapped("No ground prefab yet: add one (a ground tile of the library) with 'Use as ground'.");
        }
        for (std::size_t i = 0; i < ground.prefabs.size(); ++i) {
            const std::string label = ground.prefabs[i].substr(ground.prefabs[i].find_last_of('/') + 1);
            if (ImGui::RadioButton(label.c_str(), ground_brush_ == i + 1)) {
                ground_brush_ = static_cast<std::uint8_t>(i + 1);
            }
        }
        ImGui::SliderInt("Brush", &brush_size_, 1, 6);
    }
    if (!status_.empty()) {
        ImGui::TextColored(status_error_ ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f) : ImVec4(0.6f, 0.9f, 0.6f, 1.0f), "%s", status_.c_str());
    }
    ImGui::TextDisabled("%zu instances, %zu points; view %.0f ms; navigation %s (%.0f ms)", level_.document().instances.size(),
                        level_.document().points.size(), build_ms_,
                        navigation_running_ ? "building" : (navigation_.empty() ? "none" : "ready"), navigation_ms_);
    if (instance_issues_ > 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "%d problem(s): %s", instance_issues_, last_issue_.c_str());
    }
}

void LevelEditor::draw_library() {
    static const char* const kFilters[] = {"All", "Props", "Ground", "Lights", "Kit pieces", "Models without prefab"};
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##search", "Search the assets", search_, sizeof(search_));
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::Combo("##filter", &library_filter_, kFilters, IM_ARRAYSIZE(kFilters));
    const std::string search = lower(search_);
    const float cell = 104.0f;
    const int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cell));
    ImGui::BeginChild("##assets", ImVec2(0.0f, 0.0f));
    std::string folder;
    int column = 0;
    for (const LibraryItem& item : library_) {
        const bool kit = !item.family.empty();
        const bool keep = library_filter_ == 0 || (library_filter_ == 1 && item.prefab && !item.ground && !item.light && !kit) ||
                          (library_filter_ == 2 && item.ground) || (library_filter_ == 3 && item.light) ||
                          (library_filter_ == 4 && kit) || (library_filter_ == 5 && !item.prefab);
        if (!keep || (!search.empty() && lower(item.key).find(search) == std::string::npos)) {
            continue;
        }
        const std::string group = kit ? item.family : item.folder;
        if (group != folder) {
            folder = group;
            column = 0;
            ImGui::SeparatorText(folder.c_str());
        }
        if (column > 0) {
            ImGui::SameLine();
        }
        ImGui::BeginGroup();
        ImGui::PushID(item.key.c_str());
        const moteur::Texture* thumbnail = thumbnails_->find(item.key, item.fingerprint);
        const bool chosen = chosen_ == item.key;
        if (chosen) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.6f, 0.2f, 0.8f));
        }
        bool clicked;
        if (thumbnail != nullptr) {
            clicked = ImGui::ImageButton("##thumb", texture_ref(*thumbnail), ImVec2(cell - 16.0f, cell - 16.0f));
        } else {
            if (ImGui::IsRectVisible(ImVec2(cell, cell)) && thumbnails_failed_.count(item.key) == 0) {
                thumbnails_wanted_.insert(item.key);  // drawn between two frames
            }
            clicked = ImGui::Button(item.prefab ? "..." : "model", ImVec2(cell - 8.0f, cell - 8.0f));
        }
        if (chosen) {
            ImGui::PopStyleColor();
        }
        if (clicked && item.prefab) {
            chosen_ = item.key;
            tool_ = Tool::Place;
        }
        if (item.prefab && ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload(kPayload, item.key.data(), item.key.size());
            ImGui::Text("Place %s", item.name.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s%s%s", item.key.c_str(), item.grid > 0.0f ? "\nkit grid " : "",
                              item.grid > 0.0f ? std::to_string(item.grid).substr(0, 4).c_str() : "");
        }
        if (ImGui::BeginPopupContextItem("##menu")) {
            if (item.prefab && ImGui::MenuItem("Use as ground")) {
                moteur::LevelGround ground = level_.document().ground;
                if (std::find(ground.prefabs.begin(), ground.prefabs.end(), item.key) == ground.prefabs.end() && ground.prefabs.size() < 255) {
                    ground.prefabs.push_back(item.key);
                    level_.begin_action("ground prefab");
                    level_.set_ground(ground);
                    level_.end_action();
                }
                ground_brush_ = static_cast<std::uint8_t>(std::find(ground.prefabs.begin(), ground.prefabs.end(), item.key) - ground.prefabs.begin() + 1);
                tool_ = Tool::Paint;
            }
            if (!item.prefab && ImGui::MenuItem("Make its prefab")) {
                make_prefab_for_model(item);
            }
            ImGui::EndPopup();
        }
        ImGui::SetNextItemWidth(cell - 8.0f);
        const std::string name = item.name.size() > 14 ? item.name.substr(0, 13) + "~" : item.name;
        ImGui::TextUnformatted(name.c_str());
        ImGui::PopID();
        ImGui::EndGroup();
        column = (column + 1) % columns;
    }
    ImGui::EndChild();
}

void LevelEditor::draw_inspector() {
    if (selection_.empty()) {
        ImGui::TextDisabled("Nothing selected: click an instance.");
        return;
    }
    if (selection_.size() > 1) {
        ImGui::Text("%zu instances selected", selection_.size());
        if (ImGui::Button("Duplicate")) {
            duplicate_selection();
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            delete_selection();
        }
        return;
    }
    const moteur::LevelInstance* found = level_.document().instance(selection_.front());
    if (found == nullptr) {
        return;
    }
    moteur::LevelInstance instance = *found;
    ImGui::Text("Instance %u", instance.id);
    ImGui::TextWrapped("%s", instance.prefab.c_str());
    bool changed = false;
    // Where it is.
    glm::vec3 position = instance.transform.position;
    if (ImGui::DragFloat3("Position", &position.x, 0.05f)) {
        instance.transform.position = position;
        changed = true;
    }
    track_item("move");
    const glm::vec3 euler = glm::degrees(glm::eulerAngles(instance.transform.rotation));
    glm::vec3 angles = euler;
    if (ImGui::DragFloat3("Rotation", &angles.x, 0.5f)) {
        instance.transform.rotation = glm::normalize(glm::quat(glm::radians(angles)));
        changed = true;
    }
    track_item("turn");
    glm::vec3 scale = instance.transform.scale;
    if (ImGui::DragFloat3("Scale", &scale.x, 0.01f, 0.01f, 100.0f)) {
        instance.transform.scale = glm::max(scale, glm::vec3(0.01f));
        changed = true;
    }
    track_item("scale");
    if (ImGui::BeginCombo("Layer", instance.layer.c_str())) {
        for (const moteur::LevelLayer& layer : level_.document().layers) {
            if (ImGui::Selectable(layer.name.c_str(), layer.name == instance.layer)) {
                instance.layer = layer.name;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    // Its prefab's fields, the instance's overrides over them (a lamp's light).
    moteur::Assets& assets = app_.assets();
    try {
        const moteur::Asset<moteur::Prefab> prefab = assets.prefab(instance.prefab);
        const nlohmann::json& merged = prefab->merged();
        for (const moteur::Prefab::Entity& entity : prefab->entities()) {
            for (const moteur::Prefab::Component& component : entity.components) {
                if (component.type == nullptr || component.type->type == nullptr || component.type->name == "transform" ||
                    component.type->tag) {
                    continue;
                }
                const std::string label = (entity.name.empty() ? entity.path : entity.name) + " > " + component.type->name;
                if (!ImGui::TreeNode(label.c_str())) {
                    continue;
                }
                nlohmann::json base = merged.contains(entity.path) && merged[entity.path].contains(component.type->name)
                                          ? merged[entity.path][component.type->name]
                                          : nlohmann::json::object();
                nlohmann::json value = base;
                if (instance.overrides.contains(entity.path) && instance.overrides[entity.path].contains(component.type->name)) {
                    moteur::merge_fields(value, instance.overrides[entity.path][component.type->name]);
                }
                moteur::ScratchObject object(*component.type->type);
                moteur::DataIssues issues;
                moteur::read_json(value, object.get(), *component.type->type, label, issues, assets.read_options(moteur::kFieldData));
                ImGui::PushID(label.c_str());
                if (moteur::inspect_reflected(object.get(), *component.type->type)) {
                    const nlohmann::json written = moteur::write_json(object.get(), *component.type->type, assets.write_options(moteur::kFieldData));
                    nlohmann::json& override_value = instance.overrides[entity.path][component.type->name];
                    if (!override_value.is_object()) {
                        override_value = nlohmann::json::object();
                    }
                    moteur::merge_fields(override_value, changed_fields(value, written));
                    prune_overrides(instance.overrides, merged);
                    changed = true;
                }
                track_item("edit a field");
                ImGui::PopID();
                ImGui::TreePop();
            }
        }
    } catch (const std::exception& e) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", e.what());
    }
    // The game's data of the instance (LevelInstance::properties), as JSON.
    if (ImGui::TreeNode("Properties (the game's data)")) {
        if (properties_of_ != instance.id) {
            const std::string text = instance.properties.dump(2);
            std::fill(properties_text_.begin(), properties_text_.end(), '\0');
            std::copy_n(text.begin(), std::min(text.size(), properties_text_.size() - 1), properties_text_.begin());
            properties_of_ = instance.id;
            properties_error_.clear();
        }
        ImGui::InputTextMultiline("##properties", properties_text_.data(), properties_text_.size(), ImVec2(-1.0f, 120.0f));
        if (ImGui::Button("Apply")) {
            try {
                const nlohmann::json parsed = nlohmann::json::parse(std::string(properties_text_.data()));
                if (!parsed.is_object()) {
                    throw std::runtime_error("expected an object");
                }
                instance.properties = parsed;
                properties_error_.clear();
                changed = true;
            } catch (const std::exception& e) {
                properties_error_ = e.what();
            }
        }
        if (!properties_error_.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", properties_error_.c_str());
        }
        ImGui::TreePop();
    }
    if (!instance.overrides.empty() && ImGui::Button("Back to the prefab's values")) {
        instance.overrides = nlohmann::json::object();
        changed = true;
    }
    if (changed) {
        if (!level_.in_action()) {
            level_.begin_action("edit");
            level_.set(instance);
            level_.end_action();
        } else {
            level_.set(instance);
        }
    }
}

void LevelEditor::draw_level_settings() {
    if (!ImGui::CollapsingHeader("Level", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    moteur::LevelSettings settings = level_.settings();
    bool changed = false;
    char description[256];
    std::snprintf(description, sizeof(description), "%s", settings.description.c_str());
    if (ImGui::InputText("Description", description, sizeof(description))) {
        settings.description = description;
        changed = true;
    }
    track_item("description");
    ImGui::SeparatorText("Lighting");
    ImGui::Checkbox("Night view (N)", &night_);
    if (moteur::inspect_reflected(&settings.lighting, moteur::type_info<moteur::LevelLighting>())) {
        changed = true;
    }
    track_item("lighting");
    ImGui::SeparatorText("Layers");
    for (std::size_t i = 0; i < settings.layers.size(); ++i) {
        moteur::LevelLayer& layer = settings.layers[i];
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Checkbox("##visible", &layer.visible)) {
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Selectable(layer.name.c_str(), layer.name == layer_)) {
            layer_ = layer.name;
        }
        ImGui::PopID();
    }
    ImGui::SeparatorText("Points");
    for (const moteur::LevelPoint& point : level_.document().points) {
        ImGui::PushID(point.name.c_str());
        if (ImGui::SmallButton("go")) {
            camera_.set_target(point.position);
        }
        ImGui::SameLine();
        ImGui::Text("%s (%.1f, %.1f, %.1f)", point.name.c_str(), point.position.x, point.position.y, point.position.z);
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            level_.begin_action("remove point");
            level_.remove_point(point.name);
            level_.end_action();
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    ImGui::SeparatorText("Navigation");
    if (ImGui::DragFloat("Chunk size (m)", &settings.chunk_size, 1.0f, 8.0f, 256.0f, "%.0f")) {
        changed = true;
    }
    track_item("chunk size");
    if (moteur::inspect_reflected(&settings.agents.front(), moteur::type_info<moteur::NavAgent>())) {
        changed = true;
    }
    track_item("navigation agent");
    if (!navigation_.empty()) {
        ImGui::TextDisabled("%d tiles, %d polygons", navigation_.front().tile_count(), navigation_.front().polygon_count());
    }
    if (changed) {
        if (!level_.in_action()) {
            level_.begin_action("level settings");
            level_.set_settings(settings);
            level_.end_action();
        } else {
            level_.set_settings(settings);
        }
    }
}

void LevelEditor::draw_popups() {
    const auto name_from_buffer = [this]() {
        std::string name = name_buffer_;
        if (!ends_with(name, ".level.json")) {
            name += ".level.json";
        }
        return "levels/" + name;
    };
    if (open_new_) {
        ImGui::OpenPopup("New level");
        open_new_ = false;
    }
    if (ImGui::BeginPopupModal("New level", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(modified() ? "The changes not saved will be lost." : "An empty level, with a start point.");
        if (ImGui::Button("Create")) {
            moteur::LevelDocument fresh;
            moteur::LevelPoint start;
            start.name = "start";
            fresh.points.push_back(start);
            adopt(std::move(fresh));
            key_.clear();
            saved_revision_ = level_.revision();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (open_save_as_) {
        ImGui::OpenPopup("Save the level as");
        open_save_as_ = false;
    }
    if (ImGui::BeginPopupModal("Save the level as", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Name", name_buffer_, sizeof(name_buffer_));
        ImGui::TextDisabled("%s", level_path(name_from_buffer()).c_str());
        if (ImGui::Button("Save")) {
            key_ = name_from_buffer();
            file_time_ = moteur::file_time(level_path(key_));
            save(true);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (confirm_overwrite_) {
        ImGui::OpenPopup("Overwrite the file?");
        confirm_overwrite_ = false;
    }
    if (ImGui::BeginPopupModal("Overwrite the file?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("The file changed on disk since it was opened.");
        if (ImGui::Button("Overwrite")) {
            save(true);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (confirm_open_) {
        ImGui::OpenPopup("Lose the changes?");
        confirm_open_ = false;
    }
    if (ImGui::BeginPopupModal("Lose the changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Open %s without saving this level?", pending_open_.c_str());
        if (ImGui::Button("Open")) {
            open(pending_open_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (confirm_quit_) {
        ImGui::OpenPopup("Quit without saving?");
        confirm_quit_ = false;
    }
    if (ImGui::BeginPopupModal("Quit without saving?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("A copy is kept, offered when the level opens again.");
        if (ImGui::Button("Save and quit")) {
            if (save(false)) {
                stop_requested_ = true;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Quit")) {
            stop_requested_ = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// --- "Essayer" --------------------------------------------------------------------------------

void LevelEditor::start_trial(bool at_pointer) {
    try {
        const auto start = std::chrono::steady_clock::now();
        moteur::LevelBakeStats stats;
        moteur::BakedLevel baked = moteur::bake_level(level_.document(), sources_, &stats);
        // The slice itself, on this level: its hero, its creatures from the level's spawns.
        Demo3D::Options options;
        options.hero = true;
        options.level = key_.empty() ? std::string("levels/untitled.level.json") : key_;
        options.baked_level = std::make_shared<moteur::BakedLevel>(std::move(baked));
        if (at_pointer && pointer_point_) {
            options.start = *pointer_point_;
        }
        options.night = night_;
        trial_ = std::make_unique<Demo3D>(app_, options, false);
        status_ = "Trying the level (baked in " + std::to_string(static_cast<int>(ms_since(start))) + " ms)";
        status_error_ = false;
    } catch (const std::exception& e) {
        status_ = std::string("The level cannot be tried: ") + e.what();
        status_error_ = true;
    }
}

// --- Self-test --------------------------------------------------------------------------------

void LevelEditor::check(bool condition, const char* what) {
    if (!condition) {
        ++test_failures_;
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Level editor self-test: FAILED: %s", what);
    } else {
        SDL_Log("Level editor self-test: ok: %s", what);
    }
}

void LevelEditor::run_self_test() {
    constexpr const char* kFloor = "prefabs/blockout/floor_8m.prefab.json";
    constexpr const char* kStep = "prefabs/blockout/step.prefab.json";
    constexpr const char* kLamp = "models/props/lampadaire_gothique.prefab.json";
    if (test_wait_ > 0) {
        --test_wait_;
        return;
    }
    switch (test_step_) {
        case 0: {  // a new level: the ground painted, three by three tiles of 8 m
            moteur::LevelDocument fresh;
            moteur::LevelPoint start;
            start.name = "start";
            start.position = {12.0f, 0.0f, 12.0f};
            fresh.points.push_back(start);
            adopt(std::move(fresh));
            key_ = "levels/editeur_essai.level.json";
            moteur::LevelGround ground;
            ground.prefabs = {kFloor};
            level_.set_ground(ground);
            level_.begin_action("paint");
            for (int z = 0; z < 3; ++z) {
                for (int x = 0; x < 3; ++x) {
                    level_.paint_ground({x, z}, 1);
                }
            }
            level_.end_action();
            check(level_.document().ground.width == 3 && level_.document().ground.height == 3, "the ground is painted");
            // A second height: a step and a raised platform.
            moteur::LevelInstance step;
            step.prefab = kStep;
            step.layer = "default";
            step.transform.position = {16.0f, 0.0f, 4.0f};
            level_.add(step);
            test_wait_ = 2;
            ++test_step_;
            break;
        }
        case 1: {  // the lamp from the ARPG's assets: its light is in the world
            const bool have_lamp = app_.assets().exists(kLamp);
            place(have_lamp ? kLamp : kStep, {6.0f, 0.0f, 6.0f});
            test_lamp_ = selection_.empty() ? 0 : selection_.front();
            test_wait_ = 2;
            ++test_step_;
            break;
        }
        case 2: {
            check(roots_.count(test_lamp_) != 0, "the lamp is instantiated");
            int lights = 0;
            for (const auto& [entity, light] : world_.registry().view<moteur::LightSource>().each()) {
                static_cast<void>(entity);
                static_cast<void>(light);
                ++lights;
            }
            check(!app_.assets().exists(kLamp) || lights > 0, "its light is in the world");
            // Moved with the handles: the gizmo's drag along x, snapped.
            camera_.set_viewport({static_cast<float>(app_.renderer().width()), static_cast<float>(app_.renderer().height())});
            const glm::vec3 origin = selection_centre();
            const float length = gizmo_.world_size(camera_, origin);
            const std::optional<glm::vec2> grab = camera_.world_to_screen(origin + glm::vec3(length * 0.9f, 0.0f, 0.0f));
            const std::optional<glm::vec2> to = camera_.world_to_screen(origin + glm::vec3(length * 0.9f + 2.0f, 0.0f, 0.0f));
            const bool began = grab && to && gizmo_.begin(camera_, origin, moteur::GizmoMode::Translate, moteur::GizmoHandle::X, *grab);
            check(began, "the handle is grabbed");
            if (began) {
                drag_start_ = {{test_lamp_, level_.document().instance(test_lamp_)->transform}};
                drag_pivot_ = origin;
                level_.begin_action("move");
                apply_gizmo(gizmo_.drag(camera_, *to, {.translation = 0.5f}));
                level_.end_action();
                gizmo_.end();
                drag_start_.clear();
            }
            const moteur::LevelInstance* lamp = level_.document().instance(test_lamp_);
            check(lamp != nullptr && std::fabs(lamp->transform.position.x - 8.0f) < 1e-3f, "the lamp moved 2 m along x");
            // Undo, redo.
            undo();
            check(std::fabs(level_.document().instance(test_lamp_)->transform.position.x - 6.0f) < 1e-3f, "undo puts it back");
            redo();
            check(std::fabs(level_.document().instance(test_lamp_)->transform.position.x - 8.0f) < 1e-3f, "redo moves it again");
            // Its light's intensity overridden by the inspector's way (an override of the instance).
            moteur::LevelInstance changed = *level_.document().instance(test_lamp_);
            changed.overrides["2"]["light"]["intensity"] = 20.0;
            level_.set(changed);
            thumbnails_wanted_.insert(level_.document().instance(test_lamp_)->prefab);
            test_wait_ = 5;
            ++test_step_;
            break;
        }
        case 3: {  // the navigation, built again in the background
            if (navigation_running_ || navigation_wanted_) {
                if (++test_wait_frames_ < 600) {
                    return;
                }
            }
            check(!navigation_.empty() && navigation_.front().polygon_count() > 0, "the navigation mesh is built");
            const std::string& lamp = level_.document().instance(test_lamp_)->prefab;
            const auto item = std::find_if(library_.begin(), library_.end(), [&lamp](const LibraryItem& i) { return i.key == lamp; });
            check(item != library_.end() && thumbnails_->find(lamp, item->fingerprint) != nullptr, "the lamp has a thumbnail");
            if (!options_.capture_path.empty() && !capture_done_) {
                // A look at the editor: the lamp selected with its handles, at night, the navigation shown.
                selection_ = {test_lamp_};
                camera_.set_target(level_.document().instance(test_lamp_)->transform.position);
                camera_.set_visible_height(16.0f);
                night_ = true;
                app_.renderer().request_capture(options_.capture_path);
                capture_done_ = true;
                test_wait_ = 3;
                return;
            }
            // Saved, opened again: the same document.
            test_text_ = level_json().dump();
            check(save(true), "the level is saved");
            check(open(key_), "the level opens again");
            const std::string again = level_json().dump();
            check(again == test_text_, "the same level once opened again");
            if (again != test_text_) {  // both kept, to compare
                std::string ignored;
                moteur::write_file_atomic(levels_dir_ + "saved.json", nlohmann::json::parse(test_text_).dump(2), ignored);
                moteur::write_file_atomic(levels_dir_ + "opened.json", nlohmann::json::parse(again).dump(2), ignored);
            }
            check(!level_.can_undo(), "nothing to undo after opening");
            ++test_step_;
            test_wait_ = 2;
            break;
        }
        case 4: {  // baked as the preparation does, then tried
            try {
                moteur::LevelBakeStats stats;
                const moteur::BakedLevel baked = moteur::bake_level(level_.document(), sources_, &stats);
                check(stats.chunks > 0 && stats.nav_tiles > 0, "the level bakes (chunks and navigation)");
            } catch (const std::exception& e) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "bake: %s", e.what());
                check(false, "the level bakes");
            }
            start_trial(false);
            check(trial_ != nullptr, "'Essayer' starts");
            test_wait_ = 90;
            ++test_step_;
            break;
        }
        case 5: {
            check(trial_ != nullptr && trial_->hero_grounded(), "the hero stands on the level");
            end_trial_ = true;
            test_wait_ = 3;
            ++test_step_;
            break;
        }
        case 6: {
            check(trial_ == nullptr, "back in the editor");
            SDL_Log("Level editor self-test: %s (%d failure(s)); view built in %.0f ms, navigation %.0f ms, %d thumbnail(s)",
                    test_failures_ == 0 ? "passed" : "FAILED", test_failures_, build_ms_, navigation_ms_, thumbnails_drawn_);
            ++test_step_;
            if (standalone_) {
                app_.quit();
            } else {
                stop_requested_ = true;
            }
            break;
        }
        default:
            break;
    }
}
