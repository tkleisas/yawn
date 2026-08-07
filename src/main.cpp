// Windows headers FIRST — App.h transitively pulls ASIO (via Ableton
// Link), which configures the Windows SDK in a way that hides
// RtlCaptureStackBackTrace from a later windows.h include. Pin
// _WIN32_WINNT to Win7 and pull windows.h + dbghelp explicitly so the
// crash-handler symbols below are always declared.
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
// NOMINMAX before windows.h so the std::min / std::max calls in
// transitively-included headers (TextRasterizer, etc.) aren't
// hijacked by windows.h's all-caps macros.
#ifndef NOMINMAX
#define NOMINMAX
#endif
// WIN32_LEAN_AND_MEAN skips the legacy WinSock include from
// windows.h — ASIO (pulled in via Link/App.h) needs WinSock2 and
// errors out if WinSock is already in scope.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

#include "app/App.h"
#include "util/Logger.h"
#include "presets/PresetGenerator.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <exception>
#include <ctime>
#include <string>
#include <vector>

#ifndef _WIN32
#include <execinfo.h>
#include <unistd.h>   // write, STDERR_FILENO
#include <fcntl.h>    // open
#endif

// Redirect stdout/stderr to a log file for debugging
static FILE* g_logFile = nullptr;

static void initLogging() {
    g_logFile = std::fopen("yawn.log", "w");
    if (g_logFile) {
        std::freopen("yawn.log", "w", stdout);
        std::freopen("yawn.log", "a", stderr);
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        std::setvbuf(stderr, nullptr, _IONBF, 0);
    }
}

// ─── Crash handler ──────────────────────────────────────────────────────────

#ifndef _WIN32
// Log fd opened once at startup so the signal handler never needs the
// malloc-backed fopen.
static int g_crashLogFd = -1;

// Async-signal-safe crash writer. A SIGSEGV from heap corruption very
// often fires *inside* malloc with the arena lock held; fopen / fprintf /
// backtrace_symbols all allocate and would re-take that lock and
// self-deadlock — which is why the app HUNG ("not responding") instead of
// crashing. Use only write(2) + backtrace_symbols_fd, both async-signal-
// safe, so a crash always terminates cleanly and leaves a log.
static void writeCrashSafe(const char* reason) {
    const int fd = (g_crashLogFd >= 0) ? g_crashLogFd : STDERR_FILENO;
    auto put = [fd](const char* s) {
        if (!s) return;
        size_t n = std::strlen(s);
        while (n > 0) {
            ssize_t w = ::write(fd, s, n);
            if (w <= 0) break;
            s += w; n -= static_cast<size_t>(w);
        }
    };
    put("\n========== CRASH (signal) ==========\n");
    put("Reason: "); put(reason); put("\n");
    put("Stack trace:\n");
    void* stack[64];
    int frames = backtrace(stack, 64);
    backtrace_symbols_fd(stack, frames, fd);  // async-signal-safe, no malloc
    put("====================================\n");
}
#endif

static void writeCrashLog(const char* reason) {
    FILE* f = std::fopen("yawn.log", "a");
    if (!f) f = stderr;

    // Timestamp
    std::time_t now = std::time(nullptr);
    char timeBuf[64];
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));

    std::fprintf(f, "\n========== CRASH ==========\n");
    std::fprintf(f, "Time: %s\n", timeBuf);
    std::fprintf(f, "Reason: %s\n", reason);

#ifdef _WIN32
    // Thread info
    std::fprintf(f, "Thread ID: %lu\n", GetCurrentThreadId());

    void* stack[64];
    HANDLE process = GetCurrentProcess();
    SymInitialize(process, nullptr, TRUE);
    USHORT frames = CaptureStackBackTrace(1, 64, stack, nullptr);
    std::fprintf(f, "Stack trace (%u frames):\n", frames);

    char symbolBuf[sizeof(SYMBOL_INFO) + 256];
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuf);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 255;

    IMAGEHLP_LINE64 line;
    line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

    for (USHORT i = 0; i < frames; ++i) {
        DWORD64 addr = reinterpret_cast<DWORD64>(stack[i]);
        DWORD displacement = 0;
        if (SymFromAddr(process, addr, nullptr, symbol)) {
            if (SymGetLineFromAddr64(process, addr, &displacement, &line))
                std::fprintf(f, "  [%2u] %s (%s:%lu) (0x%llx)\n", i, symbol->Name,
                             line.FileName, line.LineNumber,
                             static_cast<unsigned long long>(addr));
            else
                std::fprintf(f, "  [%2u] %s (0x%llx)\n", i, symbol->Name,
                             static_cast<unsigned long long>(addr));
        } else {
            std::fprintf(f, "  [%2u] 0x%llx\n", i,
                         static_cast<unsigned long long>(addr));
        }
    }
#else
    void* stack[64];
    int frames = backtrace(stack, 64);
    std::fprintf(f, "Stack trace (%d frames):\n", frames);
    char** symbols = backtrace_symbols(stack, frames);
    if (symbols) {
        for (int i = 0; i < frames; ++i)
            std::fprintf(f, "  [%2d] %s\n", i, symbols[i]);
        free(symbols);
    }
#endif

    std::fprintf(f, "===========================\n");
    std::fflush(f);
    if (f != stderr) std::fclose(f);

    // Also flush stdout/stderr in case log was redirected
    std::fflush(stdout);
    std::fflush(stderr);
}

static void signalHandler(int sig) {
    const char* name = "Unknown signal";
    switch (sig) {
        case SIGSEGV: name = "SIGSEGV (Segmentation fault)"; break;
        case SIGABRT: name = "SIGABRT (Abort)"; break;
        case SIGFPE:  name = "SIGFPE (Floating point exception)"; break;
        case SIGILL:  name = "SIGILL (Illegal instruction)"; break;
        default: break;
    }
    // Async-signal-safe path on POSIX (writeCrashLog's stdio/malloc can
    // deadlock when the signal interrupted malloc — see writeCrashSafe).
#ifdef _WIN32
    writeCrashLog(name);
#else
    writeCrashSafe(name);
#endif
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

static void terminateHandler() {
    const char* reason = "std::terminate called";
    try {
        auto eptr = std::current_exception();
        if (eptr) std::rethrow_exception(eptr);
    } catch (const std::exception& e) {
        char buf[512];
        std::snprintf(buf, sizeof(buf), "Uncaught exception: %s", e.what());
        writeCrashLog(buf);
        std::abort();
    } catch (...) {
        reason = "Uncaught unknown exception";
    }
    writeCrashLog(reason);
    std::abort();
}

#ifdef _WIN32
// Windows Structured Exception Handler — catches access violations,
// stack overflows, heap corruption, and other OS-level crashes that
// C signals don't cover.
static LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS* exInfo) {
    const char* desc = "Unknown Windows exception";
    char buf[256];
    DWORD code = exInfo ? exInfo->ExceptionRecord->ExceptionCode : 0;

    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:      desc = "Access violation"; break;
        case EXCEPTION_STACK_OVERFLOW:         desc = "Stack overflow"; break;
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:  desc = "Array bounds exceeded"; break;
        case EXCEPTION_DATATYPE_MISALIGNMENT:  desc = "Data misalignment"; break;
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:     desc = "Float divide by zero"; break;
        case EXCEPTION_FLT_OVERFLOW:           desc = "Float overflow"; break;
        case EXCEPTION_FLT_UNDERFLOW:          desc = "Float underflow"; break;
        case EXCEPTION_INT_DIVIDE_BY_ZERO:     desc = "Integer divide by zero"; break;
        case EXCEPTION_INT_OVERFLOW:           desc = "Integer overflow"; break;
        case EXCEPTION_ILLEGAL_INSTRUCTION:    desc = "Illegal instruction"; break;
        case EXCEPTION_IN_PAGE_ERROR:          desc = "Page fault (I/O error)"; break;
        case EXCEPTION_GUARD_PAGE:             desc = "Guard page violation"; break;
        case EXCEPTION_INVALID_HANDLE:         desc = "Invalid handle"; break;
        case STATUS_HEAP_CORRUPTION:           desc = "Heap corruption"; break;
        default:
            std::snprintf(buf, sizeof(buf), "Windows exception 0x%08lX", code);
            desc = buf;
            break;
    }

    // For access violations, log the faulting address
    char reasonBuf[512];
    if (code == EXCEPTION_ACCESS_VIOLATION && exInfo->ExceptionRecord->NumberParameters >= 2) {
        const char* op = exInfo->ExceptionRecord->ExceptionInformation[0] == 0 ? "reading" : "writing";
        std::snprintf(reasonBuf, sizeof(reasonBuf), "%s (%s address 0x%llx)", desc, op,
                      static_cast<unsigned long long>(exInfo->ExceptionRecord->ExceptionInformation[1]));
        writeCrashLog(reasonBuf);
    } else {
        writeCrashLog(desc);
    }

    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

static void initCrashHandler() {
#ifndef _WIN32
    // Pre-open the crash log so the (async-signal-safe) signal handler can
    // write to it with write(2) without touching malloc/stdio.
    g_crashLogFd = ::open("yawn.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
#endif
    std::signal(SIGSEGV, signalHandler);
    std::signal(SIGABRT, signalHandler);
    std::signal(SIGFPE,  signalHandler);
    std::signal(SIGILL,  signalHandler);
    std::set_terminate(terminateHandler);

#ifdef _WIN32
    SetUnhandledExceptionFilter(unhandledExceptionFilter);
#endif
}

// Separated so main() can use __try/__except (MSVC forbids mixing SEH with C++ unwinding)
static int runApp() {
    auto app = std::make_unique<yawn::App>();

    if (!app->init()) {
        LOG_ERROR("App", "Failed to initialize application");
        return 1;
    }

    app->run();
    const int code = app->exitCode();
    app->shutdown();

    LOG_INFO("App", "Y.A.W.N shutdown complete");
    return code;
}

#ifdef _WIN32
// SEH wrapper — must be in a function with no C++ objects requiring unwinding
static int runAppSEH() {
    __try {
        return runApp();
    } __except(unhandledExceptionFilter(GetExceptionInformation())) {
        return 1;
    }
}
#endif

// ─── Headless preset-generation CLI ──────────────────────────────────────────
// `YAWN --gen-presets [--seed N] [--alien 0..1] [--no-validate]
//                     [--device <id>] [--count N]`
// Generates procedural presets into the global preset library and exits
// without opening the GUI. Output goes to the console (logging is not
// redirected to yawn.log in this mode).
static int runGenPresets(int argc, char* argv[]) {
    using namespace yawn::presets;
    GenOptions opt;
    std::string onlyDevice;
    int overrideCount = -1;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* def) -> std::string {
            return (i + 1 < argc) ? std::string(argv[++i]) : std::string(def);
        };
        if      (a == "--seed")        opt.masterSeed = std::strtoull(next("0").c_str(), nullptr, 10);
        else if (a == "--alien")       opt.alienNameRatio = std::strtof(next("0.5").c_str(), nullptr);
        else if (a == "--no-validate") opt.validate = false;
        else if (a == "--device")      onlyDevice = next("");
        else if (a == "--count")       overrideCount = std::atoi(next("0").c_str());
    }

    PresetGenerator gen(opt);
    std::vector<GenSpec> catalog = PresetGenerator::defaultCatalog();
    if (!onlyDevice.empty()) {
        std::vector<GenSpec> filtered;
        for (auto& s : catalog)
            if (s.deviceId == onlyDevice) {
                if (overrideCount > 0) s.count = overrideCount;
                filtered.push_back(s);
            }
        if (filtered.empty() && PresetGenerator::isSupported(onlyDevice))
            filtered.push_back({onlyDevice, PresetGenerator::kindOf(onlyDevice),
                                overrideCount > 0 ? overrideCount : 10});
        catalog = filtered;
    } else if (overrideCount > 0) {
        for (auto& s : catalog) s.count = overrideCount;
    }

    std::fprintf(stderr,
                 "YAWN preset generator — seed=%llu alien=%.2f validate=%d devices=%zu\n",
                 static_cast<unsigned long long>(gen.options().masterSeed),
                 gen.options().alienNameRatio, static_cast<int>(gen.options().validate),
                 catalog.size());

    std::vector<GeneratedPreset> results = gen.generateBatch(catalog);

    int valid = 0;
    for (const auto& r : results) if (r.valid) ++valid;
    std::fprintf(stderr, "Generated %zu presets (%d valid). Manifest: %s\n",
                 results.size(), valid,
                 PresetGenerator::manifestPath().string().c_str());
    int shown = 0;
    for (const auto& r : results) {
        if (shown++ >= 50) break;
        std::fprintf(stderr, "  [%-13s] %s%s\n", r.deviceId.c_str(),
                     r.name.c_str(), r.valid ? "" : "  (unvalidated)");
    }
    return 0;
}

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--gen-presets") == 0)
            return runGenPresets(argc, argv);
    }

    initLogging();
    initCrashHandler();

    LOG_INFO("App", "Starting Y.A.W.N — Yet Another Audio Workstation New");

#ifdef _WIN32
    return runAppSEH();
#else
    return runApp();
#endif
}
