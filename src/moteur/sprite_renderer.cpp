#include "moteur/sprite_renderer.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "moteur/glyph_atlas.hpp"

namespace moteur {

namespace {

// How many sprites the vertex buffer holds at first. It grows when a frame needs more.
constexpr std::size_t kInitialCapacity = SpriteBatcher::kMaxQuadsPerRun;

}  // namespace

SpriteRenderer::SpriteRenderer(Renderer& renderer, SDL_GPUTextureFormat depth_format, std::string name)
    : renderer_(renderer), name_(std::move(name)) {
    ShaderInfo vertex_info;
    vertex_info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    vertex_info.uniform_buffers = 1;  // the view-projection matrix
    ShaderInfo fragment_info;
    fragment_info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fragment_info.samplers = 1;  // the sprite texture

    const GpuShader vertex_shader = renderer.load_shader("sprite.vert", vertex_info);
    const GpuShader fragment_shader = renderer.load_shader("sprite.frag", fragment_info);

    // Pixel art: keep hard edges.
    sampler_ = renderer.create_sampler(SDL_GPU_FILTER_NEAREST, (name_ + ".sampler").c_str());

    // Static index buffer: the same six indices for every quad, offset by four vertices per quad.
    // A draw call reaches quads beyond the first ones through the base-vertex offset it is given.
    std::vector<std::uint16_t> indices;
    indices.reserve(static_cast<std::size_t>(SpriteBatcher::kMaxQuadsPerRun) * 6);
    for (std::uint32_t quad = 0; quad < SpriteBatcher::kMaxQuadsPerRun; ++quad) {
        const auto base = static_cast<int>(quad * 4);
        for (const int corner : {0, 1, 2, 0, 2, 3}) {
            indices.push_back(static_cast<std::uint16_t>(base + corner));
        }
    }
    index_buffer_ = renderer.create_buffer(SDL_GPU_BUFFERUSAGE_INDEX, indices.data(),
                                           indices.size() * sizeof(std::uint16_t), (name_ + ".indices").c_str());

    ensure_capacity(kInitialCapacity);
    glyph_sampler_ = renderer.create_sampler(SDL_GPU_FILTER_LINEAR, (name_ + ".glyph sampler").c_str());

    SDL_GPUVertexBufferDescription vertex_buffer = {};
    vertex_buffer.slot = 0;
    vertex_buffer.pitch = sizeof(SpriteVertex);
    vertex_buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_GPUVertexAttribute attributes[3] = {};
    attributes[0].location = 0;  // position, matches TEXCOORD0 in the vertex shader
    attributes[0].buffer_slot = 0;
    attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attributes[0].offset = offsetof(SpriteVertex, x);
    attributes[1].location = 1;  // uv, matches TEXCOORD1
    attributes[1].buffer_slot = 0;
    attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attributes[1].offset = offsetof(SpriteVertex, u);
    attributes[2].location = 2;  // tint, matches TEXCOORD2: four bytes read as floats in [0, 1]
    attributes[2].buffer_slot = 0;
    attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
    attributes[2].offset = offsetof(SpriteVertex, r);

    // Premultiplied alpha blending: textures hold color already multiplied by alpha, so the source
    // is added as it is and only the background is attenuated. Transparent pixels let the
    // background show through.
    SDL_GPUColorTargetDescription color_target = {};
    color_target.format = renderer.swapchain_format();
    color_target.blend_state.enable_blend = true;
    color_target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    color_target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    color_target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    color_target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    color_target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    color_target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

    SDL_GPUGraphicsPipelineCreateInfo info = {};
    info.vertex_shader = vertex_shader.get();
    info.fragment_shader = fragment_shader.get();
    info.vertex_input_state.vertex_buffer_descriptions = &vertex_buffer;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = attributes;
    info.vertex_input_state.num_vertex_attributes = 3;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.target_info.color_target_descriptions = &color_target;
    info.target_info.num_color_targets = 1;
    if (depth_format != SDL_GPU_TEXTUREFORMAT_INVALID) {
        // Declared to match the pass; depth test and write stay off (the defaults), so sprites
        // keep being ordered by their own depth key, not by the depth buffer.
        info.target_info.has_depth_stencil_target = true;
        info.target_info.depth_stencil_format = depth_format;
    }

    // The pipeline keeps what it needs from the shaders, which are released when this returns.
    const std::string pipeline_name = name_ + " pipeline";
    const NameProperty name_property(SDL_PROP_GPU_GRAPHICSPIPELINE_CREATE_NAME_STRING, pipeline_name.c_str());
    info.props = name_property.id();
    SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(renderer.device(), &info);
    if (pipeline == nullptr) {
        throw std::runtime_error(std::string("SDL_CreateGPUGraphicsPipeline failed: ") + SDL_GetError());
    }
    pipeline_ = GpuGraphicsPipeline(renderer.device(), pipeline);

    // The glyph pipeline: same blending and targets, its own shaders and vertices.
    ShaderInfo glyph_vertex_info;
    glyph_vertex_info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    glyph_vertex_info.uniform_buffers = 1;
    ShaderInfo glyph_fragment_info;
    glyph_fragment_info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    glyph_fragment_info.samplers = 1;          // the atlas page
    glyph_fragment_info.uniform_buffers = 1;   // its size and distance range
    const GpuShader glyph_vertex_shader = renderer.load_shader("text.vert", glyph_vertex_info);
    const GpuShader glyph_fragment_shader = renderer.load_shader("text.frag", glyph_fragment_info);

    SDL_GPUVertexBufferDescription glyph_buffer = {};
    glyph_buffer.slot = 0;
    glyph_buffer.pitch = sizeof(GlyphVertex);
    glyph_buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_GPUVertexAttribute glyph_attributes[5] = {};
    glyph_attributes[0] = {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, static_cast<Uint32>(offsetof(GlyphVertex, x))};
    glyph_attributes[1] = {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, static_cast<Uint32>(offsetof(GlyphVertex, u))};
    glyph_attributes[2] = {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, static_cast<Uint32>(offsetof(GlyphVertex, r))};
    glyph_attributes[3] = {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM,
                           static_cast<Uint32>(offsetof(GlyphVertex, outline_r))};
    glyph_attributes[4] = {4, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, static_cast<Uint32>(offsetof(GlyphVertex, dilate))};

    SDL_GPUGraphicsPipelineCreateInfo glyph_info = info;
    glyph_info.vertex_shader = glyph_vertex_shader.get();
    glyph_info.fragment_shader = glyph_fragment_shader.get();
    glyph_info.vertex_input_state.vertex_buffer_descriptions = &glyph_buffer;
    glyph_info.vertex_input_state.vertex_attributes = glyph_attributes;
    glyph_info.vertex_input_state.num_vertex_attributes = 5;
    const std::string glyph_pipeline_name = name_ + " glyph pipeline";
    const NameProperty glyph_name_property(SDL_PROP_GPU_GRAPHICSPIPELINE_CREATE_NAME_STRING, glyph_pipeline_name.c_str());
    glyph_info.props = glyph_name_property.id();
    SDL_GPUGraphicsPipeline* glyph_pipeline = SDL_CreateGPUGraphicsPipeline(renderer.device(), &glyph_info);
    if (glyph_pipeline == nullptr) {
        throw std::runtime_error(std::string("SDL_CreateGPUGraphicsPipeline (glyphs) failed: ") + SDL_GetError());
    }
    glyph_pipeline_ = GpuGraphicsPipeline(renderer.device(), glyph_pipeline);
}

void SpriteRenderer::draw(const Texture& texture, glm::vec2 position, glm::vec2 size,
                          const SpriteOptions& options) {
    SpriteDesc sprite;
    sprite.texture = texture.gpu.get();
    sprite.position = position;
    sprite.size = size;
    sprite.uv_rect = options.uv_rect;
    sprite.tint = pack_color(options.tint);
    sprite.depth = options.depth;
    sprite.flip_x = options.flip_x;
    sprite.flip_y = options.flip_y;
    if (options.clip) {
        sprite.clip = *options.clip;
        sprite.has_clip = true;
    }
    batcher_.add(sprite);
}

void SpriteRenderer::draw(const SpriteRegion& region, glm::vec2 anchor, float scale, const SpriteOptions& options) {
    const PlacedSprite placed = place_region(region, anchor, scale, options.flip_x, options.flip_y);
    SpriteOptions with_region = options;
    with_region.uv_rect = region.uv_rect;
    draw(*region.texture, placed.position, placed.size, with_region);
}

void SpriteRenderer::draw_glyph(const Texture& page, glm::vec2 position, glm::vec2 size, glm::vec4 uv,
                                const GlyphLook& look, float depth, float top_shift, float bottom_shift,
                                const std::optional<glm::vec4>& clip) {
    SpriteDesc sprite;
    sprite.texture = page.gpu.get();
    sprite.position = position;
    sprite.size = size;
    sprite.uv_rect = uv;
    sprite.tint = pack_color(look.color);
    sprite.depth = depth;
    sprite.top_shift = top_shift;
    sprite.bottom_shift = bottom_shift;
    if (clip) {
        sprite.clip = *clip;
        sprite.has_clip = true;
    }
    GlyphStyle style;
    style.outline_color = pack_color(look.outline_color);
    style.dilate = look.dilate;
    style.outline = look.outline;
    style.softness = look.softness;
    batcher_.add_glyph(sprite, style);
}

void SpriteRenderer::ensure_glyph_capacity(std::size_t glyphs) {
    if (glyphs <= glyph_capacity_) {
        return;
    }
    std::size_t capacity = std::max<std::size_t>(glyph_capacity_, 1024);
    while (capacity < glyphs) {
        capacity *= 2;
    }
    const std::size_t bytes = capacity * 4 * sizeof(GlyphVertex);
    glyph_vertex_buffer_ = renderer_.create_buffer(SDL_GPU_BUFFERUSAGE_VERTEX, bytes, (name_ + ".glyph vertices").c_str());
    glyph_transfer_buffer_ = renderer_.create_transfer_buffer(bytes);
    glyph_capacity_ = capacity;
}

void SpriteRenderer::ensure_capacity(std::size_t sprites) {
    if (sprites <= capacity_) {
        return;
    }
    // Doubling keeps growth rare. The old buffers are released as soon as the GPU is done with them.
    std::size_t capacity = std::max<std::size_t>(capacity_, kInitialCapacity);
    while (capacity < sprites) {
        capacity *= 2;
    }
    const std::size_t bytes = capacity * 4 * sizeof(SpriteVertex);
    vertex_buffer_ = renderer_.create_buffer(SDL_GPU_BUFFERUSAGE_VERTEX, bytes, (name_ + ".vertices").c_str());
    transfer_buffer_ = renderer_.create_transfer_buffer(bytes);
    if (capacity_ != 0) {
        SDL_Log("Sprite buffer grown from %zu to %zu sprites (%.1f MiB, %s)", capacity_, capacity,
                static_cast<double>(bytes) / (1024.0 * 1024.0), name_.c_str());
    }
    capacity_ = capacity;
}

void SpriteRenderer::prepare(SDL_GPUCommandBuffer* commands, RenderStats& stats) {
    batcher_.finish();
    if (batcher_.sprite_count() == 0) {
        return;
    }
    const std::size_t sprites = batcher_.sprite_count() - batcher_.glyph_count();
    const std::size_t glyphs = batcher_.glyph_count();
    ensure_capacity(sprites);
    ensure_glyph_capacity(glyphs);

    // Write the vertices into the staging buffers. `cycle = true` lets SDL hand out a fresh
    // buffer if the previous frame's is still being read by the GPU, instead of waiting for it.
    SDL_GPUDevice* device = renderer_.device();
    const auto stage = [&](GpuTransferBuffer& transfer, const void* data, std::size_t bytes) {
        void* mapped = SDL_MapGPUTransferBuffer(device, transfer.get(), true);
        if (mapped == nullptr) {
            throw std::runtime_error(std::string("SDL_MapGPUTransferBuffer failed: ") + SDL_GetError());
        }
        std::memcpy(mapped, data, bytes);
        SDL_UnmapGPUTransferBuffer(device, transfer.get());
    };
    const std::size_t sprite_bytes = sprites * 4 * sizeof(SpriteVertex);
    const std::size_t glyph_bytes = glyphs * 4 * sizeof(GlyphVertex);
    if (sprite_bytes > 0) {
        stage(transfer_buffer_, batcher_.vertices().data(), sprite_bytes);
    }
    if (glyph_bytes > 0) {
        stage(glyph_transfer_buffer_, batcher_.glyph_vertices().data(), glyph_bytes);
    }

    // The copies happen in their own pass, before the render pass opens. Only the used part is sent.
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(commands);
    if (sprite_bytes > 0) {
        const SDL_GPUTransferBufferLocation source = {transfer_buffer_.get(), 0};
        const SDL_GPUBufferRegion destination = {vertex_buffer_.get(), 0, static_cast<Uint32>(sprite_bytes)};
        SDL_UploadToGPUBuffer(copy_pass, &source, &destination, true);
    }
    if (glyph_bytes > 0) {
        const SDL_GPUTransferBufferLocation source = {glyph_transfer_buffer_.get(), 0};
        const SDL_GPUBufferRegion destination = {glyph_vertex_buffer_.get(), 0, static_cast<Uint32>(glyph_bytes)};
        SDL_UploadToGPUBuffer(copy_pass, &source, &destination, true);
    }
    SDL_EndGPUCopyPass(copy_pass);

    stats.bytes_uploaded += sprite_bytes + glyph_bytes;
}

void SpriteRenderer::render(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, std::uint32_t width,
                            std::uint32_t height, RenderStats& stats) {
    if (batcher_.sprite_count() == 0) {
        return;
    }

    const SDL_GPUBufferBinding indices = {index_buffer_.get(), 0};

    // One matrix for the whole frame. Unless the game gave one (a camera), sprites are placed in
    // window pixels: origin at the top left, y pointing down. That default follows the swapchain
    // size, so resizing the window keeps sprites unscaled.
    const glm::mat4 projection =
        has_view_projection_
            ? view_projection_
            : glm::orthoRH_ZO(0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, -1.0f, 1.0f);
    // The atlas page's size and distance range, for the glyphs' screen-space distance.
    const float atlas_info[4] = {static_cast<float>(GlyphAtlas::kPageSize), static_cast<float>(GlyphAtlas::kPageSize),
                                 GlyphAtlas::kRangeTexels, 0.0f};

    const void* bound_texture = nullptr;
    int bound_kind = -1;  // 0: sprites, 1: glyphs
    for (const SpriteRun& run : batcher_.runs()) {
        const int kind = run.glyphs ? 1 : 0;
        if (kind != bound_kind) {
            // Uniforms are pushed after the bind: a pipeline change may reset them.
            SDL_BindGPUGraphicsPipeline(pass, run.glyphs ? glyph_pipeline_.get() : pipeline_.get());
            const SDL_GPUBufferBinding vertices = {run.glyphs ? glyph_vertex_buffer_.get() : vertex_buffer_.get(), 0};
            SDL_BindGPUVertexBuffers(pass, 0, &vertices, 1);
            SDL_BindGPUIndexBuffer(pass, &indices, SDL_GPU_INDEXELEMENTSIZE_16BIT);
            SDL_PushGPUVertexUniformData(commands, 0, glm::value_ptr(projection), sizeof(projection));
            if (run.glyphs) {
                SDL_PushGPUFragmentUniformData(commands, 0, atlas_info, sizeof(atlas_info));
            }
            bound_kind = kind;
            bound_texture = nullptr;
        }
        if (run.texture != bound_texture) {
            const SDL_GPUTextureSamplerBinding binding = {static_cast<SDL_GPUTexture*>(const_cast<void*>(run.texture)),
                                                          run.glyphs ? glyph_sampler_.get() : sampler_.get()};
            SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
            bound_texture = run.texture;
        }
        // The index buffer only knows the first quads: the base-vertex offset moves it to this run.
        SDL_DrawGPUIndexedPrimitives(pass, run.count * 6, 1, 0, static_cast<Sint32>(run.first_sprite * 4), 0);
        ++stats.draw_calls;
    }
    stats.sprites += static_cast<int>(batcher_.sprite_count());
}

}  // namespace moteur
