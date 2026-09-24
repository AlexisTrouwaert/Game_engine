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
                        float exposure, bool raw) {
    SDL_BindGPUGraphicsPipeline(pass, pipeline_.get());
    const SDL_GPUTextureSamplerBinding binding = {scene, sampler_.get()};
    SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
    const float settings[4] = {raw ? 1.0f : exposure, raw ? 1.0f : 0.0f, 0.0f, 0.0f};
    SDL_PushGPUFragmentUniformData(commands, 0, settings, sizeof(settings));
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
}

Fxaa::Fxaa(Renderer& renderer, SDL_GPUTextureFormat target_format) {
    ShaderInfo vertex_info;
    vertex_info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    ShaderInfo fragment_info;
    fragment_info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fragment_info.samplers = 1;         // the tone-mapped scene
    fragment_info.uniform_buffers = 1;  // its texel size
    const GpuShader vertex_shader = renderer.load_shader("tonemap.vert", vertex_info);  // the same triangle
    const GpuShader fragment_shader = renderer.load_shader("fxaa.frag", fragment_info);
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
    const NameProperty name_property(SDL_PROP_GPU_GRAPHICSPIPELINE_CREATE_NAME_STRING, "fxaa pipeline");
    info.props = name_property.id();
    SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(renderer.device(), &info);
    if (pipeline == nullptr) {
        throw std::runtime_error(std::string("SDL_CreateGPUGraphicsPipeline (fxaa) failed: ") + SDL_GetError());
    }
    pipeline_ = GpuGraphicsPipeline(renderer.device(), pipeline);
    // Linear: the filter blends two texels with one shifted read, and scales a reduced render size up.
    sampler_ = renderer.create_sampler(SDL_GPU_FILTER_LINEAR, "fxaa.sampler");
}

void Fxaa::render(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, SDL_GPUTexture* image, std::uint32_t width,
                  std::uint32_t height) {
    SDL_BindGPUGraphicsPipeline(pass, pipeline_.get());
    const SDL_GPUTextureSamplerBinding binding = {image, sampler_.get()};
    SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
    const float texel[4] = {1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height), 0.0f, 0.0f};
    SDL_PushGPUFragmentUniformData(commands, 0, texel, sizeof(texel));
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
}

DepthView::DepthView(Renderer& renderer, SDL_GPUTextureFormat target_format) {
    ShaderInfo vertex_info;
    vertex_info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    ShaderInfo fragment_info;
    fragment_info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fragment_info.samplers = 1;
    const GpuShader vertex_shader = renderer.load_shader("tonemap.vert", vertex_info);  // the same triangle
    const GpuShader fragment_shader = renderer.load_shader("depth_view.frag", fragment_info);
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
    const NameProperty name_property(SDL_PROP_GPU_GRAPHICSPIPELINE_CREATE_NAME_STRING, "depth view pipeline");
    info.props = name_property.id();
    SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(renderer.device(), &info);
    if (pipeline == nullptr) {
        throw std::runtime_error(std::string("SDL_CreateGPUGraphicsPipeline (depth view) failed: ") + SDL_GetError());
    }
    pipeline_ = GpuGraphicsPipeline(renderer.device(), pipeline);
    sampler_ = renderer.create_sampler(SDL_GPU_FILTER_NEAREST, "depth view.sampler");  // plain reads, no comparison
}

void DepthView::render(SDL_GPURenderPass* pass, SDL_GPUTexture* depth, const SDL_GPUViewport& area,
                       const SDL_GPUViewport& full) {
    SDL_SetGPUViewport(pass, &area);
    SDL_BindGPUGraphicsPipeline(pass, pipeline_.get());
    const SDL_GPUTextureSamplerBinding binding = {depth, sampler_.get()};
    SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    SDL_SetGPUViewport(pass, &full);
}

}  // namespace moteur
