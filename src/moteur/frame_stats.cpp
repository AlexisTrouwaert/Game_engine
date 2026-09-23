#include "moteur/frame_stats.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace moteur {

FrameStats::FrameStats(std::size_t capacity) : capacity_(capacity) {
    if (capacity == 0) {
        throw std::invalid_argument("FrameStats: capacity must be positive");
    }
    samples_.reserve(std::min<std::size_t>(capacity, 4096));
}

void FrameStats::add(double value) {
    if (samples_.size() < capacity_) {
        samples_.push_back(value);
        return;
    }
    samples_[next_] = value;
    next_ = (next_ + 1) % capacity_;
}

void FrameStats::clear() {
    samples_.clear();
    next_ = 0;
}

double FrameStats::mean() const {
    if (samples_.empty()) {
        return 0.0;
    }
    return std::accumulate(samples_.begin(), samples_.end(), 0.0) / static_cast<double>(samples_.size());
}

double FrameStats::max() const {
    if (samples_.empty()) {
        return 0.0;
    }
    return *std::max_element(samples_.begin(), samples_.end());
}

double FrameStats::percentile(double p) const {
    if (samples_.empty()) {
        return 0.0;
    }
    std::vector<double> sorted = samples_;
    std::sort(sorted.begin(), sorted.end());

    const double count = static_cast<double>(sorted.size());
    const auto rank = static_cast<std::size_t>(std::ceil(std::clamp(p, 0.0, 100.0) * count / 100.0));
    return sorted[std::max<std::size_t>(rank, 1) - 1];
}

}  // namespace moteur
