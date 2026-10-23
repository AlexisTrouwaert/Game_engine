#include "moteur/crash.hpp"

#include "moteur/assert.hpp"

#include <SDL3/SDL.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// After windows.h.
#include <dbghelp.h>
#else
#include <cerrno>
#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#endif

namespace moteur {

namespace {

// --- What the handler needs, prepared beforehand ---------------------------------------------------

constexpr int kLogLines = 200;
constexpr int kLogLineSize = 240;
constexpr int kContextSize = 4096;

struct State {
    std::atomic<bool> installed{false};
    std::atomic<bool> crashing{false};
    bool silent = false;  // MOTEUR_CRASH_SILENT: no message to the player (read at installation)
    std::string directory;  // with a trailing separator
    char title[256] = {};
    // The context lines, "key: value\n", rebuilt (under the mutex) by set_crash_context.
    std::mutex context_mutex;
    std::string context_text;
    char context[kContextSize] = {};
    // The last lines of the log, a ring written by any thread (a torn line in a report is harmless).
    char log[kLogLines][kLogLineSize] = {};
    std::atomic<unsigned> log_next{0};
    // The player's message, before and after the report's path ("{report}"), prepared at installation.
    char message_before[1024] = {};
    char message_after[1024] = {};
#if defined(_WIN32)
    wchar_t wide_directory[MAX_PATH] = {};
    wchar_t wide_title[256] = {};
    wchar_t wide_before[1024] = {};
    wchar_t wide_after[1024] = {};
#endif
    // What install_crash_reporter() replaced, given back by uninstall_crash_reporter().
    std::terminate_handler previous_terminate = nullptr;
#if defined(_WIN32)
    LPTOP_LEVEL_EXCEPTION_FILTER previous_filter = nullptr;
    void (*previous_abort)(int) = nullptr;
    unsigned int previous_abort_behavior = 0;
#else
    struct sigaction previous_actions[5] = {};
    stack_t previous_stack = {};
#endif
};

#if !defined(_WIN32)
constexpr int kSignals[5] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
#endif

// A report's stem gets this suffix ("_2", "_3"...) when a report of the same second exists.
constexpr int kMaxSameSecond = 1000;

State& state() {
    static State s;
    return s;
}

// "2026-10-05_20-48-03" from the local time: the stem of a report's files (see the writers below
// for two reports in the same second).
void stamp(char* out, std::size_t size) {
#if defined(_WIN32)
    SYSTEMTIME t;
    GetLocalTime(&t);
    std::snprintf(out, size, "%04u-%02u-%02u_%02u-%02u-%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
#else
    const time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    std::snprintf(out, size, "%04d-%02d-%02d_%02d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min,
                  t.tm_sec);
#endif
}

// --- Writing a report, the same on every OS ----------------------------------------------------------

struct Writer {
#if defined(_WIN32)
    HANDLE file = INVALID_HANDLE_VALUE;
    void write(const char* text, std::size_t size) {
        DWORD written = 0;
        WriteFile(file, text, static_cast<DWORD>(size), &written, nullptr);
    }
#else
    int file = -1;
    void write(const char* text, std::size_t size) {
        while (size > 0) {
            const ssize_t n = ::write(file, text, size);
            if (n <= 0) {
                return;
            }
            text += n;
            size -= static_cast<std::size_t>(n);
        }
    }
#endif
    void text(const char* line) { write(line, std::strlen(line)); }
    template <typename... Args>
    void format(const char* pattern, Args... args) {
        char line[512];
        const int n = std::snprintf(line, sizeof(line), pattern, args...);
        if (n > 0) {
            write(line, static_cast<std::size_t>(n < static_cast<int>(sizeof(line)) ? n : static_cast<int>(sizeof(line)) - 1));
        }
    }
};

void write_header(Writer& out, const char* what) {
    State& s = state();
    out.text("Crash report\n");
    out.format("program: %s\n", s.title);
    out.text(s.context);
    out.format("event: %s\n", what);
}

void write_log(Writer& out) {
    State& s = state();
    out.text("\n--- log (last lines)\n");
    const unsigned next = s.log_next.load(std::memory_order_relaxed);
    const unsigned first = next > kLogLines ? next - kLogLines : 0;
    for (unsigned i = first; i < next; ++i) {
        const char* line = s.log[i % kLogLines];
        out.write(line, strnlen(line, kLogLineSize));
        out.text("\n");
    }
}

#if defined(_WIN32)

void write_stack(Writer& out, CONTEXT context) {
    out.text("\n--- stack (module+offset; function when the symbols are there)\n");
    const HANDLE process = GetCurrentProcess();
    const HANDLE thread = GetCurrentThread();
    STACKFRAME64 frame = {};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
    for (int depth = 0; depth < 64; ++depth) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context, nullptr, SymFunctionTableAccess64,
                         SymGetModuleBase64, nullptr) ||
            frame.AddrPC.Offset == 0) {
            break;
        }
        const DWORD64 address = frame.AddrPC.Offset;
        HMODULE module = nullptr;
        char module_name[MAX_PATH] = "?";
        DWORD64 offset = address;
        // FROM_ADDRESS: the API takes the code address in its name parameter, as a pointer.
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(address), &module) &&  // NOLINT(performance-no-int-to-ptr): required by the API
            module != nullptr) {
            char path[MAX_PATH] = {};
            GetModuleFileNameA(module, path, MAX_PATH);
            const char* slash = std::strrchr(path, '\\');
            std::snprintf(module_name, sizeof(module_name), "%s", slash != nullptr ? slash + 1 : path);
            offset = address - reinterpret_cast<DWORD64>(module);
        }
        alignas(SYMBOL_INFO) char symbol_buffer[sizeof(SYMBOL_INFO) + 256] = {};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbol_buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        DWORD64 displacement = 0;
        if (SymFromAddr(process, address, &displacement, symbol)) {
            IMAGEHLP_LINE64 line = {};
            line.SizeOfStruct = sizeof(line);
            DWORD line_displacement = 0;
            if (SymGetLineFromAddr64(process, address, &line_displacement, &line)) {
                out.format("#%02d %s+0x%llx %s (%s:%lu)\n", depth, module_name, static_cast<unsigned long long>(offset),
                           symbol->Name, line.FileName, line.LineNumber);
            } else {
                out.format("#%02d %s+0x%llx %s+0x%llx\n", depth, module_name, static_cast<unsigned long long>(offset),
                           symbol->Name, static_cast<unsigned long long>(displacement));
            }
        } else {
            out.format("#%02d %s+0x%llx\n", depth, module_name, static_cast<unsigned long long>(offset));
        }
    }
}

const char* exception_name(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION: return "access violation";
        case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
        case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer division by zero";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
        case 0xE06D7363: return "uncaught C++ exception";
        case 0xE0000001: return "stop (abort or terminate)";
        default: return "exception";
    }
}

// Creates the .txt and the .dmp of a report under a stem no other report has: the date and time,
// then "_2", "_3"... when a report of the same second exists (a GPU loss, then a crash while
// ending). CREATE_NEW: an existing report is never overwritten. The .txt's path (wide) goes to
// `report_path`; false (and `text` invalid) when the .txt cannot be created. A .dmp that cannot be
// created leaves `dump` invalid: the .txt is written all the same.
bool create_report_files(HANDLE& text, HANDLE& dump, wchar_t* report_path, std::size_t report_size) {
    const State& s = state();
    char name[64];
    stamp(name, sizeof(name));
    wchar_t wide_name[64];
    MultiByteToWideChar(CP_UTF8, 0, name, -1, wide_name, 64);
    text = INVALID_HANDLE_VALUE;
    dump = INVALID_HANDLE_VALUE;
    for (int n = 1; n <= kMaxSameSecond; ++n) {
        wchar_t stem[MAX_PATH];
        if (n == 1) {
            _snwprintf_s(stem, MAX_PATH, _TRUNCATE, L"%s%s", s.wide_directory, wide_name);
        } else {
            _snwprintf_s(stem, MAX_PATH, _TRUNCATE, L"%s%s_%d", s.wide_directory, wide_name, n);
        }
        _snwprintf_s(report_path, report_size, _TRUNCATE, L"%s.txt", stem);
        text = CreateFileW(report_path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (text == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_FILE_EXISTS) {
                continue;
            }
            return false;  // the folder cannot be written
        }
        wchar_t dump_path[MAX_PATH];
        _snwprintf_s(dump_path, MAX_PATH, _TRUNCATE, L"%s.dmp", stem);
        dump = CreateFileW(dump_path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (dump == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_EXISTS) {
            // A .dmp without its .txt (another report that could not write it): the stem is taken.
            CloseHandle(text);
            text = INVALID_HANDLE_VALUE;
            DeleteFileW(report_path);
            continue;
        }
        return true;
    }
    return false;
}

// Writes the .dmp and the .txt; returns the .txt's path (wide) for the message.
void write_report(EXCEPTION_POINTERS* pointers, const char* what, wchar_t* report_path, std::size_t report_size) {
    HANDLE text = INVALID_HANDLE_VALUE;
    HANDLE dump = INVALID_HANDLE_VALUE;
    if (!create_report_files(text, dump, report_path, report_size)) {
        return;
    }
    if (dump != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION info = {};
        info.ThreadId = GetCurrentThreadId();
        info.ExceptionPointers = pointers;
        info.ClientPointers = FALSE;
        // Small: the threads, their stacks and the modules; not the whole memory (hundreds of MB).
        const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump, type, pointers != nullptr ? &info : nullptr,
                          nullptr, nullptr);
        CloseHandle(dump);
    }

    Writer out;
    out.file = text;
    write_header(out, what);
    CONTEXT context = {};
    if (pointers != nullptr) {
        const EXCEPTION_RECORD* record = pointers->ExceptionRecord;
        out.format("code: 0x%08lx (%s)\n", static_cast<unsigned long>(record->ExceptionCode), exception_name(record->ExceptionCode));
        out.format("address: 0x%llx\n", static_cast<unsigned long long>(reinterpret_cast<DWORD64>(record->ExceptionAddress)));
        if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
            out.format("access: %s at 0x%llx\n", record->ExceptionInformation[0] == 0 ? "read" : "write",
                       static_cast<unsigned long long>(record->ExceptionInformation[1]));
        }
        context = *pointers->ContextRecord;
    } else {
        RtlCaptureContext(&context);
    }
    // Every loaded module with its base: what the offline script needs to read the stack.
    write_stack(out, context);
    write_log(out);
    CloseHandle(out.file);
}

void tell_player(const wchar_t* report_path) {
    if (state().silent) {
        return;  // the automated checks of the chain: nobody to click
    }
    const State& s = state();
    wchar_t message[2600];
    _snwprintf_s(message, 2600, _TRUNCATE, L"%s%s%s", s.wide_before, report_path, s.wide_after);
    MessageBoxW(nullptr, message, s.wide_title, MB_OK | MB_ICONERROR | MB_TOPMOST);
}

LONG WINAPI on_exception(EXCEPTION_POINTERS* pointers) {
    if (state().crashing.exchange(true)) {
        return EXCEPTION_CONTINUE_SEARCH;  // a crash inside the handler: let the system do it
    }
    wchar_t report[MAX_PATH] = {};
    write_report(pointers, "crash", report, MAX_PATH);
    tell_player(report);
    return EXCEPTION_EXECUTE_HANDLER;  // the process ends
}

#else  // macOS, Linux

void write_stack(Writer& out) {
    out.text("\n--- stack (module+offset; atos -o <symbols> -l <base> <address>)\n");
    void* frames[64];
    const int count = backtrace(frames, 64);
    for (int i = 0; i < count; ++i) {
        Dl_info info = {};
        if (dladdr(frames[i], &info) != 0 && info.dli_fname != nullptr) {
            const char* slash = std::strrchr(info.dli_fname, '/');
            const auto base = reinterpret_cast<unsigned long long>(info.dli_fbase);
            const auto address = reinterpret_cast<unsigned long long>(frames[i]);
            out.format("#%02d %s+0x%llx (base 0x%llx, address 0x%llx) %s\n", i, slash != nullptr ? slash + 1 : info.dli_fname,
                       address - base, base, address, info.dli_sname != nullptr ? info.dli_sname : "");
        } else {
            out.format("#%02d ? 0x%llx\n", i, reinterpret_cast<unsigned long long>(frames[i]));
        }
    }
}

// Writes the report into a file no other report has, its path into `path` (its size:
// `path_size`): the date and time, then "_2", "_3"... when a report of the same second exists (a
// GPU loss, then a crash while ending). O_EXCL: an existing report is never overwritten. False if
// the file cannot be created.
bool write_report_file(const char* what, char* path, std::size_t path_size) {
    const State& s = state();
    char name[64];
    stamp(name, sizeof(name));
    Writer out;
    for (int n = 1; n <= kMaxSameSecond && out.file < 0; ++n) {
        if (n == 1) {
            std::snprintf(path, path_size, "%s%s.txt", s.directory.c_str(), name);
        } else {
            std::snprintf(path, path_size, "%s%s_%d.txt", s.directory.c_str(), name, n);
        }
        out.file = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
        if (out.file < 0 && errno != EEXIST) {
            return false;  // the folder cannot be written
        }
    }
    if (out.file < 0) {
        return false;
    }
    write_header(out, what);
    write_stack(out);
    write_log(out);
    close(out.file);
    return true;
}

void write_report(const char* what) {
    State& s = state();
    char path[1024];
    if (!write_report_file(what, path, sizeof(path))) {
        return;
    }
    char message[3200];
    std::snprintf(message, sizeof(message), "%s%s%s", s.message_before, path, s.message_after);
    // Not async-signal-safe: the last thing tried, once the report is on disk.
    if (state().silent) {
        return;
    }
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, s.title, message, nullptr);
}

void on_signal(int signal, siginfo_t* info, void*) {
    if (!state().crashing.exchange(true)) {
        char what[96];
        std::snprintf(what, sizeof(what), "signal %d (%s) at 0x%llx", signal,
                      signal == SIGSEGV ? "SIGSEGV" : signal == SIGBUS ? "SIGBUS" : signal == SIGABRT ? "SIGABRT"
                                                    : signal == SIGILL ? "SIGILL" : signal == SIGFPE ? "SIGFPE" : "?",
                      reinterpret_cast<unsigned long long>(info != nullptr ? info->si_addr : nullptr));
        write_report(what);
    }
    // SA_RESETHAND gave the default action back: the system writes its own report and ends us.
    raise(signal);
}

#endif

void report_and_end(const char* default_what);

void on_terminate() {
    report_and_end("std::terminate");
}

#if defined(_WIN32)
void on_abort(int) {
    report_and_end("abort (assertion, or an error of the C library)");
}
#endif

void report_and_end(const char* default_what) {
    char what[400];
    std::snprintf(what, sizeof(what), "%s", default_what);
    if (const std::exception_ptr current = std::current_exception()) {
        try {
            std::rethrow_exception(current);
        } catch (const std::exception& e) {
            std::snprintf(what, sizeof(what), "uncaught C++ exception: %s", e.what());
        } catch (...) {
            std::snprintf(what, sizeof(what), "uncaught C++ exception (unknown type)");
        }
    }
#if defined(_WIN32)
    if (!state().crashing.exchange(true)) {
        wchar_t report[MAX_PATH] = {};
        write_report(nullptr, what, report, MAX_PATH);
        tell_player(report);
    }
    TerminateProcess(GetCurrentProcess(), 3);
#else
    if (!state().crashing.exchange(true)) {
        write_report(what);
    }
    signal(SIGABRT, SIG_DFL);
    std::abort();
#endif
}

}  // namespace

namespace {

// The player's message, cut around `{report}`, prepared for the handlers (no allocation there).
void store_message(State& s, const std::string& player_message) {
    const std::string message = player_message.empty() ? kDefaultCrashMessage : player_message;
    const std::size_t at = message.find("{report}");
    std::snprintf(s.message_before, sizeof(s.message_before), "%s", message.substr(0, at).c_str());
    std::snprintf(s.message_after, sizeof(s.message_after), "%s",
                  at == std::string::npos ? "" : message.substr(at + std::string_view("{report}").size()).c_str());
#if defined(_WIN32)
    MultiByteToWideChar(CP_UTF8, 0, s.message_before, -1, s.wide_before, 1024);
    MultiByteToWideChar(CP_UTF8, 0, s.message_after, -1, s.wide_after, 1024);
#endif
}

}  // namespace

void set_crash_message(const std::string& player_message) {
    State& s = state();
    if (s.installed.load()) {
        store_message(s, player_message);
    }
}

void install_crash_reporter(const std::string& directory, const std::string& program_title,
                            const std::string& player_message) {
    State& s = state();
    if (s.installed.exchange(true)) {
        return;
    }
    s.directory = directory;
    if (!s.directory.empty() && s.directory.back() != '/' && s.directory.back() != '\\') {
        s.directory += '/';
    }
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(std::u8string(s.directory.begin(), s.directory.end())), error);
    std::snprintf(s.title, sizeof(s.title), "%s", program_title.c_str());
    store_message(s, player_message);
    s.silent = SDL_getenv("MOTEUR_CRASH_SILENT") != nullptr;
    s.crashing = false;
    s.previous_terminate = std::set_terminate(on_terminate);
#if defined(_WIN32)
    MultiByteToWideChar(CP_UTF8, 0, s.directory.c_str(), -1, s.wide_directory, MAX_PATH);
    MultiByteToWideChar(CP_UTF8, 0, s.title, -1, s.wide_title, 256);
    // The symbols of the program's folder, when they are there (development): names in the reports.
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    SymInitialize(GetCurrentProcess(), nullptr, TRUE);
    s.previous_filter = SetUnhandledExceptionFilter(on_exception);
    // abort() (and a failed assert): through the same report.
    s.previous_abort = signal(SIGABRT, on_abort);
    // Errors of the C runtime (a bad parameter) would otherwise show their own dialog.
    s.previous_abort_behavior = _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#else
    static char alternate_stack[64 * 1024];
    stack_t stack = {};
    stack.ss_sp = alternate_stack;
    stack.ss_size = sizeof(alternate_stack);
    sigaltstack(&stack, &s.previous_stack);
    struct sigaction action = {};
    action.sa_sigaction = on_signal;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    sigemptyset(&action.sa_mask);
    for (int i = 0; i < 5; ++i) {
        sigaction(kSignals[i], &action, &s.previous_actions[i]);
    }
    void* dummy[1];
    backtrace(dummy, 1);  // loads what backtrace() needs now, not in the handler
#endif
    SDL_Log("Crash reports: '%s'", s.directory.c_str());
}

void uninstall_crash_reporter() {
    State& s = state();
    if (!s.installed.load()) {
        return;
    }
    std::set_terminate(s.previous_terminate);
#if defined(_WIN32)
    SetUnhandledExceptionFilter(s.previous_filter);
    signal(SIGABRT, s.previous_abort != SIG_ERR ? s.previous_abort : SIG_DFL);
    _set_abort_behavior(s.previous_abort_behavior, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    SymCleanup(GetCurrentProcess());
#else
    for (int i = 0; i < 5; ++i) {
        sigaction(kSignals[i], &s.previous_actions[i], nullptr);
    }
    if ((s.previous_stack.ss_flags & SS_DISABLE) != 0) {
        stack_t none = {};
        none.ss_flags = SS_DISABLE;
        sigaltstack(&none, nullptr);
    } else {
        sigaltstack(&s.previous_stack, nullptr);
    }
#endif
    s.installed = false;  // last: a report_fatal_error() meanwhile still finds everything in place
}

void set_crash_context(const std::string& key, const std::string& value) {
    State& s = state();
    const std::lock_guard lock(s.context_mutex);
    const std::string prefix = key + ": ";
    std::string text;
    bool replaced = false;
    std::size_t start = 0;
    while (start < s.context_text.size()) {
        const std::size_t end = s.context_text.find('\n', start);
        const std::string line = s.context_text.substr(start, end - start);
        if (line.rfind(prefix, 0) == 0) {
            text.append(prefix).append(value) += '\n';
            replaced = true;
        } else {
            text.append(line) += '\n';
        }
        start = end + 1;
    }
    if (!replaced) {
        text.append(prefix).append(value) += '\n';
    }
    s.context_text = text;
    std::snprintf(s.context, sizeof(s.context), "%s", text.c_str());
}

void record_crash_log_line(const char* line) {
    State& s = state();
    const unsigned index = s.log_next.fetch_add(1, std::memory_order_relaxed) % kLogLines;
    std::snprintf(s.log[index], kLogLineSize, "%s", line);
}

void report_fatal_error(const std::string& what, const std::string& player_message) {
    SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "Fatal error: %s", what.c_str());
    State& s = state();
    if (!s.installed || s.crashing.exchange(true)) {
        return;
    }
    const std::string message = player_message.empty() ? kDefaultGpuLostMessage : player_message;
    const std::size_t at = message.find("{report}");
    const std::string before = message.substr(0, at);
    const std::string after = at == std::string::npos ? std::string() : message.substr(at + std::string_view("{report}").size());
#if defined(_WIN32)
    wchar_t report[MAX_PATH] = {};
    write_report(nullptr, what.c_str(), report, MAX_PATH);
    if (!s.silent) {
        const auto wide = [](const std::string& text) {
            std::wstring out(static_cast<std::size_t>(MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0)), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), static_cast<int>(out.size()));
            out.resize(out.empty() ? 0 : out.size() - 1);
            return out;
        };
        const std::wstring text = wide(before) + report + wide(after);
        MessageBoxW(nullptr, text.c_str(), s.wide_title, MB_OK | MB_ICONERROR | MB_TOPMOST);
    }
#else
    char path[1024];
    if (write_report_file(what.c_str(), path, sizeof(path)) && !s.silent) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, s.title, (before + path + after).c_str(), nullptr);
    }
#endif
    s.crashing = false;  // a real crash afterwards (while ending) is still reported
}

std::string crash_reports_directory() {
    return state().installed ? state().directory : std::string();
}

void crash_now(CrashKind kind) {
    SDL_Log("Crash: provoked on purpose (%s)", kind == CrashKind::AccessViolation ? "access violation"
                                               : kind == CrashKind::Abort         ? "abort"
                                               : kind == CrashKind::Assertion     ? "failed assertion"
                                                                                  : "uncaught exception");
    switch (kind) {
        case CrashKind::AccessViolation: {
            volatile int* nowhere = nullptr;
            *nowhere = 42;  // NOLINT(clang-analyzer-core.NullDereference): the access violation is the purpose
            break;
        }
        case CrashKind::Abort:
            std::abort();
        case CrashKind::Assertion:
            MOTEUR_VERIFY(kind != CrashKind::Assertion, "assertion failed on purpose to check the crash reports");
            break;
        case CrashKind::UncaughtException:
            // An exception escaping a thread's function: std::terminate, with the exception current.
            std::thread([] { throw std::runtime_error("exception provoked to check the crash reports"); }).join();
            break;
    }
    std::abort();
}

}  // namespace moteur
