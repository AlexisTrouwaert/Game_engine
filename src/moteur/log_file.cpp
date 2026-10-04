#include "moteur/log_file.hpp"

#include <SDL3/SDL.h>

#include <filesystem>
#ifdef _WIN32
#include <share.h>
#endif

#include "moteur/console.hpp"

namespace moteur {

namespace {

std::filesystem::path utf8_path(const std::string& text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

std::FILE* open_file(const std::string& path, const char* mode) {
#ifdef _WIN32
    const std::wstring wide_mode(mode, mode + std::char_traits<char>::length(mode));
    // Shared: the log can be read while the game runs.
    return _wfsopen(utf8_path(path).wstring().c_str(), wide_mode.c_str(), _SH_DENYNO);
#else
    return std::fopen(path.c_str(), mode);
#endif
}

const char* level_name(int priority) {
    switch (priority) {
        case SDL_LOG_PRIORITY_TRACE:
        case SDL_LOG_PRIORITY_VERBOSE:
        case SDL_LOG_PRIORITY_DEBUG: return "debug";
        case SDL_LOG_PRIORITY_WARN: return "avert";
        case SDL_LOG_PRIORITY_ERROR: return "erreur";
        case SDL_LOG_PRIORITY_CRITICAL: return "critique";
        default: return "info";
    }
}

}  // namespace

LogFile::~LogFile() {
    close();
}

std::string LogFile::format_line(const char* level, const char* message) {
    SDL_Time now = 0;
    SDL_DateTime t{};
    if (SDL_GetCurrentTime(&now) && SDL_TimeToDateTime(now, &t, true)) {
        char stamp[32];
        SDL_snprintf(stamp, sizeof(stamp), "%02d:%02d:%02d.%03d", t.hour, t.minute, t.second, t.nanosecond / 1000000);
        return std::string(stamp) + " [" + level + "] " + message;
    }
    return std::string("[") + level + "] " + message;
}

bool LogFile::open(const std::string& directory, std::size_t max_bytes) {
    close();
    max_bytes_ = max_bytes;
    path_ = directory + "journal.txt";
    old_path_ = directory + "journal.1.txt";
    std::error_code error;
    if (std::filesystem::exists(utf8_path(path_), error)) {
        std::filesystem::rename(utf8_path(path_), utf8_path(old_path_), error);  // the previous run's
    }
    file_ = open_file(path_, "wb");
    bytes_ = 0;
    SDL_LogOutputFunction previous = nullptr;
    SDL_GetLogOutputFunction(&previous, &previous_userdata_);
    previous_ = reinterpret_cast<void*>(previous);
    SDL_SetLogOutputFunction(
        [](void* userdata, int category, SDL_LogPriority priority, const char* message) {
            static_cast<LogFile*>(userdata)->write(category, static_cast<int>(priority), message);
        },
        this);
    installed_ = true;
    if (file_ == nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Log: cannot open '%s': the log only goes to the terminal", path_.c_str());
        return false;
    }
    return true;
}

void LogFile::set_console(Console* console) {
    std::lock_guard lock(mutex_);
    console_ = console;
}

void LogFile::close() {
    if (installed_) {
        SDL_SetLogOutputFunction(reinterpret_cast<SDL_LogOutputFunction>(previous_), previous_userdata_);
        installed_ = false;
    }
    std::lock_guard lock(mutex_);
    if (file_ != nullptr) {
        std::fclose(file_);
        file_ = nullptr;
    }
    console_ = nullptr;
}

void LogFile::rotate() {
    std::fclose(file_);
    std::error_code error;
    std::filesystem::remove(utf8_path(old_path_), error);
    std::filesystem::rename(utf8_path(path_), utf8_path(old_path_), error);
    file_ = open_file(path_, "wb");
    bytes_ = 0;
}

void LogFile::write(int category, int priority, const char* message) {
    if (previous_ != nullptr) {  // the terminal, as before
        reinterpret_cast<SDL_LogOutputFunction>(previous_)(previous_userdata_, category,
                                                           static_cast<SDL_LogPriority>(priority), message);
    }
    std::lock_guard lock(mutex_);
    if (file_ != nullptr) {
        const std::string line = format_line(level_name(priority), message) + "\n";
        std::fwrite(line.data(), 1, line.size(), file_);
        std::fflush(file_);
        bytes_ += line.size();
        if (max_bytes_ > 0 && bytes_ > max_bytes_) {
            rotate();
        }
    }
    if (console_ != nullptr) {
        const Console::Level level = priority >= SDL_LOG_PRIORITY_ERROR ? Console::Level::Error
                                     : priority == SDL_LOG_PRIORITY_WARN ? Console::Level::Warning
                                                                         : Console::Level::Info;
        console_->log_line(level, message);
    }
}

}  // namespace moteur
