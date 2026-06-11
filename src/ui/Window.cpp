#include "ui/Window.h"
#include "ui/GlCaps.h"
#include "util/Logger.h"
#include <glad/gl.h>
#include <cstdio>

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

namespace {

// One configuration in the context-creation fallback chain. The
// renderer needs the GL 3.3 *feature set*; it does not need a core
// profile specifically, a deep depth buffer (3D rendering happens in
// the visual engine's own FBOs), or stencil (clipping is scissor-
// based). So when the strict request fails we progressively relax:
// some Windows-10-era Intel ICDs reject exact pixel formats or the
// WGL_ARB_create_context core-profile path but still hand back a
// perfectly usable 3.3+ compatibility context via a legacy request.
struct GLAttempt {
    const char* desc;
    bool requestCore;   // set 3.3 core version/profile attributes
    int  depthBits;
};

constexpr GLAttempt kGLAttempts[] = {
    {"GL 3.3 core, 24-bit depth", true,  24},
    {"GL 3.3 core, default depth", true, 16},
    {"driver-default legacy context", false, 16},
};

} // namespace

bool Window::create(const WindowConfig& config) {
    Uint32 flags = SDL_WINDOW_OPENGL;
    if (config.resizable) flags |= SDL_WINDOW_RESIZABLE;
    if (config.maximized) flags |= SDL_WINDOW_MAXIMIZED;

    std::string lastError;

    for (const auto& attempt : kGLAttempts) {
        // Framebuffer attributes bind to the WINDOW's pixel format on
        // Windows, so each attempt recreates the window, not just the
        // context.
        SDL_GL_ResetAttributes();
        if (attempt.requestCore) {
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                                 SDL_GL_CONTEXT_PROFILE_CORE);
        }
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, attempt.depthBits);

        m_window = SDL_CreateWindow(
            config.title.c_str(), config.width, config.height, flags);
        if (!m_window) {
            lastError = SDL_GetError();
            LOG_WARN("UI", "Window creation failed (%s): %s",
                     attempt.desc, lastError.c_str());
            continue;
        }

        m_glContext = SDL_GL_CreateContext(m_window);
        if (!m_glContext) {
            lastError = SDL_GetError();
            LOG_WARN("UI", "GL context failed (%s): %s",
                     attempt.desc, lastError.c_str());
            SDL_DestroyWindow(m_window);
            m_window = nullptr;
            continue;
        }

        // Load OpenGL functions via glad and verify we actually got
        // at least the GL 3.1 feature set (the renderers' hard floor —
        // GLSL 1.40, VAOs, FBOs, TBOs; chosen so Sandy Bridge-era
        // Windows drivers and Raspberry Pi 4/5 Mesa v3d qualify). A
        // legacy request can succeed yet hand back GL 1.1 (Microsoft
        // Basic Display Adapter, RDP sessions), which would crash on
        // the first missing entry point instead of failing cleanly
        // here.
        const int version = gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress);
        const int maj = GLAD_VERSION_MAJOR(version);
        const int min = GLAD_VERSION_MINOR(version);
        if (!version || maj < 3 || (maj == 3 && min < 1)) {
            char buf[128];
            std::snprintf(buf, sizeof(buf),
                          "driver provides OpenGL %d.%d but Y.A.W.N needs 3.1+",
                          maj, min);
            lastError = buf;
            LOG_WARN("UI", "GL version check failed (%s): %s",
                     attempt.desc, lastError.c_str());
            SDL_GL_DestroyContext(m_glContext);
            SDL_DestroyWindow(m_window);
            m_glContext = nullptr;
            m_window = nullptr;
            continue;
        }

        // Publish the context's capabilities for the renderers: on a
        // 3.1/3.2 context they switch to GLSL 1.40 preambles and use
        // ARB-extension or CPU fallbacks for swizzle / instancing.
        GlCaps::major = maj;
        GlCaps::minor = min;
        GlCaps::extTextureSwizzle  = GLAD_GL_ARB_texture_swizzle != 0;
        GlCaps::extInstancedArrays = GLAD_GL_ARB_instanced_arrays != 0;

        // Log the adapter details — the single most useful line in a
        // user's yawn.log when graphics misbehave on their machine.
        const char* vendor   = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
        const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        const char* glver    = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        LOG_INFO("UI", "OpenGL %d.%d loaded via '%s' — %s | %s | %s",
                 maj, min, attempt.desc,
                 vendor ? vendor : "?", renderer ? renderer : "?",
                 glver ? glver : "?");

        // Enable vsync
        SDL_GL_SetSwapInterval(1);
        return true;
    }

    LOG_ERROR("UI", "All GL context attempts failed; last error: %s",
              lastError.c_str());

    // The user can't be expected to find yawn.log — tell them what's
    // wrong and what to do in a dialog (plain Win32/desktop message
    // box; works without any GL).
    std::string msg =
        "Y.A.W.N could not create an OpenGL 3.1 graphics context.\n\n"
        "Last error: " + lastError + "\n\n"
        "This usually means the graphics driver is missing or outdated "
        "(Windows' built-in 'Microsoft Basic Display Adapter' has no "
        "OpenGL support), or the app is running over Remote Desktop or "
        "in a virtual machine.\n\n"
        "Please install the latest graphics driver from your GPU vendor "
        "(Intel, NVIDIA, or AMD) and try again.";
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                              "Y.A.W.N — Graphics initialization failed",
                              msg.c_str(), nullptr);
    return false;
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
