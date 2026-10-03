#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace moteur {

// Skeletons and clips on the CPU, as read from a glTF file (see parse_gltf): plain data, testable
// without ozz or a GPU. Skeleton and ClipLibrary (skeleton.hpp) build what plays them from it.

// A joint's transformation relative to its parent: scale, then rotation, then translation.
struct JointPose {
    glm::vec3 translation{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};  // identity (glm's order: w, x, y, z)
    glm::vec3 scale{1.0f};

    glm::mat4 matrix() const;
    bool operator==(const JointPose&) const = default;
};

struct JointData {
    std::string name;  // the glTF node's name, or "node <index>" when it has none
    int parent = -1;   // index in SkeletonData::joints, always smaller than this joint's; -1 for a root
    JointPose rest;    // the node's transformation in the file: the pose when nothing plays
};

// The joints of a model, parents before children (depth first, children in file order: the order
// ozz builds its skeletons in). They are the nodes the skins use and the nodes the clips animate,
// with all their ancestors, so that a node above the skeleton (an "armature" with a translation, a
// Z-up correction) still moves the character. A model without skins nor clips has none.
struct SkeletonData {
    std::vector<JointData> joints;

    bool empty() const { return joints.empty(); }
    // The first joint with that name, -1 if none.
    int find(std::string_view name) const;
    // Every joint's matrix in model space, in the rest pose.
    std::vector<glm::mat4> rest_model_matrices() const;
    // Why `other` cannot play the clips made for this skeleton (and the reverse): a different
    // number of joints, or the first joint whose name or parent differs. Empty when they match.
    std::string mismatch(const SkeletonData& other) const;
};

// What a skinned mesh needs to be deformed: its palette of joints. A vertex's joint indices point
// into `joints`, which gives the skeleton joint; the matrix of a palette entry is the joint's model
// matrix times its inverse bind matrix.
struct SkinData {
    std::string name;
    std::vector<int> joints;                  // index in SkeletonData::joints
    std::vector<glm::mat4> inverse_binds;     // one per palette entry
};

// Keys of one property, times in seconds from the start of the clip, interpolated linearly
// (rotations: shortest path). STEP and CUBICSPLINE keys of the file are converted to linear ones
// when read (see parse_gltf).
template <typename T>
struct KeyTrack {
    std::vector<float> times;
    std::vector<T> values;

    bool empty() const { return times.empty(); }
};

struct JointTrack {
    KeyTrack<glm::vec3> translations;
    KeyTrack<glm::quat> rotations;
    KeyTrack<glm::vec3> scales;
};

// One clip: a track per skeleton joint (same order as SkeletonData::joints). A joint the clip does
// not animate has empty tracks, and keeps its rest pose while the clip plays.
struct ClipData {
    std::string name;       // the glTF animation's name, or "animation <index>"
    float duration = 0.0f;  // seconds; a clip of a single pose (all keys at one time) lasts 1/60 s
    std::vector<JointTrack> tracks;
};

}  // namespace moteur
