#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "moteur/variables.hpp"

namespace moteur {

// The console (milestone 7, part 3): commands typed in the game ("spawn squelette 5", "set
// fog.enabled 0"), variables, and the log lines, shown by the DEBUG > Console window.
//
// Two kinds of commands keep the game deterministic:
// - tool commands (looking, measuring, reloading assets, timing) run at once and never touch the
//   game's logic;
// - game commands (spawning, teleporting, cheating) are queued and run at the start of the next
//   tick, and the Application records them in input recordings: a replay that contains them gives
//   the same game again.
class Console {
public:
    enum class Kind { Tool, Game };
    enum class Level { Info, Warning, Error, Input };
    struct Line {
        Level level;
        std::string text;
    };
    using Args = std::vector<std::string>;  // the words after the command's name
    using Handler = std::function<void(const Args& args, Console& console)>;
    // Candidates for argument `index` (from 0) starting with `prefix`.
    using Completer = std::function<std::vector<std::string>(std::size_t index, const std::string& prefix)>;

    Console();

    Variables& variables() { return variables_; }
    const Variables& variables() const { return variables_; }

    // Registers a command (replacing one of the same name: a scene registers its commands each time
    // it starts). `usage`: its arguments ("<monstre> [nombre]"); `help`: one line.
    void add(std::string name, std::string usage, std::string help, Kind kind, Handler handler, Completer completer = {});
    void remove(std::string_view name);
    bool has(std::string_view name) const;

    // A line typed by the player or read from a script: tool commands run now, game commands are
    // queued (and so is "set" of a Logic variable). Shown in the output with its result.
    void submit(const std::string& line);
    // Runs every line of a text (a script: "exec file"); '#' starts a comment. False if a line failed.
    bool run_script(std::string_view text);
    // The game commands queued since the last call (the Application takes them before each tick).
    std::vector<std::string> take_game_commands();
    // Runs a game command line now (the Application, at the start of a tick).
    void run_game_command(const std::string& line);

    void print(std::string text) { add_line(Level::Info, std::move(text)); }
    void warn(std::string text) { add_line(Level::Warning, std::move(text)); }
    void error(std::string text) { add_line(Level::Error, std::move(text)); }
    // From any thread (the log): kept until the main thread calls flush_log().
    void log_line(Level level, std::string text);
    void flush_log();
    const std::deque<Line>& lines() const { return lines_; }
    void clear() { lines_.clear(); }
    // Grows by one at each new line (the window scrolls down when it changes).
    std::uint64_t line_count() const { return line_count_; }

    // The lines typed, oldest first.
    const std::vector<std::string>& history() const { return history_; }
    // Candidates for the last word of `line` (command names, variable names, the command's own
    // arguments), sorted.
    std::vector<std::string> complete(const std::string& line) const;
    // `line` with its last word extended as far as all candidates agree.
    std::string complete_line(const std::string& line) const;

    // The words of a line: spaces separate them, double quotes keep spaces inside one. False, with
    // `error`, for an unclosed quote.
    static bool split(std::string_view line, std::vector<std::string>& words, std::string& error);

private:
    struct Command {
        std::string name, usage, help;
        Kind kind;
        Handler handler;
        Completer completer;
    };
    const Command* find(std::string_view name) const;
    void execute(const std::vector<std::string>& words, bool from_queue);
    void add_line(Level level, std::string text);
    void add_builtins();

    Variables variables_;
    std::vector<Command> commands_;
    std::vector<std::string> queued_;
    std::deque<Line> lines_;
    std::uint64_t line_count_ = 0;
    std::uint64_t error_count_ = 0;
    std::vector<std::string> history_;
    std::mutex log_mutex_;
    std::vector<Line> log_queue_;
    static constexpr std::size_t kMaxLines = 2000;
};

}  // namespace moteur
