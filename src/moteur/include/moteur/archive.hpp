#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace moteur {

/// The engine's archive format (milestone 8, part 7): the shipped game's assets and shaders packed
/// into a few files instead of thousands, read through read_file() (see mount_archive()).
///
/// Layout (little-endian):
/// @verbatim
///   header  "MPAK", version (u32), entry count (u32), index size (u32), FNV-1a 64 of the index (u64)
///   index   per entry, sorted by path: path length (u16), path (UTF-8, '/'), offset (u64),
///           stored size (u64), original size (u64), compression (u8: 0 none, 1 Zstandard),
///           CRC32 of the original bytes (u32)
///   data    each entry at an offset aligned on 4096 bytes, in the order of the index
/// @endverbatim
///
/// The index's hash is checked when the archive is opened, each entry's CRC32 when it is read: a
/// damaged archive gives a message, never garbage. The order of the entries is stable (sorted) and
/// each is aligned, so that a change of one asset does not shift the rest (platforms such as Steam
/// patch by blocks). Paths keep their case and are compared exactly; two paths that differ only by
/// their case are refused when writing (Windows and macOS would not tell them apart on disk).
///
/// An open Archive holds its index only (no file handle): the file must stay in place while it is
/// used. Its member functions are const and thread-safe.
class Archive {
public:
    /// The format version written and accepted: an archive of another version is refused.
    static constexpr std::uint32_t kVersion = 1;
    /// The alignment of each entry's data in the file, in bytes.
    static constexpr std::uint64_t kAlignment = 4096;

    /// One file of the archive, as its index describes it.
    struct Entry {
        std::string path;                ///< its path inside the archive, '/' separators, case kept
        std::uint64_t offset = 0;        ///< where its stored bytes start in the archive file, in bytes
        std::uint64_t stored_size = 0;   ///< its size in the archive (compressed or not), in bytes
        std::uint64_t size = 0;          ///< its original size, in bytes: what read() returns
        std::uint8_t compression = 0;    ///< 0: stored as it is; 1: Zstandard
        std::uint32_t crc = 0;           ///< CRC-32 of the original bytes (see crc32())
    };

    /// Reads the header and the index (two reads, no entry data). Null, with `error`, if the file is
    /// missing, not an archive, of another version, its index is damaged, not sorted by path (or has
    /// a path twice), gives an entry an unknown compression, or it is shorter than its index says (an
    /// interrupted copy).
    static std::unique_ptr<Archive> open(const std::string& path, std::string& error);

    /// The archive file's path, as given to open().
    const std::string& path() const { return path_; }
    /// Every entry, sorted by path. Valid as long as the archive.
    const std::vector<Entry>& entries() const { return entries_; }
    /// The entry of that path (compared exactly, case included), or null. O(log n). The pointer is
    /// valid as long as the archive.
    const Entry* find(const std::string& path) const;
    /// The original bytes of an entry, checked (CRC32), decompressed if needed. Thread-safe (each
    /// call opens its own handle). Throws std::runtime_error naming the archive and the entry if it
    /// cannot be read, decompressed or does not match its CRC32. `entry` comes from this archive.
    std::vector<std::uint8_t> read(const Entry& entry) const;

private:
    std::string path_;
    std::vector<Entry> entries_;  ///< sorted by path
};

/// A file to pack.
struct ArchiveInput {
    std::string path;               ///< its path inside the archive ("assets/maps/tranche.json")
    std::vector<std::uint8_t> data; ///< its content (moved into the archive by write_archive())
    bool compress = true;           ///< false for what is already compressed (KTX2, OGG, MP3, PNG)
};

/// Writes an archive (atomically, see write_file_atomic()), its entries sorted by path. Each is
/// compressed with Zstandard (level 19) when `compress` is set and that makes it smaller; otherwise
/// it is stored as it is. The whole archive is built in memory first. False with `error` if two
/// paths collide (same path, or the same but for their case), a path is longer than 65535 bytes, or
/// the file cannot be written.
bool write_archive(const std::string& path, std::vector<ArchiveInput> inputs, std::string& error);

/// Whether a file of this extension is worth compressing: false for the formats that are already
/// compressed (`.ktx2`, `.ogg`, `.mp3`, `.flac`, `.png`, `.jpg`, `.jpeg`, any case), true otherwise
/// (`.json`, `.gltf`, `.bin`...).
bool archive_should_compress(const std::string& path);

}  // namespace moteur
