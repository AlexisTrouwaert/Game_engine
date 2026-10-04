#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace moteur {

// Files written so that a crash or a power cut never leaves a half-written one (milestone 7,
// part 5): the bytes go to "<path>.tmp", are flushed to the disk, then the temporary file replaces
// the target in one step (ReplaceFileW / MoveFileExW on Windows, rename on POSIX systems). With
// `keep_backup`, the previous content stays as "<path>.bak". Paths are UTF-8. False, with the
// reason in `error`, if anything failed (the target is then untouched).
bool write_file_atomic(const std::string& path, const void* data, std::size_t size, std::string& error,
                       bool keep_backup = false);
inline bool write_file_atomic(const std::string& path, const std::string& text, std::string& error, bool keep_backup = false) {
    return write_file_atomic(path, text.data(), text.size(), error, keep_backup);
}

// The whole file, or nullopt if it cannot be read.
std::optional<std::vector<std::uint8_t>> read_file_bytes(const std::string& path);
// Whether a file or directory exists at this UTF-8 path.
bool path_exists(const std::string& path);
// The last modification time (ticks of the filesystem clock, 100 ns or finer), 0 if none: to notice a file changed by
// someone else before writing over it.
std::int64_t file_time(const std::string& path);

// CRC-32 (IEEE 802.3, the zlib one): continue a running value by passing it as `crc`.
std::uint32_t crc32(const void* data, std::size_t size, std::uint32_t crc = 0);

}  // namespace moteur
