#include "moteur/renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>

#include "moteur/billboard_renderer.hpp"
#include "moteur/debug_lines.hpp"
#include "moteur/debug_ui.hpp"
#include "moteur/ktx_texture.hpp"
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

// Creates a CPU-visible staging buffer filled with `data`.
SDL_GPUTransferBuffer* make_filled_transfer_buffer(SDL_GPUDevice* device, const void* data, std::size_t size) {
    SDL_GPUTransferBufferCreateInfo info = {};
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    info.size = static_cast<Uint32>(size);
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &info);
    if (transfer == nullptr) {
        throw std::runtime_error(std::string("SDL_CreateGPUTransferBuffer failed: ") + SDL_GetError());
    }

    void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
    if (mapped == nullptr) {
        const std::string error = SDL_GetError();
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        throw std::runtime_error("SDL_MapGPUTransferBuffer failed: " + error);
    }
    std::memcpy(mapped, data, size);
    SDL_UnmapGPUTransferBuffer(device, transfer);
    return transfer;
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
        if (config.debug_ui) {
            debug_ui_ = std::make_unique<DebugUi>(window_, device_, swapchain_format(), config.debug_ui_font,
                                                  config.debug_ui_font_size);
        }
    } catch (...) {
        // The destructor does not run when a constructor throws: clean up here.
        debug_ui_.reset();
        screen_sprites_.reset();
        sprites_.reset();
        depth_view_.reset();
        fxaa_.reset();
        tone_mapper_.reset();
        debug_lines_.reset();
        billboards_.reset();
        meshes_.reset();
        SDL_WaitForGPUIdle(device_);
        SDL_ReleaseWindowFromGPUDevice(device_, window_);
        SDL_DestroyGPUDevice(device_);
        throw;
    }
}

Renderer::~Renderer() {
    SDL_WaitForGPUIdle(device_);
    // Every GPU resource must be released before the device.
    debug_ui_.reset();
    screen_sprites_.reset();
    sprites_.reset();
    depth_view_.reset();
    fxaa_.reset();
    tone_mapper_.reset();
    debug_lines_.reset();
    billboards_.reset();
    meshes_.reset();
    scene_texture_ = {};
    scene_msaa_texture_ = {};
    depth_texture_ = {};
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
        return GpuTexture(device_, raw);
    };
    // With MSAA the pass draws into a multisampled texture (never sampled), resolved into the plain
    // one at its end.
    scene_texture_ = create(kSceneFormat, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, "scene color");
    scene_msaa_texture_ = samples == SDL_GPU_SAMPLECOUNT_1
                              ? GpuTexture()
                              : create(kSceneFormat, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, "scene color (msaa)", samples);
    depth_texture_ = create(depth_format_, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET, "scene depth", samples);
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
        std::size_t size = 0;
        void* code = SDL_LoadFile(path.c_str(), &size);
        if (code == nullptr) {
            tried += "\n  " + path;
            continue;
        }

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
            throw std::runtime_error("SDL_CreateGPUShader failed for " + path + ": " + error);
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
    GpuBuffer buffer(device_, raw);  // released automatically if anything below throws

    SDL_GPUTransferBuffer* transfer = make_filled_transfer_buffer(device_, data, size);

    // The copy is recorded in its own command buffer, outside any render pass.
    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(device_);
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(commands);
    SDL_GPUTransferBufferLocation source = {transfer, 0};
    SDL_GPUBufferRegion destination = {raw, 0, static_cast<Uint32>(size)};
    SDL_UploadToGPUBuffer(copy_pass, &source, &destination, false);
    SDL_EndGPUCopyPass(copy_pass);
    SDL_SubmitGPUCommandBuffer(commands);
    SDL_WaitForGPUIdle(device_);

    SDL_ReleaseGPUTransferBuffer(device_, transfer);
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
    return GpuBuffer(device_, buffer);
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
    texture.gpu = GpuTexture(device_, raw);  // released automatically if anything below throws
    texture.width = image.width;
    texture.height = image.height;
    for (Uint32 level = 0; level < levels; ++level) {
        const std::size_t level_width = std::max(1, image.width >> level);
        const std::size_t level_height = std::max(1, image.height >> level);
        texture.gpu_bytes += level_width * level_height * 4;
    }

    SDL_GPUTransferBuffer* transfer = make_filled_transfer_buffer(device_, image.pixels.data(), image.pixels.size());

    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(device_);
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(commands);
    SDL_GPUTextureTransferInfo source = {};
    source.transfer_buffer = transfer;
    source.pixels_per_row = static_cast<Uint32>(image.width);
    source.rows_per_layer = static_cast<Uint32>(image.height);
    SDL_GPUTextureRegion destination = {};
    destination.texture = raw;
    destination.w = static_cast<Uint32>(image.width);
    destination.h = static_cast<Uint32>(image.height);
    destination.d = 1;
    SDL_UploadToGPUTexture(copy_pass, &source, &destination, false);
    SDL_EndGPUCopyPass(copy_pass);
    if (levels > 1) {
        // Filtered in linear space for an sRGB texture (the GPU converts on each read and write).
        SDL_GenerateMipmapsForGPUTexture(commands, raw);
    }
    SDL_SubmitGPUCommandBuffer(commands);
    SDL_WaitForGPUIdle(device_);

    SDL_ReleaseGPUTransferBuffer(device_, transfer);
    return texture;
}

Texture Renderer::create_texture(const CompressedImage& image, const char* debug_name) {
    Texture texture;
    texture.gpu = create_texture_levels(image.format, image.width, image.height, image.levels, debug_name);
    texture.width = image.width;
    texture.height = image.height;
    for (const std::vector<std::uint8_t>& level : image.levels) {
        texture.gpu_bytes += level.size();
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
    GpuTexture texture(device_, raw);

    std::vector<SDL_GPUTransferBuffer*> transfers;
    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(device_);
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(commands);
    for (std::size_t level = 0; level < levels.size(); ++level) {
        const Uint32 level_width = std::max<Uint32>(1, static_cast<Uint32>(width) >> level);
        const Uint32 level_height = std::max<Uint32>(1, static_cast<Uint32>(height) >> level);
        SDL_GPUTransferBuffer* transfer = make_filled_transfer_buffer(device_, levels[level].data(), levels[level].size());
        transfers.push_back(transfer);
        SDL_GPUTextureTransferInfo source = {};
        source.transfer_buffer = transfer;
        source.pixels_per_row = level_width;
        source.rows_per_layer = level_height;
        SDL_GPUTextureRegion destination = {};
        destination.texture = raw;
        destination.mip_level = static_cast<Uint32>(level);
        destination.w = level_width;
        destination.h = level_height;
        destination.d = 1;
        SDL_UploadToGPUTexture(copy_pass, &source, &destination, false);
    }
    SDL_EndGPUCopyPass(copy_pass);
    SDL_SubmitGPUCommandBuffer(commands);
    SDL_WaitForGPUIdle(device_);
    for (SDL_GPUTransferBuffer* transfer : transfers) {
        SDL_ReleaseGPUTransferBuffer(device_, transfer);
    }
    return texture;
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

void Renderer::set_clear_color(float r, float g, float b, float a) {
    clear_color_ = {r, g, b, a};
}

bool Renderer::begin_frame() {
    stats_ = {};

    command_buffer_ = SDL_AcquireGPUCommandBuffer(device_);
    if (command_buffer_ == nullptr) {
        SDL_Log("SDL_AcquireGPUCommandBuffer failed: %s", SDL_GetError());
        return false;
    }

    swapchain_texture_ = nullptr;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(command_buffer_, window_, &swapchain_texture_, &width_, &height_)) {
        SDL_Log("SDL_WaitAndAcquireGPUSwapchainTexture failed: %s", SDL_GetError());
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
    stats_.gpu_timed = gpu_timing_;
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

    // Phase 1: copies. They must happen before any render pass opens.
    part(kGpuUpload, [this](SDL_GPUCommandBuffer* commands) {
        meshes_->prepare(commands, stats_);  // culling and batching of every 3D pass, too
        // Debug lines use the meshes' camera when the game gave none, and the meshes add their own.
        if (meshes_->has_work()) {
            if (!debug_lines_->has_camera_) {
                debug_lines_->set_camera(meshes_->view_projection());
            }
            meshes_->add_debug_lines(debug_lines_->lines());
        }
        billboards_->prepare(commands, stats_);
        debug_lines_->prepare(commands, stats_);
        sprites_->prepare(commands, stats_);
        screen_sprites_->prepare(commands, stats_);
        if (debug_ui_) {
            debug_ui_->prepare(commands);
        }
    });

    // Phase 2: the render passes. The debug groups name them in RenderDoc and Xcode captures.
    const bool has_scene = meshes_->has_work() || billboards_->has_work() || debug_lines_->has_work();
    if (has_scene && meshes_->wants_shadows()) {
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
    if (has_scene && meshes_->wants_point_shadows()) {
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
        part(kGpuScene, [this](SDL_GPUCommandBuffer* commands) {
            // The 3D world, in linear light. The clear color is given in screen (sRGB) terms like the
            // 2D one, so it is converted to linear first.
            const glm::vec3 clear = srgb_to_linear(glm::vec3(clear_color_.r, clear_color_.g, clear_color_.b));
            SDL_GPUColorTargetInfo scene = {};
            scene.texture = scene_texture_.get();
            scene.clear_color = {clear.r, clear.g, clear.b, 1.0f};
            scene.load_op = SDL_GPU_LOADOP_CLEAR;
            scene.store_op = SDL_GPU_STOREOP_STORE;  // read by the tone mapping
            if (scene_msaa_texture_) {
                // MSAA: the samples are averaged into the plain texture, and not kept themselves.
                scene.texture = scene_msaa_texture_.get();
                scene.resolve_texture = scene_texture_.get();
                scene.store_op = SDL_GPU_STOREOP_RESOLVE;
            }

            // Nothing reads the depth after the pass: DONT_CARE spares tiled GPUs (Apple) writing it back.
            SDL_GPUDepthStencilTargetInfo depth = {};
            depth.texture = depth_texture_.get();
            depth.clear_depth = 1.0f;
            depth.load_op = SDL_GPU_LOADOP_CLEAR;
            depth.store_op = SDL_GPU_STOREOP_DONT_CARE;
            depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
            depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

            SDL_PushGPUDebugGroup(commands, "scene");
            SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &scene, 1, &depth);
            if (pass != nullptr) {
                meshes_->render(commands, pass, stats_);
                billboards_->render(commands, pass, stats_);   // after the opaque meshes, blended
                debug_lines_->render(commands, pass, stats_);  // last: some are drawn over everything
                SDL_EndGPURenderPass(pass);
            } else {
                SDL_Log("SDL_BeginGPURenderPass (scene) failed: %s", SDL_GetError());
            }
            SDL_PopGPUDebugGroup(commands);
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
            tone_mapper_->render(command_buffer_, pass, scene_texture_.get(), exposure_, raw_view);
        }
        render_debug_texture(pass);
        sprites_->render(command_buffer_, pass, width_, height_, stats_);
        screen_sprites_->render(command_buffer_, pass, width_, height_, stats_);
        if (debug_ui_) {
            debug_ui_->render(command_buffer_, pass);  // on top of everything
        }
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

    if (gpu_timing_) {
        // The compose pass (and the capture read-back, if any) is what is left in this buffer.
        const std::uint64_t start = SDL_GetPerformanceCounter();
        SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(command_buffer_);
        if (fence == nullptr) {
            SDL_Log("SDL_SubmitGPUCommandBufferAndAcquireFence failed: %s", SDL_GetError());
        } else {
            SDL_WaitForGPUFences(device_, true, &fence, 1);
            SDL_ReleaseGPUFence(device_, fence);
            stats_.gpu_ms[kGpuCompose] += static_cast<float>(static_cast<double>(SDL_GetPerformanceCounter() - start) *
                                                             1000.0 / static_cast<double>(SDL_GetPerformanceFrequency()));
        }
    } else if (!SDL_SubmitGPUCommandBuffer(command_buffer_)) {
        SDL_Log("SDL_SubmitGPUCommandBuffer failed: %s", SDL_GetError());
    }
    command_buffer_ = nullptr;
    swapchain_texture_ = nullptr;

    if (download) {
        SDL_WaitForGPUIdle(device_);  // the transfer buffer is only readable once the GPU is done
        void* mapped = SDL_MapGPUTransferBuffer(device_, download.get(), false);
        if (mapped == nullptr) {
            SDL_Log("SDL_MapGPUTransferBuffer (capture) failed: %s", SDL_GetError());
        } else {
            write_capture_png(capture_path_, mapped, capture_width, capture_height, swapchain_format());
            SDL_UnmapGPUTransferBuffer(device_, download.get());
        }
    }
    capture_path_.clear();
}

}  // namespace moteur
