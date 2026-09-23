#include "moteur/renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

#include "moteur/debug_ui.hpp"
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

}  // namespace

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
    SDL_Log("GPU: backend=%s, device=%s, present=%s, debug=%s, depth=%s", SDL_GetGPUDeviceDriver(device_),
            gpu_name, present_mode_name(present_mode), config.debug ? "on" : "off", depth_format_name());

    try {
        meshes_ = std::make_unique<MeshRenderer>(*this, kSceneFormat, depth_format_);
        tone_mapper_ = std::make_unique<ToneMapper>(*this, swapchain_format());
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
        tone_mapper_.reset();
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
    tone_mapper_.reset();
    meshes_.reset();
    scene_texture_ = {};
    depth_texture_ = {};
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

void Renderer::set_exposure(float exposure) {
    exposure_ = std::max(exposure, 0.001f);
}

void Renderer::ensure_scene_targets() {
    const auto scaled = [this](std::uint32_t size) {
        return std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::lround(static_cast<float>(size) * render_scale_)));
    };
    const std::uint32_t width = scaled(width_);
    const std::uint32_t height = scaled(height_);
    if (scene_texture_ && scene_width_ == width && scene_height_ == height) {
        return;
    }
    // A frame still in flight may use the previous textures: SDL only frees them once the GPU is done.
    const auto create = [&](SDL_GPUTextureFormat format, SDL_GPUTextureUsageFlags usage, const char* name) {
        SDL_GPUTextureCreateInfo info = {};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = format;
        info.usage = usage;
        info.width = width;
        info.height = height;
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        info.sample_count = SDL_GPU_SAMPLECOUNT_1;
        const NameProperty name_property(SDL_PROP_GPU_TEXTURE_CREATE_NAME_STRING, name);
        info.props = name_property.id();
        SDL_GPUTexture* raw = SDL_CreateGPUTexture(device_, &info);
        if (raw == nullptr) {
            throw std::runtime_error(std::string("SDL_CreateGPUTexture (") + name + ") failed: " + SDL_GetError());
        }
        return GpuTexture(device_, raw);
    };
    scene_texture_ = create(kSceneFormat, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, "scene color");
    depth_texture_ = create(depth_format_, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET, "scene depth");
    scene_width_ = width;
    scene_height_ = height;
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
    // Phase 1: copies. They must happen before any render pass opens.
    sprites_->prepare(command_buffer_, stats_);
    screen_sprites_->prepare(command_buffer_, stats_);
    if (debug_ui_) {
        debug_ui_->prepare(command_buffer_);
    }

    // Phase 2: the render passes. The debug groups name them in RenderDoc and Xcode captures.
    const bool has_scene = meshes_->has_work();
    if (has_scene) {
        // The 3D world, in linear light. The clear color is given in screen (sRGB) terms like the
        // 2D one, so it is converted to linear first.
        const glm::vec3 clear = srgb_to_linear(glm::vec3(clear_color_.r, clear_color_.g, clear_color_.b));
        SDL_GPUColorTargetInfo scene = {};
        scene.texture = scene_texture_.get();
        scene.clear_color = {clear.r, clear.g, clear.b, 1.0f};
        scene.load_op = SDL_GPU_LOADOP_CLEAR;
        scene.store_op = SDL_GPU_STOREOP_STORE;  // read by the tone mapping

        // Nothing reads the depth after the pass: DONT_CARE spares tiled GPUs (Apple) writing it back.
        SDL_GPUDepthStencilTargetInfo depth = {};
        depth.texture = depth_texture_.get();
        depth.clear_depth = 1.0f;
        depth.load_op = SDL_GPU_LOADOP_CLEAR;
        depth.store_op = SDL_GPU_STOREOP_DONT_CARE;
        depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

        SDL_PushGPUDebugGroup(command_buffer_, "scene");
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(command_buffer_, &scene, 1, &depth);
        if (pass != nullptr) {
            meshes_->render(command_buffer_, pass, stats_);
            SDL_EndGPURenderPass(pass);
        } else {
            SDL_Log("SDL_BeginGPURenderPass (scene) failed: %s", SDL_GetError());
        }
        SDL_PopGPUDebugGroup(command_buffer_);
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
        if (has_scene) {
            tone_mapper_->render(command_buffer_, pass, scene_texture_.get(), exposure_);
        }
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

    if (!SDL_SubmitGPUCommandBuffer(command_buffer_)) {
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
