#include "app/App.h"
#include <cstdio>
#include <cstdlib>

// Redirect stdout/stderr to a log file for debugging
static FILE* g_logFile = nullptr;

static void initLogging() {
    g_logFile = std::fopen("yawn.log", "w");
    if (g_logFile) {
        // Duplicate log to file — we'll use freopen to redirect stderr/stdout
        std::freopen("yawn.log", "w", stdout);
        std::freopen("yawn.log", "a", stderr);
        std::setvbuf(stdout, nullptr, _IONBF, 0); // unbuffered
        std::setvbuf(stderr, nullptr, _IONBF, 0);
    }
}

int main(int /*argc*/, char* /*argv*/[]) {
    initLogging();

    std::printf("Starting Y.A.W.N — Yet Another Audio Workstation New\n");
    std::fflush(stdout);

    auto app = std::make_unique<yawn::App>();

    if (!app->init()) {
        std::fprintf(stderr, "Failed to initialize application\n");
        std::fflush(stderr);
        return 1;
    }

    app->run();
    app->shutdown();

    std::printf("Y.A.W.N shutdown complete\n");
    std::fflush(stdout);
    return 0;
}
