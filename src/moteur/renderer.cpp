#include "moteur/renderer.hpp"

#include <cstring>
#include <stdexcept>
#include <string>

#include "moteur/paths.hpp"
#include "moteur/screenshot.hpp"
#include "moteur/sprite_renderer.hpp"

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
    SDL_Log("GPU: backend=%s, device=%s, present=%s, debug=%s", SDL_GetGPUDeviceDriver(device_), gpu_name,
            present_mode_name(present_mode), config.debug ? "on" : "off");

    try {
        sprites_ = std::make_unique<SpriteRenderer>(*this);
    } catch (...) {
        // The destructor does not run when a constructor throws: clean up here.
        sprites_.reset();
        SDL_WaitForGPUIdle(device_);
        SDL_ReleaseWindowFromGPUDevice(device_, window_);
        SDL_DestroyGPUDevice(device_);
        throw;
    }
}

Renderer::~Renderer() {
    SDL_WaitForGPUIdle(device_);
    sprites_.reset();  // its GPU resources must be released before the device
    SDL_ReleaseWindowFromGPUDevice(device_, window_);
    SDL_DestroyGPUDevice(device_);
}

SpriteRenderer& Renderer::sprites() {
    return *sprites_;
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
    // Stored premultiplied: see premultiply_alpha(). The sprite pipeline blends with that in mind.
    Image image = straight_image;
    premultiply_alpha(image);

    SDL_GPUTextureCreateInfo info = {};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = static_cast<Uint32>(image.width);
    info.height = static_cast<Uint32>(image.height);
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
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
    SDL_SubmitGPUCommandBuffer(commands);
    SDL_WaitForGPUIdle(device_);

    SDL_ReleaseGPUTransferBuffer(device_, transfer);
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
    return true;
}

void Renderer::end_frame() {
    // Phase 1: copies. They must happen before the render pass opens.
    sprites_->prepare(command_buffer_, stats_);

    // Phase 2: drawing.
    SDL_GPUColorTargetInfo target = {};
    target.texture = swapchain_texture_;
    target.clear_color = clear_color_;
    target.load_op = SDL_GPU_LOADOP_CLEAR;
    target.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(command_buffer_, &target, 1, nullptr);
    if (pass != nullptr) {
        sprites_->render(command_buffer_, pass, width_, height_, stats_);
        SDL_EndGPURenderPass(pass);
    } else {
        SDL_Log("SDL_BeginGPURenderPass failed: %s", SDL_GetError());
    }
    sprites_->clear();

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
