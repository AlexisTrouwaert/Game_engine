#include "moteur/animation_debug.hpp"

#include <algorithm>

#include "moteur/debug_lines.hpp"

namespace moteur {

void draw_skeleton(DebugLineBuffer& lines, const glm::mat4& world, const SkeletonData& skeleton,
                   const std::vector<glm::mat4>& pose, const SkeletonDrawOptions& options) {
    const std::size_t count = std::min(pose.size(), skeleton.joints.size());
    for (std::size_t j = 0; j < count; ++j) {
        const glm::mat4 joint = world * pose[j];
        const glm::vec3 at(joint[3]);
        const bool highlighted = static_cast<int>(j) == options.highlight;
        const glm::vec4 color = highlighted ? options.highlight_color : options.color;
        const int parent = skeleton.joints[j].parent;
        if (parent >= 0 && static_cast<std::size_t>(parent) < count) {
            lines.line(glm::vec3(world * pose[static_cast<std::size_t>(parent)][3]), at, color, options.on_top);
        }
        lines.sphere(at, highlighted ? options.joint_radius * 2.5f : options.joint_radius, color, options.on_top);
        const float axis = options.axis_length > 0.0f ? options.axis_length
                                                      : (highlighted ? options.joint_radius * 8.0f : 0.0f);
        if (axis > 0.0f) {
            static constexpr glm::vec4 kAxisColors[3] = {{1.0f, 0.2f, 0.2f, 1.0f}, {0.2f, 1.0f, 0.2f, 1.0f}, {0.3f, 0.4f, 1.0f, 1.0f}};
            for (int a = 0; a < 3; ++a) {
                const glm::vec3 direction(joint[a]);
                const float length = glm::length(direction);
                if (length > 0.0f) {
                    lines.line(at, at + direction / length * axis, kAxisColors[a], options.on_top);
                }
            }
        }
    }
}

}  // namespace moteur
