#include "tone_mapper.hpp"

#include <stdexcept>
#include <string>

#include "moteur/renderer.hpp"

namespace moteur {

ToneMapper::ToneMapper(Renderer& renderer, SDL_GPUTextureFormat target_format) {
    ShaderInfo vertex_info;
    vertex_info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    ShaderInfo fragment_info;
    fragment_info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fragment_info.samplers = 1;         // the scene
    fragment_info.uniform_buffers = 1;  // exposure

    const GpuShader vertex_shader = renderer.load_shader("tonemap.vert", vertex_info);
    const GpuShader fragment_shader = renderer.load_shader("tonemap.frag", fragment_info);

    // It writes every pixel: no blending, no depth, no vertex buffer (the vertex shader makes the triangle).
    SDL_GPUColorTargetDescription color_target = {};
    color_target.format = target_format;

    SDL_GPUGraphicsPipelineCreateInfo info = {};
    info.vertex_shader = vertex_shader.get();
    info.fragment_shader = fragment_shader.get();
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.target_info.color_target_descriptions = &color_target;
    info.target_info.num_color_targets = 1;

    const NameProperty name_property(SDL_PROP_GPU_GRAPHICSPIPELINE_CREATE_NAME_STRING, "tonemap pipeline");
    info.props = name_property.id();
    SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(renderer.device(), &info);
    if (pipeline == nullptr) {
        throw std::runtime_error(std::string("SDL_CreateGPUGraphicsPipeline (tonemap) failed: ") + SDL_GetError());
    }
    pipeline_ = GpuGraphicsPipeline(renderer.device(), pipeline);

    // Linear, clamped to the edges: scales a reduced render size up smoothly, without wrapping.
    sampler_ = renderer.create_sampler(SDL_GPU_FILTER_LINEAR, "tonemap.sampler");
}

void ToneMapper::render(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, SDL_GPUTexture* scene,
                        float exposure) {
    SDL_BindGPUGraphicsPipeline(pass, pipeline_.get());
    const SDL_GPUTextureSamplerBinding binding = {scene, sampler_.get()};
    SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
    const float settings[4] = {exposure, 0.0f, 0.0f, 0.0f};
    SDL_PushGPUFragmentUniformData(commands, 0, settings, sizeof(settings));
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
}

}  // namespace moteur
