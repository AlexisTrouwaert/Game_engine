#include "moteur/geometry_pool.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace moteur {

namespace {

std::uint32_t round_up(std::uint32_t value, std::uint32_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

}  // namespace

// --- GpuSlice

GpuSlice::GpuSlice(GpuSlice&& other) noexcept
    : own_(std::move(other.own_)),
      pool_(std::exchange(other.pool_, nullptr)),
      pooled_(std::exchange(other.pooled_, nullptr)),
      page_(std::exchange(other.page_, -1)),
      offset_(std::exchange(other.offset_, 0)),
      size_(std::exchange(other.size_, 0)) {}

GpuSlice& GpuSlice::operator=(GpuSlice&& other) noexcept {
    if (this != &other) {
        reset();
        own_ = std::move(other.own_);
        pool_ = std::exchange(other.pool_, nullptr);
        pooled_ = std::exchange(other.pooled_, nullptr);
        page_ = std::exchange(other.page_, -1);
        offset_ = std::exchange(other.offset_, 0);
        size_ = std::exchange(other.size_, 0);
    }
    return *this;
}

void GpuSlice::reset() {
    if (pool_ != nullptr && page_ >= 0) {
        pool_->release(page_, offset_, round_up(size_, GeometryPool::kAlignment));
    }
    own_.reset();
    pool_ = nullptr;
    pooled_ = nullptr;
    page_ = -1;
    offset_ = 0;
    size_ = 0;
}

// --- GeometryPool::Ranges

bool GeometryPool::Ranges::take(std::uint32_t size, std::uint32_t alignment, std::uint32_t& offset) {
    size = round_up(size, alignment);
    for (std::size_t i = 0; i < free.size(); ++i) {
        auto& [start, length] = free[i];
        if (length >= size) {  // every start and length is a multiple of the alignment
            offset = start;
            start += size;
            length -= size;
            if (length == 0) {
                free.erase(free.begin() + static_cast<std::ptrdiff_t>(i));
            }
            return true;
        }
    }
    return false;
}

void GeometryPool::Ranges::give_back(std::uint32_t offset, std::uint32_t size) {
    auto next = std::lower_bound(free.begin(), free.end(), std::make_pair(offset, std::uint32_t{0}));
    next = free.insert(next, {offset, size});
    // Merged with the following range, then with the previous one.
    if (next + 1 != free.end() && next->first + next->second == (next + 1)->first) {
        next->second += (next + 1)->second;
        free.erase(next + 1);
    }
    if (next != free.begin() && (next - 1)->first + (next - 1)->second == next->first) {
        (next - 1)->second += next->second;
        free.erase(next);
    }
}

// --- GeometryPool

GeometryPool::GeometryPool(std::function<GpuBuffer(std::uint32_t size)> create_page)
    : create_page_(std::move(create_page)) {}

GeometryPool::~GeometryPool() = default;

GpuSlice GeometryPool::allocate(std::uint32_t size) {
    if (size == 0) {
        throw std::invalid_argument("GeometryPool: empty allocation");
    }
    GpuSlice slice;
    slice.size_ = size;
    if (size > kPageSize / 4) {
        slice.own_ = create_page_(size);  // a large mesh: a buffer of its own
        return slice;
    }
    std::uint32_t offset = 0;
    int page = -1;
    for (std::size_t p = 0; p < pages_.size(); ++p) {
        if (pages_[p].ranges.take(size, kAlignment, offset)) {
            page = static_cast<int>(p);
            break;
        }
    }
    if (page < 0) {
        Page fresh;
        fresh.buffer = create_page_(kPageSize);
        fresh.ranges.free = {{0, kPageSize}};
        fresh.ranges.take(size, kAlignment, offset);
        pages_.push_back(std::move(fresh));
        page = static_cast<int>(pages_.size()) - 1;
    }
    used_bytes_ += round_up(size, kAlignment);
    slice.pool_ = this;
    slice.pooled_ = pages_[static_cast<std::size_t>(page)].buffer.get();
    slice.page_ = page;
    slice.offset_ = offset;
    return slice;
}

void GeometryPool::release(int page, std::uint32_t offset, std::uint32_t size) {
    used_bytes_ -= size;
    waiting_.push_back({page, offset, size, frame_});
}

void GeometryPool::begin_frame() {
    ++frame_;
    std::erase_if(waiting_, [this](const Waiting& range) {
        if (frame_ - range.frame < kReuseDelayFrames) {
            return false;
        }
        pages_[static_cast<std::size_t>(range.page)].ranges.give_back(range.offset, range.size);
        return true;
    });
}

GeometryPool::Stats GeometryPool::stats() const {
    Stats stats;
    stats.pages = pages_.size();
    stats.used_bytes = used_bytes_;
    for (const Waiting& range : waiting_) {
        stats.waiting_bytes += range.size;
    }
    return stats;
}

}  // namespace moteur
