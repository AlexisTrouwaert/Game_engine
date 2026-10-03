#include "moteur/mesh_renderer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

#include "moteur/debug_lines.hpp"
#include "moteur/image.hpp"
#include "moteur/mesh.hpp"
#include "moteur/model.hpp"

namespace moteur {

namespace {

// Uniform block, laid out exactly like the cbuffer of mesh.frag.hlsl: only float4x4 and float4
// members, so no padding rule can differ between C++ and HLSL. The vertex shaders (mesh.vert.hlsl,
// shadow.vert.hlsl) receive a single float4x4: the view-projection of their pass.
struct FrameUniforms {
    glm::vec4 eye;                // xyz: camera position; w: number of point lights
    glm::vec4 to_sun;             // xyz
    glm::vec4 sun;                // rgb: color x intensity
    glm::vec4 environment;        // x: intensity, y: highest specular mip level
    glm::vec4 irradiance[9];      // spherical harmonics, rgb
    glm::vec4 light_position[MeshRenderer::kMaxPointLights];  // xyz, w: range
    glm::vec4 light_color[MeshRenderer::kMaxPointLights];     // rgb: color x intensity
    glm::mat4 light_view_projection;  // world -> the sun's shadow map
    glm::vec4 shadow;                 // x: on, y: metres per texel, z: normal offset (texels), w: depth bias
    glm::mat4 point_shadow_faces[MeshRenderer::kMaxShadowedPointLights * 6];  // row * 6 + face
    glm::vec4 point_shadow_tiles;   // x, y: tile size in atlas coordinates; z: 1 / tile texels; w: normal offset (texels)
    glm::vec4 point_shadow_params;  // x, y: texel size in atlas coordinates; z: texel size per metre; w: depth bias (m)
    glm::vec4 debug_view;           // x: MeshView; y: MeshRenderer::kDistanceViewRange
    glm::vec4 fog;                  // x on, y never seen, z explored, w saturation
    glm::vec4 fog_rect;             // xy: corner (x, z); zw: 1 / size in metres
    glm::vec4 cutout;               // xyz: focus; w: radius, 0 off
};

constexpr std::size_t kInitialInstances = 1024;

// A depth texture that can be drawn into and sampled (shadow maps).
GpuTexture create_depth_texture(SDL_GPUDevice* device, SDL_GPUTextureFormat format, int width, int height,
                                const char* name) {
    SDL_GPUTextureCreateInfo info = {};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = format;
    info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = static_cast<Uint32>(width);
    info.height = static_cast<Uint32>(height);
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    const NameProperty name_property(SDL_PROP_GPU_TEXTURE_CREATE_NAME_STRING, name);
    info.props = name_property.id();
    SDL_GPUTexture* raw = SDL_CreateGPUTexture(device, &info);
    if (raw == nullptr) {
        throw std::runtime_error(std::string("SDL_CreateGPUTexture (") + name + ") failed: " + SDL_GetError());
    }
    return GpuTexture(device, raw);
}

// FNV-1a over raw bytes: the signature of a point light's shadow.
void hash_bytes(std::uint64_t& hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash = (hash ^ bytes[i]) * 1099511628211ull;
    }
}

constexpr int kMaterialTextures = 5;  // then the environment in slot 5, the shadow maps in 6 and 7, the fog in 8

// The most precise depth format that can be both drawn into and sampled: SDL guarantees D16 for
// sampling, not D32.
SDL_GPUTextureFormat pick_shadow_format(SDL_GPUDevice* device) {
    const SDL_GPUTextureUsageFlags usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    return SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTURETYPE_2D, usage)
               ? SDL_GPU_TEXTUREFORMAT_D32_FLOAT
               : SDL_GPU_TEXTUREFORMAT_D16_UNORM;
}

Texture one_pixel(Renderer& renderer, std::uint8_t r, std::uint8_t g, std::uint8_t b, const char* name) {
    Image image;
    image.width = 1;
    image.height = 1;
    image.pixels = {r, g, b, 255};
    TextureSettings settings;
    settings.premultiply = false;
    return renderer.create_texture(image, settings, name);
}

// The vertex inputs of the mesh pipelines. Slot 0: the mesh's vertices. Slot 1: one MeshInstance
// per instance. Slot 2, for skinned meshes only: one VertexSkin per vertex (locations 13 and 14).
struct MeshVertexInput {
    static constexpr int kInstanceAttributes = 9;  // locations 4 to 12: nine float4 (see MeshInstance)
    static constexpr int kAttributes = 4 + kInstanceAttributes;
    static constexpr int kSkinnedAttributes = kAttributes + 2;
    SDL_GPUVertexBufferDescription buffers[3] = {};
    SDL_GPUVertexAttribute attributes[kSkinnedAttributes] = {};

    MeshVertexInput() {
        buffers[0].slot = 0;
        buffers[0].pitch = sizeof(Vertex3D);
        buffers[0].input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
        buffers[1].slot = 1;
        buffers[1].pitch = sizeof(MeshInstance);
        buffers[1].input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE;
        attributes[0].location = 0;  // position, TEXCOORD0
        attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        attributes[0].offset = offsetof(Vertex3D, position);
        attributes[1].location = 1;  // normal, TEXCOORD1
        attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        attributes[1].offset = offsetof(Vertex3D, normal);
        attributes[2].location = 2;  // uv, TEXCOORD2
        attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        attributes[2].offset = offsetof(Vertex3D, uv);
        attributes[3].location = 3;  // tangent, TEXCOORD3
        attributes[3].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        attributes[3].offset = offsetof(Vertex3D, tangent);
        for (int i = 0; i < kInstanceAttributes; ++i) {
            SDL_GPUVertexAttribute& attribute = attributes[4 + i];
            attribute.location = static_cast<Uint32>(4 + i);
            attribute.buffer_slot = 1;
            attribute.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
            attribute.offset = static_cast<Uint32>(i * sizeof(glm::vec4));
        }
        buffers[2].slot = 2;
        buffers[2].pitch = sizeof(VertexSkin);
        buffers[2].input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
        attributes[kAttributes].location = kAttributes;  // joints, TEXCOORD13: read as uint4
        attributes[kAttributes].buffer_slot = 2;
        attributes[kAttributes].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4;
        attributes[kAttributes].offset = offsetof(VertexSkin, joints);
        attributes[kAttributes + 1].location = kAttributes + 1;  // weights, TEXCOORD14: 65535ths read as [0, 1]
        attributes[kAttributes + 1].buffer_slot = 2;
        attributes[kAttributes + 1].format = SDL_GPU_VERTEXELEMENTFORMAT_USHORT4_NORM;
        attributes[kAttributes + 1].offset = offsetof(VertexSkin, weights);
    }
};

constexpr int kEmissiveAttribute = 4 + 8;  // the instance's emissive row: its w is the palette of a skinned one

}  // namespace

MeshRenderer::MeshRenderer(Renderer& renderer, SDL_GPUTextureFormat color_format, SDL_GPUTextureFormat depth_format)
    : renderer_(renderer), device_(renderer.device()), color_format_(color_format), depth_format_(depth_format) {
    ShaderInfo vertex_info;
    vertex_info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    vertex_info.uniform_buffers = 1;
    ShaderInfo fragment_info;
    fragment_info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fragment_info.uniform_buffers = 1;               // frame
    fragment_info.samplers = kMaterialTextures + 4;  // + the environment, the shadows (sun, points), the fog

    // Kept: the scene pipelines are made again when the number of samples changes (MSAA).
    vertex_shader_ = renderer.load_shader("mesh.vert", vertex_info);
    ShaderInfo skinned_info = vertex_info;
    skinned_info.storage_buffers = 1;  // the palettes
    skinned_vertex_shader_ = renderer.load_shader("mesh_skinned.vert", skinned_info);
    fragment_shader_ = renderer.load_shader("mesh.frag", fragment_info);
    create_scene_pipelines(SDL_GPU_SAMPLECOUNT_1);
    const MeshVertexInput input;

    // Material textures tile: repeat beyond [0, 1]. Trilinear filtering between mipmaps, and
    // anisotropic filtering: the ground is seen at a grazing angle, where plain trilinear blurs.
    SDL_GPUSamplerCreateInfo sampler_info = {};
    sampler_info.min_filter = SDL_GPU_FILTER_LINEAR;
    sampler_info.mag_filter = SDL_GPU_FILTER_LINEAR;
    sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sampler_info.enable_anisotropy = true;
    sampler_info.max_anisotropy = 8.0f;
    sampler_info.max_lod = 1000.0f;  // every mipmap level
    const NameProperty sampler_name(SDL_PROP_GPU_SAMPLER_CREATE_NAME_STRING, "mesh.sampler");
    sampler_info.props = sampler_name.id();
    SDL_GPUSampler* sampler = SDL_CreateGPUSampler(renderer.device(), &sampler_info);
    if (sampler == nullptr) {
        throw std::runtime_error(std::string("SDL_CreateGPUSampler (mesh) failed: ") + SDL_GetError());
    }
    material_sampler_ = GpuSampler(renderer.device(), sampler);

    // The environment wraps around horizontally and stops at the poles; its mip level is chosen by
    // the shader (roughness), so no anisotropy.
    sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.enable_anisotropy = false;
    sampler_info.max_anisotropy = 1.0f;
    const NameProperty environment_name(SDL_PROP_GPU_SAMPLER_CREATE_NAME_STRING, "environment.sampler");
    sampler_info.props = environment_name.id();
    sampler = SDL_CreateGPUSampler(renderer.device(), &sampler_info);
    if (sampler == nullptr) {
        throw std::runtime_error(std::string("SDL_CreateGPUSampler (environment) failed: ") + SDL_GetError());
    }
    environment_sampler_ = GpuSampler(renderer.device(), sampler);

    // The shadow pass: depth only, from the sun. The slope bias pushes the stored depth away on
    // surfaces seen at a grazing angle from the sun, where self-shadowing ("acne") appears first.
    shadow_format_ = pick_shadow_format(device_);
    {
        ShaderInfo shadow_vertex_info;
        shadow_vertex_info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        shadow_vertex_info.uniform_buffers = 1;
        ShaderInfo shadow_fragment_info;
        shadow_fragment_info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        const GpuShader shadow_vertex = renderer.load_shader("shadow.vert", shadow_vertex_info);
        const GpuShader shadow_fragment = renderer.load_shader("shadow.frag", shadow_fragment_info);

        SDL_GPUGraphicsPipelineCreateInfo shadow = {};
        shadow.vertex_shader = shadow_vertex.get();
        shadow.fragment_shader = shadow_fragment.get();
        // The position (location 0) and the rows of the world matrix (locations 1 to 3: the
        // locations of a shader's inputs must be contiguous, see shadow.vert.hlsl).
        SDL_GPUVertexAttribute shadow_attributes[4] = {input.attributes[0], input.attributes[4], input.attributes[5],
                                                       input.attributes[6]};
        for (Uint32 i = 1; i < 4; ++i) {
            shadow_attributes[i].location = i;
        }
        shadow.vertex_input_state.vertex_buffer_descriptions = input.buffers;
        shadow.vertex_input_state.num_vertex_buffers = 2;
        shadow.vertex_input_state.vertex_attributes = shadow_attributes;
        shadow.vertex_input_state.num_vertex_attributes = 4;
        shadow.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        shadow.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        shadow.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
        shadow.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        shadow.rasterizer_state.enable_depth_bias = true;
        shadow.rasterizer_state.depth_bias_constant_factor = 1.0f;
        shadow.rasterizer_state.depth_bias_slope_factor = 2.0f;
        shadow.depth_stencil_state.enable_depth_test = true;
        shadow.depth_stencil_state.enable_depth_write = true;
        shadow.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
        shadow.target_info.has_depth_stencil_target = true;
        shadow.target_info.depth_stencil_format = shadow_format_;
        const auto create_shadow = [&](const char* name) {
            const NameProperty name_property(SDL_PROP_GPU_GRAPHICSPIPELINE_CREATE_NAME_STRING, name);
            shadow.props = name_property.id();
            SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device_, &shadow);
            if (pipeline == nullptr) {
                throw std::runtime_error(std::string("SDL_CreateGPUGraphicsPipeline (") + name + ") failed: " + SDL_GetError());
            }
            return GpuGraphicsPipeline(device_, pipeline);
        };
        shadow_single_sided_ = create_shadow("shadow pipeline");
        shadow.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        shadow_double_sided_ = create_shadow("shadow pipeline (double sided)");

        // Skinned: the palette (the instance's emissive row, location 4) and the vertex's joints and
        // weights (5 and 6) as well, from the skin slot.
        ShaderInfo skinned_shadow_info = shadow_vertex_info;
        skinned_shadow_info.storage_buffers = 1;
        const GpuShader shadow_skinned_vertex = renderer.load_shader("shadow_skinned.vert", skinned_shadow_info);
        SDL_GPUVertexAttribute skinned_attributes[7] = {shadow_attributes[0], shadow_attributes[1], shadow_attributes[2],
                                                        shadow_attributes[3], input.attributes[kEmissiveAttribute],
                                                        input.attributes[MeshVertexInput::kAttributes],
                                                        input.attributes[MeshVertexInput::kAttributes + 1]};
        for (Uint32 i = 4; i < 7; ++i) {
            skinned_attributes[i].location = i;
        }
        const auto create_skinned = [&](const GpuShader& vertex, const char* name) {
            SDL_GPUGraphicsPipelineCreateInfo skinned = shadow;
            skinned.vertex_shader = vertex.get();
            skinned.vertex_input_state.num_vertex_buffers = 3;
            skinned.vertex_input_state.vertex_attributes = skinned_attributes;
            skinned.vertex_input_state.num_vertex_attributes = 7;
            const NameProperty name_property(SDL_PROP_GPU_GRAPHICSPIPELINE_CREATE_NAME_STRING, name);
            skinned.props = name_property.id();
            SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device_, &skinned);
            if (pipeline == nullptr) {
                throw std::runtime_error(std::string("SDL_CreateGPUGraphicsPipeline (") + name + ") failed: " + SDL_GetError());
            }
            return GpuGraphicsPipeline(device_, pipeline);
        };
        shadow.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
        shadow_skinned_single_sided_ = create_skinned(shadow_skinned_vertex, "shadow pipeline (skinned)");
        shadow.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        shadow_skinned_double_sided_ = create_skinned(shadow_skinned_vertex, "shadow pipeline (skinned, double sided)");

        // Point light shadows: the same inputs, but the depth written is the distance to the light
        // (point_shadow.frag.hlsl), so no slope bias: the bias is a length, applied when shading.
        ShaderInfo point_fragment_info;
        point_fragment_info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        point_fragment_info.uniform_buffers = 1;
        const GpuShader point_vertex = renderer.load_shader("point_shadow.vert", shadow_vertex_info);
        const GpuShader point_fragment = renderer.load_shader("point_shadow.frag", point_fragment_info);
        shadow.vertex_shader = point_vertex.get();
        shadow.fragment_shader = point_fragment.get();
        shadow.rasterizer_state.enable_depth_bias = false;
        shadow.rasterizer_state.depth_bias_constant_factor = 0.0f;
        shadow.rasterizer_state.depth_bias_slope_factor = 0.0f;
        shadow.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
        point_single_sided_ = create_shadow("point shadow pipeline");
        shadow.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        point_double_sided_ = create_shadow("point shadow pipeline (double sided)");
        const GpuShader point_skinned_vertex = renderer.load_shader("point_shadow_skinned.vert", skinned_shadow_info);
        shadow.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
        point_skinned_single_sided_ = create_skinned(point_skinned_vertex, "point shadow pipeline (skinned)");
        shadow.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        point_skinned_double_sided_ = create_skinned(point_skinned_vertex, "point shadow pipeline (skinned, double sided)");

        // Clearing one tile: a triangle at the far depth, no vertex input, always written.
        ShaderInfo clear_vertex_info;
        clear_vertex_info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        const GpuShader clear_vertex = renderer.load_shader("shadow_clear.vert", clear_vertex_info);
        shadow.vertex_shader = clear_vertex.get();
        shadow.fragment_shader = shadow_fragment.get();
        shadow.vertex_input_state = {};
        shadow.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
        point_clear_ = create_shadow("point shadow tile clear");
        point_atlas_stand_in_ = create_depth_texture(device_, shadow_format_, 1, 1, "point shadow atlas (none)");

        // Comparison with filtering: each lookup already blends the 2 x 2 nearest results (hardware PCF).
        SDL_GPUSamplerCreateInfo compare = {};
        compare.min_filter = SDL_GPU_FILTER_LINEAR;
        compare.mag_filter = SDL_GPU_FILTER_LINEAR;
        compare.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        compare.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        compare.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        compare.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        compare.enable_compare = true;
        compare.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        const NameProperty compare_name(SDL_PROP_GPU_SAMPLER_CREATE_NAME_STRING, "shadow.sampler");
        compare.props = compare_name.id();
        SDL_GPUSampler* compare_sampler = SDL_CreateGPUSampler(device_, &compare);
        if (compare_sampler == nullptr) {
            throw std::runtime_error(std::string("SDL_CreateGPUSampler (shadow) failed: ") + SDL_GetError());
        }
        shadow_sampler_ = GpuSampler(device_, compare_sampler);
    }

    white_ = one_pixel(renderer, 255, 255, 255, "mesh.white");
    flat_normal_ = one_pixel(renderer, 128, 128, 255, "mesh.flat normal");
    const std::vector<std::vector<std::uint8_t>> black(1, std::vector<std::uint8_t>(8, 0));  // one RGBA16F texel
    black_environment_ = renderer.create_texture_levels(SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, 1, 1, black,
                                                        "environment.black");
    fog_stand_in_ = renderer.create_texture_levels(SDL_GPU_TEXTUREFORMAT_R8_UNORM, 1, 1, {{255}}, "fog.none");
    fog_sampler_ = renderer.create_sampler(SDL_GPU_FILTER_LINEAR, "fog.sampler");
}

void MeshRenderer::set_fog_cells(int width, int height, const std::vector<std::uint8_t>& cells) {
    if (width <= 0 || height <= 0 || cells.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
        throw std::invalid_argument("MeshRenderer::set_fog_cells: " + std::to_string(cells.size()) + " bytes for " +
                                    std::to_string(width) + " x " + std::to_string(height) + " cells");
    }
    if (fog_size_ != glm::ivec2(width, height)) {
        fog_texture_ = renderer_.create_texture_levels(SDL_GPU_TEXTUREFORMAT_R8_UNORM, width, height, {cells}, "fog");
        fog_transfer_ = renderer_.create_transfer_buffer(cells.size());
        fog_size_ = {width, height};
        fog_dirty_ = false;
        return;
    }
    fog_cells_ = cells;
    fog_dirty_ = true;
}

void MeshRenderer::fog_uniforms(glm::vec4& fog, glm::vec4& rect) const {
    const bool on = fog_.enabled && fog_texture_;
    fog = glm::vec4(on ? 1.0f : 0.0f, fog_.unexplored, fog_.explored, fog_.saturation);
    const glm::vec2 size = glm::vec2(glm::max(fog_size_, glm::ivec2(1))) * fog_.cell_size;
    rect = glm::vec4(fog_.origin, 1.0f / size.x, 1.0f / size.y);
}

void MeshRenderer::create_scene_pipelines(SDL_GPUSampleCount samples) {
    const MeshVertexInput input;

    // Opaque: no blending. Linear HDR (see Renderer::kSceneFormat).
    SDL_GPUColorTargetDescription color_target = {};
    color_target.format = color_format_;

    SDL_GPUGraphicsPipelineCreateInfo info = {};
    info.vertex_shader = vertex_shader_.get();
    info.fragment_shader = fragment_shader_.get();
    info.vertex_input_state.vertex_buffer_descriptions = input.buffers;
    info.vertex_input_state.num_vertex_buffers = 2;
    info.vertex_input_state.vertex_attributes = input.attributes;
    info.vertex_input_state.num_vertex_attributes = 4 + MeshVertexInput::kInstanceAttributes;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    // glTF convention: front faces are counter-clockwise; the inside of closed shapes is never drawn.
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
    info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    info.multisample_state.sample_count = samples;
    info.depth_stencil_state.enable_depth_test = true;
    info.depth_stencil_state.enable_depth_write = true;
    info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    info.target_info.color_target_descriptions = &color_target;
    info.target_info.num_color_targets = 1;
    info.target_info.has_depth_stencil_target = true;
    info.target_info.depth_stencil_format = depth_format_;

    const auto create = [&](const char* name) {
        const NameProperty name_property(SDL_PROP_GPU_GRAPHICSPIPELINE_CREATE_NAME_STRING, name);
        info.props = name_property.id();
        SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device_, &info);
        if (pipeline == nullptr) {
            throw std::runtime_error(std::string("SDL_CreateGPUGraphicsPipeline (") + name + ") failed: " + SDL_GetError());
        }
        return GpuGraphicsPipeline(device_, pipeline);
    };
    single_sided_ = create("mesh pipeline");
    // Double-sided materials (leaves, cloth, and every Poly Haven model): back faces are drawn too,
    // and the shader turns their normal around.
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    double_sided_ = create("mesh pipeline (double sided)");
    // Debug view: the edges of the triangles only.
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_LINE;
    wireframe_double_sided_ = create("mesh pipeline (wireframe, double sided)");
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
    wireframe_single_sided_ = create("mesh pipeline (wireframe)");

    // Skinned meshes: the skin slot and its two attributes, and the palettes.
    info.vertex_shader = skinned_vertex_shader_.get();
    info.vertex_input_state.num_vertex_buffers = 3;
    info.vertex_input_state.num_vertex_attributes = MeshVertexInput::kSkinnedAttributes;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    skinned_single_sided_ = create("mesh pipeline (skinned)");
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    skinned_double_sided_ = create("mesh pipeline (skinned, double sided)");
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_LINE;
    skinned_wireframe_double_sided_ = create("mesh pipeline (skinned, wireframe, double sided)");
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
    skinned_wireframe_single_sided_ = create("mesh pipeline (skinned, wireframe)");
}

void MeshRenderer::set_sun(glm::vec3 to_light, glm::vec3 color, float intensity) {
    to_sun_ = glm::length(to_light) > 0.0f ? glm::normalize(to_light) : glm::vec3(0.0f, 1.0f, 0.0f);
    sun_color_ = color;
    sun_intensity_ = intensity;
}

void MeshRenderer::set_environment(const Environment* environment, float intensity) {
    environment_ = environment;
    environment_intensity_ = intensity;
}

void MeshRenderer::add_light(const PointLight& light) {
    if (static_cast<int>(lights_.size()) >= kMaxPointLights) {
        ++dropped_lights_;
        return;
    }
    lights_.push_back(light);
}

void MeshRenderer::draw(const Mesh& mesh, const glm::mat4& world, const Material& material) {
    draws_.push_back({&mesh, world, material, transform_box(mesh.bounds, world)});
}

void MeshRenderer::draw(const Mesh& mesh, const glm::mat4& world, const Material& material, const Aabb& bounds) {
    draws_.push_back({&mesh, world, material, bounds});
}

MeshRenderer::Palette MeshRenderer::add_palette(const std::vector<glm::mat4>& matrices) {
    Palette palette;
    palette.first = static_cast<std::int32_t>(palette_rows_.size() / 3);
    palette.size = static_cast<std::uint32_t>(matrices.size());
    // GLM stores columns: row r of m is (m[0][r], m[1][r], m[2][r], m[3][r]); the fourth row is 0, 0, 0, 1.
    for (const glm::mat4& m : matrices) {
        for (int r = 0; r < 3; ++r) {
            palette_rows_.emplace_back(m[0][r], m[1][r], m[2][r], m[3][r]);
        }
    }
    return palette;
}

void MeshRenderer::draw(const Mesh& mesh, const glm::mat4& world, const Material& material, const Aabb& bounds,
                        Palette palette) {
    MeshDraw draw{&mesh, world, material, bounds};
    if (mesh.skinned() && palette.first >= 0) {
        draw.palette = palette.first;
        draw.palette_size = palette.size;
    }
    draws_.push_back(draw);
}

void MeshRenderer::draw(const Mesh& mesh, const glm::mat4& world, glm::vec4 color, const Texture* texture) {
    Material material;
    material.base_color = color;
    material.base_color_texture = texture;
    draw(mesh, world, material);
}

void MeshRenderer::draw(const Model& model, const glm::mat4& world) {
    const Material fallback;  // parts without a material
    for (const Model::Part& part : model.parts) {
        const Material& material =
            part.material >= 0 ? model.materials[static_cast<std::size_t>(part.material)] : fallback;
        draw(part.mesh, world * part.transform, material);
    }
}

void MeshRenderer::prepare_shadows() {
    const int size = std::clamp(shadow_options_.resolution, 256, 8192);
    if (!shadow_map_ || shadow_map_size_ != size) {
        SDL_GPUTextureCreateInfo info = {};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = shadow_format_;
        info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = static_cast<Uint32>(size);
        info.height = static_cast<Uint32>(size);
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        info.sample_count = SDL_GPU_SAMPLECOUNT_1;
        const NameProperty name_property(SDL_PROP_GPU_TEXTURE_CREATE_NAME_STRING, "shadow map");
        info.props = name_property.id();
        SDL_GPUTexture* raw = SDL_CreateGPUTexture(device_, &info);
        if (raw == nullptr) {
            throw std::runtime_error(std::string("SDL_CreateGPUTexture (shadow map) failed: ") + SDL_GetError());
        }
        shadow_map_ = GpuTexture(device_, raw);
        shadow_map_size_ = size;
    }
    ShadowSettings settings;
    settings.resolution = size;
    settings.max_height = shadow_options_.max_height;
    shadow_frame_ = fit_sun_shadow(view_projection_, to_sun_, settings);
}

void MeshRenderer::ensure_instance_capacity(std::size_t instances) {
    if (instances <= instance_capacity_) {
        return;
    }
    // Doubling keeps growth rare. The old buffers are released as soon as the GPU is done with them.
    std::size_t capacity = std::max(instance_capacity_, kInitialInstances);
    while (capacity < instances) {
        capacity *= 2;
    }
    const std::size_t bytes = capacity * sizeof(MeshInstance);
    instance_buffer_ = renderer_.create_buffer(SDL_GPU_BUFFERUSAGE_VERTEX, bytes, "mesh.instances");
    instance_transfer_ = renderer_.create_transfer_buffer(bytes);
    if (instance_capacity_ != 0) {
        SDL_Log("Mesh instance buffer grown from %zu to %zu instances (%.1f MiB)", instance_capacity_, capacity,
                static_cast<double>(bytes) / (1024.0 * 1024.0));
    }
    instance_capacity_ = capacity;
}

void MeshRenderer::ensure_palette_capacity(std::size_t rows) {
    if (rows <= palette_capacity_) {
        return;
    }
    std::size_t capacity = std::max<std::size_t>(palette_capacity_, 64 * 3);
    while (capacity < rows) {
        capacity *= 2;
    }
    const std::size_t bytes = capacity * sizeof(glm::vec4);
    palette_buffer_ = renderer_.create_buffer(SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, bytes, "mesh.palettes");
    palette_transfer_ = renderer_.create_transfer_buffer(bytes);
    palette_capacity_ = capacity;
}

void MeshRenderer::prepare(SDL_GPUCommandBuffer* commands, RenderStats& stats) {
    stats.dropped_lights += dropped_lights_;
    if (fog_dirty_ && fog_texture_) {
        void* mapped = SDL_MapGPUTransferBuffer(device_, fog_transfer_.get(), true);
        if (mapped == nullptr) {
            throw std::runtime_error(std::string("SDL_MapGPUTransferBuffer (fog) failed: ") + SDL_GetError());
        }
        std::memcpy(mapped, fog_cells_.data(), fog_cells_.size());
        SDL_UnmapGPUTransferBuffer(device_, fog_transfer_.get());
        SDL_GPUCopyPass* fog_pass = SDL_BeginGPUCopyPass(commands);
        SDL_GPUTextureTransferInfo source = {};
        source.transfer_buffer = fog_transfer_.get();
        source.pixels_per_row = static_cast<Uint32>(fog_size_.x);
        source.rows_per_layer = static_cast<Uint32>(fog_size_.y);
        SDL_GPUTextureRegion region = {};
        region.texture = fog_texture_.get();
        region.w = static_cast<Uint32>(fog_size_.x);
        region.h = static_cast<Uint32>(fog_size_.y);
        region.d = 1;
        SDL_UploadToGPUTexture(fog_pass, &source, &region, true);
        SDL_EndGPUCopyPass(fog_pass);
        stats.bytes_uploaded += fog_cells_.size();
        fog_dirty_ = false;
    }
    if (!has_work()) {
        return;
    }
    // The camera's pass, then the sun's (its frustum is the box the shadow map covers).
    // A default Frustum (all planes zero) lets everything through: no culling.
    const auto frustum = [this](const glm::mat4& view_projection) {
        return culling_ ? Frustum::from_view_projection(view_projection) : Frustum{};
    };
    main_batcher_.build(draws_, frustum(view_projection_), MeshBatcher::Pass::Main);
    stats.meshes_submitted += static_cast<int>(main_batcher_.submitted());
    const bool shadows = wants_shadows();
    if (shadows) {
        prepare_shadows();
        shadow_batcher_.build(draws_, frustum(shadow_frame_.view_projection), MeshBatcher::Pass::Shadow);
        stats.shadow_casters_submitted += static_cast<int>(shadow_batcher_.submitted());
    }
    prepare_point_shadows(stats);

    // The instances, one pass after the other: scene, sun, then each face of each point light drawn.
    const std::size_t main_count = main_batcher_.visible();
    const std::size_t shadow_count = shadows ? shadow_batcher_.visible() : 0;
    point_instance_base_ = main_count + shadow_count;
    std::size_t point_count = 0;
    for (int b = 0; b < point_renders_ * 6; ++b) {
        point_count += point_batchers_[static_cast<std::size_t>(b)].visible();
    }
    const std::size_t count = main_count + shadow_count + point_count;
    if (count == 0) {
        return;
    }
    ensure_instance_capacity(count);
    // `cycle = true`: a fresh staging buffer if the GPU still reads the previous frame's.
    void* mapped = SDL_MapGPUTransferBuffer(device_, instance_transfer_.get(), true);
    if (mapped == nullptr) {
        throw std::runtime_error(std::string("SDL_MapGPUTransferBuffer (mesh instances) failed: ") + SDL_GetError());
    }
    auto* out = static_cast<MeshInstance*>(mapped);
    if (main_count > 0) {
        std::memcpy(out, main_batcher_.instances().data(), main_count * sizeof(MeshInstance));
    }
    if (shadow_count > 0) {
        std::memcpy(out + main_count, shadow_batcher_.instances().data(), shadow_count * sizeof(MeshInstance));
    }
    std::size_t next = point_instance_base_;
    for (int b = 0; b < point_renders_ * 6; ++b) {
        const MeshBatcher& batcher = point_batchers_[static_cast<std::size_t>(b)];
        if (batcher.visible() > 0) {
            std::memcpy(out + next, batcher.instances().data(), batcher.visible() * sizeof(MeshInstance));
            next += batcher.visible();
        }
    }
    SDL_UnmapGPUTransferBuffer(device_, instance_transfer_.get());

    const std::size_t bytes = count * sizeof(MeshInstance);
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(commands);
    const SDL_GPUTransferBufferLocation source = {instance_transfer_.get(), 0};
    const SDL_GPUBufferRegion destination = {instance_buffer_.get(), 0, static_cast<Uint32>(bytes)};
    SDL_UploadToGPUBuffer(copy_pass, &source, &destination, true);
    stats.bytes_uploaded += bytes;

    // The palettes of the skinned draws, the same for every pass.
    if (!palette_rows_.empty()) {
        ensure_palette_capacity(palette_rows_.size());
        const std::size_t palette_bytes = palette_rows_.size() * sizeof(glm::vec4);
        void* rows = SDL_MapGPUTransferBuffer(device_, palette_transfer_.get(), true);
        if (rows == nullptr) {
            SDL_EndGPUCopyPass(copy_pass);
            throw std::runtime_error(std::string("SDL_MapGPUTransferBuffer (mesh palettes) failed: ") + SDL_GetError());
        }
        std::memcpy(rows, palette_rows_.data(), palette_bytes);
        SDL_UnmapGPUTransferBuffer(device_, palette_transfer_.get());
        const SDL_GPUTransferBufferLocation palette_source = {palette_transfer_.get(), 0};
        const SDL_GPUBufferRegion palette_destination = {palette_buffer_.get(), 0, static_cast<Uint32>(palette_bytes)};
        SDL_UploadToGPUBuffer(copy_pass, &palette_source, &palette_destination, true);
        stats.bytes_uploaded += palette_bytes;
    }
    SDL_EndGPUCopyPass(copy_pass);
}

void MeshRenderer::prepare_point_shadows(RenderStats& stats) {
    point_jobs_.clear();
    point_renders_ = 0;
    const int budget = std::clamp(shadow_options_.point_budget, 0, kMaxShadowedPointLights);
    std::vector<PointShadowCandidate> candidates;
    std::vector<int> candidate_light;  // index in lights_ of each candidate
    for (std::size_t i = 0; i < lights_.size(); ++i) {
        if (lights_[i].casts_shadows && lights_[i].range > 0.0f) {
            candidates.push_back({lights_[i].position, lights_[i].range});
            candidate_light.push_back(static_cast<int>(i));
        }
    }
    if (budget == 0 || candidates.empty()) {
        return;
    }

    // The middle of the view on the ground: the lights nearest to it get the shadows.
    const glm::mat4 inverse = glm::inverse(view_projection_);
    const glm::vec4 near_point = inverse * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    const glm::vec4 far_point = inverse * glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    const Ray middle{glm::vec3(near_point) / near_point.w,
                     glm::normalize(glm::vec3(far_point) / far_point.w - glm::vec3(near_point) / near_point.w)};
    const glm::vec3 focus = middle.hit_height(0.0f).value_or(eye_);
    const std::vector<int> selected =
        select_point_shadows(candidates, Frustum::from_view_projection(view_projection_), focus, budget);
    if (selected.empty()) {
        return;
    }

    // The atlas: six tiles across (the faces), a row per light of the budget.
    const int tile = std::clamp(shadow_options_.point_resolution, 64, 2048);
    if (!point_atlas_ || tile != point_tile_ || budget != point_rows_) {
        point_atlas_ = create_depth_texture(device_, shadow_format_, 6 * tile, budget * tile, "point shadow atlas");
        point_tile_ = tile;
        point_rows_ = budget;
        point_slots_.reset(budget);
        point_atlas_new_ = true;
    }

    // What each shadow depends on: the light, and the casters inside its sphere, draw by draw.
    if (point_casters_.size() < selected.size()) {
        point_casters_.resize(selected.size());
    }
    std::vector<std::uint64_t> signatures;
    for (std::size_t k = 0; k < selected.size(); ++k) {
        const PointLight& light = lights_[static_cast<std::size_t>(candidate_light[static_cast<std::size_t>(selected[k])])];
        std::vector<std::uint32_t>& casters = point_casters_[k];
        casters.clear();
        std::uint64_t hash = 14695981039346656037ull;
        hash_bytes(hash, &light.position, sizeof(light.position));
        hash_bytes(hash, &light.range, sizeof(light.range));
        hash_bytes(hash, &tile, sizeof(tile));
        for (std::size_t i = 0; i < draws_.size(); ++i) {
            const MeshDraw& draw = draws_[i];
            if (!draw.material.casts_shadow || (culling_ && !intersects_sphere(draw.bounds, light.position, light.range))) {
                continue;
            }
            casters.push_back(static_cast<std::uint32_t>(i));
            const Mesh* mesh = draw.mesh;
            hash_bytes(hash, &mesh, sizeof(mesh));
            hash_bytes(hash, &draw.world, sizeof(draw.world));
            hash_bytes(hash, &draw.material.double_sided, sizeof(bool));
            if (draw.palette >= 0) {  // an animated character: its pose
                hash_bytes(hash, &palette_rows_[static_cast<std::size_t>(draw.palette) * 3],
                           static_cast<std::size_t>(draw.palette_size) * 3 * sizeof(glm::vec4));
            }
        }
        signatures.push_back(hash);
    }
    const std::vector<PointShadowSlots::Assignment> assignments = point_slots_.assign(signatures);

    // The faces to draw again, culled against each face of their light.
    for (std::size_t k = 0; k < assignments.size(); ++k) {
        const PointLight& light = lights_[static_cast<std::size_t>(candidate_light[static_cast<std::size_t>(selected[k])])];
        PointShadowJob job;
        job.light = candidate_light[static_cast<std::size_t>(selected[k])];
        job.row = assignments[k].slot;
        job.render = assignments[k].render;
        job.faces = point_shadow_faces(light.position, light.range, tile);
        if (job.render) {
            job.first_batcher = static_cast<std::size_t>(point_renders_) * 6;
            if (point_batchers_.size() < job.first_batcher + 6) {
                point_batchers_.resize(job.first_batcher + 6);
            }
            for (int f = 0; f < 6; ++f) {
                const Frustum face = culling_ ? Frustum::from_view_projection(job.faces.view_projection[f]) : Frustum{};
                point_batchers_[job.first_batcher + static_cast<std::size_t>(f)].build(draws_, point_casters_[k], face,
                                                                                      MeshBatcher::Pass::Shadow);
            }
            ++point_renders_;
        }
        point_jobs_.push_back(job);
    }
    stats.point_shadow_lights += static_cast<int>(point_jobs_.size());
    stats.point_shadow_updates += point_renders_;
}

void MeshRenderer::add_debug_lines(DebugLineBuffer& lines) const {
    if (!has_work()) {
        return;
    }
    if (debug_.bounds) {
        for (const std::uint32_t index : main_batcher_.visible_draws()) {
            lines.box(draws_[index].bounds, {0.2f, 1.0f, 0.3f, 1.0f});
        }
    }
    if (debug_.shadow_frustum && wants_shadows()) {
        lines.frustum(shadow_frame_.view_projection, {1.0f, 0.9f, 0.2f, 1.0f}, true);
    }
    if (debug_.lights) {
        for (std::size_t i = 0; i < lights_.size(); ++i) {
            const bool shadowed = std::any_of(point_jobs_.begin(), point_jobs_.end(),
                                              [i](const PointShadowJob& job) { return job.light == static_cast<int>(i); });
            const glm::vec4 color = shadowed ? glm::vec4(1.0f, 0.2f, 0.1f, 1.0f) : glm::vec4(1.0f, 0.6f, 0.1f, 1.0f);
            lines.circle(lights_[i].position, {0.0f, 1.0f, 0.0f}, lights_[i].range, color, 48, true);
        }
    }
}

void MeshRenderer::render_point_shadows(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, RenderStats& stats) {
    std::size_t offset = point_instance_base_;
    const auto tile = static_cast<float>(point_tile_);
    for (const PointShadowJob& job : point_jobs_) {
        if (!job.render) {
            continue;
        }
        const PointLight& light = lights_[static_cast<std::size_t>(job.light)];
        const glm::vec4 light_uniform(light.position, 1.0f / light.range);
        for (int f = 0; f < 6; ++f) {
            // The tile of this face: viewport and scissor, so that nothing spills over its neighbours.
            const SDL_GPUViewport viewport = {static_cast<float>(f) * tile, static_cast<float>(job.row) * tile, tile, tile,
                                              0.0f, 1.0f};
            SDL_SetGPUViewport(pass, &viewport);
            const SDL_Rect scissor = {f * point_tile_, job.row * point_tile_, point_tile_, point_tile_};
            SDL_SetGPUScissor(pass, &scissor);
            SDL_BindGPUGraphicsPipeline(pass, point_clear_.get());
            SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
            ++stats.draw_calls;
            ++stats.point_shadow_draw_calls;

            SDL_PushGPUVertexUniformData(commands, 0, &job.faces.view_projection[f], sizeof(glm::mat4));
            SDL_PushGPUFragmentUniformData(commands, 0, &light_uniform, sizeof(light_uniform));
            const MeshBatcher& batcher = point_batchers_[job.first_batcher + static_cast<std::size_t>(f)];
            const SDL_GPUGraphicsPipeline* bound_pipeline = nullptr;
            for (const MeshBatch& batch : batcher.batches()) {
                SDL_GPUGraphicsPipeline* pipeline =
                    batch.skinned ? (batch.material->double_sided ? point_skinned_double_sided_.get() : point_skinned_single_sided_.get())
                                  : (batch.material->double_sided ? point_double_sided_.get() : point_single_sided_.get());
                if (pipeline != bound_pipeline) {
                    SDL_BindGPUGraphicsPipeline(pass, pipeline);
                    bound_pipeline = pipeline;
                }
                draw_batch(pass, batch, offset + batch.first_instance);
                ++stats.draw_calls;
                ++stats.point_shadow_draw_calls;
            }
            offset += batcher.visible();
            stats.point_shadow_casters += static_cast<int>(batcher.visible());
            stats.point_shadow_triangles += batcher.triangles();
        }
    }
    point_atlas_new_ = false;
}

void MeshRenderer::render_shadows(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, RenderStats& stats) {
    SDL_PushGPUVertexUniformData(commands, 0, &shadow_frame_.view_projection, sizeof(glm::mat4));
    const std::size_t base = main_batcher_.visible();  // the shadow instances follow the main ones
    const SDL_GPUGraphicsPipeline* bound_pipeline = nullptr;
    for (const MeshBatch& batch : shadow_batcher_.batches()) {
        SDL_GPUGraphicsPipeline* pipeline =
            batch.skinned ? (batch.material->double_sided ? shadow_skinned_double_sided_.get() : shadow_skinned_single_sided_.get())
                          : (batch.material->double_sided ? shadow_double_sided_.get() : shadow_single_sided_.get());
        if (pipeline != bound_pipeline) {
            SDL_BindGPUGraphicsPipeline(pass, pipeline);
            bound_pipeline = pipeline;
        }
        draw_batch(pass, batch, base + batch.first_instance);
        ++stats.draw_calls;
        ++stats.shadow_draw_calls;
    }
    stats.shadow_casters += static_cast<int>(shadow_batcher_.visible());
    stats.shadow_triangles += shadow_batcher_.triangles();
    shadows_drawn_ = true;
}

void MeshRenderer::render(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, RenderStats& stats) {
    if (!has_work()) {
        return;
    }

    // Everything that is the same for every draw of the frame, pushed once.
    FrameUniforms frame = {};
    frame.eye = glm::vec4(eye_, static_cast<float>(lights_.size()));
    frame.to_sun = glm::vec4(to_sun_, 0.0f);
    frame.sun = glm::vec4(sun_color_ * sun_intensity_, 0.0f);
    if (environment_ != nullptr) {
        frame.environment = glm::vec4(environment_intensity_, static_cast<float>(environment_->specular_levels - 1), 0.0f, 0.0f);
        for (int i = 0; i < 9; ++i) {
            frame.irradiance[i] = glm::vec4(environment_->irradiance.coefficients[i], 0.0f);
        }
    }
    for (std::size_t i = 0; i < lights_.size(); ++i) {
        frame.light_position[i] = glm::vec4(lights_[i].position, lights_[i].range);
        frame.light_color[i] = glm::vec4(lights_[i].color * lights_[i].intensity, -1.0f);  // w: no shadow
    }
    if (!point_jobs_.empty()) {
        for (const PointShadowJob& job : point_jobs_) {
            frame.light_color[job.light].w = static_cast<float>(job.row);
            for (int f = 0; f < 6; ++f) {
                frame.point_shadow_faces[job.row * 6 + f] = job.faces.view_projection[f];
            }
        }
        const auto tile = static_cast<float>(point_tile_);
        const auto rows = static_cast<float>(point_rows_);
        frame.point_shadow_tiles = glm::vec4(1.0f / 6.0f, 1.0f / rows, 1.0f / tile, shadow_options_.point_normal_offset);
        frame.point_shadow_params = glm::vec4(1.0f / (6.0f * tile), 1.0f / (rows * tile),
                                              point_jobs_.front().faces.texel_size_per_metre,
                                              shadow_options_.point_depth_bias);
    }
    frame.light_view_projection = shadow_frame_.view_projection;
    frame.shadow = glm::vec4(shadows_drawn_ ? 1.0f : 0.0f, shadow_frame_.texel_size, shadow_options_.normal_offset,
                             shadow_options_.depth_bias);
    frame.debug_view = glm::vec4(static_cast<float>(view_), kDistanceViewRange, 0.0f, 0.0f);
    fog_uniforms(frame.fog, frame.fog_rect);
    frame.cutout = cutout_.enabled ? glm::vec4(cutout_.focus, cutout_.radius) : glm::vec4(0.0f);
    SDL_PushGPUFragmentUniformData(commands, 0, &frame, sizeof(frame));
    struct {
        glm::mat4 view_projection;
        glm::vec4 debug;
    } vertex_frame{view_projection_, glm::vec4(static_cast<float>(weight_joint_), 0.0f, 0.0f, 0.0f)};
    SDL_PushGPUVertexUniformData(commands, 0, &vertex_frame, sizeof(vertex_frame));

    // The shadow map is always bound (the shader ignores it when it was not drawn); it exists from
    // the first frame with 3D on (prepare() creates it when shadows are enabled).
    if (!shadow_map_) {
        prepare_shadows();
    }
    SDL_GPUTexture* environment = environment_ != nullptr ? environment_->specular.get() : black_environment_.get();
    SDL_GPUTexture* point_atlas = point_atlas_ ? point_atlas_.get() : point_atlas_stand_in_.get();
    const SDL_GPUTextureSamplerBinding environment_bindings[4] = {
        {environment, environment_sampler_.get()},
        {shadow_map_.get(), shadow_sampler_.get()},
        {point_atlas, shadow_sampler_.get()},
        {fog_texture(), fog_sampler_.get()},
    };

    const SDL_GPUGraphicsPipeline* bound_pipeline = nullptr;
    const Texture* bound_textures[kMaterialTextures] = {};
    for (const MeshBatch& batch : main_batcher_.batches()) {
        const Material& material = *batch.material;
        const bool wireframe = view_ == MeshView::Wireframe;
        SDL_GPUGraphicsPipeline* pipeline = nullptr;
        if (batch.skinned) {
            pipeline = wireframe ? (material.double_sided ? skinned_wireframe_double_sided_ : skinned_wireframe_single_sided_).get()
                                 : (material.double_sided ? skinned_double_sided_ : skinned_single_sided_).get();
        } else {
            pipeline = wireframe ? (material.double_sided ? wireframe_double_sided_ : wireframe_single_sided_).get()
                                 : (material.double_sided ? double_sided_ : single_sided_).get();
        }
        if (pipeline != bound_pipeline) {
            // Bindings are not assumed to survive a pipeline change, on any backend: rebind everything.
            SDL_BindGPUGraphicsPipeline(pass, pipeline);
            SDL_BindGPUFragmentSamplers(pass, kMaterialTextures, environment_bindings, 4);
            bound_pipeline = pipeline;
            for (const Texture*& texture : bound_textures) {
                texture = nullptr;
            }
        }
        // Rebind the material textures only when one of them changes.
        const Texture* textures[kMaterialTextures] = {
            material.base_color_texture != nullptr ? material.base_color_texture : &white_,
            material.metallic_roughness_texture != nullptr ? material.metallic_roughness_texture : &white_,
            material.normal_texture != nullptr ? material.normal_texture : &flat_normal_,
            material.occlusion_texture != nullptr ? material.occlusion_texture : &white_,
            material.emissive_texture != nullptr ? material.emissive_texture : &white_,
        };
        bool same = true;
        for (int i = 0; i < kMaterialTextures; ++i) {
            same = same && textures[i] == bound_textures[i];
        }
        if (!same) {
            SDL_GPUTextureSamplerBinding bindings[kMaterialTextures];
            for (int i = 0; i < kMaterialTextures; ++i) {
                bindings[i] = {textures[i]->gpu.get(), material_sampler_.get()};
                bound_textures[i] = textures[i];
            }
            SDL_BindGPUFragmentSamplers(pass, 0, bindings, kMaterialTextures);
        }
        draw_batch(pass, batch, batch.first_instance);
        ++stats.draw_calls;
        ++stats.mesh_draw_calls;
    }
    stats.meshes += static_cast<int>(main_batcher_.visible());
    stats.triangles += main_batcher_.triangles();
}

void MeshRenderer::draw_batch(SDL_GPURenderPass* pass, const MeshBatch& batch, std::size_t first_instance) {
    // Each batch binds its own slice of the instance buffer: the instance index then starts at 0 in
    // the shader on every backend (first_instance is not reliable for that on all of them).
    const SDL_GPUBufferBinding buffers[3] = {
        {batch.mesh->vertices.get(), 0},
        {instance_buffer_.get(), static_cast<Uint32>(first_instance * sizeof(MeshInstance))},
        {batch.mesh->skin.get(), 0},
    };
    SDL_BindGPUVertexBuffers(pass, 0, buffers, batch.skinned ? 3 : 2);
    if (batch.skinned) {
        // Bound for every skinned batch: cheap, and no binding is assumed to survive a pipeline change.
        SDL_GPUBuffer* palettes = palette_buffer_.get();
        SDL_BindGPUVertexStorageBuffers(pass, 0, &palettes, 1);
    }
    const SDL_GPUBufferBinding indices = {batch.mesh->indices.get(), 0};
    SDL_BindGPUIndexBuffer(pass, &indices, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    SDL_DrawGPUIndexedPrimitives(pass, batch.mesh->index_count, batch.count, 0, 0, 0);
}

}  // namespace moteur
