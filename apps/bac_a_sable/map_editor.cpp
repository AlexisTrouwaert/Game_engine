#include "map_editor.hpp"

#include <imgui.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>

#include "moteur/color.hpp"
#include "moteur/debug_lines.hpp"
#include "moteur/debug_tools.hpp"
#include "moteur/file_io.hpp"
#include "moteur/mesh_renderer.hpp"
#include "moteur/paths.hpp"

#include "sandbox_data.hpp"
#include "world_test.hpp"

namespace {


constexpr int kBlock = 16;  // cells per side of a block of the view (built again together)
constexpr float kTopPitch = 89.0f;  // seen from above: Camera3D's steepest
const char* const kToolNames[] = {"Sélection", "Pinceau",   "Gomme",  "Rectangle", "Rectangle vide",
                                  "Remplir",   "Pipette",   "Points", "Objets",    "Connecteurs"};
const char* const kToolKeys[] = {"V", "B", "E", "R", "Maj+R", "F", "I", "P", "O", "C"};
const char* const kDirections[] = {"Nord", "Est", "Sud", "Ouest"};

glm::vec3 linear(float r, float g, float b) {
    return moteur::srgb_to_linear(glm::vec3(r, g, b));
}

moteur::Material surface(glm::vec3 color, float roughness, bool casts_shadow = true) {
    moteur::Material material;
    material.base_color = glm::vec4(color, 1.0f);
    material.roughness = roughness;
    material.casts_shadow = casts_shadow;
    return material;
}

moteur::Material glowing(glm::vec3 emissive) {
    moteur::Material material;
    material.base_color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    material.emissive = emissive;
    material.casts_shadow = false;
    return material;
}

moteur::Transform place(glm::vec3 position, glm::vec3 scale) {
    moteur::Transform transform;
    transform.position = position;
    transform.scale = scale;
    return transform;
}

// A color of its own for each name (points, tile types), the same at every run.
glm::vec3 name_color(const std::string& name) {
    std::uint32_t hash = 2166136261u;
    for (const char c : name) {
        hash = (hash ^ static_cast<unsigned char>(c)) * 16777619u;
    }
    const float hue = static_cast<float>(hash % 360u) / 60.0f;
    const float x = 1.0f - std::abs(std::fmod(hue, 2.0f) - 1.0f);
    const int sector = static_cast<int>(hue);
    const glm::vec3 colors[6] = {{1, x, 0}, {x, 1, 0}, {0, 1, x}, {0, x, 1}, {x, 0, 1}, {1, 0, x}};
    return colors[sector % 6];
}

std::filesystem::path utf8_path(const std::string& text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

std::vector<std::string> json_files(const std::string& directory) {
    std::vector<std::string> names;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(utf8_path(directory), error)) {
        if (entry.path().extension() == ".json") {
            const std::u8string stem = entry.path().stem().u8string();
            names.emplace_back(stem.begin(), stem.end());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool valid_name(const std::string& name) {
    if (name.empty()) {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}

// The footprint of an object on the ground (to pick it with the pointer), metres.
float object_radius(const moteur::MapObject& object) {
    const nlohmann::json& p = object.props;
    float r = 0.45f;
    if (object.type == "arbre") {
        r = p.value("couronne", 1.2f) * 0.5f;
    } else if (object.type == "rocher" && p.contains("echelle")) {
        r = p["echelle"].at(0).get<float>() * 0.5f;
    } else if (object.type == "caisse" || object.type == "tonneau") {
        r = p.value("arete", 0.6f) * 0.6f;
    }
    return std::max(0.3f, r * object.scale);
}

glm::vec2 edge_point(const moteur::MapConnector& connector) {
    const glm::vec2 c = glm::vec2(connector.cell) + 0.5f;
    switch (connector.direction) {
        case moteur::MapConnector::Direction::North: return {c.x, c.y - 0.5f};
        case moteur::MapConnector::Direction::East: return {c.x + 0.5f, c.y};
        case moteur::MapConnector::Direction::South: return {c.x, c.y + 0.5f};
        case moteur::MapConnector::Direction::West: return {c.x - 0.5f, c.y};
    }
    return c;
}

void cell_box(moteur::DebugLineBuffer& lines, glm::ivec2 a, glm::ivec2 b, float y, glm::vec4 color, bool on_top) {
    const glm::vec2 lo = glm::vec2(glm::min(a, b));
    const glm::vec2 hi = glm::vec2(glm::max(a, b)) + 1.0f;
    lines.line({lo.x, y, lo.y}, {hi.x, y, lo.y}, color, on_top);
    lines.line({hi.x, y, lo.y}, {hi.x, y, hi.y}, color, on_top);
    lines.line({hi.x, y, hi.y}, {lo.x, y, hi.y}, color, on_top);
    lines.line({lo.x, y, hi.y}, {lo.x, y, lo.y}, color, on_top);
}

}  // namespace

std::string MapEditor::maps_directory(moteur::Application& app) {
#ifdef MOTEUR_ASSETS_SOURCE_DIR
    const std::string source = MOTEUR_ASSETS_SOURCE_DIR;
    std::error_code error;
    if (std::filesystem::is_directory(utf8_path(source + "/maps"), error)) {
        return source + "/maps/";
    }
#endif
    return app.assets().root() + "maps/";
}

MapEditor::MapEditor(moteur::Application& app, const Options& options, bool standalone)
    : app_(app), options_(options), standalone_(standalone), top_view_(options.top_view),
      maps_dir_(options.maps_directory.empty() ? maps_directory(app) : options.maps_directory) {
    if (maps_dir_.back() != '/' && maps_dir_.back() != '\\') {
        maps_dir_ += '/';
    }
    moteur::Renderer& renderer = app.renderer();
    cube_ = moteur::make_asset(moteur::Mesh::create(renderer, moteur::make_cube(), "editor.cube"));
    tile_ = moteur::make_asset(moteur::Mesh::create(renderer, moteur::make_plane(), "editor.tile"));
    sky_ = moteur::Environment::create(renderer, moteur::make_sky(256, 128), "editor.sky");
    meshes_ = make_object_meshes(app);
    renderer.meshes().set_shadows(moteur::ShadowOptions{});
    renderer.meshes().set_culling(true);
    renderer.set_render_scale(1.0f);
    renderer.set_exposure(1.0f);

    camera_.set_angles(top_view_ ? 0.0f : 45.0f, top_view_ ? kTopPitch : 55.0f);
    camera_.set_projection(top_view_ ? moteur::Projection::Orthographic : moteur::Projection::Perspective);
    refresh_lists();
    if (options_.map.empty() || !open(options_.map)) {
        if (!options_.map.empty()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Éditeur : %s", status_.c_str());  // also in the panel
        }
        moteur::MapDocument fresh(new_size_[0], new_size_[1], new_size_[2], "sol");
        fresh.add_tile_type({"mur", false, true, ""});
        fresh.set_symbol({'.', {fresh.tile_id("sol")}, ""});
        fresh.set_symbol({'#', {fresh.tile_id("sol"), fresh.tile_id("mur")}, ""});
        fresh.set_point({new_size_[0] / 2, new_size_[1] / 2}, "depart");
        adopt(std::move(fresh));
        name_.clear();
        saved_revision_ = doc_.revision();  // nothing to lose yet
    }
    if (moteur::DebugTools* tools = app.debug_tools()) {
        tools->watch(world_, "Éditeur");
    }
}

MapEditor::~MapEditor() {
    trial_.reset();
    // Changes not saved: a copy in the player's preferences, offered when the map opens again.
    if (modified() && doc_.width() > 0) {
        std::string error;
        if (moteur::write_file_atomic(recovery_path(), doc_.to_json(), error)) {
            SDL_Log("Éditeur : modifications non enregistrées gardées dans %s", recovery_path().c_str());
        } else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Éditeur : %s", error.c_str());
        }
    }
    if (moteur::DebugTools* tools = app_.debug_tools()) {
        tools->forget(world_);
    }
}

// --- File -------------------------------------------------------------------------------------

std::string MapEditor::map_path(const std::string& name) const {
    return maps_dir_ + name + ".json";
}

std::string MapEditor::recovery_path() const {
    return app_.preferences_directory() + "editeur_" + (name_.empty() ? std::string("sans_nom") : name_) + ".json";
}

void MapEditor::refresh_lists() {
    map_names_ = json_files(maps_dir_);
    effect_names_ = json_files(app_.assets().root() + "effects");
    character_names_.clear();
    if (app_.data().has<CharacterData>()) {
        const moteur::DataTable<CharacterData>& table = app_.data().get<CharacterData>();
        for (const std::uint32_t index : table.sorted()) {
            character_names_.push_back(table.id(index));
        }
    }
}

bool MapEditor::open(const std::string& name) {
    const std::string path = map_path(name);
    try {
        const moteur::MapData data = moteur::MapData::parse(moteur::read_text_file(path), "maps/" + name + ".json");
        adopt(moteur::MapDocument::from(data));
    } catch (const std::exception& e) {
        status_ = e.what();
        status_error_ = true;
        return false;
    }
    name_ = name;
    file_time_ = moteur::file_time(path);
    external_change_ = false;
    saved_revision_ = doc_.revision();
    const std::int64_t recovery = moteur::file_time(recovery_path());
    recovery_found_ = recovery != 0 && recovery > file_time_;
    status_ = "Ouvert : " + path;
    status_error_ = false;
    return true;
}

void MapEditor::adopt(moteur::MapDocument document) {
    doc_ = std::move(document);
    doc_.clear_history();
    selected_.clear();
    connector_ = -1;
    painting_ = moving_ = false;
    rect_start_.reset();
    widget_action_ = false;
    issues_revision_ = ~0ull;
    issues_timer_ = 0.0;
    force_rebuild_ = true;
    // The brush starts with the stack of the start (or the first entry of the palette).
    brush_stack_.assign(static_cast<std::size_t>(doc_.layer_count()), moteur::kNoTile);
    const std::vector<moteur::MapSymbol> palette = doc_.palette();
    const std::map<std::string, std::vector<glm::ivec2>> points = doc_.points();  // a copy: one map to search
    const auto start = points.find("depart");
    if (start != points.end() && !start->second.empty()) {
        brush_stack_ = doc_.stack(start->second.front());
    } else if (!palette.empty()) {
        for (std::size_t i = 0; i < palette.front().tiles.size() && i < brush_stack_.size(); ++i) {
            brush_stack_[i] = palette.front().tiles[i];
        }
    }
    layer_ = std::min(layer_, doc_.layer_count() - 1);
    frame_map();
}

bool MapEditor::save(bool overwrite) {
    if (name_.empty()) {
        open_save_as_ = true;
        return false;
    }
    const std::string path = map_path(name_);
    if (!overwrite && moteur::file_time(path) != file_time_) {
        confirm_overwrite_ = true;  // changed (or created) by someone else since it was opened
        return false;
    }
    std::string error;
    if (!moteur::write_file_atomic(path, doc_.to_json(), error)) {
        status_ = "Échec de l'enregistrement : " + error;
        status_error_ = true;
        return false;
    }
    file_time_ = moteur::file_time(path);
    external_change_ = false;
    saved_revision_ = doc_.revision();
    std::error_code ignored;
    std::filesystem::remove(utf8_path(recovery_path()), ignored);
    recovery_found_ = false;
    int errors = 0;
    for (const moteur::MapDocument::Issue& issue : issues_) {
        errors += issue.level == moteur::MapDocument::Issue::Level::Error ? 1 : 0;
    }
    status_ = "Enregistré : " + path + (errors > 0 ? " (" + std::to_string(errors) + " erreur(s) de vérification)" : "");
    status_error_ = errors > 0;
    refresh_lists();
    return true;
}

// --- Tools ------------------------------------------------------------------------------------

std::vector<moteur::TileId> MapEditor::brush_stack() const {
    std::vector<moteur::TileId> stack = brush_stack_;
    stack.resize(static_cast<std::size_t>(doc_.layer_count()), moteur::kNoTile);
    return stack;
}

void MapEditor::paint(glm::ivec2 centre) {
    const int lo = -(brush_size_ - 1) / 2;
    const int hi = brush_size_ / 2;
    for (int dy = lo; dy <= hi; ++dy) {
        for (int dx = lo; dx <= hi; ++dx) {
            const glm::ivec2 cell = centre + glm::ivec2(dx, dy);
            if (!doc_.contains(cell)) {
                continue;
            }
            if (tool_ == Tool::Eraser) {
                if (layer_ >= 0) {
                    doc_.set_tile(layer_, cell, moteur::kNoTile);
                } else {
                    for (int layer = 1; layer < doc_.layer_count(); ++layer) {
                        doc_.set_tile(layer, cell, moteur::kNoTile);  // the floor stays
                    }
                }
            } else if (layer_ >= 0) {
                doc_.set_tile(layer_, cell, brush_tile_);
            } else {
                doc_.set_stack(cell, brush_stack());
            }
        }
    }
    note_cells(centre + lo, centre + hi);
}

std::string MapEditor::pick_object(glm::vec3 ground) const {
    std::string best;
    float best_distance = 1e9f;
    for (const moteur::MapObject& object : doc_.objects()) {
        const float d = glm::length(glm::vec2(object.position.x - ground.x, object.position.z - ground.z));
        if (d < object_radius(object) && d < best_distance) {
            best = object.id;
            best_distance = d;
        }
    }
    return best;
}

glm::vec3 MapEditor::snapped(glm::vec3 position) const {
    if (!snap_ || snap_step_ <= 0.0f) {
        return position;
    }
    return {std::round(position.x / snap_step_) * snap_step_, position.y, std::round(position.z / snap_step_) * snap_step_};
}

void MapEditor::place_object(glm::vec3 position) {
    const ObjectType& type = object_types()[static_cast<std::size_t>(object_type_)];
    moteur::MapObject object;
    object.type = type.name;
    object.position = snapped({position.x, 0.0f, position.z});
    object.props = object_props(type);
    const std::string previous = selected_;
    selected_ = doc_.add_object(std::move(object));
    note_object(previous);
    note_object(selected_);
}

void MapEditor::duplicate_selected() {
    const moteur::MapObject* object = doc_.object(selected_);
    if (object == nullptr) {
        return;
    }
    moteur::MapObject copy = *object;
    copy.id.clear();
    copy.position += glm::vec3(1.0f, 0.0f, 1.0f);
    const std::string previous = selected_;
    selected_ = doc_.add_object(std::move(copy));
    note_object(previous);
    note_object(selected_);
}

void MapEditor::delete_selected() {
    if (doc_.object(selected_) == nullptr) {
        return;
    }
    doc_.remove_object(selected_);
    note_object(selected_);
    selected_.clear();
}

void MapEditor::turn_selected(float degrees) {
    const moteur::MapObject* object = doc_.object(selected_);
    if (object == nullptr) {
        return;
    }
    moteur::MapObject turned = *object;
    turned.rotation = std::fmod(turned.rotation + degrees + 360.0f, 360.0f);
    doc_.update_object(turned);
    note_object(selected_);
}

void MapEditor::undo() {
    if (!doc_.in_action() && doc_.undo()) {
        status_ = "Annulé";
        status_error_ = false;
    }
}

void MapEditor::redo() {
    if (!doc_.in_action() && doc_.redo()) {
        status_ = "Rétabli";
        status_error_ = false;
    }
}

void MapEditor::tool_press(bool shift, bool ctrl) {
    update_pointer();
    if (!ground_) {
        return;
    }
    const glm::ivec2 cell = moteur::cell_at({ground_->x, ground_->z});
    const bool inside = doc_.contains(cell);
    switch (tool_) {
        case Tool::Brush:
        case Tool::Eraser:
            if (inside) {
                doc_.begin_action(tool_ == Tool::Brush ? "pinceau" : "gomme");
                painting_ = true;
                last_cell_ = cell;
                paint(cell);
            }
            break;
        case Tool::Rect:
        case Tool::HollowRect:
            if (inside) {
                rect_start_ = cell;
            }
            break;
        case Tool::Fill:
            if (inside) {
                doc_.begin_action("remplissage");
                const int changed = doc_.fill(cell, brush_stack());
                doc_.end_action();
                status_ = std::to_string(changed) + " case(s) remplie(s)";
                status_error_ = false;
            }
            break;
        case Tool::Picker:
            if (inside) {
                brush_stack_ = doc_.stack(cell);
                if (layer_ >= 0) {
                    brush_tile_ = brush_stack_[static_cast<std::size_t>(layer_)];
                }
                tool_ = before_picker_;
            }
            break;
        case Tool::Point:
            if (inside) {
                doc_.set_point(cell, shift ? std::string() : point_name_);
                note_cells(cell, cell);
            }
            break;
        case Tool::Select:
        case Tool::Object: {
            const std::string picked = pick_object(*ground_);
            if (!picked.empty()) {
                const std::string previous = selected_;
                selected_ = picked;
                note_object(previous);
                note_object(selected_);
                if (ctrl) {
                    duplicate_selected();  // Ctrl+clic: drags a copy
                }
                const moteur::MapObject* object = doc_.object(selected_);
                grab_offset_ = glm::vec2(object->position.x - ground_->x, object->position.z - ground_->z);
                doc_.begin_action("déplacement");
                moving_ = true;
            } else if (tool_ == Tool::Object) {
                place_object(*ground_);
            } else {
                note_object(selected_);
                selected_.clear();
            }
            break;
        }
        case Tool::Connector: {
            if (!inside) {
                break;
            }
            const std::vector<moteur::MapConnector>& connectors = doc_.connectors();
            for (std::size_t i = 0; i < connectors.size(); ++i) {
                if (connectors[i].cell == cell) {
                    connector_ = static_cast<int>(i);
                    return;
                }
            }
            // On the edge of the map only, facing out of it (the nearest side at a corner).
            const glm::vec2 local(ground_->x - static_cast<float>(cell.x), ground_->z - static_cast<float>(cell.y));
            using Direction = moteur::MapConnector::Direction;
            std::vector<std::pair<float, Direction>> sides;
            if (cell.y == 0) sides.emplace_back(local.y, Direction::North);
            if (cell.x == doc_.width() - 1) sides.emplace_back(1.0f - local.x, Direction::East);
            if (cell.y == doc_.height() - 1) sides.emplace_back(1.0f - local.y, Direction::South);
            if (cell.x == 0) sides.emplace_back(local.x, Direction::West);
            if (sides.empty()) {
                status_ = "Un connecteur se pose sur une case du bord de la carte";
                status_error_ = true;
                break;
            }
            std::sort(sides.begin(), sides.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            std::vector<moteur::MapConnector> next = connectors;
            moteur::MapConnector connector;
            for (int n = 1;; ++n) {
                connector.name = "c" + std::to_string(n);
                if (std::none_of(next.begin(), next.end(), [&](const auto& c) { return c.name == connector.name; })) {
                    break;
                }
            }
            connector.kind = connector_kind_;
            connector.cell = cell;
            connector.direction = sides.front().second;
            next.push_back(connector);
            doc_.set_connectors(std::move(next));
            connector_ = static_cast<int>(doc_.connectors().size()) - 1;
            note_cells(cell, cell);
            break;
        }
    }
}

void MapEditor::tool_drag() {
    if (!ground_) {
        return;
    }
    const glm::ivec2 cell = moteur::cell_at({ground_->x, ground_->z});
    if (painting_ && cell != last_cell_) {
        // Every cell between the last one and this one: a fast stroke leaves no hole.
        for (const glm::ivec2 c : moteur::MapDocument::line(last_cell_, cell)) {
            paint(c);
        }
        last_cell_ = cell;
    }
    if (moving_) {
        if (const moteur::MapObject* object = doc_.object(selected_)) {
            moteur::MapObject moved = *object;
            const glm::vec3 p = snapped({ground_->x + grab_offset_.x, object->position.y, ground_->z + grab_offset_.y});
            if (p != object->position) {
                moved.position = p;
                doc_.update_object(moved);
                note_object(selected_);
            }
        }
    }
}

void MapEditor::tool_release() {
    if (painting_) {
        doc_.end_action();
        painting_ = false;
    }
    if (moving_) {
        doc_.end_action();
        moving_ = false;
    }
    if (rect_start_ && hovered_) {
        doc_.begin_action(tool_ == Tool::HollowRect ? "rectangle vide" : "rectangle");
        doc_.rect(*rect_start_, *hovered_, brush_stack(), tool_ == Tool::HollowRect);
        doc_.end_action();
        note_cells(*rect_start_, *hovered_);
    }
    rect_start_.reset();
}

void MapEditor::start_trial(bool at_pointer) {
    try {
        WorldTest::Options trial;
        trial.map = name_.empty() ? "salle_portes" : name_;
        trial.data = std::make_shared<moteur::MapData>(doc_.to_map_data(name_.empty() ? "éditeur" : "maps/" + name_ + ".json"));
        if (at_pointer && ground_) {
            trial.start = glm::vec2(ground_->x, ground_->z);
        }
        trial_ = std::make_unique<WorldTest>(app_, trial, false);
        status_.clear();
    } catch (const std::exception& e) {
        status_ = std::string("Essai impossible : ") + e.what();
        status_error_ = true;
    }
}

// --- View -------------------------------------------------------------------------------------

void MapEditor::note_cells(glm::ivec2 a, glm::ivec2 b) {
    const glm::ivec2 lo = glm::max(glm::min(a, b) - 1, glm::ivec2(0));  // the neighbours' walls too
    const glm::ivec2 hi = glm::min(glm::max(a, b) + 1, glm::ivec2(doc_.width() - 1, doc_.height() - 1));
    if (blocks_x_ > 0) {
        for (int by = lo.y / kBlock; by <= hi.y / kBlock && by < blocks_y_; ++by) {
            for (int bx = lo.x / kBlock; bx <= hi.x / kBlock && bx < blocks_x_; ++bx) {
                dirty_blocks_[static_cast<std::size_t>(by * blocks_x_ + bx)] = 1;
            }
        }
    }
    noted_revision_ = doc_.revision();
}

void MapEditor::note_object(const std::string& id) {
    if (!id.empty()) {
        dirty_objects_.push_back(id);
    }
    noted_revision_ = doc_.revision();
}

void MapEditor::clear_view() {
    world_.registry().clear();
    blocks_.clear();
    object_entities_.clear();
    ground_entities_.clear();
}

MapEditor::CellLook MapEditor::look(glm::ivec2 cell) const {
    CellLook result;
    bool all_walkable = true;
    const std::vector<moteur::MapDocument::TileType>& types = doc_.tile_types();
    for (int layer = 0; layer < doc_.layer_count(); ++layer) {
        const moteur::TileId id = doc_.tile(layer, cell);
        if (id == moteur::kNoTile || id > types.size()) {
            continue;
        }
        const moteur::MapDocument::TileType& type = types[id - 1u];
        result.any = true;
        all_walkable = all_walkable && type.walkable;
        result.opaque = result.opaque || type.opaque;
        result.pillar = result.pillar || type.name == "pilier";
        if (layer == 0) {
            result.ground = id;
        }
    }
    result.walkable = result.any && all_walkable;
    return result;
}

void MapEditor::rebuild() {
    clear_view();
    blocks_x_ = (doc_.width() + kBlock - 1) / kBlock;
    blocks_y_ = (doc_.height() + kBlock - 1) / kBlock;
    blocks_.assign(static_cast<std::size_t>(blocks_x_) * static_cast<std::size_t>(blocks_y_), {});
    dirty_blocks_.assign(blocks_.size(), 0);
    for (int by = 0; by < blocks_y_; ++by) {
        for (int bx = 0; bx < blocks_x_; ++bx) {
            rebuild_block(bx, by);
        }
    }
    for (const moteur::MapObject& object : doc_.objects()) {
        rebuild_object(object.id);
    }
    // A dark bottom under the map: holes look like holes.
    entt::registry& registry = world_.registry();
    const entt::entity bottom = registry.create();
    const auto w = static_cast<float>(doc_.width());
    const auto h = static_cast<float>(doc_.height());
    registry.emplace<moteur::Transform>(bottom, place({w * 0.5f, -3.0f, h * 0.5f}, {w + 40.0f, 1.0f, h + 40.0f}));
    registry.emplace<moteur::MeshComponent>(bottom, tile_, surface(linear(0.04f, 0.04f, 0.05f), 1.0f, false));
    ground_entities_.push_back(bottom);
}

void MapEditor::rebuild_block(int bx, int by) {
    entt::registry& registry = world_.registry();
    std::vector<entt::entity>& entities = blocks_[static_cast<std::size_t>(by * blocks_x_ + bx)];
    registry.destroy(entities.begin(), entities.end());
    entities.clear();
    const auto add = [&](const moteur::Asset<moteur::Mesh>& mesh, const moteur::Transform& transform, const moteur::Material& material) {
        const entt::entity e = registry.create();
        registry.emplace<moteur::Transform>(e, transform);
        registry.emplace<moteur::MeshComponent>(e, mesh, material);
        entities.push_back(e);
    };

    // The floor, merged by tile of layer 0 and tone of the checkerboard: a tile type has a tint
    // of its own (except "sol"), to tell grounds apart.
    std::map<std::pair<moteur::TileId, int>, moteur::MeshData> floors;
    const moteur::MeshData quad = moteur::make_plane();
    const int x0 = bx * kBlock, y0 = by * kBlock;
    const int x1 = std::min(x0 + kBlock, doc_.width()), y1 = std::min(y0 + kBlock, doc_.height());
    moteur::Material stone = surface(linear(0.62f, 0.58f, 0.52f), 0.8f);
    moteur::Material dark_stone = surface(linear(0.42f, 0.4f, 0.38f), 0.7f);
    const moteur::Material wood = surface(linear(0.45f, 0.3f, 0.18f), 0.75f);
    const moteur::Material grass = surface(linear(0.25f, 0.45f, 0.18f), 0.9f);
    for (int j = y0; j < y1; ++j) {
        for (int i = x0; i < x1; ++i) {
            const CellLook cell = look({i, j});
            if (!cell.any) {
                continue;
            }
            moteur::MeshData& block = floors[{cell.ground, (i + j) & 1}];
            const auto base = static_cast<std::uint32_t>(block.vertices.size());
            for (moteur::Vertex3D vertex : quad.vertices) {
                vertex.position += glm::vec3(static_cast<float>(i) + 0.5f, 0.0f, static_cast<float>(j) + 0.5f);
                block.vertices.push_back(vertex);
            }
            for (const std::uint32_t index : quad.indices) {
                block.indices.push_back(base + index);
            }
            const glm::vec3 foot(static_cast<float>(i) + 0.5f, 0.0f, static_cast<float>(j) + 0.5f);
            if (!cell.walkable && cell.opaque && cell.pillar) {
                add(cube_, place(foot + glm::vec3(0.0f, 1.25f, 0.0f), {0.6f, 2.5f, 0.6f}), dark_stone);
            } else if (!cell.walkable && cell.opaque) {
                add(cube_, place(foot + glm::vec3(0.0f, 0.75f, 0.0f), {1.0f, 1.5f, 1.0f}), stone);
            } else if (!cell.walkable) {
                add(cube_, place(foot + glm::vec3(0.0f, 0.3f, 0.0f), {0.9f, 0.6f, 0.25f}), wood);
            } else if (cell.opaque) {
                add(cube_, place(foot + glm::vec3(0.0f, 0.45f, 0.0f), {0.8f, 0.9f, 0.8f}), grass);
            }
        }
    }
    const std::vector<moteur::MapDocument::TileType>& types = doc_.tile_types();
    for (auto& [key, data] : floors) {
        const auto [ground, tone] = key;
        glm::vec3 color = tone == 0 ? glm::vec3(0.55f, 0.56f, 0.5f) : glm::vec3(0.45f, 0.46f, 0.41f);
        if (ground != moteur::kNoTile && ground <= types.size() && types[ground - 1u].name != "sol") {
            color = glm::mix(color, name_color(types[ground - 1u].name), 0.35f);
        }
        add(moteur::make_asset(moteur::Mesh::create(app_.renderer(), data, "editor.floor block")), moteur::Transform{},
            surface(linear(color.r, color.g, color.b), 0.85f, false));
    }
}

void MapEditor::rebuild_object(const std::string& id) {
    entt::registry& registry = world_.registry();
    if (const auto found = object_entities_.find(id); found != object_entities_.end()) {
        registry.destroy(found->second.begin(), found->second.end());
        object_entities_.erase(found);
    }
    const moteur::MapObject* object = doc_.object(id);
    if (object == nullptr) {
        return;
    }
    const bool selected = id == selected_;
    std::vector<entt::entity> entities;
    try {
        entities = build_object(world_, *object, meshes_, selected);
    } catch (const std::exception& e) {
        // A property of the wrong kind (a file edited by hand): only its marker shows, the panel
        // lets fix it.
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Éditeur : objet %s (%s) : %s", id.c_str(), object->type.c_str(), e.what());
    }
    // What the game spawns (creatures, effects) has a stand-in in the editor.
    const auto stand_in = [&](const moteur::Asset<moteur::Mesh>& mesh, const moteur::Transform& transform, moteur::Material material) {
        if (selected) {
            material.emissive += glm::vec3(0.6f, 0.45f, 0.1f);
        }
        const entt::entity e = registry.create();
        registry.emplace<moteur::Transform>(e, transform);
        registry.emplace<moteur::MeshComponent>(e, mesh, material);
        entities.push_back(e);
    };
    const glm::vec3 p = object->position;
    if (object->type == "monstre") {
        const float s = object->scale;
        stand_in(cube_, place(p + glm::vec3(0.0f, 0.7f * s, 0.0f), glm::vec3(0.5f, 1.4f, 0.5f) * s), surface(linear(0.7f, 0.2f, 0.15f), 0.6f));
    } else if (object->type == "effet") {
        stand_in(meshes_.sphere, place(p + glm::vec3(0.0f, 0.3f, 0.0f), glm::vec3(0.3f)), glowing({0.3f, 0.6f, 1.5f}));
    }
    object_entities_[id] = std::move(entities);
}

void MapEditor::update_pointer() {
    hovered_.reset();
    ground_.reset();
    const glm::vec2 pointer = app_.input().pointer();
    if (pointer.x < 0.0f || pointer.y < 0.0f) {
        return;
    }
    if (const auto ground = camera_.ground_point(pointer)) {
        ground_ = ground;
        if (const glm::ivec2 cell = moteur::cell_at({ground->x, ground->z}); doc_.contains(cell)) {
            hovered_ = cell;
        }
    }
}

void MapEditor::frame_map() {
    const auto w = static_cast<float>(doc_.width());
    const auto h = static_cast<float>(doc_.height());
    camera_.set_target({w * 0.5f, 0.0f, h * 0.5f});
    const glm::vec2 viewport = camera_.viewport();
    const float aspect = viewport.y > 0.0f ? viewport.x / viewport.y : 16.0f / 9.0f;
    // At an angle the far side of the map recedes: a little more room than seen from above.
    camera_.set_visible_height(std::clamp(std::max(h, w / aspect) * (top_view_ ? 1.08f : 1.35f), 6.0f, 400.0f));
}

// --- Frame ------------------------------------------------------------------------------------

void MapEditor::on_event(const SDL_Event& event) {
    const bool has_ui = ImGui::GetCurrentContext() != nullptr;
    if (trial_) {
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE && !event.key.repeat) {
            end_trial_ = true;
            return;
        }
        trial_->on_event(event);
        return;
    }
    const ImGuiIO* io = has_ui ? &ImGui::GetIO() : nullptr;
    switch (event.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            if (io != nullptr && io->WantCaptureMouse) {
                return;
            }
            const SDL_Keymod mods = SDL_GetModState();
            if (event.button.button == SDL_BUTTON_LEFT) {
                tool_press((mods & SDL_KMOD_SHIFT) != 0, (mods & SDL_KMOD_CTRL) != 0);
            } else if (event.button.button == SDL_BUTTON_RIGHT || event.button.button == SDL_BUTTON_MIDDLE) {
                update_pointer();
                if (ground_) {
                    panning_ = true;
                    pan_anchor_ = *ground_;
                }
            }
            return;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.button == SDL_BUTTON_LEFT) {
                update_pointer();
                tool_release();
            } else {
                panning_ = false;
            }
            return;
        case SDL_EVENT_MOUSE_WHEEL: {
            if (io != nullptr && io->WantCaptureMouse) {
                return;
            }
            // Zoom towards the pointer: the ground under it stays under it.
            const glm::vec2 pointer = app_.input().pointer();
            const auto before = camera_.ground_point(pointer);
            camera_.set_visible_height(std::clamp(camera_.visible_height() * std::pow(0.88f, event.wheel.y), 3.0f, 400.0f));
            const auto after = camera_.ground_point(pointer);
            if (before && after) {
                camera_.set_target(camera_.target() + (*before - *after));
            }
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
        else if (key == SDLK_D && !event.key.repeat) duplicate_selected();
        else if (key == SDLK_N && !event.key.repeat) open_new_ = true;
        return;
    }
    if (event.key.repeat && key != SDLK_LEFTBRACKET && key != SDLK_RIGHTBRACKET) {
        return;
    }
    const auto choose = [&](Tool tool) {
        if (tool == Tool::Picker && tool_ != Tool::Picker) {
            before_picker_ = tool_;
        }
        tool_ = tool;
    };
    switch (key) {
        case SDLK_ESCAPE:
            if (!selected_.empty()) {
                note_object(selected_);
                selected_.clear();
            } else if (!standalone_) {
                if (modified()) confirm_quit_ = true;
                else stop_requested_ = true;
            }
            break;
        case SDLK_V: choose(Tool::Select); break;
        case SDLK_B: choose(Tool::Brush); break;
        case SDLK_E: choose(Tool::Eraser); break;
        case SDLK_R: choose(shift ? Tool::HollowRect : Tool::Rect); break;
        case SDLK_F: choose(Tool::Fill); break;
        case SDLK_I: choose(Tool::Picker); break;
        case SDLK_P: choose(Tool::Point); break;
        case SDLK_O: choose(Tool::Object); break;
        case SDLK_C: choose(Tool::Connector); break;
        case SDLK_T:
            top_view_ = !top_view_;
            camera_.set_angles(top_view_ ? 0.0f : 45.0f, top_view_ ? kTopPitch : 55.0f);
            camera_.set_projection(top_view_ ? moteur::Projection::Orthographic : moteur::Projection::Perspective);
            break;
        case SDLK_HOME: frame_map(); break;
        case SDLK_DELETE: delete_selected(); break;
        case SDLK_LEFTBRACKET:
        case SDLK_RIGHTBRACKET: {
            const float sign = key == SDLK_LEFTBRACKET ? -1.0f : 1.0f;
            if (!selected_.empty()) {
                turn_selected(sign * (shift ? 90.0f : 15.0f));
            } else {
                brush_size_ = std::clamp(brush_size_ + static_cast<int>(sign), 1, 9);
            }
            break;
        }
        case SDLK_F5: trial_request_ = 1; break;
        case SDLK_F6: trial_request_ = 2; break;
        default: break;
    }
}

void MapEditor::update(double dt) {
    elapsed_ += dt;
    if (options_.run_seconds > 0.0 && elapsed_ >= options_.run_seconds) {
        if (standalone_) {
            app_.quit();
        } else {
            stop_requested_ = true;
        }
    }
    // The trial is created and destroyed here, outside a frame (it loads and uploads).
    if (end_trial_) {
        trial_.reset();
        end_trial_ = false;
        app_.input().clear_actions();
    }
    if (trial_request_ != 0) {
        update_pointer();
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

    const bool has_ui = ImGui::GetCurrentContext() != nullptr;
    // Keys move the camera (unless a text field has them).
    if (!has_ui || !ImGui::GetIO().WantTextInput) {
        const bool* keys = SDL_GetKeyboardState(nullptr);
        const bool ctrl = keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL];
        glm::vec2 direction(0.0f);
        if (!ctrl) {
            direction.x = (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT] ? 1.0f : 0.0f);
            direction.y = (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN] ? 1.0f : 0.0f);
        }
        if (direction != glm::vec2(0.0f)) {
            const glm::vec3 forward = camera_.forward();
            glm::vec3 ahead(forward.x, 0.0f, forward.z);
            if (glm::length(ahead) < 1e-3f) {
                ahead = glm::vec3(0.0f, 0.0f, -1.0f);  // straight down: "up" on screen is north
            }
            ahead = glm::normalize(ahead);
            const glm::vec3 right(-ahead.z, 0.0f, ahead.x);
            camera_.set_target(camera_.target() + (right * direction.x + ahead * direction.y) *
                                                      (camera_.visible_height() * 0.8f * static_cast<float>(dt)));
        }
    }

    update_pointer();
    if (painting_ || moving_) {
        tool_drag();
    }

    // The view follows the document: only what the notes name, or everything (undo, a new type...).
    const std::uint64_t revision = doc_.revision();
    const std::uint64_t build_start = SDL_GetTicksNS();
    if (force_rebuild_ || (revision != built_revision_ && revision != noted_revision_)) {
        rebuild();
        force_rebuild_ = false;
        full_build_ms_ = static_cast<double>(SDL_GetTicksNS() - build_start) * 1e-6;
    } else {
        const bool any = !dirty_objects_.empty() || std::find(dirty_blocks_.begin(), dirty_blocks_.end(), 1) != dirty_blocks_.end();
        for (int by = 0; by < blocks_y_; ++by) {
            for (int bx = 0; bx < blocks_x_; ++bx) {
                std::uint8_t& dirty = dirty_blocks_[static_cast<std::size_t>(by * blocks_x_ + bx)];
                if (dirty != 0) {
                    rebuild_block(bx, by);
                    dirty = 0;
                }
            }
        }
        std::sort(dirty_objects_.begin(), dirty_objects_.end());
        dirty_objects_.erase(std::unique(dirty_objects_.begin(), dirty_objects_.end()), dirty_objects_.end());
        for (const std::string& id : dirty_objects_) {
            rebuild_object(id);
        }
        if (any) {
            partial_build_ms_ = static_cast<double>(SDL_GetTicksNS() - build_start) * 1e-6;
        }
    }
    dirty_objects_.clear();
    built_revision_ = revision;
    if (!selected_.empty() && doc_.object(selected_) == nullptr) {
        selected_.clear();  // undone
    }
    if (connector_ >= static_cast<int>(doc_.connectors().size())) {
        connector_ = -1;
    }

    // Checks: again when the map changed, at most twice a second, not during a stroke.
    issues_timer_ -= dt;
    if (revision != issues_revision_ && issues_timer_ <= 0.0 && !painting_ && !moving_) {
        issues_ = doc_.validate();
        issue_cells_.assign(static_cast<std::size_t>(doc_.width()) * static_cast<std::size_t>(doc_.height()), 0);
        for (const moteur::MapDocument::Issue& issue : issues_) {
            const std::uint8_t level = issue.level == moteur::MapDocument::Issue::Level::Error ? 1 : 2;
            for (const glm::ivec2 cell : issue.cells) {
                if (doc_.contains(cell)) {
                    std::uint8_t& slot = issue_cells_[static_cast<std::size_t>(cell.y * doc_.width() + cell.x)];
                    slot = slot == 0 ? level : std::min(slot, level);
                }
            }
        }
        issues_revision_ = revision;
        issues_timer_ = 0.5;
    }

    // Someone else changed the file?
    check_timer_ -= dt;
    if (check_timer_ <= 0.0 && !name_.empty()) {
        check_timer_ = 1.0;
        external_change_ = moteur::file_time(map_path(name_)) != file_time_;
    }
}

void MapEditor::render(moteur::Renderer& renderer, double alpha) {
    ++frames_;
    ++rendered_;
    if (trial_) {
        trial_->render(renderer, alpha);
        if (ImGui::GetCurrentContext() != nullptr) {
            ImGui::SetNextWindowPos(ImVec2(ImGui::GetMainViewport()->WorkPos.x + ImGui::GetMainViewport()->WorkSize.x - 10.0f,
                                           ImGui::GetMainViewport()->WorkPos.y + 10.0f),
                                    ImGuiCond_Always, ImVec2(1.0f, 0.0f));
            ImGui::SetNextWindowSize(ImVec2(380.0f, 0.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Essai de la carte", nullptr, ImGuiWindowFlags_NoMove)) {
                if (ImGui::Button("Revenir à l'éditeur (Échap)")) {
                    end_trial_ = true;
                }
                ImGui::Separator();
                trial_->draw_controls();
            }
            ImGui::End();
            if (standalone_) {
                if (moteur::DebugTools* tools = app_.debug_tools()) {
                    tools->draw();
                }
            }
        }
        return;
    }
    // The capture: a few frames after the view was first built (updates may not have run yet).
    const bool capture_mode = !options_.capture_path.empty();
    if (built_revision_ == ~0ull || force_rebuild_) {
        frames_ = 0;
    }
    if (capture_mode && !capture_done_ && frames_ >= 5) {
        renderer.request_capture(options_.capture_path);
        capture_done_ = true;
        frames_ = 0;
    } else if (capture_mode && capture_done_ && frames_ >= 3 && standalone_) {
        app_.quit();
    }

    renderer.set_clear_color(0.03f, 0.03f, 0.04f);
    const glm::vec2 viewport(static_cast<float>(renderer.width()), static_cast<float>(renderer.height()));
    if (viewport != camera_.viewport()) {
        camera_.set_viewport(viewport);
        if (!framed_) {
            frame_map();  // with the real proportions of the window
        }
    }
    framed_ = true;
    if (panning_) {
        // The ground grabbed stays under the pointer.
        if (const auto ground = camera_.ground_point(app_.input().pointer())) {
            camera_.set_target(camera_.target() + (pan_anchor_ - *ground));
        }
    }
    moteur::MeshRenderer& meshes = renderer.meshes();
    meshes.set_camera(camera_.view_projection(), camera_.position());
    renderer.billboards().set_camera(camera_);
    const float yaw = glm::radians(-60.0f), elevation = glm::radians(50.0f);
    meshes.set_sun({std::cos(elevation) * std::cos(yaw), std::sin(elevation), std::cos(elevation) * std::sin(yaw)},
                   linear(1.0f, 0.93f, 0.8f), 2.5f);
    meshes.set_environment(&*sky_, 0.8f);
    meshes.set_fog({});
    meshes.set_cutout({});
    moteur::CollectOptions collect;
    collect.view = camera_.frustum();
    world_.submit(renderer, 1.0f, collect);
    draw_overlays(renderer, camera_);

    if (!capture_mode && ImGui::GetCurrentContext() != nullptr) {
        if (show_labels_) {
            draw_labels(camera_);
        }
        draw_panels();
        if (standalone_) {
            if (moteur::DebugTools* tools = app_.debug_tools()) {
                tools->draw();
            }
        }
    }
}

void MapEditor::draw_overlays(moteur::Renderer& renderer, const moteur::Camera3D& camera) {
    moteur::DebugLineBuffer& lines = renderer.debug_lines().lines();
    const int w = doc_.width(), h = doc_.height();
    const glm::vec3 target = camera.target();
    const float reach = camera.visible_height() * 1.2f;
    const int x0 = std::max(0, static_cast<int>(target.x - reach)), x1 = std::min(w, static_cast<int>(target.x + reach) + 1);
    const int y0 = std::max(0, static_cast<int>(target.z - reach)), y1 = std::min(h, static_cast<int>(target.z + reach) + 1);

    // The grid near what the camera looks at (not when zoomed far out), the border of the map.
    if (show_grid_ && camera.visible_height() < 80.0f) {
        const glm::vec4 grid(1.0f, 1.0f, 1.0f, 0.12f);
        for (int x = x0; x <= x1; ++x) {
            lines.line({static_cast<float>(x), 0.02f, static_cast<float>(y0)}, {static_cast<float>(x), 0.02f, static_cast<float>(y1)}, grid);
        }
        for (int y = y0; y <= y1; ++y) {
            lines.line({static_cast<float>(x0), 0.02f, static_cast<float>(y)}, {static_cast<float>(x1), 0.02f, static_cast<float>(y)}, grid);
        }
    }
    cell_box(lines, {0, 0}, {w - 1, h - 1}, 0.03f, doc_.chunk ? glm::vec4(0.3f, 0.9f, 1.0f, 1.0f) : glm::vec4(1.0f, 1.0f, 1.0f, 0.6f), false);

    // What the checks found: red for errors, orange for warnings.
    if (show_issues_ && issue_cells_.size() == static_cast<std::size_t>(w) * static_cast<std::size_t>(h)) {
        int drawn = 0;
        for (int y = y0; y < y1 && drawn < 4000; ++y) {
            for (int x = x0; x < x1 && drawn < 4000; ++x) {
                const std::uint8_t level = issue_cells_[static_cast<std::size_t>(y * w + x)];
                if (level != 0) {
                    const glm::vec4 color = level == 1 ? glm::vec4(1.0f, 0.15f, 0.1f, 0.9f) : glm::vec4(1.0f, 0.6f, 0.1f, 0.9f);
                    const glm::vec3 a(static_cast<float>(x) + 0.2f, 0.06f, static_cast<float>(y) + 0.2f);
                    lines.line(a, a + glm::vec3(0.6f, 0.0f, 0.6f), color, true);
                    lines.line(a + glm::vec3(0.6f, 0.0f, 0.0f), a + glm::vec3(0.0f, 0.0f, 0.6f), color, true);
                    ++drawn;
                }
            }
        }
    }

    // Named points.
    for (const auto& [name, cells] : doc_.points()) {
        const glm::vec3 color = name_color(name);
        for (const glm::ivec2 cell : cells) {
            cell_box(lines, cell, cell, 0.05f, glm::vec4(color, 1.0f), false);
            const glm::vec3 c(static_cast<float>(cell.x) + 0.5f, 0.05f, static_cast<float>(cell.y) + 0.5f);
            lines.circle(c, {0.0f, 1.0f, 0.0f}, 0.3f, glm::vec4(color, 1.0f), 16);
        }
    }

    // Connectors: an arrow out of the map.
    const std::vector<moteur::MapConnector>& connectors = doc_.connectors();
    for (std::size_t i = 0; i < connectors.size(); ++i) {
        const glm::vec4 color = static_cast<int>(i) == connector_ ? glm::vec4(1.0f) : glm::vec4(1.0f, 0.85f, 0.2f, 1.0f);
        const glm::vec2 from = glm::vec2(connectors[i].cell) + 0.5f;
        const glm::vec2 to = edge_point(connectors[i]);
        const glm::vec2 out = to - from;
        const glm::vec2 tip = to + out;
        const glm::vec2 side(-out.y, out.x);
        lines.line({from.x, 0.1f, from.y}, {tip.x, 0.1f, tip.y}, color, true);
        lines.line({tip.x, 0.1f, tip.y}, {tip.x - out.x * 0.6f + side.x * 0.5f, 0.1f, tip.y - out.y * 0.6f + side.y * 0.5f}, color, true);
        lines.line({tip.x, 0.1f, tip.y}, {tip.x - out.x * 0.6f - side.x * 0.5f, 0.1f, tip.y - out.y * 0.6f - side.y * 0.5f}, color, true);
    }

    // The selected object.
    if (const moteur::MapObject* object = doc_.object(selected_)) {
        const glm::vec3 c(object->position.x, 0.08f, object->position.z);
        const float r = object_radius(*object);
        lines.circle(c, {0.0f, 1.0f, 0.0f}, r, glm::vec4(1.0f, 0.85f, 0.3f, 1.0f), 32, true);
        const float angle = glm::radians(object->rotation);
        // Its facing (rotation 0 looks towards +x).
        lines.line(c, c + glm::vec3(std::cos(angle), 0.0f, -std::sin(angle)) * (r + 0.3f), glm::vec4(1.0f, 0.85f, 0.3f, 1.0f), true);
    }

    // What the tool would touch.
    if (rect_start_ && hovered_) {
        cell_box(lines, *rect_start_, *hovered_, 0.07f, glm::vec4(0.3f, 0.8f, 1.0f, 1.0f), true);
    } else if (hovered_) {
        const bool brush = tool_ == Tool::Brush || tool_ == Tool::Eraser;
        const int lo = brush ? -(brush_size_ - 1) / 2 : 0;
        const int hi = brush ? brush_size_ / 2 : 0;
        cell_box(lines, *hovered_ + lo, *hovered_ + hi, 0.07f,
                 tool_ == Tool::Eraser ? glm::vec4(1.0f, 0.4f, 0.3f, 1.0f) : glm::vec4(1.0f, 0.85f, 0.2f, 1.0f), true);
    }
}

void MapEditor::draw_labels(const moteur::Camera3D& camera) {
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
    const auto text = [&](glm::vec3 at, const std::string& label, ImU32 color) {
        if (const auto pixel = camera.world_to_screen(at)) {
            const ImVec2 p(pixel->x / std::max(scale.x, 1e-3f), pixel->y / std::max(scale.y, 1e-3f));
            draw->AddText(ImVec2(p.x + 1.0f, p.y + 1.0f), IM_COL32(0, 0, 0, 200), label.c_str());
            draw->AddText(p, color, label.c_str());
        }
    };
    if (camera.visible_height() < 70.0f) {
        for (const auto& [name, cells] : doc_.points()) {
            const glm::vec3 c = name_color(name);
            const ImU32 color = IM_COL32(static_cast<int>(c.r * 255), static_cast<int>(c.g * 255), static_cast<int>(c.b * 255), 255);
            for (const glm::ivec2 cell : cells) {
                text({static_cast<float>(cell.x) + 0.5f, 0.1f, static_cast<float>(cell.y) + 0.5f}, name, color);
            }
        }
    }
    for (const moteur::MapConnector& connector : doc_.connectors()) {
        const glm::vec2 p = edge_point(connector);
        text({p.x, 0.1f, p.y}, connector.name + " (" + connector.kind + ")", IM_COL32(255, 220, 60, 255));
    }
}

// --- Panels -----------------------------------------------------------------------------------

void MapEditor::track_item(const char* action) {
    if (ImGui::IsItemActivated() && !doc_.in_action()) {
        doc_.begin_action(action);
        widget_action_ = true;
    }
    if (ImGui::IsItemDeactivated() && widget_action_) {
        doc_.end_action();
        widget_action_ = false;
    }
}

void MapEditor::draw_controls() {
    if (trial_) {
        ImGui::TextUnformatted("Essai de la carte en cours : Échap pour revenir à l'éditeur.");
        return;
    }
    ImGui::TextWrapped("Éditeur de cartes : le panneau « Éditeur de cartes » a les outils. Clic droit ou molette "
                       "enfoncée : déplacer la vue ; molette : zoom ; T : vue de dessus ; F5 : essayer la carte.");
    if (ImGui::Button("Quitter l'éditeur")) {
        if (modified()) confirm_quit_ = true;
        else stop_requested_ = true;
    }
}

void MapEditor::draw_panels() {
    const std::uint64_t before = doc_.revision();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 10.0f, viewport->WorkPos.y + 10.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400.0f, viewport->WorkSize.y - 20.0f), ImGuiCond_FirstUseEver);
    const std::string title = "Éditeur de cartes : " + (name_.empty() ? std::string("(sans nom)") : name_) + (modified() ? " *" : "") +
                              "###editeur";
    if (ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_MenuBar)) {
        draw_menu();
        if (recovery_found_) {
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Une copie non enregistrée de cette carte existe.");
            if (ImGui::Button("Ouvrir la copie")) {
                try {
                    const moteur::MapData data = moteur::MapData::parse(moteur::read_text_file(recovery_path()), recovery_path());
                    adopt(moteur::MapDocument::from(data));
                    saved_revision_ = ~0ull;
                } catch (const std::exception& e) {
                    status_ = e.what();
                    status_error_ = true;
                }
                recovery_found_ = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Supprimer la copie")) {
                std::error_code ignored;
                std::filesystem::remove(utf8_path(recovery_path()), ignored);
                recovery_found_ = false;
            }
            ImGui::Separator();
        }
        if (external_change_) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "Le fichier a changé sur le disque.");
            if (ImGui::Button("Recharger le fichier")) {
                if (modified()) {
                    pending_open_ = name_;
                    confirm_open_ = true;
                } else {
                    open(name_);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Garder ma version")) {
                file_time_ = moteur::file_time(map_path(name_));
                external_change_ = false;
            }
            ImGui::Separator();
        }
        draw_tools_panel();
        draw_tiles_panel();
        draw_object_panel();
        draw_connectors_panel();
        draw_issues_panel();
        if (ImGui::CollapsingHeader("Affichage")) {
            if (ImGui::Checkbox("Vue de dessus (T)", &top_view_)) {
                camera_.set_angles(top_view_ ? 0.0f : 45.0f, top_view_ ? kTopPitch : 55.0f);
                camera_.set_projection(top_view_ ? moteur::Projection::Orthographic : moteur::Projection::Perspective);
            }
            ImGui::Checkbox("Grille", &show_grid_);
            ImGui::Checkbox("Noms des points et connecteurs", &show_labels_);
            ImGui::Checkbox("Cases signalées par les vérifications", &show_issues_);
            if (ImGui::Button("Toute la carte (Origine)")) {
                frame_map();
            }
        }
        // Status line: the cell under the pointer, the last message.
        ImGui::Separator();
        if (hovered_) {
            std::string stack;
            for (int layer = 0; layer < doc_.layer_count(); ++layer) {
                const moteur::TileId id = doc_.tile(layer, *hovered_);
                stack += (layer > 0 ? " + " : "") + (id == moteur::kNoTile ? std::string("-") : doc_.tile_types()[id - 1u].name);
            }
            const auto point = doc_.point_at(*hovered_);
            ImGui::Text("Case %d, %d : %s%s", hovered_->x, hovered_->y, stack.c_str(), point ? (" ; point " + *point).c_str() : "");
        } else {
            ImGui::TextDisabled("Pointeur hors de la carte");
        }
        if (!status_.empty()) {
            if (status_error_) {
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", status_.c_str());
            } else {
                ImGui::TextWrapped("%s", status_.c_str());
            }
        }
    }
    ImGui::End();
    draw_popups();
    // A change made through the panels that no note describes: everything is built again.
    if (doc_.revision() != before && noted_revision_ != doc_.revision()) {
        force_rebuild_ = true;
    }
}

void MapEditor::draw_menu() {
    if (!ImGui::BeginMenuBar()) {
        return;
    }
    if (ImGui::BeginMenu("Fichier")) {
        if (ImGui::MenuItem("Nouvelle carte...", "Ctrl+N")) {
            open_new_ = true;
        }
        if (ImGui::BeginMenu("Ouvrir")) {
            for (const std::string& name : map_names_) {
                if (ImGui::MenuItem(name.c_str(), nullptr, name == name_)) {
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
        if (ImGui::MenuItem("Enregistrer", "Ctrl+S")) {
            save(false);
        }
        if (ImGui::MenuItem("Enregistrer sous...")) {
            open_save_as_ = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem(standalone_ ? "Quitter" : "Quitter l'éditeur")) {
            if (modified()) confirm_quit_ = true;
            else if (standalone_) app_.quit();
            else stop_requested_ = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Édition")) {
        const std::string undo_label = doc_.can_undo() ? "Annuler " + doc_.undo_name() : std::string("Annuler");
        if (ImGui::MenuItem(undo_label.c_str(), "Ctrl+Z", false, doc_.can_undo())) {
            undo();
        }
        if (ImGui::MenuItem("Rétablir", "Ctrl+Y", false, doc_.can_redo())) {
            redo();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Dupliquer l'objet", "Ctrl+D", false, !selected_.empty())) {
            duplicate_selected();
        }
        if (ImGui::MenuItem("Supprimer l'objet", "Suppr", false, !selected_.empty())) {
            delete_selected();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Carte")) {
        if (ImGui::MenuItem("Redimensionner...")) {
            std::fill(std::begin(resize_), std::end(resize_), 0);
            open_resize_ = true;
        }
        bool chunk = doc_.chunk;
        if (ImGui::MenuItem("Morceau de carte (chunk)", nullptr, &chunk)) {
            doc_.set_chunk(chunk);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Essayer")) {
        if (ImGui::MenuItem("Depuis le départ", "F5")) {
            trial_request_ = 1;
        }
        if (ImGui::MenuItem("Depuis le pointeur", "F6")) {
            trial_request_ = 2;
        }
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}

void MapEditor::draw_tools_panel() {
    if (!ImGui::CollapsingHeader("Outils", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    for (int i = 0; i < IM_ARRAYSIZE(kToolNames); ++i) {
        if (i % 3 != 0) {
            ImGui::SameLine();
        }
        const std::string label = std::string(kToolNames[i]) + " (" + kToolKeys[i] + ")";
        if (ImGui::RadioButton(label.c_str(), static_cast<int>(tool_) == i)) {
            const auto tool = static_cast<Tool>(i);
            if (tool == Tool::Picker && tool_ != Tool::Picker) {
                before_picker_ = tool_;
            }
            tool_ = tool;
        }
    }
    if (tool_ == Tool::Brush || tool_ == Tool::Eraser) {
        ImGui::SliderInt("Taille ([ et ])", &brush_size_, 1, 9);
    }
    if (tool_ == Tool::Point) {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%s", point_name_.c_str());
        if (ImGui::InputText("Nom du point", buffer, sizeof(buffer))) {
            point_name_ = buffer;
        }
        ImGui::TextDisabled("Clic : poser ; Maj+clic : retirer");
        for (const auto& [name, cells] : doc_.points()) {
            const std::string label = name + " (" + std::to_string(cells.size()) + ")";
            if (ImGui::SmallButton(label.c_str())) {
                point_name_ = name;
                camera_.set_target({static_cast<float>(cells.front().x) + 0.5f, 0.0f, static_cast<float>(cells.front().y) + 0.5f});
            }
            ImGui::SameLine();
        }
        ImGui::NewLine();
    }
}

void MapEditor::draw_tiles_panel() {
    if (!ImGui::CollapsingHeader("Tuiles et légende", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    const std::vector<moteur::MapDocument::TileType>& types = doc_.tile_types();
    const auto tile_combo = [&](const char* label, moteur::TileId& id) {
        const char* preview = id == moteur::kNoTile || id > types.size() ? "(rien)" : types[id - 1u].name.c_str();
        bool changed = false;
        if (ImGui::BeginCombo(label, preview)) {
            if (ImGui::Selectable("(rien)", id == moteur::kNoTile)) {
                id = moteur::kNoTile;
                changed = true;
            }
            for (std::size_t i = 0; i < types.size(); ++i) {
                if (ImGui::Selectable(types[i].name.c_str(), id == i + 1)) {
                    id = static_cast<moteur::TileId>(i + 1);
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    };

    // Which layers the brush and eraser touch.
    std::string layer_label = layer_ < 0 ? std::string("Toute la pile") : "Calque " + std::to_string(layer_);
    if (ImGui::BeginCombo("Calque", layer_label.c_str())) {
        if (ImGui::Selectable("Toute la pile", layer_ < 0)) {
            layer_ = -1;
        }
        for (int layer = 0; layer < doc_.layer_count(); ++layer) {
            if (ImGui::Selectable(("Calque " + std::to_string(layer)).c_str(), layer == layer_)) {
                layer_ = layer;
            }
        }
        ImGui::EndCombo();
    }
    brush_stack_.resize(static_cast<std::size_t>(doc_.layer_count()), moteur::kNoTile);
    if (layer_ >= 0) {
        tile_combo("Tuile du pinceau", brush_tile_);
    } else {
        ImGui::TextDisabled("Pile du pinceau (et des rectangles, du remplissage) :");
        for (int layer = 0; layer < doc_.layer_count(); ++layer) {
            ImGui::PushID(layer);
            tile_combo(("Calque " + std::to_string(layer)).c_str(), brush_stack_[static_cast<std::size_t>(layer)]);
            ImGui::PopID();
        }
    }

    // The palette: the legend's stacks.
    ImGui::SeparatorText("Palette (légende)");
    for (const moteur::MapSymbol& symbol : doc_.legend()) {
        std::string label = std::string("'") + symbol.character + "'  ";
        for (std::size_t i = 0; i < symbol.tiles.size(); ++i) {
            const moteur::TileId id = symbol.tiles[i];
            label += (i > 0 ? " + " : "") + (id == moteur::kNoTile || id > types.size() ? std::string("-") : types[id - 1u].name);
        }
        if (symbol.tiles.empty()) {
            label += "(vide)";
        }
        if (!symbol.point.empty()) {
            label += "  ; point " + symbol.point;
        }
        ImGui::PushID(symbol.character);
        if (ImGui::Selectable(label.c_str(), false)) {
            brush_stack_.assign(static_cast<std::size_t>(doc_.layer_count()), moteur::kNoTile);
            for (std::size_t i = 0; i < symbol.tiles.size() && i < brush_stack_.size(); ++i) {
                brush_stack_[i] = symbol.tiles[i];
            }
            if (layer_ >= 0) {
                brush_tile_ = brush_stack_[static_cast<std::size_t>(layer_)];
            }
            if (!symbol.point.empty()) {
                point_name_ = symbol.point;
            }
        }
        ImGui::PopID();
    }
    static char character[2] = "";
    ImGui::SetNextItemWidth(40.0f);
    ImGui::InputText("##caractere", character, sizeof(character));
    ImGui::SameLine();
    if (ImGui::Button("Ajouter la pile du pinceau à la légende") && character[0] != '\0') {
        doc_.set_symbol({character[0], brush_stack(), ""});
        character[0] = '\0';
    }
    ImGui::TextDisabled("(un caractère libre ; sinon l'enregistrement en choisit un)");

    // The tile types: what the grid of the game reads.
    ImGui::SeparatorText("Types de tuiles");
    if (ImGui::BeginTable("types", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Nom");
        ImGui::TableSetupColumn("Marchable");
        ImGui::TableSetupColumn("Opaque");
        ImGui::TableSetupColumn("Région");
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < types.size(); ++i) {
            moteur::MapDocument::TileType type = types[i];
            const auto id = static_cast<moteur::TileId>(i + 1);
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            char name[64];
            std::snprintf(name, sizeof(name), "%s", type.name.c_str());
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText("##nom", name, sizeof(name), ImGuiInputTextFlags_EnterReturnsTrue)) {
                const std::string fresh = name;
                if (!fresh.empty() && doc_.tile_id(fresh) == moteur::kNoTile) {
                    type.name = fresh;
                    doc_.set_tile_type(id, type);
                }
            }
            ImGui::TableNextColumn();
            if (ImGui::Checkbox("##marchable", &type.walkable)) {
                doc_.set_tile_type(id, type);
            }
            ImGui::TableNextColumn();
            if (ImGui::Checkbox("##opaque", &type.opaque)) {
                doc_.set_tile_type(id, type);
            }
            ImGui::TableNextColumn();
            char region[64];
            std::snprintf(region, sizeof(region), "%s", type.region.c_str());
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText("##region", region, sizeof(region), ImGuiInputTextFlags_EnterReturnsTrue)) {
                type.region = region;
                doc_.set_tile_type(id, type);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    static char new_type[64] = "";
    ImGui::InputText("##nouveau type", new_type, sizeof(new_type));
    ImGui::SameLine();
    if (ImGui::Button("Ajouter ce type") && new_type[0] != '\0') {
        doc_.add_tile_type({new_type, true, false, ""});
        new_type[0] = '\0';
    }

    ImGui::SeparatorText("Carte");
    char description[512];
    std::snprintf(description, sizeof(description), "%s", doc_.description.c_str());
    if (ImGui::InputTextMultiline("Description", description, sizeof(description), ImVec2(-1.0f, 50.0f))) {
        doc_.set_description(description);
    }
    track_item("description");
    ImGui::Text("%d x %d cases, %d calques, %zu objets, %zu connecteurs", doc_.width(), doc_.height(), doc_.layer_count(),
                doc_.objects().size(), doc_.connectors().size());
}

void MapEditor::draw_object_panel() {
    if (!ImGui::CollapsingHeader("Objets", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    const std::vector<ObjectType>& types = object_types();
    if (ImGui::BeginCombo("Type à poser", types[static_cast<std::size_t>(object_type_)].label.c_str())) {
        for (std::size_t i = 0; i < types.size(); ++i) {
            if (ImGui::Selectable(types[i].label.c_str(), static_cast<int>(i) == object_type_)) {
                object_type_ = static_cast<int>(i);
                tool_ = Tool::Object;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::Checkbox("Aimanter", &snap_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    ImGui::DragFloat("Pas (m)", &snap_step_, 0.05f, 0.05f, 2.0f, "%.2f");
    ImGui::TextDisabled("Objets : clic pose ; sur un objet : le choisir et le déplacer ; Ctrl+clic : en tirer une copie.");
    ImGui::TextDisabled("[ et ] : tourner de 15 degrés (Maj : 90) ; Suppr : supprimer ; Ctrl+D : dupliquer.");

    const moteur::MapObject* current = doc_.object(selected_);
    if (current == nullptr) {
        ImGui::TextDisabled("Aucun objet choisi.");
        return;
    }
    moteur::MapObject object = *current;
    const ObjectType* type = find_object_type(object.type);
    ImGui::SeparatorText((object.id + " : " + (type != nullptr ? type->label : object.type)).c_str());
    bool changed = false;
    changed |= ImGui::DragFloat3("Position (x, y, z)", &object.position.x, 0.05f);
    track_item("position");
    changed |= ImGui::DragFloat("Rotation (degrés)", &object.rotation, 1.0f, -360.0f, 360.0f);
    track_item("rotation");
    changed |= ImGui::DragFloat("Échelle", &object.scale, 0.01f, 0.05f, 20.0f);
    track_item("échelle");
    if (type != nullptr) {
        object.props = object_props(*type, object.props);
        for (const ObjectProperty& property : type->properties) {
            nlohmann::json& value = object.props[property.key];
            const char* label = property.label.c_str();
            ImGui::PushID(property.key.c_str());
            switch (property.kind) {
                case ObjectProperty::Kind::Number: {
                    float v = value.is_number() ? value.get<float>() : 0.0f;
                    if (ImGui::DragFloat(label, &v, (property.max - property.min) / 200.0f, property.min, property.max)) {
                        value = v;
                        changed = true;
                    }
                    track_item(property.key.c_str());
                    break;
                }
                case ObjectProperty::Kind::Integer: {
                    int v = value.is_number() ? value.get<int>() : 0;
                    if (ImGui::DragInt(label, &v, 0.2f, static_cast<int>(property.min), static_cast<int>(property.max))) {
                        value = v;
                        changed = true;
                    }
                    track_item(property.key.c_str());
                    break;
                }
                case ObjectProperty::Kind::Text: {
                    char text[128];
                    std::snprintf(text, sizeof(text), "%s", value.is_string() ? value.get<std::string>().c_str() : "");
                    if (ImGui::InputText(label, text, sizeof(text), ImGuiInputTextFlags_EnterReturnsTrue)) {
                        value = std::string(text);
                        changed = true;
                    }
                    break;
                }
                case ObjectProperty::Kind::Vec2:
                case ObjectProperty::Kind::Vec3:
                case ObjectProperty::Kind::Color: {
                    const int n = property.kind == ObjectProperty::Kind::Vec2 ? 2 : 3;
                    float v[3] = {0.0f, 0.0f, 0.0f};
                    for (int i = 0; i < n && value.is_array() && i < static_cast<int>(value.size()); ++i) {
                        v[i] = value[static_cast<std::size_t>(i)].get<float>();
                    }
                    bool edited = false;
                    if (property.kind == ObjectProperty::Kind::Color) {
                        edited = ImGui::ColorEdit3(label, v, ImGuiColorEditFlags_Float);
                    } else if (n == 2) {
                        edited = ImGui::DragFloat2(label, v, 0.01f, property.min, property.max);
                    } else {
                        edited = ImGui::DragFloat3(label, v, 0.01f, property.min, property.max);
                    }
                    if (edited) {
                        value = nlohmann::json::array();
                        for (int i = 0; i < n; ++i) {
                            value.push_back(v[i]);
                        }
                        changed = true;
                    }
                    track_item(property.key.c_str());
                    break;
                }
                case ObjectProperty::Kind::Bool: {
                    bool v = value.is_boolean() && value.get<bool>();
                    if (ImGui::Checkbox(label, &v)) {
                        value = v;
                        changed = true;
                    }
                    break;
                }
                case ObjectProperty::Kind::Character:
                case ObjectProperty::Kind::Effect: {
                    const bool effect = property.kind == ObjectProperty::Kind::Effect;
                    const std::string current_value = value.is_string() ? value.get<std::string>() : std::string();
                    const std::vector<std::string>& choices = effect ? effect_names_ : character_names_;
                    if (ImGui::BeginCombo(label, current_value.c_str())) {
                        for (const std::string& choice : choices) {
                            const std::string stored = effect ? "effects/" + choice + ".json" : choice;
                            if (ImGui::Selectable(choice.c_str(), stored == current_value)) {
                                value = stored;
                                changed = true;
                            }
                        }
                        ImGui::EndCombo();
                    }
                    if (!effect && !current_value.empty() &&
                        std::find(choices.begin(), choices.end(), current_value) == choices.end()) {
                        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "« %s » n'est pas dans la table personnages",
                                           current_value.c_str());
                    }
                    break;
                }
            }
            ImGui::PopID();
        }
    }
    if (changed) {
        doc_.update_object(object);
        note_object(object.id);
    }
    if (ImGui::Button("Dupliquer")) {
        duplicate_selected();
    }
    ImGui::SameLine();
    if (ImGui::Button("Supprimer")) {
        delete_selected();
    }
}

void MapEditor::draw_connectors_panel() {
    if (!ImGui::CollapsingHeader("Connecteurs")) {
        return;
    }
    ImGui::TextDisabled("Outil Connecteurs (C) : clic sur une case du bord. Pour assembler des morceaux de carte.");
    char kind[64];
    std::snprintf(kind, sizeof(kind), "%s", connector_kind_.c_str());
    if (ImGui::InputText("Genre des nouveaux", kind, sizeof(kind))) {
        connector_kind_ = kind;
    }
    const std::vector<moteur::MapConnector>& connectors = doc_.connectors();
    for (std::size_t i = 0; i < connectors.size(); ++i) {
        const moteur::MapConnector& c = connectors[i];
        const std::string label = c.name + " (" + c.kind + ") " + std::to_string(c.cell.x) + ", " + std::to_string(c.cell.y) + " " +
                                  kDirections[static_cast<int>(c.direction)] + "##" + std::to_string(i);
        if (ImGui::Selectable(label.c_str(), static_cast<int>(i) == connector_)) {
            connector_ = static_cast<int>(i);
            camera_.set_target({static_cast<float>(c.cell.x) + 0.5f, 0.0f, static_cast<float>(c.cell.y) + 0.5f});
        }
    }
    if (connector_ < 0 || connector_ >= static_cast<int>(connectors.size())) {
        return;
    }
    std::vector<moteur::MapConnector> next = connectors;
    moteur::MapConnector& c = next[static_cast<std::size_t>(connector_)];
    bool changed = false;
    char name[64];
    std::snprintf(name, sizeof(name), "%s", c.name.c_str());
    if (ImGui::InputText("Nom", name, sizeof(name), ImGuiInputTextFlags_EnterReturnsTrue)) {
        c.name = name;
        changed = true;
    }
    std::snprintf(kind, sizeof(kind), "%s", c.kind.c_str());
    if (ImGui::InputText("Genre", kind, sizeof(kind), ImGuiInputTextFlags_EnterReturnsTrue)) {
        c.kind = kind;
        changed = true;
    }
    int direction = static_cast<int>(c.direction);
    if (ImGui::Combo("Direction", &direction, kDirections, IM_ARRAYSIZE(kDirections))) {
        c.direction = static_cast<moteur::MapConnector::Direction>(direction);
        changed = true;
    }
    if (ImGui::Button("Supprimer le connecteur")) {
        next.erase(next.begin() + connector_);
        connector_ = -1;
        changed = true;
    }
    if (changed) {
        doc_.set_connectors(std::move(next));
    }
}

void MapEditor::draw_issues_panel() {
    int errors = 0;
    for (const moteur::MapDocument::Issue& issue : issues_) {
        errors += issue.level == moteur::MapDocument::Issue::Level::Error ? 1 : 0;
    }
    const std::string header = "Vérifications (" + std::to_string(errors) + " erreur(s), " +
                               std::to_string(issues_.size() - static_cast<std::size_t>(errors)) + " avertissement(s))###verifs";
    if (!ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    if (issues_.empty()) {
        ImGui::TextDisabled("Rien à signaler.");
    }
    for (std::size_t i = 0; i < issues_.size(); ++i) {
        const moteur::MapDocument::Issue& issue = issues_[i];
        const bool error = issue.level == moteur::MapDocument::Issue::Level::Error;
        ImGui::PushStyleColor(ImGuiCol_Text, error ? ImVec4(1.0f, 0.45f, 0.35f, 1.0f) : ImVec4(1.0f, 0.75f, 0.35f, 1.0f));
        const std::string label = issue.message + "##issue" + std::to_string(i);
        if (ImGui::Selectable(label.c_str()) && !issue.cells.empty()) {
            const glm::ivec2 cell = issue.cells.front();
            camera_.set_target({static_cast<float>(cell.x) + 0.5f, 0.0f, static_cast<float>(cell.y) + 0.5f});
        }
        ImGui::PopStyleColor();
    }
}

void MapEditor::draw_popups() {
    if (open_new_) {
        ImGui::OpenPopup("Nouvelle carte");
        open_new_ = false;
    }
    if (open_resize_) {
        ImGui::OpenPopup("Redimensionner");
        open_resize_ = false;
    }
    if (open_save_as_) {
        std::snprintf(name_buffer_, sizeof(name_buffer_), "%s", name_.empty() ? "nouvelle_carte" : name_.c_str());
        ImGui::OpenPopup("Enregistrer sous");
        open_save_as_ = false;
    }
    if (confirm_overwrite_) {
        ImGui::OpenPopup("Fichier modifié ailleurs");
        confirm_overwrite_ = false;
    }
    if (confirm_quit_) {
        ImGui::OpenPopup("Quitter sans enregistrer");
        confirm_quit_ = false;
    }
    if (confirm_open_) {
        ImGui::OpenPopup("Abandonner les modifications");
        confirm_open_ = false;
    }
    const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();

    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Nouvelle carte", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputInt2("Largeur, hauteur", new_size_);
        ImGui::SliderInt("Calques", &new_size_[2], 1, 4);
        new_size_[0] = std::clamp(new_size_[0], 4, 1024);
        new_size_[1] = std::clamp(new_size_[1], 4, 1024);
        ImGui::TextDisabled("Un sol partout, un type « mur » et le point de départ au milieu.");
        if (ImGui::Button("Créer")) {
            moteur::MapDocument fresh(new_size_[0], new_size_[1], new_size_[2], "sol");
            fresh.add_tile_type({"mur", false, true, ""});
            fresh.set_symbol({'.', {fresh.tile_id("sol")}, ""});
            if (new_size_[2] >= 2) {
                fresh.set_symbol({'#', {fresh.tile_id("sol"), fresh.tile_id("mur")}, ""});
            }
            fresh.set_point({new_size_[0] / 2, new_size_[1] / 2}, "depart");
            adopt(std::move(fresh));
            name_.clear();
            file_time_ = 0;
            external_change_ = recovery_found_ = false;
            saved_revision_ = doc_.revision();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Annuler")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Redimensionner", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Cases ajoutées (positif) ou retirées (négatif) de chaque côté :");
        ImGui::InputInt("Gauche", &resize_[0]);
        ImGui::InputInt("Haut", &resize_[1]);
        ImGui::InputInt("Droite", &resize_[2]);
        ImGui::InputInt("Bas", &resize_[3]);
        const int width = doc_.width() + resize_[0] + resize_[2];
        const int height = doc_.height() + resize_[1] + resize_[3];
        ImGui::Text("Nouvelle taille : %d x %d (les cases nouvelles reçoivent la pile du pinceau)", width, height);
        const bool valid = width >= 1 && height >= 1 && width <= 1024 && height <= 1024;
        ImGui::BeginDisabled(!valid);
        if (ImGui::Button("Appliquer")) {
            doc_.begin_action("redimensionnement");
            doc_.resize(resize_[0], resize_[1], resize_[2], resize_[3], brush_stack());
            doc_.end_action();
            force_rebuild_ = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Annuler")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Enregistrer sous", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Nom (minuscules, chiffres, _ et -)", name_buffer_, sizeof(name_buffer_));
        const std::string name = name_buffer_;
        const bool valid = valid_name(name);
        if (valid && std::find(map_names_.begin(), map_names_.end(), name) != map_names_.end() && name != name_) {
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Une carte de ce nom existe : elle sera remplacée (après confirmation).");
        }
        ImGui::BeginDisabled(!valid);
        if (ImGui::Button("Enregistrer")) {
            if (name != name_) {
                name_ = name;
                file_time_ = 0;  // a file of that name, if any, is someone else's: asks before writing over it
            }
            ImGui::CloseCurrentPopup();
            save(false);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Annuler")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Fichier modifié ailleurs", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s a changé (ou existe déjà) depuis que l'éditeur l'a lu.", map_path(name_).c_str());
        if (ImGui::Button("Écraser avec ma version")) {
            save(true);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Annuler")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Quitter sans enregistrer", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("La carte a des modifications non enregistrées.");
        const auto leave = [&] {
            if (standalone_) app_.quit();
            else stop_requested_ = true;
        };
        if (ImGui::Button("Enregistrer et quitter")) {
            if (save(false)) {
                leave();
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Quitter (une copie est gardée)")) {
            leave();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Annuler")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Abandonner les modifications", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Ouvrir « %s » abandonne les modifications non enregistrées.", pending_open_.c_str());
        if (ImGui::Button("Ouvrir quand même")) {
            saved_revision_ = doc_.revision();  // no copy kept: the choice was made
            open(pending_open_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Annuler")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// --- Self-test --------------------------------------------------------------------------------

// The editor's workflow without a hand on the mouse: what the panels and the tools call, step by
// step (one step per update, some waiting for the view or the checks to follow).
void MapEditor::run_self_test() {
    const auto check = [&](bool ok, const char* what) {
        if (!ok) {
            ++test_failures_;
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "editor self-test: FAILED: %s", what);
        }
    };
    if (test_wait_ > 0) {
        --test_wait_;
        return;
    }
    const std::string name = "essai_editeur";
    if (test_step_ == 0 && built_revision_ == ~0ull) {
        return;  // the view is built first (its time is one of the measures)
    }
    switch (test_step_++) {
        case 0: {  // a stroke of the brush: one step, undone and redone whole
            test_start_ns_ = SDL_GetTicksNS();
            test_start_frame_ = rendered_;
            SDL_Log("editor self-test: %d x %d cells, %zu objects; the whole view built in %.1f ms", doc_.width(), doc_.height(),
                    doc_.objects().size(), full_build_ms_);
            check(doc_.width() > 8 && doc_.height() > 8, "a map is open");
            const moteur::TileId wall = doc_.tile_id("mur");
            check(wall != moteur::kNoTile, "the map has walls");
            tool_ = Tool::Brush;
            layer_ = -1;
            brush_size_ = 1;
            brush_stack_.assign(static_cast<std::size_t>(doc_.layer_count()), moteur::kNoTile);
            brush_stack_[0] = doc_.tile_id("sol");
            if (brush_stack_.size() > 1) {
                brush_stack_[1] = wall;
            }
            test_before_ = doc_.stack({4, 3});
            doc_.begin_action("pinceau");
            for (const glm::ivec2 c : moteur::MapDocument::line({2, 3}, {6, 3})) {
                paint(c);
            }
            doc_.end_action();
            check(doc_.stack({4, 3}) == brush_stack(), "the stroke painted its cells");
            break;
        }
        case 1: {
            check(built_revision_ == doc_.revision(), "the view is up to date");
            SDL_Log("editor self-test: a stroke of 5 cells built again in %.2f ms (only its blocks)", partial_build_ms_);
            undo();
            check(doc_.stack({4, 3}) == test_before_, "one undo takes the whole stroke away");
            redo();
            check(doc_.stack({4, 3}) == brush_stack(), "redo brings it back");
            // An object placed, turned, duplicated, deleted; undo brings everything back.
            const std::size_t count = doc_.objects().size();
            object_type_ = 0;
            place_object({3.0f, 0.0f, 5.0f});
            check(doc_.objects().size() == count + 1 && doc_.object(selected_) != nullptr, "an object is placed and chosen");
            const std::string id = selected_;
            check(pick_object({3.1f, 0.0f, 5.1f}) == id, "the pointer finds it");
            turn_selected(15.0f);
            check(std::abs(doc_.object(id)->rotation - 15.0f) < 1e-4f, "it turns");
            duplicate_selected();
            check(doc_.objects().size() == count + 2 && selected_ != id, "it is duplicated");
            delete_selected();
            check(doc_.objects().size() == count + 1, "the copy is deleted");
            undo();
            check(doc_.objects().size() == count + 2, "undo brings the copy back");
            undo();
            undo();
            undo();
            check(doc_.objects().size() == count, "and undoes the rest");
            redo();
            check(doc_.objects().size() == count + 1, "redo places it again");
            issues_timer_ = 0.0;
            test_wait_ = 3;
            break;
        }
        case 2: {
            check(issues_revision_ == doc_.revision(), "the checks followed the edits");
            // Saved under a new name: the file reads back as the same map.
            name_ = name;
            file_time_ = moteur::file_time(map_path(name));  // a leftover of an earlier run is ours
            check(save(false), "the map is saved");
            check(!modified(), "nothing left to save");
            try {
                const moteur::MapData back = moteur::MapData::parse(moteur::read_text_file(map_path(name)), name);
                check(moteur::MapDocument::from(back).to_json() == doc_.to_json(), "the file reads back as the same map");
            } catch (const std::exception& e) {
                check(false, e.what());
            }
            // Someone else writes the file: noticed, and not overwritten without asking.
            std::string error;
            SDL_Delay(20);
            check(moteur::write_file_atomic(map_path(name), doc_.to_json() + "\n", error), "another program writes the file");
            check_timer_ = 0.0;
            break;
        }
        case 3: {
            check(external_change_, "the change on the disk is noticed");
            doc_.set_description("modifiée par le test");
            check(!save(false), "saving asks before overwriting it");
            check(confirm_overwrite_, "with the question");
            confirm_overwrite_ = false;
            check(save(true), "and overwrites when asked to");
            check(!external_change_ && !modified(), "after which all is in order");
            const std::string saved = doc_.to_json();
            check(open(name), "the map opens again");
            check(doc_.to_json() == saved, "as it was saved");
            trial_request_ = 1;
            break;
        }
        case 4:
            check(trial_ != nullptr, "\"Essayer\" starts the Monde scene on the map");
            test_wait_ = 30;
            break;
        case 5:
            end_trial_ = true;
            break;
        case 6: {
            check(trial_ == nullptr, "and comes back to the editor");
            const double seconds = static_cast<double>(SDL_GetTicksNS() - test_start_ns_) * 1e-9;
            const long frames = rendered_ - test_start_frame_;
            SDL_Log("editor self-test: %ld frames in %.2f s (%.1f ms per frame, the trial included)", frames, seconds,
                    frames > 0 ? seconds * 1000.0 / static_cast<double>(frames) : 0.0);
            std::error_code ignored;
            std::filesystem::remove(utf8_path(map_path(name)), ignored);
            std::filesystem::remove(utf8_path(recovery_path()), ignored);
            SDL_Log("editor self-test: %s (%d failure(s))", test_failures_ == 0 ? "ok" : "FAILED", test_failures_);
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
