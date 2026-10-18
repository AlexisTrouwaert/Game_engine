#include "moteur/assert.hpp"
#include "moteur/renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>

#include "moteur/billboard_renderer.hpp"
#include "moteur/debug_lines.hpp"
#if !defined(MOTEUR_SHIPPING)
#include "moteur/debug_ui.hpp"
#endif
#include "moteur/mesh_renderer.hpp"
#include "moteur/paths.hpp"
#include "moteur/screenshot.hpp"
#include "moteur/color.hpp"
#include "moteur/sprite_renderer.hpp"
#include "tone_mapper.hpp"

namespace moteur {

namespace {

// Shader formats the engine can provide. One backend per OS for now:
// Direct3D 12 on Windows, Metal on macOS. Add SPIRV to allow Vulkan.
constexpr SDL_GPUShaderFormat kShaderFormats =
#if defined(SDL_PLATFORM_WIN32)
    SDL_GPU_SHADERFORMAT_DXIL;
#elif defined(SDL_PLATFORM_MACOS)
    SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB;
#else
    SDL_GPU_SHADERFORMAT_SPIRV;
#endif

// The most precise depth format the GPU can render to. SDL guarantees D16_UNORM and either
// D24_UNORM or D32_FLOAT, never both, so the choice has to be made at run time.
SDL_GPUTextureFormat pick_depth_format(SDL_GPUDevice* device) {
    for (const SDL_GPUTextureFormat format : {SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTUREFORMAT_D24_UNORM}) {
        if (SDL_GPUTextureSupportsFormat(device, format, SDL_GPU_TEXTURETYPE_2D,
                                         SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
            return format;
        }
    }
    return SDL_GPU_TEXTUREFORMAT_D16_UNORM;
}

const char* present_mode_name(SDL_GPUPresentMode mode) {
    switch (mode) {
        case SDL_GPU_PRESENTMODE_VSYNC: return "vsync";
        case SDL_GPU_PRESENTMODE_IMMEDIATE: return "immediate";
        case SDL_GPU_PRESENTMODE_MAILBOX: return "mailbox";
    }
    return "unknown";
}

SDL_GPUPresentMode choose_present_mode(SDL_GPUDevice* device, SDL_Window* window, bool vsync) {
    if (vsync) {
        return SDL_GPU_PRESENTMODE_VSYNC;  // always supported
    }
    for (const SDL_GPUPresentMode mode : {SDL_GPU_PRESENTMODE_IMMEDIATE, SDL_GPU_PRESENTMODE_MAILBOX}) {
        if (SDL_WindowSupportsGPUPresentMode(device, window, mode)) {
            return mode;
        }
    }
    return SDL_GPU_PRESENTMODE_VSYNC;
}

SDL_GPUSampleCount sample_count(AntiAliasing mode) {
    switch (mode) {
        case AntiAliasing::Msaa2: return SDL_GPU_SAMPLECOUNT_2;
        case AntiAliasing::Msaa4: return SDL_GPU_SAMPLECOUNT_4;
        default: return SDL_GPU_SAMPLECOUNT_1;
    }
}

constexpr AntiAliasing kAntiAliasingModes[] = {AntiAliasing::None, AntiAliasing::Fxaa, AntiAliasing::Msaa2,
                                               AntiAliasing::Msaa4};

}  // namespace

const char* anti_aliasing_name(AntiAliasing mode) {
    switch (mode) {
        case AntiAliasing::None: return "none";
        case AntiAliasing::Fxaa: return "fxaa";
        case AntiAliasing::Msaa2: return "msaa2";
        case AntiAliasing::Msaa4: return "msaa4";
    }
    return "none";
}

bool parse_anti_aliasing(const std::string& name, AntiAliasing& mode) {
    for (const AntiAliasing candidate : kAntiAliasingModes) {
        if (name == anti_aliasing_name(candidate)) {
            mode = candidate;
            return true;
        }
    }
    return false;
}

Renderer::Renderer(SDL_Window* window, const RendererConfig& config) : window_(window) {
    device_ = SDL_CreateGPUDevice(kShaderFormats, config.debug, nullptr);
    if (device_ == nullptr) {
        throw std::runtime_error(std::string("SDL_CreateGPUDevice failed: ") + SDL_GetError());
    }

    if (!SDL_ClaimWindowForGPUDevice(device_, window_)) {
        const std::string error = SDL_GetError();
        SDL_DestroyGPUDevice(device_);
        throw std::runtime_error("SDL_ClaimWindowForGPUDevice failed: " + error);
    }

    const SDL_GPUPresentMode present_mode = choose_present_mode(device_, window_, config.vsync);
    if (!SDL_SetGPUSwapchainParameters(device_, window_, SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                       present_mode)) {
        SDL_Log("SDL_SetGPUSwapchainParameters failed: %s", SDL_GetError());
    }

    const char* gpu_name =
        SDL_GetStringProperty(SDL_GetGPUDeviceProperties(device_), SDL_PROP_GPU_DEVICE_NAME_STRING, "unknown");
    depth_format_ = pick_depth_format(device_);
    // The compressed formats chosen for textures (BC7 for colors, BC5 for normal maps), whose
    // support on Apple Silicon is checked from this line.
    const auto samples_format = [this](SDL_GPUTextureFormat format) {
        return SDL_GPUTextureSupportsFormat(device_, format, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_SAMPLER) ? "yes"
                                                                                                                  : "no";
    };
    SDL_Log("GPU: backend=%s, device=%s, present=%s, debug=%s, depth=%s, bc7=%s, bc5=%s", SDL_GetGPUDeviceDriver(device_),
            gpu_name, present_mode_name(present_mode), config.debug ? "on" : "off", depth_format_name(),
            samples_format(SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM_SRGB), samples_format(SDL_GPU_TEXTUREFORMAT_BC5_RG_UNORM));
    device_description_ = std::string(SDL_GetGPUDeviceDriver(device_)) + ", " + gpu_name;
    compressed_formats_.bc7 = std::string_view(samples_format(SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM_SRGB)) == "yes" &&
                              std::string_view(samples_format(SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM)) == "yes";
    compressed_formats_.bc5 = std::string_view(samples_format(SDL_GPU_TEXTUREFORMAT_BC5_RG_UNORM)) == "yes";

    try {
        meshes_ = std::make_unique<MeshRenderer>(*this, kSceneFormat, depth_format_);
        billboards_ = std::make_unique<BillboardRenderer>(*this, kSceneFormat, depth_format_);
        debug_lines_ = std::make_unique<DebugLineRenderer>(*this, kSceneFormat, depth_format_);
        tone_mapper_ = std::make_unique<ToneMapper>(*this, swapchain_format());
        fxaa_ = std::make_unique<Fxaa>(*this, swapchain_format());
        depth_view_ = std::make_unique<DepthView>(*this, swapchain_format());
        // Both sprite renderers draw in the "compose" pass, which has no depth texture.
        sprites_ = std::make_unique<SpriteRenderer>(*this, SDL_GPU_TEXTUREFORMAT_INVALID, "sprite");
        screen_sprites_ = std::make_unique<SpriteRenderer>(*this, SDL_GPU_TEXTUREFORMAT_INVALID, "screen sprite");
#if !defined(MOTEUR_SHIPPING)
        if (config.debug_ui) {
            debug_ui_ = std::make_unique<DebugUi>(window_, device_, swapchain_format(), config.debug_ui_font,
                                                  config.debug_ui_font_size);
        }
#endif
    } catch (...) {
        // The destructor does not run when a constructor throws: clean up here.
#if !defined(MOTEUR_SHIPPING)
        debug_ui_.reset();
#endif
        screen_sprites_.reset();
        sprites_.reset();
        depth_view_.reset();
        fxaa_.reset();
        tone_mapper_.reset();
        debug_lines_.reset();
        billboards_.reset();
        meshes_.reset();
        vertex_pool_.reset();
        index_pool_.reset();
        SDL_WaitForGPUIdle(device_);
        SDL_ReleaseWindowFromGPUDevice(device_, window_);
        SDL_DestroyGPUDevice(device_);
        throw;
    }
}

Renderer::~Renderer() {
    flush_uploads();
    SDL_WaitForGPUIdle(device_);
    release_upload_pages();
    for (PickSlot& slot : pick_slots_) {
        if (slot.fence != nullptr) {
            SDL_ReleaseGPUFence(device_, slot.fence);
            slot.fence = nullptr;
        }
        slot.buffer.reset();
    }
    id_texture_.reset();
    id_depth_.reset();
    // Every GPU resource must be released before the device.
#if !defined(MOTEUR_SHIPPING)
    debug_ui_.reset();
#endif
    screen_sprites_.reset();
    sprites_.reset();
    depth_view_.reset();
    fxaa_.reset();
    tone_mapper_.reset();
    debug_lines_.reset();
    billboards_.reset();
    meshes_.reset();
    vertex_pool_.reset();  // after everything that may hold a mesh
    index_pool_.reset();
    scene_texture_ = {};
    scene_msaa_texture_ = {};
    depth_texture_ = {};
    distance_texture_ = {};
    distance_msaa_texture_ = {};
    ldr_texture_ = {};
    SDL_ReleaseWindowFromGPUDevice(device_, window_);
    SDL_DestroyGPUDevice(device_);
}

SpriteRenderer& Renderer::sprites() {
    return *sprites_;
}

SpriteRenderer& Renderer::screen_sprites() {
    return *screen_sprites_;
}

MeshRenderer& Renderer::meshes() {
    return *meshes_;
}

BillboardRenderer& Renderer::billboards() {
    return *billboards_;
}

DebugLineRenderer& Renderer::debug_lines() {
    return *debug_lines_;
}

void Renderer::render_debug_texture(SDL_GPURenderPass* pass) {
    if (debug_texture_ == DebugTexture::None) {
        return;
    }
    // In the bottom right corner: the sun's map as a square, the atlas with its proportions.
    SDL_GPUTexture* texture = nullptr;
    float aspect = 1.0f;  // width / height
    if (debug_texture_ == DebugTexture::SunShadowMap) {
        texture = meshes_->shadow_map();
    } else if (meshes_->point_rows_ > 0) {
        texture = meshes_->point_shadow_atlas();
        aspect = 6.0f / static_cast<float>(meshes_->point_rows_);
    }
    if (texture == nullptr) {
        return;
    }
    const auto w = static_cast<float>(width_);
    const auto h = static_cast<float>(height_);
    float area_width = std::min(w * 0.5f, h * 0.45f * aspect);
    float area_height = area_width / aspect;
    const float margin = 10.0f;
    const SDL_GPUViewport area = {w - area_width - margin, h - area_height - margin, area_width, area_height, 0.0f, 1.0f};
    const SDL_GPUViewport full = {0.0f, 0.0f, w, h, 0.0f, 1.0f};
    depth_view_->render(pass, texture, area, full);
}

const char* Renderer::depth_format_name() const {
    switch (depth_format_) {
        case SDL_GPU_TEXTUREFORMAT_D32_FLOAT: return "D32_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_D24_UNORM: return "D24_UNORM";
        case SDL_GPU_TEXTUREFORMAT_D16_UNORM: return "D16_UNORM";
        default: return "none";
    }
}

void Renderer::set_render_scale(float scale) {
    render_scale_ = std::clamp(scale, 0.25f, 1.0f);
}

bool Renderer::supports(AntiAliasing mode) const {
    const SDL_GPUSampleCount samples = sample_count(mode);
    return samples == SDL_GPU_SAMPLECOUNT_1 || (SDL_GPUTextureSupportsSampleCount(device_, kSceneFormat, samples) &&
                                                SDL_GPUTextureSupportsSampleCount(device_, kDistanceFormat, samples) &&
                                                SDL_GPUTextureSupportsSampleCount(device_, depth_format_, samples));
}

void Renderer::set_anti_aliasing(AntiAliasing mode) {
    const AntiAliasing asked = mode;
    while (!supports(mode)) {
        mode = mode == AntiAliasing::Msaa4 ? AntiAliasing::Msaa2 : AntiAliasing::None;
    }
    if (mode != asked) {
        SDL_Log("Anti-aliasing %s not supported by this GPU: %s instead", anti_aliasing_name(asked),
                anti_aliasing_name(mode));
    }
    anti_aliasing_ = mode;
}

void Renderer::set_exposure(float exposure) {
    exposure_ = std::max(exposure, 0.001f);
}

void Renderer::ensure_id_targets() {
    if (id_texture_ && id_width_ == scene_width_ && id_height_ == scene_height_) {
        return;
    }
    const auto create = [&](SDL_GPUTextureFormat format, SDL_GPUTextureUsageFlags usage, const char* name) {
        SDL_GPUTextureCreateInfo info = {};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = format;
        info.usage = usage;
        info.width = scene_width_;
        info.height = scene_height_;
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        info.sample_count = SDL_GPU_SAMPLECOUNT_1;
        const NameProperty name_property(SDL_PROP_GPU_TEXTURE_CREATE_NAME_STRING, name);
        info.props = name_property.id();
        SDL_GPUTexture* raw = SDL_CreateGPUTexture(device_, &info);
        if (raw == nullptr) {
            throw std::runtime_error(std::string("SDL_CreateGPUTexture (") + name + ") failed: " + SDL_GetError());
        }
        return GpuTexture(device_, raw, texture_memory_bytes(info), texture_memory_kind(info));
    };
    id_texture_ = create(kIdFormat, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, "scene ids");
    id_depth_ = create(depth_format_, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET, "scene ids depth");
    id_width_ = scene_width_;
    id_height_ = scene_height_;
}

void Renderer::collect_picks() {
    for (PickSlot& slot : pick_slots_) {
        if (slot.fence == nullptr || !SDL_QueryGPUFence(device_, slot.fence)) {
            continue;
        }
        SDL_ReleaseGPUFence(device_, slot.fence);
        slot.fence = nullptr;
        if (void* mapped = SDL_MapGPUTransferBuffer(device_, slot.buffer.get(), false)) {
            std::memcpy(&picked_id_, mapped, sizeof(picked_id_));
            picked_pixel_ = slot.pixel;
            SDL_UnmapGPUTransferBuffer(device_, slot.buffer.get());
        }
    }
}

void Renderer::ensure_scene_targets() {
    const auto scaled = [this](std::uint32_t size) {
        return std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::lround(static_cast<float>(size) * render_scale_)));
    };
    const std::uint32_t width = scaled(width_);
    const std::uint32_t height = scaled(height_);
    if (scene_texture_ && scene_width_ == width && scene_height_ == height && targets_anti_aliasing_ == anti_aliasing_) {
        return;
    }
    // The pipelines of the scene pass are made for a number of samples.
    const SDL_GPUSampleCount samples = sample_count(anti_aliasing_);
    if (samples != scene_samples_) {
        meshes_->create_scene_pipelines(samples);
        billboards_->create_pipeline(samples);
        debug_lines_->create_pipelines(samples);
        scene_samples_ = samples;
    }
    // A frame still in flight may use the previous textures and pipelines: SDL only frees them once
    // the GPU is done.
    const auto create = [&](SDL_GPUTextureFormat format, SDL_GPUTextureUsageFlags usage, const char* name,
                            SDL_GPUSampleCount texture_samples = SDL_GPU_SAMPLECOUNT_1) {
        SDL_GPUTextureCreateInfo info = {};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = format;
        info.usage = usage;
        info.width = width;
        info.height = height;
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        info.sample_count = texture_samples;
        const NameProperty name_property(SDL_PROP_GPU_TEXTURE_CREATE_NAME_STRING, name);
        info.props = name_property.id();
        SDL_GPUTexture* raw = SDL_CreateGPUTexture(device_, &info);
        if (raw == nullptr) {
            throw std::runtime_error(std::string("SDL_CreateGPUTexture (") + name + ") failed: " + SDL_GetError());
        }
        return GpuTexture(device_, raw, texture_memory_bytes(info), texture_memory_kind(info));
    };
    // With MSAA the pass draws into a multisampled texture (never sampled), resolved into the plain
    // one at its end.
    scene_texture_ = create(kSceneFormat, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, "scene color");
    scene_msaa_texture_ = samples == SDL_GPU_SAMPLECOUNT_1
                              ? GpuTexture()
                              : create(kSceneFormat, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, "scene color (msaa)", samples);
    depth_texture_ = create(depth_format_, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET, "scene depth", samples);
    distance_texture_ = create(kDistanceFormat, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, "scene distance");
    distance_msaa_texture_ = samples == SDL_GPU_SAMPLECOUNT_1
                                 ? GpuTexture()
                                 : create(kDistanceFormat, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, "scene distance (msaa)", samples);
    // With FXAA, the tone mapping draws into a texture of the swapchain's format, which FXAA reads.
    ldr_texture_ = anti_aliasing_ == AntiAliasing::Fxaa
                       ? create(swapchain_format(), SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                "tone mapped scene")
                       : GpuTexture();
    scene_width_ = width;
    scene_height_ = height;
    targets_anti_aliasing_ = anti_aliasing_;
}

GpuShader Renderer::load_shader(const std::string& name, const ShaderInfo& info) {
    struct Backend {
        SDL_GPUShaderFormat format;
        const char* extension;
        const char* entrypoint;
    };
    // SPIRV-Cross renames the MSL entry point to "main0" ("main" is reserved in Metal).
    static constexpr Backend kBackends[] = {
        {SDL_GPU_SHADERFORMAT_DXIL, "dxil", "main"},
        {SDL_GPU_SHADERFORMAT_MSL, "msl", "main0"},
        {SDL_GPU_SHADERFORMAT_SPIRV, "spv", "main"},
    };

    const std::string directory = base_path() + "shaders/";
    const SDL_GPUShaderFormat supported = SDL_GetGPUShaderFormats(device_);

    std::string tried;
    for (const Backend& backend : kBackends) {
        if ((supported & backend.format) == 0) {
            continue;
        }
        const std::string path = directory + name + "." + backend.extension;
        if (!file_exists(path)) {  // in the archives (shipped game) or on disk
            tried += "\n  " + path;
            continue;
        }
        FileData file = read_file(path);
        const std::size_t size = file.size();
        void* code = file.release();

        SDL_GPUShaderCreateInfo create_info = {};
        create_info.code = static_cast<const Uint8*>(code);
        create_info.code_size = size;
        create_info.entrypoint = backend.entrypoint;
        create_info.format = backend.format;
        create_info.stage = info.stage;
        create_info.num_samplers = info.samplers;
        create_info.num_uniform_buffers = info.uniform_buffers;
        create_info.num_storage_buffers = info.storage_buffers;
        create_info.num_storage_textures = info.storage_textures;
        const NameProperty name_property(SDL_PROP_GPU_SHADER_CREATE_NAME_STRING, name.c_str());
        create_info.props = name_property.id();

        SDL_GPUShader* shader = SDL_CreateGPUShader(device_, &create_info);
        const std::string error = shader == nullptr ? SDL_GetError() : "";
        SDL_free(code);
        if (shader == nullptr) {
            std::string message = "SDL_CreateGPUShader failed for ";
            message.append(path).append(": ").append(error);
            throw std::runtime_error(message);
        }
        return GpuShader(device_, shader);
    }

    throw std::runtime_error("Shader '" + name + "' not found for the active backend. Looked for:" + tried);
}

GpuBuffer Renderer::create_buffer(SDL_GPUBufferUsageFlags usage, const void* data, std::size_t size,
                                  const char* debug_name) {
    SDL_GPUBufferCreateInfo info = {};
    info.usage = usage;
    info.size = static_cast<Uint32>(size);
    const NameProperty name_property(SDL_PROP_GPU_BUFFER_CREATE_NAME_STRING, debug_name);
    info.props = name_property.id();
    SDL_GPUBuffer* raw = SDL_CreateGPUBuffer(device_, &info);
    if (raw == nullptr) {
        throw std::runtime_error(std::string("SDL_CreateGPUBuffer failed: ") + SDL_GetError());
    }
    GpuBuffer buffer(device_, raw, size, GpuMemoryKind::Buffers);  // released automatically if anything below throws

    // Recorded with the other pending uploads, submitted before the next frame.
    const SDL_GPUTransferBufferLocation source = stage_upload(data, size);
    const SDL_GPUBufferRegion destination = {raw, 0, static_cast<Uint32>(size)};
    SDL_UploadToGPUBuffer(upload_pass(), &source, &destination, false);
    return buffer;
}

GpuBuffer Renderer::create_buffer(SDL_GPUBufferUsageFlags usage, std::size_t size, const char* debug_name) {
    SDL_GPUBufferCreateInfo info = {};
    info.usage = usage;
    info.size = static_cast<Uint32>(size);
    const NameProperty name_property(SDL_PROP_GPU_BUFFER_CREATE_NAME_STRING, debug_name);
    info.props = name_property.id();
    SDL_GPUBuffer* buffer = SDL_CreateGPUBuffer(device_, &info);
    if (buffer == nullptr) {
        throw std::runtime_error(std::string("SDL_CreateGPUBuffer failed: ") + SDL_GetError());
    }
    return GpuBuffer(device_, buffer, size, GpuMemoryKind::Buffers);
}

GpuTransferBuffer Renderer::create_transfer_buffer(std::size_t size) {
    SDL_GPUTransferBufferCreateInfo info = {};
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    info.size = static_cast<Uint32>(size);
    SDL_GPUTransferBuffer* buffer = SDL_CreateGPUTransferBuffer(device_, &info);
    if (buffer == nullptr) {
        throw std::runtime_error(std::string("SDL_CreateGPUTransferBuffer failed: ") + SDL_GetError());
    }
    return GpuTransferBuffer(device_, buffer);
}

Texture Renderer::create_texture(const Image& straight_image, const char* debug_name) {
    return create_texture(straight_image, TextureSettings{}, debug_name);
}

Texture Renderer::create_texture(const Image& straight_image, const TextureSettings& settings, const char* debug_name) {
    // Premultiplied for sprites: see premultiply_alpha(). The sprite pipeline blends with that in mind.
    Image image = straight_image;
    if (settings.premultiply) {
        premultiply_alpha(image);
    }

    // Every level halves the size, down to 1 x 1.
    Uint32 levels = 1;
    if (settings.mipmaps) {
        for (int size = std::max(image.width, image.height); size > 1; size /= 2) {
            ++levels;
        }
    }

    SDL_GPUTextureCreateInfo info = {};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = settings.srgb ? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    // Generating mipmaps draws each level from the previous one: the texture must be a render target too.
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | (levels > 1 ? SDL_GPU_TEXTUREUSAGE_COLOR_TARGET : 0);
    info.width = static_cast<Uint32>(image.width);
    info.height = static_cast<Uint32>(image.height);
    info.layer_count_or_depth = 1;
    info.num_levels = levels;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    const NameProperty name_property(SDL_PROP_GPU_TEXTURE_CREATE_NAME_STRING, debug_name);
    info.props = name_property.id();
    SDL_GPUTexture* raw = SDL_CreateGPUTexture(device_, &info);
    if (raw == nullptr) {
        throw std::runtime_error(std::string("SDL_CreateGPUTexture failed: ") + SDL_GetError());
    }
    Texture texture;
    // Counted as a texture even with mipmaps: it is a render target only for the GPU to draw its
    // levels, once (texture_memory_kind() would see a target). Released automatically if anything
    // below throws.
    texture.gpu = GpuTexture(device_, raw, texture_memory_bytes(info), GpuMemoryKind::Textures);
    texture.width = image.width;
    texture.height = image.height;
    for (Uint32 level = 0; level < levels; ++level) {
        const std::size_t level_width = std::max(1, image.width >> level);
        const std::size_t level_height = std::max(1, image.height >> level);
        texture.gpu_bytes += level_width * level_height * 4;
    }

    const SDL_GPUTransferBufferLocation staged = stage_upload(image.pixels.data(), image.pixels.size());
    SDL_GPUTextureTransferInfo source = {};
    source.transfer_buffer = staged.transfer_buffer;
    source.offset = staged.offset;
    source.pixels_per_row = static_cast<Uint32>(image.width);
    source.rows_per_layer = static_cast<Uint32>(image.height);
    SDL_GPUTextureRegion destination = {};
    destination.texture = raw;
    destination.w = static_cast<Uint32>(image.width);
    destination.h = static_cast<Uint32>(image.height);
    destination.d = 1;
    SDL_UploadToGPUTexture(upload_pass(), &source, &destination, false);
    if (levels > 1) {
        // Filtered in linear space for an sRGB texture (the GPU converts on each read and write).
        SDL_GenerateMipmapsForGPUTexture(upload_commands(), raw);
    }
    return texture;
}

GpuTexture Renderer::create_texture_levels(SDL_GPUTextureFormat format, int width, int height,
                                           const std::vector<std::vector<std::uint8_t>>& levels, const char* debug_name) {
    if (levels.empty()) {
        throw std::invalid_argument("create_texture_levels: no level given");
    }
    SDL_GPUTextureCreateInfo info = {};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = format;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = static_cast<Uint32>(width);
    info.height = static_cast<Uint32>(height);
    info.layer_count_or_depth = 1;
    info.num_levels = static_cast<Uint32>(levels.size());
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    const NameProperty name_property(SDL_PROP_GPU_TEXTURE_CREATE_NAME_STRING, debug_name);
    info.props = name_property.id();
    SDL_GPUTexture* raw = SDL_CreateGPUTexture(device_, &info);
    if (raw == nullptr) {
        throw std::runtime_error(std::string("SDL_CreateGPUTexture failed: ") + SDL_GetError());
    }
    GpuTexture texture(device_, raw, texture_memory_bytes(info), texture_memory_kind(info));

    for (std::size_t level = 0; level < levels.size(); ++level) {
        const Uint32 level_width = std::max<Uint32>(1, static_cast<Uint32>(width) >> level);
        const Uint32 level_height = std::max<Uint32>(1, static_cast<Uint32>(height) >> level);
        const SDL_GPUTransferBufferLocation staged = stage_upload(levels[level].data(), levels[level].size());
        SDL_GPUTextureTransferInfo source = {};
        source.transfer_buffer = staged.transfer_buffer;
        source.offset = staged.offset;
        source.pixels_per_row = level_width;
        source.rows_per_layer = level_height;
        SDL_GPUTextureRegion destination = {};
        destination.texture = raw;
        destination.mip_level = static_cast<Uint32>(level);
        destination.w = level_width;
        destination.h = level_height;
        destination.d = 1;
        SDL_UploadToGPUTexture(upload_pass(), &source, &destination, false);
    }
    return texture;
}

GpuSlice Renderer::create_geometry(SDL_GPUBufferUsageFlags usage, const void* data, std::size_t size) {
    if (!vertex_pool_) {
        vertex_pool_ = std::make_unique<GeometryPool>(
            [this](std::uint32_t bytes) { return create_buffer(SDL_GPU_BUFFERUSAGE_VERTEX, bytes, "geometry: vertices"); });
        index_pool_ = std::make_unique<GeometryPool>(
            [this](std::uint32_t bytes) { return create_buffer(SDL_GPU_BUFFERUSAGE_INDEX, bytes, "geometry: indices"); });
    }
    if (usage != SDL_GPU_BUFFERUSAGE_VERTEX && usage != SDL_GPU_BUFFERUSAGE_INDEX) {
        throw std::invalid_argument("create_geometry: vertices or indices only");
    }
    GeometryPool& pool = usage == SDL_GPU_BUFFERUSAGE_INDEX ? *index_pool_ : *vertex_pool_;
    GpuSlice slice = pool.allocate(static_cast<std::uint32_t>(size));
    const SDL_GPUTransferBufferLocation source = stage_upload(data, size);
    const SDL_GPUBufferRegion destination = {slice.buffer(), slice.offset(), static_cast<Uint32>(size)};
    SDL_UploadToGPUBuffer(upload_pass(), &source, &destination, false);
    return slice;
}

GeometryPool::Stats Renderer::geometry_stats() const {
    GeometryPool::Stats total;
    for (const GeometryPool* pool : {vertex_pool_.get(), index_pool_.get()}) {
        if (pool != nullptr) {
            const GeometryPool::Stats stats = pool->stats();
            total.pages += stats.pages;
            total.used_bytes += stats.used_bytes;
            total.waiting_bytes += stats.waiting_bytes;
        }
    }
    return total;
}

// --- Batched uploads (milestone 8, part 5) -------------------------------------------------------
// Until then, every resource created with data had its own transfer buffer and command buffer,
// submitted and waited for: about 1 ms each under Direct3D 12, 200 ms for the 400 buffers of the
// slice's floor. Now they share pages and one command buffer, submitted before the next frame.

namespace {
constexpr std::size_t kUploadPageSize = 16u << 20;  // a larger upload gets a page of its own
constexpr std::size_t kUploadAlignment = 512;       // texture data placement (Direct3D 12's rule)
constexpr std::size_t kSparePages = 4;              // standard pages kept for reuse, at most
}  // namespace

SDL_GPUCommandBuffer* Renderer::upload_commands() {
    if (uploads_.commands == nullptr) {
        uploads_.commands = SDL_AcquireGPUCommandBuffer(device_);
        if (uploads_.commands == nullptr) {
            throw std::runtime_error(std::string("SDL_AcquireGPUCommandBuffer (uploads) failed: ") + SDL_GetError());
        }
    }
    if (uploads_.pass != nullptr) {
        SDL_EndGPUCopyPass(uploads_.pass);
        uploads_.pass = nullptr;
    }
    return uploads_.commands;
}

SDL_GPUCopyPass* Renderer::upload_pass() {
    if (uploads_.pass == nullptr) {
        SDL_GPUCommandBuffer* commands = upload_commands();
        uploads_.pass = SDL_BeginGPUCopyPass(commands);
    }
    return uploads_.pass;
}

SDL_GPUTransferBufferLocation Renderer::stage_upload(const void* data, std::size_t size) {
    std::size_t offset = (uploads_.page_used + kUploadAlignment - 1) / kUploadAlignment * kUploadAlignment;
    if (uploads_.pages.empty() || offset + size > uploads_.page_size) {
        // Pages the GPU is done with come back first.
        std::erase_if(uploads_.in_flight, [this](Uploads::InFlight& batch) {
            if (!SDL_QueryGPUFence(device_, batch.fence)) {
                return false;
            }
            SDL_ReleaseGPUFence(device_, batch.fence);
            for (SDL_GPUTransferBuffer* done : batch.pages) {
                if (uploads_.spare.size() < kSparePages) {
                    uploads_.spare.push_back(done);
                } else {
                    SDL_ReleaseGPUTransferBuffer(device_, done);  // a burst is over: give its pages back
                }
            }
            return true;
        });
        SDL_GPUTransferBuffer* page = nullptr;
        if (size <= kUploadPageSize && !uploads_.spare.empty()) {
            page = uploads_.spare.back();
            uploads_.spare.pop_back();
            uploads_.page_size = kUploadPageSize;
        } else {
            SDL_GPUTransferBufferCreateInfo info = {};
            info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            info.size = static_cast<Uint32>(std::max(size, kUploadPageSize));
            page = SDL_CreateGPUTransferBuffer(device_, &info);
            if (page == nullptr) {
                throw std::runtime_error(std::string("SDL_CreateGPUTransferBuffer (uploads) failed: ") + SDL_GetError());
            }
            uploads_.page_size = info.size;
        }
        uploads_.pages.push_back(page);
        uploads_.large.push_back(uploads_.page_size > kUploadPageSize);
        offset = 0;
    }
    SDL_GPUTransferBuffer* page = uploads_.pages.back();
    auto* mapped = static_cast<std::uint8_t*>(SDL_MapGPUTransferBuffer(device_, page, false));
    if (mapped == nullptr) {
        throw std::runtime_error(std::string("SDL_MapGPUTransferBuffer (uploads) failed: ") + SDL_GetError());
    }
    std::memcpy(mapped + offset, data, size);
    SDL_UnmapGPUTransferBuffer(device_, page);
    uploads_.page_used = offset + size;
    return {page, static_cast<Uint32>(offset)};
}

void Renderer::flush_uploads() {
    if (uploads_.commands == nullptr) {
        return;
    }
    if (uploads_.pass != nullptr) {
        SDL_EndGPUCopyPass(uploads_.pass);
        uploads_.pass = nullptr;
    }
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(uploads_.commands);
    uploads_.commands = nullptr;
    // A standard page goes round again once the GPU passed the fence; a large one (a big texture)
    // is released, freed by SDL_GPU when the copies are done.
    Uploads::InFlight batch;
    batch.fence = fence;
    for (std::size_t i = 0; i < uploads_.pages.size(); ++i) {
        if (fence != nullptr && !uploads_.large[i]) {
            batch.pages.push_back(uploads_.pages[i]);
        } else {
            SDL_ReleaseGPUTransferBuffer(device_, uploads_.pages[i]);
        }
    }
    if (fence == nullptr) {
        lose_device(std::string("SDL_SubmitGPUCommandBufferAndAcquireFence (uploads) failed: ") + SDL_GetError());
    } else {
        uploads_.in_flight.push_back(std::move(batch));
    }
    uploads_.pages.clear();
    uploads_.large.clear();
    uploads_.page_size = 0;
    uploads_.page_used = 0;
}

void Renderer::release_upload_pages() {
    for (Uploads::InFlight& batch : uploads_.in_flight) {
        SDL_WaitForGPUFences(device_, true, &batch.fence, 1);
        SDL_ReleaseGPUFence(device_, batch.fence);
        uploads_.spare.insert(uploads_.spare.end(), batch.pages.begin(), batch.pages.end());
    }
    uploads_.in_flight.clear();
    for (SDL_GPUTransferBuffer* page : uploads_.spare) {
        SDL_ReleaseGPUTransferBuffer(device_, page);
    }
    uploads_.spare.clear();
}

GpuSampler Renderer::create_sampler(SDL_GPUFilter filter, const char* debug_name) {
    SDL_GPUSamplerCreateInfo info = {};
    info.min_filter = filter;
    info.mag_filter = filter;
    info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    const NameProperty name_property(SDL_PROP_GPU_SAMPLER_CREATE_NAME_STRING, debug_name);
    info.props = name_property.id();
    SDL_GPUSampler* sampler = SDL_CreateGPUSampler(device_, &info);
    if (sampler == nullptr) {
        throw std::runtime_error(std::string("SDL_CreateGPUSampler failed: ") + SDL_GetError());
    }
    return GpuSampler(device_, sampler);
}

void Renderer::set_vsync(bool vsync) {
    flush_uploads();
    SDL_WaitForGPUIdle(device_);
    const SDL_GPUPresentMode mode = choose_present_mode(device_, window_, vsync);
    if (!SDL_SetGPUSwapchainParameters(device_, window_, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode)) {
        SDL_Log("SDL_SetGPUSwapchainParameters failed: %s", SDL_GetError());
    } else {
        SDL_Log("GPU: present=%s", present_mode_name(mode));
    }
}

void Renderer::set_clear_color(float r, float g, float b, float a) {
    clear_color_ = {r, g, b, a};
}

void Renderer::lose_device(const std::string& reason) {
    if (device_lost_.empty()) {
        device_lost_ = reason.empty() ? std::string("unknown GPU failure") : reason;
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "GPU: device lost: %s", device_lost_.c_str());
    }
}

template <typename Prepare>
bool Renderer::prepare_guarded(const char* what, Prepare&& prepare) {
    try {
        prepare();
        return true;
    } catch (const std::exception& e) {
        ++frame_failures_;
        ++stats_.failed_preparations;
        // Logged in full the first times, then once in a while: a failure every frame must not
        // flood the log.
        if (frame_failures_ <= 10 || frame_failures_ % 1000 == 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Renderer: %s skipped this frame (%s; %llu failure(s) so far)", what,
                         e.what(), static_cast<unsigned long long>(frame_failures_));
        }
        return false;
    }
}

bool Renderer::begin_frame() {
    collect_picks();
    MOTEUR_ASSERT_MAIN_THREAD("Renderer");
    MOTEUR_ASSERT(command_buffer_ == nullptr, "Renderer::begin_frame() called again before end_frame()");
    stats_ = {};
    if (device_lost()) {
        return false;
    }
    flush_uploads();  // what was created since the last frame, before anything else
    if (vertex_pool_) {
        vertex_pool_->begin_frame();
        index_pool_->begin_frame();
    }

    command_buffer_ = SDL_AcquireGPUCommandBuffer(device_);
    if (command_buffer_ == nullptr) {
        lose_device(std::string("SDL_AcquireGPUCommandBuffer failed: ") + SDL_GetError());
        return false;
    }

    swapchain_texture_ = nullptr;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(command_buffer_, window_, &swapchain_texture_, &width_, &height_)) {
        lose_device(std::string("SDL_WaitAndAcquireGPUSwapchainTexture failed: ") + SDL_GetError());
        SDL_CancelGPUCommandBuffer(command_buffer_);
        command_buffer_ = nullptr;
        return false;
    }

    if (swapchain_texture_ == nullptr) {
        // Minimized window or similar: not an error. The command buffer still has to be submitted.
        SDL_SubmitGPUCommandBuffer(command_buffer_);
        command_buffer_ = nullptr;
        return false;
    }
    ensure_scene_targets();
    return true;
}

void Renderer::end_frame() {
    MOTEUR_ASSERT_MAIN_THREAD("Renderer");
    MOTEUR_ASSERT(command_buffer_ != nullptr, "Renderer::end_frame() without a begin_frame() that returned true");
    stats_.gpu_timed = gpu_timing_;
    flush_uploads();  // resources created while the frame was recorded: their data first
    // Each part of the frame is recorded into the frame's command buffer. With GPU timing on, it
    // gets its own command buffer instead, submitted and waited for right away (see
    // set_gpu_timing()); the parts still run in the same order.
    const auto part = [this](GpuTime slot, const auto& record) {
        if (!gpu_timing_) {
            record(command_buffer_);
            return;
        }
        SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(device_);
        if (commands == nullptr) {
            record(command_buffer_);
            return;
        }
        record(commands);
        const std::uint64_t start = SDL_GetPerformanceCounter();
        SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
        if (fence != nullptr) {
            SDL_WaitForGPUFences(device_, true, &fence, 1);
            SDL_ReleaseGPUFence(device_, fence);
        }
        stats_.gpu_ms[slot] += static_cast<float>(static_cast<double>(SDL_GetPerformanceCounter() - start) * 1000.0 /
                                                  static_cast<double>(SDL_GetPerformanceFrequency()));
    };

    // Phase 1: copies. They must happen before any render pass opens. A renderer whose preparation
    // fails is skipped for this frame (its render() is not called): it opened no copy pass, so the
    // command buffer stays valid (milestone 9, part 2).
    bool meshes_ok = true;
    bool billboards_ok = true;
    bool lines_ok = true;
    bool sprites_ok = true;
    bool screen_ok = true;
    part(kGpuUpload, [&](SDL_GPUCommandBuffer* commands) {
        meshes_ok = prepare_guarded("meshes", [&] {
            if (simulate_frame_failure_) {
                simulate_frame_failure_ = false;
                throw std::runtime_error("SDL_MapGPUTransferBuffer (mesh instances) failed: provoked on purpose");
            }
            meshes_->prepare(commands, stats_);  // culling and batching of every 3D pass, too
        });
        if (!meshes_ok) {
            meshes_->clear();
        }
        // Debug lines use the meshes' camera when the game gave none, and the meshes add their own.
        if (meshes_->has_work()) {
            if (!debug_lines_->has_camera_) {
                debug_lines_->set_camera(meshes_->view_projection());
            }
            meshes_->add_debug_lines(debug_lines_->lines());
        }
        billboards_ok = prepare_guarded("billboards", [&] { billboards_->prepare(commands, stats_); });
        lines_ok = prepare_guarded("debug lines", [&] { debug_lines_->prepare(commands, stats_); });
        sprites_ok = prepare_guarded("sprites", [&] { sprites_->prepare(commands, stats_); });
        screen_ok = prepare_guarded("screen sprites", [&] { screen_sprites_->prepare(commands, stats_); });
        if (!billboards_ok) {
            billboards_->clear();
        }
        if (!lines_ok) {
            debug_lines_->clear();
        }
#if !defined(MOTEUR_SHIPPING)
        if (debug_ui_) {
            debug_ui_->prepare(commands);
        }
#endif
    });

    // Phase 2: the render passes. The debug groups name them in RenderDoc and Xcode captures.
    const bool has_scene = meshes_->has_work() || billboards_->has_work() || debug_lines_->has_work();
    if (has_scene && meshes_ok && meshes_->wants_shadows()) {
        part(kGpuShadow, [this](SDL_GPUCommandBuffer* commands) {
            SDL_GPUDepthStencilTargetInfo shadow = {};
            shadow.texture = meshes_->shadow_map();
            shadow.clear_depth = 1.0f;
            shadow.load_op = SDL_GPU_LOADOP_CLEAR;
            shadow.store_op = SDL_GPU_STOREOP_STORE;  // read by the "scene" pass
            shadow.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
            shadow.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
            SDL_PushGPUDebugGroup(commands, "shadow");
            SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, nullptr, 0, &shadow);
            if (pass != nullptr) {
                meshes_->render_shadows(commands, pass, stats_);
                SDL_EndGPURenderPass(pass);
            } else {
                SDL_Log("SDL_BeginGPURenderPass (shadow) failed: %s", SDL_GetError());
            }
            SDL_PopGPUDebugGroup(commands);
        });
    }
    if (has_scene && meshes_ok && meshes_->wants_point_shadows()) {
        part(kGpuPointShadows, [this](SDL_GPUCommandBuffer* commands) {
            // Only the tiles of the lights whose shadow changed are drawn: the others are kept (LOAD).
            SDL_GPUDepthStencilTargetInfo atlas = {};
            atlas.texture = meshes_->point_shadow_atlas();
            atlas.clear_depth = 1.0f;
            atlas.load_op = meshes_->point_shadow_atlas_is_new() ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
            atlas.store_op = SDL_GPU_STOREOP_STORE;
            atlas.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
            atlas.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
            SDL_PushGPUDebugGroup(commands, "point shadows");
            SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, nullptr, 0, &atlas);
            if (pass != nullptr) {
                meshes_->render_point_shadows(commands, pass, stats_);
                SDL_EndGPURenderPass(pass);
            } else {
                SDL_Log("SDL_BeginGPURenderPass (point shadows) failed: %s", SDL_GetError());
            }
            SDL_PopGPUDebugGroup(commands);
        });
    }
    if (has_scene) {
        part(kGpuScene, [&](SDL_GPUCommandBuffer* commands) {
            // The 3D world, in linear light. The clear color is given in screen (sRGB) terms like the
            // 2D one, so it is converted to linear first.
            const glm::vec3 clear = srgb_to_linear(glm::vec3(clear_color_.r, clear_color_.g, clear_color_.b));
            // Two passes (milestone 7, soft particles): the opaque meshes, which also write their
            // distance along the view into a second target; then what is blended over them, which
            // reads it. Without anything blended, the first pass ends the scene by itself.
            const bool blended = billboards_->has_work() || debug_lines_->has_work();
            SDL_GPUColorTargetInfo targets[2] = {};
            SDL_GPUColorTargetInfo& scene = targets[0];
            scene.texture = scene_texture_.get();
            scene.clear_color = {clear.r, clear.g, clear.b, 1.0f};
            scene.load_op = SDL_GPU_LOADOP_CLEAR;
            scene.store_op = SDL_GPU_STOREOP_STORE;  // read by the tone mapping
            if (scene_msaa_texture_) {
                // MSAA: the samples are averaged into the plain texture at the end of the scene; kept
                // between the two passes, the second goes on drawing into them.
                scene.texture = scene_msaa_texture_.get();
                scene.resolve_texture = blended ? nullptr : scene_texture_.get();
                scene.store_op = blended ? SDL_GPU_STOREOP_STORE : SDL_GPU_STOREOP_RESOLVE;
            }
            SDL_GPUColorTargetInfo& distance = targets[1];
            distance.texture = distance_texture_.get();
            distance.clear_color = {kFarDistance, 0.0f, 0.0f, 0.0f};
            distance.load_op = SDL_GPU_LOADOP_CLEAR;
            distance.store_op = blended ? SDL_GPU_STOREOP_STORE : SDL_GPU_STOREOP_DONT_CARE;
            if (distance_msaa_texture_) {
                distance.texture = distance_msaa_texture_.get();
                distance.resolve_texture = blended ? distance_texture_.get() : nullptr;
                distance.store_op = blended ? SDL_GPU_STOREOP_RESOLVE : SDL_GPU_STOREOP_DONT_CARE;
            }

            // The depth is kept for the second pass only: DONT_CARE spares tiled GPUs (Apple) writing it back.
            SDL_GPUDepthStencilTargetInfo depth = {};
            depth.texture = depth_texture_.get();
            depth.clear_depth = 1.0f;
            depth.load_op = SDL_GPU_LOADOP_CLEAR;
            depth.store_op = blended ? SDL_GPU_STOREOP_STORE : SDL_GPU_STOREOP_DONT_CARE;
            depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
            depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

            SDL_PushGPUDebugGroup(commands, "scene");
            SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, targets, 2, &depth);
            if (pass != nullptr) {
                if (meshes_ok) {
                    meshes_->render(commands, pass, stats_);
                }
                SDL_EndGPURenderPass(pass);
            } else {
                SDL_Log("SDL_BeginGPURenderPass (scene) failed: %s", SDL_GetError());
            }
            if (blended) {
                SDL_GPUColorTargetInfo over = {};
                over.texture = scene_texture_.get();
                over.load_op = SDL_GPU_LOADOP_LOAD;
                over.store_op = SDL_GPU_STOREOP_STORE;
                if (scene_msaa_texture_) {
                    over.texture = scene_msaa_texture_.get();
                    over.resolve_texture = scene_texture_.get();
                    over.store_op = SDL_GPU_STOREOP_RESOLVE;
                }
                depth.load_op = SDL_GPU_LOADOP_LOAD;
                depth.store_op = SDL_GPU_STOREOP_DONT_CARE;
                pass = SDL_BeginGPURenderPass(commands, &over, 1, &depth);
                if (pass != nullptr) {
                    if (billboards_ok) {
                        billboards_->render(commands, pass, stats_);  // after the opaque meshes, blended
                    }
                    if (lines_ok) {
                        debug_lines_->render(commands, pass, stats_);  // last: some are drawn over everything
                    }
                    SDL_EndGPURenderPass(pass);
                } else {
                    SDL_Log("SDL_BeginGPURenderPass (scene, blended) failed: %s", SDL_GetError());
                }
            }
            SDL_PopGPUDebugGroup(commands);
            if (meshes_ok && meshes_->ids()) {
                // The id pass (milestone 9, part 7): the meshes again, their pick ids, their own depth.
                ensure_id_targets();
                SDL_GPUColorTargetInfo ids = {};
                ids.texture = id_texture_.get();
                ids.clear_color = {0.0f, 0.0f, 0.0f, 0.0f};
                ids.load_op = SDL_GPU_LOADOP_CLEAR;
                ids.store_op = SDL_GPU_STOREOP_STORE;
                SDL_GPUDepthStencilTargetInfo id_depth = {};
                id_depth.texture = id_depth_.get();
                id_depth.clear_depth = 1.0f;
                id_depth.load_op = SDL_GPU_LOADOP_CLEAR;
                id_depth.store_op = SDL_GPU_STOREOP_DONT_CARE;
                id_depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
                id_depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
                SDL_PushGPUDebugGroup(commands, "ids");
                SDL_GPURenderPass* id_pass = SDL_BeginGPURenderPass(commands, &ids, 1, &id_depth);
                if (id_pass != nullptr) {
                    meshes_->render_ids(commands, id_pass, stats_);
                    SDL_EndGPURenderPass(id_pass);
                } else {
                    SDL_Log("SDL_BeginGPURenderPass (ids) failed: %s", SDL_GetError());
                }
                SDL_PopGPUDebugGroup(commands);
            }
        });
    }

    // The debug views other than the wireframe are meant to be seen as they are.
    const MeshView view = meshes_->view();
    const bool raw_view = view != MeshView::Lit && view != MeshView::Wireframe;
    const bool fxaa = has_scene && ldr_texture_;
    if (fxaa) {
        // FXAA works on screen colors: the tone mapping first, into a texture of its own.
        part(kGpuCompose, [&](SDL_GPUCommandBuffer* commands) {
            SDL_GPUColorTargetInfo ldr = {};
            ldr.texture = ldr_texture_.get();
            ldr.load_op = SDL_GPU_LOADOP_DONT_CARE;  // every pixel is written
            ldr.store_op = SDL_GPU_STOREOP_STORE;
            SDL_PushGPUDebugGroup(commands, "tonemap");
            SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &ldr, 1, nullptr);
            if (pass != nullptr) {
                tone_mapper_->set_outline(meshes_->ids() && id_texture_ ? id_texture_.get() : nullptr, outline_id_,
                                          {static_cast<int>(id_width_), static_cast<int>(id_height_)}, outline_color_,
                                          outline_thickness_);
                tone_mapper_->render(commands, pass, scene_texture_.get(), exposure_, raw_view);
                SDL_EndGPURenderPass(pass);
            } else {
                SDL_Log("SDL_BeginGPURenderPass (tonemap) failed: %s", SDL_GetError());
            }
            SDL_PopGPUDebugGroup(commands);
        });
    }

    // The screen: the tone-mapped scene covers every pixel, so there is nothing to clear then.
    SDL_GPUColorTargetInfo target = {};
    target.texture = swapchain_texture_;
    target.clear_color = clear_color_;
    target.load_op = has_scene ? SDL_GPU_LOADOP_DONT_CARE : SDL_GPU_LOADOP_CLEAR;
    target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_PushGPUDebugGroup(command_buffer_, "compose");
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(command_buffer_, &target, 1, nullptr);
    if (pass != nullptr) {
        if (fxaa) {
            fxaa_->render(command_buffer_, pass, ldr_texture_.get(), scene_width_, scene_height_);
        } else if (has_scene) {
            tone_mapper_->set_outline(meshes_->ids() && id_texture_ ? id_texture_.get() : nullptr, outline_id_,
                                      {static_cast<int>(id_width_), static_cast<int>(id_height_)}, outline_color_,
                                      outline_thickness_);
            tone_mapper_->render(command_buffer_, pass, scene_texture_.get(), exposure_, raw_view);
        }
        render_debug_texture(pass);
        if (sprites_ok) {
            sprites_->render(command_buffer_, pass, width_, height_, stats_);
        }
        if (screen_ok) {
            screen_sprites_->render(command_buffer_, pass, width_, height_, stats_);
        }
#if !defined(MOTEUR_SHIPPING)
        if (debug_ui_) {
            debug_ui_->render(command_buffer_, pass);  // on top of everything
        }
#endif
        SDL_EndGPURenderPass(pass);
    } else {
        SDL_Log("SDL_BeginGPURenderPass (compose) failed: %s", SDL_GetError());
    }
    SDL_PopGPUDebugGroup(command_buffer_);
    meshes_->clear();
    billboards_->clear();
    debug_lines_->clear();
    sprites_->clear();
    screen_sprites_->clear();

    // Phase 3, optional: a copy pass reading the just-drawn swapchain image back to the CPU, for
    // request_capture(). Must happen before submit, while swapchain_texture_ is still valid.
    GpuTransferBuffer download;
    const std::uint32_t capture_width = width_;
    const std::uint32_t capture_height = height_;
    if (!capture_path_.empty()) {
        const std::size_t size = static_cast<std::size_t>(capture_width) * capture_height * 4;
        SDL_GPUTransferBufferCreateInfo info = {};
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        info.size = static_cast<Uint32>(size);
        SDL_GPUTransferBuffer* raw = SDL_CreateGPUTransferBuffer(device_, &info);
        if (raw == nullptr) {
            SDL_Log("SDL_CreateGPUTransferBuffer (capture) failed: %s", SDL_GetError());
        } else {
            download = GpuTransferBuffer(device_, raw);
            SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(command_buffer_);
            SDL_GPUTextureRegion region = {};
            region.texture = swapchain_texture_;
            region.w = capture_width;
            region.h = capture_height;
            region.d = 1;
            SDL_GPUTextureTransferInfo destination = {};
            destination.transfer_buffer = raw;
            destination.pixels_per_row = capture_width;
            destination.rows_per_layer = capture_height;
            SDL_DownloadFromGPUTexture(copy_pass, &region, &destination);
            SDL_EndGPUCopyPass(copy_pass);
        }
    }

    // The pixel under the pointer, from the id pass, read back once this frame is done.
    PickSlot* pick_slot = nullptr;
    if (pick_requested_ && meshes_->ids() && id_texture_ && width_ > 0 && height_ > 0) {
        PickSlot& slot = pick_slots_[next_pick_slot_];
        if (slot.fence == nullptr) {
            if (!slot.buffer) {
                SDL_GPUTransferBufferCreateInfo info = {};
                info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
                info.size = 4;
                if (SDL_GPUTransferBuffer* raw = SDL_CreateGPUTransferBuffer(device_, &info)) {
                    slot.buffer = GpuTransferBuffer(device_, raw);
                }
            }
            if (slot.buffer) {
                const auto x = static_cast<std::uint32_t>(std::clamp(pick_pixel_.x * static_cast<float>(id_width_) / static_cast<float>(width_),
                                                                     0.0f, static_cast<float>(id_width_ - 1)));
                const auto y = static_cast<std::uint32_t>(std::clamp(pick_pixel_.y * static_cast<float>(id_height_) / static_cast<float>(height_),
                                                                     0.0f, static_cast<float>(id_height_ - 1)));
                SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(command_buffer_);
                SDL_GPUTextureRegion region = {};
                region.texture = id_texture_.get();
                region.x = x;
                region.y = y;
                region.w = 1;
                region.h = 1;
                region.d = 1;
                SDL_GPUTextureTransferInfo destination = {};
                destination.transfer_buffer = slot.buffer.get();
                SDL_DownloadFromGPUTexture(copy_pass, &region, &destination);
                SDL_EndGPUCopyPass(copy_pass);
                slot.pixel = pick_pixel_;
                pick_slot = &slot;
                next_pick_slot_ = (next_pick_slot_ + 1) % 3;
            }
        }
    }
    pick_requested_ = false;

    if (pick_slot != nullptr && !gpu_timing_) {
        // Its fence tells when the id can be read (collect_picks(), at a next frame).
        pick_slot->fence = SDL_SubmitGPUCommandBufferAndAcquireFence(command_buffer_);
        if (pick_slot->fence == nullptr) {
            lose_device(std::string("SDL_SubmitGPUCommandBufferAndAcquireFence failed: ") + SDL_GetError());
        }
    } else if (gpu_timing_) {
        // The compose pass (and the capture read-back, if any) is what is left in this buffer.
        const std::uint64_t start = SDL_GetPerformanceCounter();
        SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(command_buffer_);
        if (fence == nullptr) {
            lose_device(std::string("SDL_SubmitGPUCommandBufferAndAcquireFence failed: ") + SDL_GetError());
        } else {
            SDL_WaitForGPUFences(device_, true, &fence, 1);
            if (pick_slot != nullptr) {
                pick_slot->fence = fence;  // signalled: read at the next collect_picks()
            } else {
                SDL_ReleaseGPUFence(device_, fence);
            }
            stats_.gpu_ms[kGpuCompose] += static_cast<float>(static_cast<double>(SDL_GetPerformanceCounter() - start) *
                                                             1000.0 / static_cast<double>(SDL_GetPerformanceFrequency()));
        }
    } else if (!SDL_SubmitGPUCommandBuffer(command_buffer_)) {
        lose_device(std::string("SDL_SubmitGPUCommandBuffer failed: ") + SDL_GetError());
    }
    command_buffer_ = nullptr;
    swapchain_texture_ = nullptr;

    if (download) {
        SDL_WaitForGPUIdle(device_);  // the transfer buffer is only readable once the GPU is done
        void* mapped = SDL_MapGPUTransferBuffer(device_, download.get(), false);
        if (mapped == nullptr) {
            SDL_Log("SDL_MapGPUTransferBuffer (capture) failed: %s", SDL_GetError());
        } else {
            write_capture_png(capture_path_, mapped, capture_width, capture_height, swapchain_format(), capture_max_width_);
            SDL_UnmapGPUTransferBuffer(device_, download.get());
        }
    }
    capture_path_.clear();
}

}  // namespace moteur
