#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "moteur/animation_data.hpp"
#include "moteur/mesh.hpp"

namespace moteur {

// Skeletons and clips ready to be played, built at load time from what the glTF reader gives
// (SkeletonData, ClipData). ozz-animation does the work underneath; it stays out of the headers.

class SkeletalClip;
class PoseSampler;

// Clips are played in ticks of the fixed step (see ClipClock): a clip of d seconds lasts
// round(d x 60) ticks, at least one. At another simulation rate, clips keep their tick count
// (they play faster or slower than in the file).
inline constexpr int kClipTicksPerSecond = 60;

// The joints of a model, parents first (see SkeletonData). Moves, does not copy.
class Skeleton {
public:
    // Throws std::runtime_error naming `name` if there is no joint, or more than ozz takes (1024).
    static Skeleton create(const SkeletonData& data, const std::string& name);

    Skeleton();
    ~Skeleton();
    Skeleton(Skeleton&&) noexcept;
    Skeleton& operator=(Skeleton&&) noexcept;

    const std::string& name() const;
    const SkeletonData& data() const;
    int joint_count() const;
    std::size_t bytes() const;  // in main memory

    // Takes the content of `fresh` (the same file, loaded again). Throws std::runtime_error, and
    // stays as it was, if the joints changed (names, order, parents): the scene must be reloaded.
    void replace_in_place(Skeleton&& fresh);

private:
    friend class ClipLibrary;
    friend class PoseSampler;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// One clip of a ClipLibrary. Its address stays the same for the library's lifetime, reloads
// included: a player may keep a pointer to it.
class SkeletalClip {
public:
    SkeletalClip();
    ~SkeletalClip();
    SkeletalClip(SkeletalClip&&) noexcept;
    SkeletalClip& operator=(SkeletalClip&&) noexcept;

    const std::string& name() const;
    float duration() const;  // seconds
    // round(duration() x kClipTicksPerSecond), at least 1.
    int duration_ticks() const;
    std::size_t bytes() const;

private:
    friend class ClipLibrary;
    friend class PoseSampler;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// The clips of one file, built for that file's skeleton. They play on any skeleton of the same
// structure (same joints in the same order: see SkeletonData::mismatch), such as the KayKit
// characters, which share theirs.
class ClipLibrary {
public:
    // `source` names the file in messages. Throws std::runtime_error naming it and the clip if a
    // clip cannot be built (keys outside the clip, no track for the skeleton).
    static ClipLibrary create(const SkeletonData& skeleton, const std::vector<ClipData>& clips, const std::string& source);

    // Throws std::runtime_error naming the file and the clip if there is no such clip.
    const SkeletalClip& clip(const std::string& name) const;
    bool contains(const std::string& name) const { return clips_.count(name) != 0; }
    // All clip names, sorted.
    std::vector<std::string> names() const;
    std::size_t size() const { return clips_.size(); }
    std::size_t bytes() const;
    const std::string& source() const { return source_; }

    // The skeleton the clips were made for, and why they cannot play on `skeleton` (empty if they can).
    const SkeletonData& skeleton() const { return skeleton_; }
    std::string mismatch(const Skeleton& skeleton) const;

    // Takes the content of `fresh` (the same file, loaded again), keeping every clip at its
    // address. Throws std::runtime_error, and stays as it was, if the clip names or the skeleton
    // changed: the scene must be reloaded.
    void replace_in_place(ClipLibrary&& fresh);

private:
    std::string source_;
    SkeletonData skeleton_;
    std::map<std::string, SkeletalClip> clips_;
};

// One clip of a blended pose: where it is (a ratio of its duration, as in PoseSampler::sample) and
// how much it counts. `joint_weights`, one per joint of the skeleton (empty: 1 everywhere), makes
// it count more or less joint by joint: a mask. Weights need not add up to 1: they are normalized
// joint by joint.
struct PoseInput {
    const SkeletalClip* clip = nullptr;
    float ratio = 0.0f;
    float weight = 1.0f;
    std::span<const float> joint_weights;
};

// Samples clips into a pose for one animated character, and keeps what makes that cheap from
// frame to frame: ozz's sampling cache (reading a clip forward reuses the keys found last time)
// and the buffers. Moves, does not copy.
class PoseSampler {
public:
    PoseSampler();
    ~PoseSampler();
    PoseSampler(PoseSampler&&) noexcept;
    PoseSampler& operator=(PoseSampler&&) noexcept;

    // The pose of `clip` at `ratio` of its duration (0: start, 1: end; clamped). Throws
    // std::invalid_argument if the clip was made for another number of joints.
    void sample(const Skeleton& skeleton, const SkeletalClip& clip, float ratio);
    // The rest pose.
    void rest(const Skeleton& skeleton);
    // The bind pose (tools): the joints of `skins` where the skinned meshes were bound to them (the
    // inverse of their bind matrices: every palette is the identity, the meshes as modelled), the
    // other joints in their rest pose.
    void bind(const Skeleton& skeleton, const std::vector<SkinData>& skins);
    // Several clips blended (crossfades, blend spaces, masked layers). A joint whose weights add
    // up to almost nothing takes its rest pose. One input of weight 1 without mask costs a
    // sample(); none gives the rest pose. Throws std::invalid_argument as sample() does, or if
    // joint weights do not have one value per joint.
    void blend(const Skeleton& skeleton, std::span<const PoseInput> inputs);

    // Every joint's matrix in model space, from the last sample() or rest().
    const std::vector<glm::mat4>& model() const { return model_; }
    // The skinning matrices of `skin`: each palette joint's model matrix times its inverse bind
    // matrix. `out` is resized.
    void palette(const SkinData& skin, std::vector<glm::mat4>& out) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::vector<glm::mat4> model_;
};

// Where a vertex of a skinned mesh goes in a pose: the same formula as skinning.hlsli (linear
// blend skinning), on the CPU, for tests and tools. `palette`: PoseSampler::palette() of the
// mesh's skin; `position`: the vertex in mesh space.
glm::vec3 skin_position(const std::vector<glm::mat4>& palette, const VertexSkin& skin, glm::vec3 position);

// How a walk or a run moves over the ground (milestone 5, part 6), measured on an in-place clip:
// while a foot is on the ground (near its lowest), it slides backwards under the character at the
// speed the character must move for it not to slip. `feet`: joint indices; `forward`: the
// character's forward axis in model space (+Z in glTF). Model units (metres of the file) per
// second at x1. `touch_down` is, for each foot, where in the cycle (a ratio) it lands.
struct Stride {
    float ground_speed = 0.0f;
    std::vector<float> touch_down;
};
Stride measure_stride(const Skeleton& skeleton, const SkeletalClip& clip, const std::vector<int>& feet,
                      glm::vec3 forward = {0.0f, 0.0f, 1.0f});

// Samples `clip` at `seconds` from its start (clamped to its duration) on `skeleton`, and writes
// every joint's matrix in model space into `model` (resized). Allocates at each call: for tests,
// tools and debug views. Throws std::invalid_argument if the clip was made for another number of
// joints.
void sample_model_pose(const Skeleton& skeleton, const SkeletalClip& clip, float seconds, std::vector<glm::mat4>& model);

}  // namespace moteur
