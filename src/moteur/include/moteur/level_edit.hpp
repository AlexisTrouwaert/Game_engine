#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "moteur/level.hpp"

namespace moteur {

// Editing a level (milestone 9, part 10): every change the level editor makes goes through an
// EditableLevel, which records it for undo and tells the editor's view what to build again. Plain
// CPU code, tested without a GPU.

/// One thing a change touched, given to the listener (EditableLevel::set_listener()).
struct LevelChange {
    /// What was changed.
    enum class Kind {
        Instance,  ///< `id`: added, changed or removed (look it up: absent means removed)
        Point,     ///< `name`: added, changed or removed
        Volume,    ///< `id`
        Ground,    ///< the painted ground (its cells, its prefabs, its grid)
        Settings,  ///< description, layers, lighting, chunk size, navigation
    };
    Kind kind = Kind::Instance;  ///< what was changed
    std::uint32_t id = 0;        ///< Instance, Volume
    std::string name;            ///< Point
};

/// The level's settings, changed as one (EditableLevel::set_settings()).
struct LevelSettings {
    std::string description;  ///< LevelDocument::description
    float chunk_size = 32.0f;  ///< LevelDocument::chunk_size
    std::vector<LevelLayer> layers;  ///< LevelDocument::layers (at least one)
    LevelLighting lighting;  ///< LevelDocument::lighting
    std::vector<NavAgent> agents;  ///< LevelDocument::agents (at least one)
    NavBuildSettings navigation;  ///< LevelDocument::navigation

    /// Equal field by field.
    bool operator==(const LevelSettings& other) const;
};

/// The cell of `ground` holding the point (x, z), which may be outside its grid (negative, or past
/// its size).
glm::ivec2 ground_cell(const LevelGround& ground, glm::vec3 point);

/// Grows `ground` so that cells `from` to `to` (inclusive, in its current numbering) are in its
/// grid: its origin moves for cells below 0, the cells keep their place in the world. Returns how
/// far the numbering moved (add it to a cell of the old numbering).
glm::ivec2 grow_ground(LevelGround& ground, glm::ivec2 from, glm::ivec2 to);

/// A level and its undo history. The changes made between begin_action() and end_action() are one
/// step of the history (a drag, a brush stroke, a paste); a change made outside an action is a step
/// of its own. Changes to the same instance (or point, volume, ground) inside one step merge: a drag
/// over hundreds of frames is one change. The history keeps the last 500 steps.
class EditableLevel {
public:
    /// Starts with `document`, nothing to undo.
    explicit EditableLevel(LevelDocument document = {});

    /// The level as it is now.
    const LevelDocument& document() const { return document_; }
    /// Counts the changes (undo and redo included): what the editor compares to know whether the
    /// level was saved.
    std::uint64_t revision() const { return revision_; }
    /// Called once for each thing a change, an undo or a redo touched, after it is done.
    void set_listener(std::function<void(const LevelChange&)> listener) { listener_ = std::move(listener); }

    /// Adds an instance, with the next identifier; returns it.
    std::uint32_t add(LevelInstance instance);
    /// Replaces the instance with the same identifier. False when there is none, or it is the same.
    bool set(const LevelInstance& instance);
    /// Removes an instance. False when there is none.
    bool remove(std::uint32_t id);
    /// Adds a point, or replaces the one with the same name. False when it is the same.
    bool set_point(const LevelPoint& point);
    /// Removes a point. False when there is none.
    bool remove_point(const std::string& name);
    /// Adds a volume, with the next identifier; returns it.
    std::uint32_t add(LevelVolume volume);
    /// Replaces the volume with the same identifier. False when there is none, or it is the same.
    bool set(const LevelVolume& volume);
    /// Removes a volume. False when there is none.
    bool remove_volume(std::uint32_t id);
    /// Sets a cell of the painted ground to `value` (the index + 1 of one of its prefabs, 0: none),
    /// growing the grid when the cell is outside it. False when unchanged, or `value` names no prefab.
    bool paint_ground(glm::ivec2 cell, std::uint8_t value);
    /// Replaces the whole ground (its prefabs, its grid). False when the same; throws
    /// std::invalid_argument when its cells do not match its size.
    bool set_ground(const LevelGround& ground);
    /// The settings of the level as they are.
    LevelSettings settings() const;
    /// Replaces the settings. False when the same; throws std::invalid_argument without a layer or
    /// an agent.
    bool set_settings(const LevelSettings& settings);

    /// Starts an undo step named `name` (undo_name()); ends the current one first, if any.
    void begin_action(std::string name);
    /// Ends the current step; a step that changed nothing is dropped. Clears the steps to redo when
    /// it changed something.
    void end_action();
    /// True between begin_action() and end_action().
    bool in_action() const { return in_action_; }
    /// Whether undo() and redo() have a step to apply.
    bool can_undo() const { return !undo_.empty(); }
    /// See can_undo().
    bool can_redo() const { return !redo_.empty(); }
    /// The name of the step undo() would undo; empty when none.
    const std::string& undo_name() const;
    /// The name of the step redo() would apply again; empty when none.
    const std::string& redo_name() const;
    /// Undoes the last step (ends the current action first). False when there is none.
    bool undo();
    /// Applies again the last step undone. False when there is none.
    bool redo();
    /// Forgets every step (a level just opened).
    void clear_history();
    /// The steps kept.
    static constexpr std::size_t kMaxSteps = 500;

private:
    struct InstanceEdit {
        std::uint32_t id = 0;
        std::optional<LevelInstance> before, after;
        std::size_t index = 0;  ///< where it is (or was) in the document's list
    };
    struct PointEdit {
        std::string name;
        std::optional<LevelPoint> before, after;
        std::size_t index = 0;
    };
    struct VolumeEdit {
        std::uint32_t id = 0;
        std::optional<LevelVolume> before, after;
        std::size_t index = 0;
    };
    struct GroundEdit {
        LevelGround before, after;
    };
    struct SettingsEdit {
        LevelSettings before, after;
    };
    struct Step {
        std::string name;
        std::vector<InstanceEdit> instances;
        std::vector<PointEdit> points;
        std::vector<VolumeEdit> volumes;
        std::optional<GroundEdit> ground;
        std::optional<SettingsEdit> settings;
        bool empty() const { return instances.empty() && points.empty() && volumes.empty() && !ground && !settings; }
    };

    Step& current();  ///< the step being recorded (opens one outside an action)
    void changed(const LevelChange& change);
    void close_lone_step();  ///< outside an action, a change is its own step
    void record_instance(std::uint32_t id, const std::optional<LevelInstance>& before, std::size_t index);
    void record_point(const std::string& name, const std::optional<LevelPoint>& before, std::size_t index);
    void record_volume(std::uint32_t id, const std::optional<LevelVolume>& before, std::size_t index);
    void apply(const Step& step, bool forward);
    void apply_settings(const LevelSettings& settings);

    LevelDocument document_;
    std::uint64_t revision_ = 0;
    std::function<void(const LevelChange&)> listener_;
    std::vector<Step> undo_;
    std::vector<Step> redo_;
    std::optional<Step> recording_;
    bool in_action_ = false;
};

}  // namespace moteur
