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

/// The joints of a model, parents first (see SkeletonData), built for playback. Moves, does not
/// copy. Usually loaded and shared through the asset cache (`Asset<Skeleton>`).
class Skeleton {
public:
    /// Builds the skeleton of `data` (copied). `name` (usually the model's path) appears in
    /// messages. Throws std::runtime_error naming `name` if there is no joint, or more than ozz
    /// takes (1024), or if ozz refuses it. Allocates; loading time.
    static Skeleton create(const SkeletonData& data, const std::string& name);

    /// An empty skeleton: name() and data() are empty, joint_count() and bytes() are 0;
    /// replace_in_place() takes any fresh skeleton; nothing else may be called on it (nor on a
    /// moved-from one) until a skeleton from create() is assigned to it.
    Skeleton();
    ~Skeleton();
    Skeleton(Skeleton&&) noexcept;             ///< leaves `other` empty
    Skeleton& operator=(Skeleton&&) noexcept;  ///< leaves `other` empty

    const std::string& name() const;   ///< the name given to create(); empty for an empty skeleton
    /// The joints it was built from (names, parents, rest pose); no joint for an empty skeleton.
    const SkeletonData& data() const;
    int joint_count() const;           ///< 0 for an empty skeleton
    std::size_t bytes() const;  ///< in main memory, approximately

    /// Takes the content of `fresh` (the same file, loaded again). Throws std::runtime_error, and
    /// stays as it was, if the joints changed (names, order, parents): the scene must be reloaded.
    /// An empty skeleton takes any `fresh` (it has no joint to keep). Throws std::invalid_argument,
    /// and stays as it was, if `fresh` is empty (default-constructed or moved-from).
    void replace_in_place(Skeleton&& fresh);

private:
    friend class ClipLibrary;
    friend class PoseSampler;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// One clip of a ClipLibrary. Its address stays the same for the library's lifetime, reloads
/// included: a player may keep a pointer to it.
/// Only a ClipLibrary makes them: a default-constructed or moved-from clip is empty: its name() is
/// empty, duration() and bytes() are 0, duration_ticks() is 1; it may not be played.
class SkeletalClip {
public:
    SkeletalClip();  ///< an empty clip
    ~SkeletalClip();
    SkeletalClip(SkeletalClip&&) noexcept;             ///< leaves `other` empty
    SkeletalClip& operator=(SkeletalClip&&) noexcept;  ///< leaves `other` empty

    const std::string& name() const;  ///< as in the file (ClipData::name), unique in its library; empty for an empty clip
    float duration() const;  ///< seconds; 0 for an empty clip
    /// Clips are played in ticks of the fixed step (see ClipClock): duration() in ticks at tick_rate(),
    /// as ticks_from_seconds() rounds it, at least 1. At any simulation rate, a clip lasts what the
    /// file says.
    int duration_ticks() const;
    std::size_t bytes() const;  ///< the ozz animation, in main memory; 0 for an empty clip

private:
    friend class ClipLibrary;
    friend class PoseSampler;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// The clips of one file, built for that file's skeleton. They play on any skeleton of the same
/// structure (same joints in the same order: see SkeletonData::mismatch), such as the KayKit
/// characters, which share theirs. Usually loaded and shared through the asset cache
/// (`Asset<ClipLibrary>`).
class ClipLibrary {
public:
    /// Builds every clip of `clips` for `skeleton` (both copied as needed). `source` names the file
    /// in messages. Throws std::runtime_error naming it and the clip if a clip cannot be built: not
    /// one track per joint, keys ozz refuses (outside the clip's duration, unsorted), or two clips of
    /// the same name. Allocates; loading time.
    static ClipLibrary create(const SkeletonData& skeleton, const std::vector<ClipData>& clips, const std::string& source);

    /// The prepared form of the library (milestone 9, part 4): its ozz animations as an ozz archive
    /// (the runtime format, keys quantized), behind a checksum. What an imported model keeps
    /// (ModelData::prepared_clips): the shipped game never builds clips from glTF keys.
    std::vector<std::uint8_t> save() const;
    /// A library from save()'s bytes, for `skeleton`. Throws std::runtime_error naming `source` for
    /// bytes that are not such an archive (checksum, ozz tags and versions), or clips that do not fit
    /// the skeleton (their number of tracks) or share a name.
    static ClipLibrary load(const SkeletonData& skeleton, const std::vector<std::uint8_t>& bytes, const std::string& source);

    /// The clip `name`; the reference stays valid, at the same address, as long as the library,
    /// reloads included. Throws std::runtime_error naming the file and the clip if there is no
    /// such clip.
    const SkeletalClip& clip(const std::string& name) const;
    /// Whether the library has a clip `name` (exact name). O(log n).
    bool contains(const std::string& name) const { return clips_.count(name) != 0; }
    /// All clip names, sorted. Allocates.
    std::vector<std::string> names() const;
    /// The number of clips.
    std::size_t size() const { return clips_.size(); }
    /// The clips' animations in main memory (SkeletalClip::bytes()), summed.
    std::size_t bytes() const;
    /// The name given to create(), usually the file's path.
    const std::string& source() const { return source_; }

    /// The skeleton the clips were made for.
    const SkeletonData& skeleton() const { return skeleton_; }
    /// Why the clips cannot play on `skeleton` (SkeletonData::mismatch()); empty if they can.
    std::string mismatch(const Skeleton& skeleton) const;

    /// Takes the content of `fresh` (the same file, loaded again), keeping every clip at its
    /// address. Throws std::runtime_error, and stays as it was, if the clip names or the skeleton
    /// changed: the scene must be reloaded.
    void replace_in_place(ClipLibrary&& fresh);

private:
    std::string source_;
    SkeletonData skeleton_;
    std::map<std::string, SkeletalClip> clips_;
};

/// One clip of a blended pose: where it is (a ratio of its duration, as in PoseSampler::sample) and
/// how much it counts. `joint_weights`, one per joint of the skeleton (empty: 1 everywhere), makes
/// it count more or less joint by joint: a mask. Weights need not add up to 1: they are normalized
/// joint by joint.
struct PoseInput {
    const SkeletalClip* clip = nullptr;  ///< not owned; must not be null for PoseSampler::blend()
    float ratio = 0.0f;   ///< [0, 1] of the clip's duration (clamped)
    float weight = 1.0f;  ///< >= 0, relative to the other inputs (normalized joint by joint)
    /// One weight per joint (empty: 1 everywhere). Not owned: must stay valid until the blend.
    std::span<const float> joint_weights;
};

/// Samples clips into a pose for one animated character, and keeps what makes that cheap from
/// frame to frame: ozz's sampling cache (reading a clip forward reuses the keys found last time)
/// and the buffers. Moves, does not copy. Not thread-safe: one sampler per character, or per
/// thread; several samplers may sample the same Skeleton and clips at once.
class PoseSampler {
public:
    PoseSampler();  ///< no pose yet: model() is empty
    ~PoseSampler();
    PoseSampler(PoseSampler&&) noexcept;             ///< `other` may only be destroyed or assigned to afterwards
    PoseSampler& operator=(PoseSampler&&) noexcept;  ///< `other` may only be destroyed or assigned to afterwards

    /// The pose of `clip` at `ratio` of its duration (0: start, 1: end; clamped). Throws
    /// std::invalid_argument if the clip was made for another number of joints. Allocates only
    /// the first time, or for a bigger skeleton.
    void sample(const Skeleton& skeleton, const SkeletalClip& clip, float ratio);
    /// The rest pose (each joint's JointData::rest).
    void rest(const Skeleton& skeleton);
    /// The bind pose (tools): the joints of `skins` where the skinned meshes were bound to them (the
    /// inverse of their bind matrices: every palette is the identity, the meshes as modelled), the
    /// other joints in their rest pose.
    void bind(const Skeleton& skeleton, const std::vector<SkinData>& skins);
    /// Several clips blended (crossfades, blend spaces, masked layers). A joint whose weights add
    /// up to almost nothing takes its rest pose. One input of weight 1 without mask costs a
    /// sample(); none gives the rest pose. Throws std::invalid_argument as sample() does, if an
    /// input has no clip, or if joint weights do not have one value per joint.
    void blend(const Skeleton& skeleton, std::span<const PoseInput> inputs);

    /// Every joint's matrix in model space, from the last sample(), rest(), bind() or blend(). The
    /// reference stays valid as long as the sampler; the next call changes the content.
    const std::vector<glm::mat4>& model() const { return model_; }
    /// The skinning matrices of `skin`: each palette joint's model matrix times its inverse bind
    /// matrix (a joint outside the pose: the inverse bind matrix alone). `out` is resized.
    void palette(const SkinData& skin, std::vector<glm::mat4>& out) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::vector<glm::mat4> model_;
};

/// Where a vertex of a skinned mesh goes in a pose: the same formula as skinning.hlsli (linear
/// blend skinning), on the CPU, for tests and tools. `palette`: PoseSampler::palette() of the
/// mesh's skin; `skin`: the vertex's joints (indices into `palette`, not checked) and weights;
/// `position`: the vertex in mesh space. Returns the vertex in model space.
glm::vec3 skin_position(const std::vector<glm::mat4>& palette, const VertexSkin& skin, glm::vec3 position);

/// How a walk or a run moves over the ground (milestone 5, part 6), as measure_stride() finds it.
struct Stride {
    float ground_speed = 0.0f;  ///< model units (metres of the file) per second at x1, >= 0
    std::vector<float> touch_down;  ///< for each foot, where in the cycle (a ratio in [0, 1)) it lands
};
/// Measures a Stride on an in-place clip: while a foot is on the ground (within a tenth of its
/// rise above its lowest point), it slides backwards under the character at the speed the
/// character must move for it not to slip; that speed is averaged over every foot and every
/// moment on the ground. `feet`: joint indices; `forward`: the character's forward axis in model
/// space (+Z in glTF; normalized here). A clip of no duration or no foot gives an empty Stride.
/// Throws std::invalid_argument for a foot that is not a joint, or as PoseSampler::sample() does.
/// Samples the clip 120 times per second (at least 32 times): a tool, for writing "ground_speed"
/// in an AnimationSet file, not for every frame.
Stride measure_stride(const Skeleton& skeleton, const SkeletalClip& clip, const std::vector<int>& feet,
                      glm::vec3 forward = {0.0f, 0.0f, 1.0f});

/// Samples `clip` at `seconds` from its start (clamped to its duration) on `skeleton`, and writes
/// every joint's matrix in model space into `model` (resized). Allocates at each call: for tests,
/// tools and debug views. Throws std::invalid_argument if the clip was made for another number of
/// joints.
void sample_model_pose(const Skeleton& skeleton, const SkeletalClip& clip, float seconds, std::vector<glm::mat4>& model);

}  // namespace moteur
