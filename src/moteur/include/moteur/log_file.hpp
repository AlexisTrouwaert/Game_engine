#pragma once

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>

namespace moteur {

class Console;

// The log written to a file (milestone 7, part 3): every SDL_Log line, from any thread, goes to
// journal.txt in the player's preferences (one line per message: time, level, text), still to the
// terminal, and to the console. The previous run's file is kept as journal.1.txt, and so is the
// current one when it grows past `max_bytes`. Each line is flushed: a crash leaves the log.
class LogFile {
public:
    LogFile() = default;
    ~LogFile();
    LogFile(const LogFile&) = delete;
    LogFile& operator=(const LogFile&) = delete;

    // Opens `directory`/journal.txt (rotating the previous one) and takes over SDL's log output.
    // False, with a message on the terminal, if the file cannot be opened (the log then only goes
    // to the terminal and the console).
    bool open(const std::string& directory, std::size_t max_bytes = 4 * 1024 * 1024);
    // Lines also go to this console (null: none).
    void set_console(Console* console);
    // Gives SDL its own output back and closes the file.
    void close();
    const std::string& path() const { return path_; }

    // "12:04:36.512 [info] text" (local time).
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
    void* previous_ = nullptr;           // SDL's output function before ours
    void* previous_userdata_ = nullptr;
    bool installed_ = false;
};

}  // namespace moteur
