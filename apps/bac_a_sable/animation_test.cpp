#include "animation_test.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "moteur/animation_debug.hpp"
#include "moteur/animator.hpp"
#include "moteur/audio.hpp"
#include "moteur/color.hpp"
#include "moteur/debug_lines.hpp"
#include "moteur/debug_tools.hpp"
#include "moteur/mesh_renderer.hpp"

#include "demo3d.hpp"

namespace {

constexpr float kShownHeight = 2.0f;  // metres: every character is scaled to it, side by side
constexpr float kGap = 0.8f;
constexpr float kPitch = 12.0f;       // degrees below the horizon
constexpr float kFieldOfView = 30.0f;

// The knight carries every weapon of its pack: one sword and one shield are enough.
const std::vector<std::string> kKnightExtras = {"1H_Sword_Offhand", "2H_Sword", "Badge_Shield", "Rectangle_Shield",
                                                "Spike_Shield"};
const char* const kKnight = "models/characters/kaykit_adventurers/Knight.glb";
const char* const kKaykitSet = "animations/kaykit.json";
const char* const kSword = "models/characters/kaykit_adventurers/sword_1handed.gltf";

// The walker's ellipse, in front of the row.
constexpr float kPathX = 3.6f;  // half-axes, metres
constexpr float kPathZ = 1.1f;
constexpr float kPathCentreZ = 2.4f;
constexpr float kAcceleration = 3.0f;  // m/s per second, towards the chosen speed

}  // namespace

AnimationTest::AnimationTest(moteur::Application& app, const Options& options, bool standalone)
    : app_(app), options_(options), standalone_(standalone) {
    moteur::Renderer& renderer = app.renderer();
    floor_ = moteur::Mesh::create(renderer, moteur::make_plane({30.0f, 8.0f}), "animation.floor");
    sky_ = moteur::Environment::create(renderer, moteur::make_sky(256, 128), "animation.sky");

    if (options_.crowd > 0) {
        build_crowd(options_.crowd);
        moteur::ShadowOptions shadows;
        shadows.enabled = options_.sun_shadows;
        renderer.meshes().set_shadows(shadows);
        torch_on_ = false;
    }
    if (options_.crowd == 0) {
        add("models/skinned_reference.glb", {}, "Bend");
        add(kKnight, kKnightExtras, "Idle");
        add("models/characters/kaykit_skeletons/Skeleton_Warrior.glb", {}, "Walking_A");
        add("models/characters/kaykit_skeletons/Skeleton_Minion.glb", {}, "Running_A");
        add("models/characters/khronos/Fox.glb", {}, "Walk");
        add("models/characters/khronos/RiggedFigure.glb", {}, "");
        place_in_row();
        add_walker();
    }
    // The slice's sounds (tools/audio/fetch_test_sounds.py; without them, the events are only listed).
    moteur::Assets& assets = app.assets();
    if (assets.exists(Demo3D::kSteps[0])) {
        for (const char* path : Demo3D::kSteps) {
            steps_.push_back(assets.sound(path));
        }
        for (const char* path : Demo3D::kImpacts) {
            impacts_.push_back(assets.sound(path));
        }
    }

    // A torch on the left of the row: its shadows (a point light's) follow the poses.
    entt::registry& registry = world_.registry();
    torch_ = registry.create();
    registry.emplace<moteur::Name>(torch_, "Torche");
    moteur::Transform torch_place;
    torch_place.position = {-4.0f, 2.0f, 1.5f};
    registry.emplace<moteur::Transform>(torch_, torch_place);
    registry.emplace<moteur::LightSource>(torch_, glm::vec3(1.0f, 0.6f, 0.3f), 20.0f, 16.0f, true);
    if (!torch_on_) {
        registry.emplace<moteur::Hidden>(torch_);
    }

    if (moteur::DebugTools* tools = app.debug_tools()) {
        tools->watch(world_, "Animation");
    }
}

AnimationTest::~AnimationTest() {
    if (options_.report && measured_.frames > 0) {
        const auto n = static_cast<double>(measured_.frames);
        SDL_Log("perf: animation  %.0f animated, %.0f poses sampled, %.0f palettes (%.0f matrices) per frame",
                measured_.animated / n, measured_.poses / n, measured_.palettes / n, measured_.matrices / n);
        SDL_Log("perf: animation  sampling %.3f ms, palettes %.3f ms (means over %ld frames)", measured_.sample_ms / n,
                measured_.palette_ms / n, measured_.frames);
    }
    if (moteur::DebugTools* tools = app_.debug_tools()) {
        tools->forget(world_);
    }
}

void AnimationTest::add(const std::string& path, const std::vector<std::string>& hidden_nodes, const std::string& first_clip) {
    moteur::Assets& assets = app_.assets();
    if (!assets.exists(path)) {
        SDL_Log("Animation test: no '%s' (python tools/models/fetch_test_characters.py)", path.c_str());
        return;
    }
    entt::registry& registry = world_.registry();
    Character character;
    character.path = path;
    character.entity = registry.create();
    registry.emplace<moteur::Name>(character.entity, path.substr(path.rfind('/') + 1));
    registry.emplace<moteur::Transform>(character.entity);
    moteur::ModelComponent model{assets.model(path), {}};
    for (const std::string& node : hidden_nodes) {
        model.hide(node);
    }
    registry.emplace<moteur::ModelComponent>(character.entity, std::move(model));
    moteur::Animator animator = moteur::Animator::create(assets.skeleton(path), assets.clips(path));
    character.clip_names = animator.clips->names();
    const auto found = std::find(character.clip_names.begin(), character.clip_names.end(), first_clip);
    character.clip = found != character.clip_names.end() ? static_cast<int>(found - character.clip_names.begin()) : 0;
    registry.emplace<moteur::Animator>(character.entity, std::move(animator));
    characters_.push_back(std::move(character));
    restart(characters_.back());
}

void AnimationTest::add_walker() {
    moteur::Assets& assets = app_.assets();
    if (!assets.exists(kKnight)) {
        return;
    }
    entt::registry& registry = world_.registry();
    Walker& walker = walker_;
    walker.entity = registry.create();
    registry.emplace<moteur::Name>(walker.entity, "Chevalier (mélanges)");
    moteur::ModelComponent model{assets.model(kKnight), {}};
    for (const std::string& node : kKnightExtras) {
        model.hide(node);
    }
    const float height = model.model->bounds.size().y;
    walker.scale = height > 0.0f ? kShownHeight / height : 1.0f;
    registry.emplace<moteur::ModelComponent>(walker.entity, std::move(model));
    registry.emplace<moteur::Transform>(walker.entity);
    registry.emplace<moteur::PreviousTransform>(walker.entity);

    moteur::Animator animator =
        moteur::Animator::create(assets.skeleton(kKnight), assets.clips(kKnight), assets.animation_set(kKaykitSet));
    const moteur::AnimationSet& set = *animator.set;
    walker.motions.push_back("locomotion");
    for (const std::string& name : animator.clips->names()) {
        walker.motions.push_back(name);
        if (name.find("Melee_Attack") != std::string::npos) {
            walker.attacks.push_back(name);
        }
    }
    const auto chop = std::find(walker.attacks.begin(), walker.attacks.end(), "1H_Melee_Attack_Chop");
    walker.attack = chop != walker.attacks.end() ? static_cast<int>(chop - walker.attacks.begin()) : 0;
    walker.fade = set.default_fade_ticks();

    // The ground speeds of the file, against those measured on the clips.
    const moteur::SkeletonData& joints = animator.skeleton->data();
    walker.feet[0] = joints.find("foot.l");
    walker.feet[1] = joints.find("foot.r");
    const moteur::BlendSpaceData* locomotion = set.blend_space("locomotion");
    if (locomotion != nullptr && walker.feet[0] >= 0 && walker.feet[1] >= 0) {
        for (const std::string& name : locomotion->clips) {
            const float declared = set.clip(name).ground_speed;
            if (declared <= 0.0f) {
                continue;
            }
            const moteur::Stride stride = moteur::measure_stride(*animator.skeleton, animator.clips->clip(name),
                                                                 {walker.feet[0], walker.feet[1]});
            char line[160];
            SDL_snprintf(line, sizeof line, "%s : %.2f m/s dans %s, %.3f mesurée (x %.2f : %.2f m/s ici)\n",
                         name.c_str(), static_cast<double>(declared), kKaykitSet, static_cast<double>(stride.ground_speed),
                         static_cast<double>(walker.scale), static_cast<double>(declared * walker.scale));
            walker.strides += line;
        }
        walker.max_speed = locomotion->positions.back() * walker.scale * 1.3f;
        walker.target_speed = locomotion->positions[1] * walker.scale;  // a walk
    }
    animator.play("locomotion", {.fade_ticks = 0});
    registry.emplace<moteur::Animator>(walker.entity, std::move(animator));
    add_held_objects();
    update_walker(0.0);
    registry.replace<moteur::PreviousTransform>(walker.entity, registry.get<moteur::Transform>(walker.entity));
}

// A sword of its own file on the right hand, a torch on the left, a marker on the head.
void AnimationTest::add_held_objects() {
    Walker& walker = walker_;
    moteur::Assets& assets = app_.assets();
    moteur::Renderer& renderer = app_.renderer();
    entt::registry& registry = world_.registry();
    const auto hold = [&](const char* name, entt::entity owner, const char* point, const moteur::Transform& offset) {
        const entt::entity held = registry.create();
        registry.emplace<moteur::Name>(held, name);
        registry.emplace<moteur::Transform>(held, offset);
        registry.emplace<moteur::Parent>(held, owner);
        if (point != nullptr) {
            registry.emplace<moteur::BoneAttachment>(held, point);
        }
        return held;
    };
    if (assets.exists(kSword)) {
        walker.sword = hold("Épée (right_hand)", walker.entity, "right_hand", {});
        registry.emplace<moteur::ModelComponent>(walker.sword, assets.model(kSword), std::vector<std::uint32_t>{});
    }
    // A stick held in the fist, the flame at its top, the light in the flame. Metres of the file:
    // the knight's scale applies (it is 2.45 m tall there).
    stick_ = moteur::make_asset(moteur::Mesh::create(renderer, moteur::make_cube({0.06f, 0.6f, 0.06f}), "animation.torch"));
    flame_ = moteur::make_asset(moteur::Mesh::create(renderer, moteur::make_sphere(0.09f, 16, 8), "animation.flame"));
    moteur::Transform stick_place;
    stick_place.position = {0.0f, 0.15f, 0.0f};
    walker.torch = hold("Torche (left_hand)", walker.entity, "left_hand", stick_place);
    moteur::Material wood;
    wood.base_color = glm::vec4(moteur::srgb_to_linear(glm::vec3(0.35f, 0.22f, 0.12f)), 1.0f);
    wood.roughness = 0.8f;
    registry.emplace<moteur::MeshComponent>(walker.torch, stick_, wood);
    moteur::Transform flame_place;
    flame_place.position = {0.0f, 0.35f, 0.0f};
    const entt::entity flame = hold("Flamme", walker.torch, nullptr, flame_place);
    moteur::Material fire;
    fire.base_color = glm::vec4(1.0f, 0.6f, 0.2f, 1.0f);
    fire.emissive = glm::vec3(8.0f, 3.5f, 0.8f);
    fire.casts_shadow = false;
    registry.emplace<moteur::MeshComponent>(flame, flame_, fire);
    registry.emplace<moteur::LightSource>(flame, glm::vec3(1.0f, 0.55f, 0.2f), 6.0f, 6.0f, true);
    moteur::Transform above;
    above.position = {0.0f, 1.5f, 0.0f};  // the knight's helmet is big: the head joint is at the neck
    walker.marker = hold("Repère (head)", walker.entity, "head", above);
    show_held_objects();
}

// The objects on its points, or those of the knight's file.
void AnimationTest::show_held_objects() {
    Walker& walker = walker_;
    entt::registry& registry = world_.registry();
    moteur::ModelComponent& model = registry.get<moteur::ModelComponent>(walker.entity);
    model.hidden_parts.clear();
    for (const std::string& node : kKnightExtras) {
        model.hide(node);
    }
    const auto show = [&](entt::entity entity, bool shown) {
        if (entity == entt::null) {
            return;
        }
        if (shown) {
            registry.remove<moteur::Hidden>(entity);
        } else if (!registry.all_of<moteur::Hidden>(entity)) {
            registry.emplace<moteur::Hidden>(entity);
        }
    };
    const bool own_sword = walker.own_sword && walker.sword != entt::null;
    if (own_sword) {
        model.hide("1H_Sword");
    }
    show(walker.sword, own_sword);
    if (walker.torch_on) {
        model.hide("Round_Shield");
    }
    show(walker.torch, walker.torch_on);  // its flame and light hang on it
}

// Towards the chosen speed, around the ellipse, facing where it goes; the animation follows the speed.
void AnimationTest::update_walker(double dt) {
    Walker& walker = walker_;
    if (walker.entity == entt::null) {
        return;
    }
    const auto step = static_cast<float>(dt);
    const float change = std::clamp(walker.target_speed - walker.speed, -kAcceleration * step, kAcceleration * step);
    walker.speed += change;
    if (walker.moving) {
        const float along = std::sqrt(kPathX * kPathX * std::sin(walker.angle) * std::sin(walker.angle) +
                                      kPathZ * kPathZ * std::cos(walker.angle) * std::cos(walker.angle));
        walker.angle = std::fmod(walker.angle + walker.speed * step / along, glm::two_pi<float>());
    }
    const glm::vec3 heading = glm::normalize(glm::vec3(-kPathX * std::sin(walker.angle), 0.0f, kPathZ * std::cos(walker.angle)));
    moteur::Transform transform;
    transform.position = {kPathX * std::cos(walker.angle), 0.0f, kPathCentreZ + kPathZ * std::sin(walker.angle)};
    transform.rotation = glm::angleAxis(std::atan2(heading.x, heading.z), glm::vec3(0.0f, 1.0f, 0.0f));  // faces +Z
    transform.scale = glm::vec3(walker.scale);
    entt::registry& registry = world_.registry();
    registry.replace<moteur::Transform>(walker.entity, transform);
    // In metres of the file: the Animator knows nothing of the entity's scale.
    registry.get<moteur::Animator>(walker.entity).set_move_speed(walker.speed / walker.scale);
}

// How fast the lower foot drifts along the way while it stays the lower one, on the floor: about 0
// when the pace of the animation matches the character's speed.
void AnimationTest::measure_slide(float blend) {
    Walker& walker = walker_;
    entt::registry& registry = world_.registry();
    const auto* pose = walker.entity != entt::null ? registry.try_get<moteur::AnimationPose>(walker.entity) : nullptr;
    if (pose == nullptr || walker.feet[0] < 0 || walker.feet[1] < 0) {
        return;
    }
    const glm::mat4 world = world_.world_matrix(walker.entity, blend);
    const std::vector<glm::mat4>& joints = pose->sampler.model();
    glm::vec3 feet[2];
    for (int k = 0; k < 2; ++k) {
        feet[k] = glm::vec3(world * joints[static_cast<std::size_t>(walker.feet[k])][3]);
    }
    const int lower = feet[0].y <= feet[1].y ? 0 : 1;
    const std::uint64_t now = SDL_GetTicksNS();
    const double seconds = static_cast<double>(now - walker.last_ns) * 1e-9;
    walker.lowest = std::min(walker.lowest, feet[lower].y);
    const bool on_floor = feet[lower].y <= walker.lowest + 0.03f;  // as measure_stride: near its lowest
    if (on_floor && walker.planted == lower && walker.last_ns != 0 && seconds > 0.0 && seconds < 0.1) {
        // Along the way it walks: forwards (+) or backwards (-). Its sideways wobble and its roll
        // from heel to toe belong to the clip; the drift is what matching the pace cancels.
        const glm::vec3 heading = registry.get<moteur::Transform>(walker.entity).rotation * glm::vec3(0.0f, 0.0f, 1.0f);
        const float speed = glm::dot(feet[lower] - walker.foot_at[lower], heading) / static_cast<float>(seconds);
        walker.slide += (speed - walker.slide) * std::min(1.0f, static_cast<float>(seconds) / 0.3f);
    }
    walker.planted = lower;
    walker.foot_at[0] = feet[0];
    walker.foot_at[1] = feet[1];
    walker.last_ns = now;
}

void AnimationTest::on_events() {
    entt::registry& registry = world_.registry();
    for (const moteur::AnimatorEvent& event : events_) {
        const glm::vec3 at = registry.get<moteur::Transform>(event.entity).position;
        const bool step = event.name.rfind("step", 0) == 0;
        const std::vector<moteur::Asset<moteur::Sound>>& sounds = step ? steps_ : impacts_;
        if (!sounds.empty() && crowd_.empty()) {  // a crowd's hundreds of steps: listed only
            moteur::PlaySound sound;
            sound.position = at;
            sound.volume = step ? 0.6f : 0.8f;
            sound.pitch = event.name == "death_ground" ? 0.6f : 1.0f;
            sound.pitch_variation = 0.06f;
            sound.volume_variation = 0.15f;
            app_.audio().play(sounds, sound);
        }
        char line[160];
        SDL_snprintf(line, sizeof line, "tick %lld : %s (%s, %s, %.0f %%)", static_cast<long long>(tick_), event.name.c_str(),
                     event.clip.c_str(), event.layer == 0 ? "corps" : "haut", event.weight / 10.0);
        event_log_.insert(event_log_.begin(), line);
    }
    if (event_log_.size() > 8) {
        event_log_.resize(8);
    }
}

std::uint32_t AnimationTest::next_random() {
    random_ ^= random_ << 13;
    random_ ^= random_ >> 17;
    random_ ^= random_ << 5;
    return random_;
}

void AnimationTest::build_crowd(int count) {
    moteur::Assets& assets = app_.assets();
    const char* const paths[] = {kKnight, "models/characters/kaykit_skeletons/Skeleton_Warrior.glb",
                                 "models/characters/kaykit_skeletons/Skeleton_Minion.glb"};
    for (const char* path : paths) {
        if (!assets.exists(path)) {
            SDL_Log("Animation test: no '%s' (python tools/models/fetch_test_characters.py)", path);
            return;
        }
    }
    entt::registry& registry = world_.registry();
    constexpr float kSpacing = 1.6f;
    const int columns = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<float>(count) * 1.8f))));
    const int rows = (count + columns - 1) / columns;
    for (int i = 0; i < count; ++i) {
        const char* path = paths[i % 3];
        const entt::entity entity = registry.create();
        registry.emplace<moteur::Name>(entity, "Foule " + std::to_string(i));
        moteur::ModelComponent model{assets.model(path), {}};
        if (path == kKnight) {
            for (const std::string& node : kKnightExtras) {
                model.hide(node);
            }
        }
        const float height = model.model->bounds.size().y;
        moteur::Transform place;
        place.position = {(static_cast<float>(i % columns) - 0.5f * static_cast<float>(columns - 1)) * kSpacing, 0.0f,
                          (static_cast<float>(i / columns) - 0.5f * static_cast<float>(rows - 1)) * kSpacing};
        place.rotation = glm::angleAxis(static_cast<float>(next_random() % 360) * glm::pi<float>() / 180.0f,
                                        glm::vec3(0.0f, 1.0f, 0.0f));
        place.scale = glm::vec3(height > 0.0f ? kShownHeight / height : 1.0f);
        registry.emplace<moteur::Transform>(entity, place);
        registry.emplace<moteur::ModelComponent>(entity, std::move(model));
        moteur::Animator animator =
            moteur::Animator::create(assets.skeleton(path), assets.clips(path), assets.animation_set(kKaykitSet));
        animator.set_speed(0.8 + 0.4 * static_cast<double>(next_random() % 1000) / 1000.0);
        play_random(animator, 0);
        animator.advance(static_cast<int>(next_random() % 120));  // each at its own phase
        registry.emplace<moteur::Animator>(entity, std::move(animator));
        crowd_.push_back(entity);
    }
    // Seen as in the game: from above, the whole crowd in view.
    camera_.set_projection(moteur::Projection::Perspective);
    camera_.set_angles(0.0f, 50.0f);
    camera_.set_field_of_view(kFieldOfView);
    camera_.set_target({0.0f, 0.0f, 0.0f});
    camera_.set_visible_height(std::max(static_cast<float>(rows) * kSpacing * 1.3f + 2.0f,
                                        (static_cast<float>(columns) * kSpacing + 2.0f) * 9.0f / 16.0f) /
                               std::max(options_.crowd_zoom, 0.1f));
}

void AnimationTest::play_random(moteur::Animator& animator, int fade) {
    static const char* const kMotions[] = {"locomotion", "locomotion", "Idle", "Running_A", "Walking_B",
                                           "1H_Melee_Attack_Chop", "Hit_A", "Death_A"};
    const std::string name = kMotions[next_random() % std::size(kMotions)];
    const bool loop = name != "Hit_A" && name != "Death_A";
    animator.play(name, {.fade_ticks = fade, .loop = loop, .restart = true, .match_speed = true});
    animator.set_move_speed(3.5 * static_cast<double>(next_random() % 1000) / 1000.0);
}

void AnimationTest::stir_crowd() {
    if (crowd_.empty()) {
        return;
    }
    entt::registry& registry = world_.registry();
    const std::size_t changes = std::max<std::size_t>(1, crowd_.size() / 60);  // each changes every second or so
    for (std::size_t k = 0; k < changes; ++k) {
        moteur::Animator& animator = registry.get<moteur::Animator>(crowd_[next_random() % crowd_.size()]);
        if (next_random() % 3 == 0) {
            animator.play("1H_Melee_Attack_Slice_Diagonal", {.layer = 1, .loop = false});
        } else {
            play_random(animator, 12);
        }
    }
}

// Side by side, each scaled to the same height, feet on the floor.
void AnimationTest::place_in_row() {
    entt::registry& registry = world_.registry();
    std::vector<float> centres;
    float cursor = 0.0f;
    for (Character& character : characters_) {
        const moteur::Aabb bounds = registry.get<moteur::ModelComponent>(character.entity).model->bounds;
        character.height = bounds.size().y;
        const float scale = character.height > 0.0f ? kShownHeight / character.height : 1.0f;
        const float width = std::max(bounds.size().x, bounds.size().z) * scale;
        centres.push_back(cursor + width * 0.5f);
        cursor += width + kGap;
    }
    const float half = (cursor - kGap) * 0.5f;
    for (std::size_t i = 0; i < characters_.size(); ++i) {
        const moteur::Aabb bounds = registry.get<moteur::ModelComponent>(characters_[i].entity).model->bounds;
        const float scale = characters_[i].height > 0.0f ? kShownHeight / characters_[i].height : 1.0f;
        moteur::Transform transform;
        transform.scale = glm::vec3(scale);
        transform.position = glm::vec3(centres[i] - half, 0.0f, 0.0f) -
                             scale * glm::vec3(bounds.center().x, bounds.min.y, bounds.center().z);
        registry.replace<moteur::Transform>(characters_[i].entity, transform);
    }

    camera_.set_projection(moteur::Projection::Perspective);
    camera_.set_angles(0.0f, kPitch);  // from +Z
    camera_.set_field_of_view(kFieldOfView);
    camera_.set_target({0.0f, kShownHeight * 0.5f, 0.0f});
    camera_.set_visible_height(std::max(kShownHeight * 1.6f, (cursor + 1.0f) * 9.0f / 16.0f));
}

void AnimationTest::restart(Character& character) {
    moteur::Animator& animator = world_.registry().get<moteur::Animator>(character.entity);
    animator.set_speed(playing_ ? speed_ : 0.0f);
    if (rest_pose_ || character.clip_names.empty()) {
        animator.clear();
        return;
    }
    moteur::PlayOptions options;
    options.restart = true;
    options.fade_ticks = 0;
    animator.play(character.clip_names[static_cast<std::size_t>(character.clip)], options);
}

void AnimationTest::update(double dt) {
    elapsed_ += dt;
    tick_seconds_ = dt;
    if (options_.run_seconds > 0.0 && elapsed_ >= options_.run_seconds) {
        if (standalone_) {
            app_.quit();
        } else {
            stop_requested_ = true;
        }
    }
    world_.begin_tick();
    update_walker(dt);
    stir_crowd();
    events_.clear();
    // Paused, "Tick suivant" plays one tick at the panel's speed.
    const bool step = step_ && !playing_;
    step_ = false;
    if (step) {
        playing_ = true;
        apply_speed();
    }
    moteur::advance_animators(world_.registry(), 1, &events_);
    if (step) {
        playing_ = false;
        apply_speed();
    }
    ++tick_;
    on_events();
}

void AnimationTest::render(moteur::Renderer& renderer, double alpha) {
    renderer.set_clear_color(0.35f, 0.38f, 0.42f);
    camera_.set_viewport({static_cast<float>(renderer.width()), static_cast<float>(renderer.height())});
    app_.audio().set_listener(camera_);
    moteur::MeshRenderer& meshes = renderer.meshes();
    meshes.set_camera(camera_.view_projection(), camera_.position());
    meshes.set_sun(glm::normalize(glm::vec3(-0.4f, 1.0f, 0.6f)), glm::vec3(1.0f, 0.95f, 0.85f), 3.0f);
    meshes.set_environment(&*sky_, 1.0f);
    meshes.draw(floor_, glm::mat4(1.0f), glm::vec4(moteur::srgb_to_linear(glm::vec3(0.45f)), 1.0f));

    moteur::CollectOptions collect;
    collect.view = camera_.frustum();
    collect.bind_pose = bind_pose_;
    meshes.set_weight_joint(weight_palette_index());
    const float blend = interpolation_ ? static_cast<float>(alpha) : 1.0f;
    world_.submit(renderer, blend, collect);
    measure_slide(blend);
    if (++frame_ > 120) {  // after the loading and the first frames
        const moteur::AnimationStats& stats = world_.animation_stats();
        ++measured_.frames;
        measured_.animated += stats.animated;
        measured_.poses += stats.poses;
        measured_.palettes += stats.palettes;
        measured_.matrices += static_cast<double>(stats.matrices);
        measured_.sample_ms += stats.sample_ms;
        measured_.palette_ms += stats.palette_ms;
    }

    // Where a health bar would go: on the head (yellow), or above the box (white).
    if (walker_.head_markers && walker_.marker != entt::null) {
        moteur::DebugLineBuffer& marks = renderer.debug_lines().lines();
        marks.sphere(world_.world_position(walker_.marker, blend), 0.08f, glm::vec4(1.0f, 0.85f, 0.2f, 1.0f), true);
        const moteur::Aabb box = moteur::transform_box(world_.registry().get<moteur::ModelComponent>(walker_.entity).model->bounds,
                                                       world_.world_matrix(walker_.entity, blend));
        marks.sphere({box.center().x, box.max.y + 0.3f, box.center().z}, 0.08f, glm::vec4(1.0f), true);
    }

    if (!show_skeleton_) {
        return;
    }
    // The pose drawing used, from the World's cache (AnimationPose): the lines never lag behind the meshes.
    entt::registry& registry = world_.registry();
    moteur::DebugLineBuffer& lines = renderer.debug_lines().lines();
    moteur::SkeletonDrawOptions options;
    options.axis_length = axes_ ? 0.06f : 0.0f;
    for (std::size_t c = 0; c <= characters_.size(); ++c) {
        const entt::entity entity = c < characters_.size() ? characters_[c].entity : walker_.entity;
        if (entity == entt::null) {
            continue;
        }
        const auto* pose = registry.try_get<moteur::AnimationPose>(entity);
        if (pose == nullptr) {
            continue;
        }
        const bool selected = static_cast<int>(c) == selected_;
        options.color = selected ? glm::vec4(1.0f, 0.85f, 0.2f, 1.0f) : glm::vec4(0.2f, 0.9f, 1.0f, 1.0f);
        options.highlight = selected ? joint_ : -1;
        moteur::draw_skeleton(lines, world_.world_matrix(entity, blend), registry.get<moteur::Animator>(entity).skeleton->data(),
                              pose->sampler.model(), options);
    }
}

void AnimationTest::apply_speed() {
    entt::registry& registry = world_.registry();
    for (auto [entity, animator] : registry.view<moteur::Animator>().each()) {
        animator.set_speed(playing_ ? speed_ : 0.0f);
    }
}

int AnimationTest::weight_palette_index() const {
    if (joint_ < 0 || characters_.empty()) {
        return -1;
    }
    const auto& model = world_.registry().get<moteur::ModelComponent>(characters_[static_cast<std::size_t>(selected_)].entity).model;
    if (!model || model->skins.empty()) {
        return -1;
    }
    const std::vector<int>& palette = model->skins.front().joints;
    const auto found = std::find(palette.begin(), palette.end(), joint_);
    return found != palette.end() ? static_cast<int>(found - palette.begin()) : -1;
}

void AnimationTest::draw_controls() {
    ImGui::TextWrapped("Les personnages de test, chacun jouant un clip de son fichier, au tick, dessinés entre deux "
                       "ticks, déformés sur le GPU (skinning) ; le squelette est dessiné par-dessus, et les armes et "
                       "casques suivent leur os.");
    if (characters_.size() < 2) {
        ImGui::TextWrapped("Personnages absents : python tools/models/fetch_test_characters.py");
    }
    entt::registry& registry = world_.registry();
    bool changed_playback = ImGui::Checkbox("Lecture", &playing_);
    ImGui::SameLine();
    ImGui::Checkbox("Squelette", &show_skeleton_);
    ImGui::SameLine();
    if (ImGui::Checkbox("Pose de repos", &rest_pose_)) {
        for (Character& character : characters_) {
            restart(character);
        }
    }
    ImGui::SameLine();
    ImGui::Checkbox("Interpolation", &interpolation_);
    ImGui::BeginDisabled(playing_);
    if (ImGui::Button("Tick suivant")) {
        step_ = true;
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("En pause : avance d'un tick (à la vitesse choisie), événements compris.");
    ImGui::SameLine();
    ImGui::Checkbox("Axes des os", &axes_);
    ImGui::SameLine();
    ImGui::Checkbox("Pose de liaison", &bind_pose_);
    ImGui::SetItemTooltip("Les maillages tels qu'ils ont été liés au squelette (palettes identité), et le "
                          "squelette dans cette pose ; les clips continuent de tourner.");
    if (ImGui::Checkbox("Torche (ombres ponctuelles)", &torch_on_)) {
        if (torch_on_) {
            registry.remove<moteur::Hidden>(torch_);
        } else {
            registry.emplace<moteur::Hidden>(torch_);
        }
    }
    ImGui::SetItemTooltip("Dessiner entre les deux derniers ticks (sinon au dernier). Avec --fixed-hz 10, la "
                          "différence saute aux yeux.");
    ImGui::PushItemWidth(200.0f);
    changed_playback |= ImGui::SliderFloat("Vitesse", &speed_, 0.0f, 2.0f, "x%.2f");
    if (changed_playback) {
        apply_speed();
    }
    if (tick_seconds_ > 0.0) {
        ImGui::Text("Logique : %.0f ticks/s", 1.0 / tick_seconds_);
    }
    const moteur::AnimationStats& stats = world_.animation_stats();
    ImGui::Text("Animation : %d poses sur %d, %d palettes ; %.3f ms d'échantillonnage, %.3f ms de palettes", stats.poses,
                stats.animated, stats.palettes, stats.sample_ms, stats.palette_ms);
    if (characters_.empty()) {
        ImGui::PopItemWidth();
        return;
    }
    std::vector<const char*> names;
    for (const Character& character : characters_) {
        names.push_back(character.path.c_str() + character.path.rfind('/') + 1);
    }
    if (ImGui::Combo("Personnage", &selected_, names.data(), static_cast<int>(names.size()))) {
        joint_ = -1;
    }
    Character& character = characters_[static_cast<std::size_t>(selected_)];
    moteur::Animator& animator = registry.get<moteur::Animator>(character.entity);
    ImGui::Text("%d os, %zu parties, %zu clips, hauteur %.2f m dans le fichier", animator.skeleton->joint_count(),
                registry.get<moteur::ModelComponent>(character.entity).model->parts.size(), character.clip_names.size(),
                static_cast<double>(character.height));
    if (!character.clip_names.empty()) {
        const std::string& current = character.clip_names[static_cast<std::size_t>(character.clip)];
        if (ImGui::BeginCombo("Clip", current.c_str())) {
            for (std::size_t i = 0; i < character.clip_names.size(); ++i) {
                if (ImGui::Selectable(character.clip_names[i].c_str(), static_cast<int>(i) == character.clip)) {
                    character.clip = static_cast<int>(i);
                    restart(character);
                }
            }
            ImGui::EndCombo();
        }
        if (const moteur::Animator::Motion* motion = animator.dominant(); motion != nullptr && !motion->blend_space()) {
            // The time in ticks: moving it puts the clock there (no interpolation from where it was).
            float tick = static_cast<float>(motion->clock.cycle_time()) / static_cast<float>(moteur::ClipClock::kOne);
            const int ticks = motion->clock.cycle_ticks();
            const double seconds = static_cast<double>(motion->clip->duration());
            if (ImGui::SliderFloat("Temps (ticks)", &tick, 0.0f, static_cast<float>(ticks), "%.1f")) {
                animator.seek(tick);
            }
            ImGui::Text("Durée : %.3f s, %d ticks", seconds, ticks);
        }
    }
    // A joint: highlighted with its axes, and its weights in the view "Poids d'un os".
    const std::vector<moteur::JointData>& joints = animator.skeleton->data().joints;
    if (ImGui::BeginCombo("Os", joint_ >= 0 ? joints[static_cast<std::size_t>(joint_)].name.c_str() : "(aucun)")) {
        if (ImGui::Selectable("(aucun)", joint_ < 0)) {
            joint_ = -1;
        }
        for (std::size_t j = 0; j < joints.size(); ++j) {
            if (ImGui::Selectable(joints[j].name.c_str(), static_cast<int>(j) == joint_)) {
                joint_ = static_cast<int>(j);
            }
        }
        ImGui::EndCombo();
    }
    moteur::MeshRenderer& meshes = app_.renderer().meshes();
    bool weights = meshes.view() == moteur::MeshView::Weights;
    ImGui::SameLine();
    if (ImGui::Checkbox("Vue des poids", &weights)) {
        meshes.set_view(weights ? moteur::MeshView::Weights : moteur::MeshView::Lit);
    }
    ImGui::SetItemTooltip("Le poids de l'os choisi sur chaque sommet : bleu 0, vert 0,5, rouge 1 (même vue que "
                          "Débogage > Vue > Poids d'un os). Les autres personnages KayKit montrent le même os.");
    if (joint_ >= 0 && weight_palette_index() < 0) {
        ImGui::TextDisabled("Cet os ne déforme aucun sommet (hors de la palette du skin).");
    }
    anti_aliasing_combo(app_.renderer());
    ImGui::PopItemWidth();
    draw_walker_controls();
}

// Clips that end (deaths, hits, attacks, dodges) play once; idles, walks, runs and "-ing" clips loop.
static bool loops(const std::string& name) {
    return name == "locomotion" || name.find("Idle") != std::string::npos || name.find("Walking") != std::string::npos ||
           name.find("Running") != std::string::npos || (name.size() > 3 && name.compare(name.size() - 3, 3, "ing") == 0);
}

void AnimationTest::draw_walker_controls() {
    Walker& walker = walker_;
    if (walker.entity == entt::null) {
        return;
    }
    ImGui::SeparatorText("Mélanges et transitions (chevalier)");
    moteur::Animator& animator = world_.registry().get<moteur::Animator>(walker.entity);
    ImGui::PushItemWidth(200.0f);
    ImGui::SliderFloat("Vitesse de déplacement", &walker.target_speed, 0.0f, walker.max_speed, "%.2f m/s");
    ImGui::SameLine();
    ImGui::Checkbox("Avancer", &walker.moving);
    ImGui::SetItemTooltip("Décoché : il marche sur place, et ses pieds glissent de toute sa vitesse.");
    ImGui::SliderInt("Fondu (ticks)", &walker.fade, 0, 60);
    const std::string& current = walker.motions[static_cast<std::size_t>(walker.motion)];
    if (ImGui::BeginCombo("Mouvement", current.c_str())) {
        for (std::size_t i = 0; i < walker.motions.size(); ++i) {
            if (ImGui::Selectable(walker.motions[i].c_str(), static_cast<int>(i) == walker.motion)) {
                walker.motion = static_cast<int>(i);
                const std::string& name = walker.motions[i];
                animator.play(name, {.fade_ticks = walker.fade, .loop = loops(name), .match_speed = true});
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Fondu vers ce clip (ou le mélange « locomotion ») ; une marche ou une course seule suit "
                          "aussi la vitesse de déplacement.");
    if (!walker.attacks.empty()) {
        std::vector<const char*> attacks;
        for (const std::string& name : walker.attacks) {
            attacks.push_back(name.c_str());
        }
        ImGui::Combo("Attaque", &walker.attack, attacks.data(), static_cast<int>(attacks.size()));
        ImGui::SameLine();
        if (ImGui::Button("Frapper")) {
            animator.play(walker.attacks[static_cast<std::size_t>(walker.attack)], {.layer = 1, .loop = false});
        }
        ImGui::SetItemTooltip("Le haut du corps attaque (couche 1, masque « %s ») pendant que les jambes marchent ; "
                              "cliquer pendant l'attaque ne la relance pas.",
                              animator.set->upper_mask().c_str());
    }
    ImGui::PopItemWidth();
    bool changed = ImGui::Checkbox("Épée attachée", &walker.own_sword);
    ImGui::SetItemTooltip("sword_1handed.gltf, un fichier à part, sur le point « right_hand » de kaykit.json ; décoché : "
                          "l'épée du fichier du chevalier (elles doivent se confondre).");
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Torche", &walker.torch_on);
    ImGui::SetItemTooltip("Une torche qui éclaire, sur le point « left_hand » (à la place du bouclier).");
    ImGui::SameLine();
    ImGui::Checkbox("Repères de tête", &walker.head_markers);
    ImGui::SetItemTooltip("Jaune : sur l'os de la tête (point « head »), comme une barre de vie qui la suivrait ; "
                          "blanc : au-dessus de la boîte du personnage, immobile.");
    if (changed) {
        show_held_objects();
    }

    ImGui::Text("Déplacement : %.2f m/s ; dérive du pied au sol : %+.2f m/s", static_cast<double>(walker.speed),
                static_cast<double>(walker.slide));
    ImGui::SetItemTooltip("La vitesse du pied posé, le long du chemin (moyenne glissante) : près de 0 quand la "
                          "cadence de l'animation suit le déplacement ; décocher « Avancer » la fait tomber à "
                          "moins la vitesse.");
    for (int l = 0; l < moteur::Animator::kLayers; ++l) {
        const moteur::Animator::Layer& layer = animator.layer(l);
        if (layer.motions.empty()) {
            continue;
        }
        if (l == 0) {
            ImGui::TextUnformatted("Corps entier :");
        } else {
            ImGui::Text("Haut du corps (%.0f %%) :", layer.weight / 10.0);
        }
        for (const moteur::Animator::Motion& motion : layer.motions) {
            if (motion.blend_space()) {
                ImGui::BulletText("%s %.0f %%, phase %.2f, x%.2f", motion.name.c_str(), motion.weight / 10.0,
                                  static_cast<double>(motion.ratio(1.0f)), motion.rate / 1000.0);
            } else {
                ImGui::BulletText("%s %.0f %%, %.1f / %d ticks, x%.2f", motion.name.c_str(), motion.weight / 10.0,
                                  static_cast<double>(motion.clock.cycle_time()) / moteur::ClipClock::kOne,
                                  motion.clock.cycle_ticks(), motion.rate / 1000.0);
            }
        }
    }
    ImGui::TextDisabled("%s", walker.strides.c_str());
    ImGui::TextUnformatted(steps_.empty() ? "Événements (sons absents : python tools/audio/fetch_test_sounds.py) :"
                                          : "Événements (pas et coups sonores) :");
    for (const std::string& line : event_log_) {
        ImGui::BulletText("%s", line.c_str());
    }
}
