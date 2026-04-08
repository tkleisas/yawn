#include "app/App.h"
#include "util/Logger.h"
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <exception>
#include <ctime>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#else
#include <execinfo.h>
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
    writeCrashLog(name);
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
    app->shutdown();

    LOG_INFO("App", "Y.A.W.N shutdown complete");
    return 0;
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

int main(int /*argc*/, char* /*argv*/[]) {
    initLogging();
    initCrashHandler();

    LOG_INFO("App", "Starting Y.A.W.N — Yet Another Audio Workstation New");

#ifdef _WIN32
    return runAppSEH();
#else
    return runApp();
#endif
}
