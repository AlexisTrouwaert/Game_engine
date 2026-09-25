#include "moteur/debug_tools.hpp"

#include <imgui.h>
#include <imgui_internal.h>  // settings handlers (the open windows in imgui.ini)

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "moteur/aabb.hpp"
#include "moteur/application.hpp"
#include "moteur/debug_lines.hpp"
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
        case SoundGroup::Music: return "Musique";
        case SoundGroup::Effects: return "Effets";
        case SoundGroup::Ambience: return "Ambiance";
        case SoundGroup::Interface: return "Interface";
    }
    return "?";
}

const char* refusal_label(const std::string& reason) {
    if (reason == "inaudible") {
        return "trop faible pour être entendu";
    }
    if (reason == "sound_limit") {
        return "limite par fichier";
    }
    if (reason == "voice_limit") {
        return "limite de voix";
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
    changed |= ImGui::DragFloat3("Échelle", &transform.scale.x, 0.01f, 0.0f, 0.0f, "%.3f");
    return changed;
}

bool edit_material(Material& material) {
    bool changed = edit_linear_color("Couleur", material.base_color);
    changed |= ImGui::SliderFloat("Métal", &material.metallic, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Rugosité", &material.roughness, 0.0f, 1.0f);
    changed |= edit_linear_color("Émission", material.emissive);
    changed |= ImGui::Checkbox("Deux faces", &material.double_sided);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Projette une ombre", &material.casts_shadow);
    ImGui::TextDisabled("Textures : %s%s%s%s%s", material.base_color_texture ? "couleur " : "",
                        material.metallic_roughness_texture ? "métal-rugosité " : "",
                        material.normal_texture ? "normales " : "", material.occlusion_texture ? "occlusion " : "",
                        material.emissive_texture ? "émission" : "");
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
    add<PreviousTransform>("Transform au tick précédent", [](PreviousTransform& previous) {
        const glm::vec3& p = previous.value.position;
        ImGui::Text("Position : %.3f %.3f %.3f", static_cast<double>(p.x), static_cast<double>(p.y), static_cast<double>(p.z));
        ImGui::TextDisabled("(copiée à chaque tick : l'entité bouge, elle est interpolée)");
        return false;
    });
    add<Parent>("Parent", [](Parent& parent) {
        if (parent.entity == entt::null) {
            ImGui::TextUnformatted("aucun");
        } else {
            ImGui::Text("Entité #%u", static_cast<unsigned>(entt::to_entity(parent.entity)));
        }
        return false;
    });
    add<Name>("Nom", [](Name& name) {
        char buffer[128] = {};
        std::snprintf(buffer, sizeof(buffer), "%s", name.value.c_str());
        if (ImGui::InputText("Nom", buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
            name.value = buffer;
            return true;
        }
        return false;
    });
    add_tag<Hidden>("Caché");
    add<MeshComponent>("Maillage", [](MeshComponent& component) {
        if (component.mesh) {
            ImGui::Text("%u triangles, %.2f Mo", component.mesh->index_count / 3, megabytes(component.mesh->gpu_bytes));
        }
        return edit_material(component.material);
    });
    add<ModelComponent>("Modèle", [](ModelComponent& component) {
        if (component.model) {
            const Model& model = *component.model;
            const glm::vec3 size = model.bounds.size();
            ImGui::Text("%zu parties, %zu triangles, %.2f Mo", model.parts.size(), model.triangle_count, megabytes(model.gpu_bytes));
            ImGui::Text("Taille : %.2f x %.2f x %.2f m", static_cast<double>(size.x), static_cast<double>(size.y),
                        static_cast<double>(size.z));
        }
        return false;
    });
    add<LightSource>("Lumière", [](LightSource& light) {
        bool changed = edit_linear_color("Couleur", light.color);
        changed |= ImGui::DragFloat("Intensité", &light.intensity, 0.05f, 0.0f, 1000.0f);
        changed |= ImGui::DragFloat("Portée (m)", &light.range, 0.05f, 0.0f, 1000.0f);
        changed |= ImGui::Checkbox("Ombres", &light.casts_shadows);
        return changed;
    });
    add<Billboard>("Billboard", [](Billboard& billboard) {
        bool changed = ImGui::DragFloat2("Taille (m)", &billboard.size.x, 0.01f, 0.0f, 100.0f);
        changed |= edit_linear_color("Couleur", billboard.options.color);
        changed |= ImGui::Checkbox("Additif", &billboard.options.additive);
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
            ImGui::TextUnformatted("aucun");
            return false;
        }
        ImGui::Text("Entité #%u", static_cast<unsigned>(entt::to_entity(parent.entity)));
        ImGui::SameLine();
        if (ImGui::SmallButton("Choisir")) {
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
    static constexpr const char* kLabels[kWindowCount] = {"Inspecteur d'entités", "Assets", "Entrées", "Audio", "États de jeu"};
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
        last_reload_ = app_.assets().reload(type, key) ? "« " + key + " » rechargé"
                                                       : "« " + key + " » non rechargé (voir le journal)";
    }
    if (collect_request_) {
        collect_request_ = false;
        const std::size_t freed = app_.assets().collect_garbage();
        last_reload_ = std::to_string(freed) + " asset(s) libéré(s)";
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
        for (auto [child, parent] : registry.view<Parent>().each()) {
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

    if (!begin_window("Inspecteur d'entités", Window::Inspector, 640.0f, 520.0f)) {
        return;
    }
    if (world_ == nullptr) {
        ImGui::TextDisabled("Aucun monde : lancer une scène faite d'entités (la démo 3D).");
        ImGui::End();
        return;
    }
    World& world = *world_;
    entt::registry& registry = world.registry();
    ImGui::Text("%s : %zu entités", world_label_.c_str(), world.entity_count());
    if (edited_) {
        ImGui::TextColored(kWarning, "Modifié à la main : la simulation n'est plus déterministe (rejeux, captures).");
    }
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputTextWithHint("##filtre", "Filtre (nom ou #numéro)", filter_, sizeof(filter_));
    ImGui::SameLine();
    ImGui::Checkbox("Nommées seulement", &named_only_);

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
        ImGui::TextDisabled("Choisir une entité dans la liste, ou dans le monde (le clic de la scène).");
    }
    ImGui::EndChild();
    ImGui::End();
}

void DebugTools::draw_entity(World& world, entt::entity entity) {
    entt::registry& registry = world.registry();
    const Name* name = registry.try_get<Name>(entity);
    ImGui::Text("Entité #%u (version %u)%s%s", static_cast<unsigned>(entt::to_entity(entity)),
                static_cast<unsigned>(entt::to_version(entity)), name != nullptr ? " : " : "",
                name != nullptr ? name->value.c_str() : "");
    bool hidden = registry.all_of<Hidden>(entity);
    if (ImGui::Checkbox("Cachée", &hidden)) {
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
                ImGui::TextDisabled("(type non inscrit : son contenu n'est pas affiché)");
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
    ImGui::TextDisabled("Rechargement à chaud : %s", assets.hot_reload() ? "actif" : "inactif");
    const std::vector<AssetTypeStats> stats = assets.stats();
    const std::vector<std::vector<AssetInfo>> infos = assets.infos();
    std::size_t total = 0;
    if (ImGui::BeginTable("types", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("En mémoire");
        ImGui::TableSetupColumn("Mémoire (Mo)");
        ImGui::TableSetupColumn("Chargements");
        ImGui::TableSetupColumn("Échecs");
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
    ImGui::Text("Total : %.2f Mo (GPU, et mémoire centrale pour les sons)", megabytes(total));
    if (ImGui::Button("Libérer les assets inutilisés")) {
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
            ImGui::TableSetupColumn("Fichier", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Mo", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Utilisateurs", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("État", ImGuiTableColumnFlags_WidthFixed);
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
                    ImGui::TextColored(kError, "remplacé");
                    ImGui::SetItemTooltip("%s", info->error.c_str());
                } else if (!info->reload_error.empty()) {
                    ImGui::TextColored(kWarning, "erreur");
                    ImGui::SetItemTooltip("Rechargement refusé, l'ancien contenu reste :\n%s", info->reload_error.c_str());
                } else {
                    ImGui::TextUnformatted("prêt");
                }
                ImGui::TableNextColumn();
                ImGui::BeginDisabled(!Assets::reloadable(t));
                if (ImGui::SmallButton("Recharger")) {
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
    if (!begin_window("Entrées", Window::Input, 560.0f, 420.0f)) {
        return;
    }
    const Input& input = app_.input();
    ImGui::Text("Profil : %s", input.profiles().empty() ? "(aucun)" : input.profile_label(input.profile()).c_str());
    ImGui::Text("Dernier périphérique : %s ; manettes branchées : %d",
                input.last_device() == InputDevice::Gamepad ? "manette" : "clavier et souris", input.gamepad_count());
    const glm::vec2 pointer = input.pointer();
    if (pointer.x >= 0.0f) {
        ImGui::Text("Pointeur : %.0f, %.0f px", static_cast<double>(pointer.x), static_cast<double>(pointer.y));
    } else {
        ImGui::TextUnformatted("Pointeur : hors de la fenêtre");
    }
    if (!input.enabled()) {
        ImGui::TextColored(kWarning, "Entrées réelles ignorées (rejeu d'un enregistrement)");
    }
    if (input.muted()) {
        ImGui::TextColored(kWarning, "Muette (un état au-dessus les reçoit)");
    }
    if (input.action_count() == 0) {
        ImGui::TextDisabled("Aucune action déclarée par la scène.");
        ImGui::End();
        return;
    }
    if (ImGui::BeginTable("actions", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Action");
        ImGui::TableSetupColumn("Liaisons");
        ImGui::TableSetupColumn("État");
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
                ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "enfoncé");
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
    ImGui::Text("Sortie : %s%s", stats.device_name.c_str(), stats.device ? "" : " (arrêtée)");
    ImGui::Text("%d Hz, %d canaux, tampon %.0f ms", stats.sample_rate, stats.channels, static_cast<double>(stats.device_latency_ms));
    ImGui::SeparatorText("Volumes");
    float master = audio.master_volume();
    if (ImGui::SliderFloat("Général", &master, 0.0f, 1.0f)) {
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
        if (ImGui::Checkbox("en pause", &paused)) {
            audio.set_group_paused(group, paused);
        }
        ImGui::PopID();
    }
    ImGui::SeparatorText("Voix");
    ImGui::Text("Sons : %d ; musiques : %d ; flux : %d", stats.voices, stats.musics, stats.streams);
    if (const Music* music = audio.music()) {
        ImGui::Text("Musique : %s", music->name.c_str());
    }
    ImGui::Text("Lancés : %zu ; fusionnés : %zu ; volés : %zu", stats.played, stats.merged, stats.stolen);
    ImGui::Text("Refusés : %zu inaudibles, %zu limite par fichier, %zu limite de voix", stats.dropped_inaudible,
                stats.dropped_sound_limit, stats.dropped_voice_limit);
    if (stats.last_refused.empty()) {
        ImGui::TextDisabled("Aucun son refusé.");
    } else {
        ImGui::TextColored(kWarning, "Dernier refusé : %s (%s)", stats.last_refused.c_str(), refusal_label(stats.last_refused_reason));
    }
    ImGui::Text("Crête : mixage %.2f, sortie %.2f, limiteur %.2f", static_cast<double>(stats.mix_peak),
                static_cast<double>(stats.peak), static_cast<double>(stats.limiter_gain));
    ImGui::SameLine();
    if (ImGui::SmallButton("Remettre à zéro")) {
        audio.reset_peak();
    }
    const std::vector<Audio::ActiveSound> sounds = audio.active_sounds();
    if (!sounds.empty() && ImGui::BeginTable("voix", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Son");
        ImGui::TableSetupColumn("Groupe");
        ImGui::TableSetupColumn("Force");
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
            ImGui::Text("%s%s%s", sound.placed ? "placé " : "", sound.looping ? "boucle " : "", sound.stopping ? "s'arrête" : "");
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void DebugTools::draw_states() {
    if (!begin_window("États de jeu", Window::States, 420.0f, 260.0f)) {
        return;
    }
    if (stacks_.empty()) {
        ImGui::TextDisabled("Aucune pile d'états : lancer le test « États de jeu ».");
    }
    for (const WatchedStack& watched : stacks_) {
        const StateStack& stack = *watched.stack;
        ImGui::SeparatorText(watched.label.c_str());
        ImGui::Text("%zu état(s)%s", stack.size(), stack.has_pending() ? ", transitions en attente" : "");
        ImGui::PushID(watched.stack);
        if (stack.size() > 0 && ImGui::BeginTable("pile", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("#");
            ImGui::TableSetupColumn("État");
            ImGui::TableSetupColumn("Dessous");
            ImGui::TableHeadersRow();
            for (std::size_t i = stack.size(); i-- > 0;) {  // the top first, as the stack is drawn
                const GameState& state = *stack.at(i);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%zu%s", i, i + 1 == stack.size() ? " (dessus)" : "");
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(state.name());
                ImGui::TableNextColumn();
                ImGui::Text("%s, %s", state.transparent() ? "visible" : "caché", state.blocking() ? "figé" : "mis à jour");
            }
            ImGui::EndTable();
        }
        ImGui::PopID();
    }
    ImGui::End();
}

}  // namespace moteur
