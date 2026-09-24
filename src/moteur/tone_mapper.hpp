#pragma once

#include <SDL3/SDL.h>

#include <cstdint>

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
    // `raw`: no tone mapping (debug views, whose values are meant to be seen as they are).
    void render(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, SDL_GPUTexture* scene, float exposure,
                bool raw = false);

private:
    GpuGraphicsPipeline pipeline_;
    GpuSampler sampler_;
};

// Internal too: FXAA (fxaa.frag.hlsl), for AntiAliasing::Fxaa. The tone mapping first draws the
// scene into a texture of screen colors (the "tonemap" pass); this pass then covers the "compose"
// target with that texture, filtered, in place of the ToneMapper.
class Fxaa {
public:
    Fxaa(Renderer& renderer, SDL_GPUTextureFormat target_format);

    // Inside a render pass: covers the whole target with `image` (of `width` x `height` texels),
    // anti-aliased and scaled to the target.
    void render(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, SDL_GPUTexture* image, std::uint32_t width,
                std::uint32_t height);

private:
    GpuGraphicsPipeline pipeline_;
    GpuSampler sampler_;
};

// Internal too: shows a depth texture (a shadow map) in a rectangle of the "compose" pass, in
// grays (near the light bright). For Renderer::set_debug_texture().
class DepthView {
public:
    DepthView(Renderer& renderer, SDL_GPUTextureFormat target_format);

    // `area`: where, in pixels of the target; the viewport is set back to `full` afterwards.
    void render(SDL_GPURenderPass* pass, SDL_GPUTexture* depth, const SDL_GPUViewport& area, const SDL_GPUViewport& full);

private:
    GpuGraphicsPipeline pipeline_;
    GpuSampler sampler_;
};

}  // namespace moteur
