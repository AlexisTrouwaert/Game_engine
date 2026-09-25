#pragma once

#include <cstddef>

namespace moteur {

// The memory the process uses now, in bytes: what it has committed for itself (Windows: private
// bytes; macOS: physical footprint; Linux: resident set). 0 if the system cannot tell. For leak
// checks: compare two moments of the same run, not two systems.
std::size_t process_memory_bytes();

}  // namespace moteur
