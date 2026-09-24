#include "moteur/debug_lines.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

namespace moteur {

void DebugLineBuffer::clear() {
    depth_tested_.clear();
    on_top_.clear();
}

void DebugLineBuffer::line(glm::vec3 from, glm::vec3 to, glm::vec4 color, bool on_top) {
    std::vector<DebugLineVertex>& target = on_top ? on_top_ : depth_tested_;
    target.push_back({from, color});
    target.push_back({to, color});
}

void DebugLineBuffer::box(const Aabb& box, glm::vec4 color, bool on_top) {
    if (box.empty()) {
        return;
    }
    // Corner i has bit 0 for x, bit 1 for y, bit 2 for z (Aabb::corner): an edge joins two corners
    // that differ by one bit.
    for (int i = 0; i < 8; ++i) {
        for (const int bit : {1, 2, 4}) {
            if ((i & bit) == 0) {
                line(box.corner(i), box.corner(i | bit), color, on_top);
            }
        }
    }
}

void DebugLineBuffer::frustum(const glm::mat4& view_projection, glm::vec4 color, bool on_top) {
    const glm::mat4 inverse = glm::inverse(view_projection);
    glm::vec3 corners[8];
    for (int i = 0; i < 8; ++i) {
        const glm::vec4 clip((i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, (i & 4) ? 1.0f : 0.0f, 1.0f);
        const glm::vec4 world = inverse * clip;
        corners[i] = glm::vec3(world) / world.w;
    }
    for (int i = 0; i < 8; ++i) {
        for (const int bit : {1, 2, 4}) {
            if ((i & bit) == 0) {
                line(corners[i], corners[i | bit], color, on_top);
            }
        }
    }
}

void DebugLineBuffer::axes(glm::vec3 origin, float length, bool on_top) {
    line(origin, origin + glm::vec3(length, 0.0f, 0.0f), {1.0f, 0.1f, 0.1f, 1.0f}, on_top);
    line(origin, origin + glm::vec3(0.0f, length, 0.0f), {0.1f, 1.0f, 0.1f, 1.0f}, on_top);
    line(origin, origin + glm::vec3(0.0f, 0.0f, length), {0.2f, 0.4f, 1.0f, 1.0f}, on_top);
}

void DebugLineBuffer::circle(glm::vec3 center, glm::vec3 normal, float radius, glm::vec4 color, int segments,
                             bool on_top) {
    segments = std::max(segments, 3);
    const glm::vec3 n = glm::normalize(normal);
    // Two directions across the circle's plane.
    const glm::vec3 helper = std::abs(n.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 u = glm::normalize(glm::cross(n, helper));
    const glm::vec3 v = glm::cross(n, u);
    glm::vec3 previous = center + u * radius;
    for (int k = 1; k <= segments; ++k) {
        const float angle = glm::two_pi<float>() * static_cast<float>(k) / static_cast<float>(segments);
        const glm::vec3 point = center + (u * std::cos(angle) + v * std::sin(angle)) * radius;
        line(previous, point, color, on_top);
        previous = point;
    }
}

void DebugLineBuffer::sphere(glm::vec3 center, float radius, glm::vec4 color, bool on_top) {
    circle(center, {1.0f, 0.0f, 0.0f}, radius, color, 32, on_top);
    circle(center, {0.0f, 1.0f, 0.0f}, radius, color, 32, on_top);
    circle(center, {0.0f, 0.0f, 1.0f}, radius, color, 32, on_top);
}

DebugLineRenderer::DebugLineRenderer(Renderer& renderer, SDL_GPUTextureFormat color_format,
                                     SDL_GPUTextureFormat depth_format)
    : renderer_(renderer), color_format_(color_format), depth_format_(depth_format) {
    ShaderInfo vertex_info;
    vertex_info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    vertex_info.uniform_buffers = 1;
    ShaderInfo fragment_info;
    fragment_info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    // Kept: the pipelines are made again when the number of samples changes (MSAA).
    vertex_shader_ = renderer.load_shader("debug_line.vert", vertex_info);
    fragment_shader_ = renderer.load_shader("debug_line.frag", fragment_info);
    create_pipelines(SDL_GPU_SAMPLECOUNT_1);
}

void DebugLineRenderer::create_pipelines(SDL_GPUSampleCount samples) {
    SDL_GPUVertexBufferDescription vertex_buffer = {};
    vertex_buffer.slot = 0;
    vertex_buffer.pitch = sizeof(DebugLineVertex);
    vertex_buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_GPUVertexAttribute attributes[2] = {};
    attributes[0].location = 0;
    attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[0].offset = offsetof(DebugLineVertex, position);
    attributes[1].location = 1;
    attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    attributes[1].offset = offsetof(DebugLineVertex, color);

    SDL_GPUColorTargetDescription color_target = {};
    color_target.format = color_format_;
    SDL_GPUGraphicsPipelineCreateInfo info = {};
    info.vertex_shader = vertex_shader_.get();
    info.fragment_shader = fragment_shader_.get();
    info.vertex_input_state.vertex_buffer_descriptions = &vertex_buffer;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = attributes;
    info.vertex_input_state.num_vertex_attributes = 2;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_LINELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.multisample_state.sample_count = samples;
    info.depth_stencil_state.enable_depth_test = true;
    info.depth_stencil_state.enable_depth_write = false;
    info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    info.target_info.color_target_descriptions = &color_target;
    info.target_info.num_color_targets = 1;
    info.target_info.has_depth_stencil_target = true;
    info.target_info.depth_stencil_format = depth_format_;
    const auto create = [&](const char* name) {
        const NameProperty name_property(SDL_PROP_GPU_GRAPHICSPIPELINE_CREATE_NAME_STRING, name);
        info.props = name_property.id();
        SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(renderer_.device(), &info);
        if (pipeline == nullptr) {
            throw std::runtime_error(std::string("SDL_CreateGPUGraphicsPipeline (") + name + ") failed: " + SDL_GetError());
        }
        return GpuGraphicsPipeline(renderer_.device(), pipeline);
    };
    depth_tested_pipeline_ = create("debug line pipeline");
    info.depth_stencil_state.enable_depth_test = false;
    on_top_pipeline_ = create("debug line pipeline (on top)");
}

void DebugLineRenderer::prepare(SDL_GPUCommandBuffer* commands, RenderStats& stats) {
    if (!has_work()) {
        return;
    }
    const std::size_t count = lines_.depth_tested().size() + lines_.on_top().size();
    if (count > capacity_) {
        std::size_t capacity = std::max<std::size_t>(capacity_, 4096);
        while (capacity < count) {
            capacity *= 2;
        }
        vertex_buffer_ = renderer_.create_buffer(SDL_GPU_BUFFERUSAGE_VERTEX, capacity * sizeof(DebugLineVertex),
                                                 "debug lines");
        transfer_buffer_ = renderer_.create_transfer_buffer(capacity * sizeof(DebugLineVertex));
        capacity_ = capacity;
    }
    SDL_GPUDevice* device = renderer_.device();
    auto* mapped = static_cast<DebugLineVertex*>(SDL_MapGPUTransferBuffer(device, transfer_buffer_.get(), true));
    if (mapped == nullptr) {
        throw std::runtime_error(std::string("SDL_MapGPUTransferBuffer (debug lines) failed: ") + SDL_GetError());
    }
    std::copy(lines_.depth_tested().begin(), lines_.depth_tested().end(), mapped);
    std::copy(lines_.on_top().begin(), lines_.on_top().end(), mapped + lines_.depth_tested().size());
    SDL_UnmapGPUTransferBuffer(device, transfer_buffer_.get());
    const std::size_t bytes = count * sizeof(DebugLineVertex);
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(commands);
    const SDL_GPUTransferBufferLocation source = {transfer_buffer_.get(), 0};
    const SDL_GPUBufferRegion destination = {vertex_buffer_.get(), 0, static_cast<Uint32>(bytes)};
    SDL_UploadToGPUBuffer(copy_pass, &source, &destination, true);
    SDL_EndGPUCopyPass(copy_pass);
    stats.bytes_uploaded += bytes;
}

void DebugLineRenderer::render(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, RenderStats& stats) {
    if (!has_work()) {
        return;
    }
    SDL_PushGPUVertexUniformData(commands, 0, &view_projection_, sizeof(view_projection_));
    const SDL_GPUBufferBinding binding = {vertex_buffer_.get(), 0};
    const auto tested = static_cast<Uint32>(lines_.depth_tested().size());
    const auto on_top = static_cast<Uint32>(lines_.on_top().size());
    if (tested > 0) {
        SDL_BindGPUGraphicsPipeline(pass, depth_tested_pipeline_.get());
        SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
        SDL_DrawGPUPrimitives(pass, tested, 1, 0, 0);
        ++stats.draw_calls;
    }
    if (on_top > 0) {
        SDL_BindGPUGraphicsPipeline(pass, on_top_pipeline_.get());
        SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
        SDL_DrawGPUPrimitives(pass, on_top, 1, tested, 0);
        ++stats.draw_calls;
    }
    stats.debug_lines += static_cast<int>(lines_.line_count());
}

}  // namespace moteur
