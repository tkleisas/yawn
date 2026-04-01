#include "app/App.h"
#include "util/Logger.h"
#include <cstdio>
#include <cstdlib>

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

int main(int /*argc*/, char* /*argv*/[]) {
    initLogging();

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
