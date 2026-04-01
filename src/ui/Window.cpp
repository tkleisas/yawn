#include "ui/Window.h"
#include "util/Logger.h"
#include <glad/gl.h>

namespace yawn {
namespace ui {

Window::~Window() {
    destroy();
}

Window::Window(Window&& other) noexcept
    : m_window(other.m_window)
    , m_glContext(other.m_glContext)
{
    other.m_window = nullptr;
    other.m_glContext = nullptr;
}

Window& Window::operator=(Window&& other) noexcept {
    if (this != &other) {
        destroy();
        m_window = other.m_window;
        m_glContext = other.m_glContext;
        other.m_window = nullptr;
        other.m_glContext = nullptr;
    }
    return *this;
}

bool Window::create(const WindowConfig& config) {
    Uint32 flags = SDL_WINDOW_OPENGL;
    if (config.resizable) flags |= SDL_WINDOW_RESIZABLE;
    if (config.maximized) flags |= SDL_WINDOW_MAXIMIZED;

    m_window = SDL_CreateWindow(
        config.title.c_str(),
        config.width,
        config.height,
        flags
    );

    if (!m_window) {
        LOG_ERROR("UI", "Failed to create SDL window: %s", SDL_GetError());
        return false;
    }

    m_glContext = SDL_GL_CreateContext(m_window);
    if (!m_glContext) {
        LOG_ERROR("UI", "Failed to create GL context: %s", SDL_GetError());
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
        return false;
    }

    // Load OpenGL functions via glad
    int version = gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress);
    if (!version) {
        LOG_ERROR("UI", "Failed to load OpenGL functions via glad");
        SDL_GL_DestroyContext(m_glContext);
        SDL_DestroyWindow(m_window);
        m_glContext = nullptr;
        m_window = nullptr;
        return false;
    }

    LOG_INFO("UI", "OpenGL %d.%d loaded", GLAD_VERSION_MAJOR(version), GLAD_VERSION_MINOR(version));

    // Enable vsync
    SDL_GL_SetSwapInterval(1);

    return true;
}

void Window::destroy() {
    if (m_glContext) {
        SDL_GL_DestroyContext(m_glContext);
        m_glContext = nullptr;
    }
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
}

void Window::makeCurrent() {
    if (m_window && m_glContext) {
        SDL_GL_MakeCurrent(m_window, m_glContext);
    }
}

void Window::swap() {
    if (m_window) {
        SDL_GL_SwapWindow(m_window);
    }
}

int Window::getWidth() const {
    int w = 0;
    if (m_window) SDL_GetWindowSize(m_window, &w, nullptr);
    return w;
}

int Window::getHeight() const {
    int h = 0;
    if (m_window) SDL_GetWindowSize(m_window, nullptr, &h);
    return h;
}

} // namespace ui
} // namespace yawn
