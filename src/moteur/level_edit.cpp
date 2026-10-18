#include "moteur/level_edit.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace moteur {

namespace {

const std::string kNoName;

template <typename List, typename Key>
std::optional<std::size_t> find_index(const List& list, const Key& key) {
    for (std::size_t i = 0; i < list.size(); ++i) {
        if constexpr (std::is_same_v<Key, std::uint32_t>) {
            if (list[i].id == key) {
                return i;
            }
        } else {
            if (list[i].name == key) {
                return i;
            }
        }
    }
    return std::nullopt;
}

// Puts `item` (or nothing) where the list has the item of `key`: replaced, removed, or inserted at
// `index` (clamped: the list may be shorter than when the edit was recorded only if other edits of
// the same step are undone in the wrong order, which apply() avoids).
template <typename List, typename Key, typename Item>
void place(List& list, const Key& key, const std::optional<Item>& item, std::size_t index) {
    const std::optional<std::size_t> at = find_index(list, key);
    if (item) {
        if (at) {
            list[*at] = *item;
        } else {
            list.insert(list.begin() + static_cast<std::ptrdiff_t>(std::min(index, list.size())), *item);
        }
    } else if (at) {
        list.erase(list.begin() + static_cast<std::ptrdiff_t>(*at));
    }
}

}  // namespace

bool LevelSettings::operator==(const LevelSettings& other) const {
    return description == other.description && chunk_size == other.chunk_size && layers == other.layers &&
           lighting == other.lighting && agents == other.agents && navigation == other.navigation;
}

glm::ivec2 ground_cell(const LevelGround& ground, glm::vec3 point) {
    const float size = ground.cell_size > 0.0f ? ground.cell_size : 1.0f;
    return {static_cast<int>(std::floor((point.x - ground.origin.x) / size)),
            static_cast<int>(std::floor((point.z - ground.origin.y) / size))};
}

glm::ivec2 grow_ground(LevelGround& ground, glm::ivec2 from, glm::ivec2 to) {
    const glm::ivec2 low = glm::min(glm::min(from, to), glm::ivec2(0));
    const glm::ivec2 high = glm::max(glm::max(from, to) + 1, glm::ivec2(ground.width, ground.height));
    const glm::ivec2 shift = -low;
    const glm::ivec2 size = high - low;
    if (shift == glm::ivec2(0) && size == glm::ivec2(ground.width, ground.height)) {
        return shift;
    }
    std::vector<std::uint8_t> cells(static_cast<std::size_t>(size.x) * static_cast<std::size_t>(size.y), 0);
    for (int z = 0; z < ground.height; ++z) {
        for (int x = 0; x < ground.width; ++x) {
            cells[static_cast<std::size_t>(z + shift.y) * static_cast<std::size_t>(size.x) + static_cast<std::size_t>(x + shift.x)] =
                ground.cells[static_cast<std::size_t>(z) * static_cast<std::size_t>(ground.width) + static_cast<std::size_t>(x)];
        }
    }
    ground.origin -= glm::vec2(shift) * ground.cell_size;
    ground.width = size.x;
    ground.height = size.y;
    ground.cells = std::move(cells);
    return shift;
}

EditableLevel::EditableLevel(LevelDocument document) : document_(std::move(document)) {
    document_.navigation.tile_size = document_.chunk_size;  // a tile is a chunk
}

EditableLevel::Step& EditableLevel::current() {
    if (!recording_) {
        recording_ = Step{};
    }
    return *recording_;
}

void EditableLevel::changed(const LevelChange& change) {
    ++revision_;
    if (listener_) {
        listener_(change);
    }
}

void EditableLevel::close_lone_step() {
    if (!in_action_) {
        end_action();
    }
}

void EditableLevel::record_instance(std::uint32_t id, const std::optional<LevelInstance>& before, std::size_t index) {
    Step& step = current();
    const LevelInstance* now = document_.instance(id);
    const std::optional<LevelInstance> after = now != nullptr ? std::optional<LevelInstance>(*now) : std::nullopt;
    for (InstanceEdit& edit : step.instances) {
        if (edit.id == id) {  // already changed in this step: its "before" stays the first one
            edit.after = after;
            return;
        }
    }
    step.instances.push_back({id, before, after, index});
}

void EditableLevel::record_point(const std::string& name, const std::optional<LevelPoint>& before, std::size_t index) {
    Step& step = current();
    const LevelPoint* now = document_.point(name);
    for (PointEdit& edit : step.points) {
        if (edit.name == name) {
            edit.after = now != nullptr ? std::optional<LevelPoint>(*now) : std::nullopt;
            return;
        }
    }
    PointEdit edit;
    edit.name = name;
    edit.before = before;
    if (now != nullptr) {
        edit.after = *now;
    }
    edit.index = index;
    step.points.push_back(std::move(edit));
}

void EditableLevel::record_volume(std::uint32_t id, const std::optional<LevelVolume>& before, std::size_t index) {
    Step& step = current();
    const std::optional<std::size_t> at = find_index(document_.volumes, id);
    const std::optional<LevelVolume> now = at ? std::optional<LevelVolume>(document_.volumes[*at]) : std::nullopt;
    for (VolumeEdit& edit : step.volumes) {
        if (edit.id == id) {
            edit.after = now;
            return;
        }
    }
    step.volumes.push_back({id, before, now, index});
}

std::uint32_t EditableLevel::add(LevelInstance instance) {
    const std::uint32_t id = document_.add(std::move(instance));
    record_instance(id, std::nullopt, document_.instances.size() - 1);
    changed({LevelChange::Kind::Instance, id, {}});
    close_lone_step();
    return id;
}

bool EditableLevel::set(const LevelInstance& instance) {
    const std::optional<std::size_t> at = find_index(document_.instances, instance.id);
    if (!at || document_.instances[*at] == instance) {
        return false;
    }
    const LevelInstance before = document_.instances[*at];
    document_.instances[*at] = instance;
    record_instance(instance.id, before, *at);
    changed({LevelChange::Kind::Instance, instance.id, {}});
    close_lone_step();
    return true;
}

bool EditableLevel::remove(std::uint32_t id) {
    const std::optional<std::size_t> at = find_index(document_.instances, id);
    if (!at) {
        return false;
    }
    const LevelInstance before = document_.instances[*at];
    document_.instances.erase(document_.instances.begin() + static_cast<std::ptrdiff_t>(*at));
    record_instance(id, before, *at);
    changed({LevelChange::Kind::Instance, id, {}});
    close_lone_step();
    return true;
}

bool EditableLevel::set_point(const LevelPoint& point) {
    const std::optional<std::size_t> at = find_index(document_.points, point.name);
    std::optional<LevelPoint> before;
    std::size_t index = document_.points.size();
    if (at) {
        if (document_.points[*at] == point) {
            return false;
        }
        before = document_.points[*at];
        index = *at;
        document_.points[*at] = point;
    } else {
        document_.points.push_back(point);
    }
    record_point(point.name, before, index);
    changed({LevelChange::Kind::Point, 0, point.name});
    close_lone_step();
    return true;
}

bool EditableLevel::remove_point(const std::string& name) {
    const std::optional<std::size_t> at = find_index(document_.points, name);
    if (!at) {
        return false;
    }
    const LevelPoint before = document_.points[*at];
    document_.points.erase(document_.points.begin() + static_cast<std::ptrdiff_t>(*at));
    record_point(name, before, *at);
    changed({LevelChange::Kind::Point, 0, name});
    close_lone_step();
    return true;
}

std::uint32_t EditableLevel::add(LevelVolume volume) {
    const std::uint32_t id = document_.add(std::move(volume));
    record_volume(id, std::nullopt, document_.volumes.size() - 1);
    changed({LevelChange::Kind::Volume, id, {}});
    close_lone_step();
    return id;
}

bool EditableLevel::set(const LevelVolume& volume) {
    const std::optional<std::size_t> at = find_index(document_.volumes, volume.id);
    if (!at || document_.volumes[*at] == volume) {
        return false;
    }
    const LevelVolume before = document_.volumes[*at];
    document_.volumes[*at] = volume;
    record_volume(volume.id, before, *at);
    changed({LevelChange::Kind::Volume, volume.id, {}});
    close_lone_step();
    return true;
}

bool EditableLevel::remove_volume(std::uint32_t id) {
    const std::optional<std::size_t> at = find_index(document_.volumes, id);
    if (!at) {
        return false;
    }
    const LevelVolume before = document_.volumes[*at];
    document_.volumes.erase(document_.volumes.begin() + static_cast<std::ptrdiff_t>(*at));
    record_volume(id, before, *at);
    changed({LevelChange::Kind::Volume, id, {}});
    close_lone_step();
    return true;
}

bool EditableLevel::paint_ground(glm::ivec2 cell, std::uint8_t value) {
    LevelGround& ground = document_.ground;
    if (value > ground.prefabs.size()) {
        return false;
    }
    const bool inside = cell.x >= 0 && cell.y >= 0 && cell.x < ground.width && cell.y < ground.height;
    if (!inside && value == 0) {
        return false;  // erasing outside: nothing there
    }
    if (inside && ground.cells[static_cast<std::size_t>(cell.y) * static_cast<std::size_t>(ground.width) +
                               static_cast<std::size_t>(cell.x)] == value) {
        return false;
    }
    Step& step = current();
    if (!step.ground) {
        step.ground = GroundEdit{ground, ground};
    }
    const glm::ivec2 shift = grow_ground(ground, cell, cell);
    const glm::ivec2 at = cell + shift;
    ground.cells[static_cast<std::size_t>(at.y) * static_cast<std::size_t>(ground.width) + static_cast<std::size_t>(at.x)] = value;
    step.ground->after = ground;
    changed({LevelChange::Kind::Ground, 0, {}});
    close_lone_step();
    return true;
}

bool EditableLevel::set_ground(const LevelGround& ground) {
    if (ground.cells.size() != static_cast<std::size_t>(std::max(ground.width, 0)) * static_cast<std::size_t>(std::max(ground.height, 0))) {
        throw std::invalid_argument("EditableLevel::set_ground: the cells do not match the size");
    }
    if (ground == document_.ground) {
        return false;
    }
    Step& step = current();
    if (!step.ground) {
        step.ground = GroundEdit{document_.ground, ground};
    }
    document_.ground = ground;
    step.ground->after = ground;
    changed({LevelChange::Kind::Ground, 0, {}});
    close_lone_step();
    return true;
}

LevelSettings EditableLevel::settings() const {
    return {document_.description, document_.chunk_size, document_.layers,
            document_.lighting,    document_.agents,     document_.navigation};
}

void EditableLevel::apply_settings(const LevelSettings& settings) {
    document_.description = settings.description;
    document_.chunk_size = settings.chunk_size;
    document_.layers = settings.layers;
    document_.lighting = settings.lighting;
    document_.agents = settings.agents;
    document_.navigation = settings.navigation;
    document_.navigation.tile_size = settings.chunk_size;  // a tile is a chunk
}

bool EditableLevel::set_settings(const LevelSettings& settings) {
    if (settings.layers.empty() || settings.agents.empty()) {
        throw std::invalid_argument("EditableLevel::set_settings: a level needs a layer and an agent");
    }
    const LevelSettings before = this->settings();
    if (before == settings) {
        return false;
    }
    apply_settings(settings);
    Step& step = current();
    if (!step.settings) {
        step.settings = SettingsEdit{before, before};
    }
    step.settings->after = this->settings();
    changed({LevelChange::Kind::Settings, 0, {}});
    close_lone_step();
    return true;
}

void EditableLevel::begin_action(std::string name) {
    end_action();
    in_action_ = true;
    recording_ = Step{};
    recording_->name = std::move(name);
}

void EditableLevel::end_action() {
    in_action_ = false;
    if (!recording_) {
        return;
    }
    Step step = std::move(*recording_);
    recording_.reset();
    // Edits that came back to where they started (moved, then moved back) change nothing.
    std::erase_if(step.instances, [](const InstanceEdit& e) { return e.before == e.after; });
    std::erase_if(step.points, [](const PointEdit& e) { return e.before == e.after; });
    std::erase_if(step.volumes, [](const VolumeEdit& e) { return e.before == e.after; });
    if (step.ground && step.ground->before == step.ground->after) {
        step.ground.reset();
    }
    if (step.settings && step.settings->before == step.settings->after) {
        step.settings.reset();
    }
    if (step.empty()) {
        return;
    }
    if (step.name.empty()) {
        step.name = "edit";
    }
    undo_.push_back(std::move(step));
    if (undo_.size() > kMaxSteps) {
        undo_.erase(undo_.begin());
    }
    redo_.clear();
}

const std::string& EditableLevel::undo_name() const {
    return undo_.empty() ? kNoName : undo_.back().name;
}

const std::string& EditableLevel::redo_name() const {
    return redo_.empty() ? kNoName : redo_.back().name;
}

void EditableLevel::apply(const Step& step, bool forward) {
    // Forward in the order recorded, backward in the reverse: the indices recorded stay right.
    const auto each = [forward](const auto& list, const auto& body) {
        if (forward) {
            for (const auto& item : list) {
                body(item);
            }
        } else {
            for (auto it = list.rbegin(); it != list.rend(); ++it) {
                body(*it);
            }
        }
    };
    each(step.instances, [&](const InstanceEdit& edit) {
        place(document_.instances, edit.id, forward ? edit.after : edit.before, edit.index);
        changed({LevelChange::Kind::Instance, edit.id, {}});
    });
    each(step.points, [&](const PointEdit& edit) {
        place(document_.points, edit.name, forward ? edit.after : edit.before, edit.index);
        changed({LevelChange::Kind::Point, 0, edit.name});
    });
    each(step.volumes, [&](const VolumeEdit& edit) {
        place(document_.volumes, edit.id, forward ? edit.after : edit.before, edit.index);
        changed({LevelChange::Kind::Volume, edit.id, {}});
    });
    if (step.ground) {
        document_.ground = forward ? step.ground->after : step.ground->before;
        changed({LevelChange::Kind::Ground, 0, {}});
    }
    if (step.settings) {
        apply_settings(forward ? step.settings->after : step.settings->before);
        changed({LevelChange::Kind::Settings, 0, {}});
    }
}

bool EditableLevel::undo() {
    end_action();
    if (undo_.empty()) {
        return false;
    }
    Step step = std::move(undo_.back());
    undo_.pop_back();
    apply(step, false);
    redo_.push_back(std::move(step));
    return true;
}

bool EditableLevel::redo() {
    end_action();
    if (redo_.empty()) {
        return false;
    }
    Step step = std::move(redo_.back());
    redo_.pop_back();
    apply(step, true);
    undo_.push_back(std::move(step));
    return true;
}

void EditableLevel::clear_history() {
    undo_.clear();
    redo_.clear();
    recording_.reset();
    in_action_ = false;
}

}  // namespace moteur
