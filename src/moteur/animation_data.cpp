#include "moteur/animation_data.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace moteur {

glm::mat4 JointPose::matrix() const {
    return glm::scale(glm::translate(glm::mat4(1.0f), translation) * glm::mat4_cast(rotation), scale);
}

int SkeletonData::find(std::string_view name) const {
    for (std::size_t i = 0; i < joints.size(); ++i) {
        if (joints[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::vector<glm::mat4> SkeletonData::rest_model_matrices() const {
    std::vector<glm::mat4> model(joints.size());
    for (std::size_t i = 0; i < joints.size(); ++i) {
        const JointData& joint = joints[i];
        const glm::mat4 local = joint.rest.matrix();
        model[i] = joint.parent >= 0 ? model[static_cast<std::size_t>(joint.parent)] * local : local;
    }
    return model;
}

std::string SkeletonData::mismatch(const SkeletonData& other) const {
    if (joints.size() != other.joints.size()) {
        return std::to_string(joints.size()) + " joints instead of " + std::to_string(other.joints.size());
    }
    for (std::size_t i = 0; i < joints.size(); ++i) {
        const JointData& a = joints[i];
        const JointData& b = other.joints[i];
        if (a.name != b.name) {
            return "joint " + std::to_string(i) + " is '" + a.name + "' instead of '" + b.name + "'";
        }
        if (a.parent != b.parent) {
            const auto parent_name = [](const SkeletonData& skeleton, int parent) {
                return parent >= 0 ? "'" + skeleton.joints[static_cast<std::size_t>(parent)].name + "'" : std::string("none");
            };
            return "joint '" + a.name + "' has the parent " + parent_name(*this, a.parent) + " instead of " +
                   parent_name(other, b.parent);
        }
    }
    return {};
}

}  // namespace moteur
