#pragma once

/// @file
/// The engine's assertions (milestone 8, review of milestones 1 to 7, T2): the contracts of the code,
/// checked where they are written.
///
/// - MOTEUR_ASSERT(condition, message): a programming error the development builds catch (Debug and
///   Release). Compiled out of the shipping configuration (MOTEUR_SHIPPING): the condition is not
///   even evaluated there, so it must have no effect of its own.
/// - MOTEUR_VERIFY(condition, message): checked in every configuration, shipping included: what
///   protects the player's data (a save about to be written) or what would otherwise corrupt memory.
/// - MOTEUR_UNREACHABLE(message): a branch that cannot be taken; checked everywhere.
///
/// A failed check writes the message, the expression, the file and the line to the log (critical
/// level), then stops the program: on Windows under a debugger it first breaks on the line; then it
/// aborts, and the crash reporter (moteur/crash.hpp), when installed, writes its report: the event is
/// the abort, and the context line `assertion` says which check failed. An assertion is never a way
/// to handle a bad file or a bad input: those are refused with a message (exceptions at load time).
///
/// `message` is a string literal, in English (the reports are for the developers). Any thread may
/// fail a check.

namespace moteur {

/// What the macros call when a check fails: logs `Assertion failed: message (expression) at
/// file:line`, records that line as the crash context `assertion` (see set_crash_context()), breaks
/// into the debugger when one is attached (Windows only), then calls std::abort(). Never returns.
/// Called through the macros, which fill `expression`, `file` and `line`, rather than directly.
[[noreturn]] void assertion_failed(const char* expression, const char* message, const char* file, int line);

/// True on the program's main thread: the one that initialized the engine's library (its static
/// objects are built on it, before main()). Most of the engine is for that thread only (the
/// Renderer, Assets, the World, the Console...); MOTEUR_ASSERT_MAIN_THREAD() checks it.
bool on_main_thread();

}  // namespace moteur

/// Checks `condition` in every configuration, shipping included; on failure calls
/// moteur::assertion_failed(), which stops the program. A statement (`do { } while (false)`): end it
/// with a semicolon.
#define MOTEUR_VERIFY(condition, message)                                             \
    do {                                                                              \
        if (!(condition)) [[unlikely]] {                                              \
            ::moteur::assertion_failed(#condition, message, __FILE__, __LINE__);      \
        }                                                                             \
    } while (false)

/// Marks a branch that cannot be taken: reaching it stops the program in every configuration
/// (moteur::assertion_failed() with `unreachable` as the expression). The compiler knows it does not
/// return, so no value needs to follow it.
#define MOTEUR_UNREACHABLE(message) ::moteur::assertion_failed("unreachable", message, __FILE__, __LINE__)

/// Checks `condition` in the development builds (Debug and Release) like MOTEUR_VERIFY(); in the
/// shipping configuration (MOTEUR_SHIPPING) it expands to nothing and `condition` is not evaluated.
#if defined(MOTEUR_SHIPPING)
#define MOTEUR_ASSERT(condition, message) ((void)0)
#else
#define MOTEUR_ASSERT(condition, message) MOTEUR_VERIFY(condition, message)
#endif

/// Checks, in the development builds, that the caller is on the main thread (moteur::on_main_thread());
/// `what` names the service in the message ("Renderer", "Assets"...), a string literal.
#define MOTEUR_ASSERT_MAIN_THREAD(what) MOTEUR_ASSERT(::moteur::on_main_thread(), what " is for the main thread only")
