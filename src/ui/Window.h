#pragma once

#include <SDL3/SDL.h>
#include <string>
#include <memory>

namespace yawn {
namespace ui {

struct WindowConfig {
    std::string title = "Y.A.W.N";
    int width = 1280;
    int height = 800;
    bool resizable = true;
    bool maximized = false;
};

class Window {
public:
    Window() = default;
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&& other) noexcept;
    Window& operator=(Window&& other) noexcept;

    bool create(const WindowConfig& config);
    void destroy();

    void makeCurrent();
    void swap();

    // Dump the current back buffer to a PNG (glReadPixels + stb).
    // Call after the frame is fully rendered, before swap(). Returns
    // false when the window/back buffer isn't readable.
    bool captureToPng(const std::string& path);

    SDL_Window* getHandle() const { return m_window; }
    SDL_GLContext getGLContext() const { return m_glContext; }
    bool isOpen() const { return m_window != nullptr; }

    int getWidth() const;
    int getHeight() const;

private:
    SDL_Window* m_window = nullptr;
    SDL_GLContext m_glContext = nullptr;
};

} // namespace ui
} // namespace yawn
