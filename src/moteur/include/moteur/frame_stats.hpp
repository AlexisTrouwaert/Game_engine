#pragma once

#include <cstddef>
#include <vector>

namespace moteur {

/// Keeps the most recent samples of a measurement (for example the CPU time of each frame, in
/// milliseconds) and summarizes them. Spikes cause stutter, not the average, so besides the mean
/// it reports the maximum and percentiles.
class FrameStats {
public:
    /// `capacity`: how many of the latest samples are kept (8 bytes each, allocated as samples come).
    /// Throws std::invalid_argument if it is 0.
    explicit FrameStats(std::size_t capacity = 65536);

    /// Records a sample, in whatever unit the caller uses. Once the capacity is reached, it replaces
    /// the oldest one. O(1).
    void add(double value);
    /// Forgets every sample (the capacity stays).
    void clear();

    /// Number of samples currently held, at most the capacity.
    std::size_t count() const { return samples_.size(); }

    /// Summaries of the samples held, in their unit; 0 when there are none. O(n).
    ///@{
    double mean() const;
    double max() const;
    ///@}

    /// Nearest-rank percentile, `p` in [0, 100] (clamped): percentile(99) is the value below which
    /// 99 % of the samples fall; percentile(0) is the minimum. 0 when there are no samples. Copies
    /// and sorts the samples: O(n log n), allocates; for reports, not every frame.
    double percentile(double p) const;

    /// Sample `i` of those held, `i` below count() (unchecked). In the order they were added until
    /// the buffer wraps around, in no particular order after.
    double sample(std::size_t i) const { return samples_[i]; }

private:
    std::size_t capacity_;
    std::size_t next_ = 0;  ///< where the next sample goes once the buffer is full
    std::vector<double> samples_;
};

}  // namespace moteur
