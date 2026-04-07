#ifdef YAWN_HAS_VST3

#include "vst3/VST3EditorWindow.h"
#include "pluginterfaces/base/funknown.h"
#include <iostream>

namespace yawn {
namespace vst3 {

// ── PlugFrameAdapter ──

Steinberg::tresult PLUGIN_API PlugFrameAdapter::resizeView(
    Steinberg::IPlugView* view, Steinberg::ViewRect* newSize)
{
    if (!m_owner || !newSize) return Steinberg::kInvalidArgument;

    int w = newSize->getWidth();
    int h = newSize->getHeight();
    m_owner->resizeToView(w, h);

    if (view)
        view->onSize(newSize);

    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API PlugFrameAdapter::queryInterface(
    const Steinberg::TUID iid, void** obj)
{
    if (!obj) return Steinberg::kInvalidArgument;
    *obj = nullptr;

    if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::IPlugFrame::iid) ||
        Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid)) {
        addRef();
        *obj = static_cast<Steinberg::IPlugFrame*>(this);
        return Steinberg::kResultOk;
    }
    return Steinberg::kNoInterface;
}

// ── VST3EditorWindow ──

VST3EditorWindow::~VST3EditorWindow() {
    close();
}

#ifdef _WIN32

bool VST3EditorWindow::s_classRegistered = false;

static const wchar_t* kWindowClassName = L"YawnVST3Editor";

LRESULT CALLBACK VST3EditorWindow::wndProc(HWND hwnd, UINT msg,
                                             WPARAM wp, LPARAM lp)
{
    auto* self = reinterpret_cast<VST3EditorWindow*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return 0;
        }
        case WM_CLOSE:
            if (self) self->close();
            return 0;
        case WM_DESTROY:
            return 0;
        case WM_SIZE:
            if (self && self->m_plugView) {
                RECT rc;
                GetClientRect(hwnd, &rc);
                Steinberg::ViewRect vr;
                vr.left = 0;
                vr.top = 0;
                vr.right = rc.right;
                vr.bottom = rc.bottom;
                self->m_plugView->onSize(&vr);
            }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool VST3EditorWindow::createNativeWindow(const std::string& title,
                                            int width, int height)
{
    HINSTANCE hInst = GetModuleHandleW(nullptr);

    if (!s_classRegistered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = wndProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = kWindowClassName;
        if (!RegisterClassExW(&wc)) return false;
        s_classRegistered = true;
    }

    // Convert title to wide string
    int wLen = MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, nullptr, 0);
    std::wstring wTitle(wLen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, &wTitle[0], wLen);

    // Calculate window size including non-client area
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rc = {0, 0, width, height};
    AdjustWindowRect(&rc, style, FALSE);

    m_hwnd = CreateWindowExW(
        0, kWindowClassName, wTitle.c_str(), style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hInst,
        this  // Pass as lpParam for WM_CREATE
    );

    if (!m_hwnd) return false;

    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);
    return true;
}

void VST3EditorWindow::destroyNativeWindow() {
    if (m_hwnd) {
        // Clear the user data first to prevent re-entrant close()
        SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, 0);
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

void VST3EditorWindow::resizeToView(int width, int height) {
    if (!m_hwnd) return;

    DWORD style = static_cast<DWORD>(GetWindowLongPtrW(m_hwnd, GWL_STYLE));
    RECT rc = {0, 0, width, height};
    AdjustWindowRect(&rc, style, FALSE);
    SetWindowPos(m_hwnd, nullptr, 0, 0,
                 rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void VST3EditorWindow::pollEvents() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.hwnd) {
            wchar_t className[64] = {};
            GetClassNameW(msg.hwnd, className, 64);
            if (wcscmp(className, kWindowClassName) == 0 ||
                GetAncestor(msg.hwnd, GA_ROOT) != msg.hwnd) {
                // Message belongs to a VST3 editor window or its children
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
                continue;
            }
        }
        // Put non-editor messages back for SDL to handle
        PostMessageW(msg.hwnd, msg.message, msg.wParam, msg.lParam);
        break;
    }
}

#else
// Stub implementations for non-Windows platforms

bool VST3EditorWindow::createNativeWindow(const std::string&, int, int) {
    std::cerr << "[VST3] Plugin editor windows not yet supported on this platform\n";
    return false;
}

void VST3EditorWindow::destroyNativeWindow() {}

void VST3EditorWindow::resizeToView(int, int) {}

void VST3EditorWindow::pollEvents() {}

#endif // _WIN32

// ── Cross-platform lifecycle ──

bool VST3EditorWindow::open(VST3PluginInstance* instance,
                              const std::string& title)
{
    if (m_isOpen) close();
    if (!instance || !instance->controller()) return false;

    m_instance = instance;

    // Create the plugin view
    m_plugView = instance->createView();
    if (!m_plugView) {
        std::cerr << "[VST3] Plugin '" << title << "' has no editor view\n";
        return false;
    }

    // Check platform support
#ifdef _WIN32
    if (m_plugView->isPlatformTypeSupported(Steinberg::kPlatformTypeHWND)
        != Steinberg::kResultOk) {
        std::cerr << "[VST3] Plugin does not support HWND platform type\n";
        m_plugView = nullptr;
        return false;
    }
#endif

    // Create the IPlugFrame
    m_plugFrame = Steinberg::owned(new PlugFrameAdapter(this));
    m_plugView->setFrame(m_plugFrame.get());

    // Query preferred size
    Steinberg::ViewRect viewRect = {};
    if (m_plugView->getSize(&viewRect) != Steinberg::kResultOk) {
        viewRect.left = 0;
        viewRect.top = 0;
        viewRect.right = 800;
        viewRect.bottom = 600;
    }

    int w = viewRect.getWidth();
    int h = viewRect.getHeight();
    if (w <= 0 || h <= 0) { w = 800; h = 600; }

    // Create native window
    if (!createNativeWindow(title, w, h)) {
        m_plugView->setFrame(nullptr);
        m_plugView = nullptr;
        m_plugFrame = nullptr;
        return false;
    }

    // Attach the plugin view to the native window
#ifdef _WIN32
    if (m_plugView->attached(m_hwnd, Steinberg::kPlatformTypeHWND)
        != Steinberg::kResultOk) {
        std::cerr << "[VST3] Failed to attach plugin view\n";
        destroyNativeWindow();
        m_plugView->setFrame(nullptr);
        m_plugView = nullptr;
        m_plugFrame = nullptr;
        return false;
    }
#endif

    m_isOpen = true;
    return true;
}

void VST3EditorWindow::close() {
    if (!m_isOpen && !m_plugView) return;

    if (m_plugView) {
        m_plugView->removed();
        m_plugView->setFrame(nullptr);
        m_plugView = nullptr;
    }

    destroyNativeWindow();

    m_plugFrame = nullptr;
    m_instance = nullptr;
    m_isOpen = false;
}

} // namespace vst3
} // namespace yawn

#endif // YAWN_HAS_VST3
