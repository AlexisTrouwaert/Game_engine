// Objects on joints (milestone 5, part 8): BoneAttachment, attach points of the description file,
// computed when drawing from the pose of the frame, without the joint's scale.

#include <doctest/doctest.h>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "moteur/animation_set.hpp"
#include "moteur/animator.hpp"
#include "moteur/model.hpp"
#include "moteur/paths.hpp"
#include "moteur/skeleton.hpp"
#include "moteur/world.hpp"

namespace {

const std::string kAssets = MOTEUR_SOURCE_ASSETS;

// root > arm: the root slides along x from 0 to 1 over the clip ("Slide", 1 s); the arm is 1 m
// above it. "Shrink" holds the arm at a scale of 0.01, as an FBX export does.
moteur::SkeletonData skeleton() {
    moteur::SkeletonData data;
    data.joints.push_back({"root", -1, {}});
    moteur::JointPose arm;
    arm.translation = {0.0f, 1.0f, 0.0f};
    data.joints.push_back({"arm", 0, arm});
    return data;
}

moteur::ClipData slide() {
    moteur::ClipData clip;
    clip.name = "Slide";
    clip.duration = 1.0f;
    clip.tracks.resize(2);
    clip.tracks[0].translations.times = {0.0f, 1.0f};
    clip.tracks[0].translations.values = {glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f)};
    return clip;
}

moteur::ClipData shrink() {
    moteur::ClipData clip;
    clip.name = "Shrink";
    clip.duration = 1.0f;
    clip.tracks.resize(2);
    clip.tracks[1].scales.times = {0.0f, 1.0f};
    clip.tracks[1].scales.values = {glm::vec3(0.01f), glm::vec3(0.01f)};
    return clip;
}

struct Rig {
    moteur::SkeletonData data = skeleton();
    moteur::Asset<moteur::Skeleton> skeleton_asset = moteur::make_asset(moteur::Skeleton::create(data, "rig"));
    moteur::Asset<moteur::ClipLibrary> clips = moteur::make_asset(moteur::ClipLibrary::create(data, {slide(), shrink()}, "rig.glb"));
    moteur::Asset<moteur::AnimationSet> set = moteur::make_asset(moteur::AnimationSet::parse(
        R"({"version": 1, "attach_points": {"hand": {"joint": "arm", "position": [0, 0.5, 0], "rotation": [0, 90, 0]}}})",
        "rig.json"));
};

struct Recorder final : moteur::WorldSink {
    std::vector<glm::mat4> meshes;
    std::vector<glm::vec3> lights;
    void mesh(const moteur::Mesh&, const glm::mat4& world, const moteur::Material&, const moteur::Aabb&) override {
        meshes.push_back(world);
    }
    void light(const moteur::PointLight& light) override { lights.push_back(light.position); }
    void billboard(const moteur::Texture&, glm::vec3, glm::vec2, const moteur::BillboardOptions&) override {}
};

// ozz quantizes rotations (about 1e-4): millimetres.
bool near(const glm::vec3& value, const glm::vec3& expected, float tolerance = 1e-3f) {
    return glm::all(glm::lessThan(glm::abs(value - expected), glm::vec3(tolerance)));
}

// An owner at (10, 0, 0) scaled x2 playing `clip`, and a mesh on `point` of it.
struct Scene {
    moteur::World world;
    entt::entity owner;
    entt::entity sword;

    Scene(const Rig& rig, const char* clip, const std::string& point) {
        entt::registry& registry = world.registry();
        owner = registry.create();
        moteur::Transform place;
        place.position = {10.0f, 0.0f, 0.0f};
        place.scale = glm::vec3(2.0f);
        registry.emplace<moteur::Transform>(owner, place);
        moteur::Animator animator = moteur::Animator::create(rig.skeleton_asset, rig.clips, rig.set);
        animator.play(clip, {.fade_ticks = 0});
        registry.emplace<moteur::Animator>(owner, std::move(animator));
        sword = registry.create();
        registry.emplace<moteur::Transform>(sword);
        registry.emplace<moteur::Parent>(sword, owner);
        registry.emplace<moteur::BoneAttachment>(sword, point);
        registry.emplace<moteur::MeshComponent>(sword, moteur::make_asset(moteur::Mesh{}), moteur::Material{});
    }
};

}  // namespace

TEST_CASE("AnimationSet: attach points") {
    const Rig rig;
    const moteur::AttachPoint* hand = rig.set->attach_point("hand");
    REQUIRE(hand != nullptr);
    CHECK(hand->joint == "arm");
    CHECK(near(glm::vec3(hand->offset() * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)), {0.0f, 0.0f, -1.0f}));  // 90 degrees about Y
    CHECK(rig.set->attach_point("foot") == nullptr);
    CHECK_THROWS_WITH_AS(moteur::AnimationSet::parse(R"({"version": 1, "attach_points": {"p": {"joint": "a", "position": [1]}}})", "x"),
                         doctest::Contains("attach point 'p': position needs 3 numbers"), std::runtime_error);
}

TEST_CASE("BoneAttachment: on the joint of the pose drawn, between two ticks") {
    const Rig rig;
    Scene scene(rig, "Slide", "arm");
    for (int tick = 0; tick < 30; ++tick) {
        scene.world.begin_tick();
        moteur::advance_animators(scene.world.registry());
    }
    // Half way between the ticks 29 and 30: the root at 29.5 / 60 m, the arm 1 m above; x2, at 10.
    Recorder frame;
    scene.world.collect(frame, 0.5f);
    REQUIRE(frame.meshes.size() == 1);
    CHECK(near(glm::vec3(frame.meshes[0][3]), {10.0f + 2.0f * 29.5f / 60.0f, 2.0f, 0.0f}));
    // The entity's own Transform is relative to the joint.
    scene.world.registry().patch<moteur::Transform>(scene.sword, [](moteur::Transform& t) { t.position = {0.0f, 0.25f, 0.0f}; });
    Recorder offset;
    scene.world.collect(offset, 0.5f);
    CHECK(near(glm::vec3(offset.meshes[0][3]), {10.0f + 2.0f * 29.5f / 60.0f, 2.5f, 0.0f}));
    // world_matrix() (tools) gives the pose last drawn.
    CHECK(near(scene.world.world_position(scene.sword, 0.5f), glm::vec3(offset.meshes[0][3])));
}

TEST_CASE("BoneAttachment: a named point adds its offset; an unknown one leaves the object at the parent") {
    const Rig rig;
    Scene named(rig, "Slide", "hand");
    Recorder frame;
    named.world.collect(frame, 1.0f);
    // At the start: the arm at (0, 1, 0), the point 0.5 m above it and turned 90 degrees; x2, at 10.
    REQUIRE(frame.meshes.size() == 1);
    CHECK(near(glm::vec3(frame.meshes[0][3]), {10.0f, 3.0f, 0.0f}));
    CHECK(near(glm::vec3(frame.meshes[0] * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)), {0.0f, 0.0f, -2.0f}));

    Scene unknown(rig, "Slide", "tail");
    Recorder lost;
    unknown.world.collect(lost, 1.0f);
    REQUIRE(lost.meshes.size() == 1);
    CHECK(near(glm::vec3(lost.meshes[0][3]), {10.0f, 0.0f, 0.0f}));
}

TEST_CASE("BoneAttachment: the joint's scale is not passed on, the parent's is") {
    const Rig rig;
    Scene scene(rig, "Shrink", "arm");  // the arm at a scale of 0.01
    Recorder frame;
    scene.world.collect(frame, 1.0f);
    REQUIRE(frame.meshes.size() == 1);
    for (int c = 0; c < 3; ++c) {
        CHECK(glm::length(glm::vec3(frame.meshes[0][c])) == doctest::Approx(2.0f));  // the owner's x2 only
    }
}

TEST_CASE("BoneAttachment: its parent's pose is sampled even out of view; a light follows the hand") {
    const Rig rig;
    Scene scene(rig, "Slide", "arm");
    entt::registry& registry = scene.world.registry();
    const entt::entity torch = registry.create();
    registry.emplace<moteur::Transform>(torch);
    registry.emplace<moteur::Parent>(torch, scene.owner);
    registry.emplace<moteur::BoneAttachment>(torch, "hand");
    registry.emplace<moteur::LightSource>(torch);
    for (int tick = 0; tick < 30; ++tick) {
        scene.world.begin_tick();
        moteur::advance_animators(registry);
    }
    moteur::CollectOptions away;
    away.view = moteur::Frustum::from_view_projection(
        glm::perspective(glm::radians(40.0f), 1.0f, 0.1f, 50.0f) *
        glm::lookAt(glm::vec3(0.0f, 1.0f, -10.0f), glm::vec3(0.0f, 1.0f, -20.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
    Recorder frame;
    scene.world.collect(frame, 1.0f, away);
    REQUIRE(frame.meshes.size() == 1);  // the renderer culls it; the world gives it, in its place
    CHECK(near(glm::vec3(frame.meshes[0][3]), {11.0f, 2.0f, 0.0f}));  // half the clip: root at 0.5 m
    REQUIRE(frame.lights.size() == 1);
    CHECK(near(frame.lights[0], {11.0f, 3.0f, 0.0f}));
    // Destroying the owner takes what it holds.
    scene.world.destroy(scene.owner);
    CHECK_FALSE(registry.valid(torch));
    CHECK_FALSE(registry.valid(scene.sword));
}

TEST_CASE("KayKit: a sword on the knight's right_hand point falls exactly on the sword of the file") {
    const std::string path = kAssets + "models/characters/kaykit_adventurers/Knight.glb";
    if (!std::filesystem::exists(path)) {
        return;
    }
    const moteur::ModelData data = moteur::load_gltf(path);  // with its parts: the sword of the file
    const moteur::FileData file = moteur::read_file(kAssets + "animations/kaykit.json");
    entt::registry registry;
    const entt::entity knight = registry.create();
    registry.emplace<moteur::Animator>(
        knight, moteur::Animator::create(
                    moteur::make_asset(moteur::Skeleton::create(data.skeleton, "Knight")),
                    moteur::make_asset(moteur::ClipLibrary::create(data.skeleton, data.clips, "Knight")),
                    moteur::make_asset(moteur::AnimationSet::parse({static_cast<const char*>(file.data()), file.size()}, "kaykit.json"))));
    const moteur::Animator& animator = registry.get<moteur::Animator>(knight);

    const moteur::ModelPart* sword = nullptr;
    for (const moteur::ModelPart& part : data.parts) {
        if (part.joint >= 0 && part.node == "1H_Sword") {
            sword = &part;
        }
    }
    REQUIRE(sword != nullptr);
    for (const char* clip : {"Idle", "1H_Melee_Attack_Chop", "Running_A"}) {
        std::vector<glm::mat4> pose;
        moteur::sample_model_pose(*animator.skeleton, animator.clips->clip(clip), 0.4f, pose);
        const glm::mat4 in_file = pose[static_cast<std::size_t>(sword->joint)] * sword->joint_offset;
        glm::mat4 attached;
        REQUIRE(moteur::attach_point_matrix(registry, knight, "right_hand", &pose, attached));
        CAPTURE(clip);
        for (int c = 0; c < 4; ++c) {
            CHECK(near(glm::vec3(attached[c]), glm::vec3(in_file[c]), 1e-3f));
        }
    }
}
