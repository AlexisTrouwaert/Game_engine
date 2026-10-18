#pragma once

#include <glm/glm.hpp>

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "moteur/application.hpp"
#include "moteur/camera3d.hpp"
#include "moteur/collision_scene.hpp"
#include "moteur/environment.hpp"
#include "moteur/gizmo.hpp"
#include "moteur/jobs.hpp"
#include "moteur/level_bake.hpp"
#include "moteur/level_edit.hpp"
#include "moteur/navigation.hpp"
#include "moteur/thumbnails.hpp"
#include "moteur/world.hpp"

#include "sandbox_scene.hpp"

class Demo3D;

// The level editor (milestone 9, part 10): the socle of the production editor of milestone 11. It
// edits a level of version 3 (level.hpp) with the game's real assets and shows it as the game does.
//
// - Asset library: the prefabs and models of every asset folder (the engine's, the game's mounted
//   ones), by folder and by kind (props, ground, lights, kit pieces by family from the kits'
//   .kit.json), searched by name, each with a thumbnail drawn by the engine (ThumbnailRenderer,
//   kept on disk). A prefab is dragged into the view, or chosen then placed with clicks. A model
//   without a prefab gets one with a button.
// - Placement in 3D: select (click, Ctrl+click adds, Shift+click toggles), move / turn / scale with
//   the handles (W / E / R), snapped to the grid of the piece's kit (or the editor's) and to the
//   surfaces under it, duplicate (Ctrl+D), delete, undo and redo (Ctrl+Z / Ctrl+Y: EditableLevel).
// - Ground: the ground prefabs of the level painted cell by cell (brush, eraser), the grid grows
//   where one paints.
// - Lights: a light prefab placed as any other; the fields of a placed prefab (its light: color,
//   intensity, range, shadows, shadow_near) edited in the inspector, kept as the instance's
//   overrides.
// - The game's rendering: the same meshes, materials, lights and shadows, the level's lighting
//   (apply_lighting()), and a night view to judge the lights.
// - Navigation and collision shown: the navigation mesh built again in the background after each
//   change, the surfaces nothing walks on (walls, obstacles) shown.
// - "Essayer" (F5; F6 from the pointer): the slice played on the level as it is, baked in memory
//   (saved or not), from the chosen point, by day or by night; Escape comes back.
// - Saving in version 3 (an atomic write; a change on disk by someone else is noticed); a copy is
//   kept in the player's preferences when the editor closes with changes not saved.
class LevelEditor final : public SandboxScene {
public:
    struct Options {
        std::string level = "levels/editeur_essai.level.json";  // an asset key; empty: a new level
        double run_seconds = 0.0;
        std::string capture_path;  // one capture of the view (no panels) once the level is built
        // The automatic test of the editor's workflow (a lamp placed, ground painted, moved with the
        // handles, undo and redo, the navigation built, saved and opened again, baked, "Essayer"),
        // on a copy of the level in `levels_directory`.
        bool self_test = false;
        std::string levels_directory;  // empty: levels_directory(app)
        std::optional<glm::vec3> focus;  // the view framed there at the start, else the whole level
        float focus_height = 16.0f;      // metres seen from top to bottom, with `focus`
        bool show_collision = false;     // the walls and obstacles drawn from the start
    };

    LevelEditor(moteur::Application& app, const Options& options, bool standalone);
    ~LevelEditor() override;

    void on_event(const SDL_Event& event) override;
    void update(double dt) override;
    void render(moteur::Renderer& renderer, double alpha) override;
    void draw_controls() override;
    bool stop_requested() const override { return stop_requested_; }
    bool uses_escape() const override { return true; }
    // After a self-test: the checks that failed (0: passed).
    int self_test_failures() const { return test_failures_; }

    // Where the levels are read and written: assets/levels of the source tree when the program was
    // built from it, else of the program.
    static std::string levels_directory(moteur::Application& app);

private:
    enum class Tool { Select, Place, Paint, Erase, Point };

    // An asset the library shows.
    struct LibraryItem {
        std::string key;     // the prefab's key (or the model's, for a model without prefab)
        std::string name;    // the file's name without its extensions
        std::string folder;  // the key's folder
        std::string family;  // a kit's family ("Façades gothiques"), else empty
        bool prefab = true;  // false: a model without a prefab
        bool light = false;  // a prefab with a light
        bool ground = false; // a ground prefab (a tile of the painted ground)
        float grid = 0.0f;   // the snapping step of its kit; 0: the editor's
        std::uint64_t fingerprint = 0;  // its files' sizes and times: a changed asset gets a new thumbnail
    };

    // --- File
    bool open(const std::string& key);
    void adopt(moteur::LevelDocument document);  // everything built again, history cleared
    bool save(bool overwrite);
    std::string level_path(const std::string& key) const;
    std::string recovery_path() const;
    bool modified() const { return level_.revision() != saved_revision_; }
    nlohmann::json level_json() const;
    void refresh_levels();

    // --- Library
    void scan_library();
    void draw_library();
    void ensure_thumbnails();  // a few per frame, outside a frame's recording
    bool make_prefab_for_model(const LibraryItem& item);

    // --- Edits
    void place(const std::string& prefab, glm::vec3 position);
    void select(std::uint32_t id, bool add, bool toggle);
    void duplicate_selection();
    void delete_selection();
    void apply_gizmo(const moteur::GizmoDelta& delta);
    void undo();
    void redo();
    glm::vec3 drop_point(glm::vec2 pointer) const;  // under the pointer: a surface, else the ground plane
    float snap_step() const;                        // of the selection's kit, else the editor's
    void paint_at(glm::vec3 point, bool erase);
    void track_item(const char* action);  // the widget just drawn: one undo step while it is held

    // --- View
    void on_change(const moteur::LevelChange& change);
    void rebuild_instance(std::uint32_t id);
    void rebuild_ground();
    void rebuild_all();
    void rebuild_collision();  // the scene, from every instance's collision
    void start_navigation();   // in the background, from the scene
    void finish_navigation();
    void frame_level();
    glm::vec3 selection_centre() const;
    void update_camera(float dt);
    void draw_overlays(moteur::Renderer& renderer, const moteur::Camera3D& camera);

    // --- Panels
    void draw_panels();
    void draw_menu();
    void draw_toolbar();
    void draw_inspector();
    void draw_level_settings();
    void draw_popups();
    void draw_gizmo(const moteur::Camera3D& camera);

    // --- "Essayer"
    void start_trial(bool at_pointer);

    void run_self_test();
    void check(bool condition, const char* what);

    moteur::Application& app_;
    Options options_;
    bool standalone_;
    bool stop_requested_ = false;
    double elapsed_ = 0.0;
    long frames_ = 0;
    bool capture_done_ = false;

    // The level and its file.
    moteur::EditableLevel level_;
    std::string key_;   // asset key; empty: not saved yet
    std::string levels_dir_;
    moteur::LevelSources sources_;  // the prefabs and models' collision, for the view and the bake
    std::int64_t file_time_ = 0;
    bool external_change_ = false;
    double check_timer_ = 0.0;
    std::uint64_t saved_revision_ = 0;
    std::string status_;
    bool status_error_ = false;
    bool recovery_found_ = false;
    std::vector<std::string> level_names_;  // files of levels_dir_

    // The library.
    std::vector<LibraryItem> library_;
    std::map<std::string, float> kit_grids_;          // prefab key -> grid of its kit
    std::map<std::string, std::string> kit_families_;  // prefab key -> family of its kit
    char search_[96] = "";
    int library_filter_ = 0;  // all, props, ground, lights, kits, models
    std::string chosen_;      // the prefab placed by the Place tool
    std::unique_ptr<moteur::ThumbnailRenderer> thumbnail_renderer_;
    std::unique_ptr<moteur::ThumbnailCache> thumbnails_;
    std::set<std::string> thumbnails_wanted_;  // keys shown without a thumbnail this frame
    std::set<std::string> thumbnails_failed_;
    int thumbnails_drawn_ = 0;

    // Tools.
    Tool tool_ = Tool::Select;
    moteur::GizmoMode gizmo_mode_ = moteur::GizmoMode::Translate;
    moteur::Gizmo gizmo_;
    moteur::GizmoHandle hovered_handle_ = moteur::GizmoHandle::None;
    std::vector<std::uint32_t> selection_;
    std::map<std::uint32_t, moteur::Transform> drag_start_;  // the selection's transforms when the drag began
    glm::vec3 drag_pivot_{0.0f};
    bool snap_ = true;
    float grid_step_ = 0.5f;
    float angle_step_ = 15.0f;
    bool surface_snap_ = true;
    std::uint8_t ground_brush_ = 1;  // index + 1 of the ground prefab painted
    int brush_size_ = 1;
    bool painting_ = false;
    std::string point_name_ = "start";
    bool night_ = false;
    bool show_navigation_ = true;
    bool show_collision_ = false;
    bool show_grid_ = true;
    std::string layer_ = "default";
    bool widget_action_ = false;
    std::optional<glm::vec2> pending_pick_;  // a click waiting for the id pass's answer
    int pending_pick_frames_ = 0;
    bool pick_add_ = false, pick_toggle_ = false;
    std::string dragged_asset_;  // an asset dragged from the library
    std::vector<char> properties_text_ = std::vector<char>(8192, '\0');  // the selected instance's properties, as edited
    std::uint32_t properties_of_ = 0;  // the instance properties_text_ was filled from
    std::string properties_error_;

    // Camera.
    moteur::Camera3D camera_;
    float yaw_ = 45.0f;
    float pitch_ = 50.0f;
    bool orbiting_ = false;
    bool panning_ = false;
    glm::vec2 last_mouse_{0.0f};
    std::optional<glm::vec3> pointer_point_;  // under the pointer this frame

    // The view: one root entity per instance, the ground's tiles, the collision of each instance.
    moteur::World world_;
    std::unordered_map<std::uint32_t, entt::entity> roots_;
    std::unordered_map<std::uint32_t, moteur::CollisionMeshData> instance_collision_;
    std::unordered_map<std::uint32_t, moteur::Aabb> instance_bounds_;
    moteur::CollisionMeshData ground_collision_;
    std::vector<entt::entity> ground_entities_;
    std::set<std::uint32_t> dirty_instances_;
    bool ground_dirty_ = true;
    bool collision_dirty_ = true;
    double navigation_timer_ = 0.0;  // seconds since the last change: built again after a pause
    bool navigation_wanted_ = true;
    std::optional<moteur::Environment> sky_;
    moteur::CollisionScene scene_;
    std::vector<moteur::NavMesh> navigation_;
    std::shared_ptr<std::vector<moteur::NavMesh>> navigation_result_;
    std::shared_ptr<std::string> navigation_error_;
    moteur::JobHandle navigation_job_;
    bool navigation_running_ = false;
    std::shared_ptr<double> navigation_spent_ = std::make_shared<double>(0.0);  // the build's own time, in milliseconds
    double navigation_ms_ = 0.0;
    double build_ms_ = 0.0;  // the last full build of the view
    int instance_issues_ = 0;
    std::string last_issue_;

    // Popups.
    bool open_new_ = false, open_save_as_ = false, confirm_overwrite_ = false, confirm_quit_ = false, confirm_open_ = false;
    std::string pending_open_;
    char name_buffer_[64] = "nouveau_niveau";

    // "Essayer".
    std::unique_ptr<Demo3D> trial_;  // the slice on the level being edited
    int trial_request_ = 0;  // 1: at the start point, 2: at the pointer
    bool end_trial_ = false;

    // Self-test.
    int test_step_ = 0;
    int test_wait_ = 0;
    int test_wait_frames_ = 0;
    int test_failures_ = 0;
    std::uint32_t test_lamp_ = 0;
    std::string test_text_;
};
