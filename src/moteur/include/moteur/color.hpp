#pragma once

#include <glm/glm.hpp>

namespace moteur {

// Color conversions of the 3D chain (milestone 3, part 6). The shaders (tonemap.frag.hlsl) do the
// same computations on the GPU; these CPU versions are the reference the unit tests check, and a
// way to turn hand-picked colors into the linear values lighting expects.
//
// Lighting is computed on *linear* values (proportional to light energy). Images and screens store
// *sRGB* values, spaced to match what the eye perceives: 0.5 in sRGB is only about 0.21 in linear.

// The sRGB transfer functions, per component, on [0, 1].
float srgb_to_linear(float srgb);
float linear_to_srgb(float linear);
glm::vec3 srgb_to_linear(glm::vec3 srgb);
glm::vec3 linear_to_srgb(glm::vec3 linear);

// Khronos PBR Neutral tone mapping: brings linear HDR values (lights can exceed 1.0) into [0, 1]
// for the screen. Colors below about 0.76 pass almost unchanged (so materials keep the look they
// have in Blender and other glTF viewers); brighter ones are compressed smoothly towards white.
glm::vec3 tonemap_pbr_neutral(glm::vec3 color);

}  // namespace moteur
