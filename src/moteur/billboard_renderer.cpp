#include "moteur/billboard_renderer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "moteur/camera3d.hpp"

namespace moteur {

namespace {

constexpr std::size_t kInitialCapacity = 1024;  // billboards; the buffer grows when a frame needs more

}  // namespace

BillboardRenderer::BillboardRenderer(Renderer& renderer, SDL_GPUTextureFormat color_format,
                                     SDL_GPUTextureFormat depth_format)
    : renderer_(renderer), color_format_(color_format), depth_format_(depth_format) {
    ShaderInfo vertex_info;
    vertex_info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    vertex_info.uniform_buffers = 1;  // the view-projection matrix
    ShaderInfo fragment_info;
    fragment_info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fragment_info.samplers = 1;
    // Kept: the pipeline is made again when the number of samples changes (MSAA).
    vertex_shader_ = renderer.load_shader("billboard.vert", vertex_info);
    fragment_shader_ = renderer.load_shader("billboard.frag", fragment_info);

    // Smooth, with mipmaps: billboards are seen at every distance.
    SDL_GPUSamplerCreateInfo sampler_info = {};
    sampler_info.min_filter = SDL_GPU_FILTER_LINEAR;
    sampler_info.mag_filter = SDL_GPU_FILTER_LINEAR;
    sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.max_lod = 1000.0f;
    const NameProperty sampler_name(SDL_PROP_GPU_SAMPLER_CREATE_NAME_STRING, "billboard.sampler");
    sampler_info.props = sampler_name.id();
    SDL_GPUSampler* sampler = SDL_CreateGPUSampler(renderer.device(), &sampler_info);
    if (sampler == nullptr) {
        throw std::runtime_error(std::string("SDL_CreateGPUSampler (billboard) failed: ") + SDL_GetError());
    }
    sampler_ = GpuSampler(renderer.device(), sampler);

    // The same six indices for every quad, as for sprites.
    std::vector<std::uint16_t> indices;
    indices.reserve(static_cast<std::size_t>(BillboardBatcher::kMaxQuadsPerRun) * 6);
    for (std::uint32_t quad = 0; quad < BillboardBatcher::kMaxQuadsPerRun; ++quad) {
        const auto base = static_cast<int>(quad * 4);
        for (const int corner : {0, 1, 2, 0, 2, 3}) {
            indices.push_back(static_cast<std::uint16_t>(base + corner));
        }
    }
    index_buffer_ = renderer.create_buffer(SDL_GPU_BUFFERUSAGE_INDEX, indices.data(),
                                           indices.size() * sizeof(std::uint16_t), "billboard.indices");
    ensure_capacity(kInitialCapacity);

    create_pipeline(SDL_GPU_SAMPLECOUNT_1);
}

void BillboardRenderer::create_pipeline(SDL_GPUSampleCount samples) {
    SDL_GPUVertexBufferDescription vertex_buffer = {};
    vertex_buffer.slot = 0;
    vertex_buffer.pitch = sizeof(BillboardVertex);
    vertex_buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_GPUVertexAttribute attributes[3] = {};
    attributes[0].location = 0;  // position
    attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[0].offset = offsetof(BillboardVertex, x);
    attributes[1].location = 1;  // uv
    attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attributes[1].offset = offsetof(BillboardVertex, u);
    attributes[2].location = 2;  // color, premultiplied
    attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    attributes[2].offset = offsetof(BillboardVertex, r);

    // Premultiplied alpha, as for sprites: "source + background x (1 - source alpha)". With a
    // source alpha of 0 (additive billboards), the source is simply added.
    SDL_GPUColorTargetDescription color_target = {};
    color_target.format = color_format_;
    color_target.blend_state.enable_blend = true;
    color_target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    color_target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    color_target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    color_target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    color_target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    color_target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

    SDL_GPUGraphicsPipelineCreateInfo info = {};
    info.vertex_shader = vertex_shader_.get();
    info.fragment_shader = fragment_shader_.get();
    info.vertex_input_state.vertex_buffer_descriptions = &vertex_buffer;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = attributes;
    info.vertex_input_state.num_vertex_attributes = 3;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;  // seen from both sides
    // Hidden by the meshes in front, but writes no depth: billboards behind others still show
    // through their transparent parts (they are sorted instead).
    info.multisample_state.sample_count = samples;
    info.depth_stencil_state.enable_depth_test = true;
    info.depth_stencil_state.enable_depth_write = false;
    info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    info.target_info.color_target_descriptions = &color_target;
    info.target_info.num_color_targets = 1;
    info.target_info.has_depth_stencil_target = true;
    info.target_info.depth_stencil_format = depth_format_;
    const NameProperty name_property(SDL_PROP_GPU_GRAPHICSPIPELINE_CREATE_NAME_STRING, "billboard pipeline");
    info.props = name_property.id();
    SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(renderer_.device(), &info);
    if (pipeline == nullptr) {
        throw std::runtime_error(std::string("SDL_CreateGPUGraphicsPipeline (billboard) failed: ") + SDL_GetError());
    }
    pipeline_ = GpuGraphicsPipeline(renderer_.device(), pipeline);
}

void BillboardRenderer::set_camera(const Camera3D& camera) {
    view_projection_ = camera.view_projection();
    view_.eye = camera.position();
    view_.forward = camera.forward();
    view_.right = glm::normalize(glm::cross(view_.forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    view_.up = glm::cross(view_.right, view_.forward);
    has_camera_ = true;
}

void BillboardRenderer::draw(const Texture& texture, glm::vec3 center, glm::vec2 size, const BillboardOptions& options) {
    BillboardDesc billboard;
    billboard.texture = texture.gpu.get();
    billboard.center = center;
    billboard.size = size;
    billboard.uv_rect = options.uv_rect;
    billboard.color = options.color;
    billboard.additive = options.additive;
    billboard.facing = options.facing;
    batcher_.add(billboard);
}

void BillboardRenderer::ensure_capacity(std::size_t billboards) {
    if (billboards <= capacity_) {
        return;
    }
    std::size_t capacity = std::max(capacity_, kInitialCapacity);
    while (capacity < billboards) {
        capacity *= 2;
    }
    const std::size_t bytes = capacity * 4 * sizeof(BillboardVertex);
    vertex_buffer_ = renderer_.create_buffer(SDL_GPU_BUFFERUSAGE_VERTEX, bytes, "billboard.vertices");
    transfer_buffer_ = renderer_.create_transfer_buffer(bytes);
    if (capacity_ != 0) {
        SDL_Log("Billboard buffer grown from %zu to %zu billboards (%.1f MiB)", capacity_, capacity,
                static_cast<double>(bytes) / (1024.0 * 1024.0));
    }
    capacity_ = capacity;
}

void BillboardRenderer::prepare(SDL_GPUCommandBuffer* commands, RenderStats& stats) {
    if (!has_work()) {
        return;
    }
    batcher_.finish(view_);
    const std::size_t count = batcher_.count();
    ensure_capacity(count);
    const std::size_t bytes = count * 4 * sizeof(BillboardVertex);
    SDL_GPUDevice* device = renderer_.device();
    void* mapped = SDL_MapGPUTransferBuffer(device, transfer_buffer_.get(), true);
    if (mapped == nullptr) {
        throw std::runtime_error(std::string("SDL_MapGPUTransferBuffer (billboards) failed: ") + SDL_GetError());
    }
    std::memcpy(mapped, batcher_.vertices().data(), bytes);
    SDL_UnmapGPUTransferBuffer(device, transfer_buffer_.get());
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(commands);
    const SDL_GPUTransferBufferLocation source = {transfer_buffer_.get(), 0};
    const SDL_GPUBufferRegion destination = {vertex_buffer_.get(), 0, static_cast<Uint32>(bytes)};
    SDL_UploadToGPUBuffer(copy_pass, &source, &destination, true);
    SDL_EndGPUCopyPass(copy_pass);
    stats.bytes_uploaded += bytes;
}

void BillboardRenderer::render(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, RenderStats& stats) {
    if (!has_work()) {
        return;
    }
    SDL_BindGPUGraphicsPipeline(pass, pipeline_.get());
    const SDL_GPUBufferBinding vertices = {vertex_buffer_.get(), 0};
    SDL_BindGPUVertexBuffers(pass, 0, &vertices, 1);
    const SDL_GPUBufferBinding indices = {index_buffer_.get(), 0};
    SDL_BindGPUIndexBuffer(pass, &indices, SDL_GPU_INDEXELEMENTSIZE_16BIT);
    SDL_PushGPUVertexUniformData(commands, 0, &view_projection_, sizeof(view_projection_));
    const void* bound_texture = nullptr;
    for (const BillboardRun& run : batcher_.runs()) {
        if (run.texture != bound_texture) {
            const SDL_GPUTextureSamplerBinding binding = {static_cast<SDL_GPUTexture*>(const_cast<void*>(run.texture)),
                                                          sampler_.get()};
            SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
            bound_texture = run.texture;
        }
        SDL_DrawGPUIndexedPrimitives(pass, run.count * 6, 1, 0, static_cast<Sint32>(run.first * 4), 0);
        ++stats.draw_calls;
        ++stats.billboard_draw_calls;
    }
    stats.billboards += static_cast<int>(batcher_.count());
}

}  // namespace moteur
