// Animation debug tools (milestone 5, part 9): the skeleton in debug lines, the bind pose, and what
// the Animator keeps for the debug window (its ticks, its last event).

#include <doctest/doctest.h>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <string>
#include <vector>

#include "moteur/animation_debug.hpp"
#include "moteur/animation_set.hpp"
#include "moteur/animator.hpp"
#include "moteur/debug_lines.hpp"
#include "moteur/model.hpp"
#include "moteur/skeleton.hpp"
#include "moteur/world.hpp"

namespace {

const std::string kAssets = MOTEUR_SOURCE_ASSETS;

bool identity(const glm::mat4& m) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            if (std::abs(m[c][r] - (c == r ? 1.0f : 0.0f)) > 1e-4f) {
                return false;
            }
        }
    }
    return true;
}

// Records the palettes the World gives.
struct Palettes final : moteur::WorldSink {
    std::vector<std::vector<glm::mat4>> palettes;
    void mesh(const moteur::Mesh&, const glm::mat4&, const moteur::Material&, const moteur::Aabb&) override {}
    moteur::MeshRenderer::Palette palette(const std::vector<glm::mat4>& matrices) override {
        palettes.push_back(matrices);
        return {};
    }
    void light(const moteur::PointLight&) override {}
    void billboard(const moteur::Texture&, glm::vec3, glm::vec2, const moteur::BillboardOptions&) override {}
};

}  // namespace

TEST_CASE("draw_skeleton: a segment per joint with a parent, spheres, and the axes asked for") {
    moteur::SkeletonData skeleton;
    skeleton.joints.push_back({"root", -1, {}});
    skeleton.joints.push_back({"arm", 0, {}});
    skeleton.joints.push_back({"hand", 1, {}});
    std::vector<glm::mat4> pose(3, glm::mat4(1.0f));
    pose[1][3] = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
    pose[2][3] = glm::vec4(0.0f, 2.0f, 0.0f, 1.0f);

    moteur::DebugLineBuffer plain;
    moteur::draw_skeleton(plain, glm::mat4(1.0f), skeleton, pose);
    moteur::DebugLineBuffer spheres;
    for (int j = 0; j < 3; ++j) {
        spheres.sphere(glm::vec3(pose[static_cast<std::size_t>(j)][3]), 0.015f, glm::vec4(1.0f), true);
    }
    CHECK(plain.line_count() == spheres.line_count() + 2);  // two joints have a parent

    moteur::DebugLineBuffer axes;
    moteur::SkeletonDrawOptions with_axes;
    with_axes.axis_length = 0.1f;
    moteur::draw_skeleton(axes, glm::mat4(1.0f), skeleton, pose, with_axes);
    CHECK(axes.line_count() == plain.line_count() + 3 * 3);

    moteur::DebugLineBuffer highlighted;
    moteur::SkeletonDrawOptions one;
    one.highlight = 1;  // bigger, with its own axes
    moteur::draw_skeleton(highlighted, glm::mat4(1.0f), skeleton, pose, one);
    CHECK(highlighted.line_count() == plain.line_count() + 3);
}

TEST_CASE("Bind pose: every palette is the identity, the meshes as modelled") {
    const moteur::ModelData data = moteur::load_gltf(kAssets + "models/skinned_reference.glb");
    REQUIRE_FALSE(data.skins.empty());
    const moteur::Skeleton skeleton = moteur::Skeleton::create(data.skeleton, "reference");
    moteur::PoseSampler sampler;
    sampler.bind(skeleton, data.skins);
    std::vector<glm::mat4> palette;
    for (const moteur::SkinData& skin : data.skins) {
        sampler.palette(skin, palette);
        for (const glm::mat4& m : palette) {
            CHECK(identity(m));
        }
    }

    // Through the World, with CollectOptions::bind_pose, whatever the clip plays.
    moteur::Model model;
    for (const moteur::ModelPart& source : data.parts) {
        moteur::Model::Part part;
        part.mesh.bounds = source.mesh.bounds();
        part.transform = source.transform;
        part.skin = source.skin;
        part.joint = source.joint;
        part.joint_offset = source.joint_offset;
        model.parts.push_back(std::move(part));
    }
    model.skins = data.skins;
    model.bounds = data.bounds();
    model.joint_count = data.skeleton.joints.size();
    moteur::World world;
    entt::registry& registry = world.registry();
    const entt::entity column = registry.create();
    registry.emplace<moteur::Transform>(column);
    registry.emplace<moteur::ModelComponent>(column, moteur::make_asset(std::move(model)));
    moteur::Animator animator = moteur::Animator::create(
        moteur::make_asset(moteur::Skeleton::create(data.skeleton, "reference")),
        moteur::make_asset(moteur::ClipLibrary::create(data.skeleton, data.clips, "reference")));
    animator.play("Bend", {.fade_ticks = 0});
    registry.emplace<moteur::Animator>(column, std::move(animator));
    for (int tick = 0; tick < 30; ++tick) {
        moteur::advance_animators(registry);
    }
    // The pose the World computed, which the palettes come from (the test meshes have no GPU
    // buffers, so none is given to the sink).
    const moteur::SkinData& skin = data.skins.front();
    moteur::CollectOptions options;
    options.bind_pose = true;
    Palettes sink;
    world.collect(sink, 1.0f, options);
    registry.get<moteur::AnimationPose>(column).sampler.palette(skin, palette);
    for (const glm::mat4& m : palette) {
        CHECK(identity(m));
    }
    world.collect(sink, 1.0f);
    registry.get<moteur::AnimationPose>(column).sampler.palette(skin, palette);
    bool moved = false;
    for (const glm::mat4& m : palette) {
        moved |= !identity(m);
    }
    CHECK(moved);  // half way through Bend, not bound
}

TEST_CASE("Animator: its ticks and its last event, for the debug window, even when nobody collects them") {
    moteur::SkeletonData skeleton;
    skeleton.joints.push_back({"root", -1, {}});
    moteur::ClipData clip;
    clip.name = "Loop";
    clip.duration = 0.5f;  // 30 ticks
    clip.tracks.resize(1);
    const auto set = moteur::make_asset(moteur::AnimationSet::parse(
        R"({"version": 1, "clips": {"Loop": {"events": [{"time": 0.25, "name": "half"}]}}})", "loop.json"));
    moteur::Animator animator = moteur::Animator::create(
        moteur::make_asset(moteur::Skeleton::create(skeleton, "one")),
        moteur::make_asset(moteur::ClipLibrary::create(skeleton, {clip}, "loop.glb")), set);
    animator.play("Loop");
    CHECK(animator.last_event().tick == -1);
    for (int tick = 0; tick < 50; ++tick) {
        animator.advance();  // no list of events
    }
    CHECK(animator.ticks() == 50);
    CHECK(animator.last_event().name == "half");
    CHECK(animator.last_event().clip == "Loop");
    CHECK(animator.last_event().tick == 45);  // 15, then 45
}

TEST_CASE("World: animation statistics of a collection, culled characters not sampled") {
    const moteur::ModelData data = moteur::load_gltf(kAssets + "models/skinned_reference.glb");
    moteur::World world;
    entt::registry& registry = world.registry();
    const auto skeleton = moteur::make_asset(moteur::Skeleton::create(data.skeleton, "reference"));
    const auto clips = moteur::make_asset(moteur::ClipLibrary::create(data.skeleton, data.clips, "reference"));
    moteur::Model model;
    model.bounds = data.bounds();
    model.joint_count = data.skeleton.joints.size();
    const auto shared = moteur::make_asset(std::move(model));
    for (const float x : {0.0f, 100.0f}) {  // one in view, one far away
        const entt::entity entity = registry.create();
        moteur::Transform place;
        place.position = {x, 0.0f, 0.0f};
        registry.emplace<moteur::Transform>(entity, place);
        registry.emplace<moteur::ModelComponent>(entity, shared);
        moteur::Animator animator = moteur::Animator::create(skeleton, clips);
        animator.play("Bend");
        registry.emplace<moteur::Animator>(entity, std::move(animator));
    }
    moteur::CollectOptions options;
    options.view = moteur::Frustum::from_view_projection(
        glm::perspective(glm::radians(40.0f), 1.0f, 0.1f, 50.0f) *
        glm::lookAt(glm::vec3(0.0f, 1.0f, 10.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
    Palettes sink;
    world.collect(sink, 1.0f, options);
    const moteur::AnimationStats& stats = world.animation_stats();
    CHECK(stats.animated == 2);
    CHECK(stats.poses == 1);
    CHECK(stats.sample_ms >= 0.0);
    world.collect(sink, 1.0f);  // everything in view
    CHECK(world.animation_stats().poses == 2);
}
