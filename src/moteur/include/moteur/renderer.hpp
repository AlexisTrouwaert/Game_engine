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

class BillboardRenderer;
class DebugLineRenderer;
class DebugUi;
class DepthView;
class Fxaa;
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
    // true: a tangent-space normal map. Only its x and y are needed (the shader rebuilds z), so a
    // compressed one (KTX2) becomes two-channel BC5.
    bool normal_map = false;
};

struct CompressedImage;  // see ktx_texture.hpp

// The block-compressed formats the GPU can sample (KTX2 textures are transcoded to them).
struct CompressedFormats {
    bool bc7 = false;  // colors and data, 1 byte per pixel
    bool bc5 = false;  // two channels (normal maps), 1 byte per pixel
};

// A GPU texture and its size in pixels.
struct Texture {
    GpuTexture gpu;
    int width = 0;
    int height = 0;
    std::size_t gpu_bytes = 0;  // every mipmap level included
};

// Counters filled while a frame is executed, valid from end_frame() until the next begin_frame().
struct RenderStats {
    int sprites = 0;
    int draw_calls = 0;              // every pass together (sprites, meshes, shadows)
    std::size_t bytes_uploaded = 0;  // vertex and instance data sent to the GPU this frame

    // The "scene" pass: meshes recorded, left after frustum culling (drawn), their triangles and
    // draw calls (one per batch of instances; also counted in draw_calls).
    int meshes_submitted = 0;
    int meshes = 0;
    std::size_t triangles = 0;
    int mesh_draw_calls = 0;
    int dropped_lights = 0;  // point lights beyond MeshRenderer::kMaxPointLights, ignored

    // The "shadow" pass, the same counters, among the meshes that cast shadows.
    int shadow_casters_submitted = 0;
    int shadow_casters = 0;
    std::size_t shadow_triangles = 0;
    int shadow_draw_calls = 0;

    // Point light shadows: lights with a shadow this frame, those whose six faces were drawn again
    // (the others kept theirs), and for those faces the meshes drawn, their triangles and the draw
    // calls (six tile clears per light drawn included).
    int point_shadow_lights = 0;
    int point_shadow_updates = 0;
    int point_shadow_casters = 0;
    std::size_t point_shadow_triangles = 0;
    int point_shadow_draw_calls = 0;

    // Sprites in the 3D world (BillboardRenderer), in the "scene" pass.
    int billboards = 0;
    int billboard_draw_calls = 0;
    int debug_lines = 0;  // DebugLineRenderer

    // GPU time of each part of the frame, in milliseconds, when Renderer::set_gpu_timing() is on
    // (gpu_timed): approximate (see there). In the order of GpuTime.
    bool gpu_timed = false;
    float gpu_ms[5] = {};
};

// The parts of a frame timed by Renderer::set_gpu_timing(), as indices into RenderStats::gpu_ms.
enum GpuTime { kGpuUpload, kGpuShadow, kGpuPointShadows, kGpuScene, kGpuCompose, kGpuTimeCount };

// A depth texture the Renderer can show over the frame, for debugging.
enum class DebugTexture { None, SunShadowMap, PointShadowAtlas };

// How the edges of the 3D scene are smoothed (Renderer::set_anti_aliasing()).
//   None    stair-stepped edges, no cost
//   Fxaa    a full-screen filter on the tone-mapped image: cheap, works everywhere, a little blurry
//   Msaa2/4 2 or 4 depth samples per pixel along the triangles' edges, resolved before the tone
//           mapping: very clean on geometry, cheap on tiled GPUs (Apple), but nothing for textures
//           or shadows
enum class AntiAliasing { None, Fxaa, Msaa2, Msaa4 };
// "none", "fxaa", "msaa2", "msaa4": the names of the command line.
const char* anti_aliasing_name(AntiAliasing mode);
// The mode named `name` (as above); false when there is none.
bool parse_anti_aliasing(const std::string& name, AntiAliasing& mode);

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
//   "shadow"    only when meshes were recorded and the sun casts shadows: their depth seen from
//               the sun, into the shadow map (see MeshRenderer)
//   "point shadows"  only when a point light's shadow must be drawn again: its six faces, into
//               the point shadow atlas (the other tiles are kept)
//   "scene"     only when meshes or billboards were recorded: the 3D world (meshes(), then
//               billboards(), sorted and blended, hidden by the meshes in front), in linear HDR
//               colors, into an offscreen float texture with its depth, at the render resolution
//               (multisampled with MSAA, then resolved into a plain texture at the end of the pass)
//   "tonemap"   with FXAA only: the scene tone mapped into a screen-color texture, for FXAA
//   "compose"   the swapchain: the scene tone mapped to screen colors (with FXAA, that texture
//               filtered; the clear color without 3D), then the world sprites (sprites()), the
//               interface in window pixels (screen_sprites()), and ImGui on top
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
    // Sprites in the 3D world, turned towards the camera, drawn after the meshes in the "scene" pass.
    BillboardRenderer& billboards();
    // Lines to look at what happens (boxes, frustums, rays), drawn last in the "scene" pass.
    DebugLineRenderer& debug_lines();

    // Debugging: shows a shadow map in the bottom right corner of the frame, in grays.
    void set_debug_texture(DebugTexture texture) { debug_texture_ = texture; }
    DebugTexture debug_texture() const { return debug_texture_; }

    // Measures the GPU time of each part of the frame (RenderStats::gpu_ms). SDL_GPU has no timer
    // queries: each part (uploads, shadow, point shadows, scene, compose) is then submitted on its
    // own and waited for, and the time between the submission and the end of the work is taken.
    // Approximate (it includes the submission), and it stops the CPU and the GPU from working at
    // the same time: frame rates drop. For measurements only.
    void set_gpu_timing(bool on) { gpu_timing_ = on; }
    bool gpu_timing() const { return gpu_timing_; }

    // Fraction of the window's pixels the 3D scene is rendered at, in [0.25, 1]; the tone mapping
    // scales it up to the window. Below 1, it trades sharpness for speed (Retina screens, small
    // GPUs). Sprites and the interface always stay at the window's full resolution.
    void set_render_scale(float scale);
    float render_scale() const { return render_scale_; }
    // Anti-aliasing of the 3D scene, None by default. Can change between frames: the next
    // begin_frame() recreates what the mode needs (textures, the scene pass's pipelines), a short
    // pause. An MSAA mode the GPU cannot do falls back to the next one it can (see supports()).
    void set_anti_aliasing(AntiAliasing mode);
    AntiAliasing anti_aliasing() const { return anti_aliasing_; }
    // Whether the GPU can render the scene with `mode` (None and Fxaa always).
    bool supports(AntiAliasing mode) const;
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
    // A texture whose levels (mipmaps) are all given, in its GPU format (block-compressed or not).
    Texture create_texture(const CompressedImage& image, const char* debug_name = nullptr);
    // What the GPU can sample, unless block compression was turned off (then nothing: KTX2 textures
    // become RGBA8, to compare the two).
    CompressedFormats compressed_formats() const {
        return block_compression_ ? compressed_formats_ : CompressedFormats{};
    }
    void set_block_compression(bool enabled) { block_compression_ = enabled; }

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

    // Color and depth of the "scene" pass, at the render resolution, recreated when it or the
    // anti-aliasing changes (the scene pass's pipelines too, for the number of samples).
    void ensure_scene_targets();
    // In the "compose" pass: the texture chosen by set_debug_texture(), if any.
    void render_debug_texture(SDL_GPURenderPass* pass);
    SDL_GPUTextureFormat depth_format_ = SDL_GPU_TEXTUREFORMAT_INVALID;
    GpuTexture scene_texture_;       // what the tone mapping reads (the resolved image with MSAA)
    GpuTexture scene_msaa_texture_;  // with MSAA only: the multisampled image the scene pass draws
    GpuTexture depth_texture_;       // multisampled with MSAA
    GpuTexture ldr_texture_;         // with FXAA only: the tone-mapped scene, which FXAA reads
    std::uint32_t scene_width_ = 0;
    std::uint32_t scene_height_ = 0;
    AntiAliasing anti_aliasing_ = AntiAliasing::None;
    AntiAliasing targets_anti_aliasing_ = AntiAliasing::None;  // what the textures were made for
    SDL_GPUSampleCount scene_samples_ = SDL_GPU_SAMPLECOUNT_1;  // what the pipelines were made for
    float render_scale_ = 1.0f;
    float exposure_ = 1.0f;

    std::unique_ptr<MeshRenderer> meshes_;
    std::unique_ptr<BillboardRenderer> billboards_;
    std::unique_ptr<DebugLineRenderer> debug_lines_;
    std::unique_ptr<DepthView> depth_view_;
    DebugTexture debug_texture_ = DebugTexture::None;
    CompressedFormats compressed_formats_;
    bool block_compression_ = true;
    bool gpu_timing_ = false;
    std::unique_ptr<SpriteRenderer> sprites_;
    std::unique_ptr<SpriteRenderer> screen_sprites_;
    std::unique_ptr<ToneMapper> tone_mapper_;
    std::unique_ptr<Fxaa> fxaa_;
    std::unique_ptr<DebugUi> debug_ui_;
};

}  // namespace moteur
