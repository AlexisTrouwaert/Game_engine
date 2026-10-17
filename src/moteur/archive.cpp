#include "moteur/archive.hpp"

#include <SDL3/SDL.h>

// ZSTD_decompressBound is in zstd's static-linking API: zstd is linked statically, at the version
// vcpkg pins.
#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <unordered_map>

#include "moteur/file_io.hpp"

namespace moteur {

namespace {

constexpr char kMagic[4] = {'M', 'P', 'A', 'K'};
constexpr std::size_t kHeaderSize = 4 + 4 + 4 + 4 + 8;
constexpr int kZstdLevel = 19;

std::uint64_t fnv1a(const std::uint8_t* bytes, std::size_t size) {
    std::uint64_t hash = 14695981039346656037ull;
    for (std::size_t i = 0; i < size; ++i) {
        hash = (hash ^ bytes[i]) * 1099511628211ull;
    }
    return hash;
}

template <typename T>
void put(std::vector<std::uint8_t>& out, T value) {
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        out.push_back(static_cast<std::uint8_t>(static_cast<std::uint64_t>(value) >> (8 * i)));
    }
}

template <typename T>
bool take(const std::uint8_t*& at, const std::uint8_t* end, T& value) {
    if (end - at < static_cast<std::ptrdiff_t>(sizeof(T))) {
        return false;
    }
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        v |= static_cast<std::uint64_t>(at[i]) << (8 * i);
    }
    value = static_cast<T>(v);
    at += sizeof(T);
    return true;
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// Reads `size` bytes at `offset` of a file, with a handle of its own (thread-safe). The sizes come
// from the file itself: they are checked against the file's size before anything is allocated (a
// damaged header must not ask for gigabytes, found by fuzzing, milestone 8 part 8).
std::vector<std::uint8_t> read_range(const std::string& path, std::uint64_t offset, std::uint64_t size) {
    SDL_IOStream* io = SDL_IOFromFile(path.c_str(), "rb");
    if (io == nullptr) {
        throw std::runtime_error(std::string("cannot open: ") + SDL_GetError());
    }
    const Sint64 file_size = SDL_GetIOSize(io);
    if (file_size < 0 || offset > static_cast<std::uint64_t>(file_size) || size > static_cast<std::uint64_t>(file_size) - offset) {
        SDL_CloseIO(io);
        throw std::runtime_error("truncated (" + std::to_string(file_size < 0 ? 0 : file_size) + " bytes, " +
                                 std::to_string(offset) + " + " + std::to_string(size) + " expected)");
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    const bool ok = SDL_SeekIO(io, static_cast<Sint64>(offset), SDL_IO_SEEK_SET) == static_cast<Sint64>(offset) &&
                    (size == 0 || SDL_ReadIO(io, bytes.data(), bytes.size()) == bytes.size());
    SDL_CloseIO(io);
    if (!ok) {
        throw std::runtime_error("truncated");
    }
    return bytes;
}

}  // namespace

bool archive_should_compress(const std::string& path) {
    const std::size_t dot = path.rfind('.');
    const std::string extension = dot == std::string::npos ? std::string() : lower(path.substr(dot));
    for (const char* packed : {".ktx2", ".ogg", ".mp3", ".png", ".jpg", ".jpeg", ".flac"}) {
        if (extension == packed) {
            return false;
        }
    }
    return true;
}

std::unique_ptr<Archive> Archive::open(const std::string& path, std::string& error) {
    try {
        const std::vector<std::uint8_t> header = read_range(path, 0, kHeaderSize);
        const std::uint8_t* at = header.data();
        const std::uint8_t* end = at + header.size();
        if (std::memcmp(at, kMagic, 4) != 0) {
            error = "Archive '" + path + "': not an archive of the engine";
            return nullptr;
        }
        at += 4;
        std::uint32_t version = 0, count = 0, index_size = 0;
        std::uint64_t index_hash = 0;
        take(at, end, version);
        take(at, end, count);
        take(at, end, index_size);
        take(at, end, index_hash);
        if (version != kVersion) {
            error = "Archive '" + path + "': version " + std::to_string(version) + ", " + std::to_string(kVersion) + " expected";
            return nullptr;
        }
        const std::vector<std::uint8_t> index = read_range(path, kHeaderSize, index_size);
        if (fnv1a(index.data(), index.size()) != index_hash) {
            error = "Archive '" + path + "': damaged index";
            return nullptr;
        }
        auto archive = std::unique_ptr<Archive>(new Archive());
        archive->path_ = path;
        at = index.data();
        end = at + index.size();
        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint16_t length = 0;
            Entry entry;
            if (!take(at, end, length) || end - at < length) {
                error = "Archive '" + path + "': damaged index";
                return nullptr;
            }
            entry.path.assign(reinterpret_cast<const char*>(at), length);
            at += length;
            if (!take(at, end, entry.offset) || !take(at, end, entry.stored_size) || !take(at, end, entry.size) ||
                !take(at, end, entry.compression) || !take(at, end, entry.crc)) {
                error = "Archive '" + path + "': damaged index";
                return nullptr;
            }
            // find() searches by halves: an index out of order (or a path twice) would make it miss
            // entries without a word. A hash that matches does not prove the writer sorted it.
            if (!archive->entries_.empty() && !(archive->entries_.back().path < entry.path)) {
                error = "Archive '" + path + "': index not sorted ('" + entry.path + "' after '" + archive->entries_.back().path + "')";
                return nullptr;
            }
            if (entry.compression > 1) {  // read() would hand the stored bytes over as they are
                error = "Archive '" + path + "': '" + entry.path + "': unknown compression " + std::to_string(entry.compression);
                return nullptr;
            }
            archive->entries_.push_back(std::move(entry));
        }
        // A truncated archive (an interrupted copy or download) is told now, not at the first read.
        std::uint64_t needed = kHeaderSize + index_size;
        for (const Entry& entry : archive->entries_) {
            if (entry.stored_size > UINT64_MAX - entry.offset) {  // a sum that wraps would look small
                error = "Archive '" + path + "': damaged index";
                return nullptr;
            }
            needed = std::max(needed, entry.offset + entry.stored_size);
        }
        SDL_PathInfo info{};
        if (!SDL_GetPathInfo(path.c_str(), &info) || info.size < needed) {
            error = "Archive '" + path + "': truncated (" + std::to_string(info.size) + " bytes, " + std::to_string(needed) +
                    " expected)";
            return nullptr;
        }
        return archive;
    } catch (const std::exception& e) {
        error = "Archive '" + path + "': " + e.what();
        return nullptr;
    }
}

const Archive::Entry* Archive::find(const std::string& path) const {
    const auto found = std::lower_bound(entries_.begin(), entries_.end(), path,
                                        [](const Entry& entry, const std::string& key) { return entry.path < key; });
    return found != entries_.end() && found->path == path ? &*found : nullptr;
}

std::vector<std::uint8_t> Archive::read(const Entry& entry) const {
    std::vector<std::uint8_t> stored;
    try {
        stored = read_range(path_, entry.offset, entry.stored_size);
    } catch (const std::exception& e) {
        throw std::runtime_error("Archive '" + path_ + "', '" + entry.path + "': " + e.what());
    }
    std::vector<std::uint8_t> bytes;
    if (entry.compression == 1) {
        // The size the index gives is checked against what the stored frames can hold at most,
        // before it is allocated.
        const unsigned long long bound = ZSTD_decompressBound(stored.data(), stored.size());
        if (bound == ZSTD_CONTENTSIZE_ERROR || entry.size > bound) {
            throw std::runtime_error("Archive '" + path_ + "', '" + entry.path + "': cannot decompress it");
        }
        bytes.resize(static_cast<std::size_t>(entry.size));
        const std::size_t written = ZSTD_decompress(bytes.data(), bytes.size(), stored.data(), stored.size());
        if (ZSTD_isError(written) || written != bytes.size()) {
            throw std::runtime_error("Archive '" + path_ + "', '" + entry.path + "': cannot decompress it");
        }
    } else {
        bytes = std::move(stored);
    }
    if (crc32(bytes.data(), bytes.size()) != entry.crc) {
        throw std::runtime_error("Archive '" + path_ + "', '" + entry.path + "': damaged (CRC32)");
    }
    return bytes;
}

bool write_archive(const std::string& path, std::vector<ArchiveInput> inputs, std::string& error) {
    std::sort(inputs.begin(), inputs.end(), [](const ArchiveInput& a, const ArchiveInput& b) { return a.path < b.path; });
    // Every pair, not only neighbours: the byte order puts 'Audio/' and 'audio/' apart ('M' < 'a').
    std::unordered_map<std::string, const std::string*> lowered;
    lowered.reserve(inputs.size());
    for (const ArchiveInput& input : inputs) {
        const auto [found, fresh] = lowered.emplace(lower(input.path), &input.path);
        if (!fresh) {
            error = "Archive '" + path + "': '" + *found->second + "' and '" + input.path + "' collide";
            return false;
        }
    }
    // Each entry, stored (compressed when it is worth it: kept raw if it does not shrink).
    std::vector<Archive::Entry> entries;
    std::vector<std::vector<std::uint8_t>> stored;
    for (ArchiveInput& input : inputs) {
        Archive::Entry entry;
        entry.path = input.path;
        entry.size = input.data.size();
        entry.crc = crc32(input.data.data(), input.data.size());
        std::vector<std::uint8_t> bytes;
        if (input.compress && !input.data.empty()) {
            bytes.resize(ZSTD_compressBound(input.data.size()));
            const std::size_t written = ZSTD_compress(bytes.data(), bytes.size(), input.data.data(), input.data.size(), kZstdLevel);
            if (!ZSTD_isError(written) && written < input.data.size()) {
                bytes.resize(written);
                entry.compression = 1;
            }
        }
        if (entry.compression == 0) {
            bytes = std::move(input.data);
        }
        entry.stored_size = bytes.size();
        entries.push_back(std::move(entry));
        stored.push_back(std::move(bytes));
    }
    // The index's size, then the offsets.
    std::size_t index_size = 0;
    for (const Archive::Entry& entry : entries) {
        index_size += 2 + entry.path.size() + 8 + 8 + 8 + 1 + 4;
    }
    const auto align = [](std::uint64_t value) {
        return (value + Archive::kAlignment - 1) / Archive::kAlignment * Archive::kAlignment;
    };
    std::uint64_t offset = align(kHeaderSize + index_size);
    for (Archive::Entry& entry : entries) {
        entry.offset = offset;
        offset = align(offset + entry.stored_size);
    }
    std::vector<std::uint8_t> index;
    for (const Archive::Entry& entry : entries) {
        if (entry.path.size() > 0xFFFF) {
            error = "Archive '" + path + "': path too long: " + entry.path;
            return false;
        }
        put(index, static_cast<std::uint16_t>(entry.path.size()));
        index.insert(index.end(), entry.path.begin(), entry.path.end());
        put(index, entry.offset);
        put(index, entry.stored_size);
        put(index, entry.size);
        put(index, entry.compression);
        put(index, entry.crc);
    }
    std::vector<std::uint8_t> file;
    file.reserve(static_cast<std::size_t>(offset));
    file.insert(file.end(), kMagic, kMagic + 4);
    put(file, Archive::kVersion);
    put(file, static_cast<std::uint32_t>(entries.size()));
    put(file, static_cast<std::uint32_t>(index.size()));
    put(file, fnv1a(index.data(), index.size()));
    file.insert(file.end(), index.begin(), index.end());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        file.resize(static_cast<std::size_t>(entries[i].offset), 0);
        file.insert(file.end(), stored[i].begin(), stored[i].end());
    }
    return write_file_atomic(path, file.data(), file.size(), error);
}

}  // namespace moteur
