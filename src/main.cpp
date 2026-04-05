#include "app/App.h"
#include "util/Logger.h"
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <exception>

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
    std::fprintf(f, "\n========== CRASH ==========\n");
    std::fprintf(f, "Reason: %s\n", reason);

#ifdef _WIN32
    void* stack[64];
    HANDLE process = GetCurrentProcess();
    SymInitialize(process, nullptr, TRUE);
    USHORT frames = CaptureStackBackTrace(1, 64, stack, nullptr);
    std::fprintf(f, "Stack trace (%u frames):\n", frames);

    char symbolBuf[sizeof(SYMBOL_INFO) + 256];
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuf);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 255;

    for (USHORT i = 0; i < frames; ++i) {
        DWORD64 addr = reinterpret_cast<DWORD64>(stack[i]);
        if (SymFromAddr(process, addr, nullptr, symbol))
            std::fprintf(f, "  [%2u] %s (0x%llx)\n", i, symbol->Name,
                         static_cast<unsigned long long>(addr));
        else
            std::fprintf(f, "  [%2u] 0x%llx\n", i,
                         static_cast<unsigned long long>(addr));
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
    if (f != stderr) std::fclose(f);
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

static void initCrashHandler() {
    std::signal(SIGSEGV, signalHandler);
    std::signal(SIGABRT, signalHandler);
    std::signal(SIGFPE,  signalHandler);
    std::signal(SIGILL,  signalHandler);
    std::set_terminate(terminateHandler);
}

int main(int /*argc*/, char* /*argv*/[]) {
    initLogging();
    initCrashHandler();

    LOG_INFO("App", "Starting Y.A.W.N — Yet Another Audio Workstation New");

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
