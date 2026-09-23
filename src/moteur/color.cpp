#include "moteur/color.hpp"

#include <algorithm>
#include <cmath>

namespace moteur {

float srgb_to_linear(float srgb) {
    srgb = std::clamp(srgb, 0.0f, 1.0f);
    return srgb <= 0.04045f ? srgb / 12.92f : std::pow((srgb + 0.055f) / 1.055f, 2.4f);
}

float linear_to_srgb(float linear) {
    linear = std::clamp(linear, 0.0f, 1.0f);
    return linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
}

glm::vec3 srgb_to_linear(glm::vec3 srgb) {
    return {srgb_to_linear(srgb.r), srgb_to_linear(srgb.g), srgb_to_linear(srgb.b)};
}

glm::vec3 linear_to_srgb(glm::vec3 linear) {
    return {linear_to_srgb(linear.r), linear_to_srgb(linear.g), linear_to_srgb(linear.b)};
}

glm::vec3 tonemap_pbr_neutral(glm::vec3 color) {
    // The reference implementation published by Khronos (glTF Sample Viewer).
    constexpr float kStartCompression = 0.8f - 0.04f;
    constexpr float kDesaturation = 0.15f;

    const float x = std::min(color.r, std::min(color.g, color.b));
    const float offset = x < 0.08f ? x - 6.25f * x * x : 0.04f;
    color -= offset;

    const float peak = std::max(color.r, std::max(color.g, color.b));
    if (peak < kStartCompression) {
        return color;
    }
    constexpr float d = 1.0f - kStartCompression;
    const float new_peak = 1.0f - d * d / (peak + d - kStartCompression);
    color *= new_peak / peak;

    const float g = 1.0f - 1.0f / (kDesaturation * (peak - new_peak) + 1.0f);
    return glm::mix(color, glm::vec3(new_peak), g);
}

}  // namespace moteur
