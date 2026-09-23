#pragma once

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "moteur/gpu_resource.hpp"
#include "moteur/image.hpp"

namespace moteur {

class DebugUi;
class SpriteRenderer;

struct RendererConfig {
    bool debug = false;  // enables the GPU backend's validation layer
    bool vsync = true;
    bool debug_ui = false;          // creates a DebugUi (Dear ImGui), drawn on top of the sprites
    std::string debug_ui_font;      // TrueType font of the DebugUi; empty for ImGui's built-in one
    float debug_ui_font_size = 16.0f;
};

// What a shader declares, needed by SDL_GPU to validate bindings.
struct ShaderInfo {
    SDL_GPUShaderStage stage = SDL_GPU_SHADERSTAGE_VERTEX;
    std::uint32_t samplers = 0;
    std::uint32_t uniform_buffers = 0;
    std::uint32_t storage_buffers = 0;
    std::uint32_t storage_textures = 0;
};

// A GPU texture and its size in pixels.
struct Texture {
    GpuTexture gpu;
    int width = 0;
    int height = 0;
};

// Counters filled while a frame is executed, valid from end_frame() until the next begin_frame().
struct RenderStats {
    int sprites = 0;
    int draw_calls = 0;
    std::size_t bytes_uploaded = 0;  // vertex data sent to the GPU this frame
};

// Owns the GPU device and drives one frame at a time.
//
// A frame has two phases, because SDL_GPU forbids copying data to the GPU inside a render pass:
//
//   begin_frame()   acquire the swapchain image
//   ... the game records what it wants to draw (renderer.sprites().draw(...)) ...
//   end_frame()     upload data (copy pass), draw everything (render pass), submit
//
// Nothing is sent to the GPU while the game records, so recording is cheap and the engine is free
// to sort and batch what was recorded.
class Renderer {
public:
    // Throws std::runtime_error if no GPU backend is available.
    // The window must outlive the renderer.
    Renderer(SDL_Window* window, const RendererConfig& config);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void set_clear_color(float r, float g, float b, float a = 1.0f);

    // Returns false when nothing can be drawn (for example a minimized window); in that case
    // record nothing and do not call end_frame().
    bool begin_frame();
    void end_frame();

    // Reads back the very next frame's swapchain image and writes it to `path` as a PNG, for
    // automated pixel comparisons (deterministic scene + fixed tick -> reproducible capture).
    // One-shot: cleared after end_frame() attempts it, whether or not that attempt succeeds. Stalls
    // the GPU until the frame is done; never call this every frame, only for tests and tools.
    void request_capture(std::string path) { capture_path_ = std::move(path); }

    // Sprite drawing, recorded between begin_frame() and end_frame().
    SpriteRenderer& sprites();

    // The debug interface, or null when RendererConfig::debug_ui is off. Its windows are drawn
    // after the sprites, in window pixels.
    DebugUi* debug_ui() { return debug_ui_.get(); }

    // Statistics of the frame that just ended.
    const RenderStats& stats() const { return stats_; }

    // Loads shaders/<name>.<ext> next to the executable, where <ext> depends on the active
    // backend (dxil, msl, spv). Throws std::runtime_error if the file is missing or the shader
    // is rejected. The shader is named `name` for graphics debuggers (RenderDoc, Xcode).
    GpuShader load_shader(const std::string& name, const ShaderInfo& info);

    // Resource creation. Data is uploaded immediately and the call waits for the GPU to finish,
    // which is fine while loading but must not be used inside a frame.
    // All of these throw std::runtime_error on failure.
    //
    // `debug_name`, when given, labels the resource for graphics debuggers (RenderDoc, Xcode):
    // captured frames show "sprite.vertices" instead of an anonymous buffer. It costs nothing at
    // runtime and is not required; leave it null for a resource not worth naming individually
    // (for example one of many identical glyph textures).

    // usage: SDL_GPU_BUFFERUSAGE_VERTEX or SDL_GPU_BUFFERUSAGE_INDEX.
    GpuBuffer create_buffer(SDL_GPUBufferUsageFlags usage, const void* data, std::size_t size,
                            const char* debug_name = nullptr);

    // A buffer of `size` bytes whose content is undefined until an upload writes it. Unlike the
    // call above it does not send or wait for anything, so it is cheap enough for a growing buffer.
    GpuBuffer create_buffer(SDL_GPUBufferUsageFlags usage, std::size_t size, const char* debug_name = nullptr);

    // Staging buffer that the CPU writes and a copy pass then sends to the GPU.
    GpuTransferBuffer create_transfer_buffer(std::size_t size);

    // RGBA8 texture without mipmaps, stored as plain UNORM (no sRGB conversion) and with the
    // alpha premultiplied into the color, whatever the alpha of `image`.
    Texture create_texture(const Image& image, const char* debug_name = nullptr);

    // filter: SDL_GPU_FILTER_NEAREST for pixel art, SDL_GPU_FILTER_LINEAR for smooth scaling.
    // Coordinates outside [0, 1] are clamped to the edge.
    GpuSampler create_sampler(SDL_GPUFilter filter, const char* debug_name = nullptr);

    // Pixel format of the window's swapchain, needed to build a graphics pipeline.
    SDL_GPUTextureFormat swapchain_format() const { return SDL_GetGPUSwapchainTextureFormat(device_, window_); }

    SDL_GPUDevice* device() const { return device_; }

    // Size of the swapchain image of the current frame, in pixels.
    std::uint32_t width() const { return width_; }
    std::uint32_t height() const { return height_; }

private:
    SDL_Window* window_ = nullptr;
    SDL_GPUDevice* device_ = nullptr;
    SDL_FColor clear_color_ = {0.1f, 0.1f, 0.15f, 1.0f};

    SDL_GPUCommandBuffer* command_buffer_ = nullptr;
    SDL_GPUTexture* swapchain_texture_ = nullptr;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    RenderStats stats_;
    std::string capture_path_;

    std::unique_ptr<SpriteRenderer> sprites_;
    std::unique_ptr<DebugUi> debug_ui_;
};

}  // namespace moteur
