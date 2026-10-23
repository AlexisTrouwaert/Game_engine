#include "demo3d.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#if !defined(MOTEUR_SHIPPING)
#include <imgui.h>
#endif

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "moteur/animator.hpp"
#include "moteur/billboard_renderer.hpp"
#include "moteur/debug_tools.hpp"
#include "moteur/color.hpp"
#include "moteur/debug_lines.hpp"
#include "moteur/file_io.hpp"
#include "moteur/map_document.hpp"
#include "moteur/fixed_timestep.hpp"
#include "moteur/image.hpp"
#include "moteur/mesh_renderer.hpp"
#include "moteur/paths.hpp"
#include "moteur/profiler.hpp"
#include "moteur/sprite_renderer.hpp"

#include "map_objects.hpp"
#include "sandbox_data.hpp"

namespace {

constexpr float kWallHeight = 1.5f;       // metres: high enough to block, low enough to see over
constexpr float kCreatureSpeed = 2.0f;    // cells per second, as in the 2D demo
constexpr float kSentSpeed = 3.0f;        // cells per second, when sent somewhere by a click
constexpr int kRoom = 10;                 // the walls make rooms of 10 x 10 cells (see demo_wall)

// A color picked by eye (sRGB) turned into the linear value the lighting works with.
glm::vec3 linear(float r, float g, float b) {
    return moteur::srgb_to_linear(glm::vec3(r, g, b));
}

moteur::Material surface(glm::vec3 color, float roughness, float metallic = 0.0f) {
    moteur::Material material;
    material.base_color = glm::vec4(color, 1.0f);
    material.roughness = roughness;
    material.metallic = metallic;
    return material;
}

moteur::Material glowing(glm::vec3 emissive) {
    moteur::Material material;
    material.base_color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    material.emissive = emissive;
    material.casts_shadow = false;
    return material;
}

moteur::Transform place(glm::vec3 position, glm::vec3 scale, float yaw = 0.0f) {
    moteur::Transform transform;
    transform.position = position;
    transform.rotation = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
    transform.scale = scale;
    return transform;
}

// The demo's own components: the game's, not the engine's.
struct Walker {  // a creature
    glm::vec2 velocity;             // cells per second
    std::optional<glm::vec2> goal;  // sent there by a click
};
struct Health {
    float value;  // [0, 1]
};
struct Stature {  // an animated character: how tall it stands (its bar goes above)
    float height;  // metres
};
struct Archetype {  // what a character of the slice is: its row of the "personnages" table
    moteur::DataRef<CharacterData> data;
    float pace = 1.0f;  // its own pace: multiplies the row's speed
};
struct HeroTag {};  // the hero of the slice (saves tell it from the creatures)
struct Remains {  // a dead creature (milestone 9 bis, part 2): lies, then dissolves
    int ticks = 0;  // since its death
};
struct Hunter {  // a creature of the slice (milestone 6): wanders, sees the hero, chases, strikes
    glm::vec2 wander{0.0f};  // its way while it does not chase (turned back when blocked)
    bool chasing = false;
    int lost_ticks = 0;      // ticks without seeing the hero while chasing
    int cooldown = 0;        // ticks before its next blow
};

// Their descriptions (milestone 9, part 3): saved, shown in the inspector, without anything written
// by hand. The keys are those of the saves of milestone 7 (the game's language).
MOTEUR_REFLECT(Walker, MOTEUR_FIELD_AS(velocity, "v", {.unit = "cells/s", .flags = moteur::kFieldDefault | moteur::kFieldLogic}),
               MOTEUR_FIELD_AS(goal, "but", {.doc = "sent there by a click", .flags = moteur::kFieldDefault | moteur::kFieldLogic}));
MOTEUR_REFLECT(Health, MOTEUR_FIELD_AS(value, "vie", {.min = 0.0, .max = 1.0, .flags = moteur::kFieldDefault | moteur::kFieldLogic}));
MOTEUR_REFLECT(Stature, MOTEUR_FIELD_AS(height, "hauteur", {.min = 0.1, .max = 20.0, .unit = "m"}));
MOTEUR_REFLECT(Archetype,
               MOTEUR_FIELD_AS(data, "personnage", {.doc = "its row of the table personnages",
                                                    .flags = moteur::kFieldDefault | moteur::kFieldLogic, .required = true}),
               MOTEUR_FIELD_AS(pace, "allure", {.min = 0.0, .doc = "multiplies the row's speed",
                                                .flags = moteur::kFieldDefault | moteur::kFieldLogic}));
MOTEUR_REFLECT(HeroTag);
MOTEUR_REFLECT(Remains, MOTEUR_FIELD_AS(ticks, "ticks", {.min = 0, .unit = "ticks", .flags = moteur::kFieldDefault | moteur::kFieldLogic}));
MOTEUR_REFLECT(Hunter, MOTEUR_FIELD_AS(wander, "errance", {.flags = moteur::kFieldDefault | moteur::kFieldLogic}),
               MOTEUR_FIELD_AS(chasing, "chasse", {.flags = moteur::kFieldDefault | moteur::kFieldLogic}),
               MOTEUR_FIELD_AS(lost_ticks, "perdu", {.min = 0, .unit = "ticks", .flags = moteur::kFieldDefault | moteur::kFieldLogic}),
               MOTEUR_FIELD_AS(cooldown, "recharge", {.min = 0, .unit = "ticks", .flags = moteur::kFieldDefault | moteur::kFieldLogic}));

// Registered once for the program, with the forms the saves of milestone 7 wrote.
void register_slice_components() {
    moteur::ComponentTypes& types = moteur::component_types();
    types.add<Walker>("marcheur");
    types.add<Health>("sante", moteur::upgrade_from_bare("vie"));
    types.add<Stature>("stature", moteur::upgrade_from_bare("hauteur"));
    types.add<Archetype>("archetype", moteur::upgrade_from_array({"personnage", "allure"}));
    types.add<HeroTag>("heros", [](const nlohmann::json& value) { return value.is_object() ? value : nlohmann::json::object(); });
    types.add<Hunter>("chasseur", moteur::upgrade_from_array({"errance", "chasse", "perdu", "recharge"}));
    types.add<Remains>("restes");
}

// The remains of the dead (milestone 9 bis, part 2): they lie this long, then dissolve over this long.
constexpr int kRemainsLie = 90;      // ticks
constexpr int kRemainsVanish = 90;   // ticks
// A blow's flash: almost white, faded out over this many ticks.
constexpr glm::vec3 kFlashColor{3.0f, 2.7f, 2.3f};
constexpr int kFlashTicks = 10;

// The row's identifier of a saved Archetype (an object since milestone 9, an array before).
std::string archetype_id(const nlohmann::json& archetype) {
    return archetype.is_object() ? archetype.at("personnage").get<std::string>() : archetype.at(0).get<std::string>();
}

constexpr float kCharacterRadius = 0.35f;  // metres: the hero and the creatures, as circles
constexpr float kEye = 1.5f;               // metres above the feet: what sees and is seen (a level's lines of sight)

// The floor of a height, in half metres: the parvis (0.84 m) and the street (0) are two floors.
int floor_of(float height) {
    return static_cast<int>(std::lround(height * 2.0f));
}

// A creature of a level is picked by its shape (rays, milestone 9, part 7): a capsule a little
// wider than its collider, as tall as its row says.
void add_selectable(entt::registry& registry, entt::entity creature, const CharacterData& row) {
    moteur::Selectable selectable;
    selectable.kind = moteur::PickKind::Character;
    selectable.radius = row.radius + 0.12f;
    selectable.height = row.height;
    registry.emplace_or_replace<moteur::Selectable>(creature, selectable);
}
constexpr int kHeroSight = 14;             // cells: the hero's field of view
const char* const kSliceMap = "maps/tranche.json";
const char* const kDust = "effects/dust.json";
const char* const kBrazierFire = "effects/brazier_fire.json";

constexpr float kCreatureHeight = 1.5f;  // the skeletons of the demo without the slice (the slice reads its table)
constexpr float kBodyTop = 1.6f;         // the bodies of primitives (the demo of milestone 3)
// The knight carries every weapon of its pack in its file: its shield stays, the sword is a file
// of its own held on its right hand (see spawn_hero()), the others go.
const std::vector<std::string> kKnightHidden = {"1H_Sword", "1H_Sword_Offhand", "2H_Sword", "Badge_Shield",
                                                "Rectangle_Shield", "Spike_Shield"};

// Centre of a room, where its brazier stands.
bool is_brazier_cell(int i, int j) {
    return i % kRoom == kRoom / 2 && j % kRoom == kRoom / 2;
}

}  // namespace

Demo3D::Demo3D(moteur::Application& app, const Options& options, bool standalone)
    : app_(app), options_(options), standalone_(standalone) {
    options_.map_size = std::clamp(options_.map_size, 10, 400);
    options_.creatures = std::max(options_.creatures, 0);
    options_.decor = std::max(options_.decor, 0);
    if (options_.hero && options_.level.empty() && !options_.slice_map) {
        options_.level = kSliceLevel;  // the slice plays its level since milestone 9, part 11
    }
    moteur::Renderer& renderer = app.renderer();

    cube_ = moteur::make_asset(moteur::Mesh::create(renderer, moteur::make_cube(), "demo.cube"));
    tile_ = moteur::make_asset(moteur::Mesh::create(renderer, moteur::make_plane(), "demo.tile"));
    sphere_ = moteur::make_asset(moteur::Mesh::create(renderer, moteur::make_sphere(0.5f, 16, 8), "demo.sphere"));
    rock_ = moteur::make_asset(moteur::Mesh::create(renderer, moteur::make_sphere(0.5f, 7, 4), "demo.rock"));  // faceted
    if (options_.ui != nullptr) {
        ui_ = options_.ui;
    } else {
        own_ui_ = std::make_unique<moteur::Ui>(app, kFont);
        ui_ = own_ui_.get();
    }
    hud_ = &ui_->open(kHud, 0);
    sky_ = moteur::Environment::create(renderer, moteur::make_sky(512, 256), "demo.sky");
    if (app.assets().exists(kBarrel)) {
        barrel_ = app.assets().model(kBarrel);
    } else {
        SDL_Log("Demo 3D: no barrel model (%s), crates instead", kBarrel);  // tools/models/fetch_test_models.py
    }
    if (options_.hero) {  // the slice: the ARPG's assets, when its folder is there (see kPavingA)
        if (app.assets().exists(kPavingA) && app.assets().exists(kPavingB)) {
            paving_a_ = app.assets().model(kPavingA);
            paving_b_ = app.assets().model(kPavingB);
        } else {
            SDL_Log("Demo 3D: no cobblestones (%s), checkered floor", kPavingA);
        }
        if (app.assets().exists(kStreetLamp)) {
            street_lamp_ = app.assets().prefab(kStreetLamp);
            // Its emissive glass and flame are where its light sits: they cast no shadow, or the
            // light would light nothing. Their material is the model's, shared: set once.
            const moteur::ComponentType* model_type = moteur::component_types().find<moteur::ModelComponent>();
            for (const moteur::Prefab::Entity& entity : street_lamp_->entities()) {
                for (const moteur::Prefab::Component& component : entity.components) {
                    const auto* model = component.type == model_type
                                            ? static_cast<const moteur::ModelComponent*>(component.value->get())
                                            : nullptr;
                    if (model != nullptr && model->model) {
                        for (moteur::Material& material : model->model->materials) {
                            if (material.emissive != glm::vec3(0.0f)) {
                                material.casts_shadow = false;
                            }
                        }
                    }
                }
            }
        } else {
            SDL_Log("Demo 3D: no street lamp (%s), plain posts", kStreetLamp);
        }
    }

    // A soft round glow (torch halos), and a white pixel (health bars).
    moteur::Image glow;
    glow.width = 64;
    glow.height = 64;
    glow.pixels.resize(64 * 64 * 4);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            const float dx = (static_cast<float>(x) + 0.5f) / 32.0f - 1.0f;
            const float dy = (static_cast<float>(y) + 0.5f) / 32.0f - 1.0f;
            const float r = std::min(std::sqrt(dx * dx + dy * dy), 1.0f);
            const auto i = static_cast<std::size_t>((y * 64 + x) * 4);
            glow.pixels[i] = glow.pixels[i + 1] = glow.pixels[i + 2] = 255;
            glow.pixels[i + 3] = static_cast<std::uint8_t>(std::lround((1.0f - r) * (1.0f - r) * 255.0f));
        }
    }
    moteur::TextureSettings mask;
    mask.mipmaps = true;
    glow_ = moteur::make_asset(renderer.create_texture(glow, mask, "demo.glow"));
    debug_flags_ = moteur::WorldDebugFlags::declare(app.console().variables());
    effects_on_ = &app.console().variables().add_bool("effects.enabled", true, "effets de particules de la tranche (purement visuels)");
    setup_serializers();
    if (options_.hero) {
        register_commands();
    }
    animated_ = options_.hero && characters_available(app.assets());
    if (options_.hero && !animated_) {
        SDL_Log("Slice: no animated characters (python tools/models/fetch_test_characters.py): bodies of primitives");
    }
    std::vector<moteur::MapObject> recorded;
    if (options_.hero && !options_.export_objects_path.empty()) {
        recording_ = &recorded;
    }
    if (options_.hero && !options_.level.empty()) {
        open_level();  // milestone 9, part 11: the slice's level, its decor and the creatures' places
    } else {
        build_map();
        build_floor();
        build_decor();
    }
    // A saved slice: its characters come from the save, not from a new draw.
    std::optional<moteur::SaveGame> saved;
    Uint64 load_start = 0;
    double decode_ms = 0.0;
    if (options_.hero && options_.saved_game) {
        load_start = SDL_GetPerformanceCounter();
        saved = *options_.saved_game;
    } else if (options_.hero && !options_.load_path.empty()) {
        std::string error;
        load_start = SDL_GetPerformanceCounter();
        const auto bytes = moteur::read_file_bytes(options_.load_path);
        saved = bytes ? moteur::SaveGame::decode(*bytes, error) : std::nullopt;
        decode_ms = static_cast<double>(SDL_GetPerformanceCounter() - load_start) * 1000.0 /
                    static_cast<double>(SDL_GetPerformanceFrequency());
        if (!saved) {
            throw std::runtime_error("Tranche : sauvegarde « " + options_.load_path + " » illisible : " +
                                     (bytes ? error : std::string("fichier absent")));
        }
    }
    if (!saved) {
        spawn_creatures();
    }
    light_braziers();
    if (options_.hero) {
        if (saved) {
            const Uint64 apply_start = SDL_GetPerformanceCounter();
            load(*saved);
            SDL_Log("Save: read and decoded in %.2f ms, the world given back in %.2f ms", decode_ms,
                    static_cast<double>(SDL_GetPerformanceCounter() - apply_start) * 1000.0 /
                        static_cast<double>(SDL_GetPerformanceFrequency()));
        } else {
            spawn_hero();
        }
        if (ring_ == entt::null && selected_ != entt::null) {
            create_selection_marks();  // a level without creatures: the hero's marks still
        }
        moteur::Assets& assets = app.assets();
        if (assets.exists(kAmbience) && assets.exists(kBank)) {
            // The sounds as events (milestone 9 bis, part 7): the game's system, or one of its own.
            sound_ = options_.sound;
            if (sound_ == nullptr) {
                own_sound_ = std::make_unique<moteur::AudioEvents>(app.audio(), assets);
                own_sound_->load(kBank);
                sound_ = own_sound_.get();
            }
            ambience_ = sound_->play("ambiance/tombes");
        }
    }

    // The export of the draws as the slice map's objects (milestone 7, part 8).
    if (recording_ != nullptr) {
        recording_ = nullptr;
        moteur::MapDocument document = moteur::MapDocument::from(*slice_map_);
        for (moteur::MapObject& object : recorded) {
            document.add_object(std::move(object));
        }
        std::string error;
        if (moteur::write_file_atomic(options_.export_objects_path, document.to_json(), error)) {
            SDL_Log("Slice: %zu objects written into '%s'", document.objects().size(), options_.export_objects_path.c_str());
        } else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Slice: %s", error.c_str());
        }
        stop_after_export_ = true;  // at the first update (the loop is not running yet)
    }

    // The engine's defaults, whatever an earlier test changed.
    moteur::ShadowOptions shadows;
    if (options_.point_budget >= 0) {
        shadows.point_budget = options_.point_budget;
    }
    renderer.meshes().set_shadows(shadows);
    renderer.meshes().set_culling(true);
    renderer.set_render_scale(1.0f);
    renderer.set_exposure(1.0f);

    const auto middle = static_cast<float>(options_.map_size) * 0.5f;
    if (!saved) {  // a loaded slice has its camera from the save
        camera_.set_target({middle, 0.0f, middle});
    }
    if (hero_ != entt::null && !saved) {
        camera_.set_target(foot_of(hero_));
        follow_ = true;
    }
    if (level_mode()) {
        renderer.meshes().set_ids(true);  // the creature under the pointer outlined (milestone 9, part 7)
        // Looking north, along the street: it goes up the screen. Not saved (the same in every game):
        // set for a loaded game too.
        camera_.set_angles(180.0f, 55.0f);
    }
    if (options_.fake_mouse) {
        mouse_ = options_.fake_mouse_position;
    }

    declare_actions(app);
    moteur::Input& input = app.input();
    actions_ = find_actions(input);
    input.set_enabled(!options_.no_input);

    // The debug tools (menu mode): the world in the inspector, with the demo's own components.
#if !defined(MOTEUR_SHIPPING)
    if (moteur::DebugTools* tools = app.debug_tools()) {
        tools->watch(world_, "Démo 3D");
        // The demo's components need nothing more: their descriptions draw them (component_types()).
    }
#endif
}

std::string profile_label(moteur::Application& app, const std::string& profile) {
    const std::string id = "input-profile-" + profile;
    return app.localization().has(id) ? app.localization().format(id) : app.input().profile_label(profile);
}

std::string profile_label(moteur::Application& app) {
    return profile_label(app, app.input().profile());
}

void Demo3D::declare_actions(moteur::Application& app) {
    // The actions, then their bindings: the defaults, and the player's changes over them.
    moteur::Input& input = app.input();
    input.clear_actions();
    input.add_button("move_to");
    input.add_axis("move");
    input.add_axis("camera");
    for (const char* name : {"zoom_in", "zoom_out", "select", "next", "follow", "projection"}) {
        input.add_button(name);
    }
    for (int i = 0; i < kSkills; ++i) {
        input.add_button("skill_" + std::to_string(i + 1));
    }
    input.add_button("pause");
    for (const char* name : kMenuActions) {  // the menus (moteur::Ui): arrows, pad, Enter, a click
        input.add_button(name);
    }
    input.load_bindings(moteur::read_text_file(app.assets().file_path("input/demo3d.json")), "input/demo3d.json");
    const std::string directory = app.preferences_directory();
    const std::string user_file = directory.empty() ? std::string() : directory + "demo3d_bindings.json";
    if (!user_file.empty() && SDL_GetPathInfo(user_file.c_str(), nullptr)) {
        try {
            input.apply_user_bindings(moteur::read_text_file(user_file), user_file);
        } catch (const std::exception& e) {
            SDL_Log("Demo 3D: %s (the player's bindings are ignored)", e.what());
        }
    }
}

Demo3D::Actions Demo3D::find_actions(const moteur::Input& input) {
    Actions actions{};
    actions.move_to = input.find("move_to");
    actions.move = input.find("move");
    actions.camera = input.find("camera");
    actions.zoom_in = input.find("zoom_in");
    actions.zoom_out = input.find("zoom_out");
    actions.select = input.find("select");
    actions.next = input.find("next");
    actions.follow = input.find("follow");
    actions.projection = input.find("projection");
    for (int i = 0; i < kSkills; ++i) {
        actions.skills[i] = input.find("skill_" + std::to_string(i + 1));
    }
    actions.pause = input.find("pause");
    return actions;
}

std::string Demo3D::user_bindings_path() const {
    const std::string directory = app_.preferences_directory();
    return directory.empty() ? std::string() : directory + "demo3d_bindings.json";
}

Demo3D::~Demo3D() {
    ui_->close(kHud);
    for (const char* name : {"god", "heal", "tp", "spawn", "kill", "fog"}) {
        app_.console().remove(name);
    }
    app_.renderer().meshes().set_fog({});
    app_.renderer().meshes().set_cutout({});
#if !defined(MOTEUR_SHIPPING)
    if (moteur::DebugTools* tools = app_.debug_tools()) {
        tools->forget(world_);
    }
#endif
    if (sound_ != nullptr) {
        sound_->stop(ambience_);
        sound_->set_reverb_zones({});
        sound_->set_occlusion_test({});
    }
}

void Demo3D::add_static(const moteur::Asset<moteur::Mesh>& mesh, const moteur::Transform& transform, const moteur::Material& material) {
    entt::registry& registry = world_.registry();
    const entt::entity entity = registry.create();
    registry.emplace<moteur::Transform>(entity, transform);
    registry.emplace<moteur::MeshComponent>(entity, mesh, material);
    ++static_draws_;
}

void Demo3D::build_map() {
    if (options_.hero) {
        // The slice: the map of its file (the same rooms as below), and the grid of the game.
        slice_map_ = app_.assets().map(kSliceMap);
        tileset_ = slice_map_->tileset();
        ground_ = slice_map_->tile_id("sol");
        wall_ = slice_map_->tile_id("mur");
        map_.emplace(slice_map_->tiles());
        options_.map_size = slice_map_->width();
        grid_ = moteur::NavGrid(*map_, tileset_);
        clearance_ = moteur::ClearanceMap(grid_);
        explored_ = moteur::ExploredMap(grid_.width(), grid_.height());
        fog_cells_.assign(static_cast<std::size_t>(grid_.width()) * static_cast<std::size_t>(grid_.height()), 0);
        app_.renderer().meshes().set_fog_cells(grid_.width(), grid_.height(), fog_cells_);
        return;
    }
    const int n = options_.map_size;
    ground_ = tileset_.add({"", true, false});
    wall_ = tileset_.add({"", false, true});
    map_.emplace(n, n, 2);
    map_->fill(0, ground_);
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            if (demo_wall(i, j)) {
                map_->set(1, {i, j}, wall_);
            }
        }
    }
}

void Demo3D::build_floor() {
    const int n = options_.map_size;
    if (paving_a_ && paving_b_) {
        // The ARPG's cobblestones: 8 m tiles laid over the whole map, slightly narrowed so that a
        // whole number of them covers it; B in patches (a fixed scatter, about one tile in five)
        // to break A's repetition. The tiles' material receives shadows and casts none.
        constexpr float kTile = 8.0f;
        const int count = static_cast<int>(std::ceil(static_cast<float>(n) / kTile));
        const float side = static_cast<float>(n) / static_cast<float>(count);
        entt::registry& registry = world_.registry();
        for (int z = 0; z < count; ++z) {
            for (int x = 0; x < count; ++x) {
                const bool patch = (x * 7 + z * 3) % 5 == 0;
                const entt::entity tile = registry.create();
                registry.emplace<moteur::Transform>(
                    tile, place({(static_cast<float>(x) + 0.5f) * side, 0.0f, (static_cast<float>(z) + 0.5f) * side},
                                glm::vec3(side / kTile, 1.0f, side / kTile)));
                registry.emplace<moteur::ModelComponent>(tile, patch ? paving_b_ : paving_a_);
                static_draws_ += (patch ? paving_b_ : paving_a_)->parts.size();
            }
        }
        for (moteur::Material& material : paving_a_->materials) {
            material.casts_shadow = false;
        }
        for (moteur::Material& material : paving_b_->materials) {
            material.casts_shadow = false;
        }
        return;
    }
    // The floor receives shadows but casts none: it is below everything.
    moteur::Material light = surface(linear(0.55f, 0.58f, 0.5f), 0.85f);
    moteur::Material dark = surface(linear(0.44f, 0.47f, 0.4f), 0.85f);
    light.casts_shadow = false;
    dark.casts_shadow = false;
    if (!options_.merged_floor) {
        // One tile per cell: the case measured against the merged floor below.
        for (int j = 0; j < n; ++j) {
            for (int i = 0; i < n; ++i) {
                const glm::vec3 centre(static_cast<float>(i) + 0.5f, 0.0f, static_cast<float>(j) + 0.5f);
                add_static(tile_, place(centre, glm::vec3(1.0f)), ((i + j) & 1) == 0 ? light : dark);
            }
        }
    } else {
        // One mesh per block of 10 x 10 cells and per color: few draws, but no culling inside a block.
        const int blocks = (n + kRoom - 1) / kRoom;
        const moteur::MeshData quad = moteur::make_plane();
        for (int bj = 0; bj < blocks; ++bj) {
            for (int bi = 0; bi < blocks; ++bi) {
                for (int tone = 0; tone < 2; ++tone) {
                    moteur::MeshData block;
                    for (int j = bj * kRoom; j < std::min((bj + 1) * kRoom, n); ++j) {
                        for (int i = bi * kRoom; i < std::min((bi + 1) * kRoom, n); ++i) {
                            if (((i + j) & 1) != tone) {
                                continue;
                            }
                            const auto base = static_cast<std::uint32_t>(block.vertices.size());
                            for (moteur::Vertex3D vertex : quad.vertices) {
                                vertex.position += glm::vec3(static_cast<float>(i) + 0.5f, 0.0f, static_cast<float>(j) + 0.5f);
                                block.vertices.push_back(vertex);
                            }
                            for (const std::uint32_t index : quad.indices) {
                                block.indices.push_back(base + index);
                            }
                        }
                    }
                    if (block.indices.empty()) {
                        continue;
                    }
                    add_static(moteur::make_asset(moteur::Mesh::create(app_.renderer(), block, "demo.floor block")),
                               moteur::Transform{}, tone == 0 ? light : dark);
                }
            }
        }
    }
}

void Demo3D::build_decor() {
    const int n = options_.map_size;
    // Walls: a 1.5 m block per wall cell.
    moteur::Material stone = surface(linear(0.62f, 0.58f, 0.52f), 0.8f);
    stone.fades = options_.hero;  // the slice: walls before the hero fade
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            if (is_wall(i, j)) {
                add_static(cube_, place({static_cast<float>(i) + 0.5f, kWallHeight * 0.5f, static_cast<float>(j) + 0.5f},
                                        {1.0f, kWallHeight, 1.0f}),
                           stone);
            }
        }
    }

    // A brazier in the middle of each room: a post, and a flame whose light is a torch (see
    // light_braziers()).
    const moteur::Material iron = surface(linear(0.3f, 0.3f, 0.32f), 0.5f, 1.0f);
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            if (is_brazier_cell(i, j)) {
                const glm::vec3 foot(static_cast<float>(i) + 0.5f, 0.0f, static_cast<float>(j) + 0.5f);
                add_static(cube_, place(foot + glm::vec3(0.0f, 0.6f, 0.0f), {0.25f, 1.2f, 0.25f}), iron);
                braziers_.push_back(foot + glm::vec3(0.0f, 1.45f, 0.0f));
            }
        }
    }

    if (options_.hero && slice_map_) {
        place_street_lamps();
    }

    // Rocks, trees, crates and barrels, on free cells, the same on every run of a seed.
    Random random(options_.seed + 7);
    const moteur::Material rock = surface(linear(0.5f, 0.5f, 0.48f), 0.9f);
    const moteur::Material bark = surface(linear(0.35f, 0.24f, 0.15f), 0.9f);
    const moteur::Material leaves = surface(linear(0.22f, 0.42f, 0.18f), 0.8f);
    const moteur::Material wood = surface(linear(0.6f, 0.44f, 0.26f), 0.7f);
    moteur::Material bark_fading = bark;  // the slice: trees before the hero fade too
    moteur::Material leaves_fading = leaves;
    bark_fading.fades = leaves_fading.fades = options_.hero;
    // One piece of decor, the same whether it was drawn below or read from the map's objects (the
    // slice, milestone 7, part 8; types of map_objects.hpp).
    const auto place_decor = [&](const std::string& type, glm::vec3 spot, float yaw, const nlohmann::json& props) {
        if (type == "rocher") {
            const glm::vec3 scale = moteur::vec3_from_json(props.at("echelle"));
            add_static(rock_, place(spot + glm::vec3(0.0f, scale.y * 0.3f, 0.0f), scale, yaw), rock);
        } else if (type == "arbre") {
            const float height = props.at("hauteur").get<float>();
            add_static(cube_, place(spot + glm::vec3(0.0f, height * 0.5f, 0.0f), {0.16f, height, 0.16f}, yaw), bark_fading);
            const float crown = props.at("couronne").get<float>();
            add_static(sphere_, place(spot + glm::vec3(0.0f, height + crown * 0.3f, 0.0f), glm::vec3(crown, crown * 0.85f, crown)), leaves_fading);
        } else if (type == "caisse" || (type == "tonneau" && !barrel_)) {
            const float edge = props.at("arete").get<float>();
            add_static(cube_, place(spot + glm::vec3(0.0f, edge * 0.5f, 0.0f), glm::vec3(edge), yaw), wood);
        } else if (type == "tonneau") {
            entt::registry& registry = world_.registry();
            const entt::entity barrel = registry.create();
            registry.emplace<moteur::Transform>(barrel, place(spot, glm::vec3(1.0f), yaw));
            registry.emplace<moteur::ModelComponent>(barrel, barrel_);
            static_draws_ += barrel_->parts.size();
        }
    };
    if (from_objects()) {
        // A broken object of the map (unknown type, property of the wrong kind) is left out with a
        // message: the map is data, it never stops the game (milestone 7, part 10).
        std::vector<std::string> unknown;
        for (const moteur::MapObject& object : slice_map_->objects()) {
            const ObjectType* type = find_object_type(object.type);
            if (type == nullptr) {
                if (std::find(unknown.begin(), unknown.end(), object.type) == unknown.end()) {
                    unknown.push_back(object.type);
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Tranche : type d'objet inconnu « %s » (objet %s...) : ignoré",
                                object.type.c_str(), object.id.c_str());
                }
                continue;
            }
            try {
                place_decor(object.type, object.position, glm::radians(object.rotation), object_props(*type, object.props));
            } catch (const std::exception& e) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Tranche : objet %s (%s) ignoré : %s", object.id.c_str(),
                            object.type.c_str(), e.what());
            }
        }
        return;
    }
    for (int k = 0; k < options_.decor; ++k) {
        glm::ivec2 cell(0);
        do {
            cell = {static_cast<int>(random.next() * static_cast<float>(n)), static_cast<int>(random.next() * static_cast<float>(n))};
        } while (!map_->walkable(tileset_, cell) || is_brazier_cell(cell.x, cell.y));
        const glm::vec3 spot(static_cast<float>(cell.x) + 0.2f + 0.6f * random.next(), 0.0f,
                             static_cast<float>(cell.y) + 0.2f + 0.6f * random.next());
        const float yaw = random.next() * glm::two_pi<float>();
        const float kind = random.next();
        const float size = random.next();
        std::string type;
        nlohmann::json props;
        if (kind < 0.4f) {
            const glm::vec3 scale = (0.3f + 0.5f * size) * glm::vec3(1.0f, 0.55f + 0.3f * random.next(), 0.8f + 0.4f * random.next());
            type = "rocher";
            props = {{"echelle", moteur::to_json_value(scale)}};
        } else if (kind < 0.65f) {
            type = "arbre";
            props = {{"hauteur", 1.0f + 0.8f * size}, {"couronne", 0.9f + 0.7f * size}};
        } else {
            type = kind < 0.9f ? "caisse" : "tonneau";
            props = {{"arete", 0.45f + 0.35f * size}};
        }
        place_decor(type, spot, yaw, props);
        if (recording_ != nullptr) {
            recording_->push_back({"", type, spot, glm::degrees(yaw), 1.0f, props});
        }
    }
}

bool Demo3D::from_objects() const {
    return options_.hero && slice_map_ && !slice_map_->objects().empty();
}

void Demo3D::spawn_creatures() {
    // As in the 2D demo: anywhere walkable, one of eight directions, each at its own pace.
    static constexpr glm::vec2 kDirections[8] = {
        {-1.0f, -1.0f}, {-1.0f, 0.0f}, {-1.0f, 1.0f}, {0.0f, -1.0f}, {0.0f, 1.0f}, {1.0f, -1.0f}, {1.0f, 0.0f}, {1.0f, 1.0f},
    };
    Random random(options_.seed);
    const auto n = static_cast<float>(options_.map_size);
    std::vector<CreatureSpawn> spawns;
    if (level_mode()) {
        // The level's spawns (their instances' properties), by identifier.
        for (const LevelSpawn& spawn : level_spawns_) {
            try {
                const nlohmann::json& props = spawn.properties;
                const std::string character = props.at("personnage").get<std::string>();
                if (app_.data().get<CharacterData>().find(character) == nullptr) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Tranche : apparition %u ignorée : « %s » n'est pas dans la table personnages",
                                spawn.id, character.c_str());
                    continue;
                }
                const std::optional<glm::vec3> place = free_place(spawn.position);
                const glm::vec3 at = place.value_or(spawn.position);
                spawns.push_back({{at.x, at.z}, moteur::vec2_from_json(props.at("direction")), props.value("rythme", 1.0f),
                                  moteur::vec3_from_json(props.at("couleur")), props.value("vie", 1.0f), character, at.y});
            } catch (const std::exception& e) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Tranche : apparition %u ignorée : %s", spawn.id, e.what());
            }
        }
    } else if (from_objects()) {
        for (const moteur::MapObject& object : slice_map_->objects()) {
            if (object.type != "monstre") {
                continue;
            }
            try {
                const nlohmann::json props = object_props(*find_object_type("monstre"), object.props);
                const std::string character = props.at("personnage").get<std::string>();
                if (options_.hero && app_.data().get<CharacterData>().find(character) == nullptr) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                "Tranche : monstre %s ignoré : le personnage « %s » n'est pas dans la table personnages",
                                object.id.c_str(), character.c_str());
                    continue;
                }
                spawns.push_back({{object.position.x, object.position.z}, moteur::vec2_from_json(props.at("direction")),
                                  props.at("rythme").get<float>(), moteur::vec3_from_json(props.at("couleur")),
                                  props.at("vie").get<float>(), character});
            } catch (const std::exception& e) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Tranche : monstre %s ignoré : %s", object.id.c_str(), e.what());
            }
        }
    } else {
        for (int k = 0; k < options_.creatures; ++k) {
            glm::vec2 position(0.0f);
            do {
                position = {1.0f + random.next() * (n - 2.0f), 1.0f + random.next() * (n - 2.0f)};
            } while (!walkable(position));
            const glm::vec2 direction = kDirections[static_cast<std::size_t>(random.next() * 8.0f) % 8];
            const float pace = 0.75f + 0.5f * random.next();
            const glm::vec3 color = linear(0.35f + 0.55f * random.next(), 0.35f + 0.55f * random.next(), 0.35f + 0.55f * random.next());
            const float health = 0.3f + 0.7f * random.next();
            // The slice: warriors and minions in turn, as rows of the characters' table.
            const std::string character = options_.hero ? (k % 2 == 0 ? "squelette_guerrier" : "squelette_sbire") : std::string();
            spawns.push_back({position, direction, pace, color, health, character});
            if (recording_ != nullptr) {
                recording_->push_back({"", "monstre", {position.x, 0.0f, position.y}, 0.0f, 1.0f,
                                       {{"personnage", character}, {"rythme", pace}, {"direction", moteur::to_json_value(direction)},
                                        {"vie", health}, {"couleur", moteur::to_json_value(color)}}});
            }
        }
    }
    for (int k = 0; k < static_cast<int>(spawns.size()); ++k) {
        const entt::entity creature = spawn_creature(spawns[static_cast<std::size_t>(k)], k);
        if (k == 0) {
            selected_ = creature;
        }
    }
    if (selected_ == entt::null) {
        return;
    }
    // Start next to the first creature, selected: a ring under it, and a mark where it is sent.
    camera_.set_target(foot_of(selected_));
    create_selection_marks();
}

entt::entity Demo3D::spawn_creature(const CreatureSpawn& spawn, int k) {
    entt::registry& registry = world_.registry();
    const moteur::Material skin = surface(linear(0.85f, 0.7f, 0.55f), 0.6f);
    // A part of the creature, attached to it.
    const auto attach = [&registry](entt::entity creature, const moteur::Asset<moteur::Mesh>& mesh,
                                    const moteur::Transform& transform, const moteur::Material& material) {
        const entt::entity part = registry.create();
        registry.emplace<moteur::Transform>(part, transform);
        registry.emplace<moteur::Parent>(part, creature);
        registry.emplace<moteur::MeshComponent>(part, mesh, material);
        return part;
    };
    const glm::vec2 position = spawn.position;
    const glm::vec2 direction = spawn.direction;
    const float pace = spawn.pace;
    const glm::vec3 color = spawn.color;
    const float health = spawn.health;
    // The creature is where its feet are; its body and head are attached to it (or, in the
    // animated slice, a KayKit skeleton, warriors and minions in turn).
    const moteur::Transform foot = place({position.x, spawn.height, position.y}, glm::vec3(1.0f));
    const moteur::DataRef<CharacterData> row =
        options_.hero ? app_.data().get<CharacterData>().ref(spawn.character) : moteur::DataRef<CharacterData>{};
    const entt::entity creature =
        animated_ ? spawn_animated(row ? row->model.c_str() : kSkeletons[k % 2], foot, row ? row->height : kCreatureHeight, {})
                  : registry.create();
    if (!animated_) {
        registry.emplace<moteur::Transform>(creature, foot);
        registry.emplace<moteur::PreviousTransform>(creature, foot);
    }
    registry.emplace<Walker>(creature, direction * (kCreatureSpeed * pace), std::nullopt);
    registry.emplace<Health>(creature, health);
    if (options_.hero) {
        if (!row) {
            throw std::runtime_error("Tranche : le personnage « " + spawn.character + " » manque dans la table personnages");
        }
        moteur::Collider collider;
        collider.radius = row->radius;
        collider.push_weight = row->weight;
        registry.emplace<moteur::Collider>(creature, collider);
        moteur::Mover mover;
        mover.speed = row->speed * pace;
        mover.move(direction);
        registry.emplace<moteur::Mover>(creature, mover);
        registry.emplace<Hunter>(creature, Hunter{direction});
        registry.emplace<Archetype>(creature, Archetype{row, pace});
        registry.emplace<moteur::PersistentId>(creature, next_persistent_id_++);
        if (level_mode()) {
            add_selectable(registry, creature, *row);  // picked by its shape (milestone 9, part 7)
        }
    }
    registry.emplace<moteur::Name>(creature, "créature " + std::to_string(k));
    if (!animated_) {
        attach(creature, cube_, place({0.0f, 0.5f, 0.0f}, {0.45f, 1.0f, 0.45f}), surface(color, 0.7f));
        attach(creature, sphere_, place({0.0f, 1.2f, 0.0f}, glm::vec3(0.38f)), skin);
    }
    ++creatures_;
    return creature;
}

void Demo3D::create_selection_marks() {
    entt::registry& registry = world_.registry();
    ring_ = registry.create();
    registry.emplace<moteur::Transform>(ring_, place({0.0f, 0.01f, 0.0f}, glm::vec3(1.1f)));
    registry.emplace<moteur::Parent>(ring_, selected_);
    registry.emplace<moteur::MeshComponent>(ring_, tile_, glowing({0.3f, 1.2f, 0.4f}));
    registry.emplace<moteur::Name>(ring_, "sélection");
    goal_ = registry.create();
    registry.emplace<moteur::Transform>(goal_, place(glm::vec3(0.0f), glm::vec3(0.4f)));
    registry.emplace<moteur::MeshComponent>(goal_, tile_, glowing({1.2f, 1.0f, 0.2f}));
    registry.emplace<moteur::Hidden>(goal_);
    registry.emplace<moteur::Name>(goal_, "destination");
}

void Demo3D::spawn_hero() {
    entt::registry& registry = world_.registry();
    const moteur::DataRef<CharacterData> row = app_.data().get<CharacterData>().ref("chevalier");
    if (!row) {
        throw std::runtime_error("Tranche : la ligne chevalier manque dans la table personnages");
    }
    // The start point of the map (the first room, away from its brazier) or of the level.
    glm::vec3 start_at(0.0f);
    if (level_mode()) {
        const moteur::LevelPoint* point = level_->header().point(moteur::kStartPoint);
        const glm::vec3 wanted = options_.start ? *options_.start : point != nullptr ? point->position : level_->header().bounds.center();
        start_at = free_place(wanted).value_or(wanted);
    } else {
        const glm::vec2 start = moteur::nearest_fit(grid_, moteur::cell_centre(slice_map_->point(moteur::kStartPoint)), kCharacterRadius)
                                    .value_or(moteur::cell_centre(slice_map_->point(moteur::kStartPoint)));
        start_at = {start.x, 0.0f, start.y};
    }
    const moteur::Transform foot = place(start_at, glm::vec3(1.0f));
    if (animated_) {
        // The knight, its sword (a file of its own) on the right_hand point of its description.
        hero_ = spawn_animated(row->model.c_str(), foot, row->height, kKnightHidden);
        registry.emplace<Walker>(hero_, glm::vec2(0.0f), std::nullopt);
        registry.emplace<Health>(hero_, 1.0f);
        registry.emplace<moteur::Name>(hero_, "héros");
        const entt::entity sword = registry.create();
        registry.emplace<moteur::Name>(sword, "épée (right_hand)");
        registry.emplace<moteur::Transform>(sword);
        registry.emplace<moteur::Parent>(sword, hero_);
        registry.emplace<moteur::BoneAttachment>(sword, "right_hand");
        registry.emplace<moteur::ModelComponent>(sword, app_.assets().model(kSword), std::vector<std::uint32_t>{});
    } else {
        hero_ = registry.create();
        registry.emplace<moteur::Transform>(hero_, foot);
        registry.emplace<moteur::PreviousTransform>(hero_, foot);
        registry.emplace<Walker>(hero_, glm::vec2(0.0f), std::nullopt);
        registry.emplace<Health>(hero_, 1.0f);
        registry.emplace<moteur::Name>(hero_, "héros");
        // Taller than the creatures, in blue steel, with a sword at his side.
        const auto part = [&](const moteur::Asset<moteur::Mesh>& mesh, const moteur::Transform& transform,
                              const moteur::Material& material) {
            const entt::entity entity = registry.create();
            registry.emplace<moteur::Transform>(entity, transform);
            registry.emplace<moteur::Parent>(entity, hero_);
            registry.emplace<moteur::MeshComponent>(entity, mesh, material);
        };
        part(cube_, place({0.0f, 0.65f, 0.0f}, {0.5f, 1.3f, 0.5f}), surface(linear(0.25f, 0.4f, 0.8f), 0.35f, 0.8f));
        part(sphere_, place({0.0f, 1.55f, 0.0f}, glm::vec3(0.42f)), surface(linear(0.85f, 0.7f, 0.55f), 0.6f));
        part(cube_, place({0.35f, 0.8f, 0.0f}, {0.06f, 1.0f, 0.12f}), surface(linear(0.8f, 0.8f, 0.85f), 0.2f, 1.0f));
    }
    // Milestone 6: a circle the creatures give way to, moved by paths or straight by keys.
    moteur::Collider collider;
    collider.radius = row->radius;
    collider.push_weight = row->weight;
    registry.emplace<moteur::Collider>(hero_, collider);
    moteur::Mover mover;
    mover.speed = row->speed;
    registry.emplace<moteur::Mover>(hero_, mover);
    registry.emplace<Archetype>(hero_, Archetype{row, 1.0f});
    registry.emplace<HeroTag>(hero_);
    registry.emplace<moteur::PersistentId>(hero_, next_persistent_id_++);
    selected_ = hero_;
}

bool Demo3D::characters_available(moteur::Assets& assets) {
    for (const char* path : {kKnight, kSword, kSkeletons[0], kSkeletons[1]}) {
        if (!assets.exists(path)) {
            return false;
        }
    }
    return true;
}

entt::entity Demo3D::spawn_animated(const char* path, const moteur::Transform& foot, float height,
                                    const std::vector<std::string>& hidden_nodes) {
    moteur::Assets& assets = app_.assets();
    entt::registry& registry = world_.registry();
    const entt::entity entity = registry.create();
    moteur::ModelComponent model{assets.model(path), {}};
    for (const std::string& node : hidden_nodes) {
        model.hide(node);
    }
    const float file_height = model.model->bounds.size().y;
    moteur::Transform placed = foot;
    placed.scale = glm::vec3(file_height > 0.0f ? height / file_height : 1.0f);
    registry.emplace<moteur::Transform>(entity, placed);
    registry.emplace<moteur::PreviousTransform>(entity, placed);
    registry.emplace<moteur::ModelComponent>(entity, std::move(model));
    registry.emplace<Stature>(entity, height);
    moteur::Animator animator =
        moteur::Animator::create(assets.skeleton(path), assets.clips(path), assets.animation_set(kAnimationSet));
    animator.start_graph();  // milestone 7: the clips come from the set's graph; the game sets its parameters
    registry.emplace<moteur::Animator>(entity, std::move(animator));
    return entity;
}

float Demo3D::top_of(entt::entity entity) const {
    const Stature* stature = world_.registry().try_get<Stature>(entity);
    return stature != nullptr ? stature->height : kBodyTop;
}

void Demo3D::attack(entt::entity target) {
    if (!animated_) {
        if (target != entt::null) {
            strike(target);
        }
        return;
    }
    entt::registry& registry = world_.registry();
    moteur::Animator& animator = registry.get<moteur::Animator>(hero_);
    if (!animator.fire("attaque")) {
        return;  // a blow under way: clicking again does not start it again
    }
    attack_target_ = target;
    if (target != entt::null) {  // turned towards it
        const glm::vec2 d = cell_position(target) - cell_position(hero_);
        if (glm::dot(d, d) > 1e-6f) {
            moteur::Mover& mover = registry.get<moteur::Mover>(hero_);
            mover.facing = d / std::sqrt(glm::dot(d, d));
            const glm::quat rotation = moteur::facing_rotation(mover.facing);
            registry.patch<moteur::Transform>(hero_, [&rotation](moteur::Transform& t) { t.rotation = rotation; });
        }
    }
}

void Demo3D::animate_characters(float dt) {
    entt::registry& registry = world_.registry();
    // Facing where they went this tick, their clips at the pace of their feet.
    for (auto [entity, transform, previous, animator] :
         registry.view<moteur::Transform, const moteur::PreviousTransform, moteur::Animator>().each()) {
        const glm::vec2 moved(transform.position.x - previous.value.position.x, transform.position.z - previous.value.position.z);
        const float distance = glm::length(moved);
        if (distance > 1e-5f && !registry.all_of<moteur::Mover>(entity)) {  // a Mover turns its entity itself
            transform.rotation = moteur::facing_rotation(moved / distance);  // KayKit faces +Z; no trigonometry
        }
        const float speed = dt > 0.0f ? distance / dt : 0.0f;
        animator.set_move_speed(static_cast<double>(speed / transform.scale.y));  // metres of the file
    }
    events_.clear();
    moteur::advance_animators(registry, 1, &events_);
    // Root motion (milestone 9 bis, part 3): the clips' own moves, for the next tick's movement
    // (through the walls' collisions). Millimetres of the file, in the model's space (KayKit faces
    // +Z): turned by the facing, scaled by the character's size.
    for (auto [entity, transform, animator, mover] : registry.view<moteur::Transform, moteur::Animator, moteur::Mover>().each()) {
        const glm::ivec3 root = animator.take_root_motion();
        if (root == glm::ivec3(0)) {
            continue;
        }
        const glm::vec2 forward = mover.facing;
        const glm::vec2 side(forward.y, -forward.x);  // the model's +X once turned
        const float metres = transform.scale.y / 1000.0f;
        mover.root_motion += (forward * static_cast<float>(root.z) + side * static_cast<float>(root.x)) * metres;
    }
    for (const moteur::AnimatorEvent& event : events_) {
        if (event.entity != hero_) {
            if (event.name == "impact" && options_.hero) {
                creature_blow(event.entity);
            }
            continue;  // the creatures' own footsteps: silent, there are hundreds
        }
        if (event.name == "impact") {
            // The blow lands on what is within reach now: the creature clicked if it still is,
            // otherwise the nearest one (one that walked into the swing is hit).
            entt::entity target = attack_target_;
            if (target == entt::null || !registry.valid(target) || !in_reach(target)) {
                target = creature_in_reach();
            }
            if (target != entt::null) {
                strike(target);
            }
            attack_target_ = entt::null;
        } else if (event.name.rfind("step", 0) == 0) {
            const glm::vec3 at = foot_of(hero_);
            play_effect(kDust, at + glm::vec3(0.0f, 0.05f, 0.0f), 0.8f);
            play_step(at);
        }
    }
}

bool Demo3D::in_reach(entt::entity creature) const {
    const Archetype* hero = world_.registry().try_get<Archetype>(hero_);
    const float kReach = hero != nullptr ? hero->data->reach : 2.0f;  // metres
    const glm::vec2 d = cell_position(creature) - cell_position(hero_);
    // Not from the parvis to the street below (milestone 9, part 11: a level of several floors).
    return glm::dot(d, d) <= kReach * kReach && std::fabs(foot_of(creature).y - foot_of(hero_).y) < 0.6f;
}

entt::entity Demo3D::creature_in_reach() const {
    const entt::entity nearest = creature_near(foot_of(hero_), 2.0f);
    return nearest != entt::null && in_reach(nearest) ? nearest : entt::null;
}

void Demo3D::hurt(entt::entity creature, float blow) {
    entt::registry& registry = world_.registry();
    Health& health = registry.get<Health>(creature);
    const bool was_alive = health.value > 0.0f;
    const Archetype* victim = registry.try_get<Archetype>(creature);
    health.value = std::max(health.value - blow, 0.0f);
    if (health.value <= 0.0f) {
        Walker& walker = registry.get<Walker>(creature);  // down: it stops where it is
        walker.velocity = glm::vec2(0.0f);
        walker.goal.reset();
    }
    if (was_alive) {
        // The blow flashes on its target (milestone 9 bis, part 2); a death leaves remains, which
        // dissolve after a while.
        moteur::MaterialEffects& effects = registry.get_or_emplace<moteur::MaterialEffects>(creature);
        effects.flash = glm::vec4(kFlashColor, 1.0f);
        if (health.value <= 0.0f) {
            registry.emplace_or_replace<Remains>(creature);
        }
    }
    if (moteur::Animator* animator = registry.try_get<moteur::Animator>(creature)) {
        if (health.value <= 0.0f && was_alive) {
            animator->fire("mort");  // falls, and stays on the ground
        } else if (health.value > 0.0f) {
            animator->fire("touche");  // the blow shakes it, added over what it does (an additive layer)
        }
    }
    if (options_.hero) {
        // Milestone 6: sparks where the blade meets the bones; the dead go up in smoke and no
        // longer block the way.
        const glm::vec3 p = foot_of(creature);
        if (victim != nullptr && !victim->data->hit_effect.empty()) {
            play_effect(victim->data->hit_effect.c_str(), p + glm::vec3(0.0f, 1.0f, 0.0f), 0.8f);  // from its row
        }
        if (health.value <= 0.0f && was_alive) {
            if (victim != nullptr && !victim->data->death_effect.empty()) {
                play_effect(victim->data->death_effect.c_str(), p + glm::vec3(0.0f, 0.1f, 0.0f));
            }
            registry.remove<moteur::Collider>(creature);
            if (moteur::Mover* mover = registry.try_get<moteur::Mover>(creature)) {
                mover->stop();
            }
        }
    }
}

void Demo3D::update_effects() {
    entt::registry& registry = world_.registry();
    for (auto [entity, remains] : registry.view<Remains>().each()) {
        ++remains.ticks;
        const float dissolve = std::clamp(static_cast<float>(remains.ticks - kRemainsLie) / kRemainsVanish, 0.0f, 1.0f);
        if (dissolve > 0.0f) {
            registry.get_or_emplace<moteur::MaterialEffects>(entity).dissolve = dissolve;
        }
    }
    std::vector<entt::entity> done;
    for (auto [entity, effects] : registry.view<moteur::MaterialEffects>().each()) {
        effects.flash.a = std::max(0.0f, effects.flash.a - 1.0f / kFlashTicks);
        if (effects.flash.a <= 0.0f && effects.dissolve <= 0.0f) {
            done.push_back(entity);  // nothing left to show: the meshes go back to the plain path
        }
    }
    registry.remove<moteur::MaterialEffects>(done.begin(), done.end());
    if (ghost_ != entt::null) {
        // Once round its pillar every 9 s, bobbing; facing where it goes.
        const float angle = static_cast<float>(live_ticks_ % 540) / 540.0f * glm::two_pi<float>();
        const float bob = 0.12f * std::sin(static_cast<float>(live_ticks_ % 120) / 120.0f * glm::two_pi<float>());
        moteur::Transform& transform = registry.get<moteur::Transform>(ghost_);
        transform.position = ghost_center_ + glm::vec3(1.6f * std::cos(angle), bob, 1.6f * std::sin(angle));
        transform.rotation = glm::angleAxis(-angle, glm::vec3(0.0f, 1.0f, 0.0f));
        registry.patch<moteur::Transform>(ghost_);
    }
}

void Demo3D::strike(entt::entity creature) {
    entt::registry& registry = world_.registry();
    // The slice: the hero's damage over the creature's life, from their rows.
    const Archetype* attacker = registry.try_get<Archetype>(hero_);
    const Archetype* victim = registry.try_get<Archetype>(creature);
    const float blow = attacker != nullptr && victim != nullptr
                           ? static_cast<float>(attacker->data->damage) / static_cast<float>(victim->data->life)
                           : 0.25f;
    hurt(creature, blow);
    flashes_.push_back({0, creature, live_ticks_ + 45});
    ++hits_;
    if (sound_ != nullptr) {
        sound_->play("combat/impact", {.position = foot_of(creature) + glm::vec3(0.0f, 1.0f, 0.0f)});
    }
}

void Demo3D::play_step(glm::vec3 at) {
    if (sound_ == nullptr) {
        return;
    }
    sound_->set_global("sol", floor_of(at.y) > 0 ? "dalle" : "pave");
    sound_->play("personnage/pas", {.position = at});
}

void Demo3D::play_steps(glm::vec2 before) {
    constexpr float kStride = 0.85f;  // cells between two footsteps
    const glm::vec2 now = cell_position(hero_);
    step_distance_ += glm::length(now - before);
    if (step_distance_ < kStride) {
        return;
    }
    step_distance_ = std::fmod(step_distance_, kStride);
    play_step(foot_of(hero_));
}

void Demo3D::place_street_lamps() {
    // One per "lampadaire" point of the map (tools/maps/place_slice_lamps.py): the model, and its
    // light as a child entity at the flame. Its cell is not walkable (the map's tile).
    entt::registry& registry = world_.registry();
    const moteur::Material iron = surface(linear(0.12f, 0.12f, 0.13f), 0.6f, 1.0f);
    int index = 0;
    for (const glm::ivec2& cell : slice_map_->points("lampadaire")) {
        const glm::vec3 foot(static_cast<float>(cell.x) + 0.5f, 0.0f, static_cast<float>(cell.y) + 0.5f);
        if (street_lamp_) {
            // The ARPG's prefab: the model, its light and its halo.
            moteur::PrefabPlacement placement;
            placement.position = foot;
            placement.rotation = glm::angleAxis(glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            const moteur::PrefabInstance lamp = moteur::instantiate(world_, *street_lamp_, placement);
            registry.emplace_or_replace<moteur::Name>(lamp.root, "lampadaire " + std::to_string(index));
            if (const auto* model = registry.try_get<moteur::ModelComponent>(lamp.root); model != nullptr && model->model) {
                static_draws_ += model->model->parts.size();
            }
        } else {  // a plain iron post where the model would be, and a light on its top
            const entt::entity lamp = registry.create();
            registry.emplace<moteur::Transform>(lamp, place(foot + glm::vec3(0.0f, 1.75f, 0.0f), {0.12f, 3.5f, 0.12f}));
            registry.emplace<moteur::MeshComponent>(lamp, cube_, iron);
            registry.emplace<moteur::Name>(lamp, "lampadaire " + std::to_string(index));
            ++static_draws_;
            const entt::entity light = registry.create();
            moteur::Transform local;
            local.position = glm::vec3(0.0f, 0.5f, 0.0f);  // the post is a unit cube scaled to 3.5 m
            registry.emplace<moteur::Transform>(light, local);
            registry.emplace<moteur::Parent>(light, lamp);
            registry.emplace<moteur::LightSource>(light, glm::vec3(1.0f, 0.55f, 0.25f), 12.0f, 12.0f, true, 0.55f);
            registry.emplace<moteur::Name>(light, "lampadaire " + std::to_string(index) + "/lumiere");
        }
        ++index;
    }
}

void Demo3D::light_braziers() {
    // Every flame is drawn, but only the lights nearest to the camera are sent (the engine takes at
    // most 32; see render()), and among them the engine gives shadows to its budget.
    static constexpr glm::vec3 kFlames[3] = {{1.0f, 0.5f, 0.15f}, {1.0f, 0.65f, 0.25f}, {1.0f, 0.42f, 0.1f}};
    entt::registry& registry = world_.registry();
    moteur::BillboardOptions halo;
    halo.additive = true;
    for (const glm::vec3& position : braziers_) {
        if (level_mode()) {
            play_effect(kBrazierFire, position - glm::vec3(0.0f, 0.1f, 0.0f));  // the prefab has its light
            ++torches_;
            continue;
        }
        const glm::vec3 color = kFlames[torches_ % 3];
        const entt::entity torch = registry.create();
        registry.emplace<moteur::Transform>(torch, place(position, glm::vec3(0.18f)));
        registry.emplace<moteur::MeshComponent>(torch, sphere_, glowing(color * 10.0f));
        registry.emplace<moteur::LightSource>(torch, color, 5.0f, 7.0f, torch_shadows_);
        halo.color = glm::vec4(color * 1.5f, 1.0f);
        registry.emplace<moteur::Billboard>(torch, glow_, glm::vec2(1.0f), halo);
        registry.emplace<moteur::Name>(torch, "torche " + std::to_string(torches_));
        ++torches_;
        if (options_.hero) {
            play_effect(kBrazierFire, position - glm::vec3(0.0f, 0.1f, 0.0f));  // milestone 6: flames over the bowl
        }
    }
}

glm::vec2 Demo3D::cell_position(entt::entity creature) const {
    const glm::vec3 p = world_.registry().get<moteur::Transform>(creature).position;
    return {p.x, p.z};
}

bool Demo3D::walkable(glm::vec2 p) const {
    if (options_.hero) {
        return moteur::circle_fits(grid_, p, kCharacterRadius);
    }
    return map_->walkable(tileset_, glm::ivec2(glm::floor(p)));
}

bool Demo3D::is_wall(int i, int j) const {
    return map_->at(1, {i, j}) == wall_;
}

void Demo3D::move_creatures(float dt) {
    for (auto [entity, transform, walker] : world_.registry().view<moteur::Transform, Walker>().each()) {
        glm::vec2 position(transform.position.x, transform.position.z);
        if (walker.goal) {
            const glm::vec2 to_goal = *walker.goal - position;
            const float distance = glm::length(to_goal);
            if (distance <= kSentSpeed * dt) {
                position = *walker.goal;
                walker.goal.reset();
                walker.velocity = glm::vec2(0.0f);  // arrived: waits there
            } else if (const glm::vec2 next = position + to_goal / distance * (kSentSpeed * dt); walkable(next)) {
                position = next;
            } else {
                walker.goal.reset();  // a wall in the way, and no pathfinding yet: it stops
                walker.velocity = glm::vec2(0.0f);
            }
        } else if (const glm::vec2 next = position + walker.velocity * dt; walkable(next)) {
            position = next;
        } else {
            walker.velocity = -walker.velocity;  // turns back before a wall, as in the 2D demo
        }
        transform.position.x = position.x;
        transform.position.z = position.y;
    }
}

void Demo3D::show_selection() {
    if (selected_ == entt::null) {
        return;
    }
    entt::registry& registry = world_.registry();
    if (registry.get<moteur::Parent>(ring_).entity != selected_) {
        registry.patch<moteur::Parent>(ring_, [this](moteur::Parent& parent) { parent.entity = selected_; });
    }
    std::optional<glm::vec2> goal = registry.get<Walker>(selected_).goal;
    float goal_height = 0.0f;
    if (const moteur::Mover* mover = registry.try_get<moteur::Mover>(selected_);
        mover != nullptr && mover->mode == moteur::MoveMode::ToPoint) {
        goal = glm::vec2(mover->goal.x, mover->goal.z);  // the slice: where the hero walks to
        goal_height = mover->goal.y;
    }
    if (goal) {
        // The mark is a still entity: moved through replace(), so that its cached place is dropped.
        const moteur::Transform mark = place({goal->x, goal_height + 0.012f, goal->y}, glm::vec3(0.4f));
        if (registry.get<moteur::Transform>(goal_) != mark) {
            registry.replace<moteur::Transform>(goal_, mark);
        }
        registry.remove<moteur::Hidden>(goal_);
    } else {
        registry.emplace_or_replace<moteur::Hidden>(goal_);
    }
}

void Demo3D::screen_axes(glm::vec3& ahead, glm::vec3& right) const {
    const glm::vec3 forward = camera_.forward();
    ahead = glm::normalize(glm::vec3(forward.x, 0.0f, forward.z));
    right = glm::cross(ahead, glm::vec3(0.0f, 1.0f, 0.0f));
}

// `direction`: x to the right of the screen, y up the screen, length at most 1.
void Demo3D::move_camera(glm::vec2 direction, float dt) {
    if (direction == glm::vec2(0.0f)) {
        return;
    }
    glm::vec3 ahead, right;
    screen_axes(ahead, right);
    const float speed = camera_.visible_height() * 1.2f;  // the same speed on screen at any zoom
    glm::vec3 target = camera_.target() + (right * direction.x + ahead * direction.y) * (speed * dt);
    const auto n = static_cast<float>(options_.map_size);
    target.x = std::clamp(target.x, 0.0f, n);
    target.z = std::clamp(target.z, 0.0f, n);
    camera_.set_target(target);
}

entt::entity Demo3D::creature_near(glm::vec3 ground, float radius) const {
    entt::entity best = entt::null;
    float best_distance = radius * radius;
    // In the order of creation (reach()), so that a tie always goes to the same one.
    for (auto [entity, walker] : world_.registry().storage<Walker>()->reach()) {
        if (entity == hero_) {
            continue;  // the hero is no creature
        }
        const glm::vec2 d = cell_position(entity) - glm::vec2(ground.x, ground.z);
        const float distance = glm::dot(d, d);
        if (distance <= best_distance && std::fabs(foot_of(entity).y - ground.y) < 1.0f) {
            best_distance = distance;
            best = entity;
        }
    }
    return best;
}

entt::entity Demo3D::next_creature(entt::entity creature) const {
    // Walkers are never removed, so their storage keeps the order of creation.
    const auto& walkers = *world_.registry().storage<Walker>();
    return walkers[(walkers.index(creature) + 1) % walkers.size()];
}

// Escape stays a raw key: it leaves the scene whatever the bindings.
void Demo3D::on_event(const SDL_Event& event) {
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
        if (standalone_) {
            app_.quit();
        } else {
            stop_requested_ = true;
        }
    }
}

void Demo3D::apply_input(float dt) {
    const moteur::Input& input = app_.input();
    // Zoom: every notch of the wheel counts, even several in one tick.
    const int zoom = input.presses(actions_.zoom_out) - input.presses(actions_.zoom_in);
    if (zoom != 0) {
        camera_.set_visible_height(std::clamp(camera_.visible_height() * std::pow(1.0f / 0.9f, static_cast<float>(zoom)), 4.0f, 60.0f));
    }
    if (input.pressed(actions_.projection)) {
        camera_.set_projection(camera_.projection() == moteur::Projection::Perspective ? moteur::Projection::Orthographic
                                                                                       : moteur::Projection::Perspective);
    }
    if (input.pressed(actions_.follow)) {
        follow_ = !follow_;
    }
    if (selected_ == entt::null) {
        return;
    }
    if (hero_ == entt::null && input.pressed(actions_.next)) {
        selected_ = next_creature(selected_);
    }
    // The pointed ground, with the camera of this tick (not the interpolated one of the last frame
    // drawn): a replay gives the same result whatever the frame rate.
    const glm::vec2 pointer = options_.fake_mouse ? options_.fake_mouse_position : input.pointer();
    std::optional<glm::vec3> ground;
    pointed_.reset();
    if (pointer.x >= 0.0f && pointer.y >= 0.0f) {
        if (level_mode()) {
            // Rays against the creatures' shapes and the level (milestone 9, part 7), at the tick.
            pointed_ = moteur::pick(world_.registry(), camera_.screen_ray(pointer), &level_->collision(), navmesh());
            if (pointed_) {
                ground = pointed_->point;
            }
        } else {
            ground = camera_.ground_point(pointer);
        }
    }
    if (hero_ == entt::null && ground && input.pressed(actions_.select)) {
        // Picking a creature: the nearest to the pointed ground (see the part 11 notes).
        if (const entt::entity picked = creature_near(*ground, 1.5f); picked != entt::null) {
            selected_ = picked;
        }
    }
    if (hero_ != entt::null) {
        steer_hero(ground);  // milestone 6
    } else {
        Walker& hero = world_.registry().get<Walker>(selected_);
        // The slice: a click on a creature strikes it when it is within reach, or walks up to it.
        if (hero_ != entt::null && ground && input.pressed(actions_.move_to)) {
            if (const entt::entity target = creature_near(*ground, 0.8f); target != entt::null) {
                pressed_on_creature_ = true;
                if (in_reach(target)) {
                    attack(target);
                } else {
                    hero.goal = cell_position(target);
                }
            }
        }
        if (!input.down(actions_.move_to)) {
            pressed_on_creature_ = false;
        }
        // Move by click: held, the creature keeps heading for the pointer.
        if (ground && input.down(actions_.move_to) && !pressed_on_creature_ && walkable({ground->x, ground->z})) {
            hero.goal = glm::vec2(ground->x, ground->z);
        }
        // Move by keys or stick, relative to the screen: a goal one step ahead.
        if (const glm::vec2 direction = input.axis(actions_.move); direction != glm::vec2(0.0f)) {
            glm::vec3 ahead, right;
            screen_axes(ahead, right);
            const glm::vec3 step = (right * direction.x + ahead * direction.y) * (kSentSpeed * dt);
            const glm::vec2 next = cell_position(selected_) + glm::vec2(step.x, step.z);
            hero.goal = walkable(next) ? std::optional<glm::vec2>(next) : std::nullopt;
            hero.velocity = glm::vec2(0.0f);
        }
    }
    for (int i = 0; i < kSkills; ++i) {
        if (input.pressed(actions_.skills[i])) {
            ++skill_uses_[i];
            flashes_.push_back({i + 1, selected_, live_ticks_ + 45});
            // The slice: the first skill strikes the nearest creature within reach; the second one
            // dashes forward (milestone 9 bis, part 3: root motion, dodging blows meanwhile).
            if (i == 0 && hero_ != entt::null) {
                attack(creature_in_reach());
            } else if (i == 1 && hero_ != entt::null) {
                if (moteur::Animator* animator = world_.registry().try_get<moteur::Animator>(hero_)) {
                    animator->fire("ruee");
                }
            }
        }
    }
    std::erase_if(flashes_, [this](const SkillFlash& flash) { return flash.until_tick < live_ticks_; });
    if (!follow_) {
        move_camera(input.axis(actions_.camera), dt);
    }
}

void Demo3D::update(double dt) {
    ui_seconds_ += dt;
    if (stop_after_export_) {
        app_.quit();
        return;
    }
    elapsed_ += dt;
    ++ticks_;
    if (options_.run_seconds > 0.0 && elapsed_ >= options_.run_seconds) {
        if (standalone_) {
            app_.quit();
        } else {
            stop_requested_ = true;
        }
    }
    last_stats_ = app_.renderer().stats();
    stats_frozen_ = frozen_frames_ > 0;  // those of a frame drawn frozen (see render)
    // The systems of a tick, in their order.
    camera_.begin_update();
    world_.begin_tick();
    const bool frozen = options_.freeze_after_ticks > 0 && ticks_ > options_.freeze_after_ticks;
    if (frozen) {
        // Frozen, the characters hold their pose: no tick, but the previous one becomes the
        // current one, so that every frame drawn from now on is the same, whatever its alpha.
        if (animated_) {
            moteur::advance_animators(world_.registry(), 0);
        }
        return;
    }
    ++live_ticks_;
    update_effects();
    live_pointer_ = app_.input().pointer();
    const auto step = static_cast<float>(dt);
    const glm::vec2 hero_before = hero_ != entt::null ? cell_position(hero_) : glm::vec2(0.0f);
    // The slice's systems in the Profiler window (milestone 7, part 11), around the engine's own zones.
    {
        MOTEUR_PROFILE("tranche : entrées");
        apply_input(step);
    }
    if (options_.hero && !options_.measure.empty()) {
        if (!measure_started_) {
            start_measure();
            measure_started_ = true;
        }
        drive_measure();
    }
#if !defined(MOTEUR_SHIPPING)
    if (moteur::DebugTools* tools = app_.debug_tools(); tools != nullptr && selected_ != reported_selection_) {
        tools->select(selected_);  // a creature picked in the world shows in the inspector
        reported_selection_ = selected_;
    }
#endif
    if (level_mode()) {
        MOTEUR_PROFILE("tranche : niveau");
        level_->set_focus(foot_of(hero_));
        level_->update();
    }
    if (options_.hero) {
        {
            MOTEUR_PROFILE("tranche : chasse");
            hunt(step);
        }
        {
            MOTEUR_PROFILE("tranche : déplacements");
            move_characters(step);
        }
        {
            MOTEUR_PROFILE("tranche : vision");
            update_sight();
        }
        if (options_.freeze_after_ticks > 0) {
            particles_.update(step);  // a reproducible capture: the effects follow the ticks
        }
    } else {
        move_creatures(step);
    }
    if (animated_) {
        MOTEUR_PROFILE("tranche : animation");
        animate_characters(step);  // footsteps on the events
    } else if (hero_ != entt::null) {
        play_steps(hero_before);
    }
    show_selection();
    if (follow_ && selected_ != entt::null) {
        camera_.follow(foot_of(selected_), step, 0.25f);
    }
    // A save at the end of a given tick (the exactness test of milestone 7, part 6).
    if (options_.hero && options_.save_at_tick > 0 && live_ticks_ == options_.save_at_tick && !options_.save_path.empty()) {
        std::string error;
        // Timed in parts (milestone 7, part 10): the state gathered, encoded, written.
        const Uint64 start = SDL_GetPerformanceCounter();
        const moteur::SaveGame game = save();
        const Uint64 gathered = SDL_GetPerformanceCounter();
        const std::vector<std::uint8_t> bytes = game.encode(options_.save_cbor ? moteur::SaveEncoding::Cbor : moteur::SaveEncoding::Json);
        const Uint64 encoded = SDL_GetPerformanceCounter();
        if (moteur::write_file_atomic(options_.save_path, bytes.data(), bytes.size(), error)) {
            const Uint64 written = SDL_GetPerformanceCounter();
            const auto ms = [](Uint64 a, Uint64 b) {
                return static_cast<double>(b - a) * 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency());
            };
            SDL_Log("Save: slice saved after tick %ld into '%s' (%zu bytes, %s) in %.2f ms: state %.2f, encoding %.2f, writing %.2f",
                    live_ticks_, options_.save_path.c_str(), bytes.size(), options_.save_cbor ? "CBOR" : "JSON",
                    ms(start, written), ms(start, gathered), ms(gathered, encoded), ms(encoded, written));
        } else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Save: %s", error.c_str());
        }
    }
}

void Demo3D::render(moteur::Renderer& renderer, double alpha) {
    ++frames_;
    const bool frozen = options_.freeze_after_ticks > 0 && ticks_ > options_.freeze_after_ticks;
    frozen_frames_ += frozen ? 1 : 0;
    // The statistics line shows the previous frame's: wait until it is a frozen one too, or the
    // capture would depend on whether the last frame before the freeze came just before it.
    if (!options_.capture_path.empty() && !capture_done_ && frozen && stats_frozen_) {
        renderer.request_capture(options_.capture_path);
        capture_done_ = true;
    }
    renderer.set_clear_color(0.05f, 0.06f, 0.08f);
    camera_.set_viewport({static_cast<float>(renderer.width()), static_cast<float>(renderer.height())});
    const moteur::Camera3D camera = camera_.interpolated(alpha);
    drawn_camera_ = camera;
    const auto blend = static_cast<float>(alpha);

    moteur::MeshRenderer& meshes = renderer.meshes();
    moteur::BillboardRenderer& billboards = renderer.billboards();
    meshes.set_camera(camera.view_projection(), camera.position());
    meshes.set_time(static_cast<double>(live_ticks_) / moteur::tick_rate());  // the materials' animations
    // Who hears: the camera's target (milestone 9 bis, part 7; the listener stayed at the origin
    // before, and the sounds of the far street faded as if heard from the gate).
    app_.audio().set_listener(camera);
    if (own_sound_) {
        own_sound_->update(static_cast<float>(1.0 / std::max(1, moteur::tick_rate())));
    }
    billboards.set_camera(camera);
    if (level_mode()) {
        const moteur::LevelLighting& lighting = level_->header().lighting;  // the level's, as the editor shows it
        moteur::apply_lighting(renderer, options_.night ? moteur::night_lighting(lighting) : lighting, &*sky_);
    } else {
        const float yaw = glm::radians(sun_yaw_), elevation = glm::radians(sun_elevation_);
        meshes.set_sun({std::cos(elevation) * std::cos(yaw), std::sin(elevation), std::cos(elevation) * std::sin(yaw)},
                       linear(1.0f, 0.93f, 0.8f), sun_intensity_);
        meshes.set_environment(&*sky_, 1.0f);
    }
    // The slice: the fog of war, and the walls before the hero fade.
    moteur::FogOfWar fog;
    fog.enabled = options_.hero && fog_on_;
    fog.origin = fog_origin_;  // (0, 0) on the map
    meshes.set_fog(fog);
    moteur::Cutout cutout;
    if (options_.hero && hero_ != entt::null) {
        cutout.enabled = true;
        cutout.focus = world_.world_position(hero_, blend) + glm::vec3(0.0f, 0.9f, 0.0f);
    }
    meshes.set_cutout(cutout);

    // The world: decor, creatures, torches (the lights nearest to the middle of the view).
    const Uint64 collect_start = SDL_GetPerformanceCounter();
    moteur::CollectOptions collect;
    collect.light_focus = camera.target();
    collect.max_lights = std::max(torch_lights_, 0);
    collect.view = camera.frustum();  // animated characters out of view: no pose to compute
    world_.submit(renderer, blend, collect);
    collect_seconds_ += static_cast<double>(SDL_GetPerformanceCounter() - collect_start) / static_cast<double>(SDL_GetPerformanceFrequency());

    // The effects (the slice): real time, except when frozen for a capture (see update()). Switched
    // off, every running effect goes (they are visual only, and never saved).
    if (options_.hero && effects_on_ != nullptr && !effects_on_->as_bool()) {
        particles_.clear();
    }
    if (options_.hero) {
        const std::uint64_t now = SDL_GetTicksNS();
        if (options_.freeze_after_ticks <= 0 && last_frame_ns_ != 0) {
            particles_.update(static_cast<float>(now - last_frame_ns_) * 1e-9f);
        }
        last_frame_ns_ = now;
        const moteur::Frustum view = camera.frustum();
        particles_.draw(billboards, app_.assets(), &view);
        if (debug_flags_.any()) {
            moteur::WorldDebugSources sources;
            sources.world = &world_;
            sources.view = &view_;
            sources.highlight = hero_;
            if (level_mode()) {
                sources.scene = &level_->collision();
                sources.navmesh = navmesh();
            } else {
                sources.grid = &grid_;
                sources.clearance = &clearance_;
                sources.field = &field_;
            }
            moteur::draw_world_debug(renderer.debug_lines().lines(), debug_flags_, sources, camera.target(),
                                     camera.visible_height() * 1.1f, blend);
        }
    }

    // The cell under the mouse. Frozen, the pointer of the last live tick: in a replay, the pointer
    // of the frame depends on how many ticks ran before it, and so would the capture.
    if (!options_.fake_mouse) {
        mouse_ = frozen ? live_pointer_ : app_.input().pointer();
    }
    hovered_cell_.reset();
    if (level_mode()) {
        // The creature under the pointer, outlined: the id pass (a frame late; for the eye only).
        renderer.request_pick(mouse_);
        const entt::entity hovered = world_.entity_of_pick(renderer.picked_id());
        const bool creature = hovered != entt::null && world_.registry().all_of<Hunter>(hovered) &&
                              world_.registry().get<Health>(hovered).value > 0.0f;
        renderer.set_outline(creature ? renderer.picked_id() : 0u, {1.0f, 0.35f, 0.25f, 1.0f});
    } else if (mouse_.x >= 0.0f && mouse_.y >= 0.0f) {
        if (const auto ground = camera.ground_point(mouse_)) {
            const glm::ivec2 cell(static_cast<int>(std::floor(ground->x)), static_cast<int>(std::floor(ground->z)));
            if (cell.x >= 0 && cell.y >= 0 && cell.x < options_.map_size && cell.y < options_.map_size) {
                hovered_cell_ = cell;
                meshes.draw(*tile_, place({static_cast<float>(cell.x) + 0.5f, 0.005f, static_cast<float>(cell.y) + 0.5f}, glm::vec3(1.0f)).matrix(),
                            glowing({0.9f, 0.75f, 0.2f}));
            }
        }
    }

    draw_overlay(renderer, camera, blend);
}

void Demo3D::draw_overlay(moteur::Renderer& renderer, const moteur::Camera3D& camera, float blend) {
    moteur::UiModel& model = ui_->model();
    const float scale = ui_->scale();  // pixels per point
    const glm::vec2 view(static_cast<float>(renderer.width()), static_cast<float>(renderer.height()));

    // Health bars over the creatures on screen (the `health` template: one per creature, kept while
    // asked for each frame), above the box.
    if (health_bars_) {
        for (auto [entity, health] : world_.registry().storage<Health>().reach()) {
            if (world_.registry().all_of<moteur::Hidden>(entity)) {
                continue;  // out of the hero's sight (the slice's fog of war)
            }
            if (const auto* effects = world_.registry().try_get<moteur::MaterialEffects>(entity); effects != nullptr && effects->dissolve > 0.0f) {
                continue;  // its remains are going
            }
            const glm::vec3 p = world_.world_position(entity, blend);
            const auto head = camera.world_to_screen({p.x, p.y + top_of(entity), p.z});
            const glm::vec2 margin = glm::vec2(48.0f, 5.0f) * scale;
            if (!head || head->x < -margin.x || head->y < -margin.y || head->x > view.x + margin.x || head->y > view.y + margin.y) {
                continue;
            }
            const auto key = static_cast<std::uint64_t>(entt::to_integral(entity));
            moteur::UiElement* bar = hud_->instance("health", key);
            model.set("health." + std::to_string(key) + ".value", health.value);
            bar->set_class("hurt", health.value < 0.6f);
            bar->set_class("low", health.value < 0.3f);
            bar->set_class("selected", entity == selected_);  // the selected one stands out
            const glm::vec4 rect = bar->rect();
            const glm::vec2 size = rect.z > 0.0f ? glm::vec2(rect.z, rect.w) : glm::vec2(44.0f, 5.0f) * scale;
            bar->set_screen_position(glm::round(*head - glm::vec2(size.x * 0.5f, size.y)));
        }
    }

    // Skills just used: their words over the creature, rising and fading; the newest lowest, the
    // older ones stacked above it (the `flash` template).
    const moteur::Localization& words = app_.localization();
    for (std::size_t f = 0; f < flashes_.size(); ++f) {
        const SkillFlash& flash = flashes_[f];
        const auto newer = std::count_if(flashes_.begin() + static_cast<std::ptrdiff_t>(f) + 1, flashes_.end(),
                                         [&flash](const SkillFlash& other) { return other.creature == flash.creature; });
        if (!world_.registry().valid(flash.creature)) {
            continue;
        }
        const glm::vec3 p = world_.world_position(flash.creature, blend);
        const auto head = camera.world_to_screen({p.x, p.y + top_of(flash.creature) + 0.3f, p.z});
        if (!head) {
            continue;
        }
        // One label per flash: its creature, its end and its skill make it.
        const std::uint64_t key = (static_cast<std::uint64_t>(entt::to_integral(flash.creature)) << 32) ^
                                  (static_cast<std::uint64_t>(flash.until_tick) << 4) ^ static_cast<std::uint64_t>(flash.skill);
        moteur::UiElement* label = hud_->instance("flash", key);
        model.set("flash." + std::to_string(key) + ".text",
                  flash.skill == 0 ? words.format("flash-hit") : words.format("flash-skill", {{"number", flash.skill}}));
        const float left = static_cast<float>(flash.until_tick - live_ticks_) / 45.0f;  // 1 when used, 0 at the end
        char opacity[32];
        std::snprintf(opacity, sizeof(opacity), "opacity: %.2f", std::clamp(left * 1.5f, 0.0f, 1.0f));
        label->set_style(opacity);
        const glm::vec4 rect = label->rect();
        const float line = rect.w > 0.0f ? rect.w : 22.0f * scale;
        const float rise = (1.0f - left) * 30.0f * scale + static_cast<float>(newer) * line;
        label->set_screen_position(glm::round(*head - glm::vec2(100.0f * scale, line + rise)));
    }

    // Statistics and help, top left.
    const auto count = [](std::size_t n) { return static_cast<std::int64_t>(n); };
    model.set("hud.world", words.format("hud-world", {{"width", options_.map_size}, {"height", options_.map_size},
                                                      {"creatures", count(creatures_)}, {"objects", count(static_draws_)},
                                                      {"torches", count(torches_)}}));
    const moteur::RenderStats& s = last_stats_;
    model.set("hud.scene", words.format("hud-scene", {{"meshes", s.meshes}, {"meshes_submitted", s.meshes_submitted},
                                                      {"triangles", count(s.triangles)}, {"draws", s.mesh_draw_calls},
                                                      {"casters", s.shadow_casters}, {"casters_submitted", s.shadow_casters_submitted},
                                                      {"shadow_draws", s.shadow_draw_calls}}));
    model.set("hud.lights", words.format("hud-lights", {{"shadowed", s.point_shadow_lights}, {"updated", s.point_shadow_updates},
                                                        {"light_draws", s.point_shadow_draw_calls}, {"billboards", s.billboards},
                                                        {"draws", s.draw_calls}}));
    // The controls of the current profile, named as on the player's keyboard.
    // Only the device in use: the keys, or the gamepad's buttons.
    const moteur::Input& input = app_.input();
    const moteur::InputDevice device = input.last_device();
    const bool click_to_move = !input.sources(actions_.move_to).empty() && device == moteur::InputDevice::KeyboardMouse;
    std::string skills;
    for (int i = 0; i < kSkills; ++i) {
        if (!input.sources(actions_.skills[i]).empty()) {
            skills += (skills.empty() ? "" : ", ") + input.describe(actions_.skills[i], device);
        }
    }
    model.set("hud.move", words.format("hud-move", {{"layout", profile_label(app_)},
                                                    {"keys", input.describe(click_to_move ? actions_.move_to : actions_.move, device)},
                                                    {"skills", skills}}));
    const std::string following = words.format(follow_ ? "common-yes" : "common-no");
    std::string text;
    if (hero_ != entt::null) {
        text = words.format("hud-hero", {{"move", input.describe(actions_.move_to, device)},
                                         {"strike", input.describe(actions_.skills[0], device)}, {"hits", hits_},
                                         {"follow", input.describe(actions_.follow, device)}, {"following", following},
                                         {"pause", input.describe(actions_.pause, device)}});
    } else {
        const std::string cell = hovered_cell_ ? words.format("hud-cell", {{"x", hovered_cell_->x}, {"y", hovered_cell_->y}}) + " "
                                               : std::string();
        text = words.format("hud-observer", {{"cell", cell}, {"select", input.describe(actions_.select, device)},
                                             {"next", input.describe(actions_.next, device)},
                                             {"follow", input.describe(actions_.follow, device)}, {"following", following}});
    }
    model.set("hud.actions", text);

    // On its own (the sandbox's demo), the overlay is drawn here; in the game, with its menus.
    if (own_ui_) {
        float density = app_.to_pixels({1.0f, 0.0f}).x - app_.to_pixels({0.0f, 0.0f}).x;
        own_ui_->set_scale(density > 0.0f ? density : 1.0f);
        own_ui_->render(renderer, ui_seconds_);
        ui_seconds_ = 0.0;
    }
}

#if !defined(MOTEUR_SHIPPING)
void Demo3D::draw_controls() {
    moteur::Renderer& renderer = app_.renderer();
    moteur::MeshRenderer& meshes = renderer.meshes();
    ImGui::PushItemWidth(200.0f);
    ImGui::SeparatorText("Commandes");
    moteur::Input& input = app_.input();
    if (ImGui::BeginCombo("Profil", input.profile_label(input.profile()).c_str())) {
        for (const std::string& profile : input.profiles()) {
            if (ImGui::Selectable(input.profile_label(profile).c_str(), profile == input.profile())) {
                input.set_profile(profile);
                // Kept for the next time, in the player's file.
                const std::string path = user_bindings_path();
                const std::string text = input.user_bindings_json();
                if (!path.empty() && !SDL_SaveFile(path.c_str(), text.data(), text.size())) {
                    SDL_Log("Demo 3D: cannot write '%s': %s", path.c_str(), SDL_GetError());
                }
            }
        }
        ImGui::EndCombo();
    }
    ImGui::Text("Dernier périphérique : %s ; manettes : %d",
                input.last_device() == moteur::InputDevice::Gamepad ? "manette" : "clavier et souris", input.gamepad_count());
    ImGui::TextDisabled("Actions et touches : DEBUG > Entrées");
    ImGui::Text("Compétences utilisées : %d %d %d %d %d %d", skill_uses_[0], skill_uses_[1], skill_uses_[2],
                skill_uses_[3], skill_uses_[4], skill_uses_[5]);
    if (options_.hero) {
        ImGui::SeparatorText("Monde (jalon 6)");
        ImGui::Checkbox("Brouillard de guerre", &fog_on_);
        const moteur::ParticleStats effects = particles_.stats();
        ImGui::Text("Exploré : %d cases ; coups reçus : %d ; %d effets, %d particules", explored_.count(), blows_taken_,
                    effects.effects, effects.particles);
    }
    ImGui::SeparatorText("Caméra");
    ImGui::Checkbox("Suivre la créature choisie", &follow_);
    if (selected_ != entt::null) {
        ImGui::Text("Créature choisie : %s", world_.registry().get<moteur::Name>(selected_).value.c_str());
    }
    ImGui::SeparatorText("Lumière");
    ImGui::SliderFloat("Soleil", &sun_intensity_, 0.0f, 10.0f, "%.1f");
    ImGui::SliderFloat("Hauteur du soleil (°)", &sun_elevation_, 5.0f, 90.0f, "%.0f");
    ImGui::SliderFloat("Direction du soleil (°)", &sun_yaw_, 0.0f, 360.0f, "%.0f");
    moteur::ShadowOptions shadows = meshes.shadows();
    bool changed = ImGui::Checkbox("Ombres du soleil", &shadows.enabled);
    ImGui::SliderInt("Torches éclairantes", &torch_lights_, 0, moteur::MeshRenderer::kMaxPointLights);
    if (ImGui::Checkbox("Ombres des torches", &torch_shadows_)) {
        for (auto [entity, light] : world_.registry().view<moteur::LightSource>().each()) {
            light.casts_shadows = torch_shadows_;
        }
    }
    changed |= ImGui::SliderInt("Torches ombrées (budget)", &shadows.point_budget, 0, moteur::MeshRenderer::kMaxShadowedPointLights);
    if (changed) {
        meshes.set_shadows(shadows);
    }
    ImGui::SeparatorText("Rendu");
    float scale = renderer.render_scale();
    if (ImGui::SliderFloat("Résolution de rendu", &scale, 0.25f, 1.0f, "%.2f")) {
        renderer.set_render_scale(scale);
    }
    anti_aliasing_combo(renderer);
    bool culling = meshes.culling();
    if (ImGui::Checkbox("Frustum culling", &culling)) {
        meshes.set_culling(culling);
    }
    ImGui::Checkbox("Barres de vie", &health_bars_);
    ImGui::PopItemWidth();
    const moteur::RenderStats& s = last_stats_;
    if (ImGui::BeginTable("passes", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Passe");
        ImGui::TableSetupColumn("Soumis");
        ImGui::TableSetupColumn("Dessinés");
        ImGui::TableSetupColumn("Triangles");
        ImGui::TableSetupColumn("Draw calls");
        ImGui::TableHeadersRow();
        const auto row = [](const char* name, int submitted, int drawn, std::size_t triangles, int calls) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(name);
            ImGui::TableNextColumn();
            ImGui::Text("%d", submitted);
            ImGui::TableNextColumn();
            ImGui::Text("%d", drawn);
            ImGui::TableNextColumn();
            ImGui::Text("%zu", triangles);
            ImGui::TableNextColumn();
            ImGui::Text("%d", calls);
        };
        row("Scène", s.meshes_submitted, s.meshes, s.triangles, s.mesh_draw_calls);
        row("Ombre", s.shadow_casters_submitted, s.shadow_casters, s.shadow_triangles, s.shadow_draw_calls);
        row("Torches", s.point_shadow_lights, s.point_shadow_casters, s.point_shadow_triangles, s.point_shadow_draw_calls);
        row("Billboards", s.billboards, s.billboards, static_cast<std::size_t>(s.billboards) * 2, s.billboard_draw_calls);
        ImGui::EndTable();
    }
    ImGui::Text("Draw calls en tout : %d (sprites et interface compris)", s.draw_calls);
    ImGui::Text("Monde : %zu entités, dont %zu fixes gardées ; collecte : %.3f ms par image (moyenne)", world_.entity_count(),
                world_.cached(), collect_ms());
}
#endif

// --- Milestone 6: the world of the slice ---------------------------------------------------------

void Demo3D::play_effect(const char* path, glm::vec3 at, float scale) {
    if (effects_on_ != nullptr && !effects_on_->as_bool()) {
        return;  // visual only: the game is the same without
    }
    auto found = effects_.find(path);
    if (found == effects_.end()) {
        if (!app_.assets().exists(path)) {
            return;
        }
        found = effects_.emplace(path, app_.assets().particle_effect(path)).first;
    }
    particles_.play(found->second, at, scale);
}

void Demo3D::steer_hero(const std::optional<glm::vec3>& ground) {
    const moteur::Input& input = app_.input();
    entt::registry& registry = world_.registry();
    moteur::Mover& hero = registry.get<moteur::Mover>(hero_);
    const glm::vec2 at = cell_position(hero_);
    if (level_mode()) {
        level_steer(hero);
    } else if (ground && input.pressed(actions_.move_to)) {
        const entt::entity target = creature_near(*ground, 0.8f);
        if (target != entt::null && registry.get<Health>(target).value > 0.0f) {
            // On a creature: strikes it within reach, or walks up to it.
            pressed_on_creature_ = true;
            if (in_reach(target)) {
                attack(target);
            } else {
                hero.go_to(cell_position(target));
            }
        } else {
            pressed_on_ground_ = true;
            hero.go_to({ground->x, ground->z});
            follow_repath_ = 10;
        }
    } else if (ground && pressed_on_ground_ && input.down(actions_.move_to)) {
        // Held: towards the pointer, straight while the way is clear, else by a new path from time to time.
        const glm::vec2 goal(ground->x, ground->z);
        if (glm::length(goal - at) > 0.2f && moteur::segment_clear(grid_, at, goal, kCharacterRadius)) {
            hero.mode = moteur::MoveMode::ToPoint;
            hero.goal = glm::vec3(goal.x, 0.0f, goal.y);
            hero.path = {hero.goal};
            hero.next_point = 0;
            hero.needs_path = false;
        } else if (--follow_repath_ <= 0 && glm::length(goal - glm::vec2(hero.goal.x, hero.goal.z)) > 0.5f) {
            hero.go_to(goal);
            follow_repath_ = 10;
        }
    }
    if (!input.down(actions_.move_to)) {
        pressed_on_creature_ = false;
        pressed_on_ground_ = false;
    }
    // Keys or stick, relative to the screen: straight, sliding on the walls.
    if (const glm::vec2 direction = input.axis(actions_.move); direction != glm::vec2(0.0f)) {
        glm::vec3 ahead, right;
        screen_axes(ahead, right);
        const glm::vec3 d = right * direction.x + ahead * direction.y;
        hero.move({d.x, d.z});
    } else if (hero.mode == moteur::MoveMode::Direct) {
        hero.stop();
    }
}

void Demo3D::creature_blow(entt::entity creature) {
    entt::registry& registry = world_.registry();
    const Hunter* hunter = registry.try_get<Hunter>(creature);
    if (hunter == nullptr || !hunter->chasing || registry.get<Health>(creature).value <= 0.0f) {
        return;
    }
    const glm::vec2 hero = cell_position(hero_);
    const CharacterData& striker = *registry.get<Archetype>(creature).data;
    const CharacterData& target = *registry.get<Archetype>(hero_).data;
    if (glm::length(hero - cell_position(creature)) > target.radius + striker.radius + striker.reach + 0.4f) {
        return;  // the hero stepped out of the swing
    }
    if (const moteur::Animator* animator = registry.try_get<moteur::Animator>(hero_); animator != nullptr && animator->in_window("esquive")) {
        return;  // dashing: the blow passes (an event window of the clip, milestone 9 bis, part 3)
    }
    Health& health = registry.get<Health>(hero_);
    const float blow = static_cast<float>(striker.damage) / static_cast<float>(target.life);
    if (!god_) {
        health.value = std::max(health.value - blow, 0.05f);  // the slice does not kill its hero
    }
    ++blows_taken_;
    const glm::vec3 hero_foot = foot_of(hero_);
    if (!target.hit_effect.empty()) {
        play_effect(target.hit_effect.c_str(), hero_foot + glm::vec3(0.0f, 1.1f, 0.0f), 0.8f);  // the hero's row: blood
    }
    if (sound_ != nullptr) {
        sound_->play("combat/coup_recu", {.position = hero_foot + glm::vec3(0.0f, 1.0f, 0.0f)});
    }
}

void Demo3D::hunt(float dt) {
    // The creatures, in the order of their identifiers: they wander, turn back when blocked, chase
    // the hero once they see it (and give up four seconds after losing it), and strike it in reach.
    entt::registry& registry = world_.registry();
    const glm::vec2 hero = cell_position(hero_);
    Health& hero_health = registry.get<Health>(hero_);
    hero_health.value = std::min(hero_health.value + 0.0005f, 1.0f);  // heals slowly
    // The rows are read again every tick: a table edited while the game runs changes everyone.
    const CharacterData& hero_data = *registry.get<Archetype>(hero_).data;
    registry.get<moteur::Mover>(hero_).speed = hero_data.speed;
    registry.get<moteur::Collider>(hero_).radius = hero_data.radius;
    registry.get<moteur::Collider>(hero_).push_weight = hero_data.weight;
    std::vector<entt::entity> hunters(registry.view<Hunter>().begin(), registry.view<Hunter>().end());
    std::sort(hunters.begin(), hunters.end(), [](entt::entity a, entt::entity b) { return entt::to_integral(a) < entt::to_integral(b); });
    for (const entt::entity entity : hunters) {
        if (registry.get<Health>(entity).value <= 0.0f) {
            continue;  // down
        }
        Hunter& hunter = registry.get<Hunter>(entity);
        moteur::Mover& mover = registry.get<moteur::Mover>(entity);
        moteur::Collider& collider = registry.get<moteur::Collider>(entity);
        const Archetype& archetype = registry.get<Archetype>(entity);
        const CharacterData& data = *archetype.data;
        mover.speed = data.speed * archetype.pace;
        collider.radius = data.radius;
        const glm::vec2 p = cell_position(entity);
        const float distance = glm::length(hero - p);
        const bool sees = distance <= data.sight &&
                          (level_mode() ? moteur::line_of_sight(level_->collision(), foot_of(entity) + glm::vec3(0.0f, kEye, 0.0f),
                                                                foot_of(hero_) + glm::vec3(0.0f, kEye, 0.0f))
                                        : moteur::line_of_sight(grid_, p, hero));
        if (sees) {
            hunter.chasing = true;
            hunter.lost_ticks = 0;
        } else if (hunter.chasing && ++hunter.lost_ticks > 240) {
            hunter.chasing = false;
        }
        const float reach = hero_data.radius + data.radius + data.reach;
        if (!hunter.chasing) {
            collider.push_weight = data.weight;
            if (mover.state == moteur::MoveState::Blocked) {
                hunter.wander = -hunter.wander;  // turns back, as in the 2D demo
            }
            if (mover.mode != moteur::MoveMode::Direct || mover.state == moteur::MoveState::Blocked) {
                mover.move(hunter.wander);
            }
            continue;
        }
        if (mover.mode != moteur::MoveMode::FollowField) {
            mover.follow_field(reach - 0.05f);
        }
        // In reach (on the same floor): it stands its ground (immovable) and strikes every second and a half.
        const bool striking = distance <= reach + 0.1f && std::fabs(foot_of(entity).y - foot_of(hero_).y) < 0.6f;
        collider.push_weight = striking ? 0 : data.weight;
        if (!striking) {
            hunter.cooldown = std::min(hunter.cooldown, 30);
            continue;
        }
        if (--hunter.cooldown > 0) {
            continue;
        }
        hunter.cooldown = std::max(1, static_cast<int>(std::lround(data.cadence / dt)));  // seconds of the row, in ticks
        mover.facing = (hero - p) / std::max(distance, 1e-4f);
        if (moteur::Animator* animator = registry.try_get<moteur::Animator>(entity)) {
            animator->fire("attaque");  // the blow lands at its "impact" event
        } else {
            creature_blow(entity);
        }
    }
}

void Demo3D::move_characters(float dt) {
    if (level_mode()) {
        level_moves(dt);
        return;
    }
    // The flow field towards the hero, again when the hero changes cell.
    const glm::vec2 hero = cell_position(hero_);
    if (moteur::cell_at(hero) != field_cell_) {
        field_cell_ = moteur::cell_at(hero);
        field_target_ = hero;
        field_.compute(grid_, clearance_, hero, kCharacterRadius, 300);
    }
    entt::registry& registry = world_.registry();
    moteur::MovementStats stats;
    moteur::plan_paths(registry, grid_, clearance_, 8, stats);
    moteur::move_movers(registry, grid_, dt, &field_, stats);
    moteur::separate_colliders(registry, grid_, hash_);
    moteur::finish_movers(registry, dt);
}

void Demo3D::update_sight() {
    if (level_mode()) {
        level_sight();
        return;
    }
    entt::registry& registry = world_.registry();
    const glm::ivec2 cell = moteur::cell_at(cell_position(hero_));
    if (cell != view_cell_) {
        view_cell_ = cell;
        // What was in sight becomes explored; then what is in sight now.
        for (const glm::ivec2 c : view_.cells()) {
            if (grid_.contains(c)) {
                fog_cells_[static_cast<std::size_t>(c.y * grid_.width() + c.x)] = 128;
            }
        }
        view_.compute(grid_, cell, kHeroSight);
        explored_.add(view_);
        for (const glm::ivec2 c : view_.cells()) {
            if (grid_.contains(c)) {
                fog_cells_[static_cast<std::size_t>(c.y * grid_.width() + c.x)] = 255;
            }
        }
        app_.renderer().meshes().set_fog_cells(grid_.width(), grid_.height(), fog_cells_);
    }
    // The creatures out of sight are hidden, with their bars.
    for (const entt::entity entity : registry.view<Hunter>()) {
        const bool seen = !fog_on_ || view_.visible(moteur::cell_at(cell_position(entity)));
        if (seen) {
            registry.remove<moteur::Hidden>(entity);
        } else if (!registry.all_of<moteur::Hidden>(entity)) {
            registry.emplace<moteur::Hidden>(entity);
        }
    }
}

// --- Milestone 9, part 11: the slice in a level of version 3 --------------------------------------


void Demo3D::open_level() {
    // A closed zone: loaded at once behind the loading screen, in the order of its chunks (the same
    // entities in the same order on every run: replays, saves, the exactness test).
    moteur::StreamingSettings settings;
    settings.load_all = true;
    settings.ordered = true;
    level_ = std::make_unique<moteur::LevelStreamer>(app_.assets(), world_, settings);
    entt::registry& registry = world_.registry();
    level_->set_instance_hook([this, &registry](entt::entity root, const moteur::ChunkInstance& instance) {
        // The game's data of the instance: who comes there (made once the level is there, by their
        // identifiers), where fire burns.
        if (instance.properties.contains("personnage")) {
            level_spawns_.push_back({instance.id, instance.transform.position, instance.properties});
        }
        if (instance.properties.value("feu", false)) {
            braziers_.push_back(instance.transform.position + glm::vec3(0.0f, 1.0f, 0.0f));  // over the bowl
        }
        // The houses' faces fade before the hero (Cutout); the glass and flames of the lamps, where
        // their light sits, cast no shadow (or the light would light nothing). The materials are the
        // models', shared: set on each instance, the same values.
        const bool facade = instance.prefab.find("/facades_gothiques/") != std::string::npos;
        for (auto [entity, model] : registry.view<moteur::ModelComponent>().each()) {
            if (!model.model) {
                continue;
            }
            const moteur::Parent* parent = registry.try_get<moteur::Parent>(entity);
            if (entity != root && (parent == nullptr || parent->entity != root)) {
                continue;
            }
            for (moteur::Material& material : model.model->materials) {
                material.fades = material.fades || facade;
                if (material.emissive != glm::vec3(0.0f)) {
                    material.casts_shadow = false;
                }
            }
            static_draws_ += model.model->parts.size();
        }
    });
    if (options_.baked_level) {
        level_->open(std::move(*options_.baked_level), options_.level);  // the editor's, as it is
        options_.baked_level.reset();
    } else {
        level_->open_any(options_.level);
    }
    const moteur::LevelPoint* start = level_->header().point(moteur::kStartPoint);
    level_->set_focus(start != nullptr ? start->position : level_->header().bounds.center());
    if (!level_->finish(120.0)) {
        throw std::runtime_error("Tranche : le niveau « " + options_.level + " » ne s'est pas chargé");
    }
    std::sort(level_spawns_.begin(), level_spawns_.end(), [](const LevelSpawn& a, const LevelSpawn& b) { return a.id < b.id; });
    if (sound_ != nullptr) {
        // The places' reverberation: the street's stone, the parvis before the cathedral (its zone
        // starts at the stairs); and what the level's walls hide is muffled.
        sound_->set_reverb_zones({{"rue", moteur::Aabb{{-6.5f, -1.0f, -2.0f}, {6.5f, 12.0f, 48.0f}}, 3.0f},
                                  {"parvis", moteur::Aabb{{-12.5f, -1.0f, 57.0f}, {12.5f, 20.0f, 78.0f}}, 3.0f}});
        sound_->set_occlusion_test([this](glm::vec3 listener, glm::vec3 source) {
            const glm::vec3 ear = listener + glm::vec3(0.0f, 1.6f, 0.0f);
            return level_ && !level_->collision().segment_clear(ear, source) ? 1.0f : 0.0f;
        });
    }
    if (const moteur::LevelPoint* haunt = level_->header().point("fantome"); haunt != nullptr && app_.assets().exists(kGhost)) {
        ghost_center_ = haunt->position;
        ghost_ = registry.create();
        registry.emplace<moteur::Transform>(ghost_, place(ghost_center_, glm::vec3(1.0f)));
        registry.emplace<moteur::ModelComponent>(ghost_, moteur::ModelComponent{app_.assets().model(kGhost)});
        registry.emplace<moteur::Name>(ghost_, "fantôme");
    }

    // The fog of war over the whole level, a cell per metre; the floors' grids made now (the first
    // walk up the stairs does not wait for them).
    const moteur::Aabb& bounds = level_->header().bounds;
    fog_origin_ = glm::floor(glm::vec2(bounds.min.x, bounds.min.z));
    const int width = static_cast<int>(std::ceil(bounds.max.x - fog_origin_.x)) + 1;
    const int height = static_cast<int>(std::ceil(bounds.max.z - fog_origin_.y)) + 1;
    options_.map_size = std::max(width, height);
    for (int floor = 0; floor <= 2; ++floor) {
        floor_grid(floor);
    }
    grid_ = floor_grid(0);
    explored_ = moteur::ExploredMap(width, height);
    fog_cells_.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    app_.renderer().meshes().set_fog_cells(width, height, fog_cells_);
    SDL_Log("Slice: level %s, %d instances, %zu spawns, %d collision triangles", options_.level.c_str(),
            level_->stats().instances, level_spawns_.size(), level_->stats().collision_triangles);
}

glm::vec3 Demo3D::hero_position() const {
    return hero_ != entt::null ? foot_of(hero_) : glm::vec3(0.0f);
}

bool Demo3D::hero_grounded() const {
    const moteur::Mover* mover = hero_ != entt::null ? world_.registry().try_get<moteur::Mover>(hero_) : nullptr;
    return mover != nullptr && mover->grounded;
}

glm::vec3 Demo3D::foot_of(entt::entity entity) const {
    return world_.registry().get<moteur::Transform>(entity).position;
}

const moteur::NavMesh* Demo3D::navmesh() const {
    if (!level_mode() || level_->meshes().empty()) {
        return nullptr;
    }
    return level_->meshes().front();
}

std::optional<glm::vec3> Demo3D::free_place(glm::vec3 wanted) const {
    if (const moteur::NavMesh* mesh = navmesh()) {
        return mesh->nearest_point(wanted, {3.0f, 2.0f, 3.0f});
    }
    const std::optional<glm::vec2> fit = moteur::nearest_fit(grid_, {wanted.x, wanted.z}, kCharacterRadius);
    return fit ? std::optional<glm::vec3>(glm::vec3(fit->x, 0.0f, fit->y)) : std::nullopt;
}

bool Demo3D::fits(glm::vec3 at) const {
    if (const moteur::NavMesh* mesh = navmesh()) {
        const std::optional<glm::vec3> on = mesh->nearest_point(at, {0.1f, 1.0f, 0.1f});
        return on.has_value();
    }
    return moteur::circle_fits(grid_, {at.x, at.z}, kCharacterRadius);
}

const moteur::NavGrid& Demo3D::floor_grid(int floor) {
    auto found = floor_grids_.find(floor);
    if (found == floor_grids_.end()) {
        const moteur::NavMesh* mesh = navmesh();
        const int width = static_cast<int>(std::ceil(level_->header().bounds.max.x - fog_origin_.x)) + 1;
        const int height = static_cast<int>(std::ceil(level_->header().bounds.max.z - fog_origin_.y)) + 1;
        moteur::NavGrid grid = mesh != nullptr ? moteur::floor_sight_grid(*mesh, level_->collision(), fog_origin_, width, height,
                                                                          static_cast<float>(floor) * 0.5f)
                                               : moteur::NavGrid(width, height);
        found = floor_grids_.emplace(floor, std::move(grid)).first;
    }
    return found->second;
}

void Demo3D::level_moves(float dt) {
    // The flow field towards the hero, again when the hero moved a metre.
    const glm::vec3 hero = foot_of(hero_);
    if (!nav_field_set_ || glm::length(hero - nav_field_target_) > 1.0f) {
        if (const moteur::NavMesh* mesh = navmesh()) {
            nav_field_.compute(*mesh, hero, 40.0f);
        }
        nav_field_target_ = hero;
        nav_field_set_ = true;
    }
    entt::registry& registry = world_.registry();
    moteur::MovementStats stats;
    moteur::plan_paths(registry, level_->meshes(), 8, stats);
    const moteur::NavFlowField* fields[1] = {&nav_field_};
    moteur::NavSources nav;
    nav.meshes = level_->meshes();
    nav.fields = fields;
    nav.neighbours = &hash_;
    moteur::move_movers(registry, level_->collision(), dt, nav, stats);
    moteur::separate_colliders(registry, level_->collision(), hash_);
    moteur::finish_movers(registry, dt);
}

void Demo3D::level_sight() {
    entt::registry& registry = world_.registry();
    const glm::vec3 hero = foot_of(hero_);
    const glm::ivec2 cell = moteur::cell_at(glm::vec2(hero.x, hero.z) - fog_origin_);
    const int floor = floor_of(hero.y);
    if (cell != view_cell_ || floor != view_floor_) {
        view_cell_ = cell;
        if (floor != view_floor_) {
            grid_ = floor_grid(floor);  // the field of view of the hero's floor
            view_floor_ = floor;
        }
        const auto index = [this](glm::ivec2 c) {
            return static_cast<std::size_t>(c.y) * static_cast<std::size_t>(grid_.width()) + static_cast<std::size_t>(c.x);
        };
        for (const glm::ivec2 c : view_.cells()) {
            if (grid_.contains(c)) {
                fog_cells_[index(c)] = 128;
            }
        }
        view_.compute(grid_, cell, kHeroSight);
        explored_.add(view_);
        for (const glm::ivec2 c : view_.cells()) {
            if (grid_.contains(c)) {
                fog_cells_[index(c)] = 255;
            }
        }
        app_.renderer().meshes().set_fog_cells(grid_.width(), grid_.height(), fog_cells_);
    }
    for (const entt::entity entity : registry.view<Hunter>()) {
        const glm::vec3 p = foot_of(entity);
        const bool seen = !fog_on_ || view_.visible(moteur::cell_at(glm::vec2(p.x, p.z) - fog_origin_));
        if (seen) {
            registry.remove<moteur::Hidden>(entity);
        } else if (!registry.all_of<moteur::Hidden>(entity)) {
            registry.emplace<moteur::Hidden>(entity);
        }
    }
}

void Demo3D::level_steer(moteur::Mover& hero) {
    // What the pointer is over (pointed_, from this tick's camera): a creature by its shape, else the
    // ground of the level, on the navigation mesh.
    const moteur::Input& input = app_.input();
    entt::registry& registry = world_.registry();
    const glm::vec3 at = foot_of(hero_);
    if (pointed_ && input.pressed(actions_.move_to)) {
        const entt::entity target = pointed_->entity;
        if (target != entt::null && registry.all_of<Hunter>(target) && registry.get<Health>(target).value > 0.0f) {
            pressed_on_creature_ = true;
            if (in_reach(target)) {
                attack(target);
            } else {
                hero.go_to(foot_of(target));
            }
        } else if (target == entt::null) {
            pressed_on_ground_ = true;
            hero.go_to(pointed_->point);
            follow_repath_ = 10;
        }
    } else if (pointed_ && pointed_->entity == entt::null && pressed_on_ground_ && input.down(actions_.move_to)) {
        // Held: straight to the pointer while the mesh is clear, else a new path from time to time.
        const glm::vec3 goal = pointed_->point;
        const moteur::NavMesh* mesh = navmesh();
        if (glm::length(goal - at) > 0.2f && mesh != nullptr && mesh->walkable_line(at, goal)) {
            hero.mode = moteur::MoveMode::ToPoint;
            hero.goal = goal;
            hero.path = {goal};
            hero.next_point = 0;
            hero.needs_path = false;
        } else if (--follow_repath_ <= 0 && glm::length(goal - hero.goal) > 0.5f) {
            hero.go_to(goal);
            follow_repath_ = 10;
        }
    }
}

// --- Milestone 7, part 6: saves of the slice --------------------------------------------------

void Demo3D::setup_serializers() {
    // Everything by the descriptions: the engine's components, then the slice's (registered once).
    // The row of the characters' table is saved by identifier and bound to the table when read (the
    // save survives new rows in the table).
    register_slice_components();
    serializers_.set_tables(&app_.data());
    serializers_.add_engine_components();
    serializers_.add_registered();
}

entt::entity Demo3D::spawn_from_record(entt::registry& registry, const nlohmann::json& record) {
    // The character as its row of the table describes it (model, size, Animator); the save then
    // gives every component its value.
    const nlohmann::json& c = record.at("c");
    if (!c.contains("archetype")) {
        return entt::null;
    }
    const moteur::DataRef<CharacterData> row = app_.data().get<CharacterData>().ref(archetype_id(c["archetype"]));
    if (!row) {
        return entt::null;
    }
    const bool hero = c.contains("heros");
    glm::vec3 at(0.0f);
    if (const auto transform = c.find("transform"); transform != c.end()) {
        at = moteur::vec3_from_json(transform->contains("position") ? transform->at("position") : transform->at("p"));
    }
    const moteur::Transform foot = place(at, glm::vec3(1.0f));
    entt::entity entity = entt::null;
    if (animated_) {
        entity = spawn_animated(row->model.c_str(), foot, row->height, hero ? kKnightHidden : std::vector<std::string>{});
        if (hero) {
            const entt::entity sword = registry.create();
            registry.emplace<moteur::Name>(sword, "épée (right_hand)");
            registry.emplace<moteur::Transform>(sword);
            registry.emplace<moteur::Parent>(sword, entity);
            registry.emplace<moteur::BoneAttachment>(sword, "right_hand");
            registry.emplace<moteur::ModelComponent>(sword, app_.assets().model(kSword), std::vector<std::uint32_t>{});
        }
    } else {
        entity = registry.create();
        registry.emplace<moteur::Transform>(entity, foot);
        registry.emplace<moteur::PreviousTransform>(entity, foot);
        const auto part = [&](const moteur::Asset<moteur::Mesh>& mesh, const moteur::Transform& transform, const moteur::Material& material) {
            const entt::entity child = registry.create();
            registry.emplace<moteur::Transform>(child, transform);
            registry.emplace<moteur::Parent>(child, entity);
            registry.emplace<moteur::MeshComponent>(child, mesh, material);
        };
        if (hero) {
            part(cube_, place({0.0f, 0.65f, 0.0f}, {0.5f, 1.3f, 0.5f}), surface(linear(0.25f, 0.4f, 0.8f), 0.35f, 0.8f));
            part(sphere_, place({0.0f, 1.55f, 0.0f}, glm::vec3(0.42f)), surface(linear(0.85f, 0.7f, 0.55f), 0.6f));
        } else {
            part(cube_, place({0.0f, 0.5f, 0.0f}, {0.45f, 1.0f, 0.45f}), surface(linear(0.6f, 0.55f, 0.5f), 0.7f));
            part(sphere_, place({0.0f, 1.2f, 0.0f}, glm::vec3(0.38f)), surface(linear(0.85f, 0.7f, 0.55f), 0.6f));
        }
    }
    if (hero) {
        hero_ = entity;
        selected_ = entity;
    } else {
        ++creatures_;
        if (level_mode()) {
            add_selectable(registry, entity, *row);
        }
    }
    return entity;
}

namespace {

// Runs of explored cells: [start, length, start, length...] over the cells in row order.
nlohmann::json explored_runs(const moteur::ExploredMap& explored) {
    nlohmann::json runs = nlohmann::json::array();
    const std::vector<std::uint8_t>& cells = explored.cells();
    for (std::size_t i = 0; i < cells.size();) {
        if (!cells[i]) {
            ++i;
            continue;
        }
        const std::size_t start = i;
        while (i < cells.size() && cells[i]) {
            ++i;
        }
        runs.push_back(start);
        runs.push_back(i - start);
    }
    return runs;
}

}  // namespace

moteur::SaveGame Demo3D::save() const {
    const entt::registry& registry = world_.registry();
    moteur::SaveGame save;
    save.header.game_version = "bac_a_sable, jalon 7";
    SDL_Time now = 0;
    SDL_GetCurrentTime(&now);
    save.header.time = static_cast<std::int64_t>(now / SDL_NS_PER_SECOND);
    save.header.play_seconds = elapsed_;
    const Health* hero_health = hero_ != entt::null ? registry.try_get<Health>(hero_) : nullptr;
    save.header.summary = {{"lieu", "Tranche jouable"},
                           {"coups", hits_},
                           {"vie", hero_health != nullptr ? hero_health->value : 1.0f},
                           {"exploré", explored_.count()}};
    save.set_section("monde", 1, serializers_.save(registry));
    // A forgotten state shows up here, before it breaks a game.
    for (const std::string& name : serializers_.unknown_components(registry)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Save: component %s of a persistent entity is neither saved nor ignored",
                    name.c_str());
    }

    const moteur::SaveContext context;  // ids read straight from the components below
    nlohmann::json scene;
    scene["seed"] = options_.seed;
    scene["ticks"] = ticks_;
    scene["live_ticks"] = live_ticks_;
    scene["elapsed"] = elapsed_;
    scene["hits"] = hits_;
    scene["blows_taken"] = blows_taken_;
    scene["skill_uses"] = skill_uses_;
    scene["next_id"] = next_persistent_id_;
    const auto id_of = [&registry](entt::entity e) -> std::uint64_t {
        if (e == entt::null || !registry.valid(e)) {
            return 0;
        }
        const moteur::PersistentId* id = registry.try_get<moteur::PersistentId>(e);
        return id != nullptr ? id->value : 0;
    };
    nlohmann::json flashes = nlohmann::json::array();
    for (const SkillFlash& flash : flashes_) {
        flashes.push_back({flash.skill, id_of(flash.creature), flash.until_tick});
    }
    scene["flashes"] = flashes;
    scene["attack_target"] = id_of(attack_target_);
    scene["follow"] = follow_;
    scene["god"] = god_;
    scene["fog"] = fog_on_;
    scene["pressed"] = {pressed_on_creature_, pressed_on_ground_, follow_repath_};
    scene["camera"] = {{"target", moteur::to_json_value(camera_.target())},
                       {"height", camera_.visible_height()},
                       {"projection", camera_.projection() == moteur::Projection::Perspective ? 0 : 1}};
    scene["field"] = {{"cell", {field_cell_.x, field_cell_.y}}, {"target", moteur::to_json_value(field_target_)}};
    scene["explored"] = explored_runs(explored_);
    nlohmann::json grid = nlohmann::json::array();
    if (level_mode()) {
        // A level: what the game changed in its instances, and where the creatures' field was made.
        scene["level"] = {{"key", options_.level},
                          {"state", level_->state().to_json()},
                          {"field", nav_field_set_ ? moteur::to_json_value(nav_field_target_) : nlohmann::json()}};
        scene["grid"] = grid;
        save.set_section("scene", 1, scene);
        return save;
    }
    // The cells of the grid that differ from the map (doors opened, walls broken).
    const moteur::NavGrid original(slice_map_->tiles(), slice_map_->tileset());
    for (int j = 0; j < grid_.height(); ++j) {
        for (int i = 0; i < grid_.width(); ++i) {
            if (grid_.walkable({i, j}) != original.walkable({i, j}) || grid_.opaque({i, j}) != original.opaque({i, j})) {
                grid.push_back({i, j, grid_.walkable({i, j}), grid_.opaque({i, j})});
            }
        }
    }
    scene["grid"] = grid;
    save.set_section("scene", 1, scene);
    return save;
}

void Demo3D::load(const moteur::SaveGame& save) {
    const nlohmann::json* scene = save.section("scene");
    const nlohmann::json* records = save.section("monde");
    if (scene == nullptr || records == nullptr) {
        throw std::runtime_error("Tranche : la sauvegarde n'a pas les sections monde et scene");
    }
    if (scene->at("seed").get<std::uint32_t>() != options_.seed) {
        throw std::runtime_error("Tranche : sauvegarde faite avec la graine " + scene->at("seed").dump() +
                                 " (le décor diffère) ; relancer avec --seed " + scene->at("seed").dump());
    }
    entt::registry& registry = world_.registry();
    std::vector<std::string> problems;
    serializers_.load(registry, *records, [this](entt::registry& r, const nlohmann::json& record) { return spawn_from_record(r, record); },
                      problems);
    for (const std::string& problem : problems) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Save: %s", problem.c_str());
    }
    if (hero_ == entt::null) {
        throw std::runtime_error("Tranche : pas de héros dans la sauvegarde");
    }
    std::unordered_map<std::uint64_t, entt::entity> by_id;
    for (auto [entity, id] : registry.view<moteur::PersistentId>().each()) {
        by_id[id.value] = entity;
    }
    const auto entity_of = [&by_id](std::uint64_t id) {
        const auto found = by_id.find(id);
        return found == by_id.end() ? entt::entity{entt::null} : found->second;
    };
    ticks_ = scene->at("ticks").get<long>();
    live_ticks_ = scene->at("live_ticks").get<long>();
    elapsed_ = scene->at("elapsed").get<double>();
    hits_ = scene->at("hits").get<int>();
    blows_taken_ = scene->at("blows_taken").get<int>();
    const std::vector<int> uses = scene->at("skill_uses").get<std::vector<int>>();
    for (std::size_t i = 0; i < uses.size() && i < std::size(skill_uses_); ++i) {
        skill_uses_[i] = uses[i];
    }
    next_persistent_id_ = scene->at("next_id").get<std::uint64_t>();
    god_ = scene->value("god", false);
    fog_on_ = scene->value("fog", true);
    flashes_.clear();
    for (const nlohmann::json& f : scene->at("flashes")) {
        const entt::entity creature = entity_of(f.at(1).get<std::uint64_t>());
        if (creature != entt::null) {
            flashes_.push_back({f.at(0).get<int>(), creature, f.at(2).get<long>()});
        }
    }
    attack_target_ = entity_of(scene->at("attack_target").get<std::uint64_t>());
    follow_ = scene->at("follow").get<bool>();
    pressed_on_creature_ = scene->at("pressed").at(0).get<bool>();
    pressed_on_ground_ = scene->at("pressed").at(1).get<bool>();
    follow_repath_ = scene->at("pressed").at(2).get<int>();
    const nlohmann::json& camera = scene->at("camera");
    camera_.set_target(moteur::vec3_from_json(camera.at("target")));
    camera_.set_visible_height(camera.at("height").get<float>());
    camera_.set_projection(camera.at("projection").get<int>() == 0 ? moteur::Projection::Perspective : moteur::Projection::Orthographic);
    camera_.begin_update();
    if (level_mode()) {
        const nlohmann::json* level = scene->contains("level") ? &scene->at("level") : nullptr;
        if (level == nullptr || level->at("key").get<std::string>() != options_.level) {
            throw std::runtime_error("Tranche : la sauvegarde n'est pas faite dans le niveau « " + options_.level + " »");
        }
        level_->state() = moteur::LevelState::from_json(level->at("state"));
        nav_field_set_ = !level->at("field").is_null();
        if (nav_field_set_) {
            nav_field_target_ = moteur::vec3_from_json(level->at("field"));
            if (const moteur::NavMesh* mesh = navmesh()) {
                nav_field_.compute(*mesh, nav_field_target_, 40.0f);
            }
        }
    }
    for (const nlohmann::json& cell : scene->at("grid")) {
        grid_.set({cell.at(0).get<int>(), cell.at(1).get<int>()}, cell.at(2).get<bool>(), cell.at(3).get<bool>());
    }
    if (!scene->at("grid").empty()) {
        clearance_ = moteur::ClearanceMap(grid_);
    }
    // What follows from the state is computed again: the flow field (from where the hero was when
    // it was computed), the fog (explored cells, then the field of view).
    const nlohmann::json& field = scene->at("field");
    field_cell_ = {field.at("cell").at(0).get<int>(), field.at("cell").at(1).get<int>()};
    field_target_ = moteur::vec2_from_json(field.at("target"));
    if (field_cell_.x >= 0 && !level_mode()) {
        field_.compute(grid_, clearance_, field_target_, kCharacterRadius, 300);
    }
    const nlohmann::json& runs = scene->at("explored");
    for (std::size_t r = 0; r + 1 < runs.size(); r += 2) {
        const std::size_t start = runs[r].get<std::size_t>();
        const std::size_t length = runs[r + 1].get<std::size_t>();
        for (std::size_t i = start; i < start + length; ++i) {
            const glm::ivec2 cell(static_cast<int>(i) % grid_.width(), static_cast<int>(i) / grid_.width());
            explored_.mark(cell);
            fog_cells_[i] = 128;
        }
    }
    view_cell_ = glm::ivec2(-1);
    view_floor_ = -1000;
    update_sight();
    create_selection_marks();
    SDL_Log("Save: slice loaded (%zu characters, tick %ld)", static_cast<std::size_t>(registry.view<moteur::PersistentId>().size()),
            ticks_);
}

void Demo3D::register_commands() {
    using Kind = moteur::Console::Kind;
    moteur::Console& console = app_.console();
    console.add("god", "[0|1]", "le héros ne perd plus de vie (tranche)", Kind::Game,
                [this](const moteur::Console::Args& args, moteur::Console& out) {
                    god_ = args.empty() ? !god_ : args[0] != "0";
                    out.print(god_ ? "héros invulnérable" : "héros vulnérable");
                });
    console.add("heal", "", "rend toute sa vie au héros (tranche)", Kind::Game,
                [this](const moteur::Console::Args&, moteur::Console& out) {
                    world_.registry().get<Health>(hero_).value = 1.0f;
                    out.print("héros soigné");
                });
    console.add("tp", "<x> <z>", "téléporte le héros (mètres, sur une case marchable proche)", Kind::Game,
                [this](const moteur::Console::Args& args, moteur::Console& out) {
                    if (args.size() != 2) {
                        out.error("tp <x> <z>");
                        return;
                    }
                    const glm::vec3 wanted(static_cast<float>(std::atof(args[0].c_str())), foot_of(hero_).y,
                                           static_cast<float>(std::atof(args[1].c_str())));
                    const std::optional<glm::vec3> fit = free_place(wanted);
                    if (!fit) {
                        out.error("aucune place libre près de " + args[0] + " " + args[1]);
                        return;
                    }
                    entt::registry& registry = world_.registry();
                    moteur::Transform transform = registry.get<moteur::Transform>(hero_);
                    transform.position = level_mode() ? *fit : glm::vec3(fit->x, transform.position.y, fit->z);
                    registry.replace<moteur::Transform>(hero_, transform);
                    registry.replace<moteur::PreviousTransform>(hero_, transform);  // no slide from where it was
                    registry.get<moteur::Mover>(hero_).stop();
                    camera_.set_target({fit->x, transform.position.y, fit->z});
                    out.print("héros en " + std::to_string(fit->x) + " " + std::to_string(fit->z));
                });
    console.add(
        "spawn", "<personnage> [nombre]", "fait apparaître des créatures autour du héros (table personnages)", Kind::Game,
        [this](const moteur::Console::Args& args, moteur::Console& out) {
            if (args.empty()) {
                out.error("spawn <personnage> [nombre]");
                return;
            }
            if (app_.data().get<CharacterData>().find(args[0]) == nullptr || args[0] == "chevalier") {
                out.error("« " + args[0] + " » n'est pas une créature de la table personnages");
                return;
            }
            const int count = args.size() > 1 ? std::clamp(std::atoi(args[1].c_str()), 1, 200) : 1;
            const glm::vec2 hero = cell_position(hero_);
            int made = 0;
            // Around the hero, on a ring of places that grows: the same places on every machine. Each
            // place turns by the golden angle (2.39996 rad) from the previous one, so that they spread
            // evenly; the turn is a fixed rotation (its cosine and sine written out), applied with
            // multiplications and additions only: no std::cos / std::sin in the game's logic.
            constexpr float kTurnCos = -0.737366696f;
            constexpr float kTurnSin = 0.675492676f;
            glm::vec2 heading{1.0f, 0.0f};
            for (int k = 0; made < count && k < count * 20; ++k) {
                if (k > 0) {
                    heading = {heading.x * kTurnCos - heading.y * kTurnSin, heading.x * kTurnSin + heading.y * kTurnCos};
                }
                const float radius = 2.0f + 0.35f * std::sqrt(static_cast<float>(k));
                const glm::vec2 wanted = hero + radius * heading;
                const glm::vec3 wanted3(wanted.x, foot_of(hero_).y, wanted.y);
                if (!fits(wanted3)) {
                    continue;
                }
                const float height = level_mode() ? free_place(wanted3).value_or(wanted3).y : 0.0f;
                const glm::vec2 direction = glm::normalize(hero - wanted);
                spawn_creature({wanted, direction, 1.0f, linear(0.6f, 0.55f, 0.5f), 1.0f, args[0], height}, static_cast<int>(creatures_));
                ++made;
            }
            out.print(std::to_string(made) + " « " + args[0] + " » de plus");
        },
        [this](std::size_t index, const std::string& prefix) {
            std::vector<std::string> names;
            if (index == 0) {
                const moteur::DataTable<CharacterData>& table = app_.data().get<CharacterData>();
                for (const std::uint32_t row : table.sorted()) {
                    if (table.id(row).starts_with(prefix) && table.id(row) != "chevalier") {
                        names.push_back(table.id(row));
                    }
                }
            }
            return names;
        });
    console.add("kill", "all", "abat toutes les créatures (tranche)", Kind::Game,
                [this](const moteur::Console::Args& args, moteur::Console& out) {
                    if (args.size() != 1 || args[0] != "all") {
                        out.error("kill all");
                        return;
                    }
                    entt::registry& registry = world_.registry();
                    std::vector<entt::entity> living;
                    for (auto [entity, health, hunter] : registry.view<Health, Hunter>().each()) {
                        (void)hunter;
                        if (health.value > 0.0f) {
                            living.push_back(entity);
                        }
                    }
                    std::sort(living.begin(), living.end(),
                              [](entt::entity a, entt::entity b) { return entt::to_integral(a) < entt::to_integral(b); });
                    for (const entt::entity creature : living) {
                        hurt(creature, 1.0f);
                    }
                    out.print(std::to_string(living.size()) + " créature(s) abattue(s)");
                },
                [](std::size_t index, const std::string& prefix) {
                    return index == 0 && std::string("all").starts_with(prefix) ? std::vector<std::string>{"all"}
                                                                                : std::vector<std::string>{};
                });
    console.add("fog", "on|off", "le brouillard de guerre (tranche)", Kind::Game,
                [this](const moteur::Console::Args& args, moteur::Console& out) {
                    fog_on_ = args.empty() ? !fog_on_ : (args[0] == "on" || args[0] == "1");
                    view_cell_ = glm::ivec2(-1);  // the field of view again: what it hides changes
                    out.print(fog_on_ ? "brouillard activé" : "brouillard désactivé");
                },
                [](std::size_t index, const std::string& prefix) {
                    std::vector<std::string> names;
                    for (const char* word : {"on", "off"}) {
                        if (index == 0 && std::string(word).starts_with(prefix)) {
                            names.push_back(word);
                        }
                    }
                    return names;
                });
}

void Demo3D::start_measure() {
    god_ = true;
    entt::registry& registry = world_.registry();
    const glm::vec2 hero = cell_position(hero_);
    if (options_.measure == "combat") {
        // Every living creature on a spiral around the hero (the same places on every machine).
        std::vector<entt::entity> creatures;
        for (auto [entity, health, hunter] : registry.view<Health, Hunter>().each()) {
            (void)hunter;
            if (health.value > 0.0f) {
                creatures.push_back(entity);
            }
        }
        std::sort(creatures.begin(), creatures.end(),
                  [](entt::entity a, entt::entity b) { return entt::to_integral(a) < entt::to_integral(b); });
        std::size_t placed = 0;
        for (int k = 0; placed < creatures.size() && k < static_cast<int>(creatures.size()) * 20; ++k) {
            const float angle = static_cast<float>(k) * 2.39996f;
            const float radius = 2.0f + 0.4f * std::sqrt(static_cast<float>(k));
            const glm::vec2 at = hero + radius * glm::vec2(std::cos(angle), std::sin(angle));
            const glm::vec3 at3(at.x, foot_of(hero_).y, at.y);
            if (!fits(at3)) {
                continue;
            }
            const entt::entity creature = creatures[placed++];
            moteur::Transform transform = registry.get<moteur::Transform>(creature);
            transform.position = level_mode() ? free_place(at3).value_or(at3) : glm::vec3(at.x, transform.position.y, at.y);
            registry.replace<moteur::Transform>(creature, transform);
            registry.replace<moteur::PreviousTransform>(creature, transform);
            registry.get<moteur::Mover>(creature).stop();
        }
        // 30 fires and 32 lights on rings around the hero; the engine shadows 4 of the lights.
        static constexpr glm::vec3 kColors[3] = {{1.0f, 0.5f, 0.15f}, {0.4f, 0.6f, 1.0f}, {0.9f, 0.3f, 1.0f}};
        for (int i = 0; i < 30; ++i) {
            const float angle = static_cast<float>(i) * 6.2831853f / 30.0f;
            play_effect(kBrazierFire, glm::vec3(hero.x + 6.0f * std::cos(angle), foot_of(hero_).y + 0.1f, hero.y + 6.0f * std::sin(angle)));
        }
        for (int i = 0; i < 32; ++i) {
            const float angle = static_cast<float>(i) * 6.2831853f / 32.0f + 0.1f;
            const float radius = i % 2 == 0 ? 4.0f : 8.0f;
            const entt::entity light = registry.create();
            registry.emplace<moteur::Transform>(
                light, place(glm::vec3(hero.x + radius * std::cos(angle), foot_of(hero_).y + 1.2f, hero.y + radius * std::sin(angle)),
                             glm::vec3(0.12f)));
            registry.emplace<moteur::MeshComponent>(light, sphere_, glowing(kColors[i % 3] * 8.0f));
            registry.emplace<moteur::LightSource>(light, kColors[i % 3], 4.0f, 6.0f, true);
            registry.emplace<moteur::Name>(light, "lumière de mesure " + std::to_string(i));
        }
        SDL_Log("Measure: combat, %zu creatures around the hero, 30 fires, 32 lights", placed);
    } else if (options_.measure == "traversee" && level_mode()) {
        // The level's tour: its points "tour_01", "tour_02"... in order, put on the navigation mesh.
        std::vector<const moteur::LevelPoint*> tour;
        for (const moteur::LevelPoint& point : level_->header().points) {
            if (point.name.rfind("tour_", 0) == 0) {
                tour.push_back(&point);
            }
        }
        std::sort(tour.begin(), tour.end(), [](const moteur::LevelPoint* a, const moteur::LevelPoint* b) { return a->name < b->name; });
        for (const moteur::LevelPoint* point : tour) {
            waypoints_.push_back(free_place(point->position).value_or(point->position));
        }
        SDL_Log("Measure: traversee, %zu waypoints of the level", waypoints_.size());
    } else if (options_.measure == "traversee") {
        // A serpentine over the whole map, each point moved to the nearest place the hero fits.
        const int size = grid_.width();
        for (int row = 0; row < 5; ++row) {
            const float z = static_cast<float>(size) * (0.1f + 0.2f * static_cast<float>(row));
            for (int column = 0; column < 4; ++column) {
                const int c = row % 2 == 0 ? column : 3 - column;
                const float x = static_cast<float>(size) * (0.12f + 0.25f * static_cast<float>(c));
                if (const std::optional<glm::vec2> fit = moteur::nearest_fit(grid_, {x, z}, kCharacterRadius)) {
                    waypoints_.push_back({fit->x, 0.0f, fit->y});
                }
            }
        }
        SDL_Log("Measure: traversee, %zu waypoints", waypoints_.size());
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Measure: unknown scene '%s' (combat, traversee)",
                     options_.measure.c_str());
    }
}

void Demo3D::drive_measure() {
    entt::registry& registry = world_.registry();
    if (options_.measure == "combat") {
        if (const entt::entity target = creature_in_reach(); target != entt::null) {
            attack(target);  // refused while a blow is under way
        }
    } else if (options_.measure == "traversee" && !waypoints_.empty()) {
        if (level_mode()) {
            // A level's tour goes through the creatures: the hero strikes what is in reach on its way.
            if (const entt::entity target = creature_in_reach(); target != entt::null) {
                attack(target);
            }
        }
        moteur::Mover& mover = registry.get<moteur::Mover>(hero_);
        const glm::vec3 goal = waypoints_[waypoint_ % waypoints_.size()];
        const bool arrived = glm::length(glm::vec2(goal.x, goal.z) - cell_position(hero_)) < 1.0f;
        if (arrived || mover.mode == moteur::MoveMode::Stop) {
            if (arrived) {
                const glm::vec3 at = foot_of(hero_);
                SDL_Log("Measure: waypoint %zu reached at tick %ld (%.1f, %.2f, %.1f)", waypoint_ % waypoints_.size() + 1, live_ticks_,
                        at.x, at.y, at.z);
                ++waypoint_;
            }
            mover.go_to(waypoints_[waypoint_ % waypoints_.size()]);
        }
    }
}

float Demo3D::hero_health() const {
    const entt::registry& registry = world_.registry();
    return hero_ != entt::null && registry.all_of<Health>(hero_) ? registry.get<Health>(hero_).value : 0.0f;
}

int Demo3D::creatures_standing() const {
    int standing = 0;
    for (auto [entity, health, hunter] : world_.registry().view<const Health, const Hunter>().each()) {
        (void)entity;
        (void)hunter;
        standing += health.value > 0.0f ? 1 : 0;
    }
    return standing;
}
