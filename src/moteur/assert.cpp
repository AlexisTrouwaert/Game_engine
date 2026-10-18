#include "moteur/assert.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <thread>

#include "moteur/crash.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace moteur {

namespace {

bool debugger_attached() {
#if defined(_WIN32)
    return IsDebuggerPresent() != FALSE;
#else
    return false;  // macOS: the crash reporter's report, and the system's, show the same stack
#endif
}

// Built with the library's static objects, before main(): on the main thread.
const std::thread::id g_main_thread = std::this_thread::get_id();

}  // namespace

bool on_main_thread() {
    return std::this_thread::get_id() == g_main_thread;
}

void assertion_failed(const char* expression, const char* message, const char* file, int line) {
    char text[1024];
    std::snprintf(text, sizeof(text), "Assertion failed: %s (%s) at %s:%d", message, expression, file, line);
    SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "%s", text);
    // The crash report's context: the event line of the report says "abort", this says why.
    set_crash_context("assertion", text);
    if (debugger_attached()) {
        SDL_TriggerBreakpoint();
    }
    std::abort();
}

}  // namespace moteur
