#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "moteur/application.hpp"
#include "moteur/camera3d.hpp"
#include "moteur/environment.hpp"
#include "moteur/map_document.hpp"
#include "moteur/mesh.hpp"
#include "moteur/world.hpp"

#include "map_objects.hpp"
#include "sandbox_scene.hpp"

class WorldTest;

// The map editor (milestone 7, parts 7 and 8): a tool of the engine, in the sandbox's menu
// (DEBUG > Tests moteur) and as its own program (editeur, or bac_a_sable --editeur). It edits a
// MapDocument and writes the maps of assets/maps back into the source tree.
//
// - The map in 3D as the game builds it (floor, walls, objects), seen from above or at an angle.
// - Tools: brush (strokes without holes, size 1 to 9), filled or hollow rectangle, fill, eraser,
//   picker, named points, objects (place, select, move, turn, scale, duplicate, delete, their
//   properties from map_objects), connectors of a chunk.
// - Brush on every layer at once (the stack of a cell) or on one layer; the palette is the map's
//   legend; tile types and legend edited in the panel.
// - Undo and redo of whole actions (a brush stroke is one step), Ctrl+Z / Ctrl+Y.
// - Checks while editing: cells that cannot be reached from the start, points or objects outside.
// - Saving with an atomic write; a change of the file by someone else is noticed (the file is not
//   overwritten without asking). A copy is kept in the player's preferences if the editor closes
//   with changes not saved, and offered when the map is opened again.
// - "Essayer" (F5, or F6 with the hero under the pointer): the "Monde" scene on the map as it is,
//   saved or not; Escape comes back to the editor.
class MapEditor final : public SandboxScene {
public:
    struct Options {
        std::string map = "salle_portes";  // a file of assets/maps, without ".json"; empty: a new map
        double run_seconds = 0.0;          // > 0: quits (standalone) or asks to stop (menu)
        std::string capture_path;          // one capture of the view (no panels) after a few frames
        bool top_view = false;
        // The automatic test of the editor's workflow (edits, undo, objects, saving, a file changed
        // by someone else, "Essayer"), with the maps read and written in `maps_directory`.
        bool self_test = false;
        std::string maps_directory;  // empty: maps_directory(app)
    };

    MapEditor(moteur::Application& app, const Options& options, bool standalone);
    ~MapEditor() override;

    void on_event(const SDL_Event& event) override;
    void update(double dt) override;
    void render(moteur::Renderer& renderer, double alpha) override;
    void draw_controls() override;
    bool stop_requested() const override { return stop_requested_; }
    bool uses_escape() const override { return true; }
    // After a self-test: the checks that failed (0: passed).
    int self_test_failures() const { return test_failures_; }

    // Where the maps are read and written: assets/maps of the source tree when the program was
    // built from it (the assets one edits), else of the program.
    static std::string maps_directory(moteur::Application& app);

private:
    enum class Tool { Select, Brush, Eraser, Rect, HollowRect, Fill, Picker, Point, Object, Connector };
    // What the view shows of a cell, from its stack of tiles.
    struct CellLook {
        bool any = false;  // some tile: a floor
        bool walkable = false;
        bool opaque = false;
        bool pillar = false;
        moteur::TileId ground = moteur::kNoTile;  // the tile of layer 0
    };

    // --- File
    bool open(const std::string& name);
    void adopt(moteur::MapDocument document);  // a new document: view rebuilt, history cleared
    bool save(bool overwrite);
    std::string map_path(const std::string& name) const;
    std::string recovery_path() const;
    bool modified() const { return doc_.revision() != saved_revision_; }
    void refresh_lists();

    // --- Tools
    void tool_press(bool shift, bool ctrl);
    void tool_drag();
    void tool_release();
    void paint(glm::ivec2 cell);
    std::vector<moteur::TileId> brush_stack() const;
    std::string pick_object(glm::vec3 ground) const;
    glm::vec3 snapped(glm::vec3 position) const;
    void place_object(glm::vec3 position);
    void duplicate_selected();
    void delete_selected();
    void turn_selected(float degrees);
    void undo();
    void redo();
    void start_trial(bool at_pointer);

    // --- View
    void note_cells(glm::ivec2 a, glm::ivec2 b);  // what an edit changed: only these blocks are built again
    void note_object(const std::string& id);
    void rebuild();
    void rebuild_block(int bx, int by);
    void rebuild_object(const std::string& id);
    void clear_view();
    CellLook look(glm::ivec2 cell) const;
    void update_pointer();
    void frame_map();
    void draw_overlays(moteur::Renderer& renderer, const moteur::Camera3D& camera);
    void draw_labels(const moteur::Camera3D& camera);

    void run_self_test();

    // --- Panels
    void draw_panels();
    void draw_menu();
    void draw_tools_panel();
    void draw_tiles_panel();
    void draw_object_panel();
    void draw_connectors_panel();
    void draw_issues_panel();
    void draw_popups();
    void track_item(const char* action);  // the widget just drawn: one undo step while it is held

    moteur::Application& app_;
    Options options_;
    bool standalone_;
    bool stop_requested_ = false;
    double elapsed_ = 0.0;
    long frames_ = 0;
    bool capture_done_ = false;

    // The document and its file.
    moteur::MapDocument doc_;
    std::string name_;  // file name without ".json"; empty: not saved yet
    std::int64_t file_time_ = 0;
    bool external_change_ = false;
    double check_timer_ = 0.0;
    std::uint64_t saved_revision_ = 0;
    std::string status_;
    bool status_error_ = false;
    bool recovery_found_ = false;
    std::vector<std::string> map_names_;
    std::vector<std::string> effect_names_;
    std::vector<std::string> character_names_;

    // Checks.
    std::vector<moteur::MapDocument::Issue> issues_;
    std::vector<std::uint8_t> issue_cells_;  // 1: an error, 2: a warning (row after row)
    std::uint64_t issues_revision_ = ~0ull;
    double issues_timer_ = 0.0;
    bool show_issues_ = true;

    // Tools.
    Tool tool_ = Tool::Brush;
    Tool before_picker_ = Tool::Brush;
    int layer_ = -1;  // -1: the whole stack
    int brush_size_ = 1;
    std::vector<moteur::TileId> brush_stack_;
    moteur::TileId brush_tile_ = moteur::kNoTile;  // with one layer
    std::string point_name_ = "depart";
    int object_type_ = 0;
    std::string selected_;  // an object's id
    bool snap_ = true;
    float snap_step_ = 0.25f;
    int connector_ = -1;  // selected, in doc_.connectors()
    std::string connector_kind_ = "passage";
    bool painting_ = false;
    glm::ivec2 last_cell_{0};
    std::optional<glm::ivec2> rect_start_;
    bool moving_ = false;
    glm::vec2 grab_offset_{0.0f};
    bool widget_action_ = false;
    bool show_grid_ = true;
    bool show_labels_ = true;

    // Pointer and camera.
    moteur::Camera3D camera_;
    bool top_view_ = false;
    bool framed_ = false;  // the first frame framed the whole map with the window's proportions
    std::optional<glm::ivec2> hovered_;
    std::optional<glm::vec3> ground_;
    bool panning_ = false;
    glm::vec3 pan_anchor_{0.0f};

    // The view: the floor and what stands on the cells by blocks of 16 x 16 cells; the objects.
    moteur::World world_;
    ObjectMeshes meshes_;
    moteur::Asset<moteur::Mesh> cube_;
    moteur::Asset<moteur::Mesh> tile_;
    std::optional<moteur::Environment> sky_;
    int blocks_x_ = 0, blocks_y_ = 0;
    std::vector<std::vector<entt::entity>> blocks_;
    std::unordered_map<std::string, std::vector<entt::entity>> object_entities_;
    std::vector<entt::entity> ground_entities_;  // the dark bottom under the map
    std::vector<std::uint8_t> dirty_blocks_;
    std::vector<std::string> dirty_objects_;
    std::uint64_t built_revision_ = ~0ull;
    std::uint64_t noted_revision_ = ~0ull;  // the revision the notes describe (else: everything again)
    bool force_rebuild_ = true;

    // Popups.
    bool open_new_ = false, open_resize_ = false, open_save_as_ = false, confirm_overwrite_ = false, confirm_quit_ = false,
         confirm_open_ = false;
    std::string pending_open_;
    char name_buffer_[64] = "nouvelle_carte";
    int new_size_[3] = {40, 30, 2};  // width, height, layers
    int resize_[4] = {0, 0, 0, 0};   // left, top, right, bottom

    // Self-test.
    std::string maps_dir_;
    int test_step_ = 0;
    int test_wait_ = 0;
    int test_failures_ = 0;
    // Measures (milestone 7, part 10): the last build of the whole view, and of what an edit touched.
    double full_build_ms_ = 0.0;
    double partial_build_ms_ = 0.0;
    long rendered_ = 0;  // frames drawn since the editor opened
    std::uint64_t test_start_ns_ = 0;
    long test_start_frame_ = 0;
    std::vector<moteur::TileId> test_before_;

    // "Essayer".
    std::unique_ptr<WorldTest> trial_;
    int trial_request_ = 0;  // 1: at the start, 2: at the pointer (started in update, outside a frame)
    bool end_trial_ = false;
};
