// Checks that ozz-animation is built and linked the way the engine needs it (milestone 5, part 2):
// a skeleton and an animation built at run time from "raw" data (what the glTF loader will fill),
// sampled, blended and brought to model space.

#include <doctest/doctest.h>

#include <cmath>
#include <string>

#include "ozz/animation/offline/animation_builder.h"
#include "ozz/animation/offline/raw_animation.h"
#include "ozz/animation/offline/raw_skeleton.h"
#include "ozz/animation/offline/skeleton_builder.h"
#include "ozz/animation/runtime/animation.h"
#include "ozz/animation/runtime/blending_job.h"
#include "ozz/animation/runtime/local_to_model_job.h"
#include "ozz/animation/runtime/sampling_job.h"
#include "ozz/animation/runtime/skeleton.h"
#include "ozz/base/containers/vector.h"
#include "ozz/base/maths/math_constant.h"
#include "ozz/base/maths/simd_math.h"
#include "ozz/base/maths/soa_transform.h"
#include "ozz/base/memory/unique_ptr.h"

namespace {

namespace offline = ozz::animation::offline;

// Two joints: "root" at the origin, "tip" 1 m above it.
ozz::unique_ptr<ozz::animation::Skeleton> make_skeleton() {
    offline::RawSkeleton raw;
    raw.roots.resize(1);
    offline::RawSkeleton::Joint& root = raw.roots[0];
    root.name = "root";
    root.transform = ozz::math::Transform::identity();
    root.children.resize(1);
    offline::RawSkeleton::Joint& tip = root.children[0];
    tip.name = "tip";
    tip.transform = ozz::math::Transform::identity();
    tip.transform.translation = ozz::math::Float3(0.0f, 1.0f, 0.0f);
    REQUIRE(raw.Validate());
    offline::SkeletonBuilder build;
    return build(raw);
}

// One second; the root turns from 0 to `angle` radians around Z.
ozz::unique_ptr<ozz::animation::Animation> make_turn(const ozz::animation::Skeleton& skeleton, float angle) {
    offline::RawAnimation raw;
    raw.duration = 1.0f;
    raw.tracks.resize(static_cast<std::size_t>(skeleton.num_joints()));
    raw.tracks[0].rotations.push_back({0.0f, ozz::math::Quaternion::identity()});
    raw.tracks[0].rotations.push_back({1.0f, ozz::math::Quaternion::FromAxisAngle(ozz::math::Float3::z_axis(), angle)});
    raw.tracks[1].translations.push_back({0.0f, ozz::math::Float3(0.0f, 1.0f, 0.0f)});
    REQUIRE(raw.Validate());
    offline::AnimationBuilder build;
    return build(raw);
}

struct Pose {
    ozz::vector<ozz::math::SoaTransform> locals;
    ozz::vector<ozz::math::Float4x4> models;
};

void sample(const ozz::animation::Skeleton& skeleton, const ozz::animation::Animation& animation, float ratio,
            ozz::vector<ozz::math::SoaTransform>& locals) {
    ozz::animation::SamplingJob::Context context(skeleton.num_joints());
    ozz::animation::SamplingJob job;
    job.animation = &animation;
    job.context = &context;
    job.ratio = ratio;
    job.output = ozz::make_span(locals);
    REQUIRE(job.Run());
}

void to_model(const ozz::animation::Skeleton& skeleton, Pose& pose) {
    ozz::animation::LocalToModelJob job;
    job.skeleton = &skeleton;
    job.input = ozz::make_span(pose.locals);
    job.output = ozz::make_span(pose.models);
    REQUIRE(job.Run());
}

// ozz stores rotations quantized on 16 bits: poses are exact to about 1e-4, so positions are
// compared with an absolute tolerance (doctest's Approx is relative, useless around 0).
bool near(float value, float expected) {
    return std::fabs(value - expected) < 1e-3f;
}

// Where a joint is in model space.
ozz::math::Float3 position(const Pose& pose, int joint) {
    float values[4];
    ozz::math::StorePtrU(pose.models[static_cast<std::size_t>(joint)].cols[3], values);
    return {values[0], values[1], values[2]};
}

}  // namespace

TEST_CASE("ozz-animation samples a clip built at run time") {
    const auto skeleton = make_skeleton();
    REQUIRE(skeleton);
    CHECK(skeleton->num_joints() == 2);
    CHECK(std::string(skeleton->joint_names()[1]) == "tip");
    CHECK(skeleton->joint_parents()[1] == 0);

    const auto turn = make_turn(*skeleton, ozz::math::kPi_2);
    REQUIRE(turn);
    CHECK(turn->duration() == doctest::Approx(1.0f));

    Pose pose;
    pose.locals.resize(static_cast<std::size_t>(skeleton->num_soa_joints()));
    pose.models.resize(static_cast<std::size_t>(skeleton->num_joints()));

    sample(*skeleton, *turn, 0.0f, pose.locals);
    to_model(*skeleton, pose);
    CHECK(near(position(pose, 1).x, 0.0f));
    CHECK(near(position(pose, 1).y, 1.0f));

    // Half way: 45 degrees counter-clockwise around Z, so the tip moves towards -X.
    sample(*skeleton, *turn, 0.5f, pose.locals);
    to_model(*skeleton, pose);
    const float half = std::sqrt(0.5f);
    CHECK(near(position(pose, 1).x, -half));
    CHECK(near(position(pose, 1).y, half));

    sample(*skeleton, *turn, 1.0f, pose.locals);
    to_model(*skeleton, pose);
    CHECK(near(position(pose, 1).x, -1.0f));
    CHECK(near(position(pose, 1).y, 0.0f));
}

TEST_CASE("ozz-animation blends two sampled clips") {
    const auto skeleton = make_skeleton();
    const auto left = make_turn(*skeleton, ozz::math::kPi_2);
    const auto right = make_turn(*skeleton, -ozz::math::kPi_2);

    const std::size_t soa = static_cast<std::size_t>(skeleton->num_soa_joints());
    ozz::vector<ozz::math::SoaTransform> a(soa);
    ozz::vector<ozz::math::SoaTransform> b(soa);
    sample(*skeleton, *left, 1.0f, a);
    sample(*skeleton, *right, 1.0f, b);

    ozz::animation::BlendingJob::Layer layers[2];
    layers[0].weight = 0.5f;
    layers[0].transform = ozz::make_span(a);
    layers[1].weight = 0.5f;
    layers[1].transform = ozz::make_span(b);

    Pose pose;
    pose.locals.resize(soa);
    pose.models.resize(static_cast<std::size_t>(skeleton->num_joints()));
    ozz::animation::BlendingJob blend;
    blend.layers = layers;
    blend.rest_pose = skeleton->joint_rest_poses();
    blend.output = ozz::make_span(pose.locals);
    REQUIRE(blend.Run());
    to_model(*skeleton, pose);

    // +90 and -90 degrees, half each: the tip is back straight up.
    CHECK(near(position(pose, 1).x, 0.0f));
    CHECK(near(position(pose, 1).y, 1.0f));
}
