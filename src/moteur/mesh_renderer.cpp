#include "moteur/mesh_renderer.hpp"

#include <glm/gtc/matrix_inverse.hpp>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "moteur/image.hpp"
#include "moteur/mesh.hpp"
#include "moteur/model.hpp"

namespace moteur {

namespace {

// Uniform blocks, laid out exactly like the cbuffers of mesh.vert.hlsl and mesh.frag.hlsl: only
// float4x4 and float4 members, so no padding rule can differ between C++ and HLSL.
struct ObjectUniforms {
    glm::mat4 view_projection;
    glm::mat4 world;
    glm::mat4 normal_matrix;
};

struct MaterialUniforms {
    glm::vec4 base_color;
    glm::vec4 factors;   // metallic, roughness, normal scale, occlusion strength
    glm::vec4 emissive;  // rgb
};

struct FrameUniforms {
    glm::vec4 eye;                // xyz: camera position; w: number of point lights
    glm::vec4 to_sun;             // xyz
    glm::vec4 sun;                // rgb: color x intensity
    glm::vec4 environment;        // x: intensity, y: highest specular mip level
    glm::vec4 irradiance[9];      // spherical harmonics, rgb
    glm::vec4 light_position[MeshRenderer::kMaxPointLights];  // xyz, w: range
    glm::vec4 light_color[MeshRenderer::kMaxPointLights];     // rgb: color x intensity
};

constexpr int kMaterialTextures = 5;  // then the environment, in slot 5

Texture one_pixel(Renderer& renderer, std::uint8_t r, std::uint8_t g, std::uint8_t b, const char* name) {
    Image image;
    image.width = 1;
    image.height = 1;
    image.pixels = {r, g, b, 255};
    TextureSettings settings;
    settings.premultiply = false;
    return renderer.create_texture(image, settings, name);
}

}  // namespace

MeshRenderer::MeshRenderer(Renderer& renderer, SDL_GPUTextureFormat color_format, SDL_GPUTextureFormat depth_format) {
    ShaderInfo vertex_info;
    vertex_info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    vertex_info.uniform_buffers = 1;
    ShaderInfo fragment_info;
    fragment_info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fragment_info.uniform_buffers = 2;               // material, frame
    fragment_info.samplers = kMaterialTextures + 1;  // + the environment

    const GpuShader vertex_shader = renderer.load_shader("mesh.vert", vertex_info);
    const GpuShader fragment_shader = renderer.load_shader("mesh.frag", fragment_info);

    SDL_GPUVertexBufferDescription vertex_buffer = {};
    vertex_buffer.slot = 0;
    vertex_buffer.pitch = sizeof(Vertex3D);
    vertex_buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_GPUVertexAttribute attributes[4] = {};
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

    // Opaque: no blending. Linear HDR (see Renderer::kSceneFormat).
    SDL_GPUColorTargetDescription color_target = {};
    color_target.format = color_format;

    SDL_GPUGraphicsPipelineCreateInfo info = {};
    info.vertex_shader = vertex_shader.get();
    info.fragment_shader = fragment_shader.get();
    info.vertex_input_state.vertex_buffer_descriptions = &vertex_buffer;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = attributes;
    info.vertex_input_state.num_vertex_attributes = 4;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    // glTF convention: front faces are counter-clockwise; the inside of closed shapes is never drawn.
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
    info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    info.depth_stencil_state.enable_depth_test = true;
    info.depth_stencil_state.enable_depth_write = true;
    info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    info.target_info.color_target_descriptions = &color_target;
    info.target_info.num_color_targets = 1;
    info.target_info.has_depth_stencil_target = true;
    info.target_info.depth_stencil_format = depth_format;

    const auto create = [&](const char* name) {
        const NameProperty name_property(SDL_PROP_GPU_GRAPHICSPIPELINE_CREATE_NAME_STRING, name);
        info.props = name_property.id();
        SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(renderer.device(), &info);
        if (pipeline == nullptr) {
            throw std::runtime_error(std::string("SDL_CreateGPUGraphicsPipeline (") + name + ") failed: " + SDL_GetError());
        }
        return GpuGraphicsPipeline(renderer.device(), pipeline);
    };
    single_sided_ = create("mesh pipeline");
    // Double-sided materials (leaves, cloth, and every Poly Haven model): back faces are drawn too,
    // and the shader turns their normal around.
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    double_sided_ = create("mesh pipeline (double sided)");

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

    white_ = one_pixel(renderer, 255, 255, 255, "mesh.white");
    flat_normal_ = one_pixel(renderer, 128, 128, 255, "mesh.flat normal");
    const std::vector<std::vector<std::uint8_t>> black(1, std::vector<std::uint8_t>(8, 0));  // one RGBA16F texel
    black_environment_ = renderer.create_texture_levels(SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, 1, 1, black,
                                                        "environment.black");
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
    draws_.push_back({&mesh, world, material});
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

void MeshRenderer::render(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, RenderStats& stats) {
    stats.dropped_lights += dropped_lights_;
    if (draws_.empty() || !has_camera_) {
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
        frame.light_color[i] = glm::vec4(lights_[i].color * lights_[i].intensity, 0.0f);
    }
    SDL_PushGPUFragmentUniformData(commands, 1, &frame, sizeof(frame));

    SDL_GPUTexture* environment = environment_ != nullptr ? environment_->specular.get() : black_environment_.get();
    const SDL_GPUTextureSamplerBinding environment_binding = {environment, environment_sampler_.get()};

    const Mesh* bound_mesh = nullptr;
    const SDL_GPUGraphicsPipeline* bound_pipeline = nullptr;
    const Texture* bound_textures[kMaterialTextures] = {};
    for (const Draw& draw : draws_) {
        const Material& material = draw.material;
        SDL_GPUGraphicsPipeline* pipeline = material.double_sided ? double_sided_.get() : single_sided_.get();
        if (pipeline != bound_pipeline) {
            // Bindings are not assumed to survive a pipeline change, on any backend: rebind everything.
            SDL_BindGPUGraphicsPipeline(pass, pipeline);
            SDL_BindGPUFragmentSamplers(pass, kMaterialTextures, &environment_binding, 1);
            bound_pipeline = pipeline;
            for (const Texture*& texture : bound_textures) {
                texture = nullptr;
            }
            bound_mesh = nullptr;
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
        if (draw.mesh != bound_mesh) {
            const SDL_GPUBufferBinding vertices = {draw.mesh->vertices.get(), 0};
            SDL_BindGPUVertexBuffers(pass, 0, &vertices, 1);
            const SDL_GPUBufferBinding indices = {draw.mesh->indices.get(), 0};
            SDL_BindGPUIndexBuffer(pass, &indices, SDL_GPU_INDEXELEMENTSIZE_32BIT);
            bound_mesh = draw.mesh;
        }
        const ObjectUniforms object = {view_projection_, draw.world, glm::inverseTranspose(draw.world)};
        SDL_PushGPUVertexUniformData(commands, 0, &object, sizeof(object));
        const MaterialUniforms uniforms = {
            material.base_color,
            glm::vec4(material.metallic, material.roughness, material.normal_scale, material.occlusion_strength),
            glm::vec4(material.emissive, 0.0f),
        };
        SDL_PushGPUFragmentUniformData(commands, 0, &uniforms, sizeof(uniforms));

        SDL_DrawGPUIndexedPrimitives(pass, draw.mesh->index_count, 1, 0, 0, 0);
        ++stats.draw_calls;
        ++stats.meshes;
        stats.triangles += draw.mesh->index_count / 3;
    }
}

}  // namespace moteur
