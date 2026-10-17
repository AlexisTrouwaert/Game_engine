#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace moteur {

/// Directory of the executable, with a trailing separator (UTF-8). Assets and shaders are looked up
/// relative to it, so the program works from any working directory. Empty if the system cannot
/// tell. Asks SDL each time (it caches the answer).
std::string base_path();

/// `base_path()` + `assets/` + `name`: where an asset file is, without any check (Assets keys its
/// files from there).
std::string asset_path(const std::string& name);

/// The bytes of a file read by read_file(), owned (allocated with SDL_malloc, freed with SDL_free
/// by the destructor). Moves, does not copy: a moved-from object is empty.
class FileData {
public:
    /// Empty: data() null, size() 0.
    FileData() = default;
    /// Takes ownership of `size` bytes at `data`, which must come from SDL_malloc (or SDL_LoadFile).
    FileData(void* data, std::size_t size) : data_(data), size_(size) {}
    /// Frees the bytes (SDL_free).
    ~FileData();
    /// Takes the bytes of `other`, which is left empty.
    FileData(FileData&& other) noexcept;
    /// Frees the bytes held, then takes those of `other`, which is left empty.
    FileData& operator=(FileData&& other) noexcept;
    FileData(const FileData&) = delete;
    FileData& operator=(const FileData&) = delete;

    /// The bytes, followed by a zero not counted in size() when they come from read_file() (so that
    /// a text can be read as a C string); null when empty. Valid while this object holds them.
    const void* data() const { return data_; }
    /// The number of bytes, the trailing zero not counted.
    std::size_t size() const { return size_; }
    /// Hands the bytes over: the caller frees them with SDL_free. The object is left empty.
    void* release();

private:
    void* data_ = nullptr;
    std::size_t size_ = 0;
};

/// The whole content of a file. Every file the engine loads (assets and shaders) is read through
/// here: a file of the program's folder (base_path()) is looked for in the mounted archives first
/// (see mount_archive()), then on disk. `path` is absolute (base_path() + ...) for the archives to
/// apply; backslash and slash separators, `.` and `..` components are accepted. SDL_LoadFile handles UTF-8
/// paths on Windows, which fopen does not. The bytes are followed by a zero (not counted in the
/// size), as SDL_LoadFile's. Throws std::runtime_error with the reason if the file cannot be read:
/// missing, damaged in its archive (CRC32), or absent from the archives while archives_only() is
/// set. Thread-safe (the job system's threads read through it); blocks on the disk.
FileData read_file(const std::string& path);

/// Whether read_file() would find the file (in an archive or on disk; a directory does not count).
/// Does not check that an archived entry is intact. Thread-safe.
bool file_exists(const std::string& path);

/// Mounts an archive (milestone 8, part 7; see moteur/archive.hpp): its entries
/// (`assets/maps/tranche.json`, `shaders/mesh.vert.dxil`) stand for the files of the same path under
/// base_path(), compared exactly (case included). A later archive's entry hides an earlier one's (a
/// patch). Reads the archive's header and index (see Archive::open()); the archive file must stay
/// in place while mounted. False, with `error`, if it cannot be opened. Mount before the loading
/// starts (at the start of the program): reading is thread-safe, mounting is not meant to race with
/// it. Application mounts ApplicationConfig::archives_directory itself.
bool mount_archive(const std::string& path, std::string& error);
/// Mounts every .pak of `directory`, in the order of their names (so a later name hides an earlier
/// one's entries). Returns how many were mounted; the message of each that could not be is appended
/// to `errors`. A missing directory mounts nothing, without an error.
std::size_t mount_archives(const std::string& directory, std::vector<std::string>& errors);
/// Unmounts every archive and clears set_archives_only(): read_file() reads the disk again (tests,
/// tools). Not while other threads read.
void unmount_archives();
/// The number of archives mounted now.
std::size_t mounted_archives();
/// true: a file of the program's folder that no archive has is an error, never read from disk (the
/// shipped game: what was not prepared does not exist). Files elsewhere (the player's) stay on disk.
/// Application sets it in the shipping configuration once archives are mounted.
void set_archives_only(bool only);
/// Whether set_archives_only() is on.
bool archives_only();

/// read_file() as a string (text files, JSON): the bytes as they are, no conversion of the line
/// ends nor check of the encoding. Throws as read_file().
std::string read_text_file(const std::string& path);

}  // namespace moteur
