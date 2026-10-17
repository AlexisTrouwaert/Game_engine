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

/// The console (milestone 7, part 3): commands typed in the game ("spawn squelette 5", "set
/// fog.enabled 0"), variables, and the log lines, shown by the DEBUG > Console window.
///
/// Two kinds of commands keep the game deterministic:
/// - tool commands (looking, measuring, reloading assets, timing) run at once and never touch the
///   game's logic;
/// - game commands (spawning, teleporting, cheating) are queued and run at the start of the next
///   tick, and the Application records them in input recordings: a replay that contains them gives
///   the same game again.
///
/// Built-in commands: `help`, `set`, `get`, `reset`, `toggle`, `vars`, `echo`, `clear`. Everything
/// but log_line() is called from the main thread.
class Console {
public:
    /// What a command may touch: `Tool` commands run at once and must not change the game's logic;
    /// `Game` commands are queued for the next tick (see the class description).
    enum class Kind { Tool, Game };
    /// The kind of an output line: `Input` is a line the player typed (shown with "> ").
    enum class Level { Info, Warning, Error, Input };
    /// One line of the output.
    struct Line {
        Level level = Level::Info;  ///< its kind (the window colours it)
        std::string text;           ///< the text, without a newline
    };
    using Args = std::vector<std::string>;  ///< the words after the command's name
    /// What a command does: called with its arguments and the console (to print its result, or
    /// error() a problem). An exception it throws is caught and shown as an error.
    using Handler = std::function<void(const Args& args, Console& console)>;
    /// Candidates for argument `index` (from 0) starting with `prefix`.
    using Completer = std::function<std::vector<std::string>(std::size_t index, const std::string& prefix)>;

    /// A console with its built-in commands and no variable.
    Console();

    /// The variables the console sets by name (`set`, `get`, `reset`, `toggle`, `vars`).
    ///@{
    Variables& variables() { return variables_; }
    const Variables& variables() const { return variables_; }
    ///@}

    /// Registers a command (replacing one of the same name: a scene registers its commands each time
    /// it starts). `usage`: its arguments (`<monstre> [nombre]`); `help`: one line; `completer`
    /// (optional) proposes its arguments to complete().
    void add(std::string name, std::string usage, std::string help, Kind kind, Handler handler, Completer completer = {});
    /// Unregisters the command called `name`, if there is one (built-ins included).
    void remove(std::string_view name);
    /// A command called `name` is registered.
    bool has(std::string_view name) const;

    /// A line typed by the player or read from a script: tool commands run now, game commands are
    /// queued (and so are `set`, `reset` and `toggle` of a Logic variable). Shown in the output with its result, and kept
    /// in history(). A blank line does nothing; an unknown command or an unclosed quote is an error
    /// line.
    void submit(const std::string& line);
    /// Submits every line of a text (a script: "exec file"); blank lines and lines starting with '#'
    /// are skipped. False if a line printed an error (the following lines still run); game
    /// commands are queued, so their errors come later and do not count.
    bool run_script(std::string_view text);
    /// The game commands queued since the last call (the Application takes them before each tick).
    std::vector<std::string> take_game_commands();
    /// Runs a game command line now (the Application, at the start of a tick).
    void run_game_command(const std::string& line);

    /// Appends an output line, of level Info, Warning or Error. Main thread only (else log_line()).
    ///@{
    void print(std::string text) { add_line(Level::Info, std::move(text)); }
    void warn(std::string text) { add_line(Level::Warning, std::move(text)); }
    void error(std::string text) { add_line(Level::Error, std::move(text)); }
    ///@}
    /// From any thread (the log): kept until the main thread calls flush_log().
    void log_line(Level level, std::string text);
    /// Moves the lines given to log_line() into the output (the Application calls it each frame).
    void flush_log();
    /// The output, oldest first; only the last 2000 lines are kept. Iterators are invalidated by the next
    /// line and by clear().
    const std::deque<Line>& lines() const { return lines_; }
    /// Empties the output (not the history, nor line_count()).
    void clear() { lines_.clear(); }
    /// Grows by one at each new line (the window scrolls down when it changes).
    std::uint64_t line_count() const { return line_count_; }

    /// The lines typed, oldest first.
    const std::vector<std::string>& history() const { return history_; }
    /// Candidates for the last word of `line` (command names, variable names, the command's own
    /// arguments), sorted.
    std::vector<std::string> complete(const std::string& line) const;
    /// `line` with its last word extended as far as all candidates agree.
    std::string complete_line(const std::string& line) const;

    /// The words of a line: spaces separate them, double quotes keep spaces inside one. False, with
    /// `error`, for an unclosed quote.
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
