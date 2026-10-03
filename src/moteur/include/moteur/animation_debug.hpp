#pragma once

#include <glm/glm.hpp>

#include <vector>

#include "moteur/animation_data.hpp"

namespace moteur {

class DebugLineBuffer;

// How draw_skeleton() draws (milestone 5, part 9).
struct SkeletonDrawOptions {
    glm::vec4 color{0.2f, 0.9f, 1.0f, 1.0f};
    float joint_radius = 0.015f;  // metres in the world
    float axis_length = 0.0f;     // > 0: each joint's axes too (x red, y green, z blue), metres
    int highlight = -1;           // a joint drawn bigger, with its axes, in highlight_color
    glm::vec4 highlight_color{1.0f, 0.35f, 0.25f, 1.0f};
    bool on_top = true;           // over the meshes (false: hidden by them)
};

// A skeleton in debug lines: a segment from each joint to its parent, and a small sphere on each
// joint. `pose`: every joint's matrix in model space (AnimationPose::sampler.model(), the pose the
// meshes were drawn with this frame: the lines never lag behind them); `world`: the entity's world
// matrix at the same alpha. Axes show the joints' rotation, without their scale.
void draw_skeleton(DebugLineBuffer& lines, const glm::mat4& world, const SkeletonData& skeleton,
                   const std::vector<glm::mat4>& pose, const SkeletonDrawOptions& options = {});

}  // namespace moteur
