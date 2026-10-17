#include "moteur/debug_tools.hpp"

#include <imgui.h>
#include <imgui_internal.h>  // settings handlers (the open windows in imgui.ini)

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "moteur/aabb.hpp"
#include "moteur/animator.hpp"
#include "moteur/application.hpp"
#include "moteur/collision.hpp"
#include "moteur/debug_lines.hpp"
#include "moteur/fixed_timestep.hpp"
#include "moteur/movement.hpp"
#include "moteur/process_memory.hpp"
#include "moteur/variables.hpp"
#include "moteur/state_stack.hpp"
#include "moteur/world.hpp"

namespace moteur {

namespace {

constexpr const char* kSettingsType = "MoteurTools";
constexpr ImVec4 kWarning(1.0f, 0.75f, 0.3f, 1.0f);
constexpr ImVec4 kError(1.0f, 0.5f, 0.45f, 1.0f);

double megabytes(std::size_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

char lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool contains_ignoring_case(std::string_view text, std::string_view part) {
    if (part.size() > text.size()) {
        return false;
    }
    for (std::size_t i = 0; i + part.size() <= text.size(); ++i) {
        std::size_t k = 0;
        while (k < part.size() && lower(text[i + k]) == lower(part[k])) {
            ++k;
        }
        if (k == part.size()) {
            return true;
        }
    }
    return false;
}

const char* group_label(SoundGroup group) {
    switch (group) {
        case SoundGroup::Music: return "Music";
        case SoundGroup::Effects: return "Effects";
        case SoundGroup::Ambience: return "Ambience";
        case SoundGroup::Interface: return "Interface";
    }
    return "?";
}

const char* refusal_label(const std::string& reason) {
    if (reason == "inaudible") {
        return "too quiet to be heard";
    }
    if (reason == "sound_limit") {
        return "per-file limit";
    }
    if (reason == "voice_limit") {
        return "voice limit";
    }
    return reason.c_str();
}

// A color in linear values, which may exceed 1 (emissive, HDR light).
bool edit_linear_color(const char* label, glm::vec3& color) {
    return ImGui::ColorEdit3(label, &color.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
}
bool edit_linear_color(const char* label, glm::vec4& color) {
    return ImGui::ColorEdit4(label, &color.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
}

bool edit_transform(Transform& transform) {
    bool changed = ImGui::DragFloat3("Position", &transform.position.x, 0.05f, 0.0f, 0.0f, "%.3f");
    // Shown as angles (degrees), kept as a quaternion: converted only when the angles are touched.
    glm::vec3 angles = glm::degrees(glm::eulerAngles(transform.rotation));
    if (ImGui::DragFloat3("Rotation (°)", &angles.x, 0.5f, 0.0f, 0.0f, "%.1f")) {
        transform.rotation = glm::quat(glm::radians(angles));
        changed = true;
    }
    changed |= ImGui::DragFloat3("Scale", &transform.scale.x, 0.01f, 0.0f, 0.0f, "%.3f");
    return changed;
}

bool edit_material(Material& material) {
    bool changed = edit_linear_color("Color", material.base_color);
    changed |= ImGui::SliderFloat("Metallic", &material.metallic, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Roughness", &material.roughness, 0.0f, 1.0f);
    changed |= edit_linear_color("Emissive", material.emissive);
    changed |= ImGui::Checkbox("Double-sided", &material.double_sided);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Casts shadow", &material.casts_shadow);
    ImGui::TextDisabled("Textures: %s%s%s%s%s", material.base_color_texture ? "color " : "",
                        material.metallic_roughness_texture ? "metallic-roughness " : "",
                        material.normal_texture ? "normal " : "", material.occlusion_texture ? "occlusion " : "",
                        material.emissive_texture ? "emissive" : "");
    return changed;
}

}  // namespace

// --- ComponentInspectors ---

std::string ComponentInspectors::name(const entt::type_info& type) const {
    if (const auto found = entries_.find(type.hash()); found != entries_.end()) {
        return found->second.name;
    }
    return std::string(type.name());
}

bool ComponentInspectors::edit(entt::registry& registry, entt::entity entity, const entt::type_info& type) const {
    const auto found = entries_.find(type.hash());
    if (found == entries_.end() || !found->second.edit) {
        return false;
    }
    return found->second.edit(registry, entity);
}

void ComponentInspectors::add_engine_components() {
    add<Transform>("Transform", edit_transform);
    add<PreviousTransform>("Previous tick transform", [](PreviousTransform& previous) {
        const glm::vec3& p = previous.value.position;
        ImGui::Text("Position: %.3f %.3f %.3f", static_cast<double>(p.x), static_cast<double>(p.y), static_cast<double>(p.z));
        ImGui::TextDisabled("(copied every tick: the entity moves, it is interpolated)");
        return false;
    });
    add<Parent>("Parent", [](Parent& parent) {
        if (parent.entity == entt::null) {
            ImGui::TextUnformatted("none");
        } else {
            ImGui::Text("Entity #%u", static_cast<unsigned>(entt::to_entity(parent.entity)));
        }
        return false;
    });
    add<BoneAttachment>("Bone attachment", [](BoneAttachment& attachment) {
        char buffer[128] = {};
        std::snprintf(buffer, sizeof(buffer), "%s", attachment.point.c_str());
        if (ImGui::InputText("Point", buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
            attachment.point = buffer;
            return true;
        }
        ImGui::SetItemTooltip("An attachment point of the parent's description, or a bone name.");
        return false;
    });
    add<Name>("Name", [](Name& name) {
        char buffer[128] = {};
        std::snprintf(buffer, sizeof(buffer), "%s", name.value.c_str());
        if (ImGui::InputText("Name", buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
            name.value = buffer;
            return true;
        }
        return false;
    });
    add_tag<Hidden>("Hidden");
    add<MeshComponent>("Mesh", [](MeshComponent& component) {
        if (component.mesh) {
            ImGui::Text("%u triangles, %.2f MB", component.mesh->index_count / 3, megabytes(component.mesh->gpu_bytes));
        }
        return edit_material(component.material);
    });
    add<ModelComponent>("Model", [](ModelComponent& component) {
        if (component.model) {
            const Model& model = *component.model;
            const glm::vec3 size = model.bounds.size();
            ImGui::Text("%zu parts, %zu triangles, %.2f MB", model.parts.size(), model.triangle_count, megabytes(model.gpu_bytes));
            ImGui::Text("Size: %.2f x %.2f x %.2f m", static_cast<double>(size.x), static_cast<double>(size.y),
                        static_cast<double>(size.z));
        }
        return false;
    });
    add<Animator>("Animation", [](Animator& animator) {
        bool any = false;
        for (int l = 0; l < Animator::kLayers; ++l) {
            const Animator::Layer& layer = animator.layer(l);
            if (layer.motions.empty()) {
                continue;
            }
            any = true;
            if (l > 0) {
                ImGui::Text("Layer %d (upper body): %.0f %%", l, layer.weight / 10.0);
            }
            for (const Animator::Motion& motion : layer.motions) {
                const ClipClock& clock = motion.clock;
                if (motion.blend_space()) {
                    ImGui::BulletText("%s (blend) %.0f %%, phase %.3f, x%.2f", motion.name.c_str(), motion.weight / 10.0,
                                      static_cast<double>(motion.ratio(1.0f)), motion.rate / 1000.0);
                } else {
                    const double ticks = static_cast<double>(clock.cycle_time()) / ClipClock::kOne;
                    ImGui::BulletText("%s (%s) %.0f %%, %.1f / %d ticks (%.2f s)%s", motion.name.c_str(),
                                      clock.once() ? "once" : "looping", motion.weight / 10.0, ticks,
                                      clock.cycle_ticks(), ticks / tick_rate(), clock.finished() ? ", finished" : "");
                }
            }
        }
        if (!any) {
            ImGui::TextDisabled("No clip (rest pose)");
        }
        if (animator.clips) {
            ImGui::TextDisabled("%zu clips from %s", animator.clips->size(), animator.clips->source().c_str());
        }
        if (animator.set) {
            ImGui::TextDisabled("Description: %s", animator.set->source().c_str());
        }
        ImGui::Text("Movement speed: %.3f m/s", animator.move_speed());
        float speed = static_cast<float>(animator.speed());
        if (ImGui::DragFloat("Speed", &speed, 0.01f, 0.0f, 5.0f, "x%.3f")) {
            animator.set_speed(speed);
            return true;
        }
        return false;
    });
    add<Collider>("Collision", [](Collider& collider) {
        bool changed = ImGui::DragFloat("Radius (m)", &collider.radius, 0.01f, 0.05f, 5.0f);
        changed |= ImGui::DragInt("Push weight", &collider.push_weight, 0.1f, 0, 100);
        changed |= ImGui::Checkbox("Blocked by walls", &collider.blocked_by_walls);
        ImGui::Text("Layer %08X, mask %08X", collider.layer, collider.mask);
        return changed;
    });
    add<Mover>("Movement", [](Mover& mover) {
        static const char* const kModes[] = {"Stopped", "To point", "Direct", "Flow field"};
        static const char* const kStates[] = {"Idle", "Waiting for path", "Moving", "Arrived", "Blocked", "No path"};
        bool changed = ImGui::DragFloat("Speed (m/s)", &mover.speed, 0.05f, 0.0f, 30.0f);
        changed |= ImGui::DragFloat("Turn rate", &mover.turn_rate, 0.1f, 0.0f, 100.0f);
        ImGui::Text("Mode: %s, state: %s", kModes[static_cast<int>(mover.mode)], kStates[static_cast<int>(mover.state)]);
        ImGui::Text("Actual speed: %.2f m/s, path: %zu / %zu points", mover.actual_speed, mover.next_point,
                    mover.path.size());
        if (mover.mode == MoveMode::ToPoint) {
            ImGui::Text("Goal: (%.2f, %.2f)", mover.goal.x, mover.goal.y);
        }
        return changed;
    });
    add<LightSource>("Light", [](LightSource& light) {
        bool changed = edit_linear_color("Color", light.color);
        changed |= ImGui::DragFloat("Intensity", &light.intensity, 0.05f, 0.0f, 1000.0f);
        changed |= ImGui::DragFloat("Range (m)", &light.range, 0.05f, 0.0f, 1000.0f);
        changed |= ImGui::Checkbox("Shadows", &light.casts_shadows);
        return changed;
    });
    add<Billboard>("Billboard", [](Billboard& billboard) {
        bool changed = ImGui::DragFloat2("Size (m)", &billboard.size.x, 0.01f, 0.0f, 100.0f);
        changed |= edit_linear_color("Color", billboard.options.color);
        changed |= ImGui::Checkbox("Additive", &billboard.options.additive);
        return changed;
    });
}

bool inspector_filter_matches(std::string_view filter, std::uint32_t entity_number, std::string_view name) {
    while (!filter.empty() && filter.front() == ' ') {
        filter.remove_prefix(1);
    }
    while (!filter.empty() && filter.back() == ' ') {
        filter.remove_suffix(1);
    }
    if (filter.empty()) {
        return true;
    }
    std::string_view number = filter;
    if (number.front() == '#') {
        number.remove_prefix(1);
    }
    if (!number.empty() && std::all_of(number.begin(), number.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        if (number == std::to_string(entity_number)) {
            return true;
        }
    }
    return contains_ignoring_case(name, filter);
}

// --- DebugTools ---

DebugTools::DebugTools(Application& app) : app_(app) {
    components_.add_engine_components();
    // The parent can be selected from its child.
    components_.add<Parent>("Parent", [this](Parent& parent) {
        if (parent.entity == entt::null) {
            ImGui::TextUnformatted("none");
            return false;
        }
        ImGui::Text("Entity #%u", static_cast<unsigned>(entt::to_entity(parent.entity)));
        ImGui::SameLine();
        if (ImGui::SmallButton("Select")) {
            selected_ = parent.entity;
        }
        return false;
    });

    // Which windows are open, in imgui.ini with the rest (ImGui reads it at its first frame).
    ImGuiSettingsHandler handler;
    handler.TypeName = kSettingsType;
    handler.TypeHash = ImHashStr(kSettingsType);
    handler.UserData = this;
    handler.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler* self, const char* name) -> void* {
        return std::strcmp(name, "Windows") == 0 ? self->UserData : nullptr;
    };
    handler.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* line) {
        static_cast<DebugTools*>(entry)->read_setting(line);
    };
    handler.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* self, ImGuiTextBuffer* out) {
        const std::string lines = static_cast<const DebugTools*>(self->UserData)->write_settings();
        out->appendf("[%s][Windows]\n%s\n", self->TypeName, lines.c_str());
    };
    ImGui::AddSettingsHandler(&handler);
}

DebugTools::~DebugTools() {
    // ImGui writes its file when its context goes, after this: write it now, while the handler is here.
    ImGuiIO& io = ImGui::GetIO();
    if (io.IniFilename != nullptr) {
        ImGui::SaveIniSettingsToDisk(io.IniFilename);
    }
    ImGui::RemoveSettingsHandler(kSettingsType);
    io.IniFilename = nullptr;  // or ImGui would write it again, without our section
}

const char* DebugTools::window_key(Window window) {
    switch (window) {
        case Window::Inspector: return "Inspector";
        case Window::Assets: return "Assets";
        case Window::Input: return "Input";
        case Window::Audio: return "Audio";
        case Window::States: return "States";
        case Window::Animation: return "Animation";
        case Window::Data: return "Data";
        case Window::Console: return "Console";
        case Window::Profiler: return "Profiler";
    }
    return "";
}

bool DebugTools::read_setting(std::string_view line) {
    const std::size_t equal = line.find('=');
    if (equal == std::string_view::npos) {
        return false;
    }
    const std::string_view key = line.substr(0, equal);
    const std::string_view value = line.substr(equal + 1);
    for (int i = 0; i < kWindowCount; ++i) {
        if (key == window_key(static_cast<Window>(i))) {
            open_[i] = value == "1";
            return true;
        }
    }
    return false;
}

std::string DebugTools::write_settings() const {
    std::string text;
    for (int i = 0; i < kWindowCount; ++i) {
        text += window_key(static_cast<Window>(i));
        text += open_[i] ? "=1\n" : "=0\n";
    }
    return text;
}

void DebugTools::set_open(Window window, bool open) {
    bool& current = open_[static_cast<std::size_t>(window)];
    if (current != open) {
        current = open;
        if (ImGui::GetCurrentContext() != nullptr) {
            ImGui::MarkIniSettingsDirty();
        }
    }
}

void DebugTools::watch(World& world, std::string label) {
    if (world_ != &world) {
        selected_ = entt::null;
        edited_ = false;
    }
    world_ = &world;
    world_label_ = std::move(label);
}

void DebugTools::forget(World& world) {
    if (world_ == &world) {
        world_ = nullptr;
        world_label_.clear();
        selected_ = entt::null;
        edited_ = false;
    }
}

void DebugTools::watch(StateStack& stack, std::string label) {
    forget(stack);
    stacks_.push_back({&stack, std::move(label)});
}

void DebugTools::forget(StateStack& stack) {
    std::erase_if(stacks_, [&stack](const WatchedStack& watched) { return watched.stack == &stack; });
}

void DebugTools::menu_items() {
    static constexpr const char* kLabels[kWindowCount] = {"Entity Inspector", "Assets", "Input", "Audio", "Game States",
                                                          "Animation", "Data", "Console", "Profiler"};
    for (int i = 0; i < kWindowCount; ++i) {
        if (ImGui::MenuItem(kLabels[i], nullptr, open_[i])) {
            set_open(static_cast<Window>(i), !open_[i]);
        }
    }
}

void DebugTools::between_frames() {
    // What the windows' buttons asked for: loads and frees wait for the GPU, never inside a frame.
    if (reload_request_) {
        const auto [type, key] = *reload_request_;
        reload_request_.reset();
        last_reload_ = app_.assets().reload(static_cast<AssetType>(type), key) ? "'" + key + "' reloaded"
                                                       : "'" + key + "' not reloaded (see the log)";
    }
    if (!data_reload_.empty()) {
        const std::string name = std::move(data_reload_);
        data_reload_.clear();
        last_reload_ = app_.data().reload(app_.assets().root(), name) ? "table '" + name + "' reloaded"
                                                                      : "table '" + name + "' not reloaded (see the issues)";
    }
    if (collect_request_) {
        collect_request_ = false;
        const std::size_t freed = app_.assets().collect_garbage();
        last_reload_ = std::to_string(freed) + " asset(s) freed";
    }
}

void DebugTools::draw() {
    const auto window = [this](Window which, void (DebugTools::*draw_window)()) {
        if (open(which)) {
            (this->*draw_window)();
        }
    };
    window(Window::Inspector, &DebugTools::draw_inspector);
    window(Window::Assets, &DebugTools::draw_assets);
    window(Window::Input, &DebugTools::draw_input);
    window(Window::Audio, &DebugTools::draw_audio);
    window(Window::States, &DebugTools::draw_states);
    window(Window::Animation, &DebugTools::draw_animation);
    window(Window::Data, &DebugTools::draw_data);
    window(Window::Console, &DebugTools::draw_console);
    window(Window::Profiler, &DebugTools::draw_profiler);
    draw_stats_bar();
}

void DebugTools::draw_stats_bar() {
    if (stats_bar_ == nullptr) {
        stats_bar_ = &app_.console().variables().add_bool("debug.stats", false,
                                                          "statistics bar (frames, CPU, logic, memory, entities)",
                                                          Variable::Archive);
    }
    if (!stats_bar_->as_bool()) {
        return;
    }
    const auto& history = app_.profiler().history();
    double cpu = 0.0, logic = 0.0;
    if (!history.empty()) {
        const Profiler::Frame& frame = history.back();
        cpu = Profiler::milliseconds(frame.end - frame.start);
        for (const Profiler::Zone& zone : frame.zones) {
            if (std::string_view(zone.name) == "logic") {
                logic = Profiler::milliseconds(zone.end - zone.start);
            }
            if (std::string_view(zone.name) == "waiting for the display") {
                cpu -= Profiler::milliseconds(zone.end - zone.start);  // waiting is not work
            }
        }
    }
    char text[256];
    std::snprintf(text, sizeof(text), "%.0f FPS  |  CPU %.2f ms (logic %.2f)  |  memory %.0f MB  |  %zu entities",
                  static_cast<double>(ImGui::GetIO().Framerate), cpu, logic,
                  static_cast<double>(process_memory_bytes()) / (1024.0 * 1024.0), world_ != nullptr ? world_->entity_count() : 0);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const ImVec2 size = ImGui::CalcTextSize(text);
    const ImVec2 corner(ImGui::GetIO().DisplaySize.x - size.x - 16.0f, 28.0f);
    draw->AddRectFilled({corner.x - 6.0f, corner.y - 3.0f}, {corner.x + size.x + 6.0f, corner.y + size.y + 3.0f}, IM_COL32(0, 0, 0, 170), 4.0f);
    draw->AddText(corner, IM_COL32(230, 230, 230, 255), text);
    // The last frame times, as a small graph under the bar (16.7 ms: the line).
    const ImVec2 graph(corner.x - 6.0f, corner.y + size.y + 6.0f);
    const float width = size.x + 12.0f, height = 36.0f;
    draw->AddRectFilled(graph, {graph.x + width, graph.y + height}, IM_COL32(0, 0, 0, 140), 4.0f);
    const std::size_t count = std::min<std::size_t>(history.size(), 240);
    for (std::size_t i = 0; i < count; ++i) {
        const Profiler::Frame& frame = history[history.size() - count + i];
        const float ms = static_cast<float>(Profiler::milliseconds(frame.end - frame.start));
        const float x = graph.x + width * static_cast<float>(i) / 240.0f;
        const float h = std::min(ms / 33.3f, 1.0f) * height;
        draw->AddLine({x, graph.y + height}, {x, graph.y + height - h}, ms > 16.7f ? IM_COL32(255, 110, 90, 255) : IM_COL32(120, 200, 255, 255));
    }
    const float limit = graph.y + height - height * 16.7f / 33.3f;
    draw->AddLine({graph.x, limit}, {graph.x + width, limit}, IM_COL32(255, 255, 255, 80));
}

void DebugTools::draw_profiler() {
    if (!begin_window("Profiler", Window::Profiler, 760.0f, 520.0f)) {
        return;
    }
    Profiler& profiler = app_.profiler();
    bool frozen = profiler.frozen();
    if (ImGui::Checkbox("Freeze", &frozen)) {
        profiler.set_frozen(frozen);
    }
    ImGui::SameLine();
    bool enabled = profiler.enabled();
    if (ImGui::Checkbox("Measure", &enabled)) {
        profiler.set_enabled(enabled);
    }
    ImGui::SameLine();
    if (!profiler.capturing()) {
        if (ImGui::Button("Capture")) {
            profiler.start_capture();
        }
    } else if (ImGui::Button("Stop and write")) {
        const std::string path = app_.preferences_directory() + "profiles/profile.json";
        last_reload_ = profiler.stop_capture(path) ? "profile written: " + path : "could not write the profile";
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", profiler.capturing() ? "capturing" : last_reload_.c_str());

    const auto& history = profiler.history();
    if (history.empty()) {
        ImGui::TextDisabled("No frame measured.");
        ImGui::End();
        return;
    }
    // The frame times: a bar per frame; a click picks one (otherwise the last).
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x, height = 90.0f;
    ImGui::InvisibleButton("graphe", {width, height});
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, {origin.x + width, origin.y + height}, IM_COL32(20, 20, 24, 255));
    const std::size_t count = history.size();
    const float bar = width / static_cast<float>(Profiler::kHistory);
    for (std::size_t i = 0; i < count; ++i) {
        const Profiler::Frame& frame = history[i];
        const float ms = static_cast<float>(Profiler::milliseconds(frame.end - frame.start));
        const float x = origin.x + bar * static_cast<float>(i + Profiler::kHistory - count);
        const float h = std::min(ms / 33.3f, 1.0f) * height;
        const ImU32 color = frame.index == profiler_frame_ ? IM_COL32(255, 255, 255, 255)
                            : ms > 16.7f                   ? IM_COL32(255, 110, 90, 255)
                                                           : IM_COL32(120, 200, 255, 255);
        draw->AddRectFilled({x, origin.y + height - h}, {x + std::max(bar - 0.5f, 1.0f), origin.y + height}, color);
    }
    const float limit = origin.y + height - height * 16.7f / 33.3f;
    draw->AddLine({origin.x, limit}, {origin.x + width, limit}, IM_COL32(255, 255, 255, 70));
    if (ImGui::IsItemClicked()) {
        const float rel = (ImGui::GetIO().MousePos.x - origin.x) / bar - static_cast<float>(Profiler::kHistory - count);
        const auto i = static_cast<std::size_t>(std::clamp(rel, 0.0f, static_cast<float>(count - 1)));
        profiler_frame_ = history[i].index;
        profiler.set_frozen(true);  // to look at it
    }
    const Profiler::Frame* shown = &history.back();
    for (const Profiler::Frame& frame : history) {
        if (frame.index == profiler_frame_) {
            shown = &frame;
        }
    }
    ImGui::Text("Frame %llu: %.3f ms (click a bar to select it; the line is 16.7 ms)",
                static_cast<unsigned long long>(shown->index), Profiler::milliseconds(shown->end - shown->start));

    if (ImGui::BeginTable("zones", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          {0.0f, 200.0f})) {
        ImGui::TableSetupColumn("Zone");
        ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();
        for (const Profiler::Zone& zone : shown->zones) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Indent(static_cast<float>(zone.depth) * 14.0f);
            ImGui::TextUnformatted(zone.name);
            ImGui::Unindent(static_cast<float>(zone.depth) * 14.0f);
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", Profiler::milliseconds(zone.end - zone.start));
        }
        ImGui::EndTable();
    }

    // Statistics over the history (recomputed twice a second: it walks every frame).
    if (++profiler_summary_age_ >= 30) {
        profiler_summary_ = profiler.summary();
        profiler_summary_age_ = 0;
    }
    if (ImGui::BeginTable("stats", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Zone (over the last frames)");
        ImGui::TableSetupColumn("mean", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("p99", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("max", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("per frame", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableHeadersRow();
        for (const Profiler::Summary& zone : profiler_summary_) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(zone.name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", zone.mean_ms);
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", zone.p99_ms);
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", zone.max_ms);
            ImGui::TableNextColumn();
            ImGui::Text("%.1f", zone.calls);
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void DebugTools::toggle_console() {
    set_open(Window::Console, !open(Window::Console));
    console_focus_ = open(Window::Console);
}

void DebugTools::draw_console() {
    ImGui::SetNextWindowSize({720.0f, 360.0f}, ImGuiCond_FirstUseEver);
    if (!begin_window("Console", Window::Console, 720.0f, 360.0f)) {
        return;
    }
    Console& console = app_.console();
    const float footer = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing() * 2.0f;
    if (ImGui::BeginChild("sortie", {0.0f, -footer}, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar)) {
        for (const Console::Line& line : console.lines()) {
            ImVec4 color(0.85f, 0.85f, 0.85f, 1.0f);
            switch (line.level) {
                case Console::Level::Warning: color = {1.0f, 0.8f, 0.3f, 1.0f}; break;
                case Console::Level::Error: color = {1.0f, 0.45f, 0.4f, 1.0f}; break;
                case Console::Level::Input: color = {0.55f, 0.8f, 1.0f, 1.0f}; break;
                default: break;
            }
            ImGui::TextColored(color, "%s", line.text.c_str());
        }
        if (console.line_count() != console_seen_) {
            ImGui::SetScrollHereY(1.0f);  // follow the new lines
            console_seen_ = console.line_count();
        }
    }
    ImGui::EndChild();
    ImGui::Separator();
    if (!console_candidates_.empty()) {
        std::string list;
        for (const std::string& candidate : console_candidates_) {
            list += candidate + "  ";
        }
        ImGui::TextDisabled("%s", list.c_str());
    } else {
        ImGui::TextDisabled("Tab: complete; arrows: history; help: list commands; ²: close");
    }
    const auto callback = [](ImGuiInputTextCallbackData* data) -> int {
        auto* tools = static_cast<DebugTools*>(data->UserData);
        Console& c = tools->app_.console();
        if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
            const std::string line(data->Buf, static_cast<std::size_t>(data->BufTextLen));
            tools->console_candidates_ = c.complete(line);
            const std::string completed = c.complete_line(line);
            data->DeleteChars(0, data->BufTextLen);
            data->InsertChars(0, completed.c_str());
        } else if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
            const auto& history = c.history();
            if (history.empty()) {
                return 0;
            }
            int& at = tools->console_history_;
            if (data->EventKey == ImGuiKey_UpArrow) {
                at = at < 0 ? static_cast<int>(history.size()) - 1 : std::max(0, at - 1);
            } else if (data->EventKey == ImGuiKey_DownArrow && at >= 0) {
                at = at + 1 >= static_cast<int>(history.size()) ? -1 : at + 1;
            }
            data->DeleteChars(0, data->BufTextLen);
            if (at >= 0) {
                data->InsertChars(0, history[static_cast<std::size_t>(at)].c_str());
            }
        }
        return 0;
    };
    ImGui::PushItemWidth(-1.0f);
    if (console_focus_) {
        ImGui::SetKeyboardFocusHere();
        console_focus_ = false;
    }
    const ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion |
                                      ImGuiInputTextFlags_CallbackHistory;
    if (ImGui::InputText("##ligne", console_input_, sizeof(console_input_), flags, callback, this)) {
        console.submit(console_input_);
        console_input_[0] = '\0';
        console_history_ = -1;
        console_candidates_.clear();
        ImGui::SetKeyboardFocusHere(-1);  // keep typing
    }
    ImGui::PopItemWidth();
    ImGui::End();
}

void DebugTools::draw_data() {
    if (!begin_window("Data", Window::Data, 640.0f, 460.0f)) {
        return;
    }
    DataTables& data = app_.data();
    ImGui::Text("%zu table(s), source: %s", data.tables().size(),
                data.source() == DataSource::Json ? "JSON (hot reload)" : "compiled");
    if (!last_reload_.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", last_reload_.c_str());
    }
    if (ImGui::BeginTable("tables", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Table");
        ImGui::TableSetupColumn("Rows");
        ImGui::TableSetupColumn("Reloads");
        ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        for (const auto& table : data.tables()) {
            ImGui::PushID(table.get());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable(table->name().c_str(), data_table_ == table->name(), ImGuiSelectableFlags_SpanAllColumns |
                                                                                           ImGuiSelectableFlags_AllowOverlap)) {
                data_table_ = table->name();
                data_row_.clear();
            }
            ImGui::TableNextColumn();
            ImGui::Text("%zu%s", table->size(), table->loaded() ? "" : " (not loaded)");
            ImGui::TableNextColumn();
            ImGui::Text("%llu", static_cast<unsigned long long>(table->revision()));
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(data.source() != DataSource::Json);
            if (ImGui::SmallButton("Reload")) {
                data_reload_ = table->name();
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    const DataIssues& issues = data.last_issues();
    if (!issues.all().empty() && ImGui::CollapsingHeader("Issues from the last load", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const DataIssue& issue : issues.all()) {
            const bool error = issue.level == DataIssue::Level::Error;
            ImGui::TextColored(error ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f) : ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%s: %s",
                               issue.where.c_str(), issue.message.c_str());
        }
    }

    DataTableBase* table = data.find(data_table_);
    if (table == nullptr) {
        ImGui::TextDisabled("Select a table.");
        ImGui::End();
        return;
    }
    ImGui::SeparatorText(table->name().c_str());
    ImGui::TextDisabled("%s/: %zu file(s)", table->directory().c_str(), table->files().size());
    ImGui::InputTextWithHint("##filtre", "filter rows", data_filter_, sizeof(data_filter_));
    if (ImGui::BeginChild("lignes", {220.0f, 0.0f}, ImGuiChildFlags_Borders)) {
        for (const std::uint32_t index : table->sorted()) {  // in the order of the identifiers
            const std::string& id = table->id(index);
            if (data_filter_[0] != '\0' && id.find(data_filter_) == std::string::npos) {
                continue;
            }
            if (ImGui::Selectable(id.c_str(), id == data_row_)) {
                data_row_ = id;
            }
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    if (ImGui::BeginChild("ligne", {0.0f, 0.0f}, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
        if (const auto index = table->index_of(data_row_)) {
            ImGui::TextDisabled("%s, index %u", table->row_file(*index).c_str(), *index);
            ImGui::TextDisabled("After defaults and inheritance, as the game reads it:");
            const std::string text = table->resolved(*index).dump(2);
            ImGui::TextUnformatted(text.c_str(), text.c_str() + text.size());
        } else {
            ImGui::TextDisabled("Select a row.");
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

void DebugTools::draw_animation() {
    if (!begin_window("Animation", Window::Animation, 420.0f, 360.0f)) {
        return;
    }
    if (world_ == nullptr) {
        ImGui::TextDisabled("No world (a scene must call watch(world))");
        ImGui::End();
        return;
    }
    entt::registry& registry = world_->registry();
    const auto label = [&registry](entt::entity entity) {
        const Name* name = registry.try_get<Name>(entity);
        return "#" + std::to_string(entt::to_entity(entity)) + (name != nullptr ? " " + name->value : std::string());
    };
    // The entity selected in the inspector if it is animated, else the one chosen here.
    if (registry.valid(selected_) && registry.all_of<Animator>(selected_)) {
        animated_ = selected_;
    }
    if (!registry.valid(animated_) || !registry.all_of<Animator>(animated_)) {
        animated_ = entt::null;
    }
    if (ImGui::BeginCombo("Entity", animated_ == entt::null ? "(none)" : label(animated_).c_str())) {
        for (const auto& [entity, animator] : registry.view<Animator>().each()) {
            if (ImGui::Selectable(label(entity).c_str(), entity == animated_)) {
                animated_ = entity;
                selected_ = entity;
            }
        }
        ImGui::EndCombo();
    }
    if (animated_ == entt::null) {
        ImGui::TextDisabled("No animated entity selected");
        ImGui::End();
        return;
    }
    const Animator& animator = registry.get<Animator>(animated_);
    ImGui::Text("Tick %lld; speed x%.2f; movement %.2f m/s", static_cast<long long>(animator.ticks()),
                animator.speed(), animator.move_speed());
    const Animator::LastEvent& last = animator.last_event();
    if (last.tick >= 0) {
        ImGui::Text("Last event: %s (%s), tick %lld, %lld ticks ago", last.name.c_str(), last.clip.c_str(),
                    static_cast<long long>(last.tick), static_cast<long long>(animator.ticks() - last.tick));
    } else {
        ImGui::TextDisabled("No event yet");
    }
    // The graph (milestone 7, part 9): the state of each layer, the parameters, the last transitions.
    if (animator.graph_active()) {
        ImGui::SeparatorText("Animation graph");
        if (!animator.graph_error().empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", animator.graph_error().c_str());
        }
        ImGui::Text("Body: %s; upper body: %s", animator.graph_state(0).c_str(),
                    animator.graph_state(1).empty() ? "-" : animator.graph_state(1).c_str());
        if (animator.set) {
            for (const GraphParameter& p : animator.set->graph().parameters) {
                if (p.type == GraphParameter::Type::Trigger) {
                    ImGui::BulletText("%s (trigger)", p.name.c_str());
                } else {
                    ImGui::BulletText("%s = %g", p.name.c_str(), animator.parameter(p.name));
                }
            }
        }
        const auto& history = animator.graph_history();
        if (!history.empty() && ImGui::TreeNode("Last transitions")) {
            for (auto it = history.rbegin(); it != history.rend(); ++it) {
                ImGui::Text("tick %lld, %s: %s -> %s (%lld ticks ago)", static_cast<long long>(it->tick),
                            it->layer == 0 ? "body" : "upper", it->from.c_str(), it->to.c_str(),
                            static_cast<long long>(animator.ticks() - it->tick));
            }
            ImGui::TreePop();
        }
    }
    for (int l = 0; l < Animator::kLayers; ++l) {
        const Animator::Layer& layer = animator.layer(l);
        if (layer.motions.empty()) {
            continue;
        }
        ImGui::SeparatorText(l == 0 ? "Full body" : "Upper body");
        if (l > 0) {
            ImGui::Text("Layer weight: %.0f %%", layer.weight / 10.0);
        }
        for (const Animator::Motion& motion : layer.motions) {
            // The clip's timeline: where it is, and its events (ticks, or the blend space's cycle).
            const ClipClock& clock = motion.clock;
            const float ratio = motion.ratio(1.0f);
            char overlay[160];
            if (motion.blend_space()) {
                std::snprintf(overlay, sizeof(overlay), "%s (blend) %.0f %%: phase %.2f, x%.2f", motion.name.c_str(),
                              motion.weight / 10.0, static_cast<double>(ratio), motion.rate / 1000.0);
            } else {
                const double ticks = static_cast<double>(clock.cycle_time()) / ClipClock::kOne;
                std::snprintf(overlay, sizeof(overlay), "%s %.0f %%: %.1f / %d ticks (%.2f s)%s", motion.name.c_str(),
                              motion.weight / 10.0, ticks, clock.cycle_ticks(), ticks / tick_rate(),
                              clock.finished() ? ", finished" : "");
            }
            const ImVec2 at = ImGui::GetCursorScreenPos();
            ImGui::ProgressBar(ratio, ImVec2(-1.0f, 0.0f), overlay);
            const ImVec2 size = ImGui::GetItemRectSize();
            ImDrawList* draw = ImGui::GetWindowDrawList();
            for (std::size_t m = 0; m < motion.marks.size(); ++m) {
                const float x = at.x + size.x * static_cast<float>(motion.marks[m]) / static_cast<float>(clock.cycle_ticks());
                draw->AddLine(ImVec2(x, at.y), ImVec2(x, at.y + size.y), IM_COL32(255, 220, 60, 255), 2.0f);
            }
            if (ImGui::IsItemHovered() && !motion.events.empty()) {
                ImGui::BeginTooltip();
                for (std::size_t m = 0; m < motion.events.size(); ++m) {
                    const Animator::MotionEvent& event = motion.events[m];
                    ImGui::Text("%s (%s): %d / %d%s", event.name.c_str(), event.clip.c_str(), motion.marks[m],
                                clock.cycle_ticks(), event.always ? ", always" : "");
                }
                ImGui::EndTooltip();
            }
        }
    }
    ImGui::End();
}

bool DebugTools::begin_window(const char* title, Window which, float width, float height) {
    bool is_open = true;
    // The first time, each in its own place along the right edge (the scenes' panels are on the left).
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float step = 30.0f * static_cast<float>(which);
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 10.0f - step, viewport->WorkPos.y + 10.0f + step),
                            ImGuiCond_FirstUseEver, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_FirstUseEver);
    const bool visible = ImGui::Begin(title, &is_open);
    if (!is_open) {
        set_open(which, false);  // closed by its cross: remembered like the menu
    }
    if (!visible) {
        ImGui::End();
    }
    return visible;
}

void DebugTools::draw_inspector() {
    // The selected entity, marked in the world (over everything), whether or not the window shows.
    if (world_ != nullptr && world_->registry().valid(selected_)) {
        World& world = *world_;
        entt::registry& registry = world.registry();
        Aabb box;
        const auto wrap = [&box](const Aabb& part) {
            if (!part.empty()) {
                box.add(part.min);
                box.add(part.max);
            }
        };
        const auto add = [&](entt::entity entity) {
            const glm::mat4 matrix = world.world_matrix(entity, 1.0f);
            if (const auto* mesh = registry.try_get<MeshComponent>(entity); mesh != nullptr && mesh->mesh) {
                wrap(transform_box(mesh->mesh->bounds, matrix));
            }
            if (const auto* model = registry.try_get<ModelComponent>(entity); model != nullptr && model->model) {
                wrap(transform_box(model->model->bounds, matrix));
            }
        };
        add(selected_);
        for (const auto& [child, parent] : registry.view<Parent>().each()) {
            if (parent.entity == selected_) {
                add(child);
            }
        }
        DebugLineBuffer& lines = app_.renderer().debug_lines().lines();
        if (!box.empty()) {
            lines.box(box, {1.0f, 0.85f, 0.2f, 1.0f}, true);
        }
        lines.axes(world.world_position(selected_, 1.0f), 0.75f);
    }

    if (!begin_window("Entity Inspector", Window::Inspector, 640.0f, 520.0f)) {
        return;
    }
    if (world_ == nullptr) {
        ImGui::TextDisabled("No world: start a scene made of entities (the 3D demo).");
        ImGui::End();
        return;
    }
    World& world = *world_;
    entt::registry& registry = world.registry();
    ImGui::Text("%s: %zu entities", world_label_.c_str(), world.entity_count());
    if (edited_) {
        ImGui::TextColored(kWarning, "Edited by hand: the simulation is no longer deterministic (replays, captures).");
    }
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputTextWithHint("##filtre", "Filter (name or #number)", filter_, sizeof(filter_));
    ImGui::SameLine();
    ImGui::Checkbox("Named only", &named_only_);

    // The list, in the order of the entity numbers.
    listed_.clear();
    const auto consider = [this, &registry](entt::entity entity) {
        const Name* name = registry.try_get<Name>(entity);
        if (inspector_filter_matches(filter_, static_cast<std::uint32_t>(entt::to_entity(entity)),
                                     name != nullptr ? std::string_view(name->value) : std::string_view())) {
            listed_.push_back(entity);
        }
    };
    if (named_only_) {
        for (const entt::entity entity : registry.view<Name>()) {
            consider(entity);
        }
    } else {
        for (const entt::entity entity : registry.view<entt::entity>()) {
            consider(entity);
        }
    }
    std::sort(listed_.begin(), listed_.end(),
              [](entt::entity a, entt::entity b) { return entt::to_entity(a) < entt::to_entity(b); });

    ImGui::BeginChild("liste", ImVec2(230.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(listed_.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const entt::entity entity = listed_[static_cast<std::size_t>(row)];
            const Name* name = registry.try_get<Name>(entity);
            char label[160];
            std::snprintf(label, sizeof(label), "#%u %s##%u", static_cast<unsigned>(entt::to_entity(entity)),
                          name != nullptr ? name->value.c_str() : "", static_cast<unsigned>(entt::to_integral(entity)));
            if (ImGui::Selectable(label, entity == selected_)) {
                selected_ = entity;
            }
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("entité", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    if (registry.valid(selected_)) {
        draw_entity(world, selected_);
    } else {
        ImGui::TextDisabled("Select an entity in the list, or in the world (the scene's click).");
    }
    ImGui::EndChild();
    ImGui::End();
}

void DebugTools::draw_entity(World& world, entt::entity entity) {
    entt::registry& registry = world.registry();
    const Name* name = registry.try_get<Name>(entity);
    ImGui::Text("Entity #%u (version %u)%s%s", static_cast<unsigned>(entt::to_entity(entity)),
                static_cast<unsigned>(entt::to_version(entity)), name != nullptr ? ": " : "",
                name != nullptr ? name->value.c_str() : "");
    bool hidden = registry.all_of<Hidden>(entity);
    if (ImGui::Checkbox("Hidden", &hidden)) {
        if (hidden) {
            registry.emplace<Hidden>(entity);
        } else {
            registry.remove<Hidden>(entity);
        }
        edited_ = true;
    }

    // Every component the entity has, registered or not. Nothing is added, removed or destroyed from
    // here: the game keeps entities and expects their components (a selected creature and its Walker).
    for (auto [id, storage] : registry.storage()) {
        if (!storage.contains(entity) || storage.info() == entt::type_id<entt::entity>()) {
            continue;
        }
        ImGui::PushID(static_cast<int>(id));
        const std::string title = components_.name(storage.info());
        if (ImGui::CollapsingHeader(title.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            if (!components_.registered(storage.info())) {
                ImGui::TextDisabled("(unregistered type: its content is not shown)");
            } else if (components_.edit(registry, entity, storage.info())) {
                edited_ = true;
            }
        }
        ImGui::PopID();
    }
}

void DebugTools::draw_assets() {
    if (!begin_window("Assets", Window::Assets, 700.0f, 480.0f)) {
        return;
    }
    Assets& assets = app_.assets();
    ImGui::TextDisabled("Hot reload: %s", assets.hot_reload() ? "on" : "off");
    // What the engine holds on the GPU (milestone 8, part 4), and the geometry pools (part 5).
    const GpuMemory gpu = gpu_memory();
    const GeometryPool::Stats geometry = app_.renderer().geometry_stats();
    ImGui::Text("GPU: %.1f MB (textures %.1f, render targets %.1f, buffers %.1f)", megabytes(static_cast<std::size_t>(gpu.total())),
                megabytes(static_cast<std::size_t>(gpu.textures)), megabytes(static_cast<std::size_t>(gpu.targets)),
                megabytes(static_cast<std::size_t>(gpu.buffers)));
    ImGui::TextDisabled("Geometry: %zu page(s), %.1f MB used, %.2f MB pending release; %zu file(s) prefetching",
                        geometry.pages, megabytes(geometry.used_bytes), megabytes(geometry.waiting_bytes),
                        assets.prefetching());
    const std::vector<AssetTypeStats> stats = assets.stats();
    const std::vector<std::vector<AssetInfo>> infos = assets.infos();
    std::size_t total = 0;
    if (ImGui::BeginTable("types", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("Loaded");
        ImGui::TableSetupColumn("Memory (MB)");
        ImGui::TableSetupColumn("Loads");
        ImGui::TableSetupColumn("Failures");
        ImGui::TableHeadersRow();
        for (const AssetTypeStats& type : stats) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(type.type.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%zu", type.count);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", megabytes(type.bytes));
            ImGui::TableNextColumn();
            ImGui::Text("%zu", type.loads);
            ImGui::TableNextColumn();
            ImGui::Text("%zu", type.failures);
            total += type.bytes;
        }
        ImGui::EndTable();
    }
    ImGui::Text("Total: %.2f MB (GPU, and main memory for sounds)", megabytes(total));
    if (ImGui::Button("Free unused assets")) {
        collect_request_ = true;
    }
    if (!last_reload_.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", last_reload_.c_str());
    }
    for (std::size_t t = 0; t < infos.size() && t < stats.size(); ++t) {
        if (infos[t].empty()) {
            continue;
        }
        if (!ImGui::TreeNode(stats[t].type.c_str(), "%s (%zu)", stats[t].type.c_str(), infos[t].size())) {
            continue;
        }
        std::vector<const AssetInfo*> sorted;
        for (const AssetInfo& info : infos[t]) {
            sorted.push_back(&info);
        }
        std::sort(sorted.begin(), sorted.end(), [](const AssetInfo* a, const AssetInfo* b) { return a->key < b->key; });
        const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_Resizable;
        if (ImGui::BeginTable("assets", 5, flags)) {
            ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("MB", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Users", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableHeadersRow();
            for (const AssetInfo* info : sorted) {
                ImGui::PushID(info->key.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(info->key.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%.2f", megabytes(info->bytes));
                ImGui::TableNextColumn();
                ImGui::Text("%ld", info->users);
                ImGui::TableNextColumn();
                if (info->failed) {
                    ImGui::TextColored(kError, "placeholder");
                    ImGui::SetItemTooltip("%s", info->error.c_str());
                } else if (!info->reload_error.empty()) {
                    ImGui::TextColored(kWarning, "error");
                    ImGui::SetItemTooltip("Reload rejected, the previous content is kept:\n%s", info->reload_error.c_str());
                } else {
                    ImGui::TextUnformatted("ready");
                }
                ImGui::TableNextColumn();
                ImGui::BeginDisabled(!Assets::reloadable(static_cast<AssetType>(t)));
                if (ImGui::SmallButton("Reload")) {
                    reload_request_ = std::make_pair(t, info->key);
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::TreePop();
    }
    ImGui::End();
}

void DebugTools::draw_input() {
    if (!begin_window("Input", Window::Input, 560.0f, 420.0f)) {
        return;
    }
    const Input& input = app_.input();
    ImGui::Text("Profile: %s", input.profiles().empty() ? "(none)" : input.profile_label(input.profile()).c_str());
    ImGui::Text("Last device: %s; gamepads connected: %d",
                input.last_device() == InputDevice::Gamepad ? "gamepad" : "keyboard and mouse", input.gamepad_count());
    const glm::vec2 pointer = input.pointer();
    if (pointer.x >= 0.0f) {
        ImGui::Text("Pointer: %.0f, %.0f px", static_cast<double>(pointer.x), static_cast<double>(pointer.y));
    } else {
        ImGui::TextUnformatted("Pointer: outside the window");
    }
    if (input.replaying()) {
        ImGui::TextColored(kWarning, "Real input ignored (replaying a recording)");
    } else if (!input.enabled()) {
        ImGui::TextColored(kWarning, "Real input ignored (disabled)");
    }
    if (input.muted()) {
        ImGui::TextColored(kWarning, "Muted (a state above receives it)");
    }
    if (input.action_count() == 0) {
        ImGui::TextDisabled("No action declared by the scene.");
        ImGui::End();
        return;
    }
    if (ImGui::BeginTable("actions", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Action");
        ImGui::TableSetupColumn("Bindings");
        ImGui::TableSetupColumn("State");
        ImGui::TableHeadersRow();
        for (ActionId action = 0; action < static_cast<ActionId>(input.action_count()); ++action) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(input.action_name(action).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(input.describe(action).c_str());
            ImGui::TableNextColumn();
            if (input.is_axis(action)) {
                const glm::vec2 value = input.axis(action);
                ImGui::Text("%+.2f %+.2f", static_cast<double>(value.x), static_cast<double>(value.y));
            } else if (input.down(action)) {
                ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "down");
            }
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void DebugTools::draw_audio() {
    if (!begin_window("Audio", Window::Audio, 520.0f, 520.0f)) {
        return;
    }
    Audio& audio = app_.audio();
    const AudioStats stats = audio.stats();
    ImGui::Text("Output: %s%s", stats.device_name.c_str(), stats.device ? "" : " (stopped)");
    ImGui::Text("%d Hz, %d channels, buffer %.0f ms", stats.sample_rate, stats.channels, static_cast<double>(stats.device_latency_ms));
    ImGui::SeparatorText("Volumes");
    float master = audio.master_volume();
    if (ImGui::SliderFloat("Master", &master, 0.0f, 1.0f)) {
        audio.set_master_volume(master);
    }
    for (int g = 0; g < kSoundGroupCount; ++g) {
        const auto group = static_cast<SoundGroup>(g);
        ImGui::PushID(g);
        float volume = audio.group_volume(group);
        if (ImGui::SliderFloat(group_label(group), &volume, 0.0f, 1.0f)) {
            audio.set_group_volume(group, volume);
        }
        ImGui::SameLine();
        bool paused = audio.group_paused(group);
        if (ImGui::Checkbox("paused", &paused)) {
            audio.set_group_paused(group, paused);
        }
        ImGui::PopID();
    }
    ImGui::SeparatorText("Voices");
    ImGui::Text("Sounds: %d; music: %d; streams: %d", stats.voices, stats.musics, stats.streams);
    if (const Music* music = audio.music()) {
        ImGui::Text("Music: %s", music->name.c_str());
    }
    ImGui::Text("Played: %zu; merged: %zu; stolen: %zu", stats.played, stats.merged, stats.stolen);
    ImGui::Text("Rejected: %zu inaudible, %zu per-file limit, %zu voice limit", stats.dropped_inaudible,
                stats.dropped_sound_limit, stats.dropped_voice_limit);
    if (stats.last_refused.empty()) {
        ImGui::TextDisabled("No sound rejected.");
    } else {
        ImGui::TextColored(kWarning, "Last rejected: %s (%s)", stats.last_refused.c_str(), refusal_label(stats.last_refused_reason));
    }
    ImGui::Text("Peak: mix %.2f, output %.2f, limiter %.2f", static_cast<double>(stats.mix_peak),
                static_cast<double>(stats.peak), static_cast<double>(stats.limiter_gain));
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset")) {
        audio.reset_peak();
    }
    const std::vector<Audio::ActiveSound> sounds = audio.active_sounds();
    if (!sounds.empty() && ImGui::BeginTable("voix", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Sound");
        ImGui::TableSetupColumn("Group");
        ImGui::TableSetupColumn("Loudness");
        ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        for (const Audio::ActiveSound& sound : sounds) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(sound.name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(group_label(sound.group));
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", static_cast<double>(sound.loudness));
            ImGui::TableNextColumn();
            ImGui::Text("%s%s%s", sound.placed ? "positional " : "", sound.looping ? "looping " : "", sound.stopping ? "stopping" : "");
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void DebugTools::draw_states() {
    if (!begin_window("Game States", Window::States, 420.0f, 260.0f)) {
        return;
    }
    if (stacks_.empty()) {
        ImGui::TextDisabled("No state stack: start the playable slice (--states).");
    }
    for (const WatchedStack& watched : stacks_) {
        const StateStack& stack = *watched.stack;
        ImGui::SeparatorText(watched.label.c_str());
        ImGui::Text("%zu state(s)%s", stack.size(), stack.has_pending() ? ", transitions pending" : "");
        ImGui::PushID(watched.stack);
        if (stack.size() > 0 && ImGui::BeginTable("pile", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("#");
            ImGui::TableSetupColumn("State");
            ImGui::TableSetupColumn("Below");
            ImGui::TableHeadersRow();
            for (std::size_t i = stack.size(); i-- > 0;) {  // the top first, as the stack is drawn
                const GameState& state = *stack.at(i);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%zu%s", i, i + 1 == stack.size() ? " (top)" : "");
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(state.name());
                ImGui::TableNextColumn();
                ImGui::Text("%s, %s", state.transparent() ? "shows below" : "hides below", state.blocking() ? "freezes below" : "updates below");
            }
            ImGui::EndTable();
        }
        ImGui::PopID();
    }
    ImGui::End();
}

}  // namespace moteur
