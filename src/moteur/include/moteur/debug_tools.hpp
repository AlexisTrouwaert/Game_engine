#pragma once

#include <entt/core/type_info.hpp>
#include <entt/entity/registry.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace moteur {

class Application;
class StateStack;
class World;

// How the entity inspector shows and edits each type of component. The engine registers its own
// (Transform, Name, LightSource...); the game registers its own the same way, so that the engine
// never needs to know them:
//
//   tools->components().add<Health>("Santé", [](Health& health) {
//       return ImGui::SliderFloat("Vie", &health.value, 0.0f, 1.0f);
//   });
//
// The function draws the widgets of one component (ImGui) and returns true when it changed it. It
// works on a copy, which then replaces the component (registry.replace): the signals fire, so the
// engine's caches (the world matrix of a still entity) follow. A type nobody registered is still
// listed, by its C++ name, without its content.
class ComponentInspectors {
public:
    template <typename T>
    using Edit = std::function<bool(T&)>;

    // A component with data. Registering a type again replaces its entry.
    template <typename T>
    void add(std::string name, Edit<T> edit) {
        static_assert(!std::is_empty_v<T>, "a tag component has no data: use add_tag()");
        Entry entry;
        entry.name = std::move(name);
        entry.edit = [edit = std::move(edit)](entt::registry& registry, entt::entity entity) {
            T value = registry.get<T>(entity);
            if (!edit(value)) {
                return false;
            }
            registry.replace<T>(entity, std::move(value));
            return true;
        };
        entries_[entt::type_id<T>().hash()] = std::move(entry);
    }

    // A tag (an empty component, such as Hidden): only its name is shown.
    template <typename T>
    void add_tag(std::string name) {
        static_assert(std::is_empty_v<T>, "add_tag() is for empty components");
        Entry entry;
        entry.name = std::move(name);
        entries_[entt::type_id<T>().hash()] = std::move(entry);
    }

    // The name shown for a component type (its C++ name when not registered).
    std::string name(const entt::type_info& type) const;
    bool registered(const entt::type_info& type) const { return entries_.contains(type.hash()); }

    // Runs the registered function of `type` on the entity's component (it draws its widgets).
    // True if the component was changed. False for a tag, or a type not registered.
    bool edit(entt::registry& registry, entt::entity entity, const entt::type_info& type) const;

    // The engine's components (see world.hpp). DebugTools does it at creation.
    void add_engine_components();

private:
    struct Entry {
        std::string name;
        std::function<bool(entt::registry&, entt::entity)> edit;  // empty for a tag
    };
    std::unordered_map<entt::id_type, Entry> entries_;
};

// Whether an entity of the inspector's list passes its filter: an empty filter keeps everything;
// otherwise the filter must appear in the name (letters compared without case, ASCII only) or be the
// entity's number ("#12" or "12").
bool inspector_filter_matches(std::string_view filter, std::uint32_t entity_number, std::string_view name);

// The debug tools of the engine (milestone 4, part 8), in ImGui windows:
// - the entity inspector: the entities of a World (with their Name), a filter, the selection, and
//   the components of the selected one, editable;
// - the asset browser: what the asset manager holds, by type, memory, users, state, a reload button;
// - the inputs: every action with its bindings and state, the devices;
// - the audio: device, voices, volumes by group, the last sound refused by a limit;
// - the game states: the stacks being watched, from the bottom to the top.
//
// The Application creates it with the debug interface (ApplicationConfig::debug_ui), never without.
// The game decides where its windows are opened (menu_items(), inside its own DEBUG menu) and
// draws them (draw(), from its render()). Which windows are open is kept between two runs, with the
// windows' places, in imgui.ini in the player's preferences directory.
//
// Scenes tell it what to show: watch(world) while their world exists (and forget() in their
// destructor), select(entity) when the game picks something, watch(stack) for a StateStack.
//
// Editing a component by hand breaks the determinism of the simulation (a replay, a capture): it is
// a tool, and the inspector says so once it has happened. It never adds, removes or destroys
// anything (except the Hidden tag): the game keeps entities and expects their components.
class DebugTools {
public:
    enum class Window { Inspector, Assets, Input, Audio, States };
    static constexpr int kWindowCount = 5;

    explicit DebugTools(Application& app);
    ~DebugTools();

    DebugTools(const DebugTools&) = delete;
    DebugTools& operator=(const DebugTools&) = delete;

    ComponentInspectors& components() { return components_; }

    // The world shown by the inspector (the last one watched). `label`: its name in the window.
    void watch(World& world, std::string label);
    void forget(World& world);
    World* world() const { return world_; }

    void watch(StateStack& stack, std::string label);
    void forget(StateStack& stack);

    // The entity shown by the inspector (entt::null: none).
    void select(entt::entity entity) { selected_ = entity; }
    entt::entity selected() const { return selected_; }
    // true once something was changed by hand in the current world.
    bool edited() const { return edited_; }

    bool open(Window window) const { return open_[static_cast<std::size_t>(window)]; }
    void set_open(Window window, bool open);

    // One MenuItem per window (checked when open), for the game's DEBUG menu.
    void menu_items();
    // The open windows. From the game's render(), after the scene (the inspector marks the selected
    // entity with debug lines, drawn with the scene's camera).
    void draw();
    // What the windows' buttons asked for that loads or frees (reload an asset, free the unused
    // ones): done here, between two frames. The Application calls it.
    void between_frames();

    // For the settings file (imgui.ini), and its tests: "Inspector=1" lines.
    static const char* window_key(Window window);
    // Reads one line; false if it is not one of ours.
    bool read_setting(std::string_view line);
    std::string write_settings() const;

private:
    // Begins a window whose cross closes it; false (and already ended) when it is collapsed.
    bool begin_window(const char* title, Window which, float width, float height);
    void draw_inspector();
    void draw_entity(World& world, entt::entity entity);
    void draw_assets();
    void draw_input();
    void draw_audio();
    void draw_states();

    Application& app_;
    ComponentInspectors components_;
    bool open_[kWindowCount] = {};

    World* world_ = nullptr;
    std::string world_label_;
    entt::entity selected_ = entt::null;
    bool edited_ = false;
    char filter_[64] = {};
    bool named_only_ = false;
    std::vector<entt::entity> listed_;  // reused each frame

    struct WatchedStack {
        StateStack* stack;
        std::string label;
    };
    std::vector<WatchedStack> stacks_;

    std::optional<std::pair<std::size_t, std::string>> reload_request_;  // type index, key
    bool collect_request_ = false;
    std::string last_reload_;  // the result of the last button, shown next to them
};

}  // namespace moteur
