#pragma once

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "moteur/gpu_resource.hpp"

namespace moteur {

class GeometryPool;

/// A range of a GPU buffer holding the vertices or indices of a mesh: a part of one of the pool's
/// large buffers, or a buffer of its own for a large mesh. Bound with its offset. Move-only; gives
/// its range back to the pool when destroyed (see GeometryPool). It keeps a pointer to its pool:
/// the pool, and so the Renderer that owns it (Renderer::create_geometry()), must outlive it.
class GpuSlice {
public:
    /// An empty slice: no buffer, converts to false.
    GpuSlice() = default;
    ~GpuSlice() { reset(); }
    /// Takes the range of `other`, which becomes empty.
    GpuSlice(GpuSlice&& other) noexcept;
    /// Gives back this slice's range (reset()), then takes the range of `other`, which becomes empty.
    GpuSlice& operator=(GpuSlice&& other) noexcept;
    GpuSlice(const GpuSlice&) = delete;
    GpuSlice& operator=(const GpuSlice&) = delete;

    /// The buffer to bind: a page of the pool shared with other meshes, or the slice's own buffer.
    /// Null for an empty slice. Owned by the pool or the slice: valid while the slice is not reset.
    SDL_GPUBuffer* buffer() const { return own_ ? own_.get() : pooled_; }
    /// Where the range starts in buffer(), in bytes (a multiple of GeometryPool::kAlignment; 0 for a
    /// buffer of its own).
    std::uint32_t offset() const { return offset_; }
    /// The bytes asked for (the range given out is rounded up to GeometryPool::kAlignment).
    std::uint32_t size() const { return size_; }
    /// Whether the slice holds a range.
    explicit operator bool() const { return buffer() != nullptr; }
    /// Gives the range back to its pool (reusable after GeometryPool::kReuseDelayFrames frames) or
    /// releases its own buffer; the slice becomes empty. Nothing happens on an empty slice.
    void reset();

private:
    friend class GeometryPool;
    GpuBuffer own_;                     ///< a large mesh: its own buffer
    GeometryPool* pool_ = nullptr;      ///< otherwise: a range of one of the pool's pages
    SDL_GPUBuffer* pooled_ = nullptr;
    int page_ = -1;
    std::uint32_t offset_ = 0;
    std::uint32_t size_ = 0;
};

/// Sub-allocation of mesh data in large GPU buffers (milestone 8, part 5). Every buffer created
/// costs a resource of its own on the GPU (about 0.3 ms each under Direct3D 12, and memory rounded
/// up to 64 KB): the 400 small buffers of the slice's floor took 130 ms to create. Meshes take a
/// range of a page instead (first fit, ranges merged when freed); a mesh larger than a quarter of a
/// page gets a buffer of its own.
///
/// A freed range is not reused before kReuseDelayFrames frames have begun: the GPU may still be
/// drawing from it in a frame in flight.
///
/// The Renderer owns two pools (vertices, indices), created at the first Renderer::create_geometry();
/// a game does not normally make its own. Not thread-safe: the renderer's thread only.
class GeometryPool {
public:
    static constexpr std::uint32_t kPageSize = 8u << 20;  ///< bytes per page buffer (8 MiB); a quarter of it at most per range
    static constexpr std::uint32_t kAlignment = 256;  ///< offsets valid for vertex and index bindings on every backend
    static constexpr std::uint64_t kReuseDelayFrames = 3;  ///< begin_frame() calls before a freed range is reused

    /// `create_page(size)` makes a GPU buffer of that size, of the pool's usage (vertices or indices).
    explicit GeometryPool(std::function<GpuBuffer(std::uint32_t size)> create_page);
    /// Releases the pages. Every slice of the pool must be gone by then.
    ~GeometryPool();
    GeometryPool(const GeometryPool&) = delete;
    GeometryPool& operator=(const GeometryPool&) = delete;

    /// A range of `size` bytes (its content undefined: the caller uploads into it). First fit among
    /// the pages, a new page when none has room; above kPageSize / 4, a buffer of its own. Throws
    /// std::invalid_argument for 0 bytes, and whatever `create_page` throws.
    GpuSlice allocate(std::uint32_t size);
    /// A new frame begins: ranges freed long enough ago become free again.
    void begin_frame();

    /// What the pool holds now.
    struct Stats {
        std::size_t pages = 0;          ///< page buffers (the large meshes' own buffers not counted)
        std::size_t used_bytes = 0;     ///< in ranges given out
        std::size_t waiting_bytes = 0;  ///< freed, not reusable yet
    };
    /// What the pool holds now. O(ranges waiting).
    Stats stats() const;

    /// The allocation of one page, without GPU (tested apart).
    struct Ranges {
        std::vector<std::pair<std::uint32_t, std::uint32_t>> free;  ///< offset, size; sorted, merged
        /// Takes `size` bytes at an aligned offset; returns false if none fits. `size` is rounded up
        /// to `alignment`, which every free range's offset and size must already be a multiple of.
        bool take(std::uint32_t size, std::uint32_t alignment, std::uint32_t& offset);
        /// Frees `size` bytes at `offset` (taken before, not free now), merged with the free ranges
        /// beside it.
        void give_back(std::uint32_t offset, std::uint32_t size);
    };

private:
    friend class GpuSlice;
    void release(int page, std::uint32_t offset, std::uint32_t size);

    struct Page {
        GpuBuffer buffer;
        Ranges ranges;
    };
    struct Waiting {
        int page;
        std::uint32_t offset;
        std::uint32_t size;
        std::uint64_t frame;
    };
    std::function<GpuBuffer(std::uint32_t)> create_page_;
    std::vector<Page> pages_;
    std::vector<Waiting> waiting_;
    std::uint64_t frame_ = 0;
    std::size_t used_bytes_ = 0;
};

}  // namespace moteur
