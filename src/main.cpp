#include "app/App.h"
#include <cstdio>

int main(int /*argc*/, char* /*argv*/[]) {
    std::printf("Starting Y.A.W.N — Yet Another Audio Workstation New\n");

    yawn::App app;

    if (!app.init()) {
        std::fprintf(stderr, "Failed to initialize application\n");
        return 1;
    }

    app.run();
    app.shutdown();

    return 0;
}
