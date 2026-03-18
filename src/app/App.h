#pragma once

#include "ui/Window.h"
#include <functional>

namespace yawn {

class App {
public:
    App() = default;
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    bool init();
    void run();
    void shutdown();

    bool isRunning() const { return m_running; }

private:
    void processEvents();
    void update();
    void render();

    ui::Window m_mainWindow;
    bool m_running = false;
};

} // namespace yawn
