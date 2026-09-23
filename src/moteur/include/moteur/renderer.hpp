#pragma once

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "moteur/gpu_resource.hpp"
#include "moteur/image.hpp"

namespace moteur {

class DebugUi;
class MeshRenderer;
class SpriteRenderer;
class ToneMapper;

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

// How create_texture() stores an image.
struct TextureSettings {
    // true: the pixels are sRGB colors (base color textures, photos), and the GPU converts them to
    // linear values when a shader reads them. false: stored as they are (sprites and interface,
    // normal maps and other data).
    bool srgb = false;
    // true: a full chain of mipmaps, computed by the GPU (3D surfaces seen at a distance).
    bool mipmaps = false;
    // true: the color is multiplied by the alpha first (what the sprite pipeline blends with).
    bool premultiply = true;
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
    int meshes = 0;                  // 3D meshes drawn
    std::size_t triangles = 0;       // of those meshes
    int draw_calls = 0;              // sprites and meshes together
    int dropped_lights = 0;          // point lights beyond MeshRenderer::kMaxPointLights, ignored
    std::size_t bytes_uploaded = 0;  // vertex data sent to the GPU this frame
};

// Owns the GPU device and drives one frame at a time.
//
// A frame has two phases, because SDL_GPU forbids copying data to the GPU inside a render pass:
//
//   begin_frame()   acquire the swapchain image
//   ... the game records what it wants to draw (renderer.sprites().draw(...)) ...
//   end_frame()     upload data (copy passes), then the render passes, then submit
//
// The render passes, in order:
//
//   "scene"     only when meshes were recorded: the 3D world (meshes()), in linear HDR colors, into
//               an offscreen float texture with its depth, at the render resolution (render scale)
//   "compose"   the swapchain: the scene tone mapped to screen colors (or the clear color without
//               3D), then the world sprites (sprites()), the interface in window pixels
//               (screen_sprites()), and ImGui on top
//
// A frame without 3D is drawn exactly as the 2D renderer always drew it.
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

    // Sprite drawing, recorded between begin_frame() and end_frame(). sprites() draws the 2D world,
    // over the 3D scene, with the camera given to set_view_projection() (window pixels without one).
    // screen_sprites() draws the interface on top, always in window pixels, whatever the camera:
    // text and panels that must not move or scale with the world.
    SpriteRenderer& sprites();
    SpriteRenderer& screen_sprites();

    // 3D meshes, recorded between begin_frame() and end_frame(), drawn in the "scene" pass with
    // depth test and write, in linear HDR colors.
    MeshRenderer& meshes();

    // Fraction of the window's pixels the 3D scene is rendered at, in [0.25, 1]; the tone mapping
    // scales it up to the window. Below 1, it trades sharpness for speed (Retina screens, small
    // GPUs). Sprites and the interface always stay at the window's full resolution.
    void set_render_scale(float scale);
    float render_scale() const { return render_scale_; }
    // Multiplies the linear scene before tone mapping: 2 is one stop brighter. Clamped to > 0.
    void set_exposure(float exposure);
    float exposure() const { return exposure_; }
    // Size of the 3D scene image of the current frame, in pixels.
    std::uint32_t scene_width() const { return scene_width_; }
    std::uint32_t scene_height() const { return scene_height_; }
    // Format of that image: 16-bit floats per channel, so lights can exceed 1.0.
    static constexpr SDL_GPUTextureFormat kSceneFormat = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;

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

    // RGBA8 texture, stored as `settings` says. The first form is the one for sprites and the
    // interface: no mipmaps, plain UNORM (no sRGB conversion), alpha premultiplied into the color.
    Texture create_texture(const Image& image, const char* debug_name = nullptr);
    Texture create_texture(const Image& image, const TextureSettings& settings, const char* debug_name = nullptr);

    // A texture whose mipmap levels were computed on the CPU (for example the prefiltered
    // environment): `levels[k]` holds level k, tightly packed rows, in `format`, of size
    // max(1, width >> k) x max(1, height >> k).
    GpuTexture create_texture_levels(SDL_GPUTextureFormat format, int width, int height,
                                     const std::vector<std::vector<std::uint8_t>>& levels, const char* debug_name = nullptr);

    // filter: SDL_GPU_FILTER_NEAREST for pixel art, SDL_GPU_FILTER_LINEAR for smooth scaling.
    // Coordinates outside [0, 1] are clamped to the edge.
    GpuSampler create_sampler(SDL_GPUFilter filter, const char* debug_name = nullptr);

    // Pixel format of the window's swapchain, needed to build a graphics pipeline.
    SDL_GPUTextureFormat swapchain_format() const { return SDL_GetGPUSwapchainTextureFormat(device_, window_); }

    SDL_GPUDevice* device() const { return device_; }

    // Format of the depth texture of the "scene" pass, chosen once among the formats the GPU
    // supports, and its name for logs.
    SDL_GPUTextureFormat depth_format() const { return depth_format_; }
    const char* depth_format_name() const;

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

    // Color and depth of the "scene" pass, at the render resolution, recreated when it changes.
    void ensure_scene_targets();
    SDL_GPUTextureFormat depth_format_ = SDL_GPU_TEXTUREFORMAT_INVALID;
    GpuTexture scene_texture_;
    GpuTexture depth_texture_;
    std::uint32_t scene_width_ = 0;
    std::uint32_t scene_height_ = 0;
    float render_scale_ = 1.0f;
    float exposure_ = 1.0f;

    std::unique_ptr<MeshRenderer> meshes_;
    std::unique_ptr<SpriteRenderer> sprites_;
    std::unique_ptr<SpriteRenderer> screen_sprites_;
    std::unique_ptr<ToneMapper> tone_mapper_;
    std::unique_ptr<DebugUi> debug_ui_;
};

}  // namespace moteur
