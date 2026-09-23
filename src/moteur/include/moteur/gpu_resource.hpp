#pragma once

#include <SDL3/SDL.h>

#include <utility>

namespace moteur {

// Owns one GPU object and releases it when it goes out of scope. Move-only, so a resource can
// never be released twice.
//
// The resource remembers the device it came from and needs it to release itself: **the Renderer
// (which owns the device) must outlive every resource created from it.** In practice, declare
// resources after the Application in the same scope, or inside objects that the Application outlives.
template <typename T, typename Release>
class GpuResource {
public:
    GpuResource() = default;
    GpuResource(SDL_GPUDevice* device, T* handle) : device_(device), handle_(handle) {}
    ~GpuResource() { reset(); }

    GpuResource(const GpuResource&) = delete;
    GpuResource& operator=(const GpuResource&) = delete;

    GpuResource(GpuResource&& other) noexcept
        : device_(other.device_), handle_(std::exchange(other.handle_, nullptr)) {}

    GpuResource& operator=(GpuResource&& other) noexcept {
        if (this != &other) {
            reset();
            device_ = other.device_;
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    T* get() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }

    void reset() {
        if (handle_ != nullptr) {
            Release{}(device_, handle_);
            handle_ = nullptr;
        }
    }

private:
    SDL_GPUDevice* device_ = nullptr;
    T* handle_ = nullptr;
};

struct ReleaseBuffer {
    void operator()(SDL_GPUDevice* device, SDL_GPUBuffer* buffer) const { SDL_ReleaseGPUBuffer(device, buffer); }
};
struct ReleaseTexture {
    void operator()(SDL_GPUDevice* device, SDL_GPUTexture* texture) const { SDL_ReleaseGPUTexture(device, texture); }
};
struct ReleaseSampler {
    void operator()(SDL_GPUDevice* device, SDL_GPUSampler* sampler) const { SDL_ReleaseGPUSampler(device, sampler); }
};
struct ReleaseShader {
    void operator()(SDL_GPUDevice* device, SDL_GPUShader* shader) const { SDL_ReleaseGPUShader(device, shader); }
};
struct ReleaseTransferBuffer {
    void operator()(SDL_GPUDevice* device, SDL_GPUTransferBuffer* buffer) const {
        SDL_ReleaseGPUTransferBuffer(device, buffer);
    }
};
struct ReleaseGraphicsPipeline {
    void operator()(SDL_GPUDevice* device, SDL_GPUGraphicsPipeline* pipeline) const {
        SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
    }
};

// A one-shot SDL properties object that names a resource for graphics debuggers (RenderDoc, Xcode).
// `key` is one of the SDL_PROP_GPU_*_CREATE_NAME_STRING constants for the resource being created.
// With `name` null, id() is 0, which every SDL_CreateGPU*() call reads as "no properties": naming
// stays entirely optional. Keep the object alive across the SDL_CreateGPU*() call that reads it.
class NameProperty {
public:
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

    SDL_PropertiesID id() const { return id_; }

private:
    SDL_PropertiesID id_ = 0;
};

using GpuBuffer = GpuResource<SDL_GPUBuffer, ReleaseBuffer>;
using GpuTexture = GpuResource<SDL_GPUTexture, ReleaseTexture>;
using GpuSampler = GpuResource<SDL_GPUSampler, ReleaseSampler>;
using GpuTransferBuffer = GpuResource<SDL_GPUTransferBuffer, ReleaseTransferBuffer>;
using GpuShader = GpuResource<SDL_GPUShader, ReleaseShader>;
using GpuGraphicsPipeline = GpuResource<SDL_GPUGraphicsPipeline, ReleaseGraphicsPipeline>;

}  // namespace moteur
