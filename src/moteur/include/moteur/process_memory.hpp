#pragma once

#include <cstddef>

namespace moteur {

/// The memory the process uses now, in bytes: what it has committed for itself (Windows: private
/// bytes; macOS: physical footprint; Linux: resident set). 0 if the system cannot tell. For leak
/// checks: compare two moments of the same run, not two systems. A system call: cheap enough for
/// once per frame, from any thread.
std::size_t process_memory_bytes();

/// The highest value process_memory_bytes() has reached since the process started (the operating
/// system keeps it: Windows, macOS), in bytes; for the measurement reports (budget of milestone 8).
/// Where the system keeps no peak (Linux), the current value. 0 if the system cannot tell.
std::size_t process_peak_memory_bytes();

/// The bytes allocated now on the C/C++ heap (malloc, new), and nothing else: unlike
/// process_memory_bytes(), the graphics driver's memory (which moves with its own caches) is not in
/// it. For leak checks over long runs (milestone 8, part 8). 0 if the system cannot tell (Linux).
/// Walks the heap's summary (Windows: HeapSummary of the process heap; macOS: the malloc zones):
/// costlier than process_memory_bytes(), call it at the moments compared, not every frame.
std::size_t process_heap_bytes();

}  // namespace moteur
