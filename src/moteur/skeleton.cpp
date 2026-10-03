#include "moteur/skeleton.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

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
#include "ozz/base/maths/simd_math.h"
#include "ozz/base/maths/soa_transform.h"
#include "ozz/base/memory/unique_ptr.h"

namespace moteur {

namespace {

namespace offline = ozz::animation::offline;

ozz::math::Float3 to_ozz(const glm::vec3& v) {
    return {v.x, v.y, v.z};
}

ozz::math::Quaternion to_ozz(const glm::quat& q) {
    return {q.x, q.y, q.z, q.w};
}

ozz::math::Transform to_ozz(const JointPose& pose) {
    ozz::math::Transform transform;
    transform.translation = to_ozz(pose.translation);
    transform.rotation = to_ozz(pose.rotation);
    transform.scale = to_ozz(pose.scale);
    return transform;
}

// The joint `index` of `data` and its descendants, in the form ozz builds skeletons from.
offline::RawSkeleton::Joint raw_joint(const SkeletonData& data, int index) {
    offline::RawSkeleton::Joint joint;
    const JointData& source = data.joints[static_cast<std::size_t>(index)];
    joint.name = source.name.c_str();
    joint.transform = to_ozz(source.rest);
    for (std::size_t child = static_cast<std::size_t>(index) + 1; child < data.joints.size(); ++child) {
        if (data.joints[child].parent == index) {
            joint.children.push_back(raw_joint(data, static_cast<int>(child)));
        }
    }
    return joint;
}

}  // namespace

struct Skeleton::Impl {
    std::string name;
    SkeletonData data;
    ozz::unique_ptr<ozz::animation::Skeleton> skeleton;
};

Skeleton::Skeleton() = default;
Skeleton::~Skeleton() = default;
Skeleton::Skeleton(Skeleton&&) noexcept = default;
Skeleton& Skeleton::operator=(Skeleton&&) noexcept = default;

Skeleton Skeleton::create(const SkeletonData& data, const std::string& name) {
    if (data.joints.empty()) {
        throw std::runtime_error("Skeleton '" + name + "': no joint");
    }
    if (data.joints.size() > static_cast<std::size_t>(ozz::animation::Skeleton::kMaxJoints)) {
        throw std::runtime_error("Skeleton '" + name + "': " + std::to_string(data.joints.size()) + " joints (" +
                                 std::to_string(ozz::animation::Skeleton::kMaxJoints) + " at most)");
    }
    offline::RawSkeleton raw;
    for (std::size_t i = 0; i < data.joints.size(); ++i) {
        if (data.joints[i].parent < 0) {
            raw.roots.push_back(raw_joint(data, static_cast<int>(i)));
        }
    }
    offline::SkeletonBuilder build;
    Skeleton skeleton;
    skeleton.impl_ = std::make_unique<Impl>();
    skeleton.impl_->name = name;
    skeleton.impl_->data = data;
    skeleton.impl_->skeleton = build(raw);
    if (!skeleton.impl_->skeleton) {
        throw std::runtime_error("Skeleton '" + name + "': ozz refused it");
    }
    // ozz numbers joints depth first, children in order: the order of SkeletonData. Checked, since
    // every track and every skin relies on it.
    const auto parents = skeleton.impl_->skeleton->joint_parents();
    for (std::size_t i = 0; i < data.joints.size(); ++i) {
        const int parent = parents[i] == ozz::animation::Skeleton::kNoParent ? -1 : parents[i];
        if (parent != data.joints[i].parent) {
            throw std::logic_error("Skeleton '" + name + "': ozz ordered the joints differently");
        }
    }
    return skeleton;
}

const std::string& Skeleton::name() const {
    return impl_->name;
}

const SkeletonData& Skeleton::data() const {
    return impl_->data;
}

int Skeleton::joint_count() const {
    return impl_ ? static_cast<int>(impl_->data.joints.size()) : 0;
}

std::size_t Skeleton::bytes() const {
    if (!impl_) {
        return 0;
    }
    std::size_t bytes = static_cast<std::size_t>(impl_->skeleton->num_soa_joints()) * sizeof(ozz::math::SoaTransform) +
                        impl_->data.joints.size() * (sizeof(JointData) + sizeof(std::int16_t));
    for (const JointData& joint : impl_->data.joints) {
        bytes += joint.name.size() * 2;  // ours and ozz's
    }
    return bytes;
}

void Skeleton::replace_in_place(Skeleton&& fresh) {
    if (const std::string why = impl_->data.mismatch(fresh.impl_->data); !why.empty()) {
        throw std::runtime_error("its joints changed (" + why + "): reload the scene");
    }
    impl_ = std::move(fresh.impl_);
}

struct SkeletalClip::Impl {
    std::string name;
    ozz::unique_ptr<ozz::animation::Animation> animation;
};

SkeletalClip::SkeletalClip() = default;
SkeletalClip::~SkeletalClip() = default;
SkeletalClip::SkeletalClip(SkeletalClip&&) noexcept = default;
SkeletalClip& SkeletalClip::operator=(SkeletalClip&&) noexcept = default;

const std::string& SkeletalClip::name() const {
    return impl_->name;
}

float SkeletalClip::duration() const {
    return impl_->animation->duration();
}

int SkeletalClip::duration_ticks() const {
    return std::max(1, static_cast<int>(std::lround(duration() * static_cast<float>(kClipTicksPerSecond))));
}

std::size_t SkeletalClip::bytes() const {
    return impl_ ? impl_->animation->size() : 0;
}

ClipLibrary ClipLibrary::create(const SkeletonData& skeleton, const std::vector<ClipData>& clips, const std::string& source) {
    ClipLibrary library;
    library.source_ = source;
    library.skeleton_ = skeleton;
    for (const ClipData& data : clips) {
        const auto fail = [&](const std::string& why) {
            return std::runtime_error("Clips '" + source + "': clip '" + data.name + "': " + why);
        };
        if (data.tracks.size() != skeleton.joints.size()) {
            throw fail(std::to_string(data.tracks.size()) + " tracks for " + std::to_string(skeleton.joints.size()) + " joints");
        }
        offline::RawAnimation raw;
        raw.name = data.name.c_str();
        raw.duration = data.duration;
        raw.tracks.resize(data.tracks.size());
        for (std::size_t j = 0; j < data.tracks.size(); ++j) {
            const JointTrack& track = data.tracks[j];
            const JointPose& rest = skeleton.joints[j].rest;
            offline::RawAnimation::JointTrack& out = raw.tracks[j];
            // A property without keys keeps its rest value (ozz would take the identity).
            if (track.translations.empty()) {
                out.translations.push_back({0.0f, to_ozz(rest.translation)});
            }
            for (std::size_t k = 0; k < track.translations.times.size(); ++k) {
                out.translations.push_back({track.translations.times[k], to_ozz(track.translations.values[k])});
            }
            if (track.rotations.empty()) {
                out.rotations.push_back({0.0f, to_ozz(rest.rotation)});
            }
            for (std::size_t k = 0; k < track.rotations.times.size(); ++k) {
                out.rotations.push_back({track.rotations.times[k], to_ozz(track.rotations.values[k])});
            }
            if (track.scales.empty()) {
                out.scales.push_back({0.0f, to_ozz(rest.scale)});
            }
            for (std::size_t k = 0; k < track.scales.times.size(); ++k) {
                out.scales.push_back({track.scales.times[k], to_ozz(track.scales.values[k])});
            }
        }
        if (!raw.Validate()) {
            throw fail("invalid keys (duration " + std::to_string(data.duration) + " s)");
        }
        offline::AnimationBuilder build;
        SkeletalClip clip;
        clip.impl_ = std::make_unique<SkeletalClip::Impl>();
        clip.impl_->name = data.name;
        clip.impl_->animation = build(raw);
        if (!clip.impl_->animation) {
            throw fail("ozz refused it");
        }
        if (!library.clips_.emplace(data.name, std::move(clip)).second) {
            throw fail("two clips have this name");
        }
    }
    return library;
}

const SkeletalClip& ClipLibrary::clip(const std::string& name) const {
    const auto found = clips_.find(name);
    if (found == clips_.end()) {
        throw std::runtime_error("Clips '" + source_ + "': no clip '" + name + "'");
    }
    return found->second;
}

std::vector<std::string> ClipLibrary::names() const {
    std::vector<std::string> result;
    for (const auto& [name, clip] : clips_) {
        result.push_back(name);
    }
    return result;
}

std::size_t ClipLibrary::bytes() const {
    std::size_t bytes = 0;
    for (const auto& [name, clip] : clips_) {
        bytes += clip.bytes();
    }
    return bytes;
}

std::string ClipLibrary::mismatch(const Skeleton& skeleton) const {
    return skeleton_.mismatch(skeleton.data());
}

void ClipLibrary::replace_in_place(ClipLibrary&& fresh) {
    if (const std::string why = skeleton_.mismatch(fresh.skeleton_); !why.empty()) {
        throw std::runtime_error("its skeleton changed (" + why + "): reload the scene");
    }
    if (names() != fresh.names()) {
        throw std::runtime_error("its clips changed (added, removed or renamed): reload the scene");
    }
    for (auto& [name, clip] : clips_) {
        clip = std::move(fresh.clips_.at(name));  // same object, new content
    }
    skeleton_ = std::move(fresh.skeleton_);  // rest poses may have changed
}

struct PoseSampler::Impl {
    ozz::animation::SamplingJob::Context context;
    ozz::vector<ozz::math::SoaTransform> locals;
    ozz::vector<ozz::math::Float4x4> matrices;
    // Blending: one sampling cache and one local pose per input (the same clip tends to stay in the
    // same slot from frame to frame, which keeps its cache warm), and the joint weights in SoA.
    std::vector<std::unique_ptr<ozz::animation::SamplingJob::Context>> contexts;
    std::vector<ozz::vector<ozz::math::SoaTransform>> layer_locals;
    std::vector<ozz::vector<ozz::math::SimdFloat4>> layer_weights;
    std::vector<ozz::animation::BlendingJob::Layer> layers;
};

PoseSampler::PoseSampler() : impl_(std::make_unique<Impl>()) {}
PoseSampler::~PoseSampler() = default;
PoseSampler::PoseSampler(PoseSampler&&) noexcept = default;
PoseSampler& PoseSampler::operator=(PoseSampler&&) noexcept = default;

namespace {

// Local poses to model-space matrices, then into glm.
void to_model_space(const ozz::animation::Skeleton& rig, ozz::vector<ozz::math::SoaTransform>& locals,
                    ozz::vector<ozz::math::Float4x4>& matrices, std::vector<glm::mat4>& model) {
    matrices.resize(static_cast<std::size_t>(rig.num_joints()));
    ozz::animation::LocalToModelJob to_model;
    to_model.skeleton = &rig;
    to_model.input = ozz::make_span(locals);
    to_model.output = ozz::make_span(matrices);
    if (!to_model.Run()) {
        throw std::logic_error("local to model space failed");
    }
    model.resize(matrices.size());
    for (std::size_t i = 0; i < matrices.size(); ++i) {
        for (int c = 0; c < 4; ++c) {
            ozz::math::StorePtrU(matrices[i].cols[c], &model[i][c][0]);
        }
    }
}

}  // namespace

namespace {

// `clip` at `ratio` into `locals`, as local transforms.
void sample_locals(const Skeleton& skeleton, const ozz::animation::Skeleton& rig, const SkeletalClip& clip,
                   const ozz::animation::Animation& animation, float ratio,
                   ozz::animation::SamplingJob::Context& context, ozz::vector<ozz::math::SoaTransform>& locals) {
    if (animation.num_tracks() != rig.num_joints()) {
        throw std::invalid_argument("Clip '" + clip.name() + "' has " + std::to_string(animation.num_tracks()) +
                                    " tracks, skeleton '" + skeleton.name() + "' " + std::to_string(rig.num_joints()) +
                                    " joints");
    }
    if (context.max_tracks() < rig.num_joints()) {
        context.Resize(rig.num_joints());
    }
    locals.resize(static_cast<std::size_t>(rig.num_soa_joints()));
    ozz::animation::SamplingJob sampling;
    sampling.animation = &animation;
    sampling.context = &context;
    sampling.ratio = std::clamp(ratio, 0.0f, 1.0f);
    sampling.output = ozz::make_span(locals);
    if (!sampling.Run()) {
        throw std::logic_error("Clip '" + clip.name() + "': sampling failed");
    }
}

}  // namespace

void PoseSampler::sample(const Skeleton& skeleton, const SkeletalClip& clip, float ratio) {
    sample_locals(skeleton, *skeleton.impl_->skeleton, clip, *clip.impl_->animation, ratio, impl_->context, impl_->locals);
    to_model_space(*skeleton.impl_->skeleton, impl_->locals, impl_->matrices, model_);
}

void PoseSampler::blend(const Skeleton& skeleton, std::span<const PoseInput> inputs) {
    const ozz::animation::Skeleton& rig = *skeleton.impl_->skeleton;
    const auto joints = static_cast<std::size_t>(rig.num_joints());
    if (inputs.empty()) {
        rest(skeleton);
        return;
    }
    if (inputs.size() == 1 && inputs[0].joint_weights.empty() && inputs[0].clip != nullptr) {
        sample(skeleton, *inputs[0].clip, inputs[0].ratio);
        return;
    }
    const auto soa_joints = static_cast<std::size_t>(rig.num_soa_joints());
    while (impl_->contexts.size() < inputs.size()) {
        impl_->contexts.push_back(std::make_unique<ozz::animation::SamplingJob::Context>());
    }
    impl_->layer_locals.resize(std::max(impl_->layer_locals.size(), inputs.size()));
    impl_->layer_weights.resize(std::max(impl_->layer_weights.size(), inputs.size()));
    impl_->layers.clear();
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        const PoseInput& input = inputs[i];
        if (input.clip == nullptr) {
            throw std::invalid_argument("PoseSampler::blend: an input has no clip");
        }
        sample_locals(skeleton, rig, *input.clip, *input.clip->impl_->animation, input.ratio, *impl_->contexts[i],
                      impl_->layer_locals[i]);
        ozz::animation::BlendingJob::Layer layer;
        layer.weight = input.weight;
        layer.transform = ozz::make_span(impl_->layer_locals[i]);
        if (!input.joint_weights.empty()) {
            if (input.joint_weights.size() != joints) {
                throw std::invalid_argument("PoseSampler::blend: " + std::to_string(input.joint_weights.size()) +
                                            " joint weights for " + std::to_string(joints) + " joints");
            }
            // Four joints per SIMD value, as the local transforms.
            ozz::vector<ozz::math::SimdFloat4>& packed = impl_->layer_weights[i];
            packed.resize(soa_joints);
            for (std::size_t s = 0; s < soa_joints; ++s) {
                float lanes[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                for (std::size_t lane = 0; lane < 4 && s * 4 + lane < joints; ++lane) {
                    lanes[lane] = input.joint_weights[s * 4 + lane];
                }
                packed[s] = ozz::math::simd_float4::LoadPtrU(lanes);
            }
            layer.joint_weights = ozz::make_span(packed);
        }
        impl_->layers.push_back(layer);
    }
    impl_->locals.resize(soa_joints);
    ozz::animation::BlendingJob blending;
    blending.threshold = 0.01f;
    blending.layers = ozz::make_span(impl_->layers);
    blending.rest_pose = rig.joint_rest_poses();
    blending.output = ozz::make_span(impl_->locals);
    if (!blending.Run()) {
        throw std::logic_error("PoseSampler::blend: blending failed");
    }
    to_model_space(rig, impl_->locals, impl_->matrices, model_);
}

void PoseSampler::rest(const Skeleton& skeleton) {
    const ozz::animation::Skeleton& rig = *skeleton.impl_->skeleton;
    const auto rest = rig.joint_rest_poses();
    impl_->locals.assign(rest.begin(), rest.end());
    to_model_space(rig, impl_->locals, impl_->matrices, model_);
}

void PoseSampler::bind(const Skeleton& skeleton, const std::vector<SkinData>& skins) {
    rest(skeleton);
    for (const SkinData& skin : skins) {
        for (std::size_t i = 0; i < skin.joints.size() && i < skin.inverse_binds.size(); ++i) {
            const auto joint = static_cast<std::size_t>(skin.joints[i]);
            if (joint < model_.size()) {
                model_[joint] = glm::inverse(skin.inverse_binds[i]);
            }
        }
    }
}

void PoseSampler::palette(const SkinData& skin, std::vector<glm::mat4>& out) const {
    out.resize(skin.joints.size());
    for (std::size_t i = 0; i < skin.joints.size(); ++i) {
        const auto joint = static_cast<std::size_t>(skin.joints[i]);
        out[i] = joint < model_.size() ? model_[joint] * skin.inverse_binds[i] : skin.inverse_binds[i];
    }
}

glm::vec3 skin_position(const std::vector<glm::mat4>& palette, const VertexSkin& skin, glm::vec3 position) {
    glm::mat4 blended(0.0f);
    for (int k = 0; k < 4; ++k) {
        blended += palette[skin.joints[k]] * (static_cast<float>(skin.weights[k]) / 65535.0f);
    }
    return glm::vec3(blended * glm::vec4(position, 1.0f));
}

Stride measure_stride(const Skeleton& skeleton, const SkeletalClip& clip, const std::vector<int>& feet, glm::vec3 forward) {
    Stride stride;
    const float duration = clip.duration();
    if (feet.empty() || duration <= 0.0f) {
        return stride;
    }
    forward = glm::normalize(forward);
    // Twice the file's 30 or 60 samples per second: every key, and the moments between.
    const int samples = std::max(32, static_cast<int>(std::ceil(duration * 120.0f)));
    const float step = duration / static_cast<float>(samples);
    std::vector<std::vector<glm::vec3>> tracks(feet.size(), std::vector<glm::vec3>(static_cast<std::size_t>(samples)));
    PoseSampler sampler;
    for (int i = 0; i < samples; ++i) {
        sampler.sample(skeleton, clip, static_cast<float>(i) / static_cast<float>(samples));
        for (std::size_t f = 0; f < feet.size(); ++f) {
            const auto joint = static_cast<std::size_t>(feet[f]);
            if (joint >= sampler.model().size()) {
                throw std::invalid_argument("measure_stride: no joint " + std::to_string(feet[f]));
            }
            tracks[f][static_cast<std::size_t>(i)] = glm::vec3(sampler.model()[joint][3]);
        }
    }
    // On the ground: within a tenth of the foot's rise above its lowest point. There, the speed
    // backwards along `forward` (central differences, around the loop).
    double sum = 0.0;
    int count = 0;
    for (const std::vector<glm::vec3>& track : tracks) {
        float low = track[0].y;
        float high = track[0].y;
        for (const glm::vec3& p : track) {
            low = std::min(low, p.y);
            high = std::max(high, p.y);
        }
        const float contact = low + 0.1f * (high - low);
        const auto on_ground = [&](int i) { return track[static_cast<std::size_t>((i + samples) % samples)].y <= contact; };
        float touch_down = 0.0f;
        for (int i = 0; i < samples; ++i) {
            if (on_ground(i) && !on_ground(i - 1)) {
                touch_down = static_cast<float>(i) / static_cast<float>(samples);
            }
            if (!on_ground(i)) {
                continue;
            }
            const glm::vec3 next = track[static_cast<std::size_t>((i + 1) % samples)];
            const glm::vec3 previous = track[static_cast<std::size_t>((i + samples - 1) % samples)];
            sum += -static_cast<double>(glm::dot(next - previous, forward)) / (2.0 * static_cast<double>(step));
            ++count;
        }
        stride.touch_down.push_back(touch_down);
    }
    stride.ground_speed = count > 0 ? static_cast<float>(std::max(0.0, sum / count)) : 0.0f;
    return stride;
}

void sample_model_pose(const Skeleton& skeleton, const SkeletalClip& clip, float seconds, std::vector<glm::mat4>& model) {
    PoseSampler sampler;
    sampler.sample(skeleton, clip, clip.duration() > 0.0f ? seconds / clip.duration() : 0.0f);
    model = sampler.model();
}

}  // namespace moteur
