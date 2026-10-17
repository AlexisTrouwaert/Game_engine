#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace moteur {

/// Writes a file so that a crash or a power cut never leaves a half-written one (milestone 7,
/// part 5): the bytes go to `path.tmp`, are flushed to the disk, then the temporary file replaces
/// the target in one step (ReplaceFileW / MoveFileExW on Windows, rename on POSIX systems, followed
/// by a flush of the directory). The missing parent directories are created. With `keep_backup`, the
/// previous content stays as `path.bak` (replacing an older backup). Paths are UTF-8. Returns false,
/// with the reason in `error`, if anything failed (the target is then untouched and the temporary
/// file removed). Blocks until the data is on the disk: not for every frame. Any thread, as long as
/// two calls do not write the same path at once.
bool write_file_atomic(const std::string& path, const void* data, std::size_t size, std::string& error,
                       bool keep_backup = false);
/// write_file_atomic() of the bytes of `text` (a JSON document, a settings file), written as they
/// are: no terminating zero, no conversion of the line ends.
inline bool write_file_atomic(const std::string& path, const std::string& text, std::string& error, bool keep_backup = false) {
    return write_file_atomic(path, text.data(), text.size(), error, keep_backup);
}

/// The whole file at this UTF-8 path, read from the disk only (unlike read_file(), never from the
/// mounted archives: for the player's files and the tools), or nullopt if it cannot be opened.
std::optional<std::vector<std::uint8_t>> read_file_bytes(const std::string& path);
/// Whether a file or directory exists on the disk at this UTF-8 path (the archives are not looked
/// at: see file_exists()).
bool path_exists(const std::string& path);
/// The last modification time of the file, in ticks of the filesystem clock (100 ns on Windows,
/// finer elsewhere, from an epoch of the implementation's own), or 0 if it does not exist or cannot
/// be read: to notice that a file was changed by someone else before writing over it. Only compare
/// two values of the same machine.
std::int64_t file_time(const std::string& path);

/// CRC-32 of `size` bytes at `data` (IEEE 802.3, the zlib one: crc32(text, 9) of "123456789" is
/// 0xCBF43926). To checksum data in several pieces, pass the value returned for the previous piece
/// as `crc`; 0 starts a new checksum. Thread-safe.
std::uint32_t crc32(const void* data, std::size_t size, std::uint32_t crc = 0);

}  // namespace moteur
