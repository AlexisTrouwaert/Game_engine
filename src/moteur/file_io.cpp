#include "moteur/file_io.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#endif

namespace moteur {

namespace {

std::filesystem::path utf8_path(const std::string& text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

#ifdef _WIN32
std::string last_error_text() {
    const DWORD code = GetLastError();
    char buffer[256] = {};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0, buffer, sizeof(buffer), nullptr);
    std::string text(buffer);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
    return text + " (" + std::to_string(code) + ")";
}
#endif

}  // namespace

bool write_file_atomic(const std::string& path, const void* data, std::size_t size, std::string& error, bool keep_backup) {
    const std::filesystem::path target = utf8_path(path);
    const std::filesystem::path temporary = utf8_path(path + ".tmp");
    const std::filesystem::path backup = utf8_path(path + ".bak");
    std::error_code ec;
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path(), ec);
    }
#ifdef _WIN32
    HANDLE file = CreateFileW(temporary.wstring().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = "cannot create " + path + ".tmp: " + last_error_text();
        return false;
    }
    const auto* bytes = static_cast<const char*>(data);
    std::size_t written_total = 0;
    while (written_total < size) {
        DWORD written = 0;
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(size - written_total, 1u << 30));
        if (!WriteFile(file, bytes + written_total, chunk, &written, nullptr) || written == 0) {
            error = "cannot write " + path + ".tmp: " + last_error_text();
            CloseHandle(file);
            DeleteFileW(temporary.wstring().c_str());
            return false;
        }
        written_total += written;
    }
    if (!FlushFileBuffers(file)) {
        error = "cannot flush " + path + ".tmp: " + last_error_text();
        CloseHandle(file);
        DeleteFileW(temporary.wstring().c_str());
        return false;
    }
    CloseHandle(file);
    const bool exists = std::filesystem::exists(target, ec);
    BOOL ok = FALSE;
    if (exists) {
        // One step: the temporary file takes the target's place; the old content becomes the backup.
        ok = ReplaceFileW(target.wstring().c_str(), temporary.wstring().c_str(), keep_backup ? backup.wstring().c_str() : nullptr,
                          REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr);
    } else {
        ok = MoveFileExW(temporary.wstring().c_str(), target.wstring().c_str(), MOVEFILE_WRITE_THROUGH);
    }
    if (!ok) {
        error = "cannot replace " + path + ": " + last_error_text();
        DeleteFileW(temporary.wstring().c_str());
        return false;
    }
    return true;
#else
    const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        error = "cannot create " + path + ".tmp: " + std::strerror(errno);
        return false;
    }
    const auto* bytes = static_cast<const char*>(data);
    std::size_t written_total = 0;
    while (written_total < size) {
        const ssize_t written = ::write(fd, bytes + written_total, size - written_total);
        if (written <= 0) {
            if (written < 0 && errno == EINTR) {
                continue;
            }
            error = "cannot write " + path + ".tmp: " + std::strerror(errno);
            ::close(fd);
            ::unlink(temporary.c_str());
            return false;
        }
        written_total += static_cast<std::size_t>(written);
    }
    if (::fsync(fd) != 0) {
        error = "cannot flush " + path + ".tmp: " + std::strerror(errno);
        ::close(fd);
        ::unlink(temporary.c_str());
        return false;
    }
    ::close(fd);
    if (keep_backup && std::filesystem::exists(target, ec)) {
        // A hard link keeps the old content as the backup while the target is replaced below.
        ::unlink(backup.c_str());
        if (::link(target.c_str(), backup.c_str()) != 0) {
            std::filesystem::copy_file(target, backup, std::filesystem::copy_options::overwrite_existing, ec);
        }
    }
    if (::rename(temporary.c_str(), target.c_str()) != 0) {
        error = "cannot replace " + path + ": " + std::strerror(errno);
        ::unlink(temporary.c_str());
        return false;
    }
    // The directory entry too: the rename survives a power cut.
    const std::filesystem::path directory = target.has_parent_path() ? target.parent_path() : std::filesystem::path(".");
    const int dir = ::open(directory.c_str(), O_RDONLY);
    if (dir >= 0) {
        ::fsync(dir);
        ::close(dir);
    }
    return true;
#endif
}

std::optional<std::vector<std::uint8_t>> read_file_bytes(const std::string& path) {
    std::ifstream file(utf8_path(path), std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

bool path_exists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::exists(utf8_path(path), ec);
}

std::int64_t file_time(const std::string& path) {
    std::error_code ec;
    const auto time = std::filesystem::last_write_time(utf8_path(path), ec);
    if (ec) {
        return 0;
    }
    return static_cast<std::int64_t>(time.time_since_epoch().count());  // the clock's own ticks (100 ns on Windows)
}

std::uint32_t crc32(const void* data, std::size_t size, std::uint32_t crc) {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            t[i] = c;
        }
        return t;
    }();
    crc = ~crc;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ bytes[i]) & 0xFFu] ^ (crc >> 8);
    }
    return ~crc;
}

}  // namespace moteur
