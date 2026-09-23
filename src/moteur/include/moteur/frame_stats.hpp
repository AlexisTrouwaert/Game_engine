#pragma once

#include <cstddef>
#include <vector>

namespace moteur {

// Keeps the most recent samples of a measurement (for example the CPU time of each frame, in
// milliseconds) and summarizes them. Spikes cause stutter, not the average, so besides the mean
// it reports the maximum and percentiles.
class FrameStats {
public:
    // capacity: how many of the latest samples are kept. Must be positive.
    explicit FrameStats(std::size_t capacity = 65536);

    void add(double value);
    void clear();

    // Number of samples currently held, at most the capacity.
    std::size_t count() const { return samples_.size(); }

    // All of these return 0 when there are no samples.
    double mean() const;
    double max() const;

    // Nearest-rank percentile, p in [0, 100]: percentile(99) is the value below which 99 % of
    // the samples fall.
    double percentile(double p) const;

private:
    std::size_t capacity_;
    std::size_t next_ = 0;  // where the next sample goes once the buffer is full
    std::vector<double> samples_;
};

}  // namespace moteur
