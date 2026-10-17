#pragma once

#include <string>

namespace moteur {

/// Crash reports (milestone 8, part 8). Once installed, a crash of the program (an access violation,
/// an abort, a failed check of moteur/assert.hpp, a C++ exception nobody caught) writes into
/// `directory` (created if needed), under a name made of the local date and time
/// (`2026-10-05_20-48-03`; `2026-10-05_20-48-03_2`, `_3`... for the next reports of the same second,
/// a GPU loss then a crash while ending: a report never overwrites another):
/// - a `.txt` report: the engine's and the game's versions, the OS, the GPU and its driver, the
///   memory, what happened (code, address), the stack of the thread that crashed as module +
///   offset (and function names when the symbols are next to the program), the last 200 lines of
///   the log;
/// - on Windows, a `.dmp` of the same name: a small minidump (threads, modules), for a debugger;
///
/// then tells the player where the report is, in a message box titled `program_title` (not when the
/// environment variable MOTEUR_CRASH_SILENT is set: automated checks), and lets the program end. On
/// macOS the system also writes its own report (~/Library/Logs/DiagnosticReports).
///
/// The report is for the developers: in English, whatever the player's language. The message shown
/// to the player is the game's (`player_message`, in the player's language), `{report}` standing for
/// the report's path; empty: kDefaultCrashMessage.
///
/// The shipped program has no symbols: tools/crash/read_report.py turns the module + offset lines
/// into function, file and line with the symbols archived for that version (PDB, dSYM).
///
/// The handler runs in a broken process: everything it can prepare is prepared at installation
/// (paths, the context lines, the message), and it does as little as it can. Installed once (later
/// calls do nothing until uninstall_crash_reporter()), from the main thread, at the start:
/// Application does it when ApplicationConfig::crash_reports is set, in its preferences'
/// `crash_reports/` folder. It replaces the std::terminate handler and, on Windows, the unhandled
/// exception filter and the SIGABRT handler; elsewhere, the handlers of SIGSEGV, SIGBUS, SIGILL,
/// SIGFPE and SIGABRT (on an alternate signal stack).
void install_crash_reporter(const std::string& directory, const std::string& program_title,
                            const std::string& player_message = {});

/// Undoes install_crash_reporter(): gives back the handlers it replaced, as they were when it was
/// installed (the std::terminate handler; on Windows the unhandled exception filter, the SIGABRT
/// handler and the C runtime's abort behavior; elsewhere the five signal handlers and the alternate
/// signal stack). From then on crashes are no longer reported, report_fatal_error() only logs,
/// crash_reports_directory() is empty, and install_crash_reporter() can install it again (the
/// context lines and the log lines are kept). Nothing when it is not installed. Main thread; for
/// the one who installed it (the Application, when its constructor fails and in its destructor).
/// Handlers installed by someone else after it are replaced by the earlier ones.
void uninstall_crash_reporter();

/// The message shown to the player after a crash when the game gives none: English, with
/// `{report}` where the report's path goes.
inline constexpr const char* kDefaultCrashMessage =
    "The game ran into an error and has to close.\n\nA report was written here:\n{report}\n\n"
    "Attaching this file (and the .dmp next to it) to a bug report helps fix the problem.";

/// The message shown to the player when the GPU is lost and the game gives none (see
/// Application: ApplicationConfig::gpu_lost_message); `{report}` where the report's path goes.
inline constexpr const char* kDefaultGpuLostMessage =
    "The graphics card stopped responding (its driver was reset, or the card was removed), and the game "
    "has to close.\n\nUpdating the graphics driver usually fixes it.\n\nA report was written here:\n{report}";

/// A fatal error that is not a crash (the GPU device lost, milestone 8 part 8): logs `what` as a
/// critical line, then writes the same report as a crash (on Windows a minidump too) with `what` as
/// its event and the stack of the calling thread, then shows `player_message` to the player
/// (`{report}` for the report's path; empty: kDefaultGpuLostMessage; not when MOTEUR_CRASH_SILENT
/// is set), blocking until the player closes it. Returns: the caller then ends the program cleanly.
/// Without an installed reporter, only the log line.
void report_fatal_error(const std::string& what, const std::string& player_message);

/// A line of context for the reports (`key` "gpu", `value` "direct3d12, NVIDIA GeForce RTX 4070 Ti
/// SUPER"), written as `key: value` and kept in a buffer prepared for the handler (4 KiB for all
/// the lines; what goes past it is cut). The same key replaces its line. Thread-safe; callable
/// before install_crash_reporter() (the lines are kept for it).
void set_crash_context(const std::string& key, const std::string& value);

/// Keeps `line` among the last 200 lines that go into a report (each cut at 239 bytes). LogFile
/// calls it for every line of the log; any thread, without a lock (a line written during a crash
/// may come out torn).
void record_crash_log_line(const char* line);

/// What crash_now() provokes: `AccessViolation`, a write through a null pointer; `Abort`,
/// std::abort(); `UncaughtException`, an exception escaping a thread's function (std::terminate);
/// `Assertion`, a failed MOTEUR_VERIFY().
enum class CrashKind { AccessViolation, Abort, UncaughtException, Assertion };
/// For the checks of the chain (console command `crash`, the slice's `--test-plantage`): logs what
/// it provokes, then crashes now, through the crash reporter if it is installed. Never returns.
[[noreturn]] void crash_now(CrashKind kind);

/// Where the reports go once installed, with a trailing separator (empty before).
std::string crash_reports_directory();

}  // namespace moteur
