#include "moteur/thumbnails.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <system_error>

#include "moteur/camera3d.hpp"
#include "moteur/file_io.hpp"
#include "moteur/mesh_renderer.hpp"
#include "moteur/world.hpp"

namespace moteur {

namespace {

// The sink of the thumbnails: what the world holds, to the thumbnails' own mesh renderer.
class ThumbnailSink final : public WorldSink {
public:
    explicit ThumbnailSink(MeshRenderer& meshes) : meshes_(meshes) {}
    void mesh(const Mesh& mesh, const glm::mat4& world, const Material& material, const Aabb& bounds) override {
        meshes_.draw(mesh, world, material, bounds);
    }
    MeshRenderer::Palette palette(const std::vector<glm::mat4>& matrices) override { return meshes_.add_palette(matrices); }
    void skinned_mesh(const Mesh& mesh, const glm::mat4& world, const Material& material, const Aabb& bounds,
                      MeshRenderer::Palette palette) override {
        meshes_.draw(mesh, world, material, bounds, palette);
    }
    void light(const PointLight& light) override { meshes_.add_light(light); }
    void billboard(const Texture&, glm::vec3, glm::vec2, const BillboardOptions&) override {}  // glows: not in thumbnails

private:
    MeshRenderer& meshes_;
};

float half_to_float(std::uint16_t half) {
    const std::uint32_t sign = static_cast<std::uint32_t>(half & 0x8000u) << 16;
    const std::uint32_t exponent = (half >> 10) & 0x1fu;
    const std::uint32_t mantissa = half & 0x3ffu;
    std::uint32_t bits;
    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;
        } else {  // subnormal: normalised for a float
            int shift = 0;
            std::uint32_t m = mantissa;
            while ((m & 0x400u) == 0) {
                m <<= 1;
                ++shift;
            }
            bits = sign | static_cast<std::uint32_t>(127 - 15 - shift + 1) << 23 | (m & 0x3ffu) << 13;
        }
    } else if (exponent == 31) {
        bits = sign | 0x7f800000u | mantissa << 13;
    } else {
        bits = sign | (exponent + 127 - 15) << 23 | mantissa << 13;
    }
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

// The game's tone mapping (tonemap.frag.hlsl): Khronos PBR Neutral, then sRGB.
glm::vec3 pbr_neutral(glm::vec3 color) {
    constexpr float kStartCompression = 0.8f - 0.04f;
    constexpr float kDesaturation = 0.15f;
    const float x = std::min({color.r, color.g, color.b});
    const float offset = x < 0.08f ? x - 6.25f * x * x : 0.04f;
    color -= offset;
    const float peak = std::max({color.r, color.g, color.b});
    if (peak < kStartCompression) {
        return color;
    }
    constexpr float d = 1.0f - kStartCompression;
    const float new_peak = 1.0f - d * d / (peak + d - kStartCompression);
    color *= new_peak / peak;
    const float g = 1.0f - 1.0f / (kDesaturation * (peak - new_peak) + 1.0f);
    return color + (glm::vec3(new_peak) - color) * g;
}

float to_srgb(float c) {
    c = std::clamp(c, 0.0f, 1.0f);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

GpuTexture make_target(SDL_GPUDevice* device, SDL_GPUTextureFormat format, SDL_GPUTextureUsageFlags usage, int size,
                       const char* name) {
    SDL_GPUTextureCreateInfo info = {};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = format;
    info.usage = usage;
    info.width = static_cast<Uint32>(size);
    info.height = static_cast<Uint32>(size);
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    const NameProperty name_property(SDL_PROP_GPU_TEXTURE_CREATE_NAME_STRING, name);
    info.props = name_property.id();
    SDL_GPUTexture* raw = SDL_CreateGPUTexture(device, &info);
    if (raw == nullptr) {
        throw std::runtime_error(std::string("SDL_CreateGPUTexture (") + name + ") failed: " + SDL_GetError());
    }
    return GpuTexture(device, raw, texture_memory_bytes(info), texture_memory_kind(info));
}

std::uint64_t fnv1a(const std::string& text) {
    std::uint64_t hash = 14695981039346656037ull;
    for (const char c : text) {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string hex(std::uint64_t value) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string text(16, '0');
    for (int i = 15; i >= 0; --i) {
        text[static_cast<std::size_t>(i)] = kDigits[value & 0xfu];
        value >>= 4;
    }
    return text;
}

// Shown as they are by the interface (straight alpha, sRGB bytes as the screen's).
const TextureSettings kSettings{.srgb = false, .mipmaps = false, .premultiply = false};

std::filesystem::path path_of(const std::string& text) {
    return {std::u8string(text.begin(), text.end())};
}

}  // namespace

ThumbnailRenderer::ThumbnailRenderer(Renderer& renderer, int size) : renderer_(renderer), size_(std::clamp(size, 16, 512)) {
    SDL_GPUDevice* device = renderer.device();
    const int drawn = size_ * 2;
    meshes_ = std::make_unique<MeshRenderer>(renderer, Renderer::kSceneFormat, renderer.depth_format());
    ShadowOptions shadows;
    shadows.enabled = false;
    meshes_->set_shadows(shadows);
    meshes_->set_shadow_caps(512, 0, 64);  // no point shadows either: a thumbnail has no room for them
    color_ = make_target(device, Renderer::kSceneFormat, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, drawn, "thumbnail color");
    distance_ = make_target(device, Renderer::kDistanceFormat, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, drawn, "thumbnail distance");
    depth_ = make_target(device, renderer.depth_format(), SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET, drawn, "thumbnail depth");
    SDL_GPUTransferBufferCreateInfo info = {};
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    info.size = static_cast<Uint32>(drawn * drawn * 8);  // RGBA16F
    SDL_GPUTransferBuffer* raw = SDL_CreateGPUTransferBuffer(device, &info);
    if (raw == nullptr) {
        throw std::runtime_error(std::string("SDL_CreateGPUTransferBuffer (thumbnail) failed: ") + SDL_GetError());
    }
    download_ = GpuTransferBuffer(device, raw);
}

ThumbnailRenderer::~ThumbnailRenderer() = default;

Image ThumbnailRenderer::render(World& world, const Aabb& bounds, const Environment* sky) {
    SDL_GPUDevice* device = renderer_.device();
    const int drawn = size_ * 2;

    // Three quarters from above, the whole box in view.
    Camera3D camera;
    camera.set_viewport({static_cast<float>(drawn), static_cast<float>(drawn)});
    camera.set_field_of_view(30.0f);
    camera.set_angles(35.0f, 28.0f);
    const glm::vec3 centre = bounds.empty() ? glm::vec3(0.0f) : bounds.center();
    const glm::vec3 extent = bounds.empty() ? glm::vec3(1.0f) : bounds.size();
    camera.set_target(centre);
    camera.set_visible_height(std::max({extent.x, extent.y, extent.z, 0.1f}) * 1.25f);

    meshes_->clear();
    meshes_->set_camera(camera.view_projection(), camera.position());
    meshes_->set_sun(glm::normalize(glm::vec3(0.45f, 0.8f, 0.35f)), glm::vec3(1.0f, 0.96f, 0.9f), 2.6f);
    meshes_->set_environment(sky, 1.0f);
    meshes_->set_lod_threshold(0.0f);  // the finest level
    ThumbnailSink sink(*meshes_);
    CollectOptions options;
    options.view = camera.frustum();
    options.light_focus = centre;
    world.collect(sink, 1.0f, options);

    renderer_.submit_pending_uploads();  // the geometry of a model just loaded, before it is drawn
    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(device);
    if (commands == nullptr) {
        throw std::runtime_error(std::string("SDL_AcquireGPUCommandBuffer (thumbnail) failed: ") + SDL_GetError());
    }
    RenderStats stats;
    SDL_PushGPUDebugGroup(commands, "thumbnail");
    if (meshes_->has_work()) {
        meshes_->prepare(commands, stats);
    }
    SDL_GPUColorTargetInfo targets[2] = {};
    targets[0].texture = color_.get();
    targets[0].clear_color = {0.0f, 0.0f, 0.0f, 0.0f};  // transparent behind the asset
    targets[0].load_op = SDL_GPU_LOADOP_CLEAR;
    targets[0].store_op = SDL_GPU_STOREOP_STORE;
    targets[1].texture = distance_.get();
    targets[1].clear_color = {1000.0f, 0.0f, 0.0f, 0.0f};
    targets[1].load_op = SDL_GPU_LOADOP_CLEAR;
    targets[1].store_op = SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPUDepthStencilTargetInfo depth = {};
    depth.texture = depth_.get();
    depth.clear_depth = 1.0f;
    depth.load_op = SDL_GPU_LOADOP_CLEAR;
    depth.store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    if (SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, targets, 2, &depth)) {
        if (meshes_->has_work()) {
            meshes_->render(commands, pass, stats);
        }
        SDL_EndGPURenderPass(pass);
    }
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
    SDL_GPUTextureRegion region = {};
    region.texture = color_.get();
    region.w = static_cast<Uint32>(drawn);
    region.h = static_cast<Uint32>(drawn);
    region.d = 1;
    SDL_GPUTextureTransferInfo destination = {};
    destination.transfer_buffer = download_.get();
    destination.pixels_per_row = static_cast<Uint32>(drawn);
    destination.rows_per_layer = static_cast<Uint32>(drawn);
    SDL_DownloadFromGPUTexture(copy, &region, &destination);
    SDL_EndGPUCopyPass(copy);
    SDL_PopGPUDebugGroup(commands);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
    meshes_->clear();
    if (fence == nullptr) {
        throw std::runtime_error(std::string("SDL_SubmitGPUCommandBufferAndAcquireFence (thumbnail) failed: ") + SDL_GetError());
    }
    SDL_WaitForGPUFences(device, true, &fence, 1);
    SDL_ReleaseGPUFence(device, fence);

    // Tone mapped, then each pixel the average of a block of four (alpha-weighted colours).
    const auto* mapped = static_cast<const std::uint16_t*>(SDL_MapGPUTransferBuffer(device, download_.get(), false));
    if (mapped == nullptr) {
        throw std::runtime_error(std::string("SDL_MapGPUTransferBuffer (thumbnail) failed: ") + SDL_GetError());
    }
    Image image;
    image.width = size_;
    image.height = size_;
    image.pixels.resize(static_cast<std::size_t>(size_) * static_cast<std::size_t>(size_) * 4);
    for (int y = 0; y < size_; ++y) {
        for (int x = 0; x < size_; ++x) {
            glm::vec3 sum(0.0f);
            float alpha = 0.0f;
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    const std::size_t at = (static_cast<std::size_t>(y * 2 + dy) * static_cast<std::size_t>(drawn) +
                                            static_cast<std::size_t>(x * 2 + dx)) * 4;
                    const float a = std::clamp(half_to_float(mapped[at + 3]), 0.0f, 1.0f);
                    const glm::vec3 hdr(half_to_float(mapped[at]), half_to_float(mapped[at + 1]), half_to_float(mapped[at + 2]));
                    sum += pbr_neutral(glm::max(hdr, glm::vec3(0.0f))) * a;
                    alpha += a;
                }
            }
            std::uint8_t* out = &image.pixels[(static_cast<std::size_t>(y) * static_cast<std::size_t>(size_) + static_cast<std::size_t>(x)) * 4];
            const glm::vec3 colour = alpha > 0.0f ? sum / alpha : glm::vec3(0.0f);
            out[0] = static_cast<std::uint8_t>(std::lround(to_srgb(colour.r) * 255.0f));
            out[1] = static_cast<std::uint8_t>(std::lround(to_srgb(colour.g) * 255.0f));
            out[2] = static_cast<std::uint8_t>(std::lround(to_srgb(colour.b) * 255.0f));
            out[3] = static_cast<std::uint8_t>(std::lround(alpha / 4.0f * 255.0f));
        }
    }
    SDL_UnmapGPUTransferBuffer(device, download_.get());
    return image;
}

ThumbnailCache::ThumbnailCache(Renderer& renderer, std::string directory) : renderer_(renderer), directory_(std::move(directory)) {
    if (!directory_.empty() && directory_.back() != '/') {
        directory_ += '/';
    }
}

std::string ThumbnailCache::file_name(const std::string& key, std::uint64_t fingerprint) const {
    return directory_ + hex(fnv1a(key)) + "_" + hex(fingerprint) + ".png";
}

const Texture* ThumbnailCache::find(const std::string& key, std::uint64_t fingerprint) {
    if (const auto it = textures_.find(key); it != textures_.end() && it->second.fingerprint == fingerprint) {
        return &it->second.texture;
    }
    const std::string file = file_name(key, fingerprint);
    std::error_code error;
    if (!std::filesystem::is_regular_file(path_of(file), error)) {
        return nullptr;
    }
    try {
        const Image image = load_image(file);
        Entry& entry = textures_[key];
        entry.fingerprint = fingerprint;
        entry.texture = renderer_.create_texture(image, kSettings, "thumbnail");
        return &entry.texture;
    } catch (const std::exception& e) {
        SDL_Log("Thumbnail %s: %s (drawn again)", key.c_str(), e.what());
        return nullptr;
    }
}

const Texture* ThumbnailCache::store(const std::string& key, std::uint64_t fingerprint, const Image& image) {
    std::error_code error;
    std::filesystem::create_directories(path_of(directory_), error);
    // The older thumbnails of the same key go.
    const std::string prefix = hex(fnv1a(key)) + "_";
    std::vector<std::filesystem::path> older;
    for (const auto& entry : std::filesystem::directory_iterator(path_of(directory_), error)) {
        const std::u8string name = entry.path().filename().u8string();
        if (name.size() > prefix.size() && std::equal(prefix.begin(), prefix.end(), name.begin(),
                                                      [](char a, char8_t b) { return a == static_cast<char>(b); })) {
            older.push_back(entry.path());
        }
    }
    for (const std::filesystem::path& file : older) {
        std::filesystem::remove(file, error);
    }
    const std::vector<std::uint8_t> png = encode_png(image);
    std::string problem;
    if (!write_file_atomic(file_name(key, fingerprint), png.data(), png.size(), problem)) {
        SDL_Log("Thumbnail %s: %s", key.c_str(), problem.c_str());
    }
    Entry& entry = textures_[key];
    entry.fingerprint = fingerprint;
    entry.texture = renderer_.create_texture(image, kSettings, "thumbnail");
    return &entry.texture;
}

}  // namespace moteur
