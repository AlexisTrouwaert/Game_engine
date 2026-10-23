#pragma once

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "moteur/geometry_pool.hpp"
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

/// How the Renderer is created (read once, by its constructor).
struct RendererConfig {
    bool debug = false;  ///< enables the GPU backend's validation layer
    bool vsync = true;   ///< waits for the display; off: immediate or mailbox presentation when the window supports it
    bool debug_ui = false;          ///< creates a DebugUi (Dear ImGui), drawn on top of the sprites; ignored in shipping builds
    std::string debug_ui_font;      ///< TrueType font of the DebugUi; empty for ImGui's built-in one
    float debug_ui_font_size = 16.0f;  ///< of the DebugUi font, in pixels
};

/// What a shader declares, needed by SDL_GPU to validate bindings.
struct ShaderInfo {
    SDL_GPUShaderStage stage = SDL_GPU_SHADERSTAGE_VERTEX;  ///< vertex or fragment
    std::uint32_t samplers = 0;          ///< texture and sampler pairs it reads
    std::uint32_t uniform_buffers = 0;   ///< uniform buffer slots
    std::uint32_t storage_buffers = 0;   ///< read-only storage buffers
    std::uint32_t storage_textures = 0;  ///< read-only storage textures
};

/// How create_texture() stores an image.
struct TextureSettings {
    /// true: the pixels are sRGB colors (base color textures, photos), and the GPU converts them to
    /// linear values when a shader reads them. false: stored as they are (sprites and interface,
    /// normal maps and other data).
    bool srgb = false;
    /// true: a full chain of mipmaps, computed by the GPU (3D surfaces seen at a distance).
    bool mipmaps = false;
    /// true: the color is multiplied by the alpha first (what the sprite pipeline blends with).
    bool premultiply = true;
    /// true: a tangent-space normal map. Only its x and y are needed (the shader rebuilds z), so a
    /// compressed one (KTX2) becomes two-channel BC5.
    bool normal_map = false;
};

/// The block-compressed formats the GPU can sample (KTX2 textures are transcoded to them).
struct CompressedFormats {
    bool bc7 = false;  ///< colors and data, 1 byte per pixel
    bool bc5 = false;  ///< two channels (normal maps), 1 byte per pixel
};

/// A GPU texture and its size in pixels. Move-only (it owns its GpuTexture); must be destroyed
/// before the Renderer that created it.
struct Texture {
    GpuTexture gpu;             ///< the texture itself
    int width = 0;              ///< of level 0, in pixels
    int height = 0;             ///< of level 0, in pixels
    std::size_t gpu_bytes = 0;  ///< every mipmap level included
};

/// Counters filled while a frame is executed, valid from end_frame() until the next begin_frame().
struct RenderStats {
    int sprites = 0;                 ///< sprites drawn, world and interface together
    int draw_calls = 0;              ///< every pass together (sprites, meshes, shadows)
    /// What the frame itself sent to the GPU, in bytes: sprite and line vertices, instances,
    /// palettes, fog cells (not the uploads of resources created with Renderer::create_buffer() and
    /// the like).
    std::size_t bytes_uploaded = 0;

    /// The "scene" pass: meshes recorded, left after frustum culling (drawn), their triangles and
    /// draw calls (one per batch of instances; also counted in draw_calls).
    ///@{
    int meshes_submitted = 0;
    int meshes = 0;
    std::size_t triangles = 0;
    int mesh_draw_calls = 0;
    ///@}
    int dropped_lights = 0;  ///< point lights beyond MeshRenderer::kMaxPointLights, ignored

    /// The "shadow" pass, the same counters (recorded, left after culling, triangles, draw calls),
    /// among the meshes that cast shadows.
    ///@{
    int shadow_casters_submitted = 0;
    int shadow_casters = 0;
    std::size_t shadow_triangles = 0;
    int shadow_draw_calls = 0;
    ///@}

    /// Point light shadows: lights with a shadow this frame, those whose six faces were drawn again
    /// (the others kept theirs), and for those faces the meshes drawn, their triangles and the draw
    /// calls (six tile clears per light drawn included).
    ///@{
    int point_shadow_lights = 0;
    int point_shadow_updates = 0;
    int point_shadow_casters = 0;
    std::size_t point_shadow_triangles = 0;
    int point_shadow_draw_calls = 0;
    ///@}

    /// Sprites in the 3D world (BillboardRenderer), in the "scene" pass: those drawn and their draw
    /// calls.
    ///@{
    int billboards = 0;
    int billboard_draw_calls = 0;
    ///@}
    int debug_lines = 0;  ///< lines drawn by the DebugLineRenderer
    /// Renderers whose preparation failed this frame (a staging buffer that cannot be mapped or
    /// grown): their drawing is skipped for this frame only, the rest is drawn (see
    /// Renderer::end_frame()).
    int failed_preparations = 0;

    /// GPU time of each part of the frame, in milliseconds, when Renderer::set_gpu_timing() is on
    /// (gpu_timed): approximate (see there). gpu_ms is indexed by GpuTime.
    ///@{
    bool gpu_timed = false;
    float gpu_ms[5] = {};
    ///@}
};

/// The parts of a frame timed by Renderer::set_gpu_timing(), as indices into RenderStats::gpu_ms.
enum GpuTime { kGpuUpload, kGpuShadow, kGpuPointShadows, kGpuScene, kGpuCompose, kGpuTimeCount };

/// A depth texture the Renderer can show over the frame, for debugging.
enum class DebugTexture { None, SunShadowMap, PointShadowAtlas };

/// How the edges of the 3D scene are smoothed (Renderer::set_anti_aliasing()).
/// ```
/// None    stair-stepped edges, no cost
/// Fxaa    a full-screen filter on the tone-mapped image: cheap, works everywhere, a little blurry
/// Msaa2/4 2 or 4 depth samples per pixel along the triangles' edges, resolved before the tone
///         mapping: very clean on geometry, cheap on tiled GPUs (Apple), but nothing for textures
///         or shadows
/// ```
enum class AntiAliasing { None, Fxaa, Msaa2, Msaa4 };
/// "none", "fxaa", "msaa2", "msaa4": the names of the command line.
const char* anti_aliasing_name(AntiAliasing mode);
/// The mode named `name` (as above); false when there is none.
bool parse_anti_aliasing(const std::string& name, AntiAliasing& mode);

/// Owns the GPU device and drives one frame at a time.
///
/// A frame has two phases, because SDL_GPU forbids copying data to the GPU inside a render pass:
///
/// ```
/// begin_frame()   acquire the swapchain image
/// ... the game records what it wants to draw (renderer.sprites().draw(...)) ...
/// end_frame()     upload data (copy passes), then the render passes, then submit
/// ```
///
/// The render passes, in order:
///
/// ```
/// "shadow"    only when meshes were recorded and the sun casts shadows: their depth seen from
///             the sun, into the shadow map (see MeshRenderer)
/// "point shadows"  only when a point light's shadow must be drawn again: its six faces, into
///             the point shadow atlas (the other tiles are kept)
/// "scene"     only when meshes or billboards were recorded: the 3D world (meshes(), then
///             billboards(), sorted and blended, hidden by the meshes in front), in linear HDR
///             colors, into an offscreen float texture with its depth, at the render resolution
///             (multisampled with MSAA, then resolved into a plain texture at the end of the pass)
/// "tonemap"   with FXAA only: the scene tone mapped into a screen-color texture, for FXAA
/// "compose"   the swapchain: the scene tone mapped to screen colors (with FXAA, that texture
///             filtered; the clear color without 3D), then the world sprites (sprites()), the
///             interface in window pixels (screen_sprites()), and ImGui on top
/// ```
///
/// A frame without 3D is drawn exactly as the 2D renderer always drew it.
/// Nothing is sent to the GPU while the game records, so recording is cheap and the engine is free
/// to sort and batch what was recorded.
///
/// The Application creates the one Renderer (it owns the GPU device). Every call is for the main
/// thread, the one that created it. Every GPU resource made from it (GpuResource, Texture, Mesh,
/// Environment...) must be destroyed before it.
class Renderer {
public:
    /// Creates the GPU device on `window` (Direct3D 12 on Windows, Metal on macOS) and the renderers
    /// of every pass (loads their shaders). Throws std::runtime_error if no GPU backend is
    /// available or a shader or pipeline cannot be created.
    /// The window must outlive the renderer.
    Renderer(SDL_Window* window, const RendererConfig& config);
    /// Sends the pending uploads, waits for the GPU to be idle, then releases everything it owns and
    /// the device.
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    /// The color of the screen where nothing is drawn, in screen (sRGB) terms, each in [0, 1]; the
    /// 3D scene's background is the same color converted to linear. Kept.
    void set_clear_color(float r, float g, float b, float a = 1.0f);

    /// Starts a frame: sends the pending uploads (resources created since the last frame), then
    /// acquires the swapchain image (may wait for the display with vsync). Resets stats().
    /// Returns false when nothing can be drawn (for example a minimized window, or a lost device);
    /// in that case record nothing and do not call end_frame().
    bool begin_frame();
    /// The GPU device lost (milestone 8, part 8): a command buffer that cannot be acquired or
    /// submitted, a swapchain image that cannot be obtained. SDL_GPU cannot recover a lost device
    /// (D3D12 "device removed": a driver reset, a removed card): from then on the renderer touches
    /// the GPU no more, begin_frame() returns false, and the Application reports it and ends.
    bool device_lost() const { return !device_lost_.empty(); }
    /// Why the device was lost (the failing SDL call and its error), for the report; empty while
    /// it works.
    const std::string& device_lost_reason() const { return device_lost_; }
    /// For the checks of that chain (console "crash gpu", the slice's --test-plantage gpu).
    void simulate_device_lost() { lose_device("provoked on purpose to check the reports"); }
    /// For the checks of the error policy (console "crash frame", the slice's --test-plantage
    /// frame): the next end_frame() fails to prepare the meshes, as if their staging buffer could not
    /// be mapped. The frame must be drawn without them, and the following ones normally.
    void simulate_frame_failure() { simulate_frame_failure_ = true; }
    /// Ends the frame begun by a begin_frame() that returned true: uploads what was recorded, runs
    /// the render passes (see the class), submits, and forgets what was recorded. Does not wait for
    /// the GPU, except with set_gpu_timing() or a request_capture().
    ///
    /// Never throws (error policy of milestone 9, part 2: no exception crosses a frame). A renderer
    /// whose preparation fails (a staging buffer that cannot be mapped or grown) is logged, counted
    /// (RenderStats::failed_preparations, frame_failures()) and skipped for this frame; a failure
    /// of the device itself loses it (device_lost()).
    void end_frame();
    /// Preparations that failed since the renderer was created (see end_frame()).
    std::uint64_t frame_failures() const { return frame_failures_; }
    /// How many frames begin_frame() started so far (the first one is 1; 0 before any). Lets a
    /// cache tell which of its entries the frame being recorded uses (the glyph atlas).
    std::uint64_t frame_index() const { return frame_index_; }

    /// Reads back the swapchain image of the next end_frame() (the current frame's, if one is being
    /// recorded) and writes it to `path` as a PNG, for
    /// automated pixel comparisons (deterministic scene + fixed tick -> reproducible capture).
    /// One-shot: cleared after end_frame() attempts it, whether or not that attempt succeeds. Stalls
    /// the GPU until the frame is done; never call this every frame, only for tests and tools.
    /// `max_width` > 0: the image is reduced to at most that many pixels across (a save's thumbnail).
    /// A failure is logged, not thrown.
    void request_capture(std::string path, int max_width = 0) {
        capture_path_ = std::move(path);
        capture_max_width_ = max_width;
    }

    /// Submits the uploads of the resources created since the last frame (they wait for the next
    /// frame otherwise): before GPU work of one's own outside a frame that uses them (a thumbnail of
    /// a model just loaded). Not inside begin_frame() and end_frame().
    void submit_pending_uploads() { flush_uploads(); }

    /// Hover by the id buffer (milestone 9, part 7): with MeshRenderer::set_ids() on, the pick id
    /// under `pixel` (window pixels, as Input::pointer() gives them) is read back from this frame's
    /// id pass, without waiting: picked_id() has it one or two frames later. For what the pointer is
    /// over (an outline, a cursor), never for the game's logic (see pick() in picking.hpp).
    void request_pick(glm::vec2 pixel) {
        pick_pixel_ = pixel;
        pick_requested_ = true;
    }
    /// The last pick id read back (0: nothing selectable there), and the window pixel it was asked
    /// for. See request_pick().
    std::uint32_t picked_id() const { return picked_id_; }
    /// The pixel picked_id() was read at.
    glm::vec2 picked_pixel() const { return picked_pixel_; }
    /// Outlines the entity whose pick id is `id` (0: none) with `color` (linear rgb, a: strength),
    /// `thickness` pixels of the scene image wide: what is hovered. Needs the id pass. Kept.
    void set_outline(std::uint32_t id, glm::vec4 color = {1.0f, 0.85f, 0.35f, 1.0f}, float thickness = 2.0f) {
        outline_id_ = id;
        outline_color_ = color;
        outline_thickness_ = thickness;
    }
    /// Format of the id pass's target: one 32-bit whole number per pixel.
    static constexpr SDL_GPUTextureFormat kIdFormat = SDL_GPU_TEXTUREFORMAT_R32_UINT;

    /// Sprite drawing, recorded between begin_frame() and end_frame(). sprites() draws the 2D world,
    /// over the 3D scene, with the camera given to its SpriteRenderer::set_view_projection() (window
    /// pixels without one). screen_sprites() draws the interface on top, always in window pixels,
    /// whatever the camera: text and panels that must not move or scale with the world.
    /// The renderers belong to the Renderer: the references stay valid as long as it lives.
    ///@{
    SpriteRenderer& sprites();
    SpriteRenderer& screen_sprites();
    ///@}

    /// 3D meshes, recorded between begin_frame() and end_frame(), drawn in the "scene" pass with
    /// depth test and write, in linear HDR colors. Owned by the Renderer, valid as long as it lives.
    MeshRenderer& meshes();
    /// Sprites in the 3D world, turned towards the camera, drawn after the meshes in the "scene" pass.
    BillboardRenderer& billboards();
    /// Lines to look at what happens (boxes, frustums, rays), drawn last in the "scene" pass.
    DebugLineRenderer& debug_lines();

    /// Debugging: shows a shadow map in the bottom right corner of the frame, in grays (nothing
    /// while that map does not exist). Kept.
    void set_debug_texture(DebugTexture texture) { debug_texture_ = texture; }
    /// The texture chosen with set_debug_texture(); DebugTexture::None by default.
    DebugTexture debug_texture() const { return debug_texture_; }

    /// Measures the GPU time of each part of the frame (RenderStats::gpu_ms). SDL_GPU has no timer
    /// queries: each part (uploads, shadow, point shadows, scene, compose) is then submitted on its
    /// own and waited for, and the time between the submission and the end of the work is taken.
    /// Approximate (it includes the submission), and it stops the CPU and the GPU from working at
    /// the same time: frame rates drop. For measurements only.
    void set_gpu_timing(bool on) { gpu_timing_ = on; }
    /// Whether GPU timing is on (set_gpu_timing()); off by default.
    bool gpu_timing() const { return gpu_timing_; }

    /// Synchronisation with the display, changed while running (the player's setting). Sends the
    /// pending uploads and waits for the GPU to be idle first: between frames only, not every frame.
    /// Without vsync, the immediate or mailbox mode the window supports, else vsync anyway.
    void set_vsync(bool vsync);
    /// Fraction of the window's pixels the 3D scene is rendered at, clamped to [0.25, 1] (1 by
    /// default); the tone mapping scales it up to the window. Below 1, it trades sharpness for
    /// speed (Retina screens, small GPUs). Sprites and the interface always stay at the window's
    /// full resolution. Takes effect at the next begin_frame(), which recreates the scene's targets.
    void set_render_scale(float scale);
    /// The scale given to set_render_scale(), after clamping.
    float render_scale() const { return render_scale_; }
    /// Anti-aliasing of the 3D scene, None by default. Can change between frames: the next
    /// begin_frame() recreates what the mode needs (textures, the scene pass's pipelines), a short
    /// pause. An MSAA mode the GPU cannot do falls back to the next one it can (see supports()),
    /// with a line in the log.
    void set_anti_aliasing(AntiAliasing mode);
    /// The mode in use: the one given to set_anti_aliasing(), or its fallback.
    AntiAliasing anti_aliasing() const { return anti_aliasing_; }
    /// Whether the GPU can render the scene with `mode` (None and Fxaa always).
    bool supports(AntiAliasing mode) const;
    /// Multiplies the linear scene before tone mapping: 2 is one stop brighter. 1 by default;
    /// clamped to at least 0.001.
    void set_exposure(float exposure);
    /// The exposure given to set_exposure(), after clamping.
    float exposure() const { return exposure_; }
    /// Size of the 3D scene image of the current frame, in pixels: the swapchain's times
    /// render_scale(), rounded, at least 1. Updated by begin_frame(); 0 before the first frame.
    ///@{
    std::uint32_t scene_width() const { return scene_width_; }
    std::uint32_t scene_height() const { return scene_height_; }
    glm::ivec2 scene_size() const { return {static_cast<int>(scene_width_), static_cast<int>(scene_height_)}; }
    ///@}
    /// Format of that image: 16-bit floats per channel, so lights can exceed 1.0.
    static constexpr SDL_GPUTextureFormat kSceneFormat = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    /// The distance of the opaque surfaces along the view, in metres (milestone 7, soft particles):
    /// written by the meshes as a second target, resolved with MSAA, read by the billboards. Far
    /// where no mesh was drawn.
    static constexpr SDL_GPUTextureFormat kDistanceFormat = SDL_GPU_TEXTUREFORMAT_R16_FLOAT;
    static constexpr float kFarDistance = 60000.0f;  ///< what the distance target is cleared to
    /// That distance texture (single-sampled, kDistanceFormat), for the passes drawn after the
    /// opaque meshes. Owned by the Renderer and recreated when the scene's size or anti-aliasing
    /// changes: valid for the current frame only. Null before the first frame.
    SDL_GPUTexture* scene_distance_texture() const { return distance_texture_.get(); }

#if defined(MOTEUR_SHIPPING)
    /// The debug interface: always null in shipping builds (MOTEUR_SHIPPING), which have none.
    DebugUi* debug_ui() { return nullptr; }
#else
    /// The debug interface, or null when RendererConfig::debug_ui is off; owned by the Renderer.
    /// Its windows are drawn after the sprites, in window pixels.
    DebugUi* debug_ui() { return debug_ui_.get(); }
#endif

    /// Statistics of the frame that just ended.
    const RenderStats& stats() const { return stats_; }

    /// Loads the shader `name` from the `shaders` folder next to the executable (base_path(); from
    /// the mounted archives first, then the disk): the file named `name` followed by the extension
    /// of the active backend (`.dxil`, `.msl`, `.spv`), for example `shaders/mesh.vert.dxil`. Throws
    /// std::runtime_error, listing the paths tried, if the file is missing, or if the shader is
    /// rejected. The shader is named `name` for graphics debuggers (RenderDoc, Xcode). Loading
    /// time: reads a file.
    GpuShader load_shader(const std::string& name, const ShaderInfo& info);

    // Resource creation (milestone 8, part 5). The data given is copied into the renderer's staging
    // pages and recorded with the other pending uploads, which are submitted before the next
    // frame's command buffer (begin_frame(), end_frame()); the call neither submits nor waits, so
    // it is cheap while loading and allowed while a frame is being recorded (the data reaches the
    // GPU before that frame's passes). Unless stated otherwise, these throw std::runtime_error on
    // failure.
    //
    // `debug_name`, when given, labels the resource for graphics debuggers (RenderDoc, Xcode):
    // captured frames show "sprite.vertices" instead of an anonymous buffer. It costs nothing at
    // runtime and is not required; leave it null for a resource not worth naming individually
    // (for example one of many identical glyph textures).

    /// A GPU buffer of `size` bytes holding a copy of `data`, uploaded with the pending uploads.
    /// usage: SDL_GPU_BUFFERUSAGE_VERTEX or SDL_GPU_BUFFERUSAGE_INDEX (or any other SDL_GPU usage).
    GpuBuffer create_buffer(SDL_GPUBufferUsageFlags usage, const void* data, std::size_t size,
                            const char* debug_name = nullptr);

    /// Mesh data (vertices, indices, skin) in a range of the renderer's geometry pools (see
    /// GeometryPool): far cheaper than a buffer of its own. Uploaded with the other pending uploads.
    /// usage: SDL_GPU_BUFFERUSAGE_VERTEX or SDL_GPU_BUFFERUSAGE_INDEX; anything else throws
    /// std::invalid_argument, as does a `size` of 0. The slice must be destroyed before the
    /// Renderer. Mesh::create() is the usual way to call it.
    GpuSlice create_geometry(SDL_GPUBufferUsageFlags usage, const void* data, std::size_t size);
    /// What the geometry pools hold, vertices and indices together (DEBUG, reports).
    GeometryPool::Stats geometry_stats() const;

    /// A buffer of `size` bytes whose content is undefined until an upload writes it. Unlike the
    /// call above it does not copy or upload anything, so it is cheap enough for a growing buffer.
    GpuBuffer create_buffer(SDL_GPUBufferUsageFlags usage, std::size_t size, const char* debug_name = nullptr);

    /// Staging buffer of `size` bytes that the CPU writes (mapped) and a copy pass then sends to the
    /// GPU (upload usage). Not counted in gpu_memory().
    GpuTransferBuffer create_transfer_buffer(std::size_t size);

    /// RGBA8 texture, stored as `settings` says. The first form is the one for sprites and the
    /// interface: no mipmaps, plain UNORM (no sRGB conversion), alpha premultiplied into the color.
    /// The image is copied (and premultiplied in the copy); mipmaps are generated by the GPU with
    /// the pending uploads.
    ///@{
    Texture create_texture(const Image& image, const char* debug_name = nullptr);
    Texture create_texture(const Image& image, const TextureSettings& settings, const char* debug_name = nullptr);
    ///@}
    /// Rewrites the rectangle (x, y, width, height) of level 0 of an RGBA8 texture made by
    /// create_texture() with `rgba` (width * height * 4 bytes, rows top to bottom), with the pending
    /// uploads: the frame being recorded already sees it. The data is copied. For a texture that
    /// changes a little at a time (the glyph atlas); a part the GPU is still reading for an earlier
    /// frame is written only after that frame (no cycling: the rest of the texture is kept). Throws
    /// std::invalid_argument if the rectangle does not fit in the texture.
    void update_texture(const Texture& texture, int x, int y, int width, int height, const void* rgba);
    /// What the GPU can sample, unless block compression was turned off (then nothing: KTX2 textures
    /// become RGBA8, to compare the two).
    CompressedFormats compressed_formats() const {
        return block_compression_ ? compressed_formats_ : CompressedFormats{};
    }
    /// Turns block compression on (the default) or off, for the KTX2 textures loaded from then on.
    void set_block_compression(bool enabled) { block_compression_ = enabled; }

    /// A texture whose mipmap levels were computed on the CPU (for example the prefiltered
    /// environment): `levels[k]` holds level k, tightly packed rows, in `format`, of size
    /// max(1, width >> k) x max(1, height >> k). Sampled only (never drawn into). Throws
    /// std::invalid_argument when `levels` is empty.
    GpuTexture create_texture_levels(SDL_GPUTextureFormat format, int width, int height,
                                     const std::vector<std::vector<std::uint8_t>>& levels, const char* debug_name = nullptr);

    /// filter: SDL_GPU_FILTER_NEAREST for pixel art, SDL_GPU_FILTER_LINEAR for smooth scaling.
    /// Coordinates outside [0, 1] are clamped to the edge. Mipmaps are read with the nearest
    /// level (no blending between levels, no anisotropy).
    GpuSampler create_sampler(SDL_GPUFilter filter, const char* debug_name = nullptr);

    /// Pixel format of the window's swapchain, needed to build a graphics pipeline.
    SDL_GPUTextureFormat swapchain_format() const { return SDL_GetGPUSwapchainTextureFormat(device_, window_); }

    /// The SDL_GPU device, owned by the Renderer: valid as long as it lives. For code that creates
    /// its own GPU objects (wrap them in a GpuResource).
    SDL_GPUDevice* device() const { return device_; }

    /// Format of the depth texture of the "scene" pass, chosen once among the formats the GPU
    /// supports (D32_FLOAT, else D24_UNORM, else D16_UNORM).
    SDL_GPUTextureFormat depth_format() const { return depth_format_; }
    /// The name of depth_format() for logs: "D32_FLOAT", "D24_UNORM" or "D16_UNORM".
    const char* depth_format_name() const;

    /// "direct3d12, NVIDIA GeForce RTX 4070 Ti SUPER": the GPU backend and device, for reports.
    const std::string& device_description() const { return device_description_; }
    /// Size of the swapchain image of the current frame, in pixels (the window's size in pixels);
    /// updated by begin_frame(), 0 before the first frame.
    ///@{
    std::uint32_t width() const { return width_; }
    std::uint32_t height() const { return height_; }
    ///@}

private:
    SDL_Window* window_ = nullptr;
    SDL_GPUDevice* device_ = nullptr;
    SDL_FColor clear_color_ = {0.1f, 0.1f, 0.15f, 1.0f};

    SDL_GPUCommandBuffer* command_buffer_ = nullptr;

    /// The uploads of the resources created since the last submission (milestone 8, part 5): one
    /// command buffer and a few large transfer pages for all of them, submitted before the next
    /// frame's (or any other) command buffer. Creating a resource neither submits nor waits.
    struct Uploads {
        SDL_GPUCommandBuffer* commands = nullptr;
        SDL_GPUCopyPass* pass = nullptr;
        std::vector<SDL_GPUTransferBuffer*> pages;  ///< used by the pending uploads
        std::size_t page_size = 0;
        std::size_t page_used = 0;
        /// Pages submitted with a fence: reused once the GPU passed it (a staging ring), so that
        /// loading again and again does not allocate (and the driver does not keep) new pages.
        struct InFlight {
            SDL_GPUFence* fence = nullptr;               ///< signalled when the GPU is done with them
            std::vector<SDL_GPUTransferBuffer*> pages;  ///< the standard pages of that submission
        };
        std::vector<InFlight> in_flight;
        std::vector<SDL_GPUTransferBuffer*> spare;  ///< standard pages ready for reuse
        std::vector<bool> large;  ///< per page of `pages`: larger than the standard size (not reused)
    } uploads_;
    void release_upload_pages();
    /// The copy pass of the pending uploads (begun if needed).
    SDL_GPUCopyPass* upload_pass();
    /// The pending uploads' command buffer outside any pass (mipmap generation).
    SDL_GPUCommandBuffer* upload_commands();
    /// `data` copied into a transfer page; where it is.
    SDL_GPUTransferBufferLocation stage_upload(const void* data, std::size_t size);
    void flush_uploads();
    SDL_GPUTexture* swapchain_texture_ = nullptr;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    RenderStats stats_;
    std::string capture_path_;
    int capture_max_width_ = 0;

    /// Color and depth of the "scene" pass, at the render resolution, recreated when it or the
    /// anti-aliasing changes (the scene pass's pipelines too, for the number of samples).
    void ensure_scene_targets();
    /// In the "compose" pass: the texture chosen by set_debug_texture(), if any.
    void render_debug_texture(SDL_GPURenderPass* pass);
    SDL_GPUTextureFormat depth_format_ = SDL_GPU_TEXTUREFORMAT_INVALID;
    GpuTexture scene_texture_;       ///< what the tone mapping reads (the resolved image with MSAA)
    GpuTexture scene_msaa_texture_;  ///< with MSAA only: the multisampled image the scene pass draws
    GpuTexture depth_texture_;       ///< multisampled with MSAA
    GpuTexture distance_texture_;    ///< the meshes' distances along the view (resolved with MSAA)
    GpuTexture distance_msaa_texture_;  ///< with MSAA only: what the meshes write, resolved into the one above
    GpuTexture ldr_texture_;         ///< with FXAA only: the tone-mapped scene, which FXAA reads
    GpuTexture id_texture_;          ///< the id pass's target (MeshRenderer::set_ids()), single-sampled
    GpuTexture id_depth_;            ///< its depth
    std::uint32_t id_width_ = 0;
    std::uint32_t id_height_ = 0;
    glm::vec2 pick_pixel_{0.0f};
    bool pick_requested_ = false;
    std::uint32_t picked_id_ = 0;
    glm::vec2 picked_pixel_{0.0f};
    std::uint32_t outline_id_ = 0;
    glm::vec4 outline_color_{1.0f};
    float outline_thickness_ = 2.0f;
    /// The pick read-backs in flight: a pixel's id copied into a buffer, read once its frame is done.
    struct PickSlot {
        GpuTransferBuffer buffer;
        SDL_GPUFence* fence = nullptr;
        glm::vec2 pixel{0.0f};
    };
    PickSlot pick_slots_[3];
    int next_pick_slot_ = 0;
    /// Reads the read-backs whose frame is done (picked_id()); waits for none.
    void collect_picks();
    /// The id targets at the scene's size.
    void ensure_id_targets();
    std::uint32_t scene_width_ = 0;
    std::uint32_t scene_height_ = 0;
    AntiAliasing anti_aliasing_ = AntiAliasing::None;
    AntiAliasing targets_anti_aliasing_ = AntiAliasing::None;  ///< what the textures were made for
    SDL_GPUSampleCount scene_samples_ = SDL_GPU_SAMPLECOUNT_1;  ///< what the pipelines were made for
    float render_scale_ = 1.0f;
    float exposure_ = 1.0f;

    std::unique_ptr<GeometryPool> vertex_pool_;  ///< created at the first mesh, released after every renderer
    std::unique_ptr<GeometryPool> index_pool_;
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
#if !defined(MOTEUR_SHIPPING)
    std::unique_ptr<DebugUi> debug_ui_;
#endif
    std::string device_description_;
    std::string device_lost_;  ///< why the device was lost; empty while it works
    void lose_device(const std::string& reason);
    bool simulate_frame_failure_ = false;
    std::uint64_t frame_failures_ = 0;
    std::uint64_t frame_index_ = 0;
    /// Runs one renderer's preparation; on an exception, logs it, counts it and returns false (the
    /// renderer is then skipped for the frame).
    template <typename Prepare>
    bool prepare_guarded(const char* what, Prepare&& prepare);
};

}  // namespace moteur
