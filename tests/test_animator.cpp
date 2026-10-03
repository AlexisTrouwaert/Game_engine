// Skeletal clips played in ticks (milestone 5, part 4): the clock shared with the sprite
// animations, the Animator component, and the World drawing animated models between two ticks.

#include <doctest/doctest.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "moteur/animation_clock.hpp"
#include "moteur/animator.hpp"
#include "moteur/model.hpp"
#include "moteur/skeleton.hpp"
#include "moteur/world.hpp"

namespace {

const std::string kAssets = MOTEUR_SOURCE_ASSETS;

// The marks of a clock crossed by one advance, by index.
std::vector<std::size_t> advance(moteur::ClipClock& clock, int ticks, const std::vector<int>& marks) {
    std::vector<std::size_t> crossed;
    clock.advance(ticks, marks, [&crossed](std::size_t mark) { crossed.push_back(mark); });
    return crossed;
}

// The reference model (tools/models/make_skinned_reference_model.py), its skeleton and clips.
struct Reference {
    moteur::ModelData data = moteur::load_gltf(kAssets + "models/skinned_reference.glb");
    moteur::Asset<moteur::Skeleton> skeleton = moteur::make_asset(moteur::Skeleton::create(data.skeleton, "reference"));
    moteur::Asset<moteur::ClipLibrary> clips =
        moteur::make_asset(moteur::ClipLibrary::create(data.skeleton, data.clips, "reference"));

    // The model without GPU buffers: the world only reads boxes, joints and offsets.
    moteur::Asset<moteur::Model> model() const {
        moteur::Model model;
        for (const moteur::ModelPart& source : data.parts) {
            moteur::Model::Part part;
            part.mesh.bounds = source.mesh.bounds();
            part.transform = source.transform;
            part.node = source.node;
            part.skin = source.skin;
            part.joint = source.joint;
            part.joint_offset = source.joint_offset;
            model.parts.push_back(std::move(part));
        }
        model.bounds = data.bounds();
        model.joint_count = data.skeleton.joints.size();
        return moteur::make_asset(std::move(model));
    }
};

struct Recorder final : moteur::WorldSink {
    std::vector<glm::mat4> worlds;
    void mesh(const moteur::Mesh&, const glm::mat4& world, const moteur::Material&, const moteur::Aabb&) override {
        worlds.push_back(world);
    }
    void light(const moteur::PointLight&) override {}
    void billboard(const moteur::Texture&, glm::vec3, glm::vec2, const moteur::BillboardOptions&) override {}
};

bool near(const glm::vec3& value, const glm::vec3& expected) {
    return glm::all(glm::lessThan(glm::abs(value - expected), glm::vec3(1e-3f)));
}

}  // namespace

TEST_CASE("ClipClock: marks are crossed exactly once, whatever the steps") {
    const std::vector<int> marks = {0, 3, 7};  // a 10-tick loop
    moteur::ClipClock stepped(10, false);
    moteur::ClipClock jumped(10, false);
    std::vector<std::size_t> one_by_one;
    for (int tick = 0; tick < 25; ++tick) {
        for (const std::size_t mark : advance(stepped, 1, marks)) {
            one_by_one.push_back(mark);
        }
    }
    // The mark at 0 right after the start, then 3, 7, 0 (tick 10), 3, 7, 0 (tick 20), 3.
    CHECK(one_by_one == std::vector<std::size_t>{0, 1, 2, 0, 1, 2, 0, 1});
    CHECK(advance(jumped, 25, marks) == one_by_one);  // one big step: the same marks, in order
    CHECK(stepped.time() == jumped.time());
    CHECK(stepped.cycle_time() == 5 * moteur::ClipClock::kOne);
}

TEST_CASE("ClipClock: once, speed, restart and set_time") {
    moteur::ClipClock clock(4, true);
    clock.set_speed(1.5);
    CHECK(advance(clock, 2, {3}) == std::vector<std::size_t>{0});  // 2 ticks x 1.5 = 3.0: the mark at 3 is reached
    CHECK(clock.time() == 3000);
    CHECK_FALSE(clock.finished());
    CHECK(advance(clock, 10, {3}).empty());  // not twice
    CHECK(clock.finished());
    CHECK(clock.cycle_time() == 4000);  // stays at its end
    clock.restart();
    CHECK(advance(clock, 0, {0}) == std::vector<std::size_t>{0});  // the start is a mark after a restart
    clock.set_time(2500);
    CHECK(advance(clock, 1, {0, 3}) == std::vector<std::size_t>{1});
    CHECK_THROWS_AS(clock.set_speed(-1.0), std::invalid_argument);
    CHECK_THROWS_AS(moteur::ClipClock(0, false), std::invalid_argument);
}

TEST_CASE("SkeletalClip: durations in ticks") {
    const Reference reference;
    CHECK(reference.clips->clip("Bend").duration_ticks() == 60);
    CHECK(reference.clips->clip("Scale").duration_ticks() == 60);
    const std::string knight = kAssets + "models/characters/kaykit_adventurers/Knight.glb";
    if (std::filesystem::exists(knight)) {
        moteur::GltfOptions options;
        options.meshes = false;
        const moteur::ModelData data = moteur::load_gltf(knight, options);
        const moteur::ClipLibrary clips = moteur::ClipLibrary::create(data.skeleton, data.clips, "Knight");
        CHECK(clips.clip("Idle").duration_ticks() == 64);        // 1.0667 s: 32 frames at 30 per second
        CHECK(clips.clip("Death_A_Pose").duration_ticks() == 1);  // a single pose
    }
}

TEST_CASE("Animator: ticks, interpolation between them, loops and clips played once") {
    const Reference reference;
    moteur::Animator animator = moteur::Animator::create(reference.skeleton, reference.clips);
    CHECK(animator.clip_name().empty());
    CHECK(animator.ratio(0.5f) == 0.0f);
    CHECK_THROWS_WITH_AS(animator.play("Run"), "Clips 'reference': no clip 'Run'", std::runtime_error);

    entt::registry registry;
    const entt::entity entity = registry.create();
    registry.emplace<moteur::Animator>(entity, std::move(animator));
    moteur::Animator& playing = registry.get<moteur::Animator>(entity);
    playing.play("Bend");  // 60 ticks, looping
    CHECK(playing.ratio(0.7f) == 0.0f);  // just started: nothing to interpolate from
    for (int tick = 0; tick < 30; ++tick) {
        moteur::advance_animators(registry);
    }
    CHECK(playing.ratio(1.0f) == doctest::Approx(0.5f));
    CHECK(playing.ratio(0.0f) == doctest::Approx(29.0f / 60.0f));
    CHECK(playing.ratio(0.5f) == doctest::Approx(29.5f / 60.0f));

    // Across the end of the loop: forward, never back through the middle.
    for (int tick = 30; tick < 60; ++tick) {
        moteur::advance_animators(registry);
    }
    CHECK(playing.ratio(0.5f) == doctest::Approx(59.5f / 60.0f));
    moteur::advance_animators(registry);
    CHECK(playing.ratio(0.5f) == doctest::Approx(0.5f / 60.0f));

    // Once: stays at the end.
    playing.set_speed(2.0);
    playing.play("Scale", false);
    CHECK(playing.speed() == 2.0);  // kept from clip to clip
    for (int tick = 0; tick < 40; ++tick) {
        moteur::advance_animators(registry);
    }
    CHECK(playing.finished());
    CHECK(playing.ratio(0.5f) == 1.0f);
}

TEST_CASE("Animator: clips made for another skeleton are refused") {
    const Reference reference;
    moteur::SkeletonData other = reference.data.skeleton;
    other.joints[2].name = "elbow";
    const auto skeleton = moteur::make_asset(moteur::Skeleton::create(other, "other"));
    CHECK_THROWS_WITH_AS(moteur::Animator::create(skeleton, reference.clips),
                         "Animator: the clips of 'reference' do not fit the skeleton of 'other': joint 2 is 'upper' "
                         "instead of 'elbow'",
                         std::runtime_error);
}

TEST_CASE("World: an animated model is drawn in its pose between two ticks") {
    const Reference reference;
    moteur::World world;
    entt::registry& registry = world.registry();
    const entt::entity entity = registry.create();
    registry.emplace<moteur::Transform>(entity);
    registry.emplace<moteur::PreviousTransform>(entity);
    registry.emplace<moteur::ModelComponent>(entity, reference.model());
    moteur::Animator animator = moteur::Animator::create(reference.skeleton, reference.clips);
    animator.play("Bend");
    registry.emplace<moteur::Animator>(entity, std::move(animator));

    // Before any tick: the start of the clip, which is the rest pose; the marker is 0.25 m above "lower".
    Recorder start;
    world.collect(start, 1.0f);
    REQUIRE(start.worlds.size() == 2);  // the marker (under the armature, first in the scene), the column
    CHECK(near(glm::vec3(start.worlds[0][3]), {1.0f, 2.25f, 0.0f}));
    REQUIRE(registry.all_of<moteur::AnimationPose>(entity));

    for (int tick = 0; tick < 30; ++tick) {
        world.begin_tick();
        moteur::advance_animators(registry);
    }
    // Half way through Bend: "upper" has turned 45 degrees, and "lower" and the marker with it.
    const float h = std::sqrt(0.5f);
    Recorder half;
    world.collect(half, 1.0f);
    CHECK(near(glm::vec3(half.worlds[0][3]), {1.0f - 1.25f * h, 1.0f + 1.25f * h, 0.0f}));
    // The skinned column stays in its rest pose (skinning is part 5).
    CHECK(half.worlds[1] == glm::mat4(1.0f));
    // Its pose is what drawing used, for the debug tools.
    const std::vector<glm::mat4>& pose = registry.get<moteur::AnimationPose>(entity).sampler.model();
    CHECK(near(glm::vec3(pose[3][3]), {1.0f - h, 1.0f + h, 0.0f}));

    // Out of view: neither sampled nor drawn.
    moteur::CollectOptions away;
    const glm::mat4 look_away = glm::perspective(glm::radians(40.0f), 1.0f, 0.1f, 50.0f) *
                                glm::lookAt(glm::vec3(0.0f, 1.0f, 10.0f), glm::vec3(0.0f, 1.0f, 20.0f), glm::vec3(0, 1, 0));
    away.view = moteur::Frustum::from_view_projection(look_away);
    registry.remove<moteur::AnimationPose>(entity);
    Recorder hidden;
    world.collect(hidden, 1.0f, away);
    CHECK(hidden.worlds.empty());
    CHECK_FALSE(registry.all_of<moteur::AnimationPose>(entity));
}
