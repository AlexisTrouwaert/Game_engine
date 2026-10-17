#pragma once

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>

namespace moteur {

class Console;

/// The log written to a file (milestone 7, part 3): every SDL_Log line, from any thread, goes to
/// log.txt in the player's preferences (one line per message: time, level, text), still to the
/// terminal (SDL's previous output), to the console, and to the crash reporter's last lines (see
/// record_crash_log_line()). The previous run's file is kept as log.1.txt; when the current file
/// grows past `max_bytes` it becomes log.1.txt in turn (replacing the previous run's) and a new
/// log.txt starts. Each line is flushed: a crash leaves the log. Application owns one (see
/// ApplicationConfig::log_file); a game does not need another.
///
/// While open, SDL's log output points to this object: it must stay where it is (it cannot be
/// copied or moved) and be closed, or destroyed, before SDL's logging is used for the last time.
class LogFile {
public:
    /// A closed log: nothing is written until open().
    LogFile() = default;
    /// Calls close().
    ~LogFile();
    LogFile(const LogFile&) = delete;
    LogFile& operator=(const LogFile&) = delete;

    /// Opens `directory` + `log.txt` (`directory` ends with a separator, as
    /// Application::preferences_directory() gives it), renaming an existing log.txt to log.1.txt,
    /// and takes over SDL's log output. A log already open is closed first. `max_bytes`: the size
    /// past which the file is rotated, 0 for no limit. False, with a warning on the terminal, if the
    /// file cannot be opened: SDL's output is taken over all the same, and the log then only goes to
    /// the terminal, the console and the crash reports. Main thread, before other threads log.
    bool open(const std::string& directory, std::size_t max_bytes = 4 * 1024 * 1024);
    /// Lines also go to this console (null: none), at its error, warning or info level. The console
    /// must outlive the log, or be unset first. Thread-safe.
    void set_console(Console* console);
    /// Gives SDL its previous output back, closes the file and forgets the console. Nothing happens
    /// if the log is not open.
    void close();
    /// The path of log.txt given to the last open() (empty before): kept after close().
    const std::string& path() const { return path_; }

    /// `12:04:36.512 [info] text`: a line as written to the file (local time, to the millisecond;
    /// without the time if the clock cannot be read), without the line end. `level` is one of
    /// `debug`, `info`, `warn`, `error`, `critical` in the file, but any text is accepted.
    static std::string format_line(const char* level, const char* message);

private:
    void write(int category, int priority, const char* message);
    void rotate();

    std::mutex mutex_;
    std::FILE* file_ = nullptr;
    std::string path_;
    std::string old_path_;
    std::size_t max_bytes_ = 0;
    std::size_t bytes_ = 0;
    Console* console_ = nullptr;
    void* previous_ = nullptr;           ///< SDL's output function before ours
    void* previous_userdata_ = nullptr;
    bool installed_ = false;
};

}  // namespace moteur
