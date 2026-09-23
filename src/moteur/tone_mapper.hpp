#pragma once

#include <SDL3/SDL.h>

#include "moteur/gpu_resource.hpp"

namespace moteur {

class Renderer;

// Internal to the engine (not in include/): the full-screen pass that turns the linear HDR image of
// the 3D scene into screen colors (exposure, Khronos PBR Neutral tone mapping, sRGB encoding). The
// Renderer runs it at the start of the "compose" pass, before the sprites and ImGui.
//
// The scene image may be smaller than the screen (render scale): the linear sampler scales it up.
class ToneMapper {
public:
    // `target_format`: the format of the pass it draws in (the swapchain).
    ToneMapper(Renderer& renderer, SDL_GPUTextureFormat target_format);

    // Inside a render pass: covers the whole target with `scene`, sampled over its whole extent.
    void render(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, SDL_GPUTexture* scene, float exposure);

private:
    GpuGraphicsPipeline pipeline_;
    GpuSampler sampler_;
};

}  // namespace moteur
