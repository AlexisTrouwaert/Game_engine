#pragma once

#include <SDL3/SDL.h>

#include <atomic>
#include <cstddef>
#include <utility>

namespace moteur {

/// GPU objects of type T alive now, created through a GpuResource (leak checks: see
/// gpu_resource_counts()). Atomic: readable from any thread.
template <typename T>
inline std::atomic<long> gpu_live_count{0};

/// What the engine allocates on the GPU, by kind (milestone 8, part 4): the sizes it asked for,
/// computed from the formats (the driver adds its own alignment and padding, which SDL_GPU does not
/// report). Textures that are drawn into (render targets, shadow maps) are apart from the others.
///
/// `Textures`: sampled textures (images, environments, fog). `Targets`: textures that are drawn
/// into (the scene's targets, shadow maps). `Buffers`: vertex, index, instance and storage buffers
/// (transfer buffers are not counted). `Count`: the number of kinds, not a kind.
enum class GpuMemoryKind { Textures, Targets, Buffers, Count };
/// Bytes held now by GpuResource objects created with a size, per GpuMemoryKind (indexed by its
/// value). Atomic: readable from any thread; read them through gpu_memory().
inline std::atomic<long long> gpu_memory_bytes[static_cast<int>(GpuMemoryKind::Count)]{};

/// Owns one GPU object and releases it when it goes out of scope. Move-only, so a resource can
/// never be released twice.
///
/// The resource remembers the device it came from and needs it to release itself: **the Renderer
/// (which owns the device) must outlive every resource created from it.** In practice, declare
/// resources after the Application in the same scope, or inside objects that the Application outlives.
///
/// `T` is the SDL_GPU object type (SDL_GPUBuffer, SDL_GPUTexture...), `Release` a function object
/// that releases one (ReleaseBuffer and the like); use the aliases GpuBuffer, GpuTexture and so on.
/// Releasing is safe while the GPU still uses the object in a frame in flight: SDL_GPU frees it
/// once the GPU is done. Not thread-safe as an object; the counters it updates are atomic.
template <typename T, typename Release>
class GpuResource {
public:
    /// Holds nothing: get() is null, converts to false.
    GpuResource() = default;
    /// Takes ownership of `handle`, created on `device` (null `handle`: holds nothing). Counted in
    /// gpu_resource_counts(), not in gpu_memory().
    GpuResource(SDL_GPUDevice* device, T* handle) : device_(device), handle_(handle) {
        if (handle_ != nullptr) {
            ++gpu_live_count<T>;
        }
    }
    /// Also counts `bytes` of GPU memory of that kind while the object lives (gpu_memory()).
    GpuResource(SDL_GPUDevice* device, T* handle, std::size_t bytes, GpuMemoryKind kind)
        : GpuResource(device, handle) {
        if (handle_ != nullptr) {
            bytes_ = static_cast<long long>(bytes);
            kind_ = kind;
            gpu_memory_bytes[static_cast<int>(kind_)] += bytes_;
        }
    }
    ~GpuResource() { reset(); }

    GpuResource(const GpuResource&) = delete;
    GpuResource& operator=(const GpuResource&) = delete;

    /// Takes the object of `other`, which then holds nothing.
    GpuResource(GpuResource&& other) noexcept
        : device_(other.device_),
          handle_(std::exchange(other.handle_, nullptr)),
          bytes_(std::exchange(other.bytes_, 0)),
          kind_(other.kind_) {}

    /// Releases the object held (reset()), then takes the object of `other`, which then holds nothing.
    GpuResource& operator=(GpuResource&& other) noexcept {
        if (this != &other) {
            reset();
            device_ = other.device_;
            handle_ = std::exchange(other.handle_, nullptr);
            bytes_ = std::exchange(other.bytes_, 0);
            kind_ = other.kind_;
        }
        return *this;
    }

    /// The SDL_GPU object, still owned by this resource: valid until reset(), a move or destruction.
    /// Null when it holds nothing.
    T* get() const { return handle_; }
    /// Whether it holds an object.
    explicit operator bool() const { return handle_ != nullptr; }

    /// Releases the object now (and takes it out of the counters); then it holds nothing. Nothing
    /// happens when it already holds nothing.
    void reset() {
        if (handle_ != nullptr) {
            Release{}(device_, handle_);
            handle_ = nullptr;
            --gpu_live_count<T>;
            gpu_memory_bytes[static_cast<int>(kind_)] -= bytes_;
            bytes_ = 0;
        }
    }

private:
    SDL_GPUDevice* device_ = nullptr;
    T* handle_ = nullptr;
    long long bytes_ = 0;
    GpuMemoryKind kind_ = GpuMemoryKind::Textures;
};

/// The `Release` of GpuBuffer: SDL_ReleaseGPUBuffer().
struct ReleaseBuffer {
    /// Releases `buffer`, created on `device`.
    void operator()(SDL_GPUDevice* device, SDL_GPUBuffer* buffer) const { SDL_ReleaseGPUBuffer(device, buffer); }
};
/// The `Release` of GpuTexture: SDL_ReleaseGPUTexture().
struct ReleaseTexture {
    /// Releases `texture`, created on `device`.
    void operator()(SDL_GPUDevice* device, SDL_GPUTexture* texture) const { SDL_ReleaseGPUTexture(device, texture); }
};
/// The `Release` of GpuSampler: SDL_ReleaseGPUSampler().
struct ReleaseSampler {
    /// Releases `sampler`, created on `device`.
    void operator()(SDL_GPUDevice* device, SDL_GPUSampler* sampler) const { SDL_ReleaseGPUSampler(device, sampler); }
};
/// The `Release` of GpuShader: SDL_ReleaseGPUShader().
struct ReleaseShader {
    /// Releases `shader`, created on `device`.
    void operator()(SDL_GPUDevice* device, SDL_GPUShader* shader) const { SDL_ReleaseGPUShader(device, shader); }
};
/// The `Release` of GpuTransferBuffer: SDL_ReleaseGPUTransferBuffer().
struct ReleaseTransferBuffer {
    /// Releases `buffer`, created on `device`.
    void operator()(SDL_GPUDevice* device, SDL_GPUTransferBuffer* buffer) const {
        SDL_ReleaseGPUTransferBuffer(device, buffer);
    }
};
/// The `Release` of GpuGraphicsPipeline: SDL_ReleaseGPUGraphicsPipeline().
struct ReleaseGraphicsPipeline {
    /// Releases `pipeline`, created on `device`.
    void operator()(SDL_GPUDevice* device, SDL_GPUGraphicsPipeline* pipeline) const {
        SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
    }
};

/// A one-shot SDL properties object that names a resource for graphics debuggers (RenderDoc, Xcode).
/// `key` is one of the SDL_PROP_GPU_*_CREATE_NAME_STRING constants for the resource being created.
/// With `name` null, id() is 0, which every SDL_CreateGPU*() call reads as "no properties": naming
/// stays entirely optional. Keep the object alive across the SDL_CreateGPU*() call that reads it.
class NameProperty {
public:
    /// Creates the properties holding `name` under `key`, unless `name` is null. `name` is copied.
    NameProperty(const char* key, const char* name) {
        if (name != nullptr) {
            id_ = SDL_CreateProperties();
            SDL_SetStringProperty(id_, key, name);
        }
    }
    ~NameProperty() {
        if (id_ != 0) {
            SDL_DestroyProperties(id_);
        }
    }
    NameProperty(const NameProperty&) = delete;
    NameProperty& operator=(const NameProperty&) = delete;

    /// What to put in the create info's `props`: 0 when `name` was null.
    SDL_PropertiesID id() const { return id_; }

private:
    SDL_PropertiesID id_ = 0;
};

/// The GPU objects alive now, by kind: those held by GpuResource objects, which is all the long-lived
/// ones. Stable counts across scene changes mean nothing leaks.
struct GpuResourceCounts {
    long buffers = 0;           ///< GpuBuffer
    long textures = 0;          ///< GpuTexture
    long samplers = 0;          ///< GpuSampler
    long transfer_buffers = 0;  ///< GpuTransferBuffer
    long shaders = 0;           ///< GpuShader
    long pipelines = 0;         ///< GpuGraphicsPipeline

    /// Every kind together.
    long total() const { return buffers + textures + samplers + transfer_buffers + shaders + pipelines; }
    /// Same count for every kind.
    bool operator==(const GpuResourceCounts&) const = default;
};

/// The counts now (from gpu_live_count). Cheap; any thread, though the counts read one by one may
/// change in between while another thread creates resources.
inline GpuResourceCounts gpu_resource_counts() {
    GpuResourceCounts counts;
    counts.buffers = gpu_live_count<SDL_GPUBuffer>;
    counts.textures = gpu_live_count<SDL_GPUTexture>;
    counts.samplers = gpu_live_count<SDL_GPUSampler>;
    counts.transfer_buffers = gpu_live_count<SDL_GPUTransferBuffer>;
    counts.shaders = gpu_live_count<SDL_GPUShader>;
    counts.pipelines = gpu_live_count<SDL_GPUGraphicsPipeline>;
    return counts;
}

/// The GPU memory the engine holds now, in bytes, by kind (see GpuMemoryKind).
struct GpuMemory {
    long long textures = 0;  ///< GpuMemoryKind::Textures
    long long targets = 0;   ///< GpuMemoryKind::Targets
    long long buffers = 0;   ///< GpuMemoryKind::Buffers
    /// Every kind together.
    long long total() const { return textures + targets + buffers; }
};

/// The bytes held now (from gpu_memory_bytes). Cheap; any thread.
inline GpuMemory gpu_memory() {
    return {gpu_memory_bytes[static_cast<int>(GpuMemoryKind::Textures)],
            gpu_memory_bytes[static_cast<int>(GpuMemoryKind::Targets)],
            gpu_memory_bytes[static_cast<int>(GpuMemoryKind::Buffers)]};
}

/// The size of a texture in bytes (every level, layer and sample), from its creation info, block
/// compression included; what to give the GpuResource constructor that counts memory.
inline std::size_t texture_memory_bytes(const SDL_GPUTextureCreateInfo& info) {
    std::size_t bytes = 0;
    for (Uint32 level = 0; level < info.num_levels; ++level) {
        const Uint32 width = info.width >> level > 0 ? info.width >> level : 1;
        const Uint32 height = info.height >> level > 0 ? info.height >> level : 1;
        const Uint32 depth = info.type == SDL_GPU_TEXTURETYPE_3D
                                 ? (info.layer_count_or_depth >> level > 0 ? info.layer_count_or_depth >> level : 1)
                                 : 1;
        bytes += SDL_CalculateGPUTextureFormatSize(info.format, width, height, depth);
    }
    if (info.type != SDL_GPU_TEXTURETYPE_3D) {
        bytes *= info.layer_count_or_depth;
    }
    static constexpr Uint32 kSamples[] = {1, 2, 4, 8};
    return bytes * kSamples[info.sample_count];
}

/// The kind of a texture, from its creation info: GpuMemoryKind::Targets when it can be drawn into
/// (color or depth target), else GpuMemoryKind::Textures. A sampled texture that is a color target
/// only so that the GPU can generate its mipmaps is a texture: give its kind explicitly (as
/// Renderer::create_texture() does).
inline GpuMemoryKind texture_memory_kind(const SDL_GPUTextureCreateInfo& info) {
    return (info.usage & (SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) != 0
               ? GpuMemoryKind::Targets
               : GpuMemoryKind::Textures;
}

using GpuBuffer = GpuResource<SDL_GPUBuffer, ReleaseBuffer>;                    ///< owns an SDL_GPUBuffer
using GpuTexture = GpuResource<SDL_GPUTexture, ReleaseTexture>;                 ///< owns an SDL_GPUTexture
using GpuSampler = GpuResource<SDL_GPUSampler, ReleaseSampler>;                 ///< owns an SDL_GPUSampler
using GpuTransferBuffer = GpuResource<SDL_GPUTransferBuffer, ReleaseTransferBuffer>;  ///< owns an SDL_GPUTransferBuffer
using GpuShader = GpuResource<SDL_GPUShader, ReleaseShader>;                    ///< owns an SDL_GPUShader
using GpuGraphicsPipeline = GpuResource<SDL_GPUGraphicsPipeline, ReleaseGraphicsPipeline>;  ///< owns an SDL_GPUGraphicsPipeline

}  // namespace moteur
